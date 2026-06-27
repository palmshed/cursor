#include "services/goal_understanding_service.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Services {

namespace {

std::string to_lower(const std::string &s) {
    std::string out = s;
    for (auto &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool contains_any(const std::string &lower, const std::vector<std::string> &phrases) {
    for (auto &p : phrases) {
        if (lower.find(p) != std::string::npos)
            return true;
    }
    return false;
}

int count_matches(const std::string &lower, const std::vector<std::string> &phrases) {
    int count = 0;
    for (auto &p : phrases) {
        if (lower.find(p) != std::string::npos)
            count++;
    }
    return count;
}

double score_matches(const std::string &lower, const std::vector<std::string> &phrases,
                      double weight_per_match, double max_weight) {
    int matches = count_matches(lower, phrases);
    if (matches == 0) return 0.0;
    return std::min(static_cast<double>(matches) * weight_per_match, max_weight);
}

} // namespace

ParseResult GoalUnderstandingService::parse(const std::string &user_request) {
    ParseResult result;
    std::string lower = to_lower(user_request);

    // Phase 1: Detect intent with scoring
    std::string intent_explanation;
    auto intent_result = detect_intent(user_request, lower, intent_explanation);
    result.goal.intent = intent_result.intent;

    // Phase 2: Detect entity (depends on intent)
    std::string entity_explanation;
    auto entity_result = detect_entity(user_request, lower, intent_result.intent,
                                        entity_explanation);
    result.goal.entity = entity_result.entity;

    // Phase 3: Infer artifact from intent + entity
    result.goal.artifact = infer_artifact(intent_result.intent, entity_result.entity, lower);

    // Phase 4: Infer scope from intent + entity + artifact
    result.goal.scope = infer_scope(intent_result.intent, entity_result.entity,
                                     result.goal.artifact);

    // Phase 5: Calculate confidence
    result.confidence = calculate_confidence(intent_result.intent, intent_result.score,
                                              entity_result.entity, entity_result.score,
                                              result.ambiguities);

    // Build explanation
    std::ostringstream exp;
    exp << intent_explanation;
    if (!entity_explanation.empty()) {
        exp << " + " << entity_explanation;
    }
    result.explanation = exp.str();

    return result;
}

GoalUnderstandingService::IntentResult
GoalUnderstandingService::detect_intent(const std::string &req,
                                         const std::string &lower,
                                         std::string &explanation) {
    (void)req;

    struct IntentRule {
        Intent intent;
        std::vector<std::string> phrases;
        double weight_per_match;
        std::string label;
    };

    static const IntentRule rules[] = {
        {Intent::Chat, {"who are you", "what can you do", "how are you",
                        "how do you work", "explain the concept of",
                        "how do i install "}, 0.35, "chat/greeting"},

        {Intent::Compare, {"difference between", "what is the difference",
                           "compare ", "differ from", "diff between"}, 0.30, "comparison"},

        {Intent::Review, {"review ", "audit "}, 0.30, "review/audit"},

        {Intent::Execute, {"run ", "build ", "compile ", "make "}, 0.25, "execute/build"},

        {Intent::Diagnose, {"why did ", "why does ", "why is ", "what failed",
                            "what went wrong", "check ci", "ci fail",
                            "which test failed"}, 0.30, "diagnose"},

        {Intent::Status, {"last commit", "recent commit", "latest commit",
                          "git history", "git log", "commit history",
                          "what changed", "changed files", "files changed",
                          "modified files", "what files changed",
                          "what files are modified",
                          "files are modified",
                          "show changed files", "show modified files",
                          "check changed files", "check modified files",
                          "commit log",
                          "previous commit",
                          "git status", "current branch", "what branch",
                          "branch am i", "git branch",
                          "uncommitted", "unstaged", "working tree",
                          "did i edit", "what are the current changes",
                          "check if any files changed",
                          "get the latest commit",
                          "git diff", "show diff",
                          "what provider", "what model",
                          "am i online", "which backend",
                          "which provider", "provider am i",
                          "model am i",
                          "what backend",
                          "what is the current state"}, 0.25, "status"},

        {Intent::Locate, {"find ", "where is ", "where are ",
                          "where do we ", "where do you ",
                          "locate ",
                          "grep ", "search for ", "search ", "look for ",
                          " find "}, 0.20, "locate/search"},

        {Intent::Navigate, {"read file ", "show memory",
                            "show me the file", "show me the content"}, 0.20, "navigate/read"},

        {Intent::Modify, {"add ", "implement ", "refactor ", "fix ",
                          "migrate ", "create ", "remove ", "update ",
                          "upgrade ", "delete ", "rename ", "extract ",
                          "setup ", "configure "}, 0.15, "modify"},

        {Intent::Explain, {"explain ", "how does ", "how is ", "how do ",
                           "tell me how ", "tell me about ",
                           "walk me through ", "how it works",
                           "describe ", "describe the",
                           "overview of", "what is this"}, 0.15, "explain/overview"},
    };

    double best_score = 0.0;
    Intent best_intent = Intent::Unknown;
    double second_score = 0.0;

    for (auto &rule : rules) {
        double score = score_matches(lower, rule.phrases,
                                      rule.weight_per_match, 0.95);
        if (score > best_score) {
            second_score = best_score;
            best_score = score;
            best_intent = rule.intent;
            explanation = rule.label;
        } else if (score > second_score) {
            second_score = score;
        }
    }

    // Status override: if "how"/"explain" appears without git signal, not status
    if (best_intent == Intent::Status && second_score > 0.0) {
        if (contains_any(lower, {"how", "explain"}) &&
            !contains_any(lower, {"git", "commit", "changed", "modified", "branch"})) {
            best_intent = Intent::Explain;
            best_score = 0.3;
            explanation = "explain (overrode status)";
        }
    }

    // Locate → Navigate override for direct file reads
    if (best_intent == Intent::Locate) {
        if (contains_any(lower, {"read file", "show me the file"})) {
            best_intent = Intent::Navigate;
            best_score = 0.5;
            explanation = "navigate (direct file read)";
        }
    }

    // Execute → Explain override: "how does the X work" or "tell me how X works"
    // should be Explain even if "build "/"run " appear (e.g., "how does the build system work")
    if (best_intent == Intent::Execute) {
        if (contains_any(lower, {"how does", "how is", "how do",
                                  "tell me how", "explain"})) {
            best_intent = Intent::Explain;
            best_score = 0.4;
            explanation = "explain (overrode execute -- how/explain signal)";
        }
    }

    // Execute → Locate override: "where do we" with "run" should be Locate,
    // not Execute (e.g., "where do we call gh run view")
    if (best_intent == Intent::Execute) {
        if (contains_any(lower, {"where do", "where does"})) {
            best_intent = Intent::Locate;
            best_score = 0.4;
            explanation = "locate (overrode execute -- where signal)";
        }
    }

    // Chat fallback for simple greetings with no other match
    if (best_intent == Intent::Unknown && best_score == 0.0) {
        if (contains_any(lower, {"hello", "hi ", "hey"})) {
            best_intent = Intent::Chat;
            best_score = 0.4;
            explanation = "chat/greeting (fallback)";
        }
    }

    return {best_intent, best_score};
}

GoalUnderstandingService::EntityResult
GoalUnderstandingService::detect_entity(const std::string &req,
                                         const std::string &lower,
                                         Intent intent,
                                         std::string &explanation) {
    (void)req;

    struct EntityRule {
        Entity entity;
        std::vector<std::string> phrases;
        double weight_per_match;
        std::string label;
    };

    static const EntityRule rules[] = {
        {Entity::GitHistory, {"last commit", "recent commit", "latest commit",
                              "previous commit",
                              "recent commits", "recent comits",
                              "commit history",
                              "check commit", "check comit",
                              "commit log", "commit",
                              "git log", "git history"}, 0.30, "git-history"},

        {Entity::GitWorkingTree, {"changed files", "modified files",
                                   "uncommitted", "unstaged",
                                   "working tree", "git status",
                                   "what files changed", "show changed files",
                                   "check changed files",
                                   "show modified files", "check modified files",
                                   "did i edit",
                                   "what changed",
                                   "git diff", "show diff",
                                   "check if any files changed",
                                   "what are the current changes",
                                   "what files are modified",
                                   "files are modified"}, 0.30, "git-working-tree"},

        {Entity::CIPipeline, {"ci", "workflow", "gh run",
                               "ci/cd", "actions"}, 0.25, "ci-pipeline"},

        {Entity::GitHubAction, {"github.com", "actions/runs",
                                 "github action", "workflow run"}, 0.30, "github-action"},

        {Entity::Session, {"provider", "model", "backend",
                            "session", "online", "provider am i",
                            "what provider", "what model"}, 0.30, "session"},

        {Entity::Architecture, {"architecture", "design", "designed",
                                 "architect"}, 0.30, "architecture"},

        {Entity::Codebase, {"codebase", "project", "repo", "repository",
                             "application"}, 0.20, "codebase"},

        {Entity::File, {"read file", "file ", " read "}, 0.20, "file"},

        {Entity::Build, {"build", "cmake", "compile", "make"}, 0.20, "build"},

        {Entity::Test, {"test", "ctest", "unit test", "test suite"}, 0.20, "test"},
    };

    double best_score = 0.0;
    Entity best_entity = Entity::Unknown;
    double second_score = 0.0;

    for (auto &rule : rules) {
        double score = score_matches(lower, rule.phrases,
                                      rule.weight_per_match, 0.95);
        if (score > best_score) {
            second_score = best_score;
            best_score = score;
            best_entity = rule.entity;
            explanation = rule.label;
        } else if (score > second_score) {
            second_score = score;
        }
    }

    // Intent-based entity defaulting
    if (best_entity == Entity::Unknown || best_score == 0.0) {
        switch (intent) {
            case Intent::Locate:
                best_entity = Entity::Symbol;
                best_score = 0.3;
                explanation = "symbol (from Locate)";
                break;
            case Intent::Explain:
                best_entity = Entity::Architecture;
                best_score = 0.3;
                explanation = "architecture (from Explain)";
                break;
            case Intent::Review:
                best_entity = Entity::Architecture;
                best_score = 0.3;
                explanation = "architecture (from Review)";
                break;
            case Intent::Status:
                best_entity = Entity::GitWorkingTree;
                best_score = 0.3;
                explanation = "git-working-tree (from Status)";
                break;
            default:
                break;
        }
    }

    // Entity override: GitWorkingTree beats GitHistory when diff/status is mentioned
    if (best_entity == Entity::GitHistory &&
        contains_any(lower, {"status", "uncommitted", "working tree", "diff"})) {
        best_entity = Entity::GitWorkingTree;
        best_score = std::max(best_score, 0.5);
        explanation = "git-working-tree (override: status/diff)";
    }

    // Entity override: Locate + Codebase → Symbol
    if (best_entity == Entity::Codebase && intent == Intent::Locate) {
        best_entity = Entity::Symbol;
        best_score = std::max(best_score, 0.3);
        explanation = "symbol (override: Locate intent)";
    }

    return {best_entity, best_score};
}

Artifact GoalUnderstandingService::infer_artifact(Intent intent, Entity entity,
                                                   const std::string &lower) {
    switch (intent) {
        case Intent::Explain:
            if (entity == Entity::Architecture || entity == Entity::Codebase)
                return Artifact::Overview;
            return Artifact::Explanation;

        case Intent::Locate:
            if (lower.find("used") != std::string::npos ||
                lower.find("usage") != std::string::npos ||
                lower.find("call") != std::string::npos)
                return Artifact::Usage;
            return Artifact::Definition;

        case Intent::Review:
            return Artifact::Report;

        case Intent::Status:
            return Artifact::Status;

        case Intent::Diagnose:
            return Artifact::RootCause;

        case Intent::Compare:
            return Artifact::Difference;

        case Intent::Navigate:
            return Artifact::Definition;

        case Intent::Modify:
            return Artifact::Patch;

        case Intent::Execute:
            return Artifact::ExecutionOutput;

        case Intent::Chat:
            return Artifact::Explanation;

        default:
            return Artifact::Unknown;
    }
}

Scope GoalUnderstandingService::infer_scope(Intent intent, Entity entity,
                                             Artifact artifact) {
    (void)artifact;

    if (intent == Intent::Status)
        return Scope::Recent;
    if (entity == Entity::GitHistory || entity == Entity::GitWorkingTree)
        return Scope::Recent;

    if (intent == Intent::Locate || intent == Intent::Navigate)
        return Scope::Local;
    if (entity == Entity::Symbol || entity == Entity::File ||
        entity == Entity::Component)
        return Scope::Local;

    if (intent == Intent::Explain || intent == Intent::Review ||
        intent == Intent::Compare)
        return Scope::Global;
    if (entity == Entity::Architecture || entity == Entity::Codebase)
        return Scope::Global;

    return Scope::All;
}

double GoalUnderstandingService::calculate_confidence(Intent intent,
                                                       double intent_score,
                                                       Entity entity,
                                                       double entity_score,
                                                       const std::vector<Ambiguity> &ambiguities) {
    double base = 0.0;

    if (intent == Intent::Unknown) {
        base = 0.1;
    } else if (intent == Intent::Chat) {
        base = 0.6;
    } else {
        base = 0.35;
    }

    // Boost from intent score
    base += intent_score * 0.4;

    // Boost from entity resolution
    if (entity != Entity::Unknown) {
        base += 0.10;
        base += entity_score * 0.15;
    }

    // Known-good combination bonus
    if (intent == Intent::Status &&
        (entity == Entity::GitWorkingTree || entity == Entity::GitHistory))
        base += 0.10;
    if (intent == Intent::Locate && entity == Entity::Symbol)
        base += 0.08;
    if (intent == Intent::Explain && entity == Entity::Architecture)
        base += 0.08;
    if (intent == Intent::Diagnose && entity == Entity::CIPipeline)
        base += 0.08;

    // Penalty per ambiguity
    base -= static_cast<double>(ambiguities.size()) * 0.08;

    return std::max(0.0, std::min(1.0, base));
}

} // namespace Services
