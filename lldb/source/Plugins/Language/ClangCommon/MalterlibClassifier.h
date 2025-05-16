//===-- MalterlibClassifier.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_CLANGCOMMON_MALTERLIBCLASSIFIER_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_CLANGCOMMON_MALTERLIBCLASSIFIER_H

#include "lldb/Utility/Stream.h"
#include "llvm/ADT/StringSet.h"

#include "lldb/Core/Highlighter.h"

#include <memory>
#include <optional>

namespace lldb_private {

class MalterlibClassifier {
  class Internal;

  std::unique_ptr<Internal> internal;

public:
  MalterlibClassifier();
  ~MalterlibClassifier();

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

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_CLANGCOMMON_MALTERLIBCLASSIFIER_H
