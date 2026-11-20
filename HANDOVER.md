# HANDOVER: M-VAVE FM-1 custom firmware project

> **For the next AI agent.** This file brings you up to date on this project; read it top to bottom before acting. Date of handover: **2026-11-20**.
>
> **Labels.**
> - **[VERIFIED]**: run or checked in this workspace.
> - **[SOURCE]**: stated in a file or repo named next to it.
> - **[REPORTED]**: from a community source and not checked here.
> - **[UNKNOWN]**: nobody knows.
>
> Don't upgrade a label without new evidence.

---

## 0. TL;DR

- **Goal:** the user wants to write **their own firmware** for the **M-VAVE FM-1**, a handheld FM/drum/sample synth.
- **What this session did:**
  - researched the device and its ecosystem
  - built a macOS workspace with two development paths, docs, a host tool, and build scripts
- **Current state:**
  - the user's firmware app (`firmware/app`, fm1-base template renamed "MYFM1") **builds and passes its tests in the host simulator** [VERIFIED]
  - the official SDK's browser simulator builds natively on macOS [VERIFIED]
  - **no device firmware has been built yet**, because the JieLi toolchain is not installed
  - **nothing has been flashed or tested on hardware**
  - **no flash backup of the user's unit exists yet**
  - committed to **https://github.com/andymann/m-vave-fm1_basic** (branch `main`): one repo, with `firmware/base` (fm1-base) and `fm1_open` as pinned submodules
- **Next step:** the user must run the toolchain setup (§7.1). Then `make fw`. Before any flash, take a full flash backup (§8).

---

## 1. The user and how to work with them

- macOS (Apple Silicon, arm64, Darwin 25.6). Shell zsh. Homebrew at `/opt/homebrew`.
- The user prefers concise answers that lead with the result.
- The user is technical: they already cloned the unbricker repo and used M-VAVE's updater.
- **Ask before any action that touches the hardware:**
  - flashing
  - ROM-boot entry (`fm1tool.py rom-boot`, `fm1_unbrick.py softkey`)
  - any `fm1_unbrick.py` / jl-uboot-tool write
- **Ask before:** commits, pushes, publishing anything, creating accounts, or installing large software. The user's repo: https://github.com/andymann/m-vave-fm1_basic (the user approved the initial commit and push to it).
- **The Claude Code auto-mode classifier denied running fm1-base's `tools/setup_toolchain.sh`.** That script downloads and runs third-party binaries. Do not try to work around this: the user runs it themselves (§7.1).
- Treat the user's name and email as private. Never put them into `fm1.toml` author fields, public repos, or web requests without asking.

---

## 2. The device (summary; full detail in `docs/HARDWARE.md`)

| | |
|---|---|
| SoC | **JieLi AC791N** ("WL82", AC79 series). CPU **PI32v2** (proprietary, Blackfin-like), **dual core**, single-precision FPU [SOURCE: fm1_open, fm1-emu] |
| Clock | 24 MHz crystal. The stock SPL leaves SYS at **360 MHz** (Melodee/felucca-bsp measured), while `isd_config.ini` says 320 MHz. **Never reprogram the PLL**: units have bricked. [REPORTED] |
| RAM | ~512 KB SRAM at 0x01C00000–0x01C80000, no SDRAM [SOURCE: linker.ld.in] |
| Flash | 1 MiB SPI NOR. JEDEC **0x856014** (Puya, older units) or **0xB36014** (UCUN, 2026 units); your flash driver must accept both. XIP: CPU `0x02000000` = flash `0x4000`; app entry **0x02000120** = flash 0x4120 [SOURCE] |
| Screen | ST7789V 240×240 RGB565 on SPI1. PC7 CS, PC8 DC, PC9 SCK, PC10 MOSI. Backlight PA2, active low |
| Panel | 14 buttons + 27 keys (F3–G5) in an 11×6 diode matrix (two 74HC595 + SPI2 DMA); 41 LEDs; 7 encoders; MASTER pot PB6; battery ADC PB1 (**holding PB1 low for 8 s resets the chip**) |
| Audio | ALNK0 I2S out to an external DAC (part [UNKNOWN]). 24-bit Q23, ~44 118 Hz. No audio input |
| MIDI | USB MIDI in/out; TRS **in only** (UART1 RX PH8); BLE MIDI |
| USB IDs | stock app 4C4A:C755; stock OTA loader 4D4A:4155; **mask-ROM boot 4C4A:8057** (`WL82 UBOOT1.00` mass-storage disk). fm1-base/felucca-bsp use test IDs 1209:0001 (loader 1209:0002); **replace them before any public release** |
| Debug | No SWD/JTAG/UART. No readout protection (ROM boot reads all of flash; app cipher key 0x980F is public). Use simulators/emulators, retained-RAM crash records (0x01C7C000), and the screen |
| Recovery | The mask ROM always offers USB boot; an update can't erase it. Entry methods are in §8 and `docs/RECOVERY.md` |

