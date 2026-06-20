#include "app/startup.h"
#include "app/menu.h"
#include "services/web_service.h"
#include "utils/config.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>

namespace Core {

void Startup::initialize(Agent &agent) {
  while (true) {
    std::vector<std::string> modes = {"Online", "Offline"};
    int choice = show_menu("Mode", modes, 1);
    if (choice < 0) continue;

    if (choice == 0) {
      std::vector<std::string> providers = {
          "Together AI", "Cerebras", "Fireworks",
          "Groq", "DeepSeek", "OpenAI"};
      std::vector<Agent::Mode> provider_modes = {
          Agent::Mode::MODE_TOGETHER, Agent::Mode::MODE_CEREBRAS,
          Agent::Mode::MODE_FIREWORKS, Agent::Mode::MODE_GROQ,
          Agent::Mode::MODE_DEEPSEEK, Agent::Mode::MODE_OPENAI};
      std::vector<std::string> provider_keys = {
          "TOGETHER_API_KEY", "CEREBRAS_API_KEY", "FIREWORKS_API_KEY",
          "GROQ_API_KEY", "DEEPSEEK_API_KEY", "OPENAI_API_KEY"};

      int p = show_menu("Provider", providers, 0);
      if (p < 0) continue;
      agent.state_.mode_ = provider_modes[p];
      agent.api_key_ = Utils::Config::get_env_var(provider_keys[p]);
      if (agent.api_key_.empty()) {
        throw std::runtime_error(provider_keys[p] + " not set");
      }
    } else {
      std::vector<std::string> models;
      try {
        auto response = Services::WebService::fetch_url("http://localhost:11434/api/tags");
        if (response.success && !response.content.empty()) {
          auto json = nlohmann::json::parse(response.content);
          if (json.contains("models") && json["models"].is_array()) {
            for (const auto &m : json["models"]) {
              if (m.contains("name")) {
                models.push_back(m["name"]);
              }
            }
          }
        }
      } catch (...) {
      }

      if (models.empty()) {
        models = {"llama3.2:3b", "llama3.2:latest", "llama3.1:latest"};
      }

      int m = show_menu("Model", models, 0);
      if (m < 0) continue;
      agent.state_.mode_ = Agent::Mode::MODE_LLAMA_3B;
      agent.state_.ollama_model_ = models[m];
    }
    break;
  }
}

bool Startup::is_online_mode(const Agent &agent) {
  return agent.state_.mode_ == Agent::Mode::MODE_TOGETHER ||
         agent.state_.mode_ == Agent::Mode::MODE_CEREBRAS ||
         agent.state_.mode_ == Agent::Mode::MODE_FIREWORKS ||
         agent.state_.mode_ == Agent::Mode::MODE_GROQ ||
         agent.state_.mode_ == Agent::Mode::MODE_DEEPSEEK ||
         agent.state_.mode_ == Agent::Mode::MODE_OPENAI;
}

} // namespace Core
