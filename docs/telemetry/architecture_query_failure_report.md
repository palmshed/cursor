---
---

# Architecture Query Failure Report

**Date:** 2026-06-26  
**Report:** `architecture_query_failure_report.md`  
**Trigger:** `"tell me how this agent designed"`  
**Phase:** Investigative -- no fix applied

---

## Problem Statement

The query `"tell me how this agent designed"` is intended as an architectural question: the user wants to know how the `Agent` class is designed, what owns it, how it coordinates runtime components, and how it fits into the system.

Instead, the engine:

1. Classifies the query as **CodebaseQuery** (Repository Investigation)
2. Extracts the search term `"agent"`
3. Finds one file: `include/agent.h`
4. Reads that single header
5. Declares the goal **complete**
6. Passes only `agent.h` contents to the AI for synthesis

The AI receives insufficient evidence: one interface header with class declarations but no implementation, no call graph, no ownership context, and no architecture documentation. The result is a shallow or hallucinated answer.

---

## Timeline

```text
00:00  User: "tell me how this agent designed"
00:01  classify_goal() → CodebaseQuery (Repository Investigation)
00:02  extract_best_term("tell me how this agent designed") → "agent"
00:03  is_implementation_query("tell me how this agent designed") → false
00:04  select_next_tool() → {"find", "agent"} (no --impl)
00:05  directory_aware_find("agent", impl=false) → [agent.h (score 20)]
00:06  read include/agent.h → 82 lines returned
00:07  completion_gate() → find_ok = true → DONE
00:08  should_call_ai() → true → AI synthesis with 1 header as evidence
00:09  AI returns surface-level explanation from agent.h alone
```

---

## Classification Analysis

### Entry: `classify_goal()` at `src/services/execution_engine.cpp:294`

The classifier checks CodebaseOverview eligibility first at line 321:

```cpp
// Architecture / conceptual questions suggesting CodebaseOverview
if (contains_any(goal, {"architecture", "design", "how it works", "how does it work"}) ||
    contains_any(goal, {"explain the", "how does the", "how does a", "tell me how the"}) ||
    (contains_any(goal, {"how", "explain", "tell me"}) && contains_any(goal, {"work", "works", "pipeline", "architecture", "design", "system", "flow"}))) {
  return CodebaseOverview;
}
```

#### First sub-check: `contains_any("tell me how this agent designed", {"design"})`

The `contains_any` function at line 271 uses `word_boundary()` at line 264 for match validation:

```cpp
static bool word_boundary(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\0' ||
         c == '.' || c == ',' || c == '!' || c == '?' ||
         c == '/' || c == ':' ||
         c == ')' || c == ']' || c == '}';
}
```

For `"design"` at position 23 of `"tell me how this agent designed"`:

| Check | Position | Character | Result |
|---|---|---|---|
| `boundary_before` | 22 | `' '` (space) | `true` |
| `boundary_after` | 29 | `'e'` (part of "designed") | `false` |

`'e'` is not a word boundary character -- the match fails because "design" is a suffix of "designed".

#### Second sub-check: `contains_any(goal, {"explain the", "how does the", "how does a", "tell me how the"})`

The goal contains `"tell me how this agent designed"` -- "tell me how **the**" is not found because "this" separates "how" from "the".

#### Third sub-check: compound gate

```cpp
(contains_any(goal, {"how", "explain", "tell me"}) &&
 contains_any(goal, {"work", "works", "pipeline", "architecture", "design", "system", "flow"}))
```

Left side: `"how"` matches in `"tell me how this agent designed"` (word boundary both sides) → `true`.

Right side: checks for `"work"`, `"works"`, `"pipeline"`, `"architecture"`, `"design"`, `"system"`, `"flow"`. The `"design"` check fails for the same reason (suffix mismatch). None of the other terms are present.

**Result: compound gate fails → CodebaseOverview classification missed.**

