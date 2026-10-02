//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINELAMBDACALLCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINELAMBDACALLCHECK_H

#include "MalterlibCheck.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"

namespace clang::tidy::malterlib {

/// Finds lambda coroutines that use their captures after a suspension and are
/// called directly, or whose closures are given to something that can call
/// them directly. The closure object of a direct call is often a temporary,
/// whose captures dangle after the first suspension, so these lambdas are run
/// through `fg_CallSafe`, `g_Dispatch`, `self /` or an actor functor.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/coroutine-lambda-call.html
class CoroutineLambdaCallCheck : public MalterlibCheck {
public:
  CoroutineLambdaCallCheck(StringRef Name, ClangTidyContext *Context);
  void storeCheckOptions(ClangTidyOptions::OptionMap &Opts) override;
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus20;
  }
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override {
    Instantiations.invalidate();
    Repeated.invalidate();
  }
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_AsIs;
  }

private:
  const std::vector<StringRef> SafeCallees;
  InstantiationIndex &Instantiations = sharedInstantiationIndex();
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINELAMBDACALLCHECK_H
