{
  config,
  lib,
  pkgs,
  ...
}:
let
  cfg = config.services.statwell;
  inherit (lib)
    mkEnableOption
    mkIf
    mkOption
    types
    ;

  supportedMetrics = [
    "cpu"
    "memory"
    "load"
    "disk"
    "battery"
    "network"
  ]
  ++ cfg.providers;

  daemonArgs = [
    "daemon"
  ]
  ++ lib.optionals (cfg.networkInterface != null) [
    "--interface"
    cfg.networkInterface
  ]
  ++ lib.optionals (cfg.diskPath != null) [
    "--disk-path"
    cfg.diskPath
  ]
  ++ lib.optionals (cfg.runtimeDir != null) [
    "--runtime-dir"
    cfg.runtimeDir
  ]
  ++ lib.concatMap (provider: [
    "--provider"
    provider
  ]) cfg.providers
  ++ lib.concatLists (
    lib.mapAttrsToList (name: milliseconds: [
      "--cadence"
      "${name}=${toString milliseconds}"
    ]) cfg.cadences
  )
  ++ [
    "--package-timeout-ms"
    (toString cfg.packageTimeoutMs)
  ]
  ++ lib.optionals (cfg.homebrewBin != null) [
    "--homebrew-bin"
    cfg.homebrewBin
  ]
  ++ lib.optionals (cfg.checkupdatesBin != null) [
    "--checkupdates-bin"
    cfg.checkupdatesBin
  ];

  executable = lib.getExe cfg.package;
  systemdLauncher = pkgs.writeShellScript "statwell-daemon" ''
    exec ${lib.escapeShellArgs ([ executable ] ++ daemonArgs)}
  '';
  noAutoUpdate = cfg.homebrewNoAutoUpdate && lib.elem "homebrew" cfg.providers;
in
{
  options.services.statwell = {
    enable = mkEnableOption "the StatWell user daemon and CLI";

    package = mkOption {
      type = types.package;
      default = pkgs.callPackage ./package.nix { };
      defaultText = lib.literalExpression "pkgs.callPackage <statwell/nix/package.nix> { }";
      description = "StatWell package to install and run.";
    };

    networkInterface = mkOption {
      type = types.nullOr types.str;
      default = null;
      example = "en0";
      description = "Interface for network rates; null leaves that metric unavailable.";
    };

    diskPath = mkOption {
      type = types.nullOr types.str;
      default = null;
      example = "/Users/alice";
      description = "Absolute path whose filesystem is sampled; null uses StatWell's default.";
    };

    runtimeDir = mkOption {
      type = types.nullOr types.str;
      default = null;
      example = "/run/user/1000/statwell";
      description = "Private absolute runtime directory; null uses the platform default.";
    };

    cadences = mkOption {
      type = types.attrsOf (types.ints.between 100 3600000);
      default = { };
      example = {
        cpu = 2000;
        homebrew = 3600000;
      };
      description = "Per-metric cadence overrides in milliseconds.";
    };

    providers = mkOption {
      type = types.listOf (
        types.enum [
          "homebrew"
          "pacman"
        ]
      );
      default = [ ];
      example = [ "homebrew" ];
      description = "Optional package-update providers; the Nix update provider is not defined.";
    };

    packageTimeoutMs = mkOption {
      type = types.ints.between 100 60000;
      default = 10000;
      description = "Deadline for each package-update command in milliseconds.";
    };

    homebrewBin = mkOption {
      type = types.nullOr types.str;
      default = null;
      example = "/usr/local/bin/brew";
      description = "Absolute brew executable path; null uses StatWell's default.";
    };

    checkupdatesBin = mkOption {
      type = types.nullOr types.str;
      default = null;
      example = "/usr/bin/checkupdates";
      description = "Absolute checkupdates executable path; null uses StatWell's default.";
    };

    homebrewNoAutoUpdate = mkOption {
      type = types.bool;
      default = true;
      description = "Set HOMEBREW_NO_AUTO_UPDATE for the daemon when Homebrew is enabled.";
    };
  };

  config = mkIf cfg.enable {
    assertions = [
      {
        assertion = cfg.diskPath == null || lib.hasPrefix "/" cfg.diskPath;
        message = "services.statwell.diskPath must be absolute.";
      }
      {
        assertion = cfg.runtimeDir == null || lib.hasPrefix "/" cfg.runtimeDir;
        message = "services.statwell.runtimeDir must be absolute.";
      }
      {
        assertion = cfg.homebrewBin == null || lib.hasPrefix "/" cfg.homebrewBin;
        message = "services.statwell.homebrewBin must be absolute.";
      }
      {
        assertion = cfg.checkupdatesBin == null || lib.hasPrefix "/" cfg.checkupdatesBin;
        message = "services.statwell.checkupdatesBin must be absolute.";
      }
      {
        assertion = cfg.providers == lib.unique cfg.providers;
        message = "services.statwell.providers must not contain duplicates.";
      }
      {
        assertion = lib.all (name: lib.elem name supportedMetrics) (lib.attrNames cfg.cadences);
        message = "services.statwell.cadences names must be built-in metrics or enabled providers.";
      }
    ];

    home.packages = [ cfg.package ];

    launchd.agents.statwell = mkIf pkgs.stdenv.hostPlatform.isDarwin {
      enable = true;
      domain = "user";
      config = {
        ProgramArguments = [ executable ] ++ daemonArgs;
        EnvironmentVariables = lib.optionalAttrs noAutoUpdate {
          HOMEBREW_NO_AUTO_UPDATE = "1";
        };
        KeepAlive = true;
        RunAtLoad = true;
        ProcessType = "Background";
      };
    };

    systemd.user.services.statwell = mkIf pkgs.stdenv.hostPlatform.isLinux {
      Unit.Description = "StatWell shared system status sampler";
      Service = {
        Type = "exec";
        ExecStart = toString systemdLauncher;
        Restart = "on-failure";
        RestartSec = 2;
        Environment = lib.optional noAutoUpdate "HOMEBREW_NO_AUTO_UPDATE=1";
      };
      Install.WantedBy = [ "default.target" ];
    };
  };
}
