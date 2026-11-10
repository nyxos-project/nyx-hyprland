#include "CornerButton.hpp"

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>

using namespace Render;

namespace {
    constexpr uint32_t LEFT_BUTTON = 272;

    // Logical pixels: the button and its distance from the window's corner.
    constexpr double WIDTH  = 30;
    constexpr double HEIGHT = 20;
    constexpr double INSET  = 8;

    enum eCorner : uint8_t {
        CORNER_TOP_RIGHT = 0,
        CORNER_TOP_LEFT,
        CORNER_BOTTOM_RIGHT,
        CORNER_BOTTOM_LEFT,
    };

    // Three dots, as the Dock's hover controls show them.
    SP<ITexture> drawDots(const CHyprColor& c, int h) {
        const int  W       = std::round(h * WIDTH / HEIGHT);
        const auto SURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, h);
        const auto CR      = cairo_create(SURFACE);
        cairo_set_operator(CR, CAIRO_OPERATOR_CLEAR);
        cairo_paint(CR);
        cairo_set_operator(CR, CAIRO_OPERATOR_OVER);
        cairo_set_source_rgba(CR, c.r, c.g, c.b, c.a);
        const double R = h * 0.09;
        for (int i = -1; i <= 1; ++i) {
            cairo_arc(CR, W / 2.0 + i * h * 0.3, h / 2.0, R, 0, 2 * M_PI);
            cairo_fill(CR);
        }
        cairo_surface_flush(SURFACE);
        auto tex = g_pHyprRenderer->createTexture(SURFACE);
        cairo_destroy(CR);
        cairo_surface_destroy(SURFACE);
        return tex;
    }
}

CNyxCornerButton::CNyxCornerButton(PHLWINDOW window) : IHyprWindowDecoration(window), m_window(window) {}

PHLWINDOW CNyxCornerButton::window() const {
    return m_window.lock();
}

bool CNyxCornerButton::visible() const {
    const auto W = m_window.lock();
    if (!W || !W->mapped() || W->isHidden() || !Nyx::settings.enabled->value() || !Nyx::settings.cornerButton->value())
        return false;
    return W->isFloating() && Fullscreen::controller()->getFullscreenModes(W).internal == Fullscreen::FSMODE_NONE;
}

// No room of its own: a zero-size decoration on every edge, so its box is the window's
// and clicks in the window reach it first (it takes only those on the button).
SDecorationPositioningInfo CNyxCornerButton::getPositioningInfo() {
    SDecorationPositioningInfo info;
    info.policy   = DECORATION_POSITION_STICKY;
    info.edges    = DECORATION_EDGE_TOP | DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT;
    info.priority = 10020;
    info.reserved = false;
    return info;
}

void CNyxCornerButton::onPositioningReply(const SDecorationPositioningReply& reply) {
    m_assignedBox = reply.assignedGeometry;
}

eDecorationType CNyxCornerButton::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CNyxCornerButton::updateWindow(PHLWINDOW) {
    damageEntire();
}

CBox CNyxCornerButton::windowBoxGlobal(const SWindowRenderPresentation& presentation) const {
    CBox box = m_assignedBox;
    box.translate(g_pDecorationPositioner->getEdgeDefinedPoint(DECORATION_EDGE_TOP | DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT, m_window));
    box.translate(presentation.workspaceOffset);
    return box;
}

CBox CNyxCornerButton::buttonIn(const CBox& w) const {
    const auto   CORNER = sc<eCorner>(std::clamp<int>(Nyx::settings.corner->value(), 0, 3));
    const bool   RIGHT  = CORNER == CORNER_TOP_RIGHT || CORNER == CORNER_BOTTOM_RIGHT;
    const bool   TOP    = CORNER == CORNER_TOP_RIGHT || CORNER == CORNER_TOP_LEFT;
    const double X      = RIGHT ? w.x + w.w - INSET - WIDTH : w.x + INSET;
    const double Y      = TOP ? w.y + INSET : w.y + w.h - INSET - HEIGHT;
    return CBox{X, Y, WIDTH, HEIGHT};
}

CBox CNyxCornerButton::buttonBoxGlobal() const {
    const auto W = m_window.lock();
    return W ? buttonIn(windowBoxGlobal(W->presentation().renderPresentation())) : CBox{};
}

