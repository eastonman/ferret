{
  description = "ferret — frontend reverse-engineering toolkit";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";
    sljit-src = {
      url = "github:zherczeg/sljit";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-utils, sljit-src }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        sljit = pkgs.callPackage ./nix/sljit.nix { src = sljit-src; };
      in {
        devShells.default = pkgs.mkShell {
          packages = [
            pkgs.cmake
            pkgs.ninja
            pkgs.clang
            pkgs.clang-tools
            pkgs.cli11
            pkgs.gtest
            pkgs.ruff
            pkgs.spdlog
            sljit
            (pkgs.python3.withPackages (ps: [ ps.matplotlib ps.pandas ps.pytest ]))
          ];
        };

        packages.default = pkgs.callPackage ./nix/ferret.nix {
          inherit sljit;
          src = self;
        };
      });
}
