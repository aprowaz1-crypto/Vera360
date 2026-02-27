/**
 * Vera360 — Xenia Edge
 * JNI Bridge — connects Java (ax360e UI) ↔ C++ emulator core
 *
 * Every function declared in NativeBridge.java is implemented here.
 * Function names MUST match Java: Java_com_vera360_ax360e_NativeBridge_<method>
 * The shared library is loaded via System.loadLibrary("vera360").
 */

#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <vulkan/vulkan.h>

#include "xenia/app/emulator.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_android.h"
#include "xenia/hid/hid_android.h"

// Global emulator instance
static std::unique_ptr<xe::Emulator> g_emulator;

// Game URI stored from Java before surface is ready
static std::string g_pending_game_uri;

// ── Translate TouchOverlayView button mask → XINPUT button mask ──────────
// Java sends: A=0x0001 B=0x0002 X=0x0004 Y=0x0008
//             DU=0x0010 DD=0x0020 DL=0x0040 DR=0x0080
//             Start=0x0100 Back=0x0200 LB=0x0400 RB=0x0800
// C++ expects XINPUT: DU=0x0001 DD=0x0002 DL=0x0004 DR=0x0008
//             Start=0x0010 Back=0x0020 LThumb=0x0040 RThumb=0x0080
//             LShoulder=0x0100 RShoulder=0x0200
//             A=0x1000 B=0x2000 X=0x4000 Y=0x8000
static uint16_t TranslateButtons(int java_mask) {
  uint16_t x = 0;
  if (java_mask & 0x0001) x |= 0x1000;  // A
  if (java_mask & 0x0002) x |= 0x2000;  // B
  if (java_mask & 0x0004) x |= 0x4000;  // X
  if (java_mask & 0x0008) x |= 0x8000;  // Y
  if (java_mask & 0x0010) x |= 0x0001;  // D-Up
  if (java_mask & 0x0020) x |= 0x0002;  // D-Down
  if (java_mask & 0x0040) x |= 0x0004;  // D-Left
  if (java_mask & 0x0080) x |= 0x0008;  // D-Right
  if (java_mask & 0x0100) x |= 0x0010;  // Start
  if (java_mask & 0x0200) x |= 0x0020;  // Back
  if (java_mask & 0x0400) x |= 0x0100;  // LB → LShoulder
  if (java_mask & 0x0800) x |= 0x0200;  // RB → RShoulder
  return x;
}

