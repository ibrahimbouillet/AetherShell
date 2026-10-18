// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/WindowTracker.h"

#include "applications/ApplicationIdentity.h"
#include "applications/ShellItems.h"
#include "applications/WindowEligibility.h"
#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "platform/windows/Handles.h"
#include "platform/windows/Win32Error.h"
#include "platform/windows/WindowUtilities.h"

#include <appmodel.h>
#include <propkey.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <utility>

namespace aether::apps {
namespace {

constexpr std::string_view kLog = "applications";
constexpr wchar_t kWindowClass[] = L"AetherShell.WindowTracker";
constexpr UINT_PTR kCoalesceTimerId = 1;

// Out-of-context WinEvent callbacks run on the thread that installed the hook, so a thread-local
// table is enough to route them back to their tracker.
thread_local std::unordered_map<HWINEVENTHOOK, WindowTracker*> tHookOwners;

struct EventRange {
    DWORD first;
    DWORD last;
};
constexpr std::array<EventRange, 5> kHookedEvents{{
    {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
    {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
    {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE},  // DESTROY, SHOW, HIDE are consecutive
    {EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE},
    {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
}};

bool IsTopLevel(HWND hwnd) noexcept { return ::GetAncestor(hwnd, GA_PARENT) == ::GetDesktopWindow(); }

std::wstring WindowAppUserModelId(HWND hwnd) {
    winrt::com_ptr<IPropertyStore> store;
    if (FAILED(::SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(store.put())))) return {};
    return shell::GetStringProperty(store.get(), PKEY_AppUserModel_ID).value_or(std::wstring{});
}

std::wstring ExecutableStem(const std::wstring& path) {
    return std::filesystem::path(path).stem().native();
}

// Runs on the name worker (STA). Apps-folder display name for AUMIDs ("Google Chrome"), the version
// resource's FileDescription for executables ("Visual Studio Code"). Empty when unknown.
std::wstring ResolveDisplayName(const std::wstring& aumid, const std::wstring& executablePath) {
    if (!aumid.empty()) {
        if (auto item = shell::CreateAppsFolderItem(aumid)) {
            if (auto name = shell::GetDisplayName(item.value().get(), SIGDN_NORMALDISPLAY)) return *name;
        }
    }
    if (!executablePath.empty()) {
        PathPolicy policy;
        policy.allowUnc = false;  // never touch the network from here
        if (auto path = NormalizeExecutablePath(executablePath, policy)) {
            if (auto item = shell::CreateFileItem(path.value())) {
                winrt::com_ptr<IShellItem2> properties;
                if (SUCCEEDED(item.value()->QueryInterface(IID_PPV_ARGS(properties.put())))) {
                    if (auto description = shell::GetStringProperty(properties.get(), PKEY_FileDescription)) {
                        auto trimmed = text::Trim(std::wstring_view(*description));
                        if (!trimmed.empty()) return std::wstring(trimmed);
                    }
                }
            }
        }
    }
    return {};
}

bool SameWindows(const std::vector<RunningWindow>& a, const std::vector<RunningWindow>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const RunningWindow& x, const RunningWindow& y) {
        return x.handle == y.handle && x.minimized == y.minimized && x.title == y.title;
    });
}

bool SameApplications(const std::vector<RunningApplication>& a, const std::vector<RunningApplication>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](const RunningApplication& x, const RunningApplication& y) {
                          return x.id == y.id && x.displayName == y.displayName &&
                                 x.executablePath == y.executablePath && x.appUserModelId == y.appUserModelId &&
                                 x.processId == y.processId && x.foreground == y.foreground &&
                                 SameWindows(x.windows, y.windows);
                      });
}

}  // namespace

WindowTracker::WindowTracker(IUiDispatcher& dispatcher) : WindowTracker(dispatcher, Options{}) {}

WindowTracker::WindowTracker(IUiDispatcher& dispatcher, Options options)
    : dispatcher_(dispatcher), options_(options) {}

WindowTracker::~WindowTracker() {
    Stop();
    if (namePool_) namePool_->Shutdown();
}

