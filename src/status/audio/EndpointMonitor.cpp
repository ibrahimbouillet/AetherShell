// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/audio/EndpointMonitor.h"

#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "platform/windows/Win32Error.h"
#include "status/audio/MtaWorker.h"

#include <unknwn.h>
// After unknwn.h, so winrt::implements also accepts classic COM interfaces.
#include <winrt/base.h>

#include <audioclient.h>
#include <endpointvolume.h>
#include <propidl.h>
#include <propsys.h>

#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <mutex>
#include <optional>
#include <utility>

// PKEY_Device_FriendlyName is declared with DEFINE_PROPERTYKEY, which only emits a definition when
// INITGUID is defined (selectany, so another definition elsewhere is harmless). These three includes
// must stay last so INITGUID does not leak into the headers above.
#include <initguid.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>

namespace aether::status::audio {
namespace detail {
namespace {

constexpr std::string_view kLogComponent = "status.audio";

using Operation = std::function<Result<void>()>;

// "context: system message (0x88890004)". Core Audio's AUDCLNT_E_* codes have no system message.
std::string DescribeFailure(HRESULT hr, std::string_view context) {
    return std::format("{} (0x{:08X})", platform::ErrorFromHResult(hr, context).message,
                       static_cast<std::uint32_t>(hr));
}

class CoTaskMemString {
public:
    CoTaskMemString() = default;
    ~CoTaskMemString() { ::CoTaskMemFree(value_); }
    CoTaskMemString(const CoTaskMemString&) = delete;
    CoTaskMemString& operator=(const CoTaskMemString&) = delete;
    [[nodiscard]] LPWSTR* Put() noexcept { return &value_; }
    [[nodiscard]] LPCWSTR Get() const noexcept { return value_; }

private:
    LPWSTR value_ = nullptr;
};

class PropVariantValue {
public:
    PropVariantValue() noexcept { PropVariantInit(&value_); }
    ~PropVariantValue() { ::PropVariantClear(&value_); }
    PropVariantValue(const PropVariantValue&) = delete;
    PropVariantValue& operator=(const PropVariantValue&) = delete;
    [[nodiscard]] PROPVARIANT* Put() noexcept { return &value_; }
    [[nodiscard]] const PROPVARIANT& Get() const noexcept { return value_; }

private:
    PROPVARIANT value_;
};

// Reads PKEY_Device_FriendlyName ("Speakers (Realtek(R) Audio)"). S_FALSE: the endpoint has no
// friendly name, `name` stays empty.
HRESULT ReadFriendlyName(IMMDevice& device, std::wstring& name) noexcept {
    name.clear();
    winrt::com_ptr<IPropertyStore> store;
    HRESULT hr = device.OpenPropertyStore(STGM_READ, store.put());
    if (FAILED(hr)) return hr;
    PropVariantValue value;
    hr = store->GetValue(PKEY_Device_FriendlyName, value.Put());
    if (FAILED(hr)) return hr;
    if (value.Get().vt != VT_LPWSTR || value.Get().pwszVal == nullptr) return S_FALSE;
    try {
        name = value.Get().pwszVal;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

}  // namespace

// All members except `worker_` and `options_` are touched only on the COM thread.
class MonitorCore {
public:
    explicit MonitorCore(EndpointMonitorOptions options) : options_(std::move(options)) {}
    ~MonitorCore() { worker_.Stop(); }
    MonitorCore(const MonitorCore&) = delete;
    MonitorCore& operator=(const MonitorCore&) = delete;

    Result<void> Start(EndpointMonitor::ReadingSink sink);
    // `finalTask` runs Shutdown() on the COM thread and keeps this object alive until it has.
    void Stop(MtaWorker::Task finalTask) noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsMonitorThread() const noexcept { return worker_.IsWorkerThread(); }

    void Submit(Operation operation, ControlCompletion completion);
    Result<void> SubmitAndWait(Operation operation, std::chrono::milliseconds timeout);

    // Control operations (COM thread).
    Result<void> ApplyVolume(float level);
    Result<void> ApplyMute(bool muted);
    Result<void> ApplyToggleMute();

    // Notification handlers (COM thread). `clientId`/`generation` identify the callback object that
    // produced the event, so late events from a released one are ignored.
    void OnDefaultDeviceChanged(std::uint64_t clientId);
    void OnDeviceChanged(std::uint64_t clientId, const std::wstring& deviceId);
    void OnFriendlyNameChanged(std::uint64_t clientId, const std::wstring& deviceId);
    void OnVolumeNotification(std::uint64_t generation, float level, bool muted, ChangeOrigin origin);

    [[nodiscard]] const std::string& Label() const noexcept { return options_.label; }

private:
    void BindNoThrow() noexcept;
    void Bind();
    void Fail(HRESULT hr, std::string_view context);
    void SetUnbound(EndpointBinding binding, std::string detail);
    void ReleaseEndpoint() noexcept;
    void ReleaseEnumerator() noexcept;
    void RequestRebind();
    void ScheduleRetry();
    void ScheduleHealthCheck();
    void HealthCheck();
    void Publish(ChangeOrigin origin);
    Result<void> RunOperation(const Operation& operation) noexcept;
    Result<void> RequireBound() const;
    Error ControlFailure(HRESULT hr, std::string_view context);

    const EndpointMonitorOptions options_;
    MtaWorker worker_;

    EndpointMonitor::ReadingSink sink_;
    winrt::com_ptr<IMMDeviceEnumerator> enumerator_;
    winrt::com_ptr<IMMNotificationClient> notificationClient_;
    std::uint64_t clientId_ = 0;
    winrt::com_ptr<IMMDevice> device_;
    std::wstring deviceId_;
    std::wstring lastLoggedDeviceId_;
    winrt::com_ptr<IAudioEndpointVolume> endpointVolume_;
    winrt::com_ptr<IAudioEndpointVolumeCallback> volumeCallback_;
    std::uint64_t generation_ = 0;
    EndpointReading reading_;
    std::string nameDetail_;  // independent of transient volume/mute read failures
    bool rebindQueued_ = false;
    bool retryScheduled_ = false;
    bool healthCheckScheduled_ = false;
    bool stopped_ = false;
};

namespace {

// IMMNotificationClient: device topology changes. Called by Windows on its own threads; it must not
// block or call back into the enumerator, so every method only posts a task to the COM thread.
class NotificationClient : public winrt::implements<NotificationClient, IMMNotificationClient> {
public:
    NotificationClient(MtaWorker::Poster poster, MonitorCore* core, std::uint64_t id, EDataFlow flow,
                       ERole role) noexcept
        : poster_(std::move(poster)), core_(core), id_(id), flow_(flow), role_(role) {}

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) noexcept override {
        if (flow != flow_ || role != role_) return S_OK;
        return Post([core = core_, id = id_] { core->OnDefaultDeviceChanged(id); });
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR deviceId, DWORD) noexcept override {
        return PostDeviceChanged(deviceId);
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR deviceId) noexcept override { return PostDeviceChanged(deviceId); }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR deviceId) noexcept override {
        return PostDeviceChanged(deviceId);
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR deviceId, const PROPERTYKEY key) noexcept override {
        // Fired for every property of every endpoint (often several times per second while a device
        // is being configured); only a rename of the bound endpoint matters here.
        if (deviceId == nullptr || !IsEqualPropertyKey(key, PKEY_Device_FriendlyName)) return S_OK;
        try {
            return Post([core = core_, id = id_, device = std::wstring(deviceId)] {
                core->OnFriendlyNameChanged(id, device);
            });
        } catch (...) {
            return S_OK;
        }
    }

private:
    HRESULT PostDeviceChanged(LPCWSTR deviceId) noexcept {
        if (deviceId == nullptr) return S_OK;
        try {
            return Post([core = core_, id = id_, device = std::wstring(deviceId)] { core->OnDeviceChanged(id, device); });
        } catch (...) {
            return S_OK;
        }
    }

    template <class Work>
    HRESULT Post(Work&& work) noexcept {
        try {
            poster_.Post(std::forward<Work>(work));
        } catch (...) {
            // Out of memory: the event is lost; the health check or the next event resynchronizes.
        }
        return S_OK;
    }

    MtaWorker::Poster poster_;
    MonitorCore* core_;  // only dereferenced by tasks running on the COM thread, while it is alive
    std::uint64_t id_;
    EDataFlow flow_;
    ERole role_;
};

// IAudioEndpointVolumeCallback: volume and mute of the bound endpoint, from any process.
class VolumeCallback : public winrt::implements<VolumeCallback, IAudioEndpointVolumeCallback> {
public:
    VolumeCallback(MtaWorker::Poster poster, MonitorCore* core, std::uint64_t generation) noexcept
        : poster_(std::move(poster)), core_(core), generation_(generation) {}

    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) noexcept override {
        if (data == nullptr) return E_POINTER;
        const float level = data->fMasterVolume;
        const bool muted = data->bMuted != FALSE;
        const ChangeOrigin origin = OriginFromEventContext(data->guidEventContext);
        try {
            poster_.Post([core = core_, generation = generation_, level, muted, origin] {
                core->OnVolumeNotification(generation, level, muted, origin);
            });
        } catch (...) {
            // Out of memory: the change is lost; the health check resynchronizes within its interval.
        }
        return S_OK;
    }

private:
    MtaWorker::Poster poster_;
    MonitorCore* core_;
    std::uint64_t generation_;
};

}  // namespace

Result<void> MonitorCore::Start(EndpointMonitor::ReadingSink sink) {
    sink_ = std::move(sink);  // before the thread exists, so no synchronization is needed
    if (auto started = worker_.Start(options_.threadName); !started.ok()) return started;
    if (!worker_.Post([this] { BindNoThrow(); })) {
        return MakeError(ErrorCode::Internal, "could not queue the first audio endpoint binding");
    }
    return Ok();
}

void MonitorCore::Stop(MtaWorker::Task finalTask) noexcept {
    // From the sink (COM thread): stop deliveries, timers and control requests right away. The COM
    // cleanup itself runs as `finalTask` once the current task has unwound.
    if (worker_.IsWorkerThread()) stopped_ = true;
    worker_.Stop(std::move(finalTask));
}

void MonitorCore::Shutdown() noexcept {
    stopped_ = true;
    // Unregister both callbacks before releasing the objects they are registered with.
    ReleaseEndpoint();
    ReleaseEnumerator();
    sink_ = nullptr;
}

void MonitorCore::BindNoThrow() noexcept {
    try {
        Bind();
    } catch (const std::exception&) {
        AETHER_LOG_ERROR(kLogComponent, "binding the default audio {} device threw an exception", options_.label);
        try {
            SetUnbound(EndpointBinding::Failed, "unexpected failure while binding");
        } catch (...) {
        }
    } catch (...) {
        // For example winrt::hresult_error, which does not derive from std::exception.
        AETHER_LOG_ERROR(kLogComponent, "binding the default audio {} device threw", options_.label);
        try {
            SetUnbound(EndpointBinding::Failed, "unexpected failure while binding");
        } catch (...) {
        }
    }
}

void MonitorCore::Bind() {
    if (stopped_) return;
    ReleaseEndpoint();

    if (const HRESULT com = worker_.ComStatus(); FAILED(com)) {
        return Fail(com, "could not enter the COM multithreaded apartment");
    }

    if (!enumerator_) {
        winrt::com_ptr<IMMDeviceEnumerator> enumerator;
        HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                        __uuidof(IMMDeviceEnumerator), enumerator.put_void());
        if (FAILED(hr)) return Fail(hr, "could not create the audio device enumerator");
        const std::uint64_t clientId = ++clientId_;
        auto client = winrt::make_self<NotificationClient>(worker_.MakePoster(), this, clientId, options_.flow,
                                                           options_.role);
        hr = enumerator->RegisterEndpointNotificationCallback(client.get());
        if (FAILED(hr)) return Fail(hr, "could not register for audio device notifications");
        enumerator_ = std::move(enumerator);
        notificationClient_ = client.as<IMMNotificationClient>();
    }

