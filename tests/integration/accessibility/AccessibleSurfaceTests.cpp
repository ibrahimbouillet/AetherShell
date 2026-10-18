// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "ui/accessibility/AccessibleSurface.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::ui::accessibility;
using namespace std::chrono_literals;

namespace {

constexpr UINT kReorder = WM_APP + 51;
constexpr UINT kRemove = WM_APP + 52;
constexpr UINT kDestroy = WM_APP + 53;

struct Apartment {
    explicit Apartment(DWORD flags) : hr(::CoInitializeEx(nullptr, flags)) {}
    ~Apartment() { if (SUCCEEDED(hr)) ::CoUninitialize(); }
    HRESULT hr;
};

struct Model final : IAccessibleSurfaceModel {
    DWORD owner = ::GetCurrentThreadId();
    mutable std::atomic<bool> wrongThread{false};
    std::vector<AccessibleItem> items;
    AccessibleSurface* surface = nullptr;
    int invoked = 0;
    int toggled = 0;
    void ThreadCheck() const { if (::GetCurrentThreadId() != owner) wrongThread.store(true); }
    std::wstring RootName() const override { ThreadCheck(); return L"Aether accessibility test"; }
    CONTROLTYPEID RootControlType() const override { ThreadCheck(); return UIA_ToolBarControlTypeId; }
    std::size_t ItemCount() const override { ThreadCheck(); return items.size(); }
    AccessibleItem Item(std::size_t index) const override { ThreadCheck(); return items.at(index); }
    Result<void> Invoke(int id) override { ThreadCheck(); invoked = id; return surface->NotifyInvoked(id); }
    Result<void> Toggle(int id) override {
        ThreadCheck();
        for (auto& item : items) if (item.id == id) {
            item.toggleState = item.toggleState == ToggleState_On ? ToggleState_Off : ToggleState_On;
            toggled = id;
            return Ok();
        }
        return MakeError(ErrorCode::NotFound, "test item unavailable");
    }
    Result<void> SetFocus(int id) override {
        ThreadCheck();
        for (auto& item : items) item.hasKeyboardFocus = item.id == id;
        return surface->NotifyFocusChanged(id);
    }
};

struct Window {
    Model model;
    AccessibleSurface surface;
    HWND hwnd = nullptr;
    ~Window() { if (hwnd && ::IsWindow(hwnd)) ::DestroyWindow(hwnd); }
    static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Window*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            if (message == WM_GETOBJECT) {
                LRESULT result = 0;
                if (self->surface.HandleGetObject(hwnd, wParam, lParam, result)) return result;
            }
            if (message == kReorder) {
                std::swap(self->model.items[0], self->model.items[1]);
                return self->surface.NotifyStructureChanged().ok() ? 1 : 0;
            }
            if (message == kRemove) {
                std::erase_if(self->model.items, [](const auto& item) { return item.id == 22; });
                return self->surface.NotifyStructureChanged().ok() ? 1 : 0;
            }
            if (message == kDestroy) { ::DestroyWindow(hwnd); return 1; }
            if (message == WM_DESTROY) { self->surface.Disconnect(); return 0; }
        }
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }
    bool Create() {
        const auto instance = ::GetModuleHandleW(nullptr);
        WNDCLASSW cls{};
        cls.lpfnWndProc = Proc; cls.hInstance = instance;
        cls.lpszClassName = L"Aether.Accessibility.Test";
        if (!::RegisterClassW(&cls) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        hwnd = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName,
            L"Aether accessibility test", WS_POPUP, 40, 40, 340, 100, nullptr, nullptr, instance, this);
        if (!hwnd) return false;
        model.surface = &surface;
        for (int index = 0; index < 3; ++index) {
            AccessibleItem item;
            item.id = (index + 1) * 11;
            item.name = L"Test item " + std::to_wstring(index + 1);
            item.boundsPx = RECT{50 + index * 100, 50, 140 + index * 100, 120};
            item.supportsInvoke = index == 1;
            item.supportsToggle = index == 2;
            item.controlType = index == 2 ? UIA_CheckBoxControlTypeId : UIA_ButtonControlTypeId;
            model.items.push_back(std::move(item));
        }
        if (!surface.Connect(hwnd, model)) return false;
        ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        return true;
    }
};

void Need(bool condition) { if (!condition) throw std::runtime_error("own-window UIA assertion failed"); }
void NeedHr(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("own-window UIA call failed"); }

std::wstring Name(IUIAutomationElement& element) {
    BSTR name = nullptr;
    NeedHr(element.get_CurrentName(&name));
    std::wstring result(name ? name : L"");
    ::SysFreeString(name);
    return result;
}

std::vector<int> RuntimeId(IUIAutomationElement& element) {
    SAFEARRAY* array = nullptr;
    NeedHr(element.GetRuntimeId(&array));
    Need(array != nullptr);
    LONG first = 0, last = -1;
    NeedHr(::SafeArrayGetLBound(array, 1, &first));
    NeedHr(::SafeArrayGetUBound(array, 1, &last));
    std::vector<int> values;
    for (LONG index = first; index <= last; ++index) {
        int value = 0;
        NeedHr(::SafeArrayGetElement(array, &index, &value));
        values.push_back(value);
    }
    ::SafeArrayDestroy(array);
    return values;
}

class FocusHandler : public winrt::implements<FocusHandler, IUIAutomationFocusChangedEventHandler> {
public:
    FocusHandler(winrt::com_ptr<IUIAutomation> automation, winrt::com_ptr<IUIAutomationElement> target,
                 std::shared_ptr<std::atomic<int>> count)
        : automation_(std::move(automation)), target_(std::move(target)), count_(std::move(count)) {}
    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) noexcept override {
        BOOL same = FALSE;
        // Compare identities with our own test element; do not inspect unrelated UI content.
        if (sender && SUCCEEDED(automation_->CompareElements(sender, target_.get(), &same)) && same) ++*count_;
        return S_OK;
    }
private:
    winrt::com_ptr<IUIAutomation> automation_;
    winrt::com_ptr<IUIAutomationElement> target_;
    std::shared_ptr<std::atomic<int>> count_;
};

}  // namespace

AETHER_TEST("accessibility integration: MTA client uses STA fragment actions, events and stable identities") {
    Apartment ui(COINIT_APARTMENTTHREADED);
    REQUIRE(SUCCEEDED(ui.hr));
    Window window;
    REQUIRE(window.Create());
    const auto nativeRoot = window.surface.RootProvider();
    REQUIRE(nativeRoot.ok());
    const HWND hwnd = window.hwnd;
    std::atomic<bool> finished{false};
    auto focusEvents = std::make_shared<std::atomic<int>>(0);
    std::string failure;
    std::thread client([&] {
        Apartment mta(COINIT_MULTITHREADED);
        try {
            NeedHr(mta.hr);
            winrt::com_ptr<IUIAutomation> automation;
            NeedHr(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                __uuidof(IUIAutomation), automation.put_void()));
            winrt::com_ptr<IUIAutomationElement> root;
            NeedHr(automation->ElementFromHandle(hwnd, root.put()));
            Need(Name(*root) == L"Aether accessibility test");
            CONTROLTYPEID type = 0;
            NeedHr(root->get_CurrentControlType(&type));
            Need(type == UIA_ToolBarControlTypeId);
            winrt::com_ptr<IUIAutomationCondition> all;
            NeedHr(automation->CreateTrueCondition(all.put()));
            winrt::com_ptr<IUIAutomationElementArray> children;
            NeedHr(root->FindAll(TreeScope_Children, all.get(), children.put()));
            int count = 0;
            NeedHr(children->get_Length(&count));
            Need(count == 3);
            winrt::com_ptr<IUIAutomationElement> second, third;
            for (int index = 0; index < count; ++index) {
                winrt::com_ptr<IUIAutomationElement> child;
                NeedHr(children->GetElement(index, child.put()));
                Need(Name(*child) == L"Test item " + std::to_wstring(index + 1));
                NeedHr(child->get_CurrentControlType(&type));
                Need(type == (index == 2 ? UIA_CheckBoxControlTypeId : UIA_ButtonControlTypeId));
                RECT bounds{};
                NeedHr(child->get_CurrentBoundingRectangle(&bounds));
                Need(bounds.left == 50 + index * 100 && bounds.top == 50 && bounds.bottom == 120);
                if (index == 1) second = child;
                if (index == 2) third = child;
            }
            winrt::com_ptr<IUIAutomationInvokePattern> invoke;
            NeedHr(second->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern), invoke.put_void()));
            NeedHr(invoke->Invoke());
            winrt::com_ptr<IUIAutomationTogglePattern> toggle;
            NeedHr(third->GetCurrentPatternAs(UIA_TogglePatternId, __uuidof(IUIAutomationTogglePattern), toggle.put_void()));
            NeedHr(toggle->Toggle());
            ToggleState state{};
            NeedHr(toggle->get_CurrentToggleState(&state));
            Need(state == ToggleState_On);

            auto handler = winrt::make_self<FocusHandler>(automation, second, focusEvents);
            NeedHr(automation->AddFocusChangedEventHandler(nullptr, handler.get()));
            const auto focus = second->SetFocus();
            const auto eventDeadline = std::chrono::steady_clock::now() + 3s;
            while (focusEvents->load() == 0 && std::chrono::steady_clock::now() < eventDeadline)
                std::this_thread::sleep_for(10ms);
            NeedHr(automation->RemoveFocusChangedEventHandler(handler.get()));
            NeedHr(focus);
            Need(focusEvents->load() > 0);

            const auto identity = RuntimeId(*second);
            DWORD_PTR messageResult = 0;
            Need(::SendMessageTimeoutW(hwnd, kReorder, 0, 0, SMTO_ABORTIFHUNG, 3000, &messageResult) != 0);
            Need(messageResult == 1);
            Need(RuntimeId(*second) == identity);
            Need(Name(*second) == L"Test item 2");
            Need(::SendMessageTimeoutW(hwnd, kRemove, 0, 0, SMTO_ABORTIFHUNG, 3000, &messageResult) != 0);
            BSTR removedName = nullptr;
            const auto removed = second->get_CurrentName(&removedName);
            ::SysFreeString(removedName);
            Need(removed == UIA_E_ELEMENTNOTAVAILABLE);

            Need(::SendMessageTimeoutW(hwnd, kDestroy, 0, 0, SMTO_ABORTIFHUNG, 3000, &messageResult) != 0);
            BSTR goneName = nullptr;
            const auto gone = root->get_CurrentName(&goneName);
            ::SysFreeString(goneName);
            Need(gone == UIA_E_ELEMENTNOTAVAILABLE);
        } catch (const std::exception& ex) { failure = ex.what(); }
        catch (...) { failure = "own-window UIA client exception"; }
        finished.store(true);
    });
    while (!finished.load()) {
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&message); ::DispatchMessageW(&message);
        }
        ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
    client.join();
    CHECK(failure.empty());
    if (!failure.empty()) TEST_NOTE(failure);
    CHECK_FALSE(window.model.wrongThread.load());
    CHECK_EQ(window.model.invoked, 22);
    CHECK_EQ(window.model.toggled, 33);
    CHECK(focusEvents->load() > 0);
    window.surface.Disconnect();
    VARIANT value{};
    CHECK_EQ(nativeRoot.value()->GetPropertyValue(UIA_NamePropertyId, &value),
             static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
    ::VariantClear(&value);
}

AETHER_TEST("accessibility integration: invalid models and wrong apartments fail without exposing providers") {
    Apartment ui(COINIT_APARTMENTTHREADED);
    REQUIRE(SUCCEEDED(ui.hr));
    Window window;
    REQUIRE(window.Create());
    CHECK_FALSE(window.surface.Connect(window.hwnd, window.model).ok());
    LRESULT result = 123;
    CHECK_FALSE(window.surface.HandleGetObject(window.hwnd, 0, OBJID_CLIENT, result));
    CHECK_EQ(result, LRESULT{123});
    window.surface.Disconnect();
    window.model.items[1].id = window.model.items[0].id;
    CHECK_FALSE(window.surface.Connect(window.hwnd, window.model).ok());
    CHECK_FALSE(window.surface.RootProvider().ok());
    window.model.items[1].id = 22;
    REQUIRE(window.surface.Connect(window.hwnd, window.model).ok());
    CHECK(window.surface.NotifyPropertyChanged(22).ok());
    window.model.items[1].name = L"Renamed test item";
    window.model.items[1].itemStatus = L"Running";
    CHECK(window.surface.NotifyPropertyChanged(22).ok());
    CHECK_FALSE(window.surface.NotifyFocusChanged(999).ok());

    bool rejectedMta = false;
    std::thread wrongApartment([&] {
        Apartment mta(COINIT_MULTITHREADED);
        if (FAILED(mta.hr)) return;
        const HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"Aether MTA test", WS_POPUP,
            0, 0, 1, 1, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        if (!hwnd) return;
        AccessibleSurface surface;
        Model model;
        const auto result = surface.Connect(hwnd, model);
        rejectedMta = !result && result.error().code == ErrorCode::InvalidState;
        ::DestroyWindow(hwnd);
    });
    wrongApartment.join();
    CHECK(rejectedMta);
}

