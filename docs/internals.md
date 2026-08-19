# Internals

## The units

```
wl-viewfinder window
      |
      +-- asks sway for the focused window's geometry
      +-- wl-present mirror <output> -r <region>    the shared window
      +-- wl-viewfinder-frame                       the red frame, fed through a fifo
      +-- wl-viewfinder follow                      keeps the region on that window
      +-- wl-viewfinder watch                       gives up the aim when the call ends
      +-- wl-viewfinder-frame -k <blank output>     keeps the blank source black
```

Five transient `systemd --user` units. The four around the mirror are `BindsTo` it, so closing the
mirror window tears down the rest.

A sixth, `wl-viewfinder-off`, is started detached when the idle watcher gives up. A teardown bound
to the units it is stopping would be killed halfway through and leave the headless outputs plugged
in.

The commands that build the mirror take a `flock` first: the chooser is re-entrant, and two portal
requests arriving together would otherwise race to create the same units.

## Re-aiming

Re-aiming feeds `wl-present set-region` on the running mirror. The window never resizes, so PipeWire
never renegotiates and the share survives.

That is also why the headless output has a fixed size and a stable name. It is positioned far away
from the real outputs rather than beside them, because sway hands the pointer across a shared edge
-- an adjacent sink would swallow the cursor onto a screen nobody can see.

The outputs are found by the workspace parked on them, never by name: sway numbers a fresh headless
output per session, so `HEADLESS-1` is only ever right the first time.

The mirror asks to be fullscreen *on the sink* as it starts, rather than being moved there once it
is up: sway reads the requested fullscreen output before it maps the window, so the window opens on
the sink's workspace and the workspace in front of you is never retiled. It is moved as well, from
the mirror unit's `ExecStartPost`, which covers a mirror that came back by itself after a crash.

## Knowing that the call has ended

The portal offers no way to ask "is anybody still capturing", so the watcher reads the PipeWire
graph instead. xdpw gives every cast a node named `xdpw-stream-<random>` that lives exactly as long
as the cast does. `pw-dump -m` opens with the graph as it stands and then prints one object per
change; a removal arrives as an object with no info at all, so removals are matched against the ids
already seen rather than by name.

An empty graph is believed only after a second, because a client that renegotiates drops its node
and takes a new one straight away.

Then the aim is given up -- a red rectangle must not outlive the call it was describing. The mirror
itself is torn down `WL_VIEWFINDER_IDLE_GRACE` seconds later, not at once: Chrome runs its dialog
and the call as two separate sessions, so there is a moment with nothing capturing in the middle of
joining. If the aim moved during the wait, everything stays up -- that is somebody lining up the
next share.

## The frame

`wl-viewfinder-frame` is a small `wlr-layer-shell` client: overlay layer, empty input region, an shm
buffer with a transparent interior. It is drawn just outside the region, so it never appears in the
share, and clicks pass through it.

```
wl-viewfinder-frame [-b border] [-c rrggbb]   draw a frame, take regions on stdin
wl-viewfinder-frame -k <output>               fill an output with opaque black and stay
wl-viewfinder-frame -l                        list outputs as `name x,y wxh` and exit
```

`-k` is the same client filled opaque black over a whole output, which is what keeps the blank
source blank above whatever a desktop shell paints there. `-l` reads `xdg-output`, so the
region-to-output lookup needs no compositor.

## Porting

Everything that knows this is sway lives between the `--- compositor backend` markers in
`wl-viewfinder`. Eighteen functions, in two groups:

- **aiming** -- `focused_window`, `region_of_id`, `focused_output`, `subscribe_events`,
  `workspace_of_id`, `visible_workspace_of_output`, `workspace_visible`
- **the headless sink** -- `mirror_identifier`, `sink_enabled`, `output_of_workspace`, `sink_name`,
  `blank_name`, `new_headless`, `ensure_sink`, `ensure_blank`, `blank_region`, `park_mirror`,
  `drop_sink`

`chooser`, `blank`, `off`, `park` and `label` all reach the second group, so a port is not just
`window`. Without it the tool degrades to `WL_VIEWFINDER_SINK=window`: an ordinary mirror window,
shared as a toplevel.

Regions are `x,y WxH` in compositor-global logical coordinates.

The follower polls every 300 ms because sway's `window` IPC event has no resize change type. A
compositor with a real geometry-changed event should drive it from events instead.

## Notes

- Only tested at `scale = 1`. Geometry comes from `xdg-output` logical coordinates, so fractional
  scaling should be handled correctly, but nobody has checked.
- The rectangle shows whatever is under it, including a window dragged on top of the aimed one.
