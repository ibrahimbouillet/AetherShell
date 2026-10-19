// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/ShellApplicationCatalog.h"

#include "applications/ApplicationIdentity.h"
#include "applications/CatalogRules.h"
#include "applications/KnownApplications.h"
#include "applications/ShellItems.h"
#include "applications/TaskbandScan.h"
#include "applications/TaskbarPins.h"
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
#include <algorithm>
#include <chrono>
#include <cstdint>
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
// Packaged AUMIDs confirmed through the Apps folder when no catalog is available (each probe parses
// one shell item).
constexpr std::size_t kMaxAppsFolderProbes = 64;

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
        if (auto child = AppsFolderChildName(*parsing)) return std::move(*child);
        if (!parsing->starts_with(L"::{")) return std::move(*parsing);
    }
    return shell::GetDisplayName(item, SIGDN_PARENTRELATIVEPARSING).value_or(std::wstring{});
}

class AppsFolderReader {
public:
    AppsFolderReader()
        : packageFamilyKey_(shell::PropertyKeyFromName(L"System.AppUserModel.PackageFamilyName")),
          packageFullNameKey_(shell::PropertyKeyFromName(L"System.AppUserModel.PackageFullName")) {}

    // Facts of one Apps-folder item (an enumerated child, or an item reached from a pinned shortcut).
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

