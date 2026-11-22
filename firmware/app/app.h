/* SPDX-License-Identifier: MPL-2.0 */
/* CLOCKBOX for the M-VAVE FM-1: a port of ClockBox v3 (https://github.com/Andymann/clockbox-v3), a MIDI master clock
 * with quantized restart, follow modes, tap tempo and tempo presets, onto fm1-base.
 * This header is the app's half of the contract with the platform (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "CLOCKBOX"
#define APP_SPLASH_RGB RGB(250, 200, 40)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz (the sync pulses only) */
#define APP_TICK                         /* the clock engine runs in the 1 kHz timer interrupt */
void app_tick(void);
#define APP_DIM_LEVELS                   /* LED brightness: the dim layer's pulse width (plat_dim_ns) */

/* the store: the settings, the panel calibration (the platform's), Bluetooth's address and pairing; flash offsets */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the saved settings (or defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: the sync pulses */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                  /* the first screen, after the splash */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message on the screen and to a screen reader */

/* the host simulator's checks (host/plat_host.c "expect"): bpm, mode, playing, qrs, ... (app.c app_expect) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
