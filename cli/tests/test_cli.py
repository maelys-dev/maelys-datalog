#!/usr/bin/env python3
"""Product behavior and data schemas; the spec kit covers the shared CLI."""
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "cli/tests/fixtures"
BINARY = pathlib.Path(sys.argv[1])
SPEC = pathlib.Path(sys.argv[2])
sys.path.insert(0, str(SPEC / "conformance"))
from validate import validate  # noqa: E402


def run(command, *arguments, exit_code=0):
    process = subprocess.run(
        [str(BINARY), command, *map(str, arguments), "--format", "json", "--compact"],
        capture_output=True, text=True, cwd=ROOT, check=False,
    )
    assert process.returncode == exit_code, (command, process.stdout, process.stderr)
    payload = process.stderr if exit_code == 1 else process.stdout
    assert not (process.stdout if exit_code == 1 else process.stderr), process
    result = json.loads(payload)
    envelope = json.loads((SPEC / "schemas/envelope.json").read_text())
    assert not validate(result, envelope), validate(result, envelope)
    assert result["command"] == command
    assert result["exitCode"] == exit_code
    assert result["ok"] == (exit_code != 1)
    if exit_code != 1:
        schema = json.loads((ROOT / f"cli/schemas/{command}.json").read_text())
        assert not validate(result["data"], schema), validate(result["data"], schema)
    return result.get("data", result.get("error"))


def text(command, *arguments, exit_code=0):
    process = subprocess.run([str(BINARY), command, *map(str, arguments)],
                             capture_output=True, text=True, cwd=ROOT, check=False)
    assert process.returncode == exit_code, (process.stdout, process.stderr)
    if exit_code == 1:
        assert process.stderr.startswith("maelys-datalog: ["), process.stderr
        assert not process.stdout
    else:
        assert process.stdout and not process.stderr, process
    return process.stderr if exit_code == 1 else process.stdout


def temporary_file(directory, name, content):
    path = pathlib.Path(directory) / name
    path.write_bytes(content if isinstance(content, bytes) else content.encode())
    return path


def basic():
    domain = FIXTURE / "rbac.domain.json"
    policy = FIXTURE / "rbac.dl"
    facts = FIXTURE / "rbac.facts.dl"
    checked = run("check", "--domain", domain, policy)
    assert checked["valid"] and checked["normalizedRuleCount"] == 2
    assert checked["policyCount"] == 1
    assert checked["domains"][0]["predicates"][3]["query"] is True
    identity = run("fingerprint", "--domain", domain, policy)
    assert identity["policyFingerprint"] == checked["policyFingerprint"]
    solved = run("solve", "--domain", domain, "--facts", facts, policy)
    assert solved["derivedFactCount"] == 2
    assert solved["policyFingerprint"] == identity["policyFingerprint"]
    assert solved["executionFingerprint"] == identity["executionFingerprint"]
    assert [q["predicate"] for q in solved["queries"]] == ["allow", "can_deliver"]
    assert solved["queries"][1]["facts"] == [[{"kind": "symbol", "value": "Leela"}]]
    selected = run("solve", "--domain", domain, "--facts", facts,
                   "--query", "can_deliver/1", policy)
    assert len(selected["queries"]) == 1
    yes = run("explain", "--domain", domain, "--facts", facts,
              "--why", "true", policy, "can_deliver", '"Leela"')
    no = run("explain", "--domain", domain, "--facts", facts,
             "--why", "false", policy, "can_deliver", '"Mallory"')
    assert yes["present"] and "status=complete" in yes["document"]
    assert not no["present"] and "status=complete" in no["document"]
    assert "document=why-false" in no["document"]
    for command, args in (
        ("check", ("--domain", domain, policy)),
        ("fingerprint", ("--domain", domain, policy)),
        ("solve", ("--domain", domain, "--facts", facts, policy)),
        ("explain", ("--domain", domain, "--facts", facts,
                     "--why", "false", policy, "can_deliver", '"Mallory"')),
    ):
        output = text(command, *args)
        assert re.sub(r"[0-9a-f]{64}", "<fingerprint>", output) == (
            FIXTURE / f"golden-{command}.txt").read_text()
        result = run(command, *args)
        for key in ("policyFingerprint", "executionFingerprint"):
            if key in result:
                result[key] = "<fingerprint>"
        assert result == json.loads((FIXTURE / f"golden-{command}.json").read_text())
    assert run("solve", "--domain", domain, "--facts", facts,
               "--query", "missing", policy, exit_code=1)["code"] == "NOT_FOUND"
    assert run("explain", "--domain", domain, "--facts", facts,
               "--why", "false", policy, "can_deliver", exit_code=1)["code"] == "VALIDATION_FAILED"
    assert run("solve", "--domain", domain, "--facts", facts,
               "--work-limit", "1", policy, exit_code=1)["code"] == "UNSUPPORTED"


