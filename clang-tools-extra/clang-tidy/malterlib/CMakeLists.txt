set(LLVM_LINK_COMPONENTS
  BitstreamReader
  FrontendOpenMP
  Support
  )

add_clang_library(clangTidyMalterlibModule STATIC
  CheckedAccess.cpp
  ConceptualPrefixCheck.cpp
  FixIts.cpp
  FunctionNamingCheck.cpp
  HeaderDeclarationsOnlyCheck.cpp
  MacroNamingCheck.cpp
  LibraryFunctions.cpp
  MalterlibCheck.cpp
  MalterlibOptions.cpp
  MalterlibTidyModule.cpp
  NamingUtils.cpp
  ParameterNamingCheck.cpp
  StackSegments.cpp
  StructNotClassCheck.cpp
  TypeNamingCheck.cpp
  UnusedParameterCastCheck.cpp
  VariableNamingCheck.cpp
  WorkBudget.cpp

  LINK_LIBS
  clangTidy
  clangTidyUtils

  DEPENDS
  omp_gen
  ClangDriverOptions
  )

# Declares the context functions for X/Open itself (StackSegments.cpp).
set_source_files_properties(StackSegments.cpp PROPERTIES
  SKIP_PRECOMPILE_HEADERS ON)

clang_target_link_libraries(clangTidyMalterlibModule
  PRIVATE
  clangAST
  clangASTMatchers
  clangAnalysis
  clangBasic
  clangLex
  clangSema
  clangSerialization
  )
add_subdirectory(plugin)
