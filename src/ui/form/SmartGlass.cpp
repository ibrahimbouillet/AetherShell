// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "ui/form/SmartGlass.h"

#include "core/WorkerPool.h"
#include "diagnostics/Logger.h"
#include "graphics/GraphicsDevice.h"
#include "graphics/WinRT.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/WindowUtilities.h"

#include <dwmapi.h>
#include <exdisp.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.graphics.effects.interop.h>
#include <winrt/Windows.Graphics.Effects.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string_view>

namespace aether::ui::form {
namespace {

namespace wuc = graphics::wuc;
namespace abi_wuc = graphics::abi_wuc;
namespace wge = winrt::Windows::Graphics::Effects;
namespace abi_wge = ABI::Windows::Graphics::Effects;
using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::Foundation::Numerics::float3;

constexpr wchar_t kBackdropClass[] = L"AetherShell.SmartGlass";
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT kRefreshMs = 100;                    // the mask follows scrolling and the windows behind
constexpr auto kIconsEvery = std::chrono::seconds(3);   // the desktop's icons move rarely
constexpr auto kMoveRefresh = std::chrono::milliseconds(40);
constexpr DWORD kDwmUseHostBackdropBrush = 17;      // DWMWA_USE_HOSTBACKDROPBRUSH (Windows 11)
constexpr int kCellDip = 8;
constexpr int kMarginDip = 12;
constexpr std::size_t kMaxRects = 4096;
constexpr int kSoftPasses = 3;   // the gradient at the blur's edges, in cells

// D2D1 AlphaMask: the first source where the second one's alpha says (CLSID_D2D1AlphaMask, spelled out for SDKs that
// guard it).
constexpr GUID kAlphaMaskEffect{0xc80ecff0, 0x3fd5, 0x4f05, {0x83, 0x28, 0xc5, 0xd1, 0x72, 0x4b, 0x4f, 0x0a}};

struct AlphaMaskEffect : winrt::implements<AlphaMaskEffect, wge::IGraphicsEffect, wge::IGraphicsEffectSource, abi_wge::IGraphicsEffectD2D1Interop> {
    AlphaMaskEffect(wge::IGraphicsEffectSource source, wge::IGraphicsEffectSource mask) : source_(std::move(source)), mask_(std::move(mask)) {}
    winrt::hstring Name() const { return name_; }
    void Name(const winrt::hstring& name) { name_ = name; }
    HRESULT __stdcall GetEffectId(GUID* id) noexcept override {
        if (!id) return E_POINTER;
        *id = kAlphaMaskEffect;
        return S_OK;
    }
    HRESULT __stdcall GetNamedPropertyMapping(LPCWSTR, UINT*, abi_wge::GRAPHICS_EFFECT_PROPERTY_MAPPING*) noexcept override { return E_INVALIDARG; }
    HRESULT __stdcall GetPropertyCount(UINT* count) noexcept override {
        if (!count) return E_POINTER;
        *count = 0;
        return S_OK;
    }
    HRESULT __stdcall GetProperty(UINT, ABI::Windows::Foundation::IPropertyValue**) noexcept override { return E_INVALIDARG; }
    HRESULT __stdcall GetSource(UINT index, abi_wge::IGraphicsEffectSource** source) noexcept override try {
        if (!source) return E_POINTER;
        *source = nullptr;
        if (index > 1) return E_INVALIDARG;
        *source = (index == 0 ? source_ : mask_).as<abi_wge::IGraphicsEffectSource>().detach();
        return S_OK;
    } catch (...) {
        return winrt::to_hresult();
    }
    HRESULT __stdcall GetSourceCount(UINT* count) noexcept override {
        if (!count) return E_POINTER;
        *count = 2;
        return S_OK;
    }

private:
    wge::IGraphicsEffectSource source_{nullptr};
    wge::IGraphicsEffectSource mask_{nullptr};
    winrt::hstring name_;
};

[[nodiscard]] bool Empty(const RECT& r) noexcept { return r.right <= r.left || r.bottom <= r.top; }

// Marks the cells `rect` (client pixels) touches.
void Rasterize(std::vector<std::uint8_t>& grid, int columns, int rows, int cellPx, const RECT& rect) {
    if (Empty(rect)) return;
    const int left = std::clamp(static_cast<int>(rect.left) / cellPx, 0, columns);
    const int top = std::clamp(static_cast<int>(rect.top) / cellPx, 0, rows);
    const int right = std::clamp((static_cast<int>(rect.right) + cellPx - 1) / cellPx, 0, columns);
    const int bottom = std::clamp((static_cast<int>(rect.bottom) + cellPx - 1) / cellPx, 0, rows);
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x) grid[static_cast<std::size_t>(y) * columns + x] = 1;
}

[[nodiscard]] std::uint8_t Level(float value) noexcept {
    return static_cast<std::uint8_t>(std::lround(std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f) * 255.0f));
}

