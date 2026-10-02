//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RunsToCompletion.h"
#include "ActorAnnotations.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"
#include "OncePerTypeVisitor.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopeExit.h"
#include <functional>

namespace clang::tidy::malterlib {

/// The call of a lambda's call operator by name, `fLambda.operator()<int>(
/// Value)`, that \p Parent, a parent of \p Object, makes it the object of,
/// or null.
static const CXXMemberCallExpr *
namedCallOperatorCall(const DynTypedNode &Parent, const Expr *Object,
                      ASTContext &Context) {
  const auto *Member = Parent.get<MemberExpr>();
  const auto *Method =
      Member ? dyn_cast<CXXMethodDecl>(Member->getMemberDecl()) : nullptr;
  if (!Method || Method->getOverloadedOperator() != OO_Call || !Object ||
      Member->getBase()->IgnoreParenImpCasts() != Object->IgnoreParenImpCasts())
    return nullptr;
  const auto Above = instantiatedParents(*Member, Context);
  return Above.empty() ? nullptr : Above[0].get<CXXMemberCallExpr>();
}

bool isRunsToCompletion(const FunctionDecl *Function) {
  // A member function of an actor can be aborted when the actor is destroyed:
  // it doesn't run to completion, whatever its class says.
  if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Function);
      Method && !Method->isStatic() && isActorClass(Method->getParent()))
    return false;
  return Function && hasAnnotation(Function, AnnotationRunsToCompletion);
}

bool isRunsToCompletionEntry(const FunctionDecl *Function) {
  return Function && annotatedPhases(Function, AnnotationRunsToCompletionEntry);
}

bool returnsFuture(const FunctionDecl *Function) {
  const StringRef Name = recordName(Function->getReturnType());
  // Also the future with context that `%` gives, `fg_Work() % "Context"`.
  return Name == "TCFuture" || Name == "TCFutureWithFlags" ||
         Name == "TCUnsafeFuture" ||
         Name == "TCFutureWithExceptionTransformer";
}

/// isConcurrentActor, each local it follows told once while the outermost
/// question is answered, in \p Told, of which one followed again while it is
/// told, `Actor` of `TCActor<> const &Actor = _bChoose ? Actor : ...`, isn't
/// one.
static bool isConcurrentActorImpl(const Expr *E, const ASTContext &Context,
                                  llvm::DenseMap<const VarDecl *, bool> &Told) {
  // Moved from a local, which the move changes after it gave its value.
  bool Moved = false;
  for (int Depth = 0; E && Depth < 8; ++Depth) {
    E = skipWrappers(E)->IgnoreParenImpCasts();
    // Either of a conditional, `_bLow ? fg_ConcurrentActorLowPrio() :
    // fg_ConcurrentActor()`, when both are, or the one a constant condition
    // chooses.
    if (const auto *Conditional = dyn_cast<ConditionalOperator>(E)) {
      const Expr *Condition = Conditional->getCond();
      bool Chosen = false;
      if (!Condition->isValueDependent() &&
          Condition->EvaluateAsBooleanCondition(Chosen, Context))
        return isConcurrentActorImpl(Chosen ? Conditional->getTrueExpr()
                                            : Conditional->getFalseExpr(),
                                     Context, Told);
      return isConcurrentActorImpl(Conditional->getTrueExpr(), Context, Told) &&
             isConcurrentActorImpl(Conditional->getFalseExpr(), Context, Told);
    }
    if (const auto *Call = dyn_cast<CallExpr>(E)) {
      const StringRef Name = calleeName(Call);
      // Moved, `fg_Move(Actor)`, which doesn't change which actor it is.
      if ((isMoveFunction(Name)) && Call->getNumArgs() == 1) {
        E = Call->getArg(0);
        Moved = true;
        continue;
      }
      // `fg_ConcurrentDispatch` stands for the concurrent actor it
      // dispatches to.
      return matchesNamePattern(Name, {"fg_ConcurrentActor*",
                                       "fg_OtherConcurrentActor*",
                                       "fg_ThisConcurrentActor*",
                                       "fg_DynamicConcurrentActor*",
                                       "fg_BlockingActor*",
                                       "fg_ConcurrentDispatch"});
    }
    const auto *Ref = dyn_cast<DeclRefExpr>(E);
    // `g_ConcurrentDispatch`, of `g_ConcurrentDispatch / Lambda`, stands for
    // the concurrent actor it dispatches to.
    if (Ref && Ref->getDecl()->getIdentifier() &&
        Ref->getDecl()->getName() == "g_ConcurrentDispatch")
      return true;
    const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    // A checkout of a blocking actor that the coroutine owns, given by value
    // and kept in its frame until it completes.
    if (const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Variable);
        Parameter && !Parameter->getType()->isReferenceType() &&
        recordName(Parameter->getType()) == "CBlockingActorCheckout")
      return !isChangedVariable(Parameter) && isUnchangedAt(Ref);
    // Also the copy a mutable lambda captured, which it can change.
    if (!Variable || !Variable->hasLocalStorage() ||
        isa<ParmVarDecl>(Variable) || !Variable->getInit() ||
        (Moved ? Ref->refersToEnclosingVariableOrCapture() ||
                     isChangedOtherThanBy(Ref)
               : isChangedVariable(Variable) || !isUnchangedAt(Ref)))
      return false;
    if (auto [It, Inserted] = Told.try_emplace(Variable, false); !Inserted)
      return It->second;
    const bool Concurrent =
        isConcurrentActorImpl(Variable->getInit(), Context, Told);
    Told[Variable] = Concurrent;
    return Concurrent;
  }
  return false;
}

bool isConcurrentActor(const Expr *E, const ASTContext &Context) {
  llvm::DenseMap<const VarDecl *, bool> Told;
  return isConcurrentActorImpl(E, Context, Told);
}

/// Whether \p Operand is \p Child, past the implicit nodes that a parent map
/// that ignores them skips.
static bool isSameOperand(const Expr *Operand, const Expr *Child) {
  if (!Operand || !Child)
    return false;
  return Operand == Child ||
         Operand->IgnoreUnlessSpelledInSource() ==
             Child->IgnoreUnlessSpelledInSource() ||
         skipWrappers(Operand)->IgnoreParenImpCasts() ==
             skipWrappers(Child)->IgnoreParenImpCasts();
}

/// The local that \p E designates: the variable, or the one subscripted, like
/// `Results[Key]`, also through a local reference to it, `auto &Alias =
/// Results`.
static const VarDecl *localOf(const Expr *E) {
  E = skipWrappers(E)->IgnoreParenImpCasts();
  if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
      Call && Call->getOperator() == OO_Subscript && Call->getNumArgs() >= 1)
    E = Call->getArg(0)->IgnoreParenImpCasts();
  const auto *Ref = dyn_cast<DeclRefExpr>(E);
  const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  for (int Depth = 0; Depth < 8 && Variable && Variable->hasLocalStorage() &&
                      !isa<ParmVarDecl>(Variable) &&
                      Variable->getType()->isReferenceType() &&
                      Variable->getInit();
       ++Depth) {
    const auto *Bound =
        dyn_cast<DeclRefExpr>(Variable->getInit()->IgnoreParenImpCasts());
    const auto *Aliased = Bound ? dyn_cast<VarDecl>(Bound->getDecl()) : nullptr;
    if (!Aliased || !Aliased->hasLocalStorage())
      break;
    Variable = Aliased;
  }
  return Variable && Variable->hasLocalStorage() ? Variable : nullptr;
}

/// Whether \p S suspends, outside of the lambdas in it, which can abort the
/// coroutine: what the future of a call is wrapped or collected with, like
/// `Future % (co_await ...)` or `Future > Results[co_await ...]`, which can
/// leave after the call started, before it is joined. A throw ends the
/// program. Not an await whose awaiter never suspends, `co_await
/// CSuspendNever{}`, nor what runs only in a value of a conditional that a
/// constant condition doesn't choose, `false ? co_await ... : 1`. The
/// expression of a default argument, which the uses of it share, also those
/// in other default arguments, `int f1(int = f0() + f0());`, is looked at once,
/// in \p Shared.
static bool hasSuspension(const Stmt *S, ASTContext &Context,
                          llvm::DenseMap<const Expr *, bool> &Shared) {
  if (!S)
    return false;
  auto Recurse = [&](const Stmt *Child) {
    return hasSuspension(Child, Context, Shared);
  };
  // What isn't evaluated, like `sizeof(co_await ...)`, doesn't run.
  if (isa<UnaryExprOrTypeTraitExpr, CXXNoexceptExpr>(S))
    return false;
  if (const auto *Typeid = dyn_cast<CXXTypeidExpr>(S);
      Typeid && !Typeid->isPotentiallyEvaluated())
    return false;
  // Making a lambda runs only its captures.
  if (const auto *Lambda = dyn_cast<LambdaExpr>(S))
    return llvm::any_of(Lambda->capture_inits(), Recurse);
  if (const auto *Suspend = dyn_cast<CoroutineSuspendExpr>(S)) {
    if (isa<CoyieldExpr>(Suspend) || !awaiterNeverSuspends(Suspend))
      return true;
    return Recurse(Suspend->getOperand());
  }
  bool Constant = false;
  if (const auto *Conditional = dyn_cast<ConditionalOperator>(S);
      Conditional && !Conditional->getCond()->isValueDependent() &&
      Conditional->getCond()->EvaluateAsBooleanCondition(Constant, Context))
    return Recurse(Conditional->getCond()) ||
           Recurse(Constant ? Conditional->getTrueExpr()
                            : Conditional->getFalseExpr());
  if (const auto *Logical = dyn_cast<BinaryOperator>(S);
      Logical && Logical->isLogicalOp() &&
      !Logical->getLHS()->isValueDependent() &&
      Logical->getLHS()->EvaluateAsBooleanCondition(Constant, Context) &&
      Constant == (Logical->getOpcode() == BO_LOr))
    return Recurse(Logical->getLHS());
  // What a default argument or default member initializer runs, which isn't
  // among the children.
  const Expr *Default = nullptr;
  if (const auto *Argument = dyn_cast<CXXDefaultArgExpr>(S))
    Default = Argument->getExpr();
  else if (const auto *Initializer = dyn_cast<CXXDefaultInitExpr>(S))
    Default = Initializer->getExpr();
  if (Default) {
    if (auto It = Shared.find(Default); It != Shared.end())
      return It->second;
    Shared[Default] = false;
    const bool Suspends = Recurse(Default);
    Shared[Default] = Suspends;
    return Suspends;
  }
  return llvm::any_of(childrenOnce(S), Recurse);
}

static bool hasSuspension(const Stmt *S, ASTContext &Context) {
  llvm::DenseMap<const Expr *, bool> Shared;
  return hasSuspension(S, Context, Shared);
}

/// capture of the cleanup.
static bool awaitsCollection(const Expr *E, const VarDecl *Collection,
                             const LambdaExpr *Cleanup) {
  if (!E)
    return false;
  E = skipWrappers(E)->IgnoreParenImpCasts();
  if (const auto *Member = dyn_cast<CXXMemberCallExpr>(E);
      Member && Member->getMethodDecl() &&
      Member->getMethodDecl()->getIdentifier() &&
      Member->getMethodDecl()->getName() == "f_Wrap" &&
      llvm::none_of(Member->arguments(), [&](const Expr *Argument) {
        return hasSuspension(Argument, Collection->getASTContext());
      }))
    return awaitsCollection(Member->getImplicitObjectArgument(), Collection,
                            Cleanup);
  // Also with context, `fg_AllDone(Results) % "Joining"`, made of what can't
  // leave.
  if (const auto *Context = dyn_cast<CXXOperatorCallExpr>(E);
      Context && Context->getOperator() == OO_Percent &&
      Context->getNumArgs() == 2 &&
      !hasSuspension(Context->getArg(1), Collection->getASTContext()))
    return awaitsCollection(Context->getArg(0), Collection, Cleanup);
  const auto *Call = dyn_cast<CallExpr>(E);
  if (!Call || Call->getNumArgs() != 1)
    return false;
  // Also moved into it, `fg_AllDone(fg_Move(Results))`.
  const Expr *Given = skipWrappers(Call->getArg(0))->IgnoreParenImpCasts();
  if (const auto *Move = dyn_cast<CallExpr>(Given);
      Move && Move->getNumArgs() == 1 && (isMoveFunction(calleeName(Move))))
    Given = skipWrappers(Move->getArg(0))->IgnoreParenImpCasts();
  // Also through a local reference to it, `auto &Alias = Results`.
  auto Resolved = [](const ValueDecl *Decl) {
    for (int Depth = 0; Depth < 8; ++Depth) {
      const auto *Alias = dyn_cast<VarDecl>(Decl);
      const auto *Bound =
          Alias && Alias->hasLocalStorage() && !isa<ParmVarDecl>(Alias) &&
                  Alias->getType()->isReferenceType() && Alias->getInit()
              ? dyn_cast<DeclRefExpr>(Alias->getInit()->IgnoreParenImpCasts())
              : nullptr;
      if (!Bound)
        break;
      Decl = Bound->getDecl();
    }
    return Decl;
  };
  const auto *Ref = dyn_cast<DeclRefExpr>(Given);
  if (!Ref || Resolved(Ref->getDecl()) != Collection)
    return false;
  // A copy in the closure isn't the collection the futures go into.
  return llvm::any_of(Cleanup->captures(), [&](const LambdaCapture &Capture) {
    return Capture.capturesVariable() &&
           Resolved(Capture.getCapturedVar()) == Collection &&
           Capture.getCaptureKind() == LCK_ByRef;
  });
}

