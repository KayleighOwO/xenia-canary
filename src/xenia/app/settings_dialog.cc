/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/settings_dialog.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/imgui/imgui.h"
#include "xenia/app/emulator_window.h"
#include "xenia/app/launcher_format.h"
#include "xenia/app/launcher_theme.h"
#include "xenia/base/filesystem.h"
#include "xenia/emulator.h"
#include "xenia/ui/file_picker.h"

namespace xe {
namespace app {

namespace {

using Widget = SettingsDialog::Widget;
using QuickItem = SettingsDialog::QuickItem;
using QuickSection = SettingsDialog::QuickSection;

// The curated page. Only options that exist are shown, so entries for other
// platforms are harmless. Anything not listed is still reachable on the "All
// settings" page.
const std::vector<QuickSection>& QuickSections() {
  static const std::vector<QuickSection> sections = {
      {"Graphics",
       {
           {"gpu",
            "Graphics backend",
            Widget::kChoice,
            "Which graphics API to emulate the GPU with. Automatic is "
            "right for most systems.",
            true,
            0,
            0,
            nullptr,
            {{"any", "Automatic"},
             {"vulkan", "Vulkan"},
             {"d3d12", "Direct3D 12 (Windows only)"},
             {"null", "None (no video output)"}}},
           {"vsync",
            "Vertical sync",
            Widget::kToggle,
            "Prevents screen tearing. Turn off to run faster than your "
            "display's refresh rate.",
            false,
            0,
            0,
            nullptr,
            {}},
           {"framerate_limit",
            "Frame rate limit",
            Widget::kSlider,
            "Maximum frames per second. 0 means automatic: 60 with vertical "
            "sync, unlimited without.",
            false,
            0,
            240,
            "%d",
            {}},
           {"draw_resolution_scale_x",
            "Resolution scale (width)",
            Widget::kSlider,
            "Renders at a multiple of the original resolution. Higher looks "
            "sharper but is much slower, and can break some games. Keep "
            "width and height equal.",
            false,
            1,
            4,
            "%dx",
            {}},
           {"draw_resolution_scale_y",
            "Resolution scale (height)",
            Widget::kSlider,
            "",
            false,
            1,
            4,
            "%dx",
            {}},
           {"anisotropic_override",
            "Anisotropic filtering",
            Widget::kChoice,
            "Keeps textures sharp at oblique angles. Forcing it can rarely "
            "cause visual glitches.",
            false,
            0,
            0,
            nullptr,
            {{"-1", "Game default"},
             {"0", "Off"},
             {"2", "2x"},
             {"3", "4x"},
             {"4", "8x"},
             {"5", "16x"}}},
           {"postprocess_antialiasing",
            "Anti-aliasing",
            Widget::kChoice,
            "Smooths jagged edges in the final image.",
            false,
            0,
            0,
            nullptr,
            {{"", "None"},
             {"fxaa", "FXAA"},
             {"fxaa_extreme", "FXAA (extreme quality)"}}},
           {"postprocess_scaling_and_sharpening",
            "Upscaling and sharpening",
            Widget::kChoice,
            "How the image is stretched to your window. FSR gives the best "
            "upscaling; CAS sharpens.",
            false,
            0,
            0,
            nullptr,
            {{"", "Bilinear (default)"},
             {"cas", "AMD CAS (sharpening)"},
             {"fsr", "AMD FSR (upscaling)"}}},
           {"present_letterbox",
            "Keep aspect ratio",
            Widget::kToggle,
            "Shows black bars instead of stretching the image.",
            false,
            0,
            0,
            nullptr,
            {}},
           {"fullscreen",
            "Start in fullscreen",
            Widget::kToggle,
            "",
            true,
            0,
            0,
            nullptr,
            {}},
       }},
      {"Audio",
       {
           {"apu",
            "Audio backend",
            Widget::kChoice,
            "",
            true,
            0,
            0,
            nullptr,
            {{"any", "Automatic"},
             {"xaudio2", "XAudio2 (Windows only)"},
             {"sdl", "SDL"},
             {"nop", "None (silent)"}}},
           {"mute", "Mute", Widget::kToggle, "", false, 0, 0, nullptr, {}},
           {"xma_decoder",
            "Game audio decoder",
            Widget::kChoice,
            "Version of the decoder for compressed game audio. Try another "
            "one if a game has crackling or missing sound.",
            false,
            0,
            0,
            nullptr,
            {{"new", "New (default)"},
             {"old", "Old"},
             {"master", "Master (original Xenia)"},
             {"fake", "None (no game audio)"}}},
           {"use_dedicated_xma_thread",
            "Decode audio on a separate thread",
            Widget::kToggle,
            "",
            false,
            0,
            0,
            nullptr,
            {}},
           {"enable_xmp",
            "Music player support",
            Widget::kToggle,
            "Lets games play your own music (Xbox Music Player).",
            false,
            0,
            0,
            nullptr,
            {}},
       }},
      {"Input",
       {
           {"hid",
            "Input backend",
            Widget::kChoice,
            "",
            true,
            0,
            0,
            nullptr,
            {{"any", "Automatic"},
             {"xinput", "XInput (Windows only)"},
             {"sdl", "SDL (controllers)"},
             {"winkey", "Keyboard (Windows only)"},
             {"nop", "None"}}},
           {"vibration",
            "Controller vibration",
            Widget::kToggle,
            "",
            false,
            0,
            0,
            nullptr,
            {}},
           {"guide_button",
            "Send the Guide button to games",
            Widget::kToggle,
            "",
            false,
            0,
            0,
            nullptr,
            {}},
           {"controller_hotkeys",
            "Controller hotkeys",
            Widget::kToggle,
            "",
            false,
            0,
            0,
            nullptr,
            {}},
           {"left_stick_deadzone_percentage",
            "Left stick deadzone",
            Widget::kSlider,
            "Ignores small movements of the stick. 0 is none.",
            false,
            0,
            1,
            "%.2f",
            {}},
           {"right_stick_deadzone_percentage",
            "Right stick deadzone",
            Widget::kSlider,
            "",
            false,
            0,
            1,
            "%.2f",
            {}},
           {"keyboard_mode",
            "Keyboard as controller",
            Widget::kChoice,
            "",
            false,
            0,
            0,
            nullptr,
            {{"0", "Disabled"},
             {"1", "Enabled"},
             {"2", "Passthrough (needs a controller connected)"}}},
           {"keyboard_user_index",
            "Keyboard controller port",
            Widget::kSlider,
            "Which controller port the keyboard acts as.",
            false,
            0,
            3,
            "Port %d",
            {}},
       }},
      {"Games",
       {
           {"apply_patches",
            "Apply game patches",
            Widget::kToggle,
            "",
            false,
            0,
            0,
            nullptr,
            {}},
           {"apply_title_update",
            "Apply title updates",
            Widget::kToggle,
            "",
            false,
            0,
            0,
            nullptr,
            {}},
           {"show_achievement_notification",
            "Show achievement notifications",
            Widget::kToggle,
            "",
            false,
            0,
            0,
            nullptr,
            {}},
           {"discord",
            "Discord rich presence",
            Widget::kToggle,
            "Show the game you're playing on your Discord profile.",
            true,
            0,
            0,
            nullptr,
            {}},
       }},
      {"Interface",
       {
           {"library_show_on_startup",
            "Show the game library on startup",
            Widget::kToggle,
            "",
            true,
            0,
            0,
            nullptr,
            {}},
           {"window_size_x",
            "Window width",
            Widget::kNumber,
            "",
            true,
            320,
            7680,
            nullptr,
            {}},
           {"window_size_y",
            "Window height",
            Widget::kNumber,
            "",
            true,
            240,
            4320,
            nullptr,
            {}},
           {"font_size",
            "Interface text size",
            Widget::kSlider,
            "Size of the text in menus and windows like this one.",
            true,
            10,
            28,
            "%d",
            {}},
       }},
      {"Folders",
       {
           {"content_root",
            "Game data folder",
            Widget::kDirectory,
            "Where saves, downloadable content and profiles are kept. "
            "Leave empty for the default.",
            true,
            0,
            0,
            nullptr,
            {}},
           {"cache_root",
            "Cache folder",
            Widget::kDirectory,
            "",
            true,
            0,
            0,
            nullptr,
            {}},
           {"storage_root",
            "Settings and data folder",
            Widget::kDirectory,
            "",
            true,
            0,
            0,
            nullptr,
            {}},
       }},
  };
  return sections;
}

bool IsStartupOption(const std::string& name) {
  for (const QuickSection& section : QuickSections()) {
    for (const QuickItem& item : section.items) {
      if (item.applies_at_startup && name == item.name) {
        return true;
      }
    }
  }
  return false;
}

bool ToInt64(const SettingValue& value, int64_t* out) {
  if (const int32_t* i32 = std::get_if<int32_t>(&value)) {
    *out = *i32;
  } else if (const uint32_t* u32 = std::get_if<uint32_t>(&value)) {
    *out = *u32;
  } else if (const int64_t* i64 = std::get_if<int64_t>(&value)) {
    *out = *i64;
  } else if (const uint64_t* u64 = std::get_if<uint64_t>(&value)) {
    *out = *u64 > static_cast<uint64_t>(INT64_MAX) ? INT64_MAX
                                                   : static_cast<int64_t>(*u64);
  } else {
    return false;
  }
  return true;
}

bool FromInt64(SettingType type, int64_t v, SettingValue* out) {
  switch (type) {
    case SettingType::kInt32:
      *out = static_cast<int32_t>(std::clamp<int64_t>(v, INT32_MIN, INT32_MAX));
      return true;
    case SettingType::kUint32:
      *out = static_cast<uint32_t>(std::clamp<int64_t>(v, 0, UINT32_MAX));
      return true;
    case SettingType::kInt64:
      *out = v;
      return true;
    case SettingType::kUint64:
      *out = static_cast<uint64_t>(std::max<int64_t>(v, 0));
      return true;
    default:
      return false;
  }
}

bool IsIntegerType(SettingType type) {
  return type == SettingType::kInt32 || type == SettingType::kUint32 ||
         type == SettingType::kInt64 || type == SettingType::kUint64;
}

// Choices are written as text; turn one into a value of the option's type.
bool ChoiceToValue(SettingType type, const char* text, SettingValue* out) {
  if (type == SettingType::kString || type == SettingType::kPath) {
    *out = std::string(text);
    return true;
  }
  if (IsIntegerType(type)) {
    char* end = nullptr;
    const long long parsed = std::strtoll(text, &end, 10);
    if (end == text) {
      return false;
    }
    return FromInt64(type, static_cast<int64_t>(parsed), out);
  }
  if (type == SettingType::kBool) {
    *out = std::string(text) == "true";
    return true;
  }
  return false;
}

bool EndsWith(const std::string& text, const char* suffix) {
  const size_t length = std::char_traits<char>::length(suffix);
  return text.size() >= length &&
         text.compare(text.size() - length, length, suffix) == 0;
}

bool LooksLikeDirectory(const std::string& name) {
  return EndsWith(name, "_root") || EndsWith(name, "_dir") ||
         EndsWith(name, "_folder");
}

}  // namespace

SettingsDialog::SettingsDialog(ui::ImGuiDrawer* imgui_drawer,
                               EmulatorWindow& emulator_window,
                               std::function<void()> on_closed)
    : ui::ImGuiDialog(imgui_drawer),
      emulator_window_(emulator_window),
      on_closed_(std::move(on_closed)),
      game_mode_(false) {
  Initialize();
}

SettingsDialog::SettingsDialog(ui::ImGuiDrawer* imgui_drawer,
                               EmulatorWindow& emulator_window,
                               uint32_t title_id, std::string title_name,
                               std::function<void()> on_closed)
    : ui::ImGuiDialog(imgui_drawer),
      emulator_window_(emulator_window),
      on_closed_(std::move(on_closed)),
      game_mode_(true),
      title_id_(title_id),
      title_name_(std::move(title_name)) {
  Initialize();
}

SettingsDialog::~SettingsDialog() {
  *alive_ = false;
  // Closing the window any other way (e.g. a game being launched) must not
  // lose what the user changed.
  SaveIfDirty();
}

void SettingsDialog::OnClose() {
  if (on_closed_) {
    on_closed_();
  }
}

void SettingsDialog::Initialize() {
  all_ = SettingsModel::Enumerate();
  search_text_.reserve(all_.size());
  for (const SettingInfo& info : all_) {
    search_text_.push_back(launcher::ToLowerAscii(
        info.name + " " + info.category + " " + info.description));
    if (categories_.empty() || categories_.back() != info.category) {
      categories_.push_back(info.category);
    }
  }
  if (game_mode_) {
    overrides_ = std::make_unique<GameOverrides>(title_id_);
    overrides_load_failed_ = !overrides_->Load();
  }
}

void SettingsDialog::CommitValue(const SettingInfo& info,
                                 const SettingValue& value) {
  status_.clear();
  if (game_mode_) {
    overrides_->Set(info, value);
    dirty_ = true;
    return;
  }
  if (originals_.find(info.name) == originals_.end()) {
    originals_[info.name] = SettingsModel::GetSaved(info);
  }
  if (SettingsModel::Apply(info, value)) {
    dirty_ = true;
    if (IsStartupOption(info.name)) {
      needs_restart_ = true;
    }
  }
}

void SettingsDialog::ResetSetting(const SettingInfo& info) {
  CommitValue(info, SettingsModel::GetDefault(info));
}

void SettingsDialog::RequestBrowse(const SettingInfo& info, bool directory) {
  if (picking_path_) {
    return;
  }
  picking_path_ = true;
  // The native picker runs a nested message loop, so keep it out of the draw.
  const std::shared_ptr<bool> alive = alive_;
  EmulatorWindow* window = &emulator_window_;
  SettingsDialog* self = this;
  const SettingInfo target = info;
  emulator_window_.app_context().CallInUIThreadDeferred(
      [alive, window, self, target, directory]() {
        if (!*alive) {
          return;
        }
        auto picker = ui::FilePicker::Create();
        picker->set_mode(ui::FilePicker::Mode::kOpen);
        picker->set_type(directory ? ui::FilePicker::Type::kDirectory
                                   : ui::FilePicker::Type::kFile);
        picker->set_multi_selection(false);
        picker->set_title(directory ? "Select a folder" : "Select a file");
        if (!directory) {
          picker->set_extensions({{"All Files (*.*)", "*.*"}});
        }
        std::filesystem::path chosen;
        if (picker->Show(window->window())) {
          const auto selected = picker->selected_files();
          if (!selected.empty()) {
            chosen = selected[0];
          }
        }
        if (!*alive) {
          return;
        }
        self->picking_path_ = false;
        if (!chosen.empty()) {
          self->CommitValue(target, SettingValue(xe::path_to_utf8(chosen)));
        }
      });
}

bool SettingsDialog::Save() {
  if (game_mode_) {
    if (overrides_load_failed_ || !overrides_->Save()) {
      status_ = overrides_load_failed_
                    ? "Not saved: the existing settings file for this game "
                      "has errors. Fix or delete it first."
                    : "Could not write the settings file for this game.";
      return false;
    }
  } else if (!SettingsModel::SaveGlobal()) {
    status_ = "Could not save: there is no config file to write to.";
    return false;
  }
  status_ = "Saved.";
  return true;
}

void SettingsDialog::SaveIfDirty() {
  if (!dirty_) {
    return;
  }
  if (Save()) {
    dirty_ = false;
    originals_.clear();
  }
}

void SettingsDialog::Discard() {
  if (game_mode_) {
    overrides_load_failed_ = !overrides_->Load();
  } else {
    for (const auto& entry : originals_) {
      SettingInfo info;
      if (SettingsModel::Lookup(entry.first, &info)) {
        SettingsModel::Apply(info, entry.second);
      }
    }
    originals_.clear();
  }
  dirty_ = false;
  status_ = "Changes discarded.";
}

void SettingsDialog::RequestClose() {
  SaveIfDirty();
  if (!dirty_) {
    Close();
  }
}

void SettingsDialog::OnDraw(ImGuiIO& io) {
  launcher::ScopedTheme theme;
  const float s = launcher::UiScale();
  ImGuiViewport* viewport = ImGui::GetMainViewport();

  // While a game is running, show as a panel over a dimmed screen instead of
  // replacing the whole window.
  const bool overlay = emulator_window_.emulator()->is_title_open();
  const ImGuiWindowFlags base_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoResize;
  if (overlay) {
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("##xenia_settings_scrim", nullptr,
                     base_flags | ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoScrollbar)) {
      const ImVec2 min = ImGui::GetWindowPos();
      const ImVec2 size = ImGui::GetWindowSize();
      ImGui::GetWindowDrawList()->AddRectFilled(
          min, ImVec2(min.x + size.x, min.y + size.y),
          IM_COL32(0, 0, 0, 170));
    }
    ImGui::End();
    ImGui::PopStyleVar();
    const ImVec2 size(std::min(viewport->Size.x * 0.9f, 1100.0f * s),
                      viewport->Size.y * 0.88f);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                                   viewport->Pos.y + viewport->Size.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  } else {
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
  }
  if (!first_frame_done_) {
    ImGui::SetNextWindowFocus();
    first_frame_done_ = true;
  }

