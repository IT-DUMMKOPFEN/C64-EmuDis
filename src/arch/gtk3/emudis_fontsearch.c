/** \file   emudis_fontsearch.c
 * \brief   C64-EmuDis - search character sets (fonts) in memory
 *
 * Scans the 64KB RAM for 2KB blocks that look like C64 character sets
 * and shows them as a 16x16 character grid. Fonts can be exported as raw
 * binary, PRG or PNG and activated in the VIC-II.
 *
 * Heuristics used for every candidate block (first 64 characters, which
 * hold letters, digits and punctuation in screen code order):
 *  - most characters are not empty and differ from each other
 *  - few isolated pixels (typical for graphics, rare in code/data)
 *  - plausible pixel density
 *  - many rows repeat the previous row (vertical strokes)
 *  - screen code 32 (space) is empty
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

#define FONT_SIZE   0x800
#define SRC_RAM     0
#define SRC_ROM_UP  1
#define SRC_ROM_LO  2

enum { COL_ADDR = 0, COL_SCORE, COL_NOTES, COL_SOURCE, COL_ADDRVAL, NUM_COLS };

typedef struct fontsearch_s {
    GtkWidget *window;
    GtkWidget *tree;
    GtkListStore *store;
    GtkWidget *area;
    GtkWidget *mc_check;
    GtkWidget *live_check;
    GtkWidget *fine_check;
    GtkWidget *threshold;
    GtkWidget *scale_combo;
    GtkWidget *fg_combo, *bg_combo, *mc1_combo, *mc2_combo;
    GtkWidget *info;
    uint8_t ram[EMUDIS_MEM_SIZE];
    uint8_t rom[0x1000];
    uint8_t font[FONT_SIZE];
    int sel_source;
    int sel_addr;
    bool have_sel;
    emudis_vic_state_t vic;
    guint timer;
} fontsearch_t;

static fontsearch_t *fs = NULL;


/* ------------------------------------------------------------------------ */
/* Heuristics                                                               */
/* ------------------------------------------------------------------------ */

static int popcount8(uint8_t b)
{
    int n = 0;
    while (b) {
        n += b & 1;
        b >>= 1;
    }
    return n;
}

/** \brief  How similar is the block to the ROM font (letters A-Z), in percent */
static int rom_similarity(const uint8_t *f, const uint8_t *rom)
{
    int same = 0;
    int c, y;

    for (c = 1; c <= 26; c++) {
        for (y = 0; y < 8; y++) {
            same += 8 - popcount8((uint8_t)(f[c * 8 + y] ^ rom[c * 8 + y]));
        }
    }
    return same * 100 / (26 * 64);
}


/* ------------------------------------------------------------------------ */
/* Preview                                                                  */
/* ------------------------------------------------------------------------ */

static int combo_value(GtkWidget *combo)
{
    int v = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    return v < 0 ? 0 : v;
}

static int preview_scale(void)
{
    return combo_value(fs->scale_combo) + 1;
}

static cairo_surface_t *render_font(void)
{
    cairo_surface_t *s = emudis_surface_new(16 * 8, 16 * 8);
    bool mc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fs->mc_check));
    int c;

    cairo_surface_flush(s);
    for (c = 0; c < 256; c++) {
        emudis_draw_char(s, (c % 16) * 8, (c / 16) * 8, fs->font + c * 8, mc,
                         combo_value(fs->fg_combo), combo_value(fs->bg_combo),
                         combo_value(fs->mc1_combo), combo_value(fs->mc2_combo));
    }
    cairo_surface_mark_dirty(s);
    return s;
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    cairo_surface_t *s;
    int scale;

    if (fs == NULL) {
        return FALSE;
    }
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.16);
    cairo_paint(cr);
    if (!fs->have_sel) {
        return TRUE;
    }
    scale = preview_scale();
    s = render_font();
    cairo_save(cr);
    cairo_translate(cr, 4, 4);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(s);

    /* grid lines every 16 chars */
    cairo_set_source_rgba(cr, 1, 1, 1, 0.15);
    cairo_set_line_width(cr, 1);
    {
        int i;
        for (i = 0; i <= 16; i++) {
            cairo_move_to(cr, 4 + i * 8 * scale + 0.5, 4);
            cairo_line_to(cr, 4 + i * 8 * scale + 0.5, 4 + 128 * scale);
            cairo_move_to(cr, 4, 4 + i * 8 * scale + 0.5);
            cairo_line_to(cr, 4 + 128 * scale, 4 + i * 8 * scale + 0.5);
        }
        cairo_stroke(cr);
    }
    return TRUE;
}

static void resize_area(void)
{
    int s = preview_scale();
    gtk_widget_set_size_request(fs->area, 128 * s + 8, 128 * s + 8);
    gtk_widget_queue_draw(fs->area);
}

/* lock must be held */
static void load_selected(void)
{
    if (!fs->have_sel) {
        return;
    }
    if (fs->sel_source == SRC_RAM) {
        emudis_read_block(EMUDIS_SPACE_RAM, (uint16_t)fs->sel_addr, fs->font, FONT_SIZE);
    } else {
        memcpy(fs->font, fs->rom + (fs->sel_source == SRC_ROM_LO ? FONT_SIZE : 0), FONT_SIZE);
    }
}

static gboolean on_live_timer(gpointer data)
{
    if (fs == NULL) {
        return G_SOURCE_REMOVE;
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fs->live_check)) && fs->have_sel) {
        mainlock_obtain();
        load_selected();
        mainlock_release();
        gtk_widget_queue_draw(fs->area);
    }
    return G_SOURCE_CONTINUE;
}


/* ------------------------------------------------------------------------ */
/* Scan                                                                     */
/* ------------------------------------------------------------------------ */

static void add_result(const char *addrtext, int score, const char *notes, int source, int addr)
{
    GtkTreeIter iter;
    gtk_list_store_append(fs->store, &iter);
    gtk_list_store_set(fs->store, &iter,
                       COL_ADDR, addrtext, COL_SCORE, score, COL_NOTES, notes,
                       COL_SOURCE, source, COL_ADDRVAL, addr, -1);
}

/* locked */
static void on_scan(GtkWidget *widget, gpointer data)
{
    int step = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fs->fine_check)) ? 0x100 : 0x800;
    int threshold = (int)gtk_range_get_value(GTK_RANGE(fs->threshold));
    int addr;
    int found = 0;
    char buf[256];

    emudis_read_all(EMUDIS_SPACE_RAM, fs->ram);
    emudis_read_charrom(fs->rom);
    emudis_vic_state_get(&fs->vic);

    gtk_list_store_clear(fs->store);

    add_result("ROM upper", 100,
               fs->vic.charset_is_rom && !(fs->vic.d018 & 2) ? "Character ROM, uppercase/graphics (ACTIVE)"
                                                              : "Character ROM, uppercase/graphics",
               SRC_ROM_UP, 0xd000);
    add_result("ROM lower", 100,
               fs->vic.charset_is_rom && (fs->vic.d018 & 2) ? "Character ROM, lowercase/uppercase (ACTIVE)"
                                                             : "Character ROM, lowercase/uppercase",
               SRC_ROM_LO, 0xd800);

    for (addr = 0; addr + FONT_SIZE <= EMUDIS_MEM_SIZE; addr += step) {
        int sc = emudis_score_font(fs->ram + addr);
        bool active = (!fs->vic.charset_is_rom && addr == fs->vic.charset);
        if (sc >= threshold || active) {
            char at[16];
            char notes[160];
            int sim = rom_similarity(fs->ram + addr, fs->rom);
            int vbank = addr / 0x4000;
            g_snprintf(at, sizeof at, "$%04X", (unsigned int)addr);
            g_snprintf(notes, sizeof notes, "%s%sVIC bank %d%s%s",
                       active ? "ACTIVE  " : "",
                       sim >= 90 ? "ROM copy  " : sim >= 70 ? "ROM-like  " : "",
                       vbank,
                       (addr & 0x7ff) ? "  (not 2KB aligned)" : "",
                       ((vbank == 0 || vbank == 2) && ((addr & 0x3fff) == 0x1000
                                                       || (addr & 0x3fff) == 0x1800))
                           ? "  (VIC sees ROM here)" : "");
            add_result(at, sc, notes, SRC_RAM, addr);
            found++;
        }
    }

    /* select the first RAM font (or the ROM font) so there is a preview */
    {
        GtkTreeIter it;
        GtkTreeModel *model = GTK_TREE_MODEL(fs->store);
        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(fs->tree));
        if (gtk_tree_model_iter_nth_child(model, &it, NULL, found > 0 ? 2 : 0)) {
            gtk_tree_selection_select_iter(sel, &it);
        }
    }

    g_snprintf(buf, sizeof buf,
               "<b>%d</b> possible fonts found in RAM (threshold %d, step $%X). "
               "Current VIC charset: $%04X%s",
               found, threshold, (unsigned int)step, fs->vic.charset,
               fs->vic.charset_is_rom ? " (ROM)" : "");
    gtk_label_set_markup(GTK_LABEL(fs->info), buf);
}

