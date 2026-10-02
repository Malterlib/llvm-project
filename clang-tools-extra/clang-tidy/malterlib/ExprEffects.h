//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_EXPREFFECTS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_EXPREFFECTS_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace clang::tidy::malterlib {

/// What a statement does with one of its operands (operandEffects).
enum class EUse {
  /// The operand is the result, or a part of it: parentheses, casts that keep
  /// the object or the value, the temporaries and cleanups around it, the
  /// right of a comma, a value of a conditional, the base of a member or of
  /// `.*`, what an assignment or an increment gives back.
  Forward,
  /// What the glvalue designates is read: a load.
  Access,
  /// What the glvalue designates is read and changed: an increment, a
  /// decrement, a compound assignment. What the prefix and compound forms
  /// give back is the operand (Forward), what the postfix ones give is the
  /// value it had.
  Update,
  /// What the glvalue designates is given a value: the left of an assignment.
  Write,
  /// The pointer is followed: `*p`, `p->m`, `p[i]`, `p->*m`, `delete p`.
  Dereference,
  /// Its address is taken: `&x`, the decay of an array or a function.
  AddressOf,
  /// A reference is bound to it, of what Into tells.
  Bind,
  /// What Into tells is made from it by value.
  Copy,
  /// It is the object a member function is called on, implicit or explicit;
  /// of an explicit object, Into is the parameter that binds it.
  CallObject,
  /// It is what is called.
  Callee,
  /// It decides what runs: a condition.
  Condition,
  /// It is a value that is computed with: an operand of arithmetic or a
  /// comparison, an index, a size.
  Value,
  /// Its value is dropped: the left of a comma, a cast to void.
  Discarded,
  /// It is a statement that runs as part of this one.
  Statement,
  /// It doesn't run here: an operand of `sizeof`, `decltype`, `noexcept`,
  /// a discarded branch of `if constexpr`, the body of a lambda, which runs
  /// where it is called, the syntactic form of what has a semantic one.
  Unevaluated,
  /// What happens to it can't be told: what isn't C++, like OpenMP and
  /// Objective-C, inline assembly, what is dependent.
  Opaque,
};

/// What an operand is given to (CDestination).
enum class EDestination {
  /// Nothing.
  None,
  /// A variable that is initialized: Declaration.
  Variable,
  /// A parameter of what is called: Callee, Index, also past its last
  /// parameter, of a variadic function; Callee is null for a call through a
  /// pointer.
  Parameter,
  /// A member of what is made, Object, initialized: Declaration, a field.
  /// Object is null for one a constructor initializes.
  Field,
  /// What the glvalue Object designates, assigned.
  Designated,
  /// The capture Index of the lambda Object.
  Capture,
  /// The element Index of the array Object makes, also each of its elements,
  /// a filler, by the highest index.
  Element,
  /// A base of the object a constructor makes.
  Base,
  /// What a function returns, also by `co_return`.
  Return,
  /// What `new`, Object, makes.
  Heap,
  /// A temporary, Object, which a reference can extend.
  Temporary,
  /// The exception `throw`, Object, makes.
  Thrown,
};

/// What an operand is given to, of Bind and Copy, and what an assignment
/// gives its right side to.
struct CDestination {
  EDestination Kind = EDestination::None;
  const Decl *Declaration = nullptr;
  const FunctionDecl *Callee = nullptr;
  const Expr *Object = nullptr;
  unsigned Index = 0;
};

/// One operand of a statement and what is done with it.
struct COperandEffect {
  const Stmt *Operand = nullptr;
  EUse Use = EUse::Opaque;
  CDestination Into;
  /// When it is evaluated, relative to the others of the statement: one with
  /// a lower sequence is evaluated before, one with the same is unsequenced
  /// to it, or indeterminately sequenced, like the arguments of a call.
  unsigned Sequence = 0;
};

/// What a statement is, of what the checks tell apart, from its class
/// (effectsOf).
enum class EStatement {
  /// What isn't one of these.
  Other,
  /// A name of a declaration, a DeclRefExpr.
  Name,
  /// `this`.
  This,
  /// A member, of an object or what a pointer points to, a MemberExpr.
  Member,
  /// A call, of a function, a member function, an operator or a literal.
  Call,
  /// What a constructor makes.
  Construction,
  /// A lambda, which makes a closure.
  Lambda,
  /// A declaration statement, of what Declares tells.
  Declaration,
  /// A range-based `for`.
  RangeLoop,
  /// The body of a coroutine.
  CoroutineBody,
  /// A temporary, which a reference can extend.
  Temporary,
  /// What `new` makes.
  New,
  /// `return`.
  Return,
  /// `co_return`.
  Coreturn,
  /// A default argument or a default member initializer, evaluated where it
  /// is used, which isn't its child (CXXDefaultArgExpr, CXXDefaultInitExpr).
  Default,
  /// The construction of the base by the constructor a derived class
  /// inherits, given that constructor's parameters as they are
  /// (CXXInheritedCtorInitExpr).
  InheritedConstruction,
  /// A `std::initializer_list`, which refers to the array it is made of.
  InitializerList,
};

