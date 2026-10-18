// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "ui/accessibility/AccessibleSurface.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <utility>
#include <vector>

namespace aether::ui::accessibility {
namespace {

template <class F>
HRESULT Guard(F&& function) noexcept {
    try { return function(); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }  // Model exception text can contain private application data.
}

HRESULT ActionResult(const Result<void>& result) noexcept {
    if (result) return S_OK;
    switch (result.error().code) {
        case ErrorCode::InvalidArgument: return E_INVALIDARG;
        case ErrorCode::AccessDenied: return E_ACCESSDENIED;
        case ErrorCode::NotFound:
        case ErrorCode::InvalidState: return UIA_E_ELEMENTNOTAVAILABLE;
        case ErrorCode::Unsupported: return UIA_E_NOTSUPPORTED;
        default: return E_FAIL;
    }
}

Result<void> FromHr(HRESULT hr) {
    return SUCCEEDED(hr) ? Ok() : Result<void>(MakeError(ErrorCode::Platform, "UI Automation operation failed", hr));
}

void BoolValue(VARIANT& value, bool flag) noexcept {
    value.vt = VT_BOOL;
    value.boolVal = flag ? VARIANT_TRUE : VARIANT_FALSE;
}

HRESULT StringValue(VARIANT& value, const std::wstring& text) noexcept {
    value.vt = VT_BSTR;
    value.bstrVal = ::SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
    return value.bstrVal || text.empty() ? S_OK : E_OUTOFMEMORY;
}

class Node;
struct State {
    HWND hwnd = nullptr;
    DWORD thread = 0;
    std::atomic<bool> connected{false};
    IAccessibleSurfaceModel* model = nullptr;
    std::vector<AccessibleItem> items;
    winrt::com_ptr<IRawElementProviderFragmentRoot> root;
    std::map<int, winrt::com_ptr<IRawElementProviderFragment>> nodes;
    // Last notified values are independent of UIA property reads.
    std::map<int, std::pair<std::wstring, std::wstring>> notified;

