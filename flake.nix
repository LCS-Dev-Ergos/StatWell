{
  description = "Native system-status probes for local status surfaces";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  inputs.home-manager = {
    url = "github:nix-community/home-manager";
    inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs =
    {
      self,
      nixpkgs,
      home-manager,
    }:
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

      homeManagerModules.default = import ./nix/home-manager.nix;

      checks = eachSystem (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          home-manager = import ./nix/module-check.nix {
            inherit pkgs home-manager;
            module = self.homeManagerModules.default;
          };
        }
      );

      devShells = eachSystem (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          ci = pkgs.mkShell {
            packages =
              with pkgs;
              [
                cmake
                ninja
                gtest
                python3
                llvmPackages.clang
                llvmPackages.clang-tools
                cppcheck
              ]
              ++ pkgs.lib.optionals stdenv.hostPlatform.isLinux [ gcc ];
          };
        }
      );

      formatter = eachSystem (system: nixpkgs.legacyPackages.${system}.nixfmt);
    };
}
