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

  installPhase = ''
    runHook preInstall

    mkdir -p $out/include
    cp -a $src/ip $src/osc $out/include/

    mkdir -p $out/lib
    cp -a liboscpack.a $out/lib/

    # lol
    mkdir -p $out/lib/pkgconfig
    pcfile=$out/lib/pkgconfig/oscpack.pc
    echo -n > $pcfile
    echo "prefix=$out" >> $pcfile
    echo 'exec_prefix=''${prefix}' >> $pcfile
    echo 'libdir=''${exec_prefix}/lib' >> $pcfile
    echo 'includedir=''${exec_prefix}/include' >> $pcfile
    echo >> $pcfile
    echo 'Name: oscpack' >> $pcfile
    echo 'Description: Open Sound Control packet manipulation library' >> $pcfile
    echo 'Version: 1.1.0' >> $pcfile
    echo 'Libs: -L''${libdir}' -l:''${libdir}/liboscpack.a >> $pcfile
    echo 'Cflags: -I''${includedir}/liboscpack' >> $pcfile

    runHook postInstall
  '';

  meta = {
    homepage = "http://www.rossbencina.com/code/oscpack";
    description = "Open Sound Control packet manipulation library";
    platforms = lib.platforms.unix;
  };
})
