/*
 * PadButton.java - round or rounded-rectangle touch button
 *
 * Reports press and release separately (needed for the fire button) and
 * never takes the keyboard focus away from the SDL surface.
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
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

public class PadButton extends View {

    public interface Listener {
        void onPad(PadButton b, boolean down);
    }

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private final boolean round;
    private final int color;
    private String label;
    private boolean pressed = false;
    private boolean active = false;     /* lit (toggle state) */
    private int pointerId = -1;
    private Listener listener;

    public PadButton(Context context, String label, int color, boolean round) {
        super(context);
        this.label = label;
        this.color = color;
        this.round = round;
        setFocusable(false);
        text.setColor(Color.WHITE);
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
    }

    public void setListener(Listener l) {
        listener = l;
    }

    public void setLabel(String l) {
        label = l;
        invalidate();
    }

    public void setActive(boolean a) {
        active = a;
        invalidate();
    }

    @Override
    protected void onDraw(Canvas c) {
        float w = getWidth(), h = getHeight();
        float pad = Math.min(w, h) * 0.06f;
        int a = pressed ? 240 : (active ? 220 : 150);
        int col = pressed ? Color.WHITE : color;

        fill.setColor(Color.argb(a, Color.red(col), Color.green(col), Color.blue(col)));
        if (round) {
            float r = Math.min(w, h) / 2f - pad;
            c.drawCircle(w / 2f, h / 2f, r, fill);
        } else {
            rect.set(pad, pad, w - pad, h - pad);
            float r = Math.min(w, h) * 0.22f;
            c.drawRoundRect(rect, r, r, fill);
        }
        if (active && !pressed) {
            fill.setColor(Color.argb(255, 255, 220, 80));
            float s = Math.min(w, h) * 0.07f;
            c.drawCircle(w - pad * 2 - s, pad * 2 + s, s, fill);
        }

        float size = Math.min(h * 0.36f, w * 0.9f / Math.max(3, label.length()) * 1.7f);
        text.setTextSize(size);
        text.setColor(pressed ? Color.BLACK : Color.WHITE);
        float y = h / 2f - (text.descent() + text.ascent()) / 2f;
        c.drawText(label, w / 2f, y, text);
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int index = e.getActionIndex();

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                if (pointerId == -1) {
                    pointerId = e.getPointerId(index);
                    setPressedState(true);
                }
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                if (e.getPointerId(index) == pointerId) {
                    pointerId = -1;
                    setPressedState(false);
                }
                return true;
            case MotionEvent.ACTION_CANCEL:
                pointerId = -1;
                setPressedState(false);
                return true;
            default:
                return true;
        }
    }

    private void setPressedState(boolean p) {
        if (p != pressed) {
            pressed = p;
            if (p) {
                performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            }
            invalidate();
            if (listener != null) {
                listener.onPad(this, p);
            }
        }
    }
}
