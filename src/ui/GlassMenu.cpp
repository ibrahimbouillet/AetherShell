// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "ui/GlassMenu.h"

#include "diagnostics/Logger.h"
#include "graphics/GlassPainter.h"
#include "graphics/Glyphs.h"
#include "graphics/TextCache.h"
#include "graphics/Transition.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/WindowUtilities.h"
#include "ui/SurfaceWindow.h"
#include "ui/ThemedMenu.h"
#include "ui/accessibility/AccessibleSurface.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <string>
#include <utility>

namespace aether::ui {
namespace {

constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();
constexpr wchar_t kLevelClass[] = L"AetherShell.GlassMenu";
constexpr wchar_t kHostClass[] = L"AetherShell.GlassMenu.Host";
constexpr UINT_PTR kOwnerSubclassId = 0x474D4E55u;  // "GMNU"
constexpr UINT kOpenedMessage = WM_APP + 0x47;      // host: a level became visible (wParam = depth)
constexpr UINT kReleaseMessage = WM_APP + 0x48;     // host: a closing level finished hiding
constexpr UINT_PTR kSubmenuTimer = 1;               // hover delay before a submenu opens
constexpr UINT_PTR kIntentTimer = 2;                // re-evaluates hover after travel towards a submenu
constexpr UINT_PTR kScrollTimer = 3;                // repeats while the pointer rests on an edge arrow
constexpr UINT kScrollRepeatMs = 50;
constexpr float kScrollStrip = 16.0f;    // DIPs: edge-arrow strip of an overflowing level
constexpr float kSubmenuOverlap = 4.0f;  // DIPs a submenu panel overlaps its parent panel
constexpr float kGlyphBox = 16.0f;       // icon column glyphs on the 16-DIP grid
constexpr float kChevronBox = 12.0f;     // submenu chevron
constexpr float kArrowBox = 10.0f;       // scroll arrows
constexpr float kRadioDot = 3.0f;        // radius of a checked radio entry's dot
constexpr float kWheelRows = 3.0f;       // rows per wheel notch
constexpr float kMaxTextScale = 2.25f;
// Glass opacity preference of menu panels (GlassStyle::opacity). Menus are dense text over any
// backdrop: the default 0.8 (tint alpha 0.5 with blur) leaves dark-mode body text near 3.3:1 over a
// bright window; 0.85 (tint alpha 0.625) keeps the panel visibly translucent above 4.5:1.
constexpr float kPanelOpacity = 0.85f;

bool HasArea(const RECT& rect) noexcept { return rect.right > rect.left && rect.bottom > rect.top; }

LONG ClampCoordinate(LONG value, LONG low, LONG high) noexcept { return high < low ? low : std::clamp(value, low, high); }

bool OwnedByThisThread(HWND window) noexcept {
    return window && ::IsWindow(window) && ::GetWindowThreadProcessId(window, nullptr) == ::GetCurrentThreadId();
}

bool IsKeyboardMessage(UINT message) noexcept {
    switch (message) {
        case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        case WM_CHAR: case WM_SYSCHAR: case WM_DEADCHAR: case WM_SYSDEADCHAR: return true;
        default: return false;
    }
}

bool IsMouseMessage(UINT message) noexcept {
    return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
           (message >= WM_NCMOUSEMOVE && message <= WM_NCMBUTTONDBLCLK) ||
           (message >= WM_NCXBUTTONDOWN && message <= WM_NCXBUTTONDBLCLK);
}

bool UsesScreenCoordinates(UINT message) noexcept {
    return message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL ||
           (message >= WM_NCMOUSEMOVE && message <= WM_NCXBUTTONDBLCLK);
}

bool IsButtonDown(UINT message) noexcept {
    switch (message) {
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
        case WM_NCLBUTTONDOWN: case WM_NCLBUTTONDBLCLK: case WM_NCRBUTTONDOWN: case WM_NCRBUTTONDBLCLK:
        case WM_NCMBUTTONDOWN: case WM_NCMBUTTONDBLCLK: case WM_NCXBUTTONDOWN: case WM_NCXBUTTONDBLCLK: return true;
        default: return false;
    }
}

bool SystemKeyboardCues() noexcept {
    BOOL cues = FALSE;
    return ::SystemParametersInfoW(SPI_GETKEYBOARDCUES, 0, &cues, 0) && cues;
}

UINT DelayMs(double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0) return 1;
    return static_cast<UINT>(std::clamp(std::lround(seconds * 1000.0), 1L, 10'000L));
}

float Cross(POINT a, POINT b, POINT c) noexcept {
    return static_cast<float>(b.x - a.x) * static_cast<float>(c.y - a.y) -
           static_cast<float>(b.y - a.y) * static_cast<float>(c.x - a.x);
}

bool InTriangle(POINT point, POINT a, POINT b, POINT c) noexcept {
    const float d1 = Cross(a, b, point);
    const float d2 = Cross(b, c, point);
    const float d3 = Cross(c, a, point);
    const bool negative = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
    const bool positive = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;
    return !(negative && positive);
}

// Lower case of one UTF-16 unit independent of the C runtime locale (which towlower follows), so
// "&Étendre" answers both É and é.
wchar_t FoldCase(wchar_t character) noexcept {
    wchar_t buffer[1] = {static_cast<wchar_t>(std::towlower(character))};
    ::CharLowerBuffW(buffer, 1);
    return buffer[0];
}

RECT WorkAreaFor(HMONITOR monitor) {
    if (auto info = platform::QueryMonitor(monitor)) return info->workArea;
    return platform::PrimaryMonitor().workArea;
}

// One entry of a level, copied out of the MenuEntry model so a closing level never touches the
// caller's entries after Show returned.
struct MenuRow {
    MenuLabel label;
    std::uint32_t id = 0;
    bool separator = false;
    bool enabled = true;
    bool checked = false;
    bool radio = false;
    bool submenu = false;
    std::optional<graphics::Glyph> glyph;
    float top = 0.0f;     // DIPs from the first row
    float height = 0.0f;  // DIPs
    float trail = 0.0f;   // DIPs right of the label (accelerator, chevron, padding)
    float textHeight = 0.0f;
    float acceleratorWidth = 0.0f;
    float acceleratorHeight = 0.0f;
    bool underlined = false;
    winrt::com_ptr<IDWriteTextLayout> text;
    winrt::com_ptr<IDWriteTextLayout> accelerator;

    [[nodiscard]] bool Selectable() const noexcept { return !separator && enabled; }
};

enum class HitKind { None, Panel, Row, ScrollUp, ScrollDown };
struct LevelHit {
    HitKind kind = HitKind::None;
    std::size_t row = kNone;
};

class MenuLevel;

// The presenter's state and modal loop. Lives in GlassMenuPresenter::Impl.
class MenuController {
public:
    MenuController(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler,
                   const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion, IMenuWindowOps& ops);
    ~MenuController();
    MenuController(const MenuController&) = delete;
    MenuController& operator=(const MenuController&) = delete;

    void SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion);
    Result<MenuSelection> Show(std::span<const MenuEntry> entries, const MenuRequest& request);
    void Cancel() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept { return open_; }
    [[nodiscard]] GlassMenuPresenter::Presentation LastPresentation() const noexcept { return last_; }
    void SetTestHooks(GlassMenuPresenter::TestHooks hooks) { hooks_ = std::move(hooks); }
    [[nodiscard]] std::optional<RECT> RowBoundsPx(std::size_t depth, std::size_t row) const;

    // Used by the levels.
    [[nodiscard]] graphics::GraphicsDevice& Graphics() noexcept { return device_; }
    [[nodiscard]] graphics::FrameScheduler& Frames() noexcept { return scheduler_; }
    [[nodiscard]] const graphics::DesignTokens& Tokens() const noexcept { return tokens_; }
    [[nodiscard]] const graphics::MotionPolicy& Motion() const noexcept { return motion_; }
    [[nodiscard]] graphics::TextCache& Text() noexcept { return text_; }
    [[nodiscard]] bool Instant() const noexcept {
        return !motion_.AllowsMovement() || tokens_.mode == graphics::ThemeMode::HighContrast;
    }
    [[nodiscard]] double Scaled(double seconds) const noexcept {
        if (Instant()) return 0.0;
        const double scaled = motion_.Scale(seconds);
        return std::isfinite(scaled) && scaled > 0.0 ? scaled : 0.0;
    }
    [[nodiscard]] float TextScale() const noexcept {
        return std::isfinite(tokens_.textScale) ? std::clamp(tokens_.textScale, 1.0f, kMaxTextScale) : 1.0f;
    }
    [[nodiscard]] graphics::TextStyle LabelStyle(float scale) const noexcept {
        graphics::TextStyle style;
        style.size = tokens_.type.body * scale;
        style.ellipsis = false;
        style.verticalAlignment = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
        return style;
    }
    [[nodiscard]] float BaseLineHeight();
    [[nodiscard]] HWND Owner() const noexcept { return owner_; }
    [[nodiscard]] bool CuesVisible() const noexcept { return cues_; }
    [[nodiscard]] bool Active() const noexcept { return open_ && !done_; }
    void Rendered(std::size_t depth) noexcept;
    void Activate(std::size_t depth, std::size_t row, bool keyboard);
    void RequestRelease() noexcept {
        if (host_) ::PostMessageW(host_, kReleaseMessage, 0, 0);
    }
    void OnRootCaptureLost(HWND capture) noexcept {
        if (Active() && !IsLevelWindow(capture)) Dismiss();
    }
    void Choose(std::uint32_t command) noexcept;
    void Dismiss() noexcept;
    void SetExpanded(std::size_t depth, std::size_t row, bool expanded);
    [[nodiscard]] winrt::com_ptr<IRawElementProviderFragment> ParentProvider(std::size_t depth) const;
    [[nodiscard]] winrt::com_ptr<IRawElementProviderFragment> ChildProvider(std::size_t depth, std::size_t row) const;

private:
    struct Hit {
        std::size_t depth = kNone;
        LevelHit target;
    };

    [[nodiscard]] std::optional<Result<MenuSelection>> ShowGlass(std::span<const MenuEntry> entries,
                                                                 const MenuRequest& request);
    [[nodiscard]] Result<MenuSelection> ShowFallback(std::span<const MenuEntry> entries, const MenuRequest& request);
    [[nodiscard]] bool EnsureHost();
    void RunLoop();
    bool Intercept(MSG& message);
    void OnKey(const MSG& message);
    void OnMnemonic(wchar_t character);
    void OnMouse(const MSG& message);
    void UpdateHover(POINT previous, bool forced);
    [[nodiscard]] bool TowardsSubmenu(POINT previous, POINT current, std::size_t depth) const;
    [[nodiscard]] Hit HitTest(POINT screen) const;
    void OpenSubmenu(std::size_t depth, std::size_t row, bool selectFirst);
    void CloseLevelsAbove(std::size_t depth);
    void CloseAllLevels();
    void ReleaseHidden();
    void ReleaseClosing() noexcept;
    void ArmSubmenuTimer(std::size_t depth, std::size_t row);
    void CancelSubmenuTimer() noexcept;
    void StartAutoScroll(std::size_t depth, int direction);
    void StopAutoScroll() noexcept;
    void KillTimers() noexcept;
    void OnTimer(UINT_PTR id);
    void NotifyOpened(std::size_t depth);
    void ShowCues();
    [[nodiscard]] MenuLevel* ActiveLevel() const noexcept { return levels_.empty() ? nullptr : levels_.back().get(); }
    [[nodiscard]] bool IsLevelWindow(HWND window) const noexcept;
    void WatchForeground() noexcept;
    void StopWatchingForeground() noexcept;
    static void CALLBACK ForegroundChanged(HWINEVENTHOOK hook, DWORD event, HWND window, LONG object, LONG child,
                                           DWORD thread, DWORD time);
    static LRESULT CALLBACK HostProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK OwnerProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                      DWORD_PTR reference);

