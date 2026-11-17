# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_weather_ui (D78): the weather window - the forecast where the person is or for a city they
# search, its country map with temperatures and the location permission. Forecast and map data come
# from aether_weather; the host supplies the HTTP client and the home position.
aether_add_module_library(aether_weather_ui
    LINK aether_weather aether_ui aether_accessibility aether_graphics aether_platform aether_diagnostics aether_core
         shell32 comctl32)
aether_add_module_tests(weather_ui LINK aether_weather_ui)
foreach(_target aether_weather_ui_tests aether_weather_ui_integration_tests)
    if(TARGET ${_target})
        aether_add_world_map_resource(${_target})
    endif()
endforeach()
