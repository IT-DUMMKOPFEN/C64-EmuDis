/** \file   emudis_video.c
 * \brief   C64-EmuDis - simple MP4 video recorder
 *
 * Records the emulator screen (original resolution including the border)
 * plus sound to an MP4 file (H.264 + AAC) using VICE's FFmpeg driver.
 * The picture can be scaled 2x, 3x or 4x with sharp (nearest neighbour)
 * pixels. Requires the ffmpeg executable (bundled or in the PATH).
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "machine.h"
#include "mainlock.h"
#include "resources.h"
#include "screenshot.h"
#include "ui.h"
#include "uiapi.h"
#include "uimedia.h"
#include "util.h"

#include "emudis.h"

/* codec IDs used by the FFmpeg executable driver (see ffmpegexedrv.c) */
#define EMUDIS_CODEC_NONE   0
#define EMUDIS_CODEC_AAC    5
#define EMUDIS_CODEC_H264   10

typedef struct video_s {
    GtkWidget *window;
    GtkWidget *scale[4];
    GtkWidget *border_check;
    GtkWidget *audio_check;
    GtkWidget *half_check;
    GtkWidget *bitrate;
    GtkWidget *file_entry;
    GtkWidget *start_btn;
    GtkWidget *stop_btn;
    GtkWidget *status;
    GtkWidget *resinfo;
    guint timer;
} video_t;

static video_t *vr = NULL;
static gint64 rec_start = 0;
static char *last_file = NULL;
static int last_scale = 2;


static int selected_scale(void)
{
    int i;
    if (vr == NULL) {
        return last_scale;
    }
    for (i = 0; i < 4; i++) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vr->scale[i]))) {
            return i + 1;
        }
    }
    return 1;
}

static void base_size(int *w, int *h)
{
    int std = MACHINE_SYNC_PAL;
    resources_get_int("MachineVideoStandard", &std);
    *w = 384;
    *h = (std == MACHINE_SYNC_NTSC || std == 3) ? 247 : 272;
}

static void update_resinfo(void)
{
    int w, h, s = selected_scale();
    char buf[200];

    base_size(&w, &h);
    g_snprintf(buf, sizeof buf,
               "Output: <b>%d × %d</b> pixels (%d × %d with normal border × %d), H.264 / AAC in MP4.",
               w * s, h * s, w, h, s);
    gtk_label_set_markup(GTK_LABEL(vr->resinfo), buf);
}

static char *default_filename(void)
{
    const char *dir = g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS);
    GDateTime *now = g_date_time_new_now_local();
    gchar *stamp = g_date_time_format(now, "%Y%m%d-%H%M%S");
    char *name = lib_msprintf("c64-emudis-%s.mp4", stamp);
    char *path;

    if (dir == NULL || !g_file_test(dir, G_FILE_TEST_IS_DIR)) {
        dir = g_get_home_dir();
    }
    path = util_join_paths(dir, name, NULL);
    lib_free(name);
    g_free(stamp);
    g_date_time_unref(now);
    return path;
}

static void update_buttons(void)
{
    bool rec = screenshot_is_recording() != 0;
    if (vr == NULL) {
        return;
    }
    gtk_widget_set_sensitive(vr->start_btn, !rec);
    gtk_widget_set_sensitive(vr->stop_btn, rec);
}

static gboolean on_timer(gpointer data)
{
    if (vr == NULL) {
        return G_SOURCE_REMOVE;
    }
    if (screenshot_is_recording() && rec_start != 0) {
        gint64 secs = (g_get_monotonic_time() - rec_start) / G_USEC_PER_SEC;
        char buf[256];
        g_snprintf(buf, sizeof buf, "<span foreground='#d03030'><b>● REC</b></span> %02d:%02d  %s",
                   (int)(secs / 60), (int)(secs % 60), last_file != NULL ? last_file : "");
        gtk_label_set_markup(GTK_LABEL(vr->status), buf);
    }
    update_buttons();
    return G_SOURCE_CONTINUE;
}

