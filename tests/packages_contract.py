#!/usr/bin/env python3
"""Exercise package command parsing, deadlines, and daemon isolation."""

import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import tempfile
import time


def executable(root: Path, name: str, body: str) -> str:
    path = root / name
    path.write_text("#!/usr/bin/env python3\n" + body)
    path.chmod(0o700)
    return str(path)


def invoke(binary: str, metric: str, path: str, timeout: int = 1000) -> subprocess.CompletedProcess[str]:
    option = "--homebrew-bin" if metric == "homebrew" else "--checkupdates-bin"
    return subprocess.run(
        [binary, "sample", "--metric", metric, option, path, "--package-timeout-ms", str(timeout)],
        capture_output=True, text=True, timeout=5,
    )


def metric(response: subprocess.CompletedProcess[str], name: str) -> dict:
    assert response.stdout, response.stderr
    return json.loads(response.stdout)["metrics"][name]


def wait_snapshot(path: Path, predicate, deadline_seconds: float = 5) -> dict:
    deadline = time.monotonic() + deadline_seconds
    while time.monotonic() < deadline:
        if path.exists():
            try:
                document = json.loads(path.read_text())
                if predicate(document):
                    return document
            except (json.JSONDecodeError, KeyError):
                pass
        time.sleep(0.02)
    raise AssertionError("expected snapshot state did not appear")


