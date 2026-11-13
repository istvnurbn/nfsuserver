{
  lib,
  stdenv,
}:

stdenv.mkDerivation {
  pname = "nfsuserver";
  # NFSU_LAN_VERSION in nfsuserver/nfsuserver.cpp
  version = "1.0.5";

  # Only what the server is built from. Editing the nix files, the README or
  # the Windows launcher does not cause a rebuild, and logs or *.dat databases
  # left behind by a local test run never end up in the world-readable store.
  src = lib.fileset.toSource {
    root = ./nfsuserver;
    fileset = lib.fileset.unions [
      ./nfsuserver/Makefile
      ./nfsuserver/nfsu.conf
      ./nfsuserver/news.txt
      (lib.fileset.fileFilter (file: file.hasExt "cpp" || file.hasExt "h") ./nfsuserver)
    ];
  };

  # The Makefile hardcodes `g++` and `-L"/lib"`.
  makeFlags = [
    "CC=${stdenv.cc.targetPrefix}c++"
    "LIBS=-lpthread"
  ];

  # Pin the dialect this 2004 code base was written for, so a newer default
  # in a future compiler cannot break the build.
  env.NIX_CFLAGS_COMPILE = "-std=c++98";

  enableParallelBuilding = true;

  installPhase = ''
    runHook preInstall

    install -Dm755 nfsuserver -t "$out/bin"
    # Reference copies. The server reads nfsu.conf and news.txt from its
    # working directory, it does not look here by itself.
    install -Dm644 nfsu.conf news.txt -t "$out/share/nfsuserver"

    runHook postInstall
  '';

  meta = {
    description = "Lobby server for Need for Speed: Underground (2003) online and LAN play";
    homepage = "https://github.com/istvnurbn/nfsuserver";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
    mainProgram = "nfsuserver";
  };
}
