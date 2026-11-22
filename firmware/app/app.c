/* SPDX-License-Identifier: MPL-2.0 */
/* CLOCKBOX: ClockBox v3 (https://github.com/Andymann/clockbox-v3, clockbox-v3.ino 3.55) on the M-VAVE FM-1.
 *
 * A MIDI master clock at 24 PPQN with Quantized Restart (QRS), two standalone modes (QRS Stop Start, QRS Start) and
 * two follow modes (clock in from the TRS jack, or from USB, passed through), tap tempo, three tempo presets (a
 * press jumps, a hold fades), a clock divider for the sync output, LED brightness, and an update mode.
 *
 * What the FM-1 has instead of the ClockBox's hardware:
 *   6 TRS MIDI outs + USB       USB MIDI + Bluetooth MIDI (the FM-1 has no MIDI out jack)
 *   TRS MIDI in                 the TRS MIDI in jack ("DIN" in the ClockBox's words)
 *   CV clock / CV start jacks   the audio out, when SYNC OUT is on: the left channel pulses with the clock (the
 *                               divider's rate), the right channel gives the playhead-reset pulse after a stop
 *   TAP, PLAY, STOP             SEQ, PLAY, REC
 *   PRESET 1 / 2 / 3            FX, SEL, ENV (a press jumps, a hold of 1 s fades, HOME + a press stores)
 *   encoder, its click          SELECT (ALGO: steps of 5), HOME held
 *   4 beat LEDs + clock-in LED  the screen (in their colours), and the LEDs of keys F3 G3 A3 B3 and C4; PLAY's LED:
 *                               green while playing, red while a restart waits
 *   nudge (in the source, unused there)   OCT- / OCT+ held: the tempo -7.5 % / +7.5 % while held
 *
 * Timing: the engine runs in the platform's 1 kHz tick (app_tick, timer interrupt). It measures the time that has
 * passed on the 24 MHz crystal (TIMER4) and sends every clock that has fallen due: jitter under 1 ms, and no drift.
 * The host simulator steps it a millisecond at a time. The main loop only asks (the req_* flags); the tick owns the
 * transport. One unity build with the platform (device/unity.c, host/plat_host.c): gfx.c and announce.c in scope. */
#include "fastmath.h"

#define VERSION "3.55"
#define PPQN 24u
#define BAR (PPQN * 4u)

/* ------------------------------------------------------------- settings --- */
enum { M_QRS_STOPSTART, M_QRS_START, M_FOLLOW_DIN, M_FOLLOW_USB, NMODES };   /* the ClockBox's order (arrModes) */
static const char *const MODE_NAME[NMODES] = {"QRS STOP START", "QRS START", "FOLLOW DIN", "FOLLOW USB"};
static const uint8_t DIVIDER[] = {1, 2, 4, 8, 16, 32, 64};
#define NDIV ((uint32_t)sizeof DIVIDER)
#define BPM_MIN 30
#define BPM_MAX 300
#define BRIGHT_MIN 10
#define BRIGHT_MAX 200

#define SET_MAGIC 0x584F4243u            /* "CBOX" */
typedef struct {
    uint32_t magic;
    uint16_t preset[3];                  /* BPM */
    uint8_t mode;                        /* M_* */
    uint8_t qrs_offset;                  /* 1..24: the Stop this many ticks before the one (QRS STOP START) */
    uint8_t div;                         /* index into DIVIDER */
    uint8_t bright;                      /* LED brightness 10..200 */
    uint8_t sync_out;                    /* the audio sync pulses on */
    uint8_t rsv;
} settings_t;
static settings_t set;

static int standalone(void) { return set.mode == M_QRS_STOPSTART || set.mode == M_QRS_START; }

static void settings_defaults(void)      /* the ClockBox's EEPROM defaults (manual: Saved Settings) */
{
    set.magic = SET_MAGIC;
    set.preset[0] = 80;
    set.preset[1] = 100;
    set.preset[2] = 120;
    set.mode = M_QRS_STOPSTART;
    set.qrs_offset = 1;
    set.div = 0;
    set.bright = 50;
    set.sync_out = 0;
    set.rsv = 0;
}

static void settings_load(void)
{
    uint32_t i;
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC ||
        set.mode >= NMODES || set.qrs_offset < 1 || set.qrs_offset > PPQN || set.div >= NDIV ||
        set.bright < BRIGHT_MIN || set.bright > BRIGHT_MAX || set.sync_out > 1u) {
        settings_defaults();
        return;
    }
    for (i = 0; i < 3u; i++)
        if (set.preset[i] < BPM_MIN || set.preset[i] > BPM_MAX)
            set.preset[i] = (uint16_t)(80 + 20 * i);
}

/* ----------------------------------------------------- the clock engine --- */
/* main loop -> tick */
static volatile uint32_t period_ns = 2500000000u / 94u;   /* one 24 PPQN tick; the ClockBox starts at 94 BPM */
static volatile uint8_t req_start, req_stop, req_mode;    /* PLAY, STOP, a new mode */
static volatile uint8_t engine_on;                        /* after the boot screen (the ClockBox: after showInfo) */
static volatile uint8_t update_mode;                      /* STOP held at power-on: no MIDI out */
/* tick -> main loop, the screen */
static volatile uint8_t playing, qrs_wait;
static volatile uint32_t ck_tick;                         /* the generator's tick, from the start (uClock's) */
static volatile uint32_t beat_count;                      /* +1 at each divider tick (the preset fade steps on it) */
enum { L_OFF, L_GREEN, L_BLUE, L_PURPLE };
static volatile uint8_t led[4], ext_led, blink, blink_col;
static volatile uint32_t ext_ticks, ext_period_us;       /* follow modes: the clocks in, and their spacing */
static volatile uint32_t starts_out, stops_out;           /* (counts for the host's checks) */
static volatile uint32_t last_start_ms, last_stop_ms;

