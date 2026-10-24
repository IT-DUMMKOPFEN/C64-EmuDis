/** \file   emudis.h
 * \brief   C64-EmuDis tools - public interface
 *
 * C64-EmuDis adds memory analysis and developer tools on top of the VICE
 * GTK3 user interface: live memory viewer, font/sprite/SID finders,
 * assembler, disassembler, snapshot manager, video recorder and the
 * help/history/about pages.
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

#ifndef VICE_EMUDIS_H
#define VICE_EMUDIS_H

#include "vice.h"
#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdint.h>

#include "emudis_version.h"

#define EMUDIS_MEM_SIZE 0x10000

/** \brief  Memory spaces the tools can look at */
typedef enum emudis_space_e {
    EMUDIS_SPACE_CPU = 0,   /**< what the CPU currently sees (banking applied) */
    EMUDIS_SPACE_RAM,       /**< plain 64KB RAM */
    EMUDIS_SPACE_ROM,       /**< ROM view */
    EMUDIS_SPACE_IO,        /**< I/O view */
    EMUDIS_SPACE_COUNT
} emudis_space_t;

/** \brief  Snapshot of the VIC-II state relevant for graphics analysis */
typedef struct emudis_vic_state_s {
    int      bank;              /**< VIC bank 0-3 */
    uint16_t bank_base;         /**< $0000/$4000/$8000/$C000 */
    uint16_t screen;            /**< absolute screen RAM address */
    uint16_t charset;           /**< absolute charset address */
    uint16_t bitmap;            /**< absolute bitmap address */
    bool     charset_is_rom;    /**< charset points to the character ROM */
    bool     bitmap_mode;
    bool     multicolor;
    bool     ecm;
    uint8_t  d011, d016, d018, dd00;
    uint8_t  border, bg[4];
    uint8_t  sprite_enable;
    uint8_t  sprite_mc;
    uint8_t  sprite_xexp, sprite_yexp;
    uint8_t  sprite_color[8];
    uint8_t  sprite_mc0, sprite_mc1;
    uint16_t sprite_x[8];
    uint8_t  sprite_y[8];
    uint8_t  sprite_ptr[8];     /**< pointer values */
    uint16_t sprite_addr[8];    /**< absolute address of sprite data */
} emudis_vic_state_t;

/** \brief  CPU register snapshot */
typedef struct emudis_cpu_state_s {
    bool     valid;
    uint16_t pc;
    uint8_t  a, x, y, sp, p;
} emudis_cpu_state_t;

/* ---- emudis_common.c ---- */
const char *emudis_space_name(emudis_space_t space);
int         emudis_space_bank(emudis_space_t space);
uint8_t     emudis_peek(emudis_space_t space, uint16_t addr);
void        emudis_poke(emudis_space_t space, uint16_t addr, uint8_t value);
void        emudis_read_block(emudis_space_t space, uint16_t start, uint8_t *dest, int len);
void        emudis_read_all(emudis_space_t space, uint8_t *dest);
void        emudis_read_charrom(uint8_t *dest);
void        emudis_vic_state_get(emudis_vic_state_t *vic);
void        emudis_cpu_state_request(void);
void        emudis_cpu_state_get(emudis_cpu_state_t *cpu);
uint32_t    emudis_c64_rgb(int color);
void        emudis_set_cairo_c64_color(cairo_t *cr, int color);
bool        emudis_parse_number(const char *text, long *value);
char       *emudis_format_hex16(uint16_t v);
GtkWidget  *emudis_window_new(const char *title, int width, int height);
GtkWidget  *emudis_c64_color_combo(int initial);
GtkWidget  *emudis_label_new(const char *markup, bool left);
GtkWidget  *emudis_button_new(const char *label, const char *tooltip);
GtkWidget  *emudis_entry_new(const char *text, int chars, const char *tooltip);
gchar      *emudis_choose_file(GtkWindow *parent, const char *title,
                               bool save, const char *suggested,
                               const char *filter_name, const char *pattern);
bool        emudis_save_data(const char *path, const uint8_t *data, int len,
                             bool with_load_addr, uint16_t load_addr);
uint8_t    *emudis_load_data(const char *path, int *len);
void        emudis_message(GtkWindow *parent, bool error, const char *fmt, ...);
cairo_surface_t *emudis_surface_new(int w, int h);
void        emudis_surface_pixel(cairo_surface_t *s, int x, int y, uint32_t rgb);
void        emudis_draw_char(cairo_surface_t *s, int px0, int py0,
                             const uint8_t *data, bool multicolor,
                             int fg, int bg, int mc1, int mc2);
void        emudis_draw_sprite(cairo_surface_t *s, int px0, int py0,
                               const uint8_t *data, bool multicolor,
                               int fg, int bg, int mc1, int mc2);
bool        emudis_save_surface_png(cairo_surface_t *s, const char *path, int scale);
uint8_t     emudis_petscii_to_screencode(uint8_t c);
const char *emudis_user_dir(const char *sub);
int         emudis_score_font(const uint8_t *f);
int         emudis_score_sprite(const uint8_t *d);

/* ---- windows ---- */
void emudis_memview_show(void);
void emudis_fontsearch_show(void);
void emudis_spritesearch_show(void);
void emudis_sidsearch_show(void);
void emudis_assembler_show(void);
void emudis_assembler_load_text(const char *text);
void emudis_disassembler_show(void);
void emudis_snapshots_show(void);
void emudis_video_show(void);
void emudis_help_show(void);
void emudis_history_show(void);
void emudis_about_show(void);

/* ---- misc ---- */
bool emudis_fastload_get(void);
void emudis_fastload_set(bool enabled);
void emudis_video_quick_toggle(void);

#endif
