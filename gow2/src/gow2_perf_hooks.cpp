// gears2-nx - CPU time the console has no use for.
//
// The game has three cores on the Switch, and in heavy scenes (explosions) all three were full (275-292 % of 300 % in
// rex_perfil.log) while the GPU sat idle 26-37 ms per frame waiting for commands. The stack sampler (ZL + ZR + R) put
// half of the busiest thread, Unreal's rendering thread at 85 % of a core, in one function:
//
//   sub_82977688  Direct3D's "is the GPU there yet" check: 4 x 8 db16cyc delays, then a look at the GPU's progress.
//                 It answers 1 while still waiting and its six callers simply call it again.
//
// On the Xbox 360 that spin cost a hardware thread out of six; here it takes a core from the threads it waits for (the
// game's main thread and the GPU ring). While it answers "still waiting" the thread now sleeps 0.1 ms instead.

#include "gow2_pch.h"

#include <rex/cvar.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

REXCVAR_DEFINE_INT32(gow2_espera_gpu_us, 100, "Gears2",
                     "Microsegundos que duerme el hilo que espera a la GPU (sub_82977688) cada vez que aun no esta; "
                     "0 = girar como en la Xbox 360");

REX_EXTERN(__imp__sub_82977688);
REX_HOOK_RAW(sub_82977688) {
  __imp__sub_82977688(ctx, base);
#if defined(__SWITCH__)
  if (ctx.r3.u32 == 1) {
    const int32_t us = REXCVAR_GET(gow2_espera_gpu_us);
    svcSleepThread(us > 0 ? int64_t(us) * 1000 : 0);
  }
#endif
}
