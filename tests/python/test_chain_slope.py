"""Tests for scripts/chain_slope.py.

The script exists to catch a silent failure mode: a dependent chain that
has been severed (typically by load value prediction) reports issue
throughput as if it were latency, which reads *faster* than the truth.
The detector is that cycles_per_site vs alu_ops must have slope 1.0.
"""

from __future__ import annotations

import csv
import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "chain_slope.py"

_spec = importlib.util.spec_from_file_location("chain_slope", SCRIPT)
chain_slope = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(chain_slope)


def write_csv(path, rows, *, axes=("addresses",)):
    cols = ["benchmark", *axes, "alu_ops", "seed", "ns_per_site_min", "cycles_per_site_min"]
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        for r in rows:
            w.writerow(r)


def row(alu, cycles, addresses=64):
    return {
        "benchmark": "store_load_footprint",
        "addresses": addresses,
        "alu_ops": alu,
        "seed": 1,
        "ns_per_site_min": cycles / 4.5,
        "cycles_per_site_min": cycles,
    }


def test_serial_chain_passes(tmp_path):
    # cycles = 1.0 * alu_ops + 1.0  -> slope exactly 1
    p = tmp_path / "good.csv"
    write_csv(p, [row(a, a + 1.0) for a in (1, 2, 4, 8, 16)])
    assert chain_slope.main(["chain_slope.py", str(p)]) == 0


def test_severed_chain_fails(tmp_path):
    # A chain broken by value prediction stops tracking alu_ops: the loop
    # runs at issue throughput, so the slope collapses.
    p = tmp_path / "severed.csv"
    write_csv(p, [row(a, 0.35 + 0.05 * a) for a in (1, 2, 4, 8, 16)])
    assert chain_slope.main(["chain_slope.py", str(p)]) == 1


def test_sublinear_slope_warns_but_does_not_fail(tmp_path):
    # A slope moderately below 1.0 is a real machine behaviour: part of the
    # load's setup overlaps the chain, so the forward cost shrinks as the
    # latency shadow grows. Measured on an M5 Pro at slope 0.847. That is
    # not a benchmark bug, so it must not be reported as a severed chain.
    p = tmp_path / "sublinear.csv"
    write_csv(p, [row(a, 0.85 * a + 8.4) for a in (1, 2, 4, 8, 16)])
    assert chain_slope.main(["chain_slope.py", str(p)]) == 0


def test_carry_op_costing_more_than_a_cycle_fails(tmp_path):
    # Slope above 1.0 means subtracting alu_ops to recover the forward
    # cost would under-correct.
    p = tmp_path / "steep.csv"
    write_csv(p, [row(a, 1.12 * a + 1.0) for a in (1, 2, 4, 8, 16)])
    assert chain_slope.main(["chain_slope.py", str(p)]) == 1


def test_groups_are_fitted_independently(tmp_path):
    # One healthy group and one severed group in the same file: the file
    # must fail, because a partial failure is still a failure.
    p = tmp_path / "mixed.csv"
    good = [row(a, a + 1.0, addresses=64) for a in (1, 2, 4, 8)]
    bad = [row(a, 0.35 + 0.05 * a, addresses=128) for a in (1, 2, 4, 8)]
    write_csv(p, good + bad)
    assert chain_slope.main(["chain_slope.py", str(p)]) == 1

    groups = chain_slope.load([str(p)])
    assert len(groups) == 2, "rows must be grouped by their axis columns, not pooled"


def test_multiple_files_are_pooled_into_one_fit(tmp_path):
    # The normal workflow is one CSV per --alu_ops value.
    paths = []
    for a in (1, 2, 4, 8, 16):
        q = tmp_path / f"alu{a}.csv"
        write_csv(q, [row(a, a + 1.0)])
        paths.append(str(q))
    groups = chain_slope.load(paths)
    assert len(groups) == 1
    assert len(next(iter(groups.values()))) == 5
    assert chain_slope.main(["chain_slope.py", *paths]) == 0


def test_single_alu_ops_value_is_skipped_not_failed(tmp_path):
    p = tmp_path / "one.csv"
    write_csv(p, [row(16, 17.0)])
    assert chain_slope.main(["chain_slope.py", str(p)]) == 0


def test_rows_without_cycles_are_ignored(tmp_path):
    # Rows are blank when --freq was omitted or the JIT failed.
    p = tmp_path / "blank.csv"
    rows = [row(a, a + 1.0) for a in (1, 2, 4)]
    rows.append({**row(8, 0.0), "cycles_per_site_min": ""})
    write_csv(p, rows)
    assert chain_slope.main(["chain_slope.py", str(p)]) == 0


def test_missing_alu_ops_column_is_an_error(tmp_path):
    p = tmp_path / "noalu.csv"
    with open(p, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["benchmark", "branches", "cycles_per_site_min"])
        w.writeheader()
        w.writerow({"benchmark": "direct_branch_footprint", "branches": 8, "cycles_per_site_min": 2.0})
    with pytest.raises(SystemExit):
        chain_slope.load([str(p)])


def test_usage_error_without_arguments():
    assert chain_slope.main(["chain_slope.py"]) == 2
