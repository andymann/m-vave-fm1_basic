# FM1 hardware reference

Sources: `fm1_open/app/board/*.h`, `fm1_open/sdk/include/fm1.h`, `sdk/config/linker.ld.in`, the stock
`isd_config.ini` decoded from `FM-1.fwsc`, MvaveFM1Unbricker. "Inferred" means not stated in a source.

## SoC

| Item | Value | Source |
|---|---|---|
| SoC | **JieLi AC791N** ("WL82" family, AC79 series), BT Classic + BLE | Package.cs header `AC791N`; ROM boot USB name `WL82 UBOOT1.00` |
| CPU | JieLi **PI32v2**, compiler target `-target pi32v2 -mcpu=r3 -mfprev1` (FPU) | Build.ps1 |
| Clock | 24 MHz crystal. `isd_config.ini` (V16) says `SYS_CLK` 320 MHz, but Melodee reads the SPL's PLL2 as ×270 → 540 MHz VCO, **SYS 360 MHz** (HSB 180 / LSB 60 / SFC 90). Some units bricked after a 320 MHz retune: **don't touch the PLL** | isd_config.ini; Melodee `firmware/hal/fm1_clock.h` |
| Cores | **Dual core**: cpu1 starts when cpu0 writes its entry to `0x01C7FFF8` and sets bit 3 of `0x10008`. Stock renders voices on cpu1. Some units report a flaky core 1 | joakimeriksson/fm1-emu `docs/stock-firmware.md` |
| Accelerators | single-precision FPU, FFT, AES, SHA, CRC16 (0x13500), RNG (0x13B00) | fm1-emu |
| Internal SRAM | `ram0` 0x01C00000 … ~0x01C7FE00 (~511 KB); boot info 52 B above; `UPDATA_BEG = 0x1C7FE00-128` | linker.ld.in |
| Cache RAM | 0x01F20000 + (8 − free I-cache ways)·4 KB | linker.ld.in |
| SDRAM | none (`ENABLE_SDRAM 0`, `SDRAM_SIZE 0`) | isd_config.ini |
| Flash | 1 MiB SPI NOR on SPI0 (0x11C00), 4 KB sectors, 256 B pages. XIP: **CPU 0x02000000 = flash offset 0x4000** (so 0x02000120 = flash 0x4120). IDs `0x856014` (Puya, older) / `0xB36014` (2026 units) | unbricker |
| Chip key | `0x980F` (JieLi "PublicCode", used for app-area cipher) | jl-uboot-tool / Package.cs |
| Reset pin config | `PB01_08_0` | isd_config.ini |
| Retained RAM | 0x01C7C000 (Melodee stores crash records here) | unbricker |

## Peripherals / pins

