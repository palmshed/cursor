---
---

# Phase 1: Confidence Calibration Observation

## Calibration Error

| Metric | Value |
|---|---|
| First-pass success | 98.0% |
| Average confidence | 0.591 |
| Calibration error | **+38.6 percentage points** (confidence underestimates reality) |

Confidence is systematically too low. The system is correct 98% of the time but reports confidence as if it's guessing.

---

## Confidence Contribution Table

Every score in the pipeline, where it comes from, and what it should represent:

| Source | File:Line | Value Produced | Frequency | Impact on Average |
|---|---|---|---|---|
| `after_search()` with 0 hits | `confidence_service.cpp:28` | 0.10 | Rare (failed grep) | Minor drag |
| `after_search()` with 1-3 hits | `confidence_service.cpp:32` | 0.60 | Rare | Minor drag |
| `after_search()` with 4-10 hits | `confidence_service.cpp:36` | 0.80 | ~25% of grep calls | Pulls up |
| `after_search()` with >10 hits | `confidence_service.cpp:40` | 0.90 | Rare | Pulls up |
| `after_read(1, true)` **hardcoded** | `execution_engine.cpp:1362` | **0.55** | **Every read (100% of queries)** | **Primary dampener** |
| `after_read(1, true)` (recovery) | `execution_engine.cpp:1362` | 0.55 | Recovery reads | Dampener |
| `after_read()` inline for find | `execution_engine.cpp:1369` | 0.8 / 0.3 | ~66% of queries | Pulls up when OK |
| `after_read()` inline for references | `execution_engine.cpp:1365` | 0.8 / 0.3 | ~32% of queries | Pulls up when OK |
| `after_discovery()` | `confidence_service.cpp:206-230` | 0.25 / 0.50 / 0.75 / 1.0 | ~54% of queries | Typically 0.50 (2/4 factors) |
| Recovery tool (hardcoded) | `execution_engine.cpp:1184` | **0.50** | Every recovery | Pulls down |
| Fallback tool | `execution_engine.cpp:1380` | 0.50 | Rare | Neutral |

### Concrete example: a typical successful query

Tool sequence for "Locate the definition of MemoryManager":

```
 1. discovery                     0.50   (2 of 4 factors)
 2. find "MemoryManager"          0.80   (4-10 matches)
 3. read MemoryManager.h          0.55   (hardcoded)
 4. grep "class MemoryManager"    0.80   (4-10 matches)
 5. read memory_manager.cpp       0.55   (hardcoded)
 6. references MemoryManager      0.80   (callers found)
 7. read caller_file.cpp          0.55   (hardcoded)
 ─────────────────────────────────
    combine(): 7 scores → 4.55 / 7 = 0.65
```

The planner correctly found the class, read its header, confirmed with grep, found callers, and read usage. Three of the seven scores are the `after_read(1, true)` 0.55 -- together they pull the average down by 0.09 relative to the search + reference tools.

---

## Confidence Distribution (50 queries)

```
0.617  ─ 38 queries ─ ████████████████████████████████████████████████
0.562  ─ 11 queries ─ ██████████████
0.000  ─  1 query   ─ █ (error, tool_history empty)
```

Two confidence values. Period. The system cannot express "very confident," "barely confident," or anything between 0.562 and 0.617.

The 0.617 vs 0.562 difference is entirely driven by whether the tool sequence length happens to be even or odd (one extra 0.55 read entry changes the average).

**This is not measuring investigation quality. It is measuring tool sequence length.**

---

## Root Cause Analysis

### Root Cause 1: `after_read(1, true)` is always 0.55

File: `execution_engine.cpp:1362`
```cpp
cr = ConfidenceService::after_read(1, true);
```

`files_read` is hardcoded `1` and `relevant_to_goal` is hardcoded `true`. The function's signature supports richer input but the call site never provides it.

Reading the file that `find` located and `grep` confirmed is the **most informative step** in the pipeline -- the planner found the right file and read it. But it contributes only 0.55, less than any successful search or reference tool.

**Impact on 50-query average:** If `after_read()` returned 0.80 for confirmed-relevant reads, application-wide confidence would rise from 0.591 to approximately 0.72 using the same tool histories.

### Root Cause 2: `combine()` is unweighted averaging

```cpp
double total = 0.0;
for (auto &cr : results)
    total += cr.score;
r.score = total / results.size();
```

A `find` that locates the exact file (0.80) and a `read` of that file (0.55) should together produce higher confidence than 0.675. The convergence of find→grep→read on the same target is stronger evidence than either tool individually. The average does not model convergence.

### Root Cause 3: Recovery tools always contribute exactly 0.50

```cpp
cr.score = 0.5;
cr.reason = "recovery tool: " + rtr.tool;
```

A recovery that successfully finds new evidence is treated identically to a recovery that returns nothing. With 0.98 avg recoveries per query, this adds a 0.50 entry to nearly every confidence history, further depressing the average.

### Root Cause 4: The recovery gate fires on every query

```cpp
if (combined.score < 0.7) { ... }
```

Average confidence is 0.59, so this threshold is crossed by every query. The planner recovers on 98% of queries even though 98% already succeeded on the first pass. This inflates `avg_recoveries_per_query` (0.98) and adds unnecessary 0.50 entries to the confidence history.

---

## Correlation With Correctness

| Outcome | Count | Avg Confidence | Calibration Error |
|---|---|---|---|
| Correct (first-pass success) | 49 | 0.602 | **Underestimates by +38pp** |
| Error (tool crash) | 1 | 0.000 | Accurate (truly zero evidence) |
| Incorrect answer | 0 | N/A | No incorrect answers in sample |

**Every correct answer scored below 0.65.** The confidence function consistently reports "low confidence" for demonstrably correct investigations. This is the opposite of predictive.

---

## Secondary Issues

### `should_stop()` threshold

```cpp
ConfidenceService::should_stop(combined, 0.2)
```

With average confidence at 0.59, the stop gate (threshold 0.2) is never triggered during normal operation. This is actually correct behavior -- we don't want to stop investigations that are producing evidence. But it means the stop gate is vestigial at current calibration levels.

### `confidence_delta` is misleading

```cpp
result.recovery_metrics.confidence_delta =
    final_confidence.score - confidence_history.front().score;
```

Since the first tool is usually `discovery` (0.50) or `find` (0.80), and the final score is ~0.59, the delta is small or negative. This metric is meant to measure whether confidence improved during the investigation, but the averaging model ensures it rarely does.

---

## Summary

The confidence system has one real problem and one structural weakness:

**Real problem:** `after_read(1, true)` hardcoded to 0.55 destroys the signal from the most informative step.

**Structural weakness:** Simple averaging in `combine()` + flat 0.50 for recovery + always-triggered recovery gate at 0.7 produce a system that measures tool sequence length, not investigation quality.

The fix is: make `after_read()` reflect actual read quality, weight convergence in `combine()`, and recalibrate the recovery gate so it fires only when confidence genuinely indicates a problem.
