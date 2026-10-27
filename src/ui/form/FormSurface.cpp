// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "ui/form/FormSurface.h"

#include "diagnostics/Logger.h"
#include "graphics/FrameScheduler.h"
#include "graphics/GlassPainter.h"
#include "graphics/TextCache.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/OsVersion.h"
#include "platform/windows/WindowUtilities.h"
#include "ui/form/FormInternal.h"
#include "ui/form/FormPaint.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <oleacc.h>
#include <uxtheme.h>
#include <wincodec.h>
#if defined(_M_X64) || defined(_M_IX86)
#include <emmintrin.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <map>
#include <new>
#include <utility>

namespace aether::ui::form {
namespace {

constexpr UINT_PTR kSubclassId = 0x464F524Du;      // "FORM"
constexpr UINT_PTR kAnimationTimer = 0x464F524Du;  // timer id on the form window (no scheduler)
constexpr UINT kTimerIntervalMs = 16;
// Posted once after editors change: their images are refreshed after the queued input is handled.
UINT SnapshotRefreshMessage() noexcept {
    static const UINT message = ::RegisterWindowMessageW(L"AetherShell.Form.RefreshEditorImages");
    return message;
}
// Posted after a scrolling container first paints: its retained layers are prepared while idle.
UINT PrepareLayersMessage() noexcept {
    static const UINT message = ::RegisterWindowMessageW(L"AetherShell.Form.PrepareLayers");
    return message;
}
// DWMWA_SYSTEMBACKDROP_TYPE and its values (dwmapi.h of the Windows 11 SDK); spelled out so the
// module also builds against older SDK headers.
constexpr DWORD kDwmSystemBackdropType = 38;
constexpr DWORD kBackdropAuto = 0;
constexpr DWORD kBackdropNone = 1;
constexpr DWORD kBackdropTransient = 3;
// DWMWA_WINDOW_CORNER_PREFERENCE / DWMWA_BORDER_COLOR (Windows 11): rounded normal windows with a neutral rim.
constexpr DWORD kDwmCornerPreference = 33;
constexpr DWORD kDwmBorderColor = 34;
constexpr DWORD kCornerDefault = 0;
constexpr DWORD kCornerRound = 2;
constexpr DWORD kDwmColorDefault = 0xFFFFFFFFu;
// oleacc.h declares these GUIDs without defining them unless INITGUID is set; keep local copies.
constexpr GUID kClsidAccPropServices = {0xb5f8350b, 0x0548, 0x48b1, {0xa6, 0xee, 0x88, 0xbd, 0x00, 0xb4, 0xa5, 0xe7}};
constexpr GUID kPropIdAccHelp = {0xc831e11f, 0x44db, 0x4a99, {0x97, 0x68, 0xcb, 0x8f, 0x97, 0x8b, 0x72, 0x31}};
constexpr GUID kPropIdAccName = {0x608d3df8, 0x8128, 0x4aa7, {0xa4, 0x28, 0xf5, 0x5e, 0x49, 0x26, 0x72, 0x91}};
// Height of an owner-drawn List row (DIPs).
constexpr float kListRowHeight = 32.0f;
// Native dropdown list height reserved below a closed combo box (DIPs).
constexpr float kComboListHeight = 240.0f;
// Inset of Document/List children inside their well (DIPs).
constexpr float kWellInset = 4.0f;

bool IsRole(ControlRole role, std::initializer_list<ControlRole> roles) noexcept {
    return std::find(roles.begin(), roles.end(), role) != roles.end();
}
// Roles whose window is painted entirely from the scene.
bool SelfPainted(ControlRole role) noexcept {
    return IsRole(role, {ControlRole::Label, ControlRole::Button, ControlRole::Switch, ControlRole::Check,
                         ControlRole::ComboBox, ControlRole::Slider, ControlRole::Navigation, ControlRole::Progress,
                         ControlRole::Container});
}
// Roles that are opaque layered native children inside a well drawn by their parent.
bool Welled(ControlRole role) noexcept { return IsRole(role, {ControlRole::Field, ControlRole::Document, ControlRole::List}); }
// Roles whose window is enlarged by form.focusMargin around the visual frame.
bool Margined(ControlRole role) noexcept {
    return IsRole(role, {ControlRole::Button, ControlRole::ComboBox, ControlRole::Slider});
}

RECT MapRect(HWND from, HWND to, RECT rect) noexcept {
    ::MapWindowPoints(from, to, reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}

RECT ClientIn(HWND window, HWND space) noexcept {
    RECT client{};
    ::GetClientRect(window, &client);
    return MapRect(window, space, client);
}

std::wstring WindowString(HWND hwnd) {
    const int length = ::GetWindowTextLengthW(hwnd);
    if (length <= 0) return {};
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = ::GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(std::max(0, copied)));
    return text;
}

std::wstring ListText(HWND list, int index, UINT lengthMessage, UINT textMessage) {
    const LRESULT length = ::SendMessageW(list, lengthMessage, static_cast<WPARAM>(index), 0);
    if (length == LB_ERR || length <= 0) return {};
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const LRESULT copied = ::SendMessageW(list, textMessage, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(text.data()));
    text.resize(copied == LB_ERR ? 0 : static_cast<std::size_t>(copied));
    return text;
}

int CALLBACK FontFamilyFound(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}

bool FontFamilyInstalled(const wchar_t* family) noexcept {
    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    ::wcsncpy_s(query.lfFaceName, family, _TRUNCATE);
    bool found = false;
    HDC screen = ::GetDC(nullptr);
    if (!screen) return false;
    ::EnumFontFamiliesExW(screen, &query, &FontFamilyFound, reinterpret_cast<LPARAM>(&found), 0);
    ::ReleaseDC(nullptr, screen);
    return found;
}

const wchar_t* GdiFamily(bool display) noexcept {
    static const bool text = FontFamilyInstalled(L"Segoe UI Variable Text");
    static const bool displayFace = FontFamilyInstalled(L"Segoe UI Variable Display");
    if (display && displayFace) return L"Segoe UI Variable Display";
    return text ? L"Segoe UI Variable Text" : L"Segoe UI";
}

winrt::com_ptr<ID2D1Factory1> SharedFactory() {
    // One factory per UI thread keeps the glyph geometry cache (a few factories per thread) small.
    thread_local winrt::com_ptr<ID2D1Factory1> factory;
    if (!factory) {
        D2D1_FACTORY_OPTIONS options{};
        if (FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
                                       factory.put_void()))) {
            factory = nullptr;
        }
    }
    return factory;
}

BYTE AlphaByte(float opacity) noexcept {
    return static_cast<BYTE>(std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255.0f));
}

}  // namespace

namespace detail {

UINT RevealFocusMessage() noexcept {
    static const UINT message = ::RegisterWindowMessageW(L"AetherShell.Form.RevealFocus");
    return message;
}

UINT PolicyChangedMessage() noexcept {
    static const UINT message = ::RegisterWindowMessageW(L"AetherShell.Form.PolicyChanged");
    return message;
}

bool ShowsFocusCues(HWND hwnd) noexcept {
    return (::SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) == 0;
}

bool ShowsAccelerators(HWND hwnd) noexcept {
    return (::SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL) == 0;
}

}  // namespace detail

std::string_view ToString(FormMaterial material) noexcept {
    switch (material) {
        case FormMaterial::Acrylic: return "acrylic";
        case FormMaterial::Solid: return "solid";
        case FormMaterial::Native: return "native";
    }
    return "unknown";
}

FormMaterial ResolveFormMaterial(graphics::ThemeMode mode, bool transparencyEnabled, bool systemBackdropSupported,
                                 bool renderer) noexcept {
    if (mode == graphics::ThemeMode::HighContrast || !renderer) return FormMaterial::Native;
    if (systemBackdropSupported && transparencyEnabled) return FormMaterial::Acrylic;
    return FormMaterial::Solid;
}

bool SystemBackdropSupported() noexcept { return platform::IsBuildOrGreater(22621); }

// --- Implementation -------------------------------------------------------------------------------

struct ControlState {
    HWND hwnd = nullptr;
    ControlSpec spec;
    RECT frame{};
    bool hasFrame = false;
    bool error = false;
    bool hovered = false;
    bool pushed = false;
    int check = 0;
    bool focused = false;
    graphics::Tween hover;
    graphics::Tween pressed;
    graphics::Tween on;
    bool animating = false;  // a tween ran since the last frame; repaint once more when it settles
    // Style bits the surface added/removed (restored exactly on Native and on unregister).
    LONG_PTR addedStyle = 0;
    LONG_PTR removedStyle = 0;
    LONG_PTR addedExStyle = 0;
    LONG_PTR removedExStyle = 0;
    bool themeOverridden = false;
    bool helpPublished = false;
    bool namePublished = false;
    bool bypass = false;  // the surface itself is sending a message that it would otherwise intercept
    bool hotkey = false;  // a HOTKEY Field: painted by the surface (the control ignores WM_CTLCOLOR*)
    // Navigation and List
    std::vector<graphics::Glyph> glyphs;
    bool compact = false;
    int hoverIndex = -1;
    int leaveIndex = -1;         // the row whose hover is fading out
    graphics::Tween rowHover;    // hover amount of hoverIndex
    graphics::Tween rowLeave;    // remaining hover of leaveIndex
    int selected = -2;
    int topIndex = -1;
    graphics::Tween indicator;  // centre of the indicator, DIPs of the list client area
    // Progress
    std::optional<bool> indeterminate;
    std::optional<double> fraction;
    // Container fade
    graphics::Tween fade{1.0f};
    double start = 0.0;  // continuous animations (Busy ring, indeterminate sweep)
    // Motion masking of an opaque layered editor (see FormSurface::MaskEditors): a private image of
    // the editor, premultiplied BGRA of its window rectangle, drawn in the scene while it moves.
    bool masked = false;
    bool snapshotDirty = true;  // the editor changed since its image was taken
    std::vector<std::uint32_t> snapshot;
    SIZE snapshotSize{};
    // Present's cached rendering of a self-painted visual (see DrawCachedSelf). Valid while the
    // control has neither repainted nor been invalidated since, and its size is unchanged.
    winrt::com_ptr<ID2D1Bitmap> visual;
    SIZE visualSize{};
};

struct FormSurface::Impl final : graphics::IFrameClient {
    HWND window = nullptr;
    DWORD thread = 0;
    graphics::DesignTokens tokens;
    graphics::MotionPolicy motion;
    graphics::FrameScheduler* scheduler = nullptr;
    FormMaterial material = FormMaterial::Native;
    // D45: the window is maximized, so DWM rounds and borders nothing; the form draws its own frame.
    bool maximizedFrame = false;
    // The material for the current tokens and frame. A maximized frame paints an opaque body: DWM draws
    // its system material over the whole window rectangle, outside a window region too, so the corners
    // would show material instead of the scene behind the window.
    [[nodiscard]] FormMaterial ResolveMaterial() const noexcept {
        const auto resolved = ResolveFormMaterial(tokens.mode, tokens.transparencyEnabled, SystemBackdropSupported(), renderer);
        return maximizedFrame && resolved == FormMaterial::Acrylic ? FormMaterial::Solid : resolved;
    }
    [[nodiscard]] bool Framed() const noexcept { return maximizedFrame && material != FormMaterial::Native; }
    UINT dpi = platform::kDefaultDpi;
    bool renderer = false;
    bool frameRequested = false;
    bool timerRunning = false;
    bool snapshotTimer = false;  // a refresh message is queued
    bool bufferedPaint = false;
    bool helpUnavailableLogged = false;
    std::shared_ptr<int> life = std::make_shared<int>(0);

    winrt::com_ptr<ID2D1Factory1> factory;
    winrt::com_ptr<IDWriteFactory> dwrite;
    winrt::com_ptr<ID2D1DCRenderTarget> target;
    winrt::com_ptr<ID2D1DeviceContext> context;
    mutable graphics::TextCache text;
    HFONT font = nullptr;
    int lineHeightPx = 16;
    std::map<int, HFONT> nativeFonts;
    HBRUSH fieldBrush = nullptr;
    HBRUSH fieldDisabledBrush = nullptr;
    winrt::com_ptr<IAccPropServices> accProps;

    std::vector<std::unique_ptr<ControlState>> controls;
    std::vector<FormCard> cards;
    std::vector<FormDivider> dividers;
    FormDecorator decorator;
    // Present: one pass over a container and all its registered descendants.
    struct ContainerOverlay {
        HWND container = nullptr;
        RegionOverlay overlay;
        HWND window = nullptr;
    };
    std::vector<ContainerOverlay> overlays;
    bool deep = false;
    PresentStats stats;
    // A move Present is about to apply: the scene is rendered as if `pendingWindow` (and everything
    // inside it) already sat `pendingDy` pixels lower, so the move and the blit can follow back to back.
    HWND pendingWindow = nullptr;
    LONG pendingDy = 0;

    // A premultiplied top-down 32-bit DIB selected into its own memory DC (bounded by the size of the
    // presented container, reused while that size stays the same).
    struct Dib {
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ previous = nullptr;
        std::uint32_t* bits = nullptr;
        SIZE size{};
        Dib() = default;
        Dib(const Dib&) = delete;
        Dib& operator=(const Dib&) = delete;
        ~Dib() { Release(); }
        // True when the buffer had to be (re)created: its pixels are undefined.
        bool Ensure(LONG width, LONG height, bool& created) {
            created = false;
            if (dc && size.cx == width && size.cy == height) return true;
            Release();
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* pixels = nullptr;
            dc = ::CreateCompatibleDC(nullptr);
            bitmap = dc ? ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
            if (!dc || !bitmap || !pixels) {
                Release();
                return false;
            }
            previous = ::SelectObject(dc, bitmap);
            bits = static_cast<std::uint32_t*>(pixels);
            size = SIZE{width, height};
            created = true;
            return true;
        }
        void Release() noexcept {
            if (dc && previous) ::SelectObject(dc, previous);
            if (bitmap) ::DeleteObject(bitmap);
            if (dc) ::DeleteDC(dc);
            dc = nullptr;
            bitmap = nullptr;
            previous = nullptr;
            bits = nullptr;
            size = SIZE{};
        }
    };

