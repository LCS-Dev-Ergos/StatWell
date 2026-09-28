# Project context

This file records the decisions that started the standalone repository. The
technical scope comes from `SYSPROBE_SESSION_BRIEF.md` in the Dotfiles project;
the user confirmed the name and start of implementation in the 2026-09-28
conversation.

## Decisions

- The user chose **StatWell** as the project name and authorized scaffolding
  and the first implementations on 2026-09-28.
- The project lives in its own repository under `LCS-Dev-Ergos`, with a local
  checkout in `/Volumes/LCS.Data/StatWell`. The source brief prohibits pushing
  without a separate user request.
- The Nix **package-update provider** is deferred at the user's request. Nix
  packaging for supported hosts remains in scope.
- C++23 is the library language level so public probe results can use
  `std::expected`. Phase one built the macOS probe library and one-shot CLI.
  Phase two added Linux probes with recorded procfs/sysfs fixtures. Phase
  three added the shared daemon, snapshot protocol, and user-service templates.
- The user requested the AlgoDataStruct source and CMake style, a Makefile,
  all build directories under `builds/`, and a root `compile_commands.json`
  link for VS Code clangd. The workspace settings adapt the Yabai debugger,
  task, and sanitizer configuration to those project paths.
- Phase four provides optional Homebrew and pacman update checks with bounded
  command execution on background workers. The default daemon does not run
  package commands unless enabled by `--provider`.
- Phase five adds an opt-in Home Manager user service for macOS and Linux and
  GitHub Actions checks for host-native packaging and Linux runtime contracts.
  It does not add the deferred Nix package-update provider or activate either
  user's existing service configuration.
- Phase-five GitHub Actions run 36455837568 passed on 2026-09-28 at commit
  `3418e9c`: Ubuntu 24.04 built the package and Home Manager generation and
  passed Clang Debug, ASan/UBSan, and GCC runtime tests; macOS 15 built its
  package and Home Manager generation. The first run exposed a GCC warning
  in the Linux directory deleter, corrected before the passing run.
- Before closing the project or releasing its first stable version, perform
  the security, correctness, robustness, and performance audit described in
  `docs/release-audit.md`. The user requested this gate on 2026-09-28.

## Open work

- Migrate consumers one at a time in phase six. Verify the Mach watcher
  against the real bar and Kitty against a live tab bar after the user runs
  the Dotfiles switch. Keep a tested rollback path for each migration.
- Complete the release audit and resolve its findings before a stable tag.
