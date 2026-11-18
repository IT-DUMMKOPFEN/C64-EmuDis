/*
 * EmuBridge.java - calls into the emulator (libmain.so, src/arch/sdl/emudis_sdl.c)
 *
 * All calls are safe from the Android UI thread: the native side turns them
 * into SDL events which the emulator thread handles.
 *
 * This file is part of C64-EmuDis (GPL-2.0-or-later).
 * Author: IT-Dummkopfen
 */

package io.github.itdummkopfen.c64emudis;

public final class EmuBridge {

    /* joystick bits (VICE) */
    public static final int JOY_UP = 0x01;
    public static final int JOY_DOWN = 0x02;
    public static final int JOY_LEFT = 0x04;
    public static final int JOY_RIGHT = 0x08;
    public static final int JOY_FIRE = 0x10;

    private EmuBridge() {
    }

    /** C64 keyboard matrix key (row 0-7, column 0-7) */
    public static native void nativeKey(int row, int col, boolean down);

    /** joystick state, port 0 = control port 1, port 1 = control port 2 */
    public static native void nativeJoystick(int port, int bits);

    /** autostart a file (disk, tape, PRG, cartridge), or only attach a disk to drive 8 */
    public static native void nativeAutostart(String path, boolean attachOnly);

    public static native void nativeRestore(boolean down);

    public static native void nativeReset(boolean hard);

    /** mode: -1 toggle, 0 off, 1 on */
    public static native void nativeWarp(int mode);

    /** open or close the VICE menu */
    public static native void nativeMenu();

    /** Android back key: one menu level up while the VICE menu is open */
    public static native void nativeBack();

    public static native boolean nativeMenuActive();

    /** the C64 keyboard overlay was shown or hidden (in the menu and the
        monitor it then types instead of the Android keyboard) */
    public static native void nativeOverlay(boolean shown);

    /** trigger a VICE UI action by name, e.g. "monitor-open" */
    public static native void nativeAction(String name);

    /** set an environment variable inside the emulator's C library */
    public static native void nativeSetenv(String name, String value);

    /* --- joystick state shared by the stick and the fire buttons --------- */

    private static int joyPort = 1;     /* 0 = port 1, 1 = port 2 */
    private static int stickBits = 0;
    private static int fireBits = 0;

    public static synchronized void setStick(int bits) {
        if (bits != stickBits) {
            stickBits = bits;
            sendJoy();
        }
    }

    public static synchronized void setFire(boolean down) {
        int bits = down ? JOY_FIRE : 0;
        if (bits != fireBits) {
            fireBits = bits;
            sendJoy();
        }
    }

    /** @param port 1 or 2 */
    public static synchronized void setJoyPort(int port) {
        int p = (port == 1) ? 0 : 1;
        if (p != joyPort) {
            safeJoystick(joyPort, 0);    /* release the old port */
            joyPort = p;
            sendJoy();
        }
    }

    /** @return 1 or 2 */
    public static synchronized int getJoyPort() {
        return joyPort + 1;
    }

    private static void sendJoy() {
        safeJoystick(joyPort, stickBits | fireBits);
    }

    private static void safeJoystick(int port, int bits) {
        try {
            nativeJoystick(port, bits);
        } catch (UnsatisfiedLinkError e) {
            /* native library not loaded (yet) */
        }
    }
}
