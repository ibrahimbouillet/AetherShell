# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_module_library(aether_settings_ui LINK aether_ui aether_settings aether_updates aether_applications aether_weather comctl32 shell32)
aether_add_module_tests(settings_ui LINK aether_settings_ui)
