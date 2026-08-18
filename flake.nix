{
  description = "Aim a viewfinder at a window, share the viewfinder";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (pkgs: rec {
        wl-viewfinder = pkgs.callPackage ./package.nix { };
        default = wl-viewfinder;
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [ gcc gnumake pkg-config wayland-scanner ];
          buildInputs = with pkgs; [ wayland ];
          packages = with pkgs; [ wl-mirror slurp jq ];
        };
      });
    };
}