Result<void> WindowTracker::Start(Listener listener) {
    if (!dispatcher_.IsUiThread()) return MakeError(ErrorCode::InvalidState, "WindowTracker::Start must run on the UI thread");
    if (running_) return MakeError(ErrorCode::InvalidState, "the window tracker is already running");
    try {
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.lpfnWndProc = &WindowTracker::WindowProc;
        windowClass.hInstance = ::GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kWindowClass;
        if (auto registered = platform::RegisterWindowClassOnce(windowClass); !registered) return registered.error();
        messageWindow_ = ::CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                           windowClass.hInstance, this);
        if (!messageWindow_) return platform::LastWin32Error("creating the window tracker's message window");

        if (!namePool_) {
            namePool_ = std::make_unique<WorkerPool>(
                WorkerPool::Options{1, L"Aether app names", ComApartment::SingleThreaded});
        }
        // Measured: without COM on this thread SHGetPropertyStoreForWindow still returns a store, but
        // per-window AppUserModelIDs cannot be read from it, so windows fall back to process identity.
        APTTYPE apartmentType{};
        APTTYPEQUALIFIER apartmentQualifier{};
        if (::CoGetApartmentType(&apartmentType, &apartmentQualifier) == CO_E_NOTINITIALIZED) {
            AETHER_LOG_WARN(kLog, "COM is not initialized on the UI thread; per-window AppUserModelIDs are unavailable "
                                  "and windows are identified by their process");
        }

        alive_ = std::make_shared<bool>(true);
        running_ = true;

        // Initial state first, then the hooks, so nothing that happens in between is missed for long:
        // any event after this point re-enumerates everything anyway. The listener is installed only
        // afterwards so that it is never invoked re-entrantly from inside Start().
        Refresh();
        listener_ = std::move(listener);
        for (const auto& range : kHookedEvents) {
            const HWINEVENTHOOK hook = ::SetWinEventHook(range.first, range.last, nullptr, &WindowTracker::WinEventProc,
                                                         0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            if (!hook) {
                auto error = platform::LastWin32Error("SetWinEventHook");
                Stop();
                return error;
            }
            hooks_.push_back(hook);
            tHookOwners[hook] = this;
        }
        notifyPending_ = true;
        MarkDirty();
        AETHER_LOG_INFO(kLog, "window tracker started ({} application(s) with windows)", snapshot_.size());
        return Ok();
    } catch (...) {
        auto error = shell::CurrentExceptionToError("starting the window tracker");
        Stop();
        return error;
    }
}

void WindowTracker::Stop() noexcept {
    if (alive_) *alive_ = false;
    alive_.reset();
    running_ = false;
    ReleaseResources();
    listener_ = nullptr;
    pendingNames_.clear();
    // A stopped tracker no longer observes anything: report nothing rather than a stale list whose
    // window handles may already be dead. Per-process, per-frame and display-name caches are kept;
    // they are revalidated on the next Start().
    trackedWindows_.clear();
    firstSeen_.clear();
    notifyPending_ = false;
    try {
        std::lock_guard lock(snapshotMutex_);
        snapshot_.clear();
    } catch (...) {
    }
}

void WindowTracker::ReleaseResources() noexcept {
    for (const HWINEVENTHOOK hook : hooks_) {
        ::UnhookWinEvent(hook);
        tHookOwners.erase(hook);
    }
    hooks_.clear();
    if (messageWindow_) {
        if (timerArmed_) ::KillTimer(messageWindow_, kCoalesceTimerId);
        ::DestroyWindow(messageWindow_);
        messageWindow_ = nullptr;
    }
    timerArmed_ = false;
}

std::vector<RunningApplication> WindowTracker::Snapshot() const {
    std::lock_guard lock(snapshotMutex_);
    return snapshot_;
}

LRESULT CALLBACK WindowTracker::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<WindowTracker*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_TIMER && wParam == kCoalesceTimerId && self) {
        ::KillTimer(hwnd, kCoalesceTimerId);
        self->timerArmed_ = false;
        try {
            self->Refresh();
        } catch (...) {
            const auto error = shell::CurrentExceptionToError("window tracker refresh");
            AETHER_LOG_ERROR(kLog, "{}", error.Describe());
        }
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

void CALLBACK WindowTracker::WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                          DWORD, DWORD) {
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !hwnd) return;
    const auto owner = tHookOwners.find(hook);
    if (owner == tHookOwners.end() || !owner->second) return;
    owner->second->OnWinEvent(event, hwnd);
}

