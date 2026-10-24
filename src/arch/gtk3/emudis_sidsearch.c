/** \file   emudis_sidsearch.c
 * \brief   C64-EmuDis - find SID music players in memory
 *
 * Finds music routines by looking for code that writes to the SID
 * registers ($D400-$D7FF), groups those writes into regions, guesses the
 * init/play entry points from the classic "JMP init / JMP play" table,
 * finds JSR callers of the play routine (usually in an IRQ handler) and
 * PSID/RSID headers. Regions can be exported as PSID v2 (.sid) or .prg.
 * A live view of the SID registers shows what is playing right now.
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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "mainlock.h"

#include "emudis.h"

#define MAX_WRITES  4096
#define MAX_REGIONS 64
#define CLUSTER_GAP 0x600

enum { C_RANGE = 0, C_INIT, C_PLAY, C_WRITES, C_NOTES, C_INDEX, N_COLS };

typedef struct sid_region_s {
    uint16_t start, end;        /* data range to export */
    uint16_t code_start, code_end;
    int      init, play;        /* -1 if unknown */
    int      writes;
    int      regs;              /* distinct registers */
    bool     psid;
    char     name[33], author[33], released[33];
    int      songs;
    char     notes[256];
} sid_region_t;

typedef struct sidsearch_s {
    GtkWidget *window;
    GtkWidget *tree;
    GtkListStore *store;
    GtkWidget *info;
    GtkWidget *live;
    GtkWidget *live_check;
    GtkWidget *e_name, *e_author, *e_released, *e_start, *e_end, *e_init, *e_play, *e_songs;
    uint8_t ram[EMUDIS_MEM_SIZE];
    sid_region_t regions[MAX_REGIONS];
    int nregions;
    int selected;
    guint timer;
} sidsearch_t;

static sidsearch_t *sd = NULL;


/* ------------------------------------------------------------------------ */
/* Analysis                                                                 */
/* ------------------------------------------------------------------------ */

static bool is_sid_write(const uint8_t *m, int a, int *reg)
{
    uint8_t op = m[a];
    uint8_t hi = m[(a + 2) & 0xffff];

    if ((op == 0x8d || op == 0x8e || op == 0x8c || op == 0x9d || op == 0x99)
        && hi >= 0xd4 && hi <= 0xd7) {
        *reg = m[(a + 1) & 0xffff] & 0x1f;
        return true;
    }
    return false;
}

static uint16_t rd16(const uint8_t *m, int a)
{
    return (uint16_t)(m[a & 0xffff] | (m[(a + 1) & 0xffff] << 8));
}

/* find "JMP x / JMP y" right before or at the start of a code cluster */
static void find_entry_points(const uint8_t *m, sid_region_t *r)
{
    int a;
    int lo = r->code_start - 0x400;
    int best = -1;

    if (lo < 0) {
        lo = 0;
    }
    for (a = lo; a <= r->code_start && a + 5 < EMUDIS_MEM_SIZE; a++) {
        if (m[a] == 0x4c && m[a + 3] == 0x4c) {
            int t1 = rd16(m, a + 1);
            int t2 = rd16(m, a + 4);
            if (t1 >= lo && t1 <= r->code_end + 0x800 && t2 >= lo && t2 <= r->code_end + 0x800) {
                best = a;   /* keep the last one before the cluster start */
            }
        }
    }
    if (best >= 0) {
        r->init = best;
        r->play = best + 3;
        r->start = (uint16_t)best;
    } else {
        r->init = -1;
        r->play = -1;
        r->start = (uint16_t)(r->code_start & 0xff00);
    }
}

/* data usually follows the code: extend until a long run of zeros */
static void find_region_end(const uint8_t *m, sid_region_t *r, int limit)
{
    int a = r->code_end;
    int last_nonzero = a;
    int zeros = 0;

    while (a < limit && a < EMUDIS_MEM_SIZE) {
        if (m[a] == 0 || m[a] == 0xff) {
            zeros++;
            if (zeros >= 256) {
                break;
            }
        } else {
            zeros = 0;
            last_nonzero = a;
        }
        a++;
    }
    r->end = (uint16_t)MIN(last_nonzero + 1, 0xffff);
    /* don't run into I/O */
    if (r->start < 0xd000 && r->end >= 0xd000) {
        r->end = 0xcfff;
    }
}