/// Whether \p Guard, an `fg_AsyncDestroy` guard, is only destroyed where its
/// scope ends, or awaited early, like `co_await fg_Move(Guard)`, which joins
/// its cleanup sooner. \p EarlyAwait is set to the first early await. With
/// \p AllowClear, it may also be cleared, which drops the cleanup.
static bool isOnlyDestroyed(const VarDecl *Guard, ASTContext &Context,
                            SourceLocation &EarlyAwait,
                            bool AllowClear = false) {
  // The guard is only destroyed, or awaited early, like `co_await
  // fg_Move(Guard)`, which joins what was collected before: clearing or
  // moving it elsewhere cancels the join.
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Guard->getParentFunctionOrMethod());
  // Also through local references to it, `auto &Alias = Guard`.
  llvm::SmallPtrSet<const VarDecl *, 4> Aliases{Guard};
  if (Function && Function->getBody()) {
    llvm::SmallVector<const VarDecl *, 4> References;
    std::function<void(const Stmt *)> Collect = [&](const Stmt *S) {
      if (!S)
        return;
      if (const auto *Declaration = dyn_cast<DeclStmt>(S))
        for (const Decl *D : Declaration->decls())
          if (const auto *Reference = dyn_cast<VarDecl>(D);
              Reference && Reference->getType()->isReferenceType() &&
              Reference->getInit())
            References.push_back(Reference);
      for (const Stmt *Child : childrenOnce(S))
        Collect(Child);
    };
    Collect(Function->getBody());
    for (bool Added = true; Added && Aliases.size() < 16;) {
      Added = false;
      for (const VarDecl *Reference : References)
        if (const auto *Init = dyn_cast<DeclRefExpr>(
                Reference->getInit()->IgnoreParenImpCasts());
            Init && isa<VarDecl>(Init->getDecl()) &&
            Aliases.contains(cast<VarDecl>(Init->getDecl())) &&
            Aliases.insert(Reference).second)
          Added = true;
    }
  }
  bool Referenced = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    // What isn't evaluated, like `sizeof(fg_Move(Guard))`, doesn't use it.
    if (!S || Referenced ||
        isa<UnaryExprOrTypeTraitExpr, CXXNoexceptExpr, RequiresExpr>(S))
      return;
    // Nor a branch of an `if constexpr` that is discarded.
    if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConstexpr()) {
      if (std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Function->getASTContext())) {
        Visit(If->getInit());
        Visit(*Case);
        return;
      }
    }
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && isa<VarDecl>(Ref->getDecl()) &&
        Aliases.contains(cast<VarDecl>(Ref->getDecl()))) {
      // Making a reference to it isn't using it.
      if (llvm::any_of(Aliases, [&](const VarDecl *Alias) {
            return Alias->getInit() &&
                   Alias->getInit()->IgnoreParenImpCasts() == Ref;
          }))
        return;
      const auto MoveParents = instantiatedParents(*Ref, Context);
      // Clearing it drops the cleanup, which then never runs.
      if (const auto *Member =
              MoveParents.empty() ? nullptr : MoveParents[0].get<MemberExpr>();
          AllowClear && Member && Member->getMemberDecl()->getIdentifier() &&
          Member->getMemberDecl()->getName() == "f_Clear")
        return;
      const auto *Move =
          MoveParents.empty() ? nullptr : MoveParents[0].get<CallExpr>();
      // Also cast to an rvalue reference, `co_await static_cast<CGuard
      // &&>(Guard)`, which moves it like `fg_Move`.
      const auto *Cast =
          MoveParents.empty() ? nullptr : MoveParents[0].get<ExplicitCastExpr>();
      if (Cast && !Cast->getTypeAsWritten()->isRValueReferenceType())
        Cast = nullptr;
      DynTypedNode Up = Move   ? DynTypedNode::create(*Move)
                        : Cast ? DynTypedNode::create(*Cast)
                               : DynTypedNode();
      bool Awaited = false;
      for (int Depth = 0; (Move || Cast) && Depth < 4; ++Depth) {
        const auto Parents = instantiatedParents(Up, Context);
        if (Parents.empty())
          break;
        if (Parents[0].get<CoawaitExpr>()) {
          Awaited = true;
          break;
        }
        // Also wrapped, like `co_await fg_Move(Guard).f_Wrap()`.
        if (const auto *Member = Parents[0].get<MemberExpr>();
            Member && Member->getMemberDecl()->getIdentifier() &&
            Member->getMemberDecl()->getName() == "f_Wrap") {
          const auto CallParents = instantiatedParents(Parents[0], Context);
          if (CallParents.empty() || !CallParents[0].get<CXXMemberCallExpr>())
            break;
          Up = CallParents[0];
          continue;
        }
        if (!Parents[0].get<ImplicitCastExpr>() &&
            !Parents[0].get<ParenExpr>() &&
            !Parents[0].get<MaterializeTemporaryExpr>() &&
            !Parents[0].get<CXXBindTemporaryExpr>() &&
            !Parents[0].get<CXXConstructExpr>())
          break;
        Up = Parents[0];
      }
      if (Awaited && (Cast || isMoveFunction(calleeName(Move)))) {
        if (EarlyAwait.isInvalid() ||
            Context.getSourceManager().isBeforeInTranslationUnit(
                Ref->getBeginLoc(), EarlyAwait))
          EarlyAwait = Ref->getBeginLoc();
        return;
      }
      Referenced = true;
      return;
    }
    // Nor what never runs, a branch a constant condition doesn't take, `if
    // (false) co_await fg_Move(Guard);`, which isn't the early await.
    for (const Stmt *Child : evaluatedChildren(S, Context))
      Visit(Child);
  };
  if (Function)
    Visit(Function->getBody());
  return !Referenced;
}

/// Whether the cleanup that \p Call, an `fg_AsyncDestroy`, is given is joined
/// in the scope it is made in: the call is awaited and the guard it gives is
/// held in a local that is only destroyed there, or awaited early. A guard
/// that is returned or moved elsewhere runs the cleanup later.
bool isCleanupJoined(const CallExpr *Call, ASTContext &Context,
                     bool *AwaitedEarly) {
  SourceLocation EarlyAwait;
  const VarDecl *Guard = cleanupGuard(Call, Context, EarlyAwait);
  if (AwaitedEarly)
    *AwaitedEarly = EarlyAwait.isValid();
  return Guard;
}

const VarDecl *cleanupGuard(const CallExpr *Call, ASTContext &Context,
                            SourceLocation &EarlyAwait) {
  EarlyAwait = SourceLocation();
  DynTypedNode Node = DynTypedNode::create(*Call);
  bool Awaited = false;
  for (int Depth = 0; Depth < 16; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    const DynTypedNode &Parent = Parents[0];
    if (const auto *Guard = Parent.get<VarDecl>()) {
      const bool Joined =
          Awaited && Guard->hasLocalStorage() && !isa<ParmVarDecl>(Guard) &&
          // Also a reference that extends it, `auto &&Join = co_await ...`.
          (!Guard->getType()->isReferenceType() || extendedTemporary(Guard)) &&
          isOnlyDestroyed(Guard, Context, EarlyAwait, /*AllowClear=*/true);
      return Joined ? Guard : nullptr;
    }
    const auto *ParentExpr = Parent.get<Expr>();
    if (!ParentExpr)
      return nullptr;
    if (const auto *Await = dyn_cast<CoawaitExpr>(ParentExpr)) {
      if (Awaited || !isSameOperand(Await->getOperand(), Node.get<Expr>()))
        return nullptr;
      Awaited = true;
    } else if (const auto *Construct = dyn_cast<CXXConstructExpr>(ParentExpr)) {
      if (Construct->getNumArgs() != 1 ||
          !Construct->getConstructor()->isCopyOrMoveConstructor())
        return nullptr;
    } else if (const auto *Conditional =
                   dyn_cast<AbstractConditionalOperator>(ParentExpr)) {
      // Either awaited guard of a conditional that initializes it.
      if (!Awaited || isSameOperand(Conditional->getCond(), Node.get<Expr>()))
        return nullptr;
    } else if (const auto *List = dyn_cast<InitListExpr>(ParentExpr);
               List && Awaited && List->getType()->isArrayType()) {
      // An element of a local array, `CGuard Guards[] = {co_await ...}`,
      // destroyed with it.
    } else if (!isa<ParenExpr, ImplicitCastExpr, MaterializeTemporaryExpr,
                    CXXBindTemporaryExpr, ExprWithCleanups>(ParentExpr)) {
      return nullptr;
    }
    Node = Parent;
  }
  return nullptr;
}

/// Whether awaiting \p Operand registers a callback that runs on each resume
/// and can end the coroutine: `fg_OnResume(...)`, a guard of a phase,
/// `f_CheckDestroyedOnResume()`, or what a function whose body can be seen
/// returns of one on any of its returns, `auto fg_Watch() { return
/// fg_OnResume(...); }`, which may be what it returns, also through any
/// number of them. One too deep to tell can be one.
static bool isOnResumeRegistration(const Expr *Operand, int Depth = 0) {
  if (!Operand)
    return false;
  if (Depth > 64)
    return true;
  // Also through an awaiter made before, `co_await fg_Move(Awaiter)`, and
  // either value awaited, `co_await (bChoose ? fg_OnResume(A) : B)`.
  const llvm::SmallVector<const Expr *, 2> Values = awaitedValues(Operand);
  if (Values.size() != 1)
    return llvm::any_of(Values, [&](const Expr *Value) {
      return Value && isOnResumeRegistration(Value, Depth + 1);
    });
  Operand = Values[0];
  if (!Operand)
    return false;
  if (asCallTo(Operand, {"fg_OnResume"}) || isPhaseGuardCall(Operand))
    return true;
  const auto *Call =
      dyn_cast<CallExpr>(skipWrappers(Operand)->IgnoreParenImpCasts());
  // What a function gives back of what it is given, `fg_ConstOrMove(
  // fg_OnResume(...))`, is what it is given.
  if (Call && llvm::any_of(returnedArguments(Call), [&](const Expr *Given) {
        return isOnResumeRegistration(Given, Depth + 1);
      }))
    return true;
  const FunctionDecl *Callee = Call ? Call->getDirectCallee() : nullptr;
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody())
    return false;
  // Decided once for each function, with where on the stack of those being
  // decided it is while it is, and for one decided while one further out it
  // used is, that one's position.
  struct CDecision {
    std::optional<bool> Result;
    unsigned Position = 0;
    unsigned DependsOn = ~0u;
  };
  struct CFactories {
    llvm::DenseMap<const FunctionDecl *, CDecision> Decided;
  };
  // How many are being decided, the lowest position of one being decided
  // that what is being decided now used, and for each position, what was
  // decided depending on the one there.
  static thread_local unsigned s_nDeciding = 0;
  static thread_local unsigned s_UsedDeciding = ~0u;
  static thread_local std::vector<llvm::SmallVector<const FunctionDecl *, 4>>
      s_Pending;
  auto &Decided = astCache<CFactories>(Definition->getASTContext()).Decided;
  if (auto It = Decided.find(Definition); It != Decided.end()) {
    if (It->second.Result) {
      if (It->second.DependsOn != ~0u)
        s_UsedDeciding = std::min(s_UsedDeciding, It->second.DependsOn);
      return *It->second.Result;
    }
    // One that returns a call of itself returns what its other returns do,
    // which isn't known until it is decided.
    s_UsedDeciding = std::min(s_UsedDeciding, It->second.Position);
    return false;
  }
  const unsigned Position = s_nDeciding++;
  if (s_Pending.size() <= Position)
    s_Pending.resize(Position + 1);
  Decided[Definition] = {std::nullopt, Position};
  const unsigned OuterUsedDeciding = s_UsedDeciding;
  s_UsedDeciding = ~0u;
  bool Any = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Any || isa<LambdaExpr>(S))
      return;
    if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
      Any = isOnResumeRegistration(Return->getRetValue(), Depth + 1);
      return;
    }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Definition->getBody());
  --s_nDeciding;
  // One that took one further out to return none, which is decided later,
  // holds while that one returns none: it is kept, depending on it, so that a
  // function that many reach is decided once, and dropped to be decided
  // again when that one returns one. One that returns one is final, and so
  // is one that doesn't depend.
  const bool Provisional = !Any && s_UsedDeciding < Position;
  {
    CDecision &Decision = Decided[Definition];
    Decision.Result = Any;
    Decision.DependsOn = Provisional ? s_UsedDeciding : ~0u;
  }
  if (Provisional)
    s_Pending[s_UsedDeciding].push_back(Definition);
  llvm::SmallVector<const FunctionDecl *, 4> Pending =
      std::move(s_Pending[Position]);
  s_Pending[Position].clear();
  for (const FunctionDecl *Dependent : Pending) {
    auto It = Decided.find(Dependent);
    if (It == Decided.end())
      continue;
    if (Any) {
      Decided.erase(It);
      continue;
    }
    It->second.DependsOn = Provisional ? s_UsedDeciding : ~0u;
    if (Provisional)
      s_Pending[s_UsedDeciding].push_back(Dependent);
  }
  s_UsedDeciding =
      std::min(OuterUsedDeciding, Provisional ? s_UsedDeciding : ~0u);
  return Any;
}

/// Whether \p Guard holds an `fg_AsyncDestroy` whose cleanup awaits
/// \p Collection on every path: as a statement of its body before anything
/// that can return or branch, and the guard isn't cleared or moved.
static bool decideJoinGuard(const VarDecl *Guard, const VarDecl *Collection,
                            ASTContext &Context, SourceLocation &EarlyAwait);

/// Like `decideJoinGuard`, each guard and collection once in a translation
/// unit: each future collected asks it again, which would take time that
/// grows with the square of their number.
static bool isJoinGuard(const VarDecl *Guard, const VarDecl *Collection,
                        ASTContext &Context, SourceLocation &EarlyAwait) {
  struct CCache {
    uint64_t AST = 0;
    llvm::DenseMap<std::pair<const VarDecl *, const VarDecl *>,
                   std::pair<bool, SourceLocation>>
        Decided;
  };
  static thread_local CCache s_Cache;
  if (const uint64_t AST = astIdentity(Context); s_Cache.AST != AST) {
    s_Cache.Decided.clear();
    s_Cache.AST = AST;
  }
  if (const auto It = s_Cache.Decided.find({Guard, Collection});
      It != s_Cache.Decided.end()) {
    EarlyAwait = It->second.second;
    return It->second.first;
  }
  const bool Joins = decideJoinGuard(Guard, Collection, Context, EarlyAwait);
  s_Cache.Decided[{Guard, Collection}] = {Joins, EarlyAwait};
  return Joins;
}

static bool capturesExceptions(const FunctionDecl *Function);

