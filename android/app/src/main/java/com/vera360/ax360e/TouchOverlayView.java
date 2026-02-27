package com.vera360.ax360e;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Shader;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.util.AttributeSet;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

/**
 * Professional Xbox 360 touch controller overlay.
 * - Working analog sticks with dead zone
 * - LT / RT triggers
 * - Haptic feedback on press
 * - Scaled for all screen sizes
 */
public class TouchOverlayView extends View {

    // ── Paints ──────────────────────────────────────────────────────────
    private final Paint pBtnIdle    = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pBtnActive  = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pText       = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pStickBg    = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pStickThumb = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pTrigger    = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pDpadBg     = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pDpadDir    = new Paint(Paint.ANTI_ALIAS_FLAG);

    // ── Left stick ──────────────────────────────────────────────────────
    private float lsCenterX, lsCenterY, stickRadius, thumbRadius;
    private float leftStickX = 0f, leftStickY = 0f;
    private int leftStickPointerId = -1;

    // ── Right stick ─────────────────────────────────────────────────────
    private float rsCenterX, rsCenterY;
    private float rightStickX = 0f, rightStickY = 0f;
    private int rightStickPointerId = -1;

    // ── Button regions ──────────────────────────────────────────────────
    private final RectF btnA = new RectF(), btnB = new RectF();
    private final RectF btnX = new RectF(), btnY = new RectF();
    private final RectF dpadUp = new RectF(), dpadDown = new RectF();
    private final RectF dpadLeft = new RectF(), dpadRight = new RectF();
    private final RectF btnStart = new RectF(), btnBack = new RectF();
    private final RectF btnLB = new RectF(), btnRB = new RectF();
    private final RectF btnLT = new RectF(), btnRT = new RectF();

    private int activeButtons = 0;
    private int prevButtons = 0;
    private float ltValue = 0f, rtValue = 0f;

    // Java → C++ button bits (translated in jni_bridge to XINPUT)
    public static final int BTN_A      = 1;
    public static final int BTN_B      = 1 << 1;
    public static final int BTN_X      = 1 << 2;
    public static final int BTN_Y      = 1 << 3;
    public static final int BTN_DPAD_U = 1 << 4;
    public static final int BTN_DPAD_D = 1 << 5;
    public static final int BTN_DPAD_L = 1 << 6;
    public static final int BTN_DPAD_R = 1 << 7;
    public static final int BTN_START  = 1 << 8;
    public static final int BTN_BACK   = 1 << 9;
    public static final int BTN_LB     = 1 << 10;
    public static final int BTN_RB     = 1 << 11;
    public static final int BTN_LT     = 1 << 12;
    public static final int BTN_RT     = 1 << 13;
    public static final int BTN_LS     = 1 << 14;  // Left Stick Click
    public static final int BTN_RS     = 1 << 15;  // Right Stick Click

    private static final float DEAD_ZONE = 0.15f;

    private Vibrator vibrator;

    public TouchOverlayView(Context ctx) { super(ctx); setup(); }
    public TouchOverlayView(Context ctx, AttributeSet a) { super(ctx, a); setup(); }
    public TouchOverlayView(Context ctx, AttributeSet a, int d) { super(ctx, a, d); setup(); }

