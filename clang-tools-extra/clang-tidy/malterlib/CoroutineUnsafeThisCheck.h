//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEUNSAFETHISCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEUNSAFETHISCHECK_H

#include "MalterlibCheck.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"

namespace clang::tidy::malterlib {

/// Finds `TCFuture` coroutines that use `this` of a class that does not
/// derive from `CActor`, `CActorInternal` or `CAllowUnsafeThis`, whose
/// object may be gone when the coroutine resumes: member functions that use
/// it after a suspension, and lambdas that capture it, unless they are called
/// directly and use it only before their first suspension, are given only to a
/// test category (BlockingCallees), or keep the object alive with a shared
/// pointer; one joined with `f_CallSync()` isn't exempt.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/coroutine-unsafe-this.html
class CoroutineUnsafeThisCheck : public MalterlibCheck {
public:
  CoroutineUnsafeThisCheck(StringRef Name, ClangTidyContext *Context);
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
  void checkMethod(const CXXMethodDecl *Method, ASTContext &Context);
  /// Whether the result of a call of a member function annotated to end
  /// before its object is a local declared after the object, a local, in the
  /// same block, which ends it first.
  void checkResultEndsBeforeObject(const CallExpr *Call,
                                   ASTContext &Context);
  /// Checks \p Lambda, whose call operator is \p Operator, the one it has or
  /// an instantiation of it, like that of a generic lambda.
  void checkLambda(const LambdaExpr *Lambda, const CXXMethodDecl *Operator,
                   ASTContext &Context);
  void reportLambda(const LambdaExpr *Lambda, const LambdaCapture &Capture,
                    const CXXRecordDecl *Record, const SourceManager &SM);
  InstantiationIndex &Instantiations = sharedInstantiationIndex();
  /// The futures, `TCFuture`, of the FutureTypes option.
  const StringRef FutureTypes;
  llvm::SmallVector<StringRef, 8> FutureTypeList;
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEUNSAFETHISCHECK_H
