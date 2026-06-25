#include "services/find_service.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace Services {

std::vector<FindCandidate> directory_aware_find(const std::string &term,
                                                 bool impl_query) {
  std::vector<FindCandidate> candidates;

  if (term.empty())
    return candidates;

  // Lowercase the search term
  std::string term_lower = term;
  for (auto &c : term_lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  // CamelCase normalization: insert _ at uppercase-after-lowercase boundaries
  std::string term_normalized;
  for (size_t i = 0; i < term.size(); i++) {
    unsigned char c = static_cast<unsigned char>(term[i]);
    if (i > 0 && std::isupper(c) &&
        std::islower(static_cast<unsigned char>(term[i - 1]))) {
      term_normalized.push_back('_');
    }
    term_normalized.push_back(static_cast<char>(std::tolower(c)));
  }

  // Extract individual words for word-level matching (handles [ _-]? patterns)
  std::vector<std::string> term_words;
  {
    std::string cleaned = term_lower;
    size_t pos = 0;
    while ((pos = cleaned.find("[ _-]?", pos)) != std::string::npos) {
      cleaned.replace(pos, 6, " ");
      pos += 1;
    }
    std::istringstream ws(cleaned);
    std::string w;
    while (ws >> w) {
      if (!w.empty())
        term_words.push_back(w);
    }
  }

  auto start = std::filesystem::current_path();
  for (auto &entry :
       std::filesystem::recursive_directory_iterator(
           start, std::filesystem::directory_options::skip_permission_denied)) {
    if (!entry.is_regular_file())
      continue;

    auto path = entry.path();
    std::string ext = path.extension().string();

    // Extension filter
    if (!ext.empty() && ext != ".cpp" && ext != ".h" && ext != ".hpp" &&
        ext != ".c" && ext != ".py" && ext != ".js" && ext != ".json" &&
        ext != ".yml" && ext != ".yaml" && ext != ".cmake" && ext != ".txt" &&
        ext != ".md" && path.filename() != "CMakeLists.txt")
      continue;

    auto rel = std::filesystem::relative(path, start);
    std::string rel_str = rel.string();

    // Skip hidden dirs, build dirs, node_modules
    if (rel_str.find("/.") != std::string::npos ||
        rel_str.find("build/_deps") != std::string::npos ||
        rel_str.find("node_modules") != std::string::npos)
      continue;

    std::string stem = rel.stem().string();
    std::string stem_lower = stem;
    for (auto &c : stem_lower)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    int score = 0;
    std::string reason;

    // --- Filename matching ---
    if (stem_lower == term_lower || stem_lower == term_normalized) {
      score = 20;
      reason = "exact filename match";
    } else if (stem_lower.find(term_lower) != std::string::npos ||
               stem_lower.find(term_normalized) != std::string::npos) {
      score = 10;
      reason = "partial filename match";
    } else {
      std::string path_lower = rel_str;
      for (auto &c : path_lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (path_lower.find(term_lower) != std::string::npos ||
          path_lower.find(term_normalized) != std::string::npos) {
        score = 5;
        reason = "directory path match";
      }
    }

    // --- Word-level matching for multi-word terms ---
    if (score == 0 && term_words.size() >= 2) {
      bool all_found = true;
      for (auto &w : term_words) {
        if (stem_lower.find(w) == std::string::npos) {
          all_found = false;
          break;
        }
      }
      if (all_found) {
        score = 12;
        reason = "word-level match";
      }
    }

    if (score == 0)
      continue;

    // --- Symbol scanning for source files with non-exact score ---
    int symbol_score = 0;
    std::string symbol_reason;
    if ((ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".c") &&
        score < 20 && term.size() >= 2) {
      std::ifstream f(path);
      if (f) {
        std::string line;
        int lines_read = 0;
        while (lines_read < 80 && std::getline(f, line)) {
          lines_read++;
          auto check_symbol = [&](const std::string &prefix, int exact_score,
                                  int partial_score) {
            size_t pos = line.find(prefix);
            if (pos != std::string::npos) {
              size_t ns = pos + prefix.size();
              while (ns < line.size() &&
                     std::isspace(static_cast<unsigned char>(line[ns])))
                ns++;
              size_t ne = ns;
              while (ne < line.size() &&
                     (std::isalnum(static_cast<unsigned char>(line[ne])) ||
                      line[ne] == '_'))
                ne++;
              if (ne > ns) {
                std::string sym = line.substr(ns, ne - ns);
                std::string sym_lower = sym;
                for (auto &c : sym_lower)
                  c = static_cast<char>(
                      std::tolower(static_cast<unsigned char>(c)));
                if (sym_lower == term_lower ||
                    sym_lower == term_normalized) {
                  symbol_score = std::max(symbol_score, exact_score);
                  symbol_reason = "exact symbol match";
                } else if (sym_lower.find(term_lower) != std::string::npos ||
                           sym_lower.find(term_normalized) !=
                               std::string::npos) {
                  symbol_score = std::max(symbol_score, partial_score);
                  symbol_reason = "partial symbol match";
                }
              }
            }
          };
          check_symbol("class ", 18, 12);
          check_symbol("struct ", 18, 12);
          check_symbol("enum class ", 18, 12);
          size_t fp = line.find(term_lower);
          if (fp == std::string::npos)
            fp = line.find(term_normalized);
          if (fp != std::string::npos) {
            size_t start_search = (fp > 0) ? fp - 1 : 0;
            size_t delim = line.find_last_of(" \t*&-><:,;[](){}", start_search);
            if (delim == std::string::npos)
              delim = 0;
            else if (delim + 1 < line.size() &&
                     std::isspace(
                         static_cast<unsigned char>(line[delim + 1])))
              delim++;
            size_t end_paren = line.find('(', fp);
            if (end_paren != std::string::npos && end_paren > delim) {
              std::string possible_name =
                  line.substr(delim, end_paren - delim);
              size_t trim_start = possible_name.find_first_not_of(" \t*&");
              size_t trim_end = possible_name.find_last_not_of(" \t*&");
              if (trim_start != std::string::npos &&
                  trim_end != std::string::npos) {
                possible_name = possible_name.substr(
                    trim_start, trim_end - trim_start + 1);
                if (!possible_name.empty() &&
                    possible_name.find(' ') == std::string::npos &&
                    possible_name.find('\t') == std::string::npos) {
                  symbol_score = std::max(symbol_score, 15);
                  symbol_reason = "function symbol match";
                  break;
                }
              }
            }
          }
        }
      }
    }

    if (symbol_score > score) {
      score = symbol_score;
      reason = symbol_reason;
    }

    // --- Implementation file boost ---
    if (impl_query && ext == ".cpp") {
      score += 8;
      if (reason.find("exact") != std::string::npos)
        reason = reason + " + implementation file";
      else if (reason.find("partial") != std::string::npos)
        reason = reason + " + implementation file";
      else if (reason.find("symbol") != std::string::npos)
        reason = reason + " + implementation file";
      else
        reason += " + implementation file";
    }

    if (score == 0)
      continue;

    candidates.push_back({rel_str, stem, score, reason});
  }

  // Sort by score descending, path ascending for ties
  std::sort(candidates.begin(), candidates.end(),
            [](const FindCandidate &a, const FindCandidate &b) {
              if (a.score != b.score)
                return a.score > b.score;
              return a.path < b.path;
            });

  return candidates;
}

} // namespace Services
