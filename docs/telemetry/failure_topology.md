---
---

# Failure Topology

Telemetry analysis and engineering observations.

**Original:** 2026-06-26  
**Revised:** 2026-06-28  

**Source:** `~/.cursor/replay`  
**Logs:** 432  
**Traces:** 1,306

> This document accurately describes the system through the end of the retrieval maturity phase. Subsequent engineering cycles (Level 2.3 Answer Finalization and Level 2.4 Goal Understanding) refined the architectural understanding and supersede several design conclusions. Telemetry data and failure frequencies remain valid observations. Interpretations of root cause have evolved as the planner matured.
>
> **Status:** Once Level 2 closes, this report becomes a historical record. Future planner work should produce new versions (Planner Evolution Report v2, v3) instead of rewriting this document.

---

# Current Architecture Snapshot

```
Product:
AI Coding Agent

Current Maturity:
Level 2.4

Planner Focus:
Goal Understanding

Primary Runtime Artifact:
InvestigationSession

Primary Evidence Artifact:
EvidencePackage

Next Milestone:
GoalUnderstandingService

Generations:
   Gen 1 -- Keyword Routing (Phase 1-2)
   Gen 2 -- Evidence-driven Investigation (Phase 3-5)
   Gen 3 -- Goal-driven Planning (Phase 6, current)
```

## Status

```
Phase:       Level 2.4 (Goal Understanding)
State:       Feature complete through Level 2.3
             Goal Understanding under active development
Next exit:   Complete GoalUnderstandingService
             Close Level 2
```

---

# Planner Evolution

Three generations span six phases. Each generation made the planner qualitatively smarter.

```
Generation 1: Keyword Routing

    Phase 1: Keyword Routing
        classify_goal() matches phrases, selects GoalType
        → Brittle: every new phrasing needs keyword expansion

    Phase 2: Deterministic Retrieval
        Directory-aware find, shared ranking engine, reference search
        → Fixed "cursor binary" and other retrieval failures
        → But: planner was sending queries down the wrong path


Generation 2: Evidence-driven Investigation

    Phase 3: Planner Recovery
        Mid-loop recovery, confidence gates, post-completion recovery
        → Fixed premature termination
        → But: recovery was breaking the loop (continue vs break bug)
        → Fixed: recovery now continues instead of breaking

    Phase 4: Evidence Packaging
        EvidencePackage, InvestigationSession as canonical artifact
        → Separated planner state from evidence for AI consumption

    Phase 5: Answer Finalization [Level 2.3]
        evidence_summary replaces summary for AI synthesis
        Formatter strips planner metadata before AI sees it
        → Tool calls, confidence values, planner state no longer leak into answers


Generation 3: Goal-driven Planning [current]

    Phase 6: Goal Understanding [Level 2.4]
        GoalModel replaces GoalType as the planner's representation of intent
        GoalUnderstandingService parses user requests into structured Goals
        → Planner infers intent, not matches phrases


Future: Adaptive Planning
    Planner reasons about ambiguity, asks clarifying questions
    Recovery strategies are goal-aware
    Formatter selection is goal-aware
```

The system has matured through six phases. Each phase changed the understanding of what the planner needs.

```
Phase 1: Keyword Routing
    classify_goal() matches phrases, selects GoalType
    → Brittle: every new phrasing needs keyword expansion

Phase 2: Deterministic Retrieval
    Directory-aware find, shared ranking engine, reference search
    → Fixed "cursor binary" and other retrieval failures
    → But: planner was sending queries down the wrong path

Phase 3: Planner Recovery
    Mid-loop recovery, confidence gates, post-completion recovery
    → Fixed premature termination
    → But: recovery was breaking the loop (continue vs break bug)
    → Fixed: recovery now continues instead of breaking

Phase 4: Evidence Packaging
    EvidencePackage, InvestigationSession as canonical artifact
    → Separated planner state from evidence for AI consumption

Phase 5: Answer Finalization [Level 2.3]
    evidence_summary replaces summary for AI synthesis
    Formatter strips planner metadata before AI sees it
    → Tool calls, confidence values, planner state no longer leak into answers

Phase 6: Goal Understanding [Level 2.4 -- current]
    GoalModel replaces GoalType as the planner's representation of intent
    GoalUnderstandingService parses user requests into structured Goals
    → Planner infers intent, not matches phrases

Future: Adaptive Planning
    Planner reasons about ambiguity, asks clarifying questions
    Recovery strategies are goal-aware
    Formatter selection is goal-aware
```