    // A software Direct2D target drawing straight into a WIC bitmap's memory (no per-draw copies).
    struct WicLayer {
        winrt::com_ptr<IWICBitmap> bitmap;
        winrt::com_ptr<ID2D1RenderTarget> target;
        winrt::com_ptr<ID2D1DeviceContext> context;
        SIZE size{};
        // False when the layer cannot be created. `created` reports fresh (undefined) pixels.
        bool Ensure(IWICImagingFactory* wic, ID2D1Factory* d2d, LONG width, LONG height, UINT dpi, bool& created) {
            created = false;
            if (context && size.cx == width && size.cy == height) return true;
            bitmap = nullptr;
            target = nullptr;
            context = nullptr;
            size = SIZE{};
            if (!wic || !d2d || width <= 0 || height <= 0) return false;
            if (FAILED(wic->CreateBitmap(static_cast<UINT>(width), static_cast<UINT>(height), GUID_WICPixelFormat32bppPBGRA,
                                         WICBitmapCacheOnLoad, bitmap.put()))) {
                return false;
            }
            const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                                 D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                                 static_cast<float>(dpi), static_cast<float>(dpi));
            if (FAILED(d2d->CreateWicBitmapRenderTarget(bitmap.get(), &properties, target.put()))) {
                bitmap = nullptr;
                return false;
            }
            context = target.try_as<ID2D1DeviceContext>();
            if (!context) {
                bitmap = nullptr;
                target = nullptr;
                return false;
            }
            size = SIZE{width, height};
            created = true;
            return true;
        }
    };

    // Direct pixel access to a WIC layer for the duration of a scope (never while it is drawn to).
    struct LockedPixels {
        winrt::com_ptr<IWICBitmapLock> lock;
        std::uint32_t* bits = nullptr;
        UINT stride = 0;
        explicit LockedPixels(WicLayer& layer) {
            const WICRect all{0, 0, layer.size.cx, layer.size.cy};
            UINT bytes = 0;
            BYTE* data = nullptr;
            if (layer.bitmap && SUCCEEDED(layer.bitmap->Lock(&all, WICBitmapLockRead | WICBitmapLockWrite, lock.put())) &&
                SUCCEEDED(lock->GetStride(&stride)) && SUCCEEDED(lock->GetDataPointer(&bytes, &data))) {
                bits = reinterpret_cast<std::uint32_t*>(data);
            }
        }
        [[nodiscard]] std::uint32_t* Row(LONG y) const noexcept {
            return reinterpret_cast<std::uint32_t*>(reinterpret_cast<BYTE*>(bits) + static_cast<std::size_t>(y) * stride);
        }
    };

    // Present's retained layers of one container (Acrylic): the window material and fixed decoration
    // as they lie under the container (static), and the moving content on transparency. A scroll
    // shifts the content layer and renders only what came into view or changed.
    struct ContainerLayers {
        HWND container = nullptr;
        WicLayer background;
        std::vector<std::uint32_t> content;  // premultiplied content pixels, only ever written by this code
        WicLayer scratch;             // Direct2D draws changed content here; it is copied into `content`
        POINT origin{};               // form-client origin the background was rendered for
        UINT dpi = 0;
        bool backgroundValid = false;
        bool contentValid = false;
        HRGN dirty = nullptr;         // container pixels painted the ordinary way since the last Present
        ContainerLayers() = default;
        ContainerLayers(const ContainerLayers&) = delete;
        ContainerLayers& operator=(const ContainerLayers&) = delete;
        ~ContainerLayers() {
            if (dirty) ::DeleteObject(dirty);
        }
        void MarkDirty(const RECT& rect) {
            if (!contentValid || ::IsRectEmpty(&rect)) return;
            HRGN add = ::CreateRectRgnIndirect(&rect);
            if (!add) {
                contentValid = false;
                return;
            }
            if (!dirty) {
                dirty = add;
                return;
            }
            ::CombineRgn(dirty, dirty, add, RGN_OR);
            ::DeleteObject(add);
        }
    };
    Dib composed;  // the composed frame Present copies to the screen
    winrt::com_ptr<IWICImagingFactory> wic;
    bool wicUnavailable = false;
    bool retained = false;  // drawing the retained content layer (control visuals may come from cache)
    std::vector<std::unique_ptr<ContainerLayers>> layers;

    void InvalidateLayers() noexcept {
        for (auto& layer : layers) {
            layer->backgroundValid = false;
            layer->contentValid = false;
        }
    }

    void ReleaseLayers() noexcept {
        layers.clear();
        composed.Release();
    }

    // Ordinary painting inside a container with retained layers marks that part of its content stale.
    void NoteOrdinaryPaint(HWND hwnd, const RECT& paintedPx) {
        if (layers.empty() || ::IsRectEmpty(&paintedPx)) return;
        for (auto& layer : layers) {
            if (hwnd != layer->container && !::IsChild(layer->container, hwnd)) continue;
            RECT rect = MapRect(hwnd, layer->container, paintedPx);
            layer->MarkDirty(rect);
        }
    }

    // Form-client pixels of a rectangle in `from`'s client coordinates, honoring a pending move.
    [[nodiscard]] RECT ToForm(HWND from, RECT rect) const noexcept {
        rect = MapRect(from, window, rect);
        if (pendingWindow && pendingDy && (from == pendingWindow || ::IsChild(pendingWindow, from))) ::OffsetRect(&rect, 0, pendingDy);
        return rect;
    }
    [[nodiscard]] RECT FormClient(HWND hwnd) const noexcept {
        RECT client{};
        ::GetClientRect(hwnd, &client);
        return ToForm(hwnd, client);
    }
    [[nodiscard]] POINT FormOrigin(HWND hwnd) const noexcept {
        const RECT origin = ToForm(hwnd, RECT{});
        return POINT{origin.left, origin.top};
    }

    ~Impl() override { DetachAll(); }

    // --- basics ---
    [[nodiscard]] bool Attached() const noexcept { return window != nullptr; }
    [[nodiscard]] bool Styled() const noexcept { return material != FormMaterial::Native && renderer; }
    [[nodiscard]] float Scale() const noexcept { return platform::ScaleForDpi(dpi); }
    [[nodiscard]] int Px(float dip) const noexcept { return static_cast<int>(std::lround(dip * Scale())); }
    [[nodiscard]] D2D1_RECT_F ToDip(const RECT& px) const noexcept {
        const float s = Scale();
        return D2D1::RectF(static_cast<float>(px.left) / s, static_cast<float>(px.top) / s, static_cast<float>(px.right) / s,
                           static_cast<float>(px.bottom) / s);
    }
    [[nodiscard]] double Now() const noexcept { return graphics::FrameScheduler::Now(); }
    [[nodiscard]] double Duration(double seconds) const noexcept { return motion.Scale(seconds); }

    ControlState* Find(HWND hwnd) const noexcept {
        if (!hwnd) return nullptr;
        for (const auto& control : controls) {
            if (control->hwnd == hwnd) return control.get();
        }
        return nullptr;
    }

    // --- resources ---
    bool EnsureTarget() {
        if (target && context) return true;
        target = nullptr;
        context = nullptr;
        if (!factory) return false;
        const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        // Software rasterization: the scene is copied into GDI buffered-paint DIBs anyway, so a GPU
        // target would only add a readback per paint.
        for (const auto type : {D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1_RENDER_TARGET_TYPE_DEFAULT}) {
            const auto properties = D2D1::RenderTargetProperties(type, format, 0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_NONE,
                                                                 D2D1_FEATURE_LEVEL_DEFAULT);
            winrt::com_ptr<ID2D1DCRenderTarget> created;
            if (FAILED(factory->CreateDCRenderTarget(&properties, created.put()))) continue;
            auto device = created.try_as<ID2D1DeviceContext>();
            if (!device) continue;
            target = std::move(created);
            context = std::move(device);
            return true;
        }
        return false;
    }

    void CreateFonts() {
        if (font) ::DeleteObject(font);
        font = nullptr;
        for (auto& [role, handle] : nativeFonts) ::DeleteObject(handle);
        nativeFonts.clear();
        font = CreateUiFont(tokens.form.bodySize, FW_NORMAL, false);
        lineHeightPx = Px(tokens.form.bodySize * 1.33f);
        if (HDC screen = ::GetDC(nullptr)) {
            const HGDIOBJ old = ::SelectObject(screen, font);
            TEXTMETRICW metrics{};
            if (::GetTextMetricsW(screen, &metrics)) lineHeightPx = metrics.tmHeight;
            ::SelectObject(screen, old);
            ::ReleaseDC(nullptr, screen);
        }
    }

    HFONT CreateUiFont(float sizeDip, int weight, bool display) const {
        const float textScale = std::isfinite(tokens.textScale) ? std::clamp(tokens.textScale, 1.0f, 2.25f) : 1.0f;
        LOGFONTW logical{};
        logical.lfHeight = -std::max(1, Px(sizeDip * textScale));
        logical.lfWeight = weight;
        logical.lfCharSet = DEFAULT_CHARSET;
        logical.lfQuality = CLEARTYPE_QUALITY;
        ::wcsncpy_s(logical.lfFaceName, GdiFamily(display), _TRUNCATE);
        HFONT created = ::CreateFontIndirectW(&logical);
        return created ? created : static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    }

    HFONT NativeFont(TextRole role) {
        const int key = static_cast<int>(role);
        if (auto it = nativeFonts.find(key); it != nativeFonts.end()) return it->second;
        const auto& f = tokens.form;
        HFONT created = nullptr;
        switch (role) {
            case TextRole::Title: created = CreateUiFont(f.titleSize, FW_SEMIBOLD, true); break;
            case TextRole::Section: created = CreateUiFont(f.sectionSize, FW_SEMIBOLD, false); break;
            case TextRole::Strong: created = CreateUiFont(f.bodySize, FW_SEMIBOLD, false); break;
            case TextRole::Helper:
            case TextRole::Caption: created = CreateUiFont(f.helperSize, FW_NORMAL, false); break;
            case TextRole::Status: created = CreateUiFont(f.statusSize, FW_NORMAL, false); break;
            case TextRole::Body: default: created = CreateUiFont(f.bodySize, FW_NORMAL, false); break;
        }
        nativeFonts.emplace(key, created);
        return created;
    }

    int ListItemHeight(bool navigation) {
        const float minimum = navigation ? tokens.form.navItemHeight : kListRowHeight;
        const float measured = text.Measure(L"Ag", paint::BodyStyle(tokens)).height;
        return std::max(Px(minimum), std::max(lineHeightPx, Px(measured)) + Px(12.0f));
    }

    void CreateBrushes() {
        if (fieldBrush) ::DeleteObject(fieldBrush);
        if (fieldDisabledBrush) ::DeleteObject(fieldDisabledBrush);
        fieldBrush = ::CreateSolidBrush(tokens.form.fieldFill.ToColorRef());
        fieldDisabledBrush = ::CreateSolidBrush(tokens.form.fieldFillDisabled.ToColorRef());
    }

    void ReleaseResources() noexcept {
        if (font) ::DeleteObject(font);
        font = nullptr;
        for (auto& [role, handle] : nativeFonts) ::DeleteObject(handle);
        nativeFonts.clear();
        if (fieldBrush) ::DeleteObject(fieldBrush);
        if (fieldDisabledBrush) ::DeleteObject(fieldDisabledBrush);
        fieldBrush = fieldDisabledBrush = nullptr;
        target = nullptr;
        context = nullptr;
        ReleaseLayers();
        DropVisualCaches();
    }

    IAccPropServices* AccProps() {
        if (accProps) return accProps.get();
        const HRESULT hr = ::CoCreateInstance(kClsidAccPropServices, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IAccPropServices),
                                              accProps.put_void());
        if (FAILED(hr)) {
            accProps = nullptr;
            if (!helpUnavailableLogged) {
                AETHER_LOG_WARN("ui_form", "accessible help text unavailable (0x{:08X})", static_cast<unsigned>(hr));
                helpUnavailableLogged = true;
            }
        }
        return accProps.get();
    }

    // --- material ---
    void ApplyWindowMaterial() noexcept {
        if (!window || !::IsWindow(window)) return;
        // A maximized frame keeps the frame extended without a system material; the owner's rounded
        // window region shapes it.
        const MARGINS margins = material == FormMaterial::Acrylic || Framed() ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
        ::DwmExtendFrameIntoClientArea(window, &margins);
        if (SystemBackdropSupported()) {
            const DWORD type = material == FormMaterial::Acrylic ? kBackdropTransient : kBackdropNone;
            ::DwmSetWindowAttribute(window, kDwmSystemBackdropType, &type, sizeof(type));
        }
        // D45: a normal window is rounded by DWM with a thin neutral rim (DWM draws neither while the
        // window is maximized; the form's own frame takes over then). High contrast keeps the system's.
        const DWORD corner = material == FormMaterial::Native ? kCornerDefault : kCornerRound;
        const DWORD border = material == FormMaterial::Native ? kDwmColorDefault : tokens.form.windowRim.ToColorRef();
        (void)::DwmSetWindowAttribute(window, kDwmCornerPreference, &corner, sizeof(corner));
        (void)::DwmSetWindowAttribute(window, kDwmBorderColor, &border, sizeof(border));
        platform::ApplyImmersiveDarkMode(window, tokens.mode == graphics::ThemeMode::Dark);
    }

    void RestoreWindowMaterial() noexcept {
        if (!window || !::IsWindow(window)) return;
        const MARGINS margins{0, 0, 0, 0};
        ::DwmExtendFrameIntoClientArea(window, &margins);
        if (SystemBackdropSupported()) {
            const DWORD type = kBackdropAuto;
            ::DwmSetWindowAttribute(window, kDwmSystemBackdropType, &type, sizeof(type));
        }
        const DWORD corner = kCornerDefault, border = kDwmColorDefault;
        (void)::DwmSetWindowAttribute(window, kDwmCornerPreference, &corner, sizeof(corner));
        (void)::DwmSetWindowAttribute(window, kDwmBorderColor, &border, sizeof(border));
    }

    // --- control styles ---
    void SetStyleBits(ControlState& c, LONG_PTR addStyle, LONG_PTR removeStyle, LONG_PTR addEx, LONG_PTR removeEx) {
        const LONG_PTR style = ::GetWindowLongPtrW(c.hwnd, GWL_STYLE);
        const LONG_PTR ex = ::GetWindowLongPtrW(c.hwnd, GWL_EXSTYLE);
        const LONG_PTR added = addStyle & ~style;
        const LONG_PTR removed = removeStyle & style;
        const LONG_PTR addedEx = addEx & ~ex;
        const LONG_PTR removedEx = removeEx & ex;
        if (!added && !removed && !addedEx && !removedEx) return;
        c.addedStyle |= added;
        c.removedStyle |= removed;
        c.addedExStyle |= addedEx;
        c.removedExStyle |= removedEx;
        ::SetWindowLongPtrW(c.hwnd, GWL_STYLE, (style | added) & ~removed);
        ::SetWindowLongPtrW(c.hwnd, GWL_EXSTYLE, (ex | addedEx) & ~removedEx);
        ::SetWindowPos(c.hwnd, nullptr, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    void RestoreStyleBits(ControlState& c) {
        if (!c.addedStyle && !c.removedStyle && !c.addedExStyle && !c.removedExStyle) return;
        const LONG_PTR style = ::GetWindowLongPtrW(c.hwnd, GWL_STYLE);
        const LONG_PTR ex = ::GetWindowLongPtrW(c.hwnd, GWL_EXSTYLE);
        ::SetWindowLongPtrW(c.hwnd, GWL_STYLE, (style & ~c.addedStyle) | c.removedStyle);
        ::SetWindowLongPtrW(c.hwnd, GWL_EXSTYLE, (ex & ~c.addedExStyle) | c.removedExStyle);
        c.addedStyle = c.removedStyle = c.addedExStyle = c.removedExStyle = 0;
        ::SetWindowPos(c.hwnd, nullptr, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    float FadeOf(HWND hwnd) const noexcept {
        const double now = Now();
        float opacity = 1.0f;
        for (const auto& control : controls) {
            if (control->spec.role != ControlRole::Container) continue;
            const float value = control->fade.Value(now);
            if (value >= 0.999f) continue;
            if (control->hwnd == hwnd || ::IsChild(control->hwnd, hwnd)) opacity *= std::clamp(value, 0.0f, 1.0f);
        }
        return opacity;
    }

    void UpdateLayeredAlpha(ControlState& c) {
        if (!(::GetWindowLongPtrW(c.hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) || !(c.addedExStyle & WS_EX_LAYERED)) return;
        // A masked editor keeps the smallest nonzero opacity: invisible, yet still hit-testable.
        ::SetLayeredWindowAttributes(c.hwnd, 0, c.masked ? BYTE{1} : AlphaByte(FadeOf(c.hwnd)), LWA_ALPHA);
    }

    // --- motion masking ---
    // Every shown editor of the container counts, not only those in view: motion brings others in.
    [[nodiscard]] bool Maskable(const ControlState& c, HWND container) const noexcept {
        if (!Welled(c.spec.role) || !::IsChild(container, c.hwnd) || !::IsWindowVisible(c.hwnd)) return false;
        return (c.addedExStyle & WS_EX_LAYERED) != 0;
    }

    bool CaptureSnapshot(ControlState& c) {
        RECT bounds{};
        if (!::GetWindowRect(c.hwnd, &bounds)) return false;
        const LONG width = bounds.right - bounds.left;
        const LONG height = bounds.bottom - bounds.top;
        if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HDC memory = ::CreateCompatibleDC(nullptr);
        HBITMAP bitmap = memory ? ::CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        bool captured = false;
        if (memory && bitmap && bits) {
            const HGDIOBJ previous = ::SelectObject(memory, bitmap);
            captured = ::PrintWindow(c.hwnd, memory, 0) != FALSE;
            ::GdiFlush();
            if (captured) {
                const auto* source = static_cast<const std::uint32_t*>(bits);
                c.snapshot.assign(source, source + static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
                for (auto& pixel : c.snapshot) pixel |= 0xFF000000u;  // GDI leaves alpha 0; the editor is opaque
                c.snapshotSize = SIZE{width, height};
            }
            ::SelectObject(memory, previous);
        }
        if (bitmap) ::DeleteObject(bitmap);
        if (memory) ::DeleteDC(memory);
        return captured;
    }

    void DrawSnapshot(ID2D1DeviceContext* d2d, const ControlState& c, HWND parent) {
        if (c.snapshot.empty()) return;
        const RECT bounds = WindowRectInParent(c.hwnd);
        if (bounds.right - bounds.left != c.snapshotSize.cx || bounds.bottom - bounds.top != c.snapshotSize.cy) return;
        winrt::com_ptr<ID2D1Bitmap> bitmap;
        const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                       static_cast<float>(dpi), static_cast<float>(dpi));
        if (FAILED(d2d->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(c.snapshotSize.cx), static_cast<UINT32>(c.snapshotSize.cy)),
                                     c.snapshot.data(), static_cast<UINT32>(c.snapshotSize.cx) * 4u, properties, bitmap.put()))) {
            return;
        }
        d2d->DrawBitmap(bitmap.get(), ToDip(ToForm(parent, bounds)), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    }

    bool HasVisibleEditors(HWND container) const noexcept {
        for (const auto& control : controls) {
            if (Maskable(*control, container)) return true;
        }
        return false;
    }

    // An editor's image is kept current underneath the live editor at rest (where it is never
    // visible), so a motion can start by hiding the editor without first waiting for its image.
    void MarkSnapshotDirty(ControlState& c) {
        c.snapshotDirty = true;
        if (!snapshotTimer && window) snapshotTimer = ::PostMessageW(window, SnapshotRefreshMessage(), 0, 0) != FALSE;
    }

    void RefreshSnapshots() {
        snapshotTimer = false;
        if (!Styled()) return;
        for (auto& control : controls) {
            ControlState& c = *control;
            if (!c.snapshotDirty || !Welled(c.spec.role) || !(c.addedExStyle & WS_EX_LAYERED) || !::IsWindowVisible(c.hwnd)) continue;
            if (CaptureSnapshot(c)) {
                c.snapshotDirty = false;
                InvalidateFrame(c);
            }
        }
    }

    // True when every editor of `container` is now hidden behind its current image. False when an
    // image had to be refreshed first: present it, let a frame pass and call again.
    bool MaskEditors(HWND container) {
        bool refreshed = false;
        for (auto& control : controls) {
            ControlState& c = *control;
            if (c.masked || !Maskable(c, container) || (!c.snapshotDirty && !c.snapshot.empty())) continue;
            if (CaptureSnapshot(c)) {
                c.snapshotDirty = false;
                refreshed = true;
            }
        }
        if (refreshed) return false;
        for (auto& control : controls) {
            ControlState& c = *control;
            if (c.masked || !Maskable(c, container) || c.snapshot.empty()) continue;
            c.masked = true;
            UpdateLayeredAlpha(c);
        }
        return true;
    }

    void Unmask(ControlState& c) {
        if (!c.masked) return;
        c.masked = false;
        UpdateLayeredAlpha(c);  // the scene still shows the identical snapshot underneath
    }

    void UnmaskEditors(HWND container) {
        for (auto& control : controls) {
            if (control->masked && (!container || ::IsChild(container, control->hwnd))) Unmask(*control);
        }
    }

    void ApplyRole(ControlState& c) {
        const bool styled = Styled();
        const ControlRole role = c.spec.role;
        if (Welled(role)) {
            if (styled) {
                // The native editor renders opaque on its own layered surface (alpha 255): GDI text
                // and caret never punch holes into the premultiplied glass of the form.
                SetStyleBits(c, 0, WS_BORDER, WS_EX_LAYERED, WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
                UpdateLayeredAlpha(c);
            } else {
                RestoreStyleBits(c);
            }
            if (role == ControlRole::Document || role == ControlRole::List) {
                // Cosmetic only: "DarkMode_Explorer" is an undocumented theme name that darkens the
                // scroll bars; when it is unknown the call fails and the stock scroll bars remain.
                if (styled) {
                    ::SetWindowTheme(c.hwnd, tokens.mode == graphics::ThemeMode::Dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
                    c.themeOverridden = true;
                } else if (c.themeOverridden) {
                    ::SetWindowTheme(c.hwnd, nullptr, nullptr);
                    c.themeOverridden = false;
                }
            }
            if (role == ControlRole::Document) {
                const int margin = Px(6.0f);
                ::SendMessageW(c.hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(margin, margin));
            }
            if (role == ControlRole::List) ::SendMessageW(c.hwnd, LB_SETITEMHEIGHT, 0, ListItemHeight(false));
        } else if (role == ControlRole::Navigation) {
            if (styled) {
                SetStyleBits(c, 0, WS_BORDER, 0, WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
            } else {
                RestoreStyleBits(c);
            }
            ::SendMessageW(c.hwnd, LB_SETITEMHEIGHT, 0, ListItemHeight(true));
            c.selected = -2;  // re-snap the indicator
        } else if (role == ControlRole::Slider) {
            SetStyleBits(c, TBS_FIXEDLENGTH | TBS_NOTICKS, 0, 0, 0);
            ::SendMessageW(c.hwnd, TBM_SETTHUMBLENGTH, static_cast<WPARAM>(Px(tokens.form.sliderThumbSize)), 0);
        } else if (role == ControlRole::Progress) {
            const bool indeterminate = Indeterminate(c);
            if (styled) {
                // Unthemed: the themed bar runs a perpetual glow timer although it is never painted.
                if (!c.themeOverridden) ::SetWindowTheme(c.hwnd, L"", L"");
                c.themeOverridden = true;
                // Unthemed bars get a classic sunken static edge in the non-client area. Removing it
                // resets the native position, so the range and position are carried across.
                PBRANGE range{};
                SendNative(c, PBM_GETRANGE, FALSE, reinterpret_cast<LPARAM>(&range));
                const auto position = static_cast<int>(SendNative(c, PBM_GETPOS, 0, 0));
                SetStyleBits(c, 0, WS_BORDER, 0, WS_EX_STATICEDGE | WS_EX_CLIENTEDGE);
                if (SendNative(c, PBM_GETPOS, 0, 0) != position) {
                    SendNative(c, PBM_SETRANGE32, static_cast<WPARAM>(range.iLow), range.iHigh);
                    SendNative(c, PBM_SETPOS, static_cast<WPARAM>(position), 0);
                }
                SendNative(c, PBM_SETMARQUEE, FALSE, 0);  // the scene draws the sweep
            } else {
                RestoreStyleBits(c);
                if (c.themeOverridden) ::SetWindowTheme(c.hwnd, nullptr, nullptr);
                c.themeOverridden = false;
                if (indeterminate) {
                    SetStyleBits(c, PBS_MARQUEE, 0, 0, 0);
                    SendNative(c, PBM_SETMARQUEE, TRUE, 30);
                } else {
                    SendNative(c, PBM_SETMARQUEE, FALSE, 0);
                }
            }
        }
    }

    // Sends a message the subclass would otherwise intercept, straight to the native control.
    LRESULT SendNative(ControlState& c, UINT message, WPARAM wParam, LPARAM lParam) {
        const bool previous = c.bypass;
        c.bypass = true;
        const LRESULT result = ::SendMessageW(c.hwnd, message, wParam, lParam);
        c.bypass = previous;
        return result;
    }

    void RestoreControl(ControlState& c) noexcept {
        if (!::IsWindow(c.hwnd)) return;
        try {
            const bool marquee = c.spec.role == ControlRole::Progress && Indeterminate(c);
            ::RemoveWindowSubclass(c.hwnd, &ControlProc, kSubclassId);
            RestoreStyleBits(c);
            if (marquee) {
                // The native bar takes over the indeterminate state it was asked for.
                ::SetWindowLongPtrW(c.hwnd, GWL_STYLE, ::GetWindowLongPtrW(c.hwnd, GWL_STYLE) | PBS_MARQUEE);
                ::SendMessageW(c.hwnd, PBM_SETMARQUEE, TRUE, 30);
            }
            if (c.themeOverridden) ::SetWindowTheme(c.hwnd, nullptr, nullptr);
            c.themeOverridden = false;
            ClearHelp(c);
            ClearName(c);
            ::InvalidateRect(c.hwnd, nullptr, TRUE);
        } catch (...) {
            AETHER_LOG_ERROR("ui_form", "restoring a registered control failed");
        }
    }

    void PublishHelp(ControlState& c) {
        if (c.spec.accessibleName.empty()) {
            ClearName(c);
        } else if (auto* services = AccProps()) {
            c.namePublished = SUCCEEDED(services->SetHwndPropStr(c.hwnd, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF,
                                                                kPropIdAccName, c.spec.accessibleName.c_str()));
        }
        if (!IsRole(c.spec.role, {ControlRole::Switch, ControlRole::Check})) {
            ClearHelp(c);
            return;
        }
        if (c.spec.description.empty()) {
            ClearHelp(c);
            return;
        }
        auto* services = AccProps();
        if (!services) return;
        if (SUCCEEDED(services->SetHwndPropStr(c.hwnd, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, kPropIdAccHelp,
                                               c.spec.description.c_str()))) {
            c.helpPublished = true;
        }
    }

    void ClearHelp(ControlState& c) noexcept {
        if (!c.helpPublished) return;
        c.helpPublished = false;
        if (!accProps || !::IsWindow(c.hwnd)) return;
        MSAAPROPID properties[] = {kPropIdAccHelp};
        (void)accProps->ClearHwndProps(c.hwnd, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, properties, 1);
    }

    void ClearName(ControlState& c) noexcept {
        if (!c.namePublished) return;
        c.namePublished = false;
        if (!accProps || !::IsWindow(c.hwnd)) return;
        MSAAPROPID properties[] = {kPropIdAccName};
        (void)accProps->ClearHwndProps(c.hwnd, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, properties, 1);
    }

    // --- frames ---
    RECT WindowRectInParent(HWND hwnd) const noexcept {
        RECT rect{};
        ::GetWindowRect(hwnd, &rect);
        ::MapWindowPoints(nullptr, ::GetParent(hwnd), reinterpret_cast<POINT*>(&rect), 2);
        return rect;
    }

    RECT ChildRectFor(const ControlState& c, const RECT& frame) const {
        RECT r = frame;
        const ControlRole role = c.spec.role;
        if (Margined(role)) {
            const int m = Px(tokens.form.focusMargin);
            ::InflateRect(&r, m, m);
        } else if (Styled() && role == ControlRole::Field) {
            const int padX = Px(tokens.form.fieldPaddingX);
            const int height = std::min(static_cast<int>(frame.bottom - frame.top) - 2, lineHeightPx + 1);
            r.left = frame.left + padX;
            r.right = std::max<LONG>(r.left + 1, frame.right - padX);
            r.top = frame.top + (frame.bottom - frame.top - height) / 2;
            r.bottom = r.top + std::max(1, height);
        } else if (Styled() && (role == ControlRole::Document || role == ControlRole::List)) {
            const int inset = Px(kWellInset);
            ::InflateRect(&r, -inset, -inset);
        } else if (!Styled() && IsRole(role, {ControlRole::Switch, ControlRole::Check}) && !c.spec.description.empty()) {
            // Native: the stock check box keeps the label line; the parent paints the description below.
            r.bottom = std::min(frame.bottom, frame.top + lineHeightPx + Px(10.0f));
        }
        return r;
    }

    // The well/frame region a control occupies in its parent (frame or derived from its window).
    RECT FrameOf(const ControlState& c) const {
        if (c.hasFrame) return c.frame;
        RECT r = WindowRectInParent(c.hwnd);
        const ControlRole role = c.spec.role;
        if (Margined(role)) {
            const int m = Px(tokens.form.focusMargin);
            ::InflateRect(&r, -m, -m);
        } else if (Styled() && role == ControlRole::Field) {
            const int padX = Px(tokens.form.fieldPaddingX);
            r.left -= padX;
            r.right += padX;
            const int padY = Px(6.0f);
            r.top -= padY;
            r.bottom += padY;
        } else if (Styled() && (role == ControlRole::Document || role == ControlRole::List)) {
            const int inset = Px(kWellInset);
            ::InflateRect(&r, inset, inset);
        }
        return r;
    }

    void ApplyFrame(ControlState& c) {
        if (!c.hasFrame || !::IsWindow(c.hwnd)) return;
        const RECT old = WindowRectInParent(c.hwnd);
        const RECT r = ChildRectFor(c, c.frame);
        int height = r.bottom - r.top;
        if (c.spec.role == ControlRole::ComboBox) height += Px(kComboListHeight);  // room for the native list
        ::SetWindowPos(c.hwnd, nullptr, r.left, r.top, r.right - r.left, height, SWP_NOZORDER | SWP_NOACTIVATE);
        if (c.spec.role == ControlRole::ComboBox) FitComboHeight(c, r.bottom - r.top);
        InvalidateFrame(c, &old);
        ::InvalidateRect(c.hwnd, nullptr, FALSE);
    }

    // A closed CBS_DROPDOWNLIST combo box takes its height from the selection field item height.
    void FitComboHeight(ControlState& c, int wanted) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            RECT rect{};
            ::GetWindowRect(c.hwnd, &rect);
            const int actual = rect.bottom - rect.top;
            if (actual == wanted || wanted <= 4) return;
            const LRESULT item = ::SendMessageW(c.hwnd, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0);
            if (item == CB_ERR) return;
            const int next = std::max(1, static_cast<int>(item) + wanted - actual);
            if (next == item) return;
            ::SendMessageW(c.hwnd, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), next);
        }
    }

    void InvalidateFrame(const ControlState& c, const RECT* also = nullptr) {
        HWND parent = ::GetParent(c.hwnd);
        if (!parent) return;
        RECT frame = FrameOf(c);
        const int grow = Px(4.0f);
        ::InflateRect(&frame, grow, grow);
        ::InvalidateRect(parent, &frame, FALSE);
        if (also) {
            RECT other = *also;
            ::InflateRect(&other, grow + Px(tokens.form.fieldPaddingX), grow + Px(6.0f));
            ::InvalidateRect(parent, &other, FALSE);
        }
    }

    // --- state ---
    [[nodiscard]] bool Indeterminate(const ControlState& c) const noexcept {
        if (c.indeterminate) return *c.indeterminate;
        return (::GetWindowLongPtrW(c.hwnd, GWL_STYLE) & PBS_MARQUEE) != 0;
    }

    [[nodiscard]] std::optional<double> ProgressFraction(const ControlState& c) const noexcept {
        if (Indeterminate(c)) return std::nullopt;
        if (c.fraction) return c.fraction;
        PBRANGE range{};
        ::SendMessageW(c.hwnd, PBM_GETRANGE, FALSE, reinterpret_cast<LPARAM>(&range));
        const auto position = static_cast<int>(::SendMessageW(c.hwnd, PBM_GETPOS, 0, 0));
        if (range.iHigh <= range.iLow) return 0.0;
        return std::clamp(static_cast<double>(position - range.iLow) / static_cast<double>(range.iHigh - range.iLow), 0.0, 1.0);
    }

    [[nodiscard]] bool TopLevelShown() const noexcept {
        return window && ::IsWindowVisible(window) && !::IsIconic(::GetAncestor(window, GA_ROOT));
    }

    [[nodiscard]] bool Continuous(const ControlState& c) const noexcept {
        if (!motion.AllowsMovement() || !Styled()) return false;
        const bool busy = c.spec.role == ControlRole::Label && c.spec.text == TextRole::Status && c.spec.severity == Severity::Busy;
        const bool sweep = c.spec.role == ControlRole::Progress && Indeterminate(c);
        return (busy || sweep) && ::IsWindowVisible(c.hwnd) && TopLevelShown();
    }

    void Animate(graphics::Tween& tween, float goal, double duration) {
        const double now = Now();
        if (std::fabs(tween.Target() - goal) < 1e-4f && !tween.IsAnimating(now)) {
            tween.SnapTo(goal);
            return;
        }
        tween.AnimateTo(goal, now, Duration(duration), tokens.curves.standard);
    }

    void StartAnimation(ControlState& c) {
        if (!TweensRunning(c, Now()) && !Continuous(c)) {
            // Nothing moves (reduced motion, or already at the target): one repaint, no frames.
            c.animating = false;
            InvalidateVisual(c);
            return;
        }
        c.animating = true;
        RequestAnimation();
    }

    void InvalidateVisual(ControlState& c) {
        if (c.spec.role == ControlRole::Container) {
            for (auto& other : controls) {
                if (Welled(other->spec.role) && ::IsChild(c.hwnd, other->hwnd)) UpdateLayeredAlpha(*other);
            }
            // A fading container is presented as one frame; its children never repaint one by one.
            if (!Present(PresentRoot(c.hwnd))) ::RedrawWindow(c.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        } else if (Welled(c.spec.role)) {
            InvalidateFrame(c);
            if (c.spec.role == ControlRole::List) ::InvalidateRect(c.hwnd, nullptr, FALSE);
        } else {
            ::InvalidateRect(c.hwnd, nullptr, FALSE);
        }
    }

    void RequestAnimation() {
        if (!window) return;
        if (scheduler) {
            if (!frameRequested) {
                frameRequested = true;
                scheduler->RequestFrame(this);
            }
        } else if (!timerRunning) {
            timerRunning = ::SetTimer(window, kAnimationTimer, kTimerIntervalMs, nullptr) != 0;
        }
    }

    void StopTimer() noexcept {
        if (timerRunning && window) ::KillTimer(window, kAnimationTimer);
        timerRunning = false;
    }

    void OnFrame(double now) override {
        frameRequested = false;
        try {
            Tick(now);
        } catch (...) {
            AETHER_LOG_ERROR("ui_form", "animation frame failed");
        }
    }

    [[nodiscard]] bool TweensRunning(const ControlState& c, double now) const noexcept {
        return c.hover.IsAnimating(now) || c.pressed.IsAnimating(now) || c.on.IsAnimating(now) ||
               c.indicator.IsAnimating(now) || c.fade.IsAnimating(now) || c.rowHover.IsAnimating(now) ||
               c.rowLeave.IsAnimating(now);
    }

    // Hover amount of one Navigation/List row: the entering row rises, the previous one fades out
    // from wherever it was, so a pointer sweeping down the list leaves a short continuous trail.
    [[nodiscard]] float RowHoverAmount(const ControlState& c, int index, double now) const noexcept {
        if (index < 0) return 0.0f;
        if (index == c.hoverIndex) return std::clamp(c.rowHover.Value(now), 0.0f, 1.0f);
        if (index == c.leaveIndex) return std::clamp(c.rowLeave.Value(now), 0.0f, 1.0f);
        return 0.0f;
    }

    void Tick(double now) {
        bool again = false;
        // Index loop: invalidation never destroys controls, but stay robust to re-entrancy.
        for (std::size_t i = 0; i < controls.size(); ++i) {
            ControlState& c = *controls[i];
            const bool running = TweensRunning(c, now);
            const bool continuous = Continuous(c);
            if (!running && !continuous && !c.animating) continue;
            InvalidateVisual(c);
            c.animating = running;
            again = again || running || continuous;
        }
        if (again) {
            RequestAnimation();
        } else {
            StopTimer();
        }
    }

    [[nodiscard]] bool AnyAnimating() const noexcept {
        // A requested frame or a live timer still owes the final settled repaint.
        if (timerRunning || frameRequested) return true;
        const double now = Now();
        for (const auto& control : controls) {
            if (TweensRunning(*control, now) || Continuous(*control)) return true;
        }
        return false;
    }

    void KickContinuous() {
        for (const auto& control : controls) {
            if (Continuous(*control)) {
                control->animating = true;
                RequestAnimation();
                return;
            }
        }
    }

    void RefreshButtonState(ControlState& c) {
        const auto state = static_cast<UINT>(::SendMessageW(c.hwnd, BM_GETSTATE, 0, 0));
        const bool pushed = (state & BST_PUSHED) != 0;
        const int check = static_cast<int>(::SendMessageW(c.hwnd, BM_GETCHECK, 0, 0));
        if (pushed != c.pushed) {
            c.pushed = pushed;
            Animate(c.pressed, pushed ? 1.0f : 0.0f, tokens.form.feedback * 0.6);
            StartAnimation(c);
        }
        if (check != c.check) {
            c.check = check;
            Animate(c.on, check == BST_UNCHECKED ? 0.0f : 1.0f, tokens.form.feedback);
            StartAnimation(c);
        }
    }

    void RefreshPressed(ControlState& c, bool pressed) {
        if (pressed == c.pushed) return;
        c.pushed = pressed;
        Animate(c.pressed, pressed ? 1.0f : 0.0f, tokens.form.feedback * 0.6);
        StartAnimation(c);
    }

    float RowCenterDip(HWND list, int index) const {
        RECT item{};
        if (index < 0 || ::SendMessageW(list, LB_GETITEMRECT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&item)) == LB_ERR) {
            return -1000.0f;
        }
        return static_cast<float>(item.top + item.bottom) / 2.0f / Scale();
    }

    void RefreshNavigation(ControlState& c) {
        const int selected = static_cast<int>(::SendMessageW(c.hwnd, LB_GETCURSEL, 0, 0));
        const int top = static_cast<int>(::SendMessageW(c.hwnd, LB_GETTOPINDEX, 0, 0));
        if (selected == c.selected && top == c.topIndex) return;
        const float center = RowCenterDip(c.hwnd, selected);
        if (c.selected < -1 || top != c.topIndex || selected < 0 || c.selected < 0) {
            c.indicator.SnapTo(center);
        } else {
            c.indicator.AnimateTo(center, Now(), Duration(tokens.form.indicator), tokens.curves.standard);
            StartAnimation(c);
        }
        c.selected = selected;
        c.topIndex = top;
        ::InvalidateRect(c.hwnd, nullptr, FALSE);
    }

    void SetHover(ControlState& c, bool hovered) {
        if (c.hovered == hovered) return;
        c.hovered = hovered;
        if (hovered) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, c.hwnd, 0};
            ::TrackMouseEvent(&track);
        }
        if ((SelfPainted(c.spec.role) && c.spec.role != ControlRole::Container) || Welled(c.spec.role)) {
            // Arrival is quicker than departure: feedback starts at once and leaves gently. Containers
            // have no hover look of their own.
            Animate(c.hover, hovered ? 1.0f : 0.0f, tokens.form.feedback * (hovered ? 0.75 : 1.0));
            StartAnimation(c);
        }
    }

    void SetRowHover(ControlState& c, int index) {
        if (index == c.hoverIndex) return;
        const double now = Now();
        const int previous = c.hoverIndex;
        const float previousAmount = RowHoverAmount(c, previous, now);
        const float enteringFrom = index >= 0 && index == c.leaveIndex ? RowHoverAmount(c, index, now) : 0.0f;
        const std::array<int, 3> touched{c.leaveIndex, previous, index};
        c.leaveIndex = previous;
        c.rowLeave.SnapTo(previousAmount);
        if (previous >= 0) c.rowLeave.AnimateTo(0.0f, now, Duration(tokens.form.feedback), tokens.curves.standard);
        c.hoverIndex = index;
        c.rowHover.SnapTo(enteringFrom);
        if (index >= 0) c.rowHover.AnimateTo(1.0f, now, Duration(tokens.form.feedback * 0.75), tokens.curves.standard);
        if (c.spec.role == ControlRole::List) {
            for (const int row : touched) {
                RECT item{};
                if (row >= 0 && ::SendMessageW(c.hwnd, LB_GETITEMRECT, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&item)) != LB_ERR)
                    ::InvalidateRect(c.hwnd, &item, FALSE);
            }
        } else {
            ::InvalidateRect(c.hwnd, nullptr, FALSE);
        }
        StartAnimation(c);
    }

    // --- painting ---
    void Paint(HWND hwnd) {
        PAINTSTRUCT ps{};
        HDC hdc = ::BeginPaint(hwnd, &ps);
        if (hdc && !::IsRectEmpty(&ps.rcPaint)) PaintRegion(hwnd, hdc, ps.rcPaint, nullptr);
        ::EndPaint(hwnd, &ps);
        NoteOrdinaryPaint(hwnd, ps.rcPaint);
        for (const auto& entry : overlays) {
            if (entry.container == hwnd || ::IsChild(entry.container, hwnd)) SchedulePrepare(entry.container);
        }
    }

    // A scrolling container (one with an overlay) that painted without current retained layers gets them
    // prepared once the queued messages are handled, so its first scroll does not render everything.
    void SchedulePrepare(HWND container) {
        if (material != FormMaterial::Acrylic || preparePosted) return;
        if (std::none_of(overlays.begin(), overlays.end(), [container](const auto& entry) { return entry.container == container; })) return;
        for (const auto& layer : layers) {
            if (layer->container == container && layer->contentValid && layer->backgroundValid) return;
        }
        preparePosted = ::PostMessageW(window, PrepareLayersMessage(), reinterpret_cast<WPARAM>(container), 0) != FALSE;
    }

    void Prepare(HWND container) {
        preparePosted = false;
        ControlState* self = Find(container);
        if (!self || self->spec.role != ControlRole::Container || material != FormMaterial::Acrylic || !Styled() || !TopLevelShown() ||
            !::IsWindowVisible(container) || !EnsureTarget()) {
            return;
        }
        // Prepare after the page has painted: a later ordinary paint would only mark the work stale.
        RECT pending{};
        if (::GetUpdateRect(container, &pending, FALSE)) return;
        for (const auto& control : controls) {
            if (!Welled(control->spec.role) && ::IsChild(container, control->hwnd) && ::GetUpdateRect(control->hwnd, &pending, FALSE)) return;
        }
        RECT client{};
        ::GetClientRect(container, &client);
        if (::IsRectEmpty(&client)) return;
        deep = true;
        try {
            (void)ComposeRetained(container, client, nullptr, nullptr, 0, false);
        } catch (...) {
            InvalidateLayers();
        }
        deep = false;
    }

    void PaintRegion(HWND hwnd, HDC hdc, const RECT& clip, const RegionOverlay* overlay) {
        if (!hdc || ::IsRectEmpty(&clip)) return;
        if (!Styled() || !EnsureTarget()) {
            PaintNative(hwnd, hdc, clip);
            return;
        }
        BP_PAINTPARAMS params{sizeof(params), BPPF_ERASE, nullptr, nullptr};
        HDC memory = nullptr;
        HPAINTBUFFER buffer = ::BeginBufferedPaint(hdc, &clip, BPBF_TOPDOWNDIB, &params, &memory);
        RenderScene(hwnd, buffer ? memory : hdc, clip, overlay);
        if (buffer) ::EndBufferedPaint(buffer, TRUE);
    }

    // What RenderScene draws: everything (ordinary painting), only the window material and fixed
    // decoration (Present's cached background), or only what lies on the moving content (Present's
    // retained content layer, transparent elsewhere and without the container's own fade).
    enum class SceneLayer { All, Background, Content };

    void RenderScene(HWND hwnd, HDC hdc, const RECT& clip, const RegionOverlay* overlay, SceneLayer layer = SceneLayer::All) {
        if (FAILED(target->BindDC(hdc, &clip))) return;
        target->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        auto* d2d = context.get();
        d2d->BeginDraw();
        DrawSceneLayer(d2d, hwnd, clip, RECT{0, 0, clip.right - clip.left, clip.bottom - clip.top}, overlay, layer);
        const HRESULT hr = d2d->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            target = nullptr;
            context = nullptr;
            DropVisualCaches();
            InvalidateLayers();
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    // Draws `clip` (client pixels of `hwnd`) of the scene into `d2d` between its BeginDraw and EndDraw,
    // with `clip`'s top-left corner at `targetPx` (pixels of the target). Cleared to transparency first.
    void DrawSceneLayer(ID2D1DeviceContext* d2d, HWND hwnd, const RECT& clip, const RECT& targetPx, const RegionOverlay* overlay,
                        SceneLayer layer) {
        const float s = Scale();
        POINT origin{0, 0};
        origin = FormOrigin(hwnd);
        const float ox = static_cast<float>(origin.x + clip.left) / s;
        const float oy = static_cast<float>(origin.y + clip.top) / s;
        const D2D1_RECT_F area = D2D1::RectF(ox, oy, static_cast<float>(origin.x + clip.right) / s,
                                             static_cast<float>(origin.y + clip.bottom) / s);
        const float tx = static_cast<float>(targetPx.left) / s;
        const float ty = static_cast<float>(targetPx.top) / s;
        d2d->SetTransform(D2D1::Matrix3x2F::Translation(tx - ox, ty - oy));
        // Grayscale over translucent glass; ClearType only where every pixel is opaque (Solid).
        d2d->SetTextAntialiasMode(material == FormMaterial::Solid && layer != SceneLayer::Content ? D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE
                                                                                                    : D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        d2d->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_ALIASED);
        d2d->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        if (layer != SceneLayer::Content) {
            if (Framed()) {
                // The whole client is the rounded body; each window paints its own part of it.
                RECT client{};
                ::GetClientRect(window, &client);
                const D2D1_RECT_F body = D2D1::RectF(0.0f, 0.0f, static_cast<float>(client.right) / s, static_cast<float>(client.bottom) / s);
                paint::PaintBackground(d2d, area, tokens, material, &body);
            } else {
                paint::PaintBackground(d2d, area, tokens, material);
            }
        }
        const float fade = layer == SceneLayer::All ? FadeOf(hwnd) : 1.0f;
        const bool layered = fade < 0.999f;
        if (layered) {
            d2d->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                  D2D1::IdentityMatrix(), fade, nullptr, D2D1_LAYER_OPTIONS1_NONE),
                           nullptr);
        }
        DrawCards(d2d, area, layer);
        DrawDividers(d2d, area, layer);
        if (decorator && layer != SceneLayer::Background) {
            try {
                decorator(d2d, area, tokens);
            } catch (...) {
                AETHER_LOG_ERROR("ui_form", "form decorator threw");
            }
        }
        if (layer != SceneLayer::Background) {
            DrawControls(d2d, hwnd, area);
            if (deep) DrawDescendants(d2d, hwnd, area);
        }
        if (overlay && *overlay && layer == SceneLayer::All) {
            d2d->SetTransform(D2D1::Matrix3x2F::Translation(tx - static_cast<float>(clip.left) / s, ty - static_cast<float>(clip.top) / s));
            try {
                (*overlay)(d2d, tokens);
            } catch (...) {
                AETHER_LOG_ERROR("ui_form", "region overlay threw");
            }
        }
        if (layered) d2d->PopLayer();
        d2d->PopAxisAlignedClip();
    }

    // Visible part of a descendant window in form client pixels (clipped by every ancestor).
    RECT VisibleInForm(HWND hwnd) const noexcept {
        RECT visible = FormClient(hwnd);
        for (HWND parent = ::GetParent(hwnd); parent && parent != window; parent = ::GetParent(parent)) {
            const RECT clip = FormClient(parent);
            ::IntersectRect(&visible, &visible, &clip);
        }
        return visible;
    }

    bool Intersects(const D2D1_RECT_F& a, const D2D1_RECT_F& b) const noexcept {
        return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
    }

    // Rounds a DIP rectangle to device pixels so hairlines stay crisp.
    D2D1_RECT_F Snap(const D2D1_RECT_F& rect) const noexcept {
        const float s = Scale();
        const auto snap = [s](float v) { return std::round(v * s) / s; };
        return D2D1::RectF(snap(rect.left), snap(rect.top), snap(rect.right), snap(rect.bottom));
    }

    // Cards and dividers on a canvas move with the content; the others stay with the background.
    static bool InLayer(HWND canvas, SceneLayer layer) noexcept {
        return layer == SceneLayer::All || (layer == SceneLayer::Content) == (canvas != nullptr);
    }

    void DrawCards(ID2D1DeviceContext* d2d, const D2D1_RECT_F& area, SceneLayer layer) {
        const float pixel = 1.0f / Scale();
        for (const auto& card : cards) {
            if (!InLayer(card.canvas, layer)) continue;
            RECT px = card.px;
            std::optional<D2D1_RECT_F> clip;
            if (card.canvas) {
                if (!::IsWindow(card.canvas) || !::IsWindowVisible(card.canvas)) continue;
                px = ToForm(card.canvas, px);
                clip = ToDip(VisibleInForm(card.canvas));
            }
            const auto rect = Snap(ToDip(px));
            if (!Intersects(rect, area) || (clip && !Intersects(rect, *clip))) continue;
            if (clip) d2d->PushAxisAlignedClip(*clip, D2D1_ANTIALIAS_MODE_ALIASED);
            paint::PaintCard(d2d, rect, tokens, material, card.emphasis, pixel);
            if (clip) d2d->PopAxisAlignedClip();
        }
    }

    void DrawDividers(ID2D1DeviceContext* d2d, const D2D1_RECT_F& area, SceneLayer layer) {
        for (const auto& divider : dividers) {
            if (!InLayer(divider.canvas, layer)) continue;
            RECT px = divider.px;
            std::optional<D2D1_RECT_F> clip;
            if (divider.canvas) {
                if (!::IsWindow(divider.canvas) || !::IsWindowVisible(divider.canvas)) continue;
                px = ToForm(divider.canvas, px);
                clip = ToDip(VisibleInForm(divider.canvas));
            }
            const auto rect = ToDip(px);
            if (!Intersects(rect, area) || (clip && !Intersects(rect, *clip))) continue;
            if (clip) d2d->PushAxisAlignedClip(*clip, D2D1_ANTIALIAS_MODE_ALIASED);
            paint::PaintDivider(d2d, rect, tokens);
            if (clip) d2d->PopAxisAlignedClip();
        }
    }

    paint::Visual VisualOf(const ControlState& c, double now) const {
        paint::Visual visual;
        visual.enabled = ::IsWindowEnabled(c.hwnd) != FALSE;
        visual.hover = visual.enabled ? std::clamp(c.hover.Value(now), 0.0f, 1.0f) : 0.0f;
        visual.pressed = visual.enabled ? std::clamp(c.pressed.Value(now), 0.0f, 1.0f) : 0.0f;
        visual.error = c.error;
        visual.focusRing = ::GetFocus() == c.hwnd && detail::ShowsFocusCues(c.hwnd);
        visual.showMnemonic = detail::ShowsAccelerators(c.hwnd);
        return visual;
    }

    void DrawControls(ID2D1DeviceContext* d2d, HWND hwnd, const D2D1_RECT_F& area) {
        const double now = Now();
        DrawWells(d2d, hwnd, area, now);
        ControlState* self = Find(hwnd);
        if (!self || !SelfPainted(self->spec.role) || self->spec.role == ControlRole::Container) return;
        DrawSelf(d2d, *self, now);
    }

    // The wells of the opaque layered children whose parent is `parent`.
    void DrawWells(ID2D1DeviceContext* d2d, HWND parent, const D2D1_RECT_F& area, double now) {
        for (std::size_t i = 0; i < controls.size(); ++i) {
            ControlState& c = *controls[i];
            if (!Welled(c.spec.role) || ::GetParent(c.hwnd) != parent || !::IsWindowVisible(c.hwnd)) continue;
            const auto rect = Snap(ToDip(ToForm(parent, FrameOf(c))));
            if (!Intersects(rect, area)) continue;
            const HWND focus = ::GetFocus();
            const bool focused = c.spec.role != ControlRole::List && (focus == c.hwnd || ::IsChild(c.hwnd, focus));
            const bool enabled = ::IsWindowEnabled(c.hwnd) != FALSE;
            paint::PaintFieldWell(d2d, rect, enabled, focused, c.error, 1.0f / Scale(), tokens,
                                  enabled ? std::clamp(c.hover.Value(now), 0.0f, 1.0f) : 0.0f);
            DrawSnapshot(d2d, c, parent);
        }
    }

    // Present's single pass: the wells of every nested container, then the visual of every visible
    // self-painted descendant, each clipped to the part of its window that is actually visible.
    // The outermost registered container holding `container` (itself when none): a nested container
    // such as a scroll canvas is presented as part of its viewport, never on its own.
    HWND PresentRoot(HWND container) const noexcept {
        HWND root = container;
        for (HWND parent = ::GetParent(container); parent && parent != window; parent = ::GetParent(parent)) {
            if (const ControlState* c = Find(parent); c && c->spec.role == ControlRole::Container) root = parent;
        }
        return root;
    }

    // What DrawDescendants draws for one root, in drawing order; computed once per Present (several
    // changed strips share it) instead of walking every window's ancestors for every strip.
    struct DrawEntry {
        ControlState* control = nullptr;
        D2D1_RECT_F visible{};
        float fade = 1.0f;
    };
    std::vector<DrawEntry> drawList;
    HWND drawListRoot = nullptr;
    bool presenting = false;  // inside one retained Present: the draw list stays valid
    bool preparePosted = false;

    void BuildDrawList(HWND root) {
        drawList.clear();
        drawListRoot = nullptr;
        const float rootFade = std::max(0.001f, FadeOf(root));
        for (const bool wells : {true, false}) {
            for (std::size_t i = 0; i < controls.size(); ++i) {
                ControlState& c = *controls[i];
                const bool container = c.spec.role == ControlRole::Container;
                if (wells != container) continue;
                if (!container && (Welled(c.spec.role) || !SelfPainted(c.spec.role))) continue;
                if (c.hwnd == root || !::IsChild(root, c.hwnd) || !::IsWindowVisible(c.hwnd)) continue;
                const auto visible = ToDip(VisibleInForm(c.hwnd));
                if (visible.right <= visible.left || visible.bottom <= visible.top) continue;
                drawList.push_back(DrawEntry{&c, visible, std::clamp(FadeOf(c.hwnd) / rootFade, 0.0f, 1.0f)});
            }
        }
        drawListRoot = root;
    }

    void DrawDescendants(ID2D1DeviceContext* d2d, HWND root, const D2D1_RECT_F& area) {
        const double now = Now();
        if (!presenting || drawListRoot != root) BuildDrawList(root);  // ordinary paints rebuild it each time
        for (const auto& entry : drawList) {
            {
                ControlState& c = *entry.control;
                const bool container = c.spec.role == ControlRole::Container;
                const auto& visible = entry.visible;
                if (!Intersects(visible, area)) continue;
                const float fade = entry.fade;
                const bool layered = fade < 0.999f;
                d2d->PushAxisAlignedClip(visible, D2D1_ANTIALIAS_MODE_ALIASED);
                if (layered) {
                    d2d->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                          D2D1::IdentityMatrix(), fade, nullptr, D2D1_LAYER_OPTIONS1_NONE),
                                   nullptr);
                }
                if (container) DrawWells(d2d, c.hwnd, area, now);
                else if (!DrawCachedSelf(d2d, c, now)) DrawSelf(d2d, c, now);
                if (layered) d2d->PopLayer();
                d2d->PopAxisAlignedClip();
            }
        }
    }


    // `move` (optional) repositions `moving` by `dy` pixels: the frame is rendered at the destination
    // first, then the windows move and the finished frame is copied right behind them, so the opaque
    // layered editors (composed separately by DWM) and the scene land in the same composition.
    // Returns false without calling `move` when nothing can be presented.
    // A moving page keeps every control's look: Present reuses each visual rendered earlier instead of
    // re-rendering its text and shapes on every frame. A control that repainted, is invalidated or is
    // animating is rendered again. Only over translucent glass, where text is grayscale either way.
    bool DrawCachedSelf(ID2D1DeviceContext* d2d, ControlState& c, double now) {
        if (!retained || material != FormMaterial::Acrylic || Continuous(c) || c.spec.role == ControlRole::Navigation) return false;
        const RECT client = FormClient(c.hwnd);
        const SIZE size{client.right - client.left, client.bottom - client.top};
        if (size.cx <= 0 || size.cy <= 0) return true;
        const auto rect = ToDip(client);
        RECT pending{};
        const bool stale = !c.visual || c.visualSize.cx != size.cx || c.visualSize.cy != size.cy || TweensRunning(c, now) ||
                           ::GetUpdateRect(c.hwnd, &pending, FALSE) != FALSE;
        if (stale) {
            c.visual = nullptr;
            winrt::com_ptr<ID2D1BitmapRenderTarget> layer;
            const D2D1_SIZE_F dipSize{rect.right - rect.left, rect.bottom - rect.top};
            const D2D1_SIZE_U pixelSize{static_cast<UINT32>(size.cx), static_cast<UINT32>(size.cy)};
            if (FAILED(d2d->CreateCompatibleRenderTarget(&dipSize, &pixelSize, nullptr, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,
                                                         layer.put()))) {
                return false;
            }
            auto layerContext = layer.try_as<ID2D1DeviceContext>();
            if (!layerContext) return false;
            layerContext->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
            layerContext->BeginDraw();
            layerContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
            layerContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            layerContext->SetTransform(D2D1::Matrix3x2F::Translation(-rect.left, -rect.top));
            DrawSelf(layerContext.get(), c, now);
            if (FAILED(layerContext->EndDraw()) || FAILED(layer->GetBitmap(c.visual.put()))) {
                c.visual = nullptr;
                return false;
            }
            c.visualSize = size;
            ++stats.visualsRendered;
        } else {
            ++stats.visualsReused;
        }
        d2d->DrawBitmap(c.visual.get(), rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        return true;
    }

    void DropVisualCaches() noexcept {
        for (auto& control : controls) control->visual = nullptr;
    }

    bool Present(HWND container, HWND moving = nullptr, LONG dy = 0, const std::function<void()>& move = {}) {
        ControlState* self = Find(container);
        if (!self || self->spec.role != ControlRole::Container || !Styled() || !TopLevelShown() || !::IsWindowVisible(container) ||
            !EnsureTarget()) {
            return false;
        }
        RECT client{};
        ::GetClientRect(container, &client);
        if (::IsRectEmpty(&client)) {
            if (move) move();
            return true;
        }
        bool frameCreated = false;
        if (!composed.Ensure(client.right, client.bottom, frameCreated)) return false;
        const double started = Now();
        const RegionOverlay* overlay = nullptr;
        HWND overlayWindow = nullptr;
        for (const auto& entry : overlays) {
            if (entry.container == container && entry.overlay) {
                overlay = &entry.overlay;
                overlayWindow = entry.window;
            }
        }
        deep = true;
        pendingWindow = move ? moving : nullptr;
        pendingDy = move ? dy : 0;
        try {
            // ClearType over opaque material, or no retained layers: the full scene, rendered at once.
            if (material != FormMaterial::Acrylic || !ComposeRetained(container, client, overlay, overlayWindow, move ? dy : 0))
            {
                if (material == FormMaterial::Acrylic) ++stats.retainedFallbacks;
                RenderScene(container, composed.dc, client, overlay);
            }
        } catch (...) {
            deep = false;
            pendingWindow = nullptr;
            pendingDy = 0;
            InvalidateLayers();
            if (move) move();
            throw;
        }
        deep = false;
        pendingWindow = nullptr;
        pendingDy = 0;
        const double moveStarted = Now();
        stats.stageSeconds[0] += moveStarted - started;
        if (move) move();  // the windows move now; the finished frame follows immediately
        stats.stageSeconds[1] += Now() - moveStarted;
        // No DCX_CLIPCHILDREN: this one copy covers the descendants' pixels as well. The DC is taken
        // after the move so its visible region describes the final window arrangement.
        HDC hdc = ::GetDCEx(container, nullptr, DCX_CACHE | DCX_CLIPSIBLINGS);
        if (!hdc) {
            ::RedrawWindow(container, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
            return true;
        }
        const double copyStarted = Now();
        ::BitBlt(hdc, 0, 0, client.right, client.bottom, composed.dc, 0, 0, SRCCOPY);
        ::ReleaseDC(container, hdc);
        const double copied = Now();
        stats.stageSeconds[2] += copied - copyStarted;
        ::ValidateRect(container, nullptr);
        for (const auto& control : controls) {
            const ControlState& c = *control;
            if (c.hwnd == container || Welled(c.spec.role) || !SelfPainted(c.spec.role) || !::IsChild(container, c.hwnd)) continue;
            ::ValidateRect(c.hwnd, nullptr);
        }
        RepaintForeignChildren(container, overlayWindow);
        if (overlayWindow) ::ValidateRect(overlayWindow, nullptr);
        const double finished = Now();
        stats.stageSeconds[3] += finished - copied;
        Record(finished - started);
        return true;
    }

    ContainerLayers& LayersFor(HWND container) {
        for (auto& layer : layers) {
            if (layer->container == container) return *layer;
        }
        auto created = std::make_unique<ContainerLayers>();
        created->container = container;
        layers.push_back(std::move(created));
        return *layers.back();
    }

    // Adds `hwnd`'s pending update region (its client pixels awaiting WM_PAINT) to `region`, in the
    // client coordinates of `container`.
    static void AddUpdateRegion(HWND hwnd, HWND container, HRGN region) {
        HRGN update = ::CreateRectRgn(0, 0, 0, 0);
        if (!update) return;
        if (::GetUpdateRgn(hwnd, update, FALSE) > NULLREGION) {
            POINT offset{0, 0};
            ::MapWindowPoints(hwnd, container, &offset, 1);
            ::OffsetRgn(update, offset.x, offset.y);
            ::CombineRgn(region, region, update, RGN_OR);
        }
        ::DeleteObject(update);
    }

    // Premultiplied source-over of `count` pixels: dst = src * k + dst * (1 - srcA * k), k = alpha/255.
    // Two channels per 32-bit multiply; exact to the rounding of an 8-bit blend.
    static void BlendOver(std::uint32_t* dst, const std::uint32_t* src, std::size_t count, std::uint32_t alpha) noexcept {
        const auto scale = [](std::uint32_t pixel, std::uint32_t factor) noexcept {
            std::uint32_t rb = (pixel & 0x00FF00FFu) * factor + 0x00800080u;
            rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
            std::uint32_t ag = ((pixel >> 8) & 0x00FF00FFu) * factor + 0x00800080u;
            ag = (ag + ((ag >> 8) & 0x00FF00FFu)) & 0xFF00FF00u;
            return rb | ag;
        };
        std::size_t i = 0;
#if defined(_M_X64) || defined(_M_IX86)
        // Four pixels at a time with 16-bit lanes; the same rounding as `scale`.
        const __m128i zero = _mm_setzero_si128();
        const __m128i full = _mm_set1_epi16(255);
        const __m128i half = _mm_set1_epi16(128);
        const __m128i constant = _mm_set1_epi16(static_cast<short>(alpha));
        const auto divide = [&](__m128i value) {  // value / 255, rounded
            value = _mm_add_epi16(value, half);
            return _mm_srli_epi16(_mm_add_epi16(value, _mm_srli_epi16(value, 8)), 8);
        };
        const auto alphas = [](__m128i pixels) { return _mm_shufflehi_epi16(_mm_shufflelo_epi16(pixels, 0xFF), 0xFF); };
        for (; i + 4 <= count; i += 4) {
            const __m128i source = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
            if (_mm_movemask_epi8(_mm_cmpeq_epi32(source, zero)) == 0xFFFF) continue;  // all transparent
            __m128i sLo = _mm_unpacklo_epi8(source, zero);
            __m128i sHi = _mm_unpackhi_epi8(source, zero);
            if (alpha < 255) {
                sLo = divide(_mm_mullo_epi16(sLo, constant));
                sHi = divide(_mm_mullo_epi16(sHi, constant));
            }
            const __m128i target = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst + i));
            const __m128i dLo = divide(_mm_mullo_epi16(_mm_unpacklo_epi8(target, zero), _mm_sub_epi16(full, alphas(sLo))));
            const __m128i dHi = divide(_mm_mullo_epi16(_mm_unpackhi_epi8(target, zero), _mm_sub_epi16(full, alphas(sHi))));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), _mm_packus_epi16(_mm_add_epi16(sLo, dLo), _mm_add_epi16(sHi, dHi)));
        }
