/**
 * Vera360 — Xenia Edge
 * Android platform utilities
 */

#include "xenia/base/platform_android.h"
#include "xenia/base/logging.h"

#include <sys/system_properties.h>
#include <unistd.h>
#include <fstream>
#include <string>

namespace xe::platform {

namespace {
ANativeWindow* g_native_window = nullptr;
AAssetManager* g_asset_manager = nullptr;
std::string g_internal_data_path;
std::string g_external_data_path;
}  // namespace

void Initialize(ANativeWindow* window, AAssetManager* assets,
                const char* internal_path, const char* external_path) {
  g_native_window = window;
  g_asset_manager = assets;
  if (internal_path) g_internal_data_path = internal_path;
  if (external_path) g_external_data_path = external_path;
  XELOGI("Platform initialized — Android");
}

ANativeWindow* GetNativeWindow() { return g_native_window; }
void SetNativeWindow(ANativeWindow* w) { g_native_window = w; }

AAssetManager* GetAssetManager() { return g_asset_manager; }
void SetAssetManager(AAssetManager* mgr) { g_asset_manager = mgr; }

const std::string& GetInternalDataPath() { return g_internal_data_path; }
const std::string& GetExternalDataPath() { return g_external_data_path; }

std::string GetDeviceModel() {
  char buf[128] = {};
  __system_property_get("ro.product.model", buf);
  return std::string(buf);
}

std::string GetDeviceSoC() {
  char buf[128] = {};
  __system_property_get("ro.board.platform", buf);
  return std::string(buf);
}

int GetAndroidApiLevel() {
  char buf[8] = {};
  __system_property_get("ro.build.version.sdk", buf);
  return atoi(buf);
}

uint32_t GetCpuCoreCount() {
  return static_cast<uint32_t>(sysconf(_SC_NPROCESSORS_ONLN));
}

}  // namespace xe::platform
