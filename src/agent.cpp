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

std::string Agent::format_message(const std::string &sender,
                                   const std::string &content) {
  return Utils::Color::DIM + sender + Utils::Color::RESET + "\n\n" + content;
}

} // namespace Core
