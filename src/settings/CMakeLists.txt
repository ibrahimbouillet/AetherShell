# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_module_library(aether_settings LINK aether_core aether_diagnostics aether_platform advapi32)
aether_add_module_tests(settings LINK aether_settings)
