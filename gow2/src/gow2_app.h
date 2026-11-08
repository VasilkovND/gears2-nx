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

#include <toml++/toml.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#if defined(__SWITCH__)
#include <switch.h>
#endif

#include "gow2_cpu.h"
#include "gow2_gpu.h"

namespace gow2 {

struct LanguageConfig {
  uint32_t user_language = 5;
  uint32_t user_country = 71;
};

inline bool IsSupportedLanguage(uint32_t language) {
  switch (language) {
    case 1:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 11:
    case 12:
      return true;
    default:
      return false;
  }
}

inline bool IsSupportedCountry(uint32_t country) { return country == 31 || country == 71; }

inline LanguageConfig ParseLanguageConfig(const toml::table& root) {
  LanguageConfig config;
  const auto* language = root.get_as<toml::table>("language");
  if (!language) {
    if (root.get("language")) {
      REXLOG_WARN("gow2: [language] must be a TOML table; using default language and country");
    }
    return config;
  }

  const auto read_setting = [language](std::string_view key, uint32_t fallback) {
    const auto* node = language->get(key);
    if (!node) {
      return fallback;
    }
    const auto value = node->value<int64_t>();
    if (!value || *value < 0 || static_cast<uint64_t>(*value) > UINT32_MAX) {
      REXLOG_WARN("gow2: [language].{} must be a non-negative 32-bit integer; using {}", key, fallback);
      return fallback;
    }
    return static_cast<uint32_t>(*value);
  };

  config.user_language = read_setting("user_language", config.user_language);
  if (!IsSupportedLanguage(config.user_language)) {
    REXLOG_WARN("gow2: unsupported user_language {}; using 5", config.user_language);
    config.user_language = 5;
  }

  config.user_country = read_setting("user_country", config.user_country);
  if (!IsSupportedCountry(config.user_country)) {
    REXLOG_WARN("gow2: unsupported user_country {}; using 71", config.user_country);
    config.user_country = 71;
  }
  return config;
}

inline void ApplyLanguageConfig(const std::filesystem::path& path) {
  std::error_code ec;
  const bool exists = std::filesystem::exists(path, ec);
  if (ec) {
    REXLOG_WARN("gow2: cannot check language config {}: {}; using defaults", path.string(), ec.message());
  } else if (!exists) {
    return;
  }

  LanguageConfig config;
  if (!ec) {
    try {
      config = ParseLanguageConfig(toml::parse_file(path.string()));
    } catch (const toml::parse_error& error) {
      REXLOG_WARN("gow2: cannot parse language config {}: {}; using defaults", path.string(), error.what());
    }
  }

  const bool language_set =
      rex::cvar::SetFlagByName("user_language", std::to_string(config.user_language));
  const bool country_set =
      rex::cvar::SetFlagByName("user_country", std::to_string(config.user_country));
  if (!language_set) {
    REXLOG_WARN("gow2: could not apply user_language from {}", path.string());
  }
  if (!country_set) {
    REXLOG_WARN("gow2: could not apply user_country from {}", path.string());
  }
}

}  // namespace gow2

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
#if defined(__SWITCH__)
    // libnx's BSD sockets. Gears of War 2 runs even its single-player campaign as a listen server: without a socket
    // the map never loads and the player is left in the empty front-end map (dark, only effects and the HUD).
    const Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
      REXLOG_WARN("gow2: socketInitializeDefault failed: 0x{:X} (no sockets: the campaign may not load)", rc);
    } else {
      REXLOG_INFO("gow2: sockets ready (libnx BSD)");
    }
#endif
  }

  // game_data_root is the folder with default.xex (ConstructRuntime checks it).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto folder = rex::filesystem::GetExecutableFolder();
    if (!folder.empty()) gow2::ApplyLanguageConfig(folder / "gears2-nx.toml");

    if (!paths.game_data_root.empty()) return;
    const std::string flag = REXCVAR_GET(gow2_game);
    if (!flag.empty()) {
      paths.game_data_root = flag;
      return;
    }
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
