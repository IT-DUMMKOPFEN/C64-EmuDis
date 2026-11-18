/**
 * \file emudis_sdl.c
 * \brief   C64-EmuDis: SDL UI glue and the Android bridge
 *
 * For every SDL build this file defines the hooks the C64-EmuDis core
 * changes expect from the UI (the GTK3 UI defines them in its own files).
 *
 * On Android it also contains the JNI functions the Java side
 * (io.github.itdummkopfen.c64emudis.EmuBridge) calls for the touch
 * keyboard, the touch joystick and the buttons. The Java calls come from
 * the Android UI thread, while the emulator runs in the SDL main thread, so
 * every call is turned into an SDL user event (SDL_PushEvent is thread safe)
 * and handled in the emulator thread by emudis_sdl_handle_user_event(),
 * which ui.c calls from both of its event loops.
 *
 * \author IT-Dummkopfen
 */

/* This file is part of C64-EmuDis (GPL-2.0-or-later). */

#include "vice.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vice_sdl.h"

#include "emudis_hooks.h"
#include "gfxoutputdrv/ffmpegexedrv.h"
#include "emudis_sdl.h"

/* --- hooks used by the C64-EmuDis core changes ---------------------------- */

/* "Sprites on screen" overlay (viciisc), switched by the UI */
int emudis_sprite_overlay = 0;

/* set by the FFmpeg driver, used by the GTK3 self test */
void (*emudis_ffmpeg_sync_info)(double *video_seconds, double *audio_seconds) = NULL;

#ifndef __ANDROID__

int emudis_sdl_handle_user_event(SDL_Event *e)
{
    return 0;
}

#else /* __ANDROID__ */

#include <jni.h>
#include <android/log.h>

#include "attach.h"
#include "autostart.h"
#include "joyport/joystick.h"
#include "keyboard.h"
#include "lib.h"
#include "log.h"
#include "machine.h"
#include "resources.h"
#include "uimenu.h"
#include "vsync.h"

#define EMUDIS_EV_MAGIC     0x45440000  /* 'ED' */
#define EMUDIS_EV_MASK      0xffff0000

enum {
    EV_KEY = 1,         /* data1 = row, data2 = (col << 1) | pressed */
    EV_JOY,             /* data1 = port (0 or 1), data2 = bits */
    EV_AUTOSTART,       /* data1 = path (lib_strdup), data2 = 0 run / 1 attach only */
    EV_RESTORE,         /* data2 = pressed */
    EV_RESET,           /* data2 = 0 soft / 1 hard */
    EV_WARP,            /* data2 = -1 toggle, 0 off, 1 on */
    EV_BACK             /* Android back key */
};

#define LOG_TAG "C64-EmuDis"

static uint16_t menu_joy_bits = 0;   /* joystick bits while the menu is open */

static void push_event(int kind, intptr_t a, intptr_t b)
{
    SDL_Event e;

    SDL_zero(e);
    e.type = SDL_USEREVENT;
    e.user.code = EMUDIS_EV_MAGIC | kind;
    e.user.data1 = (void *)a;
    e.user.data2 = (void *)b;
    if (SDL_PushEvent(&e) < 1) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "SDL_PushEvent failed: %s", SDL_GetError());
        if (kind == EV_AUTOSTART) {
            free((void *)a);
        }
    }
}

static void push_key(SDL_Keycode sym, SDL_Scancode scan, int down)
{
    SDL_Event e;

    SDL_zero(e);
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.sym = sym;
    e.key.keysym.scancode = scan;
    SDL_PushEvent(&e);
}

/* while the VICE menu is open the touch joystick navigates it */
static void menu_joystick(uint16_t bits)
{
    static const struct {
        uint16_t bit;
        SDL_Keycode sym;
        SDL_Scancode scan;
    } map[] = {
        { 0x01, SDLK_UP, SDL_SCANCODE_UP },
        { 0x02, SDLK_DOWN, SDL_SCANCODE_DOWN },
        { 0x04, SDLK_LEFT, SDL_SCANCODE_LEFT },
        { 0x08, SDLK_RIGHT, SDL_SCANCODE_RIGHT },
        { 0x10, SDLK_RETURN, SDL_SCANCODE_RETURN }
    };
    size_t i;
    uint16_t changed = bits ^ menu_joy_bits;

    for (i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (changed & map[i].bit) {
            push_key(map[i].sym, map[i].scan, (bits & map[i].bit) != 0);
        }
    }
    menu_joy_bits = bits;
}

int emudis_sdl_handle_user_event(SDL_Event *e)
{
    int kind;
    intptr_t a, b;

    if (e->type != SDL_USEREVENT
            || ((uint32_t)e->user.code & EMUDIS_EV_MASK) != EMUDIS_EV_MAGIC) {
        return 0;
    }
    kind = e->user.code & 0xffff;
    a = (intptr_t)e->user.data1;
    b = (intptr_t)e->user.data2;

    switch (kind) {
        case EV_KEY:
            if (!sdl_menu_state) {
                keyboard_set_keyarr_any((int)a, (int)(b >> 1), (int)(b & 1));
            }
            break;
        case EV_JOY:
            if (sdl_menu_state) {
                menu_joystick((uint16_t)b);
            } else {
                if (menu_joy_bits) {
                    menu_joystick(0);
                }
                joystick_set_value_absolute((unsigned int)a, (uint16_t)b);
            }
            break;
        case EV_AUTOSTART:
            if (a != 0) {
                char *path = (char *)a;
                int res;
                if (b) {
                    res = file_system_attach_disk(8, 0, path);
                } else {
                    res = autostart_autodetect(path, NULL, 0, AUTOSTART_MODE_RUN);
                }
                log_message(LOG_DEFAULT, "C64-EmuDis: %s '%s': %s",
                            b ? "attach" : "autostart", path, res < 0 ? "failed" : "OK");
                free(path);
            }
            break;
        case EV_RESTORE:
            machine_set_restore_key((int)b);
            break;
        case EV_RESET:
            machine_trigger_reset(b ? MACHINE_RESET_MODE_POWER_CYCLE
                                    : MACHINE_RESET_MODE_RESET_CPU);
            break;
        case EV_WARP:
            vsync_set_warp_mode(b < 0 ? !vsync_get_warp_mode() : (int)b);
            break;
        case EV_BACK:
            if (sdl_menu_state) {
                /* one menu level up (closes the menu on the top level) */
                push_key(SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE, 1);
                push_key(SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE, 0);
            }
            break;
        default:
            break;
    }
    return 1;
}

