// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "media_controls/MediaBrightness.h"

#include "platform/windows/Handles.h"

#include <roapi.h>
#include <wbemidl.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace aether::controlcenter {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace media = winrt::Windows::Media::Control;

Error ApiError(HRESULT code, const char* operation) {
    auto category = ErrorCode::Platform;
    if (code == E_ACCESSDENIED) category = ErrorCode::AccessDenied;
    else if (code == REGDB_E_CLASSNOTREG || code == E_NOTIMPL) category = ErrorCode::Unsupported;
    else if (code == RO_E_CLOSED || code == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) category = ErrorCode::Unavailable;
    return MakeError(category, operation, code); // never include exception strings or provider metadata
}

class MtaApartment {
public:
    MtaApartment() : status_(::RoInitialize(RO_INIT_MULTITHREADED)) {}
    ~MtaApartment() { if (SUCCEEDED(status_)) ::RoUninitialize(); }
    Result<void> Check() const {
        if (status_ == RPC_E_CHANGED_MODE)
            return MakeError(ErrorCode::InvalidState, "Media and brightness require an MTA worker");
        return SUCCEEDED(status_) ? Ok() : Result<void>(ApiError(status_, "Windows runtime initialization failed"));
    }
private:
    HRESULT status_;
};

struct Deadline {
    CancellationToken cancel;
    Clock::time_point end;
    Result<void> Check() const {
        if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, "Control request cancelled");
        if (Clock::now() >= end) return MakeError(ErrorCode::Timeout, "Control request timed out");
        return Ok();
    }
};

Result<Deadline> MakeDeadline(CancellationToken cancel, std::chrono::milliseconds timeout) {
    if (timeout <= 0ms || timeout > 10s) return MakeError(ErrorCode::InvalidArgument, "Invalid control timeout");
    Deadline deadline{std::move(cancel), Clock::now() + timeout};
    if (auto checked = deadline.Check(); !checked) return checked.error();
    return deadline;
}

// Completed captures only a shared event, never stack state or the provider. A timed-out/cancelled
// operation may finish later; that completion remains safe even after this call has returned.
template<class T>
Result<T> Await(const winrt::Windows::Foundation::IAsyncOperation<T>& operation, const Deadline& deadline) {
    auto signal = std::make_shared<platform::UniqueHandle>(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!*signal) return ApiError(HRESULT_FROM_WIN32(::GetLastError()), "Media wait initialization failed");
    operation.Completed([signal](const auto&, const auto&) { ::SetEvent(signal->Get()); });
    for (;;) {
        if (auto checked = deadline.Check(); !checked) {
            try { operation.Cancel(); } catch (...) {}
            return checked.error();
        }
        const auto waited = ::WaitForSingleObject(signal->Get(), 25);
        if (waited == WAIT_OBJECT_0) return operation.GetResults();
        if (waited != WAIT_TIMEOUT) {
            try { operation.Cancel(); } catch (...) {}
            return ApiError(HRESULT_FROM_WIN32(::GetLastError()), "Media completion wait failed");
        }
    }
}

MediaPlayback Playback(media::GlobalSystemMediaTransportControlsSessionPlaybackStatus value) noexcept {
    using Status = media::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
    switch (value) {
        case Status::Closed: return MediaPlayback::Closed;
        case Status::Opened: return MediaPlayback::Opened;
        case Status::Changing: return MediaPlayback::Changing;
        case Status::Stopped: return MediaPlayback::Stopped;
        case Status::Playing: return MediaPlayback::Playing;
        case Status::Paused: return MediaPlayback::Paused;
        default: return MediaPlayback::Unknown;
    }
}

bool SameSession(const media::GlobalSystemMediaTransportControlsSession& a,
                 const media::GlobalSystemMediaTransportControlsSession& b) {
    return a && b && a.as<::IUnknown>().get() == b.as<::IUnknown>().get();
}

