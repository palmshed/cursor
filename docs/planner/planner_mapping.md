---
---

# Planner Mapping: Goal → Strategy → Tools

## Current Flow (broken)

```
User text
     │
     ▼
contains_any() keyword matching
     │
     ▼
GoalType (flat enum)
     │
     ▼
required_evidence() + select_next_tool() (hardcoded per GoalType)
     │
     ▼
Tool execution
```

**Problem:** The jump from text → GoalType discards all semantic information. Entity, artifact, and scope are lost. The planner can't reason about what it doesn't know -- it can only match phrases. Every new phrasing requires a keyword list update.

---

## Proposed Flow

```
User request
     │
     ▼
GoalUnderstandingService      ← NEW: parses intent, builds Goal
     │
     ├── Intent
     ├── Entity
     ├── Artifact
     ├── Scope
     ├── Parse confidence      ← planner's uncertainty, not Goal's
     └── Ambiguities
     │
     ▼
Goal { intent, entity, artifact, scope }
     │
     ▼
Investigation Strategy         ← derives evidence needs from Goal
     │
     ├── evidence_needed = evidence_for(Goal)
     ├── completion = criteria_for(Goal)
     │
     ▼
Tool Plan                      ← selects tools to produce required evidence
     │
     ├── Which tools for this evidence class?
     ├── What order? (deterministic first, grep last)
     ├── What if tool fails?
     │
     ▼
Tool execution → evidence → completion check → synthesis
```

The planner never parses raw text directly again.

---

## GoalUnderstandingService

A deterministic parser (not an LLM) that translates user requests into Goals.

### Interface

```cpp
struct ParseResult {
    Goal goal;
    double confidence;           // how sure is the parser about this interpretation
    std::vector<Ambiguity> ambiguities;  // alternative interpretations considered
    std::string explanation;     // why this Goal was produced
};

class GoalUnderstandingService {
public:
    ParseResult parse(const std::string &user_request);
};
```

### Example

```
Input: "what files changed"

ParseResult:
    Goal:
        intent:   Intent::Status
        entity:   Entity::GitWorkingTree
        artifact: Artifact::Status
        scope:    Scope::Recent
    confidence: 0.88
    ambiguities: [
        "entity could be GitHistory (files changed across commits) -- lower confidence"
    ]
    explanation: "contains 'changed files' → Status intent, GitWorkingTree entity"
```

### What the parser looks for (deterministic)

The parser uses patterns, but it maps them to **concepts**, not GoalTypes:

```cpp
// Patterns → Intent
"how does", "explain", "tell me how"  → Intent::Explain
"find", "where is", "locate"          → Intent::Locate
"review", "audit"                     → Intent::Review
"last commit", "what changed"         → Intent::Status
"why did", "what failed"              → Intent::Diagnose
"difference between"                  → Intent::Compare
"read file", "show me"                → Intent::Navigate
"add", "refactor", "fix"             → Intent::Modify
"run", "build", "compile"            → Intent::Execute
"who are you", "hello"               → Intent::Chat
```

```
// Patterns → Entity
"architecture", "design"                → Entity::Architecture
"commit", "history", "changed files"     → Entity::GitHistory
"status", "modified", "unstaged"         → Entity::GitWorkingTree
"class", "function", "method", "symbol"  → Entity::Symbol (inferred from context)
"ci", "workflow", "github action"        → Entity::CIPipeline
"provider", "model", "backend"           → Entity::Session
```

The difference from the current system: these patterns produce **structured concepts** (intent + entity + artifact + scope), not a single enum value. The same input produces the same structured output regardless of which specific words are used.

---

## How Investigation Strategy Becomes Generic

Instead of:

```cpp
// Current: hardcoded per GoalType
switch (type) {
    case CommitHistory:
        return {EvidenceClass::GitLog};
    case CodebaseQuery:
        return {EvidenceClass::FileSearch, EvidenceClass::FileContent};
    ...
}
```

The strategy is derived from Goal:

```cpp
// Proposed: derived from Goal fields
std::vector<EvidenceClass> evidence_for(const Goal &goal) {
    if (goal.entity == Entity::GitWorkingTree)
        return {EvidenceClass::GitStatus};
    if (goal.entity == Entity::GitHistory)
        return {EvidenceClass::GitLog};
    if (goal.intent == Intent::Locate)
        return {EvidenceClass::FileSearch, EvidenceClass::FileContent};
    if (goal.intent == Intent::Explain && goal.entity == Entity::Architecture)
        return {EvidenceClass::Discovery, EvidenceClass::FileContent};
    if (goal.intent == Intent::Diagnose && goal.entity == Entity::CIPipeline)
        return {EvidenceClass::CIWorkflow};
    ...
}
```