static bool decideJoinGuard(const VarDecl *Guard, const VarDecl *Collection,
                            ASTContext &Context, SourceLocation &EarlyAwait) {
  // A static guard lives past the call.
  if (!Guard->getInit() || !Guard->hasLocalStorage() ||
      (Guard->getType()->isReferenceType() && !extendedTemporary(Guard)))
    return false;
  const Expr *Init = skipWrappers(Guard->getInit());
  const auto *Await = dyn_cast<CoawaitExpr>(Init);
  if (!Await)
    return false;
  const CallExpr *Call = asCallTo(
      Await->getOperand(), llvm::ArrayRef<StringRef>(std::begin(AsyncDestroyFunctions),
                                         std::end(AsyncDestroyFunctions)));
  if (!Call || Call->getNumArgs() < 1)
    return false;
  const LambdaExpr *Cleanup = asLambda(Call->getArg(0));
  const CompoundStmt *Body = Cleanup ? Cleanup->getCompoundStmtBody() : nullptr;
  if (!Body)
    return false;
  bool Joins = false;
  const bool CleanupCaptures = capturesExceptions(Cleanup->getCallOperator());
  for (const Stmt *S : Body->body()) {
    const Expr *E = dyn_cast<Expr>(S);
    if (const auto *Declaration = dyn_cast<DeclStmt>(S);
        Declaration && Declaration->isSingleDecl())
      if (const auto *Variable = dyn_cast<VarDecl>(Declaration->getSingleDecl()))
        E = Variable->getInit();
    // `co_return co_await fg_AllDone(Results);` awaits before it returns.
    if (const auto *Return = dyn_cast<CoreturnStmt>(S))
      E = Return->getOperand();
    if (E) {
      E = skipWrappers(E)->IgnoreParenImpCasts();
      if (const auto *Await = dyn_cast<CoawaitExpr>(E);
          Await && awaitsCollection(Await->getOperand(), Collection, Cleanup)) {
        Joins = true;
        break;
      }
    }
    // Anything that can leave or branch before the await, like a return, an
    // if, a loop or another suspension, may skip it. Setting coroutine flags,
    // like `co_await ECoroutineFlag_CaptureExceptions`, doesn't.
    // Only what can't leave or suspend may come before it: setting coroutine
    // flags. A throw, also of a call, may skip the join where the cleanup
    // captures exceptions, and another await may.
    if (isa<NullStmt>(S))
      continue;
    // Nor a local made of what can't throw, call or suspend, `int Count =
    // 0;`.
    std::function<bool(const Stmt *)> Plain = [&](const Stmt *Inner) {
      if (!Inner)
        return true;
      if (isa<CallExpr, CXXThrowExpr, CoroutineSuspendExpr, CXXNewExpr,
              CXXDeleteExpr, LambdaExpr, StmtExpr>(Inner))
        return false;
      if (const auto *Construct = dyn_cast<CXXConstructExpr>(Inner);
          Construct && !Construct->getConstructor()->isTrivial())
        return false;
      return llvm::all_of(childrenOnce(Inner), Plain);
    };
    if (const auto *Declaration = dyn_cast<DeclStmt>(S);
        Declaration && llvm::all_of(Declaration->decls(), [&](const Decl *D) {
          const auto *Local = dyn_cast<VarDecl>(D);
          return Local && Local->hasLocalStorage() && Plain(Local->getInit());
        }))
      continue;
    // In a cleanup that doesn't capture exceptions, where a throw ends the
    // program, also what calls or throws, `fg_Log("Joining");`, unless it
    // suspends, which can abort the cleanup.
    std::function<bool(const Stmt *)> CantLeave = [&](const Stmt *Inner) {
      if (!Inner)
        return true;
      if (isa<CoroutineSuspendExpr, StmtExpr>(Inner))
        return false;
      // Not the body of a lambda it makes, only what it captures.
      if (const auto *Lambda = dyn_cast<LambdaExpr>(Inner))
        return llvm::all_of(Lambda->capture_inits(), CantLeave);
      return llvm::all_of(childrenOnce(Inner), CantLeave);
    };
    if (isa<Expr, DeclStmt>(S) && !CleanupCaptures && CantLeave(S))
      continue;
    const auto *Statement = dyn_cast<Expr>(S);
    if (!Statement)
      return false;
    const Expr *Inner = skipWrappers(Statement)->IgnoreParenImpCasts();
    const auto *Await = dyn_cast<CoroutineSuspendExpr>(Inner);
    // Nor a future, also one that is ready, `co_await fg_CallSafe(...)` of a
    // coroutine that doesn't suspend, whose error aborts the cleanup.
    const StringRef Awaited =
        Await ? recordName(Await->getOperand()->getType()) : StringRef();
    if (!Await || isSuspension(Inner) ||
        llvm::is_contained({StringRef("TCFuture"),
                            StringRef("TCFutureWithFlags"),
                            StringRef("TCUnsafeFuture"),
                            StringRef("TCFutureWithExceptionTransformer")},
                           Awaited) ||
        // Registering a callback with `fg_OnResume` runs it, which can abort
        // the cleanup, also through a phase guard,
        // `co_await pActor->f_CheckDestroyedOnResume()`.
        isOnResumeRegistration(Await->getOperand()))
      return false;
  }
  if (!Joins || !isOnlyDestroyed(Guard, Context, EarlyAwait))
    return false;
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Guard->getParentFunctionOrMethod());
  // The collection only gets futures: replacing or moving it would leave the
  // futures it had to nothing that joins them.
  bool Replaced = false;
  // The collection, and the local references to it, `auto &Alias = Results`,
  // whose uses are of it.
  llvm::SmallPtrSet<const ValueDecl *, 4> Collections{Collection};
  std::function<void(const Stmt *)> VisitCollection = [&](const Stmt *S) {
    // Not what isn't evaluated, `sizeof(Results)`, nor a branch of an `if
    // constexpr` that is discarded.
    if (!S || Replaced ||
        isa<UnaryExprOrTypeTraitExpr, CXXNoexceptExpr, RequiresExpr>(S))
      return;
    if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConstexpr()) {
      if (std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Context)) {
        VisitCollection(If->getInit());
        VisitCollection(*Case);
        return;
      }
    }
    // Nor the branch that a constant condition never takes, `if (false)`,
    // unless a jump can reach it.
    bool Taken = false;
    if (const auto *If = dyn_cast<IfStmt>(S);
        If && !If->isConstexpr() && !If->isConsteval() && If->getCond() &&
        !If->getConditionVariable() && !If->getCond()->isValueDependent() &&
        If->getCond()->EvaluateAsBooleanCondition(Taken, Context) &&
        !hasJumpTarget(Taken ? If->getElse() : If->getThen())) {
      VisitCollection(If->getInit());
      VisitCollection(If->getCond());
      VisitCollection(Taken ? If->getThen() : If->getElse());
      return;
    }
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Collections.contains(Ref->getDecl())) {
      DynTypedNode Node = DynTypedNode::create(*Ref);
      for (int Depth = 0; Depth < 64; ++Depth) {
        const auto Parents = instantiatedParents(Node, Context);
        if (Parents.empty())
          return;
        const DynTypedNode &Parent = Parents[0];
        if (Parent.get<ImplicitCastExpr>() || Parent.get<ParenExpr>()) {
          Node = Parent;
          continue;
        }
        // Moved, which is what is done with the move, like awaiting
        // `fg_AllDone(fg_Move(Results))`, and otherwise replaces it.
        if (const auto *Move = Parent.get<CallExpr>();
            Move && Move->getNumArgs() == 1 &&
            (isMoveFunction(calleeName(Move)))) {
          Node = Parent;
          continue;
        }
        const auto *Child = Node.get<Expr>();
        // Bound to a local reference, which can't be bound to another.
        if (const auto *Alias = Parent.get<VarDecl>();
            Alias && Alias->hasLocalStorage() &&
            Alias->getType()->isReferenceType() && Alias->getInit() &&
            Alias->getInit()->IgnoreParenImpCasts() == Ref) {
          Collections.insert(Alias);
          return;
        }
        // `Future > Results`, `Results[Key] = Future` and
        // `Results.f_Insert(Future)` add futures; the cleanup awaits them.
        if (const auto *Call = Parent.get<CXXOperatorCallExpr>()) {
          if (Call->getOperator() == OO_Greater && Call->getNumArgs() == 2 &&
              Call->getArg(1) == Child)
            return;
          // `Future > Results[Key]`; assigning a slot, `Results[Key] = ...`,
          // can drop a future that was there.
          if (Call->getOperator() == OO_Subscript && Call->getArg(0) == Child) {
            DynTypedNode Slot = Parent;
            for (int SlotDepth = 0; SlotDepth < 4; ++SlotDepth) {
              const auto SlotParents = instantiatedParents(Slot, Context);
              if (SlotParents.empty())
                break;
              if (SlotParents[0].get<ImplicitCastExpr>() ||
                  SlotParents[0].get<ParenExpr>()) {
                Slot = SlotParents[0];
                continue;
              }
              if (const auto *Into = SlotParents[0].get<CXXOperatorCallExpr>();
                  Into && Into->getOperator() == OO_Greater &&
                  Into->getNumArgs() == 2 &&
                  Into->getArg(1)->IgnoreParenImpCasts() ==
                      Slot.get<Expr>()->IgnoreParenImpCasts())
                return;
              break;
            }
            Replaced = true;
            return;
          }
        }
        if (const auto *Member = Parent.get<MemberExpr>()) {
          const auto *Method = dyn_cast<CXXMethodDecl>(Member->getMemberDecl());
          if (Method && (Method->isConst() ||
                         (Method->getIdentifier() &&
                          Method->getName().starts_with("f_Insert"))))
            return;
        }
        if (Parent.get<LambdaExpr>())
          return;
        // Collecting the futures empties it, so it must be awaited, like
        // `co_await fg_AllDone(Results)`; one whose future is dropped leaves
        // nothing to join.
        if (const auto *Call = Parent.get<CallExpr>();
            Call && Call->getNumArgs() == 1 && Call->getArg(0) == Child &&
            Call->getDirectCallee() && Call->getDirectCallee()->getIdentifier() &&
            Call->getDirectCallee()->getName().starts_with("fg_AllDone")) {
          DynTypedNode Up = DynTypedNode::create(*Call);
          for (int UpDepth = 0; UpDepth < 8; ++UpDepth) {
            const auto UpParents = instantiatedParents(Up, Context);
            if (UpParents.empty())
              break;
            if (UpParents[0].get<CoawaitExpr>())
              return;
            const auto *UpExpr = UpParents[0].get<Expr>();
            if (!UpExpr)
              break;
            // Through `.f_Wrap()` and the like, which give its future.
            if (const auto *Member = dyn_cast<MemberExpr>(UpExpr);
                Member && Member->getBase()->IgnoreParenImpCasts() ==
                              Up.get<Expr>()->IgnoreParenImpCasts()) {
              Up = UpParents[0];
              continue;
            }
            // Through context, `fg_AllDone(Results) % "Joining"`, which
            // gives its future.
            if (const auto *Context = dyn_cast<CXXOperatorCallExpr>(UpExpr);
                Context && Context->getOperator() == OO_Percent &&
                Context->getNumArgs() == 2 &&
                Context->getArg(1) != Up.get<Expr>()) {
              Up = UpParents[0];
              continue;
            }
            if (!isa<ParenExpr, ImplicitCastExpr, MaterializeTemporaryExpr,
                     CXXBindTemporaryExpr, ExprWithCleanups, CXXMemberCallExpr>(
                    UpExpr))
              break;
            Up = UpParents[0];
          }
        }
        Replaced = true;
        return;
      }
      // What can't be told, past that many parentheses, may replace it.
      Replaced = true;
      return;
    }
    for (const Stmt *Child : childrenOnce(S))
      VisitCollection(Child);
  };
  if (Function)
    VisitCollection(Function->getBody());
  return !Replaced;
}

/// Whether a loop around \p Site also has \p Location in it, so that \p Site
/// can run again after it, but not \p Fresh, the declaration of what is made
/// again each time it runs, in it and in those around it.
static bool inLoopAround(const Stmt *Site, SourceLocation Location,
                         SourceLocation Fresh, ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  DynTypedNode Node = DynTypedNode::create(*Site);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty() || Parents[0].get<FunctionDecl>() ||
        Parents[0].get<LambdaExpr>())
      return false;
    Node = Parents[0];
    if (Node.get<ForStmt>() || Node.get<WhileStmt>() || Node.get<DoStmt>() ||
        Node.get<CXXForRangeStmt>()) {
      const SourceRange Range = Node.getSourceRange();
      auto Contains = [&](SourceLocation Inner) {
        return Inner.isValid() &&
               !SM.isBeforeInTranslationUnit(Inner, Range.getBegin()) &&
               !SM.isBeforeInTranslationUnit(Range.getEnd(), Inner);
      };
      if (Contains(Fresh))
        return false;
      if (Contains(Location))
        return true;
    }
  }
  return false;
}

/// Whether \p Function has a `goto` or a label, which can run what is written
/// before something again after it.
static bool hasJumps(const FunctionDecl *Function) {
  if (!Function || !Function->getBody())
    return true;
  // Each function once: each call that is joined in it asks.
  struct CJumps {
    llvm::DenseMap<const FunctionDecl *, bool> Decided;
  };
  llvm::DenseMap<const FunctionDecl *, bool> &Decided =
      astCache<CJumps>(Function->getASTContext()).Decided;
  if (const auto It = Decided.find(Function); It != Decided.end())
    return It->second;
  bool Jumps = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Jumps)
      return;
    if (isa<GotoStmt, IndirectGotoStmt, LabelStmt>(S)) {
      Jumps = true;
      return;
    }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Function->getBody());
  Decided[Function] = Jumps;
  return Jumps;
}

/// The guard that joins \p Collection, declared after it and before \p Site
/// in a scope around \p Site.
static const VarDecl *findJoinGuard(const VarDecl *Collection, const Stmt *Site,
                                    ASTContext &Context,
                                    SourceLocation &JoinEarlyAwait) {
  const SourceManager &SM = Context.getSourceManager();
  // An early await in the same full expression, `Slots[(fg_Read(Local) >
  // Results, 0)] = (co_await fg_Move(Guard), 0)`, can run before the call,
  // whatever the order they are written in, so it has to be after its end.
  const auto *SiteExpr = dyn_cast<Expr>(Site);
  const Expr *Full = SiteExpr ? fullExpressionOf(SiteExpr, Context) : nullptr;
  const SourceLocation SiteEnd = Full ? Full->getEndLoc() : Site->getEndLoc();
  DynTypedNode Node = DynTypedNode::create(*Site);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    if (Parents[0].get<LambdaExpr>() || Parents[0].get<FunctionDecl>())
      return nullptr;
    if (const auto *Compound = Parents[0].get<CompoundStmt>()) {
      const Stmt *Child = Node.get<Stmt>();
      for (const Stmt *Statement : Compound->body()) {
        if (Statement == Child ||
            (isa<Expr>(Statement) && Child && isa<Expr>(Child) &&
             isSameOperand(cast<Expr>(Statement), cast<Expr>(Child))))
          break;
        const auto *Declaration = dyn_cast<DeclStmt>(Statement);
        if (!Declaration)
          continue;
        for (const Decl *D : Declaration->decls()) {
          const auto *Guard = dyn_cast<VarDecl>(D);
          SourceLocation EarlyAwait;
          if (Guard &&
              SM.isBeforeInTranslationUnit(Collection->getLocation(),
                                           Guard->getLocation()) &&
              SM.isBeforeInTranslationUnit(Guard->getLocation(),
                                           Site->getBeginLoc()) &&
              isJoinGuard(Guard, Collection, Context, EarlyAwait) &&
              // Awaited early, it joins only what was started before, and not
              // again in a loop around both.
              (EarlyAwait.isInvalid() ||
               (SM.isBeforeInTranslationUnit(SiteEnd, EarlyAwait) &&
                !inLoopAround(Site, EarlyAwait, Guard->getLocation(),
                              Context) &&
                !hasJumps(dyn_cast_or_null<FunctionDecl>(
                    Guard->getParentFunctionOrMethod()))))) {
            JoinEarlyAwait = EarlyAwait;
            return Guard;
          }
        }
      }
    }
    Node = Parents[0];
  }
  return nullptr;
}

