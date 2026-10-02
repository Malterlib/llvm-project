//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AsyncDestroyCheck.h"
#include "CheckedAccess.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"
#include "OncePerTypeVisitor.h"
#include "RunsToCompletion.h"
#include "StorageModel.h"
#include "Summaries.h"
#include "ValueFlow.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/Builtins.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

/// What \p E takes the address of: `Value` of `&Value`, also of
/// `std::addressof(Value)` or `__builtin_addressof(Value)`. Null otherwise.
static const Expr *addressedBy(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary && Unary->getOpcode() == UO_AddrOf)
    return Unary->getSubExpr();
  if (const auto *Call = dyn_cast<CallExpr>(E);
      Call && Call->getNumArgs() == 1 &&
      (Call->getBuiltinCallee() == Builtin::BIaddressof ||
       Call->getBuiltinCallee() == Builtin::BI__addressof ||
       Call->getBuiltinCallee() == Builtin::BI__builtin_addressof))
    return Call->getArg(0);
  return nullptr;
}

namespace {
/// Whether \p Node calls one of the functions that register a cleanup through
/// an unchanged local pointer to it, `auto fDestroy = &fg_AsyncDestroy<
/// CResource &>; fDestroy(Resource)`, also through copies of that.
AST_MATCHER(CallExpr, callsAsyncDestroyIndirectly) {
  if (Node.getDirectCallee())
    return false;
  const FunctionDecl *Called = functionCalledBy(&Node);
  return Called && Called->getIdentifier() &&
         llvm::is_contained(llvm::ArrayRef<StringRef>(AsyncDestroyFunctions),
                            identifierTextOf(Called));
}
} // namespace

void AsyncDestroyCheck::registerMatchers(MatchFinder *Finder) {
  const auto Names =
      hasAnyName(llvm::ArrayRef<StringRef>(AsyncDestroyFunctions));
  Finder->addMatcher(
      callExpr(callee(functionDecl(Names)), unless(isInUnevaluatedContext()))
          .bind("destroy"),
      this);
  // Also through a local pointer to one, `auto fDestroy =
  // &fg_AsyncDestroy<CResource &>; fDestroy(Resource)`, also dereferenced,
  // `(*fDestroy)(Resource)`, and through copies of it.
  Finder->addMatcher(
      callExpr(callsAsyncDestroyIndirectly(), unless(isInUnevaluatedContext()))
          .bind("destroy"),
      this);
  // Each kind of node by itself, which the finder tries only on nodes of that
  // kind, not on every expression.
  Finder->addMatcher(cxxOperatorCallExpr(hasOverloadedOperatorName("/"),
                                         unless(isInUnevaluatedContext()))
                         .bind("divide"),
                     this);
  Finder->addMatcher(
      binaryOperator(hasOperatorName("/"), unless(isInUnevaluatedContext()))
          .bind("divide"),
      this);
  // Also called by name, `g_OnScopeExit.operator/(...)`.
  Finder->addMatcher(
      callExpr(isMemberCallExpr(), callee(cxxMethodDecl(hasOverloadedOperatorName("/"))),
                        unless(isInUnevaluatedContext()))
          .bind("divide"),
      this);
  // And the functions that make the same guards, `fg_OnScopeExit([&] { ...
  // })`, also the shared one, `fg_OnScopeExitShared(...)`.
  Finder->addMatcher(
      callExpr(callee(functionDecl(
                   hasAnyName("fg_OnScopeExit", "fg_OnScopeExitShared"))),
               argumentCountIs(1), unless(isInUnevaluatedContext()))
          .bind("scopeExit"),
      this);
}

namespace {
using VariableSet = llvm::SmallPtrSet<const VarDecl *, 4>;
AST_MATCHER_P(VarDecl, isOneOf, VariableSet, Variables) {
  return Variables.contains(&Node);
}
AST_MATCHER_P(Expr, satisfies, std::function<bool(const Expr *)>, Predicate) {
  return Predicate(&Node);
}

/// The expressions that \p E can be, also either of a conditional,
/// `bChoose ? Guard : Other`, the last of a comma, and what a cast to a
/// reference refers to, `static_cast<CAsyncDestroy &>(Guard)`. Each once,
/// which keeps it linear in the size of \p E.
static llvm::SmallVector<const Expr *, 2> alternativesOf(const Expr *E) {
  llvm::SmallVector<const Expr *, 2> Alternatives;
  llvm::SmallVector<const Expr *, 4> Pending{E};
  llvm::SmallPtrSet<const Expr *, 4> Seen;
  while (!Pending.empty()) {
    const Expr *Current = Pending.pop_back_val()->IgnoreParenImpCasts();
    if (!Seen.insert(Current).second)
      continue;
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Current);
        Cast && Cast->isGLValue()) {
      Pending.push_back(Cast->getSubExpr());
    } else if (const auto *Conditional =
                   dyn_cast<AbstractConditionalOperator>(Current)) {
      for (const Expr *Arm : keptArms(Conditional))
        Pending.push_back(Arm);
    } else if (const auto *Comma = dyn_cast<BinaryOperator>(Current);
               Comma && Comma->getOpcode() == BO_Comma) {
      Pending.push_back(Comma->getRHS());
    } else {
      Alternatives.push_back(Current);
    }
  }
  return Alternatives;
}
} // namespace

void AsyncDestroyCheck::check(const MatchFinder::MatchResult &Result) {
  // Nothing in a translation unit with an error that stops it from compiling
  // (hasUncompilableErrors): no flow is built there, and what the errors leave
  // can't be relied on.
  if (hasUncompilableErrors(*Result.Context))
    return;
  if (Repeated.repeated(Result))
    return;
  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  for (const auto &[Id, Node] : Result.Nodes.getMap())
    if (isInTemplatePattern(Node, *Result.Context))
      return;
  // Not in a branch of an `if constexpr` that is discarded, which doesn't run,
  // nor in one that a constant condition never takes, `if (false)`.
  auto NeverRuns = [&](const Expr *E) {
    return isInDiscardedBranch(DynTypedNode::create(*E), *Result.Context) ||
           isInNeverTakenBranch(DynTypedNode::create(*E), *Result.Context);
  };
  if (const auto *Call = Result.Nodes.getNodeAs<CallExpr>("destroy")) {
    if (!NeverRuns(Call))
      checkAsyncDestroy(Call, *Result.Context);
    return;
  }
  if (const auto *Made = Result.Nodes.getNodeAs<CallExpr>("scopeExit")) {
    if (!NeverRuns(Made))
      checkScopeExit(Made, nullptr, Made->getArg(0), *Result.Context);
    return;
  }
  const auto *Divide = Result.Nodes.getNodeAs<Expr>("divide");
  if (NeverRuns(Divide))
    return;
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Divide)) {
    if (Operator->getNumArgs() == 2)
      checkScopeExit(Divide, Operator->getArg(0), Operator->getArg(1),
                     *Result.Context);
  } else if (const auto *Binary = dyn_cast<BinaryOperator>(Divide)) {
    checkScopeExit(Divide, Binary->getLHS(), Binary->getRHS(),
                   *Result.Context);
  } else if (const CMemberCall Named = memberCallOf(Divide);
             Named && Named->getNumArgs() == 1) {
    checkScopeExit(Divide, Named->getImplicitObjectArgument(),
                   Named->getArg(0), *Result.Context);
  }
}

/// The first parent of \p Node that is not an implicit or wrapping
/// expression around it.
static DynTypedNode outerParent(DynTypedNode Node, ASTContext &Context) {
  for (;;) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return {};
    const DynTypedNode &Parent = Parents[0];
    const auto *E = Parent.get<Expr>();
    if (E && (isa<ExprWithCleanups, MaterializeTemporaryExpr,
                  CXXBindTemporaryExpr, ImplicitCastExpr, ParenExpr,
                  OpaqueValueExpr>(E) ||
              (isa<CXXConstructExpr>(E) &&
               !isa<CXXTemporaryObjectExpr>(E) &&
               cast<CXXConstructExpr>(E)->getNumArgs() == 1))) {
      Node = Parent;
      continue;
    }
    // Also the last of a comma, `(fg_Prepare(), fg_AsyncDestroy(...))`, and
    // either value of a conditional, `bChoose ? fg_AsyncDestroy(A) :
    // fg_AsyncDestroy(B)`, which is where the value goes.
    const auto *Child = Node.get<Expr>();
    if (const auto *Comma = dyn_cast_or_null<BinaryOperator>(E);
        Comma && Comma->getOpcode() == BO_Comma && Child &&
        Comma->getRHS()->IgnoreParens() == Child->IgnoreParens()) {
      Node = Parent;
      continue;
    }
    if (const auto *Conditional = dyn_cast_or_null<ConditionalOperator>(E);
        Conditional && Child && Conditional->getCond() != Child) {
      Node = Parent;
      continue;
    }
    return Parent;
  }
}

/// The function or lambda \p Node is in.
static const FunctionDecl *enclosingFunction(DynTypedNode Node,
                                             ASTContext &Context) {
  for (;;) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    Node = Parents[0];
    if (const auto *Lambda = Node.get<LambdaExpr>())
      return Lambda->getCallOperator();
    if (const auto *Function = Node.get<FunctionDecl>())
      return Function;
  }
}

/// Whether \p Move, a move of the result of `fg_AsyncDestroy`, is what a
/// `co_await` awaits, directly or through `f_Wrap()`.
static bool isAwaitedCleanup(const Expr *Move, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Move);
  for (;;) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    const DynTypedNode &Parent = Parents[0];
    // Also parenthesized, `co_await (fg_Move(Cleanup))`.
    auto Same = [&](const Expr *Operand) {
      const auto *Child = Node.get<Expr>();
      return Operand && Child &&
             Child->IgnoreParens() == Operand->IgnoreParens();
    };
    if (const auto *Await = Parent.get<CoroutineSuspendExpr>())
      return Same(Await->getOperand()) || Same(Await->getCommonExpr());
    const auto *E = Parent.get<Expr>();
    if (!E)
      return false;
    // Only wrappers of the moved result, and calls of `f_Wrap()` on it.
    if (const auto *Member = dyn_cast<MemberExpr>(E)) {
      if (!Member->getMemberDecl()->getIdentifier() ||
          identifierTextOf(Member->getMemberDecl()) != "f_Wrap")
        return false;
    } else if (const CMemberCall Call = memberCallOf(E);
               Call && Call->isExplicitObject()) {
      // With an explicit object, the call is above the object itself.
      if (!Call->getMethodDecl()->getIdentifier() ||
          identifierTextOf(Call->getMethodDecl()) != "f_Wrap" ||
          !Same(Call->getImplicitObjectArgument()))
        return false;
    } else if (!isa<ImplicitCastExpr, ParenExpr, MaterializeTemporaryExpr,
                    CXXBindTemporaryExpr, ExprWithCleanups,
                    CXXMemberCallExpr, OpaqueValueExpr>(E) &&
               !(isa<CXXConstructExpr>(E) &&
                 cast<CXXConstructExpr>(E)->getNumArgs() == 1)) {
      return false;
    }
    Node = Parent;
  }
}

const AsyncDestroyCheck::CFunctionIndex &
AsyncDestroyCheck::indexOf(const FunctionDecl *Function) {
  auto [It, Inserted] = Indices.try_emplace(Function);
  if (!Inserted)
    return It->second;
  CFunctionIndex &Index = It->second;
  // Each statement once (forEachStmtOnce), also what a co_return shares
  // between its operand and its promise call.
  forEachStmtOnce(Function->getBody(), [&](const Stmt *S) {
    if (const auto *Declaration = dyn_cast<DeclStmt>(S))
      for (const Decl *D : Declaration->decls())
        if (const auto *Local = dyn_cast<VarDecl>(D)) {
          if (Local->getType()->isReferenceType() && Local->getInit())
            Index.References.push_back(Local);
          // Also one pointed later, `CCleanup *pCleanup; pCleanup = ...`.
          else if (Local->getType()->isPointerType())
            Index.Pointers.push_back(Local);
        }
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
      Index.Named.insert(Ref->getDecl());
    return EWalk::Descend;
  });
  return Index;
}

/// The function type \p Call calls: of the function it names, else of what
/// it calls through, a pointer, a reference or a pointer to a member
/// function; null where it can't be told.
static QualType calledTypeOf(const CallExpr *Call) {
  if (const auto *Callee =
          dyn_cast_or_null<FunctionDecl>(Call->getCalleeDecl()))
    return Callee->getType();
  const Expr *Callee = Call->getCallee();
  if (!Callee)
    return QualType();
  // Of a pointer to a member function, `(_Object.*_pRun)()`, the type of the
  // member.
  if (Callee->getType()->isSpecificPlaceholderType(BuiltinType::BoundMember))
    return Expr::findBoundMemberType(Callee);
  if (const auto *Pointer = Callee->getType()->getAs<PointerType>())
    return Pointer->getPointeeType();
  return Callee->getType().getNonReferenceType();
}