Adding support for a new combination requires adding a row -- not expanding keyword lists.

---

## How Tool Selection Becomes Evidence-Driven

Instead of:

```cpp
// Current: keyword-based routing
if (contains_any(goal, {"last commit", "git log", "recent changes"}))
    return ToolCall{"git", "log --oneline -10"};
```

The planner asks:

```cpp
// Proposed: evidence-driven routing
ToolCall select_tool_for(const EvidenceClass &need, const Goal &goal) {
    switch (need) {
        case EvidenceClass::GitStatus:    return {"git", "status"};
        case EvidenceClass::GitLog:       return {"git", "log --oneline -10"};
        case EvidenceClass::FileSearch:   return {"find", entity_name(goal)};
        case EvidenceClass::FileContent:  return {"read", target_file(goal)};
        case EvidenceClass::Discovery:    return {"discovery", "."};
    }
}
```

The `entity_name(goal)` function extracts the entity from the Goal, not from raw text.

---

## How Completion Becomes Generic

Instead of:

```cpp
// Current: hardcoded per GoalType
bool check_completion(const std::string &goal, GoalType type, EvidenceStore &ev) {
    if (type == CommitHistory) return ev.has_fact("git:results");
    if (type == CodebaseQuery) return ev.has_fact("find:results") && ev.has_fact("read:results");
    ...
}
```

The planner checks:

```cpp
// Proposed: derived from Goal
bool check_completion(const Goal &goal, EvidenceStore &ev) {
    auto needed = evidence_for(goal);
    for (auto &ec : needed) {
        if (!ev.class_is_satisfied(ec))
            return false;
    }
    return true;
}
```

---

## How Recovery Becomes Goal-Aware

Instead of:

```cpp
// Current: broad strategies that don't consider Goal
ToolCall select_recovery_tool(...) {
    if (confidence < 0.3) return {"grep", extract_term(goal)};  // grep broader
    if (confidence < 0.5) return {"find", "--impl " + term};    // find implementation
    ...
}
```

The planner considers the Goal:

```cpp
// Proposed: goal-aware recovery
ToolCall select_recovery_tool(const Goal &goal, EvidenceStore &ev, double parse_confidence) {
    if (parse_confidence < 0.3) {
        // Low confidence in goal understanding itself
        // → ask clarifying question instead of running more tools
        return ToolCall{"clarify", "Do you mean working tree changes or commit history?"};
    }
    if (goal.entity == Entity::Symbol && !ev.has(EvidenceClass::FileSearch)) {
        return {"grep", entity_name(goal)};  // broader search
    }
    if (goal.entity == Entity::Architecture && !ev.has(EvidenceClass::Discovery)) {
        return {"discovery", "."};
    }
    ...
}
```

---

## Migration Path

### Step 1: Add GoalUnderstandingService (non-breaking)

New header, new class. Works independently from existing `classify_goal()`. Used for telemetry comparison only at first.

### Step 2: Run Goal and GoalType side by side

```
User request
     │
     ├──→ GoalUnderstandingService → Goal (for telemetry, not routing)
     │
     └──→ classify_goal() → GoalType (still used for routing)
```

Compare in telemetry:
- How often does Goal match GoalType?
- When they disagree, which was correct?
- Which produces better evidence collection?

### Step 3: Derive evidence from Goal when confident

When `parse_confidence > threshold` (e.g., 0.8), use `evidence_for(goal)` instead of `required_evidence(type)`. When parse confidence is low, fall back to the existing `required_evidence()`.

### Step 4: Derive completion from Goal

Same approach -- use Goal-based completion when confident, fall back to existing checks otherwise.

### Step 5: Phase out keyword routing

Once telemetry shows Goal understanding consistently produces the same or better results than keyword classification, switch the planner to consume Goal directly and remove `classify_goal()`.

---

## Success Verification

The following prompts should produce the same Goal without any code changes:

```
# All → Intent::Status + Entity::GitWorkingTree
check the files changed
which files changed
what changed
show modified files
what did I modify
show me what I edited today
did I edit anything
what files are modified
show me uncommitted changes
what's unstaged
```

```
# All → Intent::Explain + Entity::Architecture (or Entity::Component)
how is this agent designed
explain the architecture
walk me through the design
how does this work
explain how authentication works
how does the planner work
tell me about the command router
```

```
# All → Intent::Locate + Entity::Symbol
find CommandRouter
where is DiscoveryService defined
grep Agent
search for confidence scoring logic
locate the TokenManager
find where ConfigService is used
```