#endif
        for (; i < count; ++i) {
            std::uint32_t source = src[i];
            if (alpha < 255) source = scale(source, alpha);
            const std::uint32_t a = source >> 24;
            if (a == 0) continue;
            dst[i] = a == 255 ? source : source + scale(dst[i], 255 - a);
        }
    }

    IWICImagingFactory* Wic() {
        if (!wic && !wicUnavailable) {
            if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory),
                                          wic.put_void()))) {
                wic = nullptr;
                wicUnavailable = true;  // e.g. no COM on this thread: Present renders the full scene instead
            }
        }
        return wic.get();
    }

    // The retained composition: cached material, the content layer brought up to date (shifted by
    // the move, then only exposed and changed rectangles rendered), its fade, and the overlay.
    // Returns false when the retained layers are unavailable (the caller renders the full scene).
    // `composeFrame` false only brings the retained layers up to date (idle preparation).
    bool ComposeRetained(HWND container, const RECT& client, const RegionOverlay* overlay, HWND overlayWindow, LONG dy,
                         bool composeFrame = true) {
        ContainerLayers& layer = LayersFor(container);
        bool backgroundCreated = false, scratchCreated = false;
        const auto pixelCount = static_cast<std::size_t>(client.right) * static_cast<std::size_t>(client.bottom);
        const bool contentCreated = layer.content.size() != pixelCount;
        if (contentCreated) layer.content.assign(pixelCount, 0u);
        if (!layer.background.Ensure(Wic(), factory.get(), client.right, client.bottom, dpi, backgroundCreated) ||
            !layer.scratch.Ensure(Wic(), factory.get(), client.right, client.bottom, dpi, scratchCreated)) {
            std::erase_if(layers, [container](const auto& entry) { return entry->container == container; });
            return false;
        }
        const POINT origin = FormOrigin(container);
        if (!layer.backgroundValid) ++stats.materialReasons[3];
        if (backgroundCreated) ++stats.materialReasons[0];
        if (origin.x != layer.origin.x || origin.y != layer.origin.y) {
            ++stats.materialReasons[1];
            stats.lastOriginFrom = layer.origin;
            stats.lastOriginTo = origin;
        }
        if (layer.dpi != dpi) ++stats.materialReasons[2];
        if (backgroundCreated || origin.x != layer.origin.x || origin.y != layer.origin.y || layer.dpi != dpi) layer.backgroundValid = false;
        if (contentCreated || scratchCreated || layer.dpi != dpi) {
            layer.contentValid = false;
            DropVisualCaches();  // they belong to the previous scratch target
        }
        layer.dpi = dpi;
        double mark = Now();
        const auto lap = [&](std::size_t index) {
            const double now = Now();
            stats.composeSeconds[index] += now - mark;
            mark = now;
        };
        if (!layer.backgroundValid) {
            ++stats.backgroundRenders;
            auto* d2d = layer.background.context.get();
            d2d->BeginDraw();
            DrawSceneLayer(d2d, container, client, client, nullptr, SceneLayer::Background);
            if (FAILED(d2d->EndDraw())) {
                std::erase_if(layers, [container](const auto& entry) { return entry->container == container; });
                return false;
            }
            layer.origin = origin;
            layer.backgroundValid = true;
        }
        lap(0);
        HRGN dirty = ::CreateRectRgn(0, 0, 0, 0);
        if (!dirty) throw std::bad_alloc();
        if (!layer.contentValid) {
            ++stats.contentRebuilds;
            ::SetRectRgn(dirty, 0, 0, client.right, client.bottom);
        } else {
            if (layer.dirty) ::CombineRgn(dirty, dirty, layer.dirty, RGN_OR);
            AddUpdateRegion(container, container, dirty);
            for (const auto& control : controls) {
                const ControlState& c = *control;
                if (Welled(c.spec.role) || !::IsChild(container, c.hwnd) || !::IsWindowVisible(c.hwnd)) continue;
                AddUpdateRegion(c.hwnd, container, dirty);
            }
            lap(1);
            if (dy != 0 && std::abs(dy) < client.bottom) {
                const auto width = static_cast<std::size_t>(client.right);
                const auto rows = static_cast<std::size_t>(client.bottom - std::abs(dy));
                std::uint32_t* pixels = layer.content.data();
                if (dy < 0) std::memmove(pixels, pixels + static_cast<std::size_t>(-dy) * width, rows * width * 4);
                else std::memmove(pixels + static_cast<std::size_t>(dy) * width, pixels, rows * width * 4);
                ::OffsetRgn(dirty, 0, dy);
                HRGN exposed = dy < 0 ? ::CreateRectRgn(0, client.bottom + dy, client.right, client.bottom)
                                      : ::CreateRectRgn(0, 0, client.right, dy);
                if (exposed) {
                    ::CombineRgn(dirty, dirty, exposed, RGN_OR);
                    ::DeleteObject(exposed);
                }
            } else if (dy != 0) {
                ::SetRectRgn(dirty, 0, 0, client.right, client.bottom);
            }
        }
        if (layer.dirty) {
            ::DeleteObject(layer.dirty);
            layer.dirty = nullptr;
        }
        lap(2);
        // Changed rectangles of the content, and the overlay column (drawn in the same pass).
        std::vector<RECT> rects;
        RECT bounds{};
        if (::GetRgnBox(dirty, &bounds) != NULLREGION) {
            const DWORD bytes = ::GetRegionData(dirty, 0, nullptr);
            if (bytes) {
                std::vector<std::byte> data(bytes);
                auto* region = reinterpret_cast<RGNDATA*>(data.data());
                if (::GetRegionData(dirty, bytes, region) && region->rdh.nCount <= 12) {
                    const auto* first = reinterpret_cast<const RECT*>(region->Buffer);
                    rects.assign(first, first + region->rdh.nCount);
                }
            }
            if (rects.empty()) rects.push_back(bounds);
            for (auto& rect : rects) ::IntersectRect(&rect, &rect, &client);
            std::erase_if(rects, [](const RECT& rect) { return ::IsRectEmpty(&rect) != FALSE; });
        }
        ::DeleteObject(dirty);
        RECT strip{};
        if (overlay && *overlay && overlayWindow && ::IsWindowVisible(overlayWindow)) {
            strip = MapRect(overlayWindow, container, ClientIn(overlayWindow, overlayWindow));
            ::IntersectRect(&strip, &strip, &client);
        }
        const bool overlaid = !::IsRectEmpty(&strip);
        std::optional<LockedPixels> scratch;
        if (!rects.empty() || overlaid) {
            auto* d2d = layer.scratch.context.get();
            d2d->BeginDraw();
            retained = true;
            presenting = true;
            BuildDrawList(container);
            for (const RECT& rect : rects) DrawSceneLayer(d2d, container, rect, rect, nullptr, SceneLayer::Content);
            presenting = false;
            drawListRoot = nullptr;
            retained = false;
            if (overlaid) {
                const float s = Scale();
                d2d->SetTransform(D2D1::IdentityMatrix());
                d2d->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(strip.left) / s, static_cast<float>(strip.top) / s,
                                                     static_cast<float>(strip.right) / s, static_cast<float>(strip.bottom) / s),
                                         D2D1_ANTIALIAS_MODE_ALIASED);
                d2d->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
                try {
                    (*overlay)(d2d, tokens);  // drawn in the container's client DIPs
                } catch (...) {
                    AETHER_LOG_ERROR("ui_form", "region overlay threw");
                }
                d2d->PopAxisAlignedClip();
            }
            if (FAILED(d2d->EndDraw())) {
                std::erase_if(layers, [container](const auto& entry) { return entry->container == container; });
                return false;
            }
            scratch.emplace(layer.scratch);
            if (!scratch->bits) {
                layer.contentValid = false;
                return false;
            }
            // The content never reaches into the overlay column (the scroll gutter is kept free).
            const auto width = static_cast<std::size_t>(client.right);
            for (RECT rect : rects) {
                if (overlaid && rect.right > strip.left && rect.left < strip.right) rect.right = std::max(rect.left, strip.left);
                const auto span = static_cast<std::size_t>(std::max<LONG>(0, rect.right - rect.left)) * 4;
                if (!span) continue;
                for (LONG y = rect.top; y < rect.bottom; ++y) {
                    std::memcpy(layer.content.data() + static_cast<std::size_t>(y) * width + static_cast<std::size_t>(rect.left),
                                scratch->Row(y) + rect.left, span);
                }
            }
        }
        layer.contentValid = true;
        lap(3);
        if (!composeFrame) return true;
        // Compose: material, then the content at the container's fade, then the overlay.
        ::GdiFlush();
        const auto width = static_cast<std::size_t>(client.right);
        const float fade = std::clamp(FadeOf(container), 0.0f, 1.0f);
        {
            LockedPixels background(layer.background);
            if (!background.bits) return false;
            for (LONG y = 0; y < client.bottom; ++y) {
                std::uint32_t* row = composed.bits + static_cast<std::size_t>(y) * width;
                std::memcpy(row, background.Row(y), width * 4);
            }
            lap(4);
            if (fade > 0.0f) BlendOver(composed.bits, layer.content.data(), layer.content.size(), AlphaByte(fade));
        }
        lap(5);
        if (overlaid && scratch && scratch->bits) {
            for (LONG y = strip.top; y < strip.bottom; ++y) {
                BlendOver(composed.bits + static_cast<std::size_t>(y) * width + static_cast<std::size_t>(strip.left),
                          scratch->Row(y) + strip.left, static_cast<std::size_t>(strip.right - strip.left), 255);
            }
        }
        lap(6);
        return true;
    }

    // Unregistered, unlayered descendants are not part of the scene: they repaint over it.
    void RepaintForeignChildren(HWND container, HWND overlayWindow) {
        struct Walk {
            Impl* self;
            HWND container;
            HWND overlay;
        } walk{this, container, overlayWindow};
        ::EnumChildWindows(
            container,
            [](HWND child, LPARAM parameter) -> BOOL {
                auto& w = *reinterpret_cast<Walk*>(parameter);
                if (child == w.overlay || w.self->Find(child) || !::IsWindowVisible(child)) return TRUE;
                for (HWND walkUp = child; walkUp && walkUp != w.container; walkUp = ::GetParent(walkUp)) {
                    if (::GetWindowLongPtrW(walkUp, GWL_EXSTYLE) & WS_EX_LAYERED) return TRUE;  // its own surface
                }
                ::InvalidateRect(child, nullptr, FALSE);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&walk));
    }

    void Record(double seconds) noexcept {
        ++stats.frames;
        stats.totalSeconds += seconds;
        stats.maxSeconds = std::max(stats.maxSeconds, seconds);
        const auto bucket = static_cast<std::size_t>(std::max(0.0, seconds * 2000.0));
        ++stats.histogram[std::min(bucket, stats.histogram.size() - 1)];
    }

    void DrawSelf(ID2D1DeviceContext* d2d, ControlState& c, double now) {
        const auto client = ToDip(FormClient(c.hwnd));
        const auto visual = VisualOf(c, now);
        const float margin = c.hasFrame ? tokens.form.focusMargin : 0.0f;
        const auto inner = D2D1::RectF(client.left + margin, client.top + margin, client.right - margin, client.bottom - margin);
        switch (c.spec.role) {
            case ControlRole::Label: {
                std::wstring value = WindowString(c.hwnd);
                if (::GetWindowLongPtrW(c.hwnd, GWL_STYLE) & SS_NOPREFIX) {
                    std::wstring escaped;
                    for (const wchar_t ch : value) {
                        escaped.push_back(ch);
                        if (ch == L'&') escaped.push_back(L'&');
                    }
                    value = std::move(escaped);
                }
                float angle = 0.0f;
                if (Continuous(c)) {
                    angle = static_cast<float>(std::fmod((now - c.start) * 360.0, 360.0));
                    if (!c.animating) StartAnimation(c);
                }
                paint::PaintLabel(d2d, text, client, value, c.spec, visual.enabled, visual.showMnemonic, angle, tokens);
                break;
            }
            case ControlRole::Button: {
                auto v = visual;
                if (!c.hasFrame) {
                    v.focusRing = false;  // drawn inside below
                }
                paint::PaintButton(d2d, text, inner, WindowString(c.hwnd), c.spec.glyph, c.spec.button, v, tokens,
                                   (::GetWindowLongPtrW(c.hwnd, GWL_STYLE) & BS_MULTILINE) != 0);
                if (!c.hasFrame && visual.focusRing) paint::PaintInnerFocusRing(d2d, client, tokens.form.controlRadius, tokens);
                break;
            }
            case ControlRole::Switch:
                paint::PaintSwitchRow(d2d, text, client, WindowString(c.hwnd), c.spec.description, c.on.Value(now), visual, tokens);
                break;
            case ControlRole::Check: {
                const int state = static_cast<int>(::SendMessageW(c.hwnd, BM_GETCHECK, 0, 0));
                paint::PaintCheckRow(d2d, text, client, WindowString(c.hwnd), c.spec.description,
                                     state == BST_CHECKED ? 1 : state == BST_INDETERMINATE ? 2 : 0, visual, tokens);
                break;
            }
            case ControlRole::ComboBox: {
                const auto index = static_cast<int>(::SendMessageW(c.hwnd, CB_GETCURSEL, 0, 0));
                const std::wstring value = index >= 0 ? ListText(c.hwnd, index, CB_GETLBTEXTLEN, CB_GETLBTEXT) : std::wstring();
                auto v = visual;
                v.pressed = std::max(v.pressed, ::SendMessageW(c.hwnd, CB_GETDROPPEDSTATE, 0, 0) ? 1.0f : 0.0f);
                paint::PaintComboField(d2d, text, inner, value, v, tokens);
                break;
            }
            case ControlRole::Slider: {
                RECT channel{};
                RECT thumb{};
                ::SendMessageW(c.hwnd, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
                ::SendMessageW(c.hwnd, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
                auto v = visual;
                v.pressed = std::max(v.pressed, ::GetCapture() == c.hwnd ? 1.0f : 0.0f);
                paint::PaintSlider(d2d, ToDip(ToForm(c.hwnd, channel)), ToDip(ToForm(c.hwnd, thumb)), v, tokens);
                break;
            }
            case ControlRole::Progress: {
                const auto fraction = ProgressFraction(c);
                double phase = -1.0;
                if (!fraction && motion.AllowsMovement()) {
                    const double cycle = std::max(0.2, Duration(tokens.form.progressCycle));
                    phase = std::fmod((now - c.start) / cycle, 1.0);
                    if (Continuous(c) && !c.animating) StartAnimation(c);
                }
                paint::PaintProgress(d2d, client, fraction, phase, visual.enabled, tokens);
                break;
            }
            case ControlRole::Navigation:
                DrawNavigation(d2d, c, visual, now);
                break;
            default:
                break;
        }
    }

    void DrawNavigation(ID2D1DeviceContext* d2d, ControlState& c, const paint::Visual& visual, double now) {
        RECT client{};
        ::GetClientRect(c.hwnd, &client);
        POINT origin{0, 0};
        origin = FormOrigin(c.hwnd);
        const float s = Scale();
        const float ox = static_cast<float>(origin.x) / s;
        const float oy = static_cast<float>(origin.y) / s;
        const int count = static_cast<int>(::SendMessageW(c.hwnd, LB_GETCOUNT, 0, 0));
        const int top = std::max(0, static_cast<int>(::SendMessageW(c.hwnd, LB_GETTOPINDEX, 0, 0)));
        const int selected = static_cast<int>(::SendMessageW(c.hwnd, LB_GETCURSEL, 0, 0));
        const int caret = static_cast<int>(::SendMessageW(c.hwnd, LB_GETCARETINDEX, 0, 0));
        const bool glyphColumn = !c.glyphs.empty();
        std::optional<D2D1_RECT_F> caretRow;
        std::optional<D2D1_RECT_F> anyPill;
        for (int index = top; index < count; ++index) {
            RECT item{};
            if (::SendMessageW(c.hwnd, LB_GETITEMRECT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&item)) == LB_ERR) break;
            if (item.top >= client.bottom) break;
            const auto row = ToDip(ToForm(c.hwnd, item));
            std::optional<graphics::Glyph> glyph;
            if (static_cast<std::size_t>(index) < c.glyphs.size()) glyph = c.glyphs[static_cast<std::size_t>(index)];
            paint::PaintNavigationRow(d2d, text, row, ListText(c.hwnd, index, LB_GETTEXTLEN, LB_GETTEXT), glyph, glyphColumn,
                                      index == selected, RowHoverAmount(c, index, now), c.compact, visual.enabled, tokens);
            if (index == caret) caretRow = row;
            if (!anyPill) anyPill = paint::NavigationPillRect(row);
        }
        if (selected >= 0 && anyPill) {
            if (c.selected != selected || c.selected < -1) RefreshNavigation(c);
            const float center = c.indicator.Value(now) + oy;
            const auto indicator = paint::NavigationIndicatorRect(*anyPill, center, tokens);
            graphics::PaintAccentIndicator(d2d, indicator, tokens);
        }
        (void)ox;
        if (caretRow && ::GetFocus() == c.hwnd && detail::ShowsFocusCues(c.hwnd)) {
            paint::PaintInnerFocusRing(d2d, *caretRow, tokens.form.controlRadius + 2.0f, tokens);
        }
    }

    // --- native (high contrast / no renderer) painting with GDI in system colors ---
    void PaintNative(HWND hwnd, HDC hdc, const RECT& clip) {
        ::FillRect(hdc, &clip, ::GetSysColorBrush(COLOR_WINDOW));
        const int saved = ::SaveDC(hdc);
        HPEN pen = ::CreatePen(PS_SOLID, 1, ::GetSysColor(COLOR_WINDOWTEXT));
        ::SelectObject(hdc, pen);
        ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
        const int radius = Px(tokens.form.cardRadius) * 2;
        for (const auto& card : cards) {
            RECT r = card.px;
            if (card.canvas) {
                if (!::IsWindow(card.canvas) || !::IsWindowVisible(card.canvas)) continue;
                r = MapRect(card.canvas, hwnd, r);
            } else {
                r = MapRect(window, hwnd, r);
            }
            RECT overlap{};
            if (!::IntersectRect(&overlap, &r, &clip)) continue;
            ::RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius, radius);
        }
        for (const auto& divider : dividers) {
            RECT r = divider.canvas ? (::IsWindow(divider.canvas) ? MapRect(divider.canvas, hwnd, divider.px) : RECT{})
                                    : MapRect(window, hwnd, divider.px);
            ::FillRect(hdc, &r, ::GetSysColorBrush(COLOR_WINDOWTEXT));
        }
        // Switch/Check descriptions below the stock check box (see ChildRectFor).
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, ::GetSysColor(COLOR_WINDOWTEXT));
        ::SelectObject(hdc, NativeFont(TextRole::Helper));
        const int indent = platform::SystemMetricForDpi(SM_CXMENUCHECK, dpi) + Px(6.0f);
        for (const auto& control : controls) {
            const ControlState& c = *control;
            if (!IsRole(c.spec.role, {ControlRole::Switch, ControlRole::Check}) || c.spec.description.empty() || !c.hasFrame ||
                ::GetParent(c.hwnd) != hwnd || !::IsWindowVisible(c.hwnd)) {
                continue;
            }
            const RECT box = ChildRectFor(c, c.frame);
            RECT r{c.frame.left + indent, box.bottom, c.frame.right, c.frame.bottom};
            ::DrawTextW(hdc, c.spec.description.c_str(), static_cast<int>(c.spec.description.size()), &r,
                        DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        ::RestoreDC(hdc, saved);
        ::DeleteObject(pen);
    }

    // --- child notifications (form and containers) ---
    std::optional<LRESULT> ChildNotification(HWND parent, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLORBTN:
                return ControlColor(message, reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam));
            case WM_DRAWITEM:
                return DrawItem(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            case WM_MEASUREITEM:
                return MeasureItem(parent, *reinterpret_cast<MEASUREITEMSTRUCT*>(lParam));
            default:
                return std::nullopt;
        }
    }

    ControlState* ComboOfList(HWND list) const noexcept {
        for (const auto& control : controls) {
            if (control->spec.role != ControlRole::ComboBox) continue;
            COMBOBOXINFO info{sizeof(info)};
            if (::GetComboBoxInfo(control->hwnd, &info) && info.hwndList == list) return control.get();
        }
        return nullptr;
    }

    std::optional<LRESULT> ControlColor(UINT message, HDC hdc, HWND child) {
        ControlState* c = Find(child);
        if (!c && message == WM_CTLCOLORLISTBOX) c = ComboOfList(child);
        if (!c) return std::nullopt;
        if (!Styled()) {
            if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) return std::nullopt;
            ::SetTextColor(hdc, ::GetSysColor(::IsWindowEnabled(child) ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
            ::SetBkColor(hdc, ::GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<LRESULT>(::GetSysColorBrush(COLOR_WINDOW));
        }
        const ControlRole role = c->spec.role;
        const bool editor = role == ControlRole::Field || role == ControlRole::Document;
        if ((message == WM_CTLCOLOREDIT || message == WM_CTLCOLORSTATIC) && editor) {
            const bool enabled = ::IsWindowEnabled(child) != FALSE;
            ::SetTextColor(hdc, (enabled ? tokens.form.fieldText : tokens.textDisabled).ToColorRef());
            ::SetBkColor(hdc, (enabled ? tokens.form.fieldFill : tokens.form.fieldFillDisabled).ToColorRef());
            ::SetBkMode(hdc, OPAQUE);
            return reinterpret_cast<LRESULT>(enabled ? fieldBrush : fieldDisabledBrush);
        }
        if (message == WM_CTLCOLORLISTBOX && (role == ControlRole::List || role == ControlRole::ComboBox)) {
            ::SetTextColor(hdc, tokens.form.fieldText.ToColorRef());
            ::SetBkColor(hdc, tokens.form.fieldFill.ToColorRef());
            return reinterpret_cast<LRESULT>(fieldBrush);
        }
        // Scene-painted controls never use a GDI background.
        return reinterpret_cast<LRESULT>(::GetStockObject(NULL_BRUSH));
    }

    std::optional<LRESULT> MeasureItem(HWND parent, MEASUREITEMSTRUCT& item) {
        if (item.CtlType != ODT_LISTBOX) return std::nullopt;
        for (const auto& control : controls) {
            if (::GetParent(control->hwnd) != parent || static_cast<UINT>(::GetDlgCtrlID(control->hwnd)) != item.CtlID) continue;
            if (control->spec.role == ControlRole::Navigation) {
                item.itemHeight = static_cast<UINT>(ListItemHeight(true));
                return TRUE;
            }
            if (control->spec.role == ControlRole::List) {
                item.itemHeight = static_cast<UINT>(ListItemHeight(false));
                return TRUE;
            }
        }
        return std::nullopt;
    }

    std::optional<LRESULT> DrawItem(const DRAWITEMSTRUCT& item) {
        ControlState* c = Find(item.hwndItem);
        if (!c) return std::nullopt;
        switch (c->spec.role) {
            case ControlRole::Label:
                if (Styled()) {
                    ::InvalidateRect(c->hwnd, nullptr, FALSE);
                } else {
                    DrawNativeLabel(*c, item);
                }
                return TRUE;
            case ControlRole::Navigation:
                if (Styled()) {
                    // Rows are painted from the scene in the list's WM_PAINT; a direct draw only asks for it.
                    ::InvalidateRect(c->hwnd, nullptr, FALSE);
                } else {
                    DrawNativeRow(item);
                }
                return TRUE;
            case ControlRole::List:
                if (Styled() && EnsureTarget()) {
                    DrawListRow(*c, item);
                } else {
                    DrawNativeRow(item);
                }
                return TRUE;
            default:
                return std::nullopt;
        }
    }

    void DrawNativeLabel(const ControlState& c, const DRAWITEMSTRUCT& item) {
        HDC hdc = item.hDC;
        ::FillRect(hdc, &item.rcItem, ::GetSysColorBrush(COLOR_WINDOW));
        const int saved = ::SaveDC(hdc);
        ::SelectObject(hdc, NativeFont(c.spec.text));
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, ::GetSysColor(::IsWindowEnabled(c.hwnd) ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
        const std::wstring value = WindowString(c.hwnd);
        UINT format = c.spec.wrap ? DT_WORDBREAK : (DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        if (c.spec.trailing) format |= DT_RIGHT;
        if (::GetWindowLongPtrW(c.hwnd, GWL_STYLE) & SS_NOPREFIX) format |= DT_NOPREFIX;
        if (!detail::ShowsAccelerators(c.hwnd)) format |= DT_HIDEPREFIX;
        RECT r = item.rcItem;
        ::DrawTextW(hdc, value.c_str(), static_cast<int>(value.size()), &r, format);
        ::RestoreDC(hdc, saved);
    }

    void DrawNativeRow(const DRAWITEMSTRUCT& item) {
        HDC hdc = item.hDC;
        const bool selected = (item.itemState & ODS_SELECTED) != 0;
        const bool disabled = (item.itemState & ODS_DISABLED) != 0;
        ::FillRect(hdc, &item.rcItem, ::GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        if (item.itemID != static_cast<UINT>(-1)) {
            const std::wstring value = ListText(item.hwndItem, static_cast<int>(item.itemID), LB_GETTEXTLEN, LB_GETTEXT);
            const int saved = ::SaveDC(hdc);
            ::SelectObject(hdc, font);
            ::SetBkMode(hdc, TRANSPARENT);
            ::SetTextColor(hdc, ::GetSysColor(disabled ? COLOR_GRAYTEXT : selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
            RECT r = item.rcItem;
            r.left += Px(8.0f);
            ::DrawTextW(hdc, value.c_str(), static_cast<int>(value.size()), &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            ::RestoreDC(hdc, saved);
        }
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) ::DrawFocusRect(hdc, &item.rcItem);
    }

    void DrawListRow(ControlState& c, const DRAWITEMSTRUCT& item) {
        RECT rect = item.rcItem;
        if (::IsRectEmpty(&rect)) return;
        if (item.itemID == static_cast<UINT>(-1)) {
            ::FillRect(item.hDC, &rect, fieldBrush);
            return;
        }
        HDC memory = nullptr;
        HPAINTBUFFER buffer = ::BeginBufferedPaint(item.hDC, &rect, BPBF_TOPDOWNDIB, nullptr, &memory);
        HDC dc = buffer ? memory : item.hDC;
        if (SUCCEEDED(target->BindDC(dc, &rect))) {
            const float s = Scale();
            target->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
            context->BeginDraw();
            context->SetTransform(D2D1::Matrix3x2F::Translation(-static_cast<float>(rect.left) / s, -static_cast<float>(rect.top) / s));
            context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            const bool cues = detail::ShowsFocusCues(c.hwnd);
            paint::PaintListRow(context.get(), text, ToDip(rect),
                                ListText(c.hwnd, static_cast<int>(item.itemID), LB_GETTEXTLEN, LB_GETTEXT),
                                (item.itemState & ODS_SELECTED) != 0, RowHoverAmount(c, static_cast<int>(item.itemID), Now()),
                                (item.itemState & ODS_FOCUS) != 0 && !(item.itemState & ODS_NOFOCUSRECT) && cues,
                                !(item.itemState & ODS_DISABLED), tokens);
            if (context->EndDraw() == D2DERR_RECREATE_TARGET) {
                target = nullptr;
                context = nullptr;
            }
        }
        if (buffer) ::EndBufferedPaint(buffer, TRUE);
    }

    // --- control window procedure ---
    // The native HOTKEY control paints white with the window text color whatever WM_CTLCOLOR*
    // answers. Its input, caret and HKM_* behavior stay native; only the drawing is replaced with the
    // field colors and the same localized key names the control shows ("Ctrl + Shift + K").
    static std::wstring KeyName(UINT virtualKey) {
        UINT scan = ::MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
        switch (virtualKey) {
            case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT: case VK_END:
            case VK_HOME: case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
                scan |= 0x100;  // extended key
                break;
            default:
                break;
        }
        wchar_t name[64]{};
        const int length = ::GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 64);
        return length > 0 ? std::wstring(name, static_cast<std::size_t>(length)) : std::wstring();
    }

    void PaintHotkey(ControlState& c, HDC hdc) {
        RECT client{};
        ::GetClientRect(c.hwnd, &client);
        const bool enabled = ::IsWindowEnabled(c.hwnd) != FALSE;
        ::FillRect(hdc, &client, enabled ? fieldBrush : fieldDisabledBrush);
        const auto value = static_cast<WORD>(::SendMessageW(c.hwnd, HKM_GETHOTKEY, 0, 0));
        const UINT key = LOBYTE(value);
        const UINT modifiers = HIBYTE(value);
        std::wstring label;
        const auto append = [&label](const std::wstring& part) {
            if (part.empty()) return;
            if (!label.empty()) label += L" + ";
            label += part;
        };
        if (key) {
            if (modifiers & HOTKEYF_CONTROL) append(KeyName(VK_CONTROL));
            if (modifiers & HOTKEYF_SHIFT) append(KeyName(VK_SHIFT));
            if (modifiers & HOTKEYF_ALT) append(KeyName(VK_MENU));
            append(KeyName(key));
        }
        const HGDIOBJ previous = ::SelectObject(hdc, font);
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, (enabled ? tokens.form.fieldText : tokens.textDisabled).ToColorRef());
        TEXTMETRICW metrics{};
        ::GetTextMetricsW(hdc, &metrics);
        const int x = Px(2.0f);
        const int y = std::max(0, static_cast<int>((client.bottom - metrics.tmHeight) / 2));
        if (!label.empty()) ::ExtTextOutW(hdc, x, y, ETO_CLIPPED, &client, label.c_str(), static_cast<UINT>(label.size()), nullptr);
        SIZE extent{};
        if (!label.empty()) ::GetTextExtentPoint32W(hdc, label.c_str(), static_cast<int>(label.size()), &extent);
        ::SelectObject(hdc, previous);
        if (::GetFocus() == c.hwnd) ::SetCaretPos(x + extent.cx, y);
    }

    static LRESULT CALLBACK ControlProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR reference) {
        auto* impl = reinterpret_cast<Impl*>(reference);
        if (message == WM_NCDESTROY) {
            ::RemoveWindowSubclass(hwnd, &ControlProc, id);
            if (impl) impl->Forget(hwnd);
            return ::DefSubclassProc(hwnd, message, wParam, lParam);
        }
        if (!impl) return ::DefSubclassProc(hwnd, message, wParam, lParam);
        const std::weak_ptr<int> life = impl->life;
        try {
            if (ControlState* state = impl->Find(hwnd)) {
                if (auto handled = impl->ControlMessage(*state, message, wParam, lParam)) return *handled;
            }
        } catch (...) {
            AETHER_LOG_ERROR("ui_form", "control message {} failed", message);
        }
        const LRESULT result = ::DefSubclassProc(hwnd, message, wParam, lParam);
        if (life.expired()) return result;
        try {
            if (ControlState* state = impl->Find(hwnd)) impl->AfterMessage(*state, message, wParam, lParam);
        } catch (...) {
            AETHER_LOG_ERROR("ui_form", "control update after message {} failed", message);
        }
        return result;
    }

    void Forget(HWND hwnd) noexcept {
        std::erase_if(overlays, [hwnd](const auto& entry) { return entry.container == hwnd || entry.window == hwnd; });
        auto it = std::find_if(controls.begin(), controls.end(), [hwnd](const auto& c) { return c->hwnd == hwnd; });
        if (it == controls.end()) return;
        ClearHelp(**it);
        ClearName(**it);
        controls.erase(it);
    }

    std::optional<LRESULT> ControlMessage(ControlState& c, UINT message, WPARAM wParam, LPARAM lParam) {
        if (c.bypass) return std::nullopt;
        const bool styled = Styled();
        const ControlRole role = c.spec.role;
        const HWND hwnd = c.hwnd;

        if (role == ControlRole::Container) {
            switch (message) {
                case WM_COMMAND:
                case WM_NOTIFY:
                case WM_VKEYTOITEM:
                case WM_CHARTOITEM:
                case WM_COMPAREITEM:
                case WM_DELETEITEM:
                    // Owners see notifications of controls inside containers exactly as if the
                    // controls were direct children of the form window.
                    return ::SendMessageW(window, message, wParam, lParam);
                case WM_HSCROLL:
                case WM_VSCROLL:
                    if (lParam) return ::SendMessageW(window, message, wParam, lParam);
                    break;
                case WM_CTLCOLOREDIT:
                case WM_CTLCOLORSTATIC:
                case WM_CTLCOLORLISTBOX:
                case WM_CTLCOLORBTN:
                case WM_CTLCOLORSCROLLBAR:
                case WM_DRAWITEM:
                case WM_MEASUREITEM:
                    if (auto handled = ChildNotification(hwnd, message, wParam, lParam)) return handled;
                    return ::SendMessageW(window, message, wParam, lParam);
                default:
                    break;
            }
        }

        if (c.hotkey && styled) {
            if (message == WM_ERASEBKGND) return 1;
            if (message == WM_PAINT) {
                PAINTSTRUCT ps{};
                if (HDC hdc = ::BeginPaint(hwnd, &ps)) PaintHotkey(c, hdc);
                ::EndPaint(hwnd, &ps);
                return 0;
            }
            if (message == WM_PRINTCLIENT) {
                PaintHotkey(c, reinterpret_cast<HDC>(wParam));
                return 0;
            }
        }
        if (message == WM_ERASEBKGND) {
            if (styled && SelfPainted(role)) return 1;
            if (!styled && role == ControlRole::Container) {
                RECT client{};
                ::GetClientRect(hwnd, &client);
                ::FillRect(reinterpret_cast<HDC>(wParam), &client, ::GetSysColorBrush(COLOR_WINDOW));
                return 1;
            }
            return std::nullopt;
        }
        if (message == WM_PAINT && SelfPainted(role) && (styled || role == ControlRole::Container)) {
            c.visual = nullptr;  // it repaints itself: Present renders it afresh next time
            Paint(hwnd);
            return 0;
        }
        if (message == WM_PRINTCLIENT && SelfPainted(role) && (styled || role == ControlRole::Container)) {
            RECT client{};
            ::GetClientRect(hwnd, &client);
            PaintRegion(hwnd, reinterpret_cast<HDC>(wParam), client, nullptr);
            return 0;
        }

        if (c.masked) {
            switch (message) {
                case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_CHAR: case WM_LBUTTONDOWN: case WM_RBUTTONDOWN:
                case WM_MBUTTONDOWN: case WM_MOUSEWHEEL: case WM_IME_STARTCOMPOSITION:
                    Unmask(c);  // the user is working in this editor: show it live at once
                    break;
                default:
                    break;
            }
        }

        switch (message) {
            case WM_MOUSEMOVE:
                SetHover(c, true);
                if (role == ControlRole::Navigation || role == ControlRole::List) {
                    const auto hit = static_cast<DWORD>(::SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lParam));
                    SetRowHover(c, HIWORD(hit) ? -1 : static_cast<int>(LOWORD(hit)));
                }
                break;
            case WM_MOUSELEAVE:
                SetHover(c, false);
                if (role == ControlRole::Navigation || role == ControlRole::List) SetRowHover(c, -1);
                break;
            default:
                break;
        }

        if (role == ControlRole::Progress && message == WM_TIMER && styled) {
            // The native bar animates its own (replaced) painting with timers; the scene draws the
            // progress, so those timers would only wake the thread while the form is idle.
            ::KillTimer(hwnd, wParam);
            return 0;
        }
        if (role == ControlRole::Progress && message == PBM_SETMARQUEE) {
            c.indeterminate = wParam != 0;
            if (wParam) c.start = Now();
            if (styled) {
                ::DefSubclassProc(hwnd, PBM_SETMARQUEE, FALSE, lParam);  // the scene draws the sweep
                ::InvalidateRect(hwnd, nullptr, FALSE);
                KickContinuous();
                return TRUE;
            }
            if (wParam) SetStyleBits(c, PBS_MARQUEE, 0, 0, 0);
            return std::nullopt;
        }
        if (role == ControlRole::Progress &&
            (message == PBM_SETPOS || message == PBM_DELTAPOS || message == PBM_STEPIT || message == PBM_SETRANGE ||
             message == PBM_SETRANGE32)) {
            c.fraction.reset();
        }
        return std::nullopt;
    }

    // Messages after which an editor may look different (it often draws directly, without WM_PAINT).
    static bool ChangesEditorImage(UINT message, WPARAM wParam) noexcept {
        if (message == WM_MOUSEMOVE) return (wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) != 0;  // selection drag
        if (message >= WM_KEYFIRST && message <= WM_KEYLAST) return true;
        if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) return true;
        if (message >= EM_GETSEL && message <= EM_GETIMESTATUS) return message != EM_GETSEL && message != EM_GETLINE;
        if (message >= LB_ADDSTRING && message <= LB_GETLISTBOXINFO) return true;
        switch (message) {
            case WM_PAINT: case WM_SETTEXT: case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE: case WM_VSCROLL:
            case WM_HSCROLL: case WM_WINDOWPOSCHANGED: case WM_CUT: case WM_PASTE: case WM_CLEAR: case WM_UNDO:
            case WM_IME_COMPOSITION: case WM_IME_ENDCOMPOSITION: case HKM_SETHOTKEY: case WM_THEMECHANGED: case WM_SETFONT:
                return true;
            default:
                return false;
        }
    }

    // Messages after which a control's visual may have changed. Control messages share the WM_USER
    // range across classes (PBM_GETPOS == TBM_SETRANGEMAX), so they are matched per role; a "get"
    // message must never invalidate, since painting itself sends them.
    static bool StateChanging(ControlRole role, UINT message) noexcept {
        switch (message) {
            case WM_ENABLE: case WM_SETTEXT: case WM_SETFOCUS: case WM_KILLFOCUS:
            case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: case WM_MOUSEMOVE: case WM_MOUSELEAVE:
            case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
            case WM_CAPTURECHANGED: case WM_MOUSEWHEEL: case WM_VSCROLL: case WM_HSCROLL:
            case WM_UPDATEUISTATE: case WM_CHANGEUISTATE: case WM_STYLECHANGED: case WM_SHOWWINDOW:
            case WM_WINDOWPOSCHANGED: case WM_SETFONT: case WM_THEMECHANGED:
                return true;
            default:
                break;
        }
        switch (role) {
            case ControlRole::Button:
            case ControlRole::Switch:
            case ControlRole::Check:
                return message == BM_SETCHECK || message == BM_SETSTATE || message == BM_CLICK || message == BM_SETSTYLE;
            case ControlRole::ComboBox:
                return message == CB_SETCURSEL || message == CB_SELECTSTRING || message == CB_RESETCONTENT ||
                       message == CB_DELETESTRING || message == CB_INSERTSTRING || message == CB_ADDSTRING ||
                       message == CB_SHOWDROPDOWN;
            case ControlRole::Slider:
                return message == TBM_SETPOS || message == TBM_SETPOSNOTIFY || message == TBM_SETRANGE ||
                       message == TBM_SETRANGEMIN || message == TBM_SETRANGEMAX;
            case ControlRole::Progress:
                return message == PBM_SETPOS || message == PBM_DELTAPOS || message == PBM_STEPIT || message == PBM_SETRANGE ||
                       message == PBM_SETRANGE32 || message == PBM_SETSTATE;
            case ControlRole::List:
            case ControlRole::Navigation:
                return message == LB_SETCURSEL || message == LB_SETSEL || message == LB_SETCARETINDEX ||
                       message == LB_SETTOPINDEX || message == LB_ADDSTRING || message == LB_INSERTSTRING ||
                       message == LB_DELETESTRING || message == LB_RESETCONTENT;
            default:
                return false;
        }
    }

    void AfterMessage(ControlState& c, UINT message, WPARAM wParam, LPARAM lParam) {
        (void)lParam;
        const ControlRole role = c.spec.role;
        if (Welled(role) && Styled() && (c.addedExStyle & WS_EX_LAYERED) && ChangesEditorImage(message, wParam)) {
            if (c.masked) {
                // Its image is what the moving scene shows: follow the change at once.
                if (CaptureSnapshot(c)) c.snapshotDirty = false;
            } else {
                MarkSnapshotDirty(c);
            }
        }
        if (!StateChanging(role, message)) return;
        if (message == WM_SETFOCUS && ::GetKeyState(VK_LBUTTON) >= 0 && ::GetKeyState(VK_RBUTTON) >= 0) {
            // Keyboard (or programmatic) focus: let a scrolling ancestor bring the control into view.
            for (HWND parent = ::GetParent(c.hwnd); parent && parent != window; parent = ::GetParent(parent)) {
                ::PostMessageW(parent, detail::RevealFocusMessage(), 0, 0);
            }
        }
        if (message == WM_ENABLE && !wParam) SetHover(c, false);
        if (role == ControlRole::Container) return;
        if (role == ControlRole::Button || role == ControlRole::Switch || role == ControlRole::Check) {
            RefreshButtonState(c);
        } else if (role == ControlRole::ComboBox) {
            RefreshPressed(c, ::SendMessageW(c.hwnd, CB_GETDROPPEDSTATE, 0, 0) != 0);
        } else if (role == ControlRole::Slider) {
            RefreshPressed(c, ::GetCapture() == c.hwnd);
        } else if (role == ControlRole::Navigation) {
            RefreshNavigation(c);
        } else if (role == ControlRole::Progress) {
            if (message == WM_SHOWWINDOW || message == WM_WINDOWPOSCHANGED) KickContinuous();
        } else if (role == ControlRole::Label) {
            if (message == WM_SHOWWINDOW || message == WM_WINDOWPOSCHANGED) KickContinuous();
        }
        if (Welled(role)) {
            const bool frameChange = message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE ||
                                     message == WM_SHOWWINDOW ||
                                     (message == WM_WINDOWPOSCHANGED &&
                                      (reinterpret_cast<const WINDOWPOS*>(lParam)->flags &
                                       (SWP_SHOWWINDOW | SWP_HIDEWINDOW)) != 0);
            if (frameChange) InvalidateFrame(c);
            return;
        }
        if (message == WM_MOUSEMOVE && !(role == ControlRole::Slider && ::GetCapture() == c.hwnd)) return;
        if (Styled()) ::InvalidateRect(c.hwnd, nullptr, FALSE);
    }

    // --- lifetime ---
    void DetachAll() noexcept {
        for (auto& control : controls) RestoreControl(*control);
        controls.clear();
        overlays.clear();
        if (scheduler) scheduler->Cancel(this);
        frameRequested = false;
        StopTimer();
        snapshotTimer = false;
        RestoreWindowMaterial();
        if (window && ::IsWindow(window)) ::RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
        ReleaseResources();
        if (bufferedPaint) ::BufferedPaintUnInit();
        bufferedPaint = false;
        accProps = nullptr;
        window = nullptr;
        scheduler = nullptr;
    }

    void OnDpiChanged(UINT newDpi) {
        DropVisualCaches();
        InvalidateLayers();
        dpi = newDpi ? newDpi : platform::DpiForWindow(window);
        CreateFonts();
        for (auto& control : controls) {
            ::SendMessageW(control->hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            ApplyRole(*control);
            control->selected = -2;
        }
        ::RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
};

// --- FormSurface -----------------------------------------------------------------------------------

FormSurface::FormSurface() : impl_(std::make_unique<Impl>()) {}
FormSurface::~FormSurface() = default;

Result<void> FormSurface::Attach(HWND window, const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion,
                                 graphics::FrameScheduler* scheduler) {
    try {
        if (!window || !::IsWindow(window)) return MakeError(ErrorCode::InvalidArgument, "form window is not a window");
        if (::GetWindowThreadProcessId(window, nullptr) != ::GetCurrentThreadId())
            return MakeError(ErrorCode::InvalidState, "form window belongs to another thread");
        if (impl_->Attached()) return MakeError(ErrorCode::InvalidState, "form surface is already attached");
        auto& impl = *impl_;
        impl.window = window;
        impl.thread = ::GetCurrentThreadId();
        impl.tokens = tokens;
        impl.motion = motion;
        impl.scheduler = scheduler;
        impl.dpi = platform::DpiForWindow(window);
        impl.bufferedPaint = SUCCEEDED(::BufferedPaintInit());
        impl.factory = SharedFactory();
        if (!impl.dwrite) {
            if (FAILED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                             reinterpret_cast<IUnknown**>(impl.dwrite.put())))) {
                impl.dwrite = nullptr;
            }
        }
        if (impl.dwrite) impl.text.Initialize(impl.dwrite.get(), tokens.fontFamily, tokens.displayFontFamily);
        impl.renderer = impl.factory && impl.dwrite && impl.EnsureTarget();
        impl.material = impl.ResolveMaterial();
        const LONG_PTR style = ::GetWindowLongPtrW(window, GWL_STYLE);
        if (!(style & WS_CLIPCHILDREN)) ::SetWindowLongPtrW(window, GWL_STYLE, style | WS_CLIPCHILDREN);
        impl.CreateFonts();
        impl.CreateBrushes();
        impl.ApplyWindowMaterial();
        ::RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
        AETHER_LOG_INFO("ui_form", "form surface attached ({} material, renderer {})", ToString(impl.material),
                        impl.renderer ? "available" : "unavailable");
        return Ok();
    } catch (...) {
        impl_->DetachAll();
        return MakeError(ErrorCode::Internal, "form surface attach failed");
    }
}

void FormSurface::Detach() noexcept { impl_->DetachAll(); }
bool FormSurface::Attached() const noexcept { return impl_->Attached(); }
HWND FormSurface::Window() const noexcept { return impl_->window; }

bool SameAppearance(const graphics::DesignTokens& a, const graphics::DesignTokens& b) noexcept {
    const auto same = [](const graphics::Color& x, const graphics::Color& y) {
        return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
    };
    // Tokens are derived deterministically from the mode, the accent palette/source, transparency and
    // the text scale; these fields identify every input that changes form presentation.
    return a.mode == b.mode && a.transparencyEnabled == b.transparencyEnabled && a.textScale == b.textScale &&
           a.fontFamily == b.fontFamily && a.displayFontFamily == b.displayFontFamily && a.accentSource == b.accentSource &&
           a.systemAccent == b.systemAccent && same(a.accent, b.accent) && same(a.accentRoles.fill, b.accentRoles.fill) &&
           same(a.accentRoles.fillHover, b.accentRoles.fillHover) && same(a.accentRoles.fillPressed, b.accentRoles.fillPressed) &&
           same(a.accentRoles.text, b.accentRoles.text) && same(a.accentRoles.textOnAccent, b.accentRoles.textOnAccent) &&
           same(a.textPrimary, b.textPrimary) && same(a.textSecondary, b.textSecondary) && same(a.form.windowTint, b.form.windowTint) &&
           same(a.form.windowSolid, b.form.windowSolid) && same(a.form.fieldFill, b.form.fieldFill) && same(a.focus.outer, b.focus.outer);
}

void FormSurface::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion) {
    auto& impl = *impl_;
    if (!impl.Attached()) return;
    try {
        const FormMaterial previous = impl.material;
        // Unrelated settings changes re-send the same theme: rebuilding fonts, brushes and every
        // control frame then would visibly reconstruct the page while the user is interacting.
        const bool appearance = !SameAppearance(impl.tokens, tokens);
        const bool motionChanged = motion.reduced != impl.motion.reduced || motion.durationScale != impl.motion.durationScale;
        if (!appearance && !motionChanged) return;
        impl.tokens = tokens;
        impl.motion = motion;
        if (appearance) {
            impl.UnmaskEditors(nullptr);
            impl.DropVisualCaches();
            impl.InvalidateLayers();
            impl.material = impl.ResolveMaterial();
            if (impl.dwrite) impl.text.Initialize(impl.dwrite.get(), tokens.fontFamily, tokens.displayFontFamily);
            impl.CreateFonts();
            impl.CreateBrushes();
            impl.ApplyWindowMaterial();
        }
        for (std::size_t i = 0; i < impl.controls.size(); ++i) {
            auto& control = *impl.controls[i];
            if (!motion.AllowsMovement() || impl.material == FormMaterial::Native) {
                for (auto* tween : {&control.hover, &control.pressed, &control.on, &control.indicator, &control.fade, &control.rowHover,
                                    &control.rowLeave})
                    tween->SnapTo(tween->Target());
                control.animating = false;
                impl.UpdateLayeredAlpha(control);
            }
            if (!appearance) continue;
            ::SendMessageW(control.hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(impl.font), FALSE);
            impl.ApplyRole(control);
            impl.ApplyFrame(control);
        }
        if (!motion.AllowsMovement() || impl.material == FormMaterial::Native) {
            if (impl.scheduler) impl.scheduler->Cancel(&impl);
            impl.frameRequested = false;
            impl.StopTimer();
        }
        for (auto& control : impl.controls) {
            if (control->spec.role == ControlRole::Container)
                ::SendMessageW(control->hwnd, detail::PolicyChangedMessage(), 0, 0);
        }
        if (previous != impl.material) {
            AETHER_LOG_INFO("ui_form", "form material changed to {}", ToString(impl.material));
        }
        if (appearance) ::RedrawWindow(impl.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
        impl.KickContinuous();
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "form theme update failed");
    }
}

