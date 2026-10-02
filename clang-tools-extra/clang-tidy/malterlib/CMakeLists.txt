set(LLVM_LINK_COMPONENTS
  FrontendOpenMP
  Support
  )

add_clang_library(clangTidyMalterlibModule STATIC
  ActorAnnotations.cpp
  ActorDispatchThisCheck.cpp
  ActorPhaseCheck.cpp
  AsyncDestroyCheck.cpp
  CoawaitPrecedenceCheck.cpp
  ConceptualPrefixCheck.cpp
  ContinueRunningOnActorCheck.cpp
  CoroutineActorReferenceCheck.cpp
  CoroutineLambdaCallCheck.cpp
  CoroutineReferenceCaptureCheck.cpp
  CoroutineReferenceParameterCheck.cpp
  CoroutineUnsafeThisCheck.cpp
  CoroutineUtils.cpp
  FunctionNamingCheck.cpp
  HeaderDeclarationsOnlyCheck.cpp
  MacroNamingCheck.cpp
  MalterlibCheck.cpp
  MalterlibOptions.cpp
  MalterlibTidyModule.cpp
  NamingUtils.cpp
  NoZeroBitScanCheck.cpp
  ParameterNamingCheck.cpp
  ParameterOwnershipCheck.cpp
  RunsToCompletion.cpp
  RunsToCompletionCheck.cpp
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
