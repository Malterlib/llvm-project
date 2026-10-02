//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_RUNSTOCOMPLETION_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_RUNSTOCOMPLETION_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include <optional>

namespace clang::tidy::malterlib {

/// Code that runs to completion: once it suspends, it only ends by finishing,
/// never by being aborted, because it only suspends on actors that are not
/// destroyed while it runs, like the concurrent actors. Whoever starts it
/// joins it before what it refers to is gone, so it may refer to the data of
/// its callers, like a `TCUnsafeFuture`, while the checks keep checking the
/// rest of it.

/// Whether \p Function is declared to run to completion with
/// `DMibRunsToCompletion`, on itself or on its class.
bool isRunsToCompletion(const FunctionDecl *Function);

/// Whether \p Function is declared with `DMibRunsToCompletionEntry`: it
/// begins running to completion, by switching to a concurrent actor before it
/// first suspends.
bool isRunsToCompletionEntry(const FunctionDecl *Function);

/// Whether the callers of \p Function join it, so that what its parameters and
/// its object refer to lives until it ends: it runs to completion and isn't an
/// entry, which may be called from anywhere.
inline bool isJoinedByCallers(const FunctionDecl *Function) {
  return isRunsToCompletion(Function) && !isRunsToCompletionEntry(Function);
}

/// Whether \p Function returns a future, so that it can suspend.
bool returnsFuture(const FunctionDecl *Function);

/// Whether \p E is an actor that is not destroyed while code runs on it: a
/// concurrent or blocking actor, like `fg_ConcurrentActor()`, also through an
/// unchanged local.
bool isConcurrentActor(const Expr *E, const ASTContext &Context);

/// How the future of a call or dispatch is joined.
struct RunsToCompletionJoin {
  /// The `fg_AsyncDestroy` guard that awaits the local the future is
  /// collected into, or null when it is awaited in its full-expression.
  const VarDecl *Guard = nullptr;
  /// The local the future is collected into, with a guard, or null for the
  /// cleanup of the guard itself.
  const VarDecl *Collection = nullptr;
  /// Where the guard is awaited early, like `co_await fg_Move(Guard)`, which
  /// joins while the locals around are there. Otherwise the coroutine joins
  /// the cleanup of the guard only when it finishes, after its locals are
  /// gone.
  SourceLocation EarlyAwait;
};

/// How the future that \p Future gives is joined: `co_await` in its
/// full-expression, also through `.f_Wrap()` and `% "Context"`, or collected
/// into a local, like `> Results`, `Results.f_Insert(...)` or `auto Result =
/// ...`, that an `fg_AsyncDestroy` guard declared after the local and before
/// \p Future awaits in its cleanup. None when it isn't joined.
std::optional<RunsToCompletionJoin> joinOf(const Expr *Future,
                                           ASTContext &Context);

/// How \p Lambda is started and joined: called directly, given to
/// `fg_CallSafe` or dispatched, and the result joined. \p Abortable is set
/// when it is dispatched to an actor that may be destroyed while it runs.
std::optional<RunsToCompletionJoin>
lambdaJoin(const LambdaExpr *Lambda, ASTContext &Context, bool *Abortable);

/// Whether \p S runs to completion: in a `DMibRunsToCompletion` or
/// `DMibRunsToCompletionEntry` function, or in a lambda given to one, or
/// started and joined in one without being dispatched to an actor that may be
/// destroyed.
/// Whether the cleanup that \p Call, an `fg_AsyncDestroy`, is given runs in
/// the scope it is made in: the call is awaited, and the guard it gives is a
/// local that is only destroyed there, or awaited early, which
/// \p AwaitedEarly tells.
bool isCleanupJoined(const CallExpr *Call, ASTContext &Context,
                     bool *AwaitedEarly = nullptr);

/// The guard of the joined cleanup of \p Call, like isCleanupJoined, and in
/// \p EarlyAwait where it is awaited early.
const VarDecl *cleanupGuard(const CallExpr *Call, ASTContext &Context,
                            SourceLocation &EarlyAwait);

/// Whether \p S is in code that runs to completion: a function that does, or
/// an entry, with \p AfterSwitch only where the entry runs on the concurrent
/// actor it switched to itself, or a lambda that such code joins.
bool inRunsToCompletionContext(const Stmt *S, ASTContext &Context,
                               bool AfterSwitch = false);

/// The operand that \p Suspend, a `co_await` or `co_yield`, awaits.
const Expr *awaitedOperand(const Expr *Suspend);

/// The first suspension of \p Function, outside of its lambdas, in the order
/// it is evaluated.
const Expr *firstSuspension(const FunctionDecl *Function);

/// Whether \p S, in code that runs to completion, runs on a concurrent
/// actor: in a function that runs to completion, after an entry has switched
/// to one, or in a lambda dispatched to one. Before an entry switches, it runs
/// on the actor of its caller, which may be destroyed.
bool runsOnConcurrentActor(const Stmt *S, ASTContext &Context);

/// The temporary that \p Reference, a reference variable, is bound to and
/// extends, like the `CValue{1}` of `CValue const &Value = CValue{1}`.
const MaterializeTemporaryExpr *extendedTemporary(const VarDecl *Reference);

/// Whether \p Variable lives until \p Join of something started at \p Site:
/// a local declared before the guard, in a scope around it, a parameter of the
/// function, which for a reference or pointer must run to completion, so that
/// its callers keep what it refers to, or for a join in the full-expression,
/// what \p Lambda can refer to while it runs.
bool outlivesJoin(const ValueDecl *Variable, const RunsToCompletionJoin &Join,
                  const Stmt *Site, const LambdaExpr *Lambda,
                  ASTContext &Context);

/// Whether \p Variable, a local or parameter of a coroutine that \p Lambda, an
/// `fg_AsyncDestroy` cleanup, is made in, is there when the cleanup runs in
/// code that runs to completion: the functions around it are never aborted,
/// and what a reference or pointer parameter refers to lives until the
/// callers join the function, which must run to completion. A parameter lives
/// until the coroutine joins the cleanup when it finishes; a local only until
/// the cleanup first suspends, which the caller checks.
bool outlivesEnclosing(const ValueDecl *Variable, const LambdaExpr *Lambda);

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_RUNSTOCOMPLETION_H
