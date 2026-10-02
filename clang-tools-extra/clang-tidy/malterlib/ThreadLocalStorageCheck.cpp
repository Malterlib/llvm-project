//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ThreadLocalStorageCheck.h"
#include "CheckedAccess.h"
#include "ActorAnnotations.h"
#include "HeldData.h"
#include "ThreadLocalUtils.h"
#include "ValueFlow.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/ScopeExit.h"
#include <array>
#include <cctype>
#include <functional>
#include <optional>

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

namespace {

/// Where what is stored in a target stays after the function that stores it
/// ends, by what it is given (keptBy).
struct CKept {
  /// Whatever it is given: a global or a static, an object on the heap, or
  /// what can't be told.
  bool Always = false;
  /// A member of its object, also of a part of it.
  bool Object = false;
  /// What these parameters of it refer or point to, or a member of that.
  llvm::SmallVector<unsigned, 2> Parameters;
  bool any() const { return Always || Object || !Parameters.empty(); }
  bool operator==(const CKept &Other) const {
    return Always == Other.Always && Object == Other.Object &&
           Parameters == Other.Parameters;
  }
  void merge(const CKept &Other) {
    Always |= Other.Always;
    Object |= Other.Object;
    for (unsigned Parameter : Other.Parameters)
      if (!llvm::is_contained(Parameters, Parameter))
        Parameters.push_back(Parameter);
  }
};

/// What a function keeps of what it is given for a parameter
/// (storesParameter): where, and what of the argument, what it is at Level
/// (CThreadLocalData::reach), each destination with its own level, `int
/// *const &_pValue` of `g_pValue = _pValue; g_ppSlot = &_pValue;` kept at
/// level 1 in one and at level 0 in the other.
struct CStore {
  CKept Kept;
  unsigned Level = 0;
  bool operator==(const CStore &Other) const {
    return Kept == Other.Kept && Level == Other.Level;
  }
};
using CStores = llvm::SmallVector<CStore, 2>;

/// Of the parameter \p Parameter as what it refers or points to: its
/// object, of an explicit object parameter, else that parameter.
CKept parameterKept(const ParmVarDecl *Parameter) {
  CKept Kept;
  const auto *Method = dyn_cast<CXXMethodDecl>(Parameter->getDeclContext());
  if (Method && Method->isExplicitObjectMemberFunction() &&
      Parameter->getFunctionScopeIndex() == 0)
    Kept.Object = true;
  else
    Kept.Parameters.push_back(Parameter->getFunctionScopeIndex());
  return Kept;
}

/// The class of the object \p Member is a member of, through a pointer of
/// `->`, which a member of a base of it is a part of too.
const CXXRecordDecl *memberObjectClass(const MemberExpr *Member) {
  // Not the base a member of a base is reached through, `CBase` of the
  // derived object.
  QualType Of = Member->getBase()->IgnoreParenImpCasts()->getType();
  if (Member->isArrow())
    Of = Of->getPointeeType();
  return Of.isNull() ? nullptr : Of->getAsCXXRecordDecl();
}

// One alternative of where a store ends up (CWalk::destinationOf).
enum class EWhere {
  // Thread-local storage of the same thread, of a variable Variable.
  Thread,
  // The own storage of an object whose members can refer to the data
  // (isExempt), a member of it at any depth.
  Exempt,
  // A local, Variable, also an init capture; null when it can't be told.
  Local,
  // What nothing keeps past the expression: a temporary, a local value.
  Temporary,
  // A variable that isn't local, Variable: a global or a static.
  Static,
  // The member Field that a constructor initializes.
  Member,
  // A base that a constructor initializes.
  Base,
  // The object of the function, what `this` points to.
  Object,
  // What the parameter Index of the function refers or points to.
  Parameter,
  // An object on the heap, made by Object, a `new`.
  Heap,
  // What the function returns.
  Return,
  // The capture Index of the closure Lambda, made in In; of an assignment
  // to a copy a closure keeps (closureCopying), Variable, the copy.
  Capture,
  // What the root Root, an initializer checked as a whole, is stored in.
  Root,
  // Handed to what the walk doesn't follow: a parameter of a function that
  // isn't a move, a forward or a copy, an exception.
  Given,
  // What can't be told.
  Unknown,
};

// Where a store ends up, one alternative, and how it is reached there,
// which the policies decide by and the messages name.
struct CWhere {
  EWhere Kind = EWhere::Unknown;
  const VarDecl *Variable = nullptr;
  const FieldDecl *Field = nullptr;
  const LambdaExpr *Lambda = nullptr;
  const CEffectIndex *In = nullptr;
  const Stmt *Root = nullptr;
  // Of Heap the `new`, of what is assigned the object assigned.
  const Expr *Object = nullptr;
  unsigned Index = 0;
  // Of what a callee keeps, the level of the argument it keeps (CStore).
  unsigned Level = 0;
  // The innermost member of an aggregate it is reached through.
  const FieldDecl *Part = nullptr;
  // Whether it is reached as it is or as an element, not as a member or a base.
  bool Direct = true;
  // Of a designated local, whether the storage is a part of a local object.
  bool LocalPart = false;
  // Whether it is reached through a member of a class whose members can refer to the data.
  bool Exempt = false;
  // Whether it is the storage of what an assignment operator assigns, Object.
  bool Assigned = false;
  // Whether it is where what a constructor makes ends up, of what a callee keeps.
  bool Made = false;
};
using CWheres = llvm::SmallVector<CWhere, 2>;

// What a walk follows (CWalk::into).
enum class EFollow {
  // The value stored, a pointer or a reference, and what a callee keeps of it.
  Store,
  // An object made: where it ends up, through moves, copies and temporaries.
  Made,
  // A local holder: also what is reached through it, by its type.
  Derived,
};

// The members whose default initializers a default constructor of a class
// the compiler defines runs, of each class: its own, and those of its bases
// and members made by one the compiler defines too, each with whether a class
// on the way to it, the class itself included, is exempt (IsExempt), whose own
// storage it then is. Each class once, kept when finished (Of).
using CDefaults = llvm::SmallVector<std::pair<const FieldDecl *, bool>, 4>;
struct CImplicitDefaults {
  explicit CImplicitDefaults(
      llvm::function_ref<bool(const CXXRecordDecl *)> IsExempt)
      : IsExempt(IsExempt) {}

  llvm::function_ref<bool(const CXXRecordDecl *)> IsExempt;
  llvm::DenseMap<const CXXRecordDecl *, std::unique_ptr<CDefaults>> Of;
  llvm::DenseSet<const CXXRecordDecl *> Collecting;

  // Of \p Class.
  llvm::ArrayRef<std::pair<const FieldDecl *, bool>>
  of(const CXXRecordDecl *Class) {
    if (stackNearlyExhausted())
      return withStack([&] { return of(Class); });
    if (!Class || !Class->hasDefinition())
      return {};
    Class = Class->getDefinition();
    if (const auto It = Of.find(Class); It != Of.end())
      return *It->second;
    // A class can't hold itself by value; asked again while it is collected,
    // none.
    if (!Collecting.insert(Class).second)
      return {};
    auto Made = std::make_unique<CDefaults>();
    const bool Exempt = IsExempt(Class);
    llvm::DenseSet<std::pair<const FieldDecl *, unsigned>> Seen;
    auto Add = [&](const FieldDecl *Field, bool FieldExempt) {
      if (Seen.insert({Field, FieldExempt ? 1u : 0u}).second)
        Made->push_back({Field, FieldExempt});
    };
    // Whether what makes \p Record by default is one the compiler defines.
    auto Implicit = [](const CXXRecordDecl *Record) {
      return Record && Record->hasDefinition() &&
             llvm::none_of(Record->getDefinition()->ctors(),
                           [](const CXXConstructorDecl *Constructor) {
                             return Constructor->isDefaultConstructor() &&
                                    Constructor->isUserProvided();
                           });
    };
    auto Inner = [&](const CXXRecordDecl *Record) {
      if (Implicit(Record))
        for (const auto &[Field, FieldExempt] : of(Record))
          Add(Field, FieldExempt || Exempt);
    };
    for (const CXXBaseSpecifier &Base : Class->bases())
      Inner(Base.getType()->getAsCXXRecordDecl());
    for (const FieldDecl *Field : Class->fields()) {
      if (Field->hasInClassInitializer()) {
        Add(Field, Exempt);
        continue;
      }
      QualType Type = Field->getType();
      while (const ArrayType *Array = Type->getAsArrayTypeUnsafe())
        Type = Array->getElementType();
      Inner(Type->getAsCXXRecordDecl());
    }
    Collecting.erase(Class);
    return *Of.try_emplace(Class, std::move(Made)).first->second;
  }
};

// The first parameter of \p Callee that isn't its object.
unsigned firstGiven(const FunctionDecl *Callee) {
  const auto *Method = dyn_cast<CXXMethodDecl>(Callee);
  return Method && Method->isExplicitObjectMemberFunction() ? 1 : 0;
}

// The statements of \p Body, of \p Owner, that can run (CFlowCache::running).
const llvm::DenseSet<const Stmt *> *runningIn(const Stmt *Body,
                                              const Decl *Owner,
                                              ASTContext &Context) {
  return sharedFlowCache().running(Owner, Body, Context);
}

// Whether \p S can run, as \p Running tells, of each when that is null.
bool runs(const llvm::DenseSet<const Stmt *> *Running, const Stmt *S) {
  return !Running || Running->contains(S);
}

// Where \p Function, as defined, stores what its parameter \p iParameter is,
// or what it refers or points into, so that it stays after the function ends
// (CWalk): assigned, given to a member, a static, an object on the heap or
// what a parameter refers to, or given to a function that does so, as what
// that is given, `m_Pointer.f_Set(_pValue)`; only what can run.
CStores storesParameter(const FunctionDecl *Function, unsigned iParameter,
                        const CThreadLocalData &Data);

// The one walk of where what is stored ends up (destinationOf), with the
// roots it is given: of a check, the initializers checked as a whole, of a
// summary, the member initializers of a constructor, by what they
// initialize. Each answer is every alternative, of the effect table
// (effectsOf), provenance (CThreadLocalData::reach) and the summaries of
// callees (storesParameter).
class CWalk {
public:
  CWalk(const CThreadLocalData &Data,
        const llvm::DenseMap<const Stmt *, CWhere> &Roots)
      : Data(Data), Roots(Roots) {}

  // Where the value \p Effect stores, an operand of \p Parent in \p Index,
  // ends up.
  void destinationOf(const COperandEffect &Effect, const Stmt *Parent,
                     const CEffectIndex &Index, CWheres &Out) const {
    into(Effect.Into, Parent, Index, EFollow::Store, Out);
  }

  // What a walk has gone through, each with how it was followed, which it
  // doesn't again.
  using CSeen = llvm::DenseSet<std::pair<const Stmt *, unsigned>>;
  void into(const CDestination &Into, const Stmt *Parent,
            const CEffectIndex &Index, EFollow Follow, CWheres &Out,
            CSeen *Seen = nullptr) const;
  void endsUp(const Stmt *Made, const CEffectIndex &Index, EFollow Follow,
              CWheres &Out, CSeen *Seen = nullptr) const;
  void designated(const Expr *Target, CWheres &Out) const;
  CKept keptOf(const CWhere &Where) const;

private:
  CWhere variable(const VarDecl *Variable) const;
  void calleeKeeps(const CDestination &Into, const Stmt *Parent,
                   const CEffectIndex &Index, CWheres &Out) const;

