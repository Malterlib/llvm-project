//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CoroutineUtils.h"
#include "ActorAnnotations.h"
#include "OncePerTypeVisitor.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <functional>

namespace clang::tidy::malterlib {
namespace {

/// Finds the coroutine keywords of a function body, without looking into the
/// lambdas and local classes it defines.
class CoroutineKeywordFinder
    : public OncePerTypeVisitor<CoroutineKeywordFinder> {
public:
  bool Found = false;

  bool TraverseLambdaExpr(LambdaExpr *) { return true; }
  bool TraverseCXXRecordDecl(CXXRecordDecl *) { return true; }
  bool TraverseBlockExpr(BlockExpr *) { return true; }

  bool VisitCoreturnStmt(CoreturnStmt *) { return found(); }
  bool VisitCoawaitExpr(CoawaitExpr *) { return found(); }
  bool VisitDependentCoawaitExpr(DependentCoawaitExpr *) { return found(); }
  bool VisitCoyieldExpr(CoyieldExpr *) { return found(); }

private:
  bool found() {
    Found = true;
    return false;
  }
};

/// Finds the uses of `this`, including the implicit ones of member accesses,
/// which matchers that ignore implicit code skip.
class ThisFinder : public OncePerTypeVisitor<ThisFinder> {
public:
  explicit ThisFinder(llvm::SmallVectorImpl<const CallExpr *> *MovedThis)
      : MovedThis(MovedThis) {}

  bool Found = false;
  llvm::SmallVectorImpl<const CallExpr *> *MovedThis;

  // The `this` of a local class is another object.
  bool TraverseCXXRecordDecl(CXXRecordDecl *Record) {
    return Record->isLambda() ? RecursiveASTVisitor::TraverseCXXRecordDecl(Record)
                              : true;
  }

  bool TraverseCallExpr(CallExpr *Call) {
    // Only moving or copying the own object, like `fg_MoveThis(*this)`,
    // counts; other objects are traversed like other calls.
    if (MovedThis && asCallTo(Call, {"fg_MoveThis", "fg_CopyThis"}) &&
        Call->getNumArgs() >= 1 && refersToOwnObject(Call->getArg(0))) {
      MovedThis->push_back(Call);
      return true;
    }
    return RecursiveASTVisitor::TraverseCallExpr(Call);
  }

  // Unevaluated operands don't use the object: `sizeof(m_Value)`,
  // `noexcept(...)`, requires expressions, `typeid` of a non-polymorphic
  // operand, and types, like `decltype(this)`.
  bool TraverseUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *) {
    return true;
  }
  bool TraverseCXXNoexceptExpr(CXXNoexceptExpr *) { return true; }
  bool TraverseRequiresExpr(RequiresExpr *) { return true; }
  bool TraverseCXXTypeidExpr(CXXTypeidExpr *Typeid) {
    return Typeid->isPotentiallyEvaluated()
               ? RecursiveASTVisitor::TraverseCXXTypeidExpr(Typeid)
               : true;
  }
  bool TraverseTypeLoc(TypeLoc, bool = true) { return true; }

  bool VisitCXXThisExpr(CXXThisExpr *) {
    Found = true;
    // Collecting the moves needs all of them.
    return MovedThis != nullptr;
  }
};

} // namespace

/// Whether \p Awaiter is an awaiter that never suspends: it derives from
/// `CSuspendNever` or `std::suspend_never`, like the awaiter of
/// `co_await g_CaptureExceptions`. Such awaiters are assumed to keep the
/// `await_ready` that returns true.
static bool isNeverSuspendingAwaiter(QualType Awaiter) {
  const CXXRecordDecl *Record =
      Awaiter.getNonReferenceType()->getAsCXXRecordDecl();
  llvm::SmallVector<const CXXRecordDecl *, 4> Worklist;
  llvm::SmallPtrSet<const CXXRecordDecl *, 4> Visited;
  if (Record)
    Worklist.push_back(Record);
  while (!Worklist.empty()) {
    const CXXRecordDecl *Current = Worklist.pop_back_val();
    if (!Visited.insert(Current).second)
      continue;
    if (Current->getIdentifier() && (Current->getName() == "CSuspendNever" ||
                                     Current->getName() == "suspend_never"))
      return true;
    if (!Current->hasDefinition())
      continue;
    // One derived from it that has its own `await_ready`, which can suspend.
    if (llvm::any_of(Current->getDefinition()->methods(),
                     [](const CXXMethodDecl *Method) {
                       return Method->getIdentifier() &&
                              (Method->getName() == "await_ready" ||
                               Method->getName() == "await_suspend");
                     }))
      return false;
    for (const CXXBaseSpecifier &Base : Current->bases()) {
      if (const CXXRecordDecl *BaseRecord =
              Base.getType()->getAsCXXRecordDecl())
        Worklist.push_back(BaseRecord);
    }
  }
  return false;
}

/// What is told of an AST, which doesn't change while the checks look at
/// it, kept until the AST is gone, so that what is asked many times, like
/// about the locals of a long function at each of their uses, or about a
/// lambda each call of it, is worked out once.
struct CASTFacts {
  /// Whether a local keeps its initial value.
  llvm::DenseMap<const VarDecl *, bool> Unchanged;
  /// The variables that each function names other than to read their value,
  /// which only those can change.
  llvm::DenseMap<const FunctionDecl *, llvm::DenseSet<const ValueDecl *>>
      NamedOtherThanRead;
  /// Whether the call operator of a lambda coroutine never suspends.
  llvm::DenseMap<const FunctionDecl *, bool> NonSuspending;
};

static CASTFacts &astFacts(const ASTContext &Context) {
  return astCache<CASTFacts>(Context);
}

static bool isSuspensionImpl(const Stmt *S, int Depth);

static bool lambdaNeverSuspends(const FunctionDecl *Operator);

/// Whether \p Function is a coroutine that returns a future of Malterlib, a
/// `TCFuture` or a `TCUnsafeFuture`, whose promise runs it in the call until
/// it first suspends: one that doesn't suspend gives a future that is ready.
static bool isEagerFutureCoroutine(const FunctionDecl *Function) {
  if (!Function || !isCoroutine(Function))
    return false;
  // Through aliases, of which `TCUnsafeFuture` is one.
  const StringRef Name =
      recordName(Function->getReturnType().getCanonicalType());
  return Name == "TCFuture" || Name == "TCFutureWithFlags";
}

/// Whether \p Operand calls a lambda coroutine directly whose body never
/// suspends, like `co_await fCheckResult(...)` with a local lambda that only
/// `co_return`s: a coroutine that runs to its end in the call gives a future
/// that is ready, and awaiting it doesn't suspend. Each call operator, the
/// specialization a generic lambda is called through, is looked into once,
/// also through many lambdas that call each other.
static bool isNonSuspendingLambdaCall(const Expr *Operand, int Depth) {
  // Also called explicitly, `fCheck.operator()()`.
  const Expr *Object = nullptr;
  const CallExpr *Call = nullptr;
  if (const auto *Operator =
          dyn_cast<CXXOperatorCallExpr>(skipWrappers(Operand));
      Operator && Operator->getOperator() == OO_Call &&
      Operator->getNumArgs() >= 1) {
    Call = Operator;
    Object = Operator->getArg(0);
  } else if (const auto *Member =
                 dyn_cast<CXXMemberCallExpr>(skipWrappers(Operand));
             Member && Member->getMethodDecl() &&
             Member->getMethodDecl()->getOverloadedOperator() == OO_Call) {
    Call = Member;
    Object = Member->getImplicitObjectArgument();
  } else if (const auto *Invoke = dyn_cast<CallExpr>(skipWrappers(Operand));
             Invoke &&
             (isStdInvoke(Invoke) || calleeName(Invoke) == "fg_CallSafe") &&
             Invoke->getNumArgs() >= 1) {
    // Also through `std::invoke(fReady)` or `fg_CallSafe(fReady)`, which
    // call its call operator right away and give its future, also given
    // arguments for it, `fg_CallSafe(fReady, 1)`.
    const LambdaExpr *Lambda = asLambda(Invoke->getArg(0));
    const CXXMethodDecl *Operator =
        Lambda ? Lambda->getCallOperator() : nullptr;
    // Of a generic one, `[](auto _Future) -> TCFuture<int> { co_return co_await
    // fg_Move(_Future); }`, whose template doesn't tell what it awaits, each
    // specialization it is called through.
    if (const FunctionTemplateDecl *Template =
            Operator ? Operator->getDescribedFunctionTemplate() : nullptr) {
      bool Any = false;
      for (const FunctionDecl *Specialization : Template->specializations()) {
        if (!isEagerFutureCoroutine(Specialization) ||
            !lambdaNeverSuspends(Specialization))
          return false;
        Any = true;
      }
      return Any;
    }
    return Operator && isEagerFutureCoroutine(Operator) &&
           lambdaNeverSuspends(Operator);
  }
  // Also a coroutine function called directly whose body can be seen,
  // `co_await fg_Ready()` of `TCFuture<int> fg_Ready() { co_return 0; }`,
  // but not a virtual one, which an override can replace.
  if (const auto *Direct = dyn_cast<CallExpr>(skipWrappers(Operand));
      !Call && Direct && Direct->getDirectCallee() &&
      !isa<CXXOperatorCallExpr>(Direct)) {
    const FunctionDecl *Callee = Direct->getDirectCallee()->getDefinition();
    const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
    if (!Callee || !Callee->getBody() || !isEagerFutureCoroutine(Callee) ||
        (Method && Method->isVirtual() && !Method->hasAttr<FinalAttr>() &&
         !Method->getParent()->hasAttr<FinalAttr>()))
      return false;
    return lambdaNeverSuspends(Callee);
  }
  if (!Call)
    return false;
  const LambdaExpr *Lambda = asLambda(Object);
  const auto *Operator = dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
  if (!Lambda || !Operator ||
      Operator->getParent() != Lambda->getLambdaClass() ||
      !isEagerFutureCoroutine(Operator))
    return false;
  return lambdaNeverSuspends(Operator);
}

static bool lambdaNeverSuspends(const FunctionDecl *Operator) {
  CASTFacts &Facts = astFacts(Operator->getASTContext());
  if (auto It = Facts.NonSuspending.find(Operator);
      It != Facts.NonSuspending.end())
    return It->second;
  // One that calls itself may suspend where it does.
  static thread_local llvm::SmallPtrSet<const FunctionDecl *, 8> InProgress;
  if (!InProgress.insert(Operator).second)
    return false;
  const llvm::scope_exit Done([&] { InProgress.erase(Operator); });
  const int Depth = 0;
  // Its own suspensions, not those of the lambdas it makes.
  class CFinder : public OncePerTypeVisitor<CFinder> {
  public:
    CFinder(int Depth, const ASTContext &Context)
        : Depth(Depth), Context(Context) {}
    // Nor what isn't evaluated, `sizeof(...)` or `decltype(...)`, which only
    // tells a type.
    bool TraverseStmt(Stmt *S) {
      if (S && isUnevaluatedNode(S))
        return true;
      return RecursiveASTVisitor::TraverseStmt(S);
    }
    bool TraverseDecltypeTypeLoc(DecltypeTypeLoc, bool) { return true; }
    bool TraverseTypeOfExprTypeLoc(TypeOfExprTypeLoc, bool) { return true; }
    // What a lambda it makes captures is evaluated in its body, but not the
    // lambda's own body.
    bool TraverseLambdaExpr(LambdaExpr *Lambda) {
      for (Expr *Init : Lambda->capture_inits()) {
        if (Init && !TraverseStmt(Init))
          return false;
      }
      return true;
    }
    // Only the branch of an `if constexpr` that isn't discarded runs, and the
    // one of an `if consteval` that runs when not constant evaluated.
    bool TraverseIfStmt(IfStmt *If) {
      if (If->isConsteval())
        return TraverseStmt(If->isNegatedConsteval() ? If->getThen()
                                                     : If->getElse());
      if (If->isConstexpr() && !If->getCond()->isValueDependent())
        if (const std::optional<Stmt *> Case =
                If->getNondiscardedCase(Context)) {
          if (!TraverseStmt(If->getInit()) ||
              !TraverseStmt(If->getConditionVariableDeclStmt()) ||
              !TraverseStmt(If->getCond()))
            return false;
          return !*Case || TraverseStmt(*Case);
        }
      // Nor the branch that a constant condition never takes, `if (false)`,
      // unless a jump to a label in it can reach it.
      bool Taken = false;
      if (!If->isConstexpr() && If->getCond() && !If->getConditionVariable() &&
          !If->getCond()->isValueDependent() &&
          If->getCond()->EvaluateAsBooleanCondition(Taken, Context) &&
          !hasJumpTarget(Taken ? If->getElse() : If->getThen())) {
        if (!TraverseStmt(If->getInit()) || !TraverseStmt(If->getCond()))
          return false;
        Stmt *Case = Taken ? If->getThen() : If->getElse();
        return !Case || TraverseStmt(Case);
      }
      return RecursiveASTVisitor::TraverseIfStmt(If);
    }
    // Nor what comes after a statement that always leaves, `break;`, that no
    // label in it can be reached at.
    bool TraverseCompoundStmt(CompoundStmt *Compound) {
      noteCancelled(Compound);
      bool Left = false;
      for (Stmt *Statement : Compound->body()) {
        if (Left && !hasLabel(Statement))
          continue;
        Left = false;
        if (!TraverseStmt(Statement))
          return false;
        if (leaves(Statement))
          Left = true;
      }
      return true;
    }
    bool leaves(const Stmt *S) const { return alwaysLeaves(S, Context); }
    // Nor the body of a loop whose condition is false, `while (false)` or
    // `for (; false;)`, that no label in it can be reached at.
    bool TraverseWhileStmt(WhileStmt *While) {
      bool Runs = true;
      if (!While->getConditionVariable() &&
          !While->getCond()->isValueDependent() &&
          While->getCond()->EvaluateAsBooleanCondition(Runs, Context) &&
          !Runs && !hasLabel(While->getBody()))
        return TraverseStmt(While->getCond());
      return RecursiveASTVisitor::TraverseWhileStmt(While);
    }
    bool TraverseForStmt(ForStmt *For) {
      bool Runs = true;
      if (For->getCond() && !For->getConditionVariable() &&
          !For->getCond()->isValueDependent() &&
          For->getCond()->EvaluateAsBooleanCondition(Runs, Context) && !Runs &&
          !hasLabel(For->getBody()) && !hasLabel(For->getInc()))
        return TraverseStmt(For->getInit()) && TraverseStmt(For->getCond());
      return RecursiveASTVisitor::TraverseForStmt(For);
    }
    // Nor the arm of a conditional that a constant condition doesn't choose,
    // `false ? co_await fg_Work() : 0`, or the right side of a `&&` or `||`
    // that a constant left side skips.
    bool TraverseConditionalOperator(ConditionalOperator *Conditional) {
      bool Chosen = false;
      if (!Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(Chosen, Context))
        return TraverseStmt(Conditional->getCond()) &&
               TraverseStmt(Chosen ? Conditional->getTrueExpr()
                                   : Conditional->getFalseExpr());
      return RecursiveASTVisitor::TraverseConditionalOperator(Conditional);
    }
    bool TraverseBinaryOperator(BinaryOperator *Binary) {
      bool Left = false;
      if (Binary->isLogicalOp() && !Binary->getLHS()->isValueDependent() &&
          Binary->getLHS()->EvaluateAsBooleanCondition(Left, Context) &&
          Left == (Binary->getOpcode() == BO_LOr))
        return TraverseStmt(Binary->getLHS());
      return RecursiveASTVisitor::TraverseBinaryOperator(Binary);
    }
    static bool hasLabel(const Stmt *S) {
      if (!S)
        return false;
      if (isa<LabelStmt, SwitchCase>(S))
        return true;
      return llvm::any_of(childrenOnce(S), hasLabel);
    }
    static bool isAsyncDestroy(const Stmt *S) {
      const auto *Call = dyn_cast_or_null<CallExpr>(S);
      return Call && llvm::is_contained(llvm::ArrayRef<StringRef>(
                                            std::begin(AsyncDestroyFunctions),
                                            std::end(AsyncDestroyFunctions)),
                                        calleeName(Call));
    }
    // The cleanups of `fg_AsyncDestroy` in \p Compound whose guard is
    // cleared, `Cleanup.f_Clear()`, after nothing that can leave or throw,
    // which never run.
    void noteCancelled(const CompoundStmt *Compound) {
      const auto Body = Compound->body();
      for (auto It = Body.begin(); It != Body.end(); ++It) {
        const auto *Declaration = dyn_cast<DeclStmt>(*It);
        const auto *Guard =
            Declaration && Declaration->isSingleDecl()
                ? dyn_cast<VarDecl>(Declaration->getSingleDecl())
                : nullptr;
        const auto *Await =
            Guard && Guard->getInit()
                ? dyn_cast<CoawaitExpr>(
                      skipWrappers(Guard->getInit())->IgnoreParenImpCasts())
                : nullptr;
        const Stmt *Registration =
            Await ? skipWrappers(Await->getOperand())->IgnoreParenImpCasts()
                  : nullptr;
        if (!isAsyncDestroy(Registration))
          continue;
        for (auto Next = std::next(It); Next != Body.end(); ++Next) {
          const auto *Clear = isa<Expr>(*Next)
                                  ? dyn_cast<CXXMemberCallExpr>(
                                        cast<Expr>(*Next)->IgnoreImplicit())
                                  : nullptr;
          const auto *Object =
              Clear && Clear->getImplicitObjectArgument()
                  ? dyn_cast<DeclRefExpr>(Clear->getImplicitObjectArgument()
                                              ->IgnoreParenImpCasts())
                  : nullptr;
          if (Object && Object->getDecl() == Guard && Clear->getMethodDecl() &&
              Clear->getMethodDecl()->getIdentifier() &&
              Clear->getMethodDecl()->getName() == "f_Clear") {
            Cancelled.insert(Registration);
            break;
          }
          if (mayLeave(*Next))
            break;
        }
      }
    }
    // Whether \p S can leave, also by throwing, or do anything that can.
    static bool mayLeave(const Stmt *S) {
      if (!S)
        return false;
      if (isa<ReturnStmt, CoreturnStmt, BreakStmt, ContinueStmt, GotoStmt,
              IndirectGotoStmt, CXXThrowExpr, CallExpr, CXXConstructExpr,
              CoroutineSuspendExpr, CXXNewExpr, CXXDeleteExpr, LambdaExpr>(S))
        return true;
      return llvm::any_of(childrenOnce(S), mayLeave);
    }
    bool VisitStmt(Stmt *S) {
      if (isa<CoroutineSuspendExpr>(S) && isSuspensionImpl(S, Depth + 1))
        Found = true;
      // A cleanup of `fg_AsyncDestroy`, which it joins when it finishes, can
      // suspend it there, unless it is cancelled first, or is a coroutine
      // lambda that can be seen not to suspend, whose future is ready when
      // it is joined.
      if (isAsyncDestroy(S) && !Cancelled.contains(S) && !isReadyCleanup(S))
        Found = true;
      return !Found;
    }
    static bool isReadyCleanup(const Stmt *S) {
      const auto *Call = cast<CallExpr>(S);
      bool Any = false;
      for (const Expr *Argument : Call->arguments()) {
        const auto *Lambda =
            dyn_cast<LambdaExpr>(skipWrappers(Argument)->IgnoreParenImpCasts());
        if (!Lambda)
          continue;
        const CXXMethodDecl *Operator = Lambda->getCallOperator();
        if (Operator->getDescribedFunctionTemplate() ||
            !isEagerFutureCoroutine(Operator) || !lambdaNeverSuspends(Operator))
          return false;
        Any = true;
      }
      return Any;
    }
    int Depth;
    const ASTContext &Context;
    bool Found = false;
    llvm::SmallPtrSet<const Stmt *, 2> Cancelled;
  };
  CFinder Finder(Depth, Operator->getASTContext());
  Finder.TraverseStmt(Operator->getBody());
  Facts.NonSuspending.try_emplace(Operator, !Finder.Found);
  return !Finder.Found;
}

QualType argumentParameterType(const Expr *Call, unsigned iArgument) {
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(Call)) {
    const CXXConstructorDecl *Constructor = Construct->getConstructor();
    return Constructor && iArgument < Constructor->getNumParams()
               ? Constructor->getParamDecl(iArgument)->getType()
               : QualType();
  }
  const auto *Called = dyn_cast<CallExpr>(Call);
  if (!Called)
    return QualType();
  const FunctionDecl *Callee = Called->getDirectCallee();
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
  const unsigned Offset = isa<CXXOperatorCallExpr>(Called) && Method &&
                                  !Method->isExplicitObjectMemberFunction()
                              ? 1
                              : 0;
  if (iArgument < Offset)
    return QualType();
  const unsigned iParameter = iArgument - Offset;
  if (Callee)
    return iParameter < Callee->getNumParams()
               ? Callee->getParamDecl(iParameter)->getType()
               : QualType();
  QualType Type = Called->getCallee()->IgnoreParenImpCasts()->getType();
  if (const auto *Pointer = dyn_cast<BinaryOperator>(
          Called->getCallee()->IgnoreParens());
      Pointer && Pointer->isPtrMemOp())
    Type = Pointer->getRHS()->getType();
  if (const auto *Function = Type->getAs<PointerType>())
    Type = Function->getPointeeType();
  else if (const auto *Function = Type->getAs<ReferenceType>())
    Type = Function->getPointeeType();
  else if (const auto *Member = Type->getAs<MemberPointerType>())
    Type = Member->getPointeeType();
  const auto *Prototype = Type->getAs<FunctionProtoType>();
  return Prototype && iParameter < Prototype->getNumParams()
             ? Prototype->getParamType(iParameter)
             : QualType();
}

bool suspendsIn(const Stmt *S, const ASTContext &Context) {
  if (!S)
    return false;
  // Making a lambda runs what initializes its captures, `[Value = co_await
  // fg_Work()]`, not its body.
  if (const auto *Lambda = dyn_cast<LambdaExpr>(S))
    return llvm::any_of(Lambda->capture_inits(), [&](const Expr *Init) {
      return suspendsIn(Init, Context);
    });
  if (isa<CoroutineSuspendExpr>(S) && isSuspension(S))
    return true;
  auto Constant = [&](const Expr *Condition, bool &Value) {
    return !Condition->isValueDependent() &&
           Condition->EvaluateAsBooleanCondition(Value, Context);
  };
  bool Value = false;
  if (const auto *Conditional = dyn_cast<ConditionalOperator>(S);
      Conditional && Constant(Conditional->getCond(), Value))
    return suspendsIn(Conditional->getCond(), Context) ||
           suspendsIn(Value ? Conditional->getTrueExpr()
                            : Conditional->getFalseExpr(),
                      Context);
  if (const auto *Binary = dyn_cast<BinaryOperator>(S);
      Binary && Binary->isLogicalOp() && Constant(Binary->getLHS(), Value) &&
      Value == (Binary->getOpcode() == BO_LOr))
    return suspendsIn(Binary->getLHS(), Context);
  return llvm::any_of(childrenOnce(S), [&](const Stmt *Child) {
    return suspendsIn(Child, Context);
  });
}

const Expr *accessedAfterSuspension(const Stmt *Access,
                                    const ASTContext &Context) {
  // Whether an offset added to the pointer \p Pointer suspends, also nested,
  // `(pValue + co_await fg_Offset()) + 1`, after the pointer is read.
  std::function<bool(const Expr *, int)> OffsetSuspends =
      [&](const Expr *Pointer, int Depth) {
        Pointer = Pointer->IgnoreParenImpCasts();
        // Also cast to another pointer, `static_cast<int *>(pValue + ...)`.
        if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Pointer);
            Cast && Cast->getType()->isPointerType() && Depth <= 16)
          return OffsetSuspends(Cast->getSubExpr(), Depth + 1);
        const auto *Binary = dyn_cast<BinaryOperator>(Pointer);
        if (!Binary || !Binary->isAdditiveOp() || Depth > 16)
          return false;
        const bool LeftPointer = Binary->getLHS()->getType()->isPointerType();
        return suspendsIn(LeftPointer ? Binary->getRHS() : Binary->getLHS(),
                          Context) ||
               OffsetSuspends(LeftPointer ? Binary->getLHS() : Binary->getRHS(),
                              Depth + 1);
      };
  // What an assignment writes, also one that reads it too, `*pValue +=
  // co_await fg_Work()`, after its right side, which runs first: what the
  // pointer the target is reached through points to, `pValue` of
  // `pValue->m_Value`, `this` of a member, or a reference that names it.
  if (const auto *Assign = dyn_cast_or_null<BinaryOperator>(Access);
      Assign && Assign->isAssignmentOp()) {
    if (!suspendsIn(Assign->getRHS(), Context))
      return nullptr;
    const Expr *Target = Assign->getLHS();
    for (int Depth = 0; Target && Depth < 16; ++Depth) {
      Target = Target->IgnoreParens();
      if (const auto *Member = dyn_cast<MemberExpr>(Target)) {
        if (Member->isArrow())
          return Member->getBase();
        Target = Member->getBase();
      } else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Target))
        return Subscript->getBase();
      else if (const auto *Deref = dyn_cast<UnaryOperator>(Target);
               Deref && Deref->getOpcode() == UO_Deref)
        return Deref->getSubExpr();
      else if (const auto *Ref = dyn_cast<DeclRefExpr>(Target);
               Ref && Ref->getDecl()->getType()->isReferenceType())
        return Ref;
      else
        break;
    }
    return nullptr;
  }
  // An element, `pValue[co_await fg_Index()]` or `(pValue + co_await
  // fg_Offset())[0]`.
  if (const auto *Subscript = dyn_cast_or_null<ArraySubscriptExpr>(Access))
    return suspendsIn(Subscript->getIdx(), Context) ||
                   OffsetSuspends(Subscript->getBase(), 0)
               ? Subscript->getBase()
               : nullptr;
  // A member through a pointer to it, `pValue->*(co_await fg_Member())`,
  // which is accessed after what selects it.
  if (const auto *Pointer = dyn_cast_or_null<BinaryOperator>(Access);
      Pointer && Pointer->isPtrMemOp())
    return suspendsIn(Pointer->getRHS(), Context) ? Pointer->getLHS() : nullptr;
  // A member through the pointer, `(pValue + co_await fg_Offset())->m_Value`.
  if (const auto *Member = dyn_cast_or_null<MemberExpr>(Access);
      Member && Member->isArrow())
    return OffsetSuspends(Member->getBase(), 0) ? Member->getBase() : nullptr;
  const auto *Deref = dyn_cast_or_null<UnaryOperator>(Access);
  if (!Deref || Deref->getOpcode() != UO_Deref)
    return nullptr;
  return OffsetSuspends(Deref->getSubExpr(), 0) ? Deref->getSubExpr()
                                                : nullptr;
}

llvm::SmallVector<const Expr *, 2> pointerRoots(const Expr *Pointer) {
  llvm::SmallVector<const Expr *, 2> Roots;
  std::function<void(const Expr *, int)> Add = [&](const Expr *Value,
                                                    int Depth) {
    for (; Value && Depth < 16; ++Depth) {
      Value = Value->IgnoreParenImpCasts();
      if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Value)) {
        Value = Cast->getSubExpr();
        continue;
      }
      if (const auto *Binary = dyn_cast<BinaryOperator>(Value);
          Binary && Binary->isAdditiveOp()) {
        Value = Binary->getLHS()->getType()->isPointerType() ? Binary->getLHS()
                                                             : Binary->getRHS();
        continue;
      }
      // Stepped, `pValue++`, which gives where it pointed, or `++pValue`, also
      // by more, `pValue += 2`, and what is assigned, `pValue = pOther`.
      if (const auto *Step = dyn_cast<UnaryOperator>(Value);
          Step && Step->isIncrementDecrementOp()) {
        Value = Step->getSubExpr();
        continue;
      }
      if (const auto *Assign = dyn_cast<BinaryOperator>(Value);
          Assign && Assign->isAssignmentOp()) {
        Value = Assign->getOpcode() == BO_Assign ? Assign->getRHS()
                                                 : Assign->getLHS();
        continue;
      }
      // An array member of what a pointer or reference designates,
      // `pEntry->m_Values[...]`, is in what that does.
      if (const auto *Member = dyn_cast<MemberExpr>(Value);
          Member && Member->getType()->isArrayType()) {
        Value = Member->getBase();
        continue;
      }
      // A row of an array that a pointer points into, `pRows[0][...]`, is in
      // what that does.
      if (const auto *Row = dyn_cast<ArraySubscriptExpr>(Value);
          Row && Row->getType()->isArrayType()) {
        Value = Row->getBase();
        continue;
      }
      // The last of a comma, and either value of a conditional, `(bChoose ?
      // pFirst : pSecond)[...]`.
      if (const auto *Comma = dyn_cast<BinaryOperator>(Value);
          Comma && Comma->getOpcode() == BO_Comma) {
        Value = Comma->getRHS();
        continue;
      }
      if (const auto *Conditional =
              dyn_cast<AbstractConditionalOperator>(Value)) {
        Add(Conditional->getTrueExpr(), Depth + 1);
        Value = Conditional->getFalseExpr();
        continue;
      }
      // The first value of `pValue ?: nullptr`, which is its condition.
      if (const auto *Opaque = dyn_cast<OpaqueValueExpr>(Value);
          Opaque && Opaque->getSourceExpr()) {
        Value = Opaque->getSourceExpr();
        continue;
      }
      break;
    }
    if (Value)
      Roots.push_back(Value);
  };
  Add(Pointer, 0);
  return Roots;
}

/// Of what isn't evaluated, `sizeof(Value)`, `noexcept(...)`, a `typeid`
/// that doesn't look at the dynamic type, or what a builtin only looks at the
/// type or constness of, `__builtin_constant_p(Value)`, nothing; of a generic
/// selection, only what it selects, and of `__builtin_choose_expr`, what it
/// chooses. Null when \p S isn't one of them.
static std::optional<llvm::SmallVector<const Stmt *, 4>>
unevaluatedChildren(const Stmt *S) {
  llvm::SmallVector<const Stmt *, 4> Children;
  if (isa<UnaryExprOrTypeTraitExpr, CXXNoexceptExpr>(S))
    return Children;
  if (const auto *Typeid = dyn_cast<CXXTypeidExpr>(S);
      Typeid && !Typeid->isPotentiallyEvaluated())
    return Children;
  if (const auto *Call = dyn_cast<CallExpr>(S);
      Call && (Call->getBuiltinCallee() == Builtin::BI__builtin_constant_p ||
               Call->getBuiltinCallee() == Builtin::BI__builtin_object_size))
    return Children;
  if (const auto *Selection = dyn_cast<GenericSelectionExpr>(S);
      Selection && !Selection->isResultDependent()) {
    Children.push_back(Selection->getResultExpr());
    return Children;
  }
  // Of `__builtin_choose_expr`, only what it chooses, and its condition.
  if (const auto *Choose = dyn_cast<ChooseExpr>(S);
      Choose && !Choose->isConditionDependent()) {
    Children.push_back(Choose->getCond());
    Children.push_back(Choose->getChosenSubExpr());
    return Children;
  }
  return std::nullopt;
}

llvm::SmallVector<const Stmt *, 4> evaluatedChildren(const Stmt *S,
                                                     const ASTContext &Context) {
  if (std::optional<llvm::SmallVector<const Stmt *, 4>> Unevaluated =
          unevaluatedChildren(S))
    return *Unevaluated;
  llvm::SmallVector<const Stmt *, 4> Children;
  // Of an `if consteval`, what runs when not constant evaluated.
  if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConsteval()) {
    if (const Stmt *Runs =
            If->isNegatedConsteval() ? If->getThen() : If->getElse())
      Children.push_back(Runs);
    return Children;
  }
  // Of a condition that is constant, `if (false)`, the branch it takes,
  // unless a jump to a label in the other can reach it.
  // The evaluation, like the checks' other evaluations of what isn't a
  // constant expression, takes as many steps as the language says, up to
  // -fconstexpr-steps: a chain of default arguments that each call the one
  // before twice doubles them with each. It could reuse the values of default
  // arguments the way the check of an expression for overflow does
  // (ReusesDefaultArgumentValues in ExprConstant.cpp), through an evaluation
  // that doesn't decide what the language accepts.
  bool Taken = false;
  if (const auto *If = dyn_cast<IfStmt>(S);
      If && !If->isConstexpr() && If->getCond() &&
      !If->getConditionVariable() && !If->getCond()->isValueDependent() &&
      If->getCond()->EvaluateAsBooleanCondition(Taken, Context) &&
      !hasJumpTarget(Taken ? If->getElse() : If->getThen())) {
    for (const Stmt *Child :
         {If->getInit(), static_cast<const Stmt *>(If->getCond()),
          static_cast<const Stmt *>(Taken ? If->getThen() : If->getElse())})
      if (Child)
        Children.push_back(Child);
    return Children;
  }
  if (const auto *If = dyn_cast<IfStmt>(S);
      If && If->isConstexpr() && !If->getCond()->isValueDependent())
    if (const std::optional<const Stmt *> Case =
            If->getNondiscardedCase(Context)) {
      for (const Stmt *Child :
           {If->getInit(), static_cast<const Stmt *>(
                               If->getConditionVariableDeclStmt()),
            static_cast<const Stmt *>(If->getCond()), *Case})
        if (Child)
          Children.push_back(Child);
      return Children;
    }
  // Of a conditional whose condition is constant, `false ? _pValue :
  // nullptr`, the value it chooses.
  if (const auto *Conditional = dyn_cast<ConditionalOperator>(S);
      Conditional && !Conditional->getCond()->isValueDependent() &&
      Conditional->getCond()->EvaluateAsBooleanCondition(Taken, Context)) {
    Children.push_back(Conditional->getCond());
    Children.push_back(Taken ? Conditional->getTrueExpr()
                             : Conditional->getFalseExpr());
    return Children;
  }
  // Of a loop whose condition is false, `while (false)`, not the body, which
  // never runs, unless a jump to a label in it can reach it.
  auto Never = [&](const Expr *Condition) {
    bool Runs = true;
    return Condition && !Condition->isValueDependent() &&
           Condition->EvaluateAsBooleanCondition(Runs, Context) && !Runs;
  };
  if (const auto *While = dyn_cast<WhileStmt>(S);
      While && !While->getConditionVariable() && Never(While->getCond()) &&
      !hasJumpTarget(While->getBody())) {
    Children.push_back(While->getCond());
    return Children;
  }
  if (const auto *For = dyn_cast<ForStmt>(S);
      For && !For->getConditionVariable() && Never(For->getCond()) &&
      !hasJumpTarget(For->getBody()) && !hasJumpTarget(For->getInc())) {
    for (const Stmt *Child :
         {For->getInit(), static_cast<const Stmt *>(For->getCond())})
      if (Child)
        Children.push_back(Child);
    return Children;
  }
  // Of a block, not what follows a statement that always returns, `return;`
  // or `co_return;`, unless a jump can reach it.
  if (const auto *Block = dyn_cast<CompoundStmt>(S)) {
    bool Left = false;
    for (const Stmt *Statement : Block->body()) {
      if (Left && !hasJumpTarget(Statement))
        continue;
      Left = false;
      Children.push_back(Statement);
      if (isa<ReturnStmt, CoreturnStmt>(Statement))
        Left = true;
    }
    return Children;
  }
  for (const Stmt *Child : childrenOnce(S))
    if (Child)
      Children.push_back(Child);
  return Children;
}

llvm::SmallVector<const Stmt *, 4> evaluatedChildren(const Stmt *S) {
  if (std::optional<llvm::SmallVector<const Stmt *, 4>> Unevaluated =
          unevaluatedChildren(S))
    return *Unevaluated;
  if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConsteval()) {
    llvm::SmallVector<const Stmt *, 4> Children;
    if (const Stmt *Runs =
            If->isNegatedConsteval() ? If->getThen() : If->getElse())
      Children.push_back(Runs);
    return Children;
  }
  if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConstexpr())
    if (const auto *Condition = dyn_cast<ConstantExpr>(If->getCond());
        Condition && Condition->hasAPValueResult() &&
        Condition->getAPValueResult().isInt()) {
      llvm::SmallVector<const Stmt *, 4> Children;
      const Stmt *Case = Condition->getAPValueResult().getInt().isZero()
                             ? If->getElse()
                             : If->getThen();
      for (const Stmt *Child :
           {If->getInit(),
            static_cast<const Stmt *>(If->getConditionVariableDeclStmt()),
            static_cast<const Stmt *>(If->getCond()), Case})
        if (Child)
          Children.push_back(Child);
      return Children;
    }
  llvm::SmallVector<const Stmt *, 4> Children;
  for (const Stmt *Child : childrenOnce(S))
    if (Child)
      Children.push_back(Child);
  return Children;
}

bool isIteratorType(QualType Type) {
  auto Named = [](StringRef Name) {
    return Name.contains("Iterator") || Name == "iterator" ||
           Name == "const_iterator" || Name.ends_with("_iterator") ||
           // The iterator of a vector of libc++.
           Name == "__wrap_iter";
  };
  // Also as it is named where it is declared, `std::vector<int>::iterator`,
  // whatever the class it is an alias of is named.
  for (QualType Sugared = Type; !Sugared.isNull();) {
    if (const auto *Typedef = Sugared->getAs<TypedefType>();
        Typedef && Typedef->getDecl()->getIdentifier() &&
        Named(Typedef->getDecl()->getName()))
      return true;
    const QualType Next =
        Sugared->getLocallyUnqualifiedSingleStepDesugaredType();
    if (Next == Sugared)
      break;
    Sugared = Next;
  }
  const auto *Record = Type->getAsCXXRecordDecl();
  return Record && Record->getIdentifier() && Named(Record->getName());
}

bool hasJumps(const Stmt *S) {
  if (!S || isa<LambdaExpr>(S))
    return false;
  if (isa<GotoStmt, IndirectGotoStmt, LabelStmt>(S))
    return true;
  return llvm::any_of(childrenOnce(S),
                      [](const Stmt *Child) { return hasJumps(Child); });
}

bool isPhaseGuardCall(const Expr *E) {
  const auto *Call =
      E ? dyn_cast<CallExpr>(skipWrappers(E)->IgnoreParenImpCasts()) : nullptr;
  const FunctionDecl *Callee = Call ? Call->getDirectCallee() : nullptr;
  return Callee &&
         llvm::any_of(Callee->specific_attrs<AnnotateAttr>(),
                      [](const AnnotateAttr *Annotation) {
                        return Annotation->getAnnotation() ==
                               AnnotationActorPhaseGuard;
                      });
}

/// What \p Definition, whose body only returns, returns as a constant
/// boolean, `bool await_ready() { return true; }`, evaluated once for each,
/// which can be costly, also when it can't be told.
static std::optional<bool> constantReturn(const FunctionDecl *Definition) {
  if (!Definition)
    return std::nullopt;
  struct CMemo {
    llvm::DenseMap<const FunctionDecl *, std::optional<bool>> Returns;
  };
  ASTContext &Context = Definition->getASTContext();
  auto [It, Inserted] =
      astCache<CMemo>(Context).Returns.try_emplace(Definition, std::nullopt);
  if (!Inserted)
    return It->second;
  std::optional<bool> Result;
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  const auto *Return = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(Body->body_front())
                           : nullptr;
  bool Value = false;
  if (const Expr *Returned = Return ? Return->getRetValue() : nullptr;
      Returned && !Returned->isValueDependent() &&
      Returned->EvaluateAsBooleanCondition(Value, Context))
    Result = Value;
  astCache<CMemo>(Context).Returns[Definition] = Result;
  return Result;
}

/// The member function of the awaiter that \p Call, a call of \p Method on
/// it, runs: of an awaiter whose class is final, `struct CReady final :
/// CReadyBase`, the override that class has, and else \p Method, or null when
/// that is virtual and an override can run instead.
static const CXXMethodDecl *calledAwaiterMethod(const CallExpr *Call,
                                                const CXXMethodDecl *Method) {
  if (!Method->isVirtual() || Method->hasAttr<FinalAttr>() ||
      Method->getParent()->hasAttr<FinalAttr>())
    return Method;
  // As it is, not as converted to the class declaring it.
  const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
  const Expr *Object = Member ? Member->getImplicitObjectArgument() : nullptr;
  const CXXRecordDecl *Awaiter = Object ? Object->IgnoreParenImpCasts()
                                              ->getType()
                                              .getNonReferenceType()
                                              ->getAsCXXRecordDecl()
                                        : nullptr;
  if (!Awaiter || !Awaiter->hasDefinition() ||
      !Awaiter->getDefinition()->hasAttr<FinalAttr>())
    return nullptr;
  // Found once for each class, which can take long through many virtual
  // bases.
  struct CMemo {
    llvm::DenseMap<std::pair<const CXXMethodDecl *, const CXXRecordDecl *>,
                   const CXXMethodDecl *>
        Overriders;
  };
  auto &Overriders = astCache<CMemo>(Method->getASTContext()).Overriders;
  const std::pair<const CXXMethodDecl *, const CXXRecordDecl *> Key{
      Method, Awaiter->getDefinition()};
  if (auto It = Overriders.find(Key); It != Overriders.end())
    return It->second;
  const CXXMethodDecl *Overrider = Method->getCorrespondingMethodInClass(
      Awaiter->getDefinition(), /*MayBeBase=*/true);
  Overriders[Key] = Overrider;
  return Overrider;
}

bool awaiterNeverSuspends(const CoroutineSuspendExpr *Suspend) {
  // The `await_ready` that the await calls tells, whatever class it is
  // inherited from, like with `using CSuspends::await_ready;`.
  // Also a static one, `static bool await_ready() { return true; }`, which is
  // called like a function.
  const auto *Ready =
      Suspend->getReadyExpr()
          ? dyn_cast<CallExpr>(Suspend->getReadyExpr()->IgnoreImplicit())
          : nullptr;
  const auto *ReadyMethod =
      Ready ? dyn_cast_or_null<CXXMethodDecl>(Ready->getDirectCallee())
            : nullptr;
  if (ReadyMethod) {
    const CXXRecordDecl *Owner = ReadyMethod->getParent();
    if (Owner->getIdentifier() && (Owner->getName() == "CSuspendNever" ||
                                   Owner->getName() == "suspend_never"))
      return true;
    // Also one that only returns true, `bool await_ready() { return true; }`,
    // like the awaiter of `co_await g_AccessCoroutine`, but not one that is
    // virtual, which an override can make suspend.
    // Of an awaiter whose class is final, the override that class has.
    const CXXMethodDecl *Called = calledAwaiterMethod(Ready, ReadyMethod);
    const bool Dispatched = !Called;
    const FunctionDecl *Definition =
        Dispatched ? nullptr : Called->getDefinition();
    if (constantReturn(Definition) == std::optional<bool>(true))
      return true;
    // By the awaiter's type only when what `await_ready` it calls can't be
    // told, not one that calls another, like a callable member, `static
    // constexpr auto await_ready = [] { return false; }`.
  } else if (!Suspend->getReadyExpr() && Suspend->getCommonExpr() &&
             isNeverSuspendingAwaiter(Suspend->getCommonExpr()->getType()))
    return true;
  // Also one whose `await_suspend` that isn't virtual only returns false,
  // which resumes the coroutine right away.
  const Expr *SuspendCall = Suspend->getSuspendExpr()
                                ? Suspend->getSuspendExpr()->IgnoreImplicit()
                                : nullptr;
  // Also of an awaiter whose class is final, the override that class has.
  if (const auto *Call = dyn_cast_or_null<CallExpr>(SuspendCall))
    if (const auto *Declared =
            dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
        Declared && Declared->getReturnType()->isBooleanType())
      if (const CXXMethodDecl *Method = calledAwaiterMethod(Call, Declared))
        if (constantReturn(Method->getDefinition()) ==
            std::optional<bool>(false))
          return true;
  return false;
}

bool isSuspension(const Stmt *S) { return isSuspensionImpl(S, 0); }

static bool isSuspensionImpl(const Stmt *S, int Depth) {
  const auto *Suspend = dyn_cast<CoroutineSuspendExpr>(S);
  if (!Suspend || !Suspend->getOperand())
    return false;
  if (isa<CoyieldExpr>(S))
    return true;
  if (awaiterNeverSuspends(Suspend))
    return false;
  // The last of a comma, `co_await (fg_Prepare(), fg_AsyncDestroy(...))`, is
  // what is awaited.
  const Expr *Operand = skipWrappers(Suspend->getOperand());
  for (int Level = 0; Level < 16; ++Level)
    if (const auto *Comma =
            dyn_cast<BinaryOperator>(Operand->IgnoreParenImpCasts());
        Comma && Comma->getOpcode() == BO_Comma)
      Operand = skipWrappers(Comma->getRHS());
    else
      break;
  // Awaiting coroutine flags, like `ECoroutineFlag_CaptureExceptions`, sets
  // them without suspending. Other enums can have awaiters that suspend.
  if (const auto *Enum = Operand->getType()->getAsEnumDecl();
      Enum && Enum->getIdentifier() && Enum->getName() == "ECoroutineFlag")
    return false;
  // These register callbacks of the coroutine or move the object into it,
  // without suspending it, and so does a phase guard, like
  // `f_CheckDestroyedOnResume()`, which registers one with `fg_OnResume`:
  // when it can't, it ends the coroutine.
  // Also through an awaiter made before, `co_await fg_Move(Awaiter)`, and
  // either value of a conditional, `co_await (bChoose ? fg_OnResume(A) :
  // fg_OnResume(B))`.
  if (llvm::all_of(awaitedValues(Operand), [](const Expr *Value) {
        return Value &&
               (isPhaseGuardCall(Value) || asCallTo(Value, {"fg_OnResume"}));
      }))
    return false;
  return !asCallTo(Operand, {"fg_OnResume", "fg_MoveThis", "fg_CopyThis"}) &&
         !asCallTo(Operand, llvm::ArrayRef<StringRef>(
                                std::begin(AsyncDestroyFunctions),
                                std::end(AsyncDestroyFunctions))) &&
         !isNonSuspendingLambdaCall(Operand, Depth);
}

namespace {

class SuspendPointCollector : public OncePerTypeVisitor<SuspendPointCollector> {
public:
  llvm::SmallPtrSet<const Stmt *, 8> Points;

  bool VisitCoawaitExpr(CoawaitExpr *Await) {
    add(Await);
    return true;
  }
  bool VisitCoyieldExpr(CoyieldExpr *Yield) {
    add(Yield);
    return true;
  }

private:
  void add(const CoroutineSuspendExpr *Suspend) {
    if (!isSuspension(Suspend) || !Suspend->getSuspendExpr())
      return;
    llvm::SmallVector<const Stmt *, 8> Worklist{Suspend->getSuspendExpr()};
    while (!Worklist.empty()) {
      const Stmt *S = Worklist.pop_back_val();
      if (!S || !Points.insert(S).second)
        continue;
      for (const Stmt *Child : childrenOnce(S))
        Worklist.push_back(Child);
    }
  }
};

} // namespace

llvm::SmallPtrSet<const Stmt *, 8> suspendPoints(const Stmt *Body) {
  SuspendPointCollector Collector;
  Collector.TraverseStmt(const_cast<Stmt *>(Body));
  return std::move(Collector.Points);
}

llvm::SmallVector<const CFGBlock *, 16> forwardOrder(const CFG &Graph) {
  llvm::SmallVector<const CFGBlock *, 16> Order;
  for (const CFGBlock *Block :
       llvm::ReversePostOrderTraversal<const CFG *>(&Graph))
    Order.push_back(Block);
  return Order;
}

llvm::BitVector reachableBlocks(const CFG &Graph) {
  llvm::BitVector Reachable(Graph.getNumBlockIDs());
  llvm::SmallVector<const CFGBlock *, 16> Worklist{&Graph.getEntry()};
  Reachable.set(Graph.getEntry().getBlockID());
  while (!Worklist.empty()) {
    const CFGBlock *Block = Worklist.pop_back_val();
    for (const CFGBlock::AdjacentBlock &Successor : Block->succs()) {
      const CFGBlock *Next = Successor.getReachableBlock();
      if (Next && !Reachable.test(Next->getBlockID())) {
        Reachable.set(Next->getBlockID());
        Worklist.push_back(Next);
      }
    }
  }
  return Reachable;
}

static bool isUnchangedLocal(const VarDecl *Variable);

/// \p E past parentheses, implicit casts and explicit casts of pointers and
/// references, like `static_cast<CDerived *>(this)`, which point or refer to
/// the same object.
static const Expr *skipObjectCasts(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  while (const auto *Cast = dyn_cast<ExplicitCastExpr>(E)) {
    if (!Cast->getType()->isPointerType() && !Cast->isGLValue())
      break;
    E = Cast->getSubExpr()->IgnoreParenImpCasts();
  }
  return E;
}

namespace {
/// What tells whether an expression is rooted: the declarations that are
/// roots besides `this`, whether `this` is one, and what is already told of
/// the expressions, each once however many paths reach it, like the arguments
/// of a chain of helpers, `fg_Pick(Ref0, Ref0)`.
struct CRootedContext {
  llvm::function_ref<bool(const ValueDecl *)> IsRoot;
  bool ThisIsRoot = true;
  llvm::DenseMap<const Expr *, bool> Told;
};
} // namespace

/// The parameters whose arguments \p Definition gives back as they are on
/// every return, `Map &fg_Second(Map &, Map &_Result) { return _Result; }`,
/// also either of a conditional, `return _bFirst ? _First : _Second;`, or
/// None when it can return something else.
static std::optional<llvm::SmallVector<unsigned, 2>>
returnedParameters(const FunctionDecl *Definition) {
  llvm::SmallVector<unsigned, 2> Returned;
  bool Other = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Other || isa<LambdaExpr>(S))
      return;
    if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
      // Also either value of a conditional, `return _bFirst ? _First :
      // _Second;`.
      std::function<void(const Expr *, int)> Add = [&](const Expr *Value,
                                                       int Depth) {
        Value = Value ? Value->IgnoreParenImpCasts() : nullptr;
        if (const auto *Conditional =
                dyn_cast_or_null<ConditionalOperator>(Value);
            Conditional && Depth < 8) {
          Add(Conditional->getTrueExpr(), Depth + 1);
          Add(Conditional->getFalseExpr(), Depth + 1);
          return;
        }
        const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Value);
        const auto *Parameter =
            Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl()) : nullptr;
        if (!Parameter)
          Other = true;
        else if (!llvm::is_contained(Returned,
                                     Parameter->getFunctionScopeIndex()))
          Returned.push_back(Parameter->getFunctionScopeIndex());
      };
      Add(Return->getRetValue(), 0);
      return;
    }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Definition->getBody());
  if (Other)
    return std::nullopt;
  return Returned;
}

bool isStdInvoke(const CallExpr *Call) {
  const FunctionDecl *Callee = Call ? Call->getDirectCallee() : nullptr;
  return Callee && Callee->isInStdNamespace() && Callee->getIdentifier() &&
         (Callee->getName() == "invoke" || Callee->getName() == "invoke_r") &&
         Call->getNumArgs() >= 1;
}

const CXXMethodDecl *calledInPlaceLambda(const Expr *E) {
  const Expr *Object = nullptr;
  const CallExpr *Call = nullptr;
  if (const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(E);
      Operator && Operator->getOperator() == OO_Call &&
      Operator->getNumArgs() >= 1) {
    Call = Operator;
    Object = Operator->getArg(0);
  } else if (const auto *Member = dyn_cast_or_null<CXXMemberCallExpr>(E);
             Member && Member->getMethodDecl() &&
             Member->getMethodDecl()->getOverloadedOperator() == OO_Call) {
    Call = Member;
    Object = Member->getImplicitObjectArgument();
  } else if (const auto *Invoke = dyn_cast_or_null<CallExpr>(E);
             Invoke && isStdInvoke(Invoke)) {
    // Also `std::invoke([this] { ... })`, which calls it right away.
    const LambdaExpr *Lambda = asLambda(Invoke->getArg(0));
    return Lambda ? Lambda->getCallOperator() : nullptr;
  }
  const auto *Method =
      Call ? dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee()) : nullptr;
  if (!Method || !Method->getParent()->isLambda() || !asLambda(Object))
    return nullptr;
  return Method;
}

llvm::SmallVector<const Expr *, 4>
returnedValues(const FunctionDecl *Function) {
  llvm::SmallVector<const Expr *, 4> Values;
  const FunctionDecl *Definition =
      Function ? Function->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody())
    return Values;
  const ASTContext &Context = Definition->getASTContext();
  // The locals of the function that what it returns names.
  llvm::SmallVector<const VarDecl *, 4> Locals;
  llvm::SmallPtrSet<const VarDecl *, 4> Seen;
  auto AddLocal = [&](const Expr *Value) {
    const auto *Ref = dyn_cast<DeclRefExpr>(Value->IgnoreParenImpCasts());
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
        Local->getDeclContext() == Definition && Seen.insert(Local).second)
      Locals.push_back(Local);
  };
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return;
    if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
      if (const Expr *Value = Return->getRetValue()) {
        Values.push_back(Value);
        // A local returned, what it holds there, when that can be told,
        // `q = &g_Value; return q;`, not what it was before; else all it is
        // made from and assigned.
        const auto *Ref = dyn_cast<DeclRefExpr>(Value->IgnoreParenImpCasts());
        const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        const llvm::SmallVector<const Expr *, 2> Held =
            Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
                    Local->getDeclContext() == Definition &&
                    !Local->getType()->isReferenceType()
                ? assignedValuesAt(Local, Ref)
                : llvm::SmallVector<const Expr *, 2>{};
        if (Held.empty())
          AddLocal(Value);
        for (const Expr *Made : Held) {
          Values.push_back(Made);
          AddLocal(Made);
        }
      }
      return;
    }
    for (const Stmt *Child : evaluatedChildren(S, Context))
      Visit(Child);
  };
  Visit(Definition->getBody());
  // What those locals are made from and assigned, also through other locals.
  llvm::DenseMap<const VarDecl *, llvm::SmallVector<const Expr *, 2>> Assigned;
  std::function<void(const Stmt *)> Collect = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->getOpcode() == BO_Assign)
      if (const auto *Target =
              dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts()))
        if (const auto *Local = dyn_cast<VarDecl>(Target->getDecl()))
          Assigned[Local].push_back(Assign->getRHS());
    for (const Stmt *Child : evaluatedChildren(S, Context))
      Collect(Child);
  };
  if (!Locals.empty())
    Collect(Definition->getBody());
  for (size_t iLocal = 0; iLocal < Locals.size() && iLocal < 16; ++iLocal) {
    const VarDecl *Local = Locals[iLocal];
    if (Local->getInit()) {
      Values.push_back(Local->getInit());
      AddLocal(Local->getInit());
    }
    for (const Expr *Value : Assigned.lookup(Local)) {
      Values.push_back(Value);
      AddLocal(Value);
    }
  }
  return Values;
}

const Expr *returnedArgument(const CallExpr *Call) {
  const llvm::SmallVector<const Expr *, 2> Choices =
      returnedArgumentChoices(Call);
  return Choices.size() == 1 ? Choices[0] : nullptr;
}

llvm::SmallVector<const Expr *, 2>
returnedArgumentChoices(const CallExpr *Call) {
  llvm::SmallVector<const Expr *, 2> Choices;
  if (!Call || isa<CXXOperatorCallExpr, CXXMemberCallExpr>(Call))
    return Choices;
  if (const Expr *Kept = keptPointerArgument(Call)) {
    Choices.push_back(Kept);
    return Choices;
  }
  const FunctionDecl *Callee = Call->getDirectCallee();
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody())
    return Choices;
  const std::optional<llvm::SmallVector<unsigned, 2>> Returned =
      returnedParameters(Definition);
  if (!Returned || llvm::any_of(*Returned, [&](unsigned iParameter) {
        return iParameter >= Call->getNumArgs();
      }))
    return Choices;
  for (unsigned iParameter : *Returned)
    Choices.push_back(Call->getArg(iParameter));
  return Choices;
}

static bool isRootedInThisImpl(const Expr *E, int Depth,
                               CRootedContext &Context);

static bool isRootedInThisUncached(const Expr *E, int Depth,
                                   CRootedContext &Context) {
  if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(E))
    return isRootedInThisImpl(Cleanups->getSubExpr(), Depth, Context);
  if (const auto *Temporary = dyn_cast<MaterializeTemporaryExpr>(E))
    return isRootedInThisImpl(Temporary->getSubExpr(), Depth, Context);
  // The condition that is the first value of `mp_pFirst ?: mp_pSecond`.
  if (const auto *Opaque = dyn_cast<OpaqueValueExpr>(E);
      Opaque && Opaque->getSourceExpr())
    return isRootedInThisImpl(Opaque->getSourceExpr(), Depth, Context);
  if (isa<CXXThisExpr>(E))
    return Context.ThisIsRoot;
  if (const auto *Member = dyn_cast<MemberExpr>(E))
    return isRootedInThisImpl(Member->getBase(), Depth, Context);
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary && (Unary->getOpcode() == UO_AddrOf ||
                Unary->getOpcode() == UO_Deref))
    return isRootedInThisImpl(Unary->getSubExpr(), Depth, Context);
  if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E))
    return isRootedInThisImpl(Subscript->getBase(), Depth, Context);
  // Either value of a conditional, `bChoose ? mp_First : mp_Second`, and the
  // last of a comma, `(fg_Log(), mp_Items)`.
  if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E))
    return isRootedInThisImpl(Conditional->getTrueExpr(), Depth, Context) ||
           isRootedInThisImpl(Conditional->getFalseExpr(), Depth, Context);
  if (const auto *Comma = dyn_cast<BinaryOperator>(E);
      Comma && Comma->getOpcode() == BO_Comma)
    return isRootedInThisImpl(Comma->getRHS(), Depth, Context);
  // A data member through a pointer to it, `(this->*pmItems)[1]`, not a
  // member function.
  if (const auto *Pointer = dyn_cast<BinaryOperator>(E);
      Pointer && Pointer->isPtrMemOp() &&
      !Pointer->getType()->isSpecificPlaceholderType(BuiltinType::BoundMember))
    return isRootedInThisImpl(Pointer->getLHS(), Depth, Context);
  // What a lambda called in place gives back, `[this] { return &mp_Value;
  // }()`, of any of its returns.
  if (const CXXMethodDecl *Operator = calledInPlaceLambda(E))
    return llvm::any_of(returnedValues(Operator), [&](const Expr *Value) {
      return isRootedInThisImpl(Value, Depth + 1, Context);
    });
  // The value of a statement expression, `({ mp_Items.f_FindEqual(Key); })`.
  if (const auto *Statement = dyn_cast<StmtExpr>(E))
    return !Statement->getSubStmt()->body_empty() &&
           isRootedInThisImpl(
               dyn_cast<Expr>(Statement->getSubStmt()->body_back()), Depth + 1,
               Context);
  // `*mp_pInternal`, `mp_pInternal->m_Value`, `m_Map[Key]` and `m_Map(Key)`,
  // which finds or adds an entry of a `TCMap`.
  if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
      Call && Call->getNumArgs() >= 1 &&
      (Call->getOperator() == OO_Star || Call->getOperator() == OO_Arrow ||
       Call->getOperator() == OO_Subscript || Call->getOperator() == OO_Call))
    return isRootedInThisImpl(Call->getArg(0), Depth, Context);
  // `mp_Items.f_FindEqual(Key)` points into the object.
  if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E))
    return isRootedInThisImpl(Call->getImplicitObjectArgument(), Depth,
                              Context);
  // What a function gives back of what it is given by reference, like
  // `fg_Const(mp_Value)`, which refers to it: what one whose body can be seen
  // returns, `fg_Second(mp_Items, Local)` of `return _Second;`, or any.
  if (const auto *Call = dyn_cast<CallExpr>(E);
      Call && Call->isGLValue() &&
      !isa<CXXOperatorCallExpr, CXXMemberCallExpr>(Call))
    if (const FunctionDecl *Callee = Call->getDirectCallee()) {
      const FunctionDecl *Definition = Callee->getDefinition();
      const std::optional<llvm::SmallVector<unsigned, 2>> Returned =
          Definition && Definition->getBody()
              ? returnedParameters(Definition)
              : std::nullopt;
      for (unsigned iArgument = 0; iArgument < Call->getNumArgs() &&
                                   iArgument < Callee->getNumParams();
           ++iArgument)
        if (Callee->getParamDecl(iArgument)->getType()->isReferenceType() &&
            (!Returned || llvm::is_contained(*Returned, iArgument)) &&
            isRootedInThisImpl(Call->getArg(iArgument), Depth + 1, Context))
          return true;
    }
  // Local pointers and references into the object. A pointer that is
  // assigned something else may not point there anymore, which the checks
  // that follow the control flow tell.
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    if (Context.IsRoot && Context.IsRoot(Ref->getDecl()))
      return true;
    const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
    return Variable && Variable->getInit() &&
           (Variable->hasLocalStorage() || Variable->isInitCapture()) &&
           (Variable->getType()->isReferenceType() ||
            (Variable->getType()->isPointerType() &&
             isUnchangedLocal(Variable))) &&
           isRootedInThisImpl(Variable->getInit(), Depth + 1, Context);
  }
  return false;
}

static bool isRootedInThisImpl(const Expr *E, int Depth,
                               CRootedContext &Context) {
  // Each local is made from one declared before it, so a chain of them ends;
  // the arguments of helpers, which branch, are told once each.
  if (!E || Depth > 256)
    return false;
  E = skipObjectCasts(E);
  if (auto It = Context.Told.find(E); It != Context.Told.end())
    return It->second;
  // What reaches itself isn't rooted through itself.
  Context.Told[E] = false;
  const bool Rooted = isRootedInThisUncached(E, Depth, Context);
  Context.Told[E] = Rooted;
  return Rooted;
}

bool isRootedInThis(const Expr *E) {
  CRootedContext Context;
  return isRootedInThisImpl(E, 0, Context);
}

bool isRootedIn(const Expr *E,
                llvm::function_ref<bool(const ValueDecl *)> IsRoot,
                bool ThisIsRoot) {
  CRootedContext Context;
  Context.IsRoot = IsRoot;
  Context.ThisIsRoot = ThisIsRoot;
  return isRootedInThisImpl(E, 0, Context);
}

bool usesThis(const Stmt *S,
              llvm::SmallVectorImpl<const CallExpr *> *MovedThis) {
  if (!S)
    return false;
  ThisFinder Finder(MovedThis);
  Finder.TraverseStmt(const_cast<Stmt *>(S));
  return Finder.Found;
}

bool isAwaited(const Expr *E, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*E);
  for (;;) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    const DynTypedNode &Parent = Parents[0];
    if (const auto *Await = Parent.get<CoroutineSuspendExpr>())
      return Node.get<Stmt>() == Await->getOperand() ||
             Node.get<Stmt>() == Await->getCommonExpr();
    if (!Parent.get<Expr>())
      return false;
    Node = Parent;
  }
}

namespace {

/// The parents of the statements of a body, like a ParentMap: the source of
/// an OpaqueValueExpr is of the last one that refers to it, but its
/// statements are each looked at once, where a ParentMap looks at them again
/// for each, which nested awaits, `co_await fg_Work(co_await ...)`, whose
/// parts each refer to the operand, make exponential.
class CParents {
public:
  explicit CParents(const Stmt *Root) {
    if (Root)
      build(Root, true);
  }
  const Stmt *getParent(const Stmt *S) const { return Parents.lookup(S); }

private:
  void build(const Stmt *S, bool Transparent) {
    if (!S)
      return;
    if (const auto *Pseudo = dyn_cast<PseudoObjectExpr>(S)) {
      const Expr *Syntactic = Pseudo->getSyntacticForm();
      Parents[Syntactic] = S;
      build(Syntactic, true);
      for (const Expr *Semantic : Pseudo->semantics()) {
        Parents[Semantic] = S;
        build(Semantic, false);
      }
      return;
    }
    if (const auto *Conditional = dyn_cast<BinaryConditionalOperator>(S)) {
      Parents[Conditional->getCommon()] = S;
      build(Conditional->getCommon(), true);
      for (const Expr *Part : {Conditional->getCond(),
                               Conditional->getTrueExpr(),
                               Conditional->getFalseExpr()}) {
        Parents[Part] = S;
        build(Part, false);
      }
      return;
    }
    if (const auto *Opaque = dyn_cast<OpaqueValueExpr>(S)) {
      const Expr *Source = Opaque->getSourceExpr();
      if (!Source)
        return;
      auto [It, Inserted] = Parents.try_emplace(Source, S);
      if (!Inserted && Transparent)
        It->second = S;
      if (Built.insert(Source).second)
        build(Source, true);
      return;
    }
    // Each statement is looked into once, also one that is a child of
    // several, like the parts of a coroutine body that refer to its
    // statements again, which nested coroutine lambdas would otherwise make
    // look at again for each one around them.
    for (const Stmt *Child : childrenOnce(S))
      if (Child) {
        Parents[Child] = S;
        if (Visited.insert(Child).second)
          build(Child, Transparent);
      }
  }

  llvm::DenseMap<const Stmt *, const Stmt *> Parents;
  /// The sources of OpaqueValueExprs whose statements are looked at.
  llvm::DenseSet<const Stmt *> Built;
  /// The statements whose children are looked at.
  llvm::DenseSet<const Stmt *> Visited;
};

/// Where a coroutine can be after it suspended.
struct SuspensionFlow {
  /// The AST of the function.
  const ASTContext *Context = nullptr;
  /// Whether the control flow could be built.
  bool Built = false;
  /// The statements of the control flow, which an expression of the body is
  /// evaluated with.
  llvm::DenseSet<const Stmt *> Elements;
  /// Those of them that can run after a suspension.
  llvm::DenseSet<const Stmt *> After;
  /// Those of them the control flow can reach.
  llvm::DenseSet<const Stmt *> ReachableElements;
  /// Whether a reachable statement suspends, also when nothing is after it.
  bool Suspends = false;
  /// The control flow.
  std::unique_ptr<CFG> Graph;
  /// The parents of the statements of the body.
  std::unique_ptr<CParents> Parents;
  /// The pointer parameters, and pointers of the closure, that the body
  /// points elsewhere, and where.
  llvm::DenseMap<const VarDecl *, llvm::SmallVector<const Stmt *, 2>>
      Redirections;
  /// Those that a branch points elsewhere, the `if` whose branch does, `if
  /// (_bAsync) { _pText = &Copy; co_await ...; }`, which protects what follows
  /// it when each suspension is in that branch.
  llvm::DenseMap<const VarDecl *, llvm::SmallVector<const IfStmt *, 1>>
      BranchRedirections;
  /// The references in the body to each declaration, in order, and the
  /// suspensions, made when first asked for.
  mutable std::optional<
      llvm::DenseMap<const ValueDecl *, llvm::SmallVector<const Stmt *, 2>>>
      References;
  mutable std::optional<llvm::SmallVector<const Stmt *, 8>> Suspensions;
  /// Of them, those that suspend, outside of the lambdas in the body, not
  /// `co_await ECoroutineFlag_CaptureExceptions`, made when first asked for.
  mutable std::optional<llvm::SmallVector<const Stmt *, 8>> Suspending;
  /// The locals that `co_return` gives, also through those they are made from
  /// or assigned, made when first asked for.
  mutable std::optional<llvm::SmallPtrSet<const ValueDecl *, 4>> Returned;
};

} // namespace

/// Whether \p Decl matches \p Pattern: a qualified name matches the qualified
/// name, a leading `*` matches the end of the name, and a trailing one its
/// start.
static bool matchesName(const NamedDecl *Decl, StringRef Pattern) {
  if (!Decl->getIdentifier())
    return false;
  if (Pattern.consume_front("*"))
    return Decl->getName().ends_with(Pattern);
  if (Pattern.consume_back("*"))
    return Decl->getName().starts_with(Pattern);
  if (Pattern.contains("::"))
    return hasQualifiedName(Decl, Pattern);
  return Decl->getName() == Pattern;
}

bool matchesTypeName(QualType Type, llvm::ArrayRef<StringRef> Patterns,
                     const ASTContext &Context) {
  if (Type.isNull())
    return false;
  auto Matches = [&](const NamedDecl *Decl) {
    return llvm::any_of(Patterns, [&](StringRef Pattern) {
      return !Pattern.contains('<') && matchesName(Decl, Pattern);
    });
  };
  // Views are often aliases, like `CStrPtr`, so look at every level of sugar.
  for (QualType Level = Type;;) {
    if (const auto *Typedef = dyn_cast<TypedefType>(Level.getTypePtr());
        Typedef && Matches(Typedef->getDecl()))
      return true;
    // An alias template, like `template <...> using TCStrSpan = ...`.
    if (const auto *Specialization =
            dyn_cast<TemplateSpecializationType>(Level.getTypePtr())) {
      if (const TemplateDecl *Template =
              Specialization->getTemplateName().getAsTemplateDecl();
          Template && Matches(Template))
        return true;
    }
    const QualType Next = Level.getSingleStepDesugaredType(Context);
    if (Next == Level)
      break;
    Level = Next;
  }
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record)
    return false;
  if (const auto *Specialization =
          dyn_cast<ClassTemplateSpecializationDecl>(Record)) {
    // Also a template whose first argument tells it is a view, which an alias
    // that deduction drops can't, `TCStr<CStrTraitsPtr_*>` of `CStrPtr`.
    const TemplateArgumentList &Arguments = Specialization->getTemplateArgs();
    const TagDecl *First = Arguments.size() >= 1 && Arguments[0].getKind() ==
                                                        TemplateArgument::Type
                               ? Arguments[0].getAsType()->getAsTagDecl()
                               : nullptr;
    if (First && llvm::any_of(Patterns, [&](StringRef Pattern) {
          StringRef Template;
          StringRef Argument;
          std::tie(Template, Argument) = Pattern.split('<');
          return Argument.consume_back(">") &&
                 matchesName(Specialization->getSpecializedTemplate(),
                             Template) &&
                 matchesName(First, Argument);
        }))
      return true;
    return Matches(Specialization->getSpecializedTemplate());
  }
  return Matches(Record);
}

bool isDefaultViewType(QualType Type, const ASTContext &Context) {
  return matchesTypeName(Type, vocabulary().ViewTypes.names(), Context);
}

/// The body of \p Function as written, without the code a coroutine adds.
static Stmt *writtenBody(const FunctionDecl *Function) {
  Stmt *Body = Function->getBody();
  if (auto *CoroutineBody = dyn_cast_or_null<CoroutineBodyStmt>(Body))
    Body = CoroutineBody->getBody();
  return Body;
}

static SuspensionFlow suspensionFlowUncached(const FunctionDecl *Function,
                                             ASTContext &Context);

namespace {
/// The control flows of the functions of an AST, which each parameter and
/// local of a function asks about.
struct CFlowFacts {
  llvm::DenseMap<const FunctionDecl *, std::unique_ptr<SuspensionFlow>> Flows;
};
} // namespace

static llvm::DenseMap<const VarDecl *, llvm::SmallVector<const Stmt *, 2>>
parameterRedirections(
    const Stmt *Body, ASTContext &Context,
    llvm::DenseMap<const VarDecl *, llvm::SmallVector<const IfStmt *, 1>>
        &BranchRedirections);

static const SuspensionFlow &suspensionFlow(const FunctionDecl *Function,
                                            ASTContext &Context) {
  std::unique_ptr<SuspensionFlow> &Flow =
      astCache<CFlowFacts>(Context).Flows[Function];
  if (!Flow) {
    Flow = std::make_unique<SuspensionFlow>(
        suspensionFlowUncached(Function, Context));
    if (Stmt *Body = writtenBody(Function)) {
      Flow->Parents = std::make_unique<CParents>(Body);
      Flow->Redirections =
          parameterRedirections(Body, Context, Flow->BranchRedirections);
    }
  }
  return *Flow;
}

static SuspensionFlow suspensionFlowUncached(const FunctionDecl *Function,
                                             ASTContext &Context) {
  SuspensionFlow Flow;
  Flow.Context = &Context;
  llvm::DenseSet<const Stmt *> &After = Flow.After;
  Stmt *Body = writtenBody(Function);
  if (!Body)
    return Flow;
  CFG::BuildOptions Options;
  Options.setAllAlwaysAdd();
  // Exceptions can reach handlers after a suspension.
  Options.AddEHEdges = true;
  Flow.Graph = CFG::buildCFG(Function, Body, &Context, Options);
  const std::unique_ptr<CFG> &Graph = Flow.Graph;
  if (!Graph)
    return Flow;
  Flow.Built = true;
  for (const CFGBlock *Block : *Graph) {
    for (const CFGElement &Element : *Block) {
      if (const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>())
        Flow.Elements.insert(Statement->getStmt());
    }
  }
  const llvm::BitVector Reachable = reachableBlocks(*Graph);
  for (const CFGBlock *Block : *Graph) {
    if (!Reachable.test(Block->getBlockID()))
      continue;
    for (const CFGElement &Element : *Block) {
      if (const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>())
        Flow.ReachableElements.insert(Statement->getStmt());
    }
  }
  const auto SuspendPoints = suspendPoints(Body);

  // Whether each block may end after a suspension, following loops.
  llvm::DenseMap<const CFGBlock *, bool> SuspendedAtEnd;
  auto Entry = [&](const CFGBlock *Block) {
    bool Suspended = false;
    for (const CFGBlock::AdjacentBlock &Predecessor : Block->preds()) {
      if (const CFGBlock *Pred = Predecessor.getReachableBlock())
        Suspended |= SuspendedAtEnd.lookup(Pred);
    }
    return Suspended;
  };
  auto Transfer = [&](const CFGBlock *Block, bool Suspended, bool Collect) {
    for (const CFGElement &Element : *Block) {
      const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>();
      if (!Statement)
        continue;
      if (Collect && Suspended)
        After.insert(Statement->getStmt());
      if (isSuspension(Statement->getStmt()) ||
          SuspendPoints.contains(Statement->getStmt())) {
        Suspended = true;
        if (Collect)
          Flow.Suspends = true;
      }
    }
    return Suspended;
  };
  const llvm::SmallVector<const CFGBlock *, 16> Order = forwardOrder(*Graph);
  for (bool Changed = true; Changed;) {
    Changed = false;
    for (const CFGBlock *Block : Order) {
      if (!Reachable.test(Block->getBlockID()))
        continue;
      if (Transfer(Block, Entry(Block), false) &&
          !SuspendedAtEnd.lookup(Block)) {
        SuspendedAtEnd[Block] = true;
        Changed = true;
      }
    }
  }
  for (const CFGBlock *Block : *Graph) {
    if (Reachable.test(Block->getBlockID()))
      Transfer(Block, Entry(Block), true);
  }
  return Flow;
}

llvm::DenseSet<const Stmt *>
statementsAfterSuspension(const FunctionDecl *Function, ASTContext &Context) {
  return suspensionFlow(Function, Context).After;
}

/// Whether \p S, a statement of the body of the coroutine \p Function, can
/// run after it suspended: also when that can't be told.
static bool mayRunAfterSuspension(const Stmt *S, const FunctionDecl *Function,
                                  ASTContext &Context) {
  const SuspensionFlow &Flow = suspensionFlow(Function, Context);
  return !Flow.Built || !Flow.Elements.contains(S) || Flow.After.contains(S);
}

namespace {

/// Collects the statements of a body that a predicate accepts, in the order
/// they are written, including those in the bodies of the lambdas it makes,
/// but not those of other local classes, which belong to other objects, nor
/// unevaluated operands.
class UseCollector : public OncePerTypeVisitor<UseCollector> {
public:
  explicit UseCollector(llvm::function_ref<bool(const Stmt *)> IsUse)
      : IsUse(IsUse) {}

  llvm::SmallVector<const Stmt *, 8> Uses;

  bool TraverseCXXRecordDecl(CXXRecordDecl *Record) {
    return Record->isLambda()
               ? RecursiveASTVisitor::TraverseCXXRecordDecl(Record)
               : true;
  }
  bool TraverseUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *) {
    return true;
  }
  bool TraverseCXXNoexceptExpr(CXXNoexceptExpr *) { return true; }
  bool TraverseRequiresExpr(RequiresExpr *) { return true; }
  bool TraverseCXXTypeidExpr(CXXTypeidExpr *Typeid) {
    return Typeid->isPotentiallyEvaluated()
               ? RecursiveASTVisitor::TraverseCXXTypeidExpr(Typeid)
               : true;
  }
  bool TraverseTypeLoc(TypeLoc, bool = true) { return true; }
  // Not the object named only to name a static member of it,
  // `this->ms_Value`, `p->fs_Get()` or `(*p).ms_Value`, whose storage isn't
  // accessed, also chosen by a conditional, `(bChoose ? this : p)->ms_Value`,
  // or a comma, of which what else runs is still looked at.
  bool TraverseMemberExpr(MemberExpr *Member) {
    const ValueDecl *Named = Member->getMemberDecl();
    const auto *Method = dyn_cast<CXXMethodDecl>(Named);
    if (isa<VarDecl, EnumConstantDecl>(Named) || (Method && Method->isStatic()))
      return traverseStaticBase(Member->getBase(), 0);
    return RecursiveASTVisitor::TraverseMemberExpr(Member);
  }
  bool traverseStaticBase(Expr *Base, int Depth) {
    Base = Base->IgnoreParenImpCasts();
    if (const auto *Deref = dyn_cast<UnaryOperator>(Base);
        Deref && Deref->getOpcode() == UO_Deref)
      Base = Deref->getSubExpr()->IgnoreParenImpCasts();
    if (isa<CXXThisExpr, DeclRefExpr>(Base))
      return true;
    if (Depth < 16) {
      if (auto *Conditional = dyn_cast<ConditionalOperator>(Base))
        return TraverseStmt(Conditional->getCond()) &&
               traverseStaticBase(Conditional->getTrueExpr(), Depth + 1) &&
               traverseStaticBase(Conditional->getFalseExpr(), Depth + 1);
      if (auto *Comma = dyn_cast<BinaryOperator>(Base);
          Comma && Comma->getOpcode() == BO_Comma)
        return TraverseStmt(Comma->getLHS()) &&
               traverseStaticBase(Comma->getRHS(), Depth + 1);
    }
    return TraverseStmt(Base);
  }
  // Only the arm chosen of `__builtin_choose_expr` and `_Generic`.
  bool TraverseChooseExpr(ChooseExpr *Choose) {
    return Choose->isConditionDependent()
               ? RecursiveASTVisitor::TraverseChooseExpr(Choose)
               : TraverseStmt(Choose->getChosenSubExpr());
  }
  bool TraverseGenericSelectionExpr(GenericSelectionExpr *Selection) {
    return Selection->isResultDependent()
               ? RecursiveASTVisitor::TraverseGenericSelectionExpr(Selection)
               : TraverseStmt(Selection->getResultExpr());
  }
  // Of an `if consteval`, what runs when not constant evaluated, which a
  // coroutine always is.
  bool TraverseIfStmt(IfStmt *If) {
    if (If->isConsteval())
      return TraverseStmt(If->isNegatedConsteval() ? If->getThen()
                                                   : If->getElse());
    return RecursiveASTVisitor::TraverseIfStmt(If);
  }
  // Not what a builtin only looks at the type or constness of,
  // `__builtin_constant_p(Value)` or `__builtin_object_size(pValue, 0)`, but
  // `__builtin_dynamic_object_size`, which can evaluate it.
  bool TraverseCallExpr(CallExpr *Call) {
    switch (Call->getBuiltinCallee()) {
    case Builtin::BI__builtin_constant_p:
    case Builtin::BI__builtin_object_size:
      return true;
    default:
      return RecursiveASTVisitor::TraverseCallExpr(Call);
    }
  }

  bool VisitStmt(Stmt *S) {
    if (IsUse(S))
      Uses.push_back(S);
    return true;
  }

private:
  llvm::function_ref<bool(const Stmt *)> IsUse;
};

} // namespace

static bool capturesCopy(const LambdaExpr *Lambda, const ValueDecl *Variable);
static StringRef callTargetName(const CallExpr *Call);

/// The type of the value a future of type \p Future holds, like `int` of
/// `TCFuture<int>`, or null when it isn't a future.
QualType futureValueType(QualType Future) {
  if (Future.isNull())
    return {};
  if (const auto *Specialization = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
          Future->getAsCXXRecordDecl());
      Specialization && Specialization->getTemplateArgs().size() >= 1 &&
      Specialization->getTemplateArgs()[0].getKind() == TemplateArgument::Type)
    return Specialization->getTemplateArgs()[0].getAsType();
  return {};
}

/// Whether \p Call is the call of `return_value` on the promise that
/// `co_return` makes.
static bool isPromiseReturn(const CallExpr *Call, const CParents &Parents) {
  const Stmt *Parent = Parents.getParent(Call);
  while (isa_and_nonnull<ExprWithCleanups, ImplicitCastExpr, ParenExpr,
                         MaterializeTemporaryExpr, CXXBindTemporaryExpr>(
      Parent))
    Parent = Parents.getParent(Parent);
  const auto *Coreturn = dyn_cast_or_null<CoreturnStmt>(Parent);
  return Coreturn && Coreturn->getPromiseCall() &&
         Coreturn->getPromiseCall()->IgnoreImplicit() == Call;
}

/// Whether the result of \p Call can't keep a reference to its arguments
/// past the suspensions of the caller: it returns a `TCFuture`, whose
/// coroutines the other checks hold to the same rules, also through a
/// pointer or reference to a function.
static bool returnsCheckedFuture(const CallExpr *Call,
                                 const ASTContext &Context) {
  if (const FunctionDecl *Callee = Call->getDirectCallee())
    return returnsProtectedFuture(Callee);
  // Through a pointer or reference to a function, or a pointer to a member
  // function, like `(Object.*pFunction)()`.
  return isProtectedFutureType(Call->getCallReturnType(Context), Context);
}

/// Whether \p Lambda, which isn't a coroutine itself, is called where it is
/// made, `[&Value] { return Value; }()`, also through its call operator by
/// name, `[&Value] { ... }.operator()()`, or by `std::invoke`, so it runs
/// there, with \p Call set to the call.
static bool isCalledWhereMade(const LambdaExpr *Lambda, const CParents &Parents,
                              const CallExpr **Call = nullptr) {
  const FunctionDecl *Operator = Lambda->getCallOperator();
  if (!Operator || isa_and_nonnull<CoroutineBodyStmt>(Operator->getBody()))
    return false;
  const Stmt *Made = Parents.getParent(Lambda);
  while (isa_and_nonnull<ImplicitCastExpr, MaterializeTemporaryExpr,
                         CXXBindTemporaryExpr, ParenExpr>(Made))
    Made = Parents.getParent(Made);
  auto IsLambda = [&](const Expr *E) {
    return E && E->IgnoreImplicit()->IgnoreParens() == Lambda;
  };
  auto Found = [&](const CallExpr *Found) {
    if (Call)
      *Call = Found;
    return true;
  };
  if (const auto *Invoke = dyn_cast_or_null<CXXOperatorCallExpr>(Made))
    return Invoke->getOperator() == OO_Call && Invoke->getNumArgs() >= 1 &&
           IsLambda(Invoke->getArg(0)) && Found(Invoke);
  if (const auto *Member = dyn_cast_or_null<MemberExpr>(Made)) {
    const auto *Method = dyn_cast<CXXMethodDecl>(Member->getMemberDecl());
    const auto *Named =
        dyn_cast_or_null<CXXMemberCallExpr>(Parents.getParent(Member));
    return Method && Method->getOverloadedOperator() == OO_Call && Named &&
           Named->getCallee()->IgnoreParens() == Member && Found(Named);
  }
  if (const auto *Invoke = dyn_cast_or_null<CallExpr>(Made))
    return isStdInvoke(Invoke) && Invoke->getNumArgs() >= 1 &&
           IsLambda(Invoke->getArg(0)) && Found(Invoke);
  return false;
}

/// Whether \p Lambda is held in a local that is only called, `auto fGet =
/// [&Value] { ... }; co_return fGet();`, so it runs where it is called, with
/// \p Calls set to the calls.
static bool onlyCalledInLocal(
    const LambdaExpr *Lambda, const CParents &Parents,
    llvm::SmallVectorImpl<const CXXOperatorCallExpr *> *Calls = nullptr) {
  const Stmt *Made = Parents.getParent(Lambda);
  while (
      isa_and_nonnull<ImplicitCastExpr, MaterializeTemporaryExpr,
                      CXXBindTemporaryExpr, ParenExpr, ExprWithCleanups>(Made))
    Made = Parents.getParent(Made);
  const auto *Declaration = dyn_cast_or_null<DeclStmt>(Made);
  if (!Declaration || !Declaration->isSingleDecl())
    return false;
  const auto *Local = dyn_cast<VarDecl>(Declaration->getSingleDecl());
  if (!Local || !Local->hasLocalStorage() ||
      Local->getType()->isReferenceType())
    return false;
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return false;
  bool OnlyCalled = true;
  std::function<void(const Stmt *, const Stmt *)> Visit =
      [&](const Stmt *S, const Stmt *Above) {
        if (!S || !OnlyCalled)
          return;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
            Ref && Ref->getDecl() == Local) {
          const auto *Invoke = dyn_cast_or_null<CXXOperatorCallExpr>(Above);
          if (!Invoke || Invoke->getOperator() != OO_Call ||
              Invoke->getArg(0)->IgnoreParenImpCasts() != Ref)
            OnlyCalled = false;
          else if (Calls)
            Calls->push_back(Invoke);
          return;
        }
        const Stmt *Next = isa<ImplicitCastExpr, ParenExpr>(S) ? Above : S;
        for (const Stmt *Child : childrenOnce(S))
          Visit(Child, Next);
      };
  Visit(Function->getBody(), nullptr);
  return OnlyCalled;
}

/// Whether the occurrence \p Use of borrowed data only uses a value of it,
/// so nothing keeps a reference to the data: it is read as a scalar, copied
/// or moved into a value of a type that is not a view, given to a function
/// that returns a `TCFuture`, or a member of it is used that way. A pointer or
/// view read from the data, a reference bound to it, its address, a view or
/// other object made from it, or an awaiter of it keeps a reference.
static constexpr llvm::StringLiteral HandleTypes[] = {
    "TCActor",    "TCWeakActor", "TCDistributedActor", "TCDistributedActorWrapper",
    "TCPromise",  "CThisActor"};

static bool isValueUse(const Expr *Use, const CParents &Parents,
                       const BorrowRoots &Roots,
                       const llvm::SmallPtrSetImpl<const Expr *> &Awaited,
                       const ASTContext &Context, QualType Result) {
  auto IsView = [&](QualType Type) {
    return Roots.IsView && !Type.isNull() &&
           Roots.IsView(Type.getNonReferenceType());
  };
  auto RefersElsewhere = [&](QualType Type) {
    return !Type.isNull() &&
           (Type->isPointerType() || Type->isReferenceType() || IsView(Type));
  };
  // A result that can refer to anything, which a pointer read from the
  // storage of a root can reach through a copy, like `auto *pCopy = pValue;
  // co_return pCopy;`.
  const bool ResultCanRefer =
      !Result.isNull() && (Result->isReferenceType() || RefersElsewhere(Result));
  // What `co_return` gives the result: a copy, unless it, or the result, is a
  // pointer or view, or can be one.
  auto IsResultValue = [&](QualType Type) {
    Type = Type.getNonReferenceType();
    return !RefersElsewhere(Type) &&
           (Result.isNull() ||
            (!RefersElsewhere(Result) && !Result->isReferenceType()));
  };
  // A result that can't refer to the data.
  auto IsValue = [&](QualType Type) {
    return !Type.isNull() &&
           (Type->isVoidType() || Type->isScalarType()) &&
           !Type->isPointerType() && !Type->isMemberPointerType();
  };
  // The value of a pointer that is a root, when what pointers point to isn't
  // the data, like the copy of a pointer that a lambda captures, `[pValue =
  // &g_Value]`, read as it is, which points where it was made to.
  if (!Roots.PointerValues && isa<DeclRefExpr>(Use) &&
      Use->getType()->isPointerType())
    if (const auto *Read =
            dyn_cast_or_null<ImplicitCastExpr>(Parents.getParent(Use));
        Read && Read->getCastKind() == CK_LValueToRValue)
      return true;
  // Handles, like `TCActor`, are copied by what they are given to.
  // A receiver, like `pPromise->f_SetResult(...)`, can be reached through a
  // pointer, but a pointer to a handle given away isn't a handle.
  auto IsHandle = [&](const Expr *E, bool Receiver = false) {
    QualType Type = E->getType();
    if (Receiver && !Type.isNull() && Type->isPointerType())
      Type = Type->getPointeeType();
    const StringRef Name = recordName(Type);
    return !Name.empty() &&
           llvm::is_contained(HandleTypes, Name);
  };
  // What `co_return` gives is stored as the result, which must not be able to
  // refer to anything, whatever the operand makes on the way.
  // Not when it is read as a scalar on the way, `_Value ? g_pData : nullptr`
  // or `&g_Values[_Index]`, which can't refer to it.
  // Nor when a member function of it whose body can be seen gives only what
  // has static storage, `char const *f_Name() const { return "Name"; }`,
  // and that runs as it is: not virtual, final, called by its name,
  // `Object.CObject::f_Name()`, or on an object of its type, `Value.f_Name()`.
  auto GivesOnlyStatic = [](const CXXMemberCallExpr *Call) {
    const CXXMethodDecl *Method = Call->getMethodDecl();
    return Method && !callsOverride(Call) && returnsOnlyStatic(Method);
  };
  if (!Result.isNull() &&
      (RefersElsewhere(Result) || Result->isReferenceType())) {
    const Stmt *Child = Use;
    for (const Stmt *S = Parents.getParent(Use); S;
         Child = S, S = Parents.getParent(S)) {
      if (const auto *Read = dyn_cast<ImplicitCastExpr>(S);
          Read && Read->getCastKind() == CK_LValueToRValue &&
          IsValue(Read->getType()))
        break;
      if (const auto *Member = dyn_cast<MemberExpr>(S);
          Member && isa<Expr>(Child) &&
          Member->getBase()->IgnoreParenImpCasts() ==
              cast<Expr>(Child)->IgnoreParenImpCasts())
        if (const auto *Call =
                dyn_cast_or_null<CXXMemberCallExpr>(Parents.getParent(Member));
            Call && GivesOnlyStatic(Call))
          break;
      if (isa<CoreturnStmt>(S))
        return false;
      if (!isa<Expr>(S))
        break;
    }
  }
  const Expr *Node = Use;
  for (int Depth = 0; Depth < 32; ++Depth) {
    const Stmt *Parent = Parents.getParent(Node);
    // The last statement of a statement expression, `({ &_Value; })`, is its
    // value, which goes on where the statement expression is.
    if (const auto *Block = dyn_cast_or_null<CompoundStmt>(Parent);
        Block && !Block->body_empty() && Block->body_back() == Node)
      if (const auto *Statement =
              dyn_cast_or_null<StmtExpr>(Parents.getParent(Block))) {
        Node = Statement;
        continue;
      }
    const auto *ParentExpr = dyn_cast_or_null<Expr>(Parent);
    if (!ParentExpr) {
      // A local that it initializes itself, like a reference bound to it,
      // `auto &Ref = _Value`, or a pointer to it, `auto *pValue = &_Value`.
      if (const auto *Declaration = dyn_cast_or_null<DeclStmt>(Parent)) {
        for (const Decl *D : Declaration->decls()) {
          const auto *Variable = dyn_cast<VarDecl>(D);
          if (!Variable || !Variable->getInit() ||
              Variable->getInit()->IgnoreParens() != Node->IgnoreParens())
            continue;
          const QualType Type = Variable->getType();
          // A scalar can only hold a value.
          if (IsValue(Type))
            return true;
          // An object of the same type made from it holds a value, unless it
          // is a view.
          return !Type->isReferenceType() && !Type->isPointerType() &&
                 !IsView(Type) &&
                 Variable->getASTContext().hasSameUnqualifiedType(
                     Type, Node->getType().getNonReferenceType());
        }
        return false;
      }
      // A statement that drops the value, like `Value;`, keeps nothing.
      return Parent && !isa<ReturnStmt, CoreturnStmt>(Parent) &&
             isa<CompoundStmt, IfStmt, WhileStmt, ForStmt, DoStmt, SwitchStmt,
                 CaseStmt, DefaultStmt, LabelStmt>(Parent);
    }
    auto Continue = [&] { Node = ParentExpr; };
    // The object of a member function whose body gives only what has static
    // storage, `Info.f_Name()`, isn't kept.
    if (const auto *Member = dyn_cast<MemberExpr>(ParentExpr);
        Member &&
        Member->getBase()->IgnoreParenImpCasts() == Node->IgnoreParenImpCasts())
      if (const auto *Call =
              dyn_cast_or_null<CXXMemberCallExpr>(Parents.getParent(Member));
          Call && GivesOnlyStatic(Call))
        return true;
    if (isa<ParenExpr, MaterializeTemporaryExpr, CXXBindTemporaryExpr,
            FullExpr>(ParentExpr)) {
      Continue();
      continue;
    }
    if (const auto *Cast = dyn_cast<ImplicitCastExpr>(ParentExpr)) {
      // Whether a pointer is null is a value.
      if (Cast->getCastKind() == CK_PointerToBoolean ||
          Cast->getCastKind() == CK_MemberPointerToBoolean)
        return true;
      if (Cast->getCastKind() != CK_LValueToRValue) {
        Continue();
        continue;
      }
      // Reading a value copies it. A pointer or view read keeps referring to
      // the data, unless it is the root itself and the data is its storage,
      // like a pointer copied into a closure.
      if (!RefersElsewhere(Cast->getType()))
        return true;
      if (Node == Use && !Roots.PointerValues && !ResultCanRefer)
        return true;
      Continue();
      continue;
    }
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(ParentExpr)) {
      // Discarded, `(void)_pValue`, which keeps nothing.
      if (Cast->getType()->isVoidType())
        return true;
      // A value, but not an address cast to an integer.
      if (IsValue(Cast->getType()) && !Cast->isGLValue())
        return !Cast->getSubExpr()->getType()->isPointerType();
      Continue();
      continue;
    }
    if (isa<MemberExpr, ArraySubscriptExpr>(ParentExpr)) {
      // A member or element of the data is the data too; an index is read.
      if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(ParentExpr);
          Subscript && Subscript->getIdx()->IgnoreParenImpCasts() ==
                           Node->IgnoreParenImpCasts())
        return true;
      Continue();
      continue;
    }
    if (const auto *Unary = dyn_cast<UnaryOperator>(ParentExpr)) {
      if (Unary->getOpcode() == UO_AddrOf || Unary->getOpcode() == UO_Deref) {
        Continue();
        continue;
      }
      // `++Value` gives the data itself, which is followed further.
      if (Unary->isGLValue()) {
        Continue();
        continue;
      }
      // Changing or computing with it gives a value, unless it computes a
      // pointer or view.
      return !RefersElsewhere(Unary->getType());
    }
    if (const auto *Binary = dyn_cast<BinaryOperator>(ParentExpr)) {
      // Storing into the data keeps nothing, but the assignment gives the data
      // itself, which is followed further; storing a reference to it keeps
      // it.
      if (Binary->isAssignmentOp()) {
        if (Binary->getLHS()->IgnoreParens() != Node->IgnoreParens())
          return false;
        Continue();
        continue;
      }
      if (Binary->getOpcode() == BO_Comma) {
        if (Binary->getLHS()->IgnoreParens() == Node->IgnoreParens())
          return true;
        Continue();
        continue;
      }
      // `Data.*pMember` is the data too.
      if (RefersElsewhere(Binary->getType()) || Binary->isGLValue() ||
          Binary->isPtrMemOp()) {
        Continue();
        continue;
      }
      return true;
    }
    // A fold, `(_Base + ... + p_Values)`, computes like its operator.
    if (const auto *Fold = dyn_cast<CXXFoldExpr>(ParentExpr)) {
      if (RefersElsewhere(Fold->getType()) || Fold->isGLValue()) {
        Continue();
        continue;
      }
      return true;
    }
    if (const auto *Conditional =
            dyn_cast<AbstractConditionalOperator>(ParentExpr)) {
      if (Conditional->getCond()->IgnoreParens() == Node->IgnoreParens() ||
          (!Conditional->isGLValue() &&
           !RefersElsewhere(Conditional->getType())))
        return true;
      Continue();
      continue;
    }
    // A by-value capture of an array copies its elements.
    if (isa<OpaqueValueExpr>(ParentExpr)) {
      Continue();
      continue;
    }
    if (const auto *Loop = dyn_cast<ArrayInitLoopExpr>(ParentExpr)) {
      const QualType Element = Loop->getType()->getAsArrayTypeUnsafe()
                                   ? Loop->getType()
                                         ->getAsArrayTypeUnsafe()
                                         ->getElementType()
                                   : QualType();
      return !Element.isNull() && !RefersElsewhere(Element);
    }
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(ParentExpr)) {
      // A handle made from another, like `TCActor<>` from `TCActor<CActor>`.
      if (IsHandle(Construct) && IsHandle(Node) && Construct->getNumArgs() >= 1 &&
          Construct->getArg(0)->IgnoreParenImpCasts() ==
              Node->IgnoreParenImpCasts())
        return true;
      // A copy or move owns its data, unless it is a view.
      const CXXConstructorDecl *Constructor = Construct->getConstructor();
      return Constructor && Constructor->isCopyOrMoveConstructor() &&
             !IsView(Construct->getType()) && Construct->getNumArgs() >= 1 &&
             Construct->getArg(0)->IgnoreParenImpCasts() ==
                 Node->IgnoreParenImpCasts();
    }
    if (const auto *Call = dyn_cast<CallExpr>(ParentExpr)) {
      const FunctionDecl *Callee = Call->getDirectCallee();
      const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
      // The object of a member function, also of an operator.
      const Expr *Object = nullptr;
      if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call))
        Object = Member->getImplicitObjectArgument();
      else if (isa<CXXOperatorCallExpr>(Call) && Method &&
               Method->isImplicitObjectMemberFunction() &&
               Call->getNumArgs() >= 1)
        Object = Call->getArg(0);
      // A member function called through `Data.f_Get`, whose member
      // expression is the callee.
      const bool CalledOn =
          (Object &&
           Object->IgnoreParenImpCasts() == Node->IgnoreParenImpCasts()) ||
          Call->getCallee()->IgnoreParenImpCasts() == Node->IgnoreParenImpCasts();
      if (CalledOn) {
        if (IsValue(Call->getType()) && !Call->isGLValue())
          return true;
        const bool Handle =
            IsHandle(Node, /*Receiver=*/true) || (Object && IsHandle(Object, true));
        if (Call->isGLValue() || RefersElsewhere(Call->getType())) {
          if (Handle)
            return true;
          Continue();
          continue;
        }
        // An object returned by value is a value, unless it is awaited: an
        // awaiter can keep a reference to what made it.
        return returnsCheckedFuture(Call, Context) || Handle ||
               !Awaited.contains(Call);
      }
      // A copy or move assignment into a value owns what it copies, unless it
      // is a view.
      if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
          Operator && Method && Operator->getOperator() == OO_Equal &&
          (Method->isCopyAssignmentOperator() ||
           Method->isMoveAssignmentOperator()) &&
          Operator->getNumArgs() == 2 &&
          Operator->getArg(1)->IgnoreParenImpCasts() ==
              Node->IgnoreParenImpCasts() &&
          !IsView(Operator->getArg(0)->getType()))
        return true;
      // Also called explicitly, `Copy.operator = (Value)`.
      if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
          Member && Method &&
          (Method->isCopyAssignmentOperator() ||
           Method->isMoveAssignmentOperator()) &&
          Member->getNumArgs() == 1 &&
          Member->getArg(0)->IgnoreParenImpCasts() ==
              Node->IgnoreParenImpCasts() &&
          !IsView(Member->getImplicitObjectArgument()->getType()))
        return true;
      // `fg_Move` and `fg_Forward` give what they get.
      const StringRef Name = calleeName(Call);
      // `co_return Value` gives the value to the promise, which stores it in
      // the result: a copy, unless it is a pointer or view.
      if (callTargetName(Call) == "return_value" &&
          isPromiseReturn(Call, Parents))
        return IsResultValue(Node->getType());
      // Exchanging or swapping values keeps no reference to them, but a
      // pointer or view still refers where it did: the exchanged out value
      // is followed further, and one stored in another place is kept.
      if (isExchangeFunction(Name) || isSwapFunction(Name)) {
        const bool Stored = Call->getNumArgs() < 1 ||
                            Call->getArg(0)->IgnoreParenImpCasts() !=
                                Node->IgnoreParenImpCasts();
        // A view root still views the data, unlike a pointer root, whose
        // storage the data is.
        const bool Refers =
            RefersElsewhere(Node->getType()) &&
            !(Node == Use && !Roots.PointerValues && !ResultCanRefer &&
              !IsView(Node->getType()));
        if (!Refers)
          return true;
        if (Stored || isSwapFunction(Name))
          return false;
        Continue();
        continue;
      }
      if (Call->getNumArgs() == 1 &&
          (isMoveFunction(Name) || isForwardFunction(Name))) {
        Continue();
        continue;
      }
      // An argument bound to a reference, or a pointer or view: only a
      // function that returns a TCFuture is held to not keep it, a handle is
      // copied, and so is what a handle is given, like the arguments of an
      // actor call or the message of `Promise % "Message"`.
      const Expr *Receiver = Object;
      if (!Receiver && isa<CXXOperatorCallExpr>(Call) && Call->getNumArgs() >= 1)
        Receiver = Call->getArg(0);
      return returnsCheckedFuture(Call, Context) || IsHandle(Node) ||
             (Receiver && IsHandle(Receiver, /*Receiver=*/true));
    }
    // A capture of a lambda called where it is made, `[&Value] { return
    // Value; }()`, which is gone at the end of the call, what its body does
    // with it, which is checked by itself.
    if (const auto *Lambda = dyn_cast<LambdaExpr>(ParentExpr);
        Lambda && llvm::is_contained(Lambda->capture_inits(), Node))
      return isCalledWhereMade(Lambda, Parents);
    // Anything else, like an aggregate, a lambda capture or an awaiter, can
    // keep a reference.
    return false;
  }
  return false;
}

namespace {

/// Where a use in the body of a coroutine is evaluated.
struct UsePlace {
  /// The use is of a copy that a lambda around it captured, not of the
  /// borrowed data.
  bool OfCopy = false;
  /// The use can run: the statement of the control flow it is evaluated with
  /// is reachable, or the control flow doesn't cover it.
  bool CanRun = true;
  /// The use can run after a suspension: it is evaluated after one, or in the
  /// body of a lambda or block, which runs whenever it is called.
  bool After = true;
};

} // namespace

/// Where \p Use is evaluated, following the lambdas around it: a lambda that
/// captures what it refers to by copy holds its own copy; one that captures it
/// by reference refers further out.
static UsePlace usePlace(const Stmt *Use, const CParents &Parents,
                         const SuspensionFlow &Flow,
                         const BorrowRoots *Borrow) {
  const ASTContext &Context = *Flow.Context;
  UsePlace Place;
  bool InLambda = false;
  const Stmt *Child = nullptr;
  const auto *Ref = dyn_cast<DeclRefExpr>(Use);
  // A copy of a pointer or view still refers to the borrowed data when that
  // is what it points to.
  const QualType DeclType =
      Ref ? Ref->getDecl()->getType().getNonReferenceType() : QualType();
  const bool CopyRefers =
      Ref && Borrow && Borrow->PointerValues &&
      (DeclType->isPointerType() || (Borrow->IsView && Borrow->IsView(DeclType)));
  // The object of a member function, also through its members, whose
  // arguments suspend, `_Value.f_Read(co_await fg_Work())`, is used where the
  // function runs, after them, and so is what an argument by reference or
  // pointer refers to, `fg_Read(_Value, co_await fg_Work())`.
  bool CalledAfter = false;
  for (const Stmt *Node = Use, *Parent = Parents.getParent(Use); Parent;
       Node = Parent, Parent = Parents.getParent(Parent)) {
    if (isa<MemberExpr, ImplicitCastExpr, ParenExpr, MaterializeTemporaryExpr>(
            Parent))
      continue;
    // Also through a cast to a reference, `static_cast<int const &>(_Value)`,
    // or what gives it as it is, `fg_Move(_Value)`.
    // Also a cast of a pointer, `static_cast<int *>(_pValue)`, which points
    // where it does, and either value of a conditional, `bChoose ? _pValue
    // : _pOther`.
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Parent);
        Cast && (Cast->isGLValue() || (Cast->getType()->isPointerType() &&
                                       Cast->getSubExpr()->getType()->isPointerType())))
      continue;
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Parent);
        Conditional && Conditional->getCond() != Node &&
        (Conditional->isGLValue() || Conditional->getType()->isPointerType()))
      continue;
    if (const auto *Forward = dyn_cast<CallExpr>(Parent);
        Forward && Forward->getNumArgs() == 1 && Forward->isGLValue() &&
        isMoveOrForwardFunction(calleeName(Forward)))
      continue;
    if (const auto *Address = dyn_cast<UnaryOperator>(Parent);
        Address && Address->getOpcode() == UO_AddrOf)
      continue;
    // Pointer arithmetic, `_pValue + 1`, points into what the pointer does.
    if (const auto *Binary = dyn_cast<BinaryOperator>(Parent);
        Binary && Binary->isAdditiveOp() && Binary->getType()->isPointerType())
      continue;
    // What it points to accessed after a suspension in the same expression,
    // `_pValue[(co_await fg_Work(), 0)]`.
    if (accessedAfterSuspension(Parent, Context) == Node) {
      CalledAfter = true;
      break;
    }
    // Also what a pointer to it points to, as the object of a call, `(*this)
    // .f_Read(...)` or `(&_Value)[0].f_Read(...)`.
    if (const auto *Deref = dyn_cast<UnaryOperator>(Parent);
        Deref && Deref->getOpcode() == UO_Deref)
      continue;
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Parent);
        Subscript && Node == Subscript->getBase())
      continue;
    // The left side of an assignment, also a compound one, whose right side
    // suspends, `_Value += co_await fg_Work()`, which is read and written
    // after it.
    if (const auto *Assign = dyn_cast<BinaryOperator>(Parent);
        Assign && Assign->isAssignmentOp() && Node == Assign->getLHS() &&
        suspendsIn(Assign->getRHS(), Context)) {
      CalledAfter = true;
      break;
    }
    if (const auto *Assign = dyn_cast<CXXOperatorCallExpr>(Parent);
        Assign && Assign->isAssignmentOp() && Assign->getNumArgs() == 2 &&
        Node == Assign->getArg(0) && suspendsIn(Assign->getArg(1), Context)) {
      CalledAfter = true;
      break;
    }
    // The object of a call through a pointer to a member function,
    // `(this->*fRead)(co_await fg_Work())`.
    if (const auto *Pointer = dyn_cast<BinaryOperator>(Parent);
        Pointer && Pointer->isPtrMemOp() && Node == Pointer->getLHS())
      continue;
    auto Suspends = [&](llvm::ArrayRef<const Expr *> Arguments) {
      return llvm::any_of(Arguments, [&](const Expr *Argument) {
        return suspendsIn(Argument, Context);
      });
    };
    // An argument given by reference or pointer while another one suspends.
    // Also through a pointer to a function, `fRead(_Value, co_await ...)`,
    // or to a constructor.
    auto GivenOn = [&](llvm::ArrayRef<const Expr *> Arguments,
                       const Expr *Call) {
      for (unsigned iArgument = 0; iArgument < Arguments.size(); ++iArgument) {
        if (Arguments[iArgument] != Node)
          continue;
        const QualType Type = argumentParameterType(Call, iArgument);
        return !Type.isNull() &&
               (Type->isReferenceType() || Type->isPointerType()) &&
               Suspends(Arguments);
      }
      return false;
    };
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Parent);
        Call && Node == Call->getCallee())
      CalledAfter = Suspends(llvm::ArrayRef(Call->getArgs(), Call->getNumArgs()));
    else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Parent);
             Operator && Operator->getNumArgs() >= 1 &&
             Node == Operator->getArg(0) &&
             isa_and_nonnull<CXXMethodDecl>(Operator->getDirectCallee()))
      CalledAfter = Suspends(llvm::ArrayRef(Operator->getArgs() + 1,
                                            Operator->getNumArgs() - 1));
    else if (const auto *Call = dyn_cast<CallExpr>(Parent))
      CalledAfter =
          GivenOn(llvm::ArrayRef(Call->getArgs(), Call->getNumArgs()), Call);
    else if (const auto *Construct = dyn_cast<CXXConstructExpr>(Parent))
      CalledAfter = GivenOn(
          llvm::ArrayRef(Construct->getArgs(), Construct->getNumArgs()),
          Construct);
    break;
  }
  for (const Stmt *Node = Use; Node;
       Child = Node, Node = Parents.getParent(Node)) {
    if (const auto *Lambda = dyn_cast<LambdaExpr>(Node);
        Lambda && Child && Child == Lambda->getBody()) {
      // Also `this` in one that keeps the object alive, `[this, pThis =
      // TCSharedPointer<CState>{fg_Explicit(this)}]`, whose own check tells
      // where it runs.
      if ((Ref && !CopyRefers && capturesCopy(Lambda, Ref->getDecl())) ||
          (isa<CXXThisExpr>(Use) &&
           (capturesCopy(Lambda, nullptr) || keepsObjectAlive(Lambda) ||
            (Borrow && Borrow->ThisCaptureIsCopy &&
             llvm::any_of(Lambda->captures(), [](const LambdaCapture &Capture) {
               return Capture.capturesThis();
             }))))) {
        Place.OfCopy = true;
        return Place;
      }
      // One called where it is made, `[&Value] { return Value; }()`, runs its
      // body there, after what it is given, which can suspend, `[&Value](int)
      // { ... }(co_await fg_Work())`.
      const CallExpr *Call = nullptr;
      // Not in what a constant condition doesn't choose, `false ? co_await
      // fg_Suspend() : 0`.
      std::function<bool(const Stmt *)> Suspends = [&](const Stmt *S) {
        if (!S || isa<LambdaExpr>(S))
          return false;
        if (const auto *Conditional = dyn_cast<ConditionalOperator>(S);
            Conditional && Flow.Context &&
            !Conditional->getCond()->isValueDependent()) {
          bool Chosen = false;
          if (Conditional->getCond()->EvaluateAsBooleanCondition(Chosen,
                                                                 *Flow.Context))
            return Suspends(Conditional->getCond()) ||
                   Suspends(Chosen ? Conditional->getTrueExpr()
                                   : Conditional->getFalseExpr());
        }
        // Nor the right side of `||` or `&&` that a constant left side
        // decides, `true || (co_await fg_Suspend(), false)`.
        if (const auto *Logical = dyn_cast<BinaryOperator>(S);
            Logical && Logical->isLogicalOp() && Flow.Context &&
            !Logical->getLHS()->isValueDependent()) {
          bool Left = false;
          if (Logical->getLHS()->EvaluateAsBooleanCondition(Left,
                                                            *Flow.Context) &&
              Left == (Logical->getOpcode() == BO_LOr))
            return Suspends(Logical->getLHS());
        }
        return isSuspension(S) || llvm::any_of(childrenOnce(S), Suspends);
      };
      // One held in a local that is only called, `auto fGet = [&Value] {
      // ... }; co_return fGet();`, runs where it is called, which in a
      // coroutine that never suspends is before any suspension.
      auto OnlyCalledInLocal = [&] {
        return onlyCalledInLocal(Lambda, Parents);
      };
      if (!isCalledWhereMade(Lambda, Parents, &Call)) {
        if (!(Flow.Built && Flow.After.empty() && OnlyCalledInLocal()))
          InLambda = true;
      } else if (llvm::any_of(Call->arguments(), [&](const Expr *Argument) {
                   return Argument->IgnoreParenImpCasts() != Lambda &&
                          Suspends(Argument);
                 }))
        InLambda = true;
      continue;
    }
    if (isa<BlockExpr>(Node) && Child)
      InLambda = true;
    if (Flow.Built && Flow.Elements.contains(Node)) {
      Place.CanRun = Flow.ReachableElements.contains(Node);
      Place.After = InLambda || CalledAfter || Flow.After.contains(Node);
      return Place;
    }
  }
  return Place;
}

/// Whether \p Use refers to the borrowed data and can run after a
/// suspension.
static bool useAfterSuspension(const Stmt *Use, const CParents &Parents,
                               const SuspensionFlow &Flow,
                               const BorrowRoots *Borrow) {
  const UsePlace Place = usePlace(Use, Parents, Flow, Borrow);
  return !Place.OfCopy && Place.CanRun && Place.After;
}

/// Whether \p Use refers to the borrowed data and can run.
static bool useCanRun(const Stmt *Use, const CParents &Parents,
                      const SuspensionFlow &Flow, const BorrowRoots *Borrow) {
  const UsePlace Place = usePlace(Use, Parents, Flow, Borrow);
  return !Place.OfCopy && Place.CanRun;
}

namespace {
/// An aggregate initialization, with braces, like `CResult{Value}`, or with
/// parentheses, like `CResult(Value)`.
struct CAggregateInit {
  llvm::ArrayRef<Expr *> Inits;
  /// The member of a union it initializes.
  const FieldDecl *UnionField = nullptr;
  QualType Type;
};
} // namespace

static std::optional<CAggregateInit> aggregateInit(const Expr *E) {
  if (const auto *List = dyn_cast<InitListExpr>(E))
    return CAggregateInit{const_cast<InitListExpr *>(List)->inits(),
                          List->getInitializedFieldInUnion(), List->getType()};
  if (const auto *List = dyn_cast<CXXParenListInitExpr>(E))
    return CAggregateInit{const_cast<CXXParenListInitExpr *>(List)
                              ->getUserSpecifiedInitExprs(),
                          List->getInitializedFieldInUnion(), List->getType()};
  return std::nullopt;
}

/// The local that \p Decl refers to: itself, or what a local reference to
/// it is bound to, like `pResult` of `int const *&pAlias = pResult`, which
/// assigning it assigns.
static const ValueDecl *referencedLocal(const ValueDecl *Decl) {
  // Each refers to one declared before it, so the chain ends, however long.
  for (int Depth = 0; Depth < 4096; ++Depth) {
    const auto *Local = dyn_cast<VarDecl>(Decl);
    if (!Local || !Local->hasLocalStorage() ||
        !Local->getType()->isReferenceType() || !Local->getInit())
      return Decl;
    const auto *Ref =
        dyn_cast<DeclRefExpr>(Local->getInit()->IgnoreParenImpCasts());
    if (!Ref)
      return Decl;
    Decl = Ref->getDecl();
  }
  return Decl;
}

/// Whether \p Constructor can keep a reference to what its parameter
/// \p iParameter refers to: not when it can be looked into and only copies it
/// into members that hold values, like `m_Value(_Value)` of an `int`, or
/// reads its value in its body, like `m_Value = _Value;`.
static bool constructorMayKeepUncached(const CXXConstructorDecl *Constructor,
                                       unsigned iParameter, int Depth);

/// Like constructorMayKeepUncached, each constructor and parameter told once,
/// which the constructors of bases that many classes share, `struct C1 :
/// virtual B0, virtual C0`, are met through a number of paths that grows
/// exponentially with the depth.
static bool constructorMayKeep(const CXXConstructorDecl *Constructor,
                               unsigned iParameter, int Depth = 0) {
  struct CMayKeep {
    llvm::DenseMap<std::pair<const CXXConstructorDecl *, unsigned>, bool>
        Results;
  };
  auto &Results = astCache<CMayKeep>(Constructor->getASTContext()).Results;
  const std::pair<const CXXConstructorDecl *, unsigned> Key(
      Constructor->getCanonicalDecl(), iParameter);
  if (auto It = Results.find(Key); It != Results.end())
    return It->second;
  // One that reaches itself may keep it.
  Results[Key] = true;
  const bool MayKeep =
      constructorMayKeepUncached(Constructor, iParameter, Depth);
  Results[Key] = MayKeep;
  return MayKeep;
}

static bool constructorMayKeepUncached(const CXXConstructorDecl *Constructor,
                                       unsigned iParameter, int Depth) {
  const FunctionDecl *Definition = nullptr;
  if (!Constructor->hasBody(Definition) || !Definition ||
      iParameter >= Definition->getNumParams() || Depth > 256)
    return true;
  const auto *Defined = cast<CXXConstructorDecl>(Definition);
  const ParmVarDecl *Parameter = Defined->getParamDecl(iParameter);
  std::function<bool(const Stmt *)> Mentions = [&](const Stmt *S) {
    if (!S)
      return false;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Ref->getDecl() == Parameter)
      return true;
    return llvm::any_of(childrenOnce(S), Mentions);
  };
  // Whether \p Local, a pointer or reference of the constructor, is only
  // read through for numbers or enums, `*pValue` of `int const *pValue =
  // &_Value`, which keeps nothing past the constructor.
  ASTContext &Context = Defined->getASTContext();
  // Also through a local made from it as it is, `int const &Second = First`,
  // only read through in turn.
  std::function<bool(const VarDecl *, int)> OnlyReadThroughFrom;
  auto OnlyReadThrough = [&](const VarDecl *Local) {
    return OnlyReadThroughFrom(Local, 0);
  };
  OnlyReadThroughFrom = [&](const VarDecl *Local, int AliasDepth) {
    bool Only = true;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || !Only)
        return;
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
          Ref && Ref->getDecl() == Local) {
        // Read through, `*pValue`, or read, `Alias` of a reference, as a
        // number or an enum: whether implicit casts are among the parents or
        // not, as for code as it is spelled.
        DynTypedNode Node = DynTypedNode::create(*Ref);
        int nDerefs = Local->getType()->isPointerType() ? 1 : 0;
        for (int Level = 0; Level < 8; ++Level) {
          const auto Parents = instantiatedParents(Node, Context);
          const Expr *Child = Node.get<Expr>();
          if (Parents.empty() || !Child) {
            Only = false;
            return;
          }
          const DynTypedNode &Parent = Parents[0];
          if (Parent.get<ParenExpr>() ||
              (Parent.get<ImplicitCastExpr>() &&
               (Parent.get<ImplicitCastExpr>()->getCastKind() !=
                    CK_LValueToRValue ||
                nDerefs > 0))) {
            Node = Parent;
            continue;
          }
          if (const auto *Alias = Parent.get<VarDecl>();
              Alias && Alias->getInit() == Child && Alias->hasLocalStorage() &&
              nDerefs == (Local->getType()->isPointerType() ? 1 : 0) &&
              Alias->getType()->isPointerType() ==
                  Local->getType()->isPointerType() &&
              Alias->getType()->isReferenceType() ==
                  Local->getType()->isReferenceType()) {
            Only =
                AliasDepth < 16 && OnlyReadThroughFrom(Alias, AliasDepth + 1);
            return;
          }
          if (const auto *Deref = Parent.get<UnaryOperator>();
              Deref && Deref->getOpcode() == UO_Deref && nDerefs > 0) {
            --nDerefs;
            Node = Parent;
            continue;
          }
          if (nDerefs > 0 || !(Child->getType()->isArithmeticType() ||
                               Child->getType()->isEnumeralType())) {
            Only = false;
            return;
          }
          // Its value read: converted, or an operand that doesn't change or
          // refer to it, like the right side of `m_Value = *pValue`.
          if (const auto *Read = Parent.get<ImplicitCastExpr>())
            Only = Read->getCastKind() == CK_LValueToRValue;
          else if (const auto *Binary = Parent.get<BinaryOperator>())
            Only = !(Binary->isAssignmentOp() &&
                     Binary->getLHS()->IgnoreParens() == Child) &&
                   Binary->getOpcode() != BO_Comma;
          else if (const auto *Unary = Parent.get<UnaryOperator>())
            Only = !Unary->isIncrementDecrementOp() &&
                   Unary->getOpcode() != UO_AddrOf;
          else
            Only = false;
          return;
        }
        Only = false;
        return;
      }
      for (const Stmt *Child : childrenOnce(S))
        Visit(Child);
    };
    Visit(Defined->getBody());
    return Only;
  };
  // Other than reading a value, of a number or an enum, which refers to
  // nothing, also through a local of it only read through.
  std::function<bool(const Stmt *)> Keeps = [&](const Stmt *S) {
    if (!S)
      return false;
    if (const auto *Declaration = dyn_cast<DeclStmt>(S);
        Declaration && Declaration->isSingleDecl())
      if (const auto *Local = dyn_cast<VarDecl>(Declaration->getSingleDecl());
          Local && Local->getInit() &&
          (Local->getType()->isPointerType() ||
           Local->getType()->isReferenceType())) {
        const Expr *Init = Local->getInit()->IgnoreParenImpCasts();
        if (const auto *Address = dyn_cast<UnaryOperator>(Init);
            Address && Address->getOpcode() == UO_AddrOf)
          Init = Address->getSubExpr()->IgnoreParenImpCasts();
        if (const auto *Ref = dyn_cast<DeclRefExpr>(Init);
            Ref && Ref->getDecl() == Parameter && OnlyReadThrough(Local))
          return false;
      }
    if (const auto *Read = dyn_cast<ImplicitCastExpr>(S);
        Read && Read->getCastKind() == CK_LValueToRValue &&
        (Read->getType()->isArithmeticType() ||
         Read->getType()->isEnumeralType())) {
      const auto *Ref =
          dyn_cast<DeclRefExpr>(Read->getSubExpr()->IgnoreParens());
      if (Ref && Ref->getDecl() == Parameter)
        return false;
      // Also either value of a conditional that is read, `_bUse ? _Value :
      // g_Default`, which reads what it chooses.
      if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(
              Read->getSubExpr()->IgnoreParens())) {
        auto KeepsValue = [&](const Expr *Value) {
          const auto *Chosen = dyn_cast<DeclRefExpr>(Value->IgnoreParens());
          return !(Chosen && Chosen->getDecl() == Parameter) && Keeps(Value);
        };
        return Keeps(Conditional->getCond()) ||
               KeepsValue(Conditional->getTrueExpr()) ||
               KeepsValue(Conditional->getFalseExpr());
      }
      // Also through a pointer it is, `m_Value = *_pValue`, also of a member
      // or an element, `_pValue->m_Value` or `_pValue[0]`.
      const Expr *Through = Read->getSubExpr()->IgnoreParens();
      bool Dereferenced = false;
      for (int Depth = 0; Depth < 16; ++Depth)
        if (const auto *Deref = dyn_cast<UnaryOperator>(Through);
            Deref && Deref->getOpcode() == UO_Deref) {
          Through = Deref->getSubExpr()->IgnoreParenImpCasts();
          Dereferenced = true;
        } else if (const auto *Member = dyn_cast<MemberExpr>(Through)) {
          Dereferenced |= Member->isArrow();
          Through = Member->getBase()->IgnoreParenImpCasts();
        } else if (const auto *Subscript =
                       dyn_cast<ArraySubscriptExpr>(Through);
                   Subscript && !Keeps(Subscript->getIdx())) {
          Dereferenced = true;
          Through = Subscript->getBase()->IgnoreParenImpCasts();
        } else
          break;
      // Also a member of what a reference parameter refers to,
      // `Data.m_Value`, read as a value too.
      if (const auto *Pointer = dyn_cast<DeclRefExpr>(Through);
          (Dereferenced || Parameter->getType()->isReferenceType()) &&
          Pointer && Pointer->getDecl() == Parameter)
        return false;
    }
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Ref->getDecl() == Parameter)
      return true;
    // Not in a branch that a constant condition never takes, `if (false)`.
    return llvm::any_of(evaluatedChildren(S, Defined->getASTContext()), Keeps);
  };
  if (Keeps(Defined->getBody()))
    return true;
  for (const CXXCtorInitializer *Init : Defined->inits()) {
    if (!Mentions(Init->getInit()))
      continue;
    const FieldDecl *Field = Init->getMember();
    // A base, `CValue(int const &_Value) : CBase(_Value)`, or the constructor
    // it delegates to, keeps it when that constructor does, given it as it
    // is.
    if (!Field) {
      const auto *Construct = dyn_cast_or_null<CXXConstructExpr>(
          Init->getInit() ? Init->getInit()->IgnoreImplicit() : nullptr);
      if (!Construct || !Construct->getConstructor())
        return true;
      for (unsigned iArgument = 0; iArgument < Construct->getNumArgs();
           ++iArgument) {
        const Expr *Argument = Construct->getArg(iArgument);
        if (!Mentions(Argument))
          continue;
        const QualType Type = argumentParameterType(Construct, iArgument);
        // A value made of it, `CBase(int _Value)`, keeps nothing.
        if (!Type.isNull() && !Type->isReferenceType() &&
            !Type->isPointerType() && !Type->isRecordType())
          continue;
        const auto *Given =
            dyn_cast<DeclRefExpr>(Argument->IgnoreParenImpCasts());
        if (!Given || Given->getDecl() != Parameter ||
            constructorMayKeep(Construct->getConstructor(), iArgument,
                               Depth + 1))
          return true;
      }
      continue;
    }
    // Also an array of them, `int m_Values[1]`.
    const QualType Type =
        Defined->getASTContext().getBaseElementType(Field->getType());
    // Also one the body resets first, `m_pValue = nullptr;`, and a copy of
    // what holds only values, `CState m_State`.
    if (!holdsOnlyValues(Type, Defined->getASTContext()) &&
        !resetsFirst(Defined, Field,
                     [&](const Expr *E) { return Mentions(E); }))
      return true;
  }
  return false;
}

/// The pointer and view parameters that a statement of \p Body assigns what
/// it doesn't borrow, like `pValue = &Local` or `Values =
/// std::span<int>(Local)`, which refer there after it, when they are
/// assigned nowhere else and nothing jumps back, and the statements. Also the
/// pointers of a closure, its init captures and those it captures by copy.
static llvm::DenseMap<const VarDecl *, llvm::SmallVector<const Stmt *, 2>>
parameterRedirections(
    const Stmt *Body, ASTContext &Context,
    llvm::DenseMap<const VarDecl *, llvm::SmallVector<const IfStmt *, 1>>
        &BranchRedirections) {
  llvm::DenseMap<const VarDecl *, llvm::SmallVector<const Stmt *, 2>>
      Redirections;
  if (const auto *Compound = dyn_cast<CompoundStmt>(Body)) {
    // The parameter \p E names, also through a cast to a reference to it,
    // `static_cast<int const *&>(_pValue)`, or a local reference bound to it,
    // `auto &pCursor = _pValue`, which can't be bound elsewhere.
    std::function<const VarDecl *(const Expr *, int)> ParameterOf =
        [&](const Expr *E, int Depth) -> const VarDecl * {
      for (; E && Depth < 8; ++Depth) {
        E = E->IgnoreParenImpCasts();
        if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
            Cast && Cast->isGLValue()) {
          E = Cast->getSubExpr();
          continue;
        }
        const auto *Ref = dyn_cast<DeclRefExpr>(E);
        if (!Ref)
          return nullptr;
        if (const auto *Parameter = dyn_cast<ParmVarDecl>(Ref->getDecl()))
          return Parameter;
        // Also a pointer of the closure, an init capture or one captured by
        // copy, `[pValue = &Local]() mutable { ... }`, which is its own.
        if (const auto *Captured = dyn_cast<VarDecl>(Ref->getDecl());
            Captured && !Captured->getType()->isReferenceType() &&
            (Captured->isInitCapture() || isCapturedByCopyAt(Ref)))
          return Captured;
        const auto *Alias = dyn_cast<VarDecl>(Ref->getDecl());
        if (!Alias || !Alias->hasLocalStorage() ||
            !Alias->getType()->isLValueReferenceType() ||
            Alias->getType().getNonReferenceType().isConstQualified())
          return nullptr;
        E = Alias->getInit();
      }
      return nullptr;
    };
    // What a statement that assigns a parameter assigns it, the right side
    // of `_pValue = ...`, or of the assignment operator of a view.
    auto AssignedValue = [&](const Stmt *S,
                             const VarDecl **Parameter) -> const Expr * {
      const auto *E = dyn_cast<Expr>(S);
      E = E ? E->IgnoreImplicit() : nullptr;
      const Expr *Left = nullptr;
      const Expr *Right = nullptr;
      if (const auto *Assign = dyn_cast_or_null<BinaryOperator>(E);
          Assign && Assign->getOpcode() == BO_Assign) {
        Left = Assign->getLHS();
        Right = Assign->getRHS();
      } else if (const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(E);
                 Operator && Operator->getOperator() == OO_Equal &&
                 Operator->getNumArgs() == 2) {
        Left = Operator->getArg(0);
        Right = Operator->getArg(1);
      }
      const VarDecl *Assigned = Left ? ParameterOf(Left, 0) : nullptr;
      if (!Assigned || !(Assigned->getType()->isPointerType() ||
                         Assigned->getType()->isRecordType()))
        return nullptr;
      if (Parameter)
        *Parameter = Assigned;
      return Right;
    };
    auto AssignedParameter = [&](const Stmt *S) -> const VarDecl * {
      const VarDecl *Parameter = nullptr;
      return AssignedValue(S, &Parameter) ? Parameter : nullptr;
    };
    // What each local is assigned in the body, `pAlias = _pValue`.
    llvm::DenseMap<const VarDecl *, llvm::SmallVector<const Expr *, 2>>
        LocalAssignments;
    std::function<void(const Stmt *)> CollectAssignments = [&](const Stmt *S) {
      if (!S || isa<LambdaExpr>(S))
        return;
      if (const auto *Assign = dyn_cast<BinaryOperator>(S);
          Assign && Assign->getOpcode() == BO_Assign)
        if (const auto *Target =
                dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts()))
          if (const auto *Local = dyn_cast<VarDecl>(Target->getDecl()))
            LocalAssignments[Local].push_back(Assign->getRHS());
      for (const Stmt *Child : childrenOnce(S))
        CollectAssignments(Child);
    };
    CollectAssignments(Compound);
    // Also through a local made from it, or assigned it, `pAlias = _pValue`.
    auto Mentions = [&](const Stmt *S, const ValueDecl *Decl) {
      int Followed = 0;
      std::function<bool(const Stmt *)> Visit = [&](const Stmt *Inner) {
        if (!Inner)
          return false;
        // Not the value a constant condition doesn't choose, `false ?
        // _pValue : &g_Value`.
        if (const auto *Conditional = dyn_cast<ConditionalOperator>(Inner)) {
          bool Chosen = false;
          if (!Conditional->getCond()->isValueDependent() &&
              Conditional->getCond()->EvaluateAsBooleanCondition(Chosen,
                                                                 Context))
            return Visit(Chosen ? Conditional->getTrueExpr()
                                : Conditional->getFalseExpr());
          // Nor the condition, which only chooses, `_pValue ? &g_First :
          // &g_Second`.
          return Visit(Conditional->getTrueExpr()) ||
                 Visit(Conditional->getFalseExpr());
        }
        if (const auto *Ref = dyn_cast<DeclRefExpr>(Inner)) {
          if (Ref->getDecl() == Decl)
            return true;
          // A pointer or reference, not a copy of what it points to.
          if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
              Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
              (Local->getType()->isPointerType() ||
               Local->getType()->isReferenceType()) &&
              Followed < 8) {
            ++Followed;
            return (Local->getInit() && Visit(Local->getInit())) ||
                   llvm::any_of(LocalAssignments.lookup(Local), Visit);
          }
        }
        return llvm::any_of(childrenOnce(Inner), Visit);
      };
      return Visit(S);
    };
    // An `if` and its `else` that each end by assigning it, also nested,
    // `if (bChoose) _pValue = &First; else _pValue = &Second;`, and the
    // assignments.
    std::function<bool(const Stmt *, const VarDecl *&,
                       llvm::SmallVectorImpl<const Stmt *> &, int)>
        BranchesAssign = [&](const Stmt *S, const VarDecl *&Parameter,
                             llvm::SmallVectorImpl<const Stmt *> &Assignments,
                             int Depth) {
          if (!S || Depth > 16)
            return false;
          if (const VarDecl *Assigned = AssignedParameter(S)) {
            if (Parameter && Assigned != Parameter)
              return false;
            Parameter = Assigned;
            Assignments.push_back(S);
            return true;
          }
          if (const auto *Block = dyn_cast<CompoundStmt>(S))
            return !Block->body_empty() &&
                   BranchesAssign(Block->body_back(), Parameter, Assignments,
                                  Depth + 1);
          // Of a constant condition, also of an `if constexpr` as it is
          // instantiated, only the branch it takes, unless a jump can reach
          // the other.
          if (const auto *If = dyn_cast<IfStmt>(S);
              If && !If->getInit() && !If->getConditionVariable() &&
              !If->isConsteval() && !If->getCond()->isValueDependent()) {
            std::optional<const Stmt *> Taken;
            bool Value = false;
            if (If->isConstexpr())
              Taken = If->getNondiscardedCase(Context);
            else if (If->getCond()->EvaluateAsBooleanCondition(Value,
                                                               Context) &&
                     !hasJumpTarget(Value ? If->getElse() : If->getThen()))
              Taken = Value ? If->getThen() : If->getElse();
            if (Taken)
              return *Taken &&
                     BranchesAssign(*Taken, Parameter, Assignments, Depth + 1);
          }
          if (const auto *If = dyn_cast<IfStmt>(S))
            return If->getElse() && !If->getInit() &&
                   !If->getConditionVariable() && !If->isConsteval() &&
                   BranchesAssign(If->getThen(), Parameter, Assignments,
                                  Depth + 1) &&
                   BranchesAssign(If->getElse(), Parameter, Assignments,
                                  Depth + 1);
          // Also each case of a `switch` with a default, through what it
          // falls through to until a `break` or the end, that assigns it, or
          // that leaves, `default: co_return 0;`, with nothing that can leave
          // the case otherwise, `if (bSkip) break;`.
          if (const auto *Switch = dyn_cast<SwitchStmt>(S);
              Switch && !Switch->getInit() && !Switch->getConditionVariable()) {
            const auto *Body =
                dyn_cast_or_null<CompoundStmt>(Switch->getBody());
            if (!Body)
              return false;
            auto Unlabeled = [](const Stmt *Statement, bool &Default) {
              while (true)
                if (const auto *Case = dyn_cast<CaseStmt>(Statement))
                  Statement = Case->getSubStmt();
                else if (const auto *Other = dyn_cast<DefaultStmt>(Statement)) {
                  Default = true;
                  Statement = Other->getSubStmt();
                } else
                  return Statement;
            };
            std::function<bool(const Stmt *)> Jumps = [&](const Stmt *Inner) {
              if (!Inner || isa<LambdaExpr, SwitchStmt, ForStmt, WhileStmt,
                                DoStmt, CXXForRangeStmt>(Inner))
                return false;
              if (isa<BreakStmt, ContinueStmt, GotoStmt, IndirectGotoStmt,
                      ReturnStmt, CoreturnStmt, CXXThrowExpr>(Inner))
                return true;
              return llvm::any_of(childrenOnce(Inner), Jumps);
            };
            const auto Statements = Body->body();
            bool HasDefault = false;
            bool AnyCase = false;
            for (auto It = Statements.begin(); It != Statements.end(); ++It) {
              if (!isa<SwitchCase>(*It))
                continue;
              AnyCase = true;
              bool Assigned = false;
              bool Ends = false;
              for (auto Next = It; Next != Statements.end(); ++Next) {
                bool Default = false;
                const Stmt *Statement = Unlabeled(*Next, Default);
                if (Next == It)
                  HasDefault |= Default;
                if (isa<BreakStmt>(Statement)) {
                  Ends = Assigned;
                  break;
                }
                if (isa<ReturnStmt, CoreturnStmt>(Statement)) {
                  Ends = true;
                  break;
                }
                if (BranchesAssign(Statement, Parameter, Assignments,
                                   Depth + 1))
                  Assigned = true;
                else if (Jumps(Statement))
                  return false;
                if (std::next(Next) == Statements.end())
                  Ends = Assigned;
              }
              if (!Ends)
                return false;
            }
            return AnyCase && HasDefault;
          }
          return false;
        };
    // The assignments that are redirections, of which only what they assign
    // is looked at below.
    llvm::DenseSet<const Stmt *> BranchAssignments;
    // The assignments of a branch that points one elsewhere, which take the
    // place of a redirection before it only on some paths.
    llvm::DenseSet<const Stmt *> PartialAssignments;
    llvm::SmallPtrSet<const VarDecl *, 2> PartlyAssigned;
    llvm::DenseSet<const VarDecl *> Disqualified;
    auto Redirect = [&](const Stmt *Assignment) {
      const VarDecl *Parameter = nullptr;
      const Expr *Value = AssignedValue(Assignment, &Parameter);
      BranchAssignments.insert(Assignment);
      if (Mentions(Value, Parameter))
        Disqualified.insert(Parameter);
      else
        Redirections[Parameter].push_back(Assignment);
    };
    // The assignments that an expression statement always runs, also before
    // a comma, `co_return (_pValue = &g_Value, *_pValue)`.
    std::function<void(const Expr *)> Sequenced = [&](const Expr *E) {
      if (!E)
        return;
      E = E->IgnoreImplicit()->IgnoreParens();
      if (const auto *Comma = dyn_cast<BinaryOperator>(E);
          Comma && Comma->getOpcode() == BO_Comma) {
        Sequenced(Comma->getLHS());
        Sequenced(Comma->getRHS());
        return;
      }
      if (AssignedParameter(E))
        Redirect(E);
    };
    // Each statement of the body, also of a block in it, `{ _pValue =
    // &g_Value; }`, which runs where it is.
    std::function<void(const Stmt *)> Statements = [&](const Stmt *Statement) {
      if (const auto *Block = dyn_cast<CompoundStmt>(Statement)) {
        for (const Stmt *Inner : Block->body())
          Statements(Inner);
        return;
      }
      if (AssignedParameter(Statement)) {
        Redirect(Statement);
        return;
      }
      const VarDecl *Parameter = nullptr;
      llvm::SmallVector<const Stmt *, 2> Assignments;
      if (isa<IfStmt, SwitchStmt>(Statement) &&
          BranchesAssign(Statement, Parameter, Assignments, 0)) {
        BranchAssignments.insert(Assignments.begin(), Assignments.end());
        if (llvm::any_of(Assignments, [&](const Stmt *Assignment) {
              return Mentions(AssignedValue(Assignment, nullptr), Parameter);
            }))
          Disqualified.insert(Parameter);
        else
          Redirections[Parameter].push_back(Statement);
        return;
      }
      // Or a branch of one, in a statement of its own, which protects what
      // follows when each suspension is in that branch.
      if (const auto *If = dyn_cast<IfStmt>(Statement);
          If && !If->isConstexpr() && If->getThen()) {
        llvm::SmallVector<const Stmt *, 4> Then;
        if (const auto *Block = dyn_cast<CompoundStmt>(If->getThen()))
          Then.append(Block->body_begin(), Block->body_end());
        else
          Then.push_back(If->getThen());
        for (const Stmt *Inner : Then)
          if (const VarDecl *Assigned = AssignedParameter(Inner);
              Assigned && !Mentions(AssignedValue(Inner, nullptr), Assigned)) {
            PartialAssignments.insert(Inner);
            BranchRedirections[Assigned].push_back(If);
          }
        return;
      }
      if (const auto *Return = dyn_cast<CoreturnStmt>(Statement))
        Sequenced(Return->getOperand());
      else if (const auto *Return = dyn_cast<ReturnStmt>(Statement))
        Sequenced(Return->getRetValue());
      else if (const auto *Expression = dyn_cast<Expr>(Statement))
        Sequenced(Expression);
    };
    for (const Stmt *Statement : Compound->body())
      Statements(Statement);
    // Assigned elsewhere, or a jump back.
    std::function<void(const Stmt *, bool)> Visit = [&](const Stmt *S,
                                                         bool TopLevel) {
      if (!S)
        return;
      if (isa<GotoStmt, IndirectGotoStmt, LabelStmt>(S)) {
        Redirections.clear();
        BranchRedirections.clear();
      }
      // What the branches of a redirection assign, not the assignments.
      if (BranchAssignments.contains(S))
        return Visit(AssignedValue(S, nullptr), false);
      if (PartialAssignments.contains(S)) {
        const VarDecl *Assigned = nullptr;
        if (const Expr *Value = AssignedValue(S, &Assigned)) {
          PartlyAssigned.insert(Assigned);
          return Visit(Value, false);
        }
      }
      // What is written, also through a cast to a reference,
      // `static_cast<int *&>(_pValue)`, or as either of a conditional.
      // Also through a local reference bound to it, `pCursor = nullptr` of
      // `auto &pCursor = _pValue`.
      auto Disqualify = [&](const Expr *Written) {
        for (const DeclRefExpr *Target : assignedVariables(Written))
          if (const VarDecl *Parameter = ParameterOf(Target, 0))
            Disqualified.insert(Parameter);
      };
      if (const auto *Assign = dyn_cast<BinaryOperator>(S);
          Assign && Assign->isAssignmentOp() && !TopLevel)
        Disqualify(Assign->getLHS());
      if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S);
          Operator && Operator->isAssignmentOp() &&
          Operator->getNumArgs() >= 1 && !TopLevel)
        Disqualify(Operator->getArg(0));
      if (const auto *Unary = dyn_cast<UnaryOperator>(S);
          Unary && (Unary->isIncrementDecrementOp() ||
                    Unary->getOpcode() == UO_AddrOf))
        Disqualify(Unary->getSubExpr());
      // Given to what can assign it, `fg_Reset(_pValue)` by `int *&`, or
      // bound to a reference that can, `auto &&pAlias = _pValue`, other than
      // a local one, `auto &pCursor = _pValue`, through which what assigns it
      // is told like for the parameter.
      if (const auto *Call = dyn_cast<CallExpr>(S))
        for (unsigned iArgument = 0; iArgument < Call->getNumArgs();
             ++iArgument)
          if (mayAssignArgument(Call, iArgument))
            Disqualify(Call->getArg(iArgument));
      if (const auto *Declaration = dyn_cast<DeclStmt>(S))
        for (const Decl *D : Declaration->decls())
          if (const auto *Alias = dyn_cast<VarDecl>(D);
              Alias && Alias->getType()->isReferenceType() &&
              !Alias->getType().getNonReferenceType().isConstQualified() &&
              Alias->getInit()) {
            const auto *Named =
                dyn_cast<DeclRefExpr>(Alias->getInit()->IgnoreParenImpCasts());
            if (!(Alias->hasLocalStorage() &&
                  Alias->getType()->isLValueReferenceType() && Named &&
                  ParameterOf(Named, 0)))
              Disqualify(Alias->getInit());
          }
      for (const Stmt *Child : childrenOnce(S))
        Visit(Child, false);
    };
    for (const Stmt *Statement : Compound->body()) {
      if (const Expr *Value = AssignedValue(Statement, nullptr))
        Visit(Value, false);
      else
        Visit(Statement, false);
    }
    for (const VarDecl *Parameter : Disqualified) {
      Redirections.erase(Parameter);
      BranchRedirections.erase(Parameter);
    }
    for (const VarDecl *Parameter : PartlyAssigned)
      Redirections.erase(Parameter);
  }
  return Redirections;
}

/// The parameters by a reference that isn't to const that a visible \p
/// Function assigns another of its parameters, `o_pResult = _pText;` of
/// `void fg_Set(char const *&o_pResult, char const *_pText)`, by their
/// places: the one assigned, and the one it is assigned.
static llvm::SmallVector<std::pair<unsigned, unsigned>, 1>
assignedParameters(const FunctionDecl *Function) {
  const FunctionDecl *Definition =
      Function ? Function->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody() ||
      isa<CoroutineBodyStmt>(Definition->getBody()))
    return {};
  // Each function once.
  struct CAssignedParameters {
    llvm::DenseMap<const FunctionDecl *,
                   llvm::SmallVector<std::pair<unsigned, unsigned>, 1>>
        Pairs;
  };
  auto [It, Inserted] =
      astCache<CAssignedParameters>(Definition->getASTContext())
          .Pairs.try_emplace(Definition);
  if (!Inserted)
    return It->second;
  llvm::SmallVector<std::pair<unsigned, unsigned>, 1> Pairs;
  auto ParameterOf = [](const Expr *E) -> const ParmVarDecl * {
    const auto *Ref = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
    return Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl()) : nullptr;
  };
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return;
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
    const Expr *Left = nullptr, *Right = nullptr;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->getOpcode() == BO_Assign) {
      Left = Assign->getLHS();
      Right = Assign->getRHS();
    } else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S);
               Operator && Operator->getOperator() == OO_Equal &&
               Operator->getNumArgs() == 2) {
      Left = Operator->getArg(0);
      Right = Operator->getArg(1);
    }
    const ParmVarDecl *Target = Left ? ParameterOf(Left) : nullptr;
    const ParmVarDecl *Source = Right ? ParameterOf(Right) : nullptr;
    if (Target && Source && Target != Source &&
        Target->getType()->isLValueReferenceType() &&
        !Target->getType().getNonReferenceType().isConstQualified())
      Pairs.push_back(
          {Target->getFunctionScopeIndex(), Source->getFunctionScopeIndex()});
  };
  Visit(Definition->getBody());
  // The cache may have grown while the body was looked at.
  auto &Cached = astCache<CAssignedParameters>(Definition->getASTContext())
                     .Pairs[Definition];
  Cached = std::move(Pairs);
  return Cached;
}

const Stmt *
firstUseAfterSuspension(const FunctionDecl *Function,
                        llvm::function_ref<bool(const Stmt *)> IsUse,
                        ASTContext &Context, const BorrowRoots *Borrow) {
  Stmt *Body = writtenBody(Function);
  if (!Body)
    return nullptr;
  const SuspensionFlow &Flow = suspensionFlow(Function, Context);
  const CParents &Parents = *Flow.Parents;

  // The references to a declaration from the index of the function, or else
  // what the body has of them.
  UseCollector Collector(IsUse);
  if (Borrow && Borrow->Decl) {
    if (!Flow.References) {
      UseCollector All([](const Stmt *S) { return isa<DeclRefExpr>(S); });
      All.TraverseStmt(Body);
      Flow.References.emplace();
      for (const Stmt *Ref : All.Uses)
        (*Flow.References)[cast<DeclRefExpr>(Ref)->getDecl()].push_back(Ref);
    }
    // Those that are uses, not one that reads only what the parameter has
    // of its own, `Text.f_GetLen()`.
    if (auto It = Flow.References->find(Borrow->Decl);
        It != Flow.References->end())
      for (const Stmt *Ref : It->second)
        if (IsUse(Ref))
          Collector.Uses.push_back(Ref);
  } else
    Collector.TraverseStmt(Body);
  // Without a control flow, any use may be after a suspension.
  const bool Joined = Borrow && Borrow->Joined;
  if (!Flow.Built && !Collector.Uses.empty())
    return Joined ? nullptr : Collector.Uses.front();
  // A pointer parameter that a statement of the body assigns what it doesn't
  // borrow, like `pValue = &Local`, points there after it, when it is
  // assigned nowhere else and nothing jumps back.
  const SourceManager &SM = Context.getSourceManager();
  const auto &Redirections = Flow.Redirections;
  // Whether \p Parameter is pointed elsewhere where \p Use is, also through
  // a local reference to it that \p Use reads, `pCursor->f_GetLen()` of
  // `auto &pCursor = _pText; pCursor = &Local;`.
  std::function<bool(const VarDecl *, const Stmt *)> RedirectedAt;
  auto IsRedirected = [&](const Stmt *Use) {
    const auto *Ref = dyn_cast<DeclRefExpr>(Use);
    return RedirectedAt(Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr, Use);
  };
  RedirectedAt = [&](const VarDecl *Parameter, const Stmt *Use) {
    // After a branch that points it elsewhere, when each suspension is in
    // that branch, so a path that suspends was redirected.
    if (auto Branch = Parameter ? Flow.BranchRedirections.find(Parameter)
                                : Flow.BranchRedirections.end();
        Branch != Flow.BranchRedirections.end()) {
      // Those that suspend, not `co_await ECoroutineFlag_CaptureExceptions`,
      // nor those of the lambdas it makes, which suspend those.
      if (!Flow.Suspending) {
        Flow.Suspending.emplace();
        std::function<void(const Stmt *)> Collect = [&](const Stmt *S) {
          if (!S)
            return;
          if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
            for (const Expr *Init : Lambda->capture_inits())
              Collect(Init);
            return;
          }
          if (isa<CoroutineSuspendExpr>(S) && isSuspension(S))
            Flow.Suspending->push_back(S);
          for (const Stmt *Child : childrenOnce(S))
            Collect(Child);
        };
        Collect(Body);
      }
      for (const IfStmt *If : Branch->second)
        if (SM.isBeforeInTranslationUnit(If->getEndLoc(), Use->getBeginLoc()) &&
            llvm::all_of(*Flow.Suspending, [&](const Stmt *Suspension) {
              return If->getThen()->getSourceRange().fullyContains(
                  Suspension->getSourceRange());
            }))
          return true;
    }
    auto It = Parameter ? Redirections.find(Parameter) : Redirections.end();
    if (It == Redirections.end())
      return false;
    auto After = [&](const Stmt *At) {
      return llvm::any_of(It->second, [&](const Stmt *Assignment) {
        const SourceRange Range = Assignment->getSourceRange();
        // What the assignment assigns, or what follows it; of branches that
        // each assign it, what follows them all. Not what its value reads,
        // `_pEntry = _pEntry->m_Value ? &g_First : &g_Second`, before it
        // assigns.
        const Expr *Assigned = nullptr;
        if (const auto *E = dyn_cast<Expr>(Assignment)) {
          E = E->IgnoreImplicit()->IgnoreParens();
          if (const auto *Binary = dyn_cast<BinaryOperator>(E))
            Assigned = Binary->getLHS();
          else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
                   Operator && Operator->getNumArgs() >= 1)
            Assigned = Operator->getArg(0);
        }
        return (Assigned && Assigned->getSourceRange().fullyContains(
                                At->getSourceRange())) ||
               SM.isBeforeInTranslationUnit(Range.getEnd(), At->getBeginLoc());
      });
    };
    if (After(Use))
      return true;
    // In a lambda that captures it by reference, `[&_pValue] { return
    // *_pValue; }`, it is read where the lambda is called, which each call
    // after it is past, and the capture refers to it there too.
    for (const Stmt *Child = Use, *Node = Parents.getParent(Use); Node;
         Child = Node, Node = Parents.getParent(Node))
      if (const auto *Lambda = dyn_cast<LambdaExpr>(Node)) {
        if ((Child != Lambda->getBody() &&
             !llvm::is_contained(Lambda->capture_inits(), Child)) ||
            llvm::none_of(Lambda->captures(),
                          [&](const LambdaCapture &Capture) {
                            return Capture.capturesVariable() &&
                                   Capture.getCapturedVar() == Parameter &&
                                   Capture.getCaptureKind() == LCK_ByRef;
                          }))
          return false;
        const auto Consumers = closureConsumers(Lambda, Context, {});
        return !Consumers.empty() && isCalledInScope(Consumers) &&
               llvm::all_of(Consumers, [&](const ClosureConsumer &Consumer) {
                 return After(Consumer.Site);
               });
      }
    return false;
  };
  // Assigning a pointer parameter, `pValue = nullptr`, also where it isn't
  // redirected for good, changes the pointer of the frame, not what it
  // points to, and so does assigning a pointer of the closure, an init
  // capture or one captured by copy, `[pValue = &Local]() mutable { ...
  // pValue = nullptr; }`.
  auto IsOverwritten = [&](const Stmt *Use) {
    const auto *Ref = dyn_cast<DeclRefExpr>(Use);
    const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (!Variable || !Variable->getType()->isPointerType() ||
        !(isa<ParmVarDecl>(Variable) || Variable->isInitCapture() ||
          isCapturedByCopyAt(Ref)))
      return false;
    DynTypedNode Node = DynTypedNode::create(*Ref);
    for (int Depth = 0; Depth < 8; ++Depth) {
      const auto Above = instantiatedParents(Node, Context);
      if (Above.empty())
        return false;
      if (const auto *Assign = Above[0].get<BinaryOperator>())
        return Assign->getOpcode() == BO_Assign &&
               Assign->getLHS()->IgnoreParens() == Node.get<Expr>();
      // Also through a cast to a reference to it, `static_cast<int
      // *&>(pValue) = nullptr`.
      const auto *Cast = Above[0].get<ExplicitCastExpr>();
      if (!Above[0].get<ParenExpr>() && !(Cast && Cast->isGLValue()))
        return false;
      Node = Above[0];
    }
    return false;
  };
  // Discarded, `(void)_pValue;`, which reads neither it nor what it refers
  // to, unless it is volatile.
  auto IsDiscarded = [&](const Stmt *Use) {
    const Stmt *Above = Parents.getParent(Use);
    while (isa_and_nonnull<ParenExpr>(Above))
      Above = Parents.getParent(Above);
    const auto *Discard = dyn_cast_or_null<ExplicitCastExpr>(Above);
    const auto *Used = dyn_cast<Expr>(Use);
    return Discard && Used && Discard->getType()->isVoidType() &&
           !Used->getType().isVolatileQualified() &&
           !Used->getType().getNonReferenceType().isVolatileQualified();
  };
  // Bound to a local reference that is only assigned through, `auto &pCursor
  // = _pValue; pCursor = &Local;`, which points the parameter elsewhere, as
  // what assigns it tells, and reads nothing.
  auto IsAssignedAlias = [&](const Stmt *Use) {
    const auto *Ref = dyn_cast<DeclRefExpr>(Use);
    if (!Ref || !isa<ParmVarDecl>(Ref->getDecl()) ||
        !Ref->getDecl()->getType()->isPointerType())
      return false;
    const Stmt *Above = Parents.getParent(Use);
    while (isa_and_nonnull<ParenExpr, ImplicitCastExpr>(Above))
      Above = Parents.getParent(Above);
    const auto *Declaration = dyn_cast_or_null<DeclStmt>(Above);
    if (!Declaration)
      return false;
    for (const Decl *D : Declaration->decls()) {
      const auto *Alias = dyn_cast<VarDecl>(D);
      // Not a structured binding, which is used through its names.
      if (!Alias || isa<DecompositionDecl>(Alias) || !Alias->getInit() ||
          Alias->getInit()->IgnoreParenImpCasts() != Ref ||
          !Alias->getType()->isLValueReferenceType() ||
          !Alias->hasLocalStorage())
        continue;
      bool OnlyAssigned = true;
      std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
        if (!S || !OnlyAssigned)
          return;
        if (const auto *Assign = dyn_cast<BinaryOperator>(S);
            Assign && Assign->getOpcode() == BO_Assign)
          if (const auto *Target =
                  dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParens());
              Target && Target->getDecl() == Alias) {
            Visit(Assign->getRHS());
            return;
          }
        // Or read after it points the parameter elsewhere, where it reads
        // what is not borrowed.
        if (const auto *Named = dyn_cast<DeclRefExpr>(S);
            Named && Named->getDecl() == Alias &&
            !RedirectedAt(cast<VarDecl>(Ref->getDecl()), Named))
          OnlyAssigned = false;
        for (const Stmt *Child : childrenOnce(S))
          Visit(Child);
      };
      Visit(Body);
      return OnlyAssigned;
    }
    return false;
  };
  for (const Stmt *Use : Collector.Uses) {
    if (Joined)
      break;
    if (((!Borrow || !Borrow->OfClosure) &&
         (IsRedirected(Use) || IsOverwritten(Use))) ||
        IsDiscarded(Use) || IsAssignedAlias(Use))
      continue;
    // A use is evaluated with the closest statement of the control flow that
    // contains it, unless it is in the body of a lambda or block, which runs
    // when that is called. A use the control flow doesn't cover counts as
    // after a suspension.
    if (useAfterSuspension(Use, Parents, Flow, Borrow))
      return Use;
  }

  // Before the first suspension, borrowed data may only be used as values, so
  // that nothing keeps a reference to it past the suspension. A coroutine
  // that doesn't suspend keeps nothing past one, but its result, which
  // `co_return` gives, outlives the call.
  if (!Borrow)
    return nullptr;
  const bool OnlyResults = Flow.Built && (!Flow.Suspends || Joined);
  // What the future of the coroutine holds, which `co_return` gives. When
  // it can refer to anything, a borrow anywhere in a coroutine that doesn't
  // suspend can reach it, like through `auto *pValue = &Value`.
  const QualType Result = futureValueType(Function->getReturnType());
  const bool ResultCanRefer =
      !Result.isNull() &&
      (Result->isReferenceType() || Result->isPointerType() ||
       Result->isMemberPointerType() ||
       (Borrow->IsView && Borrow->IsView(Result)));
  // Without a suspension, only the result outlives the call, and one that can't
  // refer to anything keeps nothing, like `co_return fg_Format(..., Value)` in
  // a `TCFuture<void>`.
  if (OnlyResults && !ResultCanRefer) {
    // Except a converting constructor of the result that binds a reference
    // to borrowed data, like `co_return Value;` into a `CResult(int const
    // &_Value)` that keeps `&_Value`, which only a class has: a scalar, like
    // the `int` of `co_return Values[0]`, keeps nothing, and isn't looked
    // for again for each of many parameters.
    if (Result.isNull() || !Result->isRecordType())
      return nullptr;
    const Stmt *Converted = nullptr;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || Converted || isa<LambdaExpr, BlockExpr>(S))
        return;
      // Not a `co_return` that can't run: in a branch of an `if constexpr`
      // that is discarded or of a constant condition never taken, or after
      // one that always leaves, unless a jump reaches it.
      if (const auto *Block = dyn_cast<CompoundStmt>(S)) {
        bool Left = false;
        for (const Stmt *Statement : Block->body()) {
          if (Left && !hasJumpTarget(Statement))
            continue;
          Left = false;
          Visit(Statement);
          if (isa<CoreturnStmt, ReturnStmt>(Statement))
            Left = true;
        }
        return;
      }
      for (const Stmt *Child : evaluatedChildren(S, Context))
        Visit(Child);
      const auto *Return = dyn_cast<CoreturnStmt>(S);
      if (Converted || !Return || !Return->getOperand())
        return;
      // The conversion is in the operand, or in the promise's
      // `return_value` that gets it.
      const Expr *Operand = Return->getOperand()->IgnoreImplicit();
      // `CResult(Value)` and `CResult{Value}` construct it.
      while (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Operand))
        Operand = Cast->getSubExpr()->IgnoreImplicit();
      if (!isa<CXXConstructExpr, InitListExpr, CXXParenListInitExpr>(Operand))
        if (const auto *Promise =
                dyn_cast_or_null<CallExpr>(Return->getPromiseCall());
            Promise && Promise->getNumArgs() == 1)
          Operand = Promise->getArg(0)->IgnoreImplicit();
      // A local returned, like `CResult Result(Value); co_return Result;`, is
      // what its initializer made, and what is assigned to its members.
      const VarDecl *Local = nullptr;
      // Each local once, however many copies, `CResult Second = First`.
      llvm::SmallPtrSet<const VarDecl *, 8> Expanded;
      for (int Depth = 0; Depth < 256; ++Depth) {
        const Expr *Inner = Operand->IgnoreParenImpCasts();
        if (const auto *Copy = dyn_cast<CXXConstructExpr>(Inner);
            Copy && Copy->getNumArgs() >= 1 &&
            Copy->getConstructor()->isCopyOrMoveConstructor())
          Inner = Copy->getArg(0)->IgnoreParenImpCasts();
        if (const auto *Call = dyn_cast<CallExpr>(Inner);
            Call && Call->getNumArgs() == 1) {
          const StringRef Name = calleeName(Call);
          if (isMoveFunction(Name))
            Inner = Call->getArg(0)->IgnoreParenImpCasts();
        }
        // Also cast to a reference to it, `static_cast<CResult &&>(Result)`.
        while (const auto *Cast = dyn_cast<ExplicitCastExpr>(Inner)) {
          if (!Cast->isGLValue())
            break;
          Inner = Cast->getSubExpr()->IgnoreParenImpCasts();
        }
        const auto *Ref = dyn_cast<DeclRefExpr>(Inner);
        const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        if (!Variable || !Variable->hasLocalStorage() ||
            isa<ParmVarDecl>(Variable) || !Variable->getInit() ||
            Borrow->IsRoot(Inner) || !Expanded.insert(Variable).second)
          break;
        Local = Variable;
        // What it has where it is returned, `Result = CResult{nullptr};`,
        // when that can be told, and else what it is made with.
        const Expr *Value = assignedValueAt(Variable, Ref);
        Operand = !Value || Value == Variable->getInit()
                      ? Variable->getInit()->IgnoreImplicit()
                      : skipWrappers(Value)->IgnoreImplicit();
        while (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Operand))
          Operand = Cast->getSubExpr()->IgnoreImplicit();
      }
      // The locals followed while a value is looked at, each once, also one
      // that others are made from many times, `int &r1 = b ? r0 : r0;`: one
      // met again adds nothing to what is found.
      llvm::DenseSet<const VarDecl *> FollowedAliases;
      // The data an initializer refers to: through members, dereferences and
      // subscripts to a root, either value of a conditional, `bChoose ? Value
      // : g_Value`, or the one a constant condition selects, what a comma,
      // an assignment, `(Copy = Value)`, or an increment, `++Value`, gives,
      // and casts of it, `static_cast<int const &>(Value)`.
      std::function<bool(const Expr *, int)> RefersToRootImpl =
          [&](const Expr *Part, int Depth) {
            for (; Part; ++Depth) {
              // Deeper than can be looked into, it can.
              if (Depth >= 4096)
                return true;
              if (Borrow->IsRoot(Part))
                return true;
              Part = Part->IgnoreParenImpCasts();
              // A local reference, like `int const &Alias = Value`, refers to
              // what it is bound to.
              if (const auto *Ref = dyn_cast<DeclRefExpr>(Part)) {
                const auto *Alias = dyn_cast<VarDecl>(Ref->getDecl());
                if (Alias && Alias->getType()->isReferenceType() &&
                    Alias->hasLocalStorage() && !isa<ParmVarDecl>(Alias) &&
                    Alias->getInit() && !Borrow->IsRoot(Part)) {
                  if (!FollowedAliases.insert(Alias).second)
                    return false;
                  Part = Alias->getInit();
                  continue;
                }
                // And a local pointer what it holds there, `*pValue` of
                // `auto *pValue = &Value`, which can be anything when that
                // can't be told.
                if (Alias && Alias->getType()->isPointerType() &&
                    Alias->hasLocalStorage() && !isa<ParmVarDecl>(Alias) &&
                    !Borrow->IsRoot(Part)) {
                  if (!FollowedAliases.insert(Alias).second)
                    return false;
                  const llvm::SmallVector<const Expr *, 2> Held =
                      assignedValuesAt(Alias, Ref);
                  if (Held.empty())
                    return true;
                  return llvm::any_of(Held, [&](const Expr *Value) {
                    return RefersToRootImpl(Value, Depth + 1);
                  });
                }
              }
              if (const auto *Conditional =
                      dyn_cast<AbstractConditionalOperator>(Part)) {
                bool Selected = false;
                if (!Conditional->getCond()->isValueDependent() &&
                    Conditional->getCond()->EvaluateAsBooleanCondition(
                        Selected, Context)) {
                  Part = Selected ? Conditional->getTrueExpr()
                                  : Conditional->getFalseExpr();
                  continue;
                }
                return RefersToRootImpl(Conditional->getTrueExpr(),
                                        Depth + 1) ||
                       RefersToRootImpl(Conditional->getFalseExpr(), Depth + 1);
              }
              if (const auto *Binary = dyn_cast<BinaryOperator>(Part)) {
                if (Binary->getOpcode() == BO_Comma)
                  Part = Binary->getRHS();
                // Of a pointer, what is assigned is its value, `CResult{
                // pValue = &_Value}`, not what it pointed to before; else
                // it is what it is assigned to, which it refers to.
                else if (Binary->getOpcode() == BO_Assign &&
                         Binary->getType()->isPointerType())
                  Part = Binary->getRHS();
                else if (Binary->isAssignmentOp())
                  Part = Binary->getLHS();
                // Also moved, `_pValues + 1`, which points into what the
                // pointer does, like `_pValues[1]`.
                else if (Binary->isAdditiveOp() &&
                         Binary->getType()->isPointerType())
                  Part = Binary->getLHS()->getType()->isPointerType()
                             ? Binary->getLHS()
                             : Binary->getRHS();
                else
                  return Borrow->IsRoot(Part);
              } else if (const auto *Member = dyn_cast<MemberExpr>(Part))
                Part = Member->getBase();
              else if (const auto *Unary = dyn_cast<UnaryOperator>(Part);
                       Unary && (Unary->getOpcode() == UO_Deref ||
                                 Unary->getOpcode() == UO_AddrOf ||
                                 Unary->getOpcode() == UO_PreInc ||
                                 Unary->getOpcode() == UO_PreDec))
                Part = Unary->getSubExpr();
              else if (const auto *Subscript =
                           dyn_cast<ArraySubscriptExpr>(Part))
                Part = Subscript->getBase();
              else if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Part);
                       Cast &&
                       (Cast->isGLValue() ||
                        (Cast->getType()->isPointerType() &&
                         Cast->getSubExpr()->getType()->isPointerType())))
                Part = Cast->getSubExpr();
              // What a function whose body can be seen gives back of its
              // arguments, `fg_Const(Value)`, also the address of one it is
              // given by reference, `return &_Value;`.
              else if (const auto *Call = dyn_cast<CallExpr>(Part);
                       Call && (Call->isGLValue() ||
                                Call->getType()->isPointerType())) {
                for (const Expr *Returned : returnedArguments(Call))
                  if (RefersToRootImpl(Returned, Depth + 1))
                    return true;
                if (Call->getType()->isPointerType())
                  for (const Expr *Addressed : returnedArguments(Call, true))
                    if (RefersToRootImpl(Addressed, Depth + 1))
                      return true;
                return Borrow->IsRoot(Part);
              } else
                return Borrow->IsRoot(Part);
            }
            return false;
          };
      auto RefersToRoot = [&](const Expr *Part) {
        return RefersToRootImpl(Part, 0);
      };
      // A reference binds to it; a pointer points to it, `&Value`, or is a
      // borrowed pointer.
      std::function<bool(QualType, const Expr *, int)> KeepsIn;
      auto Keeps = [&](QualType Type, const Expr *Argument) {
        FollowedAliases.clear();
        return KeepsIn(Type, Argument, 0);
      };
      KeepsIn = [&](QualType Type, const Expr *Argument, int Nesting) {
        Argument = Argument->IgnoreParenImpCasts();
        if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(Argument))
          Argument = Materialize->getSubExpr()->IgnoreParenImpCasts();
        if (Type->isReferenceType())
          return RefersToRoot(Argument);
        // An array, like `int *m_Values[1]` of `{{&Value}}`, keeps what its
        // elements keep.
        if (const ArrayType *Array = Type->getAsArrayTypeUnsafe()) {
          const auto *List = dyn_cast<InitListExpr>(
              skipWrappers(Argument)->IgnoreImplicit());
          return Nesting <= 4 && List &&
                 llvm::any_of(List->inits(), [&](const Expr *Init) {
                   return KeepsIn(Array->getElementType(), Init, Nesting + 1);
                 });
        }
        // A member that is an object of its own, like `COuter{CInner{&Value}}`
        // or `COuter{CRef(Value)}`, keeps what its own members keep.
        if (!Type->isPointerType()) {
          if (Nesting > 4 || !Type->isRecordType())
            return false;
          const Expr *Inner = skipWrappers(Argument)->IgnoreImplicit();
          while (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Inner))
            Inner = Cast->getSubExpr()->IgnoreImplicit();
          if (const std::optional<CAggregateInit> List = aggregateInit(Inner)) {
            if (const FieldDecl *Field = List->UnionField)
              return !List->Inits.empty() &&
                     KeepsIn(Field->getType(), List->Inits[0], Nesting + 1);
            const auto *Record = List->Type->getAsCXXRecordDecl();
            if (!Record)
              return false;
            unsigned iInit = 0;
            for (const CXXBaseSpecifier &Base : Record->bases()) {
              if (iInit < List->Inits.size() &&
                  KeepsIn(Base.getType(), List->Inits[iInit], Nesting + 1))
                return true;
              ++iInit;
            }
            for (const FieldDecl *Field : Record->fields()) {
              // An unnamed bit-field isn't initialized, `int : 0`.
              if (Field->isUnnamedBitField())
                continue;
              if (iInit < List->Inits.size() &&
                  KeepsIn(Field->getType(), List->Inits[iInit], Nesting + 1))
                return true;
              ++iInit;
            }
            return false;
          }
          const auto *Construct = dyn_cast<CXXConstructExpr>(Inner);
          // A copy of a borrowed view, like `CResult{Text}` of a `CStrSpan
          // Text`, refers to the same data.
          if (Borrow->IsView && Borrow->IsView(Type)) {
            const Expr *Copied = Inner;
            if (Construct &&
                Construct->getConstructor()->isCopyOrMoveConstructor() &&
                Construct->getNumArgs() >= 1)
              Copied = Construct->getArg(0);
            // Also of a local copy of it, `auto Copy = Text;`, what it has
            // where it is copied, through any number of them, each once.
            llvm::SmallPtrSet<const VarDecl *, 8> Copies;
            for (int Depth = 0; Depth < 1024; ++Depth) {
              const auto *Ref =
                  dyn_cast<DeclRefExpr>(Copied->IgnoreParenImpCasts());
              const auto *Copy = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
              if (!Copy || !Copy->hasLocalStorage() || isa<ParmVarDecl>(Copy) ||
                  Copy->getType()->isReferenceType() ||
                  !Borrow->IsView(Copy->getType()) || Borrow->IsRoot(Copied) ||
                  !Copies.insert(Copy).second)
                break;
              const Expr *Value = assignedValueAt(Copy, Ref);
              if (!Value)
                break;
              Value = skipWrappers(Value)->IgnoreImplicit();
              if (const auto *Inner = dyn_cast<CXXConstructExpr>(Value);
                  Inner && Inner->getConstructor()->isCopyOrMoveConstructor() &&
                  Inner->getNumArgs() >= 1)
                Value = Inner->getArg(0);
              Copied = Value;
            }
            if (RefersToRoot(Copied))
              return true;
          }
          if (!Construct || Construct->getConstructor()->isCopyOrMoveConstructor())
            return false;
          const CXXConstructorDecl *Constructor = Construct->getConstructor();
          for (unsigned iArgument = 0; iArgument < Construct->getNumArgs() &&
                                       iArgument < Constructor->getNumParams();
               ++iArgument) {
            if (!constructorMayKeep(Constructor, iArgument))
              continue;
            if (KeepsIn(Constructor->getParamDecl(iArgument)->getType(),
                        Construct->getArg(iArgument), Nesting + 1))
              return true;
          }
          return false;
        }
        // Either value of a conditional, like `bFirst ? &Value : nullptr`, or
        // the one a constant condition selects, `false ? &Value : nullptr`.
        if (const auto *Conditional =
                dyn_cast<AbstractConditionalOperator>(Argument)) {
          bool Selected = false;
          if (!Conditional->getCond()->isValueDependent() &&
              Conditional->getCond()->EvaluateAsBooleanCondition(Selected,
                                                                 Context))
            return KeepsIn(Type,
                           Selected ? Conditional->getTrueExpr()
                                    : Conditional->getFalseExpr(),
                           Nesting + 1);
          return KeepsIn(Type, Conditional->getTrueExpr(), Nesting + 1) ||
                 KeepsIn(Type, Conditional->getFalseExpr(), Nesting + 1);
        }
        // The last of a comma, `(fg_Log(), &Value)`.
        if (const auto *Comma = dyn_cast<BinaryOperator>(Argument);
            Comma && Comma->getOpcode() == BO_Comma)
          return Nesting <= 16 && KeepsIn(Type, Comma->getRHS(), Nesting + 1);
        // A pointer cast to another, `static_cast<void const *>(&Value)`.
        if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Argument);
            Cast && Cast->getSubExpr()->getType()->isPointerType())
          return Nesting <= 8 &&
                 KeepsIn(Cast->getSubExpr()->getType(), Cast->getSubExpr(),
                         Nesting + 1);
        // A local pointer, like `int *pValue = &Value`, points where it was
        // initialized to, or to what it is assigned, also through copies,
        // `int *pSecond = pFirst`, and conditionals, `int const *pSecond =
        // bChoose ? pFirst : pFirst`.
        const auto *Ref = dyn_cast<DeclRefExpr>(Argument);
        if (const auto *Alias =
                Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
            Alias && Alias->hasLocalStorage() && !isa<ParmVarDecl>(Alias) &&
            Alias->getType()->isPointerType() && !Borrow->IsRoot(Argument)) {
          // One already followed for this value adds nothing.
          if (!FollowedAliases.insert(Alias).second)
            return false;
          if (isChangedVariable(Alias)) {
            if (Nesting > 4)
              return false;
            // What it holds where it is read, when that can be told, `pValue
            // = nullptr;` after `int const *pValue = &_Value;`.
            if (const llvm::SmallVector<const Expr *, 2> Held =
                    assignedValuesAt(Alias, Ref);
                !Held.empty())
              return llvm::any_of(Held, [&](const Expr *Value) {
                return KeepsIn(Type, Value, Nesting + 1);
              });
            // Else, what it is assigned anywhere, also either of a
            // conditional, `(bFirst ? pFirst : pSecond) =`.
            auto IsAssignment = [&](const Stmt *S) {
              const auto *Assign = dyn_cast<BinaryOperator>(S);
              return Assign && Assign->getOpcode() == BO_Assign &&
                     llvm::any_of(assignedVariables(Assign->getLHS()),
                                  [&](const DeclRefExpr *Target) {
                                    return Target->getDecl() == Alias;
                                  });
            };
            UseCollector Assignments(IsAssignment);
            Assignments.TraverseStmt(Body);
            for (const Stmt *Assignment : Assignments.Uses) {
              if (KeepsIn(Type, cast<BinaryOperator>(Assignment)->getRHS(),
                          Nesting + 1))
                return true;
            }
            return Alias->getInit() &&
                   KeepsIn(Type, Alias->getInit(), Nesting + 1);
          }
          // Each made of one declared before it, so a chain of them ends.
          if (Alias->getInit())
            return Nesting <= 256 &&
                   KeepsIn(Type, Alias->getInit(), Nesting + 1);
        }
        if (const auto *Address = dyn_cast<UnaryOperator>(Argument);
            Address && Address->getOpcode() == UO_AddrOf)
          return RefersToRoot(Address->getSubExpr());
        return Borrow->PointerValues && RefersToRoot(Argument);
      };
      // Assigned to a pointer member of a returned local, like
      // `Result.m_pValue = &Value`, also through a local reference to it,
      // `CResult &Alias = Result; Alias.m_pValue = &Value`.
      auto CheckAssignments = [&](const VarDecl *Local) {
        auto IsLocal = [&](const ValueDecl *Decl) {
          for (int Depth = 0; Decl && Depth < 4; ++Depth) {
            if (Decl == Local)
              return true;
            const auto *Alias = dyn_cast<VarDecl>(Decl);
            const auto *Init =
                Alias && Alias->getType()->isReferenceType() &&
                        Alias->hasLocalStorage() && Alias->getInit()
                    ? dyn_cast<DeclRefExpr>(Alias->getInit()->IgnoreParenImpCasts())
                    : nullptr;
            Decl = Init ? Init->getDecl() : nullptr;
          }
          return false;
        };
        // Also of a member of it, like `Result.m_Inner.m_pValue = &Value`,
        // or an element of an array member, `Result.m_pValues[0] = &Value`.
        auto IsAssignment = [&](const Stmt *S) {
          // Also all of it, `Result = CResult{&Value}`.
          if (const auto *Whole = dyn_cast<CXXOperatorCallExpr>(S);
              Whole && Whole->getOperator() == OO_Equal &&
              Whole->getNumArgs() == 2)
            if (const auto *Ref = dyn_cast<DeclRefExpr>(
                    Whole->getArg(0)->IgnoreParenImpCasts()))
              return IsLocal(Ref->getDecl());
          const auto *Assign = dyn_cast<BinaryOperator>(S);
          if (!Assign || Assign->getOpcode() != BO_Assign)
            return false;
          const Expr *Base = Assign->getLHS()->IgnoreParenImpCasts();
          bool IsPart = false;
          while (true) {
            if (const auto *Inner = dyn_cast<MemberExpr>(Base)) {
              if (Inner->isArrow())
                return false;
              Base = Inner->getBase()->IgnoreParenImpCasts();
            } else if (const auto *Subscript =
                           dyn_cast<ArraySubscriptExpr>(Base);
                       Subscript && Subscript->getBase()
                                        ->IgnoreParenImpCasts()
                                        ->getType()
                                        ->isArrayType()) {
              Base = Subscript->getBase()->IgnoreParenImpCasts();
            } else {
              break;
            }
            IsPart = true;
          }
          const auto *Ref = dyn_cast<DeclRefExpr>(Base);
          return IsPart && Ref && IsLocal(Ref->getDecl());
        };
        UseCollector Assignments(IsAssignment);
        Assignments.TraverseStmt(Body);
        // Not one that can't run before the `co_return`: in a branch never
        // taken, `if (false)`, or after it, unless a loop around it or a jump
        // goes back.
        const SourceManager &SM = Context.getSourceManager();
        std::optional<bool> GoesBack;
        auto CanRunBefore = [&](const Stmt *Assignment) {
          const DynTypedNode Node = DynTypedNode::create(*Assignment);
          if (isInDiscardedBranch(Node, Context) ||
              isInNeverTakenBranch(Node, Context))
            return false;
          if (!GoesBack) {
            GoesBack = hasJumpTarget(Body);
            for (const Stmt *S = Parents.getParent(Return); S && !*GoesBack;
                 S = Parents.getParent(S))
              GoesBack = isa<ForStmt, WhileStmt, DoStmt, CXXForRangeStmt>(S);
          }
          if (SM.isBeforeInTranslationUnit(Return->getEndLoc(),
                                           Assignment->getBeginLoc()))
            return *GoesBack;
          // Nor one that the same part is assigned again after, before the
          // `co_return`, in a statement of a block around it, which runs on
          // every path to it, `Result.m_pValue = &Value; Result.m_pValue =
          // nullptr;`.
          if (*GoesBack)
            return true;
          const auto *Assign = dyn_cast<BinaryOperator>(Assignment);
          for (const Stmt *Later : Assignments.Uses) {
            const auto *LaterAssign = dyn_cast<BinaryOperator>(Later);
            if (!Assign || !LaterAssign || Later == Assignment ||
                !sameLocalPart(Assign->getLHS(), LaterAssign->getLHS()) ||
                !SM.isBeforeInTranslationUnit(Assignment->getEndLoc(),
                                              Later->getBeginLoc()) ||
                !SM.isBeforeInTranslationUnit(Later->getEndLoc(),
                                              Return->getBeginLoc()))
              continue;
            const Stmt *Statement = Later;
            const Stmt *Above = Parents.getParent(Statement);
            while (isa_and_nonnull<ExprWithCleanups>(Above)) {
              Statement = Above;
              Above = Parents.getParent(Above);
            }
            if (const auto *Block = dyn_cast_or_null<CompoundStmt>(Above);
                Block &&
                Block->getSourceRange().fullyContains(Return->getSourceRange()))
              return false;
          }
          return true;
        };
        for (const Stmt *S : Assignments.Uses) {
          if (!CanRunBefore(S))
            continue;
          if (const auto *Whole = dyn_cast<CXXOperatorCallExpr>(S)) {
            if (Keeps(Whole->getArg(0)->getType(), Whole->getArg(1))) {
              Converted = Whole->getArg(1);
              return;
            }
            continue;
          }
          const auto *Assign = cast<BinaryOperator>(S);
          if (Keeps(Assign->getLHS()->getType(), Assign->getRHS())) {
            Converted = Assign->getRHS();
            return;
          }
        }
      };
      if (Local)
        CheckAssignments(Local);
      if (Converted)
        return;
      // Either value of a conditional, like `b ? CResult{Value} : CResult{}`,
      // and the last of a comma.
      llvm::SmallPtrSet<const VarDecl *, 4> Followed;
      std::function<void(const Expr *)> CheckResult = [&](const Expr *E) {
        E = E->IgnoreImplicit()->IgnoreParens()->IgnoreImplicit();
        while (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(E))
          E = Cast->getSubExpr()->IgnoreImplicit();
        // A copy of what is chosen, `bChoose ? Borrowed : Empty`, is of it.
        if (const auto *Copy = dyn_cast<CXXConstructExpr>(E);
            Copy && Copy->getNumArgs() == 1 &&
            Copy->getConstructor()->isCopyOrMoveConstructor() &&
            isa<AbstractConditionalOperator, BinaryOperator>(
                Copy->getArg(0)->IgnoreImplicit()->IgnoreParens())) {
          CheckResult(Copy->getArg(0));
          return;
        }
        // The one a constant condition selects, `false ? CResult{Value} :
        // CResult{g_Value}`.
        if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E)) {
          bool Selected = false;
          if (!Conditional->getCond()->isValueDependent() &&
              Conditional->getCond()->EvaluateAsBooleanCondition(Selected,
                                                                 Context)) {
            CheckResult(Selected ? Conditional->getTrueExpr()
                                 : Conditional->getFalseExpr());
            return;
          }
          CheckResult(Conditional->getTrueExpr());
          CheckResult(Conditional->getFalseExpr());
          return;
        }
        if (const auto *Comma = dyn_cast<BinaryOperator>(E);
            Comma && Comma->getOpcode() == BO_Comma) {
          CheckResult(Comma->getRHS());
          return;
        }
        // A local, like the `Borrowed` of `bChoose ? Borrowed : Empty`, is
        // what its initializer made, and what is assigned to its members.
        if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
          const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
          if (Variable && Variable != Local && Variable->hasLocalStorage() &&
              !isa<ParmVarDecl>(Variable) && Variable->getInit() &&
              !Borrow->IsRoot(E) && Followed.insert(Variable).second) {
            CheckAssignments(Variable);
            if (!Converted)
              CheckResult(Variable->getInit());
          }
          return;
        }
        // An aggregate result with reference or pointer members, like
        // `CResult{Value}`, also in the initializers of its bases, which come
        // first.
        if (const std::optional<CAggregateInit> List = aggregateInit(E)) {
          // A union initializes the one member it names.
          if (const FieldDecl *Field = List->UnionField) {
            if (!List->Inits.empty() && Keeps(Field->getType(), List->Inits[0]))
              Converted = List->Inits[0];
            return;
          }
          if (const auto *Record = List->Type->getAsCXXRecordDecl()) {
            unsigned iInit = 0;
            for (const CXXBaseSpecifier &Base : Record->bases()) {
              if (iInit < List->Inits.size() &&
                  Keeps(Base.getType(), List->Inits[iInit]))
                Converted = List->Inits[iInit];
              ++iInit;
            }
            for (const FieldDecl *Field : Record->fields()) {
              // An unnamed bit-field isn't initialized, `int : 0`.
              if (Field->isUnnamedBitField())
                continue;
              if (iInit < List->Inits.size() &&
                  Keeps(Field->getType(), List->Inits[iInit]))
                Converted = List->Inits[iInit];
              ++iInit;
            }
          }
          return;
        }
        const auto *Construct = dyn_cast<CXXConstructExpr>(E);
        if (!Construct || Construct->getConstructor()->isCopyOrMoveConstructor())
          return;
        const CXXConstructorDecl *Constructor = Construct->getConstructor();
        for (unsigned iArgument = 0; iArgument < Construct->getNumArgs() &&
                                     iArgument < Constructor->getNumParams();
             ++iArgument) {
          const QualType Type = Constructor->getParamDecl(iArgument)->getType();
          if (constructorMayKeep(Constructor, iArgument) &&
              Keeps(Type, Construct->getArg(iArgument)))
            Converted = Construct->getArg(iArgument);
        }
      };
      CheckResult(Operand);
    };
    Visit(Body);
    return Converted;
  }
  // Also through what a lambda called where it is made returns, `co_return
  // [&Value] { return &Value; }();`, or one held in a local that is only
  // called, `auto fGet = [&Value] { return &Value; }; co_return fGet();`.
  std::function<bool(const Stmt *, int)> InResultFrom = [&](const Stmt *S,
                                                            int Depth) {
    bool InReturn = false;
    for (; S && Depth < 64; ++Depth) {
      if (isa<CoreturnStmt>(S))
        return true;
      if (isa<ReturnStmt>(S))
        InReturn = true;
      if (const auto *Lambda = dyn_cast<LambdaExpr>(S);
          Lambda && InReturn && isCalledWhereMade(Lambda, Parents))
        InReturn = false;
      else if (Lambda && InReturn) {
        llvm::SmallVector<const CXXOperatorCallExpr *, 2> Calls;
        return onlyCalledInLocal(Lambda, Parents, &Calls) &&
               llvm::any_of(Calls, [&](const CXXOperatorCallExpr *Call) {
                 return InResultFrom(Call, Depth + 1);
               });
      } else if (isa<LambdaExpr, BlockExpr>(S))
        return false;
      S = Parents.getParent(S);
    }
    return false;
  };
  auto InResult = [&](const Stmt *S) { return InResultFrom(S, 0); };
  // The locals that `co_return` gives, which what they are initialized with
  // reaches the result through.
  // Worked out once for the function, the same for each root.
  if (!Flow.Returned) {
    Flow.Returned.emplace();
    llvm::SmallPtrSet<const ValueDecl *, 4> &Returned = *Flow.Returned;
    {
      auto IsReturn = [](const Stmt *S) { return isa<CoreturnStmt>(S); };
      UseCollector Returns(IsReturn);
      Returns.TraverseStmt(Body);
      std::function<void(const Stmt *)> Collect = [&](const Stmt *S) {
        if (!S)
          return;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
          Returned.insert(Ref->getDecl());
        for (const Stmt *Child : childrenOnce(S))
          Collect(Child);
      };
      for (const Stmt *Return : Returns.Uses)
        Collect(cast<CoreturnStmt>(Return)->getOperand());
      // Also the locals they are made from, like `pFirst` of `int *pSecond =
      // pFirst`, or are assigned, also as either of a conditional,
      // `(bFirst ? pFirst : pSecond) = ...`, each once, however many.
      llvm::DenseMap<const ValueDecl *, llvm::SmallVector<const Expr *, 2>>
          AssignedValues;
      {
        // Also by the assignment operator of a class, `Result = _Text`, and
        // through a helper that assigns what it is given to what it is given
        // by reference, `fg_SetResult(pResult, _pText)`.
        auto IsAssignment = [](const Stmt *S) {
          const auto *Assign = dyn_cast<BinaryOperator>(S);
          const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S);
          return (Assign && Assign->getOpcode() == BO_Assign) ||
                 (Operator && Operator->getOperator() == OO_Equal &&
                  Operator->getNumArgs() == 2) ||
                 (isa<CallExpr>(S) && !Operator &&
                  !assignedParameters(cast<CallExpr>(S)->getDirectCallee())
                       .empty());
        };
        UseCollector Assignments(IsAssignment);
        Assignments.TraverseStmt(Body);
        for (const Stmt *Assignment : Assignments.Uses) {
          if (const auto *Assign = dyn_cast<BinaryOperator>(Assignment)) {
            for (const DeclRefExpr *Target :
                 assignedVariables(Assign->getLHS()))
              AssignedValues[referencedLocal(Target->getDecl())].push_back(
                  Assign->getRHS());
            continue;
          }
          if (const auto *Operator =
                  dyn_cast<CXXOperatorCallExpr>(Assignment)) {
            for (const DeclRefExpr *Target :
                 assignedVariables(Operator->getArg(0)))
              AssignedValues[referencedLocal(Target->getDecl())].push_back(
                  Operator->getArg(1));
            continue;
          }
          const auto *Call = cast<CallExpr>(Assignment);
          for (const auto &[iTarget, iSource] :
               assignedParameters(Call->getDirectCallee()))
            if (iTarget < Call->getNumArgs() && iSource < Call->getNumArgs())
              for (const DeclRefExpr *Target :
                   assignedVariables(Call->getArg(iTarget)))
                AssignedValues[referencedLocal(Target->getDecl())].push_back(
                    Call->getArg(iSource));
        }
      }
      llvm::SmallVector<const ValueDecl *, 8> Work(Returned.begin(),
                                                   Returned.end());
      while (!Work.empty()) {
        {
          const ValueDecl *Decl = Work.pop_back_val();
          const auto *Local = dyn_cast<VarDecl>(Decl);
          // Also a parameter, through what it is assigned, `_pValue = pCopy`.
          if (!Local || !Local->hasLocalStorage())
            continue;
          // The local that is the value, not what is read through it, like
          // `*pFirst`.
          auto CollectLocals = [&](const Expr *From) {
            // Each expression once, however large, `pFirst ? pFirst : g_Data +
            // 1 + 2 + ...`: one left unexamined could be the one.
            llvm::SmallVector<const Expr *, 4> Worklist{From};
            llvm::SmallPtrSet<const Expr *, 16> Seen;
            while (!Worklist.empty()) {
              const Expr *E = skipWrappers(Worklist.pop_back_val())
                                  ->IgnoreParenImpCasts();
              if (!Seen.insert(E).second)
                continue;
              if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E))
                Worklist.push_back(Cast->getSubExpr());
              // Also one moved or copied, `fg_Move(First)`.
              else if (const auto *Copy = dyn_cast<CXXConstructExpr>(E);
                       Copy && Copy->getNumArgs() == 1 &&
                       Copy->getConstructor()->isCopyOrMoveConstructor())
                Worklist.push_back(Copy->getArg(0));
              else if (const auto *Move = dyn_cast<CallExpr>(E);
                       Move && Move->getNumArgs() == 1 &&
                       isMoveOrForwardFunction(calleeName(Move)))
                Worklist.push_back(Move->getArg(0));
              // Also a pointer it points to, `int **ppValue = &pValue`, whose
              // value is read through it, `co_return *ppValue`.
              else if (const auto *Address = dyn_cast<UnaryOperator>(E);
                       Address && Address->getOpcode() == UO_AddrOf &&
                       Address->getSubExpr()
                           ->IgnoreParenImpCasts()
                           ->getType()
                           ->isPointerType())
                Worklist.push_back(Address->getSubExpr());
              else if (const auto *Conditional =
                           dyn_cast<AbstractConditionalOperator>(E)) {
                Worklist.push_back(Conditional->getTrueExpr());
                Worklist.push_back(Conditional->getFalseExpr());
              } else if (const auto *Binary = dyn_cast<BinaryOperator>(E);
                         Binary && (Binary->getOpcode() == BO_Comma ||
                                    Binary->isAdditiveOp())) {
                Worklist.push_back(Binary->getRHS());
                if (Binary->isAdditiveOp())
                  Worklist.push_back(Binary->getLHS());
              } else if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
                if (const auto *Other = dyn_cast<VarDecl>(Ref->getDecl());
                    Other && Other->hasLocalStorage() &&
                    Returned.insert(Other).second)
                  Work.push_back(Other);
              }
            }
          };
          if (Local->getInit() && !isa<ParmVarDecl>(Local))
            CollectLocals(Local->getInit());
          if (auto It = AssignedValues.find(Local); It != AssignedValues.end())
            for (const Expr *Value : It->second)
              CollectLocals(Value);
        }
      }
    }
  }
  const llvm::SmallPtrSet<const ValueDecl *, 4> &Returned = *Flow.Returned;
  // Whether \p Value, what \p Local is made with or one of what it is
  // assigned, can be what it gives a `co_return`, unless each one that gives
  // it has it assigned something else on every path before, `pResult =
  // nullptr; co_return pResult;`.
  auto ValueReturned = [&](const VarDecl *Local, const Expr *Value) {
    // Each `co_return` that gives it as it is, and whether one gives it
    // another way, `co_return pResult + 1`, which can't be told.
    int nDirect = 0;
    bool Init = false;
    std::function<bool(const Stmt *)> Mentions = [&](const Stmt *S) {
      if (const auto *Ref = dyn_cast_or_null<DeclRefExpr>(S))
        return Ref->getDecl() == Local;
      return S && llvm::any_of(childrenOnce(S), Mentions);
    };
    // Whether \p E can give what it is made with: a use of it that reads it
    // where that can't be told, but not one after it is assigned, `pResult =
    // g_pData; co_return pResult + 1;`, also in the operand, `co_return
    // (pResult = nullptr)` or `(pResult = nullptr, pResult)`.
    std::function<bool(const Expr *)> CarriesInit = [&](const Expr *E) {
      E = E->IgnoreParenImpCasts();
      auto AssignsLocal = [&](const Expr *Side) {
        const auto *Assign =
            dyn_cast<BinaryOperator>(Side->IgnoreParenImpCasts());
        const auto *Target =
            Assign && Assign->getOpcode() == BO_Assign
                ? dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts())
                : nullptr;
        return Target && Target->getDecl() == Local ? Assign : nullptr;
      };
      if (const BinaryOperator *Assign = AssignsLocal(E))
        return Assign->getRHS() == Value || Mentions(Assign->getRHS());
      if (const auto *Comma = dyn_cast<BinaryOperator>(E);
          Comma && Comma->getOpcode() == BO_Comma) {
        if (const BinaryOperator *Assign = AssignsLocal(Comma->getLHS()))
          return Assign->getRHS() == Value || Mentions(Assign->getRHS());
        return CarriesInit(Comma->getRHS());
      }
      bool Carries = false;
      std::function<void(const Stmt *)> Uses = [&](const Stmt *S) {
        if (!S || Carries)
          return;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
            Ref && Ref->getDecl() == Local) {
          const llvm::SmallVector<const Expr *, 2> Held =
              assignedValuesAt(Local, Ref);
          Carries = Held.empty() || llvm::is_contained(Held, Value);
          return;
        }
        for (const Stmt *Child : childrenOnce(S))
          Uses(Child);
      };
      Uses(E);
      return Carries;
    };
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || Init || isa<LambdaExpr, BlockExpr>(S))
        return;
      if (const auto *Return = dyn_cast<CoreturnStmt>(S)) {
        const Expr *Operand = Return->getOperand();
        if (!Operand || !Mentions(Operand))
          return;
        ++nDirect;
        if (CarriesInit(Operand))
          Init = true;
        return;
      }
      for (const Stmt *Child : childrenOnce(S))
        Visit(Child);
    };
    Visit(Body);
    if (Init || nDirect)
      return Init;
    // Given only through others, `pOther = pResult`, what it holds where each
    // reads it, not when it is assigned something else before, `pResult =
    // nullptr; int *pOther = pResult;`, and a lambda that uses it can't be
    // told.
    bool Carries = false;
    std::function<void(const Stmt *)> Reads = [&](const Stmt *S) {
      if (!S || Carries)
        return;
      if (isa<LambdaExpr, BlockExpr>(S)) {
        Carries = Mentions(S);
        return;
      }
      if (const auto *Assign = dyn_cast<BinaryOperator>(S);
          Assign && Assign->getOpcode() == BO_Assign)
        if (const auto *Target =
                dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts());
            Target && Target->getDecl() == Local) {
          Reads(Assign->getRHS());
          return;
        }
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
          Ref && Ref->getDecl() == Local) {
        const llvm::SmallVector<const Expr *, 2> Held =
            assignedValuesAt(Local, Ref);
        Carries = Held.empty() || llvm::is_contained(Held, Value);
        return;
      }
      for (const Stmt *Child : childrenOnce(S))
        Reads(Child);
    };
    Reads(Body);
    return Carries;
  };
  auto InReturnedLocal = [&](const Stmt *S) {
    for (int Depth = 0; S && Depth < 64; ++Depth) {
      const Stmt *Parent = Parents.getParent(S);
      // Assigned to it, like `pValue = &Value`, also as either of a
      // conditional, `(bFirst ? pFirst : pSecond) = &Value`.
      // Not one that is assigned something else after it on every path to
      // each `co_return` that gives it, `pResult = &Value; pResult =
      // &g_Value; co_return pResult;`.
      if (const auto *Assign = dyn_cast_or_null<BinaryOperator>(Parent);
          Assign && Assign->getOpcode() == BO_Assign && Assign->getRHS() == S) {
        const llvm::SmallVector<const DeclRefExpr *, 2> Targets =
            assignedVariables(Assign->getLHS());
        return llvm::any_of(Targets, [&](const DeclRefExpr *Target) {
          const auto *Local = dyn_cast<VarDecl>(Target->getDecl());
          if (Targets.size() == 1 && Local && Local->hasLocalStorage() &&
              referencedLocal(Local) == Local &&
              Assign->getLHS()->IgnoreParenImpCasts() == Target)
            return Returned.contains(Local) &&
                   ValueReturned(Local, Assign->getRHS());
          return Returned.contains(referencedLocal(Target->getDecl()));
        });
      }
      // Also by the assignment operator of a class, `Result = _Text`, and
      // through a helper that assigns it to what it is given by reference,
      // `fg_SetResult(pResult, _pText)`.
      if (const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Parent);
          Operator && Operator->getOperator() == OO_Equal &&
          Operator->getNumArgs() == 2 && Operator->getArg(1) == S)
        return llvm::any_of(assignedVariables(Operator->getArg(0)),
                            [&](const DeclRefExpr *Target) {
                              return Returned.contains(
                                  referencedLocal(Target->getDecl()));
                            });
      if (const auto *Call = dyn_cast_or_null<CallExpr>(Parent);
          Call && !isa<CXXOperatorCallExpr>(Call))
        for (const auto &[iTarget, iSource] :
             assignedParameters(Call->getDirectCallee()))
          if (iSource < Call->getNumArgs() && Call->getArg(iSource) == S &&
              iTarget < Call->getNumArgs() &&
              llvm::any_of(
                  assignedVariables(Call->getArg(iTarget)),
                  [&](const DeclRefExpr *Target) {
                    return Returned.contains(
                        referencedLocal(Target->getDecl()));
                  }))
            return true;
      if (const auto *Declaration = dyn_cast_or_null<DeclStmt>(Parent)) {
        for (const Decl *D : Declaration->decls()) {
          const auto *Local = dyn_cast<VarDecl>(D);
          if (Local && Local->getInit() && Returned.contains(Local) &&
              Local->getInit()->getSourceRange().fullyContains(
                  S->getSourceRange()))
            return ValueReturned(Local, Local->getInit());
        }
        return false;
      }
      if (!Parent || !isa<Expr>(Parent) || isa<LambdaExpr, BlockExpr>(Parent))
        return false;
      S = Parent;
    }
    return false;
  };
  UseCollector Roots(Borrow->IsRoot);
  if (Borrow->Decl)
    Roots.Uses = Collector.Uses;
  else
    Roots.TraverseStmt(Body);
  // What the suspensions await, past implicit nodes.
  llvm::SmallPtrSet<const Expr *, 8> Awaited;
  if (!Flow.Suspensions) {
    UseCollector Awaits(
        [](const Stmt *S) { return isa<CoroutineSuspendExpr>(S); });
    Awaits.TraverseStmt(Body);
    Flow.Suspensions = Awaits.Uses;
  }
  for (const Stmt *Await : *Flow.Suspensions) {
    // Past `fg_Move`, `fg_Forward`, casts, conditionals and commas, which
    // give what they get.
    llvm::SmallVector<const Expr *, 4> Worklist{
        cast<CoroutineSuspendExpr>(Await)->getOperand()};
    for (int Count = 0; !Worklist.empty() && Count < 32; ++Count) {
      const Expr *E = Worklist.pop_back_val();
      if (!E)
        continue;
      E = E->IgnoreImplicit()->IgnoreParens();
      Awaited.insert(E);
      if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E)) {
        Worklist.push_back(Cast->getSubExpr());
        continue;
      }
      if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E)) {
        Worklist.push_back(Conditional->getTrueExpr());
        Worklist.push_back(Conditional->getFalseExpr());
        continue;
      }
      if (const auto *Binary = dyn_cast<BinaryOperator>(E);
          Binary && Binary->getOpcode() == BO_Comma) {
        Worklist.push_back(Binary->getRHS());
        continue;
      }
      // What an awaiter is made from, `CAwaiter{Data.f_Get()}`, which it can
      // keep a reference to.
      if (const std::optional<CAggregateInit> List = aggregateInit(E)) {
        for (const Expr *Init : List->Inits)
          Worklist.push_back(Init);
        continue;
      }
      if (const auto *Construct = dyn_cast<CXXConstructExpr>(E)) {
        for (const Expr *Argument : Construct->arguments())
          Worklist.push_back(Argument);
        continue;
      }
      if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(E)) {
        Worklist.push_back(Cast->getSubExpr());
        continue;
      }
      const auto *Call = dyn_cast<CallExpr>(E);
      const StringRef Name = Call ? calleeName(Call) : StringRef();
      if (Call && Call->getNumArgs() == 1 &&
          (isMoveFunction(Name) || isForwardFunction(Name)))
        Worklist.push_back(Call->getArg(0));
    }
  }
  for (const Stmt *Root : Roots.Uses) {
    // A parameter pointed at a local first, like `_pValue = &Local`, borrows
    // nothing after that, and neither does a local reference that only points
    // it elsewhere.
    if (IsRedirected(Root) || IsAssignedAlias(Root))
      continue;
    const auto *E = dyn_cast<Expr>(Root);
    // A lambda that captures a copy uses its own copy, and code that can't run
    // uses nothing.
    // Without a suspension, or joined, what refers to it before the result
    // is given is gone by then, unless the result is made from it, also
    // through a local, like `auto *pValue = &Value; co_return pValue;`.
    if (E &&
        (!OnlyResults || InResult(Root) ||
         (ResultCanRefer && InReturnedLocal(Root))) &&
        useCanRun(Root, Parents, Flow, Borrow) &&
        !isValueUse(E, Parents, *Borrow, Awaited, Context, Result))
      return Root;
  }
  return nullptr;
}

bool isCoroutine(const FunctionDecl *Function) {
  const Stmt *Body = Function->getBody();
  if (!Body)
    return false;
  if (isa<CoroutineBodyStmt>(Body))
    return true;
  CoroutineKeywordFinder Finder;
  Finder.TraverseStmt(const_cast<Stmt *>(Body));
  return Finder.Found;
}

/// Whether the coroutine flags \p Flags, a value of `ECoroutineFlag`, allow
/// references. Flags that aren't a value of an enum are taken to allow them.
static bool allowsReferences(const TemplateArgument &Flags) {
  if (Flags.getKind() != TemplateArgument::Integral)
    return true;
  const auto *Enum = Flags.getIntegralType()->getAsEnumDecl();
  if (!Enum)
    return true;
  for (const EnumConstantDecl *Enumerator : Enum->enumerators()) {
    if (Enumerator->getName() == "ECoroutineFlag_AllowReferences")
      return (Flags.getAsIntegral() & Enumerator->getInitVal()) != 0;
  }
  return true;
}

/// Whether the future template \p Name with \p Arguments is protected.
static bool isProtectedFuture(StringRef Name,
                              llvm::ArrayRef<TemplateArgument> Arguments) {
  if (Name == "TCFuture")
    return true;
  if (Name == "TCFutureWithFlags")
    return Arguments.size() >= 2 && !allowsReferences(Arguments[1]);
  return false;
}

bool returnsProtectedFuture(const FunctionDecl *Function) {
  return isProtectedFutureType(Function->getReturnType(),
                               Function->getASTContext());
}

bool isProtectedFutureType(QualType Return, const ASTContext &Context) {
  if (Return.isNull())
    return false;
  // `TCUnsafeFuture` is an alias of `TCFutureWithFlags`, so look at the
  // aliases on the way.
  for (QualType Type = Return;;) {
    if (const auto *Specialization =
            dyn_cast<TemplateSpecializationType>(Type.getTypePtr()))
      if (const TemplateDecl *Template =
              Specialization->getTemplateName().getAsTemplateDecl();
          Template && Template->getName() == "TCUnsafeFuture")
        return false;
    const QualType Next = Type.getSingleStepDesugaredType(Context);
    if (Next == Type)
      break;
    Type = Next;
  }
  // The canonical specialization has the evaluated flags.
  const auto *Specialization = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Return->getAsCXXRecordDecl());
  return Specialization &&
         isProtectedFuture(Specialization->getSpecializedTemplate()->getName(),
                           Specialization->getTemplateArgs().asArray());
}

namespace {

/// Collects the instantiations of the templated functions of a translation
/// unit, and of the call operators of the lambdas they make.
class InstantiationCollector
    : public OncePerTypeVisitor<InstantiationCollector> {
public:
  explicit InstantiationCollector(
      llvm::DenseMap<const FunctionDecl *,
                     llvm::SmallVector<const FunctionDecl *, 2>> &Instantiations)
      : Instantiations(Instantiations) {}

  /// The call operators of lambdas in instantiated code, and their
  /// specializations, by where they are written: the call operator of a
  /// lambda in an instantiation isn't known to come from the one in the
  /// template.
  llvm::DenseMap<SourceLocation, llvm::SmallVector<const FunctionDecl *, 2>>
      LambdaInstances;

  bool shouldVisitTemplateInstantiations() const { return true; }

  bool VisitFunctionDecl(FunctionDecl *Function) {
    add(Function);
    return true;
  }

  // The call operators of lambdas, also the specializations of generic ones,
  // whose bodies aren't traversed otherwise.
  bool VisitLambdaExpr(LambdaExpr *Lambda) {
    CXXMethodDecl *Operator = Lambda->getCallOperator();
    Lambdas.try_emplace(Operator->getCanonicalDecl(), Lambda);
    add(Operator);
    if (const FunctionTemplateDecl *Template =
            Operator->getDescribedFunctionTemplate()) {
      for (FunctionDecl *Specialization : Template->specializations()) {
        Lambdas.try_emplace(Specialization->getCanonicalDecl(), Lambda);
        if (add(Specialization) && Specialization->getBody())
          TraverseStmt(Specialization->getBody());
      }
    }
    return true;
  }

  llvm::DenseMap<const FunctionDecl *, const LambdaExpr *> Lambdas;

private:
  bool add(const FunctionDecl *Function) {
    // Not one another file makes, `extern template`, which checks it there.
    if (!Function->hasBody() || isExternInstantiation(Function) ||
        !Visited.insert(Function).second)
      return false;
    // Under each pattern it comes from, like the call operator of a generic
    // lambda in an instantiated template, which comes from the one in the
    // template.
    const FunctionDecl *Pattern = Function;
    for (int Depth = 0; Depth < 8; ++Depth) {
      const FunctionDecl *Next = Pattern->getTemplateInstantiationPattern();
      if (!Next || Next == Pattern)
        break;
      Pattern = Next;
      if (!Function->isDependentContext())
        Instantiations[Pattern->getCanonicalDecl()].push_back(Function);
    }
    if (!Function->isDependentContext() && isLambdaCallOperator(Function))
      LambdaInstances[Function->getLocation()].push_back(Function);
    return true;
  }

  llvm::DenseMap<const FunctionDecl *,
                 llvm::SmallVector<const FunctionDecl *, 2>> &Instantiations;
  llvm::DenseSet<const FunctionDecl *> Visited;
};

} // namespace

InstantiationIndex &sharedInstantiationIndex() {
  static thread_local InstantiationIndex s_Index;
  return s_Index;
}

llvm::ArrayRef<const FunctionDecl *>
InstantiationIndex::instantiationsOf(const FunctionDecl *Pattern,
                                     ASTContext &Context) {
  if (const uint64_t AST = astIdentity(Context); IndexedAST != AST) {
    IndexedAST = AST;
    Instantiations.clear();
    Merged.clear();
    InstantiationCollector Collector(Instantiations);
    // Only what is checked, like the declarations of the headers the plugin
    // reports in a compile: the instantiations of a template are under it.
    for (Decl *Scope : Context.getTraversalScope())
      Collector.TraverseDecl(Scope);
    Lambdas = std::move(Collector.Lambdas);
    LambdaInstances = std::move(Collector.LambdaInstances);
  }
  // The call operator of a lambda in a template, by where it is written.
  if (isLambdaCallOperator(Pattern) && Pattern->isDependentContext()) {
    auto [It, Inserted] =
        Merged.try_emplace(Pattern->getCanonicalDecl());
    if (Inserted) {
      llvm::SmallPtrSet<const FunctionDecl *, 8> Seen;
      if (auto Found = Instantiations.find(Pattern->getCanonicalDecl());
          Found != Instantiations.end()) {
        for (const FunctionDecl *Instance : Found->second) {
          if (Seen.insert(Instance).second)
            It->second.push_back(Instance);
        }
      }
      if (auto Found = LambdaInstances.find(Pattern->getLocation());
          Found != LambdaInstances.end()) {
        for (const FunctionDecl *Instance : Found->second) {
          if (Instance != Pattern && Seen.insert(Instance).second)
            It->second.push_back(Instance);
        }
      }
    }
    return It->second;
  }
  auto It = Instantiations.find(Pattern->getCanonicalDecl());
  if (It == Instantiations.end())
    return {};
  return It->second;
}

const LambdaExpr *InstantiationIndex::lambdaOf(const FunctionDecl *Operator,
                                               ASTContext &Context) {
  (void)instantiationsOf(Operator, Context);
  return Lambdas.lookup(Operator->getCanonicalDecl());
}

/// Whether \p Record derives from a class named one of \p Bases.
static bool derivesFrom(const CXXRecordDecl *Record,
                        llvm::ArrayRef<StringRef> Bases) {
  // Visit each class of a hierarchy once, so shared bases don't make the
  // search exponential.
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Visited;
  llvm::SmallVector<const CXXRecordDecl *, 8> Worklist{Record};
  while (!Worklist.empty()) {
    const CXXRecordDecl *Current = Worklist.pop_back_val();
    if (!Current || !Visited.insert(Current->getCanonicalDecl()).second)
      continue;
    if (Current->getIdentifier() &&
        llvm::is_contained(Bases, Current->getName()))
      return true;
    if (!Current->hasDefinition())
      continue;
    for (const CXXBaseSpecifier &Base : Current->bases())
      Worklist.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  return false;
}

bool isActorClass(const CXXRecordDecl *Record) {
  return derivesFrom(Record, {"CActor", "CActorInternal"});
}

const CXXRecordDecl *enclosingThisClass(const LambdaExpr *Lambda) {
  const DeclContext *Context = Lambda->getLambdaClass()->getDeclContext();
  while (Context) {
    if (const auto *Method = dyn_cast<CXXMethodDecl>(Context)) {
      if (!Method->getParent()->isLambda())
        return Method->getParent();
      Context = Method->getParent()->getDeclContext();
      continue;
    }
    // A lambda in a default member initializer.
    if (const auto *Record = dyn_cast<CXXRecordDecl>(Context))
      return Record->isLambda() ? nullptr : Record;
    return nullptr;
  }
  return nullptr;
}

const Expr *skipWrappers(const Expr *E) {
  for (;;) {
    const Expr *Next = E->IgnoreImplicit()->IgnoreParens();
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(Next);
        Construct && Construct->getNumArgs() == 1 &&
        !isa<CXXTemporaryObjectExpr>(Construct))
      Next = Construct->getArg(0);
    if (Next == E)
      return E;
    E = Next;
  }
}

/// What is worked out of each method at each depth while one is looked
/// into, so that helpers that call each other many times are each looked into
/// once there.
using COwnCallsMemo =
    llvm::DenseMap<std::pair<const CXXMethodDecl *, int>, bool>;
static bool
callsOwnMethod(const CXXMethodDecl *Method,
               llvm::function_ref<bool(const CXXMethodDecl *)> Matches,
               int Depth, COwnCallsMemo &Memo);
static bool
callsOwnMethodUncached(const CXXMethodDecl *Method,
                       llvm::function_ref<bool(const CXXMethodDecl *)> Matches,
                       int Depth, COwnCallsMemo &Memo) {
  if (Matches(Method))
    return true;
  const FunctionDecl *Definition = Method->getDefinition();
  if (Depth >= 4 || !Definition || !Definition->getBody())
    return false;
  // Its object, `this`, or the explicit object parameter, `this TCVector
  // &_Self`.
  const ParmVarDecl *Self = Method->isExplicitObjectMemberFunction() &&
                                    Definition->getNumParams() >= 1
                                ? Definition->getParamDecl(0)
                                : nullptr;
  auto IsSelf = [&](const Expr *Object) {
    Object = Object->IgnoreParenImpCasts();
    if (isa<CXXThisExpr>(Object))
      return true;
    const auto *Ref = dyn_cast<DeclRefExpr>(Object);
    return Self && Ref && Ref->getDecl() == Self;
  };
  std::function<bool(const Stmt *)> Calls = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return false;
    if (const auto *Inner = dyn_cast<CXXMemberCallExpr>(S);
        Inner && Inner->getMethodDecl() && Inner->getImplicitObjectArgument() &&
        IsSelf(Inner->getImplicitObjectArgument()) &&
        !hasConstObject(Inner->getMethodDecl()) &&
        callsOwnMethod(Inner->getMethodDecl(), Matches, Depth + 1, Memo))
      return true;
    // Not in what never runs, `if constexpr (false) f_Clear();`.
    return llvm::any_of(evaluatedChildren(S, Definition->getASTContext()),
                        Calls);
  };
  return Calls(Definition->getBody());
}

static bool
callsOwnMethod(const CXXMethodDecl *Method,
               llvm::function_ref<bool(const CXXMethodDecl *)> Matches,
               int Depth, COwnCallsMemo &Memo) {
  if (auto It = Memo.find({Method, Depth}); It != Memo.end())
    return It->second;
  const bool Calls = callsOwnMethodUncached(Method, Matches, Depth, Memo);
  Memo.try_emplace({Method, Depth}, Calls);
  return Calls;
}

bool callsOwnMethod(const CXXMethodDecl *Method,
                    llvm::function_ref<bool(const CXXMethodDecl *)> Matches) {
  COwnCallsMemo Memo;
  return callsOwnMethod(Method, Matches, 0, Memo);
}

/// Whether the local function wrapper \p Variable is never given another
/// function: only called, `fRead()`, read by its const members, copied, or
/// moved from, `fg_Move(fRead)`, which leaves it nothing to call, not
/// assigned, `fRead = ...`, nor given to what can change it, `fg_Swap(fRead,
/// fOther)`.
static bool isUnchangedWrapper(const VarDecl *Variable) {
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return false;
  struct CWrappers {
    llvm::DenseMap<const VarDecl *, bool> Unchanged;
  };
  auto &Decided = astCache<CWrappers>(Variable->getASTContext()).Unchanged;
  if (auto It = Decided.find(Variable); It != Decided.end())
    return It->second;
  class CUses : public OncePerTypeVisitor<CUses> {
  public:
    explicit CUses(const VarDecl *Variable) : Variable(Variable) {}
    bool shouldVisitLambdaBody() const { return true; }
    void allow(const Expr *E) {
      if (const auto *Ref =
              E ? dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts()) : nullptr;
          Ref && Ref->getDecl() == Variable)
        Allowed.insert(Ref);
    }
    bool VisitCallExpr(CallExpr *Call) {
      if (const CallExpr *Move =
              asCallTo(Call, vocabulary().MoveFunctions.names());
          Move && Move->getNumArgs() == 1)
        allow(Move->getArg(0));
      // Given to fg_CallSafe, which copies or moves it to call it.
      if (calleeName(Call).starts_with("fg_CallSafe"))
        for (const Expr *Argument : Call->arguments())
          allow(Argument);
      return true;
    }
    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *Call) {
      if (Call->getOperator() == OO_Call && Call->getNumArgs() >= 1)
        allow(Call->getArg(0));
      return true;
    }
    bool VisitCXXMemberCallExpr(CXXMemberCallExpr *Call) {
      if (const CXXMethodDecl *Method = Call->getMethodDecl();
          Method &&
          (Method->isConst() || Method->getOverloadedOperator() == OO_Call))
        allow(Call->getImplicitObjectArgument());
      return true;
    }
    bool VisitCXXConstructExpr(CXXConstructExpr *Construct) {
      if (Construct->getNumArgs() == 1 &&
          Construct->getConstructor()->isCopyConstructor())
        allow(Construct->getArg(0));
      return true;
    }
    bool VisitCXXStaticCastExpr(CXXStaticCastExpr *Cast) {
      if (Cast->getTypeAsWritten()->isRValueReferenceType())
        allow(Cast->getSubExpr());
      return true;
    }
    // Bound to a local reference that is unchanged in turn, `auto &fAlias =
    // fRead`.
    bool VisitVarDecl(VarDecl *Alias) {
      if (Alias != Variable && Alias->hasLocalStorage() &&
          Alias->getType()->isReferenceType() && Alias->getInit())
        if (const auto *Ref =
                dyn_cast<DeclRefExpr>(Alias->getInit()->IgnoreParenImpCasts());
            Ref && Ref->getDecl() == Variable && isUnchangedWrapper(Alias))
          Allowed.insert(Ref);
      return true;
    }
    bool VisitDeclRefExpr(DeclRefExpr *Ref) {
      if (Ref->getDecl() == Variable)
        Refs.push_back(Ref);
      return true;
    }
    const VarDecl *Variable;
    llvm::SmallPtrSet<const DeclRefExpr *, 8> Allowed;
    llvm::SmallVector<const DeclRefExpr *, 8> Refs;
  };
  CUses Uses(Variable);
  Uses.TraverseStmt(Function->getBody());
  const bool Unchanged = llvm::all_of(Uses.Refs, [&](const DeclRefExpr *Ref) {
    return Uses.Allowed.contains(Ref);
  });
  Decided[Variable] = Unchanged;
  return Unchanged;
}

const LambdaExpr *asLambda(const Expr *E) {
  // Each copy is made from one declared before it, so a chain of them ends.
  for (int Depth = 0; Depth < 256; ++Depth) {
    E = skipWrappers(E);
    // Braces without a type around it, `{[...] {...}}`.
    if (const auto *List = dyn_cast<InitListExpr>(E);
        List && List->getNumInits() == 1 &&
        (List->getType().isNull() || List->getType()->isVoidType())) {
      E = List->getInit(0);
      continue;
    }
    if (const auto *Lambda = dyn_cast<LambdaExpr>(E))
      return Lambda;
    if (const CallExpr *Move = asCallTo(E, vocabulary().MoveFunctions.names());
        Move && Move->getNumArgs() == 1) {
      E = Move->getArg(0);
      continue;
    }
    // Also moved by a cast, `static_cast<TCFunction<int ()> &&>(fRead)`.
    if (const auto *Cast = dyn_cast<CXXStaticCastExpr>(E);
        Cast && Cast->getTypeAsWritten()->isRValueReferenceType()) {
      E = Cast->getSubExpr();
      continue;
    }
    // Wrapped by name, `TCFunction<int ()>(...)` or `TCFunction<int
    // ()>{...}`, which calls what it is made with.
    if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(E)) {
      E = Cast->getSubExpr();
      continue;
    }
    if (const auto *Construct = dyn_cast<CXXTemporaryObjectExpr>(E);
        Construct && Construct->getNumArgs() == 1 &&
        !Construct->getConstructor()->isCopyOrMoveConstructor()) {
      E = Construct->getArg(0);
      continue;
    }
    // A closure can't be assigned another lambda, so a local of a closure
    // type holds the lambda it is initialized with.
    const auto *Ref = dyn_cast<DeclRefExpr>(E);
    const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (!Variable || !Variable->hasLocalStorage() || !Variable->getInit())
      return nullptr;
    // In a template, the type of `auto fLambda = [...]` can be left
    // undeduced.
    const CXXRecordDecl *Closure =
        Variable->getType().getNonReferenceType()->getAsCXXRecordDecl();
    // A local function wrapper that is never given another function, like
    // `TCFunctionMovable<…> fLambda = [...]`, holds the lambda too, also one
    // moved from, `fg_CallSafe(fg_Move(fLambda))`, and so does an unchanged
    // local reference to one, `auto &fAlias = fLambda`.
    const StringRef TypeName = recordName(Variable->getType());
    const bool Wrapper =
        (TypeName.starts_with("TCFunction") || TypeName == "function") &&
        (Variable->getType()->isReferenceType()
             ? isUnchangedWrapper(Variable)
             : isUnchangedLocal(Variable) || isUnchangedWrapper(Variable));
    if ((!Closure || !Closure->isLambda()) && !Wrapper &&
        !Variable->getType()->getContainedAutoType())
      return nullptr;
    E = Variable->getInit();
  }
  return nullptr;
}

const LambdaExpr *calledLambda(const CallExpr *Call) {
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
      Operator && Operator->getOperator() == OO_Call &&
      Operator->getNumArgs() >= 1)
    return asLambda(Operator->getArg(0));
  if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
      Member && Member->getMethodDecl() &&
      Member->getMethodDecl()->getOverloadedOperator() == OO_Call)
    return asLambda(Member->getImplicitObjectArgument());
  // Also `std::invoke(fRead)`, which calls it right away.
  if (isStdInvoke(Call) && Call->getNumArgs() >= 1)
    return asLambda(Call->getArg(0));
  return nullptr;
}

static bool isUnchangedLocal(const VarDecl *Variable);
static bool refersToOwnObjectImpl(const Expr *E, ASTContext *Context,
                                  bool Parameters, int Depth);
static bool isChangedInCapturingLambdas(const DeclRefExpr *Ref,
                                        const VarDecl *Variable,
                                        ASTContext &Context);

bool refersToOwnObject(const Expr *E, ASTContext *Context) {
  return refersToOwnObjectImpl(E, Context, /*Parameters=*/false, 0);
}

bool refersToCurrentActorObject(const Expr *E, ASTContext *Context) {
  return refersToOwnObjectImpl(E, Context, /*Parameters=*/true, 0);
}

bool returnsItsParameter(const FunctionDecl *Function) {
  if (!Function)
    return false;
  switch (Function->getBuiltinID()) {
  case Builtin::BImove:
  case Builtin::BImove_if_noexcept:
  case Builtin::BIforward:
  case Builtin::BIforward_like:
  case Builtin::BIas_const:
  case Builtin::BIaddressof:
  case Builtin::BI__addressof:
    return true;
  default:
    break;
  }
  const QualType Result = Function->getReturnType();
  if (!Result->isReferenceType() && !Result->isPointerType())
    return false;
  const FunctionDecl *Definition = nullptr;
  const auto *Body = Function->hasBody(Definition)
                         ? dyn_cast_or_null<CompoundStmt>(Definition->getBody())
                         : nullptr;
  if (!Body || Body->body_empty())
    return false;
  for (const Stmt *Statement : Body->body()) {
    if (const auto *Declarations = dyn_cast<DeclStmt>(Statement);
        Declarations && llvm::all_of(Declarations->decls(), [](const Decl *D) {
          return isa<StaticAssertDecl>(D);
        }))
      continue;
    const auto *Return = dyn_cast<ReturnStmt>(Statement);
    if (!Return || Statement != Body->body_back() || !Return->getRetValue())
      return false;
    const Expr *Value = Return->getRetValue();
    while (true) {
      Value = Value->IgnoreParenImpCasts();
      if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Value))
        Value = Cast->getSubExpr();
      else
        break;
    }
    const auto *Ref = dyn_cast<DeclRefExpr>(Value);
    return Ref && isa<ParmVarDecl>(Ref->getDecl());
  }
  return false;
}

namespace {
bool onlyConstructsOrDestroysImpl(const FunctionDecl *Function,
                                  unsigned iParameter, unsigned Depth);

/// Whether what \p Use gives, the parameter or what it is cast or given back
/// as, is only constructed, destroyed or tested for null where it goes.
bool constructsOrDestroys(const Expr *Use, const CParents &Parents,
                          ASTContext &Context, unsigned Depth) {
  const Stmt *Child = Use;
  for (const Stmt *Parent = Parents.getParent(Child); Parent;
       Child = Parent, Parent = Parents.getParent(Child)) {
    if (isa<ParenExpr, ImplicitCastExpr, ExplicitCastExpr>(Parent)) {
      // A test for null, `DMibFastCheck(_pObject)` or `if (_pObject)`.
      if (const auto *Cast = dyn_cast<CastExpr>(Parent);
          Cast && Cast->getCastKind() == CK_PointerToBoolean)
        return true;
      continue;
    }
    if (const auto *Unary = dyn_cast<UnaryOperator>(Parent)) {
      if (Unary->getOpcode() == UO_AddrOf || Unary->getOpcode() == UO_Deref)
        continue;
      return Unary->getOpcode() == UO_LNot;
    }
    if (const auto *Binary = dyn_cast<BinaryOperator>(Parent))
      return Binary->isEqualityOp() &&
             (Binary->getLHS()->IgnoreParenImpCasts()->isNullPointerConstant(
                  Context, Expr::NPC_ValueDependentIsNull) ||
              Binary->getRHS()->IgnoreParenImpCasts()->isNullPointerConstant(
                  Context, Expr::NPC_ValueDependentIsNull));
    // Destroyed: `delete _pObject`, `_pObject->~CType()`.
    if (isa<CXXDeleteExpr, CXXPseudoDestructorExpr>(Parent))
      return true;
    if (const auto *Member = dyn_cast<MemberExpr>(Parent))
      return isa<CXXDestructorDecl>(Member->getMemberDecl());
    // Given to a constructor, which constructs another object with it, or the
    // place to construct in, unless its body can be seen to do more with it,
    // `CRead(CApp *_pApp) : m_Value(_pApp->m_Value) {}`.
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(Parent)) {
      const CXXConstructorDecl *Constructor = Construct->getConstructor();
      if (!Constructor || !Constructor->hasBody())
        return true;
      for (unsigned iArgument = 0; iArgument < Construct->getNumArgs() &&
                                   iArgument < Constructor->getNumParams();
           ++iArgument)
        if (Construct->getArg(iArgument) == Child)
          return onlyConstructsOrDestroysImpl(Constructor, iArgument,
                                              Depth + 1);
      return false;
    }
    if (isa<CXXNewExpr, InitListExpr, CXXParenListInitExpr>(Parent))
      return true;
    if (const auto *Call = dyn_cast<CallExpr>(Parent)) {
      const FunctionDecl *Callee = Call->getDirectCallee();
      if (!Callee || Call->getCallee()->IgnoreParenImpCasts() == Child)
        return false;
      if (Callee->getBuiltinID() == Builtin::BI__builtin_malterlib_destroy)
        return true;
      // What gives it back: what is done with the result.
      if (returnsItsParameter(Callee))
        continue;
      const auto *Method = dyn_cast<CXXMethodDecl>(Callee);
      const unsigned Offset = isa<CXXOperatorCallExpr>(Call) && Method &&
                                      Method->isImplicitObjectMemberFunction()
                                  ? 1
                                  : 0;
      for (unsigned iArgument = Offset; iArgument < Call->getNumArgs();
           ++iArgument) {
        if (Call->getArg(iArgument) != Child)
          continue;
        const unsigned iCalleeParameter = iArgument - Offset;
        if (iCalleeParameter >= Callee->getNumParams())
          return false;
        // Its memory freed, `_Allocator.f_Free(_pObject, Size)`, by one whose
        // body can't be seen; one whose body can is looked into, which can
        // cast it back, `static_cast<CApp *>(_pObject)->m_Value`.
        const QualType Type =
            Callee->getParamDecl(iCalleeParameter)->getType();
        if (Type->isVoidPointerType() && !Callee->hasBody())
          return true;
        return onlyConstructsOrDestroysImpl(Callee, iCalleeParameter,
                                            Depth + 1);
      }
      return false;
    }
    return false;
  }
  return false;
}

bool onlyConstructsOrDestroysImpl(const FunctionDecl *Function,
                                  unsigned iParameter, unsigned Depth) {
  if (!Function || Depth > 8)
    return false;
  const FunctionDecl *Definition = nullptr;
  if (!Function->hasBody(Definition) || !Definition->getBody() ||
      Definition->isDependentContext() ||
      iParameter >= Definition->getNumParams())
    return false;
  // Memoized for each instantiated function, whose uses are its own, with
  // where on the stack of those being decided it is while it is, and for one
  // decided while one further out it used is, that one's position.
  using CKey = std::pair<const FunctionDecl *, unsigned>;
  struct CDecision {
    std::optional<bool> Result;
    unsigned Position = 0;
    unsigned DependsOn = ~0u;
  };
  struct CMemo {
    llvm::DenseMap<CKey, CDecision> Decided;
  };
  // How many are being decided, the lowest position of one being decided
  // that what is being decided now used, and for each position, what was
  // decided depending on the one there.
  static thread_local unsigned s_nDeciding = 0;
  static thread_local unsigned s_UsedDeciding = ~0u;
  static thread_local std::vector<llvm::SmallVector<CKey, 4>> s_Pending;
  ASTContext &Context = Definition->getASTContext();
  llvm::DenseMap<CKey, CDecision> &Decided = astCache<CMemo>(Context).Decided;
  const CKey Key{Definition, iParameter};
  if (auto It = Decided.find(Key); It != Decided.end()) {
    if (It->second.Result) {
      if (It->second.DependsOn != ~0u)
        s_UsedDeciding = std::min(s_UsedDeciding, It->second.DependsOn);
      return *It->second.Result;
    }
    // One that calls itself back with it does only what it does otherwise,
    // which isn't known until it is decided.
    s_UsedDeciding = std::min(s_UsedDeciding, It->second.Position);
    return true;
  }
  const unsigned Position = s_nDeciding++;
  if (s_Pending.size() <= Position)
    s_Pending.resize(Position + 1);
  Decided[Key] = {std::nullopt, Position};
  const unsigned OuterUsedDeciding = s_UsedDeciding;
  s_UsedDeciding = ~0u;

  using namespace ast_matchers;
  const ParmVarDecl *Parameter = Definition->getParamDecl(iParameter);
  const Stmt *Body = Definition->getBody();
  const CParents Parents(Body);
  bool Result = true;
  for (const BoundNodes &Nodes :
       match(findAll(declRefExpr(to(parmVarDecl(equalsNode(Parameter))))
                         .bind("ref")),
             *Body, Context))
    if (!constructsOrDestroys(Nodes.getNodeAs<DeclRefExpr>("ref"), Parents,
                              Context, Depth)) {
      Result = false;
      break;
    }
  // Of a constructor, also its initializers: one that keeps it as it is,
  // `m_pApp(_pApp)`, holds it in the object, but another use, like
  // `m_Value(_pApp->m_Value)`, has to only construct or destroy too.
  if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Definition))
    for (const CXXCtorInitializer *Initializer : Constructor->inits()) {
      const Expr *Init = Initializer->getInit();
      if (!Result || !Init || !Initializer->isWritten())
        continue;
      const Expr *Kept = Init->IgnoreImplicit();
      if (const auto *List = dyn_cast<ParenListExpr>(Kept);
          List && List->getNumExprs() == 1)
        Kept = List->getExpr(0)->IgnoreImplicit();
      // Or its address, `m_pApp(&_App)`, or it cast.
      while (true) {
        Kept = Kept->IgnoreParenImpCasts();
        if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Kept))
          Kept = Cast->getSubExpr();
        else if (const auto *Unary = dyn_cast<UnaryOperator>(Kept);
                 Unary && (Unary->getOpcode() == UO_AddrOf ||
                           Unary->getOpcode() == UO_Deref))
          Kept = Unary->getSubExpr();
        else
          break;
      }
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Kept);
          Ref && Ref->getDecl() == Parameter)
        continue;
      const CParents InitParents(Init);
      for (const BoundNodes &Nodes :
           match(findAll(declRefExpr(to(parmVarDecl(equalsNode(Parameter))))
                             .bind("ref")),
                 *Init, Context))
        if (!constructsOrDestroys(Nodes.getNodeAs<DeclRefExpr>("ref"),
                                  InitParents, Context, Depth)) {
          Result = false;
          break;
        }
    }
  --s_nDeciding;
  // A result that took one further out to do only what it does, which is
  // decided later, holds while that one does: it is kept, depending on it,
  // so that a function that many call is decided once, and dropped to be
  // decided again when that one doesn't, which it can then be. One that
  // doesn't is final, and so is one that it doesn't.
  const bool Provisional = Result && s_UsedDeciding < Position;
  {
    CDecision &Decision = Decided[Key];
    Decision.Result = Result;
    Decision.DependsOn = Provisional ? s_UsedDeciding : ~0u;
  }
  if (Provisional)
    s_Pending[s_UsedDeciding].push_back(Key);
  // What was decided depending on this one holds when it does, depending on
  // what this one depends on, if anything.
  llvm::SmallVector<CKey, 4> Pending = std::move(s_Pending[Position]);
  s_Pending[Position].clear();
  for (const CKey &Dependent : Pending) {
    auto It = Decided.find(Dependent);
    if (It == Decided.end())
      continue;
    if (!Result) {
      Decided.erase(It);
      continue;
    }
    It->second.DependsOn = Provisional ? s_UsedDeciding : ~0u;
    if (Provisional)
      s_Pending[s_UsedDeciding].push_back(Dependent);
  }
  s_UsedDeciding =
      std::min(OuterUsedDeciding, Provisional ? s_UsedDeciding : ~0u);
  return Result;
}
} // namespace

bool onlyConstructsOrDestroys(const FunctionDecl *Function,
                              unsigned iParameter) {
  return onlyConstructsOrDestroysImpl(Function, iParameter, 0);
}

bool takesActorObject(const ParmVarDecl *Parameter) {
  const auto *Function = dyn_cast<FunctionDecl>(Parameter->getDeclContext());
  if (!Function)
    return false;
  // A member function of an actor, or of a helper that derives from
  // CActorInternal or CAllowUnsafeThis, has the object as `this` or holds it,
  // and a lambda and a constructor have rules of their own. One of another
  // class is given the object of the actor it runs on, like a free function,
  // `mp_Interface.f_Publish(Manager, this)`.
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      Method && !Method->isStatic() &&
      (isa<CXXConstructorDecl, CXXDestructorDecl>(Method) ||
       Method->getParent()->isLambda() ||
       isActorClass(Method->getParent()) ||
       isKnownSafeThisClass(Method->getParent())))
    return false;
  // Also a reference to a pointer, `CApp *const &_pThis`, written so, not a
  // forwarding reference a pointer is given to, `tp_CParams &&..._Params`.
  QualType Type = Parameter->getType();
  if (Type->isReferenceType() && Type.getNonReferenceType()->isPointerType()) {
    const FunctionDecl *Pattern = Function->getTemplateInstantiationPattern();
    const unsigned Index = Parameter->getFunctionScopeIndex();
    if (Pattern && (Index >= Pattern->getNumParams() ||
                    Pattern->getParamDecl(Index)->getType()->isDependentType()))
      return false;
    Type = Type.getNonReferenceType();
  }
  return (Type->isPointerType() || Type->isReferenceType()) &&
         isActorClass(Type->getPointeeType()->getAsCXXRecordDecl());
}

bool isActorObjectParameter(const ValueDecl *Declaration) {
  const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Declaration);
  return Parameter && takesActorObject(Parameter) &&
         (Parameter->getType()->isReferenceType() ||
          !isChangedVariable(Parameter));
}

static bool refersToOwnObjectImpl(const Expr *E, ASTContext *Context,
                                  bool Parameters, int Depth) {
  // A chain of aliases, each looked at once.
  if (Depth > 32)
    return false;
  E = skipObjectCasts(E);
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary && (Unary->getOpcode() == UO_AddrOf ||
                Unary->getOpcode() == UO_Deref))
    E = skipObjectCasts(Unary->getSubExpr());
  if (isa<CXXThisExpr>(E))
    return true;
  // Forwarded or moved on, `fg_Forward<tf_CActor>(_pThis)`, it is what it is
  // given.
  if (const CallExpr *Forward =
          asCallTo(E, vocabulary().MoveOrForwardFunctions.names());
      Forward && Forward->getNumArgs() == 1)
    return refersToOwnObjectImpl(Forward->getArg(0), Context, Parameters,
                                 Depth + 1);
  // An unchanged local alias, like `auto *pThis = this`, whose copy no lambda
  // the use is in changes.
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
    auto Unchanged = [&] {
      return !(Context && isChangedInCapturingLambdas(Ref, Variable, *Context));
    };
    // A parameter given the object of the current actor, `_pThis`.
    if (Parameters && isActorObjectParameter(Variable))
      return Unchanged();
    return Variable && Variable->getInit() && isUnchangedLocal(Variable) &&
           Unchanged() &&
           refersToOwnObjectImpl(Variable->getInit(), Context, Parameters,
                                 Depth + 1);
  }
  if (const auto *Member = dyn_cast<MemberExpr>(E)) {
    const ValueDecl *Decl = Member->getMemberDecl();
    return isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()) &&
           Decl->getIdentifier() && Decl->getName().ends_with("This") &&
           (Decl->getType()->isReferenceType() ||
            Decl->getType()->isPointerType());
  }
  return false;
}

/// Whether the parameter that argument \p iArgument of \p Call binds to is a
/// non-const reference, of the callee or of the function type it is called
/// through.
/// Whether \p Function, which has a body, assigns what its reference
/// parameter \p iParam refers to, or gives it on to what may.
static bool assignsThroughParameter(const FunctionDecl *Function,
                                    unsigned iParam);

bool mayAssignArgument(const CallExpr *Call, unsigned iArgument) {
  auto Assigns = [&](const FunctionDecl *Function) {
    // A member operator, like a functor's `operator()`, gets its object as
    // the first argument.
    unsigned iParam = iArgument;
    if (isa<CXXOperatorCallExpr>(Call) && hasImplicitObject(Function)) {
      if (iArgument == 0)
        return false;
      iParam = iArgument - 1;
    }
    if (!Function || iParam >= Function->getNumParams())
      return false;
    const QualType Type = Function->getParamDecl(iParam)->getType();
    if (!Type->isReferenceType() ||
        Type.getNonReferenceType().isConstQualified())
      return false;
    // A virtual one called through the object, `_pReplacer->f_Replace(Actor)`,
    // runs an override, whose code isn't known, unless none can override it.
    if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
        Method && Method->isVirtual() && !Method->hasAttr<FinalAttr>() &&
        !Method->getParent()->hasAttr<FinalAttr>()) {
      const auto *Member = dyn_cast<MemberExpr>(
          Call->getCallee()->IgnoreParenImpCasts());
      if (!Member || !Member->hasQualifier())
        return true;
    }
    // One whose code is known, also as instantiated, like a function that
    // takes a forwarding reference and only forwards it, changes it only if
    // its code does.
    const FunctionDecl *Definition = nullptr;
    if (Function->hasBody(Definition) && Definition->getBody() &&
        !Definition->isDependentContext())
      return assignsThroughParameter(Definition, iParam);
    return true;
  };
  if (const FunctionDecl *Callee = Call->getDirectCallee())
    return Assigns(Callee);
  // A call through a pointer to a function, like a callback.
  QualType CalleeType = Call->getCallee()->getType();
  if (const auto *Pointer = CalleeType->getAs<PointerType>())
    CalleeType = Pointer->getPointeeType();
  else if (const auto *Reference = CalleeType->getAs<ReferenceType>())
    CalleeType = Reference->getPointeeType();
  if (const auto *Prototype = CalleeType->getAs<FunctionProtoType>();
      Prototype && iArgument < Prototype->getNumParams()) {
    const QualType Type = Prototype->getParamType(iArgument);
    return Type->isReferenceType() &&
           !Type.getNonReferenceType().isConstQualified();
  }
  return false;
}

namespace {

/// Finds whether a variable is given another value: assigned, moved, swapped
/// or exchanged from, or passed by non-const reference, which lets the callee
/// assign it. Calls through a handle don't change which actor it refers to.
class ReassignmentFinder : public OncePerTypeVisitor<ReassignmentFinder> {
public:
  explicit ReassignmentFinder(const VarDecl *Variable,
                              bool IncludeCopies = false,
                              const DeclRefExpr *Ignored = nullptr)
      : Variable(Variable), IncludeCopies(IncludeCopies), Ignored(Ignored) {}

  bool Found = false;

  // What doesn't run doesn't change it: an operand that isn't evaluated, like
  // `sizeof(Value = Other)`, the branch an `if constexpr` discards, or the one
  // a constant condition never takes, `if (false)`, unless a jump can reach
  // it.
  bool TraverseUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *) {
    return true;
  }
  bool TraverseCXXNoexceptExpr(CXXNoexceptExpr *) { return true; }
  bool TraverseCXXTypeidExpr(CXXTypeidExpr *Typeid) {
    return !Typeid->isPotentiallyEvaluated() ||
           RecursiveASTVisitor::TraverseCXXTypeidExpr(Typeid);
  }
  bool TraverseIfStmt(IfStmt *If) {
    for (const Stmt *Child : evaluatedChildren(If, Variable->getASTContext()))
      if (!TraverseStmt(const_cast<Stmt *>(Child)))
        return false;
    return true;
  }
  // Nor the body of a loop whose condition is false, `while (false)`, unless
  // a jump can reach it.
  bool neverRuns(const Expr *Condition, const Stmt *Body) {
    bool Value = true;
    return Condition && !Condition->isValueDependent() &&
           Condition->EvaluateAsBooleanCondition(Value,
                                                 Variable->getASTContext()) &&
           !Value && !hasJumpTarget(Body);
  }
  bool TraverseWhileStmt(WhileStmt *While) {
    if (!While->getConditionVariable() &&
        neverRuns(While->getCond(), While->getBody()))
      return TraverseStmt(While->getCond());
    return RecursiveASTVisitor::TraverseWhileStmt(While);
  }
  bool TraverseForStmt(ForStmt *For) {
    if (!For->getConditionVariable() &&
        neverRuns(For->getCond(), For->getBody()))
      return TraverseStmt(For->getInit()) && TraverseStmt(For->getCond());
    return RecursiveASTVisitor::TraverseForStmt(For);
  }
  // Nor what follows a return in a block, unless a jump can reach it.
  bool TraverseCompoundStmt(CompoundStmt *Block) {
    for (auto It = Block->body_begin(); It != Block->body_end(); ++It) {
      if (!TraverseStmt(*It))
        return false;
      if (isa<ReturnStmt, CoreturnStmt>(*It) &&
          llvm::none_of(llvm::make_range(std::next(It), Block->body_end()),
                        [](const Stmt *Rest) { return hasJumpTarget(Rest); }))
        break;
    }
    return true;
  }

  // A lambda that captures the variable by copy changes its own copy, unless
  // the copies are included.
  bool TraverseLambdaExpr(LambdaExpr *Lambda) {
    if (IncludeCopies)
      return RecursiveASTVisitor::TraverseLambdaExpr(Lambda);
    for (const LambdaCapture &Capture : Lambda->captures()) {
      if (Capture.capturesVariable() && Capture.getCapturedVar() == Variable &&
          Capture.getCaptureKind() == LCK_ByCopy) {
        for (Expr *Init : Lambda->capture_inits()) {
          if (Init && !TraverseStmt(Init))
            return false;
        }
        return true;
      }
    }
    return RecursiveASTVisitor::TraverseLambdaExpr(Lambda);
  }

  bool VisitBinaryOperator(BinaryOperator *Binary) {
    if (Binary->isAssignmentOp() && refersToVariable(Binary->getLHS()))
      Found = true;
    return !Found;
  }
  // An output of inline assembly, `asm("" : "=r"(pValue))`, writes it.
  bool VisitAsmStmt(AsmStmt *Asm) {
    for (unsigned iOutput = 0; iOutput < Asm->getNumOutputs(); ++iOutput)
      if (refersToVariable(Asm->getOutputExpr(iOutput)))
        Found = true;
    return !Found;
  }
  // Its address, unless to const, lets what it is passed to change it, like
  // `fg_Replace(&Variable)`.
  bool VisitUnaryOperator(UnaryOperator *Unary) {
    if (Unary->getOpcode() == UO_AddrOf &&
        refersToVariable(Unary->getSubExpr()) &&
        !Unary->getType()->getPointeeType().isConstQualified())
      Found = true;
    // `++pValue` points it elsewhere.
    if (Unary->isIncrementDecrementOp() && refersToVariable(Unary->getSubExpr()))
      Found = true;
    return !Found;
  }
  // Casting const away from what refers or points to it, `const_cast<T
  // &>(Alias)` of `auto const &Alias = Variable`, or `*const_cast<T
  // *>(pAlias)` of `auto const *pAlias = &Variable`, can change it.
  bool VisitCXXConstCastExpr(CXXConstCastExpr *Cast) {
    return visitConstRemoval(Cast);
  }
  // So can a C-style cast to a reference or pointer that isn't const,
  // `(TCActor<> &)Alias`.
  bool VisitCStyleCastExpr(CStyleCastExpr *Cast) {
    const QualType Type = Cast->getType();
    const bool Writable =
        (Cast->isGLValue() && !Type.isConstQualified()) ||
        (Type->isPointerType() && !Type->getPointeeType().isConstQualified());
    return !Writable || visitConstRemoval(Cast);
  }
  // Nor what a requirement only names, `requires { Value = Other; }`.
  bool TraverseRequiresExpr(RequiresExpr *) { return true; }
  bool visitConstRemoval(ExplicitCastExpr *Cast) {
    // Through any number of aliases, past as many as code can have, it may.
    std::function<bool(const Expr *, int)> LeadsTo = [&](const Expr *E,
                                                         int Depth) {
      if (!E)
        return false;
      if (Depth > 1024)
        return true;
      E = E->IgnoreParenImpCasts();
      if (const auto *Unary = dyn_cast<UnaryOperator>(E);
          Unary &&
          (Unary->getOpcode() == UO_AddrOf || Unary->getOpcode() == UO_Deref))
        return LeadsTo(Unary->getSubExpr(), Depth + 1);
      const auto *Ref = dyn_cast<DeclRefExpr>(E);
      if (!Ref)
        return false;
      if (Ref->getDecl() == Variable)
        return true;
      const auto *Alias = dyn_cast<VarDecl>(Ref->getDecl());
      return Alias && Alias->hasLocalStorage() && Alias->getInit() &&
             (Alias->getType()->isReferenceType() ||
              Alias->getType()->isPointerType()) &&
             LeadsTo(Alias->getInit(), Depth + 1);
    };
    if (LeadsTo(Cast->getSubExpr(), 0))
      Found = true;
    return !Found;
  }
  // A non-const reference to the variable can change it, like `Alias =
  // Other` with `auto &Alias = Variable`.
  bool VisitVarDecl(VarDecl *Alias) {
    if (Alias->getType()->isReferenceType() &&
        !Alias->getType().getNonReferenceType().isConstQualified() &&
        Alias->getInit() && refersToVariable(Alias->getInit()))
      Found = true;
    return !Found;
  }
  // Also `++Iterator`, which moves an iterator to another entry.
  bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *Call) {
    if ((Call->isAssignmentOp() || Call->getOperator() == OO_PlusPlus ||
         Call->getOperator() == OO_MinusMinus) &&
        Call->getNumArgs() >= 1 && refersToVariable(Call->getArg(0)))
      Found = true;
    return !Found;
  }
  bool VisitCallExpr(CallExpr *Call) {
    // Assignments are found by themselves.
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
        Operator && Operator->isAssignmentOp())
      return !Found;
    const StringRef Name = calleeName(Call);
    const bool Changes = isSwapFunction(Name) || isExchangeFunction(Name);
    // Moving an object leaves it moved from, but not a pointer or a number,
    // which keeps its value, `fg_Move(pThis)`.
    const bool Moves = isMoveFunction(Name);
    // A member function of it that replaces what it holds, like
    // `pValue.swap(pOther)` or `pValue.f_Clear()` of a pointer object, also
    // one that gives it away, `pValue.f_Detach()`.
    if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call)) {
      const auto *Callee =
          dyn_cast<MemberExpr>(Member->getCallee()->IgnoreParens());
      const CXXMethodDecl *Method = Member->getMethodDecl();
      if (Callee && !Callee->isArrow() && Method && !Method->isConst() &&
          refersToVariable(Member->getImplicitObjectArgument()) &&
          // Also an assignment named, `pValue.operator=(pOther)`.
          (Method->isCopyAssignmentOperator() ||
           Method->isMoveAssignmentOperator() ||
           Method->getOverloadedOperator() == OO_Equal ||
           Method->getOverloadedOperator() == OO_PlusPlus ||
           Method->getOverloadedOperator() == OO_MinusMinus ||
           (Method->getIdentifier() &&
            llvm::is_contained({StringRef("swap"), StringRef("f_Swap"),
                                StringRef("reset"), StringRef("f_Reset"),
                                StringRef("f_Clear"), StringRef("release"),
                                StringRef("f_Detach"), StringRef("exchange"),
                                StringRef("f_Exchange"), StringRef("f_Set"),
                                StringRef("f_Next"), StringRef("f_Prev")},
                               Method->getName()))))
        Found = true;
    }
    for (unsigned iArgument = 0; iArgument < Call->getNumArgs(); ++iArgument) {
      if (refersToVariable(Call->getArg(iArgument)) &&
          (Changes ||
           (Moves && !Call->getArg(iArgument)->getType()->isScalarType()) ||
           (!Moves && mayAssignArgument(Call, iArgument))))
        Found = true;
    }
    return !Found;
  }

private:
  // Also either value of a conditional, `(bChoose ? pSelf : pSpare) =
  // Other`, or the last of a comma.
  bool refersToVariable(const Expr *E) const {
    E = E->IgnoreParenImpCasts();
    // Also cast to a reference, `static_cast<CType &>(Value) = Other`.
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
        Cast && Cast->getTypeAsWritten()->isReferenceType())
      return refersToVariable(Cast->getSubExpr());
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E))
      return Conditional->isGLValue() &&
             (refersToVariable(Conditional->getTrueExpr()) ||
              refersToVariable(Conditional->getFalseExpr()));
    if (const auto *Comma = dyn_cast<BinaryOperator>(E);
        Comma && Comma->getOpcode() == BO_Comma)
      return Comma->isGLValue() && refersToVariable(Comma->getRHS());
    const auto *Ref = dyn_cast<DeclRefExpr>(E);
    return Ref && Ref != Ignored && Ref->getDecl() == Variable;
  }

  const VarDecl *Variable;
  bool IncludeCopies;
  /// A use that doesn't count, like the move that gives the value away.
  const DeclRefExpr *Ignored;
};

} // namespace

static bool assignsThroughParameter(const FunctionDecl *Function,
                                    unsigned iParam) {
  // Memoized for each function as instantiated; a function that gives it
  // back to itself, or code too deep to follow, counts as assigning it.
  struct CMemo {
    llvm::DenseMap<std::pair<const FunctionDecl *, unsigned>,
                   std::optional<bool>>
        Decided;
  };
  static thread_local unsigned s_Depth = 0;
  auto [It, Inserted] = astCache<CMemo>(Function->getASTContext())
                            .Decided.try_emplace({Function, iParam});
  if (!Inserted)
    return It->second.value_or(true);
  if (s_Depth > 16 || iParam >= Function->getNumParams())
    return true;
  ++s_Depth;
  const llvm::scope_exit Restore([] { --s_Depth; });
  ReassignmentFinder Finder(Function->getParamDecl(iParam),
                            /*IncludeCopies=*/true);
  Finder.TraverseStmt(Function->getBody());
  astCache<CMemo>(Function->getASTContext()).Decided[{Function, iParam}] =
      Finder.Found;
  return Finder.Found;
}

bool isChangedVariable(const VarDecl *Variable) {
  return !isUnchangedLocal(Variable);
}

bool isChangedOtherThanBy(const DeclRefExpr *Use) {
  const auto *Variable = dyn_cast<VarDecl>(Use->getDecl());
  const auto *Function = Variable ? dyn_cast_or_null<FunctionDecl>(
                                        Variable->getParentFunctionOrMethod())
                                  : nullptr;
  if (!Function || !Function->getBody() || !Variable->hasLocalStorage())
    return true;
  ReassignmentFinder Finder(Variable, /*IncludeCopies=*/false, Use);
  Finder.TraverseStmt(Function->getBody());
  return Finder.Found;
}

/// Whether \p S has a statement that a jump from outside it can reach: a
/// label, or a case of a switch around it, not of a switch in it nor in a
/// lambda.
bool isNeverRun(const Stmt *S, ASTContext &Context) {
  return isInUnevaluatedOperand(DynTypedNode::create(*S), Context) ||
         isInDiscardedBranch(DynTypedNode::create(*S), Context) ||
         isInNeverTakenBranch(DynTypedNode::create(*S), Context);
}

bool isInNeverTakenBranch(const DynTypedNode &Node, ASTContext &Context) {
  auto Constant = [&](const Expr *Condition, bool &Value) {
    return Condition && !Condition->isValueDependent() &&
           Condition->EvaluateAsBooleanCondition(Value, Context);
  };
  DynTypedNode Child = Node;
  for (int Depth = 0; Depth < 1024; ++Depth) {
    const auto Parents = instantiatedParents(Child, Context);
    if (Parents.empty())
      return false;
    const DynTypedNode Parent = Parents[0];
    const Stmt *From = Child.get<Stmt>();
    bool Value = false;
    if (Parent.get<FunctionDecl>() || Parent.get<LambdaExpr>() ||
        Parent.get<BlockExpr>() || Parent.get<TranslationUnitDecl>())
      return false;
    if (const auto *If = Parent.get<IfStmt>();
        If && From && !If->isConstexpr() && !If->isConsteval() &&
        !If->getConditionVariable() && Constant(If->getCond(), Value)) {
      const Stmt *Skipped = Value ? If->getElse() : If->getThen();
      if (From == Skipped && !hasJumpTarget(Skipped))
        return true;
    }
    if (const auto *While = Parent.get<WhileStmt>();
        While && From && From == While->getBody() &&
        !While->getConditionVariable() && Constant(While->getCond(), Value) &&
        !Value && !hasJumpTarget(From))
      return true;
    if (const auto *For = Parent.get<ForStmt>();
        For && From && (From == For->getBody() || From == For->getInc()) &&
        !For->getConditionVariable() && Constant(For->getCond(), Value) &&
        !Value && !hasJumpTarget(For->getBody()))
      return true;
    if (const auto *Conditional = Parent.get<ConditionalOperator>();
        Conditional && From && Constant(Conditional->getCond(), Value) &&
        From ==
            (Value ? Conditional->getFalseExpr() : Conditional->getTrueExpr()))
      return true;
    if (const auto *Logical = Parent.get<BinaryOperator>();
        Logical && Logical->isLogicalOp() && From == Logical->getRHS() &&
        Constant(Logical->getLHS(), Value) &&
        Value == (Logical->getOpcode() == BO_LOr))
      return true;
    Child = Parent;
  }
  return false;
}

bool alwaysLeaves(const Stmt *S, const ASTContext &Context) {
  if (!S)
    return false;
  if (isa<ReturnStmt, CoreturnStmt, BreakStmt, ContinueStmt, GotoStmt>(S))
    return true;
  if (const auto *Compound = dyn_cast<CompoundStmt>(S))
    return llvm::any_of(Compound->body(),
                        [&](const Stmt *Statement) {
                          return alwaysLeaves(Statement, Context);
                        }) &&
           !hasJumpTarget(Compound);
  if (const auto *If = dyn_cast<IfStmt>(S)) {
    if (If->isConsteval())
      return alwaysLeaves(
          If->isNegatedConsteval() ? If->getThen() : If->getElse(), Context);
    if (If->isConstexpr() && !If->getCond()->isValueDependent())
      if (const std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Context))
        return alwaysLeaves(*Case, Context);
    bool Taken = false;
    if (!If->getConditionVariable() && !If->getCond()->isValueDependent() &&
        If->getCond()->EvaluateAsBooleanCondition(Taken, Context))
      return !hasJumpTarget(Taken ? If->getElse() : If->getThen()) &&
             alwaysLeaves(Taken ? If->getThen() : If->getElse(), Context);
    return alwaysLeaves(If->getThen(), Context) &&
           alwaysLeaves(If->getElse(), Context);
  }
  return false;
}

bool hasJumpTarget(const Stmt *S, bool InSwitch) {
  if (!S || isa<LambdaExpr>(S))
    return false;
  if (isa<LabelStmt>(S) || (isa<SwitchCase>(S) && !InSwitch))
    return true;
  const bool Nested = InSwitch || isa<SwitchStmt>(S);
  return llvm::any_of(childrenOnce(S), [&](const Stmt *Child) {
    return hasJumpTarget(Child, Nested);
  });
}

const Expr *assignedOnEveryPathBefore(const Stmt *Use,
                                      const VarDecl *Variable,
                                      ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Use);
  for (int Depth = 0; Depth < 64; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    const DynTypedNode &Parent = Parents[0];
    if (Parent.get<LambdaExpr>() || Parent.get<FunctionDecl>())
      return nullptr;
    if (const auto *Block = Parent.get<CompoundStmt>()) {
      const SourceManager &SM = Context.getSourceManager();
      const SourceLocation UseLoc = SM.getExpansionLoc(Use->getBeginLoc());
      // The last one of the innermost block, which comes after those of the
      // blocks around it.
      const Expr *Last = nullptr;
      bool Before = false;
      for (const Stmt *Statement : Block->body()) {
        // A jump to a label after the assignment passes it.
        if (hasJumpTarget(Statement))
          Last = nullptr;
        // The statement the use is in.
        if (!SM.isBeforeInTranslationUnit(
                SM.getExpansionLoc(Statement->getEndLoc()), UseLoc)) {
          Before = true;
          break;
        }
        const auto *Value = dyn_cast<Expr>(Statement);
        const auto *Assignment =
            Value ? dyn_cast<BinaryOperator>(
                        Value->IgnoreImplicit()->IgnoreParens())
                  : nullptr;
        const auto *Target =
            Assignment && Assignment->getOpcode() == BO_Assign
                ? dyn_cast<DeclRefExpr>(
                      Assignment->getLHS()->IgnoreParenImpCasts())
                : nullptr;
        if (Target && Target->getDecl() == Variable)
          Last = Assignment->getRHS();
        // Also by an assignment operator of a class, `iEntry = iOther`.
        const auto *Operator =
            Value ? dyn_cast<CXXOperatorCallExpr>(
                        Value->IgnoreImplicit()->IgnoreParens())
                  : nullptr;
        bool AssignedHere = Target && Target->getDecl() == Variable;
        if (Operator && Operator->getOperator() == OO_Equal &&
            Operator->getNumArgs() == 2)
          if (const auto *Assigned = dyn_cast<DeclRefExpr>(
                  Operator->getArg(0)->IgnoreParenImpCasts());
              Assigned && Assigned->getDecl() == Variable) {
            Last = Operator->getArg(1);
            AssignedHere = true;
          }
        // One changed on some paths only after it, `if (bChoose) pItems =
        // &mp_Items;`, may hold either.
        if (!AssignedHere && isChangedIn(Variable, Statement))
          Last = nullptr;
      }
      if (Last && Before)
        return Last;
    }
    Node = Parent;
  }
  return nullptr;
}

bool runsOnEveryPathBefore(const Stmt *Earlier, const Stmt *Later,
                           ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  // A jump can pass the earlier statement, `goto Later;`, in a function
  // that has one.
  DynTypedNode Up = DynTypedNode::create(*Earlier);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Up, Context);
    if (Parents.empty())
      break;
    Up = Parents[0];
    const Stmt *Body = nullptr;
    if (const auto *Function = Up.get<FunctionDecl>())
      Body = Function->getBody();
    else if (const auto *Lambda = Up.get<LambdaExpr>())
      Body = Lambda->getBody();
    if (!Body && !Up.get<FunctionDecl>())
      continue;
    // Each body looked into once, however many statements of it are asked
    // about.
    struct CJumps {
      llvm::DenseMap<const Stmt *, bool> Bodies;
    };
    auto [It, Inserted] =
        astCache<CJumps>(Context).Bodies.try_emplace(Body, false);
    if (Inserted) {
      bool Jumps = false;
      std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
        if (!S || Jumps || isa<LambdaExpr>(S))
          return;
        Jumps = isa<GotoStmt, IndirectGotoStmt>(S);
        for (const Stmt *Child : childrenOnce(S))
          Find(Child);
      };
      Find(Body);
      It->second = Jumps;
    }
    if (It->second)
      return false;
    break;
  }
  DynTypedNode Node = DynTypedNode::create(*Earlier);
  for (int Depth = 0; Depth < 64; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    const DynTypedNode &Parent = Parents[0];
    if (const auto *Block = Parent.get<CompoundStmt>()) {
      // A statement of a block that has the later one after it.
      const Stmt *Statement = Node.get<Stmt>();
      if (!Statement ||
          !SM.isBeforeInTranslationUnit(Statement->getEndLoc(),
                                        Later->getBeginLoc()) ||
          SM.isBeforeInTranslationUnit(Later->getBeginLoc(),
                                       Block->getBeginLoc()) ||
          !SM.isBeforeInTranslationUnit(Later->getBeginLoc(),
                                        Block->getEndLoc()))
        return false;
      // Without a label of a switch around the block between them, which
      // enters after the earlier one, `case 1: p = &Late;` (the labels of a
      // switch in the block enter only from it).
      bool Entered = false;
      std::function<void(const Stmt *)> FindCase = [&](const Stmt *S) {
        if (!S || Entered || isa<LambdaExpr, SwitchStmt>(S))
          return;
        if (isa<SwitchCase>(S) &&
            SM.isBeforeInTranslationUnit(Statement->getEndLoc(),
                                         S->getBeginLoc()) &&
            !SM.isBeforeInTranslationUnit(Later->getBeginLoc(),
                                          S->getBeginLoc()))
          Entered = true;
        for (const Stmt *Child : childrenOnce(S))
          FindCase(Child);
      };
      FindCase(Block);
      return !Entered;
    }
    // Only through expressions that run all of themselves, not a branch, a
    // loop or a lambda, nor a side of `?:`, `&&` or `||`.
    const auto *Outer = Parent.get<Expr>();
    if (!Outer || isa<LambdaExpr, AbstractConditionalOperator>(Outer))
      return false;
    if (const auto *Logical = dyn_cast<BinaryOperator>(Outer);
        Logical && Logical->isLogicalOp() &&
        Logical->getLHS() != Node.get<Expr>())
      return false;
    Node = Parent;
  }
  return false;
}

bool isChangedIn(const VarDecl *Variable, const Stmt *S) {
  if (!S)
    return false;
  ReassignmentFinder Finder(Variable);
  Finder.TraverseStmt(const_cast<Stmt *>(S));
  return Finder.Found;
}


/// Whether \p Variable is a local or init capture that keeps its initial
/// value.
static bool isUnchangedLocal(const VarDecl *Variable) {
  if (!(Variable->hasLocalStorage() || Variable->isInitCapture()))
    return false;
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return false;
  CASTFacts &Facts = astFacts(Variable->getASTContext());
  if (auto It = Facts.Unchanged.find(Variable); It != Facts.Unchanged.end())
    return It->second;
  // Only one that the function names other than to read it, `int *p1 = p0`,
  // can change, which is found for all of them at once, so that a chain of
  // copies isn't gone through again for each.
  auto [Named, Inserted] = Facts.NamedOtherThanRead.try_emplace(Function);
  if (Inserted) {
    class CNamedFinder : public OncePerTypeVisitor<CNamedFinder> {
    public:
      bool VisitImplicitCastExpr(ImplicitCastExpr *Cast) {
        if (Cast->getCastKind() == CK_LValueToRValue)
          if (const auto *Ref =
                  dyn_cast<DeclRefExpr>(Cast->getSubExpr()->IgnoreParens()))
            Read.insert(Ref);
        return true;
      }
      bool VisitDeclRefExpr(DeclRefExpr *Ref) {
        Refs.push_back(Ref);
        return true;
      }
      llvm::DenseSet<const DeclRefExpr *> Read;
      llvm::SmallVector<const DeclRefExpr *, 32> Refs;
    };
    CNamedFinder Finder;
    Finder.TraverseStmt(Function->getBody());
    // Also what a constructor initializes members with.
    if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Function))
      for (const CXXCtorInitializer *Initializer : Constructor->inits())
        Finder.TraverseStmt(Initializer->getInit());
    for (const DeclRefExpr *Ref : Finder.Refs)
      if (!Finder.Read.contains(Ref))
        Named->second.insert(Ref->getDecl());
  }
  if (!Named->second.contains(Variable)) {
    Facts.Unchanged.try_emplace(Variable, true);
    return true;
  }
  ReassignmentFinder Finder(Variable);
  Finder.TraverseStmt(Function->getBody());
  Facts.Unchanged.try_emplace(Variable, !Finder.Found);
  return !Finder.Found;
}

/// Whether the local \p Ref refers to keeps its initial value where \p Ref
/// uses it: in a lambda, the copy it captured must not be changed either.
static bool isUnchangedLocalAt(const DeclRefExpr *Ref,
                               const VarDecl *Variable);

bool isCapturedByCopyAt(const DeclRefExpr *Ref) {
  const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
  if (!Variable || !Ref->refersToEnclosingVariableOrCapture())
    return false;
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return false;
  // The innermost lambda around the use that captures it decides.
  const LambdaExpr *Innermost = nullptr;
  bool ByCopy = false;
  class CFinder : public OncePerTypeVisitor<CFinder> {
  public:
    CFinder(const DeclRefExpr *Ref, const VarDecl *Variable,
            const LambdaExpr *&Innermost, bool &ByCopy)
        : Ref(Ref), Variable(Variable), Innermost(Innermost), ByCopy(ByCopy) {}
    bool shouldVisitLambdaBody() const { return true; }
    bool VisitLambdaExpr(LambdaExpr *Lambda) {
      if (!Lambda->getBody() ||
          !Lambda->getBody()->getSourceRange().fullyContains(
              Ref->getSourceRange()))
        return true;
      for (const LambdaCapture &Capture : Lambda->captures())
        if (Capture.capturesVariable() && Capture.getCapturedVar() == Variable) {
          // Visited from the outside in, so a later one is inside.
          Innermost = Lambda;
          ByCopy = Capture.getCaptureKind() == LCK_ByCopy;
        }
      return true;
    }

  private:
    const DeclRefExpr *Ref;
    const VarDecl *Variable;
    const LambdaExpr *&Innermost;
    bool &ByCopy;
  } Finder(Ref, Variable, Innermost, ByCopy);
  Finder.TraverseStmt(Function->getBody());
  return Innermost && ByCopy;
}

bool isUnchangedAt(const DeclRefExpr *Ref) {
  const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
  return Variable && isUnchangedLocalAt(Ref, Variable);
}

static bool isUnchangedLocalAt(const DeclRefExpr *Ref,
                               const VarDecl *Variable) {
  if (!isUnchangedLocal(Variable))
    return false;
  if (!Ref->refersToEnclosingVariableOrCapture())
    return true;
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  ReassignmentFinder Finder(Variable, /*IncludeCopies=*/true);
  Finder.TraverseStmt(Function->getBody());
  return !Finder.Found;
}

/// What refersToThis found of each expression at each depth, which aliases
/// that each are a conditional of the one before, `bChoose ? pPrevious :
/// pPrevious`, would reach again through each path to it.
using RefersToThisMemo = llvm::DenseMap<std::pair<const Expr *, int>, bool>;

static bool refersToThis(const Expr *E, int Depth, RefersToThisMemo &Memo);

static bool refersToThisUncached(const Expr *E, int Depth,
                                 RefersToThisMemo &Memo) {
  // A chain of aliases, each looked at once.
  if (Depth > 32)
    return false;
  E = skipObjectCasts(E);
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary && (Unary->getOpcode() == UO_AddrOf ||
                Unary->getOpcode() == UO_Deref))
    E = skipObjectCasts(Unary->getSubExpr());
  if (isa<CXXThisExpr>(E))
    return true;
  // Moved or forwarded, `fg_Move(pThis)`, it is what is moved.
  if (const auto *Move = dyn_cast<CallExpr>(E);
      Move && Move->getNumArgs() == 1 &&
      isMoveOrForwardFunction(calleeName(Move)))
    return refersToThis(Move->getArg(0), Depth + 1, Memo);
  // Both values of a conditional, `bChoose ? this : this`, or one when the
  // other is null, `bChoose ? this : nullptr`, which points nowhere.
  if (const auto *Conditional = dyn_cast<ConditionalOperator>(E)) {
    auto IsNull = [](const Expr *Value) {
      return isa<CXXNullPtrLiteralExpr, GNUNullExpr>(
                 Value->IgnoreParenImpCasts()) ||
             (isa<IntegerLiteral>(Value->IgnoreParenImpCasts()) &&
              cast<IntegerLiteral>(Value->IgnoreParenImpCasts())
                  ->getValue()
                  .isZero());
    };
    const Expr *True = Conditional->getTrueExpr();
    const Expr *False = Conditional->getFalseExpr();
    if (IsNull(True))
      return refersToThis(False, Depth + 1, Memo);
    if (IsNull(False))
      return refersToThis(True, Depth + 1, Memo);
    return refersToThis(True, Depth + 1, Memo) &&
           refersToThis(False, Depth + 1, Memo);
  }
  const auto *Ref = dyn_cast<DeclRefExpr>(E);
  const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  return Variable && Variable->getInit() &&
         (Variable->getType()->isPointerType() ||
          Variable->getType()->isReferenceType()) &&
         isUnchangedLocalAt(Ref, Variable) &&
         refersToThis(Variable->getInit(), Depth + 1, Memo);
}

/// Whether \p E is `this`, `*this` or an unchanged local alias of them.
static bool refersToThis(const Expr *E, int Depth, RefersToThisMemo &Memo) {
  if (auto It = Memo.find({E, Depth}); It != Memo.end())
    return It->second;
  const bool Result = refersToThisUncached(E, Depth, Memo);
  Memo[{E, Depth}] = Result;
  return Result;
}

static bool refersToThis(const Expr *E, int Depth) {
  RefersToThisMemo Memo;
  return refersToThis(E, Depth, Memo);
}

bool isCapturedAliasOfThis(const VarDecl *Variable, bool OwnObject) {
  if (!Variable->getInit() ||
      !(Variable->getType()->isPointerType() ||
        Variable->getType()->isReferenceType()))
    return false;
  // A capture copies the value the local has when the lambda is created.
  if (!Variable->isInitCapture() && !isUnchangedLocal(Variable))
    return false;
  if (OwnObject)
    return refersToOwnObject(Variable->getInit());
  // Also a pointer or reference into it, `[pValue = &m_Value]`, which is of
  // the object, through its members and the elements of its arrays.
  // Not a copy of the value of a pointer member, `[pData = m_pData]`, which
  // points where that does, not into the object.
  auto IsOfThis = [](const Expr *Init) {
    const Expr *Into = Init->IgnoreParenImpCasts();
    if (const auto *Address = dyn_cast<UnaryOperator>(Into);
        Address && Address->getOpcode() == UO_AddrOf)
      Into = Address->getSubExpr()->IgnoreParenImpCasts();
    else if (const auto *Member = dyn_cast<MemberExpr>(Into);
             Member && Member->getType()->isPointerType())
      return refersToThis(Init, 0);
    for (int Depth = 0; Depth < 64; ++Depth) {
      if (const auto *Member = dyn_cast<MemberExpr>(Into);
          Member && isa<FieldDecl>(Member->getMemberDecl())) {
        if (Member->isArrow())
          return refersToThis(Member->getBase(), 0);
        Into = Member->getBase()->IgnoreParenImpCasts();
      } else if (const auto *Pointer = dyn_cast<BinaryOperator>(Into);
                 Pointer && Pointer->isPtrMemOp()) {
        // Also through a pointer to a member, `&(this->*pMember)`.
        if (Pointer->getOpcode() == BO_PtrMemI)
          return refersToThis(Pointer->getLHS(), 0);
        Into = Pointer->getLHS()->IgnoreParenImpCasts();
      } else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Into);
                 Subscript && Subscript->getBase()
                                  ->IgnoreParenImpCasts()
                                  ->getType()
                                  ->isConstantArrayType())
        Into = Subscript->getBase()->IgnoreParenImpCasts();
      else
        break;
    }
    return refersToThis(Init, 0) ||
           (Into != Init->IgnoreParenImpCasts() && refersToThis(Into, 0));
  };
  // Either value of a conditional, `[pState = bUse ? this : nullptr]`, or
  // the one a constant condition chooses, and the last of a comma.
  const ASTContext &Context = Variable->getASTContext();
  // Each getter and local once, at the least depth it is met at, of which
  // what is below it is looked at the furthest: getters that each return the
  // one before twice would be looked at again through each path to them. One
  // looked at gives none of it, or the look would have ended.
  llvm::DenseMap<const Decl *, int> Looked;
  auto FirstLook = [&](const Decl *D, int Depth) {
    auto [It, Inserted] = Looked.try_emplace(D, Depth);
    if (Inserted)
      return true;
    if (It->second <= Depth)
      return false;
    It->second = Depth;
    return true;
  };
  std::function<bool(const Expr *, int)> AnyOfThis = [&](const Expr *Init,
                                                         int Depth) {
    if (Depth > 32)
      return false;
    const Expr *Inner = Init->IgnoreParenImpCasts();
    if (const auto *Conditional =
            dyn_cast<AbstractConditionalOperator>(Inner)) {
      bool Selected = false;
      if (!Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(Selected, Context))
        return AnyOfThis(Selected ? Conditional->getTrueExpr()
                                  : Conditional->getFalseExpr(),
                         Depth + 1);
      return AnyOfThis(Conditional->getTrueExpr(), Depth + 1) ||
             AnyOfThis(Conditional->getFalseExpr(), Depth + 1);
    }
    if (const auto *Comma = dyn_cast<BinaryOperator>(Inner);
        Comma && Comma->getOpcode() == BO_Comma)
      return AnyOfThis(Comma->getRHS(), Depth + 1);
    // What a member function of the object whose body can be seen gives of
    // it, `[pValue = f_Get()]` of `int *f_Get() { return &m_Value; }`, whose
    // `this` is the object.
    if (const auto *Getter = dyn_cast<CXXMemberCallExpr>(Inner);
        Getter && Getter->getType()->isPointerType() &&
        Getter->getMethodDecl() && Getter->getImplicitObjectArgument() &&
        refersToThis(Getter->getImplicitObjectArgument(), 0) &&
        !callsOverride(Getter) && FirstLook(Getter->getMethodDecl(), Depth))
      for (const Expr *Returned : returnedValues(Getter->getMethodDecl()))
        if (AnyOfThis(Returned, Depth + 1))
          return true;
    // Also a copy of an unchanged local pointer into it, `[pCopy = pValue]`
    // with `int *pValue = &m_Value`.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Inner))
      if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
          Local && Local != Variable && Local->hasLocalStorage() &&
          !isa<ParmVarDecl>(Local) && Local->getInit() &&
          (Local->getType()->isPointerType() ||
           Local->getType()->isReferenceType()) &&
          isUnchangedLocalAt(Ref, Local) && FirstLook(Local, Depth) &&
          AnyOfThis(Local->getInit(), Depth + 1))
        return true;
    return IsOfThis(Init);
  };
  return AnyOfThis(Variable->getInit(), 0);
}

/// Whether a lambda \p Ref is in captures \p Variable by copy and changes its
/// copy, which isUnchangedLocal leaves to the lambda.
static bool isChangedInCapturingLambdas(const DeclRefExpr *Ref,
                                        const VarDecl *Variable,
                                        ASTContext &Context) {
  if (!Ref->refersToEnclosingVariableOrCapture())
    return false;
  DynTypedNode Node = DynTypedNode::create(*Ref);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    Node = Parents[0];
    const auto *Lambda = Node.get<LambdaExpr>();
    if (!Lambda)
      continue;
    for (const LambdaCapture &Capture : Lambda->captures()) {
      if (Capture.capturesVariable() && Capture.getCapturedVar() == Variable &&
          Capture.getCaptureKind() == LCK_ByCopy) {
        ReassignmentFinder Finder(Variable);
        Finder.TraverseStmt(Lambda->getBody());
        if (Finder.Found)
          return true;
      }
    }
  }
  return false;
}

const Expr *unchangedLocalValue(const Expr *E) {
  for (int Depth = 0; Depth < 32; ++Depth) {
    const auto *Ref = dyn_cast<DeclRefExpr>(skipWrappers(E));
    const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (!Variable || !Variable->getInit() ||
        !isUnchangedLocalAt(Ref, Variable))
      return E;
    E = Variable->getInit();
  }
  return E;
}

bool isEmptyActor(const Expr *Actor, int Depth) {
  // Not through one made from itself, `TCActor<> Actor = Actor;`, which
  // would never end.
  if (Depth > 16)
    return false;
  Actor = Actor->IgnoreParenImpCasts();
  if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(Actor))
    Actor = Materialize->getSubExpr()->IgnoreParenImpCasts();
  if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(Actor))
    Actor = Bind->getSubExpr()->IgnoreParenImpCasts();
  if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Actor))
    Actor = Cast->getSubExpr()->IgnoreParenImpCasts();
  if (isa<CXXDefaultArgExpr, CXXNullPtrLiteralExpr, CXXScalarValueInitExpr>(
          Actor))
    return true;
  if (const auto *List = dyn_cast<InitListExpr>(Actor))
    return List->getNumInits() == 0;
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(Actor)) {
    if (llvm::all_of(Construct->arguments(), [](const Expr *Argument) {
          return isa<CXXDefaultArgExpr>(Argument);
        }))
      return true;
    // Converted, `TCActor<>(nullptr)`, or copied from an empty one.
    return Construct->getNumArgs() == 1 &&
           isEmptyActor(Construct->getArg(0), Depth + 1);
  }
  // An unchanged local made empty, `TCActor<> const Actor{}`.
  if (isa<DeclRefExpr>(Actor))
    if (const Expr *Value = unchangedLocalValue(Actor);
        Value && Value->IgnoreParenImpCasts() != Actor)
      return isEmptyActor(Value, Depth + 1);
  return false;
}

static bool isOwnActorUncached(const Expr *E, ASTContext &Context, int Depth);

/// Like isOwnActorUncached, each expression told once while the outermost
/// question is answered, which a chain of copies, `auto Copy = Own;`, and of
/// conditionals of them, `bChoose ? Copy : Copy`, meets through many paths,
/// and one that reaches itself, through a local made from itself, is not.
static bool isOwnActorImpl(const Expr *E, ASTContext &Context, int Depth) {
  if (!E || Depth > 256)
    return false;
  static thread_local llvm::DenseMap<const Expr *, bool> *s_pDecided = nullptr;
  std::optional<llvm::DenseMap<const Expr *, bool>> Owned;
  if (!s_pDecided)
    s_pDecided = &Owned.emplace();
  const llvm::scope_exit Release([&] {
    if (Owned)
      s_pDecided = nullptr;
  });
  if (auto It = s_pDecided->find(E); It != s_pDecided->end())
    return It->second;
  (*s_pDecided)[E] = false;
  const bool Own = isOwnActorUncached(E, Context, Depth);
  (*s_pDecided)[E] = Own;
  return Own;
}

static bool isOwnActorUncached(const Expr *E, ASTContext &Context, int Depth) {
  E = skipWrappers(E);
  // Either of a conditional, `bChoose ? Own : OwnCopy`, when both are, or the
  // one a constant condition chooses.
  if (const auto *Conditional =
          dyn_cast<ConditionalOperator>(E->IgnoreParenImpCasts())) {
    bool Chosen = false;
    if (!Conditional->getCond()->isValueDependent() &&
        Conditional->getCond()->EvaluateAsBooleanCondition(Chosen, Context))
      return isOwnActorImpl(Chosen ? Conditional->getTrueExpr()
                                   : Conditional->getFalseExpr(),
                            Context, Depth + 1);
    return isOwnActorImpl(Conditional->getTrueExpr(), Context, Depth + 1) &&
           isOwnActorImpl(Conditional->getFalseExpr(), Context, Depth + 1);
  }
  // Moved, `fg_Move(ThisActor)`, or forwarded, it is the handle it moves.
  if (const auto *Move = dyn_cast<CallExpr>(E->IgnoreParenImpCasts());
      Move && Move->getNumArgs() == 1 &&
      isMoveOrForwardFunction(calleeName(Move)))
    return isOwnActorImpl(Move->getArg(0), Context, Depth + 1);
  // Cast to a reference to it, `static_cast<TCActor<CApp> const &>(Own)`, it
  // is the handle it designates.
  if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E->IgnoreParenImpCasts());
      Cast && Cast->isGLValue() &&
      recordName(Cast->getType()) == recordName(Cast->getSubExpr()->getType()))
    return isOwnActorImpl(Cast->getSubExpr(), Context, Depth + 1);
  // Converted by name, `TCActor<>(self)` or `static_cast<TCActor<>>(self)`,
  // it is what it would be converted implicitly.
  if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E->IgnoreParenImpCasts());
      Cast && !Cast->isGLValue() &&
      (Cast->getCastKind() == CK_ConstructorConversion ||
       Cast->getCastKind() == CK_UserDefinedConversion ||
       Cast->getCastKind() == CK_NoOp))
    return isOwnActorImpl(Cast->getSubExpr(), Context, Depth + 1);
  // `self` of this object, also through an unchanged alias, like
  // `pThis->self`, or of a parameter given the object of the current actor,
  // `_pThis->self`.
  if (const auto *Member = dyn_cast<MemberExpr>(E))
    return Member->getMemberDecl()->getIdentifier() &&
           Member->getMemberDecl()->getName() == "self" &&
           refersToCurrentActorObject(Member->getBase(), &Context);
  if (const auto *Call = asCallTo(E, {"fg_ThisActor"}))
    return Call->getNumArgs() == 1 &&
           refersToCurrentActorObject(Call->getArg(0), &Context);
  // The conversion of `self`, a `CThisActor`, to a handle.
  if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E);
      Call && isa_and_nonnull<CXXConversionDecl>(Call->getMethodDecl()))
    return isOwnActorImpl(Call->getImplicitObjectArgument(), Context,
                          Depth + 1);
  // A weak handle to the own actor, and the handle it locks again, like
  // `fg_ThisActor(this).f_Weak()` and `ThisWeak.f_Lock()`.
  if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E);
      Call && Call->getNumArgs() == 0 && Call->getMethodDecl() &&
      Call->getMethodDecl()->getIdentifier() &&
      (Call->getMethodDecl()->getName() == "f_Weak" ||
       Call->getMethodDecl()->getName() == "f_Lock") &&
      llvm::is_contained(
          {StringRef("TCActor"), StringRef("TCWeakActor"),
           StringRef("TCDistributedActor"), StringRef("TCWeakDistributedActor")},
          recordName(Call->getImplicitObjectArgument()->getType())))
    return isOwnActorImpl(Call->getImplicitObjectArgument(), Context,
                          Depth + 1);
  // A local handle initialized with the own actor that is never changed, nor
  // is the copy of it that a lambda the use is in captured.
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
    // Also one that only this use changes, `fg_Move(ThisActor)`.
    return Variable && Variable->getInit() &&
           (isUnchangedLocal(Variable) || !isChangedOtherThanBy(Ref)) &&
           !isChangedInCapturingLambdas(Ref, Variable, Context) &&
           isOwnActorImpl(Variable->getInit(), Context, Depth + 1);
  }
  return false;
}

bool isAddressOfCall(const CallExpr *Call) {
  if (!Call || Call->getNumArgs() != 1)
    return false;
  if (Call->getBuiltinCallee() == Builtin::BI__builtin_addressof)
    return true;
  const StringRef Name = calleeName(Call);
  return Name == "addressof" || Name == "__addressof" ||
         Name == "fg_AddressOf" || Name == "__builtin_addressof";
}

const FunctionDecl *setterCalled(const CallExpr *Call, unsigned &Offset) {
  Offset = 0;
  if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(Call)) {
    if (Invoke->getOperator() != OO_Call || Invoke->getNumArgs() < 1 ||
        !calledLambda(Invoke->getArg(0)))
      return nullptr;
    Offset = 1;
  } else if (isa<CXXMemberCallExpr>(Call))
    return nullptr;
  const FunctionDecl *Callee = Call->getDirectCallee();
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  return Definition && Definition->getBody() ? Definition : nullptr;
}

llvm::ArrayRef<CSetParameter> setParameters(const FunctionDecl *Function) {
  struct CSets {
    llvm::DenseMap<const FunctionDecl *, llvm::SmallVector<CSetParameter, 1>>
        Functions;
  };
  auto [It, Inserted] = astCache<CSets>(Function->getASTContext())
                            .Functions.try_emplace(Function);
  if (!Inserted || !Function->getBody())
    return It->second;
  llvm::SmallVector<CSetParameter, 1> Sets;
  // Also through an unchanged local copy of it, `auto *pSaved = _pValue;`, or
  // reference to it.
  auto ParameterIndex = [&](const Expr *E) -> std::optional<unsigned> {
    for (int Depth = 0; Depth < 8; ++Depth) {
      E = E->IgnoreParenImpCasts();
      if (const auto *Move = dyn_cast<CallExpr>(E);
          Move && Move->getNumArgs() == 1 &&
          isMoveOrForwardFunction(calleeName(Move)))
        E = Move->getArg(0)->IgnoreParenImpCasts();
      const auto *Ref = dyn_cast<DeclRefExpr>(E);
      if (const auto *Parameter =
              Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl()) : nullptr) {
        if (Parameter->getDeclContext() != Function)
          return std::nullopt;
        return Parameter->getFunctionScopeIndex();
      }
      const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      if (!Local || !Local->hasLocalStorage() || !Local->getInit() ||
          !isUnchangedLocal(Local))
        return std::nullopt;
      E = Local->getInit();
      if (const auto *Copy = dyn_cast<CXXConstructExpr>(E->IgnoreImplicit());
          Copy && Copy->getNumArgs() == 1)
        E = Copy->getArg(0);
    }
    return std::nullopt;
  };
  // Each statement of the body that runs where it is, before any that can
  // return, also in a branch, `if (bSkip) return;`, or jump.
  std::function<bool(const Stmt *)> CanLeave = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return false;
    if (isa<ReturnStmt, CoreturnStmt, GotoStmt, IndirectGotoStmt, CXXThrowExpr>(
            S))
      return true;
    return llvm::any_of(childrenOnce(S), CanLeave);
  };
  llvm::SmallPtrSet<const Stmt *, 8> Always;
  if (const auto *Body = dyn_cast<CompoundStmt>(Function->getBody()))
    for (const Stmt *Statement : Body->body()) {
      if (hasJumpTarget(Statement))
        break;
      Always.insert(Statement);
      if (CanLeave(Statement))
        break;
    }
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return;
    // A pointer given by reference, `o_pValue = ...`, or by its address,
    // `*_ppValue = ...`.
    auto AssignedTarget =
        [&](const Expr *Left) -> std::pair<std::optional<unsigned>, bool> {
      if (const std::optional<unsigned> Target = ParameterIndex(Left);
          Target &&
          Function->getParamDecl(*Target)->getType()->isReferenceType() &&
          Function->getParamDecl(*Target)
              ->getType()
              .getNonReferenceType()
              ->isPointerType())
        return {Target, false};
      Left = Left->IgnoreParenImpCasts();
      const Expr *Pointer = nullptr;
      if (const auto *Deref = dyn_cast<UnaryOperator>(Left);
          Deref && Deref->getOpcode() == UO_Deref)
        Pointer = Deref->getSubExpr();
      else if (const auto *Element = dyn_cast<ArraySubscriptExpr>(Left)) {
        Expr::EvalResult Index;
        if (!Element->getIdx()->isValueDependent() &&
            Element->getIdx()->EvaluateAsInt(Index,
                                             Function->getASTContext()) &&
            Index.Val.getInt().isZero())
          Pointer = Element->getBase();
      }
      if (const std::optional<unsigned> Target =
              Pointer ? ParameterIndex(Pointer) : std::nullopt;
          Target) {
        const QualType Type = Function->getParamDecl(*Target)->getType();
        if (Type->isPointerType() && Type->getPointeeType()->isPointerType() &&
            !Type->getPointeeType().isConstQualified())
          return {Target, true};
      }
      return {std::nullopt, false};
    };
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->getOpcode() == BO_Assign)
      if (const auto Assigned = AssignedTarget(Assign->getLHS());
          Assigned.first) {
        // Not structured bindings, which a lambda captures only in C++20.
        const std::optional<unsigned> &Target = Assigned.first;
        const bool ByPointer = Assigned.second;
        // Also the address of what a reference parameter refers to, `_pValue
        // = &_Value`, which is what its argument is, and either of a
        // conditional, `_bFirst ? _pFirst : _pSecond`: the first for sure as
        // much as the assignment, and the other maybe after it. Only the one
        // a constant condition picks, `t_bFirst ? _pFirst : _pSecond`.
        bool First = true;
        std::function<void(const Expr *, int)> AddSource = [&](const Expr
                                                                   *Value,
                                                               int Depth) {
          Value = Value->IgnoreParenImpCasts();
          if (const auto *Conditional =
                  dyn_cast<AbstractConditionalOperator>(Value);
              Conditional && Depth < 8) {
            bool Picked = false;
            if (const Expr *Condition = Conditional->getCond();
                !Condition->isValueDependent() &&
                Condition->EvaluateAsBooleanCondition(
                    Picked, Function->getASTContext())) {
              AddSource(Picked ? Conditional->getTrueExpr()
                               : Conditional->getFalseExpr(),
                        Depth + 1);
              return;
            }
            AddSource(Conditional->getTrueExpr(), Depth + 1);
            AddSource(Conditional->getFalseExpr(), Depth + 1);
            return;
          }
          // An offset into what a pointer parameter points to, `_pBase + 1`
          // and `&_pBase[1]`, which points into the same object.
          if (const auto *Offset = dyn_cast<BinaryOperator>(Value);
              Offset && Offset->isAdditiveOp() && Depth < 8 &&
              Offset->getType()->isPointerType()) {
            AddSource(Offset->getLHS()->getType()->isPointerType()
                          ? Offset->getLHS()
                          : Offset->getRHS(),
                      Depth + 1);
            return;
          }
          if (const auto *Address = dyn_cast<UnaryOperator>(Value);
              Address && Address->getOpcode() == UO_AddrOf && Depth < 8)
            if (const auto *Element = dyn_cast<ArraySubscriptExpr>(
                    Address->getSubExpr()->IgnoreParens());
                Element && Element->getBase()->getType()->isPointerType()) {
              AddSource(Element->getBase(), Depth + 1);
              return;
            }
          const bool Sure = First && Always.contains(Assign);
          First = false;
          if (const auto *Address = dyn_cast<UnaryOperator>(Value);
              Address && Address->getOpcode() == UO_AddrOf) {
            if (const std::optional<unsigned> Source =
                    ParameterIndex(Address->getSubExpr());
                Source &&
                Function->getParamDecl(*Source)->getType()->isReferenceType())
              Sets.push_back(
                  {*Target, *Source, Sure, /*Addressed=*/true, ByPointer});
          } else if (const std::optional<unsigned> Source =
                         ParameterIndex(Value))
            Sets.push_back({*Target, *Source, Sure, false, ByPointer});
        };
        AddSource(Assign->getRHS(), 0);
      }
    // Also by giving them to another that sets one of its parameters to
    // another, `fg_Set(o_pValue, _Value)`, also a lambda, each function once,
    // of a call that is a statement on every path when that sets it on every
    // path.
    unsigned Offset = 0;
    if (const auto *Call = dyn_cast<CallExpr>(S))
      if (const FunctionDecl *Callee = setterCalled(Call, Offset))
        for (const CSetParameter &Set : setParameters(Callee)) {
          if (Set.Target + Offset >= Call->getNumArgs() ||
              Set.Source + Offset >= Call->getNumArgs())
            continue;
          // A pointer it gives on as it is given, by reference or by its
          // address, or by its address that it is given by reference,
          // `fg_Set(&o_pValue, ...)`.
          std::optional<unsigned> Target;
          bool ByPointer = false;
          const Expr *TargetArgument =
              Call->getArg(Set.Target + Offset)->IgnoreParenImpCasts();
          if (!Set.TargetByPointer) {
            Target = ParameterIndex(TargetArgument);
            if (const auto *Deref = dyn_cast<UnaryOperator>(TargetArgument);
                !Target && Deref && Deref->getOpcode() == UO_Deref) {
              Target = ParameterIndex(Deref->getSubExpr());
              ByPointer = true;
            }
          } else if (const auto *Address =
                         dyn_cast<UnaryOperator>(TargetArgument);
                     Address && Address->getOpcode() == UO_AddrOf)
            Target = ParameterIndex(Address->getSubExpr());
          else {
            Target = ParameterIndex(TargetArgument);
            ByPointer = true;
          }
          // What it is given at the source, also the address of what a
          // reference parameter refers to, `fg_Set(o_pValue, &_Value)`, for
          // a pointer, and what a pointer parameter points to, `*_pValue`,
          // for a reference.
          std::optional<unsigned> Source =
              ParameterIndex(Call->getArg(Set.Source + Offset));
          bool Addressed = Set.Addressed;
          const Expr *Given =
              Call->getArg(Set.Source + Offset)->IgnoreParenImpCasts();
          if (const auto *Unary = dyn_cast<UnaryOperator>(Given);
              !Source && Unary &&
              Unary->getOpcode() == (Set.Addressed ? UO_Deref : UO_AddrOf)) {
            Source = ParameterIndex(Unary->getSubExpr());
            Addressed = !Set.Addressed;
            if (Source &&
                Function->getParamDecl(*Source)->getType()->isReferenceType() !=
                    Addressed)
              Source = std::nullopt;
          }
          const QualType TargetType =
              Target ? Function->getParamDecl(*Target)->getType() : QualType();
          if (Target && Source &&
              (ByPointer
                   ? TargetType->isPointerType() &&
                         TargetType->getPointeeType()->isPointerType()
                   : TargetType->isReferenceType() &&
                         TargetType.getNonReferenceType()->isPointerType()))
            Sets.push_back({*Target, *Source,
                            Set.Always && Always.contains(Call), Addressed,
                            ByPointer});
        }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Function->getBody());
  // Each set once, where it is last, which leaves what the parameters hold
  // after the function as it is: branches that call the same functions,
  // `if (_bFirst) fg_Select(o_pValue, _pFirst, _pSecond); else
  // fg_Select(o_pValue, _pSecond, _pFirst);`, would otherwise double them for
  // each level of callees that do the same.
  llvm::SmallVector<CSetParameter, 1> Unique;
  llvm::DenseSet<std::pair<unsigned, unsigned>> Seen;
  for (const CSetParameter &Set : llvm::reverse(Sets))
    if (Seen.insert({Set.Target,
                     Set.Source * 8 + (Set.TargetByPointer ? 4 : 0) +
                         (Set.Always ? 2 : 0) + (Set.Addressed ? 1 : 0)})
            .second)
      Unique.push_back(Set);
  std::reverse(Unique.begin(), Unique.end());
  return astCache<CSets>(Function->getASTContext()).Functions[Function] =
             std::move(Unique);
}

const Expr *setTargetArgument(const CallExpr *Call, const CSetParameter &Set,
                              unsigned Offset) {
  if (Set.Target + Offset >= Call->getNumArgs())
    return nullptr;
  const Expr *Argument = Call->getArg(Set.Target + Offset);
  if (!Set.TargetByPointer)
    return Argument;
  const auto *Address =
      dyn_cast<UnaryOperator>(Argument->IgnoreParenImpCasts());
  return Address && Address->getOpcode() == UO_AddrOf ? Address->getSubExpr()
                                                      : nullptr;
}

bool sameLocalPart(const Expr *Left, const Expr *Right) {
  for (int Depth = 0; Left && Right && Depth < 16; ++Depth) {
    Left = Left->IgnoreParenImpCasts();
    Right = Right->IgnoreParenImpCasts();
    const auto *LeftMember = dyn_cast<MemberExpr>(Left);
    const auto *RightMember = dyn_cast<MemberExpr>(Right);
    if (LeftMember && RightMember) {
      if (LeftMember->getMemberDecl() != RightMember->getMemberDecl() ||
          LeftMember->isArrow() || RightMember->isArrow())
        return false;
      Left = LeftMember->getBase();
      Right = RightMember->getBase();
      continue;
    }
    const auto *LeftRef = dyn_cast<DeclRefExpr>(Left);
    const auto *RightRef = dyn_cast<DeclRefExpr>(Right);
    return LeftRef && RightRef && LeftRef->getDecl() == RightRef->getDecl() &&
           isa<VarDecl>(LeftRef->getDecl()) &&
           cast<VarDecl>(LeftRef->getDecl())->hasLocalStorage();
  }
  return false;
}

const FunctionDecl *functionCalledBy(const CallExpr *Call) {
  if (const FunctionDecl *Callee = Call->getDirectCallee())
    return Callee;
  const Expr *Callee = Call->getCallee()->IgnoreParenImpCasts();
  // Through an unchanged local made from the function, `auto fRun = &fg_Run;`,
  // also through unchanged copies of it, `auto fCopy = fRun;`, each once.
  llvm::SmallPtrSet<const VarDecl *, 4> Followed;
  while (true) {
    if (const auto *Deref = dyn_cast<UnaryOperator>(Callee);
        Deref && Deref->getOpcode() == UO_Deref)
      Callee = Deref->getSubExpr()->IgnoreParenImpCasts();
    if (const auto *Address = dyn_cast<UnaryOperator>(Callee);
        Address && Address->getOpcode() == UO_AddrOf)
      Callee = Address->getSubExpr()->IgnoreParenImpCasts();
    const auto *Ref = dyn_cast<DeclRefExpr>(Callee);
    if (!Ref)
      return nullptr;
    if (const auto *Function = dyn_cast<FunctionDecl>(Ref->getDecl()))
      return Function;
    const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
    if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
        !Local->getInit() || isChangedVariable(Local) ||
        !Followed.insert(Local).second)
      return nullptr;
    Callee = Local->getInit()->IgnoreParenImpCasts();
  }
}

bool awaitsTransferOwnership(const CoroutineSuspendExpr *Await) {
  if (isTransferOwnership(Await->getOperand()->getType()))
    return true;
  const Expr *Awaiter = Await->getCommonExpr();
  return Awaiter && isTransferOwnership(Awaiter->getType());
}

bool isTransferOwnership(QualType Type) {
  if (Type.isNull())
    return false;
  const CXXRecordDecl *Record =
      Type.getNonReferenceType()->getAsCXXRecordDecl();
  auto Is = [](const CXXRecordDecl *Class) {
    return Class->getIdentifier() &&
           (Class->getName() == "CCoroutineTransferOwnership" ||
            Class->getName() == "CCoroutineTransferOwnershipAwaiter");
  };
  if (!Record)
    return false;
  if (Is(Record))
    return true;
  // Each class once, by the class, whose bases are each looked at once, also
  // one that many paths reach.
  struct CTransfers {
    llvm::DenseMap<const CXXRecordDecl *, bool> Records;
  };
  llvm::DenseMap<const CXXRecordDecl *, bool> &Decided =
      astCache<CTransfers>(Record->getASTContext()).Records;
  Record = Record->getCanonicalDecl();
  if (const auto It = Decided.find(Record); It != Decided.end())
    return It->second;
  bool Transfers = false;
  llvm::SmallPtrSet<const CXXRecordDecl *, 16> Visited;
  llvm::SmallVector<const CXXRecordDecl *, 16> Work{Record};
  while (!Work.empty() && !Transfers) {
    const CXXRecordDecl *Current = Work.pop_back_val();
    const CXXRecordDecl *Definition =
        Current ? Current->getDefinition() : nullptr;
    if (!Definition || Definition->isDependentContext() ||
        !Visited.insert(Definition).second)
      continue;
    for (const CXXBaseSpecifier &Base : Definition->bases()) {
      const CXXRecordDecl *Class = Base.getType()->getAsCXXRecordDecl();
      if (Class && Is(Class)) {
        Transfers = true;
        break;
      }
      Work.push_back(Class);
    }
  }
  Decided[Record] = Transfers;
  return Transfers;
}

StringRef recordName(QualType Type) {
  if (Type.isNull())
    return {};
  Type = Type.getNonReferenceType();
  if (const auto *Specialization =
          Type->getAs<TemplateSpecializationType>()) {
    if (const TemplateDecl *Template =
            Specialization->getTemplateName().getAsTemplateDecl())
      return Template->getName();
  }
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record || !Record->getIdentifier())
    return {};
  if (const auto *Specialization =
          dyn_cast<ClassTemplateSpecializationDecl>(Record))
    return Specialization->getSpecializedTemplate()->getName();
  return Record->getName();
}

bool isOwnActor(const Expr *E, ASTContext &Context) {
  return isOwnActorImpl(E, Context, 0);
}

StringRef calleeName(const CallExpr *Call) {
  if (const FunctionDecl *Callee = Call->getDirectCallee())
    return Callee->getIdentifier() ? Callee->getName() : StringRef();
  return {};
}

bool resetsFirst(const CXXConstructorDecl *Constructor, const FieldDecl *Field,
                 llvm::function_ref<bool(const Expr *)> Keeps) {
  const auto *Body =
      Constructor ? dyn_cast_or_null<CompoundStmt>(Constructor->getBody())
                  : nullptr;
  if (!Field || !Body || !Field->getType()->isPointerType())
    return false;
  // Also what can read it through the object, a call or `this`, or leave.
  std::function<bool(const Stmt *)> Uses = [&](const Stmt *S) {
    if (!S)
      return false;
    if (const auto *Member = dyn_cast<MemberExpr>(S);
        Member && Member->getMemberDecl() == Field)
      return true;
    return isa<ReturnStmt, GotoStmt, CXXThrowExpr, CoreturnStmt, CallExpr,
               CXXConstructExpr, CXXThisExpr, LambdaExpr>(S) ||
           llvm::any_of(childrenOnce(S), Uses);
  };
  for (const Stmt *Statement : Body->body()) {
    if (const auto *Assign = dyn_cast<BinaryOperator>(Statement);
        Assign && Assign->getOpcode() == BO_Assign) {
      const auto *Target =
          dyn_cast<MemberExpr>(Assign->getLHS()->IgnoreParenImpCasts());
      if (Target && Target->getMemberDecl() == Field &&
          isa<CXXThisExpr>(Target->getBase()->IgnoreParenImpCasts()))
        return !Uses(Assign->getRHS()) && !Keeps(Assign->getRHS());
    }
    if (Uses(Statement) || hasJumps(Statement))
      return false;
  }
  return false;
}

bool capturedByReferenceThroughout(const LambdaExpr *Lambda,
                                   const ValueDecl *Variable,
                                   ASTContext &Context) {
  auto CaptureKind =
      [&](const LambdaExpr *Around) -> std::optional<LambdaCaptureKind> {
    for (const LambdaCapture &Capture : Around->captures())
      if (Capture.capturesVariable() && Capture.getCapturedVar() == Variable)
        return Capture.getCaptureKind();
    return std::nullopt;
  };
  if (CaptureKind(Lambda) != std::optional<LambdaCaptureKind>(LCK_ByRef))
    return false;
  DynTypedNode Node = DynTypedNode::create(*Lambda);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return true;
    Node = Parents[0];
    if (const auto *Around = Node.get<LambdaExpr>())
      if (const std::optional<LambdaCaptureKind> Kind = CaptureKind(Around);
          Kind && *Kind != LCK_ByRef)
        return false;
    if (const auto *Function = Node.get<FunctionDecl>();
        Function && !isLambdaCallOperator(Function))
      return true;
  }
  return true;
}

bool returnsOnlyStatic(const FunctionDecl *Function) {
  const FunctionDecl *Definition =
      Function ? Function->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody())
    return false;
  // Also a reference to one, `static int s_Value; return s_Value;`.
  return onlyStatic(returnedValues(Definition),
                    Definition->getReturnType()->isReferenceType());
}

bool onlyStatic(llvm::ArrayRef<const Expr *> Values, bool ByReference) {
  // Also either of a conditional, `bPresent ? "Present" : "Empty"`.
  std::function<bool(const Expr *, int)> IsStatic = [&](const Expr *Value,
                                                        int Depth) {
    Value = Value->IgnoreParenImpCasts();
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Value);
        Conditional && Depth < 8)
      return IsStatic(Conditional->getTrueExpr(), Depth + 1) &&
             IsStatic(Conditional->getFalseExpr(), Depth + 1);
    if (isa<StringLiteral, CXXNullPtrLiteralExpr, IntegerLiteral, GNUNullExpr>(
            Value))
      return true;
    // An unchanged local pointer it returns, `int const *pValue = &g_Value;
    // return pValue;`, gives what it is made from, which is among the values
    // too.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Value))
      if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
          Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
          Local->getType()->isPointerType() && Local->getInit() &&
          !isChangedVariable(Local))
        return true;
    const auto *Address = dyn_cast<UnaryOperator>(Value);
    if (Address && Address->getOpcode() == UO_AddrOf)
      Value = Address->getSubExpr()->IgnoreParenImpCasts();
    else
      Address = nullptr;
    const auto *Ref = dyn_cast<DeclRefExpr>(Value);
    const auto *Static = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    // Its address, or an array decayed to one, not a pointer it holds,
    // `static int *s_pCached`, which can point anywhere.
    return Static && Static->hasGlobalStorage() &&
           Static->getTLSKind() == VarDecl::TLS_None &&
           !Static->getType()->isReferenceType() &&
           (Address || ByReference || Static->getType()->isArrayType());
  };
  return !Values.empty() && llvm::all_of(Values, [&](const Expr *Value) {
    return IsStatic(Value, 0);
  });
}

bool isOnlyTestedPointer(const Expr *Pointer, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Pointer);
  for (int Depth = 0; Depth < 64; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    const DynTypedNode &Up = Parents[0];
    // Also explicitly, `bool(_pValue)`, where the parents can skip the
    // conversion to bool.
    if (const auto *Test = Up.get<ExplicitCastExpr>();
        Test && Test->getType()->isBooleanType())
      return true;
    if (const auto *Cast = Up.get<ImplicitCastExpr>()) {
      if (Cast->getCastKind() == CK_PointerToBoolean)
        return true;
      if (Cast->getCastKind() != CK_LValueToRValue &&
          Cast->getCastKind() != CK_NoOp && Cast->getCastKind() != CK_BitCast)
        return false;
    } else if (const auto *Binary = Up.get<BinaryOperator>())
      return Binary->isEqualityOp() || Binary->isRelationalOp() ||
             Binary->isLogicalOp();
    else if (const auto *Unary = Up.get<UnaryOperator>())
      return Unary->getOpcode() == UO_LNot;
    // Also tested as a condition where the parents skip the conversion to
    // bool, `if (_pValue)` or `_pValue ? 1 : 0`.
    else if (const Expr *Tested = Node.get<Expr>();
             Tested &&
             ((Up.get<IfStmt>() && Up.get<IfStmt>()->getCond() &&
               Up.get<IfStmt>()->getCond()->IgnoreParenImpCasts() ==
                   Tested->IgnoreParenImpCasts()) ||
              (Up.get<WhileStmt>() &&
               Up.get<WhileStmt>()->getCond()->IgnoreParenImpCasts() ==
                   Tested->IgnoreParenImpCasts()) ||
              (Up.get<DoStmt>() &&
               Up.get<DoStmt>()->getCond()->IgnoreParenImpCasts() ==
                   Tested->IgnoreParenImpCasts()) ||
              (Up.get<ForStmt>() && Up.get<ForStmt>()->getCond() &&
               Up.get<ForStmt>()->getCond()->IgnoreParenImpCasts() ==
                   Tested->IgnoreParenImpCasts()) ||
              (Up.get<AbstractConditionalOperator>() &&
               Up.get<AbstractConditionalOperator>()
                       ->getCond()
                       ->IgnoreParenImpCasts() ==
                   Tested->IgnoreParenImpCasts())))
      return true;
    else if (!Up.get<ParenExpr>())
      return false;
    Node = Up;
  }
  return false;
}

bool readsOnlyOwnPart(const Stmt *S, ASTContext &Context) {
  const auto *Ref = dyn_cast_or_null<DeclRefExpr>(S);
  const auto *Parameter = Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl()) : nullptr;
  auto OnlyTested = [&](const Expr *Pointer) {
    return isOnlyTestedPointer(Pointer, Context);
  };
  // A pointer taken by value reads only itself, which is in the frame.
  if (Parameter && Parameter->getType()->isPointerType())
    return OnlyTested(Ref);
  if (!Parameter || Parameter->getType()->isReferenceType() ||
      Parameter->getType()->isPointerType() ||
      !Parameter->getType()->isRecordType())
    return false;
  // A scalar that is a value, not one that refers or points elsewhere.
  auto IsOwnScalar = [](QualType Type) {
    return !Type->isReferenceType() && !Type->isPointerType() &&
           (Type->isArithmeticType() || Type->isEnumeralType());
  };
  auto Parent = [&](const Stmt *Child) -> const Stmt * {
    for (DynTypedNode Node = DynTypedNode::create(*Child);;) {
      const auto Parents = instantiatedParents(Node, Context);
      const Stmt *Up = Parents.empty() ? nullptr : Parents[0].get<Stmt>();
      if (!Up || !isa<ParenExpr>(Up))
        return Up;
      Node = Parents[0];
    }
  };
  const auto *Member = dyn_cast_or_null<MemberExpr>(Parent(Ref));
  if (!Member || Member->isArrow())
    return false;
  if (const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl())) {
    // Also a pointer it holds, compared or tested, `_Span.m_pStr ==
    // _Other.m_pStr`, which doesn't read what it points to.
    if (Field->getType()->isPointerType())
      return OnlyTested(Member);
    if (!IsOwnScalar(Field->getType()))
      return false;
    const Stmt *Above = Parent(Member);
    // Also what `co_return` gives, which the promise reads as a value.
    if (const auto *Return = dyn_cast_or_null<CoreturnStmt>(Above);
        Return && Return->getOperand() == Member)
      if (const auto *Promise =
              dyn_cast_or_null<CallExpr>(Return->getPromiseCall());
          Promise && Promise->getNumArgs() == 1)
        Above = Promise->getArg(0);
    // Also one it updates, `Text.m_Length = 0` or `++Text.m_Length`, which
    // is in the frame too.
    if (const auto *Assign = dyn_cast_or_null<BinaryOperator>(Above);
        Assign && Assign->isAssignmentOp() &&
        Assign->getLHS()->IgnoreParens() == Member)
      return true;
    if (const auto *Step = dyn_cast_or_null<UnaryOperator>(Above);
        Step && Step->isIncrementDecrementOp() &&
        Step->getSubExpr()->IgnoreParens() == Member)
      return true;
    // Also a reference bound to it, `auto &nBytes = Span.m_nBytes`, or its
    // address, which are of the frame too.
    if (const auto *Address = dyn_cast_or_null<UnaryOperator>(Above);
        Address && Address->getOpcode() == UO_AddrOf &&
        Address->getSubExpr()->IgnoreParens() == Member)
      return true;
    if (!Above) {
      DynTypedNode Node = DynTypedNode::create(*Member);
      for (int Depth = 0; Depth < 4; ++Depth) {
        const auto Parents = instantiatedParents(Node, Context);
        if (Parents.empty() || !Parents[0].get<ParenExpr>()) {
          if (const auto *Alias =
                  Parents.empty() ? nullptr : Parents[0].get<VarDecl>())
            return Alias->getType()->isReferenceType();
          break;
        }
        Node = Parents[0];
      }
    }
    const auto *Read = dyn_cast_or_null<ImplicitCastExpr>(Above);
    return Read && Read->getCastKind() == CK_LValueToRValue &&
           Read->getSubExpr()->IgnoreParens() == Member;
  }
  const auto *Method = dyn_cast<CXXMethodDecl>(Member->getMemberDecl());
  const auto *Call = dyn_cast_or_null<CXXMemberCallExpr>(Parent(Member));
  // Not a virtual one, which an override can make read what the view refers
  // to.
  // Also one that gives a pointer it holds, `f_GetStr()` of `return m_pStr;`,
  // compared or tested.
  const bool GivesPointer = Method && Method->getReturnType()->isPointerType();
  if (!Method || !Call || Call->getNumArgs() != 0 || !Method->isConst() ||
      !(IsOwnScalar(Method->getReturnType()) ||
        (GivesPointer && OnlyTested(Call))) ||
      (Method->isVirtual() && !Method->hasAttr<FinalAttr>() &&
       !Method->getParent()->hasAttr<FinalAttr>()))
    return false;
  const FunctionDecl *Definition = Method->getDefinition();
  if (!Definition || !Definition->getBody())
    return false;
  // Its body only reads scalar members of the object, `return m_Length;`.
  bool OwnOnly = true;
  std::function<void(const Stmt *, const Stmt *)> Visit =
      [&](const Stmt *Inner, const Stmt *Above) {
        if (!Inner || !OwnOnly)
          return;
        if (isa<CallExpr, ArraySubscriptExpr, LambdaExpr, CXXNewExpr,
                CXXDeleteExpr, AsmStmt>(Inner)) {
          OwnOnly = false;
          return;
        }
        if (const auto *Unary = dyn_cast<UnaryOperator>(Inner);
            Unary && (Unary->getOpcode() == UO_Deref ||
                      Unary->getOpcode() == UO_AddrOf)) {
          OwnOnly = false;
          return;
        }
        if (const auto *Own = dyn_cast<MemberExpr>(Inner)) {
          const auto *Field = dyn_cast<FieldDecl>(Own->getMemberDecl());
          if (!Field ||
              !(IsOwnScalar(Field->getType()) ||
                (GivesPointer && Field->getType()->isPointerType())) ||
              !isa<CXXThisExpr>(Own->getBase()->IgnoreParenImpCasts())) {
            OwnOnly = false;
            return;
          }
          return;
        }
        // `this` only as the object of such a member.
        if (isa<CXXThisExpr>(Inner) && !isa_and_nonnull<MemberExpr>(Above)) {
          OwnOnly = false;
          return;
        }
        for (const Stmt *Child : childrenOnce(Inner))
          Visit(Child, Inner);
      };
  Visit(Definition->getBody(), nullptr);
  return OwnOnly;
}

llvm::SmallVector<const Expr *, 2> awaitedValues(const Expr *Operand) {
  llvm::SmallVector<const Expr *, 2> Values;
  // Each local followed once, whose values are then already there, also one
  // that others are made from many times, `auto A1 = b ? A0 : A0;`; one met
  // again while it is followed is made from itself.
  llvm::DenseMap<const VarDecl *, bool> Followed;
  std::function<void(const Expr *, int)> Visit = [&](const Expr *E, int Depth) {
    if (!E || Depth > 256) {
      Values.push_back(nullptr);
      return;
    }
    E = skipWrappers(E)->IgnoreParenImpCasts();
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E)) {
      // Only the one a constant condition chooses, `true ? fg_OnResume(A) :
      // fg_OnResume(B)`, told in the context of what it names.
      const ASTContext *Context = nullptr;
      std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
        if (!S || Context)
          return;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S)) {
          Context = &Ref->getDecl()->getASTContext();
          return;
        }
        for (const Stmt *Child : childrenOnce(S))
          Find(Child);
      };
      Find(Conditional);
      bool Chosen = false;
      if (const auto *Choice = dyn_cast<ConditionalOperator>(Conditional);
          Choice && Context && !Choice->getCond()->isValueDependent() &&
          Choice->getCond()->EvaluateAsBooleanCondition(Chosen, *Context))
        return Visit(Chosen ? Choice->getTrueExpr() : Choice->getFalseExpr(),
                     Depth + 1);
      Visit(Conditional->getTrueExpr(), Depth + 1);
      Visit(Conditional->getFalseExpr(), Depth + 1);
      return;
    }
    if (const auto *Comma = dyn_cast<BinaryOperator>(E);
        Comma && Comma->getOpcode() == BO_Comma) {
      Visit(Comma->getRHS(), Depth + 1);
      return;
    }
    const Expr *Named = E;
    if (const auto *Move = dyn_cast<CallExpr>(E);
        Move && Move->getNumArgs() == 1 &&
        (isMoveFunction(calleeName(Move)) ||
         isForwardFunction(calleeName(Move))))
      Named = Move->getArg(0)->IgnoreParenImpCasts();
    const auto *Ref = dyn_cast<DeclRefExpr>(Named);
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
        !Local->getInit()) {
      Values.push_back(E);
      return;
    }
    auto [It, Inserted] = Followed.try_emplace(Local, false);
    if (!Inserted) {
      if (!It->second)
        Values.push_back(nullptr);
      return;
    }
    // What it has where it is awaited: what it was last assigned, `Pending =
    // fg_Later()`, or else what it was made with, or what can't be told when
    // a branch assigns it.
    Visit(assignedValueAt(Local, Ref), Depth + 1);
    Followed[Local] = true;
  };
  Visit(Operand, 0);
  return Values;
}

llvm::SmallVector<const CallExpr *, 2> onResumeCalls(const Expr *Operand) {
  llvm::SmallVector<const CallExpr *, 2> Calls;
  for (const Expr *Value : awaitedValues(Operand)) {
    // Also what a function whose body can be seen gives back, `co_await
    // f_Watch()` of `auto f_Watch() { return fg_OnResume(...); }`, through a
    // few of them, each value it can give back: one that isn't a
    // registration, or can't be followed, is null.
    std::function<void(const Expr *, int)> Add = [&](const Expr *Given,
                                                     int Depth) {
      if (const CallExpr *Call = asCallTo(Given, {"fg_OnResume"})) {
        Calls.push_back(Call);
        return;
      }
      const auto *Factory =
          Given ? dyn_cast<CallExpr>(skipWrappers(Given)->IgnoreParens())
                : nullptr;
      const FunctionDecl *Callee =
          Factory && Depth < 4 ? Factory->getDirectCallee() : nullptr;
      const FunctionDecl *Definition =
          Callee ? Callee->getDefinition() : nullptr;
      const llvm::SmallVector<const Expr *, 4> Returned =
          Definition && Definition->getBody()
              ? returnedValues(Definition)
              : llvm::SmallVector<const Expr *, 4>();
      if (Returned.empty()) {
        Calls.push_back(nullptr);
        return;
      }
      for (const Expr *Value : Returned)
        Add(Value, Depth + 1);
    };
    Add(Value, 0);
  }
  return Calls;
}

llvm::SmallVector<const CoroutineSuspendExpr *, 2>
holderAwaits(const Expr *Init, const ASTContext *Context) {
  llvm::SmallVector<const CoroutineSuspendExpr *, 2> Awaits;
  // Past as many conditionals as the code can have, an alternative that
  // isn't told, which guards nothing.
  std::function<void(const Expr *, int)> Visit = [&](const Expr *E,
                                                     int Depth) {
    if (!E)
      return;
    if (Depth > 256) {
      Awaits.push_back(nullptr);
      return;
    }
    E = skipWrappers(E)->IgnoreParenImpCasts();
    // Of a conditional, the one a constant condition chooses, `t_bStop ?
    // co_await fp_CheckStoppedOrDestroyedOnResume() : ...`, or else each.
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E)) {
      bool Chosen = false;
      if (Context && !Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(Chosen,
                                                             *Context)) {
        Visit(Chosen ? Conditional->getTrueExpr() : Conditional->getFalseExpr(),
              Depth + 1);
        return;
      }
      Visit(Conditional->getTrueExpr(), Depth + 1);
      Visit(Conditional->getFalseExpr(), Depth + 1);
      return;
    }
    // What a comma gives last, `(0, co_await fg_OnResume(...))`.
    if (const auto *Comma = dyn_cast<BinaryOperator>(E);
        Comma && Comma->getOpcode() == BO_Comma) {
      Visit(Comma->getRHS(), Depth + 1);
      return;
    }
    // Each element of an array that holds them, `CGuard Guards[] = {co_await
    // fg_AsyncDestroy(...)}`.
    if (const auto *List = dyn_cast<InitListExpr>(E);
        List && List->getType()->isArrayType()) {
      for (const Expr *Element : List->inits())
        Visit(Element, Depth + 1);
      return;
    }
    Awaits.push_back(dyn_cast<CoroutineSuspendExpr>(E));
  };
  Visit(Init, 0);
  return Awaits;
}

const CallExpr *asCallTo(const Expr *E, llvm::ArrayRef<StringRef> Names) {
  if (!E)
    return nullptr;
  E = E->IgnoreImplicit()->IgnoreParens()->IgnoreImplicit();
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(E);
      Construct && Construct->getNumArgs() == 1)
    E = Construct->getArg(0)->IgnoreImplicit()->IgnoreParens();
  const auto *Call = dyn_cast<CallExpr>(E);
  if (!Call || isa<CXXMemberCallExpr>(Call) || isa<CXXOperatorCallExpr>(Call))
    return nullptr;
  const StringRef Name = calleeName(Call);
  return !Name.empty() && llvm::is_contained(Names, Name) ? Call : nullptr;
}

const Stmt *firstCaptureUseAfterSuspension(const CXXMethodDecl *CallOperator,
                                           ASTContext &Context) {
  const CXXRecordDecl *Closure = CallOperator->getParent();
  llvm::SmallPtrSet<const ValueDecl *, 8> Captured;
  // The copies a lambda captures live in its closure, so references and
  // pointers into them dangle with it too.
  llvm::SmallPtrSet<const ValueDecl *, 8> Copies;
  bool CapturesThis = false;
  // A copy of the object, `[*this]`, lives in the closure too.
  bool CopiesThis = false;
  for (const LambdaCapture &Capture : Closure->captures()) {
    if (Capture.capturesThis()) {
      CapturesThis = true;
      CopiesThis = Capture.getCaptureKind() == LCK_StarThis;
    } else if (Capture.capturesVariable()) {
      Captured.insert(Capture.getCapturedVar());
      if (Capture.getCaptureKind() == LCK_ByCopy ||
          (isa<VarDecl>(Capture.getCapturedVar()) &&
           cast<VarDecl>(Capture.getCapturedVar())->isInitCapture() &&
           !Capture.getCapturedVar()->getType()->isReferenceType()))
        Copies.insert(Capture.getCapturedVar());
    }
  }
  auto IsCopy = [&](const Stmt *S) {
    if (CopiesThis && isa<CXXThisExpr>(S))
      return true;
    const auto *Ref = dyn_cast<DeclRefExpr>(S);
    return Ref && Copies.contains(Ref->getDecl());
  };
  auto IsView = [&](QualType Type) { return isDefaultViewType(Type, Context); };
  BorrowRoots Roots{IsCopy, /*PointerValues=*/false, IsView};
  // A lambda inside that captures `this` copies the pointer, unless `this` is
  // the closure's own copy of the object, `[*this]`, which it points into.
  Roots.ThisCaptureIsCopy = CapturesThis && !CopiesThis;
  Roots.OfClosure = true;
  return firstUseAfterSuspension(
      CallOperator,
      [&](const Stmt *S) {
        if (CapturesThis && isa<CXXThisExpr>(S))
          return true;
        const auto *Ref = dyn_cast<DeclRefExpr>(S);
        return Ref && Captured.contains(Ref->getDecl());
      },
      Context, &Roots);
}

bool isKnownSafeThisClass(const CXXRecordDecl *Record) {
  return Record &&
         derivesFrom(Record, {"CActor", "CActorInternal", "CAllowUnsafeThis"});
}

bool isActorCallClass(const CXXRecordDecl *Record) {
  return Record && derivesFrom(Record, {"CActor"});
}

const FunctionDecl *enclosingFunction(const LambdaExpr *Lambda) {
  return dyn_cast_or_null<FunctionDecl>(
      Lambda->getLambdaClass()->getDeclContext());
}

/// Whether \p Init makes an owning pointer of the object of the code it is
/// in: from `this`, `fg_Explicit(this)` or an unchanged local alias of them,
/// like `TCSharedPointer<CState>{fg_Explicit(this)}`.
static bool ownsThis(const Expr *Init, int Depth = 0) {
  const Expr *E = skipWrappers(Init);
  // A copy of a local that owns it, `pKeep = pThis`, also moved, owns it too.
  for (int Level = 0; Level < 8; ++Level)
    if (const auto *Copy = dyn_cast<CXXConstructExpr>(E);
        Copy && Copy->getNumArgs() == 1 &&
        Copy->getConstructor()->isCopyOrMoveConstructor())
      E = skipWrappers(Copy->getArg(0));
    else if (const auto *Move = dyn_cast<CallExpr>(E);
             Move && Move->getNumArgs() == 1 &&
             isMoveFunction(calleeName(Move)))
      E = skipWrappers(Move->getArg(0));
    else
      break;
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts())) {
    const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
    return Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
           !Local->getType()->isReferenceType() && Local->getInit() &&
           Depth < 8 && isUnchangedLocal(Local) &&
           ownsThis(Local->getInit(), Depth + 1);
  }
  const Expr *Argument = nullptr;
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(E);
      Construct && Construct->getNumArgs() == 1)
    Argument = Construct->getArg(0);
  else if (const auto *List = dyn_cast<InitListExpr>(E);
           List && List->getNumInits() == 1)
    Argument = List->getInit(0);
  else if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(E))
    Argument = Cast->getSubExpr();
  // A variable initialized directly, `TCSharedPointer<C> pThis{...}`, whose
  // constructor the wrappers skip.
  else if (isa<CallExpr>(E))
    Argument = E;
  if (!Argument)
    return false;
  Argument = skipWrappers(Argument);
  if (const auto *List = dyn_cast<InitListExpr>(Argument);
      List && List->getNumInits() == 1)
    Argument = skipWrappers(List->getInit(0));
  if (const CallExpr *Explicit = asCallTo(Argument, {"fg_Explicit"});
      Explicit && Explicit->getNumArgs() == 1)
    Argument = Explicit->getArg(0);
  // Not one that can be null, `_bKeep ? this : nullptr`, which owns nothing
  // then.
  std::function<bool(const Expr *, int)> MayBeNull = [&](const Expr *Value,
                                                         int Level) {
    Value = Value->IgnoreParenImpCasts();
    if (isa<CXXNullPtrLiteralExpr, GNUNullExpr>(Value))
      return true;
    if (const auto *Zero = dyn_cast<IntegerLiteral>(Value))
      return Zero->getValue().isZero();
    if (const auto *Conditional = dyn_cast<ConditionalOperator>(Value);
        Conditional && Level < 8)
      return MayBeNull(Conditional->getTrueExpr(), Level + 1) ||
             MayBeNull(Conditional->getFalseExpr(), Level + 1);
    return false;
  };
  return !MayBeNull(Argument, 0) && refersToThis(Argument, 0) &&
         !isa<UnaryOperator>(Argument->IgnoreParenImpCasts());
}

/// Whether the init capture \p Variable of the lambda whose call operator is
/// \p Operator keeps what it owns: its closure is const, unless the const is
/// cast away, `const_cast<TCSharedPointer<CState> &>(pThis) = nullptr`, or
/// its body only reads it, so it is not reset, assigned, moved or swapped.
static bool isKeptIntact(const VarDecl *Variable,
                         const CXXMethodDecl *Operator) {
  const Stmt *Body = Operator->getBody();
  if (!Body)
    return false;
  // In a mutable closure, every use of it must only read it: go through it
  // with `->` or `*`, call a const member function of it, or copy it.
  using namespace ast_matchers;
  ASTContext &Context = Operator->getASTContext();
  const CParents Parents(Body);
  // Also a local reference to it, `auto const &Alias = pThis`, through which
  // it can be reset as well; with more than can be looked through, whether
  // one of them resets it can't be told.
  llvm::SmallVector<const VarDecl *, 4> Watched{Variable};
  for (size_t iWatched = 0; iWatched < Watched.size(); ++iWatched)
    for (const BoundNodes &Nodes :
         match(findAll(declRefExpr(to(varDecl(equalsNode(Watched[iWatched]))))
                           .bind("ref")),
               *Body, Context)) {
      const auto *Ref = Nodes.getNodeAs<DeclRefExpr>("ref");
      // A lambda inside that captures a copy changes its own copy.
      bool OfCopy = false;
      const Stmt *Child = nullptr;
      for (const Stmt *Node = Ref; Node && !OfCopy;
           Child = Node, Node = Parents.getParent(Node)) {
        if (const auto *Lambda = dyn_cast<LambdaExpr>(Node);
            Lambda && Child && Child == Lambda->getBody())
          OfCopy = capturesCopy(Lambda, Variable);
      }
      if (OfCopy)
        continue;
      // Also as either of a conditional, `auto &Alias = bChoose ? pThis :
      // pThis`.
      const Stmt *User = Parents.getParent(Ref);
      const Stmt *Used = Ref;
      while ((isa_and_nonnull<ParenExpr, ImplicitCastExpr>(User) &&
              !(isa<ImplicitCastExpr>(User) &&
                cast<ImplicitCastExpr>(User)->getCastKind() ==
                    CK_LValueToRValue)) ||
             (isa_and_nonnull<ConditionalOperator>(User) &&
              cast<ConditionalOperator>(User)->isGLValue() &&
              cast<ConditionalOperator>(User)->getCond() != Used)) {
        Used = User;
        User = Parents.getParent(User);
      }
      if (const auto *Declaration = dyn_cast_or_null<DeclStmt>(User)) {
        for (const Decl *Declared : Declaration->decls())
          if (const auto *Alias = dyn_cast<VarDecl>(Declared);
              Alias && Alias->getType()->isReferenceType() &&
              Alias->getInit() &&
              Alias->getInit()->getSourceRange().fullyContains(
                  Ref->getSourceRange()) &&
              !llvm::is_contained(Watched, Alias))
            Watched.push_back(Alias);
        if (Watched.size() > 64)
          return false;
        continue;
      }
      // Its address kept in a local pointer, `auto *pAlias = &pThis`, through
      // which a cast can reset it, is watched like it; given elsewhere, what
      // is done with it can't be told.
      if (const auto *Address = dyn_cast_or_null<UnaryOperator>(User);
          Address && Address->getOpcode() == UO_AddrOf) {
        const Stmt *Holder = Parents.getParent(Address);
        while (isa_and_nonnull<ParenExpr, ImplicitCastExpr>(Holder))
          Holder = Parents.getParent(Holder);
        const auto *Declaration = dyn_cast_or_null<DeclStmt>(Holder);
        const auto *Alias =
            Declaration && Declaration->isSingleDecl()
                ? dyn_cast<VarDecl>(Declaration->getSingleDecl())
                : nullptr;
        if (!Alias || !Alias->getType()->isPointerType())
          return false;
        if (!llvm::is_contained(Watched, Alias))
          Watched.push_back(Alias);
        if (Watched.size() > 64)
          return false;
        continue;
      }
      if (Operator->isConst()) {
        // Also a cast that casts the const away around another,
        // `const_cast<...>(static_cast<...>(pThis))`, also of a pointer to it
        // read, `const_cast<...>(pAlias)`.
        const Stmt *First = User;
        while (isa_and_nonnull<ParenExpr, ImplicitCastExpr>(First))
          First = Parents.getParent(First);
        for (const Stmt *Cast = First;
             isa_and_nonnull<ExplicitCastExpr>(Cast);) {
          if (isa<CXXConstCastExpr, CStyleCastExpr>(Cast))
            return false;
          Cast = Parents.getParent(Cast);
          while (isa_and_nonnull<ParenExpr, ImplicitCastExpr>(Cast))
            Cast = Parents.getParent(Cast);
        }
        // Or given to a function whose body can be seen to cast it so,
        // `void fg_Release(TCSharedPointer<CState> const &_pState) {
        // const_cast<...>(_pState) = {}; }`, also through those it gives it
        // to.
        if (const auto *Call = dyn_cast_or_null<CallExpr>(First))
          for (unsigned iArgument = calledFunctionOf(Call).second;
               iArgument < Call->getNumArgs(); ++iArgument) {
            const Expr *Argument = Call->getArg(iArgument);
            while (const auto *Wrapper = dyn_cast<ImplicitCastExpr>(Argument))
              Argument = Wrapper->getSubExpr();
            if (Argument->IgnoreParens() !=
                dyn_cast<Expr>(Used)->IgnoreParens())
              continue;
            // An override that can run instead may.
            if (callsOverride(Call) ||
                castsConstAway(calledFunctionOf(Call).first,
                               iArgument - calledFunctionOf(Call).second))
              return false;
          }
        continue;
      }
      bool Reads = false;
      if (const auto *Call = dyn_cast_or_null<CXXOperatorCallExpr>(User))
        Reads = (Call->getOperator() == OO_Arrow ||
                 Call->getOperator() == OO_Star) &&
                Call->getNumArgs() == 1;
      else if (const auto *Member = dyn_cast_or_null<MemberExpr>(User)) {
        const auto *Method = dyn_cast<CXXMethodDecl>(Member->getMemberDecl());
        Reads = !Member->isArrow() && Method && Method->isConst();
      } else if (const auto *Construct =
                     dyn_cast_or_null<CXXConstructExpr>(User))
        Reads = Construct->getConstructor() &&
                Construct->getConstructor()->isCopyConstructor();
      if (!Reads)
        return false;
    }
  return true;
}

bool keepsObjectAlive(const LambdaExpr *Lambda) {
  for (const LambdaCapture &Capture : Lambda->captures()) {
    if (!Capture.capturesVariable() || Capture.getCaptureKind() != LCK_ByCopy)
      continue;
    const auto *Variable = dyn_cast<VarDecl>(Capture.getCapturedVar());
    if (!Variable || !Variable->getInit())
      continue;
    // Also a copy of an unchanged local, like `[this, pThis]` with
    // `TCSharedPointer<CState> pThis{fg_Explicit(this)}`.
    if (!Variable->isInitCapture() &&
        (!Variable->hasLocalStorage() || isa<ParmVarDecl>(Variable) ||
         isChangedVariable(Variable)))
      continue;
    const StringRef Name = recordName(Variable->getType());
    if ((Name == "TCSharedPointer" || Name == "shared_ptr") &&
        ownsThis(Variable->getInit()) &&
        isKeptIntact(Variable, Lambda->getCallOperator()))
      return true;
  }
  return false;
}

/// Whether \p Type is the closure type \p Closure, or a reference to it.
static bool isClosureOf(QualType Type, const CXXRecordDecl *Closure) {
  const CXXRecordDecl *Record =
      Type.getNonReferenceType()->getAsCXXRecordDecl();
  return Record && Record->getCanonicalDecl() == Closure->getCanonicalDecl();
}

/// The name of what \p Call calls: a function or a member function, or the
/// class of the object whose operator it calls, like `operator ()` or the
/// `operator %` of a `CTestCategoryScope`.
static StringRef callTargetName(const CallExpr *Call) {
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call)) {
    if (Operator->getNumArgs() >= 1)
      return recordName(Operator->getArg(0)->getType());
    return {};
  }
  if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call)) {
    const CXXMethodDecl *Method = Member->getMethodDecl();
    return Method && Method->getIdentifier() ? Method->getName() : StringRef();
  }
  return calleeName(Call);
}

/// Whether \p Operand is \p Child, which the parent map, depending on the
/// traversal kind, reaches past implicit nodes.
static bool isOperand(const Expr *Operand, const Expr *Child) {
  return Operand && Child &&
         (Operand == Child || Operand->IgnoreUnlessSpelledInSource() ==
                                  Child->IgnoreUnlessSpelledInSource());
}

/// Whether \p Parent only passes the closure \p Closure on: parentheses,
/// implicit nodes, and copies and moves of it.
static bool passesClosureOn(const Expr *Parent, const CXXRecordDecl *Closure) {
  // `(Other, Closure)` gives the closure.
  if (const auto *Binary = dyn_cast<BinaryOperator>(Parent);
      Binary && Binary->getOpcode() == BO_Comma &&
      isClosureOf(Binary->getType(), Closure))
    return true;
  if (isa<ParenExpr, ImplicitCastExpr, MaterializeTemporaryExpr,
          CXXBindTemporaryExpr, ExprWithCleanups>(Parent))
    return true;
  if (isa<CXXConstructExpr, CXXFunctionalCastExpr, ExplicitCastExpr,
          AbstractConditionalOperator>(Parent) &&
      isClosureOf(Parent->getType(), Closure))
    return true;
  const auto *Call = dyn_cast<CallExpr>(Parent);
  if (!Call || isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Call) ||
      Call->getNumArgs() != 1)
    return false;
  const StringRef Name = calleeName(Call);
  return isMoveFunction(Name) || isForwardFunction(Name);
}

static const Expr *dispatchedFunction(const CallExpr *Call);
static bool isDispatchedFunction(const CallExpr *Call, StringRef Name,
                                 const Expr *Child);

/// Whether \p E copies or moves an object into another that owns the same,
/// like a `TCSharedPointer` from another, as opposed to an object made from
/// it, which may only point to it.
static bool isOwningCopy(const Expr *E) {
  const auto *Construct = dyn_cast<CXXConstructExpr>(E);
  if (!Construct || Construct->getNumArgs() < 1)
    return false;
  if (Construct->getConstructor()->isCopyOrMoveConstructor())
    return true;
  // A conversion between pointers of the same template, like to a const
  // pointee.
  const StringRef Name = recordName(Construct->getType());
  return Name == "TCSharedPointer" &&
         recordName(Construct->getArg(0)->getType()) == Name;
}

/// Whether \p E is the function given to fg_CallSafe, also through
/// parentheses, implicit nodes and `fg_Move` or `fg_Forward`.
static bool isCallSafeFunction(const Expr *E, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*E);
  for (int Depth = 0; Depth < 8; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    const auto *Parent = Parents.empty() ? nullptr : Parents[0].get<Expr>();
    if (!Parent)
      return false;
    if (isa<ParenExpr, ImplicitCastExpr, MaterializeTemporaryExpr,
            CXXBindTemporaryExpr>(Parent) ||
        isOwningCopy(Parent)) {
      Node = Parents[0];
      continue;
    }
    // Either value of a conditional, `bCondition ? pClosure : pOther`.
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Parent);
        Conditional &&
        (isOperand(Conditional->getTrueExpr(), Node.get<Expr>()) ||
         isOperand(Conditional->getFalseExpr(), Node.get<Expr>()))) {
      Node = Parents[0];
      continue;
    }
    const auto *Call = dyn_cast<CallExpr>(Parent);
    if (!Call || isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Call))
      return false;
    const StringRef Name = calleeName(Call);
    if (Call->getNumArgs() == 1 &&
        (isMoveFunction(Name) || isForwardFunction(Name))) {
      Node = Parents[0];
      continue;
    }
    return Name == "fg_CallSafe" && Call->getNumArgs() >= 1 &&
           isOperand(Call->getArg(0), Node.get<Expr>());
  }
  return false;
}

/// The local that \p E initializes, past parentheses, implicit nodes, copies
/// and `fg_Move`.
static const VarDecl *initializedLocal(const Expr *E, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*E);
  for (int Depth = 0; Depth < 8; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    if (const auto *Variable = Parents[0].get<VarDecl>())
      return Variable->hasLocalStorage() ? Variable : nullptr;
    const auto *Parent = Parents[0].get<Expr>();
    if (!Parent)
      return nullptr;
    const auto *Call = dyn_cast<CallExpr>(Parent);
    // Also as either value of a conditional, `bChoose ? pWork : pCopy`, not
    // its condition.
    const auto *Conditional = dyn_cast<ConditionalOperator>(Parent);
    if (!isa<ParenExpr, ImplicitCastExpr, MaterializeTemporaryExpr,
             CXXBindTemporaryExpr, ExprWithCleanups>(Parent) &&
        !(Conditional && Node.get<Expr>() != Conditional->getCond()) &&
        !isOwningCopy(Parent) &&
        !(Call && Call->getNumArgs() == 1 &&
          (isMoveFunction(calleeName(Call)))))
      return nullptr;
    Node = Parents[0];
  }
  return nullptr;
}

/// Whether the local shared pointer \p Variable is only given to fg_CallSafe,
/// also through copies and moves into other locals, each made from one use of
/// the one before, so a chain of them, each told once, also one that both
/// values of a conditional make, `auto p1 = bMove ? fg_Move(p0) : p0;`.
static bool onlyLocalCalledSafely(const VarDecl *Variable, ASTContext &Context,
                                  int Depth) {
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  if (Depth > 64 || !Function || !Function->getBody())
    return false;
  struct CCalledSafely {
    llvm::DenseMap<const VarDecl *, bool> Decided;
  };
  auto &Decided = astCache<CCalledSafely>(Context).Decided;
  if (auto It = Decided.find(Variable); It != Decided.end())
    return It->second;
  // Taken not to be while it is told, which one made from itself can't be.
  Decided[Variable] = false;
  using namespace ast_matchers;
  const auto Refs =
      match(findAll(declRefExpr(to(varDecl(equalsNode(Variable))),
                                unless(isInUnevaluatedContext()))
                        .bind("ref")),
            *Function->getBody(), Context);
  const bool Safe = llvm::all_of(Refs, [&](const BoundNodes &Nodes) {
    const auto *Ref = Nodes.getNodeAs<DeclRefExpr>("ref");
    if (Ref->refersToEnclosingVariableOrCapture())
      return false;
    if (isCallSafeFunction(Ref, Context))
      return true;
    const VarDecl *Copy = initializedLocal(Ref, Context);
    return Copy && onlyLocalCalledSafely(Copy, Context, Depth + 1);
  });
  Decided[Variable] = Safe;
  return Safe;
}

/// Whether \p MakePointer, which makes a shared pointer to a closure, is only
/// given to fg_CallSafe: directly, or through locals that are.
static bool onlyCalledSafely(const CallExpr *MakePointer, ASTContext &Context) {
  if (isCallSafeFunction(MakePointer, Context))
    return true;
  const VarDecl *Variable = initializedLocal(MakePointer, Context);
  return Variable && onlyLocalCalledSafely(Variable, Context, 0);
}

static void addClosureConsumers(const Expr *E, const CXXRecordDecl *Closure,
                                ASTContext &Context,
                                llvm::ArrayRef<StringRef> SafeCallees,
                                llvm::SmallVectorImpl<ClosureConsumer> &Consumers,
                                int Depth) {
  auto Add = [&](ClosureUse Use, const Expr *Site, std::string Target = {}) {
    Consumers.push_back({Use, Site, std::move(Target)});
  };
  auto Quoted = [](StringRef Name) { return ("'" + Name + "'").str(); };
  DynTypedNode Node = DynTypedNode::create(*E);
  for (;;) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return;
    const DynTypedNode &Parent = Parents[0];
    const auto *Child = Node.get<Expr>();
    if (const auto *Variable = Parent.get<VarDecl>()) {
      if (Variable->isInitCapture()) {
        Add(ClosureUse::Escape, Child, "a lambda that captures it");
        return;
      }
      // A local that holds the closure is used where it is referred to.
      const auto *Function =
          dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
      // In a template, the type of `auto fLambda = [...]` can be left
      // undeduced.
      const LambdaExpr *Initializer =
          Variable->getInit() ? asLambda(Variable->getInit()) : nullptr;
      // A local function wrapper made from it, like `TCFunctionMovable<…>
      // fLambda = [...]`, holds it too, and an unchanged local reference to
      // one, `auto &fAlias = fLambda`, which asLambda follows, refers to it.
      const StringRef VariableTypeName = recordName(Variable->getType());
      const bool HoldsClosure =
          isClosureOf(Variable->getType(), Closure) ||
          ((Variable->getType()->getContainedAutoType() ||
            VariableTypeName.starts_with("TCFunction") ||
            VariableTypeName == "function") &&
           Initializer && Initializer->getLambdaClass() == Closure);
      // A variable of another type, like a `TCFunction`, is made from it.
      if (!HoldsClosure) {
        const StringRef Name = recordName(Variable->getType());
        if (!Name.empty() && llvm::is_contained(SafeCallees, Name))
          Add(ClosureUse::SafeCallee, Child);
        else
          Add(ClosureUse::Escape, Child,
              Name.empty() ? Quoted(Variable->getName()) : "a " + Quoted(Name));
        return;
      }
      // Through as many local copies as the code has, `auto fCopy = fWork;`,
      // each looked at once.
      static thread_local llvm::SmallPtrSet<const VarDecl *, 8> s_Copies;
      if (Depth == 0)
        s_Copies.clear();
      if (!Variable->hasLocalStorage() || !Function || !Function->getBody() ||
          Depth >= 64) {
        Add(ClosureUse::Escape, Child, Quoted(Variable->getName()));
        return;
      }
      if (!s_Copies.insert(Variable).second)
        return;
      // The outermost lambda in the function that \p Ref is in, or captures
      // it with.
      auto CapturingLambda = [&](const DeclRefExpr *Ref) -> const LambdaExpr * {
        const LambdaExpr *Outermost = nullptr;
        DynTypedNode Node = DynTypedNode::create(*Ref);
        for (int Up = 0; Up < 256; ++Up) {
          const auto Parents = instantiatedParents(Node, Context);
          if (Parents.empty())
            break;
          Node = Parents[0];
          // The function the local is in, also the body of a lambda, ends the
          // walk.
          if (Node.get<Stmt>() == Function->getBody())
            break;
          if (const auto *Lambda = Node.get<LambdaExpr>()) {
            if (Lambda->getCallOperator() == Function)
              break;
            Outermost = Lambda;
          }
          if (Node.get<FunctionDecl>())
            break;
        }
        return Outermost;
      };
      llvm::SmallPtrSet<const LambdaExpr *, 4> CapturedBy;
      // The references to each local of the function, found once for it,
      // however many copies are followed, not in what isn't evaluated.
      struct CReferences {
        llvm::DenseMap<
            const Stmt *,
            llvm::DenseMap<const ValueDecl *,
                           llvm::SmallVector<const DeclRefExpr *, 2>>>
            Bodies;
      };
      auto [Index, Inserted] =
          astCache<CReferences>(Context).Bodies.try_emplace(
              Function->getBody());
      if (Inserted) {
        auto &References = Index->second;
        std::function<void(const Stmt *)> Collect = [&](const Stmt *S) {
          if (!S || isUnevaluatedNode(S))
            return;
          if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
            References[Ref->getDecl()].push_back(Ref);
          for (const Stmt *Child : childrenOnce(S))
            Collect(Child);
        };
        Collect(Function->getBody());
      }
      const llvm::SmallVector<const DeclRefExpr *, 2> Refs =
          astCache<CReferences>(Context).Bodies[Function->getBody()].lookup(
              Variable);
      for (const DeclRefExpr *Ref : Refs) {
        // A lambda that uses or captures the local can run later; it is
        // reported once.
        if (const LambdaExpr *Capturing = CapturingLambda(Ref)) {
          if (CapturedBy.insert(Capturing).second)
            Add(ClosureUse::Escape, Capturing, "a lambda that captures it");
          continue;
        }
        addClosureConsumers(Ref, Closure, Context, SafeCallees, Consumers,
                            Depth + 1);
      }
      return;
    }
    if (Parent.get<FieldDecl>()) {
      Add(ClosureUse::Escape, Child, "a member");
      return;
    }
    const auto *ParentExpr = Parent.get<Expr>();
    if (!ParentExpr) {
      if (Parent.get<ReturnStmt>() || Parent.get<CoreturnStmt>())
        Add(ClosureUse::Escape, Child, "the caller");
      // The result of a statement expression, `({ ...; Lambda; })`, leaves the
      // scope of what it captures.
      else if (const auto *Compound = Parent.get<CompoundStmt>();
               Compound && !Compound->body_empty() &&
               isa<Expr>(Compound->body_back()) &&
               isOperand(cast<Expr>(Compound->body_back()), Child)) {
        const auto Outer = instantiatedParents(Parent, Context);
        if (!Outer.empty() && Outer[0].get<StmtExpr>())
          Add(ClosureUse::Escape, Child, "a statement expression");
      }
      // A lambda that is made and dropped, or one in a type, is not used.
      return;
    }
    if (passesClosureOn(ParentExpr, Closure)) {
      Node = Parent;
      continue;
    }
    if (const auto *Call = dyn_cast<CallExpr>(ParentExpr)) {
      if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call)) {
        if (Operator->getOperator() == OO_Call && Operator->getNumArgs() >= 1 &&
            isOperand(Operator->getArg(0), Child)) {
          Add(ClosureUse::DirectCall, Operator);
          return;
        }
        // `g_Dispatch / Lambda`, `self / Lambda`, `g_ActorFunctor / Lambda`,
        // and `Future > Lambda`, which continues the future with it, but not
        // a scope exit, `g_OnScopeExit / Lambda`, which calls it and then
        // destroys it, unlike `g_OnScopeExitActor`, which dispatches it.
        if ((Operator->getOperator() == OO_Slash ||
             Operator->getOperator() == OO_Greater) &&
            Operator->getNumArgs() == 2 && isOperand(Operator->getArg(1), Child)) {
          if (isScopeExitHelper(Operator->getArg(0), /*AlsoActor=*/false))
            Add(ClosureUse::Escape, Operator, "a scope exit");
          else
            Add(ClosureUse::SafeCallee, Operator);
          return;
        }
        // Assigning over what holds it destroys it without calling it.
        if (Operator->getOperator() == OO_Equal &&
            Operator->getNumArgs() == 2 && isOperand(Operator->getArg(0), Child))
          return;
        // An assignment to another object, like a `TCFunction`.
        if (Operator->getOperator() == OO_Equal &&
            Operator->getNumArgs() == 2 && isOperand(Operator->getArg(1), Child)) {
          const StringRef Name = recordName(Operator->getArg(0)->getType());
          Add(ClosureUse::Escape, Operator,
              Name.empty() ? "another object" : "a " + Quoted(Name));
          return;
        }
      }
      // The same operators called explicitly, `g_Dispatch.operator / (Lambda)`.
      if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call)) {
        const CXXMethodDecl *Method = Member->getMethodDecl();
        if (Method && Method->isOverloadedOperator() &&
            (Method->getOverloadedOperator() == OO_Slash ||
             Method->getOverloadedOperator() == OO_Greater) &&
            Call->getNumArgs() == 1 && isOperand(Call->getArg(0), Child)) {
          if (isScopeExitHelper(Member->getImplicitObjectArgument(),
                                /*AlsoActor=*/false))
            Add(ClosureUse::Escape, Call, "a scope exit");
          else
            Add(ClosureUse::SafeCallee, Call);
          return;
        }
      }
      // A non-member operator called explicitly, `operator / (g_Dispatch,
      // Lambda)`.
      if (const FunctionDecl *Callee = Call->getDirectCallee();
          Callee && !isa<CXXOperatorCallExpr>(Call) &&
          (Callee->getOverloadedOperator() == OO_Slash ||
           Callee->getOverloadedOperator() == OO_Greater) &&
          Call->getNumArgs() == 2 && isOperand(Call->getArg(1), Child)) {
        if (isScopeExitHelper(Call->getArg(0), /*AlsoActor=*/false))
          Add(ClosureUse::Escape, Call, "a scope exit");
        else
          Add(ClosureUse::SafeCallee, Call);
        return;
      }
      // An explicit call of a call operator with an explicit object,
      // `Lambda.operator()()`, which has the closure as its first argument.
      if (const auto *Method =
              dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
          Method && Method->isExplicitObjectMemberFunction() &&
          Method->getOverloadedOperator() == OO_Call &&
          Call->getNumArgs() >= 1 && isOperand(Call->getArg(0), Child)) {
        Add(ClosureUse::DirectCall, Call);
        return;
      }
      const StringRef Name = callTargetName(Call);
      // A shared pointer to the closure, `fg_ToSharedPointer(fLambda)`, in a
      // local that is only given to fg_CallSafe, which keeps it alive while it
      // calls it.
      if (Name == "fg_ToSharedPointer" && Call->getNumArgs() == 1 &&
          isOperand(Call->getArg(0), Child)) {
        if (onlyCalledSafely(Call, Context))
          Add(ClosureUse::CallSafe, Call);
        else
          Add(ClosureUse::Escape, Call, Quoted(Name));
        return;
      }
      // A dispatch keeps the function it dispatches alive while it runs, but
      // the other arguments are given to that function.
      if ((Name == "fg_Dispatch" || Name == "fg_DirectDispatch" ||
           Name == "fg_ConcurrentDispatch") &&
          !isOperand(dispatchedFunction(Call), Child)) {
        Add(ClosureUse::Escape, Call, Quoted(Name));
        return;
      }
      // `f_Bind` keeps the function alive while it runs only when it binds
      // `f_DispatchWithReturn` and gets it first; other bound functions get
      // it as an argument, and can drop it.
      if ((Name == "f_Bind" || Name == "f_BindByValue") &&
          !isDispatchedFunction(Call, Name, Child)) {
        Add(ClosureUse::Escape, Call, Quoted(Name));
        return;
      }
      // An actor call, `Actor(&CActor::f_Function, Lambda)`, gives the closure
      // to the function it calls: a coroutine keeps it in its frame, and
      // `CActor::f_DispatchWithReturn` runs it, but another function can drop
      // it while its coroutine is suspended.
      if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
          Operator && Operator->getOperator() == OO_Call &&
          Operator->getNumArgs() >= 3 &&
          (Name == "TCActor" || Name == "TCWeakActor" ||
           Name == "TCDistributedActor") &&
          !isOperand(Operator->getArg(1), Child)) {
        const Expr *Target = Operator->getArg(1)->IgnoreParenImpCasts();
        if (const auto *Unary = dyn_cast<UnaryOperator>(Target))
          Target = Unary->getSubExpr()->IgnoreParenImpCasts();
        const auto *Ref = dyn_cast<DeclRefExpr>(Target);
        const auto *Method =
            Ref ? dyn_cast<CXXMethodDecl>(Ref->getDecl()) : nullptr;
        const bool Dispatch =
            Method && Method->getIdentifier() &&
            Method->getName() == "f_DispatchWithReturn" &&
            Method->getParent()->getIdentifier() &&
            Method->getParent()->getName() == "CActor" &&
            isOperand(Operator->getArg(2), Child);
        const FunctionDecl *Definition =
            Method ? Method->getDefinition() : nullptr;
        // The parameter it is given as: one that holds the closure itself,
        // like a `t_FCallback`, as opposed to a function or functor that
        // the closure is moved into, which the actor calls safely.
        unsigned iArgument = 2;
        while (iArgument < Operator->getNumArgs() &&
               !isOperand(Operator->getArg(iArgument), Child))
          ++iArgument;
        const ParmVarDecl *Parameter =
            Method && iArgument - 2 < Method->getNumParams()
                ? Method->getParamDecl(iArgument - 2)
                : nullptr;
        const QualType ParameterType =
            Parameter ? Parameter->getType().getNonReferenceType() : QualType();
        const bool HoldsClosure =
            !Parameter || isClosureOf(ParameterType, Closure);
        if (HoldsClosure && !Dispatch &&
            !(Definition && isCoroutine(Definition))) {
          Add(ClosureUse::Escape, Call, Quoted(Name));
          return;
        }
      }
      // `f_Invoke` and `f_DispatchWithReturn` keep the function they run
      // alive, which is their first argument; the others are given to it.
      if ((Name == "f_Invoke" || Name == "f_DispatchWithReturn") &&
          (Call->getNumArgs() < 1 || !isOperand(Call->getArg(0), Child))) {
        Add(ClosureUse::Escape, Call, Quoted(Name));
        return;
      }
      // fg_CallSafe calls its first argument right away, and passes the
      // others on to it.
      if (Name == "fg_CallSafe" && Call->getNumArgs() >= 1 &&
          isOperand(Call->getArg(0), Child))
        Add(ClosureUse::CallSafe, Call);
      else if (Name == "fg_CallSafe")
        Add(ClosureUse::Escape, Call, Quoted(Name));
      else if (!Name.empty() && llvm::is_contained(SafeCallees, Name))
        Add(ClosureUse::SafeCallee, Call);
      else
        Add(ClosureUse::Escape, Call, Name.empty() ? "a call" : Quoted(Name));
      return;
    }
    // An explicit call of the call operator, `fLambda.operator ()()`.
    if (const auto *Member = dyn_cast<MemberExpr>(ParentExpr);
        Member && Member->getMemberDecl()->getDeclName().getNameKind() ==
                      DeclarationName::CXXOperatorName &&
        Member->getMemberDecl()->getDeclName().getCXXOverloadedOperator() ==
            OO_Call) {
      Add(ClosureUse::DirectCall, Member);
      return;
    }
    // A local initialized with it in braces without a type,
    // `auto fLambda{[...] {...}}`, holds it.
    if (isa<InitListExpr>(ParentExpr) && asLambda(ParentExpr) &&
        instantiatedParents(Parent, Context).size() == 1 &&
        instantiatedParents(Parent, Context)[0].get<VarDecl>()) {
      Node = Parent;
      continue;
    }
    // Another object made from the closure, like a `TCFunction`.
    if (isa<CXXConstructExpr, InitListExpr, CXXFunctionalCastExpr>(
            ParentExpr)) {
      const StringRef Name = recordName(ParentExpr->getType());
      // A function wrapper that holds only the closure, like
      // `TCFunctionMovable<…>(Lambda)` or the conversion to a parameter of
      // that type, keeps it as the closure itself, so where the wrapper goes
      // tells.
      auto OnlyArgument = [&]() -> const Expr * {
        if (const auto *Construct = dyn_cast<CXXConstructExpr>(ParentExpr))
          return Construct->getNumArgs() == 1 ? Construct->getArg(0) : nullptr;
        if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(ParentExpr))
          return Cast->getSubExpr();
        if (const auto *List = dyn_cast<InitListExpr>(ParentExpr))
          return List->getNumInits() == 1 ? List->getInit(0) : nullptr;
        return nullptr;
      };
      if ((Name.starts_with("TCFunction") || Name == "function") &&
          isOperand(OnlyArgument(), Child)) {
        Node = Parent;
        continue;
      }
      if (!Name.empty() && llvm::is_contained(SafeCallees, Name))
        Add(ClosureUse::SafeCallee, ParentExpr);
      else
        Add(ClosureUse::Escape, ParentExpr,
            Name.empty() ? "another object" : "a " + Quoted(Name));
      return;
    }
    if (isa<LambdaExpr>(ParentExpr)) {
      Add(ClosureUse::Escape, Child, "a lambda that captures it");
      return;
    }
    // `(void)fLambda` discards it.
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(ParentExpr);
        Cast && Cast->getType()->isVoidType())
      return;
    // An assignment to another object, like a `TCFunction`.
    if (const auto *Binary = dyn_cast<BinaryOperator>(ParentExpr);
        Binary && Binary->getOpcode() == BO_Assign) {
      const StringRef Name = recordName(Binary->getLHS()->getType());
      Add(ClosureUse::Escape, Binary,
          Name.empty() ? "another object" : "a " + Quoted(Name));
      return;
    }
    Add(ClosureUse::Escape, ParentExpr, "an expression");
    return;
  }
}

llvm::SmallVector<ClosureConsumer, 2>
closureConsumers(const LambdaExpr *Lambda, ASTContext &Context,
                 llvm::ArrayRef<StringRef> SafeCallees) {
  llvm::SmallVector<ClosureConsumer, 2> Consumers;
  addClosureConsumers(Lambda, Lambda->getLambdaClass(), Context, SafeCallees,
                      Consumers, 0);
  return Consumers;
}

void giveUnlistedCallSafe(llvm::SmallVectorImpl<ClosureConsumer> &Consumers,
                          llvm::ArrayRef<StringRef> SafeCallees) {
  if (llvm::is_contained(SafeCallees, "fg_CallSafe"))
    return;
  for (ClosureConsumer &Consumer : Consumers)
    if (Consumer.Use == ClosureUse::CallSafe) {
      Consumer.Use = ClosureUse::Escape;
      Consumer.Target = "'fg_CallSafe'";
    }
}

bool isCalledInScope(llvm::ArrayRef<ClosureConsumer> Consumers) {
  return llvm::all_of(Consumers, [](const ClosureConsumer &Consumer) {
    return Consumer.Use == ClosureUse::DirectCall ||
           Consumer.Use == ClosureUse::CallSafe;
  });
}

/// The lambda expression whose call operator is \p Method.
const LambdaExpr *lambdaOfCallOperator(const CXXMethodDecl *Method,
                                              ASTContext &Context) {
  // The call operator of a generic lambda is instantiated from the one the
  // lambda expression has.
  if (const FunctionDecl *Pattern = Method->getTemplateInstantiationPattern())
    Method = cast<CXXMethodDecl>(Pattern);
  const Stmt *Body = Method->getBody();
  if (!Body)
    return nullptr;
  for (const DynTypedNode &Parent : instantiatedParents(*Body, Context))
    if (const auto *Lambda = Parent.get<LambdaExpr>();
        Lambda && Lambda->getLambdaClass() == Method->getParent())
      return Lambda;
  // The body of a generic lambda's call operator template isn't always mapped
  // to the lambda expression, which the function around it has.
  const auto *Around =
      dyn_cast_or_null<FunctionDecl>(Method->getParent()->getDeclContext());
  if (!Around || !Around->getBody())
    return nullptr;
  using namespace ast_matchers;
  for (const BoundNodes &Nodes :
       match(findAll(lambdaExpr().bind("lambda")), *Around->getBody(),
             Context)) {
    const auto *Lambda = Nodes.getNodeAs<LambdaExpr>("lambda");
    if (Lambda->getLambdaClass() == Method->getParent())
      return Lambda;
  }
  return nullptr;
}

/// Whether \p Lambda captures \p Variable by copy, or `*this` when
/// \p Variable is null.
static bool capturesCopy(const LambdaExpr *Lambda, const ValueDecl *Variable) {
  return llvm::any_of(Lambda->captures(), [&](const LambdaCapture &Capture) {
    if (!Variable)
      return Capture.capturesThis() && Capture.getCaptureKind() == LCK_StarThis;
    return Capture.capturesVariable() && Capture.getCapturedVar() == Variable &&
           Capture.getCaptureKind() == LCK_ByCopy;
  });
}

/// Whether \p Lambda is given only to what runs it to completion before it
/// returns, like the `operator %` of a test category, which dispatches it and
/// waits for it.
static bool isRunByBlockingCallees(const LambdaExpr *Lambda,
                                   ASTContext &Context) {
  const llvm::ArrayRef<StringRef> BlockingCallees =
      vocabulary().BlockingCallees.names();
  const auto Consumers = closureConsumers(Lambda, Context, BlockingCallees);
  return !Consumers.empty() &&
         llvm::all_of(Consumers, [&](const ClosureConsumer &Consumer) {
           const auto *Call = dyn_cast_or_null<CallExpr>(Consumer.Site);
           return Consumer.Use == ClosureUse::SafeCallee && Call &&
                  llvm::is_contained(BlockingCallees, callTargetName(Call));
         });
}

/// Whether \p Variable, or the object of `this` when it is null, lives while
/// the function that makes \p Lambda runs.
static bool isAliveWhileRunning(const ValueDecl *Variable,
                                const LambdaExpr *Lambda, ASTContext &Context) {
  const DeclContext *Home =
      Variable ? Variable->getParentFunctionOrMethod() : nullptr;
  const FunctionDecl *Function = enclosingFunction(Lambda);
  // The lambda the walk is at, made in Function.
  const LambdaExpr *Current = Lambda;
  for (int Depth = 0; Function && Depth < 16; ++Depth) {
    const auto *Method = dyn_cast<CXXMethodDecl>(Function);
    const bool InLambda = Method && Method->getParent()->isLambda();
    // The function that has the variable, or the member function whose object
    // it is, runs while the lambda is made.
    if (Variable ? Function == Home : !InLambda)
      return true;
    const LambdaExpr *Outer =
        InLambda ? lambdaOfCallOperator(Method, Context) : nullptr;
    if (!Outer)
      return false;
    // A lambda in between that captures a copy holds it in its closure, which
    // is there while it runs.
    if (capturesCopy(Outer, Variable))
      return true;
    // Another lambda in between, like `[&] { ... }()`, keeps what the function
    // around it has while it runs if it is called directly and doesn't
    // suspend before it makes this one, or is given only to what runs it to
    // completion, like a test category.
    if (!isRunByBlockingCallees(Outer, Context) &&
        ((isCoroutine(Function) &&
          mayRunAfterSuspension(Current, Function, Context)) ||
         !isCalledInScope(closureConsumers(Outer, Context, {}))))
      return false;
    Function = enclosingFunction(Outer);
    Current = Outer;
  }
  // A lambda in a default member initializer is made by a constructor, while
  // the object is there.
  return !Function && !Variable &&
         isa_and_nonnull<FieldDecl>(
             Current->getLambdaClass()->getLambdaContextDecl());
}

bool isLocalWhileRunning(const ValueDecl *Variable, const LambdaExpr *Lambda,
                         ASTContext &Context) {
  // A reference init capture, like `[&Alias = Value]`, refers to what it is
  // initialized with.
  for (int Depth = 0; Depth < 4; ++Depth) {
    const auto *Capture = dyn_cast<VarDecl>(Variable);
    // A capture by value, like `[Value = 1]`, is in the closure of its lambda.
    if (!Capture || !Capture->isInitCapture() ||
        !Capture->getType()->isReferenceType())
      break;
    // `[&Alias = Value]`, also of a member of a local, `[&Alias =
    // Data.m_Member]`, which lives as long as the local.
    const Expr *Init =
        Capture->getInit() ? skipWrappers(Capture->getInit()) : nullptr;
    for (;;) {
      if (const auto *Member = dyn_cast_or_null<MemberExpr>(Init)) {
        // A member of the object of the function, `[&Alias = m_Value]`, lives
        // as long as the object.
        if (Member->isArrow())
          return isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()) &&
                 isAliveWhileRunning(nullptr, Lambda, Context);
        Init = skipWrappers(Member->getBase());
        continue;
      }
      // An element of a local array.
      if (const auto *Subscript = dyn_cast_or_null<ArraySubscriptExpr>(Init);
          Subscript && Subscript->getBase()->IgnoreParenImpCasts()
                           ->getType()->isArrayType()) {
        Init = skipWrappers(Subscript->getBase()->IgnoreParenImpCasts());
        continue;
      }
      // Also an entry of a local container, `[&Value = Items[1]]`, which is
      // in it, what an operator of a local gives, `*Owner`, and what a
      // member function of it gives a reference to, `Items.f_GetFirst()`.
      if (const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Init);
          Operator && Operator->getNumArgs() >= 1 && Operator->isLValue() &&
          (Operator->getOperator() == OO_Subscript ||
           Operator->getOperator() == OO_Star ||
           Operator->getOperator() == OO_Call) &&
          Operator->getArg(0)->getType()->isRecordType()) {
        Init = skipWrappers(Operator->getArg(0)->IgnoreParenImpCasts());
        continue;
      }
      if (const auto *Call = dyn_cast_or_null<CXXMemberCallExpr>(Init);
          Call && Call->isLValue() && Call->getImplicitObjectArgument() &&
          !Call->getImplicitObjectArgument()->getType()->isPointerType()) {
        Init = skipWrappers(
            Call->getImplicitObjectArgument()->IgnoreParenImpCasts());
        continue;
      }
      // The object of the function, `[&Object = *this]`.
      if (const auto *Deref = dyn_cast_or_null<UnaryOperator>(Init);
          Deref && Deref->getOpcode() == UO_Deref &&
          isa<CXXThisExpr>(Deref->getSubExpr()->IgnoreParenImpCasts()))
        return isAliveWhileRunning(nullptr, Lambda, Context);
      break;
    }
    const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Init);
    if (!Ref)
      return false;
    Variable = Ref->getDecl();
  }
  // Too many reference init captures to follow.
  if (const auto *Capture = dyn_cast<VarDecl>(Variable);
      Capture && Capture->isInitCapture() &&
      Capture->getType()->isReferenceType())
    return false;
  return isAliveWhileRunning(Variable, Lambda, Context);
}

bool isObjectAliveWhileRunning(const LambdaExpr *Lambda, ASTContext &Context) {
  return isAliveWhileRunning(nullptr, Lambda, Context);
}

/// Whether \p E is an actor, a `TCActor`.
static bool isActorHandle(const Expr *E) {
  return recordName(E->getType()) == "TCActor";
}

/// Whether \p E is `g_Dispatch`, `g_Dispatch(Actor)` or
/// `g_ConcurrentDispatch`, or an actor, whose `Actor / Lambda` queues the
/// lambda on it for the result it is given, `fg_Work() > Actor / [...](
/// TCAsyncResult<int> &&) {...}`.
static bool isDispatchHelper(const Expr *E) {
  E = skipWrappers(E);
  if (isActorHandle(E))
    return true;
  if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
      Call && Call->getOperator() == OO_Call && Call->getNumArgs() >= 1)
    E = skipWrappers(Call->getArg(0));
  // `g_OnScopeExitActor / Lambda` dispatches it to the actor that is current
  // where it is made, when the scope exit runs, and `g_OnScopeExitActor(Actor)
  // / Lambda` to the actor.
  const auto *Ref = dyn_cast<DeclRefExpr>(E);
  return Ref && Ref->getDecl()->getIdentifier() &&
         (Ref->getDecl()->getName() == "g_Dispatch" ||
          Ref->getDecl()->getName() == "g_ConcurrentDispatch" ||
          Ref->getDecl()->getName() == "g_OnScopeExitActor");
}

/// The function that \p Call, a call of `fg_Dispatch` or a function like it,
/// dispatches: the argument of its first parameter that is a function, named
/// `_f...` by Malterlib's conventions. The other arguments are given to that
/// function.
static const Expr *dispatchedFunction(const CallExpr *Call) {
  const FunctionDecl *Callee = Call->getDirectCallee();
  if (!Callee)
    return nullptr;
  for (unsigned iParam = 0;
       iParam < Callee->getNumParams() && iParam < Call->getNumArgs(); ++iParam) {
    const ParmVarDecl *Param = Callee->getParamDecl(iParam);
    if (Param->getIdentifier() && Param->getName().starts_with("_f"))
      return Call->getArg(iParam);
  }
  return nullptr;
}

/// Whether \p Child is the function \p Call dispatches and returns the result
/// of: the first lambda given to `fg_Dispatch`, or the function given to
/// `f_Bind<&CActor::f_DispatchWithReturn<...>>`, not other arguments that the
/// dispatched function gets and can keep.
static bool isDispatchedFunction(const CallExpr *Call, StringRef Name,
                                 const Expr *Child) {
  if (Name == "fg_Dispatch" || Name == "fg_DirectDispatch" ||
      Name == "fg_ConcurrentDispatch" || Name == "fg_CallSafeDispatched" ||
      Name == "fg_CallSafeDispatchedOn")
    return isOperand(dispatchedFunction(Call), Child);
  // The dispatched function is the first argument; the others are its
  // arguments.
  if (Call->getNumArgs() < 1 || !isOperand(Call->getArg(0), Child))
    return false;
  // The bound function is `f_DispatchWithReturn`.
  const auto *Member =
      dyn_cast<MemberExpr>(Call->getCallee()->IgnoreParenImpCasts());
  const llvm::ArrayRef<TemplateArgumentLoc> Arguments =
      Member ? Member->template_arguments()
             : llvm::ArrayRef<TemplateArgumentLoc>();
  if (Arguments.empty())
    return false;
  // `CActor::f_DispatchWithReturn` itself, not a function of the same name
  // of another class.
  auto IsDispatchWithReturn = [](const NamedDecl *Decl) {
    const auto *Method = dyn_cast_or_null<CXXMethodDecl>(
        Decl ? Decl->getUnderlyingDecl()->getAsFunction() : nullptr);
    return Method && Method->getIdentifier() &&
           Method->getName() == "f_DispatchWithReturn" &&
           Method->getParent()->getIdentifier() &&
           Method->getParent()->getName() == "CActor";
  };
  const TemplateArgument &Bound = Arguments.front().getArgument();
  if (Bound.getKind() == TemplateArgument::Declaration)
    return IsDispatchWithReturn(Bound.getAsDecl());
  if (Bound.getKind() != TemplateArgument::Expression)
    return false;
  const Expr *E = Bound.getAsExpr();
  // Also through a constant, `constexpr auto pDispatch = &CActor::...`.
  for (int Depth = 0; Depth < 4; ++Depth) {
    E = E->IgnoreParenImpCasts();
    if (const auto *Unary = dyn_cast<UnaryOperator>(E))
      E = Unary->getSubExpr()->IgnoreParenImpCasts();
    // The name of a template as written, `&CActor::f_DispatchWithReturn`,
    // whose specialization the argument is.
    if (const auto *Lookup = dyn_cast<UnresolvedLookupExpr>(E))
      return Lookup->getNumDecls() > 0 &&
             llvm::all_of(Lookup->decls(), IsDispatchWithReturn);
    const auto *Ref = dyn_cast<DeclRefExpr>(E);
    if (!Ref)
      return false;
    const auto *Constant = dyn_cast<VarDecl>(Ref->getDecl());
    if (!Constant)
      return IsDispatchWithReturn(Ref->getDecl());
    if (!Constant->isConstexpr() || !Constant->getInit())
      return false;
    E = Constant->getInit();
  }
  return false;
}

/// The actor that the dispatch helper \p Helper dispatches to: the argument
/// of `g_Dispatch(Actor)`, or null for `g_Dispatch`, which dispatches to the
/// current actor.
static const Expr *dispatchHelperActor(const Expr *Helper) {
  const Expr *E = skipWrappers(Helper);
  if (isActorHandle(E))
    return E;
  // `g_ConcurrentDispatch` stands for the concurrent actor it dispatches to.
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
      Ref && Ref->getDecl()->getIdentifier() &&
      Ref->getDecl()->getName() == "g_ConcurrentDispatch")
    return Ref;
  if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
      Call && Call->getOperator() == OO_Call && Call->getNumArgs() >= 2)
    return Call->getArg(1);
  return nullptr;
}

/// The lambda that destroying \p Local runs, a scope exit like `auto
/// Cleanup = g_OnScopeExit / [...]`.
bool isScopeExitHelper(const Expr *E, bool AlsoActor) {
  const CXXRecordDecl *Type =
      E ? E->getType().getNonReferenceType()->getAsCXXRecordDecl() : nullptr;
  if (!Type || !Type->getIdentifier())
    return false;
  // Also those that make a shared pointer to the guard,
  // `g_OnScopeExitShared / [...]`, like `fg_OnScopeExitShared`.
  return Type->getName() == "COnScopeExitHelper" ||
         Type->getName() == "COnScopeExitHelperWithException" ||
         Type->getName() == "COnScopeExitCatchHelper" ||
         Type->getName() == "COnScopeExitSharedHelper" ||
         Type->getName() == "COnScopeExitSharedWithExceptionHelper" ||
         (AlsoActor && Type->getName() == "COnScopeExitActorHelper");
}

const LambdaExpr *scopeExitOperand(const Expr *E) {
  if (!E)
    return nullptr;
  E = skipWrappers(E)->IgnoreParenImpCasts();
  // Either value of a conditional, when both make it of the same lambda,
  // `bChoose ? g_OnScopeExit / fg_Move(fCleanup) : g_OnScopeExit /
  // fg_Move(fCleanup)`.
  if (const auto *Conditional = dyn_cast<ConditionalOperator>(E)) {
    const LambdaExpr *First = scopeExitOperand(Conditional->getTrueExpr());
    return First == scopeExitOperand(Conditional->getFalseExpr()) ? First
                                                                  : nullptr;
  }
  if (const auto *Divide = dyn_cast<CXXOperatorCallExpr>(E);
      Divide && Divide->getOperator() == OO_Slash && Divide->getNumArgs() == 2)
    return isScopeExitHelper(Divide->getArg(0)) ? asLambda(Divide->getArg(1))
                                                : nullptr;
  if (const auto *Divide = dyn_cast<CXXMemberCallExpr>(E);
      Divide && Divide->getMethodDecl() &&
      Divide->getMethodDecl()->getOverloadedOperator() == OO_Slash &&
      Divide->getNumArgs() == 1)
    return isScopeExitHelper(Divide->getImplicitObjectArgument())
               ? asLambda(Divide->getArg(0))
               : nullptr;
  if (const auto *Call = dyn_cast<CallExpr>(E);
      Call && Call->getNumArgs() >= 1 &&
      calleeName(Call).starts_with("fg_OnScopeExit"))
    return asLambda(Call->getArg(0));
  return nullptr;
}

static const VarDecl *cleanupHolderOf(const Expr *E);

const LambdaExpr *scopeExitLambda(const VarDecl *Local) {
  static thread_local int s_Depth = 0;
  if (!Local || !Local->getInit() || s_Depth > 64)
    return nullptr;
  const Expr *Init = skipWrappers(Local->getInit())->IgnoreParenImpCasts();
  // Moved from another guard, `auto Moved = fg_Move(Cleanup)`, also through a
  // local reference to it, `fg_Move(Alias)`, which then runs the callback.
  if (const auto *Move = dyn_cast<CallExpr>(Init);
      Move && Move->getNumArgs() == 1 && (isMoveFunction(calleeName(Move))))
    if (const VarDecl *Moved = cleanupHolderOf(Move->getArg(0));
        Moved && Moved != Local && Moved->hasLocalStorage() &&
        !Moved->getType()->isReferenceType()) {
      ++s_Depth;
      const LambdaExpr *Lambda = scopeExitLambda(Moved);
      --s_Depth;
      return Lambda;
    }
  // Also a copy of a shared pointer to a guard, `auto pKeep = pCleanup` of
  // `g_OnScopeExitShared`, which shares it, and the last of which to be
  // released runs the callback.
  if (isa<DeclRefExpr>(Init) && !Local->getType()->isReferenceType() &&
      recordName(Local->getType()) == "TCSharedPointer")
    if (const VarDecl *Copied = cleanupHolderOf(Init);
        Copied && Copied != Local && Copied->hasLocalStorage() &&
        !Copied->getType()->isReferenceType()) {
      ++s_Depth;
      const LambdaExpr *Lambda = scopeExitLambda(Copied);
      --s_Depth;
      return Lambda;
    }
  return scopeExitOperand(Init);
}

bool isClearedBeforeLeaving(const VarDecl *Guard, const CompoundStmt *Block,
                            llvm::function_ref<bool(const Stmt *)> Leaves) {
  if (!Guard || !Block)
    return false;
  std::function<bool(const Stmt *)> NamesGuard = [&](const Stmt *S) {
    if (const auto *Ref = dyn_cast_or_null<DeclRefExpr>(S);
        Ref && Ref->getDecl() == Guard)
      return true;
    return S && llvm::any_of(childrenOnce(S), NamesGuard);
  };
  bool After = false;
  for (const Stmt *Statement : Block->body()) {
    if (!After) {
      if (const auto *Declaration = dyn_cast<DeclStmt>(Statement))
        After = llvm::is_contained(Declaration->decls(), Guard);
      continue;
    }
    const auto *Clear = isa<Expr>(Statement)
                            ? dyn_cast<CXXMemberCallExpr>(
                                  cast<Expr>(Statement)->IgnoreImplicit())
                            : nullptr;
    const auto *Object =
        Clear && Clear->getImplicitObjectArgument()
            ? dyn_cast<DeclRefExpr>(
                  Clear->getImplicitObjectArgument()->IgnoreParenImpCasts())
            : nullptr;
    if (Object && Object->getDecl() == Guard && Clear->getMethodDecl() &&
        Clear->getMethodDecl()->getIdentifier() &&
        (Clear->getMethodDecl()->getName() == "f_Clear" ||
         Clear->getMethodDecl()->getName() == "f_ClearFunctor"))
      return true;
    if (Leaves(Statement) || NamesGuard(Statement))
      return false;
  }
  return false;
}

const Expr *fullExpressionOf(const Expr *E, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*E);
  const Expr *Full = E;
  for (int Depth = 0; Depth < 4096; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    const auto *Parent = Parents.empty() ? nullptr : Parents[0].get<Expr>();
    if (!Parent || isa<LambdaExpr, ExprWithCleanups>(Parent))
      break;
    Full = Parent;
    Node = Parents[0];
  }
  // Nor the parentheses around it, which the CFG doesn't have either.
  return Full->IgnoreParens();
}

bool isTemporaryScopeExitLambda(const LambdaExpr *Lambda,
                                ASTContext &Context) {
  // Up through what passes the lambda or the scope exit on as it is.
  auto Up = [&](DynTypedNode Node) {
    for (int Depth = 0; Depth < 16; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return DynTypedNode();
      const auto *Parent = Parents[0].get<Expr>();
      if (!Parent || !isa<ParenExpr, ImplicitCastExpr, MaterializeTemporaryExpr,
                          CXXBindTemporaryExpr, CXXConstructExpr,
                          CXXFunctionalCastExpr, ExprWithCleanups>(Parent))
        return Parents[0];
      Node = Parents[0];
    }
    return DynTypedNode();
  };
  const DynTypedNode Made = Up(DynTypedNode::create(*Lambda));
  const Expr *ScopeExit = Made.get<Expr>();
  if (!ScopeExit || scopeExitOperand(ScopeExit) != Lambda)
    return false;
  // Discarded: a statement of its own, also cast to void, or the left side
  // of a comma, `((void)(g_OnScopeExit / [...]), ...)`, not held by a local,
  // given on or returned.
  DynTypedNode Below = DynTypedNode::create(*ScopeExit);
  DynTypedNode Node = Up(Below);
  while (const auto *Cast = Node.get<ExplicitCastExpr>()) {
    if (!Cast->getType()->isVoidType())
      return false;
    Below = Node;
    Node = Up(Node);
  }
  if (const auto *Comma = Node.get<BinaryOperator>();
      Comma && Comma->getOpcode() == BO_Comma && Below.get<Expr>() &&
      Comma->getLHS()->IgnoreParens() == Below.get<Expr>()->IgnoreParens())
    return true;
  return Node.get<CompoundStmt>() || Node.get<IfStmt>() ||
         Node.get<WhileStmt>() || Node.get<ForStmt>() || Node.get<DoStmt>() ||
         Node.get<CXXForRangeStmt>() || Node.get<SwitchCase>() ||
         Node.get<LabelStmt>() || Node.get<AttributedStmt>();
}

/// The overrides of the virtual functions of an AST, by what they override,
/// found once.
struct COverrideFacts {
  bool Found = false;
  llvm::DenseMap<const CXXMethodDecl *,
                 llvm::SmallVector<const CXXMethodDecl *, 2>>
      Overriders;
};

llvm::SmallVector<const CXXMethodDecl *, 4>
dispatchedMethods(const CXXMemberCallExpr *Call) {
  llvm::SmallVector<const CXXMethodDecl *, 4> Methods;
  const CXXMethodDecl *Method = Call->getMethodDecl();
  if (!Method)
    return Methods;
  Methods.push_back(Method);
  // Not a call that names the function, `CBase::f_Run()`, nor of one that
  // can't be overridden.
  const auto *Member =
      dyn_cast<MemberExpr>(Call->getCallee()->IgnoreParenImpCasts());
  if (!Method->isVirtual() || (Member && Member->hasQualifier()) ||
      Method->hasAttr<FinalAttr>() || Method->getParent()->hasAttr<FinalAttr>())
    return Methods;
  // Only the overrides of the classes the object can be: the class it is
  // called on, `pApp->f_Tick()`, or one derived from it, none when that class
  // is final, and of the bases of the class that override the function, which
  // a using declaration can name the function past, `using CBase::f_Tick;`
  // with `CMid::f_Tick` overriding it in a base between.
  // As written, not as converted to the class of the function, `CBase *` of
  // `this` of a `CApp` calling `f_Tick()` of `CBase`.
  QualType ObjectType =
      Call->getImplicitObjectArgument()
          ? Call->getImplicitObjectArgument()->IgnoreParenImpCasts()->getType()
          : QualType();
  if (!ObjectType.isNull() && ObjectType->isPointerType())
    ObjectType = ObjectType->getPointeeType();
  const CXXRecordDecl *Object =
      ObjectType.isNull() ? nullptr : ObjectType->getAsCXXRecordDecl();
  if (Object && Object->hasDefinition())
    Object = Object->getDefinition();
  else
    Object = nullptr;
  const bool Final = Object && Object->hasAttr<FinalAttr>();
  const ASTContext &Context = Method->getASTContext();
  COverrideFacts &Facts = astCache<COverrideFacts>(Context);
  if (!Facts.Found) {
    Facts.Found = true;
    // The declarations of classes, also of the specializations of
    // templates and in functions, not the statements of their bodies.
    llvm::SmallVector<const DeclContext *, 64> Work{
        Context.getTranslationUnitDecl()};
    llvm::SmallPtrSet<const DeclContext *, 32> Visited;
    auto Add = [&](const DeclContext *Inner) {
      if (Inner && Visited.insert(Inner).second)
        Work.push_back(Inner);
    };
    while (!Work.empty())
      for (const Decl *D : Work.pop_back_val()->decls()) {
        if (const auto *Overrider = dyn_cast<CXXMethodDecl>(D))
          for (const CXXMethodDecl *Overridden :
               Overrider->overridden_methods())
            Facts.Overriders[Overridden->getCanonicalDecl()].push_back(
                Overrider);
        if (const auto *Template = dyn_cast<ClassTemplateDecl>(D)) {
          Add(Template->getTemplatedDecl());
          for (const ClassTemplateSpecializationDecl *Specialization :
               Template->specializations())
            Add(Specialization);
        } else if (const auto *Function = dyn_cast<FunctionTemplateDecl>(D)) {
          // Also its instantiations, whose local classes, `struct CDerived :
          // t_CBase` in `fg_Make<CApp>()`, are of what they are made with.
          Add(Function->getTemplatedDecl());
          for (const FunctionDecl *Specialization : Function->specializations())
            Add(Specialization);
        }
        // Also of the classes of functions, `struct CDerived : CBase {}` in a
        // body, which are declared in it.
        else if (isa<NamespaceDecl, CXXRecordDecl, LinkageSpecDecl, ExportDecl,
                     FunctionDecl>(D))
          Add(cast<DeclContext>(D));
      }
  }
  // An override in a class derived from the object's, or in one beside it
  // that shares the class declaring the function as a virtual base, `struct
  // CSide : virtual CBase`, which a class derived from both takes as the
  // final override.
  // Each class's bases are found once, each base once, also one that many
  // paths share, which searching them again for each override would go
  // through exponentially often.
  struct CAncestors {
    llvm::DenseMap<const CXXRecordDecl *,
                   llvm::SmallPtrSet<const CXXRecordDecl *, 16>>
        Of;
  };
  auto DerivesFrom = [&](const CXXRecordDecl *Derived,
                         const CXXRecordDecl *Base) {
    if (!Derived || !Base)
      return false;
    Derived = Derived->getCanonicalDecl();
    Base = Base->getCanonicalDecl();
    auto &Of = astCache<CAncestors>(Derived->getASTContext()).Of;
    if (auto It = Of.find(Derived); It != Of.end())
      return It->second.contains(Base);
    llvm::SmallPtrSet<const CXXRecordDecl *, 16> Ancestors;
    llvm::SmallVector<const CXXRecordDecl *, 16> Work{Derived};
    while (!Work.empty()) {
      const CXXRecordDecl *Current = Work.pop_back_val();
      const CXXRecordDecl *Definition = Current->getDefinition();
      if (!Definition)
        continue;
      for (const CXXBaseSpecifier &Specifier : Definition->bases())
        if (const CXXRecordDecl *Parent =
                Specifier.getType()->getAsCXXRecordDecl())
          if (Ancestors.insert(Parent->getCanonicalDecl()).second)
            Work.push_back(Parent->getCanonicalDecl());
    }
    const bool Derives = Ancestors.contains(Base);
    Of[Derived] = std::move(Ancestors);
    return Derives;
  };
  auto MayRun = [&](const CXXMethodDecl *Overridden,
                    const CXXMethodDecl *Overrider) {
    const CXXRecordDecl *Class = Overrider->getParent();
    if (!Object || Class->getCanonicalDecl() == Object->getCanonicalDecl() ||
        DerivesFrom(Object, Class))
      return true;
    if (Final)
      return false;
    if (DerivesFrom(Class, Object))
      return true;
    // A virtual base of the object that is, or derives from, the class
    // declaring the function.
    const CXXRecordDecl *Declaring = Overridden->getParent();
    const CXXRecordDecl *Defined = Object->getDefinition();
    return Defined && DerivesFrom(Class, Declaring) &&
           llvm::any_of(Defined->vbases(), [&](const CXXBaseSpecifier &Base) {
             const CXXRecordDecl *Virtual =
                 Base.getType()->getAsCXXRecordDecl();
             return Virtual && (Virtual->getCanonicalDecl() ==
                                    Declaring->getCanonicalDecl() ||
                                DerivesFrom(Virtual, Declaring));
           });
  };
  llvm::SmallPtrSet<const CXXMethodDecl *, 8> Seen{Method->getCanonicalDecl()};
  for (size_t iMethod = 0; iMethod < Methods.size(); ++iMethod)
    for (const CXXMethodDecl *Overrider :
         Facts.Overriders.lookup(Methods[iMethod]->getCanonicalDecl()))
      if (MayRun(Methods[iMethod], Overrider) &&
          Seen.insert(Overrider->getCanonicalDecl()).second)
        Methods.push_back(Overrider);
  return Methods;
}

const LambdaExpr *cleanupLambdaOf(const Expr *Argument) {
  if (const LambdaExpr *Lambda = asLambda(Argument))
    return Lambda;
  const auto *Call =
      dyn_cast<CXXOperatorCallExpr>(skipWrappers(Argument)->IgnoreParenImpCasts());
  if (!Call || Call->getOperator() != OO_Call || Call->getNumArgs() < 1)
    return nullptr;
  const LambdaExpr *Factory = asLambda(Call->getArg(0));
  if (!Factory || isCoroutine(Factory->getCallOperator()))
    return nullptr;
  // What each of its returns gives, when they all give the same lambda, not
  // one of a branch of an `if constexpr` that is discarded.
  const LambdaExpr *Made = nullptr;
  bool Other = false;
  const ASTContext &Context = Factory->getCallOperator()->getASTContext();
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Other || isa<LambdaExpr>(S))
      return;
    if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
      const LambdaExpr *Returned =
          Return->getRetValue() ? asLambda(Return->getRetValue()) : nullptr;
      if (!Returned || (Made && Made != Returned))
        Other = true;
      else
        Made = Returned;
      return;
    }
    for (const Stmt *Child : keptChildren(S, Context))
      Visit(Child);
  };
  Visit(Factory->getBody());
  return Other ? nullptr : Made;
}

llvm::SmallVector<const LambdaExpr *, 2>
asyncDestroyLambdas(const VarDecl *Holder) {
  llvm::SmallVector<const LambdaExpr *, 2> Lambdas;
  if (!Holder || !Holder->getInit())
    return Lambdas;
  // Also each of a conditional, `bChoose ? co_await fg_AsyncDestroy(A) :
  // co_await fg_AsyncDestroy(B)`.
  for (const CoroutineSuspendExpr *Await :
       holderAwaits(Holder->getInit(), &Holder->getASTContext())) {
    const auto *Call =
        Await ? dyn_cast<CallExpr>(
                    skipWrappers(Await->getOperand())->IgnoreParenImpCasts())
              : nullptr;
    if (!Call || Call->getNumArgs() < 1 ||
        !llvm::is_contained(AsyncDestroyFunctions, calleeName(Call)))
      continue;
    if (const LambdaExpr *Lambda = cleanupLambdaOf(Call->getArg(0)))
      Lambdas.push_back(Lambda);
  }
  return Lambdas;
}

bool isAsyncDestroyHolder(const VarDecl *Holder) {
  return !asyncDestroyLambdas(Holder).empty();
}

/// The local that \p E names, also through a local reference to it, `auto
/// &Alias = Cleanup`, or what an unchanged local pointer to it points to,
/// `*pCleanup` with `auto *pCleanup = &Cleanup`.
static const VarDecl *cleanupHolderOf(const Expr *E) {
  E = skipWrappers(E)->IgnoreParenImpCasts();
  for (int Depth = 0; Depth < 64; ++Depth) {
    // Also what a shared pointer to it points to, `pCleanup->f_Clear()` of
    // `fg_OnScopeExitShared`.
    if (const auto *Through = dyn_cast<CXXOperatorCallExpr>(E);
        Through &&
        (Through->getOperator() == OO_Arrow ||
         Through->getOperator() == OO_Star) &&
        Through->getNumArgs() == 1) {
      E = skipWrappers(Through->getArg(0))->IgnoreParenImpCasts();
      continue;
    }
    const auto *Deref = dyn_cast<UnaryOperator>(E);
    if (!Deref || Deref->getOpcode() != UO_Deref)
      break;
    const auto *PointerRef =
        dyn_cast<DeclRefExpr>(Deref->getSubExpr()->IgnoreParenImpCasts());
    const auto *Pointer =
        PointerRef ? dyn_cast<VarDecl>(PointerRef->getDecl()) : nullptr;
    const auto *Address =
        Pointer && Pointer->hasLocalStorage() && !isa<ParmVarDecl>(Pointer) &&
                Pointer->getInit() && !isChangedVariable(Pointer)
            ? dyn_cast<UnaryOperator>(Pointer->getInit()->IgnoreParenImpCasts())
            : nullptr;
    if (!Address || Address->getOpcode() != UO_AddrOf)
      return nullptr;
    E = Address->getSubExpr()->IgnoreParenImpCasts();
  }
  const auto *Ref = dyn_cast<DeclRefExpr>(E);
  const auto *Holder = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  for (int Depth = 0; Depth < 64 && Holder &&
                      Holder->getType()->isReferenceType() && Holder->getInit();
       ++Depth) {
    const auto *Bound =
        dyn_cast<DeclRefExpr>(Holder->getInit()->IgnoreParenImpCasts());
    const auto *Next = Bound ? dyn_cast<VarDecl>(Bound->getDecl()) : nullptr;
    if (!Next)
      break;
    Holder = Next;
  }
  return Holder;
}

/// The moves of the translation unit whose result a local reference binds,
/// `auto &&Alias = fg_Move(Cleanup)`, which move nothing.
struct CReferenceBoundMoves {
  llvm::DenseSet<const CallExpr *> Calls;
};

void updateArmedCleanups(
    const Stmt *S, llvm::SmallPtrSetImpl<const VarDecl *> &Armed,
    llvm::function_ref<void(const LambdaExpr *Lambda, const VarDecl *Holder)>
        OnRun) {
  if (const auto *Declaration = dyn_cast<DeclStmt>(S))
    for (const Decl *D : Declaration->decls())
      if (const auto *Alias = dyn_cast<VarDecl>(D);
          Alias && Alias->getType()->isReferenceType() && Alias->getInit())
        if (const auto *Move = dyn_cast<CallExpr>(
                skipWrappers(Alias->getInit())->IgnoreParenImpCasts()))
          astCache<CReferenceBoundMoves>(Alias->getASTContext())
              .Calls.insert(Move);
  if (const auto *Declaration = dyn_cast<DeclStmt>(S)) {
    for (const Decl *D : Declaration->decls())
      if (const auto *Holder = dyn_cast<VarDecl>(D);
          Holder && (scopeExitLambda(Holder) || isAsyncDestroyHolder(Holder))) {
        // One moved from another, `auto Moved = fg_Move(Cleanup)`, takes its
        // callback, and the one moved from no longer runs it, but not a
        // reference bound to it, `auto &&Alias = fg_Move(Cleanup)`, which
        // moves nothing.
        if (const auto *Move = dyn_cast_or_null<CallExpr>(
                Holder->getInit()
                    ? skipWrappers(Holder->getInit())->IgnoreParenImpCasts()
                    : nullptr);
            Move && Move->getNumArgs() == 1 &&
            (isMoveFunction(calleeName(Move))))
          if (const VarDecl *Source = cleanupHolderOf(Move->getArg(0));
              Source && Source != Holder) {
            if (Holder->getType()->isReferenceType() || !Armed.contains(Source))
              continue;
            Armed.erase(Source);
          }
        Armed.insert(Holder);
      }
    return;
  }
  if (const auto *Clear = dyn_cast<CXXMemberCallExpr>(S);
      Clear && Clear->getMethodDecl() &&
      ((Clear->getMethodDecl()->getIdentifier() &&
        (Clear->getMethodDecl()->getName() == "f_Clear" ||
         Clear->getMethodDecl()->getName() == "f_ClearFunctor")) ||
       Clear->getMethodDecl()->getOverloadedOperator() == OO_Call)) {
    bool Run = Clear->getMethodDecl()->getOverloadedOperator() == OO_Call;
    // `f_Clear()` of the guard of `fg_AsyncDestroy` cancels its cleanup too.
    if (const VarDecl *Holder =
            cleanupHolderOf(Clear->getImplicitObjectArgument())) {
      // But clearing the shared pointer to a guard itself, `pCleanup
      // .f_Clear()` of `fg_OnScopeExitShared`, releases the guard, which runs
      // the cleanup when it was the last.
      const CXXRecordDecl *Pointer =
          Holder->getType().getNonReferenceType()->getAsCXXRecordDecl();
      const bool ClearsPointer =
          Pointer && recordName(Holder->getType()) == "TCSharedPointer" &&
          Clear->getMethodDecl()->getParent()->getCanonicalDecl() ==
              Pointer->getCanonicalDecl();
      if (ClearsPointer)
        Run = true;
      // Not when another that shares it still holds it, `pKeep` of `auto
      // pKeep = pCleanup`.
      if (const LambdaExpr *Lambda = scopeExitLambda(Holder)) {
        const bool Shared =
            ClearsPointer && llvm::any_of(Armed, [&](const VarDecl *Other) {
              return Other != Holder && scopeExitLambda(Other) == Lambda;
            });
        if (Run && !Shared && Armed.contains(Holder))
          OnRun(Lambda, Holder);
        Armed.erase(Holder);
      } else if (isAsyncDestroyHolder(Holder) && !Run)
        Armed.erase(Holder);
    }
  } else if (const auto *Run = dyn_cast<CXXOperatorCallExpr>(S);
             Run && Run->getOperator() == OO_Call && Run->getNumArgs() >= 1) {
    if (const VarDecl *Holder = cleanupHolderOf(Run->getArg(0)))
      if (const LambdaExpr *Lambda = scopeExitLambda(Holder)) {
        if (Armed.contains(Holder))
          OnRun(Lambda, Holder);
        Armed.erase(Holder);
      }
  }
  // Moved away, `fg_Move(Cleanup)` or given to an rvalue reference: one given
  // by a reference that isn't to an rvalue, or by address, may not be moved,
  // and its destructor may still start the cleanup.
  // Not a move whose result a local reference binds, nor forwarding as an
  // lvalue, `fg_Forward<CGuard &>(Cleanup)`, which give it no other owner.
  auto Disarm = [&](const FunctionDecl *Callee,
                    llvm::ArrayRef<const Expr *> Arguments, unsigned Offset,
                    const CallExpr *Call = nullptr) {
    const StringRef Name =
        Callee && Callee->getIdentifier() ? Callee->getName() : "";
    if (Call && Callee &&
        (Call->isLValue() ||
         astCache<CReferenceBoundMoves>(Callee->getASTContext())
             .Calls.contains(Call)) &&
        (isMoveFunction(Name) || isForwardFunction(Name)))
      return;
    const bool Moves = isMoveFunction(Name) || isForwardFunction(Name);
    for (unsigned iArgument = 0; iArgument < Arguments.size(); ++iArgument) {
      const VarDecl *Holder = cleanupHolderOf(Arguments[iArgument]);
      if (!Holder || !isAsyncDestroyHolder(Holder))
        continue;
      const unsigned iParameter = iArgument - Offset;
      const QualType Type =
          Callee && iArgument >= Offset && iParameter < Callee->getNumParams()
              ? Callee->getParamDecl(iParameter)->getType()
              : QualType();
      if (Moves || (!Type.isNull() && Type->isRValueReferenceType()))
        Armed.erase(Holder);
    }
  };
  if (const auto *Call = dyn_cast<CallExpr>(S))
    Disarm(Call->getDirectCallee(),
           llvm::ArrayRef(Call->getArgs(), Call->getNumArgs()),
           isa<CXXOperatorCallExpr>(Call) &&
                   isa_and_nonnull<CXXMethodDecl>(Call->getDirectCallee())
               ? 1
               : 0,
           Call);
  else if (const auto *Construct = dyn_cast<CXXConstructExpr>(S))
    Disarm(Construct->getConstructor(),
           llvm::ArrayRef(Construct->getArgs(), Construct->getNumArgs()), 0);
  // An early await of the guard, `co_await fg_Move(Cleanup)`, also wrapped,
  // `co_await fg_Move(Cleanup).f_Wrap()`, runs the cleanup here.
  const auto *Await = dyn_cast<CoroutineSuspendExpr>(S);
  if (!Await)
    return;
  const Expr *Operand = skipWrappers(Await->getOperand())->IgnoreParenImpCasts();
  if (const auto *Wrap = dyn_cast<CXXMemberCallExpr>(Operand);
      Wrap && Wrap->getMethodDecl() && Wrap->getMethodDecl()->getIdentifier() &&
      Wrap->getMethodDecl()->getName() == "f_Wrap")
    Operand =
        skipWrappers(Wrap->getImplicitObjectArgument())->IgnoreParenImpCasts();
  if (const auto *Move = dyn_cast<CallExpr>(Operand);
      Move && Move->getNumArgs() == 1 && (isMoveFunction(calleeName(Move))))
    if (const VarDecl *Holder = cleanupHolderOf(Move->getArg(0)))
      for (const LambdaExpr *Destroy : asyncDestroyLambdas(Holder))
        OnRun(Destroy, Holder);
  // Also a cast to an rvalue reference, `co_await static_cast<CGuard
  // &&>(Cleanup)`, which moves it like `fg_Move`.
  if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Operand);
      Cast && Cast->getTypeAsWritten()->isRValueReferenceType())
    if (const VarDecl *Holder = cleanupHolderOf(Cast->getSubExpr())) {
      Armed.erase(Holder);
      for (const LambdaExpr *Destroy : asyncDestroyLambdas(Holder))
        OnRun(Destroy, Holder);
    }
}

const Expr *closureDispatch(const Expr *Start, const CXXRecordDecl *Closure,
                            ASTContext &Context, const Expr **Actor) {
  if (Actor)
    *Actor = nullptr;
  DynTypedNode Node = DynTypedNode::create(*Start);
  for (int Depth = 0; Depth < 16; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    const auto *Parent = Parents.empty() ? nullptr : Parents[0].get<Expr>();
    if (!Parent || isa<ExprWithCleanups>(Parent))
      return nullptr;
    const auto *Child = Node.get<Expr>();
    if (passesClosureOn(Parent, Closure)) {
      Node = Parents[0];
      continue;
    }
    const auto *Call = dyn_cast<CallExpr>(Parent);
    if (!Call)
      return nullptr;
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call)) {
      if (Operator->getOperator() != OO_Slash || Operator->getNumArgs() != 2 ||
          !isOperand(Operator->getArg(1), Child) ||
          !isDispatchHelper(Operator->getArg(0)))
        return nullptr;
      if (Actor)
        *Actor = dispatchHelperActor(Operator->getArg(0));
      return Call;
    }
    const StringRef Name = callTargetName(Call);
    // `fg_DirectDispatch` and `fg_CallSafeDispatched` run it on the current
    // actor, like `fg_Dispatch` given only it, and `fg_CallSafeDispatchedOn`
    // on the actor it is given, like `fg_Dispatch` given one.
    if ((Name != "fg_Dispatch" && Name != "fg_ConcurrentDispatch" &&
         Name != "fg_DirectDispatch" && Name != "fg_CallSafeDispatched" &&
         Name != "fg_CallSafeDispatchedOn" && Name != "f_Bind" &&
         Name != "f_BindByValue") ||
        !isDispatchedFunction(Call, Name, Child))
      return nullptr;
    if (Actor) {
      // `fg_ConcurrentDispatch` dispatches to a concurrent actor, which the
      // call stands for.
      if (Name == "fg_ConcurrentDispatch")
        *Actor = Call;
      else if ((Name == "fg_Dispatch" || Name == "fg_CallSafeDispatchedOn") &&
               Call->getNumArgs() >= 1 && !isOperand(Call->getArg(0), Child))
        *Actor = Call->getArg(0);
      else if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call))
        *Actor = Member->getImplicitObjectArgument();
    }
    return Call;
  }
  return nullptr;
}

llvm::SmallPtrSet<const Stmt *, 8> statementsBeforeLeaving(const Stmt *Body,
                                                           bool CallsLeave) {
  llvm::SmallPtrSet<const Stmt *, 8> Statements;
  // Of a coroutine, its statements until it first suspends, after which it
  // can be aborted, or resumed only after its caller goes on.
  const auto *Coroutine = dyn_cast_or_null<CoroutineBodyStmt>(Body);
  if (Coroutine)
    Body = Coroutine->getBody();
  const auto *Compound = dyn_cast_or_null<CompoundStmt>(Body);
  if (!Compound)
    return Statements;
  // Also those of a block in it, `{ pEntry = ...; }`, which run each time it
  // does, until one of them can leave.
  std::function<bool(const CompoundStmt *, int)> Add =
      [&](const CompoundStmt *Block, int Depth) {
        for (const Stmt *Statement : Block->body()) {
          if (const auto *Inner = dyn_cast<CompoundStmt>(Statement);
              Inner && Depth < 16) {
            Statements.insert(Statement);
            if (!Add(Inner, Depth + 1))
              return false;
            continue;
          }
          bool bLeaves = false;
          std::function<void(const Stmt *)> FindLeave = [&](const Stmt *S) {
            if (!S || bLeaves || isa<LambdaExpr>(S))
              return;
            bLeaves =
                isa<ReturnStmt, CoreturnStmt, GotoStmt, IndirectGotoStmt,
                    LabelStmt, CXXThrowExpr, BreakStmt, ContinueStmt>(S) ||
                (Coroutine && isa<CoawaitExpr, CoyieldExpr>(S));
            // Also a call that can throw, where what it throws is caught.
            if (const auto *Call = dyn_cast<CallExpr>(S); Call && CallsLeave) {
              const FunctionDecl *Callee = Call->getDirectCallee();
              const auto *Prototype =
                  Callee ? Callee->getType()->getAs<FunctionProtoType>()
                         : nullptr;
              if (!Prototype || !Prototype->isNothrow())
                bLeaves = true;
            }
            for (const Stmt *Child : childrenOnce(S))
              FindLeave(Child);
          };
          FindLeave(Statement);
          if (bLeaves)
            return false;
          Statements.insert(Statement);
          if (const auto *E = dyn_cast<Expr>(Statement))
            Statements.insert(E->IgnoreImplicit()->IgnoreParens());
        }
        return true;
      };
  Add(Compound, 0);
  return Statements;
}

bool reassignedBefore(const Stmt *Body, const ValueDecl *Variable,
                      const Expr *Read, const SourceManager &SM,
                      llvm::function_ref<bool(const Expr *)> Replaces) {
  // How deep it is in the lambdas it looks into, and what each of them
  // decides in the outermost call, once, also one that is called again and
  // again, `fStep(); fStep();`, which would take exponential time.
  static thread_local int s_Depth = 0;
  static thread_local llvm::DenseMap<const Stmt *, bool> s_Decided;
  if (s_Depth == 0)
    s_Decided.clear();
  const auto *Compound = dyn_cast_or_null<CompoundStmt>(Body);
  if (!Compound)
    return false;
  const llvm::SmallPtrSet<const Stmt *, 8> Always =
      statementsBeforeLeaving(Compound);
  for (const Stmt *Statement : Compound->body()) {
    if (!Always.contains(Statement) ||
        (Read && !SM.isBeforeInTranslationUnit(Statement->getEndLoc(),
                                               Read->getBeginLoc())))
      return false;
    // Also in a lambda called here that captures it by reference and
    // assigns it each time it runs, `fRefresh();` of `[&pEntry] { pEntry =
    // mp_Items.f_FindEqual(1); }`.
    const auto *StatementExpr = dyn_cast<Expr>(Statement);
    if (const auto *Call = dyn_cast_or_null<CXXOperatorCallExpr>(
            StatementExpr ? StatementExpr->IgnoreImplicit()->IgnoreParens()
                          : nullptr);
        Call && Call->getOperator() == OO_Call && Call->getNumArgs() >= 1)
      if (const LambdaExpr *Called = calledLambda(Call->getArg(0));
          Called && Called->getBody() != Body &&
          llvm::any_of(Called->captures(), [&](const LambdaCapture &Capture) {
            return Capture.capturesVariable() &&
                   Capture.getCapturedVar() == Variable &&
                   Capture.getCaptureKind() == LCK_ByRef;
          })) {
        bool Reassigns = false;
        if (const auto Decided = s_Decided.find(Called->getBody());
            Decided != s_Decided.end())
          Reassigns = Decided->second;
        else if (s_Depth < 16) {
          s_Decided[Called->getBody()] = false;
          ++s_Depth;
          Reassigns = reassignedBefore(Called->getBody(), Variable, nullptr,
                                       SM, Replaces);
          --s_Depth;
          s_Decided[Called->getBody()] = Reassigns;
        }
        if (Reassigns)
          return true;
      }
    const auto *Value = dyn_cast<Expr>(Statement);
    const auto *Assign =
        Value ? dyn_cast<BinaryOperator>(Value->IgnoreImplicit()->IgnoreParens())
              : nullptr;
    if (!Assign || Assign->getOpcode() != BO_Assign)
      continue;
    const auto *Target =
        dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts());
    if (Target && Target->getDecl() == Variable && Replaces(Assign->getRHS()))
      return true;
  }
  return false;
}

bool insideTryBlock(const Stmt *S, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*S);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    if (const auto *Try = Parents[0].get<CXXTryStmt>();
        Try && Node.get<Stmt>() == Try->getTryBlock())
      return true;
    if (Parents[0].get<LambdaExpr>() || Parents[0].get<FunctionDecl>())
      return false;
    Node = Parents[0];
  }
  return false;
}

llvm::SmallVector<const DeclRefExpr *, 2> assignedVariables(const Expr *Left) {
  llvm::SmallVector<const DeclRefExpr *, 2> Variables;
  // Each expression once, with the most depth left, not once for each path to
  // it, like the pointers of a chain of conditionals, `int **ppNext = bFirst ?
  // ppPrevious : ppPrevious`, whose number doubles with each link.
  llvm::DenseMap<const Expr *, int> Collected, Targeted;
  auto First = [](llvm::DenseMap<const Expr *, int> &Met, const Expr *E,
                  int Depth) {
    auto [It, Inserted] = Met.try_emplace(E, Depth);
    if (!Inserted && It->second <= Depth)
      return false;
    It->second = Depth;
    return true;
  };
  std::function<void(const Expr *, int)> Collect = [&](const Expr *E,
                                                      int Depth) {
    if (!E || Depth > 8)
      return;
    E = E->IgnoreParenImpCasts();
    if (!First(Collected, E, Depth))
      return;
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E); Cast && Cast->isGLValue())
      return Collect(Cast->getSubExpr(), Depth + 1);
    if (const auto *Conditional = dyn_cast<ConditionalOperator>(E);
        Conditional && Conditional->isGLValue()) {
      Collect(Conditional->getTrueExpr(), Depth + 1);
      Collect(Conditional->getFalseExpr(), Depth + 1);
      return;
    }
    if (const auto *Comma = dyn_cast<BinaryOperator>(E);
        Comma && Comma->getOpcode() == BO_Comma)
      return Collect(Comma->getRHS(), Depth + 1);
    // What a local pointer to it points to where it is written, `*ppValue`
    // with `int **ppValue = &pValue`, also through a copy of that, `int
    // **ppCopy = ppValue`, either of a conditional, `bChoose ? &pFirst :
    // &pSecond`, and a cast of the pointer, `*static_cast<int **>(&pValue)`.
    if (const auto *Deref = dyn_cast<UnaryOperator>(E);
        Deref && Deref->getOpcode() == UO_Deref) {
      std::function<void(const Expr *, int)> Targets = [&](const Expr *Value,
                                                           int Level) {
        if (!Value || Level > 16)
          return;
        Value = Value->IgnoreParenImpCasts();
        if (!First(Targeted, Value, Level))
          return;
        if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Value);
            Cast && Cast->getSubExpr()->getType()->isPointerType())
          return Targets(Cast->getSubExpr(), Level + 1);
        if (const auto *Address = dyn_cast<UnaryOperator>(Value);
            Address && Address->getOpcode() == UO_AddrOf)
          return Collect(Address->getSubExpr(), Depth + 1);
        // Also `std::addressof(pValue)` or `__builtin_addressof(pValue)`.
        if (const auto *AddressOf = dyn_cast<CallExpr>(Value);
            isAddressOfCall(AddressOf))
          return Collect(AddressOf->getArg(0), Depth + 1);
        if (const auto *Conditional = dyn_cast<ConditionalOperator>(Value)) {
          Targets(Conditional->getTrueExpr(), Level + 1);
          Targets(Conditional->getFalseExpr(), Level + 1);
          return;
        }
        const auto *Ref = dyn_cast<DeclRefExpr>(Value);
        const auto *Pointer = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        if (!Pointer || !Pointer->hasLocalStorage() ||
            isa<ParmVarDecl>(Pointer))
          return;
        Targets(assignedValueAt(Pointer, Ref), Level + 1);
      };
      Targets(Deref->getSubExpr(), 0);
      return;
    }
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E);
        Ref && !llvm::is_contained(Variables, Ref))
      Variables.push_back(Ref);
  };
  Collect(Left, 0);
  return Variables;
}

bool isJoinedBlocking(const LambdaExpr *Lambda, ASTContext &Context) {
  return isRunByBlockingCallees(Lambda, Context);
}

} // namespace clang::tidy::malterlib
