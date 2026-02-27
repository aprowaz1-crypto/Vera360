/**
 * Vera360 — Xenia Edge
 * Emulator — top-level orchestrator
 *
 * Owns and initialises every subsystem, loads an XEX, and runs the
 * main emulation loop.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct ANativeWindow;

namespace xe {

class Emulator {
 public:
  Emulator();
  ~Emulator();

  /// Full system initialisation (call once, from the JNI bridge)
  bool Initialize(ANativeWindow* window, const std::string& storage_root);

  /// Tear everything down
  void Shutdown();

  /// Load and prepare a game image (XEX / ISO / STFS)
  bool LoadGame(const std::string& path);

  /// Frame tick — called from the render loop
  void Tick();

  /// Pause / resume
  void Pause();
  void Resume();
  bool is_running() const { return running_; }

  /// Surface change (e.g., orientation)
  void OnSurfaceChanged(ANativeWindow* window, int width, int height);
  void OnSurfaceDestroyed();

 private:
  bool InitMemory();
  bool InitGraphics(ANativeWindow* window);
  bool InitCpu();
  bool InitKernel();
  bool InitApu();
  bool InitHid();

  bool running_ = false;
  std::string storage_root_;
  int surface_width_ = 0;
  int surface_height_ = 0;
};

}  // namespace xe
