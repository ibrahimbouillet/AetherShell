# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_platform: thin, documented Win32 adapters shared by every module
# (DPI, monitors, window helpers, UI dispatcher, system events, paths,
# single instance, message loop).
aether_add_module_library(aether_platform
    LINK aether_core aether_diagnostics
         user32 gdi32 shell32 shcore dwmapi advapi32 ole32 wtsapi32)
aether_add_module_tests(platform LINK aether_platform)
