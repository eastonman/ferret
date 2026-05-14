#!/usr/bin/env python3
"""Entry shim. The real CLI lives in scripts/ferret_plot/cli.py."""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ferret_plot.cli import main

if __name__ == "__main__":
    sys.exit(main())
