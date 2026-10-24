/** \file   emudis_snapshots.c
 * \brief   C64-EmuDis - snapshot manager with 10 slots and thumbnails
 *
 * Each slot stores a complete VICE snapshot (.vsf, including ROMs and
 * attached disk images) plus a PNG thumbnail of the screen. Snapshots are
 * written and read inside a CPU trap on the emulation thread, exactly like
 * VICE's own quicksave/quickload.
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

#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interrupt.h"
#include "lib.h"
#include "machine.h"
#include "mainlock.h"
#include "screenshot.h"
#include "ui.h"
#include "util.h"

#include "emudis.h"

#define NUM_SLOTS   10
#define THUMB_W     192
#define THUMB_H     136

typedef struct slot_widgets_s {
    GtkWidget *image;
    GtkWidget *label;
    GtkWidget *load;
    GtkWidget *del;
} slot_widgets_t;

typedef struct snapshots_s {
    GtkWidget *window;
    GtkWidget *roms_check;
    GtkWidget *disks_check;
    GtkWidget *status;
    slot_widgets_t slots[NUM_SLOTS];
} snapshots_t;

typedef struct snap_job_s {
    int   slot;
    bool  save;
    bool  roms, disks;
    char *vsf;
    char *png;
    struct video_canvas_s *canvas;
    int   result;
} snap_job_t;

static snapshots_t *sn = NULL;


static char *slot_path(int slot, const char *ext)
{
    char name[32];
    g_snprintf(name, sizeof name, "slot%d.%s", slot, ext);
    return util_join_paths(emudis_user_dir("snapshots"), name, NULL);
}

static void set_status(const char *text)
{
    if (sn != NULL) {
        gtk_label_set_markup(GTK_LABEL(sn->status), text);
    }
}

static void refresh_slot(int slot)
{
    slot_widgets_t *w;
    char *vsf, *png;
    GStatBuf st;

    if (sn == NULL) {
        return;
    }
    w = &sn->slots[slot];
    vsf = slot_path(slot, "vsf");
    png = slot_path(slot, "png");

    if (g_stat(vsf, &st) == 0) {
        GDateTime *dt = g_date_time_new_from_unix_local((gint64)st.st_mtime);
        gchar *when = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");
        char text[128];
        GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(png, THUMB_W, THUMB_H, TRUE, NULL);

        if (pix != NULL) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(w->image), pix);
            g_object_unref(pix);
        } else {
            gtk_image_set_from_icon_name(GTK_IMAGE(w->image), "image-missing", GTK_ICON_SIZE_DIALOG);
        }
        g_snprintf(text, sizeof text, "<b>Slot %d</b>\n%s", slot + 1, when);
        gtk_label_set_markup(GTK_LABEL(w->label), text);
        gtk_widget_set_sensitive(w->load, TRUE);
        gtk_widget_set_sensitive(w->del, TRUE);
        g_free(when);
        g_date_time_unref(dt);
    } else {
        char text[64];
        gtk_image_set_from_icon_name(GTK_IMAGE(w->image), "document-new", GTK_ICON_SIZE_DIALOG);
        g_snprintf(text, sizeof text, "<b>Slot %d</b>\n<i>empty</i>", slot + 1);
        gtk_label_set_markup(GTK_LABEL(w->label), text);
        gtk_widget_set_sensitive(w->load, FALSE);
        gtk_widget_set_sensitive(w->del, FALSE);
    }
    lib_free(vsf);
    lib_free(png);
}

static void job_free(snap_job_t *job)
{
    lib_free(job->vsf);
    lib_free(job->png);
    lib_free(job);
}

/* runs on the UI thread after the trap finished */
static gboolean job_done_idle(gpointer data)
{
    snap_job_t *job = data;
    char msg[160];

    if (job->save) {
        if (job->result < 0) {
            g_snprintf(msg, sizeof msg, "<span foreground='#d03030'>Saving slot %d failed.</span>", job->slot + 1);
        } else {
            g_snprintf(msg, sizeof msg, "Saved slot %d.", job->slot + 1);
        }
    } else {
        if (job->result < 0) {
            g_snprintf(msg, sizeof msg, "<span foreground='#d03030'>Loading slot %d failed.</span>", job->slot + 1);
        } else {
            g_snprintf(msg, sizeof msg, "Loaded slot %d.", job->slot + 1);
        }
    }
    set_status(msg);
    refresh_slot(job->slot);
    job_free(job);
    return G_SOURCE_REMOVE;
}

