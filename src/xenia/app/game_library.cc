/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/game_library.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <exception>
#include <fstream>
#include <map>
#include <utility>

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/tomlplusplus/toml.hpp"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/utf8.h"
#include "xenia/vfs/devices/disc_image_device.h"
#include "xenia/vfs/devices/disc_zarchive_device.h"
#include "xenia/vfs/entry.h"
#include "xenia/vfs/file.h"
#include "xenia/xbox.h"

namespace xe {
namespace app {

namespace {

constexpr const char* kLibraryFilename = "library.toml";
constexpr const char* kIconFolderName = "library_icons";

// The XEX2 header (including the optional header table) is tiny compared to
// this, so reading this much is always enough to find the execution info.
constexpr size_t kMaxHeaderBytes = 128 * 1024;
// Don't wander arbitrarily deep into a huge directory tree.
constexpr int kMaxScanDepth = 6;

constexpr uint32_t kXex2Magic = 0x58455832;  // 'XEX2'
// xex2_header_keys::XEX_HEADER_EXECUTION_INFO
constexpr uint32_t kXexExecutionInfoKey = 0x00040006;

std::string LowerExtension(const std::filesystem::path& path) {
  return xe::utf8::lower_ascii(xe::path_to_utf8(path.extension()));
}

std::string LowerFilename(const std::filesystem::path& path) {
  return xe::utf8::lower_ascii(xe::path_to_utf8(path.filename()));
}

int64_t FileTimeToSeconds(std::filesystem::file_time_type time) {
  return static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch())
          .count());
}

// Reads default.xex out of a disc image / archive and returns its title id.
uint32_t ReadTitleIdFromDevice(vfs::Device& device) {
  if (!device.Initialize()) {
    return 0;
  }
  vfs::Entry* entry = device.ResolvePath("default.xex");
  if (!entry) {
    return 0;
  }
  vfs::File* file = nullptr;
  if (entry->Open(vfs::FileAccess::kFileReadData, &file) !=
          X_STATUS_SUCCESS ||
      !file) {
    return 0;
  }
  std::vector<uint8_t> buffer(std::min<size_t>(entry->size(), kMaxHeaderBytes));
  size_t bytes_read = 0;
  const X_STATUS status = file->ReadSync(buffer, 0, &bytes_read);
  file->Destroy();
  if (status != X_STATUS_SUCCESS) {
    return 0;
  }
  buffer.resize(std::min(bytes_read, buffer.size()));
  return GameLibrary::ParseXexTitleId(buffer);
}

uint32_t ReadTitleIdFromXexFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return 0;
  }
  std::vector<uint8_t> buffer(kMaxHeaderBytes);
  file.read(reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size()));
  buffer.resize(static_cast<size_t>(std::max<std::streamsize>(
      file.gcount(), static_cast<std::streamsize>(0))));
  return GameLibrary::ParseXexTitleId(buffer);
}

std::string GetString(const toml::table& table, std::string_view key) {
  if (const auto* node = table.get_as<std::string>(key)) {
    return node->get();
  }
  return {};
}

int64_t GetInt(const toml::table& table, std::string_view key) {
  if (const auto* node = table.get_as<int64_t>(key)) {
    return node->get();
  }
  return 0;
}

bool GetBool(const toml::table& table, std::string_view key, bool fallback) {
  if (const auto* node = table.get_as<bool>(key)) {
    return node->get();
  }
  return fallback;
}

}  // namespace

bool GameLibrary::IsUnderFolder(const std::filesystem::path& path,
                                const std::filesystem::path& folder) {
  const std::filesystem::path relative =
      path.lexically_normal().lexically_relative(folder.lexically_normal());
  if (relative.empty()) {
    return false;
  }
  return *relative.begin() != "..";
}

std::string GameEntry::DisplayName() const {
  if (!custom_name.empty()) {
    return custom_name;
  }
  if (!title_name.empty()) {
    return title_name;
  }
  // Extracted games are all called "default.xex", so use their folder instead.
  const std::filesystem::path& source =
      format == GameFormat::kXex && path.has_parent_path() ? path.parent_path()
                                                          : path;
  std::string name = xe::path_to_utf8(
      format == GameFormat::kXex ? source.filename() : source.stem());
  return name.empty() ? xe::path_to_utf8(path) : name;
}

std::string GameEntry::TitleIdHex() const {
  return title_id ? fmt::format("{:08X}", title_id) : std::string();
}

GameLibrary::GameLibrary(std::filesystem::path storage_root)
    : library_file_(storage_root / kLibraryFilename),
      icon_folder_(storage_root / kIconFolderName) {}

