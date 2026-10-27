cmake_minimum_required(VERSION 3.27...4.4)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CONFIGURATION_TYPES Release Debug)

project(MatrixGame)

option(BUILD_TESTING "Build the engine regression tests" ON)
if(BUILD_TESTING)
    enable_testing()
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(MATRIXGAME_BUILD_DLL "Build dll instead of exe" TRUE)
option(MATRIXGAME_CHEATS "Enable cheats" TRUE)
option(MATRIXGAME_TRACE "Enable the legacy diagnostic trace build" OFF)
option(MATRIXGAME_COVERAGE "Instrument project code for gcov coverage (MinGW only)" OFF)
# The DLL's host owns music, so only the standalone EXE decodes it by default.
if(MATRIXGAME_BUILD_DLL)
    set(MATRIXGAME_MUSIC_DEFAULT OFF)
else()
    set(MATRIXGAME_MUSIC_DEFAULT ON)
endif()
option(MATRIXGAME_MUSIC "Build the standalone battle music decoder with libogg and libvorbis"
       ${MATRIXGAME_MUSIC_DEFAULT})

if(MATRIXGAME_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "MATRIXGAME_COVERAGE requires the GCC (MinGW) toolchain and its gcov.")
    endif()
    # Applies to the engine, libraries and tests; dependency builds keep their own flags.
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

find_package(DIRECTX9 REQUIRED)
if(MSVC)
    if(NOT DIRECTX9_FOUND)
        message(FATAL_ERROR "Could not find DirectX SDK")
    endif()
    include_directories(AFTER ${DIRECTX9_INCLUDE_DIRS})
endif()

include(ThirdParty/CMakeLists.txt)

list(APPEND COMPILE_DEFINITIONS MATRIXGAME_EXPORTS $<$<CONFIG:Release>:ASSERT_OFF>)
if(MATRIXGAME_TRACE)
    # Keep diagnostic signatures and layouts consistent across libraries, game and tests.
    list(APPEND COMPILE_DEFINITIONS _TRACE)
endif()
if(MATRIXGAME_MUSIC)
    list(APPEND COMPILE_DEFINITIONS MATRIXGAME_MUSIC)
endif()

if(MSVC)
    set(COMPILE_OPTIONS
        $<$<CONFIG:Release>:/Zi /W3 /O2 /Ob2 /Oi /Ot /Oy /GF /EHsc /GS- /Gy->
        $<$<CONFIG:Debug>:/Zi /W3 /Od /GF /Gm- /RTC1>
    )
    set(LINK_OPTIONS
        /SUBSYSTEM:WINDOWS /DYNAMICBASE:NO /MACHINE:X86
        $<$<CONFIG:Release>:/INCREMENTAL:NO /OPT:REF /OPT:ICF>
        $<$<CONFIG:Debug>:/INCREMENTAL /DEBUG>
    )

    list(APPEND COMPILE_DEFINITIONS $<$<CONFIG:Release>:_CRT_SECURE_NO_WARNINGS> _AFX_NO_DEBUG_CRT NOMINMAX)

    # TODO: <codecvt> is deprecated since C++17. in future we should
    # use std::string everywhere and not cast string <-> wstring.
    # till then - here is a hotfix to suppress warnings
    list(APPEND COMPILE_DEFINITIONS _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
else()
    list(APPEND COMPILE_DEFINITIONS $<$<CONFIG:Debug>:_DEBUG>)

    # All the warnings below are potential bugs.
    # -Wno-... options should be fixed and removed.
    list(APPEND COMPILE_OPTIONS
        $<$<CONFIG:Release>:-Os>
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}/="
        -ftrivial-auto-var-init=zero

        -Wall
        -Wextra
        -Wpedantic
        -Werror

        -Wno-unknown-pragmas
        -Wno-reorder
        -Wno-sign-compare
        -Wno-unused-function
        -Wno-unused-variable
        -Wno-class-memaccess
        -Wno-parentheses
        -Wno-switch
        -Wno-strict-aliasing
        -Wno-empty-body
        -Wno-pedantic
        -Wno-uninitialized
    )

    set(LINK_OPTIONS
        $<$<CONFIG:Release>:-static -s -Wl,--no-insert-timestamp>
    )
    if(MATRIXGAME_TRACE)
        # Legacy trace assertions throw, including in noexcept destructors. Retain
        # their existing termination behavior and warning in this opt-in build.
        list(APPEND COMPILE_OPTIONS -Wno-error=terminate)
    endif()
endif()

add_subdirectory(MatrixLib)
add_subdirectory(MatrixGame)

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")  # a copy of the sources may come without them
    add_subdirectory(tests)
endif()

install(
    TARGETS MatrixGame
    CONFIGURATIONS Release
    RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/bin
    LIBRARY DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/bin
)
install(
    TARGETS MatrixGame
    CONFIGURATIONS Debug
    RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/bin/Debug
    LIBRARY DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/bin/Debug
)

if(MSVC)
    install(
        FILES $<TARGET_PDB_FILE:MatrixGame>
        CONFIGURATIONS Debug
        DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/bin/Debug
    )
endif()