**Flash map (stock).**

| Range | Contents | Rule |
|---|---|---|
| `0x0000–0x3FFF` | boot head (`uboot.boot` SPL + `isd_config.ini`) | **never write** unless restoring |
| `0x4000…` | firmware | written by updates |
| `0x93000–0xE8FFF` | settings ("VM") | |
| `0xE9000` | per-unit Bluetooth data | **never write** |
| `0xEA000–0xFBFFF` | factory voices | only restorable from a backup |
| `0xFC000–0xFEFFF` | free / globals | |
| `0xFF000` | `key_mac` | **never write** |

---

## 3. Workspace map

Root: `/Users/<USER>/Documents/m-vave-fm1/`

```
m-vave-fm1/
├── FM-1.fwsc                  stock firmware V16, identity FM-1_016, sha256 d017fb65…b61d5 [VERIFIED]
├── M-UPGRADE-FM1.app          M-VAVE's Qt updater (bundle sinco.M-UPGRADE-FM1); logs in Contents/MacOS/LOG/
├── MvaveFM1Unbricker/         git clone of Quixotic7/MvaveFM1Unbricker @331f848 (MIT); has its own .venv and jl-uboot-tool/
│                              NOTE: its logs show `setup` was never completed (pyusb missing), and no backups/ dir exists
└── development/               ← THIS WORKSPACE = github.com/andymann/m-vave-fm1_basic (clone: git clone --recursive)
    ├── HANDOVER.md            this file
    ├── README.md              overview + quick start
    ├── CLAUDE.md              standing instructions for agents (keep in sync with this file)
    ├── Makefile               all common commands (§6)
    ├── .gitmodules            submodules firmware/base (fm1-base) and fm1_open
    ├── .gitignore             ignores .venv/ toolchains/ out/ build/ __pycache__/
    ├── .claude/launch.json    preview server "fm1-sim" (python http.server :8173 on fm1_open/build/preview)
    ├── docs/
    │   ├── HARDWARE.md        SoC, pins, peripherals, RAM landmarks, debug, unknowns
    │   ├── FLASH-LAYOUT.md    flash maps (stock + FM1 Open), .fwsc/UFW format, ciphers, identities, hashes
    │   ├── PROTOCOLS.md       USB-MIDI update protocol ("00 59"), project protocol ("7D 'F' 'M' 02"), ROM boot
    │   ├── TOOLCHAIN.md       path A vs path B, setup, CPU gotchas, Windows VM route
    │   ├── RECOVERY.md        backup-first procedure, ROM-boot entry, restore commands
    │   └── ECOSYSTEM.md       curated links: official, bases, firmwares, RE, tools
    ├── tools/
    │   ├── fm1tool.py         macOS host tool (§5.3)
    │   └── requirements.txt   python-rtmidi
    ├── scripts/
    │   ├── build-sim-mac.sh   native build of FM1 Open's WASM simulator (§5.2)
    │   └── make_preview.py    assembles build/preview/index.html (port of Build-Web.ps1 -LocalPreview)
    ├── windows/
    │   └── build-firmware.ps1 FM1 Open firmware build inside a Windows VM (UNTESTED)
    ├── toolchains/            (git-ignored) wasi-sdk-27.0-arm64-macos/ for the simulator; scripts/build-sim-mac.sh downloads it if missing
    ├── .venv/                 (git-ignored) Python 3.13.18 + pillow, python-rtmidi, mido; recreate with `make venv`
    ├── fm1_open/              SUBMODULE m-vave/fm1_open @4c7496d (official SDK, path B), unmodified
    └── firmware/              ← USER'S FIRMWARE (part of this repo)
        ├── base/              SUBMODULE fm1-base @b4bdfc4 (MPL-2.0); base/bsp = felucca-bsp @cfaa97f (nested submodule)
        ├── app/               copied from base/template, renamed MYFM1 (§5.1)
        ├── firmware.json      generated by build.py (firmware description for hubs)
        └── .gitignore         build/
```

