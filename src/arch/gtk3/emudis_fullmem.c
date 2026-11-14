/**
 * \file emudis_fullmem.c
 * \brief   C64-EmuDis: Full memory view
 *
 * Shows all 64KB of the C64 as 16 blocks of $1000 bytes (one pixel per byte)
 * with the name of each memory area, live changes as a heat map, a speed
 * control from 1% to 100%, pause and single frame steps.
 *
 * A block can be zoomed (64 x 64 bytes) and lists the addresses that changed.
 * The trainer search narrows down candidate addresses (e.g. the number of
 * lives): mark a baseline, play, then keep only the addresses that decreased,
 * increased, changed or stayed the same. Addresses can be written or frozen
 * (written every refresh), and the matching POKE is shown.
 *
 * \author IT-Dummkopfen
 */

/* This file is part of C64-EmuDis (GPL-2.0-or-later). */

#include "vice.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "mainlock.h"
#include "resources.h"
#include "uiactions.h"
#include "uiapi.h"
#include "ui.h"

#include "emudis.h"

#define FM_TICK_MS      50          /* 20 refreshes per second */
#define FM_HEAT_MAX     40          /* refreshes a change stays visible (2 s) */
#define FM_TILE_SCALE   2           /* overview: 64x64 bytes -> 128x128 pixels */
#define FM_TILE_PX      (64 * FM_TILE_SCALE)
#define FM_TILE_LABEL   44          /* label height above a tile (3 lines) */
#define FM_TILE_GAP     10
#define FM_ZOOM_CELL    7           /* zoom: 64x64 bytes -> 448x448 pixels */
#define FM_ZOOM_LEFT    44          /* room for row addresses */
#define FM_ZOOM_TOP     16
#define FM_LIST_MAX     1000
#define FM_FREEZE_MAX   32

enum { LIST_ALL_CHANGES = 0, LIST_RECENT, LIST_CANDIDATES };
enum { FILTER_DECREASED = 0, FILTER_INCREASED, FILTER_DEC1, FILTER_INC1,
       FILTER_CHANGED, FILTER_UNCHANGED, FILTER_EQUALS };
enum { COL_ADDR = 0, COL_ADDR_TEXT, COL_NOW, COL_PREV, COL_BASE, COL_COUNT, COL_FROZEN, COL_N };

typedef struct fm_s {
    GtkWidget *window;
    GtkWidget *overview;
    GtkWidget *zoom;
    GtkWidget *speed_scale;
    GtkWidget *speed_label;
    GtkWidget *pause_button;
    GtkWidget *space_combo;
    GtkWidget *list_mode_combo;
    GtkWidget *filter_combo;
    GtkWidget *equals_entry;
    GtkWidget *cand_label;
    GtkWidget *list_view;
    GtkListStore *store;
    GtkWidget *zoom_title;
    GtkWidget *hover_label;
    GtkWidget *value_entry;
    GtkWidget *poke_label;

    guint timer;
    int ticks;
    int space;
    int orig_speed;
    int block;                      /* zoomed block 0-15 */
    int hover;                      /* address under the mouse or -1 */
    int selected;                   /* address selected in the list or -1 */

    bool have_data;
    uint8_t cur[EMUDIS_MEM_SIZE];
    uint8_t prev[EMUDIS_MEM_SIZE];
    uint8_t last_old[EMUDIS_MEM_SIZE];      /* value before the last change */
    uint8_t heat[EMUDIS_MEM_SIZE];
    uint32_t count[EMUDIS_MEM_SIZE];        /* changes since reset */
    int block_rate[16];                     /* changes in the last second */
    int block_window[16][20];
    emudis_vic_state_t vic;

    /* trainer search */
    bool search_active;
    uint8_t base[EMUDIS_MEM_SIZE];
    uint8_t cand[EMUDIS_MEM_SIZE];
    int cand_count;
    int block_cand[16];

    /* frozen addresses */
    int frozen_n;
    uint16_t frozen_addr[FM_FREEZE_MAX];
    uint8_t frozen_val[FM_FREEZE_MAX];

    cairo_surface_t *ov_surface;            /* 256x256 pixels, one per byte */
} fm_t;

static fm_t *fm = NULL;

/* names of the $1000 blocks (default C64 memory configuration) */
static const char *block_names[16] = {
    "Zero page, stack, screen",
    "RAM (BASIC program)",
    "RAM (BASIC program)",
    "RAM (BASIC program)",
    "RAM (BASIC program)",
    "RAM (BASIC program)",
    "RAM (BASIC program)",
    "RAM (BASIC program)",
    "RAM / cartridge ROM",
    "RAM / cartridge ROM",
    "BASIC ROM",
    "BASIC ROM",
    "RAM (free $C000)",
    "I/O: VIC, SID, color, CIA",
    "KERNAL ROM",
    "KERNAL ROM"
};

static const char *block_names_ram[16] = {
    "Zero page, stack, screen",
    "RAM", "RAM", "RAM", "RAM", "RAM", "RAM", "RAM",
    "RAM", "RAM",
    "RAM under BASIC ROM", "RAM under BASIC ROM",
    "RAM (free $C000)",
    "RAM under I/O",
    "RAM under KERNAL", "RAM under KERNAL"
};

static int fm_space(void)
{
    return fm->space == 0 ? EMUDIS_SPACE_RAM : EMUDIS_SPACE_CPU;
}

/* ------------------------------------------------------------------------ */
/* reading                                                                  */
/* ------------------------------------------------------------------------ */