### Fallthrough: CodebaseQuery at line 381

```cpp
if (contains_any(goal, {"where", "what is", "what does", "what's",
                        "how does", "how is", "how are", "how", "works",
                        "find", "search", "grep", "locate",
                        "show me", "list", "tell me about", "tell me how",
                        "explain", "describe", "overview",
                        "architecture",
                        "in this project", "in this repo"}))
  return CodebaseQuery;
```

`"how"` matches at position 8 (word boundary both sides) → returns `CodebaseQuery`.

### What should have happened

`"tell me how this agent designed"` contains:
- `"tell me how"` -- explanatory intent trigger
- `"designed"` -- architecture/design intent (morphological variant of "design")
- No specific code symbol to locate

This is architecturally equivalent to `"how does this agent work"` or `"tell me about the architecture"`. It should be classified as `CodebaseOverview`.

**Root cause:** The word-boundary check treats "design" and "designed" as different words, even though a user who types "designed" is asking about design.

---

## Tool Sequence Trace

### Actual (CodebaseQuery path)

| Step | Tool | Args | Result |
|---|---|---|---|
| 1 | `find` | `agent` | `include/agent.h` (score 20, exact stem match) |
| 2 | `read` | `include/agent.h` | 82 lines -- class Agent { ... } (interface only) |
| 3 | *(completion)* | -- | find_ok=true → stop |

**Files read:** 1 (`include/agent.h`)  
**Total tools executed:** 2  
**Evidence for AI:** One header file with no implementation, no call sites, no architecture docs.

### Ideal (CodebaseOverview path)

If classified as CodebaseOverview, `select_next_tool()` at line 556 would produce:

| Step | Tool | Args | Result |
|---|---|---|---|
| 1 | `discovery` | *(empty)* | File tree scanned |
| 2 | `read` | `README.md CMakeLists.txt AGENTS.md` | Project structure + architecture docs |

**Files read:** 3+ project-level documents  
**Evidence for AI:** Architecture overview, build structure, key component relationships.

---

## Completion Gate Analysis

The completion gate at `src/services/execution_engine.cpp:858`:

```cpp
case CodebaseQuery: {
  auto need = detect_evidence_need(goal);
  if (need == EvidenceNeed::CommitHistory)
    return evidence.has_fact_containing("git:results");

  if (is_reference_query(goal)) {
    bool refs_ok = evidence.has_fact_containing("references:results") &&
                   evidence.has_fact_containing("read");
    bool refs_noresults = evidence.has_fact_containing("references:noresults");
    return refs_ok || refs_noresults;
  }

  bool find_ok = evidence.has_fact_containing("find:results") &&
                 evidence.has_fact_containing("read");
  bool grep_ok = evidence.has_fact_containing("grep:results") &&
                 evidence.has_fact_containing("read");
  bool find_tried_and_fell_through =
      evidence.has_fact_containing("find:noresults") &&
      evidence.has_fact_containing("grep:results") &&
      evidence.has_fact_containing("read");
  return find_ok || grep_ok || find_tried_and_fell_through;
}
```

**Problem:** The gate requires only one find+read cycle. For a lookup query (`"where is X"`), one file is sufficient. For an architecture question (`"how is X designed"`), one header is never enough.

The gate does not distinguish between lookup intent and explanatory intent. It treats all CodebaseQuery results equally.

**Contrast with CodebaseOverview gate** (line 841):

```cpp
case CodebaseOverview:
  return evidence.has_fact_containing("discovery") &&
         evidence.has_fact_containing("read:results");
```

The CodebaseOverview gate requires `discovery` + `read:results` -- evidence of multiple file reads. This produces richer evidence.

---

## Synthesis Analysis

The AI synthesis at `src/app/command_router.cpp:68-70`:

```cpp
bool CommandRouter::should_call_ai(const Services::ExecutionResult &result) {
  return result.outcome == Core::Outcome::Success;
}
```

