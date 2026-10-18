# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_weather: optional current-weather provider for the Top Status Bar (Open-Meteo), its
# on-disk cache and the location search used by Settings. HTTP goes through the transport-neutral
# aether::net::IHttpClient contract (src/core/Http.h); the WinHTTP implementation (aether_net) is
# injected by the application and deliberately not linked here. Only the shared contract header
# status/StatusTypes.h is used; the aether_status library is not linked.
aether_add_module_library(aether_weather
    LINK aether_core aether_diagnostics aether_platform)
aether_add_module_tests(weather LINK aether_weather)
