# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
aether_add_module_library(aether_settings_ui LINK aether_ui aether_ui_form aether_settings aether_updates aether_applications aether_weather comctl32 shell32)
aether_add_module_tests(settings_ui LINK aether_settings_ui)
# D83: the layout test lays every page out in each language from the packs a release publishes; loading them through
# i18n::LoadInstalledPack also proves src/core/LanguageTable.inc matches the translations.
aether_use_test_language_packs(aether_settings_ui_integration_tests)
# Every page in each of the 41 languages takes several minutes in a debug build.
if(TARGET aether_settings_ui_integration_tests)
    set_tests_properties(aether_settings_ui_integration_tests PROPERTIES TIMEOUT 1200)
endif()
