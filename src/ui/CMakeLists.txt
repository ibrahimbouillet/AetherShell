# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_ui: shared custom-drawn window infrastructure (SurfaceWindow, tooltips, popup menus).
aether_add_module_library(aether_ui LINK aether_graphics aether_platform aether_diagnostics aether_core user32 dwmapi comctl32)
aether_add_module_tests(ui LINK aether_ui)
