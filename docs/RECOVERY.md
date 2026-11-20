# Recovery: read this before flashing anything

The AC791N mask ROM always has a USB boot mode (`WL82 UBOOT1.00`, 4C4A:8057) that can read and write all of the flash. A bad firmware is recoverable as long as the chip can reach that mode.

## Before your first custom flash

1. **Full 1 MiB backup**, taken in ROM boot. It contains your unit's Bluetooth data (0xE9000), factory voices (0xEA000–0xFBFFF) and key_mac (0xFF000), and only a backup can bring those back:
   ```bash
   cd ../MvaveFM1Unbricker
   sudo -v && .venv/bin/python fm1_unbrick.py softkey --yes && sudo .venv/bin/python fm1_unbrick.py backup ~/fm1-backups/factory
   ```
   (or M-Flasher: https://jvitkauskas.github.io/m-apps/flasher/)
2. Keep **stock V15** around: `fm1_open/sdk/firmware/FM-1_015.fwsc` (sha256 `db1642b2…`). Use V15 for `restore`, because V16 runs past the region `restore` writes.
3. Optional: an RP2040 board (XIAO RP2040 / RP2040-Zero) for the [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter). It forces ROM boot when no firmware answers.
4. Optional: [m-bootloader](https://github.com/jvitkauskas/m-bootloader) (MIT). It replaces the stock SPL: hold REC at power-on for download mode, PLAY for a recovery menu, and it CRC-checks the app before starting it. This changes the boot head, so read its docs first.

## Getting into ROM boot

| Situation | Do this |
|---|---|
| Stock V15 / Felucca-family firmware running | `python3 tools/fm1tool.py rom-boot` (sends `F0 22 24 35 7D F7`) |
| Your own fm1-base firmware | build in an entry (felucca-bsp: same SysEx), and keep the 2-failed-boots → UBOOT guard |
| Black screen | hold OCT− + OCT+ (+HOME) for 5–10 s at power-on; or 3 quick crash-boots; leave it on for 2 min (watchdog guard) |
| Nothing on USB | FM-1 Transporter (USB_KEY 0x16EF on D+/D−) or a second FM-1 with [m-unbricker](https://github.com/jvitkauskas/m-unbricker) |

On macOS the ROM loader resets after ~10 s of USB silence. `fm1_macusb.py` sends keep-alives. Start the tool right after entering ROM boot.

## Restore

```bash
cd ../MvaveFM1Unbricker
sudo .venv/bin/python fm1_unbrick.py restore ../development/fm1_open/sdk/firmware/FM-1_015.fwsc   # firmware region only
sudo .venv/bin/python fm1_unbrick.py full-restore --image ~/fm1-backups/factory/full-flash-before.bin  # everything
```

Rules:
- Never `erasechip`.
- Never write below 0x4000 unless you mean to replace the boot head.
- Never write another unit's 0xE9000 / 0xFF000.
