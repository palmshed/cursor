---
---

# Goal Model for Level 2.4

## Purpose

Add a structured `Goal` object alongside the existing `GoalType` enum that captures what the user wants to know -- before the planner decides how to investigate.

The Goal describes **knowledge**, not **execution**.

There is no tool information, confidence, or planner state in the Goal.

---

## The Struct

```cpp
struct Goal {
    Intent intent;     // what does the user want?
    Entity entity;     // what object are they talking about?
    Artifact artifact; // what kind of answer are they expecting?
    Scope scope;       // breadth of the question
};
```

That is the complete struct.

Everything else (confidence, evidence requirements, completion criteria, tool decisions) belongs to the planner or the investigation strategy phase that derives from the Goal.

---

## Enums

These are designed to be stable. Once finalized they should almost never change.

### Intent -- the primary action

```cpp
enum class Intent {
    Unknown,
    Explain,     // "how does this work", "explain the architecture"
    Locate,      // "find X", "where is Y defined"
    Review,      // "review architecture", "audit technical debt"
    Status,      // "what changed", "last commit", "current branch"
    Diagnose,    // "why did CI fail", "find why X is triggered"
    Compare,     // "what is the difference between X and Y"
    Navigate,    // "read file X", "show memory"
    Modify,      // "add X", "refactor Y", "fix Z"
    Execute,     // "run make test", "build the project"
    Chat         // "how are you", "hello"
};
```

### Entity -- the subject the user is asking about

```cpp
enum class Entity {
    Unknown,
    Codebase,          // entire project
    Component,         // specific subsystem
    Symbol,            // function, class, variable
    File,              // specific file
    Architecture,      // design / architecture
    GitWorkingTree,    // modified / unstaged files
    GitHistory,        // commits
    CIPipeline,        // CI/CD workflow
    GitHubAction,      // specific GitHub Action run
    Session,           // runtime configuration
    Build,             // build system
    Test               // test suite
};
```

### Artifact -- the kind of answer the user expects

```cpp
enum class Artifact {
    Unknown,
    Overview,          // high-level summary
    Definition,        // where X is declared/defined
    Usage,             // where X is used/called
    Difference,        // comparison between things
    Report,            // structured analysis (review)
    RootCause,         // why something failed
    Patch,             // code change to fix something
    Status,            // current state
    Explanation,       // conceptual understanding
    ExecutionOutput    // command output
};
```

### Scope -- breadth of the question

```cpp
enum class Scope {
    Unknown,
    Global,            // entire codebase
    Local,             // specific area
    Recent,            // recent changes (git)
    All                // unrestricted
};
```

---

## GoalUnderstandingService

A new component sits between user input and the planner:

```
User request
     │
     ▼
GoalUnderstandingService
     │
     ├── parses request → builds Goal
     ├── assigns parse confidence (how sure is it that the Goal is correct?)
     ├── flags ambiguities (could also mean X, Y, or Z)
     │
     ▼
Goal { intent, entity, artifact, scope }
     │
     ▼
Planner (consumes Goal, never raw text again)
```

The `GoalUnderstandingService` is deterministic where possible, not an LLM classifier.

Example:

```
Input: "show changed files"

GoalUnderstandingService produces:

Goal {
    intent:   Intent::Status
    entity:   Entity::GitWorkingTree
    artifact: Artifact::Status
    scope:    Scope::Recent
}

Parse confidence: 0.88
Ambiguities: [
    "GitWorkingTree vs GitHistory" -- "changed files" usually means working tree,
     but could mean across commits
]
```

---

## Examples

### "show changed files"

```cpp
Goal {
    intent:   Intent::Status,
    entity:   Entity::GitWorkingTree,
    artifact: Artifact::Status,
    scope:    Scope::Recent
};
```

### "explain the architecture"

```cpp
Goal {
    intent:   Intent::Explain,
    entity:   Entity::Architecture,
    artifact: Artifact::Overview,
    scope:    Scope::Global
};
```

### "find CommandRouter"

```cpp
Goal {
    intent:   Intent::Locate,
    entity:   Entity::Symbol,
    artifact: Artifact::Definition,
    scope:    Scope::Global
};
```

### "what is the difference between A and B"

```cpp
Goal {
    intent:   Intent::Compare,
    entity:   Entity::Component,
    artifact: Artifact::Difference,
    scope:    Scope::Local
};
```

### "review architecture"

```cpp
Goal {
    intent:   Intent::Review,
    entity:   Entity::Architecture,
    artifact: Artifact::Report,
    scope:    Scope::Global
};
```

---

## Compare with Current System

| Aspect | Current (GoalType) | Proposed (Goal) |
|---|---|---|
| Representation | Flat enum | 4-field struct (intent, entity, artifact, scope) |
| Intent | Implicit in keyword match | Explicit Intent enum |
| Target | Implicit in keyword match | Explicit Entity + Artifact |
| Planner contamination | GoalType drives tool selection directly | Goal is purely about user intent; tools are a separate concern |
| New phrasing | Requires new `contains_any()` entry | No change -- same Goal inferred from different words |
| Ambiguity | Undetected (maps to wrong GoalType) | Captured as parse confidence + ambiguity list |

---

## What This Unlocks

1. **Evidence derivation becomes a function of Goal** -- `evidence_for(Goal)` replaces `required_evidence(GoalType)`. Adding a new combination requires a row, not a keyword list expansion.

2. **Completion becomes generic** -- the planner checks "is the required evidence for this Goal satisfied?" instead of hardcoded per-type checks.

3. **Recovery becomes goal-aware** -- instead of broader grep, the planner can ask "did I misidentify the entity? Should I broaden the scope?"

4. **Formatter selection** -- different artifacts (Overview vs Report vs RootCause) can use different prompts without changing the planner.

5. **Adding new phrasings** -- requires zero code changes. The GoalUnderstandingService infers the same Intent + Entity from different words.
