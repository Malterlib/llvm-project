//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEREFERENCEPARAMETERCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEREFERENCEPARAMETERCHECK_H

#include "MalterlibCheck.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"

namespace clang::tidy::malterlib {

/// Finds parameters of `TCFuture` coroutines that refer to data the caller
/// owns, like references, pointers, spans and views, and that are used after a
/// suspension, when the data can be gone, and reference parameters of the
/// member functions of actors, which actor calls can't pass. Pointers and
/// references to actors are used like `this`. This is a best effort check.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/coroutine-reference-parameter.html
class CoroutineReferenceParameterCheck : public MalterlibCheck {
public:
  CoroutineReferenceParameterCheck(StringRef Name, ClangTidyContext *Context);
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
  bool isViewType(QualType Type, const ASTContext &Context) const;
  const Stmt *borrowedUse(const FunctionDecl *Function,
                          const ParmVarDecl *Parameter,
                          ASTContext &Context) const;
  /// Checks the parameters of \p Function, which isn't a template.
  void checkFunction(const FunctionDecl *Function, ASTContext &Context);

  InstantiationIndex &Instantiations = sharedInstantiationIndex();

  const std::vector<StringRef> ViewTypes;
  const bool CheckPointers;
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEREFERENCEPARAMETERCHECK_H
