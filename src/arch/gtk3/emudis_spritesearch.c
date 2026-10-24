/** \file   emudis_spritesearch.c
 * \brief   C64-EmuDis - search sprites in memory
 *
 * Scans RAM in 64 byte steps (sprite data is always 64-byte aligned for the
 * VIC-II) and rates every block by how much it looks like a 24x21 graphic:
 * few isolated pixels, connected shapes, repeated rows, plausible density.
 * Sprites currently shown by the VIC-II are always listed and marked.
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
#include "mainlock.h"

#include "emudis.h"

#define MAX_RESULTS 1024
#define CELL_W      56
#define CELL_H      60

typedef struct sprite_hit_s {
    uint16_t addr;
    int      score;
    int      active;    /* -1 or hardware sprite number */
} sprite_hit_t;

typedef struct spritesearch_s {
    GtkWidget *window;
    GtkWidget *grid_area;
    GtkWidget *preview;
    GtkWidget *mc_check;
    GtkWidget *live_check;
    GtkWidget *threshold;
    GtkWidget *fg_combo, *bg_combo, *mc1_combo, *mc2_combo;
    GtkWidget *info;
    GtkWidget *hwinfo;
    GtkWidget *selinfo;
    uint8_t ram[EMUDIS_MEM_SIZE];
    sprite_hit_t hits[MAX_RESULTS];
    int nhits;
    int selected;       /* index into hits, -1 */
    emudis_vic_state_t vic;
    guint timer;
} spritesearch_t;

static spritesearch_t *ss = NULL;


/* ------------------------------------------------------------------------ */

static int combo_value(GtkWidget *combo)
{
    int v = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    return v < 0 ? 0 : v;
}

static bool is_mc(void)
{
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ss->mc_check));
}

static void draw_sprite_at(cairo_t *cr, const uint8_t *d, double x, double y, int scale)
{
    cairo_surface_t *s = emudis_surface_new(24, 21);

    cairo_surface_flush(s);
    emudis_draw_sprite(s, 0, 0, d, is_mc(), combo_value(ss->fg_combo), combo_value(ss->bg_combo),
                       combo_value(ss->mc1_combo), combo_value(ss->mc2_combo));
    cairo_surface_mark_dirty(s);
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(s);
}

static int grid_columns(void)
{
    int w = gtk_widget_get_allocated_width(ss->grid_area);
    int c = w / CELL_W;
    return c < 1 ? 1 : c;
}

static gboolean on_grid_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    int cols, i;
    PangoLayout *layout;
    PangoFontDescription *font;

    if (ss == NULL) {
        return FALSE;
    }
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.16);
    cairo_paint(cr);
    cols = grid_columns();
    layout = pango_cairo_create_layout(cr);
    font = pango_font_description_from_string("Monospace 7");
    pango_layout_set_font_description(layout, font);

    for (i = 0; i < ss->nhits; i++) {
        double x = (i % cols) * CELL_W + 4;
        double y = (i / cols) * CELL_H + 4;
        char t[16];

        if (i == ss->selected) {
            cairo_set_source_rgb(cr, 0.44, 0.43, 0.92);
            cairo_rectangle(cr, x - 3, y - 3, 54, 58);
            cairo_fill(cr);
        } else if (ss->hits[i].active >= 0) {
            cairo_set_source_rgb(cr, 0.46, 0.81, 0.78);
            cairo_rectangle(cr, x - 2, y - 2, 52, 46);
            cairo_stroke(cr);
        }
        draw_sprite_at(cr, ss->ram + ss->hits[i].addr, x, y, 2);
        g_snprintf(t, sizeof t, "%04X", ss->hits[i].addr);
        cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
        cairo_move_to(cr, x + 6, y + 43);
        pango_layout_set_text(layout, t, -1);
        pango_cairo_show_layout(cr, layout);
    }
    pango_font_description_free(font);
    g_object_unref(layout);
    return TRUE;
}

static gboolean on_preview_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    if (ss == NULL) {
        return FALSE;
    }
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.16);
    cairo_paint(cr);
    if (ss->selected >= 0) {
        int scale = 8;
        const uint8_t *d = ss->ram + ss->hits[ss->selected].addr;
        int i;
        draw_sprite_at(cr, d, 4, 4, scale);
        /* pixel grid */
        cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
        cairo_set_line_width(cr, 1);
        for (i = 0; i <= 24; i += (is_mc() ? 2 : 1)) {
            cairo_move_to(cr, 4 + i * scale + 0.5, 4);
            cairo_line_to(cr, 4 + i * scale + 0.5, 4 + 21 * scale);
        }
        for (i = 0; i <= 21; i++) {
            cairo_move_to(cr, 4, 4 + i * scale + 0.5);
            cairo_line_to(cr, 4 + 24 * scale, 4 + i * scale + 0.5);
        }
        cairo_stroke(cr);
    }
    return TRUE;
}

