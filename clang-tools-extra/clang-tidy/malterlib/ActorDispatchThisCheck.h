//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ACTORDISPATCHTHISCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ACTORDISPATCHTHISCHECK_H

#include "MalterlibCheck.h"
#include "NamingUtils.h"

namespace clang::tidy::malterlib {

/// Finds lambdas that capture the `this` of an actor and are dispatched to
/// another actor, like `g_Dispatch(BlockingActorCheckout) / [this] {...}`.
/// The lambda runs on that actor, where using the object races with its own
/// actor.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/actor-dispatch-this.html
class ActorDispatchThisCheck : public MalterlibCheck {
public:
  ActorDispatchThisCheck(StringRef Name, ClangTidyContext *Context);
  void storeCheckOptions(ClangTidyOptions::OptionMap &Opts) override;
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus11;
  }
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override { Repeated.invalidate(); }
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_AsIs;
  }

private:
  /// Whether \p Type is of one of `OtherActorDispatchers`, also derived
  /// from one.
  bool isDispatcherType(QualType Type) const;
  /// Reports \p Lambda, which runs on another actor, \p Actor when it is
  /// known, if it captures `this` of its actor, or a pointer or reference to
  /// the object of an actor that \p Actor isn't the actor of.
  void reportCapturedThis(const LambdaExpr *Lambda, const Expr *Actor,
                          const SourceManager &SM, ASTContext &Context);

  /// Reports \p Lambda, which runs on \p Actor when it is known, if it
  /// captures a pointer or reference to, or into, the object of an actor that
  /// \p Actor isn't the actor of. With \p OnOwnActor, it runs on the current
  /// actor, whose object a parameter given it is, like `_pThis`.
  void reportCapturedActorObjects(const LambdaExpr *Lambda, const Expr *Actor,
                                  const SourceManager &SM, bool OnOwnActor);

  /// Reports \p Argument, given to a function that runs on another actor, if
  /// it points to, or into, the object of the own actor, but not when it is
  /// the object of \p Actor, the actor the function runs on.
  void reportGivenObject(const Expr *Argument, const SourceManager &SM,
                         ASTContext &Context, const Expr *Actor = nullptr);

  /// Reports what \p Call gives a free or static member function for a
  /// pointer or reference to the object of an actor, which it uses like
  /// `this`, unless that is the object of the actor the call runs on.
  void reportGivenActorObjects(const CallExpr *Call, const SourceManager &SM,
                               ASTContext &Context);

  /// Reports what constructs a helper of an actor, \p Helper, which derives
  /// from `CActorInternal` or `CAllowUnsafeThis` and uses the object of the
  /// actor it is given like `this`, with \p Arguments that point or refer to
  /// the object of an actor other than the one it runs on.
  void reportHelperConstruction(const CXXRecordDecl *Helper,
                                llvm::ArrayRef<const Expr *> Arguments,
                                const SourceManager &SM, ASTContext &Context);

  /// Reports \p Lambda, which runs on the own actor \p Actor, if it captures
  /// the object of another actor.
  void reportOnOwnActor(const LambdaExpr *Lambda, const Expr *Actor,
                        const SourceManager &SM);

  /// Reports \p Lambda if it captures `this` of its actor, and whether it
  /// did.
  bool reportCapturedThisOfActor(const LambdaExpr *Lambda,
                                 const SourceManager &SM, ASTContext &Context);

  const std::vector<StringRef> OtherActorDispatchers;
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ACTORDISPATCHTHISCHECK_H
