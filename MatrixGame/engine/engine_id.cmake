# MatrixGame - licensed under GPLv2 or any later version.
# Invoked at build time: PBO_ENGINE_ID, a hash of the engine's headers (their paths and text, line ends left out so a
# checkout with CRLF and one with LF agree). A mod's DLL is built against these headers and carries the id; the
# engine loads only mods with its own id (games/space-rangers-hd/MODS-DESIGN.md).
cmake_minimum_required(VERSION 3.27)
if(NOT DEFINED SOURCE_ROOT OR NOT DEFINED OUTPUT_HEADER)
    message(FATAL_ERROR "SOURCE_ROOT and OUTPUT_HEADER are required")
endif()

file(GLOB_RECURSE headers RELATIVE "${SOURCE_ROOT}"
    "${SOURCE_ROOT}/MatrixGame/src/*.h" "${SOURCE_ROOT}/MatrixGame/src/*.hpp"
    "${SOURCE_ROOT}/MatrixLib/*.h" "${SOURCE_ROOT}/MatrixLib/*.hpp")
list(SORT headers)
set(all "")
foreach(header IN LISTS headers)
    file(READ "${SOURCE_ROOT}/${header}" text)
    string(REPLACE "\r" "" text "${text}")
    string(SHA256 one "${header}\n${text}")
    string(APPEND all "${one}\n")
endforeach()
string(SHA256 id "${all}")
string(SUBSTRING "${id}" 0 16 id)

set(content "// Generated at build time from the engine's headers (MatrixGame/engine/engine_id.cmake).\n#pragma once\n#define PBO_ENGINE_ID \"${id}\"\n")
# Rewritten only when the id changes, so the sources that include it are rebuilt only then (Ninja restats it).
if(EXISTS "${OUTPUT_HEADER}")
    file(READ "${OUTPUT_HEADER}" old)
    if(old STREQUAL content)
        return()
    endif()
endif()
file(WRITE "${OUTPUT_HEADER}" "${content}")
