//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CoroutineThreadLocalReferenceCheck.h"
#include "ActorAnnotations.h"
#include "ThreadLocalUtils.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/MapVector.h"
#include <deque>
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
  if (Repeated.repeated(Result))
    return;
  ASTContext &Context = *Result.Context;
  const CThreadLocalData Data(Context, Lists);
  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  auto ForEachChecked = [&](const FunctionDecl *Function, auto &&Check) {
    if (!Function->isTemplated()) {
      Check(Function);
      return;
    }
    for (const FunctionDecl *Instance :
         Instantiations.instantiationsOf(Function, Context)) {
      const InstantiationScope Scope(*this, Instance);
      Check(Instance);
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

void CoroutineThreadLocalReferenceCheck::checkFunction(
    const FunctionDecl *Function, const LambdaExpr *Lambda,
    const CThreadLocalData &Data) {
  ASTContext &Context = Data.context();
  const SourceManager &SM = Context.getSourceManager();
  Stmt *Body = Function->getBody();
  if (!Body)
    return;
  // What can refer to the data: the locals, the parameters and the captures
  // of a reference, a pointer or an array of pointers that do somewhere in
  // it, not a local reference to another local, which is that local where it
  // is used (boundLocal), and, of a member function of a thread-local type,
  // its object.
  llvm::SmallVector<const VarDecl *, 8> Variables;
  llvm::DenseMap<const VarDecl *, unsigned> Tracked;
  // Which refer to it from the start: parameters of a reference or a pointer
  // to a thread-local type, and captures of what does.
  llvm::DenseSet<const VarDecl *> FromStart;
  auto Add = [&](const VarDecl *Variable) {
    if (Tracked.try_emplace(Variable, Variables.size()).second)
      Variables.push_back(Variable);
  };
  // A local reference to another local, `int *&rpValue = pValue`.
  auto IsAlias = [&](const VarDecl *Variable) {
    const VarDecl *Bound = Variable->getType()->isReferenceType()
                               ? Data.boundLocal(Variable)
                               : nullptr;
    return Bound && Bound != Variable;
  };
  // The scopes, of ThreadLocalScopeTypes, not of a handler: locals, also a
  // temporary a reference extends, `auto &&Scope = CCurrentActorScope{...}`,
  // declared anywhere, also in what a statement starts with, `if (CScope
  // Scope; ...)`, and parameters held by value, which the coroutine owns.
  llvm::SmallVector<const VarDecl *, 2> Scopes;
  llvm::DenseMap<const VarDecl *, unsigned> ScopeIndices;
  auto AddScope = [&](const VarDecl *Variable) {
    if (ScopeIndices.try_emplace(Variable, Scopes.size()).second)
      Scopes.push_back(Variable);
  };
  // Parameters: of a thread-local type from the start; a pointer held by
  // value, which the function can point into it.
  for (const ParmVarDecl *Parameter : Function->parameters()) {
    const QualType Type = Parameter->getType();
    if ((Type->isReferenceType() || Type->isPointerType()) &&
        Data.isType(Type, true)) {
      Add(Parameter);
      FromStart.insert(Parameter);
    } else if (Type->isPointerType())
      Add(Parameter);
    else if (Data.isScopeType(Type))
      AddScope(Parameter);
  }
  // Captures of what refers to it: a pointer or an array of them, copied or
  // by reference, a reference by reference, also one to a local pointer,
  // `[&rpValue]`, a copy of a reference to a pointer, `[rpValue]`, which
  // copies the pointer, an initializer, also a reference to a local pointer,
  // `[&rpValue = pValue]`, and a structured binding, by what it binds; not a
  // copy of what a reference refers to otherwise, `[Data]`.
  bool CapturesThis = false;
  if (Lambda)
    for (const LambdaCapture &Capture : Lambda->captures()) {
      if (Capture.capturesThis() && Capture.getCaptureKind() != LCK_StarThis)
        CapturesThis = true;
      if (!Capture.capturesVariable())
        continue;
      const ValueDecl *Captured = Capture.getCapturedVar();
      if (const auto *Binding = dyn_cast<BindingDecl>(Captured))
        Captured = Binding->getDecomposedDecl();
      const auto *Variable = dyn_cast_or_null<VarDecl>(Captured);
      if (!Variable)
        continue;
      const QualType Type = Variable->getType();
      QualType Copied = Type.getNonReferenceType();
      while (const ArrayType *Array = Copied->getAsArrayTypeUnsafe())
        Copied = Array->getElementType();
      if (Capture.getCaptureKind() == LCK_ByCopy &&
          !Variable->isInitCapture() && Type->isReferenceType() &&
          !Copied->isPointerType())
        continue;
      const VarDecl *Bound =
          Type->isReferenceType() ? Data.boundLocal(Variable) : nullptr;
      if (Data.isVariable(Variable) ||
          (Bound && Bound != Variable && Data.isVariable(Bound))) {
        Add(Variable);
        FromStart.insert(Variable);
      }
    }
  // The loops over thread-local data, whose iterators refer to it.
  llvm::SmallVector<const CXXForRangeStmt *, 2> DataLoops;
  // The temporaries of ThreadLocalScopeTypes, `(CScope{...}, co_await
  // f_Other())`, which live to the end of their full expression.
  llvm::SmallVector<const CXXConstructExpr *, 2> ScopeTemporaries;
  {
    llvm::SmallVector<const Stmt *, 64> Work{Body};
    while (!Work.empty()) {
      const Stmt *S = Work.pop_back_val();
      if (!S || isa<LambdaExpr>(S))
        continue;
      if (const auto *Declaration = dyn_cast<DeclStmt>(S))
        for (const Decl *Declared : Declaration->decls())
          if (const auto *Variable = dyn_cast<VarDecl>(Declared);
              Variable && Variable->hasLocalStorage()) {
            if (!IsAlias(Variable) && Data.isVariable(Variable))
              Add(Variable);
            QualType Type = Variable->getType();
            if (Type->isReferenceType()) {
              const auto *Materialize =
                  Variable->getInit() ? dyn_cast<MaterializeTemporaryExpr>(
                                            Data.normalized(Variable->getInit())
                                                ->IgnoreParens())
                                      : nullptr;
              Type = Materialize && Materialize->getExtendingDecl() == Variable
                         ? Materialize->getSubExpr()->getType()
                         : QualType();
            }
            if (!Type.isNull() && Data.isScopeType(Type))
              AddScope(Variable);
          }
      if (const auto *Construct = dyn_cast<CXXConstructExpr>(S);
          Construct && Data.isScopeType(Construct->getType())) {
        // Not what a variable is made of, also through what a reference
        // extends.
        DynTypedNode Node = DynTypedNode::create(*Construct);
        bool Named = false;
        for (int Level = 0; Level < 8; ++Level) {
          const auto Parents = Context.getParents(Node);
          if (Parents.empty())
            break;
          if (Parents[0].get<VarDecl>()) {
            Named = true;
            break;
          }
          const Expr *Parent = Parents[0].get<Expr>();
          if (!Parent || !isa<MaterializeTemporaryExpr, CXXBindTemporaryExpr,
                              ExprWithCleanups, ImplicitCastExpr, ParenExpr,
                              CXXFunctionalCastExpr>(Parent))
            break;
          Node = Parents[0];
        }
        if (!Named)
          ScopeTemporaries.push_back(Construct);
      }
      if (const auto *For = dyn_cast<CXXForRangeStmt>(S);
          For && For->getRangeInit() &&
          (Data.isData(For->getRangeInit()) ||
           (For->getRangeInit()->getType()->isPointerType() &&
            Data.pointsTo(For->getRangeInit()))))
        DataLoops.push_back(For);
      for (const Stmt *Child : S->children())
        Work.push_back(Child);
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
  // A temporary scope with a suspension that can run after it is made in
  // its full expression.
  for (const CXXConstructExpr *Construct : ScopeTemporaries) {
    const Stmt *Full = Construct;
    for (int Level = 0; Level < 64; ++Level) {
      const auto Parents = Context.getParents(*Full);
      const Expr *Parent = Parents.empty() ? nullptr : Parents[0].get<Expr>();
      if (!Parent)
        break;
      Full = Parent;
    }
    bool Suspends = false;
    std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
      if (!S || Suspends || isa<LambdaExpr>(S))
        return;
      if (isSuspension(S) &&
          SM.isBeforeInTranslationUnit(Construct->getEndLoc(),
                                       S->getBeginLoc()) &&
          canRunIn(S, Body, Function, Context)) {
        Suspends = true;
        return;
      }
      for (const Stmt *Child : evaluatedChildren(S, Context))
        Find(Child);
    };
    Find(Full);
    if (!Suspends)
      continue;
    const SourceLocation Loc = reportLocation(Construct->getBeginLoc(), SM);
    if (Loc.isValid())
      diag(Loc, "a temporary %0 keeps thread-local data of the thread that "
                "made it and is alive across a suspension; the coroutine can "
                "resume on another thread; end its scope before the "
                "suspension")
          << Construct->getType();
  }
  if (Variables.empty() && Scopes.empty() && DataLoops.empty() && !TracksThis)
    return;

  CFG::BuildOptions Build;
  Build.setAllAlwaysAdd();
  // Where the scopes end.
  Build.AddImplicitDtors = true;
  Build.AddLifetime = true;
  const std::unique_ptr<CFG> Graph =
      CFG::buildCFG(Function, Body, &Context, Build);
  if (!Graph)
    return;
  addExceptionEdges(*Graph, Context);
  const llvm::BitVector Reachable = reachableBlocks(*Graph);
  const auto SuspendPoints = suspendPoints(Body);
  // The object, after the variables.
  const unsigned nVariables = Variables.size() + (TracksThis ? 1 : 0);
  const unsigned This = Variables.size();
  const unsigned nScopes = Scopes.size();

  // The tracked variable \p E names, also a name of a structured binding of
  // one, the object as `this`, and a local reference to one.
  auto TrackedNamed = [&](const Expr *E) -> std::optional<unsigned> {
    if (isa<CXXThisExpr>(E))
      return TracksThis ? std::optional<unsigned>(This) : std::nullopt;
    const auto *Ref = dyn_cast<DeclRefExpr>(E);
    if (!Ref)
      return std::nullopt;
    const ValueDecl *Named = Ref->getDecl();
    if (const auto *Binding = dyn_cast<BindingDecl>(Named))
      Named = Binding->getDecomposedDecl();
    const auto *Variable = dyn_cast_or_null<VarDecl>(Named);
    if (!Variable)
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
  // The tracked variables what \p E, a pointer or a glvalue, is made of, as
  // objectRelation tells: the object itself, a part or an entry of it.
  auto TrackedIn = [&](const Expr *E, bool Pointer) {
    llvm::SmallVector<unsigned, 2> Named;
    auto IsTracked = [&](const Expr *Root) {
      const std::optional<unsigned> Index = TrackedNamed(Root);
      if (Index && !llvm::is_contained(Named, *Index))
        Named.push_back(*Index);
      return Index.has_value();
    };
    if (Pointer)
      pointeeRelation(E, IsTracked, &Context);
    else
      objectRelation(E, IsTracked, &Context);
    return Named;
  };

  // Whether \p E gets the data before a suspension in it, `auto *pData =
  // (&fg_ConcurrencyThreadLocal(), co_await f_Other())`: what names it
  // before the suspension in the order of the source, or in what it awaits,
  // not the left of an assignment whose right suspends, which runs after
  // it, nor in a branch a constant doesn't take, nor in the other value of a
  // conditional, nor on the left of a comma, which is discarded.
  auto GottenBeforeSuspension = [&](const Expr *E) {
    llvm::SmallVector<const Expr *, 4> Gotten;
    llvm::SmallVector<const CoroutineSuspendExpr *, 2> Suspensions;
    llvm::SmallVector<std::pair<const Expr *, const Expr *>, 2> Assignments;
    llvm::SmallVector<const AbstractConditionalOperator *, 2> Conditionals;
    std::function<void(const Stmt *, bool)> Find = [&](const Stmt *S,
                                                       bool Valued) {
      if (!S || isa<LambdaExpr>(S))
        return;
      if (const auto *Suspend = dyn_cast<CoroutineSuspendExpr>(S)) {
        if (isSuspension(Suspend))
          Suspensions.push_back(Suspend);
        Find(Suspend->getOperand(), Valued);
        return;
      }
      if (const auto *Comma = dyn_cast<BinaryOperator>(S);
          Comma && Comma->getOpcode() == BO_Comma) {
        Find(Comma->getLHS(), false);
        Find(Comma->getRHS(), Valued);
        return;
      }
      if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(S))
        Conditionals.push_back(Conditional);
      if (const auto *Value = dyn_cast<Expr>(S); Value && Valued) {
        if (isThreadLocalData(Value, Lists.Functions) ||
            TrackedNamed(Value->IgnoreParenImpCasts()))
          Gotten.push_back(Value);
      }
      if (const auto *Assign = dyn_cast<BinaryOperator>(S);
          Assign && Assign->isAssignmentOp())
        Assignments.push_back({Assign->getLHS(), Assign->getRHS()});
      else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S);
               Operator && Operator->isAssignmentOp() &&
               Operator->getNumArgs() == 2)
        Assignments.push_back({Operator->getArg(0), Operator->getArg(1)});
      for (const Stmt *Child : evaluatedChildren(S, Context))
        Find(Child, Valued);
    };
    Find(E, true);
    auto Within = [](const Expr *Outer, const Expr *Inner) {
      return Outer->getSourceRange().fullyContains(Inner->getSourceRange());
    };
    return llvm::any_of(Suspensions, [&](const CoroutineSuspendExpr *Suspend) {
      return llvm::any_of(Gotten, [&](const Expr *Got) {
        if (llvm::any_of(Assignments, [&](const auto &Assignment) {
              return Within(Assignment.first, Got) &&
                     Within(Assignment.second, Suspend);
            }))
          return false;
        // Not one value of a conditional and the other, which don't both
        // run.
        if (llvm::any_of(Conditionals, [&](const auto *Conditional) {
              const Expr *True = Conditional->getTrueExpr();
              const Expr *False = Conditional->getFalseExpr();
              return (Within(True, Got) && Within(False, Suspend)) ||
                     (Within(False, Got) && Within(True, Suspend));
            }))
          return false;
        return SM.isBeforeInTranslationUnit(Got->getBeginLoc(),
                                            Suspend->getBeginLoc()) ||
               (Suspend->getOperand() && Within(Suspend->getOperand(), Got));
      });
    });
  };

  struct CState {
    llvm::BitVector Holds;
    llvm::BitVector Stale;
    /// The scopes that may be alive.
    llvm::BitVector Alive;
    void merge(const CState &Other) {
      Holds |= Other.Holds;
      Stale |= Other.Stale;
      Alive |= Other.Alive;
    }
    bool operator==(const CState &Other) const {
      return Holds == Other.Holds && Stale == Other.Stale &&
             Alive == Other.Alive;
    }
  };
  auto Empty = [&] {
    return CState{llvm::BitVector(nVariables), llvm::BitVector(nVariables),
                  llvm::BitVector(nScopes)};
  };
  // Whether it suspends before its body runs, by the initial suspension of
  // its promise, past its cleanups (isSuspension).
  bool LazyStart = false;
  if (const auto *Coroutine = dyn_cast<CoroutineBodyStmt>(Body))
    if (const auto *Initial =
            dyn_cast_or_null<Expr>(Coroutine->getInitSuspendStmt()))
      LazyStart = isSuspension(Initial->IgnoreImplicit());
  // The parameters, the captures and the object refer to it from the start,
  // of the thread that called it or made it; the scopes it is given are
  // alive.
  CState Start = Empty();
  for (unsigned iVariable = 0; iVariable < Variables.size(); ++iVariable)
    if (FromStart.contains(Variables[iVariable]))
      Start.Holds.set(iVariable);
  if (TracksThis)
    Start.Holds.set(This);
  if (LazyStart)
    Start.Stale = Start.Holds;
  for (unsigned iScope = 0; iScope < nScopes; ++iScope)
    if (isa<ParmVarDecl>(Scopes[iScope]))
      Start.Alive.set(iScope);

  // The variable \p Index, of \p Type, is given \p Value, also only as one
  // of its elements, \p Element: whether it refers to the data there
  // (refersTo, which follows locals where they are, localValuesAt), and is
  // stale, gotten before a suspension in it or from what is stale.
  auto Given = [&](CState &State, unsigned Index, QualType Type,
                   const Expr *Value, bool Element) {
    bool Holds = Value && Data.refersTo(Value, Type);
    bool Stale = false;
    if (Holds) {
      const bool Pointer = !Type->isReferenceType() ||
                           Type.getNonReferenceType()->isPointerType();
      Stale = GottenBeforeSuspension(Value) ||
              llvm::any_of(TrackedIn(Value, Pointer), [&](unsigned Source) {
                return State.Stale.test(Source);
              });
    }
    if (Element) {
      State.Holds[Index] = State.Holds.test(Index) || Holds;
      State.Stale[Index] = State.Stale.test(Index) || Stale;
      return;
    }
    State.Holds[Index] = Holds;
    State.Stale[Index] = Stale;
  };
  // Whether the name \p Ref, of a tracked variable, is a use of what it
  // refers to: not where a pointer is given a value, `pValue = ...`, nor its
  // address taken, nor where a tracked variable is made of it, which the
  // flow follows (Given).
  auto IsUse = [&](const Expr *Ref, bool Reference) {
    DynTypedNode Node = DynTypedNode::create(*Ref);
    const Expr *Child = Ref;
    for (int Level = 0; Level < 16; ++Level) {
      const auto Parents = Context.getParents(Node);
      if (Parents.empty())
        return true;
      if (const auto *Variable = Parents[0].get<VarDecl>())
        return !(Variable->getInit() == Child && Tracked.contains(Variable));
      const Expr *Parent = Parents[0].get<Expr>();
      if (!Parent)
        return true;
      if (isa<ParenExpr, ImplicitCastExpr>(Parent)) {
        Child = Parent;
        Node = Parents[0];
        continue;
      }
      if (const auto *Assign = dyn_cast<BinaryOperator>(Parent);
          Assign && Assign->getOpcode() == BO_Assign) {
        if (Assign->getLHS() == Child)
          return Reference;
        if (const VarDecl *Target = Data.assignedLocal(Assign->getLHS());
            Target && Tracked.contains(Target))
          return false;
        return true;
      }
      if (const auto *Unary = dyn_cast<UnaryOperator>(Parent);
          Unary && Unary->getOpcode() == UO_AddrOf)
        return false;
      return true;
    }
    return true;
  };
  // What \p S, an element, uses or changes; \p OnUse of each use of what is
  // stale, by its index and the name used, none of the object, and \p
  // OnScope of each scope alive at a suspension.
  using FOnUse =
      llvm::function_ref<void(const Stmt *, unsigned, const NamedDecl *)>;
  using FOnScope = llvm::function_ref<void(const Stmt *, unsigned)>;
  auto Step = [&](const CFGElement &Element, CState &State, FOnUse OnUse,
                  FOnScope OnScope) {
    // Where a scope ends.
    const VarDecl *Ended = nullptr;
    if (const auto LifetimeEnds = Element.getAs<CFGLifetimeEnds>())
      Ended = LifetimeEnds->getVarDecl();
    else if (const auto Destructor = Element.getAs<CFGAutomaticObjDtor>())
      Ended = Destructor->getVarDecl();
    if (Ended) {
      if (auto It = ScopeIndices.find(Ended); It != ScopeIndices.end())
        State.Alive.reset(It->second);
      return;
    }
    const auto Statement = Element.getAs<CFGStmt>();
    if (!Statement)
      return;
    const Stmt *S = Statement->getStmt();
    if (const auto *Declaration = dyn_cast<DeclStmt>(S)) {
      for (const Decl *Declared : Declaration->decls())
        if (const auto *Variable = dyn_cast<VarDecl>(Declared)) {
          if (auto It = Tracked.find(Variable); It != Tracked.end())
            Given(State, It->second, Variable->getType(), Variable->getInit(),
                  false);
          if (auto It = ScopeIndices.find(Variable); It != ScopeIndices.end())
            State.Alive.set(It->second);
        }
      return;
    }
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->getOpcode() == BO_Assign) {
      // Of a local pointer, also a parameter held by value, through a local
      // reference to it or an element of a local array; not what a reference
      // refers to, `Value = 1` of `int &Value`, which doesn't rebind it.
      if (const VarDecl *Target = Data.assignedLocal(Assign->getLHS()))
        if (auto It = Tracked.find(Target); It != Tracked.end())
          Given(
              State, It->second, Assign->getLHS()->getType(), Assign->getRHS(),
              isa<ArraySubscriptExpr>(Assign->getLHS()->IgnoreParenImpCasts()));
      return;
    }
    if (isSuspension(S) || SuspendPoints.contains(S)) {
      State.Stale |= State.Holds;
      for (unsigned iScope : State.Alive.set_bits())
        OnScope(S, iScope);
      return;
    }
    // A use: a name of what is tracked, also through a local reference to
    // it or a structured binding, read, changed, `++pValue`, moved or given
    // by reference; through a local pointer to a tracked pointer,
    // `**ppValue`; and the object.
    const auto *E = dyn_cast<Expr>(S);
    if (!E)
      return;
    std::optional<unsigned> Used;
    const NamedDecl *Name = nullptr;
    if (isa<CXXThisExpr>(E)) {
      Used = TrackedNamed(E);
    } else if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
      Name = Ref->getDecl();
      Used = TrackedNamed(Ref);
      if (!Used) {
        const Expr *Up = Ref;
        DynTypedNode Node = DynTypedNode::create(*Ref);
        for (int Level = 0; Level < 4; ++Level) {
          const auto Above = Context.getParents(Node);
          const auto *Cast =
              Above.empty() ? nullptr : Above[0].get<ImplicitCastExpr>();
          if (!Cast)
            break;
          Up = Cast;
          Node = Above[0];
        }
        const auto Above = Context.getParents(Node);
        if (const auto *Deref =
                Above.empty() ? nullptr : Above[0].get<UnaryOperator>();
            Deref && Deref->getOpcode() == UO_Deref)
          if (const Expr *Addressed = addressedExpr(Up))
            Used = TrackedNamed(Addressed->IgnoreParenImpCasts());
      }
      if (Used && !IsUse(Ref, Ref->getDecl()->getType()->isReferenceType() ||
                                  isa<BindingDecl>(Ref->getDecl())))
        Used.reset();
    }
    if (Used && State.Stale.test(*Used))
      OnUse(S, *Used, Name);
  };

  // The states at the start of each block, and of each that dispatches to
  // handlers what the elements of the blocks before it in its `try` leave,
  // before and after each (inHandlerTry).
  std::vector<std::optional<CState>> In(Graph->getNumBlockIDs());
  auto Run = [&](const CFGBlock *Block, CState State, CState *Any, FOnUse OnUse,
                 FOnScope OnScope) {
    for (const CFGElement &Element : *Block) {
      const bool InTry = Any && inHandlerTry(Block, Element, Context);
      if (InTry)
        Any->merge(State);
      Step(Element, State, OnUse, OnScope);
      if (InTry)
        Any->merge(State);
    }
    return State;
  };
  std::deque<const CFGBlock *> Work;
  llvm::BitVector Queued(Graph->getNumBlockIDs());
  In[Graph->getEntry().getBlockID()] = Start;
  Work.push_back(&Graph->getEntry());
  Queued.set(Graph->getEntry().getBlockID());
  auto NoUse = [](const Stmt *, unsigned, const NamedDecl *) {};
  auto NoScope = [](const Stmt *, unsigned) {};
  while (!Work.empty()) {
    const CFGBlock *Block = Work.front();
    Work.pop_front();
    Queued.reset(Block->getBlockID());
    CState Any = Empty();
    const CState Out =
        Run(Block, *In[Block->getBlockID()], &Any, NoUse, NoScope);
    for (const CFGBlock::AdjacentBlock &Successor : Block->succs()) {
      const CFGBlock *Next = Successor.getReachableBlock();
      if (!Next)
        continue;
      const CState &Flowing = isHandlerDispatch(Next) ? Any : Out;
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

  // The first use of each variable after a suspension, in the order of the
  // source, which is where it is to be gotten again; the scopes alive at a
  // suspension; and the suspensions that can be reached, by their blocks.
  llvm::MapVector<unsigned, std::pair<SourceLocation, const NamedDecl *>>
      FirstUses;
  llvm::SmallPtrSet<const VarDecl *, 2> ScopesAlive;
  llvm::SmallVector<std::pair<const Stmt *, const CFGBlock *>, 4> Suspensions;
  for (const CFGBlock *Block : *Graph) {
    if (!Reachable.test(Block->getBlockID()) || !In[Block->getBlockID()])
      continue;
    Run(
        Block, *In[Block->getBlockID()], nullptr,
        [&](const Stmt *Use, unsigned Index, const NamedDecl *Name) {
          const SourceLocation Loc = reportLocation(Use->getBeginLoc(), SM);
          if (Loc.isInvalid())
            return;
          auto [It, Inserted] =
              FirstUses.try_emplace(Index, std::make_pair(Loc, Name));
          if (!Inserted && SM.isBeforeInTranslationUnit(Loc, It->second.first))
            It->second = {Loc, Name};
        },
        [&](const Stmt *, unsigned Scope) {
          ScopesAlive.insert(Scopes[Scope]);
        });
    for (const CFGElement &Element : *Block)
      if (const auto Statement = Element.getAs<CFGStmt>();
          Statement && isSuspension(Statement->getStmt()))
        Suspensions.push_back({Statement->getStmt(), Block});
  }
  for (const VarDecl *Scope : Scopes) {
    if (!ScopesAlive.contains(Scope))
      continue;
    const SourceLocation Loc = reportLocation(Scope->getLocation(), SM);
    if (Loc.isValid())
      diag(Loc, "%0 keeps thread-local data of the thread that made it and is "
                "alive across a suspension; the coroutine can resume on "
                "another thread; end its scope before the suspension")
          << Scope;
  }
  // The loops that go on over it, by their range or iterator, and those
  // over it that can go on after a suspension in them, whatever their
  // iterators are: the flow from it reaches the loop again.
  llvm::DenseMap<const VarDecl *, const CXXForRangeStmt *> Loops;
  {
    llvm::SmallVector<const Stmt *, 64> Work{Body};
    while (!Work.empty()) {
      const Stmt *S = Work.pop_back_val();
      if (!S || isa<LambdaExpr>(S))
        continue;
      if (const auto *For = dyn_cast<CXXForRangeStmt>(S))
        for (const DeclStmt *Declaration :
             {For->getRangeStmt(), For->getBeginStmt(), For->getEndStmt()})
          if (const auto *Variable =
                  Declaration
                      ? dyn_cast_or_null<VarDecl>(Declaration->getSingleDecl())
                      : nullptr)
            Loops[Variable] = For;
      for (const Stmt *Child : S->children())
        Work.push_back(Child);
    }
  }
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
  // Whether the flow from \p From reaches \p For again.
  auto Continues = [&](const CFGBlock *From, const CXXForRangeStmt *For) {
    llvm::BitVector Seen(Graph->getNumBlockIDs());
    llvm::SmallVector<const CFGBlock *, 8> Pending{From};
    while (!Pending.empty()) {
      const CFGBlock *Block = Pending.pop_back_val();
      for (const CFGBlock::AdjacentBlock &Successor : Block->succs()) {
        const CFGBlock *Next = Successor.getReachableBlock();
        if (!Next || Seen.test(Next->getBlockID()))
          continue;
        Seen.set(Next->getBlockID());
        if (Next->getTerminatorStmt() == For)
          return true;
        Pending.push_back(Next);
      }
    }
    return false;
  };
  for (const CXXForRangeStmt *For : DataLoops)
    if (For->getBody() && llvm::any_of(Suspensions, [&](const auto &Each) {
          return For->getBody()->getSourceRange().fullyContains(
                     Each.first->getSourceRange()) &&
                 Continues(Each.second, For);
        }))
      ReportLoop(For, SourceLocation());
  for (const auto &[Index, Use] : FirstUses) {
    const auto &[Loc, Name] = Use;
    if (TracksThis && Index == This) {
      diag(Loc, "the object refers to thread-local data of the thread that "
                "ran before the suspension; the coroutine can resume on "
                "another thread; get it again after the suspension");
      continue;
    }
    const VarDecl *Variable = Variables[Index];
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
