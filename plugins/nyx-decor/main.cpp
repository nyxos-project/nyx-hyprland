// nyx-decor: NyxOS's window decorations (adaptive window controls).
//
// Windows tagged nyx-controls-custom-headerbar (third-party apps without usable
// controls; the tags come from [window_controls] in shell.toml through
// nyx-shellctl window-controls --apply), and untagged Wayland windows that left their
// decorations to the compositor (xdg-decoration: Qt, mpv, foot and the like; off with
// plugin:nyx_decor:server_side_headerbar), get a Nyx headerbar: the title, then maximize
// (expand when tiled), float or tile, and close. Dialogs and transient windows get
// none. Empty space drags a floating window, a double-click maximizes, and a
// right-click opens Nyx Shell's window menu (custom>>nyx-window-menu,ADDRESS,X,Y).
//
// Windows tagged nyx-controls-chromeless-floating (terminals) get no bar: while they
// float, a small button in a corner opens the same menu.
//
// It also remembers where each window floated: tiling a window and floating it again
// puts it back there (Hyprland keeps only the size), kept inside its monitor's work
// area. That holds however the mode changes (a key, a Nyx control, a dispatcher).

#include "AppIcon.hpp"
#include "CornerButton.hpp"
#include "HeaderBar.hpp"

#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/ipc/s2/S2.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/XDGDecoration.hpp>
#include <hyprland/src/protocols/XDGShell.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <format>

using namespace Config::Values;

namespace {
    std::vector<CHyprSignalListener> listeners;
    std::deque<uint64_t>             pending;

    // Run `fn` once Hyprland is idle: dispatchers and layout changes never run in the
    // middle of its own input or layout handling.
    void later(std::function<void()> fn) {
        if (!g_pEventLoopManager)
            return;
        pending.push_back(g_pEventLoopManager->doLater(std::move(fn)));
        while (pending.size() > 64)
            pending.pop_front();
    }

    template <typename T>
    SP<T> decoOf(const std::vector<WP<T>>& list, const PHLWINDOW& w) {
        for (const auto& d : list) {
            if (const auto DECO = d.lock(); DECO && DECO->window() == w)
                return DECO;
        }
        return nullptr;
    }

    SP<CNyxHeaderBar> barOf(const PHLWINDOW& w) {
        return decoOf(Nyx::bars, w);
    }

    SP<CNyxCornerButton> buttonOf(const PHLWINDOW& w) {
        return decoOf(Nyx::buttons, w);
    }

    // Add or remove one decoration of `w` as `want` says.
    template <typename T>
    void keep(std::vector<WP<T>>& list, const PHLWINDOW& w, bool want) {
        const auto HAS = decoOf(list, w);
        if (want && !HAS) {
            auto                      deco = makeShared<T>(w);
            SP<IHyprWindowDecoration> base = deco;
            if (HyprlandAPI::addWindowDecoration(Nyx::handle, w, std::move(base)))
                list.emplace_back(deco);
        } else if (!want && HAS)
            HyprlandAPI::removeWindowDecoration(Nyx::handle, HAS.get());
        std::erase_if(list, [](const auto& d) { return d.expired(); });
    }

    // Dialogs, transient and override-redirect windows keep their own look.
    bool decoratable(const PHLWINDOW& w) {
        if (!Desktop::View::validMapped(w))
            return false;
        const auto TRAITS = w->backend().traits();
        return !w->backend().parent() && !TRAITS.transient && !TRAITS.modal && !TRAITS.overrideRedirect;
    }

    // Hyprland keeps its xdg-decoration objects (xdg_toplevel -> decoration) private;
    // an explicit instantiation may name a private member.
    using DecorationMap = std::unordered_map<wl_resource*, UP<CXDGDecoration>>;
    DecorationMap CXDGDecorationProtocol::*decorationsMember();
    template <DecorationMap CXDGDecorationProtocol::*M>
    struct SDecorationsMember {
        friend DecorationMap CXDGDecorationProtocol::*decorationsMember() {
            return M;
        }
    };
    template struct SDecorationsMember<&CXDGDecorationProtocol::m_decorations>;

