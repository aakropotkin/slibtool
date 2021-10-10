{ stdenv, lib, autoconf, gnum4 }:
stdenv.mkDerivation {
  name = "slibtool";
  src = ./.;
  version = "0.5.34";
  buildInputs = [autoconf gnum4];
  meta.license = lib.licenses.gpl3;
}
