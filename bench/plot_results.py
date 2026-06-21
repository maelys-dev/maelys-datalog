#!/usr/bin/env python3
"""Optional SVG graph generation for P3-C49 benchmark CSV files."""

from __future__ import annotations

import csv
import os
import sys
from collections import defaultdict


def main(argv: list[str]) -> int:
    try:
        import matplotlib.pyplot as plt
    except Exception:
        return 0

    rows: list[dict[str, str]] = []
    for path in argv[1:]:
        with open(path, newline="", encoding="utf-8") as f:
            rows.extend(csv.DictReader(f))
    if not rows:
        return 0

    out_dir = os.path.join("bench", "results", "graphs")
    os.makedirs(out_dir, exist_ok=True)

    groups: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        if row.get("opt_level") == "-O2":
            groups[(row["benchmark"], row["group"])].append(row)

    for (benchmark, group), rs in groups.items():
        by_mode: dict[str, list[dict[str, str]]] = defaultdict(list)
        for row in rs:
            by_mode[row["mode"]].append(row)
        plt.figure(figsize=(7, 4))
        for mode, mode_rows in sorted(by_mode.items()):
            mode_rows.sort(key=lambda r: float(r["size"]))
            xs = [float(r["size"]) for r in mode_rows]
            ys = [float(r["median_us"]) for r in mode_rows]
            plt.plot(xs, ys, marker="o", label=mode)
        plt.title(f"{benchmark} ({group}, -O2)")
        plt.xlabel("size")
        plt.ylabel("median_us")
        plt.legend()
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        safe = benchmark.replace("/", "_")
        plt.savefig(os.path.join(out_dir, f"{safe}.svg"))
        plt.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
