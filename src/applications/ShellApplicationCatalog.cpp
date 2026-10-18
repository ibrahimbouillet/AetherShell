// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/ShellApplicationCatalog.h"

#include "applications/CatalogRules.h"
#include "applications/ShellItems.h"
#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "platform/windows/AppPaths.h"
#include "platform/windows/Win32Error.h"

#include <windows.h>
#include <knownfolders.h>
#include <propkey.h>
#include <shlobj_core.h>
#include <shlwapi.h>
#include <shobjidl_core.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace aether::apps {
namespace {

constexpr std::string_view kLog = "applications";
// Upper bound for one IShellLink::Resolve (it only checks that the target exists: searching and link
// tracking are disabled).
constexpr DWORD kResolveTimeoutMs = 1000;
// "::{4234D49B-0245-4DF3-B780-3893943456E1}" is the Apps folder's own parsing name.
constexpr std::wstring_view kAppsFolderParsingPrefix = L"::{4234D49B-0245-4DF3-B780-3893943456E1}\\";
constexpr std::wstring_view kAppsFolderShellPrefix = L"shell:AppsFolder\\";

Error CancelledError() { return MakeError(ErrorCode::Cancelled, "application discovery was cancelled"); }

struct DecisionCounts {
    std::array<std::size_t, 6> values{};
    void Add(CatalogDecision decision) { ++values[static_cast<std::size_t>(decision)]; }
    [[nodiscard]] std::size_t Get(CatalogDecision decision) const { return values[static_cast<std::size_t>(decision)]; }
};

bool StartsWithIgnoreCase(std::wstring_view value, std::wstring_view prefix) noexcept {
    return value.size() >= prefix.size() && text::EqualsIgnoreCase(value.substr(0, prefix.size()), prefix);
}

// The child's name inside the Apps folder ("Microsoft.WindowsCalculator_8wekyb3d8bbwe!App"). The
// parsing name of an Apps-folder child (SIGDN_DESKTOPABSOLUTEPARSING) is absolute
// ("::{4234D49B-...}\<name>"), so the folder prefix is removed; SIGDN_PARENTRELATIVEPARSING is the
// fallback for any other shape.
std::wstring AppsFolderRelativeName(IShellItem* item) {
    if (auto parsing = shell::GetDisplayName(item, SIGDN_DESKTOPABSOLUTEPARSING)) {
        std::wstring_view name = *parsing;
        if (StartsWithIgnoreCase(name, kAppsFolderParsingPrefix)) return std::wstring(name.substr(kAppsFolderParsingPrefix.size()));
        if (StartsWithIgnoreCase(name, kAppsFolderShellPrefix)) return std::wstring(name.substr(kAppsFolderShellPrefix.size()));
        if (!name.starts_with(L"::{")) return std::wstring(name);
    }
    return shell::GetDisplayName(item, SIGDN_PARENTRELATIVEPARSING).value_or(std::wstring{});
}

class AppsFolderReader {
public:
    AppsFolderReader()
        : packageFamilyKey_(shell::PropertyKeyFromName(L"System.AppUserModel.PackageFamilyName")),
          packageFullNameKey_(shell::PropertyKeyFromName(L"System.AppUserModel.PackageFullName")) {}

    Result<std::vector<DiscoveredItem>> ReadAll(const CancellationToken& cancel) const {
        winrt::com_ptr<IShellItem> folder;
        HRESULT hr = ::SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(folder.put()));
        if (FAILED(hr)) return platform::ErrorFromHResult(hr, "opening the Apps folder");
        winrt::com_ptr<IEnumShellItems> enumerator;
        hr = folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(enumerator.put()));
        if (FAILED(hr)) return platform::ErrorFromHResult(hr, "enumerating the Apps folder");