This timeline explains why the recommendations in §6 have changed since the original analysis. Each phase revealed a deeper bottleneck.

---

## 1. Telemetry Distribution

Analysis of 1,306 total telemetry events shows a stark divergence when segmenting synthetic benchmark traces from production-only usage.

```
Overall Outcome Summary (1,306 traces):
  Success:                578 (44.3%)
  Insufficient Evidence:  324 (24.8%)
  Failure:                225 (17.2%)
  User Rejected:          177 (13.6%)
  None / Missing:           2 (0.1%)
```

### 1.1 Production-Only Subset (374 traces)
These traces reflect real developer interactions with the agent, free of benchmark runner interference.

* **Total Production Traces:** 374
* **Outcomes:**
  * **User Rejected:** 157 (42.0%) -- Represents instances where developers aborted the agent's plan or rejected code diffs early.
  * **Success:** 131 (35.0%) -- Completed tasks accepted by the developer.
  * **Insufficient Evidence:** 81 (21.7%) -- Agent terminated search loops because it could not find relevant symbols or files.
  * **Failure:** 5 (1.3%) -- Agent terminated with an explicit execution failure.

### 1.2 Synthetic/Benchmark Subset (932 traces)
These traces are generated by automated evaluation suites and mock workflow scenarios.

* **Total Synthetic Traces:** 932
* **Outcomes:**
  * **Success:** 447 (48.0%)
  * **Insufficient Evidence:** 243 (26.1%)
  * **Failure:** 220 (23.6%)
  * **User Rejected:** 20 (2.1%)
  * **None / Missing:** 2 (0.2%)

---

## 2. Failure Class Taxonomy

Observed failures from trace histories map to six primary failure classes. The taxonomy has been revised since the original analysis to add **Goal Understanding** as a distinct class, reflecting the discovery that many routing and retrieval failures share a common root cause.

```
Observed Failures
    │
    ├── Goal Understanding (NEW)
    │   └── Planner misinterprets the user's intent
    │       → Different phrasings of the same query produce different investigation paths
    │       → "what files changed" vs "show modified files" vs "did I edit anything"
    │       → Fixing requires adding keyword entries, not fixing understanding
    │
    ├── Routing
    │   └── Misclassification of user intent or goal types
    │       → Typo queries default to CodebaseQuery instead of CommitHistory
    │       → Scoped git queries match generic codebase query rules
    │
    ├── Retrieval
    │   └── Vital matches or files missed due to extraction limits or keyword drop
    │       → Multi-word search terms lost during tool selection
    │       → Now addressed by directory-aware find and reference search
    │
    ├── Ranking
    │   └── Agent reads irrelevant files or gets overwhelmed by too many results
    │       → Header vs implementation split confusion
    │       → Now addressed by shared ranking engine
    │
    ├── Gate
    │   └── Stopping too early or running up to maximum iteration limit
    │       → Recovery was breaking the loop instead of continuing
    │       → Now fixed: recovery-continue, evidence_summary gates
    │
    └── Synthesis
        └── Generic answers or hallucinated statements instead of evidence-backed claims
            → AI received raw session state including confidence values and tool output
            → Now addressed by Formatter: AI receives clean evidence summaries only
```

### 2.1 Goal Understanding Failures (Root Cause)
*The planner has no explicit representation of the user's intent. It matches phrases instead of inferring meaning.*
* **Evidence:**
  * "check the files changed" vs "what changed" vs "show modified files" -- same user intent, but only works if the exact phrase is in the keyword list.
  * "explain the architecture" vs "how is this agent designed" vs "walk me through the design" -- same user intent, but the classifier may route to CodebaseOverview, CodebaseQuery, or even GeneralChat depending on which words appear.
  * Adding a new phrasing always requires editing `classify_goal()`. The planner never generalizes.
* **Level 2.4 Response:** Replace `GoalType` keyword matching with a `GoalUnderstandingService` that produces a structured Goal (Intent, Entity, Artifact, Scope). The planner never parses raw text directly again.

### 2.2 Routing Failures
*Misclassification of user intent or goal types due to typo brittleness and pattern gaps.*

