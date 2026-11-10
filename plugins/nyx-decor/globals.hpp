// nyx-decor's shared state: the plugin handle, its settings and the live headerbars.

#pragma once

#define WLR_USE_UNSTABLE

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>

#include <string>
#include <vector>

class CNyxHeaderBar;

namespace Nyx {
    inline HANDLE handle = nullptr;

    // The tag window rules put on windows that get a headerbar (nyx-shellctl
    // window-controls --apply writes them from [window_controls] in shell.toml).
    inline const std::string HEADERBAR_TAG = "nyx-controls-custom-headerbar";

    // plugin:nyx_decor:* (Lua: hl.config({ plugin = { nyx_decor = { … } } })).
    struct SSettings {
        SP<Config::Values::CBoolValue>   enabled;
        SP<Config::Values::CIntValue>    height;
        SP<Config::Values::CIntValue>    fontSize;
        SP<Config::Values::CStringValue> fontFamily;
        SP<Config::Values::CBoolValue>   doubleClickMaximize;
        SP<Config::Values::CBoolValue>   restoreFloatGeometry;
        // Nyx Theme Engine's colours (nyx-themed writes them with the theme).
        SP<Config::Values::CColorValue> bg, bgInactive, text, textInactive, hover, closeHover, closeHoverText;
    };
    inline SSettings settings;

    inline std::vector<WP<CNyxHeaderBar>> bars;

    // Run a dispatcher the way Shell Core and the Nyx apps do, so every presentation
    // of a window action is the same action.
    void dispatch(const std::string& lua);

    // `address:0x…` for a dispatcher's window argument.
    std::string selector(const PHLWINDOW& w);
}
