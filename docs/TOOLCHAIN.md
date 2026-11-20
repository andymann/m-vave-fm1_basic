# Toolchains and build paths

There are two ways to write FM-1 firmware. Both are set up in this folder.

| | **A. Bare metal on fm1-base** (`firmware/`) | **B. Official FM1 Open SDK** (`fm1_open/`) |
|---|---|---|
| What you own | Everything: audio engine, UI, MIDI, storage | UI, MIDI routing, storage, board drivers. The sound engine, RTOS, BT and USB stack are the closed `libfm1_engine.a` |
| Licence | fm1-base / felucca-bsp MPL-2.0: your app can use any licence | GPLv3 + SDK linking exception; engine binary licensed for FM1 only |
| Device build on macOS | Yes: JieLi Linux toolchain in Docker (linux/amd64, Rosetta), driven by `base/tools/build.py` | No: Windows x64 + JieLi Windows toolchain 2.5.3 (VM) |
| Fast loop on macOS | Host simulator (native clang, PNG/WAV output, scripted tests), browser emulator (`--web`, needs emcc) | WASM simulator (`scripts/build-sim-mac.sh`, ~2 s) |
| Install | `base/tools/fm1_install.py X.fwsc` (USB-MIDI) | `tools/fm1tool.py flash X.fwsc` or your own workbench HTML |

Pick **A** for "my own firmware". Pick **B** to build on M-VAVE's instrument (DX7/303/808/PSG/pluck/sampler engine + sequencer).

## A. fm1-base (bare metal)

One-time setup (downloads ~26 MB toolchain + 3 SDK files, all hash-pinned in `base/tools/toolchain.lock`):

```bash
cd firmware/base && tools/setup_toolchain.sh --only jieli,ac79-sdk,python
```

Docker Desktop must be running: on macOS, `build.py` runs the JieLi tools in a pinned distroless linux/amd64 container.

```bash
make fw          # firmware/app/build/myfm1.fwsc
make host        # host simulator + firmware/app/tests/basic.txt
make web         # browser emulator (needs emscripten: brew install emscripten)
make install     # flash over USB-MIDI (fm1_install.py)
```

Toolchain facts:
- JieLi "clang 4.0.1 for pi32v2"; `pi32v2/bin/clang -target pi32v2`, `pi32v2/bin/ld`, `common/bin/objcopy`
- Only `uboot.boot`, `cfg_tool.bin` and `cfg/eq_cfg_hw.bin` from AC79 SDK tag `AC79NN_SDK_V1.2.1_2023-12-13` (commit d179b448) go into the package
- fm1-base also builds the update loader (`ota.bin`, RAM image at 0x01C0A800) and packs the `.fwsc` (`fm1pkg_make.py`)

Native alternative: [jvitkauskas/pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm) ships clang 23 + ld.lld for pi32v2 with `macos-arm64` builds (`-mcpu=ac791n`, no libc). Felucca, fm1-base, SLOOP and m-bootloader are reported to build and run with it. Not wired into this setup.

CPU gotchas (felucca-bsp `docs/CPU.md`, Dinghy `docs/RULES.md`):
- 64-bit multiplies mixed with 16-bit loads can corrupt a register: keep int64 multiplies in `noinline` helpers
- fm1-base compiles with `-mcpu=r3 -mfprev1 -ffp-contract=off` (single-precision FPU) and its build rejects `double`. Felucca/Dinghy build without `-mcpu` (no FPU, fixed point, no 64-bit division)
- division by zero does not trap
- don't reprogram the PLL; keep PB1 analog

## B. FM1 Open SDK

macOS, simulator only:
```bash
make sim         # fm1_open/build/preview/index.html, then: make sim-serve → http://127.0.0.1:8173
```

The firmware has to be built in Windows (Parallels / VMware Fusion / UTM with Windows 11 ARM: its x64 emulation runs the JieLi tools):
1. Install the [JieLi toolchain 2.5.3](https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe) (installation guide: https://doc.zh-jieli.com/Tools/zh-cn/dev_tools/dev_env/jl_toolchain.html). Expected `C:\JL\pi32\bin\clang.exe` sha256 `d4bc97cf…bce`; the build checks all 22 files against `sdk/manifest.json`.
2. Share this folder with the VM and run `windows\build-firmware.ps1`. It mirrors `fm1_open` to `C:\fm1`, runs `sdk\Build.ps1 -FirmwareOnly`, and copies the `.fwsc` + `build-identity.json` to `out\`.
3. On the Mac: `make flash-open` (= `tools/fm1tool.py flash out/latest.fwsc --identity-json out/latest-build-identity.json`).

The `.fwsc` identity must match `FM-1_8NNNNNN` (`app/version.h`). The Build.ps1 checks (toolchain hashes, SDK manifest, DSP contracts, SRAM placement of audio symbols) are there to protect you: don't patch them out.

## Wine?

Untested. Build.ps1 also hard-codes Windows path separators, so a VM is the reliable route.
