# Architecture Diff Review

Generated: 2026-06-25

Review scope: All changes since ModelCatalog migration (commit 67c86d4d).

Review mode: Read-only. No files modified.

---

## Findings

### 1. ModelCatalog Migration

#### 1.1 AuthService duplicates provider metadata (HIGH)

- **Evidence:** `src/services/auth_service.cpp:396-437` (`initialize_default_providers()`)
- **Evidence:** `include/core/model_catalog.cpp:9-77` (`ProviderConfig` entries)
- The same `base_url`, `model` names, and `display_name` values exist in both files with discrepancies:
  - auth_service uses `"gpt-4"` as OpenAI default; ModelCatalog lists `"gpt-4.1"` and `"gpt-4o"`
  - auth_service uses `"llama3.1-8b"` for Cerebras; ModelCatalog has `"llama-4-maverick-17b-128e-instruct"`
  - **auth_service defines an `anthropic` provider; ModelCatalog has no `Provider::Anthropic`**
- **Risk:** Two metadata systems will drift. Anthropic already added to AuthService without a ModelCatalog entry. The same base URLs exist in both files with different paths (`/api/chat` vs `/v1`).
- **Recommendation:** Derive AuthService's provider metadata from ModelCatalog, or remove the duplicate struct.

#### 1.2 AuthProvider is NOT a duplicate of ProviderConfig — but the review finding is wrong (HIGH)

- **Evidence:** `include/services/auth_service.h:10-18` (`AuthProvider`) vs `include/core/model_catalog.h:35-42` (`ProviderConfig`)
- AuthProvider has: `name`, `display_name`, `api_key`, `base_url`, `model`, `is_active`, `is_valid`, `additional_config` — runtime credential container
- ProviderConfig has: `id` (enum), `base_url`, `test_url`, `auth_scheme`, `response_fmt`, `online` — static endpoint config
- They share only `base_url`. Different purposes, different lifecycles.
- **Risk:** The existing ArchitectureReview finding #3 (AuthProvider duplicates ModelCatalog) is a **false positive** — it claims duplication that does not exist. This will undermine trust in the review system.

#### 1.3 provider_label/category_label/tier_label are NOT duplicates of ModelCatalog (HIGH)

- **Evidence:** `src/app/startup.cpp:15-27` (`provider_label`), `:29-37` (`category_label`), `:40-46` (`tier_label`), `:49-61` (`api_key_var`)
- ModelCatalog has no enum-to-display-name mapping methods. These four functions provide functionality that ModelCatalog lacks.
- All four are actively used (`startup.cpp:129,159,196,203-204`).
- **Risk:** The existing ArchitectureReview finding #4 is a **false positive** — it claims duplication that does not exist. ModelCatalog has no `short_provider()` equivalent for all providers (it has one for specific display scenarios).

#### 1.4 AgentMode / MODE_ constants are dead code (MEDIUM)

- **Evidence:** `include/agent_mode.h:5-17` — full enum declaration with 11 values, zero `src/` references
- The only include is `src/diagnostics/diagnostics.cpp:4`, which never references any `AgentMode::` value
- **Risk:** Maintenance hazard. New modes could be added here thinking they do something.
- **Recommendation:** Remove file after one release cycle.

#### 1.5 Multiple hardcoded provider strings bypass ModelCatalog (MEDIUM)

| Location | Line | Bypassed ModelCatalog data |
|----------|------|---------------------------|
| `src/services/auth_service.cpp` | 401 | Together base URL |
| `src/services/auth_service.cpp` | 410 | Ollama base URL (wrong path: `/v1` vs `/api/chat`) |
| `src/services/auth_service.cpp` | 419 | OpenAI base URL |
| `src/services/auth_service.cpp` | 427 | Anthropic base URL (Anthropic not in ModelCatalog) |
| `src/services/auth_service.cpp` | 435 | Cerebras base URL |
| `src/app/startup.cpp` | 82 | Ollama model discovery URL |

- **Risk:** URL changes require modifying two files in sync.
- **Recommendation:** All base URLs should live in `ProviderConfig` derivable from `ModelCatalog`.

#### 1.6 Duplicate env-var-to-provider mapping (MEDIUM)

