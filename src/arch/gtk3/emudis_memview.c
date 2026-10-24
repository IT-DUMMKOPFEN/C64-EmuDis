/** \file   emudis_memview.c
 * \brief   C64-EmuDis - live memory viewer
 *
 * Shows the 64KB address space as hex dump with PETSCII/screen code/ASCII
 * column or as a 256x256 memory map, refreshed while the C64 is running.
 * Changed bytes are highlighted and fade out. Bytes can be edited, ranges
 * saved and loaded, and byte/text patterns searched.
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

#define BYTES_PER_ROW   16
#define ROWS_TOTAL      (EMUDIS_MEM_SIZE / BYTES_PER_ROW)
#define AGE_MAX         10
#define HEX_COL         7       /* column where hex bytes start */
#define CHR_COL         58      /* column where the character column starts */

enum { MODE_HEX = 0, MODE_MAP };
enum { TXT_PETSCII_UP = 0, TXT_PETSCII_LO, TXT_SCREEN_UP, TXT_SCREEN_LO, TXT_ASCII };

typedef struct memview_s {
    GtkWidget *window;
    GtkWidget *area;
    GtkWidget *scrollbar;
    GtkAdjustment *adj;
    GtkWidget *space_combo;
    GtkWidget *mode_combo;
    GtkWidget *text_combo;
    GtkWidget *rate_combo;
    GtkWidget *addr_entry;
    GtkWidget *find_entry;
    GtkWidget *from_entry;
    GtkWidget *to_entry;
    GtkWidget *status;
    GtkWidget *vicinfo;
    GtkWidget *highlight;

    uint8_t cur[EMUDIS_MEM_SIZE];
    uint8_t prev[EMUDIS_MEM_SIZE];
    uint8_t age[EMUDIS_MEM_SIZE];
    uint8_t charrom[0x1000];
    bool have_data;
    bool have_prev;

    emudis_vic_state_t vic;
    emudis_space_t space;
    int mode;
    int text_mode;
    int selected;
    int nibble;
    int hover;
    guint timer;
    int rate_ms;

    double cw;          /* character width */
    double lh;          /* line height */
    PangoFontDescription *font;
    cairo_surface_t *glyphs;   /* A8 atlas: 512 glyphs (2 sets) x 8x8 */
} memview_t;

static memview_t *mv = NULL;

static const int rates_ms[] = { 0, 1000, 200, 100, 40, 20 };
static const char *rate_names[] = { "Off (manual)", "1 Hz", "5 Hz", "10 Hz", "25 Hz", "50 Hz" };


/* ------------------------------------------------------------------------ */

static void build_glyph_atlas(void)
{
    int g, row, col;
    unsigned char *data;
    int stride;

    if (mv->glyphs != NULL) {
        cairo_surface_destroy(mv->glyphs);
    }
    mv->glyphs = cairo_image_surface_create(CAIRO_FORMAT_A8, 512 * 8, 8);
    cairo_surface_flush(mv->glyphs);
    data = cairo_image_surface_get_data(mv->glyphs);
    stride = cairo_image_surface_get_stride(mv->glyphs);
    memset(data, 0, (size_t)(stride * 8));
    for (g = 0; g < 512; g++) {
        for (row = 0; row < 8; row++) {
            uint8_t b = mv->charrom[g * 8 + row];
            for (col = 0; col < 8; col++) {
                data[row * stride + g * 8 + col] = ((b >> (7 - col)) & 1) ? 0xff : 0x00;
            }
        }
    }
    cairo_surface_mark_dirty(mv->glyphs);
}

/* lock must be held */
static void read_memory(void)
{
    if (mv->have_data) {
        memcpy(mv->prev, mv->cur, EMUDIS_MEM_SIZE);
        mv->have_prev = true;
    }
    emudis_read_all(mv->space, mv->cur);
    emudis_vic_state_get(&mv->vic);
    if (!mv->have_data) {
        emudis_read_charrom(mv->charrom);
        build_glyph_atlas();
    }
    mv->have_data = true;

    if (mv->have_prev) {
        int i;
        for (i = 0; i < EMUDIS_MEM_SIZE; i++) {
            if (mv->cur[i] != mv->prev[i]) {
                mv->age[i] = AGE_MAX;
            } else if (mv->age[i] > 0) {
                mv->age[i]--;
            }
        }
    }
}