static void find_text_hint(const uint8_t *m, int from, int to, char *out, int outsize)
{
    int a, run = 0, best_len = 0, best_at = -1;

    from = MAX(0, from);
    to = MIN(EMUDIS_MEM_SIZE - 1, to);
    for (a = from; a <= to; a++) {
        uint8_t c = m[a];
        bool printable = (c >= 0x20 && c < 0x7b) || (c >= 0x01 && c <= 0x1a);
        if (printable) {
            run++;
            if (run > best_len) {
                best_len = run;
                best_at = a - run + 1;
            }
        } else {
            run = 0;
        }
    }
    out[0] = '\0';
    if (best_len >= 8 && best_at >= 0) {
        int i, n = 0;
        for (i = 0; i < best_len && i < 40 && n < outsize - 1; i++) {
            uint8_t c = m[best_at + i];
            if (c >= 0x01 && c <= 0x1a) {
                c = (uint8_t)(c + 0x40);    /* screen code letter */
            }
            out[n++] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
        }
        out[n] = '\0';
    }
}

static int count_callers(const uint8_t *m, int target, int *first)
{
    int a, n = 0;

    *first = -1;
    if (target < 0) {
        return 0;
    }
    for (a = 0; a + 2 < EMUDIS_MEM_SIZE; a++) {
        if (m[a] == 0x20 && rd16(m, a + 1) == target) {
            if (*first < 0) {
                *first = a;
            }
            n++;
        }
    }
    return n;
}

static void copy_str(char *dst, const uint8_t *src, int n)
{
    int i;
    for (i = 0; i < n && src[i]; i++) {
        dst[i] = (src[i] >= 0x20 && src[i] < 0x7f) ? (char)src[i] : '?';
    }
    dst[i] = '\0';
}

static void analyse(void)
{
    const uint8_t *m = sd->ram;
    int writes[MAX_WRITES];
    int regs_of[MAX_WRITES];
    int nw = 0;
    int a, i;

    sd->nregions = 0;

    /* PSID/RSID headers left in memory */
    for (a = 0; a + 0x7c < EMUDIS_MEM_SIZE && sd->nregions < MAX_REGIONS; a++) {
        if ((m[a] == 'P' || m[a] == 'R') && m[a + 1] == 'S' && m[a + 2] == 'I' && m[a + 3] == 'D'
            && m[a + 4] == 0 && m[a + 5] >= 1 && m[a + 5] <= 4) {
            sid_region_t *r = &sd->regions[sd->nregions++];
            memset(r, 0, sizeof *r);
            r->psid = true;
            r->init = (m[a + 0x0a] << 8) | m[a + 0x0b];
            r->play = (m[a + 0x0c] << 8) | m[a + 0x0d];
            r->songs = (m[a + 0x0e] << 8) | m[a + 0x0f];
            copy_str(r->name, m + a + 0x16, 32);
            copy_str(r->author, m + a + 0x36, 32);
            copy_str(r->released, m + a + 0x56, 32);
            r->start = (uint16_t)a;
            r->end = (uint16_t)MIN(a + 0x7b, 0xffff);
            r->code_start = r->start;
            r->code_end = r->end;
            g_snprintf(r->notes, sizeof r->notes, "%cSID header in memory: \"%s\" by %s",
                       m[a], r->name, r->author);
        }
    }

    /* SID register writes */
    for (a = 0; a < EMUDIS_MEM_SIZE - 2 && nw < MAX_WRITES; a++) {
        int reg;
        if (is_sid_write(m, a, &reg)) {
            writes[nw] = a;
            regs_of[nw] = reg;
            nw++;
        }
    }

    i = 0;
    while (i < nw && sd->nregions < MAX_REGIONS) {
        int j = i;
        unsigned int regmask = 0;
        sid_region_t *r;
        int callers, first_caller, k;
        char hint[64];

        while (j + 1 < nw && writes[j + 1] - writes[j] <= CLUSTER_GAP) {
            j++;
        }
        for (k = i; k <= j; k++) {
            regmask |= 1u << regs_of[k];
        }
        r = &sd->regions[sd->nregions];
        memset(r, 0, sizeof *r);
        r->code_start = (uint16_t)writes[i];
        r->code_end = (uint16_t)(writes[j] + 3);
        r->writes = j - i + 1;
        for (k = 0; k < 32; k++) {
            if (regmask & (1u << k)) {
                r->regs++;
            }
        }
        /* skip single accidental matches inside data */
        if (r->writes < 2 && r->regs < 2) {
            i = j + 1;
            continue;
        }
        find_entry_points(m, r);
        find_region_end(m, r, (j + 1 < nw) ? writes[j + 1] : EMUDIS_MEM_SIZE);
        callers = count_callers(m, r->play, &first_caller);
        find_text_hint(m, r->start - 0x40, r->end, hint, sizeof hint);

        {
            const char *kind = (r->writes >= 6 && r->regs >= 6) ? "Music player"
                             : (r->writes >= 3) ? "SID routine (music or sound effects)"
                             : "SID access";
            GString *n = g_string_new(kind);
            g_string_append_printf(n, ", %d regs", r->regs);
            if (callers > 0) {
                g_string_append_printf(n, ", play called from $%04X%s", (unsigned int)first_caller,
                                       callers > 1 ? " (+more)" : "");
            }
            if (rd16(m, 0x0314) >= r->start && rd16(m, 0x0314) <= r->end) {
                g_string_append(n, ", IRQ vector $0314 points here");
            }
            if (hint[0]) {
                g_string_append_printf(n, ", text: \"%s\"", hint);
            }
            g_strlcpy(r->notes, n->str, sizeof r->notes);
            g_string_free(n, TRUE);
        }
        g_strlcpy(r->name, "Unknown", sizeof r->name);
        g_strlcpy(r->author, "Unknown", sizeof r->author);
        g_strlcpy(r->released, "Ripped with " EMUDIS_TITLE, sizeof r->released);
        r->songs = 1;
        sd->nregions++;
        i = j + 1;
    }
}


