/**
 * \file emudis_autowarp.c
 * \brief   C64-EmuDis: warp while loading (no loading times with true drive emulation)
 *
 * C64-EmuDis 0.1a switched true drive emulation off to get rid of loading
 * times. That breaks every disk with its own fast loader (most games and
 * demos), and without kernal traps there was no drive at all.
 *
 * Now true drive emulation stays on (full compatibility) and the emulator
 * switches to warp mode while a disk drive is busy (LED on and the head moved
 * recently) or the tape really moves (counter changes), and back to normal
 * speed when it is idle. The LED or the motor alone is not enough: the LED
 * blinks after a load error and the motor can stay on after a trap load.
 *
 * The drive and tape states come from the status bar callbacks
 * (uistatusbar.c), which the emulation thread calls. A UI timer decides.
 *
 * \author IT-Dummkopfen
 */

/* This file is part of C64-EmuDis (GPL-2.0-or-later). */

#include "vice.h"

#include <gtk/gtk.h>
#include <stdbool.h>

#include "log.h"
#include "mainlock.h"
#include "resources.h"
#include "uiapi.h"
#include "vsync.h"

#include "emudis.h"

#define AW_UNITS        4
#define AW_TICK_MS      100
#define AW_START_MS     200     /* busy this long -> warp on (ignore blips) */
#define AW_STOP_MS      700     /* idle this long -> warp off */
#define AW_LED_MIN      100     /* LED brightness (0..1000) that counts as on */

static volatile int aw_led[AW_UNITS][2];
static volatile gint64 aw_last_track_us = 0;   /* last head movement */
static volatile int aw_tape_motor[2];
static volatile int aw_tape_counter[2] = { -1, -1 };
static volatile gint64 aw_last_tape_us = 0;      /* last tape counter change */

static int aw_enabled = 1;          /* resource "EmuDisAutoWarp" */
static bool aw_warp_by_us = false;  /* we switched warp on */
static bool aw_user_override = false; /* user switched our warp off */
static gint64 aw_busy_since = 0;
static gint64 aw_idle_since = 0;
static guint aw_timer = 0;

/* --- called from the status bar (emulation thread) --------------------- */

void emudis_autowarp_note_led(unsigned int unit, unsigned int drive,
                              unsigned int pwm1, unsigned int pwm2)
{
    if (unit < AW_UNITS && drive < 2) {
        aw_led[unit][drive] = (pwm1 > AW_LED_MIN || pwm2 > AW_LED_MIN);
    }
}

void emudis_autowarp_note_track(unsigned int unit)
{
    aw_last_track_us = g_get_monotonic_time();
}

void emudis_autowarp_note_tape_motor(int port, int motor)
{
    if (port >= 0 && port < 2) {
        aw_tape_motor[port] = motor;
    }
}

void emudis_autowarp_note_tape_counter(int port, int counter)
{
    if (port >= 0 && port < 2 && counter != aw_tape_counter[port]) {
        if (aw_tape_counter[port] >= 0 && aw_tape_motor[port]) {
            aw_last_tape_us = g_get_monotonic_time();
        }
        aw_tape_counter[port] = counter;
    }
}

/* --- decision (UI thread) ------------------------------------------------ */

static bool aw_busy(gint64 now)
{
    int u, d;
    bool led = false;
    gint64 since_track = now - aw_last_track_us;

    for (u = 0; u < AW_UNITS; u++) {
        for (d = 0; d < 2; d++) {
            if (aw_led[u][d]) {
                led = true;
            }
        }
    }
    /* drive: LED on and the head moved recently; or the head is moving
       right now (some loaders switch the LED off) */
    if ((led && since_track < 3000 * 1000) || since_track < 500 * 1000) {
        return true;
    }
    /* tape: the tape really moves (the motor can stay on after a trap load) */
    return (now - aw_last_tape_us) < 3000 * 1000;
}

static gboolean aw_tick(gpointer data)
{
    gint64 now = g_get_monotonic_time();
    bool busy = aw_busy(now);
    int warp;

    if (busy) {
        aw_idle_since = 0;
        if (aw_busy_since == 0) {
            aw_busy_since = now;
        }
    } else {
        aw_busy_since = 0;
        if (aw_idle_since == 0) {
            aw_idle_since = now;
        }
    }

    mainlock_obtain();
    warp = vsync_get_warp_mode();

    if (aw_warp_by_us && !warp) {
        /* the user (or autostart) switched warp off: respect that until idle */
        aw_warp_by_us = false;
        aw_user_override = busy;
    }

    if (aw_enabled && busy && !warp && !aw_user_override
            && now - aw_busy_since >= AW_START_MS * 1000) {
        vsync_set_warp_mode(1);
        aw_warp_by_us = true;
    } else if (aw_warp_by_us && warp && !busy
               && now - aw_idle_since >= AW_STOP_MS * 1000) {
        vsync_set_warp_mode(0);
        aw_warp_by_us = false;
    }
    if (!busy) {
        aw_user_override = false;
    }
    mainlock_release();
    return G_SOURCE_CONTINUE;
}

/* --- resource and setup -------------------------------------------------- */

static int set_autowarp(int val, void *param)
{
    aw_enabled = val ? 1 : 0;
    if (!aw_enabled && aw_warp_by_us) {
        /* switching the option off ends a warp it started */
        vsync_set_warp_mode(0);
        aw_warp_by_us = false;
    }
    return 0;
}

static const resource_int_t aw_resources[] = {
    { "EmuDisAutoWarp", 1, RES_EVENT_NO, NULL, &aw_enabled, set_autowarp, NULL },
    RESOURCE_INT_LIST_END
};

int emudis_autowarp_resources_init(void)
{
    return resources_register_int(aw_resources);
}

bool emudis_autowarp_get(void)
{
    return aw_enabled != 0;
}

void emudis_autowarp_set(bool enabled)
{
    resources_set_int("EmuDisAutoWarp", enabled ? 1 : 0);
}

/** \brief  Start the decision timer (UI thread, once) */
void emudis_autowarp_start(void)
{
    if (aw_timer == 0) {
        aw_timer = g_timeout_add(AW_TICK_MS, aw_tick, NULL);
    }
}