- **Evidence:** `src/app/startup.cpp:49-61` vs `src/services/ci_investigation_service.cpp:624-629`
- Both files hardcode the same mapping from env-var names to `Provider` enum values.
- **Risk:** Adding a new provider requires updating both files.
- **Recommendation:** Centralize env-var mapping in ModelCatalog.

---

### 2. ExecutionEngine

#### 2.1 AIService used for goal classification and tool selection (HIGH)

- **Evidence:** `src/services/execution_engine.cpp:198-199` calls `classify_goal_llm(goal)`
- `src/services/execution_engine.cpp:243-270` (`classify_goal_llm`): calls `ai_->chat(prompt, "")` — uses LLM for routing decisions
- `src/services/execution_engine.cpp:338-339` calls `select_next_tool_llm(...)`
- `src/services/execution_engine.cpp:617-718` (`select_next_tool_llm`): calls `ai_->chat(prompt, "")` — uses LLM to choose investigation tools, with a prompt that demands command syntax
- The system prompt (`ai_service.cpp:42-56`) says "Never emit command syntax" but the caller prompt (`execution_engine.cpp:686-696`) demands "Choose the next tool. Options: grep <query>..."
- **Risk:** Direct violation of the architecture constraint that AIService is synthesis-only. The LLM receives contradictory instructions. When enabled (`ClassifierMode::LLM`), the LLM drives the investigation loop rather than synthesizing from evidence.
- **Recommendation:** Move LLM-assisted classification and tool selection out of AIService or document them explicitly.

#### 2.2 CommitHistory unreachable for "where are" + git queries (HIGH)

- **Evidence:** `src/services/execution_engine.cpp:174` matches `"where are"` before reaching CommitHistory at line 185
- Query `"where are recent commits"` matches CodebaseQuery call-site keywords first and never reaches CommitHistory.
- **Risk:** Intentional git queries get misclassified as CodebaseQuery.

#### 2.3 tool_history is never consulted in completion decisions (HIGH)

- **Evidence:** `src/services/execution_engine.cpp:724-775` (`check_completion()`) takes no `tool_history` parameter
- All completion decisions are driven by `EvidenceStore.facts` and `EvidenceStore.classes`
- The tool_history vector is collected, rendered to LLM, and stored in ExecutionResult — but never used as input to a stop decision.
- **Risk:** If a fact is not added correctly, the engine runs all 20 iterations regardless of actual progress made.

#### 2.4 ai_response field is declared, never populated, checked by dead diagnostic code (HIGH)

- **Evidence:** `include/services/execution_engine.h:49` declares `std::string ai_response`
- `src/services/execution_engine.cpp:962-1155` never assigns it
- `src/diagnostics/diagnostics.cpp:573` checks `if (!result.ai_response.empty())` — always false
- **Risk:** Dead field in the struct, dead branch in diagnostics.

#### 2.5 TrustMetrics schema exists but never populated by engine (HIGH)

- **Evidence:** `include/core/metrics.h:96-110` — all 4 fields declared
- Zero assignments to `TrustMetrics` fields exist in any `src/` file (excluding serialization/deserialization)
- `command_router.cpp:226` copies all-false default from engine result. `session.cpp:296` writes all-false to replay.
- **Risk:** Every replay event carries `plan_approved=false, diff_approved=false, user_corrected_goal=false, reverted=false`. Future trust analytics would be misleading.

#### 2.6 Structurally duplicate CodebaseOverview check (MEDIUM)

- **Evidence:** `src/services/execution_engine.cpp:177-179` vs `:218-223`
- Both check for `{"tell me about", "overview", "describe", "what is this"}` + codebase keywords
- The only difference is `detect_evidence_need` guard in the second check
- **Risk:** Maintenance: modifying one without the other.

#### 2.7 ArchitectureReview completion check passes empty tool_history (MEDIUM)

- **Evidence:** `src/services/execution_engine.cpp:767`
- `return select_next_tool(goal, type, evidence, {}).tool.empty();` — hardcoded empty `{}`
- The execution loop passes real `result.tool_history` at line 980
- **Risk:** Currently benign (ArchitectureReview tool selection is deterministic and history-independent), but if tool selection is later changed to depend on tool history, the completion check will silently diverge from actual execution.

#### 2.8 ArchitectureReview completion uses dual mismatched gates (MEDIUM)

