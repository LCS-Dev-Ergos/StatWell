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

## Open work

- Build and run the Linux package on an actual Linux host; macOS fixture
  tests and derivation evaluation do not establish Linux runtime behavior.
- Wire the package and user services through Home Manager, then migrate
  consumers one at a time. Verify the Mach watcher against the real bar at
  migration time.
