"""Static contract tests for the benchmark CI workflow.

These tests catch the user-visible CI behavior we rely on without
running full benchmark sweeps locally.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
RUNNER = ROOT / "scripts" / "run_benchmarks.sh"
WORKFLOW = ROOT / ".github" / "workflows" / "benchmarks.yml"

BENCHMARKS = (
    "dependent_chain_throughput",
    "direct_branch_footprint",
    "nested_call_depth",
    "branch_history_footprint",
)


def test_benchmark_runner_covers_full_sweep_html_outputs():
    text = RUNNER.read_text()

    assert 'REPS="${FERRET_BENCHMARK_REPS:-7}"' in text
    assert 'WARMUP="${FERRET_BENCHMARK_WARMUP:-1}"' in text

    for bench in BENCHMARKS:
        assert f'run_benchmark "{bench}"' in text

    assert 'local csv="$OUT_DIR/${ARTIFACT_PREFIX}${bench}.csv"' in text
    assert 'local html="$OUT_DIR/${ARTIFACT_PREFIX}${bench}.html"' in text
    assert 'local md="$OUT_DIR/${ARTIFACT_PREFIX}${bench}.md"' in text
    assert 'run_benchmark "branch_history_footprint" "surface"' in text
    assert 'run_benchmark "dependent_chain_throughput" "line" "markdown"' in text
    assert 'FREQ="$(estimate_frequency "$DEPENDENT_CHAIN_CSV")"' in text
    assert 'append_frequency_summary "$DEPENDENT_CHAIN_MD"' in text
    assert 'args+=(--freq="$freq")' in text
    assert (
        'run_benchmark "direct_branch_footprint" "line" "" "$FREQ" '
        '"--branches=16..32768@2 --spacing_bytes=${DIRECT_BRANCH_SPACING_LO}..128"'
    ) in text
    assert "DIRECT_BRANCH_SPACING_LO=4" in text
    assert "DIRECT_BRANCH_SPACING_LO=8" in text
    assert 'run_benchmark "nested_call_depth" "line" "" "$FREQ"' in text
    assert (
        'run_benchmark "branch_history_footprint" "surface" "" "$FREQ" "--branches=16..1024@2 --history_len=1..1024@2"'
    ) in text
    assert 'cat "$md"' in text
    assert '"$GITHUB_STEP_SUMMARY"' in text
    assert 'python3 scripts/write_dependent_chain_markdown.py "$csv" "$md"' in text
    assert 'python3 scripts/freq.py "$csv"' in text
    assert 'validate_csv_complete "$csv"' in text
    assert "empty benchmark metric cells at CSV lines" in text
    assert "ulimit -l 0" in text

    assert "detect_cpu_model()" in text
    assert "append_cpu_info_summary()" in text
    assert "append_cpu_info_summary" in text.split('run_benchmark "dependent_chain_throughput"')[0]
    assert "lscpu" in text
    assert "/proc/cpuinfo" in text
    assert "machdep.cpu.brand_string" in text
    assert "## Runner CPU" in text

    assert "--branches=16..32768@2" in text
    assert "--spacing_bytes=${DIRECT_BRANCH_SPACING_LO}..128" in text
    assert "--branches=16..1024@2" in text
    assert "--history_len=1..1024@2" in text
    assert "--depth=" not in text
    assert "--chain_length=" not in text


def test_benchmark_workflow_uploads_each_html_as_its_own_artifact():
    text = WORKFLOW.read_text()

    assert "workflow_dispatch:" in text
    assert "ubuntu-latest" in text
    assert "ubuntu-24.04-arm" in text
    assert "macos-latest" in text

    for bench in BENCHMARKS:
        pattern = re.compile(
            rf"name: upload {bench} html.*?"
            rf"uses: actions/upload-artifact@.*?"
            rf"name: \$\{{{{ matrix.label }}}}-{bench}\.html.*?"
            rf"path: benchmark-results/\$\{{{{ matrix.label }}}}/\$\{{{{ matrix.label }}}}-{bench}\.html.*?"
            rf"archive: false",
            re.DOTALL,
        )
        assert pattern.search(text), f"missing separate HTML artifact upload for {bench}"

    assert "path: benchmark-results/${{ matrix.label }}/*.html" not in text
    assert re.search(r"path: benchmark-results/\$\{\{ matrix\.label \}\}/\s*$", text, re.MULTILINE) is None


def test_benchmark_runner_reuses_calibrated_frequency_and_writes_job_summary(tmp_path):
    fake_bin = tmp_path / "bin"
    out_dir = tmp_path / "out"
    log_path = tmp_path / "ferret-args.log"
    summary_path = tmp_path / "job-summary.md"
    fake_bin.mkdir()

    fake_ferret = fake_bin / "ferret"
    fake_ferret.write_text(
        """#!/usr/bin/env bash