- **Evidence:** `src/services/execution_engine.cpp:728-732` (universal evidence class gate) vs `:766-767` (type-specific tool-selection gate)
- Universal gate requires `{Discovery, FileSearch, FileContent}` evidence classes
- Type-specific gate requires `select_next_tool()` returning empty
- If a grep returns no matches, `FileSearch` is NOT marked (line 1031-1032 checks `has_results`) — universal gate fails, type-specific gate passes
- **Risk:** ArchitectureReview reports Success but outcome could be Failure/InsufficientEvidence based on which gate last evaluated.

#### 2.9 exit_code available but ignored for build/test gates (MEDIUM)

- **Evidence:** `src/services/execution_engine.cpp:1037-1041`
- Build success determined by stdout.contains("error") — not by `tr.exit_code`
- Test success determined by stdout.contains("failed"/"FAILED") — not by `tr.exit_code`
- **Risk:** Exit code is the canonical success indicator. stdout matching is fragile (false positive on warnings containing "error", locale-sensitive).

#### 2.10 success field is redundant with outcome (MEDIUM)

- **Evidence:** `include/services/execution_engine.h:46` (`bool success`) vs `:47` (`Outcome outcome`)
- `result.success` set at line 1118, then outcome determined at lines 1143-1152 (which may override)
- Callers use `result.outcome`, never `result.success`
- **Risk:** Dead field. Maintains two success indicators that can disagree.

#### 2.11 stopped_early/stop_reason set but never read by callers (MEDIUM)

- **Evidence:** Set at lines 987-989 (dedup) and 1082-1084 (low confidence)
- `command_router.cpp:149-226` reads `goal_type`, `success`, `summary`, `evidence`, `confidence`, `outcome`, `recovery_metrics`, `trust_metrics` — but never `stopped_early` or `stop_reason`
- **Risk:** Telemetry gap — the engine collects stop reasons but callers ignore them.

#### 2.12 Dedup is exact string match only (MEDIUM)

- **Evidence:** `src/services/execution_engine.cpp:985`: `tc.tool + ":" + tc.args`
- No normalization. `grep "foo"` and `grep 'foo'` are different signatures. `read foo.h` and `read "foo.h"` are different.
- **Risk:** LLM tool selection (or human variation) can produce semantically identical calls that bypass dedup.

#### 2.13 CICheck's "ci" keyword is very broad (LOW)

- **Evidence:** `src/services/execution_engine.cpp:206-208`
- Any query mentioning "ci" as a standalone word routes to CICheck before GeneralChat.
- **Risk:** "tell me about the CI pipeline" is correct; "does this code violate CI best practices" might surprise.

#### 2.14 stderr collected but never influences engine decisions (LOW)

- **Evidence:** `src/services/execution_engine.cpp:1024` checks stdout for results
- stderr populated in lambda (`command_router.cpp:201-210`) but never examined for evidence marking, completion, or confidence
- **Risk:** Important error information in stderr is invisible to the engine.

#### 2.15 stdout string matching for build/test is fragile (LOW)

- **Evidence:** `src/services/execution_engine.cpp:1037-1041`
- Build: checks for "error" in stdout. Test: checks for "failed"/"FAILED" in stdout.
- Locale-sensitive, false-positive-prone on warnings with "error" in text.

#### 2.16 MAX_ITERATIONS=20 is the only hard infinite-loop prevention (LOW)

- **Evidence:** `src/services/execution_engine.cpp:971` — `for (; iteration_count < MAX_ITERATIONS; iteration_count++)`
- Four exit conditions: completion gate, empty tool, dedup, low confidence
- If none fire, 20 iterations run regardless
- **Risk:** ArchitecturalReview takes 11 steps, close to 20. Adding more review steps risks hitting the limit.

#### 2.17 Three parallel fact-tracking mechanisms (LOW)

- **Evidence:** `src/services/execution_engine.cpp:1000-1027`
- `evidence.add_fact(tc_signature)` (tool+args string)
- `evidence.facts.push_back(fact_string)` (formatted "[tool] args — stdout_prefix")
- `evidence.add_fact("grep:results")` etc. (result suffix facts for grep/read/cmake/ctest)
- **Risk:** Three conventions must be kept in sync. Completion gate may use one mechanism while another is stale.

