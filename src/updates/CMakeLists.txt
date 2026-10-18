# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_updates: discovers official releases on GitHub, authenticates the signed
# release manifest (ECDSA P-256, Windows CNG), downloads and verifies the installer
# and stages it for the separate updater executable. Nothing is installed or
# executed here. The HTTP transport is injected (core/Http.h), so this module does
# not link aether_net.
aether_add_module_library(aether_updates
    LINK aether_core aether_diagnostics aether_platform bcrypt crypt32 wintrust)
aether_add_module_tests(updates LINK aether_updates)
