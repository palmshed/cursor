---
---

# Release Readiness Report

**Date:** 2026-06-26  
**Build:** `./build/bin/cursor-agent`  
**Cycle:** Code Search Excellence -- Phase 1 Audit  
**Checklist:** `docs/release/release_checklist.md`

---

## Decision

> **BLOCKED**

The engine is operationally correct. It is not yet product-correct.  
Gate 3 (Search Correctness) fails. Release is not authorized.

---

## Gate 1 -- Build: PASS

| Check | Result |
|---|---|
| Clean build | ✓ 0 errors |
| Self-test | ✓ 8/8 passed |
| Doctor | ✓ 15/15 passed, 0 failed |
| Benchmark suite | ✓ 14/14 scenarios |

---

## Gate 2 -- Execution: PASS

| Check | Result |
|---|---|
| 19/19 audit commands exit 0 | ✓ |
| Tool routing (9/9 direct commands) | ✓ |
| `--capabilities` exits 0 | ✓ (fixed this cycle) |
| Checkpoint contamination | ✓ absent (fixed this cycle) |
| cmake / ctest routing | ✓ correct (fixed this cycle) |

This was the conclusion of the previous readiness report.  
It was necessary but insufficient.

---

## Gate 3 -- Search Correctness: FAIL

*Source: `docs/telemetry/search_correctness_report.md`*

| Metric | Result | Target |
|---|---|---|
| Queries audited | 11 | 11 |
| Semantically correct (PASS) | 2 (18%) | ≥ 90% |
| Partially correct (PARTIAL) | 4 (36%) | -- |
| Semantically wrong (FAIL) | 5 (45%) | 0 Critical |
| Confirmed bugs | 8 | 0 |
| Grep fallback rate | 27% | ≤ 20% |

### Confirmed semantic failures

| ID | Query | Failure | Severity |
|---|---|---|---|
| B-01 | `find struct ToolResult` | Regression fixture ranked above source header | **Critical** |
| B-02 | `what owns CommandRouter` | 0 tools executed; classified as General Chat | **Critical** |
| B-03 | `what depends on ExecutionEngine` | 0 tools executed; classified as General Chat | **Critical** |
| B-04 | `where is configuration loaded` | Phrase normalized to non-existent symbol; grep matched `.md` not `.cpp` | High |
| B-05 | `what is the git diff` | Live git operation misclassified as codebase search | High |
| B-06 | `who calls ReplayService` | Declaring header ranked above calling `.cpp` files | High |
| B-07 | `where is CommandRouter referenced` | Defining header ranked above `main.cpp` instantiation | High |
| B-08 | `git status` | `git log` executed instead of `git status` | Medium |

### Three failure modes

**Classifier failures (4 bugs -- B-02, B-03, B-05, B-08)**  
The intent classifier does not recognize ownership (`what owns`), dependency (`what depends on`), or natural-language git operations as investigation queries. Two queries returned zero tools executed and confidence 0.0.

**Ranking failures (3 bugs -- B-01, B-06, B-07)**  
The file ranker selects the declaring/defining file over the calling/using file for reference queries, and selects fixture files over source files for declaration queries.

**Phrase normalization failure (1 bug -- B-04)**  
Compound phrases are joined with underscores, producing symbols that do not exist in the codebase. Grep fallback then matches documentation files instead of source.

---

## Gate 4 -- UX: PASS (conditional)

| Check | Result |
|---|---|
| Progress timeline visible | ✓ for investigation queries |
| Evidence visible in JSON | ✓ `files_examined` populated when tools run |
| Insufficient evidence distinguishable | ✓ `outcome: insufficient_evidence` correct |
| Errors distinguishable | ✓ stderr separate from stdout |

*Condition: UX is acceptable for queries that reach the investigation path. Queries classified as General Chat show no progress UI at all -- this is acceptable only while classifier failures remain open.*

---

## Gate 5 -- Telemetry: PASS

| Check | Result |
|---|---|
| Dashboard current | ✓ 483 sessions, 1408 events |
| Search correctness report | ✓ `docs/telemetry/search_correctness_report.md` |
| Calibration reviewed | ✓ confidence bands show 97.4% success rate in 0.6–0.8 band |
| Failure topology | ✓ `docs/telemetry/failure_topology.md` |

---

## What Changed vs. Previous Report

The previous readiness report concluded **PASS** based on 19/19 audit commands returning exit code 0.

This report supersedes that conclusion.

The previous audit measured **operational correctness**:
- Does the binary execute?
- Do commands route to the right tool?
- Do exit codes match expectations?

This audit measured **product correctness**:
- Does the engine return the right file for a given query?
- Does it recognize the query intent?
- Does it rank results correctly?

A code-search agent that consistently returns wrong files is not release-ready even if every command exits with code 0. Exit code 0 is a necessary condition, not a sufficient one.

---

## Remaining Blockers

### Critical (must fix before release)

1. **Classifier vocabulary** -- add `what owns`, `who owns`, `what depends on`, `what uses`, `what includes` to the investigation intent trigger vocabulary.  
   *Files:* `src/app/command_router.cpp` intent classification logic.

2. **Path penalty in filename ranker** -- penalize `scenarios/`, `data/`, `docs/`, `build/` paths for `find struct`/`find class` intent queries. Source directories `include/` and `src/` must rank first for declaration intents.  
   *Files:* filename ranking logic in `find_service.cpp` or equivalent.

3. **Reference query ranking** -- for caller/reference queries, rank `.cpp` files above `.h` files. A forward declaration or defining header is not a call site.  
   *Files:* ranking logic in `find_service.cpp` or `execution_engine.cpp`.

### High (should fix before release)

4. **Git intent matching** -- extend git matchers to handle natural language wrappers: `what is the git diff`, `show me the status`, `what changed`.  
   *Files:* `command_router.cpp:1523` git intent branch.

5. **Phrase normalization** -- tokenize multi-word phrases and search each token independently. Do not join with underscores unless the compound identifier appears verbatim in the codebase.  
   *Files:* symbol lookup / find_service query normalization.

### Medium

6. **git status vs git log disambiguation** -- the bare query `git status` should execute `git status`, not `git log`.  
   *Files:* `command_router.cpp:1425` `is_git_status_query()`.

---

## Recommendation

Fix blockers in this order:

1. Classifier vocabulary (B-02, B-03) -- highest impact, lowest effort. These are vocabulary additions, not architectural changes.
2. Path penalty (B-01) -- prevents the most visible regression fixture bug.
3. Reference ranking (B-06, B-07) -- fixes the most common query pattern for architectural investigation.
4. Git intent (B-05, B-08) -- relatively contained, high user-facing impact.
5. Phrase normalization (B-04) -- most complex fix; may require tokenizer changes.

After fixes: re-run `docs/telemetry/search_correctness_report.md` against the full benchmark set. Target ≥ 90% semantic correctness and ≤ 20% grep fallback rate before re-evaluating release readiness.

---

*This report reflects the state of the repository on 2026-06-26.  
Every finding is backed by direct source investigation. No inferences from documentation.*
