// App icons for headerbars. Hyprland 0.57 has no toplevel-icon protocol, so a window's
// icon is its app's: the desktop entry whose id, StartupWMClass or last name part is the
// window's class, its Icon=, found in the hicolor theme or pixmaps (SVG first).
//
// The desktop entries are read once, on a thread of their own, so no window waits on the
// disk; until they are, bars show no icon.

#pragma once

#include <string>

namespace Nyx::Icons {
    // Start reading the desktop entries.
    void start();
    // Whether they've been read since this was last asked (bars then draw again).
    bool justReady();
    // The icon file for a window's class or initial class, or "" (not known or not yet).
    std::string pathFor(const std::string& appId, const std::string& initialAppId);
    // Stop using the reader's results (plugin unload).
    void stop();
}
