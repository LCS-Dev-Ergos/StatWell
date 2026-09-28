# StatWell

StatWell is a native C++ system-status library and command-line tool for macOS
and Linux. Its goal is to sample metrics once and make the same typed data
available to terminal bars, desktop bars, and other local clients.

The current implementation samples CPU, memory, load, disk, battery, a
named network interface, and optional Homebrew or pacman update counts. A user
daemon publishes those readings to an owner-only snapshot file. Consumer
integrations are upcoming phases. The one-shot CLI output and shared snapshot
protocol each have their own version-1 schema.

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
`make build` also links the selected profile's `compile_commands.json` into
the repository root for clangd. Run `make compdb PROFILE=debug` to refresh
that link without compiling, or choose another profile to inspect its flags.
For a plain CMake build without GoogleTest, configure with
`-DSTATWELL_BUILD_TESTS=OFF` and a binary directory under `builds/`.

The `.vscode` workspace settings follow the Yabai clangd and CodeLLDB
workflow. `make build PROFILE=debug` creates the root `compile_commands.json`
link used by clangd. The build, test, format, and sanitizer tasks invoke the
Makefile; debugger launches use binaries in `builds/<profile>/`.

The flake exposes `packages.aarch64-darwin.default`, `packages.x86_64-linux.default`,
an overlay, a Home Manager module, and a CI development shell. A CMake install
exports the `StatWell::statwell_core` target for other C++ applications.
Linux parsers have recorded `/proc` and `/sys` fixtures; GitHub Actions runs
the CLI, daemon, and provider contracts on a real Linux runner.

```sh
nix build .#statwell
```

## Home Manager

Add the StatWell flake as an input to a Home Manager configuration and import
`statwell.homeManagerModules.default` in the user's module list. For example:

```nix
{
  inputs.statwell.url = "github:LCS-Dev-Ergos/StatWell";
  inputs.statwell.inputs.nixpkgs.follows = "nixpkgs";
}

# In a Home Manager module, with inputs passed through extraSpecialArgs:
{ inputs, ... }:
{
  imports = [ inputs.statwell.homeManagerModules.default ];
  services.statwell = {
    enable = true;
    networkInterface = "en0";
    providers = [ "homebrew" ];
    cadences.homebrew = 3600000;
  };
}
```

The module installs the CLI and configures a launchd user agent on macOS or
a systemd user service on Linux. Enabling it starts the daemon on the next
Home Manager activation. Set `networkInterface` for network rates; the
other metrics work without it. `diskPath`, `runtimeDir`, `cadences`, and
`packageTimeoutMs` map to the daemon options. Package checks are opt-in via
`providers = [ "homebrew" ]` or `providers = [ "pacman" ]`; use
`homebrewBin` or `checkupdatesBin` for nonstandard executable paths. The
Homebrew daemon defaults to `HOMEBREW_NO_AUTO_UPDATE=1`. The module does not
provide a Nix package-update check.

The flake checks build sample Home Manager generations for both platforms
without activating either service. The [CI workflow](.github/workflows/ci.yml)
builds those generations and packages on macOS and Linux. Linux CI also runs
`make test` with Clang, sanitizers, and GCC; all build directories stay under
`builds/`.

## One-shot sampling

