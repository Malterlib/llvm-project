//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CoroutineLambdaCallCheck.h"
#include "../utils/OptionsUtils.h"
#include "CoroutineUtils.h"
#include "ExprEffects.h"
#include "NamingUtils.h"
#include "RunsToCompletion.h"
#include "StorageModel.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ExprCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

CoroutineLambdaCallCheck::CoroutineLambdaCallCheck(StringRef Name,
                                                   ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      SafeCallees(utils::options::parseStringList(
          listOption("SafeCallees", CVocabulary::builtIn("SafeCallees")))) {}

void CoroutineLambdaCallCheck::storeCheckOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "SafeCallees",
                utils::options::serializeStringList(SafeCallees));
}

void CoroutineLambdaCallCheck::registerMatchers(MatchFinder *Finder) {
  // Whether it is evaluated is told in `check`, for a lambda coroutine,
  // since telling it walks the parents.
  Finder->addMatcher(
      cxxOperatorCallExpr(hasOverloadedOperatorName("()")).bind("call"), this);
  Finder->addMatcher(lambdaExpr().bind("lambda"),
                     this);
}

/// The first use after a suspension of the captures of the lambda whose call
/// operator is \p Operator, when it is a protected coroutine whose closure
/// they live in or refer through. A lambda without captures has no state to
/// dangle, and neither has one with an explicit object parameter taken by
/// value, `this auto Self`, whose copy of the closure lives in the coroutine
/// frame.
static const Stmt *danglingCaptureUse(const CXXMethodDecl *Operator,
                                      ASTContext &Context,
                                      CLifetimeUse *Found = nullptr) {
  if (Operator->getParent()->captures().empty() ||
      !isProtectedCoroutine(Operator) ||
      (Operator->isExplicitObjectMemberFunction() &&
       !Operator->getParamDecl(0)->getType()->isReferenceType()))
    return nullptr;
  return firstCaptureUseAfterSuspension(Operator, Context, Found);
}

/// Where the result of the lambda of the call operator \p Operator refers to
/// what it captured by copy, which lives in its closure (copyInResult): the
/// copies it captures by copy, the copy of the closure an explicit object
/// parameter taken by value is, `[Value = 1](this auto Self)`, which the
/// frame holds, and the copy of the object of `[*this]`.
static const Stmt *capturedAddressInResult(const CXXMethodDecl *Operator) {
  const CXXRecordDecl *Closure = Operator->getParent();
  if (!Operator->getBody())
    return nullptr;
  llvm::SmallPtrSet<const ValueDecl *, 4> Copies;
  for (const LambdaCapture &Capture : Closure->captures())
    if (Capture.capturesVariable() && Capture.getCaptureKind() == LCK_ByCopy)
      Copies.insert(Capture.getCapturedVar());
  if (Operator->isExplicitObjectMemberFunction() &&
      !Operator->getParamDecl(0)->getType()->isReferenceType())
    Copies.insert(Operator->getParamDecl(0));
  const bool CopiesThis =
      llvm::any_of(Closure->captures(), [](const LambdaCapture &Capture) {
        return Capture.capturesThis() &&
               Capture.getCaptureKind() == LCK_StarThis;
      });
  if (Copies.empty() && !CopiesThis)
    return nullptr;
  ASTContext &Context = Operator->getASTContext();
  return copyInResult(Operator, [&](const Expr *At) {
    const CEffects &Effects = effectsOf(At, Context);
    if (Effects.Statement == EStatement::This)
      return CopiesThis;
    return Effects.Statement == EStatement::Name &&
           Copies.contains(cast<DeclRefExpr>(At)->getDecl());
  });
}

/// The call operator of \p Operators that the direct call \p Site runs: the
/// callee of a call, `Lambda.operator()<false>()`, or the member a call
/// operator named explicitly is, or null when it calls something else, like a
/// function wrapper made from the closure.
static const CXXMethodDecl *
invokedOperatorOf(const Expr *Site,
                  llvm::ArrayRef<const CXXMethodDecl *> Operators) {
  const Decl *Called = nullptr;
  if (const auto *Call = dyn_cast<CallExpr>(Site))
    Called = Call->getCalleeDecl();
  else if (const auto *Member = dyn_cast<MemberExpr>(Site))
    Called = Member->getMemberDecl();
  if (!Called)
    return nullptr;
  for (const CXXMethodDecl *Operator : Operators)
    if (Operator->getCanonicalDecl() == Called->getCanonicalDecl())
      return Operator;
  return nullptr;
}

/// The reports of the check that say the captures dangle where they are
/// used (reportLifetime).
static constexpr llvm::StringLiteral CalledDirectly =
    "a lambda coroutine that uses its captures after a suspension is called "
    "directly; the captures dangle when the closure is a temporary, so run it "
    "through fg_CallSafe, g_Dispatch, 'self /' or an actor functor";
static constexpr llvm::StringLiteral GivenToCaller =
    "the closure of a lambda coroutine that uses its captures after a "
    "suspension is given to %0, which can call it directly; give it only to "
    "fg_CallSafe, g_Dispatch, 'self /', an actor functor or another function "
    "that keeps it alive while it runs";
/// The report of a result that refers to a temporary of the coroutine's body
/// (temporaryInResult), whoever runs it.
static constexpr llvm::StringLiteral TemporaryInResult =
    "a lambda coroutine returns what refers to a temporary of its body, which "
    "is gone once it returns; return a copy";
static constexpr llvm::StringLiteral CaptureUsedHere =
    "a capture is used here after a suspension, or in a way that keeps a "
    "reference to it";

void CoroutineLambdaCallCheck::reportCaptureUse(
    const Stmt *Use, const CLifetimeUse &Found, SourceLocation Loc,
    StringRef Message,
    llvm::function_ref<void(const MalterlibDiagnostic &)> Arguments,
    ASTContext &Context) {
  if (Found.Use) {
    reportLifetime(*this, Found, Context,
                   {Loc, Message, Arguments, CaptureUsedHere});
    return;
  }
  // What the result keeps, told in the context of the result (resultUses),
  // not of one use.
  {
    const MalterlibDiagnostic Warning = diag(Loc, Message);
    if (Arguments)
      Arguments(Warning);
  }
  diag(Use->getBeginLoc(), CaptureUsedHere, DiagnosticIDs::Note);
}

void CoroutineLambdaCallCheck::check(const MatchFinder::MatchResult &Result) {
  // Nothing in a translation unit with an error that stops it from compiling
  // (hasUncompilableErrors): no flow is built there, and what the errors leave
  // can't be relied on.
  if (hasUncompilableErrors(*Result.Context))
    return;
  if (Repeated.repeated(Result))
    return;
  const SourceManager &SM = *Result.SourceManager;
  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  for (const auto &[Id, Node] : Result.Nodes.getMap())
    if (isInTemplatePattern(Node, *Result.Context))
      return;
  if (const auto *Call = Result.Nodes.getNodeAs<CXXOperatorCallExpr>("call")) {
    const auto *Operator =
        dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
    // Not one that never runs, in a branch an `if constexpr` discards or a
    // constant condition never takes.
    if (!Operator || !isLambdaCallOperator(Operator) ||
        !isCoroutine(Operator) ||
        isInUnevaluatedOperand(DynTypedNode::create(*Call), *Result.Context) ||
        isInDiscardedBranch(DynTypedNode::create(*Call), *Result.Context) ||
        isInNeverTakenBranch(DynTypedNode::create(*Call), *Result.Context))
      return;
    // Only a temporary closure, or one in a local variable, is the call's
    // own. A call of a copy that a lambda around captured is reported where
    // that lambda captures it. So is one given as a parameter,
    // `_fFunction(1)`, or one held elsewhere, in a member,
    // `State.m_Function()`, or through a pointer, `(*pFunction)()`, like a
    // function wrapper holds it, where it is given.
    const Expr *Closure = Call->getArg(0)->IgnoreParenImpCasts();
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Closure)) {
      const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
      if (!Variable || Ref->refersToEnclosingVariableOrCapture() ||
          isa<ParmVarDecl>(Variable) || !Variable->hasLocalStorage())
        return;
    } else if (!Closure->isPRValue() &&
               !isa<MaterializeTemporaryExpr>(Closure))
      return;
    CLifetimeUse Found;
    const Stmt *Use = danglingCaptureUse(Operator, *Result.Context, &Found);
    // One that takes its closure by value, `this auto Self`, keeps the
    // captures in its frame, but not for what its result refers to.
    if (!Use && Operator->isExplicitObjectMemberFunction() &&
        !Operator->getParamDecl(0)->getType()->isReferenceType() &&
        isProtectedCoroutine(Operator))
      // The call operator called, of a generic one the specialization, which
      // tells what its helpers return.
      if (capturedAddressInResult(Operator)) {
        const SourceLocation Loc =
            reportLocationIn(Call->getBeginLoc(), Call->getEndLoc(), SM);
        if (Loc.isValid())
          diag(Loc, "a lambda coroutine returns what refers to its captures, "
                    "which are gone once its frame is; return a copy");
        return;
      }
    if (!Use)
      return;
    // A temporary closure whose call code that runs to completion awaits in
    // its full expression lives until it is joined there, which is never
    // aborted, as coroutine-reference-capture tells of the same full
    // expression (joinOf): what the coroutine reads of its captures while it
    // runs is there; what it keeps of them past the use can outlive it.
    if (!isa<DeclRefExpr>(Closure) && Found.Used == EUsed::Designated &&
        inRunsToCompletionContext(Call, *Result.Context, /*AfterSwitch=*/true))
      if (const std::optional<RunsToCompletionJoin> Join =
              joinOf(Call, *Result.Context);
          Join && !Join->Guard)
        return;
    const SourceLocation Loc = reportLocationIn(Call->getBeginLoc(), Call->getEndLoc(), SM);
    if (Loc.isInvalid())
      return;
    reportCaptureUse(Use, Found, Loc, CalledDirectly, {}, *Result.Context);
    return;
  }

  // Other uses of the closure, which can call it directly or keep it too
  // short.
  const auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda");
  // Its call operator, or of a generic lambda, each specialization of it.
  const llvm::SmallVector<const CXXMethodDecl *, 2> Operators =
      runningOperators(Lambda);
  // What the result keeps of the captures, like `co_return &Value`, also
  // through a local, `auto *pValue = &Value; co_return pValue;`, or a view
  // made of it, outlives the closure also when what runs it keeps it alive
  // while it runs.
  auto ResultUseOf = [&](const CXXMethodDecl *Operator) -> const Stmt * {
    return isProtectedCoroutine(Operator) ? capturedAddressInResult(Operator)
                                          : nullptr;
  };
  // What the result refers to of a temporary of the body is gone when the
  // coroutine returns, however it is run; once, of the first specialization
  // that returns one, where the lambda is made: not in what isn't evaluated
  // or never runs, which is asked only of one that returns such a temporary.
  for (const CXXMethodDecl *Operator : Operators)
    if (const Expr *Temporary = isProtectedCoroutine(Operator)
                                    ? temporaryInResult(Operator)
                                    : nullptr) {
      if (!isInUnevaluatedOperand(DynTypedNode::create(*Lambda),
                                  *Result.Context) &&
          !isInDiscardedBranch(DynTypedNode::create(*Lambda), *Result.Context) &&
          !isInNeverTakenBranch(DynTypedNode::create(*Lambda), *Result.Context))
        diag(Temporary->getBeginLoc(), TemporaryInResult);
      break;
    }
  // Of any specialization, for a consumer that can call any of them.
  const Stmt *AnyUse = nullptr;
  CLifetimeUse AnyFound;
  for (const CXXMethodDecl *Operator : Operators)
    if ((AnyUse = danglingCaptureUse(Operator, *Result.Context, &AnyFound)))
      break;
  const Stmt *AnyResultUse = nullptr;
  for (const CXXMethodDecl *Operator : Operators)
    if ((AnyResultUse = ResultUseOf(Operator)))
      break;
  if ((!AnyUse && !AnyResultUse) ||
      isInUnevaluatedOperand(DynTypedNode::create(*Lambda), *Result.Context) ||
      isInDiscardedBranch(DynTypedNode::create(*Lambda), *Result.Context) ||
      isInNeverTakenBranch(DynTypedNode::create(*Lambda), *Result.Context))
    return;
  llvm::SmallVector<ClosureConsumer, 2> Consumers =
      closureConsumers(Lambda, *Result.Context, SafeCallees);
  giveUnlistedCallSafe(Consumers, SafeCallees);
  for (const ClosureConsumer &Consumer : Consumers) {
    // Not where it doesn't run, `if constexpr (false) fFunction();`.
    if (const DynTypedNode Site = DynTypedNode::create(*Consumer.Site);
        isInUnevaluatedOperand(Site, *Result.Context) ||
        isInDiscardedBranch(Site, *Result.Context) ||
        isInNeverTakenBranch(Site, *Result.Context))
      continue;
    // A call of a call operator of the lambda, `fTask.operator()<false>()`,
    // runs the specialization it names; what else it is given to can run any.
    const CXXMethodDecl *Invoked =
        Consumer.Use == ClosureUse::DirectCall
            ? invokedOperatorOf(Consumer.Site, Operators)
            : nullptr;
    CLifetimeUse Found = AnyFound;
    const Stmt *Use =
        Invoked ? danglingCaptureUse(Invoked, *Result.Context, &Found) : AnyUse;
    const Stmt *ResultUse = Invoked ? ResultUseOf(Invoked) : AnyResultUse;
    const bool InResult = ResultUse;
    // A direct call through a dependent type, in a template, an explicit
    // call of the call operator, or a call through a function wrapper, like
    // `fFunction()` of a `TCFunction` made from it, which the matcher of calls
    // of the lambda's call operator doesn't see.
    const auto *OperatorCall = dyn_cast<CXXOperatorCallExpr>(Consumer.Site);
    const auto *CallOperator =
        OperatorCall
            ? dyn_cast_or_null<CXXMethodDecl>(OperatorCall->getDirectCallee())
            : nullptr;
    if (Consumer.Use == ClosureUse::DirectCall && Use &&
        !(CallOperator && isLambdaCallOperator(CallOperator))) {
      const SourceLocation Loc =
          reportLocationIn(Consumer.Site->getBeginLoc(),
                         Consumer.Site->getEndLoc(), SM);
      if (Loc.isInvalid())
        continue;
      reportCaptureUse(Use, Found, Loc, CalledDirectly, {}, *Result.Context);
      continue;
    }
    // One that takes its closure by value, called directly other than by the
    // call operator matcher, like `Task.operator()()`, keeps the captures in
    // its frame, but not for what its result refers to.
    const CXXMethodDecl *LambdaOperator = Lambda->getCallOperator();
    if (Consumer.Use == ClosureUse::DirectCall && !Use && InResult &&
        !(CallOperator && isLambdaCallOperator(CallOperator)) &&
        LambdaOperator->isExplicitObjectMemberFunction() &&
        !LambdaOperator->getParamDecl(0)->getType()->isReferenceType()) {
      const SourceLocation Loc = reportLocationIn(
          Consumer.Site->getBeginLoc(), Consumer.Site->getEndLoc(), SM);
      if (Loc.isValid())
        diag(Loc, "a lambda coroutine returns what refers to its captures, "
                  "which are gone once its frame is; return a copy");
      continue;
    }
    if (InResult && (Consumer.Use == ClosureUse::SafeCallee ||
                     Consumer.Use == ClosureUse::CallSafe)) {
      Use = ResultUse;
      const SourceLocation Loc = reportLocationIn(
          Consumer.Site->getBeginLoc(), Consumer.Site->getEndLoc(), SM);
      if (Loc.isInvalid())
        continue;
      diag(Loc, "a lambda coroutine returns what refers to its captures, "
                "which are gone once what it is given to has run it; return "
                "a copy");
      diag(Use->getBeginLoc(), CaptureUsedHere, DiagnosticIDs::Note);
      continue;
    }
    if (Consumer.Use != ClosureUse::Escape || !Use)
      continue;
    const SourceLocation Loc =
        reportLocationIn(Consumer.Site->getBeginLoc(),
                         Consumer.Site->getEndLoc(), SM);
    if (Loc.isInvalid())
      continue;
    auto Arguments = [&](const MalterlibDiagnostic &Diag) {
      Diag << Consumer.Target;
    };
    reportCaptureUse(Use, Found, Loc, GivenToCaller, Arguments,
                     *Result.Context);
  }
}

} // namespace clang::tidy::malterlib
