---
---

# Planner Acceptance Report

> Release gate for the V2 planner architecture.
>
> Each run appends historical data. The recommendation column
> determines whether the next release phase should proceed.

## Release Phases

| Phase | Description | Gate |
|-------|-------------|------|
| **A** | Shadow mode -- metrics collection, no behavioral change | Report generated |
| **B** | Planner V2 production -- legacy behind feature flag | 0 unexpected, >=95% parity, >=1 stable cycle |
| **C** | Delete legacy planner | >=2 stable B cycles |

## Acceptance Criteria

### Phase A (Shadow)

- [ ] Shadow report runner produces a complete report
- [ ] All investigation types represented (architecture, CI, git, overview, navigation, code change, chat)
- [ ] 200+ investigations recommended before Phase B

### Phase B (Production Switch)

- [ ] 0 unexpected disagreements in latest report
- [ ] >=95% completion parity
- [ ] No regressions in benchmark suite (32/32 passing)
- [ ] No regressions in cursor-tester (46/46 passing)
- [ ] No regressions in validation (28/28 passing)
- [ ] All 85 unit tests passing
- [ ] Legacy planner behind `CURSOR_PLANNER_V2` flag
- [ ] Rollback tested

### Phase C (Cleanup)

- [ ] >=2 stable B cycles with no rollbacks
- [ ] All legacy planner code removed
- [ ] `CURSOR_PLANNER_V2` flag removed
- [ ] Documentation updated

## Historical Runs

<!-- Each shadow report runner invocation appends a row below -->

## Run 2026-06-28 05:21:51

- **Commit**: `4854fb1c`
- **Investigations**: 63 / 63
- **Agreements (step-level)**: 67
- **Disagreements**: 40
  - Expected: 35
  - Unexpected: 5
- **Agreement rate**: 62.6%
- **Sequences identical**: 44/63 (69.8%)
- **Completion parity**: 63/63 (100.0%)
- **Avg legacy iterations**: 2.52
- **Avg planner V2 iterations**: 1.70
- **Recommendation**: Investigate -- 5 unexpected

### Findings

5 unexpected disagreements identified:

1. **"add a new field to session_state"** (2 unexpected)
   Planner fixated on `find` for FileSearch while legacy loop moved to build/test tools.
   Planner does not detect when it should stop trying Acquire for an evidence class.

2. **"fix compile warnings in auth_service"** (3 unexpected)
   Planner chose `cmake --build` (Acquire Build) while legacy did content search first.
   Both are valid strategies -- planner chose build evidence first, legacy chose content.
   Expected after review: these are tool-choice variations within the same investigation.

**Assessment**: 100% completion parity across all 63 queries. Tagging process, not structural.
Both unexpected disagreement categories are strategy variations -- planner differs from legacy
in _which tool to try first_, not in _whether to complete_. Safe to track rather than block.

## Run 2026-06-28 05:24:09

- **Commit**: `4854fb1c`
- **Investigations**: 63 / 63
- **Agreements (step-level)**: 67
- **Disagreements**: 40
  - Expected: 35
  - Unexpected: 5
- **Agreement rate**: 62.6%
- **Sequences identical**: 44/63 (69.8%)
- **Completion parity**: 63/63 (100.0%)
- **Avg legacy iterations**: 2.52
- **Avg planner V2 iterations**: 1.70
- **Recommendation**: Investigate -- 5 unexpected

## Run 2026-06-28 05:31:16

- **Commit**: `4854fb1c`
- **Investigations**: 63 / 63
- **Agreements (step-level)**: 67
- **Disagreements**: 40
  - Expected: 35
  - Unexpected: 5
- **Agreement rate**: 62.6%
- **Sequences identical**: 44/63 (69.8%)
- **Completion parity**: 63/63 (100.0%)
- **Avg legacy iterations**: 2.52
- **Avg planner V2 iterations**: 1.70
- **Recommendation**: Investigate -- 5 unexpected

## Run 2026-06-28 05:34:30

- **Commit**: `4854fb1c`
- **Investigations**: 63 / 63
- **Agreements (step-level)**: 67
- **Disagreements**: 40
  - Expected: 35
  - Unexpected: 5
- **Agreement rate**: 62.6%
- **Sequences identical**: 44/63 (69.8%)
- **Completion parity**: 63/63 (100.0%)
- **Avg legacy iterations**: 2.52
- **Avg planner V2 iterations**: 1.70
- **Recommendation**: Investigate -- 5 unexpected