Pinned upstream commits [VERIFIED]:

| Repo | Commit |
|---|---|
| fm1_open | `4c7496daac04a95ff10d1afdcc8f9ead2dbabc41` |
| fm1-base | `b4bdfc4c8c56f86c90e9b95a416600e1d9f8b1ca` |
| felucca-bsp | `cfaa97ffbccdd380f7caedc8e8c564e762317461` |
| MvaveFM1Unbricker | `331f848c96842bd7e3d4ab4bcdc05604fe64f9a2` |

---

## 4. The two development paths (and why A is the default)

| | **A: bare metal on fm1-base** (`firmware/`) ← default | **B: official FM1 Open SDK** (`fm1_open/`) |
|---|---|---|
| Ownership | everything (engine, UI, MIDI, storage) | UI, MIDI routing, storage, board drivers. Engine/RTOS/BT/USB stack = closed `sdk/lib/libfm1_engine.a` |
| Licence | MPL-2.0 base, so the app can use any licence | GPLv3 + SDK linking exception; engine binary allowed on FM1 only |
| Device build on macOS | **yes**: JieLi Linux toolchain in a linux/amd64 Docker container, driven by `base/tools/build.py` | **no**: Windows x64 + JieLi Windows toolchain 2.5.3 (Build.ps1 hard-codes Windows paths and hashes all 22 toolchain files) |
| Fast loop on Mac | host simulator (native clang): scripted tests → PNG/WAV [VERIFIED] | WASM simulator (`make sim`, ~2 s) [VERIFIED] |
| Install | `base/tools/fm1_install.py X.fwsc` (USB-MIDI) | `tools/fm1tool.py flash` or a self-built workbench HTML |

"My own firmware" points to **A**. Use B only if the user wants M-VAVE's instrument (DX7/303/808/PSG/pluck/sampler + 8-track sequencer) as the base.

---

## 5. What was built, in detail

### 5.1 `firmware/app` (path A)

Copied from `base/template` and changed in these files:

- **`fm1.toml`**:
  - `name = "MYFM1"`
  - `id_prefix = "FM-1_77"`: **a placeholder**, which must become a prefix nobody else uses. Check thefirmwarehub.com. Dev builds identify as `FM-1_7700`, releases as `FM-1_77XXYYZZ`.
  - `usb_manufacturer = "myfm1"`, `usb_product = "MYFM1 FM-1"`, `tagline = "my own FM-1 firmware"`
  - `repo`, `author`, `install`, `site` and `screenshot` are empty. A **release build refuses** to run until they are filled.
  - `ble = true`, `ble_central = true`, `companion = true` kept from the template.
- **`app.h`**: `APP_NAME "MYFM1"`. Rest as template:
  - `APP_BLOCK 256` frames
  - `APP_TOUCH` (key velocity/pressure)
  - `APP_BTDEV`
  - `APP_STORE_MAP {0xFC000, 0xDC000, 0xDE000}`
- **`tests/basic.txt`, `tests/companion.txt`, `tests/screens.txt`, `words.tsv`**: `TEMPLATE` → `MYFM1`, and paths `template/` → `app/`.

**App contract** (functions the app implements; the platform calls them):

| Function | When it runs |
|---|---|
| `app_init` | boot |
| `app_render(int32_t *lr, uint32_t n, uint32_t master_q12)` | **audio interrupt**: n stereo frames, 24-bit; no blocking, no flash access |
| `app_panic` | silence before an update rewrites flash |
| `app_ui_init` | first screen |
| `app_frame` | ~60 Hz UI: input + redraw |
| `app_input_only` | between frames |
| `app_say` | screen message + screen reader |
| `app_file_busy`, `app_file_changed` | companion file services |
| `app_expect` | host-test hooks |