static void update_status(void)
{
    char buf[512];
    char vbuf[512];
    const emudis_vic_state_t *v = &mv->vic;
    int a = mv->selected >= 0 ? mv->selected : (mv->hover >= 0 ? mv->hover : -1);

    if (a >= 0) {
        uint8_t b = mv->cur[a];
        char bin[9];
        int i;
        for (i = 0; i < 8; i++) {
            bin[i] = (char)((b & (0x80 >> i)) ? '1' : '0');
        }
        bin[8] = '\0';
        g_snprintf(buf, sizeof buf,
                   "<tt>%s $%04X = $%02X  %3u  %%%s  word $%04X</tt>",
                   mv->selected >= 0 ? "Selected" : "Pointer",
                   (unsigned int)a, b, b, bin,
                   (unsigned int)(b | (mv->cur[(a + 1) & 0xffff] << 8)));
    } else {
        g_snprintf(buf, sizeof buf,
                   "<tt>Click a byte to select it, type hex digits to edit. Space: %s</tt>",
                   emudis_space_name(mv->space));
    }
    gtk_label_set_markup(GTK_LABEL(mv->status), buf);

    g_snprintf(vbuf, sizeof vbuf,
               "<tt>VIC bank %d ($%04X)  screen $%04X  charset $%04X%s  bitmap $%04X  "
               "mode: %s%s%s  sprites on: $%02X</tt>",
               v->bank, v->bank_base, v->screen, v->charset,
               v->charset_is_rom ? " (ROM)" : "", v->bitmap,
               v->bitmap_mode ? "bitmap" : "text",
               v->multicolor ? " multicolor" : "",
               v->ecm ? " ECM" : "",
               v->sprite_enable);
    gtk_label_set_markup(GTK_LABEL(mv->vicinfo), vbuf);
}

static gboolean on_timer(gpointer data)
{
    if (mv == NULL) {
        return G_SOURCE_REMOVE;
    }
    mainlock_obtain();
    read_memory();
    mainlock_release();
    update_status();
    gtk_widget_queue_draw(mv->area);
    return G_SOURCE_CONTINUE;
}

static void restart_timer(void)
{
    if (mv->timer != 0) {
        g_source_remove(mv->timer);
        mv->timer = 0;
    }
    if (mv->rate_ms > 0) {
        mv->timer = g_timeout_add((guint)mv->rate_ms, on_timer, NULL);
    }
}


/* ------------------------------------------------------------------------ */
/* Drawing                                                                  */
/* ------------------------------------------------------------------------ */

static void set_rgb(cairo_t *cr, uint32_t rgb)
{
    cairo_set_source_rgb(cr, ((rgb >> 16) & 0xff) / 255.0,
                         ((rgb >> 8) & 0xff) / 255.0, (rgb & 0xff) / 255.0);
}

static void measure_font(GtkWidget *widget)
{
    PangoLayout *layout;
    int w, h;

    if (mv->font == NULL) {
        mv->font = pango_font_description_from_string("Monospace 10");
    }
    layout = gtk_widget_create_pango_layout(widget, "0000000000");
    pango_layout_set_font_description(layout, mv->font);
    pango_layout_get_pixel_size(layout, &w, &h);
    mv->cw = w / 10.0;
    mv->lh = h < 11 ? 11 : h;
    g_object_unref(layout);
}

static int visible_rows(void)
{
    int h = gtk_widget_get_allocated_height(mv->area);
    int rows = (int)(h / mv->lh);
    return rows < 1 ? 1 : rows;
}

static int top_row(void)
{
    return (int)gtk_adjustment_get_value(mv->adj);
}

static void draw_glyph(cairo_t *cr, int glyph, double x, double y, uint32_t rgb)
{
    cairo_save(cr);
    cairo_rectangle(cr, x, y, 8, 8);
    cairo_clip(cr);
    set_rgb(cr, rgb);
    cairo_mask_surface(cr, mv->glyphs, x - glyph * 8, y);
    cairo_restore(cr);
}

static int text_glyph(uint8_t b, bool *ascii)
{
    *ascii = false;
    switch (mv->text_mode) {
        case TXT_PETSCII_UP:
            return emudis_petscii_to_screencode(b);
        case TXT_PETSCII_LO:
            return 256 + emudis_petscii_to_screencode(b);
        case TXT_SCREEN_UP:
            return b;
        case TXT_SCREEN_LO:
            return 256 + b;
        default:
            *ascii = true;
            return b;
    }
}