/// Whether what has the function type \p Called lets it throw, by its
/// exception specification: no specification that says it can't, also one
/// that isn't worked out yet; what can't be told can.
static bool mayThrow(QualType Called) {
  const auto *Prototype =
      Called.isNull() ? nullptr : Called->getAs<FunctionProtoType>();
  return !Prototype ||
         isUnresolvedExceptionSpec(Prototype->getExceptionSpecType()) ||
         !Prototype->isNothrow();
}

/// Whether \p S, a statement of a coroutine's flow, can end its frame
/// there, which destroys its locals in the reverse order of their
/// declarations and with them the guard of `fg_AsyncDestroy`, which runs its
/// cleanup: a suspension, where the coroutine can be destroyed, not an await
/// that doesn't suspend (isSuspension), `co_await g_CaptureExceptions`, whose
/// calls are told each as calls; and what can throw (mayThrow), a `throw`, a
/// call, a construction or an allocation that can.
static bool mayEndFrame(const Stmt *S) {
  if (isa<CoroutineSuspendExpr>(S))
    return isSuspension(S);
  if (isa<CXXThrowExpr, CXXNewExpr>(S))
    return true;
  if (const auto *Call = dyn_cast<CallExpr>(S))
    return !Call->getBuiltinCallee() && mayThrow(calledTypeOf(Call));
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(S))
    return !Construct->getConstructor()->isTrivial() &&
           mayThrow(Construct->getConstructor()->getType());
  return false;
}

/// Where the guard of `fg_AsyncDestroy` is run early or cancelled by the
/// name \p Named of it or of a reference to it, as it is evaluated: moved,
/// `co_await fg_Move(Cleanup)`, cast to an rvalue reference, or the object
/// of a member function called, `Cleanup.f_Clear()`: that move, cast or
/// call; null where it isn't.
static const Stmt *finishingAt(const DeclRefExpr *Named, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Named);
  for (int Depth = 0; Depth < 16; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    const DynTypedNode &Parent = Parents[0];
    if (Parent.get<ImplicitCastExpr>() || Parent.get<ParenExpr>()) {
      Node = Parent;
      continue;
    }
    if (const auto *Move = Parent.get<CallExpr>();
        Move && Move->getNumArgs() == 1 && isMoveFunction(calleeName(Move)))
      return Move;
    if (const auto *Cast = Parent.get<ExplicitCastExpr>();
        Cast && Cast->getTypeAsWritten()->isRValueReferenceType())
      return Cast;
    if (const auto *Member = Parent.get<MemberExpr>()) {
      const auto Outer = instantiatedParents(Parent, Context);
      const auto *Called =
          Outer.empty() ? nullptr : Outer[0].get<CXXMemberCallExpr>();
      return Called ? static_cast<const Stmt *>(Called) : Member;
    }
    return nullptr;
  }
  return nullptr;
}

/// Reports where a pointer that the cleanup of \p Call, named \p Name,
/// captures by reference, and whose pointee the cleanup uses when it runs,
/// is pointed at a local of \p Function declared after the guard \p Guard,
/// which is destroyed before the guard runs the cleanup. The pointer is each
/// slot a by-reference capture designates (designatedSlots), past local
/// references; what it holds where the cleanup can run is asked of the slot
/// analysis (localSlotDefinitionsReaching), which tells every definition of
/// it, an assignment, also through what designates it, a swap, an exchange, a
/// setter or a local closure that assigns it where it runs: each definition
/// that points it at such a local, made where the guard isn't finished on
/// each way, that reaches where the cleanup can run, the end of the guard or
/// where the frame can end before it (mayEndFrame), past no definition that
/// points it elsewhere and no place that finishes the guard: a name of it or
/// of one of \p Aliases moved, cast to an rvalue reference or called a member
/// function of (finishingAt), also in a local closure called, each way
/// through whose body does that (passesOnEachWay). Unless what it is pointed
/// at is alive where the cleanup keeps it (Rule K, reportLifetime).
static void checkRetargets(MalterlibCheck &Check, const CallExpr *Call,
                           const FunctionDecl *Function, const VarDecl *Guard,
                           llvm::ArrayRef<const VarDecl *> Aliases,
                           StringRef Name, ASTContext &Context) {
  const LambdaExpr *Cleanup =
      Call->getNumArgs() >= 1 ? cleanupLambdaOf(Call->getArg(0)) : nullptr;
  const Stmt *Body = Function->getBody();
  if (!Cleanup || !Body)
    return;
  CFlowCache &Cache = sharedFlowCache();
  const llvm::SmallVector<const CXXMethodDecl *, 2> Operators =
      runningOperators(Cleanup);
  // Whether a body of the cleanup that runs uses what \p Captured points to
  // (storageUsesOf), not only the pointer, `pValue = nullptr`, where a way
  // from its start passes no name of it that only gives it another value,
  // `pValue = &g_Value; co_return *pValue;`.
  auto PointeeUsed = [&](const VarDecl *Captured) {
    return llvm::any_of(Operators, [&](const CXXMethodDecl *Operator) {
      const Stmt *Code = Operator->getBody();
      if (!Code)
        return false;
      const llvm::ArrayRef<const DeclRefExpr *> Names =
          Cache.namesIn(Operator, Code, Captured, Context);
      llvm::DenseSet<const Stmt *> Replaced;
      for (const DeclRefExpr *Named : Names) {
        const CStorageUses Uses = storageUsesOf(Named, 0, Context);
        if (Uses.bWritten && !Uses.bRead)
          Replaced.insert(Named);
      }
      return llvm::any_of(Names, [&](const DeclRefExpr *Named) {
        return !storageUsesOf(Named, 1, Context).none() &&
               (Replaced.empty() ||
                !passesOnEachWay(
                    Operator, Code,
                    [&](const CFlowEvent &Event) {
                      return Event.Kind == EEvent::Statement &&
                             Replaced.contains(Event.Statement);
                    },
                    Context, Named));
      });
    });
  };
  llvm::SmallVector<const VarDecl *, 4> Pointers;
  for (const auto [Capture, Init] :
       llvm::zip(Cleanup->captures(), captureInitsOf(Cleanup))) {
    const auto *Captured = capturedVariableOf(Capture);
    if (!Captured || !Init || Capture.getCaptureKind() != LCK_ByRef ||
        !Captured->getType().getNonReferenceType()->isPointerType() ||
        !PointeeUsed(Captured))
      continue;
    for (const CSlot &Slot : designatedSlots(Init, Context))
      if (Slot.Path.empty() && !Slot.Element &&
          Slot.Variable->getType()->isPointerType() &&
          !llvm::is_contained(Pointers, Slot.Variable))
        Pointers.push_back(Slot.Variable);
  }
  if (Pointers.empty())
    return;
  // Where the guard is finished in \p Owner's body \p In, by a name of it or
  // of a reference to it.
  auto FinishingIn = [&](const Decl *Owner, const Stmt *In) {
    llvm::DenseSet<const Stmt *> Finishing;
    for (const VarDecl *Alias : Aliases)
      for (const DeclRefExpr *Named : Cache.namesIn(Owner, In, Alias, Context))
        if (const Stmt *At = finishingAt(Named, Context))
          Finishing.insert(At);
    return Finishing;
  };
  llvm::DenseSet<const Stmt *> Finishing = FinishingIn(Function, Body);
  // And the calls in the function of a local closure that captures the guard
  // by reference, each way through each body of which finishes it.
  forEachRunning(Function, Body, Body, Context, [&](const Stmt *S) {
    const auto *Closure = dyn_cast<LambdaExpr>(S);
    if (!Closure ||
        llvm::none_of(Closure->captures(), [&](const LambdaCapture &Capture) {
          return Capture.capturesVariable() &&
                 Capture.getCaptureKind() == LCK_ByRef &&
                 llvm::is_contained(Aliases, Capture.getCapturedVar());
        }))
      return;
    const llvm::SmallVector<const CXXMethodDecl *, 2> Bodies =
        runningOperators(Closure);
    if (Bodies.empty() ||
        !llvm::all_of(Bodies, [&](const CXXMethodDecl *Operator) {
          const llvm::DenseSet<const Stmt *> Inside =
              FinishingIn(Operator, Operator->getBody());
          return !Inside.empty() &&
                 passesOnEachWay(
                     Operator, Operator->getBody(),
                     [&](const CFlowEvent &Event) {
                       return Event.Kind == EEvent::Statement &&
                              Inside.contains(Event.Statement);
                     },
                     Context);
        }))
      return;
    for (const CClosureSite &Site :
         Cache.closureUses(Function, Body, Closure, Context).Calls)
      if (Site.Owner == Function && Site.Use.Parent)
        Finishing.insert(Site.Use.Parent);
  });
  const SourceManager &SM = Context.getSourceManager();
  // The local declared after the guard that what \p Definition gives the
  // pointer points to (pointedAtRootsOf): a local or a parameter held by
  // value, also a temporary a local reference extends, as that local, or
  // the local owner what an owner owns is reached through, not past what it
  // only refers to; of several, the one declared last. None of what can't be
  // told.
  llvm::DenseMap<const Expr *, const VarDecl *> Lates;
  auto LateOf = [&](const CSlotDefinition &Definition) -> const VarDecl * {
    const Expr *Value = Definition.Value.Value;
    if (!Definition.Known || !Value ||
        Definition.Value.Kind == CSlotValue::EKind::Given)
      return nullptr;
    auto [It, Inserted] = Lates.try_emplace(Value, nullptr);
    if (!Inserted)
      return It->second;
    const VarDecl *Latest = nullptr;
    for (const CStorageRoot &Root : pointedAtRootsOf(Value, Context).Roots) {
      if (Root.Kind != ERootKind::Frame &&
          !(Root.Kind == ERootKind::Owned && !Root.bPastBorrowed))
        continue;
      const auto *Pointee = dyn_cast_or_null<VarDecl>(Root.Variable);
      if (Pointee && Pointee->hasLocalStorage() &&
          SM.isBeforeInTranslationUnit(Guard->getLocation(),
                                       Pointee->getLocation()) &&
          (!Latest || SM.isBeforeInTranslationUnit(Latest->getLocation(),
                                                   Pointee->getLocation())))
        Latest = Pointee;
    }
    return Lates[Value] = Latest;
  };
  auto GuardEnds = [&](const CFlowEvent &Event) {
    return Event.Variable == Guard &&
           (Event.Kind == EEvent::AutomaticDestructor ||
            Event.Kind == EEvent::LifetimeEnds);
  };
  for (const VarDecl *Pointer : Pointers) {
    const auto Found = localSlotDefinitionsReaching(
        Function, CSlot{Pointer, {}, false},
        [&](const CSlotDefinition &Definition) {
          return Definition.Known && !LateOf(Definition);
        },
        [&](const CFlowEvent &Event) {
          return GuardEnds(Event) || (Event.Kind == EEvent::Statement &&
                                      Finishing.contains(Event.Statement));
        },
        [&](const CFlowEvent &Event) {
          return GuardEnds(Event) ||
                 (Event.Kind == EEvent::Statement &&
                  !Finishing.contains(Event.Statement) &&
                  mayEndFrame(Event.Statement));
        },
        Context);
    if (!Found)
      continue;
    for (const CSlotDefinitionAt &Each : *Found) {
      const VarDecl *Late = LateOf(Each.Definition);
      const Stmt *At = Each.Target
                           ? keptDerivationOf(Each.Target, {}, Context)
                           : Each.Element;
      if (!Late || !At)
        continue;
      CLifetimeUse Retarget;
      Retarget.Use = Each.Definition.Value.Value;
      Retarget.Used = EUsed::Pointee;
      Retarget.KeptIn.Kind = CKeptAt::EKind::Capture;
      Retarget.KeptIn.Closure = Cleanup;
      auto Arguments = [&](const MalterlibDiagnostic &Diag) {
        Diag << Pointer << Name << Late;
      };
      reportLifetime(Check, Retarget, Context,
                     {reportLocation(At->getBeginLoc(), SM),
                      "%0, which the cleanup of '%1' uses, is pointed at %2, "
                      "which is destroyed before the cleanup runs; point it "
                      "at what is declared before the result of '%1'",
                      Arguments});
    }
  }
}

