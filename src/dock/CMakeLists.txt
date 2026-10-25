# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_module_library(aether_dock LINK aether_ui aether_accessibility aether_applications aether_settings
    aether_graphics aether_platform aether_diagnostics aether_core user32 shell32 dwmapi)
aether_add_module_tests(dock LINK aether_dock)