/* lock must be held */
static void fm_read(void)
{
    int i, b, slot;

    /* frozen addresses are written before reading, so they show as stable */
    for (i = 0; i < fm->frozen_n; i++) {
        emudis_poke(fm_space(), fm->frozen_addr[i], fm->frozen_val[i]);
    }

    memcpy(fm->prev, fm->cur, EMUDIS_MEM_SIZE);
    emudis_read_all(fm_space(), fm->cur);
    emudis_vic_state_get(&fm->vic);

    slot = fm->ticks % 20;
    for (b = 0; b < 16; b++) {
        fm->block_window[b][slot] = 0;
    }
    if (fm->have_data) {
        for (i = 0; i < EMUDIS_MEM_SIZE; i++) {
            if (fm->cur[i] != fm->prev[i]) {
                fm->heat[i] = FM_HEAT_MAX;
                fm->last_old[i] = fm->prev[i];
                fm->count[i]++;
                fm->block_window[i >> 12][slot]++;
            } else if (fm->heat[i] > 0) {
                fm->heat[i]--;
            }
        }
    } else {
        memcpy(fm->last_old, fm->cur, EMUDIS_MEM_SIZE);
    }
    fm->have_data = true;
    fm->ticks++;

    for (b = 0; b < 16; b++) {
        int s, sum = 0;
        for (s = 0; s < 20; s++) {
            sum += fm->block_window[b][s];
        }
        fm->block_rate[b] = sum;
    }
}

/* ------------------------------------------------------------------------ */
/* colours                                                                  */
/* ------------------------------------------------------------------------ */

static uint32_t value_rgb(uint8_t v)
{
    /* dark blue for 0, lighter for higher values: easy to see structure */
    int r = 20 + v / 4;
    int g = 24 + (v * 3) / 8;
    int b = 60 + (v * 5) / 8;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t heat_rgb(uint8_t heat)
{
    /* fresh change: red, then orange/yellow, fading out */
    double f = (double)heat / FM_HEAT_MAX;
    int r = 255;
    int g = (int)(220.0 * (1.0 - f) + 30.0 * f);
    int b = 40;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t byte_rgb(int a)
{
    if (fm->search_active && fm->cand[a] && fm->cand_count <= 4096) {
        return 0x30e050;    /* trainer search candidate: green */
    }
    if (fm->heat[a] > 0) {
        return heat_rgb(fm->heat[a]);
    }
    return value_rgb(fm->cur[a]);
}

static void set_rgb(cairo_t *cr, uint32_t rgb)
{
    cairo_set_source_rgb(cr, ((rgb >> 16) & 0xff) / 255.0,
                         ((rgb >> 8) & 0xff) / 255.0, (rgb & 0xff) / 255.0);
}

/* ------------------------------------------------------------------------ */
/* overview                                                                 */
/* ------------------------------------------------------------------------ */

static void tile_origin(int b, int *x, int *y)
{
    *x = FM_TILE_GAP + (b & 3) * (FM_TILE_PX + FM_TILE_GAP);
    *y = FM_TILE_GAP + (b >> 2) * (FM_TILE_PX + FM_TILE_LABEL + FM_TILE_GAP) + FM_TILE_LABEL;
}

/* which VIC-II data lies in a block (for the tile label) */
static void vic_marks(int b, char *buf, size_t len)
{
    const emudis_vic_state_t *v = &fm->vic;
    int lo = b << 12, hi = lo + 0x1000;
    int s;
    bool spr = false;

    buf[0] = '\0';
    if (v->screen >= lo && v->screen < hi) {
        g_strlcat(buf, " screen", len);
    }
    if (!v->bitmap_mode && !v->charset_is_rom && v->charset >= lo && v->charset < hi) {
        g_strlcat(buf, " charset", len);
    }
    if (v->bitmap_mode && v->bitmap >= lo && v->bitmap < hi) {
        g_strlcat(buf, " bitmap", len);
    }
    for (s = 0; s < 8; s++) {
        if ((v->sprite_enable & (1 << s)) && v->sprite_addr[s] >= lo && v->sprite_addr[s] < hi) {
            spr = true;
        }
    }
    if (spr) {
        g_strlcat(buf, " sprites", len);
    }
}

static gboolean on_overview_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
    unsigned char *pix;
    int stride, b, a;

    if (fm == NULL) {
        return FALSE;
    }
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.16);
    cairo_paint(cr);
    if (!fm->have_data) {
        return TRUE;
    }

    /* one pixel per byte, 256x256 */
    cairo_surface_flush(fm->ov_surface);
    pix = cairo_image_surface_get_data(fm->ov_surface);
    stride = cairo_image_surface_get_stride(fm->ov_surface);
    for (a = 0; a < EMUDIS_MEM_SIZE; a++) {
        int blk = a >> 12, off = a & 0xfff;
        int px = (blk & 3) * 64 + (off & 63);
        int py = (blk >> 2) * 64 + (off >> 6);
        *(uint32_t *)(pix + py * stride + px * 4) = byte_rgb(a) | 0xff000000u;
    }
    cairo_surface_mark_dirty(fm->ov_surface);

    for (b = 0; b < 16; b++) {
        int x, y;
        char line1[96], line2[96], line3[200], marks[64];
        PangoLayout *layout;

        tile_origin(b, &x, &y);

        /* tile */
        cairo_save(cr);
        cairo_rectangle(cr, x, y, FM_TILE_PX, FM_TILE_PX);
        cairo_clip(cr);
        cairo_translate(cr, x, y);
        cairo_scale(cr, FM_TILE_SCALE, FM_TILE_SCALE);
        cairo_set_source_surface(cr, fm->ov_surface, -(b & 3) * 64, -(b >> 2) * 64);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        cairo_paint(cr);
        cairo_restore(cr);

        /* frame: selected block white, busy blocks red */
        if (b == fm->block) {
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_set_line_width(cr, 3);
        } else if (fm->block_rate[b] > 0) {
            cairo_set_source_rgb(cr, 0.9, 0.25, 0.2);
            cairo_set_line_width(cr, 1.5);
        } else {
            cairo_set_source_rgb(cr, 0.35, 0.35, 0.45);
            cairo_set_line_width(cr, 1);
        }
        cairo_rectangle(cr, x - 1.5, y - 1.5, FM_TILE_PX + 3, FM_TILE_PX + 3);
        cairo_stroke(cr);

        /* label */
        vic_marks(b, marks, sizeof marks);
        g_snprintf(line1, sizeof line1, "<span size='small'><b>$%X000-$%XFFF</b></span>",
                   (unsigned)b, (unsigned)b);
        g_snprintf(line2, sizeof line2, "<span size='small'>%s</span>",
                   fm->space == 0 ? block_names_ram[b] : block_names[b]);
        if (fm->search_active) {
            g_snprintf(line3, sizeof line3, "<span size='x-small'>%d chg/s  <span foreground='#40e060'>%d cand.</span>"
                       "<span foreground='#9bd'>%s</span></span>",
                       fm->block_rate[b], fm->block_cand[b], marks);
        } else {
            g_snprintf(line3, sizeof line3, "<span size='x-small'>%d changes/s<span foreground='#9bd'>%s</span></span>",
                       fm->block_rate[b], marks);
        }
        layout = gtk_widget_create_pango_layout(w, NULL);
        pango_layout_set_width(layout, FM_TILE_PX * PANGO_SCALE);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        cairo_set_source_rgb(cr, 0.92, 0.92, 0.96);
        pango_layout_set_markup(layout, line1, -1);
        cairo_move_to(cr, x, y - FM_TILE_LABEL);
        pango_cairo_show_layout(cr, layout);
        cairo_set_source_rgb(cr, 0.85, 0.85, 0.9);
        pango_layout_set_markup(layout, line2, -1);
        cairo_move_to(cr, x, y - FM_TILE_LABEL + 14);
        pango_cairo_show_layout(cr, layout);
        cairo_set_source_rgb(cr, 0.72, 0.72, 0.78);
        pango_layout_set_markup(layout, line3, -1);
        cairo_move_to(cr, x, y - FM_TILE_LABEL + 28);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
    }
    return TRUE;
}

static int overview_hit(double px, double py)
{
    int b;
    for (b = 0; b < 16; b++) {
        int x, y;
        tile_origin(b, &x, &y);
        if (px >= x && px < x + FM_TILE_PX && py >= y - FM_TILE_LABEL && py < y + FM_TILE_PX) {
            if (py < y) {
                return (b << 12) | 0x10000;     /* label: block only */
            }
            return (b << 12) | (((int)(py - y) / FM_TILE_SCALE) << 6) | ((int)(px - x) / FM_TILE_SCALE);
        }
    }
    return -1;
}

static void refresh_list(void);
static void update_zoom_title(void);

static gboolean on_overview_press(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
    int hit = overview_hit(ev->x, ev->y);
    if (hit >= 0) {
        fm->block = (hit & 0xffff) >> 12;
        update_zoom_title();
        refresh_list();
        gtk_widget_queue_draw(fm->overview);
        gtk_widget_queue_draw(fm->zoom);
    }
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* zoom                                                                     */
/* ------------------------------------------------------------------------ */

static void hover_text(int a)
{
    char buf[160];
    if (a < 0) {
        gtk_label_set_markup(GTK_LABEL(fm->hover_label),
                             "<tt>Move the mouse over a byte. Click a byte to select it.</tt>");
        return;
    }
    g_snprintf(buf, sizeof buf,
               "<tt>$%04X (%5d) = $%02X %3u   before last change: $%02X   changes: %u%s</tt>",
               (unsigned)a, a, fm->cur[a], fm->cur[a], fm->last_old[a], (unsigned)fm->count[a],
               fm->search_active && fm->cand[a] ? "   <span foreground='#2a2'>candidate</span>" : "");
    gtk_label_set_markup(GTK_LABEL(fm->hover_label), buf);
}

static bool is_frozen(int a, int *idx)
{
    int i;
    for (i = 0; i < fm->frozen_n; i++) {
        if (fm->frozen_addr[i] == a) {
            if (idx != NULL) {
                *idx = i;
            }
            return true;
        }
    }
    return false;
}

static gboolean on_zoom_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
    int base, r, c;
    char buf[16];

    if (fm == NULL) {
        return FALSE;
    }
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.16);
    cairo_paint(cr);
    if (!fm->have_data) {
        return TRUE;
    }
    base = fm->block << 12;

    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);

    /* column offsets every 8 bytes */
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.8);
    for (c = 0; c < 64; c += 8) {
        g_snprintf(buf, sizeof buf, "+%02X", (unsigned)c);
        cairo_move_to(cr, FM_ZOOM_LEFT + c * FM_ZOOM_CELL, FM_ZOOM_TOP - 4);
        cairo_show_text(cr, buf);
    }
    for (r = 0; r < 64; r++) {
        if ((r & 3) == 0) {
            cairo_set_source_rgb(cr, 0.7, 0.7, 0.8);
            g_snprintf(buf, sizeof buf, "$%04X", (unsigned)(base + r * 64));
            cairo_move_to(cr, 2, FM_ZOOM_TOP + r * FM_ZOOM_CELL + 8);
            cairo_show_text(cr, buf);
        }
        for (c = 0; c < 64; c++) {
            int a = base + r * 64 + c;
            set_rgb(cr, byte_rgb(a));
            cairo_rectangle(cr, FM_ZOOM_LEFT + c * FM_ZOOM_CELL, FM_ZOOM_TOP + r * FM_ZOOM_CELL,
                            FM_ZOOM_CELL - 1, FM_ZOOM_CELL - 1);
            cairo_fill(cr);
            if (is_frozen(a, NULL)) {
                cairo_set_source_rgb(cr, 0.3, 0.8, 1.0);
                cairo_set_line_width(cr, 1);
                cairo_rectangle(cr, FM_ZOOM_LEFT + c * FM_ZOOM_CELL + 0.5,
                                FM_ZOOM_TOP + r * FM_ZOOM_CELL + 0.5,
                                FM_ZOOM_CELL - 2, FM_ZOOM_CELL - 2);
                cairo_stroke(cr);
            }
        }
    }
    /* hover / selection */
    if (fm->selected >= base && fm->selected < base + 0x1000) {
        int o = fm->selected - base;
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_line_width(cr, 2);
        cairo_rectangle(cr, FM_ZOOM_LEFT + (o & 63) * FM_ZOOM_CELL - 1,
                        FM_ZOOM_TOP + (o >> 6) * FM_ZOOM_CELL - 1, FM_ZOOM_CELL + 1, FM_ZOOM_CELL + 1);
        cairo_stroke(cr);
    }
    return TRUE;
}

