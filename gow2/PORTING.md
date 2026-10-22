# Porting notes: Gears of War 2

Gears of War 2 (Xbox 360, retail disc) on the Nintendo Switch, as a homebrew NRO. This file records the plan, what
carries over from Dead Space 2 ([ds2-nx](https://github.com/VasilkovND/ds2-nx), `deadspace2/PORTING.md`) and Dante's
Inferno, and every problem met on the way.

## Status (0.1.0)

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
