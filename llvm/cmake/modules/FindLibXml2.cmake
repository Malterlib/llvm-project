# Try to find the libxml2 library.
#
# If LLVM_LIBXML2_SOURCE_DIR is set, libxml2 is compiled as a static target
# inside the LLVM build graph instead of being found from the host system.

if(LLVM_LIBXML2_SOURCE_DIR)
  get_filename_component(LLVM_LIBXML2_SOURCE_DIR "${LLVM_LIBXML2_SOURCE_DIR}" ABSOLUTE)
  if(NOT EXISTS "${LLVM_LIBXML2_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "LLVM_LIBXML2_SOURCE_DIR does not contain libxml2 sources: ${LLVM_LIBXML2_SOURCE_DIR}")
  endif()

  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(LIBXML2_WITH_DOCS OFF CACHE BOOL "" FORCE)
  set(LIBXML2_WITH_ICONV OFF CACHE BOOL "" FORCE)
  set(LIBXML2_WITH_ICU OFF CACHE BOOL "" FORCE)
  set(LIBXML2_WITH_MODULES OFF CACHE BOOL "" FORCE)
  set(LIBXML2_WITH_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(LIBXML2_WITH_PYTHON OFF CACHE BOOL "" FORCE)
  set(LIBXML2_WITH_READLINE OFF CACHE BOOL "" FORCE)
  set(LIBXML2_WITH_TESTS OFF CACHE BOOL "" FORCE)
  set(LIBXML2_WITH_ZLIB OFF CACHE BOOL "" FORCE)

  if(NOT TARGET LibXml2::LibXml2)
    add_subdirectory("${LLVM_LIBXML2_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/libxml2" EXCLUDE_FROM_ALL)
  endif()
  if(NOT TARGET LibXml2::LibXml2)
    message(FATAL_ERROR "LLVM_LIBXML2_SOURCE_DIR did not define target LibXml2::LibXml2: ${LLVM_LIBXML2_SOURCE_DIR}")
  endif()

  set(LibXml2_FOUND TRUE)
  set(LIBXML2_FOUND TRUE)
  set(LIBXML2_INCLUDE_DIR "${LLVM_LIBXML2_SOURCE_DIR}/include" CACHE PATH "" FORCE)
  set(LIBXML2_INCLUDE_DIRS "${LLVM_LIBXML2_SOURCE_DIR}/include;${CMAKE_BINARY_DIR}/libxml2/include" CACHE PATH "" FORCE)
  set(LIBXML2_LIBRARY LibXml2::LibXml2 CACHE STRING "" FORCE)
  set(LIBXML2_LIBRARIES LibXml2::LibXml2 CACHE STRING "" FORCE)
else()
  # Try to find the libxml2 library
  #
  # If successful, the following variables will be defined:
  # LIBXML2_INCLUDE_DIR
  # LIBXML2_LIBRARY
  # LIBXML2_STATIC_LIBRARY
  # LibXml2_FOUND
  #
  # Additionally, the following import targets will be defined:
  # LibXml2::LibXml2
  # LibXml2::LibXml2Static (if the static library is found)

  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(PC_LIBXML QUIET libxml-2.0)
  endif()

  find_path(LIBXML2_INCLUDE_DIR NAMES libxml/xpath.h
    HINTS
    ${PC_LIBXML_INCLUDEDIR}
    ${PC_LIBXML_INCLUDE_DIRS}
    PATH_SUFFIXES libxml2
  )

  if(DEFINED LIBXML2_LIBRARIES AND NOT DEFINED LIBXML2_LIBRARY)
    set(LIBXML2_LIBRARY "${LIBXML2_LIBRARIES}" CACHE FILEPATH "Path to libxml2 library")
  endif()

  find_library(LIBXML2_LIBRARY NAMES xml2 libxml2 libxml2s libxml2_a
    HINTS
    ${PC_LIBXML_LIBDIR}
    ${PC_LIBXML_LIBRARY_DIRS}
  )

  find_library(LIBXML2_STATIC_LIBRARY NAMES
    "${CMAKE_STATIC_LIBRARY_PREFIX}libxml2s${CMAKE_STATIC_LIBRARY_SUFFIX}"
    "${CMAKE_STATIC_LIBRARY_PREFIX}xml2${CMAKE_STATIC_LIBRARY_SUFFIX}"
    "${CMAKE_STATIC_LIBRARY_PREFIX}libxml2${CMAKE_STATIC_LIBRARY_SUFFIX}"
    HINTS
    ${PC_LIBXML_LIBDIR}
    ${PC_LIBXML_LIBRARY_DIRS}
  )

  include(FindPackageHandleStandardArgs)

  # When pkg-config is unavailable or has no .pc file for libxml2 (e.g. when
  # linking against a static libxml2-pic.a that ships no libxml-2.0.pc),
  # PC_LIBXML_VERSION is left empty.  Fall back to reading the version from the
  # xmlversion.h header so that the >=2.8 version check in
  # find_package_handle_standard_args succeeds.
  if(NOT PC_LIBXML_VERSION AND LIBXML2_INCLUDE_DIR AND
     EXISTS "${LIBXML2_INCLUDE_DIR}/libxml/xmlversion.h")
    file(STRINGS "${LIBXML2_INCLUDE_DIR}/libxml/xmlversion.h"
         _libxml2_version_str REGEX "^#define[\t ]+LIBXML_DOTTED_VERSION[\t ]+\".*\"")
    if(_libxml2_version_str)
      string(REGEX REPLACE "^#define[\t ]+LIBXML_DOTTED_VERSION[\t ]+\"([^\"]*)\".*" "\\1"
             PC_LIBXML_VERSION "${_libxml2_version_str}")
    endif()
    unset(_libxml2_version_str)
  endif()

  find_package_handle_standard_args(LibXml2
    REQUIRED_VARS LIBXML2_LIBRARY LIBXML2_INCLUDE_DIR
    VERSION_VAR PC_LIBXML_VERSION
  )

  if(LibXml2_FOUND)
    if(NOT TARGET LibXml2::LibXml2)
      add_library(LibXml2::LibXml2 UNKNOWN IMPORTED)
      set_target_properties(LibXml2::LibXml2 PROPERTIES
          INTERFACE_INCLUDE_DIRECTORIES "${LIBXML2_INCLUDE_DIR}"
          INTERFACE_COMPILE_OPTIONS "${PC_LIBXML_CFLAGS_OTHER}"
          IMPORTED_LOCATION "${LIBXML2_LIBRARY}")
    endif()
    if(LIBXML2_STATIC_LIBRARY AND NOT TARGET LibXml2::LibXml2Static)
      add_library(LibXml2::LibXml2Static STATIC IMPORTED)
      set_target_properties(LibXml2::LibXml2Static PROPERTIES
          INTERFACE_INCLUDE_DIRECTORIES "${LIBXML2_INCLUDE_DIR}"
          INTERFACE_COMPILE_OPTIONS "${PC_LIBXML_CFLAGS_OTHER}"
          IMPORTED_LOCATION "${LIBXML2_STATIC_LIBRARY}")
      # Static libraries need their transitive dependencies for linking.
      set(LIBXML2_STATIC_DEPS)
      foreach(lib IN LISTS PC_LIBXML_STATIC_LIBRARIES)
        if(NOT lib STREQUAL "xml2")
          list(APPEND LIBXML2_STATIC_DEPS ${lib})
        endif()
      endforeach()
      if(LIBXML2_STATIC_DEPS)
        set_target_properties(LibXml2::LibXml2Static PROPERTIES
            INTERFACE_LINK_LIBRARIES "${LIBXML2_STATIC_DEPS}")
      endif()
    endif()
  endif()

  set(LIBXML2_INCLUDE_DIRS ${LIBXML2_INCLUDE_DIR})
  set(LIBXML2_LIBRARIES ${LIBXML2_LIBRARY})
  set(LIBXML2_DEFINITIONS ${PC_LIBXML_CFLAGS_OTHER})

  mark_as_advanced(LIBXML2_INCLUDE_DIR LIBXML2_LIBRARY LIBXML2_STATIC_LIBRARY)
endif()