std::optional<RunsToCompletionJoin> joinOf(const Expr *Future,
                                           ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Future);
  const VarDecl *Collection = nullptr;
  for (int Depth = 0; Depth < 32 && !Collection; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return std::nullopt;
    const DynTypedNode &Parent = Parents[0];
    const auto *Child = Node.get<Expr>();
    if (const auto *Await = Parent.get<CoawaitExpr>()) {
      if (isSameOperand(Await->getOperand(), Child) ||
          isSameOperand(Await->getCommonExpr(), Child))
        return RunsToCompletionJoin{};
      return std::nullopt;
    }
    if (const auto *Variable = Parent.get<VarDecl>()) {
      if (!Variable->hasLocalStorage())
        return std::nullopt;
      Collection = Variable;
      break;
    }
    const auto *ParentExpr = Parent.get<Expr>();
    if (!ParentExpr)
      return std::nullopt;
    if (isa<ParenExpr, ImplicitCastExpr, MaterializeTemporaryExpr,
            CXXBindTemporaryExpr, ExprWithCleanups, CXXFunctionalCastExpr>(
            ParentExpr)) {
      Node = Parent;
      continue;
    }
    // The branch of `Choose ? fg_Read(A) : fg_Read(B)` that runs gives the
    // future of its call.
    if (const auto *Conditional =
            dyn_cast<AbstractConditionalOperator>(ParentExpr);
        Conditional && !isSameOperand(Conditional->getCond(), Child)) {
      Node = Parent;
      continue;
    }
    // A copy or move of the future, not something made from it.
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(ParentExpr);
        Construct && Construct->getNumArgs() == 1 &&
        Construct->getConstructor()->isCopyOrMoveConstructor()) {
      Node = Parent;
      continue;
    }
    // `.f_Wrap()`, `.f_Dispatch()` and `.f_Move()`, which moves it, give a
    // future of the same call.
    if (const auto *Member = dyn_cast<MemberExpr>(ParentExpr)) {
      const auto *Method = dyn_cast<CXXMethodDecl>(Member->getMemberDecl());
      const auto CallParents = instantiatedParents(*Member, Context);
      const auto *Call = CallParents.empty()
                             ? nullptr
                             : CallParents[0].get<CXXMemberCallExpr>();
      if (!Method || !Method->getIdentifier() || !Call ||
          (Method->getName() != "f_Wrap" && Method->getName() != "f_Dispatch" &&
           Method->getName() != "f_Move") ||
          llvm::any_of(Call->arguments(), [&](const Expr *Argument) {
            return hasSuspension(Argument, Context);
          }))
        return std::nullopt;
      Node = CallParents[0];
      continue;
    }
    if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(ParentExpr)) {
      if (Call->getNumArgs() != 2)
        return std::nullopt;
      // `Future % "Context"` gives a future of the same call.
      if (Call->getOperator() == OO_Percent &&
          isSameOperand(Call->getArg(0), Child)) {
        if (hasSuspension(Call->getArg(1), Context))
          return std::nullopt;
        Node = Parent;
        continue;
      }
      // `FutureA + FutureB` makes a pack whose await collects both.
      if (Call->getOperator() == OO_Plus) {
        const Expr *Other = isSameOperand(Call->getArg(0), Child)
                                ? Call->getArg(1)
                                : Call->getArg(0);
        const StringRef OtherName = recordName(Other->getType());
        // The other one can start a future too, like `fg_Read(B)`, which
        // gives what it throws to its future, also in a pack of them,
        // `fg_Read(A) + fg_Read(B)`.
        std::function<bool(const Expr *, int)> StartsFutures =
            [&](const Expr *E, int Depth) {
              E = skipWrappers(E)->IgnoreParenImpCasts();
              // A pack of them.
              if (const auto *Pack = dyn_cast<CXXOperatorCallExpr>(E);
                  Pack && Pack->getOperator() == OO_Plus &&
                  Pack->getNumArgs() == 2 && Depth < 16)
                return StartsFutures(Pack->getArg(0), Depth + 1) &&
                       StartsFutures(Pack->getArg(1), Depth + 1);
              const auto *Starts = dyn_cast<CallExpr>(E);
              // Not one whose arguments, or what gives its object,
              // `fg_GetFactory().f_Run()`, suspend, where it can abort.
              const auto *Member = dyn_cast_or_null<CXXMemberCallExpr>(Starts);
              return Starts && Starts->getDirectCallee() &&
                     returnsFuture(Starts->getDirectCallee()) &&
                     llvm::none_of(Starts->arguments(),
                                   [&](const Expr *Argument) {
                                     return hasSuspension(Argument, Context);
                                   }) &&
                     !(Member && Member->getImplicitObjectArgument() &&
                       hasSuspension(Member->getImplicitObjectArgument(),
                                     Context));
            };
        const bool StartsFuture = StartsFutures(Other, 0);
        // Also a bound actor call, `Future + Actor.f_Bind<...>()`.
        if ((!OtherName.starts_with("TCFuture") &&
             OtherName != "TCBoundActorCall") ||
            (!StartsFuture && hasSuspension(Other, Context)))
          return std::nullopt;
        Node = Parent;
        continue;
      }
      // `Future > Results` and `Results[Key] = Future` collect it.
      // `Future > Results` collects it; assigning it to a slot can drop the
      // future that was there.
      // What collects it must not leave first: a slot's key, like
      // `Results[co_await ...]`, but not the lookup of the slot itself.
      const Expr *Into = Call->getNumArgs() == 2
                             ? Call->getArg(1)->IgnoreParenImpCasts()
                             : nullptr;
      const Expr *Leaves = Into;
      if (const auto *Slot = dyn_cast_or_null<CXXOperatorCallExpr>(Into);
          Slot && Slot->getOperator() == OO_Subscript && Slot->getNumArgs() == 2)
        Leaves = Slot->getArg(1);
      if (Call->getOperator() == OO_Greater &&
          isSameOperand(Call->getArg(0), Child) &&
          !hasSuspension(Leaves, Context))
        Collection = localOf(Call->getArg(1));
      if (!Collection)
        return std::nullopt;
      break;
    }
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(ParentExpr)) {
      const CXXMethodDecl *Method = Call->getMethodDecl();
      // What it throws ends the program, unless the coroutine captures it,
      // where a local can't rely on the join anyway.
      if (!Method || !Method->getIdentifier() ||
          !Method->getName().starts_with("f_Insert") ||
          hasSuspension(Call->getImplicitObjectArgument(), Context) ||
          llvm::any_of(Call->arguments(), [&](const Expr *Argument) {
            return !isSameOperand(Argument, Child) &&
                   hasSuspension(Argument, Context);
          }))
        return std::nullopt;
      Collection = localOf(Call->getImplicitObjectArgument());
      if (!Collection)
        return std::nullopt;
      break;
    }
    if (const auto *Call = dyn_cast<CallExpr>(ParentExpr);
        Call && Call->getNumArgs() == 1 && (isMoveFunction(calleeName(Call)))) {
      Node = Parent;
      continue;
    }
    // The cleanup of an awaited `fg_AsyncDestroy`, which its guard starts
    // when its scope ends and the coroutine joins when it finishes, after its
    // locals are gone, or where the guard is awaited early.
    if (const auto *Call = dyn_cast<CallExpr>(ParentExpr);
        Call && Call->getNumArgs() >= 1 &&
        isSameOperand(Call->getArg(0), Child) &&
        llvm::is_contained(llvm::ArrayRef<StringRef>(
                               std::begin(AsyncDestroyFunctions),
                               std::end(AsyncDestroyFunctions)),
                           calleeName(Call))) {
      SourceLocation EarlyAwait;
      if (const VarDecl *Guard = cleanupGuard(Call, Context, EarlyAwait))
        return RunsToCompletionJoin{Guard, nullptr, EarlyAwait};
    }
    return std::nullopt;
  }
  if (!Collection)
    return std::nullopt;
  SourceLocation EarlyAwait;
  const VarDecl *Guard = findJoinGuard(Collection, Future, Context, EarlyAwait);
  if (!Guard)
    return std::nullopt;
  return RunsToCompletionJoin{Guard, Collection, EarlyAwait};
}

/// The call that \p Lambda, past parentheses, implicit nodes, copies, moves and
/// conversions, is an argument of, and whether it is the callee instead.
static const CallExpr *callTakingLambda(const LambdaExpr *Lambda,
                                        ASTContext &Context, bool &Callee) {
  Callee = false;
  DynTypedNode Node = DynTypedNode::create(*Lambda);
  for (int Depth = 0; Depth < 16; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    const auto *Parent = Parents.empty() ? nullptr : Parents[0].get<Expr>();
    if (!Parent)
      return nullptr;
    if (isa<ParenExpr, ImplicitCastExpr, MaterializeTemporaryExpr,
            CXXBindTemporaryExpr, CXXFunctionalCastExpr, CXXConstructExpr>(
            Parent)) {
      Node = Parents[0];
      continue;
    }
    if (const auto *Call = dyn_cast<CallExpr>(Parent)) {
      if (Call->getNumArgs() == 1 && !isa<CXXOperatorCallExpr>(Call) &&
          (isMoveFunction(calleeName(Call)))) {
        Node = Parents[0];
        continue;
      }
      const auto *Child = Node.get<Expr>();
      if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
          Operator && Operator->getOperator() == OO_Call &&
          Operator->getNumArgs() >= 1 &&
          isSameOperand(Operator->getArg(0), Child))
        Callee = true;
      return Call;
    }
    return nullptr;
  }
  return nullptr;
}

static const VarDecl *heldInLocal(const LambdaExpr *Lambda,
                                  ASTContext &Context);
static void forEachReference(const Stmt *Body, const VarDecl *Variable,
                             llvm::function_ref<void(const DeclRefExpr *)> Callback);

/// How many times a lambda being looked at was reached again, whose result
/// then depends on what is being looked at, and isn't kept.
static thread_local unsigned s_nLambdaJoinCycles = 0;

static std::optional<RunsToCompletionJoin>
lambdaJoinUncached(const LambdaExpr *Lambda, ASTContext &Context,
                   bool *Abortable);

std::optional<RunsToCompletionJoin>
lambdaJoin(const LambdaExpr *Lambda, ASTContext &Context, bool *Abortable) {
  // What a lambda's join is, kept while the outermost question is answered,
  // so that lambdas in lambdas, which each ask about the ones around them,
  // are each looked at once, not once per path to them.
  struct CJoin {
    std::optional<RunsToCompletionJoin> Join;
    bool Abortable = false;
  };
  using CCache = llvm::DenseMap<const LambdaExpr *, CJoin>;
  static thread_local CCache *s_pCache = nullptr;
  std::optional<CCache> Owned;
  if (!s_pCache)
    s_pCache = &Owned.emplace();
  const llvm::scope_exit Release([&] {
    if (Owned)
      s_pCache = nullptr;
  });
  if (auto It = s_pCache->find(Lambda); It != s_pCache->end()) {
    if (Abortable)
      *Abortable = It->second.Abortable;
    return It->second.Join;
  }
  const unsigned nCycles = s_nLambdaJoinCycles;
  bool LambdaAbortable = false;
  std::optional<RunsToCompletionJoin> Join =
      lambdaJoinUncached(Lambda, Context, &LambdaAbortable);
  if (Abortable)
    *Abortable = LambdaAbortable;
  if (s_nLambdaJoinCycles == nCycles)
    s_pCache->try_emplace(Lambda, CJoin{Join, LambdaAbortable});
  return Join;
}

static std::optional<RunsToCompletionJoin>
lambdaJoinUncached(const LambdaExpr *Lambda, ASTContext &Context,
                   bool *Abortable) {
  if (Abortable)
    *Abortable = false;
  // A lambda that calls itself, also through a function wrapper, is joined
  // where it is called first; deeper calls aren't followed again.
  static thread_local llvm::SmallPtrSet<const LambdaExpr *, 8> InProgress;
  // A call of itself, in its own body, runs where the outer call does, which
  // that call decides.
  if (!InProgress.insert(Lambda).second) {
    ++s_nLambdaJoinCycles;
    return RunsToCompletionJoin{};
  }
  struct CLeave {
    const LambdaExpr *Lambda;
    ~CLeave() { InProgress.erase(Lambda); }
  } Leave{Lambda};
  const Expr *Actor = nullptr;
  if (const Expr *Dispatch = closureDispatch(Lambda, Lambda->getLambdaClass(),
                                             Context, &Actor)) {
    // Without an actor, like `g_Dispatch / ...`, it runs on the current one,
    // which before an entry switches is that of its caller.
    if (Abortable)
      *Abortable = Actor ? !isConcurrentActor(Actor, Context)
                         : !runsOnConcurrentActor(Dispatch, Context);
    return joinOf(Dispatch, Context);
  }
  bool Callee = false;
  if (const CallExpr *Call = callTakingLambda(Lambda, Context, Callee)) {
    // What `fg_CallSafe` and `fg_AsyncDestroy` run is their first argument;
    // the cleanup of `fg_AsyncDestroy` runs when its guard is destroyed.
    const bool FirstArgument =
        Call->getNumArgs() >= 1 && asLambda(Call->getArg(0)) == Lambda;
    if (FirstArgument &&
        llvm::is_contained(llvm::ArrayRef<StringRef>(
                               std::begin(AsyncDestroyFunctions),
                               std::end(AsyncDestroyFunctions)),
                           calleeName(Call)))
    {
      SourceLocation EarlyAwait;
      if (const VarDecl *Guard = cleanupGuard(Call, Context, EarlyAwait))
        return RunsToCompletionJoin{Guard, nullptr, EarlyAwait};
      return std::nullopt;
    }
    if (Callee || (calleeName(Call) == "fg_CallSafe" && FirstArgument))
      return joinOf(Call, Context);
    return std::nullopt;
  }
  // A lambda held in a local, like `auto fApply = [&] ...`, each of whose
  // uses is a call awaited in its full-expression.
  const VarDecl *Holder = heldInLocal(Lambda, Context);
  const auto *Function =
      Holder ? dyn_cast_or_null<FunctionDecl>(Holder->getParentFunctionOrMethod())
             : nullptr;
  // Every use is a joined call, so it isn't given anywhere else.
  if (!Function || !Function->getBody())
    return std::nullopt;
  bool AnyUse = false;
  bool AllJoined = true;
  forEachReference(Function->getBody(), Holder, [&](const DeclRefExpr *Ref) {
    AnyUse = true;
    const CallExpr *Call = nullptr;
    DynTypedNode Node = DynTypedNode::create(*Ref);
    for (int Depth = 0; Depth < 4 && !Call; ++Depth) {
      const auto Up = instantiatedParents(Node, Context);
      if (Up.empty())
        break;
      if (const auto *Operator = Up[0].get<CXXOperatorCallExpr>();
          Operator && Operator->getOperator() == OO_Call &&
          Operator->getNumArgs() >= 1 &&
          isSameOperand(Operator->getArg(0), Node.get<Expr>()))
        Call = Operator;
      // Also called by name, `fLambda.operator()<int>(Value)`.
      else if (const CXXMemberCallExpr *Named =
                   namedCallOperatorCall(Up[0], Node.get<Expr>(), Context))
        Call = Named;
      // Run in place by `fg_CallSafe(fLambda)`.
      else if (const auto *Safe = Up[0].get<CallExpr>();
               Safe && !isa<CXXOperatorCallExpr>(Safe) &&
               calleeName(Safe) == "fg_CallSafe" && Safe->getNumArgs() >= 1 &&
               isSameOperand(Safe->getArg(0), Node.get<Expr>()))
        Call = Safe;
      else if (Up[0].get<ImplicitCastExpr>() || Up[0].get<ParenExpr>())
        Node = Up[0];
      else
        break;
    }
    std::optional<RunsToCompletionJoin> Join =
        Call ? joinOf(Call, Context) : std::nullopt;
    // Each call runs where code runs to completion too: one in a coroutine
    // that can be aborted doesn't make the lambda run to completion.
    // A lambda that isn't a coroutine, like a factory of a cleanup, has
    // finished when its call returns, however what it returns is joined.
    AllJoined = AllJoined && Join &&
                (!Join->Guard || !isCoroutine(Lambda->getCallOperator())) &&
                inRunsToCompletionContext(Call, Context);
  });
  if (!AnyUse || !AllJoined)
    return std::nullopt;
  return RunsToCompletionJoin{};
}

/// The local that \p Lambda initializes, past implicit nodes and copies.
static const VarDecl *heldInLocal(const LambdaExpr *Lambda,
                                  ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Lambda);
  for (int Depth = 0; Depth < 8; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    if (const auto *Variable = Parents[0].get<VarDecl>())
      return Variable->hasLocalStorage() && !isa<ParmVarDecl>(Variable)
                 ? Variable
                 : nullptr;
    const auto *Parent = Parents[0].get<Expr>();
    if (!Parent || !isa<ParenExpr, ImplicitCastExpr, MaterializeTemporaryExpr,
                        CXXBindTemporaryExpr, ExprWithCleanups,
                        CXXConstructExpr>(Parent))
      return nullptr;
    Node = Parents[0];
  }
  return nullptr;
}

/// Calls \p Callback for each reference to \p Variable in \p Body.
static void forEachReference(const Stmt *Body, const VarDecl *Variable,
                             llvm::function_ref<void(const DeclRefExpr *)> Callback) {
  if (!Body)
    return;
  if (const auto *Ref = dyn_cast<DeclRefExpr>(Body); Ref && Ref->getDecl() == Variable)
    Callback(Ref);
  // What a lambda captures it with isn't a use; what its body does with it is.
  // An init capture, like `[fCopy = fRead]`, makes a copy that is used under
  // another name.
  if (const auto *Lambda = dyn_cast<LambdaExpr>(Body)) {
    auto Init = Lambda->capture_init_begin();
    for (const LambdaCapture &Capture : Lambda->captures()) {
      const Expr *CaptureInit = *Init++;
      if (CaptureInit && Capture.capturesVariable() &&
          cast<VarDecl>(Capture.getCapturedVar())->isInitCapture())
        forEachReference(CaptureInit, Variable, Callback);
    }
    forEachReference(Lambda->getBody(), Variable, Callback);
    return;
  }
  for (const Stmt *Child : childrenOnce(Body))
    forEachReference(Child, Variable, Callback);
}

/// Whether \p Lambda is given to a function that runs to completion, or that
/// begins to, which runs it in its context.
static bool givenToRunsToCompletion(const LambdaExpr *Lambda,
                                    ASTContext &Context) {
  bool Callee = false;
  const CallExpr *Call = callTakingLambda(Lambda, Context, Callee);
  if (!Call || Callee)
    return false;
  auto RunsToCompletion = [](const FunctionDecl *Function) {
    return isRunsToCompletion(Function) || isRunsToCompletionEntry(Function);
  };
  const FunctionDecl *Function = Call->getDirectCallee();
  return Function && RunsToCompletion(Function);
}

namespace {
/// Finds the first suspension of a function, outside of its lambdas.
class FirstSuspension : public OncePerTypeVisitor<FirstSuspension> {
public:
  bool shouldVisitLambdaBody() const { return false; }
  // What a lambda captures is evaluated where it is made; its body isn't.
  bool TraverseLambdaExpr(LambdaExpr *Lambda) {
    for (Expr *Init : Lambda->capture_inits()) {
      if (Init && !TraverseStmt(Init))
        return false;
    }
    return true;
  }
  // The functions of a local class are functions of their own.
  bool TraverseCXXRecordDecl(CXXRecordDecl *) { return true; }

