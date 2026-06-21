# Configure vendored BoringSSL for LLVM's static OpenSSL-compatible targets.

macro(llvm_configure_boringssl boringssl_source_dir)
  get_filename_component(_llvm_boringssl_source_dir "${boringssl_source_dir}" ABSOLUTE)
  if(NOT EXISTS "${_llvm_boringssl_source_dir}/CMakeLists.txt")
    message(FATAL_ERROR "LLVM_BORINGSSL_SOURCE_DIR does not contain BoringSSL sources: ${_llvm_boringssl_source_dir}")
  endif()

  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)

  set(_llvm_boringssl_old_c_flags "${CMAKE_C_FLAGS}")
  set(_llvm_boringssl_old_cxx_flags "${CMAKE_CXX_FLAGS}")

  set(_llvm_boringssl_is_32_bit_x86 OFF)
  set(_llvm_boringssl_target_arch
    "${CMAKE_SYSTEM_PROCESSOR};${CMAKE_LIBRARY_ARCHITECTURE};${CMAKE_C_COMPILER_TARGET};${CMAKE_CXX_COMPILER_TARGET};${CMAKE_C_COMPILER_ARCHITECTURE_ID};${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}"
  )
  string(TOLOWER "${_llvm_boringssl_target_arch}" _llvm_boringssl_target_arch)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND _llvm_boringssl_target_arch MATCHES "(^|[; -])(i[3-6]86|x86)([; -]|$)")
    set(_llvm_boringssl_is_32_bit_x86 ON)
  endif()

  if(_llvm_boringssl_is_32_bit_x86)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2")
    endif()
  endif()

  if(NOT TARGET crypto OR NOT TARGET ssl OR NOT TARGET decrepit)
    add_subdirectory("${_llvm_boringssl_source_dir}" "${CMAKE_BINARY_DIR}/boringssl" EXCLUDE_FROM_ALL)
  endif()

  set(CMAKE_C_FLAGS "${_llvm_boringssl_old_c_flags}")
  set(CMAKE_CXX_FLAGS "${_llvm_boringssl_old_cxx_flags}")
  unset(_llvm_boringssl_old_c_flags)
  unset(_llvm_boringssl_old_cxx_flags)

  if(NOT TARGET crypto OR NOT TARGET ssl OR NOT TARGET decrepit)
    message(FATAL_ERROR "LLVM_BORINGSSL_SOURCE_DIR did not define BoringSSL targets: ${_llvm_boringssl_source_dir}")
  endif()

  unset(_llvm_boringssl_source_dir)
  unset(_llvm_boringssl_is_32_bit_x86)
  unset(_llvm_boringssl_target_arch)
endmacro()
