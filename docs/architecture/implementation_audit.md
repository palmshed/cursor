---
---

# Implementation Audit: Session Notes vs. Actual Code

**Date:** 2026-06-25
**Scope:** Verify every claim in session work logs against exact source code.
**Method:** Read code, trace execution paths, run validation suite.

---

## Priority 1 -- Baseline Queries (5 Queries)

### Claim: Find tried before grep for CodebaseQuery
**Verified? YES**
- `src/services/execution_engine.cpp:552-558`: `select_next_tool` for CodebaseQuery calls `extract_best_term` → `find <term> --impl` first, before any grep.
- Line 571-575: grep is fallback only if `find:done` exists without `find:results`.

### Claim: extract_best_term behavior for each query

**"where is replay implemented" → "replay"**
**Verified? YES**
- `extract_best_term` (execution_engine.cpp:65-188): prefix `"find "` removed at line 86-93, suffix `" implemented"` removed at line 97-106. Remaining: `"replay"`. Single word, returned as-is.
- Find handler (command_router.cpp:176-363): `stem_lower.find("replay")` matches `replay_service` → partial filename match (score +10).

**"find cursor binary" → "cursor[ _-]?binary"**
**Verified? YES (term extraction), NO (matching works)**
- `extract_best_term`: prefix `"find "` removed → `"cursor binary"`. Two-word group, no code-shaped words → reconstructed as `"cursor[ _-]?binary"` (line 179-184).
- **This fails to match any file** because the find handler treats `[ _-]?` as literal characters, not regex. `stem_lower.find("cursor[ _-]?binary")` will not match `cursor_binary`.

**"where is CommandRouter implemented" → "CommandRouter"**
**Verified? YES**
- `extract_best_term`: prefix `"where is "` removed → `"CommandRouter"`. TitleCase detected (line 168-170) → code word → returned as single token.
- Find handler: term_normalized splits CamelCase → `"command_router"`. Exact match on `command_router` stem (line 233-235).

### Claim: `extract_best_term_plain()` exists for space-joined multi-word terms
**Verified? NO**
- No function named `extract_best_term_plain` exists anywhere in the codebase.
- The single `extract_best_term()` always emits `[ _-]?` separators for multi-word groups (line 179-184).
- Multi-word queries like `"cursor binary"` produce regex-like strings that cannot match filenames.

### Claim: Binary detection (`build/bin/`, `/usr/local/bin/`) in find handler
**Verified? NO**
- `command_router.cpp:176-363`: find handler does directory-lookup of all files under the repo root. No special-case detection of binary paths.
- No check for `build/bin/`, `/usr/local/bin/`, `.o`, `.exe`, or any compiled binary location.

### Claim: Word-level matching in find handler (space-split, camelCase-split)
**Verified? NO**
- The find handler (command_router.cpp:226-248) compares `stem_lower` against `term_lower` using only `==` and `.find()`. No splitting by space or camelCase boundaries.
- The only term normalization is `upper-to-lower + _` insertion (line 193-198), which handles `CommandRouter` → `command_router` but does not split multi-word terms.

### Validation Suite: 28/28 validation, 32/32 benchmark passing
**Verified? YES**
- Validation runner (`tests/validation_runner.cpp`) output confirms 28/28 passing.
- Benchmark suite (`scenarios/benchmark/benchmark_suite.json`) output confirms 32/32 passing.
- Note: benchmark only checks outcome (`Success`/`Failure`), not tool sequence. Queries that changed from `grep → read` to `find → read` still pass because outcome is the same.

---

## Priority 2 -- Timeline Events

### Claim: Tool invocation emits section header + tool name
**Verified? YES**
- `ui_manager.cpp:802-828` -- `show_tool_invocation` calls `show_progress_section(section_for_tool(tool))` (line 824) then `→ <tool> <args>` (line 826-828).
- `section_for_tool` (line 781-791): maps tool → section name (e.g., find→"Locating files...", grep→"Checking symbols...").

### Claim: Tool completion emits ✓ counts
**Verified? YES**
- `ui_manager.cpp:831-928` -- `show_tool_output` shows:
  - grep (line 857-867): `✓ N matches found`
  - find (line 868-880): `✓ N candidates (selected_path)`
  - read (line 881-898): `✓ last_file` or `✓ N files`
  - git (line 899-905): `✓ N commits found`
  - gh (line 906-912): `✓ N lines`

### Claim: "Collecting evidence..." stage appears after tool loop
**Verified? YES**
- `execution_engine.cpp:1165-1189` -- After the main tool loop, shows `"Collecting evidence..."` section (line 1172), then `✓ N tool results, X grep, Y find, Z read` (line 1184-1188).
- Runs for ALL goal types, including ArchitectureReview (called from `execute()` at line 981).