  // A discarded branch of `if constexpr` doesn't run, nor one of a condition
  // that is constant, `if (false)`, nor the branch of `if consteval` that
  // runs when constant evaluated, which a coroutine never is.
  bool TraverseIfStmt(IfStmt *If) {
    if (If->isConsteval()) {
      Stmt *Runs = If->isNegatedConsteval() ? If->getThen() : If->getElse();
      return !Runs || TraverseStmt(Runs);
    }
    if (!Context)
      return RecursiveASTVisitor::TraverseIfStmt(If);
    bool Value = false;
    if (!If->isConstexpr() && !If->isConsteval() && If->getCond() &&
        !If->getConditionVariable() && !If->getCond()->isValueDependent() &&
        If->getCond()->EvaluateAsBooleanCondition(Value, *Context) &&
        !hasJumpTarget(Value ? If->getElse() : If->getThen())) {
      if (If->getInit() && !TraverseStmt(If->getInit()))
        return false;
      if (!TraverseStmt(If->getCond()))
        return false;
      Stmt *Taken = Value ? If->getThen() : If->getElse();
      return !Taken || TraverseStmt(Taken);
    }
    if (!If->isConstexpr())
      return RecursiveASTVisitor::TraverseIfStmt(If);
    if (If->getInit() && !TraverseStmt(If->getInit()))
      return false;
    if (std::optional<Stmt *> Taken = If->getNondiscardedCase(*Context))
      return !*Taken || TraverseStmt(*Taken);
    return RecursiveASTVisitor::TraverseIfStmt(If);
  }

  // An assignment evaluates its right side first.
  bool TraverseBinaryOperator(BinaryOperator *Binary) {
    if (!Binary->isAssignmentOp())
      return RecursiveASTVisitor::TraverseBinaryOperator(Binary);
    return TraverseStmt(Binary->getRHS()) && TraverseStmt(Binary->getLHS());
  }
  bool TraverseCompoundAssignOperator(CompoundAssignOperator *Binary) {
    return TraverseStmt(Binary->getRHS()) && TraverseStmt(Binary->getLHS());
  }
  bool TraverseCXXOperatorCallExpr(CXXOperatorCallExpr *Call) {
    if (!Call->isAssignmentOp() || Call->getNumArgs() != 2)
      return RecursiveASTVisitor::TraverseCXXOperatorCallExpr(Call);
    return TraverseStmt(Call->getArg(1)) && TraverseStmt(Call->getArg(0));
  }

  bool VisitCoawaitExpr(CoawaitExpr *Await) { return visit(Await); }
  bool VisitCoyieldExpr(CoyieldExpr *Yield) { return visit(Yield); }

  const Expr *First = nullptr;
  const ASTContext *Context = nullptr;
  /// The statements of the function's graph, and those that can run, when
  /// it can be made: not after a `co_return`, nor in a loop that never runs,
  /// `while (false)`.
  const llvm::DenseSet<const Stmt *> *InGraph = nullptr;
  const llvm::DenseSet<const Stmt *> *CanRun = nullptr;

private:
  bool visit(Expr *Suspend) {
    if (!isSuspension(Suspend))
      return true;
    if (InGraph && InGraph->contains(Suspend) && !CanRun->contains(Suspend))
      return true;
    First = Suspend;
    return false;
  }
};
} // namespace

/// The operand that \p Suspend awaits.
const Expr *awaitedOperand(const Expr *Suspend) {
  if (const auto *Await = dyn_cast<CoroutineSuspendExpr>(Suspend))
    return Await->getOperand();
  return nullptr;
}

static const Expr *findFirstSuspension(const FunctionDecl *Function) {
  if (!Function->getBody())
    return nullptr;
  llvm::DenseSet<const Stmt *> InGraph;
  llvm::DenseSet<const Stmt *> CanRun;
  bool HasGraph = false;
  {
    Stmt *Body = Function->getBody();
    if (auto *Coroutine = dyn_cast<CoroutineBodyStmt>(Body))
      Body = Coroutine->getBody();
    CFG::BuildOptions Options;
    Options.setAllAlwaysAdd();
    ASTContext &Context = Function->getASTContext();
    if (const std::unique_ptr<CFG> Graph =
            CFG::buildCFG(Function, Body, &Context, Options)) {
      HasGraph = true;
      const llvm::BitVector Reachable = reachableBlocks(*Graph);
      for (const CFGBlock *Block : *Graph) {
        if (!Block)
          continue;
        for (const CFGElement &Element : *Block)
          if (const std::optional<CFGStmt> Statement =
                  Element.getAs<CFGStmt>()) {
            InGraph.insert(Statement->getStmt());
            if (Reachable.test(Block->getBlockID()))
              CanRun.insert(Statement->getStmt());
          }
      }
    }
  }
  FirstSuspension Finder;
  Finder.Context = &Function->getASTContext();
  if (HasGraph) {
    Finder.InGraph = &InGraph;
    Finder.CanRun = &CanRun;
  }
  Finder.TraverseStmt(Function->getBody());
  if (!Finder.First)
    return nullptr;
  // What an await awaits is evaluated first: a suspension in it, like
  // `co_await fg_Entry(co_await Other)`, comes before.
  for (int Depth = 0; Depth < 8; ++Depth) {
    FirstSuspension Inner;
    Inner.Context = Finder.Context;
    Inner.InGraph = Finder.InGraph;
    Inner.CanRun = Finder.CanRun;
    Inner.TraverseStmt(const_cast<Expr *>(awaitedOperand(Finder.First)));
    if (!Inner.First)
      break;
    Finder.First = Inner.First;
  }
  return Finder.First;
}

const Expr *firstSuspension(const FunctionDecl *Function) {
  // Each function's once in a translation unit: a check asks it for each
  // statement of the function, and finding it makes the function's graph.
  struct CCache {
    uint64_t AST = 0;
    llvm::DenseMap<const FunctionDecl *, const Expr *> First;
  };
  static thread_local CCache s_Cache;
  if (const uint64_t AST = astIdentity(Function->getASTContext());
      s_Cache.AST != AST) {
    s_Cache.First.clear();
    s_Cache.AST = AST;
  }
  if (const auto It = s_Cache.First.find(Function); It != s_Cache.First.end())
    return It->second;
  const Expr *First = findFirstSuspension(Function);
  s_Cache.First[Function] = First;
  return First;
}

bool inRunsToCompletionContext(const Stmt *S, ASTContext &Context,
                               bool AfterSwitch) {
  DynTypedNode Node = DynTypedNode::create(*S);
  for (int Depth = 0; Depth < 512; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    const DynTypedNode &Parent = Parents[0];
    if (const auto *Lambda = Parent.get<LambdaExpr>()) {
      // What a capture is initialized with runs where the lambda is made.
      const bool Inside = Node.get<CompoundStmt>() == Lambda->getBody() ||
                          Node.get<CXXRecordDecl>() || Node.get<Decl>();
      if (Inside) {
        if (givenToRunsToCompletion(Lambda, Context))
          return true;
        bool Abortable = false;
        if (!lambdaJoin(Lambda, Context, &Abortable) || Abortable)
          return false;
      }
    } else if (const auto *Function = Parent.get<FunctionDecl>()) {
      const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      if (!Method || !Method->getParent()->isLambda())
        return isRunsToCompletion(Function) ||
               (isRunsToCompletionEntry(Function) &&
                (!AfterSwitch || runsOnConcurrentActor(S, Context)));
    }
    Node = Parent;
  }
  return false;
}

/// The local that \p Lambda initializes, like `fWork` of `auto fWork = [] {
/// ... }`, if it does.
static const VarDecl *localHolder(const LambdaExpr *Lambda,
                                  ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Lambda);
  for (int Depth = 0; Depth < 8; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    if (const auto *Variable = Parents[0].get<VarDecl>())
      return Variable->hasLocalStorage() ? Variable : nullptr;
    const auto *Parent = Parents[0].get<Expr>();
    if (!Parent ||
        !isa<ParenExpr, ImplicitCastExpr, MaterializeTemporaryExpr,
             CXXBindTemporaryExpr, ExprWithCleanups, CXXConstructExpr>(Parent))
      return nullptr;
    Node = Parents[0];
  }
  return nullptr;
}

/// Whether the entry that \p Call calls uses the parameter that its argument
/// \p iArgument is given to only after it has switched to a concurrent actor,
/// like `co_await fg_ContinueRunningOnActor(fg_ConcurrentActor()); co_await
/// fg_CallSafe(fCallback);`, not before, like `auto Pending =
/// fg_CallSafe(fCallback);` before the switch, also when it gives it on to
/// another entry.
static bool isUsedAfterSwitch(const CallExpr *Call, unsigned iArgument,
                              ASTContext &Context, int Depth = 0) {
  if (Depth > 4)
    return false;
  const FunctionDecl *Entry = Call->getDirectCallee();
  if (!Entry || !isRunsToCompletionEntry(Entry))
    return false;
  // An entry defined in another file can't be looked into, and is trusted.
  const FunctionDecl *Definition = nullptr;
  if (!Entry->hasBody(Definition) || !Definition || !Definition->getBody())
    return true;
  // The object of a member operator is its first argument, unless it is an
  // explicit object parameter, `this CEntry Self`, which is a parameter.
  const auto *Method = dyn_cast<CXXMethodDecl>(Call->getDirectCallee());
  const unsigned Offset = isa<CXXOperatorCallExpr>(Call) && Method &&
                                  !Method->isExplicitObjectMemberFunction()
                              ? 1
                              : 0;
  if (iArgument < Offset || iArgument - Offset >= Definition->getNumParams())
    return false;
  // The body as written, not the moves of the parameters into the frame of a
  // coroutine.
  const Stmt *Body = Definition->getBody();
  if (const auto *Coroutine = dyn_cast<CoroutineBodyStmt>(Body))
    Body = Coroutine->getBody();
  using namespace ast_matchers;
  const auto Uses = match(
      findAll(declRefExpr(to(equalsNode(
                              Definition->getParamDecl(iArgument - Offset))))
                  .bind("use")),
      *Body, Context);
  return llvm::all_of(Uses, [&](const BoundNodes &Nodes) {
    const auto *Use = Nodes.getNodeAs<DeclRefExpr>("use");
    // Not one that isn't evaluated, `sizeof(_fCallback)`.
    if (runsOnConcurrentActor(Use, Context) ||
        isInUnevaluatedOperand(DynTypedNode::create(*Use), Context))
      return true;
    // Given on to another entry, like `co_await fg_Run(fg_Move(fCommand))`.
    DynTypedNode Node = DynTypedNode::create(*Use);
    for (int Up = 0; Up < 8; ++Up) {
      const auto Parents = instantiatedParents(Node, Context);
      const auto *Parent = Parents.empty() ? nullptr : Parents[0].get<Expr>();
      if (!Parent)
        return false;
      if (const auto *Outer = dyn_cast<CallExpr>(Parent)) {
        const StringRef Name = calleeName(Outer);
        if (Outer->getNumArgs() == 1 &&
            (isMoveFunction(Name) || isForwardFunction(Name))) {
          Node = Parents[0];
          continue;
        }
        for (unsigned iOuter = 0; iOuter < Outer->getNumArgs(); ++iOuter)
          if (isSameOperand(Outer->getArg(iOuter), Node.get<Expr>()))
            return isUsedAfterSwitch(Outer, iOuter, Context, Depth + 1);
        return false;
      }
      if (!isa<ParenExpr, ImplicitCastExpr, MaterializeTemporaryExpr,
               CXXBindTemporaryExpr, CXXConstructExpr, CXXFunctionalCastExpr>(
              Parent))
        return false;
      Node = Parents[0];
    }
    return false;
  });
}

static bool runsOnConcurrentActorUncached(const Stmt *S, ASTContext &Context);

bool runsOnConcurrentActor(const Stmt *S, ASTContext &Context) {
  // Kept while the outermost question is answered: lambdas held in locals of
  // lambdas each ask about where the ones around them run, which is told once,
  // not once per path to it. One asked about again while it is told, through a
  // call of itself, doesn't run on a concurrent actor by that.
  using CCache = llvm::DenseMap<const Stmt *, bool>;
  static thread_local CCache *s_pCache = nullptr;
  static thread_local llvm::SmallPtrSet<const Stmt *, 8> s_InProgress;
  static thread_local unsigned s_nCycles = 0;
  std::optional<CCache> Owned;
  if (!s_pCache)
    s_pCache = &Owned.emplace();
  const llvm::scope_exit Release([&] {
    if (Owned)
      s_pCache = nullptr;
  });
  if (auto It = s_pCache->find(S); It != s_pCache->end())
    return It->second;
  if (!s_InProgress.insert(S).second) {
    ++s_nCycles;
    return false;
  }
  const unsigned nCycles = s_nCycles;
  const bool Runs = runsOnConcurrentActorUncached(S, Context);
  s_InProgress.erase(S);
  if (s_nCycles == nCycles)
    s_pCache->try_emplace(S, Runs);
  return Runs;
}

static bool runsOnConcurrentActorUncached(const Stmt *S, ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  DynTypedNode Node = DynTypedNode::create(*S);
  // Where the code runs: a lambda run in place, or dispatched to the current
  // actor, runs where it is made.
  const Stmt *Site = S;
  for (int Depth = 0; Depth < 512; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    const DynTypedNode &Parent = Parents[0];
    if (const auto *Lambda = Parent.get<LambdaExpr>()) {
      const bool Inside = Node.get<CompoundStmt>() == Lambda->getBody() ||
                          Node.get<CXXRecordDecl>() || Node.get<Decl>();
      if (Inside) {
        const Expr *Actor = nullptr;
        if (closureDispatch(Lambda, Lambda->getLambdaClass(), Context, &Actor) &&
            Actor)
          return isConcurrentActor(Actor, Context);
        // Given to an entry, which runs it after it has switched.
        bool Callee = false;
        if (const CallExpr *Call = callTakingLambda(Lambda, Context, Callee);
            Call && !Callee && givenToRunsToCompletion(Lambda, Context) &&
            Call->getDirectCallee() &&
            isRunsToCompletionEntry(Call->getDirectCallee()))
          return llvm::any_of(
              llvm::seq<unsigned>(0, Call->getNumArgs()), [&](unsigned iArgument) {
                // Also made into a function object, like a `TCFunction`.
                return Call->getArg(iArgument)->getSourceRange().fullyContains(
                           Lambda->getSourceRange()) &&
                       isUsedAfterSwitch(Call, iArgument, Context);
              });
        // Held in a local, like `auto fWork = [] { ... }`, it runs where it is
        // called, when each use calls it.
        if (const VarDecl *Holder = localHolder(Lambda, Context)) {
          const auto *Home = dyn_cast_or_null<FunctionDecl>(
              Holder->getParentFunctionOrMethod());
          const Stmt *HomeBody = Home ? Home->getBody() : nullptr;
          if (const auto *Coroutine =
                  dyn_cast_or_null<CoroutineBodyStmt>(HomeBody))
            HomeBody = Coroutine->getBody();
          if (HomeBody) {
            using namespace ast_matchers;
            const auto Uses = match(
                findAll(declRefExpr(to(equalsNode(Holder))).bind("use")),
                *HomeBody, Context);
            if (!Uses.empty() && llvm::all_of(Uses, [&](const BoundNodes &Nodes) {
                  const auto *Use = Nodes.getNodeAs<DeclRefExpr>("use");
                  const auto UseParents = instantiatedParents(*Use, Context);
                  const Expr *Called = Use;
                  auto Up = UseParents;
                  while (!Up.empty() && (Up[0].get<ImplicitCastExpr>() ||
                                         Up[0].get<ParenExpr>() ||
                                         Up[0].get<MaterializeTemporaryExpr>() ||
                                         Up[0].get<CXXConstructExpr>())) {
                    Called = Up[0].get<Expr>();
                    Up = instantiatedParents(Up[0], Context);
                  }
                  // Called, or given to `fg_CallSafe`, which calls it there.
                  if (const auto *Safe =
                          Up.empty() ? nullptr : Up[0].get<CallExpr>();
                      Safe && !isa<CXXOperatorCallExpr>(Safe) &&
                      Safe->getNumArgs() >= 1 &&
                      calleeName(Safe) == "fg_CallSafe" &&
                      Safe->getArg(0) == Called)
                    return runsOnConcurrentActor(Safe, Context);
                  if (const CXXMemberCallExpr *Named =
                          Up.empty()
                              ? nullptr
                              : namedCallOperatorCall(Up[0], Called, Context))
                    return runsOnConcurrentActor(Named, Context);
                  const auto *Call =
                      Up.empty() ? nullptr : Up[0].get<CXXOperatorCallExpr>();
                  return Call && Call->getOperator() == OO_Call &&
                         Call->getNumArgs() >= 1 &&
                         Call->getArg(0)->IgnoreParenImpCasts() ==
                             Called->IgnoreParenImpCasts() &&
                         runsOnConcurrentActor(Call, Context);
                }))
              return true;
          }
        }
        Site = Lambda;
      }
    } else if (const auto *Function = Parent.get<FunctionDecl>()) {
      const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      if (Method && Method->getParent()->isLambda()) {
        Node = Parent;
        continue;
      }
      if (isJoinedByCallers(Function))
        return true;
      if (!isRunsToCompletionEntry(Function))
        return false;
      // After the switch, which the entry check makes its first suspension.
      // After awaiting another entry, which switches itself, it resumes on
      // the actor it was called on.
      const Expr *First = firstSuspension(Function);
      const Expr *Operand = First ? awaitedOperand(First) : nullptr;
      const CallExpr *Switch =
          Operand ? asCallTo(skipWrappers(Operand)->IgnoreParenImpCasts(),
                             {"fg_ContinueRunningOnActor"})
                  : nullptr;
      if (Switch && Switch->getNumArgs() >= 1 &&
          isConcurrentActor(Switch->getArg(0), Context) &&
          SM.isBeforeInTranslationUnit(First->getEndLoc(), Site->getBeginLoc()))
        return true;
      // Or a switch of its own after that, like after awaiting another entry:
      // a statement of the body, which runs on every path.
      const auto *Body = dyn_cast_or_null<CompoundStmt>(Function->getBody());
      if (const auto *Coroutine =
              dyn_cast_or_null<CoroutineBodyStmt>(Function->getBody()))
        Body = dyn_cast_or_null<CompoundStmt>(Coroutine->getBody());
      if (!Body || hasJumps(Function))
        return false;
      for (const Stmt *Statement : Body->body()) {
        if (!SM.isBeforeInTranslationUnit(Statement->getEndLoc(),
                                          Site->getBeginLoc()))
          break;
        const auto *Await = dyn_cast<Expr>(Statement);
        Await = Await ? skipWrappers(Await)->IgnoreParenImpCasts() : nullptr;
        const Expr *Awaited =
            Await && isa<CoroutineSuspendExpr>(Await) ? awaitedOperand(Await)
                                                      : nullptr;
        if (const CallExpr *Explicit =
                Awaited ? asCallTo(skipWrappers(Awaited)->IgnoreParenImpCasts(),
                                   {"fg_ContinueRunningOnActor"})
                        : nullptr;
            Explicit && Explicit->getNumArgs() >= 1 &&
            isConcurrentActor(Explicit->getArg(0), Context))
          return true;
      }
      return false;
    }
    Node = Parent;
  }
  return false;
}

