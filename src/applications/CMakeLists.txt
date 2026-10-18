# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_applications: application identity, Apps-folder/Start-menu discovery, launching and
# activation, running-window tracking (WinEvent hooks) and shell icon extraction.
# windowsapp is linked for C++/WinRT's winrt::com_ptr / winrt::hresult_error support code.
aether_add_module_library(aether_applications
    LINK aether_core aether_diagnostics aether_platform
         shell32 ole32 oleaut32 propsys shlwapi user32 gdi32 dwmapi uuid windowsapp)
aether_add_module_tests(applications LINK aether_applications)
