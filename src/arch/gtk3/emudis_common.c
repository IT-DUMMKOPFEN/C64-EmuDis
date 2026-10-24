/** \file   emudis_common.c
 * \brief   C64-EmuDis tools - shared helpers
 *
 * Memory access, VIC-II state, C64 colours, file dialogs and drawing helpers
 * shared by all C64-EmuDis tool windows.
 *
 * Threading: VICE runs the emulation in its own thread. All functions that
 * touch emulator state (emudis_peek(), emudis_vic_state_get(), ...) must be
 * called while holding the VICE mainlock. GTK signal handlers connected via
 * g_signal_connect() hold the lock automatically; timers must call
 * mainlock_obtain()/mainlock_release() themselves.
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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "archdep.h"
#include "interrupt.h"
#include "lib.h"
#include "mainlock.h"
#include "maincpu.h"
#include "mem.h"
#include "monitor.h"
#include "mos6510.h"
#include "ui.h"
#include "util.h"
#include "vice_gtk3.h"

#include "emudis.h"


/* ------------------------------------------------------------------------ */
/* Memory access                                                            */
/* ------------------------------------------------------------------------ */

static const char *space_names[EMUDIS_SPACE_COUNT] = {
    "CPU view", "RAM", "ROM", "I/O"
};

static const char *space_bank_names[EMUDIS_SPACE_COUNT] = {
    "cpu", "ram", "rom", "io"
};

const char *emudis_space_name(emudis_space_t space)
{
    if (space < 0 || space >= EMUDIS_SPACE_COUNT) {
        return "?";
    }
    return space_names[space];
}

int emudis_space_bank(emudis_space_t space)
{
    int bank;

    if (space < 0 || space >= EMUDIS_SPACE_COUNT) {
        space = EMUDIS_SPACE_CPU;
    }
    bank = mem_bank_from_name(space_bank_names[space]);
    if (bank < 0) {
        bank = 0;
    }
    return bank;
}

/* lock must be held */
uint8_t emudis_peek(emudis_space_t space, uint16_t addr)
{
    return mem_bank_peek(emudis_space_bank(space), addr, NULL);
}

/* lock must be held */
void emudis_poke(emudis_space_t space, uint16_t addr, uint8_t value)
{
    mem_bank_write(emudis_space_bank(space), addr, value, NULL);
}

/* lock must be held */
void emudis_read_block(emudis_space_t space, uint16_t start, uint8_t *dest, int len)
{
    int bank = emudis_space_bank(space);
    int i;

    for (i = 0; i < len; i++) {
        dest[i] = mem_bank_peek(bank, (uint16_t)(start + i), NULL);
    }
}

/* lock must be held */
void emudis_read_all(emudis_space_t space, uint8_t *dest)
{
    emudis_read_block(space, 0, dest, EMUDIS_MEM_SIZE);
}

/** \brief  Read the 4KB character ROM (upper/graphics set + lower/upper set)
 *
 * lock must be held
 */
void emudis_read_charrom(uint8_t *dest)
{
    emudis_read_block(EMUDIS_SPACE_ROM, 0xd000, dest, 0x1000);
}


/* ------------------------------------------------------------------------ */
/* VIC-II state                                                             */
/* ------------------------------------------------------------------------ */

