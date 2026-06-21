# Try to find the OpenSSL library.
#
# If LLVM_BORINGSSL_SOURCE_DIR is set, BoringSSL is compiled as a static target
# inside the LLVM build graph and presented through the OpenSSL imported targets
# expected by projects such as curl.

if(LLVM_BORINGSSL_SOURCE_DIR)
  get_filename_component(LLVM_BORINGSSL_SOURCE_DIR "${LLVM_BORINGSSL_SOURCE_DIR}" ABSOLUTE)
  include("${CMAKE_CURRENT_LIST_DIR}/LLVMBoringSSL.cmake")
  llvm_configure_boringssl("${LLVM_BORINGSSL_SOURCE_DIR}")

  if(NOT TARGET OpenSSL::Crypto)
    add_library(OpenSSL::Crypto INTERFACE IMPORTED)
    set_target_properties(OpenSSL::Crypto PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LLVM_BORINGSSL_SOURCE_DIR}/include"
      INTERFACE_LINK_LIBRARIES "crypto;decrepit")
  endif()

  if(NOT TARGET OpenSSL::SSL)
    add_library(OpenSSL::SSL INTERFACE IMPORTED)
    set_target_properties(OpenSSL::SSL PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LLVM_BORINGSSL_SOURCE_DIR}/include"
      INTERFACE_LINK_LIBRARIES "ssl;OpenSSL::Crypto")
  endif()

  set(OPENSSL_FOUND TRUE)
  set(OpenSSL_FOUND TRUE)
  set(OPENSSL_INCLUDE_DIR "${LLVM_BORINGSSL_SOURCE_DIR}/include")
  set(OPENSSL_CRYPTO_LIBRARY OpenSSL::Crypto)
  set(OPENSSL_CRYPTO_LIBRARIES OpenSSL::Crypto)
  set(OPENSSL_SSL_LIBRARY OpenSSL::SSL)
  set(OPENSSL_SSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
  set(OPENSSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
  set(OPENSSL_VERSION "1.1.1")
  set(OpenSSL_VERSION "${OPENSSL_VERSION}")
  set(OPENSSL_VERSION_MAJOR "1")
  set(OPENSSL_VERSION_MINOR "1")
  set(OPENSSL_VERSION_FIX "1")

  foreach(_openssl_component IN LISTS OpenSSL_FIND_COMPONENTS)
    if(_openssl_component STREQUAL "Crypto" OR _openssl_component STREQUAL "SSL")
      set(OpenSSL_${_openssl_component}_FOUND TRUE)
    else()
      set(OpenSSL_${_openssl_component}_FOUND FALSE)
    endif()
  endforeach()

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(OpenSSL
    REQUIRED_VARS
      OPENSSL_INCLUDE_DIR
    VERSION_VAR
      OpenSSL_VERSION
    HANDLE_VERSION_RANGE
    HANDLE_COMPONENTS)

  mark_as_advanced(OPENSSL_INCLUDE_DIR OPENSSL_CRYPTO_LIBRARY OPENSSL_SSL_LIBRARY)
  return()
endif()

include("${CMAKE_ROOT}/Modules/FindOpenSSL.cmake")
