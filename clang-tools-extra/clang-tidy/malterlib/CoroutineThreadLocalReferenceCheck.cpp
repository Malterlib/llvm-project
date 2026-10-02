//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CoroutineThreadLocalReferenceCheck.h"
#include "CheckedAccess.h"
#include "ActorAnnotations.h"
#include "HeldData.h"
#include "ThreadLocalUtils.h"
#include "MalterlibOptions.h"
#include "SourceOrder.h"
#include "ValueFlow.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/ScopeExit.h"
#include <tuple>
#include <deque>
#include <map>
#include <functional>

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

CoroutineThreadLocalReferenceCheck::CoroutineThreadLocalReferenceCheck(
    StringRef Name, ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      ThreadLocalFunctions(listOption("ThreadLocalFunctions", "")),
      ThreadLocalTypes(listOption("ThreadLocalTypes", "")),
      ThreadLocalScopeTypes(listOption("ThreadLocalScopeTypes", "")),
      ThreadLocalTemplates(listOption("ThreadLocalTemplates", "")),
      ThreadLocalHandlerTypes(listOption("ThreadLocalHandlerTypes", "")),
      ScopeExitHelpers(listOption("ScopeExitHelpers", "")) {
  splitOptionList(ThreadLocalFunctions, Lists.Functions);
  splitOptionList(ThreadLocalTypes, Lists.Types);
  splitOptionList(ThreadLocalScopeTypes, Lists.ScopeTypes);
  splitOptionList(ThreadLocalTemplates, Lists.Templates);
  splitOptionList(ThreadLocalHandlerTypes, Lists.HandlerTypes);
  splitOptionList(ScopeExitHelpers, Lists.ScopeExitHelpers);
}

void CoroutineThreadLocalReferenceCheck::storeCheckOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "ThreadLocalFunctions", ThreadLocalFunctions);
  Options.store(Opts, "ThreadLocalTypes", ThreadLocalTypes);
  Options.store(Opts, "ThreadLocalScopeTypes", ThreadLocalScopeTypes);
  Options.store(Opts, "ThreadLocalTemplates", ThreadLocalTemplates);
  Options.store(Opts, "ThreadLocalHandlerTypes", ThreadLocalHandlerTypes);
  Options.store(Opts, "ScopeExitHelpers", ScopeExitHelpers);
}

void CoroutineThreadLocalReferenceCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(functionDecl(isDefinition()).bind("function"), this);
  Finder->addMatcher(lambdaExpr().bind("lambda"), this);
}

void CoroutineThreadLocalReferenceCheck::check(
    const MatchFinder::MatchResult &Result) {
  // Nothing in a translation unit with an error that stops it from compiling
  // (hasUncompilableErrors): no flow is built there, and what the errors leave
  // can't be relied on.
  if (hasUncompilableErrors(*Result.Context))
    return;
  if (Repeated.repeated(Result))
    return;
  ASTContext &Context = *Result.Context;
  const CThreadLocalData Data(Context, Lists);
  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  auto ForEachChecked = [&](const FunctionDecl *Function, auto &&Check) {
    for (const FunctionDecl *Body :
         Instantiations.bodiesOf(Function, Context)) {
      std::optional<InstantiationScope> Scope;
      if (Body != Function)
        Scope.emplace(*this, Body);
      Check(Body);
    }
  };
  if (const auto *Function = Result.Nodes.getNodeAs<FunctionDecl>("function")) {
    ForEachChecked(Function, [&](const FunctionDecl *Instance) {
      if (!isLambdaCallOperator(Instance) && isCoroutine(Instance))
        checkFunction(Instance, nullptr, Data);
    });
    return;
  }
  const auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda");
  ForEachChecked(Lambda->getCallOperator(), [&](const FunctionDecl *Operator) {
    const LambdaExpr *Instance =
        Operator == Lambda->getCallOperator()
            ? Lambda
            : Instantiations.lambdaOf(Operator, Context);
    if (Instance && isCoroutine(Operator))
      checkFunction(Operator, Instance, Data);
  });
}

namespace {

// The levels of what an expression is made of, as reach has them: its
// storage, or what a pointer points to, what the value stored there points
// to, and what is reached through that, at any level above.
constexpr unsigned NumLevels = 3;

// What a value refers to the data by: Key, the tracked variable, the object
// or the expression that gives the data, Name, what it is reached by,
// whether it is of the thread that ran before a suspension, and whether it
// is what gives the data elsewhere, stored in what isn't tracked, which only
// what a tracked variable is given refers to.
struct COrigin {
  unsigned Key = 0;
  const Expr *Name = nullptr;
  bool Stale = false;
  bool Elsewhere = false;
  bool operator<(const COrigin &Other) const {
    return std::tie(Key, Name, Stale, Elsewhere) <
           std::tie(Other.Key, Other.Name, Other.Stale, Other.Elsewhere);
  }
  bool operator==(const COrigin &Other) const {
    return Key == Other.Key && Name == Other.Name && Stale == Other.Stale &&
           Elsewhere == Other.Elsewhere;
  }
};

using COrigins = llvm::SmallVector<COrigin, 1>;

// Adds \p From to \p Into, each once, in order.
void addOrigins(COrigins &Into, llvm::ArrayRef<COrigin> From) {
  for (const COrigin &Origin : From) {
    auto *It = llvm::lower_bound(Into, Origin);
    if (It == Into.end() || !(*It == Origin))
      Into.insert(It, Origin);
  }
}

// Makes each of \p Origins of the thread that ran before a suspension.
void makeStale(COrigins &Origins) {
  COrigins Stale;
  for (COrigin Origin : Origins) {
    Origin.Stale = true;
    addOrigins(Stale, Origin);
  }
  Origins = std::move(Stale);
}

// What the value of an evaluated expression refers to the data by, at each
// level.
struct CValue {
  COrigins Levels[NumLevels];
  bool empty() const {
    return llvm::all_of(Levels,
                        [](const COrigins &Origins) { return Origins.empty(); });
  }
  bool operator==(const CValue &Other) const {
    return llvm::equal(Levels, Other.Levels);
  }
};

// A temporary of a scope type that is alive: what makes it, and the use of
// the default it is evaluated for.
using CTemporary = std::pair<const Expr *, const Stmt *>;

// The state of the flow: of each tracked variable and of the object, at each
// level, whether it refers to the data and whether to that of the thread that
// ran before a suspension; the scope variables that can be alive; the values
// of the expressions of the full expressions being evaluated; and the scope
// temporaries that are alive.
struct CState {
  llvm::BitVector Holds[NumLevels];
  llvm::BitVector Stale[NumLevels];
  llvm::BitVector Alive;
  llvm::DenseMap<const Expr *, CValue> Values;
  llvm::SmallVector<CTemporary, 1> Temporaries;
  void addTemporary(const CTemporary &Temporary) {
    auto *It = llvm::lower_bound(Temporaries, Temporary);
    if (It == Temporaries.end() || *It != Temporary)
      Temporaries.insert(It, Temporary);
  }
  void merge(const CState &Other) {
    for (unsigned Level = 0; Level < NumLevels; ++Level) {
      Holds[Level] |= Other.Holds[Level];
      Stale[Level] |= Other.Stale[Level];
    }
    Alive |= Other.Alive;
    for (const auto &[E, Value] : Other.Values) {
      CValue &Into = Values[E];
      for (unsigned Level = 0; Level < NumLevels; ++Level)
        addOrigins(Into.Levels[Level], Value.Levels[Level]);
    }
    for (const CTemporary &Temporary : Other.Temporaries)
      addTemporary(Temporary);
  }
  bool operator==(const CState &Other) const {
    for (unsigned Level = 0; Level < NumLevels; ++Level)
      if (Holds[Level] != Other.Holds[Level] ||
          Stale[Level] != Other.Stale[Level])
        return false;
    if (Alive != Other.Alive || Temporaries != Other.Temporaries ||
        Values.size() != Other.Values.size())
      return false;
    return llvm::all_of(Values, [&](const auto &Each) {
      const auto It = Other.Values.find(Each.first);
      return It != Other.Values.end() && It->second == Each.second;
    });
  }
};

// Where the value of an expression at a level comes from where it is
// evaluated: the value of Operand at the levels of Levels, a bit for each;
// the tracked variable or the object Variable at Level; or the data its own
// evaluation gives, Fresh, also Elsewhere (COrigin).
struct CFrom {
  enum class EKind { Operand, Variable, Fresh };
  EKind Kind = EKind::Operand;
  const Expr *Operand = nullptr;
  unsigned Levels = 0;
  unsigned Variable = 0;
  unsigned Level = 0;
  bool Elsewhere = false;
  // Of Variable, what it is reached by, of what a pointer designates, the
  // pointer and its name, else Variable and the expression.
  std::optional<unsigned> Key = std::nullopt;
  const Expr *Name = nullptr;
};

// What an element of the flow of a coroutine does to what the check tracks,
// worked out once from the effects of its statement (effectsOf) and the
// transitions of provenance (CThreadLocalData::step).
struct CStep {
  // How its value is made, at each level.
  llvm::SmallVector<CFrom, 2> From[NumLevels];
  // The operands whose data it uses, at the levels of Levels, a bit for each.
  struct CUse {
    const Expr *Operand;
    unsigned Levels;
  };
  llvm::SmallVector<CUse, 2> Uses;
  // What gives a tracked variable a value: the variable, the operand, and
  // whether only maybe, of an element of it or of one of several.
  struct CDefinition {
    unsigned Variable;
    const Expr *Operand;
    bool Element;
  };
  llvm::SmallVector<CDefinition, 1> Definitions;
  // The tracked variables it declares without a value.
  llvm::SmallVector<unsigned, 1> Resets;
  // The scope variables whose scopes start with it.
  llvm::SmallVector<unsigned, 1> ScopesStarted;
  // Whether it makes a temporary of a scope type.
  bool MakesScope = false;
  // Of a group of operands unsequenced, or indeterminately sequenced, to one
  // that suspends (COperandEffect::Sequence), each other operand and the one
  // that suspends.
  llvm::SmallVector<std::pair<const Stmt *, const Stmt *>, 1> Unordered;
  // Of a load of a tracked pointer, the pointer and the name it is loaded by.
  std::optional<std::pair<unsigned, const Expr *>> Load;
  // Of a definition in an operand that suspends, the loads of what it
  // defines in an operand unsequenced to it, which can read what it held
  // before, across the suspension.
  llvm::SmallVector<std::pair<unsigned, const Expr *>, 1> ReadsBefore;
  // Whether it suspends the coroutine.
  bool Suspends = false;
};

// What is evaluated in an operand of a group unsequenced to one that
// suspends: its statements, the uses in them and the variables they define.
struct CInside {
  llvm::SmallPtrSet<const Stmt *, 8> Statements;
  llvm::SmallVector<CStep::CUse, 2> Uses;
  llvm::SmallVector<unsigned, 1> Defined;
  llvm::SmallVector<std::pair<unsigned, const Expr *>, 1> Loads;
};

} // namespace