/* lock must be held */
void emudis_vic_state_get(emudis_vic_state_t *vic)
{
    int i;
    uint8_t d010;

    memset(vic, 0, sizeof *vic);

    vic->dd00 = emudis_peek(EMUDIS_SPACE_IO, 0xdd00);
    vic->bank = 3 - (vic->dd00 & 3);
    vic->bank_base = (uint16_t)(vic->bank * 0x4000);

    vic->d011 = emudis_peek(EMUDIS_SPACE_IO, 0xd011);
    vic->d016 = emudis_peek(EMUDIS_SPACE_IO, 0xd016);
    vic->d018 = emudis_peek(EMUDIS_SPACE_IO, 0xd018);

    vic->screen  = (uint16_t)(vic->bank_base + ((vic->d018 >> 4) & 0x0f) * 0x0400);
    vic->charset = (uint16_t)(vic->bank_base + ((vic->d018 >> 1) & 0x07) * 0x0800);
    vic->bitmap  = (uint16_t)(vic->bank_base + ((vic->d018 >> 3) & 0x01) * 0x2000);

    /* in VIC banks 0 and 2 the char ROM is visible at $1000-$1FFF */
    vic->charset_is_rom = ((vic->bank == 0 || vic->bank == 2)
                           && (((vic->d018 >> 1) & 0x07) == 2
                               || ((vic->d018 >> 1) & 0x07) == 3));

    vic->bitmap_mode = (vic->d011 & 0x20) != 0;
    vic->ecm         = (vic->d011 & 0x40) != 0;
    vic->multicolor  = (vic->d016 & 0x10) != 0;

    vic->border = emudis_peek(EMUDIS_SPACE_IO, 0xd020) & 0x0f;
    for (i = 0; i < 4; i++) {
        vic->bg[i] = emudis_peek(EMUDIS_SPACE_IO, (uint16_t)(0xd021 + i)) & 0x0f;
    }

    vic->sprite_enable = emudis_peek(EMUDIS_SPACE_IO, 0xd015);
    vic->sprite_yexp   = emudis_peek(EMUDIS_SPACE_IO, 0xd017);
    vic->sprite_mc     = emudis_peek(EMUDIS_SPACE_IO, 0xd01c);
    vic->sprite_xexp   = emudis_peek(EMUDIS_SPACE_IO, 0xd01d);
    vic->sprite_mc0    = emudis_peek(EMUDIS_SPACE_IO, 0xd025) & 0x0f;
    vic->sprite_mc1    = emudis_peek(EMUDIS_SPACE_IO, 0xd026) & 0x0f;
    d010 = emudis_peek(EMUDIS_SPACE_IO, 0xd010);

    for (i = 0; i < 8; i++) {
        vic->sprite_color[i] = emudis_peek(EMUDIS_SPACE_IO, (uint16_t)(0xd027 + i)) & 0x0f;
        vic->sprite_x[i] = (uint16_t)(emudis_peek(EMUDIS_SPACE_IO, (uint16_t)(0xd000 + i * 2))
                                      | (((d010 >> i) & 1) << 8));
        vic->sprite_y[i] = emudis_peek(EMUDIS_SPACE_IO, (uint16_t)(0xd001 + i * 2));
        vic->sprite_ptr[i] = emudis_peek(EMUDIS_SPACE_RAM, (uint16_t)(vic->screen + 0x3f8 + i));
        vic->sprite_addr[i] = (uint16_t)(vic->bank_base + vic->sprite_ptr[i] * 64);
    }
}


/* ------------------------------------------------------------------------ */
/* CPU registers (fetched inside a CPU trap so they are exact)              */
/* ------------------------------------------------------------------------ */

static GMutex cpu_mutex;
static emudis_cpu_state_t cpu_state;
static volatile gint cpu_trap_pending = 0;

static void cpu_state_trap(uint16_t addr, void *data)
{
    monitor_interface_t *mi = maincpu_monitor_interface_get();
    emudis_cpu_state_t st;

    memset(&st, 0, sizeof st);
    if (mi != NULL && mi->cpu_regs != NULL) {
        mos6510_regs_t *r = mi->cpu_regs;
        st.valid = true;
        st.pc = (uint16_t)MOS6510_REGS_GET_PC(r);
        st.a  = (uint8_t)MOS6510_REGS_GET_A(r);
        st.x  = (uint8_t)MOS6510_REGS_GET_X(r);
        st.y  = (uint8_t)MOS6510_REGS_GET_Y(r);
        st.sp = (uint8_t)MOS6510_REGS_GET_SP(r);
        st.p  = (uint8_t)MOS6510_REGS_GET_STATUS(r);
    }
    g_mutex_lock(&cpu_mutex);
    cpu_state = st;
    g_mutex_unlock(&cpu_mutex);
    g_atomic_int_set(&cpu_trap_pending, 0);
}

