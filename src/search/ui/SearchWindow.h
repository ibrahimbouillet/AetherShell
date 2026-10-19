// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Application search popup in the design language: a glass panel with a search field, an
// "Applications" caption, result rows with a selection pill and a sliding accent indicator, and a
// keyboard hint footer. The window keeps the height of its placement area; the panel inside animates
// its height with the slide spring when the result count changes, and the input region follows the
// panel, so queries never resize the window. Opening and closing are compositor transitions (instant
// under reduced motion and in high contrast).
#pragma once

#include "applications/ApplicationTypes.h"
#include "graphics/Animation.h"
#include "graphics/BitmapCache.h"
#include "graphics/TextCache.h"
#include "graphics/Theme.h"
#include "search/SearchService.h"
#include "ui/SurfaceWindow.h"
#include "ui/accessibility/AccessibleSurface.h"

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace aether::search::ui {

class SearchWindow final : public aether::ui::SurfaceWindow,
                           public aether::ui::accessibility::IAccessibleSurfaceModel {
public:
    SearchWindow(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler,
                 SearchService& search, apps::IApplicationLauncher& launcher, apps::IIconProvider& icons);
    ~SearchWindow() override;
    Result<void> Initialize();
    void Open();
    void Close();
    void Toggle();
    [[nodiscard]] bool IsOpen() const noexcept { return IsShown() && !closing_; }
    void SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion);
    [[nodiscard]] HWND EditHandle() const noexcept { return edit_; }
    [[nodiscard]] const std::vector<SearchResult>& Results() const noexcept { return results_; }
    [[nodiscard]] const std::wstring& ErrorText() const noexcept { return error_; }
    [[nodiscard]] std::size_t QueryCount() const noexcept { return queryCount_; }
    [[nodiscard]] HWND PreviousForegroundWindow() const noexcept { return previousFocus_; }
    // Overrides only this window's placement for deterministic own-window tests.
    void SetPlacementArea(std::optional<RECT> area);
    // Frames rendered so far; an idle open window renders only for caret blinks.
    [[nodiscard]] std::uint64_t RenderedFrames() const noexcept { return renderedFrames_; }
    // The panel rectangle in window DIPs at its current (possibly animating) height.
    [[nodiscard]] D2D1_RECT_F PanelDip() const noexcept;
    // The search field's text area in window DIPs (pointer clicks there place the caret).
    [[nodiscard]] D2D1_RECT_F InputDip() const noexcept;

    std::wstring RootName() const override { return L"Aether Shell application search"; }
    CONTROLTYPEID RootControlType() const override { return UIA_ListControlTypeId; }
    std::size_t ItemCount() const override { return results_.size(); }
    aether::ui::accessibility::AccessibleItem Item(std::size_t index) const override;
    Result<void> Invoke(int id) override;
    Result<void> Toggle(int id) override;
    Result<void> SetFocus(int id) override;

protected:
    void OnRender(const aether::ui::RenderContext& context) override;
    bool OnAnimate(double now) override;
    bool OnKeyDown(UINT key) override;
    void OnPointerMove(D2D1_POINT_2F point) override;
    void OnPointerDown(aether::ui::PointerButton button, D2D1_POINT_2F point) override;
    void OnPointerUp(aether::ui::PointerButton button, D2D1_POINT_2F point) override;
    void OnPointerCaptureLost() override;
    void OnWheel(float notches, D2D1_POINT_2F point) override;
    std::optional<LRESULT> OnMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void OnDestroying() override;
    void OnDpiChanged(UINT dpi, const RECT& suggested) override;

private:
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void RefreshResults();
    void ReadQuery();
    void FlushQuery();
    void UpdateSelection(std::size_t index);
    void EnsureVisible();
    std::size_t VisibleRows() const;
    void StopTimers();
    void BeginClose(bool restoreFocus);
    void Launch(std::size_t index);
    void Position();
    void UpdateEditPlacement();
    void UpdateInputRegion();
    void SetPanelTarget(bool animate);
    void MoveIndicator(bool animate);
    [[nodiscard]] bool Instant() const noexcept;
    [[nodiscard]] float Margin() const noexcept;
    [[nodiscard]] float TextScale() const noexcept;
    [[nodiscard]] float PanelWidthDip() const noexcept;
    [[nodiscard]] float MaxPanelDip() const noexcept;
    [[nodiscard]] float ChromeDip() const noexcept;
    [[nodiscard]] float TargetPanelDip() const noexcept;
    [[nodiscard]] std::optional<UINT32> CaretIndexAt(D2D1_POINT_2F point);
    std::optional<std::size_t> Hit(D2D1_POINT_2F point) const;
    int IdFor(std::size_t index) const;
    std::optional<std::size_t> IndexFor(int id) const;

    SearchService& search_;
    apps::IApplicationLauncher& launcher_;
    apps::IIconProvider& icons_;
    graphics::DesignTokens tokens_;
    graphics::MotionPolicy motion_;
    graphics::TextCache text_;
    graphics::BitmapCache bitmaps_;
    aether::ui::accessibility::AccessibleSurface accessible_;
    Connection indexReady_;
    HWND edit_ = nullptr, previousFocus_ = nullptr;
    HFONT editFont_ = nullptr;
    int editFontPx_ = 0;
    std::wstring query_, error_;
    std::vector<SearchResult> results_;
    std::size_t selected_ = 0;
    std::size_t firstVisible_ = 0, queryCount_ = 0;
    std::optional<RECT> placementArea_;
    graphics::Spring panelHeight_{0.0f};
    graphics::Tween indicatorY_{0.0f};
    double lastAnimate_ = 0.0;
    std::uint64_t renderedFrames_ = 0;
    float inputScroll_ = 0.0f;
    std::optional<UINT32> selectionAnchor_;
    RECT appliedRegion_{};
    bool queryPending_ = false, caretVisible_ = true;
    bool closing_ = false;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::unordered_map<std::wstring, std::shared_ptr<const apps::IconImage>> images_;
    std::unordered_map<std::wstring, int> accessibleIds_;
    int nextAccessibleId_ = 1;
};

}  // namespace aether::search::ui
