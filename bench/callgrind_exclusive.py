# SPDX-License-Identifier: MPL-2.0
import re
EVENTS = ("Ir", "Dr", "Dw")

def exclusive_dump(text):
    """Read self costs; calls=0 still introduces an inclusive edge cost.

    https://valgrind.org/docs/manual/cl-format.html
    Intermediate dumps contain calls entered before counters were reset.
    callgrind_annotate 3.22 can display those zero-call arcs as self costs.
    Raw dumps and the original annotation are both retained for inspection.
    """
    labels = re.findall(r"^events: (.+)$", text, re.M)
    positions = re.findall(r"^positions: (.+)$", text, re.M)
    if len(labels) != 1 or positions != ["line"]:
        raise ValueError("unsupported Callgrind event/position schema")
    names = labels[0].split()
    if not set(EVENTS).issubset(names):
        raise ValueError("missing required events")
    functions, counts = {}, {}
    current = None
    association = False
    for line in text.splitlines():
        if line.startswith(("fn=", "cfn=")):
            value = line.split("=", 1)[1]
            match = re.fullmatch(r"\((\d+)\)(?: (.*))?", value)
            identity = match[1] if match else "name:" + value
            if not match or match[2] is not None:
                functions[identity] = match[2] if match else value
            if line.startswith("fn="):
                current = identity
            continue
        if line.startswith("calls="):
            association = True
            continue
        fields = line.split()
        if not fields or not re.fullmatch(r"\*|[+-]?\d+", fields[0]):
            continue
        if association:
            association = False
            continue
        if current is None or len(fields) < 2 or len(fields) > len(names) + 1:
            raise ValueError("malformed exclusive cost record")
        values = [int(v) for v in fields[1:]]
        if any(v < 0 for v in values):
            raise ValueError("negative exclusive event count")
        values += [0] * (len(names) - len(values))
        accumulator = counts.setdefault(current, dict.fromkeys(EVENTS, 0))
        for event in EVENTS:
            accumulator[event] += values[names.index(event)]
    result = {}
    for identity, values in counts.items():
        if identity not in functions:
            raise ValueError("unresolved function-name reference")
        name = re.sub(r"'\d+$", "", functions[identity])
        target = result.setdefault(name, dict.fromkeys(EVENTS, 0))
        for event in EVENTS:
            target[event] += values[event]
    if not result:
        raise ValueError("empty exclusive profile")
    return result