const graphics::DesignTokens& FormSurface::Tokens() const noexcept { return impl_->tokens; }
const graphics::MotionPolicy& FormSurface::Motion() const noexcept { return impl_->motion; }
FormMaterial FormSurface::Material() const noexcept { return impl_->material; }

bool FormSurface::MaximizedFrame() const noexcept { return impl_->Framed(); }

void FormSurface::SetMaximizedFrame(bool maximized) {
    auto& impl = *impl_;
    if (!impl.Attached() || impl.maximizedFrame == maximized) return;
    try {
        impl.maximizedFrame = maximized;
        const FormMaterial previous = impl.material;
        impl.material = impl.ResolveMaterial();
        // Retained layers and cached control visuals hold the old background (frame and material).
        impl.UnmaskEditors(nullptr);
        impl.DropVisualCaches();
        impl.InvalidateLayers();
        if (previous != impl.material) {
            impl.CreateBrushes();
            for (auto& control : impl.controls) impl.ApplyRole(*control);
        }
        impl.ApplyWindowMaterial();
        ::RedrawWindow(impl.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "form frame update failed");
    }
}
UINT FormSurface::Dpi() const noexcept { return impl_->dpi; }
float FormSurface::ToPx(float dip) const noexcept { return dip * impl_->Scale(); }
float FormSurface::ToDip(float px) const noexcept { return px / impl_->Scale(); }
graphics::FrameScheduler* FormSurface::Scheduler() const noexcept { return impl_->scheduler; }

