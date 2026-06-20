#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

inline std::string trim_copy(const std::string &s) {
  size_t a = 0;
  while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
    ++a;
  size_t b = s.size();
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    --b;
  return s.substr(a, b - a);
}

inline bool starts_with_at(const std::string &s, size_t pos,
                           const std::string &prefix) {
  return pos + prefix.size() <= s.size() &&
         s.compare(pos, prefix.size(), prefix) == 0;
}

inline std::string normalize_input(const std::string &input) {
  std::string lower = input;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  return lower;
}

inline std::string strip_trailing_clause(std::string input) {
  std::string lower = normalize_input(input);
  const std::array<std::string_view, 4> suffixes = {
      " in code", " in repo", " in this repo", " in project"};
  for (const auto &suffix : suffixes) {
    if (lower.ends_with(suffix)) {
      input = trim_copy(input.substr(0, input.size() - suffix.size()));
      break;
    }
  }
  return input;
}

inline std::optional<std::string>
extract_grep_command(const std::string &input, const std::string &marker) {
  std::string lower = normalize_input(input);
  size_t pos = lower.find(marker);
  if (pos == std::string::npos)
    return std::nullopt;

  std::string query = trim_copy(input.substr(pos + marker.size()));
  query = strip_trailing_clause(query);
  if (query.empty())
    return std::nullopt;
  return std::make_optional(std::string("grep:") + query);
}

} // namespace Core
