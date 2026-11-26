/*
 * c64turbo.c - C64-EmuDis: CPU turbo (like the Ultimate 64 turbo modes)
 *
 * Written by
 *  IT-Dummkopfen
 *
 * The C64 CPU normally runs at the system clock: 0.985 MHz on a PAL C64,
 * 1.023 MHz on an NTSC C64. With the turbo on, the CPU runs at the chosen
 * speed (2 ... 77 MHz, like the Ultimate 64 turbo modes) while the VIC-II,
 * the CIAs, the SID and the drives keep the normal clock: programs run
 * faster, the picture, the music and the timers keep their normal speed.
 * The speed is exact for PAL and NTSC: per CPU cycle a fixed point
 * accumulator adds (system clock / turbo clock) and a system cycle happens
 * each time it passes 1.0. With the turbo off (resource EmuDisTurboEnabled)
 * every CPU cycle is a system cycle, exactly as without C64-EmuDis. If the computer the emulator runs on is not fast enough for
 * a high setting, the whole emulation becomes slower than real time.
 *
 * Disk and tape loaders (the KERNAL and fast loaders) count CPU cycles and
 * fail with a faster CPU, so by default the turbo is paused while the
 * serial bus is used, a drive motor runs or the datasette plays, and comes
 * back 0.5 s after that (resource EmuDisTurboLoadPause).
 *
 * Only x64sc uses the turbo (c64cpusc.c); x64 ignores it.
 *
 * This file is part of C64-EmuDis (GPL-2.0-or-later).
 */

#include "vice.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cmdline.h"
#include "datasette.h"
#include "drive.h"
#include "drivetypes.h"
#include "log.h"
#include "machine.h"
#include "resources.h"

#include "c64turbo.h"
#include "emudis_hooks.h"

int c64turbo_active = 0;
uint32_t c64turbo_step = C64TURBO_ONE;
uint32_t c64turbo_acc = 0;

static int turbo_enabled = 0;       /* resource EmuDisTurboEnabled */
static int turbo_speed = 20;        /* resource EmuDisTurbo (MHz) */
static int turbo_load_pause = 1;    /* resource EmuDisTurboLoadPause */
static int turbo_paused = 0;
static long system_hz = 0;          /* system clock the step was made for */

/* drive and tape activity, counted in frames */
static int last_half_track[NUM_DISK_UNITS];
static int frames_since_head_move = 100000;
static int idle_frames = 0;

#define HEAD_ONLY_FRAMES    25      /* head moved or serial bus used within 0.5 s */
#define RESUME_FRAMES       1       /* then: turbo on again */

static void update_div(void)
{
    int active = turbo_enabled && !turbo_paused;

    system_hz = machine_get_cycles_per_second();
    if (active && system_hz > 0) {
        /* system cycles per CPU cycle, 16.16 fixed point */
        uint64_t step = ((uint64_t)system_hz << 16) / ((uint64_t)turbo_speed * 1000000);
        c64turbo_step = step < 1 ? 1 : (step > C64TURBO_ONE ? C64TURBO_ONE : (uint32_t)step);
    } else {
        c64turbo_step = C64TURBO_ONE;
    }
    if (active != c64turbo_active) {
        c64turbo_acc = 0;
        c64turbo_active = active;
    }
}

static int is_valid_speed(int v)
{
    static const int speeds[] = C64TURBO_SPEEDS;
    int i;

    for (i = 0; i < C64TURBO_NUM_SPEEDS; i++) {
        if (speeds[i] == v) {
            return 1;
        }
    }
    return 0;
}

static int set_turbo_enabled(int val, void *param)
{
    turbo_enabled = val ? 1 : 0;
    update_div();
    return 0;
}

static int set_turbo_speed(int val, void *param)
{
    if (val == 1) {
        /* older C64-EmuDis versions used 1 for "turbo off" */
        turbo_enabled = 0;
        val = turbo_speed;
    }
    if (!is_valid_speed(val)) {
        return -1;
    }
    turbo_speed = val;
    update_div();
    return 0;
}

static int set_turbo_load_pause(int val, void *param)
{
    turbo_load_pause = val ? 1 : 0;
    if (!turbo_load_pause) {
        turbo_paused = 0;
    }
    update_div();
    return 0;
}

static const resource_int_t resources_int[] = {
    { "EmuDisTurboEnabled", 0, RES_EVENT_NO, NULL,
      &turbo_enabled, set_turbo_enabled, NULL },
    { "EmuDisTurbo", 20, RES_EVENT_NO, NULL,
      &turbo_speed, set_turbo_speed, NULL },
    { "EmuDisTurboLoadPause", 1, RES_EVENT_NO, NULL,
      &turbo_load_pause, set_turbo_load_pause, NULL },
    RESOURCE_INT_LIST_END
};