*Addressed by Phase 1 routing fixes (typo normalization, meta-query grounding).*

### 2.3 Retrieval Failures
*Vital matches or files missed due to extraction limits or keyword drop.*

*Addressed by Phase 2 directory-aware find and reference search.*

### 2.4 Ranking Failures
*The agent reads irrelevant files or gets overwhelmed by too many search results.*

*Addressed by Phase 2 shared ranking engine.*

### 2.5 Gate Failures
*Stopping too early (false positive) or running up to the maximum iteration limit (false negative).*

*Addressed by Phase 3 and Phase 5 (recovery-continue fix, evidence_summary gates).*

### 2.6 Synthesis Failures
*Providing generic answers or hallucinated statements instead of evidence-backed claims.*

*Addressed by Phase 5 (Formatter strips planner metadata before AI synthesis).*

---

## 3. Cost of Failure & Priority Scoring

To determine the optimal roadmap priority, we evaluate each failure class using a multi-dimensional priority model:

$$\text{Priority Product} = \text{Frequency} \times \text{Severity} \times \text{Difficulty of Recovery}$$

### 3.1 Scoring Definitions
* **Frequency (%):** Percentage contribution to observed failures.
* **Severity (1-5):** Impact of the failure class on the agent's goal.
  * *1: Negligible (minor detour, easily bypassed)* $\rightarrow$ *5: Critical (terminal failure, wrong files edited)*
* **Difficulty of Recovery (1-5):** How hard it is for the agent to recover on its own.
  * *1: Highly/Easily Recoverable (feedback loops bypass it)* $\rightarrow$ *5: Non-recoverable (terminal, loop breaks)*

### 3.2 Failure Class Priority Matrix

| Failure Class | Frequency (%) | Severity Score (1-5) | Difficulty of Recovery (1-5) | Priority Product |
|---|---|---|---|---|
| **Goal Understanding** | 34% (was Routing) | 4 (High) | 4 (Hard) | **5.44** |
| **Retrieval** | 42% | 5 (Critical) | 5 (Non-recoverable) | **10.50** |
| **Routing (pure)** | 8% | 2 (Low) | 2 (Easy) | **0.32** |
| **Ranking** | 14% | 3 (Moderate) | 3 (Moderate) | **1.26** |
| **Gate** | 7% | 4 (High) | 2 (Easy) | **0.56** |
| **Synthesis** | 3% | 2 (Low) | 2 (Easy) | **0.12** |

**Note on revised scoring:** The original analysis grouped Goal Understanding failures under "Routing" (34% frequency). With the new taxonomy splitting pure routing (typos, meta-commands) from goal understanding (intent misinterpretation), the priority shifts. Goal Understanding has higher severity than pure routing because the planner cannot recover from a wrong understanding of user intent -- it will run the wrong investigation path. Retrieval remains highest priority by product score, but Goal Understanding is the **upstream** failure -- fixing retrieval after sending the planner down the wrong path is treating symptoms.

### 3.3 Root Cause Depth

The original priority model did not account for *root cause depth*. A failure class that causes other failure classes downstream should be weighted higher.

```
Goal Understanding Failure
    ↓
Leads to wrong investigation strategy ↓
    ↓
Leads to wrong tool selection
    ↓
Leads to Retrieval Failure (wrong files searched)
    ↓
Leads to Gate Failure (exhausted iterations)
```

Solving Goal Understanding prevents cascading failures in retrieval, ranking, and gate classes. This is the primary architectural motivation for Level 2.4.

---

## 4. Top Recurring Failed/Insufficient Queries

### 4.1 Production-Only Subset (Top Failed Inputs)
These queries represent the primary friction points for real users:

* **35x:** `/` (Command prefix typo or empty slash command)
* **22x:** `/llm` (Unknown slash command/context switcher)
* **6x:** `where is replay implemented` (Conceptual retrieval query)
* **3x:** `tell me about this codebase` (Conceptual repository query)
* **2x:** `where is ZZZZ_CURSOR_TEST_NONEXISTENT` (Verification testing query)
* **2x:** `/inspect` (Slash command routing issue)
* **2x:** `/debug` (Slash command routing issue)
* **2x:** `/help` (Slash command routing issue)
* **1x:** `what is the last comit` (Typo routing failure)
* **1x:** `tell me about the last commit` (Git tool routing issue)
* **1x:** `show me the current git status` (Git tool routing issue)
* **1x:** `what files changed in the last commit` (Git tool routing issue)
* **1x:** `yeah tell me about the ui in from this codbease` (Conceptual codebase query)
* **1x:** `tell me about the snipper realated code` (Typo retrieval query)