static void draw_hex(cairo_t *cr)
{
    PangoLayout *layout;
    int rows = visible_rows();
    int top = top_row();
    int r, i;
    double y;

    layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, mv->font);

    for (r = 0; r < rows + 1 && top + r < ROWS_TOTAL; r++) {
        int row = top + r;
        uint16_t base = (uint16_t)(row * BYTES_PER_ROW);
        char text[16];

        y = r * mv->lh;

        /* address */
        g_snprintf(text, sizeof text, "%04X:", (unsigned int)base);
        set_rgb(cr, 0x75cec8);
        cairo_move_to(cr, 0, y);
        pango_layout_set_text(layout, text, -1);
        pango_cairo_show_layout(cr, layout);

        for (i = 0; i < BYTES_PER_ROW; i++) {
            int addr = base + i;
            uint8_t b = mv->cur[addr];
            double x = (HEX_COL + i * 3 + (i >= 8 ? 1 : 0)) * mv->cw;
            double cx = CHR_COL * mv->cw + i * 9;
            bool ascii;
            int g;
            uint32_t col = b == 0 ? 0x6a6a80 : 0xe0e0e0;

            /* change highlight */
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mv->highlight))
                && mv->age[addr] > 0) {
                double a = mv->age[addr] / (double)AGE_MAX;
                cairo_set_source_rgba(cr, 0.85, 0.15, 0.15, 0.25 + 0.6 * a);
                cairo_rectangle(cr, x - 1, y, mv->cw * 2 + 2, mv->lh);
                cairo_fill(cr);
                cairo_rectangle(cr, cx - 1, y, 9, mv->lh);
                cairo_fill(cr);
                col = 0xffff80;
            }
            /* selection */
            if (addr == mv->selected) {
                set_rgb(cr, 0x706deb);
                cairo_rectangle(cr, x - 1, y, mv->cw * 2 + 2, mv->lh);
                cairo_fill(cr);
                cairo_rectangle(cr, cx - 1, y, 9, mv->lh);
                cairo_fill(cr);
                col = 0xffffff;
                if (mv->nibble == 1) {
                    set_rgb(cr, 0xedf171);
                    cairo_rectangle(cr, x + mv->cw, y + mv->lh - 2, mv->cw, 2);
                    cairo_fill(cr);
                }
            } else if (addr == mv->hover) {
                cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
                cairo_rectangle(cr, x - 1, y, mv->cw * 2 + 2, mv->lh);
                cairo_fill(cr);
            }

            g_snprintf(text, sizeof text, "%02X", b);
            set_rgb(cr, col);
            cairo_move_to(cr, x, y);
            pango_layout_set_text(layout, text, -1);
            pango_cairo_show_layout(cr, layout);

            g = text_glyph(b, &ascii);
            if (ascii) {
                char ch[2];
                ch[0] = (b >= 0x20 && b < 0x7f) ? (char)b : '.';
                ch[1] = '\0';
                set_rgb(cr, (b >= 0x20 && b < 0x7f) ? 0xa9ff9f : 0x55556a);
                cairo_move_to(cr, cx, y);
                pango_layout_set_text(layout, ch, -1);
                pango_cairo_show_layout(cr, layout);
            } else {
                draw_glyph(cr, g, cx, y + (mv->lh - 8) / 2, 0xa9ff9f);
            }
        }
    }
    g_object_unref(layout);
}

static int map_scale(void)
{
    int w = gtk_widget_get_allocated_width(mv->area);
    int h = gtk_widget_get_allocated_height(mv->area);
    int s = MIN(w, h) / 256;
    return s < 1 ? 1 : s;
}

static void draw_map(cairo_t *cr)
{
    cairo_surface_t *s = emudis_surface_new(256, 256);
    int scale = map_scale();
    int i;
    bool hl = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mv->highlight));

    cairo_surface_flush(s);
    for (i = 0; i < EMUDIS_MEM_SIZE; i++) {
        uint32_t v = mv->cur[i];
        uint32_t rgb = (v << 16) | (v << 8) | v;
        if (hl && mv->age[i] > 0) {
            uint32_t a = (uint32_t)(155 + 10 * mv->age[i]);
            rgb = (MIN(a, 255u) << 16) | (40u << 8) | 40u;
        }
        emudis_surface_pixel(s, i & 0xff, i >> 8, rgb);
    }
    cairo_surface_mark_dirty(s);

    cairo_save(cr);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(s);

    /* mark VIC screen + charset */
    cairo_set_line_width(cr, 1);
    cairo_set_source_rgba(cr, 0.46, 0.81, 0.78, 0.9);
    cairo_rectangle(cr, 0.5, (mv->vic.screen >> 8) * scale + 0.5, 256 * scale - 1, 4 * scale);
    cairo_stroke(cr);
    if (!mv->vic.charset_is_rom) {
        cairo_set_source_rgba(cr, 0.93, 0.95, 0.44, 0.9);
        cairo_rectangle(cr, 0.5, (mv->vic.charset >> 8) * scale + 0.5, 256 * scale - 1, 8 * scale);
        cairo_stroke(cr);
    }
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    if (mv == NULL) {
        return FALSE;
    }
    if (mv->cw <= 0) {
        measure_font(widget);
    }
    set_rgb(cr, 0x1b1b2f);
    cairo_paint(cr);
    if (!mv->have_data) {
        return TRUE;
    }
    if (mv->mode == MODE_HEX) {
        draw_hex(cr);
    } else {
        draw_map(cr);
    }
    return TRUE;
}


