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
                    # INTENTIONAL DIVERGENCE: nixpkgs-25.11 ships kaleido 0.2.1 (legacy
                    # Orca-based renderer), while requirements.txt requires kaleido>=1.0
                    # (the new chromium-based renderer).  Both API paths are handled at
                    # runtime by output.py, so the dev shell remains functional.
                    # To use the v1 path locally: pip install --user 'kaleido>=1.0'
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
