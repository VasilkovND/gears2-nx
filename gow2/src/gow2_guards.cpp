// gears2-nx - guards against loops in the game's data (diagnostics, then a way through).
//
// sub_8255E4A8 walks an object's attached objects (the array at +236, its count at +240; UE3's Actor::Attached by its
// shape) and calls the same virtual function (vtable +428) on each, which for actors is this function again. Loading
// the first level on the Switch, the walk never ended: the attachments form a cycle, and the main thread's stack ran
// out (a fault just below it, in whatever the deepest frame allocated). The guard stops the walk at a depth no real
// attachment chain reaches and logs the objects of the cycle once.

#include "gow2_pch.h"

#include <atomic>

#include <rex/logging.h>

namespace {
constexpr int kMaxAttachDepth = 48;
thread_local int t_attach_depth = 0;
std::atomic<uint32_t> g_attach_cuts{0};
}  // namespace

REX_EXTERN(__imp__sub_8255E4A8);
REX_HOOK_RAW(sub_8255E4A8) {
  if (t_attach_depth >= kMaxAttachDepth) {
    const uint32_t self = ctx.r3.u32;
    const uint32_t data = REX_LOAD_U32(self + 236), count = REX_LOAD_U32(self + 240);
    const uint32_t first = count > 0 && data ? REX_LOAD_U32(data) : 0;
    const uint32_t n = g_attach_cuts.fetch_add(1);
    if (n < 8 || (n & (n - 1)) == 0) {
      REXLOG_WARN("[guard] attachment walk cut at depth {} ({} cuts): object {:08X} (vtable {:08X}) has {} attached, "
                  "first {:08X}{}",
                  t_attach_depth, n + 1, self, REX_LOAD_U32(self), count, first, first == self ? " (itself)" : "");
    }
    return;
  }
  ++t_attach_depth;
  __imp__sub_8255E4A8(ctx, base);
  --t_attach_depth;
}
