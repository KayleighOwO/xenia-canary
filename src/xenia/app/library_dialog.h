/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_LIBRARY_DIALOG_H_
#define XENIA_APP_LIBRARY_DIALOG_H_

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "xenia/app/game_library.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"

namespace xe {
namespace ui {
class ImmediateTexture;
}  // namespace ui
}  // namespace xe

namespace xe {
namespace app {

class EmulatorWindow;

// Game icons as ImGui textures. Shared by every library window the user opens
// so textures are created once, not on each open.
class LibraryIconCache {
 public:
  LibraryIconCache(ui::ImGuiDrawer* imgui_drawer, GameLibrary* library);
  ~LibraryIconCache();

  // Call once per frame; limits how many icons are decoded per frame.
  void BeginFrame() { loads_this_frame_ = 0; }
  // Forget "no icon" results, e.g. after a game was launched and cached one.
  void RetryMissing() { ++epoch_; }
  // The texture for a title, or nullptr if there is none (yet).
  ui::ImmediateTexture* Get(uint32_t title_id);

 private:
  struct Slot {
    std::unique_ptr<ui::ImmediateTexture> texture;
    uint64_t checked_epoch = 0;
    bool checked = false;
  };

  ui::ImGuiDrawer* imgui_drawer_;
  GameLibrary* library_;
  std::map<uint32_t, Slot> slots_;
  uint64_t epoch_ = 1;
  int loads_this_frame_ = 0;
};

// The launcher: a full-window game library with folders, search, sorting,
// grid/list views and per-game actions. Shown while no title is running.
class LibraryDialog final : public ui::ImGuiDialog {
 public:
  // on_closed is called when the dialog closes itself, right before it is
  // deleted, so the owner can give up its pointer.
  LibraryDialog(ui::ImGuiDrawer* imgui_drawer, EmulatorWindow& emulator_window,
                GameLibrary& library, LibraryIconCache& icons,
                std::function<void()> on_closed);
  ~LibraryDialog() override;

 protected:
  void OnClose() override;
  void OnDraw(ImGuiIO& io) override;

 private:
  enum class Filter { kAll, kFavorites, kRecent, kFolder };
  enum class Sort { kName, kRecent, kMostPlayed, kSize, kTitleId };
  enum class View { kGrid, kList };

  // One row of the filtered, sorted list.
  struct Item {
    size_t game_index = 0;  // Into games_.
    std::string name;
    std::string lower_name;   // For sorting.
    std::string search_text;  // Lower-case name, title id and file name.
    // Cached shortened name for the width it was computed for.
    float label_width = -1.0f;
    std::string label;
  };

  void Refresh();
  void RebuildItems();

  void DrawTopBar();
  void DrawSidebar(float width, float height);
  void DrawGames(float width, float height);
  void DrawDetails(float width, float height);
  void DrawStatusBar();
  void DrawGrid(float available_width);
  void DrawList();
  void DrawEmptyState();
  void DrawPopups();
  // Shared by grid cards and list rows.
  void HandleItemInteraction(const GameEntry& game, const std::string& label,
                             const std::string& name);
  void DrawItemContextMenu(const GameEntry& game);

  void Launch(const GameEntry& game);
  void RequestAddFolder();
  void OpenRename(const GameEntry& game);
  const GameEntry* SelectedGame() const;
  const std::string& ItemLabel(Item& item, float max_width);
  static bool SortLess(Sort sort, const GameEntry& a, const Item& item_a,
                       const GameEntry& b, const Item& item_b);

  void SetView(View view);
  void SetSort(Sort sort);
  void SaveViewSettings();

  EmulatorWindow& emulator_window_;
  GameLibrary& library_;
  LibraryIconCache& icons_;
  std::function<void()> on_closed_;
  // Lets deferred callbacks tell whether this dialog still exists.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  // Snapshot of the library; refreshed when its revision changes.
  uint64_t seen_revision_ = 0;
  std::vector<GameEntry> games_;
  std::vector<LibraryFolder> folders_;
  std::vector<Item> items_;
  size_t favorite_count_ = 0;
  size_t recent_count_ = 0;
  bool items_dirty_ = true;

  // View state.
  Filter filter_ = Filter::kAll;
  std::filesystem::path folder_filter_;
  Sort sort_ = Sort::kName;
  View view_ = View::kGrid;
  char search_[128] = {};
  std::string applied_search_;  // Lower-case copy of search_.
  bool focus_search_ = false;
  int card_size_ = 150;

  // Selection (by path, so it survives refreshes).
  std::filesystem::path selected_path_;

  // Modals.
  bool want_rename_ = false;
  std::filesystem::path rename_target_;
  char rename_buffer_[256] = {};

  bool picking_folder_ = false;
  bool launching_ = false;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_LIBRARY_DIALOG_H_
