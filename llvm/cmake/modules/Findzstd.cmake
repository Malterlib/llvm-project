# Try to find the zstd library
#
# If successful, the following variables will be defined:
# zstd_INCLUDE_DIR
# zstd_LIBRARY
# zstd_STATIC_LIBRARY
# zstd_FOUND
#
# Additionally, one of the following import targets will be defined:
# zstd::libzstd_shared
# zstd::libzstd_static

if(MSVC OR "${CMAKE_CXX_SIMULATE_ID}" STREQUAL "MSVC")
  set(zstd_STATIC_LIBRARY_SUFFIX "_static\\${CMAKE_STATIC_LIBRARY_SUFFIX}$")
else()
  set(zstd_STATIC_LIBRARY_SUFFIX "\\${CMAKE_STATIC_LIBRARY_SUFFIX}$")
endif()

if(LLVM_ZSTD_SOURCE_DIR)
  get_filename_component(LLVM_ZSTD_SOURCE_DIR "${LLVM_ZSTD_SOURCE_DIR}" ABSOLUTE)
  set(LLVM_ZSTD_CMAKE_DIR "${LLVM_ZSTD_SOURCE_DIR}/build/cmake")
  if(NOT EXISTS "${LLVM_ZSTD_CMAKE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "LLVM_ZSTD_SOURCE_DIR does not contain zstd's CMake project: ${LLVM_ZSTD_SOURCE_DIR}")
  endif()

  set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)

  set(LLVM_ZSTD_BINARY_DIR "${CMAKE_BINARY_DIR}/zstd")
  set(LLVM_ZSTD_SAVED_CMAKE_SKIP_INSTALL_RULES "${CMAKE_SKIP_INSTALL_RULES}")
  set(CMAKE_SKIP_INSTALL_RULES TRUE)
  if(NOT TARGET libzstd_static)
    add_subdirectory("${LLVM_ZSTD_CMAKE_DIR}" "${LLVM_ZSTD_BINARY_DIR}" EXCLUDE_FROM_ALL)
  endif()
  set(CMAKE_SKIP_INSTALL_RULES "${LLVM_ZSTD_SAVED_CMAKE_SKIP_INSTALL_RULES}")
  unset(LLVM_ZSTD_SAVED_CMAKE_SKIP_INSTALL_RULES)

  if(NOT TARGET libzstd_static)
    message(FATAL_ERROR "Failed to import zstd static library target from: ${LLVM_ZSTD_CMAKE_DIR}")
  endif()

  if(NOT TARGET zstd::libzstd_static)
    add_library(zstd::libzstd_static ALIAS libzstd_static)
  endif()

  set(zstd_INCLUDE_DIR "${LLVM_ZSTD_SOURCE_DIR}/lib" CACHE PATH "" FORCE)
  if(MSVC OR "${CMAKE_CXX_SIMULATE_ID}" STREQUAL "MSVC")
    set(zstd_STATIC_LIBRARY_NAME "zstd_static")
  else()
    set(zstd_STATIC_LIBRARY_NAME "zstd")
  endif()
  set(zstd_STATIC_LIBRARY "${LLVM_ZSTD_BINARY_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${zstd_STATIC_LIBRARY_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX}" CACHE FILEPATH "" FORCE)
  set(zstd_LIBRARY "${zstd_STATIC_LIBRARY}" CACHE FILEPATH "" FORCE)
else()
  find_path(zstd_INCLUDE_DIR NAMES zstd.h)
  find_library(zstd_LIBRARY NAMES zstd zstd_static)
  find_library(zstd_STATIC_LIBRARY NAMES
    zstd_static
    "${CMAKE_STATIC_LIBRARY_PREFIX}zstd${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    zstd DEFAULT_MSG
    zstd_LIBRARY zstd_INCLUDE_DIR
)

if(zstd_FOUND)
  if(zstd_LIBRARY MATCHES "${zstd_STATIC_LIBRARY_SUFFIX}$" AND NOT zstd_LIBRARY MATCHES "\\.dll\\.a$")
    set(zstd_STATIC_LIBRARY "${zstd_LIBRARY}")
  elseif (NOT TARGET zstd::libzstd_shared)
    add_library(zstd::libzstd_shared SHARED IMPORTED)
    if(WIN32 OR CYGWIN)
      include(GNUInstallDirs) # For CMAKE_INSTALL_LIBDIR and friends.
      # IMPORTED_LOCATION is the path to the DLL and IMPORTED_IMPLIB is the "library".
      get_filename_component(zstd_DIRNAME "${zstd_LIBRARY}" DIRECTORY)
      if(NOT "${CMAKE_INSTALL_LIBDIR}" STREQUAL "" AND NOT "${CMAKE_INSTALL_BINDIR}" STREQUAL "")
        string(REGEX REPLACE "${CMAKE_INSTALL_LIBDIR}$" "${CMAKE_INSTALL_BINDIR}" zstd_DIRNAME "${zstd_DIRNAME}")
      endif()
      get_filename_component(zstd_BASENAME "${zstd_LIBRARY}" NAME)
      string(REGEX REPLACE "\\${CMAKE_LINK_LIBRARY_SUFFIX}$" "${CMAKE_SHARED_LIBRARY_SUFFIX}" zstd_BASENAME "${zstd_BASENAME}")
      set_target_properties(zstd::libzstd_shared PROPERTIES
          INTERFACE_INCLUDE_DIRECTORIES "${zstd_INCLUDE_DIR}"
          IMPORTED_LOCATION "${zstd_DIRNAME}/${zstd_BASENAME}"
          IMPORTED_IMPLIB "${zstd_LIBRARY}")
      unset(zstd_DIRNAME)
      unset(zstd_BASENAME)
    else()
      set_target_properties(zstd::libzstd_shared PROPERTIES
          INTERFACE_INCLUDE_DIRECTORIES "${zstd_INCLUDE_DIR}"
          IMPORTED_LOCATION "${zstd_LIBRARY}")
    endif()
  endif()
  if(zstd_STATIC_LIBRARY MATCHES "${zstd_STATIC_LIBRARY_SUFFIX}$" AND
     NOT TARGET zstd::libzstd_static)
    add_library(zstd::libzstd_static STATIC IMPORTED)
    set_target_properties(zstd::libzstd_static PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${zstd_INCLUDE_DIR}"
        IMPORTED_LOCATION "${zstd_STATIC_LIBRARY}")
  endif()
endif()

unset(zstd_STATIC_LIBRARY_SUFFIX)

mark_as_advanced(zstd_INCLUDE_DIR zstd_LIBRARY zstd_STATIC_LIBRARY)