        std::vector<DiscoveredItem> items;
        for (;;) {
            if (cancel.IsCancelled()) return CancelledError();
            std::array<IShellItem*, 32> batch{};
            ULONG fetched = 0;
            hr = enumerator->Next(static_cast<ULONG>(batch.size()), batch.data(), &fetched);
            // Take ownership of every returned item first so none leaks on an early return.
            std::array<winrt::com_ptr<IShellItem>, 32> owned;
            for (ULONG i = 0; i < fetched && i < batch.size(); ++i) owned[i].attach(batch[i]);
            for (ULONG i = 0; i < fetched && i < batch.size(); ++i) {
                if (cancel.IsCancelled()) return CancelledError();
                items.push_back(Read(owned[i].get()));
            }
            if (FAILED(hr)) {
                if (items.empty()) return platform::ErrorFromHResult(hr, "enumerating the Apps folder");
                AETHER_LOG_WARN(kLog, "Apps folder enumeration stopped early (0x{:08X})", static_cast<unsigned>(hr));
                break;
            }
            if (hr != S_OK || fetched == 0) break;  // S_FALSE: end of the list
        }
        return items;
    }

private:
    DiscoveredItem Read(IShellItem* item) const {
        DiscoveredItem result;
        result.displayName = shell::GetDisplayName(item, SIGDN_NORMALDISPLAY).value_or(std::wstring{});
        result.parsingName = AppsFolderRelativeName(item);
        winrt::com_ptr<IShellItem2> properties;
        if (FAILED(item->QueryInterface(IID_PPV_ARGS(properties.put())))) return result;
        result.appUserModelId = shell::GetStringProperty(properties.get(), PKEY_AppUserModel_ID).value_or(std::wstring{});
        result.targetPath = shell::GetStringProperty(properties.get(), PKEY_Link_TargetParsingPath).value_or(std::wstring{});
        result.arguments = shell::GetStringProperty(properties.get(), PKEY_Link_Arguments).value_or(std::wstring{});
        result.hasPackageIdentity =
            (packageFamilyKey_ && shell::GetStringProperty(properties.get(), *packageFamilyKey_).has_value()) ||
            (packageFullNameKey_ && shell::GetStringProperty(properties.get(), *packageFullNameKey_).has_value());
        return result;
    }

    std::optional<PROPERTYKEY> packageFamilyKey_;
    std::optional<PROPERTYKEY> packageFullNameKey_;
};

// Shell display names of files, which honour desktop.ini localization ("Invite de commandes" for
// "Command Prompt.lnk"). Each directory's IShellFolder is bound once and children are resolved
// relative to it: re-parsing every full path (SHCreateItemFromParsingName) cost ~14 ms per shortcut.
class DisplayNameResolver {
public:
    // Empty when the shell cannot name the file.
    std::wstring Get(const std::filesystem::path& file) {
        IShellFolder* folder = FolderFor(file.parent_path().native());
        if (!folder) return {};
        std::wstring childName = file.filename().native();  // ParseDisplayName wants a mutable buffer
        PIDLIST_RELATIVE rawChild = nullptr;
        if (FAILED(folder->ParseDisplayName(nullptr, nullptr, childName.data(), nullptr, &rawChild, nullptr)) ||
            !rawChild) {
            return {};
        }
        const std::unique_ptr<std::remove_pointer_t<PIDLIST_RELATIVE>, shell::CoTaskMemDeleter> child(rawChild);
        STRRET value{};
        if (FAILED(folder->GetDisplayNameOf(child.get(), SHGDN_NORMAL, &value))) return {};
        wchar_t* rawName = nullptr;  // StrRetToStrW frees the STRRET's own allocation
        if (FAILED(::StrRetToStrW(&value, child.get(), &rawName)) || !rawName) return {};
        const shell::UniqueCoTaskMemString name(rawName);
        return std::wstring(name.get());
    }

private:
    IShellFolder* FolderFor(const std::wstring& directory) {
        if (const auto cached = folders_.find(directory); cached != folders_.end()) return cached->second.get();
        winrt::com_ptr<IShellFolder> folder;
        winrt::com_ptr<IShellItem> item;
        if (SUCCEEDED(::SHCreateItemFromParsingName(directory.c_str(), nullptr, IID_PPV_ARGS(item.put())))) {
            if (FAILED(item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(folder.put())))) folder = nullptr;
        }
        return folders_.emplace(directory, std::move(folder)).first->second.get();  // failures cached as null
    }

    std::unordered_map<std::wstring, winrt::com_ptr<IShellFolder>> folders_;
};

