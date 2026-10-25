// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "ui/form/ConfirmDialog.h"

#include "diagnostics/Logger.h"
#include "graphics/FrameScheduler.h"
#include "graphics/GlassPainter.h"
#include "graphics/Glyphs.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/WindowUtilities.h"
#include "ui/form/FormPaint.h"
#include "ui/form/FormScrollView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

namespace aether::ui::form {
namespace {

constexpr wchar_t kDialogClass[] = L"AetherShell.Confirmation";
constexpr int kPrimaryId = IDOK;
constexpr int kCancelId = IDCANCEL;
constexpr int kSecondaryId = 3;
constexpr float kWidth = 440.0f;
constexpr float kMinWidth = 420.0f;
constexpr float kMaxWidth = 560.0f;
constexpr float kBadge = 24.0f;
constexpr float kBadgeGap = 12.0f;
constexpr float kTitleGap = 12.0f;

thread_local std::function<void(HWND)> confirmationTestHook;

bool HasSecondary(const ConfirmRequest& request) noexcept { return !request.secondary.empty(); }
bool HasCancel(const ConfirmRequest& request) noexcept { return !request.cancel.empty(); }

// Escape and the close box: Cancel, or the last button when there is no cancel button.
ConfirmChoice EscapeChoice(const ConfirmRequest& request) noexcept {
    if (HasCancel(request)) return ConfirmChoice::Cancel;
    return HasSecondary(request) ? ConfirmChoice::Secondary : ConfirmChoice::Primary;
}

ConfirmChoice EffectiveDefault(const ConfirmRequest& request) noexcept {
    switch (request.defaultChoice) {
        case ConfirmChoice::Primary: return ConfirmChoice::Primary;
        case ConfirmChoice::Secondary: return HasSecondary(request) ? ConfirmChoice::Secondary : ConfirmChoice::Primary;
        case ConfirmChoice::Cancel: return EscapeChoice(request);
    }
    return ConfirmChoice::Primary;
}

int IdOf(ConfirmChoice choice) noexcept {
    switch (choice) {
        case ConfirmChoice::Primary: return kPrimaryId;
        case ConfirmChoice::Secondary: return kSecondaryId;
        case ConfirmChoice::Cancel: return kCancelId;
    }
    return kCancelId;
}

std::optional<graphics::StatusBadge> BadgeFor(Severity severity) noexcept {
    switch (severity) {
        case Severity::Info: return graphics::StatusBadge::Info;
        case Severity::Success: return graphics::StatusBadge::Success;
        case Severity::Warning: return graphics::StatusBadge::Warning;
        case Severity::Error: return graphics::StatusBadge::Error;
        case Severity::Busy: return graphics::StatusBadge::Info;
        case Severity::None: return std::nullopt;
    }
    return std::nullopt;
}

const wchar_t* SystemIconFor(Severity severity) noexcept {
    switch (severity) {
        case Severity::Warning: return IDI_WARNING;
        case Severity::Error: return IDI_ERROR;
        case Severity::None: return nullptr;
        default: return IDI_INFORMATION;
    }
}

class Dialog {
public:
    Dialog(const ConfirmRequest& request, const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion,
           graphics::FrameScheduler* scheduler)
        : request_(request), tokens_(tokens), motion_(motion), scheduler_(scheduler) {}
    ~Dialog() { Close(); }
    Dialog(const Dialog&) = delete;
    Dialog& operator=(const Dialog&) = delete;