    // nullopt when unreadable, or when resolution was requested and the target no longer exists.
    // Pin mode (`appsFolder` set): a shortcut without a file target whose ID list names an Apps-folder
    // item is read through that item (classified like a catalog entry; the .lnk stays attached for
    // its icon), and `fromIdList` reports it.
    std::optional<DiscoveredItem> Read(const std::wstring& path, bool resolve = true,
                                       const AppsFolderReader* appsFolder = nullptr, bool* fromIdList = nullptr) {
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
        if (resolve && link->Resolve(nullptr, flags) != S_OK) {
            AETHER_LOG_DEBUG(kLog, "skipping unresolvable shortcut {}", shell::ForLog(path));
            return std::nullopt;
        }

        DiscoveredItem result;
        result.shortcutPath = path;
        if (link->GetPath(buffer_.data(), static_cast<int>(buffer_.size()), nullptr, 0) == S_OK) {
            result.targetPath = buffer_.c_str();
        }
        if (result.targetPath.empty() && appsFolder) {
            if (auto item = AppsFolderItemOf(link.get())) {
                DiscoveredItem pinned = appsFolder->Read(item.get());
                pinned.shortcutPath = path;
                if (fromIdList) *fromIdList = true;
                return pinned;
            }
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
    // The Apps-folder item a shortcut's ID list points to (documented IShellLink::GetIDList and
    // SHCreateItemFromIDList); null for any other target. Nothing is launched or resolved.
    static winrt::com_ptr<IShellItem> AppsFolderItemOf(IShellLinkW* link) {
        PIDLIST_ABSOLUTE raw = nullptr;
        if (FAILED(link->GetIDList(&raw)) || !raw) return nullptr;
        const shell::UniqueIdList idList(raw);
        winrt::com_ptr<IShellItem> item;
        if (FAILED(::SHCreateItemFromIDList(idList.get(), IID_PPV_ARGS(item.put())))) return nullptr;
        const auto parsing = shell::GetDisplayName(item.get(), SIGDN_DESKTOPABSOLUTEPARSING);
        if (!parsing || !AppsFolderChildName(*parsing)) return nullptr;
        return item;
    }

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

Result<std::filesystem::path> PinDirectory(const TaskbarPinImportOptions& options) {
    if (!options.directory.empty()) return options.directory;
    PWSTR raw = nullptr;
    // In particular, do not use KF_FLAG_CREATE (platform::KnownFolder creates missing folders).
    const HRESULT hr = ::SHGetKnownFolderPath(FOLDERID_UserPinned, KF_FLAG_DONT_VERIFY, nullptr, &raw);
    const shell::UniqueCoTaskMemString owned(raw);
    if (FAILED(hr) || !owned) {
        return MakeError(ErrorCode::Unavailable, "Windows pinned shortcuts are unavailable",
                         static_cast<std::uint32_t>(FAILED(hr) ? hr : E_FAIL));
    }
    return std::filesystem::path(owned.get()) / L"TaskBar";
}

// One file can be named in 8.3 short form ("RUNNER~1") and in long form; the shell resolves shortcut
// targets to the long form. MakeIdForExecutable expands short names, so equal ids mean one executable.
bool SameExecutable(const std::wstring& left, const std::wstring& right) {
    if (left.empty() || right.empty()) return false;
    if (text::EqualsIgnoreCase(left, right)) return true;
    auto a = MakeIdForExecutable(left);
    auto b = MakeIdForExecutable(right);
    return a && b && a.value() == b.value();
}

const ApplicationInfo* CatalogMatch(const ApplicationInfo& pin, const std::vector<ApplicationInfo>& catalog) {
    const ApplicationInfo* match = nullptr;
    const ApplicationInfo* nameMatch = nullptr;
    std::size_t matches = 0;
    std::size_t nameMatches = 0;
    for (const auto& candidate : catalog) {
        if (candidate.id.empty()) continue;
        // An argument-bearing pin is a launch definition, not permission to start a bare catalog app.
        if (text::Trim(pin.arguments) != text::Trim(candidate.arguments)) continue;
        const bool sameId = text::EqualsIgnoreCase(pin.id, candidate.id);
        const bool sameAumid = !pin.appUserModelId.empty() &&
                              text::EqualsIgnoreCase(pin.appUserModelId, candidate.appUserModelId);
        // A pin's explicit profile identity must never be downgraded to an executable-only catalog
        // entry, nor switched to another profile that happens to share that executable.
        const bool sameExecutable = pin.appUserModelId.empty() &&
                                   SameExecutable(pin.executablePath, candidate.executablePath);
        if (!sameId && !sameAumid && !sameExecutable) continue;
        if (sameId) return &candidate;
        match = &candidate;
        ++matches;
        if (text::EqualsIgnoreCase(pin.displayName, candidate.displayName)) {
            nameMatch = &candidate;
            ++nameMatches;
        }
    }
    return matches == 1 ? match : (nameMatches == 1 ? nameMatch : nullptr);
}

// The Windows taskbar's pin list: HKCU ...\Explorer\Taskband "Favorites" (REG_BINARY), read-only with
// documented RegGetValueW. A missing value is an empty list (file-name order). The size is checked
// before anything is allocated; a value that grows between the two calls is read once more.
Result<std::vector<std::uint8_t>> ReadTaskbandFavorites(std::size_t maxBytes) {
    constexpr wchar_t kKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Taskband";
    constexpr wchar_t kValue[] = L"Favorites";
    for (int attempt = 0; attempt < 2; ++attempt) {
        DWORD size = 0;
        LSTATUS status = ::RegGetValueW(HKEY_CURRENT_USER, kKey, kValue, RRF_RT_REG_BINARY, nullptr, nullptr, &size);
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return std::vector<std::uint8_t>{};
        if (status != ERROR_SUCCESS) return platform::ErrorFromWin32(static_cast<DWORD>(status), "reading the Windows pin list");
        if (size > maxBytes) return MakeError(ErrorCode::Unsupported, "the Windows pin list exceeds the size limit");
        std::vector<std::uint8_t> data(size);
        if (size == 0) return data;
        DWORD read = size;
        status = ::RegGetValueW(HKEY_CURRENT_USER, kKey, kValue, RRF_RT_REG_BINARY, nullptr, data.data(), &read);
        if (status == ERROR_MORE_DATA) continue;
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return std::vector<std::uint8_t>{};
        if (status != ERROR_SUCCESS) return platform::ErrorFromWin32(static_cast<DWORD>(status), "reading the Windows pin list");
        data.resize(std::min<std::size_t>(read, data.size()));
        return data;
    }
    return MakeError(ErrorCode::Unavailable, "the Windows pin list changed while it was read");
}

std::string_view SourceName(TaskbandSource source) noexcept {
    switch (source) {
        case TaskbandSource::None: return "none";
        case TaskbandSource::Injected: return "injected";
        case TaskbandSource::Registry: return "registry";
    }
    return "unknown";
}

// Decides which packaged AUMIDs of the pin list are installed. With a catalog, only its packaged
// entries count (it was discovered from the Apps folder); without one, the Apps folder is asked, a
// bounded number of times, with each answer remembered.
class PackagedAumidConfirmer {
public:
    explicit PackagedAumidConfirmer(const std::vector<ApplicationInfo>& catalog) : useCatalog_(!catalog.empty()) {
        for (const auto& app : catalog) {
            const auto aumid = text::Trim(app.appUserModelId);
            if (IsPackagedAumid(aumid)) known_.insert_or_assign(text::ToLower(aumid), true);
        }
    }

    bool operator()(std::wstring_view aumid) {
        const auto key = text::ToLower(aumid);
        if (const auto known = known_.find(key); known != known_.end()) return known->second;
        if (useCatalog_ || probes_ >= kMaxAppsFolderProbes) return false;
        ++probes_;
        const bool installed = shell::CreateAppsFolderItem(aumid).ok();
        known_.insert_or_assign(key, installed);
        return installed;
    }

private:
    bool useCatalog_;
    std::size_t probes_ = 0;
    std::unordered_map<std::wstring, bool> known_;
};

// A packaged pin found only in the pin list: the catalog's entry, else the Apps-folder item itself.
std::optional<ApplicationInfo> ResolvePackagedPin(const std::wstring& aumid, const std::vector<ApplicationInfo>& catalog,
                                                  const AppsFolderReader& appsFolder) {
    const auto found = std::find_if(catalog.begin(), catalog.end(), [&](const ApplicationInfo& app) {
        return text::EqualsIgnoreCase(text::Trim(app.appUserModelId), aumid);
    });
    if (found != catalog.end()) return *found;
    auto item = shell::CreateAppsFolderItem(aumid);
    if (!item) return std::nullopt;
    auto classified = ClassifyDiscoveredItem(appsFolder.Read(item.value().get()));
    if (classified.decision != CatalogDecision::Include) return std::nullopt;
    return std::move(classified.app);
}

Result<TaskbarPinSet> DiscoverPins(const CancellationToken& cancel, const TaskbarPinImportOptions& options) {
    namespace fs = std::filesystem;
    if (cancel.IsCancelled()) return CancelledError();
    if (options.maxShortcutFiles == 0 || options.maxShortcutFiles > 4096) {
        return MakeError(ErrorCode::InvalidArgument, "pin import limit must be between 1 and 4096");
    }
    auto directory = PinDirectory(options);
    if (!directory) return directory.error();
    // Apply the same absolute local-path policy as launchable shortcuts before touching the folder.
    auto probePath = NormalizeShortcutPath((directory.value() / L"pin.lnk").native());
    if (!probePath) return MakeError(ErrorCode::InvalidArgument, "pin import needs an absolute local folder");
    const fs::path root = fs::path(probePath.value()).parent_path();
    if (::GetDriveTypeW(root.root_path().c_str()) == DRIVE_REMOTE) {
        return MakeError(ErrorCode::Unsupported, "pin import needs an ordinary local folder");
    }
    std::vector<fs::path> shortcuts;
    const DWORD rootAttributes = ::GetFileAttributesW(root.c_str());
    if (rootAttributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = ::GetLastError();
        // A missing folder has no shortcuts; the pin list may still name packaged pins.
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            return MakeError(ErrorCode::Unavailable, "pinned shortcuts could not be read", error);
        }
    } else {
        if ((rootAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            return MakeError(ErrorCode::Unsupported, "pin import needs an ordinary local folder");
        }
        std::error_code error;
        fs::directory_iterator it(root, error);
        if (error) return MakeError(ErrorCode::Unavailable, "pinned shortcuts could not be read",
                                    static_cast<std::uint32_t>(error.value()));
        std::size_t examined = 0;
        const fs::directory_iterator end;
        while (it != end) {
            if (cancel.IsCancelled()) return CancelledError();
            if (++examined > 4096) return MakeError(ErrorCode::Unsupported, "pin folder exceeds the import limit");
            const auto& path = it->path();
            if (text::EqualsIgnoreCase(path.extension().native(), L".lnk")) {
                const DWORD attributes = ::GetFileAttributesW(path.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES &&
                    (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0) {
                    if (shortcuts.size() >= options.maxShortcutFiles) {
                        return MakeError(ErrorCode::Unsupported, "pin folder exceeds the import limit");
                    }
                    shortcuts.push_back(path);
                }
            }
            it.increment(error);
            if (error) return MakeError(ErrorCode::Unavailable, "pinned shortcut enumeration failed",
                                        static_cast<std::uint32_t>(error.value()));
        }
    }
    std::sort(shortcuts.begin(), shortcuts.end(), [](const fs::path& left, const fs::path& right) {
        const auto a = text::ToLower(left.filename().native());
        const auto b = text::ToLower(right.filename().native());
        return a == b ? left.native() < right.native() : a < b;
    });

    TaskbarPinSet result;
    ShortcutReader reader;
    const AppsFolderReader appsFolder;
    std::vector<std::optional<ApplicationInfo>> imported(shortcuts.size());
    std::vector<std::wstring> fileNames;
    fileNames.reserve(shortcuts.size());
    for (std::size_t i = 0; i < shortcuts.size(); ++i) {
        if (cancel.IsCancelled()) return CancelledError();
        fileNames.push_back(shortcuts[i].filename().native());
        // Metadata only: Resolve can perform shell work beyond a read and is unnecessary for import.
        bool fromIdList = false;
        auto item = reader.Read(shortcuts[i].native(), false, &appsFolder, &fromIdList);
        if (!item) {
            ++result.skipped;
            continue;
        }
        auto classified = ClassifyDiscoveredItem(*item);
        if (classified.decision != CatalogDecision::Include) {
            ++result.skipped;
            continue;
        }
        if (fromIdList) ++result.pidlShortcuts;
        auto pin = std::move(classified.app);
        if (const auto* match = CatalogMatch(pin, options.catalog)) {
            auto enriched = *match;
            enriched.displayName = pin.displayName;
            enriched.shortcutPath = pin.shortcutPath;
            enriched.arguments = pin.arguments;
            if (!pin.executablePath.empty()) enriched.executablePath = pin.executablePath;
            pin = std::move(enriched);
        }
        imported[i] = std::move(pin);
    }

    // Windows order and packaged pins from the pin list; any failure keeps file-name order.
    const TaskbandSource source = SelectTaskbandSource(options);
    std::optional<TaskbandScan> scan;
    if (source != TaskbandSource::None) {
        auto blob = source == TaskbandSource::Injected ? options.taskbandReader()
                                                       : ReadTaskbandFavorites(options.maxTaskbandBytes);
        if (blob) {
            PackagedAumidConfirmer confirm(options.catalog);
            auto scanned = ScanTaskband(blob.value(), fileNames,
                                        [&confirm](std::wstring_view aumid) { return confirm(aumid); },
                                        options.maxTaskbandBytes);
            if (scanned) {
                scan = std::move(scanned).value();
            } else {
                AETHER_LOG_WARN(kLog, "Windows pin order unavailable: {}", ToString(scanned.error().code));
            }
        } else {
            AETHER_LOG_WARN(kLog, "Windows pin list unavailable: {}", ToString(blob.error().code));
        }
    }
    if (cancel.IsCancelled()) return CancelledError();

    struct Ordered {
        std::optional<std::size_t> offset;  // position in the pin list
        std::size_t sequence = 0;           // file-name order, then pin-list-only packaged pins
        ApplicationInfo app;
    };
    std::vector<Ordered> ordered;
    std::size_t matchedShortcuts = 0;
    for (std::size_t i = 0; i < imported.size(); ++i) {
        if (!imported[i]) continue;
        std::optional<std::size_t> offset;
        if (scan) offset = scan->shortcutOffsets[i];
        if (offset) ++matchedShortcuts;
        ordered.push_back(Ordered{offset, i, std::move(*imported[i])});
    }
    if (scan) {
        for (const auto& [offset, aumid] : scan->packaged) {
            if (cancel.IsCancelled()) return CancelledError();
            auto app = ResolvePackagedPin(aumid, options.catalog, appsFolder);
            if (!app) {
                ++result.skipped;
                continue;
            }
            ++result.fromTaskband;
            ordered.push_back(Ordered{offset, imported.size() + ordered.size(), std::move(*app)});
        }
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const Ordered& a, const Ordered& b) {
        if (a.offset.has_value() != b.offset.has_value()) return a.offset.has_value();
        if (a.offset && *a.offset != *b.offset) return *a.offset < *b.offset;
        return a.sequence < b.sequence;
    });
    result.windowsOrder = matchedShortcuts > 0 || result.fromTaskband > 0;

    std::wstring windowsDirectory = options.windowsDirectory;
    if (windowsDirectory.empty()) windowsDirectory = SystemWindowsDirectory().value_or(std::wstring{});
    std::vector<ApplicationInfo> pins;
    pins.reserve(ordered.size());
    for (auto& entry : ordered) {
        CanonicalizeFileExplorer(entry.app, windowsDirectory);
        pins.push_back(std::move(entry.app));
    }
    result.pins = MergeDuplicateApplications(std::move(pins));
    AETHER_LOG_INFO(kLog,
                    "taskbar pins: {} imported ({} order; pin list: {}), {} packaged from the pin list, {} by ID "
                    "list, {} skipped",
                    result.pins.size(), result.windowsOrder ? "Windows" : "file-name", SourceName(source),
                    result.fromTaskband, result.pidlShortcuts, result.skipped);
    return result;
}

}  // namespace

TaskbandSource SelectTaskbandSource(const TaskbarPinImportOptions& options) noexcept {
    if (!options.useTaskbandOrder) return TaskbandSource::None;
    if (options.taskbandReader) return TaskbandSource::Injected;
    // Directory overrides belong to isolated callers (tests): never pair them with the user's list.
    return options.directory.empty() ? TaskbandSource::Registry : TaskbandSource::None;
}

Result<std::vector<ApplicationInfo>> ShellApplicationCatalog::DiscoverApplications(const CancellationToken& cancel) {
    const Options options = options_;
    return shell::RunOnSta<std::vector<ApplicationInfo>>([&] { return Discover(options, cancel); },
                                                         "application discovery");
}

Result<TaskbarPinSet> DiscoverTaskbarPinSet(const CancellationToken& cancel,
                                            const TaskbarPinImportOptions& options) noexcept {
    try {
        return shell::RunOnSta<TaskbarPinSet>([&]() -> Result<TaskbarPinSet> {
            try {
                return DiscoverPins(cancel, options);
            } catch (...) {
                return MakeError(ErrorCode::Internal, "pinned shortcut import failed");
            }
        }, "pinned shortcut import");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "pinned shortcut import failed");
    }
}

Result<std::vector<ApplicationInfo>> DiscoverTaskbarPins(const CancellationToken& cancel,
                                                        const TaskbarPinImportOptions& options) noexcept {
    try {
        auto set = DiscoverTaskbarPinSet(cancel, options);
        if (!set) return set.error();
        return std::move(set.value().pins);
    } catch (...) {
        return MakeError(ErrorCode::Internal, "pinned shortcut import failed");
    }
}

}  // namespace aether::apps