/* --- stdout/stderr to logcat ------------------------------------------- */

/* VICE writes its log to stdout, which goes nowhere on Android: send it to
   logcat (tag "VICE") */

#include <pthread.h>
#include <unistd.h>

static int log_pipe[2] = { -1, -1 };

static void *log_thread(void *arg)
{
    char buf[1024];
    size_t used = 0;
    ssize_t n;

    while ((n = read(log_pipe[0], buf + used, sizeof buf - 1 - used)) > 0) {
        char *start = buf, *nl;
        used += (size_t)n;
        buf[used] = 0;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = 0;
            __android_log_write(ANDROID_LOG_INFO, "VICE", start);
            start = nl + 1;
        }
        used = (size_t)(buf + used - start);
        memmove(buf, start, used);
        if (used == sizeof buf - 1) {
            buf[used] = 0;
            __android_log_write(ANDROID_LOG_INFO, "VICE", buf);
            used = 0;
        }
    }
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved);

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    pthread_t t;

    if (log_pipe[0] < 0 && pipe(log_pipe) == 0) {
        setvbuf(stdout, NULL, _IOLBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        dup2(log_pipe[1], 1);
        dup2(log_pipe[1], 2);
        if (pthread_create(&t, NULL, log_thread, NULL) == 0) {
            pthread_detach(t);
        }
    }
    return JNI_VERSION_1_4;
}

/* --- JNI ------------------------------------------------------------------ */

/* JNI entry points are looked up by name, they need no prototypes */
#pragma GCC diagnostic ignored "-Wmissing-prototypes"

#define JNI_FN(name) Java_io_github_itdummkopfen_c64emudis_EmuBridge_##name

JNIEXPORT void JNICALL JNI_FN(nativeKey)(JNIEnv *env, jclass cls, jint row, jint col, jboolean down)
{
    push_event(EV_KEY, row, ((intptr_t)col << 1) | (down ? 1 : 0));
}

JNIEXPORT void JNICALL JNI_FN(nativeJoystick)(JNIEnv *env, jclass cls, jint port, jint bits)
{
    push_event(EV_JOY, port, bits & 0x1f);
}

JNIEXPORT void JNICALL JNI_FN(nativeAutostart)(JNIEnv *env, jclass cls, jstring jpath, jboolean attach_only)
{
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);

    if (path != NULL) {
        /* plain strdup: freed in the handler with free() */
        push_event(EV_AUTOSTART, (intptr_t)strdup(path), attach_only ? 1 : 0);
        (*env)->ReleaseStringUTFChars(env, jpath, path);
    }
}

JNIEXPORT void JNICALL JNI_FN(nativeRestore)(JNIEnv *env, jclass cls, jboolean down)
{
    push_event(EV_RESTORE, 0, down ? 1 : 0);
}

JNIEXPORT void JNICALL JNI_FN(nativeReset)(JNIEnv *env, jclass cls, jboolean hard)
{
    push_event(EV_RESET, 0, hard ? 1 : 0);
}

JNIEXPORT void JNICALL JNI_FN(nativeWarp)(JNIEnv *env, jclass cls, jint mode)
{
    push_event(EV_WARP, 0, mode);
}

JNIEXPORT void JNICALL JNI_FN(nativeMenu)(JNIEnv *env, jclass cls)
{
    /* the VICE menu key (F12) opens and closes the menu */
    push_key(SDLK_F12, SDL_SCANCODE_F12, 1);
    push_key(SDLK_F12, SDL_SCANCODE_F12, 0);
}

JNIEXPORT void JNICALL JNI_FN(nativeBack)(JNIEnv *env, jclass cls)
{
    push_event(EV_BACK, 0, 0);
}

/* set an environment variable for the emulator; Os.setenv in Java does not
   reach the emulator when it runs under ARM translation (emulators,
   Chromebooks), which has its own C library */
JNIEXPORT void JNICALL JNI_FN(nativeSetenv)(JNIEnv *env, jclass cls, jstring jname, jstring jval)
{
    const char *name = (*env)->GetStringUTFChars(env, jname, NULL);
    const char *value = (*env)->GetStringUTFChars(env, jval, NULL);

    if (name != NULL && value != NULL) {
        setenv(name, value, 1);
    }
    if (name != NULL) {
        (*env)->ReleaseStringUTFChars(env, jname, name);
    }
    if (value != NULL) {
        (*env)->ReleaseStringUTFChars(env, jval, value);
    }
}

JNIEXPORT jboolean JNICALL JNI_FN(nativeMenuActive)(JNIEnv *env, jclass cls)
{
    return sdl_menu_state ? JNI_TRUE : JNI_FALSE;
}

#endif /* __ANDROID__ */
