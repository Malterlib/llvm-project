//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CoroutineReferenceCaptureCheck.h"
#include "CheckedAccess.h"
#include "RunsToCompletion.h"
#include "CoroutineUtils.h"
#include "HeldData.h"
#include "StorageModel.h"
#include "NamingUtils.h"
#include "ValueFlow.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/ScopeExit.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

void CoroutineReferenceCaptureCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(lambdaExpr().bind("lambda"), this);
}

static const llvm::ArrayRef<StringRef> AsyncDestroyNames(AsyncDestroyFunctions);

/// Whether \p E, through parentheses, implicit nodes and `fg_Move`, is the
/// cleanup argument of a call of `fg_AsyncDestroy`.
static bool isAsyncDestroyArgument(const Expr *E, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*E);
  for (int Depth = 0; Depth < 8; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    if (const auto *Call = Parents[0].get<CallExpr>()) {
      if (Call->getNumArgs() == 1 &&
          asCallTo(Call, vocabulary().MoveFunctions.names())) {
        Node = Parents[0];
        continue;
      }
      // A guard that is returned, or moved elsewhere, runs the cleanup after
      // the variables are gone.
      return asCallTo(Call, AsyncDestroyNames) && Call->getNumArgs() >= 1 &&
             Call->getArg(0)->IgnoreUnlessSpelledInSource() ==
                 Node.get<Expr>()->IgnoreUnlessSpelledInSource() &&
             isCleanupJoined(Call, Context);
    }
    if (!Parents[0].get<Expr>())
      return false;
    Node = Parents[0];
  }
  return false;
}

/// Whether every reference to the local \p Variable is the cleanup argument
/// of `fg_AsyncDestroy`, and there is one.
static bool onlyAsyncDestroyed(const VarDecl *Variable, ASTContext &Context) {
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return false;
  const auto Refs =
      matchEach<DeclRefExpr>(declRefExpr(to(varDecl(equalsNode(Variable))),
                                         unless(isInUnevaluatedContext()))
                                 .bind("ref"),
                             *Function->getBody(), Context);
  return !Refs.empty() && llvm::all_of(Refs, [&](const BoundNodes &Nodes) {
    const auto *Ref = Nodes.getNodeAs<DeclRefExpr>("ref");
    // What a lambda captures it with isn't a use; what its body does with it
    // is.
    const auto Parents = instantiatedParents(*Ref, Context);
    if (!Parents.empty() && Parents[0].get<LambdaExpr>())
      return true;
    return isAsyncDestroyArgument(Ref, Context);
  });
}

