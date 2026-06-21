#include "services/execution_engine.h"
#include "services/confidence_service.h"
#include "ui/ui_manager.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <vector>

namespace Services {

// ---------------------------------------------------------------------------
// EvidenceStore
// ---------------------------------------------------------------------------

void EvidenceStore::add_fact(const std::string &fact) {
  facts.push_back(fact);
}

bool EvidenceStore::has_fact_containing(const std::string &keyword) const {
  for (auto &f : facts) {
    if (f.find(keyword) != std::string::npos)
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Goal classification
// ---------------------------------------------------------------------------

static bool word_boundary(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\0' ||
         c == '.' || c == ',' || c == '!' || c == '?' ||
         c == ')' || c == ']' || c == '}';
}

static bool contains_any(const std::string &text,
                         const std::vector<std::string> &terms) {
  std::string lower = text;
  for (auto &c : lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (auto &t : terms) {
    std::string tl = t;
    for (auto &c : tl)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    size_t pos = 0;
    while ((pos = lower.find(tl, pos)) != std::string::npos) {
      // For multi-word terms, ensure word-boundary before and after
      bool boundary_before = (pos == 0) || word_boundary(lower[pos - 1]);
      bool boundary_after = (pos + tl.size() >= lower.size()) ||
                             word_boundary(lower[pos + tl.size()]);
      if (boundary_before && boundary_after)
        return true;
      pos++;
    }
  }
  return false;
}

ExecutionEngine::GoalType ExecutionEngine::classify_goal(
    const std::string &goal) {
  if (contains_any(goal, {"ci", "github action", "workflow", "gh run",
                          "ci/cd", "actions"}))
    return CICheck;

  // Exclude general chat patterns before checking codebase keywords
  if (contains_any(goal, {"how are you", "how do you", "how do i",
                           "how does one", "how can i", "how can you",
                           "what is the difference", "what is a",
                           "who are you", "what can you"}))
    return GeneralChat;

  if (contains_any(goal, {"where", "what is", "what does", "what's",
                           "how does", "how is", "how are",
                           "find", "search", "grep", "locate",
                           "show me", "list", "tell me about",
                           "explain", "describe", "overview",
                           "architecture",
                           "in this project", "in this repo"}))
    return CodebaseQuery;

  if (contains_any(goal, {"add", "implement", "refactor", "fix", "migrate",
                          "create", "remove", "update", "upgrade",
                          "delete", "rename", "extract", "build",
                          "install", "setup", "configure"}))
    return CodeChange;

  return GeneralChat;
}

std::string ExecutionEngine::goal_type_name(GoalType t) {
  switch (t) {
    case CICheck: return "CI Investigation";
    case CodebaseQuery: return "Repository Investigation";
    case CodeChange: return "Code Change";
    case GeneralChat: return "General Chat";
  }
  return "Unknown";
}

// ---------------------------------------------------------------------------
// Tool selection
// ---------------------------------------------------------------------------

ToolCall ExecutionEngine::select_next_tool(
    const std::string &goal, GoalType type, const EvidenceStore &evidence) {

  switch (type) {
    case CICheck: {
      // 1. List recent runs
      if (!evidence.has_fact_containing("gh run list"))
        return {"gh", "run list --limit 5 --json databaseId,displayTitle,headBranch,conclusion,createdAt,workflowName"};

      // 2. Read workflow files if CI failures found
      if (!evidence.has_fact_containing("read workflow") &&
          evidence.has_fact_containing("failure")) {
        // Find the workflow file from evidence
        for (auto &f : evidence.facts) {
          if (f.find(".yml") != std::string::npos ||
              f.find(".yaml") != std::string::npos) {
            return {"grep", ".github/workflows/"};
          }
        }
        return {"read", ".github/workflows/"};
      }

      return {};
    }

    case CodebaseQuery: {
      // 1. Search the codebase
      if (!evidence.has_fact_containing("grep") &&
          !evidence.has_fact_containing("search")) {
        // Extract search term from goal (case-insensitive prefix matching)
        std::string term = goal;
        std::string lower_term = term;
        for (auto &c : lower_term) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        // Remove common prefixes (longest first to handle compounds)
        for (auto &prefix : {"tell me about ", "find where ", "search for ",
                             "where is ", "what is ", "what does ",
                             "how does ", "show me ", "locate ",
                             "find ", "where ", "grep "}) {
          size_t p = lower_term.find(prefix);
          if (p == 0) {
            term = term.substr(p + strlen(prefix));
            lower_term = lower_term.substr(p + strlen(prefix));
            break;
          }
        }
        // Remove common suffixes
        for (const char *suffix : {" in this project", " in this codebase",
                             " in this repo", " in the code",
                             " in code", " in files", " is defined",
                             " is implemented"}) {
          size_t p = lower_term.rfind(suffix);
          if (p != std::string::npos && p + strlen(suffix) == lower_term.size()) {
            term = term.substr(0, p);
            lower_term = lower_term.substr(0, p);
            break;
          }
        }
         // Clean the term: remove stop words and trailing noise words
         {
           static const char *stop_words[] = {"the ", "a ", "an ", "this ", "that "};
           for (auto *sw : stop_words) {
             if (lower_term.find(sw) == 0) {
               term = term.substr(strlen(sw));
               lower_term = lower_term.substr(strlen(sw));
               break;
             }
           }
         }
         {
           static const char *noise_words[] = {
             " declaration", " system", " results", " code",
             " method", " function", " class", " service",
             " utility", " fix", " target", " heuristic",
             " work", " defined", " implemented", " collector"};
           for (auto *nw : noise_words) {
             size_t p = term.rfind(nw);
             if (p != std::string::npos && p + strlen(nw) == term.size()) {
               term = term.substr(0, p);
               break;
             }
           }
         }
         // If term is multi-word, try each word individually, prefer longest
         if (term.find(' ') != std::string::npos) {
           std::vector<std::string> words;
           std::istringstream ss(term);
           std::string w;
           while (ss >> w) {
             static const char *filter_words[] = {
               "and", "or", "the", "a", "an", "this", "that",
               "in", "of", "to", "for", "system", "code", "file", "service"};
               bool skip = false;
               for (auto *fw : filter_words) {
                 if (w == fw) { skip = true; break; }
               }
               if (w.length() >= 3 && !skip) words.push_back(w);
           }
           if (!words.empty()) {
             // Prefer words with uppercase (likely code identifiers),
             // otherwise prefer shortest distinctive word
             auto has_upper = [](const std::string &s) {
               for (auto c : s) if (std::isupper(static_cast<unsigned char>(c))) return true;
               return false;
             };
             auto it = std::find_if(words.begin(), words.end(), has_upper);
             if (it != words.end()) {
               term = *it;
             } else {
               term = words[0];
             }
           }
         }
         return {"grep", term};
       }

       // 2. Read files found by grep
      if (evidence.has_fact_containing("grep") &&
          !evidence.has_fact_containing("read")) {
        return {"read", ""};
      }

      return {};
    }

    case CodeChange: {
      // 1. Discovery
      if (!evidence.has_fact_containing("discovery"))
        return {"discovery", ""};

      // 2. Grep relevant files
      if (!evidence.has_fact_containing("grep"))
        return {"grep", ""};

      // 3. Read key files
      if (!evidence.has_fact_containing("read"))
        return {"read", ""};

      // 4. Build
      if (!evidence.has_fact_containing("build"))
        return {"cmake", "--build build"};

      // 5. Tests
      if (!evidence.has_fact_containing("test"))
        return {"ctest", "--test-dir build"};

      return {};
    }

    case GeneralChat:
      return {};
  }

  return {};
}

// ---------------------------------------------------------------------------
// Completion check
// ---------------------------------------------------------------------------

bool ExecutionEngine::check_completion(const std::string & /*goal*/,
                                        GoalType type,
                                        const EvidenceStore &evidence) {
  switch (type) {
    case CICheck:
      // Done when we've listed runs AND either no failures or we've read workflows
      return evidence.has_fact_containing("gh run list") &&
             (!evidence.has_fact_containing("failure") ||
              evidence.has_fact_containing("read workflow"));

    case CodebaseQuery:
      // Done when we've searched AND found results AND read files
      return evidence.has_fact_containing("grep:results") &&
             evidence.has_fact_containing("read:results");

    case CodeChange:
      // Done when discovery, grep, read, build, and tests produced results
      return evidence.has_fact_containing("discovery") &&
             evidence.has_fact_containing("grep:results") &&
             evidence.has_fact_containing("read:results") &&
             evidence.has_fact_containing("build") &&
             evidence.has_fact_containing("test");

    case GeneralChat:
      return true;
  }
  return true;
}

bool ExecutionEngine::goal_is_achieved(const std::string &goal,
                                        const EvidenceStore &evidence) {
  return check_completion(goal, classify_goal(goal), evidence);
}

// ---------------------------------------------------------------------------
// Main execution loop
// ---------------------------------------------------------------------------

ExecutionResult ExecutionEngine::execute(const std::string &goal,
                                           ToolRunner run_tool,
                                           Core::UIManager &ui) {
  ExecutionResult result;
  EvidenceStore evidence;
  GoalType type = classify_goal(goal);

  ui.show_pipeline_section(goal_type_name(type));

  const int MAX_ITERATIONS = 20;
  std::vector<ConfidenceResult> confidence_history;
  int iteration_count = 0;

  for (iteration_count = 0; iteration_count < MAX_ITERATIONS; iteration_count++) {
    if (check_completion(goal, type, evidence))
      break;

    ToolCall tc = select_next_tool(goal, type, evidence);
    if (tc.tool.empty())
      break;

    ui.show_tool_invocation(tc.tool, tc.args);

    std::string output = run_tool(tc);

    std::string fact = tc.tool;
    if (!tc.args.empty())
      fact += " " + tc.args;
    evidence.add_fact(fact);
    evidence.facts.push_back("[" + fact + "] " +
                              output.substr(0, 200));

    // Track whether tool produced meaningful results (separate from attempt)
    bool has_results = false;
    if (tc.tool == "grep") {
      has_results = !output.empty() && output != "no matches";
    } else if (tc.tool == "read") {
      has_results = !output.empty() && output != "no files to read";
    } else {
      has_results = !output.empty();
    }
    if (has_results)
      evidence.add_fact(tc.tool + ":results");

    // Evaluate confidence after each tool run
    ConfidenceResult cr;
    if (tc.tool == "grep") {
      int hits = 0;
      std::istringstream ss(output);
      std::string line;
      while (std::getline(ss, line))
        if (!line.empty() && line != "no matches")
          hits++;
      cr = ConfidenceService::after_search(tc.args, hits);
    } else if (tc.tool == "read") {
      cr = ConfidenceService::after_read(1, true);
    } else if (tc.tool == "cmake") {
      bool ok = output.find("error") == std::string::npos;
      cr = ConfidenceService::after_build(ok, ok ? "" : output.substr(0, 200));
    } else if (tc.tool == "ctest") {
      bool ok = (output.find("failed") == std::string::npos &&
                 output.find("FAILED") == std::string::npos);
      cr.score = ok ? 0.9 : 0.3;
      cr.reason = ok ? "tests passed" : "tests failed";
    } else {
      cr.score = 0.5;
      cr.reason = "tool executed";
    }

    confidence_history.push_back(cr);

    // Check if confidence is too low to continue
    ConfidenceResult combined = ConfidenceService::combine(confidence_history);
    if (ConfidenceService::should_stop(combined, 0.2)) {
      result.stopped_early = true;
      result.stop_reason = "confidence too low: " + combined.reason;
      break;
    }
  }

  // Build summary
  std::string summary;
  summary += "Goal type: " + goal_type_name(type) + "\n";
  summary += "Evidence collected: " + std::to_string(evidence.facts.size()) +
             " facts\n";
  for (auto &f : evidence.facts) {
    if (f.size() > 100)
      summary += "  " + f.substr(0, 100) + "...\n";
    else
      summary += "  " + f + "\n";
  }

  // Final confidence
  ConfidenceResult final_confidence = ConfidenceService::combine(confidence_history);
  summary += "Confidence: " + final_confidence.reason + "\n";

  result.success = check_completion(goal, type, evidence);
  result.summary = summary;
  result.evidence = std::move(evidence);
  result.goal_type = static_cast<int>(type);
  result.confidence = final_confidence.score;

  // Determine outcome
  if (result.stopped_early) {
    result.outcome = Core::Outcome::InsufficientEvidence;
  } else if (result.success) {
    result.outcome = Core::Outcome::Success;
  } else {
    result.outcome = Core::Outcome::Failure;
  }

  // Recovery metrics
  result.recovery_metrics.attempts = iteration_count;
  result.recovery_metrics.evidence_found =
      result.evidence.has_fact_containing("grep:results") ||
      result.evidence.has_fact_containing("read:results") ||
      result.evidence.has_fact_containing("build") ||
      result.evidence.has_fact_containing("test");
  result.recovery_metrics.verification_found =
      result.evidence.has_fact_containing("build") ||
      result.evidence.has_fact_containing("test");
  result.recovery_metrics.confidence_delta =
      final_confidence.score - (confidence_history.empty() ? 0.0
                                : confidence_history.front().score);

  return result;
}

} // namespace Services
