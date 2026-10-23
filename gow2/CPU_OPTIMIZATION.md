# CPU optimization of the recompiled code (1.0.0)

How the game thread of gears2-nx was made fast enough that fights stopped being CPU-bound, what was tried, what
broke on the way, and how to carry the work further or to another ReXGlue port (ds2-nx, Dante's Inferno). Written
for whoever picks this up next, human or agent. Short version in [PORTING.md](PORTING.md), section "1.0.0".

## Starting point (0.8.0)

- Game thread ("Main XThread") 80-90 % busy in fights at 19-24 fps; render thread, ring and GPU waited for it
  (CPU 1785 / GPU 614.4 / RAM 1600 MHz).
- Flat profile: Unreal's main loop and the UnrealScript interpreter over hundreds of small functions, nothing above
  ~6 %. One-off native hooks of hot functions gave 1-3 % each: exhausted as a strategy.
- Earlier codegen passes (still in place, `tools/codegen.sh`): `patch_leaf_prologue.py` (no stack guard in leaves),
  `patch_stack_access.py` (guest-stack accesses through memcpy macros, not volatile), `patch_inline_calls.py`
  (save/restore helpers copied inline, same-file calls to `__imp__` bodies so GCC may inline them).

## Diagnosis

Every guest register is a field of `PPCContext& ctx` (`ctx.r3.u64`, `ctx.f1.f64`, `ctx.cr6.eq` ...), every guest
memory access goes through `base` (volatile or memcpy, byte-swapped), and the build uses `-fno-strict-aliasing`. GCC
cannot prove a guest store does not hit `ctx`, so after each store it reloads every register it needs, and it stores
each register back as soon as it is written. In the disassembly of a hot function (e.g. `__imp__sub_82251580`)
roughly half the instructions were `ldr/str [x(ctx), #off]` ping-pong. That traffic, not the game's work, was the
cost. ReXGlue's own `*_as_local` codegen options (`non_volatile_as_local` etc.) trust the PowerPC ABI and zero-init
the locals; they were not used (see "ABI is not reliable" below).

## What was done: `tools/patch_local_regs.py`

A pass over the generated C++ (after `patch_inline_calls.py`), run by `codegen.sh` (`LOCALREGS=0` disables it).

1. Each function gets local copies of the registers it uses (`PPCRegister L_r3 = ctx.r3;` at entry; GCC drops the
   dead ones), and every `ctx.<reg>` becomes `L_<reg>`. `ctx.fpscr` and `ctx.msr` stay in ctx.
2. The save/restore helpers (`rex_inl___savegprlr_N` ...) are copied into the body first so they work on the locals.
3. `ctx` is made exact only at calls and returns, and only for the registers that matter:
   - at a call to G: write back the *dirty* registers (forward may-dirty dataflow over labels and gotos) that G may
     read (`REF(G)`) or may change while they are live after the call (`MOD(G) & live_out`); read back `MOD(G)`;
   - at a return: write back the dirty registers in `(MOD(F) | CH(F)) & LIVEOUT(F)`;
   - a tail call (call followed by `return;`) writes back `REF(X) | at_return` (it may jump into the rest of the
     same function, which the codegen split off).
4. Saves/restores of non-volatiles nobody gets back are dropped (`// (save/restore ... dropped)` in the output).

The whole-program facts, each a least fixpoint over the call graph (`fixpoint`, `fixpoint_liveout`):

| Set | Meaning | Notes |
| --- | --- | --- |
| `REF(F)` | registers F (or its callees) may read before writing | backward liveness; calls add the callee's REF; saves of PRES registers are not reads |
| `MOD(F)` | registers whose ctx copy may differ when F returns | written ones and callees' MOD, minus PRES; tail callees' MOD in full |
| `PRES(F)` | non-volatiles saved in the entry prefix into a frame slot and loaded back right before every return | symbolic r1/r12 frame offsets (stwu, addi, mr, back chain); only u64 std/ld and stfd/lfd pairs; vector and CR saves are not recognised yet |
| `CH(F)` | registers whose ctx copy F's execution may change (its own write-backs at calls, callees' MOD) | must be restored at return if live |
| `LIVEOUT(F)` | registers a caller may read after F returns | from every direct call site, through callers' returns, plus a floor from indirect call sites |