  const CThreadLocalData &Data;
  const llvm::DenseMap<const Stmt *, CWhere> &Roots;
};

CWhere CWalk::variable(const VarDecl *Variable) const {
  CWhere Where;
  Where.Variable = Variable;
  if (Variable->getTLSKind() != VarDecl::TLS_None)
    Where.Kind = EWhere::Thread;
  else if (Variable->hasLocalStorage() || Variable->isInitCapture())
    Where.Kind = EWhere::Local;
  else
    Where.Kind = EWhere::Static;
  return Where;
}

// Where what is given to \p Into, of \p Parent in \p Index, ends up, as
// \p Follow follows it: a variable by what it is (variable); a member, an
// element or a base by where what it is a part of ends up, the innermost
// member it is in; the storage an assignment designates (designated); of
// a value stored, what a callee keeps of it, by its summary
// (storesParameter), in what its object or another argument designates or
// in what a constructor makes; of an object made, what a move or a forward
// gives back (givesItsArgument), a copy made of it, what an assignment
// operator assigns, the temporary it is made in, by the reference that
// extends it; a parameter of a coroutine held by value, of what is stored
// and of an object made, in its frame.
void CWalk::into(const CDestination &Into, const Stmt *Parent,
                 const CEffectIndex &Index, EFollow Follow, CWheres &Out,
                 CSeen *Seen) const {
  CSeen Own;
  if (!Seen)
    Seen = &Own;
  ASTContext &Context = Data.context();
  const bool Made = Follow != EFollow::Store;
  // What it is a part of, an object made, is followed as one.
  const EFollow Whole = Made ? Follow : EFollow::Made;
  auto Push = [&](EWhere Kind) {
    CWhere Where;
    Where.Kind = Kind;
    Out.push_back(Where);
    return &Out.back();
  };
  switch (Into.Kind) {
  case EDestination::Variable:
    if (const auto *Variable = dyn_cast_or_null<VarDecl>(Into.Declaration))
      Out.push_back(variable(Variable));
    else if (Made)
      Push(EWhere::Local);
    return;
  // Where what it is a part of ends up, through the innermost member, not as
  // it is; of a constructor's own initializer, the member or the base.
  case EDestination::Field:
  case EDestination::Base: {
    // The own storage of an object whose members can refer to the data: a
    // member of it, also of a base of it, by the class of what is made.
    const auto *Field = dyn_cast_or_null<FieldDecl>(Into.Declaration);
    const bool Exempt =
        Field ? Data.isExempt(Field->getParent())
              : Into.Object &&
                    Data.isExempt(Into.Object->getType()->getAsRecordDecl());
    if (!Into.Object) {
      CWhere *Where = Push(Into.Kind == EDestination::Field ? EWhere::Member
                                                            : EWhere::Base);
      Where->Field = Field;
      Where->Exempt = Exempt;
      return;
    }
    const size_t First = Out.size();
    endsUp(Into.Object, Index, Whole, Out, Seen);
    for (size_t iWhere = First; iWhere < Out.size(); ++iWhere) {
      Out[iWhere].Direct = false;
      Out[iWhere].Exempt |= Exempt;
      if (Into.Kind == EDestination::Field)
        Out[iWhere].Part = Field;
    }
    return;
  }
  case EDestination::Element:
    if (Into.Object)
      endsUp(Into.Object, Index, Whole, Out, Seen);
    else
      Push(EWhere::Given);
    return;
  case EDestination::Heap:
    Push(EWhere::Heap)->Object = Into.Object;
    return;
  case EDestination::Return:
    Push(EWhere::Return);
    return;
  case EDestination::Capture: {
    CWhere *Where = Push(EWhere::Capture);
    Where->Lambda = dyn_cast_or_null<LambdaExpr>(Into.Object);
    Where->Index = Into.Index;
    Where->In = &Index;
    return;
  }
  // The storage assigned; an object made isn't assigned as a value, only by
  // an assignment operator.
  case EDestination::Designated: {
    if (Made) {
      Push(EWhere::Given);
      return;
    }
    // The copy a closure keeps, `[pCounter = ...]() mutable { pCounter =
    // &t_Value; }` (closureCopying), which it can be called with again.
    if (const ValueDecl *Named = namedDecl(Into.Object, Context))
      if (const auto *Copy = dyn_cast<VarDecl>(Named);
          Copy && !Copy->getType()->isReferenceType())
        if (const LambdaExpr *Closure =
                closureCopying(Copy, sameValue(Into.Object, Context))) {
          CWhere *Where = Push(EWhere::Capture);
          Where->Lambda = Closure;
          Where->Variable = Copy;
          return;
        }
    designated(Into.Object, Out);
    return;
  }
  case EDestination::Parameter: {
    // A parameter of a coroutine, which its frame keeps across its
    // suspensions: held by value a copy, of a pointer what it points to, of
    // a reference what it is bound to. Not of one that can't suspend
    // (callsNonSuspendingCoroutine), whose frame is gone when the call
    // returns.
    const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Into.Declaration);
    const bool Coroutine =
        Parameter && Into.Callee && isCoroutine(Into.Callee) &&
        !callsNonSuspendingCoroutine(dyn_cast_or_null<CallExpr>(Parent));
    const bool InFrame =
        Coroutine && !Parameter->getType()->isReferenceType();
    if (!Made) {
      if (Coroutine)
        Push(EWhere::Heap);
      calleeKeeps(Into, Parent, Index, Out);
      return;
    }
    const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Into.Callee);
    // What a member function with an explicit object parameter is called on,
    // its own use.
    if (Method && Method->isExplicitObjectMemberFunction() && Into.Index == 0)
      return;
    // What an assignment operator assigns, of a class, by its storage.
    if (Method && Method->getOverloadedOperator() == OO_Equal) {
      const auto *Assignment = dyn_cast_or_null<Expr>(Parent);
      const Expr *Object =
          Assignment ? callObject(Assignment, Context) : nullptr;
      if (!Object || Follow == EFollow::Derived) {
        Push(EWhere::Given);
        return;
      }
      const size_t First = Out.size();
      designated(Object, Out);
      for (size_t iWhere = First; iWhere < Out.size(); ++iWhere) {
        Out[iWhere].Assigned = true;
        Out[iWhere].Object = Object;
      }
      return;
    }
    // What a move or a forward gives back of it ends up where that does,
    // `g_View = fg_Move(CView{&t_Value})` (givesItsArgument).
    if (Parent && Into.Index == 0 &&
        effectsOf(Parent, Context).Statement == EStatement::Call &&
        givesItsArgument(cast<CallExpr>(Parent))) {
      endsUp(Parent, Index, Whole, Out, Seen);
      return;
    }
    // What a copy or a move made of it ends up in, `CView(View{&t_Value})`.
    if (const auto *Constructor =
            dyn_cast_or_null<CXXConstructorDecl>(Into.Callee);
        Constructor && Constructor->isCopyOrMoveConstructor() && Parent &&
        isa<Expr>(Parent)) {
      endsUp(Parent, Index, Whole, Out, Seen);
      return;
    }
    if (InFrame) {
      Push(EWhere::Heap);
      return;
    }
    Push(EWhere::Given);
    return;
  }
  // A temporary made of an object: by the declaration that extends it, else
  // by where it ends up; the array of a list, `std::initializer_list<int *>`,
  // where that ends up.
  case EDestination::Temporary: {
    if (!Made)
      return;
    if (!Into.Object) {
      Push(EWhere::Given);
      return;
    }
    switch (effectsOf(Into.Object, Context).Statement) {
    case EStatement::Temporary:
      if (const auto *Extending = dyn_cast_or_null<VarDecl>(
              cast<MaterializeTemporaryExpr>(Into.Object)->getExtendingDecl()))
        Out.push_back(variable(Extending));
      else
        endsUp(Into.Object, Index, Whole, Out, Seen);
      return;
    case EStatement::InitializerList:
      endsUp(Into.Object, Index, Whole, Out, Seen);
      return;
    case EStatement::Other:
    case EStatement::Name:
    case EStatement::This:
    case EStatement::Member:
    case EStatement::Call:
    case EStatement::Construction:
    case EStatement::Lambda:
    case EStatement::Declaration:
    case EStatement::RangeLoop:
    case EStatement::CoroutineBody:
    case EStatement::New:
    case EStatement::Return:
    case EStatement::Coreturn:
    case EStatement::Default:
    case EStatement::InheritedConstruction:
      if (Follow == EFollow::Derived)
        endsUp(Into.Object, Index, Whole, Out, Seen);
      return;
    }
    return;
  }
  case EDestination::None:
  case EDestination::Thrown:
    if (Made)
      Push(EWhere::Given);
    return;
  }
}

// Where the value of \p Made, of \p Index, ends up, by what is done with
// it (consumersOf): a root it is, past what only gives it on, else what
// each use that binds or copies it gives it to (into); of a holder, also
// what is reached through it, by its type (holdsReference), and what
// can't be told of it, each once by \p Seen.
void CWalk::endsUp(const Stmt *Made, const CEffectIndex &Index, EFollow Follow,
                   CWheres &Out, CSeen *Seen) const {
  if (stackNearlyExhausted())
    return withStack([&] { endsUp(Made, Index, Follow, Out, Seen); });
  CSeen Own;
  if (!Seen)
    Seen = &Own;
  if (!Made || !Seen->insert({Made, static_cast<unsigned>(Follow)}).second)
    return;
  // A root, also past what only gives it on, its cleanups.
  for (const Stmt *Up = Made; Up;) {
    if (const auto It = Roots.find(Up); It != Roots.end()) {
      Out.push_back(It->second);
      return;
    }
    const llvm::ArrayRef<CEffectIndex::CUse> Uses = Index.usesOf(Up);
    Up = Uses.size() == 1 && Uses.front().Effect->Use == EUse::Forward
             ? Uses.front().Parent
             : nullptr;
  }
  for (const CEffectIndex::CUse &Use : Index.consumersOf(Made)) {
    switch (Use.Effect->Use) {
    // Of a holder, not a part of it that can't hold a reference or a pointer
    // (holdsReference), a value of its own, which carries nothing it
    // borrows, `return Report.m_Stats;`, nor a copy of such a value, also of
    // what a getter gives a reference to, `return Profiler.f_GetStatistics();`
    // of `CStatistics const &f_GetStatistics()`.
    case EUse::Copy:
    case EUse::Bind:
      if (const auto *Operand = dyn_cast<Expr>(Use.Effect->Operand);
          Follow == EFollow::Derived && Operand &&
          (Operand != Made || Use.Effect->Use == EUse::Copy) &&
          !holdsReference(Operand->getType().getNonReferenceType()))
        break;
      into(Use.Effect->Into, Use.Parent, Index, Follow, Out, Seen);
      break;
    // Of a holder, what is reached through it, its address, what is loaded
    // of it and what a member function gives of it.
    // A load only of what the holder holds itself, a local slot it
    // designates (designatedSlots), not of what it refers to, where what is
    // stored points to what is apart from it,
    // `Reference.f_Get().m_pHolder`.
    case EUse::Dereference:
    case EUse::Access:
    case EUse::Update:
    case EUse::AddressOf:
    case EUse::CallObject:
      if (Follow != EFollow::Derived)
        break;
      if ((Use.Effect->Use == EUse::Access || Use.Effect->Use == EUse::Update) &&
          designatedSlots(dyn_cast<Expr>(Use.Effect->Operand), Data.context())
              .empty())
        break;
      if (const auto *Parent = dyn_cast<Expr>(Use.Parent);
          Parent && (Parent->isGLValue() || holdsReference(Parent->getType())))
        endsUp(Parent, Index, Follow, Out, Seen);
      break;
    case EUse::Opaque:
      if (Follow == EFollow::Derived) {
        CWhere Where;
        Where.Kind = EWhere::Given;
        Out.push_back(Where);
      }
      break;
    case EUse::Forward:
    case EUse::Write:
    case EUse::Condition:
    case EUse::Value:
    case EUse::Discarded:
    case EUse::Unevaluated:
    case EUse::Statement:
    case EUse::Callee:
      break;
    }
  }
}

// The storage \p Target designates, or, of a pointer, points to, by its
// provenance (CThreadLocalData::reach): a member of the object, `this`,
// what a parameter refers or points to, a global or a static, which keep
// it, or a local, which doesn't, nor thread-local storage itself, of the
// same thread, nor the own storage of an object whose members can refer to
// the data (isExempt), a member of it at any depth, not what a pointer it
// holds points to. What can't be told keeps it, unless it is a temporary.
// Exempt only as a whole, when each alternative is.
void CWalk::designated(const Expr *Target, CWheres &Out) const {
  if (!Target)
    return;
  ASTContext &Context = Data.context();
  CKept Kept;
  llvm::SmallVector<const VarDecl *, 2> Locals;
  bool LocalPart = false;
  bool Reached = false;
  // Exempt only when each alternative is: thread-local storage or the own
  // storage of an exempt object.
  bool ExemptReached = false;
  bool Other = false;
  bool Ended = false;
  const bool Complete =
      Data.reach(Target, 0, [&](const Expr *At, unsigned Level, bool Part) {
        const EStatement Kind = effectsOf(At, Context).Statement;
        // Thread-local storage, of the same thread; what a value stored in
        // it points to can be anywhere.
        if (isThreadLocalData(Context, At, Data.functions())) {
          Reached = true;
          Kept.Always |= Level > 0;
          (Level > 0 ? Other : ExemptReached) = true;
          return EReach::Root;
        }
        // The own storage of an object whose members can refer to the data,
        // not what a reference member of it refers to, which is a load
        // (reach).
        // Also a member of a base of one, `_Scope.m_pBaseValue`, by the
        // class of what it is a member of.
        if (Kind == EStatement::Member && Level == 0)
          if (const auto *Field =
                  dyn_cast_or_null<FieldDecl>(memberDecl(At, Context));
              Field && !Field->getType()->isReferenceType() &&
              (Data.isExempt(Field->getParent()) ||
               Data.isExempt(memberObjectClass(cast<MemberExpr>(At))))) {
            Reached = true;
            ExemptReached = true;
            return EReach::Root;
          }
        // A temporary, which ends with its expression, also an object made
        // as a value; one a reference extends, as the variable that does,
        // `auto &&View = CView{}`.
        if (Kind == EStatement::Temporary)
          if (const auto *Extending = dyn_cast_or_null<VarDecl>(
                  cast<MaterializeTemporaryExpr>(At)->getExtendingDecl());
              Extending && Level == 0) {
            Reached = true;
            if (Extending->getTLSKind() != VarDecl::TLS_None)
              ExemptReached = true;
            else if (Extending->hasLocalStorage()) {
              if (!llvm::is_contained(Locals, Extending))
                Locals.push_back(Extending);
              LocalPart |= Part;
              Other = true;
            } else {
              Kept.Always = true;
              Other = true;
            }
            return EReach::Root;
          }
        // What a value stored in one points to can be anywhere, also an
        // alternative of an exempt one, `*CRef{&g_Holder}`.
        if (Kind == EStatement::Temporary ||
            (At->isPRValue() && At->getType()->isRecordType())) {
          Reached = true;
          Kept.Always |= Level > 0;
          Other |= Level > 0;
          return EReach::Root;
        }
        if (Kind == EStatement::This) {
          Reached = true;
          if (Level > 0) {
            Kept.Always = true;
            Other = true;
          } else if (Data.isExempt(At->getType()->getPointeeCXXRecordDecl())) {
            ExemptReached = true;
          } else {
            Kept.Object = true;
            Other = true;
          }
          return EReach::Root;
        }
        const auto *Variable =
            Kind == EStatement::Name
                ? dyn_cast<VarDecl>(cast<DeclRefExpr>(At)->getDecl())
                : nullptr;
        if (!Variable)
          return EReach::Continue;
        // What a parameter refers to, or what its value points to; not
        // itself, held by value.
        // Of a pointer held by value that its function assigns where it is
        // read, what it is assigned (localValuesAt), and itself only when
        // what it is given can reach there too.
        if (const auto *Parameter = dyn_cast<ParmVarDecl>(Variable)) {
          bool Given = true;
          if (!Parameter->getType()->isReferenceType() && Level > 0)
            if (!assignedAt(Parameter, At, Context, &Given).empty()) {
              if (Given)
                Kept.merge(parameterKept(Parameter));
              return EReach::Continue;
            }
          Reached = true;
          Other = true;
          if (Parameter->getType()->isReferenceType() || Level > 0)
            Kept.merge(parameterKept(Parameter));
          return EReach::Root;
        }
        // A local, by what is stored in it above level 0, also a local
        // reference by what it is bound to; the own storage of a local
        // object whose members can refer to the data, a part of it, which a
        // pointer is stored in, also reached through an alias of a base.
        if (Variable->hasLocalStorage() || Variable->isInitCapture()) {
          if (Variable->getType()->isReferenceType() || Level > 0)
            return EReach::Continue;
          if (Data.isExempt(Variable->getType()->getAsRecordDecl())) {
            Reached = true;
            ExemptReached = true;
            return EReach::Root;
          }
          Reached = true;
          Other = true;
          if (!llvm::is_contained(Locals, Variable))
            Locals.push_back(Variable);
          // A part of a local object, also of an element of a local array
          // of them, not an element of an array of pointers.
          LocalPart |=
              Part && heldElement(Variable->getType())->isRecordType();
          return EReach::Root;
        }
        Reached = true;
        Other = true;
        Kept.Always = true;
        return EReach::Root;
      }, &Ended);
  const bool Exempt = ExemptReached && !Other && !Ended && Complete;
  // What can't be told keeps it, also a way that ends where it can't be told
  // alongside others, `(bChoose ? Local : fg_External()).f_Set(...)`.
  if (!Reached || !Complete || Ended)
    Kept.Always |= Target->isGLValue() || Target->getType()->isPointerType();
  auto Push = [&](EWhere Kind) {
    CWhere Where;
    Where.Kind = Kind;
    Out.push_back(Where);
    return &Out.back();
  };
  if (Exempt) {
    Push(EWhere::Exempt);
    return;
  }
  const size_t First = Out.size();
  if (Kept.Always)
    Push(EWhere::Unknown);
  if (Kept.Object)
    Push(EWhere::Object);
  for (unsigned Parameter : Kept.Parameters)
    Push(EWhere::Parameter)->Index = Parameter;
  for (const VarDecl *Local : Locals) {
    CWhere *Where = Push(EWhere::Local);
    Where->Variable = Local;
    Where->LocalPart = LocalPart;
  }
  if (ExemptReached)
    Push(EWhere::Thread);
  if (Out.size() == First)
    Push(EWhere::Temporary);
}

