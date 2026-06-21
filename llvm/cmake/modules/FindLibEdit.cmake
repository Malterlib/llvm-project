#.rst:
# FindLibEdit
# -----------
#
# Find libedit library and headers
#
# The module defines the following variables:
#
# ::
#
#   LibEdit_FOUND          - true if libedit was found
#   LibEdit_INCLUDE_DIRS   - include search path
#   LibEdit_LIBRARIES      - libraries to link
#   LibEdit_VERSION_STRING - version number

if(LLVM_LIBEDIT_SOURCE_DIR)
  set(_libedit_source_dir "${LLVM_LIBEDIT_SOURCE_DIR}")
  set(_libedit_src_dir "${_libedit_source_dir}/src")
  set(_libedit_build_dir "${CMAKE_CURRENT_BINARY_DIR}/libedit")

  if(NOT EXISTS "${_libedit_src_dir}/histedit.h" OR NOT EXISTS "${_libedit_src_dir}/vis.c")
    message(FATAL_ERROR "LLVM_LIBEDIT_SOURCE_DIR does not point to a libedit source checkout: ${LLVM_LIBEDIT_SOURCE_DIR}")
  endif()

  file(MAKE_DIRECTORY "${_libedit_build_dir}")

  find_program(_libedit_awk NAMES awk gawk mawk REQUIRED)
  find_program(_libedit_sh NAMES sh REQUIRED)

  file(WRITE "${_libedit_build_dir}/config.h" [[
#pragma once

#define CLOSEDIR_VOID 1
#define HAVE_CURSES_H 1
#define HAVE_DIRENT_H 1
#define HAVE_DLFCN_H 1
#define HAVE_ENDPWENT 1
#define HAVE_FCNTL_H 1
#define HAVE_FORK 1
#define HAVE_GETLINE 1
#define HAVE_GETPW_R_POSIX 1
#define HAVE_INTTYPES_H 1
#define HAVE_ISASCII 1
#define HAVE_ISSETUGID 1
#define HAVE_LIBTINFO 1
#define HAVE_LIMITS_H 1
#define HAVE_MALLOC_H 1
#define HAVE_MEMCHR 1
#define HAVE_MEMORY_H 1
#define HAVE_MEMSET 1
#define HAVE_NCURSES_H 1
#define HAVE_REGCOMP 1
#define HAVE_RE_COMP 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRCASECMP 1
#define HAVE_STRCHR 1
#define HAVE_STRCSPN 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRRCHR 1
#define HAVE_STRSTR 1
#define HAVE_STRTOL 1
#define HAVE_SYS_CDEFS_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_TERMCAP_H 1
#define HAVE_TERM_H 1
#define HAVE_UNISTD_H 1
#define HAVE_U_INT32_T 1
#define HAVE_VFORK 1
#define HAVE_VIS 0
#define HAVE_SVIS 0
#define HAVE_WCSDUP 1
#define HAVE_WORKING_FORK 1
#define HAVE_WORKING_VFORK 1
#define LSTAT_FOLLOWS_SLASHED_SYMLINK 1
#define PACKAGE "libedit"
#define PACKAGE_NAME "libedit"
#define PACKAGE_STRING "libedit 3.0"
#define PACKAGE_TARNAME "libedit"
#define PACKAGE_VERSION "3.0"
#define RETSIGTYPE void
#define STDC_HEADERS 1
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#define VERSION "3.0"
#define WIDECHAR 1

#include "sys.h"
int issetugid(void);
#define SCCSID
#undef LIBC_SCCS
#define lint
]])

  if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    file(APPEND "${_libedit_build_dir}/config.h" [[
#define HAVE_STRLCAT 1
#define HAVE_STRLCPY 1
]])
  endif()

  set(_libedit_compat_sources)
  if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    file(WRITE "${_libedit_build_dir}/linux_compat.c" [[
#define _GNU_SOURCE 1
#include <sys/auxv.h>
#include <unistd.h>

int issetugid(void) {
#ifdef AT_SECURE
  if (getauxval(AT_SECURE) != 0)
    return 1;
#endif
  return getuid() != geteuid() || getgid() != getegid();
}
]])
    list(APPEND _libedit_compat_sources "${_libedit_build_dir}/linux_compat.c")
  endif()

  include(CMakeParseArguments)

  function(_llvm_libedit_add_makelist_output output)
    cmake_parse_arguments(LIBEDIT_MAKELIST "" "" "ARGS;DEPENDS" ${ARGN})
    set(script "${_libedit_build_dir}/Generate-${output}.cmake")

    file(WRITE "${script}" "execute_process(\n  COMMAND \"${CMAKE_COMMAND}\" -E env \"AWK=${_libedit_awk}\" \"${_libedit_sh}\" \"${_libedit_src_dir}/makelist\"\n")
    foreach(arg IN LISTS LIBEDIT_MAKELIST_ARGS)
      file(APPEND "${script}" "    \"${arg}\"\n")
    endforeach()
    file(APPEND "${script}" "  OUTPUT_FILE \"${_libedit_build_dir}/${output}\"\n  RESULT_VARIABLE result\n)\nif(NOT result EQUAL 0)\n  message(FATAL_ERROR \"libedit makelist failed for ${output}\")\nendif()\n")

    add_custom_command(
      OUTPUT "${_libedit_build_dir}/${output}"
      COMMAND "${CMAKE_COMMAND}" -P "${script}"
      DEPENDS "${_libedit_src_dir}/makelist" ${LIBEDIT_MAKELIST_DEPENDS}
      VERBATIM)
  endfunction()

  set(_libedit_vi_h "${_libedit_build_dir}/vi.h")
  set(_libedit_emacs_h "${_libedit_build_dir}/emacs.h")
  set(_libedit_common_h "${_libedit_build_dir}/common.h")
  set(_libedit_action_headers "${_libedit_vi_h}" "${_libedit_emacs_h}" "${_libedit_common_h}")
  set(_libedit_action_sources "${_libedit_src_dir}/vi.c" "${_libedit_src_dir}/emacs.c" "${_libedit_src_dir}/common.c")

  _llvm_libedit_add_makelist_output(vi.h ARGS -h "${_libedit_src_dir}/vi.c" DEPENDS "${_libedit_src_dir}/vi.c")
  _llvm_libedit_add_makelist_output(emacs.h ARGS -h "${_libedit_src_dir}/emacs.c" DEPENDS "${_libedit_src_dir}/emacs.c")
  _llvm_libedit_add_makelist_output(common.h ARGS -h "${_libedit_src_dir}/common.c" DEPENDS "${_libedit_src_dir}/common.c")
  _llvm_libedit_add_makelist_output(fcns.h ARGS -fh ${_libedit_action_headers} DEPENDS ${_libedit_action_headers})
  _llvm_libedit_add_makelist_output(help.h ARGS -bh ${_libedit_action_sources} DEPENDS ${_libedit_action_sources})
  _llvm_libedit_add_makelist_output(func.h ARGS -fc ${_libedit_action_headers} DEPENDS ${_libedit_action_headers})

  if(NOT TARGET llvm_libedit_static)
    add_library(llvm_libedit_static STATIC
      "${_libedit_src_dir}/chared.c"
      "${_libedit_src_dir}/common.c"
      "${_libedit_src_dir}/el.c"
      "${_libedit_src_dir}/eln.c"
      "${_libedit_src_dir}/emacs.c"
      "${_libedit_src_dir}/filecomplete.c"
      "${_libedit_src_dir}/hist.c"
      "${_libedit_src_dir}/history.c"
      "${_libedit_src_dir}/historyn.c"
      "${_libedit_src_dir}/keymacro.c"
      "${_libedit_src_dir}/literal.c"
      "${_libedit_src_dir}/map.c"
      "${_libedit_src_dir}/chartype.c"
      "${_libedit_src_dir}/parse.c"
      "${_libedit_src_dir}/prompt.c"
      "${_libedit_src_dir}/read.c"
      "${_libedit_src_dir}/readline.c"
      "${_libedit_src_dir}/reallocarr.c"
      "${_libedit_src_dir}/refresh.c"
      "${_libedit_src_dir}/search.c"
      "${_libedit_src_dir}/sig.c"
      "${_libedit_src_dir}/strlcat.c"
      "${_libedit_src_dir}/strlcpy.c"
      "${_libedit_src_dir}/terminal.c"
      "${_libedit_src_dir}/tokenizer.c"
      "${_libedit_src_dir}/tokenizern.c"
      "${_libedit_src_dir}/tty.c"
      "${_libedit_src_dir}/unvis.c"
      "${_libedit_src_dir}/vi.c"
      "${_libedit_src_dir}/vis.c"
      "${_libedit_src_dir}/wcsdup.c"
      ${_libedit_compat_sources}
      "${_libedit_build_dir}/fcns.h"
      "${_libedit_build_dir}/func.h"
      "${_libedit_build_dir}/help.h")

    target_include_directories(llvm_libedit_static PUBLIC
      "${_libedit_build_dir}"
      "${_libedit_src_dir}")
    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
      target_compile_definitions(llvm_libedit_static PRIVATE _GNU_SOURCE=1)
    endif()

    if((LLVM_PDCURSES_SOURCE_DIR OR LLDB_PDCURSES_SOURCE_DIR) AND (LLVM_NCURSES_SOURCE_DIR OR LLVM_NCURSES_TERMCAP_SOURCE_DIR))
      unset(LLVM_NCURSES_SOURCE_DIR CACHE)
      unset(LLVM_NCURSES_SOURCE_DIR)
      unset(LLVM_NCURSES_TERMCAP_SOURCE_DIR CACHE)
      unset(LLVM_NCURSES_TERMCAP_SOURCE_DIR)
    endif()

    if(LLVM_PDCURSES_SOURCE_DIR OR LLDB_PDCURSES_SOURCE_DIR)
      include(BuildPDCurses)
      llvm_add_pdcurses_source_target()
      target_include_directories(llvm_libedit_static PRIVATE "${LLVM_PDCURSES_SOURCE_DIR}")
      file(APPEND "${_libedit_build_dir}/config.h" [[
#undef HAVE_TERMCAP_H
]])
      set(LibEdit_TERMCAP_LIBRARIES "${LLVM_PDCURSES_TARGET}")
    elseif(LLVM_NCURSES_SOURCE_DIR OR LLVM_NCURSES_TERMCAP_SOURCE_DIR)
      include(BuildNcurses)
      llvm_add_ncurses_source_targets()
      target_include_directories(llvm_libedit_static PRIVATE "${LLVM_NCURSES_INCLUDE_DIR}")
      add_dependencies(llvm_libedit_static llvm_ncurses_static_build)
      set(LibEdit_TERMCAP_LIBRARIES llvm_tinfow_static)
    elseif(NOT LibEdit_TERMCAP_LIBRARIES)
      message(FATAL_ERROR "LibEdit_TERMCAP_LIBRARIES must be set when LLVM_LIBEDIT_SOURCE_DIR is used without LLVM_PDCURSES_SOURCE_DIR, LLVM_NCURSES_SOURCE_DIR, or LLVM_NCURSES_TERMCAP_SOURCE_DIR")
    endif()

    target_link_libraries(llvm_libedit_static INTERFACE ${LibEdit_TERMCAP_LIBRARIES})
    set_target_properties(llvm_libedit_static PROPERTIES
      ARCHIVE_OUTPUT_DIRECTORY "${LLVM_LIBRARY_OUTPUT_INTDIR}"
      OUTPUT_NAME edit
      POSITION_INDEPENDENT_CODE ON)
  endif()

  if(NOT TARGET LibEdit::LibEdit)
    add_library(LibEdit::LibEdit ALIAS llvm_libedit_static)
  endif()

  set(LibEdit_FOUND TRUE)
  set(LibEdit_INCLUDE_DIRS "${_libedit_build_dir}" "${_libedit_src_dir}")
  set(LibEdit_LIBRARIES LibEdit::LibEdit)
  set(LibEdit_VERSION_STRING "2.11")
  mark_as_advanced(LibEdit_INCLUDE_DIRS LibEdit_LIBRARIES)
  return()
