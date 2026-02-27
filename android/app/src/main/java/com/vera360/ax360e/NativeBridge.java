package com.vera360.ax360e;

import android.view.Surface;

/**
 * JNI bridge to the Vera360 native library (libvera360.so).
 * All native methods are implemented in src/xenia/app/jni_bridge.cc.
 */
public class NativeBridge {

    static {
        System.loadLibrary("vera360");
    }

    // ── Lifecycle ───────────────────────────────────────────────────────────
    public static native void init(String nativeLibDir);
    public static native void shutdown();

    // ── Vulkan surface ──────────────────────────────────────────────────────
    public static native void surfaceCreated(Surface surface);
    public static native void surfaceChanged(int width, int height);
    public static native void surfaceDestroyed();

    // ── Emulation control ───────────────────────────────────────────────────
    public static native void setGameUri(String uri);
    public static native void startEmulation();
    public static native void pause();
    public static native void resume();

    // ── Hardware queries ────────────────────────────────────────────────────
    public static native boolean isVulkanAvailable();
    public static native String getGpuName();

    // ── Input from touch overlay ────────────────────────────────────────────
    public static native void onControllerInput(int buttonMask, float lx, float ly, float rx, float ry);
    public static native void onTouchEvent(int action, float x, float y, int pointerId);
}
