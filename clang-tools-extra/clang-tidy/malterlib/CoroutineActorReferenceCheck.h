//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEACTORREFERENCECHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEACTORREFERENCECHECK_H

#include "MalterlibCheck.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"
#include <optional>
#include <string>

namespace clang::tidy::malterlib {

/// Finds pointers and references into the data of an actor, looked up in a
/// coroutine of the actor, that are used after a suspension without being
/// looked up again or re-validated with `fg_OnResume`. Other calls of the
/// actor can change or erase the data while the coroutine is suspended. This
/// is a best effort check.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/coroutine-actor-reference.html
class CoroutineActorReferenceCheck : public MalterlibCheck {
public:
  CoroutineActorReferenceCheck(StringRef Name, ClangTidyContext *Context);
  void storeCheckOptions(ClangTidyOptions::OptionMap &Opts) override;
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus20;
  }
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override {
    Instantiations.invalidate();
    Repeated.invalidate();
    Assignments.clear();
  }
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_IgnoreUnlessSpelledInSource;
  }

private:
  /// What the local \p Local is made from: its initializer and what its
  /// function assigns to it, `p = &_pActor->mp_Items[1]` after `int *p =
  /// nullptr`.
  llvm::SmallVector<const Expr *, 4> valuesOf(const VarDecl *Local);
  /// Of each function looked at, what it assigns to each local, found once
  /// for all of them rather than once for each, like for each of many locals
  /// a lambda captures.
  llvm::DenseMap<
      const FunctionDecl *,
      llvm::DenseMap<const VarDecl *, llvm::SmallVector<const Expr *, 2>>>
      Assignments;
  /// Checks \p Function, the call operator of \p Lambda when it is one.
  void checkFunction(const FunctionDecl *Function, const LambdaExpr *Lambda,
                     ASTContext &Context);
  /// The phases that must be guarded for \p Variable, a lookup into a
  /// collection declared with `DMibActorEntries`, to stay valid across a
  /// suspension. None when it can't: the collection isn't annotated, isn't
  /// node-stable, or other calls can remove the entry.
  /// \p Coroutine is the coroutine that uses it: a lambda that captures it
  /// doesn't own the entry the function around it added.
  std::optional<unsigned> requiredPhases(const VarDecl *Variable,
                                         bool RangeIterator,
                                         const FunctionDecl *Coroutine,
                                         ASTContext &Context,
                                         const FieldDecl **Field) const;
  InstantiationIndex &Instantiations = sharedInstantiationIndex();
  const std::string FreshKeyFunctions;
  const std::string NodeStableContainers;
  llvm::SmallVector<StringRef, 4> FreshKeyFunctionList;
  llvm::SmallVector<StringRef, 16> NodeStableContainerList;
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEACTORREFERENCECHECK_H
