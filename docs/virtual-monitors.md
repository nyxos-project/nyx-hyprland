# Virtual monitors

One physical output split into one to three monitors side by side. Each region is a
real Hyprland monitor with its own `wl_output`, workspaces, layout, fullscreen, layer
surfaces and screencopy target.

The feature is off by default and still experimental. It has run in a headless test
compositor and, on 2026-11-10, on the NyxOS VM's virtio-gpu output (1280×800 split in
two): regions, their own bars and docks, windows, `v~N`, merge and restore, physical
fullscreen and `disable_all` all worked there after patch 0010. It has not run on a real
ultrawide yet; see "Known limitations".

## Using it

**Turning it on (Hyprland Lua)**

```lua
hl.config({ experimental = { virtual_monitors = true } })
hl.virtual_monitor({ output = "desc:Samsung Odyssey G95NC H1AK", count = 3, layout = "center-focused" })
hl.virtual_monitor({ output = "DP-3", regions = { 20, 55, 25 }, workspaces = 10 })
```

`hl.virtual_monitor` fields:

| Field | Meaning |
|---|---|
| `output` | The physical output: a connector name or `desc:` |
| `regions` or `count` + `layout` | Proportions, or a preset: `equal`, `center-focused` |
| `workspaces` | Persistent workspaces on each region after the first (1 to 10); `0` leaves numbering to the config |
| `enabled` | `false` means no split |
| `confirm` | Seconds a runtime change waits for `hl.dsp.virtual_monitor.confirm()` before reverting |
| `profile` | A label only |

**Options**
- `virtual_monitors:workspace_target = focus | pointer`: which region workspace actions
  and swipes act on.
- `virtual_monitors:fullscreen = region | physical`: what fullscreen does on a region.
- `virtual_monitors:min_region_width`: in logical pixels, 640 by default.

**Dispatchers**
- `hl.dsp.virtual_monitor.merge({ monitor })`, `restore`, `toggle_merge`
- `fullscreen({ window })`, which is also `hl.dsp.window.fullscreen({ mode = "physical" })`
- `confirm()`
- `disable_all()`: recovery, lasting until the next config reload

**Workspace selector**
- `v~N`: workspace N of the target region, or plain N on any other monitor.
- Nyx's generated file rebinds Super + 1…0 (and with Shift, and Shift + Alt) to `v~N`
  while a screen is split, so each region's keys reach its own workspaces.

**IPC**
- `hyprctl monitors` has a `virtual` field: `null`, a region's host, index, count,
  pixel offset and workspace base, or a host's list of regions.
- `hyprctl virtualmonitors [-j]` lists the groups and configs.
- socket2 posts `virtualmonitor>>HOST,STATE` and `virtualmonitorv2>>HOST,STATE,REGIONS`,
  where the state is `split`, `merged` or `disabled`.
- Lua monitor objects have `is_virtual`, `virtual_host`, `virtual_index` and
  `virtual_regions`.

**Nyx**
- Settings › Displays › Virtual monitors.
- `nyx display virtual …`. The recovery command is
  `nyx display virtual disable --all`.
- Both write `~/.config/nyx/virtual-monitors.toml`, under NyxOS's defaults and an
  administrator's overrides, and render `~/.config/hypr/nyx-virtual-monitors.lua`.
  That file starts with `if not hl.virtual_monitor then return end`, so a stock
  Hyprland loads it without errors.

## Design

```
physical output (DRM)  ← host CMonitor: hidden like a mirror, draws its regions' copy framebuffers
   ├─ DP-3.v1  headless output → ordinary CMonitor (workspaces 1–10 as before)
   ├─ DP-3.v2  headless output → ordinary CMonitor (workspaces 201–210, named 1–10)
   └─ DP-3.v3  headless output → ordinary CMonitor (workspaces 301–310)
```

**Regions**
- `CVirtualMonitorManager` (`src/output/VirtualMonitorManager.*`) creates a headless
  output per region. Hyprland's monitor-rule lookup asks the manager first, so a region's
  mode, scale and refresh rate come from its host.
- `partition()` (`src/output/VirtualRegions.*`) puts every boundary on a whole logical
  pixel at the host's scale, and the regions cover the host exactly.

**The host**
- It is hidden through `CMonitor::isHiddenFromLayout()` at each place mirrors are
  already excluded: active monitors, `wl_output`, layer surfaces, scene drawing, cursor.
- Its hardware cursor is turned off by locking it to the software cursor, which the
  host never draws. Regions draw the software cursor.
- The arranger places the host as a whole; its regions follow at their pixel offsets,
  and so do their XWayland positions.

**Rendering**
- A region keeps a copy framebuffer, as a mirror source does, and on each commit damages
  its host at its offset.
- `IHyprRenderer::renderVirtualRegions` draws the copies side by side.
- Direct scanout is blocked on regions and hosts.

**Identity**
- A region's name is `<connector>.v<n>`.
- Its make and model are the host's, and its serial is `<serial>#v<n>`, so Nyx's stable
  id is the screen's followed by `#v<n>`.
- Its description starts with "Virtual n of N on …", and `desc:` selectors for the host
  never match a region. Nothing claims to be a connector.

**Workspaces**
- Under Omarchy, which numbers every monitor's workspaces itself (a block of ten per
  screen, regions included), the generated file sets `workspaces = 0`: regions own no
  range, `v~N` is plain N, and Omarchy's own keys are kept.