static int zoom_hit(double px, double py)
{
    int c = (int)((px - FM_ZOOM_LEFT) / FM_ZOOM_CELL);
    int r = (int)((py - FM_ZOOM_TOP) / FM_ZOOM_CELL);
    if (px < FM_ZOOM_LEFT || py < FM_ZOOM_TOP || c < 0 || c > 63 || r < 0 || r > 63) {
        return -1;
    }
    return (fm->block << 12) + r * 64 + c;
}

static void select_address(int a);

static gboolean on_zoom_motion(GtkWidget *w, GdkEventMotion *ev, gpointer data)
{
    fm->hover = zoom_hit(ev->x, ev->y);
    hover_text(fm->hover >= 0 ? fm->hover : fm->selected);
    return TRUE;
}

static gboolean on_zoom_press(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
    int a = zoom_hit(ev->x, ev->y);
    if (a >= 0) {
        select_address(a);
    }
    return TRUE;
}

static void update_zoom_title(void)
{
    char buf[160];
    g_snprintf(buf, sizeof buf, "<b>Block $%X000-$%XFFF</b>  %s", (unsigned)fm->block, (unsigned)fm->block,
               fm->space == 0 ? block_names_ram[fm->block] : block_names[fm->block]);
    gtk_label_set_markup(GTK_LABEL(fm->zoom_title), buf);
}

/* ------------------------------------------------------------------------ */
/* list of addresses                                                        */
/* ------------------------------------------------------------------------ */

static void update_poke(void)
{
    char buf[200];
    int a = fm->selected;
    if (a < 0) {
        gtk_label_set_markup(GTK_LABEL(fm->poke_label), "");
        return;
    }
    g_snprintf(buf, sizeof buf,
               "<tt>POKE %d,%u   (monitor: &gt;%04x %02x)%s</tt>",
               a, fm->cur[a], (unsigned)a, fm->cur[a],
               is_frozen(a, NULL) ? "   <b>frozen</b>" : "");
    gtk_label_set_markup(GTK_LABEL(fm->poke_label), buf);
}

static void select_address(int a)
{
    char buf[8];
    fm->selected = a;
    g_snprintf(buf, sizeof buf, "%u", fm->cur[a]);
    gtk_entry_set_text(GTK_ENTRY(fm->value_entry), buf);
    hover_text(a);
    update_poke();
    gtk_widget_queue_draw(fm->zoom);
}

static bool list_wants(int a, int mode)
{
    switch (mode) {
        case LIST_ALL_CHANGES:
            return fm->count[a] > 0 || is_frozen(a, NULL);
        case LIST_RECENT:
            return fm->heat[a] > 0 || is_frozen(a, NULL);
        default:
            return fm->search_active && fm->cand[a];
    }
}

static void refresh_list(void)
{
    int mode = gtk_combo_box_get_active(GTK_COMBO_BOX(fm->list_mode_combo));
    int base = fm->block << 12;
    int a, n = 0;
    GtkTreeIter it, sel_iter;
    bool have_sel = false;
    GtkTreeModel *model = GTK_TREE_MODEL(fm->store);

    /* keep the view responsive: detach while refilling */
    g_object_ref(model);
    gtk_tree_view_set_model(GTK_TREE_VIEW(fm->list_view), NULL);
    gtk_list_store_clear(fm->store);
    for (a = base; a < base + 0x1000 && n < FM_LIST_MAX; a++) {
        char at[8], now[16], prev[16], bs[16];
        if (!list_wants(a, mode)) {
            continue;
        }
        g_snprintf(at, sizeof at, "$%04X", (unsigned)a);
        g_snprintf(now, sizeof now, "$%02X %3u", fm->cur[a], fm->cur[a]);
        g_snprintf(prev, sizeof prev, "$%02X %3u", fm->last_old[a], fm->last_old[a]);
        if (fm->search_active) {
            g_snprintf(bs, sizeof bs, "$%02X %3u", fm->base[a], fm->base[a]);
        } else {
            g_strlcpy(bs, "-", sizeof bs);
        }
        gtk_list_store_insert_with_values(fm->store, &it, -1,
                                          COL_ADDR, a, COL_ADDR_TEXT, at, COL_NOW, now,
                                          COL_PREV, prev, COL_BASE, bs,
                                          COL_COUNT, (guint)fm->count[a],
                                          COL_FROZEN, is_frozen(a, NULL) ? "yes" : "", -1);
        if (a == fm->selected) {
            sel_iter = it;
            have_sel = true;
        }
        n++;
    }
    gtk_tree_view_set_model(GTK_TREE_VIEW(fm->list_view), model);
    g_object_unref(model);

    /* keep the selected address selected (the handler ignores the same address) */
    if (have_sel) {
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(fm->list_view)),
                                       &sel_iter);
    }
}

