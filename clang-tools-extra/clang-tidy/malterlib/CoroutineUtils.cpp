//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CoroutineUtils.h"
#include "CheckedAccess.h"
#include "ActorAnnotations.h"
#include "HeldData.h"
#include "MalterlibCheck.h"
#include "NamingUtils.h"
#include "OncePerTypeVisitor.h"
#include "SourceOrder.h"
#include "StorageModel.h"
#include "ValueFlow.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/IgnoreExpr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Sema/Sema.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ASTWriter.h"
#include "clang/Serialization/ModuleFile.h"
#include "clang/Serialization/ModuleFileExtension.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Support/Endian.h"
#include <cstdlib>
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
        Call->getNumArgs() >= 1 &&
        Call->getDirectCallee() &&
        refersToOwnObject(Call->getArg(0),
                          &Call->getDirectCallee()->getASTContext())) {
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
    if (Current->getIdentifier() && (identifierTextOf(Current) == "CSuspendNever" ||
                                     identifierTextOf(Current) == "suspend_never"))
      return true;
    if (!Current->hasDefinition())
      continue;
    // One derived from it that has its own `await_ready`, which can suspend.
    if (llvm::any_of(Current->getDefinition()->methods(),
                     [](const CXXMethodDecl *Method) {
                       return Method->getIdentifier() &&
                              (identifierTextOf(Method) == "await_ready" ||
                               identifierTextOf(Method) == "await_suspend");
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

static bool lambdaNeverSuspendsUncached(const FunctionDecl *Operator);
static bool isUnchangedLocalUncached(const VarDecl *Variable);

/// What is told of an AST, which doesn't change while the checks look at
/// it, kept until the AST is gone, so that what is asked many times, like
/// about the locals of a long function at each of their uses, or about a
/// lambda each call of it, is worked out once.
struct CASTFacts {
  /// Whether a local keeps its initial value; while that is told, it
  /// doesn't.
  TCSummaries<const VarDecl *, bool> Unchanged{&isUnchangedLocalUncached, false,
                                               false};
  /// The variables that each function names other than to read their value,
  /// which only those can change.
  llvm::DenseMap<const FunctionDecl *, llvm::DenseSet<const ValueDecl *>>
      NamedOtherThanRead;
  /// Whether the call operator of a lambda coroutine never suspends; one
  /// that calls itself may suspend where it does, which is taken of it while
  /// it is told.
  TCSummaries<const FunctionDecl *, bool> NonSuspending{
      &lambdaNeverSuspendsUncached, false, false};
};

static CASTFacts &astFacts(const ASTContext &Context) {
  return astCache<CASTFacts>(Context);
}

static bool isSuspensionImpl(const Stmt *S, int Depth);

static bool lambdaNeverSuspends(const FunctionDecl *Operator);

/// Whether \p Type is a future of Malterlib, `TCFuture` or
/// `TCFutureWithFlags`, also through an alias, of which `TCUnsafeFuture` is
/// one.
static bool isMalterlibFutureType(QualType Type) {
  const StringRef Name = recordName(Type.getCanonicalType());
  return Name == "TCFuture" || Name == "TCFutureWithFlags";
}

bool coroutineStartsWhereCalled(const FunctionDecl *Coroutine) {
  if (isMalterlibFutureType(Coroutine->getReturnType()))
    return true;
  // The type of the awaiter `initial_suspend` returns, as declared, of the
  // full expression of the initial suspend.
  const auto *Body = dyn_cast_or_null<CoroutineBodyStmt>(Coroutine->getBody());
  const Stmt *Initial = Body ? Body->getInitSuspendStmt() : nullptr;
  if (const auto *Cleanups = dyn_cast_or_null<ExprWithCleanups>(Initial))
    Initial = Cleanups->getSubExpr();
  const auto *Suspend = dyn_cast_or_null<CoroutineSuspendExpr>(Initial);
  const CXXRecordDecl *Awaiter =
      Suspend && Suspend->getOperand()
          ? Suspend->getOperand()->getType()->getAsCXXRecordDecl()
          : nullptr;
  if (!Awaiter)
    return true;
  if (Awaiter->getIdentifier() && identifierTextOf(Awaiter) == "suspend_always" &&
      Awaiter->getDeclContext()->getEnclosingNamespaceContext()->isStdNamespace())
    return false;
  // Of another awaiter, what its `await_ready()` gives where that is a
  // constant expression: its body only returns one.
  const CXXRecordDecl *Definition = Awaiter->getDefinition();
  if (!Definition)
    return true;
  for (const CXXMethodDecl *Method : Definition->methods()) {
    if (!Method->getIdentifier() || identifierTextOf(Method) != "await_ready" ||
        Method->getNumParams() != 0)
      continue;
    const FunctionDecl *Seen = Method->getDefinition();
    const auto *Block =
        Seen ? dyn_cast_or_null<CompoundStmt>(Seen->getBody()) : nullptr;
    const auto *Return =
        Block && Block->size() == 1 ? dyn_cast<ReturnStmt>(Block->body_front())
                                    : nullptr;
    const Expr *Ready = Return ? Return->getRetValue() : nullptr;
    bool Value = true;
    if (Ready && !Ready->isValueDependent() &&
        foldsCondition(Ready, Value, Coroutine->getASTContext()))
      return Value;
    return true;
  }
  return true;
}

/// Whether \p Function is a coroutine that returns a future of Malterlib
/// whose body starts in the call (coroutineStartsWhereCalled): one that
/// doesn't suspend gives a future that is ready.
static bool isEagerFutureCoroutine(const FunctionDecl *Function) {
  return Function && isCoroutine(Function) &&
         isMalterlibFutureType(Function->getReturnType()) &&
         coroutineStartsWhereCalled(Function);
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
  } else if (const CMemberCall Member =
                 memberCallOf(skipWrappers(Operand));
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
  // but not a virtual one an override can replace, unless the call names
  // it, `co_await CBaseActor::f_Init()` (callsOverride).
  if (const auto *Direct = dyn_cast<CallExpr>(skipWrappers(Operand));
      !Call && Direct && Direct->getDirectCallee() &&
      !isa<CXXOperatorCallExpr>(Direct)) {
    const FunctionDecl *Callee = Direct->getDirectCallee()->getDefinition();
    if (!Callee || !Callee->getBody() || !isEagerFutureCoroutine(Callee) ||
        callsOverride(Direct))
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
  return astFacts(Operator->getASTContext()).NonSuspending.get(Operator);
}

bool callsNonSuspendingCoroutine(const CallExpr *Call) {
  const FunctionDecl *Callee = Call ? calledDefinition(Call) : nullptr;
  return Callee && isEagerFutureCoroutine(Callee) &&
         lambdaNeverSuspends(Callee);
}

static bool lambdaNeverSuspendsUncached(const FunctionDecl *Operator) {
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
      return OncePerTypeVisitor::TraverseStmt(S);
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
        if (const std::optional<Stmt *> Case = nondiscardedCaseOf(If)) {
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
          foldsCondition(If->getCond(), Taken, Context) &&
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
          foldsCondition(While->getCond(), Runs, Context) && !Runs &&
          !hasLabel(While->getBody()))
        return TraverseStmt(While->getCond());
      return RecursiveASTVisitor::TraverseWhileStmt(While);
    }
    bool TraverseForStmt(ForStmt *For) {
      bool Runs = true;
      if (For->getCond() && !For->getConditionVariable() &&
          !For->getCond()->isValueDependent() &&
          foldsCondition(For->getCond(), Runs, Context) && !Runs &&
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
          foldsCondition(Conditional->getCond(), Chosen, Context))
        return TraverseStmt(Conditional->getCond()) &&
               TraverseStmt(Chosen ? Conditional->getTrueExpr()
                                   : Conditional->getFalseExpr());
      return RecursiveASTVisitor::TraverseConditionalOperator(Conditional);
    }
    bool TraverseBinaryOperator(BinaryOperator *Binary) {
      bool Left = false;
      if (Binary->isLogicalOp() && !Binary->getLHS()->isValueDependent() &&
          foldsCondition(Binary->getLHS(), Left, Context) &&
          Left == (Binary->getOpcode() == BO_LOr))
        return TraverseStmt(Binary->getLHS());
      return RecursiveASTVisitor::TraverseBinaryOperator(Binary);
    }
    static bool hasLabel(const Stmt *S) {
      if (!S)
        return false;
      if (isa<LabelStmt, SwitchCase>(S))
        return true;
      return llvm::any_of(keptChildren(S), hasLabel);
    }
    static bool isAsyncDestroy(const Stmt *S) {
      const auto *Call = dyn_cast_or_null<CallExpr>(S);
      return Call && llvm::is_contained(
                         llvm::ArrayRef<StringRef>(AsyncDestroyFunctions),
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
          const CMemberCall Clear = isa<Expr>(*Next)
                                  ? memberCallOf(
                                        cast<Expr>(*Next)->IgnoreImplicit())
                                  : CMemberCall();
          const auto *Object =
              Clear && Clear->getImplicitObjectArgument()
                  ? dyn_cast<DeclRefExpr>(Clear->getImplicitObjectArgument()
                                              ->IgnoreParenImpCasts())
                  : nullptr;
          if (Object && Object->getDecl() == Guard && Clear->getMethodDecl() &&
              Clear->getMethodDecl()->getIdentifier() &&
              identifierTextOf(Clear->getMethodDecl()) == "f_Clear") {
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
      return llvm::any_of(keptChildren(S), mayLeave);
    }
    bool VisitStmt(Stmt *S) {
      if (isa<CoroutineSuspendExpr>(S) && isSuspensionImpl(S, Depth + 1))
        Found = true;
      // A cleanup of `fg_AsyncDestroy`, which it joins when it finishes, can
      // suspend it there, unless it is cancelled first, or is a coroutine
      // lambda that can be seen not to suspend, whose future is ready when
      // it is joined.
      if (const auto *Call = dyn_cast<CallExpr>(S);
          Call && isAsyncDestroy(Call) && !Cancelled.contains(S) &&
          !isReadyCleanup(Call))
        Found = true;
      return !Found;
    }
    static bool isReadyCleanup(const CallExpr *Call) {
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
  return !Finder.Found;
}

std::optional<QualType> argumentParameterType(const Expr *Call,
                                              unsigned iArgument) {
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(Call)) {
    const CXXConstructorDecl *Constructor = Construct->getConstructor();
    if (!Constructor || iArgument >= Constructor->getNumParams())
      return std::nullopt;
    return Constructor->getParamDecl(iArgument)->getType();
  }
  const auto *Called = dyn_cast<CallExpr>(Call);
  if (!Called)
    return std::nullopt;
  const FunctionDecl *Callee = Called->getDirectCallee();
  const unsigned Offset = argumentShift(Called, Callee);
  if (iArgument < Offset)
    return std::nullopt;
  const unsigned iParameter = iArgument - Offset;
  if (Callee) {
    if (iParameter >= Callee->getNumParams())
      return std::nullopt;
    return Callee->getParamDecl(iParameter)->getType();
  }
  QualType Type = Called->getCallee()->IgnoreParenImpCasts()->getType();
  if (Type.isNull())
    return std::nullopt;
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
  if (!Prototype || iParameter >= Prototype->getNumParams())
    return std::nullopt;
  return Prototype->getParamType(iParameter);
}

bool suspendsIn(const Stmt *S, const ASTContext &Context) {
  if (stackNearlyExhausted())
    return withStack([&] { return suspendsIn(S, Context); });
  if (!S)
    return false;
  // Making a lambda runs what initializes its captures, `[Value = co_await
  // fg_Work()]`, not its body.
  if (const auto *Lambda = dyn_cast<LambdaExpr>(S))
    return llvm::any_of(captureInitsOf(Lambda), [&](const Expr *Init) {
      return Init && suspendsIn(Init, Context);
    });
  if (isa<CoroutineSuspendExpr>(S) && isSuspension(S))
    return true;
  auto Constant = [&](const Expr *Condition, bool &Value) {
    return !Condition->isValueDependent() &&
           foldsCondition(Condition, Value, Context);
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
  return llvm::any_of(keptChildren(S), [&](const Stmt *Child) {
    return suspendsIn(Child, Context);
  });
}

const Expr *accessedAfterSuspension(const Stmt *Access,
                                    const ASTContext &Context) {
  // Whether an offset added to the pointer \p Pointer suspends, also nested,
  // `(pValue + co_await fg_Offset()) + 1`, after the pointer is read.
  TCRecursive<bool(const Expr *, int)> OffsetSuspends =
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
  TCRecursive<void(const Expr *, int)> Add = [&](const Expr *Value,
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
        const llvm::SmallVector<const Expr *, 2> Arms = keptArms(Conditional);
        for (const Expr *Arm : llvm::drop_end(Arms))
          Add(Arm, Depth + 1);
        Value = Arms.back();
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
        Named(identifierTextOf(Typedef->getDecl())))
      return true;
    const QualType Next =
        Sugared->getLocallyUnqualifiedSingleStepDesugaredType();
    if (Next == Sugared)
      break;
    Sugared = Next;
  }
  const auto *Record = Type->getAsCXXRecordDecl();
  return Record && Record->getIdentifier() && Named(identifierTextOf(Record));
}

bool hasJumps(const Stmt *S) {
  if (stackNearlyExhausted())
    return withStack([&] { return hasJumps(S); });
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
      foldsCondition(Returned, Value, Context))
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
  const CMemberCall Member = memberCallOf(Call);
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
    if (Owner->getIdentifier() && (identifierTextOf(Owner) == "CSuspendNever" ||
                                   identifierTextOf(Owner) == "suspend_never"))
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
  // The initial and final suspends of a promise, the only implicit ones.
  if (const auto *Await = dyn_cast<CoawaitExpr>(S);
      Await && Await->isImplicit())
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
      Enum && Enum->getIdentifier() && identifierTextOf(Enum) == "ECoroutineFlag")
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
         !asCallTo(Operand, llvm::ArrayRef<StringRef>(AsyncDestroyFunctions)) &&
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
      for (const Stmt *Child : keptChildren(S))
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

namespace {
/// Of each body, the statements that can run (canRunIn), none when its
/// control flow can't be built.
struct CRunnableFacts {
  llvm::DenseMap<const Stmt *, std::optional<llvm::DenseSet<const Stmt *>>>
      Runnable;
};
} // namespace

namespace {
/// Of each closure's call operator, whether the lambda that makes it can run
/// (runsWhereItIs).
struct CClosureRuns {
  llvm::DenseMap<const FunctionDecl *, bool> Runs;
};
} // namespace

bool runsWhereItIs(const Stmt *S, ASTContext &Context) {
  // The innermost function S is in the body or a member initializer of, of a
  // lambda its call operator. What isn't in one, a default argument, runs
  // where it is used, as far as it is evaluated (CEffectIndex::evaluated) in
  // the outermost expression it is in, `false && (...)`, and not in what
  // isn't evaluated, like the operand of a `decltype` in a requires clause.
  // What a name of a structured binding designates is evaluated where the
  // binding is declared. Of a node an instantiation shares with its template,
  // the instantiation's (instantiatedParents).
  const FunctionDecl *Owner = nullptr;
  // Of a closure, the lambda that makes it, which runs only where that can.
  const LambdaExpr *MadeBy = nullptr;
  DynTypedNode Node = DynTypedNode::create(*S);
  const Stmt *Child = S;
  const Stmt *Root = S;
  const Stmt *At = S;
  bool InInitializer = false;
  bool InBinding = false;
  auto OutsideBody = [&] {
    return !isInUnevaluatedOperand(DynTypedNode::create(*S), Context) &&
           sharedFlowCache().indexOf(Root, Context).evaluated(S);
  };
  for (int Depth = 0; Depth < 4096 && !Owner; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      break;
    Node = Parents[0];
    if (const auto *Lambda = Node.get<LambdaExpr>();
        Lambda && Child && Child == Lambda->getBody()) {
      Owner = Lambda->getCallOperator();
      MadeBy = Lambda;
    } else if (const auto *Function = Node.get<FunctionDecl>()) {
      if (!InInitializer && (!Child || Child != Function->getBody()))
        return OutsideBody();
      Owner = Function;
    }
    InInitializer |= Node.get<CXXCtorInitializer>() != nullptr;
    if (Node.get<BindingDecl>())
      InBinding = true;
    Child = Node.get<Stmt>();
    if (Child && !Owner) {
      if (InBinding) {
        At = Child;
        InBinding = false;
      }
      if (At == S)
        Root = Child;
    }
  }
  if (!Owner)
    return OutsideBody();
  // A template's own code, which only its instantiations tell, can run.
  const Stmt *Body = Owner->getBody();
  if (!Body || isTemplatedCode(Owner))
    return true;
  // The element of the CFG, which has no parentheses.
  const Stmt *Element = isa<Expr>(At) ? cast<Expr>(At)->IgnoreParens() : At;
  if (!canRun(Owner, Body, Element, Context) ||
      !sharedFlowCache().indexOf(Body, Context).evaluated(At) ||
      (At != S && !sharedFlowCache().indexOf(Root, Context).evaluated(S)))
    return false;
  const auto *Operator = dyn_cast<CXXMethodDecl>(Owner);
  if (!Operator || !Operator->getParent()->isLambda())
    return true;
  // Each closure told once: the lambda of its call operator and whether that
  // runs, which each statement of its body asks.
  if (const auto It = astCache<CClosureRuns>(Context).Runs.find(Operator);
      It != astCache<CClosureRuns>(Context).Runs.end())
    return It->second;
  if (!MadeBy)
    MadeBy = lambdaOfCallOperator(Operator, Context);
  const bool Runs = !MadeBy || runsWhereItIs(MadeBy, Context);
  astCache<CClosureRuns>(Context).Runs[Operator] = Runs;
  return Runs;
}




const FunctionDecl *bodyFunctionOf(const Stmt *S, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*S);
  const Stmt *Child = S;
  bool InInitializer = false;
  for (int Depth = 0; Depth < 4096; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    Node = Parents[0];
    if (const auto *Lambda = Node.get<LambdaExpr>();
        Lambda && Child && Child == Lambda->getBody())
      return Lambda->getCallOperator();
    if (const auto *Function = Node.get<FunctionDecl>())
      return InInitializer || (Child && Child == Function->getBody())
                 ? Function
                 : nullptr;
    InInitializer |= Node.get<CXXCtorInitializer>() != nullptr;
    Child = Node.get<Stmt>();
  }
  return nullptr;
}

bool canRunIn(const Stmt *S, const Stmt *Body, const Decl *Owner,
              ASTContext &Context) {
  if (!S || !Body)
    return true;
  auto &Runnable = astCache<CRunnableFacts>(Context).Runnable;
  auto [It, Inserted] = Runnable.try_emplace(Body);
  if (Inserted) {
    CFG::BuildOptions Options;
    Options.AddInitializers = true;
    Options.AddCXXDefaultInitExprInCtors = true;
    std::unique_ptr<CFG> Graph = buildGraph(Owner, Body, Context, Options);
    std::optional<llvm::DenseSet<const Stmt *>> Statements;
    if (Graph) {
      addExceptionEdges(*Graph, Context);
      Statements.emplace();
      const llvm::BitVector Reachable = reachableBlocks(*Graph);
      llvm::SmallVector<const Stmt *, 64> Work;
      for (const CFGBlock *Block : *Graph) {
        if (!Block || !Reachable.test(Block->getBlockID()))
          continue;
        for (const CFGElement &Element : *Block)
          if (const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>())
            Work.push_back(Statement->getStmt());
          else if (const std::optional<CFGInitializer> Initializer =
                       Element.getAs<CFGInitializer>())
            Work.push_back(Initializer->getInitializer()->getInit());
        // The terminator itself, not the branches in it.
        if (const Stmt *Terminator = Block->getTerminatorStmt())
          Statements->insert(Terminator);
      }
      while (!Work.empty()) {
        const Stmt *Each = Work.pop_back_val();
        if (!Each || !Statements->insert(Each).second)
          continue;
        llvm::append_range(Work, Each->children());
      }
    }
    // The entry may have moved while the graph was built.
    Runnable[Body] = std::move(Statements);
    It = Runnable.find(Body);
  }
  return !It->second || It->second->contains(S);
}

static bool copiedInto(const VarDecl *Local, const FunctionDecl *Function) {
  const auto *Operator = dyn_cast_or_null<CXXMethodDecl>(Function);
  const CXXRecordDecl *Closure = Operator ? Operator->getParent() : nullptr;
  return Closure && Closure->isLambda() &&
         llvm::any_of(Closure->captures(), [&](const LambdaCapture &Capture) {
           return Capture.capturesVariable() &&
                  Capture.getCaptureKind() == LCK_ByCopy &&
                  Capture.getCapturedVar() == Local;
         });
}

namespace {
/// Of each statement, the closure whose body it is in (enclosingClosure),
/// once told.
struct CEnclosingClosures {
  llvm::DenseMap<const Stmt *, const LambdaExpr *> Of;
};
} // namespace

/// The nearest lambda whose body \p At is in, by its parents up to the
/// function it is in; null of one in none. What is told of each statement on
/// the way is kept for each of them, so each is gone through once.
static const LambdaExpr *enclosingClosure(const Stmt *At,
                                          ASTContext &Context) {
  auto &Known = astCache<CEnclosingClosures>(Context).Of;
  llvm::SmallVector<const Stmt *, 32> Passed;
  const LambdaExpr *Found = nullptr;
  bool Told = false;
  DynTypedNode Node = DynTypedNode::create(*At);
  const Stmt *From = At;
  for (int Step = 0; Step < 1024 && !Told; ++Step) {
    if (From) {
      if (const auto It = Known.find(From); It != Known.end()) {
        Found = It->second;
        Told = true;
        break;
      }
      Passed.push_back(From);
    }
    const auto Parents = Context.getParents(Node);
    if (Parents.empty()) {
      Told = true;
      break;
    }
    for (const DynTypedNode &Parent : Parents)
      if (const auto *Lambda = Parent.get<LambdaExpr>();
          Lambda && From && From == Lambda->getBody()) {
        Found = Lambda;
        Told = true;
      }
    if (Told || Parents[0].get<FunctionDecl>()) {
      Told = true;
      break;
    }
    From = Parents[0].get<Stmt>();
    Node = Parents[0];
  }
  // Past as many parents as are followed, of none, only for \p At itself.
  if (!Told)
    return nullptr;
  for (const Stmt *Each : Passed)
    Known[Each] = Found;
  return Found;
}

const LambdaExpr *closureCopying(const VarDecl *Local, const Stmt *At) {
  ASTContext &Context = Local->getASTContext();
  for (const LambdaExpr *Lambda = enclosingClosure(At, Context); Lambda;
       Lambda = enclosingClosure(Lambda, Context))
    if (copiedInto(Local, Lambda->getCallOperator()))
      return Lambda;
  return nullptr;
}

std::optional<llvm::SmallVector<const Expr *, 4>>
localValuesAt(const VarDecl *Local, const Stmt *At) {
  const auto *Function =
      Local ? dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod())
            : nullptr;
  if (!Function || !Function->getBody() || !Local->hasLocalStorage() ||
      (isa<ParmVarDecl>(Local) && Local->getType()->isReferenceType()))
    return std::nullopt;
  // A reference is what it is bound to.
  if (Local->getType()->isReferenceType()) {
    if (!Local->getInit())
      return std::nullopt;
    return llvm::SmallVector<const Expr *, 4>{Local->getInit()};
  }
  // What reaches there in the flow of the function (localSlotValues).
  ASTContext &Context = Function->getASTContext();
  const CSlotValues Held = localSlotValues({Local, {}, false}, At, Context);
  if (!Held.Told)
    return std::nullopt;
  // The values, each value of a conditional apart.
  llvm::SmallVector<const Expr *, 4> Values;
  TCRecursive<void(const Expr *, int)> Add = [&](const Expr *Value,
                                                   int Depth) {
    const Expr *Bare = Value->IgnoreParenImpCasts();
    // Of a choice, what a constant condition chooses (chosenValue), else
    // each.
    if (const auto *Choice = dyn_cast<AbstractConditionalOperator>(Bare);
        Choice && Depth < 8) {
      if (const Expr *Chosen = chosenValue(Choice, &Context))
        return Add(Chosen, Depth + 1);
      for (const Expr *Arm : keptArms(Choice))
        Add(Arm, Depth + 1);
      return;
    }
    if (!llvm::is_contained(Values, Value))
      Values.push_back(Value);
  };
  for (const CSlotValue &Each : Held.Values) {
    switch (Each.Kind) {
    // A value given it; of an address a setter gives it, the location whose
    // address it is, as what the setter is given there (setParameters).
    case CSlotValue::EKind::Value:
    case CSlotValue::EKind::Address:
      if (!Each.Value)
        return std::nullopt;
      Add(Each.Value, 0);
      break;
    // What a parameter is given isn't a value of the function.
    case CSlotValue::EKind::Given:
      break;
    // What it held, stepped.
    case CSlotValue::EKind::Stored:
      return std::nullopt;
    }
  }
  return Values;
}

const FieldDecl *assignedMember(const Expr *Target,
                                const FunctionDecl *Function) {
  // A local of it made with \p Init.
  auto LocalOf = [&](const Expr *E) -> const VarDecl * {
    const auto *Ref = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    return Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
                   Local->getInit() && Local->getDeclContext() == Function
               ? Local
               : nullptr;
  };
  for (int Step = 0; Target && Step < 8; ++Step) {
    Target = Target->IgnoreParenImpCasts();
    if (const VarDecl *Alias = LocalOf(Target);
        Alias && Alias->getType()->isReferenceType()) {
      Target = Alias->getInit();
      continue;
    }
    const auto *Member = dyn_cast<MemberExpr>(Target);
    const auto *Field =
        Member ? dyn_cast<FieldDecl>(Member->getMemberDecl()) : nullptr;
    if (!Field)
      return nullptr;
    const Expr *Base = Member->getBase()->IgnoreParenImpCasts();
    if (const auto *Deref = dyn_cast<UnaryOperator>(Base);
        Deref && Deref->getOpcode() == UO_Deref)
      Base = Deref->getSubExpr()->IgnoreParenImpCasts();
    if (isa<CXXThisExpr>(Base))
      return Field;
    if (const VarDecl *Self = LocalOf(Base);
        Self && Self->getType()->isPointerType() && !isChangedVariable(Self) &&
        isa<CXXThisExpr>(Self->getInit()->IgnoreParenImpCasts()))
      return Field;
    return nullptr;
  }
  return nullptr;
}

static bool isUnchangedLocal(const VarDecl *Variable);

/// The parameters whose arguments \p Definition gives back as they are on
/// every return, `Map &fg_Second(Map &, Map &_Result) { return _Result; }`,
/// also either of a conditional, `return _bFirst ? _First : _Second;`, or
/// None when it can return something else.
static std::optional<llvm::SmallVector<unsigned, 2>>
returnedParameters(const FunctionDecl *Definition) {
  llvm::SmallVector<unsigned, 2> Returned;
  bool Other = false;
  TCRecursive<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Other || isa<LambdaExpr>(S))
      return;
    if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
      // Also either value of a conditional, `return _bFirst ? _First :
      // _Second;`.
      TCRecursive<void(const Expr *, int)> Add = [&](const Expr *Value,
                                                       int Depth) {
        Value = Value ? Value->IgnoreParenImpCasts() : nullptr;
        if (const auto *Conditional =
                dyn_cast_or_null<ConditionalOperator>(Value);
            Conditional && Depth < 8) {
          for (const Expr *Arm : keptArms(Conditional))
            Add(Arm, Depth + 1);
          return;
        }
        // Also cast to a reference to what it is, `return
        // static_cast<t_CType &&>(_Value);` of a move.
        if (const auto *Cast = dyn_cast_or_null<ExplicitCastExpr>(Value);
            Cast && Cast->isGLValue() && Cast->getCastKind() == CK_NoOp &&
            Depth < 8) {
          Add(Cast->getSubExpr(), Depth + 1);
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
    for (const Stmt *Child : keptChildren(S))
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
         (identifierTextOf(Callee) == "invoke" || identifierTextOf(Callee) == "invoke_r") &&
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
  } else if (const CMemberCall Member = memberCallOf(E);
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
  TCRecursive<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return;
    // Of what can run (canRunIn).
    if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
      if (const Expr *Value = Return->getRetValue();
          Value && canRunIn(Return, Definition->getBody(), Definition,
                            const_cast<ASTContext &>(Context))) {
        Values.push_back(Value);
        // A local returned, what it holds there, when that can be told,
        // `q = &g_Value; return q;`, not what it was before; else all it is
        // made from and assigned.
        const auto *Ref = dyn_cast<DeclRefExpr>(Value->IgnoreParenImpCasts());
        const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        // What it holds there (localValuesAt).
        llvm::SmallVector<const Expr *, 4> Held;
        if (Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
            Local->getDeclContext() == Definition &&
            !Local->getType()->isReferenceType()) {
          if (const std::optional<llvm::SmallVector<const Expr *, 4>> Values =
                  localValuesAt(Local, Ref);
              Values && !Values->empty())
            Held = *Values;
          else
            llvm::append_range(Held, assignedValuesAt(Local, Ref));
        }
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
  TCRecursive<void(const Stmt *)> Collect = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->getOpcode() == BO_Assign &&
        canRunIn(Assign, Definition->getBody(), Definition,
                 const_cast<ASTContext &>(Context)))
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
  if (!Call || isMemberOrOperatorCall(Call))
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
  /// The parent of \p S, as FParentOf asks it.
  const Stmt *operator()(const Stmt *S) const { return getParent(S); }

private:
  void build(const Stmt *S, bool Transparent) {
    if (!S)
      return;
    if (stackNearlyExhausted())
      return withStack([&] { build(S, Transparent); });
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
  /// `co_await ECoroutineFlag_CaptureExceptions`, of a body a branch of which
  /// points a parameter elsewhere (BranchRedirections).
  llvm::SmallVector<const Stmt *, 8> Suspending;
  /// Whether \p Other tells the same, as what suspends tells it.
  bool operator==(const SuspensionFlow &Other) const {
    return Built == Other.Built && Suspends == Other.Suspends &&
           After == Other.After && Suspending == Other.Suspending;
  }
};

} // namespace

/// Whether \p Decl matches \p Pattern: a qualified name matches the qualified
/// name, a leading `*` matches the end of the name, and a trailing one its
/// start.
static bool matchesName(const NamedDecl *Decl, StringRef Pattern) {
  if (!Decl->getIdentifier())
    return false;
  if (Pattern.consume_front("*"))
    return identifierTextOf(Decl).ends_with(Pattern);
  if (Pattern.consume_back("*"))
    return identifierTextOf(Decl).starts_with(Pattern);
  if (Pattern.contains("::"))
    return hasQualifiedName(Decl, Pattern);
  return identifierTextOf(Decl) == Pattern;
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

bool isSharedPointerType(QualType Type, const ASTContext &Context) {
  const EOwnerKind Kind = ownerKindOf(Type, Context);
  return Kind == EOwnerKind::Shared || Kind == EOwnerKind::Weak;
}

namespace {
/// Of each record, whether its layout has room for data in it (hasRoomInside),
/// once in a translation unit.
struct CRoomInside {
  llvm::DenseMap<const CXXRecordDecl *, bool> Records;
};
} // namespace

/// Whether the layout of \p Record, its bases and its members held by value,
/// at any depth, unions included, has an array member: room for data in the
/// object itself, like an inline or static allocator or a small-buffer
/// string. A record whose definition can't be seen has none.
static bool hasRoomInside(const CXXRecordDecl *Record,
                          const ASTContext &Context) {
  auto &Records = astCache<CRoomInside>(Context).Records;
  if (auto Found = Records.find(Record); Found != Records.end())
    return Found->second;
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
  llvm::SmallVector<const CXXRecordDecl *, 8> Work{Record};
  bool Room = false;
  while (!Work.empty() && !Room) {
    const CXXRecordDecl *At = Work.pop_back_val();
    if (!At || !Seen.insert(At).second)
      continue;
    const CXXRecordDecl *Definition = At->getDefinition();
    if (!Definition)
      continue;
    for (const CXXBaseSpecifier &Base : Definition->bases())
      Work.push_back(Base.getType()->getAsCXXRecordDecl());
    for (const CXXBaseSpecifier &Base : Definition->vbases())
      Work.push_back(Base.getType()->getAsCXXRecordDecl());
    for (const FieldDecl *Field : Definition->fields()) {
      const QualType Type = Field->getType();
      if (Type->isArrayType()) {
        Room = true;
        break;
      }
      if (!Type->isReferenceType())
        Work.push_back(Type->getAsCXXRecordDecl());
    }
  }
  Records[Record] = Room;
  return Room;
}

bool isBufferOwningType(QualType Type, const ASTContext &Context) {
  Type = Type.getNonReferenceType().getCanonicalType().getUnqualifiedType();
  if (!matchesTypeName(Type, vocabulary().BufferOwnerTypes.names(), Context) &&
      !matchesTypeName(Type, vocabulary().SharedBufferOwnerTypes.names(),
                       Context))
    return false;
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  return !Record || !hasRoomInside(Record, Context);
}

// Whether \p Type is a function object of SynchronousFunctionTypes, which
// calls what it is made of where it is called: a local of one made of a
// closure is that closure.
bool isSynchronousFunctionType(QualType Type, const ASTContext &Context) {
  return matchesTypeName(Type.getNonReferenceType(),
                         vocabulary().SynchronousFunctionTypes.names(),
                         Context);
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

static llvm::DenseMap<const VarDecl *, llvm::SmallVector<const Stmt *, 2>>
parameterRedirections(
    const Stmt *Body, ASTContext &Context,
    llvm::DenseMap<const VarDecl *, llvm::SmallVector<const IfStmt *, 1>>
        &BranchRedirections);

/// The control flow of \p Function as a suspension flow (SuspensionFlow),
/// with where the body points its parameters elsewhere and, of one a branch
/// of which does, what suspends.
static SuspensionFlow suspensionFlowOf(const FunctionDecl *Function) {
  ASTContext &Context = Function->getASTContext();
  SuspensionFlow Flow = suspensionFlowUncached(Function, Context);
  Stmt *Body = writtenBody(Function);
  if (!Body)
    return Flow;
  Flow.Parents = std::make_unique<CParents>(Body);
  Flow.Redirections =
      parameterRedirections(Body, Context, Flow.BranchRedirections);
  if (Flow.BranchRedirections.empty())
    return Flow;
  // Nor those of the lambdas it makes, which suspend those.
  TCRecursive<void(const Stmt *)> Collect = [&](const Stmt *S) {
    if (!S)
      return;
    if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
      for (const Expr *Init : Lambda->capture_inits())
        Collect(Init);
      return;
    }
    if (isa<CoroutineSuspendExpr>(S) && isSuspension(S))
      Flow.Suspending.push_back(S);
    for (const Stmt *Child : keptChildren(S))
      Collect(Child);
  };
  Collect(Body);
  return Flow;
}

namespace {
/// The control flows of the functions of an AST, which each parameter and
/// local of a function asks about, each made once (TCSummaries).
struct CFlowFacts {
  TCSummaries<const FunctionDecl *, SuspensionFlow> Flows{
      &suspensionFlowOf,
      [](const FunctionDecl *const &) { return SuspensionFlow(); },
      [](const FunctionDecl *const &) { return SuspensionFlow(); }};
};
} // namespace

static const SuspensionFlow &suspensionFlow(const FunctionDecl *Function,
                                            ASTContext &Context) {
  return astCache<CFlowFacts>(Context).Flows.get(Function);
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
  Flow.Graph = buildGraph(Function, Body, Context, Options);
  const std::unique_ptr<CFG> &Graph = Flow.Graph;
  // Exceptions can reach handlers after a suspension (addExceptionEdges).
  if (Graph)
    addExceptionEdges(*Graph, Context);
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
bool mayRunAfterSuspension(const Stmt *S, const FunctionDecl *Function,
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
      return traverseStaticBase(Member->getBase());
    return RecursiveASTVisitor::TraverseMemberExpr(Member);
  }
  bool traverseStaticBase(Expr *Base) {
    if (stackNearlyExhausted())
      return withStack([&] { return traverseStaticBase(Base); });
    Base = Base->IgnoreParenImpCasts();
    if (const auto *Deref = dyn_cast<UnaryOperator>(Base);
        Deref && Deref->getOpcode() == UO_Deref)
      Base = Deref->getSubExpr()->IgnoreParenImpCasts();
    if (isa<CXXThisExpr, DeclRefExpr>(Base))
      return true;
    if (auto *Conditional = dyn_cast<ConditionalOperator>(Base))
      return TraverseStmt(Conditional->getCond()) &&
             traverseStaticBase(Conditional->getTrueExpr()) &&
             traverseStaticBase(Conditional->getFalseExpr());
    if (auto *Comma = dyn_cast<BinaryOperator>(Base);
        Comma && Comma->getOpcode() == BO_Comma)
      return TraverseStmt(Comma->getLHS()) &&
             traverseStaticBase(Comma->getRHS());
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
  // Not what a builtin doesn't evaluate, `__builtin_constant_p(Value)` or
  // `__builtin_object_size(pValue, 0)` (hasUnevaluatedOperands).
  bool TraverseCallExpr(CallExpr *Call) {
    if (hasUnevaluatedOperands(Call))
      return true;
    return RecursiveASTVisitor::TraverseCallExpr(Call);
  }

  // Not a name that isn't an odr-use, the value of a constant read as a
  // constant, `c_Size` of `constexpr int c_Size = 4096;`, which touches no
  // storage.
  bool VisitStmt(Stmt *S) {
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Ref->isNonOdrUse() != NOUR_None)
      return true;
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
bool isCalledWhereMade(
    const LambdaExpr *Lambda,
    llvm::function_ref<const Stmt *(const Stmt *)> ParentOf,
    const CallExpr **Call) {
  struct {
    llvm::function_ref<const Stmt *(const Stmt *)> Of;
    const Stmt *getParent(const Stmt *S) const { return Of(S); }
  } Parents{ParentOf};
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
    const CMemberCall Named =
        memberCallOf(Parents.getParent(Member));
    return Method && Method->getOverloadedOperator() == OO_Call && Named &&
           Named->getCallee()->IgnoreParens() == Member && Found(Named);
  }
  // Called by name with an explicit object, `[&Value](this auto const &) {
  // ... }.operator()()`, which clang makes a plain call given the lambda
  // first.
  if (const CMemberCall Named = memberCallOf(Made);
      Named && Named->isExplicitObject() && !Named->isOperator() &&
      Named->getMethodDecl()->getOverloadedOperator() == OO_Call &&
      IsLambda(Named->getImplicitObjectArgument()))
    return Found(Named);
  if (const auto *Invoke = dyn_cast_or_null<CallExpr>(Made))
    return isStdInvoke(Invoke) && Invoke->getNumArgs() >= 1 &&
           IsLambda(Invoke->getArg(0)) && Found(Invoke);
  return false;
}

/// The use \p Use, a root in the body of \p Function, of the borrowed data
/// of \p Borrow (CLifetimeUse): what it designates, or with \p Kept what
/// it keeps in a local, in the context where it is (useContext); of a
/// lambda's call operator whose closure Borrow tells isn't kept alive while
/// it runs, that closure NotKept.
static CLifetimeUse borrowUseAt(const FunctionDecl *Function, const Stmt *Use,
                                const BorrowRoots &Borrow,
                                const CParents *Parents, bool Kept) {
  CLifetimeUse Of;
  Of.Use = dyn_cast<Expr>(Use);
  Of.Used = Kept ? EUsed::Kept : EUsed::Designated;
  Of.ParentOf = Parents ? FParentOf(*Parents) : FParentOf();
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      !Borrow.ClosureKept && Method && Method->getParent()->isLambda())
    Of.NotKept = Method->getParent();
  return Of;
}

/// Whether the borrowed data of \p Borrow is alive where \p Use, a root in
/// the body of \p Function, uses it, the one decision of it
/// (aliveWhereUsed, borrowUseAt), when Borrow asks it (AskKeepers).
static bool dataAliveAt(const FunctionDecl *Function, const Stmt *Use,
                        const BorrowRoots &Borrow, const CParents *Parents,
                        ASTContext &Context, bool Kept = false) {
  return Borrow.AskKeepers && isa<Expr>(Use) &&
         aliveWhereUsed(
             borrowUseAt(Function, Use, Borrow, Parents, Kept),
             Context);
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
  TCRecursive<void(const Stmt *, const Stmt *)> Visit =
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
        for (const Stmt *Child : keptChildren(S))
          Visit(Child, Next);
      };
  Visit(Function->getBody(), nullptr);
  return OnlyCalled;
}

namespace {
/// Whether what a way of provenance reaches at an object, \p At at \p Level,
/// by what it takes there (CProvenance::projections), is its own storage
/// (storageOf).
using FOwnAt = llvm::function_ref<bool(const Expr *At, const CEffects &Effects,
                                       unsigned Level,
                                       const CProvenance &Walk)>;
} // namespace

/// Where what \p E holds at \p Level of provenance is relative to the objects
/// \p IsObject is true of, each reached told own or borrowed by \p OwnAt, by
/// CProvenance::reach: what a function whose returns can't be seen, or a
/// constructor that doesn't give its members what it is given as it is,
/// makes of what can refer to an object can't be told, nor what a static
/// local that can be changed holds; a copy the compiler defines is where its
/// source is; a value that holds no address, a number, leads nowhere. With
/// \p Reached, the objects reached as their own storage are added to it, also
/// those what can't be told is given, which it can keep.
static CObjectStorage storageOf(const Expr *E, unsigned Level,
                                llvm::function_ref<bool(const Expr *)> IsObject,
                                FOwnAt OwnAt, ASTContext &Context,
                                llvm::SmallPtrSetImpl<const Expr *> *Reached =
                                    nullptr);

static bool closureOwnAt(const Expr *At, const CEffects &Effects,
                         unsigned Level, const CProvenance &Walk);

/// Whether a value of \p Type holds no address: a number, an enumerator, a
/// null pointer, nothing, or an object or array of only values
/// (holdsOnlyValues), `struct CState { int m_Value; }`.
static bool holdsNoAddress(QualType Type, const ASTContext &Context) {
  Type = Type.getNonReferenceType();
  return !Type.isNull() &&
         (Type->isArithmeticType() || Type->isEnumeralType() ||
          Type->isNullPtrType() || Type->isVoidType() ||
          ((Type->isRecordType() || Type->isArrayType()) &&
           holdsOnlyValues(Type, Context)));
}

namespace {
/// What a pointer or reference member of a record holds, and the level of
/// provenance of that value that is the record's level (memberValues).
struct CMemberValue {
  const Expr *Value = nullptr;
  unsigned Level = 0;
};
} // namespace

/// What the record \p Record, a local slot or a closure's copy of one,
/// holds at \p Level above 0, member by member: of each pointer or reference
/// member, also of the records it holds by value, what was put in that
/// member of the slot (localSlotValues), each value it can be given there, a
/// level down; an owner member, which a copy shares or takes, and a value
/// member hold nothing it refers to. None when that can't be told: it isn't
/// one slot, a member is a view or an array of what can refer, a member has
/// no value or one that isn't an expression, the record has bases.
static std::optional<llvm::SmallVector<CMemberValue, 4>>
memberValues(const Expr *Record, unsigned Level, ASTContext &Context) {
  const QualType Type = Record->getType().getNonReferenceType();
  if (Level == 0 || !Record->isGLValue() || !isValueRecordType(Type, Context) ||
      Type->isArrayType())
    return std::nullopt;
  bool Told = true;
  const llvm::SmallVector<CSlot, 2> Slots =
      designatedSlots(Record, Context, &Told);
  if (!Told || Slots.size() != 1 || Slots.front().Element)
    return std::nullopt;
  llvm::SmallVector<CMemberValue, 4> Members;
  // The records on the way, each with the members from the record to it.
  struct CStep {
    const CXXRecordDecl *Of;
    llvm::SmallVector<const FieldDecl *, 2> Path;
  };
  llvm::SmallVector<CStep, 4> Work{{Type->getAsCXXRecordDecl(), {}}};
  unsigned nSteps = 0;
  while (!Work.empty()) {
    const CStep Step = Work.pop_back_val();
    if (!Step.Of || !Step.Of->hasDefinition() || Step.Of->getNumBases() != 0 ||
        ++nSteps > 64)
      return std::nullopt;
    for (const FieldDecl *Field : Step.Of->fields()) {
      const QualType Of = Field->getType();
      if (holdsNoAddress(Of, Context) ||
          ownerKindOf(Of.getNonReferenceType(), Context) != EOwnerKind::None)
        continue;
      llvm::SmallVector<const FieldDecl *, 2> Path = Step.Path;
      Path.push_back(Field);
      if (!Of->isReferenceType() && !Of->isPointerType()) {
        if (Of->isRecordType() && !isDefaultViewType(Of, Context) &&
            isValueRecordType(Of, Context)) {
          Work.push_back({Of->getAsCXXRecordDecl(), Path});
          continue;
        }
        return std::nullopt;
      }
      CSlot Member = Slots.front();
      llvm::append_range(Member.Path, Path);
      const CSlotValues Values = localSlotValues(Member, Record, Context);
      if (Values.Values.empty())
        return std::nullopt;
      for (const CSlotValue &Each : Values.Values) {
        if ((Each.Kind != CSlotValue::EKind::Value &&
             Each.Kind != CSlotValue::EKind::Address) ||
            !Each.Value)
          return std::nullopt;
        Members.push_back({Each.Value, Level - 1});
      }
    }
  }
  return Members;
}

/// The level of provenance at which what \p E gives, copied, refers to what
/// it refers to: of a glvalue of a pointer, an array or an object, what its
/// value points to; of another glvalue, its storage; of a pointer value,
/// what it points to; of an object made, what it holds points to.
static unsigned heldAt(const Expr *E) {
  const QualType Type = E->getType().getNonReferenceType();
  if (E->isGLValue())
    return Type->isPointerType() || Type->isRecordType() || Type->isArrayType()
               ? 1
               : 0;
  return Type->isPointerType() ? 0 : 1;
}

/// Whether the value \p Made, a result, of the type \p Result where it is
/// the value of a `co_return`, keeps alive what the way of the provenance of
/// what it holds reaches at \p At, at \p AtLevel (Rule K in the context of
/// the result, resultUse, aliveAtUse), derived by \p Made: an owner's,
/// `CReadResult{m_pJob, &m_pJob->m_Value}`, the object, or what a parameter
/// refers to, `CReadResult{_pKeep, &_Job.m_Result}`, whatever it is. Derived
/// by \p Made, or by the whole expression \p At is in (keptDerivationOf),
/// where the result is made in a local, `Result.m_pValue = &pJob->m_Value`.
/// An owner reached at its own storage, of a derivation that has what it
/// owns among its anchors, `&pJob->m_Value` of `pJob`, which dereferences
/// it, is asked of what it owns too.
static bool resultKeeps(const Expr *Made, const Expr *At, unsigned AtLevel,
                        ASTContext &Context, QualType Result = {}) {
  if (!Made || !At)
    return false;
  CKeptAt In;
  In.Kind = CKeptAt::EKind::Result;
  In.Made = Made;
  In.Result = Result;
  const CStorageUse Use = keptContext(In, Context);
  const Expr *Whole = keptDerivationOf(At, {}, Context);
  auto AliveAt = [&](unsigned Level) {
    return aliveAtUse(At, Level, Use, Context, Made) ||
           (Whole != Made && aliveAtUse(At, Level, Use, Context, Whole));
  };
  if (AliveAt(AtLevel))
    return true;
  const std::optional<CAnchor> Owned =
      AtLevel == 0 ? ownerAnchorOf(At->getType(), Context) : std::nullopt;
  return Owned &&
         (llvm::is_contained(derivationAnchors(Made, Context), *Owned) ||
          llvm::is_contained(derivationAnchors(Whole, Context), *Owned)) &&
         AliveAt(1);
}

/// Where what \p E holds at \p Level is relative to the borrowed data of \p
/// Roots at the roots \p IsUse is true of (storageOf), own of the data: of
/// the copies a closure keeps (OfClosure), as objectStorageOf tells; of
/// `this`, the object and what it holds; of a pointer or view held by value
/// whose value refers to the data (PointerValues), what it points to, not
/// its own slot; of another pointer only itself, of another view also what
/// it points to, which can be in the frame of the data; of anything else,
/// what it designates and what that holds. Of \p E a result, not what an
/// result keeps alive (resultKeeps), `CReadResult{_pJob, &_pJob->m_Value}`,
/// which the result owns with it.
static CObjectStorage
borrowedStorageOf(const Expr *E, unsigned Level,
                  llvm::function_ref<bool(const Expr *)> IsUse,
                  const BorrowRoots &Roots, ASTContext &Context,
                  llvm::SmallPtrSetImpl<const Expr *> *Reached = nullptr,
                  bool Result = false, QualType ResultType = {}) {
  auto OwnAt = [&](const Expr *At, const CEffects &Effects, unsigned AtLevel,
                   const CProvenance &Walk) {
    if (Result && resultKeeps(E, At, AtLevel, Context, ResultType))
      return false;
    if (Roots.OfClosure)
      return closureOwnAt(At, Effects, AtLevel, Walk);
    if (At->isPRValue())
      return true;
    const QualType Type = At->getType().getNonReferenceType();
    const bool View = Roots.IsView && Roots.IsView(Type);
    if (!Type->isPointerType() && !View)
      return true;
    const ValueDecl *Named = namedDecl(At, Context);
    if (Roots.PointerValues)
      return AtLevel > 0 || (Named && Named->getType()->isReferenceType());
    return AtLevel == 0 || View;
  };
  return storageOf(E, Level, IsUse, OwnAt, Context, Reached);
}

/// The handles, which are copied by what they are given to.
static constexpr llvm::StringLiteral HandleTypes[] = {
    "TCActor",    "TCWeakActor", "TCDistributedActor", "TCDistributedActorWrapper",
    "TCPromise",  "CThisActor"};

/// Whether \p Type is a handle (HandleTypes), also, of a \p Receiver of a
/// member function, `pPromise->f_SetResult(...)`, one a pointer points to; a
/// pointer to a handle given away isn't one.
static bool isHandleType(QualType Type, bool Receiver = false) {
  if (Receiver && !Type.isNull() && Type->isPointerType())
    Type = Type->getPointeeType();
  const StringRef Name = recordName(Type);
  return !Name.empty() && llvm::is_contained(HandleTypes, Name);
}

/// Whether what a function or a constructor whose body can't be seen is
/// given of \p Type by reference is copied by it, by the argument's type: an
/// owner whose copy keeps what it owns (`shared`, `sharedbuffer`), a weak
/// owner, or a handle (HandleTypes); nothing keeps a reference to the
/// caller's own object of it.
static bool copiedWhenGiven(QualType Type, const ASTContext &Context) {
  Type = Type.getNonReferenceType();
  const EOwnerKind Kind = ownerKindOf(Type, Context);
  return copyKeepsOwned(Kind) || Kind == EOwnerKind::Weak || isHandleType(Type);
}

namespace {
/// What the policy of followUses tells of one use of what is followed: that
/// it keeps it, or what is made of it, which is followed in turn.
struct CFollowed {
  bool bKept = false;
  const Expr *Next = nullptr;
  /// A second value followed, the object a member function keeps it in.
  const Expr *Also = nullptr;
  /// More values followed, what other arguments that keep it refer to.
  llvm::SmallVector<const Expr *, 2> More = {};
};
using FFollowUse = llvm::function_ref<CFollowed(const CEffectIndex::CUse &Use,
                                                const CEffects &Of)>;
} // namespace

/// Whether what is made of \p Start, a statement of \p Index, is kept, by
/// the effect table, each use past what gives it on (consumersOf): a load of
/// it, what it points to, its address and what a step gives are followed,
/// and so is an object made of it, a temporary, an element or a member of
/// an aggregate, but not the awaiter of a suspension, which holds it while
/// the coroutine is suspended. What holds no address keeps nothing, nor does
/// a use that calls it, tests it, drops it, runs it as a statement, doesn't
/// run it or writes it. \p Policy tells of each other use: what is computed
/// with, the object of a member function, what it is bound or copied to
/// elsewhere, and what can't be told.
static bool followUses(const Expr *Start, const CEffectIndex &Index,
                       ASTContext &Context, FFollowUse Policy) {
  llvm::SmallVector<const Expr *, 8> Work{Start};
  llvm::SmallPtrSet<const Stmt *, 16> Seen{Start};
  auto Continue = [&](const Stmt *Next) {
    const auto *Value = dyn_cast_or_null<Expr>(Next);
    if (!Value || (Value->isPRValue() && holdsNoAddress(Value->getType(), Context)))
      return;
    if (Seen.insert(Value).second)
      Work.push_back(Value);
  };
  while (!Work.empty()) {
    const Expr *Value = Work.pop_back_val();
    for (const CEffectIndex::CUse &Use : Index.consumersOf(Value)) {
      const COperandEffect &Effect = *Use.Effect;
      switch (Effect.Use) {
      case EUse::Forward:
      case EUse::Access:
      case EUse::Update:
      case EUse::Dereference:
      case EUse::AddressOf:
        Continue(Use.Parent);
        continue;
      case EUse::Callee:
      case EUse::Condition:
      case EUse::Discarded:
      case EUse::Statement:
      case EUse::Unevaluated:
      case EUse::Write:
        continue;
      case EUse::Bind:
      case EUse::Copy:
        switch (Effect.Into.Kind) {
        case EDestination::Temporary:
        case EDestination::Element:
        case EDestination::Field:
          if (Effect.Into.Object &&
              !effectsOf(Effect.Into.Object, Context).bSuspends) {
            Continue(Effect.Into.Object);
            continue;
          }
          break;
        case EDestination::None:
        case EDestination::Variable:
        case EDestination::Parameter:
        case EDestination::Designated:
        case EDestination::Capture:
        case EDestination::Base:
        case EDestination::Return:
        case EDestination::Heap:
        case EDestination::Thrown:
          break;
        }
        break;
      case EUse::CallObject:
      case EUse::Value:
      case EUse::Opaque:
        break;
      }
      const CFollowed Followed = Policy(Use, effectsOf(Use.Parent, Context));
      if (Followed.bKept)
        return true;
      Continue(Followed.Next);
      Continue(Followed.Also);
      for (const Expr *More : Followed.More)
        Continue(More);
    }
  }
  return false;
}

/// Whether \p Argument, as written, is a prvalue or an xvalue, which what it
/// is given to moves or copies (Rule R): nothing keeps a reference to a
/// temporary or to what the caller moved in.
static bool takenWhenGiven(const Expr *Argument) {
  return Argument && !Argument->IgnoreImplicit()->isLValue();
}

namespace {
/// What the result of a coroutine can keep of what `co_return` gives it
/// (resultMadeFrom).
struct CResultMade {
  /// What it designates itself.
  bool bDesignated = false;
  /// What its parts refer to.
  bool bHeld = false;
  /// Whether the result is a copy or a move of it, which of a type that isn't
  /// a view owns what it holds when it is the data itself.
  bool bCopy = false;
};
} // namespace

/// What the result of the type \p Result, the `T` of a `TCFuture<T>`, can
/// keep of \p Given, the operand of a `co_return`, which initializes it, by
/// type alone: the promise that makes it is plumbing, never looked into. A
/// reference designates it. A result that can't hold a pointer, a reference
/// or a view keeps nothing. One of the type of \p Given, cv and references
/// aside, is a copy or a move of it (Rule V). One of another type is made
/// from it by a conversion that can't be seen (Rule R): it can keep what its
/// parts refer to, and a reference to it when it is given as an lvalue, not
/// a temporary, what is moved or what `co_return` moves implicitly. What is
/// taken so and holds no pointer, reference or view, and an owner or a
/// handle that is copied when given (copiedWhenGiven), it keeps nothing of.
/// Of no result type, a copy.
static CResultMade resultMadeFrom(const Expr *Given, QualType Result,
                                  const ASTContext &Context) {
  CResultMade Made;
  if (Result.isNull()) {
    Made.bHeld = Made.bCopy = true;
    return Made;
  }
  if (Result->isReferenceType()) {
    Made.bDesignated = true;
    return Made;
  }
  if (!Result->isPointerType() && !Result->isMemberPointerType() &&
      !holdsBorrowed(Result, Context))
    return Made;
  Made.bHeld = true;
  if (Context.hasSameUnqualifiedType(Given->getType().getNonReferenceType(),
                                     Result)) {
    Made.bCopy = true;
    return Made;
  }
  // Taken, a move or a copy of what holds no pointer, reference or view owns
  // what it holds, and so does a copy of an owner or a handle.
  const bool Taken = takenWhenGiven(Given) || namesImplicitlyMovable(Given);
  if (copiedWhenGiven(Given->getType(), Context) ||
      (Taken &&
       !holdsBorrowed(Given->getType().getNonReferenceType(), Context))) {
    Made.bHeld = false;
    return Made;
  }
  Made.bDesignated = !Taken;
  return Made;
}

static CParameterKept
parameterKeptUncached(const std::pair<const FunctionDecl *, unsigned> &Key);

/// What a function that reaches itself is taken to keep while it is told:
/// everywhere but in its result.
static CParameterKept cycleKept() {
  CParameterKept Kept;
  Kept.bObject = Kept.bStatic = Kept.bUnknown = Kept.bOwnStorage = true;
  Kept.Arguments = ~uint64_t(0);
  return Kept;
}

namespace {
/// Of each function and parameter, what it keeps (parameterKept), once told:
/// one that reaches itself keeps it only as the rest of it does, which is
/// taken of it while it is told.
struct CKeptParameters {
  TCSummaries<std::pair<const FunctionDecl *, unsigned>, CParameterKept>
      Results{&parameterKeptUncached, CParameterKept(),
              cycleKept()};
};
} // namespace

/// Whether what \p Defined, an element of the CFG of \p Function, an
/// initializer or a statement, gives a member can be what it holds where
/// \p Function ends: a path of the CFG goes from it to the exit past none of
/// \p Others, the other definitions of it. One the CFG doesn't have can.
static bool givesAtEnd(const FunctionDecl *Function, const void *Defined,
                       llvm::ArrayRef<const void *> Others,
                       ASTContext &Context) {
  const CFG *Graph =
      sharedFlowCache().graphOf(Function, Function->getBody(), Context);
  if (!Graph)
    return true;
  auto Identity = [](const CFGElement &Element) -> const void * {
    if (const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>())
      return Statement->getStmt();
    if (const std::optional<CFGInitializer> Initializer =
            Element.getAs<CFGInitializer>())
      return Initializer->getInitializer();
    return nullptr;
  };
  const CFGBlock *Start = nullptr;
  unsigned StartIndex = 0;
  for (const CFGBlock *Block : *Graph) {
    for (unsigned iElement = 0; Block && iElement < Block->size(); ++iElement)
      if (Identity((*Block)[iElement]) == Defined) {
        Start = Block;
        StartIndex = iElement + 1;
      }
  }
  if (!Start)
    return true;
  llvm::SmallVector<std::pair<const CFGBlock *, unsigned>, 8> Work{
      {Start, StartIndex}};
  llvm::DenseSet<const CFGBlock *> Seen;
  while (!Work.empty()) {
    auto [Block, From] = Work.pop_back_val();
    bool Passed = false;
    for (unsigned iElement = From; iElement < Block->size() && !Passed;
         ++iElement)
      Passed = llvm::is_contained(Others, Identity((*Block)[iElement]));
    if (Passed)
      continue;
    if (Block == &Graph->getExit())
      return true;
    for (const CFGBlock::AdjacentBlock &Next : Block->succs())
      if (const CFGBlock *Reached = Next.getReachableBlock();
          Reached && Seen.insert(Reached).second)
        Work.push_back({Reached, 0});
  }
  return false;
}

/// Whether \p E names the explicit object parameter of \p Function taken by
/// reference, `_This` of `void f_Set(this CKeep &_This)`, the object it is
/// called on, as `this` is; not one taken by value, a copy of its own.
static bool namesExplicitObjectOf(const Expr *E, const FunctionDecl *Function) {
  const ParmVarDecl *Object = explicitObjectOf(Function);
  const auto *Ref =
      E ? dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts()) : nullptr;
  return Object && Ref && Ref->getDecl() == Object &&
         Object->getType()->isReferenceType();
}

/// The member of the object of the member function \p Function that
/// \p Target, in its body, designates as a whole, `m_pValue`,
/// `this->m_pValue` or `_This.m_pValue` of an explicit object; null otherwise.
static const FieldDecl *ownMember(const Expr *Target,
                                  const FunctionDecl *Function,
                                  ASTContext &Context) {
  Target = sameValue(Target, Context);
  if (effectsOf(Target, Context).Statement != EStatement::Member)
    return nullptr;
  const auto *Field = dyn_cast_or_null<FieldDecl>(memberDecl(Target, Context));
  const Expr *Base = sameValue(cast<MemberExpr>(Target)->getBase(), Context);
  return Field && (effectsOf(Base, Context).Statement == EStatement::This ||
                   namesExplicitObjectOf(Base, Function))
             ? Field
             : nullptr;
}

/// The class of the object of \p Function that it can keep what it is given
/// in: of a constructor the class it makes, of a member function that isn't
/// const, not of a lambda, nor with an explicit object, which is a
/// parameter, its class; null otherwise.
static const CXXRecordDecl *keepingObjectOf(const FunctionDecl *Function) {
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Function);
  if (!Method || !Method->isImplicitObjectMemberFunction() ||
      Method->getParent()->isLambda() || Method->isConst())
    return nullptr;
  return Method->getParent();
}

/// What a body that can't be seen keeps of what it is given for its
/// parameter \p iParameter, by its type (Rule R): only what the call makes
/// or is made on, each by its type (holdsBorrowed): the result \p Result,
/// null of a constructor, whose object is what it makes, when it is a
/// reference, a pointer or can hold one or a view; the object of the class
/// \p Object, called by what isn't const or made; and what the other
/// parameters of \p Parameters given by reference or pointer to what isn't
/// const refer to. Keeping it elsewhere, a static, a registry, isn't
/// assumed.
static CParameterKept keptByType(QualType Result,
                                 llvm::ArrayRef<QualType> Parameters,
                                 unsigned iParameter,
                                 const CXXRecordDecl *Object,
                                 const ASTContext &Context) {
  CParameterKept Kept;
  Kept.bUnknown = true;
  Kept.bResult = !Result.isNull() &&
                 (Result->isReferenceType() || Result->isPointerType() ||
                  holdsBorrowed(Result, Context));
  Kept.bObject = Kept.bOwnStorage =
      Object && holdsBorrowed(Context.getCanonicalTagType(Object), Context);
  for (unsigned iOther = 0; iOther < Parameters.size() && iOther < 64;
       ++iOther) {
    if (iOther == iParameter)
      continue;
    const QualType Type = Parameters[iOther];
    const QualType Referent =
        Type->isReferenceType()
            ? Type.getNonReferenceType()
            : (Type->isPointerType() ? Type->getPointeeType() : QualType());
    if (!Referent.isNull() && !Referent.isConstQualified() &&
        holdsBorrowed(Referent, Context))
      Kept.Arguments |= uint64_t(1) << iOther;
  }
  return Kept;
}

/// The types of the parameters of \p Function.
static llvm::SmallVector<QualType, 4>
parameterTypesOf(const FunctionDecl *Function) {
  llvm::SmallVector<QualType, 4> Types;
  for (const ParmVarDecl *Parameter : Function->parameters())
    Types.push_back(Parameter->getType());
  return Types;
}

/// Whether the object of \p Function can keep what it is given, by its type
/// (keepingObjectOf, holdsBorrowed).
static bool objectCanKeep(const FunctionDecl *Function) {
  const CXXRecordDecl *Object = keepingObjectOf(Function);
  return Object &&
         holdsBorrowed(Function->getASTContext().getCanonicalTagType(Object),
                       Function->getASTContext());
}

static CParameterKept parameterKeptOf(const FunctionDecl *Function,
                                      unsigned iParameter, bool Unseen);

/// What \p Function keeps by reference of what its parameter \p iParameter
/// refers to, a reference what it is bound to, a pointer or an object what
/// it points to, the one decision of what a call or a construction keeps of
/// what it is given (Rule R). Nothing of a function that returns a
/// `TCFuture`, whose coroutines are held to the same rules, nor of an owner
/// whose copy keeps what it owns, a weak owner or a handle given by
/// reference, which it copies (copiedWhenGiven). Of a body that can be seen,
/// by what is made of each of its names that runs, by the effect table, up
/// to where it is kept, a member of its object, as it is where it ends, by
/// its CFG, `m_pValue(&_Value)` but not one the body assigns again on every
/// path, `m_pValue = nullptr;`, in what another parameter refers to, or in
/// what a function or constructor given it keeps it in (heldIn), each where
/// it is (storageRootsOf), static storage too, which lasts past every call;
/// what is thrown keeps nothing. Not a value read of it, a copy of an object that
/// isn't a view, nor what it only gives back, which the caller follows. A
/// local is followed by its names; a coroutine keeps it in its frame. Of a
/// body that can't be seen, or of an override that can run instead, \p
/// Unseen: only what the call makes or is made on, each by its type
/// (holdsBorrowed): its result, the object it makes or is called on by a
/// member function that isn't const, and what the other arguments given by
/// reference or pointer to what isn't const refer to; keeping it elsewhere
/// isn't assumed. What is given to a reference as \p Argument, a prvalue or
/// an xvalue, is moved or copied: what the reference is bound to isn't kept,
/// only what it holds, and nothing of a type that can't hold a pointer, a
/// reference or a view, whose move or copy owns what it holds. Of a
/// reference to an owner (OwnerTypes) kept
/// in the object, whether only what it owns is, by where what is kept lives
/// (storageRootsOf): `m_pPage(_pPage)` and `m_Text(_pPage->m_Text, 64)` keep
/// only what `_pPage` owns.
static CParameterKept parameterKept(const FunctionDecl *Function,
                                    unsigned iParameter, bool Unseen,
                                    const Expr *Argument) {
  // A prvalue or an xvalue given to a reference it moves or copies
  // (takenWhenGiven): what the reference is bound to isn't kept, only what
  // it holds, as of a parameter by value, and nothing of what holds no
  // pointer, reference or view, whose move or copy owns what it holds.
  const bool Taken =
      Function && iParameter < Function->getNumParams() &&
      Function->getParamDecl(iParameter)->getType()->isReferenceType() &&
      takenWhenGiven(Argument);
  if (Taken &&
      !holdsBorrowed(
          Function->getParamDecl(iParameter)->getType().getNonReferenceType(),
          Function->getASTContext()))
    return CParameterKept();
  CParameterKept Kept = parameterKeptOf(Function, iParameter, Unseen);
  if (Taken)
    Kept.bOwnStorage = false;
  return Kept;
}

static CParameterKept parameterKeptOf(const FunctionDecl *Function,
                                      unsigned iParameter, bool Unseen) {
  CParameterKept Kept;
  if (!Function)
    return Kept;
  // A parameter whose pointer, reference or view the callee states it keeps
  // beyond the call (DMibKeepsArgument): the object a constructor makes
  // keeps it, also what the reference is bound to; any other function keeps
  // it past the call, as static storage does.
  if (keepsArgument(Function, iParameter)) {
    if (isa<CXXConstructorDecl>(Function))
      Kept.bObject = Kept.bOwnStorage = true;
    else
      Kept.bStatic = true;
    return Kept;
  }
  // A function that returns a `TCFuture` keeps nothing: its coroutines are
  // held to the same rules.
  if (returnsProtectedFuture(Function))
    return Kept;
  const FunctionDecl *Definition = Function->getDefinition();
  const bool Seen = !Unseen && Definition && Definition->getBody() &&
                    iParameter < Definition->getNumParams() &&
                    !isa<CoroutineBodyStmt>(Definition->getBody());
  // An owner or a handle given by reference, which a body that can't be seen
  // copies (copiedWhenGiven); one that can be seen tells what it keeps of
  // it, `m_Text(_pPage->m_Text, 64)` a part of what the owner owns.
  if (!Seen && iParameter < Function->getNumParams() &&
      Function->getParamDecl(iParameter)->getType()->isReferenceType() &&
      copiedWhenGiven(Function->getParamDecl(iParameter)->getType(),
                      Function->getASTContext()))
    return Kept;
  // A body that can't be seen, also of an override that can run instead,
  // keeps it by reference only in what the call makes or is made on, each
  // by its type: the result, the object it makes or is called on, and what
  // the other arguments given by reference or pointer refer to. Keeping it
  // elsewhere, a static, a registry, isn't assumed.
  if (Unseen || !Definition || !Definition->getBody() ||
      iParameter >= Definition->getNumParams())
    return keptByType(isa<CXXConstructorDecl>(Function)
                          ? QualType()
                          : Function->getReturnType(),
                      parameterTypesOf(Function), iParameter,
                      keepingObjectOf(Function), Function->getASTContext());
  // A coroutine keeps its parameters in its frame, which its result holds
  // and which runs on past the call.
  if (isa<CoroutineBodyStmt>(Definition->getBody())) {
    Kept.bUnknown = Kept.bResult = true;
    Kept.bObject = Kept.bOwnStorage = objectCanKeep(Function);
    return Kept;
  }
  const ParmVarDecl *Parameter = Definition->getParamDecl(iParameter);
  const QualType Type = Parameter->getType();
  const bool Reference = Type->isReferenceType();
  ASTContext &Context = Definition->getASTContext();
  if (!Reference && holdsNoAddress(Type, Context))
    return Kept;
  // What it gives back, the caller follows (returnedRoots).
  Kept =
      astCache<CKeptParameters>(Context).Results.get({Definition, iParameter});
  Kept.bResult = true;
  return Kept;
}

/// What the call \p Call of \p Callee keeps of what it gives its parameter
/// \p iParameter (Rule R): parameterKept of a callee that is named; of one
/// that isn't, through a pointer to a function or a member function or any
/// other expression, by its function type as of a body that can't be seen,
/// the object of a const member function keeping nothing; of an argument
/// that is a prvalue or an xvalue, as the named overload tells.
static CParameterKept parameterKept(const CallExpr *Call,
                                    const FunctionDecl *Callee,
                                    unsigned iParameter,
                                    const ASTContext &Context, bool Unseen) {
  const unsigned iArgument = iParameter + argumentShift(Call, Callee);
  const Expr *Argument =
      iArgument < Call->getNumArgs() ? Call->getArg(iArgument) : nullptr;
  if (Callee)
    return parameterKept(Callee, iParameter, Unseen, Argument);
  // Of a callee that isn't named, by its function type: of what a pointer
  // to a function or a block, or a reference to a function, designates, and
  // of a pointer to a member function, also the class of its object, which
  // a const one can't keep it in.
  const Expr *Called = Call->getCallee()->IgnoreParens();
  const FunctionProtoType *Type = nullptr;
  const CXXRecordDecl *Object = nullptr;
  if (const auto *Bound = dyn_cast<BinaryOperator>(Called);
      Bound && Bound->isPtrMemOp()) {
    if (const auto *Member =
            Bound->getRHS()->getType()->getAs<MemberPointerType>()) {
      Type = Member->getPointeeType()->getAs<FunctionProtoType>();
      if (Type && !Type->getMethodQuals().hasConst())
        Object = Member->getMostRecentCXXRecordDecl();
    }
  } else {
    QualType Of = Called->getType();
    if (Of->isPointerType() || Of->isBlockPointerType())
      Of = Of->getPointeeType();
    Type = Of.getNonReferenceType()->getAs<FunctionProtoType>();
  }
  CParameterKept Kept;
  // What can't be told keeps it in all it makes or is given.
  if (!Type) {
    Kept.bUnknown = Kept.bResult = Kept.bObject = Kept.bOwnStorage = true;
    Kept.Arguments = ~uint64_t(0) & ~(uint64_t(1) << (iParameter % 64));
    return Kept;
  }
  const bool Reference = iParameter < Type->getNumParams() &&
                         Type->getParamType(iParameter)->isReferenceType();
  // What is given as a prvalue or an xvalue to a reference is taken
  // (takenWhenGiven), as by the named overload.
  const bool Taken = Reference && takenWhenGiven(Argument);
  if (isProtectedFutureType(Type->getReturnType(), Context) ||
      (Reference && copiedWhenGiven(Type->getParamType(iParameter), Context)) ||
      (Taken &&
       !holdsBorrowed(Type->getParamType(iParameter).getNonReferenceType(),
                      Context)))
    return Kept;
  Kept = keptByType(Type->getReturnType(), Type->getParamTypes(), iParameter,
                    Object, Context);
  if (Taken)
    Kept.bOwnStorage = false;
  return Kept;
}

namespace {
/// In which objects a call or a construction keeps what it gives one of its
/// parameters (heldIn, Rule R). Each holder is a value followed by the same
/// rules, never an escape of its own.
struct CHeldIn {
  /// The object a construction makes, or the result of a call.
  const Expr *Made = nullptr;
  /// The object a member function is called on, as it is written.
  const Expr *Receiver = nullptr;
  /// The other arguments, as written, what each refers or points to.
  llvm::SmallVector<const Expr *, 2> Arguments;
  /// What the callee keeps of it (parameterKept).
  CParameterKept Kept;
};
} // namespace

/// In which objects \p Parent, a call or a construction of the effects \p
/// Of, keeps what it gives its parameter \p iParameter as \p Operand (Rule
/// R, parameterKept): the object it makes, the result, the object a member
/// function is called on, or what the other arguments refer to. \p Unseen,
/// when given, tells whether the call is told as of a body that can't be
/// seen; by default when an override can run instead.
static CHeldIn heldIn(const Stmt *Parent, const CEffects &Of,
                      unsigned iParameter, const Expr *Operand,
                      const ASTContext &Context,
                      std::optional<bool> Unseen = std::nullopt) {
  CHeldIn Held;
  auto Arguments = [&](llvm::ArrayRef<const Expr *> Given, unsigned Shift) {
    for (unsigned iOther = 0; iOther < 64; ++iOther)
      if ((Held.Kept.Arguments >> iOther & 1) && iOther != iParameter &&
          iOther + Shift < Given.size())
        Held.Arguments.push_back(Given[iOther + Shift]);
  };
  if (const auto *Construct = dyn_cast_or_null<CXXConstructExpr>(Parent)) {
    Held.Kept = parameterKept(Construct->getConstructor(), iParameter,
                              Unseen.value_or(false), Operand);
    Held.Made = Held.Kept.bObject ? Construct : nullptr;
    Arguments({Construct->getArgs(), Construct->getNumArgs()}, 0);
    return Held;
  }
  const auto *Call = dyn_cast_or_null<CallExpr>(Parent);
  if (!Call)
    return Held;
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Of.Callee);
  Held.Kept = parameterKept(
      Call, Of.Callee, iParameter, Context,
      Unseen.value_or(Method && mayRunOverride(Call, Method)));
  Held.Made = Held.Kept.bResult ? Call : nullptr;
  Arguments({Call->getArgs(), Call->getNumArgs()},
            argumentShift(Call, Of.Callee));
  if (Held.Kept.bObject) {
    const CMemberCall Member = memberCallOf(Call, /*Operators=*/true);
    Held.Receiver = Member ? Member->getImplicitObjectArgument()
                           : callObject(Call, Context);
  }
  return Held;
}

namespace {
/// What a holder a call keeps something in is (holderOf).
enum class EHolderOf {
  /// A value of records of the frame or a temporary: over (Rule V).
  Value,
  /// Another temporary of the full expression, which ends there unless the
  /// expression gives it on: followed.
  Temporary,
  /// Anything else keeps it.
  Kept,
};
} // namespace

/// What \p Holder, what a call keeps something in as written, an object
/// given by reference or a pointer to it given by value, is by where it lives
/// (storageRootsOf); by what it designates (designatedSlots, pointedSlots)
/// where each is a local of the frame, whatever its type, `pOther` given to
/// `CJob const *&`, `&pOther` or `&Other.m_pJob`, whose address doesn't
/// escape (localSlotEscapes), which the frame then holds it in.
static EHolderOf holderOf(const Expr *Holder, ASTContext &Context) {
  QualType Type = Holder->getType().getNonReferenceType();
  const bool Pointer = Type->isPointerType() && !Holder->isGLValue();
  if (Pointer)
    Type = Type->getPointeeType();
  bool Told = true;
  const llvm::SmallVector<CSlot, 2> Slots =
      Pointer ? pointedSlots(Holder, Context, &Told)
              : designatedSlots(Holder, Context, &Told);
  if (Told && !Slots.empty() &&
      llvm::all_of(Slots, [&](const CSlot &Slot) {
        const auto *Local = dyn_cast_or_null<VarDecl>(Slot.Variable);
        return Slot.Place == EPlace::Variable && Local &&
               Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
               !Local->getType()->isReferenceType() &&
               !localSlotEscapes(Local, Context);
      }))
    return EHolderOf::Value;
  const CStorageRoots Roots = Pointer ? pointeeRootsOf(Holder, Context)
                                      : storageRootsOf(Holder, Context);
  if (Roots.Roots.empty())
    return EHolderOf::Kept;
  auto All = [&](auto Is) { return llvm::all_of(Roots.Roots, Is); };
  if (isValueRecordType(Type, Context) &&
      All([](const CStorageRoot &Root) {
        return Root.Kind == ERootKind::Frame ||
               Root.Kind == ERootKind::Temporary;
      }))
    return EHolderOf::Value;
  if (!Pointer && All([](const CStorageRoot &Root) {
        return Root.Kind == ERootKind::Temporary;
      }))
    return EHolderOf::Temporary;
  return EHolderOf::Kept;
}

CCallKept callKept(const Stmt *Parent, unsigned Index, const Expr *Operand,
                   bool bUnseen, ASTContext &Context) {
  CCallKept Kept;
  const CEffects &Of = effectsOf(Parent, Context);
  const auto *Construct = dyn_cast<CXXConstructExpr>(Parent);
  const auto *Call = dyn_cast<CallExpr>(Parent);
  // What else is given arguments, `new (Place) CJob(...)`, keeps it.
  if (!Construct && !Call) {
    Kept.bKept = true;
    return Kept;
  }
  // A handle made from another, `TCActor<>` from `TCActor<CActor>`; an
  // owner of a buffer copies what else it is made of, `CStr Text(_Text)` of
  // a `CStrPtr`; a handle given by reference, or what a handle is called
  // on, copies it (Rule R, the argument's side); a function that returns a
  // `TCFuture` is held to keep nothing.
  if (Construct) {
    const CXXConstructorDecl *Constructor = Construct->getConstructor();
    if ((isHandleType(Construct->getType()) && Operand &&
         isHandleType(Operand->getType())) ||
        ((!Constructor || !Constructor->isCopyOrMoveConstructor()) &&
         ownsBuffer(ownerKindOf(Construct->getType(), Context))))
      return Kept;
  } else {
    const Expr *Object = callObject(Call, Context);
    if (returnsCheckedFuture(Call, Context) ||
        (Operand && isHandleType(Operand->getType())) ||
        (Object && isHandleType(Object->getType(), /*Receiver=*/true)))
      return Kept;
  }
  const CHeldIn Held = heldIn(Parent, Of, Index, Operand, Context, bUnseen);
  // A body that can be seen that is a coroutine keeps its parameters in its
  // frame, which runs on past the call.
  const FunctionDecl *Callee = Construct ? Construct->getConstructor()
                                         : Of.Callee;
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  if (Held.Kept.bStatic ||
      (!bUnseen && Definition &&
       isa_and_nonnull<CoroutineBodyStmt>(Definition->getBody()) &&
       !returnsProtectedFuture(Definition)))
    Kept.bKept = true;
  // The object it makes, and of a body that can't be seen the result by
  // its type; of one that can be seen, what it gives back is followed by
  // its returns.
  if (Construct || bUnseen)
    Kept.Made = Held.Made;
  if (!bUnseen && !Held.Kept.bUnknown && !Held.Kept.bObjectWhole) {
    Kept.ObjectMembers = Held.Kept.Members;
    Kept.ObjectPaths = Held.Kept.MemberPaths;
  }
  // Kept in the object it is called on or what another argument refers
  // to: held in a value of the frame, followed in another temporary, else
  // kept.
  llvm::SmallVector<const Expr *, 2> Holders(Held.Arguments.begin(),
                                             Held.Arguments.end());
  if (Index != CallObjectIndex && Held.Receiver)
    Holders.push_back(Held.Receiver);
  for (const Expr *Holder : Holders)
    switch (holderOf(Holder, Context)) {
    case EHolderOf::Value:
      Kept.Values.push_back(Holder);
      break;
    case EHolderOf::Temporary:
      Kept.Temporaries.push_back(Holder);
      break;
    case EHolderOf::Kept:
      Kept.bKept = true;
      break;
    }
  return Kept;
}

static CParameterKept
parameterKeptUncached(const std::pair<const FunctionDecl *, unsigned> &Key) {
  const FunctionDecl *const Definition = Key.first;
  const unsigned iParameter = Key.second;
  CParameterKept Kept;
  const ParmVarDecl *Parameter = Definition->getParamDecl(iParameter);
  const QualType Type = Parameter->getType();
  const bool Reference = Type->isReferenceType();
  ASTContext &Context = Definition->getASTContext();
  const Stmt *Body = Definition->getBody();
  const auto *Constructor = dyn_cast<CXXConstructorDecl>(Definition);
  // A member function, not of a lambda, whose `this` isn't the closure, has
  // an object of its own to keep it in.
  const auto *Method = dyn_cast<CXXMethodDecl>(Definition);
  const bool OwnObject =
      Method && Method->isInstance() && !Method->getParent()->isLambda();
  // What it refers to: of a reference, what it is bound to; of a value, a
  // pointer or an object, what it points to, not its own storage.
  auto IsParameter = [&](const Expr *At) {
    return effectsOf(At, Context).Statement == EStatement::Name &&
           cast<DeclRefExpr>(At)->getDecl() == Parameter;
  };
  auto OwnAt = [&](const Expr *, const CEffects &, unsigned Level,
                   const CProvenance &) { return Reference || Level > 0; };
  // Whether what \p Effect gives refers to it: a reference bound to what,
  // a value by what it holds.
  auto Refers = [&](const COperandEffect &Effect) {
    const auto *Given = dyn_cast_or_null<Expr>(Effect.Operand);
    if (!Given)
      return true;
    if (Effect.Use != EUse::Bind && holdsNoAddress(Given->getType(), Context))
      return false;
    const CObjectStorage Of =
        storageOf(Given, Effect.Use == EUse::Bind ? 0 : heldAt(Given),
                  IsParameter, OwnAt, Context);
    return Of.bOwn || Of.bUnknown;
  };
  // Whether what \p Effect gives, kept in the object, can keep what a
  // reference parameter is bound to itself: unless it is a reference to an
  // owner (OwnerTypes) of which it only takes what it owns, where it lives
  // being only what the parameter owns, Owned of it, or what isn't of it
  // (storageRootsOf), `_pPage->m_Text` and `TCSharedPointer<CPage>(_pPage)`.
  const EOwnerKind ParameterOwner = ownerKindOf(Type, Context);
  auto KeepsOwnStorage = [&](const COperandEffect &Effect) {
    const auto *Given = dyn_cast_or_null<Expr>(Effect.Operand);
    if (!Reference)
      return false;
    if (!Given || ParameterOwner == EOwnerKind::None ||
        ParameterOwner == EOwnerKind::Weak)
      return true;
    const CStorageRoots Roots = storageRootsOf(
        Given, Context, Effect.Use == EUse::Bind ? 0 : heldAt(Given));
    bool Owned = false;
    for (const CStorageRoot &Root : Roots.Roots) {
      if (Root.Kind == ERootKind::Unknown || Root.bPastBorrowed)
        return true;
      if (Root.Variable != Parameter)
        continue;
      if (Root.Kind != ERootKind::Owned)
        return true;
      Owned = true;
    }
    return !Owned;
  };
  // Of what \p Effect gives a function or a constructor that keeps it in the
  // object, \p Callee of it: KeepsOwnStorage, and, bound to a reference
  // parameter, what that keeps of what it is bound to.
  auto KeepsOwnStorageIn = [&](const CParameterKept &Callee,
                               const COperandEffect &Effect) {
    return KeepsOwnStorage(Effect) &&
           (Effect.Use != EUse::Bind || Callee.bOwnStorage);
  };
  // The definitions of each member of its object that can run: of a
  // constructor its initializer, and what assigns it in the body.
  llvm::DenseMap<const FieldDecl *, llvm::SmallVector<const void *, 2>> Defined;
  if (OwnObject) {
    if (Constructor)
      for (const CXXCtorInitializer *Initializer : Constructor->inits())
        if (const FieldDecl *Field = Initializer->getMember())
          Defined[Field].push_back(Initializer);
    const CEffectIndex &Index = sharedFlowCache().indexOf(Body, Context);
    forEachRunning(Definition, Body, Body, Context, [&](const Stmt *S) {
      for (const COperandEffect &Effect : Index.effectsOf(S)->Operands)
        if (Effect.Into.Kind == EDestination::Designated && Effect.Into.Object)
          if (const FieldDecl *Field =
                  ownMember(Effect.Into.Object, Definition, Context))
            Defined[Field].push_back(S);
    });
  }
  // The members kept as a whole somewhere, whose paths (MemberPaths) go
  // where it ends, in whichever order the body and the initializers keep it.
  llvm::SmallPtrSet<const FieldDecl *, 2> WholeMembers;
  // \p Field is a member the object keeps it in as its value.
  auto AddMember = [&](const FieldDecl *Field) {
    WholeMembers.insert(Field);
    if (!llvm::is_contained(Kept.Members, Field))
      Kept.Members.push_back(Field);
  };
  // The object keeps it in the part \p Path of a member, the member first.
  auto AddPath = [&](llvm::SmallVector<const FieldDecl *, 2> Path) {
    if (!llvm::is_contained(Kept.Members, Path.front()))
      Kept.Members.push_back(Path.front());
    if (!llvm::is_contained(Kept.MemberPaths, Path))
      Kept.MemberPaths.push_back(std::move(Path));
  };
  // Whether the member \p Field, given what it refers to by \p Made, holds
  // it where the function ends.
  auto InMember = [&](const FieldDecl *Field, const void *Made) {
    llvm::SmallVector<const void *, 2> Others;
    for (const void *Other : Defined.lookup(Field))
      if (Other != Made)
        Others.push_back(Other);
    return givesAtEnd(Definition, Made, Others, Context);
  };
  // The root followed, its index, and of the initializer of a base or the
  // constructor it delegates to, that construction.
  const CEffectIndex *const BodyIndex =
      &sharedFlowCache().indexOf(Body, Context);
  const CEffectIndex *Index = nullptr;
  const Stmt *BaseInit = nullptr;
  llvm::SmallPtrSet<const VarDecl *, 4> Locals;
  // The closures made of it or of a local that holds it, whose bodies are
  // followed after the body, each once, in the order they were found.
  llvm::SmallVector<const LambdaExpr *, 2> Closures;
  llvm::SmallPtrSet<const LambdaExpr *, 2> ClosuresFound;
  TCRecursive<void(const VarDecl *)> FollowLocal;
  // Where \p Holder, an object that keeps it, is in this body
  // (storageRootsOf; of an argument or a receiver given as a pointer, what
  // it points to, \p Pointee): what another
  // parameter refers to, points to or owns, the object, a local, followed by
  // its names, or a temporary of the full expression, followed in \p
  // Followed, or static storage, which lasts past every call. What can't be
  // told keeps nothing (Rule R).
  auto Place = [&](const Expr *Holder, const COperandEffect &Effect,
                   const CParameterKept &Callee, CFollowed &Followed,
                   bool Pointee) {
    if (!Holder)
      return;
    const CStorageRoots Roots = Pointee && Holder->getType()->isPointerType()
                                    ? pointeeRootsOf(Holder, Context)
                                    : storageRootsOf(Holder, Context);
    for (const CStorageRoot &Root : Roots.Roots)
      switch (Root.Kind) {
      case ERootKind::Parameter:
      case ERootKind::Owned:
      case ERootKind::Frame: {
        const auto *Other = dyn_cast_or_null<ParmVarDecl>(Root.Variable);
        if (Other && Other != Parameter &&
            Other->getDeclContext() == Definition &&
            (Root.Kind != ERootKind::Frame ||
             Other->getType()->isReferenceType())) {
          if (Other->getFunctionScopeIndex() < 64)
            Kept.Arguments |= uint64_t(1) << Other->getFunctionScopeIndex();
          break;
        }
        if (Root.Kind == ERootKind::Frame)
          FollowLocal(dyn_cast_or_null<VarDecl>(Root.Variable));
        break;
      }
      case ERootKind::Object:
      case ERootKind::Actor:
      case ERootKind::UnsafeThis:
        if (OwnObject) {
          Kept.bObject = Kept.bObjectWhole = true;
          Kept.bOwnStorage = Kept.bOwnStorage ||
                             (KeepsOwnStorage(Effect) &&
                              (Effect.Use != EUse::Bind || Callee.bOwnStorage));
        }
        break;
      case ERootKind::Temporary:
        Followed.More.push_back(Holder);
        break;
      case ERootKind::Static:
      case ERootKind::ThreadLocal:
        Kept.bStatic = true;
        break;
      case ERootKind::Unknown:
        break;
      }
  };
  // What a write that \p Effect gives what it refers to, at \p Made, keeps
  // in \p Written, a place of the object: the member's own storage, where it
  // holds it at the end (InMember), that member. Of what a pointer or
  // reference member on the way points or refers to (throughMember), where
  // the values that member can hold there (thisMemberDefinitions) each designate
  // a place (designatedPlaces), each of them with the rest of the way: of
  // the object again, a local, followed by its names, or what a parameter
  // refers to, that argument; where that can't be told and it can be a part
  // of the object by its type (canBeInObject), the object as a whole,
  // `m_rpJob = _pJob;` of `CJob const *&m_rpJob = m_pJob;`. A work stack,
  // each way at most 8 places through members.
  auto KeptInObject = [&](const CSlot &Written, const COperandEffect &Effect,
                          const Stmt *Made) {
    if (!Method)
      return;
    const QualType Object = Method->getFunctionObjectParameterType();
    llvm::SmallVector<std::pair<CSlot, unsigned>, 4> Work{{Written, 0}};
    while (!Work.empty()) {
      const auto [In, Depth] = Work.pop_back_val();
      if (In.Path.empty())
        continue;
      if (!throughMember(In)) {
        if (InMember(In.Path.front(), Made)) {
          Kept.bObject = true;
          Kept.bOwnStorage = Kept.bOwnStorage || KeepsOwnStorage(Effect);
          // Of a part of a member, that part
          // (CParameterKept::MemberPaths).
          if (In.Path.size() == 1)
            AddMember(In.Path.front());
          else
            AddPath(In.Path);
        }
        continue;
      }
      // The member it goes through first, and the rest of the way past it.
      size_t iThrough = In.Path.size() - 1;
      for (size_t iField = 0; iField + 1 < In.Path.size(); ++iField)
        if (crossesPointer(llvm::ArrayRef(In.Path).slice(iField, 2), 1)) {
          iThrough = iField;
          break;
        }
      const CSlot Holder{nullptr,
                         llvm::SmallVector<const FieldDecl *, 2>(
                             In.Path.begin(), In.Path.begin() + iThrough + 1),
                         false, EPlace::This};
      const llvm::ArrayRef<const FieldDecl *> Rest =
          llvm::ArrayRef(In.Path).drop_front(iThrough + 1);
      const std::optional<llvm::SmallVector<CThisDefinition, 2>> Values =
          Depth < 8 ? thisMemberDefinitions({Definition, Holder.Path, nullptr},
                                            Made, Context)
                    : std::nullopt;
      bool Told = Values && !Values->empty();
      llvm::SmallVector<CSlot, 4> Next;
      for (const CThisDefinition &Value :
           Values ? *Values : llvm::SmallVector<CThisDefinition, 2>()) {
        if (!Told)
          break;
        bool PlacesTold = true;
        const llvm::SmallVector<CSlot, 2> Places =
            !Value.Entry && !Value.Called && Value.Value
                ? designatedPlaces(Value.Value, Context, &PlacesTold)
                : llvm::SmallVector<CSlot, 2>();
        Told = PlacesTold && !Places.empty();
        for (CSlot Reached : Places) {
          // Past the member, the rest of the way, with its element.
          if (!Rest.empty()) {
            llvm::append_range(Reached.Path, Rest);
            Reached.Element = In.Element;
            Reached.Index = In.Index;
          }
          Next.push_back(std::move(Reached));
        }
      }
      if (!Told) {
        const QualType Last = In.Path.back()->getType();
        if (canBeInObject(In.Element && !Last->getPointeeType().isNull()
                              ? Last->getPointeeType()
                              : Last,
                          Object, Context)) {
          Kept.bObject = Kept.bObjectWhole = true;
          Kept.bOwnStorage = Kept.bOwnStorage || KeepsOwnStorage(Effect);
        }
        continue;
      }
      for (const CSlot &Reached : Next)
        switch (Reached.Place) {
        case EPlace::This:
          Work.push_back({Reached, Depth + 1});
          break;
        case EPlace::Variable:
          FollowLocal(Reached.Variable);
          break;
        case EPlace::Referent:
          if (const auto *Other = dyn_cast_or_null<ParmVarDecl>(Reached.Variable);
              Other && Other != Parameter &&
              Other->getDeclContext() == Definition &&
              Other->getFunctionScopeIndex() < 64)
            Kept.Arguments |= uint64_t(1) << Other->getFunctionScopeIndex();
          break;
        }
    }
  };
  auto Policy = [&](const CEffectIndex::CUse &Use,
                    const CEffects &Of) -> CFollowed {
    const COperandEffect &Effect = *Use.Effect;
    switch (Effect.Use) {
    // What a member function of it gives.
    case EUse::CallObject:
      return {false, dyn_cast<Expr>(Use.Parent)};
    // An address as a number, which can't hold a pointer, and what can't be
    // told keep nothing (Rule R).
    case EUse::Value:
    case EUse::Opaque:
      return {};
    case EUse::Bind:
    case EUse::Copy:
      break;
    case EUse::Forward:
    case EUse::Access:
    case EUse::Update:
    case EUse::Dereference:
    case EUse::AddressOf:
    case EUse::Callee:
    case EUse::Condition:
    case EUse::Discarded:
    case EUse::Statement:
    case EUse::Unevaluated:
    case EUse::Write:
      return {};
    }
    switch (Effect.Into.Kind) {
    // A local of it, followed by its names; a static keeps it past every
    // call.
    case EDestination::Variable: {
      const auto *Local = dyn_cast_or_null<VarDecl>(Effect.Into.Declaration);
      if (Local && Local->hasLocalStorage())
        FollowLocal(Local);
      else if (Local && Refers(Effect))
        Kept.bStatic = true;
      return {};
    }
    // Assigned to a member of its object, kept when it is there at the end;
    // to a local slot, followed by its names; elsewhere, kept.
    case EDestination::Designated: {
      const Expr *Target = Effect.Into.Object;
      // Of the object, by what the write designates (designatedPlaces), each
      // a place of the object (KeptInObject); where each is told, nowhere
      // else.
      if (OwnObject && Target) {
        bool PlacesTold = true;
        const llvm::SmallVector<CSlot, 2> Places =
            designatedPlaces(Target, Context, &PlacesTold);
        if (PlacesTold && !Places.empty() &&
            llvm::all_of(Places, [](const CSlot &In) {
              return In.Place == EPlace::This && !In.Path.empty();
            })) {
          if (Refers(Effect))
            for (const CSlot &In : Places)
              KeptInObject(In, Effect, Use.Parent);
          return {};
        }
      }
      bool Told = true;
      const llvm::SmallVector<CSlot, 2> Slots =
          Target ? designatedSlots(Target, Context, &Told)
                 : llvm::SmallVector<CSlot, 2>();
      if (!Told || Slots.empty()) {
        // Elsewhere, where it is (Place); also what it designates of what
        // another parameter refers to or points to, in the argument, and a
        // member of the object it is in, in that member (designatedPlaces);
        // where that can't be told, what another parameter refers to or
        // points to that the way to it starts at, through what a member of
        // that points or refers to too, `_View.m_rpJob = _pOld;`
        // (designatedRoot).
        CFollowed Followed;
        if (Refers(Effect)) {
          Place(Target, Effect, CParameterKept(), Followed, false);
          auto InArgument = [&](const ValueDecl *Holder) {
            if (const auto *Other = dyn_cast_or_null<ParmVarDecl>(Holder);
                Other && Other != Parameter &&
                Other->getDeclContext() == Definition &&
                Other->getFunctionScopeIndex() < 64)
              Kept.Arguments |= uint64_t(1) << Other->getFunctionScopeIndex();
          };
          const llvm::SmallVector<CSlot, 2> Places =
              designatedPlaces(Target, Context, nullptr);
          for (const CSlot &In : Places)
            if (In.Place == EPlace::This && OwnObject && !In.Path.empty())
              KeptInObject(In, Effect, Use.Parent);
            else if (In.Place == EPlace::Referent)
              InArgument(In.Variable);
          // Where no place can be told: what another parameter refers to
          // that the way starts at, in that argument; of the object, through
          // a pointer or reference member whose target can't be told, the
          // object as a whole where that can be a part of it by its type
          // (canBeInObject), `m_rpJob = _pJob;`.
          if (Places.empty()) {
            if (const CDesignatedRoot Root = designatedRoot(Target, Definition);
                Root.Root == ERoot::Parameter)
              InArgument(Root.Variable);
            else if (Root.Root == ERoot::Object && Root.bThrough && OwnObject &&
                     canBeInObject(Target->getType(),
                                   Method->getFunctionObjectParameterType(),
                                   Context)) {
              Kept.bObject = Kept.bObjectWhole = true;
              Kept.bOwnStorage = Kept.bOwnStorage || KeepsOwnStorage(Effect);
            }
          }
        }
        return Followed;
      }
      // A local slot, followed by its names; one of static storage keeps
      // it past every call.
      for (const CSlot &Slot : Slots) {
        const auto *Variable = dyn_cast_or_null<VarDecl>(Slot.Variable);
        if (Variable && !Variable->hasLocalStorage()) {
          Kept.bStatic = Kept.bStatic || Refers(Effect);
          continue;
        }
        FollowLocal(Slot.Variable);
      }
      return {};
    }
    // Given to what is constructed or called: what that keeps of it
    // (parameterKept), a base of the object as the object; what the callee
    // gives back of it.
    case EDestination::Parameter: {
      if (Of.Statement == EStatement::Construction) {
        const auto *Construct = dyn_cast<CXXConstructExpr>(Use.Parent);
        if (!Construct)
          return {};
        const CXXConstructorDecl *Called = Construct->getConstructor();
        if (!Refers(Effect))
          return {};
        // A copy of a type that isn't a view owns what it holds.
        if (Called && Called->isCopyOrMoveConstructor() &&
            Effect.Into.Index == 0 &&
            !isDefaultViewType(Construct->getType(), Context))
          return {};
        const CHeldIn Held =
            heldIn(Use.Parent, Of, Effect.Into.Index,
                   dyn_cast_or_null<Expr>(Effect.Operand), Context);
        CFollowed Followed;
        Kept.bStatic |= Held.Kept.bStatic;
        for (const Expr *Argument : Held.Arguments)
          Place(Argument, Effect, Held.Kept, Followed, true);
        if (Held.Made && Use.Parent == BaseInit) {
          Kept.bObject = Kept.bObjectWhole = true;
          Kept.bOwnStorage =
              Kept.bOwnStorage || KeepsOwnStorageIn(Held.Kept, Effect);
          return Followed;
        }
        Followed.Next = Held.Made;
        return Followed;
      }
      if (Of.Statement != EStatement::Call)
        return {};
      const auto *Call = dyn_cast<CallExpr>(Use.Parent);
      if (!Call)
        return {};
      if (givesItsArgument(Call))
        return {false, Call};
      if (!Refers(Effect))
        return {false, Call};
      // Where the callee keeps it (heldIn), through a pointer to a function
      // or any other callee by its type. Kept in the object of a member
      // function called on its own object, also with an explicit object,
      // `this->f_Store(_p)` or `_This.f_Store(_p)`, which is the first
      // argument, or on a member of it, `m_Text = _Text` of a record member;
      // elsewhere, in the receiver or what another argument refers to,
      // where that is (Place): `fs_Create(*this, _Type)` keeps it in the
      // object.
      const CHeldIn Held = heldIn(Call, Of, Effect.Into.Index,
                                  dyn_cast_or_null<Expr>(Effect.Operand),
                                  Context);
      CFollowed Followed;
      Followed.Next = Held.Made;
      Kept.bStatic |= Held.Kept.bStatic;
      if (const Expr *Object = Held.Receiver) {
        if (OwnObject &&
            (effectsOf(sameValue(Object, Context), Context).Statement ==
                 EStatement::This ||
             namesExplicitObjectOf(Object, Definition) ||
             ownMember(Object, Definition, Context))) {
          Kept.bObject = true;
          // Called on `this`, a member function whose body tells the members
          // it keeps it in keeps it in those (through the summaries),
          // `fp_Set(_pJob)`; else, or on a member, in the object as a whole.
          if (effectsOf(sameValue(Object, Context), Context).Statement ==
                  EStatement::This &&
              !Held.Kept.bUnknown && !Held.Kept.bObjectWhole &&
              !Held.Kept.Members.empty())
            for (const FieldDecl *Field : Held.Kept.Members)
              AddMember(Field);
          else
            Kept.bObjectWhole = true;
          Kept.bOwnStorage =
              Kept.bOwnStorage || KeepsOwnStorageIn(Held.Kept, Effect);
        } else
          Place(Object, Effect, Held.Kept, Followed, true);
      }
      for (const Expr *Argument : Held.Arguments)
        Place(Argument, Effect, Held.Kept, Followed, true);
      return Followed;
    }
    // Given back, which the caller follows (returnedRoots).
    case EDestination::Return:
      return {};
    // A closure, an object on the heap, an element or a member made of it:
    // that value, followed; what isn't one, thrown, keeps nothing past the
    // call (Rule R). Of a closure, also what its body does with it, where it
    // can be called (Closures).
    case EDestination::Capture:
      if (const auto *Lambda =
              dyn_cast_or_null<LambdaExpr>(Effect.Into.Object);
          Lambda && Lambda->getBody() && ClosuresFound.insert(Lambda).second)
        Closures.push_back(Lambda);
      [[fallthrough]];
    case EDestination::Heap:
    case EDestination::Temporary:
    case EDestination::Element:
    case EDestination::Field:
    case EDestination::Base:
      return {false, dyn_cast_or_null<Expr>(Effect.Into.Object)};
    case EDestination::None:
    case EDestination::Thrown:
      return {};
    }
    return {};
  };
  // Each name of \p Variable that runs, of the root followed: where it is
  // kept, Policy records.
  auto FollowNames = [&](const ValueDecl *Variable) {
    for (const Stmt *S : Index->statements())
      if (effectsOf(S, Context).Statement == EStatement::Name &&
          cast<DeclRefExpr>(S)->getDecl() == Variable &&
          Index->evaluated(S) &&
          (Index != BodyIndex || canRun(Definition, Body, S, Context)))
        (void)followUses(cast<Expr>(S), *Index, Context, Policy);
  };
  FollowLocal = [&](const VarDecl *Local) {
    if (Local && Locals.insert(Local).second)
      FollowNames(Local);
  };
  // The body, of what can run, and of a constructor each initializer, which
  // runs, what it initializes given its value.
  Index = BodyIndex;
  FollowNames(Parameter);
  // In the body of each closure made of it, its names and those of the
  // locals that hold it, captured.
  for (size_t iClosure = 0; iClosure < Closures.size() && iClosure < 64;
       ++iClosure) {
    Index = &sharedFlowCache().indexOf(Closures[iClosure]->getBody(), Context);
    FollowNames(Parameter);
    for (const VarDecl *Local :
         llvm::SmallVector<const VarDecl *, 4>(Locals.begin(), Locals.end()))
      FollowNames(Local);
  }
  // Past as many closures as it follows, where it keeps it can't be told:
  // where a body that can't be seen keeps it, by its type (keptByType).
  if (Closures.size() > 64) {
    const CParameterKept ByType =
        keptByType(Constructor ? QualType() : Definition->getReturnType(),
                   parameterTypesOf(Definition), iParameter,
                   keepingObjectOf(Definition), Context);
    Kept.bUnknown = true;
    Kept.bResult = Kept.bResult || ByType.bResult;
    Kept.Arguments |= ByType.Arguments;
    if (ByType.bObject) {
      Kept.bObject = Kept.bObjectWhole = true;
      Kept.bOwnStorage = Kept.bOwnStorage || ByType.bOwnStorage;
    }
  }
  Index = BodyIndex;
  if (Constructor)
    for (const CXXCtorInitializer *Initializer : Constructor->inits()) {
      const Expr *Init = Initializer->getInit();
      if (!Init)
        continue;
      Index = &sharedFlowCache().indexOf(Init, Context);
      BaseInit = Initializer->isBaseInitializer() ||
                         Initializer->isDelegatingInitializer()
                     ? sameValue(Init, Context)
                     : nullptr;
      Locals.clear();
      FollowNames(Parameter);
      const FieldDecl *Field = Initializer->getMember();
      if (!Field)
        continue;
      COperandEffect Given;
      Given.Operand = Init;
      Given.Use = Field->getType()->isReferenceType() ? EUse::Bind : EUse::Copy;
      if (Refers(Given) && InMember(Field, Initializer)) {
        Kept.bObject = true;
        Kept.bOwnStorage = Kept.bOwnStorage || KeepsOwnStorage(Given);
        // Of a record made as an aggregate, the members in it given it, by
        // a work stack over the nested aggregates; where an element can't be
        // told, the record as a whole.
        llvm::SmallVector<llvm::SmallVector<const FieldDecl *, 2>, 2> Paths;
        llvm::SmallVector<std::pair<const Expr *,
                                    llvm::SmallVector<const FieldDecl *, 2>>,
                          4>
            Work{{Init->IgnoreImplicit(), {Field}}};
        bool bTold = true;
        for (int Steps = 0; !Work.empty() && bTold && Steps < 64; ++Steps) {
          auto [Made, Path] = Work.pop_back_val();
          const auto *Record = Made->getType()->getAsRecordDecl();
          if (!isa<InitListExpr, CXXParenListInitExpr>(Made) || !Record ||
              Path.size() >= 8) {
            bTold = false;
            break;
          }
          for (const FieldDecl *Member : Record->fields()) {
            const Expr *Element = aggregateMemberInit(Made, Member);
            if (!Element)
              continue;
            Element = Element->IgnoreImplicit();
            llvm::SmallVector<const FieldDecl *, 2> Inner = Path;
            Inner.push_back(Member);
            if (isa<InitListExpr, CXXParenListInitExpr>(Element)) {
              Work.push_back({Element, std::move(Inner)});
              continue;
            }
            COperandEffect Part;
            Part.Operand = Element;
            Part.Use =
                Member->getType()->isReferenceType() ? EUse::Bind : EUse::Copy;
            if (Refers(Part))
              Paths.push_back(std::move(Inner));
          }
        }
        if (bTold && Work.empty() && !Paths.empty())
          for (auto &Path : Paths)
            AddPath(std::move(Path));
        else
          AddMember(Field);
      }
    }
  // A member kept as a whole anywhere is kept as a whole.
  llvm::erase_if(Kept.MemberPaths,
                 [&](const llvm::SmallVector<const FieldDecl *, 2> &Path) {
                   return WholeMembers.contains(Path.front());
                 });
  return Kept;
}

/// The effect index of the body \p Use is in, of the function or of a lambda
/// in it, which tells what is done with it.
static const CEffectIndex &useIndexOf(const Stmt *Use, const CParents &Parents,
                                      ASTContext &Context) {
  const Stmt *Root = Use;
  for (const Stmt *Child = Use, *Up = Parents.getParent(Use); Up;
       Child = Up, Up = Parents.getParent(Up)) {
    if (effectsOf(Up, Context).Statement == EStatement::Lambda &&
        cast<LambdaExpr>(Up)->getBody() == Child)
      break;
    Root = Up;
  }
  return sharedFlowCache().indexOf(Root, Context);
}

/// Whether the occurrence \p Use of borrowed data only uses a value of it, so
/// that nothing keeps a reference to the data: what it does to the storage
/// at each level of it that is the data (borrowedStorageOf), the one walk of
/// it (storageUsesOf), keeps it nowhere beyond the frame and holds it in no
/// name of the frame. `&_Job` gives a pointer value, which the same rules
/// follow. Captured by a closure that runs later, it is kept unless what
/// that closure holds keeps it alive (keptContext, Rule K); what `co_return`
/// gives is kept by the result as it is made from it, by type
/// (resultMadeFrom); the promise's calls are plumbing.
static bool runsOnlyBeforeSuspension(const LambdaExpr *Lambda,
                                     const SuspensionFlow &Flow);

static bool isValueUse(const Expr *Use, const CParents &Parents,
                       const BorrowRoots &Roots, ASTContext &Context,
                       QualType Result, const SuspensionFlow &Flow) {
  auto IsUse = [&](const Expr *At) { return At == Use; };
  auto Refers = [&](const Expr *Value, unsigned Level) {
    const CObjectStorage Of =
        borrowedStorageOf(Value, Level, IsUse, Roots, Context);
    return Of.bOwn || Of.bUnknown;
  };
  // A result that can refer to anything, a reference, a pointer or a view.
  const bool ResultCanRefer =
      !Result.isNull() &&
      (Result->isReferenceType() || Result->isPointerType() ||
       Result->isMemberPointerType() ||
       (Roots.IsView && Roots.IsView(Result.getNonReferenceType())));
  // Where the walk ends beyond the frame: a capture of a closure that runs
  // later keeps it unless alive in what the closure holds; the result of a
  // `co_return` by what it is made of.
  auto KeptAt = [&](const Expr *E, unsigned At, const COperandEffect &Effect,
                    const Stmt *Parent) {
    if (Effect.Into.Kind == EDestination::Capture) {
      const auto *Lambda = dyn_cast_or_null<LambdaExpr>(Effect.Into.Object);
      if (!Lambda)
        return true;
      // Nor by one whose body runs only before any suspension, where it is
      // called, which reads it there.
      if (runsOnlyBeforeSuspension(Lambda, Flow))
        return false;
      CKeptAt In;
      In.Kind = CKeptAt::EKind::Capture;
      In.Closure = Lambda;
      return !aliveAtUse(E, At, keptContext(In, Context), Context, E);
    }
    // Of a `co_return`, also as the promise's `return_value` is given it.
    const auto *Promise = dyn_cast<CallExpr>(Parent);
    if (Effect.Into.Kind == EDestination::Return && !Result.isNull() &&
        (effectsOf(Parent, Context).Statement == EStatement::Coreturn ||
         (Promise && isPromiseCall(Promise)))) {
      const CResultMade Made = resultMadeFrom(E, Result, Context);
      if (E->isGLValue() && At == 0)
        return Made.bDesignated;
      return Made.bHeld && !holdsNoAddress(E->getType(), Context) &&
             (!Made.bCopy || !E->isGLValue() || ResultCanRefer);
    }
    return true;
  };
  // Each level its type has: of a glvalue its storage, of a pointer what it
  // points to, of a record what its parts refer to.
  QualType Type = Use->getType().getNonReferenceType();
  int Levels = Use->isGLValue() ? 0 : -1;
  while (const auto *Pointer = Type->getAs<PointerType>()) {
    ++Levels;
    Type = Pointer->getPointeeType();
  }
  if (Type->isRecordType() || Type->isArrayType())
    ++Levels;
  CStorageUses Uses;
  for (int Level = 0; Level <= Levels && Level < 4; ++Level)
    if (Refers(Use, Level))
      Uses |= storageUsesOf(Use, Level, Context, FParentOf(Parents), KeptAt);
  return !Uses.bKept && !Uses.bHeld;
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

/// Whether the body of \p Lambda, which isn't a coroutine, runs only before
/// any suspension of the function it is made in, whose flow is \p Flow: its
/// value stays there (closureUses), also in the locals it is put in, and
/// each call of it is an element of the flow that can't run after a
/// suspension; of a function object of SynchronousFunctionTypes, the calls
/// its definition that gives it the closure reaches (wrapperFlow). Its body
/// reads its captures where it is called. One that no call reaches is one
/// only where each function object it is in is given another value, which
/// destroys it; else it is held as a value of the frame is.
static bool runsOnlyBeforeSuspension(const LambdaExpr *Lambda,
                                     const SuspensionFlow &Flow) {
  const CXXMethodDecl *Operator = Lambda->getCallOperator();
  if (!Flow.Built || !Operator || !Operator->getBody() ||
      isa<CoroutineBodyStmt>(Operator->getBody()))
    return false;
  const auto *Function = dyn_cast_or_null<FunctionDecl>(
      Lambda->getLambdaClass()->getDeclContext());
  if (!Function || !Function->getBody())
    return false;
  ASTContext &Context = Operator->getASTContext();
  auto Before = [&](const CClosureUses &Uses) {
    return Uses.bLocal && Uses.Escapes.empty() && Uses.Closures.empty();
  };
  auto CallBefore = [&](const CClosureSite &Site) {
    return Site.Kind == EClosureUse::Call && !Site.Through &&
           Site.Use.Parent && Flow.Elements.contains(Site.Use.Parent) &&
           !Flow.After.contains(Site.Use.Parent);
  };
  const CClosureUses &Uses = sharedFlowCache().closureUses(
      Function, Function->getBody(), Lambda, Context);
  if (!Before(Uses) || !llvm::all_of(Uses.Calls, CallBefore))
    return false;
  bool bCalled = !Uses.Calls.empty();
  bool bReplaced = !Uses.Locals.empty();
  for (const CClosureLocal &Local : Uses.Locals) {
    if (Local.Owner != Function)
      return false;
    const CClosureUses &Named = sharedFlowCache().closureUses(
        Function, Function->getBody(), Local.Variable, Context);
    if (!Before(Named))
      return false;
    const CWrapperFlow *Wrapper =
        Local.Given ? &sharedFlowCache().wrapperFlow(
                          Function, Function->getBody(), Local.Variable,
                          Context)
                    : nullptr;
    const std::optional<unsigned> Given =
        Wrapper && Wrapper->bTold ? Wrapper->definitionOf(Local.Given)
                                  : std::nullopt;
    if (Wrapper && !Given)
      return false;
    bReplaced = bReplaced && Wrapper && Wrapper->Definitions.size() > 1;
    for (const CClosureSite &Site : Named.Calls) {
      // A call only another definition of the function object reaches runs
      // another callable.
      const auto *Operand =
          Wrapper && Site.Use.Effect
              ? dyn_cast_or_null<Expr>(Site.Use.Effect->Operand)
              : nullptr;
      const auto *Name =
          Operand ? dyn_cast_or_null<DeclRefExpr>(sameValue(Operand, Context))
                  : nullptr;
      if (Name)
        if (const auto It = Wrapper->Reaching.find(Name);
            It != Wrapper->Reaching.end() && !It->second.test(*Given))
          continue;
      if (!CallBefore(Site))
        return false;
      bCalled = true;
    }
  }
  return bCalled || bReplaced;
}

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
        const std::optional<QualType> Type =
            argumentParameterType(Call, iArgument);
        return Type &&
               ((*Type)->isReferenceType() || (*Type)->isPointerType()) &&
               Suspends(Arguments);
      }
      return false;
    };
    if (const CMemberCall Call = memberCallOf(Parent);
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
           (capturesCopy(Lambda, nullptr) || keepsObjectAlive(Lambda, Use) ||
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
      TCRecursive<bool(const Stmt *)> Suspends = [&](const Stmt *S) {
        if (!S || isa<LambdaExpr>(S))
          return false;
        if (const auto *Conditional = dyn_cast<ConditionalOperator>(S);
            Conditional && Flow.Context &&
            !Conditional->getCond()->isValueDependent()) {
          bool Chosen = false;
          if (foldsCondition(Conditional->getCond(), Chosen, *Flow.Context))
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
          if (foldsCondition(Logical->getLHS(), Left, *Flow.Context) &&
              Left == (Logical->getOpcode() == BO_LOr))
            return Suspends(Logical->getLHS());
        }
        return isSuspension(S) || llvm::any_of(keptChildren(S), Suspends);
      };
      // One held in a local that is only called, `auto fGet = [&Value] {
      // ... }; co_return fGet();`, runs where it is called, which in a
      // coroutine that never suspends is before any suspension.
      auto OnlyCalledInLocal = [&] {
        return onlyCalledInLocal(Lambda, Parents);
      };
      if (!isCalledWhereMade(Lambda, FParentOf(Parents), &Call)) {
        if (!(Flow.Built && Flow.After.empty() && OnlyCalledInLocal()) &&
            !runsOnlyBeforeSuspension(Lambda, Flow))
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
    TCRecursive<const VarDecl *(const Expr *, int)> ParameterOf =
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
    TCRecursive<void(const Stmt *)> CollectAssignments = [&](const Stmt *S) {
      if (!S || isa<LambdaExpr>(S))
        return;
      if (const auto *Assign = dyn_cast<BinaryOperator>(S);
          Assign && Assign->getOpcode() == BO_Assign)
        if (const auto *Target =
                dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts()))
          if (const auto *Local = dyn_cast<VarDecl>(Target->getDecl()))
            LocalAssignments[Local].push_back(Assign->getRHS());
      for (const Stmt *Child : keptChildren(S))
        CollectAssignments(Child);
    };
    CollectAssignments(Compound);
    // Also through a local made from it, or assigned it, `pAlias = _pValue`.
    auto Mentions = [&](const Stmt *S, const ValueDecl *Decl) {
      int Followed = 0;
      TCRecursive<bool(const Stmt *)> Visit = [&](const Stmt *Inner) {
        if (!Inner)
          return false;
        // Not the value a constant condition doesn't choose, `false ?
        // _pValue : &g_Value`.
        if (const auto *Conditional = dyn_cast<ConditionalOperator>(Inner)) {
          bool Chosen = false;
          if (!Conditional->getCond()->isValueDependent() &&
              foldsCondition(Conditional->getCond(), Chosen, Context))
            return Visit(Chosen ? Conditional->getTrueExpr()
                                : Conditional->getFalseExpr());
          // Nor the condition, which only chooses, `_pValue ? &g_First :
          // &g_Second`.
          return llvm::any_of(keptArms(Conditional),
                              [&](const Expr *Arm) { return Visit(Arm); });
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
        return llvm::any_of(keptChildren(Inner), Visit);
      };
      return Visit(S);
    };
    // An `if` and its `else` that each end by assigning it, also nested,
    // `if (bChoose) _pValue = &First; else _pValue = &Second;`, and the
    // assignments.
    TCRecursive<bool(const Stmt *, const VarDecl *&,
                       llvm::SmallVectorImpl<const Stmt *> &, int)> BranchesAssign = [&](const Stmt *S, const VarDecl *&Parameter,
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
              Taken = nondiscardedCaseOf(If);
            else if (foldsCondition(If->getCond(), Value, Context) &&
                     !hasJumpTarget(Value ? If->getElse() : If->getThen()))
              Taken = Value ? If->getThen() : If->getElse();
            if (Taken)
              return *Taken &&
                     BranchesAssign(*Taken, Parameter, Assignments, Depth + 1);
          }
          if (const auto *If = dyn_cast<IfStmt>(S))
            return If->getElse() && !If->getInit() &&
                   !If->getConditionVariable() && !If->isConsteval() &&
                   llvm::all_of(keptBranches(If), [&](const Stmt *Branch) {
                     return BranchesAssign(Branch, Parameter, Assignments,
                                           Depth + 1);
                   });
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
            TCRecursive<bool(const Stmt *)> Jumps = [&](const Stmt *Inner) {
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
    TCRecursive<void(const Expr *)> Sequenced = [&](const Expr *E) {
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
    TCRecursive<void(const Stmt *)> Statements = [&](const Stmt *Statement) {
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
    TCRecursive<void(const Stmt *, bool)> Visit = [&](const Stmt *S,
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
      for (const Stmt *Child : keptChildren(S))
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

namespace {
/// Where what \p E holds at \p Level is relative to the objects a result
/// is asked of (keptByResult), those reached as own added to \p Reached.
using FStorageOf = llvm::function_ref<CObjectStorage(
    const Expr *E, unsigned Level, llvm::SmallPtrSetImpl<const Expr *> *Reached)>;
} // namespace

/// Adds to \p Found the occurrences of the objects \p IsObject is true of
/// in what \p Value is made of that can give it what refers to them, by the
/// effect table: what it gives on, dereferences, addresses, the object of a
/// member function, what is bound or copied into what makes it, a load only
/// of what can hold a reference or a pointer (holdsReference), not a
/// condition, a value computed with, what is dropped or doesn't run. So an
/// object counts when its storage is given on as it is, `int const
/// &m_Value` bound to it, or its value can hold a reference, not a number
/// read of it, `fg_GetName(g_aNames, iName)`, nor a condition,
/// `bUseCache ? ... : ...`.
static void contributingObjects(const Expr *Value,
                                llvm::function_ref<bool(const Expr *)> IsObject,
                                ASTContext &Context,
                                llvm::SmallPtrSetImpl<const Expr *> &Found) {
  llvm::SmallVector<const Expr *, 16> Work{Value};
  llvm::SmallPtrSet<const Expr *, 32> Seen{Value};
  while (!Work.empty()) {
    const Expr *At = Work.pop_back_val();
    if (IsObject(At)) {
      Found.insert(At);
      continue;
    }
    for (const COperandEffect &Effect : effectsOf(At, Context).Operands) {
      const auto *Operand = dyn_cast_or_null<Expr>(Effect.Operand);
      if (!Operand)
        continue;
      const bool Holds =
          holdsReference(Operand->getType().getNonReferenceType());
      bool Contributes = false;
      switch (Effect.Use) {
      case EUse::Forward:
      case EUse::Dereference:
      case EUse::AddressOf:
      case EUse::CallObject:
      case EUse::Bind:
      case EUse::Copy:
      case EUse::Opaque:
        Contributes = Operand->isGLValue() || Holds;
        break;
      case EUse::Access:
      case EUse::Update:
        Contributes = Holds;
        break;
      case EUse::Write:
      case EUse::Callee:
      case EUse::Condition:
      case EUse::Value:
      case EUse::Discarded:
      case EUse::Statement:
      case EUse::Unevaluated:
        break;
      }
      if (Contributes && Seen.insert(Operand).second)
        Work.push_back(Operand);
    }
  }
}

/// The occurrences of the objects \p IsObject is true of that the result of
/// the coroutine \p Function keeps: what the operand of each `co_return` that
/// can run reaches as their own storage (\p Storage), as the result of the
/// type \p Result is made from it, by type (resultMadeFrom): what it
/// designates, or what it holds, of a pointer or a view what it points to,
/// but not of a copy of an object in a result that can't refer, \p
/// ResultCanRefer, which owns what it holds. What can't be told of what is
/// given where provenance is cut (CObjectStorage::bCut), not what a call
/// can keep, which is told by what it is given, can keep each of them that
/// can contribute to it (contributingObjects).
static llvm::SmallPtrSet<const Expr *, 8>
keptByResult(const FunctionDecl *Function,
             llvm::function_ref<bool(const Expr *)> IsObject,
             FStorageOf Storage, QualType Result, bool ResultCanRefer,
             ASTContext &Context) {
  llvm::SmallPtrSet<const Expr *, 8> Kept;
  const Stmt *Body = Function->getBody();
  // A result whose type can't hold a pointer, a reference or a view keeps
  // nothing, whatever it is made of (holdsBorrowed).
  if (!Body || (!Result.isNull() && !Result->isReferenceType() &&
                !holdsBorrowed(Result, Context)))
    return Kept;
  forEachRunning(Function, Body, Body, Context, [&](const Stmt *S) {
    if (effectsOf(S, Context).Statement != EStatement::Coreturn)
      return;
    // What the operand initializes the result with, by type
    // (resultMadeFrom); the promise's call is plumbing.
    const Expr *Given = cast<CoreturnStmt>(S)->getOperand();
    if (!Given)
      return;
    const CResultMade Made = resultMadeFrom(Given, Result, Context);
    if (Made.bDesignated)
      Storage(Given, 0, &Kept);
    if (!Made.bHeld || holdsNoAddress(Given->getType(), Context))
      return;
    if (Made.bCopy && Given->isGLValue() && !ResultCanRefer) {
      llvm::SmallPtrSet<const Expr *, 4> Copied;
      Storage(Given, 0, &Copied);
      if (!Copied.empty())
        return;
    }
    if (Storage(Given, heldAt(Given), &Kept).bCut)
      contributingObjects(Given, IsObject, Context, Kept);
  });
  return Kept;
}

/// The uses of the borrowed data of \p Roots that the result of \p Function
/// keeps (keptByResult), by borrowedStorageOf.
static llvm::SmallPtrSet<const Expr *, 8>
resultUses(const FunctionDecl *Function, const BorrowRoots &Roots,
           QualType Result, bool ResultCanRefer, ASTContext &Context) {
  auto IsRoot = [&](const Expr *At) { return Roots.IsRoot(At); };
  return keptByResult(
      Function, IsRoot,
      [&](const Expr *E, unsigned Level,
          llvm::SmallPtrSetImpl<const Expr *> *Reached) {
        return borrowedStorageOf(E, Level, IsRoot, Roots, Context, Reached,
                                 /*Result=*/true, Result);
      },
      Result, ResultCanRefer, Context);
}

const Expr *temporaryInResult(const FunctionDecl *Function) {
  ASTContext &Context = Function->getASTContext();
  const Stmt *Body = Function->getBody();
  const QualType Result = futureValueType(Function->getReturnType());
  const auto *View = Result.isNull() ? nullptr : Result->getAsCXXRecordDecl();
  if (!Body || !View || !View->hasDefinition() ||
      !isDefaultViewType(Result, Context))
    return nullptr;
  // Whether a constructor of the view made from \p Given, of \p Type, keeps
  // it by reference: each that takes it by reference as its only argument
  // can be the conversion; none, or one that keeps only a copy, can't be
  // told.
  auto KeptByConversion = [&](QualType Type) {
    bool Any = false;
    for (const CXXConstructorDecl *Constructor : View->ctors()) {
      if (Constructor->isCopyOrMoveConstructor() ||
          Constructor->getNumParams() == 0 ||
          Constructor->getMinRequiredArguments() > 1)
        continue;
      const QualType Parameter = Constructor->getParamDecl(0)->getType();
      if (!Parameter->isReferenceType())
        continue;
      const QualType Referent = Parameter.getNonReferenceType();
      const auto *Of = Referent->getAsCXXRecordDecl();
      const auto *Given = Type->getAsCXXRecordDecl();
      if (!Of || !Given || !Given->hasDefinition() ||
          (Of->getCanonicalDecl() != Given->getCanonicalDecl() &&
           !Given->isDerivedFrom(Of)))
        continue;
      const CParameterKept Kept =
          parameterKeptOf(Constructor, 0, /*Unseen=*/false);
      if (!Kept.bObject || !Kept.bOwnStorage)
        return false;
      Any = true;
    }
    return Any;
  };
  const Expr *Found = nullptr;
  forEachRunning(Function, Body, Body, Context, [&](const Stmt *S) {
    if (Found || effectsOf(S, Context).Statement != EStatement::Coreturn)
      return;
    const Expr *Given = cast<CoreturnStmt>(S)->getOperand();
    if (!Given || !takenWhenGiven(Given) || Given->isXValue())
      return;
    const QualType Type = Given->getType().getNonReferenceType();
    if (!Type->isRecordType() || Type->isDependentType() ||
        Context.hasSameUnqualifiedType(Type, Result) ||
        isDefaultViewType(Type, Context) || isHandleType(Type))
      return;
    if (KeptByConversion(Type))
      Found = Given;
  });
  return Found;
}

const Expr *copyInResult(const FunctionDecl *Operator,
                         llvm::function_ref<bool(const Expr *)> IsCopy) {
  ASTContext &Context = Operator->getASTContext();
  const QualType Result = futureValueType(Operator->getReturnType());
  const bool ResultCanRefer =
      !Result.isNull() &&
      (Result->isReferenceType() || Result->isPointerType() ||
       Result->isMemberPointerType() ||
       isDefaultViewType(Result.getNonReferenceType(), Context));
  const llvm::SmallPtrSet<const Expr *, 8> Kept = keptByResult(
      Operator, IsCopy,
      [&](const Expr *E, unsigned Level,
          llvm::SmallPtrSetImpl<const Expr *> *Reached) {
        // What a copy refers to isn't the closure's alone when the result
        // keeps it alive too (resultKeeps). A record the closure holds by
        // value, reached as a whole above level 0, a copy of it made member
        // by member, refers to the closure's storage only where a member of
        // it was given that (memberValues): `CPlain Copy = Record;` of a
        // capture `[Record]` given `&g_Value` refers to none of it, one given
        // `&Value` of a capture `[Value]` does.
        TCRecursive<bool(const Expr *, const CEffects &, unsigned,
                         const CProvenance &)>
            OwnAt = [&](const Expr *At, const CEffects &Effects,
                        unsigned AtLevel, const CProvenance &Walk) -> bool {
          if (resultKeeps(E, At, AtLevel, Context, Result))
            return false;
          const llvm::ArrayRef<CProjection> Taken = Walk.projections();
          bool Whole = AtLevel > 0;
          for (unsigned iLevel = 1; Whole && iLevel <= AtLevel; ++iLevel)
            Whole = iLevel >= Taken.size() || Taken[iLevel].Path.empty();
          if (Whole)
            if (const std::optional<llvm::SmallVector<CMemberValue, 4>> Members =
                    memberValues(At, AtLevel, Context))
              return llvm::any_of(*Members, [&](const CMemberValue &Member) {
                return storageOf(Member.Value, Member.Level, IsCopy,
                                 [&](const Expr *Inner,
                                     const CEffects &InnerEffects,
                                     unsigned InnerLevel,
                                     const CProvenance &InnerWalk) {
                                   return OwnAt(Inner, InnerEffects,
                                                InnerLevel, InnerWalk);
                                 },
                                 Context)
                    .bOwn;
              });
          return closureOwnAt(At, Effects, AtLevel, Walk);
        };
        return storageOf(
            E, Level, IsCopy,
            [&](const Expr *At, const CEffects &Effects, unsigned AtLevel,
                const CProvenance &Walk) {
              return OwnAt(At, Effects, AtLevel, Walk);
            },
            Context, Reached);
      },
      Result, ResultCanRefer, Context);
  // The first in the source; of those that begin at the same place, the one
  // around the other (isBeforeInSource), not the first of the set.
  const Expr *First = nullptr;
  for (const Expr *Each : Kept)
    if (!First || isBeforeInSource(Each, First, Context))
      First = Each;
  return First;
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
      const auto IsReference = [](const Stmt *S) {
        return isa<DeclRefExpr>(S);
      };
      UseCollector All(IsReference);
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
  // Whether the borrowed data is alive at \p Use (dataAliveAt, Rule K),
  // what it reads, or with \p Kept what it keeps.
  auto AliveAt = [&](const Stmt *Use, bool Kept = false) {
    return Borrow &&
           dataAliveAt(Function, Use, *Borrow, &Parents, Context, Kept);
  };
  // The use found, with its context where Rule K was asked of it
  // (BorrowRoots::Found).
  auto Found = [&](const Stmt *Use, bool Kept) {
    if (Borrow && Borrow->Found)
      *Borrow->Found = Borrow->AskKeepers
                           ? borrowUseAt(Function, Use, *Borrow, &Parents,
                                         Kept)
                           : CLifetimeUse{};
    return Use;
  };
  if (Borrow && Borrow->Found)
    *Borrow->Found = CLifetimeUse{};
  // Without a control flow, any use may be after a suspension.
  const bool Joined = Borrow && Borrow->Joined;
  if (!Flow.Built && !Collector.Uses.empty()) {
    if (Joined)
      return nullptr;
    for (const Stmt *Use : Collector.Uses)
      if (!AliveAt(Use))
        return Found(Use, false);
    return nullptr;
  }
  // A pointer parameter that a statement of the body assigns what it doesn't
  // borrow, like `pValue = &Local`, points there after it, when it is
  // assigned nowhere else and nothing jumps back.
  const SourceManager &SM = Context.getSourceManager();
  const auto &Redirections = Flow.Redirections;
  // Whether \p Parameter is pointed elsewhere where \p Use is, also through
  // a local reference to it that \p Use reads, `pCursor->f_GetLen()` of
  // `auto &pCursor = _pText; pCursor = &Local;`.
  TCRecursive<bool(const VarDecl *, const Stmt *)> RedirectedAt;
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
      const llvm::SmallVector<const Stmt *, 8> *Suspending = &Flow.Suspending;
      for (const IfStmt *If : Branch->second)
        if (SM.isBeforeInTranslationUnit(If->getEndLoc(), Use->getBeginLoc()) &&
            llvm::all_of(*Suspending, [&](const Stmt *Suspension) {
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
  // Assigning a pointer or a view held by value, `pValue = nullptr` or
  // `_Span = {g_Default, 8}`, also a member of such a view, `_Span.m_pData =
  // nullptr`, also where it isn't redirected for good, changes the copy of
  // the frame, not what it points to, and so does assigning one of the
  // closure, an init capture or one captured by copy, `[pValue = &Local]()
  // mutable { ... pValue = nullptr; }`: by the effect table, what is done
  // with it, past what gives it on, a cast to a reference to it or a member
  // of it held by value, is written to, by an assignment whose value is
  // dropped, or a copy or move assignment is called on it.
  auto IsOverwritten = [&](const Stmt *Use) {
    const auto *Variable =
        effectsOf(Use, Context).Statement == EStatement::Name
            ? dyn_cast<VarDecl>(cast<DeclRefExpr>(Use)->getDecl())
            : nullptr;
    if (!Variable || Variable->getType()->isReferenceType() ||
        !(Variable->getType()->isPointerType() ||
          (Borrow && Borrow->IsView && Borrow->IsView(Variable->getType()))) ||
        !(isa<ParmVarDecl>(Variable) || Variable->isInitCapture() ||
          isCapturedByCopyAt(cast<DeclRefExpr>(Use))))
      return false;
    const CEffectIndex &Index = useIndexOf(Use, Parents, Context);
    const llvm::SmallVector<CEffectIndex::CUse, 2> Consumers =
        Index.consumersOf(Use);
    bool Written = false;
    for (const CEffectIndex::CUse &Consumer : Consumers)
      switch (Consumer.Effect->Use) {
      case EUse::Write:
        Written = true;
        break;
      case EUse::CallObject:
        if (const auto *Method =
                dyn_cast_or_null<CXXMethodDecl>(Index.effectsOf(Consumer.Parent)
                                                    ? Index.effectsOf(Consumer.Parent)->Callee
                                                    : nullptr);
            !Method || !(Method->isCopyAssignmentOperator() ||
                         Method->isMoveAssignmentOperator()))
          return false;
        Written = true;
        break;
      case EUse::Statement:
      case EUse::Discarded:
        break;
      case EUse::Forward:
      case EUse::Access:
      case EUse::Update:
      case EUse::Dereference:
      case EUse::AddressOf:
      case EUse::Bind:
      case EUse::Copy:
      case EUse::Callee:
      case EUse::Condition:
      case EUse::Value:
      case EUse::Unevaluated:
      case EUse::Opaque:
        return false;
      }
    return Written;
  };
  // A pointer or a view parameter held by value whose part a use reads holds
  // only what replaced what it was given there, `_Span.m_pData = Buffer;`
  // before `_Span.m_pData`, or `_Span = {Buffer, _Span.m_nBytes};` before
  // `_Span`, refers to none of the data: by provenance from what the use
  // reads, past what gives it on as a glvalue (EUse::Forward), a data member
  // of it held by value, a cast to a reference to it, which reaches a name of
  // the parameter only where what that part holds can be what it was given
  // (CProvenance::reach).
  auto IsReplaced = [&](const Stmt *Use) {
    const auto *Name = dyn_cast<DeclRefExpr>(Use);
    if (!Name || effectsOf(Name, Context).Statement != EStatement::Name ||
        holderKind(Name->getDecl()) != EHolder::Parameter)
      return false;
    const auto *Parameter = cast<ParmVarDecl>(Name->getDecl());
    if (Parameter->getType()->isReferenceType() ||
        !(Parameter->getType()->isPointerType() ||
          (Borrow && Borrow->IsView && Borrow->IsView(Parameter->getType()))))
      return false;
    const CEffectIndex &Index = useIndexOf(Use, Parents, Context);
    const Expr *Read = Name;
    for (int Depth = 0; Depth < 16; ++Depth) {
      const llvm::ArrayRef<CEffectIndex::CUse> Uses = Index.usesOf(Read);
      const auto *Parent = Uses.size() == 1 && Uses[0].Effect->Use == EUse::Forward
                               ? dyn_cast_or_null<Expr>(Uses[0].Parent)
                               : nullptr;
      if (!Parent || !Parent->isGLValue())
        break;
      if (effectsOf(Parent, Context).Statement == EStatement::Member) {
        const ValueDecl *Member = memberDecl(Parent, Context);
        if (!Member || (holderKind(Member) != EHolder::Field &&
                        holderKind(Member) != EHolder::IndirectField))
          break;
      }
      Read = Parent;
    }
    CProvenance Walk(Context);
    bool Reaches = false;
    const bool Complete = Walk.reach(
        Read, heldAt(Read), [&](const Expr *At, unsigned, bool) {
          if (effectsOf(At, Context).Statement == EStatement::Name &&
              cast<DeclRefExpr>(At)->getDecl() == Parameter) {
            Reaches = true;
            return EReach::Root;
          }
          return EReach::Continue;
        });
    return Complete && !Reaches;
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
    const auto *Pointer = Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl()) : nullptr;
    if (!Pointer || !Pointer->getType()->isPointerType())
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
      TCRecursive<void(const Stmt *)> Visit = [&](const Stmt *S) {
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
            Named && Named->getDecl() == Alias && !RedirectedAt(Pointer, Named))
          OnlyAssigned = false;
        for (const Stmt *Child : keptChildren(S))
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
         (IsRedirected(Use) || IsOverwritten(Use) || IsReplaced(Use))) ||
        IsDiscarded(Use) || IsAssignedAlias(Use))
      continue;
    // A use is evaluated with the closest statement of the control flow that
    // contains it, unless it is in the body of a lambda or block, which runs
    // when that is called. A use the control flow doesn't cover counts as
    // after a suspension. Not where the data is alive there, of which only
    // what the result keeps is told below.
    if (useAfterSuspension(Use, Parents, Flow, Borrow) && !AliveAt(Use))
      return Found(Use, false);
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
  // Without a suspension, or joined, only the result outlives the call: the
  // uses of the data it keeps (resultUses). The result is judged by its type,
  // the `T` of the `TCFuture<T>`, what the promise stores, not by what
  // `co_return` is given: one that can't hold a pointer, a reference or a
  // view keeps nothing (holdsBorrowed), like `co_return fg_Format(...,
  // Value)` in a `TCFuture<void>` or `co_return _Text` of a `CStrSpan` in a
  // `TCFuture<CStr>`.
  // Of a use where the data is alive (AliveAt), also in a coroutine that
  // suspends, only what the result keeps outlives the frame.
  const bool ResultCanKeep =
      ResultCanRefer || (!Result.isNull() && Result->isRecordType() &&
                         holdsBorrowed(Result, Context));
  std::optional<llvm::SmallPtrSet<const Expr *, 8>> InResult;
  auto KeptByResult = [&](const Expr *E) {
    if (!ResultCanKeep)
      return false;
    if (!InResult)
      InResult = resultUses(Function, *Borrow, Result, ResultCanRefer, Context);
    return InResult->contains(E);
  };
  if (OnlyResults && !ResultCanKeep)
    return nullptr;
  UseCollector Roots(Borrow->IsRoot);
  if (Borrow->Decl)
    Roots.Uses = Collector.Uses;
  else
    Roots.TraverseStmt(Body);
  for (const Stmt *Root : Roots.Uses) {
    // A parameter pointed at a local first, like `_pValue = &Local`, borrows
    // nothing after that, and neither does a local reference that only points
    // it elsewhere.
    if (IsRedirected(Root) || IsAssignedAlias(Root) ||
        ((!Borrow || !Borrow->OfClosure) && IsReplaced(Root)))
      continue;
    const auto *E = dyn_cast<Expr>(Root);
    // A lambda that captures a copy uses its own copy, and code that can't run
    // uses nothing.
    // Without a suspension, or joined, what refers to it before the result
    // is given is gone by then, unless the result keeps it, also through a
    // local, like `auto *pValue = &Value; co_return pValue;`.
    if (!E || !useCanRun(Root, Parents, Flow, Borrow))
      continue;
    if (OnlyResults || AliveAt(Root, /*Kept=*/true)) {
      if (KeptByResult(E))
        return Root;
    } else if (!isValueUse(E, Parents, *Borrow, Context, Result, Flow))
      return Found(Root, true);
  }
  return nullptr;
}

bool aliveAtEachUse(const FunctionDecl *Function,
                    llvm::function_ref<bool(const Stmt *)> IsUse,
                    const BorrowRoots &Borrow, ASTContext &Context) {
  const Stmt *Body = writtenBody(Function);
  if (!Body || !Borrow.AskKeepers)
    return false;
  const SuspensionFlow &Flow = suspensionFlow(Function, Context);
  // Each statement that runs once, by a work stack over what runs.
  bool Used = false;
  llvm::SmallVector<const Stmt *, 32> Work{Body};
  llvm::SmallPtrSet<const Stmt *, 32> Seen;
  while (!Work.empty()) {
    const Stmt *S = Work.pop_back_val();
    if (!Seen.insert(S).second)
      continue;
    if (IsUse(S)) {
      if (!dataAliveAt(Function, S, Borrow, Flow.Parents.get(), Context))
        return false;
      Used = true;
    }
    for (const Stmt *Child : keptChildren(S, Context))
      if (Child)
        Work.push_back(Child);
  }
  return Used;
}

bool isCoroutine(const FunctionDecl *Function) {
  const Stmt *Body = Function->getBody();
  if (!Body)
    return false;
  if (isa<CoroutineBodyStmt>(Body))
    return true;
  // Found once for each body, which the checks and the summaries of its
  // callers ask about again.
  struct CCoroutineBodies {
    llvm::DenseMap<const Stmt *, bool> Found;
  };
  auto &Bodies = astCache<CCoroutineBodies>(Function->getASTContext()).Found;
  if (const auto It = Bodies.find(Body); It != Bodies.end())
    return It->second;
  CoroutineKeywordFinder Finder;
  Finder.TraverseStmt(const_cast<Stmt *>(Body));
  Bodies[Body] = Finder.Found;
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
  const std::optional<llvm::APSInt> Value =
      templateArgumentValueOf(Flags, Enum->getASTContext());
  if (!Value)
    return true;
  for (const EnumConstantDecl *Enumerator : Enum->enumerators()) {
    if (identifierTextOf(Enumerator) == "ECoroutineFlag_AllowReferences")
      return haveBitInCommon(*Value, Enumerator->getInitVal());
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
          Template && identifierTextOf(Template) == "TCUnsafeFuture")
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
         isProtectedFuture(identifierTextOf(Specialization->getSpecializedTemplate()),
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

llvm::SmallVector<const FunctionDecl *, 2>
InstantiationIndex::bodiesOf(const FunctionDecl *Function,
                             ASTContext &Context) {
  if (!Function->isTemplated())
    return {Function};
  const llvm::ArrayRef<const FunctionDecl *> Instances =
      instantiationsOf(Function, Context);
  return {Instances.begin(), Instances.end()};
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
        llvm::is_contained(Bases, identifierTextOf(Current)))
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

/// Where \p Element is in the source: its statement, or what it ends or
/// destroys, a local or a temporary.
static SourceLocation elementLocation(const CFGElement &Element) {
  if (const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>())
    return Statement->getStmt()->getBeginLoc();
  if (const std::optional<CFGAutomaticObjDtor> Destructor =
          Element.getAs<CFGAutomaticObjDtor>())
    return Destructor->getVarDecl()->getLocation();
  if (const std::optional<CFGTemporaryDtor> Destructor =
          Element.getAs<CFGTemporaryDtor>())
    return Destructor->getBindTemporaryExpr()->getBeginLoc();
  if (const std::optional<CFGLifetimeEnds> End =
          Element.getAs<CFGLifetimeEnds>())
    return End->getVarDecl()->getLocation();
  if (const std::optional<CFGInitializer> Initializer =
          Element.getAs<CFGInitializer>())
    return Initializer->getInitializer()->getSourceLocation();
  return SourceLocation();
}

// Whether \p Loc is in what the `try` that \p Dispatch dispatches to the
// handlers of protects: its block, and of a function-try-block of a
// constructor the initializers between `try` and the block, `CView() try :
// m_Value(fg_Make()) {} catch (...) {}`.
static bool inTryOf(const CFGBlock *Dispatch, SourceLocation Loc,
                    const SourceManager &SM) {
  const auto *Try =
      Dispatch ? dyn_cast_or_null<CXXTryStmt>(Dispatch->getTerminatorStmt())
               : nullptr;
  if (!Try || !Try->getTryBlock() || Loc.isInvalid())
    return false;
  return SM.isPointWithin(Loc, Try->getTryLoc(),
                          Try->getTryBlock()->getEndLoc());
}

bool isTemplatedCode(const Decl *Owner) {
  return Owner && Owner->isTemplated();
}

std::unique_ptr<CFG> buildGraph(const Decl *Owner, const Stmt *Body,
                                ASTContext &Context,
                                const CFG::BuildOptions &Options) {
  // None of an invalid declaration, nor of code with an error that stops it
  // from compiling (hasUncompilableErrors), as clang's own analyses build
  // none: what the errors leave can't be relied on, and building one can
  // crash. A function without one can't be told.
  if (!Body || isTemplatedCode(Owner) || (Owner && Owner->isInvalidDecl()) ||
      hasUncompilableErrors(Context))
    return nullptr;
  // An enum can hold a value no enumerator names, a cast or a value read from
  // outside, so the default of a switch over each enumerator, or what follows
  // one without a default, can run.
  CFG::BuildOptions Reaching = Options;
  Reaching.AssumeReachableDefaultInSwitchStatements = true;
  return CFG::buildCFG(Owner, const_cast<Stmt *>(Body), &Context, Reaching);
}

void addExceptionEdges(CFG &Graph, const ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  for (const CFGBlock *Dispatch :
       llvm::make_range(Graph.try_blocks_begin(), Graph.try_blocks_end())) {
    auto *Handlers = const_cast<CFGBlock *>(Dispatch);
    // From each block with anything in the `try`, also of a `try` in it,
    // whose handlers aren't told apart.
    for (CFGBlock *Block : Graph) {
      if (!Block || Block == Handlers ||
          llvm::is_contained(Block->succs(), Handlers))
        continue;
      bool In =
          Block->getTerminatorStmt() &&
          inTryOf(Dispatch, Block->getTerminatorStmt()->getBeginLoc(), SM);
      for (const CFGElement &Element : *Block)
        In = In || inTryOf(Dispatch, elementLocation(Element), SM);
      if (In)
        Block->addSuccessor(CFGBlock::AdjacentBlock(Handlers, true),
                            Graph.getBumpVectorContext());
    }
  }
}

bool inDispatchTry(const CFGBlock *Dispatch, const CFGElement &Element,
                   const ASTContext &Context) {
  return isHandlerDispatch(Dispatch) &&
         inTryOf(Dispatch, elementLocation(Element), Context.getSourceManager());
}

llvm::SmallVector<CAggregatePart, 4> aggregateParts(const Expr *Init) {
  llvm::SmallVector<CAggregatePart, 4> Parts;
  llvm::ArrayRef<const Expr *> Inits;
  const Expr *Filler = nullptr;
  const FieldDecl *UnionField = nullptr;
  if (const auto *List = dyn_cast_or_null<InitListExpr>(Init)) {
    if (!List->isSemanticForm() && List->getSemanticForm())
      List = List->getSemanticForm();
    Init = List;
    Inits = llvm::ArrayRef<const Expr *>(List->getInits(), List->getNumInits());
    Filler = List->hasArrayFiller() ? List->getArrayFiller() : nullptr;
    UnionField = List->getInitializedFieldInUnion();
  } else if (const auto *Parens =
                 dyn_cast_or_null<CXXParenListInitExpr>(Init)) {
    Inits = Parens->getUserSpecifiedInitExprs();
    Filler = Parens->getArrayFiller();
    UnionField = Parens->getInitializedFieldInUnion();
  } else {
    return Parts;
  }
  const QualType Type = Init->getType();
  if (const ArrayType *Array = Type->getAsArrayTypeUnsafe()) {
    for (const Expr *Element : Inits)
      Parts.push_back({Element, Array->getElementType()});
    if (Filler)
      Parts.push_back({Filler, Array->getElementType()});
    return Parts;
  }
  const auto *Record = Type->getAsCXXRecordDecl();
  unsigned iInit = 0;
  if (Record && Record->hasDefinition() && Record->isUnion()) {
    if (UnionField && !Inits.empty())
      Parts.push_back({Inits[iInit++], UnionField->getType(), UnionField});
  } else if (Record && Record->hasDefinition()) {
    Record = Record->getDefinition();
    for (const CXXBaseSpecifier &Base : Record->bases()) {
      if (iInit >= Inits.size())
        break;
      Parts.push_back({Inits[iInit++], Base.getType(), nullptr, true});
    }
    for (const FieldDecl *Field : Record->fields()) {
      if (Field->isUnnamedBitField())
        continue;
      if (iInit >= Inits.size())
        break;
      Parts.push_back({Inits[iInit++], Field->getType(), Field});
    }
  }
  for (; iInit < Inits.size(); ++iInit)
    Parts.push_back({Inits[iInit], QualType()});
  if (Filler)
    Parts.push_back({Filler, QualType()});
  return Parts;
}

const Expr *aggregateMemberInit(const Expr *Init, const FieldDecl *Field) {
  for (const CAggregatePart &Part : aggregateParts(Init))
    if (Part.Field == Field) {
      if (const auto *Default = dyn_cast<CXXDefaultInitExpr>(Part.Init))
        return Default->getExpr();
      return isa<ImplicitValueInitExpr>(Part.Init) ? nullptr : Part.Init;
    }
  return nullptr;
}

bool isHandlerDispatch(const CFGBlock *Block) {
  return Block && isa_and_nonnull<CXXTryStmt>(Block->getTerminatorStmt());
}

const Expr *argumentOf(const CallExpr *Call, const FunctionDecl *Callee,
                       unsigned iParameter) {
  const unsigned Shift = argumentShift(Call, Callee);
  return iParameter + Shift < Call->getNumArgs()
             ? Call->getArg(iParameter + Shift)
             : nullptr;
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

static bool isChangedInCapturingLambdas(const DeclRefExpr *Ref,
                                        const VarDecl *Variable,
                                        ASTContext &Context);

/// The value of \p Condition when it is written as a constant, `true`, also
/// as the template argument it is instantiated with, `t_bUseOwn` of
/// `f_Clear<true>()`, or a constant expression the compiler evaluated.
static std::optional<bool> literalCondition(const Expr *Condition) {
  if (stackNearlyExhausted())
    return withStack([&] { return literalCondition(Condition); });
  for (int Depth = 0; Condition && Depth < 8; ++Depth) {
    Condition = Condition->IgnoreParenImpCasts();
    if (const auto *Bool = dyn_cast<CXXBoolLiteralExpr>(Condition))
      return Bool->getValue();
    if (const auto *Integer = dyn_cast<IntegerLiteral>(Condition))
      return !Integer->getValue().isZero();
    if (const auto *Constant = dyn_cast<ConstantExpr>(Condition);
        Constant && Constant->hasAPValueResult() &&
        Constant->getAPValueResult().isInt())
      return !Constant->getAPValueResult().getInt().isZero();
    if (const auto *Substituted =
            dyn_cast<SubstNonTypeTemplateParmExpr>(Condition)) {
      Condition = Substituted->getReplacement();
      continue;
    }
    if (const auto *Not = dyn_cast<UnaryOperator>(Condition);
        Not && Not->getOpcode() == UO_LNot) {
      const std::optional<bool> Value = literalCondition(Not->getSubExpr());
      return Value ? std::optional<bool>(!*Value) : std::nullopt;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

/// What \p E, a pointer, can be seen to point to as an expression: what it
/// is the address of, `&X` or `std::addressof(X)`, also through an unchanged
/// local pointer made with that, `auto ppItems = &pItems`. Null otherwise.
/// What the member \p Member of an unchanged local made by an aggregate is
/// made with, `Holder.m_ppValue` of `CHolder Holder{&t_pValue}`, also of a
/// member of a member held by value, and of a temporary aggregate, which
/// nothing changes before its member is read, `CView{&t_Value}.m_pValue`,
/// or null.
const Expr *localMemberInit(const Expr *Member) {
  llvm::SmallVector<const FieldDecl *, 4> Path;
  const Expr *Current = Member->IgnoreParenImpCasts();
  while (const auto *Part = dyn_cast<MemberExpr>(Current)) {
    const auto *Field = dyn_cast<FieldDecl>(Part->getMemberDecl());
    if (!Field || Part->isArrow())
      return nullptr;
    Path.insert(Path.begin(), Field);
    Current = Part->getBase()->IgnoreParenImpCasts();
  }
  // What the path selects of what an aggregate is made with.
  auto Select = [&](const Expr *Given) -> const Expr * {
    for (const FieldDecl *Field : Path) {
      if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Given))
        Given = Cast->getSubExpr()->IgnoreImplicit();
      if (const auto *Literal = dyn_cast<CompoundLiteralExpr>(Given))
        Given = Literal->getInitializer()->IgnoreImplicit();
      if (!isa<InitListExpr, CXXParenListInitExpr>(Given) ||
          Given->getType()->getAsRecordDecl() != Field->getParent())
        return nullptr;
      Given = aggregateMemberInit(Given, Field);
      // Not of a pointer or a reference member the aggregate isn't given,
      // which is what the code of its class gives it (targetToldByOwnClass).
      if (!Given || (isa<CXXDefaultInitExpr>(Given->IgnoreParenImpCasts()) &&
                     !targetToldByOwnClass(Field)))
        return nullptr;
      Given = Given->IgnoreImplicit();
    }
    return Given;
  };
  if (const Expr *Temporary = Current->IgnoreImplicit();
      !Path.empty() &&
      isa<InitListExpr, CXXParenListInitExpr, CXXFunctionalCastExpr,
          CompoundLiteralExpr>(Temporary))
    return Select(Temporary);
  const auto *Ref = dyn_cast<DeclRefExpr>(Current);
  const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  // A reference member stays bound to what it is made with, whatever else
  // changes; else only read where it is named, never written, given by
  // reference or its address taken, `View.m_pValue = &t_Value` or
  // `fg_Exchange(View.m_pValue, ...)`.
  if (Path.empty() || !Local || !Local->hasLocalStorage() ||
      isa<ParmVarDecl>(Local) || Local->getType()->isReferenceType() ||
      !Local->getInit())
    return nullptr;
  if (!Path.back()->getType()->isReferenceType()) {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
    if (!Function || !Function->getBody())
      return nullptr;
    ASTContext &Context = Local->getASTContext();
    CStmtWalk Walk(Function->getBody());
    while (const Stmt *Each = Walk.next()) {
      if (const auto *Named = dyn_cast<DeclRefExpr>(Each);
          Named && Named->getDecl() == Local) {
        // What it is used by, past its members and casts: not written, by
        // an assignment or a step, its address taken, bound to a reference
        // or given to a call by one, or the object of a member function.
        DynTypedNode Node = DynTypedNode::create(*Named);
        const Expr *Used = Named;
        for (int Step = 0; Step < 16; ++Step) {
          const auto Parents = Context.getParents(Node);
          if (Parents.empty())
            break;
          if (const auto *Part = Parents[0].get<MemberExpr>();
              Part && !Part->isArrow() &&
              isa<FieldDecl>(Part->getMemberDecl()) &&
              Part->getBase()->IgnoreParenImpCasts() ==
                  Used->IgnoreParenImpCasts()) {
            Used = Part;
            Node = Parents[0];
            continue;
          }
          if (const auto *Cast = Parents[0].get<ImplicitCastExpr>()) {
            if (Cast->getCastKind() == CK_LValueToRValue)
              break;
            Used = Cast;
            Node = Parents[0];
            continue;
          }
          if (Parents[0].get<ParenExpr>()) {
            Node = Parents[0];
            continue;
          }
          // A member function that isn't const can change it, implicit or
          // explicit object alike (receiverCallOf).
          if (const CMemberCall Call = receiverCallOf(
                  Parents[0], Used, const_cast<ASTContext &>(Context));
              Call && (!Call->getMethodDecl() ||
                       (!hasConstObject(Call->getMethodDecl()) &&
                        !Call->getMethodDecl()->isStatic())))
            return nullptr;
          if (const auto *Part = Parents[0].get<MemberExpr>())
            if (const auto *Method =
                    dyn_cast<CXXMethodDecl>(Part->getMemberDecl());
                Method && !hasConstObject(Method) && !Method->isStatic())
              return nullptr;
          if (const auto *Assign = Parents[0].get<BinaryOperator>();
              Assign && Assign->isAssignmentOp() &&
              Assign->getLHS()->IgnoreParenImpCasts() ==
                  Used->IgnoreParenImpCasts())
            return nullptr;
          if (const auto *Unary = Parents[0].get<UnaryOperator>();
              Unary && (Unary->isIncrementDecrementOp() ||
                        Unary->getOpcode() == UO_AddrOf))
            return nullptr;
          if (const auto *Bound = Parents[0].get<VarDecl>();
              Bound && Bound->getType()->isReferenceType() &&
              !Bound->getType().getNonReferenceType().isConstQualified())
            return nullptr;
          if (Parents[0].get<CallExpr>() ||
              Parents[0].get<CXXConstructExpr>()) {
            // Given by reference to what isn't const.
            if (Used->isGLValue() && !Used->getType().isConstQualified() &&
                !Used->getType()->isRecordType())
              return nullptr;
            if (Used->isGLValue() && Used->getType()->isRecordType())
              return nullptr;
          }
          break;
        }
      }
      Walk.addChildren(Each);
    }
  }
  return Select(Local->getInit()->IgnoreImplicit());
}

const Expr *addressedExpr(const Expr *E) {
  for (int Depth = 0; E && Depth < 16; ++Depth) {
    E = E->IgnoreParenImpCasts();
    // Also a pointer member of an unchanged local made by an aggregate,
    // `*Holder.m_ppValue` of `CHolder Holder{&t_pValue}` (localMemberInit).
    if (const Expr *Made = isa<MemberExpr>(E) ? localMemberInit(E) : nullptr;
        Made && E->getType()->isPointerType()) {
      E = Made;
      continue;
    }
    if (const auto *Address = dyn_cast<UnaryOperator>(E);
        Address && Address->getOpcode() == UO_AddrOf)
      return Address->getSubExpr();
    if (const auto *Call = dyn_cast<CallExpr>(E); isAddressOfCall(Call))
      return Call->getArg(0);
    const auto *Ref = dyn_cast<DeclRefExpr>(E);
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
        !Local->getType()->isPointerType() || !Local->getInit() ||
        !isUnchangedLocalAt(Ref, Local))
      return nullptr;
    E = Local->getInit();
  }
  return nullptr;
}

/// Whether \p E is a pointer held in what it is reached from
/// (isStoredPointer), or none when that can't be told within a budget.
static std::optional<bool> storedPointerOf(const Expr *E) {
  // Each expression once, within a budget, also through an alias whose
  // initializer reaches itself, `int *&rp = false ? rp : m_pValue`.
  llvm::SmallVector<const Expr *, 8> Work{E};
  llvm::SmallPtrSet<const Expr *, 16> Seen;
  while (!Work.empty()) {
    if (Seen.size() >= 256)
      return std::nullopt;
    E = Work.pop_back_val();
    if (!E || !Seen.insert(E).second)
      continue;
    if (!E->isGLValue() || !E->getType()->isPointerType())
      continue;
    // Past parentheses and casts that keep it what it is, the right of a
    // comma, and either value of a conditional.
    E = E->IgnoreParenImpCasts();
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
        Cast && Cast->isGLValue()) {
      Work.push_back(Cast->getSubExpr());
      continue;
    }
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E)) {
      for (const Expr *Arm : keptArms(Conditional))
        Work.push_back(Arm);
      continue;
    }
    // What a pointer to it points to, held where that is, `*ppItems` of
    // `auto ppItems = &pItems` is the local, or anywhere.
    if (const auto *Unary = dyn_cast<UnaryOperator>(E)) {
      if (Unary->getOpcode() == UO_Deref) {
        if (const Expr *Addressed = addressedExpr(Unary->getSubExpr()))
          Work.push_back(Addressed);
        else
          return true;
      }
      continue;
    }
    if (const auto *Binary = dyn_cast<BinaryOperator>(E)) {
      if (Binary->isCommaOp())
        Work.push_back(Binary->getRHS());
      else if (Binary->isPtrMemOp())
        return true;
      continue;
    }
    // Through a local reference to it, `auto &pItems = mp_pItems`, also a
    // name of a structured binding, `auto &[pValue, Count] = TL`, which is
    // what it binds.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
      if (const auto *Alias = dyn_cast<VarDecl>(Ref->getDecl());
          Alias && Alias->getType()->isReferenceType() && Alias->getInit() &&
          !isa<ParmVarDecl>(Alias))
        Work.push_back(Alias->getInit());
      else if (const auto *Binding = dyn_cast<BindingDecl>(Ref->getDecl());
               Binding && Binding->getBinding())
        Work.push_back(Binding->getBinding());
      continue;
    }
    // What a function gives back of what it is given as it is, `fg_Identity(
    // pValue)` of `int *&fg_Identity(int *&_pValue)`, is what that is.
    if (const auto *Call = dyn_cast<CallExpr>(E)) {
      const llvm::SmallVector<const Expr *, 2> Given =
          returnedArgumentChoices(Call);
      if (Given.empty())
        return true;
      llvm::append_range(Work, Given);
      continue;
    }
    if (isa<MemberExpr, ArraySubscriptExpr>(E))
      return true;
  }
  return false;
}

bool isStoredPointer(const Expr *E) {
  // What can't be told can be held anywhere.
  return storedPointerOf(E).value_or(true);
}

// The functions that combine relations, unknownOfRelations, mergeRelations
// and composeRelations, name each member of CObjectRelation by a structured
// binding, so a member added to it is a compile error in each of them until
// it decides how that member combines.

/// What can be any of \p Of, or anything made of them, when that can't be
/// told: unknown when any is related, through a pointer or a copy when all
/// that are related are, in a union, or null, when any can be. Which member
/// it is in and how deep can't be told.
static CObjectRelation unknownOfRelations(llvm::ArrayRef<CObjectRelation> Of) {
  CObjectRelation Result;
  bool AllThroughPointer = true;
  bool AllCopies = true;
  bool AllKeptAlive = true;
  for (const CObjectRelation &Each : Of) {
    [[maybe_unused]] const auto &[Kind, Outermost, ThroughPointer, Copy,
                                  InUnion, EntryDepth, StoredDepth, MadeValue,
                                  CanBeNull, KeptAlive] = Each;
    Result.bInUnion |= InUnion;
    if (Kind == EObjectRelation::Unrelated)
      continue;
    Result.Kind = EObjectRelation::Unknown;
    AllThroughPointer &= ThroughPointer;
    AllCopies &= Copy;
    AllKeptAlive &= KeptAlive;
    Result.bCanBeNull |= CanBeNull;
  }
  if (Result.Kind != EObjectRelation::Unrelated) {
    Result.bThroughPointer = AllThroughPointer;
    Result.bCopy = AllCopies;
    Result.bKeptAlive = AllKeptAlive;
  }
  return Result;
}

/// What \p Returned, the relation of what a member function gives to its
/// object, is relative to what \p Of, the relation of that object, is
/// relative to: of the object itself what \p Of is, of a member a member, of
/// the member it reaches first when the object is the object itself,
/// `f_Other()` giving `m_Other`, of an entry or what can't be told an entry,
/// at least one deeper for a lookup and at most one deeper for a change
/// through it, as deep as both; through a pointer, a copy, in a union, made
/// as a value, or null when either is.
static CObjectRelation composeRelations(const CObjectRelation &Of,
                                        const CObjectRelation &Returned) {
  using EKind = EObjectRelation;
  const auto &[OfKind, OfOutermost, OfThroughPointer, OfCopy, OfInUnion,
               OfEntryDepth, OfStoredDepth, OfMadeValue, OfCanBeNull,
               OfKeptAlive] = Of;
  const auto &[Kind, Outermost, ThroughPointer, Copy, InUnion, EntryDepth,
               StoredDepth, MadeValue, CanBeNull, KeptAlive] = Returned;
  if (OfKind == EKind::Unrelated || Kind == EKind::Unrelated)
    return Of;
  CObjectRelation Result = Of;
  if ((Kind == EKind::Entry || Kind == EKind::Unknown) &&
      (OfKind == EKind::SameObject || OfKind == EKind::Subobject))
    Result.Kind = EKind::Entry;
  else if (Kind == EKind::Subobject && OfKind == EKind::SameObject)
    Result.Kind = EKind::Subobject;
  if (!OfOutermost && OfKind == EKind::SameObject && Kind != EKind::Unknown)
    Result.Outermost = Outermost;
  Result.bThroughPointer = OfThroughPointer || ThroughPointer;
  Result.bCopy = OfCopy || Copy;
  Result.bInUnion = OfInUnion || InUnion;
  if (Kind == EKind::Entry) {
    Result.EntryDepth = OfEntryDepth + std::max(1u, EntryDepth);
    Result.StoredDepth = OfStoredDepth + std::min(1u, StoredDepth);
    Result.bMadeValue = false;
  } else if (Kind == EKind::Unknown) {
    // Of what is an entry then, one deeper; what can't be told stays so.
    if (Result.Kind == EKind::Entry) {
      Result.EntryDepth = OfEntryDepth + 1;
      Result.bMadeValue = false;
    }
  } else {
    Result.EntryDepth = OfEntryDepth + EntryDepth;
    Result.StoredDepth = OfStoredDepth + StoredDepth;
    Result.bMadeValue = OfMadeValue || MadeValue;
  }
  Result.bCanBeNull = OfCanBeNull || CanBeNull;
  // What is kept alive and its members are, not an entry of it.
  Result.bKeptAlive = (OfKeptAlive || KeptAlive) &&
                      Result.Kind != EKind::Entry &&
                      Result.Kind != EKind::Unknown;
  return Result;
}

/// What any of \p Of is: what all are; a member or an entry of the object,
/// which can't be told, when all are within it the same way, also through a
/// pointer it holds or as a copy; else unknown when any is related
/// (unknownOfRelations). In a union, or null, when any is.
static CObjectRelation mergeRelations(llvm::ArrayRef<CObjectRelation> Of) {
  using EKind = EObjectRelation;
  auto Related = [](const CObjectRelation &Each) {
    return Each.Kind != EKind::Unrelated;
  };
  if (Of.empty())
    return {};
  const bool InUnion = llvm::any_of(
      Of, [](const CObjectRelation &Each) { return Each.bInUnion; });
  auto SameMember = [](const CObjectRelation &Left,
                       const CObjectRelation &Right) {
    return Left.Outermost == Right.Outermost ||
           (Left.Outermost && Right.Outermost &&
            Left.Outermost->getMemberDecl() ==
                Right.Outermost->getMemberDecl());
  };
  auto SameWay = [](const CObjectRelation &Left, const CObjectRelation &Right) {
    return Left.bThroughPointer == Right.bThroughPointer &&
           Left.bCopy == Right.bCopy;
  };
  // Kind, Outermost, bThroughPointer and bCopy decide what it is below;
  // the depths, bMadeValue and bCanBeNull are of all that are related.
  unsigned Depth = 0;
  std::optional<unsigned> Stored;
  bool Made = false;
  bool CanBeNull = false;
  bool KeptAlive = true;
  for (const CObjectRelation &Each : Of) {
    [[maybe_unused]] const auto &[Kind, Outermost, ThroughPointer, Copy,
                                  EachInUnion, EntryDepth, StoredDepth,
                                  MadeValue, EachCanBeNull, EachKeptAlive] =
        Each;
    if (Kind == EKind::Unrelated)
      continue;
    Depth = std::max(Depth, EntryDepth);
    Stored = std::min(Stored.value_or(StoredDepth), StoredDepth);
    Made |= MadeValue;
    CanBeNull |= EachCanBeNull;
    KeptAlive &= EachKeptAlive;
  }
  if (llvm::all_of(Of, [&](const CObjectRelation &Each) {
        return Each.Kind == Of[0].Kind && SameWay(Each, Of[0]) &&
               SameMember(Each, Of[0]);
      })) {
    CObjectRelation Result = Of[0];
    Result.bInUnion = InUnion;
    Result.EntryDepth = Depth;
    Result.StoredDepth = Stored.value_or(0);
    Result.bMadeValue = Made;
    Result.bCanBeNull = CanBeNull;
    Result.bKeptAlive = KeptAlive;
    return Result;
  }
  if (llvm::none_of(Of, Related))
    return {};
  auto Within = [](const CObjectRelation &Each) {
    return Each.Kind == EKind::SameObject || Each.Kind == EKind::Subobject ||
           Each.Kind == EKind::Entry;
  };
  // Within it the same way, but of different members, a member that can't
  // be told, or of entries, an entry; one that can be a member or an entry
  // can't be told, `(bChoose ? mp_Items.m_Count : mp_Items[0].m_Value)`.
  const bool AnyEntry = llvm::any_of(Of, [](const CObjectRelation &Each) {
    return Each.Kind == EKind::Entry;
  });
  const bool AllEntries = llvm::all_of(Of, [](const CObjectRelation &Each) {
    return Each.Kind == EKind::Entry;
  });
  if (llvm::all_of(Of,
                   [&](const CObjectRelation &Each) {
                     return Within(Each) && SameWay(Each, Of[0]);
                   }) &&
      (!AnyEntry || AllEntries)) {
    CObjectRelation Result = Of[0];
    Result.bInUnion = InUnion;
    Result.Kind = AnyEntry ? EKind::Entry : EKind::Subobject;
    Result.Outermost = nullptr;
    Result.EntryDepth = Depth;
    Result.StoredDepth = Stored.value_or(0);
    Result.bMadeValue = Made;
    Result.bCanBeNull = CanBeNull;
    Result.bKeptAlive = KeptAlive && !AnyEntry;
    return Result;
  }
  return unknownOfRelations(Of);
}

static std::optional<bool> literalCondition(const Expr *Condition);

const Expr *chosenValue(const AbstractConditionalOperator *Conditional,
                        ASTContext *Context) {
  if (const auto *Written = dyn_cast<ConditionalOperator>(Conditional))
    if (const std::optional<bool> Chosen =
            literalCondition(Written->getCond()))
      return *Chosen ? Written->getTrueExpr() : Written->getFalseExpr();
  const llvm::SmallVector<const Expr *, 2> Arms =
      keptArms(Conditional, Context);
  return Arms.size() == 1 ? Arms.front() : nullptr;
}

CObjectRelation pointeeRelation(const Expr *E,
                                llvm::function_ref<bool(const Expr *)> IsObject,
                                ASTContext *Context) {
  // Of either value of a conditional each by itself, or the one a constant
  // condition chooses, and of the right of a comma.
  TCRecursive<CObjectRelation(const Expr *, int)> ValueOf =
      [&](const Expr *Pointer, int Depth) -> CObjectRelation {
    const Expr *Bare = Pointer->IgnoreParenImpCasts();
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Bare);
        Conditional && Pointer->isGLValue() && Depth < 16) {
      if (const Expr *Chosen = chosenValue(Conditional, Context))
        return ValueOf(Chosen, Depth + 1);
      return mergeRelations({ValueOf(Conditional->getTrueExpr(), Depth + 1),
                             ValueOf(Conditional->getFalseExpr(), Depth + 1)});
    }
    if (const auto *Comma = dyn_cast<BinaryOperator>(Bare);
        Comma && Comma->isCommaOp() && Pointer->isGLValue() && Depth < 16)
      return ValueOf(Comma->getRHS(), Depth + 1);
    CObjectRelation Of = objectRelation(Pointer, IsObject, Context);
    const std::optional<bool> Stored = storedPointerOf(Pointer);
    if (!Stored)
      return {EObjectRelation::Unknown, nullptr};
    if (*Stored && (Of.Kind == EObjectRelation::Subobject ||
                    Of.Kind == EObjectRelation::Entry))
      Of.bThroughPointer = true;
    // What a borrowed member of a copy points to is what the source's does,
    // not the copy's own, `Copy.m_pValue` of `CMixedRef Copy{1, &m_Value}`
    // (isBorrowedField).
    if (const auto *Member = dyn_cast<MemberExpr>(Bare); *Stored && Member)
      if (const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
          Field && isBorrowedField(Field)) {
        Of.bCopy = false;
        Of.bKeptAlive = false;
        Of.bInUnion = false;
      }
    return Of;
  };
  return ValueOf(E, 0);
}

namespace {
/// What is told of each type, once, which the checks ask of the same types
/// again and again: by the canonical type.
struct CTypeFacts {
  llvm::DenseMap<const void *, bool> Collection;
  llvm::DenseMap<const void *, QualType> OwnedPointee;
  llvm::DenseMap<const void *, bool> ReferBack;
  llvm::DenseMap<const void *, bool> InlineValue;
  llvm::DenseMap<const void *, bool> BorrowedParts;
  llvm::DenseMap<const void *, bool> OwnLevel;
  llvm::DenseMap<const void *, bool> OnlyRefers;
  llvm::DenseMap<const void *, bool> CopyShares;
};

/// The fact of \p Type in \p Facts of the AST of \p Context, told by \p
/// Tell the first time.
template <typename t_CValue, typename t_FTell>
t_CValue typeFact(llvm::DenseMap<const void *, t_CValue> CTypeFacts::*Facts,
                  QualType Type, const ASTContext &Context, t_FTell &&Tell) {
  const void *Key = Type.getCanonicalType().getAsOpaquePtr();
  auto &Known = astCache<CTypeFacts>(Context).*Facts;
  if (auto It = Known.find(Key); It != Known.end())
    return It->second;
  t_CValue Value = Tell();
  (astCache<CTypeFacts>(Context).*Facts)[Key] = Value;
  return Value;
}
} // namespace

/// Whether a value of \p Type can refer to what it was made from: a view of
/// ViewTypes, an iterator, or a class with a pointer, reference, view or
/// iterator member, also of a base, or of a member it holds by value, or one
/// that isn't defined. Not one that is all its own, a copy.
static bool canReferBack(QualType Type, const ASTContext &Context,
                         llvm::SmallPtrSetImpl<const CXXRecordDecl *> &Seen) {
  if (stackNearlyExhausted())
    return withStack([&] { return canReferBack(Type, Context, Seen); });
  // A reference refers back, also a reference member, which stripping the
  // reference would hide.
  if (Type->isReferenceType())
    return true;
  while (const ArrayType *Array = Type->getAsArrayTypeUnsafe())
    Type = Array->getElementType();
  if (Type->isPointerType() || Type->isReferenceType() ||
      Type->isMemberPointerType() || isIteratorType(Type) ||
      isDefaultViewType(Type, Context))
    return true;
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record)
    return false;
  if (!Record->hasDefinition())
    return true;
  Record = Record->getDefinition();
  if (!Seen.insert(Record).second)
    return false;
  // Also one that is like a pointer, with `operator->` or `operator*`, which
  // can keep where it points in any way, like the bits of a
  // `TCBitStorePointer`.
  if (hasOverloadedOperator(Record, {OO_Arrow, OO_Star}) != false)
    return true;
  for (const FieldDecl *Field : Record->fields())
    if (canReferBack(Field->getType(), Context, Seen))
      return true;
  for (const CXXBaseSpecifier &Base : Record->bases())
    if (canReferBack(Base.getType(), Context, Seen))
      return true;
  return false;
}

/// Whether \p E names \p Local, also through local references to it,
/// `auto &rpItems = pItems`.
static bool namesLocalThroughAliases(const Expr *E, const VarDecl *Local) {
  for (int Depth = 0; E && Depth < 8; ++Depth) {
    const auto *Named = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
    if (!Named)
      return false;
    if (Named->getDecl() == Local)
      return true;
    const auto *Alias = dyn_cast<VarDecl>(Named->getDecl());
    if (!Alias || !Alias->hasLocalStorage() || isa<ParmVarDecl>(Alias) ||
        !Alias->getType()->isReferenceType())
      return false;
    E = Alias->getInit();
  }
  return false;
}

/// Whether \p Type, also of what a pointer or reference to it is, is a
/// collection: a class, not an iterator, with a member function of
/// RemovingMethods, also of a base.
static bool isCollectionTypeOf(QualType Type) {
  Type = Type.getNonReferenceType();
  if (Type->isPointerType())
    Type = Type->getPointeeType();
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record || !Record->hasDefinition() || isIteratorType(Type))
    return false;
  llvm::SmallVector<const CXXRecordDecl *, 4> Work{Record->getDefinition()};
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Visited;
  while (!Work.empty()) {
    const CXXRecordDecl *Current = Work.pop_back_val();
    if (!Current || !Current->hasDefinition() ||
        !Visited.insert(Current->getDefinition()).second)
      continue;
    Current = Current->getDefinition();
    for (const Decl *Member : Current->decls()) {
      const auto *Method = dyn_cast<CXXMethodDecl>(Member);
      if (const auto *Template = dyn_cast<FunctionTemplateDecl>(Member))
        Method = dyn_cast<CXXMethodDecl>(Template->getTemplatedDecl());
      if (Method && Method->getIdentifier() &&
          matchesNamePattern(identifierTextOf(Method),
                             vocabulary().RemovingMethods.names()))
        return true;
    }
    for (const CXXBaseSpecifier &Base : Current->bases())
      Work.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  return false;
}

bool isCollectionType(QualType Type) {
  const CXXRecordDecl *Record =
      Type.getNonReferenceType()->getAsCXXRecordDecl();
  if (!Record && Type.getNonReferenceType()->isPointerType())
    Record = Type.getNonReferenceType()->getPointeeType()->getAsCXXRecordDecl();
  if (!Record)
    return false;
  return typeFact(&CTypeFacts::Collection, Type, Record->getASTContext(),
                  [&] { return isCollectionTypeOf(Type); });
}

/// What an owning pointer, \p Type, points to: a class like one, not an
/// iterator, with `operator->` giving a pointer or `operator*` giving a
/// reference, also inherited, also an instantiation of a member template,
/// like a `TCUniquePointer<CItems>`. Null for another type.
static QualType ownedPointeeOf(QualType Type) {
  Type = Type.getNonReferenceType();
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record || isIteratorType(Type))
    return QualType();
  llvm::SmallVector<const CXXRecordDecl *, 4> Work{Record};
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
  while (!Work.empty()) {
    const CXXRecordDecl *Current = Work.pop_back_val();
    if (!Current || !Current->hasDefinition())
      continue;
    Current = Current->getDefinition();
    if (!Seen.insert(Current).second)
      continue;
    // Also the instantiations of a member template, `template <typename
    // t_CSelf> auto operator->(this t_CSelf &&)`, as instantiated.
    llvm::SmallVector<const CXXMethodDecl *, 8> Methods(Current->methods());
    for (const Decl *Member : Current->decls())
      if (const auto *Template = dyn_cast<FunctionTemplateDecl>(Member))
        for (const FunctionDecl *Instance : Template->specializations())
          if (const auto *Method = dyn_cast<CXXMethodDecl>(Instance))
            Methods.push_back(Method);
    for (const CXXMethodDecl *Method : Methods) {
      const QualType Returned = Method->getReturnType();
      if (Method->getOverloadedOperator() == OO_Arrow &&
          Returned->isPointerType())
        return Returned->getPointeeType();
      if (Method->getOverloadedOperator() == OO_Star &&
          Method->getNumParams() ==
              (Method->isExplicitObjectMemberFunction() ? 1u : 0u) &&
          Returned->isReferenceType())
        return Returned.getNonReferenceType();
    }
    for (const CXXBaseSpecifier &Base : Current->bases())
      Work.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  return QualType();
}

QualType ownedPointee(QualType Type) {
  const CXXRecordDecl *Record =
      Type.getNonReferenceType()->getAsCXXRecordDecl();
  if (!Record)
    return QualType();
  return typeFact(&CTypeFacts::OwnedPointee, Type, Record->getASTContext(),
                  [&] { return ownedPointeeOf(Type); });
}

/// Whether a copy of \p Type can point where what it is copied from
/// points (copySharesPointees), each record once.
static bool copySharesPointeesOf(QualType Type, const ASTContext &Context,
                                 llvm::SmallPtrSetImpl<const CXXRecordDecl *> &Seen) {
  if (stackNearlyExhausted())
    return withStack([&] { return copySharesPointeesOf(Type, Context, Seen); });
  if (Type->isReferenceType())
    return true;
  while (const ArrayType *Array = Type->getAsArrayTypeUnsafe())
    Type = Array->getElementType();
  if (Type->isPointerType() || Type->isMemberPointerType() ||
      isIteratorType(Type) || isDefaultViewType(Type, Context))
    return true;
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record)
    return false;
  if (!Record->hasDefinition())
    return true;
  Record = Record->getDefinition();
  if (!Seen.insert(Record).second)
    return false;
  // One whose copy is its own owns what it copies.
  if (llvm::any_of(Record->ctors(), [](const CXXConstructorDecl *Constructor) {
        return Constructor->isCopyOrMoveConstructor() &&
               Constructor->isUserProvided();
      }))
    return false;
  for (const FieldDecl *Field : Record->fields())
    if (copySharesPointeesOf(Field->getType(), Context, Seen))
      return true;
  for (const CXXBaseSpecifier &Base : Record->bases())
    if (copySharesPointeesOf(Base.getType(), Context, Seen))
      return true;
  return false;
}

bool copySharesPointees(QualType Type, const ASTContext &Context) {
  return typeFact(&CTypeFacts::CopyShares, Type, Context, [&] {
    llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
    return copySharesPointeesOf(Type.getNonReferenceType(), Context, Seen);
  });
}

bool canReferBack(QualType Type, const ASTContext &Context) {
  if (stackNearlyExhausted())
    return withStack([&] { return canReferBack(Type, Context); });
  return typeFact(&CTypeFacts::ReferBack, Type, Context, [&] {
    llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
    return canReferBack(Type, Context, Seen);
  });
}

/// Whether \p Type is like a pointer (ownedPointee) that holds what it gives
/// in it, like an optional: as a member of that type, in a union, or as
/// bytes, also of a member or a base it holds by value; not one that only
/// points to it.
static bool holdsValueInlineOf(QualType Type);
static bool holdsValueInline(QualType Type) {
  const CXXRecordDecl *Record =
      Type.getNonReferenceType()->getAsCXXRecordDecl();
  if (!Record)
    return holdsValueInlineOf(Type);
  return typeFact(&CTypeFacts::InlineValue, Type, Record->getASTContext(),
                  [&] { return holdsValueInlineOf(Type); });
}
static bool holdsValueInlineOf(QualType Type) {
  const QualType Pointee = ownedPointee(Type);
  if (Pointee.isNull())
    return false;
  const QualType Value = Pointee.getCanonicalType().getUnqualifiedType();
  llvm::SmallVector<QualType, 8> Work{Type.getNonReferenceType()};
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
  while (!Work.empty()) {
    QualType Current = Work.pop_back_val();
    while (const ArrayType *Array = Current->getAsArrayTypeUnsafe()) {
      const QualType Element = Array->getElementType();
      if (Element->isCharType() || Element->isStdByteType() ||
          Element->isSpecificBuiltinType(BuiltinType::UChar))
        return true;
      Current = Element;
    }
    if (Current.getCanonicalType().getUnqualifiedType() == Value)
      return true;
    const CXXRecordDecl *Record = Current->getAsCXXRecordDecl();
    if (!Record || !Record->hasDefinition())
      continue;
    Record = Record->getDefinition();
    if (Record->isUnion())
      return true;
    if (!Seen.insert(Record).second)
      continue;
    for (const FieldDecl *Field : Record->fields())
      if (!Field->getType()->isReferenceType() &&
          !Field->getType()->isPointerType())
        Work.push_back(Field->getType());
    for (const CXXBaseSpecifier &Base : Record->bases())
      Work.push_back(Base.getType());
  }
  return false;
}

bool isOwnedStoragePointer(const FieldDecl *Field) {
  if (!Field || !Field->getType()->isPointerType())
    return false;
  const QualType Pointee = Field->getType()
                               ->getPointeeType()
                               .getCanonicalType()
                               .getUnqualifiedType();
  // Of a class that copies itself by its own copy constructor, to a class of
  // its own, declared in it or named by a type it declares, its storage, like
  // the shared buffer of a string, `CData *m_pData` of `TCStrImp_Dynamic` with
  // `using CData = ...`, which a copy of it shares or copies as its own value.
  if (const auto *Owner = dyn_cast<CXXRecordDecl>(Field->getParent());
      Owner && Owner->hasDefinition() &&
      llvm::any_of(Owner->getDefinition()->ctors(),
                   [](const CXXConstructorDecl *Constructor) {
                     return Constructor->isCopyConstructor() &&
                            Constructor->isUserProvided();
                   })) {
    auto OwnedBy = [&](const DeclContext *In) {
      const auto *Record = dyn_cast_or_null<CXXRecordDecl>(In);
      return Record && Record->getCanonicalDecl() == Owner->getCanonicalDecl();
    };
    // A class, not what an alias of a character or a number is, `CChar
    // *m_pData` of `TCStrImp_Ptr`, which points to what it is given.
    const QualType Declared = Field->getType()->getPointeeType();
    if (const auto *Alias = Declared->getAs<TypedefType>();
        Alias && Pointee->isRecordType() &&
        OwnedBy(Alias->getDecl()->getDeclContext()))
      return true;
    if (const CXXRecordDecl *Held = Pointee->getAsCXXRecordDecl();
        Held && OwnedBy(Held->getDeclContext()))
      return true;
  }
  if (isCollectionType(Pointee))
    return false;

  // Of the collection, or of a class in it that holds its storage,
  // `CStaticData` of `TCVector` with `CVectorData *m_pData`. Not of what is
  // like a pointer, `TCUniquePointer` (ownedPointee), whose pointee is what
  // it owns, nor a view.
  for (const DeclContext *Enclosing = Field->getParent(); Enclosing;
       Enclosing = Enclosing->getParent()) {
    const auto *Record = dyn_cast<CXXRecordDecl>(Enclosing);
    if (!Record)
      return false;
    const auto *Collection = dyn_cast<ClassTemplateSpecializationDecl>(Record);
    if (!Collection)
      continue;
    const QualType Owner = Record->getASTContext().getCanonicalTagType(Record);
    if (!isCollectionType(Owner) ||
        isDefaultViewType(Owner, Record->getASTContext()) ||
        !ownedPointee(Owner).isNull())
      continue;
    // Storage of what it holds: of an element, a type argument of it,
    // `t_CValue *m_pData`, of a class in it, `CVectorData *`, of a node of
    // the same arguments, `TCNode<t_CKey, t_CValue> *`, or bytes. Not a
    // pointer to anything else, which it borrows, `int *m_pCounter` of a
    // `TCWrapper<CItem>`.
    auto IsArgument = [&](QualType Type) {
      Type = Type.getCanonicalType().getUnqualifiedType();
      return llvm::any_of(Collection->getTemplateArgs().asArray(),
                          [&](const TemplateArgument &Argument) {
                            return Argument.getKind() ==
                                       TemplateArgument::Type &&
                                   Argument.getAsType()
                                           .getCanonicalType()
                                           .getUnqualifiedType() == Type;
                          });
    };
    if (Pointee->isVoidType() || Pointee->isCharType() ||
        Pointee->isStdByteType() || IsArgument(Pointee))
      return true;
    if (const CXXRecordDecl *Held = Pointee->getAsCXXRecordDecl()) {
      for (const DeclContext *In = Held->getDeclContext(); In;
           In = In->getParent())
        if (const auto *Outer = dyn_cast<CXXRecordDecl>(In);
            Outer && Outer->getCanonicalDecl() == Record->getCanonicalDecl())
          return true;
      if (const auto *Node = dyn_cast<ClassTemplateSpecializationDecl>(Held))
        for (const TemplateArgument &Argument :
             Node->getTemplateArgs().asArray())
          if (Argument.getKind() == TemplateArgument::Type &&
              IsArgument(Argument.getAsType()))
            return true;
    }
    return false;
  }
  return false;
}

/// Whether \p Type is an owner that can't be copied, `TCUniquePointer`, which
/// owns what it points to alone (ownedPointee).
static bool isUniqueOwner(QualType Type) {
  Type = Type.getNonReferenceType();
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record || !Record->hasDefinition() || ownedPointee(Type).isNull() ||
      holdsValueInline(Type))
    return false;
  return llvm::any_of(Record->getDefinition()->ctors(),
                      [](const CXXConstructorDecl *Constructor) {
                        return Constructor->isCopyConstructor() &&
                               Constructor->isDeleted();
                      });
}

bool isBorrowedField(const FieldDecl *Field) {
  if (!Field)
    return false;
  const QualType Type = Field->getType();
  if (Type->isReferenceType() || Type->isMemberPointerType())
    return true;
  if (Type->isPointerType())
    return !isOwnedStoragePointer(Field);
  return false;
}

bool targetToldByOwnClass(const FieldDecl *Field) {
  if (!isBorrowedField(Field))
    return true;
  const ASTContext &Context = Field->getASTContext();
  const QualType Class = Context.getCanonicalTagType(Field->getParent());
  return ownerKindOf(Class, Context) != EOwnerKind::None ||
         isDefaultViewType(Class, Context) || setByOwnClass(Field);
}

/// Whether \p Record holds bytes that something is made in, an array of
/// characters or `std::byte`, also in a member or a base it holds by value.
static bool storesBytes(const CXXRecordDecl *Record) {
  llvm::SmallVector<const CXXRecordDecl *, 8> Work{Record};
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
  while (!Work.empty()) {
    const CXXRecordDecl *Current = Work.pop_back_val();
    if (!Current || !Current->hasDefinition() ||
        !Seen.insert(Current->getDefinition()).second)
      continue;
    Current = Current->getDefinition();
    for (const FieldDecl *Field : Current->fields()) {
      QualType Type = Field->getType();
      if (Type->isReferenceType() || Type->isPointerType())
        continue;
      if (const ArrayType *Array = Type->getAsArrayTypeUnsafe()) {
        const QualType Element = Array->getElementType();
        if (Element->isCharType() || Element->isStdByteType() ||
            Element->isSpecificBuiltinType(BuiltinType::UChar))
          return true;
        Type = Element;
      }
      Work.push_back(Type->getAsCXXRecordDecl());
    }
    for (const CXXBaseSpecifier &Base : Current->bases())
      Work.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  return false;
}

/// hasBorrowedParts, with the classes \p Seen on the way, which a class that
/// holds itself, through a unique owner or its storage, is taken not to.
static bool
hasBorrowedParts(QualType Type, const ASTContext &Context,
                 llvm::SmallPtrSetImpl<const CXXRecordDecl *> &Seen) {
  if (Type.isNull())
    return true;
  if (stackNearlyExhausted())
    return withStack([&] { return hasBorrowedParts(Type, Context, Seen); });
  Type = Type.getCanonicalType();
  while (const ArrayType *Array = Type->getAsArrayTypeUnsafe())
    Type = Array->getElementType().getCanonicalType();
  if (Type->isReferenceType() || Type->isPointerType() ||
      Type->isMemberPointerType() || Type->isBlockPointerType())
    return true;
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record)
    return !(Type->isScalarType() || Type->isVoidType());
  // One that can't be looked into, a closure, an iterator, a view, or a
  // shared owner, whose copies share what it points to.
  if (!Record->hasDefinition() || Record->isLambda() || isIteratorType(Type) ||
      isDefaultViewType(Type, Context) || isSharedPointerType(Type, Context))
    return true;
  Record = Record->getDefinition();
  if (!Seen.insert(Record).second)
    return false;
  // A unique owner, one that can't be copied, owns what it points to, which
  // can have borrowed parts itself; another one that holds its value in it,
  // like an optional, by its members.
  if (const QualType Pointee = ownedPointee(Type);
      !Pointee.isNull() && !holdsValueInline(Type))
    return !isUniqueOwner(Type) || hasBorrowedParts(Pointee, Context, Seen);
  // What holds a value as bytes, an optional or a variant, `TCOptional<CView>`
  // or `TCVariant<CView, int>`, by the types its template is given, also
  // besides its members; what can't be told has borrowed parts.
  if (storesBytes(Record)) {
    const auto *Specialization =
        dyn_cast<ClassTemplateSpecializationDecl>(Record);
    if (!Specialization)
      return true;
    for (const TemplateArgument &Argument :
         Specialization->getTemplateArgs().asArray()) {
      if (Argument.getKind() == TemplateArgument::Type &&
          hasBorrowedParts(Argument.getAsType(), Context, Seen))
        return true;
      if (Argument.getKind() == TemplateArgument::Pack)
        for (const TemplateArgument &Packed : Argument.pack_elements())
          if (Packed.getKind() == TemplateArgument::Type &&
              hasBorrowedParts(Packed.getAsType(), Context, Seen))
            return true;
    }
  }
  // The elements of a collection, the type arguments of its template, and
  // the references its members hold, not the storage it owns, the pointers in
  // what it holds by value, to its nodes and the links between them, `TCMap`
  // of an intrusive tree, which a copy makes its own.
  if (const auto *Specialization =
          dyn_cast<ClassTemplateSpecializationDecl>(Record);
      Specialization && isCollectionType(Type)) {
    llvm::SmallPtrSet<const CXXRecordDecl *, 8> Storage;
    TCRecursive<bool(QualType)> StorageBorrows = [&](QualType Part) {
      Part = Part.getCanonicalType();
      while (const ArrayType *Array = Part->getAsArrayTypeUnsafe())
        Part = Array->getElementType().getCanonicalType();
      if (Part->isReferenceType() || Part->isMemberPointerType())
        return true;
      if (Part->isPointerType())
        return false;
      const CXXRecordDecl *Held = Part->getAsCXXRecordDecl();
      if (!Held)
        return false;
      if (!Held->hasDefinition() || Held->isLambda() || isIteratorType(Part) ||
          isDefaultViewType(Part, Context) ||
          isSharedPointerType(Part, Context))
        return true;
      Held = Held->getDefinition();
      if (!Storage.insert(Held).second)
        return false;
      return llvm::any_of(Held->fields(),
                          [&](const FieldDecl *Field) {
                            return StorageBorrows(Field->getType());
                          }) ||
             llvm::any_of(Held->bases(), [&](const CXXBaseSpecifier &Base) {
               return StorageBorrows(Base.getType());
             });
    };
    return llvm::any_of(
               Specialization->getTemplateArgs().asArray(),
               [&](const TemplateArgument &Argument) {
                 if (Argument.getKind() == TemplateArgument::Pack)
                   return llvm::any_of(
                       Argument.pack_elements(),
                       [&](const TemplateArgument &Packed) {
                         return Packed.getKind() == TemplateArgument::Type &&
                                hasBorrowedParts(Packed.getAsType(), Context,
                                                 Seen);
                       });
                 return Argument.getKind() == TemplateArgument::Type &&
                        hasBorrowedParts(Argument.getAsType(), Context, Seen);
               }) ||
           StorageBorrows(Type);
  }
  return llvm::any_of(Record->fields(),
                      [&](const FieldDecl *Field) {
                        if (isOwnedStoragePointer(Field))
                          return false;
                        return isBorrowedField(Field) ||
                               hasBorrowedParts(Field->getType(), Context,
                                                Seen);
                      }) ||
         llvm::any_of(Record->bases(), [&](const CXXBaseSpecifier &Base) {
           return hasBorrowedParts(Base.getType(), Context, Seen);
         });
}

bool hasBorrowedParts(QualType Type, const ASTContext &Context) {
  if (stackNearlyExhausted())
    return withStack([&] { return hasBorrowedParts(Type, Context); });
  if (Type.isNull())
    return true;
  return typeFact(
      &CTypeFacts::BorrowedParts, Type.getNonReferenceType(), Context, [&] {
        llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
        return hasBorrowedParts(Type.getNonReferenceType(), Context, Seen);
      });
}

bool isIndependentType(QualType Type, const ASTContext &Context) {
  return !Type.isNull() && !Type->isReferenceType() &&
         !hasBorrowedParts(Type, Context);
}

/// Whether a value of \p Type only refers, every member of it, at any depth
/// by value, borrowed (isBorrowedField), like a view of pointers or a pointer
/// wrapper, `struct CView { CItems *m_pItems; }`: none of it is its own.
static bool onlyRefers(QualType Type, const ASTContext &Context);
static bool onlyRefersOf(QualType Type, const ASTContext &Context) {
  if (stackNearlyExhausted())
    return withStack([&] { return onlyRefersOf(Type, Context); });
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record || !Record->hasDefinition())
    return false;
  Record = Record->getDefinition();
  if (isSharedPointerType(Type, Context) || holdsValueInline(Type))
    return false;
  bool Any = false;
  for (const FieldDecl *Field : Record->fields()) {
    Any = true;
    if (isBorrowedField(Field))
      continue;
    if (!Field->getType()->isRecordType() ||
        !onlyRefers(Field->getType(), Context))
      return false;
  }
  for (const CXXBaseSpecifier &Base : Record->bases()) {
    Any = true;
    if (!onlyRefers(Base.getType(), Context))
      return false;
  }
  return Any;
}

// Of each type once (typeFact), a base shared by several paths of a
// hierarchy too.
static bool onlyRefers(QualType Type, const ASTContext &Context) {
  return typeFact(&CTypeFacts::OnlyRefers, Type, Context,
                  [&] { return onlyRefersOf(Type, Context); });
}

static bool isCopyAtOwnLevelOf(QualType Type, const ASTContext &Context) {
  Type = Type.getNonReferenceType();
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  // Not what is like a pointer that doesn't hold what it gives, `operator->`
  // of a tagged pointer, which refers, unless it shares it
  // (OwnerTypes).
  if (Record && !ownedPointee(Type).isNull() && !holdsValueInline(Type) &&
      !isSharedPointerType(Type, Context))
    return false;
  return Record && !Record->isLambda() && !isIteratorType(Type) &&
         !isDefaultViewType(Type, Context) && !onlyRefers(Type, Context);
}

bool isCopyAtOwnLevel(QualType Type, const ASTContext &Context) {
  if (Type.isNull())
    return false;
  return typeFact(&CTypeFacts::OwnLevel, Type, Context,
                  [&] { return isCopyAtOwnLevelOf(Type, Context); });
}

/// Whether what \p Method returns can be what a borrowed member of its
/// object leads to, or what isn't of the object, by where each value its
/// returns that run give is (objectStorageOf): not only what the object
/// holds itself, its owned storage, `return m_pData[_iIndex];`, also through
/// a member function of what holds it that gives what is its own,
/// `(mp_StaticData.m_pData->f_GetData())[_Index]` of `return (t_CData *)(this
/// + 1);`. One whose body can't be seen, or a coroutine, can.
static bool returnsBorrowed(const CXXMethodDecl *Method) {
  const auto *Definition =
      Method ? dyn_cast_or_null<CXXMethodDecl>(Method->getDefinition())
             : nullptr;
  if (!Definition || !Definition->getBody() ||
      isa<CoroutineBodyStmt>(Definition->getBody()))
    return true;
  ASTContext &Context = Definition->getASTContext();
  return llvm::any_of(
      writtenReturns(Definition, Context), [&](const Expr *Value) {
        const CObjectStorage Of = objectStorageOf(Value, Definition, Context);
        return !Of.bOwn || Of.bBorrowed || Of.bUnknown || Of.bElsewhere;
      });
}

bool isCopyStorage(const CObjectRelation &Relation) {
  return Relation.Kind != EObjectRelation::Unrelated && Relation.bCopy;
}

bool isFixedArrayType(QualType Type, const ASTContext &Context) {
  Type = Type.getNonReferenceType();
  if (Type->isPointerType())
    Type = Type->getPointeeType();
  return Type->isConstantArrayType() ||
         (Type->getAsCXXRecordDecl() &&
          matchesTypeName(Type, vocabulary().FixedArrayTypes.names(), Context));
}

bool givesItsObject(const CXXMethodDecl *Method) {
  // A reference to its own class or a base of it, or a pointer to it whose
  // body can be seen to return `this`.
  const QualType Result = Method->getReturnType();
  const bool Pointer = Result->isPointerType();
  if (!(Result->isReferenceType() || Pointer) || Method->isStatic())
    return false;
  const CXXRecordDecl *Returned =
      Pointer ? Result->getPointeeCXXRecordDecl()
              : Result.getNonReferenceType()->getAsCXXRecordDecl();
  const CXXRecordDecl *Own = Method->getParent();
  if (!Returned || !Own || !Own->hasDefinition() ||
      !(Returned->getCanonicalDecl() == Own->getCanonicalDecl() ||
        Own->isDerivedFrom(Returned)))
    return false;
  const FunctionDecl *Definition = Method->getDefinition();
  if (!Definition || !Definition->getBody())
    return !Pointer;
  // Each return gives the object, `*this`, `this`, the explicit object
  // parameter or its address, also through a local alias, `auto &Self =
  // *this; return Self;`.
  const std::optional<CObjectRelation> Gives =
      returnedRelation(Method, &Method->getASTContext());
  return Gives && Gives->Kind == EObjectRelation::SameObject &&
         !Gives->bThroughPointer && !isCopyStorage(*Gives);
}

static std::optional<CObjectRelation>
returnedRelationUncached(const std::pair<const CXXMethodDecl *, int> &Key);

std::optional<CObjectRelation> returnedRelation(const CXXMethodDecl *Method,
                                                ASTContext *Context) {
  const QualType Result = Method->getReturnType();
  if (!(Result->isReferenceType() || Result->isPointerType()) ||
      Method->isStatic())
    return std::nullopt;
  const auto *Definition =
      dyn_cast_or_null<CXXMethodDecl>(Method->getDefinition());
  if (!Definition || !Definition->getBody() ||
      isa<CoroutineBodyStmt>(Definition->getBody()))
    return std::nullopt;
  // Each once for each AST; one that returns what returns it again tells
  // nothing while it is worked out.
  struct CMemo {
    TCSummaries<std::pair<const CXXMethodDecl *, int>,
                std::optional<CObjectRelation>>
        Returns{&returnedRelationUncached, std::optional<CObjectRelation>(),
                std::optional<CObjectRelation>()};
  };
  return astCache<CMemo>(Definition->getASTContext())
      .Returns.get({Definition, Context ? 1 : 0});
}

// What each return of \p Key's method gives of its object (returnedRelation),
// with its AST as the context of what it asks when the second is 1.
static std::optional<CObjectRelation>
returnedRelationUncached(const std::pair<const CXXMethodDecl *, int> &Key) {
  const CXXMethodDecl *Method = Key.first;
  const FunctionDecl *Definition = Method;
  ASTContext *Context = Key.second ? &Method->getASTContext() : nullptr;
  const ParmVarDecl *Self = Method->isExplicitObjectMemberFunction() &&
                                    Definition->getNumParams() >= 1
                                ? Definition->getParamDecl(0)
                                : nullptr;
  auto IsObject = [&](const Expr *E) {
    if (isa<CXXThisExpr>(E))
      return !Self;
    const auto *Ref = dyn_cast<DeclRefExpr>(E);
    return Self && Ref && Ref->getDecl() == Self;
  };
  std::optional<CObjectRelation> Merged;
  if (const llvm::SmallVector<const Expr *, 4> Values =
          returnedValues(Definition);
      !Values.empty()) {
    llvm::SmallVector<CObjectRelation, 4> Of;
    for (const Expr *Value : Values)
      Of.push_back(objectRelation(Value, IsObject, Context));
    Merged = mergeRelations(Of);
  }
  return Merged;
}

bool isHeldByValue(const CObjectRelation &Relation) {
  return (Relation.Kind == EObjectRelation::SameObject ||
          Relation.Kind == EObjectRelation::Subobject) &&
         !Relation.bThroughPointer && !isCopyStorage(Relation);
}

/// Whether \p Pointer, a pointer to a data member used on \p Object, can be
/// to a member of a union: one it can be seen to point to, `&CEntry::m_Empty`,
/// also through a local made with it that isn't changed, or, when that can't
/// be seen, any member of a union the class of \p Object has.
static bool pointsIntoUnion(const Expr *Pointer, const Expr *Object) {
  auto InUnion = [](const ValueDecl *Member) {
    if (const auto *Field = dyn_cast<FieldDecl>(Member))
      return Field->getParent()->isUnion();
    if (const auto *Indirect = dyn_cast<IndirectFieldDecl>(Member))
      return llvm::any_of(Indirect->chain(), [](const NamedDecl *Part) {
        const auto *Field = dyn_cast<FieldDecl>(Part);
        return Field && Field->getParent()->isUnion();
      });
    return false;
  };
  for (int Depth = 0; Pointer && Depth < 8; ++Depth) {
    Pointer = Pointer->IgnoreParenImpCasts();
    if (const auto *Address = dyn_cast<UnaryOperator>(Pointer);
        Address && Address->getOpcode() == UO_AddrOf)
      Pointer = Address->getSubExpr()->IgnoreParenImpCasts();
    const auto *Ref = dyn_cast<DeclRefExpr>(Pointer);
    if (!Ref)
      break;
    if (isa<FieldDecl, IndirectFieldDecl>(Ref->getDecl()))
      return InUnion(Ref->getDecl());
    const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
    if (!Variable || !Variable->getInit() || isa<ParmVarDecl>(Variable) ||
        !(Variable->getType().isConstQualified() ||
          (Variable->hasLocalStorage() && isUnchangedAt(Ref))))
      break;
    Pointer = Variable->getInit();
  }
  QualType Type = Object->getType().getNonReferenceType();
  if (Type->isPointerType())
    Type = Type->getPointeeType();
  // Of a union itself, or of a class with one, also in a base.
  llvm::SmallVector<const CXXRecordDecl *, 4> Work{Type->getAsCXXRecordDecl()};
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
  while (!Work.empty()) {
    const CXXRecordDecl *Record = Work.pop_back_val();
    if (!Record || !Record->hasDefinition())
      return true;
    Record = Record->getDefinition();
    if (!Seen.insert(Record).second)
      continue;
    if (Record->isUnion())
      return true;
    for (const FieldDecl *Field : Record->fields())
      if (const CXXRecordDecl *Inner = Field->getType()->getAsCXXRecordDecl();
          Inner && Inner->isUnion())
        return true;
    for (const CXXBaseSpecifier &Base : Record->bases())
      Work.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  return false;
}

// What the setters \p Local is given to in its function set it to,
// `fg_Set(pItems, _Items)` of `o_pItems = &_Items` (setParameters): what each
// is given for that (CLocalWrites::Set).
static llvm::SmallVector<const Expr *, 4> setValuesOf(const VarDecl *Local) {
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return {};
  // Only where it runs (evaluatedChildren), not `if (false)
  // fg_Set(pItems, mp_Items)`.
  const ASTContext &FunctionContext = Function->getASTContext();
  llvm::SmallVector<const Expr *, 4> Given;
  llvm::SmallVector<const Stmt *, 32> Pending{Function->getBody()};
  llvm::SmallPtrSet<const Stmt *, 32> Seen;
  while (!Pending.empty()) {
    const Stmt *S = Pending.pop_back_val();
    if (!S || !Seen.insert(S).second || hasUnevaluatedOperands(S))
      continue;
    unsigned Offset = 0;
    if (const auto *Call = dyn_cast<CallExpr>(S))
      if (const FunctionDecl *Setter = setterCalled(Call, Offset))
        for (const CSetParameter &Parameter : setParameters(Setter))
          if (const Expr *Target = setTargetArgument(Call, Parameter, Offset);
              Target && Parameter.Source + Offset < Call->getNumArgs())
            if (namesLocalThroughAliases(Target, Local))
              Given.push_back(Call->getArg(Parameter.Source + Offset));
    llvm::append_range(Pending, evaluatedChildren(S, FunctionContext));
  }
  return Given;
}

// What \p Local, a view or what holds one by value, or a part of it held
// by value, is assigned in its function (CLocalWrites::Parts).
static llvm::SmallVector<const Expr *, 4> partValuesOf(const VarDecl *Local) {
  llvm::SmallVector<const Expr *, 4> Values;
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return Values;
  // Whether \p Target is the local or a part of it held by value,
  // also through a local reference to it, `Alias.m_pValue` of
  // `auto &Alias = View`, either of a choice of them, or an
  // unchanged local pointer to it, `pView->m_pValue`.
  TCRecursive<bool(const Expr *, int)> IsPart = [&](const Expr *Target,
                                                      int Depth) -> bool {
    for (int Steps = 0; Target && Steps < 16 && Depth < 8; ++Steps) {
      Target = Target->IgnoreParenImpCasts();
      if (const auto *Named = dyn_cast<DeclRefExpr>(Target)) {
        if (Named->getDecl() == Local)
          return true;
        const auto *Alias = dyn_cast<VarDecl>(Named->getDecl());
        if (!Alias || !Alias->hasLocalStorage() || isa<ParmVarDecl>(Alias) ||
            !Alias->getInit())
          return false;
        if (Alias->getType()->isReferenceType())
          return IsPart(Alias->getInit(), Depth + 1);
        // What an unchanged pointer to it points to.
        const auto *Address =
            Alias->getType()->isPointerType() && !isChangedVariable(Alias)
                ? dyn_cast<UnaryOperator>(
                      Alias->getInit()->IgnoreParenImpCasts())
                : nullptr;
        return Address && Address->getOpcode() == UO_AddrOf &&
               IsPart(Address->getSubExpr(), Depth + 1);
      }
      if (const auto *Choice = dyn_cast<AbstractConditionalOperator>(Target);
          Choice && Choice->isGLValue())
        return llvm::any_of(keptArms(Choice), [&](const Expr *Arm) {
          return IsPart(Arm, Depth + 1);
        });
      if (const auto *Deref = dyn_cast<UnaryOperator>(Target);
          Deref && Deref->getOpcode() == UO_Deref &&
          isa<DeclRefExpr>(Deref->getSubExpr()->IgnoreParenImpCasts())) {
        Target = Deref->getSubExpr();
        continue;
      }
      if (const auto *Member = dyn_cast<MemberExpr>(Target);
          Member && Member->isArrow() &&
          isa<DeclRefExpr>(Member->getBase()->IgnoreParenImpCasts())) {
        Target = Member->getBase();
        continue;
      }
      if (const auto *Member = dyn_cast<MemberExpr>(Target);
          Member && !Member->isArrow())
        Target = Member->getBase();
      else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Target);
               Subscript && Subscript->getBase()
                                ->IgnoreParenImpCasts()
                                ->getType()
                                ->isArrayType())
        Target = Subscript->getBase();
      else
        return false;
    }
    return false;
  };
  const ASTContext &Context = Local->getASTContext();
  llvm::SmallVector<const Stmt *, 32> Pending{Function->getBody()};
  llvm::SmallPtrSet<const Stmt *, 32> Seen;
  while (!Pending.empty()) {
    const Stmt *S = Pending.pop_back_val();
    if (!S || !Seen.insert(S).second || hasUnevaluatedOperands(S))
      continue;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->isAssignmentOp() && IsPart(Assign->getLHS(), 0))
      Values.push_back(Assign->getRHS());
    else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S);
             Operator && Operator->isAssignmentOp() &&
             Operator->getNumArgs() == 2 && IsPart(Operator->getArg(0), 0))
      Values.push_back(Operator->getArg(1));
    llvm::append_range(Pending, evaluatedChildren(S, Context));
  }
  return Values;
}

namespace {
/// What locals are given in a function, of each once.
struct CLocalWrites {
  /// What setters it is given to set it to (setParameters).
  TCSummaries<const VarDecl *, llvm::SmallVector<const Expr *, 4>> Set{
      &setValuesOf, llvm::SmallVector<const Expr *, 4>(),
      llvm::SmallVector<const Expr *, 4>()};
  /// What a local view or a part of it held by value is assigned.
  TCSummaries<const VarDecl *, llvm::SmallVector<const Expr *, 4>> Parts{
      &partValuesOf, llvm::SmallVector<const Expr *, 4>(),
      llvm::SmallVector<const Expr *, 4>()};
};
} // namespace

/// The one pointer member of its object that each way of each return of \p
/// Method gives what it points to of (returnedRoots), `return m_pOutput;` or
/// `return *m_pOutput;`; null when the ways end elsewhere or at several.
static const FieldDecl *givenPointerMember(const CXXMethodDecl *Method,
                                           ASTContext &Context) {
  const CReturnedRoots *Roots = CProvenance(Context).returnedRoots(Method);
  if (!Roots || Roots->Other || Roots->Roots.empty())
    return nullptr;
  const FieldDecl *Given = nullptr;
  for (const CReturnedRoot &Root : Roots->Roots) {
    const FieldDecl *Field = nullptr;
    for (const CProjection &Projection : Root.Projections) {
      if (Projection.Element)
        return nullptr;
      if (Projection.Path.empty())
        continue;
      if (Field || Projection.Path.size() != 1)
        return nullptr;
      Field = Projection.Path.front();
    }
    if (Root.Kind != CReturnedRoot::EKind::Object || !Root.Bound.empty() ||
        !Field || !Field->getType()->isPointerType() ||
        (Given && Given != Field))
      return nullptr;
    Given = Field;
  }
  return Given;
}

bool madeOfPointer(const Expr *Value,
                   llvm::function_ref<bool(const VarDecl *)> Carries) {
  TCRecursive<bool(const Expr *, int)> MadeOf = [&](const Expr *Part,
                                                    int Depth) -> bool {
    if (!Part || Depth >= 16)
      return false;
    Part = Part->IgnoreParens();
    if (const auto *Comma = dyn_cast<BinaryOperator>(Part);
        Comma && Comma->getOpcode() == BO_Comma)
      return MadeOf(Comma->getRHS(), Depth + 1);
    // Stepped, `Offset + reinterpret_cast<uintptr_t>(&m_Value)`.
    if (const auto *Step = dyn_cast<BinaryOperator>(Part);
        Step && Step->isAdditiveOp())
      return MadeOf(Step->getLHS(), Depth + 1) ||
             MadeOf(Step->getRHS(), Depth + 1);
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Part))
      return llvm::any_of(keptArms(Conditional), [&](const Expr *Arm) {
        return MadeOf(Arm, Depth + 1);
      });
    // An integer local that carries one, `Address` of `auto Address =
    // reinterpret_cast<umint>(&m_Value)`.
    if (const auto *Named = dyn_cast<DeclRefExpr>(Part)) {
      const auto *Local = dyn_cast<VarDecl>(Named->getDecl());
      return Carries && Local && Carries(Local);
    }
    const auto *Cast = dyn_cast<CastExpr>(Part);
    if (!Cast)
      return false;
    if (Cast->getCastKind() == CK_PointerToIntegral ||
        (isa<BuiltinBitCastExpr>(Cast) &&
         Cast->getSubExpr()->getType()->isPointerType()))
      return true;
    return MadeOf(Cast->getSubExpr(), Depth + 1);
  };
  return MadeOf(Value, 0);
}

/// One step of objectRelation: of \p E, whose children it classifies with
/// \p Relation of each. With \p Context, a local alias that a lambda the use
/// is in copies and changes isn't followed.
static CObjectRelation
objectRelationStep(const Expr *E,
                   llvm::function_ref<bool(const Expr *)> IsObject,
                   llvm::function_ref<CObjectRelation(const Expr *)> Relation,
                   ASTContext *Context) {
  using EKind = EObjectRelation;
  auto Unrelated = [] { return CObjectRelation{EKind::Unrelated, nullptr}; };
  auto Unknown = [] { return CObjectRelation{EKind::Unknown, nullptr}; };
  auto Related = [](const CObjectRelation &Of) {
    return Of.Kind != EKind::Unrelated;
  };
  // Of what can be any of what it is made of: unknown when it is made of the
  // object, unrelated otherwise.
  auto Generic = [&]() -> CObjectRelation {
    for (const Stmt *Child : E->children())
      if (const auto *Part = dyn_cast_or_null<Expr>(Child);
          Part && Related(Relation(Part)))
        return Unknown();
    return Unrelated();
  };
  // Of the object, a subobject of it or an entry of it, an entry, one
  // deeper than it.
  // An entry of what is kept alive isn't (bKeptAlive): it can be erased.
  auto EntryOf = [&](CObjectRelation Of) -> CObjectRelation {
    if (Of.Kind == EKind::SameObject || Of.Kind == EKind::Subobject)
      Of.Kind = EKind::Entry;
    if (Of.Kind == EKind::Entry) {
      ++Of.EntryDepth;
      ++Of.StoredDepth;
      Of.bMadeValue = false;
    }
    Of.bKeptAlive = false;
    return Of;
  };
  // What a pointer or reference that the object, or what is reached from it,
  // holds points to: also of a copy, what the source's points to, not the
  // copy's own (bCopy), nor kept alive by what holds it (bKeptAlive).
  auto Through = [&](CObjectRelation Of) -> CObjectRelation {
    if (Of.Kind == EKind::Subobject || Of.Kind == EKind::Entry)
      Of.bThroughPointer = true;
    Of.bInUnion = false;
    Of.bCopy = false;
    Of.bKeptAlive = false;
    return Of;
  };
  // Whether \p Operand of an address is a local pointer, `&pValue` or
  // `std::addressof(pValue)`, which is of the local, not of what it points to.
  // Also through a local reference to one, `auto &rp = pValue`, or a cast
  // to a reference to it, `static_cast<int *&>(pValue)`.
  auto AddressOfLocalPointer = [&](const Expr *Operand) {
    for (int Depth = 0; Operand && Depth < 16; ++Depth) {
      Operand = Operand->IgnoreParenImpCasts();
      if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Operand);
          Cast && Cast->isGLValue()) {
        Operand = Cast->getSubExpr();
        continue;
      }
      const auto *Ref = dyn_cast<DeclRefExpr>(Operand);
      const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      if (!Local || !(Local->hasLocalStorage() || Local->isInitCapture()) ||
          IsObject(Ref))
        return false;
      if (Local->getType()->isPointerType())
        return true;
      if (!Local->getType()->isReferenceType() || !Local->getInit() ||
          isa<ParmVarDecl>(Local))
        return false;
      Operand = Local->getInit();
    }
    return false;
  };
  // What an owning pointer, a class with `operator->`, of \p Type points to:
  // what it is of, owned, unless that is an actor, derived from `CActor`,
  // which has a life of its own, like a `TCSharedPointer<CApp>`, not the
  // internals of one.
  // What an owning pointer, \p Owner, owns is one deeper than it, however
  // the owner is reached, which it can end without the owner; not what an
  // iterator or a value made from what it is in, like a handle, refers to,
  // which is that itself.
  auto Owned = [&](CObjectRelation Of, QualType Type,
                   const Expr *Owner) -> CObjectRelation {
    Type = Type.getNonReferenceType();
    if (Type->isPointerType())
      Type = Type->getPointeeType();
    if (Of.Kind != EKind::Unrelated &&
        derivesFrom(Type->getAsCXXRecordDecl(), {"CActor"}))
      Of.bThroughPointer = true;
    QualType OwnerType = Owner->getType().getNonReferenceType();
    if (OwnerType->isPointerType())
      OwnerType = OwnerType->getPointeeType();
    if (Related(Of) && !Of.bMadeValue && !isIteratorType(OwnerType)) {
      ++Of.EntryDepth;
      ++Of.StoredDepth;
    }
    Of.bMadeValue = false;
    // What it points to is stored by itself, not in a union it is in.
    Of.bInUnion = false;
    // What a copy of a shared owner points to is what the source points to,
    // which the copy keeps alive (bKeptAlive), and so is what another
    // pointer like it points to; what an owner that holds its value in it
    // gives, like an optional, is the copy's own (bCopy, holdsValueInline).
    const CXXRecordDecl *OwnerRecord = OwnerType->getAsCXXRecordDecl();
    const bool Shared =
        OwnerRecord &&
        isSharedPointerType(OwnerType,
                            Context ? *Context : OwnerRecord->getASTContext());
    Of.bKeptAlive = Shared && isCopyStorage(Of);
    // What a unique owner moved into a new one owns is that one's alone
    // (isUniqueOwner), `auto pState = fg_Move(mp_pState)`.
    if (!holdsValueInline(OwnerType) && !isUniqueOwner(OwnerType))
      Of.bCopy = false;
    return Of;
  };
  // What any of \p Values is: what all are; a member or an entry of it, which
  // can't be told, when all are within it the same way, also through a
  // pointer it holds or as a copy; else unknown when any is related, through
  // a pointer or a copy when all that are related are.
  auto AnyOfRelations = [](llvm::ArrayRef<CObjectRelation> Of) {
    return mergeRelations(Of);
  };
  auto AnyOf = [&](llvm::ArrayRef<const Expr *> Values) -> CObjectRelation {
    llvm::SmallVector<CObjectRelation, 4> Of;
    for (const Expr *Value : Values)
      Of.push_back(Relation(Value));
    return AnyOfRelations(Of);
  };
  // Further through a pointer, `p + 1` or `p[1]`: an entry of what an entry
  // or the object is in, and in a subobject, like an array member or the
  // storage of a fixed-size array, `mp_Values.data()[1]`, that subobject.
  auto Stepped = [&](CObjectRelation Of) {
    if ((Of.Kind == EKind::Subobject && !Of.bThroughPointer) ||
        Of.Kind == EKind::Entry)
      return Of;
    return EntryOf(Of);
  };
  // Of the object or a subobject, a subobject: an element of a fixed-size
  // array.
  auto SubobjectOf = [&](CObjectRelation Of) {
    if (Of.Kind == EKind::SameObject)
      Of.Kind = EKind::Subobject;
    return Of;
  };
  TCRecursive<CObjectRelation(const CallExpr *, const Expr *)> Gives;
  // What a member function \p Method, called by \p Call on \p Object,
  // gives when its body can be seen (returnedRelation): the object itself,
  // `return *this;`, also through an alias or an address, or a pointer to
  // it, a member of it, `return m_Children;`, or an entry of it, also
  // deeper, `return (*this)[0].m_Children.f_FindEqual(1);`, or what an owning
  // pointer of it owns, `return m_pData.f_Get();`, as deep as that from the
  // object; for an entry, at least one deeper for a lookup and at most one
  // deeper for a change through it. None otherwise: what a pointer the object
  // holds points to, which the fallback tells, or when an override can give
  // something else.
  // Whether \p Object is a value a call whose body can be seen makes of
  // what it is given, each return a construction or a list, `return
  // {m_pData[_Key], false};` of a lookup giving a handle, whose relation is
  // of what it is made of, not a copy of storage.
  auto MadeByCall = [&](const Expr *Object) {
    const Expr *Made = Object;
    for (int Depth = 0; Made && Depth < 8; ++Depth) {
      const Expr *Inner = Made->IgnoreImplicit()->IgnoreParens();
      if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(Inner))
        Inner = Materialize->getSubExpr();
      if (Inner == Made)
        break;
      Made = Inner;
    }
    const auto *Call = dyn_cast_or_null<CallExpr>(Made);
    const FunctionDecl *Callee = Call ? Call->getDirectCallee() : nullptr;
    if (!Callee || !Call->isPRValue() || callsOverride(Call))
      return false;
    const FunctionDecl *Definition = Callee->getDefinition();
    if (!Definition || !Definition->getBody())
      return false;
    const llvm::SmallVector<const Expr *, 4> Values =
        returnedValues(Definition);
    return !Values.empty() && llvm::all_of(Values, [](const Expr *Value) {
      Value = Value->IgnoreImplicit()->IgnoreParens();
      if (const auto *Construct = dyn_cast<CXXConstructExpr>(Value))
        return !Construct->getConstructor() ||
               !Construct->getConstructor()->isCopyOrMoveConstructor();
      return isa<InitListExpr, CXXParenListInitExpr>(Value);
    });
  };
  auto BodyGives = [&](const CXXMethodDecl *Method, const CallExpr *Call,
                       const Expr *Object) -> std::optional<CObjectRelation> {
    // Not a lambda's, whose object is the closure (LambdaGives).
    if (callsOverride(Call) || Method->getParent()->isLambda())
      return std::nullopt;
    const std::optional<CObjectRelation> Returned =
        returnedRelation(Method, Context);
    // Of a local made with its members, what the one pointer member each
    // way of each return gives (returnedRoots) holds where it is called,
    // `Buffers.f_Output()` returning `m_pOutput` of `CBuffers Buffers{&m_Value,
    // &Value}`, not what all of it is made of; of each value it can hold
    // there, told.
    if (const FieldDecl *Field =
            Context ? givenPointerMember(Method, *Context) : nullptr) {
      const auto *Local = dyn_cast_or_null<VarDecl>(namedDecl(Object, *Context));
      if (Local && Local->hasLocalStorage() &&
          !Local->getType()->isReferenceType() &&
          Local->getType()->isRecordType()) {
        const CSlotValues Values =
            localSlotValues({Local, {Field}, false}, Call, *Context);
        if (Values.Told && !Values.Values.empty() &&
            llvm::all_of(Values.Values, [](const CSlotValue &Value) {
              return Value.Kind == CSlotValue::EKind::Value && Value.Value;
            })) {
          llvm::SmallVector<CObjectRelation, 2> Of;
          for (const CSlotValue &Value : Values.Values)
            Of.push_back(Relation(Value.Value));
          return mergeRelations(Of);
        }
      }
    }
    // What a borrowed member of it leads to (returnsBorrowed), through a
    // pointer it holds: of a copy what the source's does, `f_Items()`
    // returning `m_Items` of a copy; of a handle made from what it refers
    // to, that itself (bMadeValue), `mp_Items(0).f_GetResult()`; of what
    // isn't a copy what it is in leads to there, through that pointer too,
    // `View.f_GetConfiguration()` returning `*m_pConfiguration` of `auto View
    // = m_View`. Not of a unique owner moved into a new one, which owns what
    // it points to alone (isUniqueOwner), `pItems->f_Clear()` of `auto pItems
    // = fg_Move(mp_pItems)`, nor of another owning pointer, what it owns one
    // deeper (Owned, below).
    QualType ObjectType = Object->getType().getNonReferenceType();
    if (ObjectType->isPointerType())
      ObjectType = ObjectType->getPointeeType();
    if (Returned && Returned->bThroughPointer && returnsBorrowed(Method) &&
        !isUniqueOwner(ObjectType)) {
      CObjectRelation Of = Relation(Object);
      if (isCopyStorage(Of)) {
        if (!Of.bMadeValue && !MadeByCall(Object))
          return Through(Of);
        Of.bCopy = false;
        Of.bKeptAlive = false;
        Of.bMadeValue = false;
        return Of;
      }
      // Of what tells the object's own storage, not what it refers to, `View`
      // of `CView View{&mp_Items}`: the object itself, or a member of its
      // class, also copied or through an alias. Not of an iterator, whose
      // pointers lead into what it is made from.
      const auto OwnStorage = [&] {
        if (Of.Kind == EKind::SameObject)
          return true;
        const QualType Member =
            Of.Kind == EKind::Subobject && Of.Outermost
                ? Of.Outermost->getMemberDecl()->getType().getNonReferenceType()
                : QualType();
        return !Member.isNull() &&
               Member.getCanonicalType().getUnqualifiedType() ==
                   ObjectType.getCanonicalType().getUnqualifiedType();
      };
      if (ownedPointee(ObjectType).isNull() && !isIteratorType(ObjectType) &&
          OwnStorage())
        return Through(composeRelations(Of, *Returned));
    }
    if (!Returned || Returned->bCopy || Returned->bThroughPointer)
      return std::nullopt;
    // Nothing of it, a global, `return g_Values;`, unless what it gives can
    // be what it is given, `return _pValue;`, also through its locals, which
    // is what that is.
    if (Returned->Kind == EKind::Unrelated) {
      bool NamesParameter = false;
      llvm::SmallPtrSet<const VarDecl *, 8> Followed;
      llvm::SmallVector<const Stmt *, 16> Work;
      if (const FunctionDecl *Definition = Method->getDefinition())
        llvm::append_range(Work, returnedValues(Definition));
      while (!Work.empty() && !NamesParameter) {
        const Stmt *Each = Work.pop_back_val();
        if (!Each)
          continue;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(Each)) {
          if (isa<ParmVarDecl>(Ref->getDecl()))
            NamesParameter = true;
          else if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
                   Local && Local->hasLocalStorage() && Local->getInit() &&
                   Followed.insert(Local).second)
            Work.push_back(Local->getInit());
        }
        llvm::append_range(Work, keptChildren(Each));
      }
      if (!NamesParameter)
        return Unrelated();
    }
    // What can't be told, `return _bOwn ? *this : g_Other;` or a node found
    // in a loop, `return &pNode->m_Value;` of a `TCMap`, can be the object
    // itself, an entry of it or what is no part of it: of what the object
    // is in, an entry at least one deeper for a lookup, and the object itself
    // for a change through it (composeRelations).
    if (Returned->Kind == EKind::Unrelated)
      return std::nullopt;
    CObjectRelation Of = composeRelations(Relation(Object), *Returned);
    if (!Related(Of) || Returned->Kind == EKind::Unknown)
      return Of;
    // Another actor it gives, not the object itself, has a life of its own,
    // `operator->` of a `TCActor`.
    QualType Pointee = Call->getType().getNonReferenceType();
    if (Pointee->isPointerType())
      Pointee = Pointee->getPointeeType();
    if (Returned->Kind != EKind::SameObject &&
        derivesFrom(Pointee->getAsCXXRecordDecl(), {"CActor"}))
      Of.bThroughPointer = true;
    return Of;
  };
  // Of what \p Method, called by \p Call, gives of what is in the object, a
  // copy at its own level when it has data of its own (isCopyAtOwnLevel),
  // `mp_Items.f_Copy()`, whose borrowed parts lead to what the object's do;
  // a view, a handle, an iterator or a pointer wrapper is what is in it.
  auto CopyOf = [&](CObjectRelation Result, const CallExpr *Call,
                    const CXXMethodDecl *Method) {
    if (!Related(Result) || !Call->isPRValue() ||
        Call->getType()->isPointerType() || !Method)
      return Result;
    if (isIndependentType(Call->getType(), Method->getASTContext()) ||
        isCopyAtOwnLevel(Call->getType(), Method->getASTContext()))
      Result.bCopy = true;
    // A value that refers back can refer to what it is made from itself,
    // `mp_Items.f_View()`, not only to an entry of it; not an iterator, which
    // refers to an entry.
    else if (Result.Kind == EKind::Entry && Result.StoredDepth > 0 &&
             !isIteratorType(Call->getType())) {
      --Result.StoredDepth;
      Result.bMadeValue = true;
    }
    return Result;
  };
  // What a member operator \p Method, called by \p Call on \p Object, gives,
  // the same written as an operator or called by name, `mp_pItems.operator*()`
  // or `mp_Values.operator[](0)`: a subscript an entry, or a subobject of a
  // fixed-size array, `->` and `*` what an owning pointer or an iterator
  // points to, and a call operator of a lambda what the lambda gives back.
  // None for another.
  auto OperatorGives =
      [&](const CXXMethodDecl *Method, const CallExpr *Call,
          const Expr *Object) -> std::optional<CObjectRelation> {
    const OverloadedOperatorKind Operator = Method->getOverloadedOperator();
    const unsigned Unary = Method->isExplicitObjectMemberFunction() ? 1 : 0;
    if (Operator == OO_Subscript)
      return isFixedArrayType(Object->getType(), Method->getASTContext())
                 ? SubobjectOf(Relation(Object))
                 : CopyOf(EntryOf(Relation(Object)), Call, Method);
    if ((Operator == OO_Arrow || Operator == OO_Star) &&
        Method->getNumParams() == Unary)
      return Owned(Relation(Object), Call->getType(), Object);
    return std::nullopt;
  };
  // What a member function, \p Call, gives of the object it is called on,
  // \p Object: what is in it, an entry, whatever it gives, by reference or
  // pointer, like `mp_Items.f_FindEqual(1)`, an iterator, or a value made
  // from it, a handle or a copy. What the pointer of an owning pointer,
  // `mp_pInternal.f_Get()`, gives is what `operator->` does; an element of a
  // fixed-size array, `mp_Values.front()`, a subobject; an accessor that
  // gives the object, the object, unless an override can give something else.
  Gives = [&](const CallExpr *Call, const Expr *Object) -> CObjectRelation {
    const CObjectRelation Of = Relation(Object);
    const auto *Method =
        dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
    // One whose body can be seen, also an operator, `operator*` returning
    // `*this`, gives what that tells, before what its name or kind tells.
    if (Method)
      if (const std::optional<CObjectRelation> Body =
              BodyGives(Method, Call, Object))
        return *Body;
    // One that gives the object itself (givesItsObject), also an operator
    // whose body can't be seen that returns a reference to its own class,
    // `CItems &operator[](int)`, before what its kind tells.
    if (Method && givesItsObject(Method)) {
      if (!callsOverride(Call))
        return Of;
      return Related(Of) ? Unknown() : Unrelated();
    }
    if (Method && Method->getOverloadedOperator() != OO_None)
      if (const std::optional<CObjectRelation> Operator =
              OperatorGives(Method, Call, Object))
        return *Operator;
    if (Method && Method->getIdentifier() &&
        (identifierTextOf(Method) == "f_Get" || identifierTextOf(Method) == "get") &&
        Method->getReturnType()->isPointerType() &&
        hasOverloadedOperator(Method->getParent(), {OO_Arrow}) == true)
      return Owned(Of, Call->getType(), Object);
    if (Method && Method->getIdentifier() &&
        llvm::is_contained(
            {StringRef("at"), StringRef("front"), StringRef("back"),
             StringRef("data"), StringRef("begin"), StringRef("end"),
             StringRef("cbegin"), StringRef("cend"), StringRef("rbegin"),
             StringRef("rend"), StringRef("crbegin"), StringRef("crend")},
            identifierTextOf(Method)) &&
        isFixedArrayType(Object->getType(), Method->getASTContext()))
      return SubobjectOf(Of);
    if (Method && givesItsObject(Method)) {
      if (!callsOverride(Call))
        return Of;
      return Related(Of) ? Unknown() : Unrelated();
    }
    // An iterator that an iterator gives, also a copy of it, `iEntry.
    // f_GetIterator()`, is at the same entry, not one deeper.
    if (QualType ObjectType = Object->getType().getNonReferenceType();
        Method && isIteratorType(Call->getType().getNonReferenceType()) &&
        isIteratorType(ObjectType->isPointerType()
                           ? ObjectType->getPointeeType()
                           : ObjectType))
      return Of;
    // Of a copy that has borrowed parts (hasBorrowedParts), what a pointer
    // or reference one that can't be seen gives can be what they lead to.
    if (isCopyStorage(Of) && Method && !Method->getDefinition() &&
        (Call->getType()->isPointerType() || Call->isGLValue()) &&
        hasBorrowedParts(Object->getType().getNonReferenceType(),
                         Method->getASTContext()))
      return Through(EntryOf(Of));
    CObjectRelation Result = CopyOf(EntryOf(Of), Call, Method);
    // Another actor that a member of it gives, `m_Wrapper.get()` of a pointer
    // to one, has a life of its own, unless it is the object itself, given
    // by an accessor of it that returns `this`.
    QualType Pointee = Call->getType().getNonReferenceType();
    if (Pointee->isPointerType())
      Pointee = Pointee->getPointeeType();
    if (Related(Result) &&
        derivesFrom(Pointee->getAsCXXRecordDecl(), {"CActor"}) &&
        !(Method && givesItsObject(Method) && !callsOverride(Call)))
      Result.bThroughPointer = true;
    return Result;
  };
  // What a lambda, \p Operator, called by \p Call gives back: what it returns
  // of what it is given, as it is or its address, `[](int &_Value) { return
  // &_Value; }(m_Value)`, and any other value it returns.
  auto LambdaGives = [&](const CXXMethodDecl *Operator,
                         const CallExpr *Call) -> CObjectRelation {
    llvm::SmallVector<CObjectRelation, 4> Given;
    llvm::SmallVector<const Expr *, 4> Values;
    // A pointer given by reference and given back as its value, `[](CApp
    // *const &_p) { return _p; }(m_pOther)`, is what it points to, through a
    // pointer when an object holds it.
    if (Call) {
      for (const Expr *Argument : returnedArguments(Call)) {
        CObjectRelation Of = Relation(Argument);
        if (Call->isPRValue() && Call->getType()->isPointerType() &&
            isStoredPointer(Argument))
          Of = Through(Of);
        Given.push_back(Of);
      }
      llvm::append_range(Values, returnedArguments(Call, /*Addressed=*/true));
    }
    for (const Expr *Value : returnedValues(Operator)) {
      const Expr *Bare = Value->IgnoreParenCasts();
      if (const auto *Address = dyn_cast<UnaryOperator>(Bare);
          Address && Address->getOpcode() == UO_AddrOf)
        Bare = Address->getSubExpr()->IgnoreParenCasts();
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Bare);
          Call && Ref && isa<ParmVarDecl>(Ref->getDecl()))
        continue;
      // A part of what it is given, `return _Data.m_Value;`, is that part of
      // the argument (composeRelations), its parameters bound.
      bool Bound = false;
      if (Call)
        for (const ParmVarDecl *Parameter : Operator->parameters()) {
          const unsigned iArgument = Parameter->getFunctionScopeIndex() +
                                     argumentShift(Call, Operator);
          if (iArgument >= Call->getNumArgs() ||
              !Parameter->getType()->isReferenceType())
            continue;
          const CObjectRelation Of = objectRelation(
              Value,
              [&](const Expr *E) {
                const auto *Named = dyn_cast<DeclRefExpr>(E);
                return Named && Named->getDecl() == Parameter;
              },
              Context);
          if (Of.Kind == EKind::Unrelated)
            continue;
          Given.push_back(
              composeRelations(Relation(Call->getArg(iArgument)), Of));
          Bound = true;
        }
      if (!Bound)
        Values.push_back(Value);
    }
    for (const Expr *Value : Values)
      Given.push_back(Relation(Value));
    return AnyOfRelations(Given);
  };
  // What a free function gives back of what it is given. When its body can
  // be seen: each return of a parameter that isn't changed, also its address,
  // `return _Value;` or `return &_Value;`, gives the argument; a return made
  // of no local, parameter or object, like a global, `return g_Value;`,
  // nothing; any other, like what a local alias or a parameter assigned
  // again gives, `auto &Alias = _Wrapper; return Alias.m_Items;`, can't be
  // told. Else, or when it can't, by reference or pointer what can't be told
  // when it is given the object by reference or pointer.
  // Whether \p Made, a value made by a construction or an aggregate, also
  // a local made so that a function gives back, is given what is related
  // for a pointer or a reference member, or a pointer parameter, not by
  // default, nor a value of it made so, `CView{&t_Value}`.
  TCRecursive<bool(const Expr *, int)> MadeOfBorrowed =
      [&](const Expr *Made, int Level) -> bool {
    if (!Made || Level > 8)
      return false;
    Made = Made->IgnoreImplicit()->IgnoreParens();
    if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Made))
      Made = Cast->getSubExpr()->IgnoreImplicit()->IgnoreParens();
    // A copy or a move of a local, `return View;`, is what that is.
    if (const auto *Copy = dyn_cast<CXXConstructExpr>(Made);
        Copy && Copy->getConstructor() &&
        Copy->getConstructor()->isCopyOrMoveConstructor() &&
        !Copy->getConstructor()->isUserProvided() && Copy->getNumArgs() >= 1)
      Made = Copy->getArg(0)->IgnoreImplicit()->IgnoreParens();
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Made))
      if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
          Local && Local->hasLocalStorage() && Local->getInit() &&
          !Local->getType()->isReferenceType())
        return MadeOfBorrowed(Local->getInit(), Level + 1);
    auto Part = [&](const Expr *Value, QualType Bound) {
      if (!Value || isa<CXXDefaultArgExpr>(Value))
        return false;
      if (!Bound.isNull() &&
          (Bound->isPointerType() || Bound->isReferenceType()) &&
          Relation(Value).Kind != EKind::Unrelated)
        return true;
      return MadeOfBorrowed(Value, Level + 1);
    };
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(Made)) {
      const CXXConstructorDecl *Constructor = Construct->getConstructor();
      if (!Constructor || Constructor->isCopyOrMoveConstructor())
        return false;
      for (unsigned iArgument = 0; iArgument < Construct->getNumArgs() &&
                                   iArgument < Constructor->getNumParams();
           ++iArgument) {
        const QualType Bound = Constructor->getParamDecl(iArgument)->getType();
        if (Bound->isPointerType() && Part(Construct->getArg(iArgument), Bound))
          return true;
      }
      return false;
    }
    for (const CAggregatePart &Each : aggregateParts(Made))
      if (Part(Each.Init, Each.Type))
        return true;
    return false;
  };
  auto FreeGives = [&](const CallExpr *Call) -> CObjectRelation {
    if (const Expr *Kept = keptPointerArgument(Call))
      return Relation(Kept);
    const FunctionDecl *Callee = Call->getDirectCallee();
    // After the object of an operator with one, not an explicit object
    // parameter, which is a parameter.
    const unsigned Offset = argumentShift(Call, Callee);
    // Of the body that runs (calledDefinition), also through an unchanged
    // local pointer to it, `fGet()` of `auto fGet = &fg_Get;`
    // (functionCalledBy).
    const FunctionDecl *Definition = calledDefinition(Call);
    if (const FunctionDecl *Target = Callee ? nullptr : functionCalledBy(Call);
        Target && Target->getDefinition() && Target->getDefinition()->getBody())
      Definition = Target->getDefinition();
    if (Definition && !isa<CoroutineBodyStmt>(Definition->getBody())) {
      const llvm::SmallVector<const Expr *, 4> Values =
          returnedValues(Definition);
      llvm::SmallVector<CObjectRelation, 4> Given;
      bool Told = !Values.empty();
      // What names what the function is given or its object, which aren't
      // what the caller's are; its own locals are followed by what they are
      // made with (objectRelation).
      auto NamesGiven = [&](const Expr *Value) {
        bool Found = false;
        llvm::SmallPtrSet<const VarDecl *, 8> Followed;
        TCRecursive<void(const Stmt *)> Find = [&](const Stmt *S) {
          if (!S || Found)
            return;
          // Of a choice, what is chosen, not what chooses, `return _bFirst ?
          // &t_First : &t_Second;`.
          if (const auto *Choice = dyn_cast<AbstractConditionalOperator>(S)) {
            for (const Expr *Arm : keptArms(Choice))
              Find(Arm);
            return;
          }
          // Of an element, the array, not the index, `&t_Values[_Index]`,
          // also moved by an offset, `t_Buffer + _Index`.
          if (const auto *Element = dyn_cast<ArraySubscriptExpr>(S)) {
            Find(Element->getBase());
            return;
          }
          if (const auto *Offset = dyn_cast<BinaryOperator>(S);
              Offset && Offset->isAdditiveOp() &&
              Offset->getType()->isPointerType()) {
            Find(Offset->getLHS()->getType()->isPointerType()
                     ? Offset->getLHS()
                     : Offset->getRHS());
            return;
          }
          if (isa<CXXThisExpr>(S))
            Found = true;
          if (const auto *Ref = dyn_cast<DeclRefExpr>(S)) {
            if (isa<ParmVarDecl>(Ref->getDecl()))
              Found = true;
            // Also through the locals it is made of, `auto &Alias =
            // _Wrapped; return Alias.m_Items;`.
            else if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
                     Local && Local->hasLocalStorage() && Local->getInit() &&
                     Followed.insert(Local).second)
              Find(Local->getInit());
          }
          for (const Stmt *Child : keptChildren(S))
            Find(Child);
        };
        Find(Value);
        return Found;
      };
      for (const Expr *Value : Values) {
        const Expr *Bare = Value->IgnoreParenCasts();
        bool Addressed = false;
        if (const auto *Address = dyn_cast<UnaryOperator>(Bare);
            Address && Address->getOpcode() == UO_AddrOf) {
          Bare = Address->getSubExpr()->IgnoreParenCasts();
          Addressed = true;
        }
        const auto *Ref = dyn_cast<DeclRefExpr>(Bare);
        const auto *Parameter =
            Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl()) : nullptr;
        if (Parameter && !isChangedVariable(Parameter) &&
            (!Addressed || Parameter->getType()->isReferenceType()) &&
            Parameter->getFunctionScopeIndex() + Offset < Call->getNumArgs()) {
          // A pointer given by reference and given back as its value,
          // `fg_Id(m_pOther)` of `CState *fg_Id(CState *const &)`, is what
          // it points to, through a pointer when an object holds it.
          const Expr *Argument =
              Call->getArg(Parameter->getFunctionScopeIndex() + Offset);
          CObjectRelation Of = Relation(Argument);
          if (!Addressed && Call->isPRValue() &&
              Call->getType()->isPointerType() && isStoredPointer(Argument))
            Of = Through(Of);
          Given.push_back(Of);
          continue;
        }
        // Another that is made of nothing of what it is given is what it is
        // relative to the object itself (objectRelation), like a global,
        // `return g_Other;`, nothing of it, or the thread-local data, `return
        // t_Data;`, also through its locals, `auto &Data = t_Data; return
        // Data;`, and what another function it calls gives, as deep as a few
        // such.
        // What is made of it, which can't be told, isn't it, but a value
        // made with pointers or references to it for its members, a view or
        // a wrapper, `return CView{&t_Value};`, also of a local made so,
        // whose borrowed parts lead into it (MadeOfBorrowed). As deep as
        // that in the summary it is in (TCSummaryLocal).
        static thread_local TCSummaryLocal<int> s_Nested;
        if (NamesGiven(Value))
          Told = false;
        else if (*s_Nested < 4) {
          ++*s_Nested;
          CObjectRelation Of = Relation(Value);
          // Not of a value that isn't made, `return *pData;` of a local
          // pointer assigned the data, which can be it.
          const Expr *Bare = Value->IgnoreImplicit()->IgnoreParens();
          const bool Made =
              isa<CXXConstructExpr, InitListExpr, CXXParenListInitExpr,
                  CXXFunctionalCastExpr>(Bare);
          const bool Borrowed =
              Of.Kind == EKind::Unknown && MadeOfBorrowed(Value, 0);
          --*s_Nested;
          Given.push_back(Of.Kind == EKind::Unknown && Made && !Borrowed
                              ? Unrelated()
                              : Of);
        } else
          // Past as deep as that, nothing of it.
          Given.push_back(Unrelated());
      }
      if (Told)
        return AnyOfRelations(Given);
    }
    // Also a value that isn't independent of what it is made from, like a
    // view, a shared owner or a wrapper of a pointer to it, `fg_View(_Items)`,
    // not an owning copy, `fg_TempCopy(mp_Values)` (isIndependentType).
    if (!(Call->isGLValue() || Call->getType()->isPointerType() ||
          isIteratorType(Call->getType()) ||
          (Call->getType()->isRecordType() && Callee &&
           !isIndependentType(Call->getType(), Callee->getASTContext()))))
      return Unrelated();
    // What can't be told of what it is given (unknownOfRelations), of a value
    // with data of its own that data its own (isCopyAtOwnLevel).
    const bool OwnStorage =
        Call->isPRValue() && Callee &&
        isCopyAtOwnLevel(Call->getType(), Callee->getASTContext());
    llvm::SmallVector<CObjectRelation, 4> Given;
    for (unsigned iArgument = 0; iArgument < Call->getNumArgs(); ++iArgument) {
      const QualType Parameter =
          Callee && iArgument >= Offset &&
                  iArgument - Offset < Callee->getNumParams()
              ? Callee->getParamDecl(iArgument - Offset)->getType()
              : Call->getArg(iArgument)->getType();
      if (!(Parameter->isReferenceType() || Parameter->isPointerType() ||
            !Callee))
        continue;
      Given.push_back(Relation(Call->getArg(iArgument)));
    }
    CObjectRelation Result = unknownOfRelations(Given);
    if (OwnStorage)
      Result.bCopy = Result.Kind != EKind::Unrelated;
    return Result;
  };
  // Whether \p Call is of a member function whose body can be seen, that no
  // override can run instead of, not of a lambda, that gives nothing of its
  // object (returnedRelation), which gives what it gives of what it is given
  // or of anything else like a free function, `f_Identity(_pValue)` or
  // `return &fg_ConcurrencyThreadLocal().m_Value;` (FreeGives).
  auto ReceiverFree = [&](const CallExpr *Call) {
    const auto *Method =
        dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
    if (!Method || Method->isStatic() || callsOverride(Call) ||
        Method->getParent()->isLambda())
      return false;
    const std::optional<CObjectRelation> Returned =
        returnedRelation(Method, Context);
    return Returned && Returned->Kind == EKind::Unrelated;
  };
  // Whether a value of \p Type can refer back to what it is made from
  // (canReferBack), also without a context, when it is a pointer, a
  // reference or an array of them, or of a class, by its own.
  auto CanReferBack = [&](QualType Type) {
    if (Type.isNull())
      return true;
    if (Context)
      return canReferBack(Type, *Context);
    QualType Element = Type.getNonReferenceType();
    while (const ArrayType *Array = Element->getAsArrayTypeUnsafe())
      Element = Array->getElementType();
    if (const CXXRecordDecl *Record = Element->getAsCXXRecordDecl())
      return canReferBack(Type, Record->getASTContext());
    return Type->isReferenceType() || Element->isPointerType() ||
           Element->isReferenceType();
  };
  // Whether a value of \p Type is independent of what it is made from
  // (isIndependentType), also without a context, by its class.
  auto IsIndependent = [&](QualType Type) {
    if (Context)
      return isIndependentType(Type, *Context);
    QualType Element = Type.getNonReferenceType();
    while (const ArrayType *Array = Element->getAsArrayTypeUnsafe())
      Element = Array->getElementType();
    if (const CXXRecordDecl *Record = Element->getAsCXXRecordDecl())
      return isIndependentType(Type, Record->getASTContext());
    return !Type->isReferenceType() && !Element->isPointerType();
  };
  // What an object made by \p Made, a construction or an aggregate, is
  // made of that it can keep: what it is given by pointer or reference, a
  // pointer or what binds a reference member, also of what it holds by
  // value, of what can't be told (unknownOfRelations). Unrelated when it
  // can't refer back to anything, or is independent of what it is made
  // from (isIndependentType), which copies what it is given.
  TCRecursive<CObjectRelation(const Expr *)> MadeOf =
      [&](const Expr *Made) -> CObjectRelation {
    const QualType Type = Made->getType();
    if (!CanReferBack(Type) || IsIndependent(Type))
      return Unrelated();
    llvm::SmallVector<CObjectRelation, 4> Parts;
    // \p Value given for \p Bound, a parameter's type, null for a member:
    // kept by a pointer or a reference, or as a value that can refer back,
    // a pointer, a view, or an array of them, not a number.
    auto Part = [&](const Expr *Value, QualType Bound) {
      if (!Value)
        return;
      const QualType Given = Value->getType().getNonReferenceType();
      if ((!Bound.isNull() &&
           (Bound->isPointerType() || Bound->isReferenceType())) ||
          Given->isPointerType() ||
          ((Given->isRecordType() || Given->isArrayType()) &&
           CanReferBack(Given)))
        Parts.push_back(Relation(Value));
    };
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(Made)) {
      const CXXConstructorDecl *Constructor = Construct->getConstructor();
      // A copy, or what is moved, of another of its kind is a new object
      // at its own level (bCopy, isCopyAtOwnLevel): its storage, entries
      // and what it holds by value are its own, also of a mixed one, what
      // its borrowed parts lead to the source's (Through, Owned). One that
      // only refers, a view, `CView Copy = View`, is what it refers to.
      if (Constructor && Constructor->isCopyOrMoveConstructor() &&
          Construct->getNumArgs() >= 1) {
        CObjectRelation Of = Relation(Construct->getArg(0));
        if (IsIndependent(Type) ||
            isCopyAtOwnLevel(Type, Constructor->getASTContext()) ||
            (Constructor->isMoveConstructor() && isUniqueOwner(Type)))
          Of.bCopy = Of.Kind != EKind::Unrelated;
        return Of;
      }
      for (unsigned iArgument = 0; iArgument < Construct->getNumArgs();
           ++iArgument)
        Part(Construct->getArg(iArgument),
             Constructor && iArgument < Constructor->getNumParams()
                 ? Constructor->getParamDecl(iArgument)->getType()
                 : QualType());
      // And what a constructor whose body can be seen gives its members by
      // itself, not of what it is given nor of the object it makes: what
      // they are initialized with, also by default, `int *m_pValue =
      // &t_Value;` or `m_pValue(&t_Value)`, also by one it makes itself,
      // `CView View;`, and what its body assigns them, `m_pValue = &t_Value`.
      const FunctionDecl *Defined = nullptr;
      auto OwnValue = [&](const Expr *Value) {
        llvm::SmallVector<const Stmt *, 16> Work{Value};
        while (!Work.empty()) {
          const Stmt *Each = Work.pop_back_val();
          if (!Each)
            continue;
          // Of an element, the array, not the index, `&t_Values[_Index]`.
          if (const auto *Element = dyn_cast<ArraySubscriptExpr>(Each)) {
            Work.push_back(Element->getBase());
            continue;
          }
          if (isa<CXXThisExpr>(Each))
            return false;
          if (const auto *Ref = dyn_cast<DeclRefExpr>(Each);
              Ref && isa<ParmVarDecl>(Ref->getDecl()))
            return false;
          for (const Stmt *Child : keptChildren(Each))
            Work.push_back(Child);
        }
        return true;
      };
      if (Constructor && Constructor->hasBody(Defined))
        if (const auto *Definition = dyn_cast<CXXConstructorDecl>(Defined)) {
          for (const CXXCtorInitializer *Initializer : Definition->inits()) {
            const Expr *Init = Initializer->getInit();
            if (const auto *Default =
                    dyn_cast_or_null<CXXDefaultInitExpr>(Init))
              Init = Default->getExpr();
            // Of a choice, each value apart, `_pData ? _pData :
            // &fg_ConcurrencyThreadLocal()`, which can be what it gives by
            // itself.
            llvm::SmallVector<const Expr *, 2> Alternatives;
            TCRecursive<void(const Expr *, int)> Choose =
                [&](const Expr *Value, int Level) {
                  const auto *Choice = dyn_cast<AbstractConditionalOperator>(
                      Value->IgnoreParenImpCasts());
                  if (!Choice || Level > 4) {
                    Alternatives.push_back(Value);
                    return;
                  }
                  if (const Expr *Chosen =
                          chosenValue(Choice, &Definition->getASTContext()))
                    return Choose(Chosen, Level + 1);
                  for (const Expr *Arm : keptArms(Choice))
                    Choose(Arm, Level + 1);
                };
            if (Init && Initializer->getAnyMember()) {
              Choose(Init, 0);
              for (const Expr *Alternative : Alternatives)
                if (OwnValue(Alternative))
                  Part(Alternative, Initializer->getAnyMember()->getType());
            }
            // Also what a base or the constructor it delegates to is made
            // of, `CGuard() : CGuard(&t_Value) {}`.
            if (Init && !Initializer->getAnyMember() &&
                (Initializer->isBaseInitializer() ||
                 Initializer->isDelegatingInitializer()) &&
                OwnValue(Init))
              Parts.push_back(Relation(Init));
          }
          llvm::SmallVector<const Stmt *, 16> Work{Definition->getBody()};
          while (!Work.empty()) {
            const Stmt *Each = Work.pop_back_val();
            if (!Each || isa<LambdaExpr>(Each))
              continue;
            // Of what can run (canRunIn), also through a local alias of a
            // member or of the object (assignedMember).
            if (const auto *Assign = dyn_cast<BinaryOperator>(Each);
                Assign && Assign->getOpcode() == BO_Assign)
              if (const FieldDecl *Member =
                      assignedMember(Assign->getLHS(), Definition);
                  Member && OwnValue(Assign->getRHS()) &&
                  canRunIn(Assign, Definition->getBody(), Definition,
                           Definition->getASTContext()))
                Part(Assign->getRHS(), Member->getType());
            for (const Stmt *Child : keptChildren(Each))
              Work.push_back(Child);
          }
        }
    } else {
      // Of an aggregate, each value bound to what it initializes
      // (aggregateParts).
      for (const CAggregatePart &Each : aggregateParts(Made))
        Part(Each.Init, Each.Type);
    }
    // What has data of its own made here has that data as its own, what
    // its borrowed parts lead to the parts' (isCopyAtOwnLevel, Through).
    CObjectRelation Of = unknownOfRelations(Parts);
    if (const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
        Record && isCopyAtOwnLevel(Type, Record->getASTContext()))
      Of.bCopy = Of.Kind != EKind::Unrelated;
    return Of;
  };
  if (IsObject(E))
    return {EKind::SameObject, nullptr};
  // A value that isn't a pointer or an object, like a count,
  // `mp_Items.f_GetLen()
  // + 1`, refers to nothing.
  // Unless it is made of a pointer, `reinterpret_cast<uintptr_t>(&m_Value)`,
  // which a cast can make a pointer again.
  // Also through the right of a comma or either value of a conditional,
  // `(void(0), reinterpret_cast<uintptr_t>(&m_Value))` (madeOfPointer).
  auto MadeOfPointer = [](const Expr *Value, int) {
    return madeOfPointer(Value);
  };
  if (E->isPRValue() && !E->getType().isNull() &&
      !E->getType()->isDependentType() &&
      (E->getType()->isArithmeticType() || E->getType()->isEnumeralType() ||
       E->getType()->isNullPtrType()) &&
      !MadeOfPointer(E, 0))
    return Unrelated();
  // What a lambda called in place gives back, `[this] { return &mp_Value;
  // }()`, of any of its returns.
  if (const CXXMethodDecl *Operator = calledInPlaceLambda(E))
    return LambdaGives(Operator, dyn_cast<CallExpr>(E));
  // The value of the pointer the glvalue \p Pointer designates, as it is read
  // or stepped where it is, `m_pApp`, `++m_pOther` or `m_pOther += 1`: through
  // it when an object holds it, also a variable that isn't local, like a
  // `thread_local` slot, which points apart from where it is.
  TCRecursive<CObjectRelation(const Expr *, int)> LoadedValue =
      [&](const Expr *Pointer, int Depth) -> CObjectRelation {
    const Expr *Bare = Pointer->IgnoreParenImpCasts();
    // Of a pointer member of an unchanged local made by an aggregate,
    // what it is made with, `Pair.m_pGlobal` of `CPair Pair{&t_Value,
    // &g_Value}` (localMemberInit).
    if (const Expr *Made =
            isa<MemberExpr>(Bare) ? localMemberInit(Bare) : nullptr;
        Made && Depth < 16)
      return Relation(Made);
    if (const auto *Conditional =
            dyn_cast<AbstractConditionalOperator>(Bare);
        Conditional && Depth < 16) {
      if (const Expr *Chosen = chosenValue(Conditional, Context))
        return LoadedValue(Chosen, Depth + 1);
      return AnyOfRelations(
          {LoadedValue(Conditional->getTrueExpr(), Depth + 1),
           LoadedValue(Conditional->getFalseExpr(), Depth + 1)});
    }
    if (const auto *Comma = dyn_cast<BinaryOperator>(Bare);
        Comma && Comma->isCommaOp() && Depth < 16)
      return LoadedValue(Comma->getRHS(), Depth + 1);
    // Where it is held can't be told within the budget: what can't be
    // told. A pointer to storage its object owns, `m_pData`, leads to
    // what is the object's own (isOwnedStoragePointer), which is an
    // entry of it, freed apart from it, `f_Clear()`.
    const std::optional<bool> Stored = storedPointerOf(Pointer);
    if (!Stored)
      return Unknown();
    // A pointer variable that isn't local which is the object itself,
    // like a `thread_local` slot, `t_pData`, holds what it points to
    // apart from it: through it, not it.
    // Also through what gives it back as it is, a cast to a reference
    // to it or a helper, `fg_Identity(t_pData)` (returnedArgumentChoices).
    TCRecursive<bool(const Expr *, int)> IsRootSlot =
        [&](const Expr *Slot, int Level) {
          if (!Slot || Level > 8)
            return false;
          Slot = Slot->IgnoreParenImpCasts();
          if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Slot);
              Cast && Cast->isGLValue())
            return IsRootSlot(Cast->getSubExpr(), Level + 1);
          if (const auto *Call = dyn_cast<CallExpr>(Slot);
              Call && Call->isGLValue()) {
            const llvm::SmallVector<const Expr *, 2> Given =
                returnedArgumentChoices(Call);
            if (!Given.empty())
              return llvm::all_of(Given, [&](const Expr *Argument) {
                return IsRootSlot(Argument, Level + 1);
              });
            // Also what a getter whose body can be seen gives, `CData
            // *&fg_Slot() { return t_pData; }`, on each return.
            const FunctionDecl *Definition = calledDefinition(Call);
            if (!Definition || isa<CXXMethodDecl>(Definition) ||
                isa<CoroutineBodyStmt>(Definition->getBody()))
              return false;
            const llvm::SmallVector<const Expr *, 4> Values =
                returnedValues(Definition);
            return !Values.empty() &&
                   llvm::all_of(Values, [&](const Expr *Value) {
                     return IsRootSlot(Value, Level + 1);
                   });
          }
          // Also a static member named through an object,
          // `Object.t_pData`.
          if (const auto *Member = dyn_cast<MemberExpr>(Slot)) {
            const auto *Variable =
                dyn_cast<VarDecl>(Member->getMemberDecl());
            return Variable && !Variable->hasLocalStorage() &&
                   IsObject(Member);
          }
          const auto *Ref = dyn_cast<DeclRefExpr>(Slot);
          const auto *Variable =
              Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          // Also through a local reference to it, `auto &rpData =
          // t_pData`, or what an unchanged local pointer to it points to,
          // `*ppData` of `auto **ppData = &t_pData`.
          if (Variable && Variable->hasLocalStorage() &&
              !isa<ParmVarDecl>(Variable) && Variable->getInit() &&
              Variable->getType()->isReferenceType())
            return IsRootSlot(Variable->getInit(), Level + 1);
          // Also what a pointer to it a getter gives points to,
          // `*Local.f_Get()` of `TCThreadLocal<int *> Local`.
          if (const auto *Deref = dyn_cast<UnaryOperator>(Slot);
              Deref && Deref->getOpcode() == UO_Deref &&
              IsObject(Deref->getSubExpr()->IgnoreParenImpCasts()))
            return true;
          // Also through unchanged copies of such a pointer, `*ppCopy`
          // of `auto **ppCopy = ppData` (addressedExpr).
          if (const auto *Deref = dyn_cast<UnaryOperator>(Slot);
              Deref && Deref->getOpcode() == UO_Deref)
            if (const Expr *Pointed = addressedExpr(Deref->getSubExpr()))
              return IsRootSlot(Pointed, Level + 1);
          return Variable && !Variable->hasLocalStorage() && IsObject(Ref);
        };
    if (IsRootSlot(Bare, 0)) {
      CObjectRelation Of = Relation(Pointer);
      if (Of.Kind == EKind::SameObject)
        Of.Kind = EKind::Subobject;
      return Through(Of);
    }
    if (const auto *Member =
            dyn_cast<MemberExpr>(Pointer->IgnoreParenImpCasts());
        Member &&
        isOwnedStoragePointer(dyn_cast<FieldDecl>(Member->getMemberDecl())))
      return EntryOf(Relation(Pointer));
    return *Stored ? Through(Relation(Pointer)) : Relation(Pointer);
  };

  switch (E->getStmtClass()) {
  // What is the same object: parentheses, casts of any kind, also to an
  // rvalue reference, and the temporaries and cleanups around it.
  case Stmt::ParenExprClass:
  case Stmt::ImplicitCastExprClass:
  case Stmt::CStyleCastExprClass:
  case Stmt::CXXFunctionalCastExprClass:
  case Stmt::CXXStaticCastExprClass:
  case Stmt::CXXConstCastExprClass:
  case Stmt::CXXReinterpretCastExprClass:
  case Stmt::CXXDynamicCastExprClass:
  case Stmt::CXXAddrspaceCastExprClass:
  case Stmt::BuiltinBitCastExprClass:
  case Stmt::ObjCBridgedCastExprClass:
    if (const auto *Paren = dyn_cast<ParenExpr>(E))
      return Relation(Paren->getSubExpr());
    // The value of a pointer the object holds, `m_pApp` read for
    // `m_pApp->m_Value`, points through it; the pointer itself, `&m_pApp`, is
    // part of the object.
    // Of either value of a conditional each by itself, `b ? pLocal :
    // m_pOther`, and of the right of a comma.
    // Also a load as the bits of another type, `__builtin_bit_cast(int *,
    // m_pValue)`.
    if (const auto *Read = dyn_cast<CastExpr>(E);
        Read &&
        (Read->getCastKind() == CK_LValueToRValue ||
         Read->getCastKind() == CK_LValueToRValueBitCast) &&
        Read->getSubExpr()->getType()->isPointerType())
      return LoadedValue(Read->getSubExpr(), 0);
    // A `dynamic_cast` to a pointer that checks, not one to a base, gives
    // null when the object isn't of the class (bCanBeNull).
    if (const auto *Dynamic = dyn_cast<CXXDynamicCastExpr>(E);
        Dynamic && Dynamic->getCastKind() == CK_Dynamic &&
        Dynamic->getType()->isPointerType()) {
      CObjectRelation Of = Relation(Dynamic->getSubExpr());
      Of.bCanBeNull = Related(Of);
      return Of;
    }
    return Relation(cast<CastExpr>(E)->getSubExpr());
  case Stmt::MaterializeTemporaryExprClass:
    return Relation(cast<MaterializeTemporaryExpr>(E)->getSubExpr());
  case Stmt::CXXBindTemporaryExprClass:
    return Relation(cast<CXXBindTemporaryExpr>(E)->getSubExpr());
  case Stmt::ExprWithCleanupsClass:
  case Stmt::ConstantExprClass:
    return Relation(cast<FullExpr>(E)->getSubExpr());
  case Stmt::OpaqueValueExprClass:
    if (const Expr *Source = cast<OpaqueValueExpr>(E)->getSourceExpr())
      return Relation(Source);
    return Unrelated();
  case Stmt::CXXDefaultArgExprClass:
    return Relation(cast<CXXDefaultArgExpr>(E)->getExpr());
  case Stmt::CXXDefaultInitExprClass:
    if (const Expr *Initializer = cast<CXXDefaultInitExpr>(E)->getExpr())
      return Relation(Initializer);
    return Unrelated();
  case Stmt::SubstNonTypeTemplateParmExprClass:
    return Relation(cast<SubstNonTypeTemplateParmExpr>(E)->getReplacement());
  case Stmt::GenericSelectionExprClass:
    if (const auto *Selection = cast<GenericSelectionExpr>(E);
        !Selection->isResultDependent())
      return Relation(Selection->getResultExpr());
    return Generic();
  case Stmt::ChooseExprClass:
    if (const auto *Choose = cast<ChooseExpr>(E);
        !Choose->isConditionDependent())
      return Relation(Choose->getChosenSubExpr());
    return Generic();
  // Its address or what a pointer to it points to is the same object, also a
  // pointer given a unary plus, and what a prefix increment changes; a
  // pointer stepped points to an entry.
  case Stmt::UnaryOperatorClass:
    // The address of a local pointer, `&pValue`, is of the local, not of
    // what it points to.
    if (const auto *Unary = cast<UnaryOperator>(E);
        Unary->getOpcode() == UO_AddrOf &&
        AddressOfLocalPointer(Unary->getSubExpr()))
      return Unrelated();
    // The address of what a pointer assignment or step changes is of that
    // slot, `&(m_pValue = &g_Value)`, not of what it is given.
    if (const auto *Unary = cast<UnaryOperator>(E);
        Unary->getOpcode() == UO_AddrOf &&
        Unary->getSubExpr()->getType()->isPointerType()) {
      const Expr *Changed = Unary->getSubExpr()->IgnoreParens();
      if (const auto *Assign = dyn_cast<BinaryOperator>(Changed);
          Assign && Assign->isAssignmentOp())
        return Relation(Assign->getLHS());
      if (const auto *Step = dyn_cast<UnaryOperator>(Changed);
          Step && Step->isPrefix() && Step->isIncrementDecrementOp())
        return Relation(Step->getSubExpr());
    }
    // What a pointer to a local pointer points to is that pointer, `*ppItems`
    // of `auto ppItems = &pItems`, `*&pItems`; not through a pointer that is
    // asked of itself, the object, whose value is what the caller follows.
    if (const auto *Unary = cast<UnaryOperator>(E);
        Unary->getOpcode() == UO_Deref &&
        !IsObject(Unary->getSubExpr()->IgnoreParenImpCasts()))
      if (const Expr *Addressed = addressedExpr(Unary->getSubExpr()))
        return Relation(Addressed);
    if (const auto *Unary = cast<UnaryOperator>(E);
        Unary->getOpcode() == UO_AddrOf || Unary->getOpcode() == UO_Deref ||
        Unary->getOpcode() == UO_Extension || Unary->getOpcode() == UO_Plus ||
        (Unary->isPrefix() && Unary->isIncrementDecrementOp() &&
         !Unary->getType()->isPointerType()))
      return Relation(Unary->getSubExpr());
    else if (Unary->isIncrementDecrementOp())
      return Stepped(LoadedValue(Unary->getSubExpr(), 0));
    return Generic();
  // The right of a comma, and what pointer arithmetic gives, an entry. What
  // an assignment of a pointer assigns, `(pEntry = &mp_Items[0])`, of
  // anything else what it changes, `(mp_Items[0] = 1)`. A data member
  // through a pointer to it, `this->*pmItems`, is a member of what that is
  // of, not a member function.
  // What `typeid` gives, the information of a type, which lives as long as
  // the program, not what it is given.
  case Stmt::CXXTypeidExprClass:
    return Unrelated();
  case Stmt::BinaryOperatorClass:
    if (const auto *Binary = cast<BinaryOperator>(E); Binary->isCommaOp())
      return Relation(Binary->getRHS());
    else if (Binary->isAdditiveOp() && Binary->getType()->isPointerType())
      return Stepped(Relation(Binary->getLHS()->getType()->isPointerType()
                                  ? Binary->getLHS()
                                  : Binary->getRHS()));
    // A number made of a pointer stepped, `sizeof(int) +
    // reinterpret_cast<uintptr_t>(m_Values)`, as the pointer would be.
    else if (Binary->isAdditiveOp() && MadeOfPointer(Binary, 0))
      return Stepped(Relation(MadeOfPointer(Binary->getLHS(), 0)
                                  ? Binary->getLHS()
                                  : Binary->getRHS()));
    else if (Binary->getOpcode() == BO_Assign)
      return Relation(Binary->getType()->isPointerType() ? Binary->getRHS()
                                                         : Binary->getLHS());
    else if (Binary->isPtrMemOp()) {
      if (Binary->getType()->isSpecificPlaceholderType(
              BuiltinType::BoundMember))
        return Unrelated();
      CObjectRelation Of = Relation(Binary->getLHS());
      if (Of.Kind == EKind::SameObject)
        Of.Kind = EKind::Subobject;
      // A member of a union, `&CEntry::m_Empty`, also when what it points to
      // can't be seen and the class has one.
      if (Related(Of) && pointsIntoUnion(Binary->getRHS(), Binary->getLHS()))
        Of.bInUnion = true;
      return Of;
    }
    return Generic();
  // What a compound assignment changes, `(mp_Value += 1)`, also a pointer
  // stepped, which points to an entry.
  case Stmt::CompoundAssignOperatorClass:
    if (const auto *Compound = cast<CompoundAssignOperator>(E);
        Compound->getType()->isPointerType())
      return Stepped(LoadedValue(Compound->getLHS(), 0));
    else
      return Relation(Compound->getLHS());
  // The value of a statement expression, `({ mp_Items.f_FindEqual(Key); })`.
  case Stmt::StmtExprClass:
    if (const CompoundStmt *Body = cast<StmtExpr>(E)->getSubStmt();
        !Body->body_empty())
      if (const auto *Value = dyn_cast<Expr>(Body->body_back()))
        return Relation(Value);
    return Unrelated();
  // Either value of a conditional: what both are, else unknown, or the other
  // when one is null, `bChoose ? this : nullptr`, which points nowhere.
  case Stmt::ConditionalOperatorClass:
  case Stmt::BinaryConditionalOperatorClass: {
    const auto *Conditional = cast<AbstractConditionalOperator>(E);
    auto IsNull = [](const Expr *Value) {
      const Expr *Bare = Value->IgnoreParenImpCasts();
      return isa<CXXNullPtrLiteralExpr, GNUNullExpr>(Bare) ||
             (isa<IntegerLiteral>(Bare) &&
              cast<IntegerLiteral>(Bare)->getValue().isZero());
    };
    // The one a constant condition chooses, `(t_bUseOwn ? this : m_pOther)`
    // of `f_Clear<true>()`.
    if (const Expr *Chosen = chosenValue(Conditional, Context))
      return Relation(Chosen);
    // The other, which can be null then (bCanBeNull).
    for (const auto &[Null, Other] :
         {std::pair{Conditional->getTrueExpr(), Conditional->getFalseExpr()},
          std::pair{Conditional->getFalseExpr(), Conditional->getTrueExpr()}})
      if (E->getType()->isPointerType() && IsNull(Null)) {
        CObjectRelation Of = Relation(Other);
        Of.bCanBeNull = Related(Of);
        return Of;
      }
    return AnyOf(keptArms(Conditional));
  }
  // A local reference or pointer, what its initializer is; a local iterator,
  // an entry of what it was made from; any other variable another object.
  case Stmt::DeclRefExprClass:
    // A structured binding is the part of what it binds that it names,
    // `auto &[Items] = _Wrapper`.
    if (const auto *Binding =
            dyn_cast<BindingDecl>(cast<DeclRefExpr>(E)->getDecl()))
      return Binding->getBinding() ? Relation(Binding->getBinding())
                                   : Unrelated();
    if (const auto *Ref = cast<DeclRefExpr>(E);
        const auto *Local = dyn_cast<VarDecl>(Ref->getDecl()))
      if ((Local->hasLocalStorage() || Local->isInitCapture()) &&
          !isa<ParmVarDecl>(Local) &&
          (Local->getInit() || Local->getType()->isPointerType() ||
           Local->getType()->isArrayType())) {
        const QualType Type = Local->getType();
        if (Type->isReferenceType() || Type->isPointerType()) {
          // A reference to a local pointer, `auto &rpItems = pItems`, reads
          // the pointer where it is used, which can be pointed elsewhere after
          // the reference is made, `pItems = &mp_Items; rpItems->f_Clear()`.
          const VarDecl *Pointer = Local;
          if (Type->isReferenceType() &&
              Type.getNonReferenceType()->isPointerType() && Local->getInit() &&
              !isCapturedByCopyAt(Ref))
            if (const auto *Aliased = dyn_cast<DeclRefExpr>(
                    Local->getInit()->IgnoreParenImpCasts()))
              if (const auto *Target = dyn_cast<VarDecl>(Aliased->getDecl());
                  Target && Target != Local && Target->hasLocalStorage() &&
                  !isa<ParmVarDecl>(Target) &&
                  Target->getType()->isPointerType() &&
                  (!Target->getInit() || !isUnchangedLocalAt(Ref, Target)))
                Pointer = Target;
          // A pointer assigned again, also one made without a value, or the
          // copy a lambda the use is in captures, points where it was last
          // pointed before the use, when that can be told, else anywhere it
          // is pointed, also by a conditional assignment: what can't be told
          // when any of those is of the object, through a pointer or a copy
          // only when each of them is, and nothing of it when none is, `pItems
          // = &Local; pItems->f_Remove(0)`. Also in ways that aren't seen, like
          // by inline assembly or as either of a conditional, so never the
          // object itself.
          if (Pointer->getType()->isPointerType() &&
              (Pointer != Local || !Pointer->getInit() ||
               !isUnchangedLocalAt(Ref, Pointer))) {
            const VarDecl *Local = Pointer;
            // What it holds there (localValuesAt), else anything it is
            // made with or assigned.
            llvm::SmallVector<const Expr *, 4> Values;
            const std::optional<llvm::SmallVector<const Expr *, 4>> Reaching =
                localValuesAt(Local, Ref);
            if (Reaching && !Reaching->empty())
              llvm::append_range(Values, *Reaching);
            else
              llvm::append_range(Values, assignedValuesAt(Local, Ref));
            if (Values.empty()) {
              if (Local->getInit())
                Values.push_back(Local->getInit());
              if (const auto *Function = dyn_cast_or_null<FunctionDecl>(
                      Local->getParentFunctionOrMethod());
                  Function && Function->getBody()) {
                forEachStmtOnce(Function->getBody(), [&](const Stmt *S) {
                  if (const auto *Assign = dyn_cast<BinaryOperator>(S);
                      Assign && Assign->getOpcode() == BO_Assign)
                    if (const auto *Target = dyn_cast<DeclRefExpr>(
                            Assign->getLHS()->IgnoreParenImpCasts());
                        Target && Target->getDecl() == Local)
                      Values.push_back(Assign->getRHS());
                  return EWalk::Descend;
                });
              }
            }
            // Also what a setter it is given to sets it to, `fg_Set(pItems,
            // _Items)` of `o_pItems = &_Items` (setParameters): what it is
            // given for that.
            // Found once for each local, which each use asks again.
            if (const auto *Function = dyn_cast_or_null<FunctionDecl>(
                    Local->getParentFunctionOrMethod());
                Function && Function->getBody()) {
              llvm::append_range(Values,
                                 astCache<CLocalWrites>(Local->getASTContext())
                                     .Set.get(Local));
            }
            llvm::SmallVector<CObjectRelation, 4> Held;
            for (const Expr *Value : Values)
              Held.push_back(Relation(Value));
            return unknownOfRelations(Held);
          }
          if (!Local->getInit())
            return Unrelated();
          // The copy a lambda captured of what a reference refers to, `[State]`
          // of `auto &State = mp_State`, is an object of its own when it is
          // independent (isIndependentType), a copy at its own level when it
          // holds data of its own (isCopyAtOwnLevel), whose pointers, also of a
          // shared owner, `[pItems]` of `auto &pItems = mp_pItems`, point
          // where the source's do, and what it refers to when it only refers.
          // A copy of a pointer a reference refers to, `[pItems]` of `CItems
          // *const &pItems = &mp_Items`, points where that does.
          if (Type->isReferenceType() && isCapturedByCopyAt(Ref)) {
            if (!Type.getNonReferenceType()->isPointerType()) {
              if (isIndependentType(Type.getNonReferenceType(),
                                    Local->getASTContext()))
                return Unrelated();
              CObjectRelation Of = Relation(Local->getInit());
              if (isCopyAtOwnLevel(Type.getNonReferenceType(),
                                   Local->getASTContext()))
                Of.bCopy = Of.Kind != EKind::Unrelated;
              return Of;
            }
            const CObjectRelation Of = Relation(Local->getInit());
            return isStoredPointer(Local->getInit()) ? Through(Of) : Of;
          }
          CObjectRelation Of = Relation(Local->getInit());
          if (Related(Of) && Context &&
              isChangedInCapturingLambdas(Ref, Local, *Context))
            return Unknown();
          // What a copy of a shared owner keeps alive isn't once a local
          // pointer or reference holds it, which can outlive the copy,
          // `pData = &*pKeep` of a `pKeep` that goes before (bKeptAlive).
          Of.bKeptAlive = false;
          return Of;
        }
        if (isIteratorType(Type.getNonReferenceType())) {
          // Of one assigned again, what it was last assigned before the use,
          // when that can be told, else anything.
          auto Origin = [&](const Expr *Value) {
            Value = Value->IgnoreImplicit();
            if (const auto *Copy = dyn_cast<CXXConstructExpr>(Value);
                Copy && Copy->getNumArgs() == 1)
              Value = Copy->getArg(0)->IgnoreImplicit();
            return Relation(Value);
          };
          if (isUnchangedLocalAt(Ref, Local))
            return Origin(Local->getInit());
          const llvm::SmallVector<const Expr *, 2> Values =
              assignedValuesAt(Local, Ref);
          if (Values.empty()) {
            // Only stepped, `++iEntry`, it stays in what it was made from.
            bool Assigned = false;
            if (const auto *Function = dyn_cast_or_null<FunctionDecl>(
                    Local->getParentFunctionOrMethod());
                Function && Function->getBody()) {
              forEachStmtOnce(Function->getBody(), [&](const Stmt *S) {
                const Expr *Target = nullptr;
                if (const auto *Assign = dyn_cast<BinaryOperator>(S);
                    Assign && Assign->isAssignmentOp())
                  Target = Assign->getLHS();
                else if (const auto *Operator =
                             dyn_cast<CXXOperatorCallExpr>(S);
                         Operator && Operator->isAssignmentOp() &&
                         Operator->getNumArgs() >= 1)
                  Target = Operator->getArg(0);
                if (const auto *Named = Target
                                            ? dyn_cast<DeclRefExpr>(
                                                  Target->IgnoreParenImpCasts())
                                            : nullptr;
                    Named && Named->getDecl() == Local)
                  Assigned = true;
                return Assigned ? EWalk::Stop : EWalk::Descend;
              });
            }
            return Assigned ? Unknown() : Origin(Local->getInit());
          }
          llvm::SmallVector<CObjectRelation, 2> Of;
          for (const Expr *Value : Values)
            Of.push_back(Origin(Value));
          return AnyOfRelations(Of);
        }
        // A local view, guard or array of pointers, of a type that can refer
        // back to what it is made from (canReferBack), not a closure, nor a
        // collection or what owns what it holds, which copies what it is
        // given: what it is made of (MadeOf), and what it or a part of it is
        // given later where that runs, `View.m_pItems = &_Items` or `Batch[0]
        // = &_Items`, also by an assignment operator, as what can't be told.
        if (const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
            !(Record && Record->isLambda()) && !Type->isReferenceType() &&
            (Type->isRecordType() || Type->isArrayType()) &&
            CanReferBack(Type) &&
            !isIndependentType(Type, Local->getASTContext())) {
          // Found once for each local, which each use asks again.
          const llvm::SmallVector<const Expr *, 4> &Values =
              astCache<CLocalWrites>(Local->getASTContext()).Parts.get(Local);
          if (Values.empty())
            return Local->getInit() ? Relation(Local->getInit()) : Unrelated();
          llvm::SmallVector<CObjectRelation, 4> Held;
          if (Local->getInit())
            Held.push_back(Relation(Local->getInit()));
          for (const Expr *Value : Values)
            Held.push_back(Relation(Value));
          // What has data of its own has that data as its own, what its
          // borrowed parts are given leads to theirs (isCopyAtOwnLevel), as
          // when it is made of them (MadeOf).
          CObjectRelation Of = unknownOfRelations(Held);
          if (Record && isCopyAtOwnLevel(Type, Local->getASTContext()))
            Of.bCopy = Of.Kind != EObjectRelation::Unrelated;
          return Of;
        }
      }
    return Unrelated();
  case Stmt::CXXThisExprClass:
    return Unrelated();
  // A member of it, a subobject, the outermost the one on the object.
  case Stmt::MemberExprClass:
    if (const auto *Member = cast<MemberExpr>(E);
        isa<FieldDecl, IndirectFieldDecl>(Member->getMemberDecl())) {
      CObjectRelation Of = Relation(Member->getBase());
      if (Of.Kind == EKind::SameObject) {
        Of.Kind = EKind::Subobject;
        Of.Outermost = Member;
      }
      // In a member of a union, also further in it.
      if (const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
          Related(Of) && Field && Field->getParent()->isUnion())
        Of.bInUnion = true;
      // What a reference member refers to can be another object.
      if (Member->getMemberDecl()->getType()->isReferenceType())
        return Through(Of);
      return Of;
    }
    return Unrelated();
  // An element of an array the object holds, `mp_Values[0]`, is a subobject,
  // like a member.
  case Stmt::ArraySubscriptExprClass:
    if (const Expr *Base = cast<ArraySubscriptExpr>(E)->getBase();
        Base->IgnoreParenImpCasts()->getType()->isConstantArrayType()) {
      CObjectRelation Of = Relation(Base);
      if (Of.Kind == EKind::SameObject)
        Of.Kind = EKind::Subobject;
      return Of;
    } else
      return Stepped(Relation(Base));
  // Of a member operator, also one with an explicit object parameter, its
  // first argument: a subscript gives an entry, `->` and `*` the same as
  // what they are called on, a smart pointer to it or an iterator into it,
  // others what a call gives.
  case Stmt::CXXOperatorCallExprClass:
    if (const auto *Operator = cast<CXXOperatorCallExpr>(E);
        Operator->getNumArgs() >= 1) {
      const auto *Method =
          dyn_cast_or_null<CXXMethodDecl>(Operator->getDirectCallee());
      if (Method && !Method->isStatic()) {
        if (ReceiverFree(Operator))
          return FreeGives(Operator);
        // One whose body can be seen gives what that tells, the same as a
        // member function, `operator*` returning `*this` (BodyGives).
        if (const std::optional<CObjectRelation> Body =
                BodyGives(Method, Operator, Operator->getArg(0)))
          return *Body;
        // One that gives the object itself (givesItsObject), before what
        // its kind tells, `CItems &operator[](int)` without a body.
        if (givesItsObject(Method)) {
          const CObjectRelation Of = Relation(Operator->getArg(0));
          if (!callsOverride(Operator))
            return Of;
          return Related(Of) ? Unknown() : Unrelated();
        }
        if (const std::optional<CObjectRelation> Gave =
                OperatorGives(Method, Operator, Operator->getArg(0)))
          return *Gave;
        if (Operator->getOperator() == OO_Call && calledLambda(Operator))
          return LambdaGives(Method, Operator);
        // A call operator, like `m_Map(Key)` of a `TCMap`, gives what is in
        // it like a member function; another operator a new object by value,
        // `mp_Items + Other`, a copy unless it can refer back to it, like a
        // view (CopyOf), which is what is in it, the same as named.
        if (Operator->getOperator() == OO_Call || Operator->isGLValue() ||
            Operator->getType()->isPointerType() ||
            isIteratorType(Operator->getType()) ||
            Operator->getType()->isRecordType())
          return Gives(Operator, Operator->getArg(0));
        return Unrelated();
      }
      return FreeGives(Operator);
    }
    return Generic();
  // Of a member function whose body can be seen that gives nothing of its
  // object (returnedRelation), what it gives of what it is given or of
  // anything else, like a free function, `f_Identity(_pValue)` or `return
  // &fg_ConcurrencyThreadLocal().m_Value;` (FreeGives).
  // Also with an explicit object parameter, and a call operator of what
  // isn't a lambda (ReceiverFree).
  case Stmt::CXXMemberCallExprClass:
    if (ReceiverFree(cast<CallExpr>(E)))
      return FreeGives(cast<CallExpr>(E));
    return Gives(cast<CallExpr>(E),
                 memberCallOf(E)->getImplicitObjectArgument());
  // Moved or forwarded, the same; of a member function with an explicit
  // object parameter, its first argument is its object.
  case Stmt::CallExprClass:
  case Stmt::CUDAKernelCallExprClass:
  case Stmt::UserDefinedLiteralClass:
    if (const auto *Call = cast<CallExpr>(E); givesItsArgument(Call))
      return isAddressOfCall(Call) && AddressOfLocalPointer(Call->getArg(0))
                 ? Unrelated()
                 : Relation(Call->getArg(0));
    else if (const auto *Method =
                 dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
             Method && Method->isExplicitObjectMemberFunction() &&
             Call->getNumArgs() >= 1)
      return ReceiverFree(Call) ? FreeGives(Call)
                                : Gives(Call, Call->getArg(0));
    else
      return FreeGives(Call);
  // What an await gives by value is a value of its own, made where what is
  // awaited runs, not what the operand is made of, also an iterator,
  // `co_await fg_Move(Generator).f_GetPipelinedIterator()`; by pointer or
  // reference, or a view that only refers, `co_await f_RangeAsync()` of a
  // `TCFuture<std::span<CEntry>>` (onlyRefers), what can't be told when it is
  // made of the object.
  case Stmt::DependentCoawaitExprClass:
  case Stmt::CoyieldExprClass:
  case Stmt::CoawaitExprClass:
    if (E->isPRValue() && !E->getType()->isPointerType()) {
      const CXXRecordDecl *Record = E->getType()->getAsCXXRecordDecl();
      if (!Record || isIteratorType(E->getType()) ||
          !(isDefaultViewType(E->getType(), Record->getASTContext()) ||
            onlyRefers(E->getType(), Record->getASTContext())))
        return Unrelated();
    }
    return Generic();
  // What makes another object, which can keep what it is made of when it can
  // refer back to it, like a view or a guard, `CView{&_Items}` or
  // `TCReference<CItems>(_Items)` (MadeOf).
  case Stmt::CXXConstructExprClass:
  case Stmt::CXXTemporaryObjectExprClass:
  case Stmt::InitListExprClass:
  case Stmt::CXXParenListInitExprClass:
    // Not braces that bind a reference, `CEntry &Entry{mp_Items[0]}`, which
    // are what they are given.
    if (const auto *List = dyn_cast<InitListExpr>(E);
        List && List->isGLValue() && List->getNumInits() == 1)
      return Relation(List->getInit(0));
    return MadeOf(E);
  // What `new` makes is apart from the object, and so is what it gives
  // that, but what it is made of, `new CView{&t_Value}`, can lead into it.
  case Stmt::CXXNewExprClass:
    if (const Expr *Init = cast<CXXNewExpr>(E)->getInitializer();
        Init && !isa<CXXDefaultArgExpr>(Init))
      return Relation(Init);
    return Unrelated();
  case Stmt::LambdaExprClass:
  case Stmt::CompoundLiteralExprClass:
  case Stmt::CXXScalarValueInitExprClass:
  case Stmt::ImplicitValueInitExprClass:
    return Unrelated();
  // Any other expression: unknown when it is made of the object.
  case Stmt::VAArgExprClass:
  case Stmt::UnaryExprOrTypeTraitExprClass:
  case Stmt::TypeTraitExprClass:
  case Stmt::SubstNonTypeTemplateParmPackExprClass:
  case Stmt::StringLiteralClass:
  case Stmt::SourceLocExprClass:
  case Stmt::SizeOfPackExprClass:
  case Stmt::ShuffleVectorExprClass:
  case Stmt::SYCLUniqueStableNameExprClass:
  case Stmt::RequiresExprClass:
  case Stmt::RecoveryExprClass:
  case Stmt::PseudoObjectExprClass:
  case Stmt::PredefinedExprClass:
  case Stmt::ParenListExprClass:
  case Stmt::PackIndexingExprClass:
  case Stmt::PackExpansionExprClass:
  case Stmt::UnresolvedMemberExprClass:
  case Stmt::UnresolvedLookupExprClass:
  case Stmt::OpenACCAsteriskSizeExprClass:
  case Stmt::OffsetOfExprClass:
  case Stmt::ObjCSubscriptRefExprClass:
  case Stmt::ObjCSelectorExprClass:
  case Stmt::ObjCProtocolExprClass:
  case Stmt::ObjCPropertyRefExprClass:
  case Stmt::ObjCStringLiteralClass:
  case Stmt::ObjCDictionaryLiteralClass:
  case Stmt::ObjCBoxedExprClass:
  case Stmt::ObjCArrayLiteralClass:
  case Stmt::ObjCMessageExprClass:
  case Stmt::ObjCIvarRefExprClass:
  case Stmt::ObjCIsaExprClass:
  case Stmt::ObjCIndirectCopyRestoreExprClass:
  case Stmt::ObjCEncodeExprClass:
  case Stmt::ObjCBoolLiteralExprClass:
  case Stmt::ObjCAvailabilityCheckExprClass:
  case Stmt::OMPIteratorExprClass:
  case Stmt::OMPArrayShapingExprClass:
  case Stmt::NoInitExprClass:
  case Stmt::MatrixSubscriptExprClass:
  case Stmt::MatrixSingleSubscriptExprClass:
  case Stmt::MatrixElementExprClass:
  case Stmt::MSPropertySubscriptExprClass:
  case Stmt::MSPropertyRefExprClass:
  case Stmt::IntegerLiteralClass:
  case Stmt::ImaginaryLiteralClass:
  case Stmt::HLSLOutArgExprClass:
  case Stmt::GNUNullExprClass:
  case Stmt::FunctionParmPackExprClass:
  case Stmt::FloatingLiteralClass:
  case Stmt::FixedPointLiteralClass:
  case Stmt::ExtVectorElementExprClass:
  case Stmt::ExpressionTraitExprClass:
  case Stmt::EmbedExprClass:
  case Stmt::DesignatedInitUpdateExprClass:
  case Stmt::DesignatedInitExprClass:
  case Stmt::DependentScopeDeclRefExprClass:
  case Stmt::ConvertVectorExprClass:
  case Stmt::ConceptSpecializationExprClass:
  case Stmt::CharacterLiteralClass:
  case Stmt::CXXUuidofExprClass:
  case Stmt::CXXUnresolvedConstructExprClass:
  case Stmt::CXXThrowExprClass:
  case Stmt::CXXStdInitializerListExprClass:
  case Stmt::CXXRewrittenBinaryOperatorClass:
  case Stmt::CXXReflectExprClass:
  case Stmt::CXXPseudoDestructorExprClass:
  case Stmt::CXXNullPtrLiteralExprClass:
  case Stmt::CXXNoexceptExprClass:
  case Stmt::CXXInheritedCtorInitExprClass:
  case Stmt::CXXFoldExprClass:
  case Stmt::CXXExpansionSelectExprClass:
  case Stmt::CXXDependentScopeMemberExprClass:
  case Stmt::CXXDeleteExprClass:
  case Stmt::CXXBoolLiteralExprClass:
  case Stmt::BlockExprClass:
  case Stmt::AtomicExprClass:
  case Stmt::AsTypeExprClass:
  case Stmt::ArrayTypeTraitExprClass:
  case Stmt::ArraySectionExprClass:
  case Stmt::ArrayInitLoopExprClass:
  case Stmt::ArrayInitIndexExprClass:
  case Stmt::AddrLabelExprClass:
    return Generic();
  // Not an expression.
  case Stmt::NoStmtClass:
  case Stmt::WhileStmtClass:
  case Stmt::LabelStmtClass:
  case Stmt::AttributedStmtClass:
  case Stmt::UnresolvedSYCLKernelCallStmtClass:
  case Stmt::SwitchStmtClass:
  case Stmt::DefaultStmtClass:
  case Stmt::CaseStmtClass:
  case Stmt::SYCLKernelCallStmtClass:
  case Stmt::SEHTryStmtClass:
  case Stmt::SEHLeaveStmtClass:
  case Stmt::SEHFinallyStmtClass:
  case Stmt::SEHExceptStmtClass:
  case Stmt::ReturnStmtClass:
  case Stmt::OpenACCWaitConstructClass:
  case Stmt::OpenACCUpdateConstructClass:
  case Stmt::OpenACCShutdownConstructClass:
  case Stmt::OpenACCSetConstructClass:
  case Stmt::OpenACCInitConstructClass:
  case Stmt::OpenACCExitDataConstructClass:
  case Stmt::OpenACCEnterDataConstructClass:
  case Stmt::OpenACCCacheConstructClass:
  case Stmt::OpenACCLoopConstructClass:
  case Stmt::OpenACCHostDataConstructClass:
  case Stmt::OpenACCDataConstructClass:
  case Stmt::OpenACCComputeConstructClass:
  case Stmt::OpenACCCombinedConstructClass:
  case Stmt::OpenACCAtomicConstructClass:
  case Stmt::ObjCForCollectionStmtClass:
  case Stmt::ObjCAutoreleasePoolStmtClass:
  case Stmt::ObjCAtTryStmtClass:
  case Stmt::ObjCAtThrowStmtClass:
  case Stmt::ObjCAtSynchronizedStmtClass:
  case Stmt::ObjCAtFinallyStmtClass:
  case Stmt::ObjCAtCatchStmtClass:
  case Stmt::OMPTeamsDirectiveClass:
  case Stmt::OMPTaskyieldDirectiveClass:
  case Stmt::OMPTaskwaitDirectiveClass:
  case Stmt::OMPTaskgroupDirectiveClass:
  case Stmt::OMPTaskDirectiveClass:
  case Stmt::OMPTargetUpdateDirectiveClass:
  case Stmt::OMPTargetTeamsDirectiveClass:
  case Stmt::OMPTargetParallelForDirectiveClass:
  case Stmt::OMPTargetParallelDirectiveClass:
  case Stmt::OMPTargetExitDataDirectiveClass:
  case Stmt::OMPTargetEnterDataDirectiveClass:
  case Stmt::OMPTargetDirectiveClass:
  case Stmt::OMPTargetDataDirectiveClass:
  case Stmt::OMPSingleDirectiveClass:
  case Stmt::OMPSectionsDirectiveClass:
  case Stmt::OMPSectionDirectiveClass:
  case Stmt::OMPScopeDirectiveClass:
  case Stmt::OMPScanDirectiveClass:
  case Stmt::OMPParallelSectionsDirectiveClass:
  case Stmt::OMPParallelMasterDirectiveClass:
  case Stmt::OMPParallelMaskedDirectiveClass:
  case Stmt::OMPParallelDirectiveClass:
  case Stmt::OMPOrderedDirectiveClass:
  case Stmt::OMPMetaDirectiveClass:
  case Stmt::OMPMasterDirectiveClass:
  case Stmt::OMPMaskedDirectiveClass:
  case Stmt::OMPTeamsGenericLoopDirectiveClass:
  case Stmt::OMPTeamsDistributeSimdDirectiveClass:
  case Stmt::OMPTeamsDistributeParallelForSimdDirectiveClass:
  case Stmt::OMPTeamsDistributeParallelForDirectiveClass:
  case Stmt::OMPTeamsDistributeDirectiveClass:
  case Stmt::OMPTaskLoopSimdDirectiveClass:
  case Stmt::OMPTaskLoopDirectiveClass:
  case Stmt::OMPTargetTeamsGenericLoopDirectiveClass:
  case Stmt::OMPTargetTeamsDistributeSimdDirectiveClass:
  case Stmt::OMPTargetTeamsDistributeParallelForSimdDirectiveClass:
  case Stmt::OMPTargetTeamsDistributeParallelForDirectiveClass:
  case Stmt::OMPTargetTeamsDistributeDirectiveClass:
  case Stmt::OMPTargetSimdDirectiveClass:
  case Stmt::OMPTargetParallelGenericLoopDirectiveClass:
  case Stmt::OMPTargetParallelForSimdDirectiveClass:
  case Stmt::OMPSimdDirectiveClass:
  case Stmt::OMPParallelMasterTaskLoopSimdDirectiveClass:
  case Stmt::OMPParallelMasterTaskLoopDirectiveClass:
  case Stmt::OMPParallelMaskedTaskLoopSimdDirectiveClass:
  case Stmt::OMPParallelMaskedTaskLoopDirectiveClass:
  case Stmt::OMPParallelGenericLoopDirectiveClass:
  case Stmt::OMPParallelForSimdDirectiveClass:
  case Stmt::OMPParallelForDirectiveClass:
  case Stmt::OMPMasterTaskLoopSimdDirectiveClass:
  case Stmt::OMPMasterTaskLoopDirectiveClass:
  case Stmt::OMPMaskedTaskLoopSimdDirectiveClass:
  case Stmt::OMPMaskedTaskLoopDirectiveClass:
  case Stmt::OMPGenericLoopDirectiveClass:
  case Stmt::OMPForSimdDirectiveClass:
  case Stmt::OMPForDirectiveClass:
  case Stmt::OMPDistributeSimdDirectiveClass:
  case Stmt::OMPDistributeParallelForSimdDirectiveClass:
  case Stmt::OMPDistributeParallelForDirectiveClass:
  case Stmt::OMPDistributeDirectiveClass:
  case Stmt::OMPUnrollDirectiveClass:
  case Stmt::OMPTileDirectiveClass:
  case Stmt::OMPStripeDirectiveClass:
  case Stmt::OMPSplitDirectiveClass:
  case Stmt::OMPReverseDirectiveClass:
  case Stmt::OMPInterchangeDirectiveClass:
  case Stmt::OMPInteropDirectiveClass:
  case Stmt::OMPFlushDirectiveClass:
  case Stmt::OMPErrorDirectiveClass:
  case Stmt::OMPDispatchDirectiveClass:
  case Stmt::OMPDepobjDirectiveClass:
  case Stmt::OMPCriticalDirectiveClass:
  case Stmt::OMPFuseDirectiveClass:
  case Stmt::OMPCancellationPointDirectiveClass:
  case Stmt::OMPCancelDirectiveClass:
  case Stmt::OMPBarrierDirectiveClass:
  case Stmt::OMPAtomicDirectiveClass:
  case Stmt::OMPAssumeDirectiveClass:
  case Stmt::OMPCanonicalLoopClass:
  case Stmt::NullStmtClass:
  case Stmt::MSDependentExistsStmtClass:
  case Stmt::ContinueStmtClass:
  case Stmt::BreakStmtClass:
  case Stmt::IndirectGotoStmtClass:
  case Stmt::IfStmtClass:
  case Stmt::GotoStmtClass:
  case Stmt::ForStmtClass:
  case Stmt::DoStmtClass:
  case Stmt::DeferStmtClass:
  case Stmt::DeclStmtClass:
  case Stmt::CoroutineBodyStmtClass:
  case Stmt::CoreturnStmtClass:
  case Stmt::CompoundStmtClass:
  case Stmt::CapturedStmtClass:
  case Stmt::CXXTryStmtClass:
  case Stmt::CXXForRangeStmtClass:
  case Stmt::CXXExpansionStmtPatternClass:
  case Stmt::CXXExpansionStmtInstantiationClass:
  case Stmt::CXXCatchStmtClass:
  case Stmt::MSAsmStmtClass:
  case Stmt::GCCAsmStmtClass:
    return Unknown();
  }
  return Unknown();
}

CObjectRelation objectRelation(const Expr *E,
                               llvm::function_ref<bool(const Expr *)> IsObject,
                               ASTContext *Context) {
  // Each expression once, also one that several ways lead to, like the
  // aliases of a chain of conditionals of the one before, within a budget;
  // while it is told, a way back to it tells nothing.
  int Steps = 0;
  llvm::DenseMap<const Expr *, CObjectRelation> Told;
  TCRecursive<CObjectRelation(const Expr *)> Relation =
      [&](const Expr *Of) -> CObjectRelation {
    if (!Of)
      return {EObjectRelation::Unrelated, nullptr};
    if (const auto It = Told.find(Of); It != Told.end())
      return It->second;
    if (++Steps > 4096)
      return {EObjectRelation::Unknown, nullptr};
    Told[Of] = {EObjectRelation::Unrelated, nullptr};
    const CObjectRelation Result =
        objectRelationStep(Of, IsObject, Relation, Context);
    Told[Of] = Result;
    return Result;
  };
  return Relation(E);
}

/// Of callsOwnMethod: a method, and the patterns of the names it looks for.
using COwnCallsKey =
    std::tuple<const CXXMethodDecl *, const StringRef *, size_t>;

static bool callsOwnMethodUncached(const COwnCallsKey &Key) {
  const CXXMethodDecl *const Method = std::get<0>(Key);
  const llvm::ArrayRef<StringRef> Patterns(std::get<1>(Key), std::get<2>(Key));
  if (Method->getIdentifier() &&
      matchesNamePattern(identifierTextOf(Method), Patterns))
    return true;
  const FunctionDecl *Definition = Method->getDefinition();
  if (!Definition || !Definition->getBody())
    return false;
  // Its object, `this`, or the explicit object parameter, `this TCVector
  // &_Self`.
  const ParmVarDecl *Self = Method->isExplicitObjectMemberFunction() &&
                                    Definition->getNumParams() >= 1
                                ? Definition->getParamDecl(0)
                                : nullptr;
  auto IsSelf = [&](const Expr *Object) {
    return objectRelation(
               Object,
               [&](const Expr *E) {
                 if (isa<CXXThisExpr>(E))
                   return true;
                 const auto *Ref = dyn_cast<DeclRefExpr>(E);
                 return Self && Ref && Ref->getDecl() == Self;
               },
               &Definition->getASTContext())
               .Kind == EObjectRelation::SameObject;
  };
  TCRecursive<bool(const Stmt *)> Calls = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return false;
    if (const CMemberCall Inner = memberCallOf(S);
        Inner && Inner->getMethodDecl() && Inner->getImplicitObjectArgument() &&
        IsSelf(Inner->getImplicitObjectArgument()) &&
        !hasConstObject(Inner->getMethodDecl()) &&
        callsOwnMethod(Inner->getMethodDecl(), Patterns))
      return true;
    // Not in what never runs, `if constexpr (false) f_Clear();`.
    return llvm::any_of(evaluatedChildren(S, Definition->getASTContext()),
                        Calls);
  };
  return Calls(Definition->getBody());
}

bool callsOwnMethod(const CXXMethodDecl *Method,
                    llvm::ArrayRef<StringRef> Patterns) {
  // Each method once for the AST and the patterns, also helpers that call
  // each other many times; one that calls itself back adds nothing while it
  // is told, and one whose recursion doesn't settle may.
  struct CCalls {
    TCSummaries<COwnCallsKey, bool> Decided{&callsOwnMethodUncached, false,
                                            true};
  };
  return astCache<CCalls>(Method->getASTContext())
      .Decided.get({Method, Patterns.data(), Patterns.size()});
}

/// Whether the local function wrapper \p Variable is never given another
/// function: only called, `fRead()`, read by its const members, copied, or
/// moved from, `fg_Move(fRead)`, which leaves it nothing to call, not
/// assigned, `fRead = ...`, nor given to what can change it, `fg_Swap(fRead,
/// fOther)`.
static bool isUnchangedWrapper(const VarDecl *Variable) {
  if (stackNearlyExhausted())
    return withStack([&] { return isUnchangedWrapper(Variable); });
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
    bool visitCall(CallExpr *Call) {
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
    bool visitMemberCall(CMemberCall Call) {
      if (const CXXMethodDecl *Method = Call->getMethodDecl();
          Method &&
          (hasConstObject(Method) || Method->getOverloadedOperator() == OO_Call))
        allow(Call->getImplicitObjectArgument());
      return true;
    }
    // Each call, also as a member call in each of its forms (CMemberCall).
    bool VisitCallExpr(CallExpr *Call) {
      if (!visitCall(Call))
        return false;
      const CMemberCall Member = memberCallOf(Call);
      return !Member || visitMemberCall(Member);
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
  if (const CMemberCall Member = memberCallOf(Call);
      Member && Member->getMethodDecl() &&
      Member->getMethodDecl()->getOverloadedOperator() == OO_Call)
    return asLambda(Member->getImplicitObjectArgument());
  // Also `std::invoke(fRead)`, which calls it right away.
  if (isStdInvoke(Call) && Call->getNumArgs() >= 1)
    return asLambda(Call->getArg(0));
  return nullptr;
}

static bool isChangedInCapturingLambdas(const DeclRefExpr *Ref,
                                        const VarDecl *Variable,
                                        ASTContext &Context);

/// Whether \p E names the explicit object parameter, taken by reference, of
/// the member function it is in, not of a lambda, `_This` of `void
/// f_Run(this CApp &_This)`: the object it is called on, as `this` is.
static bool namesExplicitObject(const Expr *E) {
  const auto *Ref = dyn_cast<DeclRefExpr>(E);
  const auto *Parameter = Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl()) : nullptr;
  const auto *Method =
      Parameter ? dyn_cast<CXXMethodDecl>(Parameter->getDeclContext())
                : nullptr;
  return Method && !Method->getParent()->isLambda() &&
         Parameter == explicitObjectOf(Method) &&
         Parameter->getType()->isReferenceType();
}

/// Whether \p E is `this` or names the explicit object (namesExplicitObject).
static bool namesThis(const Expr *E) {
  return isa<CXXThisExpr>(E) || namesExplicitObject(E);
}

/// Whether \p E is what names the own object: `this`, also an explicit
/// object, or a member of it that refers to its owner, which Malterlib names
/// after `this`, like `m_pThis`, and with \p Parameters, a parameter given
/// the object of the current actor, `_pThis`, whose copy no lambda the use is
/// in changes.
static bool namesOwnObject(const Expr *E, bool Parameters,
                           ASTContext *Context) {
  if (namesThis(E))
    return true;
  if (const auto *Member = dyn_cast<MemberExpr>(E)) {
    const ValueDecl *Decl = Member->getMemberDecl();
    return namesThis(Member->getBase()->IgnoreParenImpCasts()) &&
           Decl->getIdentifier() && identifierTextOf(Decl).ends_with("This") &&
           (Decl->getType()->isReferenceType() ||
            Decl->getType()->isPointerType());
  }
  const auto *Ref = dyn_cast<DeclRefExpr>(E);
  const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  return Parameters && Variable && isActorObjectParameter(Variable) &&
         !(Context && isChangedInCapturingLambdas(Ref, Variable, *Context));
}

// Not the own storage of a copy of the object, `auto Copy = *this`
// (isCopyStorage).
bool refersToOwnObject(const Expr *E, ASTContext *Context) {
  const CObjectRelation Of = objectRelation(
      E,
      [&](const Expr *Of) {
        return namesOwnObject(Of, /*Parameters=*/false, Context);
      },
      Context);
  return Of.Kind == EObjectRelation::SameObject && !isCopyStorage(Of);
}

bool refersToCurrentActorObject(const Expr *E, ASTContext *Context) {
  const CObjectRelation Of = objectRelation(
      E,
      [&](const Expr *Of) {
        return namesOwnObject(Of, /*Parameters=*/true, Context);
      },
      Context);
  return Of.Kind == EObjectRelation::SameObject && !isCopyStorage(Of);
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
                                  unsigned iParameter);
bool onlyConstructsOrDestroysUncached(
    const std::pair<const FunctionDecl *, unsigned> &Key);

/// What an expression of the uses of an object a function is given is, of
/// those managesObject follows.
enum class EManaged {
  /// The storage of a pointer to it, a name of the parameter or a local.
  Slot,
  /// A pointer to it, as a value.
  Pointer,
  /// It, as a glvalue.
  Object,
};

/// Whether each use of \p E, of \p Kind, in \p Index, only constructs,
/// destroys or compares the object: a pointer to it compared, tested or
/// converted to a number (EUse::Value, EUse::Condition), a pointer stored in
/// its own slot changed, deleted, its destructor called, given to a
/// constructor or a function that does only that with it
/// (onlyConstructsOrDestroysImpl), to one that gives it back by what is done
/// with the result, kept in an aggregate or on the heap, or copied into a local
/// pointer or bound to a local reference, added to \p Aliases by what its names
/// are, whose uses are told the same way, or captured by a lambda, a pointer to
/// it copied or it or the slot of a pointer to it by reference, added to \p
/// Captures by what the names of the capture in its body are, whose uses are
/// told the same way there; not what is unevaluated or discarded. Not a load of
/// it, a member of it used, nor a pointer to it returned or stored. Each use by
/// the effect table (effectsOf).
bool managesObject(
    const Expr *E, EManaged Kind, const CEffectIndex &Index,
    llvm::SmallVectorImpl<std::pair<const VarDecl *, EManaged>> &Aliases,
    llvm::SmallVectorImpl<
        std::tuple<const LambdaExpr *, const VarDecl *, EManaged>> &Captures,
    ASTContext &Context) {
  if (stackNearlyExhausted())
    return withStack([&] {
      return managesObject(E, Kind, Index, Aliases, Captures, Context);
    });
  for (const CEffectIndex::CUse &Use : Index.usesOf(E)) {
    const auto *Parent = dyn_cast<Expr>(Use.Parent);
    const COperandEffect &Effect = *Use.Effect;
    auto Then = [&](EManaged Next) {
      return Parent &&
             managesObject(Parent, Next, Index, Aliases, Captures, Context);
    };
    // Its destructor called, `_pObject->~CType()`: the member and the call
    // of it, whose object it is.
    if (Parent &&
        (isa_and_nonnull<CXXDestructorDecl>(memberDecl(Parent, Context)) ||
         isa_and_nonnull<CXXDestructorDecl>(callTarget(Parent, Context))))
      continue;
    bool Manages = false;
    switch (Effect.Use) {
    case EUse::Unevaluated:
    case EUse::Discarded:
    case EUse::Statement:
      Manages = true;
      break;
    case EUse::Value:
    case EUse::Condition:
      Manages = Kind == EManaged::Pointer;
      break;
    case EUse::Forward:
      Manages = Then(Kind);
      break;
    case EUse::Access:
      Manages = Kind == EManaged::Slot && Then(EManaged::Pointer);
      break;
    case EUse::Update:
    case EUse::Write:
      Manages = Kind == EManaged::Slot;
      break;
    case EUse::AddressOf:
      Manages = Kind == EManaged::Object && Then(EManaged::Pointer);
      break;
    // What it points to, `*_pObject`, as a glvalue; else, deleted, `delete
    // _pObject`, or destroyed, `_pObject->~int()`, not a member of it used.
    case EUse::Dereference:
      if (Kind != EManaged::Pointer || !Parent)
        break;
      if (effectsOf(Parent, Context).Statement == EStatement::Member)
        break;
      Manages = Parent->isGLValue() ? Then(EManaged::Object) : true;
      break;
    case EUse::Bind:
    case EUse::Copy:
      switch (Effect.Into.Kind) {
      // A local pointer made of it, `auto const *pFirst = _pFirst;`, or a
      // local reference bound to it, `auto &rpObject = _pObject`, whose
      // names are what it is.
      case EDestination::Variable: {
        const auto *Alias = dyn_cast_or_null<VarDecl>(Effect.Into.Declaration);
        if (!Alias || !Alias->hasLocalStorage() || isa<ParmVarDecl>(Alias))
          break;
        if (Alias->getType()->isReferenceType())
          Aliases.push_back({Alias, Kind});
        else if (Kind == EManaged::Pointer && Alias->getType()->isPointerType())
          Aliases.push_back({Alias, EManaged::Slot});
        else
          break;
        Manages = true;
        break;
      }
      case EDestination::Field:
      case EDestination::Heap:
      case EDestination::Element:
        Manages = true;
        break;
      // A pointer value held in a temporary, which is its slot.
      case EDestination::Temporary:
        Manages = Then(Kind == EManaged::Pointer ? EManaged::Slot : Kind);
        break;
      case EDestination::Parameter: {
        const FunctionDecl *Callee = Effect.Into.Callee;
        if (!Callee)
          break;
        if (Callee->getBuiltinID() == Builtin::BI__builtin_malterlib_destroy) {
          Manages = true;
          break;
        }
        const bool Construction =
            Parent &&
            effectsOf(Parent, Context).Statement == EStatement::Construction;
        // What gives it back, by what is done with the result.
        if (!Construction && Parent && returnsItsParameter(Callee)) {
          Manages = Then(Parent->isGLValue() ? (Kind == EManaged::Pointer
                                                     ? EManaged::Slot
                                                     : Kind)
                                             : EManaged::Pointer);
          break;
        }
        // A constructor, or the memory freed as a `void *`, `_Allocator.f_Free
        // (_pObject, Size)`, of one whose body can't be seen; one whose body
        // can is looked into, which can cast it back.
        const unsigned iParameter = Effect.Into.Index;
        if (iParameter >= Callee->getNumParams())
          break;
        if (!Callee->hasBody() &&
            (Construction ||
             Callee->getParamDecl(iParameter)->getType()->isVoidPointerType())) {
          Manages = true;
          break;
        }
        Manages = onlyConstructsOrDestroysImpl(Callee, iParameter);
        break;
      }
      // A pointer to it copied into a closure, which its names in the body
      // are the slot of, or it or the slot of a pointer to it captured by
      // reference, which they are; not a copy of it, which reads it.
      case EDestination::Capture: {
        const auto *Lambda = dyn_cast_or_null<LambdaExpr>(Effect.Into.Object);
        const auto *Captured =
            dyn_cast_or_null<VarDecl>(Effect.Into.Declaration);
        if (!Lambda || !Captured)
          break;
        if (Effect.Use == EUse::Copy && Kind == EManaged::Pointer)
          Captures.push_back({Lambda, Captured, EManaged::Slot});
        else if (Effect.Use == EUse::Bind && Kind != EManaged::Pointer)
          Captures.push_back({Lambda, Captured, Kind});
        else
          break;
        Manages = true;
        break;
      }
      case EDestination::None:
      case EDestination::Designated:
      case EDestination::Base:
      case EDestination::Return:
      case EDestination::Thrown:
        break;
      }
      break;
    case EUse::CallObject:
    case EUse::Callee:
    case EUse::Opaque:
      break;
    }
    if (!Manages)
      return false;
  }
  return true;
}

/// Whether every name of \p Parameter of \p Function in \p Index that runs,
/// \p Names, and of the local aliases of it in \p Body, only constructs,
/// destroys or compares the object (managesObject); also in the body of a
/// lambda that captures it, which stays where it is made and is only called
/// there or by what only calls it (closureUses), not one that can be kept
/// and run later.
bool namesManageObject(const FunctionDecl *Function, const Stmt *Body,
                       llvm::ArrayRef<const DeclRefExpr *> Names,
                       const ParmVarDecl *Parameter,
                       const CEffectIndex &Index, ASTContext &Context) {
  // A pointer, also by reference, is named as its slot; a reference to the
  // object, as the object.
  const EManaged Kind =
      Parameter->getType().getNonReferenceType()->isPointerType()
          ? EManaged::Slot
          : EManaged::Object;
  // The local aliases and captures of it, each with the function and body
  // its names are in.
  struct CNamed {
    const FunctionDecl *Function = nullptr;
    const Stmt *Body = nullptr;
    const VarDecl *Variable = nullptr;
    EManaged Kind = EManaged::Object;
  };
  llvm::SmallVector<CNamed, 4> Named;
  llvm::SmallVector<std::pair<const VarDecl *, EManaged>, 4> Aliases;
  llvm::SmallVector<std::tuple<const LambdaExpr *, const VarDecl *, EManaged>,
                    2>
      Captures;
  CFlowCache &Cache = sharedFlowCache();
  auto Manages = [&](const DeclRefExpr *Name, EManaged NameKind,
                     const FunctionDecl *In, const Stmt *InBody,
                     const CEffectIndex &InIndex) {
    Aliases.clear();
    Captures.clear();
    if (!managesObject(Name, NameKind, InIndex, Aliases, Captures, Context))
      return false;
    for (const auto &[Alias, AliasKind] : Aliases)
      Named.push_back({In, InBody, Alias, AliasKind});
    // A lambda of the body whose uses are told, not one of an initializer
    // of a constructor, which is indexed on its own.
    for (const auto &[Lambda, Captured, CapturedKind] : Captures) {
      const CXXMethodDecl *Operator = Lambda->getCallOperator();
      if (!InBody || !Operator || !Operator->getBody() ||
          &InIndex != &Cache.indexOf(InBody, Context) ||
          !closureUses(In, InBody, Lambda, Context).bLocal)
        return false;
      Named.push_back({Operator, Operator->getBody(), Captured, CapturedKind});
    }
    return true;
  };
  for (const DeclRefExpr *Name : Names)
    if (!Manages(Name, Kind, Function, Body, Index))
      return false;
  // The aliases are locals of their bodies. Past as many as code can have,
  // what is done can't be told.
  for (size_t iNamed = 0; iNamed < Named.size(); ++iNamed) {
    if (iNamed > 64 || !Named[iNamed].Body)
      return false;
    const CNamed Each = Named[iNamed];
    for (const DeclRefExpr *Name :
         Cache.namesIn(Each.Function, Each.Body, Each.Variable, Context))
      if (!Manages(Name, Each.Kind, Each.Function, Each.Body,
                   Cache.indexOf(Each.Body, Context)))
        return false;
  }
  return true;
}

bool onlyConstructsOrDestroysImpl(const FunctionDecl *Function,
                                  unsigned iParameter) {
  if (!Function)
    return false;
  const FunctionDecl *Definition = nullptr;
  if (!Function->hasBody(Definition) || !Definition->getBody() ||
      Definition->isDependentContext() ||
      iParameter >= Definition->getNumParams())
    return false;
  // Each instantiated function once, whose uses are its own; one that calls
  // itself back with it does only what it does otherwise, which is taken of
  // it while it is decided.
  struct CMemo {
    TCSummaries<std::pair<const FunctionDecl *, unsigned>, bool> Decided{
        &onlyConstructsOrDestroysUncached, true, false};
  };
  return astCache<CMemo>(Definition->getASTContext())
      .Decided.get({Definition, iParameter});
}

bool onlyConstructsOrDestroysUncached(
    const std::pair<const FunctionDecl *, unsigned> &Key) {
  const FunctionDecl *const Definition = Key.first;
  const unsigned iParameter = Key.second;
  ASTContext &Context = Definition->getASTContext();
  const ParmVarDecl *Parameter = Definition->getParamDecl(iParameter);
  const Stmt *Body = Definition->getBody();
  // Its names that run, not those unevaluated, `static_assert(sizeof
  // (*_pObject) > 0)`, nor in a branch a constant condition leaves out
  // (CFlowCache::namesIn).
  CFlowCache &Cache = sharedFlowCache();
  bool Result = namesManageObject(
      Definition, Body, Cache.namesIn(Definition, Body, Parameter, Context),
      Parameter, Cache.indexOf(Body, Context), Context);
  // Of a constructor, also its initializers: one that keeps it, `m_pApp
  // (_pApp)` or `m_rApp(*_pApp)`, holds it in the object, but another use,
  // like `m_Value(_pApp->m_Value)`, has to only construct or destroy too.
  if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Definition))
    for (const CXXCtorInitializer *Initializer : Constructor->inits()) {
      const Expr *Init = Initializer->getInit();
      if (!Result || !Init || !Initializer->isWritten())
        continue;
      const CEffectIndex &Index = Cache.indexOf(Init, Context);
      llvm::SmallVector<const DeclRefExpr *, 2> Names;
      for (const Stmt *S : Index.statements())
        if (Index.effectsOf(S)->Statement == EStatement::Name &&
            cast<DeclRefExpr>(S)->getDecl() == Parameter && Index.evaluated(S))
          Names.push_back(cast<DeclRefExpr>(S));
      Result = namesManageObject(Definition, Body, Names, Parameter, Index,
                                 Context);
    }
  return Result;
}
} // namespace

bool onlyConstructsOrDestroys(const FunctionDecl *Function,
                              unsigned iParameter) {
  return onlyConstructsOrDestroysImpl(Function, iParameter);
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
    if (callsOverride(Call))
      return true;
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
           foldsCondition(Condition, Value, Variable->getASTContext()) &&
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
        !pointsToConst(Unary->getType()))
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
        (Type->isPointerType() && !pointsToConst(Type));
    return !Writable || visitConstRemoval(Cast);
  }
  // Nor what a requirement only names, `requires { Value = Other; }`.
  bool TraverseRequiresExpr(RequiresExpr *) { return true; }
  bool visitConstRemoval(ExplicitCastExpr *Cast) {
    // Through any number of aliases, past as many as code can have, it may.
    TCRecursive<bool(const Expr *, int)> LeadsTo = [&](const Expr *E,
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
    if (const CMemberCall Member = memberCallOf(Call)) {
      const CXXMethodDecl *Method = Member->getMethodDecl();
      if (!Member->isArrow() && Method && !hasConstObject(Method) &&
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
                               identifierTextOf(Method)))))
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
    if (stackNearlyExhausted())
      return withStack([&] { return refersToVariable(E); });
    E = E->IgnoreParenImpCasts();
    // Also cast to a reference, `static_cast<CType &>(Value) = Other`.
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
        Cast && Cast->getTypeAsWritten()->isReferenceType())
      return refersToVariable(Cast->getSubExpr());
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E))
      return Conditional->isGLValue() &&
             llvm::any_of(keptArms(Conditional), [&](const Expr *Arm) {
               return refersToVariable(Arm);
             });
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

static bool assignsThroughParameterUncached(
    const std::pair<const FunctionDecl *, unsigned> &Key) {
  ReassignmentFinder Finder(Key.first->getParamDecl(Key.second),
                            /*IncludeCopies=*/true);
  Finder.TraverseStmt(Key.first->getBody());
  return Finder.Found;
}

static bool assignsThroughParameter(const FunctionDecl *Function,
                                    unsigned iParam) {
  // Each function as instantiated once; a function that gives it back to
  // itself counts as assigning it while it is told.
  struct CMemo {
    TCSummaries<std::pair<const FunctionDecl *, unsigned>, bool> Decided{
        &assignsThroughParameterUncached, true, true};
  };
  if (iParam >= Function->getNumParams())
    return true;
  return astCache<CMemo>(Function->getASTContext())
      .Decided.get({Function, iParam});
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

/// Whether \p S, a statement, throws or calls a function that doesn't
/// return, `throw 1;` or `fg_Abort();` of a `[[noreturn]]` one, also as the
/// last of a block, after which what follows doesn't run.
static bool neverGoesOn(const Stmt *S) {
  if (stackNearlyExhausted())
    return withStack([&] { return neverGoesOn(S); });
  if (const auto *Compound = dyn_cast_or_null<CompoundStmt>(S))
    return !Compound->body_empty() && neverGoesOn(Compound->body_back()) &&
           !hasJumpTarget(Compound);
  const auto *E = dyn_cast_or_null<Expr>(S);
  if (!E)
    return false;
  E = E->IgnoreImplicit()->IgnoreParenImpCasts();
  if (isa<CXXThrowExpr>(E))
    return true;
  if (const auto *Call = dyn_cast<CallExpr>(E))
    if (const FunctionDecl *Callee = Call->getDirectCallee())
      return Callee->isNoReturn();
  return false;
}

bool isInNeverTakenBranch(const DynTypedNode &Node, ASTContext &Context) {
  auto Constant = [&](const Expr *Condition, bool &Value) {
    return Condition && !Condition->isValueDependent() &&
           foldsCondition(Condition, Value, Context);
  };
  // Whether \p From is \p Operand, past what is implicit and parentheses,
  // which the parents can skip, `if (false) Object.f_Init();` of a result
  // with cleanups, or `false && (Object.f_Init())`.
  auto Is = [](const Stmt *From, const Stmt *Operand) {
    if (!From || !Operand)
      return false;
    if (From == Operand)
      return true;
    auto Strip = [](const Stmt *S) -> const Stmt * {
      const auto *E = dyn_cast<Expr>(S);
      return E ? IgnoreExprNodes(E, IgnoreImplicitSingleStep,
                                 IgnoreParensSingleStep)
               : S;
    };
    return Strip(From) == Strip(Operand);
  };
  DynTypedNode Child = Node;
  for (int Depth = 0; Depth < 1024; ++Depth) {
    const auto Parents = instantiatedParents(Child, Context);
    if (Parents.empty())
      return false;
    const DynTypedNode Parent = Parents[0];
    const Stmt *From = Child.get<Stmt>();
    bool Value = false;
    if (Parent.get<FunctionDecl>() || Parent.get<BlockExpr>() ||
        Parent.get<TranslationUnitDecl>())
      return false;
    // Of a lambda, only its body runs later, when it is called, not what its
    // captures are made of, which runs where it is made, `if (false)
    // (void)[Guard = Object.f_Init()] {};`.
    if (const auto *Lambda = Parent.get<LambdaExpr>()) {
      const auto *Capture = Child.get<VarDecl>();
      if (!(From && From != Lambda->getBody()) &&
          !(Capture && Capture->isInitCapture()))
        return false;
    }
    if (const auto *If = Parent.get<IfStmt>();
        If && From && !If->isConstexpr() && !If->isConsteval() &&
        !If->getConditionVariable() && Constant(If->getCond(), Value)) {
      const Stmt *Skipped = Value ? If->getElse() : If->getThen();
      if (Is(From, Skipped) && !hasJumpTarget(Skipped))
        return true;
    }
    if (const auto *While = Parent.get<WhileStmt>();
        While && From && Is(From, While->getBody()) &&
        !While->getConditionVariable() && Constant(While->getCond(), Value) &&
        !Value && !hasJumpTarget(From))
      return true;
    if (const auto *For = Parent.get<ForStmt>();
        For && From && (Is(From, For->getBody()) || Is(From, For->getInc())) &&
        !For->getConditionVariable() && Constant(For->getCond(), Value) &&
        !Value && !hasJumpTarget(For->getBody()))
      return true;
    if (const auto *Conditional = Parent.get<ConditionalOperator>();
        Conditional && From && Constant(Conditional->getCond(), Value) &&
        Is(From,
           Value ? Conditional->getFalseExpr() : Conditional->getTrueExpr()))
      return true;
    if (const auto *Logical = Parent.get<BinaryOperator>();
        Logical && Logical->isLogicalOp() && Is(From, Logical->getRHS()) &&
        Constant(Logical->getLHS(), Value) &&
        Value == (Logical->getOpcode() == BO_LOr))
      return true;
    // After what always leaves the block, `return; Object.f_Init();`, also
    // by throwing or by calling what doesn't return, `throw 1;` or
    // `fg_Abort();` of a `[[noreturn]]` one, unless a jump can come back to
    // it.
    if (const auto *Block = Parent.get<CompoundStmt>();
        Block && From && !hasJumpTarget(Block))
      for (const Stmt *Statement : Block->body()) {
        if (Is(Statement, From))
          break;
        if (alwaysLeaves(Statement, Context) || neverGoesOn(Statement))
          return true;
      }
    Child = Parent;
  }
  return false;
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
      TCRecursive<void(const Stmt *)> Find = [&](const Stmt *S) {
        if (!S || Jumps || isa<LambdaExpr>(S))
          return;
        Jumps = isa<GotoStmt, IndirectGotoStmt>(S);
        for (const Stmt *Child : keptChildren(S))
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
      TCRecursive<void(const Stmt *)> FindCase = [&](const Stmt *S) {
        if (!S || Entered || isa<LambdaExpr, SwitchStmt>(S))
          return;
        if (isa<SwitchCase>(S) &&
            SM.isBeforeInTranslationUnit(Statement->getEndLoc(),
                                         S->getBeginLoc()) &&
            !SM.isBeforeInTranslationUnit(Later->getBeginLoc(),
                                          S->getBeginLoc()))
          Entered = true;
        for (const Stmt *Child : keptChildren(S))
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
  if (!Named->second.contains(Variable))
    return true;
  return Facts.Unchanged.get(Variable);
}

static bool isUnchangedLocalUncached(const VarDecl *Variable) {
  // A local of a block, `^{ int Local; }`, is in no function.
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  if (!Function)
    return false;
  ReassignmentFinder Finder(Variable);
  Finder.TraverseStmt(Function->getBody());
  return !Finder.Found;
}


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

/// Whether the local \p Ref refers to keeps its initial value where \p Ref
/// uses it: in a lambda, the copy it captured must not be changed either.
bool isUnchangedLocalAt(const DeclRefExpr *Ref, const VarDecl *Variable) {
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

/// Whether \p E is `this`, `*this`, an explicit object, or an unchanged local
/// alias of them, also either value of a conditional whose other is null.
static bool refersToThis(const Expr *E, ASTContext &Context) {
  // Not the own storage of a copy of the object, `[pCopy = &Copy]` of `CState
  // Copy = *this` (isCopyStorage).
  const CObjectRelation Of = objectRelation(E, namesThis, &Context);
  return Of.Kind == EObjectRelation::SameObject && !isCopyStorage(Of);
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
    return refersToOwnObject(Variable->getInit(), &Variable->getASTContext());
  // Also a pointer or reference into it, `[pValue = &m_Value]`, which is of
  // the object, through its members and the elements of its arrays.
  // Not a copy of the value of a pointer member, `[pData = m_pData]`, which
  // points where that does, not into the object.
  // What a borrowed part of a local leads to, a pointer or reference
  // member of it or what an accessor of one gives, `Source.m_pItems` or
  // `&Source.f_Items()` of `CMixedView Source{0, {}, &mp_Items}`, is where it
  // was given, into the object when that is (isBorrowedField,
  // pointeeRelation): not through a pointer the object holds, nor a copy's
  // own storage (isCopyStorage).
  auto IntoByBorrowed = [&](const Expr *Init) {
    // Also through a cast of a pointer, `static_cast<int const
    // *>(View.m_pValue)`.
    const Expr *Bare = Init->IgnoreParenImpCasts();
    while (const auto *Cast = dyn_cast<ExplicitCastExpr>(Bare)) {
      if (!Cast->getType()->isPointerType() && !Cast->isGLValue())
        break;
      Bare = Cast->getSubExpr()->IgnoreParenImpCasts();
    }
    Init = Bare;
    bool Address = false;
    if (const auto *Unary = dyn_cast<UnaryOperator>(Bare);
        Unary && Unary->getOpcode() == UO_AddrOf) {
      Bare = Unary->getSubExpr()->IgnoreParenImpCasts();
      Address = true;
    }
    const Expr *Object = nullptr;
    if (const auto *Member = dyn_cast<MemberExpr>(Bare)) {
      const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
      if (!Field || !isBorrowedField(Field) ||
          Field->getType()->isReferenceType() != Address)
        return false;
      Object = Member->getBase();
    } else if (const CMemberCall Call = memberCallOf(Bare);
               Call &&
               (Address ? Call->isGLValue()
                        : Call->getType()->isPointerType()) &&
               Call->getMethodDecl() && !callsOverride(Call)) {
      // One whose body can be seen to give a borrowed part.
      const FunctionDecl *Definition = Call->getMethodDecl()->getDefinition();
      if (Definition && Definition->getBody() &&
          returnsBorrowed(Call->getMethodDecl()))
        Object = Call->getImplicitObjectArgument();
    }
    // Of a local, also of a member it holds by value.
    const Expr *Root = Object;
    while (const auto *Member =
               Root ? dyn_cast<MemberExpr>(Root->IgnoreParenImpCasts())
                    : nullptr) {
      if (Member->isArrow())
        return false;
      Root = Member->getBase();
    }
    const auto *Ref =
        Root ? dyn_cast<DeclRefExpr>(Root->IgnoreParenImpCasts()) : nullptr;
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (!Local || !Local->hasLocalStorage() ||
        Local->getType()->isReferenceType() || isa<ParmVarDecl>(Local))
      return false;
    // Of a record held by value, what the member it is loaded from holds,
    // by provenance (storageRootsOf).
    if (isOpaqueRecord(Local, Variable->getASTContext()))
      return llvm::any_of(
          storageRootsOf(Init, Variable->getASTContext(),
                         Init->isGLValue() && Init->getType()->isPointerType())
              .Roots,
          [](const CStorageRoot &Each) {
            return !Each.Variable && Each.Kind == ERootKind::Object;
          });
    auto IsThis = [](const Expr *Of) { return namesThis(Of); };
    // Of a member of the local itself, what its initializer gives that
    // member, `&m_Items[0]` of `CView View{&m_Items[0]}`, an entry, not what
    // all of it is made of, also of a copy of such a local, `auto Copy =
    // View`, unchanged.
    if (const auto *Member = dyn_cast<MemberExpr>(Bare);
        Member && Member->getBase()->IgnoreParenImpCasts() == Ref) {
      // A static data member named through the local, `Source.ms_pValue`, is
      // no part of it.
      const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
      if (!Field)
        return false;
      // What the member holds where it is captured (localSlotValues), also
      // after it is given another value, of each it can hold there.
      if (!Field->getType()->isReferenceType()) {
        const CSlotValues Values = localSlotValues(
            {Local, {Field}, false}, Bare, Local->getASTContext());
        if (Values.Told && !Values.Values.empty() &&
            llvm::all_of(Values.Values, [](const CSlotValue &Value) {
              return Value.Kind == CSlotValue::EKind::Value && Value.Value;
            }))
          return llvm::any_of(Values.Values, [&](const CSlotValue &Value) {
            const CObjectRelation Of = objectRelation(Value.Value, IsThis, &Variable->getASTContext());
            return (Of.Kind == EObjectRelation::SameObject ||
                    Of.Kind == EObjectRelation::Subobject) &&
                   !Of.bThroughPointer && !isCopyStorage(Of);
          });
      }
      const VarDecl *Holder = Local;
      for (int Depth = 0; Holder && Depth < 8; ++Depth) {
        if (!Holder->getInit() || isChangedVariable(Holder))
          break;
        const Expr *Made = Holder->getInit()->IgnoreImplicit()->IgnoreParens();
        if (const auto *Construct = dyn_cast<CXXConstructExpr>(Made);
            Construct && Construct->getConstructor() &&
            Construct->getConstructor()->isCopyOrMoveConstructor() &&
            Construct->getNumArgs() >= 1) {
          const auto *Source = dyn_cast<DeclRefExpr>(
              Construct->getArg(0)->IgnoreParenImpCasts());
          Holder = Source ? dyn_cast<VarDecl>(Source->getDecl()) : nullptr;
          if (Holder && !Holder->hasLocalStorage())
            Holder = nullptr;
          continue;
        }
        llvm::ArrayRef<Expr *> Inits;
        if (const auto *List = dyn_cast<InitListExpr>(Made)) {
          if (!List->isSemanticForm() && List->getSemanticForm())
            List = List->getSemanticForm();
          Inits = llvm::ArrayRef<Expr *>(List->getInits(), List->getNumInits());
        } else if (const auto *Paren = dyn_cast<CXXParenListInitExpr>(Made)) {
          Inits = Paren->getInitExprs();
        } else {
          break;
        }
        const RecordDecl *Record = Field->getParent();
        const auto *Class = dyn_cast<CXXRecordDecl>(Record);
        if (Record->isUnion() || (Class && Class->getNumBases() != 0))
          break;
        if (Field->getFieldIndex() >= Inits.size())
          return false;
        const Expr *Given = Inits[Field->getFieldIndex()];
        const CObjectRelation Of = objectRelation(Given, IsThis, &Variable->getASTContext());
        return (Of.Kind == EObjectRelation::SameObject ||
                Of.Kind == EObjectRelation::Subobject) &&
               !Of.bThroughPointer && !isCopyStorage(Of);
      }
    }
    const CObjectRelation Of = Address || !Bare->isGLValue()
                                   ? objectRelation(Init, IsThis, &Variable->getASTContext())
                                   : pointeeRelation(Bare, IsThis,
                                                     &Variable->getASTContext());
    return (Of.Kind == EObjectRelation::SameObject ||
            Of.Kind == EObjectRelation::Subobject ||
            Of.Kind == EObjectRelation::Unknown) &&
           !Of.bThroughPointer && !isCopyStorage(Of);
  };
  auto IsOfThis = [&](const Expr *Init) {
    if (IntoByBorrowed(Init))
      return true;
    const Expr *Into = Init->IgnoreParenImpCasts();
    if (const auto *Address = dyn_cast<UnaryOperator>(Into);
        Address && Address->getOpcode() == UO_AddrOf)
      Into = Address->getSubExpr()->IgnoreParenImpCasts();
    else if (const auto *Member = dyn_cast<MemberExpr>(Into);
             Member && Member->getType()->isPointerType())
      return refersToThis(Init, Variable->getASTContext());
    for (int Depth = 0; Depth < 64; ++Depth) {
      if (const auto *Member = dyn_cast<MemberExpr>(Into);
          Member && isa<FieldDecl>(Member->getMemberDecl())) {
        if (Member->isArrow())
          return refersToThis(Member->getBase(), Variable->getASTContext());
        Into = Member->getBase()->IgnoreParenImpCasts();
      } else if (const auto *Pointer = dyn_cast<BinaryOperator>(Into);
                 Pointer && Pointer->isPtrMemOp()) {
        // Also through a pointer to a member, `&(this->*pMember)`.
        if (Pointer->getOpcode() == BO_PtrMemI)
          return refersToThis(Pointer->getLHS(), Variable->getASTContext());
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
    return refersToThis(Init, Variable->getASTContext()) ||
           (Into != Init->IgnoreParenImpCasts() && refersToThis(Into, Variable->getASTContext()));
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
  TCRecursive<bool(const Expr *, int)> AnyOfThis = [&](const Expr *Init,
                                                         int Depth) {
    if (Depth > 32)
      return false;
    const Expr *Inner = Init->IgnoreParenImpCasts();
    if (const auto *Conditional =
            dyn_cast<AbstractConditionalOperator>(Inner)) {
      bool Selected = false;
      if (!Conditional->getCond()->isValueDependent() &&
          foldsCondition(Conditional->getCond(), Selected, Context))
        return AnyOfThis(Selected ? Conditional->getTrueExpr()
                                  : Conditional->getFalseExpr(),
                         Depth + 1);
      return llvm::any_of(keptArms(Conditional), [&](const Expr *Arm) {
        return AnyOfThis(Arm, Depth + 1);
      });
    }
    if (const auto *Comma = dyn_cast<BinaryOperator>(Inner);
        Comma && Comma->getOpcode() == BO_Comma)
      return AnyOfThis(Comma->getRHS(), Depth + 1);
    // What a member function of the object whose body can be seen gives of
    // it, `[pValue = f_Get()]` of `int *f_Get() { return &m_Value; }`, whose
    // `this` is the object.
    if (const CMemberCall Getter = memberCallOf(Inner);
        Getter && Getter->getType()->isPointerType() &&
        Getter->getMethodDecl() && Getter->getImplicitObjectArgument() &&
        refersToThis(Getter->getImplicitObjectArgument(),
                     Variable->getASTContext()) &&
        !callsOverride(Getter) && FirstLook(Getter->getMethodDecl(), Depth)) {
      // Of one with an explicit object, whose body names it as a parameter,
      // what it gives of it (returnedRelation).
      if (Getter->isExplicitObject()) {
        const std::optional<CObjectRelation> Gives = returnedRelation(
            Getter->getMethodDecl(),
            const_cast<ASTContext *>(&Variable->getASTContext()));
        if (Gives &&
            (Gives->Kind == EObjectRelation::SameObject ||
             Gives->Kind == EObjectRelation::Subobject) &&
            !Gives->bThroughPointer && !isCopyStorage(*Gives))
          return true;
      } else {
        for (const Expr *Returned : returnedValues(Getter->getMethodDecl()))
          if (AnyOfThis(Returned, Depth + 1))
            return true;
      }
    }
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
  if (stackNearlyExhausted())
    return withStack([&] { return isEmptyActor(Actor, Depth); });
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

static std::optional<llvm::SmallVector<const Expr *, 2>>
namedValuesByFlow(const DeclRefExpr *Ref, ASTContext &Context) {
  const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
  // A copy a closure keeps holds what it copied where the closure is made,
  // unless the body of a mutable closure changes it: of the innermost lambda
  // Ref is in the body of that copies it.
  const LambdaExpr *Closure = nullptr;
  if (Variable && Ref->refersToEnclosingVariableOrCapture()) {
    DynTypedNode Node = DynTypedNode::create(*Ref);
    const Stmt *Child = Ref;
    for (int Depth = 0; Depth < 4096 && !Closure; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        break;
      Node = Parents[0];
      const auto *Lambda = Node.get<LambdaExpr>();
      if (const auto *Operator = Node.get<CXXMethodDecl>();
          Operator && Operator->getParent()->isLambda())
        Lambda = lambdaOfCallOperator(Operator, Context);
      if (Lambda && Child &&
          (Child == Lambda->getBody() ||
           Child == Lambda->getCallOperator()->getBody()) &&
          llvm::any_of(Lambda->captures(), [&](const LambdaCapture &Capture) {
            return Capture.capturesVariable() &&
                   Capture.getCaptureKind() == LCK_ByCopy &&
                   Capture.getCapturedVar() == Variable;
          }))
        Closure = Lambda;
      if (Lambda) {
        Node = DynTypedNode::create(*Lambda);
        Child = Lambda;
        continue;
      }
      if (Node.get<FunctionDecl>())
        break;
      Child = Node.get<Stmt>();
    }
  }
  if (Closure) {
    // Of a mutable one, also only where its body doesn't change it
    // (localSlotChangedIn).
    CSlot Copy;
    Copy.Variable = Variable;
    if (!hasConstObject(Closure->getCallOperator()) &&
        localSlotChangedIn(Closure->getCallOperator(), Copy, Context))
      return std::nullopt;
    for (const LambdaCapture &Capture : Closure->captures())
      if (Capture.capturesVariable() && Capture.getCapturedVar() == Variable)
        if (const Expr *Init = captureInitOf(Closure, Capture))
          return llvm::SmallVector<const Expr *, 2>{Init};
    return std::nullopt;
  }
  bool Told = true;
  const llvm::SmallVector<CSlot, 2> Slots = designatedSlots(Ref, Context, &Told);
  // A local reference bound to what isn't a local slot, `auto &Own = self;`,
  // designates it.
  if (Variable && Variable->getType()->isReferenceType() &&
      Variable->getInit() && llvm::none_of(Slots, [](const CSlot &Slot) {
        return Slot.Place == EPlace::Variable;
      }))
    return llvm::SmallVector<const Expr *, 2>{Variable->getInit()};
  if (!Told || Slots.empty())
    return std::nullopt;
  llvm::SmallVector<const Expr *, 2> Values;
  for (const CSlot &Slot : Slots) {
    const CSlotReaching Reaching = localSlotReaching(Slot, Ref, Context);
    if (!Reaching.Told || Reaching.Definitions.empty())
      return std::nullopt;
    for (const CSlotDefinition &Definition : Reaching.Definitions) {
      if (!Definition.Known || !Definition.Value.Value)
        return std::nullopt;
      switch (Definition.Value.Kind) {
      case CSlotValue::EKind::Value:
        if (!llvm::is_contained(Values, Definition.Value.Value))
          Values.push_back(Definition.Value.Value);
        break;
      case CSlotValue::EKind::Stored:
      case CSlotValue::EKind::Given:
      case CSlotValue::EKind::Address:
        return std::nullopt;
      }
    }
  }
  return Values;
}

std::optional<llvm::SmallVector<const Expr *, 2>>
namedValues(const DeclRefExpr *Ref, ASTContext &Context) {
  if (std::optional<llvm::SmallVector<const Expr *, 2>> Values =
          namedValuesByFlow(Ref, Context))
    return Values;
  // Where the flow can't tell, as a member function that isn't const called
  // on it, which may not change it, `Actor(&CActor::f_Run)`: the initializer
  // of a local that nothing but this use changes, also not the copy a lambda
  // the use is in keeps, as before the flow told it (until what a callee
  // writes is told).
  const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
  if (Variable && Variable->getInit() &&
      (isUnchangedLocal(Variable) || !isChangedOtherThanBy(Ref)) &&
      !isChangedInCapturingLambdas(Ref, Variable, Context))
    return llvm::SmallVector<const Expr *, 2>{Variable->getInit()};
  return std::nullopt;
}

namespace {
/// The objects whose actor a handle is of (handleObjects), or that they
/// can't be told.
struct CHandleObjects {
  bool Told = true;
  llvm::SmallVector<const Expr *, 2> Objects;
  bool operator==(const CHandleObjects &Other) const {
    return Told == Other.Told && Objects == Other.Objects;
  }
};
} // namespace

static CHandleObjects
handleObjectsUncached(const std::pair<const Expr *, ASTContext *> &Key);

/// handleObjectsUncached, each expression told once, which a chain of copies,
/// `auto Copy = Own;`, and of conditionals of them, `bChoose ? Copy : Copy`,
/// meets through many paths; one that reaches itself, through a local given
/// itself, adds nothing while it is told.
static CHandleObjects handleObjectsOf(const Expr *E, ASTContext &Context) {
  struct CMemo {
    TCSummaries<std::pair<const Expr *, ASTContext *>, CHandleObjects> Objects{
        &handleObjectsUncached, CHandleObjects(), CHandleObjects{false, {}}};
  };
  return astCache<CMemo>(Context).Objects.get({E, &Context});
}

static CHandleObjects
handleObjectsUncached(const std::pair<const Expr *, ASTContext *> &Key) {
  const Expr *const E = skipWrappers(Key.first);
  ASTContext &Context = *Key.second;
  CHandleObjects Found;
  const CHandleObjects Untold{false, {}};
  auto Add = [&](const Expr *Of) {
    const CHandleObjects From = handleObjectsOf(Of, Context);
    Found.Told &= From.Told;
    for (const Expr *Object : From.Objects)
      if (!llvm::is_contained(Found.Objects, Object))
        Found.Objects.push_back(Object);
  };
  const Expr *const Inner = E->IgnoreParenImpCasts();
  // Each value of a conditional, `bChoose ? Own : OwnCopy`, or the one a
  // constant condition chooses.
  if (const auto *Conditional = dyn_cast<ConditionalOperator>(Inner)) {
    bool Chosen = false;
    if (!Conditional->getCond()->isValueDependent() &&
        foldsCondition(Conditional->getCond(), Chosen, Context)) {
      Add(Chosen ? Conditional->getTrueExpr() : Conditional->getFalseExpr());
      return Found;
    }
    for (const Expr *Arm : keptArms(Conditional))
      Add(Arm);
    return Found;
  }
  // Moved, `fg_Move(ThisActor)`, or forwarded, it is the handle it moves.
  if (const auto *Move = dyn_cast<CallExpr>(Inner);
      Move && Move->getNumArgs() == 1 &&
      isMoveOrForwardFunction(calleeName(Move))) {
    Add(Move->getArg(0));
    return Found;
  }
  // Cast to a reference to it, `static_cast<TCActor<CApp> const &>(Own)`, it
  // is the handle it designates; converted by name, `TCActor<>(self)` or
  // `static_cast<TCActor<>>(self)`, what it would be converted implicitly.
  if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Inner);
      Cast && (Cast->isGLValue()
                   ? recordName(Cast->getType()) ==
                         recordName(Cast->getSubExpr()->getType())
                   : Cast->getCastKind() == CK_ConstructorConversion ||
                         Cast->getCastKind() == CK_UserDefinedConversion ||
                         Cast->getCastKind() == CK_NoOp)) {
    Add(Cast->getSubExpr());
    return Found;
  }
  // `self` of an object, `pThis->self`, and the handle of one,
  // `fg_ThisActor(_pThis)`.
  if (const auto *Member = dyn_cast<MemberExpr>(E)) {
    if (!Member->getMemberDecl()->getIdentifier() ||
        identifierTextOf(Member->getMemberDecl()) != "self")
      return Untold;
    Found.Objects.push_back(Member->getBase());
    return Found;
  }
  if (const auto *Call = asCallTo(E, {"fg_ThisActor"})) {
    if (Call->getNumArgs() != 1)
      return Untold;
    Found.Objects.push_back(Call->getArg(0));
    return Found;
  }
  // The conversion of `self`, a `CThisActor`, to a handle.
  if (const CMemberCall Call = memberCallOf(E);
      Call && isa_and_nonnull<CXXConversionDecl>(Call->getMethodDecl())) {
    Add(Call->getImplicitObjectArgument());
    return Found;
  }
  // A weak handle, and the handle it locks again, like
  // `fg_ThisActor(this).f_Weak()` and `ThisWeak.f_Lock()`.
  if (const CMemberCall Call = memberCallOf(E);
      Call && Call->getNumArgs() == 0 && Call->getMethodDecl() &&
      Call->getMethodDecl()->getIdentifier() &&
      (identifierTextOf(Call->getMethodDecl()) == "f_Weak" ||
       identifierTextOf(Call->getMethodDecl()) == "f_Lock") &&
      llvm::is_contained(
          {StringRef("TCActor"), StringRef("TCWeakActor"),
           StringRef("TCDistributedActor"), StringRef("TCWeakDistributedActor")},
          recordName(Call->getImplicitObjectArgument()->getType()))) {
    Add(Call->getImplicitObjectArgument());
    return Found;
  }
  // A local handle, a parameter or a copy a closure keeps: each value that
  // reaches where it is named (localSlotReaching), also through a local
  // reference to it (designatedSlots); none of a change whose value can't be
  // told, `Actor.f_Clear()`, nor of what it is given or where what reaches
  // can't be told.
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    const std::optional<llvm::SmallVector<const Expr *, 2>> Values =
        namedValues(Ref, Context);
    if (!Values)
      return Untold;
    for (const Expr *Value : *Values)
      Add(Value);
    return Found;
  }
  return Untold;
}

std::optional<llvm::SmallVector<const Expr *, 2>>
handleObjects(const Expr *Handle, ASTContext &Context) {
  if (!Handle)
    return std::nullopt;
  const CHandleObjects Found = handleObjectsOf(Handle, Context);
  if (!Found.Told || Found.Objects.empty())
    return std::nullopt;
  return Found.Objects;
}

/// Whether each object the handle \p E is of (handleObjects) is the object
/// of the current actor (refersToCurrentActorObject).
static bool isOwnActorImpl(const Expr *E, ASTContext &Context) {
  const std::optional<llvm::SmallVector<const Expr *, 2>> Objects =
      handleObjects(E, Context);
  return Objects && llvm::all_of(*Objects, [&](const Expr *Object) {
           return refersToCurrentActorObject(Object, &Context);
         });
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

bool givesItsArgument(const CallExpr *Call) {
  if (!Call || Call->getNumArgs() != 1 || isMemberCall(Call) ||
      isa<CXXOperatorCallExpr>(Call))
    return false;
  const FunctionDecl *Callee = Call->getDirectCallee();
  return isAddressOfCall(Call) || isMoveOrForwardFunction(calleeName(Call)) ||
         (Callee && Callee->getBuiltinID() && returnsItsParameter(Callee));
}

const FunctionDecl *setterCalled(const CallExpr *Call, unsigned &Offset) {
  Offset = 0;
  if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(Call)) {
    if (Invoke->getOperator() != OO_Call || Invoke->getNumArgs() < 1 ||
        !calledLambda(Invoke->getArg(0)))
      return nullptr;
    // The lambda itself, which an explicit object parameter is given, `[](this
    // auto const &, int *&o_pValue) { ... }`, and the call operator else not.
    Offset = argumentShift(Invoke, Invoke->getDirectCallee());
  } else if (isMemberCall(Call))
    return nullptr;
  const FunctionDecl *Callee = Call->getDirectCallee();
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  return Definition && Definition->getBody() ? Definition : nullptr;
}

llvm::ArrayRef<CSetParameter> setParameters(const FunctionDecl *Function) {
  // Of each function, its sets once told (summarySetParameters); while they
  // are, none, as the call of one that can't be told.
  struct CSets {
    TCSummaries<const FunctionDecl *, llvm::SmallVector<CSetParameter, 1>>
        Functions{[](const FunctionDecl *const &Of) {
                    return summarySetParameters(Of);
                  },
                  llvm::SmallVector<CSetParameter, 1>(),
                  llvm::SmallVector<CSetParameter, 1>()};
  };
  if (!Function->getBody())
    return {};
  return astCache<CSets>(Function->getASTContext()).Functions.get(Function);
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
  return functionDesignatedBy(Call->getCallee());
}

const FunctionDecl *functionDesignatedBy(const Expr *Callee) {
  Callee = Callee->IgnoreParenImpCasts();
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
           (identifierTextOf(Class) == "CCoroutineTransferOwnership" ||
            identifierTextOf(Class) == "CCoroutineTransferOwnershipAwaiter");
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
      return identifierTextOf(Template);
  }
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record || !Record->getIdentifier())
    return {};
  if (const auto *Specialization =
          dyn_cast<ClassTemplateSpecializationDecl>(Record))
    return identifierTextOf(Specialization->getSpecializedTemplate());
  return identifierTextOf(Record);
}

bool isOwnActor(const Expr *E, ASTContext &Context) {
  const bool New = isOwnActorImpl(E, Context);
  return New;
}

StringRef calleeName(const CallExpr *Call) {
  if (const FunctionDecl *Callee = Call->getDirectCallee())
    return Callee->getIdentifier() ? identifierTextOf(Callee) : StringRef();
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
  TCRecursive<bool(const Stmt *)> Uses = [&](const Stmt *S) {
    if (!S)
      return false;
    if (const auto *Member = dyn_cast<MemberExpr>(S);
        Member && Member->getMemberDecl() == Field)
      return true;
    return isa<ReturnStmt, GotoStmt, CXXThrowExpr, CoreturnStmt, CallExpr,
               CXXConstructExpr, CXXThisExpr, LambdaExpr>(S) ||
           llvm::any_of(keptChildren(S), Uses);
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
  TCRecursive<bool(const Expr *, int)> IsStatic = [&](const Expr *Value,
                                                        int Depth) {
    Value = Value->IgnoreParenImpCasts();
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Value);
        Conditional && Depth < 8)
      return llvm::all_of(keptArms(Conditional), [&](const Expr *Arm) {
        return IsStatic(Arm, Depth + 1);
      });
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
  // What it does to what it points to (storageUsesOf): nothing, compared or
  // tested where its value ends, also in a body it is given to.
  return storageUsesOf(Pointer, Pointer->isGLValue() ? 1 : 0, Context).none();
}

// Whether what the view \p Root names points to is a temporary made with it:
// of an init capture, a temporary its initializer refers to, the array of
// `[Values = {1, 2}]`, which lives with the closure, not elsewhere.
static bool viewsOwnTemporary(const Expr *Root, const CEffects &Effects,
                              ASTContext &Context) {
  const auto *Name = Effects.Statement == EStatement::Name
                         ? dyn_cast<DeclRefExpr>(Root)
                         : nullptr;
  const auto *Variable = Name ? dyn_cast<VarDecl>(Name->getDecl()) : nullptr;
  if (!Variable || !Variable->isInitCapture() || !Variable->getInit())
    return false;
  bool Temporary = false;
  CProvenance(Context).reach(
      Variable->getInit(), 1, [&](const Expr *At, unsigned, bool) {
        if (effectsOf(At, Context).Statement != EStatement::Temporary)
          return EReach::Continue;
        Temporary = true;
        return EReach::Root;
      });
  return Temporary;
}

// Whether what the call \p At, reached at \p Level, gives back can't be
// told of what it is given (objectStorageOf): of a function whose returns
// can't be seen (returnedRoots), or that an override can run instead, given
// what can refer elsewhere, a pointer, a reference or an object, also its
// object, of a value it gives back, where provenance ends; not what only
// gives back what it is given (givesItsArgument, keptPointerArgument), which
// it follows.
static bool callUntold(const CallExpr *At, const CEffects &Effects,
                       unsigned Level, const CProvenance &Walk) {
  if ((Level == 0 && At->isPRValue() && !At->getType()->isPointerType()) ||
      givesItsArgument(At) || keptPointerArgument(At))
    return false;
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Effects.Callee);
  if (Effects.Callee && !(Method && mayRunOverride(At, Method)) &&
      Walk.returnedRoots(Effects.Callee))
    return false;
  // The object of one that gives back a reference or a pointer, step takes
  // as a part of the object.
  const bool ObjectFollowed =
      Effects.Callee && (Effects.Callee->getReturnType()->isReferenceType() ||
                         Effects.Callee->getReturnType()->isPointerType());
  return llvm::any_of(Effects.Operands, [&](const COperandEffect &Effect) {
    const auto *Operand = dyn_cast_or_null<Expr>(Effect.Operand);
    if (!Operand)
      return false;
    const bool Referring = Operand->isGLValue() ||
                           !(Operand->getType()->isArithmeticType() ||
                             Operand->getType()->isEnumeralType() ||
                             Operand->getType()->isNullPtrType());
    switch (Effect.Use) {
    // What the callee's result can keep of what it is given (parameterKept),
    // of one whose returns can't be seen as of a body that can't be.
    case EUse::Bind:
    case EUse::Copy:
      return Referring && Effect.Into.Kind == EDestination::Parameter &&
             (!isa<CallExpr>(At) ||
              heldIn(At, Effects, Effect.Into.Index, Operand, Walk.context(),
                     /*Unseen=*/!Effects.Callee ||
                         !Walk.returnedRoots(Effects.Callee) ||
                         (Method && mayRunOverride(At, Method)))
                  .Made);
    case EUse::CallObject:
      return !ObjectFollowed;
    case EUse::Forward:
    case EUse::Access:
    case EUse::Update:
    case EUse::Write:
    case EUse::Dereference:
    case EUse::AddressOf:
    case EUse::Callee:
    case EUse::Condition:
    case EUse::Value:
    case EUse::Discarded:
    case EUse::Statement:
    case EUse::Unevaluated:
    case EUse::Opaque:
      return false;
    }
    return false;
  });
}

static bool
returnsUntoldUncached(const std::pair<const FunctionDecl *, unsigned> &Key);

namespace {
/// Of each function and level, whether what its returns give there can't
/// be told (returnsUntold), once told; one that reaches itself is told by
/// the rest of it, which is taken not to while it is.
struct CUntoldReturns {
  TCSummaries<std::pair<const FunctionDecl *, unsigned>, bool> Results{
      &returnsUntoldUncached, false, true};
};
} // namespace

// Whether what the call \p At, reached at \p Level, gives back can't be told
// by a way of its returns that ends at what isn't one of their roots
// (CReturnedRoots::Other): through what a static local that can be changed
// holds, `return s_pCached;`, or what can't be told itself (storageOf), not
// a literal or a global.
static bool returnsUntold(const CallExpr *At, const CEffects &Effects,
                          unsigned Level, const CProvenance &Walk) {
  const CReturnedRoots *Roots =
      Effects.Callee ? Walk.returnedRoots(Effects.Callee) : nullptr;
  if (!Roots || !Roots->Other)
    return false;
  return astCache<CUntoldReturns>(Walk.context())
      .Results.get({Effects.Callee->getDefinition(), Level});
}

static bool
returnsUntoldUncached(const std::pair<const FunctionDecl *, unsigned> &Key) {
  const FunctionDecl *const Definition = Key.first;
  const unsigned Level = Key.second;
  ASTContext &Context = Definition->getASTContext();
  return llvm::any_of(
      writtenReturns(Definition, Context), [&](const Expr *Value) {
        return storageOf(
                   Value, Level, [](const Expr *) { return false; },
                   [](const Expr *, const CEffects &, unsigned,
                      const CProvenance &) { return true; },
                   Context)
            .bUnknown;
      });
}

static CObjectStorage storageOf(const Expr *E, unsigned Level,
                                llvm::function_ref<bool(const Expr *)> IsObject,
                                FOwnAt OwnAt, ASTContext &Context,
                                llvm::SmallPtrSetImpl<const Expr *> *Reached) {
  if (stackNearlyExhausted())
    return withStack([&] { return storageOf(E, Level, IsObject, OwnAt, Context, Reached); });
  CObjectStorage Of;
  auto Merge = [&](const CObjectStorage &Other) {
    Of.bOwn |= Other.bOwn;
    Of.bBorrowed |= Other.bBorrowed;
    Of.bUnknown |= Other.bUnknown;
    Of.bElsewhere |= Other.bElsewhere;
    Of.bCut |= Other.bCut;
  };
  // What the operands of what can't be told can be kept of, each by what it
  // refers to, the objects reached there, of a collection (Reached): its
  // object, and what its result can keep of its arguments (parameterKept).
  auto MayKeep = [&](const CallExpr *Call, const CEffects &Effects) {
    if (!Reached)
      return;
    const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Effects.Callee);
    const bool Unseen = !Effects.Callee ||
                        !CProvenance(Context).returnedRoots(Effects.Callee) ||
                        (Method && mayRunOverride(Call, Method));
    for (const COperandEffect &Effect : Effects.Operands)
      if (const auto *Operand = dyn_cast_or_null<Expr>(Effect.Operand);
          Operand &&
          !(Effect.Use == EUse::Copy &&
            holdsNoAddress(Operand->getType(), Context)) &&
          (Effect.Use == EUse::CallObject ||
           ((Effect.Use == EUse::Bind || Effect.Use == EUse::Copy) &&
            Effect.Into.Kind == EDestination::Parameter &&
            heldIn(Call, Effects, Effect.Into.Index, Operand, Context, Unseen)
                .Made)))
        Merge(
            storageOf(Operand,
                      (Effect.Use == EUse::Bind && !takenWhenGiven(Operand)) ||
                              Effect.Use == EUse::CallObject
                          ? 0
                          : heldAt(Operand),
                      IsObject, OwnAt, Context, Reached));
  };
  CProvenance Walk(Context);
  bool Ended = false;
  const bool Complete = Walk.reach(
      E, Level, [&](const Expr *At, unsigned Level, bool) {
        const CEffects &Effects = effectsOf(At, Context);
        // A value that holds no address, a number, leads nowhere: at any
        // level of a value, past the storage of a glvalue.
        if ((At->isPRValue() || Level > 0) && holdsNoAddress(At->getType(), Context))
          return EReach::Stop;
        // Nor does an object a call gives back by value, of a type that isn't
        // a view, which owns what it holds (the copy model), `CStr Name =
        // Launches.f_GetRepoName(Repo);`.
        // But of a body that can't be seen, also of an override or of a
        // callee that isn't named, one that can hold a pointer, a reference
        // or a view keeps what its arguments refer to by type
        // (parameterKept, Rule R), not its object.
        if (Effects.Statement == EStatement::Call && At->isPRValue() &&
            At->getType()->isRecordType() &&
            !isDefaultViewType(At->getType(), Context)) {
          const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Effects.Callee);
          if (Level > 0 && holdsBorrowed(At->getType(), Context) &&
              (!Effects.Callee || !Walk.returnedRoots(Effects.Callee) ||
               (Method && mayRunOverride(cast<CallExpr>(At), Method))))
            for (const COperandEffect &Effect : Effects.Operands)
              if (const auto *Operand = dyn_cast_or_null<Expr>(Effect.Operand);
                  Operand &&
                  (Effect.Use == EUse::Bind || Effect.Use == EUse::Copy) &&
                  Effect.Into.Kind == EDestination::Parameter &&
                  !(Effect.Use == EUse::Copy &&
                    holdsNoAddress(Operand->getType(), Context)) &&
                  heldIn(At, Effects, Effect.Into.Index, Operand, Context,
                         /*Unseen=*/true)
                      .Made)
                Merge(storageOf(Operand,
                                Effect.Use == EUse::Bind &&
                                        !takenWhenGiven(Operand)
                                    ? 0
                                    : heldAt(Operand),
                                IsObject, OwnAt, Context, Reached));
          return EReach::Stop;
        }
        // A copy or a move assignment the compiler defines copies as a copy
        // or a move constructor the compiler defines does, member by member
        // (copiedByDefinedAssignment): of a type whose copy doesn't share
        // what its source points to, it owns what it holds; else it holds
        // what its source holds, which the way goes on to.
        if (Effects.Statement == EStatement::Call && Level > 0 &&
            copiedByDefinedAssignment(At, Context))
          return copySharesPointees(At->getType(), Context) ? EReach::Continue
                                                            : EReach::Stop;
        // What provenance can't follow, a call or a construction of what can
        // refer to the object, can't be told; a copy the compiler defines is
        // where its source is.
        if (Effects.Statement == EStatement::Call &&
            (callUntold(cast<CallExpr>(At), Effects, Level, Walk) ||
             returnsUntold(cast<CallExpr>(At), Effects, Level, Walk))) {
          Of.bUnknown = true;
          MayKeep(cast<CallExpr>(At), Effects);
          return EReach::Root;
        }
        // What the constructor of an object keeps in it of what it is given
        // (parameterKept), at a level above 0: of a copy the compiler
        // defines, its source as the source holds it; of a reference, what
        // it designates, of a pointer, what it points to, a level down; of a
        // reference to an owner of which only what it owns is kept, and of
        // an object, what it holds, `m_Text(_pPage->m_Text, 64)`. Of one
        // whose body can't be seen, that can't be told.
        if (Effects.Statement == EStatement::Construction && Level > 0) {
          const auto *Construct = cast<CXXConstructExpr>(At);
          const CXXConstructorDecl *Constructor = Construct->getConstructor();
          // A copy or a move that doesn't share what its source points to
          // owns what it holds (the copy model), `CResult{.m_Repo = Repo}`.
          if (Constructor && Constructor->isCopyOrMoveConstructor() &&
              !copySharesPointees(Construct->getType(), Context))
            return EReach::Stop;
          if (Constructor && !Constructor->isUserProvided() &&
              Constructor->isCopyOrMoveConstructor() &&
              Construct->getNumArgs() > 0) {
            Merge(storageOf(Construct->getArg(0), Level, IsObject, OwnAt,
                            Context, Reached));
            return EReach::Root;
          }
          for (const COperandEffect &Effect : Effects.Operands) {
            const auto *Given = dyn_cast_or_null<Expr>(Effect.Operand);
            if (!Given ||
                (Effect.Use != EUse::Bind && Effect.Use != EUse::Copy) ||
                Effect.Into.Kind != EDestination::Parameter ||
                (Effect.Use == EUse::Copy && holdsNoAddress(Given->getType(), Context)))
              continue;
            const CParameterKept Kept =
                heldIn(Construct, Effects, Effect.Into.Index, Given, Context)
                    .Kept;
            if (!Kept.bObject)
              continue;
            Of.bUnknown |= Kept.bUnknown;
            Merge(storageOf(Given,
                            (Effect.Use == EUse::Bind && Kept.bOwnStorage) ||
                                    (Effect.Use != EUse::Bind &&
                                     Given->getType()->isPointerType())
                                ? Level - 1
                                : Level,
                            IsObject, OwnAt, Context, Reached));
          }
        }
        // What a static local that can be changed holds, which the function
        // can assign what is in the object, `s_pValue = &m_Value`, can't be
        // told.
        if (const auto *Static =
                Effects.Statement == EStatement::Name
                    ? dyn_cast<VarDecl>(cast<DeclRefExpr>(At)->getDecl())
                    : nullptr;
            Static && Level > 0 && Static->isStaticLocal() &&
            !Static->getType().isConstQualified()) {
          Of.bUnknown = Of.bCut = true;
          return EReach::Root;
        }
        // Of a record that isn't a view nor an owner, however it is reached,
        // what a pointer, reference or view member stored in it refers to,
        // and a copy of one held by value, is a value (recordPartAt), of
        // which nothing is followed.
        // What a pointer or a reference member whose target can't be told
        // refers to can't be told either.
        if (!IsObject(At)) {
          if (Effects.Statement != EStatement::Name &&
              Effects.Statement != EStatement::Member &&
              Effects.Statement != EStatement::Call)
            return EReach::Continue;
          const CRecordPart Part = recordPartAt(At, Level, Walk, Context);
          switch (Part.Kind) {
          case ERecordPart::Value:
            return EReach::Root;
          case ERecordPart::Assigned:
            if (llvm::any_of(memberValueRoots(Part.Field, Context).Roots,
                             [](const CStorageRoot &Root) {
                               return Root.Kind == ERootKind::Unknown ||
                                      Root.Kind == ERootKind::Object;
                             }))
              Of.bUnknown = true;
            return EReach::Root;
          case ERecordPart::Followed:
          case ERecordPart::Own:
          case ERecordPart::Owned:
            return EReach::Continue;
          }
          return EReach::Continue;
        }
        const bool Own = OwnAt(At, Effects, Level, Walk);
        (Own ? Of.bOwn : Of.bBorrowed) = true;
        if (Own && Reached)
          Reached->insert(At);
        return EReach::Root;
      },
      &Ended);
  Of.bUnknown |= !Complete;
  Of.bCut |= !Complete;
  Of.bElsewhere |= Ended;
  return Of;
}

// Whether the copies a closure keeps, the objects reached, \p At at \p Level,
// are their own storage there (objectStorageOf): of an object of a view
// (ViewTypes), what it points to is what it refers to, also through the
// pointer of a base that implements it, `TCStrImp_Ptr::m_pData` of
// `CStrPtr`, unless that is a temporary made with it (viewsOwnTemporary). Of
// `this`, the object. Below the object, at each level, the member the
// pointer loaded is in, the last of what the way takes there: borrowed, it
// leads elsewhere, also each of the object of a view, and the value of a
// pointer the object is, `_pValue` of `[_pValue]`; else what the object
// owns.
static bool closureOwnAt(const Expr *At, const CEffects &Effects,
                         unsigned Level, const CProvenance &Walk) {
  ASTContext &Context = Walk.context();
  QualType Object = At->getType().getNonReferenceType();
  if (At->isPRValue() && Object->isPointerType())
    Object = Object->getPointeeType();
  const bool ViewObject = isDefaultViewType(Object, Context) &&
                          !viewsOwnTemporary(At, Effects, Context);
  const llvm::ArrayRef<CProjection> Taken = Walk.projections();
  bool Borrowed = Level > 0 && (ViewObject || Object->isPointerType());
  for (unsigned iLevel = 1; iLevel <= Level && !Borrowed; ++iLevel)
    Borrowed = iLevel < Taken.size() && !Taken[iLevel].Path.empty() &&
               isBorrowedField(Taken[iLevel].Path.back());
  return !Borrowed;
}

CObjectStorage objectStorageOf(const Expr *E,
                               llvm::function_ref<bool(const Expr *)> IsObject,
                               ASTContext &Context, unsigned Level) {
  return storageOf(E, Level, IsObject, closureOwnAt, Context);
}

CObjectStorage
objectStorageOf(const Expr *E, llvm::function_ref<bool(const Expr *)> IsObject,
                llvm::function_ref<bool(const Expr *At, unsigned AtLevel,
                                        const CProvenance &Walk)>
                    OwnAt,
                ASTContext &Context, unsigned Level) {
  return storageOf(
      E, Level, IsObject,
      [&](const Expr *At, const CEffects &, unsigned AtLevel,
          const CProvenance &Walk) { return OwnAt(At, AtLevel, Walk); },
      Context);
}

CObjectStorage objectStorageOf(const Expr *E, const CXXMethodDecl *Method,
                               ASTContext &Context) {
  if (stackNearlyExhausted())
    return withStack([&] { return objectStorageOf(E, Method, Context); });
  // The object: `this`, or the explicit object parameter.
  return objectStorageOf(
      E,
      [&](const Expr *At) {
        const CEffects &Effects = effectsOf(At, Context);
        if (Effects.Statement == EStatement::This)
          return true;
        const auto *Explicit =
            Effects.Statement == EStatement::Name
                ? dyn_cast<ParmVarDecl>(cast<DeclRefExpr>(At)->getDecl())
                : nullptr;
        return Explicit && Method->isExplicitObjectMemberFunction() &&
               Explicit == Method->getParamDecl(0);
      },
      Context);
}

static bool readsOnlyOwnStorageUncached(const FunctionDecl *Definition);

namespace {
/// Of each member function, whether it reads only the storage of its object
/// (readsOnlyOwnStorage), once told: one that calls itself back reads what it
/// reads otherwise, which is taken of it while it is told.
struct COwnReads {
  TCSummaries<const FunctionDecl *, bool> Results{&readsOnlyOwnStorageUncached,
                                                  true, false};
};
} // namespace

bool readsOnlyOwnStorage(const CXXMethodDecl *Method, ASTContext &Context) {
  const FunctionDecl *Definition = Method ? Method->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody() ||
      isa<CoroutineBodyStmt>(Definition->getBody()))
    return false;
  return astCache<COwnReads>(Context).Results.get(Definition);
}

static bool readsOnlyOwnStorageUncached(const FunctionDecl *Definition) {
  ASTContext &Context = Definition->getASTContext();
  const auto *Own = dyn_cast<CXXMethodDecl>(Definition);
  if (!Own)
    return false;
  const Stmt *Body = Definition->getBody();
  const CEffectIndex &Index = sharedFlowCache().indexOf(Body, Context);
  bool Reads = true;
  forEachRunning(Definition, Body, Body, Context, [&](const Stmt *S) {
    if (!Reads)
      return;
    const CEffects *Effects = Index.effectsOf(S);
    for (const COperandEffect &Effect : Effects->Operands) {
      const auto *Operand = dyn_cast_or_null<Expr>(Effect.Operand);
      if (!Operand)
        continue;
      switch (Effect.Use) {
      // A load or a store: of the object's own storage, or of what isn't of
      // the object, a local or a global.
      case EUse::Access:
      case EUse::Update:
      case EUse::Write: {
        const CObjectStorage Of = objectStorageOf(Operand, Own, Context);
        Reads = !Of.bBorrowed && !Of.bUnknown;
        break;
      }
      // A member function of a part of it that reads only its own storage,
      // `CImp::f_GetStr()`, not one an override can run instead.
      case EUse::CallObject: {
        const auto *Called = dyn_cast_or_null<CXXMethodDecl>(Effects->Callee);
        const auto *Call = dyn_cast<CallExpr>(S);
        const CObjectStorage Of = objectStorageOf(Operand, Own, Context);
        Reads = Called && Call && !Of.bBorrowed && !Of.bUnknown && Of.bOwn &&
                !mayRunOverride(Call, Called) &&
                readsOnlyOwnStorage(Called, Context);
        break;
      }
      // What a function is given of the object that can refer to it, which
      // it can read through; not its own storage of only values, which
      // leads nowhere else, `fg_Min(m_Length, umint(4096))` by reference.
      case EUse::Bind:
      case EUse::Copy:
        if (Effect.Into.Kind == EDestination::Parameter &&
            (Effect.Use == EUse::Bind || Operand->getType()->isPointerType() ||
             Operand->getType()->isRecordType())) {
          const CObjectStorage Of = objectStorageOf(Operand, Own, Context);
          Reads = !Of.bBorrowed && !Of.bUnknown &&
                  (!Of.bOwn ||
                   (Operand->isGLValue() &&
                    !holdsReference(Operand->getType().getNonReferenceType())));
        }
        break;
      case EUse::Opaque:
        Reads = false;
        break;
      case EUse::Forward:
      case EUse::Dereference:
      case EUse::AddressOf:
      case EUse::Callee:
      case EUse::Condition:
      case EUse::Value:
      case EUse::Discarded:
      case EUse::Statement:
      case EUse::Unevaluated:
        break;
      }
      if (!Reads)
        return;
    }
  });
  return Reads;
}

QualType closureFieldType(const LambdaExpr *Lambda,
                          const LambdaCapture &Capture) {
  const CXXRecordDecl *Class = Lambda->getLambdaClass();
  if (!Class)
    return QualType();
  auto Field = Class->field_begin();
  for (const LambdaCapture &Each : Lambda->captures()) {
    if (Field == Class->field_end())
      break;
    if (&Each == &Capture)
      return Field->getType();
    ++Field;
  }
  return QualType();
}

bool readsOnlyOwnPart(const Stmt *S, const ValueDecl *Copy, QualType Held,
                      ASTContext &Context) {
  const auto *Ref = dyn_cast_or_null<DeclRefExpr>(S);
  if (!Ref || !Copy || Ref->getDecl() != Copy || Held.isNull())
    return false;
  const QualType Type = Held;
  auto OnlyTested = [&](const Expr *Pointer) {
    return isOnlyTestedPointer(Pointer, Context);
  };
  // A pointer held by value reads only itself, which is in the frame or the
  // closure.
  if (Type->isPointerType())
    return OnlyTested(Ref);
  if (Type->isReferenceType() || !Type->isRecordType())
    return false;
  // The part of it used, a member of a member by value, `_Source.m_Text`,
  // each in the frame; only what a part holds that can refer elsewhere, a
  // pointer, a reference or a view (holdsReference), leads to the borrowed
  // data. By the effect table: what gives it on as it is (EUse::Forward),
  // parentheses, casts to a base, is the same part; of the operands of a
  // parent, the one that is the part past the implicit nodes a parent map
  // that ignores them skips (TK_IgnoreUnlessSpelledInSource).
  const Expr *Part = Ref;
  for (int Depth = 0; Depth < 64; ++Depth) {
    const auto Parents = instantiatedParents(*Part, Context);
    const Stmt *Up = Parents.empty() ? nullptr : Parents[0].get<Stmt>();
    if (!Up)
      return false;
    // The object of a member function, with an implicit or an explicit
    // object alike (receiverCallOf): not one an override can run instead,
    // which can read what the view refers to (mayRunOverride). A reference
    // it gives into the object's own storage (callResultOf),
    // `f_GetPosition()` of `return m_Position;`, is a part of it, by what is
    // used of it. Else what it gives is a value of its own, a number or a
    // record of only values; or a pointer, `f_GetStr()` of `return
    // m_pStr;`, also through another, `return CImp::f_GetStr();`, compared
    // or tested. What it is given is used where it is made, by itself,
    // `f_HasBytes(4)`.
    if (const CMemberCall Call = receiverCallOf(Parents[0], Part, Context)) {
      const CXXMethodDecl *Method = Call->getMethodDecl();
      const QualType Gives = Method ? Method->getReturnType() : QualType();
      if (!Method || mayRunOverride(Call, Method))
        return false;
      const CCallResult Result = callResultOf(Call.call(), 0, Context);
      if (Gives->isReferenceType() && Result.bObject &&
          Result.Kind == ECallResult::OwnStorage) {
        const QualType Referred = Gives.getNonReferenceType();
        if (!holdsReference(Referred))
          return true;
        if (!Referred->isRecordType())
          return false;
        Part = Call.call();
        continue;
      }
      if (!((!Gives->isPointerType() && !holdsReference(Gives)) ||
            (Gives->isPointerType() && OnlyTested(Call))))
        return false;
      return readsOnlyOwnStorage(Method, Context);
    }
    const CEffects &Effects = effectsOf(Up, Context);
    const auto Use =
        llvm::find_if(Effects.Operands, [&](const COperandEffect &Effect) {
          const auto *Operand = dyn_cast_or_null<Expr>(Effect.Operand);
          return Operand &&
                 (Operand == Part || Operand->IgnoreImplicit() == Part);
        });
    if (Use == Effects.Operands.end() || Use->Use != EUse::Forward)
      return false;
    if (Effects.Statement != EStatement::Member) {
      // A compound statement gives on its last statement as the value of the
      // statement expression it is in, `({ Copy; })`, and drops it in any
      // other, which reads nothing; it isn't an expression.
      if (const auto *Compound = dyn_cast<CompoundStmt>(Up)) {
        const auto Above = instantiatedParents(*Compound, Context);
        Part = Above.empty() ? nullptr : Above[0].get<StmtExpr>();
        if (!Part)
          return true;
        continue;
      }
      Part = dyn_cast<Expr>(Up);
      if (!Part)
        return false;
      continue;
    }
    const auto *Member = cast<MemberExpr>(Up);
    if (const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl())) {
      const QualType Type = Field->getType();
      // Also a pointer it holds, compared or tested, `_Span.m_pStr ==
      // _Other.m_pStr`, which doesn't read what it points to.
      if (Type->isPointerType())
        return OnlyTested(Member);
      // A value of its own is in the frame, whatever is done with it: read,
      // updated, `Text.m_Length = 0`, a reference bound to it, `auto &nBytes
      // = _Span.m_nBytes` or `CStr::CFormat("{}") << _Span.m_nBytes` with a
      // `const &` parameter, or its address taken; also a record of only
      // values, `_Span.m_Info`.
      if (!holdsReference(Type))
        return true;
      // A record by value that holds a reference, a view, `_Source.m_Text`,
      // by what is used of it.
      if (Type->isReferenceType() || !Type->isRecordType())
        return false;
      Part = Member;
      continue;
    }
    return false;
  }
  return false;
}

llvm::SmallVector<const Expr *, 2> awaitedValues(const Expr *Operand) {
  llvm::SmallVector<const Expr *, 2> Values;
  // Each local followed once, whose values are then already there, also one
  // that others are made from many times, `auto A1 = b ? A0 : A0;`; one met
  // again while it is followed is made from itself.
  llvm::DenseMap<const VarDecl *, bool> Followed;
  TCRecursive<void(const Expr *)> Visit = [&](const Expr *E) {
    if (!E) {
      Values.push_back(nullptr);
      return;
    }
    E = skipWrappers(E)->IgnoreParenImpCasts();
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E)) {
      // Only the one a constant condition chooses, `true ? fg_OnResume(A) :
      // fg_OnResume(B)`, told in the context of what it names.
      const ASTContext *Context = nullptr;
      TCRecursive<void(const Stmt *)> Find = [&](const Stmt *S) {
        if (!S || Context)
          return;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S)) {
          Context = &Ref->getDecl()->getASTContext();
          return;
        }
        for (const Stmt *Child : keptChildren(S))
          Find(Child);
      };
      Find(Conditional);
      bool Chosen = false;
      if (const auto *Choice = dyn_cast<ConditionalOperator>(Conditional);
          Choice && Context && !Choice->getCond()->isValueDependent() &&
          foldsCondition(Choice->getCond(), Chosen, *Context))
        return Visit(Chosen ? Choice->getTrueExpr() : Choice->getFalseExpr());
      for (const Expr *Arm : keptArms(Conditional))
        Visit(Arm);
      return;
    }
    if (const auto *Comma = dyn_cast<BinaryOperator>(E);
        Comma && Comma->getOpcode() == BO_Comma) {
      Visit(Comma->getRHS());
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
    Visit(assignedValueAt(Local, Ref));
    Followed[Local] = true;
  };
  Visit(Operand);
  return Values;
}

llvm::SmallVector<const CallExpr *, 2> onResumeCalls(const Expr *Operand) {
  llvm::SmallVector<const CallExpr *, 2> Calls;
  for (const Expr *Value : awaitedValues(Operand)) {
    // Also what a function whose body can be seen gives back, `co_await
    // f_Watch()` of `auto f_Watch() { return fg_OnResume(...); }`, through a
    // few of them, each value it can give back: one that isn't a
    // registration, or can't be followed, is null.
    TCRecursive<void(const Expr *, int)> Add = [&](const Expr *Given,
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
  TCRecursive<void(const Expr *, int)> Visit = [&](const Expr *E,
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
          foldsCondition(Conditional->getCond(), Chosen, *Context)) {
        Visit(Chosen ? Conditional->getTrueExpr() : Conditional->getFalseExpr(),
              Depth + 1);
        return;
      }
      for (const Expr *Arm : keptArms(Conditional))
        Visit(Arm, Depth + 1);
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
  if (!Call || isMemberCall(Call) || isa<CXXOperatorCallExpr>(Call))
    return nullptr;
  const StringRef Name = calleeName(Call);
  return !Name.empty() && llvm::is_contained(Names, Name) ? Call : nullptr;
}

const Stmt *firstCaptureUseAfterSuspension(const CXXMethodDecl *CallOperator,
                                           ASTContext &Context,
                                           CLifetimeUse *Found) {
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
  // Not where what a use keeps is alive in the frame of the call, of a
  // closure that isn't kept alive while it runs (Rule K): what a copy owns
  // that a local keeper keeps, `auto pKeep = pJob; auto *pValue =
  // &pJob->m_Value;`; the copies themselves are gone with the closure.
  Roots.AskKeepers = true;
  Roots.ClosureKept = false;
  Roots.Found = Found;
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

bool keepsObjectAlive(const LambdaExpr *Lambda, const Stmt *At) {
  const CXXRecordDecl *Class = enclosingThisClass(Lambda);
  if (!Class)
    return false;
  ASTContext &Context = Class->getASTContext();
  return isAlive(objectRootOf(Class),
                 At ? useContext(At, Context) : closureUse(Lambda, Context),
                 Context);
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
  if (const CMemberCall Member = memberCallOf(Call)) {
    const CXXMethodDecl *Method = Member->getMethodDecl();
    return Method && Method->getIdentifier() ? identifierTextOf(Method) : StringRef();
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
  if (!Call || isMemberOrOperatorCall(Call) ||
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
    if (!Call || isMemberOrOperatorCall(Call))
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
static bool onlyLocalCalledSafelyUncached(const VarDecl *Variable);

static bool onlyLocalCalledSafely(const VarDecl *Variable) {
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return false;
  // Taken not to be while it is told, which one made from itself can't be.
  struct CCalledSafely {
    TCSummaries<const VarDecl *, bool> Decided{&onlyLocalCalledSafelyUncached,
                                               false, false};
  };
  return astCache<CCalledSafely>(Variable->getASTContext())
      .Decided.get(Variable);
}

static bool onlyLocalCalledSafelyUncached(const VarDecl *Variable) {
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return false;
  ASTContext &Context = Variable->getASTContext();
  using namespace ast_matchers;
  const auto Refs =
      matchEach<DeclRefExpr>(declRefExpr(to(varDecl(equalsNode(Variable))),
                                         unless(isInUnevaluatedContext()))
                                 .bind("ref"),
                             *Function->getBody(), Context);
  return llvm::all_of(Refs, [&](const BoundNodes &Nodes) {
    const auto *Ref = Nodes.getNodeAs<DeclRefExpr>("ref");
    if (Ref->refersToEnclosingVariableOrCapture())
      return false;
    if (isCallSafeFunction(Ref, Context))
      return true;
    const VarDecl *Copy = initializedLocal(Ref, Context);
    return Copy && onlyLocalCalledSafely(Copy);
  });
}

/// Whether \p MakePointer, which makes a shared pointer to a closure, is only
/// given to fg_CallSafe: directly, or through locals that are.
static bool onlyCalledSafely(const CallExpr *MakePointer, ASTContext &Context) {
  if (isCallSafeFunction(MakePointer, Context))
    return true;
  const VarDecl *Variable = initializedLocal(MakePointer, Context);
  return Variable && onlyLocalCalledSafely(Variable);
}

llvm::SmallVector<const DeclRefExpr *, 2>
referencesIn(const Stmt *Scope, const ValueDecl *Decl, ASTContext &Context) {
  struct CReferences {
    llvm::DenseMap<
        const Stmt *,
        llvm::DenseMap<const ValueDecl *,
                       llvm::SmallVector<const DeclRefExpr *, 2>>>
        Scopes;
  };
  auto &Scopes = astCache<CReferences>(Context).Scopes;
  auto [Index, Inserted] = Scopes.try_emplace(Scope);
  if (Inserted) {
    auto &References = Index->second;
    TCRecursive<void(const Stmt *)> Collect = [&](const Stmt *S) {
      if (!S || isUnevaluatedNode(S))
        return;
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
        References[Ref->getDecl()].push_back(Ref);
      for (const Stmt *Each : keptChildren(S))
        Collect(Each);
    };
    Collect(Scope);
  }
  return Index->second.lookup(Decl);
}

bool writtenIn(const ValueDecl *Variable, const Stmt *Scope,
               ASTContext &Context, const Expr *Except) {
  if (!Variable || !Scope)
    return false;
  // A reference or pointer to what isn't const lets what it is bound to or
  // given to write it.
  auto NonConstReference = [](QualType Type) {
    return Type->isLValueReferenceType() &&
           !Type.getNonReferenceType().isConstQualified();
  };
  auto ArgumentWritten = [&](const FunctionDecl *Callee,
                             llvm::ArrayRef<const Expr *> Arguments,
                             unsigned Shift, const Expr *Part) {
    for (unsigned iArgument = 0; iArgument < Arguments.size(); ++iArgument) {
      if (Arguments[iArgument] != Part)
        continue;
      if (!Callee || iArgument < Shift ||
          iArgument - Shift >= Callee->getNumParams())
        return false;
      return NonConstReference(
          Callee->getParamDecl(iArgument - Shift)->getType());
    }
    return false;
  };
  for (const DeclRefExpr *Ref : referencesIn(Scope, Variable, Context)) {
    if (Ref == Except)
      continue;
    // From the name up through what designates it or a part of it: a
    // member, an element or a base.
    const Expr *Part = Ref;
    DynTypedNode Node = DynTypedNode::create(*Ref);
    bool Written = false;
    for (int Up = 0; Up < 64; ++Up) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        break;
      if (const auto *Alias = Parents[0].get<VarDecl>()) {
        Written = NonConstReference(Alias->getType());
        break;
      }
      const auto *Parent = Parents[0].get<Expr>();
      if (!Parent)
        break;
      const auto *Cast = dyn_cast<ImplicitCastExpr>(Parent);
      const auto *Member = dyn_cast<MemberExpr>(Parent);
      const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Parent);
      if (isa<ParenExpr>(Parent) ||
          (Cast && (Cast->getCastKind() == CK_NoOp ||
                    Cast->getCastKind() == CK_DerivedToBase ||
                    Cast->getCastKind() == CK_UncheckedDerivedToBase ||
                    Cast->getCastKind() == CK_ArrayToPointerDecay)) ||
          (Member && !Member->isArrow() && Member->getBase() == Part &&
           isa<FieldDecl>(Member->getMemberDecl())) ||
          (Subscript && Subscript->getBase() == Part)) {
        Part = Parent;
        Node = Parents[0];
        continue;
      }
      // A member function of it that isn't const, `fRead.f_Clear()`.
      if (Member && !Member->isArrow() && Member->getBase() == Part) {
        const auto *Method = dyn_cast<CXXMethodDecl>(Member->getMemberDecl());
        Written = Method && Method->isInstance() && !Method->isConst();
        break;
      }
      if (const auto *Binary = dyn_cast<BinaryOperator>(Parent)) {
        Written = Binary->isAssignmentOp() && Binary->getLHS() == Part;
        break;
      }
      if (const auto *Unary = dyn_cast<UnaryOperator>(Parent)) {
        Written = Unary->isIncrementDecrementOp() ||
                  (Unary->getOpcode() == UO_AddrOf &&
                   Unary->getType()->isPointerType() &&
                   !pointsToConst(Unary->getType()));
        break;
      }
      if (const auto *Call = dyn_cast<CallExpr>(Parent)) {
        // What gives back what it is given as an xvalue, `fg_Move(fRead)`,
        // moves it, which isn't a write of it.
        if (Call->isXValue() && Call->getNumArgs() == 1)
          break;
        const auto *Callee = Call->getDirectCallee();
        const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
        const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
        // The object of a member operator, `fRead = nullptr`.
        if (Operator && Method && Method->isInstance() &&
            Operator->getNumArgs() >= 1 && Operator->getArg(0) == Part) {
          Written = !Method->isConst();
          break;
        }
        Written = ArgumentWritten(
            Callee, llvm::ArrayRef(Call->getArgs(), Call->getNumArgs()),
            Operator && Method && Method->isInstance() ? 1 : 0, Part);
        break;
      }
      if (const auto *Construct = dyn_cast<CXXConstructExpr>(Parent)) {
        Written = ArgumentWritten(
            Construct->getConstructor(),
            llvm::ArrayRef(Construct->getArgs(), Construct->getNumArgs()), 0,
            Part);
        break;
      }
      break;
    }
    if (Written)
      return true;
  }
  return false;
}

/// Whether \p Site, a direct call of a closure (ClosureUse::DirectCall), calls
/// it through \p Name itself, `fRun()` of the name `fRun`.
static bool callsThrough(const Expr *Site, const DeclRefExpr *Name,
                         const ASTContext &Context) {
  const Expr *Object = nullptr;
  if (const auto *Call = dyn_cast<CallExpr>(Site))
    Object = Call->getNumArgs() > 0 ? Call->getArg(0) : nullptr;
  else if (const auto *Member = dyn_cast<MemberExpr>(Site))
    Object = Member->getBase();
  return Object && sameValue(Object, Context) == Name;
}

static void addClosureConsumers(const Expr *E, const CXXRecordDecl *Closure,
                                ASTContext &Context,
                                llvm::ArrayRef<StringRef> SafeCallees,
                                llvm::SmallVectorImpl<ClosureConsumer> &Consumers,
                                int Depth) {
  if (stackNearlyExhausted())
    return withStack([&] { addClosureConsumers(E, Closure, Context, SafeCallees, Consumers, Depth); });
  auto Add = [&](ClosureUse Use, const Expr *Site, std::string Target = {}) {
    Consumers.push_back({Use, Site, std::move(Target)});
  };
  auto Quoted = [](StringRef Name) { return ("'" + Name + "'").str(); };
  // A local that holds the closure, its own type or a function wrapper that
  // holds only it (H1), keeps it: where it goes is where each name of the
  // local goes, through as many local copies as the code has, `auto fCopy =
  // fWork;`, each looked at once.
  auto FollowHolder = [&](const VarDecl *Variable, const Expr *Child,
                          llvm::ArrayRef<const DeclRefExpr *> Names = {},
                          llvm::function_ref<bool(const DeclRefExpr *)>
                              Reached = nullptr) {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
    static thread_local TCSummaryLocal<llvm::SmallPtrSet<const VarDecl *, 8>>
        s_Copies;
    if (Depth == 0)
      s_Copies->clear();
    // A local of static storage duration is never destroyed, nor what it
    // copies into itself: one whose closure captures only copies can be
    // called anywhere; its copies are followed.
    const bool bLasting =
        Variable->isStaticLocal() &&
        Variable->getTLSKind() == VarDecl::TLS_None &&
        llvm::all_of(Closure->captures(), [](const LambdaCapture &Capture) {
          return Capture.getCaptureKind() == LCK_ByCopy;
        });
    if ((!Variable->hasLocalStorage() && !bLasting) || !Function ||
        !Function->getBody() || Depth >= 64) {
      Add(ClosureUse::Escape, Child, Quoted(identifierTextOf(Variable)));
      return;
    }
    if (!s_Copies->insert(Variable).second)
      return;
    // The outermost lambda in the function that \p Ref is in, or captures
    // it with, and whether \p Ref is in what it captures, not its body.
    auto CapturingLambda =
        [&](const DeclRefExpr *Ref) -> std::pair<const LambdaExpr *, bool> {
      const LambdaExpr *Outermost = nullptr;
      bool InCaptures = false;
      DynTypedNode Node = DynTypedNode::create(*Ref);
      for (int Up = 0; Up < 256; ++Up) {
        const auto Parents = instantiatedParents(Node, Context);
        if (Parents.empty())
          break;
        const DynTypedNode Below = Node;
        Node = Parents[0];
        // The function the local is in, also the body of a lambda, ends the
        // walk.
        if (Node.get<Stmt>() == Function->getBody())
          break;
        if (const auto *Lambda = Node.get<LambdaExpr>()) {
          if (Lambda->getCallOperator() == Function)
            break;
          Outermost = Lambda;
          InCaptures = Below.get<Stmt>() != Lambda->getBody();
        }
        if (Node.get<FunctionDecl>())
          break;
      }
      return {Outermost, InCaptures};
    };
    // A lambda that captures the local holds it, a value like any other:
    // where it goes is followed by the same walk. One that is only called
    // in the scope, `[&] { ... }()` or `fg_CallSafe([&fRead] { return
    // fRead(); })`, runs here: its capture is nothing, and each name of the
    // local in its body goes on. Another one can run later; it is reported
    // once.
    llvm::DenseMap<const LambdaExpr *, bool> InScope;
    llvm::SmallVector<const DeclRefExpr *, 8> Own;
    if (Names.empty())
      llvm::append_range(Own,
                         referencesIn(Function->getBody(), Variable, Context));
    for (const DeclRefExpr *Ref : Names.empty() ? llvm::ArrayRef(Own) : Names) {
      if (Reached && !Reached(Ref))
        continue;
      if (const auto [Capturing, InCaptures] = CapturingLambda(Ref);
          Capturing) {
        auto [Found, Inserted] = InScope.try_emplace(Capturing, false);
        if (Inserted) {
          llvm::SmallVector<ClosureConsumer, 2> Nested;
          addClosureConsumers(Capturing, Capturing->getLambdaClass(), Context,
                              SafeCallees, Nested, Depth + 1);
          Found->second = isCalledInScope(Nested);
          if (!Found->second)
            Add(ClosureUse::Escape, Capturing, "a lambda that captures it");
        }
        if (!Found->second || InCaptures)
          continue;
      }
      const size_t nBefore = Consumers.size();
      addClosureConsumers(Ref, Closure, Context, SafeCallees, Consumers,
                          Depth + 1);
      // Called through the lasting local itself, it outlives the call.
      if (bLasting)
        Consumers.erase(
            std::remove_if(Consumers.begin() + nBefore, Consumers.end(),
                           [&](const ClosureConsumer &Consumer) {
                             return Consumer.Use == ClosureUse::DirectCall &&
                                    callsThrough(Consumer.Site, Ref, Context);
                           }),
            Consumers.end());
    }
  };
  // Whether \p Variable is a function wrapper, which can hold the closure.
  auto IsWrapper = [&](const VarDecl *Variable) {
    const StringRef Name = recordName(Variable->getType());
    return Name.starts_with("TCFunction") || Name == "function";
  };
  // Whether the local function wrapper \p Variable holds one closure over its
  // whole scope (H1): it isn't written but by \p Assigned, the one assignment
  // to a default-constructed one; else which closure a use of it sees can't
  // be told, and nothing is said of it (H2).
  auto HoldsOnlyIt = [&](const VarDecl *Variable, const Expr *Assigned) {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
    if (!Function || !Function->getBody())
      return false;
    if (Assigned) {
      const auto *Default = dyn_cast_or_null<CXXConstructExpr>(
          Variable->getInit() ? Variable->getInit()->IgnoreImplicit()
                              : nullptr);
      if (Variable->getInit() && (!Default || Default->getNumArgs() != 0))
        return false;
    }
    return !writtenIn(Variable, Function->getBody(), Context, Assigned);
  };
  // Of the local function wrapper \p Variable, given the closure by \p
  // Definition, its declaration or an assignment to it, the names it reaches
  // (wrapperFlow): each assignment and clear of it, also through a local
  // reference bound to it, `fAlias = ...`, ends what it holds, so a name
  // only other definitions reach holds another value, and a name that is
  // the object of a definition writes it and isn't a use; those that can
  // hold the closure are followed, also through its references. False where
  // the definitions of it can't be told: a closure that refers to it gives it
  // a value or clears it.
  auto FollowWrapper = [&](const VarDecl *Variable, const Stmt *Definition,
                           const Expr *Child) {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
    if (!Function || !Function->getBody() || !Definition)
      return false;
    const CWrapperFlow &Flow = sharedFlowCache().wrapperFlow(
        Function, Function->getBody(), Variable, Context);
    const std::optional<unsigned> Index =
        Flow.bTold ? Flow.definitionOf(Definition) : std::nullopt;
    if (!Index)
      return false;
    const CEffectIndex &Effects =
        sharedFlowCache().indexOf(Function->getBody(), Context);
    bool Used = false;
    FollowHolder(Variable, Child, Flow.Names, [&](const DeclRefExpr *Ref) {
      if (const auto It = Flow.Reaching.find(Ref);
          It != Flow.Reaching.end() && !It->second.test(*Index))
        return false;
      const bool Reached = llvm::none_of(
          Effects.consumersOf(Ref), [&](const CEffectIndex::CUse &Use) {
            return Use.Parent && Flow.definitionOf(Use.Parent).has_value();
          });
      Used |= Reached;
      return Reached;
    });
    // Replaced or cleared before any use, it is destroyed without running:
    // nothing is said of it.
    if (!Used)
      Add(ClosureUse::Untold, Child);
    return true;
  };
  // The statement that declares \p Variable, its definition of the wrapper
  // flow.
  auto DeclarationOf = [&](const VarDecl *Variable) -> const Stmt * {
    for (const DynTypedNode &Parent : instantiatedParents(*Variable, Context))
      if (const auto *Declaration = Parent.get<DeclStmt>())
        return Declaration;
    return nullptr;
  };
  // Whether \p Type can hold the closure: a pointer, a reference or a view
  // (holdsBorrowed), the closure, or a function wrapper.
  auto CanHold = [&](QualType Type) {
    const StringRef Name = recordName(Type);
    return holdsBorrowed(Type, Context) || isClosureOf(Type, Closure) ||
           Name.starts_with("TCFunction") || Name == "function";
  };
  // The local holder \p Target names, of a type that holds the closure.
  auto NamedHolder = [&](const Expr *Target) -> const VarDecl * {
    const auto *Ref =
        Target ? dyn_cast<DeclRefExpr>(Target->IgnoreParenImpCasts()) : nullptr;
    const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (!Variable || !Variable->hasLocalStorage() ||
        Variable->getType()->isReferenceType())
      return nullptr;
    return isClosureOf(Variable->getType(), Closure) || IsWrapper(Variable)
               ? Variable
               : nullptr;
  };
  // Of an assignment \p Assignment of the closure to \p Target: kept in a
  // local wrapper, followed from there to the names it reaches
  // (FollowWrapper), also through a local reference bound to it, `fAlias =
  // [...]` of `auto &fAlias = fRead`; where those can't be told, followed
  // where the wrapper holds only it (H1), else what is done with it can't be
  // told; false of another target.
  auto AssignedToLocal = [&](const Expr *Target, const Expr *Assignment,
                             const Expr *Child) {
    const auto *Ref =
        Target ? dyn_cast<DeclRefExpr>(Target->IgnoreParenImpCasts()) : nullptr;
    const auto *Alias = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (Alias && Alias->hasLocalStorage() &&
        Alias->getType()->isLValueReferenceType() && Alias->getInit())
      if (const auto *Bound =
              dyn_cast<DeclRefExpr>(Alias->getInit()->IgnoreParenImpCasts());
          Bound && NamedHolder(Bound)) {
        const VarDecl *Referent = NamedHolder(Bound);
        if (!IsWrapper(Referent) ||
            !FollowWrapper(Referent, Assignment, Child))
          Add(ClosureUse::Untold, Child);
        return true;
      }
    const VarDecl *Holder = NamedHolder(Target);
    if (!Holder)
      return false;
    if (!IsWrapper(Holder))
      FollowHolder(Holder, Child);
    else if (!FollowWrapper(Holder, Assignment, Child)) {
      if (HoldsOnlyIt(Holder, Ref))
        FollowHolder(Holder, Child);
      else
        Add(ClosureUse::Untold, Child);
    }
    return true;
  };
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
      // In a template, the type of `auto fLambda = [...]` can be left
      // undeduced.
      const LambdaExpr *Initializer =
          Variable->getInit() ? asLambda(Variable->getInit()) : nullptr;
      // A local function wrapper made from it, like `TCFunctionMovable<…>
      // fLambda = [...]`, or moved or copied from a local that holds it,
      // `auto fCopy = fg_Move(fRead)`, holds it too, and an unchanged local
      // reference to one, `auto &fAlias = fLambda`, which asLambda follows,
      // refers to it.
      const bool HoldsClosure =
          isClosureOf(Variable->getType(), Closure) ||
          (Variable->getType()->getContainedAutoType() &&
           Initializer && Initializer->getLambdaClass() == Closure) ||
          (IsWrapper(Variable) && !Variable->getType()->isReferenceType());
      // A variable of another type, like a `TCFunction`, is made from it.
      if (!HoldsClosure) {
        const StringRef Name = recordName(Variable->getType());
        if (!Name.empty() && llvm::is_contained(SafeCallees, Name))
          Add(ClosureUse::SafeCallee, Child);
        else
          Add(ClosureUse::Escape, Child,
              Name.empty() ? Quoted(identifierTextOf(Variable)) : "a " + Quoted(Name));
        return;
      }
      // A wrapper is followed to the names its declaration reaches
      // (FollowWrapper); where those can't be told, where it holds only
      // the closure (H1), else what is done with it can't be told.
      if (!IsWrapper(Variable))
        FollowHolder(Variable, Child);
      else if (!FollowWrapper(Variable, DeclarationOf(Variable), Child)) {
        if (HoldsOnlyIt(Variable, nullptr))
          FollowHolder(Variable, Child);
        else
          Add(ClosureUse::Untold, Child);
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
    // A use of a named holder that neither calls it nor gives it away, a
    // const member function of it whose result can't hold it, a presence
    // check, `if (fRead)` or `fRead.f_IsEmpty()`, is nothing.
    if (const auto *Callee = Depth > 0 ? dyn_cast<MemberExpr>(ParentExpr)
                                       : nullptr;
        Callee && isOperand(Callee->getBase(), Child))
      if (const auto *Method = dyn_cast<CXXMethodDecl>(Callee->getMemberDecl());
          Method && Method->isConst() &&
          Method->getOverloadedOperator() != OO_Call &&
          !CanHold(Method->getReturnType()))
        return;
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
        // An assignment to a named holder keeps it there, like initializing
        // it; to another object, like a member `TCFunction`, an escape.
        if (Operator->getOperator() == OO_Equal &&
            Operator->getNumArgs() == 2 && isOperand(Operator->getArg(1), Child)) {
          if (AssignedToLocal(Operator->getArg(0), Operator, Child))
            return;
          const StringRef Name = recordName(Operator->getArg(0)->getType());
          Add(ClosureUse::Escape, Operator,
              Name.empty() ? "another object" : "a " + Quoted(Name));
          return;
        }
      }
      // The same operators called explicitly, `g_Dispatch.operator / (Lambda)`.
      if (const CMemberCall Member = memberCallOf(Call)) {
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
            identifierTextOf(Method) == "f_DispatchWithReturn" &&
            Method->getParent()->getIdentifier() &&
            identifierTextOf(Method->getParent()) == "CActor" &&
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
    // An assignment to a named holder keeps it there; to another object, an
    // escape.
    if (const auto *Binary = dyn_cast<BinaryOperator>(ParentExpr);
        Binary && Binary->getOpcode() == BO_Assign) {
      // Assigning over what holds it destroys it without calling it.
      if (isOperand(Binary->getLHS(), Child))
        return;
      if (isOperand(Binary->getRHS(), Child) &&
          AssignedToLocal(Binary->getLHS(), Binary, Child))
        return;
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
           Consumer.Use == ClosureUse::CallSafe ||
           Consumer.Use == ClosureUse::Untold;
  });
}

namespace {
/// Of each function whose body lambdaOfCallOperator searched, the lambda
/// expressions in it, nested ones too, by closure class: the first one met
/// in the search's order.
struct CLambdasOfClass {
  llvm::DenseMap<const FunctionDecl *,
                 llvm::DenseMap<const CXXRecordDecl *, const LambdaExpr *>>
      ByFunction;
};
} // namespace

/// The lambda expression whose call operator is \p Method.
const LambdaExpr *lambdaOfCallOperator(const CXXMethodDecl *Method,
                                              ASTContext &Context) {
  // The call operator of a generic lambda is instantiated from the one the
  // lambda expression has.
  if (const auto *Pattern = dyn_cast_or_null<CXXMethodDecl>(
          Method->getTemplateInstantiationPattern()))
    Method = Pattern;
  const Stmt *Body = Method->getBody();
  if (!Body)
    return nullptr;
  for (const DynTypedNode &Parent : instantiatedParents(*Body, Context))
    if (const auto *Lambda = Parent.get<LambdaExpr>();
        Lambda && Lambda->getLambdaClass() == Method->getParent())
      return Lambda;
  // The body of a generic lambda's call operator template isn't always mapped
  // to the lambda expression, which the function around it has: found among
  // the lambda expressions of that function's body, which one search records
  // for every closure of the body.
  const auto *Around =
      dyn_cast_or_null<FunctionDecl>(Method->getParent()->getDeclContext());
  if (!Around || !Around->getBody())
    return nullptr;
  auto [It, New] =
      astCache<CLambdasOfClass>(Context).ByFunction.try_emplace(Around);
  if (New) {
    using namespace ast_matchers;
    for (const BoundNodes &Nodes :
         match(findAll(lambdaExpr().bind("lambda")), *Around->getBody(),
               Context)) {
      const auto *Lambda = Nodes.getNodeAs<LambdaExpr>("lambda");
      It->second.try_emplace(Lambda->getLambdaClass(), Lambda);
    }
  }
  return It->second.lookup(Method->getParent());
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

/// Whether \p Lambda is what the function it is made in returns, `return
/// [&]() -> TCFuture<int> { ... };`, past what gives it on as it is, a
/// conversion or a copy: it runs after that function returned, wherever what
/// it returns goes, not while the function runs.
static bool returnedByMaker(const LambdaExpr *Lambda, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Lambda);
  for (int Step = 0; Step < 64; ++Step) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    if (Parents[0].get<ReturnStmt>())
      return true;
    if (!Parents[0].get<Expr>() || Parents[0].get<CallExpr>())
      return false;
    Node = Parents[0];
  }
  return false;
}

/// The function whose body \p S is in: the call operator of the innermost
/// lambda whose body has it, else the function.
static const FunctionDecl *functionAround(const Stmt *S, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*S);
  for (int Step = 0; Step < 4096; ++Step) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    if (const auto *Lambda = Parents[0].get<LambdaExpr>();
        Lambda && Node.get<Stmt>() == Lambda->getBody())
      return Lambda->getCallOperator();
    if (const auto *Function = Parents[0].get<FunctionDecl>())
      return Function;
    Node = Parents[0];
  }
  return nullptr;
}

/// Whether \p Variable, or the object of `this` when it is null, lives while
/// the function that makes \p Lambda runs: through a lambda in between that
/// returns it (returnedByMaker), only where each call of that lambda is in
/// the function around it, which then has what it returns.
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
    // A lambda the one in between returns runs after that one returned,
    // where what it returns goes: each call of the one in between, which the
    // walk goes on from only where it is in the function around that one;
    // one called in another function, which can run it later, `auto Restore
    // = co_await fg_AsyncDestroy(fMakeHook(Location));` in another lambda,
    // can't be told.
    if (returnedByMaker(Current, Context)) {
      const FunctionDecl *Around = enclosingFunction(Outer);
      const auto Calls = closureConsumers(Outer, Context, {});
      if (!Around || Calls.empty() ||
          llvm::any_of(Calls, [&](const ClosureConsumer &Call) {
            const FunctionDecl *In = functionAround(Call.Site, Context);
            return Call.Use != ClosureUse::DirectCall || !In ||
                   In->getCanonicalDecl() != Around->getCanonicalDecl();
          }))
        return false;
    }
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
  // A reference init capture, like `[&Alias = Value]`, is a capture by
  // reference of what its initializer designates, whatever that is: of a
  // variable, also through a member of it, `[&Alias = Data.m_Member]`, an
  // element of an array, or what a call gives of its object or its first
  // argument (callResultOf), `Items.f_GetFirst()` or `Items[1]`, that
  // variable; else, what a call gives, `[&ThreadLocal =
  // fg_ConcurrencyThreadLocal()]`, what a pointer points to, a value of a
  // stored member, as `[&Alias]` of a local reference bound to it is: there
  // while the function that makes the lambda runs.
  for (int Depth = 0; Depth < 4; ++Depth) {
    const auto *Capture = dyn_cast<VarDecl>(Variable);
    // A capture by value, like `[Value = 1]`, is in the closure of its lambda.
    if (!Capture || !Capture->isInitCapture() ||
        !Capture->getType()->isReferenceType())
      break;
    const Expr *Init =
        Capture->getInit() ? skipWrappers(Capture->getInit()) : nullptr;
    for (;;) {
      if (const auto *Member = dyn_cast_or_null<MemberExpr>(Init)) {
        // A member of the object of the function, `[&Alias = m_Value]`, lives
        // as long as the object.
        if (Member->isArrow())
          return !isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()) ||
                 isAliveWhileRunning(nullptr, Lambda, Context);
        Init = skipWrappers(Member->getBase());
        continue;
      }
      if (const auto *Subscript = dyn_cast_or_null<ArraySubscriptExpr>(Init);
          Subscript && Subscript->getBase()->IgnoreParenImpCasts()
                           ->getType()->isArrayType()) {
        Init = skipWrappers(Subscript->getBase()->IgnoreParenImpCasts());
        continue;
      }
      if (const CMemberCall Call = memberCallOf(Init, /*Operators=*/true);
          Call && Call->isLValue()) {
        const CCallResult Gives = callResultOf(Call.call(), 0, Context);
        if (Gives.bObject &&
            (Gives.Kind == ECallResult::OwnStorage ||
             Gives.Kind == ECallResult::Owned ||
             Gives.Kind == ECallResult::Given) &&
            !Gives.Of->IgnoreParenImpCasts()->getType()->isPointerType()) {
          Init = skipWrappers(Gives.Of->IgnoreParenImpCasts());
          continue;
        }
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
      return true;
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
         (identifierTextOf(Ref->getDecl()) == "g_Dispatch" ||
          identifierTextOf(Ref->getDecl()) == "g_ConcurrentDispatch" ||
          identifierTextOf(Ref->getDecl()) == "g_OnScopeExitActor");
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
    if (Param->getIdentifier() && identifierTextOf(Param).starts_with("_f"))
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
  // The dispatched function is the first argument, past the object of a
  // member function with an explicit object (CMemberCall); the others are its
  // arguments.
  const CMemberCall Bind = memberCallOf(Call);
  const Expr *Dispatched =
      Bind ? (Bind->getNumArgs() > 0 ? Bind->getArg(0) : nullptr)
           : (Call->getNumArgs() > 0 ? Call->getArg(0) : nullptr);
  if (!Dispatched || !isOperand(Dispatched, Child))
    return false;
  // The bound function is `f_DispatchWithReturn`, a template argument of the
  // member function as it is named, `.f_Bind<...>`, which with an explicit
  // object clang names by a reference to it.
  const Expr *Callee = Call->getCallee()->IgnoreParenImpCasts();
  llvm::ArrayRef<TemplateArgumentLoc> Arguments;
  if (const auto *Member = dyn_cast<MemberExpr>(Callee))
    Arguments = Member->template_arguments();
  else if (const auto *Named = dyn_cast<DeclRefExpr>(Callee);
           Named && Bind && Bind->isExplicitObject())
    Arguments = Named->template_arguments();
  if (Arguments.empty())
    return false;
  // `CActor::f_DispatchWithReturn` itself, not a function of the same name
  // of another class.
  auto IsDispatchWithReturn = [](const NamedDecl *Decl) {
    const auto *Method = dyn_cast_or_null<CXXMethodDecl>(
        Decl ? Decl->getUnderlyingDecl()->getAsFunction() : nullptr);
    return Method && Method->getIdentifier() &&
           identifierTextOf(Method) == "f_DispatchWithReturn" &&
           Method->getParent()->getIdentifier() &&
           identifierTextOf(Method->getParent()) == "CActor";
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
      identifierTextOf(Ref->getDecl()) == "g_ConcurrentDispatch")
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
  return identifierTextOf(Type) == "COnScopeExitHelper" ||
         identifierTextOf(Type) == "COnScopeExitHelperWithException" ||
         identifierTextOf(Type) == "COnScopeExitCatchHelper" ||
         identifierTextOf(Type) == "COnScopeExitSharedHelper" ||
         identifierTextOf(Type) == "COnScopeExitSharedWithExceptionHelper" ||
         (AlsoActor && identifierTextOf(Type) == "COnScopeExitActorHelper");
}

const LambdaExpr *scopeExitOperand(const Expr *E) {
  if (stackNearlyExhausted())
    return withStack([&] { return scopeExitOperand(E); });
  if (!E)
    return nullptr;
  E = skipWrappers(E)->IgnoreParenImpCasts();
  // Either value of a conditional, when both make it of the same lambda,
  // `bChoose ? g_OnScopeExit / fg_Move(fCleanup) : g_OnScopeExit /
  // fg_Move(fCleanup)`.
  if (const auto *Conditional = dyn_cast<ConditionalOperator>(E)) {
    const llvm::SmallVector<const Expr *, 2> Arms = keptArms(Conditional);
    const LambdaExpr *First = scopeExitOperand(Arms.front());
    return llvm::all_of(Arms,
                        [&](const Expr *Arm) {
                          return scopeExitOperand(Arm) == First;
                        })
               ? First
               : nullptr;
  }
  if (const auto *Divide = dyn_cast<CXXOperatorCallExpr>(E);
      Divide && Divide->getOperator() == OO_Slash && Divide->getNumArgs() == 2)
    return isScopeExitHelper(Divide->getArg(0)) ? asLambda(Divide->getArg(1))
                                                : nullptr;
  if (const CMemberCall Divide = memberCallOf(E);
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
  if (stackNearlyExhausted())
    return withStack([&] { return scopeExitLambda(Local); });
  static thread_local TCSummaryLocal<int> s_Depth;
  if (!Local || !Local->getInit() || *s_Depth > 64)
    return nullptr;
  const Expr *Init = skipWrappers(Local->getInit())->IgnoreParenImpCasts();
  // Moved from another guard, `auto Moved = fg_Move(Cleanup)`, also through a
  // local reference to it, `fg_Move(Alias)`, which then runs the callback.
  if (const auto *Move = dyn_cast<CallExpr>(Init);
      Move && Move->getNumArgs() == 1 && (isMoveFunction(calleeName(Move))))
    if (const VarDecl *Moved = cleanupHolderOf(Move->getArg(0));
        Moved && Moved != Local && Moved->hasLocalStorage() &&
        !Moved->getType()->isReferenceType()) {
      ++*s_Depth;
      const LambdaExpr *Lambda = scopeExitLambda(Moved);
      --*s_Depth;
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
      ++*s_Depth;
      const LambdaExpr *Lambda = scopeExitLambda(Copied);
      --*s_Depth;
      return Lambda;
    }
  return scopeExitOperand(Init);
}

// Whether \p S, a statement, cancels the guard of a scope exit \p Guard,
// which then doesn't run its closure where it ends: `Guard.f_Clear()` or
// `Guard.f_ClearFunctor()`.
bool cancelsGuard(const Stmt *S, const VarDecl *Guard) {
  const CMemberCall Clear = isa_and_nonnull<Expr>(S)
                          ? memberCallOf(
                                cast<Expr>(S)->IgnoreImplicit())
                          : CMemberCall();
  const auto *Object =
      Clear && Clear->getImplicitObjectArgument()
          ? dyn_cast<DeclRefExpr>(
                Clear->getImplicitObjectArgument()->IgnoreParenImpCasts())
          : nullptr;
  return Object && Object->getDecl() == Guard && Clear->getMethodDecl() &&
         Clear->getMethodDecl()->getIdentifier() &&
         (identifierTextOf(Clear->getMethodDecl()) == "f_Clear" ||
          identifierTextOf(Clear->getMethodDecl()) == "f_ClearFunctor");
}

bool isClearedBeforeLeaving(const VarDecl *Guard, const CompoundStmt *Block,
                            llvm::function_ref<bool(const Stmt *)> Leaves) {
  if (!Guard || !Block)
    return false;
  TCRecursive<bool(const Stmt *)> NamesGuard = [&](const Stmt *S) {
    if (const auto *Ref = dyn_cast_or_null<DeclRefExpr>(S);
        Ref && Ref->getDecl() == Guard)
      return true;
    return S && llvm::any_of(keptChildren(S), NamesGuard);
  };
  bool After = false;
  for (const Stmt *Statement : Block->body()) {
    if (!After) {
      if (const auto *Declaration = dyn_cast<DeclStmt>(Statement))
        After = llvm::is_contained(Declaration->decls(), Guard);
      continue;
    }
    if (cancelsGuard(Statement, Guard))
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

namespace {
/// The specializations of a class template that are in memory, without
/// loading those of a precompiled header that the compile hasn't used, which
/// specializations() loads.
struct CClassTemplateInMemory : ClassTemplateDecl {
  static auto &specializations(const ClassTemplateDecl *Template) {
    return (Template->*&CClassTemplateInMemory::getCommonPtr)()
        ->Specializations;
  }
};

/// The same of a function template.
struct CFunctionTemplateInMemory : FunctionTemplateDecl {
  static auto &specializations(const FunctionTemplateDecl *Template) {
    return (Template->*&CFunctionTemplateInMemory::getCommonPtr)()
        ->Specializations;
  }
};

/// Walks the declarations whose functions can override: of classes, also of
/// the specializations of templates and in functions, not the statements of
/// their bodies, from \p Roots, the translation unit or declarations of
/// what it walks. Gives \p Method each function, and \p Entered each context
/// and template. With \p bInMemory, only what is in memory, without loading
/// the declarations of a precompiled header, and only the functions that
/// aren't from it.
void walkOverrides(llvm::ArrayRef<const Decl *> Roots, bool bInMemory,
                   llvm::function_ref<void(const CXXMethodDecl *)> Method,
                   llvm::function_ref<void(const Decl *)> Entered) {
  llvm::SmallVector<const DeclContext *, 64> Work;
  llvm::SmallPtrSet<const DeclContext *, 32> Visited;
  auto Add = [&](const DeclContext *Inner) {
    if (Inner && Visited.insert(Inner).second) {
      Work.push_back(Inner);
      Entered(cast<Decl>(Inner));
    }
  };
  auto Visit = [&](const Decl *D) {
    if (const auto *Overrider = dyn_cast<CXXMethodDecl>(D);
        Overrider && !(bInMemory && Overrider->isFromASTFile()))
      Method(Overrider);
    if (const auto *Template = dyn_cast<ClassTemplateDecl>(D)) {
      Entered(Template);
      Add(Template->getTemplatedDecl());
      if (bInMemory)
        for (ClassTemplateSpecializationDecl &Specialization :
             CClassTemplateInMemory::specializations(Template))
          Add(Specialization.getMostRecentDecl());
      else
        for (const ClassTemplateSpecializationDecl *Specialization :
             Template->specializations())
          Add(Specialization);
    } else if (const auto *Function = dyn_cast<FunctionTemplateDecl>(D)) {
      Entered(Function);
      // Also its instantiations, whose local classes, `struct CDerived :
      // t_CBase` in `fg_Make<CApp>()`, are of what they are made with.
      Add(Function->getTemplatedDecl());
      if (bInMemory)
        for (FunctionTemplateSpecializationInfo &Specialization :
             CFunctionTemplateInMemory::specializations(Function))
          Add(Specialization.getFunction()->getMostRecentDecl());
      else
        for (const FunctionDecl *Specialization : Function->specializations())
          Add(Specialization);
    }
    // Also of the classes of functions, `struct CDerived : CBase {}` in a
    // body, which are declared in it.
    else if (isa<NamespaceDecl, CXXRecordDecl, LinkageSpecDecl, ExportDecl,
                 FunctionDecl>(D))
      Add(cast<DeclContext>(D));
  };
  for (const Decl *Root : Roots)
    if (const auto *Unit = dyn_cast<TranslationUnitDecl>(Root))
      Add(Unit);
    else
      Visit(Root);
  while (!Work.empty()) {
    const DeclContext *Current = Work.pop_back_val();
    for (const Decl *D :
         bInMemory ? Current->noload_decls() : Current->decls())
      Visit(D);
  }
}

/// The record codes of the overrides a precompiled header stores.
enum EOverridesRecord : unsigned {
  /// Pairs of the IDs of an overridden function's first declaration and an
  /// overriding declaration, as little-endian 32-bit numbers.
  OverridesRecord_Pairs = serialization::FIRST_EXTENSION_RECORD_ID,
  /// The IDs of the declaration contexts and templates the walk entered.
  OverridesRecord_Contexts,
};

class COverridesReader;

/// The readers of the overrides of precompiled headers, by the AST they are
/// read into, each while its reader exists.
llvm::DenseMap<const ASTContext *, const COverridesReader *> &
overridesReaders() {
  static llvm::DenseMap<const ASTContext *, const COverridesReader *> s_Readers;
  return s_Readers;
}

/// The overrides a precompiled header stores: the walk of what it declares.
class COverridesReader : public ModuleFileExtensionReader {
public:
  COverridesReader(ModuleFileExtension *Extension, ASTReader &InReader,
                   serialization::ModuleFile &InFile,
                   const llvm::BitstreamCursor &InStream)
      : ModuleFileExtensionReader(Extension), Reader(InReader), File(InFile),
        Context(&InReader.getContext()) {
    llvm::BitstreamCursor Stream = InStream;
    llvm::SmallVector<uint64_t, 4> Record;
    bool bPairs = false;
    bool bContexts = false;
    while (true) {
      llvm::Expected<llvm::BitstreamEntry> Entry =
          Stream.advanceSkippingSubblocks();
      if (!Entry) {
        llvm::consumeError(Entry.takeError());
        return;
      }
      if (Entry->Kind != llvm::BitstreamEntry::Record)
        break;
      Record.clear();
      StringRef Blob;
      llvm::Expected<unsigned> Code =
          Stream.readRecord(Entry->ID, Record, &Blob);
      if (!Code) {
        llvm::consumeError(Code.takeError());
        return;
      }
      if (Blob.size() % 4)
        return;
      if (*Code == OverridesRecord_Pairs && Blob.size() % 8 == 0) {
        Pairs = Blob;
        bPairs = true;
      } else if (*Code == OverridesRecord_Contexts) {
        Contexts = Blob;
        bContexts = true;
      }
    }
    bValid = bPairs && bContexts;
    overridesReaders()[Context] = this;
  }

  ~COverridesReader() override {
    auto &Readers = overridesReaders();
    if (auto It = Readers.find(Context);
        It != Readers.end() && It->second == this)
      Readers.erase(It);
  }

  /// Whether the AST of \p Of has this precompiled header and no other AST
  /// file, so that what it stores and what the compile declares are all.
  bool covers(const ASTContext &Of) const {
    return bValid && &Of == Context &&
           Of.getExternalSource() ==
               static_cast<ExternalASTSource *>(&Reader) &&
           Reader.getModuleManager().size() == 1 &&
           File.Kind == serialization::MK_PCH;
  }

  /// The number of pairs and contexts.
  size_t pairs() const { return Pairs.size() / 8; }
  size_t contexts() const { return Contexts.size() / 4; }

  /// The global ID of the \p iEntry th number of \p Blob.
  GlobalDeclID id(StringRef Blob, size_t iEntry) const {
    const uint32_t Local =
        llvm::support::endian::read32le(Blob.data() + 4 * iEntry);
    return Reader.getGlobalDeclID(File, LocalDeclID::get(Reader, File, Local));
  }
  GlobalDeclID overridden(size_t iPair) const { return id(Pairs, 2 * iPair); }
  GlobalDeclID overrider(size_t iPair) const {
    return id(Pairs, 2 * iPair + 1);
  }
  GlobalDeclID context(size_t iContext) const { return id(Contexts, iContext); }

  ASTReader &Reader;

private:
  serialization::ModuleFile &File;
  const ASTContext *Context;
  StringRef Pairs;
  StringRef Contexts;
  bool bValid = false;
};

/// Writes the overrides of a precompiled header, the walk of what it
/// declares, so that a compile that uses it doesn't load all of it to find
/// them.
class COverridesWriter : public ModuleFileExtensionWriter {
public:
  COverridesWriter(ModuleFileExtension *Extension, ASTWriter &InWriter)
      : ModuleFileExtensionWriter(Extension), Writer(InWriter) {}

  void writeExtensionContents(Sema &SemaRef,
                              llvm::BitstreamWriter &Stream) override {
    const ASTContext &Context = SemaRef.getASTContext();
    // Not of one that itself uses another AST file, whose declarations it
    // doesn't write: a compile with it walks the whole translation unit.
    if (const ASTReader *Chain = Writer.getChain();
        Chain && Chain->getModuleManager().size())
      return;
    llvm::SmallString<0> Pairs;
    llvm::SmallString<0> Contexts;
    bool bWritten = true;
    // The ID of a declaration this file writes, not one of those the reader
    // makes itself, like the translation unit.
    auto Append = [&](llvm::SmallString<0> &To, const Decl *D) {
      if (D->isFromASTFile() || !Writer.wasDeclEmitted(D)) {
        bWritten = false;
        return false;
      }
      const LocalDeclID ID = Writer.getDeclID(D);
      if (ID.getRawValue() < NUM_PREDEF_DECL_IDS)
        return false;
      if (ID.getRawValue() > std::numeric_limits<uint32_t>::max()) {
        bWritten = false;
        return false;
      }
      char Bytes[4];
      llvm::support::endian::write32le(Bytes, ID.getRawValue());
      To.append(Bytes, Bytes + 4);
      return true;
    };
    walkOverrides(
        {Context.getTranslationUnitDecl()}, /*bInMemory=*/false,
        [&](const CXXMethodDecl *Overrider) {
          for (const CXXMethodDecl *Overridden :
               Overrider->overridden_methods()) {
            const size_t Size = Pairs.size();
            if (!Append(Pairs, Overridden->getCanonicalDecl()) ||
                !Append(Pairs, Overrider))
              Pairs.resize(Size);
          }
        },
        [&](const Decl *Entered) { Append(Contexts, Entered); });
    if (!bWritten)
      return;
    auto Abbreviation = std::make_shared<llvm::BitCodeAbbrev>();
    Abbreviation->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::Fixed, 8));
    Abbreviation->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::Blob));
    const unsigned Abbrev = Stream.EmitAbbrev(std::move(Abbreviation));
    uint64_t PairsRecord[] = {OverridesRecord_Pairs};
    Stream.EmitRecordWithBlob(Abbrev, PairsRecord, Pairs);
    uint64_t ContextsRecord[] = {OverridesRecord_Contexts};
    Stream.EmitRecordWithBlob(Abbrev, ContextsRecord, Contexts);
  }

private:
  ASTWriter &Writer;
};

/// Stores the overrides of what a precompiled header declares in it.
class COverridesExtension
    : public llvm::RTTIExtends<COverridesExtension, ModuleFileExtension> {
public:
  static char ID;

  ModuleFileExtensionMetadata getExtensionMetadata() const override {
    return {"malterlib-overrides", 1, 0, ""};
  }

  std::unique_ptr<ModuleFileExtensionWriter>
  createExtensionWriter(ASTWriter &Writer) override {
    return std::make_unique<COverridesWriter>(this, Writer);
  }

  std::unique_ptr<ModuleFileExtensionReader>
  createExtensionReader(const ModuleFileExtensionMetadata &Metadata,
                        ASTReader &Reader, serialization::ModuleFile &File,
                        const llvm::BitstreamCursor &Stream) override {
    // One of another version is left unread: the compile walks the whole
    // translation unit.
    if (Metadata.MajorVersion != 1 || Metadata.MinorVersion != 0)
      return nullptr;
    return std::make_unique<COverridesReader>(this, Reader, File, Stream);
  }
};

char COverridesExtension::ID = 0;

[[maybe_unused]] const bool s_bOverridesExtension = [] {
  moduleFileExtensions().push_back(std::make_shared<COverridesExtension>());
  return true;
}();

/// The overrides of the virtual functions of an AST, by what they override,
/// found once.
struct COverrideFacts {
  bool Found = false;
  /// By the first declaration of what they override: all of them, or, with a
  /// precompiled header that stores its own, those the compile declares.
  llvm::DenseMap<const CXXMethodDecl *,
                 llvm::SmallVector<const CXXMethodDecl *, 2>>
      Overriders;
  /// The precompiled header's, by the ID of what they override.
  const COverridesReader *pStored = nullptr;
  llvm::DenseMap<GlobalDeclID, llvm::SmallVector<GlobalDeclID, 2>> Stored;
  /// One declaration of each override of a function, in their order
  /// (orderedOverriders).
  llvm::DenseMap<const CXXMethodDecl *,
                 llvm::SmallVector<const CXXMethodDecl *, 2>>
      Ordered;
};

/// Adds to \p Overriders the pairs of \p Overrider and each function it
/// overrides.
void addOverrider(
    llvm::DenseMap<const CXXMethodDecl *,
                   llvm::SmallVector<const CXXMethodDecl *, 2>> &Overriders,
    const CXXMethodDecl *Overrider) {
  for (const CXXMethodDecl *Overridden : Overrider->overridden_methods())
    Overriders[Overridden->getCanonicalDecl()].push_back(Overrider);
}

/// Stops the compile at the first override that the walk of the whole
/// translation unit and the precompiled header's with the compile's don't
/// both find (MALTERLIB_TIDY_CHECK_OVERRIDES).
void checkOverrides(const ASTContext &Context, const COverrideFacts &Facts) {
  using CPair = std::pair<const CXXMethodDecl *, const CXXMethodDecl *>;
  llvm::SetVector<CPair> Indexed;
  size_t nDeclared = 0;
  for (const auto &[Overridden, Overriders] : Facts.Overriders)
    for (const CXXMethodDecl *Overrider : Overriders) {
      Indexed.insert({Overridden, Overrider});
      ++nDeclared;
    }
  ASTReader &Reader = Facts.pStored->Reader;
  for (const auto &[Overridden, Overriders] : Facts.Stored)
    for (const GlobalDeclID Overrider : Overriders)
      if (const auto *Base =
              dyn_cast_or_null<CXXMethodDecl>(Reader.GetDecl(Overridden)))
        if (const auto *Derived =
                dyn_cast_or_null<CXXMethodDecl>(Reader.GetDecl(Overrider)))
          Indexed.insert({Base, Derived});
  llvm::SetVector<CPair> Walked;
  walkOverrides(
      {Context.getTranslationUnitDecl()}, /*bInMemory=*/false,
      [&](const CXXMethodDecl *Overrider) {
        for (const CXXMethodDecl *Overridden : Overrider->overridden_methods())
          Walked.insert({Overridden->getCanonicalDecl(), Overrider});
      },
      [](const Decl *) {});
  const SourceManager &SM = Context.getSourceManager();
  auto Describe = [&](const CXXMethodDecl *Method) {
    return Method->getQualifiedNameAsString() + " at " +
           Method->getLocation().printToString(SM);
  };
  auto Stop = [&](const CPair &Pair, StringRef Finder, StringRef Other) {
    llvm::report_fatal_error(llvm::Twine("malterlib overrides: ") + Finder +
                                 " finds " + Describe(Pair.second) +
                                 " overriding " + Describe(Pair.first) + ", " +
                                 Other + " doesn't",
                             /*gen_crash_diag=*/false);
  };
  for (const CPair &Pair : Walked)
    if (!Indexed.contains(Pair))
      Stop(Pair, "the walk of the translation unit",
           "the precompiled header's index with the compile's");
  // The first of the source that the walk doesn't find, as the index is in
  // the order of its maps.
  llvm::SmallVector<CPair, 2> Unwalked;
  for (const CPair &Pair : Indexed)
    if (!Walked.contains(Pair))
      Unwalked.push_back(Pair);
  auto Before = [&](const CXXMethodDecl *Left, const CXXMethodDecl *Right) {
    if (isBeforeInSource(Left->getLocation(), Right->getLocation(), SM))
      return true;
    if (isBeforeInSource(Right->getLocation(), Left->getLocation(), SM))
      return false;
    return Left->getID() < Right->getID();
  };
  if (!Unwalked.empty())
    Stop(*llvm::min_element(Unwalked,
                            [&](const CPair &Left, const CPair &Right) {
                              if (Left.second != Right.second)
                                return Before(Left.second, Right.second);
                              return Before(Left.first, Right.first);
                            }),
         "the precompiled header's index with the compile's",
         "the walk of the translation unit");
  llvm::errs() << "overrides: " << Facts.pStored->pairs()
               << " stored by the precompiled header, " << nDeclared
               << " declared in the compile, the same as the walk of the "
                  "translation unit\n";
}

/// Finds the overrides of \p Context: those a precompiled header stores with
/// those the compile declares, or else by the walk of the whole translation
/// unit, which loads all a precompiled header declares.
void findOverrides(const ASTContext &Context, COverrideFacts &Facts) {
  Facts.Found = true;
  const Decl *Unit = Context.getTranslationUnitDecl();
  auto Add = [&](const CXXMethodDecl *Overrider) {
    addOverrider(Facts.Overriders, Overrider);
  };
  const auto Reader = overridesReaders().find(&Context);
  if (Reader == overridesReaders().end() || !Reader->second->covers(Context)) {
    walkOverrides({Unit}, /*bInMemory=*/false, Add, [](const Decl *) {});
    return;
  }
  const COverridesReader &Stored = *Reader->second;
  Facts.pStored = &Stored;
  // What the compile declares: in what it declares itself, and in the
  // declarations of the header it has read, also what it adds to them, like
  // the members and specializations of what it instantiates, which are among
  // those the header's walk entered.
  llvm::SmallVector<const Decl *, 64> Roots{Unit};
  for (size_t iContext = 0; iContext < Stored.contexts(); ++iContext)
    if (const Decl *Read =
            Stored.Reader.GetExistingDecl(Stored.context(iContext)))
      Roots.push_back(Read);
  walkOverrides(Roots, /*bInMemory=*/true, Add, [](const Decl *) {});
  for (size_t iPair = 0; iPair < Stored.pairs(); ++iPair)
    Facts.Stored[Stored.overridden(iPair)].push_back(Stored.overrider(iPair));
  if (std::getenv("MALTERLIB_TIDY_CHECK_OVERRIDES"))
    checkOverrides(Context, Facts);
}

/// One declaration of each override of \p Overridden, in the order of their
/// places in the source, which is the same however they were found: the
/// definition when it is among those found, else the first by its place.
llvm::SmallVector<const CXXMethodDecl *, 2>
orderedOverriders(COverrideFacts &Facts, const CXXMethodDecl *Overridden) {
  if (const auto It = Facts.Ordered.find(Overridden); It != Facts.Ordered.end())
    return It->second;
  llvm::SmallVector<const CXXMethodDecl *, 2> Found =
      Facts.Overriders.lookup(Overridden);
  if (Facts.pStored && Overridden->isFromASTFile())
    if (const auto It = Facts.Stored.find(Overridden->getGlobalID());
        It != Facts.Stored.end())
      for (const GlobalDeclID Overrider : It->second)
        if (const auto *Derived = dyn_cast_or_null<CXXMethodDecl>(
                Facts.pStored->Reader.GetDecl(Overrider)))
          Found.push_back(Derived);
  // The place of a declaration: its file, line and column, and the class it
  // is in, which tells apart the instantiations of one template.
  using CPlace = std::tuple<std::string, unsigned, unsigned, std::string>;
  const ASTContext &Context = Overridden->getASTContext();
  const SourceManager &SM = Context.getSourceManager();
  // Given by value: telling the place of another method can add an entry,
  // which moves the others.
  llvm::DenseMap<const CXXMethodDecl *, CPlace> Places;
  auto PlaceOf = [&](const CXXMethodDecl *Method) -> CPlace {
    auto [It, bNew] = Places.try_emplace(Method);
    if (bNew) {
      const PresumedLoc Presumed =
          SM.getPresumedLoc(SM.getFileLoc(Method->getLocation()));
      std::string Class;
      llvm::raw_string_ostream Stream(Class);
      Method->getParent()->getNameForDiagnostic(
          Stream, Context.getPrintingPolicy(), /*Qualified=*/true);
      It->second = Presumed.isValid()
                       ? CPlace(Presumed.getFilename(), Presumed.getLine(),
                                Presumed.getColumn(), Class)
                       : CPlace("", 0, 0, Class);
    }
    return It->second;
  };
  llvm::MapVector<const CXXMethodDecl *, const CXXMethodDecl *> Chosen;
  for (const CXXMethodDecl *Overrider : Found) {
    auto [It, bNew] =
        Chosen.try_emplace(Overrider->getCanonicalDecl(), Overrider);
    if (!bNew && !It->second->isThisDeclarationADefinition() &&
        (Overrider->isThisDeclarationADefinition() ||
         PlaceOf(Overrider) < PlaceOf(It->second)))
      It->second = Overrider;
  }
  llvm::SmallVector<const CXXMethodDecl *, 2> Ordered;
  for (const auto &[Canonical, Overrider] : Chosen)
    Ordered.push_back(Overrider);
  if (Ordered.size() > 1)
    llvm::stable_sort(
        Ordered, [&](const CXXMethodDecl *Left, const CXXMethodDecl *Right) {
          return PlaceOf(Left) < PlaceOf(Right);
        });
  Facts.Ordered[Overridden] = Ordered;
  return Ordered;
}
} // namespace

llvm::SmallVector<const CXXMethodDecl *, 4>
dispatchedMethods(CMemberCall Call) {
  llvm::SmallVector<const CXXMethodDecl *, 4> Methods;
  const CXXMethodDecl *Method = Call->getMethodDecl();
  if (!Method)
    return Methods;
  Methods.push_back(Method);
  // Not a call that names the function, `CBase::f_Run()`, nor of one that
  // can't be overridden.
  if (!Method->isVirtual() || Call->hasQualifier() ||
      Method->hasAttr<FinalAttr>() || Method->getParent()->hasAttr<FinalAttr>())
    return Methods;
  // Of an object whose class is exactly what it is, `Source.f_Get()` of a
  // local `CSource Source`, its final overrider only (exactReceiverClass).
  if (const CXXRecordDecl *Exact = exactReceiverClass(Call)) {
    if (const CXXMethodDecl *Final = Method->getCorrespondingMethodInClass(Exact))
      Methods.front() = Final;
    return Methods;
  }
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
  if (!Facts.Found)
    findOverrides(Context, Facts);
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
         orderedOverriders(Facts, Methods[iMethod]->getCanonicalDecl()))
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
  TCRecursive<void(const Stmt *)> Visit = [&](const Stmt *S) {
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
  // The body the call runs: of a generic factory, its call operator
  // instantiated, whose lambdas have types, not the template's.
  const FunctionDecl *Called = Call->getDirectCallee();
  if (!Called || !Called->getBody())
    return nullptr;
  Visit(Called->getBody());
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
  if (const CMemberCall Clear = memberCallOf(S);
      Clear && Clear->getMethodDecl() &&
      ((Clear->getMethodDecl()->getIdentifier() &&
        (identifierTextOf(Clear->getMethodDecl()) == "f_Clear" ||
         identifierTextOf(Clear->getMethodDecl()) == "f_ClearFunctor")) ||
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
        Callee && Callee->getIdentifier() ? identifierTextOf(Callee) : "";
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
           argumentShift(Call, Call->getDirectCallee()), Call);
  else if (const auto *Construct = dyn_cast<CXXConstructExpr>(S))
    Disarm(Construct->getConstructor(),
           llvm::ArrayRef(Construct->getArgs(), Construct->getNumArgs()), 0);
  // An early await of the guard, `co_await fg_Move(Cleanup)`, also wrapped,
  // `co_await fg_Move(Cleanup).f_Wrap()`, runs the cleanup here.
  const auto *Await = dyn_cast<CoroutineSuspendExpr>(S);
  if (!Await)
    return;
  const Expr *Operand = skipWrappers(Await->getOperand())->IgnoreParenImpCasts();
  if (const CMemberCall Wrap = memberCallOf(Operand);
      Wrap && Wrap->getMethodDecl() && Wrap->getMethodDecl()->getIdentifier() &&
      identifierTextOf(Wrap->getMethodDecl()) == "f_Wrap")
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
      else if (const CMemberCall Member = memberCallOf(Call))
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
  TCRecursive<bool(const CompoundStmt *, int)> Add =
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
          TCRecursive<void(const Stmt *)> FindLeave = [&](const Stmt *S) {
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
            for (const Stmt *Child : keptChildren(S))
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
  if (stackNearlyExhausted())
    return withStack([&] { return reassignedBefore(Body, Variable, Read, SM, Replaces); });
  // How deep it is in the lambdas it looks into, and what each of them
  // decides in the outermost call, once, also one that is called again and
  // again, `fStep(); fStep();`, which would take exponential time.
  static thread_local TCSummaryLocal<int> s_Depth;
  static thread_local TCSummaryLocal<llvm::DenseMap<const Stmt *, bool>>
      s_Decided;
  if (*s_Depth == 0)
    s_Decided->clear();
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
        if (const auto Decided = s_Decided->find(Called->getBody());
            Decided != s_Decided->end())
          Reassigns = Decided->second;
        else if (*s_Depth < 16) {
          (*s_Decided)[Called->getBody()] = false;
          ++*s_Depth;
          Reassigns = reassignedBefore(Called->getBody(), Variable, nullptr,
                                       SM, Replaces);
          --*s_Depth;
          (*s_Decided)[Called->getBody()] = Reassigns;
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

namespace {
/// Of the AST a thread analyzes, whether each statement asked of is in the
/// block of a `try` (insideTryBlock).
struct CTryFacts {
  llvm::DenseMap<const Stmt *, bool> Inside;
};
} // namespace

bool insideTryBlock(const Stmt *S, ASTContext &Context) {
  auto &Inside = astCache<CTryFacts>(Context).Inside;
  if (auto It = Inside.find(S); It != Inside.end())
    return It->second;
  bool Found = false;
  DynTypedNode Node = DynTypedNode::create(*S);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      break;
    if (const auto *Try = Parents[0].get<CXXTryStmt>();
        Try && Node.get<Stmt>() == Try->getTryBlock()) {
      Found = true;
      break;
    }
    if (Parents[0].get<LambdaExpr>() || Parents[0].get<FunctionDecl>())
      break;
    Node = Parents[0];
  }
  return Inside[S] = Found;
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
  TCRecursive<void(const Expr *, int)> Collect = [&](const Expr *E,
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
      for (const Expr *Arm : keptArms(Conditional))
        Collect(Arm, Depth + 1);
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
      TCRecursive<void(const Expr *, int)> Targets = [&](const Expr *Value,
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
          for (const Expr *Arm : keptArms(Conditional))
            Targets(Arm, Level + 1);
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