// What the callee of \p Into keeps of what \p Parent, a call or a
// construction, gives it (storesParameter), each at its level.
void CWalk::calleeKeeps(const CDestination &Into, const Stmt *Parent,
                        const CEffectIndex &Index, CWheres &Out) const {
  if (!Into.Callee || !Parent)
    return;
  ASTContext &Context = Data.context();
  const EStatement Kind = effectsOf(Parent, Context).Statement;
  // Where what is made ends up, once for each store that keeps it there.
  std::optional<CWheres> Made;
  for (const CStore &Stored : storesParameter(Into.Callee, Into.Index, Data)) {
    const size_t First = Out.size();
    if (Stored.Kept.Always) {
      CWhere Where;
      Where.Kind = EWhere::Unknown;
      Out.push_back(Where);
    }
    // What it keeps in its object: of a construction, what is made, by where
    // it ends up; of a call, the object it is called on (EUse::CallObject).
    if (Stored.Kept.Object) {
      switch (Kind) {
      case EStatement::Construction:
        if (!Made) {
          Made.emplace();
          endsUp(Parent, Index, EFollow::Made, *Made);
          for (CWhere &Where : *Made)
            Where.Made = true;
        }
        Out.append(Made->begin(), Made->end());
        break;
      case EStatement::Call:
        designated(callObject(cast<Expr>(Parent), Context), Out);
        break;
      case EStatement::Other:
      case EStatement::Name:
      case EStatement::This:
      case EStatement::Member:
      case EStatement::Lambda:
      case EStatement::Declaration:
      case EStatement::RangeLoop:
      case EStatement::CoroutineBody:
      case EStatement::Temporary:
      case EStatement::New:
      case EStatement::Return:
      case EStatement::Coreturn:
      case EStatement::Default:
      case EStatement::InheritedConstruction:
      case EStatement::InitializerList:
        break;
      }
    }
    for (unsigned Given : Stored.Kept.Parameters)
      designated(argumentFor(Parent, Given, Context), Out);
    for (size_t iWhere = First; iWhere < Out.size(); ++iWhere)
      Out[iWhere].Level = Stored.Level;
  }
}

// What \p Where keeps of what is stored there, of a summary: a global, a
// static, the heap and what can't be told always, a member or a base of
// the object, what a parameter refers to, where an object is placed, where
// a closure that captures it ends up.
CKept CWalk::keptOf(const CWhere &Where) const {
  if (stackNearlyExhausted())
    return withStack([&] { return keptOf(Where); });
  CKept Kept;
  if (Where.Exempt)
    return Kept;
  switch (Where.Kind) {
  case EWhere::Static:
  case EWhere::Unknown:
    Kept.Always = true;
    break;
  case EWhere::Member:
  case EWhere::Base:
  case EWhere::Object:
    Kept.Object = true;
    break;
  case EWhere::Parameter:
    Kept.Parameters.push_back(Where.Index);
    break;
  // Where it is placed, `new (pSpace) CType(...)`, else on the heap.
  case EWhere::Heap:
    if (const auto *New = dyn_cast_or_null<CXXNewExpr>(Where.Object);
        New && New->getNumPlacementArgs() > 0) {
      CWheres Placed;
      designated(New->getPlacementArg(0), Placed);
      for (const CWhere &Each : Placed)
        Kept.merge(keptOf(Each));
    } else
      Kept.Always = true;
    break;
  // A member of the closure, by where the closure ends up.
  case EWhere::Capture:
    if (Where.Lambda && Where.In) {
      CWheres Closure;
      endsUp(Where.Lambda, *Where.In, EFollow::Made, Closure);
      for (const CWhere &Each : Closure)
        Kept.merge(keptOf(Each));
    }
    break;
  case EWhere::Thread:
  case EWhere::Exempt:
  case EWhere::Local:
  case EWhere::Temporary:
  case EWhere::Return:
  case EWhere::Root:
  case EWhere::Given:
    break;
  }
  return Kept;
}

// The destination type of \p Effect, of its operand \p Value: what it is
// given to, a variable, a parameter, a member or what is assigned, else
// what is bound to it, a reference, or its own type.
QualType givenType(const COperandEffect &Effect, const Expr *Value,
                   ASTContext &Context) {
  const CDestination &Into = Effect.Into;
  switch (Into.Kind) {
  case EDestination::Variable:
  case EDestination::Parameter:
  case EDestination::Field:
    if (const auto *Declared = dyn_cast_or_null<ValueDecl>(Into.Declaration);
        Declared && Into.Kind != EDestination::Parameter)
      return Declared->getType();
    if (const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Into.Declaration))
      return Parameter->getType();
    break;
  case EDestination::Designated:
    if (Into.Object)
      return Into.Object->getType();
    break;
  case EDestination::None:
  case EDestination::Capture:
  case EDestination::Element:
  case EDestination::Base:
  case EDestination::Return:
  case EDestination::Heap:
  case EDestination::Temporary:
  case EDestination::Thrown:
    break;
  }
  return Effect.Use == EUse::Bind
             ? Context.getLValueReferenceType(Value->getType())
             : Value->getType();
}

// Whether a value of \p Type is a pointer or a reference, which can refer to
// the data as it is; an array, an aggregate or an object refers to it by its
// parts.
bool refersAsIs(QualType Type) {
  return !Type.isNull() &&
         (Type->isReferenceType() || Type->isAnyPointerType());
}

CStores storesParameterUncached(const FunctionDecl *Definition,
                                unsigned iParameter,
                                const CThreadLocalData &Data) {
  ASTContext &Context = Definition->getASTContext();
  const ParmVarDecl *Parameter = Definition->getParamDecl(iParameter);
  const bool Reference = Parameter->getType()->isReferenceType();
  // The levels of the argument what \p E is at \p Start is made of (reach),
  // not what a call gives of it, which that callee's own summary tells: of a
  // reference, at the level it reaches the parameter; of a value, a level
  // below, not its own storage at 0; of a value assigned where it is read,
  // what it is assigned, and what it is given when that can reach there too.
  auto From = [&](const Expr *E, unsigned Start) {
    llvm::SmallVector<unsigned, 2> Levels;
    Data.reach(
        E, Start,
        [&](const Expr *At, unsigned Level, bool) {
          if (namedDecl(At, Context) != Parameter ||
              sameValue(At, Context) != At)
            return EReach::Continue;
          bool Given = true;
          bool Assigned = false;
          if (!Reference && Level > 0) {
            Assigned = !assignedAt(Parameter, At, Context, &Given).empty();
          }
          const unsigned ArgumentLevel = Reference ? Level : Level - 1;
          if (Given && (Reference || Level > 0) &&
              !llvm::is_contained(Levels, ArgumentLevel))
            Levels.push_back(ArgumentLevel);
          return Assigned ? EReach::Continue : EReach::Root;
        });
    return Levels;
  };
  CStores Stores;
  // What \p Partial keeps, of what is made of the parameter at \p Levels,
  // each a store of its own.
  auto Keep = [&](const CKept &Partial, llvm::ArrayRef<unsigned> Levels) {
    if (!Partial.any())
      return;
    for (unsigned Level : Levels)
      Stores.push_back({Partial, Level});
  };
  CFlowCache &Cache = sharedFlowCache();
  // The body, and the member initializers of a constructor, each a root of
  // its own, which ends up in the member or base it initializes.
  llvm::SmallVector<const Stmt *, 4> Bodies{Definition->getBody()};
  llvm::DenseMap<const Stmt *, CWhere> Roots;
  if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Definition))
    for (const CXXCtorInitializer *Initializer : Constructor->inits()) {
      const Expr *Init = Initializer->getInit();
      if (!Init)
        continue;
      CWhere Where;
      if (const FieldDecl *Field = Initializer->getMember()) {
        Where.Kind = EWhere::Member;
        Where.Field = Field;
        Where.Exempt = Data.isExempt(Field->getParent());
      } else
        Where.Kind = EWhere::Base;
      Roots[Init] = Where;
      Bodies.push_back(Init);
    }
  const CWalk Walk(Data, Roots);
  const llvm::DenseSet<const Stmt *> *Running =
      runningIn(Definition->getBody(), Definition, Context);
  // Each store, all of them, of each level, not only until one is kept
  // anywhere.
  for (const Stmt *Body : Bodies) {
    const CEffectIndex &Index = Cache.indexOf(Body, Context);
    const bool Initializer = Body != Definition->getBody();
    // The constructor a derived class inherits, given its parameters as they
    // are: what that keeps of them.
    if (Index.effectsOf(Body)->Statement == EStatement::InheritedConstruction)
      llvm::append_range(
          Stores, storesParameter(
                      cast<CXXInheritedCtorInitExpr>(Body)->getConstructor(),
                      iParameter, Data));
    // A member initializer given the parameter as it is.
    if (const auto *Init = dyn_cast<Expr>(Body); Initializer && Init) {
      const CWhere Root = Roots.lookup(Init);
      if (refersAsIs(Root.Field ? Root.Field->getType() : Init->getType()) &&
          runs(Running, Init))
        Keep(Walk.keptOf(Root), From(Init, 0));
    }
    // Only what can run, also in the initializers, which the CFG of the
    // constructor has.
    for (const Stmt *S : Index.statements()) {
      if (!runs(Running, S))
        continue;
      for (const COperandEffect &Effect : Index.effectsOf(S)->Operands) {
        if (Effect.Use != EUse::Copy && Effect.Use != EUse::Bind)
          continue;
        const auto *Value = dyn_cast<Expr>(Effect.Operand);
        if (!Value || Value->isTypeDependent() ||
            !refersAsIs(givenType(Effect, Value, Context)))
          continue;
        // What each alternative keeps, of the levels a callee keeps the
        // argument at, each once.
        CWheres Wheres;
        Walk.destinationOf(Effect, S, Index, Wheres);
        llvm::SmallVector<std::pair<unsigned, CKept>, 2> ByLevel;
        for (const CWhere &Where : Wheres) {
          auto It = llvm::find_if(ByLevel, [&](const auto &Each) {
            return Each.first == Where.Level;
          });
          if (It == ByLevel.end()) {
            ByLevel.push_back({Where.Level, CKept()});
            It = std::prev(ByLevel.end());
          }
          It->second.merge(Walk.keptOf(Where));
        }
        for (const auto &[Level, Kept] : ByLevel)
          if (Kept.any())
            Keep(Kept, From(Value, Level));
      }
    }
  }
  return Stores;
}

CStores storesParameter(const FunctionDecl *Function, unsigned iParameter,
                        const CThreadLocalData &Data) {
  // Not a copy or a move, which copies what its source holds.
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      Method && (Method->isCopyAssignmentOperator() ||
                 Method->isMoveAssignmentOperator()))
    return {};
  if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Function);
      Constructor && Constructor->isCopyOrMoveConstructor())
    return {};
  const FunctionDecl *Definition = nullptr;
  if (!Function->hasBody(Definition) || !Definition ||
      iParameter >= Definition->getNumParams() || !Definition->getBody())
    return {};
  if (const QualType Type = Definition->getParamDecl(iParameter)->getType();
      !Type->isPointerType() && !Type->isReferenceType())
    return {};
  // Of the lists of the data (CThreadLocalData::listsKey), each once, worked
  // out as what is given to it is as told where nothing is set
  // (CThreadLocalData::setObjectCopied); one that reaches itself stores it
  // only as the rest of it does, which is taken of it while it is told.
  using CKey = std::pair<const FunctionDecl *, unsigned>;
  struct CSummaries {
    std::string Lists;
    const CThreadLocalLists *Of = nullptr;
    ASTContext *Context = nullptr;
    TCSummaries<CKey, CStores> Results{
        [this](const CKey &Key) {
          return storesParameterUncached(Key.first, Key.second,
                                         CThreadLocalData(*Context, *Of));
        },
        CStores(), CStores{CStore{CKept{true, false, {}}, 0}}};
  };
  CSummaries &Summaries = astCache<CSummaries>(Definition->getASTContext());
  if (Summaries.Lists != Data.listsKey()) {
    Summaries.Lists = Data.listsKey();
    Summaries.Results.clear();
  }
  Summaries.Of = &Data.lists();
  Summaries.Context = &Definition->getASTContext();
  return Summaries.Results.get({Definition, iParameter});
}

/// The innermost instantiation of a template that \p D is in, itself too: a
/// function, also a member of a class template, or a class.
const Decl *innermostInstantiation(const Decl *D) {
  for (const DeclContext *Context = isa<DeclContext>(D) ? cast<DeclContext>(D)
                                                        : D->getDeclContext();
       Context; Context = Context->getParent()) {
    if (const auto *Function = dyn_cast<FunctionDecl>(Context);
        Function && Function->getTemplateInstantiationPattern())
      return Function;
    if (const auto *Record = dyn_cast<CXXRecordDecl>(Context);
        Record && Record->getTemplateInstantiationPattern())
      return Record;
  }
  return nullptr;
}

/// Where \p Instantiation, of a function or a class, was instantiated, also
/// of a member of a class template, by its class.
SourceLocation pointOfInstantiation(const Decl *Instantiation) {
  if (stackNearlyExhausted())
    return withStack([&] { return pointOfInstantiation(Instantiation); });
  if (const auto *Function = dyn_cast<FunctionDecl>(Instantiation)) {
    if (Function->getPointOfInstantiation().isValid())
      return Function->getPointOfInstantiation();
    if (const auto *Record = dyn_cast<CXXRecordDecl>(Function->getParent()))
      return pointOfInstantiation(Record);
    return {};
  }
  if (const auto *Specialization =
          dyn_cast<ClassTemplateSpecializationDecl>(Instantiation))
    return Specialization->getPointOfInstantiation();
  if (const auto *Record = dyn_cast<CXXRecordDecl>(Instantiation)) {
    if (const MemberSpecializationInfo *Member =
            Record->getMemberSpecializationInfo();
        Member && Member->getPointOfInstantiation().isValid())
      return Member->getPointOfInstantiation();
    if (const auto *Outer = dyn_cast<CXXRecordDecl>(Record->getParent()))
      return pointOfInstantiation(Outer);
  }
  return {};
}

/// typeNames, through the types not in \p Seen, each of which is looked into
/// once, also one that the arguments of several templates share.
bool typeNames(QualType Type, const Decl *Instantiation,
               llvm::SmallPtrSetImpl<const clang::Type *> &Seen) {
  if (stackNearlyExhausted())
    return withStack([&] { return typeNames(Type, Instantiation, Seen); });
  if (Type.isNull())
    return false;
  Type = Type.getCanonicalType().getNonReferenceType();
  while (Type->isPointerType() || Type->isArrayType())
    Type = Type->isPointerType()
               ? Type->getPointeeType().getCanonicalType()
               : QualType(Type->getArrayElementTypeNoTypeQual(), 0)
                     .getCanonicalType();
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record || !Seen.insert(Type.getTypePtr()).second)
    return false;
  if (Record->getCanonicalDecl() == Instantiation->getCanonicalDecl())
    return true;
  if (const auto *Specialization =
          dyn_cast<ClassTemplateSpecializationDecl>(Record))
    for (const TemplateArgument &Argument :
         Specialization->getTemplateArgs().asArray())
      if (Argument.getKind() == TemplateArgument::Type &&
          typeNames(Argument.getAsType(), Instantiation, Seen))
        return true;
  return false;
}