void AsyncDestroyCheck::checkAsyncDestroy(const CallExpr *Call,
                                          ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  const DynTypedNode Parent =
      outerParent(DynTypedNode::create(*Call), Context);
  const auto *Await = Parent.get<CoroutineSuspendExpr>();
  const FunctionDecl *Callee = functionCalledBy(Call);
  if (!Callee)
    return;
  const StringRef Name =
      Callee->getIdentifier() ? identifierTextOf(Callee) : StringRef();

  // A function that returns the awaiter leaves the awaiting to its caller,
  // also through a local it is kept in that is only returned, `auto Awaiter
  // = fg_AsyncDestroyByValue(...); return Awaiter;`.
  auto OnlyReturned = [&](const VarDecl *Local) {
    const FunctionDecl *Function =
        enclosingFunction(DynTypedNode::create(*Call), Context);
    if (!Local || !Local->hasLocalStorage() ||
        Local->getType()->isReferenceType() || !Function ||
        !Function->getBody())
      return false;
    bool Returns = false;
    for (const auto &Nodes : matchEach<DeclRefExpr>(
             declRefExpr(to(varDecl(equalsNode(Local)))).bind("use"),
             *Function->getBody(), Context)) {
      DynTypedNode Node =
          DynTypedNode::create(*Nodes.getNodeAs<DeclRefExpr>("use"));
      for (int Depth = 0;; ++Depth) {
        const auto Parents = instantiatedParents(Node, Context);
        if (Parents.empty() || Depth > 8)
          return false;
        if (Parents[0].get<ReturnStmt>()) {
          Returns = true;
          break;
        }
        const auto *Move = Parents[0].get<CallExpr>();
        if (!Parents[0].get<ImplicitCastExpr>() &&
            !Parents[0].get<ParenExpr>() &&
            !Parents[0].get<CXXConstructExpr>() &&
            !Parents[0].get<MaterializeTemporaryExpr>() &&
            !(Move && Move->getNumArgs() == 1 &&
              (isMoveFunction(calleeName(Move)))))
          return false;
        Node = Parents[0];
      }
    }
    return Returns;
  };
  const bool Returned = Parent.get<ReturnStmt>() || Parent.get<CoreturnStmt>() ||
                        OnlyReturned(Parent.get<VarDecl>());
  // An awaiter that refers to the function it is given until it is awaited,
  // like the `TCAsyncDestroyAwaiter<FDestroy &, FDestroy>` of
  // `fg_AsyncDestroy`, unlike `fg_AsyncDestroyByValue`, which keeps a copy, is
  // returned as it is, not converted to one that keeps a copy: a temporary or
  // a local of the function that returns it is gone by then.
  const Expr *Callable = Call->getNumArgs() >= 1 ? Call->getArg(0) : nullptr;
  const auto *Awaiter = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Call->getType()->getAsCXXRecordDecl());
  const FunctionDecl *Returning =
      Returned ? enclosingFunction(DynTypedNode::create(*Call), Context)
               : nullptr;
  bool Gone = false;
  if (Returning && Callable && Awaiter &&
      Awaiter->getTemplateArgs().size() >= 1 &&
      Awaiter->getTemplateArgs()[0].getKind() == TemplateArgument::Type &&
      Awaiter->getTemplateArgs()[0].getAsType()->isReferenceType() &&
      (Context.hasSameUnqualifiedType(Returning->getReturnType(),
                                      Call->getType()) ||
       // Or the value of the future a coroutine returns, `co_return
       // fg_AsyncDestroy(Cleanup)` of a `TCFuture<CAwaiter>`.
       (!futureValueType(Returning->getReturnType()).isNull() &&
        Context.hasSameUnqualifiedType(
            futureValueType(Returning->getReturnType()), Call->getType())))) {
    const Expr *Value = Callable->IgnoreParenImpCasts();
    // Moved, `fg_Move(Cleanup)`, it is still the local.
    for (int Depth = 0; Depth < 4; ++Depth) {
      const auto *Move = dyn_cast<CallExpr>(Value);
      if (!Move || Move->getNumArgs() != 1 ||
          (!isMoveFunction(calleeName(Move)) &&
           !isForwardFunction(calleeName(Move))))
        break;
      Value = Move->getArg(0)->IgnoreParenImpCasts();
    }
    if (isa<MaterializeTemporaryExpr, LambdaExpr, CXXBindTemporaryExpr>(
            Value) ||
        isa<MaterializeTemporaryExpr>(Callable->IgnoreParens()))
      Gone = true;
    else {
      // Also through a local reference to it, `auto &Alias = Cleanup`, or
      // an unchanged local pointer to it, `*pCleanup` with `auto *pCleanup =
      // &Cleanup`, and either value of a conditional, `bChoose ? Cleanup :
      // Other`.
      // Each value once, also one that a chain of conditionals reaches in
      // many ways, `auto &r1 = bChoose ? r0 : r0`, which would take
      // exponential time.
      llvm::SmallPtrSet<const Expr *, 16> Followed;
      TCRecursive<void(const Expr *, int)> Follow = [&](const Expr *Value,
                                                          int Level) {
        const VarDecl *Local = nullptr;
        for (int Depth = 0; Depth < 64 && Value; ++Depth) {
          Value = Value->IgnoreParenImpCasts();
          if (!Followed.insert(Value).second)
            return;
          if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(Value))
            Value = Cleanups->getSubExpr()->IgnoreParenImpCasts();
          // The last of a comma, `(fg_Prepare(), Cleanup)`, and what the
          // address of a dereference is, `*&Cleanup`.
          if (const auto *Comma = dyn_cast<BinaryOperator>(Value);
              Comma && Comma->getOpcode() == BO_Comma) {
            Value = Comma->getRHS();
            continue;
          }
          if (const auto *Deref = dyn_cast<UnaryOperator>(Value);
              Deref && Deref->getOpcode() == UO_Deref)
            if (const auto *Address = dyn_cast<UnaryOperator>(
                    Deref->getSubExpr()->IgnoreParenImpCasts());
                Address && Address->getOpcode() == UO_AddrOf) {
              Value = Address->getSubExpr();
              continue;
            }
          // Also cast to a reference to it, `static_cast<decltype(Cleanup)
          // &&>(Cleanup)`, which is it.
          while (const auto *Cast = dyn_cast<ExplicitCastExpr>(Value)) {
            if (!Cast->isGLValue())
              break;
            Value = Cast->getSubExpr()->IgnoreParenImpCasts();
          }
          // A temporary that a local reference extends, `auto &&fCleanup =
          // []() -> TCFuture<void> { ... }`, goes with the local.
          if (Depth > 0 &&
              isa<MaterializeTemporaryExpr, LambdaExpr, CXXBindTemporaryExpr>(
                  Value)) {
            Gone = true;
            break;
          }
          if (const auto *Conditional = dyn_cast<ConditionalOperator>(Value)) {
            // Only the one a constant condition chooses, `true ? g_fCleanup :
            // fCleanup`.
            bool Chosen = false;
            if (!Conditional->getCond()->isValueDependent() &&
                foldsCondition(Conditional->getCond(), Chosen, Context)) {
              Value = Chosen ? Conditional->getTrueExpr()
                             : Conditional->getFalseExpr();
              continue;
            }
            // Deeper than is looked into, it can be gone.
            if (Level >= 256) {
              Gone = true;
              return;
            }
            for (const Expr *Arm : keptArms(Conditional))
              Follow(Arm, Level + 1);
            return;
          }
          if (const auto *Deref = dyn_cast<UnaryOperator>(Value);
              Deref && Deref->getOpcode() == UO_Deref) {
            const auto *Pointer = dyn_cast<DeclRefExpr>(
                Deref->getSubExpr()->IgnoreParenImpCasts());
            const auto *PointerLocal =
                Pointer ? dyn_cast<VarDecl>(Pointer->getDecl()) : nullptr;
            const auto *Address =
                PointerLocal && PointerLocal->hasLocalStorage() &&
                        !isa<ParmVarDecl>(PointerLocal) &&
                        PointerLocal->getInit() &&
                        !isChangedVariable(PointerLocal)
                    ? dyn_cast<UnaryOperator>(
                          PointerLocal->getInit()->IgnoreParenImpCasts())
                    : nullptr;
            Local = nullptr;
            Value = Address && Address->getOpcode() == UO_AddrOf
                        ? Address->getSubExpr()
                        : nullptr;
            continue;
          }
          const auto *Ref = dyn_cast<DeclRefExpr>(Value);
          Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
              !Local->getType()->isReferenceType() || !Local->getInit())
            break;
          Value = Local->getInit();
          Local = nullptr;
        }
        if (Local && Local->hasLocalStorage() &&
            !Local->getType()->isReferenceType())
          Gone = true;
      };
      Follow(Value, 0);
    }
  }
  if (Gone) {
    const SourceLocation Loc = reportLocation(Call->getBeginLoc(), SM);
    if (Loc.isValid())
      diag(Loc, "'%0' refers to its cleanup function until it is co_awaited, "
                "but the function it is returned from destroys that first; "
                "use fg_AsyncDestroyByValue")
          << Name;
  } else if (Returned) {
    // The cleanup function is checked below.
  } else if (!Await) {
    const SourceLocation Loc = reportLocation(Call->getBeginLoc(), SM);
    if (Loc.isValid())
      diag(Loc, "'%0' schedules the cleanup when it is co_awaited; co_await "
                "it and keep the result in a named variable")
          << Name;
  } else {
    // The cleanup runs when the result goes out of scope, so a temporary
    // runs it at the end of the statement.
    const DynTypedNode AwaitNode = DynTypedNode::create(*Await);
    DynTypedNode Holder = outerParent(AwaitNode, Context);
    // Either branch of a conditional that initializes it, like `auto
    // Cleanup = bLeft ? co_await fg_AsyncDestroy(Left) : co_await
    // fg_AsyncDestroy(Right)`.
    // Also the last of a comma, `auto Cleanup = (fg_Prepare(), co_await
    // fg_AsyncDestroy(...))`.
    for (DynTypedNode Branch = AwaitNode;;) {
      const auto *Conditional = Holder.get<AbstractConditionalOperator>();
      const auto *Comma = Holder.get<BinaryOperator>();
      const Expr *Inner = Branch.get<Expr>();
      if (!(Conditional && Conditional->getCond() != Inner) &&
          !(Comma && Comma->getOpcode() == BO_Comma && Inner &&
            Comma->getRHS()->IgnoreParens() == Inner->IgnoreParens()))
        break;
      Branch = Holder;
      Holder = outerParent(Holder, Context);
    }
    // An element of a local array or aggregate it initializes, like
    // `CAsyncDestroy Guards[] = {co_await fg_AsyncDestroy(...)}`, also with
    // parentheses, `CHolder Holder(co_await fg_AsyncDestroy(...))`, which
    // lives as long as it.
    for (int Depth = 0; Depth < 8 && (Holder.get<InitListExpr>() ||
                                      Holder.get<CXXParenListInitExpr>());
         ++Depth)
      Holder = outerParent(Holder, Context);
    const auto *Variable = Holder.get<VarDecl>();
    if (!Variable || !Variable->hasLocalStorage()) {
      const SourceLocation Loc = reportLocation(Await->getBeginLoc(), SM);
      if (Loc.isValid())
        diag(Loc, "the result of co_awaiting '%0' is not kept in a named "
                  "variable, so the cleanup runs at the end of the statement "
                  "instead of at the end of the scope")
            << Name;
    } else if (const FunctionDecl *Function =
                   enclosingFunction(DynTypedNode::create(*Call), Context);
               Function && Function->getBody()) {
      // Moving the result elsewhere runs the cleanup after the locals it
      // refers to are gone.
      // Also forwarded, swapped or exchanged, or cast to an rvalue, like
      // `static_cast<CCleanup &&>(Cleanup)`.
      const auto MovingFunction = hasAnyName([] {
        const CVocabulary &Names = vocabulary();
        std::vector<StringRef> Moving;
        for (const CNameList *List :
             {&Names.MoveOrForwardFunctions, &Names.SwapFunctions,
              &Names.ExchangeFunctions})
          llvm::append_range(Moving, List->names());
        return Moving;
      }());
      // Also through local references to it, `auto &Alias = Cleanup`, also
      // of references to those, and either of a conditional, `auto &Alias =
      // bChoose ? Cleanup : Other`.
      const CFunctionIndex &Index = indexOf(Function);
      const llvm::SmallVector<const VarDecl *, 8> &References = Index.References;
      VariableSet Aliases{Variable};
      // From each variable to those that are made of it, each found once,
      // also in an order of declarations that is the opposite of how they
      // are made of each other.
      llvm::DenseMap<const VarDecl *, llvm::SmallVector<const VarDecl *, 2>>
          MadeOf;
      auto Spread = [&](const VariableSet &Seeds, VariableSet &Found) {
        llvm::SmallVector<const VarDecl *, 8> Worklist(Seeds.begin(),
                                                       Seeds.end());
        while (!Worklist.empty())
          for (const VarDecl *Made : MadeOf.lookup(Worklist.pop_back_val()))
            if (Found.insert(Made).second)
              Worklist.push_back(Made);
      };
      for (const VarDecl *Reference : References)
        for (const Expr *Init : alternativesOf(Reference->getInit()))
          if (const auto *Ref = dyn_cast<DeclRefExpr>(Init))
            if (const auto *Initial = dyn_cast<VarDecl>(Ref->getDecl()))
              MadeOf[Initial].push_back(Reference);
      Spread(VariableSet{Variable}, Aliases);
      // And through a pointer to it, `fg_Move(*pCleanup)` with `auto
      // *pCleanup = &Cleanup`, also a copy of one, `auto *pCopy = pCleanup`.
      // Also one pointed at it after, `pCleanup = &Cleanup`.
      const llvm::SmallVector<const VarDecl *, 8> &PointerLocals =
          Index.Pointers;
      llvm::DenseMap<const VarDecl *, llvm::SmallVector<const Expr *, 2>>
          PointerValues;
      for (const VarDecl *Pointer : PointerLocals)
        if (Pointer->getInit())
          PointerValues[Pointer].push_back(Pointer->getInit());
      if (!PointerLocals.empty()) {
        const VariableSet IsPointerLocal(PointerLocals.begin(),
                                         PointerLocals.end());
        TCRecursive<void(const Stmt *)> Assigned = [&](const Stmt *S) {
          if (!S)
            return;
          if (const auto *Assign = dyn_cast<BinaryOperator>(S);
              Assign && Assign->getOpcode() == BO_Assign)
            if (const auto *Target = dyn_cast<DeclRefExpr>(
                    Assign->getLHS()->IgnoreParenImpCasts()))
              if (const auto *Pointer = dyn_cast<VarDecl>(Target->getDecl());
                  Pointer && IsPointerLocal.contains(Pointer))
                PointerValues[Pointer].push_back(Assign->getRHS());
          for (const Stmt *Child : keptChildren(S))
            Assigned(Child);
        };
        Assigned(Function->getBody());
      }
      // A pointer to it, or one given another such pointer.
      VariableSet PointerAliases;
      MadeOf.clear();
      for (const VarDecl *Pointer : PointerLocals)
        for (const Expr *Value : PointerValues.lookup(Pointer)) {
          const Expr *Init = Value->IgnoreParenImpCasts();
          bool Addressed = false;
          if (const auto *Address = dyn_cast<UnaryOperator>(Init);
              Address && Address->getOpcode() == UO_AddrOf) {
            Init = Address->getSubExpr()->IgnoreParenImpCasts();
            Addressed = true;
          }
          const auto *Target = dyn_cast<DeclRefExpr>(Init);
          const auto *Of =
              Target ? dyn_cast<VarDecl>(Target->getDecl()) : nullptr;
          if (!Of)
            continue;
          if (!Addressed)
            MadeOf[Of].push_back(Pointer);
          else if (Aliases.contains(Of))
            PointerAliases.insert(Pointer);
        }
      Spread(VariableSet(PointerAliases), PointerAliases);
      // Whether what has type \p Type is the guard or holds it, by value, as
      // a member, also of a member, or a base, or an element of an array.
      const QualType GuardType = Await->getType()
                                     .getNonReferenceType()
                                     .getCanonicalType()
                                     .getUnqualifiedType();
      // Each type once, as many members can have the same type, which would
      // take a time that grows exponentially with how deep they nest. One that
      // is being decided, which a type can't hold by value, holds none.
      llvm::DenseMap<const Type *, bool> Holds;
      TCRecursive<bool(QualType)> HoldsGuard = [&](QualType Type) {
        Type = QualType(Type->getBaseElementTypeUnsafe(), 0)
                   .getCanonicalType()
                   .getUnqualifiedType();
        if (Type == GuardType)
          return true;
        const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
        if (!Record || !Record->hasDefinition())
          return false;
        if (auto [It, Inserted] = Holds.try_emplace(Type.getTypePtr(), false);
            !Inserted)
          return It->second;
        Record = Record->getDefinition();
        const bool Held =
            llvm::any_of(Record->fields(),
                         [&](const FieldDecl *Field) {
                           return !Field->getType()->isReferenceType() &&
                                  HoldsGuard(Field->getType());
                         }) ||
            llvm::any_of(Record->bases(), [&](const CXXBaseSpecifier &Base) {
              return HoldsGuard(Base.getType());
            });
        Holds[Type.getTypePtr()] = Held;
        return Held;
      };
      // Also an element of an array of guards, `fg_Move(Guards[0])`.
      const auto IsGuard = ignoringParenImpCasts(
          anyOf(declRefExpr(to(varDecl(isOneOf(Aliases)))),
                arraySubscriptExpr(hasBase(ignoringParenImpCasts(
                    declRefExpr(to(varDecl(isOneOf(Aliases))))))),
                // Or a member of an aggregate that holds it,
                // `fg_Move(Holder.m_Guard)`, not another one beside it,
                // `fg_Move(Holder.m_Result)`.
                memberExpr(hasObjectExpression(ignoringParenImpCasts(
                               declRefExpr(to(varDecl(isOneOf(Aliases)))))),
                           satisfies([&](const Expr *Member) {
                             return HoldsGuard(Member->getType());
                           })),
                unaryOperator(hasOperatorName("*"),
                              hasUnaryOperand(ignoringParenImpCasts(declRefExpr(
                                  to(varDecl(isOneOf(PointerAliases)))))))));
      // Also either of a conditional, `fg_Move(bChoose ? Cleanup : Other)`.
      const StatementMatcher Guard = expr(IsGuard);
      const auto IsVariable = expr(satisfies([&](const Expr *E) {
        return llvm::any_of(alternativesOf(E), [&](const Expr *Alternative) {
          return !match(Guard, *Alternative, Context).empty();
        });
      }));
      // Only when it is named after it is made, which most results aren't.
      const bool Named =
          llvm::any_of(Aliases,
                       [&](const VarDecl *Alias) {
                         return Index.Named.contains(Alias);
                       }) ||
          llvm::any_of(PointerAliases, [&](const VarDecl *Alias) {
            return Index.Named.contains(Alias);
          });
      const auto Moves =
          !Named
              ? llvm::SmallVector<BoundNodes, 4>()
              : matchEach<Expr>(
                    expr(
                        anyOf(
                            callExpr(anyOf(callee(functionDecl(MovingFunction)),
                                           callee(unresolvedLookupExpr(
                                               hasAnyDeclaration(
                                                   ast_matchers::namedDecl(
                                                       MovingFunction))))),
                                     hasAnyArgument(IsVariable)),
                            explicitCastExpr(hasSourceExpression(IsVariable))),
                        unless(isInUnevaluatedContext()))
                        .bind("move"),
                    *Function->getBody(), Context);
      for (const auto &Move : Moves) {
        // `co_await fg_Move(Cleanup)` runs the cleanup early, on purpose, and
        // so does `co_await fg_Move(Cleanup).f_Wrap()`. Awaiting something
        // the cleanup is passed to does not.
        const auto *MoveExpr = Move.getNodeAs<Expr>("move");
        // Also a cast to an rvalue reference awaited, `co_await
        // static_cast<CGuard &&>(Cleanup)`.
        if (const auto *Cast = dyn_cast<ExplicitCastExpr>(MoveExpr);
            Cast && (!Cast->getTypeAsWritten()->isRValueReferenceType() ||
                     isAwaitedCleanup(Cast, Context)))
          continue;
        // Bound to a local reference, `auto &&Alias = fg_Move(Cleanup)`,
        // which moves nothing.
        {
          DynTypedNode Node = DynTypedNode::create(*MoveExpr);
          bool Bound = false;
          for (int Depth = 0; Depth < 8; ++Depth) {
            const auto Parents = instantiatedParents(Node, Context);
            if (Parents.empty())
              break;
            if (const auto *Alias = Parents[0].get<VarDecl>()) {
              Bound = Alias->getType()->isReferenceType();
              break;
            }
            if (!Parents[0].get<ImplicitCastExpr>() &&
                !Parents[0].get<ParenExpr>() &&
                !Parents[0].get<ExprWithCleanups>() &&
                !Parents[0].get<MaterializeTemporaryExpr>())
              break;
            Node = Parents[0];
          }
          if (Bound)
            continue;
        }
        // Forwarded as an lvalue, `fg_Forward<CGuard &>(Cleanup)`, moves
        // nothing.
        if (const auto *Forward = dyn_cast<CallExpr>(MoveExpr);
            Forward && (isForwardFunction(calleeName(Forward))) &&
            Forward->isLValue())
          continue;
        // Also forwarded as an rvalue, `co_await fg_Forward<CGuard>(Cleanup)`,
        // which is the same.
        if (const auto *MoveCall = dyn_cast<CallExpr>(MoveExpr);
            MoveCall &&
            (isMoveFunction(calleeName(MoveCall)) ||
             ((isForwardFunction(calleeName(MoveCall))) &&
              MoveCall->isXValue())) &&
            isAwaitedCleanup(MoveCall, Context))
          continue;
        const SourceLocation Loc = reportLocation(MoveExpr->getBeginLoc(), SM);
        if (Loc.isValid())
          diag(Loc, "the result of '%0' is moved, so the cleanup can run "
                    "after the locals it refers to are gone; keep it in "
                    "its variable")
              << Name;
      }
      // Returning the result moves it implicitly.
      const auto Returns =
          !Named ? llvm::SmallVector<BoundNodes, 4>()
                 : matchEach<Stmt>(
                       stmt(anyOf(returnStmt(), coreturnStmt())).bind("return"),
                       *Function->getBody(), Context);
      for (const auto &Return : Returns) {
        const Stmt *S = Return.getNodeAs<Stmt>("return");
        const Expr *Value = nullptr;
        if (const auto *Plain = dyn_cast<ReturnStmt>(S))
          Value = Plain->getRetValue();
        else
          Value = cast<CoreturnStmt>(S)->getOperand();
        const auto *Ref =
            Value ? dyn_cast<DeclRefExpr>(skipWrappers(Value)) : nullptr;
        if (!Ref || Ref->getDecl() != Variable)
          continue;
        const SourceLocation Loc = reportLocation(Ref->getBeginLoc(), SM);
        if (Loc.isValid())
          diag(Loc, "the result of '%0' is returned, so the cleanup can run "
                    "after the locals it refers to are gone; keep it in "
                    "its variable")
              << Name;
      }
      // A pointer the cleanup reads, pointed later at a local that is
      // destroyed before it runs.
      checkRetargets(*this, Call, Function, Variable,
                     llvm::SmallVector<const VarDecl *, 4>(Aliases.begin(),
                                                           Aliases.end()),
                     Name, Context);
    }
  }

  // Also one a factory makes, `fg_AsyncDestroy(fMakeCleanup())`.
  if (Call->getNumArgs() >= 1) {
    if (const LambdaExpr *Cleanup = cleanupLambdaOf(Call->getArg(0))) {
      // What its call operator is instantiated as, of a generic one.
      const CXXMethodDecl *Pattern = Cleanup->getCallOperator();
      for (const FunctionDecl *Operator :
           sharedInstantiationIndex().bodiesOf(Pattern, Context)) {
        std::optional<InstantiationScope> Scope;
        if (Operator != Pattern)
          Scope.emplace(*this, Operator);
        checkCleanup(Cleanup, Operator, Context);
      }
    }
  }
}