/* ------------------------------------------------------------------------ */
/* UI                                                                       */
/* ------------------------------------------------------------------------ */

static void set_entry_hex(GtkWidget *e, int v)
{
    char t[16];
    if (v < 0) {
        gtk_entry_set_text(GTK_ENTRY(e), "");
    } else {
        g_snprintf(t, sizeof t, "$%04X", (unsigned int)v);
        gtk_entry_set_text(GTK_ENTRY(e), t);
    }
}

static void fill_list(void)
{
    int i;

    gtk_list_store_clear(sd->store);
    for (i = 0; i < sd->nregions; i++) {
        sid_region_t *r = &sd->regions[i];
        GtkTreeIter it;
        char range[32], init[16], play[16];
        g_snprintf(range, sizeof range, "$%04X-$%04X", r->start, r->end);
        if (r->init >= 0) {
            g_snprintf(init, sizeof init, "$%04X", (unsigned int)r->init);
        } else {
            g_strlcpy(init, "?", sizeof init);
        }
        if (r->play >= 0) {
            g_snprintf(play, sizeof play, "$%04X", (unsigned int)r->play);
        } else {
            g_strlcpy(play, "?", sizeof play);
        }
        gtk_list_store_append(sd->store, &it);
        gtk_list_store_set(sd->store, &it, C_RANGE, range, C_INIT, init, C_PLAY, play,
                           C_WRITES, r->writes, C_NOTES, r->notes, C_INDEX, i, -1);
    }
}

/* locked */
static void on_scan(GtkWidget *w, gpointer d)
{
    char buf[200];
    int music = 0, i;

    emudis_read_all(EMUDIS_SPACE_RAM, sd->ram);
    analyse();
    sd->selected = -1;
    fill_list();
    for (i = 0; i < sd->nregions; i++) {
        if (sd->regions[i].writes >= 6 || sd->regions[i].psid) {
            music++;
        }
    }
    g_snprintf(buf, sizeof buf,
               "<b>%d</b> region(s) with SID access found, <b>%d</b> look like music players. "
               "IRQ vector $0314 → $%04X.",
               sd->nregions, music, rd16(sd->ram, 0x0314));
    gtk_label_set_markup(GTK_LABEL(sd->info), buf);
}