    graphics::GraphicsDevice& device_;
    graphics::FrameScheduler& scheduler_;
    graphics::DesignTokens tokens_;
    graphics::MotionPolicy motion_;
    IMenuWindowOps* ops_;
    graphics::TextCache text_;
    float baseLine_ = 0.0f;
    GlassMenuPresenter::TestHooks hooks_;
    GlassMenuPresenter::Presentation last_ = GlassMenuPresenter::Presentation::None;
    // False once the controller is destroyed; a Show still on the stack (an owner destroyed from
    // inside the menu loop together with its presenter) checks it before touching any member.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    HWND host_ = nullptr;
    HWND owner_ = nullptr;
    HWINEVENTHOOK foregroundHook_ = nullptr;
    MenuRequest request_{};
    std::vector<std::unique_ptr<MenuLevel>> levels_;
    std::vector<std::unique_ptr<MenuLevel>> closing_;
    std::uint32_t command_ = 0;
    bool open_ = false;
    bool fallbackOpen_ = false;
    bool done_ = false;
    bool armed_ = false;
    bool cues_ = false;
    bool altDown_ = false;
    bool pressSeen_ = false;
    bool moved_ = false;
    POINT openPointer_{};
    POINT lastPointer_{};
    std::size_t pendingDepth_ = kNone;
    std::size_t pendingRow_ = kNone;
    std::size_t scrollDepth_ = kNone;
    int scrollDirection_ = 0;
};

// One menu level: a composition SurfaceWindow that never activates, holding the glass panel and
// exposing its rows to UI Automation.
class MenuLevel final : public SurfaceWindow, private accessibility::IAccessibleSurfaceModel {
public:
    MenuLevel(MenuController& controller, std::size_t depth)
        : SurfaceWindow(controller.Graphics(), controller.Frames()), controller_(controller), depth_(depth) {}
    ~MenuLevel() override { Destroy(); }
    MenuLevel(const MenuLevel&) = delete;
    MenuLevel& operator=(const MenuLevel&) = delete;

    [[nodiscard]] Result<void> Build(std::span<const MenuEntry> entries, std::wstring name, POINT origin);
    void LayoutRoot(const MenuRequest& request);
    void LayoutSubmenu(const RECT& rowPx);
    void ShowEntrance();
    void BeginClose();

    [[nodiscard]] std::size_t Depth() const noexcept { return depth_; }
    [[nodiscard]] std::span<const MenuEntry> Entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t RowCount() const noexcept { return rows_.size(); }
    [[nodiscard]] const MenuRow& Row(std::size_t index) const noexcept { return rows_[index]; }
    [[nodiscard]] std::size_t Selected() const noexcept { return selected_; }
    [[nodiscard]] std::size_t OpenChild() const noexcept { return openChild_; }
    void SetOpenChild(std::size_t row) {
        const std::size_t previous = openChild_;
        if (row == previous) return;
        openChild_ = row;
        if (connected_ && previous != kNone)
            (void)accessible_.NotifyExpandCollapseChanged(static_cast<int>(previous) + 1, ExpandCollapseState_Expanded);
        if (connected_ && row != kNone)
            (void)accessible_.NotifyExpandCollapseChanged(static_cast<int>(row) + 1, ExpandCollapseState_Collapsed);
    }
    [[nodiscard]] bool Closing() const noexcept { return closing_; }

    void Select(std::size_t row, bool keyboard, bool animate = true);
    void MoveSelection(int direction) { Select(NextMenuRow(entries_, selected_, direction), true); }
    void SelectEdge(bool last) { Select(NextMenuRow(entries_, kNone, last ? -1 : 1), true); }
    void Page(int direction);
    void ScrollBy(float deltaDip) { ScrollTo(scroll_.Target() + deltaDip, true); }
    [[nodiscard]] bool CanScroll(int direction) const noexcept {
        return direction < 0 ? scroll_.Target() > 0.5f : scroll_.Target() < MaxScroll() - 0.5f;
    }
    [[nodiscard]] float ViewportHeight() const noexcept { return std::max(0.0f, viewportBottom_ - viewportTop_); }
    [[nodiscard]] int PaddingPx() const noexcept { return Px(controller_.Tokens().menu.padding); }
    [[nodiscard]] LevelHit HitTest(POINT screen) const;
    [[nodiscard]] RECT PanelScreenRect() const noexcept;
    [[nodiscard]] std::optional<RECT> RowScreenRect(std::size_t row, bool fullyVisible) const;
    void NotifyFocus() noexcept {
        if (connected_ && selected_ != kNone && !closing_) (void)accessible_.NotifyFocusChanged(static_cast<int>(selected_) + 1);
    }
    [[nodiscard]] winrt::com_ptr<IRawElementProviderFragment> Provider(std::optional<std::size_t> row = {}) const {
        auto provider = row ? accessible_.ItemProvider(static_cast<int>(*row) + 1) : accessible_.RootProvider();
        return provider ? provider->try_as<IRawElementProviderFragment>() : nullptr;
    }

protected:
    void OnRender(const RenderContext& context) override;
    bool OnAnimate(double now) override;
    std::optional<LRESULT> OnMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void OnDpiChanged(UINT newDpi, const RECT& suggestedPx) override;
    void OnDestroying() override;

private:
    // IAccessibleSurfaceModel (called on this window's STA thread).
    [[nodiscard]] std::wstring RootName() const override { return name_; }
    [[nodiscard]] CONTROLTYPEID RootControlType() const override { return UIA_MenuControlTypeId; }
    [[nodiscard]] std::size_t ItemCount() const override { return accessibleRows_.size(); }
    [[nodiscard]] accessibility::AccessibleItem Item(std::size_t index) const override;
    Result<void> Invoke(int id) override;
    Result<void> Toggle(int id) override;
    Result<void> SetFocus(int id) override;
    Result<void> SetExpanded(int id, bool expanded) override;
    Result<void> SelectItem(int id) override;
    [[nodiscard]] bool SupportsSelection() const override {
        return std::any_of(rows_.begin(), rows_.end(), [](const MenuRow& row) { return row.radio; });
    }
    [[nodiscard]] bool SelectionRequired() const override { return SupportsSelection(); }
    [[nodiscard]] winrt::com_ptr<IRawElementProviderFragment> ParentProvider() const override {
        return closing_ ? nullptr : controller_.ParentProvider(depth_);
    }
    [[nodiscard]] winrt::com_ptr<IRawElementProviderFragment> ChildProvider(int id) const override {
        return closing_ ? nullptr : controller_.ChildProvider(depth_, RowFromId(id));
    }

    [[nodiscard]] D2D1_SIZE_F Measure(float availableWidthDip, int minimumWidthPx, float availableHeightDip);
    void Apply(POINT origin, SIZE panel);
    [[nodiscard]] float MarginDip() const noexcept;
    [[nodiscard]] float MaxScroll() const noexcept {
        return overflow_ ? std::max(0.0f, contentHeight_ - ViewportHeight()) : 0.0f;
    }
    void ScrollTo(float target, bool animate);
    void UpdateArrows(bool animate);
    void EnsureVisible(std::size_t row);
    [[nodiscard]] std::size_t RowFromId(int id) const noexcept;
    [[nodiscard]] int Px(float dip) const noexcept { return static_cast<int>(std::lround(ToPx(dip))); }
    void DrawRow(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, std::size_t index, const D2D1_RECT_F& rect,
                 double now);
    void DrawArrows(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, double now);

    MenuController& controller_;
    std::size_t depth_;
    std::span<const MenuEntry> entries_;  // the caller's entries; read only while the menu is open
    std::vector<MenuRow> rows_;
    std::vector<std::size_t> accessibleRows_;
    std::wstring name_;
    accessibility::AccessibleSurface accessible_;
    bool connected_ = false;
    bool closing_ = false;
    bool iconColumn_ = false;
    bool overflow_ = false;
    D2D1_RECT_F panel_{};  // DIPs, window-relative
    float viewportTop_ = 0.0f;
    float viewportBottom_ = 0.0f;
    float contentHeight_ = 0.0f;
    float rowWidth_ = 0.0f;
    graphics::TransitionShape entrance_{};
    std::size_t selected_ = kNone;
    std::size_t openChild_ = kNone;
    std::size_t fadingRow_ = kNone;
    graphics::Tween pill_{0.0f};     // opacity of the selected row's pill
    graphics::Tween fading_{0.0f};   // opacity of the previously selected row's pill
    graphics::Tween scroll_{0.0f};   // DIPs scrolled
    graphics::Tween upArrow_{0.0f};
    graphics::Tween downArrow_{0.0f};
};

// --- MenuLevel ---

Result<void> MenuLevel::Build(std::span<const MenuEntry> entries, std::wstring name, POINT origin) {
    entries_ = entries;
    name_ = name.empty() ? std::wstring(L"Menu") : std::move(name);
    rows_.clear();
    accessibleRows_.clear();
    rows_.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const MenuEntry& entry = entries[index];
        MenuRow row;
        row.separator = entry.separator;
        if (entry.separator) {
            row.enabled = false;
        } else {
            row.label = ParseMenuLabel(entry.text);
            row.submenu = !entry.submenu.empty();
            row.id = row.submenu ? 0u : entry.id;
            row.enabled = entry.enabled;
            row.checked = entry.checked;
            row.radio = entry.radio;
            row.glyph = entry.glyph;
            accessibleRows_.push_back(index);
        }
        rows_.push_back(std::move(row));
    }
    SurfaceWindowOptions options;
    options.className = kLevelClass;
    options.title = name_;
    options.topmost = true;
    options.noActivate = true;
    options.toolWindow = true;
    options.owner = controller_.Owner();
    if (auto created = Create(options, RECT{origin.x, origin.y, origin.x + 1, origin.y + 1}); !created) return created.error();
    if (auto connected = accessible_.Connect(Handle(), *this); !connected) {
        Destroy();
        return connected.error();
    }
    connected_ = true;
    return Ok();
}

