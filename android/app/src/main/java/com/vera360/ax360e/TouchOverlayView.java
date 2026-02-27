package com.vera360.ax360e;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

/**
 * Touch overlay for virtual Xbox 360 controller.
 * Draws semi-transparent D-pad, ABXY buttons, sticks, triggers.
 */
public class TouchOverlayView extends View {

    private final Paint paintButton = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint paintText   = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint paintStick  = new Paint(Paint.ANTI_ALIAS_FLAG);

    // Left stick state
    private float leftStickX = 0f, leftStickY = 0f;
    private boolean leftStickActive = false;
    private int leftStickPointerId = -1;

    // Right stick state  
    private float rightStickX = 0f, rightStickY = 0f;
    private boolean rightStickActive = false;
    private int rightStickPointerId = -1;

    // Button regions
    private final RectF btnA = new RectF();
    private final RectF btnB = new RectF();
    private final RectF btnX = new RectF();
    private final RectF btnY = new RectF();
    private final RectF dpadUp = new RectF();
    private final RectF dpadDown = new RectF();
    private final RectF dpadLeft = new RectF();
    private final RectF dpadRight = new RectF();
    private final RectF btnStart = new RectF();
    private final RectF btnBack = new RectF();
    private final RectF btnLB = new RectF();
    private final RectF btnRB = new RectF();

    private int activeButtons = 0;

    // Button mask constants (matching Xenia HID)
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

    public TouchOverlayView(Context context) {
        super(context);
        init();
    }

    public TouchOverlayView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public TouchOverlayView(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
        init();
    }

