// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "system_controls/WiFiNetworks.h"

#include <windows.h>

#include <winrt/Windows.Devices.WiFi.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <winrt/Windows.Security.Credentials.h>

#include <algorithm>
#include <optional>

namespace aether::controlcenter {
namespace {

namespace wifi = winrt::Windows::Devices::WiFi;
namespace connectivity = winrt::Windows::Networking::Connectivity;
using winrt::Windows::Foundation::AsyncStatus;
using Clock = std::chrono::steady_clock;

// Waits in short steps, so that quitting during a scan of several seconds is not held up by it.
template <class Operation>
bool Finished(const Operation& operation, const CancellationToken& cancellation, Clock::time_point deadline) {
    while (!cancellation.IsCancelled() && Clock::now() < deadline) {
        if (operation.wait_for(std::chrono::milliseconds(150)) != AsyncStatus::Started) return operation.Status() == AsyncStatus::Completed;
    }
    try { operation.Cancel(); } catch (...) {}
    return false;
}

std::optional<wifi::WiFiAdapter> FirstAdapter(const CancellationToken& cancellation, Clock::time_point deadline, WiFiAccess& access) {
    const auto request = wifi::WiFiAdapter::RequestAccessAsync();
    if (!Finished(request, cancellation, deadline)) { access = WiFiAccess::Unavailable; return std::nullopt; }
    if (request.GetResults() != wifi::WiFiAccessStatus::Allowed) { access = WiFiAccess::Denied; return std::nullopt; }
    const auto adapters = wifi::WiFiAdapter::FindAllAdaptersAsync();
    if (!Finished(adapters, cancellation, deadline)) { access = WiFiAccess::Unavailable; return std::nullopt; }
    const auto found = adapters.GetResults();
    if (found.Size() == 0) { access = WiFiAccess::NoAdapter; return std::nullopt; }
    access = WiFiAccess::Allowed;
    return found.GetAt(0);
}

bool Open(connectivity::NetworkAuthenticationType type) {
    return type == connectivity::NetworkAuthenticationType::None || type == connectivity::NetworkAuthenticationType::Open80211 ||
           type == connectivity::NetworkAuthenticationType::Owe;
}
bool Personal(connectivity::NetworkAuthenticationType type) {
    return Open(type) || type == connectivity::NetworkAuthenticationType::SharedKey80211 ||
           type == connectivity::NetworkAuthenticationType::WpaPsk || type == connectivity::NetworkAuthenticationType::RsnaPsk ||
           type == connectivity::NetworkAuthenticationType::Wpa3Sae;
}

std::wstring ConnectedSsid(const wifi::WiFiAdapter& adapter, const CancellationToken& cancellation, Clock::time_point deadline) {
    try {
        const auto profile = adapter.NetworkAdapter().GetConnectedProfileAsync();
        if (!Finished(profile, cancellation, deadline)) return {};
        const auto connected = profile.GetResults();
        if (!connected || !connected.IsWlanConnectionProfile()) return {};
        return std::wstring(connected.WlanConnectionProfileDetails().GetConnectedSsid());
    } catch (...) {
        return {};
    }
}

}  // namespace

std::vector<WiFiNetwork> MergeWiFiNetworks(std::vector<WiFiNetwork> found) {
    std::vector<WiFiNetwork> merged;
    for (auto& network : found) {
        if (network.ssid.empty()) continue;
        network.bars = std::clamp(network.bars, 0, 4);
        const auto same = std::find_if(merged.begin(), merged.end(), [&](const WiFiNetwork& other) { return other.ssid == network.ssid; });
        if (same == merged.end()) { merged.push_back(std::move(network)); continue; }
        same->bars = std::max(same->bars, network.bars);
        same->connected = same->connected || network.connected;
    }
    std::stable_sort(merged.begin(), merged.end(), [](const WiFiNetwork& a, const WiFiNetwork& b) {
        if (a.connected != b.connected) return a.connected;
        if (a.bars != b.bars) return a.bars > b.bars;
        return a.ssid < b.ssid;
    });
    if (merged.size() > kMaxWiFiNetworks) merged.resize(kMaxWiFiNetworks);
    return merged;
}

std::wstring WiFiSignalWord(int bars) {
    return bars >= 4 ? L"Excellent signal" : bars == 3 ? L"Good signal" : bars == 2 ? L"Fair signal" : L"Weak signal";
}

WiFiList ReadWiFiNetworks(bool scan, CancellationToken cancellation, std::chrono::milliseconds timeout) {
    WiFiList list;
    const auto deadline = Clock::now() + timeout;
    try {
        const auto adapter = FirstAdapter(cancellation, deadline, list.access);
        if (!adapter) return list;
        if (scan) {
            const auto scanning = adapter->ScanAsync();
            (void)Finished(scanning, cancellation, deadline);  // a failed scan still leaves what Windows saw last
        }
        const auto connected = ConnectedSsid(*adapter, cancellation, deadline);
        std::vector<WiFiNetwork> found;
        for (const auto& available : adapter->NetworkReport().AvailableNetworks()) {
            WiFiNetwork network;
            network.ssid = std::wstring(available.Ssid());
            network.bars = available.SignalBars();
            const auto type = available.SecuritySettings().NetworkAuthenticationType();
            network.secured = !Open(type);
            network.personal = Personal(type);
            network.connected = !connected.empty() && network.ssid == connected;
            found.push_back(std::move(network));
        }
        list.networks = MergeWiFiNetworks(std::move(found));
    } catch (...) {
        // Never copy a WinRT message here: Windows may put network or device names in it.
        list.access = WiFiAccess::Unavailable;
    }
    return list;
}

WiFiConnection ConnectWiFiNetwork(std::wstring_view ssid, const std::wstring& password, CancellationToken cancellation, std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    try {
        WiFiAccess access = WiFiAccess::Allowed;
        const auto adapter = FirstAdapter(cancellation, deadline, access);
        if (!adapter) return access == WiFiAccess::Denied ? WiFiConnection::Denied : WiFiConnection::Failed;
        std::optional<wifi::WiFiAvailableNetwork> best;
        for (const auto& available : adapter->NetworkReport().AvailableNetworks())
            if (std::wstring_view(available.Ssid()) == ssid && (!best || available.SignalBars() > best->SignalBars())) best = available;
        if (!best) return WiFiConnection::NotInRange;
        const bool open = Open(best->SecuritySettings().NetworkAuthenticationType());
        if (!Personal(best->SecuritySettings().NetworkAuthenticationType())) return WiFiConnection::Unsupported;
        winrt::Windows::Foundation::IAsyncOperation<wifi::WiFiConnectionResult> connecting{nullptr};
        if (password.empty() || open) {
            connecting = adapter->ConnectAsync(*best, wifi::WiFiReconnectionKind::Automatic);
        } else {
            winrt::Windows::Security::Credentials::PasswordCredential credential;
            credential.Password(winrt::hstring(password));
            connecting = adapter->ConnectAsync(*best, wifi::WiFiReconnectionKind::Automatic, credential);
        }
        if (!Finished(connecting, cancellation, deadline)) return WiFiConnection::TimedOut;
        switch (connecting.GetResults().ConnectionStatus()) {
            case wifi::WiFiConnectionStatus::Success: return WiFiConnection::Connected;
            case wifi::WiFiConnectionStatus::InvalidCredential: return password.empty() ? WiFiConnection::NeedsPassword : WiFiConnection::WrongPassword;
            case wifi::WiFiConnectionStatus::NetworkNotAvailable: return WiFiConnection::NotInRange;
            case wifi::WiFiConnectionStatus::Timeout: return WiFiConnection::TimedOut;
            case wifi::WiFiConnectionStatus::AccessRevoked: return WiFiConnection::Denied;
            case wifi::WiFiConnectionStatus::UnsupportedAuthenticationProtocol: return WiFiConnection::Unsupported;
            default:
                // A secured network without a saved profile can fail this generally without a password.
                return password.empty() && !open ? WiFiConnection::NeedsPassword : WiFiConnection::Failed;
        }
    } catch (...) {
        return WiFiConnection::Failed;
    }
}

Result<void> DisconnectWiFi(CancellationToken cancellation) {
    try {
        WiFiAccess access = WiFiAccess::Allowed;
        const auto adapter = FirstAdapter(cancellation, Clock::now() + std::chrono::seconds(10), access);
        if (!adapter) return MakeError(ErrorCode::Unavailable, "Wi-Fi is unavailable");
        adapter->Disconnect();
        return Ok();
    } catch (...) {
        return MakeError(ErrorCode::Platform, "Wi-Fi disconnection failed");
    }
}

}  // namespace aether::controlcenter
