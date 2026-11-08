# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_module_library(aether_search LINK aether_core aether_diagnostics aether_platform normaliz ole32 oleaut32)
aether_add_module_tests(search LINK aether_search)
