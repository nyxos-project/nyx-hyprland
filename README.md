# nyx-hyprland

The patches NyxOS and the Nyx desktop apply to Hyprland, kept as one series per Hyprland
release in `patches/<version>/`, with the Arch package (`arch/`) and Hyprland plugins
(`plugins/`).

- NyxOS builds Hyprland from its flake at the tag nyx-packages pins, with
  `patches/<that version>/` (nyx-packages `pkgs/hyprland.nix`). A tag without a series
  fails to evaluate; patches apply without fuzz.
- The series is branch `nyx/<version>` in the organization's fork,
  https://github.com/nyxos-project/Hyprland, exported with `git format-patch`.
- `./update.sh <version>` (Arch) tries the newest series on a new release and rebases it
  when it doesn't apply; it never falls back to an older series. Then it writes
  `arch/PKGBUILD`, raising `pkgrel` when the same release gets a different series.

## Hyprland 0.57.2

| Patch | What it does | Files |
| --- | --- | --- |
| 0001 gestures: add workspace_swipe_wrap | `gestures:workspace_swipe_wrap`: swiping past the last workspace comes round to the first (Settings › Loop around) | gesture, config values, hyprtester `gestures.cpp` |
| 0002 output: add virtual region partitioning | Splits a monitor's area into regions | new `VirtualRegions.*`, gtest |
| 0003 output: add virtual monitors | Regions as monitors of their own (`hl.virtual_monitor`, behind `experimental:virtual_monitors`) | new `VirtualMonitorManager.*`, monitor and config hooks |
| 0004 render: composite virtual regions on their host | Draws the regions on the real output | renderer, monitor scene |
| 0005 workspaces: per-region workspace selector and pointer | Workspace selectors and the pointer follow the region | resolver, actions, gesture |
| 0006 virtual monitors: merge, restore, physical fullscreen | Dispatchers | Lua dispatchers |
| 0007 ipc: expose virtual monitors | `hyprctl` and Lua see the regions | IPC, Lua monitor |
| 0008 hyprtester: add virtual monitor tests | hyprtester `virtual_monitors` suite | new test |
| 0009 virtual monitors: workspaces = 0 | Leaves workspace numbering to the regions | `VirtualMonitorManager.*` |
| 0010 virtual monitors: fixes from the first run on a NyxOS VM | Host found by name, workspaces restored, focus restored, rules removed | `VirtualMonitorManager.*`, dispatchers, workspace rules |
| 0011 hyprtester: workspace_swipe_wrap checks the 0.57 action | Keeps 0001's test current | hyprtester `gestures.cpp` |
| 0012 fullscreen: misc:fullscreen_own_workspace | A fullscreened window gets a workspace of its own (`fullscreen-<ws>-<n>`), and goes back when it leaves fullscreen; on by default in NyxOS | new `FullscreenSpaces.*`, compositor, monitor, resolver, gesture, hyprtester `fullscreenSpaces.cpp` |
| 0013 layers: top layers slide with the workspace | The bar and dock slide away with a workspace swipe into a fullscreen workspace | renderer, gesture |

[docs/virtual-monitors.md](docs/virtual-monitors.md) describes virtual monitors in
detail; [docs/testing.md](docs/testing.md) how the series is tested.

## Hyprland 0.56.2

`patches/0.56.2/` is the series the Arch package on the development laptop was built
with: workspace swipe wrap, top layers kept below fullscreen windows, fullscreen on its
own workspace and the layer slide; no virtual monitors. It goes once that laptop runs
0.57.2.
