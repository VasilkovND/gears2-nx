// gears2 - applying the launch scene resolution (gow2_resolution.h). The choice turns game patch 8 on
// (gow2_game_patches.cpp: sub_82806A80's hard-coded height 720 and its 1280 width cap become 540 / 960,
// tools/patch_scene_resolution.py), which is read while the game builds its renderer, and the tile math in the
// same function re-reads the globals it writes, so pitch, scissor and EDRAM stay consistent. The D3D back
// buffer stays 1280x720 and the game scales its 960x540 scene onto it as it always scales its scene sizes.

#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>

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
  REXLOG_INFO("gow2: escena {}p ({}; CPU {} MHz)", Elegida(),
              Guardada() ? "guardada en gow2_resolucion.txt" : "por defecto", CpuMhz());
}

}  // namespace gow2::resolucion