/** \brief  Start recording with the given settings (lock must be held) */
static bool start_recording(const char *filename, int scale, bool border, bool audio,
                            bool half, int bitrate_kbit)
{
    if (screenshot_is_recording()) {
        return false;
    }
    if (border) {
        resources_set_int("VICIIBorderMode", 0);
    }
    /* the FFmpeg driver is started by the sound system, so sound must be on */
    resources_set_int("Sound", 1);
    resources_set_string("FFMPEGFormat", "mp4");
    resources_set_int("FFMPEGVideoCodec", EMUDIS_CODEC_H264);
    resources_set_int("FFMPEGAudioCodec", audio ? EMUDIS_CODEC_AAC : EMUDIS_CODEC_NONE);
    resources_set_int("FFMPEGVideoBitrate", bitrate_kbit * 1000);
    resources_set_int("FFMPEGAudioBitrate", 192000);
    resources_set_int("FFMPEGVideoHalveFramerate", half ? 1 : 0);
    resources_set_int("FFMPEGVideoScale", scale);

    if (screenshot_save("FFMPEG", filename, ui_get_active_canvas()) < 0) {
        return false;
    }
    if (!screenshot_is_recording()) {
        ui_media_stop_recording();
        return false;
    }
    ui_display_recording(UI_RECORDING_STATUS_VIDEO);
    rec_start = g_get_monotonic_time();
    lib_free(last_file);
    last_file = lib_strdup(filename);
    last_scale = scale;
    return true;
}

/* locked */
static void on_start(GtkWidget *w, gpointer d)
{
    const char *fn = gtk_entry_get_text(GTK_ENTRY(vr->file_entry));
    char *auto_fn = NULL;

    if (fn == NULL || *fn == '\0') {
        auto_fn = default_filename();
        gtk_entry_set_text(GTK_ENTRY(vr->file_entry), auto_fn);
        fn = auto_fn;
    }
    if (!start_recording(fn, selected_scale(),
                         gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vr->border_check)),
                         gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vr->audio_check)),
                         gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vr->half_check)),
                         gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(vr->bitrate)))) {
        gtk_label_set_markup(GTK_LABEL(vr->status),
                             "<span foreground='#d03030'>Recording could not be started.</span> "
                             "Is the <b>ffmpeg</b> program available (next to C64-EmuDis or in the PATH)?");
    }
    lib_free(auto_fn);
    update_buttons();
}

/* locked */
static void on_stop(GtkWidget *w, gpointer d)
{
    if (screenshot_is_recording()) {
        ui_media_stop_recording();
    }
    rec_start = 0;
    if (vr != NULL) {
        char buf[300];
        char *next = default_filename();
        g_snprintf(buf, sizeof buf, "Saved: %s", last_file != NULL ? last_file : "");
        gtk_label_set_text(GTK_LABEL(vr->status), buf);
        gtk_entry_set_text(GTK_ENTRY(vr->file_entry), next);
        lib_free(next);
        update_buttons();
    }
}

static void on_browse(GtkWidget *w, gpointer d)
{
    char *def = default_filename();
    gchar *fn = emudis_choose_file(GTK_WINDOW(vr->window), "Record video to", true,
                                   strrchr(def, G_DIR_SEPARATOR) ? strrchr(def, G_DIR_SEPARATOR) + 1 : def,
                                   "MP4 video", "*.mp4;*.MP4");
    if (fn != NULL) {
        if (!g_str_has_suffix(fn, ".mp4") && !g_str_has_suffix(fn, ".MP4")) {
            gchar *t = g_strconcat(fn, ".mp4", NULL);
            g_free(fn);
            fn = t;
        }
        gtk_entry_set_text(GTK_ENTRY(vr->file_entry), fn);
        g_free(fn);
    }
    lib_free(def);
}

static void on_scale_toggled(GtkWidget *w, gpointer d)
{
    static const int rates[] = { 4000, 8000, 12000, 16000 };
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) {
        int s = selected_scale();
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(vr->bitrate), rates[s - 1]);
        update_resinfo();
    }
}

static void on_destroy(GtkWidget *w, gpointer d)
{
    if (vr != NULL) {
        if (vr->timer) {
            g_source_remove(vr->timer);
        }
        lib_free(vr);
        vr = NULL;
    }
}