---

### 3. AIService

#### 3.1 AIService used for orchestration despite docs saying synthesis-only (HIGH)

See Finding 2.1 for full evidence. System prompt contradiction:
- AIService system prompt: "Never suggest internal tools or commands" / "Never emit command syntax of any kind"
- ExecutionEngine caller prompt: "Choose the next tool. Options: grep <query>... Respond with exactly one option, no explanation"
- **Risk:** LLM receives contradictory instructions. Behavior under contradiction is undefined.

#### 3.2 AIService system prompt says "investigation is done" when used during investigation (MEDIUM)

- **Evidence:** `src/services/ai_service.cpp:44-46`: "Repository investigation has already been performed. The results are in the conversation history below."
- But when called from `classify_goal_llm` (line 259) or `select_next_tool_llm` (line 698), context is empty `""` and investigation is ongoing.
- **Risk:** Misleading prompt state.

#### 3.3 No response post-processing to strip commands (MEDIUM)

- **Evidence:** `src/app/command_router.cpp:815-828`
- Response goes through `render_markdown()` only — no command-stripping, no content validation, no contract enforcement
- If the LLM ignores the prompt, raw command syntax passes to stdout and saves to memory.
- **Risk:** Relies entirely on prompt compliance for the "synthesis-only" contract.

#### 3.4 `override_api_model` declared and defined but never called (LOW)

- **Evidence:** `include/services/ai_service.h:26` declaration, `src/services/ai_service.cpp:18-20` definition
- Zero call sites in `include/` or `src/`
- **Risk:** Dead API surface. Intended for runtime model override but never wired up.

---

### 4. Replay

#### 4.1 TrustMetrics never populated in production (HIGH)

See Finding 2.5 for full evidence.

#### 4.2 `token_usage_` declared, serialized, displayed, but never assigned (HIGH)

- **Evidence:** `include/core/session_state.h:31` declares `long long token_usage_{0}`
- `src/services/replay_service.cpp:54` deserializes it (always 0)
- `src/ui/ui_manager.cpp:454` displays it (always 0)
- `src/app/command_router.cpp:2668-2670` compares it (always 0 vs 0)
- Zero assignments in production `src/` code.
- **Risk:** Displayed metric is always 0. User sees unused token counter.

#### 4.3 SessionState serialization is only 44% complete (MEDIUM)

- **Evidence:** `src/services/replay_service.cpp:34-43` serializes 8 of 18 fields
- Missing: `inspect_mode_`, `llm_classifier_`, `perm_mode_`, `last_recovery_metrics`, `last_trust_metrics`, `cwd_`, `repo_root_`, `git_branch_`, `git_status_`, `build_artifacts_`
- **Risk:** Replay logs lose repository context and runtime flags on every log. Loaded sessions will have default values for these fields.

#### 4.4 `active_model` never updated from hardcoded default (MEDIUM)

- **Evidence:** `include/core/session_state.h:12-21` — always the default ollama-llama3.2-3b
- Zero reassignments in `src/`
- **Risk:** If user configures a different model, SessionState will not reflect it.

#### 4.5 No schema version on replay storage (MEDIUM)

- **Evidence:** `src/services/replay_service.cpp` — JSON lines format, no version field
- `load_session` uses `j.value("field", default)` — works for additive changes, silently loses data on field removals/renames
- `src/services/replay_service.cpp:230-232` — malformed lines silently skipped via try/catch
- **Risk:** Schema changes cause silent data loss. No migration path.

#### 4.6 RecoveryMetrics.strategy_changes never assigned (LOW)

- **Evidence:** `include/core/metrics.h:66` declares `int strategy_changes{0}`
- Serialized (`replay_service.cpp:112`), deserialized (`:208`), displayed (`dashboard_service.cpp:507`)
- Zero assignment statements in any `src/` file
- Self-acknowledged by engine's own review (`execution_engine.cpp:904-917`): "Never assigned. Always 0."
- **Risk:** Displayed dashboard metric is always 0.

#### 4.7 Replay directory grows unboundedly (LOW)

- **Evidence:** `src/services/replay_service.cpp:83-89` — fixed path `~/.cursor/replay/`
- No cleanup, rotation, or retention policy
- **Risk:** Disk usage grows with usage.