/* emulation thread */
static void snap_trap(uint16_t addr, void *data)
{
    snap_job_t *job = data;

    if (job->save) {
        job->result = machine_write_snapshot(job->vsf, job->roms, job->disks, 0);
        if (job->result >= 0 && job->canvas != NULL) {
            screenshot_save("PNG", job->png, job->canvas);
        }
    } else {
        job->result = machine_read_snapshot(job->vsf, 0);
    }
    g_idle_add(job_done_idle, job);
}

/* locked */
static void start_job(int slot, bool save)
{
    snap_job_t *job = lib_calloc(1, sizeof *job);

    job->slot = slot;
    job->save = save;
    job->roms = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sn->roms_check));
    job->disks = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sn->disks_check));
    job->vsf = slot_path(slot, "vsf");
    job->png = slot_path(slot, "png");
    job->canvas = ui_get_active_canvas();
    set_status(save ? "Saving..." : "Loading...");
    interrupt_maincpu_trigger_trap(snap_trap, job);
}

static void on_save(GtkWidget *w, gpointer data)
{
    start_job(GPOINTER_TO_INT(data), true);
}

static void on_load(GtkWidget *w, gpointer data)
{
    start_job(GPOINTER_TO_INT(data), false);
}

static void on_delete(GtkWidget *w, gpointer data)
{
    int slot = GPOINTER_TO_INT(data);
    char *vsf = slot_path(slot, "vsf");
    char *png = slot_path(slot, "png");
    g_unlink(vsf);
    g_unlink(png);
    lib_free(vsf);
    lib_free(png);
    refresh_slot(slot);
    set_status("Slot deleted.");
}

static void on_open_folder(GtkWidget *w, gpointer data)
{
    gchar *uri = g_filename_to_uri(emudis_user_dir("snapshots"), NULL, NULL);
    if (uri != NULL) {
        gtk_show_uri_on_window(GTK_WINDOW(sn->window), uri, GDK_CURRENT_TIME, NULL);
        g_free(uri);
    }
}

static void on_destroy(GtkWidget *w, gpointer d)
{
    if (sn != NULL) {
        lib_free(sn);
        sn = NULL;
    }
}

void emudis_snapshots_show(void)
{
    GtkWidget *vbox, *grid, *row, *b;
    int i;

    if (sn != NULL) {
        gtk_window_present(GTK_WINDOW(sn->window));
        return;
    }
    sn = lib_calloc(1, sizeof *sn);

    sn->window = emudis_window_new("Snapshot Manager", 1060, 520);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(sn->window), vbox);

    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    sn->roms_check = gtk_check_button_new_with_label("Include ROMs");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sn->roms_check), TRUE);
    sn->disks_check = gtk_check_button_new_with_label("Include attached disk images");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sn->disks_check), TRUE);
    gtk_box_pack_start(GTK_BOX(row), sn->roms_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), sn->disks_check, FALSE, FALSE, 0);
    b = emudis_button_new("Open snapshot folder", NULL);
    g_signal_connect_unlocked(b, "clicked", G_CALLBACK(on_open_folder), NULL);
    gtk_box_pack_end(GTK_BOX(row), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), row, FALSE, FALSE, 0);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    for (i = 0; i < NUM_SLOTS; i++) {
        GtkWidget *frame = gtk_frame_new(NULL);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        slot_widgets_t *w = &sn->slots[i];

        gtk_container_set_border_width(GTK_CONTAINER(box), 4);
        w->image = gtk_image_new();
        gtk_widget_set_size_request(w->image, THUMB_W, THUMB_H);
        w->label = emudis_label_new("", false);
        b = emudis_button_new("Save", "Save the current state into this slot");
        g_signal_connect(b, "clicked", G_CALLBACK(on_save), GINT_TO_POINTER(i));
        w->load = emudis_button_new("Load", "Restore this state");
        g_signal_connect(w->load, "clicked", G_CALLBACK(on_load), GINT_TO_POINTER(i));
        w->del = emudis_button_new("Delete", NULL);
        g_signal_connect_unlocked(w->del, "clicked", G_CALLBACK(on_delete), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(btns), b, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(btns), w->load, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(btns), w->del, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), w->image, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), w->label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), btns, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(frame), box);
        gtk_grid_attach(GTK_GRID(grid), frame, i % 5, i / 5, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(vbox), grid, TRUE, TRUE, 0);

    sn->status = emudis_label_new("Snapshots contain the complete machine state. "
                                  "VICE's own <i>Snapshot</i> menu (load/save/quicksave) still works as well.", true);
    gtk_box_pack_start(GTK_BOX(vbox), sn->status, FALSE, FALSE, 0);

    g_signal_connect_unlocked(sn->window, "destroy", G_CALLBACK(on_destroy), NULL);
    for (i = 0; i < NUM_SLOTS; i++) {
        refresh_slot(i);
    }
    gtk_widget_show_all(sn->window);
}
