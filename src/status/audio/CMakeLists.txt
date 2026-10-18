# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# aether_status_audio: Windows Core Audio providers for the Top Status Bar
# (default output volume/mute, default microphone mute/input gain) and their
# control API. Only the shared contract header status/StatusTypes.h is used;
# the aether_status library is deliberately not linked.
#
# windowsapp: C++/WinRT (winrt::implements / winrt::com_ptr) implements the
# classic COM callback interfaces.
aether_add_module_library(aether_status_audio
    LINK aether_core aether_diagnostics aether_platform ole32 windowsapp)
aether_add_module_tests(status_audio LINK aether_status_audio)