GameLibrary::~GameLibrary() {
  stop_ = true;
  std::lock_guard<std::mutex> lock(thread_mutex_);
  if (scan_thread_.joinable()) {
    scan_thread_.join();
  }
}

std::string GameLibrary::PathKey(const std::filesystem::path& path) {
  std::string key = xe::path_to_utf8(path.lexically_normal());
#if XE_PLATFORM_WIN32
  key = xe::utf8::lower_ascii(key);
#endif
  return key;
}

GameFormat GameLibrary::FormatForPath(const std::filesystem::path& path) {
  const std::string extension = LowerExtension(path);
  if (extension == ".iso") {
    return GameFormat::kDiscImage;
  }
  if (extension == ".zar") {
    return GameFormat::kZarchive;
  }
  if (extension == ".xex") {
    return GameFormat::kXex;
  }
  return GameFormat::kUnknown;
}

bool GameLibrary::IsLibraryCandidate(const std::filesystem::path& path) {
  switch (FormatForPath(path)) {
    case GameFormat::kDiscImage:
    case GameFormat::kZarchive:
      return true;
    case GameFormat::kXex:
      // Extracted games are full of other .xex files (dlls, modules); only the
      // entry point is a game.
      return LowerFilename(path) == "default.xex";
    default:
      return false;
  }
}

uint32_t GameLibrary::ParseXexTitleId(std::span<const uint8_t> data) {
  const auto read_be32 = [&data](size_t offset, uint32_t* out_value) {
    if (offset > data.size() || data.size() - offset < 4) {
      return false;
    }
    *out_value = (uint32_t(data[offset]) << 24) |
                 (uint32_t(data[offset + 1]) << 16) |
                 (uint32_t(data[offset + 2]) << 8) | uint32_t(data[offset + 3]);
    return true;
  };

  uint32_t magic = 0;
  if (!read_be32(0x0, &magic) || magic != kXex2Magic) {
    return 0;
  }
  uint32_t header_count = 0;
  if (!read_be32(0x14, &header_count)) {
    return 0;
  }
  // Optional headers are {key, value} pairs starting at 0x18.
  header_count = std::min<uint32_t>(header_count, 0x200);
  for (uint32_t i = 0; i < header_count; ++i) {
    const size_t entry_offset = 0x18 + size_t(i) * 8;
    uint32_t key = 0;
    uint32_t value = 0;
    if (!read_be32(entry_offset, &key) ||
        !read_be32(entry_offset + 4, &value)) {
      return 0;
    }
    if (key != kXexExecutionInfoKey) {
      continue;
    }
    // For this key the value is the offset of an xex2_opt_execution_info,
    // whose title_id lives at +0xC.
    uint32_t title_id = 0;
    return read_be32(size_t(value) + 0xC, &title_id) ? title_id : 0;
  }
  return 0;
}

uint32_t GameLibrary::ReadTitleId(const Candidate& candidate) {
  try {
    switch (candidate.format) {
      case GameFormat::kXex:
        return ReadTitleIdFromXexFile(candidate.path);
      case GameFormat::kDiscImage: {
        vfs::DiscImageDevice device("", candidate.path);
        return ReadTitleIdFromDevice(device);
      }
      case GameFormat::kZarchive: {
        vfs::DiscZarchiveDevice device("", candidate.path);
        return ReadTitleIdFromDevice(device);
      }
      default:
        return 0;
    }
  } catch (const std::exception& e) {
    XELOGW("Game library: failed to read {}: {}",
           xe::path_to_utf8(candidate.path), e.what());
  } catch (...) {
    XELOGW("Game library: failed to read {}",
           xe::path_to_utf8(candidate.path));
  }
  return 0;
}

std::filesystem::path GameLibrary::IconPath(uint32_t title_id) const {
  return icon_folder_ / fmt::format("{:08X}.png", title_id);
}

GameEntry* GameLibrary::FindLocked(const std::filesystem::path& path) {
  const std::string key = PathKey(path);
  for (GameEntry& entry : games_) {
    if (PathKey(entry.path) == key) {
      return &entry;
    }
  }
  return nullptr;
}

void GameLibrary::BumpLocked() { ++revision_; }

