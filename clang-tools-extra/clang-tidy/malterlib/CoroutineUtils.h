//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEUTILS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEUTILS_H

#include "NamingUtils.h"
#include "StorageModel.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <optional>
#include <string>

namespace clang {
class CFG;
class CFGBlock;
} // namespace clang

namespace clang::tidy::malterlib {

/// The functions that register an asynchronous cleanup for when a coroutine
/// scope ends. Awaiting them does not suspend. Of StringRef, so that the table
/// is an ArrayRef<StringRef> as it is: the end of an array of StringLiteral
/// isn't a StringRef pointer.
inline constexpr llvm::StringRef AsyncDestroyFunctions[] = {
    "fg_AsyncDestroy", "fg_AsyncDestroyByValue", "fg_AsyncDestroyGeneric",
    "fg_AsyncDestroyLogError"};

/// The blocks of \p Graph that can run: those reachable from its entry,
/// indexed by block ID.
llvm::BitVector reachableBlocks(const CFG &Graph);

/// Whether \p S can run where it is, in the innermost function it is in, of
/// a lambda its call operator: by the CFG of the function, which leaves out
/// a branch an `if constexpr` discards or a constant condition never takes
/// and what comes after a return (canRun), and by the effect table, which
/// leaves out what isn't evaluated, also the operand a constant `&&` or `||`
/// skips, `c_bUpdate && (co_await fg_Update(), true)` (CEffectIndex::
/// evaluated). Outside a body, where it is used, unless it isn't evaluated,
/// like an operand of a `decltype` in a requires clause. The body of a closure
/// runs only where the lambda that makes it can run. The default of a switch
/// over each enumerator, and what follows one without a default, can run: an
/// enum can hold a value no enumerator names (buildGraph).
bool runsWhereItIs(const Stmt *S, ASTContext &Context);

/// The innermost function whose body or member initializer \p S is in, of a
/// lambda its call operator; null outside one, of a default argument.
const FunctionDecl *bodyFunctionOf(const Stmt *S, ASTContext &Context);

/// Whether \p S, in \p Body, the body of \p Owner when it is a function,
/// can run: it is in a block of the control flow of \p Body that can be
/// reached (reachableBlocks), past edges a constant condition never takes,
/// also of a handler (addExceptionEdges), not after a return nor in a branch
/// a constant condition leaves out, `if (false)`. What is in a statement that
/// can run can, also the body of a lambda; each body is built once. Each can
/// when the control flow can't be built.
bool canRunIn(const Stmt *S, const Stmt *Body, const Decl *Owner,
              ASTContext &Context);

/// The member of the object of \p Function, a member function whose body
/// can be seen, that \p Target is: a member of `this`, also through an
/// unchanged local pointer to the object, `auto *pSelf = this;
/// pSelf->m_pData`, or a local reference to a member, `auto &rpData =
/// m_pData; rpData`. Null for any other.
const FieldDecl *assignedMember(const Expr *Target,
                                const FunctionDecl *Function);

/// The innermost lambda around \p At whose body \p At is in that copies \p
/// Local, where \p Local names that copy, `[pValue]() mutable { pValue =
/// ...; }`, also an init capture by value, `[pValue = &g_Value]`, or null.
const LambdaExpr *closureCopying(const VarDecl *Local, const Stmt *At);

/// The values the local \p Local can hold where \p At is, in the flow of its
/// function (localSlotValues), each value of a conditional apart, `bFirst ?
/// &First : &Second`; of a local reference what it is bound to; of an
/// address a setter gives it, the location whose address it is. Not what a
/// parameter is given. None when that can't be told: stepped, `++pData`,
/// given to what can change it in a way the flow can't tell, its address
/// used where the flow can't follow it, or read in the body of a closure
/// made in the function, which runs where it is called.
std::optional<llvm::SmallVector<const Expr *, 4>>
localValuesAt(const VarDecl *Local, const Stmt *At);

/// The value a conditional, \p Conditional, can be seen to choose: of a
/// constant condition, also one \p Context evaluates, as what can run is
/// told (canRunIn). Null when it can be either.
const Expr *chosenValue(const AbstractConditionalOperator *Conditional,
                        ASTContext *Context);

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

/// Whether \p Function is a coroutine: its body awaits, yields or returns with
/// `co_return`. Also works for templates, whose bodies are not built as
/// coroutine bodies until they are instantiated.
bool isCoroutine(const FunctionDecl *Function);

/// Whether \p Call runs a coroutine that can't suspend: a `TCFuture`
/// coroutine whose body can be seen, which an override can't replace
/// (calledDefinition), and which never suspends, so it runs to its end in
/// the call and its frame is gone when the call returns. One whose body
/// can't be seen can suspend.
bool callsNonSuspendingCoroutine(const CallExpr *Call);

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

