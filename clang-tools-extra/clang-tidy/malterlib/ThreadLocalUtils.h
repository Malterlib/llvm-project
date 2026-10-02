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
#include "ValueFlow.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <tuple>

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
  /// ScopeExitHelpers: the variables whose `operator /`, and the functions
  /// whose call, makes a guard that runs the closure it is given where the
  /// guard ends, by name.
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

/// Whether \p Parameter, of an instantiation of a template, is of a type
/// that depends on the template's arguments, `t_CType *_pPtr`, so that what
/// it is given is thread-local data only as the instantiation is; also one of
/// a pack.
bool dependentInPattern(const ParmVarDecl *Parameter);

/// Whether \p Field, of an instantiation of a class template, is of a type
/// that depends on the template's arguments, `t_CType *mp_pPointTo`.
bool dependentInPattern(const FieldDecl *Field);

/// Whether `this` that the lambdas \p Lambda is in capture, `[this]`, is a
/// copy of the object one of them makes, `[*this]`.
bool objectCopiedAround(const LambdaExpr *Lambda);

/// Whether \p E gives the thread-local data of the thread that runs it: a
/// call of one of \p Functions given nothing but its object, also an
/// explicit object parameter, not one given the thread,
/// `fg_SystemThreadLocal(_ThreadID)`, or a variable declared `thread_local`,
/// also a static member named through an object, `this->ms_tData`.
bool isThreadLocalData(const ASTContext &Context, const Expr *E,
                       llvm::ArrayRef<StringRef> Functions);

/// What is thread-local data in a function, by the types and functions of the
/// options (CThreadLocalLists): what ThreadLocalFunctions give, `thread_local`
/// variables, what a pointer or a reference to a thread-local type refers to,
/// also `this` of a member function of one, and the local aliases, members and
/// elements of those. A call's result is only when its return type is a pointer
/// or a reference to a thread-local type, or it is one of ThreadLocalFunctions.
class CThreadLocalData {
public:
  CThreadLocalData(ASTContext &Context, const CThreadLocalLists &Lists)
      : Context(Context), Lists(Lists), Functions(Lists.Functions),
        Provenance(Context) {}

  /// Whether \p Type, also through a pointer or a reference to it when \p
  /// Through, is a thread-local type, one of ThreadLocalTypes.
  bool isType(QualType Type, bool Through = false) const;

  /// Whether the glvalue \p E is thread-local data, also a part or an entry
  /// of it, as objectRelation tells of the roots (isRoot): not what a pointer
  /// it holds points to, nor a copy.
  bool isData(const Expr *E) const;

  /// Whether what \p E is at \p Level (reach) is thread-local data: the
  /// storage, or what a pointer points to, at 0, what the value stored there
  /// points to at 1.
  bool isDataAt(const Expr *E, unsigned Level) const;

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
  bool isExplicitThread(const Expr *Call) const;

  /// The local, not a reference, that the local reference \p Reference is
  /// bound to, also through others, `int *&rpValue = pValue`, or \p
  /// Reference itself when it is a local that isn't one, also a parameter
  /// held by value; null otherwise.
  const VarDecl *boundLocal(const VarDecl *Reference) const;

  /// What a capture of a lambda is, as both thread-local checks take it
  /// (capturedBy).
  struct CCaptured {
    /// What it captures: a variable, of a structured binding the one it
    /// decomposes, or an init capture; null of `this`.
    const VarDecl *Variable = nullptr;
    const BindingDecl *Binding = nullptr;
    /// What it is named by in the closure, of a binding its own type.
    QualType Type;
    /// Whether the closure holds a copy, its own storage, not the variable.
    bool Copied = false;
    /// The local whose storage it designates: of a reference by reference,
    /// also an init capture, `[&rpValue = pValue]`, what it is bound to
    /// (boundLocal), else the variable.
    const VarDecl *Slot = nullptr;
    /// Whether it can refer to the data: a pointer or an array of them,
    /// also copied, or a reference, by reference or an init capture; not a
    /// copy of what a reference refers to otherwise, `[rValue]`.
    bool Refers = false;
    /// Whether it refers to the data where the closure is made, as \p
    /// Init, what it is made with, tells.
    bool Holds = false;
  };
  /// What \p Capture, made with \p Init, is (CCaptured).
  CCaptured capturedBy(const LambdaCapture &Capture, const Expr *Init) const;

  /// Whether \p Record is one of ThreadLocalTypes: by its name, or by its
  /// qualified name (threadLocalTypeName) when the entry has `::`.
  bool isListedType(const CXXRecordDecl *Record) const;

  /// Whether \p Name is that of one of ScopeExitHelpers.
  bool isScopeExitHelper(StringRef Name) const {
    return llvm::is_contained(Lists.ScopeExitHelpers, Name);
  }

  /// Whether \p Type is one of ThreadLocalScopeTypes, not a handler.
  bool isScopeType(QualType Type) const;

  /// Sets whether the parameters and the members of the object of a type that
  /// depends on the arguments of the template the code is an instantiation of
  /// (dependentInPattern) aren't roots: what the instantiation is given, which
  /// is reported where it is given.
  void setPatternExcluded(bool Excluded) const;

  /// Sets whether `this` is a copy of the object, of a lambda that captures
  /// `*this`, which isn't thread-local data: in \p In, and the lambdas in it
  /// that capture `this`, or, without it, wherever it is.
  void setObjectCopied(bool Copied, const DeclContext *In = nullptr) const {
    bObjectCopied = Copied;
    ObjectCopiedIn = In;
  }
  bool objectCopied() const { return bObjectCopied; }
  const DeclContext *objectCopiedIn() const { return ObjectCopiedIn; }

  ASTContext &context() const { return Context; }
  llvm::ArrayRef<StringRef> functions() const { return Functions; }

  /// Each of the lists it is told by (CThreadLocalLists), joined: what is
  /// kept of the AST that they tell is kept for them.
  const std::string &listsKey() const;

  /// The provenance of \p E (CProvenance::reach), which the checks, their
  /// exemptions and their summaries all ask, and its transitions (step).
  bool reach(const Expr *E, unsigned Level,
             llvm::function_ref<EReach(const Expr *At, unsigned Level,
                                       bool Part)>
                 Visit,
             bool *Ended = nullptr) const;

  void step(const Expr *At, unsigned AtLevel, bool Part,
            llvm::function_ref<void(const Expr *Next, unsigned Level,
                                    bool Part)>
                Next,
            bool &Complete) const;

  /// The walk reach and step are, of the closures a body shares
  /// (CReachClosures).
  const CProvenance &provenance() const { return Provenance; }

  /// What \p E is to thread-local data at \p Level (reach): a root, at
  /// level 0 what isThreadLocalData tells, `this` of a member function of a
  /// thread-local type, what a reference, or a call's result, of a
  /// reference or a pointer to a thread-local type is, also what a function
  /// whose body gives it gives (givesData); at level 1, the value of a
  /// pointer of a pointer to a thread-local type stored in what isn't a
  /// local. Not what a thread named gives (isExplicitThread), nor the value
  /// stored in thread-local storage, which points elsewhere.
  EReach rootAt(const Expr *E, unsigned Level) const;

  /// Whether \p E is thread-local data by itself, which objectRelation tells
  /// what is made of: what isThreadLocalData tells; `this`, a parameter or a
  /// variable that isn't local, a call's result, or a member, of a reference
  /// or a pointer to a thread-local type, not of what names the thread
  /// (isExplicitThread); a pointer parameter its function points into it.
  bool isRoot(const Expr *E) const;

  /// The lists it is told by.
  const CThreadLocalLists &lists() const { return Lists; }

  /// What givesData tells of \p Definition, worked out with this, whose
  /// settings are those of none (setObjectCopied, setPatternExcluded).
  bool givesDataUncached(const FunctionDecl *Definition) const;
  /// What isVariable tells of \p Variable, which can refer to the data
  /// where its function assigns it, worked out with this as it is set.
  bool isVariableUncached(const VarDecl *Variable) const;

private:
  /// Whether \p Function, whose body can be seen, gives back a reference or
  /// a pointer to thread-local data of its own, as its body tells, whatever
  /// it is called on.
  bool givesData(const FunctionDecl *Function) const;
  // Whether the call \p Call is a helper each of whose returns gives back,
  // on every alternative (reach), what it is given that is of a thread
  // named, as \p OfExplicitThread tells,
  // `fg_Const(*g_Data.f_GetForThread(_ThreadID))`.
  bool givesExplicitThread(
      const Expr *Call,
      llvm::function_ref<bool(const Expr *)> OfExplicitThread) const;
  /// Whether the call \p E gives its object or a part of it, as the body of
  /// what it calls tells (returnedRelation), not what is stored in it: what
  /// it gives is what it is called on tells, `Copy.f_Self()`, not its type.
  bool relatedToObject(const Expr *E) const;
  /// Whether the object of \p Function is a copy (setObjectCopied): where
  /// it is set, also through the lambdas in it that capture `this`, not the
  /// object of a function around it, `int &rValue = m_Value; [*this,
  /// &rValue] { ... }`.
  bool copiedIn(const DeclContext *Function) const;
  /// Whether \p This, `this`, is a copy of the object (copiedIn): of the
  /// innermost lambda whose body it is in, else of its function.
  bool thisCopied(const Expr *This) const;

  ASTContext &Context;
  const CThreadLocalLists &Lists;
  llvm::ArrayRef<StringRef> Functions;
  /// Whether the roots of a template's pattern aren't (setPatternExcluded).
  mutable bool bPatternExcluded = false;
  /// Whether `this` is a copy of the object, and where (setObjectCopied).
  mutable bool bObjectCopied = false;
  mutable const DeclContext *ObjectCopiedIn = nullptr;
  // The helpers whose returns are being told (givesExplicitThread).
  mutable llvm::DenseSet<const FunctionDecl *> DecidingHelpers;
  /// The uses of parameters whose values are being told (isRoot).
  mutable llvm::DenseSet<const Expr *> DecidingParameters;
  // The lists joined (listsKey), once asked.
  mutable std::string ListsKey;
  // The walk of provenance reach and step are (CProvenance).
  CProvenance Provenance;
  // The walks of isDataAt, shared while the settings they are made with
  // (setPatternExcluded, setObjectCopied) hold (CReachClosures); none is
  // shared inside one, nor while a parameter or a helper is decided, which
  // rootAt assumes of it.
  mutable std::unique_ptr<CReachClosures> DataWalks;
  mutable std::tuple<bool, bool, const DeclContext *> DataWalksSettings;
  mutable bool bDataWalking = false;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_THREADLOCALUTILS_H
