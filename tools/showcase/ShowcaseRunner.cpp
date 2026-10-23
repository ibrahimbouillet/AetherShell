// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/CinematicSequence.h"
#include "showcase/CleanSession.h"
#include "showcase/CaptureBuffer.h"
#include "showcase/CodeReadinessObserver.h"
#include "showcase/FrameArchive.h"
#include "showcase/DxgiDesktopCapture.h"
#include "showcase/GpuFrameCompositor.h"
#include "showcase/GifWriter.h"
#include "showcase/Mp4Writer.h"
#include "showcase/Soundtrack.h"
#include "showcase/WicFrameResize.h"
#include "showcase/Win32CleanSessionBackend.h"
#include "showcase/PresentationPlan.h"
#include "showcase/NativePresentationActions.h"
#include "showcase/PngWriter.h"
#include "showcase/ConcurrentRecorder.h"
#include "showcase/ExplorerPinIdentity.h"

#include <Windows.h>
#include <Ole2.h>
#include <UIAutomation.h>
#include <dwmapi.h>
#include <exdisp.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;
using aether::ErrorCode;
using aether::MakeError;
using aether::Result;
using aether::showcase::CinematicSequence;
using aether::showcase::CleanSession;
using aether::showcase::CaptureBuffer;
using aether::showcase::CodeReadinessObserver;
using aether::showcase::CompositeSettings;
using aether::showcase::FrameArchive;
using aether::showcase::DxgiDesktopCaptureSession;
using aether::showcase::GpuFrameCompositor;
using aether::showcase::Mp4Writer;
using aether::showcase::Mp4WriterOptions;
using aether::showcase::GifWriter;
using aether::showcase::GifWriterOptions;
using aether::showcase::Point;
using aether::showcase::Rect;
using aether::showcase::SequenceConfig;
using aether::showcase::SequenceEvents;
using aether::showcase::Size;
using aether::showcase::CreateShowcaseSoundtrack;
using aether::showcase::Win32CleanSessionBackend;

constexpr std::uint32_t kFramesPerSecond = 60;
constexpr std::uint32_t kGracefulCloseMilliseconds = 2500;
constexpr std::uint32_t kMaximumLaunchWaitSeconds = 20;

struct ScopedCom final {
    HRESULT status = E_FAIL;
    ScopedCom() noexcept : status(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ScopedCom() { if (SUCCEEDED(status)) ::CoUninitialize(); }
    [[nodiscard]] bool Usable() const noexcept { return SUCCEEDED(status); }
};

struct ScopedLocalFree final {
    wchar_t* value = nullptr;
    ~ScopedLocalFree() { if (value) (void)::CoTaskMemFree(value); }
};

struct RestoreCursor final {
    POINT original{};
    bool valid = ::GetCursorPos(&original) != FALSE;
    ~RestoreCursor() { Restore(); }
    void Restore() noexcept {
        if (valid) (void)::SetCursorPos(original.x, original.y);
        valid = false;
    }
};

struct WindowByClass final {
    std::wstring_view requested;
    HWND result = nullptr;
};

struct TaskbarVisibility final { bool visible = false; };

BOOL CALLBACK FindVisibleTaskbar(HWND window, LPARAM parameter) noexcept {
    auto& result = *reinterpret_cast<TaskbarVisibility*>(parameter);
    if (!::IsWindowVisible(window)) return TRUE;
    std::array<wchar_t, 64> className{};
    const int length = ::GetClassNameW(window, className.data(), static_cast<int>(className.size()));
    const std::wstring_view actual(className.data(), length > 0 ? static_cast<std::size_t>(length) : 0U);
    if (actual == L"Shell_TrayWnd" || actual == L"Shell_SecondaryTrayWnd") {
        result.visible = true;
        return FALSE;
    }
    return TRUE;
}

bool NativeTaskbarIsHidden() noexcept {
    TaskbarVisibility state;
    (void)::EnumWindows(&FindVisibleTaskbar, reinterpret_cast<LPARAM>(&state));
    return !state.visible;
}

bool WaitForTaskbarOwner(std::wstring_view blockingClass) noexcept {
    if (blockingClass != L"Shell_TrayWnd" && blockingClass != L"Shell_SecondaryTrayWnd") return false;
    // Explorer SHOW notifications and Aether's out-of-process re-hide callback are asynchronous.
    // Discard this acquisition and let the existing replacement owner settle. The capture worker
    // never hides a native taskbar itself or accepts an image while that taskbar is visible.
    const auto deadline = std::chrono::steady_clock::now() + 150ms;
    while (!NativeTaskbarIsHidden()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        ::Sleep(2);
    }
    return SUCCEEDED(::DwmFlush());
}

BOOL CALLBACK FindWindowClass(HWND window, LPARAM parameter) noexcept {
    auto& search = *reinterpret_cast<WindowByClass*>(parameter);
    std::array<wchar_t, 128> name{};
    const int length = ::GetClassNameW(window, name.data(), static_cast<int>(name.size()));
    if (length > 0 && ::CompareStringOrdinal(name.data(), length, search.requested.data(),
            static_cast<int>(search.requested.size()), TRUE) == CSTR_EQUAL && ::IsWindowVisible(window)) {
        search.result = window;
        return FALSE;
    }
    return TRUE;
}

HWND FindAetherWindow(std::wstring_view className) noexcept {
    WindowByClass search{className, nullptr};
    (void)::EnumWindows(&FindWindowClass, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

struct AetherSceneIdentity final {
    DWORD processId = 0;
    HANDLE process = nullptr;
    AetherSceneIdentity() noexcept = default;
    AetherSceneIdentity(const AetherSceneIdentity&) = delete;
    AetherSceneIdentity& operator=(const AetherSceneIdentity&) = delete;
    AetherSceneIdentity(AetherSceneIdentity&& other) noexcept
        : processId(other.processId), process(std::exchange(other.process, nullptr)) {}
    ~AetherSceneIdentity() { if (process) (void)::CloseHandle(process); }

    [[nodiscard]] bool MatchesAnchor(HWND window, std::wstring_view expectedClass) const noexcept {
        if (!window || !::IsWindow(window) || !::IsWindowVisible(window) || ::IsIconic(window)) return false;
        DWORD currentProcess = 0;
        if (!::GetWindowThreadProcessId(window, &currentProcess) || currentProcess != processId) return false;
        std::array<wchar_t, 64> name{};
        const int length = ::GetClassNameW(window, name.data(), static_cast<int>(name.size()));
        return length > 0 && std::wstring_view(name.data(), static_cast<std::size_t>(length)) == expectedClass;
    }
    [[nodiscard]] bool Matches(HWND dock, HWND topbar) const noexcept {
        // Retaining the original process object also rejects an exited/reused PID. Never adopt
        // a replacement process or HWND while a recording is in progress.
        return processId && process && ::WaitForSingleObject(process, 0) == WAIT_TIMEOUT &&
            MatchesAnchor(dock, L"AetherShell.Dock") && MatchesAnchor(topbar, L"AetherShell.TopBar");
    }
};

Result<AetherSceneIdentity> CaptureAetherSceneIdentity(HWND dock, HWND topbar) noexcept {
    AetherSceneIdentity identity;
    if (!dock || !::GetWindowThreadProcessId(dock, &identity.processId) || !identity.processId)
        return MakeError(ErrorCode::Unavailable, "the Aether presentation anchors are unavailable");
    identity.process = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, identity.processId);
    if (!identity.process || !identity.Matches(dock, topbar))
        return MakeError(ErrorCode::Unavailable, "the Aether presentation anchors changed or have different owners");
    return identity;
}

Result<RECT> FindDockCodeIcon(HWND dockWindow) noexcept {
    ComPtr<IUIAutomation> automation;
    HRESULT status = ::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&automation));
    if (FAILED(status)) return MakeError(ErrorCode::Unavailable, "showcase could not inspect the real Dock controls", status);
    ComPtr<IUIAutomationElement> root;
    status = automation->ElementFromHandle(dockWindow, &root);
    if (FAILED(status)) return MakeError(ErrorCode::Unavailable, "showcase could not inspect the real Dock surface", status);
    VARIANT value{};
    ::VariantInit(&value);
    value.vt = VT_BSTR;
    value.bstrVal = ::SysAllocString(L"Visual Studio Code");
    if (!value.bstrVal) return MakeError(ErrorCode::Unavailable, "showcase could not allocate a Dock lookup value");
    ComPtr<IUIAutomationCondition> condition;
    status = automation->CreatePropertyCondition(UIA_NamePropertyId, value, &condition);
    ::VariantClear(&value);
    if (FAILED(status)) return MakeError(ErrorCode::Unavailable, "showcase could not prepare a Dock lookup", status);
    ComPtr<IUIAutomationElement> item;
    status = root->FindFirst(TreeScope_Children, condition.Get(), &item);
    if (FAILED(status) || !item)
        return MakeError(ErrorCode::NotFound, "the real Visual Studio Code pin is not present in the Dock", status);
    RECT bounds{};
    status = item->get_CurrentBoundingRectangle(&bounds);
    if (FAILED(status) || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        return MakeError(ErrorCode::Unavailable, "the real Dock icon position is unavailable", status);
    RECT result{static_cast<LONG>(std::floor(bounds.left)), static_cast<LONG>(std::floor(bounds.top)),
                static_cast<LONG>(std::ceil(bounds.right)), static_cast<LONG>(std::ceil(bounds.bottom))};
    return result;
}

Result<RECT> FindDockVisibleControls(HWND dockWindow) noexcept {
    ComPtr<IUIAutomation> automation;
    HRESULT status = ::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&automation));
    ComPtr<IUIAutomationElement> root;
    if (SUCCEEDED(status)) status = automation->ElementFromHandle(dockWindow, &root);
    ComPtr<IUIAutomationCondition> condition;
    if (SUCCEEDED(status)) status = automation->CreateTrueCondition(&condition);
    ComPtr<IUIAutomationElementArray> items;
    if (SUCCEEDED(status)) status = root->FindAll(TreeScope_Children, condition.Get(), &items);
    int count = 0;
    if (SUCCEEDED(status)) status = items->get_Length(&count);
    if (FAILED(status) || count < 2 || count > 128)
        return MakeError(ErrorCode::Unavailable, "the visible Dock controls could not be measured", status);
    RECT unionBounds{};
    for (int index = 0; index < count; ++index) {
        ComPtr<IUIAutomationElement> item;
        RECT bounds{};
        status = items->GetElement(index, &item);
        if (SUCCEEDED(status)) status = item->get_CurrentBoundingRectangle(&bounds);
        if (FAILED(status) || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
            return MakeError(ErrorCode::Unavailable, "a real Dock control has no usable geometry", status);
        if (index == 0) unionBounds = bounds;
        else (void)::UnionRect(&unionBounds, &unionBounds, &bounds);
    }
    return unionBounds;
}

