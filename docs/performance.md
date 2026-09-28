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
