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

## 2026-09-29 audit run

Measured on arm64 macOS 27.2 with the audit branch's Clang 23.1.0 Release
binary. The daemon used default cadences, `en0`, no package provider, and
an isolated runtime directory. After two seconds of warm-up, CPU and RSS
were observed once per second for 30 seconds using `ps`. Some local build
activity overlapped the run; final one-minute host load was 14.15.

| Process measure | Samples | Median | Maximum |
| --- | ---: | ---: | ---: |
| CPU | 30 | 0.0% | 0.7% |
| RSS | 30 | 4,864 KiB | 5,040 KiB |

| Probe | Distinct metric sequences | Median | Maximum |
| --- | ---: | ---: | ---: |
| CPU | 15 | 63 µs | 164 µs |
| Memory | 7 | 51 µs | 127 µs |
| Load | 7 | 2 µs | 12 µs |
| Disk | 1 | 6 µs | 6 µs |
| Battery | 2 | 869 µs | 1,078 µs |
| Network | 15 | 315 µs | 1,388 µs |

Snapshot file reading plus Python JSON parsing took median 73.9 µs, maximum
303.5 µs over 200 reads. Twenty `statwell snapshot` processes took median
4.93 ms, maximum 11.33 ms including startup.

A reader polled every five milliseconds, recorded each new publication's
capture timestamp, and compared it with wall-clock observation time. For 21
publications, the interval was median 5.26 ms, maximum 7.62 ms. This includes
serialization, file publication, reader polling, and parsing; it does not
isolate syscall timing or establish a worst-case publication bound.

The EOF regression was measured separately: a provider wrote valid JSON,
closed stdout, and slept 0.8 seconds. Before the correction the runner used
0.832 s CPU in 0.842 s elapsed; the corrected Release run used 0.023 s CPU
in 0.848 s elapsed. `RUSAGE_CHILDREN` includes the waited provider, which
performed the same write/sleep workload in both runs.

All six final daemon metrics reported `ok`. The measurements are short
host-specific samples. Comparing them with the earlier runs does not isolate
a code regression from changing load, accounting, and compiler versions.
Physical Linux and long-duration measurements remain required.
