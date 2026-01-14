{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    let
      llvmPackages = pkgs: pkgs.llvmPackages;
    in
      {
        overlays.default = final: prev:
          let
            system = prev.stdenv.hostPlatform.system;
          in {
            libdispatch = final.callPackage ./nix/libdispatch/default.nix {
              stdenv = (llvmPackages final).stdenv;
            };
            oscpack = final.callPackage ./nix/oscpack/default.nix {
              stdenv = (llvmPackages final).stdenv;
            };
            sapf = final.callPackage ./default.nix {};
          };
      } //
      (flake-utils.lib.eachDefaultSystem (system:
        let
          pkgs = import nixpkgs {
            inherit system;
            overlays = [self.overlays.default];
          };
        in rec {
          packages.default = pkgs.sapf;
          
          devShell = pkgs.mkShell.override {
            # inherit stdenv;
          } {
            buildInputs = with pkgs; [
              (llvmPackages pkgs).lldb
              doctest
              meson
              ninja
              pkg-config

              eigen
              fftw
              libedit
              libsndfile
              oscpack
              rtaudio_6
              rtmidi
              xsimd
              libjpeg
            ];

            # CC = "${stdenv}/bin/clang";
            # CXX = "${stdenv}/bin/clang++";
          };
        }
      ));
}
