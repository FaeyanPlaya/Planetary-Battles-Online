add_subdirectory(src/Audio)
add_library(MatrixGameInternal STATIC)

macro(add_matrix_directory DIRECTORY)
    file(GLOB DIR_SOURCES ${DIRECTORY}/*.cpp ${DIRECTORY}/*.c)
    file(GLOB DIR_HEADERS ${DIRECTORY}/*.hpp ${DIRECTORY}/*.h)
    target_sources(MatrixGameInternal PRIVATE ${DIR_SOURCES} ${DIR_HEADERS})
    get_filename_component (GROUP ${DIRECTORY} NAME)
    source_group ("${GROUP}\\Source Files" FILES ${DIR_SOURCES})
    source_group ("${GROUP}\\Header Files" FILES ${DIR_HEADERS})
endmacro()

file(GLOB MATRIX_SOURCES src/*.cpp src/*.c)
file(GLOB MATRIX_HEADERS src/*.hpp src/*.h)

target_sources(
    MatrixGameInternal
    PRIVATE
        ${MATRIX_SOURCES}
        ${MATRIX_HEADERS})

target_include_directories(
    MatrixGameInternal
    PUBLIC
        .
        src
        "$<$<C_COMPILER_ID:MSVC>:${DIRECTX9_INCLUDE_DIRS}>")

target_compile_options(
    MatrixGameInternal
    PUBLIC
        ${COMPILE_OPTIONS})

target_compile_definitions(
    MatrixGameInternal
    PUBLIC
        MATRIXGAME_EXPORTS
        $<$<CONFIG:Release>:ASSERT_OFF>
        WINDOWS_IGNORE_PACKING_MISMATCH
        ${COMPILE_DEFINITIONS})

set(MATRIXGAME_ENGINE_LINK_OPTIONS ${LINK_OPTIONS})
list(REMOVE_ITEM MATRIXGAME_ENGINE_LINK_OPTIONS /SUBSYSTEM:WINDOWS)
if(NOT MATRIXGAME_BUILD_DLL AND NOT MSVC)
    # PBOEngine.dll, the stub EXE and the mods share MinGW's C++ runtime DLLs: one copy of its state, exceptions
    # across them (the tests link everything statically themselves).
    set(MATRIXGAME_ENGINE_LINK_OPTIONS "$<$<CONFIG:Release>:-s;-Wl,--no-insert-timestamp>")
endif()
target_link_options(
    MatrixGameInternal
    PUBLIC
        "$<$<C_COMPILER_ID:MSVC>:-SAFESEH:NO>"
        ${MATRIXGAME_ENGINE_LINK_OPTIONS})

target_link_libraries(
    MatrixGameInternal
    PUBLIC
        winmm
        ws2_32
        ${DIRECTX9_LIBRARIES}
        MatrixLib
        MatrixAudio
        "$<$<C_COMPILER_ID:MSVC>:legacy_stdio_definitions>"
)

if(MATRIXGAME_CHEATS)
    set_property(
            SOURCE src/MatrixFormGame.cpp
            APPEND
            PROPERTY COMPILE_DEFINITIONS CHEATS_ON
    )
endif()

add_matrix_directory(src/Effects)
add_matrix_directory(src/Interface)
add_matrix_directory(src/Logic)
add_matrix_directory(src/Text)

# PBOEngineId.hpp: PBO_ENGINE_ID, the hash of the engine's headers, which a mod's DLL is built with and must carry
# (ModApi.hpp); the engine loads only mods with its own.
file(GLOB_RECURSE MATRIXGAME_API_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/MatrixGame/src/*.h ${CMAKE_SOURCE_DIR}/MatrixGame/src/*.hpp
    ${CMAKE_SOURCE_DIR}/MatrixLib/*.h ${CMAKE_SOURCE_DIR}/MatrixLib/*.hpp)
set(MATRIXGAME_ENGINE_ID_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/PBOEngineId.hpp)
add_custom_command(OUTPUT ${MATRIXGAME_ENGINE_ID_HEADER}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -DOUTPUT_HEADER=${MATRIXGAME_ENGINE_ID_HEADER}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/engine/engine_id.cmake
    DEPENDS ${MATRIXGAME_API_HEADERS} ${CMAKE_CURRENT_SOURCE_DIR}/engine/engine_id.cmake
    VERBATIM)
target_sources(MatrixGameInternal PRIVATE ${MATRIXGAME_ENGINE_ID_HEADER})
target_include_directories(MatrixGameInternal PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)

if(MATRIXGAME_BUILD_DLL)
    add_library(MatrixGame SHARED)
    # remove lib prefix from the DLL name (for GCC build)
    set_target_properties(MatrixGame PROPERTIES PREFIX "")
else()
    set_property(
            SOURCE
            src/MatrixFormGame.cpp
            src/MatrixGame.cpp
            src/DelayedDamage.cpp
            src/GroupCounts.cpp
            src/PlaceContainment.cpp
            src/ZeroXTrace.cpp
            APPEND
            PROPERTY COMPILE_DEFINITIONS BUILD_EXE
    )
    # The engine is PBOEngine.dll and the EXE a stub over it (its WinMain is the DLL's, MatrixGame.cpp): the mods'
    # DLLs in the mods folder link against the engine by a name that stays, whatever the EXE is called
    # (games/space-rangers-hd/MODS-DESIGN.md).
    add_library(PBOEngine SHARED engine/PBOEngine.cpp)
    set_target_properties(PBOEngine PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    # Every object of the engine's libraries, not only those the EXE's code reaches; the libraries again for their
    # own dependencies. A mod gets the engine's includes and definitions, not its static libraries.
    target_link_libraries(PBOEngine PRIVATE MatrixGameInternal)
    target_include_directories(PBOEngine INTERFACE $<TARGET_PROPERTY:MatrixGameInternal,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(PBOEngine INTERFACE $<TARGET_PROPERTY:MatrixGameInternal,INTERFACE_COMPILE_DEFINITIONS>)
    target_compile_options(PBOEngine INTERFACE $<TARGET_PROPERTY:MatrixGameInternal,INTERFACE_COMPILE_OPTIONS>)
    target_link_options(
        PBOEngine
        PRIVATE
            -Wl,--whole-archive $<TARGET_FILE:MatrixGameInternal> $<TARGET_FILE:MatrixLib> $<TARGET_FILE:MatrixAudio>
            -Wl,--no-whole-archive
            -Wl,--export-all-symbols
            -Wl,--exclude-libs,libpng16.a:libzs.a:libvorbisfile.a:libvorbis.a:libogg.a
            ${MATRIXGAME_ENGINE_LINK_OPTIONS})
    add_executable(MatrixGame WIN32 engine/Launcher.cpp)
    # MinGW's runtime DLLs beside the EXE.
    get_filename_component(MATRIXGAME_COMPILER_BIN ${CMAKE_CXX_COMPILER} DIRECTORY)
    foreach(runtime libstdc++-6.dll libgcc_s_dw2-1.dll libmcfgthread-1.dll)
        add_custom_command(TARGET PBOEngine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MATRIXGAME_COMPILER_BIN}/${runtime} ${CMAKE_CURRENT_BINARY_DIR})
    endforeach()
    # Declare DPI awareness before startup without affecting the DLL host.
    if(MSVC)
        target_sources(MatrixGame PRIVATE MatrixGame.manifest)
    else()
        target_sources(MatrixGame PRIVATE MatrixGame.manifest.rc)
    endif()
endif()

set(MATRIX_RESOURCES
    MatrixGame.ico
    MatrixGame.rc
    Resource.h)

source_group("Resources" FILES ${MATRIX_RESOURCES})

if(MATRIXGAME_BUILD_DLL)
    target_sources(MatrixGame PRIVATE src/MatrixGame.cpp)
    target_link_libraries(MatrixGame MatrixGameInternal)
    target_link_options(MatrixGame PRIVATE ${LINK_OPTIONS})
else()
    target_link_libraries(MatrixGame PBOEngine)
    # The C runtime's start (libmingw32, linked last) wants WinMain: taken from the engine's import library, so
    # asked for before the libraries are read.
    target_link_options(MatrixGame PRIVATE -Wl,-u,_WinMain@16 ${MATRIXGAME_ENGINE_LINK_OPTIONS})
endif()
target_sources(MatrixGame PRIVATE ${MATRIX_RESOURCES})

# Copy the CFG directory from the MatrixGame/
if (NOT MATRIXGAME_BUILD_DLL)
    if (NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/CFG)
        message(STATUS "Copying the CFG/ directory to ${CMAKE_CURRENT_BINARY_DIR}")
        file(COPY CFG DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    endif ()
endif ()

# eos-bridge.exe beside the EXE (EosBridge/eos-bridge.cpp: Epic Online Services P2P; NetLink.hpp), when the EOS SDK
# (64-bit only, so the bridge is built with a 64-bit MinGW) is there; the product's ids (EOS_IDS, never in the
# repository) go to CFG\eos.txt.
set(EOS_SDK_DIR ${CMAKE_SOURCE_DIR}/../vendor/eos-sdk/SDK CACHE PATH "The EOS C SDK's SDK folder")
set(EOS_GXX ${CMAKE_SOURCE_DIR}/.tools/winlibs-13.2.0/mingw64/bin/g++.exe CACHE FILEPATH "A 64-bit MinGW g++")
set(EOS_IDS ${CMAKE_SOURCE_DIR}/../local/eos.txt CACHE FILEPATH "The EOS product's ids")
if (NOT MATRIXGAME_BUILD_DLL AND EXISTS ${EOS_SDK_DIR}/Include/eos_sdk.h AND EXISTS ${EOS_GXX})
    set(EOS_BRIDGE_SOURCE ${CMAKE_SOURCE_DIR}/EosBridge/eos-bridge.cpp)
    set(EOS_DLL ${EOS_SDK_DIR}/Bin/EOSSDK-Win64-Shipping.dll)
    get_filename_component(EOS_GXX_BIN ${EOS_GXX} DIRECTORY)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/eos-bridge.exe
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${EOS_GXX_BIN}" ${EOS_GXX} -std=c++20 -O2 -I${EOS_SDK_DIR}/Include
            ${EOS_BRIDGE_SOURCE} ${EOS_DLL} -lws2_32 -static -o ${CMAKE_CURRENT_BINARY_DIR}/eos-bridge.exe
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EOS_DLL} ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${EOS_BRIDGE_SOURCE}
        COMMENT "Building eos-bridge.exe (64-bit)")
    add_custom_target(EosBridge ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/eos-bridge.exe)
    if (EXISTS ${EOS_IDS})
        configure_file(${EOS_IDS} ${CMAKE_CURRENT_BINARY_DIR}/CFG/eos.txt COPYONLY)
    endif ()
endif ()

# The mods folder beside the EXE. Our mods of it (mods/<id> here; games/space-rangers-hd/MODS-DESIGN.md): each a DLL
# over PBOEngine and its mod.txt in mods/<id> beside the EXE; the EXE carries the fingerprints of the personal ones
# (Mods::verified), written by ModHash once they are built. The engine runs their code through ModHooks.hpp.
if (NOT MATRIXGAME_BUILD_DLL)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/mods)
    # Our mods (mods/<id>: mod.txt, <id>.cpp for a mod with code: its DLL; data.txt for its data: ModData).
    # tools/make-build.ps1 ships them.
    set(MATRIXGAME_MODS autoboom ballistics farview firstperson fixes homing interface manual newsky pbo rally repair
                        sky sr2 turrets)
    set(MATRIXGAME_MOD_FOLDERS)
    set(MATRIXGAME_MOD_FILES)
    foreach(mod ${MATRIXGAME_MODS})
        set(folder ${CMAKE_CURRENT_BINARY_DIR}/mods/${mod})
        add_custom_command(OUTPUT ${folder}/mod.txt
            COMMAND ${CMAKE_COMMAND} -E make_directory ${folder}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/mods/${mod}/mod.txt ${folder}/mod.txt
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/mods/${mod}/mod.txt)
        list(APPEND MATRIXGAME_MOD_FOLDERS ${folder})
        list(APPEND MATRIXGAME_MOD_FILES ${folder}/mod.txt)
        # A mod's models (sky's reparator.vo) and its data (data.txt: ModData) go beside its mod.txt.
        file(GLOB mod_models CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/mods/${mod}/*.vo
             ${CMAKE_CURRENT_SOURCE_DIR}/mods/${mod}/data.txt)
        foreach(model ${mod_models})
            get_filename_component(model_name ${model} NAME)
            add_custom_command(OUTPUT ${folder}/${model_name}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${folder}
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${model} ${folder}/${model_name}
                DEPENDS ${model})
            list(APPEND MATRIXGAME_MOD_FILES ${folder}/${model_name})
        endforeach()
        if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/mods/${mod}/${mod}.cpp)
            add_library(pbo_mod_${mod} SHARED mods/${mod}/${mod}.cpp ${folder}/mod.txt)
            set_target_properties(pbo_mod_${mod} PROPERTIES PREFIX "" OUTPUT_NAME ${mod} RUNTIME_OUTPUT_DIRECTORY ${folder})
            target_link_libraries(pbo_mod_${mod} PRIVATE PBOEngine ${DIRECTX9_LIBRARIES})  # (D3DX: its math)
            target_link_options(pbo_mod_${mod} PRIVATE ${MATRIXGAME_ENGINE_LINK_OPTIONS})
            list(APPEND MATRIXGAME_MOD_FILES pbo_mod_${mod})
        endif ()
    endforeach()
    add_executable(ModHash engine/ModHash.cpp)
    set_target_properties(ModHash PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(ModHash PRIVATE PBOEngine)
    set(MATRIXGAME_VERIFIED_MODS ${CMAKE_CURRENT_BINARY_DIR}/generated/VerifiedMods.cpp)
    add_custom_command(OUTPUT ${MATRIXGAME_VERIFIED_MODS}
        COMMAND ModHash ${MATRIXGAME_VERIFIED_MODS} ${MATRIXGAME_MOD_FOLDERS}
        DEPENDS ModHash ${MATRIXGAME_MOD_FILES})
    target_sources(MatrixGame PRIVATE ${MATRIXGAME_VERIFIED_MODS})
endif ()

# Copy the DATA/robots.pkg file from the game
if (NOT MATRIXGAME_BUILD_DLL)
    if (NOT MATRIXGAME_PKG_SPACE_RANGERS)
        set(MATRIXGAME_PKG_SPACE_RANGERS "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Space Rangers HD A War Apart" CACHE PATH "Space Rangers 2 path to copy robots.pkg from.")
    endif ()
    option(MATRIXGAME_PKG_BRING_FROM_GAME "Get the robots.pkg file from the game." TRUE)
    option(MATRIXGAME_PKG_SYMLINK "Create symlink instead of copying(requires admin)." FALSE) # Requires admin privileges on Windows but frees from duplicating 200+ MB.

    # Create DATA directory
    if (NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/DATA)
        message(STATUS "Creating directory ${CMAKE_CURRENT_BINARY_DIR}/DATA")
        file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/DATA)
    endif ()

    if (NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/DATA/robots.pkg AND MATRIXGAME_PKG_BRING_FROM_GAME)
        if (EXISTS ${MATRIXGAME_PKG_SPACE_RANGERS}/DATA/robots.pkg)
            if(MATRIXGAME_PKG_SYMLINK)
                message(STATUS "Symlinking DATA/robots.pkg file.")
                file(CREATE_LINK
                        ${MATRIXGAME_PKG_SPACE_RANGERS}/DATA/robots.pkg
                        ${CMAKE_CURRENT_BINARY_DIR}/DATA/robots.pkg
                        SYMBOLIC
                )
            else ()
                message(STATUS "Copying DATA/robots.pkg file.")
                file(COPY
                        ${MATRIXGAME_PKG_SPACE_RANGERS}/DATA/robots.pkg
                        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/DATA
                )
            endif ()
        else()
            message(STATUS "Space Rangers 2 not found at `${MATRIXGAME_PKG_SPACE_RANGERS}`")
            message(STATUS "Skipping copying DATA/robots.pkg file.")
            message(STATUS "You'll need to bring the DATA/robots.pkg file yourself in order to start MatrixGame.exe!")
        endif()
    endif ()
endif ()