    private void setup() {
        pBtnIdle.setColor(Color.argb(50, 255, 255, 255));
        pBtnIdle.setStyle(Paint.Style.FILL);
        pBtnActive.setColor(Color.argb(120, 22, 196, 127));
        pBtnActive.setStyle(Paint.Style.FILL);
        pText.setColor(Color.argb(200, 255, 255, 255));
        pText.setTextAlign(Paint.Align.CENTER);
        pStickBg.setColor(Color.argb(35, 255, 255, 255));
        pStickBg.setStyle(Paint.Style.FILL);
        pStickThumb.setColor(Color.argb(140, 22, 196, 127));
        pStickThumb.setStyle(Paint.Style.FILL);
        pTrigger.setColor(Color.argb(70, 255, 160, 0));
        pTrigger.setStyle(Paint.Style.FILL);
        pDpadBg.setColor(Color.argb(30, 255, 255, 255));
        pDpadBg.setStyle(Paint.Style.FILL);
        pDpadDir.setColor(Color.argb(60, 255, 255, 255));
        pDpadDir.setStyle(Paint.Style.FILL);

        vibrator = (Vibrator) getContext().getSystemService(Context.VIBRATOR_SERVICE);
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        super.onSizeChanged(w, h, ow, oh);
        float u = Math.min(w, h) / 10f;
        float textSz = u * 0.45f;
        pText.setTextSize(textSz);

        // ── D-pad (left bottom) ─────────────────────────────────────────
        float dCx = u * 2.2f, dCy = h - u * 2.5f, bs = u * 0.65f;
        dpadUp.set(dCx - bs, dCy - bs * 3, dCx + bs, dCy - bs);
        dpadDown.set(dCx - bs, dCy + bs, dCx + bs, dCy + bs * 3);
        dpadLeft.set(dCx - bs * 3, dCy - bs, dCx - bs, dCy + bs);
        dpadRight.set(dCx + bs, dCy - bs, dCx + bs * 3, dCy + bs);

        // ── ABXY (right bottom, diamond layout) ─────────────────────────
        float aCx = w - u * 2.2f, aCy = h - u * 2.5f, ar = u * 0.55f;
        float sp = ar * 2.2f;
        btnA.set(aCx - ar, aCy + sp - ar, aCx + ar, aCy + sp + ar);
        btnB.set(aCx + sp - ar, aCy - ar, aCx + sp + ar, aCy + ar);
        btnX.set(aCx - sp - ar, aCy - ar, aCx - sp + ar, aCy + ar);
        btnY.set(aCx - ar, aCy - sp - ar, aCx + ar, aCy - sp + ar);

        // ── Start / Back (center bottom) ────────────────────────────────
        float midX = w / 2f;
        float sbW = u * 1.2f, sbH = u * 0.6f;
        btnBack.set(midX - sbW * 1.2f - sbW, h - u * 1.4f - sbH,
                    midX - sbW * 1.2f + sbW, h - u * 1.4f + sbH);
        btnStart.set(midX + sbW * 0.2f, h - u * 1.4f - sbH,
                     midX + sbW * 0.2f + sbW * 2, h - u * 1.4f + sbH);

        // ── Shoulder buttons (top) ──────────────────────────────────────
        btnLB.set(u * 0.5f, u * 0.3f, u * 3.2f, u * 1.2f);
        btnRB.set(w - u * 3.2f, u * 0.3f, w - u * 0.5f, u * 1.2f);

        // ── Triggers (above shoulders) ──────────────────────────────────
        btnLT.set(u * 0.5f, u * 1.5f, u * 3.2f, u * 2.4f);
        btnRT.set(w - u * 3.2f, u * 1.5f, w - u * 0.5f, u * 2.4f);

        // ── Analog sticks ───────────────────────────────────────────────
        stickRadius = u * 1.3f;
        thumbRadius = u * 0.45f;
        lsCenterX = u * 2.2f;
        lsCenterY = h - u * 5.8f;
        rsCenterX = w - u * 2.2f;
        rsCenterY = h - u * 5.8f;
    }

