{ stdenv, lib, autoconf, gnum4 }:
stdenv.mkDerivation {
  name = "slibtool";
  src = ./.;
  version = "0.5.34";
  buildInputs = [autoconf gnum4];
  postInstallPhase = ''
    mkdir -p $out/share/;
    cp -r share/slibtool $out/share/;
  '';
  meta.license = lib.licenses.gpl3;
}
