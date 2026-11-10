#include "HeaderBar.hpp"
#include "AppIcon.hpp"

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

#include <cairo/cairo.h>
#include <hyprgraphics/image/Image.hpp>

#include <algorithm>
#include <cmath>
#include <format>

using namespace Render;

namespace {
    constexpr uint32_t LEFT_BUTTON  = 272;
    constexpr uint32_t RIGHT_BUTTON = 273;
    constexpr auto     DOUBLE_CLICK = std::chrono::milliseconds(400);

    // Logical pixels: the gap at the bar's ends, between buttons, and before the title.
    constexpr double PAD        = 6;
    constexpr double BUTTON_GAP = 2;
    constexpr double TITLE_PAD  = 12;

    enum eShape : uint8_t {
        SHAPE_CLOSE = 0,
        SHAPE_MAXIMIZE,
        SHAPE_RESTORE,
        SHAPE_FLOAT,
        SHAPE_TILE,
    };

    CHyprColor color(const SP<Config::Values::CColorValue>& v, float a) {
        auto c = CHyprColor(sc<uint64_t>(v->value()));
        c.a *= a;
        return c;
    }

    // A button's glyph in `c`, `px` square, as the window controls' icons look: thin
    // strokes, centred.
    SP<ITexture> drawGlyph(int shape, const CHyprColor& c, int px) {
        const auto SURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
        const auto CR      = cairo_create(SURFACE);
        cairo_set_operator(CR, CAIRO_OPERATOR_CLEAR);
        cairo_paint(CR);
        cairo_set_operator(CR, CAIRO_OPERATOR_OVER);
        cairo_set_source_rgba(CR, c.r, c.g, c.b, c.a);
        const double LINE = std::max(1.0, px / 10.0);
        cairo_set_line_width(CR, LINE);
        cairo_set_line_cap(CR, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(CR, CAIRO_LINE_JOIN_ROUND);
        const double M = px * 0.22, S = px - 2 * M;
        switch (shape) {
            case SHAPE_CLOSE:
                cairo_move_to(CR, M, M);
                cairo_line_to(CR, px - M, px - M);
                cairo_move_to(CR, px - M, M);
                cairo_line_to(CR, M, px - M);
                cairo_stroke(CR);
                break;
            case SHAPE_MAXIMIZE: cairo_rectangle(CR, M, M, S, S); cairo_stroke(CR); break;
            case SHAPE_RESTORE: {
                const double O = S * 0.28;
                cairo_rectangle(CR, M, M + O, S - O, S - O);
                cairo_stroke(CR);
                cairo_move_to(CR, M + O, M + O);
                cairo_line_to(CR, M + O, M);
                cairo_line_to(CR, M + S, M);
                cairo_line_to(CR, M + S, M + S - O);
                cairo_line_to(CR, M + S - O, M + S - O);
                cairo_stroke(CR);
                break;
            }
            case SHAPE_FLOAT: {
                // A window with a smaller one floating over its corner.
                cairo_rectangle(CR, M, M, S, S * 0.8);
                cairo_stroke(CR);
                cairo_rectangle(CR, M + S * 0.45, M + S * 0.4, S * 0.55, S * 0.6);
                cairo_fill(CR);
                break;
            }
            case SHAPE_TILE: {
                const double H = (S - LINE) / 2;
                cairo_rectangle(CR, M, M, H, H);
                cairo_rectangle(CR, M + S - H, M, H, H);
                cairo_rectangle(CR, M, M + S - H, H, H);
                cairo_rectangle(CR, M + S - H, M + S - H, H, H);
                cairo_stroke(CR);
                break;
            }
            default: break;
        }
        cairo_surface_flush(SURFACE);
        auto tex = g_pHyprRenderer->createTexture(SURFACE);
        cairo_destroy(CR);
        cairo_surface_destroy(SURFACE);
        return tex;
    }
}

CNyxHeaderBar::CNyxHeaderBar(PHLWINDOW window) : IHyprWindowDecoration(window), m_window(window) {}

PHLWINDOW CNyxHeaderBar::window() const {
    return m_window.lock();
}

int CNyxHeaderBar::barHeight() const {
    return std::clamp<int>(Nyx::settings.height->value(), 24, 48);
}

bool CNyxHeaderBar::visible() const {
    const auto W = m_window.lock();
    if (!W || !W->mapped() || !Nyx::settings.enabled->value())
        return false;
    if (Fullscreen::controller()->getFullscreenModes(W).internal == Fullscreen::FSMODE_FULLSCREEN)
        return false;
    return W->m_ruleApplicator->decorate().valueOrDefault();
}

bool CNyxHeaderBar::focused() const {
    return Desktop::focusState()->window() == m_window.lock();
}

SDecorationPositioningInfo CNyxHeaderBar::getPositioningInfo() {
    SDecorationPositioningInfo info;
    info.policy = DECORATION_POSITION_STICKY;
    info.edges  = DECORATION_EDGE_TOP;
    // Nearest the window: inside its border (10000), so the border goes around the bar
    // and the window as one, and inside a group's tab bar.
    info.priority       = 10010;
    info.reserved       = true;
    info.desiredExtents.topLeft = Vector2D(0.0, visible() ? sc<double>(barHeight()) : 0.0);
    return info;
}

void CNyxHeaderBar::onPositioningReply(const SDecorationPositioningReply& reply) {
    m_assignedBox = reply.assignedGeometry;
}

eDecorationType CNyxHeaderBar::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CNyxHeaderBar::updateWindow(PHLWINDOW) {
    damageEntire();
}

CBox CNyxHeaderBar::barBoxGlobal() const {
    const auto W = m_window.lock();
    return W ? barBoxGlobal(W->presentation().renderPresentation()) : CBox{};
}

CBox CNyxHeaderBar::barBoxGlobal(const SWindowRenderPresentation& presentation) const {
    CBox box = m_assignedBox;
    box.translate(g_pDecorationPositioner->getEdgeDefinedPoint(DECORATION_EDGE_TOP, m_window));
    box.translate(presentation.workspaceOffset);
    return box;
}

void CNyxHeaderBar::damageEntire() {
    const auto W = m_window.lock();
    if (!W)
        return;
    auto box = barBoxGlobal();
    box.translate(W->presentation().floatingOffset());
    g_pHyprRenderer->damageBox(box);
}

// Right to left: close, float or tile, maximize or expand.
CBox CNyxHeaderBar::buttonBox(const CBox& bar, eButton button) const {
    const double SIZE  = bar.h - 8;
    const int    INDEX = button == BUTTON_CLOSE ? 0 : button == BUTTON_FLOAT ? 1 : 2;
    return CBox{bar.x + bar.w - PAD - (INDEX + 1) * SIZE - INDEX * BUTTON_GAP, bar.y + (bar.h - SIZE) / 2, SIZE, SIZE};
}

CNyxHeaderBar::eButton CNyxHeaderBar::buttonAt(const Vector2D& coords) const {
    const auto BAR = barBoxGlobal();
    for (uint8_t b = 0; b < BUTTON_COUNT; ++b) {
        if (buttonBox(BAR, sc<eButton>(b)).containsPoint(coords))
            return sc<eButton>(b);
    }
    return BUTTON_NONE;
}

int CNyxHeaderBar::glyphShape(eButton button) const {
    const auto W = m_window.lock();
    switch (button) {
        case BUTTON_CLOSE: return SHAPE_CLOSE;
        case BUTTON_FLOAT: return W && W->isFloating() ? SHAPE_TILE : SHAPE_FLOAT;
        case BUTTON_MAXIMIZE:
            return W && Fullscreen::controller()->getFullscreenModes(W).internal == Fullscreen::FSMODE_MAXIMIZED ? SHAPE_RESTORE : SHAPE_MAXIMIZE;
        default: return -1;
    }
}

SP<ITexture> CNyxHeaderBar::glyph(eButton button, uint64_t c, int px) {
    auto&     g     = m_glyphs[button];
    const int SHAPE = glyphShape(button);
    if (!g.tex || g.color != c || g.size != px || g.shape != SHAPE) {
        g.tex   = drawGlyph(SHAPE, CHyprColor(c), px);
        g.color = c;
        g.size  = px;
        g.shape = SHAPE;
    }
    return g.tex;
}

SP<ITexture> CNyxHeaderBar::icon(int px) {
    const auto W = m_window.lock();
    if (!W)
        return nullptr;
    const auto PATH = Nyx::Icons::pathFor(W->metadata().appID(), W->metadata().initialAppID());
    if (PATH.empty())
        return nullptr;
    if (m_icon.path != PATH || m_icon.size != px) {
        m_icon.path = PATH;
        m_icon.size = px;
        m_icon.tex  = nullptr;
        Hyprgraphics::CImage image(PATH, Vector2D{sc<double>(px), sc<double>(px)});
        if (image.success() && image.cairoSurface())
            m_icon.tex = g_pHyprRenderer->createTexture(image.cairoSurface()->cairo());
        // One more frame, as the one it was made in may be under way already.
        damageEntire();
    }
    return m_icon.tex;
}

SP<ITexture> CNyxHeaderBar::title(uint64_t c, int maxWidth, float scale) {
    const auto W = m_window.lock();
    if (!W || maxWidth <= 0)
        return nullptr;
    const auto& TEXT = W->metadata().title();
    if (!m_title.tex || m_title.text != TEXT || m_title.color != c || m_title.maxWidth != maxWidth || m_title.scale != scale) {
        const std::string FAMILY = Nyx::settings.fontFamily->value();
        m_title.tex      = TEXT.empty() ? nullptr :
                                          g_pHyprRenderer->renderText(TEXT, CHyprColor(c), std::round(Nyx::settings.fontSize->value() * scale), false,
                                                                      FAMILY == "" ? std::string("sans-serif") : FAMILY, maxWidth);
        m_title.text     = TEXT;
        m_title.color    = c;
        m_title.maxWidth = maxWidth;
        m_title.scale    = scale;
    }
    return m_title.tex;
}

void CNyxHeaderBar::draw(CRenderContext& ctx, PHLMONITOR monitor, float const& a, const SWindowRenderPresentation& presentation) {
    if (!visible())
        return;
    const auto W = m_window.lock();
    if (!W || !monitor)
        return;

    const float SCALE  = monitor->m_scale;
    const bool  ACTIVE = focused();
    const auto  BAR    = barBoxGlobal(presentation);
    auto        toLocal = [&](CBox b) {
        b.translate(-monitor->m_position + presentation.floatingOffset);
        b.scale(SCALE).round();
        return b;
    };
    const CBox BOX = toLocal(BAR);
    if (BOX.empty())
        return;

    // The bar, its top corners rounded like the window's; the bottom ones are hidden
    // under the window's top edge.
    const int                   ROUND = std::round(W->presentation().rounding() * SCALE);
    CRectPassElement::SRectData bg;
    bg.color                 = color(ACTIVE ? Nyx::settings.bg : Nyx::settings.bgInactive, a);
    bg.box                   = CBox{BOX.x, BOX.y, BOX.w, BOX.h + ROUND};
    bg.clipBox               = BOX;
    bg.round                 = ROUND;
    bg.roundingPower         = W->presentation().roundingPower();
    bg.workspacePresentation = presentation;
    g_pHyprRenderer->addPassElement(ctx, makeUnique<CRectPassElement>(bg));

    for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
        const auto B      = sc<eButton>(i);
        const CBox BUTTON = toLocal(buttonBox(BAR, B));
        const bool HOT    = m_hovered == B;
        if (HOT) {
            CRectPassElement::SRectData hl;
            hl.color                 = color(B == BUTTON_CLOSE ? Nyx::settings.closeHover : Nyx::settings.hover, a);
            hl.box                   = BUTTON;
            hl.round                 = std::round(BUTTON.h / 4);
            hl.workspacePresentation = presentation;
            g_pHyprRenderer->addPassElement(ctx, makeUnique<CRectPassElement>(hl));
        }
        const auto& FG = HOT && B == BUTTON_CLOSE ? Nyx::settings.closeHoverText : ACTIVE || HOT ? Nyx::settings.text : Nyx::settings.textInactive;
        const int   PX = std::max(8, sc<int>(std::round(BUTTON.h * 0.62)));
        const auto  TEX = glyph(B, sc<uint64_t>(FG->value()), PX);
        if (!TEX)
            continue;
        CTexPassElement::SRenderData data;
        data.tex = TEX;
        data.box = CBox{BUTTON.x + std::round((BUTTON.w - PX) / 2), BUTTON.y + std::round((BUTTON.h - PX) / 2), sc<double>(PX), sc<double>(PX)};
        data.a   = a;
        g_pHyprRenderer->addPassElement(ctx, makeUnique<CTexPassElement>(std::move(data)));
    }

