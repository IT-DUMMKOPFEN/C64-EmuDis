/*
 * C64KeyboardView.java - the C64 keyboard as a see-through touch overlay
 *
 * Layout of the real C64 keyboard, sent to the emulator as keyboard matrix
 * positions (row, column), so every key works exactly like on the real
 * machine, independent of any host keymap.
 *
 * SHIFT, C= and CTRL are sticky: tap once and they stay down for the next
 * key (lit), tap again to release them. SHIFT LOCK locks the left SHIFT.
 * Every key stays down for at least MIN_HOLD_MS, so the C64 sees even very
 * short taps.
 *
 * This file is part of C64-EmuDis (GPL-2.0-or-later).
 * Author: IT-Dummkopfen
 */

package io.github.itdummkopfen.c64emudis;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.SparseArray;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

public class C64KeyboardView extends View {

    private static final long MIN_HOLD_MS = 70;

    /* key kinds */
    private static final int K_NORMAL = 0;
    private static final int K_SHIFT = 1;       /* sticky: left and right SHIFT */
    private static final int K_CBM = 2;         /* sticky */
    private static final int K_CTRL = 3;        /* sticky */
    private static final int K_SHIFTLOCK = 4;   /* toggles left SHIFT */
    private static final int K_RESTORE = 5;

    private static final int ROW_LSHIFT = 1, COL_LSHIFT = 7;

    private static final class Key {
        final String label, shiftLabel;
        final int row, col, kind;
        final float x, y, w;            /* in key units */
        final boolean function;
        final RectF rect = new RectF();
        boolean down;

        Key(String label, String shiftLabel, int row, int col, int kind,
            float x, float y, float w, boolean function) {
            this.label = label;
            this.shiftLabel = shiftLabel;
            this.row = row;
            this.col = col;
            this.kind = kind;
            this.x = x;
            this.y = y;
            this.w = w;
            this.function = function;
        }
    }

    private final List<Key> keys = new ArrayList<>();
    private final SparseArray<Key> pointers = new SparseArray<>();
    private final SparseArray<Long> downTime = new SparseArray<>();
    private final Handler handler = new Handler(Looper.getMainLooper());

    private boolean shiftLatched, cbmLatched, ctrlLatched, shiftLock;
    private boolean modifiersSent;

    private final Paint bg = new Paint();
    private final Paint keyPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint smallPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

    private static final float UNITS_W = 18.0f;
    private static final float UNITS_H = 5.0f;