void CNyxCornerButton::damageEntire() {
    const auto W = m_window.lock();
    if (!W)
        return;
    auto box = buttonBoxGlobal().expand(2);
    box.translate(W->presentation().floatingOffset());
    g_pHyprRenderer->damageBox(box);
}

void CNyxCornerButton::draw(CRenderContext& ctx, PHLMONITOR monitor, float const& a, const SWindowRenderPresentation& presentation) {
    if (!visible() || !monitor)
        return;
    const float SCALE = monitor->m_scale;
    CBox        box   = buttonIn(windowBoxGlobal(presentation));
    box.translate(-monitor->m_position + presentation.floatingOffset);
    box.scale(SCALE).round();
    if (box.empty())
        return;

    // Faint until the pointer reaches it, so it stays out of the way of what's under it.
    const float ALPHA = (m_hovered ? 1.F : 0.55F) * a;
    auto        bg    = CHyprColor(sc<uint64_t>(Nyx::settings.bg->value()));
    bg.a *= ALPHA * (m_hovered ? 1.F : 0.85F);
    CRectPassElement::SRectData rect;
    rect.color                 = bg;
    rect.box                   = box;
    rect.round                 = std::round(box.h / 2);
    rect.workspacePresentation = presentation;
    g_pHyprRenderer->addPassElement(ctx, makeUnique<CRectPassElement>(rect));
    // Under the pointer it takes the theme's hover tint, as the headerbar's buttons do.
    if (m_hovered) {
        auto tint = rect;
        tint.color = CHyprColor(sc<uint64_t>(Nyx::settings.hover->value()));
        tint.color.a *= a;
        g_pHyprRenderer->addPassElement(ctx, makeUnique<CRectPassElement>(tint));
    }

    const uint64_t FG = sc<uint64_t>(Nyx::settings.text->value());
    const int      PX = std::max(6, sc<int>(box.h));
    if (!m_dots.tex || m_dots.color != FG || m_dots.size != PX) {
        m_dots.tex   = drawDots(CHyprColor(FG), PX);
        m_dots.color = FG;
        m_dots.size  = PX;
    }
    if (!m_dots.tex)
        return;
    CTexPassElement::SRenderData data;
    data.tex = m_dots.tex;
    data.box = CBox{box.x + std::round((box.w - m_dots.tex->m_size.x) / 2), box.y, m_dots.tex->m_size.x, m_dots.tex->m_size.y};
    data.a   = ALPHA;
    g_pHyprRenderer->addPassElement(ctx, makeUnique<CTexPassElement>(std::move(data)));
}

void CNyxCornerButton::pointerAt(const Vector2D& coords) {
    const bool HOVERED = visible() && buttonBoxGlobal().containsPoint(coords);
    if (HOVERED == m_hovered)
        return;
    m_hovered = HOVERED;
    damageEntire();
}

bool CNyxCornerButton::onInputOnDeco(const eInputType type, const Vector2D& coords, std::any data) {
    if (type != INPUT_TYPE_BUTTON || !visible())
        return false;
    const auto BUTTON = buttonBoxGlobal();
    if (!BUTTON.containsPoint(coords))
        return false;
    const auto& e = std::any_cast<const IPointer::SButtonEvent&>(data);
    const auto  W = m_window.lock();
    if (e.button == LEFT_BUTTON && e.state == WL_POINTER_BUTTON_STATE_PRESSED && W) {
        if (!Desktop::focusState()->isWindowActive(W))
            Desktop::focusState()->rawWindowFocus(W, Desktop::FOCUS_REASON_CLICK_DOWN);
        Desktop::windowState()->raise(W);
        // The menu opens under the button.
        Nyx::openMenu(W, Vector2D{BUTTON.x, BUTTON.y + BUTTON.h + 4});
    }
    // Every button event on it is the button's, so the app never sees half a click.
    return true;
}

eDecorationLayer CNyxCornerButton::getDecorationLayer() {
    return DECORATION_LAYER_OVER;
}

uint64_t CNyxCornerButton::getDecorationFlags() {
    return DECORATION_ALLOWS_MOUSE_INPUT | DECORATION_NON_SOLID;
}

std::string CNyxCornerButton::getDisplayName() {
    return "Nyx window control";
}