/// Whether \p Type names \p Instantiation, a class, also through pointers,
/// references, arrays and the arguments of a template.
bool typeNames(QualType Type, const Decl *Instantiation) {
  llvm::SmallPtrSet<const clang::Type *, 8> Seen;
  return typeNames(Type, Instantiation, Seen);
}

/// Whether \p User, an instantiation of a function or a class, uses \p
/// Instantiation: calls it, or names its class.
bool uses(const Decl *User, const Decl *Instantiation) {
  if (const auto *Record = dyn_cast<CXXRecordDecl>(User)) {
    if (!Record->hasDefinition())
      return false;
    Record = Record->getDefinition();
    return llvm::any_of(Record->fields(),
                        [&](const FieldDecl *Field) {
                          return typeNames(Field->getType(), Instantiation);
                        }) ||
           llvm::any_of(Record->bases(), [&](const CXXBaseSpecifier &Base) {
             return typeNames(Base.getType(), Instantiation);
           });
  }
  const auto *Function = dyn_cast<FunctionDecl>(User);
  if (!Function || !Function->getBody())
    return false;
  bool Found = false;
  forEachStmtOnce(Function->getBody(), [&](const Stmt *S) {
    if (const auto *Call = dyn_cast<CallExpr>(S))
      Found = Call->getDirectCallee() == Instantiation;
    else if (const auto *Construct = dyn_cast<CXXConstructExpr>(S))
      Found = Construct->getConstructor() == Instantiation ||
              typeNames(Construct->getType(), Instantiation);
    else if (const auto *New = dyn_cast<CXXNewExpr>(S))
      Found = typeNames(New->getAllocatedType(), Instantiation);
    else if (const auto *Declaration = dyn_cast<DeclStmt>(S))
      Found = llvm::any_of(Declaration->decls(), [&](const Decl *Declared) {
        const auto *Variable = dyn_cast<VarDecl>(Declared);
        return Variable && typeNames(Variable->getType(), Instantiation);
      });
    else if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
      Found = Ref->getDecl() == Instantiation;
    return Found ? EWalk::Stop : EWalk::Descend;
  });
  return Found;
}

} // namespace

/// The outermost location in code that isn't a template's that made the
/// instantiation \p D is in: where it was instantiated, and, when that is in
/// a template, where the instantiation of that which uses it was, and so on;
/// \p D's own location when it isn't in one.
SourceLocation ThreadLocalStorageCheck::userLocation(const Decl *D,
                                                     SourceLocation Own,
                                                     ASTContext &Context) {
  SourceLocation Loc = Own;
  const Decl *Current = D;
  const SourceManager &SM = Context.getSourceManager();
  for (int Depth = 0; Depth < 16 && Current; ++Depth) {
    const Decl *Instantiation = innermostInstantiation(Current);
    if (!Instantiation)
      return Loc;
    const SourceLocation Point = pointOfInstantiation(Instantiation);
    if (Point.isInvalid())
      return Loc;
    Loc = Point;
    // What is instantiated whose pattern holds that point and uses it.
    const Decl *User = nullptr;
    for (const Decl *Pattern : templatePatterns(Context)) {
      if (!Pattern->getSourceRange().isValid() ||
          SM.isBeforeInTranslationUnit(Point, Pattern->getBeginLoc()) ||
          SM.isBeforeInTranslationUnit(Pattern->getEndLoc(), Point))
        continue;
      if (const auto *Function = dyn_cast<FunctionDecl>(Pattern)) {
        for (const FunctionDecl *Instance :
             Instantiations.instantiationsOf(Function, Context))
          if (Instance != Function && uses(Instance, Instantiation)) {
            User = Instance;
            break;
          }
      } else if (const auto *Template = dyn_cast<ClassTemplateDecl>(Pattern)) {
        for (const ClassTemplateSpecializationDecl *Instance :
             Template->specializations())
          if (uses(Instance, Instantiation)) {
            User = Instance;
            break;
          }
      }
      if (User)
        break;
    }
    Current = User;
  }
  return Loc;
}

llvm::ArrayRef<const Decl *>
ThreadLocalStorageCheck::templatePatterns(ASTContext &Context) {
  if (PatternsOf == &Context)
    return Patterns;
  PatternsOf = &Context;
  Patterns.clear();
  llvm::SmallVector<const DeclContext *, 32> Work{
      Context.getTranslationUnitDecl()};
  while (!Work.empty()) {
    const DeclContext *Current = Work.pop_back_val();
    for (const Decl *Declared : Current->decls()) {
      if (const auto *Template = dyn_cast<FunctionTemplateDecl>(Declared)) {
        if (Template->getTemplatedDecl()->doesThisDeclarationHaveABody())
          Patterns.push_back(Template->getTemplatedDecl());
        continue;
      }
      if (const auto *Template = dyn_cast<ClassTemplateDecl>(Declared)) {
        Patterns.push_back(Template);
        if (const CXXRecordDecl *Record = Template->getTemplatedDecl())
          for (const Decl *Member : Record->decls())
            if (const auto *Method = dyn_cast<FunctionDecl>(Member);
                Method && Method->doesThisDeclarationHaveABody())
              Patterns.push_back(Method);
        continue;
      }
      // Out-of-line members of class templates.
      if (const auto *Function = dyn_cast<FunctionDecl>(Declared);
          Function && Function->isTemplated() &&
          Function->doesThisDeclarationHaveABody())
        Patterns.push_back(Function);
      if (const auto *Inner = dyn_cast<DeclContext>(Declared);
          Inner && isa<NamespaceDecl, LinkageSpecDecl, ExportDecl>(Declared))
        Work.push_back(Inner);
    }
  }
  return Patterns;
}

namespace {
/// Whether `this` in the body of \p Lambda is a copy of the object: it
/// captures `*this`, or `this` where that is a copy already, \p Copied.
bool capturesObjectCopy(const LambdaExpr *Lambda, bool Copied) {
  for (const LambdaCapture &Capture : Lambda->captures())
    switch (Capture.getCaptureKind()) {
    case LCK_StarThis:
      return true;
    case LCK_This:
      return Copied;
    case LCK_ByCopy:
    case LCK_ByRef:
    case LCK_VLAType:
      break;
    }
  return false;
}

/// Whether what \p Parameter is given is reported where its function stores
/// it, as it is of a reference or a pointer to a thread-local type, not one
/// that depends on the arguments of the template it is an instantiation of.
bool reportedInside(const ParmVarDecl *Parameter,
                    const CThreadLocalData &Data) {
  return Data.isType(Parameter->getType(), true) &&
         !dependentInPattern(Parameter);
}


// Where the statements of a body are: the function they run in, their effect
// index, those of them that can run, and the body.
struct CPlace {
  const FunctionDecl *Function = nullptr;
  const CEffectIndex *Index = nullptr;
  const llvm::DenseSet<const Stmt *> *Running = nullptr;
  const Stmt *Body = nullptr;
};

// What an object made that a store ends up in is, which its exemptions
// depend on (CStorePolicy::exempt).
enum class EMade {
  // An aggregate, an array or a list, a member, an element or a base of
  // which is given the data.
  Part,
  // What a constructor makes that keeps what it is given in it.
  Constructed,
  // What a constructor the compiler defines makes, whose default member
  // initializers it runs.
  DefaultMember,
  // A closure that captures the data.
  Closure,
};

// The policy of malterlib-thread-local-storage on where a store ends up
// (CWalk), decided once for each alternative, and the follows it needs of
// locals: whether a local holder escapes its function, and whether a closure
// stays local.
class CStorePolicy {
public:
  CStorePolicy(const CWalk &Walk, const CThreadLocalData &Data,
               const llvm::DenseMap<const Stmt *, bool> &RootExempt)
      : Walk(Walk), Data(Data), Context(Data.context()),
        RootExempt(RootExempt) {}

  // Sets the function of the body whose stores are decided.
  void enter(const FunctionDecl *Function) { In = Function; }
  // Sets where \p Lambda is made.
  void place(const LambdaExpr *Lambda, const CPlace &Place) {
    Places[Lambda] = Place;
  }

  bool exempt(const CWhere &Where, EMade Made);

  // Whether each of \p Wheres is exempt (exempt); of none, what isn't
  // stored anywhere, unless it is a closure, which can be called anywhere.
  bool exemptAll(llvm::ArrayRef<CWhere> Wheres, EMade Made) {
    if (Wheres.empty())
      return Made != EMade::Closure;
    return llvm::all_of(Wheres, [&](const CWhere &Where) {
      return exempt(Where, Made);
    });
  }

  bool exemptCapture(const LambdaExpr *Lambda);
  bool kept(const CWhere &Where);

  // Whether what the function returns may be the data: one of
  // ThreadLocalFunctions, or a member function of a thread-local type, what
  // its callers tell is in it.
  bool returnsData() const {
    const auto *Method = dyn_cast_or_null<CXXMethodDecl>(In);
    return (In && namedFunction(In, Data.functions())) ||
           (Method && Method->isInstance() &&
            Data.isType(Context.getCanonicalTagType(Method->getParent())));
  }

  // Whether the function can hold the data in a local object, a holder: not
  // a coroutine, which can resume on another thread with it.
  bool holdsLocally() const { return !In || !isCoroutine(In); }

private:
  bool escapes(const VarDecl *Holder) { return Escaped.get(Holder); }
  bool escapesUncached(const VarDecl *Holder);
  bool staysLocal(const LambdaExpr *Lambda);
  bool staysLocalUncached(const LambdaExpr *Lambda);
  bool suspendsIn(const CPlace &Place, const Stmt *Outer) const;
  bool suspendsWhileAlive(const VarDecl *Variable, const FunctionDecl *Function,
                          const Stmt *Given = nullptr) const;
  std::optional<bool> guardStaysLocal(const CClosureSite &Site);
  CPlace placeOf(const FunctionDecl *Function, const Stmt *Body) const;

