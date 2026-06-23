#include "core/model_catalog.h"
#include <sstream>
#include <stdexcept>

namespace Core {

// ── Provider registry ────────────────────────────────────────────────────────

const std::vector<ProviderConfig>& ModelCatalog::providers() {
  static const std::vector<ProviderConfig> kProviders = {
    {
      Provider::Ollama,
      "http://localhost:11434/api/chat",
      "http://mock-ollama:11434/api/chat",
      AuthScheme::None,
      ResponseFormat::OllamaChat,
      /*online=*/false,
    },
    {
      Provider::OpenRouter,
      "https://openrouter.ai/api/v1/chat/completions",
      "http://mock-openrouter/api/v1/chat/completions",
      AuthScheme::BearerToken,
      ResponseFormat::OpenAIChat,
      /*online=*/true,
    },
    {
      Provider::OpenAI,
      "https://api.openai.com/v1/chat/completions",
      "http://mock-openai/v1/chat/completions",
      AuthScheme::BearerToken,
      ResponseFormat::OpenAIChat,
      /*online=*/true,
    },
    {
      Provider::Groq,
      "https://api.groq.com/openai/v1/chat/completions",
      "http://mock-groq/openai/v1/chat/completions",
      AuthScheme::BearerToken,
      ResponseFormat::OpenAIChat,
      /*online=*/true,
    },
    {
      Provider::Together,
      "https://api.together.xyz/v1/chat/completions",
      "http://mock-together/v1/chat/completions",
      AuthScheme::BearerToken,
      ResponseFormat::OpenAIChat,
      /*online=*/true,
    },
    {
      Provider::Fireworks,
      "https://api.fireworks.ai/inference/v1/chat/completions",
      "http://mock-fireworks/inference/v1/chat/completions",
      AuthScheme::BearerToken,
      ResponseFormat::OpenAIChat,
      /*online=*/true,
    },
    {
      Provider::Cerebras,
      "https://api.cerebras.ai/v1/chat/completions",
      "http://mock-cerebras/v1/chat/completions",
      AuthScheme::XApiKey,
      ResponseFormat::SSEStream,
      /*online=*/true,
    },
    {
      Provider::DeepSeek,
      "https://api.deepseek.com/v1/chat/completions",
      "http://mock-deepseek/v1/chat/completions",
      AuthScheme::BearerToken,
      ResponseFormat::OpenAIChat,
      /*online=*/true,
    },
  };
  return kProviders;
}

const ProviderConfig& ModelCatalog::provider(Provider p) {
  for (const auto& pc : providers()) {
    if (pc.id == p) return pc;
  }
  throw std::out_of_range("ModelCatalog: unknown provider");
}

// ── Model registry ───────────────────────────────────────────────────────────

const std::vector<ModelConfig>& ModelCatalog::models() {
  static const std::vector<ModelConfig> kModels = {

    // ══════════════════════════════════════════════════════════════════════════
    //  FREE — OpenRouter
    // ══════════════════════════════════════════════════════════════════════════

    {
      "openrouter-nemotron-ultra",
      "Nemotron Ultra",
      Provider::OpenRouter,
      "nvidia/nemotron-ultra-253b-v1:free",
      { /*tools=*/false, /*reasoning=*/true, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::Reasoning,
    },
    {
      "openrouter-deepseek-r1",
      "DeepSeek R1",
      Provider::OpenRouter,
      "deepseek/deepseek-r1:free",
      { /*tools=*/false, /*reasoning=*/true, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::Reasoning,
    },
    {
      "openrouter-qwen3-coder",
      "Qwen3 Coder",
      Provider::OpenRouter,
      "qwen/qwen3-coder:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::Coding,
    },
    {
      "openrouter-north-mini-code",
      "North Mini Code",
      Provider::OpenRouter,
      "cohere/north-mini-code:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::Coding,
    },
    {
      "openrouter-nemotron-3-ultra",
      "Nemotron 3 Ultra",
      Provider::OpenRouter,
      "nvidia/nemotron-3-ultra-550b-a55b:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::General,
      /*recommended=*/true,
    },
    {
      "openrouter-nemotron-3-super",
      "Nemotron 3 Super",
      Provider::OpenRouter,
      "nvidia/nemotron-3-super-120b-a12b:free",
      { /*tools=*/false, /*reasoning=*/true, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::Reasoning,
    },
    {
      "openrouter-nemotron-3-nano-omni",
      "Nemotron 3 Nano Omni",
      Provider::OpenRouter,
      "nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free",
      { /*tools=*/false, /*reasoning=*/true, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::Reasoning,
    },
    {
      "openrouter-nemotron-3-nano",
      "Nemotron 3 Nano",
      Provider::OpenRouter,
      "nvidia/nemotron-3-nano-30b-a3b:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::General,
    },
    {
      "openrouter-nemotron-nano-12b-vl",
      "Nemotron Nano 12B VL",
      Provider::OpenRouter,
      "nvidia/nemotron-nano-12b-v2-vl:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/true, /*free=*/true },
      CostTier::Free,
      ModelCategory::Vision,
    },
    {
      "openrouter-nemotron-nano-9b",
      "Nemotron Nano 9B",
      Provider::OpenRouter,
      "nvidia/nemotron-nano-9b-v2:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::General,
    },
    {
      "openrouter-llama-nemotron-rerank",
      "Llama Nemotron Rerank VL",
      Provider::OpenRouter,
      "nvidia/llama-nemotron-rerank-vl-1b-v2:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/true, /*free=*/true },
      CostTier::Free,
      ModelCategory::Vision,
    },
    {
      "openrouter-llama-nemotron-embed",
      "Llama Nemotron Embed VL",
      Provider::OpenRouter,
      "nvidia/llama-nemotron-embed-vl-1b-v2:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/true, /*free=*/true },
      CostTier::Free,
      ModelCategory::Vision,
    },
    {
      "openrouter-nemotron-3.5-safety",
      "Nemotron 3.5 Safety",
      Provider::OpenRouter,
      "nvidia/nemotron-3.5-content-safety:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::Experimental,
    },
    {
      "openrouter-llama-3.3-70b",
      "Llama 3.3 70B",
      Provider::OpenRouter,
      "meta-llama/llama-3.3-70b-instruct:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::General,
      /*recommended=*/true,
    },
    {
      "openrouter-llama-3.2-3b",
      "Llama 3.2 3B",
      Provider::OpenRouter,
      "meta-llama/llama-3.2-3b-instruct:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::General,
    },
    {
      "openrouter-hermes-3-405b",
      "Hermes 3 405B",
      Provider::OpenRouter,
      "nousresearch/hermes-3-llama-3.1-405b:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::General,
    },
    {
      "openrouter-qwen3-next-80b",
      "Qwen3 Next 80B",
      Provider::OpenRouter,
      "qwen/qwen3-next-80b-a3b-instruct:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::General,
    },
    {
      "openrouter-gemma-4-26b",
      "Gemma 4 26B",
      Provider::OpenRouter,
      "google/gemma-4-26b-a4b-it:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::General,
    },
    {
      "openrouter-gemma-4-31b",
      "Gemma 4 31B",
      Provider::OpenRouter,
      "google/gemma-4-31b-it:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::General,
    },
    {
      "openrouter-gpt-oss-120b",
      "GPT-OSS 120B",
      Provider::OpenRouter,
      "openai/gpt-oss-120b:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::General,
    },
    {
      "openrouter-gpt-oss-20b",
      "GPT-OSS 20B",
      Provider::OpenRouter,
      "openai/gpt-oss-20b:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::General,
    },
    {
      "openrouter-laguna-xs.2",
      "Laguna XS.2",
      Provider::OpenRouter,
      "poolside/laguna-xs.2:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::Coding,
    },
    {
      "openrouter-laguna-m.1",
      "Laguna M.1",
      Provider::OpenRouter,
      "poolside/laguna-m.1:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::Coding,
    },
    {
      "openrouter-lfm-thinking",
      "LFM 2.5 Thinking",
      Provider::OpenRouter,
      "liquid/lfm-2.5-1.2b-thinking:free",
      { /*tools=*/false, /*reasoning=*/true, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::Reasoning,
    },
    {
      "openrouter-lfm-instruct",
      "LFM 2.5 Instruct",
      Provider::OpenRouter,
      "liquid/lfm-2.5-1.2b-instruct:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::General,
    },
    {
      "openrouter-dolphin-mistral-24b",
      "Dolphin Mistral 24B",
      Provider::OpenRouter,
      "cognitivecomputations/dolphin-mistral-24b-venice-edition:free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::General,
    },

    // ══════════════════════════════════════════════════════════════════════════
    //  FREE — third-party providers
    // ══════════════════════════════════════════════════════════════════════════

    {
      "together-llama3.3-70b-free",
      "Llama 3.3 70B Turbo",
      Provider::Together,
      "meta-llama/Llama-3.3-70B-Instruct-Turbo-Free",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Free,
      ModelCategory::General,
    },

    // ══════════════════════════════════════════════════════════════════════════
    //  PAID — OpenAI
    // ══════════════════════════════════════════════════════════════════════════

    {
      "openai-gpt4.1",
      "GPT-4.1",
      Provider::OpenAI,
      "gpt-4.1",
      { /*tools=*/true, /*reasoning=*/false, /*vision=*/true, /*free=*/false },
      CostTier::Paid,
      ModelCategory::General,
      /*recommended=*/true,
    },
    {
      "openai-gpt4o",
      "GPT-4o",
      Provider::OpenAI,
      "gpt-4o",
      { /*tools=*/true, /*reasoning=*/false, /*vision=*/true, /*free=*/false },
      CostTier::Paid,
      ModelCategory::Vision,
    },

    // ══════════════════════════════════════════════════════════════════════════
    //  PAID — Groq
    // ══════════════════════════════════════════════════════════════════════════

    {
      "groq-llama3.1-70b",
      "Llama 3.1 70B",
      Provider::Groq,
      "llama-3.1-70b-versatile",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/false },
      CostTier::Paid,
      ModelCategory::General,
    },
    {
      "groq-llama3.3-70b",
      "Llama 3.3 70B",
      Provider::Groq,
      "llama-3.3-70b-versatile",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/false },
      CostTier::Paid,
      ModelCategory::General,
    },

    // ══════════════════════════════════════════════════════════════════════════
    //  PAID — Fireworks
    // ══════════════════════════════════════════════════════════════════════════

    {
      "fireworks-llama3-70b",
      "Llama 3 70B",
      Provider::Fireworks,
      "accounts/fireworks/models/llama-v3-70b-instruct",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/false },
      CostTier::Paid,
      ModelCategory::General,
    },

    // ══════════════════════════════════════════════════════════════════════════
    //  PAID — Cerebras
    // ══════════════════════════════════════════════════════════════════════════

    {
      "cerebras-llama4-maverick",
      "Llama 4 Maverick",
      Provider::Cerebras,
      "llama-4-maverick-17b-128e-instruct",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/false },
      CostTier::Paid,
      ModelCategory::General,
    },

    // ══════════════════════════════════════════════════════════════════════════
    //  PAID — DeepSeek
    // ══════════════════════════════════════════════════════════════════════════

    {
      "deepseek-chat",
      "DeepSeek Chat",
      Provider::DeepSeek,
      "deepseek-chat",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/false },
      CostTier::Paid,
      ModelCategory::General,
      /*recommended=*/true,
    },
    {
      "deepseek-r1",
      "DeepSeek R1",
      Provider::DeepSeek,
      "deepseek-reasoner",
      { /*tools=*/false, /*reasoning=*/true, /*vision=*/false, /*free=*/false },
      CostTier::Paid,
      ModelCategory::Reasoning,
    },

    // ══════════════════════════════════════════════════════════════════════════
    //  LOCAL — Ollama (defaults; startup discovers the real list at runtime)
    // ══════════════════════════════════════════════════════════════════════════

    {
      "ollama-llama3.2-3b",
      "Llama 3.2 3B",
      Provider::Ollama,
      "llama3.2:3b",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Local,
      ModelCategory::General,
    },
    {
      "ollama-llama3.2-latest",
      "Llama 3.2 Latest",
      Provider::Ollama,
      "llama3.2:latest",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Local,
      ModelCategory::General,
    },
    {
      "ollama-llama3.1-latest",
      "Llama 3.1 Latest",
      Provider::Ollama,
      "llama3.1:latest",
      { /*tools=*/false, /*reasoning=*/false, /*vision=*/false, /*free=*/true },
      CostTier::Local,
      ModelCategory::General,
    },
  };
  return kModels;
}

const ModelConfig* ModelCatalog::find_model(const std::string& id) {
  for (const auto& m : models()) {
    if (m.id == id) return &m;
  }
  return nullptr;
}

std::vector<const ModelConfig*>
ModelCatalog::models_for_provider(Provider p) {
  std::vector<const ModelConfig*> result;
  for (const auto& m : models()) {
    if (m.provider == p) result.push_back(&m);
  }
  return result;
}

std::vector<Provider> ModelCatalog::online_providers() {
  std::vector<Provider> result;
  for (const auto& pc : providers()) {
    if (pc.online) result.push_back(pc.id);
  }
  return result;
}

std::vector<const ModelConfig*>
ModelCatalog::models_for_tier(CostTier t) {
  std::vector<const ModelConfig*> result;
  for (const auto& m : models()) {
    if (m.tier == t) result.push_back(&m);
  }
  return result;
}

std::vector<const ModelConfig*>
ModelCatalog::models_for_category(ModelCategory c) {
  std::vector<const ModelConfig*> result;
  for (const auto& m : models()) {
    if (m.category == c) result.push_back(&m);
  }
  return result;
}

std::vector<const ModelConfig*>
ModelCatalog::models_for_tier_and_category(CostTier t, ModelCategory c) {
  std::vector<const ModelConfig*> result;
  for (const auto& m : models()) {
    if (m.tier == t && m.category == c) result.push_back(&m);
  }
  return result;
}

// ── Preview renderer ──────────────────────────────────────────────────────────

static const char* tier_display(CostTier t) {
  switch (t) {
  case CostTier::Local: return "Local";
  case CostTier::Free:  return "Free Online";
  case CostTier::Paid:  return "Paid Online";
  }
  return "";
}

static const char* short_provider(Provider p) {
  switch (p) {
  case Provider::Ollama:     return "ollama";
  case Provider::OpenRouter: return "openrouter";
  case Provider::OpenAI:     return "openai";
  case Provider::Groq:       return "groq";
  case Provider::Together:   return "together";
  case Provider::Fireworks:  return "fireworks";
  case Provider::Cerebras:   return "cerebras";
  case Provider::DeepSeek:   return "deepseek";
  }
  return "unknown";
}

static const char* capability_tag(const ModelCapabilities& caps) {
  if (caps.supports_reasoning && caps.supports_tools) return "Reasoning \u00b7 Tools";
  if (caps.supports_reasoning)                     return "Reasoning";
  if (caps.supports_tools)                          return "Tools";
  if (caps.supports_vision)                         return "Vision";
  return "";
}

std::string ModelCatalog::preview(const ModelConfig& m) {
  std::ostringstream out;

  if (m.recommended) out << "\xe2\x98\x85 Recommended\n";
  out << tier_display(m.tier) << " \u00b7 " << short_provider(m.provider);

  const char* cap = capability_tag(m.caps);
  if (cap[0]) out << "\n\n" << cap;

  return out.str();
}

} // namespace Core