    // The app's icon, when one is known, then the title.
    double     left     = BOX.x + std::round(TITLE_PAD * SCALE);
    const int  ICON_PX  = sc<int>(std::round(BOX.h * 0.56));
    if (const auto ICON = icon(ICON_PX)) {
        CTexPassElement::SRenderData data;
        data.tex = ICON;
        data.box = CBox{left, BOX.y + std::round((BOX.h - ICON_PX) / 2), sc<double>(ICON_PX), sc<double>(ICON_PX)};
        data.a   = a * (ACTIVE ? 1.F : 0.7F);
        g_pHyprRenderer->addPassElement(ctx, makeUnique<CTexPassElement>(std::move(data)));
        left += ICON_PX + std::round(8 * SCALE);
    }
    const CBox LEFTMOST  = toLocal(buttonBox(BAR, BUTTON_MAXIMIZE));
    const int  MAXWIDTH  = sc<int>(LEFTMOST.x - left - TITLE_PAD * SCALE);
    const auto TITLE_TEX = title(sc<uint64_t>((ACTIVE ? Nyx::settings.text : Nyx::settings.textInactive)->value()), MAXWIDTH, SCALE);
    if (TITLE_TEX) {
        CTexPassElement::SRenderData data;
        data.tex = TITLE_TEX;
        data.box = CBox{left, BOX.y + std::round((BOX.h - TITLE_TEX->m_size.y) / 2), TITLE_TEX->m_size.x, TITLE_TEX->m_size.y};
        data.a   = a;
        g_pHyprRenderer->addPassElement(ctx, makeUnique<CTexPassElement>(std::move(data)));
    }
}