    @Override
    protected void onDraw(Canvas c) {
        super.onDraw(c);

        // ── D-pad background + directions ───────────────────────────────
        drawBtn(c, dpadUp, (activeButtons & BTN_DPAD_U) != 0, "▲");
        drawBtn(c, dpadDown, (activeButtons & BTN_DPAD_D) != 0, "▼");
        drawBtn(c, dpadLeft, (activeButtons & BTN_DPAD_L) != 0, "◀");
        drawBtn(c, dpadRight, (activeButtons & BTN_DPAD_R) != 0, "▶");

        // ── ABXY ────────────────────────────────────────────────────────
        drawCircleBtn(c, btnA, (activeButtons & BTN_A) != 0, "A",
                      Color.argb(180, 96, 195, 80));
        drawCircleBtn(c, btnB, (activeButtons & BTN_B) != 0, "B",
                      Color.argb(180, 220, 60, 60));
        drawCircleBtn(c, btnX, (activeButtons & BTN_X) != 0, "X",
                      Color.argb(180, 80, 140, 220));
        drawCircleBtn(c, btnY, (activeButtons & BTN_Y) != 0, "Y",
                      Color.argb(180, 230, 190, 40));

        // ── Start / Back ────────────────────────────────────────────────
        drawBtn(c, btnStart, (activeButtons & BTN_START) != 0, "START");
        drawBtn(c, btnBack, (activeButtons & BTN_BACK) != 0, "BACK");

        // ── Shoulders ───────────────────────────────────────────────────
        drawBtn(c, btnLB, (activeButtons & BTN_LB) != 0, "LB");
        drawBtn(c, btnRB, (activeButtons & BTN_RB) != 0, "RB");

        // ── Triggers ────────────────────────────────────────────────────
        drawTrigger(c, btnLT, ltValue, "LT");
        drawTrigger(c, btnRT, rtValue, "RT");

        // ── Left Stick ──────────────────────────────────────────────────
        c.drawCircle(lsCenterX, lsCenterY, stickRadius, pStickBg);
        float lx = lsCenterX + leftStickX * stickRadius * 0.7f;
        float ly = lsCenterY + leftStickY * stickRadius * 0.7f;
        pStickThumb.setColor(leftStickPointerId >= 0
            ? Color.argb(200, 22, 196, 127)
            : Color.argb(100, 22, 196, 127));
        c.drawCircle(lx, ly, thumbRadius, pStickThumb);

        // ── Right Stick ─────────────────────────────────────────────────
        c.drawCircle(rsCenterX, rsCenterY, stickRadius, pStickBg);
        float rx = rsCenterX + rightStickX * stickRadius * 0.7f;
        float ry = rsCenterY + rightStickY * stickRadius * 0.7f;
        pStickThumb.setColor(rightStickPointerId >= 0
            ? Color.argb(200, 22, 196, 127)
            : Color.argb(100, 22, 196, 127));
        c.drawCircle(rx, ry, thumbRadius, pStickThumb);
    }

    private void drawBtn(Canvas c, RectF r, boolean active, String label) {
        c.drawRoundRect(r, 10, 10, active ? pBtnActive : pBtnIdle);
        float ty = r.centerY() - (pText.descent() + pText.ascent()) / 2;
        c.drawText(label, r.centerX(), ty, pText);
    }

    private void drawCircleBtn(Canvas c, RectF r, boolean active,
                                String label, int color) {
        Paint p = active ? pBtnActive : pBtnIdle;
        if (!active) {
            p = new Paint(pBtnIdle);
            p.setColor(Color.argb(50, Color.red(color),
                                  Color.green(color), Color.blue(color)));
        }
        float cx = r.centerX(), cy = r.centerY();
        float rad = Math.min(r.width(), r.height()) / 2f;
        c.drawCircle(cx, cy, rad, p);
        float ty = cy - (pText.descent() + pText.ascent()) / 2;
        c.drawText(label, cx, ty, pText);
    }

    private void drawTrigger(Canvas c, RectF r, float value, String label) {
        c.drawRoundRect(r, 8, 8, pBtnIdle);
        if (value > 0f) {
            RectF fill = new RectF(r.left, r.top,
                                   r.left + r.width() * value, r.bottom);
            c.drawRoundRect(fill, 8, 8, pTrigger);
        }
        float ty = r.centerY() - (pText.descent() + pText.ascent()) / 2;
        c.drawText(label, r.centerX(), ty, pText);
    }

    // ── Touch handling ──────────────────────────────────────────────────
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int pointerIndex = event.getActionIndex();
        int pointerId = event.getPointerId(pointerIndex);

