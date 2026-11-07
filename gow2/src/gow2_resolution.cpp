// gears2 - applying the launch scene resolution (gow2_resolution.h). Two things have to agree at 960x540, or the
// game's own composite crops: game patch 8 (gow2_game_patches.cpp: sub_82806A80's hard-coded height 720 and its
// 1280 width cap, tools/patch_scene_resolution.py) for the scene and its targets, and the SDK's video_mode_width /
// video_mode_height for the layers that read the display mode the OS reports (VdQueryVideoMode /
// VdGetCurrentDisplayInformation: the UI / back buffer textures, hard 1280x720 without it — with the scene alone
// patched, the game copied its 1280x720 UI 1:1 onto the 960x540 frame and the right and bottom were lost).

#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/flags.h>

#include "gow2_resolution.h"

REXCVAR_DEFINE_INT32(gow2_resolucion, 0, "Gears2",
                     "Resolucion de la escena al iniciar si gow2_resolucion.txt no existe: 720 (1280x720, la del "
                     "juego) o 540 (960x540, para frecuencias de serie). 0 = segun la CPU: 720 desde 1,5 GHz, si no "
                     "540. Al reiniciar")
    .range(0, 720);

namespace gow2::resolucion {

namespace {

uint32_t PorDefecto() {
  const int32_t pedida = REXCVAR_GET(gow2_resolucion);
  if (pedida == 720 || pedida == 540) return uint32_t(pedida);
  const uint32_t mhz = CpuMhz();
  return mhz == 0 || mhz >= 1500 ? 720 : 540;
}

}  // namespace

uint32_t Elegida() {
  const uint32_t guardada = Guardada();
  return guardada ? guardada : PorDefecto();
}

void Aplicar() {
  if (Elegida() == 540) {
    REXCVAR_SET(video_mode_width, 960);
    REXCVAR_SET(video_mode_height, 540);
  }
  REXLOG_INFO("gow2: escena {}p ({}; CPU {} MHz)", Elegida(),
              Guardada() ? "guardada en gow2_resolucion.txt" : "por defecto", CpuMhz());
}

}  // namespace gow2::resolucion