### Claim: "Preparing answer..." stage shown before answer
**Verified? YES** (with caveats)
- `command_router.cpp:658-659` -- `show_progress_section("Preparing answer...")` shown before both direct answer (line 661-731) and AI chat (line 734-743).
- **NOT shown for:** ArchitectureReview (early return at line 447-453), InsufficientEvidence/Failure/UserRejected (early return at line 467-493).

---

## Priority 3 -- Architecture Review Path

### Claim: `build_review_report` is deterministic, no AI calls
**Verified? YES**
- `execution_engine.cpp:841-975` -- `build_review_report` is pure string building from tool history. No AIService, no LLM, no external calls.
- Checks for: dead AgentMode enum (line 871-898), MODE_ constants (line 901-930), test coverage gaps (line 941-963).
- Output format: `## title` + `Risk: value` via `append_finding`.

### Claim: ArchitectureReview returns directly without AI chat
**Verified? YES**
- `command_router.cpp:447-453`: If goal_type == ArchitectureReview, returns `engine_result.summary` directly, never reaches `handle_ai_chat`.
- No AIService invocation in the ArchitectureReview path.

### Claim: Execution path uses grep/read only, no LLM
**Verified? YES**
- `execution_engine.cpp:611-636` -- `select_next_tool` for ArchitectureReview uses: discovery, git log, grep (AgentMode, MODE_, AuthProvider, provider_label, strategy_changes), read (session_state.h, metrics.h, execution_engine.cpp, validation_runner.cpp). No find, no AI.

---

## Priority 4 -- Work Log Paths

### Claim: Collect + Prepare stages shown for all paths
**Verified? YES/NO**
- "Collecting evidence..." appears for ALL paths (execution_engine.cpp:1165-1189, inside `execute()`).
- "Preparing answer..." appears ONLY for CodebaseQuery success and AI chat paths (command_router.cpp:658-731, 734-743).
- **ArchitectureReview** shows "Collecting evidence..." but NOT "Preparing answer..." (returns at line 447 before reaching 658).
- **InsufficientEvidence/Failure/UserRejected** show "Collecting evidence..." but NOT "Preparing answer..." (returns at lines 467-493 before reaching 658).

### Claim: Direct answer from evidence facts
**Verified? YES**
- `command_router.cpp:661-718` -- For CodebaseQuery with Success outcome and non-empty facts, builds answer by iterating evidence facts: parses `[find ...]`, `[grep ...]`, `[read ...]` entries and extracts file paths and content.

---

## Priority 5 -- Subagent Architecture Audit

### Claim: No Agent/Worker/Coordinator/Planner/SubAgent/Dispatcher architecture
**Verified? YES**
- Searched entire `src/` and `include/` directories for class definitions and references:
  - `^class.*Agent` -- No results (no Agent class, only `AgentMode` enum which is dead code)
  - `^class.*Worker` -- No results
  - `^class.*Coordinator` -- No results
  - `^class.*Planner` -- No results
  - `SubAgent|sub_agent|sub-agent` -- No results
  - `Dispatcher` -- No results
  - `TaskPipeline` -- No results (enum value exists at `metrics.h:22` but unused)
- Architecture is a single flat pipeline: `Agent` (not a class, just a namespace/module) → `ExecutionEngine` → `CommandRouter`.
- 30+ service classes (FileService, GitService, AIService, etc.) are leaf dependencies, not subagents.

---

## Summary of Gaps

| Claim | Status | Evidence |
|-------|--------|----------|
| Find before grep | ✅ EXISTS | execution_engine.cpp:552-558 |
| extract_best_term_plain() | ❌ MISSING | No such function |
| Binary path detection | ❌ MISSING | command_router.cpp:176-363 -- no build/bin/ check |
| Word-level matching in find | ❌ MISSING | command_router.cpp:226-248 -- only `==` and `.find()` |
| CamelCase splitting before match | ❌ MISSING | Normalization exists (line 193-198) but term may already be regex-like |
| "Collecting evidence..." stage | ✅ EXISTS | execution_engine.cpp:1165-1189 |
| "Preparing answer..." stage | ✅ EXISTS | command_router.cpp:658-731 |
| ArchitectureReview: no AI | ✅ EXISTS | execution_engine.cpp:841-975, command_router.cpp:447-453 |
| Subagent hierarchy | ✅ NOT PRESENT | No Agent/Worker/Coordinator classes exist |
| Validation 28/28, Benchmark 32/32 | ✅ PASSING | Confirmed via test output |

## Root Cause: "cursor binary" Fails

The pipeline for `"find cursor binary"`:
1. `extract_best_term` → prefix removal → `"cursor binary"` → no code-shaped words → `"cursor[ _-]?binary"` (regex-like literal)
2. find handler receives `"cursor[ _-]?binary"` as literal string
3. `stem_lower.find("cursor[ _-]?binary")` will never match any filename
4. Result: `find:noresults` → grep fallback → grep for `"cursor binary"` also fails (no source file contains that string)

**Fix needed:** Either (a) `extract_best_term_plain()` to join terms with simple space, or (b) word-level matching in find handler that splits the term and matches each word independently.