struct Variant {
    VARIANT value{};
    Variant() { ::VariantInit(&value); }
    ~Variant() { ::VariantClear(&value); }
};
struct Bstr {
    explicit Bstr(const wchar_t* text) : value(::SysAllocString(text)) {
        if (!value) throw std::bad_alloc();
    }
    ~Bstr() { ::SysFreeString(value); }
    BSTR value;
};

// Only local WMI is used. Cancellation is enabled before outbound calls. The watchdog can cancel
// a stalled synchronous COM call; semisynchronous query/method results also have short bounded waits.
// No worker thread is detached and the watchdog is joined before its target thread can be reused.
class WmiCallGuard {
public:
    explicit WmiCallGuard(const Deadline& deadline)
        : deadline_(deadline), thread_(::GetCurrentThreadId()), done_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        if (!done_) throw winrt::hresult_error(HRESULT_FROM_WIN32(::GetLastError()));
        winrt::check_hresult(::CoEnableCallCancellation(nullptr));
        enabled_ = true;
        try {
            watchdog_ = std::thread([this] {
                const auto initialized = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                for (;;) {
                    if (::WaitForSingleObject(done_.Get(), 25) != WAIT_TIMEOUT) break;
                    // Keep checking until this guard is closed: cancellation may first land in
                    // the small gap between two outgoing COM calls. This branch never allocates.
                    if (deadline_.cancel.IsCancelled() || Clock::now() >= deadline_.end)
                        (void)::CoCancelCall(thread_, 0);
                }
                if (SUCCEEDED(initialized)) ::CoUninitialize();
            });
        } catch (...) {
            ::CoDisableCallCancellation(nullptr); enabled_ = false; throw;
        }
    }
    ~WmiCallGuard() {
        ::SetEvent(done_.Get());
        if (watchdog_.joinable()) watchdog_.join();
        if (enabled_) ::CoDisableCallCancellation(nullptr);
    }
private:
    Deadline deadline_;
    DWORD thread_;
    platform::UniqueHandle done_;
    bool enabled_ = false;
    std::thread watchdog_;
};

struct InternalDisplay {
    LUID sourceAdapter{};
    UINT32 sourceId = 0;
    LUID targetAdapter{};
    UINT32 targetId = 0;
    bool operator==(const InternalDisplay& other) const noexcept {
        return sourceAdapter.HighPart == other.sourceAdapter.HighPart && sourceAdapter.LowPart == other.sourceAdapter.LowPart &&
            sourceId == other.sourceId && targetAdapter.HighPart == other.targetAdapter.HighPart &&
            targetAdapter.LowPart == other.targetAdapter.LowPart && targetId == other.targetId;
    }
};

std::optional<InternalDisplay> SingleInternalDisplay() {
    UINT32 pathCount = 0, modeCount = 0;
    if (::GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS ||
        pathCount != 1 || modeCount > 128) return std::nullopt;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (::QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS ||
        pathCount != 1 || !paths.front().targetInfo.targetAvailable) return std::nullopt;
    const auto technology = paths.front().targetInfo.outputTechnology;
    if (technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL && technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS &&
        technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED && technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED)
        return std::nullopt;
    return InternalDisplay{paths.front().sourceInfo.adapterId, paths.front().sourceInfo.id,
        paths.front().targetInfo.adapterId, paths.front().targetInfo.id};
}

Result<winrt::com_ptr<IWbemServices>> ConnectWmi(const Deadline& deadline) {
    if (auto checked = deadline.Check(); !checked) return checked.error();
    auto locator = winrt::create_instance<IWbemLocator>(CLSID_WbemLocator, CLSCTX_INPROC_SERVER);
    Bstr name(L"ROOT\\WMI");
    winrt::com_ptr<IWbemServices> services;
    auto result = locator->ConnectServer(name.value, nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT,
        nullptr, nullptr, services.put());
    if (auto checked = deadline.Check(); !checked) return checked.error();
    if (FAILED(result)) return ApiError(result, "Display brightness provider unavailable");
    result = ::CoSetProxyBlanket(services.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(result)) return ApiError(result, "Display brightness provider access failed");
    return services;
}

