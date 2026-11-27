/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_SETTINGS_DIALOG_H_
#define XENIA_APP_SETTINGS_DIALOG_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "xenia/app/settings_model.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"

namespace xe {
namespace app {

class EmulatorWindow;

// Visual editor for the emulator configuration.
//
// Two modes:
//  * Global: edits the real configuration. Changes take effect immediately
//    and are written to the config file when the window is closed.
//  * Game: edits the per-game overrides of one title. Each option has an
//    "override" checkbox; only checked options are stored for the game.
//
// Each mode has a "Quick" page with the options most people look for, and an
// "All settings" page generated from the option registry, so every option the
// emulator has is reachable and stays in sync with the code.
class SettingsDialog final : public ui::ImGuiDialog {
 public:
  // Global settings.
  SettingsDialog(ui::ImGuiDrawer* imgui_drawer, EmulatorWindow& emulator_window,
                 std::function<void()> on_closed);
  // Settings of a single game.
  SettingsDialog(ui::ImGuiDrawer* imgui_drawer, EmulatorWindow& emulator_window,
                 uint32_t title_id, std::string title_name,
                 std::function<void()> on_closed);
  ~SettingsDialog() override;

 protected:
  void OnClose() override;
  void OnDraw(ImGuiIO& io) override;

 public:
  // How an option is presented on the Quick page. Public only so the table
  // describing the page can be defined in the .cc file.
  enum class Widget {
    kAuto,       // Chosen from the option's type.
    kToggle,     // Check box.
    kChoice,     // Drop-down of fixed values.
    kSlider,     // Slider with a range.
    kNumber,     // Number input.
    kText,       // Text input.
    kDirectory,  // Text input with a folder browser.
  };
  struct Choice {
    const char* value;  // As the option stores it ("" is allowed).
    const char* label;
  };
  struct QuickItem {
    const char* name;  // Option name in the registry.
    const char* label;
    Widget widget;
    const char* help;  // Short help; empty = use the option's description.
    bool applies_at_startup;  // Needs a restart; meaningless per game.
    double min;
    double max;
    const char* format;  // Slider format.
    std::vector<Choice> choices;
  };
  struct QuickSection {
    const char* title;
    std::vector<QuickItem> items;
  };

 private:
  enum class Page { kQuick, kAll };

  void Initialize();
  void DrawHeader();
  void DrawBody(float height);
  void DrawQuickPage();
  void DrawAllPage();
  void DrawFooter();
  void BeginRows();
  void EndRows();
  // One row: label and help on the left, editor on the right.
  void DrawRow(const SettingInfo& info, const char* label,
               const std::string& help, const QuickItem* spec);
  bool DrawEditor(const SettingInfo& info, SettingValue* value,
                  const QuickItem* spec, float width);

  // Applies an edit from the UI (live in global mode, in memory per game).
  void CommitValue(const SettingInfo& info, const SettingValue& value);
  void ResetSetting(const SettingInfo& info);
  void RequestBrowse(const SettingInfo& info, bool directory);

  void SaveIfDirty();
  bool Save();
  void Discard();
  void RequestClose();

  EmulatorWindow& emulator_window_;
  std::function<void()> on_closed_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  const bool game_mode_;
  const uint32_t title_id_ = 0;
  const std::string title_name_;
  std::unique_ptr<GameOverrides> overrides_;  // Game mode only.
  bool overrides_load_failed_ = false;

  std::vector<SettingInfo> all_;
  std::vector<std::string> search_text_;  // Parallel to all_.
  std::vector<std::string> categories_;

  Page page_ = Page::kQuick;
  char search_[96] = {};
  std::string applied_search_;
  bool modified_only_ = false;
  int category_filter_ = 0;  // 0 = all, else index + 1 into categories_.

  // Global mode: values from before the first edit, to be able to discard.
  std::map<std::string, SettingValue> originals_;
  bool dirty_ = false;
  bool needs_restart_ = false;
  std::string status_;
  bool picking_path_ = false;
  bool first_frame_done_ = false;
  bool rows_open_ = false;  // Whether BeginRows() opened a table.
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_SETTINGS_DIALOG_H_