/** \brief  Ask the emulation thread for a fresh copy of the CPU registers
 *
 * lock must be held. The result arrives asynchronously, read it with
 * emudis_cpu_state_get().
 */
void emudis_cpu_state_request(void)
{
    if (g_atomic_int_compare_and_exchange(&cpu_trap_pending, 0, 1)) {
        interrupt_maincpu_trigger_trap(cpu_state_trap, NULL);
    }
}

void emudis_cpu_state_get(emudis_cpu_state_t *cpu)
{
    g_mutex_lock(&cpu_mutex);
    *cpu = cpu_state;
    g_mutex_unlock(&cpu_mutex);
}


/* ------------------------------------------------------------------------ */
/* Colours                                                                  */
/* ------------------------------------------------------------------------ */

/* "Colodore" palette by pepto */
static const uint32_t c64_palette[16] = {
    0x000000, 0xffffff, 0x813338, 0x75cec8, 0x8e3c97, 0x56ac4d, 0x2e2c9b, 0xedf171,
    0x8e5029, 0x553800, 0xc46c71, 0x4a4a4a, 0x7b7b7b, 0xa9ff9f, 0x706deb, 0xb2b2b2
};

static const char *c64_color_names[16] = {
    "Black", "White", "Red", "Cyan", "Purple", "Green", "Blue", "Yellow",
    "Orange", "Brown", "Light red", "Dark grey", "Grey", "Light green",
    "Light blue", "Light grey"
};

uint32_t emudis_c64_rgb(int color)
{
    return c64_palette[color & 0x0f];
}

void emudis_set_cairo_c64_color(cairo_t *cr, int color)
{
    uint32_t rgb = emudis_c64_rgb(color);
    cairo_set_source_rgb(cr,
                         ((rgb >> 16) & 0xff) / 255.0,
                         ((rgb >> 8) & 0xff) / 255.0,
                         (rgb & 0xff) / 255.0);
}

GtkWidget *emudis_c64_color_combo(int initial)
{
    GtkWidget *combo = gtk_combo_box_text_new();
    int i;

    for (i = 0; i < 16; i++) {
        char buf[32];
        g_snprintf(buf, sizeof buf, "%2d %s", i, c64_color_names[i]);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), buf);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), initial & 0x0f);
    return combo;
}


/* ------------------------------------------------------------------------ */
/* Parsing / formatting                                                     */
/* ------------------------------------------------------------------------ */

/** \brief  Parse "$c000", "0xc000", "%1010", "49152" or "c000" (hex default) */
bool emudis_parse_number(const char *text, long *value)
{
    char *end = NULL;
    const char *s = text;
    int base = 16;

    if (s == NULL) {
        return false;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == '$') {
        s++;
    } else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    } else if (*s == '%') {
        s++;
        base = 2;
    } else if (*s == '#') {
        s++;
        base = 10;
    } else if (*s == '+') {
        s++;
        base = 10;
    }
    if (*s == '\0') {
        return false;
    }
    *value = strtol(s, &end, base);
    while (end != NULL && (*end == ' ' || *end == '\t')) {
        end++;
    }
    return end != NULL && *end == '\0';
}

char *emudis_format_hex16(uint16_t v)
{
    return lib_msprintf("$%04X", (unsigned int)v);
}


/* ------------------------------------------------------------------------ */
/* Widgets                                                                  */
/* ------------------------------------------------------------------------ */

/** \brief  Create a C64-EmuDis tool window (non-modal, own taskbar entry) */
GtkWidget *emudis_window_new(const char *title, int width, int height)
{
    GtkWidget *window;
    GtkWindow *main_window;
    char *full = lib_msprintf("%s - %s", title, EMUDIS_TITLE);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), full);
    gtk_window_set_default_size(GTK_WINDOW(window), width, height);
    main_window = ui_get_active_window();
    if (main_window != NULL) {
        GdkPixbuf *icon = gtk_window_get_icon(main_window);
        if (icon != NULL) {
            gtk_window_set_icon(GTK_WINDOW(window), icon);
        }
    }
    gtk_container_set_border_width(GTK_CONTAINER(window), 6);
    lib_free(full);
    return window;
}

