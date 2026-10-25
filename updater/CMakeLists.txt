# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_executable(AetherShellUpdater WIN32 SOURCES main.cpp
    LINK aether_install aether_ui_form aether_graphics aether_diagnostics comctl32 shell32 user32)
aether_add_version_resource(AetherShellUpdater DESCRIPTION "Aether Shell updater" INTERNAL_NAME "AetherShellUpdater")
