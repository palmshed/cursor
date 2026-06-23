#pragma once
#include "core/model_catalog.h"
#include <nlohmann/json.hpp>
#include <string>

namespace Services {

class AIService {
private:
  Core::ModelConfig  model_;
  Core::ProviderConfig provider_;
  std::string        api_key_;

  nlohmann::json build_system_prompt(const std::string& context) const;
  nlohmann::json create_payload(const std::string& user_input,
                                const std::string& context) const;
  std::string    parse_sse_stream(const std::string& response) const;
  std::string    parse_response(const std::string& body) const;
  std::string    get_url() const;

public:
  // Construct from a fully resolved ModelConfig + the caller's API key.
  AIService(const Core::ModelConfig& model, const std::string& api_key = "");

  // Override the api_model string at runtime (e.g. when Ollama lists models).
  void override_api_model(const std::string& name);

  bool        is_available() const;
  std::string chat(const std::string& user_input, const std::string& context);

  // Read-only accessors used by diagnostics / session logging.
  const Core::ModelConfig&    model()    const { return model_; }
  const Core::ProviderConfig& provider() const { return provider_; }
};

} // namespace Services