float MenuLevel::MarginDip() const noexcept {
    // The whole exterior shadow plus the entrance offset stay inside the window, so the shadow fades
    // out naturally instead of being cut into a rectangle by the window edge.
    const auto& tokens = controller_.Tokens();
    const float extent = graphics::ShadowExtent(graphics::ElevationFor(tokens, graphics::Elevation::Popup));
    const float offset = std::isfinite(tokens.menu.enterOffset) ? std::max(0.0f, tokens.menu.enterOffset) : 0.0f;
    return std::ceil(extent) + std::ceil(offset);
}

D2D1_SIZE_F MenuLevel::Measure(float availableWidthDip, int minimumWidthPx, float availableHeightDip) {
    const auto& tokens = controller_.Tokens();
    const auto& menu = tokens.menu;
    IDWriteFactory* factory = Device().DWriteFactory();
    IDWriteTextFormat* format = controller_.Text().Format(controller_.LabelStyle(controller_.TextScale()));
    const float baseLine = controller_.BaseLineHeight();
    iconColumn_ = std::any_of(rows_.begin(), rows_.end(), [](const MenuRow& row) {
        return !row.separator && (row.glyph.has_value() || row.checked || row.radio);
    });
    const float lead = menu.paddingX + (iconColumn_ ? menu.iconColumn + menu.columnGap : 0.0f);
    float content = 0.0f;
    for (auto& row : rows_) {
        row.text = nullptr;
        row.accelerator = nullptr;
        row.acceleratorWidth = 0.0f;
        row.acceleratorHeight = 0.0f;
        row.underlined = false;
        if (row.separator) continue;
        float natural = 0.0f;
        if (factory && format) {
            if (SUCCEEDED(factory->CreateTextLayout(row.label.text.data(), static_cast<UINT32>(row.label.text.size()), format,
                                                    10000.0f, 10000.0f, row.text.put()))) {
                row.text->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK);
                DWRITE_TEXT_METRICS metrics{};
                if (SUCCEEDED(row.text->GetMetrics(&metrics))) natural = metrics.width;
            }
            if (!row.label.accelerator.empty() &&
                SUCCEEDED(factory->CreateTextLayout(row.label.accelerator.data(),
                                                    static_cast<UINT32>(row.label.accelerator.size()), format, 10000.0f,
                                                    10000.0f, row.accelerator.put()))) {
                row.accelerator->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                DWRITE_TEXT_METRICS metrics{};
                if (SUCCEEDED(row.accelerator->GetMetrics(&metrics))) {
                    row.acceleratorWidth = std::ceil(metrics.widthIncludingTrailingWhitespace);
                    row.acceleratorHeight = metrics.height;
                }
            }
        }
        row.trail = menu.paddingX;
        if (row.accelerator) row.trail += menu.acceleratorGap + row.acceleratorWidth;
        if (row.submenu) row.trail += menu.columnGap + menu.submenuColumn;
        content = std::max(content, lead + std::ceil(natural) + row.trail);
    }
    const float width = MenuPanelWidthDip(content + 2.0f * menu.padding, minimumWidthPx, Dpi(), menu, availableWidthDip);
    rowWidth_ = std::max(1.0f, width - 2.0f * menu.padding);
    float top = 0.0f;
    for (auto& row : rows_) {
        row.top = top;
        if (row.separator) {
            row.height = menu.separatorHeight;
        } else {
            float textHeight = baseLine;
            if (row.text) {
                // Labels longer than the widest panel wrap onto more lines; the row grows with them.
                row.text->SetMaxWidth(std::max(1.0f, rowWidth_ - lead - row.trail));
                DWRITE_TEXT_METRICS metrics{};
                if (SUCCEEDED(row.text->GetMetrics(&metrics))) textHeight = metrics.height;
            }
            row.textHeight = textHeight;
            row.height = std::max(menu.itemHeight, std::ceil(menu.itemHeight - baseLine + textHeight));
        }
        top += row.height;
    }
    contentHeight_ = top;
    float height = contentHeight_ + 2.0f * menu.padding;
    overflow_ = std::isfinite(availableHeightDip) && height > availableHeightDip + 0.5f;
    if (overflow_) height = std::max(std::floor(availableHeightDip), 2.0f * kScrollStrip + menu.itemHeight);
    return D2D1::SizeF(width, height);
}

void MenuLevel::Apply(POINT origin, SIZE panel) {
    const auto& tokens = controller_.Tokens();
    const int margin = static_cast<int>(std::ceil(ToPx(MarginDip())));
    const RECT bounds{origin.x - margin, origin.y - margin, origin.x + panel.cx + margin, origin.y + panel.cy + margin};
    SetBoundsPx(bounds);
    panel_ = D2D1::RectF(ToDip(static_cast<float>(margin)), ToDip(static_cast<float>(margin)),
                         ToDip(static_cast<float>(margin + panel.cx)), ToDip(static_cast<float>(margin + panel.cy)));
    const float inset = overflow_ ? kScrollStrip : tokens.menu.padding;
    viewportTop_ = panel_.top + inset;
    viewportBottom_ = panel_.bottom - inset;
    // Input only on the panel; the shadow margin is drawn (visual region) but transparent to hits.
    const RECT input{margin, margin, margin + panel.cx, margin + panel.cy};
    SetInputRegionPx(std::span<const RECT>(&input, 1));
    const RECT visual{0, 0, platform::Width(bounds), platform::Height(bounds)};
    SetVisualRegionPx(std::span<const RECT>(&visual, 1));
    const bool highContrast = tokens.mode == graphics::ThemeMode::HighContrast;
    SetBackdropPanel(panel_, tokens.menu.radius, tokens.transparencyEnabled && !highContrast, tokens.glass.backdropOpacity);
    scroll_.SnapTo(0.0f);
    UpdateArrows(false);
    Invalidate();
}

void MenuLevel::LayoutRoot(const MenuRequest& request) {
    const auto& menu = controller_.Tokens().menu;
    const bool anchored = request.placement != MenuPlacement::AtPoint && HasArea(request.anchor);
    const HMONITOR monitor = anchored ? ::MonitorFromRect(&request.anchor, MONITOR_DEFAULTTONEAREST)
                                      : ::MonitorFromPoint(request.point, MONITOR_DEFAULTTONEAREST);
    const RECT work = WorkAreaFor(monitor);
    const int margin = Px(menu.edgeMargin);
    const int gap = Px(menu.anchorGap);
    int availableHeight = platform::Height(work) - 2 * margin;
    if (anchored) {
        // Tall menus take the larger side of the anchor instead of covering it.
        const int below = work.bottom - margin - (request.anchor.bottom + gap);
        const int above = request.anchor.top - gap - (work.top + margin);
        const int room = std::max(below, above);
        if (room >= Px(menu.itemHeight * 3.0f)) availableHeight = std::min(availableHeight, room);
    }
    const D2D1_SIZE_F size = Measure(ToDip(static_cast<float>(platform::Width(work) - 2 * margin)), request.minimumWidthPx,
                                     ToDip(static_cast<float>(availableHeight)));
    const SIZE panel{Px(size.width), Px(size.height)};
    const POINT origin = PlaceMenuPanel(panel, request, work, gap, margin);
    Apply(origin, panel);

    // The entrance settles from the anchor side: a menu below its anchor starts slightly higher.
    const RECT bounds = BoundsPx();
    const POINT reference = anchored ? MenuTrackPoint(request) : request.point;
    const bool below = anchored ? origin.y + panel.cy / 2 >= (request.anchor.top + request.anchor.bottom) / 2
                                : origin.y >= reference.y - 1;
    entrance_ = graphics::TransitionShape{};
    entrance_.offsetY = below ? -menu.enterOffset : menu.enterOffset;
    entrance_.scale = menu.enterScale;
    entrance_.centerX = std::clamp(ToDip(static_cast<float>(reference.x - bounds.left)), panel_.left, panel_.right);
    entrance_.centerY = below ? panel_.top : panel_.bottom;
}

void MenuLevel::LayoutSubmenu(const RECT& rowPx) {
    const auto& menu = controller_.Tokens().menu;
    const RECT work = WorkAreaFor(::MonitorFromRect(&rowPx, MONITOR_DEFAULTTONEAREST));
    const int margin = Px(menu.edgeMargin);
    const D2D1_SIZE_F size = Measure(ToDip(static_cast<float>(platform::Width(work) - 2 * margin)), 0,
                                     ToDip(static_cast<float>(platform::Height(work) - 2 * margin)));
    const SIZE panel{Px(size.width), Px(size.height)};
    const POINT origin = PlaceSubmenuPanel(panel, rowPx, work, Px(kSubmenuOverlap), margin);
    Apply(origin, panel);
    const RECT bounds = BoundsPx();
    const bool right = origin.x + panel.cx / 2 >= (rowPx.left + rowPx.right) / 2;
    entrance_ = graphics::TransitionShape{};
    entrance_.offsetX = right ? -menu.enterOffset : menu.enterOffset;
    entrance_.scale = menu.enterScale;
    entrance_.centerX = right ? panel_.left : panel_.right;
    entrance_.centerY = std::clamp(ToDip(static_cast<float>((rowPx.top + rowPx.bottom) / 2 - bounds.top)), panel_.top,
                                   panel_.bottom);
}

void MenuLevel::ShowEntrance() {
    const auto& tokens = controller_.Tokens();
    graphics::Transition transition = graphics::MakeEnterTransition(entrance_, tokens, controller_.Motion());
    if (!transition.IsInstant()) transition.duration = controller_.Scaled(tokens.menu.open);
    ShowAnimated(transition, false);
    if (connected_) (void)accessible_.NotifyMenuVisibility(true);
}

void MenuLevel::BeginClose() {
    if (connected_) (void)accessible_.NotifyMenuVisibility(false);
    closing_ = true;
    openChild_ = kNone;
    if (!Handle() || !IsShown()) {
        controller_.RequestRelease();
        return;
    }
    const auto& tokens = controller_.Tokens();
    graphics::Transition transition = graphics::MakeExitTransition(graphics::TransitionShape{}, tokens, controller_.Motion());
    if (!transition.IsInstant()) transition.duration = controller_.Scaled(tokens.menu.close);
    // Hides at once when the fade is instant; the release request follows from WM_SHOWWINDOW.
    HideAnimated(transition);
}

