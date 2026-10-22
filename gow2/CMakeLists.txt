# gow2 - Gears of War 2 (Xbox 360, retail disc, no title update) on the Nintendo Switch.
#
# The recompiled game code is generated from your own default.xex by tools/codegen.sh (ReXGlue's codegen with the
# manifest, hints and generated-code fixes of gears-of-war-2-pc-port, https://github.com/santiquiroz/gears-of-war-2-pc-port),
# never committed. Point GOW2_CODEGEN_DIR at its output. At configure time tools/prepare_tree.py mirrors
# generated/default/ as symlinks with a Switch PCH (tools/make_nx_pch.py). tools/build_mac.sh does all of it.
#
# Configure (Switch):
#   cmake -S gow2 -B gow2/out/sw -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=tools/switch/cmake/switch-devkitA64.cmake \
#     -DREXSDK_DIR=$PWD/sdk -DGOW2_CODEGEN_DIR=$PWD/gow2/out/codegen -DREXGLUE_SWITCH_DEKO3D=ON
#   cmake --build gow2/out/sw

cmake_minimum_required(VERSION 3.25)
project(gow2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/gow2_switch.cmake)

if(NOT REXGLUE_SWITCH_DEKO3D)
    message(FATAL_ERROR "gow2: only the deko3d build exists (-DREXGLUE_SWITCH_DEKO3D=ON)")
endif()

add_executable(gow2
    src/main.cpp
    src/gow2_gpu.cpp
    src/gow2_shaders.cpp
    src/gow2_game_patches.cpp
    src/gow2_memory_hooks.cpp
    src/gow2_guards.cpp
    src/gow2_perf_hooks.cpp
    src/gow2_cpu.cpp
    # The shader library reader, shared with the PC tools (shaders/).
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/ds_shader_library.cpp
)

gow2_setup_target(gow2)
# A variant with the mtmsrd regions as the codegen's process-wide mutex by default (src/gow2_cpu.cpp), to compare.
option(GOW2_MSR_GLOBAL "The mtmsrd regions as one global mutex by default" OFF)
if(GOW2_MSR_GLOBAL)
    set_source_files_properties(src/gow2_cpu.cpp PROPERTIES COMPILE_DEFINITIONS GOW2_MSR_GLOBAL_DEFAULT=1)
    message(STATUS "gow2: mtmsrd regions as one global mutex by default")
endif()
rexglue_switch_add_deko3d(gow2)
target_compile_definitions(gow2 PRIVATE GOW2_DEKO3D=1)
# gow2_perf_hooks.cpp decompresses the game's LZX blocks natively with the SDK's libmspack (lzxd).
target_link_libraries(gow2 PRIVATE mspack)
target_include_directories(gow2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shaders ${REXSDK_DIR}/thirdparty/xxHash)

# The shader create hooks need the two names codegen/names.toml gives the XDK's Create*Shader (not known before the
# first codegen of the game's executable).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/codegen/names.toml" GOW2_NAMES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/codegen/names.toml")
if(GOW2_NAMES MATCHES "\nname = \"gow2_D3DDevice_CreateVertexShader\"" AND
   GOW2_NAMES MATCHES "\nname = \"gow2_D3DDevice_CreatePixelShader\"")
    target_sources(gow2 PRIVATE src/gow2_shader_dump.cpp)
    message(STATUS "gow2: shader create hooks on")
else()
    message(STATUS "gow2: shader create hooks off (codegen/names.toml has no Create*Shader names yet)")
endif()

# The version (Homebrew Menu and the first lines of the log) and the commit it was built from, so a log a player sends
# says which build made it. The commit is read when CMake configures (build_mac.sh RECONFIGURE=1 refreshes it).
set(GOW2_VERSION "0.8.0")
execute_process(COMMAND git rev-parse --short=10 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE GOW2_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git status --porcelain --untracked-files=no --ignore-submodules=all WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE GOW2_SUCIO OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT GOW2_COMMIT)
    set(GOW2_COMMIT "unknown")
elseif(GOW2_SUCIO)
    string(APPEND GOW2_COMMIT "+changes")
endif()
target_compile_definitions(gow2 PRIVATE GOW2_VERSION="${GOW2_VERSION}" GOW2_COMMIT="${GOW2_COMMIT}")

# Nintendo Switch: gow2.nro. On the SD card: sdmc:/switch/gears2-nx/gears2-nx.nro, with game/ beside it
# (src/gow2_app.h finds it there). No icon from the game: the Homebrew Menu shows the default one.
if(REXGLUE_PLATFORM_SWITCH)
    rexglue_switch_add_nro(gow2
        NAME "Gears of War 2"
        AUTHOR "VasilkovND"
        VERSION "${GOW2_VERSION}")
endif()
