# SPDX-License-Identifier: GPL-2.0-or-later

set(FREETYPE_EXTRA_ARGS
  -DCMAKE_RELEASE_POSTFIX:STRING=2ST
  -DCMAKE_DEBUG_POSTFIX:STRING=2ST_d
  -DFT_DISABLE_BZIP2=ON
  -DFT_DISABLE_HARFBUZZ=ON
  -DFT_DISABLE_PNG=ON
  -DFT_REQUIRE_BROTLI=ON
  -DPC_BROTLIDEC_INCLUDEDIR=${LIBDIR}/brotli/include
  -DPC_BROTLIDEC_LIBDIR=${LIBDIR}/brotli/lib
  )

ExternalProject_Add(external_freetype
  URL file://${PACKAGE_DIR}/${FREETYPE_FILE}
  DOWNLOAD_DIR ${DOWNLOAD_DIR}
  URL_HASH ${FREETYPE_HASH_TYPE}=${FREETYPE_HASH}
  PREFIX ${BUILD_DIR}/freetype
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${LIBDIR}/freetype ${DEFAULT_CMAKE_FLAGS} ${FREETYPE_EXTRA_ARGS}
  INSTALL_DIR ${LIBDIR}/freetype
)

add_dependencies(
  external_freetype
  external_brotli
)

if(BUILD_MODE STREQUAL Release AND WIN32)
  ExternalProject_Add_Step(external_freetype after_install
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${LIBDIR}/freetype ${HARVEST_TARGET}/freetype
    DEPENDEES install
  )
endif()
