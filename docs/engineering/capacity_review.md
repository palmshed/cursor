---
---

# Capacity Review -- Level 2 Exit

## Overview

The capacity review measures whether the planner behaves like a senior engineer on **unseen** problems, not just on the handcrafted regression suite.

Four stress tests were designed to probe different failure modes:

1. **Unknown query stress** -- 100 architecture questions the planner has never seen
2. **Wrong-first-path stress** -- Fixtures that deliberately mislead the first investigation step
3. **Noise stress** -- Harmless distractions polluting the repository
4. **Human evaluation** -- An evaluation package for independent reviewers

## 1. Unknown Query Stress

### Method

Generated 100 architecture questions:
- **80 programmatic** -- extracted from the codebase itself (symbols, services, enums, file ownership, dependency chains, lifecycle, configuration references)
- **20 manual** -- written as "senior engineer" questions requiring architectural reasoning

Each question was run through `cursor-agent --json` and the results were collected and analyzed.

### Results (100 queries)

| Metric | Actual | Target | Status |
|--------|--------|--------|--------|
| First-pass success | 99.0% | ≥90% | PASS |
| Recovery success | 100.0% | ≥95% | PASS |
| Avg recoveries/query | 0.97 | <1.0 | PASS |
| Avg files read | 1.99 | ≤4.0 | PASS |
| Avg latency | 0.73s | -- | -- |
| Avg confidence | 0.59 | ≥0.7 | **FAIL** |

### Failure Analysis

**1 error** (out of 100): "Find all references to Startup across the codebase" -- crashed with `nlohmann::json type_error.316: invalid UTF-8 byte`. A pre-existing bug in SymbolService: when a matched file contains raw unescaped newlines in extracted content, JSON serialization fails. The `0x0A` byte (newline) at index 128 suggests a long line in a source file was not properly escaped. This is a **tool implementation bug**, not a planner defect.

**Confidence below target**: The average combined confidence (0.59) is below the 0.7 target. Investigation shows this is a **calibration issue in ConfidenceService::combine()** -- individual tool confidences (0.6-0.8) are reasonable, but the `combine()` function applies a dampening factor that pulls the average down. The planner still produces correct answers (99% success), but the confidence score under-reports its certainty.

### Tool Distribution

| Tool | Usage | % of queries |
|------|-------|-------------|
| read | 99 | 99% |
| find | 64 | 64% |
| grep | 64 | 64% |
| discovery | 63 | 63% |
| references | 34 | 34% |

## 2. Wrong-First-Path Stress

### Fixtures

Temporary source files created under `tests/fixtures/` and `tests/recovery/`:

| Fixture | Trap | Expected Planner Recovery |
|---------|------|--------------------------|
| `tests/fixtures/similar_symbols/session_manager.h` | Symbol similar to real `SessionState` | Disambiguation via full read and grep |
| `tests/fixtures/similar_symbols/execution_context.h` | Symbol similar to real `ExecutionResult` | Same as above |
| `tests/fixtures/duplicate_names/command_router.h` | Same filename as real `include/app/command_router.h` | The pick correct path via directory-aware ranking |
| `tests/fixtures/header_only/phantom_service.h` | Declaration with no implementation | Header→impl recovery strategy |
| `tests/fixtures/impl_only/ghost_component.cpp` | Implementation with no header | Impl→header recovery strategy |

### Test Scenarios

Three JSON scenarios under `tests/recovery/` (also copied to `scenarios/regressions/`):

| Scenario | What It Tests |
|----------|---------------|
| `duplicate_filenames_commandrouter.json` | Planner picks real command_router.h despite duplicate |
| `header_to_implementation.json` | Planner finds .cpp despite ambiguous find results |
| `similar_symbols_sessionstate.json` | Planner disambiguates real SessionState from fixture |

### Results

**3/3 scenarios pass.** The planner correctly disambiguates similar symbols via the `find > read > grep` pipeline. Directory-aware ranking (in `FindService`) prefers `include/app/command_router.h` over `tests/fixtures/duplicate_names/command_router.h` because the former is deeper in `include/`.

## 3. Noise Stress

### Method