    public C64KeyboardView(Context context) {
        super(context);
        setFocusable(false);
        bg.setColor(Color.argb(70, 0, 0, 0));
        textPaint.setColor(Color.WHITE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setFakeBoldText(true);
        smallPaint.setColor(Color.argb(200, 230, 230, 230));
        smallPaint.setTextAlign(Paint.Align.CENTER);
        buildLayout();
    }

    /* --- layout ----------------------------------------------------------- */

    private float cx;   /* current x while building a row */

    private void k(String label, String shift, int row, int col, float y, float w) {
        keys.add(new Key(label, shift, row, col, K_NORMAL, cx, y, w, false));
        cx += w;
    }

    private void special(String label, int kind, int row, int col, float y, float w) {
        keys.add(new Key(label, null, row, col, kind, cx, y, w, false));
        cx += w;
    }

    private void fkey(String label, String shift, int row, int col, float y) {
        keys.add(new Key(label, shift, row, col, K_NORMAL, 16.5f, y, 1.5f, true));
    }

    private void buildLayout() {
        float y;

        /* row 1 */
        y = 0; cx = 0;
        k("←", null, 7, 1, y, 1);
        k("1", "!", 7, 0, y, 1);
        k("2", "\"", 7, 3, y, 1);
        k("3", "#", 1, 0, y, 1);
        k("4", "$", 1, 3, y, 1);
        k("5", "%", 2, 0, y, 1);
        k("6", "&", 2, 3, y, 1);
        k("7", "'", 3, 0, y, 1);
        k("8", "(", 3, 3, y, 1);
        k("9", ")", 4, 0, y, 1);
        k("0", null, 4, 3, y, 1);
        k("+", null, 5, 0, y, 1);
        k("-", null, 5, 3, y, 1);
        k("£", null, 6, 0, y, 1);
        k("HOME", "CLR", 6, 3, y, 1);
        k("DEL", "INST", 0, 0, y, 1);
        fkey("F1", "F2", 0, 4, y);

        /* row 2 */
        y = 1; cx = 0;
        special("CTRL", K_CTRL, 7, 2, y, 1.5f);
        k("Q", null, 7, 6, y, 1);
        k("W", null, 1, 1, y, 1);
        k("E", null, 1, 6, y, 1);
        k("R", null, 2, 1, y, 1);
        k("T", null, 2, 6, y, 1);
        k("Y", null, 3, 1, y, 1);
        k("U", null, 3, 6, y, 1);
        k("I", null, 4, 1, y, 1);
        k("O", null, 4, 6, y, 1);
        k("P", null, 5, 1, y, 1);
        k("@", null, 5, 6, y, 1);
        k("*", null, 6, 1, y, 1);
        k("↑", "π", 6, 6, y, 1);
        special("RESTORE", K_RESTORE, -1, -1, y, 1.5f);
        fkey("F3", "F4", 0, 5, y);

        /* row 3 */
        y = 2; cx = 0;
        k("R/S", null, 7, 7, y, 1);
        special("LOCK", K_SHIFTLOCK, ROW_LSHIFT, COL_LSHIFT, y, 1);
        k("A", null, 1, 2, y, 1);
        k("S", null, 1, 5, y, 1);
        k("D", null, 2, 2, y, 1);
        k("F", null, 2, 5, y, 1);
        k("G", null, 3, 2, y, 1);
        k("H", null, 3, 5, y, 1);
        k("J", null, 4, 2, y, 1);
        k("K", null, 4, 5, y, 1);
        k("L", null, 5, 2, y, 1);
        k(":", "[", 5, 5, y, 1);
        k(";", "]", 6, 2, y, 1);
        k("=", null, 6, 5, y, 1);
        k("RETURN", null, 0, 1, y, 2);
        fkey("F5", "F6", 0, 6, y);

        /* row 4 */
        y = 3; cx = 0;
        special("C=", K_CBM, 7, 5, y, 1);
        special("SHIFT", K_SHIFT, 1, 7, y, 1.5f);
        k("Z", null, 1, 4, y, 1);
        k("X", null, 2, 7, y, 1);
        k("C", null, 2, 4, y, 1);
        k("V", null, 3, 7, y, 1);
        k("B", null, 3, 4, y, 1);
        k("N", null, 4, 7, y, 1);
        k("M", null, 4, 4, y, 1);
        k(",", "<", 5, 7, y, 1);
        k(".", ">", 5, 4, y, 1);
        k("/", "?", 6, 7, y, 1);
        special("SHIFT", K_SHIFT, 6, 4, y, 1.5f);
        k("↓", "↑", 0, 7, y, 1);
        k("→", "←", 0, 2, y, 1);
        fkey("F7", "F8", 0, 3, y);

        /* row 5 */
        y = 4; cx = 3.5f;
        k("SPACE", null, 7, 4, y, 9);
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        float uw = w / UNITS_W;
        float uh = h / UNITS_H;
        float gap = Math.min(uw, uh) * 0.08f;
        for (Key key : keys) {
            key.rect.set(key.x * uw + gap, key.y * uh + gap,
                         (key.x + key.w) * uw - gap, (key.y + 1) * uh - gap);
        }
        textPaint.setTextSize(Math.min(uh * 0.36f, uw * 0.42f));
        smallPaint.setTextSize(Math.min(uh * 0.24f, uw * 0.28f));
    }

    /* --- drawing ------------------------------------------------------------ */

    private boolean isLit(Key key) {
        switch (key.kind) {
            case K_SHIFT: return shiftLatched;
            case K_CBM: return cbmLatched;
            case K_CTRL: return ctrlLatched;
            case K_SHIFTLOCK: return shiftLock;
            default: return false;
        }
    }

    @Override
    protected void onDraw(Canvas c) {
        c.drawRect(0, 0, getWidth(), getHeight(), bg);
        for (Key key : keys) {
            int color;
            if (key.down) {
                color = Color.argb(235, 255, 255, 255);
            } else if (isLit(key)) {
                color = Color.argb(220, 255, 190, 60);
            } else if (key.function) {
                color = Color.argb(150, 190, 110, 50);    /* the orange F keys */
            } else if (key.kind != K_NORMAL || key.label.length() > 2) {
                color = Color.argb(150, 70, 70, 80);
            } else {
                color = Color.argb(140, 75, 60, 50);      /* C64 brown */
            }
            keyPaint.setColor(color);
            float r = key.rect.height() * 0.15f;
            c.drawRoundRect(key.rect, r, r, keyPaint);

            String label = key.label;
            boolean shifted = shiftLatched || shiftLock;
            if (shifted && key.shiftLabel != null) {
                label = key.shiftLabel;
            }
            Paint p = label.length() > 2 ? smallPaint : textPaint;
            p.setColor(key.down ? Color.BLACK : Color.WHITE);
            float ty = key.rect.centerY() - (p.descent() + p.ascent()) / 2f;
            c.drawText(label, key.rect.centerX(), ty, p);
        }
    }

    /* --- touch -------------------------------------------------------------- */

    private Key keyAt(float x, float y) {
        for (Key key : keys) {
            if (key.rect.contains(x, y)) {
                return key;
            }
        }
        return null;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int index = e.getActionIndex();
        int id = e.getPointerId(index);

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                Key key = keyAt(e.getX(index), e.getY(index));
                if (key != null) {
                    pointers.put(id, key);
                    press(key);
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                Key key = pointers.get(id);
                pointers.remove(id);
                if (key != null) {
                    release(key);
                }
                return true;
            }
            case MotionEvent.ACTION_CANCEL:
                for (int i = 0; i < pointers.size(); i++) {
                    release(pointers.valueAt(i));
                }
                pointers.clear();
                return true;
            default:
                /* keys do not follow a sliding finger */
                return true;
        }
    }