  const CWalk &Walk;
  const CThreadLocalData &Data;
  ASTContext &Context;
  const llvm::DenseMap<const Stmt *, bool> &RootExempt;
  const FunctionDecl *In = nullptr;
  llvm::DenseMap<const LambdaExpr *, CPlace> Places;
  // Whether each closure stays local (staysLocal); one that stays local only
  // through closures that capture it back doesn't, while it is told.
  TCSummaries<const LambdaExpr *, bool> StaysLocal{
      [this](const LambdaExpr *const &Lambda) {
        return staysLocalUncached(Lambda);
      },
      false, false};
  llvm::DenseMap<const LambdaExpr *, bool> CaptureExempt;
  // Whether each local holder escapes its function (escapes); one that
  // escapes only through holders that escape through it doesn't, while it
  // is told, and one whose recursion doesn't settle does.
  TCSummaries<const VarDecl *, bool> Escaped{
      [this](const VarDecl *const &Holder) { return escapesUncached(Holder); },
      false, true};
};

// Whether what is stored in \p Made, ending up at \p Where, may hold the
// data: thread-local storage of the same thread, through a member of a
// class whose members can refer to it (isExempt), the storage an
// assignment operator assigns when that is the data itself (isStorage) or
// as a whole exempt, a root that is, what is returned by what returns the
// data (returnsData), a closure that is (exemptCapture), what isn't
// followed, other than a closure; a local only as it is, not as a member
// of it, and not by a default member initializer or a closure: a local
// array that an aggregate's element is, or a local holder outside a
// coroutine that doesn't escape its function (escapes).
bool CStorePolicy::exempt(const CWhere &Where, EMade Made) {
  if (Where.Exempt)
    return true;
  // What an assignment operator assigns, by whether that is the data itself,
  // or as a whole exempt; of a local, as it is made with it (below).
  if (Where.Assigned &&
      (Data.isStorage(Where.Object) || Where.Kind == EWhere::Exempt))
    return true;
  if (Where.Assigned && Where.Kind != EWhere::Local)
    return false;
  switch (Where.Kind) {
  case EWhere::Thread:
  case EWhere::Exempt:
    return true;
  case EWhere::Given:
  case EWhere::Temporary:
    return Made != EMade::Closure;
  case EWhere::Root:
    return RootExempt.lookup(Where.Root);
  case EWhere::Return:
    return Made != EMade::Closure && returnsData();
  case EWhere::Capture:
    return exemptCapture(Where.Lambda);
  case EWhere::Local:
    if (!Where.Variable || !Where.Direct || Made == EMade::DefaultMember ||
        Made == EMade::Closure)
      return false;
    // An element of a local array, as the local; of the array of a local
    // list, `std::initializer_list`, as a local holder.
    if (Made == EMade::Part && Where.Variable->getType()->isArrayType())
      return true;
    return holdsLocally() && !escapes(Where.Variable);
  case EWhere::Static:
  case EWhere::Member:
  case EWhere::Base:
  case EWhere::Object:
  case EWhere::Parameter:
  case EWhere::Heap:
  case EWhere::Unknown:
    return false;
  }
  return false;
}

// Whether what the closure \p Lambda captures may hold the data: it stays
// local (staysLocal), or where it ends up is exempt.
bool CStorePolicy::exemptCapture(const LambdaExpr *Lambda) {
  if (!Lambda)
    return false;
  if (const auto It = CaptureExempt.find(Lambda); It != CaptureExempt.end())
    return It->second;
  CaptureExempt[Lambda] = false;
  bool Exempt = staysLocal(Lambda);
  if (const auto Place = Places.find(Lambda);
      !Exempt && Place != Places.end()) {
    CWheres Wheres;
    Walk.endsUp(Lambda, *Place->second.Index, EFollow::Made, Wheres);
    Exempt = exemptAll(Wheres, EMade::Closure);
  }
  CaptureExempt[Lambda] = Exempt;
  return Exempt;
}

// Whether what is stored in the storage \p Where stays after the function
// ends: the object, what a parameter refers to, a global, a static, what
// can't be told, the frame of a coroutine; a local in a coroutine, which can
// resume on another thread with it, or that escapes its function.
bool CStorePolicy::kept(const CWhere &Where) {
  switch (Where.Kind) {
  case EWhere::Unknown:
  case EWhere::Static:
  case EWhere::Object:
  case EWhere::Parameter:
    return true;
  // The frame of a coroutine, a parameter held by value (CWalk::into).
  case EWhere::Heap:
    return true;
  // Each local it can be in, `(bChoose ? First : Second).f_Set(...)`.
  case EWhere::Local:
    return !holdsLocally() || !Where.Variable || escapes(Where.Variable);
  case EWhere::Thread:
  case EWhere::Exempt:
  case EWhere::Temporary:
  case EWhere::Member:
  case EWhere::Base:
  case EWhere::Return:
  case EWhere::Capture:
  case EWhere::Root:
  case EWhere::Given:
    return false;
  }
  return false;
}

CPlace CStorePolicy::placeOf(const FunctionDecl *Function,
                             const Stmt *Body) const {
  return {Function, &sharedFlowCache().indexOf(Body, Context),
          runningIn(Body, Function, Context), Body};
}

// Whether the local object \p Holder can leave its function: where what is
// reached through it ends up (CWalk::endsUp), of each use of it, also in the
// closures that capture it and stay local, isn't a local that doesn't
// escape itself.
bool CStorePolicy::escapesUncached(const VarDecl *Holder) {
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Holder->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return false;
  CWalk::CSeen Seen;
  llvm::SmallPtrSet<const LambdaExpr *, 4> Closures;
  CWheres Wheres;
  auto Follow = [&](const CPlace &Place) {
    for (const DeclRefExpr *Ref : sharedFlowCache().namesIn(
             Place.Function, Place.Body, Holder, Context))
      Walk.endsUp(Ref, *Place.Index, EFollow::Derived, Wheres, &Seen);
  };
  Follow(placeOf(Function, Function->getBody()));
  bool Escapes = false;
  for (size_t iWhere = 0; iWhere < Wheres.size() && !Escapes; ++iWhere) {
    const CWhere Where = Wheres[iWhere];
    switch (Where.Kind) {
    case EWhere::Local:
      Escapes = !Where.Variable || !Where.Variable->hasLocalStorage() ||
                escapes(Where.Variable);
      break;
    case EWhere::Capture:
      if (!Where.Lambda || !Where.Lambda->getBody() ||
          !staysLocal(Where.Lambda)) {
        Escapes = true;
        break;
      }
      if (Closures.insert(Where.Lambda).second)
        Follow(placeOf(Where.Lambda->getCallOperator(),
                       Where.Lambda->getBody()));
      break;
    case EWhere::Thread:
    case EWhere::Exempt:
    case EWhere::Temporary:
    case EWhere::Static:
    case EWhere::Member:
    case EWhere::Base:
    case EWhere::Object:
    case EWhere::Parameter:
    case EWhere::Heap:
    case EWhere::Return:
    case EWhere::Root:
    case EWhere::Given:
    case EWhere::Unknown:
      Escapes = true;
      break;
    }
  }
  return Escapes;
}

// Whether a suspension in \p Place that can run is in \p Outer.
bool CStorePolicy::suspendsIn(const CPlace &Place, const Stmt *Outer) const {
  return llvm::any_of(Place.Index->statements(), [&](const Stmt *S) {
    if (!isSuspension(S) || !runs(Place.Running, S))
      return false;
    for (int Level = 0; S && Level < 1024; ++Level) {
      if (S == Outer)
        return true;
      const llvm::ArrayRef<CEffectIndex::CUse> Uses = Place.Index->usesOf(S);
      S = Uses.empty() ? nullptr : Uses.front().Parent;
    }
    return false;
  });
}

// Whether a suspension can run while the local \p Variable of \p Function is
// alive: after its declaration, before its life ends, by the CFG. Of a
// function object of SynchronousFunctionTypes given a value by \p Given, a
// definition of it (CWrapperFlow), while it holds that value: after \p Given,
// before another definition of it.
bool CStorePolicy::suspendsWhileAlive(const VarDecl *Variable,
                                      const FunctionDecl *Function,
                                      const Stmt *Given) const {
  const Stmt *Body = Function ? Function->getBody() : nullptr;
  const CFG *Graph =
      Body ? sharedFlowCache().graphOf(Function, Body, Context) : nullptr;
  if (!Graph)
    return true;
  const CWrapperFlow *Flow =
      Given ? &sharedFlowCache().wrapperFlow(Function, Body, Variable, Context)
            : nullptr;
  if (Flow && !Flow->definitionOf(Given))
    Flow = nullptr;
  llvm::DenseSet<std::pair<unsigned, unsigned>> Definitions;
  if (Flow)
    for (const CWrapperFlow::CDefinition &Each : Flow->Definitions)
      Definitions.insert({Each.Block, Each.Index});
  // The element that declares it, or of a wrapper's value the definition
  // that gives it, then onward to where it ends; not into the handlers of a
  // `try` it is declared in, which unwinding destroys it before
  // (inDispatchTry).
  llvm::SmallVector<std::pair<const CFGBlock *, unsigned>, 8> Work;
  llvm::SmallVector<CFGElement, 1> Declaring;
  for (const CFGBlock *Block : *Graph)
    for (unsigned iElement = 0; iElement < Block->size(); ++iElement) {
      const CFlowEvent Event = eventOf((*Block)[iElement]);
      if (Event.Kind != EEvent::Statement)
        continue;
      if (llvm::is_contained(effectsOf(Event.Statement, Context).Declares,
                             Variable)) {
        if (!Flow)
          Work.push_back({Block, iElement + 1});
        Declaring.push_back((*Block)[iElement]);
      }
      if (Flow &&
          Flow->Definitions[*Flow->definitionOf(Given)].Block ==
              Block->getBlockID() &&
          Flow->Definitions[*Flow->definitionOf(Given)].Index == iElement)
        Work.push_back({Block, iElement + 1});
    }
  auto Unwinds = [&](const CFGBlock *Dispatch) {
    return isHandlerDispatch(Dispatch) &&
           llvm::any_of(Declaring, [&](const CFGElement &Element) {
             return inDispatchTry(Dispatch, Element, Context);
           });
  };
  llvm::DenseSet<const CFGBlock *> Seen;
  while (!Work.empty()) {
    auto [Block, iElement] = Work.pop_back_val();
    bool Ended = false;
    for (; iElement < Block->size() && !Ended; ++iElement) {
      const CFlowEvent Event = eventOf((*Block)[iElement]);
      switch (Event.Kind) {
      case EEvent::LifetimeEnds:
      case EEvent::AutomaticDestructor:
        Ended = Event.Variable == Variable;
        break;
      case EEvent::Statement:
        if (isSuspension(Event.Statement))
          return true;
        Ended = Definitions.contains({Block->getBlockID(), iElement});
        break;
      case EEvent::Initializer:
      case EEvent::NewAllocator:
      case EEvent::LoopExit:
      case EEvent::FullExpressionCleanup:
      case EEvent::ScopeBegin:
      case EEvent::ScopeEnd:
      case EEvent::DeleteDestructor:
      case EEvent::BaseDestructor:
      case EEvent::MemberDestructor:
      case EEvent::TemporaryDestructor:
      case EEvent::CleanupFunction:
        break;
      }
    }
    if (Ended)
      continue;
    for (const CFGBlock::AdjacentBlock &Successor : Block->succs())
      if (const CFGBlock *Next = Successor.getReachableBlock();
          Next && !Unwinds(Next) && Seen.insert(Next).second)
        Work.push_back({Next, 0});
  }
  return false;
}

// Of \p Site, a use of a closure, whether the closure stays local when it
// is given to make the guard of a local scope exit of one of
// ScopeExitHelpers, `auto Cleanup = g_OnScopeExit / [&] { ... };` or `auto
// Cleanup = fg_OnScopeExit([&] { ... });`: the guard is a local only run or
// told to run or not, by a member function of it, before a coroutine
// suspends; none when it isn't given to one.
std::optional<bool> CStorePolicy::guardStaysLocal(const CClosureSite &Site) {
  const CEffectIndex &Index = sharedFlowCache().indexOf(Site.Body, Context);
  const Stmt *Exit = Site.Use.Parent;
  const CEffects *Of = Exit ? Index.effectsOf(Exit) : nullptr;
  // An escape without a use (the root outside a body) is given to no
  // parameter.
  if (!Site.Use.Effect ||
      Site.Use.Effect->Into.Kind != EDestination::Parameter || !Of ||
      !Of->Callee)
    return std::nullopt;
  // The helper: what `operator /` is called on or given first, or the
  // function given the closure first.
  const ValueDecl *Helper = nullptr;
  if (Of->Callee->getOverloadedOperator() == OO_Slash) {
    const auto *Call = dyn_cast<Expr>(Exit);
    const Expr *Object = Call ? callObject(Call, Context) : nullptr;
    Helper =
        namedDecl(Object ? Object : argumentFor(Exit, 0, Context), Context);
  } else if (!isa<CXXMethodDecl>(Of->Callee) && Site.Use.Effect->Into.Index == 0)
    Helper = Of->Callee;
  if (!Helper || !Helper->getIdentifier() ||
      !Data.isScopeExitHelper(identifierTextOf(Helper)))
    return std::nullopt;
  const llvm::SmallVector<CEffectIndex::CUse, 2> Guards =
      Index.consumersOf(Exit);
  const auto *Guard =
      Guards.size() == 1 &&
              Guards.front().Effect->Into.Kind == EDestination::Variable
          ? dyn_cast_or_null<VarDecl>(Guards.front().Effect->Into.Declaration)
          : nullptr;
  const auto *Function = dyn_cast_or_null<FunctionDecl>(Site.Owner);
  if (!Guard || !Guard->hasLocalStorage())
    return false;
  // Run, or a member function of it called, the object and what is called.
  for (const DeclRefExpr *Name :
       sharedFlowCache().namesIn(Site.Owner, Site.Body, Guard, Context))
    for (const CEffectIndex::CUse &Use : Index.consumersOf(Name))
      if (Use.Effect->Use != EUse::Unevaluated &&
          Use.Effect->Use != EUse::CallObject && Use.Effect->Use != EUse::Callee)
        return false;
  return !Function || !isCoroutine(Function) ||
         !suspendsWhileAlive(Guard, Function);
}

// Whether the closure \p Lambda ends with the function that makes it, before
// a coroutine suspends, after which it can resume on another thread: no use
// of it escapes, by the one classifier of its uses (closureUses), called
// where it is made, `[&] { ... }()`, a local only called, also through a
// local reference or a function object of SynchronousFunctionTypes made of
// it, given to what only calls it, or captured by a closure that stays local
// itself; or given to make the guard of a local scope exit that is only run
// or told (guardStaysLocal). In a coroutine, not with a suspension in its
// captures or in what calls it, nor while a local it is in is alive.
bool CStorePolicy::staysLocal(const LambdaExpr *Lambda) {
  return Places.contains(Lambda) && StaysLocal.get(Lambda);
}

bool CStorePolicy::staysLocalUncached(const LambdaExpr *Lambda) {
  const CPlace &At = Places.find(Lambda)->second;
  const CClosureUses &Uses =
      sharedFlowCache().closureUses(At.Function, At.Body, Lambda, Context);
  // Whether a suspension in a coroutine \p Owner can run in \p Outer, of its
  // body \p Body.
  auto SuspendsIn = [&](const Decl *Owner, const Stmt *Body,
                        const Stmt *Outer) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Owner);
    return Function && isCoroutine(Function) &&
           suspendsIn(placeOf(Function, Body), Outer);
  };
  bool Local = llvm::none_of(captureInitsOf(Lambda), [&](const Expr *Init) {
    return Init && SuspendsIn(At.Function, At.Body, Init);
  });
  for (const CClosureSite &Site : Uses.Escapes)
    if (const std::optional<bool> Guarded = guardStaysLocal(Site))
      Local &= *Guarded;
    else
      Local = false;
  for (const CClosureSite &Site : Uses.Calls)
    if (const std::optional<bool> Guarded =
            Site.Kind == EClosureUse::Pass ? guardStaysLocal(Site)
                                           : std::nullopt)
      Local &= *Guarded;
    else
      Local &= !SuspendsIn(Site.Owner, Site.Body, Site.Use.Parent);
  for (const CClosureLocal &Each : Uses.Locals) {
    const auto *Function = dyn_cast_or_null<FunctionDecl>(Each.Owner);
    Local &= !Function || !isCoroutine(Function) ||
             !suspendsWhileAlive(Each.Variable, Function, Each.Given);
  }
  for (const LambdaExpr *Closure : Uses.Closures)
    Local &= Closure != Lambda && staysLocal(Closure);
  return Local;
}

} // namespace

ThreadLocalStorageCheck::ThreadLocalStorageCheck(StringRef Name,
                                                 ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      ThreadLocalFunctions(listOption("ThreadLocalFunctions", "")),
      ThreadLocalTypes(listOption("ThreadLocalTypes", "")),
      ThreadLocalScopeTypes(listOption("ThreadLocalScopeTypes", "")),
      ThreadLocalTemplates(listOption("ThreadLocalTemplates", "")),
      ThreadLocalHandlerTypes(listOption("ThreadLocalHandlerTypes", "")),
      ScopeExitHelpers(listOption("ScopeExitHelpers", "")),
      DebugEffects(Options.get("DebugEffects", "")) {
  splitOptionList(ThreadLocalFunctions, Lists.Functions);
  splitOptionList(ThreadLocalTypes, Lists.Types);
  splitOptionList(ThreadLocalScopeTypes, Lists.ScopeTypes);
  splitOptionList(ThreadLocalTemplates, Lists.Templates);
  splitOptionList(ThreadLocalHandlerTypes, Lists.HandlerTypes);
  splitOptionList(ScopeExitHelpers, Lists.ScopeExitHelpers);
}

void ThreadLocalStorageCheck::storeCheckOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "ThreadLocalFunctions", ThreadLocalFunctions);
  Options.store(Opts, "ThreadLocalTypes", ThreadLocalTypes);
  Options.store(Opts, "ThreadLocalScopeTypes", ThreadLocalScopeTypes);
  Options.store(Opts, "ThreadLocalTemplates", ThreadLocalTemplates);
  Options.store(Opts, "ThreadLocalHandlerTypes", ThreadLocalHandlerTypes);
  Options.store(Opts, "ScopeExitHelpers", ScopeExitHelpers);
}

void ThreadLocalStorageCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(functionDecl(isDefinition()).bind("function"), this);
  Finder->addMatcher(
      traverse(TK_AsIs,
               declaratorDecl(anyOf(varDecl(), fieldDecl())).bind("holder")),
      this);
  Finder->addMatcher(
      traverse(TK_AsIs, varDecl(hasGlobalStorage(), unless(isStaticLocal()),
                                hasInitializer(anything()))
                            .bind("global")),
      this);
  Finder->addMatcher(traverse(TK_AsIs, cxxNewExpr().bind("new")), this);
  // A generic lambda, by its instantiations.
  Finder->addMatcher(lambdaExpr().bind("lambda"), this);
}

/// Whether \p Value, given to what is of \p Type, in an instantiation of a
/// template, is thread-local data only as the instantiation is: not without
/// the parameters and the members of the object of a type that depends on the
/// template's arguments (dependentInPattern). It is reported where the data is
/// given.
static bool fromPattern(const Expr *Value, QualType Type,
                        const CThreadLocalData &Data,
                        std::optional<unsigned> Level) {
  Data.setPatternExcluded(true);
  const bool Refers =
      Level ? Data.isDataAt(Value, *Level) : Data.refersTo(Value, Type);
  Data.setPatternExcluded(false);
  return !Refers;
}

