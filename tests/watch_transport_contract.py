#!/usr/bin/env python3
"""Test real Mach reconnect/replay with a private receiver, without a live bar."""

from contextlib import contextmanager
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time
import uuid


@contextmanager
def process(arguments, **options):
    child = subprocess.Popen(arguments, stderr=subprocess.PIPE, **options)
    try:
        yield child
    finally:
        if child.poll() is None:
            child.terminate()
        try:
            child.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.communicate(timeout=5)


class Messages:
    """Read pipe bytes directly: TextIO buffering hides lines from select()."""

    def __init__(self, child):
        self.child = child
        self.pending = b""
        self.recent = []

    def wait(self, predicate, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            while b"\n" in self.pending:
                raw, self.pending = self.pending.split(b"\n", 1)
                line = raw.decode("utf-8")
                self.recent = (self.recent + [line])[-5:]
                if predicate(line):
                    return line
            if self.child.poll() is not None:
                raise AssertionError(f"Mach receiver exited: {self.child.stderr.read().decode()}")
            if select.select([self.child.stdout], [], [], 0.1)[0]:
                self.pending += os.read(self.child.stdout.fileno(), 65536)
        raise AssertionError(f"expected Mach message not received; last messages: {self.recent}")


def event(line):
    arguments = line.split("|")
    if arguments[:2] != ["--trigger", "sample"]:
        return {}
    return dict(value.split("=", 1) for value in arguments[2:] if "=" in value)


def wait_snapshot(path, daemon):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if daemon.poll() is not None:
            raise AssertionError(f"daemon exited: {daemon.stderr.read().decode()}")
        try:
            document = json.loads(path.read_text())
            if document["metrics"]["homebrew"]["status"] == "ok":
                return document
        except (FileNotFoundError, KeyError, json.JSONDecodeError):
            pass
        time.sleep(0.02)
    raise AssertionError("daemon did not publish its package sample")


def main():
    binary, receiver_binary = sys.argv[1:]
    name = "statwell_test_" + uuid.uuid4().hex
    with tempfile.TemporaryDirectory(prefix="statwell-mach-") as directory:
        root = Path(directory)
        runtime = root / "runtime"
        brew = root / "brew"
        brew.write_text(
            "#!/bin/sh\nsleep 1\n"
            "printf '{\"formulae\":[{\"name\":\"a\"},{\"name\":\"b\"}],\"casks\":[]}\\n'\n"
        )
        brew.chmod(0o700)
        with process(
            [binary, "daemon", "--runtime-dir", str(runtime), "--provider", "homebrew",
             "--homebrew-bin", str(brew)], stdout=subprocess.DEVNULL,
        ) as daemon:
            snapshot = wait_snapshot(runtime / "snapshot.json", daemon)
            with process(
                [binary, "watch", "--runtime-dir", str(runtime), "--metric", "homebrew", "--event", "sample"],
                env={**os.environ, "BAR_NAME": name}, stdout=subprocess.DEVNULL,
            ) as watcher:
                # Startup without a receiver must not terminate the watcher.
                time.sleep(0.5)
                assert watcher.poll() is None, "watcher exited while Mach service was absent"
                with process([receiver_binary, name], stdout=subprocess.PIPE) as receiver:
                    messages = Messages(receiver)
                    messages.wait(lambda line: line == "READY")
                    messages.wait(lambda line: line.startswith("--add|event|sample|"))
                    first = event(messages.wait(lambda line: event(line).get("status") == "ok", 10))
                    assert first["total"] == "2"
                    assert first["instance_id"] == snapshot["instance_id"]
                    assert first["sequence"] == str(snapshot["metrics"]["homebrew"]["sequence"])
                    assert first["refreshing"] == "false" and int(first["captured_at_unix_ms"]) > 0

                # No new package sample: replay must recover a restarted receiver.
                with process([receiver_binary, name], stdout=subprocess.PIPE) as receiver:
                    messages = Messages(receiver)
                    messages.wait(lambda line: line == "READY")
                    messages.wait(lambda line: line.startswith("--add|event|sample|"), 35)
                    replay = event(messages.wait(lambda line: event(line).get("status") == "ok"))
                    assert replay["sequence"] == first["sequence"] and replay["total"] == "2"
                    assert watcher.poll() is None

                    # Pending refresh changes the event even before its sequence advances.
                    refresh = subprocess.run(
                        [binary, "refresh", "--runtime-dir", str(runtime), "--provider", "homebrew"],
                        capture_output=True, text=True, timeout=5,
                    )
                    assert refresh.returncode == 0, refresh.stderr
                    pending = event(messages.wait(lambda line: event(line).get("refreshing") == "true"))
                    assert pending["sequence"] == first["sequence"]
                    completed = event(messages.wait(
                        lambda line: event(line).get("refreshing") == "false"
                        and event(line).get("sequence") != first["sequence"],
                    ))
                    assert completed["status"] == "ok" and completed["total"] == "2"
                    assert completed["instance_id"] == first["instance_id"]
    print("Mach startup recovery, unchanged-sample replay and refresh transitions: PASS")


if __name__ == "__main__":
    main()
