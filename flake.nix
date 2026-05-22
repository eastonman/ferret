{
  description = "ferret — frontend reverse-engineering toolkit";

  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    sljit-src = {
      url = "github:zherczeg/sljit";
      flake = false;
    };
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
    sljit-src,
  }:
    flake-utils.lib.eachDefaultSystem (
      system: let
        pkgs = nixpkgs.legacyPackages.${system};
        sljit = pkgs.callPackage ./nix/sljit.nix {src = sljit-src;};
      in {
        formatter = pkgs.nixfmt-rfc-style;

        devShells.default = pkgs.mkShell {
          packages =
            [
              pkgs.cmake
              pkgs.ninja
              pkgs.clang
              pkgs.clang-tools
              pkgs.cli11
              pkgs.gtest
              pkgs.ruff
              pkgs.spdlog
              sljit
              (pkgs.python3.withPackages (
                ps:
                  with ps; [
                    numpy
                    pandas
                    plotly
                    kaleido
                    pytest
                  ]
              ))
            ]
            ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
              pkgs.chromium
            ];
        };

        packages.default = pkgs.callPackage ./nix/ferret.nix {
          inherit sljit;
          src = self;
        };
      }
    );
}
