// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet

// <propkey.h> declares its PROPERTYKEYs with DEFINE_PROPERTYKEY. Including it once with INITGUID
// instantiates them (as DECLSPEC_SELECTANY data) for the whole library; it must come before any
// header that could include <propkey.h> without INITGUID.
#include <windows.h>
#include <initguid.h>
#include <propsys.h>
#include <propkey.h>

#include "applications/ShellItems.h"

#include "applications/ApplicationIdentity.h"
#include "core/Strings.h"
#include "platform/windows/Win32Error.h"

#include <propvarutil.h>
#include <shlobj_core.h>

namespace aether::apps::shell {

PropVariant::PropVariant() noexcept { ::PropVariantInit(&value_); }

PropVariant::~PropVariant() { ::PropVariantClear(&value_); }

PROPVARIANT* PropVariant::Put() noexcept {
    ::PropVariantClear(&value_);
    return &value_;
}

std::optional<std::wstring> PropVariant::AsString() const {
    const wchar_t* text = nullptr;
    if (value_.vt == VT_LPWSTR) {
        text = value_.pwszVal;
    } else if (value_.vt == VT_BSTR) {
        text = value_.bstrVal;
    }
    if (!text || !*text) return std::nullopt;
    return std::wstring(text);
}

std::optional<std::wstring> GetDisplayName(IShellItem* item, SIGDN form) {
    if (!item) return std::nullopt;
    wchar_t* raw = nullptr;
    if (FAILED(item->GetDisplayName(form, &raw)) || !raw) return std::nullopt;
    UniqueCoTaskMemString name(raw);
    if (!*name) return std::nullopt;
    return std::wstring(name.get());
}

std::optional<std::wstring> GetStringProperty(IShellItem2* item, const PROPERTYKEY& key) {
    if (!item) return std::nullopt;
    wchar_t* raw = nullptr;
    // Missing properties fail with various HRESULTs (TYPE_E_ELEMENTNOTFOUND, E_INVALIDARG...); all of
    // them simply mean "unknown".
    if (FAILED(item->GetString(key, &raw)) || !raw) return std::nullopt;
    UniqueCoTaskMemString value(raw);
    if (!*value) return std::nullopt;
    return std::wstring(value.get());
}

std::optional<std::wstring> GetStringProperty(IPropertyStore* store, const PROPERTYKEY& key) {
    if (!store) return std::nullopt;
    PropVariant value;
    if (FAILED(store->GetValue(key, value.Put()))) return std::nullopt;
    return value.AsString();
}

std::optional<PROPERTYKEY> PropertyKeyFromName(const wchar_t* canonicalName) noexcept {
    PROPERTYKEY key{};
    if (FAILED(::PSGetPropertyKeyFromName(canonicalName, &key))) return std::nullopt;
    return key;
}

bool HasRelativeSegment(std::wstring_view name) noexcept {
    std::size_t start = 0;
    for (;;) {
        const auto end = name.find_first_of(L"\\/", start);
        const auto segment = name.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start);
        // ".", "..", and spellings that path normalization turns into them ("...", ". .").
        if (!segment.empty() && segment.find_first_not_of(L". ") == std::wstring_view::npos) return true;
        if (end == std::wstring_view::npos) return false;
        start = end + 1;
    }
}

Result<winrt::com_ptr<IShellItem>> CreateAppsFolderItem(std::wstring_view parsingName) {
    auto valid = ValidateParsingName(parsingName);
    if (!valid) return valid.error();
    const std::wstring& name = valid.value();
    // Parsing names can come from persisted settings (pinned items): never let one climb out of the
    // Apps folder ("shell:AppsFolder\..\..\Windows\System32"), whichever separator it uses.
    if (HasRelativeSegment(name)) {
        return MakeError(ErrorCode::InvalidArgument, "parsing name contains '.' or '..' segments");
    }
    const std::wstring fullName = L"shell:AppsFolder\\" + name;
    winrt::com_ptr<IShellItem> item;
    const HRESULT hr = ::SHCreateItemFromParsingName(fullName.c_str(), nullptr, IID_PPV_ARGS(item.put()));
    if (FAILED(hr)) return platform::ErrorFromHResult(hr, "Apps folder item not found");
    return item;
}

