// nyx-decor: NyxOS's window decorations (adaptive window controls).
//
// Windows tagged nyx-controls-custom-headerbar (third-party apps without usable
// controls; the tags come from [window_controls] in shell.toml through
// nyx-shellctl window-controls --apply) get a Nyx headerbar: the title, then maximize
// (expand when tiled), float or tile, and close. Dialogs and transient windows get
// none. Empty space drags a floating window, a double-click maximizes, and a
// right-click posts nyxdecor>>menu,ADDRESS,X,Y for Nyx Shell's window menu.
//
// It also remembers where each window floated: tiling a window and floating it again
// puts it back there (Hyprland keeps only the size), kept inside its monitor's work
// area. That holds however the mode changes (a key, a Nyx control, a dispatcher).

#include "HeaderBar.hpp"

#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
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

    SP<CNyxHeaderBar> barOf(const PHLWINDOW& w) {
        for (const auto& b : Nyx::bars) {
            if (const auto BAR = b.lock(); BAR && BAR->window() == w)
                return BAR;
        }
        return nullptr;
    }

    // Dialogs, transient and override-redirect windows keep their own look.
    bool decoratable(const PHLWINDOW& w) {
        if (!Desktop::View::validMapped(w))
            return false;
        const auto TRAITS = w->backend().traits();
        return !w->backend().parent() && !TRAITS.transient && !TRAITS.modal && !TRAITS.overrideRedirect;
    }

    // Give `w` a headerbar, or take it away, as its tags and the settings say.
    void sync(const PHLWINDOW& w) {
        if (!w)
            return;
        const bool WANT = Nyx::settings.enabled->value() && decoratable(w) && w->m_ruleApplicator->m_tagKeeper.isTagged(Nyx::HEADERBAR_TAG);
        const auto HAS  = barOf(w);
        if (WANT && !HAS) {
            auto                      bar = makeShared<CNyxHeaderBar>(w);
            SP<IHyprWindowDecoration> deco = bar;
            if (HyprlandAPI::addWindowDecoration(Nyx::handle, w, std::move(deco)))
                Nyx::bars.emplace_back(bar);
        } else if (!WANT && HAS)
            HyprlandAPI::removeWindowDecoration(Nyx::handle, HAS.get());
        std::erase_if(Nyx::bars, [](const auto& b) { return b.expired(); });
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
    }

    void repositionBar(const PHLWINDOW& w) {
        if (const auto BAR = barOf(w)) {
            g_pDecorationPositioner->repositionDeco(BAR.get());
            BAR->damageEntire();
        }
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
        s.height  = add(makeShared<CIntValue>("plugin:nyx_decor:height", "Headerbar height in logical pixels", 32, SIntValueOptions{.min = 24, .max = 48}));
        s.fontSize   = add(makeShared<CIntValue>("plugin:nyx_decor:font_size", "Title size in points", 12, SIntValueOptions{.min = 6, .max = 24}));
        s.fontFamily = add(makeShared<CStringValue>("plugin:nyx_decor:font_family", "Title font (empty: sans-serif)", ""));
        s.doubleClickMaximize  = add(makeShared<CBoolValue>("plugin:nyx_decor:double_click_maximize", "Double-click a headerbar to maximize", true));
        s.restoreFloatGeometry = add(makeShared<CBoolValue>("plugin:nyx_decor:restore_float_geometry", "Float a window where it floated before", true));
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

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    Nyx::handle = handle;
    if (std::string{__hyprland_api_get_hash()} != __hyprland_api_get_client_hash())
        throw std::runtime_error("[nyx-decor] built for another Hyprland version; rebuild it against this one");

    addSettings();

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
    }));
    listeners.emplace_back(ev.render.pre.listen([](PHLMONITOR monitor) { remember(monitor); }));
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
    Nyx::bars.clear();
    floats.clear();
}