extern "C" {

// ──────────────────────────── Lifecycle ────────────────────────────

// Java: public static native void init(String nativeLibDir);
JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_init(
    JNIEnv* env, jclass /*clazz*/, jstring native_lib_dir) {

  XELOGI("JNI: init");

  const char* dir = env->GetStringUTFChars(native_lib_dir, nullptr);
  std::string lib_dir(dir);
  env->ReleaseStringUTFChars(native_lib_dir, dir);

  // Create emulator but don't init graphics yet (no surface)
  g_emulator = std::make_unique<xe::Emulator>();
  g_emulator->InitCore(lib_dir);

  XELOGI("JNI: init done, storage={}", lib_dir);
}

// Java: public static native void shutdown();
JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_shutdown(
    JNIEnv* /*env*/, jclass /*clazz*/) {
  XELOGI("JNI: shutdown");
  if (g_emulator) {
    g_emulator->Shutdown();
    g_emulator.reset();
  }
  g_pending_game_uri.clear();
}

// ──────────────────── Vulkan surface lifecycle ────────────────────

// Java: public static native void surfaceCreated(Surface surface);
JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_surfaceCreated(
    JNIEnv* env, jclass /*clazz*/, jobject surface) {

  XELOGI("JNI: surfaceCreated");
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  xe::platform::SetNativeWindow(window);

  if (g_emulator) {
    g_emulator->InitGraphicsFromSurface(window);
  }
}

// Java: public static native void surfaceChanged(int width, int height);
JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_surfaceChanged(
    JNIEnv* /*env*/, jclass /*clazz*/, jint width, jint height) {

  XELOGI("JNI: surfaceChanged {}x{}", (int)width, (int)height);
  if (g_emulator) {
    g_emulator->OnSurfaceChanged(
        xe::platform::GetNativeWindow(), width, height);
  }
}

// Java: public static native void surfaceDestroyed();
JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_surfaceDestroyed(
    JNIEnv* /*env*/, jclass /*clazz*/) {
  XELOGI("JNI: surfaceDestroyed");
  xe::platform::SetNativeWindow(nullptr);
  if (g_emulator) g_emulator->OnSurfaceDestroyed();
}

// ──────────────────── Emulation control ──────────────────────────

// Java: public static native void setGameUri(String uri);
JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_setGameUri(
    JNIEnv* env, jclass /*clazz*/, jstring uri) {

  const char* s = env->GetStringUTFChars(uri, nullptr);
  g_pending_game_uri = s;
  env->ReleaseStringUTFChars(uri, s);
  XELOGI("JNI: setGameUri = {}", g_pending_game_uri);
}

// Java: public static native void startEmulation();
JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_startEmulation(
    JNIEnv* /*env*/, jclass /*clazz*/) {

  XELOGI("JNI: startEmulation uri={}", g_pending_game_uri);
  if (g_emulator && !g_pending_game_uri.empty()) {
    // Convert content:// URI to a loadable path if needed
    // For now, try loading directly — LoadGame handles paths
    g_emulator->LoadGame(g_pending_game_uri);
    g_emulator->StartRunning();
  }
}

// Java: public static native void pause();
JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_pause(
    JNIEnv* /*env*/, jclass /*clazz*/) {
  XELOGI("JNI: pause");
  if (g_emulator) g_emulator->Pause();
}

// Java: public static native void resume();
JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_resume(
    JNIEnv* /*env*/, jclass /*clazz*/) {
  XELOGI("JNI: resume");
  if (g_emulator) g_emulator->Resume();
}

// Java: public static native void tick();
JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_tick(
    JNIEnv* /*env*/, jclass /*clazz*/) {
  if (g_emulator) g_emulator->Tick();
}

// ──────────────────────── Hardware queries ────────────────────────

// Java: public static native boolean isVulkanAvailable();
JNIEXPORT jboolean JNICALL
Java_com_vera360_ax360e_NativeBridge_isVulkanAvailable(
    JNIEnv* /*env*/, jclass /*clazz*/) {

  // Quick check: try to create a minimal VkInstance
  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "Vera360";
  app_info.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &app_info;

  VkInstance test_instance = VK_NULL_HANDLE;
  VkResult result = vkCreateInstance(&ci, nullptr, &test_instance);
  if (result == VK_SUCCESS && test_instance) {
    vkDestroyInstance(test_instance, nullptr);
    return JNI_TRUE;
  }
  return JNI_FALSE;
}

// Java: public static native String getGpuName();
JNIEXPORT jstring JNICALL
Java_com_vera360_ax360e_NativeBridge_getGpuName(
    JNIEnv* env, jclass /*clazz*/) {

  // Enumerate physical devices and return first GPU name
  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "Vera360";
  app_info.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &app_info;

  VkInstance inst = VK_NULL_HANDLE;
  if (vkCreateInstance(&ci, nullptr, &inst) != VK_SUCCESS || !inst) {
    return env->NewStringUTF("Unknown GPU");
  }

  uint32_t count = 0;
  vkEnumeratePhysicalDevices(inst, &count, nullptr);
  if (count == 0) {
    vkDestroyInstance(inst, nullptr);
    return env->NewStringUTF("No GPU found");
  }

  VkPhysicalDevice dev;
  count = 1;
  vkEnumeratePhysicalDevices(inst, &count, &dev);

  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(dev, &props);

  jstring name = env->NewStringUTF(props.deviceName);
  vkDestroyInstance(inst, nullptr);
  return name;
}

// ──────────────────────────── Input ───────────────────────────────

// Java: public static native void onControllerInput(
//           int buttonMask, float lx, float ly, float rx, float ry);
JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_onControllerInput(
    JNIEnv* /*env*/, jclass /*clazz*/,
    jint buttonMask, jfloat lx, jfloat ly, jfloat rx, jfloat ry) {

  // Translate Java button mask to XINPUT and apply as full state
  uint16_t xinput_buttons = TranslateButtons(buttonMask);

  // Set all buttons at once (pad 0)
  xe::hid::SetButtonsRaw(0, xinput_buttons);
  // Set analog sticks
  xe::hid::SetAnalog(0, true, lx, ly);
  xe::hid::SetAnalog(0, false, rx, ry);
}

// Java: public static native void onTouchEvent(
//           int action, float x, float y, int pointerId);
JNIEXPORT void JNICALL
Java_com_vera360_ax360e_NativeBridge_onTouchEvent(
    JNIEnv* /*env*/, jclass /*clazz*/,
    jint /*action*/, jfloat /*x*/, jfloat /*y*/, jint /*pointerId*/) {
  // Touch events are handled by TouchOverlayView → onControllerInput.
  // Direct touch events here are for future use (e.g. Kinect emulation).
}

}  // extern "C"