/* the sync pulses for the render: an event ring, the tick writes, the audio interrupt reads */
typedef struct {
    uint32_t ms;
    uint16_t width;                      /* samples */
    uint8_t ch;                          /* 0 the clock (left), 1 the playhead reset (right) */
} pulse_t;
#define NPULSE 16u
static pulse_t pulse_q[NPULSE];
static volatile uint32_t pulse_w, pulse_r;
static uint8_t reset_pending;            /* bWaitSyncStop: a reset pulse at the next divider tick */
static uint32_t now_tick_ms;             /* the tick's millisecond */

static void send_rt(uint8_t status, uint32_t to)
{
    if (!update_mode)
        plat_midi_send(0x0Fu | (uint32_t)status << 8, to);
}
#define OUTS (TO_USB | TO_BLE)

#ifdef PLAT_HOST
/* the host's measurements: the clocks out since "expect mark 1", their rate and their worst spacing error */
static uint32_t m_count, m_first, m_last, m_maxdev_us, m_on;
static uint32_t last_start_tick, last_stop_tick;
static void measure_clock(void)
{
    if (!m_on)
        return;
    if (m_count) {
        int32_t dev = (int32_t)((now_tick_ms - m_last) * 1000u) - (int32_t)(period_ns / 1000u);
        uint32_t a = (uint32_t)(dev < 0 ? -dev : dev);
        if (a > m_maxdev_us)
            m_maxdev_us = a;
    } else {
        m_first = now_tick_ms;
    }
    m_last = now_tick_ms;
    m_count++;
}
#define MEASURE_CLOCK() measure_clock()
#define MARK_START() (last_start_tick = ck_tick)
#define MARK_STOP() (last_stop_tick = ck_tick)
#else
#define MEASURE_CLOCK() ((void)0)
#define MARK_START() ((void)0)
#define MARK_STOP() ((void)0)
#endif

static void send_start(void)             /* sendMidiStart: every out (follow USB: the ones that are not USB) */
{
    send_rt(0xFA, set.mode == M_FOLLOW_USB ? TO_BLE : OUTS);
    starts_out++;
    last_start_ms = now_tick_ms;
    MARK_START();
}
static void send_stop(void)
{
    send_rt(0xFC, set.mode == M_FOLLOW_USB ? TO_BLE : OUTS);
    stops_out++;
    last_stop_ms = now_tick_ms;
    MARK_STOP();
}

static void pulse(uint32_t ch)
{
    uint32_t w = period_ns / 22676u;     /* one tick, in samples at 44.1 kHz ... */
    if (w > 661u)                        /* ... at most 15 ms (an AC-coupled output droops on longer ones) */
        w = 661u;
    if (w < 44u)
        w = 44u;
    if (pulse_w - pulse_r >= NPULSE)
        return;
    pulse_q[pulse_w % NPULSE].ms = now_tick_ms;
    pulse_q[pulse_w % NPULSE].width = (uint16_t)w;
    pulse_q[pulse_w % NPULSE].ch = (uint8_t)ch;
    pulse_w++;
}

static void leds_off(void)
{
    led[0] = led[1] = led[2] = led[3] = L_OFF;
}

/* ledIndicateMeasure */
static void led_measure(uint32_t m)
{
    if (!playing || m > 3u)
        return;
    if (qrs_wait) {
        led[m] = L_PURPLE;               /* (the ClockBox sets the pixel without clearing: they add up) */
    } else if (m == 0u) {
        led[0] = led[1] = led[2] = led[3] = L_GREEN;
    } else {
        leds_off();
        led[m] = L_BLUE;
    }
}

/* handleCVGate: the sync output at the divider's rate, the playhead reset after a stop */
static void cv_gate(uint32_t tick)
{
    uint32_t d = 6u * DIVIDER[set.div];
    if (tick % d == 0u) {
        if (playing)
            pulse(0);
        else if (reset_pending) {
            pulse(1);
            reset_pending = 0;
        }
        beat_count++;
    }
}

/* handleLED: the beat lights and the screen's blinker */
static uint32_t blink_timer = 1, measure;
static void led_tick(uint32_t tick)
{
    if (tick == 1u || tick % BAR == 0u) {            /* the one */
        blink_timer = 12;
        measure = 0;
        led_measure(measure);
        blink = 1;
        measure++;
        if (!standalone() && qrs_wait) {             /* QRS while following: Start on the one counted in */
            send_start();
            qrs_wait = 0;
        }
    } else if (tick % PPQN == 0u) {                  /* each quarter */
        blink_timer = 8;
        led_measure(measure);
        blink = 1;
        measure++;
    } else if (tick % blink_timer == 0u) {
        if (!qrs_wait) {
            leds_off();
            blink = 0;
        }
    }
}

/* ClockOutPPQN + handleClockTick: one tick of the internal clock */
static void clock_tick(uint32_t tick)
{
    send_rt(0xF8, OUTS);                             /* always, playing or not (SENDCLOCK_ALWAYS) */
    MEASURE_CLOCK();
    if (qrs_wait) {
        if (tick % BAR == BAR - set.qrs_offset && set.mode == M_QRS_STOPSTART)
            send_stop();                             /* Stop just before the one ... */
        if (tick % BAR == 0u) {
            send_start();                            /* ... Start on it */
            qrs_wait = 0;
        }
    }
    cv_gate(tick);
    led_tick(tick);
}