  if (!ImGui::Begin("##xenia_settings", nullptr, base_flags)) {
    ImGui::End();
    return;
  }

  DrawHeader();
  const float footer_height =
      ImGui::GetFrameHeight() * 1.3f + ImGui::GetStyle().ItemSpacing.y * 2.0f;
  DrawBody(std::max(80.0f * s, ImGui::GetContentRegionAvail().y - footer_height));
  DrawFooter();

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.WantTextInput &&
      !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                       ImGuiPopupFlags_AnyPopupLevel)) {
    RequestClose();
  }
  ImGui::End();
}

void SettingsDialog::DrawHeader() {
  const float s = launcher::UiScale();
  ImGui::AlignTextToFramePadding();
  ImGui::PushFont(imgui_drawer()->GetTitleFont());
  ImGui::TextUnformatted(game_mode_ ? "Game settings" : "Settings");
  ImGui::PopFont();
  if (game_mode_) {
    ImGui::SameLine(0.0f, 16.0f * s);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s  (%08X)", title_name_.c_str(), title_id_);
  }

  ImGui::Spacing();
  for (int i = 0; i < 2; ++i) {
    const bool active = (i == 0) == (page_ == Page::kQuick);
    if (i > 0) {
      ImGui::SameLine(0.0f, 4.0f * s);
    }
    if (active) {
      ImGui::PushStyleColor(ImGuiCol_Button, launcher::AccentColor(0.9f));
    }
    if (ImGui::Button(i == 0 ? "Quick settings" : "All settings")) {
      page_ = i == 0 ? Page::kQuick : Page::kAll;
    }
    if (active) {
      ImGui::PopStyleColor();
    }
  }

  ImGui::PushStyleColor(ImGuiCol_Text, launcher::DimTextColor());
  ImGui::PushTextWrapPos(0.0f);
  if (game_mode_) {
    ImGui::TextUnformatted(
        "Tick an option to use a different value for this game only. "
        "Unticked options follow your global settings. Applies the next "
        "time the game starts.");
  } else if (emulator_window_.emulator()->is_title_open()) {
    ImGui::TextUnformatted(
        "Changes apply immediately, but some options are only read when "
        "Xenia starts.");
  }
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
  if (overrides_load_failed_) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text("The existing settings file for this game could not be read "
                "(%s). It will not be overwritten.",
                overrides_->error().c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  }
  ImGui::Spacing();
}

void SettingsDialog::DrawBody(float height) {
  ImGui::BeginChild("##settings_body", ImVec2(0.0f, height),
                    ImGuiChildFlags_Borders);
  ImGui::BeginDisabled(overrides_load_failed_);
  if (page_ == Page::kQuick) {
    DrawQuickPage();
  } else {
    DrawAllPage();
  }
  ImGui::EndDisabled();
  ImGui::EndChild();
}

void SettingsDialog::BeginRows() {
  const float s = launcher::UiScale();
  if (ImGui::BeginTable("##rows", 2,
                        ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch,
                            1.0f);
    ImGui::TableSetupColumn("##editor", ImGuiTableColumnFlags_WidthFixed,
                            360.0f * s);
    rows_open_ = true;
  } else {
    rows_open_ = false;
  }
}

void SettingsDialog::EndRows() {
  if (rows_open_) {
    ImGui::EndTable();
    rows_open_ = false;
  }
}

void SettingsDialog::DrawQuickPage() {
  int section_index = 0;
  for (const QuickSection& section : QuickSections()) {
    std::vector<std::pair<SettingInfo, const QuickItem*>> visible;
    for (const QuickItem& item : section.items) {
      SettingInfo info;
      if (!SettingsModel::Lookup(item.name, &info)) {
        continue;  // Not available on this platform/build.
      }
      if (game_mode_ && item.applies_at_startup) {
        continue;  // Can't take effect from a per-game config.
      }
      visible.emplace_back(std::move(info), &item);
    }
    if (visible.empty()) {
      continue;
    }
    ImGui::PushID(section_index++);
    ImGui::Spacing();
    ImGui::SeparatorText(section.title);
    BeginRows();
    for (const auto& entry : visible) {
      const QuickItem& item = *entry.second;
      const std::string help =
          item.help[0] ? std::string(item.help)
                       : launcher::FirstLine(entry.first.description, 220);
      DrawRow(entry.first, item.label, help, &item);
    }
    EndRows();
    ImGui::PopID();
  }
}

