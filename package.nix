{ lib, stdenv }:

stdenv.mkDerivation {
  pname = "nfsuserver";
  version = "unstable";

  src = lib.cleanSource ./.;

  hardeningDisable = [ "format" ];

  postPatch = ''
    substituteInPlace nfsuserver/Makefile \
      --replace-fail '-L"/lib"' ""
  '';

  preBuild = ''
    cd nfsuserver
  '';

  NIX_CFLAGS_COMPILE = "-std=c++98 -Wno-narrowing";

  installPhase = ''
    runHook preInstall
    install -Dm755 nfsuserver "$out/bin/nfsuserver"
    runHook postInstall
  '';

  meta = with lib; {
    description = "Need for Speed: Underground server";
    homepage = "https://github.com/istvnurbn/nfsuserver";
    license = licenses.mit;
    platforms = platforms.linux;
    mainProgram = "nfsuserver";
  };
}