```sh
statwell sample --metric cpu --metric memory --format json
statwell sample --metric network --interface en0 --format kv
statwell sample --metric homebrew --format json
statwell sample --metric pacman --format kv
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

Package checks are opt-in. `homebrew` calls `brew outdated --json=v2` and
reports `total`, `formulae`, and `casks`; `pacman` calls the unprivileged
`checkupdates --nocolor` and reports `total`. A successful check with no
updates reports zero; command, timeout, and parse failures report an error.
Use `--homebrew-bin` or `--checkupdates-bin` to select an absolute executable
path. The defaults are `/opt/homebrew/bin/brew` and `/usr/bin/checkupdates`;
other installations should override them. `--package-timeout-ms` sets a
100–60000 ms deadline (default 10000). No shell or privileged pacman sync is
used. Homebrew may update its metadata according to its own configuration;
set `HOMEBREW_NO_AUTO_UPDATE=1` if that is not wanted.

## Shared daemon and snapshot

```sh
statwell daemon --interface en0
statwell daemon --provider homebrew --cadence homebrew=3600000
statwell daemon --provider pacman --cadence pacman=3600000
statwell snapshot
statwell watch --metric cpu --event statwell_cpu  # macOS SketchyBar
```

The daemon stays in the foreground for a user service manager. Its default
cadences are CPU/network 2 s, memory/load 5 s, battery 30 s and disk 60 s.
Override a cadence with `--cadence NAME=MS` (100 through 3600000 ms), for
example `--cadence disk=120000`. The network probe needs `--interface NAME`;
without one it reports an explicit error. `--disk-path` selects a filesystem.
Package providers run on separate workers and publish only after completion;
the system metrics continue sampling while a package command is running.
The default package cadence is one hour. A pending first check is
`unavailable`; later failures retain the last valid count with an error
status. Pass `--provider` to `snapshot` to include package checks only when
the daemon is absent. When it is active, `snapshot` returns the daemon's
configured metric set.
The daemon handles SIGINT and SIGTERM, skips missed intervals after sleep, and
allows only one instance per runtime directory.

On Linux, the directory defaults to `$XDG_RUNTIME_DIR/statwell-UID`; on macOS,
to `$TMPDIR/statwell-UID`. If that environment variable is absent, `/tmp` is
the parent. `--runtime-dir` overrides the location for both the daemon and
clients. The directory must belong to the current user and have mode `0700`;
the lock and snapshot are regular files with mode `0600`. The daemon writes a
temporary file, syncs it, and renames it over `snapshot.json`. A client reads
the complete old or new document. `statwell snapshot` reads it while the
daemon is active, then falls back to a one-shot sample when the daemon is
absent. Insecure permissions, an oversized file or an unsupported schema
prefix produce an error.

The [snapshot protocol](docs/snapshot-protocol.md) specifies freshness,
sequence numbers, units and recovery. `watch` reads the same snapshot and
sends a SketchyBar Mach event whenever the selected metric's sequence changes.
The first [daemon measurements](docs/performance.md) record idle RSS, CPU and
per-probe latency on the macOS development host.
`watch` registers the supplied event name, then sends `status`, timestamps,
sequence, and all fields in `value` as event variables. SketchyBar consumers
are migrated in a later phase; the existing widgets are still unchanged.
For an optional package metric, `watch --metric homebrew` or
`watch --metric pacman` enables that provider for its one-shot fallback. It
caches the fallback until the provider cadence expires while checking for a
daemon every two seconds, so a missing daemon does not rerun a package
command on each poll.

The command contracts are documented by the
[Homebrew manual](https://docs.brew.sh/Manpage.html) and the
[checkupdates manual](https://man.archlinux.org/man/checkupdates.8).

The installed package includes inactive service templates in
`share/statwell/services/`: `dev.lcs.statwell.plist` on macOS and
`statwell.service` on Linux. Their executable path is filled in by CMake at
install time. Copy the appropriate template to `~/Library/LaunchAgents/` or
`~/.config/systemd/user/`, adjust `--interface` for network sampling, then
load or enable it as a user service. Home Manager can manage this wiring
instead. Installing the package alone does not start a service.

Other clients can read the JSON without talking to the daemon. For example,
Kitty can use Python's `json.load(open(path))` to read `snapshot.json` without
spawning another program; check freshness as specified in the protocol. For
tmux, `statwell snapshot | jq -r '.metrics.cpu.value.total_percent'` can feed a
status script. A Waybar custom module can run the same command for memory, or
read the file directly. These examples show the data interface; packaged
consumer configurations are part of the migration phase.

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
- `src/registry.cpp`: built-in probe state, serialization and registration.
- `src/packages.cpp`: bounded, fixed-argument Homebrew and pacman commands,
  output parsing, deadlines and cancellation.
- `src/runtime.cpp`: generic cadence scheduling and the private snapshot transport.
- `src/watch.cpp`: SketchyBar Mach watcher on macOS.

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
2. Validate the Linux probes on a real Linux host and keep their recorded
   `/proc` and `/sys` parser fixtures.
3. Add the shared daemon, versioned snapshot protocol, launchd and systemd
   user services, and a SketchyBar Mach watcher.
4. Add independent, timeout-bound Homebrew and pacman update providers.
5. Add the Home Manager module, finish Nix packaging, and run host-native CI.
6. Migrate SketchyBar and Kitty one consumer at a time, retaining a tested
   rollback path until the replacements are verified on the real surfaces.
7. Complete the [first stable release audit](docs/release-audit.md) across
   security, correctness, robustness, and performance before project closure
   or a stable release.

The Nix package-update provider is intentionally outside the current scope.

## License

MIT; see [LICENSE](LICENSE).