    // Whether `w` left its decorations to the compositor: a Wayland client with an
    // xdg-decoration object, which Hyprland always answers with server-side, so the
    // client draws no title bar or buttons.
    bool serverSide(const PHLWINDOW& w) {
        if (!PROTO::xdgDecoration || w->backend().isX11())
            return false;
        const auto SURFACE = w->backend().surface();
        for (const auto& [toplevel, deco] : (*PROTO::xdgDecoration).*decorationsMember()) {
            const auto TOP = CXDGToplevelResource::fromResource(toplevel);
            if (TOP && TOP->m_owner && TOP->m_owner->m_surface.lock() == SURFACE)
                return true;
        }
        return false;
    }

    // Give `w` a headerbar or the chromeless control, or take it away, as its tags and
    // the settings say. The control stays on the window and shows only while it floats.
    // A window no rule tags gets a headerbar when it left its decorations to the
    // compositor.
    void sync(const PHLWINDOW& w) {
        if (!w)
            return;
        const bool  OK         = Nyx::settings.enabled->value() && decoratable(w);
        const auto& TAGS       = w->m_ruleApplicator->m_tagKeeper;
        const bool  CHROMELESS = TAGS.isTagged(Nyx::CHROMELESS_TAG);
        const bool  RULED      = CHROMELESS || TAGS.isTagged(Nyx::NATIVE_TAG) || TAGS.isTagged(Nyx::PRESERVE_TAG);
        const bool  BAR        = TAGS.isTagged(Nyx::HEADERBAR_TAG) || (!RULED && Nyx::settings.serverSide->value() && serverSide(w));
        keep(Nyx::bars, w, OK && BAR);
        keep(Nyx::buttons, w, OK && !BAR && CHROMELESS);
    }

    void syncAll() {
        for (const auto& w : Desktop::viewState()->windows())
            sync(w);
        for (const auto& b : Nyx::bars) {
            if (const auto BAR = b.lock()) {
                g_pDecorationPositioner->repositionDeco(BAR.get());
                BAR->damageEntire();
            }
        }
        for (const auto& b : Nyx::buttons) {
            if (const auto BUTTON = b.lock())
                BUTTON->damageEntire();
        }
    }

    // The window's mode changed: the bar's room and the control's visibility follow.
    void repositionBar(const PHLWINDOW& w) {
        if (const auto BAR = barOf(w)) {
            g_pDecorationPositioner->repositionDeco(BAR.get());
            BAR->damageEntire();
        }
        if (const auto BUTTON = buttonOf(w))
            BUTTON->damageEntire();
    }

    // ---------- Floating geometry ----------

    struct SFloat {
        PHLWINDOWREF                          window;
        CBox                                  box;
        bool                                  floating = false;
        std::chrono::steady_clock::time_point since;
    };
    std::vector<SFloat> floats;

    // A window's placement is read only after it has floated this long, so the place
    // Hyprland picks first (before the one remembered is put back) isn't remembered.
    constexpr auto SETTLE = std::chrono::milliseconds(500);

    SFloat& floatOf(const PHLWINDOW& w) {
        for (auto& f : floats) {
            if (f.window.lock() == w)
                return f;
        }
        return floats.emplace_back(SFloat{.window = w, .box = {}, .floating = false, .since = {}});
    }

    bool plainFloating(const PHLWINDOW& w) {
        return w->isFloating() && Fullscreen::controller()->getFullscreenModes(w).internal == Fullscreen::FSMODE_NONE;
    }

    void remember(const PHLMONITOR& monitor) {
        std::erase_if(floats, [](const auto& f) { return f.window.expired(); });
        const auto NOW = std::chrono::steady_clock::now();
        for (const auto& w : Desktop::viewState()->windows()) {
            if (!Desktop::View::validMapped(w) || w->isHidden() || w->m_monitor.lock() != monitor)
                continue;
            const bool FLOATING = plainFloating(w);
            auto&      f        = floatOf(w);
            if (FLOATING && !f.floating)
                f.since = NOW;
            f.floating = FLOATING;
            if (FLOATING && NOW - f.since > SETTLE && w->layoutTarget())
                f.box = w->layoutTarget()->position();
        }
    }