Result<void> FormSurface::Register(HWND control, const ControlSpec& spec) {
    auto& impl = *impl_;
    try {
        if (!impl.Attached()) return MakeError(ErrorCode::InvalidState, "form surface is not attached");
        if (!control || !::IsWindow(control) || !::IsChild(impl.window, control))
            return MakeError(ErrorCode::InvalidArgument, "control is not a descendant of the form window");
        if (impl.Find(control)) {
            SetSpec(control, spec);
            return Ok();
        }
        auto state = std::make_unique<ControlState>();
        state->hwnd = control;
        state->spec = spec;
        state->start = impl.Now();
        if (!::SetWindowSubclass(control, &Impl::ControlProc, kSubclassId, reinterpret_cast<DWORD_PTR>(&impl)))
            return MakeError(ErrorCode::Platform, "control subclassing failed", static_cast<std::int64_t>(::GetLastError()));
        auto& c = *state;
        if (spec.role == ControlRole::Field) {
            wchar_t name[32]{};
            ::GetClassNameW(control, name, 32);
            c.hotkey = ::_wcsicmp(name, HOTKEY_CLASSW) == 0;
        }
        impl.controls.push_back(std::move(state));
        if (spec.role != ControlRole::Container) ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(impl.font), FALSE);
        if (spec.role == ControlRole::Button || spec.role == ControlRole::Switch || spec.role == ControlRole::Check) {
            c.check = static_cast<int>(::SendMessageW(control, BM_GETCHECK, 0, 0));
            c.on.SnapTo(c.check == BST_UNCHECKED ? 0.0f : 1.0f);
        }
        impl.ApplyRole(c);
        impl.PublishHelp(c);
        if (Welled(spec.role)) impl.InvalidateFrame(c);
        ::InvalidateRect(control, nullptr, TRUE);
        impl.KickContinuous();
        return Ok();
    } catch (...) {
        return MakeError(ErrorCode::Internal, "control registration failed");
    }
}