void WindowTracker::OnWinEvent(DWORD event, HWND hwnd) {
    if (!running_) return;
    const bool tracked = trackedWindows_.contains(hwnd);
    switch (event) {
        case EVENT_SYSTEM_FOREGROUND:
            break;  // the foreground application may change even for windows we do not list
        case EVENT_OBJECT_DESTROY:
            if (!tracked) return;  // a window we never listed cannot change the result
            break;
        default:
            // Show/hide/rename/cloak/minimize of child controls is irrelevant.
            if (!tracked && !IsTopLevel(hwnd)) return;
            break;
    }
    MarkDirty();
}

void WindowTracker::MarkDirty() {
    if (!running_ || timerArmed_ || !messageWindow_) return;
    const auto delay = static_cast<UINT>(std::clamp<long long>(options_.coalesceDelay.count(), USER_TIMER_MINIMUM, 5000));
    if (::SetTimer(messageWindow_, kCoalesceTimerId, delay, nullptr) != 0) timerArmed_ = true;
}

const WindowTracker::ProcessIdentity& WindowTracker::LookupProcess(DWORD processId) {
    static const ProcessIdentity kUnopened{};
    platform::UniqueHandle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
    if (!process) {
        processes_.erase(processId);
        return kUnopened;
    }
    FILETIME creation{}, exit{}, kernel{}, user{};
    ::GetProcessTimes(process.Get(), &creation, &exit, &kernel, &user);
    // The creation time detects a process id that was reused by a new process.
    if (const auto cached = processes_.find(processId);
        cached != processes_.end() && ::CompareFileTime(&cached->second.creationTime, &creation) == 0) {
        return cached->second;
    }

    ProcessIdentity identity;
    identity.opened = true;
    identity.creationTime = creation;
    std::array<wchar_t, 256> aumid{};
    UINT32 aumidLength = static_cast<UINT32>(aumid.size());
    if (::GetApplicationUserModelId(process.Get(), &aumidLength, aumid.data()) == ERROR_SUCCESS && aumidLength > 1) {
        identity.appUserModelId.assign(aumid.data(), aumidLength - 1);  // length includes the terminator
    }
    std::wstring image(32768, L'\0');
    DWORD imageLength = static_cast<DWORD>(image.size());
    if (::QueryFullProcessImageNameW(process.Get(), 0, image.data(), &imageLength)) {
        image.resize(imageLength);
        identity.executablePath = std::move(image);
    }
    return processes_.insert_or_assign(processId, std::move(identity)).first->second;
}

WindowIdentity WindowTracker::Identify(HWND hwnd, const std::wstring& className) {
    WindowIdentityFacts facts;
    facts.className = className;
    ::GetWindowThreadProcessId(hwnd, &facts.windowProcessId);
    facts.windowAumid = WindowAppUserModelId(hwnd);

    DWORD identityProcess = facts.windowProcessId;
    if (IsApplicationFrameClass(className)) {
        // ApplicationFrameHost.exe draws the frame; the app itself owns the hosted CoreWindow. This
        // child-window structure is undocumented (but stable since Windows 10 1507).
        if (HWND core = ::FindWindowExW(hwnd, nullptr, L"Windows.UI.Core.CoreWindow", nullptr)) {
            DWORD coreProcess = 0;
            ::GetWindowThreadProcessId(core, &coreProcess);
            if (coreProcess != 0 && coreProcess != facts.windowProcessId) {
                facts.hostedProcessId = coreProcess;
                identityProcess = coreProcess;
            }
        }
        if (const auto known = frameIdentities_.find(hwnd); known != frameIdentities_.end()) {
            facts.rememberedFrameIdentity = known->second;
        }
    }
    const ProcessIdentity& process = LookupProcess(identityProcess);
    facts.processOpened = process.opened;
    facts.processAumid = process.appUserModelId;
    facts.processExecutable = process.executablePath;

    WindowIdentity identity = ResolveWindowIdentity(facts);
    if (ShouldRememberFrameIdentity(facts, identity)) frameIdentities_[hwnd] = identity;
    return identity;
}

std::wstring WindowTracker::DisplayNameFor(const WindowIdentity& identity, const std::wstring& firstTitle) {
    if (GetIdScheme(identity.id) == IdScheme::Window) return firstTitle;
    if (const auto resolved = displayNames_.find(identity.id);
        resolved != displayNames_.end() && !resolved->second.empty()) {
        return resolved->second;
    }
    RequestDisplayName(identity);
    if (!identity.executablePath.empty()) return ExecutableStem(identity.executablePath);
    return firstTitle;
}

