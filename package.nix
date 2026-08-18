{ lib
, stdenv
, pkg-config
, wayland-scanner
, wayland
, makeWrapper
, jq
, pipewire
, wl-mirror
, slurp
, util-linux
}:

stdenv.mkDerivation {
  pname = "wl-viewfinder";
  version = "0.1.0";

  src = ./.;

  strictDeps = true;
  nativeBuildInputs = [ pkg-config wayland-scanner makeWrapper ];
  buildInputs = [ wayland ];

  makeFlags = [ "PREFIX=$(out)" ];

  # swaymsg is deliberately not wrapped in: anyone running this already has sway on PATH, and
  # depending on it here would make every install build the compositor.
  #
  # util-linux is, for flock, because the caller that most needs the lock is the one with the
  # narrowest PATH: xdg-desktop-portal-wlr runs its chooser with coreutils, findutils, grep, sed
  # and systemd, and flock is in none of them.
  postInstall = ''
    wrapProgram $out/bin/wl-viewfinder \
      --prefix PATH : ${lib.makeBinPath [ jq pipewire wl-mirror slurp util-linux ]} \
      --prefix PATH : $out/bin
  '';

  meta = {
    description = "Aim a viewfinder at a window, share the viewfinder";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
    mainProgram = "wl-viewfinder";
  };
}
