#!/usr/bin/env python3
"""Exercise the owner-only snapshot contract against a real daemon process."""

import json
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


def invoke(binary: str, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([binary, *arguments], capture_output=True, text=True, timeout=5)


def main() -> None:
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="statwell-test-") as root:
        runtime = Path(root) / "runtime"
        daemon = subprocess.Popen(
            [binary, "daemon", "--runtime-dir", str(runtime), "--cadence", "cpu=100"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            deadline = time.monotonic() + 5
            first = None
            while time.monotonic() < deadline:
                if daemon.poll() is not None:
                    raise AssertionError(f"daemon exited: {daemon.communicate()}")
                path = runtime / "snapshot.json"
                if path.exists():
                    first = json.loads(path.read_text())
                    break
                time.sleep(0.02)
            assert first is not None, "daemon did not publish a snapshot"
            assert first["schema_version"] == 1
            assert first["instance_id"]
            assert set(first["metrics"]) == {"cpu", "memory", "load", "disk", "battery", "network"}
            assert (runtime.stat().st_mode & 0o777) == 0o700
            assert (path.stat().st_mode & 0o777) == 0o600
            assert (runtime / "daemon.lock").stat().st_mode & 0o777 == 0o600

            snapshot = invoke(binary, "snapshot", "--runtime-dir", str(runtime))
            assert snapshot.returncode == 0, snapshot.stderr
            assert json.loads(snapshot.stdout)["instance_id"] == first["instance_id"]

            duplicate = invoke(binary, "daemon", "--runtime-dir", str(runtime))
            assert duplicate.returncode != 0

            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                current = json.loads(path.read_text())
                if current["metrics"]["cpu"]["sequence"] > first["metrics"]["cpu"]["sequence"]:
                    break
                time.sleep(0.02)
            else:
                raise AssertionError("CPU cadence did not advance")
            assert current["metrics"]["cpu"]["max_age_ms"] == 300
            assert current["metrics"]["memory"]["max_age_ms"] == 15_000
            assert current["metrics"]["cpu"]["value_at_unix_ms"] > 0
            assert current["metrics"]["cpu"]["sample_duration_us"] >= 0

            with ThreadPoolExecutor(max_workers=4) as clients:
                responses = list(clients.map(
                    lambda _: invoke(binary, "snapshot", "--runtime-dir", str(runtime)), range(12),
                ))
            for response in responses:
                assert response.returncode == 0, response.stderr
                assert json.loads(response.stdout)["schema_version"] == 1

            # Pause/resume checks scheduler recovery without suspending the host.
            daemon.send_signal(signal.SIGSTOP)
            _, stopped = os.waitpid(daemon.pid, os.WUNTRACED)
            assert os.WIFSTOPPED(stopped)
            paused = json.loads(path.read_text())
            time.sleep(0.4)
            assert json.loads(path.read_text())["sequence"] == paused["sequence"]
            daemon.send_signal(signal.SIGCONT)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                resumed = json.loads(path.read_text())
                if resumed["sequence"] > paused["sequence"]:
                    break
                time.sleep(0.02)
            else:
                raise AssertionError("daemon did not publish after resume")
        finally:
            daemon.send_signal(signal.SIGCONT)
            daemon.send_signal(signal.SIGTERM)
            daemon.communicate(timeout=5)
        assert daemon.returncode == 0
        assert not (runtime / "snapshot.json").exists()
        fallback = invoke(binary, "snapshot", "--runtime-dir", str(runtime))
        assert fallback.returncode == 0, fallback.stderr
        assert json.loads(fallback.stdout)["instance_id"] != first["instance_id"]

        crashed = subprocess.Popen(
            [binary, "daemon", "--runtime-dir", str(runtime)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline and not (runtime / "snapshot.json").exists():
                time.sleep(0.02)
            assert (runtime / "snapshot.json").exists()
            crashed_id = json.loads((runtime / "snapshot.json").read_text())["instance_id"]
        finally:
            crashed.kill()
            crashed.communicate(timeout=5)
        after_crash = invoke(binary, "snapshot", "--runtime-dir", str(runtime))
        assert after_crash.returncode == 0, after_crash.stderr
        assert json.loads(after_crash.stdout)["instance_id"] != crashed_id

        restarted = subprocess.Popen(
            [binary, "daemon", "--runtime-dir", str(runtime)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if restarted.poll() is not None:
                    raise AssertionError(f"daemon restart failed: {restarted.communicate()}")
                if json.loads((runtime / "snapshot.json").read_text())["instance_id"] != crashed_id:
                    break
                time.sleep(0.02)
        finally:
            restarted.terminate()
            restarted.communicate(timeout=5)
        assert restarted.returncode == 0

        insecure = Path(root) / "insecure"
        insecure.mkdir(mode=0o755)
        insecure.chmod(0o755)
        response = invoke(binary, "daemon", "--runtime-dir", str(insecure))
        assert response.returncode != 0
        assert "0700" in response.stderr
        unknown = invoke(binary, "snapshot", "--cadence", "unknown=1000")
        assert unknown.returncode == 2


if __name__ == "__main__":
    main()