void MenuLevel::Select(std::size_t row, bool keyboard, bool animate) {
    if (row != kNone && (row >= rows_.size() || !rows_[row].Selectable())) row = kNone;
    if (row == selected_) {
        if (keyboard && row != kNone) EnsureVisible(row);
        return;
    }
    const double now = graphics::FrameScheduler::Now();
    const auto& tokens = controller_.Tokens();
    const double duration = animate ? controller_.Scaled(tokens.menu.hover) : 0.0;
    const float incomingStart = row != kNone && row == fadingRow_ ? fading_.Value(now) : 0.0f;
    if (selected_ != kNone) {
        fadingRow_ = selected_;
        fading_.SnapTo(pill_.Value(now));
        fading_.AnimateTo(0.0f, now, duration, tokens.curves.standard);
    } else if (row == fadingRow_) {
        fadingRow_ = kNone;
        fading_.SnapTo(0.0f);
    }
    selected_ = row;
    pill_.SnapTo(row != kNone ? incomingStart : 0.0f);
    if (row != kNone) pill_.AnimateTo(1.0f, now, duration, tokens.curves.decelerate);
    if (keyboard && row != kNone) EnsureVisible(row);
    Invalidate();
    RequestAnimationFrame();
    NotifyFocus();
}

void MenuLevel::Page(int direction) {
    const auto count = static_cast<long long>(rows_.size());
    if (count == 0) return;
    if (selected_ == kNone) {
        SelectEdge(direction < 0);
        return;
    }
    const float itemHeight = std::max(1.0f, controller_.Tokens().menu.itemHeight);
    const auto page = std::max<long long>(1, static_cast<long long>(ViewportHeight() / itemHeight));
    const long long step = direction < 0 ? -1 : 1;
    const long long from = static_cast<long long>(selected_);
    const long long target = std::clamp(from + step * page, 0LL, count - 1);
    for (long long index = target; index >= 0 && index < count; index += step) {
        if (rows_[static_cast<std::size_t>(index)].Selectable()) return Select(static_cast<std::size_t>(index), true);
    }
    for (long long index = target - step; index != from && index >= 0 && index < count; index -= step) {
        if (rows_[static_cast<std::size_t>(index)].Selectable()) return Select(static_cast<std::size_t>(index), true);
    }
}

void MenuLevel::ScrollTo(float target, bool animate) {
    target = std::clamp(target, 0.0f, MaxScroll());
    target = ToDip(std::round(ToPx(target)));  // rest on whole pixels so text stays crisp
    if (std::fabs(target - scroll_.Target()) < 0.01f) return;
    const auto& tokens = controller_.Tokens();
    const double now = graphics::FrameScheduler::Now();
    scroll_.AnimateTo(target, now, animate ? controller_.Scaled(tokens.form.scroll) : 0.0, tokens.curves.standard);
    UpdateArrows(animate);
    Invalidate();
    RequestAnimationFrame();
}

void MenuLevel::UpdateArrows(bool animate) {
    const double now = graphics::FrameScheduler::Now();
    const double duration = animate ? controller_.Scaled(controller_.Tokens().menu.hover) : 0.0;
    const float up = overflow_ && CanScroll(-1) ? 1.0f : 0.0f;
    const float down = overflow_ && CanScroll(1) ? 1.0f : 0.0f;
    if (upArrow_.Target() != up) upArrow_.AnimateTo(up, now, duration, controller_.Tokens().curves.standard);
    if (downArrow_.Target() != down) downArrow_.AnimateTo(down, now, duration, controller_.Tokens().curves.standard);
}

void MenuLevel::EnsureVisible(std::size_t row) {
    if (!overflow_ || row >= rows_.size()) return;
    const float top = rows_[row].top;
    const float bottom = top + rows_[row].height;
    const float view = ViewportHeight();
    float target = scroll_.Target();
    if (top < target) target = top;
    else if (bottom > target + view) target = bottom - view;
    ScrollTo(target, true);
}

LevelHit MenuLevel::HitTest(POINT screen) const {
    if (!Handle() || closing_ || !IsShown()) return {};
    const RECT bounds = BoundsPx();
    const float x = ToDip(static_cast<float>(screen.x - bounds.left));
    const float y = ToDip(static_cast<float>(screen.y - bounds.top));
    if (x < panel_.left || x >= panel_.right || y < panel_.top || y >= panel_.bottom) return {};
    if (overflow_ && y < viewportTop_) return {HitKind::ScrollUp, kNone};
    if (overflow_ && y >= viewportBottom_) return {HitKind::ScrollDown, kNone};
    if (y >= viewportTop_ && y < viewportBottom_) {
        const float content = y - viewportTop_ + scroll_.Value(graphics::FrameScheduler::Now());
        for (std::size_t index = 0; index < rows_.size(); ++index) {
            if (content >= rows_[index].top && content < rows_[index].top + rows_[index].height) return {HitKind::Row, index};
        }
    }
    return {HitKind::Panel, kNone};
}

RECT MenuLevel::PanelScreenRect() const noexcept {
    const RECT bounds = BoundsPx();
    return RECT{bounds.left + Px(panel_.left), bounds.top + Px(panel_.top), bounds.left + Px(panel_.right),
                bounds.top + Px(panel_.bottom)};
}

std::optional<RECT> MenuLevel::RowScreenRect(std::size_t index, bool fullyVisible) const {
    if (!Handle() || index >= rows_.size()) return std::nullopt;
    const auto& row = rows_[index];
    const float top = viewportTop_ + row.top - scroll_.Value(graphics::FrameScheduler::Now());
    const float bottom = top + row.height;
    if (fullyVisible && (top < viewportTop_ - 0.01f || bottom > viewportBottom_ + 0.01f)) return std::nullopt;
    const float padding = controller_.Tokens().menu.padding;
    const RECT bounds = BoundsPx();
    return RECT{bounds.left + Px(panel_.left + padding), bounds.top + Px(top), bounds.left + Px(panel_.right - padding),
                bounds.top + Px(bottom)};
}

std::size_t MenuLevel::RowFromId(int id) const noexcept {
    if (id <= 0) return kNone;
    const auto row = static_cast<std::size_t>(id) - 1;
    return row < rows_.size() && !rows_[row].separator ? row : kNone;
}

accessibility::AccessibleItem MenuLevel::Item(std::size_t index) const {
    accessibility::AccessibleItem item;
    if (index >= accessibleRows_.size()) return item;
    const std::size_t rowIndex = accessibleRows_[index];
    const auto& row = rows_[rowIndex];
    item.id = static_cast<int>(rowIndex) + 1;
    item.name = row.label.text;
    std::wstring help = row.label.accelerator;
    if (row.submenu) help += help.empty() ? L"Opens a submenu" : L", opens a submenu";
    item.helpText = std::move(help);
    if (row.checked) item.itemStatus = row.radio ? L"Selected" : L"Checked";
    item.controlType = UIA_MenuItemControlTypeId;
    std::optional<RECT> visible;
    if (auto bounds = RowScreenRect(rowIndex, false)) {
        // Clip to the visible part of the panel; rows scrolled out are reported offscreen.
        const RECT panel = PanelScreenRect();
        const LONG viewTop = BoundsPx().top + Px(viewportTop_);
        const LONG viewBottom = BoundsPx().top + Px(viewportBottom_);
        RECT clipped{bounds->left, std::max(bounds->top, viewTop), bounds->right, std::min(bounds->bottom, viewBottom)};
        if (clipped.bottom > clipped.top && clipped.right > clipped.left && panel.right > panel.left) visible = clipped;
    }
    item.boundsPx = visible.value_or(RECT{});
    item.isOffscreen = !visible || !IsShown() || closing_;
    item.isEnabled = row.enabled;
    item.isFocusable = row.enabled;
    item.hasKeyboardFocus = !closing_ && rowIndex == selected_;
    item.supportsInvoke = !row.submenu && !closing_;
    item.supportsToggle = row.checked && !row.radio && !row.submenu && !closing_;
    item.toggleState = row.checked ? ToggleState_On : ToggleState_Off;
    item.supportsExpandCollapse = row.submenu && !closing_;
    item.expandCollapseState = row.submenu
        ? (openChild_ == rowIndex ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed)
        : ExpandCollapseState_LeafNode;
    item.supportsSelectionItem = row.radio && !row.submenu && !closing_;
    item.isSelected = row.checked;
    return item;
}

Result<void> MenuLevel::Invoke(int id) {
    const std::size_t row = RowFromId(id);
    if (row == kNone || closing_ || !controller_.Active()) return MakeError(ErrorCode::InvalidState, "menu item is not available");
    if (!rows_[row].Selectable()) return MakeError(ErrorCode::InvalidArgument, "menu item is disabled");
    (void)accessible_.NotifyInvoked(id);
    controller_.Activate(depth_, row, true);
    return Ok();
}

Result<void> MenuLevel::Toggle(int id) { return Invoke(id); }

Result<void> MenuLevel::SetExpanded(int id, bool expanded) {
    const std::size_t row = RowFromId(id);
    if (row == kNone || closing_ || !controller_.Active()) return MakeError(ErrorCode::InvalidState, "menu item is not available");
    if (!rows_[row].Selectable()) return MakeError(ErrorCode::AccessDenied, "menu item is disabled");
    if (!rows_[row].submenu) return MakeError(ErrorCode::Unsupported, "menu item has no submenu");
    controller_.SetExpanded(depth_, row, expanded);
    if ((openChild_ == row) != expanded) return MakeError(ErrorCode::Platform, "submenu could not be presented");
    return Ok();
}

Result<void> MenuLevel::SelectItem(int id) {
    const std::size_t row = RowFromId(id);
    if (row == kNone || !rows_[row].radio) return MakeError(ErrorCode::Unsupported, "menu item is not a radio option");
    return Invoke(id);
}

Result<void> MenuLevel::SetFocus(int id) {
    const std::size_t row = RowFromId(id);
    if (row == kNone || closing_ || !controller_.Active()) return MakeError(ErrorCode::InvalidState, "menu item is not available");
    if (!rows_[row].Selectable()) return MakeError(ErrorCode::InvalidArgument, "menu item is disabled");
    Select(row, true);
    return Ok();
}