static void turbo_info(emudis_turbo_info_t *info)
{
    info->enabled = turbo_enabled;
    info->paused = turbo_paused;
    info->speed = turbo_speed;
    info->system_hz = machine_get_cycles_per_second();
}

int c64turbo_resources_init(void)
{
    int u;

    emudis_turbo_info = turbo_info;

    for (u = 0; u < NUM_DISK_UNITS; u++) {
        last_half_track[u] = -1;
    }
    return resources_register_int(resources_int);
}

static const cmdline_option_t cmdline_options[] = {
    { "-emudisturbo", SET_RESOURCE, CMDLINE_ATTRIB_NONE,
      NULL, NULL, "EmuDisTurboEnabled", (resource_value_t)1,
      NULL, "Switch the C64-EmuDis CPU turbo on" },
    { "+emudisturbo", SET_RESOURCE, CMDLINE_ATTRIB_NONE,
      NULL, NULL, "EmuDisTurboEnabled", (resource_value_t)0,
      NULL, "Switch the C64-EmuDis CPU turbo off (normal C64 speed)" },
    { "-emudisturbospeed", SET_RESOURCE, CMDLINE_ATTRIB_NEED_ARGS,
      NULL, NULL, "EmuDisTurbo", NULL,
      "<MHz>", "C64-EmuDis CPU turbo speed: 2, 3, 4, 5, 6, 8, 10, 12, 14, 20, 24, 32, 40, 48, 65, 77" },
    { "-emudisturboloadpause", SET_RESOURCE, CMDLINE_ATTRIB_NONE,
      NULL, NULL, "EmuDisTurboLoadPause", (resource_value_t)1,
      NULL, "Pause the CPU turbo while a disk or tape is loading" },
    { "+emudisturboloadpause", SET_RESOURCE, CMDLINE_ATTRIB_NONE,
      NULL, NULL, "EmuDisTurboLoadPause", (resource_value_t)0,
      NULL, "Keep the CPU turbo on while a disk or tape is loading" },
    CMDLINE_LIST_END
};

int c64turbo_cmdline_options_init(void)
{
    return cmdline_register_options(cmdline_options);
}

/* is a drive or the datasette loading right now? */
static int frames_since_iec = 100000;

static int loading(void)
{
    int u;
    int motor = 0;

    frames_since_head_move++;
    frames_since_iec++;
    for (u = 0; u < NUM_DISK_UNITS; u++) {
        diskunit_context_t *unit = diskunit_context[u];
        drive_t *drive;

        if (unit == NULL || (drive = unit->drives[0]) == NULL || !drive->true_emulation) {
            continue;
        }
        if (drive->byte_ready_active & BRA_MOTOR_ON) {
            motor = 1;
        }
        if (last_half_track[u] >= 0 && drive->current_half_track != last_half_track[u]) {
            frames_since_head_move = 0;
        }
        last_half_track[u] = drive->current_half_track;
    }
    /* the drive motor alone does not count: the 1541 keeps it running for a
       while after loading; the C64 talks to the drive over the serial bus */
    (void)motor;
    if (frames_since_head_move < HEAD_ONLY_FRAMES || frames_since_iec < HEAD_ONLY_FRAMES) {
        return 1;
    }
    return datasette_emudis_is_playing();
}

static void pause_turbo(void)
{
    idle_frames = 0;
    if (!turbo_paused) {
        turbo_paused = 1;
        update_div();
        log_message(LOG_DEFAULT, "C64-EmuDis: CPU turbo paused while loading");
    }
}

/* the CPU changed the serial bus lines (ATN, CLK, DATA): the KERNAL and the
   fast loaders talk to a drive with exact timing, so the turbo pauses at
   once, before the first bit goes out */
void c64turbo_iec_write(void)
{
    if (turbo_load_pause && turbo_enabled) {
        frames_since_iec = 0;
        pause_turbo();
    }
}

void c64turbo_vsync(void)
{
    if (machine_get_cycles_per_second() != system_hz) {
        update_div();   /* the C64 model changed (PAL/NTSC) */
    }
    if (!turbo_load_pause || !turbo_enabled) {
        if (turbo_paused) {
            turbo_paused = 0;
            update_div();
        }
        /* keep the head positions current */
        loading();
        return;
    }

    if (loading()) {
        pause_turbo();
    } else if (turbo_paused && ++idle_frames >= RESUME_FRAMES) {
        turbo_paused = 0;
        update_div();
        log_message(LOG_DEFAULT, "C64-EmuDis: CPU turbo %d MHz on again", turbo_speed);
    }
}

int c64turbo_get_speed(void)
{
    return turbo_speed;
}

int c64turbo_is_enabled(void)
{
    return turbo_enabled;
}

long c64turbo_get_system_hz(void)
{
    return machine_get_cycles_per_second();
}

int c64turbo_is_paused(void)
{
    return turbo_paused;
}