void GameLibrary::Load() {
  std::ifstream file(library_file_);
  if (!file.is_open()) {
    return;
  }

  toml::table root;
  try {
    root = toml::parse(file);
  } catch (const toml::parse_error& exception) {
    XELOGE("Cannot parse {}: {}", kLibraryFilename, exception.what());
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  folders_.clear();
  games_.clear();
  excluded_.clear();

  if (const toml::array* excluded = root["excluded"].as_array()) {
    for (const toml::node& node : *excluded) {
      if (const auto* value = node.as_string()) {
        excluded_.insert(value->get());
      }
    }
  }
  if (const toml::array* folders = root["folders"].as_array()) {
    for (const toml::node& node : *folders) {
      const toml::table* table = node.as_table();
      if (!table) {
        continue;
      }
      LibraryFolder folder;
      folder.path = xe::to_path(GetString(*table, "path"));
      folder.recursive = GetBool(*table, "recursive", true);
      if (!folder.path.empty()) {
        folders_.push_back(std::move(folder));
      }
    }
  }
  if (const toml::array* games = root["games"].as_array()) {
    for (const toml::node& node : *games) {
      const toml::table* table = node.as_table();
      if (!table) {
        continue;
      }
      GameEntry entry;
      entry.path = xe::to_path(GetString(*table, "path"));
      if (entry.path.empty() || FindLocked(entry.path)) {
        continue;
      }
      entry.title_name = GetString(*table, "title_name");
      entry.custom_name = GetString(*table, "custom_name");
      entry.title_id = static_cast<uint32_t>(GetInt(*table, "title_id"));
      entry.format = static_cast<GameFormat>(GetInt(*table, "format"));
      entry.size_bytes = static_cast<uint64_t>(GetInt(*table, "size"));
      entry.modified_time = GetInt(*table, "modified");
      entry.last_played = GetInt(*table, "last_played");
      entry.play_count = static_cast<uint32_t>(GetInt(*table, "play_count"));
      entry.favorite = GetBool(*table, "favorite", false);
      games_.push_back(std::move(entry));
    }
  }
  BumpLocked();
}

void GameLibrary::SaveLocked() {
  toml::table root;
  root.insert("version", int64_t(1));

  toml::array excluded;
  for (const std::string& key : excluded_) {
    excluded.push_back(key);
  }
  root.insert("excluded", std::move(excluded));

  toml::array folders;
  for (const LibraryFolder& folder : folders_) {
    folders.push_back(toml::table{{"path", xe::path_to_utf8(folder.path)},
                                  {"recursive", folder.recursive}});
  }
  root.insert("folders", std::move(folders));

  toml::array games;
  for (const GameEntry& entry : games_) {
    games.push_back(toml::table{
        {"path", xe::path_to_utf8(entry.path)},
        {"title_name", entry.title_name},
        {"custom_name", entry.custom_name},
        {"title_id", static_cast<int64_t>(entry.title_id)},
        {"format", static_cast<int64_t>(entry.format)},
        {"size", static_cast<int64_t>(entry.size_bytes)},
        {"modified", entry.modified_time},
        {"last_played", entry.last_played},
        {"play_count", static_cast<int64_t>(entry.play_count)},
        {"favorite", entry.favorite}});
  }
  root.insert("games", std::move(games));

  // Write to a temporary file first so a crash can't leave a truncated library.
  xe::filesystem::CreateParentFolder(library_file_);
  std::filesystem::path temporary = library_file_;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ofstream::trunc);
    if (!file.is_open()) {
      XELOGE("Cannot write {}", xe::path_to_utf8(temporary));
      return;
    }
    file << root;
    if (!file.good()) {
      XELOGE("Failed writing {}", xe::path_to_utf8(temporary));
      return;
    }
  }
  std::error_code ec;
  std::filesystem::rename(temporary, library_file_, ec);
  if (ec) {
    XELOGE("Cannot replace {}: {}", xe::path_to_utf8(library_file_),
           ec.message());
  }
}

std::vector<LibraryFolder> GameLibrary::Folders() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return folders_;
}

bool GameLibrary::AddFolder(const std::filesystem::path& path,
                            bool recursive) {
  if (path.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string key = PathKey(path);
  for (const LibraryFolder& folder : folders_) {
    if (PathKey(folder.path) == key) {
      return false;
    }
  }
  folders_.push_back({path, recursive});
  SaveLocked();
  BumpLocked();
  return true;
}

void GameLibrary::RemoveFolder(const std::filesystem::path& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string key = PathKey(path);
  const size_t before = folders_.size();
  folders_.erase(std::remove_if(folders_.begin(), folders_.end(),
                                [&key](const LibraryFolder& folder) {
                                  return PathKey(folder.path) == key;
                                }),
                 folders_.end());
  if (folders_.size() == before) {
    return;
  }
  // Forget the games that came from this folder, unless another library folder
  // still covers them.
  games_.erase(
      std::remove_if(games_.begin(), games_.end(),
                     [&](const GameEntry& game) {
                       if (!IsUnderFolder(game.path, path)) {
                         return false;
                       }
                       for (const LibraryFolder& remaining : folders_) {
                         if (IsUnderFolder(game.path, remaining.path)) {
                           return false;
                         }
                       }
                       return true;
                     }),
      games_.end());
  SaveLocked();
  BumpLocked();
}

void GameLibrary::SetFolderRecursive(const std::filesystem::path& path,
                                     bool recursive) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string key = PathKey(path);
  for (LibraryFolder& folder : folders_) {
    if (PathKey(folder.path) == key && folder.recursive != recursive) {
      folder.recursive = recursive;
      SaveLocked();
      BumpLocked();
      return;
    }
  }
}