### 4.2 Synthetic/Benchmark Subset (Top Failed Inputs)
These queries show the failure patterns in synthetic testing rigs:

* **55x each:**
  * `benchmark:investigate_build_failure`
  * `benchmark:find_auth_code`
  * `benchmark:recover_broken_cmakelists`
  * `benchmark:recover_broken_github_action`
  * `benchmark:search_miss_authentication`
  * `benchmark:recover_failing_test`
  * `benchmark:missing_dependency`
  * `benchmark:misnamed_config`
* **1x each:**
  * `search for benchmark service`
  * `where is DiscoveryService defined`
  * `find the UIManager declaration`
  * `where is the dashboard`
  * `search for planning service fix`
  * `find the benchmark results`
  * `where is the verification service`

---

## 5. Synthetic vs. Production Topology Comparison

Divergences between the synthetic and production traces highlight why optimizing for benchmark metrics can lead to poor real-world usability:

1. **The User Rejection Gap:** Production logs show a **42.0% User Rejected** rate, while synthetic traces show only **2.1%**. In production, users abort execution early when they see the agent heading down an incorrect path due to misrouted intent (Goal Understanding) or missed files (Retrieval). Synthetic benchmarks run blindly to completion or explicit failure.
2. **Explicit Failures vs. Insufficient Evidence:** Synthetic runs result in explicit failures **23.6%** of the time, while production has only **1.3%** explicit failures. In production, real-world tasks that hit obstacles are terminated under `InsufficientEvidence` (**21.7%**) or rejected by the user (**42.0%**) before they can fail explicitly.
3. **Intent Skew:** Synthetic logs are heavily biased toward long-running troubleshooting scenarios (`benchmark:investigate_build_failure`), whereas production logs are dominated by brief conceptual queries (`where is replay implemented`) and slash commands.

---

## 6. Feature Prioritization & Roadmap

Based on the segmented telemetry showing the **User Rejection Gap**, the implementation freeze has been partially lifted under strict boundaries. The roadmap has been updated to reflect architectural evolution through Phase 5 and into Phase 6.

### 6.1 Priority 1: Routing Improvements (Completed)
* **Status:** Built and Verified.
* **Target Failure Class:** Routing & Telemetry Distortion.
* **Solutions Integrated:**
  * **Typo Normalization Layer:** Intercepts inputs prior to classification to correct common typos (e.g. `comit` → `commit`, `snipper` → `snippet`, `codbease` → `codebase`).
  * **Command-Prefix Handling:** Gracefully handles slash commands (e.g., `/`, `/llm`) and maps them cleanly.
  * **Telemetry Isolation:** Automatically cleanses/resets the telemetry outcome metrics on every new user query, preventing previous session outcomes (like `UserRejected`) from carrying over to subsequent meta-commands.
  * **Session Meta-Query grounding:** Injected active model configuration (ID, name, and provider) into the agent's prompt context, allowing the AI to correctly answer session meta-queries (e.g. `"what provider am I using"`).

### 6.2 Priority 2: Directory-Aware Find / Scan (Completed)
* **Status:** Complete.
* **Target Failure Class:** Retrieval & Ranking.
* **Solutions Integrated:**
  * Shared ranking engine (`Services::directory_aware_find()`) replacing 4 duplicated implementations.
  * Word-level matching, CamelCase normalization, symbol scanning, implementation-file boost.
  * See `docs/telemetry/directory_aware_find_report.md`.

### 6.3 Priority 3: Reference Search (Completed)
* **Status:** Complete.
* **Target Failure Class:** Retrieval capability gaps.
* **Solutions Integrated:**
  * `references` tool exposed through `ExecutionEngine` and tool routing.
  * Deterministic caller lookup via `SymbolService::find_references`.
  * See `docs/telemetry/reference_search_report.md`.