    private void matrix(int row, int col, boolean down) {
        try {
            EmuBridge.nativeKey(row, col, down);
        } catch (UnsatisfiedLinkError err) {
            /* native library not loaded */
        }
    }

    private void press(Key key) {
        performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
        switch (key.kind) {
            case K_SHIFT:
                shiftLatched = !shiftLatched;
                if (!shiftLock) {
                    matrix(ROW_LSHIFT, COL_LSHIFT, shiftLatched);
                }
                break;
            case K_CBM:
                cbmLatched = !cbmLatched;
                matrix(key.row, key.col, cbmLatched);
                break;
            case K_CTRL:
                ctrlLatched = !ctrlLatched;
                matrix(key.row, key.col, ctrlLatched);
                break;
            case K_SHIFTLOCK:
                shiftLock = !shiftLock;
                if (!shiftLatched) {
                    matrix(ROW_LSHIFT, COL_LSHIFT, shiftLock);
                }
                break;
            case K_RESTORE:
                key.down = true;
                downTime.put(System.identityHashCode(key), SystemClock.uptimeMillis());
                try {
                    EmuBridge.nativeRestore(true);
                } catch (UnsatisfiedLinkError err) {
                    /* ignore */
                }
                break;
            default:
                key.down = true;
                downTime.put(System.identityHashCode(key), SystemClock.uptimeMillis());
                modifiersSent = shiftLatched || cbmLatched || ctrlLatched;
                matrix(key.row, key.col, true);
                break;
        }
        invalidate();
    }

    private void release(final Key key) {
        if (key.kind != K_NORMAL && key.kind != K_RESTORE) {
            return;     /* sticky keys change on press only */
        }
        Long t = downTime.get(System.identityHashCode(key));
        long held = t == null ? MIN_HOLD_MS : SystemClock.uptimeMillis() - t;
        long delay = Math.max(0, MIN_HOLD_MS - held);

        handler.postDelayed(new Runnable() {
            @Override
            public void run() {
                key.down = false;
                if (key.kind == K_RESTORE) {
                    try {
                        EmuBridge.nativeRestore(false);
                    } catch (UnsatisfiedLinkError err) {
                        /* ignore */
                    }
                } else {
                    matrix(key.row, key.col, false);
                    if (modifiersSent) {
                        releaseModifiersLater();
                    }
                }
                invalidate();
            }
        }, delay);
    }

    /* sticky modifiers are released a moment after the key they modified */
    private void releaseModifiersLater() {
        modifiersSent = false;
        handler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (shiftLatched) {
                    shiftLatched = false;
                    if (!shiftLock) {
                        matrix(ROW_LSHIFT, COL_LSHIFT, false);
                    }
                }
                if (cbmLatched) {
                    cbmLatched = false;
                    matrix(7, 5, false);
                }
                if (ctrlLatched) {
                    ctrlLatched = false;
                    matrix(7, 2, false);
                }
                invalidate();
            }
        }, 30);
    }

    /** release everything (keyboard hidden) */
    public void releaseAll() {
        for (Key key : keys) {
            if (key.down) {
                key.down = false;
                if (key.kind == K_RESTORE) {
                    try {
                        EmuBridge.nativeRestore(false);
                    } catch (UnsatisfiedLinkError err) {
                        /* ignore */
                    }
                } else {
                    matrix(key.row, key.col, false);
                }
            }
        }
        pointers.clear();
        if (shiftLatched || shiftLock) {
            matrix(ROW_LSHIFT, COL_LSHIFT, false);
        }
        if (cbmLatched) {
            matrix(7, 5, false);
        }
        if (ctrlLatched) {
            matrix(7, 2, false);
        }
        shiftLatched = cbmLatched = ctrlLatched = shiftLock = false;
        invalidate();
    }
}