SourceLocation ThreadLocalStorageCheck::instantiatedAt(
    const Expr *Value, QualType Type, const FunctionDecl *In,
    SourceLocation Own, const CThreadLocalData &Data) {
  // The instantiation it is in: \p In, or of a lambda that isn't generic,
  // the function its closure is made in, outward. The call operator of such a
  // lambda in an instantiation is instantiated with it, but where is told by
  // the function.
  const FunctionDecl *Instance = In;
  for (int Depth = 0; Instance && Depth < 16; ++Depth) {
    const auto *Method = dyn_cast<CXXMethodDecl>(Instance);
    if (!Method || !Method->getParent()->isLambda() ||
        isGenericLambdaCallOperatorOrStaticInvokerSpecialization(Method))
      break;
    Instance = dyn_cast_or_null<FunctionDecl>(
        Method->getParent()->getDeclContext());
  }
  if (!Instance || !Instance->isTemplateInstantiation() ||
      !fromPattern(Value, Type, Data, std::nullopt))
    return Own;
  return userLocation(Instance, Own, Data.context());
}

void ThreadLocalStorageCheck::reportStore(const Expr *Value, QualType Type,
                                          const Twine &Where,
                                          const CEffectIndex &Index,
                                          const SourceManager &SM,
                                          std::optional<unsigned> Level) {
  if (Classifier && fromPattern(Value, Type, *Classifier, Level))
    return;
  const bool Reference = Type->isReferenceType() &&
                         !Type.getNonReferenceType()->isPointerType();
  const SourceLocation Loc = reportLocation(
      ReportAt.isValid() ? ReportAt : Index.locationOf(Value), SM);
  if (Loc.isInvalid())
    return;
  diag(Loc, "%select{a pointer|a reference}0 to thread-local data is stored "
            "in %1, which can outlive the expression and be used on another "
            "thread; refer to thread-local data only from locals and "
            "parameters, and get it again where it is used")
      << Reference << Where.str();
}

void ThreadLocalStorageCheck::reportGiven(const Expr *Value, QualType Type,
                                          const FunctionDecl *Callee,
                                          const CEffectIndex &Index,
                                          const SourceManager &SM,
                                          std::optional<unsigned> Level) {
  if (Classifier && fromPattern(Value, Type, *Classifier, Level))
    return;
  const bool Reference = Type->isReferenceType() &&
                         !Type.getNonReferenceType()->isPointerType();
  const SourceLocation Loc = reportLocation(
      ReportAt.isValid() ? ReportAt : Index.locationOf(Value), SM);
  if (Loc.isInvalid())
    return;
  diag(Loc, "%select{a pointer|a reference}0 to thread-local data is given to "
            "%1, which stores it where it can outlive the expression and be "
            "used on another thread; refer to thread-local data only from "
            "locals and parameters, and get it again where it is used")
      << Reference << Callee;
}

void ThreadLocalStorageCheck::debugFlow(const Stmt *Root, const Decl *Owner,
                                        ASTContext &Context) {
  if (!Root)
    return;
  const bool Dump = DebugEffects == "dump";
  const bool Verify = DebugEffects == "verify";
  const SourceManager &SM = Context.getSourceManager();
  CFlowCache &Cache = sharedFlowCache();
  auto Text = [&](const Stmt *S) {
    const std::string Spelled =
        Lexer::getSourceText(
            CharSourceRange::getTokenRange(S->getSourceRange()), SM,
            Context.getLangOpts())
            .str();
    std::string Collapsed;
    for (const char Character : Spelled) {
      if (Collapsed.size() >= 48)
        break;
      if (std::isspace(static_cast<unsigned char>(Character))) {
        if (!Collapsed.empty() && Collapsed.back() != ' ')
          Collapsed.push_back(' ');
        continue;
      }
      Collapsed.push_back(Character);
    }
    return Collapsed;
  };
  auto Show = [&](SourceLocation Loc, const std::string &Message) {
    Loc = reportLocation(Loc, SM);
    if (Loc.isValid())
      diag(Loc, "%0") << Message;
  };
  auto ShowDecl = [&](const ValueDecl *Declared) {
    std::string Held;
    int Count = 0;
    forEachHeld(Declared->getType(), [&](const CHeld &Part) {
      if (++Count > 8)
        return EVisit::Found;
      Held += " ";
      Held += heldName(Part.Kind);
      switch (Part.Kind) {
      case EHeld::Pointer:
      case EHeld::Reference:
      case EHeld::MemberPointer:
      case EHeld::BlockPointer:
        Held += ":" + Part.Pointee.getAsString();
        return EVisit::Descend;
      case EHeld::Record:
        Held += ":" + Part.Record->getNameAsString();
        return EVisit::Descend;
      case EHeld::Dependent:
      case EHeld::Opaque:
        return EVisit::Skip;
      }
      return EVisit::Skip;
    });
    // A binding lives as long as what it is a part of.
    const auto *Variable = dyn_cast<VarDecl>(Declared);
    if (const auto *Binding = dyn_cast<BindingDecl>(Declared))
      Variable = dyn_cast_or_null<VarDecl>(Binding->getDecomposedDecl());
    Show(Declared->getLocation(),
         ("decl " + llvm::Twine(Declared->getDeclName().getAsString()) + " " +
          holderName(holderKind(Declared)) + " " +
          lifetimeName(variableLifetime(Variable).Kind) + " holds" + Held)
             .str());
  };
  // What each declaration is (holderKind), and what it declares in it, a
  // local class's members, an enumeration's enumerators, a using
  // declaration's shadows.
  TCRecursive<void(const Decl *, int)> ShowKind = [&](const Decl *Declared,
                                                         int Depth) {
    if (!Declared || Depth > 2)
      return;
    Show(Declared->getLocation(),
         (Twine("declkind ") + Declared->getDeclKindName() + " " +
          holderName(holderKind(Declared)))
             .str());
    if (const auto *Context = dyn_cast<DeclContext>(Declared))
      for (const Decl *Inner : Context->decls())
        if (!Inner->isImplicit() || isa<UsingShadowDecl, IndirectFieldDecl>(Inner))
          ShowKind(Inner, Depth + 1);
    if (const auto *Using = dyn_cast<BaseUsingDecl>(Declared))
      for (const UsingShadowDecl *Shadow : Using->shadows())
        ShowKind(Shadow, Depth + 1);
    if (const auto *Decomposition = dyn_cast<DecompositionDecl>(Declared))
      for (const BindingDecl *Binding : Decomposition->bindings())
        ShowKind(Binding, Depth + 1);
  };
  llvm::SmallVector<std::pair<const Stmt *, const Decl *>, 4> Bodies{
      {Root, Owner}};
  // Also the initializers of a constructor, written or not, like what a
  // constructor a derived class inherits gives its base.
  if (const auto *Constructor = dyn_cast_or_null<CXXConstructorDecl>(Owner);
      Constructor && Root == Constructor->getBody())
    for (const CXXCtorInitializer *Initializer : Constructor->inits())
      if (Initializer->getInit())
        Bodies.push_back({Initializer->getInit(), Owner});
  llvm::SmallPtrSet<const CXXConstructorDecl *, 2> Inherited;
  // Each body once, also of a closure in a default argument each use of it
  // evaluates.
  llvm::SmallPtrSet<const Stmt *, 8> Queued;
  for (const auto &[Body, Of] : Bodies)
    Queued.insert(Body);
  while (!Bodies.empty()) {
    const auto [Body, Of] = Bodies.pop_back_val();
    const CEffectIndex &Index = Cache.indexOf(Body, Context);
    const CFG *Graph = Cache.graphOf(Of, Body, Context);
    for (const Stmt *S : Index.statements())
      if (const auto *Lambda = dyn_cast<LambdaExpr>(S);
          Lambda && !Lambda->isGenericLambda() && Lambda->getBody() &&
          Queued.insert(Lambda->getBody()).second)
        Bodies.push_back({Lambda->getBody(), Lambda->getCallOperator()});
    if (!Dump && !Verify)
      continue;
    if (!Graph)
      Show(Body->getBeginLoc(), "flow-graph none");
    // Of what is verified, only what the table leaves out.
    if (Verify) {
      for (const Stmt *S : Index.statements())
        for (const Stmt *Missing : Index.unclassifiedChildren(S))
          Show(S->getBeginLoc(), (Twine("missing ") + S->getStmtClassName() +
                                  " " + Missing->getStmtClassName())
                                     .str());
      continue;
    }
    if (const auto *Function = dyn_cast_or_null<FunctionDecl>(Of))
      for (const ParmVarDecl *Parameter : Function->parameters()) {
        ShowDecl(Parameter);
        ShowKind(Parameter, 0);
      }
    for (const Stmt *S : Index.statements()) {
      const CEffects *Effects = Index.effectsOf(S);
      // Where it is evaluated, of a default argument where it is used.
      const SourceLocation At = Index.locationOf(S);
      if (Effects->bSuspends)
        Show(At,
             (Twine("statement ") + S->getStmtClassName() + " suspends").str());
      if (Effects->bLeaves)
        Show(At,
             (Twine("statement ") + S->getStmtClassName() + " leaves").str());
      // The initializers of a constructor a derived class inherits, which
      // aren't written, that it calls.
      if (const auto *Construct = Effects->Statement == EStatement::Construction
                                      ? cast<CXXConstructExpr>(S)
                                      : nullptr;
          Construct && Construct->getConstructor() &&
          Construct->getConstructor()->isInheritingConstructor() &&
          Inherited.insert(Construct->getConstructor()).second)
        for (const CXXCtorInitializer *Initializer :
             Construct->getConstructor()->inits())
          if (Initializer->getInit())
            Bodies.push_back({Initializer->getInit(), Construct->getConstructor()});
      if (Effects->Statement == EStatement::InheritedConstruction)
        Show(At, (Twine("statement ") + S->getStmtClassName() + " inherits " +
                  cast<CXXInheritedCtorInitExpr>(S)
                      ->getConstructor()
                      ->getQualifiedNameAsString())
                     .str());
      for (const COperandEffect &Effect : Effects->Operands) {
        std::string Into;
        if (Effect.Into.Kind != EDestination::None)
          Into = (Twine(" into ") + destinationName(Effect.Into.Kind) + "/" +
                  lifetimeName(lifetimeOf(Effect.Into).Kind))
                     .str();
        Show(At,
             (Twine("effect ") + S->getStmtClassName() + " " +
              useName(Effect.Use) + " " + Effect.Operand->getStmtClassName() +
              " '" + Text(Effect.Operand) + "'" + Into + " seq " +
              Twine(Effect.Sequence))
                 .str());
      }
      for (const Stmt *Missing : Index.unclassifiedChildren(S))
        Show(S->getBeginLoc(), (Twine("missing ") + S->getStmtClassName() +
                                " " + Missing->getStmtClassName() + " '" +
                                Text(Missing) + "'")
                                   .str());
      if (const auto *Label = dyn_cast<LabelStmt>(S))
        ShowKind(Label->getDecl(), 0);
      if (const auto *Temporary = dyn_cast<MaterializeTemporaryExpr>(S))
        ShowKind(Temporary->getLifetimeExtendedTemporaryDecl(), 0);
      if (const auto *Declaration = dyn_cast<DeclStmt>(S))
        for (const Decl *Declared : Declaration->decls()) {
          ShowKind(Declared, 0);
          if (const auto *Value = dyn_cast<ValueDecl>(Declared))
            ShowDecl(Value);
          if (const auto *Decomposition = dyn_cast<DecompositionDecl>(Declared))
            for (const BindingDecl *Binding : Decomposition->bindings())
              ShowDecl(Binding);
        }
      if (const auto *Lambda = dyn_cast<LambdaExpr>(S))
        for (const LambdaCapture &Capture : Lambda->captures())
          if (Capture.capturesVariable())
            if (const auto *Variable =
                    dyn_cast<VarDecl>(Capture.getCapturedVar());
                Variable && Variable->isInitCapture())
              ShowDecl(Variable);
    }
    if (!Graph)
      continue;
    const llvm::BitVector &Reachable = Cache.reachableOf(Of, Body, Context);
    for (const CFGBlock *Block : forwardOrder(*Graph)) {
      if (!Reachable.test(Block->getBlockID()))
        continue;
      for (const CFGElement &Element : *Block) {
        const CFlowEvent Event = eventOf(Element);
        std::string What;
        SourceLocation Loc = Body->getBeginLoc();
        if (Event.Statement) {
          What = (Twine(" ") + Event.Statement->getStmtClassName() + " '" +
                  Text(Event.Statement) + "'")
                     .str();
          Loc = Event.Statement->getBeginLoc();
        }
        if (Event.Variable) {
          What += " " + Event.Variable->getNameAsString();
          if (!Event.Statement)
            Loc = Event.Variable->getLocation();
        }
        if (Event.Initializer && Event.Initializer->getInit())
          Loc = Event.Initializer->getInit()->getBeginLoc();
        Show(Loc, (Twine("event ") + eventName(Event.Kind) + What).str());
      }
    }
  }
}

