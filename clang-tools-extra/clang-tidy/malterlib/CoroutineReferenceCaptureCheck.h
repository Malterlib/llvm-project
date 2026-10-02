//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEREFERENCECAPTURECHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEREFERENCECAPTURECHECK_H

#include "MalterlibCheck.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"

namespace clang::tidy::malterlib {

/// Finds lambdas whose call operators are `TCFuture` coroutines and that
/// capture by reference where the captured variables can be gone: when the
/// lambda is not called directly in their scope, or uses them after a
/// suspension. A lambda that is dispatched and joined with `f_CallSync()` from
/// a function that is not a coroutine, `TCUnsafeFuture` lambdas, and the
/// cleanup functions of `fg_AsyncDestroy`, may capture by reference.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/coroutine-reference-capture.html
class CoroutineReferenceCaptureCheck : public MalterlibCheck {
public:
  CoroutineReferenceCaptureCheck(StringRef Name, ClangTidyContext *Context)
      : MalterlibCheck(Name, Context) {}
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus20;
  }
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override;
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_IgnoreUnlessSpelledInSource;
  }
private:
  /// Checks \p Lambda, whose call operator is \p Operator, the one it has or
  /// an instantiation of it, like that of a generic lambda.
  void checkLambda(const LambdaExpr *Lambda, const CXXMethodDecl *Operator,
                   const ast_matchers::MatchFinder::MatchResult &Result);
  InstantiationIndex &Instantiations = sharedInstantiationIndex();
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEREFERENCECAPTURECHECK_H
