//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINETHREADLOCALREFERENCECHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINETHREADLOCALREFERENCECHECK_H

#include "CoroutineUtils.h"
#include "MalterlibCheck.h"
#include "NamingUtils.h"
#include "ThreadLocalUtils.h"

namespace clang::tidy::malterlib {

class CThreadLocalData;

/// Finds locals, parameters and captures of a coroutine that refer to the
/// thread-local data of the thread that runs it, used after a suspension,
/// when the coroutine can resume on another thread, and scopes that keep it
/// alive across one. What refers to it is told within the function only:
/// thread-local-storage keeps it from being stored anywhere else.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/coroutine-thread-local-reference.html
class CoroutineThreadLocalReferenceCheck : public MalterlibCheck {
public:
  CoroutineThreadLocalReferenceCheck(StringRef Name, ClangTidyContext *Context);
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
    return TK_IgnoreUnlessSpelledInSource;
  }

private:
  /// Checks the coroutine \p Function, the call operator of \p Lambda when it
  /// is one.
  void checkFunction(const FunctionDecl *Function, const LambdaExpr *Lambda,
                     const CThreadLocalData &Data);
  InstantiationIndex &Instantiations = sharedInstantiationIndex();
  /// The options of the thread-local checks (CThreadLocalLists), as
  /// listOption gives them.
  const StringRef ThreadLocalFunctions;
  const StringRef ThreadLocalTypes;
  const StringRef ThreadLocalScopeTypes;
  const StringRef ThreadLocalTemplates;
  const StringRef ThreadLocalHandlerTypes;
  const StringRef ScopeExitHelpers;
  CThreadLocalLists Lists;
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINETHREADLOCALREFERENCECHECK_H
