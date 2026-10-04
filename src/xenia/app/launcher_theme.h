/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_LAUNCHER_THEME_H_
#define XENIA_APP_LAUNCHER_THEME_H_

#include "third_party/imgui/imgui.h"

namespace xe {
namespace app {
namespace launcher {

// Everything is scaled relative to the default 14 px font so the windows keep
// their proportions when the font_size option or the window DPI changes.
inline float UiScale() { return ImGui::GetFontSize() / 14.0f; }

inline ImVec4 AccentColor(float brightness = 1.0f, float alpha = 1.0f) {
  // Xbox green.
  return ImVec4(0.063f * brightness, 0.486f * brightness, 0.063f * brightness,
                alpha);
}
inline ImVec4 DimTextColor() { return ImVec4(0.56f, 0.61f, 0.64f, 1.0f); }

// Gives the windows of the library and settings UI a calm dark look without
// touching the global ImGui style (which the other dialogs rely on). Create it
// before ImGui::Begin() and keep it alive until after ImGui::End().
class ScopedTheme {
 public:
  ScopedTheme() {
    const float s = UiScale();
    const auto color = [this](ImGuiCol index, const ImVec4& value) {
      ImGui::PushStyleColor(index, value);
      ++color_count_;
    };
    const auto var = [this](ImGuiStyleVar index, float value) {
      ImGui::PushStyleVar(index, value);
      ++var_count_;
    };
    const auto var2 = [this](ImGuiStyleVar index, ImVec2 value) {
      ImGui::PushStyleVar(index, value);
      ++var_count_;
    };

    color(ImGuiCol_Text, ImVec4(0.92f, 0.94f, 0.95f, 1.0f));
    color(ImGuiCol_TextDisabled, DimTextColor());
    color(ImGuiCol_WindowBg, ImVec4(0.067f, 0.075f, 0.082f, 1.0f));
    color(ImGuiCol_ChildBg, ImVec4(0.094f, 0.106f, 0.118f, 1.0f));
    color(ImGuiCol_PopupBg, ImVec4(0.110f, 0.122f, 0.137f, 0.98f));
    color(ImGuiCol_Border, ImVec4(0.19f, 0.21f, 0.23f, 1.0f));
    color(ImGuiCol_FrameBg, ImVec4(0.140f, 0.157f, 0.176f, 1.0f));
    color(ImGuiCol_FrameBgHovered, ImVec4(0.180f, 0.200f, 0.222f, 1.0f));
    color(ImGuiCol_FrameBgActive, ImVec4(0.210f, 0.232f, 0.256f, 1.0f));
    color(ImGuiCol_Button, ImVec4(0.160f, 0.180f, 0.200f, 1.0f));
    color(ImGuiCol_ButtonHovered, ImVec4(0.215f, 0.240f, 0.265f, 1.0f));
    color(ImGuiCol_ButtonActive, ImVec4(0.250f, 0.278f, 0.305f, 1.0f));
    color(ImGuiCol_Header, AccentColor(1.0f, 0.35f));
    color(ImGuiCol_HeaderHovered, AccentColor(1.0f, 0.22f));
    color(ImGuiCol_HeaderActive, AccentColor(1.0f, 0.50f));
    color(ImGuiCol_CheckMark, AccentColor(1.8f));
    color(ImGuiCol_SliderGrab, AccentColor(1.5f));
    color(ImGuiCol_SliderGrabActive, AccentColor(1.9f));
    color(ImGuiCol_Separator, ImVec4(0.19f, 0.21f, 0.23f, 1.0f));
    color(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    color(ImGuiCol_ScrollbarGrab, ImVec4(0.25f, 0.28f, 0.30f, 1.0f));
    color(ImGuiCol_TableHeaderBg, ImVec4(0.120f, 0.135f, 0.150f, 1.0f));
    color(ImGuiCol_TableRowBgAlt, ImVec4(1.0f, 1.0f, 1.0f, 0.025f));
    color(ImGuiCol_TableBorderLight, ImVec4(0.15f, 0.17f, 0.19f, 1.0f));

    var(ImGuiStyleVar_WindowBorderSize, 0.0f);
    var(ImGuiStyleVar_ChildBorderSize, 1.0f);
    var(ImGuiStyleVar_FrameRounding, 6.0f * s);
    var(ImGuiStyleVar_ChildRounding, 8.0f * s);
    var(ImGuiStyleVar_PopupRounding, 8.0f * s);
    var(ImGuiStyleVar_GrabRounding, 6.0f * s);
    var(ImGuiStyleVar_ScrollbarRounding, 8.0f * s);
    var(ImGuiStyleVar_ScrollbarSize, 12.0f * s);
    var2(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * s, 12.0f * s));
    var2(ImGuiStyleVar_FramePadding, ImVec2(10.0f * s, 6.0f * s));
    var2(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f * s, 8.0f * s));
  }
  ~ScopedTheme() {
    ImGui::PopStyleVar(var_count_);
    ImGui::PopStyleColor(color_count_);
  }
  ScopedTheme(const ScopedTheme&) = delete;
  ScopedTheme& operator=(const ScopedTheme&) = delete;

 private:
  int color_count_ = 0;
  int var_count_ = 0;
};

// A colored "primary action" button (Play, Save...).
inline bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0)) {
  ImGui::PushStyleColor(ImGuiCol_Button, AccentColor(1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentColor(1.25f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentColor(0.8f));
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(3);
  return pressed;
}

// Tooltip that wraps long text at a readable width.
inline void WrappedTooltip(const char* text) {
  ImGui::BeginTooltip();
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
  ImGui::EndTooltip();
}

}  // namespace launcher
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_LAUNCHER_THEME_H_