static void update_grid_size(void)
{
    int cols = grid_columns();
    int rows = (ss->nhits + cols - 1) / cols;
    gtk_widget_set_size_request(ss->grid_area, -1, rows * CELL_H + 8);
    gtk_widget_queue_draw(ss->grid_area);
}

static void update_selection_info(void)
{
    char buf[256];

    if (ss->selected < 0) {
        gtk_label_set_markup(GTK_LABEL(ss->selinfo), "Click a sprite to select it.");
        return;
    }
    {
        sprite_hit_t *h = &ss->hits[ss->selected];
        int rel = h->addr - ss->vic.bank_base;
        char ptr[64] = "";
        if (rel >= 0 && rel < 0x4000) {
            g_snprintf(ptr, sizeof ptr, "  pointer value $%02X (bank %d)", (unsigned int)(rel / 64), ss->vic.bank);
        } else {
            g_snprintf(ptr, sizeof ptr, "  VIC bank %d, pointer $%02X", h->addr / 0x4000,
                       (unsigned int)((h->addr & 0x3fff) / 64));
        }
        g_snprintf(buf, sizeof buf, "<tt><b>$%04X-$%04X</b>  score %d%s%s</tt>",
                   (unsigned int)h->addr, (unsigned int)(h->addr + 63), h->score, ptr,
                   h->active >= 0 ? "  <b>ACTIVE</b>" : "");
        gtk_label_set_markup(GTK_LABEL(ss->selinfo), buf);
    }
}

static void update_hwinfo(void)
{
    GString *s = g_string_new("<tt><b>VIC-II sprites</b>\n");
    int i;

    for (i = 0; i < 8; i++) {
        g_string_append_printf(s, "#%d %s ptr $%02X → $%04X  X %3d Y %3d  col %2d%s%s%s\n",
                               i, (ss->vic.sprite_enable >> i) & 1 ? "ON " : "off",
                               ss->vic.sprite_ptr[i], ss->vic.sprite_addr[i],
                               ss->vic.sprite_x[i], ss->vic.sprite_y[i], ss->vic.sprite_color[i],
                               (ss->vic.sprite_mc >> i) & 1 ? " MC" : "",
                               (ss->vic.sprite_xexp >> i) & 1 ? " 2W" : "",
                               (ss->vic.sprite_yexp >> i) & 1 ? " 2H" : "");
    }
    g_string_append_printf(s, "MC colours: $D025=%d  $D026=%d</tt>",
                           ss->vic.sprite_mc0, ss->vic.sprite_mc1);
    gtk_label_set_markup(GTK_LABEL(ss->hwinfo), s->str);
    g_string_free(s, TRUE);
}


/* ------------------------------------------------------------------------ */

static int active_sprite_at(uint16_t addr)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (((ss->vic.sprite_enable >> i) & 1) && ss->vic.sprite_addr[i] == addr) {
            return i;
        }
    }
    return -1;
}

/* locked */
static void on_scan(GtkWidget *widget, gpointer data)
{
    int threshold = (int)gtk_range_get_value(GTK_RANGE(ss->threshold));
    int addr;
    char buf[256];
    int i;
    bool font_block[32];

    emudis_read_all(EMUDIS_SPACE_RAM, ss->ram);
    emudis_vic_state_get(&ss->vic);
    ss->nhits = 0;
    ss->selected = -1;

    /* active sprites first */
    for (i = 0; i < 8; i++) {
        if ((ss->vic.sprite_enable >> i) & 1) {
            int k;
            bool dup = false;
            for (k = 0; k < ss->nhits; k++) {
                if (ss->hits[k].addr == ss->vic.sprite_addr[i]) {
                    dup = true;
                }
            }
            if (!dup) {
                ss->hits[ss->nhits].addr = ss->vic.sprite_addr[i];
                ss->hits[ss->nhits].score = emudis_score_sprite(ss->ram + ss->vic.sprite_addr[i]);
                ss->hits[ss->nhits].active = i;
                ss->nhits++;
            }
        }
    }

    /* blocks that look like character sets are not searched for sprites */
    {
        int b;
        for (b = 0; b < 32; b++) {
            font_block[b] = (emudis_score_font(ss->ram + b * 0x800) >= 70);
        }
    }

    for (addr = 0; addr < EMUDIS_MEM_SIZE && ss->nhits < MAX_RESULTS; addr += 64) {
        int sc;
        if (active_sprite_at((uint16_t)addr) >= 0 || font_block[addr / 0x800]) {
            continue;
        }
        sc = emudis_score_sprite(ss->ram + addr);
        if (sc >= threshold) {
            ss->hits[ss->nhits].addr = (uint16_t)addr;
            ss->hits[ss->nhits].score = sc;
            ss->hits[ss->nhits].active = -1;
            ss->nhits++;
        }
    }

    if (ss->nhits > 0) {
        ss->selected = 0;
    }
    g_snprintf(buf, sizeof buf, "<b>%d</b> possible sprites (score ≥ %d). Framed = shown by the VIC right now.",
               ss->nhits, threshold);
    gtk_label_set_markup(GTK_LABEL(ss->info), buf);
    update_hwinfo();
    update_selection_info();
    update_grid_size();
    gtk_widget_queue_draw(ss->preview);
}