Calls the code cannot see into are taken at the calling convention's word: indirect calls (`REX_CALL_INDIRECT_FUNC`),
kernel imports (`__imp__<Export>`), hooked functions (found like `patch_inline_calls.py` does) and names that are not
`sub_`: they read the arguments (r3-r10, f1-f13, v1-v13) plus r1, r2, r13, lr, xer, and change every volatile
register. Imports that capture the whole context (`RtlCaptureContext`, `RtlRaiseException`, `RtlUnwind`,
`KeBugCheck*`) get every dirty register written back at the call, without making their REF `ALL` (that spread to
thousands of callers through assert paths).

## Things that matter (learned the hard way)

- **The ABI is not reliable between the game's own functions.** An ABI-check script found callers reading volatile
  registers after direct calls without writing them (the callee keeps them: v32-v63, r5-r12, CR fields), and ~30
  functions reading non-volatiles (r30, cr2-cr4, v64+) at entry other than to save them. That is why REF/MOD come
  from the code, and only unknown targets use the ABI.
- **Conditional writes are not kills.** `if (c) { r3 = 0; goto L; }` or a write inside an `if (...) {` block
  replaces nothing for certain; treating it as a kill made liveness unsound. Writes on a line with
  `if/else/switch/case/default/?` or inside a conditional block never kill.
- **Indirect-call targets must restore every non-volatile anything after the call may read**, including the REF of
  later callees. The first console build used only the caller's own reads after indirect calls (masked to the
  volatile ones) for LIVEOUT: a virtual function left a clobbered r31 in ctx, the next function saved and
  "restored" the clobbered value, and the game faulted at startup (read of 0x000040E6). Fix: at indirect sites the
  non-volatile part of LIVEOUT is the full liveness after the call.
- **The indirect floor must be the caller's own reads** for the volatile part: with full liveness (REF of later
  calls included) the floor became "everything" and LIVEOUT stopped removing anything.
- **`lwarx` writes `reserved` in full** (`ctx.reserved.u32 = ...`): treated as partial it put `reserved` in the REF
  of every function with atomics. Likewise `simde_mm_store_ps(ctx.vN.f32, ...)` (no cast) is a full vector write.
- **Do not trust `ctx` for diagnostics any more**: the 10 s `[main]` line (r3/r30/r31/lr of the main thread), crash
  dumps' guest context and the stack cut log read ctx, which now holds the last written-back values.

## The check build (`PLR_CHECK=1`)

`PLR_CHECK=1 tools/build_mac.sh <game>` builds a slower NRO (~100 MB) that verifies the analysis on the console:

- before each direct or indirect call it writes a poison `0x5EAD5EAD_5E<site>` into the dirty registers it did not
  write back (the callee must not read them); a fault at `0x5Exxxxxx` names the site;
- it snapshots the live registers the callee should keep and logs
  `[regcheck] site NNNNNN (function): the call to X changed r31 A->B` when one changed;
- it puts the poison back after the call (the poison itself is a change of ctx the analysis does not know about: the
  first check build forgot this and produced false reports and a fault of its own);
- `gow2/out/codegen/patch_local_regs.check.map` lists every site number (`<file index><<16 | n>`).

Use it after any change to the pass: 5-10 minutes of play with fights, then `grep regcheck` in the game log.

## How to measure

- Clocks and sampler: `logs/rex/rex_perfil.log` prints `relojes: CPU ... GPU ... memoria ...` and, per 10 s window,
  `juego N fps` and each thread's CPU %. The stack sampler (ZL+ZR+R with `switch_atajos_depuracion = true`, or
  `telemetria.flag`) costs ~8 %; compare like with like.
- Frame rate per 10 s from the game log: `[deko3d] ultimos 10 s: N fotogramas/s, D dibujos por fotograma ... GPU M ms
  por fotograma`; gameplay windows are those with more than 100 draws. `GPU parada entre trabajos X` is how long the
  GPU waited for work: small means GPU-bound, large means the CPU side is the limit.