GtkWidget *emudis_label_new(const char *markup, bool left)
{
    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    if (left) {
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    }
    return label;
}

GtkWidget *emudis_button_new(const char *label, const char *tooltip)
{
    GtkWidget *button = gtk_button_new_with_label(label);
    if (tooltip != NULL) {
        gtk_widget_set_tooltip_text(button, tooltip);
    }
    return button;
}

GtkWidget *emudis_entry_new(const char *text, int chars, const char *tooltip)
{
    GtkWidget *entry = gtk_entry_new();
    if (text != NULL) {
        gtk_entry_set_text(GTK_ENTRY(entry), text);
    }
    gtk_entry_set_width_chars(GTK_ENTRY(entry), chars);
    if (tooltip != NULL) {
        gtk_widget_set_tooltip_text(entry, tooltip);
    }
    return entry;
}


/* ------------------------------------------------------------------------ */
/* Dialogs                                                                  */
/* ------------------------------------------------------------------------ */

/** \brief  Show an open/save file chooser
 *
 * Must be called from a locked signal handler: the VICE mainlock is released
 * while the dialog runs so the emulation keeps going.
 *
 * \return  filename (free with g_free()) or NULL
 */
gchar *emudis_choose_file(GtkWindow *parent, const char *title, bool save,
                          const char *suggested, const char *filter_name,
                          const char *pattern)
{
    GtkWidget *dialog;
    gchar *result = NULL;
    gint response;

    dialog = gtk_file_chooser_dialog_new(title, parent,
                                         save ? GTK_FILE_CHOOSER_ACTION_SAVE
                                              : GTK_FILE_CHOOSER_ACTION_OPEN,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         save ? "_Save" : "_Open",
                                         GTK_RESPONSE_ACCEPT,
                                         NULL);
    if (save) {
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
        if (suggested != NULL) {
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), suggested);
        }
    }
    if (filter_name != NULL && pattern != NULL) {
        GtkFileFilter *filter = gtk_file_filter_new();
        GtkFileFilter *all = gtk_file_filter_new();
        gchar **pats = g_strsplit(pattern, ";", -1);
        int i;

        gtk_file_filter_set_name(filter, filter_name);
        for (i = 0; pats[i] != NULL; i++) {
            gtk_file_filter_add_pattern(filter, pats[i]);
        }
        g_strfreev(pats);
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
        gtk_file_filter_set_name(all, "All files");
        gtk_file_filter_add_pattern(all, "*");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all);
    }

    {
        /* let the emulation run while the dialog is open */
        bool held = (mainlock_get_depth() == 1);
        if (held) {
            mainlock_release();
        }
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response == GTK_RESPONSE_ACCEPT) {
            result = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        }
        gtk_widget_destroy(dialog);
        if (held) {
            mainlock_obtain();
        }
    }
    return result;
}

void emudis_message(GtkWindow *parent, bool error, const char *fmt, ...)
{
    GtkWidget *dialog;
    va_list ap;
    char *text;

    va_start(ap, fmt);
    text = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    dialog = gtk_message_dialog_new(parent, GTK_DIALOG_DESTROY_WITH_PARENT,
                                    error ? GTK_MESSAGE_ERROR : GTK_MESSAGE_INFO,
                                    GTK_BUTTONS_OK, "%s", text);
    gtk_window_set_title(GTK_WINDOW(dialog), EMUDIS_NAME);
    g_signal_connect_swapped_unlocked(dialog, "response",
                                      G_CALLBACK(gtk_widget_destroy), dialog);
    gtk_widget_show_all(dialog);
    g_free(text);
}


/* ------------------------------------------------------------------------ */
/* Files                                                                    */
/* ------------------------------------------------------------------------ */

