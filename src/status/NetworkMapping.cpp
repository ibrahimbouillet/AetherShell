// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/NetworkMapping.h"

namespace aether::status {
namespace {

InternetConnectivity MapConnectivity(const std::optional<RawConnectivityLevel>& level) noexcept {
    if (!level) return InternetConnectivity::Unknown;
    switch (*level) {
        case RawConnectivityLevel::None: return InternetConnectivity::None;
        case RawConnectivityLevel::LocalAccess: return InternetConnectivity::LocalOnly;
        case RawConnectivityLevel::ConstrainedInternetAccess: return InternetConnectivity::Constrained;
        case RawConnectivityLevel::InternetAccess: return InternetConnectivity::Internet;
    }
    return InternetConnectivity::Unknown;  // a value added by a future Windows version
}

constexpr std::uint8_t kMaxSignalBars = 5;

}  // namespace

ConnectionKind ClassifyConnection(const std::optional<bool>& isWlan, const std::optional<bool>& isWwan,
                                  const std::optional<std::uint32_t>& ianaInterfaceType) noexcept {
    if (isWlan.value_or(false)) return ConnectionKind::WiFi;
    if (isWwan.value_or(false)) return ConnectionKind::Cellular;
    if (!ianaInterfaceType) return ConnectionKind::Unknown;
    switch (*ianaInterfaceType) {
        case kIanaEthernet: return ConnectionKind::Ethernet;
        case kIanaWifi: return ConnectionKind::WiFi;
        case kIanaMobile3gpp:
        case kIanaMobile3gpp2: return ConnectionKind::Cellular;
        default: return ConnectionKind::Other;  // VPN/tunnel, PPP, loopback, ...
    }
}

NetworkSnapshot MapNetworkReadings(const NetworkReadings& readings) {
    NetworkSnapshot snapshot;
    if (readings.queryFailed) {
        snapshot.state = ProviderState::Error;
        snapshot.detail = readings.error.empty() ? std::string("network query failed") : readings.error;
        return snapshot;
    }
    snapshot.state = ProviderState::Available;
    if (!readings.hasInternetProfile) {
        snapshot.kind = ConnectionKind::None;
        snapshot.connectivity = InternetConnectivity::None;
        return snapshot;
    }
    snapshot.kind = ClassifyConnection(readings.isWlan, readings.isWwan, readings.ianaInterfaceType);
    snapshot.connectivity = MapConnectivity(readings.connectivity);
    const bool wireless = snapshot.kind == ConnectionKind::WiFi || snapshot.kind == ConnectionKind::Cellular;
    if (wireless && readings.signalBars && *readings.signalBars <= kMaxSignalBars) {
        snapshot.signalBars = readings.signalBars;
    }
    // Partial reads are worth a note for diagnostics, without changing the state.
    if (!readings.error.empty()) snapshot.detail = "some properties could not be read: " + readings.error;
    return snapshot;
}

}  // namespace aether::status