[[nodiscard]] bool ClassIs(HWND window, std::wstring_view name) noexcept {
    wchar_t buffer[64]{};
    const int length = ::GetClassNameW(window, buffer, 64);
    return length > 0 && std::wstring_view(buffer, static_cast<std::size_t>(length)) == name;
}

// What the backdrop window's procedure calls on its timer (SmartGlass::Impl is private to the class).
struct TimerTarget {
    virtual void OnRefreshTimer() = 0;

protected:
    ~TimerTarget() = default;
};

LRESULT CALLBACK BackdropProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

ATOM RegisterBackdropClass() {
    static const ATOM atom = [] {
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.lpfnWndProc = &BackdropProc;
        cls.hInstance = ::GetModuleHandleW(nullptr);
        cls.lpszClassName = kBackdropClass;
        return ::RegisterClassExW(&cls);
    }();
    return atom;
}

}  // namespace

SmartMask ComputeSmartMask(int width, int height, int cellPx, std::span<const RECT> own, std::span<const RECT> busy, int marginPx,
                           SmartGlassLevels levels) {
    SmartMask mask;
    if (width <= 0 || height <= 0 || cellPx <= 0) return mask;
    mask.cellPx = cellPx;
    mask.columns = (width + cellPx - 1) / cellPx;
    mask.rows = (height + cellPx - 1) / cellPx;
    const std::size_t count = static_cast<std::size_t>(mask.columns) * static_cast<std::size_t>(mask.rows);
    std::vector<std::uint8_t> ownCells(count, 0), busyCells(count, 0);
    const RECT client{0, 0, width, height};
    const int margin = std::max(0, marginPx);
    for (const auto& rect : own.first(std::min(own.size(), kMaxRects))) {
        RECT wide{rect.left - margin, rect.top - margin, rect.right + margin, rect.bottom + margin}, clipped{};
        if (!Empty(rect) && ::IntersectRect(&clipped, &wide, &client)) Rasterize(ownCells, mask.columns, mask.rows, cellPx, clipped);
    }
    for (const auto& rect : busy.first(std::min(busy.size(), kMaxRects))) {
        RECT clipped{};
        if (::IntersectRect(&clipped, &rect, &client)) Rasterize(busyCells, mask.columns, mask.rows, cellPx, clipped);
    }
    const std::uint8_t busyLevel = Level(levels.busy), quietLevel = Level(levels.quiet);
    std::vector<std::uint8_t> raw(count, 0);
    for (std::size_t i = 0; i < count; ++i) raw[i] = ownCells[i] ? (busyCells[i] ? busyLevel : quietLevel) : 0;
    // A gradient at the edges: each pass makes every cell at least the average of its 3x3 neighbourhood, so the blur
    // fades out over kSoftPasses cells (and linear filtering smooths each cell into the next).
    mask.cells = std::move(raw);
    std::vector<std::uint8_t> previous;
    for (int pass = 0; pass < kSoftPasses; ++pass) {
        previous = mask.cells;
        for (int y = 0; y < mask.rows; ++y) {
            for (int x = 0; x < mask.columns; ++x) {
                int sum = 0, n = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= mask.columns || ny >= mask.rows) continue;
                        sum += previous[static_cast<std::size_t>(ny) * mask.columns + nx];
                        ++n;
                    }
                }
                auto& cell = mask.cells[static_cast<std::size_t>(y) * mask.columns + x];
                cell = std::max<std::uint8_t>(cell, static_cast<std::uint8_t>(sum / std::max(1, n)));
            }
        }
    }
    return mask;
}

