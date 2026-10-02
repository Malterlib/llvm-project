//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_CONTINUERUNNINGONACTORCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_CONTINUERUNNINGONACTORCHECK_H

#include "MalterlibCheck.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace clang::tidy::malterlib {

/// Finds uses of the object of an actor in a coroutine after
/// `co_await fg_ContinueRunningOnActor(Other)` moved it to another actor, on
/// any path until it switches back. The object belongs to its own actor, so
/// using it from another one races with it.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/continue-running-on-actor.html
class ContinueRunningOnActorCheck : public MalterlibCheck {
public:
  ContinueRunningOnActorCheck(StringRef Name, ClangTidyContext *Context);
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

  void onEndOfTranslationUnit() override { Checked.clear(); }

private:
  /// Checks \p Function, the call operator of \p Lambda when it is one.
  void checkFunction(const FunctionDecl *Function, const LambdaExpr *Lambda,
                     ASTContext &Context);
  /// Whether \p Function, the call operator of \p Lambda when it is one, can
  /// use the object of an actor.
  static bool qualifies(const FunctionDecl *Function, const LambdaExpr *Lambda);

  llvm::SmallPtrSet<const FunctionDecl *, 8> Checked;
  InstantiationIndex &Instantiations = sharedInstantiationIndex();

  const std::vector<StringRef> AllowedMemberTypes;
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_CONTINUERUNNINGONACTORCHECK_H
