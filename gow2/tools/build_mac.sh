#!/bin/bash
# Build gears2-nx.nro with devkitPro's toolchain on a Mac (Apple Silicon or Intel); on Linux the same steps with
# your distribution's packages instead of Homebrew (not tested).
#
# Once:
#   1. devkitPro: install the pkg from https://github.com/devkitPro/pacman/releases (or
#      https://devkitpro.org/wiki/Getting_Started), then:  sudo dkp-pacman -S switch-dev deko3d
#   2. Homebrew tools:  brew install cmake ninja python git
#   3. The Vulkan SDK (https://vulkan.lunarg.com/, for dxc and spirv-val: the shaders)
#
# Then, from the repository root, with the game/ folder tools/stage_sd.py made (or any extracted disc: default.xex and
# GearGame/):
#   gow2/tools/build_mac.sh <game folder>
# It generates the code (tools/codegen.sh), the shaders (tools/shaders.sh, once) and the NRO:
#   gow2/out/sw/gow2.nro                   -> sdmc:/switch/gears2-nx/gears2-nx.nro
#   gow2/out/shaders/gow2_shaders.dslib    -> beside it
#   gow2/out/shaders/shadercache_dksh.bin  -> beside it
# SD=<folder> also copies those three there when they changed (for example SD=for_sd/switch/gears2-nx).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[ $# -eq 1 ] || { sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
GAME="$1"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="$DEVKITPRO/tools/bin:$PATH"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN)}"

[ -x "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++" ] || { echo "devkitA64 not found in $DEVKITPRO (step 1)"; exit 1; }
[ -f "$DEVKITPRO/libnx/lib/libdeko3d.a" ] || { echo "deko3d not installed: sudo dkp-pacman -S deko3d"; exit 1; }

cd "$ROOT"
if [ ! -f sdk/CMakeLists.txt ]; then
  echo "== sdk submodule"
  git submodule update --init sdk
fi
if [ ! -f sdk/thirdparty/uam/uam_api.h ]; then
  echo "== Switch third-party sources"
  (cd sdk && python3 tools/switch_setup.py)
fi

# Fixes to the SDK that the game needs and that are not upstream yet (gow2/sdk-patches/), in order. Several touch the
# same files, so a patch already applied cannot always be recognised by reverse-applying it alone once a later one is
# on top: what was applied is recorded in gow2/out/sdk-patches.applied (name and hash). A patch not recorded is
# applied, or recorded if it is already there (reverse-applies).
APPLIED="$ROOT/gow2/out/sdk-patches.applied"
mkdir -p "$ROOT/gow2/out"
touch "$APPLIED"
for p in "$ROOT"/gow2/sdk-patches/*.patch; do
  [ -f "$p" ] || continue
  entry="$(basename "$p") $(shasum -a 256 "$p" | cut -d' ' -f1)"
  grep -qxF "$entry" "$APPLIED" && continue
  if git -C sdk apply --check "$p" 2>/dev/null; then
    echo "== sdk patch: $(basename "$p")"
    git -C sdk apply "$p"
  elif git -C sdk apply --reverse --check "$p" 2>/dev/null; then
    echo "== sdk patch already applied: $(basename "$p")"
  else
    echo "sdk patch $(basename "$p") does not apply to sdk/ (changed by hand, or an older version of it applied?)"
    exit 1
  fi
  echo "$entry" >> "$APPLIED"
done

"$ROOT/gow2/tools/codegen.sh" "$GAME"
if [ ! -f gow2/out/shaders/shadercache_dksh.bin ] || [ "${SHADERS:-0}" = 1 ]; then
  "$ROOT/gow2/tools/shaders.sh" "$GAME"
fi

# Configure only the first time (or with RECONFIGURE=1): afterwards Ninja re-runs CMake by itself when a CMake file
# changes.
if [ ! -f gow2/out/sw/build.ninja ] || [ "${RECONFIGURE:-0}" = 1 ]; then
  echo "== configure"
  # ccache (brew install ccache), when there: a unit compiled once is not compiled again in another build folder or
  # after a change that is undone; the PCH needs the sloppiness below.
  LAUNCHER=()
  if command -v ccache > /dev/null; then
    LAUNCHER=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
  fi
  cmake -S gow2 -B gow2/out/sw -G Ninja -DCMAKE_BUILD_TYPE=Release "${LAUNCHER[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/switch/cmake/switch-devkitA64.cmake" \
    -DREXSDK_DIR="$ROOT/sdk" -DGOW2_CODEGEN_DIR="$ROOT/gow2/out/codegen" -DREXGLUE_SWITCH_DEKO3D=ON
fi

export CCACHE_SLOPPINESS="${CCACHE_SLOPPINESS:-pch_defines,time_macros,include_file_mtime,include_file_ctime}"
export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-15G}"
echo "== build ($JOBS jobs)"
cmake --build gow2/out/sw -j "$JOBS"
ls -la gow2/out/sw/gow2.nro gow2/out/shaders/gow2_shaders.dslib gow2/out/shaders/shadercache_dksh.bin
if [ -n "${SD:-}" ]; then
  mkdir -p "$SD"
  for f in sw/gow2.nro:gears2-nx.nro shaders/gow2_shaders.dslib:gow2_shaders.dslib \
           shaders/shadercache_dksh.bin:shadercache_dksh.bin; do
    cmp -s "gow2/out/${f%%:*}" "$SD/${f##*:}" || { cp "gow2/out/${f%%:*}" "$SD/${f##*:}"; echo "== copied ${f##*:} to $SD"; }
  done
fi
