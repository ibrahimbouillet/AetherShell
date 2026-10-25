// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The Dock's transient window-preview card (FR-034, T43). A landscape, rounded, neutral glass card
// above a running application's icon, with one tile per live window of the group (most recent
// first). Each tile shows that window's genuine content through a documented DWM thumbnail, its title
// and a close button at the tile's upper right. The card never takes activation; clicking a tile asks
// the host to activate that exact window, the close button asks it to request a normal close.
//
// Honesty: a window whose thumbnail cannot be registered or whose source is only a minimized caption
// shows an explicit "Preview unavailable"/"Minimized" placeholder, never fabricated content. Pixels
// stay inside DWM; titles are transient and are never logged.
//
// Input: the window is exactly the card (no shadow margin, no invisible strip over other
// applications). Thumbnails are released on dismissal, refresh, theme/DPI relayout and destruction.
// DWM composes thumbnails above the window's own visuals, so nothing may be drawn over a thumbnail
// rectangle (the close button lives in the header) and their opacity is faded through DWM.
#pragma once

#include "applications/ApplicationTypes.h"
#include "dock/DockPreview.h"
#include "graphics/Animation.h"
#include "graphics/BitmapCache.h"
#include "graphics/TextCache.h"
#include "graphics/Theme.h"
#include "ui/SurfaceWindow.h"
#include "ui/accessibility/AccessibleSurface.h"

#include <dwmapi.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aether::dock {

struct DockPreviewContent {
    std::wstring applicationId;
    std::wstring applicationName;
    std::vector<apps::RunningWindow> windows;     // tracker order: most recent first
    std::shared_ptr<const apps::IconImage> icon;  // optional; the header omits it when null
};

class DockPreviewWindow final : public ui::SurfaceWindow, private ui::accessibility::IAccessibleSurfaceModel {
public:
    struct Callbacks {
        std::function<void()> pointerEntered;
        std::function<void()> pointerLeft;
        std::function<void(const PreviewWindowIdentity&)> activate;  // the host revalidates identity
        std::function<void(const PreviewWindowIdentity&)> close;     // the host revalidates identity
    };

    DockPreviewWindow(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler);
    ~DockPreviewWindow() override;

    [[nodiscard]] Result<void> Initialize(HWND owner, Callbacks callbacks);
    // `glassOpacity`: the Dock's material preference (0.3..1, 0.8 = the default translucent tint).
    void SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion, float glassOpacity = 0.8f);

    // Shows, or refreshes in place, the card above `anchorPx` (screen pixels of the Dock icon).
    // Windows without a live identity are dropped; returns false and dismisses when none remain.
    bool Present(const DockPreviewContent& content, const RECT& anchorPx);
    void Dismiss(bool immediate = false);
    [[nodiscard]] bool IsPresenting() const noexcept { return IsShown() && !dismissing_; }
    [[nodiscard]] const std::wstring& ApplicationId() const noexcept { return applicationId_; }

    // The host delivered a normal close request for this tile. Its button shows the pending state
    // until the tracker removes the window; after a timeout the window is treated as still open
    // (refused, cancelled or waiting on its own prompt) and the button becomes usable again.
    void MarkCloseRequested(HWND window);

    // Keyboard exploration while the Dock owns keyboard focus: Left/Right/Tab move between each
    // tile and its close button, Home/End, Enter/Space invoke, Delete closes the focused tile.
    void BeginKeyboard();
    void EndKeyboard();
    [[nodiscard]] bool KeyboardActive() const noexcept { return keyboard_; }
    bool HandleKey(UINT key);

    // Own-window test diagnostics. Screen pixels; no titles or pixels are exposed.
    [[nodiscard]] std::size_t TileCount() const noexcept { return tiles_.size(); }
    [[nodiscard]] std::size_t LiveThumbnailCount() const noexcept;
    [[nodiscard]] std::optional<RECT> TileBodyPx(std::size_t index) const;
    [[nodiscard]] std::optional<RECT> CloseButtonPx(std::size_t index) const;
    [[nodiscard]] std::optional<RECT> ThumbnailPx(std::size_t index) const;
    [[nodiscard]] HWND TileWindow(std::size_t index) const noexcept;
    [[nodiscard]] bool TileUnavailable(std::size_t index) const noexcept;
    [[nodiscard]] bool ClosePending(std::size_t index) const noexcept;
    [[nodiscard]] std::uint64_t RenderedFrames() const noexcept { return frames_; }