void FormSurface::Unregister(HWND control) noexcept {
    auto& impl = *impl_;
    auto it = std::find_if(impl.controls.begin(), impl.controls.end(), [control](const auto& c) { return c->hwnd == control; });
    if (it == impl.controls.end()) return;
    auto state = std::move(*it);
    impl.controls.erase(it);
    impl.RestoreControl(*state);
}

void FormSurface::SetSpec(HWND control, const ControlSpec& spec) {
    auto& impl = *impl_;
    ControlState* c = impl.Find(control);
    if (!c) return;
    try {
        const bool roleChanged = c->spec.role != spec.role;
        c->spec = spec;
        if (roleChanged) {
            impl.RestoreStyleBits(*c);
            impl.ApplyRole(*c);
            impl.ApplyFrame(*c);
        }
        impl.PublishHelp(*c);
        if (Welled(spec.role)) impl.InvalidateFrame(*c);
        ::InvalidateRect(control, nullptr, FALSE);
        impl.KickContinuous();
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "control spec update failed");
    }
}

const ControlSpec* FormSurface::Spec(HWND control) const noexcept {
    const ControlState* c = impl_->Find(control);
    return c ? &c->spec : nullptr;
}

void FormSurface::SetFrame(HWND control, const RECT& framePx) {
    auto& impl = *impl_;
    ControlState* c = impl.Find(control);
    if (!c) return;
    try {
        c->frame = framePx;
        c->hasFrame = true;
        impl.ApplyFrame(*c);
        if (c->spec.role == ControlRole::Navigation) c->selected = -2;
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "control frame update failed");
    }
}

