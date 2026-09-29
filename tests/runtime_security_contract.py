#!/usr/bin/env python3
"""Reject invalid runtime files promptly, before reading or changing them."""

import fcntl
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rejected(binary: str, command: str, runtime: Path) -> None:
    try:
        result = subprocess.run(
            [binary, command, "--runtime-dir", str(runtime)],
            capture_output=True, text=True, timeout=2,
        )
    except subprocess.TimeoutExpired as error:
        raise AssertionError(f"{command} blocked on an invalid runtime file") from error
    assert result.returncode == 1, (result.returncode, result.stderr)


def main() -> None:
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="statwell-security-") as temporary:
        root = Path(temporary)
        runtime = root / "runtime"
        runtime.mkdir(mode=0o700)
        lock_path = runtime / "daemon.lock"
        snapshot_path = runtime / "snapshot.json"

        os.mkfifo(lock_path, 0o600)
        rejected(binary, "snapshot", runtime)
        lock_path.unlink()

        lock_path.touch(mode=0o600)
        with lock_path.open("r+") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            os.mkfifo(snapshot_path, 0o600)
            rejected(binary, "snapshot", runtime)
            snapshot_path.unlink()

            outside = root / "outside"
            outside.write_text('{"schema_version":1,"metrics":{}}')
            outside.chmod(0o600)
            snapshot_path.symlink_to(outside)
            rejected(binary, "snapshot", runtime)
            snapshot_path.unlink()

            snapshot_path.write_text("x" * 131_073)
            snapshot_path.chmod(0o600)
            rejected(binary, "snapshot", runtime)
            snapshot_path.unlink()

        lock_path.unlink()
        target = root / "linked-lock"
        target.write_text("preserve these bytes and permissions")
        target.chmod(0o644)
        os.link(target, lock_path)
        rejected(binary, "daemon", runtime)
        assert target.stat().st_mode & 0o777 == 0o644
        assert target.read_text() == "preserve these bytes and permissions"
        lock_path.unlink()
        lock_path.touch(mode=0o600)

        link = root / "runtime-link"
        link.symlink_to(runtime, target_is_directory=True)
        rejected(binary, "snapshot", link)
        rejected(binary, "snapshot", str(link) + "/")


if __name__ == "__main__":
    main()