/* ------------------------------------------------------------------------ */
/* Navigation                                                               */
/* ------------------------------------------------------------------------ */

static void update_adjustment(void)
{
    int rows = visible_rows();
    double value = gtk_adjustment_get_value(mv->adj);
    double upper = ROWS_TOTAL;

    if (value > upper - rows) {
        value = MAX(0, upper - rows);
    }
    gtk_adjustment_configure(mv->adj, value, 0, upper, 1, rows, rows);
}

static void goto_address(int addr)
{
    int rows;
    int row;

    if (mv->mode != MODE_HEX) {
        mv->mode = MODE_HEX;
        gtk_combo_box_set_active(GTK_COMBO_BOX(mv->mode_combo), MODE_HEX);
    }
    addr &= 0xffff;
    rows = visible_rows();
    row = addr / BYTES_PER_ROW;
    if (row < top_row() || row >= top_row() + rows) {
        gtk_adjustment_set_value(mv->adj, MAX(0, row - rows / 3));
    }
    mv->selected = addr;
    mv->nibble = 0;
    update_status();
    gtk_widget_queue_draw(mv->area);
}

static int hit_test(double px, double py)
{
    if (mv->mode == MODE_HEX) {
        int r = (int)(py / mv->lh);
        int row = top_row() + r;
        int col;
        double cx0 = CHR_COL * mv->cw;

        if (row < 0 || row >= ROWS_TOTAL) {
            return -1;
        }
        if (px >= cx0 && px < cx0 + 16 * 9) {
            return row * 16 + (int)((px - cx0) / 9);
        }
        col = (int)(px / mv->cw) - HEX_COL;
        if (col < 0) {
            return -1;
        }
        if (col >= 24) {
            col--;      /* gap after 8 bytes */
        }
        if (col < 0 || col >= 48) {
            return -1;
        }
        return row * 16 + col / 3;
    } else {
        int s = map_scale();
        int x = (int)(px / s);
        int y = (int)(py / s);
        if (x < 0 || y < 0 || x > 255 || y > 255) {
            return -1;
        }
        return y * 256 + x;
    }
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    int a = hit_test(event->x, event->y);

    gtk_widget_grab_focus(widget);
    if (a < 0) {
        return TRUE;
    }
    if (mv->mode == MODE_MAP) {
        goto_address(a);
    } else {
        mv->selected = a;
        mv->nibble = 0;
        update_status();
        gtk_widget_queue_draw(mv->area);
    }
    return TRUE;
}

static gboolean on_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    int a = hit_test(event->x, event->y);
    if (a != mv->hover) {
        mv->hover = a;
        if (mv->selected < 0) {
            update_status();
        }
        gtk_widget_queue_draw(mv->area);
    }
    return TRUE;
}

static gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
    double v = gtk_adjustment_get_value(mv->adj);
    double dy = 0;

    if (event->direction == GDK_SCROLL_UP) {
        dy = -3;
    } else if (event->direction == GDK_SCROLL_DOWN) {
        dy = 3;
    } else if (event->direction == GDK_SCROLL_SMOOTH) {
        dy = event->delta_y * 3;
    }
    gtk_adjustment_set_value(mv->adj, v + dy);
    return TRUE;
}

static int hexval(guint key)
{
    if (key >= GDK_KEY_0 && key <= GDK_KEY_9) {
        return (int)(key - GDK_KEY_0);
    }
    if (key >= GDK_KEY_KP_0 && key <= GDK_KEY_KP_9) {
        return (int)(key - GDK_KEY_KP_0);
    }
    if (key >= GDK_KEY_a && key <= GDK_KEY_f) {
        return (int)(key - GDK_KEY_a + 10);
    }
    if (key >= GDK_KEY_A && key <= GDK_KEY_F) {
        return (int)(key - GDK_KEY_A + 10);
    }
    return -1;
}