### 6.4 Priority 4: Answer Finalization -- Level 2.3 (Completed)
* **Status:** Built and Verified.
* **Target Failure Class:** Synthesis, Gate.
* **Problem:** AI received `result.summary` containing tool calls, confidence values, and planner state. The user saw raw `<tool_call>` blocks and "confidence = 0.562" in synthesized answers.
* **Solutions Integrated:**
  * **evidence_summary field:** Added to `ExecutionResult`. Contains clean evidence-only content with no tool names, confidence values, or planner metadata.
  * **Formatter boundary:** AI context switched from `result.summary` → `result.evidence_summary`. The AI never sees raw session state.
  * **Read evidence formatting:** Extracts file path from `--- filename ---` header instead of skipping it.
  * **Find evidence formatting:** Strips planner-internal `CANDIDATE:`/`SELECTED:`/`REASON:` prefixes.
  * **Strengthened system prompt:** AI explicitly forbidden from reproducing planner artifacts.
  * **Mid-loop recovery fix:** Recovery was breaking the investigation loop after one recovery tool. Changed to `continue` so the primary tool sequence completes.
  * **Recovery target propagation:** Recovery tools (especially read) were losing their targets after `after_read()` overwrites confidence result. Fixed with `last_search_target` fallback.
  * **Empty-args convergence:** Tools with empty args get `last_search_target` fallback, initialized to the user's query.
  * **Goal classification fix:** Git queries ("check the files changed", "what changed") were leaking to `GeneralChat` -- AI answered with no evidence. Added missing patterns and a safety catch for queries that escape the classifier.

### 6.5 Priority 5: Goal Understanding -- Level 2.4 (Planning Phase -- Current)
* **Status:** Design phase. See `docs/planner/goal_model.md` and `docs/planner/planner_mapping.md`.
* **Target Failure Class:** Goal Understanding (root cause).
* **Problem:** The planner jumps from text → `GoalType` using `contains_any()` keyword matching. Every new phrasing requires a keyword list update. Different phrasings of the same user intent can produce different investigation paths. The planner has no representation of what the user actually wants.
* **Proposed Solution:**
  * Add `GoalUnderstandingService` -- a deterministic parser that translates user requests into structured `Goal` objects (Intent, Entity, Artifact, Scope).
  * Run `Goal` alongside `GoalType` for telemetry comparison. Phase out `GoalType` once `Goal` proves itself.
  * Derive investigation strategy from `Goal` instead of hardcoded `GoalType` switches.
  * Tool selection becomes evidence-driven: "what evidence does this Goal need?" instead of "what keyword does this text match?"
* **Deliverables:**
  * Prompt corpus (114 prompts from 9 sources)
  * Intent taxonomy (10 immutable intents: Explain, Locate, Review, Status, Diagnose, Compare, Navigate, Modify, Execute, Chat)
  * Goal model design (Intent, Entity, Artifact, Scope -- no confidence, no tools, no planner state)
  * `GoalUnderstandingService` interface + deterministic parser
  * Migration plan: run parallel → derive evidence/completion from Goal → remove keyword lists

### 6.6 Blocked Features (Freeze Maintained)
Do NOT implement until telemetry justifies it:
* Repair Loop / Autonomous code modification.
* Natural Language → Command Translation (Shell Translator).
* Git History/Status UI dashboards.
* Semantic search / AST indexing / tree-sitter.
* Subagents (frozen -- see AGENTS.md).

### 6.7 Key Success Metric (Revised)
* **Primary:** Reduce the production-only `user_rejected` rate from **42.0%** to a target below **10%** before introducing any other major capabilities.
* **Secondary:** Eliminate keyword-list expansion as a fix pattern -- no new phrasing should require code changes.
* **Tertiary:** Reduce `insufficient_evidence` on clean developer traces below **2.0%**. (Currently 4.6% after retrieval fixes; remaining traces are routing-edge cases and nonexistent-symbol queries.)

---

## 7. Telemetry Correction & Post-Fix Validation

To establish a completely clean baseline, we performed a deep audit of the production traces to isolate synthetic artifacts and evaluate the impact of the integrated routing fixes.

### 7.1 Discovery of Unit Test Contamination
We isolated **159 `user_rejected` traces** that were logged under the query `"test input"`. These were generated by repeated runs of the C++ unit test suite (`tests/main_test.cpp`), which calls `replay.log_input(...)` with hardcoded rejection outcomes.
* **Correction:** Excluding these test runs yields **217 actual developer production traces** with a true baseline of **0.0% `user_rejected`** events. All other rejections are synthetic or mock workflow testing.

