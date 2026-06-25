#pragma once
#include "core/metrics.h"
#include "core/session_state.h"
#include <string>
#include <vector>

namespace Services {

struct ReplayEvent {
  long long timestamp{0};
  int step{0};
  std::string input;
  Core::SessionState state_before;
  Core::SessionState state_after;
  Core::Outcome outcome{Core::Outcome::InsufficientEvidence};
  Core::ExecutionPath execution_path{Core::ExecutionPath::Unknown};
  Core::RecoveryMetrics recovery_metrics;
  Core::TrustMetrics trust_metrics;
  double confidence_before{0.0};
  double confidence_after{0.0};
};

struct ReplaySessionInfo {
  std::string id;
  long long created{0};
  int event_count{0};
  std::string first_input;
};

class ReplayService {
public:
  ReplayService();
  ~ReplayService();

  void log_input(const Core::SessionState &state_before,
                 const Core::SessionState &state_after,
                 const std::string &input,
                 Core::Outcome outcome = Core::Outcome::InsufficientEvidence,
                 Core::ExecutionPath exec_path = Core::ExecutionPath::Unknown,
                 const Core::RecoveryMetrics &recovery = Core::RecoveryMetrics{},
                 const Core::TrustMetrics &trust = Core::TrustMetrics{},
                 double confidence_before = 0.0,
                 double confidence_after = 0.0,
                 const Core::RetrievalMetrics &retrieval = Core::RetrievalMetrics{});
  std::vector<ReplaySessionInfo> list_sessions() const;
  std::vector<ReplayEvent> load_session(const std::string &id) const;
  const std::string &session_id() const { return session_id_; }

private:
  std::string session_id_;
  std::string log_path_;
  std::string replay_dir_;
  int step_{0};

  static std::string generate_session_id();
  static std::string replay_dir();
  static long long epoch_seconds();
};

} // namespace Services
