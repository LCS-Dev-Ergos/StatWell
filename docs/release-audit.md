# First stable release audit

This is a required gate after consumer migration and before project closure
or the first stable release. Record evidence and unresolved findings in this
document when the audit runs; the checklist is a plan, not a completed audit.

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
