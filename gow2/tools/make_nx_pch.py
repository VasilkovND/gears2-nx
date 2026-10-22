#!/usr/bin/env python3
"""Make the Switch PCH for the generated code.

generated/default/gow2_pch.h is rendered by the host codegen (ReXGlue v0.10.0). The Switch
needs the changes below (the first three came from Dead Space 2's port, ds2-nx, through Dante's
Inferno's; the second is a no-op here unless the codegen emits that macro):

  1. DEFINE_REX_FUNC gets a GCC branch (attributes after the declarator). Without it GCC
     drops the weak alias and the link fails with every sub_ symbol undefined.
  2. REX_RAW_ADDR_W(x, n): DS2's codegen writes it for stores; outside the official-SDK
     NX memory model it is REX_RAW_ADDR(x).
  3. The interrupt locks, per thread (below).
  5. A stack guard in REX_FUNC_PROLOGUE() (below).
  4. __builtin_arm_isb for GCC: the PC port's patch_spin_hints.py writes clang's builtin for the
     Xenon delay hints of spin loops; devkitA64's GCC has no such builtin, the same ISB inline.

Usage: make_nx_pch.py <generated/default/gow2_pch.h> <out.h>
"""
import sys

src, dst = sys.argv[1], sys.argv[2]
text = open(src, encoding="utf-8").read()

GCC_BRANCH = '''#elif defined(__GNUC__) && !defined(__clang__)
// GCC (devkitA64): attributes go after the declarator, or the weak alias is dropped.
#define DEFINE_REX_FUNC(name)                                       \\
  extern "C" REX_FUNC(name)                                         \\
      __attribute__((alias("__imp__" #name), weak, noinline));      \\
  REX_EXTERN(__imp__##name)
#else
#define DEFINE_REX_FUNC(name)                                       \\
  __attribute__((alias("__imp__" #name)))'''

anchor = '''#else
#define DEFINE_REX_FUNC(name)                                       \\
  __attribute__((alias("__imp__" #name)))'''
if text.count(anchor) != 1:
    sys.exit("DEFINE_REX_FUNC anchor not found exactly once")
text = text.replace(anchor, GCC_BRANCH)

if "REX_RAW_ADDR_W" not in text:
    text = text.rstrip() + '''

//=============================================================================
// Switch (rexglue-sdk-nx): DS2's codegen emits REX_RAW_ADDR_W for stores.
//=============================================================================
#ifndef REX_RAW_ADDR_W
#define REX_RAW_ADDR_W(x, n) REX_RAW_ADDR(x)
#endif
'''
# 3. The interrupt locks. Guest code brackets its lwarx/stwcx. sequences with mtmsrd (interrupts off), and the codegen
#    turns each of those into one process-wide recursive mutex. The atomic itself is already a host compare-and-swap;
#    the mutex only serialized every thread doing atomics: Dead Space 2's job scheduler (the main thread queuing
#    skinning and culling jobs, four EARSJob workers taking them) spent up to a quarter of the main thread blocked on
#    it with 3 cores for 9 busy threads. Now the "interrupts off" state is per thread (what mfmsr reads back), and the
#    old global lock stays available (ppc_msr_cerrojo_global_(), set at startup by gow2_cpu.cpp from a cvar).
OLD_LOCKS_START = "#define REX_CHECK_GLOBAL_LOCK()"
OLD_LOCKS_END = "rex::thread::global_critical_region::mutex().unlock();                                \\\n  } while (0)\n"
i = text.find(OLD_LOCKS_START)
j = text.find(OLD_LOCKS_END, i)
if i < 0 or j < 0 or text.count(OLD_LOCKS_START) != 1:
    sys.exit("interrupt lock macros not found exactly once")