std::vector<GameEntry> GameLibrary::Games() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return games_;
}

void GameLibrary::SetFavorite(const std::filesystem::path& path,
                              bool favorite) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (GameEntry* entry = FindLocked(path)) {
    entry->favorite = favorite;
    SaveLocked();
    BumpLocked();
  }
}

void GameLibrary::SetCustomName(const std::filesystem::path& path,
                                std::string name) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (GameEntry* entry = FindLocked(path)) {
    entry->custom_name = std::move(name);
    SaveLocked();
    BumpLocked();
  }
}

void GameLibrary::ExcludeGame(const std::filesystem::path& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string key = PathKey(path);
  const size_t before = games_.size();
  games_.erase(std::remove_if(games_.begin(), games_.end(),
                              [&key](const GameEntry& entry) {
                                return PathKey(entry.path) == key;
                              }),
               games_.end());
  excluded_.insert(key);
  if (games_.size() != before) {
    BumpLocked();
  }
  SaveLocked();
}

void GameLibrary::RecordLaunch(const std::filesystem::path& path,
                               uint32_t title_id, std::string_view title_name,
                               std::span<const uint8_t> icon_png) {
  // Cache the icon first; it is independent of the library state.
  if (title_id && !icon_png.empty()) {
    const std::filesystem::path icon_path = IconPath(title_id);
    std::error_code ec;
    if (!std::filesystem::exists(icon_path, ec)) {
      std::filesystem::create_directories(icon_folder_, ec);
      std::ofstream icon_file(icon_path, std::ios::binary | std::ios::trunc);
      if (icon_file.is_open()) {
        icon_file.write(reinterpret_cast<const char*>(icon_png.data()),
                        static_cast<std::streamsize>(icon_png.size()));
      }
    }
  }

  std::error_code ec;
  const uint64_t size_bytes = static_cast<uint64_t>(
      std::filesystem::is_regular_file(path, ec)
          ? std::filesystem::file_size(path, ec)
          : 0);
  const int64_t modified_time =
      FileTimeToSeconds(std::filesystem::last_write_time(path, ec));

  std::lock_guard<std::mutex> lock(mutex_);
  excluded_.erase(PathKey(path));
  GameEntry* entry = FindLocked(path);
  if (!entry) {
    games_.emplace_back();
    entry = &games_.back();
    entry->path = path;
    entry->format = FormatForPath(path);
  }
  if (title_id) {
    entry->title_id = title_id;
  }
  if (!title_name.empty()) {
    entry->title_name = std::string(title_name);
  }
  if (!ec) {
    entry->size_bytes = size_bytes;
    entry->modified_time = modified_time;
  }
  entry->last_played = static_cast<int64_t>(std::time(nullptr));
  ++entry->play_count;
  SaveLocked();
  BumpLocked();
}

std::string GameLibrary::ScanStatus() const {
  std::lock_guard<std::mutex> lock(status_mutex_);
  return status_;
}

void GameLibrary::SetStatus(std::string status) {
  std::lock_guard<std::mutex> lock(status_mutex_);
  status_ = std::move(status);
}

void GameLibrary::Rescan() {
  std::lock_guard<std::mutex> lock(thread_mutex_);
  if (scanning_.exchange(true)) {
    return;
  }
  if (scan_thread_.joinable()) {
    // The previous scan has finished (scanning_ was false); reap it.
    scan_thread_.join();
  }
  scan_thread_ = std::thread([this]() { ScanThreadMain(); });
}

