#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

binary_path="${BINARY_PATH:-${1:-}}"
if [[ -z "$binary_path" ]]; then
  for candidate in \
    "$repo_root/build/bin/cursor-agent" \
    "$repo_root/build/bin/Release/cursor-agent.exe" \
    "$repo_root/build/bin/cursor-agent.exe"; do
    if [[ -x "$candidate" ]]; then
      binary_path="$candidate"
      break
    fi
  done
fi

if [[ -z "$binary_path" ]]; then
  echo "Usage: BINARY_PATH=/path/to/cursor-agent $0 [binary-path]" >&2
  echo "Could not find a built cursor-agent under $repo_root/build/bin." >&2
  exit 2
fi

if [[ ! -x "$binary_path" ]]; then
  echo "Binary is not executable: $binary_path" >&2
  exit 2
fi
binary_path="$(cd "$(dirname "$binary_path")" && pwd)/$(basename "$binary_path")"

workdir="$(mktemp -d)"
mkdir -p "$workdir/home"
cleanup() {
  rm -rf "$workdir"
}
trap cleanup EXIT

input_file="$workdir/input.txt"
output_file="$workdir/output.txt"

cat >"$input_file" <<'EOF'
help
version
write:e2e.txt alpha beta
read:e2e.txt
replace:e2e.txt:alpha:omega:1
read:e2e.txt
grep:omega:.:e2e.txt
remember:e2e-memory-fact
memory
clear
cmd:echo e2e-cmd-ok
!echo e2e-bang-ok
!
echo e2e-shell-mode-ok
!
/help
/stats
exit
EOF

set +e
bash -c 'cd "$1" && HOME="$1/home" CURSOR_SKIP_UPDATE_CHECK=1 "$2" <"$3" >"$4" 2>&1' bash "$workdir" "$binary_path" "$input_file" "$output_file" &
agent_pid=$!
deadline=$((SECONDS + 20))
while kill -0 "$agent_pid" 2>/dev/null; do
  if ((SECONDS >= deadline)); then
    kill "$agent_pid" 2>/dev/null || true
    wait "$agent_pid" 2>/dev/null
    run_status=124
    break
  fi
  sleep 1
done
if [[ -z "${run_status:-}" ]]; then
  wait "$agent_pid"
  run_status=$?
fi
set -e
if [[ $run_status -ne 0 ]]; then
  echo "Agent batch run failed with exit code $run_status" >&2
  echo "=== Full output ===" >&2
  cat "$output_file" >&2 || true
  exit 1
fi

assert_contains() {
  local expected="$1"
  if ! grep -Fq "$expected" "$output_file"; then
    echo "Missing expected output: $expected" >&2
    echo "=== Full output ===" >&2
    cat "$output_file" >&2
    exit 1
  fi
}

assert_not_contains() {
  local unexpected="$1"
  if grep -Fq "$unexpected" "$output_file"; then
    echo "Unexpected output found: $unexpected" >&2
    echo "=== Full output ===" >&2
    cat "$output_file" >&2
    exit 1
  fi
}

assert_contains "Choose a mode."
assert_contains "Commands"
assert_contains "Cursor v"
assert_contains "File 'e2e.txt' written successfully"
assert_contains "alpha beta"
assert_contains "Successfully replaced 1 occurrence(s)"
assert_contains "omega beta"
assert_contains "Found 1 matches:"
assert_contains "Remembered: e2e-memory-fact"
assert_contains "e2e-memory-fact"
assert_contains "Session memory cleared"
assert_contains "e2e-cmd-ok"
assert_contains "e2e-bang-ok"
assert_contains "Entering shell mode"
assert_contains "e2e-shell-mode-ok"
assert_contains "Exiting shell mode"
assert_contains "Available meta commands:"
assert_contains "Session Statistics:"
assert_contains "Goodbye"
assert_contains "Agent run completed"
assert_not_contains "AI service returned status code"
assert_not_contains "AI service unavailable"

echo "Local batch E2E passed"