void CNyxHeaderBar::pointerAt(const Vector2D& coords) {
    const auto HOVERED = visible() ? buttonAt(coords) : BUTTON_NONE;
    if (HOVERED == m_hovered)
        return;
    m_hovered = HOVERED;
    damageEntire();
}

void CNyxHeaderBar::run(eButton button) {
    const auto W = m_window.lock();
    if (!W)
        return;
    const auto TARGET = Nyx::selector(W);
    switch (button) {
        case BUTTON_CLOSE: Nyx::dispatch(std::format("hl.dsp.window.close({{ window = \"{}\" }})", TARGET)); break;
        case BUTTON_MAXIMIZE:
            Nyx::dispatch(std::format("hl.dsp.window.fullscreen({{ mode = \"maximized\", action = \"toggle\", window = \"{}\" }})", TARGET));
            break;
        case BUTTON_FLOAT:
            Nyx::dispatch(std::format("hl.dsp.window.float({{ action = \"{}\", window = \"{}\" }})", W->isFloating() ? "disable" : "enable", TARGET));
            break;
        default: break;
    }
}

bool CNyxHeaderBar::onButton(const Vector2D& coords, const IPointer::SButtonEvent& e) {
    const auto W = m_window.lock();
    if (!W || !visible())
        return false;
    const auto BUTTON  = buttonAt(coords);
    const bool PRESSED = e.state == WL_POINTER_BUTTON_STATE_PRESSED;

    if (e.button == RIGHT_BUTTON) {
        if (PRESSED)
            Nyx::openMenu(W, coords);
        return true;
    }
    if (e.button != LEFT_BUTTON)
        return true;

    if (!PRESSED) {
        const auto WAS = m_pressed;
        m_pressed      = BUTTON_NONE;
        if (WAS != BUTTON_NONE && WAS == BUTTON)
            run(BUTTON);
        damageEntire();
        return true;
    }

    if (BUTTON != BUTTON_NONE) {
        m_pressed = BUTTON;
        damageEntire();
        return true;
    }

    // Empty space: focus the window; floating ones drag, and a double-click maximizes.
    if (!Desktop::focusState()->isWindowActive(W))
        Desktop::focusState()->rawWindowFocus(W, Desktop::FOCUS_REASON_CLICK_DOWN);
    const auto NOW = std::chrono::steady_clock::now();
    if (Nyx::settings.doubleClickMaximize->value() && NOW - m_lastEmptyClick < DOUBLE_CLICK) {
        m_lastEmptyClick = {};
        run(BUTTON_MAXIMIZE);
        return true;
    }
    m_lastEmptyClick = NOW;
    if (W->isFloating()) {
        Desktop::windowState()->raise(W);
        if (!g_layoutManager->dragController()->target())
            g_layoutManager->beginDragTarget(W->layoutTarget(), MBIND_MOVE, std::nullopt, true);
    }
    return true;
}

bool CNyxHeaderBar::onInputOnDeco(const eInputType type, const Vector2D& coords, std::any data) {
    switch (type) {
        case INPUT_TYPE_BUTTON: return onButton(coords, std::any_cast<const IPointer::SButtonEvent&>(data));
        // A move bind (Super+drag) started on the bar moves the window as usual.
        case INPUT_TYPE_DRAG_START: return false;
        default: return false;
    }
}

eDecorationLayer CNyxHeaderBar::getDecorationLayer() {
    return DECORATION_LAYER_UNDER;
}

uint64_t CNyxHeaderBar::getDecorationFlags() {
    return DECORATION_ALLOWS_MOUSE_INPUT | DECORATION_PART_OF_MAIN_WINDOW;
}

std::string CNyxHeaderBar::getDisplayName() {
    return "Nyx headerbar";
}