### 7.2 Clean Developer Outcome Comparison

By replaying the 217 clean developer queries through the normalized C++ routing, meta-query grounding, and outcome isolation logic, we simulated the post-fix distribution:

| Outcome | Pre-Fix Count | Pre-Fix % | Post-Routing Fix Count | Post-Routing Fix % | Post-Retrieval Fix Count | Post-Retrieval Fix % | Post-Answer-Finalization |
|---|---|---|---|---|---|---|---|
| **Success** | 131 | 60.4% | 199 | 91.7% | 203 | 93.5% | Stable |
| **Insufficient Evidence** | 81 | 37.3% | 14 | 6.5% | 10 | 4.6% | Stable |
| **Failure** | 5 | 2.3% | 4 | 1.8% | 4 | 1.8% | Stable |
| **User Rejected** | 0 | 0.0% | 0 | 0.0% | 0 | 0.0% | Stable (no new data) |

**Projected impact of Level 2.3 (Answer Finalization):** No change to outcome counts -- answer finalization affects what the user sees, not whether evidence is collected. But the perceived quality improves because answers no longer contain raw tool calls or confidence values.

**Projected impact of Level 2.4 (Goal Understanding):** Could further reduce `InsufficientEvidence` by ensuring the planner starts the correct investigation path. Currently, different phrasings of the same query can route to different GoalTypes, producing different evidence. With Goal Understanding, same intent → same investigation → same evidence.

### 7.3 Breakdown of Resolved Telemetry
The routing improvements resolved **68 historical telemetry failures**:
* **65 traces** resolved via **MetaCommand (Slash)** command parsing (e.g. `/`, `/llm`, `/help`, `/debug` no longer inheriting carryover rejections).
* **1 trace** resolved via **DirectCommand/CLI** command (e.g. `clear`).
* **1 trace** resolved via **Git/CommitHistory Typo Fix** (e.g. `comit` → `commit`).
* **1 trace** resolved via **CodebaseOverview Typo Fix** (e.g. `codbease` → `codebase`).

Subsequent cycles (retrieval, answer finalization, goal understanding) address deeper bottlenecks that routing fixes could not reach.

### 7.4 Remaining Bottleneck (Original Analysis)
Following the telemetry and routing fixes, the remaining **6.5% `insufficient_evidence`** events (14 traces) were originally interpreted as 100% genuine **Retrieval and Ranking** failures:
* `where is replay implemented` (8x)
* `find the cursor binary` / `find the cursor bin` (4x)
* `where is CommandRouter implemented` (2x)

**Revised interpretation (Level 2.4):** While these manifested as retrieval failures, at least some originated from Goal Understanding failures. The `replay` and `CommandRouter` queries were attributed to routing/meta carryover. The `cursor binary` queries were a genuine retrieval failure (multi-word pattern matching). The distinction matters because treating all remaining failures as retrieval problems would lead to more retrieval tooling ("add another search path") rather than fixing the upstream understanding gap.

### 7.5 Post-Fix Outcome (Directory-Aware Find Shared Engine)

**Fix applied:** A shared ranking engine (`Services::directory_aware_find()` in `include/services/find_service.h` / `src/services/find_service.cpp`) replaced 4 duplicated find implementations. All callers now use word-level matching, CamelCase normalization, symbol scanning, directory-path matching, and implementation-file boost.

**Resolution per query:**

| Query | Pre-Fix Outcome | Post-Fix Outcome |
|---|---|---|
| `where is replay implemented` | Success (already worked) | Success (unchanged) |
| `find cursor binary` | InsufficientEvidence | Success (word-level match on `cursor_binary` stem) |
| `find cursor bin` | InsufficientEvidence | Success (word-level match on `cursor_binary` stem) |
| `where is CommandRouter implemented` | Success (already worked) | Success (unchanged) |

**Metrics:**
```
filename_hits:     cursor[_-]?binary → 0 → 1 candidate (fixed)
grep elimination:  cursor binary no longer needs grep fallback
tool reduction:    3 tools → 2 tools (find+read instead of find+grep+read)
```

### 7.6 Reference Search Verification & Outcomes

The Reference Search capability was validated against four acceptance queries, resolving them entirely via the `references` tool and direct `read` commands:

