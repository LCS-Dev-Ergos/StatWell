{
  pkgs,
  home-manager,
  module,
}:
let
  darwin = pkgs.stdenv.hostPlatform.isDarwin;
  provider = if darwin then "homebrew" else "pacman";
  home = if darwin then "/Users/statwell-ci" else "/home/statwell-ci";
  evaluation = home-manager.lib.homeManagerConfiguration {
    inherit pkgs;
    modules = [
      module
      {
        home.username = "statwell-ci";
        home.homeDirectory = home;
        home.stateVersion = "24.11";

        services.statwell = {
          enable = true;
          networkInterface = if darwin then "en0" else "eth0";
          diskPath = home;
          providers = [ provider ];
          cadences.${provider} = 3600000;
        };
      }
    ];
  };
in
evaluation.activationPackage
