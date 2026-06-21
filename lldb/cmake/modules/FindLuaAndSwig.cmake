#.rst:
# FindLuaAndSwig
# --------------
#
# Find Lua and SWIG as a whole.

if(LLVM_LUA_SOURCE_DIR)
  set(_lua_source_dir "${LLVM_LUA_SOURCE_DIR}")
  set(_lua_build_dir "${CMAKE_CURRENT_BINARY_DIR}/lua")

  if(NOT EXISTS "${_lua_source_dir}/lua.h" OR NOT EXISTS "${_lua_source_dir}/lauxlib.c")
    message(FATAL_ERROR "LLVM_LUA_SOURCE_DIR does not point to a Lua source checkout: ${LLVM_LUA_SOURCE_DIR}")
  endif()

  file(MAKE_DIRECTORY "${_lua_build_dir}")
  file(WRITE "${_lua_build_dir}/lua.hpp" [[
#pragma once

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
]])

  file(STRINGS "${_lua_source_dir}/lua.h" _lua_major_string REGEX "^#define[ \t]+LUA_VERSION_MAJOR(_N)?[ \t]+\"?[0-9]+\"?")
  file(STRINGS "${_lua_source_dir}/lua.h" _lua_minor_string REGEX "^#define[ \t]+LUA_VERSION_MINOR(_N)?[ \t]+\"?[0-9]+\"?")
  string(REGEX REPLACE "^#define[ \t]+LUA_VERSION_MAJOR(_N)?[ \t]+\"?([0-9]+)\"?.*" "\\2" LUA_VERSION_MAJOR "${_lua_major_string}")
  string(REGEX REPLACE "^#define[ \t]+LUA_VERSION_MINOR(_N)?[ \t]+\"?([0-9]+)\"?.*" "\\2" LUA_VERSION_MINOR "${_lua_minor_string}")

  if(NOT LUA_VERSION_MAJOR OR NOT LUA_VERSION_MINOR)
    message(FATAL_ERROR "Unable to determine Lua version from ${_lua_source_dir}/lua.h")
  endif()

  set(LUA_VERSION_MAJOR "${LUA_VERSION_MAJOR}" CACHE STRING "Lua major version" FORCE)
  set(LUA_VERSION_MINOR "${LUA_VERSION_MINOR}" CACHE STRING "Lua minor version" FORCE)

  if(NOT TARGET llvm_lua_static)
    add_library(llvm_lua_static STATIC
      "${_lua_source_dir}/lapi.c"
      "${_lua_source_dir}/lauxlib.c"
      "${_lua_source_dir}/lbaselib.c"
      "${_lua_source_dir}/lcode.c"
      "${_lua_source_dir}/lcorolib.c"
      "${_lua_source_dir}/lctype.c"
      "${_lua_source_dir}/ldblib.c"
      "${_lua_source_dir}/ldebug.c"
      "${_lua_source_dir}/ldo.c"
      "${_lua_source_dir}/ldump.c"
      "${_lua_source_dir}/lfunc.c"
      "${_lua_source_dir}/lgc.c"
      "${_lua_source_dir}/linit.c"
      "${_lua_source_dir}/liolib.c"
      "${_lua_source_dir}/llex.c"
      "${_lua_source_dir}/lmathlib.c"
      "${_lua_source_dir}/lmem.c"
      "${_lua_source_dir}/loadlib.c"
      "${_lua_source_dir}/lobject.c"
      "${_lua_source_dir}/lopcodes.c"
      "${_lua_source_dir}/loslib.c"
      "${_lua_source_dir}/lparser.c"
      "${_lua_source_dir}/lstate.c"
      "${_lua_source_dir}/lstring.c"
      "${_lua_source_dir}/lstrlib.c"
      "${_lua_source_dir}/ltable.c"
      "${_lua_source_dir}/ltablib.c"
      "${_lua_source_dir}/ltm.c"
      "${_lua_source_dir}/lundump.c"
      "${_lua_source_dir}/lutf8lib.c"
      "${_lua_source_dir}/lvm.c"
      "${_lua_source_dir}/lzio.c")

    target_include_directories(llvm_lua_static PUBLIC
      "${_lua_build_dir}"
      "${_lua_source_dir}")
    if(APPLE)
      target_compile_definitions(llvm_lua_static PRIVATE LUA_USE_MACOSX)
      target_link_libraries(llvm_lua_static INTERFACE m)
    elseif(UNIX)
      target_compile_definitions(llvm_lua_static PRIVATE LUA_USE_LINUX)
      target_link_libraries(llvm_lua_static INTERFACE m ${CMAKE_DL_LIBS})
    endif()
    set_target_properties(llvm_lua_static PROPERTIES
      ARCHIVE_OUTPUT_DIRECTORY "${LLVM_LIBRARY_OUTPUT_INTDIR}"
      OUTPUT_NAME lua
      POSITION_INDEPENDENT_CODE ON)
  endif()

  if(NOT TARGET Lua::Lua)
    add_library(Lua::Lua ALIAS llvm_lua_static)
  endif()

  if(NOT TARGET llvm_lua)
    add_executable(llvm_lua "${_lua_source_dir}/lua.c")
    target_link_libraries(llvm_lua PRIVATE llvm_lua_static)
    set_target_properties(llvm_lua PROPERTIES
      OUTPUT_NAME lua
      RUNTIME_OUTPUT_DIRECTORY "${LLVM_RUNTIME_OUTPUT_INTDIR}")
  endif()

  set(LUA_INCLUDE_DIR "${_lua_build_dir}" "${_lua_source_dir}")
  set(LUA_LIBRARIES Lua::Lua)

  set(LUA_EXECUTABLE "${LLVM_RUNTIME_OUTPUT_INTDIR}/lua${CMAKE_EXECUTABLE_SUFFIX}"
    CACHE FILEPATH "Lua executable built from LLVM_LUA_SOURCE_DIR" FORCE)

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(LuaAndSwig
                                    FOUND_VAR
                                      LUAANDSWIG_FOUND
                                    REQUIRED_VARS
                                      LUA_EXECUTABLE
                                      LUA_LIBRARIES
                                      LUA_INCLUDE_DIR
                                      LUA_VERSION_MINOR
                                      LUA_VERSION_MAJOR
                                      LLDB_ENABLE_SWIG)

  mark_as_advanced(
    LUA_LIBRARIES
    LUA_INCLUDE_DIR
    LUA_VERSION_MINOR
    LUA_VERSION_MAJOR
    LUA_EXECUTABLE)
  return()
