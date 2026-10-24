/** \file   emudis_asmwin.c
 * \brief   C64-EmuDis - assembler window
 *
 * Editor for 6502/6510 assembler source with a two-pass assembler that
 * writes straight into the C64's memory. Programs can be started with SYS,
 * saved as PRG and checked in the VICE monitor (which has its own
 * line-by-line assembler, command `a`).
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

#include "kbdbuf.h"
#include "lib.h"
#include "mainlock.h"
#include "uiactions.h"

#include "emudis.h"
#include "emudis_6502.h"

typedef struct asmwin_s {
    GtkWidget *window;
    GtkWidget *editor;
    GtkWidget *listing;
    GtkWidget *origin;
    GtkWidget *run_entry;
    GtkWidget *status;
    gchar *filename;
    emudis_asm_result_t last;
    bool have_last;
} asmwin_t;

static asmwin_t *aw = NULL;

static const char *example_source =
    "; C64-EmuDis assembler example - colour bars in the border\n"
    "; Assemble & Write, then press Run (SYS 49152). Press a key to stop.\n"
    "\n"
    "BORDER  = $d020\n"
    "RASTER  = $d012\n"
    "\n"
    "        * = $c000\n"
    "\n"
    "start   sei\n"
    "        lda #$00\n"
    "        sta $dc00           ; select all keyboard columns\n"
    "loop    lda RASTER\n"
    "        sta BORDER          ; raster line value as colour\n"
    "        lda $dc01           ; keyboard matrix row\n"
    "        cmp #$ff\n"
    "        beq loop\n"
    "        lda #$7f\n"
    "        sta $dc00\n"
    "        lda #14             ; light blue\n"
    "        sta BORDER\n"
    "        cli\n"
    "        rts\n"
    "\n"
    "message !scr \"made with c64-emudis\", 0\n";


static gchar *get_source(void)
{
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(aw->editor));
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buf, &s, &e);
    return gtk_text_buffer_get_text(buf, &s, &e, FALSE);
}

static void set_listing(const char *text)
{
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(aw->listing));
    gtk_text_buffer_set_text(buf, text, -1);
}

static void set_status(const char *markup)
{
    gtk_label_set_markup(GTK_LABEL(aw->status), markup);
}

static uint16_t get_origin(void)
{
    long v;
    if (emudis_parse_number(gtk_entry_get_text(GTK_ENTRY(aw->origin)), &v) && v >= 0 && v <= 0xffff) {
        return (uint16_t)v;
    }
    return 0xc000;
}

static void goto_line(int line)
{
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(aw->editor));
    GtkTextIter it;

    gtk_text_buffer_get_iter_at_line(buf, &it, line - 1);
    gtk_text_buffer_place_cursor(buf, &it);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(aw->editor), &it, 0.2, FALSE, 0, 0);
}

static bool do_assemble(void)
{
    gchar *src = get_source();
    bool ok;
    char msg[256];

    if (aw->have_last) {
        emudis_asm_result_free(&aw->last);
    }
    ok = emudis_assemble(src, get_origin(), &aw->last);
    aw->have_last = true;
    set_listing(aw->last.listing);
    if (ok) {
        if (aw->last.bytes > 0) {
            char run[16];
            g_snprintf(msg, sizeof msg, "<span foreground='#2e9e2e'><b>OK</b></span>: %d bytes, $%04X-$%04X",
                       aw->last.bytes, (unsigned int)aw->last.lowest, (unsigned int)aw->last.highest);
            g_snprintf(run, sizeof run, "$%04X", (unsigned int)aw->last.lowest);
            if (gtk_entry_get_text_length(GTK_ENTRY(aw->run_entry)) == 0) {
                gtk_entry_set_text(GTK_ENTRY(aw->run_entry), run);
            }
        } else {
            g_strlcpy(msg, "OK, but nothing was assembled.", sizeof msg);
        }
    } else {
        g_snprintf(msg, sizeof msg,
                   "<span foreground='#d03030'><b>%d error(s)</b></span> - first in line %d (see listing)",
                   aw->last.errors, aw->last.first_error_line);
        if (aw->last.first_error_line > 0) {
            goto_line(aw->last.first_error_line);
        }
    }
    set_status(msg);
    g_free(src);
    return ok;
}

static void on_assemble(GtkWidget *w, gpointer d)
{
    do_assemble();
}

/* locked */
static void on_write(GtkWidget *w, gpointer d)
{
    int a, n = 0;
    char msg[160];

    if (!do_assemble() || aw->last.bytes == 0) {
        return;
    }
    for (a = aw->last.lowest; a <= aw->last.highest; a++) {
        if (aw->last.used[a]) {
            emudis_poke(EMUDIS_SPACE_RAM, (uint16_t)a, aw->last.mem[a]);
            n++;
        }
    }
    g_snprintf(msg, sizeof msg,
               "<span foreground='#2e9e2e'><b>Written</b></span> %d bytes to RAM $%04X-$%04X.",
               n, (unsigned int)aw->last.lowest, (unsigned int)aw->last.highest);
    set_status(msg);
}