void GameLibrary::ScanThreadMain() {
  std::vector<LibraryFolder> folders;
  std::vector<GameEntry> known;
  std::set<std::string> excluded;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    folders = folders_;
    known = games_;
    excluded = excluded_;
  }

  // 1. Walk the folders.
  std::vector<Candidate> found;
  std::set<std::string> seen;
  std::vector<std::filesystem::path> reachable_folders;
  const auto consider = [&](const std::filesystem::directory_entry& entry) {
    std::error_code ec;
    if (!entry.is_regular_file(ec) || ec) {
      return;
    }
    const std::filesystem::path path = entry.path();
    if (!IsLibraryCandidate(path)) {
      return;
    }
    const std::string key = PathKey(path);
    if (excluded.count(key) || !seen.insert(key).second) {
      return;
    }
    Candidate candidate;
    candidate.path = path;
    candidate.format = FormatForPath(path);
    ec.clear();
    candidate.size_bytes = static_cast<uint64_t>(entry.file_size(ec));
    if (ec) {
      candidate.size_bytes = 0;
    }
    ec.clear();
    const auto write_time = entry.last_write_time(ec);
    candidate.modified_time = ec ? 0 : FileTimeToSeconds(write_time);
    found.push_back(std::move(candidate));
  };

  for (const LibraryFolder& folder : folders) {
    if (stop_) {
      break;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(folder.path, ec) || ec) {
      continue;
    }
    reachable_folders.push_back(folder.path);
    SetStatus(fmt::format("Scanning {} ({} found)",
                          xe::path_to_utf8(folder.path), found.size()));
    if (folder.recursive) {
      std::filesystem::recursive_directory_iterator it(
          folder.path, std::filesystem::directory_options::skip_permission_denied,
          ec);
      const std::filesystem::recursive_directory_iterator end;
      while (!ec && it != end && !stop_) {
        if (it.depth() >= kMaxScanDepth) {
          it.disable_recursion_pending();
        }
        consider(*it);
        it.increment(ec);
      }
    } else {
      std::filesystem::directory_iterator it(
          folder.path, std::filesystem::directory_options::skip_permission_denied,
          ec);
      const std::filesystem::directory_iterator end;
      while (!ec && it != end && !stop_) {
        consider(*it);
        it.increment(ec);
      }
    }
  }

  // 2. Read metadata for new or changed files.
  struct Result {
    Candidate candidate;
    bool read = false;
    uint32_t title_id = 0;
  };
  std::map<std::string, const GameEntry*> known_by_key;
  for (const GameEntry& entry : known) {
    known_by_key[PathKey(entry.path)] = &entry;
  }
  std::vector<Result> results;
  results.reserve(found.size());
  for (size_t i = 0; i < found.size() && !stop_; ++i) {
    Result result;
    result.candidate = found[i];
    const auto it = known_by_key.find(PathKey(found[i].path));
    const bool unchanged = it != known_by_key.end() &&
                           it->second->size_bytes == found[i].size_bytes &&
                           it->second->modified_time == found[i].modified_time;
    if (!unchanged) {
      SetStatus(fmt::format("Reading {} of {}: {}", i + 1, found.size(),
                            xe::path_to_utf8(found[i].path.filename())));
      result.read = true;
      result.title_id = ReadTitleId(found[i]);
    }
    results.push_back(std::move(result));
  }

  if (stop_) {
    scanning_ = false;
    return;
  }

  // 3. Work out which known games vanished. Only forget a game if it sits in
  // a library folder that is reachable right now, so unplugging an external
  // drive never wipes favourites and play counts.
  std::set<std::string> vanished;
  for (const GameEntry& entry : known) {
    bool in_reachable_folder = false;
    for (const std::filesystem::path& folder : reachable_folders) {
      if (IsUnderFolder(entry.path, folder)) {
        in_reachable_folder = true;
        break;
      }
    }
    std::error_code ec;
    if (in_reachable_folder && !std::filesystem::exists(entry.path, ec) &&
        !ec) {
      vanished.insert(PathKey(entry.path));
    }
  }

  // 4. Merge into the live list (which the user may have edited meanwhile).
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Result& result : results) {
      GameEntry* entry = FindLocked(result.candidate.path);
      if (!entry) {
        games_.emplace_back();
        entry = &games_.back();
        entry->path = result.candidate.path;
      }
      entry->format = result.candidate.format;
      entry->size_bytes = result.candidate.size_bytes;
      entry->modified_time = result.candidate.modified_time;
      if (result.read && result.title_id) {
        entry->title_id = result.title_id;
      }
    }
    games_.erase(std::remove_if(games_.begin(), games_.end(),
                                [&vanished](const GameEntry& entry) {
                                  return vanished.count(PathKey(entry.path)) !=
                                         0;
                                }),
                 games_.end());
    SaveLocked();
    BumpLocked();
  }

  SetStatus(fmt::format("{} games found", found.size()));
  scanning_ = false;
}

}  // namespace app
}  // namespace xe
