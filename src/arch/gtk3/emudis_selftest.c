/**
 * \file emudis_selftest.c
 * \brief   C64-EmuDis automatic self test (used by the CI builds)
 *
 * Activated by environment variables, does nothing otherwise:
 *
 *   EMUDIS_SELFTEST_RECORD=<file.mp4>   record a video to this file
 *   EMUDIS_SELFTEST_DROP=<file.prg>     autostart this file while recording,
 *                                       the same way a dropped file is started
 *
 * Timeline: start recording after 4 s, autostart after 8 s, stop the
 * recording after 20 s and quit after 23 s. Every second a heartbeat with
 * the CPU clock is logged, so a frozen emulation thread is visible in the log.
 *
 * \author IT-Dummkopfen
 */

/* This file is part of C64-EmuDis (GPL-2.0-or-later). */

#include "vice.h"

#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdlib.h>

#include "autostart.h"
#include "log.h"
#include "mainlock.h"
#include "maincpu.h"
#include "resources.h"
#include "screenshot.h"
#include "uiactions.h"

#include "gfxoutputdrv/ffmpegexedrv.h"
#include "emudis.h"

static log_t st_log = LOG_DEFAULT;
static int seconds = 0;

static gboolean st_tick(gpointer data)
{
    const char *rec = getenv("EMUDIS_SELFTEST_RECORD");
    const char *drop = getenv("EMUDIS_SELFTEST_DROP");
    CLOCK clk;
    int recording;
    double vs = 0.0, as = 0.0;

    seconds++;

    mainlock_obtain();
    clk = maincpu_clk;
    recording = screenshot_is_recording();
    if (recording) {
        ffmpegexedrv_get_sync_info(&vs, &as);
    }
    mainlock_release();
    log_message(st_log, "t=%ds clk=%lu recording=%d video=%.2fs audio=%.2fs",
                seconds, (unsigned long)clk, recording, vs, as);

    if (seconds == 4) {
        bool ok;
        mainlock_obtain();
        ok = emudis_video_start_file(rec);
        mainlock_release();
        log_message(st_log, "start recording '%s': %s", rec, ok ? "OK" : "FAILED");
    } else if (seconds == 8 && drop != NULL && *drop != 0) {
        log_message(st_log, "autostart '%s' (like a dropped file)", drop);
        mainlock_obtain();
        autostart_autodetect(drop, NULL, 0, AUTOSTART_MODE_RUN);
        mainlock_release();
        log_message(st_log, "autostart returned");
    } else if (seconds == 20) {
        mainlock_obtain();
        emudis_video_stop();
        mainlock_release();
        log_message(st_log, "recording stopped");
    } else if (seconds == 23) {
        log_message(st_log, "done, quitting");
        resources_set_int("ConfirmOnExit", 0);
        ui_action_trigger(ACTION_QUIT);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/** \brief  Arm the self test if EMUDIS_SELFTEST_RECORD is set (UI thread) */
void emudis_selftest_setup(void)
{
    const char *rec = getenv("EMUDIS_SELFTEST_RECORD");

    if (rec == NULL || *rec == 0) {
        return;
    }
    st_log = log_open("SELFTEST");
    log_message(st_log, "self test armed, video: %s", rec);
    g_timeout_add(1000, st_tick, NULL);
}