Result<std::vector<winrt::com_ptr<IWbemClassObject>>> QueryWmi(IWbemServices* services, const wchar_t* query,
                                                            const Deadline& deadline) {
    if (auto checked = deadline.Check(); !checked) return checked.error();
    Bstr language(L"WQL"), statement(query);
    winrt::com_ptr<IEnumWbemClassObject> enumerator;
    const auto result = services->ExecQuery(language.value, statement.value,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, enumerator.put());
    if (FAILED(result)) return ApiError(result, "Display brightness query unavailable");
    std::vector<winrt::com_ptr<IWbemClassObject>> rows;
    for (;;) {
        if (auto checked = deadline.Check(); !checked) return checked.error();
        ULONG count = 0;
        winrt::com_ptr<IWbemClassObject> row;
        const auto status = enumerator->Next(50, 1, row.put(), &count);
        if (FAILED(status)) return ApiError(status, "Display brightness query failed");
        if (count == 1) {
            rows.push_back(std::move(row));
            if (rows.size() > 32) return MakeError(ErrorCode::Unavailable, "Display brightness target is ambiguous");
        }
        if (status == WBEM_S_FALSE) break;
    }
    return rows;
}

std::optional<unsigned> UnsignedValue(const VARIANT& value) {
    if (value.vt == VT_UI1) return value.bVal;
    if (value.vt == VT_UI2) return value.uiVal;
    if (value.vt == VT_UI4) return value.ulVal;
    if (value.vt == VT_I2 && value.iVal >= 0) return static_cast<unsigned>(value.iVal);
    if (value.vt == VT_I4 && value.lVal >= 0) return static_cast<unsigned>(value.lVal);
    return std::nullopt;
}

std::wstring StringProperty(IWbemClassObject* object, const wchar_t* name) {
    Variant property;
    if (FAILED(object->Get(name, 0, &property.value, nullptr, nullptr)) || property.value.vt != VT_BSTR || !property.value.bstrVal)
        return {};
    const auto size = ::SysStringLen(property.value.bstrVal);
    return size <= 2048 ? std::wstring(property.value.bstrVal, size) : std::wstring();
}

struct BrightnessTarget {
    std::wstring instance;
    std::wstring methodPath;
    int percent = 0;
    std::vector<int> levels;
};

Result<BrightnessTarget> ReadTarget(IWbemServices* services, const Deadline& deadline) {
    auto rows = QueryWmi(services, L"SELECT * FROM WmiMonitorBrightness WHERE Active=TRUE", deadline);
    if (!rows) return rows.error();
    if (rows->size() != 1) return MakeError(ErrorCode::Unavailable, "Primary internal display brightness unavailable");
    BrightnessTarget target;
    auto* row = rows->front().get();
    target.instance = StringProperty(row, L"InstanceName");
    Variant brightness, levels;
    if (target.instance.empty() || FAILED(row->Get(L"CurrentBrightness", 0, &brightness.value, nullptr, nullptr)))
        return MakeError(ErrorCode::Unavailable, "Display brightness reading unavailable");
    const auto current = UnsignedValue(brightness.value);
    if (!current || *current > 100) return MakeError(ErrorCode::Unavailable, "Display brightness reading unavailable");
    target.percent = static_cast<int>(*current);
    if (SUCCEEDED(row->Get(L"Level", 0, &levels.value, nullptr, nullptr)) &&
        levels.value.vt == (VT_ARRAY | VT_UI1) && levels.value.parray) {
        LONG first = 0, last = -1;
        if (SUCCEEDED(::SafeArrayGetLBound(levels.value.parray, 1, &first)) &&
            SUCCEEDED(::SafeArrayGetUBound(levels.value.parray, 1, &last)) &&
            first >= 0 && last >= first && last - first < 256) {
            for (LONG index = first; index <= last; ++index) {
                BYTE value = 0;
                if (SUCCEEDED(::SafeArrayGetElement(levels.value.parray, &index, &value)) && value <= 100)
                    target.levels.push_back(value);
            }
        }
    }
    std::sort(target.levels.begin(), target.levels.end());
    target.levels.erase(std::unique(target.levels.begin(), target.levels.end()), target.levels.end());
    auto methods = QueryWmi(services, L"SELECT * FROM WmiMonitorBrightnessMethods WHERE Active=TRUE", deadline);
    if (!methods) {
        if (auto checked = deadline.Check(); !checked) return checked.error();
        return target; // a real reading can still be displayed without an available write method
    }
    for (const auto& method : *methods) {
        if (StringProperty(method.get(), L"InstanceName") != target.instance) continue;
        if (!target.methodPath.empty()) return MakeError(ErrorCode::Unavailable, "Display brightness target is ambiguous");
        target.methodPath = StringProperty(method.get(), L"__RELPATH");
    }
    return target;
}