    winrt::com_ptr<IMMDevice> device;
    HRESULT hr = enumerator_->GetDefaultAudioEndpoint(options_.flow, options_.role, device.put());
    if (FAILED(hr)) return Fail(hr, "could not get the default audio endpoint");
    if (!device) return Fail(E_NOTFOUND, "the default audio endpoint is missing");

    CoTaskMemString rawId;
    hr = device->GetId(rawId.Put());
    if (FAILED(hr) || rawId.Get() == nullptr) return Fail(FAILED(hr) ? hr : E_UNEXPECTED, "could not read the endpoint id");
    std::wstring id = rawId.Get();

    DWORD state = 0;
    hr = device->GetState(&state);
    if (FAILED(hr)) return Fail(hr, "could not read the endpoint state");
    // GetDefaultAudioEndpoint only returns active endpoints; this guards the race with a state change.
    if (state != DEVICE_STATE_ACTIVE) return Fail(E_NOTFOUND, "the default audio endpoint is not active");

    std::string partial;  // what could not be read while the endpoint itself is usable
    std::wstring name;
    hr = ReadFriendlyName(*device, name);
    if (FAILED(hr)) {
        if (RequiresRebind(hr)) return Fail(hr, "could not read the endpoint name");
        partial = DescribeFailure(hr, "device name unavailable");
    }
    nameDetail_ = partial;

