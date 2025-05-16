add_lldb_library(lldbPluginHighlighterClang PLUGIN
  ClangHighlighter.cpp
  MalterlibClassifier.cpp

  LINK_COMPONENTS
    Support
  LINK_LIBS
    lldbCore
    lldbUtility
)
