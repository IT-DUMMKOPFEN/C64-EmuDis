/** \file   emudis_pages.c
 * \brief   C64-EmuDis - Help, History and About pages
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
#include <string.h>

#include "archdep.h"
#include "lib.h"
#include "version.h"

#include "emudis.h"

#define H1(t)   "<span size='xx-large' weight='bold'>" t "</span>\n\n"
#define H2(t)   "\n<span size='large' weight='bold' foreground='#706deb'>" t "</span>\n"
#define P(t)    t "\n"
#define LI(t)   "  •  " t "\n"
#define KBD(t)  "<tt><b>" t "</b></tt>"


/* ------------------------------------------------------------------------ */

static const char *help_text =
    H1("C64-EmuDis Help")
    P("C64-EmuDis is a Commodore 64 emulator based on <b>VICE 3.10</b>. Everything VICE can do is "
      "still there - all settings, the monitor, cartridges, disk drives, printers, joysticks and so "
      "on. On top of that C64-EmuDis adds tools for looking inside the machine. They live in the "
      "<b>Tools</b> menu.")

    H2("Quick start")
    LI("Drag a .d64, .t64, .prg, .crt or .tap file onto the window, or use <i>File → Smart attach</i>. "
       "The program starts automatically.")
    LI("Loading is instant: true drive emulation is <b>off</b> by default, PRG files are injected "
       "straight into memory and autostart runs in warp mode.")
    LI("Some copy protected disks and fast loaders need a real 1541. Switch "
       "<i>Tools → Fast loading (no loading times)</i> off, or enable "
       "<i>Preferences → Settings → Peripheral devices → Drive → True drive emulation</i>.")
    LI("All VICE settings are in <i>Preferences → Settings</i>. C64-EmuDis keeps its own settings "
       "file, so an installed VICE is not affected.")

    H2("Live Memory Viewer")
    P("Shows the 64KB address space while the C64 runs. Choose between the <b>CPU view</b> "
      "(what the 6510 sees with the current banking), plain <b>RAM</b>, <b>ROM</b> and <b>I/O</b>.")
    LI("<b>Hex dump</b>: 16 bytes per row plus PETSCII, screen code or ASCII characters drawn with "
       "the real C64 character ROM.")
    LI("<b>Memory map</b>: all 64KB as a 256×256 picture, one pixel per byte. Click to jump to an address.")
    LI("Changed bytes light up red and fade out. Refresh rate: off, 1, 5, 10, 25 or 50 times per second.")
    LI("Click a byte and type hex digits to change it. " KBD("Cursor keys") " move, "
       KBD("Esc") " deselects, " KBD("PgUp/PgDn") " scroll.")
    LI("<b>Find</b>: hex bytes like " KBD("A9 00 8D") " or text in quotes like " KBD("\"READY\"")
       " (finds ASCII/PETSCII and screen codes).")
    LI("<b>Range</b>: save a memory range as .prg or .bin, or load a file into RAM.")
    LI("The bottom line shows the VIC-II setup: bank, screen, character set, bitmap and mode.")

    H2("Font Finder")
    P("Searches RAM for 2KB character sets. Each candidate gets a score from 0-100 based on how much "
      "it looks like a font (distinct characters, few isolated pixels, empty space character, "
      "vertical strokes). The character ROM is always listed. Features:")
    LI("Preview as 16×16 grid, hires or multicolor, any colours, zoom 1-4×, <b>Live update</b>.")
    LI("Fine scan finds fonts that are not 2KB aligned (e.g. in packed files).")
    LI("Export as .bin (2048 bytes), .prg or .png. <b>Activate in VIC</b> sets $D018 to the font.")

    H2("Sprite Finder")
    P("Scans RAM in 64-byte steps (the VIC-II needs sprites 64-byte aligned) and rates every block. "
      "Sprites shown by the VIC right now are framed and listed first, together with their pointer, "
      "position, colours and multicolor/expand flags.")
    LI("Hires or multicolor preview, 8× enlarged editor-style view with pixel grid.")
    LI("Export one sprite as .bin/.prg/.png, all found sprites as one .bin (64 bytes each, "
       "SpritePad raw style) or as a PNG sprite sheet.")

    H2("SID Finder")
    P("Finds music players by looking for code that writes to the SID ($D400-$D7FF). The writes are "
      "grouped into regions; for each region C64-EmuDis guesses the <b>init</b> and <b>play</b> "
      "addresses from the typical <tt>JMP init / JMP play</tt> table, looks for JSR calls to the play "
      "routine (usually in the interrupt) and shows text found near the player (composer names, "
      "tracker signatures). PSID/RSID headers in memory are found too.")
    LI("<b>Live SID registers</b>: frequencies, notes, waveforms, ADSR, filter and volume of all three voices.")
    LI("Export as <b>.sid</b> (PSID v2, plays in SID players and VSID) or .prg. Ripping is heuristic "
       "- if a tune does not play, adjust start, end, init or play and save again.")

    H2("Assembler")
    P("A two-pass 6502/6510 assembler with an editor. " KBD("Assemble + Write to RAM") " puts the code "
      "straight into the C64, " KBD("Run (SYS)") " types the SYS command for you. Supported syntax "
      "(ACME / Kick Assembler style):")
    LI("Labels: <tt>loop</tt> or <tt>loop:</tt> at the start of a line. Constants: <tt>BORDER = $d020</tt>.")
    LI("Origin: <tt>* = $c000</tt>, <tt>.org</tt>, <tt>!org</tt>, <tt>.pc</tt>.")
    LI("Data: <tt>.byte !byte .by</tt>, <tt>.word !word</tt>, <tt>!text</tt> (raw), <tt>!pet</tt> (PETSCII), "
       "<tt>!scr .text</tt> (screen codes), <tt>.fill count,value</tt>, <tt>.res count</tt>.")
    LI("Numbers: <tt>$ff 0xff %1010 255 'A'</tt>. Operators: <tt>+ - * / % &amp; | ^ &lt;&lt; &gt;&gt;</tt>, "
       "<tt>&lt;value</tt> = low byte, <tt>&gt;value</tt> = high byte, <tt>*</tt> = current address.")
    LI("All official opcodes plus the common undocumented ones (LAX, SAX, DCP, ISC, SLO, RLA, SRE, RRA, "
       "ANC, ALR/ASR, ARR, SBX/AXS, ...).")
    P("The <b>VICE monitor</b> (button <i>Open VICE monitor</i> or <i>Debug</i> hotkey) still has the "
      "classic VICE line assembler: type " KBD("a c000") " and enter instructions line by line, "
      KBD("d c000") " disassembles, " KBD("m c000") " shows memory, " KBD("x") " returns to the emulation.")

    H2("Disassembler")
    P("Turns a memory range into a listing or into source code for <b>ACME</b> or <b>Kick Assembler</b> "
      "(inspired by Dis64). Jump and branch targets get labels (<tt>l_xxxx</tt>, <tt>sub_xxxx</tt>, "
      "<tt>dat_xxxx</tt>), VIC/SID/CIA registers and KERNAL routines get names. The output re-assembles "
      "to exactly the same bytes; anything that could be encoded differently is written as bytes. "
      KBD("Open in Assembler") " lets you change the code and write it back.")
    P("The <b>Live CPU</b> panel follows the program counter and shows the registers while the C64 runs.")

    H2("Snapshots")
    P("<i>Tools → Snapshot Manager</i> has 10 slots with a screenshot of each saved state. A snapshot "
      "contains the complete machine (CPU, memory, chips, drives and optionally ROMs and disk images). "
      "The VICE <i>Snapshot</i> menu still offers load/save to any file and quicksave/quickload.")

    H2("Screenshots and video")
    LI("<i>Tools → Take screenshot</i> saves a PNG in the original resolution with border. "
       "<i>Snapshot → Save/Record media</i> offers all VICE formats (PNG, GIF, BMP, ...).")
    LI("<i>Tools → Video Recorder</i> records MP4 (H.264 + AAC) in original size with border "
       "(384×272 on PAL) or scaled <b>2×, 3× or 4×</b> with sharp pixels.")
    LI("<i>Tools → Start/stop MP4 recording</i> records with the last used size into your Videos folder.")
    LI("Video recording needs the free <b>ffmpeg</b> program. The Windows and macOS packages include it; "
       "on Linux install it with your package manager (e.g. <tt>sudo apt install ffmpeg</tt>).")

    H2("Hotkeys")
    P("Windows/Linux: " KBD("Alt+Shift") " + key, macOS: " KBD("Cmd+Shift") " + key. "
      "All hotkeys can be changed in <i>Preferences → Settings → Host → Hotkeys</i>.")
    LI(KBD("M") " Live memory viewer     " KBD("F") " Font finder     " KBD("G") " Sprite finder")
    LI(KBD("I") " SID finder     " KBD("A") " Assembler     " KBD("D") " Disassembler")
    LI(KBD("K") " Snapshot manager     " KBD("V") " Start/stop MP4 recording     " KBD("X") " Screenshot")
    P("The usual VICE hotkeys still work, e.g. " KBD("Alt+W") " warp, " KBD("Alt+P") " pause, "
      KBD("Alt+H") " monitor, " KBD("Alt+D") " fullscreen, " KBD("Alt+F9") " reset (macOS: Cmd instead of Alt).")

    H2("Where are my files?")
    LI("Settings: Linux/macOS <tt>~/.config/c64-emudis/</tt>, Windows <tt>%APPDATA%\\c64-emudis\\</tt>.")
    LI("Snapshot slots: <tt>~/.local/state/c64-emudis/snapshots/</tt> (Linux/macOS) or "
       "<tt>%LOCALAPPDATA%\\c64-emudis\\snapshots\\</tt> (Windows). "
       "<i>Snapshot Manager → Open snapshot folder</i> opens it.")
    LI("Screenshots: your <i>Pictures</i> folder, videos: your <i>Videos</i> folder (or the home folder).")

    H2("More")
    P("The complete VICE manual is available under <i>Help → Browse manual</i> (when installed) and "
      "online at <tt>https://vice-emu.sourceforge.io/vice_toc.html</tt>.\n");


/* ------------------------------------------------------------------------ */

static const char *history_text =
    H1("The History of the Commodore 64")
    P("<i>The best-selling single computer model of all time.</i>")

    H2("The idea (1981)")
    P("In 1981 the engineers of <b>MOS Technology</b>, Commodore's chip company in Pennsylvania, had "
      "designed two new chips for a games console: the graphics chip <b>VIC-II</b> (lead engineer "
      "<b>Al Charpentier</b>) and the sound chip <b>SID</b> (designed by <b>Bob Yannes</b>). Together "
      "with Bob Russell, the system architect of the VIC-20, they proposed to build a low-cost home "
      "computer around them instead. Commodore's founder and CEO <b>Jack Tramiel</b> agreed - with "
      "one condition: the machine had to have 64 KB of RAM, a huge amount for a cheap computer at "
      "the time, and it had to be ready for the Consumer Electronics Show in January 1982.")
    P("The small team - among them Yash Terakura, Shiraz Shivji, Bob Russell, Bob Yannes and "
      "David A. Ziembicki, with Charles Winterble managing at MOS - built the first prototypes in "
      "only a few months. To save time and money the C64 reused the case of the VIC-20 and its "
      "BASIC 2.0.")

    H2("Launch (1982)")
    P("The Commodore 64 was shown at the <b>Winter CES in Las Vegas in January 1982</b>. Volume "
      "shipments started in <b>August 1982</b> at a price of <b>US$595</b>. For that money buyers got:")
    LI("the <b>MOS 6510</b> CPU at 0.985 MHz (PAL) or 1.023 MHz (NTSC)")
    LI("<b>64 KB RAM</b> (38,911 bytes free for BASIC) and 20 KB ROM")
    LI("the <b>VIC-II</b>: 320×200 pixels, 16 colours, 8 hardware sprites, smooth scrolling, raster interrupts")
    LI("the <b>SID</b>: three voices, four waveforms, ADSR envelopes and an analogue filter - "
       "a real synthesizer in a home computer")
    P("Because Commodore owned MOS Technology and made its own chips, it could cut the price again "
      "and again. The famous price war of 1983 drove many competitors out of the market.")

    H2("Peak years (1983-1987)")
    P("The C64 was sold in department stores and toy shops, not only in computer shops. Estimates of "
      "the total number sold range from about <b>12.5 to 17 million</b> units; Guinness World Records "
      "lists it as the best-selling desktop computer model of all time. Around 1.3 million were sold "
      "in 1983 and about 2.6 million in 1984 alone.")
    P("Disk drives (<b>1541</b>, with its own 6502 processor) were the standard in North America, "
      "while in Europe many users loaded their games from cassette with the <b>1530 Datasette</b>. "
      "Clever programmers wrote fast loaders and turbo tapes to speed things up - C64-EmuDis "
      "simply skips the waiting.")

    H2("Models")
    LI("<b>Commodore 64</b> (1982) - the original \"breadbin\".")
    LI("<b>SX-64</b> (1983/84) - portable with a 5\" colour screen and built-in 1541 drive, "
       "the first full-colour portable computer.")
    LI("<b>Commodore 128</b> (1985) - successor with a full C64 mode.")
    LI("<b>C64C</b> (1986) - new flat case and newer chips; GEOS gave it a graphical desktop.")
    LI("<b>C64 Games System</b> (1990) - console version without keyboard, only in Europe.")
    LI("<b>Commodore 65</b> - prototypes around 1990/91, never released.")

    H2("Games, music and the demo scene")
    P("Thousands of commercial programs were made for the C64. Games such as "
      "<i>Impossible Mission</i>, <i>Boulder Dash</i>, <i>Elite</i>, <i>Maniac Mansion</i>, "
      "<i>The Last Ninja</i>, <i>Turrican</i> and <i>Wizball</i> became classics. Composers like "
      "Rob Hubbard, Martin Galway, Ben Daglish, Chris Hülsbeck and Jeroen Tel turned the SID into "
      "an instrument with its own style of music.")
    P("Young programmers pushed the hardware far beyond what its designers expected: sprites in the "
      "border, more than 8 sprites, FLI graphics and digitized speech. This grew into the "
      "<b>demo scene</b>, which is still active today with new games and demos every year.")

    H2("The end - and the afterlife")
    P("Production ended in <b>April 1994</b>, when Commodore went bankrupt - after almost twelve "
      "years. The machine never really died: emulators like VICE keep the software alive, new "
      "hardware like the Ultimate 64 and the C64 Mini appeared, and people still write new software "
      "for it. C64-EmuDis is a small part of that story.\n");


/* ------------------------------------------------------------------------ */

static char *about_text(void)
{
    GString *s = g_string_new(NULL);

    g_string_append_printf(s,
        "<span size='xx-large' weight='bold'>%s</span>\n"
        "<span size='large'>Version %s</span>\n\n"
        "A Commodore 64 emulator for Windows, macOS and Linux with memory analysis tools.\n\n"
        "<b>Author:</b> %s\n"
        "<b>Website:</b> %s\n"
        "<b>Based on:</b> %s by The VICE Team (core version %s)\n"
        "<b>GTK version:</b> %u.%u.%u\n",
        EMUDIS_NAME, EMUDIS_VERSION, EMUDIS_AUTHOR, EMUDIS_URL, EMUDIS_VICE_BASE, VERSION,
        gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version());

    g_string_append(s,
        H2("License")
        P("C64-EmuDis is free software: you can redistribute it and/or modify it under the terms of the "
          "<b>GNU General Public License, version 2 or (at your option) any later version</b>.")
        P("Why GPL? C64-EmuDis is a modified version of VICE, and VICE is licensed under the GPL. "
          "A program derived from GPL code must itself be distributed under the GPL, so the whole "
          "application - including the new C64-EmuDis code © 2026 IT-Dummkopfen - is GPL-2.0-or-later. "
          "The complete source code is available at the website above.")
        P("This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; "
          "without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.")

        H2("Used resources and their licenses")
        LI("<b>VICE 3.10</b> - The VICE Team, vice-emu.sourceforge.io - GPL-2.0-or-later")
        LI("<b>reSID</b> (SID emulation) - Dag Lem - GPL-2.0-or-later")
        LI("<b>Commodore ROM images</b> (KERNAL, BASIC, character ROM, drive DOS ROMs) - "
           "© Commodore Business Machines, distributed as part of VICE")
        LI("<b>GTK 3, GLib, Pango, GdkPixbuf, ATK</b> - LGPL-2.1-or-later")
        LI("<b>Cairo</b> - LGPL-2.1 or MPL-1.1")
        LI("<b>GLEW</b> - Modified BSD license and MIT license")
        LI("<b>libpng</b> - PNG Reference Library License; <b>zlib</b> - zlib license")
        LI("<b>linenoise-ng</b> (VICE monitor line editing) - BSD-2-Clause")
        LI("<b>FFmpeg</b> (external program for video recording) - LGPL-2.1-or-later / GPL-2.0-or-later "
           "depending on the build, ffmpeg.org")
        LI("<b>C64 Pro Mono</b> font - © Style (style64.org), free font license")
        LI("<b>PetMe</b> fonts - © Kreative Software, Relay Fonts Free Use License")
        LI("<b>Colodore</b> palette (used in the tool previews) - Philip \"Pepto\" Timmermann")

        H2("Inspiration (ideas only, no code was copied)")
        LI("emudore by Mario Ballano - Apache-2.0")
        LI("Emu64 by Thorsten Kattanek - GPL-2.0")
        LI("BMC64 by Randy Rossi - GPL-3.0")
        LI("c64 by mohrt - MIT")
        LI("Dis64 by smnjameson - disassembler with labels for Kick Assembler")

        H2("Thanks")
        P("To the VICE Team for decades of work on the most accurate Commodore emulator, and to "
          "everyone keeping the C64 alive.\n"));

    return g_string_free(s, FALSE);
}


