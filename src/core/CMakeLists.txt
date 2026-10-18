# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_core: platform-neutral building blocks (results, JSON, threading,
# cancellation, signals, file helpers). No UI and no module-specific logic.
aether_add_module_library(aether_core LINK ole32)
aether_add_module_tests(core LINK aether_core)
