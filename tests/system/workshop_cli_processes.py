"""Exercise the actual CLI transport against disposable authored drafts."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

CLI = str(Path(sys.argv[1]).resolve())


def invoke(*arguments, code=0, stdin=None):
    result = subprocess.run([CLI, *map(str, arguments)], input=stdin, text=True, encoding="utf-8", capture_output=True)
    assert result.returncode == code, (result.returncode, result.stdout, result.stderr)
    lines = [json.loads(line) for line in result.stdout.splitlines()]
    assert lines
    return lines


with tempfile.TemporaryDirectory(prefix="phoenix-cli-") as temporary:
    root = Path(temporary)
    source = root / "draft-雪"
    target = {"kind": "entities", "id": "agent.ship"}
    invoke("create", "--args", json.dumps({"kind": "entities", "id": "agent.ship"}), "--output", source)
    assert invoke("target", "--input", source, "--args", json.dumps({"target": target}))[0]["ok"]
    initial = invoke("get", "--input", source, "--args", json.dumps({"target": target}))[-1]["result"]
    edits = [{"operation": "set", "arguments": {"target": target, "field": "name", "value": "Mercury 雪"}}]
    changes = root / "edits.json"
    changes.write_text(json.dumps(edits), encoding="utf-8")
    individual = root / "individual"
    invoke("batch", "--input", source, "--requests", changes, "--output", individual)
    persistent = root / "persistent"
    requests = [
        {"version": 1, "id": "read", "operation": "get", "arguments": {"target": target}},
        {"version": 1, "id": "edit", "operation": "batch", "arguments": {"requests": edits}},
        {"version": 1, "id": "undo", "operation": "undo"},
        {"version": 1, "id": "reread", "operation": "get", "arguments": {"target": target}},
        {"version": 1, "id": "redo", "operation": "redo"},
        {"version": 1, "id": "save", "operation": "save", "arguments": {"path": str(persistent)}},
    ]
    responses = invoke("session", "--input", source, stdin="{broken\n" + "\n".join(json.dumps(r) for r in requests) + "\n")
    assert not responses[0]["ok"] and responses[0]["error"]["code"] == "invalid_json"
    assert all(r["ok"] for r in responses[1:])
    assert responses[1]["result"] == initial == responses[4]["result"]
    assert json.loads((individual / "definitions.json").read_text()) == json.loads((persistent / "definitions.json").read_text())
    before = (individual / "definitions.json").read_bytes()
    invoke("set", "--input", individual, "--output", individual, "--args", json.dumps({
        "target": target, "field": "charge_per_second", "value": "bad type",
        "descriptor": "gameplay-editors.equipment.charge_per_second",
    }), code=1)
    assert before == (individual / "definitions.json").read_bytes()
    assert invoke("get", "--input", individual, "--args-file", "-", stdin=json.dumps({"target": target}))[0]["ok"]
    invoke("unknown-command", code=2)
    invoke("set", "--args", "{broken", code=2)

    assert invoke("defaults", "--args", json.dumps({"kind": "trees", "context": "scenario"}))[0]["result"]["context"] == "scenario"
    assert invoke("describe", "--args", json.dumps({"seeds": True}))[0]["result"]
    source_zip = root / "draft.zip"
    invoke("source.export", "--input", individual, "--output", source_zip)
    assert invoke("get", "--source", source_zip, "--args", json.dumps({"target": target}))[0]["result"]["name"] == "Mercury 雪"
    assert invoke("describe", "--args", json.dumps({"operation": "ship.inspect"}))[0]["result"]["execution"] == "query"

print("Workshop CLI one-shot/session parity, UTF-8 paths, recovery and failure persistence passed")