    // nullopt: the dialog could not be presented (the caller falls back to MessageBoxW).
    std::optional<ConfirmChoice> Run() {
        if (!Create()) return std::nullopt;
        owner_ = request_.owner && ::IsWindow(request_.owner) ? request_.owner : nullptr;
        ownerWasEnabled_ = owner_ && ::IsWindowEnabled(owner_);
        if (ownerWasEnabled_) ::EnableWindow(owner_, FALSE);
        // Activate when this process owns the foreground (the usual case: the user acted in the
        // owner). Otherwise Windows' foreground lock would refuse anyway; the dialog then appears
        // without taking activation from another application.
        DWORD foregroundProcess = 0;
        if (HWND foreground = ::GetForegroundWindow()) ::GetWindowThreadProcessId(foreground, &foregroundProcess);
        const bool activate = foregroundProcess == ::GetCurrentProcessId();
        focusTarget_ = ::GetDlgItem(hwnd_, IdOf(EffectiveDefault(request_)));
        ::ShowWindow(hwnd_, activate ? SW_SHOW : SW_SHOWNA);
        ::UpdateWindow(hwnd_);
        // SetFocus would activate the dialog (and, on close, its owner) even without the foreground:
        // an inactive dialog gets its initial focus when the user activates it (WM_ACTIVATE).
        if (activate && focusTarget_) ::SetFocus(focusTarget_);
        if (confirmationTestHook) {
            auto hook = confirmationTestHook;
            try {
                hook(hwnd_);
            } catch (...) {
                AETHER_LOG_ERROR("ui_form", "confirmation test hook threw");
            }
        }
        Loop();
        // Re-enable the owner while the dialog still exists so Windows hands activation back to it.
        if (ownerWasEnabled_ && ::IsWindow(owner_)) ::EnableWindow(owner_, TRUE);
        ownerWasEnabled_ = false;
        Close();
        return choice_;
    }

private:
    bool Create() {
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.lpfnWndProc = &Dialog::Proc;
        windowClass.hInstance = ::GetModuleHandleW(nullptr);
        windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kDialogClass;
        if (!platform::RegisterWindowClassOnce(windowClass)) return false;

        HMONITOR monitor = request_.owner && ::IsWindow(request_.owner) ? ::MonitorFromWindow(request_.owner, MONITOR_DEFAULTTONEAREST)
                                                                        : ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{sizeof(info)};
        if (!::GetMonitorInfoW(monitor, &info)) return false;
        work_ = info.rcWork;
        const UINT dpi = platform::DpiForMonitor(monitor);
        const int width = platform::DipsToPixels(kWidth, dpi);
        const int height = platform::DipsToPixels(200.0f, dpi);
        const int x = (work_.left + work_.right - width) / 2;
        const int y = (work_.top + work_.bottom - height) / 2;
        hwnd_ = ::CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kDialogClass, request_.windowTitle.c_str(), kStyle, x, y,
                                  width, height, request_.owner && ::IsWindow(request_.owner) ? request_.owner : nullptr, nullptr,
                                  windowClass.hInstance, this);
        if (!hwnd_) return false;
        if (auto attached = surface_.Attach(hwnd_, tokens_, motion_, scheduler_); !attached) {
            AETHER_LOG_WARN("ui_form", "confirmation surface unavailable: {}", attached.error().Describe());
            return false;
        }
        // Without a renderer the surface is Native; outside high contrast that is a presentation failure.
        if (surface_.Material() == FormMaterial::Native && tokens_.mode != graphics::ThemeMode::HighContrast) return false;
        if (!scroll_.Create(surface_, hwnd_, 1000)) return false;
        if (!CreateControls()) return false;
        return Layout();
    }

    HWND AddStatic(const std::wstring& text, TextRole role) {
        HWND label = ::CreateWindowExW(0, L"STATIC", text.c_str(), WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOPREFIX, 0, 0, 0, 0,
                                       scroll_.Canvas(), nullptr, ::GetModuleHandleW(nullptr), nullptr);
        if (!label) return nullptr;
        ControlSpec spec;
        spec.role = ControlRole::Label;
        spec.text = role;
        spec.wrap = true;
        if (!surface_.Register(label, spec)) return nullptr;
        return label;
    }

    HWND AddButton(const std::wstring& text, int id, ButtonStyle style, bool isDefault, bool first) {
        DWORD buttonStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_MULTILINE | (isDefault ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
        if (first) buttonStyle |= WS_GROUP;
        HWND button = ::CreateWindowExW(0, L"BUTTON", text.c_str(), buttonStyle, 0, 0, 0, 0, hwnd_,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ::GetModuleHandleW(nullptr), nullptr);
        if (!button) return nullptr;
        ControlSpec spec;
        spec.role = ControlRole::Button;
        spec.button = style;
        if (!surface_.Register(button, spec)) return nullptr;
        return button;
    }

    bool CreateControls() {
        title_ = AddStatic(request_.title, TextRole::Title);
        message_ = AddStatic(request_.message, TextRole::Body);
        if (!request_.detail.empty()) detail_ = AddStatic(request_.detail, TextRole::Helper);
        if (!title_ || !message_ || (!request_.detail.empty() && !detail_)) return false;
        const ConfirmChoice def = EffectiveDefault(request_);
        buttons_.clear();
        const auto add = [&](const std::wstring& text, int id, ButtonStyle style, ConfirmChoice choice) {
            HWND button = AddButton(text, id, style, def == choice, buttons_.empty());
            if (button) buttons_.push_back(button);
            return button != nullptr;
        };
        if (!add(request_.primary, kPrimaryId, request_.dangerousPrimary ? ButtonStyle::Danger : ButtonStyle::Primary,
                 ConfirmChoice::Primary)) {
            return false;
        }
        if (HasSecondary(request_) && !add(request_.secondary, kSecondaryId, ButtonStyle::Secondary, ConfirmChoice::Secondary)) return false;
        if (HasCancel(request_) && !add(request_.cancel, kCancelId, ButtonStyle::Secondary, ConfirmChoice::Cancel)) return false;
        if (surface_.Material() == FormMaterial::Native) {
            if (const wchar_t* icon = SystemIconFor(request_.severity)) {
                icon_ = ::CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ICON | SS_REALSIZECONTROL, 0, 0, 0, 0, scroll_.Canvas(),
                                          nullptr, ::GetModuleHandleW(nullptr), nullptr);
                if (icon_) ::SendMessageW(icon_, STM_SETICON, reinterpret_cast<WPARAM>(::LoadIconW(nullptr, icon)), 0);
            }
        }
        return true;
    }

