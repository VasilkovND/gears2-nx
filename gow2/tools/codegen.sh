#!/bin/bash
# Generate the recompiled C++ of Gears of War 2 from your own default.xex (retail disc, no title update), and the
# executable's memory image. Nothing here is committed: the output is gow2/out/codegen/.
#
#   gow2/tools/codegen.sh <folder with default.xex>     (tools/stage_sd.py's game/ folder works)
#
# Steps (each skipped when already done):
#   1. gears-of-war-2-pc-port (https://github.com/santiquiroz/gears-of-war-2-pc-port, BSD-3-Clause), the PC port, at
#      the tested commit: its codegen manifest and function hints, and its scripts that fix the generated code.
#   2. ReXGlue SDK v0.10.0 for the host with our patch (host-patches/: dump the loaded image), built with the Mac's
#      clang: only its `rexglue` tool is used. The PC port uses 923c1a5 (v0.10.0 + 9 commits, none in the code
#      generator), so the generated code is the same.
#   3. The manifest: the PC port's, with its hints, plus codegen/names.toml (names for the functions we hook).
#   4. rexglue codegen, then the PC port's fixes: patch_switch_traps.py (jump tables and unresolved branches become
#      dispatches), patch_game_patches.py (its game patches, chosen in src/gow2_game_patches.cpp), and the two its
#      Android build uses on ARM: patch_memory_barriers.py (real fences for sync, lwsync,
#      eieio, isync) and patch_spin_hints.py (an ISB pause for the delay hints of spin loops). BARRIERS=0 skips the
#      fences (to compare).
set -euo pipefail

if [ $# -ne 1 ]; then
  sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
  exit 1
fi
GAME=$(cd "$1" && pwd)
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/gow2/out"
CG="$OUT/codegen"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN)}"
BARRIERS="${BARRIERS:-1}"
LOCALREGS="${LOCALREGS:-1}"

GOW2_PC_COMMIT="19d9e79313c21b8d61de61d64d517f5c9e5220e6"   # 2026-10-20, v0.2.5
REXGLUE_TAG="v0.10.0"
# SHA-256 of the default.xex dump the port was tested with (tools/stage_sd.py checks the header for the others).
XEX_SHA="c110e168abd4518c510f8358723343e129aed17eea73fe12010b8a0cd0a73bb8"   # Europe disc, 33FCE762 0.0.0.6

[ -f "$GAME/default.xex" ] || { echo "$GAME/default.xex not found"; exit 1; }
[ -f "$GAME/default.xexp" ] && { echo "$GAME/default.xexp: this port is for the executable without a title update"; exit 1; }
python3 "$ROOT/gow2/tools/stage_sd.py" --check "$GAME"
if [ -n "$XEX_SHA" ] && [ "$(shasum -a 256 "$GAME/default.xex" | cut -d' ' -f1)" != "$XEX_SHA" ]; then
  echo "warning: default.xex is not the tested one; trying anyway"
fi

# 1. The PC port
PC="$OUT/gow2-pc-port"
if [ "$(git -C "$PC" rev-parse HEAD 2>/dev/null)" != "$GOW2_PC_COMMIT" ]; then
  echo "== gears-of-war-2-pc-port $GOW2_PC_COMMIT"
  rm -rf "$PC"
  mkdir -p "$PC"
  git -C "$PC" init -q
  git -C "$PC" remote add origin https://github.com/santiquiroz/gears-of-war-2-pc-port.git
  git -C "$PC" fetch -q --depth 1 origin "$GOW2_PC_COMMIT"
  git -C "$PC" checkout -q FETCH_HEAD
fi

# 2. Host ReXGlue
HOST="$OUT/rexglue-host"
REXGLUE="$HOST/out/$(uname -s | tr '[:upper:]' '[:lower:]' | sed 's/darwin/mac/')-$(uname -m | sed 's/x86_64/amd64/')/rexglue"
if [ ! -x "$REXGLUE" ]; then
  if [ ! -d "$HOST/.git" ]; then
    echo "== ReXGlue $REXGLUE_TAG (host)"
    git clone -q --branch "$REXGLUE_TAG" --depth 1 https://github.com/rexglue/rexglue-sdk.git "$HOST"
    git -C "$HOST" submodule update --init --recursive --depth 1 -j 8
  fi
  for p in "$ROOT"/gow2/host-patches/*.patch; do
    if git -C "$HOST" apply --reverse --check "$p" 2>/dev/null; then continue; fi
    echo "== host patch: $(basename "$p")"
    git -C "$HOST" apply "$p"
  done
  echo "== build rexglue (host)"
  cmake -S "$HOST" -B "$HOST/out/host" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DREXGLUE_USE_VULKAN=ON > "$OUT/rexglue-host.log"
  cmake --build "$HOST/out/host" --target rexglue -j "$JOBS" >> "$OUT/rexglue-host.log"
  if [ "$(uname -s)" = Darwin ]; then
    # Its libraries sit beside it: find them there, so the folder can move.
    for p in $(otool -l "$REXGLUE" | awk '/LC_RPATH/{getline; getline; print $2}'); do
      install_name_tool -rpath "$p" @executable_path "$REXGLUE" 2>/dev/null || true
    done
    codesign -f -s - "$REXGLUE" 2>/dev/null || true
  fi
fi
[ -x "$REXGLUE" ] || { echo "rexglue was not built (see $OUT/rexglue-host.log)"; exit 1; }

# 3. Project folder: the executable (copied: codegen refuses a link out of its game root), the manifest and hints.
mkdir -p "$CG/game" "$CG/hints"
cmp -s "$GAME/default.xex" "$CG/game/default.xex" || cp "$GAME/default.xex" "$CG/game/default.xex"
for f in "$PC"/hints/*.toml; do
  cmp -s "$f" "$CG/hints/$(basename "$f")" || cp "$f" "$CG/hints/"
done
python3 - "$PC/gow2_manifest.toml" "$ROOT/gow2/codegen/names.toml" "$CG/gow2_manifest.toml" <<'PY'
import re, sys
text, extra, out = open(sys.argv[1]).read(), open(sys.argv[2]).read(), sys.argv[3]
for addr in re.findall(r'^\[entrypoint\.functions\.(0x[0-9A-Fa-f]+)\]', extra, re.M):
    if re.search(rf'^{addr} = ', text, re.M | re.I):
        # The PC manifest lists it as a bare hint (0x... = {}): the named table below replaces it.
        text = re.sub(rf'^{addr} = [^\n]*\n', '', text, flags=re.M | re.I)
new = text.rstrip() + '\n\n' + extra
try:
    if open(out).read() == new:
        sys.exit(0)
except OSError:
    pass
open(out, 'w').write(new)
PY
# A name the SDK already defines as __imp__<name> (its export stubs) would replace the game's function with that stub.
for n in $(sed -n 's/^name = "\(.*\)"$/\1/p' "$ROOT/gow2/codegen/names.toml"); do
  if grep -rqE "__imp__$n\b" "$ROOT/sdk/src" "$ROOT/sdk/include"; then
    echo "codegen/names.toml: the SDK already defines __imp__$n; pick another name"; exit 1
  fi
done

# 4. Codegen (re-run only when an input changed) and the generated-code fixes.
STAMP="$CG/inputs.sha"
INPUTS=$( (cat "$CG/game/default.xex" "$CG/gow2_manifest.toml" "$CG"/hints/*.toml "$REXGLUE" \
  "$PC"/tools/patch_switch_traps.py "$PC"/tools/patch_game_patches.py "$PC"/tools/patch_memory_barriers.py \
  "$PC"/tools/patch_spin_hints.py "$ROOT"/gow2/tools/patch_leaf_prologue.py \
  "$ROOT"/gow2/tools/patch_stack_access.py "$ROOT"/gow2/tools/patch_inline_calls.py \
  "$ROOT"/gow2/tools/patch_local_regs.py; echo "barriers=$BARRIERS localregs=$LOCALREGS check=${PLR_CHECK:-0}"
  # The hooked functions (patch_inline_calls.py keeps their calls): a new hook regenerates the code.
  grep -rhoE "(REX_HOOK_RAW|REX_HOOK|REX_STUB_LOG|REX_STUB|REX_FUNC)\( *sub_[0-9A-Fa-f]{8}" \
    "$ROOT/gow2/src" "$ROOT/sdk/src" "$ROOT/sdk/include" | sort -u) \
  | shasum -a 256 | cut -d' ' -f1)
if [ "$(cat "$STAMP" 2>/dev/null)" != "$INPUTS" ] || [ ! -f "$CG/generated/default/sources.cmake" ]; then
  # Generated and fixed in a staging folder, then only the files whose text changed are copied over generated/:
  # the others keep their dates, so after a small change (a name in names.toml) Ninja rebuilds a few units.
  echo "== codegen"
  ST="$CG/staging"
  rm -rf "$ST"
  mkdir -p "$ST"
  cp "$CG/gow2_manifest.toml" "$ST/"
  cp -R "$CG/hints" "$ST/hints"
  ln -s ../game "$ST/game"
  (cd "$ST" && REX_DUMP_IMAGE="$CG/image.bin" "$REXGLUE" --force codegen gow2_manifest.toml > "$CG/codegen.log" 2>&1) || {
    tail -20 "$CG/codegen.log"; exit 1; }
  echo "== generated-code fixes (gears-of-war-2-pc-port)"
  (cd "$ST" && python3 "$PC/tools/patch_switch_traps.py" "generated/default/*.cpp") > "$CG/patch_switch_traps.log"
  tail -3 "$CG/patch_switch_traps.log"
  # The game patches the PC port carries (xenia-canary's for this executable), each behind Gow2GamePatch(id):
  # src/gow2_game_patches.cpp turns them on or off. The one that matters here is id 1, GUseTilingMode = FALSE: the PC
  # and Android builds always run without predicated tiling, and with it the Switch build drew no world at all.
  (cd "$ST" && python3 "$PC/tools/patch_game_patches.py" "generated/default/*.cpp") > "$CG/patch_game_patches.log"
  tail -1 "$CG/patch_game_patches.log"
  if [ "$BARRIERS" = 1 ]; then
    (cd "$ST" && python3 "$PC/tools/patch_memory_barriers.py" generated/default/*.cpp) > "$CG/patch_memory_barriers.log"
    tail -3 "$CG/patch_memory_barriers.log"
  fi
  python3 - "$PC/tools" "$ST" "$ST"/generated/default/*.cpp > "$CG/patch_spin_hints.log" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import patch_spin_hints
# Its default only accepts the Android copy (out/android); ours is the staging folder.
sys.exit(patch_spin_hints.main(sys.argv[3:], allowed_root=Path(sys.argv[2]).resolve()))
PY
  tail -3 "$CG/patch_spin_hints.log"
  # Ours: functions that call nothing get the prologue without the stack guard (make_nx_pch.py, point 5b).
  (cd "$ST" && python3 "$ROOT/gow2/tools/patch_leaf_prologue.py" generated/default/*.cpp) > "$CG/patch_leaf_prologue.log"
  tail -1 "$CG/patch_leaf_prologue.log"
  # And the guest stack's accesses without volatile (make_nx_pch.py, point 6).
  (cd "$ST" && python3 "$ROOT/gow2/tools/patch_stack_access.py" generated/default/*.cpp) > "$CG/patch_stack_access.log"
  tail -1 "$CG/patch_stack_access.log"
  # And calls the compiler may inline: same-file functions no hook replaces, and the register save/restore helpers.
  (cd "$ST" && python3 "$ROOT/gow2/tools/patch_inline_calls.py" "$ROOT/gow2/src" "$ROOT/sdk/src" "$ROOT/sdk/include" \
    -- generated/default/gow2_recomp.*.cpp) > "$CG/patch_inline_calls.log"
  tail -1 "$CG/patch_inline_calls.log" | cut -c1-160
  # And the guest registers in host locals inside each function, with ctx exact only where a call or a return needs
  # it (the registers each function reads and changes, worked out over the whole call graph). LOCALREGS=0 skips it;
  # PLR_CHECK=1 builds the check version (calls verify the analysis at run time, src/gow2_guards.cpp).
  if [ "$LOCALREGS" = 1 ]; then
    (cd "$ST" && PLR_MAP="$CG/patch_local_regs.check.map" python3 "$ROOT/gow2/tools/patch_local_regs.py" generated/default/gow2_inline_helpers.h \
      "$ROOT/gow2/src" "$ROOT/sdk/src" "$ROOT/sdk/include" -- generated/default/gow2_recomp.*.cpp) \
      > "$CG/patch_local_regs.log" 2>&1 || { tail -20 "$CG/patch_local_regs.log"; exit 1; }
    tail -1 "$CG/patch_local_regs.log"
  fi
  python3 - "$ST/generated/default" "$CG/generated/default" <<'PY'
import filecmp, os, shutil, sys
new, old = sys.argv[1:]
os.makedirs(old, exist_ok=True)
names = set(os.listdir(new))
changed = 0
for n in sorted(names):
    a, b = os.path.join(new, n), os.path.join(old, n)
    if not os.path.isfile(b) or not filecmp.cmp(a, b, shallow=False):
        shutil.copy2(a, b)
        changed += 1
for n in set(os.listdir(old)) - names:
    os.remove(os.path.join(old, n))
print(f"generated/default: {changed} of {len(names)} files changed")
PY
  rm -rf "$ST"
  echo "$INPUTS" > "$STAMP"
fi
echo "codegen: $(ls "$CG"/generated/default/*.cpp | wc -l | tr -d ' ') files in $CG/generated/default, image $CG/image.bin"