Result<void> WriteTarget(IWbemServices* services, const BrightnessTarget& target, int percent, const Deadline& deadline) {
    if (target.methodPath.empty() || target.levels.empty())
        return MakeError(ErrorCode::Unsupported, "Display brightness adjustment unavailable");
    if (auto checked = deadline.Check(); !checked) return checked;
    Bstr className(L"WmiMonitorBrightnessMethods"), methodName(L"WmiSetBrightness"), objectPath(target.methodPath.c_str());
    winrt::com_ptr<IWbemClassObject> type, definition, input;
    winrt::check_hresult(services->GetObject(className.value, 0, nullptr, type.put(), nullptr));
    winrt::check_hresult(type->GetMethod(methodName.value, 0, definition.put(), nullptr));
    winrt::check_hresult(definition->SpawnInstance(0, input.put()));
    Variant duration, value;
    duration.value.vt = VT_I4; duration.value.lVal = 0; // immediate transition, not a wait timeout
    value.value.vt = VT_UI1; value.value.bVal = static_cast<BYTE>(percent);
    winrt::check_hresult(input->Put(L"Timeout", 0, &duration.value, 0));
    winrt::check_hresult(input->Put(L"Brightness", 0, &value.value, 0));
    if (auto checked = deadline.Check(); !checked) return checked;
    winrt::com_ptr<IWbemCallResult> pending;
    winrt::check_hresult(services->ExecMethod(objectPath.value, methodName.value, WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, input.get(), nullptr, pending.put()));
    for (;;) {
        if (auto checked = deadline.Check(); !checked) return checked;
        LONG status = 0;
        const auto waited = pending->GetCallStatus(50, &status);
        if (waited == WBEM_S_TIMEDOUT) continue;
        if (FAILED(waited)) return ApiError(waited, "Display brightness adjustment failed");
        if (FAILED(status)) return ApiError(status, "Display brightness adjustment failed");
        winrt::com_ptr<IWbemClassObject> output;
        winrt::check_hresult(pending->GetResultObject(0, output.put()));
        Variant returned;
        if (!output || FAILED(output->Get(L"ReturnValue", 0, &returned.value, nullptr, nullptr)))
            return MakeError(ErrorCode::Unavailable, "Display brightness result unavailable");
        const auto code = UnsignedValue(returned.value);
        if (!code || *code != 0) return MakeError(ErrorCode::Platform, "Display brightness adjustment rejected");
        return Ok();
    }
}
} // namespace

struct MediaBrightness::Impl {
    struct Session { std::uint64_t key; media::GlobalSystemMediaTransportControlsSession object; };
    std::mutex mutex;
    media::GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    std::vector<Session> sessions;
    std::optional<std::uint64_t> selected;
    bool explicitSelection = false;
    std::uint64_t nextKey = 1;

