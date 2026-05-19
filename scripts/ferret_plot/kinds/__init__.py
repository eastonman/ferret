"""Plot-kind renderers: one module per CLI subcommand (`line`, `heatmap`, `facets`).

Each kind exposes `make_figure(df, args) -> plotly.graph_objects.Figure`,
where args is the argparse.Namespace produced by ferret_plot.cli.
"""
