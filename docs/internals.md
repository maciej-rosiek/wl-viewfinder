# Internals

## The units

```
wl-viewfinder window
      |
      +-- asks the compositor for the focused window's geometry
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
from the real outputs rather than beside them, because the compositor hands the pointer across a
shared edge -- an adjacent sink would swallow the cursor onto a screen nobody can see.

Under sway the outputs are found by the workspace parked on them, never by name: sway numbers a
fresh headless output per session, so `HEADLESS-1` is only ever right the first time. Hyprland
creates a headless output under any name it is given, so there the sink is simply the output called
`viewfinder`.

## Landing on the sink

Three things put the mirror window on the sink, because the obvious one is not reliable on its own.

`wl-present mirror --fullscreen-output <sink>` asks for fullscreen on the sink, so the first frame
of the share is already the whole output rather than a window with borders around it. It is a poor
*placement*, though: wl-mirror sends the request from its main thread while mesa commits the first
buffer from another, and mesa often wins. sway maps the window before the request lands, in front of
you, retiling your workspace for as long as it takes the fullscreen to arrive.

So the window is claimed before it maps, with a sway `assign` rule matching this script's own mirror
(`app_id` and title). Assignments are read while the view is being mapped, which is the only
placement early enough to beat that race. The rule names the sink *output* rather than the
`viewfinder` workspace: sway keeps runtime criteria until it is reloaded, and it skips an assign
whose output no longer exists, so rules left over from earlier sinks do nothing, where a rule naming
a workspace would keep conjuring one.

Finally the mirror is moved onto the sink and fullscreened by hand, from the mirror unit's
`ExecStartPost` -- which is what covers a mirror that came back by itself after a crash.

The focus sway hands a newly mapped window is put back where it was, for the same reason: a share
that starts by moving your keyboard onto a screen nobody can see is worse than one that flickers.

Hyprland has the same three layers under other names. A static window rule matching the mirror's
class and title (`workspace name:viewfinder silent`, `fullscreen`) is Hyprland's `assign`, applied
as the window opens; a workspace rule binding `name:viewfinder` to the `viewfinder` output, set
before the output exists, is what makes that workspace the output's first one rather than the next
free number -- renaming would not do, a renamed workspace keeps its number, and a numbered
workspace on a headless output is a `$mod+n` that focuses a screen nobody can see. The by-hand
fallback moves the window onto that workspace and fullscreens it by address, focus untouched.

Hyprland's sink sits at a large *negative* x rather than a positive one. Hyprland re-lays its
outputs out on every change, explicitly positioned ones first and `auto` ones to the right of
everything placed so far, so a sink at a positive x would push every auto-placed real screen past
itself; one ending left of x=0 never enters that calculation. wl-mirror parses the resulting
`-30000,0` region through `strtoul`, which wraps it back to the right `int32_t`.

Since 0.55 Hyprland has two config languages, and `hyprctl` speaks whichever the running config is
written in: `keyword` plus the classic dispatchers under hyprlang, `eval` plus `hl.*` under Lua.
The backend writes in Lua only -- every rule and dispatcher has exactly one spelling -- and probes
with `hyprctl eval` before building the sink, so a Hyprland on `hyprland.conf` gets one line saying
why rather than a half-built sink. Reads are `hyprctl -j`, which is the same in both.

## Knowing that the call has ended

The portal offers no way to ask "is anybody still capturing", so the watcher reads the PipeWire
graph instead. Each portal gives every cast a node named after itself -- `xdpw-stream-<random>`,
`xdph-streaming-<random>` -- that lives exactly as long as the cast does. `pw-dump -m` opens with
the graph as it stands and then prints one object per change; a removal arrives as an object with
no info at all, so removals are matched against the ids already seen rather than by name.

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

Outside is decided per side, against the output the region is on. A side the output leaves no room
on -- every side of a whole-output share, one side of a window snapped to a screen edge -- is drawn
just inside the region instead. That band is in the share, and it is the alternative to a rectangle
nobody can see: sharing a whole screen would otherwise be the one share with no mark on it at all.

```
wl-viewfinder-frame [-b border] [-c rrggbb]   draw a frame, take regions on stdin
wl-viewfinder-frame -k <output>               fill an output with opaque black and stay
wl-viewfinder-frame -l                        list outputs as `name x,y wxh` and exit
```

`-k` is the same client filled opaque black over a whole output, which is what keeps the blank
source blank above whatever a desktop shell paints there. `-l` reads `xdg-output`, so the
region-to-output lookup needs no compositor.

## Porting

Everything that knows which compositor this is lives between the `--- compositor backends` markers
in `wl-viewfinder`: one set of functions per compositor, `sway_*` and `hypr_*`, and `bind_backend`
aliases the bare names to whichever set `detect_backend` picked. A port is one more set of the
seventeen, in two groups:

- **aiming** -- `focused_window`, `region_of_id`, `focused_output`, `subscribe_events`,
  `workspace_of_id`, `visible_workspace_of_output`, `workspace_visible`
- **the headless sink** -- `mirror_line`, `sink_name`, `blank_name`, `new_headless`,
  `blank_region`, `assign_mirror`, `focus_mark`, `focus_restore`, `park_mirror`, `drop_sink`

plus a line in `detect_backend` that recognises the session. `chooser`, `blank`, `off`, `park` and
`label` all reach the second group, so a port is not just `window`. Without a backend the tool
degrades to `WL_VIEWFINDER_SINK=window`: an ordinary mirror window, shared as a toplevel.

Regions are `x,y WxH` in compositor-global logical coordinates; window ids are whatever the
compositor calls a window, opaque outside the backend. `output_geometry` and `output_at` answer
output questions from xdg-output, so a backend only has to *name* an output.

The follower polls every 300 ms because neither compositor announces a resize: sway's `window`
event has no resize change type, and Hyprland's socket has no geometry event at all. The event
stream only makes the common cases instant, and a backend without one (Hyprland without `socat`)
simply polls. A compositor with a real geometry-changed event should drive it from events instead.

The Hyprland backend has been checked against Hyprland's source and a scripted `hyprctl`, not
against a running Hyprland. What to watch on a first run: the sink appearing at `-30000,0` in
`hyprctl monitors` with `viewfinder` as its workspace and no numbered workspace on it, the mirror
landing there fullscreen without the focus moving, and `hyprctl reload` -- it rebuilds the Lua
state and with it drops every rule set at runtime, sink placement included, so re-arm after one.

## Notes

- Only tested at `scale = 1`. Geometry comes from `xdg-output` logical coordinates, so fractional
  scaling should be handled correctly, but nobody has checked.
- The rectangle shows whatever is under it, including a window dragged on top of the aimed one.
