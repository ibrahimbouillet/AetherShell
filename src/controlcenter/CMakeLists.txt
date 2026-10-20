# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_module_library(aether_controlcenter LINK aether_system_controls aether_media_controls aether_ui aether_accessibility aether_status
    aether_status_audio aether_graphics aether_platform aether_diagnostics aether_core
    user32 shell32 windowsapp wbemuuid oleaut32 ole32)
aether_add_module_tests(controlcenter LINK aether_controlcenter)
