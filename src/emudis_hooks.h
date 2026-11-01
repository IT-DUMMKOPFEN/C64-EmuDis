/*
 * emudis_hooks.h - C64-EmuDis hooks between the emulator core and the UI
 *
 * This file is part of C64-EmuDis (GPL-2.0-or-later).
 */

#ifndef EMUDIS_HOOKS_H
#define EMUDIS_HOOKS_H

/* "Sprites on screen" overlay: nonzero draws a red frame around every sprite
   on the emulated screen. Defined in the GTK3 UI (actions-emudis.c). */
extern int emudis_sprite_overlay;

#endif
