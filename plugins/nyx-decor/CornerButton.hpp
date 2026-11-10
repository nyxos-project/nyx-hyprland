// The chromeless control: a small button in a corner of a floating window that has no
// chrome on purpose (terminals). It opens Nyx Shell's window menu. It is a decoration
// of the window, so it moves, resizes and closes with it, and it shows only while the
// window floats (not tiled, maximized or fullscreen).

#pragma once

#include "globals.hpp"

#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>

class CNyxCornerButton : public IHyprWindowDecoration {
  public:
    explicit CNyxCornerButton(PHLWINDOW window);
    ~CNyxCornerButton() override = default;

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

    void      pointerAt(const Vector2D& coords);
    PHLWINDOW window() const;

  private:
    PHLWINDOWREF m_window;
    CBox         m_assignedBox;
    bool         m_hovered = false;

    struct {
        SP<Render::ITexture> tex;
        uint64_t             color = 0;
        int                  size  = 0;
    } m_dots;

    bool visible() const;
    // The button, in global logical coordinates.
    CBox buttonBoxGlobal() const;
    CBox windowBoxGlobal(const Render::SWindowRenderPresentation& presentation) const;
    CBox buttonIn(const CBox& window) const;
};
