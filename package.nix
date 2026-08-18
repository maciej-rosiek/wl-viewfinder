{ lib
, stdenv
, pkg-config
, wayland-scanner
, wayland
, makeWrapper
, jq
, wl-mirror
, slurp
, sway
}:

stdenv.mkDerivation {
  pname = "wl-viewfinder";
  version = "0.1.0";

  src = ./.;

  strictDeps = true;
  nativeBuildInputs = [ pkg-config wayland-scanner makeWrapper ];
  buildInputs = [ wayland ];

  makeFlags = [ "PREFIX=$(out)" ];

  postInstall = ''
    wrapProgram $out/bin/wl-viewfinder \
      --prefix PATH : ${lib.makeBinPath [ jq wl-mirror slurp sway ]} \
      --prefix PATH : $out/bin
  '';

  meta = {
    description = "Aim a viewfinder at a window, share the viewfinder";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
    mainProgram = "wl-viewfinder";
  };
}
