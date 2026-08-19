# Portal chooser

`wl-viewfinder chooser` is the whole tool as an `xdg-desktop-portal-wlr` source chooser. It draws
nothing. xdpw writes its list of sources to the chooser's stdin, one per line, and reads one of
those lines back:

```
Monitor: HEADLESS-2 headless output
Window: Slack (67108866)
```

```ini
# ~/.config/xdg-desktop-portal-wlr/config
[screencast]
chooser_type=dmenu
chooser_cmd=/absolute/path/to/wl-viewfinder chooser
```

## What it answers

A request that can take a window is answered with the viewfinder: the `Monitor:` line of the
headless output it sits on, or -- with `WL_VIEWFINDER_SINK=window` -- the `Window:` line whose
identifier matches the mirror.

A request that cannot take a window at all is answered with the **real focused output**, and no
viewfinder is started. That is somebody asking to share their whole screen, and a viewfinder has
nothing to add to "all of it" but a scaling pass. Chrome asks once per tab of its dialog, each with
a single-type mask, so its Entire Screen tab lands here and its Window tab lands above.

## Arm it first

xdpw builds its source list *before* it runs the chooser, and matches the reply against that list. A
source the chooser creates while the request is in flight is not in the list, so it can never be
picked. Hence: arm the viewfinder, then press Share.

```
bindsym $mod+Shift+b exec wl-viewfinder window
```

Forgetting is not fatal. The chooser still starts the viewfinder, but the request it was answering
has nothing to pick and fails, and the app falls back to its own picker. Press Share again: the
viewfinder exists now, and the second request is answered silently.

`WL_VIEWFINDER_IDLE_START` (default 300 seconds) is how long an armed viewfinder waits for a capture
before it gives up and unplugs itself.

Patching xdpw to re-read its sources after the chooser returns also works -- two
`wl_display_roundtrip` calls around the chooser -- and is how this ran until August 2026. It is not
worth it. A portal patch makes the tool unusable for anyone unwilling to run a patched portal, and
arming first buys the same result.

## Two traps in chooser_cmd

**The path has to be absolute.** xdpw runs the chooser with a sandboxed PATH holding coreutils,
findutils, grep, sed and systemd, and nothing else. A bare name dies with `command not found`, and
the symptom is not an error message: Chrome's Window tab silently bounces back to Chrome Tab.

**Under sway it needs a wrapper**, because the tool deliberately does not depend on the compositor
and so does not put sway's `bin` on its own PATH:

```sh
#!/usr/bin/env bash
PATH=/path/to/sway/bin:$PATH
exec /absolute/path/to/wl-viewfinder chooser
```

Keep the line short either way. xdpw parses its config with inih, which silently truncates a line
past ~200 characters; the symptom is again `command not found`.

## Debugging

Run xdpw with `-l TRACE` and watch the journal -- it logs the chooser command it ran and the line it
got back. For one session:

```sh
systemctl --user edit --runtime xdg-desktop-portal-wlr    # add -l TRACE to ExecStart
systemctl --user restart xdg-desktop-portal-wlr
journalctl --user -fu xdg-desktop-portal-wlr
```

The chooser also runs by hand. It arms the viewfinder as a side effect, exactly as it would during a
real request:

```sh
printf 'Monitor: HEADLESS-2 headless\nWindow: Slack (67108866)\n' | wl-viewfinder chooser
```
