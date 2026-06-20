#pragma once
#include "agent.h"
#include "ui/ui_manager.h"
#include <string>
#include <vector>

namespace Services {
class ReplayService;
}

namespace Core {

class CommandRouter;

class Session {
public:
  Session(Agent &agent, CommandRouter &router, UIManager &ui,
          Services::ReplayService *replay = nullptr);
  ~Session();
  void run();

private:
  Agent &agent_;
  CommandRouter &router_;
  UIManager &ui_;
  Services::ReplayService *replay_;
};

} // namespace Core
