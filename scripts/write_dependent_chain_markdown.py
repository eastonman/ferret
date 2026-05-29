#!/usr/bin/env python3
"""Write the dependent_chain_throughput Markdown artifact."""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ferret_plot.markdown import write_dependent_chain_markdown

_EXPECTED_ARGC = 3

if __name__ == "__main__":
    if len(sys.argv) != _EXPECTED_ARGC:
        print("usage: write_dependent_chain_markdown.py INPUT.csv OUTPUT.md", file=sys.stderr)
        sys.exit(2)
    write_dependent_chain_markdown(sys.argv[1], sys.argv[2])