static void on_list_mode_changed(GtkComboBox *c, gpointer data)
{
    refresh_list();
}

static void on_list_activated(GtkTreeSelection *sel, gpointer data)
{
    GtkTreeModel *model;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(sel, &model, &it)) {
        gint a;
        gtk_tree_model_get(model, &it, COL_ADDR, &a, -1);
        if (a != fm->selected) {    /* not when the list is only refilled */
            select_address(a);
        }
    }
}

/* values are decimal like in BASIC (POKE), $ or 0x for hex, % for binary */
static bool parse_value(const char *text, long *value)
{
    char *end;
    int base = 10;

    while (*text == ' ') {
        text++;
    }
    if (*text == '$') {
        base = 16;
        text++;
    } else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    } else if (*text == '%') {
        base = 2;
        text++;
    } else if (*text == '#') {
        text++;
    }
    if (*text == '\0') {
        return false;
    }
    *value = strtol(text, &end, base);
    while (*end == ' ') {
        end++;
    }
    return *end == '\0';
}

/* ------------------------------------------------------------------------ */
/* trainer search                                                           */
/* ------------------------------------------------------------------------ */

static void count_candidates(void)
{
    int a;
    char buf[200];

    fm->cand_count = 0;
    memset(fm->block_cand, 0, sizeof fm->block_cand);
    for (a = 0; a < EMUDIS_MEM_SIZE; a++) {
        if (fm->cand[a]) {
            fm->cand_count++;
            fm->block_cand[a >> 12]++;
        }
    }
    if (!fm->search_active) {
        g_strlcpy(buf, "Trainer search: not started", sizeof buf);
    } else if (fm->cand_count > 4096) {
        g_snprintf(buf, sizeof buf, "<b>%d</b> candidates (shown in green from 4096 down)",
                   fm->cand_count);
    } else {
        g_snprintf(buf, sizeof buf, "<b>%d</b> candidates (green)", fm->cand_count);
    }
    gtk_label_set_markup(GTK_LABEL(fm->cand_label), buf);
}

static void on_search_start(GtkWidget *w, gpointer data)
{
    memcpy(fm->base, fm->cur, EMUDIS_MEM_SIZE);
    memset(fm->cand, 1, EMUDIS_MEM_SIZE);
    fm->search_active = true;
    count_candidates();
    gtk_combo_box_set_active(GTK_COMBO_BOX(fm->list_mode_combo), LIST_CANDIDATES);
    refresh_list();
    gtk_widget_queue_draw(fm->overview);
}

static void on_search_narrow(GtkWidget *w, gpointer data)
{
    int f = gtk_combo_box_get_active(GTK_COMBO_BOX(fm->filter_combo));
    long eq = 0;
    int a;

    if (!fm->search_active) {
        on_search_start(NULL, NULL);
        return;
    }
    if (f == FILTER_EQUALS
            && (!parse_value(gtk_entry_get_text(GTK_ENTRY(fm->equals_entry)), &eq) || eq < 0 || eq > 255)) {
        emudis_message(GTK_WINDOW(fm->window), true, "Enter the value to look for (e.g. 3 or $03).");
        return;
    }
    for (a = 0; a < EMUDIS_MEM_SIZE; a++) {
        uint8_t now = fm->cur[a], was = fm->base[a];
        bool keep;
        if (!fm->cand[a]) {
            continue;
        }
        switch (f) {
            case FILTER_DECREASED: keep = now < was; break;
            case FILTER_INCREASED: keep = now > was; break;
            case FILTER_DEC1:      keep = (uint8_t)(was - 1) == now; break;
            case FILTER_INC1:      keep = (uint8_t)(was + 1) == now; break;
            case FILTER_CHANGED:   keep = now != was; break;
            case FILTER_UNCHANGED: keep = now == was; break;
            default:               keep = now == (uint8_t)eq; break;
        }
        fm->cand[a] = keep;
    }
    /* the next comparison starts from here */
    memcpy(fm->base, fm->cur, EMUDIS_MEM_SIZE);
    count_candidates();

    /* jump to the block with the most candidates */
    if (fm->block_cand[fm->block] == 0) {
        int b, best = fm->block;
        for (b = 0; b < 16; b++) {
            if (fm->block_cand[b] > fm->block_cand[best]) {
                best = b;
            }
        }
        fm->block = best;
        update_zoom_title();
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(fm->list_mode_combo), LIST_CANDIDATES);
    refresh_list();
    gtk_widget_queue_draw(fm->overview);
    gtk_widget_queue_draw(fm->zoom);
}

static void on_search_reset(GtkWidget *w, gpointer data)
{
    fm->search_active = false;
    memset(fm->cand, 0, EMUDIS_MEM_SIZE);
    count_candidates();
    refresh_list();
    gtk_widget_queue_draw(fm->overview);
}

