//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_HELDDATA_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_HELDDATA_H

#include "ExprEffects.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"

namespace clang::tidy::malterlib {

/// What a declaration is as something that holds data (holderKind).
enum class EHolder {
  /// What holds no data: a type, a namespace, a template, an enumerator.
  None,
  /// A variable, local, static, thread-local or a member that is static.
  Variable,
  /// A parameter of a function.
  Parameter,
  /// An implicit parameter, like `self` of Objective-C or that of a
  /// captured statement.
  ImplicitParameter,
  /// The object a structured binding declaration makes or refers to.
  Decomposition,
  /// A structured binding, which is a name of a part of its decomposition.
  Binding,
  /// A non-static data member.
  Field,
  /// A member of an anonymous struct or union, reached through it.
  IndirectField,
  /// A temporary a reference extends.
  Temporary,
  /// A constant of the program: a template parameter object, a constant of
  /// a GUID, an unnamed global constant.
  Constant,
  /// A function or a block, which holds code.
  Function,
  /// A statement that runs where it is declared: an expansion statement,
  /// `template for`.
  Statement,
  /// What isn't C++, like Objective-C and OpenMP.
  Opaque,
};

/// What \p D holds, a switch over every kind of declaration.
EHolder holderKind(const Decl *D);

/// How long what a destination is lives (lifetimeOf).
enum class ELifetime {
  /// Until the end of the full expression it is made in.
  FullExpression,
  /// Until the end of the scope of Function it is in, which isn't a
  /// coroutine.
  Automatic,
  /// Until the end of its scope in the coroutine Function, also across its
  /// suspensions, after which it can resume on another thread.
  Frame,
  /// As long as the thread.
  Thread,
  /// As long as the program.
  Static,
  /// Until it is deleted.
  Dynamic,
  /// As long as the object Object, or `this` when it is null, of which it is
  /// a part: a member, an element, a base, a capture, what is assigned.
  ObjectOf,
  /// As long as the closure of the lambda that has it as an init capture.
  Closure,
  /// As long as the parameter of the callee Function: what the callee does
  /// with it tells.
  Parameter,
  /// What the function Function returns.
  Return,
  /// What can't be told.
  Unknown,
};

/// How long what a destination is lives.
struct CLifetime {
  ELifetime Kind = ELifetime::Unknown;
  const FunctionDecl *Function = nullptr;
  const Expr *Object = nullptr;
};

/// How long \p Variable lives, by its storage duration and the function it is
/// in.
CLifetime variableLifetime(const VarDecl *Variable);

/// How long what \p Into is lives.
CLifetime lifetimeOf(const CDestination &Into);

/// The name of \p Kind, for what tests show.
llvm::StringRef lifetimeName(ELifetime Kind);

/// The name of \p Kind, for what tests show.
llvm::StringRef holderName(EHolder Kind);

/// What a type holds that leads elsewhere, or that a visitor looks into
/// (forEachHeld).
enum class EHeld {
  /// A pointer, Pointee what it points to.
  Pointer,
  /// A reference, Pointee what it refers to.
  Reference,
  /// A pointer to a member, Pointee the member's type.
  MemberPointer,
  /// A pointer to a block.
  BlockPointer,
  /// A class, a struct or a union, Record, by value.
  Record,
  /// What is dependent, of a template's pattern.
  Dependent,
  /// What isn't C++, like an Objective-C object.
  Opaque,
};

/// What a pointer or a reference to \p Type leads to, of an array each
/// element, `int` of `int (&)[4]` and of `int *`, its canonical type without
/// qualifiers: what holding either holds.
QualType heldElement(QualType Type);

/// One part of a type that forEachHeld visits.
struct CHeld {
  EHeld Kind = EHeld::Opaque;
  QualType Type = {};
  QualType Pointee = {};
  const RecordDecl *Record = nullptr;
  /// Of a pointer or a reference, what it leads to (heldElement).
  QualType Element = {};
};

/// What forEachHeld does after a visit.
enum class EVisit {
  /// Stop: what is looked for is found.
  Found,
  /// Don't look into it.
  Skip,
  /// Look into it: what a pointer or reference points to, the bases and
  /// members of a record, or the arguments of its template when it isn't
  /// defined.
  Descend,
};

/// Whether \p Visit finds what it looks for in \p Type: a recursion over
/// every class of type, which goes through sugar, arrays, atomics, complex
/// numbers, vectors and matrices by themselves, and visits pointers,
/// references, pointers to members, records and what is dependent. Each
/// record once.
bool forEachHeld(QualType Type, llvm::function_ref<EVisit(const CHeld &)> Visit);

/// Whether what \p Type holds can be a reference or a pointer, to what it is
/// reached from or to what that holds (forEachHeld), not a value of its own:
/// a pointer, a reference, or a record with one, also a view; not a number
/// or an object of only numbers.
bool holdsReference(QualType Type);

/// The name of \p Kind, for what tests show.
llvm::StringRef heldName(EHeld Kind);

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_HELDDATA_H