| Acceptance Query | Tool Execution Path | Status |
|---|---|---|
| `who calls ReplayService` | `references ReplayService` → `read` | PASS |
| `where is CommandRouter referenced` | `references CommandRouter` → `read` | PASS |
| `who uses ToolResult` | `references ToolResult` → `read` | PASS |
| `where is SessionState used` | `references SessionState` → `read` | PASS |

All 8/8 regression scenarios pass successfully.

### 7.7 Level 2.3 Post-Fix Metrics

After Answer Finalization (evidence_summary, Formatter, recovery-continue fix, git classification fix):

| Metric | Before | After |
|---|---|---|
| Raw tool calls in AI context | Yes (summary contained `find()`, `read()`, `grep()` output) | No (evidence_summary contains only extracted content) |
| Confidence values leaked to AI | Yes (`confidence = 0.562` in context) | No (confidence is in planner state, not in evidence) |
| Recovery break behavior | Break after one recovery tool | Continue -- primary tool sequence completes |
| Git classification (new phrasings) | Leaked to GeneralChat (no evidence) | Pattern-matched to CommitHistory |
| Extraction tests passing | 45/47 | 47/47 |

### 7.8 Current Roadmap Decision (Revised)

**Phase 1-5 are complete.** The system has matured through:
- Keyword routing fixes
- Deterministic retrieval (directory-aware find + reference search)
- Planner recovery (confidence gates, recovery-continue)
- Evidence packaging (InvestigationSession, EvidencePackage)
- Answer finalization (evidence_summary, Formatter)

**Current phase (Level 2.4):** Goal Understanding -- design phase.

The remaining bottleneck is not retrieval. It is **goal understanding**. The planner can find files, run git, and collect evidence -- but it may start the wrong investigation because it doesn't understand what the user is asking.

**Implementation freeze maintained for:**
- Subagents / repair loops / shell translation / git dashboards.
- Semantic search / AST indexing / tree-sitter.
- Any new tool capability not required by Goal Understanding.

**Level 2.4 is not a freeze violation.** Goal Understanding is a planner change, not a tool expansion. It changes how the planner interprets user input before selecting tools. It does not add new tools, search paths, or LLM features.

---

## 8. Related Engineering Milestones

The architectural changes described in this report are implemented in the following commits:

```
8efa69df  Confidence calibration -- category-weighted combine with convergence bonus
b83bca26  Answer finalization -- evidence_summary, recovery-continue, git classification
58921a33  Core documentation rewrite -- product identity, architecture, design
2b4d716f  Failure topology revision -- architectural evolution, goal understanding failures
faf66d9e  Confidence calibration investigation records
a3fe130c  Goal understanding architecture proposal
a626c0e4  Failure topology report refinement -- snapshot, generations, lessons learned
```

---

## 9. Lessons Learned

These conclusions emerged from evidence accumulated across all six phases. They represent the engineering wisdom of the cycle, not hypotheses.

* **Retrieval quality cannot compensate for incorrect goal understanding.** The fastest find, most accurate grep, and best-ranked results are wasted when the planner investigates the wrong question. Every retrieval fix in Phases 1-2 addressed symptoms, not the underlying misclassification.

* **Recovery improves evidence quality but cannot repair a misidentified goal.** Mid-loop recovery (Phase 3) successfully detects low confidence and broadens the search, but if the GoalType was wrong, recovery still collects evidence against the wrong intent. Recovery is a tactical fix, not a strategic one.

* **Answer quality depends as much on evidence formatting as on evidence collection.** The Answer Finalization phase (Level 2.3) changed no tooling, added no new search capability, and fixed zero retrieval bugs. Yet it dramatically improved perceived quality by stripping planner metadata from the AI context. What the AI sees matters as much as what the planner finds.

* **Planner improvements consistently produced larger gains than tool additions.** Keyword routing fixes (Phase 1) resolved 68 historical failures -- more than any single tool or retrieval upgrade. Confidence calibration (Phase 3) and evidence formatting (Level 2.3) each produced measurable gains without a single new file search capability. The planner, not the toolbelt, is the leverage point.

* **Intent fragmentation is the deepest bottleneck.** The same user intent ("what changed in my working tree?") could route to five different GoalTypes depending on phrasing. No amount of retrieval or recovery can fix the inconsistency that begins at classification. Goal Understanding (Level 2.4) is the response.