- Baseline 0.8.0: `gow2/out/console_logs/run49` in the main checkout (sampler off).
- Symbolize `rex_perfil.log` / `rex_crash.log` offsets with the ELF of the exact NRO:
  `gow2/out/elf/<sha256(nro)[:16]>.elf` and `aarch64-none-elf-addr2line -f -e`. `rex_crash.log` keeps old runs: match
  the `codigo hasta` size to the NRO's text size.

Result (levels 1-2, 26 minutes, sampler on): 27.3 fps on average (0.8.0: 24.9), under 24 fps 15 % of the time (43 %),
median 29.9, GPU idle 1-6 ms per frame in heavy scenes (6-12). The main thread now spends most of its sampled time
waiting (`svcWaitProcessWideKeyAtomic`): fights are mostly GPU-bound. Static: NRO 76 -> 64 MB; memory instructions
in hot functions -20-45 % (`sub_82267BF8` 662 -> 397, `sub_824D63B8` 137 -> 81).

## Working on it

- Iterate on the pass without a full codegen: copy `gow2/out/codegen/generated/default/gow2_recomp.*.cpp` and
  `gow2_inline_helpers.h` *before* the pass (a pristine copy: the main checkout's generated tree or a
  `LOCALREGS=0` codegen), run
  `python3 gow2/tools/patch_local_regs.py gow2_inline_helpers.h gow2/src sdk/src sdk/include -- gow2_recomp.*.cpp`
  in that copy (~2 min, all cores), and compile single units with the command from
  `ninja -C gow2/out/sw -t commands gow2 | grep gow2_recomp.160.cpp` to compare `aarch64-none-elf-size` and the
  disassembly of hot functions.
- `PLR_DUMP=<file.pkl>` pickles REF/MOD/LIVEOUT/PRES per function; `PLR_DEBUG=<file>` lists functions whose REF
  exceeds the ABI's.
- A function the pass cannot follow (another use of `ctx`, setjmp) is left as it is and gets REF = MOD = ALL; if any
  exists, LIVEOUT is disabled globally (safe, slower). Today: none.
- A full build is ~10-12 min (codegen re-runs whenever the pass changes: it is a codegen input); ccache helps only
  for unchanged units.

## Next steps (not done)

1. **PRES for vector and CR saves** (`__savevmx_N`, `mfcr/mtcrf`): today they put v14-v31/v64-v127 and cr2-cr4 into
   MOD/REF of ~8,000 functions, so vector-heavy code still writes back and reloads them around calls.
2. **Call-heavy functions** gained least (`sub_82581F40`, `sub_82373DC0`): values reloaded after a call and stored
   back unchanged at a merge (may-dirty joins). Splitting dirty state per path, or a store-only-if-changed scheme,
   would cut that.
3. **The stack guard prologue** (`REX_FUNC_PROLOGUE`: two loads and a compare per non-leaf call) could read a
   per-thread limit cached in ctx instead of the KPCR.
4. **Cross-file inlining** (unity builds of call-related units or LTO) now pays more: with registers in locals an
   inlined callee's ctx traffic folds away.
5. The GPU is now the limit in fights (31-38 ms per frame at 614 MHz): further frame-rate gains are on the renderer.

## Porting the pass to another ReXGlue title

The pass only relies on the generated code's shape (ReXGlue v0.10.0 text: `DEFINE_REX_FUNC`, `ctx.<reg>` fields,
`sub_X(ctx, base);` calls, `REX_CALL_INDIRECT_FUNC`, `loc_X:` labels and gotos, helper copies from
`patch_inline_calls.py`) plus two PCH pieces from `make_nx_pch.py` (the `REX_SLOAD/SSTORE` macros and the
`gow2_regcheck_fail` declaration, defined in `src/gow2_guards.cpp` for the check build). Things to adapt: the hooked
function scan (source dirs passed on the command line), `CTX_IMPORTS`, the stack guard's `GUARD_REF/GUARD_MOD`
(drop them if the title has no stack guard), and the native callers of guest functions (any `sub_X` named in the
sources gets LIVEOUT = ALL). Build the check version first, play, and only then measure the fast one.
