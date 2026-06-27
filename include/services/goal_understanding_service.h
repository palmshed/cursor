#pragma once
#include <string>
#include <vector>

namespace Services {

enum class Intent {
    Unknown = 0,
    Explain,
    Locate,
    Review,
    Status,
    Diagnose,
    Compare,
    Navigate,
    Modify,
    Execute,
    Chat
};

enum class Entity {
    Unknown = 0,
    Codebase,
    Component,
    Symbol,
    File,
    Architecture,
    GitWorkingTree,
    GitHistory,
    CIPipeline,
    GitHubAction,
    Session,
    Build,
    Test
};

enum class Artifact {
    Unknown = 0,
    Overview,
    Definition,
    Usage,
    Difference,
    Report,
    RootCause,
    Patch,
    Status,
    Explanation,
    ExecutionOutput
};

enum class Scope {
    Unknown = 0,
    Global,
    Local,
    Recent,
    All
};

struct Goal {
    Intent intent{Intent::Unknown};
    Entity entity{Entity::Unknown};
    Artifact artifact{Artifact::Unknown};
    Scope scope{Scope::Unknown};

    bool is_known() const {
        return intent != Intent::Unknown;
    }
};

struct Ambiguity {
    std::string field;
    std::string alternative;
    double confidence;
};

struct ParseResult {
    Goal goal;
    double confidence{0.0};
    std::vector<Ambiguity> ambiguities;
    std::string explanation;
};

class GoalUnderstandingService {
public:
    ParseResult parse(const std::string &user_request);

private:
    struct IntentResult { Intent intent; double score; };
    struct EntityResult { Entity entity; double score; };

    static IntentResult detect_intent(const std::string &req,
                                       const std::string &lower,
                                       std::string &explanation);
    static EntityResult detect_entity(const std::string &req,
                                       const std::string &lower,
                                       Intent intent,
                                       std::string &explanation);
    static Artifact infer_artifact(Intent intent, Entity entity,
                                    const std::string &lower);
    static Scope infer_scope(Intent intent, Entity entity, Artifact artifact);
    static double calculate_confidence(Intent intent, double intent_score,
                                        Entity entity, double entity_score,
                                        const std::vector<Ambiguity> &ambiguities);
};

} // namespace Services