/* checkMidiDIN / checkMidiUSB: the clock from outside, passed straight through, counted for the lights */
static uint32_t ext_count, ext_last_ms;
static void follow_in(uint8_t st)
{
    uint32_t to = set.mode == M_FOLLOW_DIN ? OUTS : TO_BLE;   /* USB's clock goes on to the outs that are not USB */
    send_rt(st, to);
    if (st == 0xFA) {
        playing = 1;
        ext_count = 0;
        return;
    }
    if (st == 0xFC) {
        playing = 0;
        return;
    }
    /* 0xF8 */
    if (ext_count % PPQN == 0u)
        blink = 1, ext_led = 1;
    if (ext_count % PPQN == 8u)
        blink = 0, ext_led = 0;
    ext_count = ext_count > BAR - 2u ? 0u : ext_count + 1u;
    if (ext_last_ms) {                               /* the incoming tempo, for the screen (a smoothed spacing) */
        uint32_t us = (now_tick_ms - ext_last_ms) * 1000u;
        ext_period_us = ext_period_us ? (ext_period_us * 7u + us) / 8u : us;
    }
    ext_last_ms = now_tick_ms;
    ext_ticks++;
    cv_gate(ext_count);
    led_tick(ext_count);
}

static uint32_t acc_ns;
static uint8_t was_standalone;
#ifndef PLAT_HOST
static uint32_t t24_last;
#endif
void app_tick(void)
{
    uint32_t pkt, n, elapsed;
    int port;
#ifdef PLAT_HOST
    elapsed = 1000000u;                              /* the simulator steps a millisecond at a time */
#else
    {                                                /* the time that has passed, on the crystal: a tick held up (a
                                                      * flash write with interrupts off) loses nothing */
        uint32_t t = plat_ticks24(), d = t - t24_last;
        t24_last = t;
        if (d > 2400000u)                            /* (the first call, or an absurd gap: one tick) */
            d = 24000u;
        elapsed = d / 24u * 1000u + d % 24u * 1000u / 24u;
    }
#endif
    now_tick_ms = plat_ms();
    /* MIDI in: read here only (one reader); the follow modes pass the clock through, the rest is not used */
    while (plat_midi_in(&pkt, &port)) {
        uint8_t st = (uint8_t)(pkt >> 8);
        if (!engine_on || update_mode)
            continue;
        if ((set.mode == M_FOLLOW_DIN && port == PORT_TRS) || (set.mode == M_FOLLOW_USB && port == PORT_USB))
            if (st == 0xF8 || st == 0xFA || st == 0xFC)
                follow_in(st);
    }
    if (!engine_on || update_mode)
        return;
    if (req_mode) {                                  /* processModeSwitch */
        req_mode = 0;
        if (!standalone()) {
            playing = 0;                             /* stopPlaying(true) */
            qrs_wait = 0;
            send_stop();
            reset_pending = 1;
            leds_off();
            ext_count = 0;
            ext_last_ms = 0;
            ext_period_us = 0;
        } else if (!was_standalone) {
            acc_ns = 0;                              /* uClock.start(): the clock runs again from its tick 0 (not
                                                      * between the two standalone modes: the bar stays where it is) */
            ck_tick = 0;
        }
        was_standalone = (uint8_t)standalone();
        blink = 0;
        ext_led = 0;
    }
    if (req_start) {                                 /* startHandler -> startPlaying */
        req_start = 0;
        if (!standalone()) {
            qrs_wait = 1;                            /* following: a Start on the next one counted in */
        } else if (!playing) {
            send_start();
            playing = 1;
            ck_tick = 0;                             /* uClock.start(): tick 0 now */
            acc_ns = period_ns;
        } else {
            qrs_wait = 1;                            /* playing: Quantized Restart */
        }
    }
    if (req_stop) {                                  /* stopHandler -> stopPlaying */
        req_stop = 0;
        if (standalone()) {
            playing = 0;
            qrs_wait = 0;
            send_stop();
            reset_pending = 1;                       /* syncPlayheadReset */
            leds_off();
        }
    }
    if (!standalone())
        return;
    acc_ns += elapsed;
    for (n = 0; acc_ns >= period_ns && n < 32u; n++) {   /* every clock due (a burst after a held-up tick) */
        acc_ns -= period_ns;
        clock_tick(ck_tick);
        ck_tick++;
    }
}

/* the BPM the engine runs at (uClock.setTempo) */
static float bpm_now = 94.0f;
static void engine_tempo(float bpm)
{
    if (bpm < (float)BPM_MIN)
        bpm = (float)BPM_MIN;
    if (bpm > (float)BPM_MAX)
        bpm = (float)BPM_MAX;
    bpm_now = bpm;
    period_ns = (uint32_t)(2500000000.0f / bpm);     /* 60 s / (bpm * 24) */
}

/* ------------------------------------------------------- the sync output --- */
static uint32_t r_prev_ms;
static int32_t pl_left, pl_right;                    /* samples of each pulse still to go */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, now = plat_ms(), span = now - r_prev_ms, ns = 0;
    uint32_t at[8], wid[8], ch[8], k = 0;
    int32_t level = (int32_t)(7500000.0f * (float)master_q12 / 4096.0f);
    if (span == 0u || span > 50u)
        span = (n * 1000u + 22050u) / 44100u;
    while (pulse_r != pulse_w) {                     /* the pulses the tick made since the last block, in place */
        pulse_t *p = &pulse_q[pulse_r % NPULSE];
        int32_t dt = (int32_t)(p->ms - r_prev_ms);
        uint32_t off = dt > 0 ? (uint32_t)dt * n / span : 0u;
        if ((int32_t)(p->ms - now) > 0)
            break;
        if (k < 8u) {
            at[k] = off < n ? off : n - 1u;
            wid[k] = p->width;
            ch[k] = p->ch;
            k++;
        }
        pulse_r++;
    }
    r_prev_ms = now;
    for (i = 0; i < n; i++) {
        while (ns < k && at[ns] == i) {
            if (ch[ns])
                pl_right = (int32_t)wid[ns];
            else
                pl_left = (int32_t)wid[ns];
            ns++;
        }
        lr[2 * i] = set.sync_out && pl_left > 0 ? level : 0;
        lr[2 * i + 1] = set.sync_out && pl_right > 0 ? level : 0;
        if (pl_left > 0)
            pl_left--;
        if (pl_right > 0)
            pl_right--;
    }
}

