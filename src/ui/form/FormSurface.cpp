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

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <map>
#include <utility>

namespace aether::ui::form {
namespace {

constexpr UINT_PTR kSubclassId = 0x464F524Du;      // "FORM"
constexpr UINT_PTR kAnimationTimer = 0x464F524Du;  // timer id on the form window (no scheduler)
constexpr UINT kTimerIntervalMs = 16;
// DWMWA_SYSTEMBACKDROP_TYPE and its values (dwmapi.h of the Windows 11 SDK); spelled out so the
// module also builds against older SDK headers.
constexpr DWORD kDwmSystemBackdropType = 38;
constexpr DWORD kBackdropAuto = 0;
constexpr DWORD kBackdropNone = 1;
constexpr DWORD kBackdropTransient = 3;
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
    int selected = -2;
    int topIndex = -1;
    graphics::Tween indicator;  // centre of the indicator, DIPs of the list client area
    // Progress
    std::optional<bool> indeterminate;
    std::optional<double> fraction;
    // Container fade
    graphics::Tween fade{1.0f};
    double start = 0.0;  // continuous animations (Busy ring, indeterminate sweep)
};

struct FormSurface::Impl final : graphics::IFrameClient {
    HWND window = nullptr;
    DWORD thread = 0;
    graphics::DesignTokens tokens;
    graphics::MotionPolicy motion;
    graphics::FrameScheduler* scheduler = nullptr;
    FormMaterial material = FormMaterial::Native;
    UINT dpi = platform::kDefaultDpi;
    bool renderer = false;
    bool frameRequested = false;
    bool timerRunning = false;
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
        const MARGINS margins = material == FormMaterial::Acrylic ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
        ::DwmExtendFrameIntoClientArea(window, &margins);
        if (SystemBackdropSupported()) {
            const DWORD type = material == FormMaterial::Acrylic ? kBackdropTransient : kBackdropNone;
            ::DwmSetWindowAttribute(window, kDwmSystemBackdropType, &type, sizeof(type));
        }
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
        ::SetLayeredWindowAttributes(c.hwnd, 0, AlphaByte(FadeOf(c.hwnd)), LWA_ALPHA);
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
            ::RedrawWindow(c.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
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
               c.indicator.IsAnimating(now) || c.fade.IsAnimating(now);
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
        if (SelfPainted(c.spec.role)) {
            Animate(c.hover, hovered ? 1.0f : 0.0f, tokens.form.feedback);
            StartAnimation(c);
        }
    }

    void SetRowHover(ControlState& c, int index) {
        if (index == c.hoverIndex) return;
        if (c.spec.role == ControlRole::List) {
            for (const int row : {c.hoverIndex, index}) {
                RECT item{};
                if (row >= 0 && ::SendMessageW(c.hwnd, LB_GETITEMRECT, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&item)) != LB_ERR)
                    ::InvalidateRect(c.hwnd, &item, FALSE);
            }
        } else {
            ::InvalidateRect(c.hwnd, nullptr, FALSE);
        }
        c.hoverIndex = index;
    }

