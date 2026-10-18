# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
cmake_minimum_required(VERSION 3.25)

project(AetherShell
    VERSION 0.2.0
    DESCRIPTION "A modular native Windows desktop interface with a floating application dock, system status bar, and original glass design."
    HOMEPAGE_URL "https://github.com/ibrahimbouillet/AetherShell"
    LANGUAGES C CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "Aether Shell is a native Windows application and must be built on Windows.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "Aether Shell requires the MSVC toolset (cl.exe or clang-cl).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: end users never need to install a Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AetherOptions)
include(AetherTargets)
include(AetherModules)

configure_file(src/core/BuildInfo.h.in "${AETHER_GENERATED_DIR}/aether/BuildInfo.h" @ONLY)
configure_file(resources/manifest/AetherShell.manifest.in "${AETHER_GENERATED_DIR}/AetherShell.manifest" @ONLY)

enable_testing()
if(AETHER_BUILD_TESTS)
    add_subdirectory(tests/framework)
endif()

aether_add_modules()

message(STATUS "Aether Shell ${AETHER_VERSION_FULL} (${AETHER_TARGET_ARCH}) modules: ${AETHER_ENABLED_MODULES}")
