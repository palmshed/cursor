---
---

# Reference Search: Implementation Report

**Date:** 2026-06-26  
**Phase:** Complete (Reference Search capability exposed, validated via 8/8 regressions passing)

---

## 1. Problem Statement

Previously, the agent retrieval layer lacked the ability to track symbol usage/calls (Reference Search). Queries asking about caller hierarchies (e.g., `"who calls ReplayService"` or `"where is SessionState used"`) had to fall back to broad keyword searches (`grep`) or generic searches (`find`). This resulted in:
- High token overhead from reading unrelated grep matches.
- Inefficiency: broad search queries could return dozens of matches across many files, exhausting the iteration budget.
- Higher rates of `InsufficientEvidence` outcomes.

### Acceptance Queries & Verification Target
The target queries were:
- `who calls ReplayService`
- `where is CommandRouter referenced`
- `who uses ToolResult`
- `where is SessionState used`

---

## 2. Root Cause

1. **Symbol Search was a refinement, Reference Search was a new capability.**
   - While `SymbolService::find_references` existed in the backend (scanning code occurrences and finding where symbols were referenced), it was never exposed to the agent (`ExecutionEngine` and the tool router layer).
2. **Missing classification keywords:**
   - Classification rules in `classify_goal` were unaware of common referencing verbs ("calls", "uses", etc.) without explicit suffix matches, causing them to fall back to `GeneralChat` which ran no tools at all.

---

## 3. Implementation

### 3.1 Exposing `references` Tool to Command Router
We exposed `references` as a tool call. It runs `SymbolService::find_references(dir, symbol)`.
- It registers caller file paths to `last_find_results`.
- This enables a subsequent empty-argument `read` tool call to read the exact caller files.
- Files affected: `src/app/command_router.cpp` (and mock updates in diagnostics/tests).

### 3.2 Query Classification and Execution Engine Integration
- **Classification:** Updated `classify_goal` to map keywords `call`, `calls`, `called`, `use`, `uses`, `used`, `using`, `reference`, `references`, `referenced` to `CodebaseQuery`.
- **Query Detection:** Added `is_reference_query` using substring matches to route the query straight to the `references` tool rather than file search (`find`) or `grep`.
- **Completion Gating:** Updated `check_completion` for codebase queries. A reference query is complete when `references:results` and `read` are in the evidence facts, or when `references:noresults` is verified.
- **Evidence Gating:** Mapped `references` tool success to the `EvidenceClass::FileSearch` class. Bypassed required evidence check if `references` returns no results (ensuring correct completion outcomes for non-referenced symbols).

### 3.3 Telemetry Metrics
Added two telemetry metrics to `RetrievalMetrics` (`include/core/metrics.h`, `src/services/replay_service.cpp`):
1. `reference_tool_hits` (int): Number of times the `references` tool was invoked.
2. `caller_resolution_rate` (double): Set to `1.0` if a reference query is successfully resolved (`check_completion` passes) with `references` tool usage and *without* falling back to broad `grep` calls. Otherwise, `0.0`.

---

## 4. Verification & Metrics

All 8/8 regression scenarios (including the 4 new regression files) passed.

### Before vs. After Analysis

| Query | Pre-Fix Sequence | Pre-Fix Outcome | Post-Fix Sequence | Post-Fix Outcome | caller_resolution_rate |
|---|---|---|---|---|---|
| `who calls ReplayService` | None (GeneralChat) | Failure / No Tools | `references ReplayService` → `read` | Success | **1.0** |
| `where is CommandRouter referenced` | `find CommandRouter` | Detour (read header) | `references CommandRouter` → `read` | Success | **1.0** |
| `who uses ToolResult` | None (GeneralChat) | Failure / No Tools | `references ToolResult` → `read` | Success | **1.0** |
| `where is SessionState used` | `find sessionstate` | Detour (read cpp) | `references SessionState` → `read` | Success | **1.0** |

---

## 5. Active Constraints
- No AST-indexing, semantic search, or tree-sitter layers were added.
- No LLM-based ranking was introduced.
- Strict retrieval scope was maintained.
