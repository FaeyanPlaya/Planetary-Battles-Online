include(ExternalProject)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/dependencies.json")
file(READ "${CMAKE_CURRENT_LIST_DIR}/dependencies.json" DEPENDENCY_MANIFEST)
foreach(dependency zlib libpng libogg libvorbis)
  string(JSON ${dependency}_URL GET "${DEPENDENCY_MANIFEST}" ${dependency} url)
  string(JSON ${dependency}_SHA256 GET "${DEPENDENCY_MANIFEST}" ${dependency} sha256)
endforeach()

# Match the engine's compiler, configuration and CRT without inheriting game-only warnings.
set(DEPENDENCY_CMAKE_ARGS
  "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
  "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
  "-DCMAKE_C_FLAGS_DEBUG:STRING=${CMAKE_C_FLAGS_DEBUG}"
  "-DCMAKE_C_FLAGS_RELEASE:STRING=${CMAKE_C_FLAGS_RELEASE}"
  "-DCMAKE_INSTALL_LIBDIR:STRING=lib")
if(CMAKE_TOOLCHAIN_FILE)
  list(APPEND DEPENDENCY_CMAKE_ARGS "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}")
endif()
if(MINGW)
  list(APPEND DEPENDENCY_CMAKE_ARGS
    "-DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS} -ffile-prefix-map=${CMAKE_SOURCE_DIR}/=")
else()
  list(APPEND DEPENDENCY_CMAKE_ARGS "-DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS}")