void CoroutineThreadLocalReferenceCheck::checkFunction(
    const FunctionDecl *Function, const LambdaExpr *Lambda,
    const CThreadLocalData &Data) {
  ASTContext &Context = Data.context();
  const SourceManager &SM = Context.getSourceManager();
  const Stmt *Body = Function->getBody();
  if (!Body)
    return;
  CFlowCache &Cache = sharedFlowCache();
  const CEffectIndex &Index = Cache.indexOf(Body, Context);

  // Whether a variable of \p Type can refer to the data: a pointer or a
  // reference, also an array of them, not a class that holds one, which the
  // storage check is for.
  auto Refers = [](QualType Type) {
    return forEachHeld(Type, [](const CHeld &Part) {
      switch (Part.Kind) {
      case EHeld::Pointer:
      case EHeld::Reference:
        return EVisit::Found;
      case EHeld::MemberPointer:
      case EHeld::BlockPointer:
      case EHeld::Record:
      case EHeld::Dependent:
      case EHeld::Opaque:
        return EVisit::Skip;
      }
      return EVisit::Skip;
    });
  };
  // Whether a value of \p Type is a scope of ThreadLocalScopeTypes or holds
  // one by value, also as an element, a member or a capture.
  auto HoldsScope = [&](QualType Type) {
    return forEachHeld(Type, [&](const CHeld &Part) {
      switch (Part.Kind) {
      // Not into a handler, whose scopes the runtime suspends and resumes
      // with the coroutine.
      case EHeld::Record:
        if (Data.isScopeType(Part.Type))
          return EVisit::Found;
        if (const auto *Class = dyn_cast_or_null<CXXRecordDecl>(Part.Record);
            Class && Data.isHandler(Class))
          return EVisit::Skip;
        return EVisit::Descend;
      case EHeld::Pointer:
      case EHeld::Reference:
      case EHeld::MemberPointer:
      case EHeld::BlockPointer:
      case EHeld::Dependent:
      case EHeld::Opaque:
        return EVisit::Skip;
      }
      return EVisit::Skip;
    });
  };

  // What can refer to the data: the locals, the parameters and the captures
  // that can, not a local reference to another local, which is that local
  // where it is used (boundLocal), and, of a member function of a
  // thread-local type, its object.
  llvm::SmallVector<const VarDecl *, 8> Variables;
  llvm::DenseMap<const VarDecl *, unsigned> Tracked;
  // Which refer to it from the start: parameters of a reference or a pointer
  // to a thread-local type, and captures of what does.
  // What each refers to from the start, by level, a bit for each.
  llvm::DenseMap<const VarDecl *, unsigned> FromStart;
  auto Add = [&](const VarDecl *Variable) {
    if (Tracked.try_emplace(Variable, Variables.size()).second)
      Variables.push_back(Variable);
  };
  // The copies of references the closure of the coroutine keeps, `[rpValue]`,
  // pointers of its own, not what they refer to.
  llvm::SmallPtrSet<const VarDecl *, 2> OwnCopies;
  // Whether \p Variable is what it is bound to: a reference, not one that
  // extends a temporary, its own storage (extendedSlot), nor a copy the
  // closure keeps of one.
  auto BindsReferent = [&](const VarDecl *Variable) {
    return Variable->getType()->isReferenceType() &&
           !extendedSlot(Variable) && !OwnCopies.contains(Variable);
  };
  // A local reference to another local, `int *&rpValue = pValue`.
  auto IsAlias = [&](const VarDecl *Variable) {
    const VarDecl *Bound = Variable->getType()->isReferenceType()
                               ? Data.boundLocal(Variable)
                               : nullptr;
    return Bound && Bound != Variable;
  };
  // The scopes that are variables: those that hold one, also a reference
  // that extends a temporary that does, and the copy of the object a lambda
  // captures, `[*this]`, that holds one.
  struct CScope {
    const VarDecl *Variable = nullptr;
    const LambdaExpr *ObjectCopy = nullptr;
  };
  llvm::SmallVector<CScope, 2> Scopes;
  llvm::DenseMap<const VarDecl *, unsigned> ScopeIndices;
  auto AddScope = [&](const VarDecl *Variable) {
    if (ScopeIndices.try_emplace(Variable, Scopes.size()).second)
      Scopes.push_back({Variable});
  };
  for (const ParmVarDecl *Parameter : Function->parameters()) {
    const QualType Type = Parameter->getType();
    if (Refers(Type)) {
      Add(Parameter);
      // Also of a pointer to such a pointer, at any level, `CData **`, and
      // through arrays, `CData *const (&_pData)[2]` or `CData (&_Data)[2]`.
      QualType Pointee = Type.getNonReferenceType();
      auto Elements = [](QualType Of) {
        while (const ArrayType *Array = Of->getAsArrayTypeUnsafe())
          Of = Array->getElementType();
        return Of;
      };
      // At the level it is reached at, one for each pointer, not the ones
      // before it, `*_ppData` of `CData **_ppData` is the caller's pointer.
      bool Typed = Type->isReferenceType() && Data.isType(Elements(Pointee));
      Pointee = Elements(Pointee);
      unsigned Level = 0;
      for (int Depth = 0; !Typed && Pointee->isPointerType() && Depth < 8;
           ++Depth) {
        Pointee = Elements(Pointee->getPointeeType());
        Typed = Data.isType(Pointee);
        ++Level;
      }
      if (Typed)
        FromStart[Parameter] |= 1u << std::min(Level, NumLevels - 1);
    } else if (HoldsScope(Type))
      AddScope(Parameter);
  }
  // Captures of what refers to it: a pointer or an array of them, copied or
  // by reference, a reference by reference, also one to a local pointer, a
  // copy of a reference to a pointer, which copies the pointer, an
  // initializer, and a structured binding, by what it binds; not a copy of
  // what a reference refers to otherwise, `[Data]`. A copy by what it is
  // given where the lambda is made (refersTo); a scope, also copied, which
  // the closure keeps alive with the coroutine. Of `this`, by reference, the
  // object; by copy, `[*this]`, a copy of it.
  bool CapturesThis = false;
  bool CopiesThis = false;
  llvm::SmallVector<const VarDecl *, 2> CapturedScopes;
  // The initializers are of the function the lambda is made in: `this` there
  // is of a copy of the object when a lambda around copies it, `[*this] {
  // [pValue = &m_Value] { ... }; }`.
  Data.setObjectCopied(Lambda && objectCopiedAround(Lambda));
  if (Lambda)
    for (auto [Capture, CaptureInit] :
         llvm::zip(Lambda->captures(), captureInitsOf(Lambda))) {
      switch (Capture.getCaptureKind()) {
      case LCK_This:
        CapturesThis = true;
        break;
      case LCK_StarThis:
        CopiesThis = true;
        break;
      case LCK_ByCopy:
      case LCK_ByRef:
      case LCK_VLAType:
        break;
      }
      // What it is (capturedBy): a structured binding as the variable it
      // decomposes, tracked, by its own type, a part of it; a reference
      // init capture as the local it is bound to.
      const CThreadLocalData::CCaptured Of =
          Data.capturedBy(Capture, CaptureInit);
      if (!Of.Variable)
        continue;
      if (Of.Copied && HoldsScope(Of.Type.getNonReferenceType())) {
        CapturedScopes.push_back(Of.Variable);
        continue;
      }
      // Each that can refer to it, also one given it later, `[pValue]()
      // mutable { pValue = &t_Value; ... }`; from the start what does.
      if (!Of.Refers)
        continue;
      if (Of.Copied && !Of.Binding && Of.Slot == Of.Variable &&
          Of.Variable->getType()->isReferenceType() &&
          !Of.Variable->isInitCapture())
        OwnCopies.insert(Of.Slot);
      Add(Of.Slot);
      // A pointer copied, also an init capture, at each level what it is
      // given is there, `[ppValue]` of `int **ppValue = &pValue`, `[ppValue =
      // &pValue]`; else at each level it can refer at.
      const bool CopiedPointer = Of.Copied && !Of.Binding &&
                                 Of.Type->isPointerType() && CaptureInit;
      unsigned Levels = 0;
      if (CopiedPointer) {
        for (unsigned Level = 1; Level < NumLevels; ++Level)
          if (Data.isDataAt(CaptureInit,
                            CaptureInit->isGLValue() ? Level : Level - 1))
            Levels |= 1u << Level;
      } else if (Of.Holds)
        for (unsigned Level = 0; Level < NumLevels; ++Level)
          if (Level > 0 || BindsReferent(Of.Slot))
            Levels |= 1u << Level;
      if (Levels)
        FromStart[Of.Slot] |= Levels;
    }
  // `this` of a copy of the object isn't thread-local data, also of one a
  // lambda this one is in makes, `[*this] { [this] { ... }; }`.
  const bool OwnsCopy = CopiesThis;
  if (CapturesThis && objectCopiedAround(Lambda)) {
    CapturesThis = false;
    CopiesThis = true;
  }
  Data.setObjectCopied(CopiesThis, Function);
  const llvm::scope_exit ObjectCopied([&] { Data.setObjectCopied(false); });
  for (const VarDecl *Scope : CapturedScopes)
    AddScope(Scope);
  // A copy of the object, `[*this]`, that holds one, by its class.
  if (const CXXRecordDecl *Class =
          OwnsCopy && Lambda ? enclosingThisClass(Lambda) : nullptr;
      Class && HoldsScope(Context.getCanonicalTagType(Class)))
    Scopes.push_back({nullptr, Lambda});
  // The locals, by what each declaration is (holderKind), the references
  // that extend a temporary that holds a scope, by the temporary, and the
  // loops over thread-local data, whose iterators refer to it.
  llvm::SmallVector<const CXXForRangeStmt *, 2> DataLoops;
  llvm::DenseMap<const VarDecl *, const CXXForRangeStmt *> Loops;
  for (const Stmt *S : Index.statements()) {
    const CEffects &Effects = *Index.effectsOf(S);
    for (const Decl *Declared : Effects.Declares)
      switch (holderKind(Declared)) {
      case EHolder::Variable:
      case EHolder::Decomposition: {
        const auto *Variable = cast<VarDecl>(Declared);
        if (!Variable->hasLocalStorage())
          break;
        if (Refers(Variable->getType()) && !IsAlias(Variable))
          Add(Variable);
        if (HoldsScope(Variable->getType()))
          AddScope(Variable);
        break;
      }
      case EHolder::None:
      case EHolder::Parameter:
      case EHolder::ImplicitParameter:
      case EHolder::Binding:
      case EHolder::Field:
      case EHolder::IndirectField:
      case EHolder::Temporary:
      case EHolder::Constant:
      case EHolder::Function:
      case EHolder::Statement:
      case EHolder::Opaque:
        break;
      }
    switch (Effects.Statement) {
    case EStatement::Temporary: {
      const auto *Materialize = cast<MaterializeTemporaryExpr>(S);
      if (const auto *Extending =
              dyn_cast_or_null<VarDecl>(Materialize->getExtendingDecl());
          Extending && Extending->hasLocalStorage() &&
          !Materialize->isTypeDependent() &&
          HoldsScope(Materialize->getType()))
        AddScope(Extending);
      break;
    }
    case EStatement::RangeLoop: {
      const auto *For = cast<CXXForRangeStmt>(S);
      if (For->getRangeInit() &&
          (Data.isData(For->getRangeInit()) ||
           (For->getRangeInit()->getType()->isPointerType() &&
            Data.pointsTo(For->getRangeInit()))))
        DataLoops.push_back(For);
      for (const DeclStmt *Declaration :
           {For->getRangeStmt(), For->getBeginStmt(), For->getEndStmt()})
        if (const auto *Variable =
                Declaration
                    ? dyn_cast_or_null<VarDecl>(Declaration->getSingleDecl())
                    : nullptr)
          Loops[Variable] = For;
      break;
    }
    case EStatement::Other:
    case EStatement::Name:
    case EStatement::This:
    case EStatement::Member:
    case EStatement::Call:
    case EStatement::Construction:
    case EStatement::Lambda:
    case EStatement::Declaration:
    case EStatement::CoroutineBody:
    case EStatement::New:
    case EStatement::Return:
    case EStatement::Coreturn:
    case EStatement::Default:
    case EStatement::InheritedConstruction:
    case EStatement::InitializerList:
      break;
    }
  }
  // The object of a member function of a thread-local type, also of one
  // whose lambda captures it by pointer, of the thread that called it.
  const CXXRecordDecl *ThisClass = nullptr;
  if (Lambda)
    ThisClass = CapturesThis ? enclosingThisClass(Lambda) : nullptr;
  else if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
           Method && Method->isInstance())
    ThisClass = Method->getParent();
  const bool TracksThis =
      ThisClass && Data.isType(Context.getCanonicalTagType(ThisClass));
  // The keys of what a value can refer to the data by (COrigin): the
  // variables, the object, then the expressions that give the data.
  const unsigned This = Variables.size();
  const unsigned nTracked = Variables.size() + (TracksThis ? 1 : 0);
  llvm::DenseMap<const Expr *, unsigned> RootKeys;
  llvm::SmallVector<const Expr *, 4> KeyRoots;
  auto KeyOf = [&](const Expr *Root) {
    auto [It, Inserted] =
        RootKeys.try_emplace(Root, nTracked + KeyRoots.size());
    if (Inserted)
      KeyRoots.push_back(Root);
    return It->second;
  };

  // The tracked variable \p E names, also a name of a structured binding of
  // one, the object as `this`, and a local reference to one.
  auto TrackedNamed = [&](const Expr *E) -> std::optional<unsigned> {
    switch (effectsOf(E, Context).Statement) {
    case EStatement::This:
      return TracksThis ? std::optional<unsigned>(This) : std::nullopt;
    case EStatement::Name:
      break;
    case EStatement::Other:
    case EStatement::Member:
    case EStatement::Call:
    case EStatement::Construction:
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
      return std::nullopt;
    }
    const ValueDecl *Named = cast<DeclRefExpr>(E)->getDecl();
    if (const auto *Binding = dyn_cast<BindingDecl>(Named))
      Named = Binding->getDecomposedDecl();
    const auto *Variable = dyn_cast_or_null<VarDecl>(Named);
    if (!Variable)
      return std::nullopt;
    // Not a copy another closure keeps (closureCopying), which is that
    // closure's, made where it is.
    if (const LambdaExpr *Copying = closureCopying(Variable, E);
        Copying && Copying->getCallOperator() != Function)
      return std::nullopt;
    if (auto It = Tracked.find(Variable); It != Tracked.end())
      return It->second;
    if (const VarDecl *Bound = Variable->getType()->isReferenceType()
                                   ? Data.boundLocal(Variable)
                                   : nullptr)
      if (auto It = Tracked.find(Bound); It != Tracked.end())
        return It->second;
    return std::nullopt;
  };
  // Whether \p Inner is in \p Outer.
  // Also of what is evaluated in several places, a default argument at each
  // of its uses.
  auto Within = [&](const Stmt *Inner, const Stmt *Outer) {
    llvm::SmallVector<const Stmt *, 4> Work{Inner};
    llvm::SmallPtrSet<const Stmt *, 16> Seen;
    for (int Steps = 0; !Work.empty() && Steps < 4096; ++Steps) {
      const Stmt *Current = Work.pop_back_val();
      if (Current == Outer)
        return true;
      if (!Current || !Seen.insert(Current).second)
        continue;
      for (const CEffectIndex::CUse &Use : Index.usesOf(Current))
        Work.push_back(Use.Parent);
    }
    return false;
  };
  // The tracked variable, not a reference, whose storage \p E designates, by
  // its provenance (reach): itself, also through a local reference or pointer
  // to it, `*ppValue`, or only an element or a part of it.
  struct CDesignated {
    unsigned Variable;
    bool Whole;
    // The name it is designated by.
    const DeclRefExpr *Name;
    // The other variables it can designate instead, `*(bFirst ? &pFirst :
    // &pSecond)`, each of which a value given to it only may be given, and
    // whether it can designate what isn't tracked instead.
    llvm::SmallVector<unsigned, 1> Also;
    bool Weak = false;
  };
  auto Designated = [&](const Expr *E) -> std::optional<CDesignated> {
    // The storage of a tracked variable that isn't a reference it designates
    // (reach), at level 0, the whole of it or a part, of each it can.
    std::optional<CDesignated> Found;
    bool Ended = false;
    Data.reach(E, 0, [&](const Expr *Of, unsigned Level, bool Part) {
      const std::optional<unsigned> Index = TrackedNamed(Of);
      if (!Index || *Index == This ||
          BindsReferent(Variables[*Index]))
        return EReach::Continue;
      // Above level 0, what is stored in it designates, `*ppValue`.
      if (Level != 0)
        return EReach::Continue;
      if (!Found) {
        const auto *Name = effectsOf(Of, Context).Statement == EStatement::Name
                               ? cast<DeclRefExpr>(Of)
                               : nullptr;
        Found = CDesignated{*Index, !Part, Name, {}, false};
      } else if (Found->Variable != *Index &&
                 !llvm::is_contained(Found->Also, *Index))
        Found->Also.push_back(*Index);
      else
        Found->Whole &= !Part;
      return EReach::Root;
    }, &Ended);
    if (Found)
      Found->Weak = Ended;
    return Found;
  };
  // Whether \p E gives the data by itself, what the thread that evaluates
  // it has.
  auto IsRoot = [&](const Expr *E) {
    return isThreadLocalData(Context, E, Lists.Functions) || Data.isRoot(E);
  };
  // The temporaries of a scope type: what is made of one, the outermost of
  // what gives it on, alive where it is made.
  auto MakesScope = [&](const Stmt *S) {
    const auto *Made = dyn_cast<Expr>(S);
    return Made && Made->isPRValue() && Made->getType()->isRecordType() &&
           !Made->isTypeDependent() &&
           llvm::none_of(Index.usesOf(Made),
                         [](const CEffectIndex::CUse &Use) {
                           return Use.Effect->Use == EUse::Forward;
                         }) &&
           HoldsScope(Made->getType());
  };
  bool Any = !Variables.empty() || !Scopes.empty() || !DataLoops.empty() ||
             TracksThis;
  for (const Stmt *S : Index.statements()) {
    if (Any)
      break;
    if (const auto *E = dyn_cast<Expr>(S);
        E && !E->isTypeDependent() && !E->isValueDependent() &&
        (IsRoot(E) || MakesScope(E)))
      Any = true;
  }
  if (!Any)
    return;
  const CFG *Graph = Cache.graphOf(Function, Body, Context);
  if (!Graph)
    return;
  const llvm::BitVector &Reachable =
      Cache.reachableOf(Function, Body, Context);
  const auto SuspendPoints = suspendPoints(Body);
  const unsigned nScopes = Scopes.size();

  // The statements of the CFG, as elements of its own, and the declarations
  // it makes for each variable of a declaration of several.
  llvm::DenseSet<const Stmt *> InGraph;
  for (const CFGBlock *Block : *Graph)
    for (const CFGElement &Element : *Block)
      if (const CFlowEvent Event = eventOf(Element);
          Event.Kind == EEvent::Statement && Event.Statement)
        InGraph.insert(Event.Statement);
  llvm::DenseMap<const Stmt *, const Stmt *> Synthetic;
  for (const auto &[Made, Original] : Graph->synthetic_stmts())
    Synthetic[Made] = Original;
  // What a default argument or a default member initializer evaluates
  // where it is used, which the CFG doesn't have (evaluationOf).
  llvm::DenseMap<const Stmt *, llvm::SmallVector<const Stmt *, 8>> Defaults;
  for (const Stmt *S : Index.statements())
    if (Index.effectsOf(S)->Statement == EStatement::Default)
      for (const Stmt *Evaluated : Index.evaluationOf(S))
        if (!InGraph.contains(Evaluated))
          Defaults[S].push_back(Evaluated);
  for (const auto &[Default, Evaluated] : Defaults)
    InGraph.insert_range(Evaluated);
  // The full expression each statement is in, the outermost expression, or
  // the statement itself, and of each, the full expression of the statement
  // expression it is in, `({ ...; })`, whose values outlive it.
  llvm::DenseMap<const Stmt *, const Stmt *> RootOf;
  llvm::DenseMap<const Stmt *, const Stmt *> OuterOf;
  for (const Stmt *S : Index.statements()) {
    const Stmt *Root = RootOf.try_emplace(S, S).first->second;
    const Stmt *Outer = OuterOf.lookup(S);
    for (const COperandEffect &Effect : Index.effectsOf(S)->Operands) {
      const bool InExpression = isa<Expr>(S) && isa<Expr>(Effect.Operand);
      if (RootOf.try_emplace(Effect.Operand, InExpression ? Root
                                                          : Effect.Operand)
              .second)
        OuterOf[Effect.Operand] =
            isa<Expr>(S) && !InExpression ? Root : Outer;
    }
  }
  // Whether the values of the full expression \p Root are kept where \p At,
  // the end of a full expression or a statement, is: \p Root is that of a
  // statement expression \p At is in.
  auto Outlives = [&](const Stmt *Root, const Stmt *At) {
    if (const auto It = Synthetic.find(At); It != Synthetic.end())
      At = It->second;
    for (const Stmt *Outer = OuterOf.lookup(At); Outer;
         Outer = OuterOf.lookup(Outer))
      if (Outer == Root)
        return true;
    return false;
  };
  // Whether \p S suspends, or has an operand that does, that runs.
  llvm::DenseMap<const Stmt *, bool> SuspendsIn;
  TCRecursive<bool(const Stmt *)> HasSuspension = [&](const Stmt *S) -> bool {
    if (!S)
      return false;
    if (auto It = SuspendsIn.find(S); It != SuspendsIn.end())
      return It->second;
    SuspendsIn[S] = false;
    bool Suspends = isSuspension(S) || SuspendPoints.contains(S);
    if (const CEffects *Of = Index.effectsOf(S))
      for (const COperandEffect &Effect : Of->Operands)
        if (!Suspends && Effect.Use != EUse::Unevaluated)
          Suspends = HasSuspension(Effect.Operand);
    SuspendsIn[S] = Suspends;
    return Suspends;
  };

  // What the closures made here copy where they are made, `[pValue]`, which
  // what one assigns where it runs is made of (closureWritesAt), kept while
  // the function runs.
  llvm::DenseSet<const Expr *> CaptureInits;
  for (const Stmt *S : Index.statements())
    if (Index.effectsOf(S)->Statement == EStatement::Lambda)
      for (const COperandEffect &Effect : Index.effectsOf(S)->Operands)
        if (const auto *Init = dyn_cast<Expr>(Effect.Operand);
            Init && Effect.Use == EUse::Copy &&
            Effect.Into.Kind == EDestination::Capture)
          CaptureInits.insert(Init);

  // The levels of what is reached at \p Next from an expression at \p Level
  // (step), a bit for each: the last level stands for each above it too.
  auto LevelsOf = [](unsigned Level, unsigned Next) -> unsigned {
    constexpr unsigned Last = NumLevels - 1;
    if (Level < Last || Next >= Last)
      return 1u << std::min(Next, Last);
    return ((1u << NumLevels) - 1) & ~((1u << Next) - 1);
  };
  // How \p E is made of what isn't its operand, what the slot it names holds
  // (localSlotValues) and what a reference is bound to, at \p Level, as
  // provenance tells (reach): the tracked variables there, what is evaluated
  // in \p E, and what gives the data, which the thread that evaluates \p E
  // has.
  // What AddElsewhere's walk for \p E does at \p At, at \p AtLevel: what it
  // adds to Into, and whether it goes on; without \p E, of a walk that isn't
  // in the expression it is for.
  auto VisitElsewhere = [&](const Expr *E, const Expr *At, unsigned AtLevel,
                            llvm::SmallVectorImpl<CFrom> &Into) {
    const unsigned Saturated = std::min(AtLevel, NumLevels - 1);
    if (CaptureInits.contains(At)) {
      Into.push_back({CFrom::EKind::Operand, At, 1u << Saturated});
      return EReach::Root;
    }
    if (const std::optional<unsigned> Named = TrackedNamed(At)) {
      if (*Named == This) {
        if (AtLevel == 0)
          Into.push_back({CFrom::EKind::Variable, nullptr, 0, This, 0});
        return EReach::Root;
      }
      if (AtLevel > 0 || BindsReferent(Variables[*Named]))
        Into.push_back({CFrom::EKind::Variable, nullptr, 0, *Named, Saturated});
      return EReach::Root;
    }
    if (E && At != E && Within(At, E)) {
      Into.push_back({CFrom::EKind::Operand, At, 1u << Saturated});
      return EReach::Root;
    }
    const EReach Reach = Data.rootAt(At, AtLevel);
    if (Reach == EReach::Root)
      Into.push_back({CFrom::EKind::Fresh, nullptr, 0, 0, 0, true});
    return Reach;
  };
  // The walks of AddElsewhere, shared by the statements of the body
  // (CReachClosures): what each adds, by an index of Added, each once.
  CReachClosures Elsewhere(Data.provenance());
  llvm::SmallVector<CFrom, 8> Added;
  std::map<std::tuple<CFrom::EKind, const Expr *, unsigned, unsigned, unsigned,
                      bool, std::optional<unsigned>, const Expr *>,
           unsigned>
      AddedIndex;
  // The expressions a walk can visit that each statement is in (Within), of
  // the ways visited so far: a walk for one of them is reached in it, which
  // the shared walks don't tell.
  llvm::DenseSet<const Stmt *> Around;
  auto NoteAround = [&](const Expr *At) {
    // The statements Within passes from At, by its walk, but At itself.
    llvm::SmallVector<const Stmt *, 4> Work{At};
    llvm::SmallPtrSet<const Stmt *, 16> Seen;
    for (int Steps = 0; !Work.empty() && Steps < 4096; ++Steps) {
      const Stmt *Current = Work.pop_back_val();
      if (!Current || !Seen.insert(Current).second)
        continue;
      if (Current != At)
        Around.insert(Current);
      for (const CEffectIndex::CUse &Use : Index.usesOf(Current))
        Work.push_back(Use.Parent);
    }
  };
  // The index of \p Add in Added, added once.
  auto AddedId = [&](const CFrom &Add) {
    const auto [It, New] = AddedIndex.try_emplace(
        {Add.Kind, Add.Operand, Add.Levels, Add.Variable, Add.Level,
         Add.Elsewhere, Add.Key, Add.Name},
        Added.size());
    if (New)
      Added.push_back(Add);
    return It->second;
  };
  // With \p Have, the records of Added already in Into, which aren't added
  // again: Evaluate merges them as sets.
  auto AddElsewhere = [&](const Expr *E, const Expr *Next, unsigned Level,
                          llvm::SmallVectorImpl<CFrom> &Into,
                          llvm::BitVector *Have = nullptr) {
    auto AddOnce = [&](unsigned iAdded) {
      if (Have) {
        if (Have->size() <= iAdded)
          Have->resize(Added.size());
        if (Have->test(iAdded))
          return;
        Have->set(iAdded);
      }
      Into.push_back(Added[iAdded]);
    };
    if (!Around.contains(E))
      if (const llvm::SmallVectorImpl<unsigned> *Records = Elsewhere.closure(
              Next, Level,
              [&](const Expr *At, unsigned AtLevel,
                  llvm::SmallVectorImpl<unsigned> &Ids) {
                llvm::SmallVector<CFrom, 2> Adds;
                const EReach Reach = VisitElsewhere(nullptr, At, AtLevel, Adds);
                for (const CFrom &Add : Adds)
                  Ids.push_back(AddedId(Add));
                return Reach;
              },
              NoteAround);
          Records && !Around.contains(E)) {
        for (unsigned iAdded : *Records)
          AddOnce(iAdded);
        return;
      }
    llvm::SmallVector<CFrom, 4> Adds;
    Data.reach(Next, Level, [&](const Expr *At, unsigned AtLevel, bool) {
      return VisitElsewhere(E, At, AtLevel, Adds);
    });
    for (const CFrom &Add : Adds)
      AddOnce(AddedId(Add));
  };

  // What each statement does, once, by its effects, also of a declaration
  // the CFG makes for each variable of one of several.
  llvm::DenseMap<const Stmt *, CStep> Steps;
  // What \p Writes, of local closures, give tracked variables, the
  // definitions of \p Step, only maybe unless one is assigned each time the
  // closure runs there (CClosureWrite::Definite), by what each value is where
  // the closure runs (closureWritesAt).
  auto ClosureDefinitions = [&](llvm::ArrayRef<CClosureWrite> Writes,
                                CStep &Step) {
    for (const CClosureWrite &Write : Writes) {
      const auto It = Tracked.find(Write.Slot.Variable);
      const Expr *Value = Write.Value.Value;
      if (It == Tracked.end() || !Value ||
          Write.Value.Kind != CSlotValue::EKind::Value)
        continue;
      if (!Steps.count(Value)) {
        CStep Written;
        for (unsigned Level = 0; Level < NumLevels; ++Level)
          AddElsewhere(Value, Value, Level, Written.From[Level]);
        Steps[Value] = std::move(Written);
      }
      Step.Definitions.push_back({It->second, Value, !Write.Definite});
    }
  };
  auto MakeStep = [&](const Stmt *S, const CEffects &Effects) {
    CStep Step;
    // The operands of the statement, looked up for each value its operands
    // step to, which an initializer list of N elements has N of.
    llvm::SmallPtrSet<const Stmt *, 8> OperandSet;
    for (const COperandEffect &Effect : Effects.Operands)
      if (Effect.Operand)
        OperandSet.insert(Effect.Operand);
    const auto *E = dyn_cast<Expr>(S);
    if (E && (E->isTypeDependent() || E->isValueDependent()))
      E = nullptr;
    // Its value: of a tracked variable or the object, what the flow has of
    // it; of what gives the data, that, as rootAt tells; else what its
    // operands are, by the transitions of provenance (step).
    if (E) {
      // Of each variable \p Of can designate, above level 0, what it holds
      // where it is read, reached by \p Key, named \p Name.
      auto ReadCurrent = [&](const CDesignated &Of, std::optional<unsigned> Key,
                             const Expr *Name) {
        llvm::SmallVector<unsigned, 2> Read{Of.Variable};
        llvm::append_range(Read, Of.Also);
        for (unsigned Variable : Read)
          if (Variable != This && (!Key || Variable != *Key))
            for (unsigned Level = 1; Level < NumLevels; ++Level) {
              CFrom Current{CFrom::EKind::Variable, nullptr, 0, Variable, Level};
              Current.Key = Key;
              Current.Name = Name;
              Step.From[Level].push_back(Current);
            }
      };
      if (const std::optional<unsigned> Named = TrackedNamed(E)) {
        if (*Named == This)
          Step.From[0].push_back({CFrom::EKind::Variable, nullptr, 0, This, 0});
        else
          for (unsigned Level = 0; Level < NumLevels; ++Level)
            if (Level > 0 ||
                BindsReferent(Variables[*Named]))
              Step.From[Level].push_back(
                  {CFrom::EKind::Variable, nullptr, 0, *Named, Level});
        // A reference to a part of another, `auto &rpSlot = pValues[0]`, also
        // what that holds where it is read.
        if (*Named != This && BindsReferent(Variables[*Named]))
          if (const std::optional<CDesignated> Of = Designated(E))
            ReadCurrent(*Of, *Named, E);
      } else {
        // What a pointer designates that is a tracked variable, the whole of
        // it, `*ppValue`, above level 0 what that variable holds where it is
        // read, not what it held where the pointer was made to point to it.
        // Reached by the tracked pointer it is reached through, by its
        // name, `ppValue` of `**ppValue`.
        // Of a part of one or of several, also what each holds where it is
        // read, along with what the pointer was made to point to.
        std::optional<unsigned> Through;
        std::optional<unsigned> Pointer;
        const Expr *PointerName = nullptr;
        const COperandEffect *Followed = nullptr;
        for (const COperandEffect &Effect : Effects.Operands)
          if (Effect.Use == EUse::Dereference)
            Followed = &Effect;
        const std::optional<CDesignated> Designation =
            E->isGLValue() && Followed ? Designated(E) : std::nullopt;
        const bool Exact = Designation && Designation->Whole &&
                           Designation->Also.empty() && !Designation->Weak;
        if (Designation && Designation->Variable != This) {
          for (const Expr *Inner = dyn_cast<Expr>(Followed->Operand);
               Inner && !Pointer;) {
            if ((Pointer = TrackedNamed(Inner)))
              PointerName = Inner;
            const CEffects &InnerOf = effectsOf(Inner, Context);
            Inner = InnerOf.Operands.size() == 1 &&
                            (InnerOf.Operands.front().Use == EUse::Forward ||
                             InnerOf.Operands.front().Use == EUse::Access)
                        ? dyn_cast<Expr>(InnerOf.Operands.front().Operand)
                        : nullptr;
          }
          if (Pointer == This)
            Pointer = std::nullopt;
          if (Exact)
            Through = Designation->Variable;
          else
            ReadCurrent(*Designation, Pointer, Pointer ? PointerName : nullptr);
        }
        for (unsigned Level = 0; Level < NumLevels; ++Level) {
          if (Through && Level > 0) {
            CFrom Read{CFrom::EKind::Variable, nullptr, 0, *Through, Level};
            Read.Key = Pointer;
            Read.Name = Pointer ? PointerName : nullptr;
            Step.From[Level].push_back(Read);
            continue;
          }
          const EReach Reach = effectsOf(E, Context).Statement ==
                                       EStatement::This
                                   ? EReach::Continue
                                   : Data.rootAt(E, Level);
          if (Reach == EReach::Root) {
            KeyOf(E);
            Step.From[Level].push_back({CFrom::EKind::Fresh});
          }
          if (Reach != EReach::Continue)
            continue;
          bool Complete = true;
          llvm::BitVector Have;
          Data.step(
              E, Level, false,
              [&](const Expr *Next, unsigned NextLevel, bool) {
                if (!Next)
                  return;
                const unsigned Levels = LevelsOf(Level, NextLevel);
                if (OperandSet.contains(Next)) {
                  Step.From[Level].push_back(
                      {CFrom::EKind::Operand, Next, Levels});
                  return;
                }
                AddElsewhere(E, Next, NextLevel, Step.From[Level], &Have);
                for (unsigned Each = NextLevel + 1; Each < NumLevels; ++Each)
                  if (Levels & (1u << Each))
                    AddElsewhere(E, Next, Each, Step.From[Level], &Have);
              },
              Complete);
        }
      }
    }
    for (const COperandEffect &Effect : Effects.Operands) {
      const auto *Operand = dyn_cast<Expr>(Effect.Operand);
      if (!Operand || Operand->isTypeDependent())
        continue;
      // Whether the effect uses what the operand refers to, or only gives it
      // on, or gives a tracked variable a value, which the flow follows.
      bool Use = true;
      switch (Effect.Use) {
      case EUse::Forward:
      case EUse::Unevaluated:
      case EUse::Statement:
      case EUse::AddressOf:
      case EUse::Discarded:
        Use = false;
        break;
      // What a pointer is followed to, a glvalue, is used where that is,
      // `Data[Index]` where it is read, not where its address is worked out;
      // what `delete` or an atomic operation follows it to here.
      case EUse::Dereference:
        Use = !(isa<Expr>(S) && cast<Expr>(S)->isGLValue());
        break;
      // Of a tracked pointer itself: what is assigned to it, or what is
      // loaded, which the flow follows; not one stepped, `++pValue`
      // (EUse::Update), which uses it.
      case EUse::Write:
        Use = !Designated(Operand);
        break;
      case EUse::Access:
        if (Operand->getType()->isAnyPointerType())
          if (const std::optional<CDesignated> Of = Designated(Operand);
              Of && Of->Whole && Of->Also.empty() && !Of->Weak) {
            Use = false;
            Step.Load = {Of->Variable, Of->Name ? Of->Name : Operand};
          }
        break;
      case EUse::Bind:
      case EUse::Copy: {
        const CDestination &Into = Effect.Into;
        // The variables given it, each only maybe when there are several,
        // and whether as an element.
        llvm::SmallVector<unsigned, 1> Targets;
        bool Element = false;
        switch (Into.Kind) {
        case EDestination::Variable:
          if (const auto *Variable =
                  dyn_cast_or_null<VarDecl>(Into.Declaration)) {
            if (auto It = Tracked.find(Variable); It != Tracked.end())
              Targets.push_back(It->second);
            else if (IsAlias(Variable))
              Use = false;
          }
          break;
        case EDestination::Designated:
          if (const std::optional<CDesignated> Of =
                  Into.Object ? Designated(Into.Object) : std::nullopt) {
            Targets.push_back(Of->Variable);
            llvm::append_range(Targets, Of->Also);
            Element = !Of->Whole || !Of->Also.empty() || Of->Weak;
          }
          break;
        // What an exchange gives the first of what it is given, its second,
        // `fg_Exchange(pThreadLocal, nullptr)` (ExchangeFunctions); what it
        // gives back is what that held, by the summary of its body.
        case EDestination::Parameter:
          if (const Expr *Given = argumentFor(S, 1, Context);
              Given && Into.Index == 0 && Into.Callee &&
              Into.Callee->getIdentifier() &&
              isExchangeFunction(identifierTextOf(Into.Callee)) &&
              Operand->isGLValue())
            if (const std::optional<CDesignated> Of = Designated(Operand)) {
              Targets.push_back(Of->Variable);
              llvm::append_range(Targets, Of->Also);
              Element = !Of->Whole || !Of->Also.empty() || Of->Weak;
              for (unsigned Target : Targets)
                Step.Definitions.push_back({Target, Given, Element});
              Targets.clear();
            }
          break;
        case EDestination::None:
        case EDestination::Field:
        case EDestination::Capture:
        case EDestination::Element:
        case EDestination::Base:
        case EDestination::Return:
        case EDestination::Heap:
        case EDestination::Temporary:
        case EDestination::Thrown:
          break;
        }
        for (unsigned Target : Targets)
          Step.Definitions.push_back({Target, Operand, Element});
        if (!Targets.empty())
          Use = false;
        break;
      }
      case EUse::Update:
      case EUse::CallObject:
      case EUse::Callee:
      case EUse::Condition:
      case EUse::Value:
      case EUse::Opaque:
        break;
      }
      // Not a value of its own, a number, whose reads were where it was
      // read, on its own path, `bChoose ? *pValue : co_await f_Other()`.
      if (!Use ||
          (Operand->isPRValue() && !Operand->getType()->isAnyPointerType() &&
           !Operand->getType()->isRecordType() &&
           !Operand->getType()->isReferenceType()))
        continue;
      // What it refers to; of a pointer as a glvalue, read, given by
      // reference or stepped, also the value stored in it; not of one a
      // closure captures by reference, which only names its slot, as its
      // address does.
      const bool Captured = Effect.Use == EUse::Bind &&
                            Effect.Into.Kind == EDestination::Capture;
      Step.Uses.push_back(
          {Operand, 1u | (Effect.Use != EUse::Write && !Captured &&
                                  Operand->isGLValue() &&
                                  Operand->getType()->isAnyPointerType()
                              ? 2u
                              : 0u)});
    }
    // What the local closures a call can run assign to tracked variables,
    // only maybe, by what each value is where the closure runs, there
    // (closureWritesAt).
    ClosureDefinitions(closureWritesAt(Function, S, Context), Step);
    for (const Decl *Declared : Effects.Declares)
      if (const auto *Variable = dyn_cast<VarDecl>(Declared)) {
        if (auto It = Tracked.find(Variable);
            It != Tracked.end() && !Variable->getInit())
          Step.Resets.push_back(It->second);
        if (auto It = ScopeIndices.find(Variable); It != ScopeIndices.end())
          Step.ScopesStarted.push_back(It->second);
      }
    Step.MakesScope = MakesScope(S);
    if (isa<Expr>(S)) {
      // The operands that run, each once, by their sequence.
      llvm::SmallVector<std::pair<unsigned, const Stmt *>, 4> Operands;
      llvm::SmallPtrSet<const Stmt *, 8> Listed;
      for (const COperandEffect &Effect : Effects.Operands)
        if (Effect.Use != EUse::Unevaluated && Effect.Operand &&
            Listed.insert(Effect.Operand).second)
          Operands.push_back({Effect.Sequence, Effect.Operand});
      // Of a group of operands unsequenced or indeterminately sequenced to
      // each other, one that suspends can run before or after each of the
      // others. Not of what a condition decides, whose operands after it are
      // each evaluated on a path of its own, the values of a conditional.
      const bool Decided =
          llvm::any_of(Effects.Operands, [](const COperandEffect &Effect) {
            return Effect.Use == EUse::Condition;
          });
      for (const auto &[Sequence, Suspending] : Operands) {
        if (Decided || !HasSuspension(Suspending))
          continue;
        // Not one in another, the same evaluation, like the object of a
        // member function in what is called.
        for (const auto &[OtherSequence, Other] : Operands)
          if (Other != Suspending && OtherSequence == Sequence &&
              !Within(Other, Suspending) && !Within(Suspending, Other))
            Step.Unordered.push_back({Other, Suspending});
      }
    }
    Step.Suspends = isSuspension(S) || SuspendPoints.contains(S);
    return Step;
  };
  // Not what isn't evaluated, `__builtin_constant_p(Value)`.
  for (const Stmt *S : Index.statements())
    if (Index.evaluated(S))
      Steps[S] = MakeStep(S, *Index.effectsOf(S));
  for (const auto &[Made, Original] : Graph->synthetic_stmts())
    Steps[Made] = MakeStep(Made, effectsOf(Made, Context));
  // What the closures a local object runs where it ends give there, a
  // guard of a scope exit (closureWritesAtEnd).
  llvm::DenseMap<const VarDecl *, CStep> EndSteps;
  for (const CFGBlock *Block : *Graph)
    for (const CFGElement &Element : *Block) {
      const CFlowEvent Event = eventOf(Element);
      if ((Event.Kind == EEvent::LifetimeEnds ||
           Event.Kind == EEvent::AutomaticDestructor) &&
          Event.Variable && !EndSteps.count(Event.Variable)) {
        CStep Ending;
        ClosureDefinitions(
            closureWritesAtEnd(Function, Event.Variable, Context), Ending);
        if (!Ending.Definitions.empty())
          EndSteps[Event.Variable] = std::move(Ending);
      }
    }
  // What is evaluated in each operand of a group unsequenced to one that
  // suspends, and, of each definition in the one that suspends, the loads
  // in the other of what it defines.
  llvm::DenseMap<const Stmt *, CInside> Insides;
  auto InsideOf = [&](const Stmt *Operand) -> const CInside & {
    if (const auto It = Insides.find(Operand); It != Insides.end())
      return It->second;
    CInside Inside;
    llvm::SmallVector<const Stmt *, 8> Work{Operand};
    while (!Work.empty()) {
      const Stmt *S = Work.pop_back_val();
      if (!S || !Inside.Statements.insert(S).second)
        continue;
      if (const auto It = Steps.find(S); It != Steps.end()) {
        llvm::append_range(Inside.Uses, It->second.Uses);
        for (const CStep::CDefinition &Definition : It->second.Definitions)
          Inside.Defined.push_back(Definition.Variable);
        if (It->second.Load)
          Inside.Loads.push_back(*It->second.Load);
      }
      if (const CEffects *Of = Index.effectsOf(S))
        for (const COperandEffect &Effect : Of->Operands)
          if (Effect.Use != EUse::Unevaluated)
            Work.push_back(Effect.Operand);
    }
    return Insides[Operand] = std::move(Inside);
  };
  for (auto &[S, Step] : Steps)
    for (const auto &[Other, Suspending] : Step.Unordered) {
      const auto Loads = InsideOf(Other).Loads;
      for (const Stmt *Inner : InsideOf(Suspending).Statements) {
        const auto It = Steps.find(Inner);
        if (It == Steps.end())
          continue;
        for (const CStep::CDefinition &Definition : It->second.Definitions)
          for (const auto &[Variable, Name] : Loads)
            if (Variable == Definition.Variable)
              It->second.ReadsBefore.push_back({Variable, Name});
      }
    }
  // Where the temporaries end, the elements of what each is given on to,
  // where it is destroyed or cleaned up (EEvent::TemporaryDestructor,
  // EEvent::FullExpressionCleanup).
  llvm::DenseMap<const Stmt *, llvm::SmallVector<const Expr *, 1>> EndsOf;
  for (const auto &[S, Step] : Steps) {
    const auto *Made = dyn_cast<Expr>(S);
    if (!Step.MakesScope || !Made)
      continue;
    for (const CEffectIndex::CUse &Use : Index.usesOf(Made))
      if (Index.effectsOf(Use.Parent)->Statement == EStatement::Temporary)
        EndsOf[Use.Parent].push_back(Made);
    for (const Stmt *Bound = Made; Bound;) {
      EndsOf[Bound].push_back(Made);
      const CEffects *Of = Index.effectsOf(Bound);
      Bound = Of && Of->Operands.size() == 1 &&
                      Of->Operands.front().Use == EUse::Forward
                  ? Of->Operands.front().Operand
                  : nullptr;
    }
  }

  auto Empty = [&] {
    CState State;
    for (unsigned Level = 0; Level < NumLevels; ++Level) {
      State.Holds[Level].resize(nTracked);
      State.Stale[Level].resize(nTracked);
    }
    State.Alive.resize(nScopes);
    return State;
  };
  // Whether it suspends before its body runs, by its type
  // (coroutineStartsWhereCalled).
  const bool LazyStart =
      Index.effectsOf(Body)->Statement == EStatement::CoroutineBody &&
      !coroutineStartsWhereCalled(Function);
  // The parameters, the captures and the object refer to it from the start,
  // of the thread that called it or made it, a reference by what it refers
  // to; the scopes it is given are alive.
  CState Start = Empty();
  for (unsigned iVariable = 0; iVariable < Variables.size(); ++iVariable)
    for (unsigned Level = 0; Level < NumLevels; ++Level)
      if (FromStart.lookup(Variables[iVariable]) & (1u << Level))
        Start.Holds[Level].set(iVariable);
  if (TracksThis)
    Start.Holds[0].set(This);
  if (LazyStart)
    for (unsigned Level = 0; Level < NumLevels; ++Level)
      Start.Stale[Level] = Start.Holds[Level];
  // Scopes given or captured, alive from the start, also across the initial
  // suspension of a promise that suspends before the body runs.
  llvm::DenseSet<unsigned> ScopesAlive;
  llvm::DenseSet<CTemporary> TemporariesAlive;
  for (unsigned iScope = 0; iScope < nScopes; ++iScope)
    if (Scopes[iScope].ObjectCopy ||
        (Scopes[iScope].Variable &&
         (isa<ParmVarDecl>(Scopes[iScope].Variable) ||
          llvm::is_contained(CapturedScopes, Scopes[iScope].Variable)))) {
      Start.Alive.set(iScope);
      if (LazyStart)
        ScopesAlive.insert(iScope);
    }

  // The value \p E has in \p State: what it was evaluated to, or, of what
  // the CFG doesn't have as an element, what its operands are there.
  TCRecursive<CValue(const CState &, const Expr *, int)> ValueOf;
  auto Evaluate = [&](const CStep &Step, const Expr *E, const CState &State,
                      int Depth) {
    CValue Value;
    for (unsigned Level = 0; Level < NumLevels; ++Level)
      for (const CFrom &From : Step.From[Level])
        switch (From.Kind) {
        case CFrom::EKind::Operand: {
          const CValue Of = ValueOf(State, From.Operand, Depth + 1);
          for (unsigned Each = 0; Each < NumLevels; ++Each)
            if (From.Levels & (1u << Each))
              addOrigins(Value.Levels[Level], Of.Levels[Each]);
          break;
        }
        case CFrom::EKind::Variable:
          if (State.Holds[From.Level].test(From.Variable))
            addOrigins(Value.Levels[Level],
                       COrigin{From.Key.value_or(From.Variable),
                               From.Name ? From.Name : E,
                               State.Stale[From.Level].test(From.Variable)});
          break;
        case CFrom::EKind::Fresh:
          addOrigins(Value.Levels[Level],
                     COrigin{KeyOf(E), E, false, From.Elsewhere});
          break;
        }
    return Value;
  };
  ValueOf = [&](const CState &State, const Expr *E, int Depth) -> CValue {
    if (const auto It = State.Values.find(E); It != State.Values.end())
      return It->second;
    if (InGraph.contains(E) || Depth > 32)
      return {};
    const auto It = Steps.find(E);
    return It == Steps.end() ? CValue() : Evaluate(It->second, E, State, Depth);
  };

  // What \p Element does to \p State; \p OnUse of each use of what is
  // stale, and \p OnScope of each scope alive at a suspension, a variable or
  // a temporary.
  using FOnUse = llvm::function_ref<void(const COrigin &)>;
  using FOnScope =
      llvm::function_ref<void(std::optional<unsigned>, const CTemporary *)>;
  // Whether \p Root defines \p Variable as a whole each time it is
  // evaluated: by itself, or in an operand evaluated each time; of what has
  // conditions, only the first, `_bGet || (pData = ...)` doesn't.
  // Each statement once (CStmtWalk): the operands of the effect table share
  // the common expression of a co_await, also through its OpaqueValueExprs.
  auto DefinesEachTime = [&](const Stmt *Root, unsigned Variable) {
    bool Defines = false;
    CStmtWalk Walk(Root);
    while (const Stmt *S = Walk.next()) {
      if (const auto It = Steps.find(S); It != Steps.end())
        for (const CStep::CDefinition &Definition : It->second.Definitions)
          if (Definition.Variable == Variable && !Definition.Element) {
            Defines = true;
            Walk.stop();
          }
      const CEffects *Of = Index.effectsOf(S);
      if (!Of)
        continue;
      const auto First =
          llvm::find_if(Of->Operands, [](const COperandEffect &Effect) {
            return Effect.Use == EUse::Condition;
          });
      if (First != Of->Operands.end()) {
        Walk.add(First->Operand);
        continue;
      }
      for (const COperandEffect &Effect : Of->Operands)
        if (Effect.Use != EUse::Unevaluated)
          Walk.add(Effect.Operand);
    }
    return Defines;
  };
  // Whether \p Variable is defined on each way through the operand \p Root
  // before \p Use in it is evaluated: in an operand of what \p Use is in
  // there that is evaluated before the one \p Use is in (a lower sequence),
  // `(pStats = g_Stats.f_TryGet()) ? pStats->m_nCompleted : 0`, `(pData =
  // &t_Data, pData->m_Value)`, so that what \p Use reads of it is what the
  // operand got itself.
  auto DefinedBefore = [&](const Stmt *Use, const Stmt *Root,
                           unsigned Variable) {
    for (const Stmt *At = Use; At && At != Root;) {
      const llvm::ArrayRef<CEffectIndex::CUse> Uses = Index.usesOf(At);
      if (Uses.empty())
        return false;
      const Stmt *Parent = Uses.front().Parent;
      const unsigned Sequence = Uses.front().Effect->Sequence;
      if (const CEffects *Of = Index.effectsOf(Parent))
        for (const COperandEffect &Effect : Of->Operands)
          if (Effect.Sequence < Sequence && Effect.Use != EUse::Unevaluated &&
              DefinesEachTime(Effect.Operand, Variable))
            return true;
      At = Parent;
    }
    return false;
  };
  // \p Default, of a step of what a default evaluates, the use of it.
  auto ApplyStep = [&](const Stmt *S, const CStep &Step, CState &State,
                       FOnUse OnUse, FOnScope OnScope, const Stmt *Default) {
    // What an operand unsequenced to one that suspends gets and defines can
    // be of the thread before the suspension, and what it uses can come
    // after it, also the temporaries it makes alive across it; not what it
    // uses of what it reads, there, of a variable it defines itself before
    // (DefinedBefore), which is on the same side of the suspension.
    for (const auto &[Other, Suspending] : Step.Unordered) {
      const CInside &Inside = InsideOf(Other);
      if (const auto *Value = dyn_cast<Expr>(Other)) {
        CValue Of = ValueOf(State, Value, 0);
        for (COrigins &Origins : Of.Levels)
          makeStale(Origins);
        if (!Of.empty())
          State.Values[Value] = std::move(Of);
      }
      for (unsigned Variable : Inside.Defined)
        for (unsigned Level = 0; Level < NumLevels; ++Level)
          if (State.Holds[Level].test(Variable))
            State.Stale[Level].set(Variable);
      for (const CStep::CUse &Use : Inside.Uses) {
        const CValue Of = ValueOf(State, Use.Operand, 0);
        for (unsigned Level = 0; Level < NumLevels; ++Level)
          if (Use.Levels & (1u << Level))
            for (const COrigin &Origin : Of.Levels[Level])
              if (!Origin.Elsewhere &&
                  (Origin.Key < nTracked
                       ? !(Inside.Statements.contains(Origin.Name) &&
                           DefinedBefore(Origin.Name, Other, Origin.Key))
                       : !Inside.Statements.contains(
                             KeyRoots[Origin.Key - nTracked])))
                OnUse(Origin);
      }
      for (const CTemporary &Temporary : State.Temporaries)
        if (Inside.Statements.contains(Temporary.second ? Temporary.second
                                                        : Temporary.first))
          OnScope(std::nullopt, &Temporary);
    }
    // What it gives: of what suspends, what it resumes with, of the thread
    // it resumes on, after the suspension below.
    const auto *E = dyn_cast_or_null<Expr>(S);
    auto Give = [&] {
      CValue Value = Evaluate(Step, E, State, 0);
      if (Value.empty())
        State.Values.erase(E);
      else
        State.Values[E] = std::move(Value);
    };
    if (E && !Step.Suspends)
      Give();
    for (const CStep::CUse &Use : Step.Uses) {
      const CValue Of = ValueOf(State, Use.Operand, 0);
      for (unsigned Level = 0; Level < NumLevels; ++Level)
        if (Use.Levels & (1u << Level))
          for (const COrigin &Origin : Of.Levels[Level])
            if (Origin.Stale && !Origin.Elsewhere)
              OnUse(Origin);
    }
    for (const auto &[Variable, Name] : Step.ReadsBefore)
      if (State.Holds[0].test(Variable) || State.Holds[1].test(Variable))
        OnUse({Variable, Name, true});
    // What a variable is given: a reference what it is bound to, a pointer
    // or an array what the value stored in it points to, of a pointer what
    // it is given points to, also through it; what gives the data elsewhere
    // only where it refers, as a reference or a pointer.
    for (const CStep::CDefinition &Definition : Step.Definitions) {
      const CValue Of = ValueOf(State, Definition.Operand, 0);
      COrigins Given[NumLevels];
      const QualType Type = Definition.Operand->getType();
      const unsigned Down =
          !BindsReferent(Variables[Definition.Variable]) &&
                  Definition.Operand->isPRValue() && !Type->isRecordType() &&
                  !Type->isArrayType()
              ? 1
              : 0;
      for (unsigned Level = 0; Level < NumLevels; ++Level)
        addOrigins(Given[std::min(Level + Down, NumLevels - 1)],
                   Of.Levels[Level]);
      const unsigned Refers =
          BindsReferent(Variables[Definition.Variable]) ? 0 : 1;
      if (Refers > 0)
        Given[0].clear();
      for (unsigned Level = Refers + 1; Level < NumLevels; ++Level)
        llvm::erase_if(Given[Level],
                       [](const COrigin &Origin) { return Origin.Elsewhere; });
      for (unsigned Level = 0; Level < NumLevels; ++Level) {
        const bool Holds = !Given[Level].empty();
        const bool Stale = llvm::any_of(
            Given[Level], [](const COrigin &Origin) { return Origin.Stale; });
        if (Definition.Element) {
          if (Holds)
            State.Holds[Level].set(Definition.Variable);
          if (Stale)
            State.Stale[Level].set(Definition.Variable);
          continue;
        }
        State.Holds[Level][Definition.Variable] = Holds;
        State.Stale[Level][Definition.Variable] = Stale;
      }
    }
    for (unsigned Variable : Step.Resets)
      for (unsigned Level = 0; Level < NumLevels; ++Level) {
        State.Holds[Level].reset(Variable);
        State.Stale[Level].reset(Variable);
      }
    for (unsigned Scope : Step.ScopesStarted)
      State.Alive.set(Scope);
    if (Step.MakesScope && E)
      State.addTemporary({E, Default});
    if (Step.Suspends) {
      for (unsigned Level = 0; Level < NumLevels; ++Level)
        State.Stale[Level] |= State.Holds[Level];
      // Not what it resumes with, an operand that runs after the one that
      // suspends (effectsOf), `await_resume()`, of the thread it resumes on.
      llvm::SmallPtrSet<const Stmt *, 8> Resumed;
      if (const CEffects *Of = S ? Index.effectsOf(S) : nullptr) {
        unsigned Suspending = 0;
        bool Found = false;
        for (const COperandEffect &Effect : Of->Operands)
          if (Effect.Use == EUse::Discarded) {
            Suspending = std::max(Suspending, Effect.Sequence);
            Found = true;
          }
        for (const COperandEffect &Effect : Of->Operands)
          if (Found && Effect.Use == EUse::Forward &&
              Effect.Sequence > Suspending && Effect.Operand)
            Resumed.insert(InsideOf(Effect.Operand).Statements.begin(),
                           InsideOf(Effect.Operand).Statements.end());
      }
      // Nor, of the `co_await` or the `co_yield` it is, or is a part of,
      // the operands of what that is in that are sequenced after it
      // (COperandEffect::Sequence), which the CFG can have before it: the
      // left of an assignment, also a compound one, after its right,
      // `fg_WorkerThreadLocal().m_nBytes += co_await fg_ReadChunk();`.
      const Stmt *Await = S;
      for (int Depth = 0;
           Await && !isa<CoroutineSuspendExpr>(Await) && Depth < 16; ++Depth) {
        const auto Parents = Context.getParents(*Await);
        Await = Parents.empty() ? nullptr : Parents[0].get<Stmt>();
      }
      for (const Stmt *Inner = isa_and_nonnull<CoroutineSuspendExpr>(Await)
                                   ? Await
                                   : nullptr;
           Inner;) {
        const llvm::ArrayRef<CEffectIndex::CUse> Uses = Index.usesOf(Inner);
        if (Uses.size() != 1 || !isa<Expr>(Uses.front().Parent))
          break;
        const Stmt *Outer = Uses.front().Parent;
        const unsigned Own = Uses.front().Effect->Sequence;
        for (const COperandEffect &Effect : Index.effectsOf(Outer)->Operands)
          if (Effect.Operand && Effect.Operand != Inner &&
              Effect.Sequence > Own && Effect.Use != EUse::Unevaluated)
            Resumed.insert(InsideOf(Effect.Operand).Statements.begin(),
                           InsideOf(Effect.Operand).Statements.end());
        Inner = Outer;
      }
      for (auto &[Evaluated, Value] : State.Values)
        if (!Resumed.contains(Evaluated))
          for (COrigins &Origins : Value.Levels)
            makeStale(Origins);
      for (unsigned Scope : State.Alive.set_bits())
        OnScope(Scope, nullptr);
      for (const CTemporary &Temporary : State.Temporaries)
        OnScope(std::nullopt, &Temporary);
      if (E)
        Give();
    }
  };
  // Where a full expression or a statement ends, the values of what it
  // evaluated end, of a full expression not its own, which what it is in
  // takes, and so do its temporaries, at the latest.
  auto EndFullExpression = [&](const Stmt *S, CState &State) {
    if (isa<Expr>(S) && RootOf.lookup(S) != S)
      return;
    llvm::SmallVector<const Expr *, 4> Ended;
    for (const auto &[Evaluated, Value] : State.Values)
      if (Evaluated != S && !Outlives(RootOf.lookup(Evaluated), S) &&
          !CaptureInits.contains(Evaluated))
        Ended.push_back(Evaluated);
    for (const Expr *Evaluated : Ended)
      State.Values.erase(Evaluated);
    llvm::erase_if(State.Temporaries, [&](const CTemporary &Temporary) {
      return !Outlives(RootOf.lookup(Temporary.second ? Temporary.second
                                                      : Temporary.first),
                       S);
    });
  };
  auto EndTemporaries = [&](const Stmt *At, CState &State) {
    if (const auto It = EndsOf.find(At); It != EndsOf.end())
      llvm::erase_if(State.Temporaries, [&](const CTemporary &Temporary) {
        return llvm::is_contained(It->second, Temporary.first);
      });
  };
  // The element being applied, which a use in what a default evaluates is
  // reported for (UseLocation).
  const Stmt *Applying = nullptr;
  auto Apply = [&](const CFGBlock *Block, unsigned Position,
                   const CFGElement &Element, CState &State, FOnUse OnUse,
                   FOnScope OnScope) {
    const CFlowEvent Event = eventOf(Element);
    Applying = Event.Statement;
    switch (Event.Kind) {
    // Where the scope of a variable ends.
    case EEvent::LifetimeEnds:
    case EEvent::AutomaticDestructor:
      // What a guard runs where it ends, not after it is cancelled
      // (closureRunsAtEnd).
      if (auto It = EndSteps.find(Event.Variable);
          It != EndSteps.end() &&
          closureRunsAtEnd(Function, Block->getBlockID(), Position, Context))
        ApplyStep(nullptr, It->second, State, OnUse, OnScope, nullptr);
      if (auto It = ScopeIndices.find(Event.Variable);
          It != ScopeIndices.end())
        State.Alive.reset(It->second);
      return;
    // Where a temporary ends, also one a scope is made in.
    case EEvent::FullExpressionCleanup:
      for (const MaterializeTemporaryExpr *Temporary : Event.Temporaries)
        EndTemporaries(Temporary, State);
      return;
    case EEvent::TemporaryDestructor:
      EndTemporaries(Event.Statement, State);
      return;
    case EEvent::Statement:
      break;
    case EEvent::Initializer:
    case EEvent::NewAllocator:
    case EEvent::LoopExit:
    case EEvent::ScopeBegin:
    case EEvent::ScopeEnd:
    case EEvent::DeleteDestructor:
    case EEvent::BaseDestructor:
    case EEvent::MemberDestructor:
    case EEvent::CleanupFunction:
      return;
    }
    if (auto It = Defaults.find(Event.Statement); It != Defaults.end())
      for (const Stmt *Evaluated : It->second)
        if (auto Step = Steps.find(Evaluated); Step != Steps.end())
          ApplyStep(Evaluated, Step->second, State, OnUse, OnScope,
                    Event.Statement);
    if (const auto It = Steps.find(Event.Statement); It != Steps.end())
      ApplyStep(Event.Statement, It->second, State, OnUse, OnScope, nullptr);
    EndFullExpression(Event.Statement, State);
  };

  // The states at the start of each block, and of each that dispatches to
  // handlers what the elements of the blocks before it in its `try` leave,
  // before and after each, of each dispatch its own `try`'s only, of nested
  // ones in one block too (inDispatchTry).
  std::vector<std::optional<CState>> In(Graph->getNumBlockIDs());
  using CThrown = llvm::SmallDenseMap<const CFGBlock *, CState, 2>;
  auto Run = [&](const CFGBlock *Block, CState State, CThrown *Thrown,
                 FOnUse OnUse, FOnScope OnScope) {
    llvm::SmallVector<const CFGBlock *, 2> Dispatches;
    if (Thrown)
      for (const CFGBlock::AdjacentBlock &Successor : Block->succs())
        if (const CFGBlock *To = Successor.getReachableBlock();
            To && isHandlerDispatch(To))
          Dispatches.push_back(To);
    unsigned Position = 0;
    for (const CFGElement &Element : *Block) {
      llvm::SmallVector<const CFGBlock *, 2> Into;
      for (const CFGBlock *Dispatch : Dispatches)
        if (inDispatchTry(Dispatch, Element, Context))
          Into.push_back(Dispatch);
      for (const CFGBlock *Dispatch : Into)
        Thrown->try_emplace(Dispatch, Empty()).first->second.merge(State);
      Apply(Block, Position++, Element, State, OnUse, OnScope);
      for (const CFGBlock *Dispatch : Into)
        Thrown->find(Dispatch)->second.merge(State);
    }
    return State;
  };
  std::deque<const CFGBlock *> Work;
  // What a handler is entered with: what its `try` leaves, without the
  // scopes and the temporaries made in the `try`, which unwinding ends.
  llvm::SmallVector<const Stmt *, 2> DeclaredBy(nScopes, nullptr);
  for (const Stmt *S : Index.statements())
    for (const Decl *Declared : Index.effectsOf(S)->Declares)
      for (unsigned iScope = 0; iScope < nScopes; ++iScope)
        if (Scopes[iScope].Variable == Declared && !DeclaredBy[iScope])
          DeclaredBy[iScope] = S;
  auto Unwound = [&](const CFGBlock *Dispatch, CState State) {
    const auto *Try = dyn_cast_or_null<CXXTryStmt>(Dispatch->getTerminatorStmt());
    if (!Try || !Try->getTryBlock())
      return State;
    for (unsigned iScope = 0; iScope < nScopes; ++iScope)
      if (DeclaredBy[iScope] && Within(DeclaredBy[iScope], Try->getTryBlock()))
        State.Alive.reset(iScope);
    llvm::erase_if(State.Temporaries, [&](const CTemporary &Temporary) {
      return Within(Temporary.first, Try->getTryBlock());
    });
    return State;
  };
  llvm::BitVector Queued(Graph->getNumBlockIDs());
  In[Graph->getEntry().getBlockID()] = Start;
  Work.push_back(&Graph->getEntry());
  Queued.set(Graph->getEntry().getBlockID());
  auto NoUse = [](const COrigin &) {};
  auto NoScope = [](std::optional<unsigned>, const CTemporary *) {};
  while (!Work.empty()) {
    const CFGBlock *Block = Work.front();
    Work.pop_front();
    Queued.reset(Block->getBlockID());
    CThrown Thrown;
    const CState Out =
        Run(Block, *In[Block->getBlockID()], &Thrown, NoUse, NoScope);
    for (const CFGBlock::AdjacentBlock &Successor : Block->succs()) {
      const CFGBlock *Next = Successor.getReachableBlock();
      if (!Next)
        continue;
      const auto ThrownTo = Thrown.find(Next);
      const CState Flowing =
          !isHandlerDispatch(Next) ? Out
          : ThrownTo != Thrown.end() ? Unwound(Next, ThrownTo->second)
                                     : Empty();
      std::optional<CState> &Into = In[Next->getBlockID()];
      if (Into) {
        CState Merged = *Into;
        Merged.merge(Flowing);
        if (Merged == *Into)
          continue;
        Into = std::move(Merged);
      } else
        Into = Flowing;
      if (!Queued.test(Next->getBlockID())) {
        Queued.set(Next->getBlockID());
        Work.push_back(Next);
      }
    }
  }

  // The first use of what each key refers to after a suspension, in the
  // order of the source, which is where it is to be gotten again; the scopes
  // alive at a suspension; and the suspensions that can be reached.
  llvm::MapVector<unsigned, std::pair<SourceLocation, const NamedDecl *>>
      FirstUses;
  llvm::SmallVector<const Stmt *, 4> Suspensions;
  // Where \p Name is used, evaluated for \p At: of what defaults share,
  // where the default of \p At, or in it, is used, not the first of them.
  auto UseLocation = [&](const Expr *Name, const Stmt *At) {
    for (const Stmt *Up = Name; Up && At;) {
      const llvm::ArrayRef<CEffectIndex::CUse> Uses = Index.usesOf(Up);
      if (Uses.size() > 1) {
        for (const CEffectIndex::CUse &Use : Uses)
          if (Index.effectsOf(Use.Parent)->Statement == EStatement::Default &&
              (Use.Parent == At || Within(Use.Parent, At)))
            return Index.locationOf(Use.Parent);
        break;
      }
      Up = Uses.empty() ? nullptr : Uses.front().Parent;
    }
    return Index.locationOf(Name);
  };
  for (const CFGBlock *Block : *Graph) {
    if (!Reachable.test(Block->getBlockID()) || !In[Block->getBlockID()])
      continue;
    Run(
        Block, *In[Block->getBlockID()], nullptr,
        [&](const COrigin &Use) {
          const SourceLocation Loc =
              reportLocation(UseLocation(Use.Name, Applying), SM);
          if (Loc.isInvalid())
            return;
          const auto *Ref = Use.Name && effectsOf(Use.Name, Context).Statement ==
                                            EStatement::Name
                                ? cast<DeclRefExpr>(Use.Name)
                                : nullptr;
          const NamedDecl *Name = Ref ? Ref->getDecl() : nullptr;
          auto [It, Inserted] =
              FirstUses.try_emplace(Use.Key, std::make_pair(Loc, Name));
          if (!Inserted && SM.isBeforeInTranslationUnit(Loc, It->second.first))
            It->second = {Loc, Name};
        },
        [&](std::optional<unsigned> Scope, const CTemporary *Temporary) {
          if (Scope)
            ScopesAlive.insert(*Scope);
          if (Temporary)
            TemporariesAlive.insert(*Temporary);
        });
    for (const CFGElement &Element : *Block) {
      const CFlowEvent Event = eventOf(Element);
      if (Event.Kind != EEvent::Statement)
        continue;
      if (const auto It = Steps.find(Event.Statement);
          It != Steps.end() && It->second.Suspends)
        Suspensions.push_back(Event.Statement);
    }
  }
  for (unsigned iScope = 0; iScope < nScopes; ++iScope) {
    if (!ScopesAlive.contains(iScope))
      continue;
    const CScope &Scope = Scopes[iScope];
    if (Scope.ObjectCopy) {
      const SourceLocation Loc =
          reportLocation(Scope.ObjectCopy->getBeginLoc(), SM);
      if (Loc.isValid())
        diag(Loc, "the copy of the object the lambda captures keeps "
                  "thread-local data of the thread that made it and is alive "
                  "across a suspension; the coroutine can resume on another "
                  "thread; end its scope before the suspension");
      continue;
    }
    const SourceLocation Loc =
        reportLocation(Scope.Variable->getLocation(), SM);
    if (Loc.isValid())
      diag(Loc, "%0 keeps thread-local data of the thread that made it and "
                "is alive across a suspension; the coroutine can resume on "
                "another thread; end its scope before the suspension")
          << Scope.Variable;
  }
  // In the order of the source, not of the set, so that each compile
  // reports them alike.
  struct CAliveTemporary {
    SourceLocation Loc;
    const Expr *Made;
    const Stmt *Default;
  };
  llvm::SmallVector<CAliveTemporary, 4> Alive;
  for (const auto &[Made, Default] : TemporariesAlive) {
    const SourceLocation Loc = reportLocation(
        Default ? usedLocation(Default) : Index.locationOf(Made), SM);
    if (Loc.isValid())
      Alive.push_back({Loc, Made, Default});
  }
  llvm::sort(Alive, [&](const CAliveTemporary &Left,
                        const CAliveTemporary &Right) {
    if (isBeforeInSource(Left.Loc, Right.Loc, SM))
      return true;
    if (isBeforeInSource(Right.Loc, Left.Loc, SM))
      return false;
    if (Left.Made != Right.Made)
      return isBeforeInSource(Left.Made, Right.Made, Context);
    if (!Left.Default || !Right.Default)
      return !Left.Default && Right.Default;
    return isBeforeInSource(Left.Default, Right.Default, Context);
  });
  for (const CAliveTemporary &Temporary : Alive)
    diag(Temporary.Loc, "a temporary %0 keeps thread-local data of the thread "
                        "that made it and is alive across a suspension; the "
                        "coroutine can resume on another thread; end its "
                        "scope before the suspension")
        << Temporary.Made->getType().getUnqualifiedType();
  // The loops that go on over it, by their range or iterator, and those
  // over it with a suspension in them that can be reached, whatever their
  // iterators are.
  llvm::SmallPtrSet<const CXXForRangeStmt *, 2> ReportedLoops;
  auto ReportLoop = [&](const CXXForRangeStmt *For, SourceLocation Loc) {
    if (!ReportedLoops.insert(For).second)
      return;
    const SourceLocation RangeLoc =
        reportLocation(For->getRangeInit()->getBeginLoc(), SM);
    if (RangeLoc.isInvalid() && Loc.isInvalid())
      return;
    diag(RangeLoc.isValid() ? RangeLoc : Loc,
         "the loop goes on over thread-local data of the thread that ran "
         "before a suspension; the coroutine can resume on another "
         "thread; loop over a copy, or get it again after the suspension")
        << For->getRangeInit()->getSourceRange();
  };
  for (const CXXForRangeStmt *For : DataLoops)
    if (For->getBody() && llvm::any_of(Suspensions, [&](const Stmt *Each) {
          return Within(Each, For->getBody());
        }))
      ReportLoop(For, SourceLocation());
  for (const auto &[Key, Use] : FirstUses) {
    const auto &[Loc, Name] = Use;
    if (TracksThis && Key == This) {
      diag(Loc, "the object refers to thread-local data of the thread that "
                "ran before the suspension; the coroutine can resume on "
                "another thread; get it again after the suspension");
      continue;
    }
    // What gives the data, used after a suspension in its expression.
    if (Key >= Variables.size()) {
      diag(Loc, "thread-local data gotten here is used after a suspension "
                "later in its expression; the coroutine can resume on "
                "another thread; get it after the suspension");
      continue;
    }
    const VarDecl *Variable = Variables[Key];
    if (const CXXForRangeStmt *For = Loops.lookup(Variable)) {
      ReportLoop(For, Loc);
      continue;
    }
    // By the name used, also of a reference to it or a structured binding.
    diag(Loc, "%0 refers to thread-local data of the thread that ran before "
              "the suspension; the coroutine can resume on another thread; "
              "get it again after the suspension")
        << (Name ? Name : Variable);
  }
}

} // namespace clang::tidy::malterlib