void MenuLevel::DrawRow(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, std::size_t index, const D2D1_RECT_F& rect,
                        double now) {
    const auto& tokens = controller_.Tokens();
    const auto& menu = tokens.menu;
    MenuRow& row = rows_[index];
    const float centerY = (rect.top + rect.bottom) / 2.0f;
    if (row.separator) {
        // One-pixel hairline on a pixel center, inset like the row content.
        const float y = ToDip(std::floor(ToPx(centerY)) + 0.5f);
        brush->SetColor(tokens.form.divider.ToD2D());
        dc->DrawLine(D2D1::Point2F(rect.left + menu.paddingX, y), D2D1::Point2F(rect.right - menu.paddingX, y), brush,
                     ToDip(1.0f));
        return;
    }
    float pill = 0.0f;
    if (index == selected_) pill += pill_.Value(now);
    if (index == fadingRow_) pill += fading_.Value(now);
    pill = std::clamp(pill, 0.0f, 1.0f);
    if (pill > 0.001f && row.Selectable()) {
        const graphics::Color fill = tokens.form.navSelected;
        brush->SetColor(fill.WithAlpha(fill.a * pill).ToD2D());
        dc->FillRoundedRectangle(D2D1::RoundedRect(rect, menu.rowRadius, menu.rowRadius), brush);
    }
    const graphics::Color content = row.enabled ? tokens.textPrimary : tokens.textDisabled;
    float x = rect.left + menu.paddingX;
    if (iconColumn_) {
        const float left = x + (menu.iconColumn - kGlyphBox) / 2.0f;
        const D2D1_RECT_F box = D2D1::RectF(left, centerY - kGlyphBox / 2.0f, left + kGlyphBox, centerY + kGlyphBox / 2.0f);
        brush->SetColor(content.ToD2D());
        if (row.checked && row.radio) {
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F((box.left + box.right) / 2.0f, centerY), kRadioDot, kRadioDot), brush);
        } else if (row.checked) {
            graphics::PaintGlyph(dc, graphics::Glyph::Check, box, brush, tokens.glyph.stroke);
        } else if (row.glyph) {
            graphics::PaintGlyph(dc, *row.glyph, box, brush, tokens.glyph.stroke);
        }
        x += menu.iconColumn + menu.columnGap;
    }
    float right = rect.right - menu.paddingX;
    if (row.submenu) {
        const float left = right - menu.submenuColumn + (menu.submenuColumn - kChevronBox) / 2.0f;
        brush->SetColor((row.enabled ? tokens.textSecondary : tokens.textDisabled).ToD2D());
        graphics::PaintGlyph(dc, graphics::Glyph::ChevronRight,
                             D2D1::RectF(left, centerY - kChevronBox / 2.0f, left + kChevronBox, centerY + kChevronBox / 2.0f),
                             brush, tokens.glyph.stroke);
        right -= menu.submenuColumn + menu.columnGap;
    }
    if (row.accelerator) {
        brush->SetColor((row.enabled ? tokens.textSecondary : tokens.textDisabled).ToD2D());
        dc->DrawTextLayout(D2D1::Point2F(right - row.acceleratorWidth, centerY - row.acceleratorHeight / 2.0f),
                           row.accelerator.get(), brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
    if (row.text) {
        const bool underline = controller_.CuesVisible() && row.label.mnemonicIndex != std::wstring::npos;
        if (underline != row.underlined) {
            row.text->SetUnderline(underline ? TRUE : FALSE,
                                   DWRITE_TEXT_RANGE{static_cast<UINT32>(underline ? row.label.mnemonicIndex : 0),
                                                     underline ? 1u : static_cast<UINT32>(row.label.text.size())});
            row.underlined = underline;
        }
        brush->SetColor(content.ToD2D());
        dc->DrawTextLayout(D2D1::Point2F(x, centerY - row.textHeight / 2.0f), row.text.get(), brush,
                           D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
}

void MenuLevel::DrawArrows(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, double now) {
    const auto& tokens = controller_.Tokens();
    const float centerX = (panel_.left + panel_.right) / 2.0f;
    const float up = upArrow_.Value(now);
    const float down = downArrow_.Value(now);
    const graphics::Color color = tokens.textSecondary;
    if (up > 0.001f) {
        const float y = (panel_.top + viewportTop_) / 2.0f;
        brush->SetColor(color.WithAlpha(color.a * up).ToD2D());
        graphics::PaintGlyph(dc, graphics::Glyph::ChevronUp,
                             D2D1::RectF(centerX - kArrowBox / 2.0f, y - kArrowBox / 2.0f, centerX + kArrowBox / 2.0f,
                                         y + kArrowBox / 2.0f),
                             brush, tokens.glyph.stroke);
    }
    if (down > 0.001f) {
        const float y = (viewportBottom_ + panel_.bottom) / 2.0f;
        brush->SetColor(color.WithAlpha(color.a * down).ToD2D());
        graphics::PaintGlyph(dc, graphics::Glyph::ChevronDown,
                             D2D1::RectF(centerX - kArrowBox / 2.0f, y - kArrowBox / 2.0f, centerX + kArrowBox / 2.0f,
                                         y + kArrowBox / 2.0f),
                             brush, tokens.glyph.stroke);
    }
}

void MenuLevel::OnRender(const RenderContext& context) {
    controller_.Rendered(depth_);
    auto* dc = context.d2d;
    if (!dc) return;
    const auto& tokens = controller_.Tokens();
    const auto& menu = tokens.menu;
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    graphics::GlassStyle style;
    style.cornerRadius = menu.radius;
    style.opacity = kPanelOpacity;
    style.neutral = true;
    style.highlight = false;
    style.elevation = graphics::Elevation::Popup;
    style.backdropBlur = BackdropBlurActive();
    graphics::PaintGlassPanel(dc, panel_, tokens, style);

    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(tokens.textPrimary.ToD2D(), brush.put())) || !brush) return;
    const double now = context.now;
    const float scroll = scroll_.Value(now);
    const D2D1_RECT_F viewport = D2D1::RectF(panel_.left, viewportTop_, panel_.right, viewportBottom_);
    dc->PushAxisAlignedClip(viewport, D2D1_ANTIALIAS_MODE_ALIASED);
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        const float top = viewportTop_ + rows_[index].top - scroll;
        const float bottom = top + rows_[index].height;
        if (bottom <= viewportTop_ || top >= viewportBottom_) continue;
        DrawRow(dc, brush.get(), index, D2D1::RectF(panel_.left + menu.padding, top, panel_.right - menu.padding, bottom), now);
    }
    dc->PopAxisAlignedClip();
    if (overflow_) DrawArrows(dc, brush.get(), now);
}

bool MenuLevel::OnAnimate(double now) {
    return pill_.IsAnimating(now) || fading_.IsAnimating(now) || scroll_.IsAnimating(now) || upArrow_.IsAnimating(now) ||
           downArrow_.IsAnimating(now);
}

std::optional<LRESULT> MenuLevel::OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_GETOBJECT: {
            LRESULT result = 0;
            if (connected_ && accessible_.HandleGetObject(Handle(), wParam, lParam, result)) return result;
            break;
        }
        case WM_NCHITTEST:
            // A fading level never takes the pointer.
            if (closing_) return static_cast<LRESULT>(HTTRANSPARENT);
            break;
        case WM_CAPTURECHANGED:
            if (depth_ == 0 && !closing_) controller_.OnRootCaptureLost(reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_SHOWWINDOW:
            if (wParam == FALSE && closing_) controller_.RequestRelease();
            break;
        default: break;
    }
    return std::nullopt;
}

void MenuLevel::OnDpiChanged(UINT newDpi, const RECT& suggestedPx) {
    SurfaceWindow::OnDpiChanged(newDpi, suggestedPx);
    // The layout belongs to the old DPI; like a display change this ends the menu.
    if (!closing_) controller_.Dismiss();
}

void MenuLevel::OnDestroying() {
    if (connected_) {
        accessible_.Disconnect();
        connected_ = false;
    }
}

// --- MenuController ---

MenuController::MenuController(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler,
                               const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion,
                               IMenuWindowOps& ops)
    : device_(device), scheduler_(scheduler), tokens_(tokens), motion_(motion), ops_(&ops) {
    text_.Initialize(device_.DWriteFactory(), tokens_.fontFamily, tokens_.displayFontFamily);
}

MenuController::~MenuController() {
    *alive_ = false;
    StopWatchingForeground();
    KillTimers();
    if (open_ && owner_) ::RemoveWindowSubclass(owner_, &OwnerProc, kOwnerSubclassId);
    levels_.clear();
    closing_.clear();
    if (host_) {
        ::SetWindowLongPtrW(host_, GWLP_USERDATA, 0);
        ::DestroyWindow(host_);
        host_ = nullptr;
    }
}

void MenuController::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion) {
    tokens_ = tokens;
    motion_ = motion;
    baseLine_ = 0.0f;
    text_.Initialize(device_.DWriteFactory(), tokens_.fontFamily, tokens_.displayFontFamily);
    // An open menu was laid out with the old tokens; it ends and the next Show uses the new ones.
    Cancel();
    for (auto& level : closing_) level->Invalidate();
}

float MenuController::BaseLineHeight() {
    if (baseLine_ > 0.0f) return baseLine_;
    const auto measured = text_.Measure(L"Ag", LabelStyle(1.0f));
    baseLine_ = measured.height > 0.0f ? measured.height : tokens_.type.body * 1.33f;
    return baseLine_;
}

void MenuController::Rendered(std::size_t depth) noexcept {
    if (!hooks_.rendered) return;
    try {
        hooks_.rendered(depth);
    } catch (...) {
        AETHER_LOG_WARN("ui", "glass menu render hook threw");
    }
}

std::optional<RECT> MenuController::RowBoundsPx(std::size_t depth, std::size_t row) const {
    if (!open_ || depth >= levels_.size()) return std::nullopt;
    return levels_[depth]->RowScreenRect(row, true);
}

void MenuController::Cancel() noexcept {
    if (!open_) return;
    if (fallbackOpen_) {
        ::EndMenu();  // the themed/native fallback is a system menu loop on this thread
        return;
    }
    Dismiss();
}

void MenuController::Dismiss() noexcept {
    if (!open_ || done_) return;
    command_ = 0;
    done_ = true;
    if (host_) ::PostMessageW(host_, WM_NULL, 0, 0);  // wakes a loop waiting for input
}

void MenuController::Choose(std::uint32_t command) noexcept {
    if (!open_ || done_) return;
    command_ = command;
    done_ = true;
    if (host_) ::PostMessageW(host_, WM_NULL, 0, 0);
}

Result<MenuSelection> MenuController::Show(std::span<const MenuEntry> entries, const MenuRequest& request) {
    if (open_) return MakeError(ErrorCode::InvalidState, "a popup menu is already open");
    if (auto valid = ValidateMenu(entries); !valid) return valid.error();
    if (!ops_->IsValidWindow(request.owner)) return MakeError(ErrorCode::InvalidArgument, "popup menu owner is not a window");
    ReleaseClosing();  // a new menu replaces the previous one's fade at once
    last_ = GlassMenuPresenter::Presentation::None;
    const bool highContrast = tokens_.mode == graphics::ThemeMode::HighContrast || platform::HighContrastEnabled();
    if (!highContrast && OwnedByThisThread(request.owner)) {
        const auto alive = alive_;
        auto glass = ShowGlass(entries, request);
        if (!*alive) {
            if (glass) return std::move(*glass);
            return MenuSelection{};
        }
        if (glass) {
            last_ = GlassMenuPresenter::Presentation::Glass;
            return std::move(*glass);
        }
    }
    return ShowFallback(entries, request);
}