bool outlivesEnclosing(const ValueDecl *Variable, const LambdaExpr *Lambda) {
  const auto *Home =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  if (!Home)
    return false;
  // A parameter lives in the frame of the coroutine until it finishes, after
  // it joined the cleanup; its locals are there only where the guard starts
  // the cleanup, until the cleanup first suspends, which the caller checks.
  const auto *Local = dyn_cast<VarDecl>(Variable);
  if (!Local || !Local->hasLocalStorage())
    return false;
  // A function that isn't a coroutine, like a factory of the cleanup, has
  // returned before the cleanup runs.
  if (!isCoroutine(Home))
    return false;
  if (const auto *Parameter = dyn_cast<ParmVarDecl>(Variable);
      Parameter && (Parameter->getType()->isReferenceType() ||
                    Parameter->getType()->isPointerType()) &&
      !isJoinedByCallers(Home))
    return false;
  for (const DeclContext *Context = Lambda->getLambdaClass()->getDeclContext();
       Context; Context = Context->getParent()) {
    if (Context == Home)
      return true;
  }
  return false;
}

/// The compound statement that declares \p Variable, or null when a
/// statement of another kind does, like the init statement of an `if`.
static const CompoundStmt *declaringScope(const VarDecl *Variable,
                                          ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Variable);
  for (int Depth = 0; Depth < 4; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    if (const auto *Compound = Parents[0].get<CompoundStmt>())
      return Compound;
    // One of the init statement or the condition of an `if`, a loop or a
    // `switch`, `if (int Local = 42; bRead)`, lives only as long as that
    // statement.
    if (const Stmt *Statement = Parents[0].get<Stmt>();
        Statement && !isa<DeclStmt>(Statement))
      return nullptr;
    Node = Parents[0];
  }
  return nullptr;
}

/// Whether a callback of `fg_OnResume`, which can abort the coroutine where
/// it resumes, is registered at \p S: by a registration held in a local
/// declared before it in the blocks it is in, or in the init statement or
/// condition of a statement around it.
static bool registersOnResume(const Stmt *S, ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  auto Registers = [&](const Stmt *Statement) {
    const auto *Declaration = dyn_cast_or_null<DeclStmt>(Statement);
    if (!Declaration ||
        !SM.isBeforeInTranslationUnit(Declaration->getEndLoc(),
                                      S->getBeginLoc()))
      return false;
    // Also either value of a conditional, `bChoose ? co_await
    // fg_OnResume(A) : co_await fg_OnResume(B)`.
    return llvm::any_of(Declaration->decls(), [](const Decl *D) {
      const auto *Holder = dyn_cast<VarDecl>(D);
      return Holder && Holder->getInit() &&
             llvm::any_of(
                 holderAwaits(Holder->getInit(), &Holder->getASTContext()),
                 [](const CoroutineSuspendExpr *Await) {
                   return Await && isOnResumeRegistration(Await->getOperand());
                 });
    });
  };
  // Also by a temporary registration earlier in the full expression it is
  // in, `((void)co_await fg_OnResume(...), ..., co_await ...)`, which is
  // held until the end of it.
  const Expr *Full = nullptr;
  for (DynTypedNode Up = DynTypedNode::create(*S);;) {
    const auto Parents = instantiatedParents(Up, Context);
    if (Parents.empty() || Parents[0].get<LambdaExpr>() ||
        !Parents[0].get<Expr>())
      break;
    Up = Parents[0];
    Full = Up.get<Expr>();
  }
  if (Full) {
    // The registrations of each full expression, found once.
    struct CRegistrations {
      llvm::DenseMap<const Expr *, llvm::SmallVector<SourceLocation, 1>> Ends;
    };
    llvm::DenseMap<const Expr *, llvm::SmallVector<SourceLocation, 1>>
        &Registrations = astCache<CRegistrations>(Context).Ends;
    if (Registrations.try_emplace(Full).second) {
      llvm::SmallVector<SourceLocation, 1> Ends;
      std::function<void(const Stmt *)> Find = [&](const Stmt *Inner) {
        if (!Inner || isa<LambdaExpr>(Inner))
          return;
        if (const auto *Await = dyn_cast<CoroutineSuspendExpr>(Inner);
            Await && Await->getOperand() &&
            isOnResumeRegistration(Await->getOperand()))
          Ends.push_back(Await->getEndLoc());
        for (const Stmt *Child : evaluatedChildren(Inner, Context))
          Find(Child);
      };
      Find(Full);
      Registrations[Full] = std::move(Ends);
    }
    if (llvm::any_of(Registrations[Full], [&](SourceLocation End) {
          return SM.isBeforeInTranslationUnit(End, S->getBeginLoc());
        }))
      return true;
  }
  DynTypedNode Node = DynTypedNode::create(*S);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty() || Parents[0].get<LambdaExpr>() ||
        Parents[0].get<FunctionDecl>())
      return false;
    Node = Parents[0];
    if (const auto *Block = Node.get<CompoundStmt>()) {
      if (llvm::any_of(Block->body(), Registers))
        return true;
    } else if (const auto *If = Node.get<IfStmt>()) {
      if (Registers(If->getInit()) ||
          Registers(If->getConditionVariableDeclStmt()))
        return true;
    } else if (const auto *Switch = Node.get<SwitchStmt>()) {
      if (Registers(Switch->getInit()) ||
          Registers(Switch->getConditionVariableDeclStmt()))
        return true;
    } else if (const auto *For = Node.get<ForStmt>()) {
      if (Registers(For->getInit()) ||
          Registers(For->getConditionVariableDeclStmt()))
        return true;
    } else if (const auto *While = Node.get<WhileStmt>()) {
      if (Registers(While->getConditionVariableDeclStmt()))
        return true;
    } else if (const auto *Range = Node.get<CXXForRangeStmt>()) {
      if (Registers(Range->getInit()))
        return true;
    }
  }
  return false;
}

/// Whether \p S, what runs between a call and the early await of its guard,
/// can leave \p Scope before the await: a return, a jump out of it, or an
/// await that can abort the coroutine, like of a future that fails, or any
/// while a callback of `fg_OnResume` is registered. A throw doesn't: in a
/// coroutine that doesn't capture exceptions, where nothing around the call
/// catches it, it ends the program.
static bool canLeave(const Stmt *S, const CompoundStmt *Scope,
                     ASTContext &Context) {
  if (!S)
    return false;
  // The expression of a default argument, which the uses of it share, also
  // those in other default arguments, `int f1(int = g(f0(), f0()));`, is
  // looked at once in a question.
  static thread_local llvm::DenseMap<const Expr *, bool> s_Shared;
  static thread_local int s_Depth = 0;
  if (s_Depth == 0)
    s_Shared.clear();
  ++s_Depth;
  const llvm::scope_exit Leave([] { --s_Depth; });
  // What isn't evaluated, like `noexcept(co_await ...)` or `sizeof`, doesn't
  // run.
  if (isa<UnaryExprOrTypeTraitExpr, CXXNoexceptExpr>(S))
    return false;
  if (const auto *Typeid = dyn_cast<CXXTypeidExpr>(S);
      Typeid && !Typeid->isPotentiallyEvaluated())
    return false;
  // Making a lambda runs only its captures.
  if (const auto *Lambda = dyn_cast<LambdaExpr>(S))
    return llvm::any_of(Lambda->capture_inits(), [&](const Expr *Init) {
      return canLeave(Init, Scope, Context);
    });
  const SourceManager &SM = Context.getSourceManager();
  auto InScope = [&](const Stmt *Target) {
    return Target &&
           !SM.isBeforeInTranslationUnit(Target->getBeginLoc(),
                                         Scope->getBeginLoc()) &&
           !SM.isBeforeInTranslationUnit(Scope->getEndLoc(),
                                         Target->getBeginLoc());
  };
  if (isa<ReturnStmt, CoreturnStmt, GotoStmt, IndirectGotoStmt, CoyieldExpr>(
          S))
    return true;
  if (isa<BreakStmt, ContinueStmt>(S)) {
    // A loop or `switch` in the scope keeps it in the scope.
    DynTypedNode Node = DynTypedNode::create(*S);
    for (int Depth = 0; Depth < 256; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return true;
      Node = Parents[0];
      const Stmt *Target = Node.get<Stmt>();
      if (isa_and_nonnull<ForStmt, WhileStmt, DoStmt, CXXForRangeStmt>(
              Target) ||
          (isa<BreakStmt>(S) && isa_and_nonnull<SwitchStmt>(Target)))
        return !InScope(Target);
      if (!Target)
        return true;
    }
    return true;
  }
  if (const auto *Await = dyn_cast<CoroutineSuspendExpr>(S)) {
    // Registering a callback with `fg_OnResume` runs it, which can abort the
    // coroutine, also through a phase guard, `f_CheckDestroyedOnResume()`.
    if (isOnResumeRegistration(Await->getOperand()))
      return true;
    // A callback of `fg_OnResume` can abort it where it resumes.
    if (isSuspension(Await) && registersOnResume(Await, Context))
      return true;
    const Expr *Operand =
        skipWrappers(awaitedOperand(Await))->IgnoreParenImpCasts();
    // A wrapped future kept in an unchanged local, `auto Wrapped =
    // fg_Move(Future).f_Wrap(); co_await fg_Move(Wrapped);`, is awaited like
    // it.
    const Expr *Kept = Operand;
    if (const auto *Move = dyn_cast<CallExpr>(Kept);
        Move && Move->getNumArgs() == 1 && (isMoveFunction(calleeName(Move))))
      Kept = Move->getArg(0)->IgnoreParenImpCasts();
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Kept))
      if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
          Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
          !Local->getType()->isReferenceType() && Local->getInit() &&
          !isChangedOtherThanBy(Ref))
        if (const auto *Wrap = dyn_cast<CXXMemberCallExpr>(
                skipWrappers(Local->getInit())->IgnoreParenImpCasts());
            Wrap && Wrap->getMethodDecl() &&
            Wrap->getMethodDecl()->getIdentifier() &&
            Wrap->getMethodDecl()->getName() == "f_Wrap")
          Operand = Wrap;
    // A future that fails aborts the coroutine, also a pack of them,
    // `co_await (fg_A() + fg_B())`, which aborts when one fails, but not one
    // wrapped, `.f_Wrap()`, which gives the failure as a value.
    const auto *Wrapped = dyn_cast<CXXMemberCallExpr>(Operand);
    const bool IsWrapped = Wrapped && Wrapped->getMethodDecl() &&
                           Wrapped->getMethodDecl()->getIdentifier() &&
                           Wrapped->getMethodDecl()->getName() == "f_Wrap";
    // So does one with context, `co_await (fg_Work() % "Context")`, an
    // actor call, an async result, and the cleanup of another guard, `co_await
    // fg_Move(Cleanup)`, which give the failure as an exception.
    if (const StringRef Awaited = recordName(Operand->getType());
        !IsWrapped &&
        llvm::is_contained({StringRef("TCFuture"), StringRef("TCUnsafeFuture"),
                            StringRef("TCFutureWithFlags"),
                            StringRef("TCFuturePack"),
                            StringRef("TCFuturePackWithError"),
                            StringRef("TCFutureWithExceptionTransformer"),
                            StringRef("TCBoundActorCall"),
                            StringRef("TCAsyncResult"), StringRef("TCWrapped"),
                            StringRef("TCAwaiter"),
                            StringRef("CAsyncDestroy"),
                            StringRef("TCAsyncDestroy"),
                            StringRef("TCUnwrapAwaiter"),
                            StringRef("CAsyncDestroyHelper")},
                           Awaited))
      return true;
    // Also the awaiter of a future that gives the failure as an exception,
    // `co_await fg_Work().operator co_await()`, of the library, whose second
    // argument tells it unwraps, of a pack, whose first one does, and of a
    // bound call of an actor, whose third one does.
    // Also what `await_transform` of the promise makes of it, like of
    // `co_await g_AsyncDestroy`.
    auto Unwraps = [](const Expr *E) {
      const auto *Awaiter = E ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                                    E->getType()->getAsCXXRecordDecl())
                              : nullptr;
      if (!Awaiter || !Awaiter->getIdentifier())
        return false;
      unsigned iUnwrap;
      if (Awaiter->getName() == "TCFutureAwaiter")
        iUnwrap = 1;
      else if (Awaiter->getName() == "TCFuturePackAwaiter")
        iUnwrap = 0;
      // Of a bound call of an actor, `co_await
      // Actor(...).operator co_await()`, the third.
      else if (Awaiter->getName() == "TCBoundActorCallAwaiter")
        iUnwrap = 2;
      else
        return false;
      const TemplateArgumentList &Arguments = Awaiter->getTemplateArgs();
      return Arguments.size() > iUnwrap &&
             (Arguments[iUnwrap].getKind() != TemplateArgument::Integral ||
              Arguments[iUnwrap].getAsIntegral() != 0);
    };
    if (!IsWrapped && (Unwraps(Operand) || Unwraps(Await->getCommonExpr())))
      return true;
    // Other awaits, like a switch to a concurrent actor, which isn't
    // destroyed, leave only by what they are given.
    return canLeave(Await->getOperand(), Scope, Context);
  }
  // Only the branch of an `if constexpr` that isn't discarded runs, also of
  // a constant condition, `if (false)`, unless a jump to a label or a case in
  // the other can reach it, and of an `if consteval`, the one that runs when
  // not constant evaluated.
  if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConsteval())
    return canLeave(If->isNegatedConsteval() ? If->getThen() : If->getElse(),
                    Scope, Context);
  if (const auto *If = dyn_cast<IfStmt>(S);
      If && !If->getConditionVariable() && If->getCond() &&
      !If->getCond()->isValueDependent())
    if (std::optional<llvm::APSInt> Value =
            If->getCond()->getIntegerConstantExpr(Context);
        Value &&
        (If->isConstexpr() ||
         !hasJumpTarget(Value->isZero() ? If->getThen() : If->getElse())))
      return canLeave(If->getInit(), Scope, Context) ||
             canLeave(Value->isZero() ? If->getElse() : If->getThen(), Scope,
                      Context);
  // Only the value of a conditional that a constant condition chooses,
  // `false ? co_await fg_Work() : 0`, and not the right side of `&&` or `||`
  // that a constant left side skips, `true || (co_await fg_Work(), false)`.
  bool Constant = false;
  if (const auto *Conditional = dyn_cast<ConditionalOperator>(S);
      Conditional && !Conditional->getCond()->isValueDependent() &&
      Conditional->getCond()->EvaluateAsBooleanCondition(Constant, Context))
    return canLeave(Conditional->getCond(), Scope, Context) ||
           canLeave(Constant ? Conditional->getTrueExpr()
                             : Conditional->getFalseExpr(),
                    Scope, Context);
  if (const auto *Logical = dyn_cast<BinaryOperator>(S);
      Logical && Logical->isLogicalOp() &&
      !Logical->getLHS()->isValueDependent() &&
      Logical->getLHS()->EvaluateAsBooleanCondition(Constant, Context) &&
      Constant == (Logical->getOpcode() == BO_LOr))
    return canLeave(Logical->getLHS(), Scope, Context);
  // What a default argument or default member initializer runs.
  const Expr *Default = nullptr;
  if (const auto *Argument = dyn_cast<CXXDefaultArgExpr>(S))
    Default = Argument->getExpr();
  else if (const auto *Initializer = dyn_cast<CXXDefaultInitExpr>(S))
    Default = Initializer->getExpr();
  if (Default) {
    if (auto It = s_Shared.find(Default); It != s_Shared.end())
      return It->second;
    s_Shared[Default] = false;
    const bool Leaves = canLeave(Default, Scope, Context);
    s_Shared[Default] = Leaves;
    return Leaves;
  }
  return llvm::any_of(childrenOnce(S), [&](const Stmt *Child) {
    return canLeave(Child, Scope, Context);
  });
}

