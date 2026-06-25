#pragma once
#include <string>
#include <vector>

namespace Services {

struct FindCandidate {
  std::string path;
  std::string stem;
  int score{0};
  std::string reason;
};

// Directory-aware filename + symbol lookup with deterministic ranking.
// Scans the repository filesystem, scores candidates by:
//   exact filename match (20), partial filename (10), directory path (5),
//   word-level match (12), symbol match (up to 18), implementation boost (+8).
// Returns candidates sorted by score descending, path ascending.
std::vector<FindCandidate> directory_aware_find(const std::string &term,
                                                 bool impl_query);

} // namespace Services
