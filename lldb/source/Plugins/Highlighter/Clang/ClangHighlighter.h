//===-- ClangHighlighter.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_HIGHLIGHTER_CLANG_CLANGHIGHLIGHTER_H
#define LLDB_SOURCE_PLUGINS_HIGHLIGHTER_CLANG_CLANGHIGHLIGHTER_H

#include "lldb/Utility/Stream.h"
#include "llvm/ADT/StringSet.h"

#include "lldb/Core/Highlighter.h"
#include <optional>

#include "MalterlibClassifier.h"

namespace lldb_private {

class ClangHighlighter : public Highlighter {
  llvm::StringSet<> keywords;
  MalterlibClassifier malterlibClassifier;

public:
  ClangHighlighter();
  llvm::StringRef GetName() const override { return "clang"; }

  void Highlight(const HighlightStyle &options, llvm::StringRef line,
                 std::optional<size_t> cursor_pos,
                 llvm::StringRef previous_lines, Stream &s) const override;

  /// Returns true if the given string represents a keywords in any Clang
  /// supported language.
  bool isKeyword(llvm::StringRef token) const;

  static Highlighter *CreateInstance(lldb::LanguageType language);

  static void Terminate();
  static void Initialize();

  static llvm::StringRef GetPluginNameStatic() { return "Clang Highlighter"; }
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  HighlightStyle::ColorStyle
  highlightIdentifier(llvm::StringRef identifier,
                      HighlightStyle::ColorStyle default_style) const;
  HighlightStyle::ColorStyle
  highlightNumber(llvm::StringRef identifier,
                  HighlightStyle::ColorStyle default_style) const;
  HighlightStyle::ColorStyle
  highlightString(llvm::StringRef identifier,
                  HighlightStyle::ColorStyle default_style) const;
  HighlightStyle::ColorStyle
  highlightPunctuation(llvm::StringRef identifier,
                       HighlightStyle::ColorStyle default_style) const;
  HighlightStyle::ColorStyle
  highlightKeyword(llvm::StringRef identifier,
                   HighlightStyle::ColorStyle default_style) const;
  HighlightStyle::ColorStyle
  highlightComment(llvm::StringRef identifier,
                   HighlightStyle::ColorStyle default_style) const;
};

} // namespace lldb_private

#endif
