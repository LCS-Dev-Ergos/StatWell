# StatWell

StatWell is a native C++ system-status library and command-line tool for macOS
and Linux. Its goal is to sample metrics once and make the same typed data
available to terminal bars, desktop bars, and other local clients.

The current implementation samples CPU, memory, load, disk, battery, and a
named network interface on macOS and Linux through a one-shot CLI. The shared
daemon, package-update providers, services, and consumer integrations are
upcoming phases. The CLI output below is version 1; it is not yet the daemon
snapshot protocol.

## Build

Requirements: CMake 3.25+, Ninja, a C++23 compiler, and GoogleTest when
building tests. The Makefile keeps all local build output in `builds/`:

```sh
make test
make run ARGS='--metric cpu --format json'
make test PROFILE=asan
```

Use `make help` for the profile and toolchain options. For a compiler whose
C++ ABI differs from an installed GoogleTest binary, set
`GTEST_SOURCE_DIR=/path/to/googletest` to compile the tests with that compiler.
For a plain CMake build without GoogleTest, configure with
`-DSTATWELL_BUILD_TESTS=OFF` and a binary directory under `builds/`.
The flake exposes `packages.aarch64-darwin.default`, `packages.x86_64-linux.default`,
and an overlay. A CMake install exports the `StatWell::statwell_core` target
for other C++ applications. Linux parsers have recorded `/proc` and `/sys`
fixtures and compile checks; a real Linux runtime check is still pending.

```sh
nix build .#statwell
```

## One-shot sampling

```sh
statwell sample --metric cpu --metric memory --format json
statwell sample --metric network --interface en0 --format kv
statwell --help
```

`--metric` can be repeated. With no selection, StatWell samples all implemented
metrics except network, which needs an interface name. CPU and network rates
use two readings separated by 200 ms by default; `--interval-ms` controls
that interval. Disk measures the home directory by default; use `--disk-path`
to select another filesystem. All byte counts use bytes, and network rates
use bytes per second. Errors have a code and never become a numeric zero.

The JSON and key/value formats both carry `schema_version` or
`schema.version` 1 and a Unix-millisecond capture time. Keys, units, and
error codes are part of their versioned contract. See `statwell --help` for
options and exit statuses.

## Code map

- `include/statwell/metrics.hpp` and `src/metrics.cpp`: typed values,
  calculations, and explicit errors, with no OS calls.
- `include/statwell/probes.hpp`: narrow probe functions and delta-sampling
  objects.
- `src/platform/darwin.cpp`: public Mach, sysctl, IOKit, `getifaddrs`, and
  `statvfs` implementations. Owned OS resources have RAII wrappers.
- `src/platform/linux.cpp`: bounded native reads from procfs and sysfs plus
  `statvfs`. `src/platform/linux_parsers.cpp` is shared with the
  fixture tests so malformed records can be checked on either platform.
- `src/cli/main.cpp`: one-shot CLI and versioned output.

The macOS backend uses kernel CPU counters, anonymous plus wired plus
compressed memory minus purgeable pages, and the kernel memory-pressure
level when available. A restricted process may be denied access to some
sysctls; pressure then reports `unknown`, while a denied interface-counter
read reports an error. Battery sampling selects an internal power source.
On Linux, `MemAvailable` defines available memory, and the optional memory
PSI `some avg10` value maps to normal below 1%, warning from 1% to below
10%, and critical at 10% or above. Missing or malformed PSI reports
`unknown`. A missing battery reports `unsupported`.

## Roadmap

1. Complete and harden the macOS probe library and CLI.
2. Validate the Linux probes on a real Linux host; their recorded `/proc` and
   `/sys` fixtures already cover parsing and malformed input.
3. Add the shared daemon, versioned snapshot protocol, launchd and systemd
   user services, and a SketchyBar Mach watcher.
4. Add independent, timeout-bound Homebrew and pacman update providers.
5. Add the Home Manager module and finish Nix packaging.
6. Migrate SketchyBar and Kitty one consumer at a time, retaining a tested
   rollback path until the replacements are verified on the real surfaces.

The Nix package-update provider is intentionally outside the current scope.

## License

MIT; see [LICENSE](LICENSE).