Since `outcome == Success`, AI is called unconditionally. The AI receives `engine_result.summary` -- a tool history containing:

- `find agent → include/agent.h`
- read contents of `include/agent.h`

**Available evidence for AI:**

```cpp
class Agent {
public:
  Agent(const Config &cfg, UIManager &ui, SessionState &state);
  ~Agent();

  void run(const std::string &input);
  void stop();

  SessionState &state() { return state_; }
  const SessionState &state() const { return state_; }

  // ...
private:
  SessionState state_;
  UIManager *ui_;
  // ...
};
```

**What is missing:**
- `src/core/agent.cpp` -- implementation of `run()`, lifecycle, orchestration
- `AGENTS.md` -- architecture documentation
- `src/main.cpp` -- how Agent is instantiated and wired
- Call sites: what owns Agent, what depends on Agent
- Relationship to ExecutionEngine, CommandRouter, ReplayService

The AI cannot explain "how this agent is designed" from a single header with declarations only.

---

## Root Cause

**Primary:** Word-boundary check in `contains_any()` prevents "design" from matching in "designed". The query falls through the CodebaseOverview check and lands in CodebaseQuery.

**Secondary:** The CodebaseQuery completion gate terminates after one find+read cycle. Even if the word-boundary issue were fixed for a related query, the gate would still produce insufficient evidence for any architecture question that slips through to CodebaseQuery.

**Tertiary:** The query intent (explanatory: "tell me how ... designed") is structurally different from a lookup query ("where is ..."), but the classifier has no intermediate bucket for explanatory+code queries.

---

## Affected Metrics

| Metric | Value | Interpretation |
|---|---|---|
| `filename_hit_rate` | 1.0 | Find succeeded -- correctly found `agent.h` |
| `symbol_hit_rate` | 0.0 | No symbol scanning needed (exact filename match) |
| `reference_hit_rate` | N/A | Reference search not triggered |
| `grep_fallback_rate` | 0.0 | Grep not reached -- find succeeded |
| `average_files_read` | **1** | Too low for architecture questions |
| Semantic correctness | **0%** | Answer is not useful for "how is it designed" |
| `time_until_useful_result` | ~40ms | Fast but useless |
| User rejection risk | **High** | User sees shallow answer and aborts |
| `insufficient_evidence` impact | None | Engine reports Success, not InsufficientEvidence -- the failure is invisible to telemetry |

**Key insight:** The query reports as `Success` in telemetry because find+read both succeed. The failure is semantic, not mechanical. Standard telemetry metrics (tool counts, exit codes) do not capture this failure mode.

---

## Candidate Fixes

### Fix 1: Add "designed" to CodebaseOverview trigger set

**Description:** Add `"designed"` to the CodebaseOverview trigger list at line 321.

```cpp
if (contains_any(goal, {"architecture", "design", "designed",
                         "how it works", "how does it work"}) ||
```

**Risk:** **Low.** Adding a known morphological variant of an existing trigger word. `"designed"` cannot appear in a lookup context without architectural intent (e.g. `"where is X designed"` would be unusual). The `"designed"` match uses the same word-boundary logic -- `word_boundary(' ')` after "designed" is true for any typical sentence.

**Impact:** This query would route to CodebaseOverview and follow the discovery→read pattern, producing AGENTS.md + README + CMakeLists.txt evidence.

**Alternatively**, relax word-boundary for this specific trigger to allow suffix matches. For example, treat any alphabetic character followed by end-of-string as a boundary for the "design" root. But this is more invasive and may produce false positives.

### Fix 2: Add suffix-matching mode to `contains_any`

**Description:** Add a `contains_any_suffix` variant that treats the trigger word as a root and matches suffixes (e.g., "design" matches "designed", "designing", "designs").