static void on_select(GtkTreeSelection *sel, gpointer data)
{
    GtkTreeModel *model;
    GtkTreeIter it;
    int idx;
    sid_region_t *r;
    char t[16];

    if (!gtk_tree_selection_get_selected(sel, &model, &it)) {
        return;
    }
    gtk_tree_model_get(model, &it, C_INDEX, &idx, -1);
    sd->selected = idx;
    r = &sd->regions[idx];
    gtk_entry_set_text(GTK_ENTRY(sd->e_name), r->name);
    gtk_entry_set_text(GTK_ENTRY(sd->e_author), r->author);
    gtk_entry_set_text(GTK_ENTRY(sd->e_released), r->released);
    set_entry_hex(sd->e_start, r->start);
    set_entry_hex(sd->e_end, r->end);
    set_entry_hex(sd->e_init, r->init);
    set_entry_hex(sd->e_play, r->play);
    g_snprintf(t, sizeof t, "%d", r->songs > 0 ? r->songs : 1);
    gtk_entry_set_text(GTK_ENTRY(sd->e_songs), t);
}

static bool read_fields(long *start, long *end, long *init, long *play, long *songs)
{
    if (!emudis_parse_number(gtk_entry_get_text(GTK_ENTRY(sd->e_start)), start)
        || !emudis_parse_number(gtk_entry_get_text(GTK_ENTRY(sd->e_end)), end)
        || *start < 0 || *end > 0xffff || *end < *start) {
        emudis_message(GTK_WINDOW(sd->window), true, "Please enter a valid start/end range.");
        return false;
    }
    if (!emudis_parse_number(gtk_entry_get_text(GTK_ENTRY(sd->e_init)), init)) {
        *init = *start;
    }
    if (!emudis_parse_number(gtk_entry_get_text(GTK_ENTRY(sd->e_play)), play)) {
        *play = 0;
    }
    {
        const char *s = gtk_entry_get_text(GTK_ENTRY(sd->e_songs));
        *songs = strtol(s, NULL, 10);
        if (*songs < 1 || *songs > 256) {
            *songs = 1;
        }
    }
    return true;
}

static void put_be16(uint8_t *p, int v)
{
    p[0] = (uint8_t)((v >> 8) & 0xff);
    p[1] = (uint8_t)(v & 0xff);
}

static void put_str(uint8_t *p, const char *s)
{
    int i;
    for (i = 0; i < 32 && s[i]; i++) {
        p[i] = (uint8_t)s[i];
    }
}

/* locked */
static void on_save_sid(GtkWidget *w, gpointer d)
{
    long start, end, init, play, songs;
    gchar *fn;
    uint8_t hdr[0x7c];
    FILE *fp;
    int len;

    if (!read_fields(&start, &end, &init, &play, &songs)) {
        return;
    }
    fn = emudis_choose_file(GTK_WINDOW(sd->window), "Save as PSID (.sid)", true, "ripped.sid",
                            "SID files", "*.sid;*.SID");
    if (fn == NULL) {
        return;
    }
    /* take the latest memory contents */
    emudis_read_all(EMUDIS_SPACE_RAM, sd->ram);

    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, "PSID", 4);
    put_be16(hdr + 0x04, 2);
    put_be16(hdr + 0x06, 0x7c);
    put_be16(hdr + 0x08, 0);            /* load address in the data */
    put_be16(hdr + 0x0a, (int)init);
    put_be16(hdr + 0x0c, (int)play);
    put_be16(hdr + 0x0e, (int)songs);
    put_be16(hdr + 0x10, 1);
    /* speed = 0: all songs use the vertical blank interrupt */
    put_str(hdr + 0x16, gtk_entry_get_text(GTK_ENTRY(sd->e_name)));
    put_str(hdr + 0x36, gtk_entry_get_text(GTK_ENTRY(sd->e_author)));
    put_str(hdr + 0x56, gtk_entry_get_text(GTK_ENTRY(sd->e_released)));
    put_be16(hdr + 0x76, 0x0014);       /* PAL, MOS 6581 */

    len = (int)(end - start + 1);
    fp = fopen(fn, "wb");
    if (fp == NULL
        || fwrite(hdr, 1, sizeof hdr, fp) != sizeof hdr
        || fputc((int)(start & 0xff), fp) == EOF
        || fputc((int)(start >> 8), fp) == EOF
        || fwrite(sd->ram + start, 1, (size_t)len, fp) != (size_t)len) {
        emudis_message(GTK_WINDOW(sd->window), true, "Could not write '%s'.", fn);
    } else {
        emudis_message(GTK_WINDOW(sd->window), false,
                       "Saved PSID v2 file (%d bytes of data, $%04lX-$%04lX, init $%04lX, play $%04lX).\n\n"
                       "Note: ripping is heuristic. If the tune does not play, adjust init/play "
                       "or the range and save again.",
                       len, (unsigned long)start, (unsigned long)end,
                       (unsigned long)init, (unsigned long)play);
    }
    if (fp != NULL) {
        fclose(fp);
    }
    g_free(fn);
}

