# Shared Snapshot Protocol - Version 1

The daemon publishes one UTF-8 JSON document at `snapshot.json` inside its
private runtime directory. This protocol is independent of the version-1
one-shot `statwell sample` output. A client may read the file directly or use
`statwell snapshot`. No socket request or acknowledgement is required, so
clients cannot hold up the sampler.

## Location and Publication

The default directory is `$XDG_RUNTIME_DIR/statwell-UID` on Linux and
`$TMPDIR/statwell-UID` on macOS, falling back to `/tmp/statwell-UID` if the
relevant variable is unset. `--runtime-dir` selects another absolute path.
The directory is owned by the current user with mode `0700`; `snapshot.json`
and `daemon.lock` are regular owner-only files with mode `0600`.

The daemon serializes into a temporary file in the same directory, syncs it,
then atomically renames it to `snapshot.json`. Readers see one complete
generation. The snapshot is removed on normal shutdown. The lock indicates
whether a daemon still owns the directory; a leftover file after a crash does
not count as a running daemon.

## Document

```json
{
  "schema_version": 1,
  "instance_id": "a1b2-123456",
  "sequence": 7,
  "captured_at_unix_ms": 1790606007069,
  "metrics": {
    "cpu": {
      "sequence": 4,
      "sampled_at_unix_ms": 1790606007069,
      "value_at_unix_ms": 1790606007069,
      "max_age_ms": 6000,
      "sample_duration_us": 31,
      "status": "ok",
      "value": {
        "user_percent": 18.5,
        "system_percent": 6.5,
        "total_percent": 25.0
      }
    }
  }
}
```

The six system metric names are always present: `cpu`, `memory`, `load`,
`disk`, `battery`, `network`. Optional `homebrew` and `pacman` records appear
when those providers are enabled. The example omits other records for brevity.

- `instance_id` changes on daemon restart. A one-shot fallback uses a fresh
  instance ID on each invocation.
- The top-level `sequence` advances on every publication. Each metric's
  `sequence` advances only when that probe is attempted.
- `sampled_at_unix_ms` is the latest attempt. `value_at_unix_ms` is the
  latest successful reading, or zero before any success. Both are Unix
  milliseconds.
- `max_age_ms` is three times that metric's configured cadence. A value is
  fresh only when `status == "ok"`, `value_at_unix_ms > 0`, and the reader's
  current Unix time is between `value_at_unix_ms` and
  `value_at_unix_ms + max_age_ms`. A future timestamp is stale too.
- `sample_duration_us` is the most recent probe call's elapsed monotonic time
  in microseconds; it excludes JSON serialization and file publication. For
  package providers it covers the background command and parsing after the
  check completes.
- `status` is `ok`, `error`, or `unavailable` before the first attempt.
  On failure, `error` is a stable error category and `native_code` is an
  optional OS-specific number. A previous successful `value` remains
  present with its original `value_at_unix_ms`; clients should display the
  error or explicitly mark the retained value as stale.
- A missing or unsupported reading never becomes numeric zero.

CPU fields are percentages. Memory and disk sizes use bytes. Load fields are
one-, five-, and fifteen-minute averages. Battery has integer `percent`,
`charging`, and `external_power` booleans. Network rates are bytes per
second for the selected interface. The stable error categories are
`unavailable`, `unsupported`, `invalid_input`, and `system_failure`.
Homebrew has `total`, `formulae`, and `casks` update counts; pacman has
`total`. A successful zero is distinct from an unavailable or failed check.
Package checks have a one-hour default cadence and are absent unless enabled
by the daemon's `--provider` option.

Clients should reject unknown major `schema_version` values. They may
ignore additional fields in version 1. For a missing or stopped daemon,
`statwell snapshot` performs one-shot sampling and returns the same schema.
Direct file readers should make their own fallback call when the file is
missing or its instance has stopped. Readers should reject malformed JSON;
`statwell snapshot` rejects an unsupported schema prefix, insecure permissions
and oversized files.
