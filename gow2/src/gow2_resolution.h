// gears2 - the launch scene resolution: the game's own 1280x720 or 960x540 (three quarters, the same 16:9 shape,
// presented on the 1280x720 back buffer as usual). 960x540 is for stock clocks: the GPU does about 56 % of the pixel
// work, and fights were GPU-bound at 307.2 MHz.
//
// The choice needs both halves (gow2_resolution.cpp): game patch 8 (gow2_game_patches.cpp: sub_82806A80 builds
// the scene with the height hard-coded at 720 and the width clamped to 1280; tools/patch_scene_resolution.py
// turns both into 540 / 960) for the scene and its targets, and the SDK's video_mode_width / video_mode_height
// for the layers that read the display mode the OS reports (the UI / back buffer textures, hard 1280x720 without
// it — the cvars alone leave the scene at 1280x720, and the scene alone crops the game's own composite). As in
// ds2-nx (deadspace2/src/ds2_resolution.h, there through a hook on the EARS engine's own scene-size write).
//
// What decides, first that is set: gow2_resolucion.txt beside the NRO ("720" or "540"), then gow2_resolucion in
// gow2.toml (720 or 540), then the CPU clock: 720 from 1.5 GHz up (an overclocked console), else 540. Applies at
// the next start.

#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>

#include <rex/cvar.h>
#include <rex/filesystem.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace gow2::resolucion {

inline std::filesystem::path Fichero() { return rex::filesystem::GetExecutableFolder() / "gow2_resolucion.txt"; }

// The saved choice (720 or 540), 0 if there is none.
inline uint32_t Guardada() {
  FILE* f = std::fopen(Fichero().string().c_str(), "r");
  if (!f) return 0;
  unsigned valor = 0;
  const bool leido = std::fscanf(f, "%u", &valor) == 1;
  std::fclose(f);
  return leido && (valor == 720 || valor == 540) ? valor : 0;
}

inline bool Guardar(uint32_t valor) {
  FILE* f = std::fopen(Fichero().string().c_str(), "w");
  if (!f) return false;
  const bool escrito = std::fprintf(f, "%u\n", unsigned(valor)) > 0;
  return std::fclose(f) == 0 && escrito;
}

// The CPU clock in MHz (0 if it cannot be read).
inline uint32_t CpuMhz() {
#if defined(__SWITCH__)
  uint32_t hz = 0;
  if (R_SUCCEEDED(clkrstInitialize())) {
    ClkrstSession sesion;
    if (R_SUCCEEDED(clkrstOpenSession(&sesion, PcvModuleId_CpuBus, 3))) {
      clkrstGetClockRate(&sesion, &hz);
      clkrstCloseSession(&sesion);
    }
    clkrstExit();
  }
  return hz / 1000000;
#else
  return 0;
#endif
}

// The choice that applies: the saved one, else the default (PorDefecto, gow2_resolution.cpp).
uint32_t Elegida();

// Set the choice before the game sets up its renderer (gow2_app.h: OnPreLaunchModule).
void Aplicar();

}  // namespace gow2::resolucion
