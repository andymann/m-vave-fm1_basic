# FM-1 firmware workspace

Read HANDOVER.md (project state, open tasks) and README.md first. Reference docs are in docs/. Keep them updated when you learn something new and verified (cite a source file or URL).

- Main work happens in `firmware/app/` (fm1-base app, MPL base). Don't edit `firmware/base/` (submodule) unless the change is meant to go upstream.
- `fm1_open/` is an upstream clone of M-VAVE's official SDK. Edit it only on a branch, and never patch out Build.ps1's integrity checks.
- Fast loop: `make host` (native host simulator; writes PNG screenshots and WAVs to firmware/app/build/host, and runs app/tests/basic.txt). Keep the tests passing.
- Device build: `make fw` (Docker + JieLi Linux toolchain). Flash: `make install`. Flashing, ROM boot (`tools/fm1tool.py rom-boot`) and anything with `fm1_unbrick.py` touch the user's hardware: always ask before running them.
- Hardware rules:
  - never write flash below 0x4000, or to 0xE9000–0xE9FFF / 0xFF000+ (unit-unique)
  - don't reprogram the PLL
  - keep PB1 analog
  - audio/ISR code in RAM
  - the flash driver must accept IDs 0x856014 and 0xB36014
- CPU quirks: int64 multiplies in noinline helpers; single-precision `float` only (fm1-base builds with -mcpu=r3 -mfprev1; build.py rejects `double`); no trap on divide by zero.
