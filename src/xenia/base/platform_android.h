/**
 * Vera360 — Xenia Edge
 * Android platform utilities — header
 */
#pragma once

#include <cstdint>
#include <string>

#include <android/native_window.h>
#include <android/asset_manager.h>

namespace xe::platform {

void Initialize(ANativeWindow* window, AAssetManager* assets,
                const char* internal_path, const char* external_path);

ANativeWindow* GetNativeWindow();
void SetNativeWindow(ANativeWindow* w);

AAssetManager* GetAssetManager();
void SetAssetManager(AAssetManager* mgr);

const std::string& GetInternalDataPath();
const std::string& GetExternalDataPath();

std::string GetDeviceModel();
std::string GetDeviceSoC();
int GetAndroidApiLevel();
uint32_t GetCpuCoreCount();

}  // namespace xe::platform
