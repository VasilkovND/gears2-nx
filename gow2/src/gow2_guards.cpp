// gears2-nx - guards against loops in the game's data (diagnostics, then a way through).
//
// Loading the first level on the Switch, one object (an actor, by the fields used) pointed at itself: it was in its
// own attachment array (+236 data, +240 count; UE3's Actor::Attached by its shape) and its field +64 was itself too.
// Two functions walk those links recursively and never ended, and the main thread's stack ran out:
//   sub_8255E4A8  for each attached object, the same function through vtable +428;
//   sub_8255A5C0  through the object at +64 (vtable +52), unless object flag bit 61 is set.
// The guards cut each walk at a depth no real chain reaches, take the object out of its own attachment array, and
// log the object's name and class once (UE3 names: GNames at 0x83136834, Name at +44, Class at +52, as
// gears-of-war-2-pc-port reads them), so the reason it points at itself can be found.

#include "gow2_pch.h"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_set>

#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>

namespace {
constexpr uint32_t kGNames = 0x83136834;
constexpr uint32_t kObjNameIndex = 44;
constexpr uint32_t kObjClass = 52;
constexpr uint32_t kNameEntryText = 8;
constexpr int kMaxDepth = 48;

thread_local int t_attach_depth = 0;
thread_local int t_chain_depth = 0;
std::atomic<uint32_t> g_cuts{0};

std::string NameOf(uint8_t* base, uint32_t object) {
  if (!object || object < 0x40000000u) return "?";
  const uint32_t index = REX_LOAD_U32(object + kObjNameIndex);
  const uint32_t table = REX_LOAD_U32(kGNames), count = REX_LOAD_U32(kGNames + 4);
  if (!table || index >= count) return fmt::format("name#{}", index);
  const uint32_t entry = REX_LOAD_U32(table + index * 4);
  if (!entry) return fmt::format("name#{}", index);
  std::string text;
  for (uint32_t i = 0; i < 96; ++i) {
    const char ch = char(*(base + entry + kNameEntryText + i));
    if (!ch) break;
    text += ch;
  }
  return text;
}

void Report(uint8_t* base, const char* walk, uint32_t object, int depth) {
  const uint32_t n = g_cuts.fetch_add(1);
  if (n >= 8 && (n & (n - 1)) != 0) return;
  const uint32_t cls = REX_LOAD_U32(object + kObjClass);
  REXLOG_WARN("[guard] {} walk cut at depth {} ({} cuts): {} (class {}) at {:08X}, vtable {:08X}, +64 {:08X}, "
              "attached {} first {:08X}",
              walk, depth, n + 1, NameOf(base, object), NameOf(base, cls), object, REX_LOAD_U32(object),
              REX_LOAD_U32(object + 64), REX_LOAD_U32(object + 240),
              REX_LOAD_U32(object + 240) ? REX_LOAD_U32(REX_LOAD_U32(object + 236)) : 0);
}

// The object out of its own attachment array (moving the rest down): every walk of that array loops otherwise.
// Its field +64 (a Kismet sequence's ParentSequence, by the class the log names: Main_Sequence of class Sequence)
// pointing at itself is cleared too: a root sequence has no parent, and loops that climb parents never end otherwise.
void ClearSelfParent(uint8_t* base, uint32_t object) {
  if (REX_LOAD_U32(object + 64) == object) {
    REX_STORE_U32(object + 64, 0);
    REXLOG_WARN("[guard] {} at {:08X} was its own parent (+64): cleared", NameOf(base, object), object);
  }
}

// Only for sub_8255E4A8's objects (the attachment array is theirs: other classes hold anything at +236).
void Untangle(uint8_t* base, uint32_t object) {
  ClearSelfParent(base, object);
  const uint32_t data = REX_LOAD_U32(object + 236);
  uint32_t count = REX_LOAD_U32(object + 240);
  if (data < 0x40000000u || data >= 0xA0000000u || count > 4096) return;
  for (uint32_t i = 0; i < count;) {
    if (REX_LOAD_U32(data + i * 4) == object) {
      for (uint32_t j = i + 1; j < count; ++j) REX_STORE_U32(data + (j - 1) * 4, REX_LOAD_U32(data + j * 4));
      --count;
      REX_STORE_U32(object + 240, count);
      REXLOG_WARN("[guard] {} at {:08X} was attached to itself: taken out ({} left)", NameOf(base, object), object,
                  count);
    } else {
      ++i;
    }
  }
}
}  // namespace

