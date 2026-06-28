---
---

# Phase 2: Confidence Model Design

## Current Model Problems (from Phase 1)

1. **Flat averaging** -- every tool step is equally weighted regardless of evidence type
2. **`after_read(1, true)` = 0.55** -- the most informative step contributes the lowest score
3. **Recovery always 0.50** -- regardless of whether it found new evidence
4. **Two output values** -- 0.617 and 0.562 are the only confidence levels produced for successful queries
5. **Recovery gate fires on every query** -- threshold 0.7 with average 0.59

---

## New Architecture

### Core Idea

Group tool outputs into **evidence categories**. Take the **maximum score per category** (redundant tools in the same category don't add confidence). Apply category weights. Add a **convergence bonus** when independent categories agree.

This replaces `ConfidenceService::combine()`. The individual per-tool scoring functions (`after_search`, `after_read`, etc.) remain unchanged -- they still produce per-tool scores -- but `combine()` no longer averages them.

### Evidence Categories

| # | Category | Tools Included | What It Measures |
|---|---|---|---|
| 1 | `search` | find, grep, references | Located relevant files or symbols |
| 2 | `read` | read | Read file content |
| 3 | `discovery` | discovery | Project structure understanding |
| 4 | `verification` | cmake, ctest | Build and test results |
| 5 | `git` | git | Commit history |
| 6 | `ci` | gh | CI/Workflow data |

### Category Scoring

Each category score is the **maximum** score from any tool in that category. Running `read` five times produces the same category score as running it once -- only the best read matters.

| Category | Condition | Score | Rationale |
|---|---|---|---|
| `search` | At least one of find/grep/references returned results | 0.65 | Evidence located, but unconfirmed |
| `search` | Two of (find, grep, references) returned results for same target | 0.80 | Independent search methods agree |
| `search` | All three returned results for same target | 0.92 | Strong multi-method agreement |
| `read` | At least one file read but no search confirmation | 0.55 | Read happened but context unclear |
| `read` | Read AND (find OR grep OR references) returned results | 0.80 | Read confirmed by search -- highest confidence per-read |
| `read` | 3+ files read on related targets with search confirmation | 0.88 | Breadth + confirmation |
| `discovery` | 0-1 of 4 factors | 0.20 | Minimal project understanding |
| `discovery` | 2 of 4 factors | 0.50 | Partial understanding |
| `discovery` | 3 of 4 factors | 0.70 | Good understanding |
| `discovery` | All 4 factors | 0.85 | Full project map |
| `verification` | Build failed | 0.15 | Failure is a strong negative signal |
| `verification` | Tests failed | 0.30 | Partial failure |
| `verification` | Build passed | 0.80 | Code compiles |
| `verification` | Build + tests passed | 0.93 | Full verification |
| `git` | git returned results | 0.75 | History evidence |
| `ci` | CI evidence complete | 0.80 | CI data available |

Note: `search` cross-checks across find/grep/references require that the **same target term** appears in more than one tool. The execution engine already tracks the query term per tool -- `ToolInvocation.query` / `ToolCall.args`.

### Category Weights

Weights express how much each category contributes to the final score. They are normalized to sum to 1.0.

| Category | Raw Weight | Normalized | Why |
|---|---|---|---|
| `read` | 3.0 | 0.30 | Reading content is the most informative step |
| `search` | 2.5 | 0.25 | Locating evidence is necessary but not sufficient |
| `verification` | 1.5 | 0.15 | Build/tests prove correctness |
| `discovery` | 1.0 | 0.10 | Context only, not evidence of the answer |
| `git` | 1.0 | 0.10 | Domain-specific, full weight only when applicable |
| `ci` | 1.0 | 0.10 | Domain-specific, full weight only when applicable |

Total raw weight: 10.0

### Convergence Bonus

When multiple independent categories confirm the same finding, confidence should exceed any single category's score.

| Condition | Bonus |
|---|---|
| Only one category has evidence | +0% |
| Two categories confirm the same target | +10% |
| Three+ categories confirm the same target | +20% |
| Search + Read on the same file/symbol | +5% (stacked with above) |

Convergence is detected by matching tool arguments across categories. For example, if `find "MemoryManager"` and `read MemoryManager.h` and `grep "MemoryManager"` all involve the same term, they converge.

If the planner has no convergence data (tools for different targets), treat as single-category.

### Recovery Treatment

Recovery tools no longer produce a separate `0.50` confidence entry.

Instead:
- Recovery tools produce evidence in their **normal category** (a recovery find contributes to `search`, a recovery read contributes to `read`)
- The category max scoring already handles this: if the recovery tool produces a better result than the primary pass, the category score improves
- If recovery produces no new evidence: the category score stays the same (recovery is neutral)
- If recovery errors or crashes: no change (not negative -- we don't penalize effort)

Recovery is tracked **only** as a metric (`recovery_metrics.attempts`), not as a confidence input.

### Recovery Threshold Recommendation

With the new model, expected confidence distribution:

| Investigation Quality | Expected Score Range | Examples |
|---|---|---|
| Strong (search + read + convergence) | 0.75 – 0.92 | find+read+grep+read on same symbol |
| Adequate (search + read, no convergence) | 0.55 – 0.74 | find+read only, or grep+read only |
| Weak (single source, partial) | 0.25 – 0.54 | discovery only, or failed grep |
| Failed (no evidence) | 0.00 – 0.24 | error, all tools returned empty |

**Recommended recovery threshold: 0.50**

Below 0.50, confidence genuinely indicates insufficient evidence -- recovery is appropriate.
Above 0.50, the investigation is producing real evidence even if not yet conclusive.
At 0.75+, the investigation has strong multi-category evidence and recovery is wasteful.

This replaces the current hardcoded 0.70 threshold. The old threshold caught every query (avg confidence 0.59). The new threshold at 0.50 should fire only when evidence is genuinely weak, reducing unnecessary recovery from ~98% to an estimated ≤20% of queries.

### Expected Benchmark Distribution

Estimating from the 50-query capacity review run:

| Current Score | New Estimated Score | Count | Evidence Profile |
|---|---|---|---|
| 0.617 | 0.78 – 0.88 | 38 | find+read+grep+read with convergence |
| 0.562 | 0.70 – 0.80 | 11 | find+read only, or search without full cross-confirmation |
| 0.000 | 0.00 – 0.10 | 1 | error (tool_history empty) |

**Estimated new average: 0.76 – 0.82** (up from 0.59)

### Migration Impact

| Change | Impact | Risk |
|---|---|---|
| Replace `combine()` with category-weighted max | All existing confidence consumers read new values | Low -- interface unchanged |
| Remove recovery 0.50 entry | `confidence_history` no longer includes recovery entries | Low -- recovery is a separate loop section |
| Add cross-category term matching | Requires tracking tool args per tool in confidence evaluation | Medium -- needs to store args in confidence_history |
| New recovery threshold (0.50 → 0.70) | Changes when recovery fires | Medium -- fewer recovery attempts, benchmark must verify |
| `after_read()` values change | More files read = higher category score, not more entries | Low -- only combine() changes |

### What Stays The Same

- `ConfidenceService` class interface (`after_search`, `after_read`, `after_build`, `after_tests`, `after_ci`, `after_discovery`, `should_proceed`, `should_stop`)
- `ConfidenceResult` struct
- Per-tool scoring logic (the individual `after_*` functions)
- `should_stop()` threshold (0.2) -- this is the crash/emergency stop, not the recovery gate
- `confidence_delta` metric -- computed from final confidence, not per-entry average

### What Changes

- `ConfidenceService::combine()` -- new category-weighted algorithm (full rewrite)
- `execution_engine.cpp` recovery confidence handling -- remove the `cr.score = 0.5` entry
- `execution_engine.cpp` -- pass tool args into confidence evaluation for cross-category matching
- Recovery gate threshold -- move from 0.7 to 0.5 in `execution_engine.cpp:1150`

### Verification Plan

After implementation:

1. Re-run 50-query capacity review
2. Verify confidence distribution separates into three distinct bands (strong/adequate/weak)
3. Verify recovery rate drops to ≤20% of queries
4. Verify no regression in first-pass success or recovery success
5. Plot confidence vs correctness -- confirm overlap is minimal
