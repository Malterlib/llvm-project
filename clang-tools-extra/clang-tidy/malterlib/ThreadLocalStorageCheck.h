//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_THREADLOCALSTORAGECHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_THREADLOCALSTORAGECHECK_H

#include "CoroutineUtils.h"
#include "MalterlibCheck.h"
#include "NamingUtils.h"
#include "ThreadLocalUtils.h"
#include <string>

namespace clang::tidy::malterlib {

class CThreadLocalData;

/// Finds pointers and references to thread-local data stored where they can
/// outlive the expression that stores them: in members, globals, statics,
/// objects on the heap, lambda captures, output parameters, and returns whose
/// type isn't a pointer or a reference to a thread-local type. So thread-local
/// data is only referred to from locals and parameters, which the
/// coroutine-thread-local-reference check follows.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/thread-local-storage.html
class ThreadLocalStorageCheck : public MalterlibCheck {
public:
  ThreadLocalStorageCheck(StringRef Name, ClangTidyContext *Context);
  void storeCheckOptions(ClangTidyOptions::OptionMap &Opts) override;
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus;
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
  /// An initializer outside a function, of what is of \p Type, stored in
  /// \p Where, which is \p Exempt when it may hold the data.
  struct CheckedInit {
    const Expr *Init;
    QualType Type;
    std::string Where;
    bool Exempt;
  };
  /// Checks \p Root, the body of \p Function, whose statements that can't
  /// run aren't, or \p Initializer outside a function, also the lambdas in
  /// it, as of \p Current, and the member initializers of a constructor.
  void checkRoot(const Stmt *Root, const FunctionDecl *Function,
                 const FunctionDecl *Current, const CThreadLocalData &Data,
                 const CheckedInit *Initializer = nullptr);
  /// Checks the initializer \p Init of a global, a static member or a
  /// default member initializer.
  void checkInitializer(const Expr *Init, QualType Type,
                        const std::string &Where, bool Exempt,
                        const CThreadLocalData &Data);
  /// Reports \p Value given to \p Callee, which stores it.
  void reportGiven(const Expr *Value, bool Reference,
                   const FunctionDecl *Callee, const SourceManager &SM);
  /// Reports \p Value, a pointer or a reference to thread-local data, stored
  /// in \p Where.
  void reportStore(const Expr *Value, bool Reference, const Twine &Where,
                   const SourceManager &SM);
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
  /// What is thread-local data in the translation unit checked, while it is.
  const CThreadLocalData *Classifier = nullptr;
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_THREADLOCALSTORAGECHECK_H