    winrt::com_ptr<IAudioEndpointVolume> volume;
    hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, volume.put_void());
    if (FAILED(hr)) return Fail(hr, "could not open the endpoint volume control");

    // Register before the first read so no change can slip between the read and the registration.
    const std::uint64_t generation = ++generation_;
    auto callback = winrt::make_self<VolumeCallback>(worker_.MakePoster(), this, generation);
    hr = volume->RegisterControlChangeNotify(callback.get());
    if (FAILED(hr)) return Fail(hr, "could not register for volume notifications");
    device_ = std::move(device);
    deviceId_ = std::move(id);
    endpointVolume_ = std::move(volume);
    volumeCallback_ = callback.as<IAudioEndpointVolumeCallback>();

    EndpointReading reading;
    reading.binding = EndpointBinding::Bound;
    reading.deviceName = std::move(name);

    float level = 0.0f;
    hr = endpointVolume_->GetMasterVolumeLevelScalar(&level);
    if (SUCCEEDED(hr)) {
        reading.volume = level;
    } else if (RequiresRebind(hr)) {
        return Fail(hr, "could not read the volume level");
    } else {
        if (!partial.empty()) partial += "; ";
        partial += DescribeFailure(hr, "volume level unavailable");
    }

    BOOL muted = FALSE;
    hr = endpointVolume_->GetMute(&muted);
    if (SUCCEEDED(hr)) {
        reading.muted = muted != FALSE;
    } else if (RequiresRebind(hr)) {
        return Fail(hr, "could not read the mute state");
    } else {
        if (!partial.empty()) partial += "; ";
        partial += DescribeFailure(hr, "mute state unavailable");
    }
    reading.detail = std::move(partial);

    if (deviceId_ != lastLoggedDeviceId_ || reading_.binding != EndpointBinding::Bound) {
        AETHER_LOG_INFO(kLogComponent, "default audio {} device bound", options_.label);
        // Friendly names can contain personal data; keep them in the UI snapshot, never in logs.
        lastLoggedDeviceId_ = deviceId_;
    }
    if (!reading.detail.empty()) {
        AETHER_LOG_WARN(kLogComponent, "default audio {} device partially readable: {}", options_.label,
                        reading.detail);
    }

    reading_ = std::move(reading);
    Publish(ChangeOrigin::External);
    ScheduleHealthCheck();
}

