#include "services/dashboard_service.h"
#include "services/replay_service.h"
#include <iostream>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Services {

// ---------------------------------------------------------------------------
// Computed percentages
// ---------------------------------------------------------------------------

double DashboardOutcomeAggregate::success_pct() const {
  return total_events > 0 ? (100.0 * success_count / total_events) : 0.0;
}

double DashboardOutcomeAggregate::failure_pct() const {
  return total_events > 0 ? (100.0 * failure_count / total_events) : 0.0;
}

double DashboardOutcomeAggregate::insufficient_evidence_pct() const {
  return total_events > 0 ? (100.0 * insufficient_evidence_count / total_events) : 0.0;
}

double DashboardOutcomeAggregate::user_rejected_pct() const {
  return total_events > 0 ? (100.0 * user_rejected_count / total_events) : 0.0;
}

double DashboardOutcomeAggregate::user_corrected_goal_pct() const {
  return trust_events > 0 ? (100.0 * user_corrected_goal_count / trust_events) : 0.0;
}

double DashboardOutcomeAggregate::diff_rejected_pct() const {
  return trust_events > 0 ? (100.0 * (trust_events - diff_approved_count) / trust_events) : 0.0;
}

double DashboardOutcomeAggregate::avg_attempts() const {
  return recovery_events > 0 ? (static_cast<double>(total_attempts) / recovery_events) : 0.0;
}

double DashboardOutcomeAggregate::avg_confidence_delta() const {
  return recovery_events > 0 ? (total_confidence_delta / recovery_events) : 0.0;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string replay_dir() {
  const char *home = std::getenv("HOME");
  if (!home)
    home = std::getenv("USERPROFILE");
  if (!home)
    home = ".";
  return std::string(home) + "/.cursor/replay";
}

static Core::Outcome parse_outcome(const json &j) {
  std::string name = j.value("outcome", "insufficient_evidence");
  return Core::outcome_from_name(name);
}

static int confidence_band(double v) {
  if (v < 0.2) return 0;
  if (v < 0.4) return 1;
  if (v < 0.6) return 2;
  if (v < 0.8) return 3;
  return 4;
}

// ---------------------------------------------------------------------------
// Generate dashboard
// ---------------------------------------------------------------------------

DashboardOutcomeAggregate DashboardService::generate(
    const std::string &filter) {

  DashboardOutcomeAggregate agg;
  std::string filter_outcome;

  // Parse filter: "outcome=<name>"
  if (!filter.empty()) {
    if (filter.find("outcome=") == 0) {
      filter_outcome = filter.substr(8);
      // Insert underscore before capitals (camelCase → snake_case)
      std::string normalized;
      for (size_t i = 0; i < filter_outcome.size(); i++) {
        if (i > 0 && std::isupper(static_cast<unsigned char>(filter_outcome[i])) &&
            std::islower(static_cast<unsigned char>(filter_outcome[i-1]))) {
          normalized += '_';
        }
        normalized += std::tolower(static_cast<unsigned char>(filter_outcome[i]));
      }
      // Normalize spaces/hyphens to underscores
      std::replace(normalized.begin(), normalized.end(), ' ', '_');
      std::replace(normalized.begin(), normalized.end(), '-', '_');
      filter_outcome = normalized;
    }
  }

  std::string dir = replay_dir();
  if (!fs::exists(dir))
    return agg;

  for (auto &entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() != ".log")
      continue;

    std::string session_id = entry.path().stem().string();
    std::ifstream f(entry.path());
    if (!f.is_open())
      continue;

    std::string line;
    bool session_matches_filter = false;
    Core::Outcome last_outcome = Core::Outcome::InsufficientEvidence;

    while (std::getline(f, line)) {
      try {
        auto j = json::parse(line);
        agg.total_events++;

        // Outcome
        Core::Outcome o = Core::Outcome::InsufficientEvidence;
        if (j.contains("outcome")) {
          o = parse_outcome(j);
          last_outcome = o;
        }

        // Execution path
        if (j.contains("execution_path")) {
          auto ep = Core::execution_path_from_name(j.value("execution_path", "chat_only"));
          switch (ep) {
            case Core::ExecutionPath::ChatOnly: agg.chat_only_count++; break;
            case Core::ExecutionPath::Engine: agg.engine_count++; break;
            case Core::ExecutionPath::TaskPipeline: agg.task_pipeline_count++; break;
            case Core::ExecutionPath::DirectService: agg.direct_service_count++; break;
            case Core::ExecutionPath::MetaCommand: agg.meta_command_count++; break;
            case Core::ExecutionPath::ShellEscape: agg.shell_escape_count++; break;
          }
        }

        switch (o) {
          case Core::Outcome::Success:
            agg.success_count++;
            break;
          case Core::Outcome::Failure:
            agg.failure_count++;
            break;
          case Core::Outcome::InsufficientEvidence:
            agg.insufficient_evidence_count++;
            break;
          case Core::Outcome::UserRejected:
            agg.user_rejected_count++;
            break;
        }

        // Check filter match for drill-down
        if (!filter_outcome.empty() &&
            Core::outcome_name(o) == filter_outcome) {
          session_matches_filter = true;
        }

        // Trust metrics
        if (j.contains("trust_metrics")) {
          auto t = j["trust_metrics"];
          agg.trust_events++;
          if (t.value("plan_approved", false))
            agg.plan_approved_count++;
          if (t.value("diff_approved", false))
            agg.diff_approved_count++;
          if (t.value("user_corrected_goal", false))
            agg.user_corrected_goal_count++;
          if (t.value("reverted", false))
            agg.reverted_count++;
        }

        // Recovery metrics
        if (j.contains("recovery_metrics")) {
          auto r = j["recovery_metrics"];
          agg.recovery_events++;
          agg.total_attempts += r.value("attempts", 0);
          agg.total_confidence_delta +=
              r.value("confidence_delta", 0.0);
          if (r.value("evidence_found", false))
            agg.evidence_found_count++;
          if (r.value("verification_found", false))
            agg.verification_found_count++;
        }

        // Confidence bands
        double cb = j.value("confidence_before", -1.0);
        double ca = j.value("confidence_after", -1.0);
        bool is_success = (o == Core::Outcome::Success);
        bool is_benchmark = j.value("input", "").find("benchmark:") == 0;
        if (is_benchmark)
          agg.benchmark_events++;
        else
          agg.interactive_events++;

        if (cb >= 0.0) {
          int b = confidence_band(cb);
          agg.band_cb[b]++;
          if (is_success) agg.band_success_cb[b]++;

          if (is_benchmark) {
            agg.band_cb_benchmark[b]++;
            if (is_success) agg.band_success_cb_benchmark[b]++;
          } else {
            agg.band_cb_interactive[b]++;
            if (is_success) agg.band_success_cb_interactive[b]++;
          }
        }
        if (ca >= 0.0) {
          int b = confidence_band(ca);
          agg.band_ca[b]++;
          if (is_success) agg.band_success_ca[b]++;
        }

      } catch (...) {
        // skip malformed lines
      }
    }

    agg.total_sessions++;

    // For "outcome=" filter: match if session had any matching event
    // OR if the last event matches
    if (session_matches_filter || 
        (!filter_outcome.empty() && Core::outcome_name(last_outcome) == filter_outcome)) {
      agg.matching_sessions.push_back(session_id);
    }
  }

  return agg;
}

} // namespace Services
