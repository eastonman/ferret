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
                    # nixpkgs-25.11 ships plotly 5.24.1, below the required >=6.1.1.
                    # Until the nixpkgs pin catches up, use:
                    #   pip install --user 'plotly>=6.1.1'
                    # warnIfNot surfaces a version mismatch immediately; when the
                    # nixpkgs pin is bumped and plotly meets >=6.1.1 this line can
                    # revert to plain `plotly`.
                    (pkgs.lib.warnIfNot
                      (pkgs.lib.versionAtLeast plotly.version "6.1.1")
                      "ferret: nixpkgs plotly ${plotly.version} < 6.1.1 (requirements.txt); run: pip install --user 'plotly>=6.1.1'"
                      plotly)
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
