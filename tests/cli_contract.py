"""Check the public one-shot output and error contract through the executable."""

import json
import subprocess
import sys
import tempfile
from pathlib import Path


binary = sys.argv[1]


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([binary, "sample", *args], capture_output=True, text=True)


help_result = subprocess.run([binary, "--help"], capture_output=True, text=True)
assert help_result.returncode == 0, help_result.stderr
for command in ("sample", "snapshot", "daemon", "watch"):
    assert f"  {command}" in help_result.stdout
for heading in ("SAMPLE OPTIONS", "SERVICE OPTIONS", "SHARED OPTIONS", "OUTPUT AND EXIT STATUS"):
    assert heading in help_result.stdout
assert "--interface en0" in help_result.stdout
assert max(map(len, help_result.stdout.splitlines())) <= 88


sample = run("--metric", "disk", "--disk-path", "/", "--format", "json")
assert sample.returncode == 0, sample.stderr
payload = json.loads(sample.stdout)
assert payload["schema_version"] == 1
assert isinstance(payload["captured_at_unix_ms"], int)
assert list(payload["metrics"]) == ["disk"]
assert payload["metrics"]["disk"]["status"] == "ok"
assert payload["metrics"]["disk"]["total_bytes"] > 0

kv = run("--metric", "disk", "--disk-path", "/", "--format", "kv")
assert kv.returncode == 0, kv.stderr
lines = dict(line.split("=", 1) for line in kv.stdout.splitlines())
assert lines["schema.version"] == "1"
assert lines["disk.status"] == "ok"
assert int(lines["disk.available_bytes"]) >= 0

with tempfile.TemporaryDirectory() as temporary:
    missing = str(Path(temporary) / "missing")
    failed = run("--metric", "disk", "--disk-path", missing)
    assert failed.returncode == 1
    error = json.loads(failed.stdout)["metrics"]["disk"]
    assert error["status"] == "error"
    assert error["error"] == "system_failure"
    assert "available_bytes" not in error

invalid = run("--metric", "network")
assert invalid.returncode == 2
assert "invalid arguments" in invalid.stderr