    Result<void> Refresh(const Deadline& deadline) {
        if (!manager) {
            auto requested = Await(media::GlobalSystemMediaTransportControlsSessionManager::RequestAsync(), deadline);
            if (!requested) return requested.error();
            manager = std::move(*requested);
        }
        if (auto checked = deadline.Check(); !checked) return checked;
        const auto current = manager.GetSessions();
        if (current.Size() > 32) return MakeError(ErrorCode::Unavailable, "Too many media sessions");
        std::vector<Session> fresh;
        for (const auto& session : current) {
            const auto old = std::find_if(sessions.begin(), sessions.end(), [&](const auto& entry) { return SameSession(entry.object, session); });
            if (old != sessions.end()) fresh.push_back(*old);
            else {
                if (nextKey == std::numeric_limits<std::uint64_t>::max()) return MakeError(ErrorCode::Unavailable, "Media session identity exhausted");
                fresh.push_back({nextKey++, session});
            }
        }
        sessions = std::move(fresh);
        if (selected && std::none_of(sessions.begin(), sessions.end(), [&](const auto& entry) { return entry.key == *selected; })) {
            selected.reset(); explicitSelection = false;
        }
        if (!explicitSelection) {
            selected.reset();
            const auto active = manager.GetCurrentSession();
            const auto found = std::find_if(sessions.begin(), sessions.end(), [&](const auto& entry) { return SameSession(entry.object, active); });
            if (found != sessions.end()) selected = found->key;
            else if (!sessions.empty()) selected = sessions.front().key;
        }
        return Ok();
    }
};

MediaBrightness::MediaBrightness() : impl_(std::make_unique<Impl>()) {}
MediaBrightness::~MediaBrightness() = default;

Result<MediaSnapshot> MediaBrightness::ReadMedia(CancellationToken cancel, std::chrono::milliseconds timeout) {
    try {
        MtaApartment apartment; if (auto checked = apartment.Check(); !checked) return checked.error();
        auto deadline = MakeDeadline(std::move(cancel), timeout); if (!deadline) return deadline.error();
        std::lock_guard lock(impl_->mutex);
        if (auto refreshed = impl_->Refresh(*deadline); !refreshed) return refreshed.error();
        MediaSnapshot snapshot; snapshot.selectedSession = impl_->selected;
        for (const auto& entry : impl_->sessions) {
            auto properties = Await(entry.object.TryGetMediaPropertiesAsync(), *deadline);
            if (!properties) return properties.error();
            const auto playback = entry.object.GetPlaybackInfo();
            const auto controls = playback.Controls();
            MediaSession session; session.key = entry.key;
            session.title = std::wstring(properties->Title()); session.artist = std::wstring(properties->Artist());
            session.playback = Playback(playback.PlaybackStatus());
            session.canTogglePlayPause = controls.IsPlayPauseToggleEnabled();
            session.canNext = controls.IsNextEnabled(); session.canPrevious = controls.IsPreviousEnabled();
            snapshot.sessions.push_back(std::move(session));
        }
        return snapshot;
    } catch (const winrt::hresult_error& error) { return ApiError(error.code(), "Media sessions unavailable"); }
    catch (...) { return MakeError(ErrorCode::Internal, "Media sessions unavailable"); }
}

Result<void> MediaBrightness::SelectSession(std::uint64_t key) {
    try {
        MtaApartment apartment; if (auto checked = apartment.Check(); !checked) return checked;
        std::lock_guard lock(impl_->mutex);
        if (std::none_of(impl_->sessions.begin(), impl_->sessions.end(), [&](const auto& entry) { return entry.key == key; }))
            return MakeError(ErrorCode::NotFound, "Media session no longer available");
        impl_->selected = key; impl_->explicitSelection = true; return Ok();
    } catch (...) { return MakeError(ErrorCode::Internal, "Media session selection failed"); }
}