static void on_selection_changed(GtkTreeSelection *sel, gpointer data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        int src, addr;
        gtk_tree_model_get(model, &iter, COL_SOURCE, &src, COL_ADDRVAL, &addr, -1);
        fs->sel_source = src;
        fs->sel_addr = addr;
        fs->have_sel = true;
        if (src == SRC_RAM) {
            memcpy(fs->font, fs->ram + addr, FONT_SIZE);
        } else {
            memcpy(fs->font, fs->rom + (src == SRC_ROM_LO ? FONT_SIZE : 0), FONT_SIZE);
        }
        gtk_widget_queue_draw(fs->area);
    }
}


/* ------------------------------------------------------------------------ */
/* Export                                                                   */
/* ------------------------------------------------------------------------ */

static void suggested_name(char *buf, int size, const char *ext)
{
    if (fs->sel_source == SRC_RAM) {
        g_snprintf(buf, (gulong)size, "font_%04x.%s", (unsigned int)fs->sel_addr, ext);
    } else {
        g_snprintf(buf, (gulong)size, "font_rom_%s.%s",
                   fs->sel_source == SRC_ROM_LO ? "lower" : "upper", ext);
    }
}

static bool need_selection(void)
{
    if (!fs->have_sel) {
        emudis_message(GTK_WINDOW(fs->window), true, "Please scan and select a font first.");
        return false;
    }
    return true;
}

static void export_data(bool prg)
{
    char name[64];
    gchar *fn;

    if (!need_selection()) {
        return;
    }
    suggested_name(name, sizeof name, prg ? "prg" : "bin");
    fn = emudis_choose_file(GTK_WINDOW(fs->window), prg ? "Save font as PRG" : "Save font as binary",
                            true, name, prg ? "PRG files" : "Binary files",
                            prg ? "*.prg;*.PRG" : "*.bin;*.BIN;*.64c;*.chr");
    if (fn != NULL) {
        uint16_t la = (uint16_t)(fs->sel_source == SRC_RAM ? fs->sel_addr : 0x3800);
        if (!emudis_save_data(fn, fs->font, FONT_SIZE, prg, la)) {
            emudis_message(GTK_WINDOW(fs->window), true, "Could not write '%s'.", fn);
        }
        g_free(fn);
    }
}

static void on_save_bin(GtkWidget *w, gpointer d) { export_data(false); }
static void on_save_prg(GtkWidget *w, gpointer d) { export_data(true); }

static void on_save_png(GtkWidget *w, gpointer d)
{
    char name[64];
    gchar *fn;

    if (!need_selection()) {
        return;
    }
    suggested_name(name, sizeof name, "png");
    fn = emudis_choose_file(GTK_WINDOW(fs->window), "Save font as PNG", true, name,
                            "PNG images", "*.png;*.PNG");
    if (fn != NULL) {
        cairo_surface_t *s = render_font();
        if (!emudis_save_surface_png(s, fn, preview_scale())) {
            emudis_message(GTK_WINDOW(fs->window), true, "Could not write '%s'.", fn);
        }
        cairo_surface_destroy(s);
        g_free(fn);
    }
}

/* locked */
static void on_activate(GtkWidget *w, gpointer d)
{
    emudis_vic_state_t vic;
    int rel;

    if (!need_selection()) {
        return;
    }
    emudis_vic_state_get(&vic);
    if (fs->sel_source != SRC_RAM) {
        /* ROM font: only possible in VIC bank 0 or 2 */
        if (vic.bank != 0 && vic.bank != 2) {
            emudis_message(GTK_WINDOW(fs->window), true,
                           "The character ROM is only visible to the VIC in banks 0 and 2 "
                           "(current bank: %d).", vic.bank);
            return;
        }
        rel = fs->sel_source == SRC_ROM_LO ? 0x1800 : 0x1000;
    } else {
        rel = fs->sel_addr - vic.bank_base;
        if (rel < 0 || rel >= 0x4000 || (rel & 0x7ff)) {
            emudis_message(GTK_WINDOW(fs->window), true,
                           "The font at $%04X is not in the current VIC bank %d "
                           "($%04X-$%04X) or not 2KB aligned.",
                           (unsigned int)fs->sel_addr, vic.bank, vic.bank_base,
                           vic.bank_base + 0x3fff);
            return;
        }
    }
    emudis_poke(EMUDIS_SPACE_IO, 0xd018, (uint8_t)((vic.d018 & 0xf1) | ((rel / 0x800) << 1)));
}

