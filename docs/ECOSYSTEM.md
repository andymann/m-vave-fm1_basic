# FM-1 ecosystem: where to look

## Official
- https://open.m-vave.com/fm1-workbench: Web-MIDI workbench (simulator, projects/voices/samples, firmware update, "Source + SDK" kit)
- https://github.com/m-vave/fm1_open: FM1 Open V14 source + binary SDK (cloned in `fm1_open/`)
- https://forum.m-vave.com · https://www.m-vave.com/download (V15 / V16)

## Bases for your own firmware
- [charlesvestal/fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0): platform, host sim, browser emu, BLE/WiFi/USB audio/SD (**used in `firmware/`**)
- [hugelton/felucca-bsp](https://github.com/hugelton/felucca-bsp) (MPL-2.0): chip + board layer, USB, OTA, `examples/hwtest` (in `firmware/base/bsp`)
- [zvenson/dinghy](https://github.com/zvenson/dinghy) (GPL-3.0): smallest complete firmware; `docs/RULES.md`
- [konsumer/foma](https://github.com/konsumer/foma): Docker-only mini firmware

## Firmwares to read
- [hugelton/Felucca](https://github.com/hugelton/Felucca) (GPL-3.0): the reference community firmware; `BUILDING.md`, `web/EDITOR_PROTOCOL.md`
- [keremimo/melodee](https://github.com/keremimo/melodee) (GPL-3.0): richest HAL (`firmware/hal/*`), dual core, `docs/BOOT-SAFETY.md`
- [Quixotic7/ChoralRootFM1](https://github.com/Quixotic7/ChoralRootFM1), [isod89/sloop-fm1](https://github.com/isod89/sloop-fm1), [charlesvestal/fm1-x0x](https://github.com/charlesvestal/fm1-x0x), [charlesvestal/fm1-ctl](https://github.com/charlesvestal/fm1-ctl)
- Lists: [cicloid/awesome-fm-1](https://github.com/cicloid/awesome-fm-1), [aroum/fm1-custom-fw](https://github.com/aroum/fm1-custom-fw), https://thefirmwarehub.com

## Reverse engineering / emulation / toolchain
- [ip2k/lunar-modulator `docs/01-hardware.md`](https://github.com/ip2k/lunar-modulator/blob/main/docs/01-hardware.md): best hardware summary
- [AL-255/FM-1-RE](https://github.com/AL-255/FM-1-RE): stock firmware analysis, OTA protocol
- [joakimeriksson/fm1-emu](https://github.com/joakimeriksson/fm1-emu): full dual-core emulator that runs `.fwsc` files (browser/native/plugin)
- [jvitkauskas/pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa) / [pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm): open ISA docs + LLVM (macOS builds)
- [kagaimiq/jielie](https://github.com/kagaimiq/jielie), [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool), [jl-misctools](https://github.com/kagaimiq/jl-misctools)
- JieLi AC79 SDK: https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK (register headers `include_lib/driver/cpu/wl82/asm/`), docs https://doc.zh-jieli.com/AC79/zh-cn/master/index.html

## Flashing / recovery
- [Quixotic7/MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker) (in `../MvaveFM1Unbricker`) · [jvitkauskas/m-flasher](https://github.com/jvitkauskas/m-flasher) · [m-bootloader](https://github.com/jvitkauskas/m-bootloader) · [m-unbricker](https://github.com/jvitkauskas/m-unbricker)
- [kurogedelic/FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) · thefirmwarehub.com/tools/rescue/
- [Quixotic7/FM1Backups](https://github.com/Quixotic7/FM1Backups): V15, V16, a factory dump
