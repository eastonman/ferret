#!/usr/bin/env bash
# Run full default ferret benchmark sweeps and render one standalone HTML per benchmark.
# dependent_chain_throughput is the frequency calibration pass; its estimated
# frequency is reused as --freq for the remaining benchmark sweeps.

set -euo pipefail

cd "$(dirname "$0")/.."

FERRET_BIN="${1:-build/ferret}"
OUT_DIR="${2:-benchmark-results/local}"
REPS="${FERRET_BENCHMARK_REPS:-7}"
WARMUP="${FERRET_BENCHMARK_WARMUP:-1}"
ARTIFACT_PREFIX="${FERRET_BENCHMARK_ARTIFACT_PREFIX:-}"

mkdir -p "$OUT_DIR"

run_benchmark() {
  local bench="$1"
  local plot_kind="$2"
  local extra_output="${3:-}"
  local freq="${4:-}"
  local csv="$OUT_DIR/${ARTIFACT_PREFIX}${bench}.csv"
  local html="$OUT_DIR/${ARTIFACT_PREFIX}${bench}.html"
  local md="$OUT_DIR/${ARTIFACT_PREFIX}${bench}.md"
  local args=(run "$bench" --reps="$REPS" --warmup="$WARMUP" --out="$csv")

  echo "==> running $bench full sweep"
  if [[ -n "$freq" ]]; then
    args+=(--freq="$freq")
  fi
  "$FERRET_BIN" "${args[@]}"

  echo "==> rendering $html"
  python3 scripts/plot.py "$plot_kind" "$csv" --out="$html" --html-js=inline

  if [[ "$extra_output" == "markdown" ]]; then
    echo "==> writing $md"
    write_dependent_chain_markdown "$csv" "$md"
  fi
}

write_dependent_chain_markdown() {
  local csv="$1"
  local md="$2"
  python3 scripts/write_dependent_chain_markdown.py "$csv" "$md"
}

estimate_frequency() {
  local csv="$1"
  local estimate
  estimate="$(python3 scripts/freq.py "$csv")"
  if [[ "$estimate" != estimated_freq=* ]]; then
    echo "freq.py returned unexpected output: $estimate" >&2
    return 2
  fi

  local freq="${estimate#estimated_freq=}"
  if [[ -z "$freq" ]]; then
    echo "freq.py returned an empty estimated frequency" >&2
    return 2
  fi
  printf "%s\n" "$freq"
}

append_frequency_summary() {
  local md="$1"
  if [[ -z "${GITHUB_STEP_SUMMARY:-}" ]]; then
    return 0
  fi

  echo "==> appending frequency table to $GITHUB_STEP_SUMMARY"
  {
    cat "$md"
    echo
  } >> "$GITHUB_STEP_SUMMARY"
}

detect_cpu_model() {
  local model=""
  if command -v lscpu >/dev/null 2>&1; then
    model="$(LC_ALL=C lscpu | awk -F': +' '/^Model name:/ {print $2; exit}')"
  fi
  if [[ -z "$model" ]] && [[ -r /proc/cpuinfo ]]; then
    model="$(awk -F': ' '/^model name/ {print $2; exit}' /proc/cpuinfo)"
  fi
  if [[ -z "$model" ]] && command -v sysctl >/dev/null 2>&1; then
    model="$(sysctl -n machdep.cpu.brand_string 2>/dev/null || true)"
  fi
  printf "%s\n" "${model:-unknown}"
}

append_cpu_info_summary() {
  if [[ -z "${GITHUB_STEP_SUMMARY:-}" ]]; then
    return 0
  fi

  local model
  model="$(detect_cpu_model)"
  local arch
  arch="$(uname -m 2>/dev/null || echo unknown)"
  local os
  os="$(uname -s 2>/dev/null || echo unknown)"

  echo "==> appending runner CPU info to $GITHUB_STEP_SUMMARY"
  {
    echo "## Runner CPU"
    echo
    echo "| field | value |"
    echo "| --- | --- |"
    echo "| model | ${model} |"
    echo "| arch | ${arch} |"
    echo "| os | ${os} |"
    echo
  } >> "$GITHUB_STEP_SUMMARY"
}

append_cpu_info_summary

run_benchmark "dependent_chain_throughput" "line" "markdown"

DEPENDENT_CHAIN_CSV="$OUT_DIR/${ARTIFACT_PREFIX}dependent_chain_throughput.csv"
DEPENDENT_CHAIN_MD="$OUT_DIR/${ARTIFACT_PREFIX}dependent_chain_throughput.md"
FREQ="$(estimate_frequency "$DEPENDENT_CHAIN_CSV")"
echo "==> calibrated frequency $FREQ"
append_frequency_summary "$DEPENDENT_CHAIN_MD"

# direct_branch_footprint's --spacing_bytes axis is log2_range, so the
# lower bound is rounded up per arch: 5 B JMP rel32 on x86_64 forces
# spacing>=8, while AArch64's 4 B B imm26 admits spacing=4.
case "$(uname -m)" in
  aarch64 | arm64) DIRECT_BRANCH_SPACING_LO=4 ;;
  *) DIRECT_BRANCH_SPACING_LO=8 ;;
esac
run_benchmark "direct_branch_footprint" "line" "" "$FREQ" "--branches=16..32768@2 --spacing_bytes=${DIRECT_BRANCH_SPACING_LO}..128"
run_benchmark "nested_call_depth" "line" "" "$FREQ"
run_benchmark "branch_history_footprint" "surface" "" "$FREQ"