/* locked handler */
static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    int h;

    if (mv->mode != MODE_HEX) {
        return FALSE;
    }
    switch (event->keyval) {
        case GDK_KEY_Page_Up:
            gtk_adjustment_set_value(mv->adj, top_row() - visible_rows());
            return TRUE;
        case GDK_KEY_Page_Down:
            gtk_adjustment_set_value(mv->adj, top_row() + visible_rows());
            return TRUE;
        default:
            break;
    }
    if (mv->selected < 0) {
        return FALSE;
    }
    switch (event->keyval) {
        case GDK_KEY_Left:  goto_address(mv->selected - 1); return TRUE;
        case GDK_KEY_Right: goto_address(mv->selected + 1); return TRUE;
        case GDK_KEY_Up:    goto_address(mv->selected - 16); return TRUE;
        case GDK_KEY_Down:  goto_address(mv->selected + 16); return TRUE;
        case GDK_KEY_Escape:
            mv->selected = -1;
            update_status();
            gtk_widget_queue_draw(mv->area);
            return TRUE;
        default:
            break;
    }
    h = hexval(event->keyval);
    if (h >= 0) {
        uint8_t b = mv->cur[mv->selected];
        if (mv->space == EMUDIS_SPACE_ROM) {
            gtk_label_set_markup(GTK_LABEL(mv->status), "<tt>ROM cannot be edited.</tt>");
            return TRUE;
        }
        if (mv->nibble == 0) {
            b = (uint8_t)((b & 0x0f) | (h << 4));
        } else {
            b = (uint8_t)((b & 0xf0) | h);
        }
        emudis_poke(mv->space, (uint16_t)mv->selected, b);
        mv->cur[mv->selected] = b;
        if (mv->nibble == 0) {
            mv->nibble = 1;
            gtk_widget_queue_draw(mv->area);
        } else {
            goto_address(mv->selected + 1);
        }
        return TRUE;
    }
    return FALSE;
}

static void on_adj_changed(GtkAdjustment *adj, gpointer data)
{
    gtk_widget_queue_draw(mv->area);
}

static void on_size_allocate(GtkWidget *widget, GdkRectangle *alloc, gpointer data)
{
    if (mv->cw <= 0) {
        measure_font(widget);
    }
    update_adjustment();
}


/* ------------------------------------------------------------------------ */
/* Toolbar handlers (locked)                                                */
/* ------------------------------------------------------------------------ */

static void refresh_now(void)
{
    read_memory();
    update_status();
    gtk_widget_queue_draw(mv->area);
}

static void on_space_changed(GtkComboBox *combo, gpointer data)
{
    mv->space = (emudis_space_t)gtk_combo_box_get_active(combo);
    mv->have_prev = false;
    memset(mv->age, 0, sizeof mv->age);
    refresh_now();
}

static void on_mode_changed(GtkComboBox *combo, gpointer data)
{
    mv->mode = gtk_combo_box_get_active(combo);
    gtk_widget_set_sensitive(mv->scrollbar, mv->mode == MODE_HEX);
    gtk_widget_queue_draw(mv->area);
}

static void on_text_changed(GtkComboBox *combo, gpointer data)
{
    mv->text_mode = gtk_combo_box_get_active(combo);
    gtk_widget_queue_draw(mv->area);
}

static void on_rate_changed(GtkComboBox *combo, gpointer data)
{
    int i = gtk_combo_box_get_active(combo);
    mv->rate_ms = rates_ms[i < 0 ? 0 : i];
    restart_timer();
}

static void on_goto(GtkWidget *widget, gpointer data)
{
    long v;
    const char *t = gtk_entry_get_text(GTK_ENTRY(mv->addr_entry));

    if (emudis_parse_number(t, &v) && v >= 0 && v <= 0xffff) {
        goto_address((int)v);
    } else {
        gtk_label_set_markup(GTK_LABEL(mv->status), "<tt>Invalid address (use e.g. $C000 or 49152 with #)</tt>");
    }
}

static void on_preset(GtkComboBox *combo, gpointer data)
{
    int i = gtk_combo_box_get_active(combo);
    int addr = -1;

    switch (i) {
        case 1: addr = 0x0000; break;
        case 2: addr = 0x0100; break;
        case 3: addr = 0x0801; break;
        case 4: addr = mv->vic.screen; break;
        case 5: addr = mv->vic.charset; break;
        case 6: addr = mv->vic.bitmap; break;
        case 7: addr = mv->vic.sprite_addr[0]; break;
        case 8: addr = 0xc000; break;
        case 9: addr = 0xd000; break;
        case 10: addr = 0xd400; break;
        case 11: addr = 0xd800; break;
        case 12: addr = 0xe000; break;
        default: break;
    }
    if (addr >= 0) {
        goto_address(addr);
    }
    gtk_combo_box_set_active(combo, 0);
}

static void on_refresh(GtkWidget *widget, gpointer data)
{
    refresh_now();
}

static void on_highlight_toggled(GtkWidget *widget, gpointer data)
{
    gtk_widget_queue_draw(mv->area);
}

/* parse "A9 00 8D" or "\"HELLO\"" into a byte pattern */
static int parse_pattern(const char *text, uint8_t *pat, int max, bool *as_text)
{
    int n = 0;
    const char *p = text;

    *as_text = false;
    while (*p == ' ') {
        p++;
    }
    if (*p == '"' || *p == '\'') {
        char q = *p++;
        *as_text = true;
        while (*p && *p != q && n < max) {
            pat[n++] = (uint8_t)*p++;
        }
        return n;
    }
    while (*p && n < max) {
        char hx[3];
        if (*p == ' ' || *p == ',' || *p == '$') {
            p++;
            continue;
        }
        if (!g_ascii_isxdigit(p[0]) || !g_ascii_isxdigit(p[1])) {
            return -1;
        }
        hx[0] = p[0];
        hx[1] = p[1];
        hx[2] = '\0';
        pat[n++] = (uint8_t)strtol(hx, NULL, 16);
        p += 2;
    }
    return n;
}

