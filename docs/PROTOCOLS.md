# FM1 USB-MIDI protocols

Reference implementations:
- updater: `fm1_open/sdk/WebUpdate.js` (browser) and `tools/fm1tool.py` (Python port)
- device side: `fm1_open/app/transfer.c`

## A. Update / identity protocol ("00 59")

- **Frame**: raw = `00 59 cmd lenLo lenHi 00 payload… chk`, where chk = `~sum(payload) & 0xFF`.
  - The whole raw buffer is packed **LSB-first into 7-bit bytes** (bit accumulator) and wrapped as `F0 … F7`.
  - Payload ≤ 520 B; frame ≤ 608 MIDI bytes.
- **Parse**: require raw[0]=0, raw[1]=0x59, le24(raw[3..5]) = len−7, and sum(raw[6..]) ≡ 0xFF.

| cmd | Direction | Meaning |
|---|---|---|
| 0x11 | host→dev | Identity → 27-byte ASCII. Examples: `FM-1_015` (stock V15), `FM-1_016`, `FM-1_8000014` (Open V14), `ota-FM-1_015` (vendor loader), `ota-FM1C_<16 hex>` |
| 0x31 | host→dev | Open capability → 47 B: `FMC1`, ver 1, family (7 = FM1), state (0 = idle), loader 1, u16 512 max chunk, u32 1048576 flash, build_id[32] @14, error @46 |
| 0x32 | host→dev | Arm update: `[1, family, size u32 LE, build_id[32]]`, reply `[0]` = OK |
| 0x30 | dev→host | **Read request**: `[seq, addr u32, len u24]` (len ≤ 512). Host answers cmd 0x30 with the same 8 bytes + `image[addr:addr+len]`. The device paces the transfer. |
| 0x30 finish | dev→host | addr `0xE0000000` (loader staged) / `0xF0000000` (firmware written), len 8. Host answers with the 8 bytes + `"success\0"` |
| 0x21 / 0x22 | host→dev | Stock V15 voice-bank write (factory voice restore), ack = cmd 0 `[0]` |

Raw (unframed) SysEx:
- `F0 22 24 35 7F F7` enters the OTA loader.
- `F0 22 24 35 7D F7` enters mask-ROM boot (stock/Felucca).

**Install sequence** (`Updater.install`):
1. Query 0x11, then 0x31 (Open only).
2. 0x32 arm.
3. Send `7F` key, then serve reads until 0xE0000000.
4. The device re-enumerates as `ota-…` (≤ 40 s).
5. Send `7F` key again, then serve reads until 0xF0000000 (≤ 180 s idle).
6. Reboot, then verify the identity and the 0x31 build_id.

Stock V15 has no 0x31: the "legacy" route skips step 2. The image served is the **de-interleaved** `.fwsc` (UFW image), not raw flash. The loader picks the entries it needs.

## B. Project / library protocol (`F0 7D 46 4D 02 …`)

- **Frame**: `F0 7D 'F' 'M' 02 cmd tokLo7 tokHi7 pack7(payload) chk F7`, where sum(7D…chk) ≡ 0 mod 128.
- **Reply**: cmd|0x40, same token, data[0] = status (0 OK, 1 empty, 71 invalid, 72 no memory, 73 sample space busy).
- One request at a time; replies are ≤ 128 B and cached, so retries are idempotent.

| cmd | Function |
|---|---|
| 0 | info `[4, slots=16, cur, projBytes u16, clock, 8× source type]` |
| 1 / 2 | begin read `[kind,target]` / read chunk `[off u16, n≤64]` (kind 0 project, 1 DX7 voice 156 B, 2 timbre 223 B, 3 tuned project, 4 DX7 bank 4096 B) |
| 3 / 4 / 5 / 6 | begin write / write chunk (sequential, ≤64 B) / commit `[crc32]` / cancel |
| 7 / 8 / 9 / 10 | save slot / load slot / slot info (12-char name) / rename |
| 11–16 | sample status / begin / write `[off u32, ≤128 B]` / finish / read / cancel |
| 17–22 | engine/render/audio/master stats, tuning capability, extension capability |

`fm1_app_sysex()` also handles MMC (`F0 7F xx 06 cmd F7`) and DX7 single-voice dumps (`F0 43 0n 00 01 1B …`).

File formats: project `FM1P`, timbre `FM1T`, sample bank `.fm1s` (`FM1S`, 32 kHz, SEA5 codec chunks), standard Yamaha DX7 VMEM/VCED.

## C. Mask-ROM boot (JieLi UBOOT)

USB mass-storage device `WL82 UBOOT1.00` (VID 0x4C4A PID 0x8057). Vendor SCSI commands carry the loader upload and flash read/erase/write.
Implemented in [kagaimiq/jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) (`wl82loader.bin`). On macOS use `../MvaveFM1Unbricker/fm1_macusb.py` (libusb BOT, keep-alive needed: the chip resets after ~10 s of USB silence).
