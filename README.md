# wl-viewfinder

Share a rectangle of your screen, aimed at a window and following it as it moves and resizes.

`select` and `output` work on **any wlroots-based compositor**. `window` -- aim at the focused
window and follow it -- additionally needs **sway**, because following a window is the one question
no Wayland client can answer for itself. See [Porting](#porting).

## Why

Sharing a window on wlroots compositors has two long-standing annoyances:

- **No mouse cursor.** wlroots does not paint cursors into toplevel captures
  ([wlroots#4037](https://gitlab.freedesktop.org/wlroots/wlroots/-/issues/4037)), so the person
  watching cannot see what you are pointing at. Sharing a *screen* does show the cursor.
- **You are stuck with what you picked.** Changing which window you share means going back through
  the portal picker, which renegotiates the stream -- in most conferencing apps that is a visible
  drop, and in some it means re-sharing from scratch.

wl-viewfinder sidesteps both. [wl-mirror](https://github.com/Ferdi265/wl-mirror) shows a rectangle
of an *output* in an ordinary window, and that window is what you share. Because the source is an
output capture, the cursor is in it. Because you share one unchanging window, re-aiming the
rectangle never touches the portal: **you can change what you are showing mid-call without the
stream so much as flickering.**

A red frame on screen shows where the rectangle is. It is drawn in a band just *outside* the
region, so it never appears in the share.

## Usage

```
wl-viewfinder blank    # start sharing, showing nothing
wl-viewfinder window   # aim at the focused window, and follow it
wl-viewfinder select   # drag out a region
wl-viewfinder output   # the whole focused output
wl-viewfinder off      # stop
wl-viewfinder label    # one line: what is being shared
wl-viewfinder status   # units, region, followed window
```

What you share depends on where the mirror was parked, and under sway it is parked out of sight.

Start with `blank` and the call sees a black screen until you aim at something -- and going back
to `blank` mid-call hides the room again without dropping the share. Blanking points the mirror at
a second, small headless output that is only ever black rather than stopping it, so the window and
the sink both stay present: a portal request that asks only for windows still has something to be
answered with, and nothing is renegotiated.

**Under sway (the default).** The tool asks sway for a headless output -- a screen that renders and
can be captured like any other, but that no display is showing -- and parks the mirror there
fullscreen. Nothing appears on your own screen, and the thing to share is that **monitor**
(`HEADLESS-n`), once at the start of the call. Because the share target is an output rather than a
window, it has a fixed size: re-aiming the rectangle never renegotiates the stream, and the portal
issues a restore token for it without the patches a window target needs. `wl-viewfinder off`
unplugs it again.

**Anywhere else,** or with `WL_VIEWFINDER_SINK=window`, the mirror is an ordinary window titled
**"Shared region"** and that is what you hand over. It has to stay mapped and on a visible
workspace: a window the compositor is not drawing produces no frames, and the share freezes.

`WL_VIEWFINDER_SINK_MODE` sets the shared resolution (default `1920x1080`). Regions of a different
aspect are letterboxed into it rather than resizing the stream mid-call.

Sway bindings:

```
bindsym $mod+Shift+b exec wl-viewfinder window
bindsym $mod+Shift+n exec wl-viewfinder off
```

`window` follows the window afterwards: resize it, move it, throw it at another output, and the
rectangle goes with it. It deliberately does **not** follow focus -- re-aim explicitly, so glancing
at another window never shares it by accident.

Note the rectangle is a screen region, not a window handle: it shows whatever is underneath it, so
switching workspaces changes what is shared. The red frame is there to keep that honest.

**The aim is given up when the call is.** The moment nothing is capturing any more -- you pressed
"stop sharing", or the meeting ended -- the tool goes back to `blank` by itself: the follower stops
and the red frame goes away, because a rectangle claiming a share that has ended is exactly the lie
the frame exists to prevent. The mirror stays up, so joining the next call still costs one portal
prompt. Needs `pw-dump`; without it the aim simply stays where you left it.

## Install

| dependency | needed for |
| --- | --- |
| `wl-mirror` >= 0.18 (ships `wl-present`) | everything -- it is the engine |
| a systemd user session | everything -- the four transient units |
| `slurp` | `select`, and `output` when there is no sway |
| `sway` + `jq` | `window` only |
| `pw-dump` (pipewire) | giving up the aim when the call ends |

Build needs a C compiler, `pkg-config`, `wayland-scanner` and `wayland-client` headers. The Wayland
protocols are vendored, so `wayland-protocols` and `wlr-protocols` are not build dependencies.

```sh
make
sudo make install          # PREFIX=/usr/local
```

Nix:

```sh
nix run github:maciej-rosiek/wl-viewfinder -- window
```

or add the flake's `packages.default` to your system or home-manager packages.

## How it works

```
wl-viewfinder window
      |
      +-- asks sway for the focused window's geometry
      +-- wl-present mirror <output> -r <region>    (systemd user unit, the shared window)
      +-- wl-viewfinder-frame                        (layer-shell red frame, via a fifo)
      +-- wl-viewfinder follow                       (keeps the region on that window)
      +-- wl-viewfinder watch                        (gives up the aim when the call ends)
```

Four transient `systemd --user` units. The frame, the follower and the watcher are `BindsTo` the
mirror, so closing the mirror window tears down everything -- a red frame left on screen claiming a
share that has ended would be worse than no frame at all.

The watcher answers "is anybody still capturing" from the PipeWire graph rather than from the
portal, which offers no way to ask: `xdg-desktop-portal-wlr` gives each cast a node named
`xdpw-stream-<random>` that lives exactly as long as the cast does, and `pw-dump -m` streams
additions and removals of it. An empty graph is believed only after it has stayed empty for a
second, because a client renegotiating drops its node and takes a new one straight away.

Re-aiming feeds `wl-present set-region` on the running mirror. The mirror window itself never
resizes, so PipeWire never renegotiates and the share survives.

`wl-viewfinder-frame` is a small wlroots-layer-shell client: overlay layer, empty input region so
clicks fall through, and an shm buffer whose interior is transparent. It costs about 4 MB resident.
It also answers `-l`, listing outputs as `name x,y wxh` from `xdg-output`; that is what lets the
region-to-output lookup -- needed on every path -- work without asking a compositor.

## Porting

Everything that knows this is sway lives between the `--- compositor backend` markers in
`wl-viewfinder`: `focused_window`, `region_of_id`, `focused_output`, and `subscribe_window_events`.
Only `window` calls them. Regions are `x,y WxH` in compositor-global logical coordinates.

Anything with `wlr-layer-shell` and a capture protocol wl-mirror supports (`wlr-screencopy` or
`ext-image-copy-capture-v1`) can work: niri, Hyprland, river, Wayfire, COSMIC, labwc. **GNOME and
KDE cannot** -- they implement neither protocol.

The follower polls every 300 ms because sway's `window` IPC event has no resize change type;
compositors with a real geometry-changed event should drive it from events instead.

## Known limitations

- Only tested at `scale = 1`. Geometry comes from `xdg-output`'s logical coordinates, so fractional
  scaling should be handled correctly, but nobody has checked -- reports welcome.
- The rectangle shows whatever is under it, including a window you drag on top of the shared one.
- No audio; this only concerns video.

## License

MIT
