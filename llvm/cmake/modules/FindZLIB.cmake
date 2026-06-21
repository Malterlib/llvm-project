# Try to find the zlib library.
#
# If LLVM_ZLIB_SOURCE_DIR is set, zlib is compiled as a PIC static target inside
# the LLVM build graph instead of being found from the host system.

if(LLVM_ZLIB_SOURCE_DIR)
  get_filename_component(LLVM_ZLIB_SOURCE_DIR "${LLVM_ZLIB_SOURCE_DIR}" ABSOLUTE)
  if(NOT EXISTS "${LLVM_ZLIB_SOURCE_DIR}/zlib.h")
    message(FATAL_ERROR "LLVM_ZLIB_SOURCE_DIR does not contain zlib sources: ${LLVM_ZLIB_SOURCE_DIR}")
  endif()

  set(LLVM_ZLIB_SRCS
    "${LLVM_ZLIB_SOURCE_DIR}/adler32.c"
    "${LLVM_ZLIB_SOURCE_DIR}/compress.c"
    "${LLVM_ZLIB_SOURCE_DIR}/crc32.c"
    "${LLVM_ZLIB_SOURCE_DIR}/deflate.c"
    "${LLVM_ZLIB_SOURCE_DIR}/gzclose.c"
    "${LLVM_ZLIB_SOURCE_DIR}/gzlib.c"
    "${LLVM_ZLIB_SOURCE_DIR}/gzread.c"
    "${LLVM_ZLIB_SOURCE_DIR}/gzwrite.c"
    "${LLVM_ZLIB_SOURCE_DIR}/inflate.c"
    "${LLVM_ZLIB_SOURCE_DIR}/infback.c"
    "${LLVM_ZLIB_SOURCE_DIR}/inftrees.c"
    "${LLVM_ZLIB_SOURCE_DIR}/inffast.c"
    "${LLVM_ZLIB_SOURCE_DIR}/trees.c"
    "${LLVM_ZLIB_SOURCE_DIR}/uncompr.c"
    "${LLVM_ZLIB_SOURCE_DIR}/zutil.c"
  )

  if(NOT TARGET llvm_zlib_static)
    add_library(llvm_zlib_static STATIC ${LLVM_ZLIB_SRCS})
    target_include_directories(llvm_zlib_static PUBLIC "${LLVM_ZLIB_SOURCE_DIR}")
    target_compile_definitions(llvm_zlib_static PRIVATE _LARGEFILE64_SOURCE=1)
    set_target_properties(llvm_zlib_static PROPERTIES
      ARCHIVE_OUTPUT_DIRECTORY "${LLVM_LIBRARY_OUTPUT_INTDIR}"
      OUTPUT_NAME z
      POSITION_INDEPENDENT_CODE ON
    )
  endif()

  if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS llvm_zlib_static)
  endif()

  set(LLVM_ZLIB_STATIC_LIBRARY "${LLVM_LIBRARY_OUTPUT_INTDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}z${CMAKE_STATIC_LIBRARY_SUFFIX}" CACHE FILEPATH "" FORCE)
  set(ZLIB_FOUND TRUE)
  set(ZLIB_INCLUDE_DIR "${LLVM_ZLIB_SOURCE_DIR}" CACHE PATH "" FORCE)
  set(ZLIB_INCLUDE_DIRS "${LLVM_ZLIB_SOURCE_DIR}" CACHE PATH "" FORCE)
  set(ZLIB_LIBRARY ZLIB::ZLIB CACHE STRING "" FORCE)
  set(ZLIB_LIBRARIES ZLIB::ZLIB CACHE STRING "" FORCE)
else()
  include("${CMAKE_ROOT}/Modules/FindZLIB.cmake")
endif()

mark_as_advanced(ZLIB_INCLUDE_DIR ZLIB_INCLUDE_DIRS ZLIB_LIBRARY ZLIB_LIBRARIES LLVM_ZLIB_STATIC_LIBRARY)