void app_panic(void) { pl_left = pl_right = 0; }

/* ------------------------------------------------------------ tap tempo --- */
/* TapTempo.cpp (Damien Clarke, MIT) as the ClockBox sets it up: 4 taps averaged, a skipped tap detected, the chain
 * reset after 3 beats or 2 s, beats no shorter than 250 ms (240 BPM) */
static struct {
    uint32_t beat_ms, last_tap, dur[4];
    int idx, taps;
    uint8_t skipped;
} tt = {638u, 0u, {0}, 0, 0, 0};
#define TT_VALUES 4

static float tap_bpm(void) { return 60000.0f / (float)tt.beat_ms; }
static void tap_set_bpm(float bpm) { tt.beat_ms = (uint32_t)(60000.0f / bpm + 0.5f); }   /* (rounded: see below) */
static int tap_chain_active(uint32_t ms) { return tt.last_tap + 2000u > ms && tt.last_tap + tt.beat_ms * 3u > ms; }
static void tap(uint32_t ms)
{
    uint32_t d, sum = 0;
    int i, amount;
    if (!tap_chain_active(ms)) {
        tt.taps = 0;
        tt.idx = 0;
        for (i = 0; i < TT_VALUES; i++)
            tt.dur[i] = 0;
    }
    d = ms - tt.last_tap;
    tt.last_tap = ms;
    if (++tt.taps == 1)
        return;
    if (tt.taps > 2 && !tt.skipped && (float)d > (float)tt.beat_ms * 1.75f && (float)d < (float)tt.beat_ms * 2.75f) {
        d >>= 1;                                     /* a beat missed: half of it */
        tt.skipped = 1;
    } else {
        tt.skipped = 0;
    }
    tt.dur[tt.idx] = d;
    tt.idx = (tt.idx + 1) % TT_VALUES;
    amount = tt.taps - 1 > TT_VALUES ? TT_VALUES : tt.taps - 1;
    for (i = 0; i < amount; i++)
        sum += tt.dur[i];
    tt.beat_ms = sum / (uint32_t)amount;
    if (tt.beat_ms < 250u)
        tt.beat_ms = 250u;
}

/* -------------------------------------------------------------- tempo --- */
static int32_t bpm_shown = 94;           /* fBPM_Cache: whole BPM, what the screen shows */
static float fade_target, fade_diff;     /* fTargetBPM (0: no fade), fBPM_Diff */
static uint8_t nudge;                    /* -1 / +1 while OCT- / OCT+ held */
static int32_t nudge_saved;

/* setGlobalBPM. The ClockBox keeps TapTempo's beat in whole ms and re-reads the BPM from it; rounding the beat to the
 * nearest ms (not down) keeps 245..300 BPM from creeping up a step */
static void set_bpm(float bpm)
{
    if (standalone())
        tap_set_bpm(bpm);
    engine_tempo(bpm);
}

static void set_preset(int p)            /* a press: jump */
{
    bpm_shown = set.preset[p];
    set_bpm((float)bpm_shown);
    fade_target = 0.0f;
}
static void fade_preset(int p)           /* a hold: ease towards it, a 16th of the way at each divider tick */
{
    fade_target = (float)set.preset[p];
    fade_diff = fade_target - bpm_now;
}
static void next_beat(void)              /* nextBeat */
{
    if (fade_target == 0.0f)
        return;
    if (fm_fabsf(bpm_now - fade_target) > 3.0f) {
        set_bpm(bpm_now + fade_diff / 16.0f);
    } else {
        set_bpm(fade_target);
        bpm_shown = (int32_t)fade_target;
        fade_target = 0.0f;
    }
}

/* --------------------------------------------------------------------- UI --- */
enum { CFG_NONE, CFG_MODE, CFG_QRS, CFG_DIV, CFG_BRIGHT };
#define B_SHIFT B_HOME
#define B_START B_PLAY
#define B_STOPB B_REC
#define B_TAP B_SEQ
static const uint8_t PRESET_BTN[3] = {B_FX, B_SEL, B_ENV};
static const uint8_t BEAT_KEY[4] = {0, 2, 4, 6};    /* F3 G3 A3 B3 */
#define EXT_KEY 7u                                  /* C4 */

static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t btn;                        /* held at the last input */
    uint8_t cfg;                         /* iConfigChangeMode */
    uint8_t used;                        /* buttons whose press took part in a combination: no action on release */
    uint32_t preset_down[3];             /* when each preset button went down (0: up) */
    uint8_t preset_long[3];
    uint32_t red_until;                  /* ledRed() for half a second: saved */
    uint32_t boot_ms;
    uint8_t booted;
    uint32_t last_beat;
    uint8_t cfg_qrs0, cfg_div0, cfg_bright0;   /* the values a settings page started with */
    uint8_t redraw;
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void save(void)                   /* EEPROM.update + ledRed: the settings kept */
{
    if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
        ui.red_until = plat_ms() + 500u;
        app_say("SAVED", "");
    } else {
        app_say("SAVE", "FAILED");
    }
}

static void switch_mode(void)            /* switchMode + processModeSwitch */
{
    char t[24];
    set.mode = (uint8_t)((set.mode + 1u) % NMODES);
    if (standalone())
        set_bpm((float)bpm_shown);
    fade_target = 0.0f;
    req_mode = 1;
    save();
    t[0] = 0;
    text_cat(t, MODE_NAME[set.mode]);
    app_say("MODE", t);
}

