# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_module_library(aether_media_controls LINK aether_core aether_platform windowsapp wbemuuid oleaut32 ole32 user32)
aether_add_module_tests(media_controls LINK aether_media_controls)
