# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_module_library(aether_app EXCLUDE main.cpp
    LINK aether_core aether_diagnostics aether_platform aether_net aether_graphics aether_ui
         aether_status aether_status_audio aether_weather aether_weather_location aether_applications aether_search
         aether_settings aether_updates aether_install aether_dock aether_topbar aether_search_ui
         aether_settings_ui aether_ui_form aether_controlcenter dbghelp comctl32 shell32)
aether_add_executable(AetherShellApp WIN32 SOURCES main.cpp LINK aether_app)
aether_add_version_resource(AetherShellApp DESCRIPTION "Aether Shell native desktop interface" INTERNAL_NAME "AetherShellApp")
aether_add_module_tests(app LINK aether_app)
if(AETHER_BUILD_TESTS)
    # Own synthetic lifetime fixture only: not installed or included in release payloads.
    aether_add_executable(aether_window_corner_parent_fixture WIN32
        SOURCES "${PROJECT_SOURCE_DIR}/tests/fixtures/app/WindowCornerLifetimeParent.cpp" LINK shell32)
    add_dependencies(aether_app_integration_tests aether_window_corner_parent_fixture)
endif()
