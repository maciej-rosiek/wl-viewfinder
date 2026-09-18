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
, socat
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

  # swaymsg and hyprctl are deliberately not wrapped in: anyone running this already has their
  # compositor on PATH, and depending on one here would make every install build a compositor.
  #
  # util-linux is, for flock, because the caller that most needs the lock is the one with the
  # narrowest PATH: xdg-desktop-portal-wlr runs its chooser with coreutils, findutils, grep, sed
  # and systemd, and flock is in none of them. socat is for Hyprland's event socket; without it
  # the follower still works, it just polls.
  postInstall = ''
    wrapProgram $out/bin/wl-viewfinder \
      --prefix PATH : ${lib.makeBinPath [ jq pipewire wl-mirror slurp socat util-linux ]} \
      --prefix PATH : $out/bin
  '';

  meta = {
    description = "Aim a viewfinder at a window, share the viewfinder";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
    mainProgram = "wl-viewfinder";
  };
}