namespace {

// Of ofThis: what is returned, whether as a pointer, and its AST.
using COfThisKey = std::tuple<const Expr *, unsigned, const ASTContext *>;
static bool ofThisUncached(const COfThisKey &Key);

/// Whether \p Value, which a member function returns, is of its object,
/// `this`, also through members, `&m_Value`, and the arrays in it, `m_Data`,
/// as a pointer to it when \p Pointer, or what another getter of the object
/// gives so, `return fp_GetStr();`. Each once for each way it is met at:
/// getters that each return the one before more than once would be looked
/// at again through each path to them; one that returns itself isn't by
/// that while it is told, and one whose recursion doesn't settle is.
static bool ofThis(const Expr *Value, bool Pointer, const ASTContext &Context) {
  if (!Value)
    return false;
  struct COfThis {
    TCSummaries<COfThisKey, bool> Decided{&ofThisUncached, false, true};
  };
  return astCache<COfThis>(Context).Decided.get(
      {Value, Pointer ? 1u : 0u, &Context});
}

static bool ofThisUncached(const COfThisKey &Key) {
  const Expr *Value = std::get<0>(Key);
  const bool Pointer = std::get<1>(Key) != 0;
  const ASTContext &Context = *std::get<2>(Key);
  Value = Value->IgnoreParens();
  // Not a pointer it holds, `return m_pValue;`, which points
  // elsewhere.
  if (const auto *Cast = dyn_cast<CastExpr>(Value)) {
    switch (Cast->getCastKind()) {
    case CK_ArrayToPointerDecay:
      return Pointer && ofThis(Cast->getSubExpr(), false, Context);
    case CK_NoOp:
    case CK_DerivedToBase:
    case CK_UncheckedDerivedToBase:
      return ofThis(Cast->getSubExpr(), Pointer, Context);
    default:
      return false;
    }
  }
  if (isa<CXXThisExpr>(Value))
    return Pointer;
  // Also what another getter of the object gives, `return fp_GetStr();`,
  // also of an object in it, `return m_Imp.f_GetStr();`, but not a
  // virtual one, which an override can replace.
  if (const CMemberCall Forwarded = memberCallOf(Value);
      Forwarded && Forwarded->getMethodDecl() &&
      Forwarded->getImplicitObjectArgument() && Forwarded->memberExpr() &&
      ofThis(Forwarded->getImplicitObjectArgument(),
             Forwarded->memberExpr()->isArrow(), Context) &&
      !Forwarded->getMethodDecl()->isVirtual() &&
      (Pointer ? Forwarded->getType()->isPointerType()
               : Forwarded->isGLValue()))
    return llvm::any_of(
        returnedValues(Forwarded->getMethodDecl()),
        [&](const Expr *Inner) { return ofThis(Inner, Pointer, Context); });
  if (const auto *Address = dyn_cast<UnaryOperator>(Value);
      Address && Pointer && Address->getOpcode() == UO_AddrOf)
    return ofThis(Address->getSubExpr(), false, Context);
  if (const auto *Deref = dyn_cast<UnaryOperator>(Value);
      Deref && !Pointer && Deref->getOpcode() == UO_Deref)
    return ofThis(Deref->getSubExpr(), true, Context);
  if (const auto *Member = dyn_cast<MemberExpr>(Value); Member && !Pointer)
    return ofThis(Member->getBase(), Member->isArrow(), Context);
  if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Value);
      Subscript && !Pointer)
    return ofThis(Subscript->getBase(), true, Context);
  if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Value))
    return llvm::any_of(keptArms(Conditional), [&](const Expr *Arm) {
      return ofThis(Arm, Pointer, Context);
    });
  return false;
}

