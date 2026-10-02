//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CoroutineUnsafeThisCheck.h"
#include "ActorAnnotations.h"
#include "RunsToCompletion.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Analysis/CFG.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

void CoroutineUnsafeThisCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(cxxMethodDecl(isDefinition()).bind("method"), this);
  Finder->addMatcher(lambdaExpr().bind("lambda"), this);
}

void CoroutineUnsafeThisCheck::checkMethod(const CXXMethodDecl *Method,
                                           ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  if (Method->isStatic() || isLambdaCallOperator(Method))
    return;
  if (!isProtectedCoroutine(Method))
    return;
  const CXXRecordDecl *Record = Method->getParent();
  if (isKnownSafeThisClass(Record))
    return;
  // A member function is called directly, so it runs while the caller has
  // the object until it first suspends. Moving the object into the
  // coroutine with `co_await fg_MoveThis(*this)` before that is safe too.
  llvm::SmallVector<const CallExpr *, 2> MovedThis;
  usesThis(Method->getBody(), &MovedThis);
  // Only one whose result a variable of the class is made of, `auto This =
  // co_await fg_MoveThis(*this)`, moves the object: the result is a reference
  // to it, which one bound to it, `auto &&This = ...`, still refers to, and so
  // does a view made of it, `std::span<int const> View = ...`.
  llvm::erase_if(MovedThis, [&](const CallExpr *Call) {
    DynTypedNode Node = DynTypedNode::create(*Call);
    bool Awaited = false;
    for (int Depth = 0; Depth < 16; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return true;
      const Expr *Child = Node.get<Expr>();
      Node = Parents[0];
      // Either value of a conditional, `bChoose ? co_await fg_MoveThis(*this)
      // : co_await fg_MoveThis(*this)`, not its condition.
      if (const auto *Conditional = Node.get<ConditionalOperator>();
          Conditional && Conditional->getCond() != Child)
        continue;
      if (const auto *Variable = Node.get<VarDecl>()) {
        const CXXRecordDecl *Made = Variable->getType()->getAsCXXRecordDecl();
        return !Awaited || Variable->getType()->isReferenceType() || !Made ||
               Made->getCanonicalDecl() != Record->getCanonicalDecl();
      }
      if (Node.get<CoawaitExpr>()) {
        Awaited = true;
        continue;
      }
      if (!Node.get<ImplicitCastExpr>() && !Node.get<ParenExpr>() &&
          !Node.get<MaterializeTemporaryExpr>() &&
          !Node.get<CXXBindTemporaryExpr>() && !Node.get<ExprWithCleanups>() &&
          !Node.get<CXXConstructExpr>() && !Node.get<OpaqueValueExpr>())
        return true;
    }
    return true;
  });
  // Only those that can run: not in a discarded branch of an `if constexpr`,
  // which isn't in the flow, nor one a constant condition skips, `if
  // (false)`.
  {
    CFG::BuildOptions Options;
    Options.PruneTriviallyFalseEdges = true;
    if (std::unique_ptr<CFG> Graph = CFG::buildCFG(
            Method, const_cast<Stmt *>(Method->getBody()), &Context, Options)) {
      const llvm::BitVector Reachable = reachableBlocks(*Graph);
      llvm::DenseSet<const Stmt *> Runs;
      for (const CFGBlock *Block : *Graph)
        if (Block && Reachable.test(Block->getBlockID()))
          for (const CFGElement &Element : *Block)
            if (const std::optional<CFGStmt> Statement =
                    Element.getAs<CFGStmt>())
              Runs.insert(Statement->getStmt());
      llvm::erase_if(MovedThis, [&](const CallExpr *Call) {
        return !Runs.contains(Call);
      });
    }
  }
  // Not `this` in the captures of a lambda that keeps the object alive,
  // `[this, pThis = TCSharedPointer<CState>{fg_Explicit(this)}]`, whose own
  // check tells where it runs.
  auto CapturedKept = [&](const Stmt *S) {
    DynTypedNode Node = DynTypedNode::create(*S);
    for (int Depth = 0; Depth < 16; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return false;
      // The initializer of an init capture is the variable's and the
      // lambda's.
      for (const DynTypedNode &Parent : Parents)
        if (const auto *Lambda = Parent.get<LambdaExpr>())
          return Node.get<Stmt>() != Lambda->getBody() &&
                 keepsObjectAlive(Lambda);
      const auto *Variable = Parents[0].get<VarDecl>();
      if (!Parents[0].get<Expr>() && !(Variable && Variable->isInitCapture()))
        return false;
      Node = Parents[0];
    }
    return false;
  };
  auto IsThis = [&](const Stmt *S) {
    return isa<CXXThisExpr>(S) &&
           llvm::none_of(MovedThis,
                         [&](const CallExpr *Call) {
                           return Call->getSourceRange().fullyContains(
                               S->getSourceRange());
                         }) &&
           !CapturedKept(S);
  };
  auto IsView = [&](QualType Type) {
    return isDefaultViewType(Type, Context);
  };
  BorrowRoots Roots{IsThis, /*PointerValues=*/true, IsView};
  // The object of a member function that runs to completion is kept by its
  // callers until they join it, like a reference parameter: only its result
  // can keep it longer.
  Roots.Joined = isJoinedByCallers(Method);
  const Stmt *Use = firstUseAfterSuspension(Method, IsThis, Context, &Roots);
  if (!Use && !MovedThis.empty() && !Roots.Joined) {
    const llvm::DenseSet<const Stmt *> After =
        statementsAfterSuspension(Method, Context);
    for (const CallExpr *Call : MovedThis) {
      if (!isAwaited(Call, Context) || After.contains(Call)) {
        Use = Call;
        break;
      }
    }
  }
  if (!Use)
    return;
  const SourceLocation Loc = reportLocation(Method->getLocation(), SM);
  if (Loc.isInvalid())
    return;
  diag(Loc, "TCFuture coroutine %0 is a member of %1, which is not an "
            "actor, and uses its object after a suspension, when it may be "
            "gone; derive %1 from CActor or CAllowUnsafeThis, or return a "
            "TCUnsafeFuture")
      << diagnosticDecl(Method) << diagnosticDecl(Record);
  diag(Use->getBeginLoc(), "the object is used here after a suspension, or in a way that keeps a reference to it",
       DiagnosticIDs::Note);
}

void CoroutineUnsafeThisCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  ASTContext &Context = *Result.Context;

  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("method")) {
    if (!Method->isTemplated()) {
      checkMethod(Method, Context);
      return;
    }
    for (const FunctionDecl *Instance :
         Instantiations.instantiationsOf(Method, Context))
      if (const auto *InstanceMethod = dyn_cast<CXXMethodDecl>(Instance)) {
        const InstantiationScope Scope(*this, Instance);
        checkMethod(InstanceMethod, Context);
      }
    return;
  }

  const auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda");
  if (!Lambda->getCallOperator()->isTemplated()) {
    checkLambda(Lambda, Lambda->getCallOperator(), Context);
    return;
  }
  for (const FunctionDecl *Instance :
       Instantiations.instantiationsOf(Lambda->getCallOperator(), Context))
    if (const auto *Operator = dyn_cast<CXXMethodDecl>(Instance))
      if (const LambdaExpr *InstanceLambda =
              Instantiations.lambdaOf(Instance, Context)) {
        const InstantiationScope Scope(*this, Instance);
        checkLambda(InstanceLambda, Operator, Context);
      }
}

void CoroutineUnsafeThisCheck::checkLambda(const LambdaExpr *Lambda,
                                           const CXXMethodDecl *Operator,
                                           ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  // Not one that is never made, in what isn't evaluated or never runs, `if
  // constexpr (false)`.
  if (!isProtectedCoroutine(Operator) || isNeverRun(Lambda, Context))
    return;
  // The captures that point to the object: `this`, `[pThis = this]` and
  // `[pThis]` with `auto *pThis = this`. A copy of the object, `[*this]`,
  // lives in the closure.
  llvm::SmallPtrSet<const ValueDecl *, 2> Aliases;
  const LambdaCapture *First = nullptr;
  bool CapturesThis = false;
  for (const LambdaCapture &Capture : Lambda->captures()) {
    bool PointsToObject = false;
    if (Capture.capturesThis() && Capture.getCaptureKind() != LCK_StarThis) {
      PointsToObject = CapturesThis = true;
    } else if (Capture.capturesVariable()) {
      if (const auto *Variable = dyn_cast<VarDecl>(Capture.getCapturedVar());
          Variable && isCapturedAliasOfThis(Variable, /*OwnObject=*/false)) {
        Aliases.insert(Variable);
        PointsToObject = true;
      }
    }
    if (PointsToObject && !First)
      First = &Capture;
  }
  // A lambda that a member function that runs to completion starts and joins
  // runs while the object is kept for that function.
  if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(
          enclosingPhaseFunction(Lambda, Context));
      Method && !Method->isStatic() && isJoinedByCallers(Method) &&
      !isKnownSafeThisClass(Method->getParent()) &&
      lambdaJoin(Lambda, Context, nullptr) &&
      inRunsToCompletionContext(Lambda, Context)) {
    // Except what its result keeps of the object, like `co_return &m_Value`,
    // which outlives the join.
    // Also through a capture of a pointer to it, `[pThis = this]`.
    auto IsThis = [&](const Stmt *S) {
      if (isa<CXXThisExpr>(S))
        return true;
      const auto *Ref = dyn_cast<DeclRefExpr>(S);
      return Ref && Aliases.contains(Ref->getDecl());
    };
    auto IsView = [&](QualType Type) { return isDefaultViewType(Type, Context); };
    BorrowRoots Roots{IsThis, /*PointerValues=*/true, IsView};
    Roots.Joined = true;
    const Stmt *Use = firstUseAfterSuspension(Operator, IsThis,
                                              Context, &Roots);
    if (!Use)
      return;
    const SourceLocation Loc = reportLocationIn(Lambda->getBeginLoc(), Lambda->getEndLoc(), SM);
    if (Loc.isInvalid())
      return;
    diag(Loc, "a TCFuture coroutine lambda that %0, which runs to "
              "completion, starts and joins returns a pointer or reference "
              "into the object of %1, which can outlive the join; return a "
              "value instead")
        << diagnosticDecl(Method) << diagnosticDecl(Method->getParent());
    diag(Use->getBeginLoc(), "the object is kept here by the result",
         DiagnosticIDs::Note);
    return;
  }
  if (!First)
    return;
  const CXXRecordDecl *Record = enclosingThisClass(Lambda);
  if (!Record)
    return;
  if (isKnownSafeThisClass(Record))
    return;
  // A shared pointer to the object in the closure keeps it alive while the
  // closure is, which a direct call of a temporary closure doesn't.
  llvm::SmallVector<ClosureConsumer, 2> Consumers =
      closureConsumers(Lambda, Context, vocabulary().SafeCallees.names());
  giveUnlistedCallSafe(Consumers, vocabulary().SafeCallees.names());
  // But not what its result keeps of the object, `co_return &m_Value`, which
  // outlives the closure.
  const bool KeptAlive =
      keepsObjectAlive(Lambda) && !Consumers.empty() &&
      llvm::all_of(Consumers, [](const ClosureConsumer &Consumer) {
        return Consumer.Use == ClosureUse::SafeCallee ||
               Consumer.Use == ClosureUse::CallSafe;
      });
  // Otherwise the object must live while the function that makes the lambda
  // runs, through the lambdas around it.
  if (!KeptAlive && !isObjectAliveWhileRunning(Lambda, Context)) {
    reportLambda(Lambda, *First, Record, SM);
    return;
  }
  // A dispatch joined right away runs while the caller has the object.
  if (!KeptAlive && isJoinedBlocking(Lambda, Context))
    return;
  // A lambda called directly runs while the caller has the object until it
  // first suspends.
  auto IsObject = [&](const Stmt *S) {
    if (CapturesThis && isa<CXXThisExpr>(S))
      return true;
    const auto *Ref = dyn_cast<DeclRefExpr>(S);
    return Ref && Aliases.contains(Ref->getDecl());
  };
  auto IsView = [&](QualType Type) { return isDefaultViewType(Type, Context); };
  const BorrowRoots Roots{IsObject, /*PointerValues=*/true, IsView};
  if (KeptAlive) {
    BorrowRoots Result = Roots;
    Result.Joined = true;
    if (firstUseAfterSuspension(Operator, IsObject, Context, &Result))
      reportLambda(Lambda, *First, Record, SM);
    return;
  }
  if (isCalledInScope(Consumers) &&
      !firstUseAfterSuspension(Operator, IsObject, Context,
                               &Roots))
    return;
  reportLambda(Lambda, *First, Record, SM);
}

