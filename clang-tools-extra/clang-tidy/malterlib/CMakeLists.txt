set(LLVM_LINK_COMPONENTS
  FrontendOpenMP
  Support
  )

add_clang_library(clangTidyMalterlibModule STATIC
  ConceptualPrefixCheck.cpp
  FunctionNamingCheck.cpp
  MacroNamingCheck.cpp
  MalterlibCheck.cpp
  MalterlibTidyModule.cpp
  NamingUtils.cpp
  ParameterNamingCheck.cpp
  TypeNamingCheck.cpp
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
