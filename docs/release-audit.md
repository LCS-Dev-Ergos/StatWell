# First stable release audit

This is a required gate after consumer migration and before project closure
or the first stable release. Record evidence and unresolved findings in this
document when the audit runs; the checklist is a plan, not a completed audit.

## Audit progress, 2026-09-28

The audit is in progress on `LCSMacBookPro.local` (aarch64 macOS). The Linux
CI from phase five passed before the audit changes, but a fresh Linux run and
the real `lcs-legion-arch` Home Manager switch are still required. No stable
release has been tagged.

### Security and correctness evidence

- Source review covered the unprivileged launchd/systemd user services, the
  owner-only runtime directory (`0700`), lock and snapshot (`0600`), `openat`
  and `O_NOFOLLOW` checks, atomic rename and `fsync`, and bounded snapshot
  reads. The package runners use fixed argument vectors without a shell,
  process groups, deadlines, a 4 MiB output cap, and bounded JSON parsing.
- `tests/daemon_contract.py` exercises owner-only modes, concurrent clients,
  duplicate daemons, clean shutdown, crash recovery, and restart.
  `tests/packages_contract.py` exercises Homebrew and pacman zero, update,
  malformed output, oversized output, missing executable, timeout, worker
  isolation, and retained last value. The Nix update provider is excluded.
- A release CLI sample reported `memory.total_bytes=17179869184`, matching
  `sysctl -n hw.memsize` exactly. Disk total was `894662582272` bytes,
  matching `df -k "$HOME"` after conversion by 1024; available space varied
  slightly between the two reads. Battery was 100% on external power, in
  agreement with `pmset -g batt`. The named `en0` network probe returned `ok`.
- The user's prior macOS switch and visual checks confirmed the migrated
  SketchyBar widgets, the battery popup, and Kitty's memory, load, disk, and
  battery segments. `docs/STATWELL_MIGRATION.md` in Dotfiles records the
  consumer rollback paths. The Arch surface has not been observed live.

### Robustness and build evidence

- `make test PROFILE=debug`, `asan`, `tsan`, and `gcc` each passed all five
  CTest targets after the watch fix. GCC on macOS used
  `GTEST_SOURCE_DIR` from the pinned Nix source to avoid mixing libstdc++
  with Homebrew's libc++ GoogleTest archive.
- `make format-check` passed. Clang-tidy reported no findings in project
  code, including `tests/watch_test.cpp`; generated warnings were suppressed
  as non-user code. `cppcheck --enable=warning,style,performance,portability`
  reported no findings in `src` or `include`.
- `nix flake check --all-systems --no-build` evaluated macOS and Linux
  packages and Home Manager checks. `nix build .#statwell --no-link` built the
  changed macOS package with Nix's pinned Clang 21. These results do not
  replace a Linux build of the changed revision.

### Performance evidence

The changed Clang 23 release binary ran with default probe cadences, `en0`,
and no package provider in a separate temporary runtime directory. After two
seconds of warm-up, `ps -p PID -o %cpu=,rss=` was sampled once per second for
20 seconds. Distinct metric sequences supplied probe durations. CPU median
was 0.2% (maximum 0.5%); RSS median was 4,800 KiB (maximum 5,680 KiB).

| Probe | Samples | Median | Maximum |
| --- | ---: | ---: | ---: |
| CPU | 10 | 87 µs | 181 µs |
| Memory | 5 | 63 µs | 85 µs |
| Load | 5 | 3 µs | 5 µs |
| Disk | 1 | 5 µs | 5 µs |
| Battery | 1 | 356 µs | 356 µs |
| Network | 10 | 340.5 µs | 664 µs |

Reading and JSON parsing the private snapshot file 200 times took a median
30.7 µs (maximum 243.4 µs). Twenty `statwell snapshot` client processes took
a median 6.1 ms (maximum 9.0 ms) including process startup. The prior macOS
phase-three baseline was 0.0% median CPU and 5,624 KiB median RSS, with
similar probe durations; this was a different short run under different host
load, so the figures are not a controlled before/after comparison. The
phase-four Homebrew run and package-worker cadence test are the current
provider isolation evidence.

### Findings and open gates

- **Fixed in this audit branch:** A live but stalled daemon could leave a
  SketchyBar widget showing its last value indefinitely. The watcher now
  emits one additional event after the value has expired, with a one-second
  margin for SketchyBar's second-resolution Lua clock. A deterministic
  regression test checks the identity transition and prevents event spam.
- **Fixed in Dotfiles:** Kitty now kills an in-flight one-shot fallback as
  soon as the daemon's valid snapshot reappears; a running-process regression
  test verifies the cleanup.
- **Still required:** Re-run Linux CI on the changed StatWell revision; build,
  switch, and visually verify the `lcs-legion-arch` Home Manager generation;
  build and activate Dotfiles after pinning the corrected StatWell revision;
  and compare performance on the real Linux host. Direct snapshot publication
  latency and long-duration sleep/wake behavior have not yet been measured.

## Security

- Review the daemon's user privileges, runtime-directory ownership and modes,
  atomic snapshot publication, lock handling, symlink and path attacks, and
  behavior when another user controls an input path.
- Review every parser and package-command boundary for bounded input, fixed
  argument vectors, timeouts, cancellation, and accidental shell execution.
- Check launchd and systemd defaults, Home Manager options, dependencies,
  repository workflows, and release artifacts for excess permissions.

## Correctness

- Compare macOS and Linux probe results with independent host observations;
  verify units, thresholds, timestamps, stale detection, and all documented
  version-1 JSON and key/value fields.
- Test Homebrew and pacman zero, update, error, timeout, and last-valid-value
  cases. Keep the Nix package-update provider outside this release unless a
  separate design is approved.
- Verify the SketchyBar and Kitty adapters on real surfaces after the user's
  switch, including missing-daemon fallback and rollback to the old consumer.

## Robustness

- Exercise malformed and oversized procfs/sysfs and package output, missing
  permissions, process crashes, signals, sleep/wake, rapid restarts, concurrent
  clients, and service-manager restart behavior.
- Run both compiler toolchains with warnings as errors, sanitizers including
  ThreadSanitizer for the daemon, static analysis, Nix builds, and Linux CI.

## Performance

- Measure idle CPU and RSS, per-probe latency, snapshot write/read cost,
  client refresh cost, and package-provider isolation on both supported hosts.
- Compare measurements with the phase-three and phase-four baselines in
  `docs/performance.md`; investigate regressions rather than assuming an
  acceptable threshold.

## Exit criteria

Record the host, build, commands, measurements, and results for each check.
Resolve release-blocking findings, document remaining limits, verify a
rollback for each migrated consumer, then decide whether to tag a stable
version. No stable-release claim follows from the passing phase-five CI alone.