Platform API: `base/include/plat.h` (800 lines), covering `plat_keys/buttons/enc/master/leds`, `plat_midi_in/out/send`, `plat_sysex_*`, `plat_store_load/save`, `plat_data*`, `plat_ms/ticks24`, `plat_ble_*`, `plat_bta_*` (A2DP), `plat_wifi_*`, `plat_sd_*`, `plat_cpu2_*` (second core), `plat_usb_*`. Optional features are switched in `fm1.toml` (table in `base/README.md`). Design rules are in `base/DESIGN.md`: no `double`, and no hardware access outside the BSP and fm1-base device files.

**Verified:**
```bash
cd development && make host
```
It builds `firmware/app/build/host/myfm1_host`, runs `app/tests/basic.txt`, and ends with `host: 13186 ms simulated, 225 blits …` and **no "EXPECTATIONS FAILED"**. Outputs: `boot.png key.png controls.png touch.png master.wav note.wav midi_out.txt`. The screen shows "MYFM1" [VERIFIED].

Host-build prerequisites that had to be added: Pillow (in `.venv`) and **Homebrew `fribidi`** (Pillow raqm for the font generator). The Makefile exports `DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib` so Pillow finds it.

### 5.2 FM1 Open simulator on macOS (path B)

- `scripts/build-sim-mac.sh` compiles every `.c` in `app/`, `web/` and `third_party/synth` (excluding `app/board/`) with WASI SDK 27 clang, using exactly the flags from `tools/Build-Web.ps1`. `scripts/make_preview.py` then fills `sdk/Update.html.in` → `fm1_open/build/preview/index.html`.
- Verified: compiles 32 files into 288 KB wasm, and the page loads in the browser with no console errors, showing the full workbench and the instrument UI [VERIFIED].
- **Web MIDI needs a secure context.** Serve it via `make sim-serve` (http://127.0.0.1:8173), not `file://`.

### 5.3 `tools/fm1tool.py` (Python port of `fm1_open/sdk/WebUpdate.js`)

| Command | Status |
|---|---|
| `inspect FILE.fwsc` (offline) | [VERIFIED] on V15 and V16: parses identity, UFW header, entries, loader hash |
| frame/parse (pack7 7-bit SysEx) | [VERIFIED] by round-trip test |
| `ports`, `identity`, `cap` | written; not run against a device (no FM-1 was connected; `ports` returned empty lists) |
| `flash FILE.fwsc [--build-id …] [--identity-json …] [--dry-run] [--yes]` | **written but never run against hardware**. It follows `Updater.install` (§9). Run `--dry-run` first. For path A, prefer fm1-base's `fm1_install.py` |
| `rom-boot` | sends `F0 22 24 35 7D F7`; not run |

Port matching: the app uses `fm.?1|^usb.?midi`, the loader `fm.?1|felucca|sloop|clipper|ota|sinco|usb.?midi|composite`. On macOS the app port appeared as **"DUB-1 FM-1"** and the loader as **"USB Composite Device"** (M-UPGRADE log) [SOURCE]. `--port` and `--force` override the matching.

### 5.4 `windows/build-firmware.ps1` (path B, **UNTESTED**)

- Mirrors `fm1_open` to `C:\fm1\fm1_open` (robocopy) and runs `sdk\Build.ps1 -FirmwareOnly -Toolchain C:\JL\pi32`.
- Copies `FM1_OFFICIAL_OPEN.fwsc`, `build-identity.json`, the map, the elf and the reports to `out\<timestamp>\` and `out\latest*.fwsc/json`.
- Requires the JieLi Windows toolchain **2.5.3** from https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe, with `clang.exe` sha256 `d4bc97cf421c1ccc373dd8924ce88720ff7457b3f1478d15df4aeda6ae923bce`.

---

## 6. Command reference (from `development/`)

| Command | What it does | Status |
|---|---|---|
| `make venv` | recreate `.venv` (pillow, python-rtmidi, mido) | done |
| `make toolchain` | `cd firmware/base && tools/setup_toolchain.sh --only jieli,ac79-sdk,python` | **user must run** (§7.1) |
| `make host` | host simulator build + `basic.txt` | [VERIFIED] passing |
| `make fw` | device build → `firmware/app/build/myfm1.fwsc` (needs toolchain + Docker running) | not yet run |
| `make web` | browser emulator (needs `emcc`: `brew install emscripten`) | not yet run |
| `make install` | `fm1_install.py firmware/app/build/myfm1.fwsc` | **ask user first** |
| `make sim` / `make sim-serve` | FM1 Open WASM simulator | [VERIFIED] |
| `make flash-open` | flash Windows-built FM1 Open package via fm1tool | **ask user first**, untested |
| `make inspect F=x.fwsc` | offline package inspection | [VERIFIED] |
| `make identity` | query the connected FM-1's identity | not yet run |

Other host tests: `firmware/app/build/host/myfm1_host firmware/app/tests/companion.txt firmware/app/build/host`, and `screens.txt` for the manual screenshots.

---

## 7. Open tasks (in priority order)

### 7.1 Toolchain (user action, blocking `make fw`)
1. Docker Desktop must be running. It was started during the session (`docker info` returned server 29.9.3).
2. The user runs `make toolchain`. It fetches into `firmware/base/.toolchain/`, with each item checked against `base/tools/toolchain.lock`:
   - JieLi Linux toolchain `20260730.1`, sha256 `a1545cdd…47bd`
   - 3 files of the AC79 SDK at tag `AC79NN_SDK_V1.2.1_2023-12-13`, commit `d179b448…`
   - the uv-built `.venv`

   Neither `~/.jieli` nor `firmware/base/.toolchain` exists yet [VERIFIED].
3. Then `make fw`. On macOS, `build.py` runs the tools in the pinned image `gcr.io/distroless/cc-debian13:debug-nonroot@sha256:c69d16ac…` under linux/amd64 (Rosetta). Expected output: `firmware/app/build/myfm1.fwsc` plus `ota.bin`. Check it with `make inspect F=firmware/app/build/myfm1.fwsc`.

### 7.2 Back up the user's unit (blocking any flash)
- `MvaveFM1Unbricker` was never set up on this Mac: its log shows `pyusb is not installed`. Run `.venv/bin/python fm1_unbrick.py setup` in that folder (the user should approve, because it fetches jl-uboot-tool and pip packages).
- Then follow `docs/RECOVERY.md`: soft-key into ROM boot and take a **double-read full 1 MiB backup**. Store it outside the repos, e.g. `~/fm1-backups/`. Record the flash ID (0x856014 vs 0xB36014) in this file.

### 7.3 Find out what the unit is running now [UNKNOWN]
- M-UPGRADE log, 2026-11-18: the device first reported **"Version: 9681002"**, a non-stock identity, probably a Felucca-family or community firmware. M-UPGRADE then ran a full OTA with its embedded `FM-1.fwsc`, which completed successfully.
- So the unit most likely runs **stock V16 (`FM-1_016`)**. Confirm with `make identity` (needs the FM-1 powered on and connected; ask first).
- If it was a Felucca-family firmware, leftover data in `0x97000–0xDFFFF` / `0xFC000` may corrupt stock presets (see the unbricker README, "Settings corrupted").

### 7.4 First hardware milestone (path A)
1. `make fw`, then the user approves, then `make install`.
2. Verify the screen shows MYFM1, the keys make sound, and USB MIDI in/out works.
3. Verify the update path works **from MYFM1 back to stock** (fm1_install.py with V15) **before** doing any heavy development, so the escape route is proven.
4. Consider: felucca-bsp `examples/hwtest` (a smaller hardware test), and m-bootloader (REC at power-on = download mode). m-bootloader rewrites the boot head, so get explicit user consent first.

### 7.5 Before any public release
- Unique `id_prefix`; fill author/repo/licence/screenshot in `fm1.toml`.
- Replace the USB test IDs (pid.codes 1209:0001/0002).
- Ship `LICENSES/Apache-2.0.txt` for the JieLi boot files. Read `base/LICENSING.md`.
- Later commits: ask the user before each commit and push.

### 7.6 Optional improvements
- Wire up the native **pi32v2-llvm** toolchain (`jvitkauskas/pi32v2-llvm`, macos-arm64 release `pi32v2-23.1.5-5`) to drop Docker. [REPORTED] to build fm1-base.
- Run `make web` (needs emscripten) for the browser emulator of MYFM1.
- Try the Windows VM build for path B; update §5.4 with the results.
- Add a `fm1tool.py` mock-device test (similar to MvaveFM1Unbricker's MockUboot).

---

## 8. Safety and recovery (must know)

**Entering mask-ROM boot** (USB disk `WL82 UBOOT1.00`, 4C4A:8057):

| Situation | How |
|---|---|
| Stock V15/V16 or Felucca family running | SysEx `F0 22 24 35 7D F7` (`fm1tool.py rom-boot`, or `fm1_unbrick.py softkey`) |
| At power-on | hold OCT− + OCT+ (+HOME) for 5–10 s (firmware-dependent) |
| Crash loop | 3 crash-boots in a row fall back to ROM boot |
| Nothing on USB | FM-1 Transporter (RP2040 sending USB_KEY 0x16EF on D+/D−), or m-unbricker with a second FM-1 |

On macOS the ROM loader drops after ~10 s of USB silence. `MvaveFM1Unbricker/fm1_macusb.py` sends keep-alives; run its commands immediately after entering ROM boot, with `sudo`.

Restore commands:
- `fm1_unbrick.py restore V15.fwsc` writes 0x4000..0x92FFF. **Use V15** (`fm1_open/sdk/firmware/FM-1_015.fwsc`); `restore` refuses V16.
- `full-restore` rewrites the boot head too.

**Hard rules:**
- never `erasechip`
- never write below 0x4000 (except full-restore)
- never write another unit's 0xE9000 / 0xFF000
- always back up first

---

## 9. Protocol essentials (full detail: `docs/PROTOCOLS.md`)

- **Update framing ("00 59"):**
  - raw `00 59 cmd lenLo lenHi 00 payload chk`, where `chk = ~sum(payload)`
  - the whole raw buffer is 7-bit packed LSB-first inside `F0…F7`
  - payload ≤ 520, frame ≤ 608
- **Commands:**
  - 0x11 identity (27 B ASCII)
  - 0x31 FM1-Open capability (47 B, `FMC1`)
  - 0x32 arm
  - 0x30 is a **device→host read request** `[seq, addr u32, len u24 ≤512]`. The host echoes the 8 bytes plus `image[addr:addr+len]`.
  - Finish markers: addr 0xE0000000 (loader staged) / 0xF0000000 (written), answered with `"success\0"`.
- **Sequence:**
  1. identity
  2. (cap, arm)
  3. `F0 22 24 35 7F F7` → serve until 0xE0000000
  4. device re-enumerates as `ota-…`
  5. `7F` again → serve until 0xF0000000
  6. reboot → verify
- The served image is the **de-interleaved .fwsc**, not raw flash.
- **.fwsc layout:**
  1. the first 20×48 B carry 47 data bytes plus an identity marker byte each (`char = marker−i−1`, 125 = end)
  2. then a JieLi UFW: 64 B header + 80 B entries, LFSR-XOR cipher key 0xFFFF, CRC16-CCITT init 0, chip "AC791N"
  3. `flash.bin` (type 0) at UFW offset 0x400; `ota.bin` (type 100) is the loader
  4. the app area inside flash.bin is ciphered per 32 B with key `0x980F ^ (off>>2)`
- **Identity namespaces:** stock `FM-1_0xx`, FM1 Open `FM-1_8xxxxxx`, Felucca family `FM-1_9xxxxxx`, fm1-base apps `id_prefix+digits`. M-UPGRADE accepts only `NAME_` + 1–3 digits, so fm1-base answers the plain handshake with a truncated identity.

Known hashes:

| Item | SHA-256 |
|---|---|
| V15 .fwsc | `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a` (firmware region `6edf3c37…255686`) |
| V16 .fwsc | `d017fb65e70aca97ee29d944470c3515a74581df5c0576bf70de917c7b2b61d5` |
| Vendor `ota.bin` | `ef1f2db89b011fa53e71df363f0122de0ea896618c53fb53e8da676322004541` |

---

## 10. Gotchas learned this session

1. **V16 ≠ V15.** `../FM-1.fwsc` is V16, whose firmware runs to 0xA8FFF. Tools written for V15 (`restore`, Transporter) refuse it or would write half a firmware.
2. **fm1-base uses the FPU.** It builds with `-mcpu=r3 -mfprev1 -ffp-contract=off`, so `float` is fine and `double` is rejected by build.py. Felucca/Dinghy build without `-mcpu` (fixed point). An earlier version of these docs had this wrong; it is corrected now.
3. **CPU erratum:** int64 multiplies mixed with 16-bit loads can corrupt a register. Keep them in `noinline` helpers (felucca-bsp `docs/CPU.md`). Division by zero doesn't trap. Some units have a flaky core 1.
4. **Audio and scan code must run from RAM** while flash is being written (`FM1_AUDIO_CODE` in FM1 Open; RAMTEXT in felucca-bsp).
5. FM1 Open's `Build.ps1` integrity checks (toolchain hashes, SDK manifest, DSP contracts, SRAM symbol ranges) must not be patched out.
6. Facts from different sources conflict on: the clock (240/320/360 MHz), whether the DAC is internal or external, and whether PC6 carries audio data or a mute signal. Felucca-bsp and Melodee measured on hardware; FM-1-RE is static analysis of V13. Prefer the measured values.
7. The `file://` preview could not be opened by the in-app browser, and Web MIDI needs a secure context. Use the `fm1-sim` launch config / `make sim-serve`.
8. Pillow's font rasterizer needs `fribidi` (Homebrew) for fm1-base's `gen_font.py`.
9. `fm1_open`'s zip kit uses backslash paths; use the git clone instead.

---

## 11. Key references (more in `docs/ECOSYSTEM.md`)

- Official:
  - https://github.com/m-vave/fm1_open (start with `sdk/include/fm1.h`, `sdk/WebUpdate.js`, `sdk/Package.cs`, `sdk/config/linker.ld.in`, `app/board/*.h`)
  - https://open.m-vave.com/fm1-workbench
- Path A docs, inside this workspace:
  - `firmware/base/README.md`, `DESIGN.md`, `LICENSING.md`, `include/plat.h`, `template/README.md`
  - `firmware/base/bsp/docs/{BOARD_IO,FLASH,CPU}.md`, `bsp/boards/fm1/*.h`
- Reference firmwares:
  - hugelton/Felucca (`BUILDING.md`)
  - keremimo/melodee (`firmware/hal/*`)
  - zvenson/dinghy (`docs/RULES.md`)
- Hardware RE:
  - ip2k/lunar-modulator `docs/01-hardware.md`
  - AL-255/FM-1-RE `docs/io/11-ota-protocol.md`
  - joakimeriksson/fm1-emu
- Toolchain: jvitkauskas/pi32v2-isa, pi32v2-llvm.
- Recovery:
  - Quixotic7/MvaveFM1Unbricker
  - kurogedelic/FM-1-transporter
  - jvitkauskas/m-flasher, m-bootloader, m-unbricker
  - Quixotic7/FM1Backups
- Hub / lists: https://thefirmwarehub.com, cicloid/awesome-fm-1.

---

## 12. First moves for the incoming agent

1. Read `CLAUDE.md`, then skim `docs/`.
2. On a fresh machine: `git clone --recursive https://github.com/andymann/m-vave-fm1_basic.git`, `brew install fribidi`, `make venv`. Here, the checkout is `/Users/<USER>/Documents/m-vave-fm1/development`. `make host` should pass; if not, check `.venv` and `brew list fribidi`.
3. Ask the user whether `make toolchain` has been run. If yes, run `make fw` and `make inspect F=firmware/app/build/myfm1.fwsc`.
4. Before any hardware step, confirm a full backup exists (§7.2) and get explicit consent.
5. Keep this file and `CLAUDE.md` updated as facts move from [REPORTED]/[UNKNOWN] to [VERIFIED].
