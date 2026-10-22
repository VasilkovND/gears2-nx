// gears2-nx - what the game's memory allocator asks for when it runs out (diagnostics).
//
// Loading the first level, UE3's pooled allocator called its out-of-memory handler, which allocates to build its
// message and so recursed until the main thread's stack ran out (a fault just below the stack). Neither the
// handler's size nor a failing NtAllocateVirtualMemory showed in the log. These hooks say:
//   gow2_appOutOfMemory (0x827F7638): the size the allocator could not get, once (the handler re-enters itself);
//   gow2_VirtualAlloc   (0x82AAA880): VirtualAlloc(address, size, type, protect); each failure, and the total.

#include "gow2_pch.h"

#include <atomic>

#include <rex/logging.h>

namespace {
std::atomic<uint32_t> g_oom_logged{0};
std::atomic<uint32_t> g_va_failed{0};
std::atomic<uint64_t> g_va_bytes{0};
std::atomic<uint32_t> g_va_calls{0};
}  // namespace

REX_EXTERN(__imp__gow2_appOutOfMemory);
REX_EXTERN(gow2_appOutOfMemory) {
  if (g_oom_logged.fetch_add(1) < 4) {
    REXLOG_ERROR("[mem] game out of memory: allocator {:08X} asked for {} bytes ({:08X}), lr {:08X}; VirtualAlloc so "
                 "far: {} calls, {} MB, {} failed",
                 ctx.r3.u32, ctx.r4.u32, ctx.r4.u32, uint32_t(ctx.lr), g_va_calls.load(), g_va_bytes.load() >> 20,
                 g_va_failed.load());
  }
  __imp__gow2_appOutOfMemory(ctx, base);
}

REX_EXTERN(__imp__gow2_VirtualAlloc);
REX_EXTERN(gow2_VirtualAlloc) {
  const uint32_t address = ctx.r3.u32, size = ctx.r4.u32, type = ctx.r5.u32, protect = ctx.r6.u32;
  __imp__gow2_VirtualAlloc(ctx, base);
  g_va_calls.fetch_add(1);
  if (ctx.r3.u32) {
    g_va_bytes.fetch_add(size);
  } else if (g_va_failed.fetch_add(1) < 16) {
    REXLOG_ERROR("[mem] VirtualAlloc({:08X}, {:X}, {:X}, {:X}) failed (total so far {} MB in {} calls)", address, size,
                 type, protect, g_va_bytes.load() >> 20, g_va_calls.load());
  }
}