static void on_find(GtkWidget *widget, gpointer data)
{
    uint8_t pat[64];
    uint8_t alt[64];
    bool as_text;
    int n = parse_pattern(gtk_entry_get_text(GTK_ENTRY(mv->find_entry)), pat, 64, &as_text);
    int start, i, k;

    if (n <= 0) {
        gtk_label_set_markup(GTK_LABEL(mv->status),
                             "<tt>Search for hex bytes (A9 00 8D) or \"TEXT\" (finds ASCII, PETSCII and screen codes)</tt>");
        return;
    }
    /* screen code variant for text searches */
    for (k = 0; k < n; k++) {
        uint8_t c = pat[k];
        if (c >= 'a' && c <= 'z') {
            c = (uint8_t)(c - 'a' + 'A');
            pat[k] = c;
        }
        alt[k] = (c >= 0x40 && c < 0x60) ? (uint8_t)(c - 0x40) : c;
    }

    start = (mv->selected >= 0 ? mv->selected + 1 : 0);
    for (i = 0; i < EMUDIS_MEM_SIZE; i++) {
        int a = (start + i) & 0xffff;
        bool m1 = true, m2 = as_text;
        for (k = 0; k < n; k++) {
            uint8_t b = mv->cur[(a + k) & 0xffff];
            if (b != pat[k]) {
                m1 = false;
            }
            if (as_text && b != alt[k]) {
                m2 = false;
            }
        }
        if (m1 || m2) {
            goto_address(a);
            {
                char buf[128];
                g_snprintf(buf, sizeof buf, "<tt>Found at $%04X%s</tt>", (unsigned int)a,
                           m2 && !m1 ? " (as screen codes)" : "");
                gtk_label_set_markup(GTK_LABEL(mv->status), buf);
            }
            return;
        }
    }
    gtk_label_set_markup(GTK_LABEL(mv->status), "<tt>Pattern not found.</tt>");
}

static bool get_range(long *from, long *to)
{
    if (!emudis_parse_number(gtk_entry_get_text(GTK_ENTRY(mv->from_entry)), from)
        || !emudis_parse_number(gtk_entry_get_text(GTK_ENTRY(mv->to_entry)), to)
        || *from < 0 || *to > 0xffff || *to < *from) {
        emudis_message(GTK_WINDOW(mv->window), true,
                       "Please enter a valid range, e.g. from $C000 to $CFFF.");
        return false;
    }
    return true;
}

static void save_range(bool prg)
{
    long from, to;
    gchar *fn;
    char suggested[64];

    if (!get_range(&from, &to)) {
        return;
    }
    g_snprintf(suggested, sizeof suggested, "mem_%04lx-%04lx.%s",
               (unsigned long)from, (unsigned long)to, prg ? "prg" : "bin");
    fn = emudis_choose_file(GTK_WINDOW(mv->window), prg ? "Save range as PRG" : "Save range as BIN",
                            true, suggested, prg ? "PRG files" : "Binary files",
                            prg ? "*.prg;*.PRG" : "*.bin;*.BIN");
    if (fn != NULL) {
        uint8_t *buf = lib_malloc((size_t)(to - from + 1));
        emudis_read_block(mv->space, (uint16_t)from, buf, (int)(to - from + 1));
        if (!emudis_save_data(fn, buf, (int)(to - from + 1), prg, (uint16_t)from)) {
            emudis_message(GTK_WINDOW(mv->window), true, "Could not write '%s'.", fn);
        }
        lib_free(buf);
        g_free(fn);
    }
}

static void on_save_prg(GtkWidget *w, gpointer d) { save_range(true); }
static void on_save_bin(GtkWidget *w, gpointer d) { save_range(false); }

