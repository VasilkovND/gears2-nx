// gears2-nx - CPU time the console has no use for.
//
// The game has three cores on the Switch, and in heavy scenes (explosions) all three were full (275-292 % of 300 % in
// rex_perfil.log) while the GPU sat idle 26-37 ms per frame waiting for commands. The stack sampler (ZL + ZR + R) put
// half of the busiest thread, Unreal's rendering thread at 85 % of a core, in one function:
//
//   sub_82977688  Direct3D's "is the GPU there yet" check: 4 x 8 db16cyc delays, then a look at the GPU's progress.
//                 It answers 1 while still waiting and its six callers simply call it again.
//
//   sub_829783A8  Direct3D's query GetData (occlusion and event queries): 1 (S_FALSE) while the GPU has not written
//                 the result yet, and Unreal's rendering thread polls it in a loop.
//   sub_82955830  the audio hardware (XMA, registers at 0x7FEA1818) check sub_82955768 spins on with db16cyc until it
//                 answers non-zero or its time runs out.
//
// On the Xbox 360 those spins cost a hardware thread out of six; here they take a core from the threads they wait for
// (the game's main thread and the GPU ring). While they answer "still waiting" the thread now sleeps instead.

#include "gow2_pch.h"

#include <rex/cvar.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

REXCVAR_DEFINE_INT32(gow2_espera_gpu_us, 100, "Gears2",
                     "Microsegundos que duermen las esperas activas del juego (GPU, consultas, audio) cada vez que aun no esta; "
                     "0 = girar como en la Xbox 360");

namespace {
void DormirEsperando() {
#if defined(__SWITCH__)
  const int32_t us = REXCVAR_GET(gow2_espera_gpu_us);
  svcSleepThread(us > 0 ? int64_t(us) * 1000 : 0);
#endif
}
}  // namespace

REX_EXTERN(__imp__sub_82977688);
REX_HOOK_RAW(sub_82977688) {
  __imp__sub_82977688(ctx, base);
  if (ctx.r3.u32 == 1) DormirEsperando();
}

REX_EXTERN(__imp__sub_829783A8);
REX_HOOK_RAW(sub_829783A8) {
  __imp__sub_829783A8(ctx, base);
  if (ctx.r3.u32 == 1) DormirEsperando();  // S_FALSE: not there yet
}

REX_EXTERN(__imp__sub_82955830);
REX_HOOK_RAW(sub_82955830) {
  __imp__sub_82955830(ctx, base);
  if (ctx.r3.u32 == 0) DormirEsperando();
}