std::vector<RECT> WindowsBehind(HWND window) {
    std::vector<RECT> behind;
    try {
        RECT bounds{};
        if (!window || !::GetWindowRect(window, &bounds)) return behind;
        int visited = 0;
        for (HWND other = ::GetWindow(window, GW_HWNDNEXT); other && visited < 1024; other = ::GetWindow(other, GW_HWNDNEXT), ++visited) {
            if (!::IsWindowVisible(other) || ::IsIconic(other)) continue;
            if (ClassIs(other, L"Progman") || ClassIs(other, L"WorkerW")) continue;   // the desktop: its icons count apart
            // Click-through overlays show nothing of their own to read: this blur's own window, the corners Aether
            // lays over maximized windows, other programs' highlights. Aether's other windows (weather, search) count.
            if (::GetWindowLongPtrW(other, GWL_EXSTYLE) & WS_EX_TRANSPARENT) continue;
            if (platform::IsWindowCloaked(other)) continue;
            RECT frame{};
            if (FAILED(::DwmGetWindowAttribute(other, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) ::GetWindowRect(other, &frame);
            RECT hit{};
            if (::IntersectRect(&hit, &frame, &bounds)) behind.push_back(hit);
        }
    } catch (...) {
    }
    return behind;
}

std::vector<RECT> DesktopIconRects() {
    std::vector<RECT> icons;
    try {
        auto windows = winrt::create_instance<IShellWindows>(CLSID_ShellWindows, CLSCTX_LOCAL_SERVER);
        VARIANT location{};
        location.vt = VT_I4;
        location.lVal = CSIDL_DESKTOP;
        VARIANT empty{};
        long handle = 0;
        winrt::com_ptr<IDispatch> dispatch;
        if (windows->FindWindowSW(&location, &empty, SWC_DESKTOP, &handle, SWFO_NEEDDISPATCH, dispatch.put()) != S_OK || !dispatch) return icons;
        winrt::com_ptr<IShellBrowser> browser;
        if (FAILED(dispatch.as<IServiceProvider>()->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(browser.put()))) || !browser) return icons;
        winrt::com_ptr<IShellView> view;
        if (FAILED(browser->QueryActiveShellView(view.put())) || !view) return icons;
        HWND viewWindow = nullptr;
        if (FAILED(view->GetWindow(&viewWindow)) || !viewWindow) return icons;
        // Icons hidden (View > Show desktop icons off): the list is hidden too.
        HWND list = ::FindWindowExW(viewWindow, nullptr, L"SysListView32", nullptr);
        if (!list || !::IsWindowVisible(list)) return icons;
        auto folder = view.as<IFolderView>();
        POINT spacing{};
        if (FAILED(folder->GetSpacing(&spacing)) || spacing.x <= 0 || spacing.y <= 0) return icons;
        int count = 0;
        if (FAILED(folder->ItemCount(SVGIO_ALLVIEW, &count))) return icons;
        count = std::min(count, 2048);
        icons.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            PITEMID_CHILD item = nullptr;
            if (FAILED(folder->Item(i, &item)) || !item) continue;
            POINT at{};
            const HRESULT placed = folder->GetItemPosition(item, &at);
            ::CoTaskMemFree(item);
            if (FAILED(placed)) continue;
            RECT rect{at.x, at.y, at.x + spacing.x, at.y + spacing.y};
            ::MapWindowPoints(list, nullptr, reinterpret_cast<POINT*>(&rect), 2);
            icons.push_back(rect);
        }
    } catch (...) {
        icons.clear();
    }
    return icons;
}