RECT FormSurface::Frame(HWND control) const noexcept {
    const ControlState* c = impl_->Find(control);
    if (!c) return RECT{};
    try {
        return impl_->FrameOf(*c);
    } catch (...) {
        return c->frame;
    }
}

void FormSurface::SetError(HWND control, bool invalid) {
    ControlState* c = impl_->Find(control);
    if (!c || c->error == invalid) return;
    c->error = invalid;
    if (Welled(c->spec.role)) {
        impl_->InvalidateFrame(*c);
    } else {
        ::InvalidateRect(control, nullptr, FALSE);
    }
}

void FormSurface::SetNavigationGlyphs(HWND list, std::vector<graphics::Glyph> glyphs) {
    ControlState* c = impl_->Find(list);
    if (!c) return;
    c->glyphs = std::move(glyphs);
    ::InvalidateRect(list, nullptr, FALSE);
}

void FormSurface::SetNavigationCompact(HWND list, bool compact) {
    ControlState* c = impl_->Find(list);
    if (!c || c->compact == compact) return;
    c->compact = compact;
    ::InvalidateRect(list, nullptr, FALSE);
}

void FormSurface::SetProgress(HWND progress, std::optional<double> fraction) {
    auto& impl = *impl_;
    ControlState* c = impl.Find(progress);
    if (!c || c->spec.role != ControlRole::Progress) return;
    try {
        if (!fraction || !std::isfinite(*fraction)) {
            if (!impl.Indeterminate(*c)) c->start = impl.Now();
            ::SendMessageW(progress, PBM_SETMARQUEE, TRUE, 30);
        } else {
            ::SendMessageW(progress, PBM_SETMARQUEE, FALSE, 0);
            c = impl.Find(progress);
            if (!c) return;
            const double value = std::clamp(*fraction, 0.0, 1.0);
            PBRANGE range{};
            ::SendMessageW(progress, PBM_GETRANGE, FALSE, reinterpret_cast<LPARAM>(&range));
            const int position = range.iLow + static_cast<int>(std::lround(value * (range.iHigh - range.iLow)));
            ::SendMessageW(progress, PBM_SETPOS, static_cast<WPARAM>(position), 0);
            if ((c = impl.Find(progress)) != nullptr) c->fraction = value;
        }
        ::InvalidateRect(progress, nullptr, FALSE);
        impl.KickContinuous();
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "progress update failed");
    }
}