void CoroutineUnsafeThisCheck::reportLambda(const LambdaExpr *Lambda,
                                            const LambdaCapture &Capture,
                                            const CXXRecordDecl *Record,
                                            const SourceManager &SM) {
  const SourceLocation Loc = reportLocationIn(
      Capture.isImplicit() ? Lambda->getCaptureDefaultLoc()
                           : Capture.getLocation(),
      Lambda->getEndLoc(), SM);
  if (Loc.isInvalid())
    return;
  // Of an alias, `[pThis = this]`, or a pointer into it, `[pValue =
  // &m_Value]`, the capture.
  if (!Capture.capturesThis() && Capture.capturesVariable()) {
    diag(Loc, "a TCFuture coroutine lambda captures %1, which points to or "
              "into the object of %0, which is not an actor, so the object "
              "may be gone when it runs or resumes; derive %0 from CActor or "
              "CAllowUnsafeThis, keep it alive with a shared pointer in the "
              "capture, or return a TCUnsafeFuture")
        << diagnosticDecl(Record) << Capture.getCapturedVar();
    return;
  }
  diag(Loc, "a TCFuture coroutine lambda captures 'this' of %0, which is "
            "not an actor, so the object may be gone when it runs or "
            "resumes; derive %0 from CActor or CAllowUnsafeThis, keep it "
            "alive with a shared pointer in the capture, or return a "
            "TCUnsafeFuture")
      << diagnosticDecl(Record);
}

} // namespace clang::tidy::malterlib
