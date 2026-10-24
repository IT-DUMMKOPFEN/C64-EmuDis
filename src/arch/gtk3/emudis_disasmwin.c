/** \file   emudis_disasmwin.c
 * \brief   C64-EmuDis - disassembler window with live CPU view
 *
 * Disassembles a memory range as listing or as re-assemblable ACME /
 * Kick Assembler source with automatic labels and system labels (idea
 * inspired by Dis64). The live CPU view follows the program counter while
 * the C64 is running.
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
#include "emudis_6502.h"

typedef struct disasmwin_s {
    GtkWidget *window;
    GtkWidget *from, *to;
    GtkWidget *syntax;
    GtkWidget *labels, *syslabels, *illegal;
    GtkWidget *space;
    GtkWidget *output;
    GtkWidget *live_check;
    GtkWidget *live_view;
    GtkWidget *regs;
    uint8_t mem[EMUDIS_MEM_SIZE];
    guint timer;
} disasmwin_t;

static disasmwin_t *dw = NULL;


static void set_text(GtkWidget *tv, const char *text)
{
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), text, -1);
}

static gchar *get_text(GtkWidget *tv)
{
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(b, &s, &e);
    return gtk_text_buffer_get_text(b, &s, &e, FALSE);
}

/* locked */
static void on_disassemble(GtkWidget *w, gpointer d)
{
    long from, to;
    char *text;
    emudis_space_t space;

    if (!emudis_parse_number(gtk_entry_get_text(GTK_ENTRY(dw->from)), &from)
        || !emudis_parse_number(gtk_entry_get_text(GTK_ENTRY(dw->to)), &to)
        || from < 0 || to > 0xffff || to < from) {
        emudis_message(GTK_WINDOW(dw->window), true, "Please enter a valid range, e.g. $C000 to $C0FF.");
        return;
    }
    space = gtk_combo_box_get_active(GTK_COMBO_BOX(dw->space)) == 1 ? EMUDIS_SPACE_RAM : EMUDIS_SPACE_CPU;
    emudis_read_all(space, dw->mem);
    text = emudis_disasm_range(dw->mem, (uint16_t)from, (uint16_t)to,
                               (emudis_syntax_t)gtk_combo_box_get_active(GTK_COMBO_BOX(dw->syntax)),
                               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dw->labels)),
                               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dw->syslabels)),
                               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dw->illegal)));
    set_text(dw->output, text);
    lib_free(text);
}

static void on_save(GtkWidget *w, gpointer d)
{
    int syn = gtk_combo_box_get_active(GTK_COMBO_BOX(dw->syntax));
    gchar *fn = emudis_choose_file(GTK_WINDOW(dw->window), "Save disassembly", true,
                                   syn == EMUDIS_SYNTAX_KICKASS ? "disassembly.asm"
                                   : syn == EMUDIS_SYNTAX_ACME ? "disassembly.a" : "disassembly.txt",
                                   "Text / source files", "*.asm;*.a;*.s;*.txt");
    gchar *text;

    if (fn == NULL) {
        return;
    }
    text = get_text(dw->output);
    if (!g_file_set_contents(fn, text, -1, NULL)) {
        emudis_message(GTK_WINDOW(dw->window), true, "Could not write '%s'.", fn);
    }
    g_free(text);
    g_free(fn);
}

static void on_copy(GtkWidget *w, gpointer d)
{
    gchar *text = get_text(dw->output);
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
    g_free(text);
}

static void on_to_assembler(GtkWidget *w, gpointer d)
{
    gchar *text;

    if (gtk_combo_box_get_active(GTK_COMBO_BOX(dw->syntax)) == EMUDIS_SYNTAX_LISTING) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(dw->syntax), EMUDIS_SYNTAX_ACME);
        on_disassemble(NULL, NULL);
    }
    text = get_text(dw->output);
    emudis_assembler_load_text(text);
    g_free(text);
}

/* find a start address before pc that decodes exactly onto pc */
static uint16_t find_start_before(const uint8_t *mem, uint16_t pc, int lines)
{
    int back;

    for (back = lines * 3; back > 0; back--) {
        uint16_t a = (uint16_t)(pc - back);
        int count = 0;
        while (a != pc && count < lines * 3) {
            uint16_t next = (uint16_t)(a + emudis_opcode_size(mem[a]));
            if ((uint16_t)(next - (uint16_t)(pc - back)) > (uint16_t)back) {
                break;      /* stepped over pc */
            }
            a = next;
            count++;
        }
        if (a == pc && count >= lines - 2 && count <= lines + 2) {
            return (uint16_t)(pc - back);
        }
    }
    return pc;
}

