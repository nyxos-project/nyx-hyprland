// The Nyx headerbar: a compositor decoration on the top edge of a window that has no
// usable window controls of its own. It is part of the window (border and shadow go
// around both), moves, resizes and animates with it, and isn't drawn in true
// fullscreen.

#pragma once

#include "globals.hpp"

#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>

#include <array>
#include <chrono>
#include <optional>
#include <string>

class CNyxHeaderBar : public IHyprWindowDecoration {
  public:
    explicit CNyxHeaderBar(PHLWINDOW window);
    ~CNyxHeaderBar() override = default;

    SDecorationPositioningInfo getPositioningInfo() override;
    void                       onPositioningReply(const SDecorationPositioningReply& reply) override;
    void                       draw(Render::CRenderContext& ctx, PHLMONITOR monitor, float const& a, const Render::SWindowRenderPresentation& presentation) override;
    eDecorationType            getDecorationType() override;
    void                       updateWindow(PHLWINDOW window) override;
    void                       damageEntire() override;
    bool                       onInputOnDeco(const eInputType type, const Vector2D& coords, std::any data = {}) override;
    eDecorationLayer           getDecorationLayer() override;
    uint64_t                   getDecorationFlags() override;
    std::string                getDisplayName() override;

    // The pointer moved: light up the button under it.
    void pointerAt(const Vector2D& coords);

    PHLWINDOW window() const;

  private:
    enum eButton : uint8_t {
        BUTTON_MAXIMIZE = 0, // floating: maximize/restore; tiled: expand/restore
        BUTTON_FLOAT,        // float or tile
        BUTTON_CLOSE,
        BUTTON_COUNT,
        BUTTON_NONE = BUTTON_COUNT,
    };

    PHLWINDOWREF m_window;
    CBox         m_assignedBox;

    eButton      m_hovered = BUTTON_NONE;
    eButton      m_pressed = BUTTON_NONE;
    std::chrono::steady_clock::time_point m_lastEmptyClick;

    // The title, cached for its text, colour, width and scale.
    struct STitle {
        SP<Render::ITexture> tex;
        std::string          text;
        uint64_t             color    = 0;
        int                  maxWidth = 0;
        float                scale    = 0;
    } m_title;

    // A button's glyph, drawn with cairo for a colour and pixel size.
    struct SGlyph {
        SP<Render::ITexture> tex;
        uint64_t             color = 0;
        int                  size  = 0;
        int                  shape = -1;
    };
    std::array<SGlyph, BUTTON_COUNT> m_glyphs;

    bool         visible() const;
    bool         focused() const;
    int          barHeight() const;
    CBox         barBoxGlobal() const;
    CBox         barBoxGlobal(const Render::SWindowRenderPresentation& presentation) const;
    CBox         buttonBox(const CBox& bar, eButton button) const;
    eButton      buttonAt(const Vector2D& coords) const;
    int          glyphShape(eButton button) const;
    void         run(eButton button);
    bool         onButton(const Vector2D& coords, const IPointer::SButtonEvent& e);

    SP<Render::ITexture> glyph(eButton button, uint64_t color, int px);
    SP<Render::ITexture> title(uint64_t color, int maxWidth, float scale);
};