static void on_filter_changed(GtkComboBox *c, gpointer data)
{
    gtk_widget_set_sensitive(fm->equals_entry, gtk_combo_box_get_active(c) == FILTER_EQUALS);
}

/* ------------------------------------------------------------------------ */
/* write / freeze                                                           */
/* ------------------------------------------------------------------------ */

static bool selected_value(uint8_t *v)
{
    long val;
    if (fm->selected < 0) {
        emudis_message(GTK_WINDOW(fm->window), true, "Select an address first (click a byte or a list entry).");
        return false;
    }
    if (!parse_value(gtk_entry_get_text(GTK_ENTRY(fm->value_entry)), &val)
            || val < 0 || val > 255) {
        emudis_message(GTK_WINDOW(fm->window), true, "Enter a value from 0 to 255 (or $00-$FF).");
        return false;
    }
    *v = (uint8_t)val;
    return true;
}

static void on_write(GtkWidget *w, gpointer data)
{
    uint8_t v;
    int idx;
    if (!selected_value(&v)) {
        return;
    }
    mainlock_obtain();
    emudis_poke(fm_space(), (uint16_t)fm->selected, v);
    mainlock_release();
    if (is_frozen(fm->selected, &idx)) {
        fm->frozen_val[idx] = v;
    }
    update_poke();
}

static void on_freeze(GtkWidget *w, gpointer data)
{
    uint8_t v;
    int idx;
    if (fm->selected >= 0 && is_frozen(fm->selected, &idx)) {
        /* unfreeze */
        fm->frozen_n--;
        fm->frozen_addr[idx] = fm->frozen_addr[fm->frozen_n];
        fm->frozen_val[idx] = fm->frozen_val[fm->frozen_n];
    } else {
        if (!selected_value(&v)) {
            return;
        }
        if (fm->frozen_n >= FM_FREEZE_MAX) {
            emudis_message(GTK_WINDOW(fm->window), true, "At most %d addresses can be frozen.", FM_FREEZE_MAX);
            return;
        }
        fm->frozen_addr[fm->frozen_n] = (uint16_t)fm->selected;
        fm->frozen_val[fm->frozen_n] = v;
        fm->frozen_n++;
    }
    update_poke();
    refresh_list();
    gtk_widget_queue_draw(fm->zoom);
}

static void on_reset_counters(GtkWidget *w, gpointer data)
{
    memset(fm->count, 0, sizeof fm->count);
    memset(fm->heat, 0, sizeof fm->heat);
    refresh_list();
}

/* ------------------------------------------------------------------------ */
/* speed, pause                                                             */
/* ------------------------------------------------------------------------ */

static void on_speed_changed(GtkRange *range, gpointer data)
{
    int v = (int)gtk_range_get_value(range);
    char buf[32];
    mainlock_obtain();
    resources_set_int("Speed", v);
    mainlock_release();
    g_snprintf(buf, sizeof buf, "%3d%%", v);
    gtk_label_set_text(GTK_LABEL(fm->speed_label), buf);
}

static void set_speed(int v)
{
    gtk_range_set_value(GTK_RANGE(fm->speed_scale), v);
}

static void on_speed_preset(GtkWidget *w, gpointer data)
{
    set_speed(GPOINTER_TO_INT(data));
}

static void on_pause(GtkWidget *w, gpointer data)
{
    ui_action_trigger(ACTION_PAUSE_TOGGLE);
}

static void on_step(GtkWidget *w, gpointer data)
{
    ui_action_trigger(ACTION_ADVANCE_FRAME);
}

static void on_space_changed(GtkComboBox *c, gpointer data)
{
    fm->space = gtk_combo_box_get_active(c);
    fm->have_data = false;
    memset(fm->heat, 0, sizeof fm->heat);
    update_zoom_title();
}

/* ------------------------------------------------------------------------ */
/* timer, setup                                                             */
/* ------------------------------------------------------------------------ */

static gboolean on_tick(gpointer data)
{
    if (fm == NULL) {
        return G_SOURCE_REMOVE;
    }
    mainlock_obtain();
    fm_read();
    mainlock_release();

    gtk_widget_queue_draw(fm->overview);
    gtk_widget_queue_draw(fm->zoom);
    if (fm->ticks % 5 == 0) {           /* list and labels 4x per second */
        refresh_list();
        hover_text(fm->hover >= 0 ? fm->hover : fm->selected);
        update_poke();
        gtk_button_set_label(GTK_BUTTON(fm->pause_button), ui_pause_active() ? "Resume" : "Pause");
    }
    return G_SOURCE_CONTINUE;
}

static void on_destroy(GtkWidget *w, gpointer data)
{
    if (fm == NULL) {
        return;
    }
    if (fm->timer != 0) {
        g_source_remove(fm->timer);
    }
    /* back to the speed the user had before */
    mainlock_obtain();
    resources_set_int("Speed", fm->orig_speed);
    mainlock_release();
    if (fm->ov_surface != NULL) {
        cairo_surface_destroy(fm->ov_surface);
    }
    lib_free(fm);
    fm = NULL;
}

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

static GtkWidget *add_column(GtkWidget *view, const char *title, int col)
{
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(title, r, "text", col, NULL);
    g_object_set(r, "family", "monospace", NULL);
    gtk_tree_view_column_set_sort_column_id(c, col == COL_ADDR_TEXT ? COL_ADDR : col);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), c);
    return GTK_WIDGET(c);
}

