---
---

# Commit Plan: Directory-Aware Find Engineering Cycle

## Files to Commit

| File | Category | Change |
|------|----------|--------|
| `include/services/find_service.h` | retrieval | NEW -- shared ranking engine header |
| `src/services/find_service.cpp` | retrieval | NEW -- shared ranking engine impl |
| `CMakeLists.txt` | retrieval | ADD -- find_service.cpp to SERVICE_SOURCES |
| `src/app/command_router.cpp` | retrieval | REFACTOR -- production caller uses shared engine |
| `src/diagnostics/diagnostics.cpp` | retrieval | REFACTOR -- diagnostics caller uses shared engine |
| `src/services/execution_engine.cpp` | retrieval | MODIFY -- extract_best_term, find:results parsing |
| `src/ui/ui_manager.cpp` | ui | MODIFY -- tool output formatting, candidate count display |
| `include/ui/ui_manager.h` | ui | MODIFY -- header for UI changes |
| `tests/validation_runner.cpp` | test | REFACTOR -- validation caller uses shared engine |
| `tests/benchmark_runner.cpp` | test | REFACTOR -- benchmark caller uses shared engine |
| `docs/telemetry/directory_aware_find_report.md` | docs | NEW -- implementation report |
| `docs/telemetry/final_verification.md` | docs | NEW -- 6×3 path verification |
| `docs/telemetry/find_implementation_audit.md` | docs | NEW -- pre-refactor 4-implementation audit |
| `docs/telemetry/ui_audit.md` | docs | NEW -- UI behavior audit |
| `docs/architecture/implementation_audit.md` | docs | NEW -- session-vs-code claim verification |
| `AGENTS.md` | docs | MODIFY -- path references (failure_topology.md → docs/telemetry/) |
| `failure_topology.md` | docs | DELETE -- moved to docs/telemetry/failure_topology.md |

## Commit Order

### Commit 1 -- Retrieval Engine
```
feat: add shared directory-aware find ranking
```
Files: `include/services/find_service.h`, `src/services/find_service.cpp`, `CMakeLists.txt`, `src/app/command_router.cpp`, `src/diagnostics/diagnostics.cpp`, `src/services/execution_engine.cpp`

### Commit 2 -- UI Improvements
```
feat: improve investigation progress visibility
```
Files: `src/ui/ui_manager.cpp`, `include/ui/ui_manager.h`

### Commit 3 -- Test Alignment
```
test: align validation and benchmark retrieval behavior
```
Files: `tests/validation_runner.cpp`, `tests/benchmark_runner.cpp`

### Commit 4 -- Documentation
```
docs: record directory-aware find engineering cycle
```
Files: `docs/telemetry/directory_aware_find_report.md`, `docs/telemetry/final_verification.md`, `docs/telemetry/find_implementation_audit.md`, `docs/telemetry/ui_audit.md`, `docs/architecture/implementation_audit.md`, `AGENTS.md`, `failure_topology.md` (delete)

## Rules
- No commit mixes retrieval + UI + test + docs
- Retrieval engine and diagnostics are both "retrieval" since diagnostics.cpp:363-372 is a thin find-wrapper caller
- Each commit must build and pass tests (verified before cycle close)
