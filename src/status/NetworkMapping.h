// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure mapping from Windows.Networking.Connectivity readings to a NetworkSnapshot. The raw types mirror
// the WinRT values so this header needs no WinRT include and every case can be unit tested.
//
// Privacy: the Wi-Fi SSID / connection profile name is deliberately never read. Windows 11 24H2 and later
// treat it as location data (it requires the location capability and consent), so the snapshot's
// profileName always stays empty.
#pragma once

#include "status/StatusTypes.h"

#include <cstdint>
#include <optional>
#include <string>

namespace aether::status {

// Mirrors Windows.Networking.Connectivity.NetworkConnectivityLevel.
enum class RawConnectivityLevel { None = 0, LocalAccess = 1, ConstrainedInternetAccess = 2, InternetAccess = 3 };

// IANA ifType values used by the mapping (https://www.iana.org/assignments/ianaiftype-mib).
inline constexpr std::uint32_t kIanaEthernet = 6;      // ethernetCsmacd
inline constexpr std::uint32_t kIanaWifi = 71;         // ieee80211
inline constexpr std::uint32_t kIanaMobile3gpp = 243;  // wwanPP
inline constexpr std::uint32_t kIanaMobile3gpp2 = 244; // wwanPP2

// One query of NetworkInformation::GetInternetConnectionProfile(). A std::nullopt member could not be
// read (the property threw); it is then reported as unknown, never guessed.
struct NetworkReadings {
    bool queryFailed = false;        // GetInternetConnectionProfile itself failed
    std::string error;               // first error met (UTF-8, no personal data)
    bool hasInternetProfile = false; // false: Windows has no connection profile with Internet routing
    std::optional<RawConnectivityLevel> connectivity;
    std::optional<bool> isWlan;
    std::optional<bool> isWwan;
    std::optional<std::uint32_t> ianaInterfaceType;
    std::optional<std::uint8_t> signalBars;  // only set when GetSignalBars() returned a value
};

// Rules:
//  * The query failed: state Error with the error as detail.
//  * No Internet connection profile: Available, kind None, connectivity None.
//  * Kind: WLAN profile -> WiFi, WWAN profile -> Cellular, else by IANA type (6 Ethernet, 71 WiFi,
//    243/244 Cellular, any other known type Other); unknown when nothing could be read.
//  * Connectivity: the profile's NetworkConnectivityLevel; unknown when it could not be read.
//  * Signal bars: only for WiFi/Cellular and only when Windows reported a value in 0..5.
//  * profileName: always empty (see the privacy note above).
[[nodiscard]] NetworkSnapshot MapNetworkReadings(const NetworkReadings& readings);

[[nodiscard]] ConnectionKind ClassifyConnection(const std::optional<bool>& isWlan, const std::optional<bool>& isWwan,
                                                const std::optional<std::uint32_t>& ianaInterfaceType) noexcept;

}  // namespace aether::status
