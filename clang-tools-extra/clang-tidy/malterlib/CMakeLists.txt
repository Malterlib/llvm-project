set(LLVM_LINK_COMPONENTS
  FrontendOpenMP
  Support
  )

add_clang_library(clangTidyMalterlibModule STATIC
  ConceptualPrefixCheck.cpp
  FunctionNamingCheck.cpp
  HeaderDeclarationsOnlyCheck.cpp
  MacroNamingCheck.cpp
  MalterlibCheck.cpp
  MalterlibTidyModule.cpp
  NamingUtils.cpp
  NoZeroBitScanCheck.cpp
  ParameterNamingCheck.cpp
  ParameterOwnershipCheck.cpp
  StructNotClassCheck.cpp
  TypeNamingCheck.cpp
  UnusedParameterCastCheck.cpp
  VariableNamingCheck.cpp

  LINK_LIBS
  clangTidy
  clangTidyUtils

  DEPENDS
  omp_gen
  ClangDriverOptions
  )

clang_target_link_libraries(clangTidyMalterlibModule
  PRIVATE
  clangAST
  clangASTMatchers
  clangAnalysis
  clangBasic
  clangLex
  )
add_subdirectory(plugin)