bool emudis_save_data(const char *path, const uint8_t *data, int len,
                      bool with_load_addr, uint16_t load_addr)
{
    FILE *fp = fopen(path, "wb");
    bool ok = true;

    if (fp == NULL) {
        return false;
    }
    if (with_load_addr) {
        if (fputc(load_addr & 0xff, fp) == EOF || fputc(load_addr >> 8, fp) == EOF) {
            ok = false;
        }
    }
    if (ok && len > 0 && fwrite(data, 1, (size_t)len, fp) != (size_t)len) {
        ok = false;
    }
    if (fclose(fp) != 0) {
        ok = false;
    }
    return ok;
}

uint8_t *emudis_load_data(const char *path, int *len)
{
    gchar *contents = NULL;
    gsize size = 0;
    uint8_t *data;

    if (!g_file_get_contents(path, &contents, &size, NULL)) {
        return NULL;
    }
    data = lib_malloc(size + 1);
    memcpy(data, contents, size);
    data[size] = 0;
    g_free(contents);
    *len = (int)size;
    return data;
}

/** \brief  Get (and create) a C64-EmuDis data directory below the user dir
 *
 * \return  static path string, valid until the next call
 */
const char *emudis_user_dir(const char *sub)
{
    static char *path = NULL;

    if (path != NULL) {
        lib_free(path);
    }
    path = util_join_paths(archdep_user_state_path(), sub, NULL);
    g_mkdir_with_parents(path, 0755);
    return path;
}


/* ------------------------------------------------------------------------ */
/* Drawing                                                                  */
/* ------------------------------------------------------------------------ */

cairo_surface_t *emudis_surface_new(int w, int h)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t *cr = cairo_create(s);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    return s;
}

void emudis_surface_pixel(cairo_surface_t *s, int x, int y, uint32_t rgb)
{
    int w = cairo_image_surface_get_width(s);
    int h = cairo_image_surface_get_height(s);
    int stride = cairo_image_surface_get_stride(s);
    unsigned char *data = cairo_image_surface_get_data(s);

    if (x < 0 || y < 0 || x >= w || y >= h) {
        return;
    }
    *(uint32_t *)(data + y * stride + x * 4) = rgb;
}

/** \brief  Draw an 8x8 character (hires or multicolor) into a surface
 *
 * Call cairo_surface_flush() before and cairo_surface_mark_dirty() after a
 * batch of drawing operations.
 */
void emudis_draw_char(cairo_surface_t *s, int px0, int py0,
                      const uint8_t *data, bool multicolor,
                      int fg, int bg, int mc1, int mc2)
{
    int row, col;

    for (row = 0; row < 8; row++) {
        uint8_t b = data[row];
        if (!multicolor) {
            for (col = 0; col < 8; col++) {
                int set = (b >> (7 - col)) & 1;
                emudis_surface_pixel(s, px0 + col, py0 + row,
                                     emudis_c64_rgb(set ? fg : bg));
            }
        } else {
            for (col = 0; col < 4; col++) {
                int bits = (b >> (6 - col * 2)) & 3;
                int c = bits == 0 ? bg : bits == 1 ? mc1 : bits == 2 ? mc2 : fg;
                emudis_surface_pixel(s, px0 + col * 2, py0 + row, emudis_c64_rgb(c));
                emudis_surface_pixel(s, px0 + col * 2 + 1, py0 + row, emudis_c64_rgb(c));
            }
        }
    }
}

/** \brief  Draw a 24x21 sprite (hires or multicolor) into a surface */
void emudis_draw_sprite(cairo_surface_t *s, int px0, int py0,
                        const uint8_t *data, bool multicolor,
                        int fg, int bg, int mc1, int mc2)
{
    int row, col;

    for (row = 0; row < 21; row++) {
        for (col = 0; col < 3; col++) {
            uint8_t b = data[row * 3 + col];
            int px;
            if (!multicolor) {
                for (px = 0; px < 8; px++) {
                    int set = (b >> (7 - px)) & 1;
                    emudis_surface_pixel(s, px0 + col * 8 + px, py0 + row,
                                         emudis_c64_rgb(set ? fg : bg));
                }
            } else {
                for (px = 0; px < 4; px++) {
                    int bits = (b >> (6 - px * 2)) & 3;
                    /* sprite MC: 01 = $D025, 10 = sprite colour, 11 = $D026 */
                    int c = bits == 0 ? bg : bits == 1 ? mc1 : bits == 2 ? fg : mc2;
                    emudis_surface_pixel(s, px0 + col * 8 + px * 2, py0 + row, emudis_c64_rgb(c));
                    emudis_surface_pixel(s, px0 + col * 8 + px * 2 + 1, py0 + row, emudis_c64_rgb(c));
                }
            }
        }
    }
}