void MonitorCore::Fail(HRESULT hr, std::string_view context) {
    const EndpointBinding binding = ClassifyFailure(hr);
    std::string detail = binding == EndpointBinding::NoDevice
                             ? std::format("no active default audio {} device", options_.label)
                             : DescribeFailure(hr, context);
    SetUnbound(binding, std::move(detail));
}

void MonitorCore::SetUnbound(EndpointBinding binding, std::string detail) {
    ReleaseEndpoint();
    // The enumerator's connection to the audio service may be dead: rebuild it on the next attempt.
    if (binding == EndpointBinding::ServiceUnavailable) ReleaseEnumerator();

    if (reading_.binding != binding || reading_.detail != detail) {
        if (binding == EndpointBinding::NoDevice) {
            AETHER_LOG_INFO(kLogComponent, "{}", detail);
        } else {
            AETHER_LOG_WARN(kLogComponent, "default audio {} device {}: {}", options_.label, ToString(binding),
                            detail);
        }
    }
    reading_ = EndpointReading{};
    reading_.binding = binding;
    reading_.detail = std::move(detail);
    Publish(ChangeOrigin::External);

    // A missing device is reported by the notification client when it comes back; everything else
    // (service down, unexpected failure, no notification client) is retried on a timer as well.
    if (binding != EndpointBinding::NoDevice || !notificationClient_) ScheduleRetry();
}

