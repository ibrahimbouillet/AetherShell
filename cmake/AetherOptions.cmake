# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# Project-wide build options. Every value here can be overridden on the CMake
# command line (for example by CI when producing a release build).

set(AETHER_VERSION_PRERELEASE "alpha.3" CACHE STRING
    "SemVer 2.0.0 prerelease identifier appended to the project version (empty for a stable build)")
if(AETHER_VERSION_PRERELEASE)
    set(AETHER_VERSION_FULL "${PROJECT_VERSION}-${AETHER_VERSION_PRERELEASE}")
    set(_aether_default_channel "prerelease")
else()
    set(AETHER_VERSION_FULL "${PROJECT_VERSION}")
    set(_aether_default_channel "stable")
endif()

# Trusted update source. These values identify the official repository of the
# project owner (verified with the GitHub CLI before being configured here).
set(AETHER_UPDATE_REPO_OWNER "ibrahimbouillet" CACHE STRING "GitHub owner of the official release repository")
set(AETHER_UPDATE_REPO_NAME "AetherShell" CACHE STRING "GitHub repository that publishes official releases")
set(AETHER_UPDATE_CHANNEL "${_aether_default_channel}" CACHE STRING "Default update channel (stable or prerelease)")
set_property(CACHE AETHER_UPDATE_CHANNEL PROPERTY STRINGS stable prerelease)

set(AETHER_MODULES "all" CACHE STRING
    "Semicolon-separated list of modules to configure; dependencies are added automatically. 'all' configures everything.")
option(AETHER_BUILD_TESTS "Build the automated test executables" ON)
option(AETHER_BUILD_SHOWCASE_RUNNER "Build the owner-started local cinematic capture runner" OFF)
option(AETHER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (enabled in CI)" OFF)

set(AETHER_SOURCE_REVISION "" CACHE STRING "Source revision embedded in the binaries (defaults to the git HEAD)")
set(AETHER_SOURCE_REVISION_RESOLVED "${AETHER_SOURCE_REVISION}")
if(NOT AETHER_SOURCE_REVISION_RESOLVED)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE _aether_rev
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _aether_rev_result)
        if(_aether_rev_result EQUAL 0)
            set(AETHER_SOURCE_REVISION_RESOLVED "${_aether_rev}")
        endif()
    endif()
endif()
if(NOT AETHER_SOURCE_REVISION_RESOLVED)
    set(AETHER_SOURCE_REVISION_RESOLVED "unknown")
endif()

string(TOLOWER "${MSVC_CXX_ARCHITECTURE_ID}" AETHER_TARGET_ARCH)
if(AETHER_TARGET_ARCH STREQUAL "x86" OR AETHER_TARGET_ARCH STREQUAL "")
    message(FATAL_ERROR "Aether Shell supports 64-bit targets only (x64, arm64). Detected: '${MSVC_CXX_ARCHITECTURE_ID}'.")
endif()

# Four-part file version used by the Windows version resource and manifest.
set(AETHER_FILE_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")
set(AETHER_FILE_VERSION_COMMAS "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