void FormSurface::SetCards(std::vector<FormCard> cards) {
    impl_->cards = std::move(cards);
    if (impl_->window) ::RedrawWindow(impl_->window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void FormSurface::SetDividers(std::vector<FormDivider> dividers) {
    impl_->dividers = std::move(dividers);
    if (impl_->window) ::RedrawWindow(impl_->window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void FormSurface::SetDecorator(FormDecorator decorator) {
    impl_->decorator = std::move(decorator);
    if (impl_->window) ::RedrawWindow(impl_->window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void FormSurface::FadeIn(HWND container) {
    auto& impl = *impl_;
    ControlState* c = impl.Find(container);
    if (!c || c->spec.role != ControlRole::Container) return;
    const double now = impl.Now();
    if (!impl.motion.AllowsMovement() || !impl.Styled()) {
        c->fade.SnapTo(1.0f);
    } else {
        if (!c->fade.IsAnimating(now)) c->fade.SnapTo(0.0f);
        c->fade.AnimateTo(1.0f, now, impl.Duration(impl.tokens.form.page), impl.tokens.curves.decelerate);
        impl.StartAnimation(*c);
    }
    for (auto& other : impl.controls) {
        if (Welled(other->spec.role) && ::IsChild(container, other->hwnd)) impl.UpdateLayeredAlpha(*other);
    }
    try {
        if (impl.Present(impl.PresentRoot(container))) return;
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "container presentation failed");
    }
    ::RedrawWindow(container, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

bool FormSurface::Present(HWND container) {
    try {
        return impl_->Attached() && impl_->Present(container);
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "container presentation failed");
        return false;
    }
}

bool FormSurface::PresentMove(HWND container, HWND moving, int dyPx, const std::function<void()>& move) {
    if (!move) return Present(container);
    bool called = false;
    const auto once = [&] {
        if (called) return;
        called = true;
        move();
    };
    try {
        if (impl_->Attached() && impl_->Present(container, moving, dyPx, once)) return true;
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "container presentation failed");
        if (!called) once();
        ::RedrawWindow(container, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        return true;  // moved, and the ordinary repaint was requested
    }
    return false;
}

POINT FormSurface::ClientOriginPx(HWND hwnd) const noexcept {
    if (!impl_->Attached() || !hwnd) return POINT{};
    return impl_->FormOrigin(hwnd);
}

void FormSurface::SetContainerOverlay(HWND container, RegionOverlay overlay, HWND overlayWindow) {
    auto& overlays = impl_->overlays;
    std::erase_if(overlays, [container](const auto& entry) { return entry.container == container; });
    if (container && overlay) overlays.push_back(Impl::ContainerOverlay{container, std::move(overlay), overlayWindow});
}

bool FormSurface::HasVisibleEditors(HWND container) const noexcept {
    try {
        return impl_->Attached() && impl_->Styled() && impl_->HasVisibleEditors(container);
    } catch (...) {
        return false;
    }
}

bool FormSurface::MaskEditors(HWND container) {
    try {
        return !(impl_->Attached() && impl_->Styled()) || impl_->MaskEditors(container);
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "editor masking failed");
        impl_->UnmaskEditors(container);
        return true;
    }
}

void FormSurface::UnmaskEditors(HWND container) noexcept {
    try {
        impl_->UnmaskEditors(container);
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "editor unmasking failed");
    }
}

bool FormSurface::EditorsMasked(HWND container) const noexcept {
    for (const auto& control : impl_->controls) {
        if (control->masked && (!container || ::IsChild(container, control->hwnd))) return true;
    }
    return false;
}

FormSurface::PresentStats FormSurface::Stats() const noexcept { return impl_->stats; }
void FormSurface::ResetStats() noexcept { impl_->stats = PresentStats{}; }

bool FormSurface::Animating() const noexcept { return impl_->AnyAnimating(); }

std::optional<LRESULT> FormSurface::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    auto& impl = *impl_;
    if (!impl.Attached()) return std::nullopt;
    try {
        switch (message) {
            case WM_PAINT:
                impl.Paint(impl.window);
                return 0;
            case WM_PRINTCLIENT: {
                RECT client{};
                ::GetClientRect(impl.window, &client);
                impl.PaintRegion(impl.window, reinterpret_cast<HDC>(wParam), client, nullptr);
                return 0;
            }
            case WM_ERASEBKGND:
                if (!impl.Styled()) {
                    RECT client{};
                    ::GetClientRect(impl.window, &client);
                    ::FillRect(reinterpret_cast<HDC>(wParam), &client, ::GetSysColorBrush(COLOR_WINDOW));
                }
                return 1;
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLORBTN:
            case WM_DRAWITEM:
            case WM_MEASUREITEM:
                return impl.ChildNotification(impl.window, message, wParam, lParam);
            case WM_TIMER:
                if (wParam == kAnimationTimer) {
                    impl.Tick(impl.Now());
                    return 0;
                }
                return std::nullopt;
            case WM_DPICHANGED:
                impl.OnDpiChanged(HIWORD(wParam));
                return std::nullopt;
            case WM_DWMCOMPOSITIONCHANGED:
                impl.ApplyWindowMaterial();
                return std::nullopt;
            case WM_SIZE:
                if (wParam != SIZE_MINIMIZED) impl.KickContinuous();
                return std::nullopt;
            case WM_SHOWWINDOW:
                if (wParam) impl.KickContinuous();
                else {
                    impl.DropVisualCaches();  // a hidden form keeps no cached renderings
                    impl.ReleaseLayers();
                }
                return std::nullopt;
            case WM_NCDESTROY:
                impl.DetachAll();
                return std::nullopt;
            default:
                if (message == SnapshotRefreshMessage()) {
                    impl.RefreshSnapshots();
                    return 0;
                }
                if (message == PrepareLayersMessage()) {
                    impl.Prepare(reinterpret_cast<HWND>(wParam));
                    return 0;
                }
                return std::nullopt;
        }
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "form message {} failed", message);
        return std::nullopt;
    }
}

D2D1_SIZE_F FormSurface::MeasureText(std::wstring_view text, TextRole role, float maxWidthDip, bool wrap) const {
    try {
        const auto parsed = paint::ParseMnemonic(text);
        const float width = std::isfinite(maxWidthDip) && maxWidthDip > 0.0f ? maxWidthDip : 10000.0f;
        return impl_->text.Measure(parsed.text, paint::StyleFor(impl_->tokens, role, wrap), width);
    } catch (...) {
        return D2D1::SizeF(0.0f, 0.0f);
    }
}

float FormSurface::PreferredWidth(HWND control) const {
    const ControlState* c = impl_->Find(control);
    if (!c) return 0.0f;
    try {
        const auto& f = impl_->tokens.form;
        const auto body = paint::BodyStyle(impl_->tokens);
        auto& text = impl_->text;
        switch (c->spec.role) {
            case ControlRole::Button: {
                const auto parsed = paint::ParseMnemonic(WindowString(control));
                float width = text.Measure(parsed.text, body).width + 2.0f * f.buttonPaddingX;
                if (c->spec.glyph) width += f.glyphSize + 8.0f;
                return std::max(f.buttonMinWidth, std::ceil(width));
            }
            case ControlRole::ComboBox: {
                float widest = 0.0f;
                const int count = static_cast<int>(::SendMessageW(control, CB_GETCOUNT, 0, 0));
                for (int i = 0; i < count; ++i) {
                    widest = std::max(widest, text.Measure(ListText(control, i, CB_GETLBTEXTLEN, CB_GETLBTEXT), body).width);
                }
                return std::max(f.comboMinWidth, std::ceil(widest + 2.0f * f.fieldPaddingX + 12.0f + 8.0f + 2.0f));
            }
            case ControlRole::Check: {
                const auto parsed = paint::ParseMnemonic(WindowString(control));
                const float extent = std::max(0.0f, paint::FocusRingExtent(impl_->tokens) - 1.0f);
                return std::ceil(extent + f.checkSize + f.relatedGap + text.Measure(parsed.text, body).width + 4.0f);
            }
            default: {
                const auto parsed = paint::ParseMnemonic(WindowString(control));
                return std::ceil(text.Measure(parsed.text, paint::StyleFor(impl_->tokens, c->spec.text, false)).width);
            }
        }
    } catch (...) {
        return 0.0f;
    }
}

HFONT FormSurface::ControlFont() const noexcept { return impl_->font; }

void FormSurface::PaintRegion(HWND windowBeingPainted, HDC hdc, const RECT& clipPx) {
    PaintRegion(windowBeingPainted, hdc, clipPx, RegionOverlay{});
}

void FormSurface::PaintRegion(HWND windowBeingPainted, HDC hdc, const RECT& clipPx, const RegionOverlay& overlay) {
    auto& impl = *impl_;
    if (!impl.Attached() || !windowBeingPainted ||
        (windowBeingPainted != impl.window && !::IsChild(impl.window, windowBeingPainted))) {
        return;
    }
    try {
        impl.PaintRegion(windowBeingPainted, hdc, clipPx, overlay ? &overlay : nullptr);
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "form region paint failed");
    }
}

}  // namespace aether::ui::form
