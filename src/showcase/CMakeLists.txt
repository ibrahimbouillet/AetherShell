# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# Deterministic camera planning and clean-session coordination for the native showcase pipeline.
aether_add_module_library(aether_showcase LINK aether_core aether_graphics d3d11 d3dcompiler dxgi gdi32 mfplat mfreadwrite mfuuid ole32 oleaut32 propsys shell32 uiautomationcore)
aether_add_module_tests(showcase LINK aether_showcase)
