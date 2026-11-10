# Repository audit (November 2026)

What existed before virtual monitors, and what the work could build on.

## Compositor

- Source: `~/Projects/Hyprland`, a clone of upstream. The Nyx change was one commit,
  `gestures: add workspace_swipe_wrap`, kept as one patch per Hyprland version.
- Packaging for Arch and Omarchy: this repository. `update.sh` turns Arch's `hyprland`
  PKGBUILD into `hyprland-nyx` (which replaces and provides `hyprland`) and applies the
  patches. The laptop ran `hyprland-nyx 0.56.2-1`.
- Packaging for NyxOS: `nyx-packages/pkgs/hyprland.nix` overrode nixpkgs' Hyprland
  0.55.4 with a separate copy of the patch. That made two copies of the patch on three
  Hyprland versions.
- Workspace looping: only the touchpad swipe needed a patch. Keyboard and wheel
  looping come from upstream's `m+1` and `m-1`, which already wrap within one monitor.
- Hyprland 0.57.1 needs hyprutils 0.14.3 and aquamarine 0.16.0. Arch's `extra` has both,
  but the laptop's package database hadn't been synced since they were added.

## What upstream already provides

- **Mirroring.** A mirrored output is already "not part of the layout":
  - it is left out of `monitors()`;
  - its `wl_output` global is removed;
  - pointer and hardware-cursor code skip it, and so does workspace placement;
  - it draws its source's saved copy framebuffer.

  Virtual monitors reuse each of these points.
- **Headless outputs.** These are complete monitors (`wl_output`, xdg-output, layer
  shell, screencopy, XWayland RandR), and they can be created at runtime on a DRM
  session.
- **Per-monitor workspaces.** `m~N`, `m±1`, `previous_per_monitor`, persistent
  workspace rules bound to a monitor, and the lifecycle policy, which remembers each
  monitor's active workspace and where its workspaces return, by monitor name.

## Nyx side

- `nyx-core/crates/nyx-desktop`: the Hyprland model every app uses, with stable ids
  (`make|model|serial`) and user workspaces 1 to 10.
- Shell Core (`nyx-shell`): normalized desktop state over its socket. It is a pure core
  that emits effects, and it has no D-Bus service of its own.
- Dock, Bar, desktop widgets and Shell Core look up per-screen settings by stable id
  first, then by connector. Surfaces choose screens by connector name.
- Settings' Displays page writes `hl.monitor` rules into `settings.lua`, then runs
  `hyprctl reload`. Its keep-or-revert timer runs inside the Settings process.
- There was no `nyx display` command. NyxOS's `nyx` is `nyxos/pkgs/nyx-cli/nyx.sh`; the
  Omarchy one is `nyx/bin/nyx`.
- Settings' control interface (`io.github.design_nexus.nyx.Control`) is read-only by
  design.
- Each app pins `nyx-desktop` to a git revision of `nyx-core`.