endif()
if(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
  list(APPEND DEPENDENCY_CMAKE_ARGS
    "-DCMAKE_MSVC_RUNTIME_LIBRARY:STRING=${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif()

set(DEPENDENCY_INSTALL_CONFIGURATION "$<CONFIG>")
set(DEPENDENCY_DEBUG_POSTFIX "$<$<CONFIG:Debug>:d>")
if(MSVC)
  set(ZLIB_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/zlib/lib/zs${DEPENDENCY_DEBUG_POSTFIX}.lib")
  set(PNG_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/libpng/lib/libpng16_static${DEPENDENCY_DEBUG_POSTFIX}.lib")
  set(STATIC_PREFIX "")
  set(STATIC_SUFFIX ".lib")
else()
  set(ZLIB_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/zlib/lib/libzs${DEPENDENCY_DEBUG_POSTFIX}.a")
  set(PNG_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/libpng/lib/libpng16${DEPENDENCY_DEBUG_POSTFIX}.a")
  set(STATIC_PREFIX "lib")
  set(STATIC_SUFFIX ".a")
endif()
# libogg and libvorbis use the same archive names in every configuration.
set(OGG_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/libogg/lib/${STATIC_PREFIX}ogg${STATIC_SUFFIX}")
foreach(library vorbis vorbisfile vorbisenc)
  string(TOUPPER ${library} name)
  set(${name}_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/libvorbis/lib/${STATIC_PREFIX}${library}${STATIC_SUFFIX}")
endforeach()

ExternalProject_Add(zlib-external
  URL "${zlib_URL}"
  URL_HASH "SHA256=${zlib_SHA256}"
  DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.tools/downloads"
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  PREFIX zlib
  CMAKE_ARGS
    ${DEPENDENCY_CMAKE_ARGS}
    "-DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/zlib"
    -DZLIB_BUILD_SHARED=OFF
    -DZLIB_BUILD_STATIC=ON
    -DZLIB_BUILD_TESTING=OFF
  INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config ${DEPENDENCY_INSTALL_CONFIGURATION}
  INSTALL_BYPRODUCTS "${ZLIB_ARCHIVE}"
    "${CMAKE_CURRENT_BINARY_DIR}/zlib/include/zlib.h"
    "${CMAKE_CURRENT_BINARY_DIR}/zlib/include/zconf.h")

add_library(ZLIB INTERFACE)
target_include_directories(ZLIB INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/zlib/include")
target_link_libraries(ZLIB INTERFACE "${ZLIB_ARCHIVE}")
add_dependencies(ZLIB zlib-external)

ExternalProject_Add(libpng-external
  URL "${libpng_URL}"
  URL_HASH "SHA256=${libpng_SHA256}"
  DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.tools/downloads"
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  PREFIX libpng
  DEPENDS zlib-external
  CMAKE_ARGS
    ${DEPENDENCY_CMAKE_ARGS}
    "-DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/libpng"
    -DPNG_SHARED=OFF
    -DPNG_STATIC=ON
    -DPNG_TESTS=OFF
    -DPNG_TOOLS=OFF
    "-DZLIB_INCLUDE_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/zlib/include"
    "-DZLIB_LIBRARY:FILEPATH=${ZLIB_ARCHIVE}"
    "-DZLIB_LIBRARY_DEBUG:FILEPATH=${CMAKE_CURRENT_BINARY_DIR}/zlib/lib/$<IF:$<BOOL:${MSVC}>,zsd.lib,libzsd.a>"
    "-DZLIB_LIBRARY_RELEASE:FILEPATH=${CMAKE_CURRENT_BINARY_DIR}/zlib/lib/$<IF:$<BOOL:${MSVC}>,zs.lib,libzs.a>"
  INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config ${DEPENDENCY_INSTALL_CONFIGURATION}
  INSTALL_BYPRODUCTS "${PNG_ARCHIVE}"
    "${CMAKE_CURRENT_BINARY_DIR}/libpng/include/png.h"
    "${CMAKE_CURRENT_BINARY_DIR}/libpng/include/pngconf.h"
    "${CMAKE_CURRENT_BINARY_DIR}/libpng/include/pnglibconf.h")

add_library(LIBPNG INTERFACE)
target_include_directories(LIBPNG INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/libpng/include")
target_link_libraries(LIBPNG INTERFACE "${PNG_ARCHIVE}" ZLIB)
add_dependencies(LIBPNG libpng-external)


# Battle music decoding is optional; without it the libraries are neither downloaded nor built.
if(MATRIXGAME_MUSIC)
  ExternalProject_Add(libogg-external
    URL "${libogg_URL}"
    URL_HASH "SHA256=${libogg_SHA256}"
    DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.tools/downloads"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    PREFIX libogg
    CMAKE_ARGS
      ${DEPENDENCY_CMAKE_ARGS}
      "-DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/libogg"
      -DBUILD_SHARED_LIBS=OFF
      -DBUILD_TESTING=OFF
      -DINSTALL_DOCS=OFF
      -DINSTALL_PKG_CONFIG_MODULE=OFF
      -DINSTALL_CMAKE_PACKAGE_MODULE=OFF
    INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config ${DEPENDENCY_INSTALL_CONFIGURATION}
    INSTALL_BYPRODUCTS "${OGG_ARCHIVE}"
      "${CMAKE_CURRENT_BINARY_DIR}/libogg/include/ogg/ogg.h"
      "${CMAKE_CURRENT_BINARY_DIR}/libogg/include/ogg/config_types.h")

  # libvorbis 1.3.7 declares CMake 2.8.12 compatibility, which current CMake releases no longer accept.
  ExternalProject_Add(libvorbis-external
    URL "${libvorbis_URL}"
    URL_HASH "SHA256=${libvorbis_SHA256}"
    DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.tools/downloads"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    PREFIX libvorbis
    DEPENDS libogg-external
    CMAKE_ARGS
      ${DEPENDENCY_CMAKE_ARGS}
      "-DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/libvorbis"
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5
      -DBUILD_SHARED_LIBS=OFF
      -DINSTALL_CMAKE_PACKAGE_MODULE=OFF
      "-DOGG_INCLUDE_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/libogg/include"
      "-DOGG_LIBRARY:FILEPATH=${OGG_ARCHIVE}"
    INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config ${DEPENDENCY_INSTALL_CONFIGURATION}
    INSTALL_BYPRODUCTS "${VORBIS_ARCHIVE}" "${VORBISFILE_ARCHIVE}" "${VORBISENC_ARCHIVE}"
      "${CMAKE_CURRENT_BINARY_DIR}/libvorbis/include/vorbis/vorbisfile.h")

  add_library(OGG INTERFACE)
  target_include_directories(OGG INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/libogg/include")
  target_link_libraries(OGG INTERFACE "${OGG_ARCHIVE}")
  add_dependencies(OGG libogg-external)

  # Decoder for the standalone music player.
  add_library(VORBISFILE INTERFACE)
  target_include_directories(VORBISFILE INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/libvorbis/include")
  target_link_libraries(VORBISFILE INTERFACE "${VORBISFILE_ARCHIVE}" "${VORBIS_ARCHIVE}" OGG)
  add_dependencies(VORBISFILE libvorbis-external)

  # Encoder used only by tests to produce synthetic tracks.
  add_library(VORBISENC INTERFACE)
  target_link_libraries(VORBISENC INTERFACE "${VORBISENC_ARCHIVE}" VORBISFILE)
endif()