    int Px(float dip) const noexcept { return static_cast<int>(std::lround(surface_.ToPx(dip))); }
    RECT PxRect(float left, float top, float right, float bottom) const noexcept {
        return RECT{Px(left), Px(top), Px(right), Px(bottom)};
    }

    bool Layout() {
        const auto& f = surface_.Tokens().form;
        float buttonsWidth = 0.0f;
        std::vector<float> widths;
        for (HWND button : buttons_) {
            widths.push_back(std::max(f.buttonMinWidth, surface_.PreferredWidth(button)));
            buttonsWidth += widths.back();
        }
        buttonsWidth += f.relatedGap * static_cast<float>(std::max<std::size_t>(1, buttons_.size()) - 1);
        RECT chrome{0, 0, 0, 0};
        ::AdjustWindowRectExForDpi(&chrome, kStyle, FALSE, WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, surface_.Dpi());
        const float availableWidth = surface_.ToDip(static_cast<float>(work_.right - work_.left - (chrome.right - chrome.left)));
        const float availableHeight = surface_.ToDip(static_cast<float>(work_.bottom - work_.top - (chrome.bottom - chrome.top)));
        const float width = std::min(std::clamp(std::max(kWidth, buttonsWidth + 2.0f * f.outer), kMinWidth, kMaxWidth), availableWidth);
        const float innerWidth = width - 2.0f * f.outer;
        if (innerWidth <= 0.0f) return false;
        const bool stacked = buttonsWidth > innerWidth;
        std::vector<float> heights;
        float footerHeight = f.footerHeight;
        if (stacked) {
            footerHeight = 2.0f * f.outer;
            for (std::size_t i = 0; i < buttons_.size(); ++i) {
                widths[i] = innerWidth;
                const auto value = paint::ParseMnemonic(i == 0 ? request_.primary : (i == 1 && HasSecondary(request_) ? request_.secondary : request_.cancel));
                const float textHeight = surface_.MeasureText(value.text, TextRole::Body, std::max(1.0f, innerWidth - 2.0f * f.buttonPaddingX)).height;
                heights.push_back(std::max(f.controlHeight, std::ceil(textHeight) + 16.0f));
                footerHeight += heights.back() + (i ? f.relatedGap : 0.0f);
            }
        } else {
            heights.assign(buttons_.size(), f.controlHeight);
        }
        if (availableHeight < footerHeight + 40.0f) return false;
        const bool badge = request_.severity != Severity::None;
        const float textLeft = f.outer + (badge ? kBadge + kBadgeGap : 0.0f);
        const float textWidth = std::max(1.0f, width - textLeft - f.outer - surface_.ToDip(static_cast<float>(scroll_.GutterPx())));
        float y = f.outer;
        const float titleHeight = std::ceil(surface_.MeasureText(request_.title, TextRole::Title, textWidth).height);
        const auto titleLine = surface_.MeasureText(L"Ag", TextRole::Title, textWidth).height;
        surface_.SetFrame(title_, PxRect(textLeft, y, textLeft + textWidth, y + titleHeight));
        badgeTop_ = y + std::max(0.0f, (titleLine - kBadge) / 2.0f);
        badgeLeft_ = f.outer;
        y += titleHeight + kTitleGap;
        const float messageHeight = std::ceil(surface_.MeasureText(request_.message, TextRole::Body, textWidth).height);
        surface_.SetFrame(message_, PxRect(textLeft, y, textLeft + textWidth, y + messageHeight));
        y += messageHeight;
        if (detail_) {
            y += f.relatedGap;
            const float detailHeight = std::ceil(surface_.MeasureText(request_.detail, TextRole::Helper, textWidth).height);
            surface_.SetFrame(detail_, PxRect(textLeft, y, textLeft + textWidth, y + detailHeight));
            y += detailHeight;
        }
        if (badge) y = std::max(y, badgeTop_ + kBadge);
        const float contentHeight = y + f.outer;
        footerTop_ = std::min(contentHeight, availableHeight - footerHeight);
        const float height = footerTop_ + footerHeight;
        scroll_.SetFrame(PxRect(0.0f, 0.0f, width, footerTop_));
        scroll_.SetContentHeight(Px(contentHeight));
        clientWidth_ = width;
        // Buttons are right-aligned in the footer: [Primary][Secondary][Cancel].
        float right = width - f.outer;
        float top = footerTop_ + (stacked ? f.outer : (footerHeight - f.controlHeight) / 2.0f);
        if (stacked) {
            for (std::size_t i = 0; i < buttons_.size(); ++i) {
                surface_.SetFrame(buttons_[i], PxRect(f.outer, top, width - f.outer, top + heights[i]));
                top += heights[i] + f.relatedGap;
            }
        } else {
            for (std::size_t i = buttons_.size(); i-- > 0;) {
                surface_.SetFrame(buttons_[i], PxRect(right - widths[i], top, right, top + heights[i]));
                right -= widths[i] + f.relatedGap;
            }
        }
        if (icon_) {
            ::SetWindowPos(icon_, nullptr, Px(badgeLeft_), Px(badgeTop_) - Px(4.0f), Px(32.0f), Px(32.0f), SWP_NOZORDER | SWP_NOACTIVATE);
        }

        // Size and centre the window on its owner (or the work area), kept inside the work area.
        RECT frame{0, 0, Px(width), Px(height)};
        const UINT dpi = surface_.Dpi();
        ::AdjustWindowRectExForDpi(&frame, kStyle, FALSE, WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, dpi);
        const int w = frame.right - frame.left;
        const int h = frame.bottom - frame.top;
        RECT anchor = work_;
        if (request_.owner && ::IsWindow(request_.owner) && ::IsWindowVisible(request_.owner) && !::IsIconic(request_.owner)) {
            ::GetWindowRect(request_.owner, &anchor);
        }
        int x = (anchor.left + anchor.right - w) / 2;
        int topPx = (anchor.top + anchor.bottom - h) / 2;
        x = std::clamp(x, static_cast<int>(work_.left), std::max<int>(work_.left, work_.right - w));
        topPx = std::clamp(topPx, static_cast<int>(work_.top), std::max<int>(work_.top, work_.bottom - h));
        ::SetWindowPos(hwnd_, nullptr, x, topPx, w, h, SWP_NOZORDER | SWP_NOACTIVATE);

        const float footer = footerTop_;
        const float clientWidth = clientWidth_;
        const auto severity = request_.severity;
        const float badgeLeft = badgeLeft_;
        const float badgeTop = badgeTop_;
        surface_.SetDecorator([this, footer, clientWidth, severity, badgeLeft, badgeTop](ID2D1DeviceContext* target, const D2D1_RECT_F&,
                                                                                    const graphics::DesignTokens& tokens) {
            winrt::com_ptr<ID2D1SolidColorBrush> brush;
            if (FAILED(target->CreateSolidColorBrush(tokens.form.footerTint.ToD2D(), brush.put()))) return;
            const auto band = D2D1::RectF(0.0f, footer, clientWidth, 100000.0f);
            target->FillRectangle(band, brush.get());
            // A uniform hairline separates the footer band from the content.
            const float pixel = paint::PixelOf(target);
            brush->SetColor(tokens.form.divider.ToD2D());
            target->FillRectangle(D2D1::RectF(0.0f, footer, clientWidth, footer + pixel), brush.get());
            if (auto badge = BadgeFor(severity)) {
                const float visibleBadgeTop = badgeTop - surface_.ToDip(static_cast<float>(scroll_.Position()));
                target->PushAxisAlignedClip(D2D1::RectF(0.0f, 0.0f, clientWidth, footer), D2D1_ANTIALIAS_MODE_ALIASED);
                graphics::PaintStatusBadge(target, *badge, D2D1::RectF(badgeLeft, visibleBadgeTop, badgeLeft + kBadge, visibleBadgeTop + kBadge), tokens);
                target->PopAxisAlignedClip();
            }
        });
        return true;
    }

