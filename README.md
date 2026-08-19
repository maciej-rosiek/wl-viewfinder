# wl-viewfinder

Share a rectangle of your screen, aimed at a window and following it as it moves and resizes.

Sharing a window on a wlroots compositor has two long-standing problems: the cursor is missing from
the share ([wlroots#4037]), and changing which window you share means going back through the portal
picker, which renegotiates the stream.

wl-viewfinder shares a [wl-mirror] window instead. The mirror shows a rectangle of an *output*, so
the cursor is in it, and re-aiming the rectangle never touches the portal.

## Features

- Aim at a window, and follow it as it moves and resizes.
- Re-aim during a call. Nothing is renegotiated, so the share does not flicker or drop.
- The mouse cursor is in the share.
- A red frame marks the rectangle. It is drawn outside the region, so it is never shared -- except
  around a whole screen, where there is no outside and the frame hugs the screen edge.
- Blank the share at any time without dropping it.
- Answer the screencast portal directly, with no picker (see [Portal chooser](#portal-chooser)).
- Video only. Audio is untouched.

## Install

1. **Install it.** `nix profile install github:maciej-rosiek/wl-viewfinder`, or [build it](#building)
   and `sudo make install`. Everything it drives is a separate program -- see
   [dependencies](#dependencies); wl-mirror 0.18 or newer is the one nothing works without.

2. **Bind the aim.** A viewfinder is re-aimed far more often than it is started:

   ```
   bindsym $mod+Shift+b exec wl-viewfinder window
   ```

3. **Point the portal at the chooser**, to have the sharing dialog answered for you:

   ```ini
   # ~/.config/xdg-desktop-portal-wlr/config
   [screencast]
   chooser_type=dmenu
   chooser_cmd=/absolute/path/to/wl-viewfinder chooser
   ```

   The path has to be absolute, and under sway it has to be a wrapper -- both traps are in
   [docs/portal-chooser.md]. Then `systemctl --user restart xdg-desktop-portal-wlr`.

4. **Check it before you need it**, because a call is a poor place to find out. Press the binding: a
   red rectangle should appear around the focused window, with the share waiting behind it.

   ```sh
   wl-viewfinder status                        # the units, the region, the followed window
   cat "$XDG_RUNTIME_DIR/wl-viewfinder/sink"   # the monitor to pick in the dialog
   ```

   The rectangle is the whole check on any compositor. Under sway that second line matters too: it
   names a monitor no screen shows, and it is what the dialog is answered with.

5. **Then a real one.** With the viewfinder still armed, press Share in a browser and pick that
   monitor -- or pick nothing, if the chooser is wired up and answers for you. Re-aim mid-call with
   the same binding: the picture should follow without the call noticing. `wl-viewfinder off` stops
   it, and it unplugs itself anyway once the last capture ends.

Arming *before* pressing Share is the one step that is not optional. Why, and what it costs to
forget, is under [Portal chooser](#portal-chooser).

## Usage

```
wl-viewfinder window   aim at the focused window and follow it
wl-viewfinder select   drag out a region
wl-viewfinder output   the whole focused output
wl-viewfinder blank    share, but show nothing
wl-viewfinder off      stop
wl-viewfinder label    one line: what is being shared
wl-viewfinder status   the units, the region and the followed window
wl-viewfinder chooser  answer a portal request with the viewfinder
```

Bind what you use:

```
bindsym $mod+Shift+b exec wl-viewfinder window
```

### What to share

Under sway the mirror sits on a **headless output**: a screen that renders and can be captured, but
that no monitor shows. Nothing appears on your own desktop, and the thing to pick in the sharing
dialog is that monitor, `HEADLESS-n`. You pick it once, at the start of the call.

Everywhere else, and with `WL_VIEWFINDER_SINK=window`, the mirror is an ordinary window titled
**viewfinder**. Share that window instead. It has to stay mapped and visible, or it makes no frames.

The headless outputs are parked far away from the real ones, so that sway never hands the pointer
across to a screen nobody can see. They are still part of the layout while a share is armed, which
anything that captures the *layout* rather than an output will find: `grim` with no `-o` returns a
mostly black image tens of thousands of pixels wide. Give such a tool an output, or the bounding box
of the real ones.

### Aiming

`window` follows the window, but not the focus. You re-aim by hand, so glancing at another window
never shares it by accident. Leaving the window's workspace blanks the share and coming back
restores it, because an output only renders the workspace in front of you. `output` has no such
guard.

`blank` aims at a second, always-black output instead of stopping the mirror, so the share is never
dropped. Join a call blank, and go back to `blank` to hide the room again.

### Stopping

When the last capture ends the viewfinder goes back to `blank`, then stops and unplugs its outputs.

`off` stops it by hand -- but not while something is still capturing. Unplugging an output out from
under a live capture is a protocol error, and it takes xdg-desktop-portal-wlr down with it.

### Environment

| variable | default | |
| --- | --- | --- |
| `WL_VIEWFINDER_SINK` | `auto` | `headless`, `window`, or `auto` for headless under sway |
| `WL_VIEWFINDER_SINK_MODE` | `1920x1080` | the shared resolution; other ratios letterbox into it |
| `WL_VIEWFINDER_SINK_WORKSPACE` | `viewfinder` | sway workspace that parks the headless outputs |
| `WL_VIEWFINDER_IDLE_GRACE` | `20` | seconds between the last capture ending and the teardown |
| `WL_VIEWFINDER_IDLE_START` | `300` | seconds an armed viewfinder waits for its first capture |

The last two need `pw-dump`.

## Portal chooser

`wl-viewfinder chooser` answers an [xdg-desktop-portal-wlr] request with the viewfinder. It draws
nothing: it reads the source list on stdin and prints one line back. Stock xdpw, no patches.

```ini
# ~/.config/xdg-desktop-portal-wlr/config
[screencast]
chooser_type=dmenu
chooser_cmd=/absolute/path/to/wl-viewfinder chooser
```

**Arm the viewfinder before you press Share.** xdpw lists its sources *before* it runs the chooser,
so a source made during the request cannot be picked. Forgetting costs one press: the first request
fails and the app falls back to its own picker, but the viewfinder is running by then, and the
second press is answered silently.

The path has to be absolute, and under sway it needs a wrapper. Both traps, and what the chooser
answers for which request, are in [docs/portal-chooser.md].

## Supported compositors

`select`, `output` and `blank` work on any wlroots-based compositor. They need `wlr-layer-shell` and
a capture protocol wl-mirror supports (`wlr-screencopy` or `ext-image-copy-capture-v1`): sway, niri,
Hyprland, river, Wayfire, COSMIC, labwc. GNOME and KDE implement neither and cannot work.

`window`, and the headless output the share sits on, are sway-only for now. Everything that knows
this is sway is in one marked block of the script -- see [porting](docs/internals.md#porting).

## Dependencies

- [wl-mirror] >= 0.18, which ships `wl-present` -- the engine
- a systemd user session -- the transient units
- `flock` (util-linux) -- the commands that build the mirror are re-entrant
- `slurp` -- `select`, and `output` where there is no sway
- `sway` and `jq` -- `window`, the headless output, and blanking off a workspace
- `pw-dump` (pipewire) -- noticing that the call has ended

## Building

Needs a C compiler, `pkg-config`, `wayland-scanner` and the `wayland-client` headers. The Wayland
protocols are vendored.

```sh
make
sudo make install    # PREFIX=/usr/local
```

With Nix: `nix run github:maciej-rosiek/wl-viewfinder -- window`, or add the flake's
`packages.default` to your system or home-manager packages.

## How it works

`wl-present` mirrors a region of an output into a window. wl-viewfinder aims that region, draws the
frame around it, and re-aims with `wl-present set-region` -- which never resizes the window, so
PipeWire never renegotiates and a live share survives being re-aimed.

The unit graph, the frame client, how the tool notices the call has ended, and how to port it to
another compositor are in [docs/internals.md].

## License

MIT, see [LICENSE](LICENSE).

[wl-mirror]: https://github.com/Ferdi265/wl-mirror
[wlroots#4037]: https://gitlab.freedesktop.org/wlroots/wlroots/-/issues/4037
[xdg-desktop-portal-wlr]: https://github.com/emersion/xdg-desktop-portal-wlr
[docs/portal-chooser.md]: docs/portal-chooser.md
[docs/internals.md]: docs/internals.md