void ThreadLocalStorageCheck::checkRoot(const Stmt *Root,
                                        const FunctionDecl *Function,
                                        const FunctionDecl *Current,
                                        const CThreadLocalData &Data,
                                        const CheckedInit *Initializer) {
  if (stackNearlyExhausted())
    return withStack(
        [&] { checkRoot(Root, Function, Current, Data, Initializer); });
  ASTContext &Context = Data.context();
  const SourceManager &SM = Context.getSourceManager();
  if (!DebugEffects.empty()) {
    debugFlow(Root, Function, Context);
    if (DebugEffects == "dump")
      return;
  }
  if (!Root)
    return;
  CFlowCache &Cache = sharedFlowCache();
  auto Name = [](const NamedDecl *Decl) {
    return "'" + Decl->getNameAsString() + "'";
  };

  // What is checked: the root, the member initializers of a constructor, and
  // the bodies of the lambdas in them, each by its effect index, the function
  // it is in, the statements of it that can run, and whether `this` in it is
  // a copy of the object, of a closure that captures `*this`, also in the
  // closures in it that capture `this`.
  struct CBody {
    const Stmt *Root;
    const FunctionDecl *Function;
    const llvm::DenseSet<const Stmt *> *Running;
    bool ObjectCopied = false;
  };
  llvm::SmallVector<CBody, 4> Bodies;
  // The initializers checked as a whole, of a variable outside a function
  // and the member initializers of a constructor: what each is stored in,
  // whether that may hold the data, and its type.
  llvm::DenseMap<const Stmt *, CWhere> Roots;
  llvm::DenseMap<const Stmt *, std::string> RootWhere;
  llvm::DenseMap<const Stmt *, bool> RootExempt;
  llvm::DenseMap<const Stmt *, QualType> RootType;
  auto AddRoot = [&](const Expr *Init, std::string Where, bool Exempt,
                     QualType Type) {
    CWhere Site;
    Site.Kind = EWhere::Root;
    Site.Root = Init;
    Roots[Init] = Site;
    RootWhere[Init] = std::move(Where);
    RootExempt[Init] = Exempt;
    RootType[Init] = Type;
  };
  const llvm::DenseSet<const Stmt *> *Running =
      runningIn(Root, Function, Context);
  Bodies.push_back({Root, Current, Running, Data.objectCopied()});
  if (Initializer)
    AddRoot(Initializer->Init, Initializer->Where, Initializer->Exempt,
            Initializer->Type);
  // Each initializer of a constructor, also what isn't written: a default
  // member initializer it runs, `int *m_pValue = &t_Value;`, and what a
  // constructor the compiler defines runs of a member or a base.
  if (const auto *Constructor = dyn_cast_or_null<CXXConstructorDecl>(Function);
      Constructor && Root == Function->getBody())
    for (const CXXCtorInitializer *Each : Constructor->inits()) {
      const Expr *Init = Each->getInit();
      if (!Init)
        continue;
      if (const EStatement Kind = effectsOf(Init, Context).Statement;
          !Each->isWritten() && Kind != EStatement::Default &&
          Kind != EStatement::Construction)
        continue;
      if (const FieldDecl *Field = Each->getMember())
        AddRoot(Init, "the member " + Name(Field),
                Data.isExempt(Field->getParent()), Field->getType());
      else if (Each->isBaseInitializer())
        AddRoot(Init, "the base " + Init->getType().getAsString(),
                Data.isExempt(Constructor->getParent()), Init->getType());
      // The constructor delegated to, by what it keeps elsewhere: what it
      // keeps in the object is the object's own, of where it is made.
      else if (Each->isDelegatingInitializer())
        AddRoot(Init, "the delegated constructor", true, Init->getType());
      else
        continue;
      // What can run of it, which the CFG of the constructor has.
      Bodies.push_back({Init, Current, Running, Data.objectCopied()});
    }

  const CWalk Walk(Data, Roots);
  CStorePolicy Policy(Walk, Data, RootExempt);
  // The default member initializers of each class, also through the classes
  // on the way to them, whose exemption holds for them (isExempt).
  const auto IsExempt = [&](const CXXRecordDecl *Record) {
    return Data.isExempt(Record);
  };
  CImplicitDefaults MemberDefaults(IsExempt);
  // Whether an object of \p Class can hold what refers to what is of \p
  // Pointee, by its type (forEachHeld): a pointer or a reference to it or to
  // a base of it, not of what it holds; not a void pointer, nor bytes, which
  // a constructor that can't be seen isn't taken to keep it in.
  // Of an array on either side, what its elements are, a whole array or one
  // it decays to a pointer to (heldElement).
  auto CanHold = [&](const CXXRecordDecl *Class, QualType Pointee) {
    const QualType To = heldElement(Pointee);
    const CXXRecordDecl *ToRecord = To->getAsCXXRecordDecl();
    return forEachHeld(Context.getCanonicalTagType(Class), [&](const CHeld &Part) {
      switch (Part.Kind) {
      case EHeld::Pointer:
      case EHeld::Reference: {
        const QualType Held = Part.Element;
        if (Held == To)
          return EVisit::Found;
        if (Held->isVoidType() || Held->isCharType() || Held->isStdByteType())
          return EVisit::Skip;
        const CXXRecordDecl *HeldRecord = Held->getAsCXXRecordDecl();
        return Held == To || (ToRecord && HeldRecord && ToRecord->hasDefinition() &&
                              ToRecord->isDerivedFrom(HeldRecord))
                   ? EVisit::Found
                   : EVisit::Skip;
      }
      case EHeld::Record:
        return EVisit::Descend;
      case EHeld::Opaque:
        return EVisit::Found;
      case EHeld::MemberPointer:
      case EHeld::BlockPointer:
      case EHeld::Dependent:
        return EVisit::Skip;
      }
      return EVisit::Skip;
    });
  };
  // What a store that ends up in \p Where is said to be stored in.
  auto Describe = [&](const CWhere &Where) -> std::string {
    if (Where.Assigned)
      return "what it is assigned to";
    switch (Where.Kind) {
    case EWhere::Thread:
    case EWhere::Local:
    case EWhere::Static:
      if (!Where.Variable)
        return "what the expression refers to";
      if (Where.Variable->hasLocalStorage() || Where.Variable->isInitCapture())
        return "the local " + Name(Where.Variable);
      return (Where.Variable->isStaticLocal() ? "the static "
                                              : "the variable ") +
             Name(Where.Variable);
    case EWhere::Member:
      return Where.Field ? "the member " + Name(Where.Field)
                         : "what the expression refers to";
    case EWhere::Base:
      return "the base";
    case EWhere::Heap:
      return "an object on the heap";
    case EWhere::Return:
      return "what is returned";
    case EWhere::Capture:
      if (Where.Lambda && Where.Index < Where.Lambda->capture_size()) {
        const LambdaCapture &Capture = Where.Lambda->capture_begin()[Where.Index];
        if (Capture.capturesVariable())
          return "the capture of " + Name(Capture.getCapturedVar());
      }
      return "the capture";
    case EWhere::Root:
      return RootWhere.lookup(Where.Root);
    case EWhere::Exempt:
    case EWhere::Temporary:
    case EWhere::Object:
    case EWhere::Parameter:
    case EWhere::Given:
    case EWhere::Unknown:
      break;
    }
    return "";
  };

  const bool WasCopied = Data.objectCopied();
  const DeclContext *WasCopiedIn = Data.objectCopiedIn();
  const llvm::scope_exit RestoreCopied(
      [&] { Data.setObjectCopied(WasCopied, WasCopiedIn); });
  // Where each closure is made, of every body, also the closures in the
  // closures that run, before what is stored is told, which asks where a
  // closure in another is made (staysLocal). Each body once, as what it is a
  // copy of the object in tells, also of a closure in a default argument
  // that each use of it evaluates.
  llvm::DenseSet<std::pair<const Stmt *, unsigned>> Queued;
  for (const CBody &Body : Bodies)
    Queued.insert({Body.Root, Body.ObjectCopied ? 1u : 0u});
  for (unsigned iBody = 0; iBody < Bodies.size(); ++iBody) {
    const CBody Body = Bodies[iBody];
    const CEffectIndex &Index = Cache.indexOf(Body.Root, Context);
    for (const Stmt *S : Index.statements())
      if (Index.effectsOf(S)->Statement == EStatement::Lambda) {
        const auto *Lambda = cast<LambdaExpr>(S);
        Policy.place(Lambda, {Body.Function, &Index, Body.Running, Body.Root});
        // Not the body of a generic lambda, whose instantiations are checked
        // themselves.
        if (!Lambda->isGenericLambda() && Lambda->getBody() &&
            runs(Body.Running, Lambda) &&
            Queued
                .insert({Lambda->getBody(),
                         capturesObjectCopy(Lambda, Body.ObjectCopied) ? 1u
                                                                       : 0u})
                .second)
          Bodies.push_back(
              {Lambda->getBody(), Lambda->getCallOperator(),
               runningIn(Lambda->getBody(), Lambda->getCallOperator(),
                         Context),
               capturesObjectCopy(Lambda, Body.ObjectCopied)});
      }
  }
  for (unsigned iBody = 0; iBody < Bodies.size(); ++iBody) {
    const CBody Body = Bodies[iBody];
    Data.setObjectCopied(Body.ObjectCopied, Body.Function);
    const CEffectIndex &Index = Cache.indexOf(Body.Root, Context);
    const FunctionDecl *In = Body.Function;
    Policy.enter(In);
    // What a root is, a value an initializer gives as it is, `int *g_pValue =
    // &t_Value`, and what a member initializer gives a pointer member.
    if (const auto *Init = dyn_cast<Expr>(Body.Root);
        Init && Roots.count(Body.Root)) {
      const QualType Given = RootType.lookup(Body.Root);
      // Only where it can run, of a member initializer.
      if (refersAsIs(Given) && !RootExempt.lookup(Body.Root) &&
          (Body.Root == Root || runs(Body.Running, Init)) &&
          Data.refersTo(Init, Given))
        reportStore(sameValue(Init, Context), Given,
                    RootWhere.lookup(Body.Root), Index, SM);
    }
    // Each statement that can run, once; what a default argument or a
    // default member initializer evaluates, at each of its uses, reported
    // where that use is (ReportAt), not only at the first.
    llvm::DenseSet<const Stmt *> InDefault;
    llvm::SmallVector<const Stmt *, 2> Defaults;
    for (const Stmt *S : Index.statements())
      if (Index.effectsOf(S)->Statement == EStatement::Default &&
          runs(Body.Running, S) && Index.evaluated(S)) {
        Defaults.push_back(S);
        for (const Stmt *Evaluated : Index.evaluationOf(S))
          InDefault.insert(Evaluated);
      }
    // Each store of \p S, by where it ends up (CWalk::destinationOf), as the
    // policy decides (CStorePolicy).
    auto CheckStatement = [&](const Stmt *S) {
      if (const auto *E = dyn_cast<Expr>(S);
          E && (E->isTypeDependent() || E->isValueDependent()))
        return;
      const CEffects &Effects = *Index.effectsOf(S);
      // A default constructor the compiler defines runs the default member
      // initializers where it is called, `CDefault g_Default;` of `struct
      // CDefault { int *m_pValue = &t_Value; };`, also of its bases and its
      // members it makes the same way: each is stored in the member of what
      // it makes, where that ends up, not in thread-local storage.
      if (const auto *Construct = Effects.Statement == EStatement::Construction
                                      ? cast<CXXConstructExpr>(S)
                                      : nullptr;
          Construct && Construct->getConstructor() &&
          Construct->getConstructor()->isDefaultConstructor() &&
          !Construct->getConstructor()->isUserProvided()) {
        std::optional<bool> Exempt;
        for (const auto &[Field, Own] :
             MemberDefaults.of(Construct->getConstructor()->getParent())) {
          if (!Exempt) {
            CWheres Wheres;
            Walk.endsUp(Construct, Index, EFollow::Made, Wheres);
            Exempt = Policy.exemptAll(Wheres, EMade::DefaultMember);
          }
          checkDefaultMember(Field, Own || *Exempt, Data,
                             Index.locationOf(Construct));
        }
      }
      for (const COperandEffect &Effect : Effects.Operands) {
        // The object of a coroutine that is a member function, which its
        // frame keeps by reference across its suspensions, unless of a
        // thread-local type, which it reports itself. Not of one that can't
        // suspend (callsNonSuspendingCoroutine), whose frame is gone when the
        // call returns, as of its arguments.
        if (Effect.Use == EUse::CallObject &&
            Effects.Statement == EStatement::Call) {
          const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Effects.Callee);
          const auto *Object = dyn_cast<Expr>(Effect.Operand);
          if (Method && Object && Method->isImplicitObjectMemberFunction() &&
              isCoroutine(Method) && !Object->isTypeDependent() &&
              !callsNonSuspendingCoroutine(dyn_cast<CallExpr>(S))) {
            const QualType Bound =
                Object->getType()->isPointerType()
                    ? Object->getType()
                    : Context.getLValueReferenceType(Object->getType());
            if (!Data.isType(Bound, true) && Data.refersTo(Object, Bound))
              reportGiven(Object, Bound, Method, Index, SM);
          }
          continue;
        }
        if (Effect.Use != EUse::Copy && Effect.Use != EUse::Bind)
          continue;
        const auto *Value = dyn_cast<Expr>(Effect.Operand);
        if (!Value || Value->isTypeDependent() || Value->isValueDependent())
          continue;
        const CDestination &Into = Effect.Into;
        const QualType Type = givenType(Effect, Value, Context);
        CWheres Wheres;
        switch (Into.Kind) {
        // A static, a global, thread-local storage aside; not a local.
        case EDestination::Variable:
          if (!refersAsIs(Type))
            break;
          Walk.destinationOf(Effect, S, Index, Wheres);
          for (const CWhere &Where : Wheres)
            if (Where.Kind == EWhere::Static && Data.refersTo(Value, Type)) {
              reportStore(sameValue(Value, Context), Type, Describe(Where),
                          Index, SM);
              break;
            }
          break;
        // A member of an aggregate where it ends up, also of a local holder;
        // an element or a base part where the array or the aggregate ends
        // up.
        case EDestination::Field:
        case EDestination::Element:
        case EDestination::Base: {
          if (!refersAsIs(Type) || !Into.Object)
            break;
          Walk.destinationOf(Effect, S, Index, Wheres);
          const auto Reported = llvm::find_if(Wheres, [&](const CWhere &Where) {
            return !Policy.exempt(Where, EMade::Part);
          });
          if (Reported == Wheres.end() || !Data.refersTo(Value, Type))
            break;
          const auto *Field = dyn_cast_or_null<FieldDecl>(Into.Declaration);
          const FieldDecl *Member = Field ? Field : Reported->Part;
          reportStore(sameValue(Value, Context), Type,
                      Member ? "the member " + Name(Member)
                             : Describe(*Reported),
                      Index, SM);
          break;
        }
        case EDestination::Heap:
          if (refersAsIs(Type) && Data.refersTo(Value, Type))
            reportStore(sameValue(Value, Context), Type,
                        "an object on the heap", Index, SM);
          break;
        // A pointer assigned, unless where it is a local, thread-local data
        // itself, of the same thread, or a member of a class whose members can
        // refer to it.
        case EDestination::Designated: {
          const Expr *Target = Into.Object;
          if (!Target || !refersAsIs(Type) || !Data.refersTo(Value, Type) ||
              Data.isStorage(Target))
            break;
          Walk.destinationOf(Effect, S, Index, Wheres);
          // The copy a closure keeps, which it can be called with again,
          // unless the closure ends with the function that makes it or is
          // where it may hold the data (exemptCapture).
          if (Wheres.size() == 1 && Wheres.front().Kind == EWhere::Capture) {
            if (!Policy.exemptCapture(Wheres.front().Lambda))
              reportStore(Value, Type,
                          "the capture " + Name(Wheres.front().Variable),
                          Index, SM);
            break;
          }
          // The own storage of an object whose members can refer to it, a
          // member of it at any depth.
          if (Wheres.size() == 1 && Wheres.front().Kind == EWhere::Exempt)
            break;
          auto Any = [&](auto &&Is) { return llvm::any_of(Wheres, Is); };
          const bool Always = Any([](const CWhere &Where) {
            return Where.Kind == EWhere::Unknown;
          });
          const auto Parameter = llvm::find_if(Wheres, [](const CWhere &Where) {
            return Where.Kind == EWhere::Parameter;
          });
          // By what it is (effectsOf): a member, by its name, unless of a
          // class whose members can refer to it; a variable; what a
          // parameter refers to.
          const Expr *Bare = sameValue(Target, Context);
          // A name of a structured binding by the part it names.
          if (const ValueDecl *Named = namedDecl(Bare, Context);
              Named && holderKind(Named) == EHolder::Binding &&
              cast<BindingDecl>(Named)->getBinding())
            Bare = sameValue(cast<BindingDecl>(Named)->getBinding(), Context);
          std::string Where;
          // A member that isn't a reference, whose storage is elsewhere.
          // Also through an alias of a member of a local holder, as the
          // member.
          if (const ValueDecl *Member = memberDecl(Bare, Context);
              Member && !Member->getType()->isReferenceType())
            Where = "the member " + Name(Member);
          else if (!Any([](const CWhere &Each) {
                     return Each.Kind == EWhere::Unknown ||
                            Each.Kind == EWhere::Object ||
                            Each.Kind == EWhere::Parameter ||
                            (Each.Kind == EWhere::Local && Each.LocalPart);
                   }))
            break;
          else if (const ValueDecl *Named = namedDecl(Bare, Context);
                   Named && Always)
            Where = "the variable " + Name(Named);
          else if (Parameter != Wheres.end() && In &&
                   Parameter->Index < In->getNumParams())
            Where = "what the parameter " +
                    Name(In->getParamDecl(Parameter->Index)) + " refers to";
          else
            Where = "what the expression refers to";
          reportStore(Value, Type, Where, Index, SM);
          break;
        }
        // What is returned, unless its type tells it is the data; not of a
        // member function of a thread-local type, what its callers tell is
        // in it (isData), nor of one of ThreadLocalFunctions.
        case EDestination::Return: {
          const bool Coreturn = Effects.Statement == EStatement::Coreturn;
          const QualType ReturnType =
              Coreturn ? Value->getType()
                       : (In ? In->getReturnType() : QualType());
          if (ReturnType.isNull() || Policy.returnsData())
            break;
          if ((ReturnType->isPointerType() || ReturnType->isReferenceType()) &&
              !Data.isType(ReturnType, true) &&
              Data.refersTo(Value, ReturnType)) {
            const SourceLocation Loc = reportLocation(
                instantiatedAt(Value, ReturnType, In, Index.locationOf(Value),
                               Data),
                SM);
            if (Loc.isValid())
              diag(Loc,
                   "%select{a pointer|a reference}0 to thread-local data is "
                   "returned as a type that isn't %select{a pointer|a "
                   "reference}0 to a thread-local type, so what it is given "
                   "to can't tell it is thread-local data; return the "
                   "thread-local type, or a copy")
                  << ReturnType->isReferenceType();
          }
          break;
        }
        // A capture of a closure that doesn't stay local, also a closure in
        // thread-local storage aside (exemptCapture).
        case EDestination::Capture: {
          const auto *Lambda = dyn_cast_or_null<LambdaExpr>(Into.Object);
          if (!Lambda || Into.Index >= Lambda->capture_size() ||
              Policy.exemptCapture(Lambda))
            break;
          const LambdaCapture &Capture = Lambda->capture_begin()[Into.Index];
          // Where it is captured, of an initializer where that is.
          const auto *CapturedVariable = Capture.capturesVariable()
                                             ? dyn_cast<VarDecl>(
                                                   Capture.getCapturedVar())
                                             : nullptr;
          // Only where it is reported (instantiatedAt), which asks again.
          auto Report = [&](bool Reference, const std::string &What) {
            const SourceLocation Loc = reportLocation(
                instantiatedAt(Value,
                               Capture.getCaptureKind() == LCK_ByRef
                                   ? Context.getLValueReferenceType(
                                         Value->getType())
                                   : Value->getType(),
                               In,
                               Capture.isImplicit() ||
                                       (CapturedVariable &&
                                        CapturedVariable->isInitCapture())
                                   ? Index.locationOf(sameValue(Value, Context))
                                   : Capture.getLocation(),
                               Data),
                SM);
            if (Loc.isValid())
              diag(Loc, "%select{a pointer|a reference}0 to thread-local "
                        "data is stored in the capture of %1, which can "
                        "outlive the expression and be used on another "
                        "thread; refer to thread-local data only from locals "
                        "and parameters, and get it again where it is used")
                  << Reference << What;
          };
          switch (Capture.getCaptureKind()) {
          // `this`, a pointer to the object, of a thread-local type.
          // Not a copy of the object, of a closure that captures `*this`.
          case LCK_This:
            if (const CXXRecordDecl *Class = enclosingThisClass(Lambda);
                Class && !Data.objectCopied() &&
                Data.isType(Context.getCanonicalTagType(Class)))
              Report(false, "'this'");
            break;
          case LCK_StarThis:
          case LCK_VLAType:
            break;
          case LCK_ByRef:
          case LCK_ByCopy: {
            const ValueDecl *Captured = Capture.getCapturedVar();
            const auto *Variable = dyn_cast<VarDecl>(Captured);
            const bool Copied = Capture.getCaptureKind() == LCK_ByCopy;
            QualType Element = Captured->getType().getNonReferenceType();
            while (const ArrayType *Array = Element->getAsArrayTypeUnsafe())
              Element = Array->getElementType();
            // What can refer to it and does where the closure is made
            // (capturedBy).
            if (const CThreadLocalData::CCaptured Of =
                    Data.capturedBy(Capture, Value);
                !Of.Refers || !Of.Holds)
              break;
            Report(!Copied && !Element->isPointerType() &&
                       !(Variable && Variable->isInitCapture() &&
                         Variable->getType()->isPointerType()),
                   Name(Captured));
            break;
          }
          }
          break;
        }
        // What is given to a function that stores it (storesParameter), as
        // where it stays tells (kept), or to a constructor that keeps it in
        // what it makes, which ends up where it outlives the expression.
        case EDestination::Parameter: {
          const FunctionDecl *Callee = Into.Callee;
          const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Into.Declaration);
          if (!Callee || !Parameter || !refersAsIs(Parameter->getType()))
            break;
          // What it reports itself, of a thread-local type, where it is
          // defined, also in another translation unit.
          if (reportedInside(Parameter, Data))
            break;
          const QualType Given = Parameter->getType();
          const bool Construction =
              Effects.Statement == EStatement::Construction;
          const auto *Constructor =
              Construction ? dyn_cast<CXXConstructorDecl>(Callee) : nullptr;
          if (Construction ? !Constructor ||
                                 Constructor->isCopyOrMoveConstructor()
                           : Effects.Statement != EStatement::Call ||
                                 namedFunction(Callee, Lists.Functions) ||
                                 Into.Index < firstGiven(Callee))
            break;
          // What a constructor keeps in itself doesn't outlive the
          // expression where it ends up where it may hold the data: a
          // temporary, a local that stays local, outside a coroutine,
          // thread-local storage, a scope or a handler, also of its own
          // class (isExempt); what it keeps elsewhere does.
          std::optional<bool> LocalObject;
          auto IsLocalObject = [&] {
            if (!LocalObject) {
              CWheres Made;
              Walk.endsUp(S, Index, EFollow::Made, Made);
              LocalObject = Data.isExempt(Constructor->getParent()) ||
                            Policy.exemptAll(Made, EMade::Constructed);
            }
            return *LocalObject;
          };
          const std::string Made =
              Constructor ? "the " + Name(Constructor->getParent()) + " made of it"
                          : std::string();
          // Of a constructor that can't be seen, by what its class can hold,
          // also of what is const.
          if (Constructor && !Constructor->hasBody()) {
            if (!Given->getPointeeType().isNull() && !IsLocalObject() &&
                CanHold(Constructor->getParent(), Given->getPointeeType()) &&
                Data.refersTo(Value, Given))
              reportStore(Value, Given, Made, Index, SM);
            break;
          }
          // Each store, at the level of the argument it keeps.
          Walk.destinationOf(Effect, S, Index, Wheres);
          llvm::SmallVector<unsigned, 2> Levels;
          for (const CWhere &Where : Wheres)
            if (!llvm::is_contained(Levels, Where.Level))
              Levels.push_back(Where.Level);
          for (unsigned Level : Levels) {
            if (llvm::none_of(Wheres, [&](const CWhere &Where) {
                  return Where.Level == Level &&
                         (Where.Made ? !IsLocalObject() : Policy.kept(Where));
                }) ||
                !Data.isDataAt(Value, Level))
              continue;
            if (Constructor)
              reportStore(Value, Given, Made, Index, SM, Level);
            else
              reportGiven(Value, Given, Callee, Index, SM, Level);
            break;
          }
          break;
        }
        case EDestination::None:
        case EDestination::Temporary:
        case EDestination::Thrown:
          break;
        }
      }
    };
    for (const Stmt *S : Index.statements())
      if (!InDefault.contains(S) && runs(Body.Running, S))
        CheckStatement(S);
    for (const Stmt *Default : Defaults) {
      if (InDefault.contains(Default))
        continue;
      ReportAt = usedLocation(Default);
      for (const Stmt *S : Index.evaluationOf(Default))
        CheckStatement(S);
      ReportAt = SourceLocation();
    }
  }
}

