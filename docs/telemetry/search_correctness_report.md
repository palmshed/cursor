# Search Correctness Report

**Generated:** 2026-06-26  
**Build:** `./build/bin/cursor-agent`  
**Scope:** Semantic verification of every search audit query  
**Method:** For each query, the selected file was compared against the canonical declaration/definition in source by direct `grep` investigation.

Exit code 0 is a necessary condition, not a sufficient one.  
This report answers: *did the engine pick the right file?*

---

## Summary

| Query | Selected File | Correct? | Verdict |
|---|---|---|---|
| `find class ReplayService` | `include/services/replay_service.h` | ✓ | **PASS** |
| `find struct ToolResult` | `scenarios/regressions/who_uses_toolresult.json` | ✗ | **FAIL** |
| `who calls ReplayService` | `include/app/command_router.h` | ~ | **PARTIAL** |
| `where is CommandRouter referenced` | `include/app/command_router.h` | ~ | **PARTIAL** |
| `where is SessionState used` | `include/agent.h` | ✓ | **PASS** |
| `what owns CommandRouter` | *(no tools run — General Chat)* | ✗ | **FAIL** |
| `what depends on ExecutionEngine` | *(no tools run — General Chat)* | ✗ | **FAIL** |
| `where is configuration loaded` | `docs/release/release_readiness_report.md` | ✗ | **FAIL** |
| `how does startup flow` | `README.md` | ~ | **PARTIAL** |
| `what is the git diff` | `src/services/capability_registry.cpp` | ✗ | **FAIL** |
| `git status` | *(git log executed, no file selected)* | ~ | **PARTIAL** |

**Result: 2 PASS · 5 FAIL · 4 PARTIAL**  
The engine is **not** semantically production-ready for architectural queries.

---

## Per-Query Analysis

---

### 1. `find class ReplayService`

| Field | Value |
|---|---|
| Tool sequence | `find ReplayService` → `read include/services/replay_service.h` |
| Selected file | `include/services/replay_service.h` |
| Correct declaration | `include/services/replay_service.h:30: class ReplayService {` |
| Better answer exists? | No |

**Verdict: PASS.**  
The filename hit resolved directly to the canonical header. Tool sequence was minimal (1 find + 1 read). Confidence 0.675 is appropriate.

---

### 2. `find struct ToolResult`

| Field | Value |
|---|---|
| Tool sequence | `find ToolResult` → `read scenarios/regressions/who_uses_toolresult.json` |
| Selected file | `scenarios/regressions/who_uses_toolresult.json` |
| Correct declaration | `include/services/execution_engine.h:18: struct ToolResult {` |
| Better answer exists? | **Yes — `include/services/execution_engine.h`** |

**Root cause:** The filename lookup matched `who_uses_toolresult.json` because the token `toolresult` appears in the filename. The engine ranked the JSON fixture first because filename hits outrank content hits in the current pipeline. The JSON file contains only a regression test prompt — not the struct declaration.

**Verdict: FAIL.** Confirmed: a query for a struct declaration resolved to a regression fixture. The struct `ToolResult` is declared in `include/services/execution_engine.h` and used in `src/app/command_router.cpp`, `src/services/execution_engine.cpp`, and `src/diagnostics/diagnostics.cpp`. The correct answer is `execution_engine.h`.

**Required fix:** Filename ranking must de-prioritize files in `scenarios/` and `data/` directories when the query intent contains `find struct` or `find class`. Declaration-intent queries should prefer `include/` and `src/` paths over fixture paths.

---

### 3. `who calls ReplayService`

| Field | Value |
|---|---|
| Tool sequence | `references ReplayService` (78 lines) → `read include/app/command_router.h` |
| Selected file | `include/app/command_router.h` |
| Correct callers | `src/main.cpp` (instantiates at lines 168, 603); `src/app/command_router.cpp` (accepts as parameter line 74) |
| Better answer exists? | **Yes — `src/main.cpp` and `src/app/command_router.cpp`** |

**Root cause:** The reference search found 78 matching lines. The ranking then selected the *header* rather than the *implementation files* that actually instantiate and call `ReplayService`. A forward declaration in a header is not a caller.

**Verdict: PARTIAL.** The result is topically adjacent but semantically wrong for a "who *calls*" query.

---

### 4. `where is CommandRouter referenced`

| Field | Value |
|---|---|
| Tool sequence | `references CommandRouter` (372 lines) → `read include/app/command_router.h` |
| Selected file | `include/app/command_router.h` |
| Correct reference sites | `src/main.cpp:17` (include), `src/main.cpp:612` (instantiation) |
| Better answer exists? | **Yes — `src/main.cpp`** |

**Root cause:** With 372 reference hits, the ranker chose the file with the highest occurrence density — which is the defining header itself. The header is the origin, not a reference site.

**Verdict: PARTIAL.** Returning the defining header for a "where is X referenced" query is semantically wrong. The question asks for use sites, not the definition.

