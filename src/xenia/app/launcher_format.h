/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_LAUNCHER_FORMAT_H_
#define XENIA_APP_LAUNCHER_FORMAT_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace xe {
namespace app {
namespace launcher {

// "0 B", "512 B", "1.5 KB", "4.2 GB" (powers of 1024).
std::string FormatBytes(uint64_t bytes);

// "Never" for 0, otherwise "Just now", "5 min ago", "3 h ago", "Yesterday",
// "12 days ago", "4 months ago", "2 years ago". Both are Unix timestamps.
std::string FormatRelativeTime(int64_t then, int64_t now);

std::string ToLowerAscii(std::string_view text);

// Whether `needle_lowercase` (already lower-case) occurs in `haystack`,
// ignoring ASCII case. An empty needle matches everything.
bool ContainsInsensitive(std::string_view haystack,
                         std::string_view needle_lowercase);

// The first non-empty line of `text`, trimmed and shortened to at most about
// `max_bytes` (never splitting a UTF-8 sequence), with "..." appended if cut.
std::string FirstLine(std::string_view text, size_t max_bytes);

// Shortens to at most `max_bytes` bytes of text on a UTF-8 boundary and
// appends "..." if anything was removed.
std::string TruncateUtf8(std::string_view text, size_t max_bytes);

}  // namespace launcher
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_LAUNCHER_FORMAT_H_
