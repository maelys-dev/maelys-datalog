#!/usr/bin/env python3
"""Structural ABNF validator for the MAELYS-DATALOG-v2 specification.

This deliberately validates ABNF structure and rule references only. It is not
a Datalog parser and has no runtime or semantic authority.
"""

from __future__ import annotations

import pathlib
import re
import sys
from dataclasses import dataclass


RULE_RE = re.compile(r"^([A-Za-z][A-Za-z0-9-]*)[ \t]*=[ \t]*(.+)$")
NAME_RE = re.compile(r"[A-Za-z][A-Za-z0-9-]*")
REPEAT_RE = re.compile(r"(?:[0-9]+\*[0-9]*|\*[0-9]+|\*|[0-9]+)")
NUM_RE = re.compile(r"%[xX][0-9A-Fa-f]+(?:[-.][0-9A-Fa-f]+)*")


class AbnfError(ValueError):
    pass


@dataclass(frozen=True)
class Token:
    kind: str
    value: str
    column: int


def strip_comment(line: str) -> str:
    in_literal = False
    for index, char in enumerate(line):
        if char == '"':
            in_literal = not in_literal
        elif char == ";" and not in_literal:
            return line[:index]
    return line


def tokenize(expression: str, source: str, line_no: int) -> list[Token]:
    tokens: list[Token] = []
    pos = 0
    while pos < len(expression):
        char = expression[pos]
        if char in " \t":
            pos += 1
            continue
        if char in "/()[]":
            tokens.append(Token(char, char, pos + 1))
            pos += 1
            continue
        repeat = REPEAT_RE.match(expression, pos)
        if repeat:
            value = repeat.group(0)
            tokens.append(Token("repeat", value, pos + 1))
            pos = repeat.end()
            continue
        if expression.startswith(("%s\"", "%S\""), pos):
            end = expression.find('"', pos + 3)
            if end < 0:
                raise AbnfError(f"{source}:{line_no}:{pos + 1}: unclosed case-sensitive literal")
            value = expression[pos : end + 1]
            if '"' in value[3:-1]:
                raise AbnfError(f"{source}:{line_no}:{pos + 1}: malformed case-sensitive literal")
            tokens.append(Token("literal", value, pos + 1))
            pos = end + 1
            continue
        if expression.startswith(("%s", "%S"), pos):
            raise AbnfError(
                f"{source}:{line_no}:{pos + 1}: case-sensitive literal requires a quoted value"
            )
        numeric = NUM_RE.match(expression, pos)
        if numeric:
            tokens.append(Token("numeric", numeric.group(0), pos + 1))
            pos = numeric.end()
            continue
        name = NAME_RE.match(expression, pos)
        if name:
            tokens.append(Token("name", name.group(0), pos + 1))
            pos = name.end()
            continue
        raise AbnfError(f"{source}:{line_no}:{pos + 1}: unsupported ABNF token {char!r}")
    if not tokens:
        raise AbnfError(f"{source}:{line_no}: empty production")
    return tokens


def validate_tokens(tokens: list[Token], source: str, line_no: int) -> set[str]:
    stack: list[str] = []
    references: set[str] = set()
    previous = "start"
    element_kinds = {"name", "literal", "numeric", ")", "]"}

    for index, token in enumerate(tokens):
        if token.kind in ("(", "["):
            stack.append(token.kind)
            previous = "open"
        elif token.kind in (")", "]"):
            expected = "(" if token.kind == ")" else "["
            if not stack or stack[-1] != expected:
                raise AbnfError(
                    f"{source}:{line_no}:{token.column}: unmatched {token.value}"
                )
            if previous in ("start", "open", "alternative", "repeat"):
                raise AbnfError(
                    f"{source}:{line_no}:{token.column}: empty or incomplete group"
                )
            stack.pop()
            previous = token.kind
        elif token.kind == "/":
            if previous not in element_kinds:
                raise AbnfError(
                    f"{source}:{line_no}:{token.column}: empty alternative"
                )
            previous = "alternative"
        elif token.kind == "repeat":
            if previous == "repeat":
                raise AbnfError(
                    f"{source}:{line_no}:{token.column}: repeated repetition prefix"
                )
            match = re.fullmatch(r"(?:(\d*)\*(\d*)|(\d+))", token.value)
            if not match:
                raise AbnfError(
                    f"{source}:{line_no}:{token.column}: invalid repetition"
                )
            if match.group(1) is not None:
                minimum = int(match.group(1) or "0")
                maximum = int(match.group(2)) if match.group(2) else None
                if maximum is not None and minimum > maximum:
                    raise AbnfError(
                        f"{source}:{line_no}:{token.column}: repetition minimum exceeds maximum"
                    )
            if index + 1 >= len(tokens) or tokens[index + 1].kind in ("/", ")", "]", "repeat"):
                raise AbnfError(
                    f"{source}:{line_no}:{token.column}: repetition has no element"
                )
            previous = "repeat"
        else:
            if token.kind == "name":
                references.add(token.value.lower())
            previous = token.kind

    if stack:
        raise AbnfError(f"{source}:{line_no}: unclosed group {stack[-1]}")
    if previous in ("alternative", "repeat", "open"):
        raise AbnfError(f"{source}:{line_no}: incomplete production")
    return references


def validate_text(text: str, source: str) -> tuple[int, int]:
    definitions: dict[str, int] = {}
    referenced: set[str] = set()
    productions = 0
    for line_no, raw_line in enumerate(text.splitlines(), 1):
        line = strip_comment(raw_line).strip()
        if not line:
            continue
        match = RULE_RE.fullmatch(line)
        if not match:
            raise AbnfError(f"{source}:{line_no}: expected 'rule = expression'")
        name = match.group(1).lower()
        if name in definitions:
            raise AbnfError(
                f"{source}:{line_no}: duplicate production {match.group(1)!r}; "
                f"first defined at line {definitions[name]}"
            )
        definitions[name] = line_no
        tokens = tokenize(match.group(2), source, line_no)
        referenced.update(validate_tokens(tokens, source, line_no))
        productions += 1

    if not definitions:
        raise AbnfError(f"{source}: no productions")
    unknown = sorted(referenced - set(definitions))
    if unknown:
        raise AbnfError(f"{source}: unknown rule reference(s): {', '.join(unknown)}")
    return productions, len(referenced)


def self_test() -> None:
    invalid = {
        "duplicate": "a = %s\"a\"\na = %s\"b\"\n",
        "unknown": "a = missing\n",
        "group": "a = (%s\"a\"\n",
        "repeat": "a = 3*2%s\"a\"\n",
        "case-literal": "a = %sfoo\n",
    }
    for name, sample in invalid.items():
        try:
            validate_text(sample, f"<self-test:{name}>")
        except AbnfError:
            continue
        raise RuntimeError(f"ABNF validator self-test did not reject {name}")


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: validate_maelys_datalog_v2_spec.py FILE.abnf [...]", file=sys.stderr)
        return 2
    self_test()
    total = 0
    for raw_path in argv[1:]:
        path = pathlib.Path(raw_path)
        try:
            text = path.read_text(encoding="utf-8")
            productions, _ = validate_text(text, str(path))
        except (OSError, UnicodeError, AbnfError) as exc:
            print(f"ABNF INVALID: {exc}", file=sys.stderr)
            return 1
        print(f"ABNF VALID {path.as_posix()} productions={productions}")
        total += productions
    print(f"ABNF VALID files={len(argv) - 1} productions={total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