AETHER_TEST("accessibility integration: pattern availability, disabled actions and removed references are safe") {
    Apartment ui(COINIT_APARTMENTTHREADED);
    REQUIRE(SUCCEEDED(ui.hr));
    Window window;
    REQUIRE(window.Create());
    const auto nativeRoot = window.surface.RootProvider();
    REQUIRE(nativeRoot.ok());
    const auto root = nativeRoot.value().as<IRawElementProviderFragment>();
    winrt::com_ptr<IRawElementProviderFragment> first;
    REQUIRE(SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, first.put())));
    REQUIRE(first != nullptr);
    auto simple = first.as<IRawElementProviderSimple>();
    winrt::com_ptr<IUnknown> pattern;
    CHECK(SUCCEEDED(simple->GetPatternProvider(UIA_InvokePatternId, pattern.put())));
    CHECK(pattern == nullptr);
    window.model.items[0].supportsInvoke = true;
    window.model.items[0].isEnabled = false;
    REQUIRE(SUCCEEDED(simple->GetPatternProvider(UIA_InvokePatternId, pattern.put())));
    REQUIRE(pattern != nullptr);
    const auto invoke = pattern.as<IInvokeProvider>();
    CHECK_EQ(invoke->Invoke(), static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED));
    CHECK_EQ(window.model.invoked, 0);
    winrt::com_ptr<IRawElementProviderFragment> parent;
    CHECK(SUCCEEDED(first->Navigate(NavigateDirection_Parent, parent.put())));
    CHECK(parent != nullptr);
    SAFEARRAY* ids = nullptr;
    REQUIRE(SUCCEEDED(first->GetRuntimeId(&ids)));
    REQUIRE(ids != nullptr);
    int prefix = 0, id = 0;
    LONG zero = 0, one = 1;
    CHECK(SUCCEEDED(::SafeArrayGetElement(ids, &zero, &prefix)));
    CHECK(SUCCEEDED(::SafeArrayGetElement(ids, &one, &id)));
    CHECK_EQ(prefix, UiaAppendRuntimeId);
    CHECK_EQ(id, 11);
    ::SafeArrayDestroy(ids);
    const auto fragmentRoot = nativeRoot.value().as<IRawElementProviderFragmentRoot>();
    winrt::com_ptr<IRawElementProviderFragment> second;
    REQUIRE(SUCCEEDED(fragmentRoot->ElementProviderFromPoint(160, 80, second.put())));
    REQUIRE(second != nullptr);
    CHECK(SUCCEEDED(second->SetFocus()));
    CHECK(window.model.items[1].hasKeyboardFocus);
    winrt::com_ptr<IRawElementProviderFragment> focused;
    CHECK(SUCCEEDED(fragmentRoot->GetFocus(focused.put())));
    CHECK(focused != nullptr);
    window.model.items.erase(window.model.items.begin());
    CHECK(window.surface.NotifyStructureChanged().ok());
    VARIANT value{};
    CHECK_EQ(simple->GetPropertyValue(UIA_NamePropertyId, &value),
             static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
    ::VariantClear(&value);
    parent = nullptr;
    CHECK_EQ(first->Navigate(NavigateDirection_Parent, parent.put()),
             static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
    CHECK(parent == nullptr);
    winrt::com_ptr<IRawElementProviderFragmentRoot> removedRoot;
    CHECK_EQ(first->get_FragmentRoot(removedRoot.put()), static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
    CHECK(removedRoot == nullptr);
}
