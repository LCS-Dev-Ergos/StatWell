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
  `std::expected`. The first phase is the macOS probe library, a one-shot CLI,
  and their tests. Linux probes, the daemon, package-update providers, Home
  Manager integration, and consumer migration follow in later phases.

## Open work

- Implement the Linux backend using recorded `/proc` and `/sys` fixtures.
- Define and implement the versioned daemon snapshot protocol, sampling
  schedule, and platform user services.
- Add Homebrew and pacman providers with bounded execution, then the Home
  Manager module and staged consumer migrations.