static gboolean on_grid_click(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    int cols = grid_columns();
    int c = (int)(event->x / CELL_W);
    int r = (int)(event->y / CELL_H);
    int i = r * cols + c;

    if (c < cols && i >= 0 && i < ss->nhits) {
        ss->selected = i;
        update_selection_info();
        gtk_widget_queue_draw(ss->grid_area);
        gtk_widget_queue_draw(ss->preview);
    }
    return TRUE;
}

static void on_grid_size(GtkWidget *w, GdkRectangle *a, gpointer d)
{
    static int last_cols = -1;
    int cols = grid_columns();
    if (cols != last_cols) {
        last_cols = cols;
        update_grid_size();
    }
}

static void on_options_changed(GtkWidget *w, gpointer d)
{
    gtk_widget_queue_draw(ss->grid_area);
    gtk_widget_queue_draw(ss->preview);
}

static gboolean on_live_timer(gpointer data)
{
    if (ss == NULL) {
        return G_SOURCE_REMOVE;
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ss->live_check))) {
        mainlock_obtain();
        emudis_read_all(EMUDIS_SPACE_RAM, ss->ram);
        emudis_vic_state_get(&ss->vic);
        mainlock_release();
        update_hwinfo();
        gtk_widget_queue_draw(ss->grid_area);
        gtk_widget_queue_draw(ss->preview);
    }
    return G_SOURCE_CONTINUE;
}

/* locked */
static void on_use_vic_colors(GtkWidget *w, gpointer d)
{
    int spr = 0;
    emudis_vic_state_get(&ss->vic);
    if (ss->selected >= 0 && ss->hits[ss->selected].active >= 0) {
        spr = ss->hits[ss->selected].active;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(ss->fg_combo), ss->vic.sprite_color[spr]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(ss->bg_combo), ss->vic.bg[0]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(ss->mc1_combo), ss->vic.sprite_mc0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(ss->mc2_combo), ss->vic.sprite_mc1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ss->mc_check), (ss->vic.sprite_mc >> spr) & 1);
}

static bool need_sel(void)
{
    if (ss->selected < 0) {
        emudis_message(GTK_WINDOW(ss->window), true, "Please select a sprite first.");
        return false;
    }
    return true;
}

static void export_one(int kind)
{
    char name[64];
    gchar *fn;
    uint16_t a;
    static const char *ext[] = { "bin", "prg", "png" };

    if (!need_sel()) {
        return;
    }
    a = ss->hits[ss->selected].addr;
    g_snprintf(name, sizeof name, "sprite_%04x.%s", a, ext[kind]);
    fn = emudis_choose_file(GTK_WINDOW(ss->window), "Save sprite", true, name,
                            kind == 2 ? "PNG images" : kind == 1 ? "PRG files" : "Sprite data",
                            kind == 2 ? "*.png" : kind == 1 ? "*.prg" : "*.bin;*.spr;*.raw");
    if (fn == NULL) {
        return;
    }
    if (kind == 2) {
        cairo_surface_t *s = emudis_surface_new(24, 21);
        cairo_surface_flush(s);
        emudis_draw_sprite(s, 0, 0, ss->ram + a, is_mc(), combo_value(ss->fg_combo),
                           combo_value(ss->bg_combo), combo_value(ss->mc1_combo),
                           combo_value(ss->mc2_combo));
        cairo_surface_mark_dirty(s);
        if (!emudis_save_surface_png(s, fn, 8)) {
            emudis_message(GTK_WINDOW(ss->window), true, "Could not write '%s'.", fn);
        }
        cairo_surface_destroy(s);
    } else if (!emudis_save_data(fn, ss->ram + a, 64, kind == 1, a)) {
        emudis_message(GTK_WINDOW(ss->window), true, "Could not write '%s'.", fn);
    }
    g_free(fn);
}

