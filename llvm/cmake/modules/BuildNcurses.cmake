function(llvm_add_ncurses_source_targets)
  set(_ncurses_source_dir "${LLVM_NCURSES_SOURCE_DIR}")
  if(NOT _ncurses_source_dir)
    set(_ncurses_source_dir "${LLVM_NCURSES_TERMCAP_SOURCE_DIR}")
  endif()

  if(NOT _ncurses_source_dir)
    return()
  endif()

  if(TARGET llvm_ncursesw_static AND TARGET llvm_panelw_static AND TARGET llvm_tinfow_static)
    return()
  endif()

  get_filename_component(_ncurses_source_dir "${_ncurses_source_dir}" ABSOLUTE)
  if(NOT EXISTS "${_ncurses_source_dir}/configure" OR NOT EXISTS "${_ncurses_source_dir}/panel/panel.h")
    message(FATAL_ERROR "ncurses source directory does not point to an ncurses source checkout: ${_ncurses_source_dir}")
  endif()

  set(_ncurses_build_root "${CMAKE_BINARY_DIR}/ncurses")
  set(_ncurses_build_dir "${_ncurses_build_root}/build")
  set(_ncurses_install_dir "${_ncurses_build_root}/install")
  set(_ncurses_parent_include_dir "${_ncurses_install_dir}/include")
  set(_ncurses_include_dir "${_ncurses_install_dir}/include/ncursesw")
  set(_ncurses_library_dir "${_ncurses_install_dir}/lib")
  set(_ncurses_library "${_ncurses_library_dir}/libncursesw.a")
  set(_panel_library "${_ncurses_library_dir}/libpanelw.a")
  set(_tinfo_library "${_ncurses_library_dir}/libtinfo.a")
  set(_ncurses_stamp "${_ncurses_build_root}/ncurses-install.stamp")

  file(MAKE_DIRECTORY "${_ncurses_parent_include_dir}" "${_ncurses_include_dir}" "${_ncurses_library_dir}")

  find_program(_ncurses_make NAMES gmake make REQUIRED)

  include(ProcessorCount)
  ProcessorCount(_ncurses_jobs)
  if(NOT _ncurses_jobs)
    set(_ncurses_jobs 1)
  endif()

  if(CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _ncurses_build_type)
    set(_ncurses_config_flags "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${_ncurses_build_type}} -fPIC")
  else()
    set(_ncurses_config_flags "${CMAKE_C_FLAGS} -fPIC")
  endif()

  add_custom_command(
    OUTPUT
      "${_ncurses_stamp}"
      "${_ncurses_library}"
      "${_panel_library}"
      "${_tinfo_library}"
      "${_ncurses_include_dir}/curses.h"
      "${_ncurses_include_dir}/panel.h"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_ncurses_build_dir}" "${_ncurses_install_dir}"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${_ncurses_build_dir}"
      "${CMAKE_COMMAND}" -E env
        "CC=${CMAKE_C_COMPILER}"
        "AR=${CMAKE_AR}"
        "RANLIB=${CMAKE_RANLIB}"
        "CFLAGS=${_ncurses_config_flags}"
        "${_ncurses_source_dir}/configure"
          "--prefix=${_ncurses_install_dir}"
          "--with-normal"
          "--without-shared"
          "--without-debug"
          "--with-termlib=tinfo"
          "--enable-widec"
          "--without-ada"
          "--without-cxx"
          "--without-cxx-binding"
          "--without-manpages"
          "--without-progs"
          "--without-tests"
          "--without-tack"
          "--without-gpm"
          "--without-dlsym"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${_ncurses_build_dir}"
      "${_ncurses_make}" "-j${_ncurses_jobs}" libs
    COMMAND "${CMAKE_COMMAND}" -E chdir "${_ncurses_build_dir}"
      "${_ncurses_make}" install.libs install.includes
    COMMAND "${CMAKE_COMMAND}" -E touch "${_ncurses_stamp}"
    DEPENDS
      "${_ncurses_source_dir}/configure"
      "${_ncurses_source_dir}/include/curses.h.in"
      "${_ncurses_source_dir}/panel/panel.h"
    VERBATIM
    USES_TERMINAL)

  add_custom_target(llvm_ncurses_static_build DEPENDS "${_ncurses_stamp}")

  add_library(llvm_tinfow_static INTERFACE)
  target_include_directories(llvm_tinfow_static INTERFACE "${_ncurses_parent_include_dir}" "${_ncurses_include_dir}")
  target_link_libraries(llvm_tinfow_static INTERFACE "${_tinfo_library}")
  add_dependencies(llvm_tinfow_static llvm_ncurses_static_build)

  add_library(llvm_ncursesw_static INTERFACE)
  target_include_directories(llvm_ncursesw_static INTERFACE "${_ncurses_parent_include_dir}" "${_ncurses_include_dir}")
  target_link_libraries(llvm_ncursesw_static INTERFACE "${_ncurses_library}" llvm_tinfow_static)
  add_dependencies(llvm_ncursesw_static llvm_ncurses_static_build)

  add_library(llvm_panelw_static INTERFACE)
  target_include_directories(llvm_panelw_static INTERFACE "${_ncurses_parent_include_dir}" "${_ncurses_include_dir}")
  target_link_libraries(llvm_panelw_static INTERFACE "${_panel_library}" llvm_ncursesw_static)
  add_dependencies(llvm_panelw_static llvm_ncurses_static_build)

  set(LLVM_NCURSES_INCLUDE_DIR "${_ncurses_parent_include_dir};${_ncurses_include_dir}" CACHE STRING "The ncurses include directories" FORCE)
  set(LLVM_NCURSES_LIBRARY llvm_ncursesw_static CACHE STRING "The ncurses library target" FORCE)
  set(LLVM_PANEL_LIBRARY llvm_panelw_static CACHE STRING "The ncurses panel library target" FORCE)
  set(LLVM_TINFO_LIBRARY llvm_tinfow_static CACHE STRING "The ncurses tinfo library target" FORCE)
endfunction()