    private void init() {
        paintButton.setColor(Color.argb(80, 255, 255, 255));
        paintButton.setStyle(Paint.Style.FILL);

        paintText.setColor(Color.argb(180, 255, 255, 255));
        paintText.setTextSize(32f);
        paintText.setTextAlign(Paint.Align.CENTER);

        paintStick.setColor(Color.argb(100, 120, 200, 255));
        paintStick.setStyle(Paint.Style.FILL);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldW, int oldH) {
        super.onSizeChanged(w, h, oldW, oldH);
        float unit = Math.min(w, h) / 10f;

        // D-pad (left side)
        float dpadCx = unit * 2.5f;
        float dpadCy = h - unit * 3f;
        float bs = unit * 0.7f;
        dpadUp.set(dpadCx - bs, dpadCy - bs*3, dpadCx + bs, dpadCy - bs);
        dpadDown.set(dpadCx - bs, dpadCy + bs, dpadCx + bs, dpadCy + bs*3);
        dpadLeft.set(dpadCx - bs*3, dpadCy - bs, dpadCx - bs, dpadCy + bs);
        dpadRight.set(dpadCx + bs, dpadCy - bs, dpadCx + bs*3, dpadCy + bs);

        // ABXY (right side)
        float abxyCx = w - unit * 2.5f;
        float abxyCy = h - unit * 3f;
        float abr = unit * 0.6f;
        btnA.set(abxyCx - abr, abxyCy + abr, abxyCx + abr, abxyCy + abr*3);
        btnB.set(abxyCx + abr, abxyCy - abr, abxyCx + abr*3, abxyCy + abr);
        btnX.set(abxyCx - abr*3, abxyCy - abr, abxyCx - abr, abxyCy + abr);
        btnY.set(abxyCx - abr, abxyCy - abr*3, abxyCx + abr, abxyCy - abr);

        // Start / Back
        float midX = w / 2f;
        btnBack.set(midX - unit*2, h - unit*1.5f, midX - unit*0.5f, h - unit*0.5f);
        btnStart.set(midX + unit*0.5f, h - unit*1.5f, midX + unit*2, h - unit*0.5f);

        // Shoulder buttons
        btnLB.set(unit*0.5f, unit*0.3f, unit*3f, unit*1.3f);
        btnRB.set(w - unit*3f, unit*0.3f, w - unit*0.5f, unit*1.3f);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float w = getWidth();
        float h = getHeight();
        float unit = Math.min(w, h) / 10f;

        // D-pad
        canvas.drawRoundRect(dpadUp, 8, 8, paintButton);
        canvas.drawRoundRect(dpadDown, 8, 8, paintButton);
        canvas.drawRoundRect(dpadLeft, 8, 8, paintButton);
        canvas.drawRoundRect(dpadRight, 8, 8, paintButton);

        // ABXY
        canvas.drawOval(btnA, paintButton);
        canvas.drawOval(btnB, paintButton);
        canvas.drawOval(btnX, paintButton);
        canvas.drawOval(btnY, paintButton);
        canvas.drawText("A", btnA.centerX(), btnA.centerY() + 10, paintText);
        canvas.drawText("B", btnB.centerX(), btnB.centerY() + 10, paintText);
        canvas.drawText("X", btnX.centerX(), btnX.centerY() + 10, paintText);
        canvas.drawText("Y", btnY.centerX(), btnY.centerY() + 10, paintText);

        // Start / Back
        canvas.drawRoundRect(btnStart, 12, 12, paintButton);
        canvas.drawRoundRect(btnBack, 12, 12, paintButton);
        canvas.drawText("≡", btnStart.centerX(), btnStart.centerY() + 10, paintText);
        canvas.drawText("⊞", btnBack.centerX(), btnBack.centerY() + 10, paintText);

        // Shoulder buttons
        canvas.drawRoundRect(btnLB, 12, 12, paintButton);
        canvas.drawRoundRect(btnRB, 12, 12, paintButton);
        canvas.drawText("LB", btnLB.centerX(), btnLB.centerY() + 10, paintText);
        canvas.drawText("RB", btnRB.centerX(), btnRB.centerY() + 10, paintText);

        // Left analog stick zone
        float lsCx = unit * 2.5f;
        float lsCy = h - unit * 6.5f;
        canvas.drawCircle(lsCx, lsCy, unit * 1.2f, paintButton);
        if (leftStickActive) {
            canvas.drawCircle(lsCx + leftStickX * unit, lsCy + leftStickY * unit, unit * 0.5f, paintStick);
        } else {
            canvas.drawCircle(lsCx, lsCy, unit * 0.5f, paintStick);
        }

        // Right analog stick zone
        float rsCx = w - unit * 2.5f;
        float rsCy = h - unit * 6.5f;
        canvas.drawCircle(rsCx, rsCy, unit * 1.2f, paintButton);
        if (rightStickActive) {
            canvas.drawCircle(rsCx + rightStickX * unit, rsCy + rightStickY * unit, unit * 0.5f, paintStick);
        } else {
            canvas.drawCircle(rsCx, rsCy, unit * 0.5f, paintStick);
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int pointerCount = event.getPointerCount();
        activeButtons = 0;

        for (int i = 0; i < pointerCount; i++) {
            float x = event.getX(i);
            float y = event.getY(i);
            int action = event.getActionMasked();

            // Check buttons
            if (btnA.contains(x, y)) activeButtons |= BTN_A;
            if (btnB.contains(x, y)) activeButtons |= BTN_B;
            if (btnX.contains(x, y)) activeButtons |= BTN_X;
            if (btnY.contains(x, y)) activeButtons |= BTN_Y;
            if (dpadUp.contains(x, y)) activeButtons |= BTN_DPAD_U;
            if (dpadDown.contains(x, y)) activeButtons |= BTN_DPAD_D;
            if (dpadLeft.contains(x, y)) activeButtons |= BTN_DPAD_L;
            if (dpadRight.contains(x, y)) activeButtons |= BTN_DPAD_R;
            if (btnStart.contains(x, y)) activeButtons |= BTN_START;
            if (btnBack.contains(x, y)) activeButtons |= BTN_BACK;
            if (btnLB.contains(x, y)) activeButtons |= BTN_LB;
            if (btnRB.contains(x, y)) activeButtons |= BTN_RB;
        }

        // Send to native
        NativeBridge.onControllerInput(activeButtons, leftStickX, leftStickY, rightStickX, rightStickY);

        invalidate();
        return true;
    }
}
