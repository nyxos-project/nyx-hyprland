# Testing virtual monitors

## Never run a test compositor in a graphical session

`hyprtester` starts its own Hyprland. In 0.57 that Hyprland ignores
`HYPRLAND_HEADLESS_ONLY`, so it picks its backends from the environment:

- With `WAYLAND_DISPLAY` set, it opens a window on the running desktop.
- Without it, it reaches for the GPU through the login session, which took the running
  Hyprland down.

Run hyprtester only somewhere with no session to disturb: CI, a VM, or a TTY on a
machine nobody is using. Never run it from a terminal inside a desktop session.

## Automated

**Unit tests** run in process, with no display, session or GPU output: the null
Aquamarine backend and an in-memory Wayland display.

```sh
cd ~/Projects/Hyprland
PKG_CONFIG_PATH=$HOME/Projects/nyx-hyprland/.deps/prefix/lib/pkgconfig \
  cmake --build build --target hyprland_gtests
./build/hyprland_gtests --gtest_filter='VirtualRegions.*:*Monitor*:*Workspace*:*Resolver*'
```

The private prefix holds hyprutils 0.14.3 and aquamarine 0.16.0 (what 0.57 needs) for a
laptop whose package database predates them. Once Arch's `extra` versions are installed, drop
`PKG_CONFIG_PATH`.

**Nyx crates** (pure tests, no Hyprland). Until `nyx-core` is pushed and the pins move,
point Cargo at the local checkout:

```sh
P='patch."https://github.com/design-nexus/nyx-core.git".nyx-desktop.path="../nyx-core/crates/nyx-desktop"'
(cd ~/Projects/nyx-core && cargo test -p nyx-desktop --no-default-features --lib)
(cd ~/Projects/nyx-shell && cargo test --config "$P" -p nyx-shell-proto -p nyx-shell-core --lib)
(cd ~/Projects/nyx-bar && cargo test --config "$P" -p nyx-bar-workspaces)
(cd ~/Projects/nyx-dock && cargo test -p nyx-dock-core --lib)
```

**Integration tests** are `hyprtester/src/tests/main/virtual_monitors.cpp`, which uses
`foot` instead of kitty. Run them only somewhere safe, as described above.

```sh
cd hyprtester
../build/hyprtester/hyprtester -b ../build/Hyprland -p plugin/hyprtestplugin.so -c test.lua \
  virtualMonitorsDisabledByDefault virtualMonitorsTwoRegions \
  virtualMonitorsThreeRegionsAndRatios virtualMonitorsRejectsBadLayouts
```

They cover:
- the feature being off by default;
- exact two- and three-region geometry, center-focused and custom proportions, and
  resizing in place;
- the host leaving the layout;
- workspace isolation and looping within each region (`m±1`, `v~N`);
- a window opening on the focused region, region fullscreen, and moving between
  regions;
- merge and restore keeping windows and workspaces;
- disabling back to an ordinary monitor;
- an invalid layout being refused, an unconfirmed change reverting, a confirmed one
  staying, `disable_all`, and `output remove` refusing regions.

What has actually run:
- In the only runs so far (which put a window on the desktop, see above), the first
  three passed.
- `virtualMonitorsRejectsBadLayouts` passed the first confirm-timeout revert, then hung
  on the second `count = 3, confirm = 1` change. That is unexplained; reproduce it in a
  safe environment before relying on confirm timeouts.

## Manual checklist (physical ultrawide)

Install `hyprland-nyx` 0.57.2, log in again, and set
`experimental:virtual_monitors = true`. Keep a second way in, such as an SSH session or
a TTY, with `hyprctl dispatch 'hl.dsp.virtual_monitor.disable_all()'` ready.

| Area | Check |
|---|---|
| Split | 2 and 3 equal, center-focused and custom regions: no gap, no overlap, sharp text at fractional scale |
| Isolation | Switch every workspace on each region; the others never change |
| Looping | Workspace 10 → next → 1 and back on each region, by keyboard, wheel and swipe |
| Keys | Super + 1…0 on each region reach that region's workspaces (NyxOS); Omarchy's blocks per region (Omarchy) |
| Targeting | `workspace_target = pointer`: shortcuts follow the pointer's region |
| Windows | Tiled and floating windows moved, and dragged across region edges; geometry right afterwards |
| Fullscreen | In each region, with the others still usable; physical fullscreen merges and comes back |
| Merge | Join and split again with apps open; nothing closes, workspaces return |
| Layout changes | 1 → 2 → 3 → 2 with apps open; every window stays reachable |
| Recovery | Change with `confirm = 15` and don't confirm: reverts; `nyx display virtual disable --all` |
| Shell | Bar, Dock (per region), widgets, Control Center and notifications on the right region |
| Apps | GTK4, Qt6, XWayland, a game (XWayland mode change sees the region), a video |
| Capture | Portal screen share of a region, a window; whole screen only while merged |
| Lifecycle | Unplug and replug, suspend and resume, log out and in: regions and workspaces come back |
| Performance | Frame time and GPU memory split versus merged, cursor latency, 120 Hz and above |
| Off | Turn the feature off: ordinary Hyprland behaviour |

None of these has been checked yet.