static void on_load_file(GtkWidget *w, gpointer d)
{
    gchar *fn = emudis_choose_file(GTK_WINDOW(mv->window), "Load file into memory", false,
                                   NULL, "PRG / BIN files", "*.prg;*.PRG;*.bin;*.BIN");
    uint8_t *data;
    int len = 0;
    long addr = 0;
    int offset = 0;
    int i;

    if (fn == NULL) {
        return;
    }
    data = emudis_load_data(fn, &len);
    if (data == NULL) {
        emudis_message(GTK_WINDOW(mv->window), true, "Could not read '%s'.", fn);
        g_free(fn);
        return;
    }
    if (g_str_has_suffix(fn, ".prg") || g_str_has_suffix(fn, ".PRG")) {
        if (len < 2) {
            emudis_message(GTK_WINDOW(mv->window), true, "PRG file is too short.");
            lib_free(data);
            g_free(fn);
            return;
        }
        addr = data[0] | (data[1] << 8);
        offset = 2;
    } else if (!emudis_parse_number(gtk_entry_get_text(GTK_ENTRY(mv->from_entry)), &addr)
               || addr < 0 || addr > 0xffff) {
        emudis_message(GTK_WINDOW(mv->window), true,
                       "For BIN files enter the load address in the 'From' field.");
        lib_free(data);
        g_free(fn);
        return;
    }
    for (i = offset; i < len && addr + (i - offset) <= 0xffff; i++) {
        emudis_poke(EMUDIS_SPACE_RAM, (uint16_t)(addr + i - offset), data[i]);
    }
    {
        char buf[160];
        g_snprintf(buf, sizeof buf, "<tt>Loaded %d bytes to $%04lX-$%04lX (RAM)</tt>",
                   len - offset, (unsigned long)addr,
                   (unsigned long)MIN(0xffff, addr + len - offset - 1));
        refresh_now();
        goto_address((int)addr);
        gtk_label_set_markup(GTK_LABEL(mv->status), buf);
    }
    lib_free(data);
    g_free(fn);
}

static void on_destroy(GtkWidget *widget, gpointer data)
{
    if (mv != NULL) {
        if (mv->timer != 0) {
            g_source_remove(mv->timer);
        }
        if (mv->glyphs != NULL) {
            cairo_surface_destroy(mv->glyphs);
        }
        if (mv->font != NULL) {
            pango_font_description_free(mv->font);
        }
        lib_free(mv);
        mv = NULL;
    }
}


/* ------------------------------------------------------------------------ */

static GtkWidget *combo_from(const char **items, int n, int active)
{
    GtkWidget *c = gtk_combo_box_text_new();
    int i;
    for (i = 0; i < n; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), items[i]);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
    return c;
}

