//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ACTORANNOTATIONS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ACTORANNOTATIONS_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <optional>
#include <string>

namespace clang::tidy::malterlib {

/// The phases of the life of an actor that the annotations of
/// `Mib/Concurrency/TidyAnnotations` name. Once an actor is in a phase, it
/// stays in it: the flags that tell are only ever set.
enum EActorPhase : unsigned {
  /// The actor is destroyed: `fp_DestroyInternal` has set the flag that
  /// `f_IsDestroyed()` returns, and `fp_Destroy` runs.
  ActorPhase_Destroy = 1u << 0,
  /// The app stops: `f_StopApp` has set `m_bStoppingApp`, and `fp_StopApp`
  /// runs.
  ActorPhase_StopApp = 1u << 1,
  /// Not a phase: the call that added an entry of a collection may remove it,
  /// by the key it added it with.
  ActorPhase_Owner = 1u << 2,
};

/// The kinds of annotations, as the first argument of `clang::annotate`.
inline constexpr llvm::StringLiteral AnnotationActorEntries =
    "malterlib-actor-entries";
inline constexpr llvm::StringLiteral AnnotationActorPhase =
    "malterlib-actor-phase";
inline constexpr llvm::StringLiteral AnnotationActorPhaseEntry =
    "malterlib-actor-phase-entry";
inline constexpr llvm::StringLiteral AnnotationActorPhaseFlag =
    "malterlib-actor-phase-flag";
inline constexpr llvm::StringLiteral AnnotationActorPhaseGuard =
    "malterlib-actor-phase-guard";
inline constexpr llvm::StringLiteral AnnotationRunsToCompletion =
    "malterlib-runs-to-completion";
inline constexpr llvm::StringLiteral AnnotationRunsToCompletionEntry =
    "malterlib-runs-to-completion-entry";

/// The member functions of the standard library that give an iterator,
/// which can remove the entry it is at; the Malterlib options file adds
/// Malterlib's, IteratorMethods. The ones that remove entries are
/// CVocabulary::RemovingMethods.
inline constexpr llvm::StringLiteral BuiltInIteratorMethods =
    "begin;end;rbegin;rend";
/// The containers of the standard library whose entries stay where they are
/// when other entries are added or removed; the Malterlib options file adds
/// Malterlib's, NodeStableContainers.
inline constexpr llvm::StringLiteral BuiltInNodeStableContainers =
    "std::map;std::set;std::multimap;std::multiset;std::list;"
    "std::forward_list";

/// The phases that an annotation of \p Kind on \p D names, also on another
/// declaration of it, and for a member function on a function it overrides.
/// None when it isn't annotated.
std::optional<unsigned> annotatedPhases(const Decl *D, llvm::StringRef Kind);

/// Whether \p D, or another declaration of it, has an annotation of \p Kind.
/// A member of a class annotated with \p Kind is annotated too, and so is a
/// class derived from one.
bool hasAnnotation(const Decl *D, llvm::StringRef Kind);

/// The names of \p Phases, like "Destroy and StopApp".
std::string phaseNames(unsigned Phases);

/// The innermost function that \p S is in, through the lambdas made in it,
/// which run in the same phase as it, being made after the flags were set.
const FunctionDecl *enclosingPhaseFunction(const Stmt *S, ASTContext &Context);

/// The phases \p Function runs in: those of a `DMibActorPhase` or
/// `DMibActorPhaseEntry` function. Zero when it isn't one.
unsigned contextPhases(const FunctionDecl *Function);

/// Whether \p Object is the object of the actor whose phase the context is:
/// `this`, or what is named for it, like the `pThis` of a static function or
/// the `m_pThis` of a protocol, also through unchanged local copies, and what
/// it holds by value or owns. A pointer to another actor, like a member
/// `m_pApp`, isn't. Null is the own object.
bool isOwnActorObject(const Expr *Object, int Depth = 0);

/// Whether \p Call, of a static or free function, is given another actor
/// than the own: an object of one, `fs_Stop(pOther)`, or a handle to one,
/// `fs_Stop(Other)` of `TCActor<CApp> Other`, which what the function does
/// is of, also only objects by pointer or reference that aren't of the live
/// object of the own actor, like a copy of its state, `fg_IsStopping(&State)`.
bool givenAnotherActor(const CallExpr *Call, ASTContext &Context);

/// Whether \p E, a value a callback returns, is an error: made by a function,
/// a message, or constructed from what describes it, like `DMibErrorInstance
/// (...)`. A variable, a null pointer or an empty result may be success.
bool isErrorResult(const Expr *E);

/// The phases whose flags \p E tests, like `mp_State.m_bStoppingApp ||
/// f_IsDestroyed()`: a flag, a function that returns it, or a disjunction
/// with them.
unsigned testedPhaseFlags(const Expr *E);

/// The phases that an `fg_OnResume` callback guards: those whose flags it
/// tests and returns an error for, or throws for, before it can return
/// success.
unsigned guardedPhases(const LambdaExpr *Lambda);

/// The function a call through a pointer to one calls, `fStop(this)`: the
/// one an unchanged local or capture it is read from was made from, `auto
/// fStop = &fg_Stop`. Null when that can't be told.
const FunctionDecl *calleeThroughPointer(const CallExpr *Call);

/// The phases that \p S starts or runs: the phase entries and the functions
/// of phases it calls, also in the lambdas it makes, which it can call, and
/// through the functions it calls, like a `f_StopWrapper()` that calls
/// `f_StopApp()`. Not in the branch an `if constexpr` discards.
unsigned startedPhases(const Stmt *S, const ASTContext &Context);

/// The phases that destroying an object of \p Record starts or runs, like
/// `startedPhases` of its destructor's body, also of the destructors of its
/// members and bases, which run after it, also when it has none written.
unsigned destructionStartedPhases(const CXXRecordDecl *Record,
                                  const ASTContext &Context);

/// The phases a variable initialized with \p Init guards on every resume
/// while it lives: `co_await f_CheckDestroyedOnResume()`, another
/// `DMibActorPhaseGuard` function, or an `fg_OnResume` callback that tests
/// the flags.
unsigned guardedPhasesOfHolder(const Expr *Init,
                               const ASTContext *Context = nullptr);

/// The call of a `DMibActorPhaseGuard` function of the own actor that a
/// variable initialized with \p Init holds the registration of, like `co_await
/// f_CheckDestroyedOnResume()`, whose callback reads the object of the actor
/// where the coroutine resumes, also one of the values of a conditional.
const CallExpr *ownPhaseGuardCall(const Expr *Init,
                                  const ASTContext *Context = nullptr);

/// Whether \p Key designates a local variable initialized with a function of
/// \p FreshKeyFunctions and not changed after, also through unchanged copies,
/// that \p Function adds an entry to \p Field of \p Object by: the key of the
/// entry that the call owns.
bool isOwnEntryKey(const Expr *Key, const FieldDecl *Field,
                   const FunctionDecl *Function,
                   llvm::ArrayRef<llvm::StringRef> FreshKeyFunctions,
                   ASTContext &Context, const Expr *Object);

/// Whether \p Left and \p Right are the same object: both the own actor's,
/// or the same variable.
bool isSameCollectionObject(const Expr *Left, const Expr *Right);

/// The local variable that \p Key designates, through moves and copies, and
/// the captures of lambdas that copy it.
const VarDecl *keyVariable(const Expr *Key);

/// Whether \p Name matches one of \p Patterns, of which a trailing `*`
/// matches any end.
bool matchesNamePattern(llvm::StringRef Name,
                        llvm::ArrayRef<llvm::StringRef> Patterns);

/// Splits a semicolon-separated list of an option.
void splitOptionList(llvm::StringRef List,
                     llvm::SmallVectorImpl<llvm::StringRef> &Out);

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_ACTORANNOTATIONS_H
