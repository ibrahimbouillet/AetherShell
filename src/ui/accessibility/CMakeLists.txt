# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_module_library(aether_accessibility
    LINK aether_core aether_diagnostics aether_platform uiautomationcore ole32 oleaut32 windowsapp)
aether_add_module_tests(accessibility LINK aether_accessibility)