/// Whether the coroutine \p Function can capture exceptions, which then don't
/// end the program but leave it, like returns: by its flags,
/// `TCFutureWithFlags<int, ECoroutineFlag_CaptureExceptions>`, or by setting
/// them or a scope that captures them, `co_await
/// ECoroutineFlag_CaptureExceptions` or `co_await g_CaptureExceptions`.
static bool decideCapturesExceptions(const FunctionDecl *Function);

/// Like `decideCapturesExceptions`, each function once: each call joined in
/// it asks.
static bool capturesExceptions(const FunctionDecl *Function) {
  struct CCaptures {
    llvm::DenseMap<const FunctionDecl *, bool> Decided;
  };
  llvm::DenseMap<const FunctionDecl *, bool> &Decided =
      astCache<CCaptures>(Function->getASTContext()).Decided;
  if (const auto It = Decided.find(Function); It != Decided.end())
    return It->second;
  const bool Captures = decideCapturesExceptions(Function);
  Decided[Function] = Captures;
  return Captures;
}

static bool decideCapturesExceptions(const FunctionDecl *Function) {
  // Whether the flags \p Flags, of `ECoroutineFlag`, capture exceptions.
  auto Captures = [](const Expr *Flags, const ASTContext &Context) {
    const EnumDecl *Enum = Flags->getType()->getAsEnumDecl();
    if (!Enum)
      return true;
    std::optional<llvm::APSInt> Value =
        Flags->isValueDependent() ? std::nullopt
                                  : Flags->getIntegerConstantExpr(Context);
    if (!Value)
      return true;
    for (const EnumConstantDecl *Enumerator : Enum->enumerators())
      if (Enumerator->getName().contains("CaptureExceptions") ||
          Enumerator->getName().contains("CaptureMalterlibExceptions"))
        if ((Value->getExtValue() & Enumerator->getInitVal().getExtValue()) !=
            0)
          return true;
    return false;
  };
  const ASTContext &Context = Function->getASTContext();
  // Whether \p Type is \p Name with flags as its second argument that
  // capture exceptions, `TCFutureWithFlags<int, ECoroutineFlag_...>`.
  auto FlagsCapture = [](QualType Type, StringRef Name) {
    const auto *Specialization =
        dyn_cast_or_null<ClassTemplateSpecializationDecl>(
            Type.isNull() ? nullptr : Type->getAsCXXRecordDecl());
    if (!Specialization || !Specialization->getIdentifier() ||
        Specialization->getName() != Name ||
        Specialization->getTemplateArgs().size() < 2 ||
        Specialization->getTemplateArgs()[1].getKind() !=
            TemplateArgument::Integral)
      return false;
    const TemplateArgument &Argument = Specialization->getTemplateArgs()[1];
    const llvm::APSInt Flags = Argument.getAsIntegral();
    const EnumDecl *Enum = Argument.getIntegralType()->getAsEnumDecl();
    return Enum &&
           llvm::any_of(
               Enum->enumerators(), [&](const EnumConstantDecl *Enumerator) {
                 return (Enumerator->getName().contains("CaptureExceptions") ||
                         Enumerator->getName().contains(
                             "CaptureMalterlibExceptions")) &&
                        (Flags.getExtValue() &
                         Enumerator->getInitVal().getExtValue()) != 0;
               });
  };
  if (FlagsCapture(Function->getReturnType(), "TCFutureWithFlags"))
    return true;
  bool Found = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Found)
      return;
    // Not the body of a lambda, but its capture initializers, which run
    // here, `[Flag = (co_await ECoroutineFlag_CaptureExceptions, 0)]`.
    if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
      for (const Expr *Init : Lambda->capture_inits())
        Visit(Init);
      return;
    }
    if (const auto *Await = dyn_cast<CoroutineSuspendExpr>(S);
        Await && Await->getOperand()) {
      const Expr *Operand = Await->getOperand()->IgnoreParenImpCasts();
      const StringRef Name = recordName(Operand->getType());
      // Also what `fg_MoveThis<Flags>(*this)` or `fg_CopyThis` gives,
      // whose flags are of the coroutine.
      // Also the awaiter that awaiting it gives, `co_await
      // Coroutine.await_transform(g_CaptureExceptions)`.
      if (Name.starts_with("CCaptureExceptionsHelper") ||
          Name.starts_with("CCaptureExceptionScopeAwaiter") ||
          FlagsCapture(Operand->getType(), "TCFutureForwardThis") ||
          (Operand->getType()->isEnumeralType() &&
           Operand->getType()->getAsEnumDecl()->getIdentifier() &&
           Operand->getType()->getAsEnumDecl()->getName() == "ECoroutineFlag" &&
           Captures(Operand, Context)))
        Found = true;
    }
    // Not in a branch of an `if constexpr` that is discarded.
    for (const Stmt *Child : evaluatedChildren(S, Function->getASTContext()))
      Visit(Child);
  };
  Visit(Function->getBody());
  return Found;
}

/// Whether a `try` of the function \p S is in, not of a lambda in it, is
/// around \p S, which catches what it throws there, so a throw leaves the
/// scope of what is around it without ending the program.
static bool insideTry(const Stmt *S, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*S);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty() || Parents[0].get<LambdaExpr>() ||
        Parents[0].get<FunctionDecl>())
      return false;
    if (const auto *Try = Parents[0].get<CXXTryStmt>();
        Try && Node.get<Stmt>() == Try->getTryBlock())
      return true;
    Node = Parents[0];
  }
  return false;
}

/// How many of the first statements of \p Block can leave \p Scope, for each
/// count of them, run wholly, like making a lambda runs what initializes its
/// captures: counted once for the block.
static const llvm::SmallVector<unsigned, 0> &
leavingCounts(const CompoundStmt *Block, const CompoundStmt *Scope,
              ASTContext &Context) {
  struct CCounts {
    llvm::DenseMap<std::pair<const CompoundStmt *, const CompoundStmt *>,
                   llvm::SmallVector<unsigned, 0>>
        Blocks;
  };
  auto &Counted = astCache<CCounts>(Context).Blocks;
  auto [It, Inserted] = Counted.try_emplace({Block, Scope});
  if (Inserted) {
    llvm::SmallVector<unsigned, 0> Counts{0};
    for (const Stmt *Child : Block->body()) {
      bool Leaves = false;
      if (const auto *Lambda = dyn_cast<LambdaExpr>(Child))
        Leaves = llvm::any_of(Lambda->capture_inits(), [&](const Expr *Init) {
          return canLeave(Init, Scope, Context);
        });
      else
        Leaves = canLeave(Child, Scope, Context);
      Counts.push_back(Counts.back() + (Leaves ? 1 : 0));
    }
    Counted[{Block, Scope}] = std::move(Counts);
    return Counted[{Block, Scope}];
  }
  return It->second;
}

/// Whether the early await of the guard of \p Join runs on every path from
/// \p Site before \p Scope ends: it is a statement of the scope, or of
/// blocks in it, and nothing between can leave the scope first.
static bool isAwaitedOnEveryPath(const RunsToCompletionJoin &Join,
                                 const Stmt *Site, const CompoundStmt *Scope,
                                 ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  // What doesn't depend on the call, found once for the guard, its early
  // await and the scope, which each call joined there asks: where the await
  // is, and whether what runs after it in its statement can leave.
  struct CFacts {
    bool Valid = false;
    const Stmt *AwaitExpr = nullptr;
    const Stmt *TopStatement = nullptr;
    std::optional<bool> LeavesAfter;
  };
  struct CJoinFacts {
    llvm::DenseMap<std::tuple<const VarDecl *, unsigned, const CompoundStmt *>,
                   CFacts>
        Joins;
  };
  auto &JoinFacts = astCache<CJoinFacts>(Context).Joins;
  const auto FactsKey = std::make_tuple(
      Join.Guard, Join.EarlyAwait.getRawEncoding(), Scope);
  if (const auto It = JoinFacts.find(FactsKey); It == JoinFacts.end()) {
    CFacts Facts;
    // The reference to the guard that is awaited.
    const DeclRefExpr *AwaitRef = nullptr;
    std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
      if (!S || AwaitRef)
        return;
      // Also through a local reference to it, `co_await fg_Move(Alias)`.
      auto IsGuard = [&](const ValueDecl *Decl) {
        for (int Depth = 0; Decl && Depth < 4; ++Depth) {
          if (Decl == Join.Guard)
            return true;
          const auto *Alias = dyn_cast<VarDecl>(Decl);
          const auto *Init =
              Alias && Alias->getType()->isReferenceType() &&
                      Alias->getInit()
                  ? dyn_cast<DeclRefExpr>(
                        Alias->getInit()->IgnoreParenImpCasts())
                  : nullptr;
          Decl = Init ? Init->getDecl() : nullptr;
        }
        return false;
      };
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
          Ref && Ref->getBeginLoc() == Join.EarlyAwait &&
          IsGuard(Ref->getDecl())) {
        AwaitRef = Ref;
        return;
      }
      for (const Stmt *Child : childrenOnce(S))
        Find(Child);
    };
    Find(Scope);
    if (!AwaitRef) {
      JoinFacts[FactsKey] = Facts;
      return false;
    }
    // Only blocks between the await's statement and the scope, not an `if`, a
    // loop, a `switch`, a `try` or a lambda.
    DynTypedNode Node = DynTypedNode::create(*AwaitRef);
    bool InBlock = false;
    bool InScope = false;
    // The await, and the statement of the scope that it is in.
    const Stmt *AwaitExpr = nullptr;
    const Stmt *TopStatement = nullptr;
    for (int Depth = 0; Depth < 256 && !InScope; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        break;
      const DynTypedNode &Parent = Parents[0];
      if (!AwaitExpr && Parent.get<CoroutineSuspendExpr>())
        AwaitExpr = Parent.get<Stmt>();
      if (Parent.get<CompoundStmt>() == Scope)
        TopStatement = Node.get<Stmt>();
      // Nor the right side of `&&` or `||`, or a branch of `?:`, which may not
      // run, like `bJoin && (co_await fg_Move(Guard), true)`.
      if (const auto *Binary = Parent.get<BinaryOperator>();
          Binary && Binary->isLogicalOp() &&
          isSameOperand(Binary->getRHS(), Node.get<Expr>()))
        break;
      if (const auto *Conditional = Parent.get<AbstractConditionalOperator>();
          Conditional &&
          !isSameOperand(Conditional->getCond(), Node.get<Expr>()))
        break;
      // Not in a `try` either, whose handlers skip it.
      if (const auto *Compound = Parent.get<CompoundStmt>()) {
        InBlock = true;
        InScope = Compound == Scope;
      } else if (InBlock ||
                 !(Parent.get<Expr>() || Parent.get<DeclStmt>() ||
                   Parent.get<VarDecl>() || Parent.get<CoreturnStmt>()))
        break;
      Node = Parent;
    }
    Facts.Valid = InScope;
    Facts.AwaitExpr = AwaitExpr;
    Facts.TopStatement = TopStatement;
    JoinFacts[FactsKey] = Facts;
  }
  const CFacts Facts = JoinFacts[FactsKey];
  if (!Facts.Valid)
    return false;
  const Stmt *AwaitExpr = Facts.AwaitExpr;
  const Stmt *TopStatement = Facts.TopStatement;
  // A throw between the call and the await ends the program, unless the
  // coroutine captures exceptions, which then leave it, or a `try` around the
  // call catches it, which leaves the scope of what is around it: then the
  // local must outlive the coroutine.
  // Of the function or lambda the call is in.
  const FunctionDecl *Coroutine = nullptr;
  for (DynTypedNode Up = DynTypedNode::create(*Site); !Coroutine;) {
    const auto Parents = instantiatedParents(Up, Context);
    if (Parents.empty())
      break;
    Up = Parents[0];
    if (const auto *Lambda = Up.get<LambdaExpr>())
      Coroutine = Lambda->getCallOperator();
    else
      Coroutine = Up.get<FunctionDecl>();
  }
  if (!Coroutine || capturesExceptions(Coroutine) || insideTry(Site, Context))
    return false;
  // Nothing between the call and the await leaves the scope.
  const SourceLocation From = Site->getEndLoc();
  bool Leaves = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Leaves)
      return;
    // Making a lambda runs what initializes its captures, not its body.
    if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
      for (const Expr *Init : Lambda->capture_inits())
        Visit(Init);
      return;
    }
    const SourceRange Range = S->getSourceRange();
    // Entirely outside of what runs between.
    if (SM.isBeforeInTranslationUnit(Range.getEnd(), From) ||
        !SM.isBeforeInTranslationUnit(Range.getBegin(), Join.EarlyAwait))
      return;
    // Partly between: look at what is in it, of an await what it awaits.
    if (SM.isBeforeInTranslationUnit(Range.getBegin(), From) ||
        SM.isBeforeInTranslationUnit(Join.EarlyAwait, Range.getEnd())) {
      // A loop around it runs its condition and increment again after it,
      // `while (co_await fg_Next())`.
      if (SM.isBeforeInTranslationUnit(Range.getEnd(), Join.EarlyAwait)) {
        llvm::SmallVector<const Stmt *, 2> Again;
        if (const auto *While = dyn_cast<WhileStmt>(S))
          Again = {While->getCond()};
        else if (const auto *For = dyn_cast<ForStmt>(S))
          Again = {For->getCond(), For->getInc()};
        else if (const auto *Do = dyn_cast<DoStmt>(S))
          Again = {Do->getCond()};
        else if (const auto *Range = dyn_cast<CXXForRangeStmt>(S))
          Again = {Range->getCond(), Range->getInc()};
        for (const Stmt *Repeated : Again)
          if (Repeated && canLeave(Repeated, Scope, Context)) {
            Leaves = true;
            return;
          }
      }
      // What is written before the call in an expression around it can run
      // after it: an argument, unsequenced with it, or the left side of an
      // assignment, `Slots[co_await fg_Index()] = (fg_Read(Value) > Results,
      // 0)`, but not what is sequenced before it, like the left of a comma or
      // the function a call calls.
      if (const auto *Around = dyn_cast<Expr>(S)) {
        const auto *Binary = dyn_cast<BinaryOperator>(Around);
        const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Around);
        const auto *Called = dyn_cast<CallExpr>(Around);
        for (const Stmt *Child : Around->children()) {
          if (!Child ||
              !SM.isBeforeInTranslationUnit(Child->getEndLoc(), From))
            continue;
          // The elements of a braced list, and of an aggregate initialized
          // with parentheses, run in order.
          const auto *Construct = dyn_cast<CXXConstructExpr>(Around);
          const bool SequencedBefore =
              (Binary && (Binary->getOpcode() == BO_Comma ||
                          Binary->isLogicalOp()) &&
               Child == Binary->getLHS()) ||
              (Conditional && Child == Conditional->getCond()) ||
              (Called && Child == Called->getCallee()) ||
              isa<InitListExpr, CXXParenListInitExpr>(Around) ||
              (Construct && Construct->isListInitialization());
          if (!SequencedBefore && canLeave(Child, Scope, Context)) {
            Leaves = true;
            return;
          }
        }
      }
      // An await around the call that ends before the early await can leave
      // too, like when what it awaits fails, `co_await (fg_Read(Local) >
      // Results, fg_Work(0))`.
      if (const auto *Await = dyn_cast<CoroutineSuspendExpr>(S)) {
        Visit(Await->getOperand());
        if (!Leaves &&
            SM.isBeforeInTranslationUnit(Range.getEnd(), Join.EarlyAwait) &&
            canLeave(Await, Scope, Context))
          Leaves = true;
        return;
      }
      // The left side of an assignment whose right side has the await runs
      // after it, `fg_Destination() = (co_await fg_Move(Guard).f_Wrap(),
      // 0)`.
      const Expr *AssignedValue = nullptr;
      if (const auto *Assign = dyn_cast<BinaryOperator>(S);
          Assign && Assign->isAssignmentOp())
        AssignedValue = Assign->getRHS();
      else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S);
               Operator && Operator->isAssignmentOp() &&
               Operator->getNumArgs() == 2)
        AssignedValue = Operator->getArg(1);
      if (AssignedValue &&
          !SM.isBeforeInTranslationUnit(Join.EarlyAwait,
                                        AssignedValue->getBeginLoc()) &&
          !SM.isBeforeInTranslationUnit(AssignedValue->getEndLoc(),
                                        Join.EarlyAwait) &&
          !SM.isBeforeInTranslationUnit(AssignedValue->getBeginLoc(), From)) {
        Visit(AssignedValue);
        return;
      }
      // Of a block, the statements wholly between are found by where they
      // end, which is in order, and whether any can leave is counted once
      // for the block, which each call joined in it asks.
      if (const auto *Block = dyn_cast<CompoundStmt>(S)) {
        const llvm::ArrayRef<Stmt *> Body(Block->body_begin(), Block->size());
        auto FirstEndingFrom = [&](SourceLocation Loc) {
          return static_cast<size_t>(
              llvm::partition_point(Body,
                                    [&](const Stmt *Child) {
                                      return SM.isBeforeInTranslationUnit(
                                          Child->getEndLoc(), Loc);
                                    }) -
              Body.begin());
        };
        const size_t First = FirstEndingFrom(From);
        const size_t Last = FirstEndingFrom(Join.EarlyAwait);
        if (First < Body.size())
          Visit(Body[First]);
        if (Leaves)
          return;
        if (First + 1 < Last) {
          const llvm::SmallVector<unsigned, 0> &Counts =
              leavingCounts(Block, Scope, Context);
          if (Counts[Last] != Counts[First + 1]) {
            Leaves = true;
            return;
          }
        }
        if (Last > First && Last < Body.size())
          Visit(Body[Last]);
        return;
      }
      // Not a branch of an `if constexpr` that is discarded.
      for (const Stmt *Child : evaluatedChildren(S, Context))
        Visit(Child);
      return;
    }
    if (canLeave(S, Scope, Context))
      Leaves = true;
  };
  Visit(Scope);
  // What else the statement of the await runs can run before it, like the
  // other arguments of a call, which are unsequenced with it.
  auto Contains = [&](const Stmt *Outer) {
    return AwaitExpr && Outer &&
           !SM.isBeforeInTranslationUnit(AwaitExpr->getBeginLoc(),
                                         Outer->getBeginLoc()) &&
           !SM.isBeforeInTranslationUnit(Outer->getEndLoc(),
                                         AwaitExpr->getEndLoc());
  };
  std::function<void(const Stmt *)> VisitAfter = [&](const Stmt *S) {
    if (!S || Leaves || S == AwaitExpr)
      return;
    if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
      for (const Expr *Init : Lambda->capture_inits())
        VisitAfter(Init);
      return;
    }
    // What is sequenced after the await, the right side of a built-in `,`,
    // `&&` or `||`, or the branches of `?:` after its condition, runs after
    // it.
    if (const auto *Binary = dyn_cast<BinaryOperator>(S);
        Binary && (Binary->getOpcode() == BO_Comma || Binary->isLogicalOp()) &&
        Contains(Binary->getLHS())) {
      VisitAfter(Binary->getLHS());
      return;
    }
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(S);
        Conditional && Contains(Conditional->getCond())) {
      VisitAfter(Conditional->getCond());
      return;
    }
    // A braced initializer runs its elements in order: those after the one
    // with the await run after it, `Pair{(co_await ..., 0), co_await
    // fg_Work()}`.
    llvm::ArrayRef<const Expr *> Ordered;
    if (const auto *List = dyn_cast<InitListExpr>(S))
      Ordered = llvm::ArrayRef<const Expr *>(List->getInits(), List->getNumInits());
    else if (const auto *Construct = dyn_cast<CXXConstructExpr>(S);
             Construct && Construct->isListInitialization())
      Ordered = llvm::ArrayRef<const Expr *>(Construct->getArgs(),
                                             Construct->getNumArgs());
    else if (const auto *Parens = dyn_cast<CXXParenListInitExpr>(S))
      Ordered = llvm::ArrayRef<const Expr *>(Parens->getInitExprs().data(),
                                             Parens->getInitExprs().size());
    if (!Ordered.empty() && llvm::any_of(Ordered, Contains)) {
      for (const Expr *Element : Ordered) {
        VisitAfter(Element);
        if (Contains(Element))
          break;
      }
      return;
    }
    if (AwaitExpr &&
        SM.isBeforeInTranslationUnit(AwaitExpr->getEndLoc(), S->getBeginLoc())) {
      if (canLeave(S, Scope, Context))
        Leaves = true;
      return;
    }
    for (const Stmt *Child : evaluatedChildren(S, Context))
      VisitAfter(Child);
  };
  if (Leaves)
    return false;
  if (!Facts.LeavesAfter) {
    VisitAfter(TopStatement);
    JoinFacts[FactsKey].LeavesAfter = Leaves;
    return !Leaves;
  }
  return !*Facts.LeavesAfter;
}

