// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/CodeReadinessObserver.h"

#include <Ole2.h>
#include <UIAutomation.h>
#include <dwmapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <new>
#include <string_view>
#include <thread>
#include <utility>

namespace aether::showcase {
namespace {

using Microsoft::WRL::ComPtr;
constexpr DWORD kPollMilliseconds = 100;
constexpr DWORD kProviderTimeoutMilliseconds = 250;

struct ScopedProcess final {
    HANDLE value = nullptr;
    ~ScopedProcess() { if (value) (void)::CloseHandle(value); }
};

struct ScopedCom final {
    HRESULT status = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ScopedCom() { if (SUCCEEDED(status)) ::CoUninitialize(); }
};

std::uint64_t CreationTime(HANDLE process) noexcept {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!::GetProcessTimes(process, &creation, &exit, &kernel, &user)) return 0;
    ULARGE_INTEGER value{};
    value.LowPart = creation.dwLowDateTime;
    value.HighPart = creation.dwHighDateTime;
    return value.QuadPart;
}

bool IsVisibleCandidate(HWND window, ProcessIdentity& identity) noexcept {
    if (!::IsWindowVisible(window) || ::IsIconic(window) || ::GetWindow(window, GW_OWNER)) return false;
    DWORD cloaked = 0;
    if (FAILED(::DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) || cloaked)
        return false;
    DWORD processId = 0;
    (void)::GetWindowThreadProcessId(window, &processId);
    if (processId == 0) return false;
    ScopedProcess process{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId)};
    if (!process.value) return false;
    std::array<wchar_t, 32'768> image{};
    DWORD length = static_cast<DWORD>(image.size());
    if (!::QueryFullProcessImageNameW(process.value, 0, image.data(), &length)) return false;
    const std::wstring_view path(image.data(), length);
    const auto separator = path.find_last_of(L"\\/");
    const auto basename = path.substr(separator == std::wstring_view::npos ? 0U : separator + 1U);
    if (::CompareStringOrdinal(basename.data(), static_cast<int>(basename.size()), L"Code.exe", 8, TRUE) != CSTR_EQUAL)
        return false;
    identity = {processId, CreationTime(process.value)};
    return identity.creationTime != 0;
}

struct CandidateWindows final {
    std::array<HWND, 16> windows{};
    std::size_t count = 0;
};

BOOL CALLBACK CollectCandidates(HWND window, LPARAM parameter) noexcept {
    auto& result = *reinterpret_cast<CandidateWindows*>(parameter);
    if (result.count == result.windows.size()) return FALSE;
    ProcessIdentity identity;
    if (IsVisibleCandidate(window, identity)) result.windows[result.count++] = window;
    return TRUE;
}

HRESULT NameCondition(IUIAutomation2* automation, std::wstring_view name, bool substring,
                      IUIAutomationCondition** condition) noexcept {
    VARIANT value{};
    ::VariantInit(&value);
    value.vt = VT_BSTR;
    value.bstrVal = ::SysAllocStringLen(name.data(), static_cast<UINT>(name.size()));
    if (!value.bstrVal) return E_OUTOFMEMORY;
    const auto flags = static_cast<PropertyConditionFlags>(PropertyConditionFlags_IgnoreCase |
        (substring ? PropertyConditionFlags_MatchSubstring : PropertyConditionFlags_None));
    const HRESULT status = automation->CreatePropertyConditionEx(UIA_NamePropertyId, value, flags, condition);
    (void)::VariantClear(&value);
    return status;
}

bool EqualName(std::wstring_view name, std::wstring_view expected) noexcept {
    return ::CompareStringOrdinal(name.data(), static_cast<int>(name.size()), expected.data(),
                                  static_cast<int>(expected.size()), TRUE) == CSTR_EQUAL;
}

bool WorkbenchLoaded(IUIAutomation2* automation, IUIAutomationCondition* markers,
                     IUIAutomationCacheRequest* cache, HWND window) noexcept {
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(window, &root)) || !root) return false;
    ComPtr<IUIAutomationElementArray> elements;
    if (FAILED(root->FindAllBuildCache(TreeScope_Descendants, markers, cache, &elements)) || !elements) return false;
    int length = 0;
    if (FAILED(elements->get_Length(&length)) || length <= 0 || length > 128) return false;
    bool manage = false, activityBar = false, fileMenu = false, editMenu = false;
    for (int index = 0; index < length; ++index) {
        ComPtr<IUIAutomationElement> element;
        if (FAILED(elements->GetElement(index, &element)) || !element) continue;
        BOOL offscreen = TRUE, enabled = FALSE;
        CONTROLTYPEID type = 0;
        RECT bounds{};
        if (FAILED(element->get_CachedIsOffscreen(&offscreen)) || offscreen ||
            FAILED(element->get_CachedIsEnabled(&enabled)) || !enabled ||
            FAILED(element->get_CachedControlType(&type)) ||
            FAILED(element->get_CachedBoundingRectangle(&bounds)) ||
            bounds.right <= bounds.left || bounds.bottom <= bounds.top) continue;
        BSTR rawName = nullptr;
        if (FAILED(element->get_CachedName(&rawName)) || !rawName) continue;
        const std::wstring_view name(rawName, ::SysStringLen(rawName));
        if (EqualName(name, L"Manage") && (type == UIA_ButtonControlTypeId || type == UIA_MenuItemControlTypeId))
            manage = true;
        if ((EqualName(name, L"Activity Bar") || name.starts_with(L"Activity Bar (")) &&
            (type == UIA_ToolBarControlTypeId || type == UIA_GroupControlTypeId || type == UIA_TabControlTypeId))
            activityBar = true;
        if (type == UIA_MenuItemControlTypeId) {
            if (EqualName(name, L"File")) fileMenu = true;
            if (EqualName(name, L"Edit")) editMenu = true;
        }
        ::SysFreeString(rawName);
    }
    // Manage is rendered by the workbench, unlike an early blank native Electron window.
    return manage && (activityBar || (fileMenu && editMenu));
}

}  // namespace

