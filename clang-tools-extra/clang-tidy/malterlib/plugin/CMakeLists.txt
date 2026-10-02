add_clang_library(clangTidyMalterlibPlugin STATIC
  MalterlibPlugin.cpp

  LINK_LIBS
  clangTidy
  clangTidyMalterlibModule

  DEPENDS
  omp_gen
  ClangDriverOptions
  )

clang_target_link_libraries(clangTidyMalterlibPlugin
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSema
  clangTooling
  )
