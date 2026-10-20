// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Reads shell window properties back from a window of this test only. The window is never shown, so
// it never gets a taskbar button, and its properties are cleared before it is destroyed. No other
// window is read.
#include "AetherTest.h"

#include "applications/ShellItems.h"
#include "applications/WindowProperties.h"
#include "platform/windows/ComApartment.h"

#include <windows.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shellapi.h>

#include <string>

using namespace aether;
using namespace aether::apps;

namespace {

constexpr wchar_t kProbeClass[] = L"AetherShell.WindowPropertiesProbe";

class HiddenWindow {
public:
    HiddenWindow() {
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.lpfnWndProc = ::DefWindowProcW;
        windowClass.hInstance = ::GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kProbeClass;
        ::RegisterClassExW(&windowClass);
        // No WS_VISIBLE and never shown: no taskbar button, nothing on screen.
        hwnd_ = ::CreateWindowExW(0, kProbeClass, L"Aether properties probe", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                                  nullptr, nullptr, windowClass.hInstance, nullptr);
    }
    ~HiddenWindow() {
        Clear();
        if (hwnd_) ::DestroyWindow(hwnd_);
        ::UnregisterClassW(kProbeClass, ::GetModuleHandleW(nullptr));
    }
    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;

    [[nodiscard]] HWND Get() const noexcept { return hwnd_; }

    HRESULT Set(const PROPERTYKEY& key, const wchar_t* value) {
        winrt::com_ptr<IPropertyStore> store;
        HRESULT hr = ::SHGetPropertyStoreForWindow(hwnd_, IID_PPV_ARGS(store.put()));
        if (FAILED(hr)) return hr;
        shell::PropVariant variant;
        if (value) {
            hr = ::InitPropVariantFromString(value, variant.Put());
            if (FAILED(hr)) return hr;
        } else {
            (void)variant.Put();  // VT_EMPTY removes the property
        }
        return store->SetValue(key, variant.Get());
    }

    // The documented requirement: window properties must be removed before the window is destroyed.
    void Clear() {
        if (!hwnd_) return;
        for (const PROPERTYKEY* key : {&PKEY_AppUserModel_RelaunchDisplayNameResource,
                                       &PKEY_AppUserModel_RelaunchIconResource, &PKEY_AppUserModel_RelaunchCommand,
                                       &PKEY_AppUserModel_ID}) {
            (void)Set(*key, nullptr);
        }
    }

private:
    HWND hwnd_ = nullptr;
};

}  // namespace

AETHER_TEST("window properties: AUMID and relaunch presentation are read from an own hidden window") {
    const platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    REQUIRE(apartment.IsUsable());
    HiddenWindow window;
    REQUIRE(window.Get() != nullptr);
    CHECK_FALSE(::IsWindowVisible(window.Get()));

    const auto empty = ReadWindowShellProperties(window.Get());
    REQUIRE(empty.ok());
    CHECK(empty.value().relaunchIconResource.empty());
    CHECK(empty.value().relaunchDisplayName.empty());

    REQUIRE(SUCCEEDED(window.Set(PKEY_AppUserModel_ID, L"Aether.Fixture.WindowProperties")));
    // A relaunch command accompanies the presentation properties, as applications set them; it is
    // never executed (the window is never shown and nothing reads the command).
    REQUIRE(SUCCEEDED(window.Set(PKEY_AppUserModel_RelaunchCommand, L"C:\\AetherFixture\\fixture.exe")));
    REQUIRE(SUCCEEDED(window.Set(PKEY_AppUserModel_RelaunchIconResource, L"C:\\AetherFixture\\Avatar.ico,0")));
    REQUIRE(SUCCEEDED(window.Set(PKEY_AppUserModel_RelaunchDisplayNameResource, L"  Fixture Profile  ")));

    const auto read = ReadWindowShellProperties(window.Get());
    REQUIRE(read.ok());
    CHECK_EQ(read.value().aumid, std::wstring(L"Aether.Fixture.WindowProperties"));
    CHECK_EQ(read.value().relaunchIconResource, std::wstring(L"C:\\AetherFixture\\Avatar.ico,0"));
    CHECK_EQ(read.value().relaunchDisplayName, std::wstring(L"Fixture Profile"));

    // Indirect names would have to be loaded from a file: dropped.
    REQUIRE(SUCCEEDED(window.Set(PKEY_AppUserModel_RelaunchDisplayNameResource, L"@C:\\AetherFixture\\Strings.dll,-7")));
    const auto indirect = ReadWindowShellProperties(window.Get());
    REQUIRE(indirect.ok());
    CHECK(indirect.value().relaunchDisplayName.empty());
    CHECK_EQ(indirect.value().aumid, std::wstring(L"Aether.Fixture.WindowProperties"));

    window.Clear();
    const auto cleared = ReadWindowShellProperties(window.Get());
    REQUIRE(cleared.ok());
    CHECK(cleared.value().aumid.empty());
    CHECK(cleared.value().relaunchIconResource.empty());
    CHECK(cleared.value().relaunchDisplayName.empty());
}

AETHER_TEST("window properties: a destroyed window is rejected") {
    const platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    REQUIRE(apartment.IsUsable());
    HWND destroyed = nullptr;
    {
        HiddenWindow window;
        destroyed = window.Get();
        REQUIRE(destroyed != nullptr);
    }
    const auto result = ReadWindowShellProperties(destroyed);
    REQUIRE_FALSE(result.ok());
    CHECK_EQ(result.error().code, ErrorCode::InvalidArgument);
}
