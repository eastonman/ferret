{
  description = "ferret — frontend reverse-engineering toolkit";

  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
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
        sljitStatic = pkgs.pkgsStatic.callPackage ./nix/sljit.nix {src = sljit-src;};
      in {
        formatter = pkgs.nixfmt-rfc-style;

        devShells.default = pkgs.mkShell {
          packages =
            [
              pkgs.cmake
              pkgs.cmake-format
              pkgs.ninja
              pkgs.clang
              pkgs.clang-tools
              pkgs.cli11
              pkgs.gtest
              pkgs.markdownlint-cli2
              pkgs.prettier
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

        devShells.android = let
          pkgsUnfree = import nixpkgs {
            inherit system;
            config.allowUnfree = true;
          };
          androidNdk = pkgsUnfree.androidenv.androidPkgs.ndk-bundle;
          platformTools = pkgsUnfree.androidenv.androidPkgs.platform-tools;
        in
          pkgs.mkShell {
            packages = [
              pkgs.cmake
              pkgs.ninja
              androidNdk
              platformTools
            ];
            shellHook = ''
              export ANDROID_NDK_HOME=${androidNdk}/libexec/android-sdk/ndk/${androidNdk.version}
            '';
          };

        packages =
          {
            default = pkgs.callPackage ./nix/ferret.nix {
              inherit sljit;
              src = self;
            };
          }
          # pkgsStatic is musl + -static on Linux. On Darwin it cannot
          # produce a fully static executable (no static libc, no
          # crt0.o), so the output is Linux-only. Note this deliberately
          # does NOT pass -DFERRET_STATIC=ON: that option forces the
          # FetchContent path, which needs network access the Nix
          # sandbox does not grant. pkgsStatic's stdenv supplies the
          # static linking, and its spdlog/gtest/cli11 are .a archives
          # the existing find_package path resolves unchanged.
          // pkgs.lib.optionalAttrs pkgs.stdenv.isLinux {
            static = pkgs.pkgsStatic.callPackage ./nix/ferret.nix {
              sljit = sljitStatic;
              src = self;
            };
          };
      }
    );
}
