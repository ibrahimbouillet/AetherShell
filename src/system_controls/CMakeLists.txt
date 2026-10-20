# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_module_library(aether_system_controls LINK aether_core aether_platform aether_status windowsapp runtimeobject)
aether_add_module_tests(system_controls LINK aether_system_controls)
