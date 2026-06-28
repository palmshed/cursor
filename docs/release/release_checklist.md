---
---

# Release Checklist

This checklist gates every release of `cursor-agent`.

Gates are ordered. A failure at any gate blocks everything below it.
**Search Correctness is a mandatory gate** -- this is a code-search product.
Operational correctness (exit codes, routing) is necessary but not sufficient.

---

## Gate 1 -- Build

| # | Check | How to verify | Status |
|---|---|---|---|
| 1.1 | Clean build | `cmake --build build/ 2>&1 \| grep -c error` → 0 | ☐ |
| 1.2 | Self-test passes | `cursor-agent --self-test` → 8/8 passed | ☐ |
| 1.3 | Doctor passes | `cursor-agent --doctor` → 0 failed | ☐ |
| 1.4 | Benchmark suite passes | `cursor-agent --benchmark` → 14/14 | ☐ |

---

## Gate 2 -- Execution

| # | Check | How to verify | Status |
|---|---|---|---|
| 2.1 | Commands execute correctly | `audit_runner.py` all exit 0 | ☐ |
| 2.2 | Tool routing verified | `--self-test` direct command routing 9/9 | ☐ |
| 2.3 | Exit codes verified | `--capabilities` exits 0; `--version` exits 0 | ☐ |
| 2.4 | Checkpoint contamination absent | Search results from `include/` and `src/` only | ☐ |
| 2.5 | cmake / ctest routing correct | `cursor-agent "build the project"` no stderr | ☐ |

---

## Gate 3 -- Search Correctness  *(mandatory product gate)*

**Definition:** A query is semantically correct if and only if the engine returns  
the canonical declaration, definition, or use site -- not a fixture, document, or  
forward-declaration header -- for the queried symbol or intent.

Exit code 0 does **not** satisfy this gate.

| # | Check | Target | Status |
|---|---|---|---|
| 3.1 | Search correctness audit run | All queries in benchmark set audited | ☐ |
| 3.2 | No Critical semantic failures | 0 queries with `goal_type: General Chat` for named-symbol inputs | ☐ |
| 3.3 | Declaration queries resolve to source | `find struct/class X` → `include/` or `src/`, never `scenarios/` or `docs/` | ☐ |
| 3.4 | Reference queries resolve to call sites | `who calls X` / `where is X referenced` → `.cpp` caller, not defining `.h` | ☐ |
| 3.5 | Ownership queries trigger investigation | `what owns X` / `what depends on X` → references search, not General Chat | ☐ |
| 3.6 | Git intent queries execute live commands | `what is the git diff` → `git diff`, not codebase grep | ☐ |
| 3.7 | Phrase queries tokenize correctly | Multi-word phrases searched as tokens, not underscore-joined symbols | ☐ |
| 3.8 | Grep fallback rate below target | `grep_fallback_rate` ≤ 20% across benchmark set | ☐ |
| 3.9 | Semantic correctness ≥ 90% | Pass + Partial-correct ≥ 90% of audited queries | ☐ |

*Source: `docs/telemetry/search_correctness_report.md`*

---

## Gate 4 -- UX

| # | Check | How to verify | Status |
|---|---|---|---|
| 4.1 | Progress timeline visible | Every investigation query shows stage headers before JSON | ☐ |
| 4.2 | Evidence visible | `files_examined` populated in JSON output | ☐ |
| 4.3 | Insufficient evidence distinguishable | `outcome: insufficient_evidence` does not report exit 1 | ☐ |
| 4.4 | Errors distinguishable from empty results | Stderr captures tool errors; stdout clean | ☐ |

---

## Gate 5 -- Telemetry

| # | Check | How to verify | Status |
|---|---|---|---|
| 5.1 | Dashboard metrics current | `cursor-agent --dashboard` reflects recent sessions | ☐ |
| 5.2 | Failure topology updated | `docs/telemetry/failure_topology.md` reflects current failure classes | ☐ |
| 5.3 | Search correctness report current | `docs/telemetry/search_correctness_report.md` dated within this cycle | ☐ |
| 5.4 | Calibration report reviewed | `cursor-agent --calibrate` confidence bands reviewed | ☐ |

---

## Decision

| Field | Value |
|---|---|
| **Gate 1 -- Build** | |
| **Gate 2 -- Execution** | |
| **Gate 3 -- Search Correctness** | |
| **Gate 4 -- UX** | |
| **Gate 5 -- Telemetry** | |
| **Overall decision** | ☐ READY  ☐ BLOCKED |
| **Blocking items** | |
| **Release authorized by** | |
| **Date** | |

---

## Release Pipeline

```
Gate 1: Build
    ↓  (fail → fix build)
Gate 2: Execution
    ↓  (fail → fix routing / exit codes)
Gate 3: Search Correctness       ← primary product gate
    ↓  (fail → fix classifier / ranking / normalization)
Gate 4: UX
    ↓  (fail → fix progress / evidence visibility)
Gate 5: Telemetry
    ↓  (fail → update reports)
Release
```

A gate may only be skipped with an explicit written waiver documenting:
- which checks are skipped
- why they are not blocking for this specific release
- what telemetry will be monitored post-release

No waivers are valid for Gate 3 items 3.2, 3.3, 3.4, or 3.5.
Those are unconditional requirements for a code-search product.
