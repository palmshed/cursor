# TODO

## Testability layer (diagnostics/trace/scenario) - milestone list

### Milestone 1: Diagnostics JSON
- [x] Add CLI parsing for `--diagnostics` (and prompt extraction)
- [x] Add a non-interactive single-prompt execution path
- [x] Output diagnostics JSON including: goal_type, outcome, confidence, ai_called, evidence_count, tools

### Milestone 2: Trace export
- [ ] Add CLI parsing for `--trace <file>`
- [ ] Record tool events (tool, args, output up to a cap)
- [ ] Write trace JSON to the provided file

### Milestone 3: Scenario runner
- [ ] Add CLI parsing for `--scenario <file>`
- [ ] Implement scenario runner that reads JSON array form
- [ ] For each scenario: run prompt, compute outcome + ai_called, compare to expectations
- [ ] Print pass/fail summary, exit non-zero if any failures

### Milestone 4: cursor-tester harness
- [ ] Add new executable `cursor-tester`
- [ ] Implement suite runner over a directory of scenario files
- [ ] CI-friendly output and proper exit code

### Testing (after each milestone)
- [ ] Run `./build/bin/cursor-agent --self-test`
- [ ] Run new `--diagnostics` against a known prompt and validate JSON shape manually
- [ ] Run `./build/bin/cursor-agent --scenario` against a minimal new scenario file
- [ ] Run `cursor-tester` against the scenario directory

