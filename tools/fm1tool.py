#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""fm1tool - macOS/Linux host tool for the M-VAVE FM1 (JieLi AC791N).

Python port of the FM1 Open updater protocol (fm1_open/sdk/WebUpdate.js):
  * "00 59" frames, 7-bit LSB-first packed inside SysEx, over USB MIDI
  * device-driven pull: the device sends read requests (cmd 0x30) and the
    host answers with slices of the de-interleaved .fwsc image

Commands
  ports                         list MIDI ports
  identity                      query identity (cmd 0x11)
  cap                           query FM1 Open capability (cmd 0x31)
  inspect FILE.fwsc             validate and describe a package (offline)
  flash FILE.fwsc [--build-id ID | --identity-json build-identity.json]
                                install a package (same steps as the workbench)
  rom-boot                      send F0 22 24 35 7D F7: stock V15 / Felucca-family
                                drop into the mask-ROM "WL82 UBOOT1.00" disk
                                (recover with MvaveFM1Unbricker / jl-uboot-tool)

Flashing writes the firmware region of the device. Keep USB connected and the
battery charged. Back up projects/voices/samples first (workbench).
"""
import argparse
import hashlib
import json
import re
import sys
import time

# --------------------------------------------------------------------------
# Package (.fwsc) parsing  -- WebUpdate.js inspectPackage
# --------------------------------------------------------------------------

STOCK_V15_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
VENDOR_LOADER_SHA256 = "ef1f2db89b011fa53e71df363f0122de0ea896618c53fb53e8da676322004541"
OPEN_IDENTITY = re.compile(r"^(FM1_OFFICIAL_OPEN|FM1_COMMUNITY_\d+|FM-1_8\d{6})$")
ENTER_LOADER = bytes([0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7])   # vendor OTA loader
ENTER_ROMBOOT = bytes([0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7])  # mask-ROM UBOOT (stock/Felucca)
FINISH_STAGE1 = 0xE0000000
FINISH_STAGE2 = 0xF0000000


class Fm1Error(Exception):
    pass


def check(ok, msg):
    if not ok:
        raise Fm1Error(msg)


def le(p, at, n):
    return int.from_bytes(bytes(p[at:at + n]), "little")


def crc16(data, c=0):
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def jl_cipher(data, key=0xFFFF):
    out, k = bytearray(data), key
    for i in range(len(out)):
        out[i] ^= k & 0xFF
        k = ((k << 1) ^ (0x1021 if k & 0x8000 else 0)) & 0xFFFF
    return bytes(out)


def ascii_z(p):
    return bytes(p).split(b"\0")[0].decode("ascii", "replace")


def inspect_package(raw: bytes):
    check(1024 <= len(raw) <= 2 * 1024 * 1024, "package length invalid")
    image = bytearray()
    product, ended = "", False
    for i in range(20):
        image += raw[i * 48:i * 48 + 47]
        marker = raw[i * 48 + 47]
        if marker == 125:
            ended = True
        else:
            c = (marker - i - 1) & 0xFF
            check(not ended and 32 <= c <= 126, "package identity invalid")
            product += chr(c)
    image += raw[960:]
    image = bytes(image)
    head = jl_cipher(image[:64])
    count = le(head, 8, 2)
    check(le(head, 0, 2) == crc16(head[2:]) and le(head, 4, 4) == len(image), "UFW header CRC/size mismatch")
    check(ascii_z(head[16:32]) == "AC791N" and 0 < count <= 12, "chip name or entry count invalid")
    table = image[64:64 + count * 80]
    check(crc16(table) == le(head, 2, 2), "UFW file table CRC mismatch")
    entries = []
    for i in range(count):
        e = jl_cipher(table[i * 80:i * 80 + 80])
        ent = dict(type=le(e, 0, 2), crc=le(e, 4, 2), offset=le(e, 8, 4), size=le(e, 12, 4),
                   stored=le(e, 16, 4), encrypted=le(e, 24, 4), name=ascii_z(e[64:]))
        check(ent["offset"] + ent["size"] <= len(image), "entry out of range: " + ent["name"])
        data = image[ent["offset"]:ent["offset"] + ent["size"]]
        check(ent["encrypted"] or crc16(data) == ent["crc"], "entry CRC mismatch: " + ent["name"])
        ent["data"] = data
        entries.append(ent)
    flash = [e for e in entries if e["type"] == 0]
    loader = [e for e in entries if e["type"] == 100]
    check(len(flash) == 1 and 16384 <= flash[0]["size"] <= 1044480 and flash[0]["size"] % 4096 == 0,
          "flash.bin entry invalid")
    check(len(loader) == 1 and 0 < loader[0]["size"] < 32768, "ota.bin loader missing/invalid")
    return dict(product=product, image=image, entries=entries, flash=flash[0]["data"],
                loader=loader[0]["data"], sha256=hashlib.sha256(raw).hexdigest(),
                loader_sha256=hashlib.sha256(loader[0]["data"]).hexdigest())


# --------------------------------------------------------------------------
# Framing  -- WebUpdate.js pack7 / frame / parse
# --------------------------------------------------------------------------

def pack7(data):
    out, bits, value = [], 0, 0
    for b in data:
        value |= b << bits
        bits += 8
        while bits >= 7:
            out.append(value & 127)
            value >>= 7
            bits -= 7
    if bits:
        out.append(value & 127)
    return out


def frame(command, payload=b""):
    payload = bytes(payload)
    check(len(payload) <= 520, "payload too long")
    s = sum(payload) & 0xFF
    raw = [0, 0x59, command, len(payload) & 0xFF, len(payload) >> 8, 0, *payload, (~s) & 0xFF]
    return [0xF0, *pack7(raw), 0xF7]


def parse(msg):
    if not msg or len(msg) < 10 or len(msg) > 608 or msg[0] != 0xF0 or msg[-1] != 0xF7:
        return None
    raw, bits, value = [], 0, 0
    for b in msg[1:-1]:
        if b > 127:
            return None
        value |= b << bits
        bits += 7
        if bits >= 8:
            raw.append(value & 0xFF)
            value >>= 8
            bits -= 8
    if bits >= 7 or value or len(raw) < 7 or raw[0] != 0 or raw[1] != 0x59 or le(raw, 3, 3) != len(raw) - 7:
        return None
    if sum(raw[6:]) & 0xFF != 0xFF:
        return None
    return raw[2], bytes(raw[6:-1])


# --------------------------------------------------------------------------
# MIDI link (python-rtmidi)
# --------------------------------------------------------------------------

APP_PORT = re.compile(r"fm.?1|^usb.?midi(?:\s+\d+)?$", re.I)
LOADER_PORT = re.compile(r"fm.?1|felucca|sloop|clipper|ota|sinco|usb.?midi|composite", re.I)


def rtmidi():
    try:
        import rtmidi as rm  # python-rtmidi
    except ImportError:
        sys.exit("python-rtmidi missing: pip install -r tools/requirements.txt")
    return rm


def list_ports():
    rm = rtmidi()
    return rm.MidiIn().get_ports(), rm.MidiOut().get_ports()


class Link:
    def __init__(self, in_idx, out_idx, name):
        rm = rtmidi()
        self.name = name
        self.mi, self.mo = rm.MidiIn(), rm.MidiOut()
        self.mi.ignore_types(sysex=False, timing=True, active_sense=True)
        self.mi.open_port(in_idx)
        self.mo.open_port(out_idx)
        self.queue = []
        self.mi.set_callback(self._rx)

    def _rx(self, event, _data=None):
        p = parse(event[0])
        if p:
            self.queue.append(p)

    def send(self, data):
        self.mo.send_message(list(data))

    def read(self, timeout):
        end = time.time() + timeout
        while not self.queue and time.time() < end:
            time.sleep(0.002)
        return self.queue.pop(0) if self.queue else None

    def query(self, command, payload=b"", timeout=1.5):
        self.queue.clear()
        self.send(frame(command, payload))
        end = time.time() + timeout
        while time.time() < end:
            r = self.read(min(0.25, end - time.time()))
            if r and r[0] == command:
                return r[1]
        raise TimeoutError("no reply to cmd 0x%02X" % command)

    def close(self):
        for p in (self.mi, self.mo):
            try:
                p.close_port()
            except Exception:
                pass


def open_device(loader=False, force=False, port=None):
    """Return (link, identity) of exactly one responding FM1, or (None, None)."""
    ins, outs = list_ports()
    pat = LOADER_PORT if loader else APP_PORT
    found = []
    for i, name in enumerate(ins):
        if port is not None and port.lower() not in name.lower():
            continue
        if port is None and not force and not pat.search(name):
            continue
        o = [j for j, n in enumerate(outs) if n == name]
        if not o:  # macOS CoreMIDI names usually match; fall back to single output
            o = [0] if len(ins) == 1 and len(outs) == 1 else []
        if len(o) != 1:
            continue
        link = Link(i, o[0], name)
        try:
            for attempt in range(2):
                try:
                    r = link.query(0x11, timeout=0.75)
                    break
                except TimeoutError:
                    if attempt:
                        raise
            ident = ascii_z(r)
            check(len(r) == 27 and ident, "invalid identity reply")
            found.append((link, ident))
        except (TimeoutError, Fm1Error):
            link.close()
    if len(found) > 1:
        for l, _ in found:
            l.close()
        raise Fm1Error("several FM1-like devices answered; connect only one")
    return found[0] if found else (None, None)


def capability(data):
    check(len(data) == 47 and data[:4] == b"FMC1" and data[4] == 1 and data[5] <= 7
          and le(data, 8, 2) == 512 and le(data, 10, 4) == 1048576, "capability reply mismatch")
    check(data[7] == 1, "unknown loader")
    return dict(family=data[5], state=data[6], build=data[14:46].hex(), error=data[46])


# --------------------------------------------------------------------------
# Update
# --------------------------------------------------------------------------

def serve(link, image, finish, idle, phase):
    covered = bytearray((len(image) + 7) // 8)
    total, started, last = 0, time.time(), time.time()
    while True:
        check(time.time() - started < max(idle * 3, 120), "update exceeded maximum duration")
        check(time.time() - last < idle, "device stopped requesting data; update incomplete")
        pkt = link.read(0.25)
        if not pkt:
            continue
        cmd, p = pkt
        if cmd != 0x30 or len(p) != 8:
            continue
        addr, n = le(p, 1, 4), le(p, 5, 3)
        check(0 < n <= 512, "invalid request length")
        if addr in (FINISH_STAGE1, FINISH_STAGE2):
            check(addr == finish and n == 8, "device is in a different update stage")
            ok = bytes(p) + b"success\0"
            link.send(frame(0x30, ok))
            end = time.time() + 0.6
            while time.time() < end:
                again = link.read(0.1)
                if again and again[0] == 0x30 and le(again[1], 1, 4) == finish:
                    link.send(frame(0x30, bytes(again[1]) + b"success\0"))
            print("\r%s: done            " % phase)
            return
        check(addr + n <= len(image), "device read past end of package")
        last = time.time()
        link.send(frame(0x30, bytes(p) + image[addr:addr + n]))
        for i in range(addr, addr + n):
            if not covered[i >> 3] & (1 << (i & 7)):
                covered[i >> 3] |= 1 << (i & 7)
                total += 1
        print("\r%s: %3d%%" % (phase, min(99, total * 100 // len(image))), end="", flush=True)


def wait_for(identity, any_ota=False, timeout=40, force=False):
    end = time.time() + timeout
    while time.time() < end:
        try:
            link, ident = open_device(loader=True, force=force)
        except Fm1Error:
            raise
        if link:
            if ident == identity or (any_ota and ident.lower().startswith("ota-")):
                return link, ident
            link.close()
        time.sleep(0.6)
    raise Fm1Error("timeout waiting for %s to reconnect" % identity)


def cmd_flash(args):
    raw = open(args.package, "rb").read()
    pkg = inspect_package(raw)
    product = pkg["product"]
    stock = pkg["sha256"] == STOCK_V15_SHA256
    build_id = args.build_id
    if args.identity_json:
        build_id = json.load(open(args.identity_json, encoding="utf-8-sig"))["build_id"]
    check(stock or OPEN_IDENTITY.match(product), "package identity %r is not an FM1 Open identity" % product)
    if pkg["loader_sha256"] == VENDOR_LOADER_SHA256:
        loader_identity = "ota-FM-1_015"
    elif stock:
        loader_identity = "ota-" + product
    else:
        check(build_id and re.fullmatch(r"[0-9a-f]{64}", build_id), "--build-id/--identity-json required for this loader")
        loader_identity = "ota-FM1C_" + build_id[:16]

    link, ident = open_device(loader=True, force=args.force)
    check(link, "no FM1 answered the identity query")
    print("device:", ident, "via", link.name)
    print("package:", product, pkg["sha256"])
    if stock and ident == "FM-1_015":
        sys.exit("device already runs stock V15 (restoring factory voices is done by the workbench)")

    mode = None
    if ident == loader_identity or (args.force and ident.lower().startswith("ota-")):
        mode = "loader"
    elif ident == "FM-1_015" or stock:
        check(args.force, "stock/legacy route needs --force")
        mode = "legacy"
    else:
        try:
            cap = capability(link.query(0x31))
            mode = "tagged"
        except TimeoutError:
            check(args.force, "device has no FM1 Open capability (third-party firmware?); use --force to migrate")
            mode = "legacy"

    if args.dry_run:
        print("dry run: would install in mode", mode)
        link.close()
        return
    if not args.yes:
        if input("Type FLASH to write %s to %s: " % (product, ident)).strip() != "FLASH":
            sys.exit("aborted")

    try:
        if mode != "loader":
            if mode == "tagged":
                check(cap["state"] == 0, "device busy with another update")
                check(build_id, "--build-id/--identity-json needed for an FM1 Open device")
                check(cap["build"] != build_id, "device already runs this build")
                n = len(pkg["image"])
                ack = link.query(0x32, bytes([1, cap["family"]]) + n.to_bytes(4, "little") + bytes.fromhex(build_id))
                check(ack == b"\0", "device refused the package")
            link.send(ENTER_LOADER)
            serve(link, pkg["image"], FINISH_STAGE1, 10, "staging loader")
            link.close()
            link, ident = wait_for(loader_identity, any_ota=(mode == "legacy" or stock), force=args.force)
        link.send(ENTER_LOADER)
        serve(link, pkg["image"], FINISH_STAGE2, 180, "writing firmware")
        link.close()
        link, ident = wait_for(product, timeout=60, force=args.force)
        print("rebooted as", ident)
        if not stock and build_id:
            after = capability(link.query(0x31))
            check(after["build"] == build_id, "device rebooted but build id differs")
            print("build id verified")
    finally:
        if link:
            link.close()


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="substring of the MIDI port name to use")
    ap.add_argument("--force", action="store_true", help="accept any MIDI port / legacy route")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("ports")
    sub.add_parser("identity")
    sub.add_parser("cap")
    p = sub.add_parser("inspect")
    p.add_argument("package")
    p = sub.add_parser("flash")
    p.add_argument("package")
    p.add_argument("--build-id")
    p.add_argument("--identity-json", help="build/dx7/build-identity.json from the firmware build")
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--yes", action="store_true")
    p = sub.add_parser("rom-boot")
    p.add_argument("--yes", action="store_true")
    args = ap.parse_args()

    try:
        if args.cmd == "ports":
            ins, outs = list_ports()
            print("inputs: ", ins)
            print("outputs:", outs)
        elif args.cmd == "inspect":
            pkg = inspect_package(open(args.package, "rb").read())
            print("product :", pkg["product"])
            print("sha256  :", pkg["sha256"], "(stock V15)" if pkg["sha256"] == STOCK_V15_SHA256 else "")
            print("loader  :", pkg["loader_sha256"], "(vendor)" if pkg["loader_sha256"] == VENDOR_LOADER_SHA256 else "")
            for e in pkg["entries"]:
                print("  type %3d %-14s off 0x%06X size 0x%06X%s" % (e["type"], e["name"], e["offset"], e["size"],
                      " encrypted" if e["encrypted"] else ""))
            print("flash.bin ends at flash offset 0x%X" % len(pkg["flash"]))
        else:
            link, ident = open_device(loader=True, force=args.force, port=args.port) if args.cmd != "flash" else (None, None)
            if args.cmd == "identity":
                check(link, "no FM1 answered")
                print(ident, "via", link.name)
            elif args.cmd == "cap":
                check(link, "no FM1 answered")
                print(ident, capability(link.query(0x31)))
            elif args.cmd == "rom-boot":
                check(link, "no FM1 answered")
                print("device:", ident)
                if args.yes or input("Type UBOOT to reboot into mask-ROM boot: ").strip() == "UBOOT":
                    link.send(ENTER_ROMBOOT)
                    print("sent; the unit should appear as USB disk 'WL82 UBOOT1.00' (VID 4C4A PID 8057)")
            elif args.cmd == "flash":
                cmd_flash(args)
            if link:
                link.close()
    except (Fm1Error, TimeoutError) as e:
        sys.exit("error: %s" % e)


if __name__ == "__main__":
    main()