void emudis_video_show(void)
{
    GtkWidget *grid, *row, *b, *vbox;
    char *fn;
    int i;

    if (vr != NULL) {
        gtk_window_present(GTK_WINDOW(vr->window));
        return;
    }
    vr = lib_calloc(1, sizeof *vr);
    vr->window = emudis_window_new("Video Recorder", 620, 300);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(vr->window), vbox);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);

    gtk_grid_attach(GTK_GRID(grid), emudis_label_new("<b>Size</b>", true), 0, 0, 1, 1);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    for (i = 0; i < 4; i++) {
        char t[32];
        g_snprintf(t, sizeof t, i == 0 ? "1x (original)" : "%dx", i + 1);
        vr->scale[i] = i == 0 ? gtk_radio_button_new_with_label(NULL, t)
                              : gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(vr->scale[0]), t);
        gtk_box_pack_start(GTK_BOX(row), vr->scale[i], FALSE, FALSE, 0);
    }
    gtk_grid_attach(GTK_GRID(grid), row, 1, 0, 2, 1);

    vr->border_check = gtk_check_button_new_with_label("Record with normal border (set VIC-II border mode to 'normal')");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vr->border_check), TRUE);
    gtk_grid_attach(GTK_GRID(grid), vr->border_check, 1, 1, 2, 1);
    vr->audio_check = gtk_check_button_new_with_label("Record sound (AAC)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vr->audio_check), TRUE);
    gtk_grid_attach(GTK_GRID(grid), vr->audio_check, 1, 2, 1, 1);
    vr->half_check = gtk_check_button_new_with_label("Half frame rate (25/30 fps)");
    gtk_grid_attach(GTK_GRID(grid), vr->half_check, 2, 2, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), emudis_label_new("<b>Bitrate</b>", true), 0, 3, 1, 1);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    vr->bitrate = gtk_spin_button_new_with_range(500, 50000, 500);
    gtk_box_pack_start(GTK_BOX(row), vr->bitrate, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("kbit/s"), FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), row, 1, 3, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), emudis_label_new("<b>File</b>", true), 0, 4, 1, 1);
    vr->file_entry = emudis_entry_new("", 48, NULL);
    gtk_widget_set_hexpand(vr->file_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), vr->file_entry, 1, 4, 1, 1);
    b = emudis_button_new("Browse...", NULL);
    g_signal_connect_unlocked(b, "clicked", G_CALLBACK(on_browse), NULL);
    gtk_grid_attach(GTK_GRID(grid), b, 2, 4, 1, 1);
    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);

    vr->resinfo = emudis_label_new("", true);
    gtk_box_pack_start(GTK_BOX(vbox), vr->resinfo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox),
                       emudis_label_new("<small>Tip: 2x or more keeps colours razor sharp, because MP4 stores "
                                        "colour at half resolution (4:2:0).</small>", true),
                       FALSE, FALSE, 0);

    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    vr->start_btn = emudis_button_new("● Start recording", NULL);
    vr->stop_btn = emudis_button_new("■ Stop", NULL);
    g_signal_connect(vr->start_btn, "clicked", G_CALLBACK(on_start), NULL);
    g_signal_connect(vr->stop_btn, "clicked", G_CALLBACK(on_stop), NULL);
    gtk_box_pack_start(GTK_BOX(row), vr->start_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), vr->stop_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), row, FALSE, FALSE, 0);
    vr->status = emudis_label_new("", true);
    gtk_label_set_ellipsize(GTK_LABEL(vr->status), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(vbox), vr->status, FALSE, FALSE, 0);

    for (i = 0; i < 4; i++) {
        g_signal_connect_unlocked(vr->scale[i], "toggled", G_CALLBACK(on_scale_toggled), NULL);
    }
    g_signal_connect_unlocked(vr->window, "destroy", G_CALLBACK(on_destroy), NULL);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vr->scale[last_scale - 1]), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(vr->bitrate), last_scale * 4000);
    fn = default_filename();
    gtk_entry_set_text(GTK_ENTRY(vr->file_entry), fn);
    lib_free(fn);
    update_resinfo();
    update_buttons();
    vr->timer = g_timeout_add(500, on_timer, NULL);
    gtk_widget_show_all(vr->window);
}

/** \brief  Quick start/stop of an MP4 recording with the last used size
 *
 * lock must be held
 */
void emudis_video_quick_toggle(void)
{
    if (screenshot_is_recording()) {
        on_stop(NULL, NULL);
        return;
    }
    {
        char *fn = default_filename();
        if (!start_recording(fn, last_scale, true, true, false, last_scale * 4000)) {
            emudis_message(NULL, true,
                           "Recording could not be started.\n\nC64-EmuDis needs the ffmpeg program "
                           "(next to the C64-EmuDis executable or in the PATH).");
        }
        lib_free(fn);
    }
    if (vr != NULL) {
        update_buttons();
    }
}
