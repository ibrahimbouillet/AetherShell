# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_welcome: the optional handwritten greeting after sign-in and unlock (FR-041, ADR 0013).
aether_add_module_library(aether_welcome LINK aether_ui aether_settings aether_graphics aether_platform aether_diagnostics aether_core)
aether_add_module_tests(welcome LINK aether_welcome)