/* ------------------------------------------------------------------------ */

typedef struct page_s {
    GtkWidget *window;
} page_t;

static page_t help_page = { NULL };
static page_t history_page = { NULL };
static page_t about_page = { NULL };

static void on_page_destroy(GtkWidget *w, gpointer data)
{
    page_t *p = data;
    p->window = NULL;
}

static void show_page(page_t *page, const char *title, const char *markup, int w, int h)
{
    GtkWidget *scroll, *label, *box, *close_btn, *vbox;

    if (page->window != NULL) {
        gtk_window_present(GTK_WINDOW(page->window));
        return;
    }
    page->window = emudis_window_new(title, w, h);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_yalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 100);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(scroll), box);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    close_btn = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(close_btn, GTK_ALIGN_END);
    g_signal_connect_swapped_unlocked(close_btn, "clicked", G_CALLBACK(gtk_widget_destroy), page->window);
    gtk_box_pack_start(GTK_BOX(vbox), close_btn, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(page->window), vbox);
    g_signal_connect_unlocked(page->window, "destroy", G_CALLBACK(on_page_destroy), page);
    gtk_widget_show_all(page->window);
    /* don't start with the text selected */
    gtk_label_select_region(GTK_LABEL(label), 0, 0);
}

void emudis_help_show(void)
{
    show_page(&help_page, "Help", help_text, 820, 760);
}

void emudis_history_show(void)
{
    show_page(&history_page, "History of the C64", history_text, 820, 760);
}

void emudis_about_show(void)
{
    char *text = about_text();
    show_page(&about_page, "About", text, 760, 720);
    g_free(text);
}
