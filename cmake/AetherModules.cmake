# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# Module registry. Each module lives in its own directory, produces the target
# aether_<module> (or an executable for app/updater/installer) and declares the
# modules it depends on. Selecting a subset with -DAETHER_MODULES=<list> pulls
# in dependencies automatically, which keeps focused builds fast and isolated.

set(AETHER_MODULE_ORDER
    core diagnostics platform net graphics accessibility ui showcase
    status status_audio weather weather_location applications search settings updates install
    dock topbar search_ui settings_ui system_controls media_controls controlcenter
    app launcher updater installer)

macro(aether_declare_module name directory)
    set(AETHER_MODULE_DIR_${name} "${directory}")
    set(AETHER_MODULE_DEPS_${name} ${ARGN})
endmacro()

aether_declare_module(core          src/core)
aether_declare_module(diagnostics   src/diagnostics       core)
aether_declare_module(platform      src/platform/windows  core diagnostics)
aether_declare_module(net           src/platform/net      core diagnostics platform)
aether_declare_module(graphics      src/graphics          core diagnostics platform)
aether_declare_module(accessibility src/ui/accessibility  core diagnostics platform)
aether_declare_module(ui            src/ui                core diagnostics platform graphics)
aether_declare_module(showcase      src/showcase          core graphics)
# status_audio, weather and search use only the shared contract headers of
# status/applications (StatusTypes.h, ApplicationTypes.h), not their libraries.
aether_declare_module(status        src/status            core diagnostics platform)
aether_declare_module(status_audio  src/status/audio      core diagnostics platform)
aether_declare_module(weather       src/weather           core diagnostics platform)
aether_declare_module(weather_location src/weather_location core platform)
aether_declare_module(applications  src/applications      core diagnostics platform)
aether_declare_module(search        src/search            core diagnostics platform)
aether_declare_module(settings      src/settings          core diagnostics platform)
aether_declare_module(updates       src/updates           core diagnostics platform)
aether_declare_module(install       src/install           core diagnostics platform updates settings)
aether_declare_module(dock          src/dock              ui accessibility applications settings)
aether_declare_module(topbar        src/topbar            ui accessibility status status_audio weather settings)
aether_declare_module(search_ui     src/search/ui         ui accessibility search applications)
aether_declare_module(settings_ui   src/settings/ui       ui settings updates applications weather)
aether_declare_module(system_controls src/system_controls core platform status)
aether_declare_module(media_controls src/media_controls   core platform)
aether_declare_module(controlcenter src/controlcenter     ui accessibility status status_audio system_controls media_controls)
aether_declare_module(app           src/app
    core diagnostics platform net graphics accessibility ui status status_audio weather weather_location
    applications search settings updates install dock topbar search_ui settings_ui controlcenter)
aether_declare_module(launcher      launcher              core diagnostics platform updates install)
aether_declare_module(updater       updater               core diagnostics platform updates install)
aether_declare_module(installer     installer             core diagnostics platform graphics updates install settings app launcher updater)

function(aether_resolve_modules out_var)
    if(AETHER_MODULES STREQUAL "" OR AETHER_MODULES STREQUAL "all")
        set(${out_var} ${AETHER_MODULE_ORDER} PARENT_SCOPE)
        return()
    endif()
    set(_pending ${AETHER_MODULES})
    set(_resolved "")
    while(_pending)
        list(POP_FRONT _pending _module)
        if(NOT DEFINED AETHER_MODULE_DIR_${_module})
            message(FATAL_ERROR "Unknown Aether Shell module '${_module}'. Known modules: ${AETHER_MODULE_ORDER}")
        endif()
        if(NOT _module IN_LIST _resolved)
            list(APPEND _resolved ${_module})
            list(APPEND _pending ${AETHER_MODULE_DEPS_${_module}})
        endif()
    endwhile()
    set(_ordered "")
    foreach(_module IN LISTS AETHER_MODULE_ORDER)
        if(_module IN_LIST _resolved)
            list(APPEND _ordered ${_module})
        endif()
    endforeach()
    set(${out_var} ${_ordered} PARENT_SCOPE)
endfunction()

function(aether_add_modules)
    aether_resolve_modules(_modules)
    set(_enabled "")
    foreach(_module IN LISTS _modules)
        set(_dir "${PROJECT_SOURCE_DIR}/${AETHER_MODULE_DIR_${_module}}")
        if(EXISTS "${_dir}/CMakeLists.txt")
            add_subdirectory("${_dir}" "${PROJECT_BINARY_DIR}/modules/${_module}")
            list(APPEND _enabled ${_module})
        else()
            message(STATUS "Module '${_module}' has no CMakeLists.txt yet; skipped.")
        endif()
    endforeach()
    set(AETHER_ENABLED_MODULES "${_enabled}" PARENT_SCOPE)
endfunction()

# True when the given module was selected for this build.
function(aether_module_enabled module out_var)
    aether_resolve_modules(_modules)
    if(module IN_LIST _modules AND EXISTS "${PROJECT_SOURCE_DIR}/${AETHER_MODULE_DIR_${module}}/CMakeLists.txt")
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()
