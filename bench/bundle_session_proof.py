# SPDX-License-Identifier: MPL-2.0
"""Assemble preserved benchmark evidence; never compile, time, or drop passes."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from session_proof import separate_passes


def bundle(proof, historic, output, proof_run, historic_run, base, head):
    if not all(re.fullmatch(r"[0-9]+", v) for v in (proof_run, historic_run)):
        raise ValueError("run identifiers must be decimal")
    current_meta = json.loads((proof / "metadata.json").read_text())
    old_meta = json.loads((historic / "metadata.json").read_text())
    if (current_meta["base"], current_meta["head"], old_meta["base"]) != (base, head, base):
        raise ValueError("source revision mismatch")
    source = proof / "session-proof"
    gate = json.loads((source / "counts-gate.json").read_text())
    acceptance = json.loads((source / "acceptance.json").read_text())
    counts = json.loads((source / "counts.json").read_text())
    if not gate["strict_ordered_counts_pass"] or not gate["neutral_layout_counts_identical"]:
        raise ValueError("incomplete count gate")
    if any(acceptance.values()) or len(counts) != 400 or not all(r["repeat_identical"] and r["verdict"] == "PASS" for r in counts):
        raise ValueError("count acceptance failed")
    for pad in (16, 64, 256):
        observed = json.loads((source / f"placement-counts-{pad}.json").read_text())
        if observed["strict_drift"] or len(observed["observations"]) != 1600:
            raise ValueError("incomplete neutral-layout count evidence")
    # These reports must already exist in the measured run, not be reconstructed
    # from a selected set of new timings during the report-only job.
    for filename in ("placement.md", "controls.md", "passes.md", "pass-quality.md"):
        if not (source / filename).is_file():
            raise ValueError("incomplete timing reports")
    timing_files = [p for pattern in ("*-aa-*.csv", "*-pad-*.csv") for p in source.glob(pattern) if ".samples." not in p.name]
    if len(timing_files) != 48:
        raise ValueError("expected all 48 placement timing passes")
    output.mkdir()
    current = output / "current"
    current.mkdir()
    old = output / "historic"
    old.mkdir()
    for path in source.glob("*.md"):
        shutil.copy2(path, current / path.name)
    for name in ("counts.json", "acceptance.json", "counts-gate.json", "codegen.json", "protocol.json", "binaries.sha256.json", "passes.json", "placement.json"):
        shutil.copy2(source / name, current / name)
    for name in ("comparison.md", "sessions.md", "explanations.md", "metadata.json"):
        shutil.copy2(proof / name, current / name)
    for name in ("comparison.md", "sessions.md", "metadata.json"):
        shutil.copy2(historic / name, old / name)
    separate_passes(old, historic, old_meta)
    quality = Path(__file__).with_name("report_session_quality.py")
    for root, destination in ((proof, current), (historic, old)):
        subprocess.run([sys.executable, str(quality), str(root), str(destination)], check=True)
    diagnostic = proof.parent / "bench-diagnostic"
    if diagnostic.exists():
        target = output / "solver-diagnostic"
        target.mkdir()
        for pattern in ("*.md", "*.json"):
            for path in diagnostic.glob(pattern):
                shutil.copy2(path, target / path.name)
    repository = "https://github.com/maelys-dev/maelys-datalog"
    manifest = dict(proof_run=proof_run, historic_run=historic_run, base=base, head=head,
                    original_head=old_meta["head"], source_metadata=current_meta,
                    note="Report assembly only. No compilation or timing occurs in this job. Original raw artifacts remain authoritative.")
    (output / "sources.json").write_text(json.dumps(manifest, indent=2) + "\n")
    report = ["# #110 — preuve du correctif noinline", "",
              f"Base `{base}` ; tête corrigée `{head}` ; ancienne tête `{old_meta['head']}`.", "",
              f"[Run mesuré]({repository}/actions/runs/{proof_run}) · [Run historique, B1 conservée]({repository}/actions/runs/{historic_run}). Ce job assemble les rapports sans compiler ni mesurer. Les fichiers bruts restent dans leurs artefacts d’origine.", "",
              "Le correctif produit ajoute seulement `__attribute__((noinline))` à la définition de `solve_aggregate_literal`. Aucun `error = {0}` ni changement de contrat n’est inclus.", "",
              "| Profil | Cas | Delta Ir exclusif | Delta Dr | Delta Dw | Répétitions identiques |",
              "|---|---:|---:|---:|---:|---|"]
    for profile in ("SMALL", "LARGE"):
        rows = [r for r in counts if r["profile"] == profile]
        if len(rows) != 200:
            raise ValueError("profile inventory")
        ranges = [f"{min(r[e]['delta'] for r in rows):+d} … {max(r[e]['delta'] for r in rows):+d}" for e in ("Ir", "Dr", "Dw")]
        report.append(f"| {profile} | 200 | " + " | ".join(ranges) + " | 200/200 |")
    report += ["", "Les 0/16/64/256 octets de placement neutre conservent les comptes Ir/Dr/Dw de chaque fonction sur les 400 cas, les deux révisions et les deux répétitions. Bcm/I1mr sont lus séparément. Aucun agrégat ne s’exécute dans ces politiques. L’exception nommée de session préparée vaut +8 Ir/+1 Dr/+2 Dw ; le +1 Dr était déjà présent dans les sondes historiques.", "",
               "Compiler : `clang -Wall -Wextra -g -I. -Iinclude -O2 -UNDEBUG`, avec `-DMAELYS_DATALOG_PROFILE_LARGE` pour LARGE. Désassemblage : `objdump -d --no-show-raw-insn --disassemble=solve_once_derive_ordered BINARY`. Les hashes et le code généré sont conservés dans le run mesuré.", "",
               "- [Tableau Callgrind par cas](current/counts.md) et [Bcm/I1mr](current/placement-events.md).",
               "- [Placement](current/placement.md), [six contrôles](current/controls.md) et [comptes des variantes](current/placement-counts.md).",
               "- [Toutes les passes actuelles](current/passes.md) et [historiques](historic/passes.md), sans retrait de B1.",
               "- Qualité médiane/min de toutes les passes [actuelles](current/all-pass-quality.md) et [historiques](historic/all-pass-quality.md), avec chaque anomalie et ses échantillons conservés en JSON.", "",
               "La classification dans la bande de placement suit la convention demandée ; elle ne prouve pas à elle seule une causalité. Au-dessus de la bande, le résidu reste non attribué. Les classifications A/A originales ne sont pas remplacées."]
    (output / "README.md").write_text("\n".join(report) + "\n")


if __name__ == "__main__":
    if len(sys.argv) != 8:
        raise SystemExit("usage: bundle_session_proof.py PROOF HISTORIC OUTPUT PROOF_RUN HISTORIC_RUN BASE HEAD")
    bundle(*(Path(v) for v in sys.argv[1:4]), *sys.argv[4:])
