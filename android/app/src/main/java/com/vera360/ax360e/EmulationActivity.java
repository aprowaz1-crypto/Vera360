package com.vera360.ax360e;

import android.net.Uri;
import android.os.Bundle;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.FrameLayout;
import androidx.appcompat.app.AppCompatActivity;

/**
 * ax360e Emulation Activity — fullscreen Vulkan rendering surface.
 * Hosts SurfaceView and touch overlay for virtual gamepad.
 */
public class EmulationActivity extends AppCompatActivity implements SurfaceHolder.Callback {

    private static final String TAG = "ax360e:Emu";

    private SurfaceView surfaceView;
    private TouchOverlayView touchOverlay;
    private boolean nativeRunning = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_emulation);

        // Go immersive fullscreen
        goFullscreen();

        surfaceView = findViewById(R.id.surface_view);
        surfaceView.getHolder().addCallback(this);

        touchOverlay = findViewById(R.id.touch_overlay);

        Uri gameUri = getIntent().getData();
        if (gameUri != null) {
            // Resolve content:// URI to file descriptor
            NativeBridge.setGameUri(gameUri.toString());
        }
    }

    private void goFullscreen() {
        View decorView = getWindow().getDecorView();
        WindowInsetsController controller = decorView.getWindowInsetsController();
        if (controller != null) {
            controller.hide(WindowInsets.Type.systemBars());
            controller.setSystemBarsBehavior(
                WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }
    }

    // ── SurfaceHolder.Callback ──────────────────────────────────────────────

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        // Pass native window to Vulkan renderer
        Surface surface = holder.getSurface();
        NativeBridge.surfaceCreated(surface);
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        NativeBridge.surfaceChanged(width, height);
        if (!nativeRunning) {
            nativeRunning = true;
            NativeBridge.startEmulation();
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        NativeBridge.surfaceDestroyed();
        nativeRunning = false;
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (nativeRunning) {
            NativeBridge.pause();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        goFullscreen();
        if (nativeRunning) {
            NativeBridge.resume();
        }
    }

    @Override
    protected void onDestroy() {
        NativeBridge.shutdown();
        super.onDestroy();
    }
}
