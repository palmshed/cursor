#include "services/evidence_gap_engine.h"
#include <algorithm>
#include <set>

namespace Services {

// Check if two entries from different tools converge on the same target.
// Uses case-insensitive substring matching on the query/target strings.
// This is a token-knowledge-free heuristic: if two different tools searched
// for the same thing and both found results, they corroborate each other.
static bool entries_converge(const EvidenceEntry &a, const EvidenceEntry &b) {
  if (a.tool.empty() || b.tool.empty())
    return false;
  if (a.tool == b.tool)
    return false; // same tool cannot independently corroborate

  // Compare queries: if one query is a substring of the other (case-insensitive),
  // they converge on the same topic.
  std::string a_q = a.query;
  std::string b_q = b.query;
  for (auto &c : a_q) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (auto &c : b_q) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  // Also try targets
  std::string a_t = a.target;
  std::string b_t = b.target;
  for (auto &c : a_t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (auto &c : b_t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  return (a_q.find(b_q) != std::string::npos ||
          b_q.find(a_q) != std::string::npos ||
          a_t.find(b_t) != std::string::npos ||
          b_t.find(a_t) != std::string::npos ||
          a_t.find(b_q) != std::string::npos ||
          b_t.find(a_q) != std::string::npos);
}

// Check if any pair of entries of the same type from different tools
// converge and both have quality >= Moderate.
static bool is_independently_verified(const std::vector<EvidenceEntry> &entries) {
  if (entries.size() < 2)
    return false;

  for (size_t i = 0; i < entries.size(); ++i) {
    for (size_t j = i + 1; j < entries.size(); ++j) {
      if (entries[i].quality < Moderate || entries[j].quality < Moderate)
        continue;
      if (entries_converge(entries[i], entries[j]))
        return true;
    }
  }
  return false;
}

EvidenceGap EvidenceGapEngine::evaluate(
    const std::vector<EvidenceRequirement> &requirements,
    const EvidenceStore &evidence) const {

  EvidenceGap gap;

  for (auto &req : requirements) {
    EvidenceGap::RequirementStatus rs;
    rs.requirement = req;
    rs.best_quality = QualNone;
    rs.is_independently_verified = false;

    // Collect entries matching this evidence class
    std::vector<EvidenceEntry> matching;
    for (auto &e : evidence.entries) {
      if (e.type == req.ec)
        matching.push_back(e);
    }

    if (!matching.empty()) {
      // Best quality among matching entries
      for (auto &m : matching)
        if (m.quality > rs.best_quality)
          rs.best_quality = m.quality;

      // Independent verification: two+ different tools converge
      rs.is_independently_verified = is_independently_verified(matching);
    }

    gap.requirements.push_back(rs);
  }

  return gap;
}

} // namespace Services
