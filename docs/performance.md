# Daemon measurements

Measured on 2026-09-28 on the aarch64 macOS development host. The release
binary was built with Clang 23, run as an ordinary user with default cadences,
and configured for the live `en0` interface. After two seconds of warm-up,
the process was observed once per second for twelve seconds. CPU percentage
and RSS came from `ps -p PID -o %cpu=,rss=`. Probe durations came from distinct
per-metric sequences in `snapshot.json`; they exclude serialization and file
publication.

| Process measure | Median | Maximum |
| --- | ---: | ---: |
| CPU | 0.0% | 0.7% |
| RSS | 5,624 KiB | 5,648 KiB |

| Probe | Observations | Median duration | Maximum duration |
| --- | ---: | ---: | ---: |
| CPU | 6 | 99.5 µs | 172 µs |
| Memory | 3 | 71 µs | 76 µs |
| Load | 3 | 4 µs | 5 µs |
| Disk | 1 | 4 µs | 4 µs |
| Battery | 1 | 346 µs | 346 µs |
| Network | 6 | 314 µs | 594 µs |

Every listed probe had status `ok` in the last observed snapshot. Disk and
battery have one observation because their default cadences are 60 and 30
seconds. This short host-specific run does not establish long-term power use,
Linux performance, or worst-case latency.

## Package-provider run

Measured on the same host with the final phase-four Clang release binary.
The daemon used the live `en0` interface and enabled Homebrew at its default
one-hour cadence. `HOMEBREW_NO_AUTO_UPDATE=1` kept this run focused on an
already installed Homebrew metadata snapshot. After Homebrew completed and
two seconds of warm-up, CPU and RSS were read with `ps -p PID -o %cpu=,rss=`
once per second for twelve seconds. Durations came from distinct metric
sequences in the snapshots over that interval; Homebrew's completed check
was recorded before the idle observations.

| Process measure | Median | Maximum |
| --- | ---: | ---: |
| CPU | 0.0% | 0.0% |
| RSS | 4,800 KiB | 4,864 KiB |

| Probe | Observations | Median duration | Maximum duration |
| --- | ---: | ---: | ---: |
| CPU | 7 | 88 µs | 279 µs |
| Memory | 3 | 22 µs | 116 µs |
| Load | 3 | 1 µs | 3 µs |
| Disk | 1 | 6 µs | 6 µs |
| Battery | 1 | 402 µs | 402 µs |
| Network | 7 | 206 µs | 1,102 µs |
| Homebrew | 1 | 1,347,983 µs | 1,347,983 µs |

The Homebrew reading succeeded with one outdated formula and no casks. The
command ran on a worker while system probes continued on their cadence.
The idle RSS figures are from a separate run and should not be compared as a
controlled before/after difference; macOS process accounting and host load
vary. This does not establish Linux or long-term resource use.