std::wstring NormalizeExplorerTarget(std::wstring_view locationUrl) {
    if (locationUrl.empty()) return {};
    std::vector<wchar_t> path(32'768, L'\0');
    DWORD capacity = static_cast<DWORD>(path.size());
    const std::wstring url(locationUrl);
    if (SUCCEEDED(::PathCreateFromUrlW(url.c_str(), path.data(), &capacity, 0)) && capacity > 0) {
        while (capacity > 0 && path[capacity - 1] == L'\0') --capacity;
        return std::wstring(path.data(), capacity);
    }
    if (locationUrl.starts_with(L"shell:") || locationUrl.starts_with(L"::{")) return std::wstring(locationUrl);
    return {};
}

struct CabinetWindowList final {
    std::vector<HWND> windows;
    bool allocationFailed = false;
};

BOOL CALLBACK CollectCabinetWindow(HWND window, LPARAM parameter) noexcept {
    auto& result = *reinterpret_cast<CabinetWindowList*>(parameter);
    if (!::IsWindowVisible(window)) return TRUE;
    std::array<wchar_t, 64> className{};
    const int length = ::GetClassNameW(window, className.data(), static_cast<int>(className.size()));
    if (length <= 0 || (::CompareStringOrdinal(className.data(), length, L"CabinetWClass", 13, TRUE) != CSTR_EQUAL &&
                        ::CompareStringOrdinal(className.data(), length, L"ExploreWClass", 13, TRUE) != CSTR_EQUAL))
        return TRUE;
    try {
        result.windows.push_back(window);
    } catch (...) {
        result.allocationFailed = true;
        ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    return TRUE;
}

class ExplorerWindowRestorer final {
public:
    ExplorerWindowRestorer() = default;
    ExplorerWindowRestorer(const ExplorerWindowRestorer&) = delete;
    ExplorerWindowRestorer& operator=(const ExplorerWindowRestorer&) = delete;
    ~ExplorerWindowRestorer() { (void)Restore(); }

    [[nodiscard]] Result<void> Snapshot() {
        CabinetWindowList visible;
        if (!::EnumWindows(&CollectCabinetWindow, reinterpret_cast<LPARAM>(&visible)) || visible.allocationFailed)
            return MakeError(ErrorCode::Unavailable, "showcase could not snapshot File Explorer windows");
        if (visible.windows.empty()) return aether::Ok();

        ComPtr<IShellWindows> shellWindows;
        HRESULT status = ::CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER,
                                             IID_PPV_ARGS(&shellWindows));
        if (FAILED(status)) return MakeError(ErrorCode::Unavailable, "showcase could not prepare Explorer restoration", status);
        long count = 0;
        status = shellWindows->get_Count(&count);
        if (FAILED(status)) return MakeError(ErrorCode::Unavailable, "showcase could not inspect Explorer windows", status);
        for (long index = 0; index < count; ++index) {
            VARIANT itemIndex{};
            ::VariantInit(&itemIndex);
            itemIndex.vt = VT_I4;
            itemIndex.lVal = index;
            ComPtr<IDispatch> dispatch;
            status = shellWindows->Item(itemIndex, &dispatch);
            if (FAILED(status) || !dispatch) continue;
            ComPtr<IWebBrowser2> browser;
            if (FAILED(dispatch.As(&browser))) continue;
            SHANDLE_PTR nativeWindow = 0;
            if (FAILED(browser->get_HWND(&nativeWindow)) || nativeWindow == 0) continue;
            const HWND window = reinterpret_cast<HWND>(nativeWindow);
            if (std::find(visible.windows.begin(), visible.windows.end(), window) == visible.windows.end()) continue;
            VARIANT_BOOL shown = VARIANT_FALSE;
            if (FAILED(browser->get_Visible(&shown)) || shown == VARIANT_FALSE) continue;
            BSTR rawUrl = nullptr;
            if (FAILED(browser->get_LocationURL(&rawUrl)) || !rawUrl) {
                if (rawUrl) ::SysFreeString(rawUrl);
                return MakeError(ErrorCode::Unavailable, "showcase could not safely prepare an Explorer window for restoration");
            }
            std::wstring target = NormalizeExplorerTarget(rawUrl);
            ::SysFreeString(rawUrl);
            if (target.empty())
                return MakeError(ErrorCode::Unsupported, "showcase cannot safely restore this Explorer location");
            targets_.push_back(std::move(target));
            windows_.push_back(window);
        }
        if (windows_.size() != visible.windows.size())
            return MakeError(ErrorCode::Unsupported, "showcase could not prepare every visible Explorer window for restoration");
        return aether::Ok();
    }

    [[nodiscard]] Result<void> Close() noexcept {
        closed_ = true;
        for (const HWND window : windows_) {
            if (::IsWindow(window) && !::PostMessageW(window, WM_CLOSE, 0, 0))
                return MakeError(ErrorCode::Unavailable, "showcase could not close a File Explorer window");
        }
        const auto deadline = std::chrono::steady_clock::now() + 6s;
        for (;;) {
            const bool anyRemain = std::any_of(windows_.begin(), windows_.end(), [](HWND window) { return ::IsWindow(window) != FALSE; });
            if (!anyRemain) {
                closed_ = true;
                return aether::Ok();
            }
            if (std::chrono::steady_clock::now() >= deadline)
                return MakeError(ErrorCode::Timeout, "a File Explorer window did not close for the showcase");
            ::Sleep(25);
        }
    }

    [[nodiscard]] Result<void> Restore() noexcept {
        if (!closed_) return aether::Ok();
        bool failed = false;
        for (const auto& target : targets_) {
            const std::wstring parameters = L"\"" + target + L"\"";
            const auto position = static_cast<std::size_t>(&target - targets_.data());
            const bool originalWindowStillExists = position < windows_.size() && ::IsWindow(windows_[position]);
            if (originalWindowStillExists) continue;
            const auto result = reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, L"open", L"explorer.exe",
                parameters.c_str(), nullptr, SW_SHOWNORMAL));
            if (result <= 32) failed = true;
        }
        closed_ = false;
        if (failed) return MakeError(ErrorCode::Unavailable, "one or more Explorer windows could not be reopened");
        return aether::Ok();
    }

private:
    std::vector<HWND> windows_;
    std::vector<std::wstring> targets_;
    bool closed_ = false;
};

struct CodeWindowSearch final { bool found = false; };

BOOL CALLBACK FindVisibleCodeWindow(HWND window, LPARAM parameter) noexcept {
    auto& result = *reinterpret_cast<CodeWindowSearch*>(parameter);
    if (!::IsWindowVisible(window) || ::GetWindow(window, GW_OWNER) != nullptr) return TRUE;
    DWORD processId = 0;
    (void)::GetWindowThreadProcessId(window, &processId);
    if (processId == 0) return TRUE;
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return TRUE;
    std::array<wchar_t, 32'768> image{};
    DWORD length = static_cast<DWORD>(image.size());
    const BOOL queried = ::QueryFullProcessImageNameW(process, 0, image.data(), &length);
    (void)::CloseHandle(process);
    if (queried && length >= 8 && ::CompareStringOrdinal(image.data() + length - 8, 8, L"Code.exe", 8, TRUE) == CSTR_EQUAL) {
        result.found = true;
        return FALSE;
    }
    return TRUE;
}

bool HasVisibleCodeWindow() noexcept {
    CodeWindowSearch search;
    (void)::EnumWindows(&FindVisibleCodeWindow, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

struct CloseCodeWindowsContext final { bool failed = false; };

BOOL CALLBACK CloseCodeWindow(HWND window, LPARAM parameter) noexcept {
    auto& context = *reinterpret_cast<CloseCodeWindowsContext*>(parameter);
    if (!::IsWindowVisible(window) || ::GetWindow(window, GW_OWNER) != nullptr) return TRUE;
    DWORD processId = 0;
    (void)::GetWindowThreadProcessId(window, &processId);
    if (processId == 0) return TRUE;
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return TRUE;
    std::array<wchar_t, 32'768> image{};
    DWORD length = static_cast<DWORD>(image.size());
    const BOOL queried = ::QueryFullProcessImageNameW(process, 0, image.data(), &length);
    (void)::CloseHandle(process);
    if (!queried || length < 8 ||
        ::CompareStringOrdinal(image.data() + length - 8, 8, L"Code.exe", 8, TRUE) != CSTR_EQUAL) return TRUE;
    if (!::PostMessageW(window, WM_CLOSE, 0, 0)) context.failed = true;
    return TRUE;
}

Result<void> CloseVisibleCodeWindows() noexcept {
    CloseCodeWindowsContext context;
    (void)::EnumWindows(&CloseCodeWindow, reinterpret_cast<LPARAM>(&context));
    if (context.failed) return MakeError(ErrorCode::Unavailable, "showcase could not close its temporary Code window");
    const auto deadline = std::chrono::steady_clock::now() + 6s;
    while (HasVisibleCodeWindow()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return MakeError(ErrorCode::Timeout, "the temporary Code window did not close after capture");
        ::Sleep(25);
    }
    return aether::Ok();
}

class CodeWindowCleanup final {
public:
    ~CodeWindowCleanup() { if (armed_) (void)CloseVisibleCodeWindows(); }
    void Arm() noexcept { armed_ = true; }
    [[nodiscard]] Result<void> Close() noexcept {
        if (!armed_) return aether::Ok();
        auto result = CloseVisibleCodeWindows();
        if (result) armed_ = false;
        return result;
    }
private:
    bool armed_ = false;
};

class CaptureControllerWindows final {
public:
    ~CaptureControllerWindows() { Restore(); }
    Result<void> Hide(std::span<const aether::showcase::ProcessIdentity> processes) {
        processes_ = processes;
        if (!::EnumWindows(&HideWindow, reinterpret_cast<LPARAM>(this)) || failed_) {
            Restore();
            return MakeError(ErrorCode::Unavailable, "showcase could not put its retained controller windows aside");
        }
        return aether::Ok();
    }
    void Restore() noexcept {
        for (const auto& saved : windows_) {
            DWORD processId = 0;
            (void)::GetWindowThreadProcessId(saved.window, &processId);
            if (::IsWindow(saved.window) && processId == saved.processId) {
                auto placement = saved.placement;
                (void)::SetWindowPlacement(saved.window, &placement);
                (void)::ShowWindow(saved.window, saved.placement.showCmd == SW_SHOWMAXIMIZED
                    ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE);
            }
        }
        windows_.clear();
    }
private:
    struct SavedWindow { HWND window; DWORD processId; WINDOWPLACEMENT placement; };
    static BOOL CALLBACK HideWindow(HWND window, LPARAM parameter) noexcept {
        auto& self = *reinterpret_cast<CaptureControllerWindows*>(parameter);
        if (!::IsWindowVisible(window) || ::IsIconic(window)) return TRUE;
        DWORD processId = 0;
        (void)::GetWindowThreadProcessId(window, &processId);
        const auto found = std::find_if(self.processes_.begin(), self.processes_.end(),
            [&](const auto& process) { return process.processId == processId; });
        if (found == self.processes_.end()) return TRUE;
        std::array<wchar_t, 64> name{};
        (void)::GetClassNameW(window, name.data(), static_cast<int>(name.size()));
        const std::wstring_view type(name.data());
        if (type == L"Progman" || type == L"WorkerW" || type == L"Shell_TrayWnd" ||
            type == L"Shell_SecondaryTrayWnd") return TRUE;
        WINDOWPLACEMENT placement{sizeof(placement)};
        if (!::GetWindowPlacement(window, &placement)) { self.failed_ = true; return FALSE; }
        try { self.windows_.push_back({window, processId, placement}); }
        catch (...) { self.failed_ = true; return FALSE; }
        (void)::ShowWindow(window, SW_HIDE);
        if (::IsWindowVisible(window)) { self.failed_ = true; return FALSE; }
        return TRUE;
    }
    std::span<const aether::showcase::ProcessIdentity> processes_;
    std::vector<SavedWindow> windows_;
    bool failed_ = false;
};

class DesktopIconVisibility final {
public:
    ~DesktopIconVisibility() { Restore(); }
    void Hide() noexcept {
        (void)::EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
            auto& self = *reinterpret_cast<DesktopIconVisibility*>(parameter);
            const HWND view = ::FindWindowExW(window, nullptr, L"SHELLDLL_DefView", nullptr);
            if (!view) return TRUE;
            const HWND icons = ::FindWindowExW(view, nullptr, L"SysListView32", nullptr);
            if (icons && ::IsWindowVisible(icons)) {
                self.icons_ = icons;
                (void)::ShowWindow(icons, SW_HIDE);
            }
            return FALSE;
        }, reinterpret_cast<LPARAM>(this));
    }
    void Restore() noexcept {
        if (icons_ && ::IsWindow(icons_)) (void)::ShowWindow(icons_, SW_SHOWNA);
        icons_ = nullptr;
    }
private:
    HWND icons_ = nullptr;
};

