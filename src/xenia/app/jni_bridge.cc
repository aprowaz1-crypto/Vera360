/**
 * Vera360 — Xenia Edge
 * JNI Bridge — connects Java (ax360e UI) ↔ C++ emulator core
 *
 * Every function declared in NativeBridge.java is implemented here.
 * The shared library is loaded via System.loadLibrary("vera360").
 */

#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/asset_manager_jni.h>

#include "xenia/app/emulator.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_android.h"

// Global emulator instance
static std::unique_ptr<xe::Emulator> g_emulator;

extern "C" {

// ──────────────────────────── Lifecycle ────────────────────────────

JNIEXPORT jboolean JNICALL
Java_com_vera360_ax360e_NativeBridge_nativeInit(
    JNIEnv* env, jclass /*clazz*/, jobject surface, jobject asset_mgr,
    jstring storage_path) {

  XELOGI("JNI: nativeInit");

  // Store Android references
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  AAssetManager* assets = AAssetManager_fromJava(env, asset_mgr);
  xe::platform::SetNativeWindow(window);
  xe::platform::SetAssetManager(assets);

  const char* path = env->GetStringUTFChars(storage_path, nullptr);
  std::string root(path);
  env->ReleaseStringUTFChars(storage_path, path);

  g_emulator = std::make_unique<xe::Emulator>();
  bool ok = g_emulator->Initialize(window, root);
  return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_nativeShutdown(
    JNIEnv* /*env*/, jclass /*clazz*/) {
  XELOGI("JNI: nativeShutdown");
  if (g_emulator) {
    g_emulator->Shutdown();
    g_emulator.reset();
  }
}

JNIEXPORT jboolean JNICALL
Java_com_vera360_ax360e_NativeBridge_nativeLoadGame(
    JNIEnv* env, jclass /*clazz*/, jstring path) {
  XELOGI("JNI: nativeLoadGame");
  const char* p = env->GetStringUTFChars(path, nullptr);
  bool ok = g_emulator ? g_emulator->LoadGame(p) : false;
  env->ReleaseStringUTFChars(path, p);
  return ok ? JNI_TRUE : JNI_FALSE;
}

// ──────────────────────────── Render loop ──────────────────────────

JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_nativeTick(
    JNIEnv* /*env*/, jclass /*clazz*/) {
  if (g_emulator) g_emulator->Tick();
}

JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_nativePause(
    JNIEnv* /*env*/, jclass /*clazz*/) {
  if (g_emulator) g_emulator->Pause();
}

JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_nativeResume(
    JNIEnv* /*env*/, jclass /*clazz*/) {
  if (g_emulator) g_emulator->Resume();
}

// ──────────────────────────── Surface ─────────────────────────────

JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_nativeSurfaceChanged(
    JNIEnv* env, jclass /*clazz*/, jobject surface, jint w, jint h) {
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  xe::platform::SetNativeWindow(window);
  if (g_emulator) g_emulator->OnSurfaceChanged(window, w, h);
}

JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_nativeSurfaceDestroyed(
    JNIEnv* /*env*/, jclass /*clazz*/) {
  xe::platform::SetNativeWindow(nullptr);
  if (g_emulator) g_emulator->OnSurfaceDestroyed();
}

// ──────────────────────────── Input ───────────────────────────────

JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_nativeSetButton(
    JNIEnv* /*env*/, jclass /*clazz*/, jint pad, jint button, jboolean pressed) {
  // Forward to HID
  // xe::hid::SetButton(pad, static_cast<uint16_t>(button), pressed);
}

JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_nativeSetAnalog(
    JNIEnv* /*env*/, jclass /*clazz*/, jint pad, jboolean left,
    jfloat x, jfloat y) {
  // xe::hid::SetAnalog(pad, left, x, y);
}

JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_nativeSetTrigger(
    JNIEnv* /*env*/, jclass /*clazz*/, jint pad, jboolean left, jfloat value) {
  // xe::hid::SetTrigger(pad, left, value);
}

// ──────────────────────────── Queries ─────────────────────────────

JNIEXPORT jboolean JNICALL
Java_com_vera360_ax360e_NativeBridge_nativeIsRunning(
    JNIEnv* /*env*/, jclass /*clazz*/) {
  return (g_emulator && g_emulator->is_running()) ? JNI_TRUE : JNI_FALSE;
}

}  // extern "C"
