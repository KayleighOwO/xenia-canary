/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_SETTINGS_MODEL_H_
#define XENIA_APP_SETTINGS_MODEL_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "third_party/tomlplusplus/toml.hpp"
#include "xenia/base/cvar.h"

namespace xe {
namespace app {

enum class SettingType : uint8_t {
  kBool,
  kInt32,
  kUint32,
  kInt64,
  kUint64,
  kDouble,
  kString,
  kPath,
  kUnknown,
};

// Paths are carried as UTF-8 strings.
using SettingValue =
    std::variant<bool, int32_t, uint32_t, int64_t, uint64_t, double,
                 std::string>;

// One configuration option, as registered with the cvar system.
struct SettingInfo {
  cvar::IConfigVar* var = nullptr;  // Not owned; lives for the whole process.
  std::string name;
  std::string category;
  std::string description;
  SettingType type = SettingType::kUnknown;
};

// Typed, UI-independent access to every persistent config variable.
//
// "Saved" is the value in the global config (what gets written to disk),
// "in use" is what the emulator is actually using right now, which differs when
// a command line argument or a per-game config overrides the saved value.
class SettingsModel {
 public:
  // All persistent (non-transient) options sorted by category, then name.
  static std::vector<SettingInfo> Enumerate();
  static bool Lookup(std::string_view name, SettingInfo* out_info);
  static SettingType TypeOf(cvar::IConfigVar* var);

  static SettingValue GetSaved(const SettingInfo& info);
  static SettingValue GetDefault(const SettingInfo& info);
  static SettingValue GetInUse(const SettingInfo& info);
  static bool IsModified(const SettingInfo& info);

  // Changes the saved value and applies it immediately (dropping command line
  // and game config overrides). Returns false if the value's type does not
  // match the option. Call SaveGlobal() to persist.
  static bool Apply(const SettingInfo& info, const SettingValue& value);
  static void ResetToDefault(const SettingInfo& info);
  // Writes the global config file. Returns false if there is none to write.
  static bool SaveGlobal();

  static std::string Format(const SettingValue& value);
};

// The per-game overrides of one title: <config folder>/config/<TITLEID>.config.toml
// containing only the options the user chose to change for that game.
class GameOverrides {
 public:
  explicit GameOverrides(uint32_t title_id);

  // Reads the file if it exists. Returns false if it exists but cannot be
  // parsed; Save() then refuses to run so a hand-edited file is not destroyed.
  bool Load();
  const std::string& error() const { return error_; }

  bool Has(const SettingInfo& info) const;
  // Fetches the override as the option's type. False if absent or mistyped.
  bool Get(const SettingInfo& info, SettingValue* out_value) const;
  void Set(const SettingInfo& info, const SettingValue& value);
  void Remove(const SettingInfo& info);
  size_t count() const;

  // Writes the file, or deletes it when no overrides are left.
  bool Save();

  const std::filesystem::path& file_path() const { return file_path_; }
  uint32_t title_id() const { return title_id_; }

 private:
  uint32_t title_id_;
  std::filesystem::path file_path_;
  toml::table root_;
  std::string error_;
  bool parse_failed_ = false;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_SETTINGS_MODEL_H_
