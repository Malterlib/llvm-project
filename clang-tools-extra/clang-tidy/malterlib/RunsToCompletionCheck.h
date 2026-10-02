//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_RUNSTOCOMPLETIONCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_RUNSTOCOMPLETIONCHECK_H

#include "MalterlibCheck.h"
#include "NamingUtils.h"

namespace clang::tidy::malterlib {

/// Verifies the contract of `DMibRunsToCompletion`: a function that runs to
/// completion is only started where code runs to completion, and joined there
/// before what its arguments refer to is gone; it doesn't switch to an actor
/// that may be destroyed; and a `DMibRunsToCompletionEntry` switches to a
/// concurrent actor before it first suspends.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/runs-to-completion.html
class RunsToCompletionCheck : public MalterlibCheck {
public:
  RunsToCompletionCheck(StringRef Name, ClangTidyContext *Context)
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
  void checkUse(const Expr *Use, const FunctionDecl *Function,
                ASTContext &Context);
  /// Checks \p Call of \p Function, which runs to completion: that it is
  /// called where the code runs to completion, on a concurrent actor, and
  /// joined.
  void checkCall(const CallExpr *Call, const FunctionDecl *Function,
                 ASTContext &Context);
  void checkEntry(const FunctionDecl *Function, ASTContext &Context);
  void checkOverride(const FunctionDecl *Function);
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_RUNSTOCOMPLETIONCHECK_H