    HRESULT Check() const noexcept {
        if (!connected.load()) return UIA_E_ELEMENTNOTAVAILABLE;
        return thread == ::GetCurrentThreadId() ? S_OK : RPC_E_WRONG_THREAD;
    }
    HRESULT Refresh();
    const AccessibleItem* Find(int id) const noexcept {
        const auto found = std::find_if(items.begin(), items.end(), [id](const auto& item) { return item.id == id; });
        return found == items.end() ? nullptr : &*found;
    }
};

// Standard COM STA object: deliberately no IAgileObject/free-threaded marshaler. With
// ProviderOptions_UseComThreading, UIA marshals calls back to the creating window's STA.
class Node final : public IRawElementProviderSimple, public IRawElementProviderFragment,
                   public IRawElementProviderFragmentRoot,
                   public IInvokeProvider, public IToggleProvider {
public:
    Node(std::shared_ptr<State> state, int id) : state_(std::move(state)), id_(id) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) noexcept override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple))
            *object = static_cast<IRawElementProviderSimple*>(this);
        else if (iid == __uuidof(IRawElementProviderFragment))
            *object = static_cast<IRawElementProviderFragment*>(this);
        else if (iid == __uuidof(IRawElementProviderFragmentRoot) && id_ == 0)
            *object = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (iid == __uuidof(IInvokeProvider) && id_ != 0)
            *object = static_cast<IInvokeProvider*>(this);
        else if (iid == __uuidof(IToggleProvider) && id_ != 0)
            *object = static_cast<IToggleProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const auto remaining = --refs_;
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) noexcept override {
        if (!options) return E_POINTER;
        *options = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return state_->Check();
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** result) noexcept override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return Guard([&]() -> HRESULT {
            const auto hr = Refresh(); if (FAILED(hr)) return hr;
            const auto* item = state_->Find(id_);
            if (!item) return S_OK;
            if (pattern == UIA_InvokePatternId && item->supportsInvoke)
                return QueryInterface(__uuidof(IInvokeProvider), reinterpret_cast<void**>(result));
            if (pattern == UIA_TogglePatternId && item->supportsToggle)
                return QueryInterface(__uuidof(IToggleProvider), reinterpret_cast<void**>(result));
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* result) noexcept override {
        if (!result) return E_POINTER;
        ::VariantInit(result);
        return Guard([&]() -> HRESULT {
            const auto hr = Refresh(); if (FAILED(hr)) return hr;
            const auto* item = state_->Find(id_);
            switch (property) {
                case UIA_NamePropertyId:
                    return StringValue(*result, item ? item->name : state_->model->RootName());
                case UIA_HelpTextPropertyId:
                    return StringValue(*result, item ? item->helpText : std::wstring{});
                case UIA_ItemStatusPropertyId:
                    return StringValue(*result, item ? item->itemStatus : std::wstring{});
                case UIA_AutomationIdPropertyId:
                    return StringValue(*result, item ? L"item-" + std::to_wstring(id_) : L"surface");
                case UIA_ControlTypePropertyId:
                    result->vt = VT_I4;
                    result->lVal = item ? item->controlType : state_->model->RootControlType();
                    break;
                case UIA_IsEnabledPropertyId: BoolValue(*result, !item || item->isEnabled); break;
                case UIA_IsKeyboardFocusablePropertyId: BoolValue(*result, item && item->isFocusable); break;
                case UIA_HasKeyboardFocusPropertyId: BoolValue(*result, item && item->hasKeyboardFocus); break;
                case UIA_IsOffscreenPropertyId:
                    BoolValue(*result, item ? item->isOffscreen : !::IsWindowVisible(state_->hwnd)); break;
                case UIA_IsControlElementPropertyId:
                case UIA_IsContentElementPropertyId: BoolValue(*result, true); break;
                case UIA_IsInvokePatternAvailablePropertyId: BoolValue(*result, item && item->supportsInvoke); break;
                case UIA_IsTogglePatternAvailablePropertyId: BoolValue(*result, item && item->supportsToggle); break;
                default: break;  // VT_EMPTY asks UIA to use its default value.
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) noexcept override {
        if (!result) return E_POINTER;
        *result = nullptr;
        const auto hr = state_->Check(); if (FAILED(hr)) return hr;
        return id_ == 0 ? ::UiaHostProviderFromHwnd(state_->hwnd, result) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** result) noexcept override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return Guard([&]() -> HRESULT {
            const auto hr = Refresh(); if (FAILED(hr)) return hr;
            int index = -1;
            if (id_ != 0) {
                const auto found = std::find_if(state_->items.begin(), state_->items.end(),
                    [&](const auto& item) { return item.id == id_; });
                index = static_cast<int>(found - state_->items.begin());
            }
            const auto next = NavigateIndex(index, static_cast<int>(state_->items.size()), direction);
            if (!next) return S_OK;
            if (*next < 0) return state_->root->QueryInterface(__uuidof(IRawElementProviderFragment),
                                                             reinterpret_cast<void**>(result));
            return Fragment(state_->items[static_cast<std::size_t>(*next)].id, result);
        });
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** result) noexcept override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return Guard([&]() -> HRESULT {
            const auto hr = Refresh(); if (FAILED(hr)) return hr;
            // The HWND host supplies the root's identity; UIA prefixes item IDs with that root.
            if (id_ == 0) return S_OK;
            SAFEARRAY* ids = ::SafeArrayCreateVector(VT_I4, 0, 2);
            if (!ids) return E_OUTOFMEMORY;
            LONG first = 0, second = 1;
            auto runtimeId = RuntimeIdForItem(id_);
            if (!runtimeId) { ::SafeArrayDestroy(ids); return E_INVALIDARG; }
            HRESULT put = ::SafeArrayPutElement(ids, &first, &runtimeId.value()[0]);
            if (SUCCEEDED(put)) put = ::SafeArrayPutElement(ids, &second, &runtimeId.value()[1]);
            if (FAILED(put)) { ::SafeArrayDestroy(ids); return put; }
            *result = ids;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* result) noexcept override {
        if (!result) return E_POINTER;
        *result = {};
        return Guard([&]() -> HRESULT {
            const auto hr = Refresh(); if (FAILED(hr)) return hr;
            RECT bounds{};
            if (const auto* item = state_->Find(id_)) bounds = item->boundsPx;
            else if (!::GetWindowRect(state_->hwnd, &bounds)) return UIA_E_ELEMENTNOTAVAILABLE;
            result->left = bounds.left;
            result->top = bounds.top;
            result->width = static_cast<double>(bounds.right) - bounds.left;
            result->height = static_cast<double>(bounds.bottom) - bounds.top;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** result) noexcept override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return Guard([&] { return Refresh(); });
    }
    HRESULT STDMETHODCALLTYPE SetFocus() noexcept override {
        return Guard([&]() -> HRESULT {
            const auto hr = Refresh(); if (FAILED(hr)) return hr;
            const auto* item = state_->Find(id_);
            if (!item || !item->isFocusable) return UIA_E_NOTSUPPORTED;
            if (!item->isEnabled) return UIA_E_ELEMENTNOTENABLED;
            return ActionResult(state_->model->SetFocus(id_));
        });
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** result) noexcept override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return Guard([&]() -> HRESULT {
            const auto hr = Refresh(); if (FAILED(hr)) return hr;
            state_->root.copy_to(result);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** result) noexcept override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return Guard([&]() -> HRESULT {
            const auto hr = Refresh(); if (FAILED(hr)) return hr;
            for (const auto& item : state_->items) {
                const auto& rect = item.boundsPx;
                if (!item.isOffscreen && x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom)
                    return Fragment(item.id, result);
            }
            RECT bounds{};
            if (::GetWindowRect(state_->hwnd, &bounds) && x >= bounds.left && x < bounds.right &&
                y >= bounds.top && y < bounds.bottom)
                return state_->root->QueryInterface(__uuidof(IRawElementProviderFragment),
                                                    reinterpret_cast<void**>(result));
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** result) noexcept override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return Guard([&]() -> HRESULT {
            const auto hr = Refresh(); if (FAILED(hr)) return hr;
            for (const auto& item : state_->items) if (item.hasKeyboardFocus) return Fragment(item.id, result);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Invoke() noexcept override { return Action(false); }
    HRESULT STDMETHODCALLTYPE Toggle() noexcept override { return Action(true); }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* result) noexcept override {
        if (!result) return E_POINTER;
        *result = ToggleState_Off;
        return Guard([&]() -> HRESULT {
            const auto hr = Refresh(); if (FAILED(hr)) return hr;
            const auto* item = state_->Find(id_);
            if (!item || !item->supportsToggle) return UIA_E_NOTSUPPORTED;
            *result = item->toggleState;
            return S_OK;
        });
    }

private:
    HRESULT Refresh() {
        const auto hr = state_->Refresh();
        if (FAILED(hr)) return hr;
        return id_ == 0 || state_->Find(id_) ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT Fragment(int id, IRawElementProviderFragment** result) {
        const auto found = state_->nodes.find(id);
        if (found == state_->nodes.end()) return UIA_E_ELEMENTNOTAVAILABLE;
        found->second.copy_to(result);
        return S_OK;
    }
    HRESULT Action(bool toggle) noexcept {
        return Guard([&]() -> HRESULT {
            const auto hr = Refresh(); if (FAILED(hr)) return hr;
            const auto* item = state_->Find(id_);
            if (!item || (toggle ? !item->supportsToggle : !item->supportsInvoke)) return UIA_E_NOTSUPPORTED;
            if (!item->isEnabled) return UIA_E_ELEMENTNOTENABLED;
            return ActionResult(toggle ? state_->model->Toggle(id_) : state_->model->Invoke(id_));
        });
    }
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<State> state_;
    int id_;
};

HRESULT State::Refresh() {
    const auto hr = Check(); if (FAILED(hr)) return hr;
    const auto count = model->ItemCount();
    if (count > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return E_INVALIDARG;
    std::vector<AccessibleItem> current;
    current.reserve(count);
    std::set<int> ids;
    for (std::size_t index = 0; index < count; ++index) {
        auto item = model->Item(index);
        if (item.id <= 0 || !ids.insert(item.id).second || item.boundsPx.right < item.boundsPx.left ||
            item.boundsPx.bottom < item.boundsPx.top || item.toggleState < ToggleState_Off ||
            item.toggleState > ToggleState_Indeterminate) return E_INVALIDARG;
        current.push_back(std::move(item));
    }
    items = std::move(current);
    return S_OK;
}

}  // namespace

struct AccessibleSurface::Impl {
    std::shared_ptr<State> state;
    HRESULT EnsureNodes() {
        if (!state) return UIA_E_ELEMENTNOTAVAILABLE;
        const auto hr = state->Refresh(); if (FAILED(hr)) return hr;
        for (const auto& item : state->items) {
            if (!state->nodes.contains(item.id)) {
                auto* node = new Node(state, item.id);
                winrt::com_ptr<IRawElementProviderFragment> fragment;
                fragment.attach(static_cast<IRawElementProviderFragment*>(node));
                state->nodes.emplace(item.id, std::move(fragment));
            }
        }
        return S_OK;
    }
    HRESULT Simple(int id, winrt::com_ptr<IRawElementProviderSimple>& result) {
        const auto hr = EnsureNodes(); if (FAILED(hr)) return hr;
        if (id == 0) return state->root->QueryInterface(__uuidof(IRawElementProviderSimple), result.put_void());
        if (!state->Find(id)) return UIA_E_ELEMENTNOTAVAILABLE;
        return state->nodes.at(id)->QueryInterface(__uuidof(IRawElementProviderSimple), result.put_void());
    }
};

std::optional<int> NavigateIndex(int index, int count, NavigateDirection direction) noexcept {
    if (count < 0 || index < -1 || index >= count) return std::nullopt;
    switch (direction) {
        case NavigateDirection_Parent: if (index >= 0) return -1; break;
        case NavigateDirection_FirstChild: if (index == -1 && count > 0) return 0; break;
        case NavigateDirection_LastChild: if (index == -1 && count > 0) return count - 1; break;
        case NavigateDirection_NextSibling: if (index >= 0 && index < count - 1) return index + 1; break;
        case NavigateDirection_PreviousSibling: if (index > 0) return index - 1; break;
        default: break;
    }
    return std::nullopt;
}

AccessibleSurface::AccessibleSurface() : impl_(std::make_unique<Impl>()) {}
AccessibleSurface::~AccessibleSurface() { Disconnect(); }

Result<std::array<int, 2>> RuntimeIdForItem(int id) {
    if (id <= 0) return MakeError(ErrorCode::InvalidArgument, "accessible item identity must be positive");
    return std::array<int, 2>{UiaAppendRuntimeId, id};
}

Result<void> AccessibleSurface::Connect(HWND hwnd, IAccessibleSurfaceModel& model) {
    if (impl_->state) return MakeError(ErrorCode::InvalidState, "accessible surface already connected");
    if (!::IsWindow(hwnd) || ::GetWindowThreadProcessId(hwnd, nullptr) != ::GetCurrentThreadId())
        return MakeError(ErrorCode::InvalidArgument, "accessible surface requires its own UI-thread window");
    APTTYPE apartment{}; APTTYPEQUALIFIER qualifier{};
    const auto com = ::CoGetApartmentType(&apartment, &qualifier);
    if (FAILED(com) || (apartment != APTTYPE_STA && apartment != APTTYPE_MAINSTA))
        return MakeError(ErrorCode::InvalidState, "accessible surface requires an initialized COM STA");
    const auto hr = Guard([&]() -> HRESULT {
        impl_->state = std::make_shared<State>();
        auto& state = impl_->state;
        state->hwnd = hwnd;
        state->thread = ::GetCurrentThreadId();
        state->model = &model;
        state->connected.store(true);
        auto* node = new Node(state, 0);
        state->root.attach(static_cast<IRawElementProviderFragmentRoot*>(node));
        const auto ensured = impl_->EnsureNodes(); if (FAILED(ensured)) return ensured;
        for (const auto& item : state->items) state->notified[item.id] = {item.name, item.itemStatus};
        return S_OK;
    });
    if (FAILED(hr)) Disconnect();
    return FromHr(hr);
}

bool AccessibleSurface::HandleGetObject(HWND hwnd, WPARAM wParam, LPARAM lParam, LRESULT& result) noexcept {
    if (static_cast<LONG>(lParam) != UiaRootObjectId || !impl_->state || hwnd != impl_->state->hwnd) return false;
    winrt::com_ptr<IRawElementProviderSimple> root;
    const auto hr = Guard([&] { return impl_->Simple(0, root); });
    result = SUCCEEDED(hr) ? ::UiaReturnRawElementProvider(hwnd, wParam, lParam, root.get()) : 0;
    return true;
}

void AccessibleSurface::Disconnect() noexcept {
    const auto state = impl_->state;
    if (!state) return;
    // Owner-thread use is part of the contract. Even on misuse, disconnect model access first.
    state->connected.store(false);
    state->model = nullptr;
    for (const auto& [id, node] : state->nodes) {
        (void)id;
        winrt::com_ptr<IRawElementProviderSimple> simple;
        if (SUCCEEDED(node->QueryInterface(__uuidof(IRawElementProviderSimple), simple.put_void())))
            (void)::UiaDisconnectProvider(simple.get());
    }
    winrt::com_ptr<IRawElementProviderSimple> root;
    if (state->root && SUCCEEDED(state->root->QueryInterface(__uuidof(IRawElementProviderSimple), root.put_void())))
        (void)::UiaDisconnectProvider(root.get());
    (void)::UiaReturnRawElementProvider(state->hwnd, 0, 0, nullptr);
    state->nodes.clear();
    state->root = nullptr;
    impl_->state.reset();
}

Result<winrt::com_ptr<IRawElementProviderSimple>> AccessibleSurface::RootProvider() const {
    winrt::com_ptr<IRawElementProviderSimple> result;
    const auto hr = Guard([&] { return impl_->Simple(0, result); });
    if (FAILED(hr)) return MakeError(ErrorCode::Platform, "accessible root unavailable", hr);
    return result;
}

Result<void> AccessibleSurface::NotifyStructureChanged() {
    return FromHr(Guard([&]() -> HRESULT {
        winrt::com_ptr<IRawElementProviderSimple> root;
        const auto hr = impl_->Simple(0, root); if (FAILED(hr)) return hr;
        auto& state = impl_->state;
        for (auto it = state->nodes.begin(); it != state->nodes.end();) {
            if (state->Find(it->first)) { ++it; continue; }
            auto simple = it->second.as<IRawElementProviderSimple>();
            (void)::UiaDisconnectProvider(simple.get());
            state->notified.erase(it->first);
            it = state->nodes.erase(it);
        }
        for (const auto& item : state->items)
            state->notified.try_emplace(item.id, item.name, item.itemStatus);
        return ::UiaRaiseStructureChangedEvent(root.get(), StructureChangeType_ChildrenInvalidated, nullptr, 0);
    }));
}

Result<void> AccessibleSurface::NotifyFocusChanged(int id) {
    return FromHr(Guard([&]() -> HRESULT {
        winrt::com_ptr<IRawElementProviderSimple> node;
        const auto hr = impl_->Simple(id, node); if (FAILED(hr)) return hr;
        return ::UiaRaiseAutomationEvent(node.get(), UIA_AutomationFocusChangedEventId);
    }));
}

Result<void> AccessibleSurface::NotifyInvoked(int id) {
    return FromHr(Guard([&]() -> HRESULT {
        winrt::com_ptr<IRawElementProviderSimple> node;
        const auto hr = impl_->Simple(id, node); if (FAILED(hr)) return hr;
        return ::UiaRaiseAutomationEvent(node.get(), UIA_Invoke_InvokedEventId);
    }));
}

Result<void> AccessibleSurface::NotifyPropertyChanged(int id) {
    return FromHr(Guard([&]() -> HRESULT {
        winrt::com_ptr<IRawElementProviderSimple> node;
        const auto hr = impl_->Simple(id, node); if (FAILED(hr)) return hr;
        auto& state = impl_->state;
        const auto* item = state->Find(id);
        if (!item) return E_INVALIDARG;
        auto& old = state->notified[id];
        const std::pair<std::wstring, std::wstring> updated{item->name, item->itemStatus};
        const auto notify = [&](PROPERTYID property, const std::wstring& before, const std::wstring& after) -> HRESULT {
            if (before == after) return S_OK;
            VARIANT a{}, b{};
            auto made = StringValue(a, before);
            if (SUCCEEDED(made)) made = StringValue(b, after);
            if (SUCCEEDED(made)) made = ::UiaRaiseAutomationPropertyChangedEvent(node.get(), property, a, b);
            ::VariantClear(&a); ::VariantClear(&b);
            return made;
        };
        auto changed = notify(UIA_NamePropertyId, old.first, updated.first);
        if (SUCCEEDED(changed)) changed = notify(UIA_ItemStatusPropertyId, old.second, updated.second);
        if (SUCCEEDED(changed)) old = updated;
        return changed;
    }));
}

}  // namespace aether::ui::accessibility