/* locked */
static void on_run(GtkWidget *w, gpointer d)
{
    long v;
    char cmd[32];

    if (!emudis_parse_number(gtk_entry_get_text(GTK_ENTRY(aw->run_entry)), &v) || v < 0 || v > 0xffff) {
        set_status("Enter a valid start address for SYS.");
        return;
    }
    g_snprintf(cmd, sizeof cmd, "SYS%ld\r", v);
    if (kbdbuf_feed(cmd) < 0) {
        set_status("Could not type the SYS command (keyboard buffer busy).");
    } else {
        char msg[64];
        g_snprintf(msg, sizeof msg, "Typed <tt>SYS %ld</tt> into the C64.", v);
        set_status(msg);
    }
}

static void on_save_prg(GtkWidget *w, gpointer d)
{
    gchar *fn;
    char name[32];

    if (!do_assemble() || aw->last.bytes == 0) {
        return;
    }
    g_snprintf(name, sizeof name, "program_%04x.prg", (unsigned int)aw->last.lowest);
    fn = emudis_choose_file(GTK_WINDOW(aw->window), "Save program as PRG", true, name,
                            "PRG files", "*.prg;*.PRG");
    if (fn != NULL) {
        if (!emudis_save_data(fn, aw->last.mem + aw->last.lowest,
                              aw->last.highest - aw->last.lowest + 1, true,
                              (uint16_t)aw->last.lowest)) {
            emudis_message(GTK_WINDOW(aw->window), true, "Could not write '%s'.", fn);
        }
        g_free(fn);
    }
}

static void on_new(GtkWidget *w, gpointer d)
{
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(aw->editor)),
                             "        * = $c000\n\nstart   rts\n", -1);
    g_free(aw->filename);
    aw->filename = NULL;
}

static void on_open(GtkWidget *w, gpointer d)
{
    gchar *fn = emudis_choose_file(GTK_WINDOW(aw->window), "Open assembler source", false, NULL,
                                   "Assembler source", "*.asm;*.s;*.a;*.inc;*.txt");
    gchar *contents = NULL;

    if (fn == NULL) {
        return;
    }
    if (g_file_get_contents(fn, &contents, NULL, NULL)) {
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(aw->editor)), contents, -1);
        g_free(contents);
        g_free(aw->filename);
        aw->filename = fn;
    } else {
        emudis_message(GTK_WINDOW(aw->window), true, "Could not read '%s'.", fn);
        g_free(fn);
    }
}

static void on_save_src(GtkWidget *w, gpointer d)
{
    gchar *fn = emudis_choose_file(GTK_WINDOW(aw->window), "Save assembler source", true,
                                   aw->filename ? NULL : "program.asm", "Assembler source",
                                   "*.asm;*.s;*.a;*.txt");
    gchar *src;

    if (fn == NULL) {
        return;
    }
    src = get_source();
    if (!g_file_set_contents(fn, src, -1, NULL)) {
        emudis_message(GTK_WINDOW(aw->window), true, "Could not write '%s'.", fn);
        g_free(fn);
    } else {
        g_free(aw->filename);
        aw->filename = fn;
    }
    g_free(src);
}

static void on_monitor(GtkWidget *w, gpointer d)
{
    ui_action_trigger(ACTION_MONITOR_OPEN);
}

