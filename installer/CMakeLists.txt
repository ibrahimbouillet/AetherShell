# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
if(NOT TARGET AetherShellApp OR NOT TARGET AetherShell OR NOT TARGET AetherShellUpdater)
    message(FATAL_ERROR "The native setup requires app, launcher and updater targets.")
endif()
set(_payload_dir "${CMAKE_CURRENT_BINARY_DIR}/payload")
set(_payload_rc "${_payload_dir}/payload.rc")
add_custom_command(OUTPUT "${_payload_rc}"
    COMMAND "${CMAKE_COMMAND}"
        "-DOUTPUT_DIR=${_payload_dir}" "-DVERSION=${AETHER_VERSION_FULL}"
        "-DARCHITECTURE=${AETHER_TARGET_ARCH}"
        "-DAPP=$<TARGET_FILE:AetherShellApp>" "-DLAUNCHER=$<TARGET_FILE:AetherShell>"
        "-DUPDATER=$<TARGET_FILE:AetherShellUpdater>" "-DREPO=${PROJECT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/GeneratePayload.cmake"
    DEPENDS AetherShellApp AetherShell AetherShellUpdater
        "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/GeneratePayload.cmake" VERBATIM)
aether_add_executable(AetherShellSetup WIN32 SOURCES main.cpp "${_payload_rc}"
    LINK aether_install aether_settings aether_ui_form aether_graphics aether_platform aether_core comctl32 shell32 ole32)
aether_add_version_resource(AetherShellSetup DESCRIPTION "Aether Shell per-user setup" INTERNAL_NAME "AetherShellSetup")
if(AETHER_BUILD_TESTS)
    aether_add_executable(AetherShellSetupTest WIN32 SOURCES main.cpp "${_payload_rc}"
        LINK aether_install aether_settings aether_ui_form aether_graphics aether_platform aether_core comctl32 shell32 ole32)
    target_compile_definitions(AetherShellSetupTest PRIVATE AETHER_SETUP_TEST_MODE=1)
endif()
aether_add_module_tests(installer LINK aether_install aether_ui_form aether_graphics)
if(TARGET aether_installer_integration_tests)
    add_dependencies(aether_installer_integration_tests AetherShellSetupTest)
    target_compile_definitions(aether_installer_integration_tests PRIVATE
        "AETHER_SETUP_TEST_EXE=L\"$<TARGET_FILE:AetherShellSetupTest>\"")
endif()
