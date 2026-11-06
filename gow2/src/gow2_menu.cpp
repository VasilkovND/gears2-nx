// gears2 - the port's options menu: ZL + ZR + Minus opens it over the game (the presenter paints it,
// sdk-patches/0050: no ImGui, which would move presentation to the UI thread), and while it is open the game sees
// the controller idle. Up / Down choose the row, Left / Right change it, A saves, B closes. One row: the scene
// resolution (gow2_resolution.h) — 720p for overclocked consoles, 540p for stock clocks; the game sets up its
// renderer at start, so a new choice applies at the next start. Ported from ds2-nx (src/ds2_menu.cpp, with its
// vibration and skip rows taken out).

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <rex/input/filtro_mando.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/superposicion.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

#include "gow2_resolution.h"

namespace gow2::menu {
namespace {

#if defined(__SWITCH__)
constexpr uint64_t kCombinacion = HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Minus;
constexpr uint64_t kIzquierda = HidNpadButton_Left | HidNpadButton_StickLLeft;
constexpr uint64_t kDerecha = HidNpadButton_Right | HidNpadButton_StickLRight;
constexpr uint64_t kAceptar = HidNpadButton_A;
constexpr uint64_t kCerrar = HidNpadButton_B;
#else
constexpr uint64_t kCombinacion = 0, kIzquierda = 0, kDerecha = 0, kAceptar = 0, kCerrar = 0;
#endif

struct Estado {
  std::mutex candado;
  bool abierto = false;
  bool baja = false;       // the row's choice: 540p, false for 720p
  std::string aviso;        // the last line: what happened
  std::array<uint64_t, 8> antes{};  // buttons held at each slot's previous poll
};
Estado& ElEstado() {
  static Estado e;
  return e;
}

void Pintar(const Estado& e) {
  const uint32_t guardada = resolucion::Guardada();
  const char* res = e.baja ? "540P   960X540  (STOCK CLOCKS)" : "720P  1280X720  (OVERCLOCK)";
  std::vector<std::string> lineas = {
      "GEARS OF WAR 2 - OPTIONS",
      std::string("> ") + "RESOLUTION:  < " + res + " >",
      "    SAVED " + (guardada ? std::to_string(guardada) + "P" : std::string("AUTO")) +
          " (APPLIES AT THE NEXT START)",
      "LEFT/RIGHT: CHANGE   A: SAVE   B: CLOSE",
      e.aviso,
  };
  rex::ui::FijarSuperposicion(std::move(lineas));
}

bool Filtro(size_t ranura, uint64_t botones) {
  Estado& e = ElEstado();
  std::lock_guard<std::mutex> cerrojo(e.candado);
  const size_t i = ranura < e.antes.size() ? ranura : e.antes.size() - 1;
  const uint64_t nuevos = botones & ~e.antes[i];
  e.antes[i] = botones;
  if (!e.abierto) {
    if ((botones & kCombinacion) != kCombinacion || !(nuevos & kCombinacion)) return false;
    e.abierto = true;
    e.baja = resolucion::Elegida() == 540;
    e.aviso.clear();
    REXLOG_INFO("gow2: menu de opciones abierto");
    Pintar(e);
    return true;
  }
  if (nuevos & kCerrar) {
    e.abierto = false;
    rex::ui::FijarSuperposicion({});
    REXLOG_INFO("gow2: menu de opciones cerrado");
    return true;
  }
  if (nuevos & (kIzquierda | kDerecha)) {
    e.baja = !e.baja;
    e.aviso.clear();
    Pintar(e);
  } else if (nuevos & kAceptar) {
    const uint32_t valor = e.baja ? 540 : 720;
    const bool ok = resolucion::Guardar(valor);
    e.aviso = ok ? "SAVED. APPLIES AT THE NEXT START" : "COULD NOT SAVE (SD CARD?)";
    REXLOG_INFO("gow2: resolucion guardada: {}p ({})", valor, ok ? "ok" : "FALLO");
    Pintar(e);
  }
  return true;  // the game does not see the controller while the menu is open
}

}  // namespace

void Instalar() { rex::input::FijarFiltroMando(&Filtro); }

}  // namespace gow2::menu
