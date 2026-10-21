#!/bin/bash
# The deko3d shader library and DKSH cache, from your own copy: shaders are pulled out of the game's cooked packages
# (shaders/gow2_extract_shaders.cpp: Unreal Engine 3 keeps them in the shader caches inside GearGame/CookedXenon),
# translated ahead of time (shaders/build_library.sh) and compiled for the console's GPU, so the game never compiles a
# shader while it runs. Containers the console dumped (shaders_dump/ beside the NRO: the ones the packages did not have)
# are added with EXTRA=<that folder>. Nothing here is committed.
#
# The game has 35,886 different shaders (every material of every map has its own); all of them would be a ~470 MB
# library. By default the set is the startup packages (engine, game, menus) and Act 1's hospital maps (SP_Hospital*):
# 10,371. ALL=1 takes every package, PACKAGES="<glob> ..." (file names in GearGame/CookedXenon) chooses them.
#
#   gow2/tools/shaders.sh <game folder>   -> gow2/out/shaders/gow2_shaders.dslib and shadercache_dksh.bin (beside the NRO)
#
# Needs the Vulkan SDK's dxc and spirv-val (VULKAN_BIN, see shaders/README.md).
set -euo pipefail
[ $# -eq 1 ] || { sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
GAME=$(cd "$1" && pwd)
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/gow2/out"
[ -d "$GAME/GearGame" ] || { echo "$GAME/GearGame not found"; exit 1; }
MSPACK="$ROOT/sdk/thirdparty/libmspack/libmspack/mspack"
mkdir -p "$OUT/bin"
if [ ! -x "$OUT/bin/gow2_extract_shaders" ] || [ "$ROOT/shaders/gow2_extract_shaders.cpp" -nt "$OUT/bin/gow2_extract_shaders" ]; then
  clang -O2 -w -I"$MSPACK" -c "$MSPACK/lzxd.c" -o "$OUT/bin/lzxd.o"
  clang++ -std=c++20 -O2 -I"$MSPACK" -I"$ROOT/sdk/thirdparty/xxHash" "$ROOT/shaders/gow2_extract_shaders.cpp" \
    "$OUT/bin/lzxd.o" -o "$OUT/bin/gow2_extract_shaders"
fi
COOKED="$GAME/GearGame/CookedXenon"
if [ "${ALL:-0}" = 1 ]; then
  INPUTS=("$GAME/GearGame")
else
  PACKAGES="${PACKAGES:-Core.xxx Engine.xxx GameFramework.xxx GearGame.xxx GearGame_P.xxx GearStart.xxx IpDrv.xxx OnlineSubsystemLive.xxx Startup_*.xxx SP_Hospital*.xxx}"
  INPUTS=()
  for g in $PACKAGES; do
    for f in "$COOKED"/$g; do [ -f "$f" ] && INPUTS+=("$f"); done
  done
fi
rm -rf "$OUT/shaders_extracted"
"$OUT/bin/gow2_extract_shaders" "$OUT/shaders_extracted" "${INPUTS[@]}" | tail -3 || true
if [ -n "${EXTRA:-}" ]; then
  for f in "$EXTRA"/*.bin; do
    [ -f "$f" ] && cp -n "$f" "$OUT/shaders_extracted/" || true
  done
fi
echo "shaders: $(ls "$OUT/shaders_extracted" | grep -c '^vs_') VS, $(ls "$OUT/shaders_extracted" | grep -c '^ps_') PS"
# The variants the console compiled in the background in earlier sessions (tools/dksh_variants.py) go in the cache too.
DKSH=1 VARIANTES="$ROOT/gow2/dksh_variants.txt" "$ROOT/shaders/build_library.sh" "$OUT/shaders_extracted" "$OUT/shaders"
ls -la "$OUT/shaders/gow2_shaders.dslib" "$OUT/shaders/shadercache_dksh.bin"
