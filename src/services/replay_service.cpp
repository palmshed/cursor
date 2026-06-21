#include "services/replay_service.h"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Services {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string compact_timestamp() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  std::ostringstream ss;
  ss << std::put_time(std::localtime(&t), "%Y%m%d_%H%M%S");
  return ss.str();
}

static long long now_epoch() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

static json state_to_json(const Core::SessionState &s) {
  return json{
      {"mode", static_cast<int>(s.mode_)},
      {"ollama_model", s.ollama_model_},
      {"verbose_mode", s.verbose_mode_},
      {"command_count", s.command_count_},
      {"token_usage", s.token_usage_},
      {"last_confidence_before", s.last_confidence_before},
      {"last_confidence_after", s.last_confidence_after},
      {"last_outcome", Core::outcome_name(s.last_outcome)},
      {"last_execution_path", Core::execution_path_name(s.last_execution_path)},
  };
}

static Core::SessionState json_to_state(const json &j) {
  Core::SessionState s;
  s.mode_ = static_cast<Core::AgentMode>(j.value("mode", 0));
  s.ollama_model_ = j.value("ollama_model", "");
  s.verbose_mode_ = j.value("verbose_mode", false);
  s.command_count_ = j.value("command_count", 0);
  s.token_usage_ = j.value("token_usage", 0LL);
  s.last_confidence_before = j.value("last_confidence_before", 0.0);
  s.last_confidence_after = j.value("last_confidence_after", 0.0);
  if (j.contains("last_outcome"))
    s.last_outcome = Core::outcome_from_name(j.value("last_outcome", "insufficient_evidence"));
  if (j.contains("last_execution_path"))
    s.last_execution_path = Core::execution_path_from_name(j.value("last_execution_path", "unknown"));
  return s;
}

// ---------------------------------------------------------------------------
// ReplayService
// ---------------------------------------------------------------------------

ReplayService::ReplayService()
    : session_id_(generate_session_id()),
      log_path_(),
      replay_dir_(replay_dir()),
      step_(0) {
  log_path_ = replay_dir_ + "/" + session_id_ + ".log";
  fs::create_directories(replay_dir_);
}

ReplayService::~ReplayService() = default;

std::string ReplayService::generate_session_id() { return compact_timestamp(); }

std::string ReplayService::replay_dir() {
  const char *home = std::getenv("HOME");
  if (!home)
    home = std::getenv("USERPROFILE");
  if (!home)
    home = ".";
  return std::string(home) + "/.cursor/replay";
}

long long ReplayService::epoch_seconds() { return now_epoch(); }

void ReplayService::log_input(
    const Core::SessionState &state_before,
    const Core::SessionState &state_after, const std::string &input,
    Core::Outcome outcome, Core::ExecutionPath exec_path,
    const Core::RecoveryMetrics &recovery,
    const Core::TrustMetrics &trust, double confidence_before,
    double confidence_after) {
  json entry;
  entry["ts"] = now_epoch();
  entry["step"] = ++step_;
  entry["input"] = input;
  entry["state_before"] = state_to_json(state_before);
  entry["state_after"] = state_to_json(state_after);
  entry["outcome"] = Core::outcome_name(outcome);
  entry["execution_path"] = Core::execution_path_name(exec_path);

  json rec_json;
  rec_json["attempts"] = recovery.attempts;
  rec_json["strategy_changes"] = recovery.strategy_changes;
  rec_json["evidence_found"] = recovery.evidence_found;
  rec_json["verification_found"] = recovery.verification_found;
  rec_json["confidence_delta"] = recovery.confidence_delta;
  entry["recovery_metrics"] = rec_json;

  json tru_json;
  tru_json["plan_approved"] = trust.plan_approved;
  tru_json["diff_approved"] = trust.diff_approved;
  tru_json["user_corrected_goal"] = trust.user_corrected_goal;
  tru_json["reverted"] = trust.reverted;
  entry["trust_metrics"] = tru_json;

  entry["confidence_before"] = confidence_before;
  entry["confidence_after"] = confidence_after;

  std::ofstream out(log_path_, std::ios::app);
  if (out.is_open()) {
    out << entry.dump() << "\n";
  }
}

std::vector<ReplaySessionInfo> ReplayService::list_sessions() const {
  std::vector<ReplaySessionInfo> result;
  if (!fs::exists(replay_dir_))
    return result;

  for (auto &entry : fs::directory_iterator(replay_dir_)) {
    if (entry.path().extension() != ".log")
      continue;

    std::string id = entry.path().stem().string();
    std::ifstream f(entry.path());
    if (!f.is_open())
      continue;

    std::string line;
    if (!std::getline(f, line))
      continue;

    try {
      auto j = json::parse(line);
      ReplaySessionInfo info;
      info.id = id;
      info.created = j.value("ts", 0LL);
      info.event_count = 1;
      info.first_input = j.value("input", "");

      while (std::getline(f, line))
        info.event_count++;

      result.push_back(info);
    } catch (...) {
      // skip malformed files
    }
  }

  return result;
}

std::vector<ReplayEvent> ReplayService::load_session(const std::string &id) const {
  std::vector<ReplayEvent> result;
  fs::path path = replay_dir_ + "/" + id + ".log";
  if (!fs::exists(path))
    return result;

  std::ifstream f(path);
  if (!f.is_open())
    return result;

  std::string line;
  while (std::getline(f, line)) {
    try {
      auto j = json::parse(line);
      ReplayEvent ev;
      ev.timestamp = j.value("ts", 0LL);
      ev.step = j.value("step", 0);
      ev.input = j.value("input", "");
      if (j.contains("state_before"))
        ev.state_before = json_to_state(j["state_before"]);
      if (j.contains("state_after"))
        ev.state_after = json_to_state(j["state_after"]);
      if (j.contains("outcome"))
        ev.outcome = Core::outcome_from_name(j.value("outcome", "insufficient_evidence"));
      if (j.contains("execution_path"))
        ev.execution_path = Core::execution_path_from_name(j.value("execution_path", "unknown"));
      if (j.contains("recovery_metrics")) {
        auto r = j["recovery_metrics"];
        ev.recovery_metrics.attempts = r.value("attempts", 0);
        ev.recovery_metrics.strategy_changes = r.value("strategy_changes", 0);
        ev.recovery_metrics.evidence_found = r.value("evidence_found", false);
        ev.recovery_metrics.verification_found = r.value("verification_found", false);
        ev.recovery_metrics.confidence_delta = r.value("confidence_delta", 0.0);
      }
      if (j.contains("trust_metrics")) {
        auto t = j["trust_metrics"];
        ev.trust_metrics.plan_approved = t.value("plan_approved", false);
        ev.trust_metrics.diff_approved = t.value("diff_approved", false);
        ev.trust_metrics.user_corrected_goal = t.value("user_corrected_goal", false);
        ev.trust_metrics.reverted = t.value("reverted", false);
      }
      ev.confidence_before = j.value("confidence_before", 0.0);
      ev.confidence_after = j.value("confidence_after", 0.0);
      result.push_back(ev);
    } catch (...) {
      // skip malformed lines
    }
  }

  return result;
}

} // namespace Services
