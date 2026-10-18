# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_ui: shared custom-drawn window infrastructure (SurfaceWindow, tooltips).
aether_add_module_library(aether_ui LINK aether_graphics aether_platform aether_diagnostics aether_core user32 dwmapi)
aether_add_module_tests(ui LINK aether_ui)
