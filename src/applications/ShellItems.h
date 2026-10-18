// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Internal shell helpers shared by the catalog, the launcher, the window tracker and the icon
// provider: RAII for shell memory, string properties, creating the shell item that represents an
// application, and running COM work on a single-threaded apartment (shell namespace extensions and
// IShellItemImageFactory expect an STA).
#pragma once

#include "applications/ApplicationTypes.h"
#include "core/Result.h"
#include "platform/windows/ComApartment.h"

#include <windows.h>
#include <unknwn.h>
#include <propsys.h>
#include <shobjidl_core.h>

#include <winrt/base.h>

#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace aether::apps::shell {

struct CoTaskMemDeleter {
    void operator()(void* memory) const noexcept { ::CoTaskMemFree(memory); }
};
using UniqueCoTaskMemString = std::unique_ptr<wchar_t, CoTaskMemDeleter>;
using UniqueIdList = std::unique_ptr<std::remove_pointer_t<PIDLIST_ABSOLUTE>, CoTaskMemDeleter>;

// RAII PROPVARIANT.
class PropVariant {
public:
    PropVariant() noexcept;
    ~PropVariant();
    PropVariant(const PropVariant&) = delete;
    PropVariant& operator=(const PropVariant&) = delete;

    [[nodiscard]] PROPVARIANT* Put() noexcept;  // clears, then exposes for an out-parameter
    [[nodiscard]] const PROPVARIANT& Get() const noexcept { return value_; }
    // The value as a string when it is VT_LPWSTR or VT_BSTR and non-empty.
    [[nodiscard]] std::optional<std::wstring> AsString() const;

private:
    PROPVARIANT value_;
};

[[nodiscard]] std::optional<std::wstring> GetDisplayName(IShellItem* item, SIGDN form);
[[nodiscard]] std::optional<std::wstring> GetStringProperty(IShellItem2* item, const PROPERTYKEY& key);
[[nodiscard]] std::optional<std::wstring> GetStringProperty(IPropertyStore* store, const PROPERTYKEY& key);

// Looks a property key up by its canonical name ("System.AppUserModel.PackageFamilyName") for the
// properties the SDK does not declare in propkey.h. nullopt when the property system does not know it.
[[nodiscard]] std::optional<PROPERTYKEY> PropertyKeyFromName(const wchar_t* canonicalName) noexcept;

// "shell:AppsFolder\<parsing name>". The parsing name is validated first (see ValidateParsingName);
// names with "." or ".." segments (with '\' or '/' separators, or dot/space spellings such as "...")
// are rejected as well, so the item always lies inside the Apps folder.
[[nodiscard]] bool HasRelativeSegment(std::wstring_view name) noexcept;
[[nodiscard]] Result<winrt::com_ptr<IShellItem>> CreateAppsFolderItem(std::wstring_view parsingName);
// Shell item for an existing file. The caller validates the path first.
[[nodiscard]] Result<winrt::com_ptr<IShellItem>> CreateFileItem(const std::wstring& path);

enum class ItemSource { AppsFolder, Shortcut, Executable };

struct ApplicationItem {
    winrt::com_ptr<IShellItem> item;
    ItemSource source = ItemSource::AppsFolder;
};

// The shell item that best represents `app`, in order: the Apps-folder item (parsing name, else a
// packaged or explicit AUMID), the validated .lnk, the validated executable. Network (UNC) paths are
// rejected. NotFound when nothing usable exists.
[[nodiscard]] Result<ApplicationItem> CreateApplicationItem(const ApplicationInfo& app);

// UTF-8, control characters removed, bounded: safe to put in a log line.
[[nodiscard]] std::string ForLog(std::wstring_view text, std::size_t maxLength = 200);

// Converts an exception escaping COM/shell code into an Error. Call from a catch block only.
[[nodiscard]] Error CurrentExceptionToError(std::string_view context) noexcept;

enum class StaStrategy {
    Inline,           // the thread already is in a single-threaded apartment
    InitializeHere,   // COM is not initialized here (or only through the implicit MTA)
    DedicatedThread,  // the thread is in the MTA or NTA: hop to a temporary STA thread
};
[[nodiscard]] StaStrategy CurrentThreadStaStrategy() noexcept;

// Runs `work` on a COM single-threaded apartment and returns its result: inline when the calling
// thread already is an STA, after initializing one when COM is not initialized on this thread, and
// on a temporary STA thread (joined before returning) when the caller lives in the MTA. Exceptions
// thrown by `work` are converted into errors; none escapes.
template <class T>
Result<T> RunOnSta(const std::function<Result<T>()>& work, std::string_view context) noexcept {
    const auto invoke = [&]() -> Result<T> {
        try {
            return work();
        } catch (...) {
            return CurrentExceptionToError(context);
        }
    };
    try {
        switch (CurrentThreadStaStrategy()) {
            case StaStrategy::Inline:
                return invoke();
            case StaStrategy::InitializeHere: {
                platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
                if (!apartment.IsUsable()) {
                    return MakeError(ErrorCode::Platform, std::string(context) + ": COM initialization failed",
                                     static_cast<std::uint32_t>(apartment.Status()));
                }
                return invoke();
            }
            case StaStrategy::DedicatedThread: {
                std::optional<Result<T>> result;
                std::thread thread([&]() noexcept {
                    try {
                        ::SetThreadDescription(::GetCurrentThread(), L"Aether shell STA");
                        platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
                        if (!apartment.IsUsable()) {
                            result.emplace(MakeError(ErrorCode::Platform,
                                                     std::string(context) + ": COM initialization failed",
                                                     static_cast<std::uint32_t>(apartment.Status())));
                            return;
                        }
                        result.emplace(invoke());
                    } catch (...) {
                        result.emplace(MakeError(ErrorCode::Internal, std::string(context) + ": STA thread failed"));
                    }
                });
                thread.join();
                if (!result) return MakeError(ErrorCode::Internal, std::string(context) + ": STA thread failed");
                return std::move(*result);
            }
        }
    } catch (...) {
        return CurrentExceptionToError(context);
    }
    return MakeError(ErrorCode::Internal, std::string(context) + ": unexpected apartment state");
}

}  // namespace aether::apps::shell