def malformed():
    domain = FIXTURE / "rbac.domain.json"
    policy = FIXTURE / "rbac.dl"
    with tempfile.TemporaryDirectory() as temp:
        bad_policy = temporary_file(temp, "bad.dl", "allow(X) :- missing(X).\n")
        report = run("check", "--domain", domain, bad_policy, exit_code=2)
        assert not report["valid"] and report["diagnostics"][0]["line"] == 1
        text("check", "--domain", domain, bad_policy, exit_code=2)

        bad_domain = json.loads(domain.read_text())
        bad_domain["unknown"] = 1
        path = temporary_file(temp, "unknown.json", json.dumps(bad_domain))
        report = run("check", "--domain", path, policy, exit_code=2)
        assert report["diagnostics"][0]["code"] == "DOMAIN_INVALID"
        assert run("solve", "--domain", path, "--facts",
                   FIXTURE / "rbac.facts.dl", policy, exit_code=1)["code"] == "VALIDATION_FAILED"
        for name, content in (
            ("truncated.json", b'{"format":'),
            ("duplicate.json", b'{"format":"x","format":"y"}'),
            ("nul.json", b'{"name":"\\u0000"}'),
            ("utf8.json", b'{"name":"\xff"}'),
        ):
            path = temporary_file(temp, name, content)
            assert run("check", "--domain", path, policy, exit_code=1)["code"] == "VALIDATION_FAILED"
        bad_domain = json.loads(domain.read_text())
        bad_domain["predicates"].append(bad_domain["predicates"][0])
        path = temporary_file(temp, "duplicate-predicate.json", json.dumps(bad_domain))
        assert run("check", "--domain", path, policy, exit_code=2)["diagnostics"][0]["index"] == 5
        bad_domain = json.loads(domain.read_text())
        bad_domain["atoms"].append("admin")
        path = temporary_file(temp, "duplicate-atom.json", json.dumps(bad_domain))
        assert run("check", "--domain", path, policy, exit_code=2)["diagnostics"][0]["index"] == 3
        for name, content in (
            ("unterminated.dl", 'request("Leela).'),
            ("overflow.dl", "request(9223372036854775808, true)."),
            ("arity.dl", 'request("a", "b", "c", "d", "e").'),
            ("utf8.dl", b'request("\xff", "b").'),
            ("nul.dl", b'request("a", "b").\x00'),
        ):
            path = temporary_file(temp, name, content)
            error = run("solve", "--domain", domain, "--facts", path,
                        policy, exit_code=1)
            assert error["code"] == "VALIDATION_FAILED"
        assert run("check", "--domain", pathlib.Path(temp) / "missing.json",
                   policy, exit_code=1)["code"] == "IO_FAILED"
        blank = temporary_file(temp, "blank.dl", "\n\nrequest(\"Leela\", \"billing:read\").\n")
        assert run("solve", "--domain", domain, "--facts", blank,
                   policy)["derivedFactCount"] == 2


def integers_and_manifest():
    with tempfile.TemporaryDirectory() as temp:
        d1 = {
            "format": "maelys-datalog-domain-v1", "name": "numbers_cli",
            "predicates": [
                {"name": "input", "arity": 1, "role": "edb"},
                {"name": "allow", "arity": 1, "role": "idb", "query": True},
            ], "atoms": [],
        }
        domain = temporary_file(temp, "numbers.json", json.dumps(d1))
        source = "allow(X) :- input(X).\n"
        policy = temporary_file(temp, "numbers.dl", source)
        facts = temporary_file(temp, "bounds.dl",
                               "input(-9223372036854775808). input(9223372036854775807).")
        solved = run("solve", "--domain", domain, "--facts", facts, policy)
        assert [row[0]["value"] for row in solved["queries"][0]["facts"]] == [
            -9223372036854775808, 9223372036854775807]
        overflow = temporary_file(temp, "overflow.dl", "input(9223372036854775808).")
        assert "fact 0" in run("solve", "--domain", domain, "--facts", overflow,
                               policy, exit_code=1)["message"]
        d2 = dict(d1, name="numbers_two_cli")
        domain2 = temporary_file(temp, "numbers-two.json", json.dumps(d2))
        source2 = "allow(X) :- input(X).\n"
        policy2 = temporary_file(temp, "numbers-two.dl", source2)
        manifest = {
            "policy_set_id": "numbers", "policy_set_version": "1",
            "manifest_version": "1", "default_profile": "enforce",
            "created_for": "test", "strict_loading": True, "fail_closed": True,
            "capabilities": [], "policies": []
        }
        for index, (name, file, src) in enumerate((
            ("numbers_cli", policy, source), ("numbers_two_cli", policy2, source2))):
            manifest["policies"].append({
                "policy_id": f"policy{index}", "domain": name,
                "file": file.name, "sha256": hashlib.sha256(src.encode()).hexdigest(),
                "mode": "enforce", "enabled": True, "description": "CLI test",
                "queries": [{"name": "allow", "arity": 1}],
            })
        manifest_path = temporary_file(temp, "manifest.json", json.dumps(manifest))
        report = run("check", "--domain", domain, "--domain", domain2,
                     "--manifest", manifest_path)
        assert report["valid"] and report["policyCount"] == 2
        assert run("check", "--domain", domain, "--domain", domain2,
                   "--manifest", manifest_path, "--policy-id", "x",
                   exit_code=1)["code"] == "VALIDATION_FAILED"


if __name__ == "__main__":
    basic()
    malformed()
    integers_and_manifest()
    print("cli behavior and schemas: PASS")
