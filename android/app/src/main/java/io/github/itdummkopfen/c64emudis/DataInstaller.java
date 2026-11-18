/*
 * DataInstaller.java - unpack the VICE data (ROMs, keymaps, ...) from the APK
 *
 * VICE reads its data files with plain file functions, so they are copied
 * from assets/vice/ into the app's own storage. This happens on the first
 * start and after every app update.
 *
 * This file is part of C64-EmuDis (GPL-2.0-or-later).
 * Author: IT-Dummkopfen
 */

package io.github.itdummkopfen.c64emudis;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

public final class DataInstaller {

    private static final String TAG = "C64-EmuDis";

    private DataInstaller() {
    }

    /** @return the VICE data directory */
    public static File install(Context ctx) {
        File root = ctx.getFilesDir();
        File stamp = new File(root, "vice.stamp");
        String want = "unknown";

        try {
            PackageInfo pi = ctx.getPackageManager().getPackageInfo(ctx.getPackageName(), 0);
            want = pi.versionName + "/" + pi.lastUpdateTime;
        } catch (Exception e) {
            Log.w(TAG, "no package info", e);
        }

        if (want.equals(readText(stamp))) {
            return new File(root, "vice");
        }

        Log.i(TAG, "unpacking VICE data (" + want + ")");
        AssetManager am = ctx.getAssets();
        byte[] buf = new byte[65536];
        try (BufferedReader list = new BufferedReader(new InputStreamReader(
                am.open("vice/files.txt"), StandardCharsets.UTF_8))) {
            String name;
            while ((name = list.readLine()) != null) {
                name = name.trim();
                if (name.isEmpty()) {
                    continue;
                }
                File out = new File(root, name);
                File dir = out.getParentFile();
                if (dir != null && !dir.isDirectory() && !dir.mkdirs()) {
                    throw new IOException("cannot create " + dir);
                }
                try (InputStream in = am.open(name);
                     OutputStream os = new FileOutputStream(out)) {
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        os.write(buf, 0, n);
                    }
                }
            }
            writeText(stamp, want);
        } catch (IOException e) {
            Log.e(TAG, "unpacking VICE data failed", e);
        }
        return new File(root, "vice");
    }

    static String readText(File f) {
        try (InputStream in = new java.io.FileInputStream(f)) {
            byte[] b = new byte[(int) Math.min(f.length(), 4096)];
            int n = in.read(b);
            return n > 0 ? new String(b, 0, n, StandardCharsets.UTF_8) : "";
        } catch (IOException e) {
            return "";
        }
    }

    static void writeText(File f, String s) {
        try (OutputStream os = new FileOutputStream(f)) {
            os.write(s.getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            Log.w(TAG, "cannot write " + f, e);
        }
    }
}