void WindowTracker::RequestDisplayName(const WindowIdentity& identity) {
    if (!namePool_ || !alive_ || displayNames_.contains(identity.id) || pendingNames_.contains(identity.id)) return;
    pendingNames_.insert(identity.id);
    std::weak_ptr<bool> alive = alive_;
    IUiDispatcher* dispatcher = &dispatcher_;
    WindowTracker* self = this;
    const bool posted = namePool_->Post([alive, dispatcher, self, id = identity.id, aumid = identity.appUserModelId,
                                         exe = identity.executablePath] {
        if (alive.expired()) return;
        std::wstring name;
        try {
            name = ResolveDisplayName(aumid, exe);
        } catch (...) {
            name.clear();
        }
        dispatcher->Post([alive, self, id, name = std::move(name)] {
            // Runs on the UI thread, like Stop() and the destructor, so this check cannot race them.
            const auto token = alive.lock();
            if (!token || !*token) return;
            self->pendingNames_.erase(id);
            self->displayNames_[id] = name;
            if (!name.empty()) self->MarkDirty();
        });
    });
    if (!posted) pendingNames_.erase(identity.id);
}

void WindowTracker::Refresh() {
    if (!running_) return;
    std::vector<HWND> windows;
    windows.reserve(256);
    ::EnumWindows(
        [](HWND hwnd, LPARAM context) -> BOOL {
            reinterpret_cast<std::vector<HWND>*>(context)->push_back(hwnd);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));

    const HWND foreground = ::GetForegroundWindow();
    const HWND foregroundRoot = foreground ? ::GetAncestor(foreground, GA_ROOTOWNER) : nullptr;

    std::vector<RunningApplication> applications;
    std::vector<WindowIdentity> identities;  // parallel to `applications`
    std::unordered_set<HWND> tracked;
    std::unordered_set<DWORD> seenProcesses;
    for (const HWND hwnd : windows) {  // EnumWindows returns windows in z-order: most recent first
        const WindowFacts facts = CollectWindowFacts(hwnd);
        if (!IsEligibleAppWindow(facts)) continue;
        WindowIdentity identity = Identify(hwnd, facts.className);
        if (identity.id.empty()) continue;
        seenProcesses.insert(identity.processId);
        auto existing = std::find_if(applications.begin(), applications.end(),
                                     [&](const RunningApplication& app) { return app.id == identity.id; });
        if (existing == applications.end()) {
            RunningApplication app;
            app.id = identity.id;
            app.executablePath = identity.executablePath;
            app.appUserModelId = identity.appUserModelId;
            app.processId = identity.processId;
            applications.push_back(std::move(app));
            identities.push_back(identity);
            existing = std::prev(applications.end());
        }
        existing->windows.push_back(RunningWindow{hwnd, facts.title, ::IsIconic(hwnd) != FALSE});
        if (hwnd == foreground || hwnd == foregroundRoot) existing->foreground = true;
        tracked.insert(hwnd);
    }

    // Names, and a stable order: applications keep the position of their first appearance.
    std::unordered_map<std::wstring, std::uint64_t> order;
    for (std::size_t i = 0; i < applications.size(); ++i) {
        auto& app = applications[i];
        app.displayName = DisplayNameFor(identities[i], app.windows.front().title);
        const auto known = firstSeen_.find(app.id);
        order[app.id] = known != firstSeen_.end() ? known->second : nextOrder_++;
    }
    std::stable_sort(applications.begin(), applications.end(),
                     [&](const RunningApplication& a, const RunningApplication& b) { return order[a.id] < order[b.id]; });
    firstSeen_ = std::move(order);

    // Forget state about windows, processes and applications that are gone.
    std::erase_if(frameIdentities_, [](const auto& entry) { return !::IsWindow(entry.first); });
    std::erase_if(processes_, [&](const auto& entry) { return !seenProcesses.contains(entry.first); });
    std::erase_if(displayNames_, [&](const auto& entry) { return !firstSeen_.contains(entry.first); });
    trackedWindows_ = std::move(tracked);

    bool changed = false;
    {
        std::lock_guard lock(snapshotMutex_);
        changed = !SameApplications(snapshot_, applications);
        if (changed) snapshot_ = applications;
    }
    if ((changed || notifyPending_) && listener_) {
        notifyPending_ = false;
        const auto listener = listener_;  // the listener may call Stop()
        listener(applications);
    } else if (changed) {
        notifyPending_ = false;
    }
}

}  // namespace aether::apps
