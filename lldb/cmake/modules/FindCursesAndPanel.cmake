#.rst:
# FindCursesAndPanel
# -----------
#
# Find the curses, terminfo, and panel library as a whole.

include(CMakePushCheckState)

if(LLDB_PDCURSES_SOURCE_DIR AND LLVM_NCURSES_TERMCAP_SOURCE_DIR)
  unset(LLVM_NCURSES_SOURCE_DIR CACHE)
  unset(LLVM_NCURSES_SOURCE_DIR)
  unset(LLVM_NCURSES_TERMCAP_SOURCE_DIR CACHE)
  unset(LLVM_NCURSES_TERMCAP_SOURCE_DIR)
endif()

if(LLDB_PDCURSES_SOURCE_DIR AND LLVM_NCURSES_SOURCE_DIR)
  message(FATAL_ERROR "Only one of LLDB_PDCURSES_SOURCE_DIR and LLVM_NCURSES_SOURCE_DIR may be set")
endif()

if(LLVM_NCURSES_SOURCE_DIR)
  include(BuildNcurses)
  llvm_add_ncurses_source_targets()
  set(CURSES_INCLUDE_DIRS "${LLVM_NCURSES_INCLUDE_DIR}" CACHE PATH "The curses include directory" FORCE)
  set(CURSES_LIBRARIES "${LLVM_NCURSES_LIBRARY}" CACHE STRING "The curses library target" FORCE)
  set(PANEL_LIBRARIES "${LLVM_PANEL_LIBRARY}" CACHE STRING "The curses panel library target" FORCE)
  set(HAS_TERMINFO_SYMBOLS TRUE CACHE BOOL "ncurses provides the required curses symbols" FORCE)
endif()

if(LLDB_PDCURSES_SOURCE_DIR)
  set(LLVM_PDCURSES_SOURCE_DIR "${LLDB_PDCURSES_SOURCE_DIR}" CACHE PATH "The PDCurses source directory" FORCE)
  if(LLDB_PDCURSES_TARGET)
    set(LLVM_PDCURSES_TARGET "${LLDB_PDCURSES_TARGET}" CACHE STRING "The PDCurses CMake target to link" FORCE)
  endif()
  include(BuildPDCurses)
  llvm_add_pdcurses_source_target()

  set(CURSES_INCLUDE_DIRS "${LLVM_PDCURSES_SOURCE_DIR}" CACHE PATH "The curses include directory" FORCE)
  set(CURSES_LIBRARIES "${LLVM_PDCURSES_TARGET}" CACHE STRING "The curses library target" FORCE)
  set(PANEL_LIBRARIES "${LLVM_PDCURSES_TARGET}" CACHE STRING "The curses panel library target" FORCE)
  set(HAS_TERMINFO_SYMBOLS TRUE CACHE BOOL "PDCurses provides the required curses symbols" FORCE)
endif()

function(lldb_check_curses_tinfo CURSES_INCLUDE_DIRS CURSES_LIBRARIES CURSES_HAS_TINFO)
  cmake_reset_check_state()
  set(CMAKE_REQUIRED_INCLUDES "${CURSES_INCLUDE_DIRS}")
  set(CMAKE_REQUIRED_LIBRARIES "${CURSES_LIBRARIES}")
  # acs_map is one of many symbols that are part of tinfo but could
  # be bundled in curses.
  check_symbol_exists(acs_map "curses.h" CURSES_HAS_TINFO)
endfunction()

if(CURSES_INCLUDE_DIRS AND CURSES_LIBRARIES AND PANEL_LIBRARIES)
  if(NOT HAS_TERMINFO_SYMBOLS)
    lldb_check_curses_tinfo("${CURSES_INCLUDE_DIRS}"
                            "${CURSES_LIBRARIES}"
                            CURSES_HAS_TINFO)
    if(NOT CURSES_HAS_TINFO)
      message(WARNING "CURSES_LIBRARIES was provided manually but is missing terminfo symbols")
    endif()
    mark_as_advanced(CURSES_HAS_TINFO)
  endif()
  set(CURSESANDPANEL_FOUND TRUE)
else()
  find_package(Curses QUIET)
  find_library(PANEL_LIBRARIES NAMES panel DOC "The curses panel library" QUIET)
  include(FindPackageHandleStandardArgs)

  if(CURSES_FOUND AND PANEL_LIBRARIES)
    # Sometimes the curses libraries define their own terminfo symbols,
    # other times they're extern and are defined by a separate terminfo library.
    # Auto-detect which.
    lldb_check_curses_tinfo("${CURSES_INCLUDE_DIRS}"
                            "${CURSES_LIBRARIES}"
                            CURSES_HAS_TINFO)
    if(NOT CURSES_HAS_TINFO)
      message(STATUS "curses library missing terminfo symbols, looking for tinfo separately")
      find_library(TINFO_LIBRARIES NAMES tinfo DOC "The curses tinfo library" QUIET)
      list(APPEND CURSES_LIBRARIES "${TINFO_LIBRARIES}")
    endif()
    set(HAS_TERMINFO_SYMBOLS "$<OR:$<BOOL:${TERMINFO_LIBRARIES}>,$<BOOL:${CURSES_HAS_TINFO}>>")
  endif()

  find_package_handle_standard_args(CursesAndPanel
                                    FOUND_VAR
                                      CURSESANDPANEL_FOUND
                                    REQUIRED_VARS
                                      CURSES_INCLUDE_DIRS
                                      CURSES_LIBRARIES
                                      PANEL_LIBRARIES
                                      HAS_TERMINFO_SYMBOLS)

  if(CURSES_FOUND AND PANEL_LIBRARIES AND HAS_TERMINFO_SYMBOLS)
    mark_as_advanced(CURSES_INCLUDE_DIRS
                      PANEL_LIBRARIES
                      HAS_TERMINFO_SYMBOLS
                      CURSES_HAS_TINFO)
  endif()
  if(TINFO_LIBRARIES)
    mark_as_advanced(TINFO_LIBRARIES)
  endif()
endif()
