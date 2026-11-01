// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The Wi-Fi networks around this PC and connecting to one (D56, FR-008). Only an explicit request reads
// them: opening the Wi-Fi view, or choosing a network there. Network names are shown to the person and go
// nowhere else: they are never logged or sent, and neither is a password, which Windows keeps in its own
// profile once a connection succeeds. Worker only, on an MTA worker of its own: a scan takes seconds.
#pragma once

#include "core/Cancellation.h"
#include "core/Result.h"

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace aether::controlcenter {

enum class WiFiAccess { Allowed, Denied, NoAdapter, Unavailable };

struct WiFiNetwork {
    std::wstring ssid;
    int bars = 0;           // 0..4, as Windows reports it
    bool secured = true;
    bool personal = true;   // open or password based; an enterprise network is for Windows' own dialog
    bool connected = false;
};

struct WiFiList {
    WiFiAccess access = WiFiAccess::Allowed;
    std::vector<WiFiNetwork> networks;
};

enum class WiFiConnection { Connected, NeedsPassword, WrongPassword, NotInRange, TimedOut, Unsupported, Denied, Failed };

inline constexpr std::size_t kMaxWiFiNetworks = 32;

// Pure: one entry per name (access points of one network report separately), with its strongest signal;
// hidden networks left out; the connected one first, then by signal, then by name; a bounded list.
[[nodiscard]] std::vector<WiFiNetwork> MergeWiFiNetworks(std::vector<WiFiNetwork> found);
// Pure: the signal in words, for the panel and for screen readers.
[[nodiscard]] std::wstring WiFiSignalWord(int bars);

// `scan` asks the adapter for a fresh look, which takes seconds; otherwise this is what Windows last saw.
[[nodiscard]] WiFiList ReadWiFiNetworks(bool scan, CancellationToken cancellation = {},
                                        std::chrono::milliseconds timeout = std::chrono::seconds(15));
// An empty password tries the profile Windows may already have, or an open network.
[[nodiscard]] WiFiConnection ConnectWiFiNetwork(std::wstring_view ssid, const std::wstring& password,
                                                CancellationToken cancellation = {},
                                                std::chrono::milliseconds timeout = std::chrono::seconds(25));
[[nodiscard]] Result<void> DisconnectWiFi(CancellationToken cancellation = {});

}  // namespace aether::controlcenter
