# M-VAVE FM-1: custom firmware workspace

The FM-1 is a **JieLi AC791N** (WL82, dual-core PI32v2 at 360 MHz, ~512 KB SRAM, 1 MiB SPI flash, no SDRAM). It has an ST7789 240×240 screen, a 41-key/button matrix with LEDs, 7 encoders, an I2S DAC, USB MIDI+audio, TRS MIDI in, and BLE. Updates go over USB-MIDI SysEx, with no signature check, and the mask ROM always offers a USB recovery mode.

**Agents: start with [HANDOVER.md](HANDOVER.md).**

## Layout

| Path | What |
|---|---|
| `firmware/` | **Your firmware**. `app/` is **CLOCKBOX**, a port of [ClockBox v3](https://github.com/Andymann/clockbox-v3) (MIDI master clock; manual: `firmware/app/docs/MANUAL.md`); `base/` is [fm1-base](https://github.com/charlesvestal/fm1-base) as a submodule (with felucca-bsp inside) |
| `fm1_open/` | M-VAVE's official [FM1 Open V14](https://github.com/m-vave/fm1_open) SDK + app source (submodule) |
| `tools/fm1tool.py` | macOS host tool: MIDI ports, identity, capability, `.fwsc` inspect, flash (updater protocol), ROM boot |
| `scripts/build-sim-mac.sh` | Native macOS build of the FM1 Open WASM simulator (the official one is Windows-only) |
| `windows/build-firmware.ps1` | FM1 Open firmware build inside a Windows VM |
| `docs/` | [HARDWARE](docs/HARDWARE.md) · [FLASH-LAYOUT](docs/FLASH-LAYOUT.md) · [PROTOCOLS](docs/PROTOCOLS.md) · [TOOLCHAIN](docs/TOOLCHAIN.md) · [RECOVERY](docs/RECOVERY.md) · [ECOSYSTEM](docs/ECOSYSTEM.md) |
| `toolchains/` | WASI SDK 27 (downloaded on first `make sim`; git-ignored) |
| `../MvaveFM1Unbricker` | ROM-boot backup/restore (jl-uboot-tool), outside this repo: `git clone https://github.com/Quixotic7/MvaveFM1Unbricker` |

## Quick start

```bash
git clone --recursive https://github.com/andymann/m-vave-fm1_basic.git && cd m-vave-fm1_basic
brew install fribidi      # Pillow font rendering for fm1-base
```

```bash
make venv                 # once: .venv with Pillow, python-rtmidi, mido
make host                 # build + test your firmware in the host simulator → firmware/app/build/host/*.png, *.wav
```
Then edit `firmware/app/app.c` and repeat.

For hardware:
```bash
make toolchain            # once: JieLi Linux toolchain + 3 AC79 SDK files (hash-pinned), start Docker Desktop first
make fw                   # firmware/app/build/clockbox.fwsc
```
Then read [docs/RECOVERY.md](docs/RECOVERY.md) and take the full flash backup.
```bash
make install              # flash over USB-MIDI
```

To work on M-VAVE's own instrument instead (path B), run `make sim-serve` and open http://127.0.0.1:8173. For the firmware, see [docs/TOOLCHAIN.md](docs/TOOLCHAIN.md#b-fm1-open-sdk).

## Before you publish
- `id_prefix` in `firmware/app/fm1.toml` is `FM-1_130977` (dev builds `FM-1_13097700`); fill in author/repo/licence.
- Before release, replace fm1-base/felucca-bsp's USB test IDs (pid.codes 1209:0001/0002).
- Ship `LICENSES/Apache-2.0.txt` with every package: the boot files come from the JieLi SDK.
