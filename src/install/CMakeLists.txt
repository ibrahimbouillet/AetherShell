# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_module_library(aether_install
    LINK aether_core aether_diagnostics aether_platform aether_updates aether_settings
         shell32 ole32 advapi32 user32 bcrypt runtimeobject)
aether_add_module_tests(install LINK aether_install)
if(AETHER_BUILD_TESTS)
    include("${CMAKE_CURRENT_SOURCE_DIR}/TestPayloads.cmake")
endif()