    void Choose(ConfirmChoice choice) {
        if (done_) return;
        choice_ = choice;
        done_ = true;
        if (hwnd_) ::PostMessageW(hwnd_, WM_NULL, 0, 0);  // wake the modal loop
    }

    std::optional<ConfirmChoice> ChoiceOfButton(HWND button) const noexcept {
        if (!button || ::GetParent(button) != hwnd_) return std::nullopt;
        switch (::GetDlgCtrlID(button)) {
            case kPrimaryId: return ConfirmChoice::Primary;
            case kSecondaryId: return ConfirmChoice::Secondary;
            case kCancelId: return ConfirmChoice::Cancel;
            default: return std::nullopt;
        }
    }

    bool Intercept(MSG& message) {
        if (!hwnd_ || !::IsWindow(hwnd_) || (message.hwnd != hwnd_ && !::IsChild(hwnd_, message.hwnd))) return false;
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
            // Enter activates the focused push button (it is the default one in a dialog), else the default.
            const auto focused = ChoiceOfButton(::GetFocus());
            Choose(focused ? *focused : EffectiveDefault(request_));
            return true;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            Choose(EscapeChoice(request_));
            return true;
        }
        return ::IsDialogMessageW(hwnd_, &message) != FALSE;
    }

    void Loop() {
        auto* loop = scheduler_ ? scheduler_->Loop() : nullptr;
        if (loop) {
            const bool finished = loop->RunNested([this] { return done_ || !hwnd_ || !::IsWindow(hwnd_); },
                                                  [this](MSG& message) { return Intercept(message); });
            if (!finished) {
                choice_ = ConfirmChoice::Cancel;  // WM_QUIT: re-posted by RunNested
                done_ = true;
            }
            return;
        }
        MSG message{};
        while (!done_ && hwnd_ && ::IsWindow(hwnd_)) {
            const BOOL got = ::GetMessageW(&message, nullptr, 0, 0);
            if (got == 0) {
                ::PostQuitMessage(static_cast<int>(message.wParam));
                choice_ = ConfirmChoice::Cancel;
                done_ = true;
                break;
            }
            if (got == -1) break;
            if (!Intercept(message)) {
                ::TranslateMessage(&message);
                ::DispatchMessageW(&message);
            }
        }
    }