/// Whether \p Lambda is the cleanup function of `fg_AsyncDestroy`, which starts
/// it when its guard is destroyed, while the variables declared before the
/// guard are there. It is passed directly, through a local, like
/// `fg_AsyncDestroy(fg_Move(fCleanup))`, or made by a factory lambda that every
/// call of passes to it, like `fg_AsyncDestroy(fMakeCleanup(Location))`. The
/// arguments the factory's reference parameters, which the cleanup captures,
/// are bound to must be variables, not temporaries that are gone by then.
static bool isAsyncDestroyCleanup(const LambdaExpr *Lambda,
                                  ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Lambda);
  for (int Depth = 0; Depth < 8; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    if (Parents[0].get<CallExpr>())
      return isAsyncDestroyArgument(Node.get<Expr>(), Context);
    if (const auto *Variable = Parents[0].get<VarDecl>())
      return Variable->hasLocalStorage() && onlyAsyncDestroyed(Variable, Context);
    if (Parents[0].get<ReturnStmt>())
      break;
    if (!Parents[0].get<Expr>())
      return false;
    Node = Parents[0];
  }
  // Returned by a factory lambda held in a local, which isn't a coroutine.
  const auto *FactoryOperator =
      dyn_cast<CXXMethodDecl>(Lambda->getLambdaClass()->getDeclContext());
  if (!FactoryOperator || !FactoryOperator->getParent()->isLambda() ||
      isCoroutine(FactoryOperator))
    return false;
  const LambdaExpr *Factory = nullptr;
  for (DynTypedNode Up = DynTypedNode::create(*Lambda);;) {
    const auto Parents = instantiatedParents(Up, Context);
    if (Parents.empty())
      return false;
    Up = Parents[0];
    // Also of a generic one, whose call operator is a template that the
    // factory's is an instantiation of.
    if (const auto *Outer = Up.get<LambdaExpr>();
        Outer && Outer->getLambdaClass() == FactoryOperator->getParent()) {
      Factory = Outer;
      break;
    }
  }
  const auto FactoryParents = instantiatedParents(*Factory, Context);
  const auto *Holder =
      FactoryParents.empty() ? nullptr : FactoryParents[0].get<VarDecl>();
  const auto *Function =
      Holder ? dyn_cast_or_null<FunctionDecl>(Holder->getParentFunctionOrMethod())
             : nullptr;
  if (!Holder || !Holder->hasLocalStorage() || !Function || !Function->getBody())
    return false;
  // The factory's reference parameters that the cleanup captures.
  llvm::SmallVector<unsigned, 4> CapturedParameters;
  for (const LambdaCapture &Capture : Lambda->captures()) {
    if (const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(
            Capture.capturesVariable() ? Capture.getCapturedVar() : nullptr);
        Parameter && Parameter->getDeclContext() == FactoryOperator)
      CapturedParameters.push_back(Parameter->getFunctionScopeIndex());
  }
  const auto Refs = matchEach<DeclRefExpr>(
      declRefExpr(to(varDecl(equalsNode(Holder)))).bind("ref"),
      *Function->getBody(), Context);
  return !Refs.empty() && llvm::all_of(Refs, [&](const BoundNodes &Nodes) {
    const auto *Ref = Nodes.getNodeAs<DeclRefExpr>("ref");
    const auto Parents = instantiatedParents(*Ref, Context);
    // What a lambda captures the factory with isn't a call of it; what its
    // body does with it is.
    if (!Parents.empty() && Parents[0].get<LambdaExpr>())
      return true;
    const auto *Call =
        Parents.empty() ? nullptr : Parents[0].get<CXXOperatorCallExpr>();
    if (!Call || Call->getOperator() != OO_Call ||
        Call->getArg(0)->IgnoreParenImpCasts() != Ref ||
        !isAsyncDestroyArgument(Call, Context))
      return false;
    // After the closure, the object of the factory's call operator, which an
    // explicit object parameter is given (argumentShift).
    const unsigned Shift = argumentShift(Call, FactoryOperator);
    return llvm::all_of(CapturedParameters, [&](unsigned iParameter) {
      if (iParameter + Shift >= Call->getNumArgs())
        return false;
      const auto *Argument = dyn_cast<DeclRefExpr>(
          Call->getArg(iParameter + Shift)->IgnoreParenImpCasts());
      const auto *Variable =
          Argument ? dyn_cast<VarDecl>(Argument->getDecl()) : nullptr;
      return Variable && (Variable->hasLocalStorage() ||
                          Variable->hasGlobalStorage());
    });
  });
}

/// Whether the reference parameter \p Variable is to a const array of
/// characters, `char const (&)[N]` of any character type, also deduced from
/// `auto const &`: what it designates is taken to be a string literal, of
/// static storage.
static bool designatesStringLiteral(const VarDecl *Variable,
                                    const ASTContext &Context) {
  if (!isa<ParmVarDecl>(Variable) || !Variable->getType()->isReferenceType())
    return false;
  const ArrayType *Array = Context.getAsArrayType(
      Variable->getType().getNonReferenceType().getCanonicalType());
  return Array && Array->getElementType().isConstQualified() &&
         Array->getElementType()->isAnyCharacterType();
}

