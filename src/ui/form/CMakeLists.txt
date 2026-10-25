# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_ui_form: the shared Aether presentation for activatable application windows with native
# child controls (Settings, setup, confirmations): DWM Acrylic or opaque material, scene painting of
# native controls, opaque layered editors, scrolling regions and the confirmation dialog. It does not
# use the compositor or the shell surfaces, so setup can use it on a machine without Aether running.
aether_add_module_library(aether_ui_form
    LINK aether_graphics aether_platform aether_diagnostics aether_core
         user32 gdi32 dwmapi uxtheme comctl32 d2d1 dwrite oleacc uiautomationcore ole32 oleaut32)
aether_add_module_tests(ui_form LINK aether_ui_form)
