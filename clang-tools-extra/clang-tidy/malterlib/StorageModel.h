//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_STORAGEMODEL_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_STORAGEMODEL_H

#include "RunsToCompletion.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <memory>
#include <optional>

namespace clang::tidy::malterlib {

class MalterlibCheck;
class MalterlibDiagnostic;
struct COperandEffect;

// The one model of whether the storage a pointer, a reference or a view
// refers to is alive where a closure or a coroutine uses it: where each value
// points, as storage roots by provenance (storageRootsOf), and whether each
// root lives at a use (isAlive), decided by type: storage that isn't of a
// frame or a temporary lives where the context holds, by value, a keeper of
// it, an owner of OwnerTypes that fits one of its anchors (holdsKeeper).

/// What an owner of OwnerTypes keeps of what it owns (ownerKindOf).
enum class EOwnerKind {
  /// Not an owner.
  None,
  /// A copy and a move keep it, counting references.
  Shared,
  /// Refers to what a shared owner owns without keeping it.
  Weak,
  /// A move keeps it.
  Unique,
  /// A move keeps the buffer, at the same address.
  Buffer,
  /// A buffer copies share, counting references, and copy before they change
  /// it: a copy and a move keep it.
  SharedBuffer,
};

/// The kind \p Type, also through a reference, is of OwnerTypes, by the name
/// of its canonical type: an entry names a class or a template, and an alias
/// is never looked up. Of the `buffer` or `sharedbuffer` kind, not a type whose
/// layout has room for the data in it (isBufferOwningType), like a `TCVector`
/// with an inline or static allocator.
EOwnerKind ownerKindOf(QualType Type, const ASTContext &Context);

/// Whether a copy of an owner of the kind \p Kind keeps what the source owns:
/// of `shared` and `sharedbuffer`; a move keeps it of any kind but `weak`.
bool copyKeepsOwned(EOwnerKind Kind);

/// Whether an owner of the kind \p Kind holds its data in a buffer on the
/// heap, which a view made of it refers to: of `buffer` and `sharedbuffer`.
bool ownsBuffer(EOwnerKind Kind);

/// Where storage lives (storageRootsOf).
enum class ERootKind {
  /// Static storage, not of a thread: a global, a static, a string literal.
  Static,
  /// The storage of a variable of a thread, `thread_local`, Variable, also
  /// an extended temporary of one: alive on its thread only (isAlive).
  ThreadLocal,
  /// The object of an actor, `this` or its explicit object parameter, and
  /// what it holds by value.
  Actor,
  /// The object of a class of CAllowUnsafeThis, and what it holds by value.
  UnsafeThis,
  /// The object of another class.
  Object,
  /// A local or a parameter held by value, or a part of one, of the function
  /// that Variable is of, also an extended temporary of the local it extends.
  Frame,
  /// A temporary that ends with its full expression, Made.
  Temporary,
  /// What the reference or pointer parameter Variable refers to.
  Parameter,
  /// What owners own (OwnerTypes), past members and elements of it and the
  /// storage it owns, not past what it only refers to: Anchors are the
  /// owners dereferenced on the way, Outer and Variable where the outermost
  /// of them is.
  Owned,
  /// What can't be told.
  Unknown,
};

/// What a keeper of a borrow fits, a type (holdsKeeper): of what an owner of
/// the `shared` or `unique` kind owns, its owned type; of what a `buffer` or
/// `sharedbuffer` one owns, or one whose owned type can't be told, the
/// owner's own type; of an object, its class.
struct CAnchor {
  /// The canonical type, without qualifiers.
  const Type *Of = nullptr;
  bool operator==(const CAnchor &Other) const { return Of == Other.Of; }
};

/// The anchor of what an owner of the type \p Owner owns (CAnchor), which a
/// keeper of that type fits; none when it isn't an owner that keeps.
std::optional<CAnchor> ownerAnchorOf(QualType Owner, const ASTContext &Context);

/// The anchor of an object of the type \p Object (CAnchor).
CAnchor objectAnchorOf(QualType Object);

/// What an operator or a cast does with a pointer value it is given
/// (pointerOperationOf).
enum class EPointerOperation {
  /// Compares or tests it, reading only the value: `==`, `<`, `<=>`, `!`,
  /// `&&`, a conversion to `bool`.
  Compare,
  /// Makes a pointer elsewhere in the same storage from it: `+`, `-`, `++`,
  /// `+=`.
  Offset,
  /// Reaches what it points to: `*`, `->*`.
  Dereference,
  /// Converts it, keeping its value: a load, a cast between pointer types,
  /// to or from an integer, a decay.
  Convert,
  /// Takes no pointer value, or makes or discards one: arithmetic, an
  /// assignment's store, the address of an lvalue, a null pointer.
  None,
};

/// What the binary operator \p Kind does with a pointer operand.
EPointerOperation pointerOperationOf(BinaryOperatorKind Kind);
/// What the unary operator \p Kind does with a pointer operand.
EPointerOperation pointerOperationOf(UnaryOperatorKind Kind);
/// What the cast \p Kind does with a pointer operand.
EPointerOperation pointerOperationOf(CastKind Kind);

/// A step of the derivation of a value that changes what keeps the storage it
/// reaches (CStorageRoot::Hops); the steps that don't, a local's initializer,
/// a reference binding, an address-of and a dereference of what it gives, a
/// member, an element of a built-in array, a cast, a call into what it is
/// called on or given (callResultOf), are followed by the walk itself.
struct CHop {
  enum class EKind {
    /// A dereference of an owner (OwnerTypes) of OwnerKind, what it owns of
    /// Anchor; Past when the way from it to the use passes a pointer, a
    /// reference or a view that what it owns holds, not owns.
    Owner,
    /// A step through what can't be told to be owned: a member function
    /// whose returns can't be seen called on what isn't an owner, like a
    /// lookup into an entry of a container that isn't an owner.
    Opaque,
  };
  EKind Kind = EKind::Opaque;
  EOwnerKind OwnerKind = EOwnerKind::None;
  std::optional<CAnchor> Anchor;
  bool bPast = false;
  /// Of Owner, the owner as the way reaches it, where its value is read
  /// (keeperOwns); null where the way reaches an owner member without one.
  const Expr *Owner = nullptr;
  bool operator==(const CHop &Other) const {
    return Kind == Other.Kind && OwnerKind == Other.OwnerKind &&
           Anchor == Other.Anchor && bPast == Other.bPast &&
           Owner == Other.Owner;
  }
};

/// One storage a way of provenance ends at (storageRootsOf).
struct CStorageRoot {
  ERootKind Kind = ERootKind::Unknown;
  /// Of Frame, ThreadLocal and Parameter, the variable; of an object, its
  /// explicit object parameter, null of `this`, also of an actor a parameter
  /// is given at each call; of Owned, that of Outer.
  const ValueDecl *Variable = nullptr;
  /// Of Temporary, the temporary.
  const Expr *Made = nullptr;
  /// Of an object, its class.
  const CXXRecordDecl *Class = nullptr;
  /// Of Owned, an anchor of each owner dereferenced on the way from the
  /// outermost storage to it, also before a pointer or a reference that
  /// isn't owned, and then of what that refers to, `CContext` and `CJob` of
  /// `_Batch.m_pContext->m_pJob->m_Result` and of
  /// `pContext->m_pRawJob->m_Value`, also of what a member function whose
  /// returns can't be seen gives, `pContext->f_GetJob()->m_Value`; of
  /// Unknown, the type of what the value the way starts at refers to.
  llvm::SmallVector<CAnchor, 2> Anchors;
  /// Of Owned, the kind of the storage the outermost owner is in.
  ERootKind Outer = ERootKind::Unknown;
  /// Whether it is a part of it, a member, a base or an element, not the
  /// whole.
  bool bPart = false;
  /// Of Owned, whether the way from the owners to it passes a pointer or a
  /// reference that isn't owned, so that the owners only anchor it: what
  /// they own doesn't hold it.
  bool bPastBorrowed = false;
  /// Of Owned, the derivation's steps that change what keeps it (CHop),
  /// the one nearest the use first.
  llvm::SmallVector<CHop, 2> Hops;
  /// Of Unknown, the pointer or reference member the way reads whose target
  /// can't be told (targetToldByOwnClass), which a keeper made of it doesn't
  /// own.
  const FieldDecl *Member = nullptr;
  /// Of Unknown with a raw pointer Member, the local record it was read
  /// from, whose type isn't an owner and has a destructor of its own: what
  /// the pointer points to can be storage the record manages, which ends
  /// with it.
  const VarDecl *Record = nullptr;
  bool operator==(const CStorageRoot &Other) const {
    return Kind == Other.Kind && Variable == Other.Variable &&
           Made == Other.Made && Class == Other.Class &&
           Anchors == Other.Anchors && Member == Other.Member &&
           Record == Other.Record &&
           Outer == Other.Outer && bPart == Other.bPart &&
           bPastBorrowed == Other.bPastBorrowed && Hops == Other.Hops;
  }
};

/// The root of the object of the class \p Class that isn't an actor nor of
/// CAllowUnsafeThis (ERootKind::Object), whose anchor is its class.
CStorageRoot objectRootOf(const CXXRecordDecl *Class);

/// Each storage the ways of provenance of a value end at (storageRootsOf).
struct CStorageRoots {
  /// None of only null pointers, which refer to nothing.
  llvm::SmallVector<CStorageRoot, 2> Roots;
  /// Whether a way ends in a null pointer, or passes a `dynamic_cast` of a
  /// pointer that checks the class, which gives one where it doesn't match.
  bool Null = false;
  /// Whether each is of \p Is.
  bool all(llvm::function_ref<bool(const CStorageRoot &)> Is) const;
  /// Whether there are roots and each is static storage.
  bool allStatic() const;
  bool operator==(const CStorageRoots &Other) const {
    return Roots == Other.Roots && Null == Other.Null;
  }
};

/// Whether \p Variable, a local, a parameter or a capture, is a record held
/// by value that isn't a view (ViewTypes) nor an owner (OwnerTypes), also an
/// array of them, whose parts Rule V's boundary tells apart (recordPartAt).
bool isOpaqueRecord(const ValueDecl *Variable, const ASTContext &Context);

class CProvenance;

/// What a walk of provenance reaches of a record held by value
/// (recordPartAt).
enum class ERecordPart {
  /// Not such a record, or its storage at level 0: followed as anything else.
  Followed,
  /// A copy of it, or the value of a pointer, a reference or a view member
  /// stored in it: a value, of which nothing is followed.
  Value,
  /// The value of a pointer or a reference member stored in it (Field), at
  /// the level of what it refers to: where each value the code of its class
  /// gives it refers to (memberValueRoots), which can be anything unless that
  /// is all it is ever given (targetToldByOwnClass).
  Assigned,
  /// Its own storage above level 0: what a `buffer` or `sharedbuffer`
  /// template with arguments that aren't the defaults, which holds its data
  /// in itself, points to.
  Own,
  /// What an owner member of it owns (Owner).
  Owned,
};

/// What the walk of provenance \p Walk at \p At, at \p Level, where it
/// takes what Walk's projections say, reaches of a record that isn't a view
/// nor an owner (isValueRecordType): the one boundary of provenance of Rule
/// V, decided where a member is loaded, by the types of the members the way
/// loads, however the record is reached: at its name, a member of it,
/// `Info.m_pValue`, through a pointer or a reference to it,
/// `pContext->m_pValue`, a structured binding of a member of it, `auto
/// [pValue] = Info`, a member function whose body can be seen that returns
/// such a member, `Info.f_Job()` of `return m_Job;`. A reference member, and
/// a pointer or a view member above level 0, give a value. Of a record held
/// by value (isOpaqueRecord), also through a local reference to one or to a
/// part of one (designatedSlots), its own storage and what an owner member
/// owns are told too; Record is the record of Own and Owned.
struct CRecordPart {
  ERecordPart Kind = ERecordPart::Followed;
  const VarDecl *Record = nullptr;
  const FieldDecl *Owner = nullptr;
  /// The member of Assigned.
  const FieldDecl *Field = nullptr;
};
CRecordPart recordPartAt(const Expr *At, unsigned Level,
                         const CProvenance &Walk, ASTContext &Context);

/// What the result of a call is relative to its object and its arguments
/// (callResultOf).
enum class ECallResult {
  /// A body that can't be seen, one an override can run instead, a way that
  /// can't be told, ways that don't agree, or what a view or an actor refers
  /// to through what it stores, which provenance follows.
  Untold,
  /// Into the object's own storage, `return m_Position;`, also what a
  /// `buffer` or `sharedbuffer` template that holds its data in itself
  /// points to.
  OwnStorage,
  /// What an owner member of the object owns, `return *m_pJob;` (Owner), or
  /// what the object, an owner, owns, `return m_pValue;` of a unique pointer.
  Owned,
  /// The value of a pointer, reference or view member stored in the object,
  /// a record that isn't a view, an owner nor an actor, `return m_pJob;` of
  /// `TCPointer::f_Get()`: a value, of which nothing is followed (Rule V).
  StoredBorrow,
  /// The object or an argument itself (Given), `return *this;`.
  Given,
  /// Nothing of the object or the arguments: a value it makes, or what has
  /// static storage or is captured, as it names it.
  New,
};

struct CCallResult {
  ECallResult Kind = ECallResult::Untold;
  /// Of Owned, the owner member; none when the object is the owner.
  const FieldDecl *Owner = nullptr;
  /// Of Given and of the kinds of the object's storage, the object or the
  /// argument as the call is given it, also a pointer to what it is of.
  const Expr *Of = nullptr;
  /// Whether Of is the object the call is made on, also an explicit one.
  bool bObject = false;
  /// Of StoredBorrow, the pointer or reference member whose value it is at
  /// the level of what that refers to, which refers to where the code of
  /// its class points it (ERecordPart::Assigned).
  const FieldDecl *Field = nullptr;
};

/// What the result of \p E, a call, is at \p Level relative to its object
/// and its arguments, from the returned roots of a body that can be seen
/// (CProvenance::returnedRoots): each way of each return by the type of what
/// it ends at and the members it takes there, by recordPartThrough's rules,
/// all ways the same, also of each override the call can run. What it ends
/// at is the object, also through a pointer to it, or what an argument is
/// given as: what it designates, by reference, or what it points to, by
/// pointer, each alike, `return _pContext->m_pJob;` a stored member of what
/// the argument points to. Above level 0, a way that ends at level 0 of the
/// whole object is what the result's type holds, which recordPartAt tells.
/// The one answer to what a getter gives: none of its callers assumes that
/// the object owns what it returns.
CCallResult callResultOf(const Expr *E, unsigned Level, ASTContext &Context);

/// Where what \p E designates or points to lives, by provenance
/// (CProvenance::reach), at \p Level: at 0 the storage a glvalue designates
/// or a pointer points to, above it what the value stored there, loaded,
/// points to. Each value-flow edge is followed: copies, moves, the aliases of
/// local references and pointers, members, elements and bases, what getters
/// and forwarding functions whose bodies can be seen give back
/// (returnedRoots), the arms that run of conditionals, the views made of a
/// pointer and a length or of another view; a pointer member of an object is
/// where each value it is ever given points (memberValueRoots). What an owner
/// owns is Owned, with the anchor of each owner on the way.
CStorageRoots storageRootsOf(const Expr *E, ASTContext &Context,
                             unsigned Level = 0);

/// Where what the pointer-valued expression \p E points to lives
/// (storageRootsOf), whatever its value category: of a prvalue pointer what it
/// points to, of a glvalue what the pointer it designates points to, also an
/// argument bound to `T *&`, and of an array, which decays, the array. The one
/// answer of which level that is: no caller picks it.
CStorageRoots pointeeRootsOf(const Expr *E, ASTContext &Context);

/// Where what a pointer pointed at \p E points to lives: of a pointer value,
/// or a location that holds one, what it points to (pointeeRootsOf); of a
/// location of another type, whose address the pointer is given, `o_pValue
/// = &_Value;` of a setter given it, that location (storageRootsOf).
CStorageRoots pointedAtRootsOf(const Expr *E, ASTContext &Context);

/// Where each value the member \p Field of a pointer, a reference or a view
/// is ever given refers to (storageRootsOf): its default member initializer,
/// those of the constructors, and what the member functions of its class
/// assign it; a null one nothing; unknown when a member function or a friend
/// that can give it anything can't be seen, or a value is changed in place,
/// and of a pointer or a reference member unless that is stated
/// (targetToldByOwnClass, DMibSetByOwnClass).
CStorageRoots memberValueRoots(const FieldDecl *Field, ASTContext &Context);

/// Where what each pointer or reference member (isBorrowedField) of the
/// record \p Record designates refers to, also of the records it holds by
/// value, which a copy of it shares: of a member whose target can't be told
/// (targetToldByOwnClass) Unknown, else where each value the code of its
/// class gives it refers to (memberValueRoots) and, of a record that is a
/// member of the object of \p Function, what the initializers of that member
/// in its class give it, `m_Request{{}, &m_Value}`; Unknown where those can't
/// be told or a member function of the class assigns that member.
CStorageRoots borrowedPartRoots(const Expr *Record,
                                const FunctionDecl *Function,
                                ASTContext &Context);

/// Whether a value of \p Type holds owners that keep what they own as its
/// parts, by value: a record in members or bases, an array, or a container
/// of them, whose copy copies them; the one classification of what holds
/// owners, which the keepers of a value go by (holdsKeeper).
bool holdsOwnerParts(QualType Type, const ASTContext &Context);

/// The call operators of \p Closure that can run: of a generic lambda each
/// specialization of it, as instantiated, else its call operator.
llvm::SmallVector<const CXXMethodDecl *, 2>
runningOperators(const LambdaExpr *Closure);

/// Whether \p Type can refer to storage it doesn't own: it holds a pointer, a
/// reference or a view, not only in owners (OwnerTypes) other than weak
/// ones, which keep what they own (forEachHeld).
bool holdsBorrowed(QualType Type, const ASTContext &Context);

/// How a value a context holds is taken: where a holding is made of an
/// expression, a capture, a result or an argument, by the category of the
/// expression as written; else held.
enum class ETake {
  /// Copied from an lvalue: a by-copy capture, an init capture or an
  /// argument made of an lvalue; it keeps through `shared` and
  /// `sharedbuffer` owners.
  Copy,
  /// Moved, an xvalue, or made, a prvalue, also the name of a local or a
  /// parameter a result moves implicitly: it keeps through any owner.
  Move,
  /// A parameter or a local held by value, declared with its type: it keeps
  /// through any owner.
  Held,
};

/// A value a context holds by value, of its declared type, taken as Take.
struct CHeldValue {
  QualType Type;
  ETake Take = ETake::Copy;
  /// Of a local, the variable: it is held only where it is in scope at
  /// Where, declared before it in a scope still open there (inScopeAt).
  const VarDecl *Local = nullptr;
  /// Of a local, where the context is used: the use, or, of a function
  /// around a lambda that isn't a coroutine, the lambda; null where no use is
  /// told, a closure wherever in its body, where no local is held.
  const Stmt *Where = nullptr;
  /// Of a local, whether it holds only where it is declared before Where;
  /// else wherever its scope is open there, of a value kept in a local of
  /// the frame, which is read where that local is, after Where.
  bool bOrdered = true;
  /// The variable whose slot holds it where Where is: a local, a parameter,
  /// or what a closure captures by copy; null where none is told.
  const VarDecl *Holder = nullptr;
  /// What it is made of where it is taken: the initializer of a capture, the
  /// value a result is made of, an argument; null where none is told.
  const Expr *Made = nullptr;
  /// Of a capture, the closure: where no use is told, it holds what it is
  /// made of only where the code of the closure doesn't change it.
  const LambdaExpr *Closure = nullptr;
  /// Of the object of a member function that joins a callee, that function:
  /// a member of it holds what it holds at the join, Where, or at the exit
  /// where Where is null.
  const FunctionDecl *ObjectOf = nullptr;
};

/// A callee that is joined by the end of an expression, the context "alive
/// until this join" (CStorageUse::Joined): one that code that runs to
/// completion starts and joins, or that a callee that runs what it is given to
/// completion before it returns joins (BlockingCallees).
struct CJoined {
  /// How it is joined: in the full expression where it starts, or by a guard
  /// that is awaited early or where the function that has it finishes.
  RunsToCompletionJoin Join;
  /// Where it starts: the call, or the closure that is the callee.
  const Stmt *Site = nullptr;
  /// The closure that is the callee, when it is a lambda.
  const LambdaExpr *Started = nullptr;
};

/// The context where storage is used, the second half of Rule K (isAlive):
/// what it holds by value, each as it is taken, and what tells whether frame
/// storage and the object of an actor are alive there. Made by one function
/// for each kind of context: useContext where a use is, and where a value is
/// kept closureUse, resultUse, argumentsUse.
struct CStorageUse {
  /// What the context holds by value.
  llvm::SmallVector<CHeldValue, 4> Held;
  /// The closure that refers to it, of a use the innermost lambda whose code
  /// it is in.
  const LambdaExpr *Closure = nullptr;
  /// The functions whose frames are alive there: of a use, the function
  /// executing it and, through each lambda that isn't a coroutine, the
  /// function where that lambda is (useContext).
  llvm::SmallVector<const FunctionDecl *, 2> Frames;
  /// The function the code runs in, past the lambdas around it: what a
  /// parameter of it that each call gives the object of the actor the
  /// caller runs on refers to is that actor's (givenOwnActor).
  const FunctionDecl *RunsIn = nullptr;
  /// Whether it runs on the actor whose object refers to it, as its own
  /// callback.
  bool bOnOwnActor = false;
  /// The class of a closure that isn't kept alive while it runs, whose
  /// captures aren't held there and whose copies aren't alive there; null
  /// when each is.
  const CXXRecordDecl *NotKept = nullptr;
  /// Of a callee until it is joined, the join (CJoined), which tells whether
  /// each kind of storage is alive there (isAlive).
  std::optional<CJoined> Joined;
};

/// The parent of a statement in the body a context is of; without it, the
/// instantiated parents of the translation unit (instantiatedParents).
using FParentOf = llvm::function_ref<const Stmt *(const Stmt *)>;

/// The context of the closure \p Closure where a value it keeps is used,
/// wherever in its body: its captures by copy and init captures and the
/// parameters of its call operators held by value; not the locals of their
/// bodies, which hold only where they are in scope at a use. A lambda
/// that isn't a coroutine also has what the functions and lambdas around it
/// hold, their locals in scope where it is, out to the nearest coroutine or
/// the outermost function, wherever it is called. As a callback of the actor whose member function, also
/// through the lambdas around it, makes it. No frame is alive there.
CStorageUse closureUse(const LambdaExpr *Closure, ASTContext &Context);

/// The context of the use \p Use, where it is written, the one context of a
/// use: the function executing it (by \p ParentOf, then the instantiated
/// parents), whose frame is alive there, holds its parameters held by
/// value and its locals held by value in scope at the use; of a lambda's
/// call operator also its closure's captures, unless its class is \p
/// NotKept, which isn't kept alive while it runs. When that function is a lambda
/// that isn't a coroutine, the context nests into the context of where the
/// lambda is, its locals in scope there, out to the nearest coroutine or the
/// outermost function.
CStorageUse useContext(const Stmt *Use, ASTContext &Context,
                       FParentOf ParentOf = {},
                       const CXXRecordDecl *NotKept = nullptr);

/// Whether \p Named is the name of an entity that `return` and `co_return`
/// move implicitly: a variable of the function, or a parameter, held by
/// value and not volatile, not a capture.
bool namesImplicitlyMovable(const Expr *Named);

/// The context of the value \p Made, a result: the value by its type, or of
/// a `co_return` by the type \p Result the promise stores, taken as the
/// result expression is; made from \p Made of another type, a new value.
CStorageUse resultUse(const Expr *Made, ASTContext &Context,
                      QualType Result = {});

/// The context of the call \p Call of \p Callee while the callee runs: the
/// parameters held by value, by their types, each taken as its argument is.
CStorageUse argumentsUse(const CallExpr *Call, const FunctionDecl *Callee,
                         ASTContext &Context);

/// Where a value is kept, which tells the context Rule K asks about it in
/// (keptContext).
struct CKeptAt {
  enum class EKind {
    /// A capture of the closure Closure.
    Capture,
    /// A local of the function executing the use, whose context is the
    /// use's (useContext).
    Local,
    /// The result Made, of the type Result the promise stores.
    Result,
    /// An argument of the call Call of Callee.
    Argument,
    /// What a callee that is joined by Join, started at Site, holds until the
    /// join: the arguments of the call Call of Callee, or the closure Closure
    /// that is the callee.
    Joined,
  };
  EKind Kind = EKind::Local;
  const LambdaExpr *Closure = nullptr;
  const Expr *Made = nullptr;
  QualType Result;
  const CallExpr *Call = nullptr;
  const FunctionDecl *Callee = nullptr;
  const RunsToCompletionJoin *Join = nullptr;
  const Stmt *Site = nullptr;
};

/// The context of a value kept at \p Kept, the one map of a keeping place to
/// its context: a capture, what its closure holds (closureUse); a result,
/// the result's (resultUse); an argument, the call's (argumentsUse); what a
/// callee holds until it is joined, what the code where it starts holds
/// (useContext of Site), of a guard that is only awaited where its function
/// finishes only the parameters of that function, and what the callee holds,
/// its arguments or its closure, with the join (CStorageUse::Joined). A local
/// is where the use is, which aliveWhereUsed tells (useContext).
CStorageUse keptContext(const CKeptAt &Kept, ASTContext &Context);

/// Whether \p Root is alive at \p Use, the one predicate of it (Rule K):
/// static storage is; the object of an actor in its own callbacks, also in
/// a function each call gives it to, and of a class of CAllowUnsafeThis;
/// the frame within the same frame, also the copies the closure keeps; what
/// owners own, another object, what a parameter refers to, and what can't be
/// told when it has an anchor, when what the context holds by value has a
/// keeper of one of its anchors (holdsKeeper); of a parameter, also a class
/// of CAllowUnsafeThis. Not a temporary, nor what is of a thread: each
/// context is a closure, a frame, a result or a call that can run on another
/// thread or after a suspension that can resume on one. Until a join
/// (CStorageUse::Joined), in one place for every kind: static storage, an
/// object, the object of an actor and of a class of CAllowUnsafeThis are,
/// which the callers of the code keep; a local or a parameter of the
/// functions around while they are there at the join (frameAliveAtJoin);
/// a temporary of the full expression where the callee starts and is
/// joined; what a parameter refers to when the callers of its function join
/// it, or by a keeper; what owners own and what can't be told by a keeper;
/// never what is of a thread.
bool isAlive(const CStorageRoot &Root, const CStorageUse &Use,
             ASTContext &Context);

/// The anchors of the derivation \p Derivation, the expression a borrow is
/// derived by (Rule K): every object type on it, by a work stack over every
/// subexpression of it (children, syntax only: it tells types), whatever
/// its kind, each once: a glvalue gives the type of the object it
/// designates, a pointer value what it points to, so an owner gives what it
/// owns where it is dereferenced, by its `operator->`, `operator*` or a
/// getter. The name of a local reference or pointer, or of an init capture,
/// is followed to what it is initialized and assigned with, a structured
/// binding to what it designates, and an opaque value to its source. Not
/// into a statement, the body of a lambda. At most 4096 expressions, told
/// once for each derivation. Of each type on it also the anchors of the
/// owners it holds by value, transitively by the one holder classification,
/// not of itself: what `f_Result()` gives of an object that holds
/// `TCSharedPointer<CJob> m_pJob` is anchored by `CJob` too, whether or not
/// the getter's body can be seen.
llvm::ArrayRef<CAnchor> derivationAnchors(const Expr *Derivation,
                                          ASTContext &Context);

/// Whether what \p Use reaches at \p Level is alive in the context \p At,
/// the one entry point of Rule K at a use: the roots of what it reads
/// (storageRootsOf), which tell the kind of storage, each alive by isAlive,
/// or by a keeper of an anchor of the derivation \p Derivation, by default
/// \p Use itself: every object type on it, of every subexpression, also
/// through local pointers and references and by-copy captures to what they
/// are initialized and assigned with; or else by \p OrElse. Of the name of
/// a variable a closure captures by copy, what the copy refers to, the copy
/// itself being the closure's.
bool aliveAtUse(const Expr *Use, unsigned Level, const CStorageUse &At,
                ASTContext &Context, const Expr *Derivation = nullptr,
                llvm::function_ref<bool(const CStorageRoot &)> OrElse = {});

/// The whole expression \p Use is in, which keeps what it makes of it: the
/// outermost expression around it, by \p ParentOf or else the instantiated
/// parents, not past a lambda it is the capture of.
const Expr *keptDerivationOf(const Expr *Use, FParentOf ParentOf,
                             ASTContext &Context);

/// What is done with the value of a use where the part of its access path
/// that hands it on unchanged ends (valueEndOf).
enum class EValueEnd {
  /// Compared or tested: nothing of what it refers to is touched there.
  Compared,
  /// What it points to is reached: `*`, `->`, `[]`, `->*`, `delete`.
  Dereferenced,
  /// A pointer elsewhere in the same storage is made of it: `+`, `++`.
  Offset,
  /// Given to a call, or the object of one, which can keep it (Rule R).
  Given,
  /// Anything else: kept, stored, returned, bound, captured.
  Kept,
};

/// What a use does to the storage it designates or points to
/// (storageUsesOf): the set of what its ends do.
struct CStorageUses {
  /// What is there is read: loaded, copied, given by reference to a body
  /// that can't be seen.
  bool bRead = false;
  /// It is written, also destroyed.
  bool bWritten = false;
  /// Given to a body that can't be seen, which uses what it points to.
  bool bGiven = false;
  /// Kept beyond the frame: stored in a member, a global or what isn't a
  /// value of the frame, captured by a closure that runs later, returned by
  /// a coroutine, or kept by what a call makes or is given (Rule R).
  bool bKept = false;
  /// Of the uses of a parameter in its body, the levels of the storage at
  /// which a `return` gives it back, one bit each: the caller goes on with
  /// the call's value at each.
  unsigned ReturnedLevels = 0;
  /// Read, written, given or kept through a name of the frame it is put in,
  /// a local pointer or reference, a binding, or a capture of a closure that
  /// isn't called where it is made: what holds it there can outlive a
  /// suspension.
  bool bHeld = false;
  /// Whether it is only compared or tested, or not used at all.
  bool none() const {
    return !bRead && !bWritten && !bGiven && !bKept && !ReturnedLevels;
  }
  CStorageUses &operator|=(const CStorageUses &Other) {
    bRead = bRead || Other.bRead;
    bWritten = bWritten || Other.bWritten;
    bGiven = bGiven || Other.bGiven;
    bKept = bKept || Other.bKept;
    ReturnedLevels |= Other.ReturnedLevels;
    bHeld = bHeld || Other.bHeld;
    return *this;
  }
  bool operator==(const CStorageUses &Other) const {
    return bRead == Other.bRead && bWritten == Other.bWritten &&
           bGiven == Other.bGiven && bKept == Other.bKept &&
           ReturnedLevels == Other.ReturnedLevels && bHeld == Other.bHeld;
  }
  bool operator!=(const CStorageUses &Other) const { return !(*this == Other); }
};

/// Whether what a walk of storageUsesOf gives to \p Parent by \p Effect,
/// \p E with the storage at \p At, which goes beyond the frame there, keeps
/// it: of a capture of a closure that runs later and of the result a
/// `co_return` makes, what the caller tells by Rule K and the result's type.
using FKeptAt = llvm::function_ref<bool(const Expr *E, unsigned At,
                                        const COperandEffect &Effect,
                                        const Stmt *Parent)>;

/// Whether \p Type, also an array of it, is a record that isn't a view
/// (ViewTypes), an owner (OwnerTypes) nor an actor (isActorClass), which is
/// never a copy and whose object has rules of its own (ObjectReached): a
/// value, whose stored pointer, reference and view members Rule V's boundary
/// doesn't follow; what is put in one held by value is over.
bool isValueRecordType(QualType Type, const ASTContext &Context);

/// What \p Use does to the storage at \p Level of it, 0 what a glvalue
/// designates or what a pointer value points to, the one function of it,
/// for a reference and a pointer alike. It reports a use only where it
/// reaches one: a read or a write of the contents, in the function or in a
/// body that can be seen, a gift to a body that can't be seen, or a keep
/// beyond the frame (Rule R); every other end is over. From a glvalue: a
/// load reads, a write writes, a member or element access goes on with the
/// part, `&` goes on with the pointer value, binding a reference goes on
/// with it, a dereference's glvalue discarded, `(void)*pEntry`, reads. From
/// a pointer value: compared or tested touches nothing, a conversion, an
/// offset or a value handed on unchanged goes on, a dereference, also `->*`
/// and `.*`, goes on with the glvalue. A value of a type that can't hold a
/// pointer, a reference or a view, an integer, is over. A call and a
/// construction alike: given to a parameter of a body that can be seen, it
/// goes on into that body with the parameter (the summary engine, over each
/// body the call can run), and so does the object of a member function with
/// `this`; a body that gives it back hands it on, `return *this;` its
/// object; given to one that can't be seen, what it is bound to is read,
/// and written when not const, what it points to is given. What the call
/// makes or is given that keeps it (Rule R, callKept) goes on with it: the
/// result or the object made by its type, a holder that isn't a value of
/// the frame keeps it. What names it in the frame goes on with its names: a
/// reference variable, a structured binding by reference, the variable of a
/// range-based `for` by reference, a local pointer it is copied into,
/// `pInitial = pValue;`, a capture by a closure whose code runs in the
/// frame (called in the scope that makes it, or the one the use is in); a
/// copy of what is there reads it. Put in a record that is a value
/// (isValueRecordType) held in the frame, an aggregate, an element, a
/// member, a constructor's object, a compiler-defined copy or move
/// assignment's, or an `std::initializer_list`'s array, it is over (Rule V).
/// Returned, stored in a member or a global, captured by a closure that runs
/// later, it is kept, where \p KeptAt, when given, tells so. An unevaluated
/// operand touches nothing; a step that can't be told because it is
/// type-dependent reads, unless it is parentheses or a conditional, which go
/// on. By \p ParentOf, else the instantiated parents with the implicit
/// nodes.
CStorageUses storageUsesOf(const Expr *Use, unsigned Level,
                           ASTContext &Context, FParentOf ParentOf = {},
                           FKeptAt KeptAt = {});

/// What the integer \p Number, made of the value of a pointer, does to what
/// that pointer points to, by the walk of storageUsesOf: converted back to a
/// pointer it is the pointer again, copied into an integer local its names go
/// on, through parentheses, conversions to another integer, offsets and the
/// arms of a conditional it goes on; compared, tested, hashed, logged, given
/// or returned as a number it touches nothing.
CStorageUses numberUsesOf(const Expr *Number, ASTContext &Context);

/// The walks the calls of storageUsesOf share while it is alive, made where a
/// check starts a function and ended where it is done with it: each use of a
/// parameter asks again what the same names reach and what the same places
/// hold. Where none is alive each call works out its own.
class CStorageUsesScope {
public:
  explicit CStorageUsesScope(ASTContext &Context);
  CStorageUsesScope(const CStorageUsesScope &) = delete;
  CStorageUsesScope &operator=(const CStorageUsesScope &) = delete;
  ~CStorageUsesScope();

