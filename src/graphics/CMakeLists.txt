# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_graphics: Direct3D/Direct2D/DirectWrite devices, Windows.UI.Composition
# surfaces, glass material, design tokens, text, bitmaps, frame pacing and
# animation primitives.
aether_add_module_library(aether_graphics
    LINK aether_core aether_diagnostics aether_platform
         d3d11 dxgi dxguid d2d1 dwrite windowscodecs dwmapi CoreMessaging windowsapp)

# D81: the Liquid shader is compiled with the build by the Windows SDK's fxc. Compiled by D3DCompile at the first Liquid
# frame, it held the UI thread for about half a second at every start (and again after a lost device), and loaded the
# shader compiler's 4 MB library at sign-in. Without fxc the renderer compiles it at run time, as before.
find_program(AETHER_FXC fxc)
if(AETHER_FXC)
    set(_liquid_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/graphics")
    set(_liquid_hlsl "${_liquid_dir}/LiquidShader.hlsl")
    set(_liquid_vs "${_liquid_dir}/LiquidShaderVS.h")
    set(_liquid_ps "${_liquid_dir}/LiquidShaderPS.h")
    add_custom_command(OUTPUT "${_liquid_vs}" "${_liquid_ps}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/LiquidShader.inc" "-DOUTPUT=${_liquid_hlsl}"
                -P "${PROJECT_SOURCE_DIR}/cmake/AetherHlslFromInc.cmake"
        COMMAND "${AETHER_FXC}" /nologo /T vs_4_0 /E VS /O3 /Vn kLiquidVertexShader /Fh "${_liquid_vs}" "${_liquid_hlsl}"
        COMMAND "${AETHER_FXC}" /nologo /T ps_4_0 /E PS /O3 /Vn kLiquidPixelShader /Fh "${_liquid_ps}" "${_liquid_hlsl}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/LiquidShader.inc" "${PROJECT_SOURCE_DIR}/cmake/AetherHlslFromInc.cmake"
        COMMENT "Compiling the Liquid shader"
        VERBATIM)
    target_sources(aether_graphics PRIVATE "${_liquid_vs}" "${_liquid_ps}")
    target_include_directories(aether_graphics PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(aether_graphics PRIVATE AETHER_LIQUID_PRECOMPILED=1)
else()
    message(STATUS "fxc not found: the Liquid shader is compiled at run time")
    target_link_libraries(aether_graphics PUBLIC d3dcompiler)
endif()

aether_add_module_tests(graphics LINK aether_graphics)