REX_EXTERN(__imp__sub_8255E4A8);
REX_HOOK_RAW(sub_8255E4A8) {
  Untangle(base, ctx.r3.u32);
  if (t_attach_depth >= kMaxDepth) {
    Report(base, "attachment", ctx.r3.u32, t_attach_depth);
    return;
  }
  ++t_attach_depth;
  __imp__sub_8255E4A8(ctx, base);
  --t_attach_depth;
}

REX_EXTERN(__imp__sub_8255A5C0);
REX_HOOK_RAW(sub_8255A5C0) {
  ClearSelfParent(base, ctx.r3.u32);
  if (t_chain_depth >= kMaxDepth) {
    Report(base, "+64 chain", ctx.r3.u32, t_chain_depth);
    ctx.r3.u64 = 0;  // what the function answers at the end of a chain
    return;
  }
  ++t_chain_depth;
  __imp__sub_8255A5C0(ctx, base);
  --t_chain_depth;
}

// The generic guard (make_nx_pch.py, point 5): any recompiled function entered with the guest stack within 32 KB of
// its limit lands here and returns at once with r3 = 0, so a runaway recursion anywhere ends in a cut instead of a
// crash. Logged once per function and call site (lr), with the object in r3 when it looks like a UE3 object.
extern "C" void gow2_stack_cut(PPCContext& ctx, uint8_t* base, const char* function) {
  static std::atomic<uint32_t> cuts{0};
  static std::mutex mutex;
  static std::unordered_set<uint64_t> seen;
  const uint32_t n = cuts.fetch_add(1) + 1;
  const uint32_t lr = uint32_t(ctx.lr), object = ctx.r3.u32;
  bool fresh;
  {
    std::lock_guard<std::mutex> lock(mutex);
    fresh = seen.size() < 4096 && seen.insert((uint64_t(std::hash<const void*>{}(function)) << 32) ^ lr).second;
  }
  if (fresh || (n & (n - 1)) == 0) {
    const bool ue3 = object >= 0x40000000u && object < 0xC0000000u && REX_LOAD_U32(object) >= 0x82000000u &&
                     REX_LOAD_U32(object) < 0x83300000u;
    REXLOG_WARN("[guard] stack cut #{} in {} (lr {:08X}, r1 {:08X}), r3 {:08X}{}", n, function, lr, ctx.r1.u32,
                object, ue3 ? fmt::format(" = {} (class {})", NameOf(base, object),
                                          NameOf(base, REX_LOAD_U32(object + kObjClass)))
                            : std::string());
  }
  ctx.r3.u64 = 0;
}

// Where the main guest thread is, for the 10 s statistics (src/gow2_gpu.cpp): its last call site (lr), stack pointer
// and the objects in r3, r30 and r31, named when they look like UE3 objects. A hang shows up as the same place
// repeated, without a rebuild to find it.
namespace {
std::string Describe(uint8_t* base, uint32_t value) {
  const bool ue3 = value >= 0x40000000u && value < 0xC0000000u && REX_LOAD_U32(value) >= 0x82000000u &&
                   REX_LOAD_U32(value) < 0x83300000u;
  if (!ue3) return fmt::format("{:08X}", value);
  return fmt::format("{:08X} {} ({})", value, NameOf(base, value), NameOf(base, REX_LOAD_U32(value + kObjClass)));
}
}  // namespace

void gow2_log_main_thread() {
  auto* kernel = rex::system::kernel_state();
  if (!kernel) return;
  for (auto& thread : kernel->object_table()->GetObjectsByType<rex::system::XThread>()) {
    if (!thread->main_thread() || !thread->thread_state()) continue;
    const PPCContext& ctx = *thread->thread_state()->context();
    uint8_t* base = kernel->memory()->virtual_membase();
    REXLOG_INFO("[main] lr {:08X} r1 {:08X} r3 {} r30 {} r31 {}", uint32_t(ctx.lr), ctx.r1.u32,
                Describe(base, ctx.r3.u32), Describe(base, ctx.r30.u32), Describe(base, ctx.r31.u32));
  }
}