std::vector<RECT> OwnContentRects(HWND form) {
    struct Walk {
        HWND form;
        std::vector<RECT> rects;
    } walk{form, {}};
    if (!form || !::IsWindow(form)) return walk.rects;
    ::EnumChildWindows(form, [](HWND child, LPARAM data) -> BOOL {
        auto& walk = *reinterpret_cast<Walk*>(data);
        try {
            if (walk.rects.size() >= kMaxRects) return FALSE;
            // Shown within the form (whether the form itself is on screen yet or not).
            const auto styled = [](HWND w) { return (::GetWindowLongPtrW(w, GWL_STYLE) & WS_VISIBLE) != 0; };
            for (HWND w = child; w && w != walk.form; w = ::GetParent(w))
                if (!styled(w)) return TRUE;
            for (HWND inner = ::GetWindow(child, GW_CHILD); inner; inner = ::GetWindow(inner, GW_HWNDNEXT))
                if (styled(inner)) return TRUE;   // a container: its own children are the content
            if (ClassIs(child, L"Static") && ::GetWindowTextLengthW(child) == 0) return TRUE;
            RECT rect{};
            if (!::GetWindowRect(child, &rect)) return TRUE;
            // Clipped by every window that holds it (a scrolled page shows only what its viewport shows).
            for (HWND parent = ::GetParent(child); parent && parent != walk.form; parent = ::GetParent(parent)) {
                RECT client{};
                ::GetClientRect(parent, &client);
                ::MapWindowPoints(parent, nullptr, reinterpret_cast<POINT*>(&client), 2);
                if (!::IntersectRect(&rect, &rect, &client)) return TRUE;
            }
            ::MapWindowPoints(nullptr, walk.form, reinterpret_cast<POINT*>(&rect), 2);
            if (!Empty(rect)) walk.rects.push_back(rect);
        } catch (...) {
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&walk));
    return walk.rects;
}

struct SmartGlass::Impl final : TimerTarget {
    graphics::GraphicsDevice& device;
    HWND form = nullptr;
    std::function<std::vector<RECT>()> extraContent;
    HWND backdrop = nullptr;
    wuc::Desktop::DesktopWindowTarget target{nullptr};
    wuc::ContainerVisual root{nullptr};
    wuc::SpriteVisual blur{nullptr};
    wuc::SpriteVisual tint{nullptr};
    wuc::CompositionColorBrush tintColor{nullptr};
    wuc::CompositionDrawingSurface maskSurface{nullptr};
    wuc::CompositionSurfaceBrush maskBrush{nullptr};
    SmartMask mask;
    bool maskDrawn = false;
    graphics::Color tintValue{};
    float tintAlpha = 0.0f;
    RECT placed{};
    bool shown = false;
    std::chrono::steady_clock::time_point lastRefresh{};
    bool refreshQueued = false;
    // The desktop's icons, read on their own thread every few seconds.
    WorkerPool iconWorker{{1, L"Aether smart glass", ComApartment::MultiThreaded}};
    std::mutex iconMutex;
    std::vector<RECT> icons;
    std::atomic<bool> iconQuery = false;
    std::chrono::steady_clock::time_point lastIcons{};
    winrt::com_ptr<IVirtualDesktopManager> desktops;
    Connection deviceReplaced;

    Impl(graphics::GraphicsDevice& graphicsDevice, HWND window, std::function<std::vector<RECT>()> extra)
        : device(graphicsDevice), form(window), extraContent(std::move(extra)) {}

    ~Impl() {
        iconWorker.Shutdown();   // joins a running query before the members it writes go
        if (backdrop && ::IsWindow(backdrop)) {
            ::KillTimer(backdrop, kRefreshTimer);
            ::SetWindowLongPtrW(backdrop, GWLP_USERDATA, 0);
            try {
                target = nullptr;
                root = nullptr;
            } catch (...) {
            }
            ::DestroyWindow(backdrop);
        }
    }

    Result<void> Start() {
        if (backdrop) return Ok();
        if (!device.Capabilities().hostBackdrop) return MakeError(ErrorCode::Unavailable, "no desktop blur on this system");
        if (!RegisterBackdropClass()) return MakeError(ErrorCode::Platform, "smart glass class");
        backdrop = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP, kBackdropClass, L"",
                                     WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        if (!backdrop) return MakeError(ErrorCode::Platform, "smart glass window");
        ::SetWindowLongPtrW(backdrop, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(static_cast<TimerTarget*>(this)));
        const BOOL enable = TRUE;
        if (FAILED(::DwmSetWindowAttribute(backdrop, kDwmUseHostBackdropBrush, &enable, sizeof(enable))))
            return Fail("DWMWA_USE_HOSTBACKDROPBRUSH rejected");
        try {
            const auto& compositor = device.Compositor();
            auto interop = compositor.as<abi_wuc::Desktop::ICompositorDesktopInterop>();
            winrt::check_hresult(interop->CreateDesktopWindowTarget(
                backdrop, FALSE, reinterpret_cast<abi_wuc::Desktop::IDesktopWindowTarget**>(winrt::put_abi(target))));
            root = compositor.CreateContainerVisual();
            target.Root(root);
            maskSurface = device.CompositionDevice().CreateDrawingSurface(
                winrt::Windows::Foundation::Size{1.0f, 1.0f}, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
            maskBrush = compositor.CreateSurfaceBrush(maskSurface);
            maskBrush.Stretch(wuc::CompositionStretch::Fill);
            maskBrush.BitmapInterpolationMode(wuc::CompositionBitmapInterpolationMode::Linear);
            const auto masked = [&](const wuc::CompositionBrush& source) {
                auto effect = winrt::make<AlphaMaskEffect>(wuc::CompositionEffectSourceParameter(L"source"),
                                                           wuc::CompositionEffectSourceParameter(L"mask"));
                auto brush = compositor.CreateEffectFactory(effect).CreateBrush();
                brush.SetSourceParameter(L"source", source);
                brush.SetSourceParameter(L"mask", maskBrush);
                return brush;
            };
            blur = compositor.CreateSpriteVisual();
            blur.Brush(masked(compositor.CreateHostBackdropBrush()));
            root.Children().InsertAtTop(blur);
            try {   // the tint is a bonus: the blur alone already keeps text off text
                tintColor = compositor.CreateColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0});
                tint = compositor.CreateSpriteVisual();
                tint.Brush(masked(tintColor));
                root.Children().InsertAtTop(tint);
            } catch (const winrt::hresult_error& error) {
                AETHER_LOG_WARN("ui_form", "smart glass tint refused (0x{:08X})", static_cast<unsigned>(error.code().value));
                tint = nullptr;
                tintColor = nullptr;
            }
        } catch (const winrt::hresult_error& error) {
            return Fail("composition refused", error.code().value);
        }
        ApplyTint();
        deviceReplaced = device.DeviceReplaced.Connect([this] { maskDrawn = false; Refresh(true); });
        ::SetTimer(backdrop, kRefreshTimer, kRefreshMs, nullptr);
        AETHER_LOG_INFO("ui_form", "smart glass started");
        Refresh(true);
        return Ok();
    }

    Result<void> Fail(const char* what, HRESULT hr = E_FAIL) {
        AETHER_LOG_WARN("ui_form", "smart glass unavailable: {} (0x{:08X})", what, static_cast<unsigned>(hr));
        try {
            target = nullptr;
            root = nullptr;
            blur = nullptr;
            tint = nullptr;
        } catch (...) {
        }
        if (backdrop) {
            ::SetWindowLongPtrW(backdrop, GWLP_USERDATA, 0);
            ::DestroyWindow(backdrop);
            backdrop = nullptr;
        }
        return MakeError(ErrorCode::Unavailable, what);
    }

    void ApplyTint() {
        if (!tintColor) return;
        const auto channel = [](float value) { return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };
        try {
            tintColor.Color(winrt::Windows::UI::Color{channel(tintAlpha), channel(tintValue.r), channel(tintValue.g), channel(tintValue.b)});
        } catch (...) {
        }
    }

    // The form's client area on screen, or false while the form is not to be seen.
    bool FormClient(RECT& client) const {
        if (!form || !::IsWindow(form) || !::IsWindowVisible(form) || ::IsIconic(form) || platform::IsWindowCloaked(form)) return false;
        if (!::GetClientRect(form, &client) || Empty(client)) return false;
        ::MapWindowPoints(form, nullptr, reinterpret_cast<POINT*>(&client), 2);
        return true;
    }

    void Hide() {
        if (!backdrop || !shown) return;
        ::ShowWindow(backdrop, SW_HIDE);
        shown = false;
    }

    // The backdrop exactly under the form's client area, directly below the form.
    void Follow(const RECT* clientOverride = nullptr) {
        if (!backdrop) return;
        RECT client{};
        if (clientOverride) client = *clientOverride;
        else if (!FormClient(client)) {
            Hide();
            return;
        }
        const bool moved = !::EqualRect(&client, &placed);
        const bool restack = ::GetWindow(form, GW_HWNDNEXT) != backdrop;
        if (!moved && !restack && shown) return;
        UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW;
        if (!moved) flags |= SWP_NOMOVE | SWP_NOSIZE;
        ::SetWindowPos(backdrop, form, client.left, client.top, client.right - client.left, client.bottom - client.top, flags);
        placed = client;
        shown = true;
    }

    void QueryIcons() {
        const auto now = std::chrono::steady_clock::now();
        if (iconQuery || now - lastIcons < kIconsEvery) return;
        iconQuery = true;
        lastIcons = now;
        if (!iconWorker.Post([this] {
                auto found = DesktopIconRects();
                std::lock_guard lock(iconMutex);
                icons = std::move(found);
                iconQuery = false;
            }))
            iconQuery = false;
    }

    void Refresh(bool force = false) {
        if (!backdrop || !root) return;
        RECT client{};
        if (!FormClient(client)) {
            Hide();
            return;
        }
        Follow();
        FollowDesktop();
        lastRefresh = std::chrono::steady_clock::now();
        QueryIcons();
        try {
            const float scale = static_cast<float>(platform::DpiForWindow(form)) / 96.0f;
            const int cell = std::max(4, static_cast<int>(std::lround(kCellDip * scale)));
            const int margin = static_cast<int>(std::lround(kMarginDip * scale));
            auto own = OwnContentRects(form);
            if (extraContent) {
                auto extra = extraContent();
                own.insert(own.end(), extra.begin(), extra.end());
            }
            auto busy = WindowsBehind(form);
            {
                std::lock_guard lock(iconMutex);
                busy.insert(busy.end(), icons.begin(), icons.end());
            }
            // Behind is in screen pixels; the mask is in the client's.
            for (auto& rect : busy) ::OffsetRect(&rect, -client.left, -client.top);
            auto next = ComputeSmartMask(client.right - client.left, client.bottom - client.top, cell, own, busy, margin);
            if (!force && maskDrawn && next == mask) return;
            mask = std::move(next);
            Draw();
        } catch (...) {
            AETHER_LOG_WARN("ui_form", "smart glass mask could not be computed");
        }
    }

    void Draw() {
        if (mask.columns <= 0 || mask.rows <= 0 || !maskSurface) return;
        try {
            auto interop = maskSurface.as<abi_wuc::ICompositionDrawingSurfaceInterop>();
            const auto size = maskSurface.SizeInt32();
            if (size.Width != mask.columns || size.Height != mask.rows) winrt::check_hresult(interop->Resize(SIZE{mask.columns, mask.rows}));
            winrt::com_ptr<ID2D1DeviceContext> context;
            POINT offset{};
            const HRESULT begun = interop->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), context.put_void(), &offset);
            if (FAILED(begun)) {
                if (graphics::GraphicsDevice::IsDeviceLostError(begun)) device.RequestRecovery();
                maskDrawn = false;
                return;
            }
            std::vector<std::uint32_t> pixels(mask.cells.size());
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                const std::uint32_t v = mask.cells[i];
                pixels[i] = (v << 24) | (v << 16) | (v << 8) | v;   // white, premultiplied
            }
            winrt::com_ptr<ID2D1Bitmap1> bitmap;
            const auto properties =
                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            HRESULT hr = context->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(mask.columns), static_cast<UINT32>(mask.rows)), pixels.data(),
                                               static_cast<UINT32>(mask.columns) * 4, &properties, bitmap.put());
            context->Clear(D2D1::ColorF(0, 0, 0, 0));
            if (SUCCEEDED(hr)) {
                const auto at = D2D1::RectF(static_cast<float>(offset.x), static_cast<float>(offset.y),
                                            static_cast<float>(offset.x + mask.columns), static_cast<float>(offset.y + mask.rows));
                context->DrawBitmap(bitmap.get(), &at, 1.0f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, nullptr, nullptr);
            }
            hr = interop->EndDraw();
            if (FAILED(hr)) {
                if (graphics::GraphicsDevice::IsDeviceLostError(hr)) device.RequestRecovery();
                maskDrawn = false;
                return;
            }
            // One mask pixel per cell: the visuals span whole cells (a little past the client; the window clips them).
            const float2 size2{static_cast<float>(mask.columns * mask.cellPx), static_cast<float>(mask.rows * mask.cellPx)};
            blur.Size(size2);
            if (tint) tint.Size(size2);
            maskDrawn = true;
        } catch (const winrt::hresult_error& error) {
            maskDrawn = false;
            if (graphics::GraphicsDevice::IsDeviceLostError(error.code())) device.RequestRecovery();
        }
    }

    void OnRefreshTimer() override { Refresh(); }

    // A form moved to another virtual desktop takes its backdrop along (a new window starts on the current one).
    void FollowDesktop() {
        try {
            if (!desktops) desktops = winrt::create_instance<IVirtualDesktopManager>(CLSID_VirtualDesktopManager);
            BOOL here = TRUE;
            if (FAILED(desktops->IsWindowOnCurrentVirtualDesktop(backdrop, &here)) || here) return;
            GUID id{};
            if (SUCCEEDED(desktops->GetWindowDesktopId(form, &id))) (void)desktops->MoveWindowToDesktop(backdrop, id);
        } catch (...) {
        }
    }

    void OnFormMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        if (!backdrop) return;
        switch (message) {
            case WM_WINDOWPOSCHANGING: {
                // Moves with the form, not a frame behind it: the new client area from the new window rectangle.
                const auto* pos = reinterpret_cast<const WINDOWPOS*>(lParam);
                if (!pos || (pos->flags & SWP_NOMOVE && pos->flags & SWP_NOSIZE) || !shown) return;
                RECT window{}, client{};
                if (!::GetWindowRect(form, &window) || !FormClient(client)) return;
                const int x = pos->flags & SWP_NOMOVE ? window.left : pos->x;
                const int y = pos->flags & SWP_NOMOVE ? window.top : pos->y;
                const int width = pos->flags & SWP_NOSIZE ? window.right - window.left : pos->cx;
                const int height = pos->flags & SWP_NOSIZE ? window.bottom - window.top : pos->cy;
                const RECT next{x + (client.left - window.left), y + (client.top - window.top),
                                x + width - (window.right - client.right), y + height - (window.bottom - client.bottom)};
                if (!Empty(next)) Follow(&next);
                return;
            }
            case WM_WINDOWPOSCHANGED:
            case WM_SIZE:
            case WM_SHOWWINDOW:
            case WM_ACTIVATE:
                if (message == WM_SHOWWINDOW && !wParam) {
                    Hide();
                    return;
                }
                Follow();
                if (std::chrono::steady_clock::now() - lastRefresh >= kMoveRefresh) Refresh();
                return;
            default:
                return;
        }
    }
};

