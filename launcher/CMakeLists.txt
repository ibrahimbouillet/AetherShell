# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_executable(AetherShell WIN32 SOURCES main.cpp LINK aether_install shell32 user32)
aether_add_version_resource(AetherShell DESCRIPTION "Aether Shell launcher" INTERNAL_NAME "AetherShell")