static void cfg_enter(uint8_t c)
{
    ui.cfg = c;
    ui.cfg_qrs0 = set.qrs_offset;
    ui.cfg_div0 = set.div;
    ui.cfg_bright0 = set.bright;
    ui.used |= 1u;
    ui.redraw = 1;
}

static void cfg_leave(void)              /* both released: kept if changed */
{
    if ((ui.cfg == CFG_QRS && set.qrs_offset != ui.cfg_qrs0) || (ui.cfg == CFG_DIV && set.div != ui.cfg_div0) ||
        (ui.cfg == CFG_BRIGHT && set.bright != ui.cfg_bright0))
        save();
    ui.cfg = CFG_NONE;
    ui.redraw = 1;
}

static void press_start(void)            /* startHandler */
{
    req_start = 1;
}

static void press_stop(void)             /* stopHandler */
{
    if (standalone())
        req_stop = 1;
}

static void press_tap(void)              /* tapHandler: standalone, taps 250 ms apart at least */
{
    uint32_t now = plat_ms();
    if (!standalone())
        return;
    if (now - tt.last_tap >= 250u)
        tap(now);
}

static void preset_store(int p)          /* HOME + a preset: the tempo now, kept */
{
    char t[8];
    set.preset[p] = (uint16_t)bpm_shown;
    save();
    int_text(bpm_shown, t);
    app_say(p == 0 ? "PRESET 1" : p == 1 ? "PRESET 2" : "PRESET 3", t);
}

static void nudge_set(int dir)           /* nudgePlus / nudgeMinus */
{
    if (!standalone())
        return;
    if (dir && !nudge) {
        nudge_saved = bpm_shown;
        nudge = (uint8_t)(dir > 0 ? 1 : 2);
        set_bpm((float)bpm_shown * (dir > 0 ? 1.075f : 0.925f));
    } else if (!dir && nudge) {
        nudge = 0;
        bpm_shown = nudge_saved;
        set_bpm((float)bpm_shown);
    }
    ui.redraw = 1;
}

static void lights(void)
{
    uint32_t now = plat_ms(), keys = 0, btn = 0, glow = 0, i;
    int red = ui.red_until && now < ui.red_until;
    static uint32_t dim_set;
    for (i = 0; i < 4u; i++)
        if (led[i] != L_OFF || red || update_mode || !ui.booted)
            keys |= 1u << BEAT_KEY[i];
    if (ext_led && !standalone())
        keys |= 1u << EXT_KEY;
    if (update_mode || red || qrs_wait)
        btn |= 1u << B_PLAY;                         /* PLAY red */
    else if (playing)
        btn |= LED_PLAY_GREEN;
    for (i = 0; i < 3u; i++)
        glow |= 1u << PRESET_BTN[i];
    glow |= 1u << B_SHIFT | 1u << B_TAP | 1u << B_STOPB;
    if (ui.btn >> B_SHIFT & 1u)
        btn |= 1u << B_SHIFT;
    if (set.bright >= BRIGHT_MAX) {
        if (dim_set) {
            plat_dim_ns(0);
            dim_set = 0;
        }
        plat_lights(btn, keys, glow, 0, 1);
    } else {
        uint32_t ns = 500u + (uint32_t)(set.bright - BRIGHT_MIN) * 3500u / (BRIGHT_MAX - BRIGHT_MIN);
        if (ns != dim_set) {
            plat_dim_ns(ns);
            dim_set = ns;
        }
        plat_lights(btn, 0, glow, keys, 1);
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), now = plat_ms(), down = btn & ~ui.btn, up = ui.btn & ~btn, i;
    int shift = (int)(btn >> B_SHIFT & 1u), start = (int)(btn >> B_START & 1u), stop = (int)(btn >> B_STOPB & 1u),
        tapb = (int)(btn >> B_TAP & 1u);
    int32_t e;
    if (!ui.booted || update_mode) {
        ui.btn = btn;
        lights();
        return;
    }
    /* the combinations (the ClockBox's loop(), in its order) */
    if (shift && start) {
        if (ui.cfg == CFG_NONE) {
            cfg_enter(CFG_MODE);
            switch_mode();
        }
    } else if (shift && stop) {
        if (ui.cfg == CFG_NONE)
            cfg_enter(CFG_QRS);
    } else if (shift && tapb) {
        if (ui.cfg == CFG_NONE)
            cfg_enter(CFG_DIV);
    } else if (start && stop && !shift) {
        if (ui.cfg == CFG_NONE)
            cfg_enter(CFG_BRIGHT);
    }
    if (ui.cfg != CFG_NONE && !shift && !start && !stop)
        cfg_leave();
    if (shift && (down & (1u << B_START | 1u << B_STOPB | 1u << B_TAP)))
        ui.used |= 1u;
    /* the single buttons: PLAY and STOP on release (Button2's tap), TAP on its press */
    if ((up >> B_START & 1u) && !ui.used && !shift && ui.cfg == CFG_NONE)
        press_start();
    if ((up >> B_STOPB & 1u) && !ui.used && !shift && ui.cfg == CFG_NONE)
        press_stop();
    if ((down >> B_TAP & 1u) && !shift && ui.cfg == CFG_NONE)
        press_tap();
    /* the presets: a press jumps, a hold of a second fades, HOME + a press stores (standalone) */
    for (i = 0; i < 3u; i++) {
        uint32_t bit = 1u << PRESET_BTN[i];
        if (down & bit) {
            ui.preset_down[i] = now ? now : 1u;
            ui.preset_long[i] = 0;
        }
        if ((btn & bit) && ui.preset_down[i] && !ui.preset_long[i] && now - ui.preset_down[i] >= 1000u) {
            ui.preset_long[i] = 1;
            if (standalone() && !shift) {
                char t[8];
                fade_preset((int)i);
                int_text(set.preset[i], t);
                app_say("FADE TO", t);
            }
        }
        if ((up & bit) && ui.preset_down[i]) {
            if (!ui.preset_long[i] && standalone()) {
                if (shift)
                    preset_store((int)i);
                else
                    set_preset((int)i);
            }
            ui.preset_down[i] = 0;
        }
    }
    /* EDIT: the audio sync output on / off (the FM-1's stand-in for the CV jacks) */
    if (down >> B_EDIT & 1u) {
        set.sync_out ^= 1u;
        save();
        app_say("SYNC OUT", set.sync_out ? "ON" : "OFF");
    }
    /* nudge while OCT- / OCT+ held */
    nudge_set((btn >> B_OCTUP & 1u) ? 1 : (btn >> B_OCTDN & 1u) ? -1 : 0);
    /* the encoder: the tempo, or the page's value */
    e = plat_enc(EN_SELECT);
    if (!e && ui.cfg == CFG_NONE && (e = plat_enc(EN_ALGO)) != 0)
        e *= 5;                                       /* ALGO: steps of 5, as HOME + SELECT */
    else if (shift && ui.cfg == CFG_NONE)
        e *= 5;
    if (e) {
        if (ui.cfg == CFG_NONE && standalone() && !nudge) {
            bpm_shown = clamp(bpm_shown + e, BPM_MIN, BPM_MAX);
            fade_target = 0.0f;
            set_bpm((float)bpm_shown);
        } else if (ui.cfg == CFG_QRS) {
            set.qrs_offset = (uint8_t)clamp(set.qrs_offset + e, 1, (int32_t)PPQN);
        } else if (ui.cfg == CFG_DIV) {
            set.div = (uint8_t)clamp(set.div + e, 0, (int32_t)NDIV - 1);
        } else if (ui.cfg == CFG_BRIGHT) {
            set.bright = (uint8_t)clamp(set.bright + e * 10, BRIGHT_MIN, BRIGHT_MAX);
        }
        ui.redraw = 1;
    }
    /* the tempo from tapping (the ClockBox reads it back every pass) */
    if (standalone() && !nudge && fm_fabsf(tap_bpm() - (float)bpm_shown) >= 1.0f) {
        bpm_shown = clamp((int32_t)tap_bpm(), BPM_MIN, BPM_MAX);
        engine_tempo((float)bpm_shown);
        ui.redraw = 1;
    }
    if (btn != ui.btn)
        ui.redraw = 1;
    if (!btn)
        ui.used = 0;                                  /* (after the releases above: the last one still counts) */
    ui.btn = btn;
    lights();
}