endif()

find_package(PkgConfig QUIET)
pkg_check_modules(PC_LIBEDIT QUIET libedit)

find_path(LibEdit_INCLUDE_DIRS NAMES histedit.h HINTS ${PC_LIBEDIT_INCLUDE_DIRS})
find_library(LibEdit_LIBRARIES NAMES edit HINTS ${PC_LIBEDIT_LIBRARY_DIRS})

include(CheckIncludeFile)
if(LibEdit_INCLUDE_DIRS AND EXISTS "${LibEdit_INCLUDE_DIRS}/histedit.h")
  include(CMakePushCheckState)
  cmake_push_check_state()
  list(APPEND CMAKE_REQUIRED_INCLUDES ${LibEdit_INCLUDE_DIRS})
  list(APPEND CMAKE_REQUIRED_LIBRARIES ${LibEdit_LIBRARIES})
  check_include_file(histedit.h HAVE_HISTEDIT_H)
  cmake_pop_check_state()
  if (HAVE_HISTEDIT_H)
    file(STRINGS "${LibEdit_INCLUDE_DIRS}/histedit.h"
          libedit_major_version_str
          REGEX "^#define[ \t]+LIBEDIT_MAJOR[ \t]+[0-9]+")
    string(REGEX REPLACE "^#define[ \t]+LIBEDIT_MAJOR[ \t]+([0-9]+)" "\\1"
            libedit_major_version "${libedit_major_version_str}")

    file(STRINGS "${LibEdit_INCLUDE_DIRS}/histedit.h"
          libedit_minor_version_str
          REGEX "^#define[ \t]+LIBEDIT_MINOR[ \t]+[0-9]+")
    string(REGEX REPLACE "^#define[ \t]+LIBEDIT_MINOR[ \t]+([0-9]+)" "\\1"
            libedit_minor_version "${libedit_minor_version_str}")

    set(LibEdit_VERSION_STRING "${libedit_major_version}.${libedit_minor_version}")
  else()
    set(LibEdit_INCLUDE_DIRS "")
    set(LibEdit_LIBRARIES "")
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibEdit
                                  FOUND_VAR
                                    LibEdit_FOUND
                                  REQUIRED_VARS
                                    LibEdit_INCLUDE_DIRS
                                    LibEdit_LIBRARIES
                                  VERSION_VAR
                                    LibEdit_VERSION_STRING)
mark_as_advanced(LibEdit_INCLUDE_DIRS LibEdit_LIBRARIES)

if (LibEdit_FOUND AND NOT TARGET LibEdit::LibEdit)
  add_library(LibEdit::LibEdit INTERFACE IMPORTED)
  target_link_libraries(LibEdit::LibEdit INTERFACE ${LibEdit_LIBRARIES})
  target_include_directories(LibEdit::LibEdit INTERFACE ${LibEdit_INCLUDE_DIRS})
endif()
