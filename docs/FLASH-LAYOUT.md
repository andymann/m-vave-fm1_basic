# Flash layout and package format

## 1 MiB SPI flash: stock V15 / V16

| Range | Contents |
|---|---|
| `0x00000–0x03FFF` | **Boot head**: JieLi flash header (32-byte records, cipher key 0xFFFF) → `uboot.boot` @0xA0 (14 488 B in V16), `isd_config.ini` @0x3938, record `app_dir_head` → 0x4000, record `key_mac` → 0xFF000. **Never write this** unless you're doing a full restore. |
| `0x04000–0x92FFF` | Firmware region V15 (V16 runs to `0xA8FFF`) |
| `0x93000–0xE8FFF` | Stock settings store (JieLi "VM"); sector 0x93000 starts `55 AA AA 55` on a factory unit |
| `0xE9000–0xE9FFF` | Per-unit Bluetooth data (**unit-unique, keep**) |
| `0xEA000–0xFBFFF` | Factory voices / preset resources (only restorable from a backup) |
| `0xFC000–0xFEFFF` | Erased at the factory; Felucca-family globals |
| `0xFF000–0xFFFFF` | `key_mac` (unit-unique, keep) |

## FM1 Open layout (built by `sdk/Package.cs`)

| Range | Contents |
|---|---|
| `0x0000` | Boot head from `sdk/tools/pack` (`uboot.boot`, `boot-config.bin` = isd_config) |
| `0x4000` | `app_area_head`, then `app.bin` @0x4120 = `.text`+`.data`+`.ram0_data`+`.cache_ram_data`, `cfg_tool.bin`, `PRCT` |
| after app | VM (≥ 32 KB), BTIF (4 KB) |
| `0x7D000–0xFEFFF` | **USER** partition, 536 576 B: `fm1_flash_read/write/erase` (4 KB sectors). Projects, sample region (466 944 B), DX7 library |

Entry point `_start` = **0x02000120** = flash 0x4120 (XIP: CPU `0x02000000 + X − 0x4000`). The stock SPL starts the app without verifying it. Build.ps1 refuses any other entry.
Audio and scan code must be in SRAM (`FM1_AUDIO_CODE`, section `.volatile_ram_code`, checked to sit in 0x01C00000–0x01C80000) so it keeps running while flash is being programmed.

## `.fwsc` package

1. **M-VAVE interleave**: the first 20 blocks of 48 bytes are 47 data bytes plus 1 marker byte.
   marker = `product[i] + i + 1`, and 125 (`}`) means end of string. Remove the markers to get the UFW image (file size = UFW + 20).
2. **UFW header** (64 B, JieLi LFSR XOR cipher, key 0xFFFF, step `k = (k<<1) ^ (k&0x8000 ? 0x1021 : 0)`):
   - crc16 @0 over bytes 2..63
   - crc16 of the file table @2
   - total size @4, file count @8
   - chip name `AC791N` @16
3. **File table**: 80-byte entries at 64, same cipher. Fields: type @0, crc16 @4, offset @8, size @12, stored @16, encrypted @24, name @64.
   - type 0 = `flash.bin` (at 0x400, multiple of 4 KB)
   - type 100 = `ota.bin` (vendor loader, < 32 KB)
   - stock packages add `USR` (50, factory voices, encrypted), `isd_config.ini` (52), `script.ver`, `blimit.bin`, `tail.bin`
4. CRC16 = CCITT, poly 0x1021, init 0.
5. Inside `flash.bin`, the app area at 0x4000 is ciphered in 32-byte blocks with key `0x980F ^ (offset >> 2)`.

Inspect any package offline: `python3 tools/fm1tool.py inspect FILE.fwsc`.

Known hashes:

| Package | SHA-256 |
|---|---|
| Stock V15 `FM-1_015` | `db1642b2…7edb8a` (firmware region `6edf3c37…255686`) |
| Stock V16 `FM-1_016` | `d017fb65…b61d5` (the `../FM-1.fwsc` in this folder) |
| Vendor loader `ota.bin` | `ef1f2db8…004541` |

Identity namespaces: stock `FM-1_0xx`, Felucca family `FM-1_9xxxxxx`, FM1 Open `FM-1_8xxxxxx` (required by Build.ps1 / WebUpdate.js). Don't reuse a stock identity.

The flash driver of any bare-metal firmware must accept **both** flash IDs (0x856014 and 0xB36014), or saves and updates fail on 2026 units.
