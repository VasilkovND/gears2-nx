# gears2-nx

Gears of War 2 (Xbox 360, retail disc) running natively on the Nintendo Switch. It's not an emulator: the game's
PowerPC code is statically recompiled to C++ with [ReXGlue](https://github.com/rexglue/rexglue-sdk), with the codegen
manifest, function hints and generated-code fixes of
[gears-of-war-2-pc-port](https://github.com/santiquiroz/gears-of-war-2-pc-port) (the PC port), built for the Switch as
a homebrew NRO, and frames are drawn by a native deko3d renderer from the game's translated shaders. It follows
[ds2-nx](https://github.com/VasilkovND/ds2-nx), the Dead Space 2 port, and the Dante's Inferno port.

> [!NOTE]
> You need your own copy of the game (the retail disc; no title update). This repository contains no game data or
> game code: the code, the shaders and the NRO are all made on your computer from your copy.

Status: **0.1.0, in development** — the build tools are ready; not built from the game or tested on a console yet.
Installation: [INSTALL.md](INSTALL.md). Plan and porting notes: [gow2/PORTING.md](gow2/PORTING.md).

## Layout

| Folder | Contents |
| --- | --- |
| `gow2/` | The game project: CMake, Switch glue (`src/`: app, GPU ring reader and renderer glue, shader library and create hooks, CPU), build scripts (`tools/`), hook names for the codegen (`codegen/`), our SDK patches (`sdk-patches/`) and host-tool patches (`host-patches/`) |
| `shaders/` | Shader toolchain: extraction from the game's cooked UE3 packages, Xenos microcode -> HLSL -> SPIR-V -> shader library and DKSH cache ([shaders/README.md](shaders/README.md)) |
| `sdk/` | **Submodule** -> [`rexglue-sdk-nx`](https://github.com/nx-mod/rexglue-sdk-nx) (`switch` branch): the ReXGlue SDK + Horizon (Switch) layer + deko3d renderer. Our changes to it live as patches in `gow2/sdk-patches/`, applied by the build script |
| `tools/switch/cmake/` | `switch-devkitA64.cmake`, the CMake toolchain file for devkitA64 |

## Build

devkitA64, libnx and deko3d from [devkitPro](https://devkitpro.org) (`sudo dkp-pacman -S switch-dev deko3d`), CMake,
Ninja, Python 3, clang and the [Vulkan SDK](https://vulkan.lunarg.com) (`dxc`, `spirv-val`). On a Mac:

```sh
git submodule update --init sdk
python3 gow2/tools/stage_sd.py --disc "Gears of War 2.iso" -o sd/gears2-nx
gow2/tools/build_mac.sh sd/gears2-nx/game
```

`build_mac.sh` runs, in order:

1. `gow2/tools/codegen.sh`: fetches gears-of-war-2-pc-port and ReXGlue v0.10.0 at pinned commits, builds ReXGlue's
   `rexglue` tool for the Mac, generates the C++ from your `default.xex` with the PC port's manifest and hints plus
   `gow2/codegen/names.toml`, and applies the PC port's fixes (jump tables and unresolved branches, ARM memory
   barriers, spin-loop pauses).
2. `gow2/tools/shaders.sh`: expands the cooked packages (`GearGame/CookedXenon`, LZX), extracts the shader containers
   from their shader caches and builds `gow2_shaders.dslib` and `shadercache_dksh.bin`.
3. The NRO: `gow2/out/sw/gow2.nro`.

## SD card

```
sdmc:/switch/gears2-nx/
    gears2-nx.nro             gow2/out/sw/gow2.nro
    gow2_shaders.dslib        gow2/out/shaders/
    shadercache_dksh.bin      gow2/out/shaders/
    game/                     the extracted disc (default.xex, GearGame/), by stage_sd.py
    telemetria.flag           optional, debugging only
```

Start it with title takeover (hold R while starting any game, then the Homebrew Menu): applet mode does not have the
memory.

## Credits

- **[santiquiroz](https://github.com/santiquiroz/gears-of-war-2-pc-port)** and the gow2-recomp contributors: the PC
  port of Gears of War 2. Its codegen manifest, function hints and generated-code fixes are what makes the recompiled
  code run; they are fetched at build time (BSD-3-Clause).
- **[nx-mod](https://github.com/nx-mod/rexglue-sdk-nx)**: rexglue-sdk-nx, the Switch fork of the SDK, and its Halo 3
  port, where the PM4 ring reader and the universal deko3d renderer started.
- **[StevensND](https://github.com/StevensND/nfsmw-nx)**: NFSMW-NX: the Switch layer, deko3d renderer and shader tools.
- **[Tom Clay](https://github.com/rexglue/rexglue-sdk)**: the ReXGlue SDK, built on the work of the
  **[Xenia](https://xenia.jp)** team.
- **[hedge-dev](https://github.com/hedge-dev/XenosRecomp)**: XenosRecomp.
- **[centollOS](https://github.com/centollOS/SwitchWakerHD)**: the uam shader compiler patches.
- **[devkitPro](https://devkitpro.org) and [switchbrew](https://github.com/switchbrew/libnx)**: devkitA64, libnx
  and deko3d. **[Atmosphère](https://github.com/Atmosphere-NX/Atmosphere)**: the custom firmware.
- The other libraries listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Legal

Unofficial fan port with no affiliation to Microsoft or Epic Games. "Gears of War" is a trademark of Microsoft. You
provide your own legally obtained copy of the game. No prebuilt files are distributed: the NRO contains code
recompiled from the game and the shader library the game's own shaders, so both are made on your own computer from
your copy ([INSTALL.md](INSTALL.md)). Running homebrew requires custom firmware, which violates Nintendo's terms and
can get a console banned.

Source code is under the GPL-3.0 license (see [LICENSE](LICENSE)). The SDK changes are under the SDK's BSD-3-Clause
license, and the shader translator changes under MIT.