/// The variable whose storage what \p Value, at \p Level, refers to lives in,
/// which is gone before the closure is, by provenance (storageRootsOf): a
/// local or a parameter held by value (Frame), also an extended temporary of
/// the local it extends, the local owner what an owner owns is reached
/// through, not past what it only refers to (Owned), what a reference
/// parameter refers to (Parameter), a variable of a thread (ThreadLocal), and
/// a temporary made in the initializer of \p InitCapture, an init capture,
/// which ends with the full expression that makes the closure, as that init
/// capture, `[Values = {1, 2}]`; of several, the one declared last, which
/// goes first. None of a pointer parameter's pointee, the object, static
/// storage, another temporary or what can't be told.
static const VarDecl *localOf(const Expr *Value, unsigned Level,
                              ASTContext &Context,
                              const VarDecl *InitCapture = nullptr) {
  const SourceManager &SM = Context.getSourceManager();
  const VarDecl *Latest = nullptr;
  for (const CStorageRoot &Root : storageRootsOf(Value, Context, Level).Roots) {
    const auto *Variable = dyn_cast_or_null<VarDecl>(Root.Variable);
    switch (Root.Kind) {
    case ERootKind::Frame:
      break;
    case ERootKind::Owned:
      if (Root.bPastBorrowed)
        continue;
      break;
    case ERootKind::Parameter:
      if (!Variable || !Variable->getType()->isReferenceType() ||
          designatesStringLiteral(Variable, Context))
        continue;
      break;
    case ERootKind::ThreadLocal:
      break;
    case ERootKind::Temporary:
      if (!InitCapture || !InitCapture->getInit() || !Root.Made ||
          !SM.isPointWithin(SM.getExpansionLoc(Root.Made->getBeginLoc()),
                            SM.getExpansionLoc(
                                InitCapture->getInit()->getBeginLoc()),
                            SM.getExpansionLoc(
                                InitCapture->getInit()->getEndLoc())))
        continue;
      Variable = InitCapture;
      break;
    // What a raw pointer member of a local record points to, which can be
    // storage the record manages (CStorageRoot::Record): that record.
    case ERootKind::Unknown:
      if (!Root.Record)
        continue;
      Variable = Root.Record;
      break;
    case ERootKind::Static:
    case ERootKind::Actor:
    case ERootKind::UnsafeThis:
    case ERootKind::Object:
      continue;
    }
    if (Variable &&
        (Variable->hasLocalStorage() ||
         Variable->getTLSKind() != VarDecl::TLS_None) &&
        (!Latest || SM.isBeforeInTranslationUnit(Latest->getLocation(),
                                                 Variable->getLocation())))
      Latest = Variable;
  }
  return Latest;
}

/// The local a copy \p Capture borrows, like `Value` of `[pValue = &Value]`,
/// or of `[pValue]` of `int *pValue = &Value`: the lambda borrows it like
/// `[&Value]`. Of a pointer, a view or an object that holds them, not an
/// owner, the local what they refer to lives in (localOf), a level down of
/// what is copied as it is named. A record held by value that isn't a view
/// is a value (Rule V), which provenance doesn't follow (isOpaqueRecord):
/// also one an init capture holds, `[Context = CReadContext{&Value}]`,
/// whatever it is made of.
static const VarDecl *borrowedLocal(const LambdaExpr *Lambda,
                                    const LambdaCapture &Capture,
                                    ASTContext &Context) {
  if (!Capture.capturesVariable() || Capture.getCaptureKind() != LCK_ByCopy)
    return nullptr;
  const ValueDecl *Captured = Capture.getCapturedVar();
  const QualType Type = Captured->getType().getNonReferenceType();
  if (ownerKindOf(Type, Context) != EOwnerKind::None || !holdsReference(Type))
    return nullptr;
  const Expr *Init = captureInitOf(Lambda, Capture);
  if (!Init)
    return nullptr;
  const auto *Variable = dyn_cast<VarDecl>(Captured);
  return localOf(
      Init, Init->isGLValue() || !Init->getType()->isPointerType() ? 1 : 0,
      Context, Variable && Variable->isInitCapture() ? Variable : nullptr);
}