static gboolean on_live_timer(gpointer data)
{
    emudis_cpu_state_t cpu;
    GString *s;
    uint16_t a;
    int i;
    char line[128];

    if (dw == NULL) {
        return G_SOURCE_REMOVE;
    }
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dw->live_check))) {
        return G_SOURCE_CONTINUE;
    }
    mainlock_obtain();
    emudis_cpu_state_request();
    emudis_read_all(EMUDIS_SPACE_CPU, dw->mem);
    mainlock_release();
    emudis_cpu_state_get(&cpu);
    if (!cpu.valid) {
        return G_SOURCE_CONTINUE;
    }

    g_snprintf(line, sizeof line,
               "<tt><b>PC</b> $%04X  <b>A</b> $%02X  <b>X</b> $%02X  <b>Y</b> $%02X  <b>SP</b> $%02X  "
               "<b>NV-BDIZC</b> %c%c%c%c%c%c%c%c</tt>",
               cpu.pc, cpu.a, cpu.x, cpu.y, cpu.sp,
               (cpu.p & 0x80) ? '1' : '0', (cpu.p & 0x40) ? '1' : '0', '1',
               (cpu.p & 0x10) ? '1' : '0', (cpu.p & 0x08) ? '1' : '0', (cpu.p & 0x04) ? '1' : '0',
               (cpu.p & 0x02) ? '1' : '0', (cpu.p & 0x01) ? '1' : '0');
    gtk_label_set_markup(GTK_LABEL(dw->regs), line);

    s = g_string_new(NULL);
    a = find_start_before(dw->mem, cpu.pc, 6);
    for (i = 0; i < 22; i++) {
        char buf[96];
        int size = emudis_disasm_line(dw->mem, a, buf, sizeof buf, true);
        g_string_append_printf(s, "%s %s\n", a == cpu.pc ? ">" : " ", buf);
        a = (uint16_t)(a + size);
    }
    set_text(dw->live_view, s->str);
    g_string_free(s, TRUE);
    return G_SOURCE_CONTINUE;
}

static void on_destroy(GtkWidget *w, gpointer d)
{
    if (dw != NULL) {
        if (dw->timer) {
            g_source_remove(dw->timer);
        }
        lib_free(dw);
        dw = NULL;
    }
}

static GtkWidget *mono_view(void)
{
    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 6);
    return tv;
}

void emudis_disassembler_show(void)
{
    GtkWidget *vbox, *bar, *bar2, *paned, *sw, *b, *right, *frame;

    if (dw != NULL) {
        gtk_window_present(GTK_WINDOW(dw->window));
        return;
    }
    dw = lib_calloc(1, sizeof *dw);

    dw->window = emudis_window_new("Disassembler", 1080, 700);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(dw->window), vbox);

    bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(bar), gtk_label_new("From"), FALSE, FALSE, 0);
    dw->from = emudis_entry_new("$E000", 7, NULL);
    gtk_box_pack_start(GTK_BOX(bar), dw->from, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), gtk_label_new("to"), FALSE, FALSE, 0);
    dw->to = emudis_entry_new("$E0FF", 7, NULL);
    gtk_box_pack_start(GTK_BOX(bar), dw->to, FALSE, FALSE, 0);
    dw->space = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dw->space), "CPU view");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dw->space), "RAM");
    gtk_combo_box_set_active(GTK_COMBO_BOX(dw->space), 0);
    gtk_box_pack_start(GTK_BOX(bar), dw->space, FALSE, FALSE, 0);
    dw->syntax = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dw->syntax), "Listing");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dw->syntax), "ACME source");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dw->syntax), "Kick Assembler source");
    gtk_combo_box_set_active(GTK_COMBO_BOX(dw->syntax), 0);
    gtk_box_pack_start(GTK_BOX(bar), dw->syntax, FALSE, FALSE, 0);
    b = emudis_button_new("Disassemble", NULL);
    g_signal_connect(b, "clicked", G_CALLBACK(on_disassemble), NULL);
    gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bar, FALSE, FALSE, 0);

    bar2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    dw->labels = gtk_check_button_new_with_label("Auto labels");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dw->labels), TRUE);
    dw->syslabels = gtk_check_button_new_with_label("System labels (VIC, SID, CIA, KERNAL)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dw->syslabels), TRUE);
    dw->illegal = gtk_check_button_new_with_label("Illegal opcodes as data");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dw->illegal), TRUE);
    gtk_box_pack_start(GTK_BOX(bar2), dw->labels, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar2), dw->syslabels, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar2), dw->illegal, FALSE, FALSE, 0);
    b = emudis_button_new("Save...", NULL);
    g_signal_connect(b, "clicked", G_CALLBACK(on_save), NULL);
    gtk_box_pack_end(GTK_BOX(bar2), b, FALSE, FALSE, 0);
    b = emudis_button_new("Copy", "Copy to clipboard");
    g_signal_connect_unlocked(b, "clicked", G_CALLBACK(on_copy), NULL);
    gtk_box_pack_end(GTK_BOX(bar2), b, FALSE, FALSE, 0);
    b = emudis_button_new("Open in Assembler", "Edit and re-assemble the source");
    g_signal_connect(b, "clicked", G_CALLBACK(on_to_assembler), NULL);
    gtk_box_pack_end(GTK_BOX(bar2), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bar2, FALSE, FALSE, 0);

    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    dw->output = mono_view();
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), dw->output);
    gtk_paned_pack1(GTK_PANED(paned), sw, TRUE, FALSE);

    right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    frame = gtk_frame_new("Live CPU");
    {
        GtkWidget *fb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(fb), 4);
        dw->live_check = gtk_check_button_new_with_label("Follow PC while running");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dw->live_check), TRUE);
        dw->regs = emudis_label_new("", true);
        dw->live_view = mono_view();
        gtk_widget_set_size_request(dw->live_view, 380, -1);
        gtk_box_pack_start(GTK_BOX(fb), dw->live_check, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(fb), dw->regs, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(fb), dw->live_view, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(frame), fb);
    }
    gtk_box_pack_start(GTK_BOX(right), frame, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(paned), right, FALSE, FALSE);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

    g_signal_connect_unlocked(dw->window, "destroy", G_CALLBACK(on_destroy), NULL);
    dw->timer = g_timeout_add(100, on_live_timer, NULL);
    gtk_widget_show_all(dw->window);
    on_disassemble(NULL, NULL);
}