void app_input_only(void) { input(); }

/* ---------------------------------------------------------------- drawing --- */
#define C_GREEN RGB(40, 220, 90)
#define C_BLUE RGB(60, 120, 255)
#define C_PURPLE RGB(210, 60, 230)
#define C_YELLOW RGB(245, 215, 40)
#define C_RED RGB(240, 50, 40)
#define C_CYAN RGB(40, 210, 230)
#define C_SEGOFF RGB(28, 28, 34)          /* the beat lights and the blinker, off */
#define C_DIGOFF RGB(12, 12, 15)          /* a seven-segment digit's unlit segments */
static uint16_t led_rgb(uint8_t l) { return l == L_GREEN ? C_GREEN : l == L_BLUE ? C_BLUE : l == L_PURPLE ? C_PURPLE : 0; }

/* a seven-segment digit, w x h, segments t thick (-1: blank, the unlit segments only) */
static void seg_digit(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, int d, uint16_t on)
{
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    uint32_t s = d >= 0 && d <= 9 ? SEG[d] : 0u;
    int32_t h2 = h / 2;
    cv_rect(x + t, y, w - 2 * t, t, s & 1u ? on : C_DIGOFF);
    cv_rect(x + w - t, y + t, t, h2 - t, s & 2u ? on : C_DIGOFF);
    cv_rect(x + w - t, y + h2, t, h2 - t, s & 4u ? on : C_DIGOFF);
    cv_rect(x + t, y + h - t, w - 2 * t, t, s & 8u ? on : C_DIGOFF);
    cv_rect(x, y + h2, t, h2 - t, s & 16u ? on : C_DIGOFF);
    cv_rect(x, y + t, t, h2 - t, s & 32u ? on : C_DIGOFF);
    cv_rect(x + t, y + h2 - t / 2, w - 2 * t, t, s & 64u ? on : C_DIGOFF);
}

static void seg_number(int32_t cx, int32_t y, int32_t v, int digits, int32_t w, int32_t h, int32_t t, uint16_t c)
{
    int32_t gap = w / 5, total = digits * w + (digits - 1) * gap, x = cx - total / 2, i, p = 1;
    for (i = 1; i < digits; i++)
        p *= 10;
    for (i = 0; i < digits; i++, p /= 10) {
        int d = (int)(v / p % 10);
        if (v < p && p > 1)
            d = -1;                                  /* no leading zeros */
        seg_digit(x + i * (w + gap), y, w, h, t, d, c);
    }
}

static void centre(int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text((240 - text_w(f, s)) / 2, y, f, s, c);
}

static void beat_row(int32_t y)          /* the four beat LEDs, and the clock-in LED while following */
{
    int32_t i, x0 = standalone() ? 28 : 12;
    for (i = 0; i < 4; i++) {
        uint16_t c = led_rgb(led[i]);
        cv_rect(x0 + i * 48, y, 40, 22, c ? c : C_SEGOFF);
    }
    if (!standalone())
        cv_rect(x0 + 4 * 48 + 4, y, 24, 22, ext_led ? C_YELLOW : C_SEGOFF);
}