bool CleanSceneVisible(const AetherSceneIdentity& identity, HWND dock, HWND topbar,
                       const RECT& monitor, std::wstring& blockingClass,
                       bool allowCode = false, bool allowPresentation = false) noexcept {
    blockingClass.clear();
    if (!identity.Matches(dock, topbar)) {
        blockingClass = L"aether-anchor-changed";
        return false;
    }
    struct Scene {
        HWND dock; HWND topbar; RECT monitor; std::wstring& blockingClass;
        const AetherSceneIdentity& identity; bool allowCode; bool allowPresentation; bool clean = true;
    } scene{dock, topbar, monitor, blockingClass, identity, allowCode, allowPresentation};
    const BOOL enumerated = ::EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& state = *reinterpret_cast<Scene*>(parameter);
        if (!::IsWindowVisible(window) || ::IsIconic(window)) return TRUE;
        if (window == state.dock || window == state.topbar) {
            if (state.identity.MatchesAnchor(window, window == state.dock ? L"AetherShell.Dock" : L"AetherShell.TopBar"))
                return TRUE;
            state.blockingClass = L"aether-anchor-changed";
            state.clean = false;
            return FALSE;
        }
        if ((::GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYERED) != 0) {
            COLORREF key = 0; BYTE alpha = 255; DWORD flags = 0;
            if (::GetLayeredWindowAttributes(window, &key, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0)
                return TRUE;  // verified zero-alpha infrastructure has no desktop pixels
        }
        DWORD cloaked = 0;
        if (SUCCEEDED(::DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
        RECT bounds{}, intersection{};
        if (!::GetWindowRect(window, &bounds)) {
            // Hover surfaces can be destroyed between enumeration and inspection. A vanished
            // window has no captured pixels; an uninspectable live surface still fails closed.
            if (!::IsWindow(window) || !::IsWindowVisible(window)) return TRUE;
            state.blockingClass = L"uninspectable-live-window";
            state.clean = false;
            return FALSE;
        }
        if (!::IntersectRect(&intersection, &bounds, &state.monitor) ||
            intersection.right - intersection.left < 2 || intersection.bottom - intersection.top < 2) return TRUE;
        std::array<wchar_t, 64> name{};
        (void)::GetClassNameW(window, name.data(), static_cast<int>(name.size()));
        const std::wstring_view type(name.data());
        if (type == L"Progman" || type == L"WorkerW") return TRUE;
        DWORD processId = 0;
        (void)::GetWindowThreadProcessId(window, &processId);
        if (type == L"AetherShell.Tooltip" && processId != 0 && processId == state.identity.processId)
            return TRUE;  // The genuine Dock's own hover surface is part of the product.
        if (state.allowPresentation && processId != 0 && processId == state.identity.processId &&
            (type == L"AetherShell.ControlCenter" || type == L"AetherShell.Settings" ||
             type == L"AetherShell.Search" || type == L"#32768"))
            return TRUE;
        if (state.allowCode && processId != 0) {
            HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
            if (process) {
                std::array<wchar_t, 32'768> image{};
                DWORD length = static_cast<DWORD>(image.size());
                const BOOL queried = ::QueryFullProcessImageNameW(process, 0, image.data(), &length);
                (void)::CloseHandle(process);
                if (queried) {
                    const std::wstring_view imagePath(image.data(), length);
                    const auto separator = imagePath.find_last_of(L"\\/");
                    const auto basename = imagePath.substr(separator == std::wstring_view::npos ? 0U : separator + 1U);
                    if (::CompareStringOrdinal(basename.data(), static_cast<int>(basename.size()),
                                               L"Code.exe", 8, TRUE) == CSTR_EQUAL) return TRUE;
                }
            }
        }
        if (!::IsWindow(window) || !::IsWindowVisible(window)) return TRUE;
        try { state.blockingClass = type; } catch (...) {}
        state.clean = false;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&scene));
    if (!identity.Matches(dock, topbar)) {
        blockingClass = L"aether-anchor-changed";
        return false;
    }
    return enumerated != FALSE && scene.clean;
}

bool ClickAt(Point position) noexcept {
    if (!::SetCursorPos(static_cast<int>(std::lround(position.x)), static_cast<int>(std::lround(position.y)))) return false;
    INPUT inputs[2]{};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    return ::SendInput(2, inputs, sizeof(INPUT)) == 2;
}

bool MoveRealPointer(Point position) noexcept {
    const int left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 1 || height <= 1) return false;
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>(std::lround((position.x - left) * 65535.0 / (width - 1)));
    input.mi.dy = static_cast<LONG>(std::lround((position.y - top) * 65535.0 / (height - 1)));
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    return ::SendInput(1, &input, sizeof(input)) == 1;
}

class BackdropProbe final {
public:
    void Remember(const aether::showcase::RgbaFrameView& frame, Point initialCursor) noexcept {
        count_ = 0;
        for (std::uint32_t row = 0; row < 24; ++row) {
            for (std::uint32_t column = 0; column < 32; ++column) {
                const auto x = static_cast<std::uint32_t>(frame.width * (0.15 + 0.70 * column / 31.0));
                const auto y = static_cast<std::uint32_t>(frame.height * (0.15 + 0.60 * row / 23.0));
                // The real pointer belongs to the recording, not to its wallpaper fingerprint.
                if (std::abs(static_cast<double>(x) - initialCursor.x) < 48 &&
                    std::abs(static_cast<double>(y) - initialCursor.y) < 48) continue;
                const auto offset = static_cast<std::size_t>(y) * frame.rowStrideBytes + x * 4U;
                samples_[count_++] = {x, y, {frame.pixels[offset], frame.pixels[offset + 1U], frame.pixels[offset + 2U]}};
            }
        }
    }
    bool Matches(const aether::showcase::RgbaFrameView& frame) const noexcept {
        if (count_ == 0) return false;
        for (std::size_t index = 0; index < count_; ++index) {
            const auto& sample = samples_[index];
            const auto offset = static_cast<std::size_t>(sample.y) * frame.rowStrideBytes + sample.x * 4U;
            for (std::size_t channel = 0; channel < 3; ++channel)
                if (std::abs(static_cast<int>(frame.pixels[offset + channel]) - sample.rgb[channel]) > 2) return false;
        }
        return true;
    }
private:
    struct Sample { std::uint32_t x = 0, y = 0; std::array<std::uint8_t, 3> rgb{}; };
    std::array<Sample, 768> samples_{};
    std::size_t count_ = 0;
};

void WaitUntil(std::chrono::steady_clock::time_point target) noexcept {
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= target) return;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(target - now).count();
        if (remaining > 2) ::Sleep(static_cast<DWORD>(remaining - 1));
        else ::SwitchToThread();
    }
}

std::wstring UniqueToken() {
    GUID value{};
    if (FAILED(::CoCreateGuid(&value))) return {};
    std::array<wchar_t, 40> text{};
    if (::StringFromGUID2(value, text.data(), static_cast<int>(text.size())) == 0) return {};
    std::wstring result;
    for (const wchar_t character : text) if (character != L'{' && character != L'}' && character != L'-' && character != L'\0')
        result.push_back(character);
    return result;
}

Result<std::filesystem::path> KnownFolder(REFKNOWNFOLDERID folder) {
    ScopedLocalFree storage;
    const HRESULT status = ::SHGetKnownFolderPath(folder, KF_FLAG_CREATE, nullptr, &storage.value);
    if (FAILED(status) || !storage.value)
        return MakeError(ErrorCode::Unavailable, "showcase could not access a local output folder", status);
    return std::filesystem::path(storage.value);
}

Result<void> EnsureSpace(const std::filesystem::path& localPath, std::uint64_t requiredBytes) noexcept {
    std::array<wchar_t, 32'768> volume{};
    if (!::GetVolumePathNameW(localPath.c_str(), volume.data(), static_cast<DWORD>(volume.size())))
        return MakeError(ErrorCode::Unavailable, "showcase could not check local capture storage", ::GetLastError());
    ULARGE_INTEGER available{};
    if (!::GetDiskFreeSpaceExW(volume.data(), &available, nullptr, nullptr))
        return MakeError(ErrorCode::Unavailable, "showcase could not check local capture storage", ::GetLastError());
    if (available.QuadPart < requiredBytes)
        return MakeError(ErrorCode::InsufficientSpace, "showcase local capture storage is insufficient");
    return aether::Ok();
}

Result<std::filesystem::path> BuildOutputPath(const std::filesystem::path& videos, std::wstring_view token) {
    try {
        const auto directory = videos / L"Aether Shell";
        std::filesystem::create_directories(directory);
        return directory / (L"Aether-Shell-Showcase-" + std::wstring(token) + L".mp4");
    } catch (...) {
        return MakeError(ErrorCode::Io, "showcase could not prepare the local video output folder");
    }
}

struct CaptureReport final {
    std::filesystem::path output;
    double durationSeconds = 0.0;
    double meanCaptureFramesPerSecond = 0.0;
    double slowCapturePercent = 0.0;
    std::size_t sourceFrames = 0;
    std::size_t outputFrames = 0;
    double slowOutputPercent = 0.0;
    double p95CaptureIntervalMilliseconds = 0.0;
    double meanCompositeMilliseconds = 0.0;
    double p95CompositeMilliseconds = 0.0;
    bool hardwareAccelerated = false;
    bool qualityReceiptWritten = false;
    bool restorationSucceeded = false;
};

double PercentileMilliseconds(std::vector<std::uint64_t> durations, double percentile) {
    if (durations.empty()) return 0.0;
    std::sort(durations.begin(), durations.end());
    const auto rank = static_cast<std::size_t>(std::ceil(percentile * static_cast<double>(durations.size())));
    const auto index = std::min(durations.size() - 1U, rank == 0 ? 0U : rank - 1U);
    return static_cast<double>(durations[index]) / 1000.0;
}

bool WriteLocalQualityReceipt(const CaptureReport& report, std::uint32_t width, std::uint32_t height) noexcept {
    try {
        auto receiptPath = report.output;
        receiptPath.replace_extension(L".qc.txt");
        std::ofstream receipt(receiptPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!receipt) return false;
        receipt << "Aether Shell showcase local quality receipt\n"
                << "video=" << width << 'x' << height << " progressive SDR BT.709\n"
                << "video_fps=60\n"
                << "duration_seconds=" << report.durationSeconds << '\n'
                << "source_frames=" << report.sourceFrames << '\n'
                << "source_mean_fps=" << report.meanCaptureFramesPerSecond << '\n'
                << "source_p95_interval_ms=" << report.p95CaptureIntervalMilliseconds << '\n'
                << "source_intervals_over_25ms_percent=" << report.slowCapturePercent << '\n'
                << "output_frames=" << report.outputFrames << '\n'
                << "composite_mean_ms=" << report.meanCompositeMilliseconds << '\n'
                << "composite_p95_ms=" << report.p95CompositeMilliseconds << '\n'
                << "composite_over_16_667ms_percent=" << report.slowOutputPercent << '\n'
                << "hardware_accelerated=" << (report.hardwareAccelerated ? "true" : "false") << '\n'
                << "application_relaunch_attempts_succeeded=" << (report.restorationSucceeded ? "true" : "false") << '\n'
                << "audio=AAC-LC stereo 48000 Hz 192000 bit/s; original procedural score\n"
                << "privacy=local capture; review required before sharing\n";
        receipt.flush();
        return receipt.good();
    } catch (...) {
        return false;
    }
}

void WriteLocalRunStatus(std::string_view phase, const aether::Error* error = nullptr) noexcept {
    try {
        auto local = KnownFolder(FOLDERID_LocalAppData);
        if (!local) return;
        const auto directory = *local / L"AetherShell" / L"Showcase";
        std::filesystem::create_directories(directory);
        std::ofstream status(directory / L"last-run-status.txt", std::ios::binary | std::ios::trunc);
        status << "phase=" << phase << '\n';
        // All runner/backend errors are fixed diagnostic text, never OS descriptions or paths.
        if (error) status << "error=" << error->message << "\nplatform_code=" << error->platformCode << '\n';
    } catch (...) {}
}

void WriteRenderProgress(std::string_view stage, std::size_t frame, std::size_t total) noexcept {
    try {
        auto local = KnownFolder(FOLDERID_LocalAppData);
        if (!local) return;
        std::ofstream status(*local / L"AetherShell" / L"Showcase" / L"last-run-status.txt",
                             std::ios::binary | std::ios::trunc);
        status << "phase=rendering\nstage=" << stage << "\nframe=" << frame << "\ntotal=" << total << '\n';
    } catch (...) {}
}

struct RunnerOptions final {
    bool preflightOnly = false;
    bool quiet = false;
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
    std::filesystem::path renderRecipe;
    std::filesystem::path actionPlan;
    std::filesystem::path sceneSettings;
    std::string scenario;
    bool allowDeviceChanges = false;
    std::uint32_t presentationFramesPerSecond = 60;
    std::vector<std::uint32_t> keepProcessIds;
};