/** \brief  Save a surface as PNG, scaled up with nearest-neighbour */
bool emudis_save_surface_png(cairo_surface_t *s, const char *path, int scale)
{
    cairo_surface_t *out;
    cairo_t *cr;
    cairo_status_t st;
    int w = cairo_image_surface_get_width(s);
    int h = cairo_image_surface_get_height(s);

    if (scale < 1) {
        scale = 1;
    }
    out = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w * scale, h * scale);
    cr = cairo_create(out);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_destroy(cr);
    st = cairo_surface_write_to_png(out, path);
    cairo_surface_destroy(out);
    return st == CAIRO_STATUS_SUCCESS;
}

/** \brief  Convert a PETSCII code to a screen code (for char ROM lookup) */
uint8_t emudis_petscii_to_screencode(uint8_t c)
{
    if (c < 0x20) {
        return (uint8_t)(c + 0x80);
    } else if (c < 0x40) {
        return c;
    } else if (c < 0x60) {
        return (uint8_t)(c - 0x40);
    } else if (c < 0x80) {
        return (uint8_t)(c - 0x20);
    } else if (c < 0xa0) {
        return (uint8_t)(c + 0x40);
    } else if (c < 0xc0) {
        return (uint8_t)(c - 0x40);
    } else if (c < 0xff) {
        return (uint8_t)(c - 0x80);
    }
    return 0x5e;
}


/* ------------------------------------------------------------------------ */
/* Heuristics shared by the finders                                         */
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

static bool char_pixel(const uint8_t *ch, int x, int y)
{
    if (x < 0 || y < 0 || x > 7 || y > 7) {
        return false;
    }
    return (ch[y] >> (7 - x)) & 1;
}

/** \brief  Rate how much a 2KB block looks like a character set, 0..100
 *
 * Looks at the first 64 characters (letters, digits, punctuation):
 *  - distinct, non-empty characters
 *  - small bit difference between neighbouring rows (glyphs are drawn with
 *    vertical strokes; random data/code changes ~4 of 8 bits per row)
 *  - few isolated pixels, plausible density
 *  - screen code 32 (space) is empty
 */
int emudis_score_font(const uint8_t *f)
{
    int nonblank = 0, distinct = 0, pixels = 0, isolated = 0, rowdiff = 0, rows = 0;
    int c, d, x, y;
    double s = 0.0;
    double density, avgdiff;

    for (c = 0; c < 64; c++) {
        const uint8_t *ch = f + c * 8;
        bool blank = true;
        bool dup = false;

        for (y = 0; y < 8; y++) {
            if (ch[y] != 0) {
                blank = false;
            }
            pixels += popcount8(ch[y]);
        }
        if (blank) {
            continue;
        }
        nonblank++;
        for (y = 1; y < 8; y++) {
            rowdiff += popcount8((uint8_t)(ch[y] ^ ch[y - 1]));
            rows++;
        }
        for (d = 0; d < c && !dup; d++) {
            if (memcmp(ch, f + d * 8, 8) == 0) {
                dup = true;
            }
        }
        if (!dup) {
            distinct++;
        }
        for (y = 0; y < 8; y++) {
            for (x = 0; x < 8; x++) {
                if (char_pixel(ch, x, y)
                    && !char_pixel(ch, x - 1, y) && !char_pixel(ch, x + 1, y)
                    && !char_pixel(ch, x, y - 1) && !char_pixel(ch, x, y + 1)
                    && !char_pixel(ch, x - 1, y - 1) && !char_pixel(ch, x + 1, y + 1)
                    && !char_pixel(ch, x + 1, y - 1) && !char_pixel(ch, x - 1, y + 1)) {
                    isolated++;
                }
            }
        }
    }

    if (nonblank < 32 || pixels == 0 || rows == 0) {
        return 0;
    }
    if (distinct < 16) {
        return 5;   /* fill pattern */
    }

    s += 25.0 * MIN(1.0, MAX(0.0, (distinct / (double)nonblank - 0.5) / 0.45));
    s += 15.0 * (1.0 - MIN(1.0, (isolated / (double)pixels) / 0.12));
    density = pixels / (64.0 * 64.0);
    if (density > 0.10 && density < 0.55) {
        s += 10.0;
    } else if (density > 0.05 && density < 0.7) {
        s += 4.0;
    }
    avgdiff = rowdiff / (double)rows;
    s += 35.0 * (1.0 - MIN(1.0, MAX(0.0, (avgdiff - 1.0) / 2.5)));
    if (memcmp(f + 32 * 8, "\0\0\0\0\0\0\0\0", 8) == 0) {
        s += 15.0;
    }
    return (int)(s + 0.5);
}

