// gears2 - applying the launch scene resolution (gow2_resolution.h). The game's Unreal engine asks the OS for the
// display mode (XGetVideoMode -> VdQueryVideoMode) when it sets up its renderer, so setting the SDK's
// video_mode_width / video_mode_height cvars before launch is enough: the game then creates its scene targets at
// 960x540 itself and scales to the back buffer as it always does.

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

void Aplicar() {
  const uint32_t guardada = Guardada();
  const uint32_t elegida = guardada ? guardada : PorDefecto();
  if (elegida == 540) {
    REXCVAR_SET(video_mode_width, 960);
    REXCVAR_SET(video_mode_height, 540);
  }
  REXLOG_INFO("gow2: escena {}p ({}; CPU {} MHz)", elegida,
              guardada ? "guardada en gow2_resolucion.txt" : "por defecto", CpuMhz());
}

}  // namespace gow2::resolucion
