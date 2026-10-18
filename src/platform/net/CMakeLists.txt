# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_net: the production IHttpClient (WinHTTP, synchronous mode on worker
# threads) plus the pure URL, redirect and header policies it enforces.
aether_add_module_library(aether_net
    LINK aether_core aether_diagnostics aether_platform winhttp)
# ws2_32 is needed only by the in-process loopback HTTP server used by the integration tests.
aether_add_module_tests(net LINK aether_net ws2_32)