```cpp
static bool contains_any_suffix(const std::string &text,
                                 const std::vector<std::string> &roots) {
  std::string lower = text;
  for (auto &c : lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (auto &r : roots) {
    std::string rl = r;
    for (auto &c : rl)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // Find the root as a word-starting or boundary-followed prefix
    size_t pos = 0;
    while ((pos = lower.find(rl, pos)) != std::string::npos) {
      bool boundary_before = (pos == 0) || word_boundary(lower[pos - 1]);
      // After: any non-letter is a boundary (suffix mode)
      bool boundary_after = (pos + rl.size() >= lower.size()) ||
                             !std::isalpha(static_cast<unsigned char>(lower[pos + rl.size()]));
      if (boundary_before && boundary_after)
        return true;
      pos++;
    }
  }
  return false;
}
```

**Risk:** **Medium.** Could match unintended words if a root is short (e.g., "how" would match "however"). Should be used selectively -- only for roots where suffix matches are semantically safe: "design", "implement", "architecture", "configure".

**Impact:** Generalizes the fix beyond this single query. Any morphological variant of "design" would match (designed, designing, redesign, etc.).

### Fix 3: Intermediate intent layer -- "explain X" vs "find X"

**Description:** Add an intermediate classification layer between CodebaseOverview and CodebaseQuery that detects explanatory intent toward a specific code entity (e.g., "how does X work", "how is X designed", "tell me how X works"). Route these to an "ExplainCodebaseEntity" path that:

1. Runs the find pipeline (same as CodebaseQuery) to locate the entity
2. Does NOT stop at one file read
3. Also reads related files: the implementation file, callers, AGENTS.md
4. Uses a stricter completion gate requiring 2+ reads + project documentation

**Risk:** **Medium-High.** New classification path means new code paths, new gate logic, and potential interaction with existing routing. Requires testing across the benchmark and regression suites.

**Impact:** Generalizes to all "how does X work" and "how is X designed" queries, not just the "design" word-boundary case.

### Fix 4: CodebaseQuery completion gate requires 2+ reads for explanatory queries

**Description:** Detect explanatory intent within the CodebaseQuery gate and require more evidence. For example, check if the goal contains "how" or "explain" or "tell me" and, if so, require 2+ distinct read operations or a read of both a `.h` and its corresponding `.cpp` file.

```cpp
bool is_explanatory_query(const std::string &goal) {
  std::string lower = goal;
  for (auto &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return lower.find("how") != std::string::npos ||
         lower.find("explain") != std::string::npos ||
         lower.find("tell me") != std::string::npos;
}
```

Then in the completion gate, add:

```cpp
if (is_explanatory_query(goal) && evidence.has_fact_containing("find:results")) {
  // Require 2+ reads or matching .cpp read for explanatory queries
  if (!evidence.has_fact_containing("read:results.multiple") &&
      !evidence.has_fact_containing("read:*.cpp"))
    return false;
}
```

**Risk:** **Medium.** Increases tool count for all CodebaseQuery queries that contain "how" -- including `"how do I add a file"` which currently works fine with one find+read. Would need careful guarding to avoid regressions on lookup queries that happen to contain "how".

**Impact:** Fixes the completion gate problem independently of the classification fix.

### Fix 5: Evidence ranking -- if top result is a header, read the corresponding .cpp

**Description:** After the find→read cycle reads a `.h` file, automatically enqueue a read of the corresponding `.cpp` file. This is safe because:
- If no `.cpp` exists with the same stem, the read fails gracefully
- The `is_implementation_query` flag already encodes preference for `.cpp` but only affects ranking, not the number of files read

**Risk:** **Low-Medium.** Adds one extra tool per header-based find cycle. May increase average_files_read for all queries, not just explanatory ones. Could introduce latency for simple lookup queries.

**Impact:** Every find→read cycle that reads a `.h` would also read the `.cpp` -- doubling the evidence without any classification change. This alone would fix the synthesis gap for this query (agent.h + agent.cpp would give significantly more structural evidence).

---

## Recommendation

