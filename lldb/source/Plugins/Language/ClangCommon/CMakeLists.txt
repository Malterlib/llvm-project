add_lldb_library(lldbPluginClangCommon
  ClangHighlighter.cpp
  MalterlibClassifier.cpp

  LINK_LIBS
    lldbCore
    lldbUtility
  LINK_COMPONENTS
    Support
)
