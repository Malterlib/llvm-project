add_lldb_library(lldbPluginClangCommon
  ClangHighlighter.cpp
  MalterlibClassifier.cpp

  LINK_COMPONENTS
    Support
  LINK_LIBS
    lldbCore
    lldbUtility
)
