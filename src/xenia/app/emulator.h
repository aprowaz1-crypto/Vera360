/**
 * Vera360 — Xenia Edge
 * Emulator — top-level orchestrator
 *
 * Owns and initialises every subsystem, loads an XEX, and runs the
 * main emulation loop.
 */
#pragma once

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

struct ANativeWindow;

namespace xe::kernel {
class KernelState;
}

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
  bool is_game_loaded() const { return game_loaded_; }
  uint64_t frame_count() const { return frame_count_; }

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

  /// Game loading helpers
  bool LoadXex(const std::string& path, std::ifstream& file,
               std::streampos file_size);
  bool LoadStfsPackage(const std::string& path);
  bool LoadDiscImage(const std::string& path);

  bool running_ = false;
  bool game_loaded_ = false;
  uint64_t frame_count_ = 0;
  std::string storage_root_;
  int surface_width_ = 0;
  int surface_height_ = 0;

  kernel::KernelState* kernel_state_ = nullptr;
};

}  // namespace xe
