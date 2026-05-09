#!/usr/bin/env python3
"""Read a dependent_chain_throughput CSV and print estimated frequency.

Usage: python scripts/freq.py freq.csv

Computes 1 / ns_per_site_min for the single (or first) data row and
prints `estimated_freq=<X>GHz` for easy copy-paste into ferret's
`--freq=...` flag.
"""

import csv
import sys


def main(argv):
    if len(argv) != 2:
        print("usage: freq.py FILE.csv", file=sys.stderr)
        return 2
    with open(argv[1], newline="") as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    if not rows:
        print("freq.py: no data rows", file=sys.stderr)
        return 2
    ns_per_site = float(rows[0]["ns_per_site_min"])
    if ns_per_site <= 0.0:
        print("freq.py: ns_per_site_min <= 0", file=sys.stderr)
        return 2
    ghz = 1.0 / ns_per_site
    print(f"estimated_freq={ghz:.3f}GHz")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