static void draw(void)
{
    char t[64], n[12];
    uint32_t now = plat_ms();
    int red = ui.red_until && now < ui.red_until;
    /* the header: the name (or a message), Bluetooth, the blinker */
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 6, &FONT_B, ui.msg[0], red ? C_RED : C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        int32_t x = cv_text(8, 6, &FONT_B, APP_NAME, C_WHITE);
        if (FM1B_BLE)
            cv_text(x + 8, 10, &FONT_XS, "BLE", plat_ble_connected() ? C_HI : C_DIM);
        if (set.sync_out)
            cv_text(x + 36, 10, &FONT_XS, "SYNC", C_HI);
    }
    {
        uint16_t c = !blink ? C_SEGOFF : !standalone() && ext_led ? C_YELLOW : qrs_wait ? C_PURPLE :
                     led[0] == L_GREEN ? C_GREEN : C_BLUE;
        cv_rect(212, 6, 20, 20, red ? C_RED : c);
    }
    cv_blit(0, 0);
    /* the middle: the tempo, a page's value, or the follow mode's words */
    cv_begin(240, 144, C_BLACK);
    if (update_mode) {
        centre(30, &FONT_L, "UPDATE MODE", C_RED);
        centre(80, &FONT_S, "MIDI OUT SILENCED", C_GRAY);
        centre(100, &FONT_S, "POWER OFF AND ON TO LEAVE", C_GRAY);
    } else if (!ui.booted) {                         /* showInfo: 2 s */
        centre(14, &FONT_L, "CLOCKBOX", C_YELLOW);
        t[0] = 0;
        text_cat(t, "VERSION " VERSION " FM-1");
        centre(62, &FONT_B, t, C_WHITE);
        centre(96, &FONT_S, "socialmidi.com", C_GRAY);
    } else if (ui.cfg == CFG_QRS) {
        centre(4, &FONT_M, "QRS OFFSET (PPQN)", C_WHITE);
        seg_number(120, 38, set.qrs_offset, 2, 46, 84, 10, C_HI);
    } else if (ui.cfg == CFG_DIV) {
        int digits = DIVIDER[set.div] >= 10 ? 2 : 1, i;
        centre(4, &FONT_M, "CLOCK DIVIDER", C_WHITE);
        for (i = 0; i < 10; i++)                     /* the slash */
            cv_line(digits == 2 ? 30 + i : 60 + i, 122, digits == 2 ? 62 + i : 92 + i, 38, C_HI);
        seg_number(digits == 2 ? 150 : 140, 38, DIVIDER[set.div], digits, 46, 84, 10, C_HI);
    } else if (ui.cfg == CFG_BRIGHT) {
        centre(4, &FONT_M, "LED BRIGHTNESS", C_WHITE);
        seg_number(120, 38, set.bright, 3, 46, 84, 10, C_HI);
    } else if (standalone()) {
        uint16_t c = nudge ? C_AMB : fade_target != 0.0f ? C_CYAN : C_WHITE;
        seg_number(120, 6, bpm_shown, 3, 56, 100, 11, c);
        beat_row(118);
    } else {
        centre(6, &FONT_M, "FOLLOW CLOCK", C_WHITE);
        centre(36, &FONT_M, set.mode == M_FOLLOW_DIN ? "FROM TRS MIDI" : "FROM USB MIDI", C_WHITE);
        if (ext_period_us && now - ext_last_ms < 1000u) {
            int32_t tenths = (int32_t)(25000000u / ext_period_us);   /* 60e6 / (us * 24) * 10 */
            t[0] = 0;
            text_cat(t, "IN ~");
            int_text(tenths / 10, n);
            text_cat(t, n);
            text_cat(t, " BPM");
            centre(76, &FONT_B, t, C_HI);
        } else {
            centre(76, &FONT_B, "WAITING FOR CLOCK", C_DIM);
        }
        beat_row(118);
    }
    cv_blit(0, 36);
    /* the bottom: the mode, the presets and the settings; HOME held: what it does */
    cv_begin(240, 60, C_BLACK);
    if (ui.booted && !update_mode) {
        if (ui.cfg == CFG_QRS || ui.cfg == CFG_DIV || ui.cfg == CFG_BRIGHT) {
            centre(10, &FONT_S, "TURN SELECT TO SET", C_GRAY);
            centre(32, &FONT_S, "RELEASE TO KEEP", C_GRAY);
        } else if (ui.btn >> B_SHIFT & 1u) {
            centre(4, &FONT_XS, "HOME + PLAY: MODE   HOME + REC: QRS", C_HI);
            centre(20, &FONT_XS, "HOME + SEQ: DIVIDER   HOME + FX/SEL/ENV:", C_HI);
            centre(36, &FONT_XS, "STORE PRESET   HOME + SELECT: BPM x5", C_HI);
        } else {
            int i;
            int32_t x = 8;
            centre(2, &FONT_M, MODE_NAME[set.mode], qrs_wait ? C_PURPLE : C_YELLOW);
            for (i = 0; i < 3; i++) {
                static const char *const P[3] = {"FX ", "SEL ", "ENV "};
                int lit = standalone() && set.preset[i] == bpm_shown;
                x = cv_text(x, 32, &FONT_XS, P[i], C_GRAY);
                int_text(set.preset[i], n);
                x = cv_text(x, 32, &FONT_XS, n, lit ? C_WHITE : C_AMB) + 10;
            }
            t[0] = 0;
            text_cat(t, "/");
            int_text(DIVIDER[set.div], n);
            text_cat(t, n);
            text_cat(t, "  OFS ");
            int_text(set.qrs_offset, n);
            text_cat(t, n);
            cv_text(232 - text_w(&FONT_XS, t), 32, &FONT_XS, t, C_GRAY);
            cv_text(8, 46, &FONT_XS, "SEQ TAP  PLAY START/QRS  REC STOP", C_DIM);
        }
    }
    cv_blit(0, 180);
}