/// What refers to the storage of the variables a cleanup captures by
/// reference: the captured variables, references bound to them or to their
/// members and elements, like `auto &Sub = Captured.m_Value`, and pointers to
/// them, like `auto *pCaptured = &Captured` and copies of such pointers. What
/// a captured pointer points to is not in the captured storage, so neither
/// `auto *pCopy = pCaptured` nor `auto &Pointee = *pCaptured` refers to it.
///
/// References can't be rebound, so they are found once. Pointers can be
/// assigned, so whether one points into the storage is followed along the
/// control flow, in a bit per pointer that may.
class CleanupStorage : public OncePerTypeVisitor<CleanupStorage> {
public:
  explicit CleanupStorage(const LambdaExpr *Cleanup) {
    for (const LambdaCapture &Capture : Cleanup->captures()) {
      if (Capture.capturesVariable() && Capture.getCaptureKind() == LCK_ByRef &&
          !captureAlive(Cleanup, Capture,
                        Cleanup->getCallOperator()->getASTContext(),
                        /*EachUse=*/false))
        Storage.insert(Capture.getCapturedVar());
    }
  }

  bool empty() const { return Storage.empty(); }

  /// The pointers that may point into the storage, by their bits.
  llvm::DenseMap<const ValueDecl *, unsigned> Pointers;
  /// The captured variables and the references bound into them. A reference
  /// bound through a pointer, like `auto &Value = *pValue`, is added when the
  /// control flow reaches it with the pointer pointing into the storage.
  llvm::SmallPtrSet<const ValueDecl *, 8> Storage;
  /// The references that may be bound into the storage.
  llvm::SmallPtrSet<const ValueDecl *, 8> MaybeStorage;

  // Pointers that are initialized or assigned to point into the storage at
  // some point, and references that may be bound into it, in source order.
  bool VisitVarDecl(VarDecl *Variable) {
    if (!Variable->getInit())
      return true;
    const Expr *Init = withoutCleanups(Variable->getInit());
    if (Variable->getType()->isReferenceType() && isInStorage(Init, nullptr))
      MaybeStorage.insert(Variable);
    else if (Variable->getType()->isPointerType() &&
             pointsIntoStorage(Init, nullptr))
      Pointers.try_emplace(Variable, Pointers.size());
    return true;
  }

  /// Adds the references \p Declaration binds into the storage, with the
  /// pointers that point into it in \p Bits.
  void bindReferences(const DeclStmt *Declaration,
                      const llvm::BitVector &Bits) {
    for (const Decl *D : Declaration->decls()) {
      const auto *Variable = dyn_cast<VarDecl>(D);
      if (Variable && MaybeStorage.contains(Variable) &&
          isInStorage(withoutCleanups(Variable->getInit()), &Bits))
        Storage.insert(Variable);
    }
  }
  bool VisitBinaryOperator(BinaryOperator *Binary) {
    if (pointsIntoStorage(Binary->getRHS(), nullptr))
      for (const DeclRefExpr *Ref : assignedPointers(Binary))
        Pointers.try_emplace(aliasedPointer(Ref->getDecl()), Pointers.size());
    return true;
  }

  /// The local pointers \p Binary can assign, if it is an assignment of
  /// them: one, or either of a conditional, `(bFirst ? pFirst : pSecond) =
  /// ...`, which assigns only one of them.
  static llvm::SmallVector<const DeclRefExpr *, 2>
  assignedPointers(const BinaryOperator *Binary) {
    llvm::SmallVector<const DeclRefExpr *, 2> Pointers;
    if (Binary->getOpcode() != BO_Assign)
      return Pointers;
    for (const DeclRefExpr *Ref : assignedVariables(Binary->getLHS()))
      if (const auto *Variable =
              dyn_cast<VarDecl>(aliasedPointer(Ref->getDecl()));
          Variable && Variable->getType()->isPointerType())
        Pointers.push_back(Ref);
    return Pointers;
  }

  /// The local pointer that \p Declaration is, also through references to
  /// it, `int *&pAlias = pValue`, which can't be rebound.
  static const ValueDecl *aliasedPointer(const ValueDecl *Declaration) {
    for (int Depth = 0; Depth < 8; ++Depth) {
      const auto *Variable = dyn_cast_or_null<VarDecl>(Declaration);
      if (!Variable || !Variable->getType()->isReferenceType() ||
          !Variable->getType()->getPointeeType()->isPointerType() ||
          !Variable->getInit())
        break;
      const auto *Bound =
          dyn_cast<DeclRefExpr>(Variable->getInit()->IgnoreParenImpCasts());
      if (!Bound)
        break;
      Declaration = Bound->getDecl();
    }
    return Declaration;
  }

  static const Expr *withoutCleanups(const Expr *E) {
    if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(E))
      return Cleanups->getSubExpr();
    return E;
  }

  /// Whether the object \p E designates is in the storage. \p Bits says
  /// which pointers point into the storage, or null for any that may.
  bool isInStorage(const Expr *E, const llvm::BitVector *Bits) const {
    if (stackNearlyExhausted())
      return withStack([&] { return isInStorage(E, Bits); });
    E = E->IgnoreParens();
    if (const auto *Cast = dyn_cast<CastExpr>(E);
        Cast && (Cast->getCastKind() == CK_NoOp ||
                 Cast->getCastKind() == CK_DerivedToBase ||
                 Cast->getCastKind() == CK_UncheckedDerivedToBase ||
                 Cast->getCastKind() == CK_LValueBitCast))
      return isInStorage(Cast->getSubExpr(), Bits);
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E))
      return Storage.contains(Ref->getDecl()) ||
             (!Bits && MaybeStorage.contains(Ref->getDecl()));
    if (const auto *Member = dyn_cast<MemberExpr>(E))
      return Member->isArrow() ? pointsIntoStorage(Member->getBase(), Bits)
                               : isInStorage(Member->getBase(), Bits);
    // Binding a reference to `fg_Move(Captured)` doesn't move the object.
    if (const auto *Call = dyn_cast<CallExpr>(E);
        Call && Call->getNumArgs() == 1 &&
        asCallTo(Call, vocabulary().MoveOrForwardFunctions.names()))
      return isInStorage(Call->getArg(0), Bits);
    if (const auto *Unary = dyn_cast<UnaryOperator>(E);
        Unary && Unary->getOpcode() == UO_Deref)
      return pointsIntoStorage(Unary->getSubExpr(), Bits);
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E))
      return pointsIntoStorage(Subscript->getBase(), Bits);
    // Either arm of `bChoose ? Value : Local`.
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E))
      return llvm::any_of(keptArms(Conditional), [&](const Expr *Arm) {
        return isInStorage(Arm, Bits);
      });
    // What overloaded operators give, like `Captured[0]` or `*Captured` of a
    // container or an optional, is taken to be in the object.
    if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
        Call && Call->getNumArgs() >= 1 &&
        (Call->getOperator() == OO_Subscript ||
         Call->getOperator() == OO_Star || Call->getOperator() == OO_Arrow))
      return isInStorage(Call->getArg(0), Bits);
    // Also what a getter gives by reference, `Name.f_Get()`.
    if (const CMemberCall Call = memberCallOf(E))
      return getsFromStorage(Call, Bits);
    return false;
  }

  /// Whether the visible member function \p Call calls can give back what is
  /// in its object, `char const *f_GetStr() const { return m_Data; }` or
  /// `CValue const &f_Get() const { return m_Value; }`, on any of its
  /// returns, when \p Call is of what is in the storage.
  bool getsFromStorage(CMemberCall Call, const llvm::BitVector *Bits) const {
    const CXXMethodDecl *Method = Call->getMethodDecl();
    const auto *Callee =
        Call->memberExpr();
    if (!Method || !Callee || Method->isExplicitObjectMemberFunction())
      return false;
    // Of its object, `this`, also through members, `&m_Value`, and the arrays
    // in it, `m_Data` (ofThis).
    auto OfThis = [&](const Expr *Value, bool Pointer) {
      return ofThis(Value, Pointer, Method->getASTContext());
    };
    const QualType Returned = Method->getReturnType();
    const bool Pointer = Returned->isPointerType();
    if (!Pointer && !Returned->isReferenceType())
      return false;
    for (const Expr *Value : returnedValues(Method))
      if (OfThis(Value, Pointer))
        return Callee->isArrow()
                   ? pointsIntoStorage(Call->getImplicitObjectArgument(), Bits)
                   : isInStorage(Call->getImplicitObjectArgument(), Bits);
    return false;
  }

  /// Whether the pointer value \p E points into the storage.
  bool pointsIntoStorage(const Expr *E, const llvm::BitVector *Bits) const {
    if (stackNearlyExhausted())
      return withStack([&] { return pointsIntoStorage(E, Bits); });
    E = withoutCleanups(E)->IgnoreParens();
    if (const auto *Cast = dyn_cast<CastExpr>(E)) {
      switch (Cast->getCastKind()) {
      case CK_ArrayToPointerDecay:
        return isInStorage(Cast->getSubExpr(), Bits);
      case CK_LValueToRValue: {
        const auto *Ref =
            dyn_cast<DeclRefExpr>(Cast->getSubExpr()->IgnoreParens());
        if (!Ref)
          return false;
        // Also the value of a pointer it captures by reference, `[&pValue]`,
        // that points to a local of the function it is in, unchanged, `int
        // *pValue = &Value`, which is gone like what it captures.
        if (const auto *Captured = dyn_cast<VarDecl>(Ref->getDecl());
            Captured && Storage.contains(Captured) &&
            Captured->getType()->isPointerType() && Captured->getInit() &&
            !isChangedVariable(Captured))
          if (const Expr *Addressed = addressedBy(
                  withoutCleanups(Captured->getInit())->IgnoreParenImpCasts()))
            if (const auto *Local =
                    dyn_cast<DeclRefExpr>(Addressed->IgnoreParenImpCasts());
                Local && isa<VarDecl>(Local->getDecl()) &&
                cast<VarDecl>(Local->getDecl())->hasLocalStorage() &&
                !cast<VarDecl>(Local->getDecl())->getType()->isReferenceType())
              return true;
        auto It = Pointers.find(aliasedPointer(Ref->getDecl()));
        return It != Pointers.end() && (!Bits || Bits->test(It->second));
      }
      case CK_NoOp:
      case CK_BitCast:
      case CK_DerivedToBase:
      case CK_UncheckedDerivedToBase:
        return pointsIntoStorage(Cast->getSubExpr(), Bits);
      default:
        return false;
      }
    }
    if (const Expr *Addressed = addressedBy(E))
      return isInStorage(Addressed, Bits);
    if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
        Call && Call->getNumArgs() >= 1 && Call->getOperator() == OO_Arrow)
      return isInStorage(Call->getArg(0), Bits);
    // Pointer arithmetic stays in the same object, like `Values + 1`.
    if (const auto *Binary = dyn_cast<BinaryOperator>(E);
        Binary && Binary->isAdditiveOp()) {
      if (Binary->getLHS()->getType()->isPointerType())
        return pointsIntoStorage(Binary->getLHS(), Bits);
      if (Binary->getRHS()->getType()->isPointerType())
        return pointsIntoStorage(Binary->getRHS(), Bits);
    }
    // Either arm of `bFlag ? &Value : &Local`.
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E))
      return llvm::any_of(keptArms(Conditional), [&](const Expr *Arm) {
        return pointsIntoStorage(Arm, Bits);
      });
    // What a getter gives, `Name.f_GetStr()`, and an argument a function
    // gives back, `fg_Identity(pValue)`.
    if (const CMemberCall Call = memberCallOf(E))
      return getsFromStorage(Call, Bits);
    if (const auto *Call = dyn_cast<CallExpr>(E))
      if (const Expr *Given = returnedArgument(Call))
        return pointsIntoStorage(Given, Bits);
    return false;
  }
};

