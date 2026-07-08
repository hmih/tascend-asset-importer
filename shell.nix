{
  pkgs ? import <nixpkgs> { },
}:
pkgs.mkShellNoCC {
  buildInputs = with pkgs; [
    # Build tools
    cmake
    ninja
    # VCS
    git
    git-lfs
    openssh
    # C++ deps (UModel build)
    simde
    SDL2
    libpng
    zlib
    lzo
    # Map extraction (UELib)
    dotnet-sdk_10
  ];

  shellHook = ''
    export DYLD_LIBRARY_PATH="${pkgs.lzo}/lib''${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
  '';
}
