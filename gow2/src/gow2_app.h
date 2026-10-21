// gears2-nx - Gears of War 2 (Xbox 360, retail disc, no title update) on the Nintendo Switch.
//
// Where the game is, next to the NRO (sdmc:/switch/gears2-nx/ on the console):
//   game/   the extracted disc (default.xex and GearGame/)
// tools/stage_sd.py makes this layout from your ISO. The runtime maps game: and d: to that folder.
//
// The recompiled code is the retail executable's (Media ID 63AF211E, version 0.0.0.5), the one the PC port
// (gears-of-war-2-pc-port) supports: a title update's default.xexp would change the code under it, so it is refused.

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "gow2_cpu.h"
#include "gow2_gpu.h"

REXCVAR_DEFINE_STRING(gow2_game, "", "Gears2", "Carpeta del juego (disco extraido). Vacio: game/ junto al ejecutable.");

class Gears2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Gears2App>(new Gears2App(ctx, "gow2", PPCImageConfig));
  }

 protected:
  // No Xenos emulation: gow2_gpu.cpp reads the game's command ring and draws with the deko3d renderer.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (!config.graphics) config.graphics = gow2::gpu::CreateGraphicsSystem();
  }

  // game_data_root is the folder with default.xex (ConstructRuntime checks it).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) return;
    const std::string flag = REXCVAR_GET(gow2_game);
    if (!flag.empty()) {
      paths.game_data_root = flag;
      return;
    }
    const auto folder = rex::filesystem::GetExecutableFolder();
    if (!folder.empty()) paths.game_data_root = folder / "game";
  }

  void OnPostInitLogging() override { gow2::cpu::InformarSistema(); }

  void OnLoadXexImage(std::string& xex_image) override {
    std::error_code ec;
    const auto root = game_data_root();
    if (!std::filesystem::is_regular_file(root / "default.xex", ec) ||
        !std::filesystem::is_directory(root / "GearGame", ec)) {
      Fail(fmt::format("Gears of War 2: default.xex or GearGame/ is missing from {}. Make the folder with "
                       "tools/stage_sd.py (INSTALL.md).",
                       root.string()));
    }
    if (std::filesystem::exists(root / "default.xexp", ec)) {
      Fail(fmt::format("Gears of War 2: {} has a title update (default.xexp). This build runs the retail executable "
                       "without one: remove default.xexp.",
                       root.string()));
    }
    REXLOG_INFO("gow2: game at {}; loading {}", root.string(), xex_image);
  }

  void OnPreLaunchModule() override { gow2::cpu::Configurar(); }

 private:
  [[noreturn]] static void Fail(const std::string& msg) {
    REXLOG_ERROR("{}", msg);
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
    rex::FlushLogging();
    std::_Exit(1);
  }
};