/// Finds the references to local pointers that assign them, which don't
/// use what they point to.
class AssignedPointerFinder : public OncePerTypeVisitor<AssignedPointerFinder> {
public:
  llvm::SmallPtrSet<const Expr *, 8> Assigned;

  bool VisitBinaryOperator(BinaryOperator *Binary) {
    for (const DeclRefExpr *Ref : CleanupStorage::assignedPointers(Binary))
      Assigned.insert(Ref);
    return true;
  }
};

} // namespace

void AsyncDestroyCheck::checkCleanup(const LambdaExpr *Cleanup,
                                     const FunctionDecl *Function,
                                     ASTContext &Context) {
  CleanupStorage Storage(Cleanup);
  if (Storage.empty())
    return;
  // In code that runs to completion, the cleanup ends before the coroutine
  // does, while its parameters are there, and before an early await of its
  // guard, while the locals declared before the guard are: those outlive the
  // cleanup, like the reference capture check tells.
  if (inRunsToCompletionContext(Cleanup, Context, /*AfterSwitch=*/true)) {
    const std::optional<RunsToCompletionJoin> Join =
        lambdaJoin(Cleanup, Context, nullptr);
    // What a capture designates, alive until the join (CKeptAt::Joined).
    CKeptAt Joined;
    Joined.Kind = CKeptAt::EKind::Joined;
    Joined.Closure = Cleanup;
    Joined.Join = Join ? &*Join : nullptr;
    Joined.Site = Cleanup;
    const CStorageUse UntilJoin =
        Join ? keptContext(Joined, Context) : CStorageUse{};
    llvm::SmallVector<const ValueDecl *, 8> Outliving;
    for (const LambdaCapture &Capture : Cleanup->captures()) {
      const ValueDecl *Variable =
          Capture.capturesVariable() ? Capture.getCapturedVar() : nullptr;
      if (!Variable || !Storage.Storage.contains(Variable))
        continue;
      const Expr *Init = captureInitOf(Cleanup, Capture);
      if ((isa<ParmVarDecl>(Variable) &&
           outlivesEnclosing(Variable, Cleanup)) ||
          (Join && Init && aliveAtUse(Init, 0, UntilJoin, Context)))
        Outliving.push_back(Variable);
    }
    for (const ValueDecl *Variable : Outliving)
      Storage.Storage.erase(Variable);
    if (Storage.empty())
      return;
  }
  // A pointer copied from one that is only found later in the source, like in
  // a loop, is found by going over the body until nothing new is.
  for (size_t nFound = SIZE_MAX;
       nFound != Storage.Pointers.size() + Storage.MaybeStorage.size();) {
    nFound = Storage.Pointers.size() + Storage.MaybeStorage.size();
    Storage.TraverseStmt(Function->getBody());
  }
  AssignedPointerFinder Assignments;
  Assignments.TraverseStmt(Function->getBody());

  Stmt *Body = Function->getBody();
  if (!Body)
    return;
  if (auto *CoroutineBody = dyn_cast<CoroutineBodyStmt>(Body))
    Body = CoroutineBody->getBody();
  CFG::BuildOptions Options;
  Options.setAllAlwaysAdd();
  const std::unique_ptr<CFG> Graph =
      buildGraph(Function, Body, Context, Options);
  // Exceptions can reach handlers after a suspension (addExceptionEdges).
  if (Graph)
    addExceptionEdges(*Graph, Context);
  // Not accepted when its control flow can't be built, like through a long
  // chain of conditionals in default arguments that destroy temporaries.
  if (!Graph) {
    const SourceLocation Loc =
        reportLocation(Cleanup->getBeginLoc(), Context.getSourceManager());
    if (Loc.isValid())
      diag(Loc, "the control flow of the cleanup is too complex to analyze, "
                "so whether it uses what is gone when it runs can't be told; "
                "split up what makes it complex");
    return;
  }
  // Blocks that can't run, like `if (false)` branches, don't change the state.
  const llvm::BitVector Reachable = reachableBlocks(*Graph);
  const auto SuspendPoints = suspendPoints(Body);

  // The references are valid until the first suspension of the cleanup, so
  // whatever it needs after it is copied or moved into locals first. Follow
  // the control flow, so a use in a later iteration of a loop that suspends
  // counts too, and so does a pointer assigned to point into the storage
  // after it was declared.
  // Of each pointer, whether it can point into the storage, and whether it
  // can on a path that has suspended, which is what is gone: not one that
  // points elsewhere where it suspends, `if (_bAsync) { pText = &Copy;
  // co_await fg_Work(); }`, merged with one that points into it where it
  // doesn't.
  struct CState {
    bool Suspended = false;
    llvm::BitVector Pointers;
    llvm::BitVector Stale;

    bool operator!=(const CState &Other) const {
      return Suspended != Other.Suspended || Pointers != Other.Pointers ||
             Stale != Other.Stale;
    }
    void merge(const CState &Other) {
      Suspended |= Other.Suspended;
      Pointers |= Other.Pointers;
      Stale |= Other.Stale;
    }
    void set(unsigned Index, bool Into) {
      Pointers[Index] = Into;
      Stale[Index] = Into && Suspended;
    }
  };
  // A lambda copies the pointers it captures when it is made, so each copy
  // has a bit of its own, which a call of the lambda uses.
  llvm::DenseMap<std::pair<const LambdaExpr *, const ValueDecl *>, unsigned>
      LambdaCopies;
  for (const auto &Nodes : matchEach<LambdaExpr>(
           lambdaExpr().bind("lambda"), *Function->getBody(), Context)) {
    const auto *Inner = Nodes.getNodeAs<LambdaExpr>("lambda");
    for (const LambdaCapture &Capture : Inner->captures()) {
      if (!Capture.capturesVariable())
        continue;
      // A copy of a pointer, or an init capture, like `[pValue = &Value]` or
      // `[&Sub = Value.m_Sub]`, which is initialized where the lambda is made.
      const auto *Variable = dyn_cast<VarDecl>(Capture.getCapturedVar());
      const bool InitCapture = Variable && Variable->isInitCapture() &&
                               Variable->getInit() &&
                               (Variable->getType()->isPointerType() ||
                                Variable->getType()->isReferenceType());
      if (InitCapture ||
          (Capture.getCaptureKind() == LCK_ByCopy &&
           Storage.Pointers.count(
               CleanupStorage::aliasedPointer(Capture.getCapturedVar()))))
        LambdaCopies.try_emplace({Inner, Capture.getCapturedVar()},
                                 Storage.Pointers.size() + LambdaCopies.size());
    }
  }
  const unsigned nPointers = Storage.Pointers.size() + LambdaCopies.size();
  auto Empty = [&] {
    return CState{false, llvm::BitVector(nPointers),
                  llvm::BitVector(nPointers)};
  };
  // Also, in \p Thrown, each state an exception thrown in the block carries
  // to each handler dispatch it reaches: before and after each element in the
  // block of that dispatch's own `try` (inDispatchTry), of nested ones that
  // share the block apart.
  using CThrown = llvm::SmallDenseMap<const CFGBlock *, CState, 2>;
  auto Transfer = [&](const CFGBlock *Block, CState State, auto &&OnUse,
                      CThrown *Thrown = nullptr) {
    llvm::SmallVector<const CFGBlock *, 2> Dispatches;
    if (Thrown)
      for (const CFGBlock::AdjacentBlock &Successor : Block->succs())
        if (const CFGBlock *To = Successor.getReachableBlock();
            To && isHandlerDispatch(To))
          Dispatches.push_back(To);
    llvm::SmallVector<const CFGBlock *, 2> Into;
    auto Carry = [&] {
      for (const CFGBlock *Dispatch : Into)
        Thrown->try_emplace(Dispatch, Empty()).first->second.merge(State);
    };
    for (const CFGElement &Element : *Block) {
      const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>();
      Carry();
      Into.clear();
      for (const CFGBlock *Dispatch : Dispatches)
        if (inDispatchTry(Dispatch, Element, Context))
          Into.push_back(Dispatch);
      Carry();
      if (!Statement)
        continue;
      const Stmt *S = Statement->getStmt();
      // Not one that only names a static member, `Digest.mc_Size`, which
      // doesn't need the object.
      auto NamesStatic = [&](const DeclRefExpr *Ref) {
        DynTypedNode Node = DynTypedNode::create(*Ref);
        for (int Depth = 0; Depth < 8; ++Depth) {
          const auto Parents = instantiatedParents(Node, Context);
          if (Parents.empty())
            return false;
          if (Parents[0].get<ImplicitCastExpr>() ||
              Parents[0].get<ParenExpr>()) {
            Node = Parents[0];
            continue;
          }
          const auto *Member = Parents[0].get<MemberExpr>();
          if (!Member)
            return false;
          const ValueDecl *Named = Member->getMemberDecl();
          const auto *Method = dyn_cast<CXXMethodDecl>(Named);
          return (isa<VarDecl>(Named) &&
                  cast<VarDecl>(Named)->isStaticDataMember()) ||
                 (Method && Method->isStatic()) || isa<EnumConstantDecl>(Named);
        }
        return false;
      };
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
          Ref && State.Suspended && !NamesStatic(Ref)) {
        auto It = Storage.Pointers.find(
            CleanupStorage::aliasedPointer(Ref->getDecl()));
        if (Storage.Storage.contains(Ref->getDecl()) ||
            (It != Storage.Pointers.end() && State.Stale.test(It->second) &&
             !Assignments.Assigned.contains(Ref)))
          OnUse(Ref->getBeginLoc(), Ref->getDecl(), Ref);
      }
      // A lambda that captures the storage by reference and is called after
      // the suspension, like `fRead()` or `fRead.operator()()` with `auto
      // fRead = [&Value] { ... }`, uses it then, also through the local
      // lambdas it captures, which it can call, `[&fRead] { return fRead();
      // }`.
      llvm::SmallPtrSet<const LambdaExpr *, 4> Checked;
      TCRecursive<void(const LambdaExpr *)> CheckInvoked =
          [&](const LambdaExpr *Invoked) {
            if (!Invoked || !Checked.insert(Invoked).second)
              return;
            const auto CaptureInits = captureInitsOf(Invoked);
            auto Init = CaptureInits.begin();
            for (const LambdaCapture &Capture : Invoked->captures()) {
              const Expr *CaptureInit = *Init++;
              if (!Capture.capturesVariable())
                continue;
              auto Copy =
                  LambdaCopies.find({Invoked, Capture.getCapturedVar()});
              const auto *Variable =
                  dyn_cast<VarDecl>(Capture.getCapturedVar());
              if (Variable && Variable->isInitCapture()) {
                if (Copy != LambdaCopies.end() &&
                    State.Stale.test(Copy->second))
                  OnUse(Capture.getLocation(), Variable, nullptr);
                continue;
              }
              if (Variable && Variable->hasLocalStorage() &&
                  Variable->getInit())
                CheckInvoked(dyn_cast<LambdaExpr>(
                    CleanupStorage::withoutCleanups(Variable->getInit())
                        ->IgnoreParenImpCasts()));
              const auto *Captured = dyn_cast_or_null<DeclRefExpr>(
                  CaptureInit ? CaptureInit->IgnoreParenImpCasts() : nullptr);
              if (!Captured)
                continue;
              // A reference to the storage, a copy of a pointer into it, or a
              // reference to such a pointer, which points where it does now:
              // used where the lambda's body uses what it captures, in the
              // context there (reportLifetime).
              auto Pointer = Storage.Pointers.find(
                  CleanupStorage::aliasedPointer(Captured->getDecl()));
              if ((Capture.getCaptureKind() == LCK_ByRef &&
                   (Storage.Storage.contains(Captured->getDecl()) ||
                    (Pointer != Storage.Pointers.end() &&
                     State.Stale.test(Pointer->second)))) ||
                  (Copy != LambdaCopies.end() &&
                   State.Stale.test(Copy->second)))
                forEachStmtOnce(Invoked->getBody(), [&](const Stmt *Each) {
                  if (const auto *Ref = dyn_cast<DeclRefExpr>(Each);
                      Ref && Ref->getDecl() == Capture.getCapturedVar())
                    OnUse(Captured->getBeginLoc(), Captured->getDecl(), Ref);
                  return EWalk::Descend;
                });
            }
          };
      if (const auto *Call = dyn_cast<CallExpr>(S); Call && State.Suspended)
        CheckInvoked(calledLambda(Call));
      // What a call or subscript after the suspension uses that is evaluated
      // before it, the object of a member function, `Value.f_Read(co_await
      // fg_Work())`, what is given by reference or pointed to by a pointer
      // given, `fg_Read(&Value, co_await fg_Work())`, and the array or
      // pointer indexed, `Values[(co_await fg_Work(), 0)]`.
      if (State.Suspended) {
        llvm::SmallVector<const Expr *, 4> Early;
        llvm::SmallVector<const DeclRefExpr *, 2> EarlyPointers;
        if (const CMemberCall Member = memberCallOf(S))
          Early.push_back(Member->getImplicitObjectArgument());
        else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(S))
          Early.push_back(Subscript->getBase());
        // Also of a constructor, `CRead{Value, co_await fg_Work()}`, and
        // through a pointer to a function.
        const auto *Call = dyn_cast<CallExpr>(S);
        const auto *Construct = dyn_cast<CXXConstructExpr>(S);
        if (Call || Construct) {
          const auto *Called = cast<Expr>(S);
          const unsigned nArguments =
              Call ? Call->getNumArgs() : Construct->getNumArgs();
          auto ArgumentAt = [&](unsigned iArgument) {
            return Call ? Call->getArg(iArgument)
                        : Construct->getArg(iArgument);
          };
          const auto *Method =
              Call ? dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee())
                   : nullptr;
          if (isa_and_nonnull<CXXOperatorCallExpr>(Call) && Method &&
              !Method->isExplicitObjectMemberFunction() && nArguments >= 1)
            Early.push_back(Call->getArg(0));
          for (unsigned iArgument = 0; iArgument < nArguments; ++iArgument) {
            const std::optional<QualType> Parameter =
                argumentParameterType(Called, iArgument);
            if (!Parameter)
              continue;
            const QualType Type = *Parameter;
            const Expr *Argument = ArgumentAt(iArgument);
            if (Type->isReferenceType()) {
              Early.push_back(Argument);
              continue;
            }
            if (!Type->isPointerType())
              continue;
            // Also cast, `static_cast<int const *>(&Value)`.
            Argument = Argument->IgnoreParenCasts();
            if (const Expr *Addressed = addressedBy(Argument))
              Early.push_back(Addressed);
            else if (const auto *Ref = dyn_cast<DeclRefExpr>(Argument))
              EarlyPointers.push_back(Ref);
          }
        }
        // A pointer is read before, and what it points to used after.
        for (const DeclRefExpr *Ref : EarlyPointers) {
          auto It = Storage.Pointers.find(
              CleanupStorage::aliasedPointer(Ref->getDecl()));
          if (It != Storage.Pointers.end() && State.Stale.test(It->second) &&
              !Assignments.Assigned.contains(Ref))
            OnUse(Ref->getBeginLoc(), Ref->getDecl(), Ref);
        }
        // Also each that a conditional can choose, `_bFirst ? First :
        // Second`, or the one a constant condition chooses.
        llvm::SmallVector<const Expr *, 8> Used(Early.begin(), Early.end());
        for (size_t Index = 0; Index < Used.size() && Index < 256; ++Index) {
          const Expr *E = Used[Index];
          if (!E)
            continue;
          // Also through what a pointer to it points to, `(&Value)[0]` or
          // `(*pValue)`.
          E = E->IgnoreParenImpCasts();
          for (int Depth = 0; Depth < 16; ++Depth)
            if (const auto *Member = dyn_cast<MemberExpr>(E);
                Member && !Member->isArrow())
              E = Member->getBase()->IgnoreParenImpCasts();
            else if (const auto *Unary = dyn_cast<UnaryOperator>(E);
                     Unary && (Unary->getOpcode() == UO_Deref ||
                               Unary->getOpcode() == UO_AddrOf))
              E = Unary->getSubExpr()->IgnoreParenImpCasts();
            else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E))
              E = Subscript->getBase()->IgnoreParenImpCasts();
            else
              break;
          if (const auto *Member = dyn_cast<MemberExpr>(E))
            E = Member->getBase()->IgnoreParenImpCasts();
          if (const auto *Conditional =
                  dyn_cast<AbstractConditionalOperator>(E)) {
            const auto *Binary = dyn_cast<BinaryConditionalOperator>(E);
            const Expr *True =
                Binary ? Binary->getCommon() : Conditional->getTrueExpr();
            bool Chosen = false;
            if (!Conditional->getCond()->isValueDependent() &&
                foldsCondition(Conditional->getCond(), Chosen, Context))
              Used.push_back(Chosen ? True : Conditional->getFalseExpr());
            else
              Used.append({True, Conditional->getFalseExpr()});
            continue;
          }
          const auto *Ref = dyn_cast<DeclRefExpr>(E);
          if (!Ref)
            continue;
          auto It = Storage.Pointers.find(
              CleanupStorage::aliasedPointer(Ref->getDecl()));
          if (Storage.Storage.contains(Ref->getDecl()) ||
              (It != Storage.Pointers.end() && State.Stale.test(It->second) &&
               !Assignments.Assigned.contains(Ref)))
            OnUse(Ref->getBeginLoc(), Ref->getDecl(), Ref);
        }
      }
      if (const auto *Made = dyn_cast<LambdaExpr>(S)) {
        for (const auto &[Key, Index] : LambdaCopies) {
          if (Key.first != Made)
            continue;
          const auto *Variable = dyn_cast<VarDecl>(Key.second);
          if (Variable && Variable->isInitCapture()) {
            State.set(Index,
                      Variable->getType()->isReferenceType()
                          ? Storage.isInStorage(CleanupStorage::withoutCleanups(
                                                    Variable->getInit()),
                                                &State.Pointers)
                          : Storage.pointsIntoStorage(Variable->getInit(),
                                                      &State.Pointers));
          } else {
            // A copy is gone where what it copies is.
            const unsigned Copied = Storage.Pointers.lookup(
                CleanupStorage::aliasedPointer(Key.second));
            State.Pointers[Index] = State.Pointers.test(Copied);
            State.Stale[Index] = State.Stale.test(Copied);
          }
        }
      }
      if (const auto *Declaration = dyn_cast<DeclStmt>(S)) {
        // A reference bound points where it is bound, gone if that is.
        const llvm::BitVector Before = State.Pointers;
        Storage.bindReferences(Declaration, State.Pointers);
        for (unsigned Index = 0; Index < nPointers; ++Index)
          if (State.Pointers[Index] != Before[Index])
            State.Stale[Index] = State.Pointers[Index] && State.Suspended;
        for (const Decl *D : Declaration->decls()) {
          const auto *Variable = dyn_cast<VarDecl>(D);
          auto It = Variable ? Storage.Pointers.find(Variable)
                             : Storage.Pointers.end();
          if (It != Storage.Pointers.end())
            State.set(It->second,
                      Variable->getInit() &&
                          Storage.pointsIntoStorage(Variable->getInit(),
                                                    &State.Pointers));
        }
      } else if (const auto *Binary = dyn_cast<BinaryOperator>(S)) {
        // Either of a conditional may be assigned, and keep what it held.
        const llvm::SmallVector<const DeclRefExpr *, 2> Assigned =
            CleanupStorage::assignedPointers(Binary);
        const bool Into =
            !Assigned.empty() &&
            Storage.pointsIntoStorage(Binary->getRHS(), &State.Pointers);
        for (const DeclRefExpr *Ref : Assigned)
          if (auto It = Storage.Pointers.find(
                  CleanupStorage::aliasedPointer(Ref->getDecl()));
              It != Storage.Pointers.end()) {
            // Either of a conditional keeps what it was, also gone.
            const bool Kept = Assigned.size() > 1;
            const bool Stale = Kept && State.Stale[It->second];
            State.set(It->second, Into || (Kept && State.Pointers[It->second]));
            State.Stale[It->second] = State.Stale[It->second] || Stale;
          }
      }
      if (isSuspension(S) || SuspendPoints.contains(S)) {
        State.Suspended = true;
        State.Stale |= State.Pointers;
      }
    }
    Carry();
    return State;
  };
  llvm::DenseMap<const CFGBlock *, CState> StateAtEnd;
  // Of the blocks with a handler after them, the states that exceptions
  // thrown in them carry to each dispatch.
  llvm::DenseMap<std::pair<const CFGBlock *, const CFGBlock *>, CState>
      StateThrown;
  auto Entry = [&](const CFGBlock *Block) {
    CState State = Empty();
    for (const CFGBlock::AdjacentBlock &Predecessor : Block->preds()) {
      if (const CFGBlock *Pred = Predecessor.getReachableBlock()) {
        if (!isHandlerDispatch(Block)) {
          if (auto It = StateAtEnd.find(Pred); It != StateAtEnd.end())
            State.merge(It->second);
        } else if (auto It = StateThrown.find({Pred, Block});
                   It != StateThrown.end())
          State.merge(It->second);
      }
    }
    return State;
  };
  // Bits and references bound into the storage are only ever added, so this
  // ends.
  const llvm::SmallVector<const CFGBlock *, 16> Order = forwardOrder(*Graph);
  for (bool Changed = true; Changed;) {
    const size_t nBound = Storage.Storage.size();
    Changed = false;
    for (const CFGBlock *Block : Order) {
      if (!Reachable.test(Block->getBlockID()))
        continue;
      const bool Throws = llvm::any_of(
          Block->succs(), [](const CFGBlock::AdjacentBlock &Successor) {
            return isHandlerDispatch(Successor.getReachableBlock());
          });
      CThrown Thrown;
      const CState End = Transfer(
          Block, Entry(Block),
          [](SourceLocation, const ValueDecl *, const Expr *) {},
          Throws ? &Thrown : nullptr);
      auto MergeInto = [&](CState &Into, const CState &Value, bool Inserted) {
        CState Merged = Into;
        Merged.merge(Value);
        if (Inserted || Merged != Into) {
          Into = Merged;
          Changed = true;
        }
      };
      {
        auto [It, Inserted] = StateAtEnd.try_emplace(Block, Empty());
        MergeInto(It->second, End, Inserted);
      }
      for (const auto &[Dispatch, Value] : Thrown) {
        auto [It, Inserted] =
            StateThrown.try_emplace({Block, Dispatch}, Empty());
        MergeInto(It->second, Value, Inserted);
      }
    }
    if (Storage.Storage.size() != nBound)
      Changed = true;
  }

  const SourceManager &SM = Context.getSourceManager();
  llvm::SmallPtrSet<const ValueDecl *, 4> Reported;
  for (const CFGBlock *Block : *Graph) {
    if (!Reachable.test(Block->getBlockID()))
      continue;
    Transfer(Block, Entry(Block),
             [&](SourceLocation UseLoc, const ValueDecl *Used,
                 const Expr *Use) {
               // Not where what the use uses is alive by what the cleanup
               // holds there (Rule K, reportLifetime).
               if (Reported.contains(Used))
                 return;
               CLifetimeUse Of;
               Of.Use = Use;
               auto Arguments = [&](const MalterlibDiagnostic &Diag) {
                 Diag << Used;
               };
               if (reportLifetime(
                       *this, Of, Context,
                       {reportLocation(UseLoc, SM),
                        "the cleanup function of 'fg_AsyncDestroy' uses %0, "
                        "which it captures by reference, after its first "
                        "suspension, where the reference is gone; copy or "
                        "move it into a local before the suspension",
                        Arguments}))
                 Reported.insert(Used);
             });
  }
}

