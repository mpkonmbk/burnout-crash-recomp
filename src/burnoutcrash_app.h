// burnoutcrash - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <filesystem>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/rex_app.h>
#include <rex/system/flags.h>

class BurnoutCrashApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BurnoutCrashApp>(new BurnoutCrashApp(ctx, "burnoutcrash",
        PPCImageConfig));
  }

  // Without --game_data_root, use a "game" folder next to the exe, falling back
  // to the project's game/ folder this build was generated from.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) return;
    auto local = rex::filesystem::GetExecutableFolder() / "game";
    paths.game_data_root = std::filesystem::is_directory(local)
                               ? local
                               : std::filesystem::path(BURNOUTCRASH_DEFAULT_GAME_DIR);
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    // Default to the Xenos GPU plugin unless --gpu_plugin overrides it.
    if (config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
    // XBLA titles run as the trial unless license bit 0 (the full-game unlock) is set. Default to the
    // full game; an explicit --license_mask on the command line or in the config still wins.
    if (rex::cvar::GetFlagSource("license_mask") == rex::cvar::Source::kDefault) {
      REXCVAR_SET(license_mask, 1u);
    }
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
};
