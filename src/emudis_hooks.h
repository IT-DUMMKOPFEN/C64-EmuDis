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

/* CPU turbo state for the UI (status bar): set by c64/c64turbo.c in the
   C64 emulators, NULL in all others. Defined in vsync.c (every emulator). */
typedef struct emudis_turbo_info_s {
    int enabled;    /* turbo switched on */
    int paused;     /* paused because a disk or tape is loading */
    int speed;      /* chosen turbo speed in MHz */
    long system_hz; /* normal CPU clock (PAL 985248, NTSC 1022727, ...) */
} emudis_turbo_info_t;

extern void (*emudis_turbo_info)(emudis_turbo_info_t *info);

#endif