static bool spr_pixel(const uint8_t *d, int x, int y)
{
    if (x < 0 || y < 0 || x >= 24 || y >= 21) {
        return false;
    }
    return (d[y * 3 + x / 8] >> (7 - (x & 7))) & 1;
}

/** \brief  Rate how much a 64 byte block looks like a sprite, 0..100
 *
 *  - smooth shapes: few horizontal and vertical pixel transitions
 *  - few isolated pixels
 *  - neighbouring rows are similar
 *  - not a fill pattern (only $00/$FF bytes), not text ($20 bytes)
 */
int emudis_score_sprite(const uint8_t *d)
{
    int x, y, i;
    int pixels = 0, isolated = 0, htrans = 0, vtrans = 0, similar = 0;
    int nonzero = 0, partial = 0, spaces = 0;
    double s = 0.0;
    double density, trans;

    for (i = 0; i < 63; i++) {
        if (d[i] != 0) {
            nonzero++;
        }
        if (d[i] != 0 && d[i] != 0xff) {
            partial++;
        }
        if (d[i] == 0x20) {
            spaces++;
        }
    }
    if (nonzero < 5 || partial < 6) {
        return 0;
    }
    for (y = 0; y < 21; y++) {
        if (y > 0) {
            int diff = popcount8((uint8_t)(d[y * 3] ^ d[y * 3 - 3]))
                     + popcount8((uint8_t)(d[y * 3 + 1] ^ d[y * 3 - 2]))
                     + popcount8((uint8_t)(d[y * 3 + 2] ^ d[y * 3 - 1]));
            vtrans += diff;
            if (diff <= 3) {
                similar++;
            }
        }
        for (x = 0; x < 24; x++) {
            bool p = spr_pixel(d, x, y);
            int n;
            if (x > 0 && p != spr_pixel(d, x - 1, y)) {
                htrans++;
            }
            if (!p) {
                continue;
            }
            pixels++;
            n = spr_pixel(d, x - 1, y) + spr_pixel(d, x + 1, y) + spr_pixel(d, x, y - 1)
                + spr_pixel(d, x, y + 1) + spr_pixel(d, x - 1, y - 1) + spr_pixel(d, x + 1, y + 1)
                + spr_pixel(d, x + 1, y - 1) + spr_pixel(d, x - 1, y + 1);
            if (n == 0) {
                isolated++;
            }
        }
    }
    if (pixels < 12) {
        return 0;
    }
    trans = (htrans / (23.0 * 21.0) + vtrans / (24.0 * 20.0)) / 2.0;
    s += 50.0 * (1.0 - MIN(1.0, MAX(0.0, (trans - 0.08) / 0.22)));
    s += 15.0 * (1.0 - MIN(1.0, (isolated / (double)pixels) / 0.10));
    s += 25.0 * (similar / 20.0);
    density = pixels / (24.0 * 21.0);
    if (density > 0.06 && density < 0.8) {
        s += 10.0;
    }
    if (spaces > 16) {
        s *= 0.4;   /* probably screen memory */
    }
    return (int)(s + 0.5);
}