  struct CShared;

private:
  std::unique_ptr<CShared> Shared;
  // The scope alive where this one was made, alive again where it ends.
  CShared *Previous;
};

/// The end of the part of an access path that hands a value on unchanged
/// (valueEndOf).
struct CValueEnd {
  /// The outermost expression the value is handed on to unchanged.
  const Expr *Top = nullptr;
  /// What Top is in, which does Kind with it.
  DynTypedNode Consumer;
  EValueEnd Kind = EValueEnd::Kept;
  /// Whether it was converted on the way to a value the provenance doesn't
  /// follow, an integer, `reinterpret_cast<umint>(pValue)`.
  bool bConverted = false;
  /// What the use does to what it points to, a pointer named, or else to
  /// what it designates or points to (storageUsesOf).
  CStorageUses Uses;
};

/// What is done with the value of \p Use, read at the end of its access path
/// (accessPathOf) where the steps stop handing the value on unchanged: past
/// parentheses, casts that keep it, loads, the arms of a conditional, the
/// right of a comma and `?:`, each a step of the provenance at the same level
/// that isn't a part, nor a write or an update of it; there, what the
/// operator or the cast does with it (pointerOperationOf), else what the
/// expression or statement it is in does with it by the effect table. Past a
/// conversion that keeps the value but that the provenance doesn't follow,
/// to an integer, what is done with what it converts to. By \p ParentOf, else
/// the instantiated parents with the implicit nodes, whatever the traversal
/// of the check that asks.
CValueEnd valueEndOf(const Expr *Use, FParentOf ParentOf, ASTContext &Context);

/// Which storage a use that a lifetime report is about uses (CLifetimeUse),
/// the one rule of it: no site picks a level.
enum class EUsed {
  /// What the use as written touches where it is used, read off its access
  /// path: what the outermost expression of it designates or refers to and
  /// what each step on it reads. Derived by its access path.
  Designated,
  /// What the value made of the use as written, which something keeps past
  /// the use, refers to: the outermost expression of its access path, a
  /// pointer or a view also what it refers to, and what a suspension on it
  /// reads when it resumes. Derived by the whole expression the use is in,
  /// which keeps it.
  Kept,
  /// Of a borrow from an entry of actor state, which another call of the
  /// actor can erase or replace (Q9): the allocation it lands in, what the
  /// owner of the hop nearest the use owns (CStorageRoot::Hops), when that is
  /// a dereference of an owner of the `shared` or `unique` kind not past what
  /// it only refers to, alive where the context of the use holds a keeper of
  /// what that owner owns. The use is a name of the local reference or
  /// pointer the derivation initializes; of a pointer, what it points to.
  Landed,
  /// What a pointer the context uses is pointed at by the use as written
  /// (pointedAtRootsOf), where it is kept (KeptIn). Derived by itself.
  Pointee,
};

/// A use of storage that a lifetime report is about: the use as written, at
/// the use site, by the parents of the body it is found in, and which
/// storage of it is used. Its context is where it is written (useContext);
/// only what is kept elsewhere, Kept and Pointee, names where (KeptIn).
struct CLifetimeUse {
  const Expr *Use = nullptr;
  EUsed Used = EUsed::Designated;
  /// The parents of the body the use is found in, or else the instantiated
  /// parents.
  FParentOf ParentOf = {};
  /// The class of a closure that isn't kept alive while it runs
  /// (CStorageUse::NotKept).
  const CXXRecordDecl *NotKept = nullptr;
  /// Of Kept and Pointee, where the value is kept; a local is where the use
  /// is.
  CKeptAt KeptIn;
};

/// Whether what \p Use uses is alive in its context, the one decision of
/// every lifetime report (Rule K): the use's access path, the inverse of the
/// provenance's step, each expression around the use whose provenance
/// derives from what is below it, gives the derivation and its anchors
/// (derivationAnchors), Used the storage
/// (storageRootsOf), the context its holdings (useContext of the use, or
/// keptContext of where it is kept), and each root is alive by isAlive. A
/// copy a closure keeps is alive unless the closure is NotKept.
bool aliveWhereUsed(const CLifetimeUse &Use, ASTContext &Context);

/// What a lifetime report says: a warning at Location, with the arguments
/// Arguments gives, and, unless Note is empty, a note at the use.
struct CLifetimeReport {
  SourceLocation Location;
  llvm::StringRef Message;
  llvm::function_ref<void(const MalterlibDiagnostic &)> Arguments = {};
  llvm::StringRef Note = {};
  llvm::function_ref<void(const MalterlibDiagnostic &)> NoteArguments = {};
};

/// The one emitter of every report that says storage may be gone, destroyed,
/// moved, erased, stale or dangling where something uses it: \p Report, by
/// \p Check, unless what \p Use uses is alive (aliveWhereUsed). Whether it
/// reported.
bool reportLifetime(MalterlibCheck &Check, const CLifetimeUse &Use,
                    ASTContext &Context, const CLifetimeReport &Report);

/// Where what \p Capture of \p Closure refers to lives: by reference, what
/// its initializer designates; a copy, what it refers to, of a pointer, a
/// view or an object that holds them, a level down (storageRootsOf).
CStorageRoots captureRoots(const LambdaExpr *Closure,
                           const LambdaCapture &Capture, ASTContext &Context);

/// Whether the storage \p Capture of \p Closure refers to is alive while the
/// closure runs (captureRoots): by what the closure holds wherever in its
/// body (closureUse without a use), or at each use of it in its body
/// (aliveAtUse), by the locals in scope there too, unless \p EachUse is
/// false.
bool captureAlive(const LambdaExpr *Closure, const LambdaCapture &Capture,
                  ASTContext &Context, bool EachUse = true);

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_STORAGEMODEL_H