/// What a statement does with its operands, and of itself.
struct CEffects {
  /// What it is.
  EStatement Statement = EStatement::Other;
  /// Of a declaration statement, what it declares.
  llvm::SmallVector<const Decl *, 1> Declares;
  /// Each operand, also several times, `++x`, which is accessed and given
  /// back, in the order the statement has them. Room for one in place: the
  /// translation unit keeps the effects of each statement (effectsOf), most
  /// with one operand or none.
  llvm::SmallVector<COperandEffect, 1> Operands;
  /// Whether it can suspend the coroutine it is in: `co_await` or
  /// `co_yield`.
  bool bSuspends = false;
  /// Whether it leaves the function: `return`, `co_return` or `throw`.
  bool bLeaves = false;
  /// Of a call, the function it calls, also through an unchanged local
  /// pointer to it (callTarget).
  const FunctionDecl *Callee = nullptr;
};

/// What \p S does with each of its operands, one entry for each class of
/// statement, a switch over all of them generated from the list of clang's,
/// so that a class added there can't be missed: what is read, written,
/// bound, copied and to what, what escapes, what suspends and what doesn't
/// run.
const CEffects &effectsOf(const Stmt *S, const ASTContext &Context);

/// The effects of \p S that effectsOf keeps for the translation unit it was
/// last asked of, which \p S must be of and have been asked of.
const CEffects &keptEffectsOf(const Stmt *S);

/// Whether \p Call is a call a coroutine makes of its own promise's
/// `return_value`, `return_void` or `yield_value`, for `co_return` and
/// `co_yield`: plumbing of the result, never a keep or a launch: what it is
/// given is read where the result is made from it (EUse::Access), its
/// object dropped (EUse::Discarded).
bool isPromiseCall(const CallExpr *Call);

/// Whether \p User uses its operand \p Operand, a pointer, past
/// parentheses and implicit casts, only by its value, without what it
/// points to: compared, `this == _pExpected`, computed with, tested as a
/// condition, or discarded (EUse::Value, EUse::Condition, EUse::Discarded),
/// the one classification of such uses for each check.
bool usesOnlyPointerValue(const Stmt *User, const Expr *Operand,
                          const ASTContext &Context);

/// \p E as it is: past what only gives it on as the same value of the same
/// type and category, its only operand (effectsOf), like parentheses,
/// cleanups, temporaries bound, a constant and the braces of a scalar.
const Expr *sameValue(const Expr *E, const ASTContext &Context);

/// The declaration \p E names as it is (sameValue), of a name, the leaf the
/// enumerations end in; null otherwise.
const ValueDecl *namedDecl(const Expr *E, const ASTContext &Context);

/// The member \p E names, of a member (EStatement::Member); null otherwise.
const ValueDecl *memberDecl(const Expr *E, const ASTContext &Context);

/// The function \p E calls directly, of a call (EStatement::Call); null
/// otherwise.
const FunctionDecl *calledFunction(const Expr *E, const ASTContext &Context);

/// The function \p E, a call, calls, also through an unchanged local pointer
/// to a function or a member function, `(Object.*pmGet)()`; null otherwise.
const FunctionDecl *callTarget(const Expr *E, const ASTContext &Context);

/// The object \p E, a call, is called on (EUse::CallObject); null otherwise.
const Expr *callObject(const Expr *E, const ASTContext &Context);

/// What \p Call, a call or a construction, gives its callee's parameter \p
/// iParameter (EDestination::Parameter), also the object an explicit object
/// parameter is bound to; null when nothing is.
const Expr *argumentFor(const Stmt *Call, unsigned iParameter,
                        const ASTContext &Context);

/// Where the default argument or default member initializer \p Default
/// (EStatement::Default) is used, a valid location where its own isn't.
SourceLocation usedLocation(const Stmt *Default);

/// The name of \p Use, for what tests show.
llvm::StringRef useName(EUse Use);

/// The name of \p Kind, for what tests show.
llvm::StringRef destinationName(EDestination Kind);

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_EXPREFFECTS_H
