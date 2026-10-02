//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ASYNCDESTROYCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ASYNCDESTROYCHECK_H

#include "MalterlibCheck.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "NamingUtils.h"

namespace clang::tidy::malterlib {

/// Checks the uses of `fg_AsyncDestroy`: its awaiter is `co_await`ed, the
/// result is kept in a named variable that is not moved elsewhere, its cleanup
/// function uses the references it captures only before its first suspension,
/// and coroutines tear down actors and functors with it instead of with
/// `g_OnScopeExit`.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/async-destroy.html
class AsyncDestroyCheck : public MalterlibCheck {
public:
  AsyncDestroyCheck(StringRef Name, ClangTidyContext *Context)
      : MalterlibCheck(Name, Context) {}
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus20;
  }
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override {
    Repeated.invalidate();
    Indices.clear();
  }
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_AsIs;
  }

private:
  void checkAsyncDestroy(const CallExpr *Call, ASTContext &Context);
  void checkCleanup(const LambdaExpr *Cleanup, ASTContext &Context);
  void checkScopeExit(const Expr *Divide, const Expr *Left, const Expr *Right,
                      ASTContext &Context);
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
  /// What a function has that the results in it are looked up in, made once
  /// for it however many results it has: its local references and pointers,
  /// and the variables it names.
  struct CFunctionIndex {
    llvm::SmallVector<const VarDecl *, 8> References;
    llvm::SmallVector<const VarDecl *, 8> Pointers;
    llvm::DenseSet<const ValueDecl *> Named;
  };
  const CFunctionIndex &indexOf(const FunctionDecl *Function);
  llvm::DenseMap<const FunctionDecl *, CFunctionIndex> Indices;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ASYNCDESTROYCHECK_H