---

### 5. ArchitectureReview

#### 5.1 Finding #3 (AuthProvider duplication) is a false positive (HIGH)

- **Evidence:** `src/services/execution_engine.cpp:862-873`
- Claims AuthProvider duplicates ModelCatalog metadata. Incorrect — AuthProvider is a runtime credential container, ProviderConfig is static endpoint config. They share only `base_url`.
- ModelCatalog is never read in the investigation plan, so the finding cannot verify its own claim.
- **Risk:** Undermines trust in the review system. Finding will always trigger regardless of codebase state.

#### 5.2 Finding #4 (provider_label duplication) is a false positive (HIGH)

- **Evidence:** `src/services/execution_engine.cpp:875-886`
- Claims `provider_label`/`category_label`/`tier_label` duplicate ModelCatalog display logic. Incorrect — ModelCatalog has no enum-to-display-name mapping for these enums.
- Four functions are all actively used. ModelCatalog provides no alternative.
- **Risk:** Same as 5.1. Always triggers. Incorrect premise.

#### 5.3 Finding #7 (ArchitectureReview untested) has a string-matching bug (MEDIUM)

- **Evidence:** `src/services/execution_engine.cpp:930-931`
- Checks for literal string `"ArchitectureReview"` in `validation_runner.cpp` content
- Queries at `validation_runner.cpp:113-115` are natural language: `"review architecture"`, `"review codebase"`, `"review recent changes"`
- The string `"ArchitectureReview"` never appears in validation_runner.cpp
- **Risk:** This sub-finding always fires even though ArchitectureReview IS tested.

#### 5.4 Investigation plan covers only 5 of 7 top-level directories (MEDIUM)

- **Evidence:** `src/services/execution_engine.cpp:580-605` — 11 sequential steps
- Reads only: `session_state.h`, `metrics.h`, `execution_engine.cpp`, `validation_runner.cpp`
- Greps only: `AgentMode`, `MODE_`, `AuthProvider`, `provider_label|...`, `strategy_changes`
- Misses: `ui/`, `utils/`, `app/` (except by grep), `services/` (except execution_engine), `core/model_catalog.h`, `replay_service.cpp`, `ai_service.cpp`, `AGENTS.md`
- **Risk:** Architecture areas with significant issues (AIService role violation, TrustMetrics gaps) have no dedicated review finding.

#### 5.5 Evidence for "AgentMode never referenced" is incomplete (MEDIUM)

- **Evidence:** `src/services/execution_engine.cpp:840-848`
- Grep confirms the enum file exists but cannot prove "never referenced" — only proves no scoped `AgentMode::` usage
- `diagnostics.cpp:4` includes `agent_mode.h` (compile-time reference)
- The finding ignores this include.

#### 5.6 MODE_ "unused" claim mixed with review's own references (LOW)

- **Evidence:** `src/services/execution_engine.cpp:851-859`
- Grep for `MODE_` matches lines in the investigation plan (585, 588, 852) and finding text itself
- Cannot distinguish production usage from review code usage
- **Risk:** Finding overcounts MODE_ occurrences.

---

### 6. Validation

#### 6.1 Two GoalTypes have zero test coverage (HIGH)

- **Evidence:** `CodeChange` and `GitHubInvestigation` — zero queries in benchmark, validation, scenarios, and unit tests
- Entire execution paths could be broken without detection.
- **Risk:** Any regression in these GoalTypes is invisible.

#### 6.2 No gating for validation or benchmark runners (HIGH)

- **Evidence:** `CMakeLists.txt:606-626` — `validation_runner` and `benchmark_runner` are compiled but never added to `add_test()` or `check` target
- A complete failure in either produces zero build or test failures.
- No `.github/` directory exists — no CI pipeline enforces these checks.
- **Risk:** The entire investigation quality validation suite runs only when manually invoked.

#### 6.3 No very-long-input or special-character edge case tests (HIGH)

- **Evidence:** Zero tests with query strings >1KB, unicode, shell metacharacters, or injection patterns
- Empty input has only a trivial slash-command test
- **Risk:** Security and stability vulnerabilities could go undetected.

#### 6.4 Benchmark covers only 44% of GoalTypes (MEDIUM)

- **Evidence:** `scenarios/benchmark/benchmark_suite.json` — 33 queries covering 4 of 9 GoalTypes
- Missing: `GeneralChat`, `SessionState`, `CodeChange`, `GitHubInvestigation`, `ArchitectureReview`
- **Risk:** Insufficient data to validate engine behavior across the full GoalType space.

#### 6.5 Validation covers only 55% of GoalTypes (MEDIUM)

- **Evidence:** `tests/validation_runner.cpp:90-116` — 20 queries covering 5 of 9 GoalTypes
- Missing: `GeneralChat`, `CodeChange`, `CICheck`, `GitHubInvestigation`
- **Risk:** Major GoalTypes without end-to-end smoke tests.

#### 6.6 `build_review_report()` never called in any test (MEDIUM)

- **Evidence:** `include/services/execution_engine.h:98` — private method
- Not exercised by validation_runner, benchmark, scenarios, or unit tests
- **Risk:** The entire ArchitectureReview report generation has zero test coverage beyond manual runs.

#### 6.7 LLM classifier modes untested (LOW)

- **Evidence:** `classify_goal_llm()` and `select_next_tool_llm()` exist but no test exercises LLM classifier mode
- **Risk:** LLM-powered classification could degrade undetected.

#### 6.8 Failure classification logic untested (MEDIUM)

- **Evidence:** `tests/validation_runner.cpp:29-71` — `classify_failure()`, `root_cause()`, `repair_candidate()`, `is_recoverable()`
- Pure logic with no unit tests
- Only executed when manually running the validator
- **Risk:** Failure-classification system has no regression protection.

---

### 7. Documentation

#### 7.1 AIService role documented as synthesis-only, actually used for orchestration (HIGH)

- **Evidence:** `AGENTS.md:130-136` and `ARCHITECTURE.md:92-96`: "AIService does not: investigate repositories, execute tools, emit commands, perform orchestration."
- Actual: `execution_engine.cpp:198-199` uses AIService for goal classification, `:338-339` for tool selection
- Both docs are wrong about the AIService role. See Finding 2.1 for full evidence.

#### 7.2 "Completion Gate" portrayed as distinct component, is just a method (MEDIUM)

- **Evidence:** `AGENTS.md:46-50` shows `EvidenceStore → Completion Gate → AIService` as distinct pipeline stage
- Actual: `execution_engine.h:94-95` — `bool check_completion(...)` is a private method of ExecutionEngine
- No `CompletionGate` class, struct, or file exists anywhere
- **Risk:** New contributors search for a component that does not exist.

#### 7.3 EvidenceStore portrayed as pipeline component, is a local struct (MEDIUM)

- **Evidence:** `AGENTS.md:46` — portrays as separate data store between tool_history and completion
- Actual: `execution_engine.h:32-44` — struct with string vectors, created as local variable in `execute()` (line 966)
- Not a shared/singleton store. Ephemeral per-call.
- **Risk:** Implies durability and cross-session access that does not exist.

#### 7.4 ConfidenceService completely undocumented (MEDIUM)

- **Evidence:** `include/services/confidence_service.h:14-54` — full class with `after_search()`, `after_read()`, `after_build()`, `after_tests()`, `after_ci()`, `after_discovery()`, `combine()`, `should_proceed()`, `should_stop()`
- Zero mentions in AGENTS.md or ARCHITECTURE.md
- Drives early termination logic at `execution_engine.cpp:1050-1085`
- **Risk:** Major behavioral component (confidence-driven early stopping) has no documentation.

#### 7.5 Five enums undocumented (MEDIUM)

- `ExecutionPath` (`metrics.h:6-14`) — 7 values
- `PermissionMode` (`session_state.h:8`) — 3 values
- `ClassifierMode` (`execution_engine.h:30`) — 2 values
- `EvidenceClass` (`execution_engine.h:28`) — 7 values
- `EvidenceNeed` (`execution_engine.h:29`) — 2 values
- All control branching logic in the engine. None documented.
- **Risk:** `ClassifierMode::LLM` is directly linked to the AIService role discrepancy.

#### 7.6 "Router" named imprecisely — actual class is CommandRouter (LOW)

- **Evidence:** `AGENTS.md:67`: "connect Engine, Replay, Router, and UI"
- Actual: `include/app/command_router.h:23`: `class CommandRouter`
- No `Router` class exists.