| Function | Detail | Source |
|---|---|---|
| Display | **ST7789V 240×240 RGB565**, SPI1 + DMA (regs 0x11D00, IRQ 16), ~12 MHz; PC7 CS, PC8 D/C, PC9 SCK, PC10 MOSI, no reset line; **PA2 backlight, active low** | `fm1_lcd_hw.h`, `lcd.inc` |
| Key/button matrix | 41 inputs = 14 buttons + 27 keys (F3…G5); 11 col × 6 row diode matrix. Columns: two 74HC595 (PA4 data, PA3 clock, PA1 latch), clocked by **SPI2 DMA** (regs 0x11E00, IRQ 37). Rows: PA0, PA5–PA8, PB7 with pull-ups, low = pressed. IDs 0–13 buttons, 14–40 keys; 2-sample press / 8-sample release debounce; TIMER5 (0x10900, IRQ 63, PRD 600) scan at 10 kHz | `fm1_input.h`, `device.c` |
| Key LEDs | 41 LEDs multiplexed on PA9/PA10 and PH6/PH9 (`fm1_led_set(0..40)`) | `fm1_input.h` |
| Encoders | 7 (SELECT, PRESETS, ALGORITHM, K1–K4: names inferred from the panel) | `fm1.h` `encoders[7]` |
| Master pot | PB6, ADC ch 4 | `fm1_input.h` |
| Battery sense | PB1, ADC ch 3 (10-bit SARADC at 0x13100). **Holding PB1 low for 8 s resets the chip**, so keep it an analog input. Li-Po 3.7 V 2000 mAh with NTC; charger IC unknown. Stock thresholds 531/561/591 | `fm1.h`, lunar-modulator |
| Headphone detect | PB5 | felucca-bsp (reported) |
| Watchdog/reset | P33 domain: P3_WDT_CON 0x80; P3_PR_PWR 0xA0 bit 4 = chip reset | Melodee |
| Power | VDDIO 3.2 V, VDC14 1.60 V (DCDC), SYSVDD 1.38 V | Melodee `fm1_power.h` |
| Audio out | ALNK0 I2S (regs 0x12E00), 24-bit stereo, 128-frame DMA halves, IRQ 11 prio 4; PC0 MCLK (11.2896 MHz = 256 fs), PC1 SCLK, PC2 LRCLK, PC6 data out, PC3 RX (IRQ clock only). External DAC with no setup required; **part unknown** (CS4344-class guess). Hardware rate **44 118 Hz** (nominal 44.1 k) | `fm1_audio.h` |
| Audio in | none connected | `fm1_audio.h` |
| Timebase | TIMER4 24 MHz free-running (`fm1_millis`) | `device.c` |
| GPIO block | 0x50000, 16 words per port (PA=0, PB=1, PC=2, PH=7): OUT/IN/DIR/DIE/PU/PD/HD0/HD | `fm1_gpio.h` |
| MIDI | TRS MIDI **in only**: UART1 (0x12100) RX on **PH8**, 31 250 Bd, RX DMA ring, no TX pin. USB MIDI in/out; BLE MIDI peripheral/central | `fm1.h`, manual |
| USB IDs | stock app `4C4A:C755` ("FM-1 Midi"/"FM-1 Audio"); stock OTA loader `4D4A:4155` ("ota-FM-1"); mask ROM `4C4A:8057` | AL-255/FM-1-RE `docs/io/11-ota-protocol.md` |
| USB | USB MIDI + UAC (audio playback/capture) composite; app port name `FM-1` / `USB MIDI` ("DUB-1 FM-1" on macOS), loader port `ota-…`/"USB Composite Device" | WebUpdate.js, updater log |
| Bluetooth | on-SoC BT 5.0 dual mode (+ Wi-Fi radio), wire antenna; A2DP sink (SBC), BLE MIDI (up to 8 bonds) | `fm1.h` |

## Debug access

No SWD/JTAG header, no test pads, no UART console. There is no readout protection: ROM boot reads all of the flash, and the app cipher key 0x980F is public. Debug options:
- emulators: [joakimeriksson/fm1-emu](https://github.com/joakimeriksson/fm1-emu) (Rust/WASM peripheral model), [simonjohansson/fm1-emulator](https://github.com/simonjohansson/fm1-emulator) (QEMU)
- fm1_open's WASM simulator
- crash records in retained RAM 0x01C7C000
- the screen

## RAM landmarks (bare metal)

| Address | Use |
|---|---|
| `0x01C7C000–0x01C7FD50` | survives reset/UBOOT entry |
| `0x01C7FD80` | boot-state mailbox ("usb_update_mode" + P33 reset → ROM UBOOT) |
| `0x01C7FD88` | 112 B UPDATA_PARM → SPL runs staged update loader after core reset |
| `0x01C7FE00` | RAM interrupt vector table (ALNK0 11, SPI1 16, SPI2 37, TIMER5 63) |
| `0x01C7FE08` | r0 handed to the app by the SPL (boot params) |

Source: Melodee `firmware/hal/fm1_sys.h`, Felucca `firmware/app.ld`, AL-255.

## Panel names (manual)

Buttons: HOME, EDIT, SEQ, GLO, PLAY, REC, SAVE, OCT−, OCT+, ENV, LFO, FX, SCL, ARP.
`fm1_controls_read()` returns `keys` (bit 0..26 = F3..G5), `buttons` (bit 0..13), `encoders[7]`, `knob`, `battery`.

## Boot / recovery entry points

| How | Result |
|---|---|
| SysEx `F0 22 24 35 7F F7` | running firmware jumps to the vendor OTA loader (`ota.bin`), enumerates as `ota-…` |
| SysEx `F0 22 24 35 7D F7` | stock V15 / Felucca-family jump to mask-ROM boot → USB disk `WL82 UBOOT1.00` (VID 0x4C4A PID 0x8057) |
| Hold OCT− + OCT+ (+HOME) at power-on | firmware-dependent update / ROM-boot hold |
| 3 crash-boots in a row | ROM boot (SPL fallback) |
| FM-1 Transporter (XIAO RP2040 on USB D+/D−) | forces ROM boot when nothing else works |

ROM boot is in mask ROM: an update can never erase it. See [RECOVERY.md](RECOVERY.md).

## Unknowns

Still unidentified: the DAC and amp part numbers, the charger IC, the display FPC pinout, whether the flash is inside the SoC package, and whether a TAP is usable on the USB pins.
