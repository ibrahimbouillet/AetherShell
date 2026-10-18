# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_diagnostics: structured logging with rotation and diagnostic export.
aether_add_module_library(aether_diagnostics LINK aether_core)
aether_add_module_tests(diagnostics LINK aether_diagnostics)