Result<MenuSelection> MenuController::ShowFallback(std::span<const MenuEntry> entries, const MenuRequest& request) {
    // High contrast: the themed presenter shows the system-colored native menu. Otherwise the
    // owner-drawn menu, which falls back to the native one by itself.
    ThemedMenuPresenter themed(tokens_, *ops_);
    open_ = true;
    fallbackOpen_ = true;
    struct Reset {
        MenuController* self;
        std::shared_ptr<bool> alive;
        ~Reset() {
            if (!*alive) return;
            self->open_ = false;
            self->fallbackOpen_ = false;
        }
    } reset{this, alive_};
    auto result = themed.Show(entries, request);
    if (*reset.alive) {
        last_ = themed.LastShowWasThemed() ? GlassMenuPresenter::Presentation::Themed
                                           : GlassMenuPresenter::Presentation::Native;
    }
    return result;
}

bool MenuController::EnsureHost() {
    if (host_ && ::IsWindow(host_)) return true;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &MenuController::HostProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kHostClass;
    if (auto registered = platform::RegisterWindowClassOnce(wc); !registered) {
        AETHER_LOG_WARN("ui", "glass menu host class unavailable: {}", registered.error().Describe());
        return false;
    }
    host_ = ::CreateWindowExW(0, kHostClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!host_) {
        AETHER_LOG_WARN("ui", "glass menu host window unavailable ({})", ::GetLastError());
        return false;
    }
    ::SetWindowLongPtrW(host_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    return true;
}

std::optional<Result<MenuSelection>> MenuController::ShowGlass(std::span<const MenuEntry> entries,
                                                               const MenuRequest& request) {
    if (!EnsureHost()) return std::nullopt;
    request_ = request;
    owner_ = request.owner;
    command_ = 0;
    done_ = false;
    armed_ = false;
    altDown_ = false;
    pressSeen_ = false;
    moved_ = false;
    cues_ = request.keyboard || SystemKeyboardCues();
    pendingDepth_ = pendingRow_ = scrollDepth_ = kNone;
    scrollDirection_ = 0;
    if (!::GetCursorPos(&openPointer_)) openPointer_ = POINT{std::numeric_limits<LONG>::min(), 0};
    lastPointer_ = openPointer_;

    auto root = std::make_unique<MenuLevel>(*this, 0);
    std::wstring name = platform::WindowTitle(owner_);
    if (auto built = root->Build(entries, std::move(name), MenuTrackPoint(request)); !built) {
        AETHER_LOG_WARN("ui", "glass menu unavailable ({}); using the themed menu", built.error().Describe());
        return std::nullopt;
    }
    root->LayoutRoot(request);
    if (request.keyboard) root->Select(NextMenuRow(entries, kNone, 1), true, false);
    // The subclass observes the owner losing activation, being destroyed or the display changing.
    if (!::SetWindowSubclass(owner_, &MenuController::OwnerProc, kOwnerSubclassId, reinterpret_cast<DWORD_PTR>(this))) {
        AETHER_LOG_WARN("ui", "glass menu owner could not be observed; using the themed menu");
        return std::nullopt;
    }
    levels_.push_back(std::move(root));
    open_ = true;

    // Foreground rule, as TrackMenuWithForegroundRule: the owner becomes foreground so a click
    // elsewhere (another application) deactivates it and ends the menu.
    MenuSelection selection;
    selection.previousForeground = ops_->Foreground();
    const HWND owner = owner_;
    IMenuWindowOps* ops = ops_;
    const LONG_PTR style = ops->ExtendedStyle(owner);
    const bool noActivate = (style & WS_EX_NOACTIVATE) != 0;
    if (noActivate) ops->SetExtendedStyle(owner, style & ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE));
    (void)ops->SetForeground(owner);  // Windows may refuse; the menu is shown anyway
    // Refused: the owner cannot lose an activation it never had, so a switch to another
    // application is observed directly instead.
    if (ops->Foreground() != owner) WatchForeground();
    armed_ = true;

    const auto alive = alive_;
    {
        // Restores the owner and the thread's input state however the loop ends.
        struct Session {
            MenuController* self;
            std::shared_ptr<bool> alive;
            HWND owner;
            IMenuWindowOps* ops;
            bool noActivate;
            ~Session() {
                if (!*alive) return;
                self->armed_ = false;
                self->StopWatchingForeground();
                self->KillTimers();
                if (const HWND capture = ::GetCapture(); capture && self->IsLevelWindow(capture)) ::ReleaseCapture();
                ::RemoveWindowSubclass(owner, &MenuController::OwnerProc, kOwnerSubclassId);
                self->open_ = false;
                self->CloseAllLevels();
                if (ops->IsValidWindow(owner)) {
                    ops->PostNull(owner);  // lets anything waiting on the owner's queue run before the next input
                    if (noActivate) ops->SetExtendedStyle(owner, ops->ExtendedStyle(owner) | WS_EX_NOACTIVATE);
                }
            }
        } session{this, alive, owner, ops, noActivate};

        MenuLevel& first = *levels_.front();
        first.ShowEntrance();
        // Mouse input of the thread goes to the root level: clicks outside every level are seen
        // (and consumed) by the loop instead of reaching the window underneath.
        ::SetCapture(first.Handle());
        ::PostMessageW(host_, kOpenedMessage, 0, 0);
        RunLoop();
        if (!*alive) return MenuSelection{0, selection.previousForeground};
        selection.command = command_;
    }
    if (!*alive) return MenuSelection{0, selection.previousForeground};
    if (request.restore == ForegroundRestore::Always ||
        (request.restore == ForegroundRestore::WhenDismissed && selection.command == 0)) {
        RestorePreviousForeground(*ops, owner, selection.previousForeground);
    }
    return Result<MenuSelection>(selection);
}

void MenuController::RunLoop() {
    const auto alive = alive_;
    if (platform::MessageLoop* loop = scheduler_.Loop()) {
        // The nested loop keeps servicing the frame timer and dispatcher work (hover and scroll
        // animations, compositor completions) while the menu takes the thread's input.
        const bool finished = loop->RunNested([this, alive] { return !*alive || done_; },
                                              [this, alive](MSG& message) { return *alive && Intercept(message); },
                                              /*runFilters*/ false);
        if (!finished && *alive) {  // WM_QUIT, already re-posted for the outer loop
            command_ = 0;
            done_ = true;
        }
        return;
    }
    while (*alive && !done_) {
        MSG message{};
        const BOOL got = ::GetMessageW(&message, nullptr, 0, 0);
        if (got == 0) ::PostQuitMessage(static_cast<int>(message.wParam));  // for the outer loop
        if (got == 0 || got == -1) {
            command_ = 0;
            done_ = true;
            return;
        }
        if (!Intercept(message)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }
}

bool MenuController::Intercept(MSG& message) {
    const UINT id = message.message;
    const bool keyboard = IsKeyboardMessage(id);
    const bool mouse = !keyboard && IsMouseMessage(id);
    if (!keyboard && !mouse) return false;
    try {
        if (Active()) {
            if (keyboard) OnKey(message);
            else OnMouse(message);
        }
    } catch (...) {
        AETHER_LOG_ERROR("ui", "glass menu input handling failed; closing the menu");
        Dismiss();
    }
    return true;  // the menu owns the thread's keyboard and mouse input while it is open
}

void MenuController::ShowCues() {
    if (cues_) return;
    cues_ = true;
    for (auto& level : levels_) level->Invalidate();
}

void MenuController::OnKey(const MSG& message) {
    MenuLevel* level = ActiveLevel();
    if (!level) return;
    switch (message.message) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const auto key = static_cast<UINT>(message.wParam);
            if (key == VK_MENU) {
                // Alt shows the mnemonic underlines; Alt released without another key closes the menu.
                if ((message.lParam & (1 << 30)) == 0) altDown_ = true;
                ShowCues();
                return;
            }
            altDown_ = false;
            const std::size_t depth = levels_.size() - 1;
            switch (key) {
                case VK_F10:
                    if ((::GetKeyState(VK_SHIFT) & 0x8000) == 0) Dismiss();
                    return;
                case VK_ESCAPE:
                    if (depth > 0) CloseLevelsAbove(depth - 1);
                    else Dismiss();
                    return;
                case VK_LEFT:
                    if (depth > 0) CloseLevelsAbove(depth - 1);
                    return;
                case VK_UP: level->MoveSelection(-1); return;
                case VK_DOWN: level->MoveSelection(1); return;
                case VK_HOME: level->SelectEdge(false); return;
                case VK_END: level->SelectEdge(true); return;
                case VK_PRIOR: level->Page(-1); return;
                case VK_NEXT: level->Page(1); return;
                case VK_RETURN:
                case VK_SPACE:
                    if (level->Selected() != kNone) Activate(depth, level->Selected(), true);
                    return;
                case VK_RIGHT: {
                    const std::size_t row = level->Selected();
                    if (row != kNone && level->Row(row).submenu) OpenSubmenu(depth, row, true);
                    return;
                }
                default: {
                    // Letters become WM_CHAR/WM_SYSCHAR for the mnemonic search.
                    MSG copy = message;
                    ::TranslateMessage(&copy);
                    return;
                }
            }
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (message.wParam == VK_MENU && altDown_) {
                altDown_ = false;
                Dismiss();
            }
            return;
        case WM_CHAR:
        case WM_SYSCHAR:
            altDown_ = false;
            OnMnemonic(static_cast<wchar_t>(message.wParam));
            return;
        default: return;  // dead keys
    }
}

void MenuController::OnMnemonic(wchar_t character) {
    MenuLevel* level = ActiveLevel();
    if (!level || character < 0x20) return;
    const auto rows = MnemonicRows(level->Entries(), character);
    if (rows.empty()) return;
    const std::size_t depth = levels_.size() - 1;
    if (rows.size() == 1) {
        level->Select(rows.front(), true);
        Activate(depth, rows.front(), true);
        return;
    }
    // Repeated mnemonics cycle through their rows.
    std::size_t next = rows.front();
    for (const std::size_t row : rows) {
        if (level->Selected() != kNone && row > level->Selected()) {
            next = row;
            break;
        }
    }
    level->Select(next, true);
}

void MenuController::Activate(std::size_t depth, std::size_t row, bool keyboard) {
    if (!Active() || depth >= levels_.size()) return;
    MenuLevel& level = *levels_[depth];
    if (row >= level.RowCount() || !level.Row(row).Selectable()) return;
    if (level.Row(row).submenu) {
        OpenSubmenu(depth, row, keyboard);
        return;
    }
    Choose(level.Row(row).id);
}

