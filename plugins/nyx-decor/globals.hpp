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
class CNyxCornerButton;

namespace Nyx {
    inline HANDLE handle = nullptr;

    // The tags window rules put on windows (nyx-shellctl window-controls --apply writes
    // them from [window_controls] in shell.toml): a headerbar, or the chromeless control.
    inline const std::string HEADERBAR_TAG = "nyx-controls-custom-headerbar";
    inline const std::string CHROMELESS_TAG = "nyx-controls-chromeless-floating";
    inline const std::string NATIVE_TAG     = "nyx-controls-native-nyx";
    inline const std::string PRESERVE_TAG   = "nyx-controls-preserve-native";

    // plugin:nyx_decor:* (Lua: hl.config({ plugin = { nyx_decor = { … } } })).
    struct SSettings {
        SP<Config::Values::CBoolValue>   enabled;
        // A headerbar on untagged windows that left their decorations to the compositor.
        SP<Config::Values::CBoolValue>   serverSide;
        SP<Config::Values::CIntValue>    height;
        SP<Config::Values::CIntValue>    fontSize;
        SP<Config::Values::CStringValue> fontFamily;
        SP<Config::Values::CBoolValue>   doubleClickMaximize;
        SP<Config::Values::CBoolValue>   restoreFloatGeometry;
        // The chromeless control, and its corner: top-right, top-left, bottom-right,
        // bottom-left.
        SP<Config::Values::CBoolValue> cornerButton;
        SP<Config::Values::CIntValue>  corner;
        // Nyx Theme Engine's colours (nyx-themed writes them with the theme).
        SP<Config::Values::CColorValue> bg, bgInactive, text, textInactive, hover, closeHover, closeHoverText;
    };
    inline SSettings settings;

    inline std::vector<WP<CNyxHeaderBar>>    bars;
    inline std::vector<WP<CNyxCornerButton>> buttons;

    // Run a dispatcher the way Shell Core and the Nyx apps do, so every presentation
    // of a window action is the same action.
    void dispatch(const std::string& lua);

    // Ask Nyx Shell for `w`'s window menu at `at` (global logical coordinates): Hyprland's
    // custom event `custom>>nyx-window-menu,0xADDRESS,X,Y`, as hl.dsp.event posts it.
    void openMenu(const PHLWINDOW& w, const Vector2D& at);

    // `address:0x…` for a dispatcher's window argument.
    std::string selector(const PHLWINDOW& w);
}