static void on_save_bin(GtkWidget *w, gpointer d) { export_one(0); }
static void on_save_prg(GtkWidget *w, gpointer d) { export_one(1); }
static void on_save_png(GtkWidget *w, gpointer d) { export_one(2); }

static void on_save_all(GtkWidget *w, gpointer d)
{
    gchar *fn;
    int i;

    if (ss->nhits == 0) {
        emudis_message(GTK_WINDOW(ss->window), true, "No sprites found yet.");
        return;
    }
    fn = emudis_choose_file(GTK_WINDOW(ss->window), "Save all found sprites (raw, 64 bytes each)",
                            true, "sprites_all.bin", "Sprite data", "*.bin;*.spr;*.raw");
    if (fn != NULL) {
        uint8_t *buf = lib_malloc((size_t)ss->nhits * 64);
        for (i = 0; i < ss->nhits; i++) {
            memcpy(buf + i * 64, ss->ram + ss->hits[i].addr, 64);
        }
        if (!emudis_save_data(fn, buf, ss->nhits * 64, false, 0)) {
            emudis_message(GTK_WINDOW(ss->window), true, "Could not write '%s'.", fn);
        }
        lib_free(buf);
        g_free(fn);
    }
}

static void on_save_sheet(GtkWidget *w, gpointer d)
{
    gchar *fn;
    int i, cols = 8;

    if (ss->nhits == 0) {
        emudis_message(GTK_WINDOW(ss->window), true, "No sprites found yet.");
        return;
    }
    fn = emudis_choose_file(GTK_WINDOW(ss->window), "Save sprite sheet as PNG", true,
                            "sprites_sheet.png", "PNG images", "*.png");
    if (fn != NULL) {
        int rows = (ss->nhits + cols - 1) / cols;
        cairo_surface_t *s = emudis_surface_new(cols * 26, rows * 23);
        cairo_surface_flush(s);
        for (i = 0; i < ss->nhits; i++) {
            emudis_draw_sprite(s, (i % cols) * 26 + 1, (i / cols) * 23 + 1, ss->ram + ss->hits[i].addr,
                               is_mc(), combo_value(ss->fg_combo), combo_value(ss->bg_combo),
                               combo_value(ss->mc1_combo), combo_value(ss->mc2_combo));
        }
        cairo_surface_mark_dirty(s);
        if (!emudis_save_surface_png(s, fn, 3)) {
            emudis_message(GTK_WINDOW(ss->window), true, "Could not write '%s'.", fn);
        }
        cairo_surface_destroy(s);
        g_free(fn);
    }
}

static void on_destroy(GtkWidget *w, gpointer d)
{
    if (ss != NULL) {
        if (ss->timer) {
            g_source_remove(ss->timer);
        }
        lib_free(ss);
        ss = NULL;
    }
}


/* ------------------------------------------------------------------------ */

