# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_status: system status providers for the Top Status Bar (clock, battery, input language,
# network) plus the pure formatting and mapping helpers they are built on. The audio providers live
# in src/status/audio (module status_audio) and are not part of this library.
aether_add_module_library(aether_status
    LINK aether_core aether_diagnostics aether_platform
         windowsapp user32 advapi32 shlwapi ole32)
aether_add_module_tests(status LINK aether_status)

if(AETHER_BUILD_TESTS)
    aether_add_test_executable(aether_status_runtime_shutdown_tests
        SOURCES "${PROJECT_SOURCE_DIR}/tests/integration/status/shutdown/RuntimeShutdownTests.cpp"
        LINK aether_status LABELS "integration;status")
endif()
