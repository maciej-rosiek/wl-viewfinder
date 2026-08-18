# wl-viewfinder

Share a rectangle of your screen, aimed at a window and following it as it moves and resizes.

`select` and `output` work on **any wlroots-based compositor**. `window` -- aim at the focused
window and follow it -- additionally needs **sway**. See [Porting](#porting).

## Why

Sharing a window on wlroots compositors has two long-standing annoyances: wlroots does not paint the
cursor into toplevel captures ([wlroots#4037](https://gitlab.freedesktop.org/wlroots/wlroots/-/issues/4037)),
and changing which window you share means going back through the portal picker, which renegotiates
the stream.

wl-viewfinder sidesteps both. [wl-mirror](https://github.com/Ferdi265/wl-mirror) shows a rectangle
of an *output* in an ordinary window, and that window is what you share: the cursor is in it because
the source is an output capture, and re-aiming the rectangle never touches the portal, so **you can
change what you are showing mid-call without the stream flickering.**

A red frame shows where the rectangle is, drawn just *outside* the region so it never appears in the
share.

## Usage

```
wl-viewfinder blank    # start sharing, showing nothing
wl-viewfinder window   # aim at the focused window, and follow it
wl-viewfinder select   # drag out a region
wl-viewfinder output   # the whole focused output
wl-viewfinder off      # stop
wl-viewfinder label    # one line: what is being shared
wl-viewfinder status   # units, region, aim, followed window
wl-viewfinder chooser  # answer a portal request with the viewfinder (see On demand)
```

**Under sway (the default)** the mirror is parked on a headless output -- a screen that renders and
can be captured, but that no display is showing. Nothing appears on your own screen, and the thing
to share is that **monitor** (`HEADLESS-n`), once at the start of the call. `wl-viewfinder off`
unplugs it. **Anywhere else,** or with `WL_VIEWFINDER_SINK=window`, the mirror is an ordinary window
titled **"viewfinder"**, which has to stay mapped and visible or it produces no frames.

`blank` aims the mirror at a second, always-black headless output rather than stopping it, so the
share is never dropped: join a call blank, and go back to `blank` mid-call to hide the room again.
`WL_VIEWFINDER_SINK_MODE` sets the shared resolution (default `1920x1080`); other aspect ratios are
letterboxed into it.

Sway bindings:

```
bindsym $mod+Shift+b exec wl-viewfinder window
bindsym $mod+Shift+n exec wl-viewfinder off
```

`window` follows the window, but deliberately **not** focus -- re-aim explicitly, so glancing at
another window never shares it by accident. Leaving its workspace blanks the share and coming back
restores it, because an output only ever renders the workspace in front of you. `output` is not
guarded.

**The aim is given up when the call is.** Once nothing is capturing the tool returns to `blank`, and
if nothing starts capturing it stops and unplugs its outputs. `WL_VIEWFINDER_IDLE_GRACE` and
`WL_VIEWFINDER_IDLE_START` are the two waits; both need `pw-dump`.

## On demand

`wl-viewfinder chooser` is the whole tool as an `xdg-desktop-portal-wlr` source chooser. A request
that can take a window is answered with the viewfinder, started if it is the call's first. A request
that cannot take a window at all is answered with the **real screen** and starts nothing -- that is
somebody asking to share everything. Chrome asks once per tab of its dialog with a single-type mask,
so its Entire Screen tab gets the screen and its Window tab gets the viewfinder. Nothing is prompted
for, and nothing runs between calls.

```ini
# ~/.config/xdg-desktop-portal-wlr/config
[screencast]
chooser_type=dmenu
chooser_cmd=wl-viewfinder chooser
```

This needs **a patched xdpw** to work from cold: stock xdpw matches the chooser's reply against the
source list it wrote *beforehand*, so a source created *by* the chooser can never be picked. Two
changes fix it -- re-read the sources after the chooser exits, and accept a bare output name or
toplevel identifier. Without the patch the answer still works whenever the viewfinder is already
running.

## Install

| dependency | needed for |
| --- | --- |
| `wl-mirror` >= 0.18 (ships `wl-present`) | everything -- it is the engine |
| a systemd user session | everything -- the transient units |
| `flock` (util-linux) | serialising the commands that build the mirror -- a chooser is re-entrant |
| `slurp` | `select`, and `output` when there is no sway |
| `sway` + `jq` | `window`, and blanking the share off its workspace |
| `pw-dump` (pipewire) | giving up the aim when the call ends |

Build needs a C compiler, `pkg-config`, `wayland-scanner` and `wayland-client` headers. The Wayland
protocols are vendored.

```sh
make
sudo make install          # PREFIX=/usr/local
```

Nix: `nix run github:maciej-rosiek/wl-viewfinder -- window`, or add the flake's `packages.default`
to your system or home-manager packages.

## How it works

```
wl-viewfinder window
      |
      +-- asks sway for the focused window's geometry
      +-- wl-present mirror <output> -r <region>    (the shared window)
      +-- wl-viewfinder-frame                        (layer-shell red frame, via a fifo)
      +-- wl-viewfinder follow                       (keeps the region on that window)
      +-- wl-viewfinder watch                        (gives up the aim when the call ends)
      +-- wl-viewfinder-frame -k <blank output>      (keeps the blank source black)
```

Five transient `systemd --user` units, all `BindsTo` the mirror, so closing the mirror window tears
down the rest. Re-aiming feeds `wl-present set-region` on the running mirror: the window never
resizes, so PipeWire never renegotiates and the share survives.

The watcher answers "is anybody still capturing" from the PipeWire graph, which the portal offers no
way to ask -- xdpw gives each cast a node named `xdpw-stream-<random>` that lives exactly as long as
the cast. An empty graph is believed only after a second, because a renegotiating client drops its
node and takes a new one.

`wl-viewfinder-frame` is a small layer-shell client: overlay layer, empty input region, shm buffer
with a transparent interior. `-k <output>` is the same client filled opaque black over a whole
output, which is what keeps the blank source blank above whatever a desktop shell paints there. `-l`
lists outputs as `name x,y wxh` from `xdg-output`, so the region-to-output lookup needs no
compositor.

## Porting

Everything that knows this is sway lives between the `--- compositor backend` markers in
`wl-viewfinder`: `focused_window`, `region_of_id`, `focused_output`, `subscribe_events` and the
three workspace lookups. Only `window` and the workspace guard call them. Regions are `x,y WxH` in
compositor-global logical coordinates.

Anything with `wlr-layer-shell` and a capture protocol wl-mirror supports (`wlr-screencopy` or
`ext-image-copy-capture-v1`) can work: niri, Hyprland, river, Wayfire, COSMIC, labwc. **GNOME and
KDE cannot** -- they implement neither protocol.

The follower polls every 300 ms because sway's `window` IPC event has no resize change type;
compositors with a real geometry-changed event should drive it from events instead.

## Known limitations

- Only tested at `scale = 1`. Geometry comes from `xdg-output`'s logical coordinates, so fractional
  scaling should be handled correctly, but nobody has checked.
- The rectangle shows whatever is under it, including a window you drag on top of the shared one.
- No audio; this only concerns video.

## License

MIT