MenuController::Hit MenuController::HitTest(POINT screen) const {
    for (std::size_t depth = levels_.size(); depth-- > 0;) {
        const LevelHit hit = levels_[depth]->HitTest(screen);
        if (hit.kind != HitKind::None) return Hit{depth, hit};
    }
    return {};
}

void MenuController::OnMouse(const MSG& message) {
    const UINT id = message.message;
    POINT screen{GET_X_LPARAM(message.lParam), GET_Y_LPARAM(message.lParam)};
    if (!UsesScreenCoordinates(id) && message.hwnd) ::ClientToScreen(message.hwnd, &screen);
    if (id == WM_MOUSEMOVE || id == WM_NCMOUSEMOVE) {
        if (screen.x == lastPointer_.x && screen.y == lastPointer_.y) return;  // a window appeared under a still pointer
        const POINT previous = lastPointer_;
        lastPointer_ = screen;
        if (!moved_ && (std::abs(screen.x - openPointer_.x) > ::GetSystemMetrics(SM_CXDRAG) ||
                        std::abs(screen.y - openPointer_.y) > ::GetSystemMetrics(SM_CYDRAG))) {
            moved_ = true;
        }
        UpdateHover(previous, false);
        return;
    }
    const Hit hit = HitTest(screen);
    if (id == WM_MOUSEWHEEL) {
        if (hit.depth == kNone) return;
        const float notches = static_cast<float>(GET_WHEEL_DELTA_WPARAM(message.wParam)) / static_cast<float>(WHEEL_DELTA);
        levels_[hit.depth]->ScrollBy(-notches * kWheelRows * tokens_.menu.itemHeight);
        return;
    }
    if (IsButtonDown(id)) {
        if (hit.depth == kNone) {
            Dismiss();  // outside every level: closes the menu and is not delivered anywhere
            return;
        }
        pressSeen_ = true;
        MenuLevel& level = *levels_[hit.depth];
        if (hit.target.kind == HitKind::ScrollUp || hit.target.kind == HitKind::ScrollDown) {
            level.ScrollBy((hit.target.kind == HitKind::ScrollUp ? -0.8f : 0.8f) * level.ViewportHeight());
            return;
        }
        if (hit.target.kind != HitKind::Row || !level.Row(hit.target.row).Selectable()) return;
        if (hit.target.row != level.OpenChild()) CloseLevelsAbove(hit.depth);
        level.Select(hit.target.row, false);
        if (level.Row(hit.target.row).submenu) OpenSubmenu(hit.depth, hit.target.row, false);
        return;
    }
    const bool leftUp = id == WM_LBUTTONUP || id == WM_NCLBUTTONUP;
    const bool rightUp = id == WM_RBUTTONUP || id == WM_NCRBUTTONUP;
    if (!(leftUp || (rightUp && request_.rightButton))) return;
    if (hit.depth == kNone || hit.target.kind != HitKind::Row) return;
    // A release only chooses after a press in the menu or a drag into it, so the release of the
    // click that opened the menu never picks the entry that appeared under the pointer.
    if (!pressSeen_ && !moved_) return;
    Activate(hit.depth, hit.target.row, false);
}

bool MenuController::TowardsSubmenu(POINT previous, POINT current, std::size_t depth) const {
    if (depth + 1 >= levels_.size()) return false;
    const RECT child = levels_[depth + 1]->PanelScreenRect();
    const RECT parent = levels_[depth]->PanelScreenRect();
    const bool right = child.left >= (parent.left + parent.right) / 2;
    if (right ? current.x <= previous.x : current.x >= previous.x) return false;
    const LONG edge = right ? child.left : child.right;
    return InTriangle(current, previous, POINT{edge, child.top}, POINT{edge, child.bottom});
}

void MenuController::UpdateHover(POINT previous, bool forced) {
    const Hit hit = HitTest(lastPointer_);
    if (hit.depth == kNone || (hit.target.kind != HitKind::ScrollUp && hit.target.kind != HitKind::ScrollDown)) StopAutoScroll();
    if (hit.depth == kNone) {
        // Leaving every level clears the hover in the innermost level, unless it opened a submenu.
        if (MenuLevel* level = ActiveLevel(); level && level->OpenChild() == kNone) level->Select(kNone, false);
        CancelSubmenuTimer();
        return;
    }
    MenuLevel& level = *levels_[hit.depth];
    if (hit.target.kind == HitKind::ScrollUp || hit.target.kind == HitKind::ScrollDown) {
        StartAutoScroll(hit.depth, hit.target.kind == HitKind::ScrollUp ? -1 : 1);
        return;
    }
    if (hit.target.kind != HitKind::Row) return;
    // Travelling diagonally across other rows towards the open submenu keeps it open.
    if (!forced && level.OpenChild() != kNone && hit.target.row != level.OpenChild() &&
        TowardsSubmenu(previous, lastPointer_, hit.depth)) {
        ::SetTimer(host_, kIntentTimer, DelayMs(tokens_.menu.submenuDelay), nullptr);
        return;
    }
    ::KillTimer(host_, kIntentTimer);
    if (hit.target.row != level.OpenChild()) CloseLevelsAbove(hit.depth);
    const MenuRow& row = level.Row(hit.target.row);
    level.Select(row.Selectable() ? hit.target.row : kNone, false);
    if (row.Selectable() && row.submenu && level.OpenChild() != hit.target.row) ArmSubmenuTimer(hit.depth, hit.target.row);
    else CancelSubmenuTimer();
}

void MenuController::OpenSubmenu(std::size_t depth, std::size_t row, bool selectFirst) {
    if (!Active() || depth >= levels_.size()) return;
    MenuLevel& parent = *levels_[depth];
    if (row >= parent.RowCount() || !parent.Row(row).Selectable() || !parent.Row(row).submenu) return;
    CancelSubmenuTimer();
    parent.Select(row, selectFirst);
    if (parent.OpenChild() == row && depth + 1 < levels_.size()) {
        if (selectFirst) levels_[depth + 1]->SelectEdge(false);
        return;
    }
    CloseLevelsAbove(depth);
    const auto rowRect = parent.RowScreenRect(row, false);
    if (!rowRect) return;
    const RECT panel = parent.PanelScreenRect();
    // Extended upwards by the panel padding so the first submenu row lines up with this row.
    const RECT beside{panel.left, rowRect->top - parent.PaddingPx(), panel.right, rowRect->bottom};
    const std::span<const MenuEntry> entries(parent.Entries()[row].submenu);
    auto child = std::make_unique<MenuLevel>(*this, depth + 1);
    if (auto built = child->Build(entries, parent.Row(row).label.text, POINT{beside.right, beside.top}); !built) {
        AETHER_LOG_WARN("ui", "glass submenu unavailable: {}", built.error().Describe());
        return;
    }
    child->LayoutSubmenu(beside);
    if (selectFirst) child->Select(NextMenuRow(entries, kNone, 1), true, false);
    levels_.push_back(std::move(child));
    parent.SetOpenChild(row);
    levels_.back()->ShowEntrance();
    ::PostMessageW(host_, kOpenedMessage, depth + 1, 0);
}

void MenuController::CloseLevelsAbove(std::size_t depth) {
    if (depth >= levels_.size()) return;
    bool closed = false;
    while (levels_.size() > depth + 1) {
        std::unique_ptr<MenuLevel> level = std::move(levels_.back());
        levels_.pop_back();
        level->BeginClose();
        closing_.push_back(std::move(level));
        closed = true;
    }
    if (pendingDepth_ != kNone && pendingDepth_ > depth) CancelSubmenuTimer();
    if (scrollDepth_ != kNone && scrollDepth_ > depth) StopAutoScroll();
    if (!closed) return;
    levels_[depth]->SetOpenChild(kNone);
    levels_[depth]->Invalidate();
    levels_[depth]->NotifyFocus();
}

winrt::com_ptr<IRawElementProviderFragment> MenuController::ParentProvider(std::size_t depth) const {
    if (!Active() || depth == 0 || depth >= levels_.size()) return nullptr;
    const auto& parent = levels_[depth - 1];
    const auto row = parent->OpenChild();
    return row == kNone ? nullptr : parent->Provider(row);
}

void MenuController::SetExpanded(std::size_t depth, std::size_t row, bool expanded) {
    if (expanded) OpenSubmenu(depth, row, true);
    else if (depth < levels_.size() && levels_[depth]->OpenChild() == row) CloseLevelsAbove(depth);
}

winrt::com_ptr<IRawElementProviderFragment> MenuController::ChildProvider(std::size_t depth, std::size_t row) const {
    if (!Active() || depth + 1 >= levels_.size() || levels_[depth]->OpenChild() != row) return nullptr;
    return levels_[depth + 1]->Provider();
}

void MenuController::CloseAllLevels() {
    CancelSubmenuTimer();
    StopAutoScroll();
    while (!levels_.empty()) {
        std::unique_ptr<MenuLevel> level = std::move(levels_.back());
        levels_.pop_back();
        level->BeginClose();
        closing_.push_back(std::move(level));
    }
}

void MenuController::ReleaseHidden() {
    std::vector<std::unique_ptr<MenuLevel>> released;
    for (auto it = closing_.begin(); it != closing_.end();) {
        if (!(*it)->Handle() || !(*it)->IsShown()) {
            released.push_back(std::move(*it));
            it = closing_.erase(it);
        } else {
            ++it;
        }
    }
}

void MenuController::ReleaseClosing() noexcept {
    std::vector<std::unique_ptr<MenuLevel>> released = std::move(closing_);
    closing_.clear();
}

void MenuController::ArmSubmenuTimer(std::size_t depth, std::size_t row) {
    if (pendingDepth_ == depth && pendingRow_ == row) return;
    pendingDepth_ = depth;
    pendingRow_ = row;
    ::SetTimer(host_, kSubmenuTimer, DelayMs(tokens_.menu.submenuDelay), nullptr);
}

void MenuController::CancelSubmenuTimer() noexcept {
    pendingDepth_ = pendingRow_ = kNone;
    if (host_) ::KillTimer(host_, kSubmenuTimer);
}

void MenuController::StartAutoScroll(std::size_t depth, int direction) {
    if (scrollDepth_ == depth && scrollDirection_ == direction) return;
    if (depth >= levels_.size() || !levels_[depth]->CanScroll(direction)) {
        StopAutoScroll();
        return;
    }
    scrollDepth_ = depth;
    scrollDirection_ = direction;
    ::SetTimer(host_, kScrollTimer, kScrollRepeatMs, nullptr);
}

void MenuController::StopAutoScroll() noexcept {
    scrollDepth_ = kNone;
    scrollDirection_ = 0;
    if (host_) ::KillTimer(host_, kScrollTimer);
}

void MenuController::KillTimers() noexcept {
    CancelSubmenuTimer();
    StopAutoScroll();
    if (host_) ::KillTimer(host_, kIntentTimer);
}

