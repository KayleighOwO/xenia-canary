/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_GAME_LIBRARY_H_
#define XENIA_APP_GAME_LIBRARY_H_

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace xe {
namespace app {

enum class GameFormat : uint8_t {
  kUnknown = 0,
  kDiscImage = 1,  // .iso (XISO / XGD)
  kZarchive = 2,   // .zar
  kXex = 3,        // default.xex of an extracted game folder
};

struct LibraryFolder {
  std::filesystem::path path;
  bool recursive = true;
};

struct GameEntry {
  // The path that is handed to Emulator::LaunchPath.
  std::filesystem::path path;
  // Name learned from the title itself the last time it was launched.
  std::string title_name;
  // User supplied name, takes priority over everything else.
  std::string custom_name;
  // 0 when unknown.
  uint32_t title_id = 0;
  GameFormat format = GameFormat::kUnknown;
  uint64_t size_bytes = 0;
  // Used only to detect that a file changed between scans.
  int64_t modified_time = 0;
  // Seconds since the Unix epoch, 0 when never launched from Xenia.
  int64_t last_played = 0;
  uint32_t play_count = 0;
  bool favorite = false;

  // custom_name, then title_name, then a name derived from the file name.
  std::string DisplayName() const;
  // Eight upper-case hex digits, or an empty string when the id is unknown.
  std::string TitleIdHex() const;
};

// The list of games shown by the launcher.
//
// Games come from user-chosen folders (found by a background scan) and from
// anything launched through Xenia. Everything is persisted in library.toml in
// the storage root; icons harvested at launch are cached next to it.
//
// All public members are thread-safe. The UI is expected to poll revision()
// and re-fetch Games() when it changes.
class GameLibrary {
 public:
  explicit GameLibrary(std::filesystem::path storage_root);
  ~GameLibrary();

  GameLibrary(const GameLibrary&) = delete;
  GameLibrary& operator=(const GameLibrary&) = delete;

  void Load();

  // Folders.
  std::vector<LibraryFolder> Folders() const;
  // Returns false if the folder is already in the library.
  bool AddFolder(const std::filesystem::path& path, bool recursive);
  // Removes the folder and the games found in it, except for games that are
  // also inside another library folder.
  void RemoveFolder(const std::filesystem::path& path);
  void SetFolderRecursive(const std::filesystem::path& path, bool recursive);

  // Games.
  std::vector<GameEntry> Games() const;
  // Incremented whenever the folder list or the games change.
  uint64_t revision() const { return revision_.load(); }

  // Starts a background scan of all folders. Does nothing if one is running.
  void Rescan();
  bool is_scanning() const { return scanning_.load(); }
  std::string ScanStatus() const;

  void SetFavorite(const std::filesystem::path& path, bool favorite);
  // An empty name removes the custom name.
  void SetCustomName(const std::filesystem::path& path, std::string name);
  // Removes the game and keeps later scans from adding it back.
  void ExcludeGame(const std::filesystem::path& path);

  // Call after a title has been launched successfully. Adds the game if it is
  // not known yet, bumps its play statistics and stores what the running title
  // told us about itself (name, id, icon). icon_png may be empty.
  void RecordLaunch(const std::filesystem::path& path, uint32_t title_id,
                    std::string_view title_name,
                    std::span<const uint8_t> icon_png);

  // Where the cached icon of a title lives (the file may not exist).
  std::filesystem::path IconPath(uint32_t title_id) const;

  // Whether `path` is `folder` itself or lies somewhere below it.
  static bool IsUnderFolder(const std::filesystem::path& path,
                            const std::filesystem::path& folder);
  // Whether the scanner would list this file.
  static bool IsLibraryCandidate(const std::filesystem::path& path);
  // Reads the title id out of the (unencrypted) header of a XEX2 file.
  // Returns 0 if the data is not a XEX2 header with execution info.
  static uint32_t ParseXexTitleId(std::span<const uint8_t> header_data);

 private:
  struct Candidate {
    std::filesystem::path path;
    GameFormat format = GameFormat::kUnknown;
    uint64_t size_bytes = 0;
    int64_t modified_time = 0;
  };

  static std::string PathKey(const std::filesystem::path& path);
  static GameFormat FormatForPath(const std::filesystem::path& path);
  static uint32_t ReadTitleId(const Candidate& candidate);

  // The following require mutex_ to be held.
  GameEntry* FindLocked(const std::filesystem::path& path);
  void SaveLocked();
  void BumpLocked();

  void ScanThreadMain();
  void SetStatus(std::string status);

  std::filesystem::path library_file_;
  std::filesystem::path icon_folder_;

  mutable std::mutex mutex_;
  std::vector<LibraryFolder> folders_;
  std::vector<GameEntry> games_;
  std::set<std::string> excluded_;  // PathKey()s

  std::atomic<uint64_t> revision_{1};
  std::atomic<bool> scanning_{false};
  std::atomic<bool> stop_{false};
  std::mutex thread_mutex_;  // Guards scan_thread_.
  std::thread scan_thread_;
  mutable std::mutex status_mutex_;
  std::string status_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_GAME_LIBRARY_H_
