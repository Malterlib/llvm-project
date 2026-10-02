//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COAWAITPRECEDENCECHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COAWAITPRECEDENCECHECK_H

#include "MalterlibCheck.h"
#include "NamingUtils.h"

namespace clang::tidy::malterlib {

/// Finds `co_await X % "Message"`, where `%` applies to the awaited result
/// instead of to the future, since `co_await` binds tighter.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/coawait-precedence.html
class CoawaitPrecedenceCheck : public MalterlibCheck {
public:
  CoawaitPrecedenceCheck(StringRef Name, ClangTidyContext *Context)
      : MalterlibCheck(Name, Context) {}
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus20;
  }
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override { Repeated.invalidate(); }
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_AsIs;
  }
private:
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COAWAITPRECEDENCECHECK_H