set -euo pipefail
printf '%s\\n' "$*" >> "$FERRET_FAKE_LOG"
out=''
bench=''
for arg in "$@"; do
  if [[ "$arg" == "--out="* ]]; then
    out="${arg#--out=}"
  elif [[ -z "$bench" && "$arg" != "run" ]]; then
    bench="$arg"
  fi
done
mkdir -p "$(dirname "$out")"
case "$bench" in
  dependent_chain_throughput)
    cat > "$out" <<'CSV'
benchmark,chain_length,ticks_min,ticks_median,iters,sites_per_iter,reps,ns_per_site_min,ns_per_site_median
dependent_chain_throughput,100000000,1,1,1,100000000,1,0.25,0.25
CSV
    ;;
  *)
    cat > "$out" <<CSV
benchmark,param,ticks_min,ticks_median,iters,sites_per_iter,reps,ns_per_site_min,ns_per_site_median,cycles_per_site_min,cycles_per_site_median,freq_hz
$bench,1,1,1,1,1,1,0.25,0.25,1,1,4000000000
CSV
    ;;
esac
""",
        encoding="utf-8",
    )
    fake_ferret.chmod(0o755)

    fake_python = fake_bin / "python3"
    fake_python.write_text(
        """#!/usr/bin/env bash
set -euo pipefail
script="$1"
shift
case "$script" in
  -)
    exec "$FERRET_REAL_PYTHON" - "$@"
    ;;
  scripts/freq.py)
    printf 'estimated_freq=4.000GHz\\n'
    ;;
  scripts/plot.py)
    out=''
    while [[ $# -gt 0 ]]; do
      case "$1" in
        --out=*) out="${1#--out=}" ;;
        --out) shift; out="$1" ;;
      esac
      shift
    done
    mkdir -p "$(dirname "$out")"
    printf '<html></html>\\n' > "$out"
    ;;
  scripts/write_dependent_chain_markdown.py)
    cat > "$2" <<'MD'
# dependent_chain_throughput

| chain_length | estimated_ghz |
| --- | --- |
| 100000000 | 4 |
MD
    ;;
  *)
    printf 'unexpected python shim call: %s\\n' "$script" >&2
    exit 99
    ;;
esac
""",
        encoding="utf-8",
    )
    fake_python.chmod(0o755)

    env = os.environ.copy()
    env.update(
        {
            "FERRET_BENCHMARK_ARTIFACT_PREFIX": "test-",
            "FERRET_BENCHMARK_REPS": "1",
            "FERRET_BENCHMARK_WARMUP": "0",
            "FERRET_FAKE_LOG": str(log_path),
            "FERRET_REAL_PYTHON": sys.executable,
            "GITHUB_STEP_SUMMARY": str(summary_path),
            "PATH": f"{fake_bin}{os.pathsep}{env['PATH']}",
        }
    )

    subprocess.run(
        ["bash", str(RUNNER), str(fake_ferret), str(out_dir)],
        cwd=ROOT,
        env=env,
        check=True,
    )

    calls = log_path.read_text(encoding="utf-8").splitlines()
    assert any("run dependent_chain_throughput" in call for call in calls)
    assert not any("--freq=" in call for call in calls if "dependent_chain_throughput" in call)
    for bench in ("direct_branch_footprint", "nested_call_depth", "branch_history_footprint"):
        assert any(f"run {bench}" in call and "--freq=4.000GHz" in call for call in calls)
    assert any(
        "run branch_history_footprint" in call and "--branches=16..1024@2" in call and "--history_len=1..1024@2" in call
        for call in calls
    )

    summary = summary_path.read_text(encoding="utf-8")
    assert "# dependent_chain_throughput" in summary
    assert "| chain_length | estimated_ghz |" in summary

    assert "## Runner CPU" in summary
    assert "| model |" in summary
    assert "| arch |" in summary
    assert "| os |" in summary
    cpu_section = summary.split("## Runner CPU", 1)[1].split("##", 1)[0]
    assert "unknown" not in cpu_section, f"CPU model was not detected: {cpu_section}"