protected:
    void OnRender(const ui::RenderContext& context) override;
    bool OnAnimate(double now) override;
    void OnDpiChanged(UINT dpi, const RECT& suggestedPx) override;
    void OnPointerMove(D2D1_POINT_2F point) override;
    void OnPointerLeave() override;
    void OnPointerDown(ui::PointerButton button, D2D1_POINT_2F point) override;
    void OnPointerUp(ui::PointerButton button, D2D1_POINT_2F point) override;
    void OnDeviceReplaced() override;
    std::optional<LRESULT> OnMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void OnDestroying() override;

private:
    struct Tile {
        PreviewWindowIdentity identity;
        std::wstring title;
        bool minimized = false;
        HTHUMBNAIL thumbnail = nullptr;
        SIZE sourcePx{};
        std::optional<RECT> sourceCrop;  // invisible resize borders removed when DWM reports them
        bool unavailable = false;
        double closeRequestedAt = -1;
        int bodyId = 0;
        int closeId = 0;
    };
    enum class Part { Body, Close };
    struct Element { std::size_t tile = 0; Part part = Part::Body; };

    void Register(Tile& tile);
    static void Unregister(Tile& tile) noexcept;
    void ReleaseThumbnails() noexcept;
    void Layout();
    void UpdateThumbnails(float opacity);
    [[nodiscard]] std::optional<Element> HitElement(D2D1_POINT_2F point) const;
    [[nodiscard]] RECT ScreenRect(const D2D1_RECT_F& rect) const;
    void Invoke(const Element& element);
    void FinishDismiss();
    void Focus(std::size_t element);
    [[nodiscard]] float CornerRadius() const noexcept;

    // IAccessibleSurfaceModel
    [[nodiscard]] std::wstring RootName() const override;
    [[nodiscard]] CONTROLTYPEID RootControlType() const override { return UIA_ListControlTypeId; }
    [[nodiscard]] std::size_t ItemCount() const override { return tiles_.size() * 2; }
    [[nodiscard]] ui::accessibility::AccessibleItem Item(std::size_t index) const override;
    Result<void> Invoke(int id) override;
    Result<void> Toggle(int) override { return MakeError(ErrorCode::Unsupported, "window previews do not toggle"); }
    Result<void> SetFocus(int id) override;
    [[nodiscard]] std::optional<Element> ElementForId(int id) const;

    Callbacks callbacks_;
    graphics::DesignTokens tokens_;
    graphics::MotionPolicy motion_;
    float glassOpacity_ = 0.8f;
    graphics::TextCache text_;
    graphics::BitmapCache bitmaps_;
    graphics::Tween opacity_{0.0f};
    ui::accessibility::AccessibleSurface accessible_;
    std::wstring applicationId_;
    std::wstring applicationName_;
    std::shared_ptr<const apps::IconImage> icon_;
    std::wstring iconKey_;
    std::vector<Tile> tiles_;
    std::size_t omitted_ = 0;
    PreviewLayout layout_;
    UINT layoutDpi_ = 96;
    RECT anchorPx_{};
    std::optional<Element> hover_;
    std::optional<Element> pressed_;
    std::size_t focus_ = 0;
    bool keyboard_ = false;
    bool inside_ = false;
    bool dismissing_ = false;
    bool laying_ = false;
    std::map<HWND, std::pair<int, int>> ids_;
    int nextId_ = 1;
    std::uint64_t frames_ = 0;
};

}  // namespace aether::dock