/* a screen reader that has just started listening (or asks) hears the screen */
static void speak(void)
{
    char t[96], n[12];
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME ". ");
        if (standalone()) {
            int_text(bpm_shown, n);
            text_cat(t, n);
            text_cat(t, " BPM. ");
        }
        text_cat(t, MODE_NAME[set.mode]);
        text_cat(t, playing ? ". PLAYING" : ". STOPPED");
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

void app_init(void)                     /* (a start clears the RAM on the device; the host's reboot does not) */
{
    memset(&ui, 0, sizeof ui);
    memset(&tt, 0, sizeof tt);
    tt.beat_ms = 638u;
    engine_on = update_mode = 0;
    req_start = req_stop = req_mode = 0;
    playing = qrs_wait = 0;
    ck_tick = beat_count = 0;
    leds_off();
    ext_led = blink = 0;
    ext_ticks = ext_period_us = ext_count = ext_last_ms = 0;
    starts_out = stops_out = last_start_ms = last_stop_ms = 0;
    pulse_r = pulse_w = 0;
    reset_pending = 0;
    acc_ns = 0;
    was_standalone = 0;
    blink_timer = 1;
    measure = 0;
    fade_target = fade_diff = 0.0f;
    nudge = 0;
    pl_left = pl_right = 0;
    settings_load();
    bpm_shown = 94;
    set_bpm(94.0f);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.boot_ms = plat_ms();
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_state;
    uint32_t now = plat_ms(), state;
    if (!ui.booted) {
        if (now - ui.boot_ms < 300u && (plat_buttons() >> B_STOPB & 1u) && !update_mode) {
            update_mode = 1;                         /* STOP held at power-on */
            ui.booted = 1;
            ui.redraw = 1;
        } else if (now - ui.boot_ms >= 2000u) {
            ui.booted = 1;
            ui.btn = plat_buttons();
            engine_on = 1;                           /* initClock + processModeSwitch */
            req_mode = 1;
            ui.redraw = 1;
        }
    }
    input();
    speak();
    if (beat_count != ui.last_beat) {                /* nextBeat: the preset fade's step */
        ui.last_beat = beat_count;
        next_beat();
    }
    state = (uint32_t)blink | (uint32_t)ext_led << 1 | (uint32_t)qrs_wait << 2 | (uint32_t)playing << 3 |
            (uint32_t)led[0] << 4 | (uint32_t)led[1] << 6 | (uint32_t)led[2] << 8 | (uint32_t)led[3] << 10 |
            (uint32_t)(ui.red_until && now < ui.red_until) << 12 | (uint32_t)(now < ui.msg_until) << 13 |
            (uint32_t)bpm_shown << 14 | (uint32_t)(ext_period_us / 2000u) << 24;
    if (state != last_state) {
        last_state = state;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "bpm"))
        *got = bpm_shown;
    else if (!strcmp(what, "bpm10"))                 /* the engine's tempo, in tenths */
        *got = (int)(bpm_now * 10.0f + 0.5f);
    else if (!strcmp(what, "mode"))
        *got = set.mode;
    else if (!strcmp(what, "playing"))
        *got = playing;
    else if (!strcmp(what, "qrs"))
        *got = qrs_wait;
    else if (!strcmp(what, "qrs_offset"))
        *got = set.qrs_offset;
    else if (!strcmp(what, "div"))
        *got = DIVIDER[set.div];
    else if (!strcmp(what, "bright"))
        *got = set.bright;
    else if (!strcmp(what, "sync_out"))
        *got = set.sync_out;
    else if (!strncmp(what, "preset", 6))            /* preset1 .. preset3 */
        *got = set.preset[(atoi(what + 6) + 2) % 3];
    else if (!strcmp(what, "cfg"))
        *got = ui.cfg;
    else if (!strcmp(what, "tick"))
        *got = (int)ck_tick;
    else if (!strcmp(what, "tick_in_bar"))
        *got = (int)(ck_tick % BAR);
    else if (!strcmp(what, "starts"))
        *got = (int)starts_out;
    else if (!strcmp(what, "stops"))
        *got = (int)stops_out;
    else if (!strcmp(what, "start_gap"))             /* ms from the last Stop to the last Start */
        *got = (int)(last_start_ms - last_stop_ms);
    else if (!strcmp(what, "ext_ticks"))
        *got = (int)ext_ticks;
    else if (!strcmp(what, "update_mode"))
        *got = update_mode;
    else if (!strncmp(what, "led", 3))               /* led0 .. led3: L_OFF, L_GREEN, L_BLUE, L_PURPLE */
        *got = led[atoi(what + 3) & 3];
    else if (!strcmp(what, "mark")) {                /* start measuring the clocks out */
        m_on = 1;
        m_count = m_maxdev_us = 0;
        *got = 1;
    } else if (!strcmp(what, "rate10"))              /* their tempo, in tenths of BPM */
        *got = m_count > 1u && m_last != m_first ?
                   (int)((25000u * (m_count - 1u) + (m_last - m_first) / 2u) / (m_last - m_first)) : 0;
    else if (!strcmp(what, "jitter_ok"))             /* every spacing within 1 ms of the ideal */
        *got = m_count > 1u && m_maxdev_us < 1000u;
    else if (!strcmp(what, "stop_start_ticks"))      /* QRS: the Stop's tick to the Start's */
        *got = (int)(last_start_tick - last_stop_tick);
    else if (!strcmp(what, "start_tick_in_bar"))
        *got = (int)(last_start_tick % BAR);
    else if (!strcmp(what, "fading"))
        *got = fade_target != 0.0f;
    else
        return 0;
    return 1;
}
#endif