namespace {
LRESULT CALLBACK BackdropProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* target = reinterpret_cast<TimerTarget*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_TIMER:
            if (wParam == kRefreshTimer && target) {
                target->OnRefreshTimer();
                return 0;
            }
            break;
        default: break;
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}
}  // namespace

SmartGlass::SmartGlass(graphics::GraphicsDevice& device, HWND form, std::function<std::vector<RECT>()> extraContent)
    : impl_(std::make_unique<Impl>(device, form, std::move(extraContent))) {}
SmartGlass::~SmartGlass() = default;
Result<void> SmartGlass::Start() { return impl_->Start(); }
bool SmartGlass::Started() const noexcept { return impl_->backdrop != nullptr; }
void SmartGlass::SetTint(graphics::Color tint, float alpha) {
    impl_->tintValue = tint;
    impl_->tintAlpha = std::isfinite(alpha) ? std::clamp(alpha, 0.0f, 1.0f) : 0.0f;
    impl_->ApplyTint();
}
void SmartGlass::OnFormMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        impl_->OnFormMessage(message, wParam, lParam);
    } catch (...) {
    }
}
void SmartGlass::Refresh() { impl_->Refresh(true); }
const SmartMask& SmartGlass::Mask() const noexcept { return impl_->mask; }
HWND SmartGlass::BackdropWindow() const noexcept { return impl_->backdrop; }

}  // namespace aether::ui::form