Result<winrt::com_ptr<IShellItem>> CreateFileItem(const std::wstring& path) {
    winrt::com_ptr<IShellItem> item;
    const HRESULT hr = ::SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item.put()));
    if (FAILED(hr)) return platform::ErrorFromHResult(hr, "file not found");
    return item;
}

Result<ApplicationItem> CreateApplicationItem(const ApplicationInfo& app) {
    Error last = MakeError(ErrorCode::NotFound, "the application has no launchable shell item");

    const auto parsingName = text::Trim(app.parsingName);
    if (!parsingName.empty()) {
        auto item = CreateAppsFolderItem(parsingName);
        if (item) return ApplicationItem{std::move(item).value(), ItemSource::AppsFolder};
        last = item.error();
    }
    // Packaged and explicit AUMIDs are Apps-folder parsing names too (running applications only know
    // their AUMID).
    const auto aumid = text::Trim(app.appUserModelId);
    if (!aumid.empty() && (IsPackagedAumid(aumid) || IsExplicitAumid(aumid)) &&
        !text::EqualsIgnoreCase(aumid, parsingName)) {
        auto item = CreateAppsFolderItem(aumid);
        if (item) return ApplicationItem{std::move(item).value(), ItemSource::AppsFolder};
        last = item.error();
    }
    if (!app.shortcutPath.empty()) {
        if (auto path = NormalizeShortcutPath(app.shortcutPath)) {
            auto item = CreateFileItem(path.value());
            if (item) return ApplicationItem{std::move(item).value(), ItemSource::Shortcut};
            last = item.error();
        } else {
            last = path.error();
        }
    }
    if (!app.executablePath.empty()) {
        if (auto path = NormalizeExecutablePath(app.executablePath)) {
            auto item = CreateFileItem(path.value());
            if (item) return ApplicationItem{std::move(item).value(), ItemSource::Executable};
            last = item.error();
        } else {
            last = path.error();
        }
    }
    return last;
}

bool ShouldPreferShortcutIcon(const ApplicationInfo& app) noexcept {
    try {
        if (app.kind != ApplicationKind::Desktop || app.shortcutPath.empty()) return false;
        auto path = NormalizeShortcutPath(app.shortcutPath);
        if (!path) return false;
        const DWORD attributes = ::GetFileAttributesW(path->c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
    } catch (...) { return false; }
}

std::string ForLog(std::wstring_view value, std::size_t maxLength) {
    return text::SanitizeForLog(text::Narrow(value), maxLength);
}

Error CurrentExceptionToError(std::string_view context) noexcept {
    try {
        try {
            throw;
        } catch (const winrt::hresult_error& error) {
            return platform::ErrorFromHResult(static_cast<HRESULT>(error.code()), context);
        } catch (const std::bad_alloc&) {
            return MakeError(ErrorCode::Internal, std::string(context) + ": out of memory");
        } catch (const std::exception& error) {
            return MakeError(ErrorCode::Internal, std::string(context) + ": " + error.what());
        } catch (...) {
            return MakeError(ErrorCode::Internal, std::string(context) + ": unknown exception");
        }
    } catch (...) {
        return Error{ErrorCode::Internal, {}, 0};
    }
}

StaStrategy CurrentThreadStaStrategy() noexcept {
    APTTYPE type{};
    APTTYPEQUALIFIER qualifier{};
    const HRESULT hr = ::CoGetApartmentType(&type, &qualifier);
    if (hr == CO_E_NOTINITIALIZED) return StaStrategy::InitializeHere;
    if (FAILED(hr)) return StaStrategy::DedicatedThread;
    switch (type) {
        case APTTYPE_STA:
        case APTTYPE_MAINSTA:
            return StaStrategy::Inline;
        case APTTYPE_MTA:
            // A thread that never called CoInitializeEx while the process has an MTA reports the
            // implicit MTA; it may still initialize its own STA.
            return qualifier == APTTYPEQUALIFIER_IMPLICIT_MTA ? StaStrategy::InitializeHere
                                                               : StaStrategy::DedicatedThread;
        default:
            return StaStrategy::DedicatedThread;
    }
}

}  // namespace aether::apps::shell