class ShortcutReader {
public:
    ShortcutReader() : buffer_(32768, L'\0') {
        std::array<wchar_t, MAX_PATH> windows{};
        const UINT length = ::GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
        if (length > 0 && length < windows.size()) {
            installerDirectory_ = std::wstring(windows.data(), length) + L"\\Installer\\";
        }
    }

    // nullopt when the file is not a readable shortcut or its target no longer exists.
    std::optional<DiscoveredItem> Read(const std::wstring& path) {
        winrt::com_ptr<IShellLinkW> link;
        if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.put())))) {
            return std::nullopt;
        }
        winrt::com_ptr<IPersistFile> file;
        if (FAILED(link->QueryInterface(IID_PPV_ARGS(file.put()))) || FAILED(file->Load(path.c_str(), STGM_READ))) {
            return std::nullopt;
        }
        // Checks that the target still exists. SLR_NO_UI: never show the "searching" or "problem with
        // shortcut" dialogs (the high word is the timeout). SLR_NOUPDATE: never write the .lnk back.
        // No search heuristics or distributed link tracking (both can scan disks or the network).
        const DWORD flags = SLR_NO_UI | SLR_NOUPDATE | SLR_NOSEARCH | SLR_NOTRACK | SLR_NOLINKINFO |
                            (kResolveTimeoutMs << 16);
        if (link->Resolve(nullptr, flags) != S_OK) {
            AETHER_LOG_DEBUG(kLog, "skipping unresolvable shortcut {}", shell::ForLog(path));
            return std::nullopt;
        }

        DiscoveredItem result;
        result.shortcutPath = path;
        if (link->GetPath(buffer_.data(), static_cast<int>(buffer_.size()), nullptr, 0) == S_OK) {
            result.targetPath = buffer_.c_str();
        }
        // Advertised (Windows Installer) shortcuts report an icon file in %WINDIR%\Installer instead of
        // the real target; the Apps folder resolves those itself.
        if (!installerDirectory_.empty() && StartsWithIgnoreCase(result.targetPath, installerDirectory_)) return std::nullopt;
        if (SUCCEEDED(link->GetArguments(buffer_.data(), static_cast<int>(buffer_.size())))) {
            result.arguments = buffer_.c_str();
        }
        winrt::com_ptr<IPropertyStore> store;
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(store.put())))) {
            result.appUserModelId = shell::GetStringProperty(store.get(), PKEY_AppUserModel_ID).value_or(std::wstring{});
        }
        const std::filesystem::path shortcutFile(path);
        result.displayName = names_.Get(shortcutFile);
        if (result.displayName.empty()) result.displayName = shortcutFile.stem().native();
        return result;
    }

private:
    std::wstring buffer_;
    std::wstring installerDirectory_;
    DisplayNameResolver names_;
};

// Walks one Programs folder. Returns Cancelled when cancellation was requested.
Result<void> CollectShortcuts(const std::filesystem::path& root, const ShellApplicationCatalog::Options& options,
                              const CancellationToken& cancel, ShortcutReader& reader, std::size_t& filesRead,
                              std::vector<DiscoveredItem>& out) {
    namespace fs = std::filesystem;
    std::error_code error;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error);
    if (error) {
        AETHER_LOG_WARN(kLog, "cannot read {}: {}", shell::ForLog(root.native()), error.message());
        return Ok();
    }
    // Directory symlinks and junctions are not followed (default directory_options), so the walk
    // cannot loop; depth and file-count limits bound it on pathological Start menus.
    const fs::recursive_directory_iterator end;
    while (it != end) {
        if (cancel.IsCancelled()) return CancelledError();
        std::error_code entryError;
        if (it->is_directory(entryError)) {
            if (it.depth() >= options.maxShortcutDepth) it.disable_recursion_pending();
        } else if (text::EqualsIgnoreCase(it->path().extension().native(), L".lnk") &&
                   it->is_regular_file(entryError)) {
            if (filesRead >= options.maxShortcutFiles) {
                AETHER_LOG_WARN(kLog, "stopped reading Start-menu shortcuts after {} files", filesRead);
                return Ok();
            }
            ++filesRead;
            if (auto item = reader.Read(it->path().native())) out.push_back(std::move(*item));
        }
        it.increment(error);
        if (error) {
            AETHER_LOG_WARN(kLog, "Start-menu walk of {} stopped: {}", shell::ForLog(root.native()), error.message());
            break;
        }
    }
    return Ok();
}