/** \brief  Show the full memory view (called from a locked action) */
void emudis_fullmem_show(void)
{
    GtkWidget *vbox, *bar, *hbox, *right, *b, *scroll, *grid, *frame;
    static const char *spaces[] = { "RAM (64KB)", "CPU view (ROMs, I/O)" };
    static const char *list_modes[] = { "All changed addresses", "Recently changed", "Trainer candidates" };
    static const char *filters[] = { "decreased", "increased", "decreased by 1", "increased by 1",
                                     "changed", "unchanged", "equals value" };
    static const int presets[] = { 1, 5, 10, 25, 50, 100 };
    int i;

    if (fm != NULL) {
        gtk_window_present(GTK_WINDOW(fm->window));
        return;
    }
    fm = lib_calloc(1, sizeof *fm);
    fm->hover = -1;
    fm->selected = -1;
    fm->block = 0;
    resources_get_int("Speed", &fm->orig_speed);
    if (fm->orig_speed < 1 || fm->orig_speed > 100) {
        fm->orig_speed = 100;
    }
    fm->ov_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 256, 256);

    fm->window = emudis_window_new("Full Memory View", 1240, 820);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
    gtk_container_add(GTK_CONTAINER(fm->window), vbox);

    /* --- speed bar --- */
    bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(bar), emudis_label_new("<b>C64 speed</b>", true), FALSE, FALSE, 0);
    fm->speed_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(fm->speed_scale), FALSE);
    gtk_widget_set_size_request(fm->speed_scale, 260, -1);
    gtk_range_set_value(GTK_RANGE(fm->speed_scale), fm->orig_speed);
    gtk_widget_set_tooltip_text(fm->speed_scale,
            "Emulation speed 1% - 100%. Slow it down to see exactly what changes when. "
            "The old speed is restored when this window is closed.");
    fm->speed_label = gtk_label_new(NULL);
    {
        char buf[16];
        g_snprintf(buf, sizeof buf, "%3d%%", fm->orig_speed);
        gtk_label_set_text(GTK_LABEL(fm->speed_label), buf);
    }
    g_signal_connect_unlocked(fm->speed_scale, "value-changed", G_CALLBACK(on_speed_changed), NULL);
    gtk_box_pack_start(GTK_BOX(bar), fm->speed_scale, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), fm->speed_label, FALSE, FALSE, 0);
    for (i = 0; i < (int)G_N_ELEMENTS(presets); i++) {
        char buf[8];
        g_snprintf(buf, sizeof buf, "%d%%", presets[i]);
        b = gtk_button_new_with_label(buf);
        g_signal_connect_unlocked(b, "clicked", G_CALLBACK(on_speed_preset), GINT_TO_POINTER(presets[i]));
        gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);
    }
    fm->pause_button = emudis_button_new(ui_pause_active() ? "Resume" : "Pause", "Pause / resume the C64");
    g_signal_connect_unlocked(fm->pause_button, "clicked", G_CALLBACK(on_pause), NULL);
    gtk_box_pack_start(GTK_BOX(bar), fm->pause_button, FALSE, FALSE, 8);
    b = emudis_button_new("Next frame", "Pause and advance one frame (1/50 s)");
    g_signal_connect_unlocked(b, "clicked", G_CALLBACK(on_step), NULL);
    gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);
    fm->space_combo = combo_from(spaces, 2, 0);
    gtk_widget_set_tooltip_text(fm->space_combo,
            "RAM: all 64KB of RAM (also under the ROMs). CPU view: what the CPU sees (BASIC/KERNAL ROM, I/O)");
    g_signal_connect_unlocked(fm->space_combo, "changed", G_CALLBACK(on_space_changed), NULL);
    gtk_box_pack_end(GTK_BOX(bar), fm->space_combo, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(bar), gtk_label_new("Memory:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bar, FALSE, FALSE, 0);

    /* --- overview | zoom + list --- */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

    fm->overview = gtk_drawing_area_new();
    gtk_widget_set_size_request(fm->overview,
                                FM_TILE_GAP + 4 * (FM_TILE_PX + FM_TILE_GAP),
                                FM_TILE_GAP + 4 * (FM_TILE_PX + FM_TILE_LABEL + FM_TILE_GAP));
    gtk_widget_add_events(fm->overview, GDK_BUTTON_PRESS_MASK);
    g_signal_connect_unlocked(fm->overview, "draw", G_CALLBACK(on_overview_draw), NULL);
    g_signal_connect_unlocked(fm->overview, "button-press-event", G_CALLBACK(on_overview_press), NULL);
    gtk_widget_set_tooltip_text(fm->overview,
            "All 64KB, one pixel per byte. Red/yellow: just changed. Green: trainer search candidate. "
            "Click a block to zoom it.");
    frame = gtk_frame_new("All memory ($0000-$FFFF)");
    gtk_container_add(GTK_CONTAINER(frame), fm->overview);
    gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, FALSE, 0);

    right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(hbox), right, TRUE, TRUE, 0);
    fm->zoom_title = emudis_label_new("", true);
    gtk_box_pack_start(GTK_BOX(right), fm->zoom_title, FALSE, FALSE, 0);

    grid = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(right), grid, TRUE, TRUE, 0);
    fm->zoom = gtk_drawing_area_new();
    gtk_widget_set_size_request(fm->zoom, FM_ZOOM_LEFT + 64 * FM_ZOOM_CELL + 4,
                                FM_ZOOM_TOP + 64 * FM_ZOOM_CELL + 4);
    gtk_widget_add_events(fm->zoom, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect_unlocked(fm->zoom, "draw", G_CALLBACK(on_zoom_draw), NULL);
    g_signal_connect_unlocked(fm->zoom, "motion-notify-event", G_CALLBACK(on_zoom_motion), NULL);
    g_signal_connect_unlocked(fm->zoom, "button-press-event", G_CALLBACK(on_zoom_press), NULL);
    gtk_box_pack_start(GTK_BOX(grid), fm->zoom, FALSE, FALSE, 0);

    /* list */
    {
        GtkWidget *lbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *lbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkTreeSelection *sel;

        fm->list_mode_combo = combo_from(list_modes, 3, LIST_RECENT);
        g_signal_connect_unlocked(fm->list_mode_combo, "changed", G_CALLBACK(on_list_mode_changed), NULL);
        gtk_box_pack_start(GTK_BOX(lbar), fm->list_mode_combo, TRUE, TRUE, 0);
        b = emudis_button_new("Reset", "Reset the change counters");
        g_signal_connect_unlocked(b, "clicked", G_CALLBACK(on_reset_counters), NULL);
        gtk_box_pack_start(GTK_BOX(lbar), b, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(lbox), lbar, FALSE, FALSE, 0);

        fm->store = gtk_list_store_new(COL_N, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING,
                                       G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING);
        fm->list_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(fm->store));
        g_object_unref(fm->store);
        add_column(fm->list_view, "Address", COL_ADDR_TEXT);
        add_column(fm->list_view, "Now", COL_NOW);
        add_column(fm->list_view, "Before", COL_PREV);
        add_column(fm->list_view, "Baseline", COL_BASE);
        add_column(fm->list_view, "Changes", COL_COUNT);
        add_column(fm->list_view, "Frozen", COL_FROZEN);
        sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(fm->list_view));
        g_signal_connect_unlocked(sel, "changed", G_CALLBACK(on_list_activated), NULL);
        scroll = gtk_scrolled_window_new(NULL, NULL);
        gtk_widget_set_size_request(scroll, 380, -1);
        gtk_container_add(GTK_CONTAINER(scroll), fm->list_view);
        gtk_box_pack_start(GTK_BOX(lbox), scroll, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(grid), lbox, TRUE, TRUE, 0);
    }

    fm->hover_label = emudis_label_new("", true);
    gtk_box_pack_start(GTK_BOX(right), fm->hover_label, FALSE, FALSE, 0);

    /* --- trainer search --- */
    frame = gtk_frame_new("Trainer search");
    {
        GtkWidget *tb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(tb), 4);

        b = emudis_button_new("1. Start search",
                              "All addresses become candidates; the current values are the baseline");
        g_signal_connect_unlocked(b, "clicked", G_CALLBACK(on_search_start), NULL);
        gtk_box_pack_start(GTK_BOX(row1), b, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row1), gtk_label_new("2. play, then keep values that"), FALSE, FALSE, 4);
        fm->filter_combo = combo_from(filters, (int)G_N_ELEMENTS(filters), FILTER_DECREASED);
        g_signal_connect_unlocked(fm->filter_combo, "changed", G_CALLBACK(on_filter_changed), NULL);
        gtk_box_pack_start(GTK_BOX(row1), fm->filter_combo, FALSE, FALSE, 0);
        fm->equals_entry = emudis_entry_new("", 5, "Value for \"equals value\", e.g. 3 or $03");
        gtk_widget_set_sensitive(fm->equals_entry, FALSE);
        gtk_box_pack_start(GTK_BOX(row1), fm->equals_entry, FALSE, FALSE, 0);
        b = emudis_button_new("3. Narrow down",
                              "Keep only the candidates that match, compared with the last step");
        g_signal_connect_unlocked(b, "clicked", G_CALLBACK(on_search_narrow), NULL);
        gtk_box_pack_start(GTK_BOX(row1), b, FALSE, FALSE, 0);
        b = emudis_button_new("Clear", "End the trainer search");
        g_signal_connect_unlocked(b, "clicked", G_CALLBACK(on_search_reset), NULL);
        gtk_box_pack_start(GTK_BOX(row1), b, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(tb), row1, FALSE, FALSE, 0);

        fm->cand_label = emudis_label_new("Trainer search: not started", true);
        gtk_box_pack_start(GTK_BOX(row2), fm->cand_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row2), gtk_label_new("   Selected address, value:"), FALSE, FALSE, 0);
        fm->value_entry = emudis_entry_new("", 5, "New value, e.g. 9 or $09");
        gtk_box_pack_start(GTK_BOX(row2), fm->value_entry, FALSE, FALSE, 0);
        b = emudis_button_new("Write", "Write the value once");
        g_signal_connect_unlocked(b, "clicked", G_CALLBACK(on_write), NULL);
        gtk_box_pack_start(GTK_BOX(row2), b, FALSE, FALSE, 0);
        b = emudis_button_new("Freeze / unfreeze",
                              "Keep writing the value (e.g. infinite lives); click again to stop");
        g_signal_connect_unlocked(b, "clicked", G_CALLBACK(on_freeze), NULL);
        gtk_box_pack_start(GTK_BOX(row2), b, FALSE, FALSE, 0);
        fm->poke_label = emudis_label_new("", true);
        gtk_label_set_selectable(GTK_LABEL(fm->poke_label), TRUE);
        gtk_box_pack_start(GTK_BOX(row2), fm->poke_label, FALSE, FALSE, 8);
        gtk_box_pack_start(GTK_BOX(tb), row2, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(frame), tb);
    }
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 0);

    update_zoom_title();
    hover_text(-1);
    count_candidates();

    g_signal_connect_unlocked(fm->window, "destroy", G_CALLBACK(on_destroy), NULL);
    gtk_widget_show_all(fm->window);

    /* first read now (the lock is held by the action) */
    fm_read();
    fm->timer = g_timeout_add(FM_TICK_MS, on_tick, NULL);
}