---

### 5. `where is SessionState used`

| Field | Value |
|---|---|
| Tool sequence | `references SessionState` (75 lines) → `read include/agent.h` |
| Selected file | `include/agent.h` |
| Correct answer | `include/agent.h:43: SessionState state_;` — Agent owns SessionState as a member |
| Better answer exists? | No — `agent.h` is the primary ownership site |

**Verdict: PASS.**  
`agent.h` is the primary ownership site. The struct is defined in `include/core/session_state.h` but the agent *owns* it as `state_`. For a "where is it used" query, the ownership site is the correct answer.

---

### 6. `what owns CommandRouter`

| Field | Value |
|---|---|
| Tool sequence | *(no tools executed)* |
| Goal classified as | `General Chat` |
| Confidence | 0.0 |
| Correct answer | `src/main.cpp:612` — `Core::CommandRouter router(agent, ui)` instantiated in `main()` |
| Better answer exists? | **Yes — `src/main.cpp`** |

**Root cause:** The intent classifier routed "what owns CommandRouter" as `General Chat` rather than `Repository Investigation`. The query contains the name of a concrete class, but the ownership keyword "owns" did not trigger an investigation branch.

**Verdict: FAIL.** Zero tools executed for a concrete ownership question. Classifier failure.

---

### 7. `what depends on ExecutionEngine`

| Field | Value |
|---|---|
| Tool sequence | *(no tools executed)* |
| Goal classified as | `General Chat` |
| Confidence | 0.0 |
| Correct answer | `src/app/command_router.cpp` (instantiates `Services::ExecutionEngine engine` at line 149); `src/diagnostics/diagnostics.cpp` (includes `execution_engine.h`) |
| Better answer exists? | **Yes — references search on `ExecutionEngine`** |

**Root cause:** "What depends on X" was not recognized as a repository investigation query. Dependency queries (`what depends on`, `what uses`, `what includes`) are missing from the classifier's investigation-trigger vocabulary.

**Verdict: FAIL.** Zero tools executed for a dependency question. Classifier failure.

---

### 8. `where is configuration loaded`

| Field | Value |
|---|---|
| Tool sequence | `find configuration_loaded` (no results) → `grep configuration_loaded` (2 matches in `.md` files) |
| Selected file | `docs/release/release_readiness_report.md` |
| Correct answer | No single function; configuration is distributed: `sandbox_service.cpp` loads `data/sandbox_config.json`; `auth_service.cpp` loads `data/auth_config.json` |
| Better answer exists? | **Yes — `src/services/sandbox_service.cpp`, `src/services/auth_service.cpp`** |

**Root cause:** The compound phrase "configuration loaded" was normalized to the token `configuration_loaded`, which does not exist as a filename or symbol. Grep fallback matched the string only inside `.md` documentation files. The engine correctly returned `insufficient_evidence` but still presented a `.md` report as the examined file, which is misleading.

**Verdict: FAIL.** Three compounded failures: phrase normalization, grep fallback matching docs over source, and misleading file attribution despite an `insufficient_evidence` outcome.

---

### 9. `how does startup flow`

| Field | Value |
|---|---|
| Tool sequence | `discovery` (C++/CMake) → `read README.md CMakeLists.txt AGENTS.md` |
| Selected file | `README.md` (reported) |
| Correct answer | `src/main.cpp:48` — `int main()` is the startup entry; `ReplayService` init → `CommandRouter` construction → query loop |
| Better answer exists? | **Yes — `src/main.cpp`** |

**Root cause:** The query was classified as `Codebase Overview`, which triggers the discovery/README path. A flow question should trigger investigation of `main.cpp` and the call graph instead.

**Verdict: PARTIAL.** README is a starting point but semantically insufficient for a flow question.

---

### 10. `what is the git diff`

| Field | Value |
|---|---|
| Tool sequence | `find git_diff` (no results) → `grep git_diff` (20 matches) → `read src/services/capability_registry.cpp` |
| Selected file | `src/services/capability_registry.cpp` |
| Correct answer | This should execute `git diff` as a live command, not search for the string "git diff" |
| Better answer exists? | **Yes — direct `git diff` execution** |

**Root cause:** The bare-string triggers for git commands (`lower.find("git diff")` at `command_router.cpp:1523`) do not match the natural language form "what is the git diff". The engine fell through to codebase investigation, found 20 occurrences of the string "git diff" in capability documentation, and ranked `capability_registry.cpp` as the top result.

**Verdict: FAIL.** A live git operation was misclassified as a codebase search.

---

### 11. `git status`

| Field | Value |
|---|---|
| Tool sequence | `git log --oneline -10` |
| Selected file | *(no file; git log output returned)* |
| Correct answer | `git status` should execute `git status`, not `git log` |

