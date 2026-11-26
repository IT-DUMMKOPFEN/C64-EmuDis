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

#include <stdint.h>

/** \brief  The turbo speeds that can be chosen (MHz) */
#define C64TURBO_SPEEDS { 2, 3, 4, 5, 6, 8, 10, 12, 14, 20, 24, 32, 40, 48, 65, 77 }
#define C64TURBO_NUM_SPEEDS 16

/** \brief  1.0 in the 16.16 fixed point step below */
#define C64TURBO_ONE 65536u

/** \brief  The turbo runs right now (on and not paused for loading)
 *
 * Read by the x64sc CPU (c64cpusc.c) for every cycle.
 */
extern int c64turbo_active;

/** \brief  System cycles per CPU cycle (16.16 fixed point, c64cpusc.c) */
extern uint32_t c64turbo_step;

/** \brief  Accumulator for the step (c64cpusc.c) */
extern uint32_t c64turbo_acc;

int c64turbo_resources_init(void);
int c64turbo_cmdline_options_init(void);

/** \brief  The CPU changed the serial bus lines (c64cia2.c) */
void c64turbo_iec_write(void);

/** \brief  Called once per frame: pauses the turbo while a disk or tape loads */
void c64turbo_vsync(void);

/** \brief  Chosen turbo speed in MHz (also while the turbo is off) */
int c64turbo_get_speed(void);

/** \brief  Turbo switched on (resource EmuDisTurboEnabled) */
int c64turbo_is_enabled(void);

/** \brief  Normal C64 CPU clock in Hz (PAL 985248, NTSC 1022727, ...) */
long c64turbo_get_system_hz(void);

/** \brief  True while the turbo is paused because a disk or tape is loading */
int c64turbo_is_paused(void);

#endif