namespace {

/// Finds calls of member functions named `f_Destroy`.
class DestroyFinder : public OncePerTypeVisitor<DestroyFinder> {
public:
  explicit DestroyFinder(const ASTContext &Context) : Context(Context) {}

  const Expr *Found = nullptr;
  const ASTContext &Context;

  // Not a branch of an `if constexpr` that is discarded, nor one that a
  // constant condition never takes, `if (false)`, unless a jump can reach it.
  bool TraverseIfStmt(IfStmt *If) {
    if (If->isConstexpr() && !If->getCond()->isValueDependent())
      if (const std::optional<Stmt *> Case = nondiscardedCaseOf(If)) {
        return TraverseStmt(If->getInit()) &&
               TraverseStmt(If->getConditionVariableDeclStmt()) &&
               TraverseStmt(If->getCond()) && TraverseStmt(*Case);
      }
    bool Taken = false;
    if (!If->isConstexpr() && !If->isConsteval() &&
        !If->getConditionVariable() && !If->getCond()->isValueDependent() &&
        foldsCondition(If->getCond(), Taken, Context) &&
        !hasJumpTarget(Taken ? If->getElse() : If->getThen()))
      return TraverseStmt(If->getInit()) && TraverseStmt(If->getCond()) &&
             TraverseStmt(Taken ? If->getThen() : If->getElse());
    return RecursiveASTVisitor::TraverseIfStmt(If);
  }

  // Nor what comes after what always leaves, `if constexpr (!c_bNeedsDestroy)
  // return;`, unless a jump can reach it.
  bool TraverseCompoundStmt(CompoundStmt *Compound) {
    bool Left = false;
    for (Stmt *Statement : Compound->body()) {
      if (Left && !hasJumpTarget(Statement))
        continue;
      Left = false;
      if (!TraverseStmt(Statement))
        return false;
      Left = alwaysLeaves(Statement, Context);
    }
    return true;
  }

  // Not what isn't evaluated, `sizeof(Resource.f_Destroy())`.
  bool TraverseUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *) {
    return true;
  }
  bool TraverseCXXNoexceptExpr(CXXNoexceptExpr *) { return true; }
  bool TraverseCXXTypeidExpr(CXXTypeidExpr *Typeid) {
    return !Typeid->isPotentiallyEvaluated() ||
           RecursiveASTVisitor::TraverseCXXTypeidExpr(Typeid);
  }
  // Nor a requirement, `requires { Resource.f_Destroy(); }`, which only
  // tells whether it is well-formed.
  bool TraverseRequiresExpr(RequiresExpr *) { return true; }
  // Nor what is in a type, `decltype(Resource.f_Destroy())`.
  bool TraverseDecltypeTypeLoc(DecltypeTypeLoc, bool = true) { return true; }
  bool TraverseTypeOfExprTypeLoc(TypeOfExprTypeLoc, bool = true) {
    return true;
  }
  // Nor a lambda that is made but not called where it is, whose body runs
  // only when it is called.
  bool TraverseLambdaExpr(LambdaExpr *Lambda) {
    for (Expr *Init : Lambda->capture_inits())
      if (!TraverseStmt(Init))
        return false;
    return true;
  }
  // One that is called runs its body, also held in a local, `fDestroy()`,
  // or called through its call operator by name, `[&] { ... }.operator()()`,
  // each once.
  // Also one a scope exit made in it runs, `auto Inner = g_OnScopeExit / [&]
  // { ... };`, where the scope ends.
  bool TraverseCXXOperatorCallExpr(CXXOperatorCallExpr *Call) {
    if (Call->getOperator() == OO_Call && Call->getNumArgs() >= 1)
      if (!traverseCalled(calledLambda(Call->getArg(0))))
        return false;
    if (Call->getOperator() == OO_Slash && Call->getNumArgs() == 2 &&
        isScopeExitHelper(skipWrappers(Call->getArg(0)), /*AlsoActor=*/false))
      if (!traverseCalled(asLambda(Call->getArg(1))))
        return false;
    return RecursiveASTVisitor::TraverseCXXOperatorCallExpr(Call);
  }
  // Nor the value of a conditional that a constant condition doesn't choose,
  // `t_bOwnCallback ? fg_Move(_Callback).f_Destroy() : TCFuture<int>{}`.
  bool TraverseConditionalOperator(ConditionalOperator *Conditional) {
    bool Taken = false;
    if (!Conditional->getCond()->isValueDependent() &&
        foldsCondition(Conditional->getCond(), Taken, Context))
      return TraverseStmt(Conditional->getCond()) &&
             TraverseStmt(Taken ? Conditional->getTrueExpr()
                                : Conditional->getFalseExpr());
    return RecursiveASTVisitor::TraverseConditionalOperator(Conditional);
  }
  // Also a member function whose body can be seen, `Subscription.
  // f_TryDestroy()` of `fg_Move(m_Callback).f_Destroy();`, but not a virtual
  // one, which an override can replace, nor an `f_Destroy` that returns
  // nothing, which is synchronous whatever it starts, each once.
  bool TraverseCXXMemberCallExpr(CXXMemberCallExpr *Call) {
    return traverseMemberCall(memberCallOf(Call)) &&
           RecursiveASTVisitor::TraverseCXXMemberCallExpr(Call);
  }
  bool traverseMemberCall(CMemberCall Call) {
    if (const CXXMethodDecl *Method = Call->getMethodDecl();
        Method && Method->getOverloadedOperator() == OO_Call &&
        Call->getImplicitObjectArgument())
      if (!traverseCalled(calledLambda(Call->getImplicitObjectArgument())))
        return false;
    if (const CXXMethodDecl *Method = Call->getMethodDecl();
        Method && Method->getOverloadedOperator() == OO_None &&
        !(Method->getIdentifier() && identifierTextOf(Method) == "f_Destroy") &&
        !callsOverride(Call))
      if (!traverseFunction(Method))
        return false;
    return true;
  }
  // Also one `std::invoke` or `fg_CallSafe` calls, `std::invoke([&] { ... })`,
  // and a function
  // whose body can be seen, which tears down what it is given,
  // `fg_DestroySubscription(Resource)` of `return _Resource.f_Destroy();`,
  // also a static member function, `CSubscription::fs_Destroy(Callback)`,
  // each once.
  bool TraverseCallExpr(CallExpr *Call) {
    // A member call with an explicit object, as one with an implicit one.
    if (const CMemberCall Member = memberCallOf(Call);
        Member && !traverseMemberCall(Member))
      return false;
    if (isStdInvoke(Call)) {
      if (!traverseCalled(calledLambda(Call->getArg(0))))
        return false;
    } else if (calleeName(Call) == "fg_OnScopeExit" &&
               Call->getNumArgs() >= 1) {
      if (!traverseCalled(asLambda(Call->getArg(0))))
        return false;
    } else if (calleeName(Call) == "fg_CallSafe" && Call->getNumArgs() >= 1) {
      // Also what `fg_CallSafe` calls right away, `fg_CallSafe([...]() ->
      // TCFuture<int> { co_await fg_Move(Actor).f_Destroy(); ... })`.
      if (!traverseCalled(asLambda(Call->getArg(0))))
        return false;
    } else if (const FunctionDecl *Callee = Call->getDirectCallee();
               Callee && (!isa<CXXMethodDecl>(Callee) ||
                          cast<CXXMethodDecl>(Callee)->isStatic()))
      if (!traverseFunction(Callee))
        return false;
    return RecursiveASTVisitor::TraverseCallExpr(Call);
  }
  bool traverseCalled(const LambdaExpr *Lambda) {
    if (!Lambda || !Called.insert(Lambda).second)
      return true;
    return TraverseStmt(Lambda->getBody());
  }
  bool traverseFunction(const FunctionDecl *Function) {
    const FunctionDecl *Definition = Function->getDefinition();
    if (!Definition || !Definition->getBody() ||
        !CalledFunctions.insert(Definition).second ||
        CalledFunctions.size() > 64)
      return true;
    return TraverseStmt(Definition->getBody());
  }
  llvm::SmallPtrSet<const LambdaExpr *, 4> Called;
  llvm::SmallPtrSet<const FunctionDecl *, 4> CalledFunctions;

  // A synchronous `f_Destroy` that returns nothing is not asynchronous
  // teardown.
  bool visitMemberCall(CMemberCall Call) {
    const CXXMethodDecl *Method = Call->getMethodDecl();
    // Also through a pointer to it, `(Resource.*fDestroy)()` with an
    // unchanged local `auto fDestroy = &CResource::f_Destroy`.
    if (!Method)
      if (const auto *Through =
              dyn_cast<BinaryOperator>(Call->getCallee()->IgnoreParens());
          Through && Through->isPtrMemOp()) {
        const Expr *Pointer = Through->getRHS()->IgnoreParenImpCasts();
        for (int Depth = 0; Depth < 8; ++Depth) {
          const auto *Ref = dyn_cast<DeclRefExpr>(Pointer);
          const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
              !Local->getInit() || isChangedVariable(Local))
            break;
          Pointer = Local->getInit()->IgnoreParenImpCasts();
        }
        if (const auto *Address = dyn_cast<UnaryOperator>(Pointer);
            Address && Address->getOpcode() == UO_AddrOf)
          if (const auto *Named = dyn_cast<DeclRefExpr>(
                  Address->getSubExpr()->IgnoreParenImpCasts()))
            Method = dyn_cast<CXXMethodDecl>(Named->getDecl());
      }
    if (Method && Method->getIdentifier() && identifierTextOf(Method) == "f_Destroy" &&
        !Call->getType()->isVoidType())
      Found = Call;
    return !Found;
  }
  // Each call, also as a member call in each of its forms (CMemberCall).
  bool VisitCallExpr(CallExpr *Call) {
    if (!visitCall(Call))
      return false;
    const CMemberCall Member = memberCallOf(Call);
    return !Member || visitMemberCall(Member);
  }
  // Also one with an explicit object parameter, `f_Destroy(this CResource
  // &&_Self)`, called as `fg_Move(Resource).f_Destroy()`.
  bool visitCall(CallExpr *Call) {
    if (const auto *Method =
            dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
        Method && Method->isExplicitObjectMemberFunction() &&
        Method->getIdentifier() && identifierTextOf(Method) == "f_Destroy" &&
        !Call->getType()->isVoidType())
      Found = Call;
    return !Found;
  }
};

} // namespace

void AsyncDestroyCheck::checkScopeExit(const Expr *Divide, const Expr *Left,
                                       const Expr *Right,
                                       ASTContext &Context) {
  // Without a helper, made by `fg_OnScopeExit`.
  if (Left && !isScopeExitHelper(skipWrappers(Left), /*AlsoActor=*/false))
    return;
  // Also kept in a local before, `g_OnScopeExit / fg_Move(fCleanup)`.
  const LambdaExpr *Lambda = asLambda(Right);
  if (!Lambda)
    return;
  DestroyFinder Finder(Context);
  for (const FunctionDecl *Body : sharedInstantiationIndex().bodiesOf(
           Lambda->getCallOperator(), Context))
    Finder.TraverseStmt(Body->getBody());
  if (!Finder.Found)
    return;
  // Only a coroutine can await the asynchronous teardown.
  const FunctionDecl *Function =
      enclosingFunction(DynTypedNode::create(*Divide), Context);
  if (!Function || !isCoroutine(Function))
    return;
  const SourceLocation Loc =
      reportLocation(Divide->getBeginLoc(), Context.getSourceManager());
  if (Loc.isInvalid())
    return;
  // What makes the guard: the helper, also the shared one,
  // `g_OnScopeExitShared`, or the function.
  StringRef Used = "g_OnScopeExit";
  if (Left) {
    const CXXRecordDecl *Helper = skipWrappers(Left)
                                      ->getType()
                                      .getNonReferenceType()
                                      ->getAsCXXRecordDecl();
    if (Helper && Helper->getIdentifier() &&
        identifierTextOf(Helper).contains("Shared"))
      Used = "g_OnScopeExitShared";
  } else if (const auto *Call = dyn_cast<CallExpr>(Divide);
             Call && Call->getDirectCallee() &&
             Call->getDirectCallee()->getIdentifier())
    Used = identifierTextOf(Call->getDirectCallee());
  else
    Used = "fg_OnScopeExit";
  diag(Loc, "the teardown of an actor or functor in a coroutine uses "
            "'fg_AsyncDestroy' instead of '%0'")
      << Used;
}

} // namespace clang::tidy::malterlib
