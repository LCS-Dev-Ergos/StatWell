{
  description = "Native system-status probes for local status surfaces";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "aarch64-darwin"
        "x86_64-linux"
      ];
      eachSystem = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = eachSystem (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          statwell = pkgs.callPackage ./nix/package.nix { };
        in
        {
          inherit statwell;
          default = statwell;
        }
      );

      overlays.default = final: _previous: {
        statwell = final.callPackage ./nix/package.nix { };
      };

      formatter = eachSystem (system: nixpkgs.legacyPackages.${system}.nixfmt);
    };
}
