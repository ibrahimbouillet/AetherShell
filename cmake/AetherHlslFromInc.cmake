# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# D81: writes the HLSL held in a C++ raw-string include (R"HLSL( ... )HLSL") to a plain .hlsl file, so the shader
# compiler of the Windows SDK can compile it with the build. The .inc file stays the one source of the shader.
# Usage: cmake -DINPUT=<file.inc> -DOUTPUT=<file.hlsl> -P AetherHlslFromInc.cmake
if(NOT INPUT OR NOT OUTPUT)
    message(FATAL_ERROR "AetherHlslFromInc: INPUT and OUTPUT are required")
endif()
file(READ "${INPUT}" _text)
string(FIND "${_text}" "R\"HLSL(" _open)
string(FIND "${_text}" ")HLSL\"" _close REVERSE)
if(_open EQUAL -1 OR _close EQUAL -1 OR _close LESS _open)
    message(FATAL_ERROR "AetherHlslFromInc: ${INPUT} holds no R\"HLSL( ... )HLSL\" string")
endif()
math(EXPR _start "${_open} + 7")
math(EXPR _length "${_close} - ${_start}")
string(SUBSTRING "${_text}" ${_start} ${_length} _hlsl)
get_filename_component(_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_directory}")
file(WRITE "${OUTPUT}" "${_hlsl}")