static void on_options_changed(GtkWidget *w, gpointer d)
{
    resize_area();
}

/* locked */
static void on_use_vic_colors(GtkWidget *w, gpointer d)
{
    emudis_vic_state_t vic;
    uint8_t col;

    emudis_vic_state_get(&vic);
    /* use the colour RAM of the first screen cell as foreground */
    col = emudis_peek(EMUDIS_SPACE_IO, 0xd800) & 0x0f;
    gtk_combo_box_set_active(GTK_COMBO_BOX(fs->bg_combo), vic.bg[0]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(fs->mc1_combo), vic.bg[1]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(fs->mc2_combo), vic.bg[2]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(fs->fg_combo), col & 0x07);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fs->mc_check), vic.multicolor);
}

static void on_destroy(GtkWidget *w, gpointer d)
{
    if (fs != NULL) {
        if (fs->timer) {
            g_source_remove(fs->timer);
        }
        lib_free(fs);
        fs = NULL;
    }
}


/* ------------------------------------------------------------------------ */

void emudis_fontsearch_show(void)
{
    GtkWidget *paned, *left, *right, *scroll, *opts, *row, *b, *grid, *sw2;
    GtkCellRenderer *rend;
    GtkTreeViewColumn *col;
    int i;

    if (fs != NULL) {
        gtk_window_present(GTK_WINDOW(fs->window));
        return;
    }
    fs = lib_calloc(1, sizeof *fs);

    fs->window = emudis_window_new("Font Finder", 1000, 640);
    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_add(GTK_CONTAINER(fs->window), paned);

    /* left: controls + results */
    left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    b = emudis_button_new("Scan memory", "Search the 64KB RAM for character sets");
    g_signal_connect(b, "clicked", G_CALLBACK(on_scan), NULL);
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    fs->fine_check = gtk_check_button_new_with_label("Fine scan ($100 steps)");
    gtk_widget_set_tooltip_text(fs->fine_check,
                                "Also find fonts that are not 2KB aligned, e.g. inside packed files");
    gtk_box_pack_start(GTK_BOX(row), fs->fine_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), row, FALSE, FALSE, 0);

    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("Min. score:"), FALSE, FALSE, 0);
    fs->threshold = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 20, 95, 5);
    gtk_range_set_value(GTK_RANGE(fs->threshold), 60);
    gtk_widget_set_size_request(fs->threshold, 160, -1);
    gtk_box_pack_start(GTK_BOX(row), fs->threshold, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(left), row, FALSE, FALSE, 0);

    fs->store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING,
                                   G_TYPE_INT, G_TYPE_INT);
    fs->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(fs->store));
    rend = gtk_cell_renderer_text_new();
    g_object_set(rend, "family", "Monospace", NULL);
    col = gtk_tree_view_column_new_with_attributes("Address", rend, "text", COL_ADDR, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(fs->tree), col);
    col = gtk_tree_view_column_new_with_attributes("Score", gtk_cell_renderer_text_new(),
                                                   "text", COL_SCORE, NULL);
    gtk_tree_view_column_set_sort_column_id(col, COL_SCORE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(fs->tree), col);
    col = gtk_tree_view_column_new_with_attributes("Notes", gtk_cell_renderer_text_new(),
                                                   "text", COL_NOTES, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(fs->tree), col);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, 380, -1);
    gtk_container_add(GTK_CONTAINER(scroll), fs->tree);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(left), scroll, TRUE, TRUE, 0);
    fs->info = emudis_label_new("Press <b>Scan memory</b> to search for fonts.", true);
    gtk_label_set_line_wrap(GTK_LABEL(fs->info), TRUE);
    gtk_box_pack_start(GTK_BOX(left), fs->info, FALSE, FALSE, 0);
    gtk_paned_pack1(GTK_PANED(paned), left, FALSE, FALSE);

    /* right: preview + options */
    right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    opts = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(opts), 6);
    gtk_grid_set_row_spacing(GTK_GRID(opts), 4);
    fs->mc_check = gtk_check_button_new_with_label("Multicolor");
    fs->live_check = gtk_check_button_new_with_label("Live update");
    gtk_widget_set_tooltip_text(fs->live_check, "Re-read the selected font from memory while the C64 runs");
    fs->scale_combo = gtk_combo_box_text_new();
    for (i = 1; i <= 4; i++) {
        char t[8];
        g_snprintf(t, sizeof t, "%dx", i);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fs->scale_combo), t);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(fs->scale_combo), 2);
    fs->fg_combo = emudis_c64_color_combo(14);
    fs->bg_combo = emudis_c64_color_combo(6);
    fs->mc1_combo = emudis_c64_color_combo(1);
    fs->mc2_combo = emudis_c64_color_combo(11);
    gtk_grid_attach(GTK_GRID(opts), fs->mc_check, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), fs->live_check, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), gtk_label_new("Zoom"), 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), fs->scale_combo, 3, 0, 1, 1);
    b = emudis_button_new("Use VIC colours", "Take colours and multicolor mode from the VIC-II");
    g_signal_connect(b, "clicked", G_CALLBACK(on_use_vic_colors), NULL);
    gtk_grid_attach(GTK_GRID(opts), b, 4, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), gtk_label_new("Foreground"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), fs->fg_combo, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), gtk_label_new("Background"), 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), fs->bg_combo, 3, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), gtk_label_new("MC 1 ($D022)"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), fs->mc1_combo, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), gtk_label_new("MC 2 ($D023)"), 2, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(opts), fs->mc2_combo, 3, 2, 1, 1);
    gtk_box_pack_start(GTK_BOX(right), opts, FALSE, FALSE, 0);

    fs->area = gtk_drawing_area_new();
    sw2 = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw2), fs->area);
    gtk_widget_set_vexpand(sw2, TRUE);
    gtk_widget_set_hexpand(sw2, TRUE);
    gtk_box_pack_start(GTK_BOX(right), sw2, TRUE, TRUE, 0);

    grid = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    b = emudis_button_new("Save .bin", "Raw 2048 bytes");
    g_signal_connect(b, "clicked", G_CALLBACK(on_save_bin), NULL);
    gtk_box_pack_start(GTK_BOX(grid), b, FALSE, FALSE, 0);
    b = emudis_button_new("Save .prg", "2048 bytes with load address");
    g_signal_connect(b, "clicked", G_CALLBACK(on_save_prg), NULL);
    gtk_box_pack_start(GTK_BOX(grid), b, FALSE, FALSE, 0);
    b = emudis_button_new("Save .png", "Image of the 16x16 character grid at the current zoom");
    g_signal_connect(b, "clicked", G_CALLBACK(on_save_png), NULL);
    gtk_box_pack_start(GTK_BOX(grid), b, FALSE, FALSE, 0);
    b = emudis_button_new("Activate in VIC", "Point $D018 to this font (must be in the current VIC bank)");
    g_signal_connect(b, "clicked", G_CALLBACK(on_activate), NULL);
    gtk_box_pack_start(GTK_BOX(grid), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), grid, FALSE, FALSE, 0);
    gtk_paned_pack2(GTK_PANED(paned), right, TRUE, FALSE);

    g_signal_connect_unlocked(fs->area, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect_unlocked(gtk_tree_view_get_selection(GTK_TREE_VIEW(fs->tree)), "changed",
                     G_CALLBACK(on_selection_changed), NULL);
    g_signal_connect_unlocked(fs->mc_check, "toggled", G_CALLBACK(on_options_changed), NULL);
    g_signal_connect_unlocked(fs->scale_combo, "changed", G_CALLBACK(on_options_changed), NULL);
    g_signal_connect_unlocked(fs->fg_combo, "changed", G_CALLBACK(on_options_changed), NULL);
    g_signal_connect_unlocked(fs->bg_combo, "changed", G_CALLBACK(on_options_changed), NULL);
    g_signal_connect_unlocked(fs->mc1_combo, "changed", G_CALLBACK(on_options_changed), NULL);
    g_signal_connect_unlocked(fs->mc2_combo, "changed", G_CALLBACK(on_options_changed), NULL);
    g_signal_connect_unlocked(fs->window, "destroy", G_CALLBACK(on_destroy), NULL);

    resize_area();
    fs->timer = g_timeout_add(200, on_live_timer, NULL);
    gtk_widget_show_all(fs->window);

    /* scan right away (called with the lock held) */
    on_scan(NULL, NULL);
}