void MonitorCore::ReleaseEndpoint() noexcept {
    if (endpointVolume_ && volumeCallback_) {
        const HRESULT hr = endpointVolume_->UnregisterControlChangeNotify(volumeCallback_.get());
        if (FAILED(hr)) {
            AETHER_LOG_DEBUG(kLogComponent, "UnregisterControlChangeNotify failed (0x{:08X})",
                             static_cast<std::uint32_t>(hr));
        }
    }
    volumeCallback_ = nullptr;
    endpointVolume_ = nullptr;
    device_ = nullptr;
    deviceId_.clear();
    nameDetail_.clear();
    ++generation_;  // notifications still in flight from the released endpoint are ignored
}

void MonitorCore::ReleaseEnumerator() noexcept {
    if (enumerator_ && notificationClient_) {
        const HRESULT hr = enumerator_->UnregisterEndpointNotificationCallback(notificationClient_.get());
        if (FAILED(hr)) {
            AETHER_LOG_DEBUG(kLogComponent, "UnregisterEndpointNotificationCallback failed (0x{:08X})",
                             static_cast<std::uint32_t>(hr));
        }
    }
    notificationClient_ = nullptr;
    enumerator_ = nullptr;
    ++clientId_;  // device events still in flight from the released client are ignored
}

void MonitorCore::RequestRebind() {
    // Notifications come in bursts (a Bluetooth headset connecting fires several); one rebind queued
    // behind them handles the whole burst.
    if (rebindQueued_ || stopped_) return;
    rebindQueued_ = worker_.Post([this] {
        rebindQueued_ = false;
        BindNoThrow();
    });
}

void MonitorCore::ScheduleRetry() {
    if (retryScheduled_ || stopped_) return;
    retryScheduled_ = worker_.PostAfter(options_.retryInterval, [this] {
        retryScheduled_ = false;
        if (!stopped_ && reading_.binding != EndpointBinding::Bound) BindNoThrow();
    });
}

void MonitorCore::ScheduleHealthCheck() {
    if (healthCheckScheduled_ || stopped_) return;
    healthCheckScheduled_ = worker_.PostAfter(options_.healthCheckInterval, [this] {
        healthCheckScheduled_ = false;
        HealthCheck();
    });
}

void MonitorCore::HealthCheck() {
    if (stopped_ || reading_.binding != EndpointBinding::Bound || !endpointVolume_) return;
    float level = 0.0f;
    const HRESULT levelResult = endpointVolume_->GetMasterVolumeLevelScalar(&level);
    BOOL muted = FALSE;
    const HRESULT muteResult = endpointVolume_->GetMute(&muted);
    if ((FAILED(levelResult) && RequiresRebind(levelResult)) || (FAILED(muteResult) && RequiresRebind(muteResult))) {
        AETHER_LOG_INFO(kLogComponent, "default audio {} device stopped answering; rebinding", options_.label);
        return BindNoThrow();
    }
    // Picks up a change whose notification was lost; deduplication drops the usual no-op.
    ApplyEndpointLevelReadings(reading_, levelResult, level, muteResult, muted != FALSE, nameDetail_);
    Publish(ChangeOrigin::External);
    ScheduleHealthCheck();
}

void MonitorCore::Publish(ChangeOrigin origin) {
    if (!stopped_ && sink_) sink_(reading_, origin);
}

void MonitorCore::OnDefaultDeviceChanged(std::uint64_t clientId) {
    if (clientId != clientId_) return;
    RequestRebind();
}

void MonitorCore::OnDeviceChanged(std::uint64_t clientId, const std::wstring& deviceId) {
    if (clientId != clientId_) return;
    // The bound endpoint changed state or vanished, or something appeared while nothing usable is
    // bound (no device, or a failed binding to retry).
    if (reading_.binding != EndpointBinding::Bound || deviceId == deviceId_) RequestRebind();
}

void MonitorCore::OnFriendlyNameChanged(std::uint64_t clientId, const std::wstring& deviceId) {
    if (clientId != clientId_ || reading_.binding != EndpointBinding::Bound || deviceId != deviceId_ || !device_) {
        return;
    }
    std::wstring name;
    const HRESULT hr = ReadFriendlyName(*device_, name);
    if (FAILED(hr)) {
        if (RequiresRebind(hr)) {
            RequestRebind();
        } else {
            reading_.deviceName.clear();
            nameDetail_ = DescribeFailure(hr, "device name unavailable");
            HealthCheck();
        }
        return;
    }
    reading_.deviceName = std::move(name);
    nameDetail_.clear();
    HealthCheck();
}