/// Whether the pointer \p Capture of \p Lambda captures by reference, also
/// through a reference to it, can point to storage that is gone before the
/// closure is: what it holds where the lambda captures it (localOf), or
/// anything when it is written.
static bool referencedPointerBorrows(const LambdaExpr *Lambda,
                                     const LambdaCapture &Capture,
                                     ASTContext &Context) {
  const auto *Variable = capturedVariableOf(Capture);
  if (!Variable ||
      !Variable->getType().getNonReferenceType()->isPointerType())
    return false;
  if (!Variable->getType()->isReferenceType() &&
      (isChangedVariable(Variable) || !Variable->getInit()))
    return true;
  const Expr *Init = captureInitOf(Lambda, Capture);
  return Init && localOf(Init, 1, Context);
}

void CoroutineReferenceCaptureCheck::onStartOfTranslationUnit() {
  Instantiations.invalidate();
  Repeated.invalidate();
}

/// The reports of the check that say what a capture refers to may be gone
/// where it is used (reportLifetime).
static constexpr llvm::StringLiteral ReferenceUsedAfterSuspension =
    "a TCFuture coroutine lambda uses %0, which it captures by reference, "
    "after a suspension, when %0 may be gone; capture by value, or return a "
    "TCUnsafeFuture";
static constexpr llvm::StringLiteral ReferenceUsedHere =
    "%0 is used here after a suspension, or in a way that keeps a reference "
    "to it";
static constexpr llvm::StringLiteral PointerUsedAfterSuspension =
    "a TCFuture coroutine lambda uses %0, which points to %1, after a "
    "suspension, when %1 may be gone; capture a value or a shared pointer, "
    "or return a TCUnsafeFuture";
static constexpr llvm::StringLiteral PointerUsedHere =
    "%0 is used here after a suspension";

void CoroutineReferenceCaptureCheck::check(
    const MatchFinder::MatchResult &Result) {
  // Nothing in a translation unit with an error that stops it from compiling
  // (hasUncompilableErrors): no flow is built there, and what the errors leave
  // can't be relied on.
  if (hasUncompilableErrors(*Result.Context))
    return;
  if (Repeated.repeated(Result))
    return;
  const auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda");
  const CXXMethodDecl *Operator = Lambda->getCallOperator();
  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  for (const FunctionDecl *Body :
       Instantiations.bodiesOf(Operator, *Result.Context)) {
    if (Body == Operator) {
      checkLambda(Lambda, Operator, Result);
      continue;
    }
    if (const auto *InstanceOperator = dyn_cast<CXXMethodDecl>(Body))
      if (const LambdaExpr *InstanceLambda =
              Instantiations.lambdaOf(Body, *Result.Context)) {
        const InstantiationScope Scope(*this, Body);
        checkLambda(InstanceLambda, InstanceOperator, Result);
      }
  }
}

