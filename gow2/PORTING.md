# Porting notes: Gears of War 2

Gears of War 2 (Xbox 360, retail disc) on the Nintendo Switch, as a homebrew NRO. This file records the plan, what
carries over from Dead Space 2 ([ds2-nx](https://github.com/VasilkovND/ds2-nx), `deadspace2/PORTING.md`) and Dante's
Inferno, and every problem met on the way.

## Status (0.8.0)

Playable on a console with the CPU overclocked (1785 MHz CPU, 768 MHz GPU): ~25 fps on average, 20-25 in fights, the
game's own 30 fps cap in quiet places. At stock clocks (1020 MHz) it is a slide show: the game thread alone needs more
than a frame. Levels 1 and 2 played through; every one of the game's 35,953 shaders is in the library.

| Area | State |
| --- | --- |
| Rendering | Levels 1-2 correct except: level 2's tree vertex shader skipped (`gow2_saltar_vs_rotos`), some distortion black |
| Shaders | 35,953 (1,402 VS, 34,551 PS) from every package; 392 MB library + 833 MB DKSH cache (300,130 variants), both read from the card on demand |
| Speed | Game thread bound (UnrealScript interpreter); GPU 25-38 ms per frame at 768 MHz |
| Language | Russian (`user_language = 12`) |

## What it took (0.8.0)

Every item names its switch (a `gow2.toml` cvar) and its code; SDK changes are the patches in `gow2/sdk-patches/`.

### Crashes and hangs

- **Unbounded recursion**: objects that point at themselves (Kismet's `Main_Sequence` in a checkpoint written by an
  early build listed itself as child and parent). A generic guard in every recompiled function's prologue
  (`make_nx_pch.py` point 5) returns at once when the guest stack is within 32 KB of its limit (KPCR + 0x74);
  `gow2_guards.cpp` logs where the chain was cut. The broken checkpoint itself has to be deleted.
- **Kernel memory blocks exhausted**: commits in 2 MB granules (SDK 0023).
- **Logger reading an unmapped address**: the main-thread logger checks protection first.
- **GPU hang after small resolves**: logged by SDK 0024/0027.

### The black world

Four separate causes, found with the skip modes (ZL+ZR+Right) and frame dumps (ZL+ZR+Left):

1. **EDRAM format aliases**: the game resolves a format-12 surface as 3 and 10 as 2 over the same EDRAM; the renderer
   kept them apart (SDK 0025 aliases them).
2. **Depth prepass mismatch**: the scene draws a depth-only prepass with one vertex shader and the base pass with
   another, testing `GEQUAL`. Separately translated shaders do not give bit-identical depth, so the base pass lost most
   of its pixels. A depth bias (SDK 0029) was not enough; `gow2_sin_prepaso_z` skips the prepass and the base pass
   writes its own depth (same image, less GPU).
3. **Far value per depth buffer**: shadow maps are cleared to 0, the scene (reversed Z) to 1; emulated clears now track
   the direction per buffer (base, format, pitch: `DepthKey`).
4. **Stencil-only clears** (SDK 0030) and depth fills drawn as clears.

### Overexposure, black characters, ghosting, flat surfaces

- **7e3 scene and exponent bias**: the scene is kept ×8 in EDRAM (`copy_dest_exp_bias`), resolved with -3, lights with
  -5 (SDK 0036); 7e3 values clamped (SDK 0035).
- **Black hero and black ghosting**: the ambient occlusion buffer is cleared through the depth path with raw EDRAM bits
  (depth24 << 8 | stencil, e.g. 0x00003C00); the colour clear now uses those bits (SDK 0040). Five render targets share
  one EDRAM base each frame (`kOtrasPorBase` = 8).
- **Level 2 flat brown surfaces**: textures whose first mip was still all zero when uploaded are checked again every
  frame, and samplers apply the fetch constant's LOD bias (SDK 0041).

### Level 2's giant triangles

The search modes (each press skips one vertex shader of the scene's depth-writing draws, with a capture) found one
shader: `38234053372F8F2F`, the trees. It picks its matrices from the float constants by an index it computes from a
per-vertex value: `a0 = ((r1.x + c10.x) mod 3) * 4`, then `c11..c14 + a0`. 198 vertex shaders have that pattern;
characters use relative constant reads too and are fine. The shader is skipped (`gow2_saltar_vs_rotos`) and its
inputs (fetch constants, c10-c26, the first vertices of the index stream) are logged as `[gpu] skin VS` for the real
fix.

### Shaders

- **Unreal does not create shaders through Direct3D**: only 7 shader objects are ever created; the rest are loaded
  straight from the packages. Draws are matched to the library by their microcode (`Biblioteca::Identificar`:
  by address, learned patched microcode, then candidates by length with the fetches masked).
- **Fetches at a rewritten r0.x**: some vertex shaders compute r0.x before a vertex fetch that uses it (occlusion
  boxes, trees). XenosRecomp marks those elements as fetched by the shader itself and says which in specialization
  constant 1 (`g_ManualesR0`); the loader reads it from the SPIR-V. SDK 0044 lets other specialization constants keep
  their value in the GLSL pass (constant 1 at first made every such shader fail: black screen).
- **A package with an undecodable LZX chunk**: `SP_Hospital_03.xxx` (the garden) has a chunk the offline extractor
  cannot decode on this disc; the whole package used to be dropped, and with it the garden ground's shaders (the ground
  was the clear colour, dark red). The extractor keeps what decodes and goes on.
- **Every shader, on demand**: 35,953 shaders do not fit in memory (392 MB of SPIR-V + 833 MB of DKSH). The library
  loads only its containers (needed to identify the microcode) and reads a shader's SPIR-V from the card the first time
  it is used (`gow2_shaders_por_demanda`, `BibliotecaShaders::CargarIndice`/`LeerSpirv`); the DKSH cache loads only
  its entry table and reads an entry when first asked for (`CacheDksh::CargarIndice`, SDK 0045). Limits raised to
  2 GB / 2^20 entries.

### Occlusion queries

Direct3D's query is a 64-byte block: the begin structure at +0x20 and the end one at +0, marked `0xFFFFFEED` on
`Issue(END)`; `GetData` answers "not yet" while the mark is there and Unreal's rendering thread polls it in a loop.
The renderer reports the GPU's samples-passed counter at each begin and end (`EmpezarOclusion` /
`TerminarOclusionAsincrona`), the ring thread writes the count back once that work's fence has passed
(`OcclusionPoll`), and the query boxes draw (they needed the r0.x fetches above). Waiting for the GPU stalled the
frame, so when the game waits (`g_gow2_query_waited`, set by the `GetData` hook) queries older than
`gow2_oclusion_sin_esperar_ms` are answered visible at once; a query of the work still being recorded makes that
work submitted (`EnviarOclusiones`). About half of the tested objects come back hidden.

### Speed

GPU and ring:
- Partial submits every 256 draws (SDK 0039): the GPU was idle until the frame closed.
- The ring thread is woken only when asleep, and gives the read pointer back every 256 words inside a batch: Direct3D
  waits for ring space on it.
- No full barrier for descriptor-only writes (SDK 0042); xxHash inlined (SDK 0037).

CPU, native code (`gow2_perf_hooks.cpp`, each behind a cvar):
- The game's spin waits sleep 100 µs instead of spinning (GPU progress, query `GetData`, XMA).
- XMemDecompress with libmspack (`gow2_lzx_nativo`), the audio filter and the ramped mix (NEON).
- memcpy, memset, TMap find by FName, bone quaternion to matrix (`gow2_cpu_nativo`; the matrix with single-precision
  steps and no fused multiply-adds, so the bits are the same).

CPU, generated code (passes run by `codegen.sh` after the PC port's):
- `patch_leaf_prologue.py`: functions that call nothing (9,304 of 48,830) get the prologue without the stack guard.
- `patch_stack_access.py`: guest stack (r1) accesses without `volatile` (348,442): the stack is one thread's, so the
  compiler may keep spilled locals in registers.
- `patch_inline_calls.py`: the register save/restore helpers (`__savegprlr_N` ... a fifth of all calls, 43,209) get
  always-inline copies in every file; same-file calls to functions no hook replaces call the body directly, so GCC may
  inline them. The hooked functions are read from the sources and are part of the codegen inputs: a new hook
  regenerates the code.

What limits it now: the game thread (Unreal's main loop and UnrealScript interpreter) is 85-93 % busy in fights, with a
flat profile (no function above ~6 %); the hottest ones (TMap find, the actor iterator) are bound by cache misses, not
instructions. The render thread and the GPU wait for it. Measure without the stack sampler (ZL+ZR+R): it pauses every
thread every millisecond and costs ~8 %. A 60 Hz panel cannot pace 25 fps evenly (frames alternate 33/50 ms); 30 or 20
can.

### Diagnostics

| Shortcut / switch | What it gives |
| --- | --- |
| ZL+ZR+Up | FPS overlay |
| ZL+ZR+Down | Capture to `capturas/` |
| ZL+ZR+Left | Full trace of the next frame (every draw: state, vertex fetches, every sampler's fetch words, pixel constants c0-c15), dump of every resolved texture (`volcado_NN/`), then a probe frame (`gow2_sonda` = "x,y": every base-pass draw drawn only into that 8x8 square, the GPU counts its pixels: `[probe] the one that shows`) |
| ZL+ZR+Right | Search modes (direct with `gow2_busqueda_directa`): each press skips one shader and takes a capture; pixel shaders of the base pass ranked by vertices (`gow2_busqueda_ps`) or vertex shaders writing the scene's depth |
| ZL+ZR+R | Stack sampler on/off (`logs/rex/rex_perfil.log`) |
| `[esperas]` | Every 10 s: time the game slept waiting for the GPU, queries and audio, GPU waits by call site |
| `[gpu] occlusion` | Queries counted, hidden, answered without waiting |
| `gow2/out/elf/<nro hash>.elf` | The ELF of every NRO built, to symbolize a profile (`addr2line`) |

### Open

- The trees' computed matrix index (`[gpu] skin VS` lines from level 2 will show what it reads).
- Distortion copies black (format 10 vs 3 images at EDRAM 2D0).
- The game thread: more native functions, or less work per frame.

## History (0.1.0)

### Status (0.1.0)

First NRO built from the game (Europe disc), not run on a console yet.

| Step | Result |
| --- | --- |
| Disc | Gears of War 2 (Europe) (En,Fr,De,Es,It,Zh,Ko,Pl,Ru,Cs,Hu): Media ID 33FCE762, XEX 0.0.0.6, `default.xex` sha256 `c110e168...`; 1,810 files, 6.2 GiB, streamed out of the .7z by `stage_sd.py` |
| Codegen | The PC port's manifest and hints as they are; 223 units; `patch_switch_traps.py`: 511 traps, 0 unresolved branches left; 559 barrier fences, 12 spin pauses |
| Shaders | 35,886 unique containers in the packages (34,486 PS, 1,400 VS); the default set (startup + SP_Hospital*) is 10,371: all translate and validate; 116 MB library, 79,726 DKSH variants (221 MB), 56 variants of 28 VS fail in uam |
| Create hooks | `D3DDevice_CreateVertexShader` 0x829832E8 (exact match with DS2), `CreatePixelShader` 0x82982F20 (same shape) |

### The Europe disc is the PC port's code

The PC port supports 0.0.0.5 (63AF211E). The Europe disc is 0.0.0.6 with the same file time (2008-09-23). Its image
run through the PC port's own tools (`harvest_hints.py` on the sections of the dumped image, `hint_fixpoint.py`,
`find_orphan_gotos.py`, against a hint-free baseline codegen) gives all 175 of the PC port's harvested hints at the
same addresses (plus 19 more, among them the PC port's three runtime-found ones), and exactly its five catch-funclet
ends, three parent tail chunks and the tail call at 0x82D0C758. So the manifest is used unchanged.

### Shaders without constant tables

Unreal Engine 3 strips the D3D constant table from every shader (it binds constants by register itself), and
XenosRecomp needed it for float constant names, bool constants and samplers. Now (`ShaderRecompiler::sinTabla`): float
registers are read straight from the stage's uniform block (`g_C(i)` for relative reads), literal registers stay the
shader's own, the 16 bool bits get names, and samplers come from a first pass that records every fetch constant and
its dimension; the console takes them from the SPIR-V bindings (`SamplersDesdeSpirv`) and uploads the whole uniform
block. Vertex shaders that declare one usage twice read one input.

### First run on the console (2026-10-22, run 1)

Boots in title takeover, sound plays, 30 swaps/s, ~275 draws per frame in the menu; nothing on screen but the menu's red
selection bar. From the log:

- **26 s of black screen and stalling audio at startup**: the renderer thread read the 221 MB DKSH cache byte by byte
  (`istreambuf_iterator`) and kept three copies of it (660 MB). SDK patch 0011: one read, entries moved.
- **Almost every draw skipped, "VS not identified"** (975,529 of 1,100,054 in 10 s): the global shader cache
  (`GlobalShaderCache-Xbox360.bin`, 115 shaders: menus, fonts, movies, post-processing) was not in the set, and the
  similarity step refused ties between UE3 vertex shaders that share their code. Now in the default set; ties are
  broken by the fetch words D3D does not patch, and an exact tie takes the first. Unknown microcode is written to
  `shaders_unknown/` (up to 2,000) for the next round.
- `k_16_16` / `k_16_16_16_16` render targets and resolves (formats 4/5, copy 25/26) were rejected: SDK patch 0010
  maps them to half floats.
- 7 shaders created at run time (not in any package) were dumped by the create hooks: `gow2/out/console_dumps/`.

### Run 2 (2026-10-22)

The menus and UI draw; the 3D scene does not. DKSH cache loads in 3.6 s (was 26). 363,213 of 607,055 draws in 10 s
were still skipped for an unknown VS, from five distinct microcodes (`shaders_unknown/`):

- One is the scene's main vertex shader (81 words, drawing into the scene target): the shader that reads
  POSITION0-3 and was left out of the library. POSITION2/3 now have locations 16/17 (translator `USAGE_LOCATIONS`,
  SDK patch 0012: `UbicacionDeUso`, 32 attribute slots).
- Four (VS of 15, 24, 27 words and a 9-word PS, loaded with IM_LOAD_IMMEDIATE at address 0) match nothing in the
  packages: Direct3D's own internal shaders (clears); emulated as clears where they have that shape.
- Rectangle lists (primitive 8) were rejected by the renderer: UE3 draws its full-screen passes with them. The ring
  reader now turns each three-vertex rectangle into a quad (fourth vertex = a + b - corner, the corner opposite the
  longest edge) in a guest buffer of its own (`ExpandRectList`), for one float stream; deko3d draws quad lists.

### Open

- Two packages do not expand offline (`SP_Hospital_03`, `SP_Sinkhole_W`: an LZX error in one block; window and frame
  padding ruled out). The game itself reads them with its own XMemDecompress, so this only means their shaders are
  dumped on the console by the create hooks instead (`shaders_dump/`).
- 28 vertex shaders index more than 16 manual vertex fetch descriptors (uam: "array index must be < 16").
- The full set (all maps) would be ~470 MB of library: later, per-chapter sets or loading on demand.

## Where the code comes from

[gears-of-war-2-pc-port](https://github.com/santiquiroz/gears-of-war-2-pc-port) (santiquiroz, BSD-3-Clause), commit
`19d9e793` (2026-10-20, v0.2.5): Act 1 plays on PC; on Android (Snapdragon 8 Elite) the start of Act 1 runs at the game's
30 fps cap while cool and 10-12 fps when hot, GPU-bound on Xenos emulation.

- **No generated C++ in that repository**: `rexglue codegen gow2_manifest.toml` at build time from the player's
  `default.xex` (about 230 MB of C++), then its `tools/patch_switch_traps.py`. Its hints (`hints/harvested.toml`,
  `hints/runtime.toml`, the manifest's `[entrypoint.functions]`) carry the functions discovery misses.
- **SDK version**: the PC port uses ReXGlue `923c1a5` (0.10.0.9-dev) = v0.10.0 + 9 commits (logging, window, VFS
  truncate, XAM content, socket leaks). None touches the code generator or `rex/ppc` headers, so the host `rexglue`
  here is plain v0.10.0 (plus `host-patches/0001-dump-image.patch`).
- **Executable**: retail only, title 4D53082D, Media ID 63AF211E, XEX 0.0.0.5 (2008-09-23); no title update (TU6 is
  an open request there). The PC port does not check it in code; `tools/stage_sd.py` checks the XEX header here.
- **ARM**: the PC port's Android build needed two more passes over the generated code, used here too:
  `patch_memory_barriers.py` (the codegen emits nothing for `sync`/`lwsync`/`eieio` and a `REX_UNIMPLEMENTED` throw
  for `isync`; ARM reorders loads and stores) and `patch_spin_hints.py` (an `isb` pause for the Xenon delay hints in
  spin loops; GCC gets the builtin from `make_nx_pch.py`). `BARRIERS=0` builds without the fences, to compare.
- **What the PC port's own `src/` does** is PC/Android features (options menu, mouse and keyboard, co-op, texture
  packs, Coalesced overrides, update check). None of it is needed for the game to run, so none is built here. Two of
  its Windows-only guards may matter later: the runtime's UTF-8 decoder throwing on bad bytes in guest content names
  (`gow2_utf8_guard.cpp`) and the dirty-disc error box (`InstallDiscErrorSilencer`).

## Title Update 6

Three TU6 packages exist, one per retail executable: Media ID 63AF211E (0.0.0.5 -> 0.0.6.5), 33FCE762
(0.0.0.6 -> 0.0.6.6) and 51D281BE (0.0.0.7 -> 0.0.6.7); `tools/stage_sd.py` knows their `default.xexp` hashes. Each
holds a 2 MB `default.xexp` (code changes), UnrealScript patches (`Patches/ScriptPatch_{Engine,GearGame,IpDrv,
OnlineSubsystemLive}.bin`, 236 KB for GearGame) and `TU__*` ini and localization files the game reads from `update:`
(the SDK mounts `update_data_root` there).

Not used yet: the PC port's manifest hints and fixes are for 0.0.0.5 without an update (TU6 is its open issue #13),
and TU6 is mostly multiplayer balance and matchmaking. `stage_sd.py --tu` stages it in `tu6/`, apart from `game/`, for
when the base game runs. Using it later means codegen on the patched image (rexglue applies a sibling `.xexp`), the
hints found again for the new addresses, and `update_data_root` pointed at `tu6/`.

## Plan (from Dead Space 2's notes, section 7)

1. **Memory**: SDK patch 0001 (commit granule) from the start: the game commits physical memory in small pieces, and
   Horizon runs out of mappings long before memory (`2001-0103`). Title takeover only.
2. **Renderer**: no Xenos emulation on the console (a few fps). `src/gow2_gpu.cpp` is the game-neutral ring reader
   (from rexglue-sdk-nx's Halo 3 port, via DS2 and Dante): draws go to the deko3d renderer with shaders translated
   ahead of time. Expect UE3-specific work: its resolves (scene colour/depth to textures for post-processing),
   `7e3` float targets, deferred clears, and the half-resolution passes the PC port's patch 0026 talks about.
3. **Shaders**: UE3 keeps compiled shaders in the shader caches of its cooked packages, not in the executable. They are
   LZX-compressed (`.xxx`), so `shaders/gow2_extract_shaders.cpp` expands each package (fully compressed or chunk
   compressed, the formats the PC port's `tools/ue3` documents) and scans it for 2008-format containers. Not yet run
   on the game: whether UE3 stores the whole D3D container (what XenosRecomp reads) is the first thing to check. If it
   does not, the console dumps them at creation instead (`src/gow2_shader_dump.cpp`), which needs the addresses of
   the XDK's `D3DDevice_Create{Vertex,Pixel}Shader` in `codegen/names.toml`: `tools/match_funcs.py` /
   `tools/fuzzy_find.py` against Dead Space 2's named tree (XDK of 2010 vs 2008: may need the fuzzy search).
4. **CPU**: the per-thread `mtmsrd` state (PCH), FPCR flush mode fixed (0004), yield backoff (0005) and XMA lock fixes
   (0006) come with the SDK patches. UE3's job threads, physics (PhysX on 360) and the game thread on three 1 GHz cores
   will likely be CPU-bound in fights; profile with `telemetria.flag` early.
5. **Video**: Bink movies decoded in software by the recompiled code (as DS2's VP6 was): expect stutter at stock
   clocks.
6. **Audio**: XAudio2 + XMA through the SDK (FFmpeg).

## Build pieces

| Piece | From | Change for Gears of War 2 |
| --- | --- | --- |
| `tools/codegen.sh` | Dante (Hell's Gate fetch) | Fetches the PC port; its manifest + hints; its three fixers; header check instead of hashes |
| `tools/stage_sd.py` | Dante | One disc, no title update, XEX header check (title, Media ID, version), `--folder`, `--check` |
| `tools/shaders.sh`, `shaders/gow2_extract_shaders.cpp` | Dante's executable scan | UE3 package expansion (LZX via the SDK's libmspack) |
| `tools/make_nx_pch.py` | DS2 / Dante | + `__builtin_arm_isb` for GCC |
| `src/gow2_app.h` | Dante | No TU check, no setjmp/longjmp fiber fix (the PC port needs none), refuses a `default.xexp` |
| `src/gow2_gpu.cpp`, `gow2_shaders.*`, `gow2_cpu.*` | Dante (game-neutral) | Renamed only |
| `src/gow2_shader_dump.cpp` | Dante | Built only when `codegen/names.toml` names both Create*Shader functions |
| `sdk-patches/0001-0008` | DS2 0001-0006, Dante 0007-0008 | As they are |
