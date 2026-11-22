# CLOCKBOX

CLOCKBOX turns the M-VAVE FM-1 into a MIDI master clock: [ClockBox v3](https://github.com/Andymann/clockbox-v3) by
socialmidi.com, ported to the FM-1. 24 PPQN clock over USB and Bluetooth MIDI, Quantized Restart that re-syncs your
gear on the next downbeat without stopping, follow modes for clock from the TRS jack or USB, tap tempo, three
presets that jump or fade, and a sync pulse on the audio out.

![CLOCKBOX at 120 BPM, the four beat lights green on the one](manual/img/screen-playing.png "CLOCKBOX on the FM-1's screen")

This is version {{VERSION}}. You install it at your own risk, and the installer can put M-VAVE's own firmware back.

- [Install](install/) it from Chrome or Edge, with the FM-1 connected by USB.
- The [Manual](manual/) explains the controls, QRS, the modes and the settings.
- [Download {{PKG}}](firmware/{{PKG}}) to use the command-line installer: `python3 tools/fm1_install.py {{PKG}}`.
- Built on fm1-base and felucca-bsp (MPL-2.0), which come from Felucca by Leo Kuroshita, Hügelton Instruments.

M-VAVE and FM-1 are trademarks of their owners. CLOCKBOX is not affiliated with M-VAVE.