    // `box` inside the work area: no bigger than it, and moved in when it would be
    // outside (a monitor that went away, or a smaller one).
    CBox clampToWorkArea(CBox box, const PHLMONITOR& monitor) {
        const CBox AREA = monitor->logicalBoxMinusReserved();
        box.w           = std::min(box.w, AREA.w);
        box.h           = std::min(box.h, AREA.h);
        if (!AREA.containsPoint(box.middle()))
            return CBox{AREA.middle() - box.size() / 2, box.size()};
        box.x = std::clamp(box.x, AREA.x, AREA.x + AREA.w - box.w);
        box.y = std::clamp(box.y, AREA.y, AREA.y + AREA.h - box.h);
        return box;
    }

    void onFloatingChanged(const PHLWINDOW& w) {
        repositionBar(w);
        if (!w || !w->isFloating() || !Nyx::settings.restoreFloatGeometry->value())
            return;
        auto& f    = floatOf(w);
        f.floating = true;
        f.since    = std::chrono::steady_clock::now();
        if (f.box.w < 5 || f.box.h < 5)
            return;
        const auto BOX = f.box;
        later([ref = PHLWINDOWREF{w}, BOX] {
            const auto W = ref.lock();
            if (!W || !plainFloating(W) || !W->layoutTarget())
                return;
            const auto MONITOR = W->m_monitor.lock();
            if (!MONITOR)
                return;
            g_layoutManager->setTargetGeom(clampToWorkArea(BOX, MONITOR), W->layoutTarget());
        });
    }

    void addSettings() {
        auto& s = Nyx::settings;
        auto  add = [](auto v) {
            HyprlandAPI::addConfigValueV2(Nyx::handle, v);
            return v;
        };
        s.enabled = add(makeShared<CBoolValue>("plugin:nyx_decor:enabled", "Nyx headerbars on windows tagged nyx-controls-custom-headerbar", true));
        s.serverSide = add(makeShared<CBoolValue>("plugin:nyx_decor:server_side_headerbar", "A headerbar on apps that leave their decorations to the compositor", true));
        s.height  = add(makeShared<CIntValue>("plugin:nyx_decor:height", "Headerbar height in logical pixels", 32, SIntValueOptions{.min = 24, .max = 48}));
        s.fontSize   = add(makeShared<CIntValue>("plugin:nyx_decor:font_size", "Title size in points", 12, SIntValueOptions{.min = 6, .max = 24}));
        s.fontFamily = add(makeShared<CStringValue>("plugin:nyx_decor:font_family", "Title font (empty: sans-serif)", ""));
        s.doubleClickMaximize  = add(makeShared<CBoolValue>("plugin:nyx_decor:double_click_maximize", "Double-click a headerbar to maximize", true));
        s.restoreFloatGeometry = add(makeShared<CBoolValue>("plugin:nyx_decor:restore_float_geometry", "Float a window where it floated before", true));
        s.cornerButton = add(makeShared<CBoolValue>("plugin:nyx_decor:corner_button", "The window control on floating chromeless windows", true));
        s.corner       = add(makeShared<CIntValue>("plugin:nyx_decor:corner_button_corner", "Where the control sits", 0,
                                                   SIntValueOptions{.min = 0, .max = 3, .map = std::unordered_map<std::string, Config::INTEGER>{
                                                                                                {"top-right", 0}, {"top-left", 1}, {"bottom-right", 2}, {"bottom-left", 3}}}));
        s.bg           = add(makeShared<CColorValue>("plugin:nyx_decor:col.bg", "Headerbar of the focused window", 0xff181825));
        s.bgInactive   = add(makeShared<CColorValue>("plugin:nyx_decor:col.bg_inactive", "Headerbar of other windows", 0xff1e1e2e));
        s.text         = add(makeShared<CColorValue>("plugin:nyx_decor:col.text", "Title and buttons", 0xffcdd6f4));
        s.textInactive = add(makeShared<CColorValue>("plugin:nyx_decor:col.text_inactive", "Title and buttons of other windows", 0xff7f849c));
        s.hover        = add(makeShared<CColorValue>("plugin:nyx_decor:col.hover", "A button under the pointer", 0x1fcdd6f4));
        s.closeHover   = add(makeShared<CColorValue>("plugin:nyx_decor:col.close_hover", "Close under the pointer", 0xfff38ba8));
        s.closeHoverText = add(makeShared<CColorValue>("plugin:nyx_decor:col.close_hover_text", "Close's mark under the pointer", 0xff1e1e2e));
    }
}