/* locked */
static void on_save_prg(GtkWidget *w, gpointer d)
{
    long start, end, init, play, songs;
    gchar *fn;

    if (!read_fields(&start, &end, &init, &play, &songs)) {
        return;
    }
    fn = emudis_choose_file(GTK_WINDOW(sd->window), "Save music as PRG", true, "music.prg",
                            "PRG files", "*.prg;*.PRG");
    if (fn != NULL) {
        emudis_read_all(EMUDIS_SPACE_RAM, sd->ram);
        if (!emudis_save_data(fn, sd->ram + start, (int)(end - start + 1), true, (uint16_t)start)) {
            emudis_message(GTK_WINDOW(sd->window), true, "Could not write '%s'.", fn);
        }
        g_free(fn);
    }
}


/* ------------------------------------------------------------------------ */
/* Live SID register view                                                   */
/* ------------------------------------------------------------------------ */

static const char *note_name(unsigned int freq_reg)
{
    static char buf[16];
    static const char *names[] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    double hz = freq_reg * 985248.0 / 16777216.0;
    int n;

    if (hz < 16.0) {
        return "---";
    }
    n = (int)lround(12.0 * log2(hz / 440.0)) + 57;     /* 57 = A-4 */
    if (n < 0 || n > 107) {
        return "---";
    }
    g_snprintf(buf, sizeof buf, "%s%d", names[n % 12], n / 12);
    return buf;
}

static gboolean on_live_timer(gpointer data)
{
    uint8_t r[0x20];
    GString *s;
    int v;

    if (sd == NULL) {
        return G_SOURCE_REMOVE;
    }
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sd->live_check))) {
        return G_SOURCE_CONTINUE;
    }
    mainlock_obtain();
    emudis_read_block(EMUDIS_SPACE_IO, 0xd400, r, 0x20);
    mainlock_release();

    s = g_string_new("<tt><b>SID registers (live, last written values)</b>\n"
                     "Voice  Freq  Note  PW    Wave       Gate  A D S R\n");
    for (v = 0; v < 3; v++) {
        const uint8_t *p = r + v * 7;
        unsigned int f = (unsigned int)(p[0] | (p[1] << 8));
        unsigned int pw = (unsigned int)((p[2] | (p[3] << 8)) & 0x0fff);
        uint8_t c = p[4];
        char wave[16];
        g_snprintf(wave, sizeof wave, "%s%s%s%s%s%s",
                   (c & 0x10) ? "TRI " : "", (c & 0x20) ? "SAW " : "",
                   (c & 0x40) ? "PUL " : "", (c & 0x80) ? "NOI " : "",
                   (c & 0x02) ? "S" : "", (c & 0x04) ? "R" : "");
        if (!(c & 0xf0) && !(c & 0x06)) {
            g_strlcpy(wave, "-", sizeof wave);
        }
        g_string_append_printf(s, "  %d    %04X  %-4s  %03X   %-10s %s   %X %X %X %X\n",
                               v + 1, f, note_name(f), pw, wave,
                               (c & 1) ? "ON " : "off",
                               (unsigned int)(p[5] >> 4), (unsigned int)(p[5] & 15), (unsigned int)(p[6] >> 4), (unsigned int)(p[6] & 15));
    }
    g_string_append_printf(s, "Filter cutoff $%03X  res %X  route %s%s%s%s  mode %s%s%s%s  volume %X</tt>",
                           (unsigned int)(((r[0x16] << 3) | (r[0x15] & 7)) & 0x7ff),
                           (unsigned int)(r[0x17] >> 4),
                           (r[0x17] & 1) ? "1" : "", (r[0x17] & 2) ? "2" : "",
                           (r[0x17] & 4) ? "3" : "", (r[0x17] & 8) ? "E" : "",
                           (r[0x18] & 0x10) ? "LP " : "", (r[0x18] & 0x20) ? "BP " : "",
                           (r[0x18] & 0x40) ? "HP " : "", (r[0x18] & 0x80) ? "3OFF" : "",
                           (unsigned int)(r[0x18] & 15));
    gtk_label_set_markup(GTK_LABEL(sd->live), s->str);
    g_string_free(s, TRUE);
    return G_SOURCE_CONTINUE;
}

