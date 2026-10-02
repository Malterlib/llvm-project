//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ACTORPHASECHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ACTORPHASECHECK_H

#include "MalterlibCheck.h"
#include "NamingUtils.h"
#include "llvm/ADT/SmallVector.h"
#include <string>

namespace clang::tidy::malterlib {

/// Verifies the phase contracts of `Mib/Concurrency/TidyAnnotations`: a
/// `DMibActorPhase` function is only used where the actor is in one of its
/// phases, a `DMibActorPhaseFlag` is only ever set to `true`, and a collection
/// declared with `DMibActorEntries` only loses entries in the phases it names
/// or, for `Owner`, by the call that added them.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/actor-phase.html
class ActorPhaseCheck : public MalterlibCheck {
public:
  ActorPhaseCheck(StringRef Name, ClangTidyContext *Context);
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus;
  }
  void storeCheckOptions(ClangTidyOptions::OptionMap &Opts) override;
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override {
    Repeated.invalidate();
    References.clear();
  }
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_AsIs;
  }

private:
  void checkPhaseUse(const Expr *Use, const FunctionDecl *Function,
                     unsigned Phases, ASTContext &Context);
  void checkFlagUse(const Expr *Use, const ValueDecl *Flag,
                    ASTContext &Context);
  void checkEntriesUse(const Expr *Use, const FieldDecl *Field,
                       unsigned Phases, ASTContext &Context, int Depth,
                       const Expr *Object);
  void checkIterator(const Expr *Iterator, const FieldDecl *Field,
                     unsigned Phases, ASTContext &Context, const Expr *Object,
                     int Depth);
  void checkIteratorUses(const VarDecl *Iterator, const FieldDecl *Field,
                         unsigned Phases, ASTContext &Context,
                         const Expr *Object, int Depth,
                         const Expr *Origin = nullptr);
  void checkFlagHolderAssignment(const Expr *Assignment, const Expr *Target,
                                 ASTContext &Context);
  void checkChange(const Expr *Site, const Expr *Object, const Expr *Key,
                   const FieldDecl *Field, unsigned Phases,
                   ASTContext &Context);

  const std::string RemovingMethods;
  const std::string IteratorMethods;
  const std::string FreshKeyFunctions;
  llvm::SmallVector<StringRef, 16> RemovingMethodList;
  llvm::SmallVector<StringRef, 8> IteratorMethodList;
  llvm::SmallVector<StringRef, 4> FreshKeyFunctionList;
  /// The references to \p Variable in the body of \p Function.
  llvm::ArrayRef<const DeclRefExpr *> referencesTo(const FunctionDecl *Function,
                                                   const VarDecl *Variable);

  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
  /// The references to the variables of each function, found once for each
  /// function in the translation unit, which many aliases are looked up in.
  llvm::DenseMap<const FunctionDecl *,
                 llvm::DenseMap<const VarDecl *,
                                llvm::SmallVector<const DeclRefExpr *, 4>>>
      References;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ACTORPHASECHECK_H
