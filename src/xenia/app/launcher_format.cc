/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/launcher_format.h"

#include <iterator>

#include "third_party/fmt/include/fmt/format.h"

namespace xe {
namespace app {
namespace launcher {

std::string FormatBytes(uint64_t bytes) {
  static const char* const kUnits[] = {"B", "KB", "MB", "GB", "TB"};
  double value = static_cast<double>(bytes);
  size_t unit = 0;
  while (value >= 1024.0 && unit + 1 < std::size(kUnits)) {
    value /= 1024.0;
    ++unit;
  }
  if (unit == 0) {
    return fmt::format("{} B", bytes);
  }
  return fmt::format("{:.1f} {}", value, kUnits[unit]);
}

std::string FormatRelativeTime(int64_t then, int64_t now) {
  if (then <= 0) {
    return "Never";
  }
  const int64_t seconds = now - then;
  if (seconds < 60) {
    return "Just now";  // Also covers timestamps from the future.
  }
  const int64_t minutes = seconds / 60;
  if (minutes < 60) {
    return fmt::format("{} min ago", minutes);
  }
  const int64_t hours = minutes / 60;
  if (hours < 24) {
    return fmt::format("{} h ago", hours);
  }
  const int64_t days = hours / 24;
  if (days == 1) {
    return "Yesterday";
  }
  if (days < 30) {
    return fmt::format("{} days ago", days);
  }
  if (days < 365) {
    return fmt::format("{} months ago", days / 30);
  }
  return fmt::format("{} years ago", days / 365);
}

std::string ToLowerAscii(std::string_view text) {
  std::string result(text);
  for (char& c : result) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return result;
}

bool ContainsInsensitive(std::string_view haystack,
                         std::string_view needle_lowercase) {
  if (needle_lowercase.empty()) {
    return true;
  }
  if (needle_lowercase.size() > haystack.size()) {
    return false;
  }
  const size_t last_start = haystack.size() - needle_lowercase.size();
  for (size_t start = 0; start <= last_start; ++start) {
    size_t i = 0;
    for (; i < needle_lowercase.size(); ++i) {
      char c = haystack[start + i];
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
      if (c != needle_lowercase[i]) {
        break;
      }
    }
    if (i == needle_lowercase.size()) {
      return true;
    }
  }
  return false;
}

std::string TruncateUtf8(std::string_view text, size_t max_bytes) {
  if (text.size() <= max_bytes) {
    return std::string(text);
  }
  size_t cut = max_bytes;
  // Step back off continuation bytes so a multi-byte character isn't split.
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
    --cut;
  }
  return std::string(text.substr(0, cut)) + "...";
}

std::string FirstLine(std::string_view text, size_t max_bytes) {
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    std::string_view line = text.substr(start, end - start);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t' ||
                             line.front() == '\r')) {
      line.remove_prefix(1);
    }
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' ||
                             line.back() == '\r')) {
      line.remove_suffix(1);
    }
    if (!line.empty()) {
      return TruncateUtf8(line, max_bytes);
    }
    start = end + 1;
  }
  return std::string();
}

}  // namespace launcher
}  // namespace app
}  // namespace xe
