/**
 * Vera360 — Xenia Edge
 * Thread State management
 */

#include "xenia/cpu/processor.h"
#include "xenia/base/logging.h"

namespace xe::cpu {

// Additional thread state utilities

void ResetThreadState(ThreadState* ts) {
  for (auto& r : ts->gpr) r = 0;
  ts->lr = 0;
  ts->ctr = 0;
  ts->xer = 0;
  ts->cr = 0;
  for (auto& f : ts->fpr) f = 0.0;
  ts->pc = 0;
  ts->reserve_valid = false;
  XELOGD("Thread state #{} reset", ts->thread_id);
}

void DumpThreadState(const ThreadState* ts) {
  XELOGI("=== Thread #{} State ===", ts->thread_id);
  XELOGI("PC: 0x{:08X}  LR: 0x{:08X}  CTR: 0x{:08X}  CR: 0x{:08X}",
         ts->pc, ts->lr, ts->ctr, ts->cr);
  for (int i = 0; i < 32; i += 4) {
    XELOGI("r{:2d}: 0x{:016X}  r{:2d}: 0x{:016X}  r{:2d}: 0x{:016X}  r{:2d}: 0x{:016X}",
           i, ts->gpr[i], i+1, ts->gpr[i+1], i+2, ts->gpr[i+2], i+3, ts->gpr[i+3]);
  }
}

}  // namespace xe::cpu
