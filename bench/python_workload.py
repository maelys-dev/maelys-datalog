#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""One isolated Python consumer. No instrumentation of native allocations here."""
import argparse
import hashlib
import json
import time
from contextlib import nullcontext


CASES = [f"{n}-{kind}-{mode}" for n in (7, 93)
         for kind in ("symbol", "integer") for mode in ("solve", "prepared")]
COLD_CASES = ["7-symbol-solve", "7-symbol-prepared"]
PHASES = ["input", "solve", "query", "close", "total"]
# Fixed before measurement: two extra real requests, never a calibrated delay.
POSITIVE_CONTROL_REQUESTS = 3
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


def repeat_transaction(transaction, repetitions):
    """Benchmark-only injection; the normal path keeps the original callable."""
    if repetitions == 1:
        return transaction
    if repetitions != POSITIVE_CONTROL_REQUESTS:
        raise ValueError("unsupported request repetition count")

    def repeated(phases=False):
        answers = []
        durations = [0] * (len(PHASES) - 1) if phases else None
        for _ in range(repetitions):
            values, stamps = transaction(phases)
            answers.extend(values)
            if phases:
                for i in range(len(durations)):
                    durations[i] += stamps[i + 1] - stamps[i]
        combined = [0] if phases else None
        if phases:
            for duration in durations:
                combined.append(combined[-1] + duration)
        return answers, combined

    return repeated


def measure(case, samples, warmup, positive_control=False, telemetry=False, fixed_storage=False):
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

            # Select before starting any clock. The injected variant calls the
            # same binding/binary; every request completes input/solve/query/close.
            repetitions = POSITIVE_CONTROL_REQUESTS if positive_control else 1
            transaction = repeat_transaction(transaction, repetitions)
            expected = [q[2] for q in queries]
            expected_measured = expected * repetitions
            def timed(phases=False):
                start = time.perf_counter_ns()
                answers, stamps = transaction(phases)
                elapsed = time.perf_counter_ns() - start
                return elapsed, answers, stamps

            recorder = None
            if telemetry:
                from python_telemetry import Recorder, CALIBRATION_EVERY
                recorder = Recorder(samples, warmup)
                fixed_storage = True
                def timed(phases=False):
                    return recorder.measure(transaction, phases)

            def checked(phases=False):
                elapsed, answers, stamps = timed(phases)
                if answers != expected_measured:
                    raise ValueError(f"wrong query answers: {case}: {answers} != {expected_measured}")
                return elapsed, stamps

            # Fixed storage is part of the new telemetry protocol. Keep the
            # growing-list path available for explicit, separate controls.
            data = {phase: [0] * samples if fixed_storage else [] for phase in PHASES}
            with recorder if recorder else nullcontext():
                cold, _ = checked()
                for _ in range(warmup if samples else 0):
                    checked()
                for index in range(samples):
                    if recorder and index % CALIBRATION_EVERY == 0:
                        recorder.calibrate('total', index)
                    total, _ = checked()  # No intermediate clocks in this sample.
                    if fixed_storage:
                        data["total"][index] = total
                    else:
                        data["total"].append(total)
                if recorder and samples:
                    recorder.calibrate('total', samples)
                for index in range(samples):
                    if recorder and index % CALIBRATION_EVERY == 0:
                        recorder.calibrate('phases', index)
                    _, stamps = checked(True)  # Separate diagnostic transaction.
                    for i, phase in enumerate(PHASES[:-1]):
                        duration = stamps[i + 1] - stamps[i]
                        if fixed_storage:
                            data[phase][index] = duration
                        else:
                            data[phase].append(duration)
                if recorder and samples:
                    recorder.calibrate('phases', samples)
            if session:
                reused.close()
                session.close()
    value = dict(case=case, cold=cold, samples=data, request_repetitions=repetitions,
                 sample_storage='fixed' if fixed_storage else 'growing',
                 output_sha256=hashlib.sha256(json.dumps(expected).encode()).hexdigest())
    if recorder:
        value['telemetry'] = recorder.export()
    return value


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", choices=CASES)
    parser.add_argument("--samples", type=int, default=501)
    parser.add_argument("--warmup", type=int, default=50)
    parser.add_argument("--positive-control", action="store_true",
                        help="benchmark only: execute three complete requests per sample")
    parser.add_argument("--telemetry", action="store_true",
                        help="record contemporaneous CPU/resource/GC events and adjacent calibration")
    parser.add_argument("--fixed-storage", action="store_true",
                        help="preallocate sample lists, also implied by --telemetry")
    args = parser.parse_args()
    print(json.dumps(measure(args.case, args.samples, args.warmup, args.positive_control,
                             args.telemetry, args.fixed_storage)))