void MonitorCore::OnVolumeNotification(std::uint64_t generation, float level, bool muted, ChangeOrigin origin) {
    if (generation != generation_ || reading_.binding != EndpointBinding::Bound) return;
    reading_.volume = level;
    reading_.muted = muted;
    reading_.detail = nameDetail_;
    Publish(origin);
}

Result<void> MonitorCore::RequireBound() const {
    if (stopped_) return MakeError(ErrorCode::InvalidState, std::format("the audio {} provider is stopping", options_.label));
    if (reading_.binding == EndpointBinding::Bound && endpointVolume_) return Ok();
    if (reading_.binding == EndpointBinding::Initializing) {
        return MakeError(ErrorCode::Unavailable, std::format("the audio {} device is not bound yet", options_.label));
    }
    return MakeError(ErrorCode::Unavailable, reading_.detail.empty()
                                                 ? std::format("no usable default audio {} device", options_.label)
                                                 : reading_.detail);
}

Error MonitorCore::ControlFailure(HRESULT hr, std::string_view context) {
    Error error = platform::ErrorFromHResult(hr, context);
    if (RequiresRebind(hr)) {
        // The endpoint vanished or the service went away between the last event and this request.
        error.code = ErrorCode::Unavailable;
        RequestRebind();
    }
    return error;
}

Result<void> MonitorCore::ApplyVolume(float level) {
    if (auto bound = RequireBound(); !bound.ok()) return bound;
    const HRESULT hr = endpointVolume_->SetMasterVolumeLevelScalar(level, &AudioEventContext());
    if (FAILED(hr)) return ControlFailure(hr, std::format("could not set the audio {} volume", options_.label));
    return Ok();
}

Result<void> MonitorCore::ApplyMute(bool muted) {
    if (auto bound = RequireBound(); !bound.ok()) return bound;
    // S_FALSE means the endpoint already had this mute state; both are success.
    const HRESULT hr = endpointVolume_->SetMute(muted ? TRUE : FALSE, &AudioEventContext());
    if (FAILED(hr)) return ControlFailure(hr, std::format("could not change the audio {} mute state", options_.label));
    return Ok();
}

Result<void> MonitorCore::ApplyToggleMute() {
    if (auto bound = RequireBound(); !bound.ok()) return bound;
    // Read the endpoint itself rather than the last snapshot, which may lag behind a notification
    // that is still queued.
    BOOL muted = FALSE;
    const HRESULT hr = endpointVolume_->GetMute(&muted);
    if (FAILED(hr)) return ControlFailure(hr, std::format("could not read the audio {} mute state", options_.label));
    return ApplyMute(muted == FALSE);
}

Result<void> MonitorCore::RunOperation(const Operation& operation) noexcept {
    try {
        return operation();
    } catch (const std::exception& ex) {
        return MakeError(ErrorCode::Internal, std::string("audio control request failed: ") + ex.what());
    } catch (...) {
        return MakeError(ErrorCode::Internal, "audio control request failed");
    }
}

void MonitorCore::Submit(Operation operation, ControlCompletion completion) {
    auto once = std::make_shared<CompletionOnce>(std::move(completion));
    if (worker_.IsWorkerThread()) {
        // Called from the sink: run inline, waiting for ourselves would deadlock.
        once->Complete(RunOperation(operation));
        return;
    }
    const bool queued = worker_.Post([this, operation = std::move(operation), once] {
        once->Complete(RunOperation(operation));
    });
    if (!queued) {
        once->Complete(MakeError(ErrorCode::InvalidState, std::format("the audio {} provider is not running", options_.label)));
    }
}

Result<void> MonitorCore::SubmitAndWait(Operation operation, std::chrono::milliseconds timeout) {
    struct Waiter {
        std::mutex mutex;
        std::condition_variable done;
        std::optional<Result<void>> result;
    };
    auto waiter = std::make_shared<Waiter>();
    Submit(std::move(operation), [waiter](Result<void> result) {
        {
            std::lock_guard lock(waiter->mutex);
            waiter->result.emplace(std::move(result));
        }
        waiter->done.notify_all();
    });
    std::unique_lock lock(waiter->mutex);
    if (!waiter->done.wait_for(lock, timeout, [&] { return waiter->result.has_value(); })) {
        return MakeError(ErrorCode::Timeout, std::format("the audio {} device did not answer within {} ms",
                                                         options_.label, timeout.count()));
    }
    return std::move(*waiter->result);
}

}  // namespace detail