struct CodeReadinessObserver::Impl final {
    HANDLE stopEvent = nullptr;
    std::thread worker;
    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};
    HWND window = nullptr;
    ProcessIdentity process;

    ~Impl() {
        if (stopEvent) (void)::SetEvent(stopEvent);
        if (worker.joinable()) worker.join();
        if (stopEvent) (void)::CloseHandle(stopEvent);
    }

    void Run() noexcept {
        try {
            ScopedCom apartment;
            if (FAILED(apartment.status)) { failed.store(true, std::memory_order_release); return; }
            ComPtr<IUIAutomation2> automation;
            HRESULT status = ::CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                                IID_PPV_ARGS(&automation));
            if (SUCCEEDED(status)) status = automation->put_ConnectionTimeout(kProviderTimeoutMilliseconds);
            if (SUCCEEDED(status)) status = automation->put_TransactionTimeout(kProviderTimeoutMilliseconds);
            ComPtr<IUIAutomationCacheRequest> cache;
            if (SUCCEEDED(status)) status = automation->CreateCacheRequest(&cache);
            if (SUCCEEDED(status)) status = cache->put_TreeScope(TreeScope_Element);
            for (const auto property : {UIA_NamePropertyId, UIA_ControlTypePropertyId, UIA_IsOffscreenPropertyId,
                                        UIA_IsEnabledPropertyId, UIA_BoundingRectanglePropertyId})
                if (SUCCEEDED(status)) status = cache->AddProperty(property);
            std::array<ComPtr<IUIAutomationCondition>, 4> names;
            constexpr std::array<std::wstring_view, 4> markerNames{L"Manage", L"Activity Bar", L"File", L"Edit"};
            for (std::size_t index = 0; index < names.size() && SUCCEEDED(status); ++index)
                status = NameCondition(automation.Get(), markerNames[index], index == 1, &names[index]);
            std::array<IUIAutomationCondition*, 4> conditions{};
            for (std::size_t index = 0; index < names.size(); ++index) conditions[index] = names[index].Get();
            ComPtr<IUIAutomationCondition> markers;
            if (SUCCEEDED(status)) status = automation->CreateOrConditionFromNativeArray(conditions.data(),
                static_cast<int>(conditions.size()), &markers);
            if (FAILED(status)) { failed.store(true, std::memory_order_release); return; }

            while (::WaitForSingleObject(stopEvent, 0) == WAIT_TIMEOUT) {
                CandidateWindows candidates;
                (void)::EnumWindows(&CollectCandidates, reinterpret_cast<LPARAM>(&candidates));
                for (std::size_t index = 0; index < candidates.count; ++index) {
                    if (::WaitForSingleObject(stopEvent, 0) != WAIT_TIMEOUT) return;
                    ProcessIdentity identity;
                    if (!IsVisibleCandidate(candidates.windows[index], identity) ||
                        !WorkbenchLoaded(automation.Get(), markers.Get(), cache.Get(), candidates.windows[index])) continue;
                    ProcessIdentity checked;
                    if (!IsVisibleCandidate(candidates.windows[index], checked) || checked != identity) continue;
                    window = candidates.windows[index];
                    process = identity;
                    ready.store(true, std::memory_order_release);
                    return;
                }
                if (::WaitForSingleObject(stopEvent, kPollMilliseconds) != WAIT_TIMEOUT) return;
            }
        } catch (...) {
            failed.store(true, std::memory_order_release);
        }
    }
};

CodeReadinessObserver::CodeReadinessObserver() noexcept = default;
CodeReadinessObserver::CodeReadinessObserver(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
CodeReadinessObserver::~CodeReadinessObserver() = default;
CodeReadinessObserver::CodeReadinessObserver(CodeReadinessObserver&& other) noexcept = default;
CodeReadinessObserver& CodeReadinessObserver::operator=(CodeReadinessObserver&& other) noexcept = default;

Result<CodeReadinessObserver> CodeReadinessObserver::Start() noexcept {
    try {
        auto impl = std::make_unique<Impl>();
        impl->stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!impl->stopEvent)
            return MakeError(ErrorCode::Unavailable, "showcase readiness cancellation could not initialize", ::GetLastError());
        impl->worker = std::thread([state = impl.get()] { state->Run(); });
        return CodeReadinessObserver(std::move(impl));
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Unavailable, "showcase readiness observer could not allocate its worker");
    } catch (...) {
        return MakeError(ErrorCode::Unavailable, "showcase readiness observer could not start its worker");
    }
}

bool CodeReadinessObserver::IsReady() const noexcept {
    if (!impl_ || !impl_->ready.load(std::memory_order_acquire)) return false;
    ProcessIdentity current;
    return IsVisibleCandidate(impl_->window, current) && current == impl_->process;
}

bool CodeReadinessObserver::Failed() const noexcept {
    return impl_ && impl_->failed.load(std::memory_order_acquire);
}

HWND CodeReadinessObserver::ReadyWindow() const noexcept { return IsReady() ? impl_->window : nullptr; }
ProcessIdentity CodeReadinessObserver::ReadyProcess() const noexcept { return IsReady() ? impl_->process : ProcessIdentity{}; }

}  // namespace aether::showcase