void emudis_spritesearch_show(void)
{
    GtkWidget *paned, *left, *right, *row, *b, *scroll, *opts;

    if (ss != NULL) {
        gtk_window_present(GTK_WINDOW(ss->window));
        return;
    }
    ss = lib_calloc(1, sizeof *ss);
    ss->selected = -1;

    ss->window = emudis_window_new("Sprite Finder", 1040, 680);
    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_add(GTK_CONTAINER(ss->window), paned);

    left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    b = emudis_button_new("Scan memory", "Search RAM for sprite data (64-byte blocks)");
    g_signal_connect(b, "clicked", G_CALLBACK(on_scan), NULL);
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("Min. score:"), FALSE, FALSE, 0);
    ss->threshold = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 30, 95, 5);
    gtk_range_set_value(GTK_RANGE(ss->threshold), 70);
    gtk_widget_set_size_request(ss->threshold, 160, -1);
    gtk_box_pack_start(GTK_BOX(row), ss->threshold, FALSE, FALSE, 0);
    ss->live_check = gtk_check_button_new_with_label("Live update");
    gtk_box_pack_start(GTK_BOX(row), ss->live_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), row, FALSE, FALSE, 0);

    ss->grid_area = gtk_drawing_area_new();
    gtk_widget_add_events(ss->grid_area, GDK_BUTTON_PRESS_MASK);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), ss->grid_area);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_size_request(scroll, 520, -1);
    gtk_box_pack_start(GTK_BOX(left), scroll, TRUE, TRUE, 0);
    ss->info = emudis_label_new("Press <b>Scan memory</b>.", true);
    gtk_box_pack_start(GTK_BOX(left), ss->info, FALSE, FALSE, 0);
    gtk_paned_pack1(GTK_PANED(paned), left, TRUE, FALSE);

    right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    ss->preview = gtk_drawing_area_new();
    gtk_widget_set_size_request(ss->preview, 24 * 8 + 8, 21 * 8 + 8);
    gtk_box_pack_start(GTK_BOX(right), ss->preview, FALSE, FALSE, 0);
    ss->selinfo = emudis_label_new("", true);
    gtk_box_pack_start(GTK_BOX(right), ss->selinfo, FALSE, FALSE, 0);

    opts = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(opts), 6);
    gtk_grid_set_row_spacing(GTK_GRID(opts), 4);
    ss->mc_check = gtk_check_button_new_with_label("Multicolor");
    ss->fg_combo = emudis_c64_color_combo(1);
    ss->bg_combo = emudis_c64_color_combo(6);
    ss->mc1_combo = emudis_c64_color_combo(2);
    ss->mc2_combo = emudis_c64_color_combo(7);
    gtk_grid_attach(GTK_GRID(opts), ss->mc_check, 0, 0, 1, 1);
    b = emudis_button_new("Use VIC colours", "Take colours of the selected active sprite");
    g_signal_connect(b, "clicked", G_CALLBACK(on_use_vic_colors), NULL);
    gtk_grid_attach(GTK_GRID(opts), b, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), gtk_label_new("Sprite colour"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), ss->fg_combo, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), gtk_label_new("Background"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), ss->bg_combo, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), gtk_label_new("MC 1 ($D025)"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), ss->mc1_combo, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), gtk_label_new("MC 2 ($D026)"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), ss->mc2_combo, 1, 4, 1, 1);
    gtk_box_pack_start(GTK_BOX(right), opts, FALSE, FALSE, 0);

    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    b = emudis_button_new("Save .bin", "64 bytes raw sprite data");
    g_signal_connect(b, "clicked", G_CALLBACK(on_save_bin), NULL);
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    b = emudis_button_new("Save .prg", "64 bytes with load address");
    g_signal_connect(b, "clicked", G_CALLBACK(on_save_prg), NULL);
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    b = emudis_button_new("Save .png", "8x enlarged image");
    g_signal_connect(b, "clicked", G_CALLBACK(on_save_png), NULL);
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), row, FALSE, FALSE, 0);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    b = emudis_button_new("Save all (.bin)", "All found sprites, 64 bytes each (SpritePad raw style)");
    g_signal_connect(b, "clicked", G_CALLBACK(on_save_all), NULL);
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    b = emudis_button_new("Save sheet (.png)", "All found sprites as one image");
    g_signal_connect(b, "clicked", G_CALLBACK(on_save_sheet), NULL);
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), row, FALSE, FALSE, 0);

    ss->hwinfo = emudis_label_new("", true);
    gtk_box_pack_start(GTK_BOX(right), ss->hwinfo, FALSE, FALSE, 6);
    gtk_paned_pack2(GTK_PANED(paned), right, FALSE, FALSE);

    g_signal_connect_unlocked(ss->grid_area, "draw", G_CALLBACK(on_grid_draw), NULL);
    g_signal_connect_unlocked(ss->grid_area, "button-press-event", G_CALLBACK(on_grid_click), NULL);
    g_signal_connect_unlocked(ss->grid_area, "size-allocate", G_CALLBACK(on_grid_size), NULL);
    g_signal_connect_unlocked(ss->preview, "draw", G_CALLBACK(on_preview_draw), NULL);
    g_signal_connect_unlocked(ss->mc_check, "toggled", G_CALLBACK(on_options_changed), NULL);
    g_signal_connect_unlocked(ss->fg_combo, "changed", G_CALLBACK(on_options_changed), NULL);
    g_signal_connect_unlocked(ss->bg_combo, "changed", G_CALLBACK(on_options_changed), NULL);
    g_signal_connect_unlocked(ss->mc1_combo, "changed", G_CALLBACK(on_options_changed), NULL);
    g_signal_connect_unlocked(ss->mc2_combo, "changed", G_CALLBACK(on_options_changed), NULL);
    g_signal_connect_unlocked(ss->window, "destroy", G_CALLBACK(on_destroy), NULL);

    ss->timer = g_timeout_add(200, on_live_timer, NULL);
    gtk_widget_show_all(ss->window);
    on_scan(NULL, NULL);
}
