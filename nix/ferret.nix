{ stdenv, cmake, ninja, cli11, gtest, sljit, src }:

stdenv.mkDerivation {
  pname = "ferret";
  version = "0.1.0";
  inherit src;

  nativeBuildInputs = [ cmake ninja ];
  buildInputs = [ cli11 gtest sljit ];

  doCheck = true;
  checkPhase = ''
    runHook preCheck
    ctest --output-on-failure
    runHook postCheck
  '';
}