#### 7.7 "Replay" named imprecisely — actual class is ReplayService (LOW)

- **Evidence:** `AGENTS.md:184` section titled "Replay"
- Actual: `include/services/replay_service.h:30`: `class ReplayService`

#### 7.8 SessionState documentation omits 10 fields (LOW)

- **Evidence:** `AGENTS.md:78-84` and `ARCHITECTURE.md:117-123` list ~5 fields
- `session_state.h:10-47` has 18 fields. Missing: `perm_mode_`, `llm_classifier_`, `command_count_`, `token_usage_`, `last_execution_path`, `cwd_`, `repo_root_`, `git_branch_`, `git_status_`, `build_artifacts_`

#### 7.9 DESIGN.md has no structural conflicts (INFORMATIONAL)

- Evidence: DESIGN.md documents UI philosophy and layout, makes no structural claims about components.

#### 7.10 No removed concepts referenced in docs (INFORMATIONAL)

- All GoalTypes in code exist. No orphaned enum values or classes mentioned in docs that no longer exist.

---

## Summary

### By Severity

| Severity | Count | Key Issues |
|----------|-------|------------|
| HIGH | 11 | AIService role violation, codebase/validation/doc gaps |
| MEDIUM | 20 | Routing edge cases, serialization gaps, coverage holes |
| LOW | 10 | Dedup precision, naming drift, stdout matching |

### By Area

| Area | HIGH | MEDIUM | LOW |
|------|------|--------|-----|
| 1. ModelCatalog | 3 | 2 | 0 |
| 2. ExecutionEngine | 4 | 8 | 5 |
| 3. AIService | 1 | 2 | 1 |
| 4. Replay | 2 | 3 | 2 |
| 5. ArchitectureReview | 2 | 3 | 1 |
| 6. Validation | 3 | 3 | 2 |
| 7. Documentation | 1 | 5 | 3 |

### Cross-Cutting Risks

1. **AIService role violation** (Findings 2.1, 3.1, 7.1, 7.5): The architecture documents categorically forbid AIService from making orchestration decisions, but the code implements LLM-assisted goal classification and tool selection. Two of two architecture documents are wrong about this, and the system prompt contradicts the caller prompt.

2. **False positives in ArchitectureReview** (Findings 1.2, 1.3, 5.1, 5.2): Two review findings (AuthProvider duplication, provider_label duplication) make incorrect claims that ModelCatalog has capabilities it does not. ModelCatalog is never read in the investigation plan, so these findings cannot verify their own premises. They will always trigger and undermine trust.

3. **TrustMetrics/RecoveryMetrics schema without population** (Findings 2.5, 4.1, 4.2, 4.6): Five fields across two structs are declared, serialized, displayed, but never assigned in production. All replay events carry default (zero/false) values.

4. **Validation blind spots** (Findings 6.1, 6.2, 6.4, 6.5): Two GoalTypes have zero test coverage. No validation runner or benchmark runner is gated in CI. The entire investigation quality validation suite runs only manually.

5. **Serialization gaps** (Findings 4.3, 4.5): Only 44% of SessionState fields are serialized. No schema version exists on replay storage. Schema changes cause silent data loss.

### What the ArchitectureReview gets right

Despite the false positives on findings #3 and #4, the ArchitectureReview system correctly identifies:
- AgentMode/MODE_ dead code (genuine)
- TrustMetrics never populated (genuine but incompletely evidenced)
- strategy_changes always 0 (genuine)
- Validation coverage gaps for CodeChange/CICheck/GitHubInvestigation (genuine)

The infrastructure (deterministic, evidence-backed, no LLM involvement) is correct and valuable. The investigation plan needs broadening and two findings need correction.

### What the ArchitectureReview misses

The review plan does not inspect:
- `AIService` role conformance (would find the HIGH-priority violation)
- `ReplayService` serialization consistency (would find the 44% gap)
- `ModelCatalog` content (would prevent the false-positive findings)
- AGENTS.md/ARCHITECTURE.md conformance (would find the documentation drift)
- `app/`, `ui/`, `utils/` directories
- `ConfidenceService` (undocumented component driving termination)

---

*End of review. Read-only. No files modified.*