const MaterializeTemporaryExpr *extendedTemporary(const VarDecl *Reference) {
  const Expr *Init = Reference->getInit();
  if (!Init)
    return nullptr;
  if (const auto *Full = dyn_cast<FullExpr>(Init))
    Init = Full->getSubExpr();
  // Also bound to a base of it, `CBase const &Value = CDerived{}`; not
  // `IgnoreParenImpCasts`, which skips the temporary too.
  for (;;) {
    if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Init))
      Init = Cast->getSubExpr();
    else if (const auto *Paren = dyn_cast<ParenExpr>(Init))
      Init = Paren->getSubExpr();
    else
      break;
  }
  const auto *Temporary = dyn_cast<MaterializeTemporaryExpr>(Init);
  return Temporary && Temporary->getExtendingDecl() == Reference ? Temporary
                                                                 : nullptr;
}

bool outlivesJoin(const ValueDecl *Variable, const RunsToCompletionJoin &Join,
                  const Stmt *Site, const LambdaExpr *Lambda,
                  ASTContext &Context) {
  // A structured binding, like `x` of `auto &[x, y] = Pair`, is of the
  // object it decomposes.
  if (const auto *Binding = dyn_cast<BindingDecl>(Variable)) {
    Variable = Binding->getDecomposedDecl();
    if (!Variable)
      return false;
  }
  // A reference init capture, like `[&Alias = Value]`, refers to what it is
  // initialized with, and so does a reference that is decomposed.
  for (int Depth = 0; Depth < 4; ++Depth) {
    // A local reference, like `int &Alias = Value`, too.
    const auto *Capture = dyn_cast<VarDecl>(Variable);
    if (!Capture ||
        !(Capture->isInitCapture() || isa<DecompositionDecl>(Capture) ||
          (Capture->hasLocalStorage() && !isa<ParmVarDecl>(Capture))) ||
        !Capture->getType()->isReferenceType() || !Capture->getInit())
      break;
    // One that extends the temporary it is bound to, `CValue const &Value =
    // CValue{1}`, which lives as long as the reference.
    if (extendedTemporary(Capture))
      break;
    // Also of a member of a local, `[&Alias = Data.m_Member]`, which lives as
    // long as the local.
    const Expr *Init = skipWrappers(Capture->getInit())->IgnoreParenImpCasts();
    while (const auto *Member = dyn_cast<MemberExpr>(Init)) {
      if (Member->isArrow()) {
        // Also of what an unchanged pointer parameter of a function that runs
        // to completion points to, `[&Value = _pState->m_Value]`, which its
        // callers keep until they join it.
        if (const auto *Pointer =
                dyn_cast<DeclRefExpr>(Member->getBase()->IgnoreParenImpCasts()))
          if (const auto *Parameter = dyn_cast<ParmVarDecl>(Pointer->getDecl());
              Parameter && Parameter->getType()->isPointerType() &&
              !isChangedVariable(Parameter)) {
            const auto *Function = dyn_cast_or_null<FunctionDecl>(
                Parameter->getParentFunctionOrMethod());
            return Function && isJoinedByCallers(Function);
          }
        // A member of the object of a function that runs to completion,
        // like `[&Value = m_Value]`, whose callers keep the object until they
        // join it.
        if (!isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()))
          return false;
        const DeclContext *Home = Capture->getParentFunctionOrMethod();
        while (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Home)) {
          if (!Method->getParent()->isLambda())
            break;
          Home = Method->getParent()->getParent();
        }
        const auto *Function = dyn_cast_or_null<FunctionDecl>(Home);
        return Function && isa<CXXMethodDecl>(Function) &&
               isJoinedByCallers(Function);
      }
      Init = Member->getBase()->IgnoreParenImpCasts();
      // An element of a local array lives as long as the array.
      while (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Init)) {
        if (!Subscript->getBase()->IgnoreParenImpCasts()->getType()->isArrayType())
          return false;
        Init = Subscript->getBase()->IgnoreParenImpCasts();
      }
    }
    while (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Init)) {
      if (!Subscript->getBase()->IgnoreParenImpCasts()->getType()->isArrayType())
        return false;
      Init = Subscript->getBase()->IgnoreParenImpCasts();
    }
    // Either of a conditional, `[&Value = bChoose ? _A : _B]`, which both
    // must outlive the join.
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Init)) {
      // Each arm decided once for the join, also in a chain of conditionals
      // of the same references, `int &r2 = b ? r1 : r1`, which would take
      // time that grows exponentially with its length.
      // Keyed by what is decided too, which a nested decision, like of the
      // callers of a function, can change.
      using CArm = std::tuple<const ValueDecl *, const RunsToCompletionJoin *,
                              const Stmt *, const LambdaExpr *>;
      static thread_local llvm::DenseMap<CArm, bool> *s_pArms = nullptr;
      llvm::DenseMap<CArm, bool> Arms;
      const bool Outermost = !s_pArms;
      if (Outermost)
        s_pArms = &Arms;
      const llvm::scope_exit Restore([&] {
        if (Outermost)
          s_pArms = nullptr;
      });
      auto Outlives = [&](const Expr *Arm) {
        const auto *ArmRef =
            dyn_cast<DeclRefExpr>(skipWrappers(Arm)->IgnoreParenImpCasts());
        if (!ArmRef)
          return false;
        const CArm Key{ArmRef->getDecl(), &Join, Site, Lambda};
        // One being decided, that refers to itself, `int &r = b ? r :
        // Value`, is taken not to while it is.
        auto [It, Inserted] = s_pArms->try_emplace(Key, false);
        if (!Inserted)
          return It->second;
        const bool Outlives =
            outlivesJoin(ArmRef->getDecl(), Join, Site, Lambda, Context);
        (*s_pArms)[Key] = Outlives;
        return Outlives;
      };
      // Only the one a constant condition chooses.
      bool Chosen = false;
      if (const auto *Choice = dyn_cast<ConditionalOperator>(Conditional);
          Choice && !Choice->getCond()->isValueDependent() &&
          Choice->getCond()->EvaluateAsBooleanCondition(Chosen, Context))
        return Outlives(Chosen ? Choice->getTrueExpr()
                               : Choice->getFalseExpr());
      return Outlives(Conditional->getTrueExpr()) &&
             Outlives(Conditional->getFalseExpr());
    }
    const auto *Ref = dyn_cast<DeclRefExpr>(Init);
    if (!Ref)
      return false;
    Variable = Ref->getDecl();
  }
  if (const auto *Parameter = dyn_cast<ParmVarDecl>(Variable)) {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(Parameter->getParentFunctionOrMethod());
    if (!Function)
      return false;
    if (Parameter->getType()->isReferenceType() ||
        Parameter->getType()->isPointerType())
      return isJoinedByCallers(Function);
    if (!Join.Guard)
      return !Lambda || isLocalWhileRunning(Variable, Lambda, Context);
    // Like a local declared before everything, also of a function around the
    // lambda that has the guard.
    if (Function == Join.Guard->getParentFunctionOrMethod())
      return true;
  }
  const auto *Local = dyn_cast<VarDecl>(Variable);
  // A static, like `static int const Value`, lives as long as the program.
  if (Local && Local->getStorageDuration() == SD_Static)
    return true;
  if (!Local || !Local->hasLocalStorage())
    return false;
  if (!Join.Guard)
    return !Lambda || isLocalWhileRunning(Variable, Lambda, Context);
  if (Local->getParentFunctionOrMethod() !=
      Join.Guard->getParentFunctionOrMethod()) {
    // A local of a function around the lambda that has the guard lives until
    // the lambda ends when each lambda in between is joined in its
    // full-expression, where the local is.
    for (const DeclContext *Inner = Join.Guard->getParentFunctionOrMethod();
         Inner; Inner = Inner->getParent()) {
      if (Inner == Local->getParentFunctionOrMethod())
        return true;
      const auto *Method = dyn_cast<CXXMethodDecl>(Inner);
      if (!Method || !Method->getParent()->isLambda())
        return false;
      const LambdaExpr *Between = lambdaOfCallOperator(Method, Context);
      bool Abortable = false;
      const std::optional<RunsToCompletionJoin> BetweenJoin =
          Between ? lambdaJoin(Between, Context, &Abortable) : std::nullopt;
      if (!BetweenJoin || BetweenJoin->Guard || Abortable)
        return false;
      Inner = Method->getParent();
    }
    return false;
  }
  // The guard starts its cleanup where its scope ends, but the coroutine
  // joins it only when it finishes, after its locals are gone: a local
  // outlives the join only where the guard is awaited early in its scope, like
  // `co_await fg_Move(Guard)` before the scope ends.
  const SourceManager &SM = Context.getSourceManager();
  if (Join.EarlyAwait.isInvalid())
    return false;
  const CompoundStmt *Scope = declaringScope(Local, Context);
  return Scope &&
         SM.isBeforeInTranslationUnit(Local->getLocation(), Join.EarlyAwait) &&
         !SM.isBeforeInTranslationUnit(Join.EarlyAwait, Scope->getBeginLoc()) &&
         !SM.isBeforeInTranslationUnit(Scope->getEndLoc(), Join.EarlyAwait) &&
         isAwaitedOnEveryPath(Join, Site, Scope, Context);
}

} // namespace clang::tidy::malterlib
