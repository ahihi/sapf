{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-24.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    let
      llvmPackages = pkgs: pkgs.llvmPackages_16;
    in
      {
        overlays.default = final: prev:
          let
            system = prev.stdenv.hostPlatform.system;
          in {
            # sapf = TODO;
            libdispatch = final.callPackage ./nix/libdispatch/default.nix {
              stdenv = (llvmPackages final).stdenv;
            };
          };
      } //
      (flake-utils.lib.eachDefaultSystem (system:
        let
          pkgs = import nixpkgs {
            inherit system;
            overlays = [self.overlays.default];
          };
          # stdenv = (llvmPackages pkgs).stdenv;
        in rec {
          # packages.default = pkgs.sapf;

          devShell = pkgs.mkShell.override {
            # inherit stdenv;
          } {
            buildInputs = with pkgs; [
              fftw
              (llvmPackages pkgs).lldb
              libedit
              libsndfile
              meson
              ninja
              pkg-config
              rtaudio_6
              xsimd
            ];

            # CC = "${stdenv}/bin/clang";
            # CXX = "${stdenv}/bin/clang++";
          };
        }
      ));
}