void SettingsDialog::DrawAllPage() {
  const float s = launcher::UiScale();
  if (game_mode_) {
    ImGui::PushStyleColor(ImGuiCol_Text, launcher::DimTextColor());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(
        "Options read when Xenia starts (graphics, audio and input backends, "
        "folders, window) have no effect here.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  }

  ImGui::SetNextItemWidth(260.0f * s);
  if (ImGui::InputTextWithHint("##settings_search", "Search settings...",
                               search_, sizeof(search_))) {
    applied_search_ = launcher::ToLowerAscii(search_);
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(180.0f * s);
  const char* preview = category_filter_ == 0
                            ? "All categories"
                            : categories_[static_cast<size_t>(category_filter_ - 1)]
                                  .c_str();
  if (ImGui::BeginCombo("##settings_category", preview)) {
    if (ImGui::Selectable("All categories", category_filter_ == 0)) {
      category_filter_ = 0;
    }
    for (size_t i = 0; i < categories_.size(); ++i) {
      if (ImGui::Selectable(categories_[i].c_str(),
                            category_filter_ == static_cast<int>(i) + 1)) {
        category_filter_ = static_cast<int>(i) + 1;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::Checkbox(game_mode_ ? "Overridden only" : "Changed only",
                  &modified_only_);
  ImGui::Spacing();

  BeginRows();
  std::string last_category;
  size_t shown = 0;
  for (size_t i = 0; i < all_.size(); ++i) {
    const SettingInfo& info = all_[i];
    if (category_filter_ != 0 &&
        info.category != categories_[static_cast<size_t>(category_filter_ - 1)]) {
      continue;
    }
    if (!launcher::ContainsInsensitive(search_text_[i], applied_search_)) {
      continue;
    }
    if (modified_only_ && !(game_mode_ ? overrides_->Has(info)
                                       : SettingsModel::IsModified(info))) {
      continue;
    }
    if (info.category != last_category) {
      last_category = info.category;
      ImGui::TableNextRow();
      ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                             ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
      ImGui::TableSetColumnIndex(0);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.82f, 0.62f, 1.0f));
      ImGui::TextUnformatted(info.category.c_str());
      ImGui::PopStyleColor();
    }
    DrawRow(info, info.name.c_str(), launcher::FirstLine(info.description, 220),
            nullptr);
    ++shown;
  }
  EndRows();
  if (shown == 0) {
    ImGui::Spacing();
    ImGui::TextDisabled("No settings match.");
  }
}

void SettingsDialog::DrawRow(const SettingInfo& info, const char* label,
                             const std::string& help, const QuickItem* spec) {
  const float s = launcher::UiScale();
  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::PushID(info.name.c_str());
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);

  bool overridden = true;
  if (game_mode_) {
    overridden = overrides_->Has(info);
    bool checked = overridden;
    if (ImGui::Checkbox("##override", &checked)) {
      if (checked) {
        CommitValue(info, SettingsModel::GetSaved(info));
      } else {
        overrides_->Remove(info);
        dirty_ = true;
        status_.clear();
      }
      overridden = checked;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
      ImGui::SetTooltip("Use a different value for this game");
    }
    ImGui::SameLine();
  }
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (!info.description.empty() &&
      ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
    launcher::WrappedTooltip(
        (info.name + "\n\n" + info.description).c_str());
  }
  if (spec && spec->applies_at_startup) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.30f, 1.0f), "(restart required)");
  }
  if (!help.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, launcher::DimTextColor());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(help.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  }
  if (!game_mode_) {
    const std::string saved = SettingsModel::Format(SettingsModel::GetSaved(info));
    const std::string in_use = SettingsModel::Format(SettingsModel::GetInUse(info));
    if (saved != in_use) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.70f, 0.30f, 1.0f));
      ImGui::PushTextWrapPos(0.0f);
      ImGui::Text("Currently using \"%s\": set by the command line or by a "
                  "game's own settings.",
                  in_use.c_str());
      ImGui::PopTextWrapPos();
      ImGui::PopStyleColor();
    }
  }

  ImGui::TableSetColumnIndex(1);
  SettingValue value = SettingsModel::GetSaved(info);
  if (game_mode_ && overridden) {
    SettingValue override_value;
    if (overrides_->Get(info, &override_value)) {
      value = std::move(override_value);
    }
  }
  const bool show_reset = !game_mode_ && SettingsModel::IsModified(info);
  const float reset_width = ImGui::CalcTextSize("Reset").x +
                            style.FramePadding.x * 2.0f + style.ItemSpacing.x;
  const float editor_width =
      std::max(80.0f * s, ImGui::GetContentRegionAvail().x -
                              (show_reset ? reset_width : 0.0f));
  ImGui::BeginDisabled(game_mode_ && !overridden);
  const bool changed = DrawEditor(info, &value, spec, editor_width);
  ImGui::EndDisabled();
  if (changed) {
    CommitValue(info, value);
  }
  if (show_reset) {
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
      ResetSetting(info);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Back to the default: %s",
                        SettingsModel::Format(SettingsModel::GetDefault(info))
                            .c_str());
    }
  }
  ImGui::PopID();
}