  /// The functions whose bodies are analyzed for \p Function: \p Function
  /// itself when it isn't templated, else its instantiations, like the
  /// specializations of the call operator of a generic lambda. A pattern has
  /// no control flow or types to tell until it is instantiated, so it is never
  /// one of them.
  llvm::SmallVector<const FunctionDecl *, 2>
  bodiesOf(const FunctionDecl *Function, ASTContext &Context);

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

/// What \p Matcher matches among the statements of \p Body that can run, each
/// visited once by the one walker (forEachStmtOnce) and matched by itself,
/// those of the kind t_CNode, in the order of the source: the matcher
/// classifies a node, the walker finds them, so no scan visits what can't run,
/// like the branch `if (false)` never takes.
template <typename t_CNode, typename t_CMatcher>
llvm::SmallVector<ast_matchers::BoundNodes, 4>
matchEach(const t_CMatcher &Matcher, const Stmt &Body, ASTContext &Context) {
  llvm::SmallVector<const t_CNode *, 8> Found;
  forEachStmtOnce(&Body, [&](const Stmt *S) {
    if (const auto *Node = dyn_cast<t_CNode>(S))
      Found.push_back(Node);
    return EWalk::Descend;
  });
  // Of nodes that begin at the same place, the one around the other first.
  const SourceManager &SM = Context.getSourceManager();
  auto Before = [&](SourceLocation Left, SourceLocation Right) {
    if (Left.isInvalid() || Right.isInvalid())
      return Left.getRawEncoding() < Right.getRawEncoding();
    return SM.isBeforeInTranslationUnit(Left, Right);
  };
  llvm::stable_sort(Found, [&](const t_CNode *Left, const t_CNode *Right) {
    if (Left->getBeginLoc() != Right->getBeginLoc())
      return Before(Left->getBeginLoc(), Right->getBeginLoc());
    return Before(Right->getEndLoc(), Left->getEndLoc());
  });
  llvm::SmallVector<ast_matchers::BoundNodes, 4> Matches;
  for (const t_CNode *Node : Found)
    for (ast_matchers::BoundNodes &Nodes :
         ast_matchers::match(Matcher, *Node, Context))
      Matches.push_back(std::move(Nodes));
  return Matches;
}

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

/// Adds to \p Graph, built without exception edges, which clang can't build
/// in the condition of a range-based `for` of an iterator, `__begin !=
/// __end`, an edge to the block that dispatches to the handlers of each
/// `try` from each block with anything in its block, also in the block of a
/// `try` in it: what can throw, and which handler catches it, isn't told.
void addExceptionEdges(CFG &Graph, const ASTContext &Context);

/// Whether \p Owner is a template or in one, a dependent context, whose
/// types and calls only its instantiations tell: a function template, a
/// member of a class template, a generic lambda's call operator, a lambda or
/// a variable in one of them.
bool isTemplatedCode(const Decl *Owner);

/// The control flow of \p Body of \p Owner, built by \p Options; every CFG
/// of the checks is built here. None of what is templated (isTemplatedCode),
/// which clang can't build a CFG of, nor when clang can't build it: each
/// caller takes no graph as a flow that can't be told.
std::unique_ptr<CFG> buildGraph(const Decl *Owner, const Stmt *Body,
                                ASTContext &Context,
                                const CFG::BuildOptions &Options);

/// A value an aggregate initialization gives, with what it initializes: a
/// base, \p Base, a member, \p Field, an array element or what fills the
/// rest of an array, of the element type, or none of them, \p Type null,
/// past the parts it initializes.
struct CAggregatePart {
  const Expr *Init;
  QualType Type;
  const FieldDecl *Field = nullptr;
  bool Base = false;
};

/// The values the aggregate initialization \p Init gives, braces in their
/// semantic form or parentheses, each with what it initializes: of an
/// array, each element and what fills the rest; of a union, the member it
/// initializes; of a class, its bases first and then its members, not an
/// unnamed bit-field, which has none. Nothing of what isn't one.
llvm::SmallVector<CAggregatePart, 4> aggregateParts(const Expr *Init);

/// The value the aggregate initialization \p Init gives the member \p Field
/// (aggregateParts), or null when it gives none, also of what initializes it
/// by default, `int *m_pValue = nullptr;`.
const Expr *aggregateMemberInit(const Expr *Init, const FieldDecl *Field);

/// Whether \p Block dispatches to the handlers of a `try`
/// (addExceptionEdges), which an exception thrown anywhere in its block
/// reaches: the analyses join into it the states before and after each
/// element of the blocks before it that is in that block (inDispatchTry),
/// not their ends, so that a refresh after what throws doesn't hide what the
/// throw leaves.
bool isHandlerDispatch(const CFGBlock *Block);

// Whether \p Element is in the block of the `try` whose handlers \p
// Dispatch, a successor of its block, dispatches to (isHandlerDispatch), of
// that `try` only, not one around it or in it that another dispatches to:
// the states before and after it are what that dispatch's handlers get.
bool inDispatchTry(const CFGBlock *Dispatch, const CFGElement &Element,
                   const ASTContext &Context);

/// The argument of \p Call for the parameter \p iParameter of \p Callee,
/// what it calls: of an operator with an implicit object, after the object,
/// which is its first argument; of one with an explicit object parameter,
/// whose object is that parameter, at its index. Null when it has none.
const Expr *argumentOf(const CallExpr *Call, const FunctionDecl *Callee,
                       unsigned iParameter);

/// The class whose `this` \p Lambda captures: the class of the member
/// function it is in, looking through enclosing lambdas.
const CXXRecordDecl *enclosingThisClass(const LambdaExpr *Lambda);

/// Whether \p E, ignoring parentheses and implicit nodes, is a call of a free
/// function named one of \p Names.
const CallExpr *asCallTo(const Expr *E, llvm::ArrayRef<StringRef> Names);

/// Whether \p S suspends a coroutine: a `co_await` or `co_yield`, but not
/// `co_await` of a coroutine flag, like `ECoroutineFlag_CaptureExceptions`, of
/// `fg_OnResume` or `fg_AsyncDestroy`, or of an awaiter that derives from
/// `CSuspendNever`, like `g_CaptureExceptions`, which don't suspend. Not the
/// implicit initial and final suspends of a promise: how the body starts is
/// coroutineStartsWhereCalled's, and the final one runs after the body.
bool isSuspension(const Stmt *S);

/// Whether the body of the coroutine \p Coroutine starts where it is called,
/// running until it first suspends in the call, the one answer of how a
/// coroutine starts, told by its type and never by looking into its promise:
/// a future of Malterlib, `TCFuture`, `TCFutureWithFlags` and so
/// `TCUnsafeFuture`, does; of another coroutine type, the awaiter its
/// promise's `initial_suspend` returns tells: `std::suspend_always` doesn't,
/// one whose `await_ready()` only returns a constant expression does where
/// that is true, and any other does.
bool coroutineStartsWhereCalled(const FunctionDecl *Coroutine);

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

/// Whether \p S, a statement of the body of the coroutine \p Function, can
/// run after it suspended: also when that can't be told, of a statement its
/// control flow doesn't have.
bool mayRunAfterSuspension(const Stmt *S, const FunctionDecl *Function,
                           ASTContext &Context);

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
  /// Whether a use, a root, where the borrowed data is alive (Rule K, the
  /// one decision aliveWhereUsed: what it designates after a suspension,
  /// what it keeps before one, in the context where it is written,
  /// useContext) is neither one after a suspension
  /// nor one that keeps it, unless the result keeps it (resultUses). Not
  /// where only the result is told, which no keeper of the frame keeps.
  bool AskKeepers = false;
  /// Where set, the use found and its context (CLifetimeUse) when Rule K was
  /// asked of it, for the one emitter (reportLifetime); a null Use when it
  /// was found by what the result keeps, or without Rule K.
  CLifetimeUse *Found = nullptr;
  /// Of a lambda's call operator, whether its closure is kept alive while it
  /// runs, so that what its captures hold is in the context of a use, not
  /// only its parameters and the locals of its body (CLifetimeUse::NotKept).
  bool ClosureKept = true;
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

/// Whether \p Type is an owning pointer whose copies share what it points to,
/// counting references, of the shared and weak kinds of OwnerTypes, like
/// `TCSharedPointer`.
bool isSharedPointerType(QualType Type, const ASTContext &Context);

/// Whether \p Type owns the data it holds in a buffer on the heap, of the
/// buffer or sharedbuffer kind of OwnerTypes by the name of its canonical
/// type, like `TCVector` and `CStr`:
/// made of anything other than one of its own, it copies it, and moved, the
/// one moved to keeps the buffer at the same address. Not one whose layout
/// has room for the data in it (an array member, also of a base or a member
/// held by value), like a `TCVector` with an inline or static allocator.
bool isBufferOwningType(QualType Type, const ASTContext &Context);

bool isSynchronousFunctionType(QualType Type, const ASTContext &Context);

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
/// makes can keep a reference past the suspension: what a function or a
/// constructor keeps of what it is given is told by heldIn (Rule R). A
/// use where the data is alive (BorrowRoots::AskKeepers, Rule K) counts only
/// when the result keeps it.
const Stmt *
firstUseAfterSuspension(const FunctionDecl *Function,
                        llvm::function_ref<bool(const Stmt *)> IsUse,
                        ASTContext &Context,
                        const BorrowRoots *Borrow = nullptr);

/// Whether \p IsUse accepts a statement in the body of \p Function, and the
/// borrowed data is alive at each that does (BorrowRoots::AskKeepers, Rule
/// K), by the context of the code there.
bool aliveAtEachUse(const FunctionDecl *Function,
                    llvm::function_ref<bool(const Stmt *)> IsUse,
                    const BorrowRoots &Borrow, ASTContext &Context);

/// The first use, after a suspension, of what the lambda whose call operator
/// is \p CallOperator captures: the captured variables and `this`, which live
/// in its closure or outside of it. Null when there is none. A use where
/// what it keeps is alive (Rule K, in the context of the call operator's
/// frame, of a closure that isn't kept alive while it runs) isn't one; with
/// \p Found, the use and its context, as BorrowRoots::Found.
const Stmt *firstCaptureUseAfterSuspension(const CXXMethodDecl *CallOperator,
                                           ASTContext &Context,
                                           CLifetimeUse *Found = nullptr);

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
  /// It is put in a local function wrapper that is written in another way
  /// too, so which closure a use of the wrapper sees can't be told: nothing
  /// is said of it, and it counts as called in the scope.
  Untold,
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

bool cancelsGuard(const Stmt *S, const VarDecl *Guard);

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
/// capture, like `[&Alias = Value]`, is a capture by reference of what its
/// initializer designates: a variable it reaches is followed; what else it
/// designates, `[&ThreadLocal = fg_ConcurrencyThreadLocal()]`, is there as
/// what a local reference bound to it is.
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

/// Whether the object of the code \p Lambda is in is alive in the closure,
/// where \p At, in its body or null, uses it (isAlive of the object in
/// closureUse): the closure, or the locals in scope at \p At, hold by value
/// a keeper of it, an owner of its class, also a base or a derived one,
/// `[this, pThis]` of a `TCSharedPointer<CState>`. Weak pointers don't.
bool keepsObjectAlive(const LambdaExpr *Lambda, const Stmt *At = nullptr);

/// Whether the local \p Ref refers to keeps its initial value where \p Ref
/// uses it: in a lambda, the copy it captured must not be changed either.
bool isUnchangedLocalAt(const DeclRefExpr *Ref, const VarDecl *Variable);

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

/// Whether \p Lambda, which isn't a coroutine itself, is called where it is
/// made, by \p ParentOf, with \p Call set to the call.
bool isCalledWhereMade(
    const LambdaExpr *Lambda,
    llvm::function_ref<const Stmt *(const Stmt *)> ParentOf,
    const CallExpr **Call = nullptr);

/// What a call or a construction keeps of what it is given (callKept).
struct CCallKept {
  /// What the call makes that holds it, which goes on with it: the object
  /// a constructor makes, and of a body that can't be seen its result, by
  /// type.
  const Expr *Made = nullptr;
  /// The temporaries of the full expression it is kept in that aren't
  /// values of records, the object it is called on or what another argument
  /// refers to, which go on with it.
  llvm::SmallVector<const Expr *, 2> Temporaries;
  /// The holders that are values of records of the frame, the object it is
  /// called on or what another argument refers to, which hold it there.
  llvm::SmallVector<const Expr *, 2> Values;
  /// The members of the object a constructor makes, or a member function
  /// is called on, that hold it as their value, where a body that can be
  /// seen tells that only they do (CParameterKept::Members); else empty,
  /// the object as a whole.
  llvm::SmallVector<const FieldDecl *, 2> ObjectMembers;
  /// Of a member of ObjectMembers that is a record made as an aggregate, the
  /// paths in it that hold it (CParameterKept::MemberPaths).
  llvm::SmallVector<llvm::SmallVector<const FieldDecl *, 2>, 1> ObjectPaths;
  /// Whether it keeps it beyond the frame: in static storage, in a holder
  /// that isn't a value of the frame nor a temporary, or in the frame of a
  /// coroutine.
  bool bKept = false;
};

/// The index callKept is given for the object a member function is called
/// on, which keeps nothing of itself.
inline constexpr unsigned CallObjectIndex = ~0u;

/// What the call or construction \p Parent keeps of what it gives its
/// parameter \p Index as \p Operand, or of its object (CallObjectIndex), of
/// a body that can't be seen when \p bUnseen, the one decision of it (Rule R, parameterKept): nothing of a
/// handle made from a handle, an owner of a buffer, a handle given or
/// called on, or a function that returns a `TCFuture`; else where the
/// callee keeps it (heldIn): what it makes, static storage, the object it
/// is called on and what other arguments refer to, each held there where it
/// is a value of records of the frame (Values), followed where it is
/// another temporary.
CCallKept callKept(const Stmt *Parent, unsigned Index, const Expr *Operand,
                   bool bUnseen, ASTContext &Context);

/// Whether the pointer value \p Pointer is only compared or tested, `_pValue
/// == nullptr`, `if (_pValue)`, `bool(_pValue)` or `(_bEnabled ? _pValue :
/// nullptr) != nullptr`, which reads only the pointer, not what it points
/// to: where its value stops being handed on unchanged (valueEndOf).
bool isOnlyTestedPointer(const Expr *Pointer, ASTContext &Context);

/// Where what an expression designates or points to is relative to the
/// object of a member function (objectStorageOf).
struct CObjectStorage {
  /// Its own storage: the object, a part of it, or what a pointer it holds as
  /// its storage points to (isOwnedStoragePointer), at any depth.
  bool bOwn = false;
  /// What a borrowed pointer or reference of it, at any depth, leads to
  /// (isBorrowedField), which is elsewhere: the buffer of a view.
  bool bBorrowed = false;
  /// Whether a way can't be told.
  bool bUnknown = false;
  /// Whether a way ends at what isn't of the object: a literal, a global, a
  /// local, a number.
  bool bElsewhere = false;
  /// Whether a way that can't be told was cut where nothing tells what it
  /// can hold: past the bound of provenance, or at a static local that can
  /// be changed; not what a call or a constructor whose body can't be seen
  /// makes, which is told by what it is given.
  bool bCut = false;
};

/// What a function keeps of what it is given for a parameter past its call
/// (parameterKept).
struct CParameterKept {
  /// In a member of its object, also of a base, as it is where it ends.
  bool bObject = false;
  /// In what these other parameters refer or point to (a bit for each by
  /// its index), also a member of it or what it owns; all of them where
  /// that can't be told.
  uint64_t Arguments = 0;
  /// In static storage, a global or a static or what one refers to, by a
  /// body that can be seen: a holder that lasts past every call. A body
  /// that can't be seen isn't assumed to keep it there (Rule R).
  bool bStatic = false;
  /// Whether that can't be told: its body can't be seen.
  bool bUnknown = false;
  /// Of bObject, whether what a reference parameter is bound to can be kept
  /// itself, not only what an owner it is owns (OwnerTypes): a reference or
  /// a pointer to it or a part of it, or what can't be told.
  bool bOwnStorage = false;
  /// Whether what it gives back can keep it: of a body that can be seen, as
  /// its returns tell, which the caller follows; else by the type it gives.
  bool bResult = false;
  /// Of bObject, the members of the object it is the value of, assigned or
  /// initialized with it, `m_pInput(_pInput)`, each once.
  llvm::SmallVector<const FieldDecl *, 2> Members;
  /// Of a member of Members that is a record a constructor makes as an
  /// aggregate, `m_Read{_pInput, _pFallback}`, the paths of members in it
  /// that are given it, the member first; a member of Members with none here
  /// holds it as a whole.
  llvm::SmallVector<llvm::SmallVector<const FieldDecl *, 2>, 1> MemberPaths;
  /// Of bObject, whether it is kept in the object other than as the value
  /// of one of Members: in a base, by a member function called on the
  /// object, in a part of a member, or where that can't be told.
  bool bObjectWhole = false;
  bool operator==(const CParameterKept &Other) const {
    return bObject == Other.bObject && Arguments == Other.Arguments &&
           bStatic == Other.bStatic &&
           bUnknown == Other.bUnknown && bOwnStorage == Other.bOwnStorage &&
           bResult == Other.bResult && Members == Other.Members &&
           MemberPaths == Other.MemberPaths &&
           bObjectWhole == Other.bObjectWhole;
  }
};

/// The operand of a `co_return` of \p Function that can run, a temporary
/// of a record of another type than the view (ViewTypes) the coroutine gives,
/// `co_return CText(Text);` of a `TCFuture<CStrSpan>`, that the conversion
/// into the result keeps by reference (Rule R, parameterKept with the
/// temporary not taken): a constructor of the result that takes it by
/// reference, whose body can't be seen, or one whose body keeps it. What the
/// result refers to is gone when the full expression ends. Null when none.
const Expr *temporaryInResult(const FunctionDecl *Function);

/// The first occurrence of the copies a closure keeps, \p IsCopy, that what
/// the lambda coroutine of the call operator \p Operator gives back refers
/// to, as their own storage (objectStorageOf): what the operand of each
/// `co_return` that can run reaches, `co_return &Value`, also through a
/// local, `auto *pValue = &Value; co_return pValue;`, or a view of it, as the
/// result is made from it, by type: a copy or a move of an operand of the
/// result's type holds what its parts refer to; one of another type, made
/// by a conversion in the promise that can't be seen, can also refer to an
/// operand given as an lvalue when the result can hold a pointer, a
/// reference or a view, `co_return Value;` into a `CResult` with an
/// `int const *` member. Not a copy of it in a result that isn't a pointer,
/// a reference or a view. Null when none.
const Expr *copyInResult(const FunctionDecl *Operator,
                         llvm::function_ref<bool(const Expr *)> IsCopy);

/// Where what \p E, in \p Method, designates, of a glvalue, or points to, of
/// a pointer, is relative to the object of \p Method, `this` or its explicit
/// object parameter, by provenance (CProvenance::reach): through each load,
/// by the member the pointer is loaded from, also through what the member
/// functions it calls give back (returnedRoots), `CImp::f_GetStr()`, only
/// the branch a constant condition takes; arithmetic of a pointer and a
/// number, `m_pStr + m_Length`, is where the pointer is. What a function
/// whose returns can't be seen, or a constructor that doesn't give its
/// members what it is given as it is, makes of what can refer to the object
/// can't be told; a copy the compiler defines is where its source is.
CObjectStorage objectStorageOf(const Expr *E, const CXXMethodDecl *Method,
                               ASTContext &Context);

/// Where what \p E designates or points to, at \p Level of provenance, 1 for
/// what the pointers of an object point to, is relative to the objects \p
/// IsObject is true of, like the copies a closure keeps, as objectStorageOf
/// tells of the object of a member function.
CObjectStorage objectStorageOf(const Expr *E,
                               llvm::function_ref<bool(const Expr *)> IsObject,
                               ASTContext &Context, unsigned Level = 0);

class CProvenance;

/// Where what \p E holds at \p Level of provenance is relative to the
/// objects \p IsObject is true of, as objectStorageOf tells, each object
/// reached, \p At at \p AtLevel, its own storage when \p OwnAt is true of it
/// by what the way takes of each level (CProvenance::projections of \p
/// Walk), else borrowed.
CObjectStorage
objectStorageOf(const Expr *E, llvm::function_ref<bool(const Expr *)> IsObject,
                llvm::function_ref<bool(const Expr *At, unsigned AtLevel,
                                        const CProvenance &Walk)>
                    OwnAt,
                ASTContext &Context, unsigned Level);

/// Whether \p Method, whose body can be seen, only reads and writes the
/// storage of its object, or what isn't of the object, not what its borrowed
/// pointers point to (objectStorageOf), by each statement that runs: calls
/// only member functions of a part of it that do the same, which an override
/// can't replace, `return CImp::f_GetStr();`, and gives nothing of the
/// object that can refer to it to a function.
bool readsOnlyOwnStorage(const CXXMethodDecl *Method, ASTContext &Context);

/// Whether \p S names \p Copy, a value held where the coroutine runs, a
/// parameter taken by value or a capture by copy, like a view, only to use a
/// part of it that is its own, also of a member of it by value that holds a
/// view, `_Source.m_Text`: a member that holds no pointer, reference or view
/// (holdsReference), `Text.m_Length`, `_Source.m_Line`, `Text.m_Length = 0`
/// or bound to a reference, or what a const member function of it that reads
/// only its own storage (readsOnlyOwnStorage) gives, a value of only values,
/// `_Source.m_Text.f_GetLen()`, or a pointer compared or tested,
/// `_Buffer.f_GetStr() != nullptr`, or a pointer of it compared or tested,
/// `_pValue == nullptr`: that is in the frame or the closure, unlike the
/// data the view or pointer refers to; also a reference it gives into its
/// own storage (callResultOf), `_Source.f_GetPosition().m_Line`. \p Held is
/// the type the frame or the closure holds, of a capture the closure's field
/// (closureFieldType), not the type of the variable it copies: `[Text]` of
/// `auto const &Text = Span` holds a view. One predicate for every check
/// that asks whether a use of a view reads what it refers to.
bool readsOnlyOwnPart(const Stmt *S, const ValueDecl *Copy, QualType Held,
                      ASTContext &Context);

/// The type of the field of \p Lambda's closure that \p Capture is, what
/// the closure holds of it: of a copy of a reference, the type it refers to.
QualType closureFieldType(const LambdaExpr *Lambda,
                          const LambdaCapture &Capture);

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
dispatchedMethods(CMemberCall Call);

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
/// a pointer to a function or to a member function. None when it can't be
/// told, also of an argument given to a C ellipsis, `fg_Log("%d", m_Value)`.
std::optional<QualType> argumentParameterType(const Expr *Call,
                                              unsigned iArgument);

/// Whether \p Type is an iterator, like `TCMap<...>::CIterator`, which is at
/// an entry through pointers.
bool isIteratorType(QualType Type);

/// Whether \p S jumps, with a `goto` or a label, outside of the lambdas in
/// it, which can pass a test or run what is written before something again.
bool hasJumps(const Stmt *S);

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

/// Whether \p Variable, or a member, an element or a base of it, is written
/// in \p Scope, also in the lambdas it makes, other than by the name \p
/// Except: assigned, incremented, called with a member function that isn't
/// const, or bound or given to a reference or pointer that isn't to const.
/// A move, what gives it back as an xvalue, `fg_Move(fRead)`, and a const
/// member function, `if (fRead)`, aren't writes.
bool writtenIn(const ValueDecl *Variable, const Stmt *Scope,
               ASTContext &Context, const Expr *Except = nullptr);

/// The evaluated references to \p Decl in \p Scope, also in the lambdas it
/// makes; the references of each scope are found once.
llvm::SmallVector<const DeclRefExpr *, 2>
referencesIn(const Stmt *Scope, const ValueDecl *Decl, ASTContext &Context);

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

/// Whether the integer \p Value is made of the value of a pointer, which a
/// cast can make a pointer again: a conversion of a pointer to an integer,
/// `reinterpret_cast<umint>(&m_Value)`, through parentheses, conversions,
/// offsets, the right of a comma and the arms of a conditional that can run,
/// and an integer local \p Carries is true of.
bool madeOfPointer(const Expr *Value,
                   llvm::function_ref<bool(const VarDecl *)> Carries = {});

/// Whether \p E refers to the object of the code it is in, or the actor that
/// owns it: `this`, `*this`, a member of a helper object that refers to its
/// owner, which Malterlib names after `this`, like `m_pThis` or `&mp_This`, or
/// an unchanged local alias of one of them: the object itself by
/// objectRelation.
/// With \p Context, an alias that a lambda the use is in copies and changes
/// is not one.
bool refersToOwnObject(const Expr *E, ASTContext *Context);

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
/// frees its memory as a `void *`, gives it to a constructor, or compares it,
/// with null or another pointer, `_pFirst == _pSecond`, also through casts, functions that give it back and functions that
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
bool refersToCurrentActorObject(const Expr *E, ASTContext *Context);

/// The lambda that \p E is: a lambda expression, a local of its closure
/// type, or `fg_Move` of one of them. Null otherwise.
const LambdaExpr *asLambda(const Expr *E);

/// What an expression is relative to an object (objectRelation).
enum class EObjectRelation {
  /// Another object, also what doesn't involve it.
  Unrelated,
  /// The object itself, also through parentheses, casts of any kind, its
  /// address or a pointer to it, the temporaries and cleanups around it, both
  /// values of a conditional that both are, the right of a comma, a move or
  /// a forward of it, or a local reference or pointer to it.
  SameObject,
  /// A member of it, also of a member.
  Subobject,
  /// What is in it: an element, what a subscript or a member function of it
  /// gives by reference or pointer, a local iterator made from it, and what
  /// is reached from one of these.
  Entry,
  /// What is made of it in a way that can't be told.
  Unknown,
};

/// What objectRelation tells of an expression.
struct CObjectRelation {
  EObjectRelation Kind = EObjectRelation::Unrelated;
  /// Of a subobject or an entry, the member access that reaches the first
  /// member from the object, `mp_Items` of `mp_Items[0].m_Value` relative to
  /// `this`, when there is one.
  const MemberExpr *Outermost = nullptr;
  /// Whether it is reached through a pointer or a reference that the object,
  /// or what is reached from it, holds: a pointer or reference member, or one
  /// held in an entry, `m_pApp->m_Value` or `mp_Items[0]->m_Value`, or an
  /// owning pointer to an actor, which has a life of its own. What that
  /// points to can be another object, unlike what a local pointer or
  /// reference to it, or a parameter, points to.
  bool bThroughPointer = false;
  /// Whether it is a value a member function made from it, which can refer
  /// into it, like a view or a handle, or be a copy, `mp_Items.f_Copy()`.
  bool bCopy = false;
  /// Whether it is in a member of a union, at any depth, also through a local
  /// alias, whose assignment can end the life of another member of it,
  /// `int &Slot = mp_Items.m_Empty`.
  bool bInUnion = false;
  /// How many collections or owning pointers deep it is, of any kind: 1 for
  /// an entry of a collection of the object, `mp_Items[0]`, or what an
  /// owning pointer member owns, `*mp_pData`, 2 for an entry of a collection
  /// in that, `mp_Items[0].m_Children.f_FindEqual(1)`, or what an owning
  /// pointer in an entry owns, `*mp_Items[0].m_pData`. The most it can be,
  /// kept however it is reached, like the flags.
  unsigned EntryDepth = 0;
  /// Of those, how many are steps into what is stored, by a reference or a
  /// pointer, not a value made from what it is in, like a view, a handle or
  /// an iterator, `mp_Items.f_View()`, which can refer to that itself: what
  /// a change through it can change is no deeper. The least of what it can
  /// be.
  unsigned StoredDepth = 0;
  /// Whether it is a value made from what it is in that can refer to that
  /// itself, like a view or a handle, whose `operator->` gives that, not
  /// what an owning pointer owns one deeper.
  bool bMadeValue = false;
  /// Whether, as a pointer, it can be null: a value of a conditional is,
  /// `bKeep ? this : nullptr`, or it is a `dynamic_cast` to a pointer, which
  /// gives null when the object isn't of the class, also through what keeps
  /// what it is made of, like casts, local aliases, `*&pMaybe`, and helpers
  /// that give back what they are given, `fg_Move(pMaybe)`. When any of what
  /// it can be can.
  bool bCanBeNull = false;
  /// Whether it is what a copy of a shared owner points to, which the copy
  /// keeps alive (OwnerTypes), `*pData` of `auto pData = mp_pData`:
  /// using it isn't using what can be erased, though what is looked up in it
  /// can be, and changing it changes what the source points to. When all of
  /// what it can be are.
  bool bKeptAlive = false;
  bool operator==(const CObjectRelation &Other) const {
    return Kind == Other.Kind && Outermost == Other.Outermost &&
           bThroughPointer == Other.bThroughPointer && bCopy == Other.bCopy &&
           bInUnion == Other.bInUnion && EntryDepth == Other.EntryDepth &&
           StoredDepth == Other.StoredDepth && bMadeValue == Other.bMadeValue &&
           bCanBeNull == Other.bCanBeNull && bKeptAlive == Other.bKeptAlive;
  }
};

/// Whether a value of \p Type can refer to what it was made from: a
/// reference, a pointer, a view of ViewTypes, an iterator, or a class with
/// one, also in a base or a member it holds by value, or one that isn't
/// defined. Not a number, nor a value all its own, a copy.
bool canReferBack(QualType Type, const ASTContext &Context);

/// Whether a copy of \p Type can point where what it is copied from points,
/// the copy model: a pointer, a reference, a view of ViewTypes, an iterator,
/// or an object the compiler copies member by member with such a member,
/// also of a base or of a member it holds by value, or one that isn't
/// defined; not one whose copy constructor is its own, which owns what it
/// copies, `CStr`, nor one of only values.
bool copySharesPointees(QualType Type, const ASTContext &Context);

/// Whether \p Type, also of what a pointer or reference to it is, is a
/// collection: a class, not an iterator, with a member function of
/// RemovingMethods, also of a base.
bool isCollectionType(QualType Type);

/// What an owning pointer, \p Type, points to: a class like one, not an
/// iterator, with `operator->` giving a pointer or `operator*` giving a
/// reference, also inherited, also an instantiation of a member template,
/// like a `TCUniquePointer<CItems>`. Null for another type.
QualType ownedPointee(QualType Type);

// The two levels of a copy. A copy is new at its own level: its storage,
// its members held by value and its entries are its own (bCopy, isCopyStorage);
// what its borrowed parts lead to, references, pointers that aren't storage it
// owns, and what shared owners point to (isBorrowedField, OwnerTypes),
// is what the source's lead to, one level down (objectRelation clears bCopy
// when it follows one). A mixed object is both, by its fields.

/// Whether pointer member \p Field is storage its object owns: of a
/// collection that is a class template, not a view, pointing to what isn't a
/// collection itself, its elements or its nodes, `t_CValue *m_pData`.
bool isOwnedStoragePointer(const FieldDecl *Field);

/// Whether member \p Field holds what its object only refers to: a
/// reference, a pointer that isn't owned storage (isOwnedStoragePointer), or
/// a pointer to a member.
bool isBorrowedField(const FieldDecl *Field);

/// Whether what the member \p Field points or refers to is told by what the
/// code of its class gives it (DMibSetByOwnClass): of a borrowed member
/// (isBorrowedField) of a class that is neither an owner (OwnerTypes) nor a
/// view only when it is annotated so, as anything else can give it another
/// value; of any other member always.
bool targetToldByOwnClass(const FieldDecl *Field);

/// Whether a value of \p Type has parts that lead to what it doesn't own,
/// which a copy shares with its source: a reference or pointer, a borrowed
/// member (isBorrowedField), a shared owner (OwnerTypes), a view, an
/// iterator, a closure, what can't be looked into, a collection of elements
/// that have such parts, or a class with any of these by value, as a member,
/// a base or an array element at any depth. Not a unique owner of what has
/// none, nor a value held inline, like an optional of one.
bool hasBorrowedParts(QualType Type, const ASTContext &Context);

/// Whether a value of \p Type is independent of what it is made from, so
/// that a copy of it keeps nothing of that: it has no borrowed parts
/// (hasBorrowedParts), plain value data, a uniquely owning pointer of what
/// is independent, or a collection of independent elements, `TCVector<int>`.
bool isIndependentType(QualType Type, const ASTContext &Context);

/// Whether a copy of a value of \p Type is new at its own level, a copy of
/// what it holds by value: a class with data of its own, also a mixed one or
/// a shared owner, not one that only refers, a view of pointers, a view of
/// ViewTypes or an iterator, which is what it refers to.
bool isCopyAtOwnLevel(QualType Type, const ASTContext &Context);

/// Whether \p Relation is of the own storage of a copy, none of what its
/// source is (bCopy): what it points to through its borrowed parts is
/// related again, as objectRelation tells.
bool isCopyStorage(const CObjectRelation &Relation);

/// Whether \p Relation is of the object itself or what it holds by value: the
/// object, or a member of it, also of a member or an element of an array
/// member, not reached through a pointer or a reference it holds, nor a value
/// made from it.
bool isHeldByValue(const CObjectRelation &Relation);

/// What \p E, a pointer, can be seen to point to as an expression: what it
/// is the address of, `&X` or `std::addressof(X)`, also through an unchanged
/// local pointer made with that, `auto ppItems = &pItems`. Null otherwise.
const Expr *addressedExpr(const Expr *E);

/// What the member \p Member of an unchanged local made by an aggregate is
/// made with, `Holder.m_ppValue` of `CHolder Holder{&t_pValue}`, also of a
/// member of a member held by value, or null.
const Expr *localMemberInit(const Expr *Member);

/// Whether \p E is a pointer that is held in what it is reached from: a
/// member, `m_pApp`, an element, `mp_Items[0]` of pointers, what a member
/// function or an operator gives a reference to, or what a pointer to a
/// pointer points to, `*ppItems`, not a local alias or a parameter, nor what
/// an assignment, a comma or a conditional gives. objectRelation tells such a
/// pointer as what holds it, its value as through a pointer.
bool isStoredPointer(const Expr *E);

/// What \p E, a pointer, points to relative to the object: objectRelation of
/// it, through a pointer when it is held in what it is reached from
/// (isStoredPointer), `m_pApp` given by reference to a pointer. Of anything
/// else what objectRelation tells.
CObjectRelation pointeeRelation(const Expr *E,
                                llvm::function_ref<bool(const Expr *)> IsObject,
                                ASTContext *Context);

/// Whether \p Type, also of what a pointer or reference to it is, holds its
/// elements in it, fixed in number: a built-in array, or one of
/// FixedArrayTypes, like `std::array`.
bool isFixedArrayType(QualType Type, const ASTContext &Context);

/// Whether \p Method gives a reference to the object it is called on: one
/// that returns a reference to its own class, or a base of it, and whose body,
/// when it can be seen, returns `*this` or its explicit object parameter,
/// `CItems &f_Entries() { return *this; }`, also through a local alias or an
/// address, `&*this`, or a pointer to it, `return this;` (returnedRelation).
bool givesItsObject(const CXXMethodDecl *Method);

/// What \p Method, whose body can be seen and which returns a reference or a
/// pointer, gives of the object it is called on: what each value it returns
/// is relative to `this` or its explicit object parameter (objectRelation),
/// merged. None when its body can't be seen, or while it is worked out.
std::optional<CObjectRelation> returnedRelation(const CXXMethodDecl *Method,
                                                ASTContext *Context);

/// What \p E is relative to the object, what \p IsObject is true of, like
/// `this`, a parameter or the object of an actor: one classifier, a switch
/// over every class of expression, that tells the receiver of a call, what is
/// moved, deleted, assigned or given on, what an alias, an iterator or a
/// lookup refers to, and whether an expression is the object itself or
/// reaches into it. With \p Context, a local alias that a lambda the use is
/// in copies and changes isn't followed.
CObjectRelation objectRelation(const Expr *E,
                               llvm::function_ref<bool(const Expr *)> IsObject,
                               ASTContext *Context);

/// Whether \p Method is named as one of \p Patterns (matchesNamePattern), or
/// is one whose body calls such a member function of its object, `this` or
/// its explicit object parameter, `f_GetArray(_Len)` of `f_SetLen(_Len);
/// return f_GetArray();`, also deeper, but not one that can't change it, nor
/// in a lambda. \p Patterns stay where they are while the AST is analyzed.
bool callsOwnMethod(const CXXMethodDecl *Method,
                    llvm::ArrayRef<llvm::StringRef> Patterns);

/// The lambda that \p Call calls, `fRead()` or `fRead.operator()()`. Null
/// otherwise.
const LambdaExpr *calledLambda(const CallExpr *Call);

/// Whether \p Variable, which a lambda captures, holds a pointer or reference
/// to the object of the code it is in: an init capture like
/// `[pThis = this]`, or an unchanged local like `auto *pThis = this` that is
/// captured by copy. With \p OwnObject, also what refersToOwnObject accepts,
/// and without, also a pointer or reference into it, `[pValue = &m_Value]`.
bool isCapturedAliasOfThis(const VarDecl *Variable, bool OwnObject);

/// Whether \p E is the actor of the code it is in: each object the handle is
/// of (handleObjects) is the object of the current actor, `self`,
/// `fg_ThisActor(this)`, also through a local handle, by what reaches where it
/// is named.
/// The values what \p Ref names can hold where it is: of a copy a closure
/// keeps that its body doesn't change, what it copies where the closure is
/// made; of a local reference bound to what isn't a local slot, what it is
/// bound to; else what each definition of the slots it designates
/// (designatedSlots) that reaches there gives (localSlotReaching). None when
/// that can't be told: a change whose value can't be told, `Actor.f_Clear()`,
/// what a parameter is given, a copy a mutable closure changes; except, where
/// the flow can't tell, the initializer of a local that nothing but this use
/// changes by an assignment (isUnchangedLocal), until what a member function
/// that isn't const writes in its object is told.
std::optional<llvm::SmallVector<const Expr *, 2>>
namedValues(const DeclRefExpr *Ref, ASTContext &Context);

/// The objects whose actor the actor handle \p Handle is of where it is:
/// what `fg_ThisActor(X)` is given and what `X->self` is read of, through
/// conversions, moves, casts, a weak handle and the handle it locks, each
/// value of a conditional (of a constant one the value it chooses), and of a
/// local handle, a parameter or a copy a closure keeps, those of each value
/// that reaches where it is named (localSlotReaching); none when that can't
/// be told.
std::optional<llvm::SmallVector<const Expr *, 2>>
handleObjects(const Expr *Handle, ASTContext &Context);

bool isOwnActor(const Expr *E, ASTContext &Context);

/// The name of the class or class template of \p Type, or an empty name.
StringRef recordName(QualType Type);

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

/// Whether \p Call gives back what it is given, as it is: one of
/// MoveFunctions or ForwardFunctions, also when its body can't be seen, and
/// the builtins of `std::move` and `std::forward`, also an address taken
/// (isAddressOfCall), which gives what its argument designates.
bool givesItsArgument(const CallExpr *Call);

/// Whether \p Type is an ownership transfer, `CCoroutineTransferOwnership`,
/// its awaiter, `CCoroutineTransferOwnershipAwaiter`, which its `operator
/// co_await` gives, or a class derived from either.
bool isTransferOwnership(QualType Type);

/// A pointer parameter that a function sets to another of its parameters,
/// `void fg_Set(int *&o_pValue, int *_pValue) { o_pValue = _pValue; }`, and
/// whether it does on every way through it (setParameters).
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
  /// Of an Addressed one, whether it is set to the address of a part of what
  /// the source refers to, a member or an element of it, `&_Value.m_Value`.
  bool Part = false;
  bool operator==(const CSetParameter &Other) const {
    return Target == Other.Target && Source == Other.Source &&
           Always == Other.Always && Addressed == Other.Addressed &&
           TargetByPointer == Other.TargetByPointer && Part == Other.Part;
  }
};

