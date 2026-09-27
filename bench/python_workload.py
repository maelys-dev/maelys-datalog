#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""One isolated Python consumer. No instrumentation of native allocations here."""
import argparse
import hashlib
import json
import time


CASES = [f"{n}-{kind}-{mode}" for n in (7, 93)
         for kind in ("symbol", "integer") for mode in ("solve", "prepared")]
COLD_CASES = ["7-symbol-solve", "7-symbol-prepared"]
PHASES = ["input", "solve", "query", "close", "total"]
SOURCE = """can_read(User, Doc) :- owns(User, Doc) or delegated(User, Doc), not(blocked(User)).
has_any_document(User) :- owns(User, _).
allow(User, Doc) :- user(User), can_read(User, Doc).
"""


def fixture(case):
    count, kind, mode = case.split("-")
    def user(i):
        return f"user{i}" if kind == "symbol" else i
    def doc(i):
        return f"doc{i}.pdf" if kind == "symbol" else 1000 + i
    if count == "7":
        facts = [("user", [user(i)]) for i in range(3)] + [
            ("owns", [user(0), doc(0)]), ("delegated", [user(1), doc(0)]),
            ("owns", [user(2), doc(0)]), ("blocked", [user(2)])]
        queries = [("allow", [user(i), doc(0)], i != 2) for i in range(3)] + [
            ("has_any_document", [user(0)], True), ("has_any_document", [user(1)], False)]
    else:
        facts = []
        for i in range(30):
            facts.extend([("user", [user(i)]), ("owns", [user(i), doc(i)]),
                          ("delegated", [user(i), doc((i + 1) % 30)])])
            if i % 10 == 0:
                facts.append(("blocked", [user(i)]))
        queries = [("allow", [user(i), doc(i)], i % 10 != 0) for i in (0, 1, 10, 29)]
    return facts, queries, mode


def measure(case, samples, warmup):
    # Import and policy compilation are outside request latency; each invocation
    # is a fresh interpreter. Cold means its first request, not machine boot.
    import maelys_datalog as md
    facts, queries, mode = fixture(case)
    with md.Engine() as engine:
        engine.register_domain("python_perf", [
            md.Predicate(name, arity, flags) for name, arity, flags in [
                ("user", 1, md.PRED_EDB), ("owns", 2, md.PRED_EDB),
                ("delegated", 2, md.PRED_EDB), ("blocked", 1, md.PRED_EDB),
                ("can_read", 2, md.PRED_IDB),
                ("has_any_document", 1, md.PRED_IDB | md.PRED_QUERY),
                ("allow", 2, md.PRED_IDB | md.PRED_QUERY)]])
        with engine.load_inline_ruleset("python_perf", "perf", SOURCE) as rules:
            session = rules.prepare() if mode == "prepared" else None
            reused = rules.edb() if session else None

            def transaction(phases=False):
                stamps = [time.perf_counter_ns()] if phases else None
                edb = reused if reused is not None else rules.edb()
                if reused is not None:
                    edb.reset()
                for predicate, terms in facts:
                    edb.add_fact(predicate, terms)
                if phases:
                    stamps.append(time.perf_counter_ns())
                result = session.solve(edb) if session else rules.solve(edb)
                if phases:
                    stamps.append(time.perf_counter_ns())
                answers = [result.contains_fact(p, t) for p, t, _ in queries]
                if phases:
                    stamps.append(time.perf_counter_ns())
                result.close()
                if reused is None:
                    edb.close()
                if phases:
                    stamps.append(time.perf_counter_ns())
                return answers, stamps

            expected = [q[2] for q in queries]
            def checked(phases=False):
                start = time.perf_counter_ns()
                answers, stamps = transaction(phases)
                elapsed = time.perf_counter_ns() - start
                if answers != expected:
                    raise ValueError(f"wrong query answers: {case}: {answers} != {expected}")
                return elapsed, stamps

            cold, _ = checked()
            data = {phase: [] for phase in PHASES}
            for _ in range(warmup if samples else 0):
                checked()
            for _ in range(samples):
                total, _ = checked()  # No intermediate clocks in this sample.
                data["total"].append(total)
            for _ in range(samples):
                _, stamps = checked(True)  # Separate diagnostic transaction.
                for i, phase in enumerate(PHASES[:-1]):
                    data[phase].append(stamps[i + 1] - stamps[i])
            if session:
                reused.close()
                session.close()
    return dict(case=case, cold=cold, samples=data,
                output_sha256=hashlib.sha256(json.dumps(expected).encode()).hexdigest())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", choices=CASES)
    parser.add_argument("--samples", type=int, default=501)
    parser.add_argument("--warmup", type=int, default=50)
    args = parser.parse_args()
    print(json.dumps(measure(args.case, args.samples, args.warmup)))
