#include "app/startup.h"
#include "app/menu.h"
#include "core/model_catalog.h"
#include "services/web_service.h"
#include "utils/config.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>

namespace Core {

// ── helpers ───────────────────────────────────────────────────────────────────

static const char* provider_label(Provider p) {
  switch (p) {
  case Provider::OpenRouter: return "openrouter";
  case Provider::OpenAI:     return "openai";
  case Provider::Groq:       return "groq";
  case Provider::Together:   return "together";
  case Provider::Fireworks:  return "fireworks";
  case Provider::Cerebras:   return "cerebras";
  case Provider::DeepSeek:   return "deepseek";
  case Provider::Ollama:     return "ollama";
  }
  return "unknown";
}

static const char* category_label(ModelCategory c) {
  switch (c) {
  case ModelCategory::Coding:       return "Coding";
  case ModelCategory::General:      return "General";
  case ModelCategory::Reasoning:    return "Reasoning";
  case ModelCategory::Vision:       return "Vision";
  case ModelCategory::Experimental: return "Experimental";
  }
  return "Unknown";
}

static const char* tier_label(CostTier t) {
  switch (t) {
  case CostTier::Local: return "local";
  case CostTier::Free:  return "free";
  case CostTier::Paid:  return "paid";
  }
  return "unknown";
}

static std::string api_key_var(Provider p) {
  switch (p) {
  case Provider::OpenRouter: return "OPENROUTER_API_KEY";
  case Provider::OpenAI:     return "OPENAI_API_KEY";
  case Provider::Groq:       return "GROQ_API_KEY";
  case Provider::Together:   return "TOGETHER_API_KEY";
  case Provider::Fireworks:  return "FIREWORKS_API_KEY";
  case Provider::Cerebras:   return "CEREBRAS_API_KEY";
  case Provider::DeepSeek:   return "DEEPSEEK_API_KEY";
  case Provider::Ollama:     return "";
  }
  return "";
}

// ── Startup::initialize ────────────────────────────────────────────────────────

void Startup::initialize(Agent& agent) {
  while (true) {
    // ── Level 1: Cost Tier ────────────────────────────────────────────────
    const std::vector<std::string> tier_items = {
      "Local Models",
      "Free Online Models",
      "Paid Online Models",
    };
    const int t_idx = show_menu("Select Model Type", tier_items, 0);
    if (t_idx < 0) continue;
    const CostTier chosen_tier = static_cast<CostTier>(t_idx);

    // ── Level 2: Model Category ──────────────────────────────────────────
    if (chosen_tier == CostTier::Local) {
      // ── Local: discover Ollama models ─────────────────────────────────
      std::vector<std::string> local_models;
      try {
        auto resp = Services::WebService::fetch_url("http://localhost:11434/api/tags");
        if (resp.success && !resp.content.empty()) {
          auto j = nlohmann::json::parse(resp.content);
          if (j.contains("models") && j["models"].is_array()) {
            for (const auto& m : j["models"]) {
              if (m.contains("name")) local_models.push_back(m["name"]);
            }
          }
        }
      } catch (...) {}

      if (local_models.empty()) {
        local_models = {"llama3.2:3b", "llama3.2:latest", "llama3.1:latest"};
      }

      const int m = show_menu("Local Model", local_models, 0);
      if (m < 0) continue;

      const std::string& chosen_name = local_models[static_cast<size_t>(m)];
      agent.state_.active_model = {
        "ollama-custom",
        chosen_name,
        Provider::Ollama,
        chosen_name,
        {},
        CostTier::Local,
        ModelCategory::General,
      };
      agent.api_key_ = "";
      std::cout << "  local \u00b7 " << chosen_name << "\n\n";
      break;
    }

    // ── Free / Paid: pick a category ─────────────────────────────────────
    // Build the category list dynamically — skip empty ones.
    const std::vector<ModelCategory> all_cats = {
      ModelCategory::Coding,
      ModelCategory::General,
      ModelCategory::Reasoning,
      ModelCategory::Vision,
      ModelCategory::Experimental,
    };
    std::vector<ModelCategory> available_cats;
    std::vector<std::string> cat_labels;
    for (auto c : all_cats) {
      if (!ModelCatalog::models_for_tier_and_category(chosen_tier, c).empty()) {
        available_cats.push_back(c);
        cat_labels.push_back(category_label(c));
      }
    }

    const int c_idx = show_menu("Category", cat_labels, 1);
    if (c_idx < 0) continue;
    const ModelCategory chosen_cat = available_cats[static_cast<size_t>(c_idx)];

    // ── Level 3: Pick a Model ───────────────────────────────────────────
    const auto filtered = ModelCatalog::models_for_tier_and_category(chosen_tier, chosen_cat);
    const std::string env_model = Utils::Config::get_env_var("MODEL_NAME");
    const ModelConfig* chosen = nullptr;

    if (filtered.empty()) {
      if (!env_model.empty()) {
        ModelConfig fallback;
        fallback.id           = "custom";
        fallback.display_name = "Custom (env MODEL_NAME)";
        fallback.provider     = Provider::OpenRouter;
        fallback.api_model    = env_model;
        fallback.tier         = chosen_tier;
        fallback.category     = chosen_cat;
        agent.state_.active_model = fallback;
        chosen = &agent.state_.active_model;
      }
      continue;
    }

    if (filtered.size() > 5) {
      // ── Large category: sub-menu (Recommended / All Models) ──────────
      const std::string title = std::string(category_label(chosen_cat)) + " Models";
      const std::vector<std::string> sub_items = {"Recommended", "All Models"};
      const int sub = show_menu(title, sub_items, 0);
      if (sub < 0) continue;

      std::vector<const ModelConfig*> pool;
      if (sub == 0) {
        for (const auto* m : filtered) {
          if (m->recommended) pool.push_back(m);
        }
        if (pool.empty()) pool = filtered; // safety: fall back to all
      } else {
        pool = filtered;
      }

      std::vector<std::string> model_items;
      for (const auto* m : pool) model_items.push_back(m->display_name);

      const int m_idx = show_menu(
          sub == 0 ? "Recommended" : "All Models", model_items, 0,
          [&](int i) { return ModelCatalog::preview(*pool[static_cast<size_t>(i)]); });
      if (m_idx < 0) continue;
      chosen = pool[static_cast<size_t>(m_idx)];
    } else {
      // ── Small category: flat list ─────────────────────────────────────
      std::vector<std::string> model_items;
      for (const auto* m : filtered) model_items.push_back(m->display_name);

      const int m_idx = show_menu("Model", model_items, 0,
          [&](int i) { return ModelCatalog::preview(*filtered[static_cast<size_t>(i)]); });
      if (m_idx < 0) continue;
      chosen = filtered[static_cast<size_t>(m_idx)];
    }

    agent.state_.active_model = *chosen;

    // ── Resolve API key ─────────────────────────────────────────────────
    const std::string key_var = api_key_var(chosen->provider);
    agent.api_key_ = key_var.empty() ? "" : Utils::Config::get_env_var(key_var);
    if (ModelCatalog::provider(chosen->provider).online && agent.api_key_.empty()) {
      throw std::runtime_error(key_var + " not set");
    }

    // ── Summary line ─────────────────────────────────────────────────────
    std::cout << "  " << tier_label(chosen_tier)
              << " \u00b7 " << provider_label(chosen->provider)
              << " \u00b7 " << chosen->display_name << "\n\n";
    break;
  }
}

// ── Startup::is_online_mode ───────────────────────────────────────────────────

bool Startup::is_online_mode(const Agent& agent) {
  return ModelCatalog::provider(agent.state_.active_model.provider).online;
}

} // namespace Core