static void on_destroy(GtkWidget *w, gpointer d)
{
    if (aw != NULL) {
        if (aw->have_last) {
            emudis_asm_result_free(&aw->last);
        }
        g_free(aw->filename);
        lib_free(aw);
        aw = NULL;
    }
}

static GtkWidget *mono_view(bool editable)
{
    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), editable);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tv), editable);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 6);
    return tv;
}

static GtkWidget *scrolled(GtkWidget *child)
{
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), child);
    return sw;
}

void emudis_assembler_show(void)
{
    GtkWidget *vbox, *bar, *bar2, *paned, *b;

    if (aw != NULL) {
        gtk_window_present(GTK_WINDOW(aw->window));
        return;
    }
    aw = lib_calloc(1, sizeof *aw);

    aw->window = emudis_window_new("Assembler", 900, 720);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(aw->window), vbox);

    bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    b = emudis_button_new("New", NULL);
    g_signal_connect(b, "clicked", G_CALLBACK(on_new), NULL);
    gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);
    b = emudis_button_new("Open...", "Load a source file");
    g_signal_connect(b, "clicked", G_CALLBACK(on_open), NULL);
    gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);
    b = emudis_button_new("Save source...", NULL);
    g_signal_connect(b, "clicked", G_CALLBACK(on_save_src), NULL);
    gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(bar), gtk_label_new("Default origin:"), FALSE, FALSE, 0);
    aw->origin = emudis_entry_new("$C000", 7, "Used when the source has no '* = address' line");
    gtk_box_pack_start(GTK_BOX(bar), aw->origin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 4);
    b = emudis_button_new("Open VICE monitor", "The VICE monitor has its own line assembler: a <address> <instruction>");
    g_signal_connect(b, "clicked", G_CALLBACK(on_monitor), NULL);
    gtk_box_pack_end(GTK_BOX(bar), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bar, FALSE, FALSE, 0);

    bar2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    b = emudis_button_new("Assemble", "Check the source and show the listing (F5)");
    g_signal_connect_unlocked(b, "clicked", G_CALLBACK(on_assemble), NULL);
    gtk_box_pack_start(GTK_BOX(bar2), b, FALSE, FALSE, 0);
    b = emudis_button_new("Assemble + Write to RAM", "Assemble and poke the bytes into the C64's RAM");
    g_signal_connect(b, "clicked", G_CALLBACK(on_write), NULL);
    gtk_box_pack_start(GTK_BOX(bar2), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar2), gtk_label_new("Run at:"), FALSE, FALSE, 4);
    aw->run_entry = emudis_entry_new("", 7, "Start address for SYS");
    gtk_box_pack_start(GTK_BOX(bar2), aw->run_entry, FALSE, FALSE, 0);
    b = emudis_button_new("Run (SYS)", "Types SYS <address> into the C64 (BASIC must be at READY.)");
    g_signal_connect(b, "clicked", G_CALLBACK(on_run), NULL);
    gtk_box_pack_start(GTK_BOX(bar2), b, FALSE, FALSE, 0);
    b = emudis_button_new("Save .prg...", "Assemble and save as PRG file");
    g_signal_connect_unlocked(b, "clicked", G_CALLBACK(on_save_prg), NULL);
    gtk_box_pack_start(GTK_BOX(bar2), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bar2, FALSE, FALSE, 0);

    paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    aw->editor = mono_view(TRUE);
    aw->listing = mono_view(FALSE);
    gtk_paned_pack1(GTK_PANED(paned), scrolled(aw->editor), TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), scrolled(aw->listing), TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 400);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

    aw->status = emudis_label_new("Syntax: ACME / Kick Assembler style. See Help → C64-EmuDis Help.", true);
    gtk_box_pack_start(GTK_BOX(vbox), aw->status, FALSE, FALSE, 0);

    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(aw->editor)), example_source, -1);

    g_signal_connect_unlocked(aw->window, "destroy", G_CALLBACK(on_destroy), NULL);
    gtk_widget_show_all(aw->window);
}

/** \brief  Open the assembler with the given source text */
void emudis_assembler_load_text(const char *text)
{
    emudis_assembler_show();
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(aw->editor)), text, -1);
    gtk_window_present(GTK_WINDOW(aw->window));
}
