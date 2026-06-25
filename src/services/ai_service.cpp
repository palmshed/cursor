#include "services/ai_service.h"
#include "services/web_service.h"
#include <nlohmann/json.hpp>
#include <sstream>

// Use the HeaderMap type from web_service.h

namespace Services {

// ── Construction ──────────────────────────────────────────────────────────────

AIService::AIService(const Core::ModelConfig& model,
                     const std::string& api_key)
    : model_(model),
      provider_(Core::ModelCatalog::provider(model.provider)),
      api_key_(api_key) {}

void AIService::override_api_model(const std::string& name) {
  model_.api_model = name;
}

// ── Availability ──────────────────────────────────────────────────────────────

bool AIService::is_available() const {
  if (provider_.online) {
    return !api_key_.empty();
  }
  return true; // local providers (Ollama) never need a key
}

// ── URL selection ─────────────────────────────────────────────────────────────

std::string AIService::get_url() const {
  if (std::getenv("TEST_MODE")) {
    return provider_.test_url;
  }
  return provider_.base_url;
}

// ── System prompt ─────────────────────────────────────────────────────────────

std::string AIService::build_system_prompt(const std::string& context) const {
  const std::string body =
      "You answer questions from repository evidence.\n\n"
      "Repository investigation has already been performed. "
      "The results are in the conversation history below.\n\n"
      "RULES:\n"
      "- Synthesize answers from the evidence provided\n"
      "- Prefer evidence over your general knowledge\n"
      "- Never suggest internal tools or commands\n"
      "- Never emit command syntax of any kind\n"
      "- Be concise and precise\n\n"
      "Conversation history:\n" +
      context;
  return body;
}

// ── Payload construction ──────────────────────────────────────────────────────

nlohmann::json AIService::create_payload(const std::string& user_input,
                                         const std::string& context,
                                         const std::string& system_prompt_override) const {
  const std::string system_prompt = system_prompt_override.empty() ?
                                    build_system_prompt(context) :
                                    system_prompt_override;

  // Cerebras uses streaming SSE
  if (provider_.response_fmt == Core::ResponseFormat::SSEStream) {
    return {{"model", model_.api_model},
            {"messages",
             {{{"role", "system"}, {"content", system_prompt}},
              {{"role", "user"},   {"content", user_input}}}},
            {"stream", true},
            {"max_completion_tokens", 4096},
            {"temperature", 0.7},
            {"top_p", 0.9}};
  }

  // Ollama uses its own format
  if (provider_.response_fmt == Core::ResponseFormat::OllamaChat) {
    return {{"model", model_.api_model},
            {"stream", false},
            {"messages",
             {{{"role", "system"}, {"content", system_prompt}},
              {{"role", "user"},   {"content", user_input}}}}};
  }

  // Standard OpenAI-compatible format
  return {{"model", model_.api_model},
          {"messages",
           {{{"role", "system"}, {"content", system_prompt}},
            {{"role", "user"},   {"content", user_input}}}},
          {"max_tokens", 1000},
          {"temperature", 0.7}};
}

// ── SSE stream parser (Cerebras) ──────────────────────────────────────────────

std::string AIService::parse_sse_stream(const std::string& response) const {
  std::string result;
  std::istringstream stream(response);
  std::string line;

  while (std::getline(stream, line)) {
    if (!line.starts_with("data: ")) continue;
    std::string json_str = line.substr(6);
    if (json_str == "[DONE]") break;

    try {
      auto j = nlohmann::json::parse(json_str);
      if (j.contains("choices") && !j["choices"].empty()) {
        auto& choice = j["choices"][0];
        if (choice.contains("delta") && choice["delta"].contains("content")) {
          result += choice["delta"]["content"].get<std::string>();
        } else if (choice.contains("text")) {
          result += choice["text"].get<std::string>();
        }
      }
    } catch (const std::exception&) {
      continue; // ignore malformed chunk
    }
  }
  return result;
}

// ── Response parser ───────────────────────────────────────────────────────────

std::string AIService::parse_response(const std::string& body) const {
  // Strip leading whitespace
  const size_t start = body.find_first_not_of(" \t\r\n");
  const std::string clean = (start == std::string::npos) ? body : body.substr(start);

  auto j = nlohmann::json::parse(clean);

  switch (provider_.response_fmt) {
  case Core::ResponseFormat::OllamaChat:
    if (j.contains("message") && j["message"].contains("content"))
      return j["message"]["content"].get<std::string>();
    if (j.contains("response"))
      return j["response"].get<std::string>();
    break;

  case Core::ResponseFormat::OpenAIChat:
  case Core::ResponseFormat::SSEStream: // non-stream fallback
    if (j.contains("choices") && !j["choices"].empty()) {
      auto& choice = j["choices"][0];
      if (choice.contains("message") && choice["message"].contains("content"))
        return choice["message"]["content"].get<std::string>();
      if (choice.contains("text"))
        return choice["text"].get<std::string>();
    }
    break;
  }

  return "Error: Unexpected response format from AI service: " + body;
}

// ── Public chat interface ─────────────────────────────────────────────────────

std::string AIService::chat(const std::string& user_input,
                            const std::string& context,
                            const std::string& system_prompt_override) {
  if (!is_available()) {
    return "Error: AI service is not available. Please check your API key and "
           "internet connection.";
  }

  const auto payload  = create_payload(user_input, context, system_prompt_override);
  const auto url      = get_url();

  try {
    HeaderMap headers = {{"Content-Type", "application/json"},
                         {"Accept",       "application/json"},
                         {"User-Agent",   "Cursor/1.0"}};

    // Auth header
    switch (provider_.auth_scheme) {
    case Core::AuthScheme::BearerToken:
      headers["Authorization"] = "Bearer " + api_key_;
      break;
    case Core::AuthScheme::XApiKey:
      headers["X-API-Key"]  = api_key_;
      headers["Accept"]     = "text/event-stream";
      break;
    case Core::AuthScheme::None:
      break;
    }

    WebService web_service;
    const std::string json_body = payload.dump();
    const WebResponse response  = web_service.post_json(url, json_body, headers);

    if (response.status_code != 200) {
      std::string err = response.content;
      if (err.length() > 500) err = err.substr(0, 500) + "...[truncated]";
      return "Error: AI service returned status code " +
             std::to_string(response.status_code) + " - " + err +
             " | Error: " + response.error_message;
    }

    // Streaming response (Cerebras SSE)
    if (provider_.response_fmt == Core::ResponseFormat::SSEStream) {
      return parse_sse_stream(response.content);
    }

    return parse_response(response.content);

  } catch (const std::exception& e) {
    return "Error: " + std::string(e.what());
  }
  return "Error: Unknown error occurred in AI service";
}

} // namespace Services
