package com.vera360.ax360e;

import android.content.res.AssetFileDescriptor;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.view.Choreographer;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.TextView;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * ax360e Emulation Activity — fullscreen Vulkan rendering surface.
 * Resolves content:// URIs to real file paths for native code.
 * Drives the emulation loop via Choreographer for vsync-aligned ticks.
 */
public class EmulationActivity extends AppCompatActivity
        implements SurfaceHolder.Callback, Choreographer.FrameCallback {

    private static final String TAG = "ax360e:Emu";

    private SurfaceView surfaceView;
    private TouchOverlayView touchOverlay;
    private TextView tvLoading;
    private boolean nativeRunning = false;
    private volatile boolean emulationLoopActive = false;
    private String resolvedGamePath = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_emulation);

        goFullscreen();

        surfaceView  = findViewById(R.id.surface_view);
        touchOverlay = findViewById(R.id.touch_overlay);
        tvLoading    = findViewById(R.id.tv_loading);

        surfaceView.getHolder().addCallback(this);

        // Show loading while we resolve the URI
        if (tvLoading != null) {
            tvLoading.setVisibility(View.VISIBLE);
            touchOverlay.setVisibility(View.GONE);
        }

        // Resolve content:// URI to a real file path in background
        Uri gameUri = getIntent().getData();
        if (gameUri != null) {
            new Thread(() -> {
                resolvedGamePath = resolveUriToPath(gameUri);
                runOnUiThread(() -> {
                    if (resolvedGamePath == null) {
                        Toast.makeText(this, "Failed to open game file",
                                       Toast.LENGTH_LONG).show();
                        finish();
                    }
                });
            }, "Vera360-FileResolve").start();
        } else {
            Toast.makeText(this, "No game file selected", Toast.LENGTH_LONG).show();
            finish();
        }
    }

    /**
     * Resolve a content:// URI to a local file path.
     * Copies the file to app cache if needed (content providers don't support random access).
     */
    private String resolveUriToPath(Uri uri) {
        // If it's already a file:// path, use directly
        if ("file".equals(uri.getScheme())) {
            return uri.getPath();
        }

        // For content:// URIs, copy to cache dir for random-access reads
        try {
            String name = "game_" + System.currentTimeMillis();
            String lastSeg = uri.getLastPathSegment();
            if (lastSeg != null) {
                int dot = lastSeg.lastIndexOf('.');
                if (dot >= 0) name += lastSeg.substring(dot);
                else name += ".xex";
            }

            File cacheFile = new File(getCacheDir(), name);

            try (InputStream in = getContentResolver().openInputStream(uri);
                 OutputStream out = new FileOutputStream(cacheFile)) {
                if (in == null) return null;
                byte[] buf = new byte[65536];
                int n;
                long total = 0;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                    total += n;
                    final long t = total;
                    if (tvLoading != null) {
                        runOnUiThread(() -> tvLoading.setText(
                            String.format("Loading… %.1f MB", t / (1024.0 * 1024.0))));
                    }
                }
            }

            return cacheFile.getAbsolutePath();

        } catch (Exception e) {
            e.printStackTrace();
            return null;
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

    // ── SurfaceHolder.Callback ──────────────────────────────────────────

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        NativeBridge.surfaceCreated(holder.getSurface());
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        NativeBridge.surfaceChanged(width, height);
        if (!nativeRunning) {
            // Wait until file is resolved
            new Thread(() -> {
                while (resolvedGamePath == null) {
                    try { Thread.sleep(50); } catch (InterruptedException e) { return; }
                }
                runOnUiThread(() -> {
                    NativeBridge.setGameUri(resolvedGamePath);
                    NativeBridge.startEmulation();
                    nativeRunning = true;
                    emulationLoopActive = true;

                    if (tvLoading != null) tvLoading.setVisibility(View.GONE);
                    touchOverlay.setVisibility(View.VISIBLE);

                    // Start vsync-driven emulation loop
                    Choreographer.getInstance().postFrameCallback(this);
                });
            }, "Vera360-WaitFile").start();
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        emulationLoopActive = false;
        NativeBridge.surfaceDestroyed();
        nativeRunning = false;
    }

    // ── Choreographer-based render loop (vsync aligned) ─────────────────

    @Override
    public void doFrame(long frameTimeNanos) {
        if (emulationLoopActive) {
            NativeBridge.tick();
            Choreographer.getInstance().postFrameCallback(this);
        }
    }

    // ── Lifecycle ───────────────────────────────────────────────────────

    @Override
    protected void onPause() {
        super.onPause();
        emulationLoopActive = false;
        if (nativeRunning) NativeBridge.pause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        goFullscreen();
        if (nativeRunning) {
            NativeBridge.resume();
            emulationLoopActive = true;
            Choreographer.getInstance().postFrameCallback(this);
        }
    }

    @Override
    protected void onDestroy() {
        emulationLoopActive = false;
        NativeBridge.shutdown();
        // Clean up cached game file
        if (resolvedGamePath != null) {
            File f = new File(resolvedGamePath);
            if (f.exists() && f.getParent() != null
                    && f.getParent().equals(getCacheDir().getAbsolutePath())) {
                f.delete();
            }
        }
        super.onDestroy();
    }
}
