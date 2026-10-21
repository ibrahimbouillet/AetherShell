# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_module_library(aether_weather_location LINK aether_core aether_platform windowsapp runtimeobject)
aether_add_module_tests(weather_location LINK aether_weather_location)
