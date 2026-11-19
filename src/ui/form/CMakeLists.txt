# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_ui_form: the shared Aether presentation for activatable application windows with native
# child controls (Settings, setup, confirmations): DWM Acrylic or opaque material, scene painting of
# native controls, opaque layered editors, scrolling regions and the confirmation dialog. It uses no
# shell surface, so setup can use it on a machine without Aether running. Only Liquid's smart
# transparency (D84, SmartGlass) composes with a GraphicsDevice its owner passes in; without one the
# module never touches the compositor.
aether_add_module_library(aether_ui_form
    LINK aether_graphics aether_platform aether_diagnostics aether_core
         user32 gdi32 dwmapi uxtheme comctl32 d2d1 dwrite windowscodecs oleacc uiautomationcore ole32 oleaut32
         shell32 shlwapi uuid)
aether_add_module_tests(ui_form LINK aether_ui_form)