**Apply Fix 1 (add "designed" to CodebaseOverview triggers) as the immediate, minimal fix.**

The word-boundary bug is unambiguous: "design" should match "designed" for architectural intent classification. The fix is one additional string in an existing trigger list. It addresses the root cause directly and carries near-zero risk.

**Evaluate Fix 5 (read .cpp when .h is found) for the general case.**

If this failure pattern appears in telemetry for queries that do NOT contain "design" but still produce shallow answers (e.g., `"how does the agent work"` classified as CodebaseQuery), the completion gate or multi-read logic (Fix 4 or Fix 5) would be the correct follow-up.

**Do not apply Fix 3 (new classification path) or Fix 4 (gate changes) until telemetry confirms a wider pattern.**

A single query failure does not justify a new classification path or gate complexity. The 5-question reality check ("Which production traces fail today? How will we measure success?") must precede any new capability or architectural change.

---

## Appendix: Full Tool Output Trace

```
[classify_goal] input="tell me how this agent designed"
[classify_goal] check CodebaseOverview:
  contains_any("design"):
    lower.find("design") → pos=23
    word_boundary(' ') @ pos-1 → true
    word_boundary('e') @ pos+6 → false ← NO MATCH
  contains_any("tell me how the"):
    "tell me how" found @ pos=0
    "the" NOT FOUND after "tell me how" → NO MATCH
  compound gate (how + design):
    "how" → pos=8, boundary_before=' ', boundary_after=' ' → MATCH
    "design" → pos=23, boundary_after='e' → NO MATCH
  → CodebaseOverview: NO
[classify_goal] check CodebaseQuery:
  contains_any("how"):
    pos=8, boundary_before=' ', boundary_after=' ' → MATCH
  → CodebaseQuery: YES

[extract_best_term] input="tell me how this agent designed"
[extract_best_term] remove prefix "tell me how " → "this agent designed"
[extract_best_term] remove suffix check: none match
[extract_best_term] tokenize: ["this", "agent", "designed"]
[extract_best_term] stop words: "this" is stop word → split (empty before, start group after)
[extract_best_term] phrase groups: [["agent", "designed"]]  (single multi-word group)
[extract_best_term] scores: group[0] = 0 (no code shape) + 0*2 (pos bonus) = 0
[extract_best_term] best group (only): ["agent", "designed"]
[extract_best_term] no word in group has strong code-shape (ws >= 10)
[extract_best_term] group size >= 2 → return first noun: "agent"
[extract_best_term] → "agent"

[select_next_tool] type=CodebaseQuery
[select_next_tool] is_git_diff_query: false
[select_next_tool] is_reference_query: false
[select_next_tool] impl_query: false (no "implemented", "defined", "where is")
[select_next_tool] → {"find", "agent"} (without --impl)

[directory_aware_find] term="agent", impl=false
[directory_aware_find] scoring:
  include/agent.h: exact stem match "agent" → score 20
  (other matches: none above threshold)
[directory_aware_find] selected: include/agent.h (score 20, exact match)

[read] file=include/agent.h
[read] → 82 lines (class Agent declaration)

[completion_gate] type=CodebaseQuery
[completion_gate] has_fact("find:results") → true
[completion_gate] has_fact("read") → true
[completion_gate] find_ok = true → DONE

[should_call_ai] outcome=Success → true

[AI synthesis input]
  Tool trace:
    find agent → include/agent.h
    read include/agent.h (82 lines)
  Evidence: class Agent { ... } interface only
  No: agent.cpp, AGENTS.md, main.cpp, call sites

[AI output] Surface-level explanation of Agent class interface.
[User] Aborts or follows up with more specific question.
```

---

*This report identifies a single-query semantic failure. No fix has been applied. The recommendation is a one-line change to add "designed" to the CodebaseOverview trigger set, followed by telemetry monitoring to determine if the completion gate also needs adjustment.*
