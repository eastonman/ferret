"""Tests for benchmark Markdown artifact summaries."""

from __future__ import annotations

from ferret_plot.markdown import dependent_chain_markdown
from fixtures import dct_df


class TestDependentChainMarkdown:
    def test_writes_heading_and_frequency_summary(self):
        body = dependent_chain_markdown(dct_df(chain_lengths=(1_000_000,), with_freq=False))

        assert body.startswith("# dependent_chain_throughput\n")
        assert "| chain_length | ns_per_site_min | ns_per_site_median | estimated_ghz |" in body
        assert "| 1000000 | 0.221 | 0.221 | 4.525 |" in body

    def test_includes_cycles_when_present(self):
        body = dependent_chain_markdown(dct_df(chain_lengths=(1_000_000,), with_freq=True))

        assert "cycles_per_site_min" in body
        assert "cycles_per_site_median" in body
