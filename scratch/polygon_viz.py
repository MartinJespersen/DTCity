#!/usr/bin/env python3

import argparse
import re
import sys
from pathlib import Path

try:
    import matplotlib.pyplot as plt
except ModuleNotFoundError:
    raise SystemExit(
        "Matplotlib is required. Install it with:\n"
        "    python -m pip install matplotlib"
    )

SAMPLE_POLYGONS = {
    "triangle": [
        (0.0, 0.0),
        (10.0, 0.0),
        (0.0, 10.0)
    ],
    "quad": [
        (2.0, -2.0),
        (6.0, -2.0),
        (6.0, 4.0),
        (2.0, 4.0)
    ]
}

VERTEX_PATTERN = re.compile(
    r"^\s*([A-Za-z_][A-Za-z0-9_]*)\[(\d+)\]\s*=\s*"
    r"\{\s*([-+0-9.eE]+)\s*,\s*([-+0-9.eE]+)\s*\}\s*$"
)


def parse_polygons(text):
    indexed_polygons = {}
    for line in text.splitlines():
        match = VERTEX_PATTERN.match(line)
        if not match:
            continue

        polygon_name = match.group(1)
        vertex_idx = int(match.group(2))
        vertex = (float(match.group(3)), float(match.group(4)))
        indexed_polygons.setdefault(polygon_name, {})[vertex_idx] = vertex

    polygons = {}
    for polygon_name, indexed_vertices in indexed_polygons.items():
        sorted_indices = sorted(indexed_vertices)
        expected_indices = list(range(len(sorted_indices)))
        if sorted_indices != expected_indices:
            raise ValueError(
                f"{polygon_name} has non-contiguous vertex indices: "
                f"{sorted_indices}"
            )

        polygons[polygon_name] = [
            indexed_vertices[vertex_idx] for vertex_idx in sorted_indices
        ]

    return polygons


def polygon_twice_area(vertices):
    twice_area = 0.0
    for vertex_idx, vertex in enumerate(vertices):
        next_vertex = vertices[(vertex_idx + 1) % len(vertices)]
        twice_area += (
            vertex[0] * next_vertex[1] - vertex[1] * next_vertex[0]
        )
    return twice_area


def enable_mouse_wheel_zoom(figure, axes):
    def on_scroll(event):
        if event.inaxes != axes or event.xdata is None or event.ydata is None:
            return

        zoom_factor = 0.8 if event.button == "up" else 1.25
        x_min, x_max = axes.get_xlim()
        y_min, y_max = axes.get_ylim()
        cursor_x = event.xdata
        cursor_y = event.ydata

        new_x_min = cursor_x - (cursor_x - x_min) * zoom_factor
        new_x_max = cursor_x + (x_max - cursor_x) * zoom_factor
        new_y_min = cursor_y - (cursor_y - y_min) * zoom_factor
        new_y_max = cursor_y + (y_max - cursor_y) * zoom_factor

        axes.set_xlim(new_x_min, new_x_max)
        axes.set_ylim(new_y_min, new_y_max)
        figure.canvas.draw_idle()

    figure.canvas.mpl_connect("scroll_event", on_scroll)


def plot_polygons(polygons, output_path):
    figure, axes = plt.subplots(figsize=(12, 8))

    for polygon_name, vertices in polygons.items():
        if len(vertices) < 2:
            continue

        closed_vertices = vertices + [vertices[0]]
        x_values = [vertex[0] for vertex in closed_vertices]
        y_values = [vertex[1] for vertex in closed_vertices]
        twice_area = polygon_twice_area(vertices)
        winding = (
            "CCW"
            if twice_area > 0.0
            else "CW"
            if twice_area < 0.0
            else "degenerate"
        )

        line = axes.plot(
            x_values,
            y_values,
            marker="o",
            markersize=5,
            linewidth=2,
            label=f"{polygon_name}: {len(vertices)} vertices, {winding}",
        )[0]
        axes.fill(
            x_values,
            y_values,
            color=line.get_color(),
            alpha=0.15,
        )

        for vertex_idx, vertex in enumerate(vertices):
            axes.annotate(
                f"{polygon_name}[{vertex_idx}]\n"
                f"({vertex[0]:.6f}, {vertex[1]:.6f})",
                vertex,
                xytext=(7, 7),
                textcoords="offset points",
                fontsize=8,
                color=line.get_color(),
            )

    axes.set_title("DTCity polygon clipping visualization")
    axes.set_xlabel("X")
    axes.set_ylabel("Y")
    axes.set_aspect("equal", adjustable="datalim")
    axes.grid(True, alpha=0.3)
    axes.legend()
    axes.margins(0.1)
    figure.tight_layout()

    enable_mouse_wheel_zoom(figure, axes)

    if output_path:
        figure.savefig(output_path, dpi=180)
        print(f"Saved visualization to {output_path}")
    else:
        print("Use the toolbar to pan, zoom, configure subplots, or save.")
        print("Use the mouse wheel to zoom around the cursor.")
        plt.show()


def main():
    parser = argparse.ArgumentParser(
        description="Interactively visualize polygons copied from DTCity logs."
    )
    parser.add_argument(
        "--input",
        type=Path,
        help=(
            "Text/log file containing lines such as "
            "triangle[0] = {1.0, 2.0}. Use '-' to read stdin."
        ),
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Save an image instead of opening the interactive window.",
    )
    args = parser.parse_args()

    if args.input:
        if str(args.input) == "-":
            input_text = sys.stdin.read()
        else:
            input_text = args.input.read_text(encoding="utf-8")

        polygons = parse_polygons(input_text)
        if not polygons:
            parser.error(f"No polygon vertices found in {args.input}")
    else:
        polygons = SAMPLE_POLYGONS

    plot_polygons(polygons, args.output)


if __name__ == "__main__":
    main()