**Root cause:** The bare query "git status" routed to `Commit History` and executed `git log` rather than `git status`. The outcome was reported as success at confidence 0.5, masking the wrong-subcommand error.

**Verdict: PARTIAL.** A git command was executed, but it was the wrong one.

---

## Confirmed Bugs

| ID | Query | Failure Type | Root Cause | Correct Answer |
|---|---|---|---|---|
| **B-01** | `find struct ToolResult` | Wrong file ranked first | Fixture filename match outranks source content match; `scenarios/` ranked above `include/` | `include/services/execution_engine.h` |
| **B-02** | `what owns CommandRouter` | Classifier miss | "owns" keyword not in investigation trigger vocabulary | `src/main.cpp:612` |
| **B-03** | `what depends on ExecutionEngine` | Classifier miss | "depends on" not in investigation trigger vocabulary | `src/app/command_router.cpp` |
| **B-04** | `where is configuration loaded` | Phrase normalization + grep fallback to docs | Compound phrase normalized to non-existent symbol; grep matched `.md` not `.cpp` | `src/services/sandbox_service.cpp`, `src/services/auth_service.cpp` |
| **B-05** | `what is the git diff` | Intent misclassification | Natural language wrapper prevents bare-string match that would trigger live git diff | Live `git diff` execution |
| **B-06** | `who calls ReplayService` | Reference ranking selects declarer over caller | High-occurrence header ranked above implementation files that instantiate the service | `src/main.cpp`, `src/app/command_router.cpp` |
| **B-07** | `where is CommandRouter referenced` | Reference ranking selects origin over reference sites | Defining header ranked above `src/main.cpp` which contains the actual instantiation | `src/main.cpp:612` |
| **B-08** | `git status` | Wrong git subcommand | Bare "git status" routes to `Commit History`, executing `git log` not `git status` | Live `git status` output |

---

## Classification by Failure Mode

### Classifier Failures (B-02, B-03, B-05, B-08)
The intent classifier does not recognize natural language ownership (`what owns`), dependency (`what depends on`), or natural language git operations (`what is the git diff`, bare `git status`) as investigation queries. These queries arrive with zero tools executed and confidence 0.0. **Fix:** expand classifier vocabulary with ownership and dependency intent patterns.

### Ranking Failures (B-01, B-06, B-07)
The file ranker selects the wrong file from a valid result set. In B-01, a fixture file ranks above a source header. In B-06/B-07, the defining file ranks above use/call sites. **Fix:** (1) penalize `scenarios/`, `data/`, `docs/` paths for declaration intents; (2) for reference queries, rank `.cpp` files above `.h` files.

### Search Phrase Normalization Failure (B-04)
The compound phrase "configuration loaded" is joined to `configuration_loaded`, a token that does not exist as a symbol or filename. **Fix:** tokenize multi-word phrases and search for each token independently.

---

## Metrics

| Metric | Value |
|---|---|
| Queries audited | 11 |
| Semantically correct (PASS) | 2 (18%) |
| Partially correct (PARTIAL) | 4 (36%) |
| Semantically wrong (FAIL) | 5 (45%) |
| Confirmed bugs | 8 |
| Classifier failures | 4 |
| Ranking failures | 3 |
| Phrase normalization failures | 1 |
| Queries with zero tools executed | 2 |
| Grep fallback rate | 3/11 (27%) |

---

## Release Readiness Verdict

> **The search engine is NOT production-ready for architectural queries.**

Exit code 0 was achieved on all 11 queries. Semantic correctness was achieved on only **2 of 11 (18%)**.

The engine is reliable for named class lookup when the class name appears uniquely in a header filename, and for session state ownership when the owning class has a clear member field.

The engine fails for: ownership queries, dependency queries, natural language git operations, struct lookup when a fixture file contains the name, reference queries where the declaring header outranks calling files, and compound-phrase configuration queries.

The grep fallback rate is 27%, against a 25%-reduction target that has not yet been met.

---

## Recommended Fixes (Priority Order)

1. **B-02 / B-03 — Classifier vocabulary:** Add `what owns`, `who owns`, `what depends on`, `what uses`, `what includes` to investigation intent triggers. Zero-cost classification fixes.
2. **B-01 — Path penalty:** Penalize `scenarios/`, `data/`, `docs/`, `build/` paths when query intent is `find struct` or `find class`. Prefer `include/` and `src/`.
3. **B-06 / B-07 — Reference ranking:** For caller/reference queries, rank `.cpp` files above `.h` files. A forward declaration is not a call site.
4. **B-05 / B-08 — Git intent matching:** Extend git matchers to handle natural language wrappers: `what is the git diff`, `show me the status`, `what changed`.
5. **B-04 — Phrase normalization:** Tokenize compound phrases. Search tokens independently. Do not join with underscores unless the compound identifier is verbatim in the codebase.

---

*Every finding in this report is backed by direct `grep` against `include/` and `src/` source files. No inferences were made from documentation.*
