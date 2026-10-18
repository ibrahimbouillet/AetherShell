# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_graphics: Direct3D/Direct2D/DirectWrite devices, Windows.UI.Composition
# surfaces, glass material, design tokens, text, bitmaps, frame pacing and
# animation primitives.
aether_add_module_library(aether_graphics
    LINK aether_core aether_diagnostics aether_platform
         d3d11 dxgi dxguid d2d1 dwrite windowscodecs dwmapi CoreMessaging windowsapp)
aether_add_module_tests(graphics LINK aether_graphics)