- The first region of the first split output keeps the ordinary workspaces, so splitting
  moves nothing.
- Region slot `s` (counted across groups, three per group) owns `s*100+1` to
  `s*100+N` through persistent workspace rules bound to it, named `1` to `N`.
- `m±1` already loops within a monitor. The wraparound animation uses a region's own
  workspaces.

**Merge (full width)**
- Merging saves each region's active workspace and the focused region, shows the host
  again, moves every workspace onto it, and destroys the region outputs.
- Restore recreates the regions, puts each workspace back on the region it was on
  (falling back to its ID range), and shows the saved workspaces again, recreating them
  if they were empty and got cleaned up.
- Physical fullscreen is merge plus fullscreen, restored when the window leaves
  fullscreen or closes.

**Hotplug**
- When the host goes away its regions go too, and the lifecycle policy remembers where
  their workspaces belong, by region name.
- When the host comes back, the group is rebuilt with the same names and the workspaces
  return.

**Safety**
- Everything is off unless `experimental:virtual_monitors` is set, and
  `HYPRLAND_NO_VIRTUAL_MONITORS=1` overrides it.
- An invalid layout leaves the plain monitor and shows a notification.
- `hyprctl output remove` refuses regions.

## Patch series

Branch `nyx/0.57.2` in `~/Projects/Hyprland`, exported to `patches/0.57.2/` (`patches/0.57.1/` has the same series without 0009):

| Patch | Upstream files touched |
|---|---|
| 0001 gestures: add workspace_swipe_wrap | `UnifiedWorkspaceSwipeGesture.cpp`, `ConfigValues.cpp`, hyprtester `gestures.cpp` |
| 0002 output: add virtual region partitioning | new `VirtualRegions.*`, new test |
| 0003 output: add virtual monitors | new `VirtualMonitorManager.*`; `Monitor.{hpp,cpp}`, `ConfigValues.cpp`, `MonitorRuleManager.cpp`, `MonitorLayoutController.cpp`, `ConfigManager.cpp`, `Compositor.cpp`, `LuaBindingsConfigRules.cpp`, `MonitorState.cpp`, `ProtocolManager.cpp`, `LayerSurface.cpp` |
| 0004 render: composite virtual regions on their host | `Renderer.{cpp,hpp}`, `MonitorScene.cpp`, `MonitorResources.cpp`, `OutputCommitCoordinator.cpp` |
| 0005 workspaces: per-region selector and pointer targeting | `Resolver.cpp`, `ConfigActions.cpp`, `UnifiedWorkspaceSwipeGesture.cpp` |
| 0006 virtual monitors: dispatchers | `LuaBindingsDispatchers.cpp` |
| 0007 ipc: expose virtual monitors | `Commands.cpp`, `LuaMonitor.cpp` |
| 0008 hyprtester: add virtual monitor tests | new `virtual_monitors.cpp` |
| 0009 virtual monitors: workspaces = 0, merge remembers each workspace's region | `VirtualMonitorManager.*` only |
| 0010 virtual monitors: fixes from the first run on a NyxOS VM (host found by name, workspaces restored to their own region, monitor-less persistent workspaces left in place, focus restored, rules removed) | `VirtualMonitorManager.*`, `LuaBindingsDispatchers.cpp`, `WorkspaceRuleManager.{hpp,cpp}` |

**Upstream changes:** about 400 lines across 22 files, all small hooks; the rest is in
new files. With the feature off, every hook takes the upstream path: the checks are on
`m_virtual`, which stays empty.

**Rebasing**
- `./update.sh <version>` tries the newest series as is, then rebases it with
  `git am -3`.
- If that conflicts, check out `nyx/<version>` from `v<version>`, run
  `git am -3 patches/<newest>/*.patch`, resolve, then run
  `./update.sh --export nyx/<version> <version>`.
- The likeliest conflicts are in `Monitor.cpp` (`isMirror` call sites and the DS-block
  list), `Renderer.cpp` (`renderMirrored`, `damageMirrorsWith`) and the Lua binding
  registration lists.

**Packaging**
- `hyprland-nyx` (Arch) applies every patch in the chosen series directory.
- NyxOS builds Hyprland's own flake at `v0.57.2` with the same series, read from this
  repository (`nyx-packages`: inputs `hyprland` and `nyx-hyprland`). That needs
  `nix flake lock` after this repository is pushed.

## Known limitations

- **Frame pacing:** each region renders on its headless output's own timer at the
  host's refresh rate, and the host composites the latest copies. That adds up to one
  frame of latency, and region frames aren't aligned to the host's vblank. Presentation
  feedback to clients on a region comes from the headless output, not the panel.
- **What a split screen loses:**
  - direct scanout, tearing, VRR and HDR per region;
  - exclusive modes;
  - rotated hosts.

  Physical fullscreen and merging give the screen back whole, with all of these.
- **Cursor and cost:** the cursor is drawn in software on regions. Each frame costs one
  extra GPU copy of the damaged area.
- **Capture:** the host has no `wl_output`, so portals can capture regions and windows
  but not the whole physical screen while it's split. Merge first.
- **Placement and order:** a region's workspaces stay with it, including on another
  physical screen after hotplug. Workspace IDs depend on the order of `hl.virtual_monitor`
  declarations.
- **Open issue:** a hang during the second confirm-timeout cycle of
  `virtualMonitorsRejectsBadLayouts` hasn't been explained yet (see `testing.md`).