Result<aether::showcase::PresentationPlan> LoadPresentationPlan(const RunnerOptions& options) {
    if (options.actionPlan.empty()) return aether::showcase::MakePresentationPlan(options.scenario);
    const auto native = options.actionPlan.native();
    if (!options.actionPlan.is_absolute() || native.starts_with(L"\\\\"))
        return MakeError(ErrorCode::Validation, "presentation plans must use an absolute local file");
    std::ifstream input(options.actionPlan, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > 16'384)
        return MakeError(ErrorCode::Validation, "the local presentation plan is unavailable or too large");
    const auto bytes = static_cast<std::size_t>(input.tellg());
    input.seekg(0);
    std::string text(bytes, '\0');
    if (!input.read(text.data(), static_cast<std::streamsize>(bytes)))
        return MakeError(ErrorCode::Io, "the local presentation plan could not be read");
    return aether::showcase::ParsePresentationPlan(text);
}

Result<std::wstring> LoadSceneExplorerName(const RunnerOptions& options,
    const aether::showcase::PresentationPlan& plan) {
    const bool needed=std::any_of(plan.actions.begin(),plan.actions.end(),[](const auto& action){
        return action.kind==aether::showcase::PresentationActionKind::DockExplorerMenu;
    });
    if(!needed)return std::wstring{};
    if(options.sceneSettings.empty()||!options.sceneSettings.is_absolute()||options.sceneSettings.native().starts_with(L"\\\\"))
        return MakeError(ErrorCode::Validation,"the full gallery requires --scene-settings with an absolute local settings file");
    std::ifstream input(options.sceneSettings,std::ios::binary|std::ios::ate);
    if(!input||input.tellg()<0||input.tellg()>1024*1024)
        return MakeError(ErrorCode::Validation,"scene settings are unavailable or exceed supported bounds");
    std::string data(static_cast<std::size_t>(input.tellg()),'\0');
    input.seekg(0);
    if(!input.read(data.data(),static_cast<std::streamsize>(data.size())))
        return MakeError(ErrorCode::Io,"scene settings could not be read");
    std::array<wchar_t,32'768> windowsDirectory{};
    const UINT length=::GetWindowsDirectoryW(windowsDirectory.data(),static_cast<UINT>(windowsDirectory.size()));
    if(!length||length>=windowsDirectory.size())return MakeError(ErrorCode::Unavailable,"the trusted Windows Explorer identity is unavailable");
    const auto executable=std::filesystem::path(windowsDirectory.data())/L"explorer.exe";
    return aether::showcase::ResolveExplorerPinName(data,executable.native());
}

Result<std::size_t> ValidatePreflight(const RunnerOptions& options) {
    ScopedCom apartment;
    if (!apartment.Usable()) return MakeError(ErrorCode::Unavailable, "showcase preflight could not initialize Windows UI automation");
    if (!NativeTaskbarIsHidden())
        return MakeError(ErrorCode::Unavailable, "the Windows taskbar is still visible; enable Aether taskbar replacement first");
    const HWND dock = FindAetherWindow(L"AetherShell.Dock");
    const HWND topbar = FindAetherWindow(L"AetherShell.TopBar");
    if (!dock || !topbar) return MakeError(ErrorCode::Unavailable, "Aether Dock and Top Status Bar must already be running");
    auto aetherIdentity = CaptureAetherSceneIdentity(dock, topbar);
    if (!aetherIdentity) return aetherIdentity.error();
    if (options.scenario.empty() && options.actionPlan.empty()) {
        auto codeIcon = FindDockCodeIcon(dock);
        if (!codeIcon) return codeIcon.error();
    } else {
        auto plan = LoadPresentationPlan(options);
        if (!plan) return plan.error();
        auto explorerName = LoadSceneExplorerName(options,*plan);
        if (!explorerName) return explorerName.error();
        bool changesVolume = false;
        for (const auto& action : plan->actions)
            if (action.kind == aether::showcase::PresentationActionKind::VolumeOffset) changesVolume = true;
        if (changesVolume && !options.allowDeviceChanges)
            return MakeError(ErrorCode::AccessDenied, "the volume scenario requires --allow-device-changes");
        auto controls = aether::showcase::NativePresentationActions::Create(dock, topbar,std::move(*explorerName));
        if (!controls) return controls.error();
        if (auto prepared = controls->Prepare(changesVolume); !prepared) return prepared.error();
    }
    auto backend = Win32CleanSessionBackend::Create(options.keepProcessIds);
    if (!backend) return backend.error();
    auto sessionId = Win32CleanSessionBackend::CurrentSessionId();
    if (!sessionId) return sessionId.error();
    ExplorerWindowRestorer explorer;
    if (auto snapshot = explorer.Snapshot(); !snapshot) return snapshot.error();
    auto applications = backend->EnumerateVisibleApplications(*sessionId);
    if (!applications) return applications.error();
    const auto selected = CleanSession::SelectApplicationsToClose(*applications, *sessionId);
    for (const auto process : selected) {
        auto descriptor = backend->CaptureRelaunchDescriptor(process);
        if (!descriptor)
            return MakeError(ErrorCode::Unavailable,
                "showcase preflight could not prepare every visible app for relaunch; no windows were closed");
        descriptor->Clear();
    }
    if (!aetherIdentity->Matches(dock, topbar))
        return MakeError(ErrorCode::Unavailable, "the Aether presentation anchors changed during preflight");
    return selected.size();
}

Result<void> SaveRecipe(const std::filesystem::path& archivePath, const SequenceConfig& config,
                        const SequenceEvents& events, bool restored) {
    if (config.timing.easingProfile != aether::showcase::EasingProfile::SmoothQuintic)
        return MakeError(ErrorCode::Unsupported, "the capture recipe cannot record custom easing overrides");
    auto path = archivePath;
    path.replace_extension(L".aetherrecipe");
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file || !events.visualStudioCodeWindowVisibleAt)
        return MakeError(ErrorCode::Io, "the local numeric capture recipe could not be saved");
    file << std::setprecision(17) << "AETHER_RECIPE_2\n"
         << config.sourcePixels.width << ' ' << config.sourcePixels.height << ' '
         << config.outputPixels.width << ' ' << config.outputPixels.height << '\n'
         << config.dockBoundsPixels.left << ' ' << config.dockBoundsPixels.top << ' '
         << config.dockBoundsPixels.right << ' ' << config.dockBoundsPixels.bottom << '\n'
         << config.visualStudioCodeIconPixels.x << ' ' << config.visualStudioCodeIconPixels.y << ' '
         << config.cursorStartPixels.x << ' ' << config.cursorStartPixels.y << '\n'
         << *events.visualStudioCodeWindowVisibleAt << ' '
         << events.visualStudioCodeWindowClosedAt.value_or(-1.0) << ' ' << restored << '\n';
    const auto& t = config.timing;
    file << t.desktopHold << ' ' << t.cursorApproach << ' ' << t.dockDive << ' ' << t.horizontalTrack << ' '
         << t.focusPause << ' ' << t.cameraReveal << ' ' << t.finalHold << ' ' << t.postCloseHold << ' ' << t.topMarginPixels << ' '
         << t.cursorAnchorX << ' ' << t.dockEntryFraction << ' ' << t.maximumDockZoom << '\n';
    file.flush();
    return file.good() ? aether::Ok() : Result<void>(MakeError(ErrorCode::Io, "the local capture recipe was incomplete"));
}

