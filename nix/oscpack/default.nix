{
  cmake,
  lib,
  fetchFromGitHub,
  stdenv,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "oscpack";
  version = "1.1.0";

  src = fetchFromGitHub {
    owner = "RossBencina";
    repo = "oscpack";
    rev = "release_1_1_0";
    hash = "sha256-yojptjtbAxbRmvCPLTLvjghiTtF/7GqCo88nY0XOQqg=";
  };

  nativeBuildInputs = [
    cmake
  ];

  # TODO
  # installPhase = ''
  #   runHook preInstall
  #   runHook postInstall
  # '';

  meta = {
    homepage = "http://www.rossbencina.com/code/oscpack";
    description = "Open Sound Control packet manipulation library";
    platforms = lib.platforms.unix;
  };
})