Result<void> MediaBrightness::ControlMedia(std::uint64_t key, MediaAction action, CancellationToken cancel,
                                         std::chrono::milliseconds timeout) {
    try {
        MtaApartment apartment; if (auto checked = apartment.Check(); !checked) return checked;
        auto deadline = MakeDeadline(std::move(cancel), timeout); if (!deadline) return deadline.error();
        std::lock_guard lock(impl_->mutex);
        if (auto refreshed = impl_->Refresh(*deadline); !refreshed) return refreshed;
        const auto found = std::find_if(impl_->sessions.begin(), impl_->sessions.end(), [&](const auto& entry) { return entry.key == key; });
        if (found == impl_->sessions.end()) return MakeError(ErrorCode::NotFound, "Media session no longer available");
        const auto controls = found->object.GetPlaybackInfo().Controls();
        if (auto checked = deadline->Check(); !checked) return checked;
        winrt::Windows::Foundation::IAsyncOperation<bool> operation{nullptr};
        if (action == MediaAction::TogglePlayPause && controls.IsPlayPauseToggleEnabled()) operation = found->object.TryTogglePlayPauseAsync();
        else if (action == MediaAction::Next && controls.IsNextEnabled()) operation = found->object.TrySkipNextAsync();
        else if (action == MediaAction::Previous && controls.IsPreviousEnabled()) operation = found->object.TrySkipPreviousAsync();
        else return MakeError(ErrorCode::Unsupported, "Media command is not supported by this session");
        auto result = Await(operation, *deadline);
        if (!result) return result.error();
        return *result ? Ok() : Result<void>(MakeError(ErrorCode::Unavailable, "Media session rejected the command"));
    } catch (const winrt::hresult_error& error) { return ApiError(error.code(), "Media command failed"); }
    catch (...) { return MakeError(ErrorCode::Internal, "Media command failed"); }
}

Result<BrightnessSnapshot> MediaBrightness::ReadBrightness(CancellationToken cancel, std::chrono::milliseconds timeout) {
    try {
        MtaApartment apartment; if (auto checked = apartment.Check(); !checked) return checked.error();
        auto deadline = MakeDeadline(std::move(cancel), timeout); if (!deadline) return deadline.error();
        if (!SingleInternalDisplay()) return MakeError(ErrorCode::Unavailable, "Primary internal display brightness unavailable");
        WmiCallGuard guard(*deadline);
        auto services = ConnectWmi(*deadline); if (!services) return services.error();
        auto target = ReadTarget(services->get(), *deadline); if (!target) return target.error();
        return BrightnessSnapshot{target->percent, !target->methodPath.empty() && !target->levels.empty()};
    } catch (const winrt::hresult_error& error) { return ApiError(error.code(), "Display brightness unavailable"); }
    catch (...) { return MakeError(ErrorCode::Internal, "Display brightness unavailable"); }
}

Result<BrightnessSnapshot> MediaBrightness::SetBrightness(int percent, CancellationToken cancel, std::chrono::milliseconds timeout) {
    if (percent < 0 || percent > 100) return MakeError(ErrorCode::InvalidArgument, "Invalid display brightness percentage");
    try {
        MtaApartment apartment; if (auto checked = apartment.Check(); !checked) return checked.error();
        auto deadline = MakeDeadline(std::move(cancel), timeout); if (!deadline) return deadline.error();
        const auto display = SingleInternalDisplay();
        if (!display) return MakeError(ErrorCode::Unavailable, "Primary internal display brightness unavailable");
        WmiCallGuard guard(*deadline);
        auto services = ConnectWmi(*deadline); if (!services) return services.error();
        auto target = ReadTarget(services->get(), *deadline); if (!target) return target.error();
        if (target->levels.empty()) return MakeError(ErrorCode::Unsupported, "Display brightness adjustment unavailable");
        const auto nearest = *std::min_element(target->levels.begin(), target->levels.end(), [percent](int a, int b) {
            return std::abs(a - percent) < std::abs(b - percent);
        });
        // Recheck topology immediately before mutation; never redirect a stale request to another display.
        if (SingleInternalDisplay() != display) return MakeError(ErrorCode::Unavailable, "Display topology changed");
        if (auto written = WriteTarget(services->get(), *target, nearest, *deadline); !written) return written.error();
        auto observed = ReadTarget(services->get(), *deadline); if (!observed) return observed.error();
        if (observed->instance != target->instance || SingleInternalDisplay() != display)
            return MakeError(ErrorCode::Unavailable, "Display topology changed");
        return BrightnessSnapshot{observed->percent, !observed->methodPath.empty() && !observed->levels.empty()};
    } catch (const winrt::hresult_error& error) { return ApiError(error.code(), "Display brightness adjustment failed"); }
    catch (...) { return MakeError(ErrorCode::Internal, "Display brightness adjustment failed"); }
}

} // namespace aether::controlcenter
