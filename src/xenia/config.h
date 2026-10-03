/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CONFIG_H_
#define XENIA_CONFIG_H_

#include <filesystem>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wabsolute-value"
#endif
#include "third_party/tomlplusplus/toml.hpp"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

toml::parse_result ParseFile(const std::filesystem::path& filename);

namespace config {
void SetupConfig(const std::filesystem::path& config_folder);
void LoadGameConfig(const std::string_view title_id);
void SaveConfig();

// The folder the global config lives in. Empty before SetupConfig().
const std::filesystem::path& GetConfigFolder();
// Where the per-game config of a title (eight hex digits) is read from.
std::filesystem::path GetGameConfigPath(const std::string_view title_id);
}  // namespace config

#endif  // XENIA_CONFIG_H_
