/** \file   actions-emudis.c
 * \brief   UI action implementations for the C64-EmuDis tools
 */

/*
 * This file is part of C64-EmuDis, a modified version of VICE.
 * Copyright (C) 2026 IT-Dummkopfen
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include "vice.h"

#include <gtk/gtk.h>
#include <stdbool.h>
#include <stddef.h>

#include "hotkeys.h"
#include "lib.h"
#include "mainlock.h"
#include "resources.h"
#include "screenshot.h"
#include "ui.h"
#include "uiactions.h"
#include "uiapi.h"
#include "util.h"

#include "emudis.h"
#include "actions-emudis.h"


/* ------------------------------------------------------------------------ */
/* Fast loading                                                             */
/* ------------------------------------------------------------------------ */

bool emudis_fastload_get(void)
{
    int tde = 1;
    resources_get_int("Drive8TrueEmulation", &tde);
    return tde == 0;
}

void emudis_fastload_set(bool enabled)
{
    int unit;

    for (unit = 8; unit <= 11; unit++) {
        resources_set_int_sprintf("Drive%dTrueEmulation", enabled ? 0 : 1, unit);
    }
    if (enabled) {
        resources_set_int("AutostartWarp", 1);
        resources_set_int("AutostartPrgMode", 1);   /* inject into RAM */
        resources_set_int("AutostartDelayRandom", 0);
    }
}


/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

/** \brief  Run \a fn with the VICE mainlock held (if not already held) */
static void with_lock(void (*fn)(void))
{
    bool need = (mainlock_get_depth() == 0);

    if (need) {
        mainlock_obtain();
    }
    fn();
    if (need) {
        mainlock_release();
    }
}

static void take_screenshot(void)
{
    const char *dir = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    GDateTime *now = g_date_time_new_now_local();
    gchar *stamp = g_date_time_format(now, "%Y%m%d-%H%M%S");
    char *name = lib_msprintf("c64-emudis-%s.png", stamp);
    char *path;
    char *msg;

    if (dir == NULL || !g_file_test(dir, G_FILE_TEST_IS_DIR)) {
        dir = g_get_home_dir();
    }
    path = util_join_paths(dir, name, NULL);
    if (screenshot_save("PNG", path, ui_get_active_canvas()) < 0) {
        msg = lib_msprintf("Screenshot failed: %s", path);
    } else {
        msg = lib_msprintf("Screenshot saved: %s", path);
    }
    ui_display_statustext(msg, true);
    lib_free(msg);
    lib_free(path);
    lib_free(name);
    g_free(stamp);
    g_date_time_unref(now);
}


/* ------------------------------------------------------------------------ */
/* Action handlers                                                          */
/* ------------------------------------------------------------------------ */

static void memview_action(ui_action_map_t *self)      { with_lock(emudis_memview_show); }
static void fonts_action(ui_action_map_t *self)        { with_lock(emudis_fontsearch_show); }
static void sprites_action(ui_action_map_t *self)      { with_lock(emudis_spritesearch_show); }
static void sids_action(ui_action_map_t *self)         { with_lock(emudis_sidsearch_show); }
static void assembler_action(ui_action_map_t *self)    { with_lock(emudis_assembler_show); }
static void disassembler_action(ui_action_map_t *self) { with_lock(emudis_disassembler_show); }
static void snapshots_action(ui_action_map_t *self)    { with_lock(emudis_snapshots_show); }
static void video_action(ui_action_map_t *self)        { with_lock(emudis_video_show); }
static void video_toggle_action(ui_action_map_t *self) { with_lock(emudis_video_quick_toggle); }
static void screenshot_action(ui_action_map_t *self)   { with_lock(take_screenshot); }
static void help_action(ui_action_map_t *self)         { emudis_help_show(); }
static void history_action(ui_action_map_t *self)      { emudis_history_show(); }
static void about_action(ui_action_map_t *self)        { emudis_about_show(); }

static void fastload_toggle(void)
{
    emudis_fastload_set(!emudis_fastload_get());
}

static void fastload_action(ui_action_map_t *self)
{
    with_lock(fastload_toggle);
    vhk_gtk_set_check_item_blocked_by_action(ACTION_EMUDIS_FASTLOAD_TOGGLE,
                                             (gboolean)emudis_fastload_get());
    ui_display_statustext(emudis_fastload_get()
                          ? "Fast loading ON (true drive emulation off)"
                          : "Fast loading OFF (true drive emulation on)", true);
}

static const ui_action_map_t emudis_actions[] = {
    { .action = ACTION_EMUDIS_MEMVIEW,         .handler = memview_action,      .uithread = true },
    { .action = ACTION_EMUDIS_FONTS,           .handler = fonts_action,        .uithread = true },
    { .action = ACTION_EMUDIS_SPRITES,         .handler = sprites_action,      .uithread = true },
    { .action = ACTION_EMUDIS_SIDS,            .handler = sids_action,         .uithread = true },
    { .action = ACTION_EMUDIS_ASSEMBLER,       .handler = assembler_action,    .uithread = true },
    { .action = ACTION_EMUDIS_DISASSEMBLER,    .handler = disassembler_action, .uithread = true },
    { .action = ACTION_EMUDIS_SNAPSHOTS,       .handler = snapshots_action,    .uithread = true },
    { .action = ACTION_EMUDIS_VIDEO,           .handler = video_action,        .uithread = true },
    { .action = ACTION_EMUDIS_VIDEO_TOGGLE,    .handler = video_toggle_action, .uithread = true },
    { .action = ACTION_EMUDIS_SCREENSHOT,      .handler = screenshot_action,   .uithread = true },
    { .action = ACTION_EMUDIS_FASTLOAD_TOGGLE, .handler = fastload_action,     .uithread = true },
    { .action = ACTION_EMUDIS_HELP,            .handler = help_action,         .uithread = true },
    { .action = ACTION_EMUDIS_HISTORY,         .handler = history_action,      .uithread = true },
    { .action = ACTION_EMUDIS_ABOUT,           .handler = about_action,        .uithread = true },
    UI_ACTION_MAP_TERMINATOR
};

void actions_emudis_register(void)
{
    ui_actions_register(emudis_actions);
}

void actions_emudis_setup_ui(void)
{
    vhk_gtk_set_check_item_blocked_by_action(ACTION_EMUDIS_FASTLOAD_TOGGLE,
                                             (gboolean)emudis_fastload_get());
}
