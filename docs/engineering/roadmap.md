---
---

# Engineering Roadmap

## Current Sprint

### Sprint 2.1 -- Confidence Calibration

**Objective**

Align planner confidence with actual investigation quality.

**Tasks**

- Audit `ConfidenceService::combine()`
- Measure confidence against known-correct benchmark queries
- Increase confidence only when supported by evidence, not by heuristics

**Target**

- ≥0.70 average confidence for successful investigations with sufficient evidence
- Low confidence must remain low when evidence is genuinely weak
- Confidence should become predictive of correctness

**Deliverables**

- Updated confidence calibration report
- Before/after confidence distribution
- No regression in search correctness or recovery behavior

---

### Sprint 2.2 -- UTF-8 Robustness

**Objective**

Remove remaining production robustness issues.

**Tasks**

- Fix UTF-8 serialization crash
- Test Unicode filenames
- Test Unicode symbols
- Test Unicode repository paths
- Test replay serialization with Unicode content

**Deliverables**

- Zero UTF-8 crashes
- Regression tests covering Unicode scenarios

---

## Final Level 2 Validation

After both mini-sprints:

- Re-run the complete Capacity Review
- Re-run Search Correctness benchmarks
- Run the Human Evaluation package with an independent reviewer

Level 2 closes only when every exit criterion in `acceptance_criteria.md` is met.