def main() -> None:
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="statwell-packages-") as directory:
        root = Path(directory)
        brew = executable(root, "brew", """
import json, sys
assert sys.argv[1:] == ['outdated', '--json=v2']
print(json.dumps({'formulae': [{'name': 'a', 'nested': {'key': [1, 2]}}],
                  'casks': [{'name': 'b'}, {'name': 'c'}]}))
""")
        result = invoke(binary, "homebrew", brew)
        assert result.returncode == 0, result.stderr
        assert metric(result, "homebrew")["total"] == 3
        assert metric(result, "homebrew")["formulae"] == 1
        assert metric(result, "homebrew")["casks"] == 2

        empty_brew = executable(root, "empty-brew", "print('{\"formulae\":[],\"casks\":[]}')\n")
        result = invoke(binary, "homebrew", empty_brew)
        assert result.returncode == 0
        assert metric(result, "homebrew")["total"] == 0

        # Closing stdout before process exit must wait without a POLLHUP spin.
        closed_output = executable(root, "closed-output", """
import os, time
os.write(1, b'{"formulae":[],"casks":[]}')
os.close(1)
time.sleep(0.8)
""")
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        result = invoke(binary, "homebrew", closed_output, timeout=2000)
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        assert result.returncode == 0, result.stderr
        cpu_seconds = after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime
        assert cpu_seconds < 0.5, f"closed stdout caused a busy wait: {cpu_seconds:.3f}s CPU"

        # The pipe must work when the caller has an unused standard descriptor.
        for descriptor in (0, 1, 2):
            result = subprocess.run(
                [binary, "sample", "--metric", "homebrew", "--homebrew-bin", empty_brew],
                capture_output=True, text=True, timeout=5,
                preexec_fn=lambda descriptor=descriptor: os.close(descriptor),
            )
            assert result.returncode == 0, (descriptor, result.stderr)

        # Concurrent workers must not inherit each other's pipe endpoints.
        inspect_body = """
import os
for descriptor in range(3, 64):
    try:
        os.fstat(descriptor)
    except OSError:
        continue
    raise SystemExit(1)
"""
        inspected_brew = executable(root, "inspected-brew", inspect_body + 'print(\'{"formulae":[],"casks":[]}\')\n')
        inspected_pacman = executable(root, "inspected-pacman", inspect_body + 'raise SystemExit(2)\n')
        for _ in range(3):
            result = subprocess.run(
                [binary, "snapshot", "--runtime-dir", str(root / "isolated"),
                 "--provider", "homebrew", "--provider", "pacman",
                 "--homebrew-bin", inspected_brew, "--checkupdates-bin", inspected_pacman],
                capture_output=True, text=True, timeout=5,
            )
            assert result.returncode == 0, result.stderr
            readings = json.loads(result.stdout)["metrics"]
            for name in ("homebrew", "pacman"):
                assert readings[name]["status"] == "ok", readings[name]

        malformed = executable(root, "malformed", "print('{\"formulae\":[],\"casks\":[1]}')\n")
        result = invoke(binary, "homebrew", malformed)
        assert result.returncode == 1
        assert metric(result, "homebrew")["error"] == "invalid_input"

        huge = executable(root, "huge", "import sys\nsys.stdout.write('x' * 4200000)\n")
        result = invoke(binary, "homebrew", huge)
        assert result.returncode == 1
        assert metric(result, "homebrew")["status"] == "error"

        updates = executable(root, "checkupdates", """
import sys
assert sys.argv[1:] == ['--nocolor']
print('linux 1 -> 2')
print('pacman 7 -> 8')
""")
        result = invoke(binary, "pacman", updates)
        assert result.returncode == 0
        assert metric(result, "pacman")["total"] == 2

        for output in ("database sync pending", "pkg old => new", "pkg old ->", "pkg old -> new extra", "pkg old -> new\x1b"):
            malformed_pacman = executable(root, "malformed-pacman", f"print({output!r})\n")
            result = invoke(binary, "pacman", malformed_pacman)
            assert result.returncode == 1, result.stdout
            assert metric(result, "pacman")["error"] == "invalid_input"

        no_updates = executable(root, "no-updates", "import sys\nsys.exit(2)\n")
        result = invoke(binary, "pacman", no_updates)
        assert result.returncode == 0
        assert metric(result, "pacman")["total"] == 0

        failure = executable(root, "failure", "import sys\nsys.exit(1)\n")
        result = invoke(binary, "pacman", failure)
        assert result.returncode == 1
        assert metric(result, "pacman")["status"] == "error"

        sleeping = executable(root, "sleeping", "import time\ntime.sleep(3)\n")
        started = time.monotonic()
        result = invoke(binary, "homebrew", sleeping, timeout=150)
        assert result.returncode == 1
        assert time.monotonic() - started < 1.5
        assert metric(result, "homebrew")["status"] == "error"

        missing = invoke(binary, "pacman", str(root / "missing"))
        assert missing.returncode == 1
        assert metric(missing, "pacman")["status"] == "error"

        runtime = root / "runtime"
        daemon = subprocess.Popen(
            [binary, "daemon", "--runtime-dir", str(runtime), "--provider", "homebrew",
             "--homebrew-bin", sleeping, "--package-timeout-ms", "3000", "--cadence", "cpu=100"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        try:
            path = runtime / "snapshot.json"
            first = wait_snapshot(path, lambda document: "homebrew" in document["metrics"])
            assert first["metrics"]["homebrew"]["status"] == "unavailable"
            advanced = wait_snapshot(path, lambda document: document["metrics"]["cpu"]["sequence"]
                                     >= first["metrics"]["cpu"]["sequence"] + 3)
            assert advanced["metrics"]["homebrew"]["status"] == "unavailable"
            started = time.monotonic()
        finally:
            daemon.send_signal(signal.SIGTERM)
            daemon.communicate(timeout=5)
        assert daemon.returncode == 0
        assert time.monotonic() - started < 1.5

        counter = root / "checks.txt"
        changing = executable(root, "changing-brew", f"""
from pathlib import Path
counter = Path({str(counter)!r})
attempt = int(counter.read_text()) + 1 if counter.exists() else 1
counter.write_text(str(attempt))
print('{{"formulae":[{{"name":"first"}}],"casks":[]}}' if attempt == 1 else 'bad-json')
""")
        active = root / "active"
        daemon = subprocess.Popen(
            [binary, "daemon", "--runtime-dir", str(active), "--provider", "homebrew",
             "--homebrew-bin", changing, "--cadence", "homebrew=300"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        try:
            path = active / "snapshot.json"
            good = wait_snapshot(path, lambda document: document["metrics"]["homebrew"]["status"] == "ok")
            assert good["metrics"]["homebrew"]["value"] == {"total": 1, "formulae": 1, "casks": 0}
            bad = wait_snapshot(path, lambda document: document["metrics"]["homebrew"]["status"] == "error")
            assert bad["metrics"]["homebrew"]["value"] == good["metrics"]["homebrew"]["value"]
            assert bad["metrics"]["homebrew"]["value_at_unix_ms"] == good["metrics"]["homebrew"]["value_at_unix_ms"]
            assert bad["metrics"]["homebrew"]["sequence"] > good["metrics"]["homebrew"]["sequence"]
        finally:
            daemon.terminate()
            daemon.communicate(timeout=5)
        assert daemon.returncode == 0

        fallback = subprocess.run(
            [binary, "snapshot", "--runtime-dir", str(root / "fallback"), "--provider", "pacman",
             "--checkupdates-bin", no_updates], capture_output=True, text=True, timeout=5,
        )
        assert fallback.returncode == 0, fallback.stderr
        assert json.loads(fallback.stdout)["metrics"]["pacman"]["value"]["total"] == 0


if __name__ == "__main__":
    main()
