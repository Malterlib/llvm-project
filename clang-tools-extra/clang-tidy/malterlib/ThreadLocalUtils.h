//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_THREADLOCALUTILS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_THREADLOCALUTILS_H

#include "CoroutineUtils.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace clang::tidy::malterlib {

/// The lists of the options of the thread-local checks, none built in: the
/// names of a library, like Malterlib's, are in its options file.
struct CThreadLocalLists {
  /// ThreadLocalFunctions: the functions that give the thread-local data of
  /// the thread that calls them.
  llvm::SmallVector<StringRef, 16> Functions;
  /// ThreadLocalTypes: the classes of thread-local data, by name, also
  /// qualified (threadLocalTypeName).
  llvm::SmallVector<StringRef, 8> Types;
  /// ThreadLocalScopeTypes: the scopes that keep a reference to it and that
  /// the coroutine runtime doesn't move with the coroutine.
  llvm::SmallVector<StringRef, 4> ScopeTypes;
  /// ThreadLocalTemplates: the class templates that hold a value for each
  /// thread, whose first argument, a class that isn't a template's, must be
  /// one of ThreadLocalTypes, and whose member functions are their
  /// implementation.
  llvm::SmallVector<StringRef, 4> Templates;
  /// ThreadLocalHandlerTypes: the bases of the scopes that the coroutine
  /// runtime suspends and resumes with the coroutine, on the thread it
  /// resumes on.
  llvm::SmallVector<StringRef, 2> HandlerTypes;
  /// ScopeExitHelpers: the variables whose `operator /` makes a guard that
  /// runs the closure it is given where the guard ends, by name.
  llvm::SmallVector<StringRef, 4> ScopeExitHelpers;
};

/// The name of \p Record that ThreadLocalTypes give it by: qualified by the
/// namespaces and classes it is in, not anonymous or inline namespaces, up
/// to a function it is local to, `NMib::NFile::CThreadLocal`.
std::string threadLocalTypeName(const CXXRecordDecl *Record);

/// Whether \p Function is named by one of \p Patterns: its name, also that of
/// an operator, `operator->`, or of a conversion as its class declares it,
/// `operator t_CData *`, after the names of the classes and namespaces it is
/// in, as many as the pattern has, `TCThreadLocal::f_Get` or
/// `fg_ConcurrencyThreadLocal`, spaces aside.
bool namedFunction(const FunctionDecl *Function,
                   llvm::ArrayRef<StringRef> Patterns);

/// Whether \p E gives the thread-local data of the thread that runs it: a
/// call of one of \p Functions given nothing but its object, also an
/// explicit object parameter, not one given the thread,
/// `fg_SystemThreadLocal(_ThreadID)`, or a variable declared `thread_local`,
/// also a static member named through an object, `this->ms_tData`.
bool isThreadLocalData(const Expr *E, llvm::ArrayRef<StringRef> Functions);

/// What is thread-local data in a function, by the types and functions of the
/// options (CThreadLocalLists): what ThreadLocalFunctions give, `thread_local`
/// variables, what a pointer or a reference to a thread-local type refers to,
/// also `this` of a member function of one, and the local aliases, members and
/// elements of those. A call's result is only when its return type is a pointer
/// or a reference to a thread-local type, or it is one of ThreadLocalFunctions.
class CThreadLocalData {
public:
  CThreadLocalData(ASTContext &Context, const CThreadLocalLists &Lists)
      : Context(Context), Lists(Lists), Functions(Lists.Functions) {}

  /// Whether \p Type, also through a pointer or a reference to it when \p
  /// Through, is a thread-local type, one of ThreadLocalTypes.
  bool isType(QualType Type, bool Through = false) const;

  /// Whether the glvalue \p E is thread-local data, also a part or an entry
  /// of it, as objectRelation tells of the roots (isRoot): not what a pointer
  /// it holds points to, nor a copy.
  bool isData(const Expr *E) const;

  /// Whether the value of the pointer \p E points into thread-local data, as
  /// pointeeRelation tells of the roots (isRoot).
  bool pointsTo(const Expr *E) const;

  /// Whether the glvalue \p Target, what is assigned, is thread-local data
  /// itself (isData), not a variable, nor a pointer of a reference or a
  /// pointer to a thread-local type, which points to it but is stored apart,
  /// `m_pData` of `CData *m_pData`.
  bool isStorage(const Expr *Target) const;

  /// Whether \p E, bound to \p Bound, a reference or a pointer, refers to
  /// thread-local data: a pointer into it, also given by a reference to a
  /// pointer, or a glvalue of it a reference is bound to.
  bool refersTo(const Expr *E, QualType Bound) const;

