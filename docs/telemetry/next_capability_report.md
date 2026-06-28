---
---

# Telemetry & Capability-Gap Report: Reference & Symbol Search

**Date:** 2026-06-26  
**Status:** Design Proposal (No Code)  
**Context:** Post-Directory-Aware Find Cycle  

---

## Executive Summary

With the successful deployment of the **Directory-Aware Find** ranking engine, all 37 regression and verification scenarios are passing. Telemetry confirms that first-order retrieval failures--such as the agent failing to find files by name due to malformed regex patterns--have been resolved. 

However, a major structural capability gap remains: **The `ExecutionEngine` is currently blind to symbol declarations and cross-file references as active tools.** 

Although the codebase contains a robust backend implementation for parsing symbols and tracing references in `Services::SymbolService` (used by direct developer slash commands like `/symbols` and `/references`), these capabilities are not exposed to the agent's autonomous planning loop. This report evaluates exposing **Reference Search** and **Symbol Search** to the agent to determine the smallest, highest-value retrieval extension justified by telemetry.

---

## 1. Reference Search (Highest Value)

Reference Search allows the agent to deterministically query where a symbol (class, struct, variable, or function) is called, used, or instantiated across the codebase (e.g., `"who calls ReplayService"`, `"where is CommandRouter referenced"`).

### Telemetry Signals & Justification
* **Grep Fallback Noise:** Generic or common symbol names (e.g., `replay`, `command`, `state`) produce massive grep results (e.g., 173 matches for `replay`). This floods the agent's context window, causing token truncation or forcing the agent to spend its iteration budget reading comments and unrelated strings.
* **Instantiation Tracing Failures:** Telemetry shows that when the agent seeks caller context (e.g., "where is the class instantiated or configured?"), it has to guess caller files or perform broad directory-wide scans, which leads to `InsufficientEvidence` due to exhaustion of the loop budget.

### Expected Failure Classes Reduced
* `context_window_dilution` (reducing grep noise in common symbols).
* `missed_instantiation_context` (failing to find where a component is registered or configured).
* `iteration_budget_exhaustion` (spending iterations scanning file paths instead of calling a deterministic reference tool).

### Implementation Complexity: Low-to-Medium
* **Existing Backend:** `SymbolService::find_references` is already implemented in [symbol_service.cpp](file:///Users/bniladridas/Desktop/cursor/src/services/symbol_service.cpp#L113-L148). It scans source files, filters out definitions (class/struct/fn headers), and extracts relative paths, lines, and context.
* **Exposing the Tool:** We only need to expose a `references` tool to `ExecutionEngine::select_next_tool()` and map natural language queries with reference intent (e.g., "who uses", "who calls", "where is X referenced") to this tool.

### Validation Strategy
* **Regression Scenarios:** Create 3 scenarios in `scenarios/regressions/`:
  1. `scenarios/regressions/reference_search_replay.json` (Query: "who calls ReplayService")
  2. `scenarios/regressions/reference_search_router.json` (Query: "where is CommandRouter referenced")
* **Verification Assertions:**
  * Assert `outcome` is `success`.
  * Assert `tools_used` includes `references` and `read`.
  * Assert `files_examined` includes the caller files (`agent.cpp` or `main.cpp`).

### Production Success Metrics
* `reference_tool_hits`: Frequency of the `references` tool being successfully invoked.
* `average_tokens_saved_per_search`: Reduction in returned characters compared to a raw `grep` fallback.
* `caller_resolution_rate`: Percentage of caller-tracing queries resolved within 3 iterations.

---

## 2. Symbol Search (Symbol Indexing)

Symbol Search allows the agent to directly find the file and line number declaring a class, struct, function, or enum (e.g., `"find class ReplayService"`, `"find function classify_goal"`).

### Telemetry Signals & Justification
* **Implementation Scan Overhead:** Currently, when the agent looks for class declarations, it may match the implementation file first (e.g., `.cpp` instead of `.h`) due to `impl_query` boosts, requiring the agent to read several files to locate member signatures.
* **Query Classification Routing:** Keywords like `class`, `struct`, or `function` are parsed into generic terms in the heuristic classifier, occasionally routing to generic search fallbacks instead of symbol extraction.

### Expected Failure Classes Reduced
* `declaration_vs_implementation_confusion` (identifying headers vs. sources).
* `redundant_source_reads` (reading multiple files just to locate class declarations).

### Implementation Complexity: Low
* **Existing Backend:** `SymbolService::find_symbols` is already implemented in [symbol_service.cpp](file:///Users/bniladridas/Desktop/cursor/src/services/symbol_service.cpp#L62-L111).
* **Current State:** Note that `directory_aware_find` already has a symbol scanning cascade (lines 123-203 in [find_service.cpp](file:///Users/bniladridas/Desktop/cursor/src/services/find_service.cpp#L123-L203)) which scans files and boosts their ranking score (by +15 to +18) if they contain class/struct/fn symbol matches. Thus, Symbol Search is already **partially integrated** into the ranking system of the `find` tool.

### Validation Strategy
* **Regression Scenarios:** Create scenarios in `scenarios/repository/`:
  * `find_class_replay_service.json` (Query: "find class ReplayService")
* **Verification Assertions:**
  * Assert `outcome` is `success`.
  * Assert `files_examined` contains `include/services/replay_service.h` (or exact declaring header).

### Production Success Metrics
* `symbol_precision`: Rate at which the top candidate returned by the `find` tool is the actual declaring header file.

---

## 3. Comparison & Decision Matrix

| Metric / Dimension | Reference Search | Symbol Search |
|--------------------|:----------------:|:-------------:|
| **Implementation Complexity** | Low-to-Medium (Expose existing service) | Low (Leverage/expose existing service) |
| **Telemetry Evidence Support** | **High** (Addresses noisy grep and caller queries) | **Medium** (Scoring cascades already do symbol checks) |
| **Token / Context Efficiency** | **High** (Filters call-sites, avoids grep flood) | **Medium** (Improves precision on header files) |
| **Duplicate Work Prevention** | **No overlapping capability** exists in agent tools | **Overlaps** with `find` tool's symbol scoring |
| **Priority** | **1 (Highest)** | **2 (Lower)** |

---

## Conclusion & Recommendations

> [!IMPORTANT]
> **Reference Search** is the recommended next capability to expose to the agent.

1. **Why Reference Search First?** 
   While Symbol Search is useful, the current `directory_aware_find` tool already implements symbol declaration scanning to boost scoring. In contrast, the agent has **no way to trace callers/uses** other than falling back to noisy, broad greps. Reference Search provides a completely new, deterministic dimension of codebase navigation.
2. **Deterministic Focus:**
   Both options remain strictly deterministic, telemetry-measurable, and avoid complex indexing systems (like Tree-Sitter or LLM semantic embeddings) or multi-agent orchestration architectures.
3. **Execution Plan:**
   In the next cycle, we should expose the existing `SymbolService::find_references` as a first-class agent tool named `references` within `execution_engine.cpp` and update `select_next_tool` to route to it when caller/user queries are detected.