void Nyx::dispatch(const std::string& lua) {
    later([lua] { HyprlandAPI::invokeHyprctlCommand("dispatch", lua); });
}

std::string Nyx::selector(const PHLWINDOW& w) {
    return std::format("address:0x{:x}", rc<uintptr_t>(w.get()));
}

void Nyx::openMenu(const PHLWINDOW& w, const Vector2D& at) {
    if (!w || !IPC::Socket2::sock())
        return;
    IPC::Socket2::sock()->postEvent({.event = "custom", .data = std::format("nyx-window-menu,0x{:x},{},{}", rc<uintptr_t>(w.get()), sc<int>(at.x), sc<int>(at.y))});
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    Nyx::handle = handle;
    if (std::string{__hyprland_api_get_hash()} != __hyprland_api_get_client_hash())
        throw std::runtime_error("[nyx-decor] built for another Hyprland version; rebuild it against this one");

    addSettings();
    Nyx::Icons::start();

    auto& ev = Event::bus()->m_events;
    listeners.emplace_back(ev.window.open.listen([](PHLWINDOW w) { sync(w); }));
    listeners.emplace_back(ev.window.updateRules.listen([](PHLWINDOW w) { sync(w); }));
    listeners.emplace_back(ev.window.floating.listen([](PHLWINDOW w) { onFloatingChanged(w); }));
    listeners.emplace_back(ev.window.fullscreen.listen([](PHLWINDOW w) { repositionBar(w); }));
    listeners.emplace_back(ev.window.title.listen([](PHLWINDOW w) {
        if (const auto BAR = barOf(w))
            BAR->damageEntire();
    }));
    // Focus changes restyle every bar (focused and the one that lost it).
    listeners.emplace_back(ev.window.active.listen([](PHLWINDOW, Desktop::eFocusReason) {
        for (const auto& b : Nyx::bars) {
            if (const auto BAR = b.lock())
                BAR->damageEntire();
        }
    }));
    listeners.emplace_back(ev.input.mouse.move.listen([](Vector2D pos, Event::SCallbackInfo&) {
        for (const auto& b : Nyx::bars) {
            if (const auto BAR = b.lock())
                BAR->pointerAt(pos);
        }
        for (const auto& b : Nyx::buttons) {
            if (const auto BUTTON = b.lock())
                BUTTON->pointerAt(pos);
        }
    }));
    listeners.emplace_back(ev.render.pre.listen([](PHLMONITOR monitor) {
        remember(monitor);
        // The app icons are known now: the bars draw them.
        if (Nyx::Icons::justReady()) {
            for (const auto& b : Nyx::bars) {
                if (const auto BAR = b.lock())
                    BAR->damageEntire();
            }
        }
    }));
    listeners.emplace_back(ev.config.reloaded.listen([] { later(syncAll); }));

    later(syncAll);
    return {"nyx-decor", "NyxOS window decorations: headerbars for apps without controls, floating geometry", "Design Nexus", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    listeners.clear();
    if (g_pEventLoopManager) {
        for (const auto SEQ : pending)
            g_pEventLoopManager->removeDoLater(SEQ);
    }
    pending.clear();
    for (const auto& b : Nyx::bars) {
        if (const auto BAR = b.lock())
            HyprlandAPI::removeWindowDecoration(Nyx::handle, BAR.get());
    }
    for (const auto& b : Nyx::buttons) {
        if (const auto BUTTON = b.lock())
            HyprlandAPI::removeWindowDecoration(Nyx::handle, BUTTON.get());
    }
    Nyx::bars.clear();
    Nyx::buttons.clear();
    floats.clear();
    Nyx::Icons::stop();
}
