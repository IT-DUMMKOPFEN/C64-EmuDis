/*
 * c64turbo.h - C64-EmuDis: CPU turbo (like the Ultimate 64 turbo modes)
 *
 * Written by
 *  IT-Dummkopfen
 *
 * This file is part of C64-EmuDis (GPL-2.0-or-later).
 */

#ifndef C64TURBO_H
#define C64TURBO_H

/** \brief  The speeds that can be chosen (MHz, 1 = normal C64 speed) */
#define C64TURBO_SPEEDS { 1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 20, 24, 32, 40, 48, 65, 77 }
#define C64TURBO_NUM_SPEEDS 17

/** \brief  Active turbo factor: CPU cycles per system cycle (1 = off)
 *
 * Read by the x64sc CPU (c64cpusc.c) for every cycle.
 */
extern int c64turbo_div;

/** \brief  Counts the CPU cycles within one system cycle (c64cpusc.c) */
extern int c64turbo_sub;

int c64turbo_resources_init(void);
int c64turbo_cmdline_options_init(void);

/** \brief  The CPU changed the serial bus lines (c64cia2.c) */
void c64turbo_iec_write(void);

/** \brief  Called once per frame: pauses the turbo while a disk or tape loads */
void c64turbo_vsync(void);

/** \brief  Current turbo setting in MHz (1 = off) */
int c64turbo_get_speed(void);

/** \brief  True while the turbo is paused because a disk or tape is loading */
int c64turbo_is_paused(void);

#endif
