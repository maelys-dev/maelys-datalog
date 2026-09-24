# SPDX-License-Identifier: MPL-2.0
import csv, json, sys
from pathlib import Path
root, out = map(Path, sys.argv[1:])
paths = sorted(root.glob("*-sessions-*.csv")) + sorted((root / "session-proof").glob("*-aa-*.csv")) + sorted((root / "session-proof").glob("*-pad-*.csv"))
paths = [p for p in paths if ".samples." not in p.name]
report = ["# Median/minimum across general and placement session passes", "", "Threshold: median/minimum < 1.2. Every case and original pass is retained; a threshold crossing alone does not establish its cause.", "", "| Pass | Cases | Median/min >= 1.2 | Maximum ratio |", "|---|---:|---:|---:|"]
flagged = []
for path in paths:
    rows = list(csv.DictReader(path.open()))
    assert len(rows) == 200
    ratios = [float(r["median_us"]) / float(r["min_us"]) for r in rows]
    bad = [r for r, v in zip(rows, ratios) if v >= 1.2]
    relative = str(path.relative_to(root))
    report.append(f"| {relative} | 200 | {len(bad)} | {max(ratios):.6f} |")
    if bad:
        wanted = {tuple(r[k] for k in ("policy", "order", "values", "size")) for r in bad}
        samples = {k: [] for k in wanted}
        for row in csv.DictReader(path.with_suffix(".samples.csv").open()):
            key = tuple(row[k] for k in ("policy", "order", "values", "size"))
            if key in samples:
                samples[key].append(float(row["elapsed_us"]))
        for row in bad:
            key = tuple(row[k] for k in ("policy", "order", "values", "size"))
            assert len(samples[key]) == 301
            flagged.append(dict(path=relative, case=key, ratio=float(row["median_us"])/float(row["min_us"]), summary=row, samples=samples[key]))
out.mkdir(exist_ok=True)
(out / "all-pass-quality.md").write_text("\n".join(report)+"\n")
(out / "all-pass-quality.json").write_text(json.dumps(flagged, indent=2)+"\n")
print("Passes", len(paths), "case/pass observations", 200 * len(paths), "flagged", len(flagged))