void ThreadLocalStorageCheck::check(const MatchFinder::MatchResult &Result) {
  // Nothing in a translation unit with an error that stops it from compiling
  // (hasUncompilableErrors): no flow is built there, and what the errors leave
  // can't be relied on.
  if (hasUncompilableErrors(*Result.Context))
    return;
  if (Repeated.repeated(Result))
    return;
  ASTContext &Context = *Result.Context;
  const CThreadLocalData Data(Context, Lists);
  Classifier = &Data;
  const SourceManager &SM = Context.getSourceManager();
  // What is held for each thread, a class that isn't one of ThreadLocalTypes,
  // which isn't taken as thread-local data elsewhere: each of them.
  if (const auto *Holder = Result.Nodes.getNodeAs<DeclaratorDecl>("holder")) {
    // Also a member of a template as instantiated, of a type that doesn't
    // depend on it; not in the template itself, which is reported where
    // code that isn't a template's made the instantiation.
    if (Holder->getType().isNull() || Holder->getType()->isDependentType() ||
        Holder->getDeclContext()->isDependentContext())
      return;
    // Not a member of a template whose type is an argument of it, which is
    // reported where the argument is given.
    if (isa<FieldDecl>(Holder) &&
        Holder->getType()->getAs<SubstTemplateTypeParmType>())
      return;
    for (const CXXRecordDecl *Held : Data.heldPerThread(Holder->getType())) {
      if (const CXXRecordDecl *Definition = Held->getDefinition())
        Held = Definition;
      if (Data.isListedType(Held))
        continue;
      // In an instantiation, where code that isn't a template's made it.
      const SourceLocation Loc = reportLocation(
          userLocation(Holder, Holder->getLocation(), Context), SM);
      if (Loc.isValid())
        diag(Loc, "%0 holds '%1' for each thread, which isn't one of "
                  "ThreadLocalTypes, so it isn't taken as thread-local data; "
                  "add it to ThreadLocalTypes in the options file, or hold a "
                  "struct of its own for the thread instead")
            << Holder << threadLocalTypeName(Held);
    }
    return;
  }
  // Also what is made on the heap, in an instantiation of a template where
  // code that isn't a template's made it; not of a type of an argument of a
  // class template, which the declaration that gives it is reported for.
  if (const auto *New = Result.Nodes.getNodeAs<CXXNewExpr>("new")) {
    const QualType Type = New->getAllocatedType();
    if (Type.isNull() || Type->isDependentType())
      return;
    if (const auto *Argument = Type->getAs<SubstTemplateTypeParmType>();
        Argument && isa_and_nonnull<ClassTemplateSpecializationDecl>(
                        Argument->getAssociatedDecl()))
      return;
    const FunctionDecl *Function = nullptr;
    DynTypedNode Node = DynTypedNode::create(*New);
    for (int Level = 0; Level < 256 && !Function; ++Level) {
      const auto Parents = Context.getParents(Node);
      if (Parents.empty())
        break;
      Function = Parents[0].get<FunctionDecl>();
      Node = Parents[0];
    }
    for (const CXXRecordDecl *Held : Data.heldPerThread(Type)) {
      if (const CXXRecordDecl *Definition = Held->getDefinition())
        Held = Definition;
      if (Data.isListedType(Held))
        continue;
      const SourceLocation Loc = reportLocation(
          Function ? userLocation(Function, New->getBeginLoc(), Context)
                   : New->getBeginLoc(),
          SM);
      if (Loc.isValid())
        diag(Loc, "the object made on the heap holds '%0' for each thread, "
                  "which isn't one of ThreadLocalTypes, so it isn't taken as "
                  "thread-local data; add it to ThreadLocalTypes in the "
                  "options file, or hold a struct of its own for the thread "
                  "instead")
            << threadLocalTypeName(Held);
    }
    return;
  }
  // The initializer of a global or a static member, also what lambdas in it
  // capture; a default member initializer where it runs, of a constructor
  // that runs it or where one the compiler defines is called.
  if (const auto *Variable = Result.Nodes.getNodeAs<VarDecl>("global")) {
    if (!Variable->getInit() ||
        Variable->getDeclContext()->isDependentContext() ||
        Variable->getInit()->isValueDependent() ||
        Variable->getParentFunctionOrMethod())
      return;
    // Not a thread-local variable, of the same thread (isStorage).
    checkInitializer(Variable->getInit(), Variable->getType(),
                     "the variable '" + Variable->getNameAsString() + "'",
                     Variable->getTLSKind() != VarDecl::TLS_None, Data);
    return;
  }
  // Of a lambda, as part of the function it is in; of a generic one, each of
  // its instantiations, which that doesn't visit.
  if (const auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda")) {
    const FunctionDecl *Operator = Lambda->getCallOperator();
    if (!Lambda->isGenericLambda() || !Operator)
      return;
    for (const FunctionDecl *Instance :
         Instantiations.bodiesOf(Operator, Context)) {
      if (Instance == Operator || Instance->isDependentContext() ||
          !Instance->getBody())
        continue;
      const InstantiationScope Scope(*this, Instance);
      Data.setObjectCopied(capturesObjectCopy(Lambda, objectCopiedAround(Lambda)),
                           Instance);
      checkRoot(Instance->getBody(), Instance, Instance, Data);
      Data.setObjectCopied(false);
    }
    return;
  }
  const auto *Function = Result.Nodes.getNodeAs<FunctionDecl>("function");
  if (!Function || isLambdaCallOperator(Function))
    return;
  // Not of the implementation of one of ThreadLocalTemplates itself.
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      Method && Data.isTemplate(Method->getParent()))
    return;
  for (const FunctionDecl *Body : Instantiations.bodiesOf(Function, Context)) {
    std::optional<InstantiationScope> Scope;
    if (Body != Function)
      Scope.emplace(*this, Body);
    checkRoot(Body->getBody(), Body, Body, Data);
  }
}

// What the default member initializer of \p Field gives, which the default
// constructor the compiler defines runs, checked where it is constructed,
// \p At, and reported there, once for each place.
void ThreadLocalStorageCheck::checkDefaultMember(const FieldDecl *Field,
                                                 bool Exempt,
                                                 const CThreadLocalData &Data,
                                                 SourceLocation At) {
  const Expr *Init = Field->getInClassInitializer();
  if (!Init || Init->isValueDependent() ||
      !CheckedDefaults
           .insert({Field, At.getRawEncoding() * 2 + (Exempt ? 1u : 0u)})
           .second)
    return;
  const SourceLocation WasReportAt = ReportAt;
  ReportAt = At;
  checkInitializer(Init, Field->getType(),
                   "the member '" + Field->getNameAsString() + "'", Exempt,
                   Data);
  ReportAt = WasReportAt;
}

void ThreadLocalStorageCheck::checkInitializer(const Expr *Init, QualType Type,
                                               const std::string &Where,
                                               bool Exempt,
                                               const CThreadLocalData &Data) {
  CheckedInit Initializer{Init, Type, Where, Exempt};
  checkRoot(Init, nullptr, nullptr, Data, &Initializer);
}

} // namespace clang::tidy::malterlib
