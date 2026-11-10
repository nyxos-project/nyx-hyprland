# Further compositor enhancements: assessment

None of these is implemented. For each: what Hyprland 0.57.1 already does, what would
change where, the risks, and a recommendation. Priority 1 means it's worth doing next.

## Agent workspaces

**Upstream:** special workspaces, `no_initial_focus` and `suppress_event` window rules,
`misc:focus_on_activate`, and ext-image-copy-capture with workspace capture sources
(`WorkspaceCaptureSource`).

**Compositor:** a workspace flag that refuses focus and activation from its windows
unless the user acts. This would be a small hook in the focus code.

**Nyx:** Shell Core holds the lifecycle and policy. Sandboxing belongs to Nyx services:
a hidden workspace is not a security boundary.

**Risk:** medium. Focus code changes upstream often.

**Recommendation:** priority 2, once the agent workspace design exists.

## Persistent workspace layouts

**Upstream:** IPC for window geometry, workspace rules, and window rules by class.

**Compositor:** nothing new; `nyx-desktop`'s `profiles` already places apps by
workspace.

**Nyx:** a session layout manager on top of `profiles`, adding virtual monitor
assignments through the `region` and `physical` fields.

**Risk:** low.

**Recommendation:** priority 1, all in Nyx.

## Advanced window grouping

**Upstream:** groups with tabs (the groupbar), moving them between workspaces and
monitors, and locking.

**Compositor:** side-by-side groups and synchronized resizing would be layout-algorithm
work, with high maintenance cost.

**Nyx:** none.

**Recommendation:** priority 4. Use upstream groups.

## Focus protection

**Upstream:** `misc:focus_on_activate`, xdg-activation tokens (urgent-only without a
serial), and the `focus_on_activate`, `no_initial_focus`, `suppress_event` and
`stay_focused` window rules.

**Compositor:** at most an option to protect a fullscreen window from activation by
background windows.

**Nyx:** default rules for background apps.

**Risk:** low.

**Recommendation:** priority 2. Configure first and measure what's missing.

## Smart display hotplug

**Upstream:** the workspace lifecycle policy (return monitors, remembered active
workspaces) and monitor rules by `desc:`.

**Compositor:** virtual monitors already rebuild with stable names.

**Nyx:** saved display profiles in Settings (it already remembers displays), and window
relocation through `profiles`.

**Risk:** low.

**Recommendation:** priority 2, Nyx only.

## Per-workspace wallpapers

**Upstream:** per-output wallpaper clients (layer surfaces) and socket2 `workspacev2`
events.

**Compositor:** none. A region is an output, so per-region wallpapers already work.

**Nyx:** the wallpaper service swaps the image on workspace events.

**Risk:** low.

**Recommendation:** priority 3, no Hyprland change.

## Workspace overview

**Upstream:** `j/clients`, `j/workspaces`, ext-image-copy-capture with toplevel and
workspace sources, and `hyprland-toplevel-export`.

**Compositor:** probably none. Measure capture cost for many thumbnails first.

**Nyx:** the UI in Quickshell, with `nyx-desktop`'s capture module for thumbnails.

**Risk:** low in the compositor.

**Recommendation:** priority 2, as a Quickshell component.

## Window parking

**Upstream:** special workspaces (Omarchy's `special:minimized`) and pinning.

**Compositor:** none for park and restore.

**Nyx:** remember the original workspace and geometry in Shell Core, which already
tracks minimized windows.

**Risk:** low.

**Recommendation:** priority 3, Nyx only.

## Secure screen sharing

**Upstream:**
- xdg-desktop-portal-hyprland shares outputs, windows and regions.
- `no_screen_share` window rules exist.
- Workspace capture sources exist.

**Compositor:** regions are shareable already. Sharing the whole physical screen while
it's split would need a capture source for the host. Exclusion is by rule, not a
security guarantee.

**Nyx:** a share picker that lists regions and the screen.

**Risk:** medium (protocol surface).

**Recommendation:** priority 2: a host capture source, after the hardware tests.

## Performance

**Upstream:** damage tracking, `debug:damage_blink`, the debug overlay and Tracy.

**Compositor:**
- Measure the virtual monitor copy and the latency added by unsynchronized region
  frames first.
- If they matter, render regions from the host's frame callback.
- Draw the hardware cursor on the host.

**Risk:** high: render-loop changes.

**Recommendation:** priority 1 to measure, then decide. No speculative code.
