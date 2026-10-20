// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "ui/accessibility/AccessibleSurface.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <limits>
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
    int rangeCalls = 0;
    int rangeId = 0;
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
    Result<void> SetRangeValue(int id, double value) override {
        ThreadCheck();
        ++rangeCalls; rangeId = id;
        for (auto& item : items) if (item.id == id) { item.rangeValue = value; return Ok(); }
        return MakeError(ErrorCode::NotFound, "test range item unavailable");
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

AETHER_TEST("accessibility integration: immutable provider metadata survives disconnect on owner and MTA threads") {
    Apartment ui(COINIT_APARTMENTTHREADED);
    REQUIRE(SUCCEEDED(ui.hr));
    Window window;
    REQUIRE(window.Create());
    const auto root = window.surface.RootProvider();
    REQUIRE(root.ok());
    auto* const provider = root.value().get(); // retained on the owner thread until every reader joins
    constexpr auto expected = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    ProviderOptions options{};
    CHECK_EQ(provider->get_ProviderOptions(nullptr), static_cast<HRESULT>(E_POINTER));
    CHECK_EQ(provider->get_ProviderOptions(&options), static_cast<HRESULT>(S_OK));
    CHECK(options == expected);

    struct MetadataRead {
        HRESULT apartment = E_PENDING;
        HRESULT result = E_PENDING;
        ProviderOptions options{};
        bool differentThread = false;
    };
    const auto ownerThread = ::GetCurrentThreadId();
    const auto readFromMta = [&] {
        MetadataRead read;
        std::thread reader([&] {
            Apartment mta(COINIT_MULTITHREADED);
            read.apartment = mta.hr;
            read.differentThread = ::GetCurrentThreadId() != ownerThread;
            // Deliberately call only this immutable getter directly, without COM marshaling.
            // No property, action, navigation or other model-dependent provider method runs here.
            if (SUCCEEDED(mta.hr)) read.result = provider->get_ProviderOptions(&read.options);
        });
        reader.join();
        return read;
    };
    const auto before = readFromMta();
    CHECK(SUCCEEDED(before.apartment)); CHECK(before.differentThread);
    CHECK_EQ(before.result, static_cast<HRESULT>(S_OK)); CHECK(before.options == expected);

    window.surface.Disconnect();
    options = {};
    CHECK_EQ(provider->get_ProviderOptions(nullptr), static_cast<HRESULT>(E_POINTER));
    CHECK_EQ(provider->get_ProviderOptions(&options), static_cast<HRESULT>(S_OK));
    CHECK(options == expected);
    const auto after = readFromMta();
    CHECK(SUCCEEDED(after.apartment)); CHECK(after.differentThread);
    CHECK_EQ(after.result, static_cast<HRESULT>(S_OK)); CHECK(after.options == expected);

    // Metadata availability never revives a disconnected element or relaxes its property guard.
    VARIANT value{};
    CHECK_EQ(provider->GetPropertyValue(UIA_ControlTypePropertyId, &value),
             static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
    ::VariantClear(&value);
    winrt::com_ptr<IUnknown> pattern;
    CHECK_EQ(provider->GetPatternProvider(UIA_InvokePatternId, pattern.put()),
             static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
    CHECK(pattern == nullptr);
    CHECK_FALSE(window.model.wrongThread.load());
    CHECK_EQ(window.model.invoked, 0); CHECK_EQ(window.model.toggled, 0);
}

AETHER_TEST("accessibility integration: range values expose known metadata and guard user intents") {
    Apartment ui(COINIT_APARTMENTTHREADED); REQUIRE(SUCCEEDED(ui.hr));
    Window window; REQUIRE(window.Create());
    auto& item = window.model.items[0];
    item.controlType = UIA_SliderControlTypeId; item.supportsRangeValue = true;
    item.rangeValue = 42; item.rangeSmallChange = 2; item.rangeLargeChange = 10;
    REQUIRE(window.surface.NotifyStructureChanged().ok());
    const auto root = window.surface.RootProvider(); REQUIRE(root.ok());
    auto fragment = root.value().as<IRawElementProviderFragment>();
    winrt::com_ptr<IRawElementProviderFragment> child;
    REQUIRE(SUCCEEDED(fragment->Navigate(NavigateDirection_FirstChild, child.put())));
    REQUIRE(child != nullptr);
    auto simple = child.as<IRawElementProviderSimple>();
    winrt::com_ptr<IUnknown> pattern;
    REQUIRE(SUCCEEDED(simple->GetPatternProvider(UIA_RangeValuePatternId, pattern.put())));
    REQUIRE(pattern != nullptr);
    auto range = pattern.as<IRangeValueProvider>();
    double reading = -1;
    CHECK_EQ(range->get_Value(&reading), static_cast<HRESULT>(S_OK)); CHECK_EQ(reading, 42.0);
    CHECK_EQ(range->get_Minimum(&reading), static_cast<HRESULT>(S_OK)); CHECK_EQ(reading, 0.0);
    CHECK_EQ(range->get_Maximum(&reading), static_cast<HRESULT>(S_OK)); CHECK_EQ(reading, 100.0);
    CHECK_EQ(range->get_SmallChange(&reading), static_cast<HRESULT>(S_OK)); CHECK_EQ(reading, 2.0);
    CHECK_EQ(range->get_LargeChange(&reading), static_cast<HRESULT>(S_OK)); CHECK_EQ(reading, 10.0);
    BOOL readOnly = TRUE;
    CHECK_EQ(range->get_IsReadOnly(&readOnly), static_cast<HRESULT>(S_OK)); CHECK_FALSE(readOnly);
    CHECK_EQ(range->get_Value(nullptr), static_cast<HRESULT>(E_POINTER));
    CHECK_EQ(range->get_IsReadOnly(nullptr), static_cast<HRESULT>(E_POINTER));
    VARIANT value{};
    REQUIRE(SUCCEEDED(simple->GetPropertyValue(UIA_IsRangeValuePatternAvailablePropertyId, &value)));
    CHECK_EQ(value.vt, VT_BOOL); CHECK_EQ(value.boolVal, VARIANT_TRUE); ::VariantClear(&value);
    REQUIRE(SUCCEEDED(simple->GetPropertyValue(UIA_RangeValueValuePropertyId, &value)));
    CHECK_EQ(value.vt, VT_R8); CHECK_EQ(value.dblVal, 42.0); ::VariantClear(&value);

    CHECK_EQ(range->SetValue(80), static_cast<HRESULT>(S_OK));
    CHECK_EQ(window.model.rangeCalls, 1); CHECK_EQ(window.model.rangeId, 11);
    CHECK_EQ(range->get_Value(&reading), static_cast<HRESULT>(S_OK)); CHECK_EQ(reading, 80.0);
    CHECK(window.surface.NotifyPropertyChanged(11).ok());
    CHECK(window.surface.NotifyPropertyChanged(11).ok()); // unchanged known range is safe and needs no invented value
    for (const double invalid : {-1.0, 101.0, std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
        CHECK_EQ(range->SetValue(invalid), static_cast<HRESULT>(E_INVALIDARG));
    CHECK_EQ(window.model.rangeCalls, 1);
    item.isEnabled = false;
    CHECK_EQ(range->SetValue(20), static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED));
    CHECK_EQ(range->get_Value(&reading), static_cast<HRESULT>(S_OK)); CHECK_EQ(reading, 80.0);
    item.isEnabled = true; item.rangeReadOnly = true;
    CHECK_EQ(range->SetValue(20), static_cast<HRESULT>(UIA_E_INVALIDOPERATION));
    CHECK_EQ(range->get_IsReadOnly(&readOnly), static_cast<HRESULT>(S_OK)); CHECK(readOnly);
    item.rangeReadOnly = false; item.supportsRangeValue = false; // unknown reading withdraws this pattern
    pattern = nullptr;
    CHECK_EQ(simple->GetPatternProvider(UIA_RangeValuePatternId, pattern.put()), static_cast<HRESULT>(S_OK));
    CHECK(pattern == nullptr);
    CHECK_EQ(range->get_Value(&reading), static_cast<HRESULT>(UIA_E_NOTSUPPORTED));
    CHECK_EQ(range->SetValue(20), static_cast<HRESULT>(UIA_E_NOTSUPPORTED));
    REQUIRE(SUCCEEDED(simple->GetPropertyValue(UIA_IsRangeValuePatternAvailablePropertyId, &value)));
    CHECK_EQ(value.vt, VT_BOOL); CHECK_EQ(value.boolVal, VARIANT_FALSE); ::VariantClear(&value);
    item.supportsRangeValue = true;

    HRESULT wrongRead = E_PENDING, wrongSet = E_PENDING;
    std::thread wrongThread([&] {
        Apartment mta(COINIT_MULTITHREADED);
        if (FAILED(mta.hr)) return;
        double ignored = 0;
        // Raw calls deliberately verify rejection before any model access; this is not COM marshaling.
        wrongRead = range->get_Value(&ignored); wrongSet = range->SetValue(20);
    });
    wrongThread.join();
    CHECK_EQ(wrongRead, static_cast<HRESULT>(RPC_E_WRONG_THREAD));
    CHECK_EQ(wrongSet, static_cast<HRESULT>(RPC_E_WRONG_THREAD));
    CHECK_EQ(window.model.rangeCalls, 1); CHECK_FALSE(window.model.wrongThread.load());
    window.surface.Disconnect();
    CHECK_EQ(range->get_Value(&reading), static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
    CHECK_EQ(range->SetValue(20), static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
    CHECK_EQ(window.model.rangeCalls, 1);
}

AETHER_TEST("accessibility integration: malformed range models and removed range references fail closed") {
    Apartment ui(COINIT_APARTMENTTHREADED); REQUIRE(SUCCEEDED(ui.hr));
    Window window; REQUIRE(window.Create());
    auto valid = window.model.items[0];
    valid.supportsRangeValue = true; valid.rangeValue = 50; valid.controlType = UIA_SliderControlTypeId;
    window.model.items[0] = valid;
    const auto root = window.surface.RootProvider(); REQUIRE(root.ok());
    auto fragment = root.value().as<IRawElementProviderFragment>();
    winrt::com_ptr<IRawElementProviderFragment> child;
    REQUIRE(SUCCEEDED(fragment->Navigate(NavigateDirection_FirstChild, child.put())));
    REQUIRE(child != nullptr);
    auto simple = child.as<IRawElementProviderSimple>();
    winrt::com_ptr<IUnknown> pattern;
    REQUIRE(SUCCEEDED(simple->GetPatternProvider(UIA_RangeValuePatternId, pattern.put())));
    REQUIRE(pattern != nullptr);
    auto range = pattern.as<IRangeValueProvider>();
    std::vector<AccessibleItem> malformed;
    auto invalid = valid; invalid.rangeValue = std::numeric_limits<double>::quiet_NaN(); malformed.push_back(invalid);
    invalid = valid; invalid.rangeMinimum = std::numeric_limits<double>::infinity(); malformed.push_back(invalid);
    invalid = valid; invalid.rangeMaximum = std::numeric_limits<double>::infinity(); malformed.push_back(invalid);
    invalid = valid; invalid.rangeMinimum = 101; malformed.push_back(invalid);
    invalid = valid; invalid.rangeValue = -1; malformed.push_back(invalid);
    invalid = valid; invalid.rangeValue = 101; malformed.push_back(invalid);
    invalid = valid; invalid.rangeSmallChange = -1; malformed.push_back(invalid);
    invalid = valid; invalid.rangeLargeChange = std::numeric_limits<double>::quiet_NaN(); malformed.push_back(invalid);
    for (const auto& item : malformed) {
        window.model.items[0] = item;
        double reading = 0;
        CHECK_EQ(range->get_Value(&reading), static_cast<HRESULT>(E_INVALIDARG));
        CHECK_EQ(range->SetValue(50), static_cast<HRESULT>(E_INVALIDARG));
        CHECK_EQ(window.model.rangeCalls, 0);
    }
    window.model.items[0] = valid;
    double reading = 0;
    CHECK_EQ(range->get_Value(&reading), static_cast<HRESULT>(S_OK)); CHECK_EQ(reading, 50.0);
    window.model.items.erase(window.model.items.begin());
    CHECK(window.surface.NotifyStructureChanged().ok());
    CHECK_EQ(range->get_Value(&reading), static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
    CHECK_EQ(range->SetValue(50), static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
    CHECK_EQ(window.model.rangeCalls, 0); CHECK_FALSE(window.model.wrongThread.load());
}