void MenuController::OnTimer(UINT_PTR id) {
    switch (id) {
        case kSubmenuTimer: {
            const std::size_t depth = pendingDepth_;
            const std::size_t row = pendingRow_;
            CancelSubmenuTimer();
            if (Active() && depth < levels_.size() && levels_[depth]->Selected() == row) OpenSubmenu(depth, row, false);
            return;
        }
        case kIntentTimer:
            ::KillTimer(host_, kIntentTimer);
            if (Active()) UpdateHover(lastPointer_, true);
            return;
        case kScrollTimer:
            if (!Active() || scrollDepth_ >= levels_.size() || !levels_[scrollDepth_]->CanScroll(scrollDirection_)) {
                StopAutoScroll();
                return;
            }
            levels_[scrollDepth_]->ScrollBy(static_cast<float>(scrollDirection_) * tokens_.menu.itemHeight);
            return;
        default: ::KillTimer(host_, id); return;
    }
}

void MenuController::NotifyOpened(std::size_t depth) {
    if (!Active() || depth >= levels_.size() || !hooks_.opened) return;
    const auto opened = hooks_.opened;  // the hook may replace the hooks
    opened(levels_[depth]->Handle(), depth);
}

// The WinEvent callback carries no context; one glass menu at most is open per UI thread.
thread_local MenuController* foregroundWatcher = nullptr;

void MenuController::WatchForeground() noexcept {
    StopWatchingForeground();
    foregroundWatcher = this;
    foregroundHook_ = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                        &MenuController::ForegroundChanged, 0, 0,
                                        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!foregroundHook_) foregroundWatcher = nullptr;
}

void MenuController::StopWatchingForeground() noexcept {
    if (foregroundHook_) ::UnhookWinEvent(foregroundHook_);
    foregroundHook_ = nullptr;
    if (foregroundWatcher == this) foregroundWatcher = nullptr;
}

void CALLBACK MenuController::ForegroundChanged(HWINEVENTHOOK, DWORD event, HWND, LONG, LONG, DWORD, DWORD) {
    // Out of context, delivered through this thread's message loop; another process took the foreground.
    if (event == EVENT_SYSTEM_FOREGROUND && foregroundWatcher) foregroundWatcher->Dismiss();
}

bool MenuController::IsLevelWindow(HWND window) const noexcept {
    if (!window) return false;
    return std::any_of(levels_.begin(), levels_.end(), [window](const auto& level) { return level->Handle() == window; });
}

LRESULT CALLBACK MenuController::HostProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<MenuController*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        try {
            switch (message) {
                case WM_TIMER: self->OnTimer(static_cast<UINT_PTR>(wParam)); return 0;
                case kOpenedMessage: self->NotifyOpened(static_cast<std::size_t>(wParam)); return 0;
                case kReleaseMessage: self->ReleaseHidden(); return 0;
                default: break;
            }
        } catch (...) {
            AETHER_LOG_ERROR("ui", "glass menu host message {} threw", message);
        }
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK MenuController::OwnerProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                           DWORD_PTR reference) {
    auto* self = reinterpret_cast<MenuController*>(reference);
    if (self && self->armed_) {
        bool dismiss = false;
        switch (message) {
            case WM_ACTIVATE: dismiss = LOWORD(wParam) == WA_INACTIVE; break;
            case WM_ACTIVATEAPP: dismiss = wParam == FALSE; break;
            case WM_CANCELMODE:
            case WM_DESTROY:
            case WM_DISPLAYCHANGE:
            case WM_DPICHANGED: dismiss = true; break;
            default: break;
        }
        if (dismiss) {
            self->Dismiss();
            ::PostMessageW(hwnd, WM_NULL, 0, 0);  // wakes the menu loop
        }
    }
    if (message == WM_NCDESTROY) ::RemoveWindowSubclass(hwnd, &MenuController::OwnerProc, id);
    return ::DefSubclassProc(hwnd, message, wParam, lParam);
}

}  // namespace

// --- Pure rules ---

POINT PlaceMenuPanel(SIZE panel, const MenuRequest& request, const RECT& workArea, int anchorGapPx, int edgeMarginPx) noexcept {
    const LONG width = std::max<LONG>(0, panel.cx);
    const LONG height = std::max<LONG>(0, panel.cy);
    const LONG margin = std::max(0, edgeMarginPx);
    const LONG gap = std::max(0, anchorGapPx);
    const LONG left = workArea.left + margin;
    const LONG top = workArea.top + margin;
    const LONG right = workArea.right - margin;
    const LONG bottom = workArea.bottom - margin;
    POINT origin{};
    if (request.placement != MenuPlacement::AtPoint && HasArea(request.anchor)) {
        const RECT& anchor = request.anchor;
        origin.x = anchor.left + (anchor.right - anchor.left) / 2 - width / 2;
        const LONG below = anchor.bottom + gap;
        const LONG above = anchor.top - gap - height;
        const LONG roomBelow = bottom - below;
        const LONG roomAbove = anchor.top - gap - top;
        bool placeBelow = request.placement == MenuPlacement::BelowAnchor;
        if (placeBelow && roomBelow < height) placeBelow = roomAbove >= height ? false : roomBelow >= roomAbove;
        else if (!placeBelow && roomAbove < height) placeBelow = roomBelow >= height ? true : roomBelow > roomAbove;
        if (placeBelow) {
            // The edge margin never pushes the panel onto an anchor that sits at the work-area edge.
            const LONG low = std::min(top, below);
            origin.y = ClampCoordinate(below, low, bottom - height);
        } else {
            const LONG high = std::max(bottom - height, above);
            origin.y = ClampCoordinate(above, top, high);
        }
    } else {
        origin = request.point;
        // Like TrackPopupMenu: flip to the left of / above the point when there is no room.
        if (origin.x + width > right && request.point.x - width >= left) origin.x = request.point.x - width;
        if (origin.y + height > bottom && request.point.y - height >= top) origin.y = request.point.y - height;
        origin.y = ClampCoordinate(origin.y, top, bottom - height);
    }
    origin.x = ClampCoordinate(origin.x, left, right - width);
    return origin;
}

POINT PlaceSubmenuPanel(SIZE panel, const RECT& row, const RECT& workArea, int overlapPx, int edgeMarginPx) noexcept {
    const LONG width = std::max<LONG>(0, panel.cx);
    const LONG height = std::max<LONG>(0, panel.cy);
    const LONG margin = std::max(0, edgeMarginPx);
    const LONG overlap = std::max(0, overlapPx);
    const LONG left = workArea.left + margin;
    const LONG top = workArea.top + margin;
    const LONG right = workArea.right - margin;
    const LONG bottom = workArea.bottom - margin;
    const LONG rightX = row.right - overlap;
    const LONG leftX = row.left + overlap - width;
    const LONG roomRight = right - rightX;
    const LONG roomLeft = row.left + overlap - left;
    POINT origin{};
    if (roomRight >= width) origin.x = rightX;
    else if (roomLeft >= width) origin.x = leftX;
    else origin.x = roomRight >= roomLeft ? rightX : leftX;
    origin.x = ClampCoordinate(origin.x, left, right - width);
    origin.y = ClampCoordinate(row.top, top, bottom - height);
    return origin;
}

float MenuPanelWidthDip(float contentDip, int minimumWidthPx, UINT dpi, const graphics::MenuTokens& menu,
                        float availableDip) noexcept {
    const float scale = static_cast<float>(dpi >= 48 && dpi <= 960 ? dpi : 96u) / 96.0f;
    float width = std::isfinite(menu.minWidth) ? std::max(0.0f, menu.minWidth) : 0.0f;
    if (std::isfinite(contentDip)) width = std::max(width, contentDip);
    if (minimumWidthPx > 0) width = std::max(width, static_cast<float>(minimumWidthPx) / scale);
    if (std::isfinite(menu.maxWidth) && menu.maxWidth > 0.0f) width = std::min(width, menu.maxWidth);
    if (std::isfinite(availableDip) && availableDip > 0.0f) width = std::min(width, availableDip);
    return std::max(0.0f, std::ceil(width - 0.01f));
}

std::size_t NextMenuRow(std::span<const MenuEntry> entries, std::size_t from, int direction) noexcept {
    const std::size_t count = entries.size();
    if (count == 0 || direction == 0) return kNone;
    const bool forward = direction > 0;
    std::size_t index = 0;
    if (from >= count) index = forward ? 0 : count - 1;
    else index = forward ? (from + 1) % count : (from + count - 1) % count;
    for (std::size_t step = 0; step < count; ++step) {
        const MenuEntry& entry = entries[index];
        if (!entry.separator && entry.enabled) return index;
        index = forward ? (index + 1) % count : (index + count - 1) % count;
    }
    return kNone;
}

std::vector<std::size_t> MnemonicRows(std::span<const MenuEntry> entries, wchar_t letter) {
    std::vector<std::size_t> rows;
    if (letter == 0) return rows;
    const wchar_t wanted = FoldCase(letter);
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const MenuEntry& entry = entries[index];
        if (entry.separator || !entry.enabled) continue;
        const wchar_t mnemonic = ParseMenuLabel(entry.text).mnemonic;
        if (mnemonic != 0 && FoldCase(mnemonic) == wanted) rows.push_back(index);
    }
    return rows;
}

// --- GlassMenuPresenter ---

struct GlassMenuPresenter::Impl final : MenuController {
    using MenuController::MenuController;
};

GlassMenuPresenter::GlassMenuPresenter(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler,
                                       const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion,
                                       IMenuWindowOps& ops)
    : impl_(std::make_unique<Impl>(device, scheduler, tokens, motion, ops)) {}

GlassMenuPresenter::~GlassMenuPresenter() = default;

void GlassMenuPresenter::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion) {
    impl_->SetTheme(tokens, motion);
}

Result<MenuSelection> GlassMenuPresenter::Show(std::span<const MenuEntry> entries, const MenuRequest& request) {
    try {
        return impl_->Show(entries, request);
    } catch (...) {
        return MakeError(ErrorCode::Internal, "popup menu could not be shown");
    }
}

void GlassMenuPresenter::Cancel() noexcept { impl_->Cancel(); }

bool GlassMenuPresenter::IsOpen() const noexcept { return impl_->IsOpen(); }

GlassMenuPresenter::Presentation GlassMenuPresenter::LastPresentation() const noexcept { return impl_->LastPresentation(); }

void GlassMenuPresenter::SetTestHooks(TestHooks hooks) { impl_->SetTestHooks(std::move(hooks)); }

std::optional<RECT> GlassMenuPresenter::RowBoundsPx(std::size_t depth, std::size_t row) const {
    return impl_->RowBoundsPx(depth, row);
}

}  // namespace aether::ui