j += len(OLD_LOCKS_END)
NEW_LOCKS = """// Switch (make_nx_pch.py): the interrupt locks per thread unless ppc_msr_cerrojo_global_() (see the script).
inline bool& ppc_msr_cerrojo_global_() {
  static bool global = false;
  return global;
}

inline i32& ppc_msr_profundidad_() {
  static thread_local i32 profundidad = 0;
  return profundidad;
}

#define REX_CHECK_GLOBAL_LOCK()                                          \\
  (ppc_msr_cerrojo_global_() ? ([&]() -> u64 {                           \\
    auto lock_ = rex::thread::global_critical_region::AcquireDirect();   \\
    return ppc_global_lock_count_().load() ? 0 : 0x8000;                 \\
  }())                                                                   \\
                             : (ppc_msr_profundidad_() ? u64(0) : u64(0x8000)))

#define REX_ENTER_GLOBAL_LOCK()                              \\
  do {                                                       \\
    if (ppc_msr_cerrojo_global_()) {                         \\
      rex::thread::global_critical_region::mutex().lock();   \\
      ppc_global_lock_count_().fetch_add(1);                 \\
    } else {                                                 \\
      ++ppc_msr_profundidad_();                              \\
    }                                                        \\
  } while (0)

#define REX_LEAVE_GLOBAL_LOCK()                              \\
  do {                                                       \\
    if (ppc_msr_cerrojo_global_()) {                         \\
      ppc_global_lock_count_().fetch_sub(1);                 \\
      rex::thread::global_critical_region::mutex().unlock(); \\
    } else {                                                 \\
      --ppc_msr_profundidad_();                              \\
    }                                                        \\
  } while (0)
"""
text = text[:i] + NEW_LOCKS + text[j:]

# 5. A stack guard in every recompiled function: REX_FUNC_PROLOGUE() (the first statement of each) returns at once,
#    answering 0, when the guest stack pointer is within kGuardMargin of its thread's stack limit (the KPCR's
#    stack_end_ptr at r13 + 0x74). Gears of War 2 recursed without end through objects that pointed at themselves
#    (each walk a different function), and every one of those crashed with a fault below the stack; with the guard
#    the deepest call of any such recursion is cut, the chain unwinds normally, and src/gow2_guards.cpp logs where
#    it happened (gow2_stack_cut). A few instructions per call.
PROLOGUE_GCC = """#elif defined(__GNUC__)
#define REX_FUNC_PROLOGUE()         \\
  do {                              \\
    if (((size_t)base & 0x1F) != 0) \\
      __builtin_unreachable();      \\
  } while (0)
#else
#define REX_FUNC_PROLOGUE() ((void)0)
#endif
#endif"""
PROLOGUE_GUARD = """#elif defined(__GNUC__)
// Switch (make_nx_pch.py, point 5): the stack guard.
#define REX_FUNC_PROLOGUE()                                    \\
  do {                                                         \\
    if (((size_t)base & 0x1F) != 0)                            \\
      __builtin_unreachable();                                 \\
    if (__builtin_expect(gow2_stack_near_limit(ctx, base), 0)) { \\
      gow2_stack_cut(ctx, base, __func__);                     \\
      return;                                                  \\
    }                                                          \\
  } while (0)
#else
#define REX_FUNC_PROLOGUE() ((void)0)
#endif
#endif

// Point 5's helpers: the guest stack pointer against the KPCR's stack_end_ptr (r13 + 0x74), and the cut itself
// (src/gow2_guards.cpp: answers 0 and logs the call site once).
constexpr uint32_t kGuardMargin = 0x8000;
extern "C" void gow2_stack_cut(PPCContext& ctx, uint8_t* base, const char* function);
inline bool gow2_stack_near_limit(const PPCContext& ctx, const uint8_t* base) {
  const uint32_t pcr = ctx.r13.u32;
  if (!pcr) return false;
  uint32_t raw;
  __builtin_memcpy(&raw, base + pcr + 0x74, 4);
  const uint32_t limit = __builtin_bswap32(raw);
  return limit && ctx.r1.u32 < limit + kGuardMargin;
}"""
if PROLOGUE_GUARD.split("\n")[1] not in text:
    if text.count(PROLOGUE_GCC) != 1:
        sys.exit("REX_FUNC_PROLOGUE (GCC, no profiling) not found exactly once")
    text = text.replace(PROLOGUE_GCC, PROLOGUE_GUARD)

if "__builtin_arm_isb" not in text:
    text = text.rstrip() + '''

//=============================================================================
// Switch (make_nx_pch.py): the spin-loop pause patch_spin_hints.py writes (clang's builtin) for GCC.
//=============================================================================
#if defined(__GNUC__) && !defined(__clang__) && defined(__aarch64__) && !defined(__builtin_arm_isb)
#define __builtin_arm_isb(x) __asm__ __volatile__("isb" ::: "memory")
#endif
'''

# Only when it changes: every unit of the recompiled tree depends on this header through the PCH, so rewriting it
# with the same text on each configure would make the next build recompile all of them.
try:
    previous = open(dst, encoding="utf-8").read()
except OSError:
    previous = None
if previous == text:
    print("unchanged", dst)
else:
    open(dst, "w", encoding="utf-8").write(text)
    print("wrote", dst)
