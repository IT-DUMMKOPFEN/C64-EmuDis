/*
 * EmuActivity.java - C64-EmuDis for Android
 *
 * The emulator itself is VICE (x64sc) with its SDL2 UI in libmain.so;
 * SDLActivity runs it and shows its picture on an SDL surface. This activity
 * puts the touch controls around that surface:
 *
 *   landscape: left column with the buttons (KEYS at the top left) and the
 *              fire button, C64 picture in the middle, joystick on the right
 *   portrait:  button row at the top (KEYS on the left), C64 picture,
 *              fire button left and joystick right below it
 *
 * KEYS shows or hides the C64 keyboard as a see-through overlay.
 * PORT switches the touch joystick between control port 1 and 2.
 *
 * This file is part of C64-EmuDis (GPL-2.0-or-later).
 * Author: IT-Dummkopfen
 */

package io.github.itdummkopfen.c64emudis;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.system.Os;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class EmuActivity extends SDLActivity {

    private static final String TAG = "C64-EmuDis";
    private static final int REQ_OPEN_FILE = 64;

    /* C64 picture with borders: 384x272 pixels, PAL pixel aspect 0.936 */
    private static final float C64_ASPECT = 1.32f;

    private SharedPreferences prefs;
    private EmuLayout controls;
    private C64KeyboardView keyboard;
    private PadButton keysBtn, menuBtn, loadBtn, portBtn, warpBtn, resetBtn, fireBtn;
    private JoystickView stick;
    private boolean warp = false;
    private long lastBack = 0;
    private final Handler handler = new Handler(Looper.getMainLooper());

    /* --- SDL setup ------------------------------------------------------ */

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected String[] getArguments() {
        /* no exit question: the Android back key handles quitting */
        return new String[] { "+confirmonexit" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        prefs = getSharedPreferences("c64emudis", Context.MODE_PRIVATE);

        /* VICE data and settings live in the app's own storage */
        File data = DataInstaller.install(this);
        File home = getFilesDir();
        File config = new File(home, "config");
        File media = new File(home, "media");
        //noinspection ResultOfMethodCallIgnored
        config.mkdirs();
        //noinspection ResultOfMethodCallIgnored
        media.mkdirs();
        /* VICE's own folders (settings, log) */
        //noinspection ResultOfMethodCallIgnored
        new File(config, "c64-emudis").mkdirs();
        //noinspection ResultOfMethodCallIgnored
        new File(home, "c64-emudis").mkdirs();
        String[][] envVars = {
            { "HOME", home.getAbsolutePath() },
            { "XDG_CONFIG_HOME", config.getAbsolutePath() },
            { "XDG_DATA_HOME", home.getAbsolutePath() },
            { "XDG_CACHE_HOME", getCacheDir().getAbsolutePath() },
            { "XDG_STATE_HOME", home.getAbsolutePath() },
            { "EMUDIS_DATADIR", data.getAbsolutePath() },
        };
        for (String[] v : envVars) {
            try {
                Os.setenv(v[0], v[1], true);
            } catch (Exception e) {
                Log.e(TAG, "setenv failed", e);
            }
        }

        super.onCreate(savedInstanceState);
        if (mBrokenLibraries || mLayout == null) {
            return;
        }

        /* the libraries are loaded now; the emulator starts later (when the
           surface exists), so this reaches it in time */
        for (String[] v : envVars) {
            try {
                EmuBridge.nativeSetenv(v[0], v[1]);
            } catch (UnsatisfiedLinkError e) {
                Log.e(TAG, "nativeSetenv failed", e);
            }
        }

        Window w = getWindow();
        w.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = w.getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            w.setAttributes(lp);
        }

        buildControls();
        EmuBridge.setJoyPort(prefs.getInt("joyport", 2));
        updatePortLabel();
    }

    /** keep the orientation the user chose (SDL would lock it to the window shape) */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        /* nothing: the manifest allows every orientation */
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }

    /* --- controls ----------------------------------------------------- */

    private void buildControls() {
        ViewGroup root = mLayout;

        /* the SDL surface moves into our layout, which places it */
        root.removeView(mSurface);
        controls = new EmuLayout(this);
        controls.setBackgroundColor(Color.BLACK);
        controls.addView(mSurface);

        int blue = Color.rgb(64, 49, 141);       /* C64 dark blue */
        int lblue = Color.rgb(112, 100, 214);    /* C64 light blue */
        int red = Color.rgb(200, 40, 40);

        keysBtn = new PadButton(this, "KEYS", lblue, false);
        menuBtn = new PadButton(this, "MENU", blue, false);
        loadBtn = new PadButton(this, "LOAD", blue, false);
        portBtn = new PadButton(this, "PORT 2", blue, false);
        warpBtn = new PadButton(this, "WARP", blue, false);
        resetBtn = new PadButton(this, "RESET", blue, false);
        fireBtn = new PadButton(this, "FIRE", red, true);
        stick = new JoystickView(this);
        keyboard = new C64KeyboardView(this);
        keyboard.setVisibility(View.GONE);

        keysBtn.setListener((b, down) -> {
            if (down) {
                toggleKeyboard();
            }
        });
        menuBtn.setListener((b, down) -> {
            if (down) {
                call(EmuBridge::nativeMenu);
            }
        });
        loadBtn.setListener((b, down) -> {
            if (!down) {
                openFile();
            }
        });
        portBtn.setListener((b, down) -> {
            if (down) {
                int port = EmuBridge.getJoyPort() == 2 ? 1 : 2;
                EmuBridge.setJoyPort(port);
                prefs.edit().putInt("joyport", port).apply();
                updatePortLabel();
                toast("Joystick in port " + port);
            }
        });
        warpBtn.setListener((b, down) -> {
            if (down) {
                warp = !warp;
                warpBtn.setActive(warp);
                final int mode = warp ? 1 : 0;
                call(() -> EmuBridge.nativeWarp(mode));
            }
        });
        resetBtn.setListener((b, down) -> {
            if (down) {
                call(() -> EmuBridge.nativeReset(false));
                toast("Reset");
            }
        });
        fireBtn.setListener((b, down) -> EmuBridge.setFire(down));

        controls.addView(stick);
        controls.addView(fireBtn);
        controls.addView(menuBtn);
        controls.addView(loadBtn);
        controls.addView(portBtn);
        controls.addView(warpBtn);
        controls.addView(resetBtn);
        controls.addView(keyboard);
        controls.addView(keysBtn);       /* on top: always reachable */

        root.addView(controls, 0, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        mSurface.requestFocus();
    }

    private void updatePortLabel() {
        if (portBtn != null) {
            portBtn.setLabel("PORT " + EmuBridge.getJoyPort());
        }
    }

    private void toggleKeyboard() {
        final boolean show = keyboard.getVisibility() != View.VISIBLE;
        if (show) {
            keyboard.setVisibility(View.VISIBLE);
            keysBtn.setActive(true);
        } else {
            keyboard.releaseAll();
            keyboard.setVisibility(View.GONE);
            keysBtn.setActive(false);
        }
        /* in the menu and the monitor the overlay replaces the Android keyboard */
        call(() -> EmuBridge.nativeOverlay(show));
        controls.requestLayout();
    }

    /* "am start ... --es action monitor-open" triggers a VICE action (used by
       the CI smoke test) */
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        String action = intent == null ? null : intent.getStringExtra("action");
        if (action != null && !action.isEmpty()) {
            call(() -> EmuBridge.nativeAction(action));
        }
    }

    private interface NativeCall {
        void run();
    }

    private void call(NativeCall c) {
        try {
            c.run();
        } catch (UnsatisfiedLinkError e) {
            Log.w(TAG, "native call failed", e);
        }
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show();
    }

    /* --- layout ------------------------------------------------------- */

    private class EmuLayout extends ViewGroup {

        EmuLayout(Context c) {
            super(c);
            setMotionEventSplittingEnabled(true);
        }

        @Override
        protected void onMeasure(int wSpec, int hSpec) {
            setMeasuredDimension(MeasureSpec.getSize(wSpec), MeasureSpec.getSize(hSpec));
        }

        private void place(View v, float l, float t, float r, float b) {
            int il = Math.round(l), it = Math.round(t), ir = Math.round(r), ib = Math.round(b);
            v.measure(MeasureSpec.makeMeasureSpec(Math.max(0, ir - il), MeasureSpec.EXACTLY),
                      MeasureSpec.makeMeasureSpec(Math.max(0, ib - it), MeasureSpec.EXACTLY));
            v.layout(il, it, ir, ib);
        }

        @Override
        protected void onLayout(boolean changed, int l, int t, int r, int b) {
            float w = r - l, h = b - t;
            if (w > h) {
                layoutLandscape(w, h);
            } else {
                layoutPortrait(w, h);
            }
        }

        private void layoutLandscape(float w, float h) {
            float imgW = h * C64_ASPECT;
            float rest = w - imgW;
            float leftW = Math.max(h * 0.20f, Math.min(rest * 0.38f, h * 0.30f));
            float rightW = Math.max(h * 0.42f, rest - leftW);
            if (leftW + rightW > w * 0.6f) {
                float s = w * 0.6f / (leftW + rightW);
                leftW *= s;
                rightW *= s;
            }

            place(mSurface, leftW, 0, w - rightW, h);

            /* left column: buttons at the top (KEYS first), fire at the bottom */
            PadButton[] col = { keysBtn, menuBtn, loadBtn, portBtn, warpBtn, resetBtn };
            float colH = h * 0.62f;
            float bh = colH / col.length;
            float m = leftW * 0.06f;
            for (int i = 0; i < col.length; i++) {
                place(col[i], m, i * bh + m * 0.5f, leftW - m, (i + 1) * bh);
            }
            float fs = Math.min(leftW, h - colH) * 0.95f;
            float fcx = leftW / 2f, fcy = colH + (h - colH) / 2f;
            place(fireBtn, fcx - fs / 2, fcy - fs / 2, fcx + fs / 2, fcy + fs / 2);

            /* joystick on the right */
            float ss = Math.min(rightW, h * 0.9f);
            float scx = w - rightW / 2f, scy = h * 0.55f;
            scy = Math.min(scy, h - ss / 2f);
            place(stick, scx - ss / 2, scy - ss / 2, scx + ss / 2, scy + ss / 2);

            /* keyboard over the lower part, to the right of the button column */
            float kh = Math.min(h * 0.58f, (w - leftW) * 5f / 18f * 1.45f);
            place(keyboard, leftW, h - kh, w, h);
        }

        private void layoutPortrait(float w, float h) {
            /* button row at the top (KEYS on the left) */
            PadButton[] row = { keysBtn, menuBtn, loadBtn, portBtn, warpBtn, resetBtn };
            float bw = w / row.length;
            float barH = Math.min(bw * 0.62f, h * 0.08f);
            float m = bw * 0.05f;
            for (int i = 0; i < row.length; i++) {
                place(row[i], i * bw + m, m, (i + 1) * bw - m, barH);
            }

            float imgH = Math.min(w / C64_ASPECT, h * 0.45f);
            float sTop = barH + m;
            place(mSurface, 0, sTop, w, sTop + imgH);

            /* fire left, joystick right below the picture */
            float top = sTop + imgH;
            float ch = h - top;
            float ss = Math.min(w * 0.56f, ch * 0.9f);
            float scx = w - ss / 2f - w * 0.03f, scy = top + ch / 2f;
            place(stick, scx - ss / 2, scy - ss / 2, scx + ss / 2, scy + ss / 2);
            float fs = Math.min(w * 0.32f, ch * 0.5f);
            float fcx = w * 0.04f + fs / 2f;
            place(fireBtn, fcx - fs / 2, scy - fs / 2, fcx + fs / 2, scy + fs / 2);

            /* keyboard directly below the picture, over the controls */
            float kh = Math.min(ch, w * 5f / 18f * 1.6f);
            place(keyboard, 0, top, w, top + kh);
        }
    }

    /* --- loading files ------------------------------------------------ */

    private void openFile() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        try {
            startActivityForResult(i, REQ_OPEN_FILE);
        } catch (Exception e) {
            toast("No file picker found");
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_OPEN_FILE && resultCode == RESULT_OK && data != null && data.getData() != null) {
            final Uri uri = data.getData();
            new Thread(() -> {
                final File f = copyToMedia(uri);
                handler.post(() -> {
                    if (f == null) {
                        toast("Could not read the file");
                        return;
                    }
                    toast("Starting " + f.getName());
                    call(() -> EmuBridge.nativeAutostart(f.getAbsolutePath(), false));
                });
            }).start();
        }
    }

    private String displayName(Uri uri) {
        String name = null;
        try (Cursor c = getContentResolver().query(uri, null, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int i = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (i >= 0) {
                    name = c.getString(i);
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "no display name", e);
        }
        if (name == null || name.isEmpty()) {
            name = "image.d64";
        }
        return name.replaceAll("[/\\\\:*?\"<>|]", "_");
    }

    private File copyToMedia(Uri uri) {
        File out = new File(new File(getFilesDir(), "media"), displayName(uri));
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream os = new FileOutputStream(out)) {
            if (in == null) {
                return null;
            }
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) {
                os.write(buf, 0, n);
            }
            return out;
        } catch (Exception e) {
            Log.e(TAG, "copy failed", e);
            return null;
        }
    }

    /* --- back key ------------------------------------------------------ */

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == KeyEvent.ACTION_UP) {
                handleBack();
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @SuppressWarnings("deprecation")
    @Override
    public void onBackPressed() {
        handleBack();
    }

    private void handleBack() {
        if (keyboard != null && keyboard.getVisibility() == View.VISIBLE) {
            toggleKeyboard();
            return;
        }
        boolean menu = false;
        try {
            menu = EmuBridge.nativeMenuActive();
        } catch (UnsatisfiedLinkError e) {
            /* ignore */
        }
        if (menu) {
            call(EmuBridge::nativeBack);
            return;
        }
        long now = System.currentTimeMillis();
        if (now - lastBack < 2000) {
            finish();
        } else {
            lastBack = now;
            toast("Press back again to quit");
        }
    }
}