    void Close() noexcept {
        if (ownerWasEnabled_ && ::IsWindow(owner_)) ::EnableWindow(owner_, TRUE);
        ownerWasEnabled_ = false;
        scroll_.Destroy();
        surface_.Detach();
        if (hwnd_ && ::IsWindow(hwnd_)) ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    std::optional<LRESULT> Handle(UINT message, WPARAM wParam, LPARAM lParam) {
        if (auto handled = surface_.HandleMessage(message, wParam, lParam)) return handled;
        switch (message) {
            case WM_COMMAND: {
                const int code = HIWORD(wParam);
                if (code != BN_CLICKED && lParam != 0) return std::nullopt;
                switch (LOWORD(wParam)) {
                    case kPrimaryId: Choose(ConfirmChoice::Primary); return 0;
                    case kSecondaryId: Choose(ConfirmChoice::Secondary); return 0;
                    case kCancelId: Choose(HasCancel(request_) ? ConfirmChoice::Cancel : EscapeChoice(request_)); return 0;
                    default: return std::nullopt;
                }
            }
            case WM_CLOSE:
                Choose(EscapeChoice(request_));
                return 0;
            case WM_DPICHANGED: {
                MONITORINFO info{sizeof(info)};
                if (::GetMonitorInfoW(::MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info)) work_ = info.rcWork;
                (void)Layout();
                return 0;
            }
            case DM_GETDEFID:
                return MAKELRESULT(IdOf(EffectiveDefault(request_)), DC_HASDEFID);
            case WM_ACTIVATE:
                if (LOWORD(wParam) == WA_INACTIVE) {
                    if (HWND focus = ::GetFocus(); focus && ::IsChild(hwnd_, focus)) focusTarget_ = focus;
                } else if (HWND focus = ::GetFocus(); !focus || !::IsChild(hwnd_, focus)) {
                    if (focusTarget_ && ::IsWindow(focusTarget_)) ::SetFocus(focusTarget_);
                }
                return 0;
            case WM_DESTROY:
                if (!done_) {
                    choice_ = ConfirmChoice::Cancel;
                    done_ = true;
                }
                return std::nullopt;
            default:
                return std::nullopt;
        }
    }

    static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        }
        auto* self = reinterpret_cast<Dialog*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) {
            try {
                if (auto handled = self->Handle(message, wParam, lParam)) return *handled;
            } catch (...) {
                AETHER_LOG_ERROR("ui_form", "confirmation message {} failed", message);
            }
            if (message == WM_NCDESTROY) ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static constexpr DWORD kStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;

    const ConfirmRequest& request_;
    graphics::DesignTokens tokens_;
    graphics::MotionPolicy motion_;
    graphics::FrameScheduler* scheduler_ = nullptr;
    FormSurface surface_;
    FormScrollView scroll_;
    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    HWND focusTarget_ = nullptr;  // the control that receives focus whenever the dialog activates
    bool ownerWasEnabled_ = false;
    HWND title_ = nullptr;
    HWND message_ = nullptr;
    HWND detail_ = nullptr;
    HWND icon_ = nullptr;
    std::vector<HWND> buttons_;
    RECT work_{};
    float footerTop_ = 0.0f;
    float clientWidth_ = 0.0f;
    float badgeTop_ = 0.0f;
    float badgeLeft_ = 0.0f;
    bool done_ = false;
    ConfirmChoice choice_ = ConfirmChoice::Cancel;
};

