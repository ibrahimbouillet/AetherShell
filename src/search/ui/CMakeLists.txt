# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_module_library(aether_search_ui LINK aether_ui aether_accessibility aether_search aether_applications comctl32 oleacc uuid)
aether_add_module_tests(search_ui LINK aether_search_ui)