        // Handle stick pointer up
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            leftStickPointerId = -1;  leftStickX = 0; leftStickY = 0;
            rightStickPointerId = -1; rightStickX = 0; rightStickY = 0;
            activeButtons = 0; ltValue = 0; rtValue = 0;
            sendInput();
            invalidate();
            return true;
        }
        if (action == MotionEvent.ACTION_POINTER_UP) {
            if (pointerId == leftStickPointerId) {
                leftStickPointerId = -1; leftStickX = 0; leftStickY = 0;
            }
            if (pointerId == rightStickPointerId) {
                rightStickPointerId = -1; rightStickX = 0; rightStickY = 0;
            }
        }

        // Process all pointers
        activeButtons = 0;
        ltValue = 0; rtValue = 0;

        for (int i = 0; i < event.getPointerCount(); i++) {
            float x = event.getX(i);
            float y = event.getY(i);
            int pid = event.getPointerId(i);

            // ── Analog sticks ───────────────────────────────────────────
            float ldx = x - lsCenterX, ldy = y - lsCenterY;
            float lDist = (float) Math.sqrt(ldx * ldx + ldy * ldy);
            if (lDist < stickRadius * 1.5f && rightStickPointerId != pid) {
                if (leftStickPointerId < 0 || leftStickPointerId == pid) {
                    leftStickPointerId = pid;
                    float norm = Math.min(lDist / stickRadius, 1.0f);
                    if (norm < DEAD_ZONE) norm = 0;
                    leftStickX = (lDist > 0) ? (ldx / lDist) * norm : 0;
                    leftStickY = (lDist > 0) ? (ldy / lDist) * norm : 0;
                    continue;
                }
            }

            float rdx = x - rsCenterX, rdy = y - rsCenterY;
            float rDist = (float) Math.sqrt(rdx * rdx + rdy * rdy);
            if (rDist < stickRadius * 1.5f && leftStickPointerId != pid) {
                if (rightStickPointerId < 0 || rightStickPointerId == pid) {
                    rightStickPointerId = pid;
                    float norm = Math.min(rDist / stickRadius, 1.0f);
                    if (norm < DEAD_ZONE) norm = 0;
                    rightStickX = (rDist > 0) ? (rdx / rDist) * norm : 0;
                    rightStickY = (rDist > 0) ? (rdy / rDist) * norm : 0;
                    continue;
                }
            }

            // ── Buttons ─────────────────────────────────────────────────
            if (btnA.contains(x, y))      activeButtons |= BTN_A;
            if (btnB.contains(x, y))      activeButtons |= BTN_B;
            if (btnX.contains(x, y))      activeButtons |= BTN_X;
            if (btnY.contains(x, y))      activeButtons |= BTN_Y;
            if (dpadUp.contains(x, y))    activeButtons |= BTN_DPAD_U;
            if (dpadDown.contains(x, y))  activeButtons |= BTN_DPAD_D;
            if (dpadLeft.contains(x, y))  activeButtons |= BTN_DPAD_L;
            if (dpadRight.contains(x, y)) activeButtons |= BTN_DPAD_R;
            if (btnStart.contains(x, y))  activeButtons |= BTN_START;
            if (btnBack.contains(x, y))   activeButtons |= BTN_BACK;
            if (btnLB.contains(x, y))     activeButtons |= BTN_LB;
            if (btnRB.contains(x, y))     activeButtons |= BTN_RB;

            // Triggers → held = full press
            if (btnLT.contains(x, y)) { activeButtons |= BTN_LT; ltValue = 1.0f; }
            if (btnRT.contains(x, y)) { activeButtons |= BTN_RT; rtValue = 1.0f; }

            // D-pad diagonal support (between rects)
            float dCx = dpadUp.centerX(), dCy = (dpadUp.bottom + dpadDown.top) / 2;
            float ddx = x - dCx, ddy = y - dCy;
            float dDist = (float) Math.sqrt(ddx * ddx + ddy * ddy);
            float dSize = dpadUp.width() * 2.5f;
            if (dDist < dSize && dDist > dpadUp.width() * 0.3f) {
                if (ddy < -dpadUp.width() * 0.3f) activeButtons |= BTN_DPAD_U;
                if (ddy >  dpadUp.width() * 0.3f) activeButtons |= BTN_DPAD_D;
                if (ddx < -dpadUp.width() * 0.3f) activeButtons |= BTN_DPAD_L;
                if (ddx >  dpadUp.width() * 0.3f) activeButtons |= BTN_DPAD_R;
            }
        }

        // Haptic feedback on new press
        if ((activeButtons & ~prevButtons) != 0) {
            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY,
                HapticFeedbackConstants.FLAG_IGNORE_GLOBAL_SETTING);
        }
        prevButtons = activeButtons;

        sendInput();
        invalidate();
        return true;
    }

    private void sendInput() {
        NativeBridge.onControllerInput(activeButtons,
            leftStickX, leftStickY, rightStickX, rightStickY);
    }
}