endif()

if(LUA_LIBRARIES AND LUA_INCLUDE_DIR AND LLDB_ENABLE_SWIG)
  set(LUAANDSWIG_FOUND TRUE)
else()
  if (LLDB_ENABLE_SWIG)
    find_package(Lua 5.3)
    if(LUA_FOUND)
      # Find the Lua executable. Only required to run a subset of the Lua
      # tests.
      find_program(LUA_EXECUTABLE
        NAMES
        "lua"
        "lua${LUA_VERSION_MAJOR}.${LUA_VERSION_MINOR}"
      )
      mark_as_advanced(
        LUA_LIBRARIES
        LUA_INCLUDE_DIR
        LUA_VERSION_MINOR
        LUA_VERSION_MAJOR
        LUA_EXECUTABLE)
    endif()
  else()
    message(STATUS "SWIG 4 or later is required for Lua support in LLDB but could not be found")
  endif()


  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(LuaAndSwig
                                    FOUND_VAR
                                      LUAANDSWIG_FOUND
                                    REQUIRED_VARS
                                      LUA_EXECUTABLE
                                      LUA_LIBRARIES
                                      LUA_INCLUDE_DIR
                                      LUA_VERSION_MINOR
                                      LUA_VERSION_MAJOR
                                      LLDB_ENABLE_SWIG)
endif()
