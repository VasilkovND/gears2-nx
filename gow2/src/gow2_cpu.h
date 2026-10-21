// gears2-nx - the recompiled code's "interrupts off" regions (gow2_cpu.cpp).

#pragma once

namespace gow2::cpu {

// Before the game's first thread runs: reads gow2_msr_cerrojo_global.
void Configurar();

// The build (version, commit) and the console (firmware, Atmosphere, title takeover or applet, handheld or docked) at
// the top of the log: what a player's log needs to say before anything else.
void InformarSistema();

}  // namespace gow2::cpu