    // --- painting ---
    void Paint(HWND hwnd) {
        PAINTSTRUCT ps{};
        HDC hdc = ::BeginPaint(hwnd, &ps);
        if (hdc && !::IsRectEmpty(&ps.rcPaint)) PaintRegion(hwnd, hdc, ps.rcPaint, nullptr);
        ::EndPaint(hwnd, &ps);
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

    void RenderScene(HWND hwnd, HDC hdc, const RECT& clip, const RegionOverlay* overlay) {
        if (FAILED(target->BindDC(hdc, &clip))) return;
        const float s = Scale();
        target->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        POINT origin{0, 0};
        ::MapWindowPoints(hwnd, window, &origin, 1);
        const float ox = static_cast<float>(origin.x + clip.left) / s;
        const float oy = static_cast<float>(origin.y + clip.top) / s;
        const D2D1_RECT_F area = D2D1::RectF(ox, oy, static_cast<float>(origin.x + clip.right) / s,
                                             static_cast<float>(origin.y + clip.bottom) / s);
        auto* d2d = context.get();
        d2d->BeginDraw();
        d2d->SetTransform(D2D1::Matrix3x2F::Translation(-ox, -oy));
        // Grayscale over translucent glass; ClearType only where every pixel is opaque (Solid).
        d2d->SetTextAntialiasMode(material == FormMaterial::Solid ? D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE
                                                                  : D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        d2d->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        d2d->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_ALIASED);
        paint::PaintBackground(d2d, area, tokens, material);
        const float fade = FadeOf(hwnd);
        const bool layered = fade < 0.999f;
        if (layered) {
            d2d->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                  D2D1::IdentityMatrix(), fade, nullptr, D2D1_LAYER_OPTIONS1_NONE),
                           nullptr);
        }
        DrawCards(d2d, area);
        DrawDividers(d2d, area);
        if (decorator) {
            try {
                decorator(d2d, area, tokens);
            } catch (...) {
                AETHER_LOG_ERROR("ui_form", "form decorator threw");
            }
        }
        DrawControls(d2d, hwnd, area);
        if (overlay && *overlay) {
            d2d->SetTransform(D2D1::Matrix3x2F::Translation(-static_cast<float>(clip.left) / s, -static_cast<float>(clip.top) / s));
            try {
                (*overlay)(d2d, tokens);
            } catch (...) {
                AETHER_LOG_ERROR("ui_form", "region overlay threw");
            }
        }
        if (layered) d2d->PopLayer();
        d2d->PopAxisAlignedClip();
        const HRESULT hr = d2d->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            target = nullptr;
            context = nullptr;
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    // Visible part of a descendant window in form client pixels (clipped by every ancestor).
    RECT VisibleInForm(HWND hwnd) const noexcept {
        RECT visible = ClientIn(hwnd, window);
        for (HWND parent = ::GetParent(hwnd); parent && parent != window; parent = ::GetParent(parent)) {
            const RECT clip = ClientIn(parent, window);
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

    void DrawCards(ID2D1DeviceContext* d2d, const D2D1_RECT_F& area) {
        const float pixel = 1.0f / Scale();
        for (const auto& card : cards) {
            RECT px = card.px;
            std::optional<D2D1_RECT_F> clip;
            if (card.canvas) {
                if (!::IsWindow(card.canvas) || !::IsWindowVisible(card.canvas)) continue;
                px = MapRect(card.canvas, window, px);
                clip = ToDip(VisibleInForm(card.canvas));
            }
            const auto rect = Snap(ToDip(px));
            if (!Intersects(rect, area) || (clip && !Intersects(rect, *clip))) continue;
            if (clip) d2d->PushAxisAlignedClip(*clip, D2D1_ANTIALIAS_MODE_ALIASED);
            paint::PaintCard(d2d, rect, tokens, material, card.emphasis, pixel);
            if (clip) d2d->PopAxisAlignedClip();
        }
    }

    void DrawDividers(ID2D1DeviceContext* d2d, const D2D1_RECT_F& area) {
        for (const auto& divider : dividers) {
            RECT px = divider.px;
            std::optional<D2D1_RECT_F> clip;
            if (divider.canvas) {
                if (!::IsWindow(divider.canvas) || !::IsWindowVisible(divider.canvas)) continue;
                px = MapRect(divider.canvas, window, px);
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
        for (std::size_t i = 0; i < controls.size(); ++i) {
            ControlState& c = *controls[i];
            if (!Welled(c.spec.role) || ::GetParent(c.hwnd) != hwnd || !::IsWindowVisible(c.hwnd)) continue;
            const auto rect = Snap(ToDip(MapRect(hwnd, window, FrameOf(c))));
            if (!Intersects(rect, area)) continue;
            const HWND focus = ::GetFocus();
            const bool focused = c.spec.role != ControlRole::List && (focus == c.hwnd || ::IsChild(c.hwnd, focus));
            paint::PaintFieldWell(d2d, rect, ::IsWindowEnabled(c.hwnd) != FALSE, focused, c.error, 1.0f / Scale(), tokens);
        }
        ControlState* self = Find(hwnd);
        if (!self || !SelfPainted(self->spec.role) || self->spec.role == ControlRole::Container) return;
        DrawSelf(d2d, *self, now);
    }

    void DrawSelf(ID2D1DeviceContext* d2d, ControlState& c, double now) {
        const auto client = ToDip(ClientIn(c.hwnd, window));
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
                paint::PaintSlider(d2d, ToDip(MapRect(c.hwnd, window, channel)), ToDip(MapRect(c.hwnd, window, thumb)), v, tokens);
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
        ::MapWindowPoints(c.hwnd, window, &origin, 1);
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
            const auto row = ToDip(MapRect(c.hwnd, window, item));
            std::optional<graphics::Glyph> glyph;
            if (static_cast<std::size_t>(index) < c.glyphs.size()) glyph = c.glyphs[static_cast<std::size_t>(index)];
            paint::PaintNavigationRow(d2d, text, row, ListText(c.hwnd, index, LB_GETTEXTLEN, LB_GETTEXT), glyph, glyphColumn,
                                      index == selected, index == c.hoverIndex ? 1.0f : 0.0f, c.compact, visual.enabled, tokens);
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
                                (item.itemState & ODS_SELECTED) != 0, static_cast<int>(item.itemID) == c.hoverIndex,
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
            Paint(hwnd);
            return 0;
        }
        if (message == WM_PRINTCLIENT && SelfPainted(role) && (styled || role == ControlRole::Container)) {
            RECT client{};
            ::GetClientRect(hwnd, &client);
            PaintRegion(hwnd, reinterpret_cast<HDC>(wParam), client, nullptr);
            return 0;
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
        if (scheduler) scheduler->Cancel(this);
        frameRequested = false;
        StopTimer();
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
        impl.material = ResolveFormMaterial(tokens.mode, tokens.transparencyEnabled, SystemBackdropSupported(), impl.renderer);
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

void FormSurface::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion) {
    auto& impl = *impl_;
    if (!impl.Attached()) return;
    try {
        const FormMaterial previous = impl.material;
        impl.tokens = tokens;
        impl.motion = motion;
        impl.material = ResolveFormMaterial(tokens.mode, tokens.transparencyEnabled, SystemBackdropSupported(), impl.renderer);
        if (impl.dwrite) impl.text.Initialize(impl.dwrite.get(), tokens.fontFamily, tokens.displayFontFamily);
        impl.CreateFonts();
        impl.CreateBrushes();
        impl.ApplyWindowMaterial();
        for (std::size_t i = 0; i < impl.controls.size(); ++i) {
            auto& control = *impl.controls[i];
            if (!motion.AllowsMovement() || impl.material == FormMaterial::Native) {
                for (auto* tween : {&control.hover, &control.pressed, &control.on, &control.indicator, &control.fade})
                    tween->SnapTo(tween->Target());
                control.animating = false;
                impl.UpdateLayeredAlpha(control);
            }
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
        ::RedrawWindow(impl.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
        impl.KickContinuous();
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "form theme update failed");
    }
}

const graphics::DesignTokens& FormSurface::Tokens() const noexcept { return impl_->tokens; }
const graphics::MotionPolicy& FormSurface::Motion() const noexcept { return impl_->motion; }
FormMaterial FormSurface::Material() const noexcept { return impl_->material; }
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
    ::RedrawWindow(container, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

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
                return std::nullopt;
            case WM_NCDESTROY:
                impl.DetachAll();
                return std::nullopt;
            default:
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
