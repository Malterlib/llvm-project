//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEUTILS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEUTILS_H

#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "NamingUtils.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace clang {
class CFG;
class CFGBlock;
} // namespace clang

namespace clang::tidy::malterlib {

/// The functions that register an asynchronous cleanup for when a coroutine
/// scope ends. Awaiting them does not suspend.
inline constexpr llvm::StringLiteral AsyncDestroyFunctions[] = {
    "fg_AsyncDestroy", "fg_AsyncDestroyByValue", "fg_AsyncDestroyGeneric",
    "fg_AsyncDestroyLogError"};

/// The blocks of \p Graph that can run: those reachable from its entry,
/// indexed by block ID.
llvm::BitVector reachableBlocks(const CFG &Graph);

/// The blocks of \p Graph in the order the flow reaches them, each after the
/// blocks before it that aren't through a back edge, so that propagating what
/// holds forward visits each block of a graph without loops once per pass.
llvm::SmallVector<const CFGBlock *, 16> forwardOrder(const CFG &Graph);

/// Whether \p Call is `std::invoke` or `std::invoke_r`, which calls what it
/// is given right away.
bool isStdInvoke(const CallExpr *Call);

/// The call operator of the lambda \p E calls where it is made, `[...] {
/// ... }()`, also explicitly, `[...] { ... }.operator()()`, or null.
const CXXMethodDecl *calledInPlaceLambda(const Expr *E);

/// What \p Function, whose body can be seen, gives back: the values of its
/// returns that can run, not in a branch that is discarded or never taken,
/// and of a local it returns, `return pValue;`, what it is made from and
/// assigned, `pValue = &m_Value`.
llvm::SmallVector<const Expr *, 4> returnedValues(const FunctionDecl *Function);

/// The argument of \p Call, of a free function whose body can be seen, that
/// it gives back as it is on every return, `fg_Identity(pValue)` of `return
/// _pValue;`, or null.
const Expr *returnedArgument(const CallExpr *Call);

/// The arguments of \p Call, of a free function whose body can be seen, one
/// of which it gives back as it is on every return, `fg_Choose(bFirst, First,
/// Second)` of `return _bFirst ? _First : _Second;`, or none when it can give
/// back something else.
llvm::SmallVector<const Expr *, 2>
returnedArgumentChoices(const CallExpr *Call);

/// Whether \p E designates `this` or data reached from it through members,
/// dereferences, subscripts and member calls, like `*mp_pInternal` or
/// `mp_Items.f_FindEqual(_Key)`, also through local pointers and references
/// that do. A new allocation that only reads members does not.
bool isRootedInThis(const Expr *E);

/// Whether \p E is rooted in `this`, like isRootedInThis, unless
/// \p ThisIsRoot is false, or in a declaration that \p IsRoot accepts, like a
/// pointer parameter to the object of an actor.
bool isRootedIn(const Expr *E,
                llvm::function_ref<bool(const ValueDecl *)> IsRoot,
                bool ThisIsRoot = true);

/// Whether \p Function is a coroutine: its body awaits, yields or returns with
/// `co_return`. Also works for templates, whose bodies are not built as
/// coroutine bodies until they are instantiated.
bool isCoroutine(const FunctionDecl *Function);

/// Whether \p Function returns a future whose coroutines get the lifetime
/// protections of `TCFuture`: a `TCFuture`, but not a `TCUnsafeFuture` or a
/// `TCFutureWithFlags` that allows references.
bool returnsProtectedFuture(const FunctionDecl *Function);

/// The instantiations of the templated functions of a translation unit, by
/// their patterns, including the call operators of lambdas, which checks that
/// don't match instantiations use to look at what a template becomes when
/// its pattern can't be told.
class InstantiationIndex {
public:
  /// The instantiations of \p Pattern in the translation unit of \p Context.
  llvm::ArrayRef<const FunctionDecl *> instantiationsOf(const FunctionDecl *Pattern,
                                                        ASTContext &Context);

  /// The lambda expression whose call operator, or a specialization of it,
  /// \p Operator is.
  const LambdaExpr *lambdaOf(const FunctionDecl *Operator, ASTContext &Context);

