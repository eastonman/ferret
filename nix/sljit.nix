{
  stdenv,
  src,
}:
stdenv.mkDerivation {
  pname = "sljit";
  version = "unstable-2026-02-15";
  inherit src;

  # Upstream sljit's CMakeLists.txt only builds a test executable using
  # GNU-ld-only linker flags, not a library target — see the file's own
  # comment: "This script is incomplete...you are better off install GNU
  # make and using that instead." We compile the single source file
  # directly into a static library, which is what GNU make would do
  # under the hood for the library target.
  dontConfigure = true;

  buildPhase = ''
    runHook preBuild
    $CC -c -O2 -fPIC -I sljit_src sljit_src/sljitLir.c -o sljitLir.o
    $AR rcs libsljit.a sljitLir.o
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/lib $out/include/sljit
    cp libsljit.a $out/lib/
    cp sljit_src/*.h $out/include/sljit/
    runHook postInstall
  '';

  meta = {
    description = "Stack-Less JIT compiler library";
    homepage = "https://github.com/zherczeg/sljit";
    license = "BSD-2-Clause";
  };
}
