# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_module_library(aether_topbar LINK aether_ui aether_accessibility aether_status
    aether_status_audio aether_weather aether_settings aether_graphics aether_platform
    aether_diagnostics aether_core user32 shell32)
aether_add_module_tests(topbar LINK aether_topbar)