  /// Forgets the index, at the start of a translation unit, whose context
  /// can be at the address of the one before.
  void invalidate() { IndexedAST = 0; }

private:
  /// The identity of the AST it indexes, astIdentity.
  uint64_t IndexedAST = 0;
  llvm::DenseMap<const FunctionDecl *, const LambdaExpr *> Lambdas;
  llvm::DenseMap<SourceLocation, llvm::SmallVector<const FunctionDecl *, 2>>
      LambdaInstances;
  llvm::DenseMap<const FunctionDecl *, llvm::SmallVector<const FunctionDecl *, 2>>
      Merged;
  llvm::DenseMap<const FunctionDecl *, llvm::SmallVector<const FunctionDecl *, 2>>
      Instantiations;
};

/// The index that the checks share, which is made once for a translation
/// unit.
InstantiationIndex &sharedInstantiationIndex();

/// Whether \p Type is a `TCFuture` that the checks protect, as opposed to a
/// `TCUnsafeFuture`.
bool isProtectedFutureType(QualType Type, const ASTContext &Context);

/// Whether \p Function is a coroutine that returns a protected future.
inline bool isProtectedCoroutine(const FunctionDecl *Function) {
  return Function && returnsProtectedFuture(Function) && isCoroutine(Function);
}

/// Whether \p Record is an actor: it derives from `CActor` or
/// `CActorInternal`.
bool isActorClass(const CXXRecordDecl *Record);

/// The class whose `this` \p Lambda captures: the class of the member
/// function it is in, looking through enclosing lambdas.
const CXXRecordDecl *enclosingThisClass(const LambdaExpr *Lambda);

/// Whether \p E, ignoring parentheses and implicit nodes, is a call of a free
/// function named one of \p Names.
const CallExpr *asCallTo(const Expr *E, llvm::ArrayRef<StringRef> Names);

/// Whether \p S suspends a coroutine: a `co_await` or `co_yield`, but not
/// `co_await` of a coroutine flag, like `ECoroutineFlag_CaptureExceptions`, of
/// `fg_OnResume` or `fg_AsyncDestroy`, or of an awaiter that derives from
/// `CSuspendNever`, like `g_CaptureExceptions`, which don't suspend.
bool isSuspension(const Stmt *S);

/// Whether the awaiter of \p Suspend never suspends: `CSuspendNever`, or one
/// whose `await_ready` that isn't virtual only returns true.
bool awaiterNeverSuspends(const CoroutineSuspendExpr *Suspend);

/// The parts of the suspensions in \p Body that run when the coroutine
/// suspends: the `await_suspend` calls and what they evaluate. In a CFG with
/// exception edges, an exception that `await_resume` throws reaches its
/// handler before the `co_await` completes, so a flow over the CFG counts the
/// coroutine as suspended from these on.
llvm::SmallPtrSet<const Stmt *, 8> suspendPoints(const Stmt *Body);

/// Whether \p S uses `this`, explicitly or implicitly through a member. With
/// \p MovedThis, the calls of `fg_MoveThis` and `fg_CopyThis`, which move or
/// copy the object into a coroutine, are collected there instead.
bool usesThis(const Stmt *S,
              llvm::SmallVectorImpl<const CallExpr *> *MovedThis = nullptr);

/// Whether \p E is part of what a `co_await` awaits, like `fg_Move(X)` in
/// `co_await fg_Move(X).f_Wrap()`.
bool isAwaited(const Expr *E, ASTContext &Context);

/// The statements of the body of the coroutine \p Function that can run after
/// it suspended, following its control flow, loops included.
llvm::DenseSet<const Stmt *>
statementsAfterSuspension(const FunctionDecl *Function, ASTContext &Context);

/// The borrowed data of firstUseAfterSuspension.
struct BorrowRoots {
  /// The expressions that designate the borrowed data, like a parameter or a
  /// capture.
  llvm::function_ref<bool(const Stmt *)> IsRoot;
  /// Whether the value of a root that is a pointer or view refers to the
  /// borrowed data, like for a pointer parameter, rather than being a copy of
  /// a pointer kept in the borrowed storage, like one copied into a closure.
  bool PointerValues = false;
  /// Whether a type is a non-owning view, like a span, which refers to the
  /// data it is made from.
  llvm::function_ref<bool(QualType)> IsView = {};
  /// Whether `this` that a lambda in the body captures, `[this]` or `[&]`, is
  /// a copy of the pointer made when that lambda is made, as for the closure
  /// that holds `this`, as opposed to the object it points to.
  bool ThisCaptureIsCopy = false;
  /// Whether the callers join the coroutine, like one that runs to
  /// completion, and keep the borrowed data until then: only what its result
  /// keeps, which `co_return` gives, outlives the call.
  bool Joined = false;
  /// Whether what is checked is the closure the roots are members of, which
  /// assigning a pointer of it accesses as much as reading it, `pValue =
  /// &Value`, unlike for what the pointer points to, which pointing it
  /// elsewhere leaves.
  bool OfClosure = false;
  /// When the roots, and the uses, are the references to this declaration,
  /// like a parameter, which are then found by an index of the function made
  /// once, not by going through the body for each.
  const ValueDecl *Decl = nullptr;
};

/// Whether \p Type, or an alias on the way to it, is named one of
/// \p Patterns: a qualified name matches the qualified name, a leading `*`
/// matches the end of the name, and a trailing one its start. A pattern of a
/// template and its first argument, `TCStr<CStrTraitsPtr_*>`, matches its
/// specializations with such an argument.
bool matchesTypeName(QualType Type, llvm::ArrayRef<StringRef> Patterns,
                     const ASTContext &Context);

/// Whether \p Type is one of the view types of the vocabulary, the types that
/// refer to data without owning it, like `std::span` and Malterlib's `CStrPtr`.
bool isDefaultViewType(QualType Type, const ASTContext &Context);

/// The first statement in the body of the coroutine \p Function that
/// \p IsUse accepts and that can run after the coroutine suspended. What the
/// first `co_await` awaits runs before it suspends. The bodies of the lambdas
/// and blocks the coroutine makes can run whenever they are called, so the
/// statements in them count as after a suspension, unless they use a copy the
/// lambda captured. Null when there is none.
///
/// With \p Borrow, a coroutine that suspends may use its borrowed data before
/// the first suspension only as values: read as scalars, copied or moved into
/// values that are not views, or given to functions that return a `TCFuture`.
/// The first other use, like binding a reference, taking the address, making
/// a view, an awaiter or another object from it, counts too, since what it
/// makes can keep a reference past the suspension.
const Stmt *
firstUseAfterSuspension(const FunctionDecl *Function,
                        llvm::function_ref<bool(const Stmt *)> IsUse,
                        ASTContext &Context,
                        const BorrowRoots *Borrow = nullptr);

/// The first use, after a suspension, of what the lambda whose call operator
/// is \p CallOperator captures: the captured variables and `this`, which live
/// in its closure or outside of it. Null when there is none.
const Stmt *firstCaptureUseAfterSuspension(const CXXMethodDecl *CallOperator,
                                           ASTContext &Context);

/// Whether \p S refers to \p Decl.
inline bool isReferenceTo(const Stmt *S, const ValueDecl *Decl) {
  const auto *Ref = dyn_cast<DeclRefExpr>(S);
  return Ref && Ref->getDecl() == Decl;
}


/// How the closure of a lambda is used.
enum class ClosureUse {
  /// It is called, like `Lambda()`, and the coroutine starts in the call.
  DirectCall,
  /// It is given to `fg_CallSafe`, which keeps it alive and calls it right
  /// away.
  CallSafe,
  /// It is given to a function of the safe list, which keeps it alive while
  /// it runs, maybe later.
  SafeCallee,
  /// It is given to something else, which can keep it or call it directly.
  Escape,
};

struct ClosureConsumer {
  ClosureUse Use;
  /// Where the closure is used.
  const Expr *Site;
  /// What it is given to, for an escape.
  std::string Target;
};

/// The uses of the closure of \p Lambda: where the lambda expression goes,
/// past `fg_Move`, copies and moves of the closure and locals that hold it.
/// Unevaluated uses, like `decltype(fLambda())`, are left out.
llvm::SmallVector<ClosureConsumer, 2>
closureConsumers(const LambdaExpr *Lambda, ASTContext &Context,
                 llvm::ArrayRef<StringRef> SafeCallees);

/// \p Consumers as a check that keeps a closure safe only where it is given to
/// one of \p SafeCallees sees them: one given to `fg_CallSafe`, which calls it
/// right away, is safe where that is one of them, like the others, and else
/// given to it like to any other function.
void giveUnlistedCallSafe(llvm::SmallVectorImpl<ClosureConsumer> &Consumers,
                          llvm::ArrayRef<StringRef> SafeCallees);

/// Whether each of \p Consumers calls the closure while the scope that made it
/// is still there: directly, or through `fg_CallSafe`.
bool isCalledInScope(llvm::ArrayRef<ClosureConsumer> Consumers);

/// Whether \p Lambda is given only to what runs it to completion before it
/// returns, like the `operator %` of a test category, which dispatches it and
/// waits for it. Not one joined with `f_CallSync()`, which, like a coroutine
/// given references, must run to completion to keep them, since what is between
/// the dispatch and the join can leave first.
bool isJoinedBlocking(const LambdaExpr *Lambda, ASTContext &Context);

/// Whether \p E is the helper of a scope exit, `g_OnScopeExit`,
/// `g_OnScopeExitCatch` or `g_OnScopeExitActor`, by its type, also through a
/// reference to it; not of `g_OnScopeExitActor` without \p AlsoActor.
bool isScopeExitHelper(const Expr *E, bool AlsoActor = true);

/// The lambda \p E makes a scope exit with, `g_OnScopeExit / [...]`, also
/// called explicitly, `g_OnScopeExit.operator/([...])`, or
/// `fg_OnScopeExit([...])`; null when it doesn't make one.
const LambdaExpr *scopeExitOperand(const Expr *E);

/// The lambda that destroying \p Local runs, a scope exit like `auto Cleanup
/// = g_OnScopeExit / [...]` or `fg_OnScopeExit([...])`.
const LambdaExpr *scopeExitLambda(const VarDecl *Local);

/// Whether \p Guard, a scope exit declared as a statement of \p Block, is
/// cleared, `Rollback.f_Clear()` or `Rollback.f_ClearFunctor()`, which keeps
/// its callback from running, by a later statement of the block, before any
/// statement between that \p Leaves tells can leave the block, or that names
/// the guard otherwise, like to move it.
bool isClearedBeforeLeaving(const VarDecl *Guard, const CompoundStmt *Block,
                            llvm::function_ref<bool(const Stmt *)> Leaves);

/// Whether \p Lambda is given to a scope exit that is a discarded temporary,
/// `(void)(g_OnScopeExit / [this] { ... })`, which runs it at the end of the
/// full expression it is made in, `fullExpressionOf`.
bool isTemporaryScopeExitLambda(const LambdaExpr *Lambda, ASTContext &Context);

/// The full expression \p E is in, at whose end its temporaries are
/// destroyed: the outermost expression around it, not past a lambda or a
/// statement, and not its `ExprWithCleanups` or parentheses, which the CFG
/// doesn't have as elements.
const Expr *fullExpressionOf(const Expr *E, ASTContext &Context);

/// The dispatch that \p Start, the closure \p Closure, is given to, like
/// `g_Dispatch(Actor) / Lambda`, `fg_Dispatch(Actor, Lambda)` or
/// `Actor.f_Bind<...>(Lambda)`, past parentheses, implicit nodes, copies and
/// moves. \p Actor is set to the actor it dispatches to, or to null for
/// `g_Dispatch`, which dispatches to a concurrent actor. Null when it isn't
/// dispatched there.
const Expr *closureDispatch(const Expr *Start, const CXXRecordDecl *Closure,
                            ASTContext &Context, const Expr **Actor);

/// Whether \p Variable lives while the function that makes \p Lambda runs: it
/// is a local of that function, or of a function around it whose lambdas in
/// between are not coroutines and are called directly, like the blocks of
/// `[&] { ... }()`, or one of them holds a copy of it. A reference init
/// capture, like `[&Alias = Value]`, is followed to what it refers to.
bool isLocalWhileRunning(const ValueDecl *Variable, const LambdaExpr *Lambda,
                         ASTContext &Context);

/// Whether the object of `this` lives while the function that makes
/// \p Lambda runs: that is a member function, or the lambdas in between are
/// not coroutines and are called directly, or one of them holds a copy of the
/// object, `[*this]`.
bool isObjectAliveWhileRunning(const LambdaExpr *Lambda, ASTContext &Context);

/// The type of the value of the future \p Type, like `int &` of
/// `TCFuture<int &>`, or null.
QualType futureValueType(QualType Type);

/// Whether the local that \p Ref refers to keeps its initial value where
/// \p Ref uses it, also the copy a lambda captured.
bool isUnchangedAt(const DeclRefExpr *Ref);

/// Whether \p Earlier runs on every path to \p Later: it is in a statement,
/// only through expressions that run all of themselves, of a block that has
/// \p Later after that statement.
bool runsOnEveryPathBefore(const Stmt *Earlier, const Stmt *Later,
                           ASTContext &Context);

/// What the local \p Variable is assigned on every path to \p Use, if it is:
/// the value of the last assignment to it that is a statement before it in
/// the innermost block that it is in that has one, like `&Local` of `pValue =
/// &Local;` before `[pValue] { ... }`.
const Expr *assignedOnEveryPathBefore(const Stmt *Use, const VarDecl *Variable,
                                      ASTContext &Context);

/// Whether \p Ref, of a variable a lambda around it captures, is of a copy the
/// lambda captured, like `State` in `[State] { ... }`, which is its own
/// object even when the variable is a reference.
bool isCapturedByCopyAt(const DeclRefExpr *Ref);

/// The lambda expression whose call operator \p Method is.
const LambdaExpr *lambdaOfCallOperator(const CXXMethodDecl *Method,
                                       ASTContext &Context);

/// The function that \p Lambda is made in, which may be the call operator of
/// another lambda.
const FunctionDecl *enclosingFunction(const LambdaExpr *Lambda);

/// Whether \p Lambda init-captures a strong owning pointer to the object of
/// the code it is in, like `[pThis = TCSharedPointer<CState>{fg_Explicit(this)}]`,
/// which keeps the object alive as long as the closure. Weak pointers don't.
bool keepsObjectAlive(const LambdaExpr *Lambda);

/// Whether \p Record is a class whose objects coroutines may refer to through
/// pointers and references like through `this`: it derives from `CActor`,
/// `CActorInternal` or `CAllowUnsafeThis`.
bool isKnownSafeThisClass(const CXXRecordDecl *Record);

/// Whether \p Record derives from `CActor`, so its member functions can be
/// called through actor calls.
bool isActorCallClass(const CXXRecordDecl *Record);

/// \p E without parentheses, implicit nodes and the copies and moves of
/// constructors that take it.
const Expr *skipWrappers(const Expr *E);

/// Whether the pointer value \p Pointer is only compared or tested, `_pValue
/// == nullptr`, `if (_pValue)` or `bool(_pValue)`, which reads only the
/// pointer, not what it points to.
bool isOnlyTestedPointer(const Expr *Pointer, ASTContext &Context);

/// Whether \p S names a parameter taken by value, like a view, only to read
/// or update a part of it that is its own, a scalar member, `Text.m_Length`
/// or `Text.m_Length = 0`, or what a const member function of it whose body
/// can be seen gives of only those, `Text.f_GetLen()`, or a pointer compared
/// or tested, `_pValue == nullptr`: that is in the frame of the coroutine,
/// unlike the data the view or pointer refers to.
bool readsOnlyOwnPart(const Stmt *S, ASTContext &Context);

/// Whether the function \p Function, whose body can be seen, returns only
/// what has static storage: a string literal, `return "Name";`, null, a
/// number, or the address of a static variable, `return &g_Value;`, a static
/// array, or one by reference, `return s_Value;` of `int const &`.
bool returnsOnlyStatic(const FunctionDecl *Function);

/// Whether each of \p Values, what a function gives back, is of static
/// storage, as returnsOnlyStatic tells, also by reference with
/// \p ByReference, like the values a coroutine gives with `co_return`.
bool onlyStatic(llvm::ArrayRef<const Expr *> Values, bool ByReference);

/// Whether \p Lambda captures \p Variable by reference, and so does each
/// lambda around it that captures it, so what it assigns to it is assigned to
/// the variable, not to a copy in a lambda around it, `[p] { [&p] { p =
/// nullptr; }(); }`.
bool capturedByReferenceThroughout(const LambdaExpr *Lambda,
                                   const ValueDecl *Variable,
                                   ASTContext &Context);

/// Whether the body of the constructor \p Constructor sets the pointer member
/// \p Field to what \p Keeps doesn't tell keeps what it is given, `m_pValue =
/// nullptr;`, before anything else can use it or the body can leave, so what
/// its initializer kept, `m_pValue(&_Value)`, is gone.
bool resetsFirst(const CXXConstructorDecl *Constructor, const FieldDecl *Field,
                 llvm::function_ref<bool(const Expr *)> Keeps);

/// What awaiting \p Operand awaits: each value of a conditional, `co_await
/// (bChoose ? fg_OnResume(A) : fg_OnResume(B))`, what a comma gives last, and
/// an awaiter made before, `co_await fg_Move(Awaiter)` of `auto Awaiter =
/// fg_OnResume(...)`, also through a reference to it, `auto &Alias =
/// Awaiter`. A value that can't be followed, like a local made from itself,
/// gives a null entry.
llvm::SmallVector<const Expr *, 2> awaitedValues(const Expr *Operand);

/// The `fg_OnResume` calls that awaiting \p Operand awaits, for each of the
/// values of `awaitedValues`, also those a function whose body can be seen
/// gives back, `f_Watch()`, null for one that isn't.
llvm::SmallVector<const CallExpr *, 2> onResumeCalls(const Expr *Operand);

/// The awaits that a variable initialized with \p Init holds the result of:
/// the await, `co_await fg_OnResume(...)`, or those of the values of a
/// conditional, `bChoose ? co_await A : co_await B`. A value that isn't an
/// await gives a null entry. With \p Context, of a conditional whose
/// condition is a constant, `t_bStop ? co_await A : co_await B`, the value it
/// chooses.
llvm::SmallVector<const CoroutineSuspendExpr *, 2>
holderAwaits(const Expr *Init, const ASTContext *Context = nullptr);

/// The cleanup lambdas of the guard \p Holder that `co_await
/// fg_AsyncDestroy(Lambda)` gives, also of each value of a conditional.
llvm::SmallVector<const LambdaExpr *, 2>
asyncDestroyLambdas(const VarDecl *Holder);

/// The lambda that \p Argument of `fg_AsyncDestroy` is, also one a factory
/// lambda held in an unchanged local returns, `fg_AsyncDestroy(fMake())`.
const LambdaExpr *cleanupLambdaOf(const Expr *Argument);

/// The functions that a call of \p Method can run: itself and, when the call
/// is virtual, what overrides it in the file, also through other overrides.
llvm::SmallVector<const CXXMethodDecl *, 4>
dispatchedMethods(const CXXMemberCallExpr *Call);

/// Whether \p Holder holds the guard of `fg_AsyncDestroy` of a lambda.
bool isAsyncDestroyHolder(const VarDecl *Holder);

/// Updates \p Armed, the locals whose destruction runs a cleanup lambda,
/// for what \p S does: a scope exit, `g_OnScopeExit / [...]`, or the guard of
/// `fg_AsyncDestroy`, is armed where it is declared, and runs unless
/// cancelled by `f_Clear()` or `f_ClearFunctor()` first, or a scope exit run
/// early, `Cleanup()`, which clears it. The guard of `fg_AsyncDestroy` is
/// also disarmed when moved away, `fg_Move(Cleanup)` or given to an rvalue
/// reference. Calls \p OnRun with each lambda that \p S runs early: a scope
/// exit that is armed, called, or the cleanup of a guard awaited early,
/// `co_await fg_Move(Cleanup)`, which the move before disarmed.
void updateArmedCleanups(
    const Stmt *S, llvm::SmallPtrSetImpl<const VarDecl *> &Armed,
    llvm::function_ref<void(const LambdaExpr *Lambda, const VarDecl *Holder)>
        OnRun);

/// Whether \p S suspends, outside of the lambdas in it, like an argument
/// `(co_await fg_Work(), 1)`, where it runs: not in the arm of a conditional
/// that a constant condition doesn't choose, `false ? co_await fg_Work() : 1`,
/// nor on the right side of a `&&` or `||` whose constant left side skips it.
bool suspendsIn(const Stmt *S, const ASTContext &Context);

/// The pointer operand of \p Access, a subscript or a dereference, when what
/// it points to is accessed after a suspension in the same expression, like
/// the `p` of `p[(co_await fg_Work(), 0)]`, `*(p + (co_await fg_Work(),
/// 0))` or `(p + (co_await fg_Work(), 0))[0]`: the pointer is read before
/// the suspension, but what it points to is used after it. Of an assignment,
/// also a compound one, whose right side suspends, what its target is reached
/// through, `p` of `*p += co_await fg_Work()`, `this` of a member, or a
/// reference that names it, which is written after it.
const Expr *accessedAfterSuspension(const Stmt *Access,
                                    const ASTContext &Context);

/// The variables or `this` that the pointer \p Pointer is made of, past
/// casts and arithmetic, like `pValue` of `pValue + 1`, both of a
/// conditional, `bChoose ? pFirst : pSecond`.
llvm::SmallVector<const Expr *, 2> pointerRoots(const Expr *Pointer);

/// The type of the parameter that the argument \p iArgument of \p Call, a
/// call or a construction, is given to: of the function it calls, after the
/// object of a member operator, or of the type of what it calls through, like
/// a pointer to a function or to a member function. Null when it can't be
/// told.
QualType argumentParameterType(const Expr *Call, unsigned iArgument);

/// Whether \p Type is an iterator, like `TCMap<...>::CIterator`, which is at
/// an entry through pointers.
bool isIteratorType(QualType Type);

/// Whether \p S jumps, with a `goto` or a label, outside of the lambdas in
/// it, which can pass a test or run what is written before something again.
bool hasJumps(const Stmt *S);

/// The children of \p S that can run: of an `if constexpr` whose condition
/// is known, not the branch it discards, of an `if`, a loop or a `?:` whose
/// condition is constant, not what it never runs, `if (false)`, and of what
/// isn't evaluated, like `sizeof(Value)` or `__builtin_constant_p(Value)`,
/// none.
llvm::SmallVector<const Stmt *, 4> evaluatedChildren(const Stmt *S,
                                                     const ASTContext &Context);

/// The same without the context, by the value Sema keeps of the condition of
/// an `if constexpr`.
llvm::SmallVector<const Stmt *, 4> evaluatedChildren(const Stmt *S);

/// The statements of \p Body, a compound statement, that run each time it
/// runs: those before the first one that can return, jump or throw, also
/// past the cleanups and parentheses around one. With \p CallsLeave, also
/// before one that calls what can throw, for a body run where what it
/// throws is caught.
llvm::SmallPtrSet<const Stmt *, 8>
statementsBeforeLeaving(const Stmt *Body, bool CallsLeave = false);

/// Whether a statement of \p Body that runs each time it does, before \p
/// Read, assigns \p Variable what \p Replaces tells replaces what it held,
/// like the `pValue = &Local` before `*pValue` in a lambda that captures
/// `pValue` by copy, so that the read is of that.
bool reassignedBefore(const Stmt *Body, const ValueDecl *Variable,
                      const Expr *Read, const SourceManager &SM,
                      llvm::function_ref<bool(const Expr *)> Replaces);

/// Whether \p S is in the `try` block of a `try` statement of the function
/// or lambda it is in, where what it throws can be caught.
bool insideTryBlock(const Stmt *S, ASTContext &Context);

/// The variables that an assignment to \p Left can assign: the one it names,
/// also through parentheses and casts to a reference, `static_cast<int
/// *&>(pValue)`, the right side of a comma, either of a conditional,
/// `(bFirst ? pFirst : pSecond)`, which assigns only one of them, and what a
/// local pointer points to, `*ppValue` with `int **ppValue = &pValue`.
llvm::SmallVector<const DeclRefExpr *, 2> assignedVariables(const Expr *Left);

/// Whether \p E calls a phase guard, a `DMibActorPhaseGuard` function like
/// `f_CheckDestroyedOnResume()`, which registers a callback with
/// `fg_OnResume` that ends the coroutine on resume in the phases it guards.
/// What is awaited is told by `awaitedValues`.
bool isPhaseGuardCall(const Expr *E);

/// Whether the parameter that argument \p iArgument of \p Call binds to is a
/// non-const reference, of the callee or of the function type it is called
/// through.
bool mayAssignArgument(const CallExpr *Call, unsigned iArgument);

/// Whether the local or parameter \p Variable can be given another value in
/// its function: assigned, incremented, moved or swapped from, or passed by
/// non-const reference or address.
bool isChangedVariable(const VarDecl *Variable);

/// Whether the local that \p Use refers to is changed elsewhere than by
/// \p Use, like the move that gives its value away, `fg_Move(Actor)`.
bool isChangedOtherThanBy(const DeclRefExpr *Use);

/// Whether \p S gives \p Variable another value, not counting the copies
/// lambdas in it capture.
bool isChangedIn(const VarDecl *Variable, const Stmt *S);

/// The initializer of the local \p E names when it keeps its initial value,
/// like `g_Dispatch(self)` of `auto Dispatch = g_Dispatch(self)`, and \p E
/// otherwise.
const Expr *unchangedLocalValue(const Expr *E);

/// Whether \p Actor is an empty actor, which a timer replaces with the
/// current one: the default argument, `{}`, `nullptr` or `TCActor<>()`, also
/// in an unchanged local, `TCActor<> Empty; fg_RegisterTimer(..., Empty)`.
bool isEmptyActor(const Expr *Actor, int Depth = 0);

/// Whether \p E refers to the object of the code it is in, or the actor that
/// owns it: `this`, `*this`, a member of a helper object that refers to its
/// owner, which Malterlib names after `this`, like `m_pThis` or `&mp_This`, or
/// an unchanged local alias of one of them.
/// With \p Context, an alias that a lambda the use is in copies and changes
/// is not one.
bool refersToOwnObject(const Expr *E, ASTContext *Context = nullptr);

/// Whether \p Parameter, of a free or static member function, points or
/// refers to the object of an actor, `CActor *_pThis`, also as an
/// instantiation makes it, `t_CActor *_pActor` of `fg_Run<CApp>`: what the
/// function is given there must be the object of the actor it runs on.
bool takesActorObject(const ParmVarDecl *Parameter);

/// Whether \p Function only gives back what it is given, as a reference or
/// pointer: its body returns a parameter, also through casts, after nothing
/// but static assertions, like `fg_Forward` and `fg_Move`, or it is a builtin
/// like `std::move`. What matters is what is done with the result.
bool returnsItsParameter(const FunctionDecl *Function);

/// Whether \p Function only constructs or destroys with its parameter
/// \p iParameter, an object it is given: deletes it, calls its destructor,
/// frees its memory as a `void *`, gives it to a constructor, or tests it for
/// null, also through casts, functions that give it back and functions that
/// do only that with it, like `fg_DeleteObject` and `fg_ConstructObject`. That
/// manages the object; it doesn't use it like `this`.
bool onlyConstructsOrDestroys(const FunctionDecl *Function,
                              unsigned iParameter);

/// Whether \p Declaration is such a parameter, which isn't changed: the
/// object of the actor the function runs on, used like `this`.
bool isActorObjectParameter(const ValueDecl *Declaration);

/// Whether \p E refers to the object of the actor the code it is in runs on:
/// what refersToOwnObject accepts, or such a parameter, `_pThis`, also
/// through unchanged local aliases.
bool refersToCurrentActorObject(const Expr *E, ASTContext *Context = nullptr);

/// The lambda that \p E is: a lambda expression, a local of its closure
/// type, or `fg_Move` of one of them. Null otherwise.
const LambdaExpr *asLambda(const Expr *E);

/// Whether \p Method is one that \p Matches tells of, or one whose body calls
/// such a member function of its object, `this` or its explicit object
/// parameter, `f_GetArray(_Len)` of `f_SetLen(_Len); return f_GetArray();`,
/// also deeper, but not one that can't change it, nor in a lambda.
bool callsOwnMethod(const CXXMethodDecl *Method,
                    llvm::function_ref<bool(const CXXMethodDecl *)> Matches);

/// The lambda that \p Call calls, `fRead()` or `fRead.operator()()`. Null
/// otherwise.
const LambdaExpr *calledLambda(const CallExpr *Call);

/// Whether \p Variable, which a lambda captures, holds a pointer or reference
/// to the object of the code it is in: an init capture like
/// `[pThis = this]`, or an unchanged local like `auto *pThis = this` that is
/// captured by copy. With \p OwnObject, also what refersToOwnObject accepts,
/// and without, also a pointer or reference into it, `[pValue = &m_Value]`.
bool isCapturedAliasOfThis(const VarDecl *Variable, bool OwnObject);

/// Whether \p E is the actor of the code it is in: `self`,
/// `fg_ThisActor(this)`, or a local handle initialized with one of them that
/// is never changed.
bool isOwnActor(const Expr *E, ASTContext &Context);

/// The name of the class or class template of \p Type, or an empty name.
StringRef recordName(QualType Type);

/// Whether \p S has a statement that a jump from outside it can reach: a
/// label, or a case of a switch around it, not of a switch in it nor in a
/// lambda.
bool hasJumpTarget(const Stmt *S, bool InSwitch = false);

/// Whether \p S always leaves what follows it: a return or a jump, also as
/// the end of a block, or in each branch of an `if` that runs, like the one an
/// `if constexpr` keeps.
bool alwaysLeaves(const Stmt *S, const ASTContext &Context);

/// Whether \p Node is in what a constant condition never runs, unless a jump
/// to a label in it can reach it: the branch of an `if` it doesn't take, `if
/// (false)`, the body of a loop whose condition is false, `while (false)`, the
/// arm of a conditional it doesn't choose, or the right side of `&&` or `||`
/// it doesn't evaluate; not past the function or lambda it is in.
bool isInNeverTakenBranch(const DynTypedNode &Node, ASTContext &Context);

/// Whether \p S never runs: it isn't evaluated, like in `decltype` or
/// `sizeof`, or is in a branch of an `if constexpr` that is discarded, or in
/// one that a constant condition never takes, `if (false)`.
bool isNeverRun(const Stmt *S, ASTContext &Context);

/// Whether \p Call takes the address of its argument: `std::addressof`,
/// `fg_AddressOf` or `__builtin_addressof`, like `&` without its overloads.
bool isAddressOfCall(const CallExpr *Call);

/// Whether \p Type is an ownership transfer, `CCoroutineTransferOwnership`,
/// its awaiter, `CCoroutineTransferOwnershipAwaiter`, which its `operator
/// co_await` gives, or a class derived from either.
bool isTransferOwnership(QualType Type);

/// A parameter that a function assigns another of its parameters, `void
/// fg_Set(int *&o_pValue, int *_pValue) { o_pValue = _pValue; }`, and whether
/// it always does, as a statement of its body before any return.
struct CSetParameter {
  unsigned Target;
  unsigned Source;
  bool Always;
  /// Whether it is set to the address of what the source refers to, `&_Value`,
  /// rather than to its value.
  bool Addressed = false;
  /// Whether the target is given by its address, `void fg_Set(int **_ppValue,
  /// int *_pValue) { *_ppValue = _pValue; }` of `fg_Set(&pValue, ...)`, rather
  /// than by reference.
  bool TargetByPointer = false;
};

/// What the argument of \p Call that \p Set targets names, with the arguments
/// of the function's parameters from \p Offset: the pointer it is given by
/// reference, or the one whose address it is given, `&pValue`; null when it
/// isn't the address of one.
const Expr *setTargetArgument(const CallExpr *Call, const CSetParameter &Set,
                              unsigned Offset);

/// The parameters of \p Function, whose body can be seen, that are
/// references to pointers that it assigns another of its parameters, also
/// moved, `o_pValue = fg_Move(_pValue)`, or the address of one that is a
/// reference, `o_pValue = &_Value`, of what its argument is.
llvm::ArrayRef<CSetParameter> setParameters(const FunctionDecl *Function);

/// The definition of the function \p Call runs whose body tells what it sets
/// of the parameters, setParameters: one it calls directly, `fg_Set(pValue,
/// ...)`, not a member function or an operator, or the call operator of a
/// lambda, `fSet(pValue, ...)`, whose arguments start after the lambda, at
/// \p Offset.
const FunctionDecl *setterCalled(const CallExpr *Call, unsigned &Offset);

/// Whether \p Left and \p Right are the same part of a local, the same
/// member of it, `Result.m_pValue`, or it itself.
bool sameLocalPart(const Expr *Left, const Expr *Right);

/// The function \p Call calls: its direct callee, or the one an unchanged
/// local pointer it calls through is made from, also dereferenced, `auto
/// fDestroy = &fg_AsyncDestroy<CResource &>; (*fDestroy)(Resource)`, and
/// through unchanged local copies of that, `auto fCopy = fDestroy;`. Null
/// when that can't be told.
const FunctionDecl *functionCalledBy(const CallExpr *Call);

/// Whether \p Await awaits an ownership transfer: its operand is one, or the
/// awaiter that the `operator co_await` of its operand gives is,
/// `fg_ContinueRunningOnActor(Actor).operator co_await()` of a wrapper.
bool awaitsTransferOwnership(const CoroutineSuspendExpr *Await);

/// The name of the function \p Call calls, or an empty name.
StringRef calleeName(const CallExpr *Call);

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEUTILS_H
