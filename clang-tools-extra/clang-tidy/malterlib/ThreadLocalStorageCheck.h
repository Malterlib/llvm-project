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

class CEffectIndex;
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
    PatternsOf = nullptr;
    CheckedDefaults.clear();
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
  /// Checks the default member initializer of \p Field where a constructor
  /// the compiler defines runs it, as an initializer of the member, \p
  /// Exempt when what it makes may hold the data; once for each place.
  void checkDefaultMember(const FieldDecl *Field, bool Exempt,
                          const CThreadLocalData &Data, SourceLocation At);
  /// The default member initializers checked (checkDefaultMember), and
  /// whether exempt.
  llvm::DenseSet<std::pair<const FieldDecl *, unsigned>> CheckedDefaults;
  /// The outermost location in code that isn't a template's that made the
  /// instantiation \p D is in, or \p Own.
  SourceLocation userLocation(const Decl *D, SourceLocation Own,
                              ASTContext &Context);
  /// Where to report what \p Value, given to what is of \p Type in \p In,
  /// does: of an instantiation of a template where it is thread-local data
  /// only by the template's arguments (fromPattern), where code that isn't
  /// a template's made the instantiation (userLocation); else \p Own.
  SourceLocation instantiatedAt(const Expr *Value, QualType Type,
                                const FunctionDecl *In, SourceLocation Own,
                                const CThreadLocalData &Data);
  /// The function and class templates of the translation unit of \p
  /// Context, and the members of class templates, once for it.
  llvm::ArrayRef<const Decl *> templatePatterns(ASTContext &Context);
  const ASTContext *PatternsOf = nullptr;
  std::vector<const Decl *> Patterns;
  /// With DebugEffects, builds the CFG and the effect index of \p Root, of
  /// \p Owner, and of the bodies of the lambdas in it, and with `dump`
  /// shows them: what each statement does with its operands, the events of
  /// the flow, and what the declarations hold.
  void debugFlow(const Stmt *Root, const Decl *Owner, ASTContext &Context);
  /// Reports \p Value given to \p Callee, which stores it, where \p Index
  /// tells it is evaluated (CEffectIndex::locationOf), not what is
  /// thread-local data only as an instantiation of a template is, as what
  /// \p Value is at \p Level (CThreadLocalData::reach), or by \p Type.
  void reportGiven(const Expr *Value, QualType Type,
                   const FunctionDecl *Callee, const CEffectIndex &Index,
                   const SourceManager &SM,
                   std::optional<unsigned> Level = std::nullopt);
  /// Reports \p Value, a pointer or a reference to thread-local data, given
  /// to what is of \p Type, stored in \p Where, where \p Index tells it is
  /// evaluated (CEffectIndex::locationOf).
  void reportStore(const Expr *Value, QualType Type, const Twine &Where,
                   const CEffectIndex &Index, const SourceManager &SM,
                   std::optional<unsigned> Level = std::nullopt);
  /// Where what is checked is reported, of what a default argument
  /// evaluates, where the use of it that is checked is; invalid otherwise.
  SourceLocation ReportAt;
  InstantiationIndex &Instantiations = sharedInstantiationIndex();
  /// The options of the thread-local checks (CThreadLocalLists), as
  /// listOption gives them.
  const StringRef ThreadLocalFunctions;
  const StringRef ThreadLocalTypes;
  const StringRef ThreadLocalScopeTypes;
  const StringRef ThreadLocalTemplates;
  const StringRef ThreadLocalHandlerTypes;
  const StringRef ScopeExitHelpers;
  /// DebugEffects, not stored: `build` builds what the flow is told by, for
  /// timing, `verify` also shows what the effect table leaves out and the
  /// bodies whose CFG can't be built, and `dump` shows all of it instead of
  /// checking, for the tests of the effect table.
  const StringRef DebugEffects;
  CThreadLocalLists Lists;
  /// What is thread-local data in the translation unit checked, while it is.
  const CThreadLocalData *Classifier = nullptr;
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_THREADLOCALSTORAGECHECK_H