void CoroutineReferenceCaptureCheck::checkLambda(
    const LambdaExpr *Lambda, const CXXMethodDecl *Operator,
    const MatchFinder::MatchResult &Result) {
  auto IsByReference = [](const LambdaCapture &Capture) {
    return Capture.capturesVariable() && Capture.getCaptureKind() == LCK_ByRef;
  };
  // A copy of a pointer into a local, `[pValue = &Value]`, borrows the local
  // like `[&Value]`.
  auto IsPointerIntoLocal = [&](const LambdaCapture &Capture) {
    return borrowedLocal(Lambda, Capture, *Result.Context) != nullptr;
  };
  if (!isProtectedCoroutine(Operator) ||
      (llvm::none_of(Lambda->captures(), IsByReference) &&
       llvm::none_of(Lambda->captures(), IsPointerIntoLocal)))
    return;
  // Not one that is never made, in what isn't evaluated or never runs, `if
  // constexpr (false)`.
  if (isNeverRun(Lambda, *Result.Context))
    return;

  // The cleanup of `fg_AsyncDestroy` starts when its guard is destroyed, while
  // the variables declared before it are there, and runs until it first
  // suspends.
  const bool AsyncDestroyCleanup = isAsyncDestroyCleanup(Lambda, *Result.Context);
  // The factory that returns the cleanup, which isn't a coroutine.
  const DeclContext *FactoryOperator = nullptr;
  if (AsyncDestroyCleanup) {
    for (DynTypedNode Node = DynTypedNode::create(*Lambda);;) {
      const auto Parents = instantiatedParents(Node, *Result.Context);
      if (Parents.empty() || Parents[0].get<CallExpr>() || Parents[0].get<VarDecl>())
        break;
      if (Parents[0].get<ReturnStmt>()) {
        FactoryOperator = Lambda->getLambdaClass()->getDeclContext();
        break;
      }
      if (!Parents[0].get<Expr>())
        break;
      Node = Parents[0];
    }
  }

  // Whether what \p Capture refers to lives until \p Joining joins the
  // lambda (CKeptAt::Joined): what the closure keeps of its initializer, also
  // what a pointer or a view refers to, alive there (aliveWhereUsed).
  auto AliveUntilJoin = [&](const LambdaCapture &Capture,
                            const RunsToCompletionJoin &Joining) {
    CLifetimeUse Kept;
    Kept.Use = captureInitOf(Lambda, Capture);
    Kept.Used = EUsed::Kept;
    Kept.KeptIn.Kind = CKeptAt::EKind::Joined;
    Kept.KeptIn.Closure = Lambda;
    Kept.KeptIn.Join = &Joining;
    Kept.KeptIn.Site = Lambda;
    return Kept.Use && aliveWhereUsed(Kept, *Result.Context);
  };

  // A lambda given only to what runs it to completion before it returns,
  // joined by the end of that call, runs while what it borrows lives until
  // then.
  if (isJoinedBlocking(Lambda, *Result.Context) &&
      llvm::all_of(Lambda->captures(), [&](const LambdaCapture &Capture) {
        return (!IsByReference(Capture) && !IsPointerIntoLocal(Capture)) ||
               AliveUntilJoin(Capture, RunsToCompletionJoin{});
      }))
    return;

  // A lambda that code that runs to completion starts and joins runs while
  // what it captures lives until the join, which is never aborted.
  std::optional<RunsToCompletionJoin> Join;
  // In an entry, only once it has switched to a concurrent actor itself: it
  // resumes after awaiting another entry on the actor it was called on.
  const bool RunsToCompletion =
      inRunsToCompletionContext(Lambda, *Result.Context, /*AfterSwitch=*/true);
  if (RunsToCompletion)
    Join = lambdaJoin(Lambda, *Result.Context, nullptr);

  // A lambda called directly runs right away, where the variables it captures
  // are if the lambdas around it keep them, until it first suspends, also one
  // given to `fg_CallSafe` while the options keep it among the SafeCallees.
  llvm::SmallVector<ClosureConsumer, 2> Consumers =
      closureConsumers(Lambda, *Result.Context, {});
  giveUnlistedCallSafe(Consumers, vocabulary().SafeCallees.names());
  // Nothing is said of one in a local function wrapper whose closure can't
  // be told (ClosureUse::Untold).
  if (llvm::any_of(Consumers, [](const ClosureConsumer &Consumer) {
        return Consumer.Use == ClosureUse::Untold;
      }))
    return;
  const bool CalledInScope = isCalledInScope(Consumers);

  const SourceManager &SM = *Result.SourceManager;
  bool ReportedDefault = false;
  for (const LambdaCapture &Capture : Lambda->captures()) {
    if (!IsByReference(Capture))
      continue;
    const ValueDecl *Variable = Capture.getCapturedVar();
    // Nor what is alive while the closure runs (captureAlive):
    // static storage, not of a thread, also a member or an element of it,
    // through local references and unchanged pointers to it, `[&Timeout =
    // pConfig->m_Timeout]` of `auto const *pConfig = &g_Config`, what an
    // owner the closure keeps owns, and the object of an actor in its own
    // callback.
    if (captureAlive(Lambda, Capture, *Result.Context))
      continue;
    // What it refers to, alive at a use where the context there holds a
    // keeper of it by value (Rule K, aliveWhereUsed), which is then no use
    // after a suspension; the use found and its context (Found).
    CLifetimeUse Found;
    const Stmt *Use = nullptr;
    // Joined, the variable lives while the lambda runs, and so does what it
    // points to or views when that lives until the join: only what its result
    // keeps of it, like `co_return &Value`, outlives the join.
    if (Join && AliveUntilJoin(Capture, *Join)) {
      auto IsVariable = [&](const Stmt *S) { return isReferenceTo(S, Variable); };
      auto IsView = [&](QualType Type) {
        return isDefaultViewType(Type, *Result.Context);
      };
      BorrowRoots Roots{IsVariable, /*PointerValues=*/false, IsView};
      Roots.Joined = true;
      // Its references are found by the index of the body made once, not by
      // going through the body for each capture.
      Roots.Decl = Variable;
      Use = firstUseAfterSuspension(Operator, IsVariable, *Result.Context,
                                    &Roots);
      if (!Use)
        continue;
    }
    // The cleanup of an `fg_AsyncDestroy` in code that runs to completion runs
    // before the functions around it end.
    // A local, not a parameter, is there only until the cleanup first
    // suspends: the coroutine joins the cleanup after its locals are gone.
    if (!Use && AsyncDestroyCleanup && RunsToCompletion &&
        outlivesEnclosing(Variable, Lambda)) {
      if (isa<ParmVarDecl>(Variable))
        continue;
      auto IsVariable = [&](const Stmt *S) { return isReferenceTo(S, Variable); };
      auto IsView = [&](QualType Type) {
        return isDefaultViewType(Type, *Result.Context);
      };
      // Also what a pointer it captures points to, through a copy made
      // before, `int *pSaved = pValue`, when that can be a local too.
      BorrowRoots Roots{
          IsVariable,
          referencedPointerBorrows(Lambda, Capture, *Result.Context), IsView};
      Roots.Decl = Variable;
      Roots.AskKeepers = true;
      Roots.Found = &Found;
      Use = firstUseAfterSuspension(Operator, IsVariable, *Result.Context,
                                    &Roots);
      if (!Use)
        continue;
    }
    // A cleanup returned by a factory runs after the factory returned, so it
    // can't refer to the factory's locals; its reference parameters refer to
    // the variables of the call.
    // Of a reference, what it refers to, `value` of `[&Alias = value]` or of
    // `int &Alias = _Input`, where that is told by what it is bound to.
    const ValueDecl *Referent = Variable;
    for (int Depth = 0; Depth < 64; ++Depth) {
      const auto *Reference = dyn_cast<VarDecl>(Referent);
      if (!Reference || !Reference->getType()->isReferenceType() ||
          isa<ParmVarDecl>(Reference) || !Reference->getInit())
        break;
      const auto *Bound =
          dyn_cast<DeclRefExpr>(Reference->getInit()->IgnoreParenImpCasts());
      if (!Bound || !isa<VarDecl>(Bound->getDecl()))
        break;
      Referent = Bound->getDecl();
    }
    const bool FactoryLocal =
        FactoryOperator &&
        Referent->getParentFunctionOrMethod() == FactoryOperator &&
        !(isa<ParmVarDecl>(Referent) && Referent->getType()->isReferenceType());
    // The lambdas around it must keep the variable too.
    if (!Use && (AsyncDestroyCleanup || CalledInScope) && !FactoryLocal &&
        isLocalWhileRunning(Referent, Lambda, *Result.Context)) {
      auto IsVariable = [&](const Stmt *S) { return isReferenceTo(S, Variable); };
      auto IsView = [&](QualType Type) {
        return isDefaultViewType(Type, *Result.Context);
      };
      // Also what a pointer it captures points to, through a copy made
      // before, `int *pSaved = pValue`, when that can be a local too.
      BorrowRoots Roots{
          IsVariable,
          referencedPointerBorrows(Lambda, Capture, *Result.Context), IsView};
      Roots.Decl = Variable;
      Roots.AskKeepers = true;
      Roots.Found = &Found;
      Use = firstUseAfterSuspension(Operator, IsVariable, *Result.Context,
                                    &Roots);
      if (!Use)
        continue;
    }
    // Captures made by `[&]` are reported once, at the default.
    if (Capture.isImplicit() && ReportedDefault)
      continue;
    ReportedDefault = ReportedDefault || Capture.isImplicit();
    const SourceLocation Loc = reportLocationIn(
        Capture.isImplicit() ? Lambda->getCaptureDefaultLoc()
                             : Capture.getLocation(),
        Lambda->getEndLoc(), SM);
    if (Loc.isInvalid())
      continue;
    if (!Use) {
      diag(Loc, "a TCFuture coroutine lambda captures %0 by reference but is "
                "not called directly where %0 is, so it can run after %0 is "
                "gone; capture by value, or return a TCUnsafeFuture")
          << Variable;
      continue;
    }
    auto Arguments = [&](const MalterlibDiagnostic &Diag) { Diag << Variable; };
    if (Found.Use) {
      reportLifetime(*this, Found, *Result.Context,
                     {Loc, ReferenceUsedAfterSuspension, Arguments,
                      ReferenceUsedHere, Arguments});
      continue;
    }
    // What the result keeps, told in the context of the result
    // (resultUses), not of one use.
    Arguments(diag(Loc, ReferenceUsedAfterSuspension));
    Arguments(diag(Use->getBeginLoc(), ReferenceUsedHere, DiagnosticIDs::Note));
  }

  // A copy of a pointer into a local borrows the local: it is checked like a
  // capture of the local by reference, with the uses of the copy.
  for (const LambdaCapture &Capture : Lambda->captures()) {
    const VarDecl *Pointee = borrowedLocal(Lambda, Capture, *Result.Context);
    if (!Pointee)
      continue;
    const ValueDecl *Pointer = Capture.getCapturedVar();
    // What moves with its owner into the closure lives while it runs, like
    // what is joined: only what its result keeps of it outlives it.
    const bool KeptByClosure =
        captureAlive(Lambda, Capture, *Result.Context);
    const bool FactoryLocal =
        FactoryOperator && Pointee->getParentFunctionOrMethod() == FactoryOperator;
    CLifetimeUse Found;
    const Stmt *Use = nullptr;
    const bool Joined = Join && AliveUntilJoin(Capture, *Join);
    // Also what an init capture of this lambda holds, the array of
    // `[Values = {1, 2}]`, which lives with its closure, while it runs, but
    // not of a cleanup, whose closure is moved into its guard: a copy of the
    // list refers to the array of the closure it was made in, which is gone.
    const auto *CaptureOperator =
        dyn_cast<CXXMethodDecl>(Pointee->getDeclContext());
    const bool OwnInitCapture =
        Pointee->isInitCapture() && CaptureOperator &&
        CaptureOperator->getParent() == Lambda->getLambdaClass() &&
        !AsyncDestroyCleanup;
    // A record held by value is asked where a use reads what a member of it
    // points to, wherever the closure runs.
    if (KeptByClosure || isOpaqueRecord(Pointer, *Result.Context) ||
        ((Joined ||
          ((AsyncDestroyCleanup || CalledInScope) && !FactoryLocal)) &&
         (OwnInitCapture ||
          isLocalWhileRunning(Pointee, Lambda, *Result.Context)))) {
      // Not where it reads only what the closure's copy has of its own
      // (readsOnlyOwnPart), by the type of the closure's field: a pointer
      // only compared or tested, `pValue != nullptr`, or a view's own
      // storage, `Text.f_GetLen()`, also of `[Text]` of `auto const &Text =
      // Span`, not what it points to.
      const QualType Held = closureFieldType(Lambda, Capture);
      auto IsPointer = [&](const Stmt *S) {
        return isReferenceTo(S, Pointer) &&
               !readsOnlyOwnPart(S, Pointer, Held, *Result.Context);
      };
      auto IsView = [&](QualType Type) {
        return isDefaultViewType(Type, *Result.Context);
      };
      // The value of the pointer is what is borrowed, also in a copy of it,
      // `int *pSaved = p`; joined, only what the result keeps of it outlives
      // the join.
      BorrowRoots Roots{IsPointer, /*PointerValues=*/true, IsView};
      Roots.Joined = Joined || KeptByClosure;
      // Alive at a use where the context there holds a keeper of what it
      // points to by value (Rule K, aliveWhereUsed): what the copy a name of
      // it designates points to.
      Roots.AskKeepers = true;
      Roots.Found = &Found;
      Use = firstUseAfterSuspension(Operator, IsPointer, *Result.Context, &Roots);
      if (!Use)
        continue;
    }
    const SourceLocation Loc =
        reportLocationIn(Capture.isImplicit() ? Lambda->getCaptureDefaultLoc()
                                              : Capture.getLocation(),
                         Lambda->getEndLoc(), SM);
    if (Loc.isInvalid())
      continue;
    // The array of a list it captures, `[Values = {1, 2}]` of a closure that
    // is a temporary, a temporary of the expression that makes the closure:
    // what the closure keeps of its initializer, told by the emitter.
    if (!Use && Pointer == Pointee) {
      CLifetimeUse Kept;
      Kept.Use = captureInitOf(Lambda, Capture);
      Kept.Used = EUsed::Kept;
      Kept.KeptIn.Kind = CKeptAt::EKind::Capture;
      Kept.KeptIn.Closure = Lambda;
      auto ListArguments = [&](const MalterlibDiagnostic &Diag) {
        Diag << Pointer;
      };
      if (Kept.Use)
        reportLifetime(*this, Kept, *Result.Context,
                       {Loc,
                        "a TCFuture coroutine lambda captures the list %0, "
                        "whose array is a temporary of the expression that "
                        "makes the closure, so it can be gone when the closure "
                        "runs; capture an array or a container by value",
                        ListArguments});
      continue;
    }
    if (!Use) {
      diag(Loc, "a TCFuture coroutine lambda captures %0, which points to %1, but "
                "is not called directly where %1 is, so it can run after %1 "
                "is gone; capture a value or a shared pointer, or return a "
                "TCUnsafeFuture")
          << Pointer << Pointee;
      continue;
    }
    auto Arguments = [&](const MalterlibDiagnostic &Diag) {
      Diag << Pointer << Pointee;
    };
    auto NoteArguments = [&](const MalterlibDiagnostic &Diag) {
      Diag << Pointer;
    };
    if (Found.Use) {
      reportLifetime(*this, Found, *Result.Context,
                     {Loc, PointerUsedAfterSuspension, Arguments,
                      PointerUsedHere, NoteArguments});
      continue;
    }
    // What the result keeps, told in the context of the result
    // (resultUses), not of one use.
    Arguments(diag(Loc, PointerUsedAfterSuspension));
    NoteArguments(
        diag(Use->getBeginLoc(), PointerUsedHere, DiagnosticIDs::Note));
  }
}

} // namespace clang::tidy::malterlib
