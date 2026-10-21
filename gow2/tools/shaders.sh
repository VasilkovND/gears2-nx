#!/bin/bash
# The deko3d shader library and DKSH cache, from your own copy: shaders are pulled out of the game's cooked packages
# (shaders/gow2_extract_shaders.cpp: Unreal Engine 3 keeps them in the shader caches inside GearGame/CookedXenon),
# translated ahead of time (shaders/build_library.sh) and compiled for the console's GPU, so the game never compiles a
# shader while it runs. Containers the console dumped (shaders_dump/ beside the NRO: the ones the packages did not have)
# are added with EXTRA=<that folder>. Nothing here is committed.
#
#   gow2/tools/shaders.sh <game folder>   -> gow2/out/shaders/gow2_shaders.dslib and shadercache_dksh.bin (beside the NRO)
#
# Needs the Vulkan SDK's dxc and spirv-val (VULKAN_BIN, see shaders/README.md).
set -euo pipefail
[ $# -eq 1 ] || { sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
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
rm -rf "$OUT/shaders_extracted"
"$OUT/bin/gow2_extract_shaders" "$OUT/shaders_extracted" "$GAME/GearGame" | tail -5
if [ -n "${EXTRA:-}" ]; then
  for f in "$EXTRA"/*.bin; do
    [ -f "$f" ] && cp -n "$f" "$OUT/shaders_extracted/" || true
  done
fi
echo "shaders: $(ls "$OUT/shaders_extracted" | grep -c '^vs_') VS, $(ls "$OUT/shaders_extracted" | grep -c '^ps_') PS"
# The variants the console compiled in the background in earlier sessions (tools/dksh_variants.py) go in the cache too.
DKSH=1 VARIANTES="$ROOT/gow2/dksh_variants.txt" "$ROOT/shaders/build_library.sh" "$OUT/shaders_extracted" "$OUT/shaders"
ls -la "$OUT/shaders/gow2_shaders.dslib" "$OUT/shaders/shadercache_dksh.bin"
