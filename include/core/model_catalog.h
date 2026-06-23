#pragma once
#include <string>
#include <vector>

namespace Core {

// ── Providers ──────────────────────────────────────────────────────────────
// A provider owns an endpoint and an auth scheme.
// Adding a new provider requires only a new enum value + ProviderConfig entry.

enum class Provider : int {
  Ollama,      // local, no auth
  OpenRouter,
  OpenAI,
  Groq,
  Together,
  Fireworks,
  Cerebras,
  DeepSeek,
};

enum class AuthScheme {
  None,        // e.g. Ollama local
  BearerToken, // Authorization: Bearer <key>
  XApiKey,     // X-API-Key: <key>   (Cerebras)
};

// Response format understood by the parser.
enum class ResponseFormat {
  OpenAIChat,  // choices[0].message.content
  OllamaChat,  // message.content  (or .response)
  SSEStream,   // server-sent events with delta.content (Cerebras)
};

struct ProviderConfig {
  Provider     id;
  std::string  base_url;       // production endpoint, no trailing slash
  std::string  test_url;       // mock endpoint used when TEST_MODE is set
  AuthScheme   auth_scheme;
  ResponseFormat response_fmt;
  bool         online;         // false → no API key required
};

// ── Cost / Category ──────────────────────────────────────────────────────────

enum class CostTier {
  Local,
  Free,
  Paid,
};

enum class ModelCategory {
  Coding,
  General,
  Reasoning,
  Vision,
  Experimental,
};

// ── Model capabilities ──────────────────────────────────────────────────────

struct ModelCapabilities {
  bool supports_tools{false};
  bool supports_reasoning{false};
  bool supports_vision{false};
  bool is_free{false};
};

// ── Model entry ─────────────────────────────────────────────────────────────

struct ModelConfig {
  std::string        id;           // unique catalog key (e.g. "groq-llama3.1-70b")
  std::string        display_name; // shown in menus (short, no provider suffix)
  Provider           provider;
  std::string        api_model;    // exact string sent to the API
  ModelCapabilities  caps;
  CostTier           tier;
  ModelCategory      category;
  bool               recommended{false}; // shown first in large categories
};

// ── Catalog ─────────────────────────────────────────────────────────────────

class ModelCatalog {
public:
  // Returns all registered provider configurations.
  static const std::vector<ProviderConfig>& providers();

  // Returns the ProviderConfig for a given Provider enum value.
  // Throws std::out_of_range if the provider is not found.
  static const ProviderConfig& provider(Provider p);

  // Returns all registered models.
  static const std::vector<ModelConfig>& models();

  // Returns the ModelConfig for a given catalog id.
  // Returns nullptr if not found.
  static const ModelConfig* find_model(const std::string& id);

  // Returns all models that belong to a given provider.
  static std::vector<const ModelConfig*> models_for_provider(Provider p);

  // Convenience: online providers only.
  static std::vector<Provider> online_providers();

  // Filter by cost tier.
  static std::vector<const ModelConfig*> models_for_tier(CostTier t);

  // Filter by category.
  static std::vector<const ModelConfig*> models_for_category(ModelCategory c);

  // Filter by both tier and category.
  static std::vector<const ModelConfig*>
  models_for_tier_and_category(CostTier t, ModelCategory c);

  // Multi-line preview string for a model (displayed in the model picker).
  static std::string preview(const ModelConfig& m);
};

} // namespace Core
