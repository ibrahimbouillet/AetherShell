# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# Helpers that give every Aether Shell target the same compiler settings.

set(AETHER_GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")

function(aether_apply_compile_options target)
    target_compile_options(${target} PRIVATE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /Zc:inline
        /EHsc /bigobj /sdl /guard:cf /Zi
        $<$<BOOL:${AETHER_WARNINGS_AS_ERRORS}>:/WX>)
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        WINRT_NO_SOURCE_LOCATION
        _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${AETHER_GENERATED_DIR}")
endfunction()

function(aether_apply_link_options target)
    target_link_options(${target} PRIVATE
        /DEBUG /guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA
        "/PDBALTPATH:$<TARGET_PDB_FILE_NAME:${target}>"
        $<$<STREQUAL:${AETHER_TARGET_ARCH},x64>:/CETCOMPAT>
        $<$<NOT:$<CONFIG:Debug>>:/OPT:REF /OPT:ICF /INCREMENTAL:NO>)
endfunction()

# aether_add_module_library(<target>
#     [DIRECTORY <dir>]          # defaults to the current source directory
#     [LINK <targets/libs>...]   # public dependencies
#     [EXCLUDE <file names>...]) # sources in the directory that must not be compiled
#
# Collects every .cpp/.h file directly inside the directory (non-recursive).
# A directory without .cpp files becomes an INTERFACE (header-only) library.
function(aether_add_module_library target)
    cmake_parse_arguments(ARG "" "DIRECTORY" "LINK;EXCLUDE" ${ARGN})
    if(NOT ARG_DIRECTORY)
        set(ARG_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    file(GLOB _cpp CONFIGURE_DEPENDS "${ARG_DIRECTORY}/*.cpp")
    file(GLOB _hdr CONFIGURE_DEPENDS "${ARG_DIRECTORY}/*.h")
    foreach(_excluded IN LISTS ARG_EXCLUDE)
        list(FILTER _cpp EXCLUDE REGEX "/${_excluded}$")
        list(FILTER _hdr EXCLUDE REGEX "/${_excluded}$")
    endforeach()
    if(_cpp)
        add_library(${target} STATIC ${_cpp} ${_hdr})
        aether_apply_compile_options(${target})
        target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/src" "${AETHER_GENERATED_DIR}")
        target_link_libraries(${target} PUBLIC ${ARG_LINK})
    else()
        add_library(${target} INTERFACE ${_hdr})
        target_include_directories(${target} INTERFACE "${PROJECT_SOURCE_DIR}/src" "${AETHER_GENERATED_DIR}")
        target_link_libraries(${target} INTERFACE ${ARG_LINK})
    endif()
    set_target_properties(${target} PROPERTIES FOLDER "modules")
endfunction()

# aether_add_executable(<target> [WIN32] SOURCES ... [LINK ...] [MANIFEST <file>])
function(aether_add_executable target)
    cmake_parse_arguments(ARG "WIN32" "MANIFEST" "SOURCES;LINK" ${ARGN})
    if(NOT ARG_MANIFEST)
        set(ARG_MANIFEST "${AETHER_GENERATED_DIR}/AetherShell.manifest")
    endif()
    if(ARG_WIN32)
        add_executable(${target} WIN32 ${ARG_SOURCES} "${ARG_MANIFEST}")
    else()
        add_executable(${target} ${ARG_SOURCES} "${ARG_MANIFEST}")
    endif()
    aether_apply_compile_options(${target})
    aether_apply_link_options(${target})
    target_link_libraries(${target} PRIVATE ${ARG_LINK})
endfunction()

# aether_add_version_resource(<target> DESCRIPTION <text> INTERNAL_NAME <name>)
# Embeds the shared VERSIONINFO (company, product, version, MIT copyright) into an executable.
function(aether_add_version_resource target)
    cmake_parse_arguments(ARG "" "DESCRIPTION;INTERNAL_NAME" "" ${ARGN})
    set(AETHER_RC_DESCRIPTION "${ARG_DESCRIPTION}")
    set(AETHER_RC_INTERNAL_NAME "${ARG_INTERNAL_NAME}")
    set(_rc "${AETHER_GENERATED_DIR}/version/${target}.rc")
    configure_file("${PROJECT_SOURCE_DIR}/resources/app/VersionInfo.rc.in" "${_rc}" @ONLY)
    target_sources(${target} PRIVATE "${_rc}")
endfunction()

# aether_add_test_executable(<target> SOURCES ... [LINK ...] [LABELS ...] [TIMEOUT <seconds>])
function(aether_add_test_executable target)
    cmake_parse_arguments(ARG "" "TIMEOUT" "SOURCES;LINK;LABELS" ${ARGN})
    if(NOT ARG_TIMEOUT)
        set(ARG_TIMEOUT 120)
    endif()
    aether_add_executable(${target} SOURCES ${ARG_SOURCES} LINK aether_test_main ${ARG_LINK})
    set_target_properties(${target} PROPERTIES FOLDER "tests")
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "${ARG_LABELS}" TIMEOUT ${ARG_TIMEOUT})
endfunction()

# aether_add_module_tests(<module> [LINK ...])
#
# Creates up to three test executables from conventional folders:
#   tests/unit/<module>/*.cpp         -> aether_<module>_tests              (labels: unit)
#   tests/integration/<module>/*.cpp  -> aether_<module>_integration_tests  (labels: integration, needs a Windows desktop)
#   tests/network/<module>/*.cpp      -> aether_<module>_network_tests      (labels: network, needs Internet access)
function(aether_add_module_tests module)
    if(NOT AETHER_BUILD_TESTS)
        return()
    endif()
    cmake_parse_arguments(ARG "" "" "LINK" ${ARGN})
    file(GLOB _unit CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/unit/${module}/*.cpp")
    if(_unit)
        aether_add_test_executable(aether_${module}_tests SOURCES ${_unit} LINK ${ARG_LINK} LABELS "unit;${module}")
    endif()
    file(GLOB _integration CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/integration/${module}/*.cpp")
    if(_integration)
        aether_add_test_executable(aether_${module}_integration_tests SOURCES ${_integration} LINK ${ARG_LINK}
            LABELS "integration;${module}" TIMEOUT 300)
    endif()
    file(GLOB _network CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/network/${module}/*.cpp")
    if(_network)
        aether_add_test_executable(aether_${module}_network_tests SOURCES ${_network} LINK ${ARG_LINK}
            LABELS "network;${module}" TIMEOUT 300)
    endif()
endfunction()