/// What the argument of \p Call that \p Set targets names, with the arguments
/// of the function's parameters from \p Offset: the pointer it is given by
/// reference, or the one whose address it is given, `&pValue`; null when it
/// isn't the address of one.
const Expr *setTargetArgument(const CallExpr *Call, const CSetParameter &Set,
                              unsigned Offset);

/// The pointers \p Function, whose body can be seen, sets that it is given by
/// a reference or by their address, `int *&o_pValue` or `int **o_ppValue`, to
/// another of its parameters, also into what that points to, `_pBase + 1`,
/// or the address of what a reference parameter refers to or a part of it,
/// `&_Value.m_Value`, as what its flow has at its exit tells (its function
/// summary, summarySetParameters): each value it can set one to, the first on
/// every way when it sets it on every way. Each function once.
llvm::ArrayRef<CSetParameter> setParameters(const FunctionDecl *Function);

/// The sets of setParameters, as the function summary of \p Function tells.
llvm::SmallVector<CSetParameter, 1>
summarySetParameters(const FunctionDecl *Function);

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

/// The function \p Callee designates: a function, its address, or, through
/// an unchanged local made from it, also a pointer to a member function,
/// `auto pmSet = &CView::f_Set`, and its unchanged copies; null otherwise.
const FunctionDecl *functionDesignatedBy(const Expr *Callee);

/// Whether \p Await awaits an ownership transfer: its operand is one, or the
/// awaiter that the `operator co_await` of its operand gives is,
/// `fg_ContinueRunningOnActor(Actor).operator co_await()` of a wrapper.
bool awaitsTransferOwnership(const CoroutineSuspendExpr *Await);

/// The name of the function \p Call calls, or an empty name.
StringRef calleeName(const CallExpr *Call);

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_COROUTINEUTILS_H