bool SettingsDialog::DrawEditor(const SettingInfo& info, SettingValue* value,
                                const QuickItem* spec, float width) {
  Widget widget = spec ? spec->widget : Widget::kAuto;
  if (widget == Widget::kAuto) {
    switch (info.type) {
      case SettingType::kBool:
        widget = Widget::kToggle;
        break;
      case SettingType::kString:
        widget = Widget::kText;
        break;
      case SettingType::kPath:
        widget = LooksLikeDirectory(info.name) ? Widget::kDirectory
                                               : Widget::kText;
        break;
      default:
        widget = Widget::kNumber;
        break;
    }
  }
  const bool is_path = info.type == SettingType::kPath;

  switch (widget) {
    case Widget::kToggle: {
      bool* current = std::get_if<bool>(value);
      if (!current) {
        break;
      }
      bool checked = *current;
      if (ImGui::Checkbox("##v", &checked)) {
        *value = checked;
        return true;
      }
      return false;
    }
    case Widget::kChoice: {
      if (!spec) {
        break;
      }
      const std::string current = SettingsModel::Format(*value);
      const SettingsDialog::Choice* selected = nullptr;
      for (const Choice& choice : spec->choices) {
        if (current == choice.value) {
          selected = &choice;
          break;
        }
      }
      const std::string preview =
          selected ? std::string(selected->label)
                   : (current.empty() ? std::string("(none)")
                                      : "Custom: " + current);
      bool changed = false;
      ImGui::SetNextItemWidth(width);
      if (ImGui::BeginCombo("##v", preview.c_str())) {
        for (const Choice& choice : spec->choices) {
          const bool is_selected = current == choice.value;
          if (ImGui::Selectable(choice.label, is_selected)) {
            SettingValue chosen;
            if (ChoiceToValue(info.type, choice.value, &chosen)) {
              *value = std::move(chosen);
              changed = true;
            }
          }
          if (is_selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
      return changed;
    }
    case Widget::kSlider: {
      if (!spec) {
        break;
      }
      ImGui::SetNextItemWidth(width);
      if (double* current = std::get_if<double>(value)) {
        float f = static_cast<float>(*current);
        if (ImGui::SliderFloat("##v", &f, static_cast<float>(spec->min),
                               static_cast<float>(spec->max),
                               spec->format ? spec->format : "%.2f",
                               ImGuiSliderFlags_AlwaysClamp)) {
          *value = std::round(static_cast<double>(f) * 100.0) / 100.0;
          return true;
        }
        return false;
      }
      int64_t current = 0;
      if (ToInt64(*value, &current)) {
        int v = static_cast<int>(std::clamp<int64_t>(current, INT_MIN, INT_MAX));
        if (ImGui::SliderInt("##v", &v, static_cast<int>(spec->min),
                             static_cast<int>(spec->max),
                             spec->format ? spec->format : "%d",
                             ImGuiSliderFlags_AlwaysClamp)) {
          return FromInt64(info.type, v, value);
        }
        return false;
      }
      break;
    }
    case Widget::kNumber: {
      ImGui::SetNextItemWidth(width);
      bool changed = false;
      if (double* d = std::get_if<double>(value)) {
        double v = *d;
        if (ImGui::InputDouble("##v", &v, 0.0, 0.0, "%.4g")) {
          *value = v;
          changed = true;
        }
      } else if (int32_t* i32 = std::get_if<int32_t>(value)) {
        int32_t v = *i32;
        if (ImGui::InputScalar("##v", ImGuiDataType_S32, &v)) {
          *value = v;
          changed = true;
        }
      } else if (uint32_t* u32 = std::get_if<uint32_t>(value)) {
        uint32_t v = *u32;
        if (ImGui::InputScalar("##v", ImGuiDataType_U32, &v)) {
          *value = v;
          changed = true;
        }
      } else if (int64_t* i64 = std::get_if<int64_t>(value)) {
        int64_t v = *i64;
        if (ImGui::InputScalar("##v", ImGuiDataType_S64, &v)) {
          *value = v;
          changed = true;
        }
      } else if (uint64_t* u64 = std::get_if<uint64_t>(value)) {
        uint64_t v = *u64;
        if (ImGui::InputScalar("##v", ImGuiDataType_U64, &v)) {
          *value = v;
          changed = true;
        }
      } else {
        break;
      }
      int64_t as_integer = 0;
      if (changed && spec && spec->max > spec->min && ToInt64(*value, &as_integer)) {
        FromInt64(info.type,
                  std::clamp<int64_t>(as_integer, static_cast<int64_t>(spec->min),
                                      static_cast<int64_t>(spec->max)),
                  value);
      }
      return changed;
    }
    case Widget::kText:
    case Widget::kDirectory: {
      std::string* text = std::get_if<std::string>(value);
      if (!text) {
        break;
      }
      const bool browse = is_path || widget == Widget::kDirectory;
      const ImGuiStyle& style = ImGui::GetStyle();
      const float browse_width = ImGui::CalcTextSize("Browse...").x +
                                 style.FramePadding.x * 2.0f +
                                 style.ItemSpacing.x;
      char buffer[2048];
      std::snprintf(buffer, sizeof(buffer), "%s", text->c_str());
      bool changed = false;
      ImGui::SetNextItemWidth(browse ? std::max(60.0f, width - browse_width)
                                     : width);
      if (ImGui::InputText("##v", buffer, sizeof(buffer))) {
        *value = std::string(buffer);
        changed = true;
      }
      if (browse) {
        ImGui::SameLine();
        ImGui::BeginDisabled(picking_path_);
        if (ImGui::Button("Browse...")) {
          RequestBrowse(info, widget == Widget::kDirectory ||
                                  LooksLikeDirectory(info.name));
        }
        ImGui::EndDisabled();
      }
      return changed;
    }
    default:
      break;
  }
  // Unsupported combination: show the value without allowing edits.
  ImGui::TextDisabled("%s", SettingsModel::Format(*value).c_str());
  return false;
}

void SettingsDialog::DrawFooter() {
  const float s = launcher::UiScale();
  // Always put an item on the left: the SameLine() below needs something on
  // this line to attach to, or the buttons end up next to the body above.
  ImGui::AlignTextToFramePadding();
  if (!status_.empty()) {
    ImGui::TextUnformatted(status_.c_str());
  } else if (needs_restart_) {
    ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.30f, 1.0f),
                       "Some changes need a restart of Xenia to take effect.");
  } else if (dirty_ && game_mode_) {
    ImGui::TextDisabled("Unsaved changes.");
  } else {
    ImGui::TextDisabled(game_mode_
                            ? "Changes are saved with \"Save and close\"."
                            : "Changes apply immediately and are saved when "
                              "you close this window.");
  }

  const ImGuiStyle& style = ImGui::GetStyle();
  const char* discard_label = "Discard changes";
  const char* done_label = game_mode_ ? "Save and close" : "Done";
  const float buttons_width =
      ImGui::CalcTextSize(discard_label).x + ImGui::CalcTextSize(done_label).x +
      style.FramePadding.x * 4.0f + style.ItemSpacing.x + 24.0f * s;
  float extra = 0.0f;
  if (game_mode_) {
    extra = ImGui::CalcTextSize("Remove all overrides").x +
            style.FramePadding.x * 2.0f + style.ItemSpacing.x;
  }
  ImGui::SameLine();
  const float remaining = ImGui::GetContentRegionAvail().x;
  const float total = buttons_width + extra;
  if (remaining > total) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + remaining - total);
  }
  if (game_mode_) {
    ImGui::BeginDisabled(overrides_load_failed_ || overrides_->count() == 0);
    if (ImGui::Button("Remove all overrides")) {
      for (const SettingInfo& info : all_) {
        overrides_->Remove(info);
      }
      dirty_ = true;
      status_.clear();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
  }
  ImGui::BeginDisabled(!dirty_);
  if (ImGui::Button(discard_label)) {
    Discard();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (launcher::AccentButton(done_label)) {
    RequestClose();
  }
}

}  // namespace app
}  // namespace xe