std::wstring FallbackText(const ConfirmRequest& request) {
    std::wstring text = request.title;
    const auto append = [&text](const std::wstring& part) {
        if (part.empty()) return;
        if (!text.empty()) text += L"\r\n\r\n";
        text += part;
    };
    append(request.message);
    append(request.detail);
    return text;
}

}  // namespace

UINT FallbackMessageBoxFlags(const ConfirmRequest& request) noexcept {
    const bool secondary = HasSecondary(request);
    const bool cancel = HasCancel(request);
    UINT flags = secondary ? (cancel ? MB_YESNOCANCEL : MB_YESNO) : (cancel ? MB_OKCANCEL : MB_OK);
    switch (request.severity) {
        case Severity::Warning: flags |= MB_ICONWARNING; break;
        case Severity::Error: flags |= MB_ICONERROR; break;
        case Severity::None: break;
        default: flags |= MB_ICONINFORMATION; break;
    }
    // The default button by position: [Primary][Secondary][Cancel].
    int index = 0;
    switch (EffectiveDefault(request)) {
        case ConfirmChoice::Primary: index = 0; break;
        case ConfirmChoice::Secondary: index = 1; break;
        case ConfirmChoice::Cancel: index = secondary ? 2 : 1; break;
    }
    flags |= index == 2 ? MB_DEFBUTTON3 : index == 1 ? MB_DEFBUTTON2 : MB_DEFBUTTON1;
    return flags;
}

ConfirmChoice ChoiceFromMessageBox(const ConfirmRequest& request, int result) noexcept {
    switch (result) {
        case IDOK:
        case IDYES:
            return ConfirmChoice::Primary;
        case IDNO:
            return HasSecondary(request) ? ConfirmChoice::Secondary : ConfirmChoice::Cancel;
        case IDCANCEL:
            return HasCancel(request) ? ConfirmChoice::Cancel : EscapeChoice(request);
        default:
            // A failed message box never confirms anything.
            return ConfirmChoice::Cancel;
    }
}

Result<ConfirmChoice> ShowConfirmation(const ConfirmRequest& request, const graphics::DesignTokens& tokens,
                                       const graphics::MotionPolicy& motion, graphics::FrameScheduler* scheduler) {
    if (request.primary.empty()) return MakeError(ErrorCode::InvalidArgument, "a confirmation needs a primary button label");
    try {
        std::optional<ConfirmChoice> choice;
        {
            Dialog dialog(request, tokens, motion, scheduler);
            choice = dialog.Run();
        }
        if (choice) return *choice;
        AETHER_LOG_WARN("ui_form", "confirmation surface unavailable; asking with a message box");
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "confirmation presentation failed; asking with a message box");
    }
    const std::wstring text = FallbackText(request);
    const int result = ::MessageBoxW(request.owner && ::IsWindow(request.owner) ? request.owner : nullptr, text.c_str(),
                                     request.windowTitle.c_str(), FallbackMessageBoxFlags(request));
    return ChoiceFromMessageBox(request, result);
}

void SetConfirmationTestHook(std::function<void(HWND dialog)> hook) { confirmationTestHook = std::move(hook); }

}  // namespace aether::ui::form
