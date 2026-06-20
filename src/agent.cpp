#include "agent.h"
#include "app/command_router.h"
#include "app/session.h"
#include "memory_manager.h"
#include "services/ai_service.h"
#include "services/replay_service.h"
#include "ui/ui_manager.h"
#include "utils/ui.h"

#include <iostream>
#include <memory>
#include <string>

namespace Core {

Agent::Agent()
    : memory_(std::make_unique<Data::MemoryManager>()), ai_service_(nullptr) {}

Agent::Agent(Agent &&) noexcept = default;

Agent &Agent::operator=(Agent &&) noexcept = default;

Agent::~Agent() {
  // Destructor defined here because unique_ptr types are forward declared
}

void Agent::run() {
  UIManager ui(*this);
  Services::ReplayService replay;
  CommandRouter router(*this, ui, &replay);
  Session session(*this, router, ui, &replay);
  session.run();
}

bool Agent::shell_mode() const noexcept { return shell_mode_; }

const std::string &Agent::active_goal() const noexcept { return active_goal_; }

const std::vector<Agent::AgentTask> &Agent::tasks() const noexcept {
  return tasks_;
}

const std::map<std::string, std::string> &Agent::agent_params()
    const noexcept {
  return agent_params_;
}

std::string Agent::format_message(const std::string &sender,
                                   const std::string &content) {
  return Utils::Color::DIM + sender + Utils::Color::RESET + "\n\n" + content;
}

} // namespace Core
