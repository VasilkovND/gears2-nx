// gears2-nx - the game patches the PC port puts in the generated code (gears-of-war-2-pc-port's
// tools/patch_game_patches.py, run by tools/codegen.sh: xenia-canary's patches for this executable). Each patched
// site asks Gow2GamePatch(id) whether it is on; a cvar each, so they can be compared without a rebuild.
//
// On by default only what the Switch build needs:
//   1  GUseTilingMode = FALSE (the PC port's "Black Shading Fix", on by default there too). With predicated tiling the
//      game drew no world on the Switch, only effects and the HUD; the PC and Android builds never run with it.
// Off by default (PC improvements that cost GPU time or memory here):
//   2  texture streaming pool 196 MB, 3 16x anisotropic filtering, 4 unlocked frame rate, 5 / 6 ambient occlusion and
//      depth of field fixes for resolutions above 720p, 7 widescreen (back buffer width; 0 = the game's own 16:9).

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(gow2_parche_sin_teselas, true, "Gears2",
                    "GUseTilingMode = FALSE: el juego dibuja la escena de una vez, sin predicated tiling (parche 1)");
REXCVAR_DEFINE_BOOL(gow2_parche_streaming_texturas, false, "Gears2",
                    "Reserva de texturas de 196 MB y RHIGetTextureMemoryStats = FALSE (parche 2)");
REXCVAR_DEFINE_BOOL(gow2_parche_aniso16, false, "Gears2", "Filtrado anisotropico 16x (parche 3)");
REXCVAR_DEFINE_BOOL(gow2_parche_sin_limite_fps, false, "Gears2", "Sin el limite de 30 fps del juego (parche 4)");
REXCVAR_DEFINE_BOOL(gow2_parche_ao, false, "Gears2", "Oclusion ambiental para resoluciones mayores (parche 5)");
REXCVAR_DEFINE_BOOL(gow2_parche_dof, false, "Gears2", "Profundidad de campo para resoluciones mayores (parche 6)");

extern "C" double Gow2WidescreenHalfFov(double half_fov, int) { return half_fov; }

extern "C" int Gow2GamePatch(int id) {
  switch (id) {
    case 1: return REXCVAR_GET(gow2_parche_sin_teselas);
    case 2: return REXCVAR_GET(gow2_parche_streaming_texturas);
    case 3: return REXCVAR_GET(gow2_parche_aniso16);
    case 4: return REXCVAR_GET(gow2_parche_sin_limite_fps);
    case 5: return REXCVAR_GET(gow2_parche_ao);
    case 6: return REXCVAR_GET(gow2_parche_dof);
    default: return 0;  // 7: the game's own 16:9
  }
}