Result<std::vector<ApplicationInfo>> Discover(const ShellApplicationCatalog::Options& options,
                                              const CancellationToken& cancel) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const auto millisecondsSince = [](Clock::time_point from) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - from).count();
    };
    DecisionCounts counts;

    std::vector<ApplicationInfo> appsFolderApps;
    std::size_t appsFolderItems = 0;
    std::optional<Error> appsFolderError;
    auto discovered = AppsFolderReader().ReadAll(cancel);
    if (discovered) {
        appsFolderItems = discovered.value().size();
        for (const auto& item : discovered.value()) {
            auto classification = ClassifyDiscoveredItem(item);
            counts.Add(classification.decision);
            if (classification.decision == CatalogDecision::Include) appsFolderApps.push_back(std::move(classification.app));
        }
    } else {
        if (discovered.error().code == ErrorCode::Cancelled) return discovered.error();
        AETHER_LOG_WARN(kLog, "Apps folder unavailable: {}", discovered.error().Describe());
        appsFolderError = discovered.error();
    }

    const auto appsFolderMs = millisecondsSince(start);

    const auto shortcutsStart = Clock::now();
    std::vector<ApplicationInfo> shortcutApps;
    std::size_t shortcutFiles = 0;
    if (options.includeStartMenuShortcuts) {
        ShortcutReader reader;
        std::vector<DiscoveredItem> shortcuts;
        for (const GUID* folderId : {&FOLDERID_CommonPrograms, &FOLDERID_Programs}) {
            auto folder = platform::KnownFolder(*folderId);
            if (!folder) {
                AETHER_LOG_WARN(kLog, "Start-menu folder unavailable: {}", folder.error().Describe());
                continue;
            }
            auto walked = CollectShortcuts(folder.value(), options, cancel, reader, shortcutFiles, shortcuts);
            if (!walked) return walked.error();
        }
        for (const auto& item : shortcuts) {
            auto classification = ClassifyDiscoveredItem(item);
            counts.Add(classification.decision);
            if (classification.decision == CatalogDecision::Include) shortcutApps.push_back(std::move(classification.app));
        }
    }

    if (appsFolderError && shortcutApps.empty()) {
        return MakeError(ErrorCode::Unavailable, "no application source could be read: " + appsFolderError->message,
                         appsFolderError->platformCode);
    }
    if (cancel.IsCancelled()) return CancelledError();

    const auto shortcutsMs = millisecondsSince(shortcutsStart);

    auto applications = CombineCatalog(std::move(appsFolderApps), std::move(shortcutApps));
    AETHER_LOG_INFO(kLog,
                    "discovered {} applications in {} ms (Apps folder: {} items in {} ms; Start menu: {} shortcuts "
                    "in {} ms; skipped {} uninstallers, {} help, {} documents, {} URLs, {} invalid)",
                    applications.size(), millisecondsSince(start), appsFolderItems, appsFolderMs, shortcutFiles,
                    shortcutsMs,
                    counts.Get(CatalogDecision::SkipUninstaller), counts.Get(CatalogDecision::SkipHelp),
                    counts.Get(CatalogDecision::SkipDocument), counts.Get(CatalogDecision::SkipUrl),
                    counts.Get(CatalogDecision::SkipInvalid));
    return applications;
}

}  // namespace

Result<std::vector<ApplicationInfo>> ShellApplicationCatalog::DiscoverApplications(const CancellationToken& cancel) {
    const Options options = options_;
    return shell::RunOnSta<std::vector<ApplicationInfo>>([&] { return Discover(options, cancel); },
                                                         "application discovery");
}

}  // namespace aether::apps