/** \brief  Show the live memory viewer (called from a locked action) */
void emudis_memview_show(void)
{
    GtkWidget *vbox, *bar1, *bar2, *hbox, *b, *preset;
    static const char *spaces[] = { "CPU view", "RAM", "ROM", "I/O" };
    static const char *modes[] = { "Hex dump", "Memory map (64KB)" };
    static const char *texts[] = { "PETSCII (upper)", "PETSCII (lower)",
                                   "Screen codes (upper)", "Screen codes (lower)", "ASCII" };
    static const char *presets[] = { "Jump to...", "Zero page $0000", "Stack $0100",
                                     "BASIC $0801", "Screen RAM (VIC)", "Charset (VIC)",
                                     "Bitmap (VIC)", "Sprite 0 data (VIC)", "$C000",
                                     "I/O $D000", "SID $D400", "Color RAM $D800",
                                     "KERNAL $E000" };

    if (mv != NULL) {
        gtk_window_present(GTK_WINDOW(mv->window));
        return;
    }

    mv = lib_calloc(1, sizeof *mv);
    mv->selected = -1;
    mv->hover = -1;
    mv->rate_ms = 100;
    mv->space = EMUDIS_SPACE_CPU;

    mv->window = emudis_window_new("Live Memory Viewer", 880, 640);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(mv->window), vbox);

    /* toolbar 1 */
    bar1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    mv->space_combo = combo_from(spaces, 4, 0);
    gtk_widget_set_tooltip_text(mv->space_combo, "Which memory to show: CPU view (with banking), plain RAM, ROMs or I/O");
    mv->mode_combo = combo_from(modes, 2, 0);
    mv->text_combo = combo_from(texts, 5, 0);
    mv->rate_combo = combo_from(rate_names, 6, 3);
    gtk_widget_set_tooltip_text(mv->rate_combo, "Live refresh rate while the C64 is running");
    mv->highlight = gtk_check_button_new_with_label("Highlight changes");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mv->highlight), TRUE);
    gtk_box_pack_start(GTK_BOX(bar1), mv->space_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar1), mv->mode_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar1), mv->text_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar1), gtk_label_new("Refresh:"), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(bar1), mv->rate_combo, FALSE, FALSE, 0);
    b = emudis_button_new("Refresh now", "Read memory once");
    g_signal_connect(b, "clicked", G_CALLBACK(on_refresh), NULL);
    gtk_box_pack_start(GTK_BOX(bar1), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar1), mv->highlight, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(vbox), bar1, FALSE, FALSE, 0);

    /* toolbar 2 */
    bar2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    mv->addr_entry = emudis_entry_new("$0400", 7, "Address: $hex, 0xhex, %binary or #decimal");
    b = emudis_button_new("Go", NULL);
    g_signal_connect(b, "clicked", G_CALLBACK(on_goto), NULL);
    g_signal_connect(mv->addr_entry, "activate", G_CALLBACK(on_goto), NULL);
    preset = combo_from(presets, (int)G_N_ELEMENTS(presets), 0);
    g_signal_connect_unlocked(preset, "changed", G_CALLBACK(on_preset), NULL);
    mv->find_entry = emudis_entry_new("", 14, "Hex bytes like A9 00 8D or \"TEXT\"");
    g_signal_connect(mv->find_entry, "activate", G_CALLBACK(on_find), NULL);
    gtk_box_pack_start(GTK_BOX(bar2), gtk_label_new("Address:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar2), mv->addr_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar2), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar2), preset, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar2), gtk_label_new("Find:"), FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(bar2), mv->find_entry, FALSE, FALSE, 0);
    b = emudis_button_new("Find next", NULL);
    g_signal_connect(b, "clicked", G_CALLBACK(on_find), NULL);
    gtk_box_pack_start(GTK_BOX(bar2), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bar2, FALSE, FALSE, 0);

    /* view */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    mv->area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(mv->area, TRUE);
    gtk_widget_add_events(mv->area, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK
                          | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK | GDK_KEY_PRESS_MASK);
    gtk_widget_set_hexpand(mv->area, TRUE);
    gtk_widget_set_vexpand(mv->area, TRUE);
    mv->adj = gtk_adjustment_new(0x0400 / 16, 0, ROWS_TOTAL, 1, 32, 32);
    mv->scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, mv->adj);
    gtk_box_pack_start(GTK_BOX(hbox), mv->area, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), mv->scrollbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

    /* range bar */
    bar2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    mv->from_entry = emudis_entry_new("$C000", 7, NULL);
    mv->to_entry = emudis_entry_new("$CFFF", 7, NULL);
    gtk_box_pack_start(GTK_BOX(bar2), gtk_label_new("Range from"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar2), mv->from_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar2), gtk_label_new("to"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar2), mv->to_entry, FALSE, FALSE, 0);
    b = emudis_button_new("Save .prg", "Save the range with a 2-byte load address");
    g_signal_connect(b, "clicked", G_CALLBACK(on_save_prg), NULL);
    gtk_box_pack_start(GTK_BOX(bar2), b, FALSE, FALSE, 0);
    b = emudis_button_new("Save .bin", "Save the raw bytes of the range");
    g_signal_connect(b, "clicked", G_CALLBACK(on_save_bin), NULL);
    gtk_box_pack_start(GTK_BOX(bar2), b, FALSE, FALSE, 0);
    b = emudis_button_new("Load file...", "Load a PRG (uses its load address) or BIN (at 'Range from') into RAM");
    g_signal_connect(b, "clicked", G_CALLBACK(on_load_file), NULL);
    gtk_box_pack_start(GTK_BOX(bar2), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bar2, FALSE, FALSE, 0);

    mv->status = emudis_label_new("", true);
    mv->vicinfo = emudis_label_new("", true);
    gtk_label_set_selectable(GTK_LABEL(mv->status), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), mv->status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), mv->vicinfo, FALSE, FALSE, 0);

    g_signal_connect_unlocked(mv->area, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect_unlocked(mv->area, "size-allocate", G_CALLBACK(on_size_allocate), NULL);
    g_signal_connect_unlocked(mv->area, "motion-notify-event", G_CALLBACK(on_motion), NULL);
    g_signal_connect_unlocked(mv->area, "scroll-event", G_CALLBACK(on_scroll), NULL);
    g_signal_connect_unlocked(mv->adj, "value-changed", G_CALLBACK(on_adj_changed), NULL);
    g_signal_connect(mv->area, "button-press-event", G_CALLBACK(on_button_press), NULL);
    g_signal_connect(mv->area, "key-press-event", G_CALLBACK(on_key_press), NULL);
    g_signal_connect(mv->space_combo, "changed", G_CALLBACK(on_space_changed), NULL);
    g_signal_connect_unlocked(mv->mode_combo, "changed", G_CALLBACK(on_mode_changed), NULL);
    g_signal_connect_unlocked(mv->text_combo, "changed", G_CALLBACK(on_text_changed), NULL);
    g_signal_connect_unlocked(mv->rate_combo, "changed", G_CALLBACK(on_rate_changed), NULL);
    g_signal_connect_unlocked(mv->highlight, "toggled", G_CALLBACK(on_highlight_toggled), NULL);
    g_signal_connect_unlocked(mv->window, "destroy", G_CALLBACK(on_destroy), NULL);

    /* initial read (we are called with the lock held) */
    read_memory();
    update_status();
    restart_timer();

    gtk_widget_show_all(mv->window);
}