static void on_destroy(GtkWidget *w, gpointer d)
{
    if (sd != NULL) {
        if (sd->timer) {
            g_source_remove(sd->timer);
        }
        lib_free(sd);
        sd = NULL;
    }
}

static GtkWidget *add_field(GtkWidget *grid, int row, const char *label, int chars)
{
    GtkWidget *e = emudis_entry_new("", chars, NULL);
    GtkWidget *l = gtk_label_new(label);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), l, (row % 2) * 2, row / 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), e, (row % 2) * 2 + 1, row / 2, 1, 1);
    return e;
}

void emudis_sidsearch_show(void)
{
    GtkWidget *vbox, *row, *b, *scroll, *grid, *frame;
    GtkTreeViewColumn *col;
    GtkCellRenderer *mono;
    static const char *titles[] = { "Range", "Init", "Play", "Writes", "Notes" };
    int i;

    if (sd != NULL) {
        gtk_window_present(GTK_WINDOW(sd->window));
        return;
    }
    sd = lib_calloc(1, sizeof *sd);
    sd->selected = -1;

    sd->window = emudis_window_new("SID Finder", 980, 640);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(sd->window), vbox);

    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    b = emudis_button_new("Scan memory", "Find code that writes to the SID and PSID headers");
    g_signal_connect(b, "clicked", G_CALLBACK(on_scan), NULL);
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    sd->live_check = gtk_check_button_new_with_label("Live SID registers");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sd->live_check), TRUE);
    gtk_box_pack_start(GTK_BOX(row), sd->live_check, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(vbox), row, FALSE, FALSE, 0);

    sd->store = gtk_list_store_new(N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                   G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT);
    sd->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(sd->store));
    mono = gtk_cell_renderer_text_new();
    g_object_set(mono, "family", "Monospace", NULL);
    for (i = 0; i < 5; i++) {
        col = gtk_tree_view_column_new_with_attributes(titles[i], i < 3 ? mono : gtk_cell_renderer_text_new(),
                                                       "text", i, NULL);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(sd->tree), col);
    }
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), sd->tree);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    sd->info = emudis_label_new("", true);
    gtk_box_pack_start(GTK_BOX(vbox), sd->info, FALSE, FALSE, 0);

    frame = gtk_frame_new("Export selected region");
    grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    sd->e_name = add_field(grid, 0, "Name", 28);
    sd->e_author = add_field(grid, 1, "Author", 28);
    sd->e_released = add_field(grid, 2, "Released", 28);
    sd->e_songs = add_field(grid, 3, "Songs", 4);
    sd->e_start = add_field(grid, 4, "Start", 7);
    sd->e_end = add_field(grid, 5, "End", 7);
    sd->e_init = add_field(grid, 6, "Init", 7);
    sd->e_play = add_field(grid, 7, "Play", 7);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    b = emudis_button_new("Save .sid (PSID v2)", "Save as PSID file playable in SID players / VSID");
    g_signal_connect(b, "clicked", G_CALLBACK(on_save_sid), NULL);
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    b = emudis_button_new("Save .prg", "Save the range with load address");
    g_signal_connect(b, "clicked", G_CALLBACK(on_save_prg), NULL);
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), row, 0, 4, 4, 1);
    gtk_container_add(GTK_CONTAINER(frame), grid);
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 0);

    sd->live = emudis_label_new("", true);
    gtk_box_pack_start(GTK_BOX(vbox), sd->live, FALSE, FALSE, 0);

    g_signal_connect_unlocked(gtk_tree_view_get_selection(GTK_TREE_VIEW(sd->tree)), "changed",
                              G_CALLBACK(on_select), NULL);
    g_signal_connect_unlocked(sd->window, "destroy", G_CALLBACK(on_destroy), NULL);

    sd->timer = g_timeout_add(100, on_live_timer, NULL);
    gtk_widget_show_all(sd->window);
    on_scan(NULL, NULL);
}
