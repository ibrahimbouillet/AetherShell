# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# Minimal, dependency-free test framework (see AetherTest.h). Each module's
# tests compile into one executable registered with CTest.
add_library(aether_test_main STATIC AetherTest.h AetherTestMain.cpp)
aether_apply_compile_options(aether_test_main)
target_include_directories(aether_test_main PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(aether_test_main PROPERTIES FOLDER "tests")
