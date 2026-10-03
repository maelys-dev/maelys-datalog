#!/usr/bin/env python3
"""Rebuild independent negative controls for the compact-fact invariants.

A private header copy is compiled per mutation. No checkout files are changed.
The canonical control must pass under ASan, UBSan and alignment sanitization.
Generated binaries and outputs belong outside Git.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADER = Path("src/core/maelys_datalog_types.h")
MUTATIONS = {
    "integer-load-truncated": ("memcpy(&term.as, &fact->payload[index], sizeof(term.as));", "memcpy(&term.as, &fact->payload[index], sizeof(term.as)); if (term.kind == MAELYS_DATALOG_TERM_INT) term.as.integer = (int32_t)term.as.integer;"),
    "integer-store-truncated": ("fact->payload[index].integer = term.as.integer", "fact->payload[index].integer = (int32_t)term.as.integer"),
    "reused-term-not-cleared": ("    fact->payload[index].integer = 0;\n    switch (term.kind)", "    /* mutant leaves old upper payload bytes */\n    switch (term.kind)"),
    "tag-written-to-next-slot": ("fact->kind[index] = (uint8_t)term.kind", "fact->kind[(index+1u)%MAELYS_DATALOG_MAX_TERMS] = (uint8_t)term.kind"),
    "inactive-compiled-terms-copied": ("i < atom->arity && i < MAELYS_DATALOG_MAX_TERMS", "i < MAELYS_DATALOG_MAX_TERMS"),
    "reused-symbol-upper-bytes": ("fact->payload[index].integer = 0;\n    fact->payload[index].symbol = value", "fact->payload[index].symbol = value"),
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--large", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    original = (ROOT / HEADER).read_text()
    for name, patch in [("baseline", None), *MUTATIONS.items()]:
        source = original
        if patch:
            before, after = patch
            if source.count(before) != 1:
                raise SystemExit(f"ambiguous mutation {name}: {source.count(before)} matches")
            source = source.replace(before, after)
        with tempfile.TemporaryDirectory(prefix="compact-facts-") as tmp:
            overlay = Path(tmp)
            header = overlay / HEADER
            header.parent.mkdir(parents=True)
            header.write_text(source)
            binary = args.output / name
            command = [args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-O1",
                       "-fsanitize=address,undefined,alignment", "-fno-sanitize-recover=all",
                       "-I" + str(overlay), "-I" + str(ROOT), "-I" + str(ROOT / "include")]
            if args.large:
                command.append("-DMAELYS_DATALOG_PROFILE_LARGE")
            command.extend([str(ROOT / "tests/test_maelys_datalog_compact_facts.c"), "-o", str(binary)])
            subprocess.run(command, check=True, capture_output=True)
            result = subprocess.run([str(binary)], capture_output=True)
            (args.output / (name + ".log")).write_bytes(result.stdout + result.stderr)
            if (result.returncode == 0) != (patch is None):
                raise SystemExit(f"{'baseline failed' if patch is None else 'mutation survived'}: {name}")
            print(f"PASS {name}: {'control passed' if patch is None else 'detected'}", flush=True)

if __name__ == "__main__":
    main()
