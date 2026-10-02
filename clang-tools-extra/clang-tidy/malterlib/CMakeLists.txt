set(LLVM_LINK_COMPONENTS
  BitstreamReader
  FrontendOpenMP
  Support
  )

add_clang_library(clangTidyMalterlibModule STATIC
  ActorAnnotations.cpp
  ActorDispatchThisCheck.cpp
  ActorPhaseCheck.cpp
  AsyncDestroyCheck.cpp
  CheckedAccess.cpp
  CoawaitPrecedenceCheck.cpp
  ConceptualPrefixCheck.cpp
  ContinueRunningOnActorCheck.cpp
  CoroutineActorReferenceCheck.cpp
  CoroutineLambdaCallCheck.cpp
  CoroutineReferenceCaptureCheck.cpp
  CoroutineReferenceParameterCheck.cpp
  CoroutineThreadLocalReferenceCheck.cpp
  CoroutineUnsafeThisCheck.cpp
  CoroutineUtils.cpp
  ExprEffects.cpp
  FixIts.cpp
  FunctionNamingCheck.cpp
  HeaderDeclarationsOnlyCheck.cpp
  HeldData.cpp
  LibraryFunctions.cpp
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
  StackSegments.cpp
  StorageModel.cpp
  StructNotClassCheck.cpp
  ThreadLocalStorageCheck.cpp
  ThreadLocalUtils.cpp
  TypeNamingCheck.cpp
  UnusedParameterCastCheck.cpp
  ValueFlow.cpp
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