Result<CaptureReport> RenderArchive(FrameArchive& archive, SequenceConfig config,
                                    const SequenceEvents& events, bool restorationSucceeded) {
    // Offline work must yield to the interactive desktop. It never changes display settings.
    (void)::SetPriorityClass(::GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    // Recipe version one records only the default easing profile. Do not silently rerender an
    // unrecorded custom curve or allocate from unchecked event/timing values.
    if (config.timing.easingProfile != aether::showcase::EasingProfile::SmoothQuintic)
        return MakeError(ErrorCode::Unsupported, "the capture recipe does not support custom easing overrides");
    auto sequence = CinematicSequence::Create(config);
    if (!sequence) return sequence.error();
    if (config.sourcePixels.width != archive.Width() || config.sourcePixels.height != archive.Height())
        return MakeError(ErrorCode::Validation, "the capture recipe does not match its frame archive");
    if (config.outputPixels.width > 16'384 || config.outputPixels.height > 16'384 ||
        std::floor(config.outputPixels.width) != config.outputPixels.width ||
        std::floor(config.outputPixels.height) != config.outputPixels.height)
        return MakeError(ErrorCode::Validation, "the capture recipe output dimensions are unsupported");
    const auto& frameTimes = archive.CaptureTimes();
    constexpr double kMaximumRenderDurationSeconds = 120.0;
    constexpr double kSourceCoverageToleranceSeconds = 1.0 / kFramesPerSecond;
    const auto finalDuration = sequence->DurationSeconds(events);
    const double launchAt = sequence->LaunchAtSeconds();
    if (!finalDuration || !std::isfinite(*finalDuration) || *finalDuration <= 0.0 ||
        *finalDuration > kMaximumRenderDurationSeconds || !std::isfinite(launchAt) ||
        !events.visualStudioCodeWindowVisibleAt ||
        !std::isfinite(*events.visualStudioCodeWindowVisibleAt) ||
        *events.visualStudioCodeWindowVisibleAt < launchAt || frameTimes.size() < 2 ||
        !std::isfinite(frameTimes.front()) || !std::isfinite(frameTimes.back()) ||
        frameTimes.front() < 0.0 || frameTimes.front() > config.timing.desktopHold ||
        frameTimes.back() <= frameTimes.front() ||
        frameTimes.back() > kMaximumRenderDurationSeconds + kSourceCoverageToleranceSeconds ||
        *events.visualStudioCodeWindowVisibleAt > frameTimes.back() ||
        *finalDuration > frameTimes.back() + kSourceCoverageToleranceSeconds)
        return MakeError(ErrorCode::Validation, "the capture recipe timing is invalid or exceeds its captured source coverage");
    if (config.timing.postCloseHold > 0.0 &&
        (!events.visualStudioCodeWindowClosedAt || !sequence->CloseAtSeconds(events) ||
         !std::isfinite(*events.visualStudioCodeWindowClosedAt) ||
         *events.visualStudioCodeWindowClosedAt < *sequence->CloseAtSeconds(events) ||
         *events.visualStudioCodeWindowClosedAt > frameTimes.back()))
        return MakeError(ErrorCode::Validation, "the capture recipe has no valid observed application closure");
    const auto outputWidth = static_cast<std::uint32_t>(config.outputPixels.width);
    const auto outputHeight = static_cast<std::uint32_t>(config.outputPixels.height);
    auto videosFolder = KnownFolder(FOLDERID_Videos);
    if (!videosFolder) return videosFolder.error();
    const auto videoPath = BuildOutputPath(*videosFolder, UniqueToken());
    if (!videoPath) return videoPath.error();
    auto compositor = GpuFrameCompositor::Create();
    if (!compositor) return compositor.error();
    Mp4WriterOptions writerOptions;
    writerOptions.outputFile = videoPath->native();
    writerOptions.width = outputWidth;
    writerOptions.height = outputHeight;
    writerOptions.framesPerSecond = kFramesPerSecond;
    writerOptions.averageBitrate = outputWidth > 1920 ? 60'000'000 : 24'000'000;
    writerOptions.includeAudio = true;
    writerOptions.audioSampleRate = 48'000;
    writerOptions.audioChannels = 2;
    writerOptions.audioBitrate = 192'000;
    auto writer = Mp4Writer::Create(std::move(writerOptions));
    if (!writer) return writer.error();
    std::vector<std::uint8_t> rendered(static_cast<std::size_t>(outputWidth) * outputHeight * 4U);
    auto gifPath = *videoPath;
    gifPath.replace_extension(L".gif");
    GifWriterOptions gifOptions;
    gifOptions.outputFile = gifPath.native();
    gifOptions.width = std::min(960U, outputWidth);
    gifOptions.height = static_cast<std::uint32_t>(std::lround(
        static_cast<double>(outputHeight) * gifOptions.width / outputWidth));
    const auto gifWidth = gifOptions.width;
    const auto gifHeight = gifOptions.height;
    auto gif = GifWriter::Create(std::move(gifOptions));
    if (!gif) return gif.error();
    std::vector<std::uint8_t> gifPixels(static_cast<std::size_t>(gifWidth) * gifHeight * 4U);

    WriteLocalRunStatus("rendering");
    CompositeSettings composite;
    composite.framesPerSecond = kFramesPerSecond;
    composite.shutterAngleDegrees = 180.0;
    composite.sampleCount = aether::showcase::TemporalSampleSet::MaximumSamples;
    composite.motionBlurEnabled = true;
    const auto outputCount = static_cast<std::size_t>(std::ceil(*finalDuration * kFramesPerSecond));
    const double outputDuration = static_cast<double>(outputCount) / kFramesPerSecond;
    const double trackStart = config.timing.desktopHold + config.timing.cursorApproach + config.timing.dockDive;
    auto soundtrack = CreateShowcaseSoundtrack(outputDuration, sequence->LaunchAtSeconds(),
                                              trackStart, trackStart + config.timing.horizontalTrack);
    if (!soundtrack) return soundtrack.error();
    std::size_t audioOffset = 0;
    const std::size_t audioFrames = soundtrack->size() / 2U;
    std::size_t slowFrames = 0;
    std::uint64_t renderTimeMicroseconds = 0;
    std::vector<std::uint64_t> renderTimes;
    renderTimes.reserve(outputCount);
    for (std::size_t index = 0; index < outputCount; ++index) {
        const bool reportProgress = index < 3U || index % 30U == 0;
        // Sink Writer applies backpressure across streams. Feed matching audio before each video
        // frame; queuing the entire picture first can deadlock once its audio queue is starved.
        const auto audioEnd = std::min(audioFrames, (index + 1U) * (48'000U / kFramesPerSecond));
        if (audioEnd > audioOffset) {
            if (reportProgress) WriteRenderProgress("audio", index, outputCount);
            const auto pcm = std::span<const std::int16_t>(soundtrack->data() + audioOffset * 2U,
                                                         (audioEnd - audioOffset) * 2U);
            if (auto appended = writer->AppendAudio(pcm); !appended) return appended.error();
            audioOffset = audioEnd;
        }
        const double time = static_cast<double>(index) / kFramesPerSecond;
        if (reportProgress) WriteRenderProgress("composition", index, outputCount);
        auto frame = compositor->Render(*sequence, archive, time, events, rendered, outputWidth, outputHeight,
                                        static_cast<std::size_t>(outputWidth) * 4U, composite);
        if (!frame) return frame.error();
        renderTimeMicroseconds += frame->elapsedMicroseconds;
        renderTimes.push_back(frame->elapsedMicroseconds);
        if (frame->elapsedMicroseconds > 1'000'000ULL / kFramesPerSecond) ++slowFrames;
        if (reportProgress) WriteRenderProgress("video-encode", index, outputCount);
        if (auto appended = writer->AppendFrame({outputWidth, outputHeight,
                    static_cast<std::size_t>(outputWidth) * 4U, rendered}); !appended) return appended.error();
        if (index % 3U == 0) {
            if (reportProgress) WriteRenderProgress("gif-encode", index, outputCount);
            auto scaled = aether::showcase::ResizeRgbaFrame({outputWidth, outputHeight,
                static_cast<std::size_t>(outputWidth) * 4U, rendered}, gifPixels, gifWidth, gifHeight,
                static_cast<std::size_t>(gifWidth) * 4U);
            if (!scaled) return scaled.error();
            if (auto appended = gif->AppendFrame({gifWidth, gifHeight,
                static_cast<std::size_t>(gifWidth) * 4U, gifPixels}); !appended) return appended.error();
        }
        // This is an offline render, not a real-time desktop loop. Yield to the user's desktop.
        ::Sleep(1);
    }
    WriteRenderProgress("finalize-video", outputCount, outputCount);
    if (auto finalized = writer->Finalize(); !finalized) return finalized.error();
    WriteRenderProgress("finalize-gif", outputCount, outputCount);
    if (auto finalized = gif->Finalize(); !finalized) return finalized.error();

    const double elapsedCapture = frameTimes.back() - frameTimes.front();
    double meanCaptureFps = elapsedCapture > 0.0 ? static_cast<double>(frameTimes.size() - 1) / elapsedCapture : 0.0;
    std::size_t slowSourceFrames = 0;
    std::vector<std::uint64_t> sourceIntervals;
    sourceIntervals.reserve(frameTimes.size() > 0 ? frameTimes.size() - 1U : 0U);
    for (std::size_t index = 1; index < frameTimes.size(); ++index)
    {
        const double interval = frameTimes[index] - frameTimes[index - 1];
        sourceIntervals.push_back(static_cast<std::uint64_t>(std::llround(interval * 1'000'000.0)));
        if (interval > 1.5 / kFramesPerSecond) ++slowSourceFrames;
    }
    const double slowCapturePercent = frameTimes.size() > 1
        ? 100.0 * static_cast<double>(slowSourceFrames) / static_cast<double>(frameTimes.size() - 1) : 100.0;
    const auto archiveSize = static_cast<std::uint64_t>(archive.Width()) * archive.Height() * 4ULL * frameTimes.size();
    if (archiveSize == 0 || renderTimeMicroseconds == 0)
        return MakeError(ErrorCode::Validation, "showcase cadence measurements are unavailable");

    CaptureReport report;
    report.output = *videoPath;
    report.durationSeconds = *finalDuration;
    report.meanCaptureFramesPerSecond = meanCaptureFps;
    report.slowCapturePercent = slowCapturePercent;
    report.sourceFrames = frameTimes.size();
    report.outputFrames = outputCount;
    report.slowOutputPercent = outputCount == 0 ? 100.0 :
        100.0 * static_cast<double>(slowFrames) / static_cast<double>(outputCount);
    report.p95CaptureIntervalMilliseconds = PercentileMilliseconds(std::move(sourceIntervals), 0.95);
    report.meanCompositeMilliseconds = outputCount == 0 ? 0.0 :
        static_cast<double>(renderTimeMicroseconds) / static_cast<double>(outputCount) / 1000.0;
    report.p95CompositeMilliseconds = PercentileMilliseconds(std::move(renderTimes), 0.95);
    report.hardwareAccelerated = compositor->HardwareAccelerated();
    report.restorationSucceeded = restorationSucceeded;
    report.qualityReceiptWritten = WriteLocalQualityReceipt(report, outputWidth, outputHeight);
    return report;
}

Result<CaptureReport> RenderSavedRecipe(const std::filesystem::path& path, const RunnerOptions& options) {
    const auto& nativePath = path.native();
    const auto localDrivePath = [](std::wstring_view value) noexcept {
        if (value.size() < 3 || !((value[0] >= L'A' && value[0] <= L'Z') ||
                                (value[0] >= L'a' && value[0] <= L'z')) ||
            value[1] != L':' || value[2] != L'\\' || value.find(L'\0') != std::wstring_view::npos) return false;
        const wchar_t root[] = {value[0], L':', L'\\', L'\0'};
        const auto type = ::GetDriveTypeW(root);
        return type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_RAMDISK;
    };
    if (!path.is_absolute() || !localDrivePath(nativePath) || path.extension() != L".aetherrecipe")
        return MakeError(ErrorCode::InvalidArgument, "offline rendering needs an absolute local capture recipe");
    struct RecipeFile final {
        HANDLE value = INVALID_HANDLE_VALUE;
        ~RecipeFile() { if (value != INVALID_HANDLE_VALUE) (void)::CloseHandle(value); }
    } recipeFile;
    recipeFile.value = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (recipeFile.value == INVALID_HANDLE_VALUE)
        return MakeError(ErrorCode::Io, "the local capture recipe could not be opened");
    std::array<wchar_t, 32'768> resolvedPath{};
    const DWORD resolvedLength = ::GetFinalPathNameByHandleW(recipeFile.value, resolvedPath.data(),
        static_cast<DWORD>(resolvedPath.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    const std::wstring_view resolved(resolvedPath.data(),
        resolvedLength < resolvedPath.size() ? resolvedLength : 0U);
    if (resolvedLength == 0 || resolvedLength >= resolvedPath.size() ||
        !resolved.starts_with(L"\\\\?\\") || !localDrivePath(resolved.substr(4)))
        return MakeError(ErrorCode::InvalidArgument, "offline rendering refused a nonlocal capture recipe");
    LARGE_INTEGER size{};
    constexpr std::size_t kMaximumRecipeBytes = 4096;
    if (!::GetFileSizeEx(recipeFile.value, &size) || size.QuadPart <= 0 ||
        size.QuadPart > static_cast<LONGLONG>(kMaximumRecipeBytes))
        return MakeError(ErrorCode::Validation, "the local capture recipe size is invalid");
    std::array<char, kMaximumRecipeBytes> contents{};
    DWORD bytesRead = 0;
    if (!::ReadFile(recipeFile.value, contents.data(), static_cast<DWORD>(size.QuadPart), &bytesRead, nullptr) ||
        bytesRead != static_cast<DWORD>(size.QuadPart))
        return MakeError(ErrorCode::Io, "the local capture recipe could not be read completely");
    std::istringstream file(std::string(contents.data(), bytesRead));
    std::string version;
    SequenceConfig config;
    double visibleAt = 0;
    double closedAt = -1;
    bool restored = false;
    auto& t = config.timing;
    file >> version;
    if (version != "AETHER_RECIPE_1" && version != "AETHER_RECIPE_2")
        return MakeError(ErrorCode::Validation, "the local capture recipe version is unsupported");
    file >> config.sourcePixels.width >> config.sourcePixels.height >> config.outputPixels.width >> config.outputPixels.height
         >> config.dockBoundsPixels.left >> config.dockBoundsPixels.top >> config.dockBoundsPixels.right >> config.dockBoundsPixels.bottom
         >> config.visualStudioCodeIconPixels.x >> config.visualStudioCodeIconPixels.y >> config.cursorStartPixels.x >> config.cursorStartPixels.y
         >> visibleAt;
    if (version == "AETHER_RECIPE_2") file >> closedAt;
    file >> restored >> t.desktopHold >> t.cursorApproach >> t.dockDive >> t.horizontalTrack >> t.focusPause
         >> t.cameraReveal >> t.finalHold;
    if (version == "AETHER_RECIPE_2") file >> t.postCloseHold;
    file >> t.topMarginPixels >> t.cursorAnchorX >> t.dockEntryFraction >> t.maximumDockZoom;
    if (!file)
        return MakeError(ErrorCode::Validation, "the local capture recipe is incomplete or unsupported");
    file >> std::ws;
    if (!file.eof())
        return MakeError(ErrorCode::Validation, "the local capture recipe contains unsupported trailing values");
    if (options.outputWidth != 0) config.outputPixels = {static_cast<double>(options.outputWidth), static_cast<double>(options.outputHeight)};
    auto archivePath = path;
    archivePath.replace_extension(L".aetherframes");
    auto archive = FrameArchive::Open(archivePath.native());
    if (!archive) return archive.error();
    SequenceEvents events;
    events.visualStudioCodeLaunchRequested = true;
    events.visualStudioCodeWindowVisibleAt = visibleAt;
    if (closedAt >= 0.0) {
        events.visualStudioCodeWindowClosedAt = closedAt;
        events.visualStudioCodeCloseRequested = true;
    }
    return RenderArchive(*archive, config, events, restored);
}

Result<CaptureReport> RunPresentation(const RunnerOptions& options) {
    auto plan = LoadPresentationPlan(options);
    if (!plan) return plan.error();
    for (const auto& action : plan->actions)
        if (action.kind == aether::showcase::PresentationActionKind::VolumeOffset && !options.allowDeviceChanges)
            return MakeError(ErrorCode::AccessDenied, "the volume scenario requires --allow-device-changes");
    if (!NativeTaskbarIsHidden())
        return MakeError(ErrorCode::Unavailable, "enable Aether taskbar replacement before the presentation");
    const HWND dock = FindAetherWindow(L"AetherShell.Dock"), topbar = FindAetherWindow(L"AetherShell.TopBar");
    if (!dock || !topbar) return MakeError(ErrorCode::Unavailable, "Aether Dock and Top Status Bar must be running");
    auto aetherIdentity = CaptureAetherSceneIdentity(dock, topbar);
    if (!aetherIdentity) return aetherIdentity.error();
    MONITORINFO monitor{sizeof(monitor)};
    if (!::GetMonitorInfoW(::MonitorFromWindow(dock, MONITOR_DEFAULTTONEAREST), &monitor))
        return MakeError(ErrorCode::Unavailable, "the Aether monitor is unavailable");
    const RECT bounds = monitor.rcMonitor;
    const auto width = static_cast<std::uint32_t>(bounds.right - bounds.left);
    const auto height = static_cast<std::uint32_t>(bounds.bottom - bounds.top);
    const auto outputWidth = options.outputWidth ? options.outputWidth : width;
    const auto outputHeight = options.outputHeight ? options.outputHeight : height;
    if (width < 1280 || height < 720 || width > 16'384 || height > 16'384)
        return MakeError(ErrorCode::Unsupported, "presentation source dimensions are unsupported");
    auto explorerName = LoadSceneExplorerName(options,*plan);
    if (!explorerName) return explorerName.error();
    auto actions = aether::showcase::NativePresentationActions::Create(dock, topbar,std::move(*explorerName));
    if (!actions) return actions.error();
    auto local = KnownFolder(FOLDERID_LocalAppData);
    if (!local) return local.error();
    const auto directory = *local / L"AetherShell" / L"Showcase";
    std::filesystem::create_directories(directory);
    const std::uint32_t fps = options.presentationFramesPerSecond;
    const auto frames = static_cast<std::size_t>(std::ceil(plan->durationSeconds * fps));
    const auto token = UniqueToken();
    if (token.empty()) return MakeError(ErrorCode::Unavailable, "presentation output identifier unavailable");
    const auto archivePath = directory / (L"presentation-" + token + L".aetherframes");
    if (auto space = EnsureSpace(directory, static_cast<std::uint64_t>(width) * height * 4ULL * (frames + 2ULL) +
            256ULL * 1024ULL * 1024ULL); !space)
        return space.error();
    auto archive = FrameArchive::Create(archivePath.native(), width, height, fps);
    if (!archive) return archive.error();
    auto buffer = CaptureBuffer::Create(width, height);
    if (!buffer) return buffer.error();
    auto capture = DxgiDesktopCaptureSession::Create(bounds);
    if (!capture) return capture.error();
    auto backend = Win32CleanSessionBackend::Create(options.keepProcessIds);
    if (!backend) return backend.error();
    auto sessionId = Win32CleanSessionBackend::CurrentSessionId();
    if (!sessionId) return sessionId.error();
    ExplorerWindowRestorer explorer;
    if (auto snapshot = explorer.Snapshot(); !snapshot) return snapshot.error();
    RestoreCursor cursor;
    CaptureControllerWindows controllers;
    if (auto hidden = controllers.Hide(backend->ProtectedControllerProcesses()); !hidden) return hidden.error();
    DesktopIconVisibility icons;
    icons.Hide();
    CleanSession clean(*backend, *sessionId, kGracefulCloseMilliseconds);
    WriteLocalRunStatus("closing-applications");
    if (auto started = clean.Begin(); !started) return started.error();
    if (auto closed = explorer.Close(); !closed) return closed.error();
    // Failure cleanup is deferred so the later-declared recorder always joins first on unwind.
    aether::showcase::PresentationSession presentation(*actions, false);
    if (auto started = presentation.Begin(*plan, options.allowDeviceChanges); !started) return started.error();
    (void)MoveRealPointer({bounds.left + width * 0.3, bounds.top + height * 0.55});
    ::Sleep(650);
    std::wstring blockingClass;
    if (!CleanSceneVisible(*aetherIdentity, dock, topbar, bounds, blockingClass))
        return MakeError(ErrorCode::AccessDenied, "an unexpected visible surface prevents presentation capture");
    WriteLocalRunStatus("capturing-presentation");
    // Dedicated acquisition must keep advancing while a UIA provider or action sleeps. DXGI and
    // CaptureBuffer have exactly one caller from this point until Finish joins the MTA worker.
    auto recorder = aether::showcase::ConcurrentRecorder::Start(plan->durationSeconds, fps,
        [&](double timestamp) -> Result<void> {
        if ((::GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0)
            return MakeError(ErrorCode::Cancelled, "presentation cancelled; restoration was attempted");
        std::wstring acquisitionBlockingClass;
        if (!NativeTaskbarIsHidden() || !CleanSceneVisible(*aetherIdentity, dock, topbar, bounds, acquisitionBlockingClass, false, true))
            return MakeError(ErrorCode::AccessDenied, "an unexpected visible surface interrupted presentation capture");
        auto frame = capture->Capture(true);
        if (!frame) return frame.error();
        if (buffer->FrameCount() == 0) {
            bool hasBackdrop = false;
            for (std::uint32_t y = height / 5; y < height * 4U / 5U && !hasBackdrop; y += 24)
                for (std::uint32_t x = width / 5; x < width * 4U / 5U; x += 24) {
                    const auto offset = static_cast<std::size_t>(y) * frame->rowStrideBytes + x * 4U;
                    if (frame->pixels[offset] > 8 || frame->pixels[offset + 1U] > 8 || frame->pixels[offset + 2U] > 8) {
                        hasBackdrop = true; break;
                    }
                }
            if (!hasBackdrop) return MakeError(ErrorCode::Unavailable, "the scenic desktop backdrop is black; no presentation was finalized");
        }
        if (!CleanSceneVisible(*aetherIdentity, dock, topbar, bounds, acquisitionBlockingClass, false, true))
            return MakeError(ErrorCode::AccessDenied, "presentation pixels were rejected after a surface change");
        if (auto stored = buffer->Append(timestamp, *frame); !stored) return stored.error();
        return aether::Ok();
    });
    if (!recorder) return recorder.error();
    std::vector<double> actionStarted(plan->actions.size(),-1),actionCompleted(plan->actions.size(),-1);
    double previousValidation = -1;
    for (;;) {
        auto finished = recorder->Poll();
        if (!finished) return finished.error();
        if (*finished) break;
        const double elapsed = recorder->ElapsedSeconds();
        if (auto tick = presentation.Tick(elapsed,[&](std::size_t index,bool starting){
                (starting?actionStarted:actionCompleted)[index]=recorder->ElapsedSeconds();
            }); !tick) return tick.error();
        if (elapsed - previousValidation >= 0.25) {
            if (auto privacy = actions->ValidateVisibleControls(); !privacy) return privacy.error();
            previousValidation = elapsed;
        }
        ::Sleep(2);
    }
    if (auto finished = recorder->Finish(); !finished) return finished.error();
    if(auto observed=aether::showcase::ValidatePresentationObservations(*plan,actionStarted,actionCompleted,
        presentation.AppliedActionCount());!observed)return observed.error();
    // Restore precise device volume and close only scenario-owned panels before reopening old apps.
    auto restoredActions = presentation.Restore();
    auto restoredApps = clean.Restore();
    auto restoredExplorer = explorer.Restore();
    controllers.Restore(); icons.Restore(); cursor.Restore();
    if (!restoredActions) return restoredActions.error();
    const bool restored = restoredApps.ok() && restoredExplorer.ok();
    WriteLocalRunStatus("saving-capture");
    if (auto flushed = buffer->FlushTo(*archive); !flushed) return flushed.error();
    if (auto finished = archive->Finalize(); !finished) return finished.error();
    auto videos = KnownFolder(FOLDERID_Videos);
    if (!videos) return videos.error();
    auto path = BuildOutputPath(*videos, token);
    if (!path) return path.error();
    Mp4WriterOptions encoding;
    encoding.outputFile = path->native(); encoding.width = outputWidth; encoding.height = outputHeight;
    encoding.framesPerSecond = fps; encoding.averageBitrate = outputWidth > 1920 ? 40'000'000 : 16'000'000;
    auto writer = Mp4Writer::Create(std::move(encoding));
    if (!writer) return writer.error();
    std::vector<std::uint8_t> resized(static_cast<std::size_t>(outputWidth) * outputHeight * 4U);
    const auto resize = [&](const aether::showcase::RgbaFrameView& source) -> Result<aether::showcase::RgbaFrameView> {
        if (source.width == outputWidth && source.height == outputHeight) return source;
        auto result = aether::showcase::ResizeRgbaFrame(source, resized, outputWidth, outputHeight,
                                                       static_cast<std::size_t>(outputWidth) * 4U);
        if (!result) return result.error();
        return aether::showcase::RgbaFrameView{outputWidth, outputHeight, static_cast<std::size_t>(outputWidth) * 4U, resized};
    };
    auto stillSource = archive->ReadFrameAt(plan->stillAtSeconds);
    if (!stillSource) return stillSource.error();
    auto still = resize(*stillSource);
    if (!still) return still.error();
    auto pngPath = *path; pngPath.replace_extension(L".png");
    if (auto written = aether::showcase::WritePngFrame(pngPath.native(), *still); !written) return written.error();
    auto desktopSource = archive->ReadFrameAt(0.2);
    if (!desktopSource) return desktopSource.error();
    auto desktopOutput = resize(*desktopSource);
    if (!desktopOutput) return desktopOutput.error();
    const auto desktopPath = path->parent_path() / (path->stem().wstring() + L"-desktop.png");
    if (auto written = aether::showcase::WritePngFrame(desktopPath.native(), *desktopOutput); !written) return written.error();
    WriteLocalRunStatus("rendering-presentation");
    for (std::size_t index = 0; index < frames; ++index) {
        auto source = archive->ReadFrameAt(static_cast<double>(index) / fps);
        if (!source) return source.error();
        auto output = resize(*source);
        if (!output) return output.error();
        if (auto written = writer->AppendFrame(*output); !written) return written.error();
    }
    if (auto finished = writer->Finalize(); !finished) return finished.error();
    // Gallery stills are selected from genuine settled source frames after restoration. Numeric
    // suffixes avoid exporting a local setting, control name or device identity into filenames.
    std::size_t shotIndex = 0;
    for (std::size_t actionIndex=0;actionIndex<plan->actions.size();++actionIndex) {
        const auto& action=plan->actions[actionIndex];
        if (!aether::showcase::IsPresentationShotAction(action.kind)) continue;
        const double at = actionCompleted[actionIndex] + 0.8;
        auto source = archive->ReadFrameAt(at);
        if (!source) return source.error();
        auto output = resize(*source);
        if (!output) return output.error();
        const auto shot = path->parent_path() / (path->stem().wstring() + L"-shot-" + std::to_wstring(shotIndex++) + L".png");
        if (auto written = aether::showcase::WritePngFrame(shot.native(), *output); !written) return written.error();
    }
    CaptureReport report;
    report.output = *path; report.durationSeconds = plan->durationSeconds;
    report.sourceFrames = archive->FrameCount(); report.outputFrames = frames; report.restorationSucceeded = restored;
    const auto& times = archive->CaptureTimes();
    if (times.size() > 1) report.meanCaptureFramesPerSecond = static_cast<double>(times.size() - 1) / (times.back() - times.front());
    std::vector<std::uint64_t> intervals;
    std::size_t over25 = 0;
    double maximumIntervalMilliseconds = 0;
    for (std::size_t index = 1; index < times.size(); ++index) {
        const double milliseconds = (times[index] - times[index - 1]) * 1000;
        intervals.push_back(static_cast<std::uint64_t>(milliseconds * 1000));
        if (milliseconds > 25) ++over25;
        maximumIntervalMilliseconds = std::max(maximumIntervalMilliseconds, milliseconds);
    }
    report.p95CaptureIntervalMilliseconds = PercentileMilliseconds(std::move(intervals), 0.95);
    // Separate receipt: these are direct full-desktop presentations, not D33 camera/audio films.
    std::ofstream receipt(path->wstring() + L".presentation-qc.txt", std::ios::binary);
    receipt << "presentation_version=1\noutput_fps=" << fps << "\naudio=none\nsource_frames=" << report.sourceFrames
            << "\nsource_mean_fps=" << report.meanCaptureFramesPerSecond << "\noutput_frames=" << frames
            << "\nsource_p95_interval_ms=" << report.p95CaptureIntervalMilliseconds
            << "\nsource_maximum_interval_ms=" << maximumIntervalMilliseconds
            << "\nsource_intervals_over_25ms=" << over25
            << "\nbuffer_committed_bytes=" << buffer->MemoryBytes() << "\nbuffer_stored_bytes=" << buffer->StoredBytes()
            << "\ndevice_and_app_restoration=" << restored << "\nprivacy_review=required\n";
    for(std::size_t index=0;index<plan->actions.size();++index)
        receipt << "action=" << index << ' ' << static_cast<int>(plan->actions[index].kind) << ' '
                << actionStarted[index] << ' ' << actionCompleted[index] << '\n';
    return report;
}

Result<CaptureReport> RunShowcase(const RunnerOptions& options) {
    WriteLocalRunStatus("preparing");
    ScopedCom apartment;
    if (!apartment.Usable()) return MakeError(ErrorCode::Unavailable, "showcase could not initialize Windows UI automation");
    if (!options.renderRecipe.empty()) return RenderSavedRecipe(options.renderRecipe, options);
    if (!options.scenario.empty() || !options.actionPlan.empty()) return RunPresentation(options);

    if (!NativeTaskbarIsHidden())
        return MakeError(ErrorCode::Unavailable, "the Windows taskbar is still visible; enable Aether taskbar replacement first");

    const HWND dock = FindAetherWindow(L"AetherShell.Dock");
    const HWND topbar = FindAetherWindow(L"AetherShell.TopBar");
    if (!dock || !topbar) return MakeError(ErrorCode::Unavailable, "Aether Dock and Top Status Bar must already be running");
    auto aetherIdentity = CaptureAetherSceneIdentity(dock, topbar);
    if (!aetherIdentity) return aetherIdentity.error();
    if (auto codePin = FindDockCodeIcon(dock); !codePin) return codePin.error();

    MONITORINFO monitor{sizeof(monitor)};
    const HMONITOR monitorHandle = ::MonitorFromWindow(dock, MONITOR_DEFAULTTONEAREST);
    if (!monitorHandle || !::GetMonitorInfoW(monitorHandle, &monitor))
        return MakeError(ErrorCode::Unavailable, "the Aether display geometry is unavailable");
    const RECT bounds = monitor.rcMonitor;
    const auto width = static_cast<std::uint32_t>(bounds.right - bounds.left);
    const auto height = static_cast<std::uint32_t>(bounds.bottom - bounds.top);
    const auto outputWidth = options.outputWidth == 0 ? width : options.outputWidth;
    const auto outputHeight = options.outputHeight == 0 ? height : options.outputHeight;
    if (width < 1280 || height < 720 || width > 16'384 || height > 16'384)
        return MakeError(ErrorCode::Unsupported, "the active Aether display does not meet the showcase source resolution");

    const auto relative = [&](RECT value) {
        // The Dock HWND intentionally extends below its visible panel so the launch/lift motion has
        // transparent trailing room. Intersect the HWND with the captured monitor; those off-screen
        // pixels are not part of the real desktop image and must not invalidate the camera source.
        const LONG left = std::max(value.left, bounds.left);
        const LONG top = std::max(value.top, bounds.top);
        const LONG right = std::min(value.right, bounds.right);
        const LONG bottom = std::min(value.bottom, bounds.bottom);
        return Rect{static_cast<double>(left - bounds.left), static_cast<double>(top - bounds.top),
                    static_cast<double>(right - bounds.left), static_cast<double>(bottom - bounds.top)};
    };
    const std::wstring token = UniqueToken();
    if (token.empty()) return MakeError(ErrorCode::Unavailable, "showcase could not allocate an output identifier");
    auto localFolder = KnownFolder(FOLDERID_LocalAppData);
    if (!localFolder) return localFolder.error();
    const auto captureDirectory = *localFolder / L"AetherShell" / L"Showcase";
    try { std::filesystem::create_directories(captureDirectory); }
    catch (...) { return MakeError(ErrorCode::Io, "showcase could not prepare local temporary capture storage"); }
    const auto archivePath = captureDirectory / (L"capture-" + token + L".aetherframes");

    const auto maximumCaptureBytes = static_cast<std::uint64_t>(width) * height * 4ULL * 1800ULL;
    if (auto space = EnsureSpace(captureDirectory, maximumCaptureBytes); !space) return space.error();
    auto capture = DxgiDesktopCaptureSession::Create(bounds);
    if (!capture) return capture.error();
    auto archive = FrameArchive::Create(archivePath.native(), width, height, kFramesPerSecond);
    if (!archive) return archive.error();
    auto captureBuffer = CaptureBuffer::Create(width, height);
    if (!captureBuffer) return captureBuffer.error();

    auto backend = Win32CleanSessionBackend::Create(options.keepProcessIds);
    if (!backend) return backend.error();
    auto sessionId = Win32CleanSessionBackend::CurrentSessionId();
    if (!sessionId) return sessionId.error();
    ExplorerWindowRestorer explorer;
    if (auto snapshot = explorer.Snapshot(); !snapshot) return snapshot.error();
    RestoreCursor restoreCursor;
    CaptureControllerWindows controllers;
    if (auto hidden = controllers.Hide(backend->ProtectedControllerProcesses()); !hidden) return hidden.error();
    DesktopIconVisibility desktopIcons;
    desktopIcons.Hide();
    CleanSession cleanSession(*backend, *sessionId, kGracefulCloseMilliseconds);
    WriteLocalRunStatus("closing-applications");
    if (auto started = cleanSession.Begin(); !started) return started.error();
    CodeWindowCleanup codeCleanup;
    if (auto closedExplorer = explorer.Close(); !closedExplorer) {
        (void)codeCleanup.Close();
        (void)cleanSession.Restore();
        return closedExplorer.error();
    }

    auto remaining = backend->EnumerateVisibleApplications(*sessionId);
    if (!remaining) {
        (void)codeCleanup.Close();
        (void)explorer.Restore();
        (void)cleanSession.Restore();
        return remaining.error();
    }
    if (!CleanSession::SelectApplicationsToClose(*remaining, *sessionId).empty()) {
        (void)codeCleanup.Close();
        (void)explorer.Restore();
        (void)cleanSession.Restore();
        return MakeError(ErrorCode::Unavailable, "the desktop is not clear; no showcase footage was captured");
    }
    if (!NativeTaskbarIsHidden()) {
        (void)codeCleanup.Close();
        (void)explorer.Restore();
        (void)cleanSession.Restore();
        return MakeError(ErrorCode::Unavailable, "Windows restored its taskbar during cleanup; no showcase footage was captured");
    }

    // Closing running apps changes the Dock's item set and centers. Wait for its native layout to
    // settle and query the actual pin again; the preflight coordinate must never drive this click.
    (void)::SetCursorPos(bounds.left + static_cast<int>(width / 4U), bounds.top + static_cast<int>(height / 2U));
    ::Sleep(650);
    std::wstring blockingClass;
    if (!CleanSceneVisible(*aetherIdentity, dock, topbar, bounds, blockingClass)) {
        std::string diagnostic;
        for (const wchar_t character : blockingClass)
            if (character >= 32 && character < 127) diagnostic.push_back(static_cast<char>(character));
        return MakeError(ErrorCode::Unavailable, "a visible surface remains (class " + diagnostic +
            "); no footage was captured");
    }
    auto visibleControls = FindDockVisibleControls(dock);
    if (!visibleControls) return visibleControls.error();
    auto codeScreen = FindDockCodeIcon(dock);
    if (!codeScreen) return codeScreen.error();
    const auto dockBounds = relative(*visibleControls);
    const double iconCenterX = (static_cast<double>(codeScreen->left) + codeScreen->right) / 2.0 - bounds.left;
    const double iconCenterY = (static_cast<double>(codeScreen->top) + codeScreen->bottom) / 2.0 - bounds.top;
    if (iconCenterX < 0 || iconCenterY < 0 || iconCenterX >= width || iconCenterY >= height ||
        dockBounds.right <= dockBounds.left || dockBounds.bottom <= dockBounds.top)
        return MakeError(ErrorCode::Validation, "the clean Dock controls fall outside the active display");
    SequenceConfig config;
    config.sourcePixels = {static_cast<double>(width), static_cast<double>(height)};
    config.outputPixels = {static_cast<double>(outputWidth), static_cast<double>(outputHeight)};
    config.dockBoundsPixels = dockBounds;
    config.visualStudioCodeIconPixels = {iconCenterX, iconCenterY};
    config.cursorStartPixels = {static_cast<double>(width) * 0.42, static_cast<double>(height) * 0.62};
    config.timing.topMarginPixels = 48.0;
    config.timing.dockEntryFraction = 0.02;
    config.timing.focusPause = 1.0;
    // The owner's three-second loaded-app interval includes the camera reveal, then a stable
    // reading hold. Closure occurs exactly three seconds after observed workbench readiness.
    config.timing.finalHold = 3.0 - config.timing.cameraReveal;
    config.timing.postCloseHold = 3.0;
    auto sequence = CinematicSequence::Create(config);
    if (!sequence) return sequence.error();
    auto readiness = CodeReadinessObserver::Start();
    if (!readiness) return readiness.error();

    WriteLocalRunStatus("capturing");
    const auto runStart = std::chrono::steady_clock::now();
    const auto frameInterval = std::chrono::duration<double>(1.0 / kFramesPerSecond);
    std::vector<double> frameTimes;
    frameTimes.reserve(600);
    SequenceEvents events;
    bool clickSent = false;
    bool closeSent = false;
    double closeRequestedAt = 0;
    std::optional<double> codeWindowVisibleAt;
    std::optional<double> codeWindowClosedAt;
    BackdropProbe backdrop;
    unsigned emptyBackdropFrames = 0;
    std::size_t frameIndex = 0;
    std::int64_t previousPresentCounter = 0;
    std::vector<std::int64_t> desktopPresentCounters;
    desktopPresentCounters.reserve(1800);
    const auto launchDeadline = runStart + std::chrono::seconds(
        static_cast<std::int64_t>(std::ceil(sequence->LaunchAtSeconds())) + kMaximumLaunchWaitSeconds);

    for (;;) {
        if ((::GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0)
            return MakeError(ErrorCode::Cancelled, "showcase cancelled; application restoration was attempted");
        const auto scheduled = runStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameInterval * frameIndex);
        WaitUntil(scheduled);
        const auto beforeCapture = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(beforeCapture - runStart).count();
        events.visualStudioCodeLaunchRequested = clickSent;
        if (readiness->Failed())
            return MakeError(ErrorCode::Unavailable, "the application workbench readiness observer failed");
        if (!codeWindowVisibleAt && clickSent && readiness->IsReady()) codeWindowVisibleAt = elapsed;
        events.visualStudioCodeWindowVisibleAt = codeWindowVisibleAt;
        events.visualStudioCodeWindowClosedAt = codeWindowClosedAt;
        events.visualStudioCodeCloseRequested = closeSent;
        const auto state = sequence->Evaluate(elapsed, events);
        const Point cursor{state.cursorSourcePixels.x + bounds.left, state.cursorSourcePixels.y + bounds.top};
        if (!MoveRealPointer(cursor)) {
            (void)codeCleanup.Close();
            (void)explorer.Restore();
            (void)cleanSession.Restore();
            return MakeError(ErrorCode::Unavailable, "showcase could not move the real pointer along its planned path");
        }
        if (!clickSent && elapsed >= sequence->LaunchAtSeconds()) {
            codeCleanup.Arm();
            // Hover magnification can move the center even after the stable layout query.
            auto actualIcon = FindDockCodeIcon(dock);
            POINT actualPointer{};
            if (!actualIcon || !::GetCursorPos(&actualPointer) ||
                !::PtInRect(&*actualIcon, actualPointer) ||
                !ClickAt(Point{static_cast<double>(actualPointer.x), static_cast<double>(actualPointer.y)})) {
                (void)codeCleanup.Close();
                (void)explorer.Restore();
                (void)cleanSession.Restore();
                return MakeError(ErrorCode::Unavailable, "showcase could not activate the real Dock pin");
            }
            clickSent = true;
        }
        if (state.closeVisualStudioCode && !closeSent) {
            const HWND showcaseWindow = readiness->ReadyWindow();
            if (!showcaseWindow || !::PostMessageW(showcaseWindow, WM_CLOSE, 0, 0))
                return MakeError(ErrorCode::Unavailable, "the showcase application could not receive its close request");
            closeSent = true;
            closeRequestedAt = elapsed;
        }
        if (closeSent && !codeWindowClosedAt && elapsed - closeRequestedAt > 6.0)
            return MakeError(ErrorCode::Timeout, "the showcase application did not finish closing");
        if (!CleanSceneVisible(*aetherIdentity, dock, topbar, bounds, blockingClass, clickSent && !codeWindowClosedAt)) {
            if (WaitForTaskbarOwner(blockingClass)) continue;
            std::string surface;
            for (const auto character : blockingClass)
                if (character >= 32 && character < 127) surface.push_back(static_cast<char>(character));
            return MakeError(ErrorCode::Unavailable, "a foreign visible surface (class " + surface +
                ") interrupted capture before acquisition; no showcase was finalized");
        }
        auto frame = capture->Capture(true);
        if (!frame) {
            (void)codeCleanup.Close();
            (void)explorer.Restore();
            (void)cleanSession.Restore();
            return frame.error();
        }
        if (frameIndex == 0) {
            // Reject the missing-wallpaper failure observed during the first real take. This is a
            // photographed scenic-desktop scenario, so an entirely black central field is unusable.
            bool hasBackdrop = false;
            for (std::uint32_t y = height / 4U; y < height * 3U / 4U && !hasBackdrop; y += 16U) {
                for (std::uint32_t x = width / 4U; x < width * 3U / 4U; x += 16U) {
                    const auto offset = static_cast<std::size_t>(y) * frame->rowStrideBytes + x * 4U;
                    if (frame->pixels[offset] > 8U || frame->pixels[offset + 1U] > 8U ||
                        frame->pixels[offset + 2U] > 8U) { hasBackdrop = true; break; }
                }
            }
            if (!hasBackdrop)
                return MakeError(ErrorCode::Unavailable, "the scenic desktop backdrop is black; restore its image before capture");
        }
        if (!CleanSceneVisible(*aetherIdentity, dock, topbar, bounds, blockingClass, clickSent && !codeWindowClosedAt)) {
            if (WaitForTaskbarOwner(blockingClass)) continue;
            std::string surface;
            for (const auto character : blockingClass)
                if (character >= 32 && character < 127) surface.push_back(static_cast<char>(character));
            return MakeError(ErrorCode::Unavailable, "a foreign visible surface (class " + surface +
                ") interrupted capture after acquisition; no showcase was finalized");
        }
        const double capturedAt = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count();
        if (frameIndex == 0) backdrop.Remember(*frame, config.cursorStartPixels);
        if (closeSent && !codeWindowClosedAt) {
            // IsWindowVisible can become false before DWM's closing animation has disappeared.
            // Start the requested empty ending only after the photographed background returns.
            if (!HasVisibleCodeWindow() && backdrop.Matches(*frame)) ++emptyBackdropFrames;
            else emptyBackdropFrames = 0;
            if (emptyBackdropFrames >= 2) {
                codeWindowClosedAt = capturedAt;
                events.visualStudioCodeWindowClosedAt = capturedAt;
            }
        }
        if (auto appended = captureBuffer->Append(capturedAt, *frame); !appended) {
            (void)codeCleanup.Close();
            (void)explorer.Restore();
            (void)cleanSession.Restore();
            return appended.error();
        }
        const auto presentCounter = capture->LastPresentCounter();
        if (capture->UpdatedLastCapture() && presentCounter != previousPresentCounter) {
            desktopPresentCounters.push_back(presentCounter);
            previousPresentCounter = presentCounter;
        }
        frameTimes.push_back(capturedAt);
        if (!codeWindowVisibleAt && std::chrono::steady_clock::now() >= launchDeadline) {
            (void)codeCleanup.Close();
            (void)explorer.Restore();
            (void)cleanSession.Restore();
            return MakeError(ErrorCode::Timeout, "the real application window did not appear after the Dock launch");
        }
        if (codeWindowVisibleAt) {
            const auto duration = sequence->DurationSeconds(events);
            if (duration && capturedAt >= *duration) break;
        }
        ++frameIndex;
    }

    WriteLocalRunStatus("restoring-applications");
    restoreCursor.Restore();
    const auto closeShowcaseCode = codeCleanup.Close();
    const auto restoreExplorer = explorer.Restore();
    const auto restoreApplications = cleanSession.Restore();
    desktopIcons.Restore();
    controllers.Restore();
    const bool restorationSucceeded = closeShowcaseCode && restoreExplorer && restoreApplications;

    WriteLocalRunStatus("saving-capture");
    // Disk throughput must not slow pointer movement or native Dock animation. Reconstruct the
    // bounded lossless tile buffer only after all desktop restoration attempts have completed.
    if (auto flushed = captureBuffer->FlushTo(*archive); !flushed) return flushed.error();
    if (auto finalized = archive->Finalize(); !finalized) return finalized.error();
    if (auto saved = SaveRecipe(archivePath, config, events, restorationSucceeded); !saved) return saved.error();
    {
        auto metricsPath = archivePath;
        metricsPath.replace_extension(L".capture-qc.txt");
        std::ofstream metrics(metricsPath, std::ios::binary | std::ios::trunc);
        LARGE_INTEGER frequency{};
        (void)::QueryPerformanceFrequency(&frequency);
        metrics << "capture_backend=DXGI desktop duplication\n"
                << "sampled_frames=" << frameTimes.size() << '\n'
                << "desktop_present_count=" << desktopPresentCounters.size() << '\n'
                << "dxgi_acquired_count=" << capture->AcquiredFrameCount() << '\n'
                << "buffer_committed_bytes=" << captureBuffer->MemoryBytes() << '\n'
                << "buffer_stored_bytes=" << captureBuffer->StoredBytes() << '\n'
                << "qpc_frequency=" << frequency.QuadPart << '\n';
        for (const auto counter : desktopPresentCounters) metrics << "desktop_present_qpc=" << counter << '\n';
    }
    return RenderArchive(*archive, config, events, restorationSucceeded);

}

std::wstring ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int count = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return L"The showcase could not complete.";
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                              result.data(), count) != count) return L"The showcase could not complete.";
    return result;
}

bool ParseRunnerOptions(RunnerOptions& options) noexcept {
    int count = 0;
    wchar_t** arguments = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    if (!arguments) return false;
    bool valid = true;
    for (int index = 1; index < count && valid; ++index) {
        const std::wstring_view argument(arguments[index]);
        if (argument == L"--preflight") {
            options.preflightOnly = true;
        } else if (argument == L"--quiet") {
            options.quiet = true;
        } else if (argument == L"--allow-device-changes") {
            options.allowDeviceChanges = true;
        } else if (argument == L"--fps" && index + 1 < count) {
            const std::wstring_view value(arguments[++index]);
            if (value == L"60") options.presentationFramesPerSecond = 60;
            else if (value == L"30") options.presentationFramesPerSecond = 30;
            else valid = false;
        } else if (argument == L"--scenario" && index + 1 < count) {
            const std::wstring_view value(arguments[++index]);
            if (!options.scenario.empty()) { valid = false; continue; }
            for (const wchar_t ch : value) {
                if (ch < 32 || ch > 126) { valid = false; break; }
                options.scenario.push_back(static_cast<char>(ch));
            }
            if (!aether::showcase::MakePresentationPlan(options.scenario)) valid = false;
        } else if (argument == L"--action-plan" && index + 1 < count) {
            if (!options.actionPlan.empty()) valid = false;
            options.actionPlan = arguments[++index];
        } else if (argument == L"--scene-settings" && index + 1 < count) {
            if(!options.sceneSettings.empty())valid=false;
            options.sceneSettings=arguments[++index];
        } else if (argument == L"--render-only" && index + 1 < count) {
            options.renderRecipe = arguments[++index];
        } else if (argument == L"--output-size" && index + 1 < count) {
            const std::wstring_view size(arguments[++index]);
            if (size == L"1920x1080") { options.outputWidth = 1920; options.outputHeight = 1080; }
            else if (size == L"3840x2160") { options.outputWidth = 3840; options.outputHeight = 2160; }
            else if (size == L"source") { options.outputWidth = 0; options.outputHeight = 0; }
            else valid = false;
        } else if (argument == L"--keep-pid" && index + 1 < count) {
            const wchar_t* value = arguments[++index];
            wchar_t* end = nullptr;
            const unsigned long parsed = std::wcstoul(value, &end, 10);
            if (value == end || end == nullptr || *end != L'\0' || parsed == 0 ||
                parsed > std::numeric_limits<std::uint32_t>::max()) {
                valid = false;
            } else {
                const auto processId = static_cast<std::uint32_t>(parsed);
                if (std::find(options.keepProcessIds.begin(), options.keepProcessIds.end(), processId) ==
                    options.keepProcessIds.end()) options.keepProcessIds.push_back(processId);
            }
        } else {
            valid = false;
        }
    }
    (void)::LocalFree(arguments);
    return valid && (options.scenario.empty() || options.actionPlan.empty()) &&
        (options.renderRecipe.empty() || (options.scenario.empty() && options.actionPlan.empty()));
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        // Keep UI Automation, cursor input and capture geometry in physical screen pixels.
        (void)::SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        RunnerOptions options;
        if (!ParseRunnerOptions(options)) {
            if (!options.quiet)
                (void)::MessageBoxW(nullptr, L"The showcase command options are invalid.",
                                    L"Aether Shell Showcase", MB_OK | MB_ICONWARNING);
            return 2;
        }
        if (options.preflightOnly) {
            auto preflight = ValidatePreflight(options);
            if (!preflight) {
                WriteLocalRunStatus("preflight-failed", &preflight.error());
                if (!options.quiet) {
                    const std::wstring message = ToWide(preflight.error().message);
                    (void)::MessageBoxW(nullptr, message.c_str(), L"Aether Shell Showcase Preflight", MB_OK | MB_ICONWARNING);
                }
                return 1;
            }
            WriteLocalRunStatus("preflight-passed");
            if (!options.quiet) {
                const std::wstring message = L"Preflight passed for " + std::to_wstring(*preflight) +
                    L" visible user applications. No windows were closed or captured.";
                (void)::MessageBoxW(nullptr, message.c_str(), L"Aether Shell Showcase Preflight", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }
        auto result = RunShowcase(options);
        if (!result) {
            WriteLocalRunStatus("failed", &result.error());
            if (!options.quiet) {
                const std::wstring message = ToWide(result.error().message);
                (void)::MessageBoxW(nullptr, message.c_str(), L"Aether Shell Showcase", MB_OK | MB_ICONWARNING);
            }
            return 1;
        }
        WriteLocalRunStatus(result->restorationSucceeded ? "completed" : "completed-with-relaunch-warning");
        if (options.quiet) return 0;
        const auto& report = *result;
        const auto output = report.output.wstring();
        if (!options.scenario.empty() || !options.actionPlan.empty()) {
            const std::wstring message = L"The genuine desktop presentation MP4 and PNG stills are ready:\n\n" + output +
                L"\n\nThis is a silent " + std::to_wstring(options.presentationFramesPerSecond) +
                L"-FPS full-desktop recording. Review every frame and still for private content before sharing.";
            (void)::MessageBoxW(nullptr, message.c_str(), L"Aether Shell Presentation", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        const std::wstring message = L"The captured showcase video is ready:\n\n" + output +
            L"\n\n" + std::to_wstring(report.sourceFrames) + L" source frames at " +
            std::to_wstring(report.meanCaptureFramesPerSecond) + L" average captures per second; " +
            std::to_wstring(report.outputFrames) + L" H.264 frames at 60 FPS with original AAC stereo audio at 48 kHz; " +
            std::to_wstring(report.slowOutputPercent) + L"% of composition frames took longer than one 60-FPS interval.\n" +
            (report.hardwareAccelerated ? L"Hardware compositing was used." : L"WARP software compositing was used.") +
            L"\nThe export still needs full visual, privacy, audio and delivery review.";
        (void)::MessageBoxW(nullptr, message.c_str(), L"Aether Shell Showcase", MB_OK | MB_ICONINFORMATION);
        return 0;
    } catch (...) {
        (void)::MessageBoxW(nullptr, L"The showcase stopped after an unexpected error. Restoration was attempted.",
                           L"Aether Shell Showcase", MB_OK | MB_ICONERROR);
        return 1;
    }
}