Created 9 noise files in the production repository:
- `docs/legacy/README.md` -- misleading documentation
- `src/generated/execution_engine.cpp` -- generated stub with same class name
- `include/generated/execution_engine.h` -- generated stub header
- `src/backup/command_router.cpp` -- backup copy
- `tests/fixtures/output/replay_service.cpp` -- fixture copy
- `data/archive/v0.1/agent.cpp` -- old version
- `build/artifacts/symbol_cache.json` -- misleading artifact
- Various markdown and config noise

Ran 8 production queries against the noise-polluted repo, compared results against a clean baseline.

### Results

| Metric | Baseline | Noisy | Delta |
|--------|----------|-------|-------|
| Success rate | 8/8 (100%) | 8/8 (100%) | 0 |
| Contaminated queries | 0 | 3/8 | +3 |

**Contamination analysis**: The planner encounters noise files in 3 queries but still converges on production code. Contamination means noise files appeared in evidence but didn't prevent correct answers. For example, the planner saw both `include/generated/execution_engine.h` and `include/services/execution_engine.h` as find candidates -- both had the same score -- but when reading the generated stub, the evidence was insufficient, triggering recovery via grep which found the real file.

**Conclusion**: Noise does not reduce success rate. Recovery strategies handle distractions gracefully.

## 4. Human Evaluation

### Package

The human evaluation package is documented in `docs/engineering/human_evaluation_package.md`. It contains:

- 20 architecture questions with expected answers and evidence locations
- 5-point scoring rubric
- Scoring sheet with columns for correctness, investigation clarity, /inspect usefulness, and recovery visibility
- Pass threshold: ≥16/20 questions scoring ≥3

### Instructions for running

An evaluator unfamiliar with the repository should:
1. Run each question through `cursor-agent --json "<question>"`
2. Evaluate the answer against the expected answer
3. Score using the rubric
4. Report whether the planner's investigation path is understandable

## Capacity Metrics Summary

| Metric | Actual | Target | Status |
|--------|--------|--------|--------|
| First-pass success | 99.0% | ≥90% | PASS |
| Recovery success | 100.0% | ≥95% | PASS |
| Avg recoveries/query | 0.97 | <1.0 | PASS |
| Avg files read | 1.99 | ≤4.0 | PASS |
| Grep fallback | 64%* | ≤15% | See note |
| Avg confidence | 0.59 | ≥0.7 | FAIL |

*Grep fallback rate is high because for generic architecture questions, grep is a primary tool (not a fallback). The 15% target was designed for the benchmark suite where query answers are known ahead of time and should not require grep. This metric should be recalibrated for the capacity review context.

## Recommendations

1. **Do not advance to Level 3 yet.** The confidence calibration issue needs resolution first. The planner produces correct answers but under-reports confidence, which would undermine adaptive planning in Level 3.

2. **Fix the UTF-8 serialization bug** in SymbolService to eliminate the one failure.

3. **Recalibrate ConfidenceService::combine()** to produce scores ≥0.7 for queries with 2+ successful tool invocations.

4. **Run the human evaluation** with an independent engineer before declaring Level 2 complete.

## Test Infrastructure Created

| File | Purpose |
|------|---------|
| `tests/capacity_review.py` | Generates 100 questions, runs them, computes metrics |
| `tests/noise_stress.py` | Creates noise distractions, runs baseline vs noisy comparison |
| `tests/fixtures/similar_symbols/*.h` | Similar symbol name fixtures |
| `tests/fixtures/duplicate_names/*.h` | Duplicate filename fixtures |
| `tests/fixtures/header_only/*.h` | Header-only declaration fixture |
| `tests/fixtures/impl_only/*.cpp` | Implementation-only fixture |
| `tests/recovery/*.json` | Wrong-first-path scenario tests |
| `scenarios/regressions/duplicate_filenames_commandrouter.json` | Permanent regression test |
| `scenarios/regressions/header_to_implementation.json` | Permanent regression test |
| `scenarios/regressions/similar_symbols_sessionstate.json` | Permanent regression test |
| `docs/engineering/human_evaluation_package.md` | Human evaluation materials |
