/*
 * JoystickView.java - touch joystick (8 directions)
 *
 * The whole view reacts to touches: the direction is measured from the
 * centre of the view, so the thumb can start anywhere.
 *
 * This file is part of C64-EmuDis (GPL-2.0-or-later).
 * Author: IT-Dummkopfen
 */

package io.github.itdummkopfen.c64emudis;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

public class JoystickView extends View {

    private final Paint basePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ringPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint knobPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint arrowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint arrowOnPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path arrow = new Path();

    private int pointerId = -1;
    private float knobX, knobY;     /* relative to the centre */
    private int bits = 0;

    public JoystickView(Context context) {
        super(context);
        setFocusable(false);
        basePaint.setColor(Color.argb(150, 40, 40, 70));
        ringPaint.setColor(Color.argb(200, 120, 120, 200));
        ringPaint.setStyle(Paint.Style.STROKE);
        knobPaint.setColor(Color.argb(230, 200, 30, 30));
        arrowPaint.setColor(Color.argb(140, 200, 200, 255));
        arrowOnPaint.setColor(Color.argb(255, 255, 255, 255));
    }

    private float radius() {
        return Math.min(getWidth(), getHeight()) * 0.42f;
    }

    @Override
    protected void onDraw(Canvas c) {
        float cx = getWidth() / 2f, cy = getHeight() / 2f, r = radius();

        ringPaint.setStrokeWidth(r * 0.04f);
        c.drawCircle(cx, cy, r, basePaint);
        c.drawCircle(cx, cy, r, ringPaint);

        /* four arrows, lit when that direction is active */
        drawArrow(c, cx, cy, r, 0, (bits & EmuBridge.JOY_UP) != 0);
        drawArrow(c, cx, cy, r, 90, (bits & EmuBridge.JOY_RIGHT) != 0);
        drawArrow(c, cx, cy, r, 180, (bits & EmuBridge.JOY_DOWN) != 0);
        drawArrow(c, cx, cy, r, 270, (bits & EmuBridge.JOY_LEFT) != 0);

        c.drawCircle(cx + knobX, cy + knobY, r * 0.38f, knobPaint);
    }

    private void drawArrow(Canvas c, float cx, float cy, float r, float angle, boolean on) {
        float s = r * 0.14f;
        arrow.reset();
        arrow.moveTo(0, -r * 0.88f);
        arrow.lineTo(-s, -r * 0.88f + s * 1.2f);
        arrow.lineTo(s, -r * 0.88f + s * 1.2f);
        arrow.close();
        c.save();
        c.translate(cx, cy);
        c.rotate(angle);
        c.drawPath(arrow, on ? arrowOnPaint : arrowPaint);
        c.restore();
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
                    update(e.getX(index), e.getY(index));
                }
                return true;
            case MotionEvent.ACTION_MOVE:
                if (pointerId != -1) {
                    int i = e.findPointerIndex(pointerId);
                    if (i >= 0) {
                        update(e.getX(i), e.getY(i));
                    }
                }
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL:
                if (action == MotionEvent.ACTION_CANCEL || e.getPointerId(index) == pointerId) {
                    pointerId = -1;
                    knobX = knobY = 0;
                    setBits(0);
                    invalidate();
                }
                return true;
            default:
                return true;
        }
    }

    private void update(float x, float y) {
        float r = radius();
        float dx = x - getWidth() / 2f;
        float dy = y - getHeight() / 2f;
        float d = (float) Math.hypot(dx, dy);
        int b = 0;

        /* knob follows the finger, limited to the ring */
        float max = r * 0.62f;
        if (d > max) {
            knobX = dx / d * max;
            knobY = dy / d * max;
        } else {
            knobX = dx;
            knobY = dy;
        }

        if (d > r * 0.22f) {
            /* 8 sectors of 45 degrees: 0 = right, counter-clockwise */
            double a = Math.toDegrees(Math.atan2(-dy, dx));
            if (a < 0) {
                a += 360;
            }
            int sector = (int) Math.floor((a + 22.5) / 45.0) % 8;
            switch (sector) {
                case 0: b = EmuBridge.JOY_RIGHT; break;
                case 1: b = EmuBridge.JOY_RIGHT | EmuBridge.JOY_UP; break;
                case 2: b = EmuBridge.JOY_UP; break;
                case 3: b = EmuBridge.JOY_UP | EmuBridge.JOY_LEFT; break;
                case 4: b = EmuBridge.JOY_LEFT; break;
                case 5: b = EmuBridge.JOY_LEFT | EmuBridge.JOY_DOWN; break;
                case 6: b = EmuBridge.JOY_DOWN; break;
                default: b = EmuBridge.JOY_DOWN | EmuBridge.JOY_RIGHT; break;
            }
        }
        setBits(b);
        invalidate();
    }

    private void setBits(int b) {
        if (b != bits) {
            if (b != 0) {
                performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK);
            }
            bits = b;
            EmuBridge.setStick(b);
        }
    }
}
