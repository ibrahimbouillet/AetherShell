// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "status/NetworkMapping.h"
#include "status/SnapshotUtilities.h"

using namespace aether::status;

namespace {

std::optional<std::uint8_t> Bars(int value) { return static_cast<std::uint8_t>(value); }

NetworkReadings Profile(RawConnectivityLevel level, bool wlan, bool wwan, std::optional<std::uint32_t> iana,
                        std::optional<std::uint8_t> bars = std::nullopt) {
    NetworkReadings readings;
    readings.hasInternetProfile = true;
    readings.connectivity = level;
    readings.isWlan = wlan;
    readings.isWwan = wwan;
    readings.ianaInterfaceType = iana;
    readings.signalBars = bars;
    return readings;
}

}  // namespace

AETHER_TEST("network mapping: no Internet profile is None/None") {
    const NetworkSnapshot snapshot = MapNetworkReadings(NetworkReadings{});
    CHECK_EQ(snapshot.state, ProviderState::Available);
    CHECK_EQ(snapshot.kind, ConnectionKind::None);
    CHECK_EQ(snapshot.connectivity, InternetConnectivity::None);
    CHECK_FALSE(snapshot.signalBars.has_value());
    CHECK(snapshot.profileName.empty());
}

AETHER_TEST("network mapping: Wi-Fi with signal bars, never a profile name") {
    const NetworkSnapshot snapshot =
        MapNetworkReadings(Profile(RawConnectivityLevel::InternetAccess, true, false, kIanaWifi, Bars(3)));
    CHECK_EQ(snapshot.state, ProviderState::Available);
    CHECK_EQ(snapshot.kind, ConnectionKind::WiFi);
    CHECK_EQ(snapshot.connectivity, InternetConnectivity::Internet);
    CHECK_EQ(snapshot.signalBars, Bars(3));
    CHECK(snapshot.profileName.empty());
    CHECK(snapshot.detail.empty());

    // IANA 71 identifies Wi-Fi even when the WLAN flag could not be read.
    NetworkReadings partial = Profile(RawConnectivityLevel::LocalAccess, false, false, kIanaWifi);
    partial.isWlan.reset();
    CHECK_EQ(MapNetworkReadings(partial).kind, ConnectionKind::WiFi);
    CHECK_EQ(MapNetworkReadings(partial).connectivity, InternetConnectivity::LocalOnly);

    // Bars are only reported when Windows gave a value, and only within 0..5.
    const auto wifi = [](std::optional<std::uint8_t> bars) {
        return MapNetworkReadings(Profile(RawConnectivityLevel::InternetAccess, true, false, kIanaWifi, bars));
    };
    CHECK_FALSE(wifi(std::nullopt).signalBars.has_value());
    CHECK_FALSE(wifi(Bars(9)).signalBars.has_value());
    CHECK_EQ(wifi(Bars(0)).signalBars, Bars(0));
    CHECK_EQ(wifi(Bars(5)).signalBars, Bars(5));
}

AETHER_TEST("network mapping: Ethernet, cellular and other adapters") {
    const NetworkSnapshot ethernet = MapNetworkReadings(
        Profile(RawConnectivityLevel::ConstrainedInternetAccess, false, false, kIanaEthernet, Bars(4)));
    CHECK_EQ(ethernet.kind, ConnectionKind::Ethernet);
    CHECK_EQ(ethernet.connectivity, InternetConnectivity::Constrained);
    CHECK_FALSE(ethernet.signalBars.has_value());  // wired: bars are meaningless

    const NetworkSnapshot wwan =
        MapNetworkReadings(Profile(RawConnectivityLevel::InternetAccess, false, true, 243, Bars(5)));
    CHECK_EQ(wwan.kind, ConnectionKind::Cellular);
    CHECK_EQ(wwan.signalBars, Bars(5));

    CHECK_EQ(MapNetworkReadings(Profile(RawConnectivityLevel::InternetAccess, false, false, kIanaMobile3gpp2)).kind,
             ConnectionKind::Cellular);
    CHECK_EQ(MapNetworkReadings(Profile(RawConnectivityLevel::InternetAccess, false, false, 53)).kind,
             ConnectionKind::Other);  // propVirtual (VPN)
    CHECK_EQ(MapNetworkReadings(Profile(RawConnectivityLevel::None, false, false, std::nullopt)).kind,
             ConnectionKind::Unknown);
    CHECK_EQ(MapNetworkReadings(Profile(RawConnectivityLevel::None, false, false, kIanaEthernet)).connectivity,
             InternetConnectivity::None);
}

AETHER_TEST("network mapping: classification precedence") {
    // The profile flags win over the adapter type (a WWAN profile over a virtual adapter).
    CHECK_EQ(ClassifyConnection(true, false, kIanaEthernet), ConnectionKind::WiFi);
    CHECK_EQ(ClassifyConnection(false, true, 131), ConnectionKind::Cellular);
    CHECK_EQ(ClassifyConnection(std::nullopt, std::nullopt, kIanaEthernet), ConnectionKind::Ethernet);
    CHECK_EQ(ClassifyConnection(std::nullopt, std::nullopt, std::nullopt), ConnectionKind::Unknown);
}

AETHER_TEST("network mapping: failures and unreadable properties") {
    NetworkReadings failed;
    failed.queryFailed = true;
    failed.error = "GetInternetConnectionProfile failed: RPC server unavailable (0x800706BA)";
    const NetworkSnapshot error = MapNetworkReadings(failed);
    CHECK_EQ(error.state, ProviderState::Error);
    CHECK_EQ(error.kind, ConnectionKind::Unknown);
    CHECK_EQ(error.connectivity, InternetConnectivity::Unknown);
    CHECK(error.detail.find("0x800706BA") != std::string::npos);

    NetworkReadings partial = Profile(RawConnectivityLevel::InternetAccess, false, false, kIanaEthernet);
    partial.connectivity.reset();
    partial.error = "Access is denied (0x80070005)";
    const NetworkSnapshot snapshot = MapNetworkReadings(partial);
    CHECK_EQ(snapshot.state, ProviderState::Available);
    CHECK_EQ(snapshot.kind, ConnectionKind::Ethernet);
    CHECK_EQ(snapshot.connectivity, InternetConnectivity::Unknown);
    CHECK(snapshot.detail.find("0x80070005") != std::string::npos);
}

AETHER_TEST("network mapping: descriptions never carry a profile name") {
    NetworkSnapshot snapshot =
        MapNetworkReadings(Profile(RawConnectivityLevel::InternetAccess, true, false, kIanaWifi, Bars(2)));
    snapshot.profileName = L"HomeNetwork-5G";  // even if a caller filled it in
    const std::string text = Describe(snapshot);
    CHECK(text.find("HomeNetwork") == std::string::npos);
    CHECK(text.find("WiFi") != std::string::npos);
    CHECK(text.find("bars=2") != std::string::npos);
}