  /// Whether the local, the parameter or the capture \p Variable refers to
  /// thread-local data: a parameter or a variable that isn't local of a
  /// reference or a pointer to a thread-local type, or a local reference, a
  /// local pointer or a local array of them made to refer to it, also a
  /// pointer, or a parameter held by value, assigned to point into it
  /// anywhere in its function where that can run.
  bool isVariable(const VarDecl *Variable) const;

  /// Whether a member of \p Record can refer to thread-local data: one of a
  /// thread-local type, a scope that derives from one of
  /// ThreadLocalHandlerTypes (isHandler), or one of ThreadLocalScopeTypes.
  bool isExempt(const RecordDecl *Record) const;

  /// Whether \p Record is a scope the coroutine runtime suspends and resumes
  /// with the coroutine: one that derives from one of
  /// ThreadLocalHandlerTypes.
  bool isHandler(const CXXRecordDecl *Record) const;

  /// Whether \p Record is one of ThreadLocalTemplates, or a specialization of
  /// one.
  bool isTemplate(const CXXRecordDecl *Record) const;

  /// What each of ThreadLocalTemplates that \p Type is, or holds in it as an
  /// argument of a template, also of a pack, like
  /// `TCAggregate<TCThreadLocal<CData>>`, holds for each thread, a class that
  /// isn't a template's.
  llvm::SmallVector<const CXXRecordDecl *, 2>
  heldPerThread(QualType Type) const;

  /// Whether \p Call is of one of ThreadLocalFunctions given the thread,
  /// `fg_SystemThreadLocal(_ThreadID)` or `f_GetForThread(_ThreadID)`, which
  /// isn't the data of the thread that runs it.
  bool isExplicitThread(const CallExpr *Call) const;

  /// \p E past parentheses, cleanups, temporaries bound and the braces of a
  /// scalar, `int *p{&t_Value}`.
  const Expr *normalized(const Expr *E) const;

  /// The local, not a reference, that the local reference \p Reference is
  /// bound to, also through others, `int *&rpValue = pValue`, or \p
  /// Reference itself when it is a local that isn't one, also a parameter
  /// held by value; null otherwise.
  const VarDecl *boundLocal(const VarDecl *Reference) const;

  /// The local pointer, or local array of pointers, that the assignment to
  /// \p Target gives a value, also through a local reference to it or as an
  /// element, or null.
  const VarDecl *assignedLocal(const Expr *Target) const;

  /// Whether \p Record is one of ThreadLocalTypes: by its name, or by its
  /// qualified name (threadLocalTypeName) when the entry has `::`.
  bool isListedType(const CXXRecordDecl *Record) const;

  /// Whether \p Name is that of one of ScopeExitHelpers.
  bool isScopeExitHelper(StringRef Name) const {
    return llvm::is_contained(Lists.ScopeExitHelpers, Name);
  }

  /// Whether \p Type is one of ThreadLocalScopeTypes, not a handler.
  bool isScopeType(QualType Type) const;

  ASTContext &context() const { return Context; }
  llvm::ArrayRef<StringRef> functions() const { return Functions; }

private:
  /// Whether \p E is thread-local data by itself, which objectRelation tells
  /// what is made of: what isThreadLocalData tells; `this`, a parameter or a
  /// variable that isn't local, a call's result, or a member, of a reference
  /// or a pointer to a thread-local type, not of what names the thread
  /// (isExplicitThread); a pointer parameter its function points into it.
  bool isRoot(const Expr *E) const;
  /// Whether \p E is a call of a member function that gives what isn't the
  /// data of what it is called on: called on what a pointer the data holds
  /// points to, or whose body gives what a pointer points to, a copy or
  /// nothing of it (returnedRelation).
  bool givesOtherData(const Expr *E) const;
  /// Whether \p E, whose relation to the data objectRelation can't tell, is
  /// made of it as it is: a root (isRoot), or a part, an address, a value of a
  /// conditional, an assignment, a step or a move of one, also through what
  /// a local can hold there (localValuesAt); not what a call gives of it.
  bool madeOfData(const Expr *E, int Depth,
                  llvm::SmallPtrSetImpl<const VarDecl *> &Seen) const;
  /// What the assignments of its function that can run give \p Variable.
  const llvm::SmallVector<const Expr *, 2> &
  assignedTo(const VarDecl *Variable) const;

  ASTContext &Context;
  const CThreadLocalLists &Lists;
  llvm::ArrayRef<StringRef> Functions;
  /// Of each variable, whether it refers to the data.
  mutable llvm::DenseMap<const VarDecl *, bool> Variables;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_THREADLOCALUTILS_H