// ---------------------------------------------------------------------------------------------
// EndpointMonitor
// ---------------------------------------------------------------------------------------------

EndpointMonitor::EndpointMonitor(EndpointMonitorOptions options)
    : core_(std::make_shared<detail::MonitorCore>(std::move(options))) {}

EndpointMonitor::~EndpointMonitor() { Stop(); }

Result<void> EndpointMonitor::Start(ReadingSink sink) {
    try {
        return core_->Start(std::move(sink));
    } catch (const std::exception& ex) {
        return MakeError(ErrorCode::Internal, std::string("could not start the audio endpoint monitor: ") + ex.what());
    }
}

void EndpointMonitor::Stop() noexcept {
    // The final task owns a reference, so the core outlives a Stop() issued from its own thread.
    std::shared_ptr<detail::MonitorCore> core = core_;
    MtaWorker::Task finalTask;
    try {
        finalTask = [core] { core->Shutdown(); };
    } catch (...) {
        // Allocation failed: the worker still leaves the apartment; the COM objects are then released
        // with the core (on whichever thread destroys it), which COM's MTA tolerates.
    }
    core->Stop(std::move(finalTask));
}

bool EndpointMonitor::IsMonitorThread() const noexcept { return core_->IsMonitorThread(); }

namespace {

template <class Function>
Result<void> NoThrow(Function&& function) noexcept {
    try {
        return std::forward<Function>(function)();
    } catch (const std::exception& ex) {
        return MakeError(ErrorCode::Internal, std::string("audio control request failed: ") + ex.what());
    } catch (...) {
        return MakeError(ErrorCode::Internal, "audio control request failed");
    }
}

void CompleteNow(ControlCompletion completion, Result<void> result) noexcept {
    CompletionOnce(std::move(completion)).Complete(std::move(result));
}

}  // namespace

void EndpointMonitor::SetVolumeAsync(float level, ControlCompletion completion) {
    auto normalized = NormalizeRequestedVolume(level);
    if (!normalized.ok()) return CompleteNow(std::move(completion), normalized.error());
    try {
        detail::MonitorCore* core = core_.get();
        core->Submit([core, value = normalized.value()] { return core->ApplyVolume(value); }, std::move(completion));
    } catch (...) {
        // Submit only throws before taking ownership of `completion` when allocation fails.
        AETHER_LOG_ERROR("status.audio", "could not queue a volume request");
    }
}

void EndpointMonitor::SetMutedAsync(bool muted, ControlCompletion completion) {
    try {
        detail::MonitorCore* core = core_.get();
        core->Submit([core, muted] { return core->ApplyMute(muted); }, std::move(completion));
    } catch (...) {
        AETHER_LOG_ERROR("status.audio", "could not queue a mute request");
    }
}

void EndpointMonitor::ToggleMuteAsync(ControlCompletion completion) {
    try {
        detail::MonitorCore* core = core_.get();
        core->Submit([core] { return core->ApplyToggleMute(); }, std::move(completion));
    } catch (...) {
        AETHER_LOG_ERROR("status.audio", "could not queue a mute toggle request");
    }
}

Result<void> EndpointMonitor::SetVolume(float level, std::chrono::milliseconds timeout) {
    auto normalized = NormalizeRequestedVolume(level);
    if (!normalized.ok()) return normalized.error();
    return NoThrow([&] {
        detail::MonitorCore* core = core_.get();
        return core->SubmitAndWait([core, value = normalized.value()] { return core->ApplyVolume(value); }, timeout);
    });
}

Result<void> EndpointMonitor::SetMuted(bool muted, std::chrono::milliseconds timeout) {
    return NoThrow([&] {
        detail::MonitorCore* core = core_.get();
        return core->SubmitAndWait([core, muted] { return core->ApplyMute(muted); }, timeout);
    });
}

Result<void> EndpointMonitor::ToggleMute(std::chrono::milliseconds timeout) {
    return NoThrow([&] {
        detail::MonitorCore* core = core_.get();
        return core->SubmitAndWait([core] { return core->ApplyToggleMute(); }, timeout);
    });
}

}  // namespace aether::status::audio
