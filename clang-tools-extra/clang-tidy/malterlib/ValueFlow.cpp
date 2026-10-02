//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ValueFlow.h"
#include "CheckedAccess.h"
#include "CoroutineUtils.h"
#include "ActorAnnotations.h"
#include "HeldData.h"
#include "LibraryFunctions.h"
#include "NamingUtils.h"
#include "MalterlibOptions.h"
#include "RunsToCompletion.h"
#include "StorageModel.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Analysis/Analyses/Dominators.h"
#include <deque>
#include <functional>
#include <map>
#include <set>
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/MathExtras.h"

// Every switch over a kind of clang's lists all of them, no default, so that
// one added there is an error here until it is classified.
#pragma clang diagnostic error "-Wswitch"

namespace clang::tidy::malterlib {

// How many walks are nested in what is worked out (CProvenance::reach), each
// summary its own.
static thread_local TCSummaryLocal<unsigned> s_Nested;

CFlowEvent eventOf(const CFGElement &Element) {
  CFlowEvent Event;
  switch (Element.getKind()) {
  case CFGElement::Statement:
  case CFGElement::Constructor:
  case CFGElement::CXXRecordTypedCall:
    Event.Kind = EEvent::Statement;
    Event.Statement = Element.castAs<CFGStmt>().getStmt();
    break;
  case CFGElement::Initializer:
    Event.Kind = EEvent::Initializer;
    Event.Initializer = Element.castAs<CFGInitializer>().getInitializer();
    break;
  case CFGElement::NewAllocator:
    Event.Kind = EEvent::NewAllocator;
    Event.Statement = Element.castAs<CFGNewAllocator>().getAllocatorExpr();
    break;
  case CFGElement::LoopExit:
    Event.Kind = EEvent::LoopExit;
    Event.Statement = Element.castAs<CFGLoopExit>().getLoopStmt();
    break;
  case CFGElement::FullExprCleanup: {
    const auto Cleanup = Element.castAs<CFGFullExprCleanup>();
    Event.Kind = EEvent::FullExpressionCleanup;
    Event.Statement = Cleanup.getCleanupExpr();
    Event.Temporaries = Cleanup.getExpiringMTEs();
    break;
  }
  case CFGElement::ScopeBegin: {
    const auto Scope = Element.castAs<CFGScopeBegin>();
    Event.Kind = EEvent::ScopeBegin;
    Event.Variable = Scope.getVarDecl();
    Event.Statement = Scope.getTriggerStmt();
    break;
  }
  case CFGElement::ScopeEnd: {
    const auto Scope = Element.castAs<CFGScopeEnd>();
    Event.Kind = EEvent::ScopeEnd;
    Event.Variable = Scope.getVarDecl();
    Event.Statement = Scope.getTriggerStmt();
    break;
  }
  case CFGElement::LifetimeEnds: {
    const auto Ends = Element.castAs<CFGLifetimeEnds>();
    Event.Kind = EEvent::LifetimeEnds;
    Event.Variable = Ends.getVarDecl();
    Event.Statement = Ends.getTriggerStmt();
    break;
  }
  case CFGElement::AutomaticObjectDtor: {
    const auto Destructor = Element.castAs<CFGAutomaticObjDtor>();
    Event.Kind = EEvent::AutomaticDestructor;
    Event.Variable = Destructor.getVarDecl();
    Event.Statement = Destructor.getTriggerStmt();
    break;
  }
  case CFGElement::DeleteDtor:
    Event.Kind = EEvent::DeleteDestructor;
    Event.Statement = Element.castAs<CFGDeleteDtor>().getDeleteExpr();
    break;
  case CFGElement::BaseDtor:
    Event.Kind = EEvent::BaseDestructor;
    Event.Base = Element.castAs<CFGBaseDtor>().getBaseSpecifier();
    break;
  case CFGElement::MemberDtor:
    Event.Kind = EEvent::MemberDestructor;
    Event.Field = Element.castAs<CFGMemberDtor>().getFieldDecl();
    break;
  case CFGElement::TemporaryDtor:
    Event.Kind = EEvent::TemporaryDestructor;
    Event.Statement =
        Element.castAs<CFGTemporaryDtor>().getBindTemporaryExpr();
    break;
  case CFGElement::CleanupFunction:
    Event.Kind = EEvent::CleanupFunction;
    Event.Variable = Element.castAs<CFGCleanupFunction>().getVarDecl();
    break;
  }
  return Event;
}

llvm::StringRef eventName(EEvent Kind) {
  switch (Kind) {
  case EEvent::Statement:
    return "Statement";
  case EEvent::Initializer:
    return "Initializer";
  case EEvent::NewAllocator:
    return "NewAllocator";
  case EEvent::LoopExit:
    return "LoopExit";
  case EEvent::FullExpressionCleanup:
    return "FullExpressionCleanup";
  case EEvent::ScopeBegin:
    return "ScopeBegin";
  case EEvent::ScopeEnd:
    return "ScopeEnd";
  case EEvent::LifetimeEnds:
    return "LifetimeEnds";
  case EEvent::AutomaticDestructor:
    return "AutomaticDestructor";
  case EEvent::DeleteDestructor:
    return "DeleteDestructor";
  case EEvent::BaseDestructor:
    return "BaseDestructor";
  case EEvent::MemberDestructor:
    return "MemberDestructor";
  case EEvent::TemporaryDestructor:
    return "TemporaryDestructor";
  case EEvent::CleanupFunction:
    return "CleanupFunction";
  }
  return "";
}

std::unique_ptr<CFG> buildFlowGraph(const Decl *Owner, const Stmt *Body,
                                    ASTContext &Context) {
  if (!Body)
    return nullptr;
  CFG::BuildOptions Options;
  Options.setAllAlwaysAdd();
  Options.AddImplicitDtors = true;
  Options.AddTemporaryDtors = true;
  // Past the limit of the uses of a shared default argument, leave out the
  // destructors of the rest, which only end their temporaries.
  Options.OmitRepeatedSharedTemporaryDtors = true;
  Options.AddLifetime = true;
  Options.AddInitializers = true;
  Options.AddCXXNewAllocator = true;
  Options.AddCXXDefaultInitExprInCtors = true;
  std::unique_ptr<CFG> Graph = buildGraph(Owner, Body, Context, Options);
  if (Graph)
    addExceptionEdges(*Graph, Context);
  return Graph;
}

CEffectIndex::CEffectIndex(const Stmt *Root, const ASTContext &Context)
    : Root(Root) {
  if (!Root)
    return;
  // Each use, then the map of them with no more room than they take: most
  // bodies are small.
  llvm::SmallVector<std::pair<const Stmt *, CUse>, 64> Found;
  llvm::SmallVector<const Stmt *, 32> Work{Root};
  llvm::SmallPtrSet<const Stmt *, 32> Seen{Root};
  while (!Work.empty()) {
    const Stmt *S = Work.pop_back_val();
    Statements.push_back(S);
    const CEffects &Of = malterlib::effectsOf(S, Context);
    llvm::SmallVector<const Stmt *, 4> Next;
    for (const COperandEffect &Effect : Of.Operands) {
      Found.push_back({Effect.Operand, {S, &Effect}});
      // Not into the body of a lambda or a block.
      if (Effect.Use == EUse::Unevaluated && isa<LambdaExpr>(S) &&
          Effect.Operand == cast<LambdaExpr>(S)->getBody())
        continue;
      if (isa<BlockExpr>(S))
        continue;
      if (Seen.insert(Effect.Operand).second)
        Next.push_back(Effect.Operand);
    }
    // In the order of the operands.
    Work.append(Next.rbegin(), Next.rend());
  }
  Uses = decltype(Uses)(Found.size());
  for (const auto &[Operand, Use] : Found)
    Uses[Operand].push_back(Use);
  for (const auto &[Operand, Use] : Found)
    if (!Seen.contains(Operand))
      Unindexed.insert(Operand);
}

llvm::ArrayRef<CEffectIndex::CUse>
CEffectIndex::usesOf(const Stmt *Operand) const {
  if (const auto It = Uses.find(Operand); It != Uses.end())
    return It->second;
  return {};
}

llvm::SmallVector<CEffectIndex::CUse, 2>
CEffectIndex::consumersOf(const Stmt *E) const {
  llvm::SmallVector<CUse, 2> Consumers;
  llvm::SmallVector<const Stmt *, 4> Work{E};
  llvm::SmallPtrSet<const Stmt *, 8> Seen{E};
  while (!Work.empty()) {
    const Stmt *Of = Work.pop_back_val();
    for (const CUse &Use : usesOf(Of)) {
      if (Use.Effect->Use == EUse::Forward) {
        if (Seen.insert(Use.Parent).second)
          Work.push_back(Use.Parent);
        continue;
      }
      Consumers.push_back(Use);
    }
  }
  return Consumers;
}

const CEffects *CEffectIndex::effectsOf(const Stmt *S) const {
  // The statements under the root are the root and the operands it was
  // walked into, whose effects effectsOf kept when it was made.
  if (!Root || (S != Root && (!Uses.contains(S) || Unindexed.contains(S))))
    return nullptr;
  return &keptEffectsOf(S);
}

llvm::SmallVector<const Stmt *, 2>
CEffectIndex::unclassifiedChildren(const Stmt *S) const {
  llvm::SmallVector<const Stmt *, 2> Missing;
  const CEffects *Of = effectsOf(S);
  if (!Of)
    return Missing;
  llvm::SmallPtrSet<const Stmt *, 8> Operands;
  for (const COperandEffect &Effect : Of->Operands)
    Operands.insert(Effect.Operand);
  for (const Stmt *Child : S->children())
    if (Child && !Operands.contains(Child))
      Missing.push_back(Child);
  return Missing;
}

void CEffectIndex::evaluate(const Stmt *S,
                            llvm::SmallVectorImpl<const Stmt *> &Order,
                            llvm::SmallPtrSetImpl<const Stmt *> &Seen,
                            int Depth) const {
  if (stackNearlyExhausted())
    return withStack([&] { evaluate(S, Order, Seen, Depth); });
  const CEffects *Of = effectsOf(S);
  if (!Of || Depth > 256 || !Seen.insert(S).second)
    return;
  // The operands that run, each once, by their sequence, then it.
  llvm::SmallVector<const COperandEffect *, 4> Operands;
  llvm::SmallPtrSet<const Stmt *, 8> Listed;
  for (const COperandEffect &Effect : Of->Operands)
    if (Effect.Use != EUse::Unevaluated && Effect.Operand &&
        Listed.insert(Effect.Operand).second)
      Operands.push_back(&Effect);
  llvm::stable_sort(Operands,
                    [](const COperandEffect *Left, const COperandEffect *Right) {
                      return Left->Sequence < Right->Sequence;
                    });
  for (const COperandEffect *Effect : Operands)
    evaluate(Effect->Operand, Order, Seen, Depth + 1);
  Order.push_back(S);
}

llvm::SmallVector<const Stmt *, 8>
CEffectIndex::evaluationOf(const Stmt *Default) const {
  llvm::SmallVector<const Stmt *, 8> Order;
  const CEffects *Of = effectsOf(Default);
  if (!Of || Of->Statement != EStatement::Default)
    return Order;
  // Each use of a default is a node of its own over the one expression it
  // evaluates: of a use that isn't in that expression, the evaluation is the
  // same for each, found once. Not of one in it, a default that uses itself,
  // which its own evaluation leaves out.
  const Stmt *Shared =
      Of->Operands.size() == 1 ? Of->Operands.front().Operand : nullptr;
  if (Shared) {
    llvm::SmallVector<const Stmt *, 8> Up{Default};
    llvm::SmallPtrSet<const Stmt *, 8> Climbed{Default};
    while (!Up.empty() && Shared) {
      const Stmt *At = Up.pop_back_val();
      if (At == Shared)
        Shared = nullptr;
      for (const CUse &Use : usesOf(At))
        if (Climbed.insert(Use.Parent).second)
          Up.push_back(Use.Parent);
    }
  }
  if (Shared)
    if (const auto It = Evaluations.find(Shared); It != Evaluations.end())
      return It->second;
  llvm::SmallPtrSet<const Stmt *, 16> Seen{Default};
  for (const COperandEffect &Effect : Of->Operands)
    evaluate(Effect.Operand, Order, Seen, 0);
  if (Shared)
    Evaluations.try_emplace(Shared, Order);
  return Order;
}

bool CEffectIndex::evaluated(const Stmt *S) const {
  if (stackNearlyExhausted())
    return withStack([&] { return evaluated(S); });
  if (const auto It = Evaluated.find(S); It != Evaluated.end())
    return It->second;
  // Sized for the statements under the root, most of which are asked about.
  if (Evaluated.empty())
    Evaluated = decltype(Evaluated)(Statements.size());
  // By any of its uses, of what is evaluated in several places, a default
  // argument at each of its uses; while it is decided, not.
  const llvm::ArrayRef<CUse> Uses = usesOf(S);
  if (Uses.empty())
    return Evaluated[S] = true;
  Evaluated[S] = false;
  const bool Is = llvm::any_of(Uses, [&](const CUse &Use) {
    return Use.Effect->Use != EUse::Unevaluated && evaluated(Use.Parent);
  });
  Evaluated[S] = Is;
  return Is;
}

SourceLocation CEffectIndex::locationOf(const Stmt *S) const {
  if (!S)
    return {};
  // Into what it only gives on, to a default it is.
  const Stmt *Start = S;
  for (const Stmt *Inner = S; Inner;) {
    const CEffects *Of = effectsOf(Inner);
    if (!Of)
      break;
    if (Of->Statement == EStatement::Default) {
      Start = Inner;
      break;
    }
    if (Of->Operands.size() != 1 || Of->Operands.front().Use != EUse::Forward)
      break;
    Inner = Of->Operands.front().Operand;
  }
  // Out to the outermost default it is evaluated for, also of a default in
  // a default, and the first that has a location.
  SourceLocation First;
  const Stmt *Outermost = nullptr;
  const Stmt *Outer = Start;
  for (int Level = 0; Outer && Level < 1024; ++Level) {
    const CEffects *Of = effectsOf(Outer);
    if (Of && Of->Statement == EStatement::Default)
      Outermost = Outer;
    if (First.isInvalid() && Outer != Start)
      First = Outer->getBeginLoc();
    else if (First.isInvalid())
      First = S->getBeginLoc();
    const llvm::ArrayRef<CUse> Uses = usesOf(Outer);
    Outer = Uses.empty() ? nullptr : Uses.front().Parent;
  }
  return Outermost ? usedLocation(Outermost) : First;
}

void CFlowCache::check(ASTContext &Context) {
  if (const uint64_t AST = astIdentity(Context); IndexedAST != AST) {
    IndexedAST = AST;
    Summaries.reset();
    Graphs.clear();
    Indexes.clear();
    Returns.clear();
    Names.clear();
    WrapperFlows.clear();
    Summaries = std::make_unique<CFlowSummaries>(Context);
  }
}

CFlowSummaries &CFlowCache::summaries(ASTContext &Context) {
  check(Context);
  return *Summaries;
}

const CFG *CFlowCache::graphOf(const Decl *Owner, const Stmt *Body,
                               ASTContext &Context) {
  check(Context);
  if (auto Found = Graphs.find(Body); Found == Graphs.end() || !Found->second) {
    auto Made = std::make_unique<CGraph>();
    Made->Graph = buildFlowGraph(Owner, Body, Context);
    if (Made->Graph) {
      Made->Reachable = reachableBlocks(*Made->Graph);
      for (const auto &[Synthetic, Original] : Made->Graph->synthetic_stmts())
        Made->Synthetic[Synthetic] = Original;
    }
    // Added once made: making it can add others, which moves the entries.
    std::unique_ptr<CGraph> &Slot = Graphs[Body];
    if (!Slot)
      Slot = std::move(Made);
  }
  return Graphs.find(Body)->second->Graph.get();
}

const llvm::BitVector &CFlowCache::reachableOf(const Decl *Owner,
                                               const Stmt *Body,
                                               ASTContext &Context) {
  graphOf(Owner, Body, Context);
  return Graphs.find(Body)->second->Reachable;
}

const CEffectIndex &CFlowCache::indexOf(const Stmt *Root,
                                        ASTContext &Context) {
  check(Context);
  if (auto Found = Indexes.find(Root); Found != Indexes.end() && Found->second)
    return *Found->second;
  // Added once made, like a graph.
  auto Made = std::make_unique<CEffectIndex>(Root, Context);
  std::unique_ptr<CEffectIndex> &Index = Indexes[Root];
  if (!Index)
    Index = std::move(Made);
  return *Index;
}

const llvm::DenseSet<const Stmt *> *
CFlowCache::running(const Decl *Owner, const Stmt *Body, ASTContext &Context) {
  const CFG *Graph = graphOf(Owner, Body, Context);
  if (!Graph)
    return nullptr;
  if (Graphs.find(Body)->second->Running)
    return Graphs.find(Body)->second->Running.get();
  const CEffectIndex &Index = indexOf(Body, Context);
  CGraph &Of = *Graphs.find(Body)->second;
  Of.Running = std::make_unique<llvm::DenseSet<const Stmt *>>();
  for (const CFGBlock *Block : *Graph) {
    if (!Block || !Of.Reachable.test(Block->getBlockID()))
      continue;
    for (const CFGElement &Element : *Block) {
      const CFlowEvent Event = eventOf(Element);
      switch (Event.Kind) {
      // Not what isn't evaluated, `__builtin_constant_p(...)`.
      // Of what isn't in the body, a member initializer, by an index of its
      // own.
      case EEvent::Statement: {
        if (Index.effectsOf(Event.Statement) &&
            !Index.evaluated(Event.Statement))
          break;
        Of.Running->insert(Event.Statement);
        const CEffectIndex &Own = Index.effectsOf(Event.Statement)
                                      ? Index
                                      : indexOf(Event.Statement, Context);
        for (const Stmt *Evaluated : Own.evaluationOf(Event.Statement))
          Of.Running->insert(Evaluated);
        if (const Stmt *Original = Of.Synthetic.lookup(Event.Statement))
          Of.Running->insert(Original);
        break;
      }
      case EEvent::NewAllocator:
        Of.Running->insert(Event.Statement);
        break;
      case EEvent::Initializer:
        if (Event.Initializer && Event.Initializer->getInit())
          Of.Running->insert(Event.Initializer->getInit());
        break;
      case EEvent::LoopExit:
      case EEvent::FullExpressionCleanup:
      case EEvent::ScopeBegin:
      case EEvent::ScopeEnd:
      case EEvent::LifetimeEnds:
      case EEvent::AutomaticDestructor:
      case EEvent::DeleteDestructor:
      case EEvent::BaseDestructor:
      case EEvent::MemberDestructor:
      case EEvent::TemporaryDestructor:
      case EEvent::CleanupFunction:
        break;
      }
    }
  }
  return Of.Running.get();
}

void forEachRunning(const Decl *Owner, const Stmt *Body, const Stmt *Root,
                    ASTContext &Context,
                    llvm::function_ref<void(const Stmt *)> Visit) {
  CFlowCache &Cache = sharedFlowCache();
  const CEffectIndex &Index = Cache.indexOf(Root, Context);
  const llvm::DenseSet<const Stmt *> *Running =
      Cache.running(Owner, Body, Context);
  for (const Stmt *S : Index.statements())
    if (Running ? Running->contains(S) : Index.evaluated(S))
      Visit(S);
}

llvm::ArrayRef<const Expr *> CFlowCache::returnsOf(const FunctionDecl *Function,
                                                   ASTContext &Context) {
  check(Context);
  if (const auto It = Returns.find(Function); It != Returns.end())
    return *It->second;
  // Made before it is added, its own allocation.
  auto Values = std::make_unique<llvm::SmallVector<const Expr *, 4>>();
  if (const Stmt *Body = Function->getBody()) {
    const CEffectIndex &Index = indexOf(Body, Context);
    forEachRunning(Function, Body, Body, Context, [&](const Stmt *S) {
      if (Index.effectsOf(S)->Statement == EStatement::Return)
        if (const Expr *Value = cast<ReturnStmt>(S)->getRetValue())
          Values->push_back(Value);
    });
  }
  std::unique_ptr<llvm::SmallVector<const Expr *, 4>> &Cached =
      Returns[Function];
  if (!Cached)
    Cached = std::move(Values);
  return *Cached;
}

bool onlyCallsParameter(const FunctionDecl *Callee, unsigned iParameter,
                        ASTContext &Context) {
  return sharedFlowCache().onlyCallsParameter(Callee, iParameter, Context);
}

llvm::ArrayRef<const Expr *> writtenReturns(const FunctionDecl *Function,
                                            ASTContext &Context) {
  return sharedFlowCache().returnsOf(Function, Context);
}

bool mayRunOverride(const CallExpr *Call, const CXXMethodDecl *Method) {
  return callsOverride(Call) ||
         (Method && Method->isVirtual() && !Call->getDirectCallee() &&
          !Method->hasAttr<FinalAttr>() &&
          !Method->getParent()->hasAttr<FinalAttr>());
}

bool canRun(const Decl *Owner, const Stmt *Body, const Stmt *S,
            ASTContext &Context) {
  CFlowCache &Cache = sharedFlowCache();
  const llvm::DenseSet<const Stmt *> *Running =
      Cache.running(Owner, Body, Context);
  return Running ? Running->contains(S)
                 : Cache.indexOf(Body, Context).evaluated(S);
}

const Stmt *CFlowCache::originalOf(const Decl *Owner, const Stmt *Body,
                                   const Stmt *S, ASTContext &Context) {
  if (!graphOf(Owner, Body, Context))
    return nullptr;
  return Graphs.find(Body)->second->Synthetic.lookup(S);
}

//===----------------------------------------------------------------------===//
// The local slots of a function (localSlotValues).
//===----------------------------------------------------------------------===//

namespace {
using CPath = std::vector<const FieldDecl *>;

/// Whether \p Prefix is the start of \p Path.
bool startsWith(llvm::ArrayRef<const FieldDecl *> Path,
                llvm::ArrayRef<const FieldDecl *> Prefix) {
  return Prefix.size() <= Path.size() &&
         std::equal(Prefix.begin(), Prefix.end(), Path.begin());
}

} // namespace

bool crossesPointer(llvm::ArrayRef<const FieldDecl *> Path, size_t From) {
  for (size_t iField = From ? From - 1 : 0; iField + 1 < Path.size();
       ++iField) {
    const QualType Type = Path[iField]->getType();
    const auto *Held =
        Type->isReferenceType() ? nullptr : Type->getAsCXXRecordDecl();
    const auto *Of = dyn_cast<CXXRecordDecl>(Path[iField + 1]->getParent());
    if (!Held || !Of ||
        (Held->getCanonicalDecl() != Of->getCanonicalDecl() &&
         !(Held->hasDefinition() && Held->isDerivedFrom(Of))))
      return true;
  }
  return false;
}

// What the members \p Path of what \p Made makes are made with: of an
// aggregate, `CView{&t_Value}`; of a constructor whose body can be seen, what
// it gives the member of a parameter, the argument, `CView(int *&_pValue) :
// m_pValue(_pValue) {}`, and of one whose body does nothing but initialize,
// a member made of nothing it is given; else null.
const Expr *projected(const Expr *Made, llvm::ArrayRef<const FieldDecl *> Path) {
  // Of a member a constructor whose body is seen makes as an aggregate, the
  // constructor and its call, whose parameters the members are given.
  const CXXConstructorDecl *InConstructor = nullptr;
  const CXXConstructExpr *InConstruct = nullptr;
  for (size_t iField = 0; iField < Path.size(); ++iField) {
    const FieldDecl *Field = Path[iField];
    if (!Made)
      return nullptr;
    Made = Made->IgnoreImplicit();
    // A copy or a move assignment the compiler defines gives each member
    // what it copies gives it, `Pair = CPair{_pJob, 1}`.
    for (int Steps = 0; Steps < 4; ++Steps) {
      const Expr *Copied = copiedByDefinedAssignment(
          Made, const_cast<ASTContext &>(Field->getASTContext()));
      if (!Copied)
        break;
      Made = Copied->IgnoreImplicit();
    }
    if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Made))
      Made = Cast->getSubExpr()->IgnoreImplicit();
    if (const auto *Literal = dyn_cast<CompoundLiteralExpr>(Made))
      Made = Literal->getInitializer()->IgnoreImplicit();
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(Made)) {
      if (InConstructor)
        return nullptr;
      const CXXConstructorDecl *Constructor = Construct->getConstructor();
      const auto *Definition =
          Constructor ? dyn_cast_or_null<CXXConstructorDecl>(
                            Constructor->getDefinition())
                      : nullptr;
      // A reference member bound to a reference parameter, or a member
      // given the value of a parameter held by value, `m_pValue(_pValue)`.
      const ASTContext &Context = Field->getASTContext();
      auto ParameterOf = [&](const Expr *Init) -> const ParmVarDecl * {
        if (const auto *Named =
                dyn_cast_or_null<ParmVarDecl>(namedDecl(Init, Context)))
          return Named;
        // An owner member copied or moved from a parameter, `m_pJob(_pJob)`
        // or `m_pJob(fg_Move(_pJob))`, by the kind of the member's type
        // (OwnerTypes): a copy of a shared owner keeps what it owns, a move
        // of any (a copy of a sharedbuffer one is a value of its own); what
        // is given at the call is copied or moved the same way.
        if (const auto *Copy = dyn_cast<CXXConstructExpr>(Init->IgnoreImplicit());
            Copy && Copy->getNumArgs() == 1 &&
            Copy->getConstructor()->isCopyOrMoveConstructor()) {
          const EOwnerKind Kind = ownerKindOf(Field->getType(), Context);
          const Expr *From = Copy->getArg(0)->IgnoreParenImpCasts();
          if (const auto *Move = dyn_cast<CallExpr>(From);
              Move && Move->getNumArgs() == 1 &&
              !isMemberOrOperatorCall(Move) &&
              vocabulary().MoveOrForwardFunctions.contains(calleeName(Move)))
            From = Move->getArg(0)->IgnoreParenImpCasts();
          const auto *Ref = dyn_cast<DeclRefExpr>(From);
          if (Ref && Kind != EOwnerKind::None && Kind != EOwnerKind::Weak &&
              (Kind == EOwnerKind::Shared ||
               Copy->getConstructor()->isMoveConstructor()))
            return dyn_cast<ParmVarDecl>(Ref->getDecl());
        }
        const CEffects &Of = effectsOf(Init, Context);
        if (Of.Operands.size() != 1 || Of.Operands.front().Use != EUse::Access)
          return nullptr;
        const auto *Loaded = dyn_cast_or_null<ParmVarDecl>(namedDecl(
            dyn_cast<Expr>(Of.Operands.front().Operand), Context));
        return Loaded && !Loaded->getType()->isReferenceType() ? Loaded
                                                                : nullptr;
      };
      // A member made of nothing the constructor is given, by its default
      // member initializer or its initializer, a null pointer, `CJob *m_pJob
      // = nullptr;`, or a default construction, `TCPointer<CJob> m_pJob;`,
      // by a constructor whose body does nothing else.
      const auto *Body =
          Definition ? dyn_cast_or_null<CompoundStmt>(Definition->getBody())
                     : nullptr;
      const bool OnlyInitializes = Body && Body->body_empty();
      // Of a pointer or a reference member, only when what the code of its
      // class gives it is all it is ever given (targetToldByOwnClass).
      auto MadeOfNothing = [&](const Expr *Init) -> const Expr * {
        if (!OnlyInitializes || !targetToldByOwnClass(Field))
          return nullptr;
        Init = Init->IgnoreImplicit();
        if (const auto *Default = dyn_cast<CXXDefaultInitExpr>(Init))
          Init = Default->getExpr()->IgnoreImplicit();
        if (isa<ImplicitValueInitExpr, CXXScalarValueInitExpr>(Init) ||
            nullPointerConstantOf(Init, const_cast<ASTContext &>(Context),
                                  Expr::NPC_ValueDependentIsNotNull) !=
                Expr::NPCK_NotNull)
          return Init;
        if (const auto *Default = dyn_cast<CXXConstructExpr>(Init);
            Default && Default->getNumArgs() == 0)
          return Init;
        return nullptr;
      };
      // What an initializer gives the rest of the path is what it holds
      // after the constructor only where the body doesn't give it another
      // value on a way: what reaches the exit of the body is only what it
      // held where the body was entered (thisMemberDefinitions).
      auto BodyKeeps = [&]() {
        if (OnlyInitializes)
          return true;
        const std::optional<llvm::SmallVector<CThisDefinition, 2>> Reaching =
            thisMemberDefinitions(
                {Definition,
                 llvm::SmallVector<const FieldDecl *, 2>(Path.begin() + iField,
                                                         Path.end()),
                 nullptr},
                nullptr, const_cast<ASTContext &>(Context));
        return Reaching && !Reaching->empty() &&
               llvm::all_of(*Reaching, [](const CThisDefinition &Each) {
                 return Each.Entry;
               });
      };
      const Expr *Given = nullptr;
      if (Definition && Definition->getParent() == Field->getParent() &&
          BodyKeeps())
        for (const CXXCtorInitializer *Initializer : Definition->inits())
          if (Initializer->getMember() == Field && Initializer->getInit()) {
            const Expr *Init = Initializer->getInit()->IgnoreImplicit();
            if (const ParmVarDecl *Parameter =
                    ParameterOf(Initializer->getInit());
                Parameter && Parameter->getDeclContext() == Definition &&
                Parameter->getFunctionScopeIndex() < Construct->getNumArgs())
              Given = Construct->getArg(Parameter->getFunctionScopeIndex());
            else if (isa<InitListExpr, CXXParenListInitExpr>(Init) &&
                     iField + 1 < Path.size()) {
              // A member that is a record the initializer makes as an
              // aggregate, `m_Read{_pInput, _pFallback}`: its members are
              // what that gives them, each parameter what the call gives it.
              Given = Init;
              InConstructor = Definition;
              InConstruct = Construct;
            } else
              Given = MadeOfNothing(Initializer->getInit());
          }
      Made = Given;
      continue;
    }
    if (!isa<InitListExpr, CXXParenListInitExpr>(Made) ||
        Made->getType()->getAsRecordDecl() != Field->getParent())
      return nullptr;
    Made = aggregateMemberInit(Made, Field);
    // A pointer or a reference member an aggregate isn't given is what its
    // default member initializer, the code of its class, gives it.
    if (Made && isa<CXXDefaultInitExpr>(Made->IgnoreParenImpCasts()) &&
        !targetToldByOwnClass(Field))
      return nullptr;
  }
  if (!Made || !InConstructor)
    return Made;
  // In the constructor's aggregate: a parameter of it is what the call gives
  // it, a value made of nothing it is given is itself, anything else can't
  // be told here.
  const Expr *Leaf = Made->IgnoreParenImpCasts();
  if (const auto *Named = dyn_cast<DeclRefExpr>(Leaf))
    if (const auto *Parameter = dyn_cast<ParmVarDecl>(Named->getDecl());
        Parameter && Parameter->getDeclContext() == InConstructor &&
        !Parameter->getType()->isReferenceType())
      return Parameter->getFunctionScopeIndex() < InConstruct->getNumArgs()
                 ? InConstruct->getArg(Parameter->getFunctionScopeIndex())
                 : nullptr;
  bool Given = false;
  forEachStmtOnce(Leaf, [&](const Stmt *Each) {
    if (isa<CXXThisExpr>(Each)) {
      Given = true;
      return EWalk::Stop;
    }
    if (const auto *Named = dyn_cast<DeclRefExpr>(Each);
        Named && isa<ParmVarDecl>(Named->getDecl())) {
      Given = true;
      return EWalk::Stop;
    }
    return EWalk::Descend;
  });
  return Given ? nullptr : Made;
}

const FieldDecl *boundMember(const BindingDecl *Binding) {
  const auto *Member =
      Binding->getBinding()
          ? dyn_cast<MemberExpr>(Binding->getBinding()->IgnoreParenImpCasts())
          : nullptr;
  return Member ? dyn_cast<FieldDecl>(Member->getMemberDecl()) : nullptr;
}

namespace {

/// Of each function, the variables the generic lambdas made in it copy, and
/// those the other coroutine lambdas made in it copy (slotFunction), found
/// once.
struct CGenericCopies {
  struct CCopies {
    llvm::DenseSet<const ValueDecl *> Generic;
    llvm::DenseSet<const ValueDecl *> Coroutine;
  };
  llvm::DenseMap<const FunctionDecl *, CCopies> Of;
};

/// The function whose flow \p Variable is a slot of where \p At is: the call
/// operator of the closure around it that copies it (closureCopying), or the
/// specialization of the call operator of a generic lambda that copies it
/// whose body \p At is in, else the function it is a local of.
/// The call operator of the closure of a coroutine lambda that copies \p
/// Variable, whose body \p At is in, also in a closure in it that refers to
/// it: the body of a coroutine is its call operator's, which isn't under the
/// lambda expression as the body it is written with is (closureCopying). Not
/// of a generic lambda, whose specializations slotFunction finds.
static const FunctionDecl *coroutineClosureCopying(const VarDecl *Variable,
                                                   const Stmt *At) {
  ASTContext &Context = Variable->getASTContext();
  DynTypedNode Node = DynTypedNode::create(*At);
  for (int Step = 0; Step < 4096; ++Step) {
    const auto Parents = Context.getParents(Node);
    if (Parents.empty())
      return nullptr;
    if (const auto *Method = Parents[0].get<CXXMethodDecl>()) {
      if (!Method->getParent()->isLambda() ||
          !isa_and_nonnull<CoroutineBodyStmt>(Method->getBody()) ||
          Method->getDescribedFunctionTemplate() ||
          Method->getTemplateInstantiationPattern())
        return nullptr;
      const CXXRecordDecl *Class = Method->getParent();
      return llvm::any_of(Class->captures(),
                          [&](const LambdaCapture &Capture) {
                            return Capture.capturesVariable() &&
                                   Capture.getCaptureKind() == LCK_ByCopy &&
                                   Capture.getCapturedVar() == Variable;
                          })
                 ? Method
                 : nullptr;
    }
    if (Parents[0].get<Decl>())
      return nullptr;
    Node = Parents[0];
  }
  return nullptr;
}

const FunctionDecl *slotFunction(const VarDecl *Variable, const Stmt *At) {
  const auto *Local =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  if (!At)
    return Local;
  if (const LambdaExpr *Closure = closureCopying(Variable, At))
    return Closure->getCallOperator();
  if (!Local || !Local->getBody())
    return Local;
  ASTContext &Context = Variable->getASTContext();
  const auto Found = astCache<CGenericCopies>(Context).Of.try_emplace(Local);
  CGenericCopies::CCopies &Copies = Found.first->second;
  if (Found.second)
    forEachRunning(
        Local, Local->getBody(), Local->getBody(), Context, [&](const Stmt *S) {
          const auto *Lambda = dyn_cast<LambdaExpr>(S);
          if (!Lambda)
            return;
          const CXXMethodDecl *Operator = Lambda->getCallOperator();
          llvm::DenseSet<const ValueDecl *> *Into =
              Operator->getDescribedFunctionTemplate() ? &Copies.Generic
              : isa_and_nonnull<CoroutineBodyStmt>(Operator->getBody())
                  ? &Copies.Coroutine
                  : nullptr;
          if (!Into)
            return;
          for (const LambdaCapture &Capture : Lambda->captures())
            if (Capture.capturesVariable() &&
                Capture.getCaptureKind() == LCK_ByCopy)
              Into->insert(Capture.getCapturedVar());
        });
  if (Copies.Coroutine.contains(Variable))
    if (const FunctionDecl *Coroutine = coroutineClosureCopying(Variable, At))
      return Coroutine;
  if (!Copies.Generic.contains(Variable))
    return Local;
  // Up the instantiated parents, which reach the specialization the body of
  // which shares what isn't dependent with the pattern's.
  DynTypedNode Node = DynTypedNode::create(*At);
  for (int Step = 0; Step < 4096; ++Step) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      break;
    if (const auto *Running = Parents[0].get<CXXMethodDecl>()) {
      if (Running->getTemplateInstantiationPattern() &&
          Running->getParent()->isLambda() &&
          llvm::any_of(Running->getParent()->captures(),
                       [&](const LambdaCapture &Capture) {
                         return Capture.capturesVariable() &&
                                Capture.getCaptureKind() == LCK_ByCopy &&
                                Capture.getCapturedVar() == Variable;
                       }))
        return Running;
      break;
    }
    if (Parents[0].get<Decl>() && !Parents[0].get<VarDecl>())
      break;
    Node = Parents[0];
  }
  return Local;
}

} // namespace

// The temporary the local reference \p Variable extends, which is its own
// storage, a slot (CSlot), `auto &&pValue = &g_Value;` (extendedTemporary);
// null otherwise.
const MaterializeTemporaryExpr *extendedSlot(const VarDecl *Variable) {
  return Variable && Variable->getType()->isReferenceType() &&
                 Variable->hasLocalStorage() && !isa<ParmVarDecl>(Variable)
             ? extendedTemporary(Variable)
             : nullptr;
}

namespace {
/// Whether \p Variable is a local slot of some function: a local that isn't
/// a reference, also a parameter held by value and an init capture, and the
/// temporary a local reference extends (extendedSlot).
bool isSlotVariable(const VarDecl *Variable) {
  return Variable &&
         ((!Variable->getType()->isReferenceType() &&
           (Variable->hasLocalStorage() || Variable->isInitCapture())) ||
          extendedSlot(Variable));
}

} // namespace

std::optional<CSlot> bindingSlotOf(const BindingDecl *Binding) {
  const auto *Bound =
      Binding ? dyn_cast_or_null<MemberExpr>(Binding->getBinding()) : nullptr;
  const auto *Field = Bound ? dyn_cast<FieldDecl>(Bound->getMemberDecl()) : nullptr;
  const auto *Of =
      Field ? dyn_cast<DeclRefExpr>(Bound->getBase()->IgnoreImpCasts()) : nullptr;
  const auto *Decomposed =
      Of ? dyn_cast<DecompositionDecl>(Of->getDecl()) : nullptr;
  if (!Decomposed || !isSlotVariable(Decomposed))
    return std::nullopt;
  return CSlot{Decomposed, {Field}, false};
}

const BindingDecl *bindingCopiedBy(const Expr *Made) {
  const Expr *E = Made ? Made->IgnoreImplicit() : nullptr;
  if (const auto *Construct = dyn_cast_or_null<CXXConstructExpr>(E);
      Construct && Construct->getNumArgs() == 1 &&
      Construct->getConstructor()->isCopyOrMoveConstructor())
    E = Construct->getArg(0)->IgnoreImplicit();
  const auto *Named = dyn_cast_or_null<DeclRefExpr>(E);
  return Named ? dyn_cast<BindingDecl>(Named->getDecl()) : nullptr;
}

const InitListExpr *elementListGivenTo(const CXXConstructExpr *Construct) {
  if (!Construct || Construct->getNumArgs() < 1 ||
      !llvm::all_of(llvm::drop_begin(Construct->arguments()),
                    [](const Expr *Argument) {
                      return isa<CXXDefaultArgExpr>(Argument);
                    }))
    return nullptr;
  const auto *Given = dyn_cast<CXXStdInitializerListExpr>(
      Construct->getArg(0)->IgnoreImplicit());
  return Given ? dyn_cast<InitListExpr>(Given->getSubExpr()->IgnoreImplicit())
               : nullptr;
}

namespace {
/// Whether a parameter of \p Type, a reference or a pointer, lets what is
/// called change what it is given: not of what is const.
bool changesThrough(QualType Type) {
  if (Type.isNull())
    return true;
  if (!Type->isReferenceType() && !Type->isPointerType())
    return false;
  return !pointsToConst(Type);
}
} // namespace

static llvm::SmallVector<CSlot, 2>
namedSlots(const Expr *Target, ASTContext &Context, bool *Told);

namespace {
/// What a designation is asked for (designationOf): the slots a location is
/// named as, without what pointers hold (Named); also through what they hold
/// (Values); and also the object of the function and what a parameter refers
/// to (Places); or the object and what a reference parameter refers to
/// without what pointers hold, which asks no flow (NamedPlaces).
enum class EDesignation : unsigned { Named, Values, Places, NamedPlaces };
} // namespace

static llvm::SmallVector<CSlot, 2> designation(const Expr *E,
                                               EDesignation Mode,
                                               ASTContext &Context,
                                               bool &Told);

// Whether an object of \p Class can run what it is made with where it ends,
// a guard of a scope exit: its destructor isn't trivial, and, where its body
// can be seen, calls a member function of it or what a member of it holds,
// `~CGuard() { m_fFunction(); }`; not a copy of a closure, nor a callback
// that only keeps it.
bool runsWhereItEnds(const CXXRecordDecl *Class, ASTContext &Context) {
  if (!Class || !Class->hasDefinition() || Class->hasTrivialDestructor())
    return false;
  const CXXDestructorDecl *Destructor = Class->getDestructor();
  const FunctionDecl *Definition =
      Destructor ? Destructor->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody())
    return true;
  bool Calls = false;
  forEachRunning(
      Definition, Definition->getBody(), Definition->getBody(), Context,
      [&](const Stmt *S) {
        const CEffects &Effects = effectsOf(S, Context);
        if (Calls || Effects.Statement != EStatement::Call)
          return;
        for (const COperandEffect &Effect : Effects.Operands) {
          const auto *Operand = dyn_cast<Expr>(Effect.Operand);
          if (!Operand ||
              (Effect.Use != EUse::CallObject && Effect.Use != EUse::Callee))
            continue;
          const Expr *Object = sameValue(Operand, Context);
          // The object an explicit object is given, `*this` of `f_Run()`.
          if (const auto *Deref =
                  dyn_cast<UnaryOperator>(Object->IgnoreParenImpCasts());
              Deref && Deref->getOpcode() == UO_Deref)
            Object = sameValue(Deref->getSubExpr(), Context);
          const EStatement Kind = effectsOf(Object, Context).Statement;
          const auto *Field =
              dyn_cast_or_null<FieldDecl>(memberDecl(Object, Context));
          const auto *Owner =
              Field ? dyn_cast<CXXRecordDecl>(Field->getParent()) : nullptr;
          Calls |= Kind == EStatement::This ||
                   (Owner && (Owner == Class || Class->isDerivedFrom(Owner)));
        }
      });
  return Calls;
}

// The call operators of the closure of \p Lambda that have bodies: its own,
// of a generic lambda each specialization made of its template, which are
// what is called, not the pattern.
llvm::SmallVector<const FunctionDecl *, 2>
callOperatorsOf(const LambdaExpr *Lambda) {
  llvm::SmallVector<const FunctionDecl *, 2> Operators;
  if (const FunctionTemplateDecl *Template =
          Lambda->getDependentCallOperator()) {
    for (const FunctionDecl *Specialization : Template->specializations())
      if (Specialization->getBody())
        Operators.push_back(Specialization);
  } else if (const CXXMethodDecl *Operator = Lambda->getCallOperator();
             Operator && Operator->getBody())
    Operators.push_back(Operator);
  return Operators;
}

// Whether calling \p Callee on a closure, or with it, only looks at it: a
// comparison, `!`, or a member function of a const object without parameters
// that gives a `bool`, a conversion to `bool` or `f_IsEmpty()`.
static bool inspects(const FunctionDecl *Callee) {
  if (!Callee)
    return false;
  switch (Callee->getOverloadedOperator()) {
  case OO_EqualEqual:
  case OO_ExclaimEqual:
  case OO_Less:
  case OO_Greater:
  case OO_LessEqual:
  case OO_GreaterEqual:
  case OO_Spaceship:
  case OO_Exclaim:
    // A comparison gives what it tells; one that gives nothing, like the
    // continuation of a future, `Future > [&] { ... }`, keeps what it is
    // given.
    return !Callee->getReturnType()->isVoidType();
  default:
    break;
  }
  const auto *Method = dyn_cast<CXXMethodDecl>(Callee);
  return Method && !Method->isStatic() && hasConstObject(Method) &&
         Method->getNumParams() ==
             (Method->isExplicitObjectMemberFunction() ? 1u : 0u) &&
         Method->getReturnType()->isBooleanType();
}

// What a call of a member function, with the effects \p Effects, does to the
// function object of SynchronousFunctionTypes it is called on, of the type
// \p Object before a conversion to a base, or of the member function's
// class: gives it another callable, `operator =`, or clears it, a member
// function that can change it, without parameters, that gives nothing,
// `f_Clear()`, also one of a base of its class.
enum class EWrapperChange { None, Assign, Clear };
static EWrapperChange wrapperChangeOf(const CEffects *Effects, QualType Object,
                                      ASTContext &Context) {
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Effects ? Effects->Callee : nullptr);
  if (!Method || Method->isStatic() || isa<CXXConstructorDecl>(Method) ||
      isa<CXXDestructorDecl>(Method))
    return EWrapperChange::None;
  if (!isSynchronousFunctionType(
          Context.getCanonicalTagType(Method->getParent()), Context) &&
      (Object.isNull() || !isSynchronousFunctionType(Object, Context)))
    return EWrapperChange::None;
  if (Method->getOverloadedOperator() == OO_Equal)
    return EWrapperChange::Assign;
  if (Method->getOverloadedOperator() == OO_None &&
      !isa<CXXConversionDecl>(Method) && !hasConstObject(Method) &&
      Method->getNumParams() ==
          (Method->isExplicitObjectMemberFunction() ? 1u : 0u) &&
      Method->getReturnType()->isVoidType())
    return EWrapperChange::Clear;
  return EWrapperChange::None;
}

// What a change of a function object of SynchronousFunctionTypes, a call
// with the effects \p Effects, gives it (wrapperChangeOf): of an assignment,
// what it is given; null of a clear.
static const Expr *wrapperGiven(const CEffects *Effects, QualType Object,
                                ASTContext &Context) {
  if (wrapperChangeOf(Effects, Object, Context) != EWrapperChange::Assign)
    return nullptr;
  for (const COperandEffect &Effect : Effects->Operands)
    if (Effect.Use != EUse::CallObject && Effect.Use != EUse::Callee)
      return dyn_cast_or_null<Expr>(Effect.Operand);
  return nullptr;
}

// What \p Use, of a closure value of the type \p Type in \p Index, is
// (EClosureUse), by what is done with it and, of what it is given to, how
// long that lives (lifetimeOf). Of an alias, \p Value is what its uses are
// of, or \p Local the local whose names they are; of a capture, \p Closure
// the closure and \p Captured the declaration its body names it by.
static EClosureUse closureUseOf(const CEffectIndex &Index,
                                const CEffectIndex::CUse &Use, QualType Type,
                                ASTContext &Context, const Stmt *&Value,
                                const VarDecl *&Local,
                                const LambdaExpr *&Closure,
                                const ValueDecl *&Captured, bool &Replaced) {
  Value = nullptr;
  Local = nullptr;
  Closure = nullptr;
  Captured = nullptr;
  Replaced = false;
  const CEffects *Parent = Use.Parent ? Index.effectsOf(Use.Parent) : nullptr;
  const FunctionDecl *Called = Parent ? Parent->Callee : nullptr;
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Called);
  const CDestination &Into = Use.Effect->Into;
  // Of what it is the object of, itself; else what it is given to is called
  // on.
  const Expr *Object = Use.Effect->Use != EUse::CallObject &&
                               Use.Effect->Use != EUse::Callee &&
                               isa_and_nonnull<Expr>(Use.Parent)
                           ? callObject(cast<Expr>(Use.Parent), Context)
                           : nullptr;
  const EWrapperChange Change = wrapperChangeOf(
      Parent,
      Use.Effect->Use == EUse::CallObject || Use.Effect->Use == EUse::Callee
          ? Type
      : Object ? Object->getType()
               : QualType(),
      Context);
  // A member function called on it, by what that is; given another callable
  // or cleared, it is replaced, not kept.
  auto ByMember = [&] {
    if (Method && Method->getOverloadedOperator() == OO_Call)
      return EClosureUse::Call;
    if (Change != EWrapperChange::None) {
      Replaced = true;
      return EClosureUse::Inspect;
    }
    return inspects(Called) ? EClosureUse::Inspect : EClosureUse::Escape;
  };
  switch (Use.Effect->Use) {
  case EUse::CallObject:
    return ByMember();
  // What is called: it, as a function, or a member function named on it,
  // `fRead.operator()<int>()`.
  case EUse::Callee:
    if (Method && !Method->isStatic())
      return ByMember();
    return EClosureUse::Call;
  case EUse::Condition:
  case EUse::Value:
  case EUse::Discarded:
  case EUse::Statement:
  case EUse::Unevaluated:
    return EClosureUse::Inspect;
  // What is loaded is it.
  case EUse::Access:
    Value = Use.Parent;
    return EClosureUse::Alias;
  case EUse::Copy:
  case EUse::Bind:
    switch (Into.Kind) {
    case EDestination::Parameter: {
      const auto *Call = Parent && Parent->Statement == EStatement::Call
                             ? cast<CallExpr>(Use.Parent)
                             : nullptr;
      // Called by what it is given to, `std::invoke(fRead)` (isStdInvoke),
      // or as the explicit object of its call operator.
      if ((Call && isStdInvoke(Call) && Into.Index == 0) ||
          (Method && Method->isExplicitObjectMemberFunction() &&
           Into.Index == 0 && Method->getOverloadedOperator() == OO_Call))
        return EClosureUse::Call;
      // What only gives it back, `fg_Move(fRead)` (givesItsArgument), and a
      // function object of SynchronousFunctionTypes made of it, by what is
      // done with them.
      if ((Call && givesItsArgument(Call)) ||
          (Parent && Parent->Statement == EStatement::Construction &&
           isSynchronousFunctionType(cast<Expr>(Use.Parent)->getType(),
                                     Context))) {
        Value = Use.Parent;
        return EClosureUse::Alias;
      }
      // Assigned to a local function object of SynchronousFunctionTypes,
      // `fRead = [&] { ... };`, as it is made with it.
      if (const auto *Assignment = dyn_cast_or_null<Expr>(Use.Parent);
          Change == EWrapperChange::Assign && Assignment) {
        const auto *Assigned = dyn_cast_or_null<VarDecl>(
            namedDecl(callObject(Assignment, Context), Context));
        switch (variableLifetime(Assigned).Kind) {
        case ELifetime::Automatic:
        case ELifetime::Frame:
          Local = Assigned;
          return EClosureUse::Alias;
        case ELifetime::FullExpression:
        case ELifetime::Thread:
        case ELifetime::Static:
        case ELifetime::Dynamic:
        case ELifetime::ObjectOf:
        case ELifetime::Closure:
        case ELifetime::Parameter:
        case ELifetime::Return:
        case ELifetime::Unknown:
          return EClosureUse::Escape;
        }
        return EClosureUse::Escape;
      }
      if (inspects(Into.Callee))
        return EClosureUse::Inspect;
      if (Into.Callee && onlyCallsParameter(Into.Callee, Into.Index, Context))
        return EClosureUse::Pass;
      return EClosureUse::Escape;
    }
    // A temporary, or a local, also a reference, until the function or the
    // full expression ends.
    case EDestination::Variable:
    case EDestination::Temporary:
      switch (lifetimeOf(Into).Kind) {
      case ELifetime::FullExpression:
      case ELifetime::Automatic:
      case ELifetime::Frame:
        if (Into.Kind == EDestination::Temporary && Into.Object) {
          Value = Into.Object;
          return EClosureUse::Alias;
        }
        Local = dyn_cast_or_null<VarDecl>(Into.Declaration);
        return Local ? EClosureUse::Alias : EClosureUse::Escape;
      case ELifetime::Thread:
      case ELifetime::Static:
      case ELifetime::Dynamic:
      case ELifetime::ObjectOf:
      case ELifetime::Closure:
      case ELifetime::Parameter:
      case ELifetime::Return:
      case ELifetime::Unknown:
        return EClosureUse::Escape;
      }
      return EClosureUse::Escape;
    case EDestination::Capture:
      Closure = dyn_cast_or_null<LambdaExpr>(Into.Object);
      Captured = dyn_cast_or_null<ValueDecl>(Into.Declaration);
      return Closure && Captured ? EClosureUse::Capture : EClosureUse::Escape;
    case EDestination::None:
    case EDestination::Field:
    case EDestination::Designated:
    case EDestination::Element:
    case EDestination::Base:
    case EDestination::Return:
    case EDestination::Heap:
    case EDestination::Thrown:
      return EClosureUse::Escape;
    }
    return EClosureUse::Escape;
  // Not past what gives it on, which consumersOf follows.
  case EUse::Forward:
  case EUse::Update:
  case EUse::Write:
  case EUse::Dereference:
  case EUse::AddressOf:
  case EUse::Opaque:
    return EClosureUse::Escape;
  }
  return EClosureUse::Escape;
}

llvm::ArrayRef<const DeclRefExpr *>
CFlowCache::namesIn(const Decl *Owner, const Stmt *Body,
                    const ValueDecl *Named, ASTContext &Context) {
  check(Context);
  auto Found = Names.find(Body);
  if (Found == Names.end()) {
    // Made before it is added, its own allocation.
    auto Made = std::make_unique<llvm::DenseMap<
        const ValueDecl *, llvm::SmallVector<const DeclRefExpr *, 2>>>();
    const CEffectIndex &Index = indexOf(Body, Context);
    forEachRunning(Owner, Body, Body, Context, [&](const Stmt *S) {
      if (Index.effectsOf(S)->Statement != EStatement::Name)
        return;
      const auto *Name = cast<DeclRefExpr>(S);
      (*Made)[Name->getDecl()].push_back(Name);
      if (const auto *Binding = dyn_cast<BindingDecl>(Name->getDecl());
          Binding && Binding->getDecomposedDecl())
        (*Made)[Binding->getDecomposedDecl()].push_back(Name);
    });
    Found = Names.try_emplace(Body, std::move(Made)).first;
  }
  if (const auto It = Found->second->find(Named); It != Found->second->end())
    return It->second;
  return {};
}

// Of \p Definitions, each at its Block and Index, Weak when it only adds to
// what reaches, those that can reach the start of each block of \p Graph: a
// may analysis over the blocks of \p Order, into the handlers of each `try`
// what can reach an element in it; with \p bAgain, each also past the entry.
template <typename t_CDefinitions>
static std::vector<llvm::BitVector>
reachingIn(const CFG &Graph, const llvm::BitVector &Reachable,
           const CFlowOrder &Order, const t_CDefinitions &Definitions,
           bool bAgain) {
  const unsigned nBlocks = Graph.getNumBlockIDs();
  const unsigned nDefinitions = Definitions.size();
  std::vector<llvm::BitVector> In(nBlocks, llvm::BitVector(nDefinitions));
  if (nDefinitions == 0)
    return In;
  std::vector<llvm::SmallVector<unsigned, 2>> ByBlock(nBlocks);
  for (unsigned iDefinition = 0; iDefinition < nDefinitions; ++iDefinition)
    ByBlock[Definitions[iDefinition].Block].push_back(iDefinition);
  for (auto &Each : ByBlock)
    llvm::stable_sort(Each, [&](unsigned Left, unsigned Right) {
      return Definitions[Left].Index < Definitions[Right].Index;
    });
  auto Apply = [&](llvm::BitVector &State, unsigned iDefinition) {
    if (!Definitions[iDefinition].Weak)
      State.reset();
    State.set(iDefinition);
  };
  // In forward order, which a loop takes another pass for.
  for (bool Changed = true; Changed;) {
    Changed = false;
    for (const CFGBlock *Block : Order.Order) {
      if (!Block || !Reachable.test(Block->getBlockID()))
        continue;
      llvm::BitVector Out = In[Block->getBlockID()];
      // What each dispatch to handlers is thrown with, of its own `try`.
      const auto &InTryOf = Order.InTry[Block->getBlockID()];
      llvm::SmallVector<llvm::BitVector, 1> Thrown(
          InTryOf.size(), llvm::BitVector(nDefinitions));
      const auto &Here = ByBlock[Block->getBlockID()];
      size_t Next = 0;
      unsigned Position = 0;
      for (unsigned Count = Block->size(); Position < Count;) {
        const unsigned At = Position++;
        for (size_t iTry = 0; iTry < InTryOf.size(); ++iTry)
          if (InTryOf[iTry].second.test(At))
            Thrown[iTry] |= Out;
        for (; Next < Here.size() && Definitions[Here[Next]].Index <= At;
             ++Next)
          Apply(Out, Here[Next]);
        for (size_t iTry = 0; iTry < InTryOf.size(); ++iTry)
          if (InTryOf[iTry].second.test(At))
            Thrown[iTry] |= Out;
      }
      for (; Next < Here.size(); ++Next)
        Apply(Out, Here[Next]);
      if (bAgain && Block == &Graph.getEntry())
        Out.set();
      for (const CFGBlock::AdjacentBlock &Successor : Block->succs())
        if (const CFGBlock *To = Successor.getReachableBlock()) {
          llvm::BitVector &Target = In[To->getBlockID()];
          const llvm::BitVector Before = Target;
          Target |= Out;
          for (size_t iTry = 0; iTry < InTryOf.size(); ++iTry)
            if (InTryOf[iTry].first == To->getBlockID())
              Target |= Thrown[iTry];
          Changed |= Target != Before;
        }
    }
  }
  return In;
}

// Of \p Definitions, those that reach the element \p Index of the block \p
// Block, before it, by what reaches the start of each block, \p In
// (reachingIn).
template <typename t_CDefinitions>
static llvm::BitVector reachingBefore(const std::vector<llvm::BitVector> &In,
                                      const t_CDefinitions &Definitions,
                                      unsigned Block, unsigned Index) {
  if (Block >= In.size())
    return llvm::BitVector(Definitions.size());
  llvm::BitVector Reaching = In[Block];
  llvm::SmallVector<unsigned, 4> Before;
  for (unsigned iDefinition = 0; iDefinition < Definitions.size();
       ++iDefinition)
    if (Definitions[iDefinition].Block == Block &&
        Definitions[iDefinition].Index < Index)
      Before.push_back(iDefinition);
  llvm::stable_sort(Before, [&](unsigned Left, unsigned Right) {
    return Definitions[Left].Index < Definitions[Right].Index;
  });
  for (unsigned iDefinition : Before) {
    if (!Definitions[iDefinition].Weak)
      Reaching.reset();
    Reaching.set(iDefinition);
  }
  return Reaching;
}

std::optional<unsigned>
CWrapperFlow::definitionOf(const Stmt *Statement) const {
  for (unsigned iDefinition = 0; iDefinition < Definitions.size();
       ++iDefinition)
    if (Definitions[iDefinition].Statement == Statement)
      return iDefinition;
  return std::nullopt;
}

CFlowPositions *CFlowCache::positionsOf(const Decl *Owner, const Stmt *Body,
                                        ASTContext &Context) {
  const CFG *Graph = graphOf(Owner, Body, Context);
  if (!Graph)
    return nullptr;
  CGraph &Of = *Graphs.find(Body)->second;
  if (Of.Positions)
    return Of.Positions.get();
  auto Made = std::make_unique<CFlowPositions>();
  for (const CFGBlock *Block : *Graph) {
    if (!Block || !Of.Reachable.test(Block->getBlockID()))
      continue;
    unsigned Here = 0;
    for (const CFGElement &Element : *Block) {
      const unsigned At = Here++;
      if (const CFlowEvent Event = eventOf(Element);
          Event.Kind == EEvent::Statement && Event.Statement)
        Made->Of[Event.Statement].push_back({Block->getBlockID(), At});
    }
  }
  Of.Positions = std::move(Made);
  return Of.Positions.get();
}

const llvm::BitVector &CFlowCache::reachedFrom(const Decl *Owner,
                                               const Stmt *Body, unsigned Block,
                                               ASTContext &Context) {
  CFlowPositions &Positions = *positionsOf(Owner, Body, Context);
  if (const auto It = Positions.Reached.find(Block);
      It != Positions.Reached.end())
    return It->second;
  const CFG &Graph = *graphOf(Owner, Body, Context);
  const llvm::BitVector &Reachable = reachableOf(Owner, Body, Context);
  llvm::SmallVector<const CFGBlock *, 16> Blocks(Graph.getNumBlockIDs());
  for (const CFGBlock *Each : Graph)
    if (Each)
      Blocks[Each->getBlockID()] = Each;
  // What a block that can't be reached holds the flows don't carry on.
  llvm::BitVector Reached(Graph.getNumBlockIDs());
  llvm::SmallVector<unsigned, 16> Work;
  if (Block < Reachable.size() && Reachable.test(Block))
    Work.push_back(Block);
  while (!Work.empty()) {
    const CFGBlock *From = Blocks[Work.pop_back_val()];
    if (!From)
      continue;
    for (const CFGBlock::AdjacentBlock &Successor : From->succs())
      if (const CFGBlock *To = Successor.getReachableBlock();
          To && !Reached.test(To->getBlockID())) {
        Reached.set(To->getBlockID());
        if (To->getBlockID() < Reachable.size() &&
            Reachable.test(To->getBlockID()))
          Work.push_back(To->getBlockID());
      }
  }
  return Positions.Reached.try_emplace(Block, std::move(Reached)).first->second;
}

const CFlowOrder *CFlowCache::orderOf(const Decl *Owner, const Stmt *Body,
                                      ASTContext &Context) {
  const CFG *Graph = graphOf(Owner, Body, Context);
  if (!Graph)
    return nullptr;
  CGraph &Of = *Graphs.find(Body)->second;
  if (Of.Order)
    return Of.Order.get();
  auto Made = std::make_unique<CFlowOrder>();
  // Also what only an edge the order doesn't follow reaches, after it.
  Made->Order = forwardOrder(*Graph);
  llvm::BitVector Ordered(Graph->getNumBlockIDs());
  for (const CFGBlock *Block : Made->Order)
    if (Block)
      Ordered.set(Block->getBlockID());
  for (const CFGBlock *Block : *Graph)
    if (Block && Of.Reachable.test(Block->getBlockID()) &&
        !Ordered.test(Block->getBlockID()))
      Made->Order.push_back(Block);
  Made->InTry.assign(Graph->getNumBlockIDs(), {});
  for (const CFGBlock *Block : *Graph) {
    if (!Block || !Of.Reachable.test(Block->getBlockID()))
      continue;
    for (const CFGBlock::AdjacentBlock &Successor : Block->succs()) {
      const CFGBlock *Dispatch = Successor.getReachableBlock();
      if (!Dispatch || !isHandlerDispatch(Dispatch))
        continue;
      llvm::BitVector InDispatch(Block->size());
      unsigned At = 0;
      for (const CFGElement &Element : *Block) {
        if (inDispatchTry(Dispatch, Element, Context))
          InDispatch.set(At);
        ++At;
      }
      if (InDispatch.any())
        Made->InTry[Block->getBlockID()].push_back(
            {Dispatch->getBlockID(), InDispatch});
    }
  }
  Of.Order = std::move(Made);
  return Of.Order.get();
}

const VarDecl *CFlowCache::aliasedWrapper(const Decl *Owner, const Stmt *Body,
                                          const VarDecl *Variable,
                                          ASTContext &Context) {
  const VarDecl *Referent = Variable;
  for (int Depth = 0; Referent && Referent->hasLocalStorage() &&
                      Referent->getType()->isReferenceType() && Depth < 16;
       ++Depth)
    Referent = dyn_cast_or_null<VarDecl>(namedDecl(Referent->getInit(), Context));
  if (!Referent || Referent == Variable)
    return nullptr;
  const CWrapperFlow &Flow = wrapperFlow(Owner, Body, Referent, Context);
  return Flow.bTold && llvm::is_contained(Flow.Aliases, Variable) ? Referent
                                                                   : nullptr;
}

// Its definitions are the declaration and the assignments and clears
// (wrapperChangeOf) of the names of it in the body, each a strong update, as
// it is named itself; their reach is a flow over the CFG (reachingIn).
// Untold when a closure that refers to it gives it a value or clears it in
// its body, where it runs where the closure is called; a change that can't
// be told otherwise, through a reference to it, only keeps a value longer.
const CWrapperFlow &CFlowCache::wrapperFlow(const Decl *Owner,
                                            const Stmt *Body,
                                            const VarDecl *Variable,
                                            ASTContext &Context) {
  check(Context);
  const std::pair<const VarDecl *, const Stmt *> Key{Variable, Body};
  if (const auto It = WrapperFlows.find(Key); It != WrapperFlows.end())
    return *It->second;
  // Made before it is added, its own allocation.
  auto Made = std::make_unique<CWrapperFlow>();
  const CFG *Graph = graphOf(Owner, Body, Context);
  const CFlowOrder *Order = Graph ? orderOf(Owner, Body, Context) : nullptr;
  if (Order && !Variable->getType()->isReferenceType() &&
      isSynchronousFunctionType(Variable->getType(), Context)) {
    const CEffectIndex &Index = indexOf(Body, Context);
    // Its names and those of the local references bound to it, `auto
    // &fAlias = fRead;`, also to one of those, second names of the slot.
    llvm::SmallVector<const DeclRefExpr *, 8> &Names = Made->Names;
    llvm::append_range(Names, namesIn(Owner, Body, Variable, Context));
    for (size_t iName = 0; iName < Names.size() && Made->Aliases.size() < 16;
         ++iName)
      for (const CEffectIndex::CUse &Use : Index.consumersOf(Names[iName]))
        if (const auto *Alias =
                Use.Effect->Use == EUse::Bind &&
                        Use.Effect->Into.Kind == EDestination::Variable
                    ? dyn_cast_or_null<VarDecl>(Use.Effect->Into.Declaration)
                    : nullptr;
            Alias && Alias->hasLocalStorage() &&
            Alias->getType()->isReferenceType() &&
            namedDecl(Alias->getInit(), Context) == Names[iName]->getDecl() &&
            !llvm::is_contained(Made->Aliases, Alias)) {
          Made->Aliases.push_back(Alias);
          llvm::append_range(Names, namesIn(Owner, Body, Alias, Context));
        }
    bool bTold = true;
    // Each change and what it gives, null of a clear (wrapperGiven), in the
    // order of the names, which is the order of Given.
    llvm::MapVector<const Stmt *, const Expr *> Changes;
    // What the changes in the bodies of closures give.
    llvm::SmallVector<const Expr *, 2> ClosureGiven;
    for (const DeclRefExpr *Name : Names)
      for (const CEffectIndex::CUse &Use : Index.consumersOf(Name)) {
        if (Use.Effect->Use == EUse::CallObject)
          if (const CEffects *Effects = Index.effectsOf(Use.Parent);
              wrapperChangeOf(Effects, Variable->getType(), Context) !=
              EWrapperChange::None)
            Changes.try_emplace(Use.Parent,
                                wrapperGiven(Effects, Variable->getType(),
                                             Context));
        // A capture by reference, by the closure classifier.
        const Stmt *Aliased = nullptr;
        const VarDecl *Local = nullptr;
        const LambdaExpr *Closure = nullptr;
        const ValueDecl *Captured = nullptr;
        bool Replaced = false;
        if (Use.Effect->Use != EUse::Bind ||
            Use.Effect->Into.Kind != EDestination::Capture ||
            closureUseOf(Index, Use, Variable->getType(), Context, Aliased,
                         Local, Closure, Captured,
                         Replaced) != EClosureUse::Capture)
          continue;
        for (const FunctionDecl *Operator : callOperatorsOf(Closure)) {
          const CEffectIndex &Inner = indexOf(Operator->getBody(), Context);
          for (const DeclRefExpr *Named :
               namesIn(Operator, Operator->getBody(), Captured, Context))
            for (const CEffectIndex::CUse &InnerUse : Inner.consumersOf(Named)) {
              const CEffects *Effects = Inner.effectsOf(InnerUse.Parent);
              if (InnerUse.Effect->Use != EUse::CallObject ||
                  wrapperChangeOf(Effects, Variable->getType(), Context) ==
                      EWrapperChange::None)
                continue;
              bTold = false;
              if (const Expr *Value =
                      wrapperGiven(Effects, Variable->getType(), Context))
                ClosureGiven.push_back(Value);
            }
        }
      }
    llvm::DenseMap<const Stmt *, std::pair<unsigned, unsigned>> Positions;
    llvm::SmallPtrSet<const Stmt *, 16> Placed(Names.begin(), Names.end());
    bool bDeclared = false;
    const llvm::BitVector &Reachable = reachableOf(Owner, Body, Context);
    for (const CFGBlock *Block : *Graph) {
      if (!Block || !Reachable.test(Block->getBlockID()))
        continue;
      for (unsigned iElement = 0; iElement < Block->size(); ++iElement) {
        const CFlowEvent Event = eventOf((*Block)[iElement]);
        if (Event.Kind != EEvent::Statement)
          continue;
        const Stmt *S = Event.Statement;
        if (Placed.contains(S))
          Positions.try_emplace(S, Block->getBlockID(), iElement);
        if (const auto Change = Changes.find(S); Change != Changes.end())
          Made->Definitions.push_back(
              {S, Block->getBlockID(), iElement, Change->second});
        else if (llvm::is_contained(effectsOf(S, Context).Declares,
                                    Variable)) {
          const Stmt *Original = originalOf(Owner, Body, S, Context);
          Made->Definitions.push_back({Original ? Original : S,
                                       Block->getBlockID(), iElement,
                                       Variable->getInit()});
          bDeclared = true;
        }
      }
    }
    Made->bTold = bTold && bDeclared;
    if (Made->bTold) {
      // Each strong, as reachingIn takes them.
      struct CStrong {
        unsigned Block;
        unsigned Index;
        bool Weak;
      };
      llvm::SmallVector<CStrong, 4> Strong;
      for (const CWrapperFlow::CDefinition &Each : Made->Definitions)
        Strong.push_back({Each.Block, Each.Index, false});
      const std::vector<llvm::BitVector> In = reachingIn(
          *Graph, Reachable, *Order, Strong, /*bAgain=*/false);
      for (const DeclRefExpr *Name : Names) {
        llvm::BitVector Reaching(Strong.size(), true);
        if (const auto It = Positions.find(Name); It != Positions.end())
          Reaching = reachingBefore(In, Strong, It->second.first,
                                    It->second.second);
        Made->Reaching[Name] = std::move(Reaching);
      }
    } else {
      // Each value it is given, also where the CFG doesn't have it.
      if (const Expr *Init = Variable->getInit())
        Made->Given.push_back(Init);
      for (const auto &[Change, Value] : Changes)
        if (Value && !llvm::is_contained(Made->Given, Value))
          Made->Given.push_back(Value);
      for (const Expr *Value : ClosureGiven)
        if (!llvm::is_contained(Made->GivenInClosures, Value))
          Made->GivenInClosures.push_back(Value);
      Made->Definitions.clear();
      Made->Names.clear();
      Made->Aliases.clear();
    }
  }
  std::unique_ptr<CWrapperFlow> &Slot = WrapperFlows[Key];
  if (!Slot)
    Slot = std::move(Made);
  return *Slot;
}

// What each use of a closure value is (closureUseOf), \p Root of \p Body of
// \p Owner, a statement, or a declaration whose names there it is, \p
// bNamed: each use of an alias of it, by what is done with the alias, and in
// the body of a closure that captures it, of the names of the capture, of
// one that stays local itself, whose calls run it. Of a function object of
// SynchronousFunctionTypes whose values are told (wrapperFlow), only the
// names the definition that gives it the value reaches. The closure itself
// captured through what it is in, `fVisit = [&] { fVisit(); };`, is a
// recursion: the uses in its body are its own. Asked again while it is told,
// it stays local, as what is told around it confirms or not
// (CFlowSummaries::ClosureUses).
static CClosureUses closureUsesUncached(const Decl *Owner, const Stmt *Body,
                                        const void *Root, bool bNamed,
                                        ASTContext &Context) {
  CFlowCache &Cache = sharedFlowCache();
  CClosureUses Found;
  CClosureUses *Uses = &Found;
  struct CIn {
    const Decl *Owner;
    const Stmt *Body;
    const CEffectIndex *Index;
  };
  llvm::DenseSet<std::pair<const void *, const Stmt *>> Followed;
  // The definitions of the wrappers followed from, by the body.
  llvm::DenseSet<std::pair<const Stmt *, const Stmt *>> FollowedGiven;
  TCRecursive<void(const CIn &, const ValueDecl *, int)> FollowNames;
  TCRecursive<void(const CIn &, const VarDecl *, const Stmt *, int)> FollowLocal;
  // Of a name of a wrapper whose values are told, \p Held, its own changes
  // end the value, which the flow tells, not replace it.
  TCRecursive<void(const CIn &, const Stmt *, int, const CWrapperFlow *)> FollowValue = [&](const CIn &In, const Stmt *Value, int Depth,
                        const CWrapperFlow *Held) {
        if (!Followed.insert({Value, In.Body}).second)
          return;
        // The root, an initializer outside a function, `auto g_fRead = [&]
        // { ... };`, is given to what the body doesn't tell.
        if (In.Index->usesOf(Value).empty()) {
          Uses->bLocal = false;
          Uses->Escapes.push_back({EClosureUse::Escape, In.Owner, In.Body, {}});
          return;
        }
        for (const CEffectIndex::CUse &Use : In.Index->consumersOf(Value)) {
          const Stmt *Aliased = nullptr;
          const VarDecl *Local = nullptr;
          const LambdaExpr *Closure = nullptr;
          const ValueDecl *Captured = nullptr;
          bool Replaced = false;
          EClosureUse Kind = closureUseOf(
              *In.Index, Use,
              isa<Expr>(Value) ? cast<Expr>(Value)->getType() : QualType(),
              Context, Aliased, Local, Closure, Captured, Replaced);
          Uses->bReplaced |=
              Replaced && !(Held && Held->definitionOf(Use.Parent));
          // Not past a depth of aliases, where it can't be followed.
          if (Depth >= 16 &&
              (Kind == EClosureUse::Alias || Kind == EClosureUse::Capture))
            Kind = EClosureUse::Escape;
          const CClosureSite Site{Kind, In.Owner, In.Body, Use};
          switch (Kind) {
          case EClosureUse::Call:
          case EClosureUse::Pass:
            Uses->Calls.push_back(Site);
            break;
          case EClosureUse::Inspect:
            break;
          case EClosureUse::Alias:
            // A second name of the slot it is followed in, whose names are.
            if (Local && Held && llvm::is_contained(Held->Aliases, Local))
              break;
            if (Local)
              FollowLocal(In, Local, Use.Parent, Depth + 1);
            else
              FollowValue(In, Aliased, Depth + 1, nullptr);
            break;
          case EClosureUse::Capture: {
            // Of a closure that stays local itself, whose calls run it, in
            // its body, each operator it has (callOperatorsOf); of itself,
            // the recursion, its body only.
            if (static_cast<const void *>(Closure) != Root) {
              const CClosureUses &Outer =
                  Cache.closureUses(In.Owner, In.Body, Closure, Context);
              if (!Outer.bLocal) {
                Uses->bLocal = false;
                Uses->Escapes.push_back(Site);
                break;
              }
              Uses->Closures.push_back(Closure);
              Uses->bReplaced |= Outer.bReplaced;
              for (CClosureSite Called : Outer.Calls) {
                if (!Called.Through)
                  Called.Through = Closure;
                Uses->Calls.push_back(Called);
              }
            }
            for (const FunctionDecl *Operator : callOperatorsOf(Closure))
              FollowNames({Operator, Operator->getBody(),
                           &Cache.indexOf(Operator->getBody(), Context)},
                          Captured, Depth + 1);
            break;
          }
          case EClosureUse::Escape:
            Uses->bLocal = false;
            Uses->Escapes.push_back(Site);
            break;
          }
        }
      };
  FollowNames = [&](const CIn &In, const ValueDecl *Named, int Depth) {
    if (!Followed.insert({Named, In.Body}).second)
      return;
    for (const DeclRefExpr *Name :
         Cache.namesIn(In.Owner, In.Body, Named, Context))
      FollowValue(In, Name, Depth, nullptr);
  };
  // In the local \p Local from what gives it the value, \p Given: of a
  // wrapper whose values are told, the names that definition reaches, and
  // where another can reach as well, one that a call may run instead; else
  // each name, as long as it lives.
  FollowLocal = [&](const CIn &In, const VarDecl *Local, const Stmt *Given,
                    int Depth) {
    // A second name of a wrapper whose values are told is of its slot.
    if (const VarDecl *Wrapper =
            Cache.aliasedWrapper(In.Owner, In.Body, Local, Context))
      Local = Wrapper;
    const CWrapperFlow &Flow =
        Cache.wrapperFlow(In.Owner, In.Body, Local, Context);
    const std::optional<unsigned> Definition =
        Flow.bTold ? Flow.definitionOf(Given) : std::nullopt;
    if (!Definition) {
      Uses->Locals.push_back({In.Owner, Local, nullptr});
      FollowNames(In, Local, Depth);
      return;
    }
    if (!FollowedGiven.insert({Given, In.Body}).second)
      return;
    Uses->Locals.push_back({In.Owner, Local, Given});
    for (const DeclRefExpr *Name : Flow.Names) {
      const llvm::BitVector &Reaching = Flow.Reaching.find(Name)->second;
      if (!Reaching.test(*Definition))
        continue;
      Uses->bReplaced |= Reaching.count() > 1;
      FollowValue(In, Name, Depth, &Flow);
    }
  };
  const CIn In{Owner, Body, &Cache.indexOf(Body, Context)};
  if (bNamed)
    FollowNames(In, static_cast<const ValueDecl *>(Root), 0);
  else
    FollowValue(In, static_cast<const Stmt *>(Root), 0, nullptr);
  return Found;
}

const CClosureUses &closureUses(const Decl *Owner, const Stmt *Body,
                                const Stmt *Value, ASTContext &Context) {
  return sharedFlowCache().closureUses(Owner, Body, Value, Context);
}

// What \p Made copies or moves of a record, by a copy or move constructor or
// assignment operator the compiler defines, `View` of `auto Copy = View;` or
// `Copy = View;`; null of another, whose body can do anything else.
static const Expr *copiedFrom(const Stmt *Made, ASTContext &Context) {
  const Expr *Value = dyn_cast_or_null<Expr>(Made);
  // The construction or the call itself, also past what only gives it on
  // (sameValue); a construction by the constructor its arguments are given
  // to.
  auto CalledBy = [&](const Expr *E) -> const FunctionDecl * {
    const CEffects &Of = effectsOf(E, Context);
    if (Of.Statement == EStatement::Construction)
      return Of.Operands.empty() ? nullptr : Of.Operands.front().Into.Callee;
    return Of.Callee;
  };
  if (Value && !CalledBy(Value))
    Value = sameValue(Value, Context);
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Value ? CalledBy(Value) : nullptr);
  if (!Method || Method->isStatic())
    return nullptr;
  const auto *Constructor = dyn_cast<CXXConstructorDecl>(Method);
  const bool Moves = Constructor ? Constructor->isMoveConstructor()
                                 : Method->isMoveAssignmentOperator();
  const bool Copies = Constructor ? Constructor->isCopyOrMoveConstructor()
                                  : Method->isCopyAssignmentOperator() ||
                                        Method->isMoveAssignmentOperator();
  // One the user provides, of an owner (OwnerTypes), by its kind: a copy or
  // a move of a shared owner gives what the source owns, a move of a
  // unique, a buffer or a sharedbuffer one too; a copy of those owns
  // another, of a sharedbuffer one a buffer that lives as long as it does,
  // which is its own as a value.
  if (!Copies)
    return nullptr;
  if (Method->isUserProvided()) {
    const EOwnerKind Kind =
        ownerKindOf(Context.getCanonicalTagType(Method->getParent()), Context);
    if (!(Kind == EOwnerKind::Shared ||
          (Kind != EOwnerKind::None && Kind != EOwnerKind::Weak && Moves)))
      return nullptr;
  }
  // The source, past an explicit object parameter.
  return argumentFor(Value, Method->isExplicitObjectMemberFunction() ? 1 : 0,
                     Context);
}

const Expr *copiedByDefinedAssignment(const Expr *Call, ASTContext &Context) {
  const CEffects &Of = effectsOf(Call, Context);
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Of.Callee);
  if (Of.Statement != EStatement::Call || !Method || Method->isUserProvided() ||
      !(Method->isCopyAssignmentOperator() ||
        Method->isMoveAssignmentOperator()))
    return nullptr;
  return copiedFrom(Call, Context);
}

namespace {
// What a write of a local flow designates (CLocalFlow::makeTargets), and
// whether each is told.
struct CTargets {
  llvm::SmallVector<CSlot, 2> Slots;
  bool Told = true;
  bool operator==(const CTargets &Other) const {
    return Slots == Other.Slots && Told == Other.Told;
  }
};

// What a write of a local flow designates of one variable (targetsOn): the
// slots of it, and of any, whether they are all it designates and told.
struct CTargetsOn {
  llvm::SmallVector<CSlot, 1> Slots;
  bool Only = false;
  bool Told = true;
  bool operator==(const CTargetsOn &Other) const {
    return Slots == Other.Slots && Only == Other.Only && Told == Other.Told;
  }
};

// A definition of a local slot (CFacts).
struct CDefinition {
  unsigned Block;
  unsigned Index;
  CSlotValue Value;
  bool Known;
  bool Weak;
  // What a write designates, where it is in its expression (unordered).
  const Expr *Target = nullptr;
  // Of a part, whether it is what the whole of what it is in is given,
  // not the part on its own (wholeReaches).
  bool Whole = false;
  // Whether it is a change the value of can't be told, by what can change
  // it (CWrite::EKind::Opaque, Through), not a value that can't be told.
  bool Changed = false;
  // Of the object of the function, whether it ends a way the function
  // doesn't take (CWrite::Infeasible), which gives it no value.
  bool Infeasible = false;
  // Of a slot that isn't an element, whether it writes only an element of
  // it, what holds it unchanged (placeUnchangedBetween).
  bool Element = false;
  // Whether it is a write whose place can't be told, of storage only code
  // that can't be seen gives (unseenStorage), that can be the slot: of the
  // object of the function or what a parameter refers to, which a caller
  // can give where such code reaches it or not (CFlowFact::Reached).
  bool Unseen = false;
  bool operator==(const CDefinition &Other) const {
    return Block == Other.Block && Index == Other.Index &&
           Value == Other.Value && Known == Other.Known && Weak == Other.Weak &&
           Target == Other.Target && Whole == Other.Whole &&
           Changed == Other.Changed && Infeasible == Other.Infeasible &&
           Element == Other.Element && Unseen == Other.Unseen;
  }
};

// The definitions of a local slot of a function (CLocalFlow::observeFacts).
struct CFacts {
  std::vector<CDefinition> Definitions;
  bool Told = true;
  // What reaches the start of each block, once asked (CLocalFlow::flow),
  // which the definitions tell.
  mutable std::vector<llvm::BitVector> In;
  mutable bool Flowed = false;
  // The definitions by where they are, once asked (CLocalFlow::indexFacts):
  // by block, in the order of their index in it, and by the expression their
  // target is in (unordered), in order.
  mutable std::vector<unsigned> InBlock;
  mutable std::vector<std::pair<const Stmt *, unsigned>> InExpression;
  mutable bool Indexed = false;
  bool operator==(const CFacts &Other) const {
    return Definitions == Other.Definitions && Told == Other.Told;
  }
};

// A local slot of a function, whose facts are a summary (CFlowSummaries).
struct CSlotKey {
  const FunctionDecl *Function = nullptr;
  const VarDecl *Variable = nullptr;
  llvm::SmallVector<const FieldDecl *, 2> Path;
  EPlace Place = EPlace::Variable;
  // Of an element of a constant index, the index: its facts are those of
  // what it is in but for the writes of another constant index.
  std::optional<int64_t> Index = std::nullopt;
  bool operator==(const CSlotKey &Other) const {
    return Function == Other.Function && Variable == Other.Variable &&
           Path == Other.Path && Place == Other.Place && Index == Other.Index;
  }
};
} // namespace
} // namespace clang::tidy::malterlib

namespace llvm {
template <> struct DenseMapInfo<clang::tidy::malterlib::CSlotKey> {
  using CKey = clang::tidy::malterlib::CSlotKey;
  using CFunctionInfo = PointerLikeTypeTraits<const clang::FunctionDecl *>;
  static CKey getEmptyKey() {
    CKey Key;
    Key.Function = static_cast<const clang::FunctionDecl *>(
        CFunctionInfo::getFromVoidPointer(reinterpret_cast<void *>(
            static_cast<uintptr_t>(-1) << CFunctionInfo::NumLowBitsAvailable)));
    return Key;
  }
  static CKey getTombstoneKey() {
    CKey Key;
    Key.Function = static_cast<const clang::FunctionDecl *>(
        CFunctionInfo::getFromVoidPointer(reinterpret_cast<void *>(
            static_cast<uintptr_t>(-2) << CFunctionInfo::NumLowBitsAvailable)));
    return Key;
  }
  static unsigned getHashValue(const CKey &Key) {
    return static_cast<unsigned>(llvm::hash_combine(
        Key.Function, Key.Variable, static_cast<unsigned>(Key.Place),
        Key.Index ? *Key.Index : int64_t(-1),
        llvm::hash_combine_range(Key.Path.begin(), Key.Path.end())));
  }
  static bool isEqual(const CKey &Left, const CKey &Right) {
    return Left == Right;
  }
};
} // namespace llvm

namespace clang::tidy::malterlib {

namespace {
/// Of each parameter of a function, whether its body can change what it
/// refers to through a reference to const (constParameterWritten), once told.
struct CConstParameterWrites {
  llvm::DenseMap<std::pair<const FunctionDecl *, unsigned>, bool> Of;
};
} // namespace

/// Whether \p Target, an owner (OwnerTypes), past members, subscripts and
/// implicit steps, names a variable of a function around the closure whose
/// body it is in, which a closure can change for that function: the changes
/// of the keepers of that function's flow (Rule K).
static bool namesEnclosingOwner(const Expr *Target, ASTContext &Context) {
  if (ownerKindOf(Target->getType().getNonReferenceType().getUnqualifiedType(),
                  Context) == EOwnerKind::None)
    return false;
  for (int Step = 0; Target && Step < 64; ++Step) {
    Target = Target->IgnoreParenImpCasts();
    if (const auto *Named = dyn_cast<DeclRefExpr>(Target))
      return Named->refersToEnclosingVariableOrCapture();
    if (const auto *Member = dyn_cast<MemberExpr>(Target)) {
      if (Member->isArrow())
        return false;
      Target = Member->getBase();
      continue;
    }
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Target)) {
      Target = Subscript->getBase();
      continue;
    }
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Target);
        Operator && Operator->getNumArgs() >= 1 &&
        (Operator->getOperator() == OO_Subscript ||
         Operator->getOperator() == OO_Star)) {
      Target = Operator->getArg(0);
      continue;
    }
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Target)) {
      Target = Cast->getSubExpr();
      continue;
    }
    return false;
  }
  return false;
}

/// The parameter of \p Function that \p E designates or points to, through
/// local references bound to it and local pointers made of its address,
/// past casts; null of what else.
static const ParmVarDecl *referredParameter(const Expr *E,
                                            const FunctionDecl *Function,
                                            ASTContext &Context) {
  for (int Step = 0; E && Step < 64; ++Step) {
    E = E->IgnoreParenCasts();
    if (const auto *Address = dyn_cast<UnaryOperator>(E);
        Address && (Address->getOpcode() == UO_AddrOf ||
                    Address->getOpcode() == UO_Deref)) {
      E = Address->getSubExpr();
      continue;
    }
    const auto *Named = dyn_cast_or_null<VarDecl>(namedDecl(E, Context));
    if (const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Named))
      return Parameter->getDeclContext() == Function ? Parameter : nullptr;
    if (!Named || !Named->hasLocalStorage() || !Named->getInit() ||
        !(Named->getType()->isReferenceType() ||
          Named->getType()->isPointerType()))
      return nullptr;
    E = Named->getInit();
  }
  return nullptr;
}

/// The functions \p Call can run as \p Declared: the overrides of a virtual
/// member function it can dispatch to (dispatchedMethods), else \p Declared.
static llvm::SmallVector<const FunctionDecl *, 4>
runFunctions(const Stmt *Call, const FunctionDecl *Declared) {
  llvm::SmallVector<const FunctionDecl *, 4> Runs;
  if (const CMemberCall Member = memberCallOf(Call);
      Member && Member->getMethodDecl() &&
      Member->getMethodDecl()->getCanonicalDecl() ==
          Declared->getCanonicalDecl())
    llvm::append_range(Runs, dispatchedMethods(Member));
  if (Runs.empty())
    Runs.push_back(Declared);
  return Runs;
}

/// Whether \p E is a part of what \p Parameter, a pointer or a shared or
/// unique owner (OwnerTypes), points to or owns, which a reference to const
/// of it can change: past members, subscripts and the dereferences, built in
/// or the owner's access, `_pContext->m_pJob`, `*_ppValue`, also through
/// owners and pointers in it.
static bool pointedThroughParameter(const Expr *E, const ParmVarDecl *Parameter,
                                    const FunctionDecl *Function,
                                    ASTContext &Context) {
  bool Through = false;
  for (int Step = 0; E && Step < 64; ++Step) {
    E = E->IgnoreParenImpCasts();
    if (Through && referredParameter(E, Function, Context) == Parameter)
      return true;
    if (const auto *Member = dyn_cast<MemberExpr>(E)) {
      Through |= Member->isArrow();
      E = Member->getBase();
      continue;
    }
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E)) {
      Through = true;
      E = Subscript->getBase();
      continue;
    }
    if (const auto *Dereference = dyn_cast<UnaryOperator>(E);
        Dereference && Dereference->getOpcode() == UO_Deref) {
      Through = true;
      E = Dereference->getSubExpr();
      continue;
    }
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
        Operator && Operator->getNumArgs() >= 1 &&
        (Operator->getOperator() == OO_Arrow ||
         Operator->getOperator() == OO_Star)) {
      const EOwnerKind Kind = ownerKindOf(
          Operator->getArg(0)->getType().getNonReferenceType(), Context);
      if (Kind != EOwnerKind::Shared && Kind != EOwnerKind::Unique)
        return false;
      Through = true;
      E = Operator->getArg(0);
      continue;
    }
    return false;
  }
  return false;
}

/// Whether a body that can be seen of \p Function can change what its
/// parameter \p iParameter, a reference to const, refers to: it casts the
/// const away from it, or gives it as a reference that isn't to const, also
/// on to a function that does, through local references and pointers to it
/// (referredParameter), or, of a pointer or a shared or unique owner, writes
/// what it points to or owns, which its constness doesn't cover
/// (pointedThroughParameter), and of a virtual member function each override
/// the call can run (runFunctions). Not where no body is seen. A work stack
/// over the functions, each once.
static bool constParameterWritten(const FunctionDecl *Function,
                                  unsigned iParameter, ASTContext &Context) {
  auto &Cache = astCache<CConstParameterWrites>(Context).Of;
  const std::pair<const FunctionDecl *, unsigned> Key{Function, iParameter};
  if (const auto It = Cache.find(Key); It != Cache.end())
    return It->second;
  bool Written = false;
  llvm::SmallVector<std::pair<const FunctionDecl *, unsigned>, 4> Work{Key};
  llvm::DenseSet<std::pair<const FunctionDecl *, unsigned>> Seen;
  while (!Work.empty() && !Written) {
    const auto [Each, iEach] = Work.pop_back_val();
    if (!Seen.insert({Each, iEach}).second)
      continue;
    if (const auto It = Cache.find({Each, iEach}); It != Cache.end()) {
      Written = It->second;
      continue;
    }
    const FunctionDecl *Definition = Each->getDefinition();
    if (!Definition || !Definition->getBody() ||
        iEach >= Definition->getNumParams())
      continue;
    const ParmVarDecl *Parameter = Definition->getParamDecl(iEach);
    forEachRunning(
        Definition, Definition->getBody(), Definition->getBody(), Context,
        [&](const Stmt *S) {
          if (Written)
            return;
          // A cast that takes the const away from it.
          if (const auto *Cast = dyn_cast<ExplicitCastExpr>(S);
              Cast &&
              (isa<CXXConstCastExpr>(Cast) || isa<CStyleCastExpr>(Cast))) {
            const QualType To = Cast->getType();
            const bool Writable =
                (Cast->isGLValue() && !To.isConstQualified()) ||
                (To->isPointerType() &&
                 !To->getPointeeType().isConstQualified());
            if (Writable && referredParameter(Cast->getSubExpr(), Definition,
                                              Context) == Parameter)
              Written = true;
            return;
          }
          const CEffects &Effects = effectsOf(S, Context);
          for (const COperandEffect &Effect : Effects.Operands) {
            const auto *Given =
                dyn_cast_or_null<ParmVarDecl>(Effect.Into.Declaration);
            const auto *Operand = dyn_cast_or_null<Expr>(Effect.Operand);
            // A write of what it points to or owns, an assignment, an
            // update, a member function that isn't const called on it, or a
            // reference that isn't to const bound to it.
            const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Effects.Callee);
            if (Operand &&
                (Effect.Use == EUse::Write || Effect.Use == EUse::Update ||
                 (Effect.Use == EUse::CallObject && Method &&
                  !Method->isConst() && !Method->isStatic()) ||
                 (Effect.Use == EUse::Bind && Given &&
                  changesThrough(Given->getType()))) &&
                pointedThroughParameter(Operand, Parameter, Definition,
                                        Context)) {
              Written = true;
              return;
            }
            if (Effect.Use != EUse::Bind ||
                Effect.Into.Kind != EDestination::Parameter || !Given ||
                !Operand ||
                referredParameter(Operand, Definition, Context) != Parameter)
              continue;
            if (changesThrough(Given->getType())) {
              Written = true;
              return;
            }
            if (const auto *Declared =
                    dyn_cast<FunctionDecl>(Given->getDeclContext()))
              for (const FunctionDecl *Runs : runFunctions(S, Declared))
                Work.push_back({Runs, Given->getFunctionScopeIndex()});
          }
        });
  }
  Cache[Key] = Written;
  return Written;
}

/// Whether \p Call, given what is bound to \p Parameter, a reference to
/// const, can change it there (constParameterWritten): of an owner or a
/// pointer it refers to, whose changes the slots of a flow tell.
static bool changedThroughConst(const Stmt *Call, const ParmVarDecl *Parameter,
                                ASTContext &Context) {
  if (!Parameter || !Parameter->getType()->isReferenceType())
    return false;
  const QualType Referent = Parameter->getType().getNonReferenceType();
  if (!Referent->isPointerType() &&
      ownerKindOf(Referent.getUnqualifiedType(), Context) == EOwnerKind::None)
    return false;
  const auto *Declared = dyn_cast<FunctionDecl>(Parameter->getDeclContext());
  if (!Declared)
    return false;
  return llvm::any_of(runFunctions(Call, Declared),
                      [&](const FunctionDecl *Runs) {
                        return constParameterWritten(
                            Runs, Parameter->getFunctionScopeIndex(), Context);
                      });
}

namespace {
/// Of each parameter of a function, whether its body can change what it
/// points to or refers to (pointedParameterWritten), once told.
struct CPointedParameterWrites {
  llvm::DenseMap<std::pair<const FunctionDecl *, unsigned>, bool> Of;
};
} // namespace

/// Whether \p E is \p Parameter or a part of what it points to or refers to:
/// past members, subscripts, dereferences and casts.
static bool partOfParameter(const Expr *E, const ParmVarDecl *Parameter) {
  for (int Step = 0; E && Step < 64; ++Step) {
    E = E->IgnoreParenCasts();
    if (const auto *Named = dyn_cast<DeclRefExpr>(E))
      return Named->getDecl() == Parameter;
    if (const auto *Member = dyn_cast<MemberExpr>(E))
      E = Member->getBase();
    else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E))
      E = Subscript->getBase();
    else if (const auto *Unary = dyn_cast<UnaryOperator>(E);
             Unary && Unary->getOpcode() == UO_Deref)
      E = Unary->getSubExpr();
    else
      return false;
  }
  return false;
}

/// Whether a body that can be seen of \p Function can change what its
/// parameter \p iParameter, a pointer or a reference, points to or refers to:
/// what is named through it is assigned, written, stepped, given its address, bound to
/// a reference that isn't to const, copied as a pointer that isn't to const,
/// other than given back, which is where the caller uses it, or is the object
/// of an assignment or a member function that can change it; also where no
/// body is seen.
static bool pointedParameterWritten(const FunctionDecl *Function,
                                    unsigned iParameter, ASTContext &Context) {
  auto &Cache = astCache<CPointedParameterWrites>(Context).Of;
  const std::pair<const FunctionDecl *, unsigned> Key{Function, iParameter};
  if (const auto It = Cache.find(Key); It != Cache.end())
    return It->second;
  const FunctionDecl *Definition = Function->getDefinition();
  bool Written = !Definition || !Definition->getBody() ||
                 iParameter >= Definition->getNumParams() ||
                 isa<CoroutineBodyStmt>(Definition->getBody());
  if (!Written) {
    const ParmVarDecl *Parameter = Definition->getParamDecl(iParameter);
    forEachRunning(
        Definition, Definition->getBody(), Definition->getBody(), Context,
        [&](const Stmt *S) {
          if (Written)
            return;
          const CEffects &Effects = effectsOf(S, Context);
          for (const COperandEffect &Effect : Effects.Operands) {
            // What is assigned to it.
            if (const auto *Assigned =
                    Effect.Into.Kind == EDestination::Designated
                        ? dyn_cast_or_null<Expr>(Effect.Into.Object)
                        : nullptr;
                Assigned && partOfParameter(Assigned, Parameter)) {
              Written = true;
              return;
            }
            const auto *Operand = dyn_cast_or_null<Expr>(Effect.Operand);
            if (!Operand || !partOfParameter(Operand, Parameter))
              continue;
            switch (Effect.Use) {
            case EUse::Write:
            case EUse::Update:
            case EUse::AddressOf:
            case EUse::Opaque:
              Written = true;
              break;
            case EUse::CallObject: {
              const auto *Method =
                  dyn_cast_or_null<CXXMethodDecl>(Effects.Callee);
              Written = !Method ||
                        Method->getOverloadedOperator() == OO_Equal ||
                        (!hasConstObject(Method) && !Method->isStatic() &&
                         !onlyGivesAccess(Method));
              break;
            }
            case EUse::Bind: {
              // What it gives back is where its caller uses it.
              if (Effect.Into.Kind == EDestination::Return)
                break;
              const auto *Into =
                  dyn_cast_or_null<ValueDecl>(Effect.Into.Declaration);
              const QualType Type = Into ? Into->getType() : QualType();
              Written = Type.isNull() || !Type->isReferenceType() ||
                        !Type.getNonReferenceType().isConstQualified();
              break;
            }
            case EUse::Copy:
              Written = Effect.Into.Kind != EDestination::Return &&
                        Operand->getType()->isPointerType() &&
                        !pointsToConst(Operand->getType());
              break;
            case EUse::Forward:
            case EUse::Access:
            case EUse::Dereference:
            case EUse::Callee:
            case EUse::Condition:
            case EUse::Value:
            case EUse::Discarded:
            case EUse::Statement:
            case EUse::Unevaluated:
              break;
            }
            if (Written)
              return;
          }
        });
  }
  Cache[Key] = Written;
  return Written;
}

/// Whether \p Call, given what \p Parameter, a pointer or a reference that
/// isn't to const, points to or refers to, can change it there: each body it
/// can run (runFunctions), unless each can be seen and doesn't
/// (pointedParameterWritten).
static bool changedThroughParameter(const Stmt *Call,
                                    const ParmVarDecl *Parameter,
                                    ASTContext &Context) {
  const auto *Declared =
      Parameter ? dyn_cast<FunctionDecl>(Parameter->getDeclContext()) : nullptr;
  if (!Declared || !isa_and_nonnull<CallExpr>(Call))
    return true;
  return llvm::any_of(runFunctions(Call, Declared),
                      [&](const FunctionDecl *Runs) {
                        return pointedParameterWritten(
                            Runs, Parameter->getFunctionScopeIndex(), Context);
                      });
}

bool parameterOnlyRead(const CallExpr *Call, const FunctionDecl *Callee,
                       unsigned iParameter, ASTContext &Context) {
  return Callee && iParameter < Callee->getNumParams() &&
         !changedThroughParameter(Call, Callee->getParamDecl(iParameter),
                                  Context);
}

namespace {
/// Of each member function, the members of its object it can change
/// (changedMembers), once told.
struct CChangedMembers {
  /// By the member function and whether its own body runs where an
  /// override could (changedMembers' \p Static).
  llvm::DenseMap<llvm::PointerIntPair<const CXXMethodDecl *, 1, bool>,
                 std::optional<llvm::SmallVector<const FieldDecl *, 4>>>
      Of;
};
} // namespace

/// Whether \p E names the object of the member function whose body it is in:
/// `this`, or an explicit object parameter that refers to it, `_Self` of
/// `f_Set(this CKeep &_Self)`.
static bool namesObject(const Expr *E) {
  if (isa<CXXThisExpr>(E))
    return true;
  const auto *Named = dyn_cast<DeclRefExpr>(E);
  const auto *Parameter =
      Named ? dyn_cast<ParmVarDecl>(Named->getDecl()) : nullptr;
  return Parameter && Parameter->isExplicitObjectParameter() &&
         Parameter->getType()->isReferenceType();
}

/// The members of the object of the member function whose body \p E is in
/// that \p E designates or is in, the outermost first, by the members it is
/// reached through from `this` or an explicit object parameter that refers
/// to it (namesObject), `m_Inner` and `m_Value` of
/// `m_Inner.m_Value`; with \p Element, also through a local reference bound
/// to one and an operator `[]` of one, the first of which \p Element is set
/// to. Empty of what else.
static llvm::SmallVector<const FieldDecl *, 2>
thisMemberPath(const Expr *E, const Expr **Element = nullptr) {
  llvm::SmallVector<const FieldDecl *, 2> Path;
  for (int Step = 0; E && Step < 64; ++Step) {
    E = E->IgnoreImplicit()->IgnoreParens();
    if (const auto *Member = dyn_cast<MemberExpr>(E)) {
      const auto *Of = dyn_cast<FieldDecl>(Member->getMemberDecl());
      if (!Of || (Member->isArrow() &&
                  !isa<CXXThisExpr>(Member->getBase()->IgnoreImplicit()->IgnoreParens())))
        return {};
      Path.insert(Path.begin(), Of);
      E = Member->getBase();
      continue;
    }
    if (!Element)
      break;
    if (const auto *Subscript = dyn_cast<CXXOperatorCallExpr>(E);
        Subscript && !*Element &&
        Subscript->getOperator() == OO_Subscript &&
        Subscript->getNumArgs() == 2) {
      *Element = Subscript;
      E = Subscript->getArg(0);
      continue;
    }
    const auto *Named = dyn_cast<DeclRefExpr>(E);
    const auto *Variable =
        Named ? dyn_cast<VarDecl>(Named->getDecl()) : nullptr;
    if (Variable && Variable->getType()->isReferenceType() &&
        Variable->hasLocalStorage() && !isa<ParmVarDecl>(Variable) &&
        Variable->getInit()) {
      E = Variable->getInit();
      continue;
    }
    break;
  }
  return E && namesObject(E) ? Path
                                  : llvm::SmallVector<const FieldDecl *, 2>();
}

/// The member of the object of the member function whose body \p E is in
/// that \p E designates or is in, by the members it is reached through from
/// `this`, `m_Inner` of `m_Inner.m_Value`; null of what else.
static const FieldDecl *memberOfThis(const Expr *E) {
  const llvm::SmallVector<const FieldDecl *, 2> Path = thisMemberPath(E);
  return Path.empty() ? nullptr : Path.front();
}

CThisMember thisMemberOf(const Expr *E, const FunctionDecl *Function) {
  CThisMember Member;
  const Expr *Element = nullptr;
  Member.Path = thisMemberPath(E, &Element);
  if (Member.Path.empty())
    return {};
  Member.Function = Function;
  Member.Element = Element;
  return Member;
}

/// The members of the object of \p Method it can change: what its body, and
/// that of each member function it calls on the same object, assigns,
/// steps, calls a member function that can change it on or gives where it
/// can be changed, of each a member of the object, `m_nAccess` of
/// `++m_nAccess;`. None told where a body can't be seen, an override can run
/// (a virtual one that isn't final nor of a final class, unless of \p Method
/// \p Static, called where no override can run, callsOverride), the object
/// is an explicit object parameter that isn't a reference, or the object
/// itself (`this` or a reference explicit object parameter, namesObject) is
/// given or changed, other than given back. A work stack over the member
/// functions, each once.
static std::optional<llvm::SmallVector<const FieldDecl *, 4>>
changedMembers(const CXXMethodDecl *Method, ASTContext &Context,
               bool Static = false) {
  Static = Static && Method->isVirtual();
  auto &Cache = astCache<CChangedMembers>(Context).Of;
  const llvm::PointerIntPair<const CXXMethodDecl *, 1, bool> Key(Method,
                                                                  Static);
  if (const auto It = Cache.find(Key); It != Cache.end())
    return It->second;
  std::optional<llvm::SmallVector<const FieldDecl *, 4>> Changed;
  Changed.emplace();
  llvm::SmallVector<const CXXMethodDecl *, 4> Work{Method};
  llvm::SmallPtrSet<const CXXMethodDecl *, 8> Seen;
  while (!Work.empty() && Changed) {
    const CXXMethodDecl *Each = Work.pop_back_val();
    if (!Seen.insert(Each).second)
      continue;
    const auto *Definition =
        dyn_cast_or_null<CXXMethodDecl>(Each->getDefinition());
    // An override can run of a virtual one, unless it is final or of a
    // final class.
    if (!Definition || !Definition->getBody() ||
        (Definition->isVirtual() && !(Static && Each == Method) &&
         !Definition->hasAttr<FinalAttr>() &&
         !Definition->getParent()->isEffectivelyFinal()) ||
        (Definition->isExplicitObjectMemberFunction() &&
         !Definition->getParamDecl(0)->getType()->isReferenceType()) ||
        isa<CXXConstructorDecl, CXXDestructorDecl>(Definition)) {
      Changed.reset();
      break;
    }
    const CEffectIndex &Index =
        sharedFlowCache().indexOf(Definition->getBody(), Context);
    auto Changes = [&](const Expr *Target) {
      if (!Target || !Changed)
        return;
      if (const FieldDecl *Field = memberOfThis(Target)) {
        if (!llvm::is_contained(*Changed, Field))
          Changed->push_back(Field);
        return;
      }
      // The object itself, or a pointer to it given on.
      if (namesObject(Target->IgnoreImplicit()->IgnoreParens()))
        Changed.reset();
    };
    forEachRunning(Definition, Definition->getBody(), Definition->getBody(),
                   Context, [&](const Stmt *S) {
                     const CEffects *Effects = Index.effectsOf(S);
                     if (!Effects || !Changed)
                       return;
                     for (const COperandEffect &Effect : Effects->Operands) {
                       const auto *Operand = dyn_cast<Expr>(Effect.Operand);
                       switch (Effect.Use) {
                       case EUse::Copy:
                       case EUse::Bind:
                         if (Effect.Into.Kind == EDestination::Designated)
                           Changes(dyn_cast_or_null<Expr>(Effect.Into.Object));
                         else if (Effect.Into.Kind ==
                                      EDestination::Parameter &&
                                  Operand &&
                                  (Effect.Use == EUse::Bind ||
                                   Operand->getType()->isPointerType())) {
                           const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(
                               Effect.Into.Declaration);
                           if (!Parameter ||
                               changesThrough(Parameter->getType()))
                             Changes(Operand);
                         } else if (Operand &&
                                    Effect.Into.Kind != EDestination::Return &&
                                    namesObject(Operand->IgnoreImplicit()
                                                    ->IgnoreParens()))
                           Changed.reset();
                         break;
                       case EUse::Update:
                         Changes(Operand);
                         break;
                       case EUse::CallObject: {
                         const auto *Called =
                             dyn_cast_or_null<CXXMethodDecl>(Effects->Callee);
                         if (!Called || hasConstObject(Called) ||
                             Called->isStatic() || onlyGivesAccess(Called))
                           break;
                         if (Operand && namesObject(Operand->IgnoreImplicit()
                                                        ->IgnoreParens()))
                           Work.push_back(Called);
                         else
                           Changes(Operand);
                         break;
                       }
                       default:
                         break;
                       }
                     }
                   });
  }
  Cache[Key] = Changed;
  return Changed;
}

namespace {
/// A part of what a parameter refers to or points to that the function can
/// change through it (parameterWrites): the members to it, the outermost
/// first, and of an element of an array member, the element, by its index
/// where that is a constant.
struct CParameterWrite {
  llvm::SmallVector<const FieldDecl *, 2> Path;
  bool Element = false;
  std::optional<int64_t> Index = std::nullopt;
};

/// Of each function and parameter, what it can change through it
/// (parameterWrites), once told.
struct CParameterWrites {
  llvm::DenseMap<std::pair<const FunctionDecl *, unsigned>,
                 std::optional<llvm::SmallVector<CParameterWrite, 2>>>
      Of;
};
} // namespace

/// The parts of what the parameter \p iParameter of \p Function refers to or
/// points to that its body can change through it (CParameterWrite): of each
/// name of the parameter, where each use of it goes, through `->`, `*`, `.`
/// and the element of an array member, `_pBatch->m_pJobs[0]`, to a part that
/// is assigned, stepped, given a member function that can change it is
/// called on, or bound to a parameter that can change it; a part only read,
/// loaded, copied, compared or called a member function on that can't
/// change it, nothing. None told of a body that can't be seen, a virtual
/// member function, a parameter that is neither a pointer nor a reference,
/// or one whose pointer or location is used in another way: kept, given on,
/// its address taken. A work stack over the uses, at most 256 steps.
static std::optional<llvm::SmallVector<CParameterWrite, 2>>
parameterWrites(const FunctionDecl *Function, unsigned iParameter,
                ASTContext &Context) {
  const FunctionDecl *Definition = nullptr;
  if (!Function || !Function->hasBody(Definition) || !Definition ||
      !Definition->getBody() || iParameter >= Definition->getNumParams() ||
      isa<CoroutineBodyStmt>(Definition->getBody()))
    return std::nullopt;
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Definition);
      Method && Method->isVirtual())
    return std::nullopt;
  auto &Cache = astCache<CParameterWrites>(Context).Of;
  const std::pair<const FunctionDecl *, unsigned> Key{Definition, iParameter};
  if (const auto It = Cache.find(Key); It != Cache.end())
    return It->second;
  const ParmVarDecl *Parameter = Definition->getParamDecl(iParameter);
  const QualType Type = Parameter->getType();
  std::optional<llvm::SmallVector<CParameterWrite, 2>> Writes;
  if (Type->isPointerType() || Type->isReferenceType()) {
    Writes.emplace();
    const CEffectIndex &Index =
        sharedFlowCache().indexOf(Definition->getBody(), Context);
    // Where a use is: the expression, whether it is the pointer the
    // parameter holds (not yet followed), the part it designates, and
    // whether its element is indexed.
    struct CPlace {
      const Expr *At;
      bool Pointer;
      CParameterWrite Part;
      bool Indexed = false;
    };
    llvm::SmallVector<CPlace, 8> Work;
    for (const DeclRefExpr *Named : sharedFlowCache().namesIn(
             Definition, Definition->getBody(), Parameter, Context))
      Work.push_back({Named, Type->isPointerType(), {}});
    for (int Step = 0; !Work.empty() && Writes; ++Step) {
      if (Step >= 256) {
        Writes.reset();
        break;
      }
      CPlace Each = Work.pop_back_val();
      const llvm::SmallVector<CEffectIndex::CUse, 2> Uses =
          Index.consumersOf(Each.At);
      if (Uses.empty()) {
        Writes.reset();
        break;
      }
      for (const CEffectIndex::CUse &Use : Uses) {
        const auto *Parent = dyn_cast_or_null<Expr>(Use.Parent);
        if (!Parent || !Use.Effect) {
          Writes.reset();
          break;
        }
        CPlace Next{Parent, Each.Pointer, Each.Part, Each.Indexed};
        // The member it is the base of, of a part not yet an element.
        auto Member = [&]() {
          const auto *Of = dyn_cast<MemberExpr>(Parent->IgnoreParens());
          const auto *Field =
              Of ? dyn_cast<FieldDecl>(Of->getMemberDecl()) : nullptr;
          if (!Field)
            return false;
          if (!Next.Part.Element)
            Next.Part.Path.push_back(Field);
          return true;
        };
        bool Written = false;
        switch (Use.Effect->Use) {
        case EUse::Forward:
          // Parentheses, casts, the base of `.`.
          if (isa<MemberExpr>(Parent->IgnoreParens()) && !Member()) {
            Writes.reset();
            break;
          }
          Work.push_back(Next);
          continue;
        case EUse::Access:
          // The pointer loaded goes on; a part loaded is read.
          if (Each.Pointer && Each.At->getType()->isPointerType() &&
              Each.Part.Path.empty() && !Each.Part.Element &&
              isa<ImplicitCastExpr>(Parent)) {
            Work.push_back(Next);
            continue;
          }
          // A pointer member loaded gives what it points to, which a slot
          // can be reached through.
          if (!Each.Pointer && Each.At->getType()->isPointerType())
            Writes.reset();
          break;
        case EUse::Dereference: {
          // `p->m`, `*p`, and the element of an array member, `m_A[0]`.
          if (Each.Pointer) {
            Next.Pointer = false;
            if (isa<MemberExpr>(Parent->IgnoreParens())) {
              if (!Member()) {
                Writes.reset();
                break;
              }
            } else if (const auto *Unary = dyn_cast<UnaryOperator>(Parent);
                       !Unary || Unary->getOpcode() != UO_Deref) {
              Writes.reset();
              break;
            }
            Work.push_back(Next);
            continue;
          }
          const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Parent);
          if (!Subscript || !Each.Part.Element || Each.Indexed) {
            Writes.reset();
            break;
          }
          if (const std::optional<llvm::APSInt> Index =
                  constantIntegerOf(Subscript->getIdx(), Context))
            Next.Part.Index = signedValueOf(*Index);
          Next.Indexed = true;
          Work.push_back(Next);
          continue;
        }
        case EUse::AddressOf: {
          // An array member that decays to be indexed.
          const auto *Decay = dyn_cast<ImplicitCastExpr>(Parent);
          if (!Decay || Decay->getCastKind() != CK_ArrayToPointerDecay ||
              Each.Part.Element || Each.Part.Path.empty()) {
            Writes.reset();
            break;
          }
          Next.Part.Element = true;
          Work.push_back(Next);
          continue;
        }
        case EUse::Write:
        case EUse::Update:
          Written = true;
          break;
        case EUse::CallObject: {
          const auto *Called = dyn_cast_or_null<CXXMethodDecl>(
              isa<CallExpr>(Parent) ? cast<CallExpr>(Parent)->getDirectCallee()
                                    : nullptr);
          if (!Called) {
            Writes.reset();
            break;
          }
          // One that can't change it but gives access to what it holds,
          // `operator->` of an owner, gives what a slot can be reached
          // through.
          Written = !Called->isStatic() && !hasConstObject(Called);
          if (!Written && (Called->getReturnType()->isPointerType() ||
                           Called->getReturnType()->isReferenceType()))
            Writes.reset();
          break;
        }
        case EUse::Bind: {
          const auto *Given =
              Use.Effect->Into.Kind == EDestination::Parameter
                  ? dyn_cast_or_null<ParmVarDecl>(Use.Effect->Into.Declaration)
                  : nullptr;
          if (!Given || Each.Pointer) {
            Writes.reset();
            break;
          }
          // Of a body that can change it, also one that casts the const
          // away (changedThroughConst).
          Written = changesThrough(Given->getType())
                        ? changedThroughParameter(Parent, Given, Context)
                        : changedThroughConst(Parent, Given, Context);
          break;
        }
        case EUse::Copy:
          // A copy of the pointer keeps it where it can't be followed.
          if (Each.Pointer && Each.At->getType()->isPointerType()) {
            Writes.reset();
            break;
          }
          break;
        case EUse::Condition:
        case EUse::Value:
        case EUse::Discarded:
          break;
        case EUse::Callee:
        case EUse::Statement:
        case EUse::Unevaluated:
        case EUse::Opaque:
          Writes.reset();
          break;
        }
        if (!Writes)
          break;
        if (Written) {
          if (Each.Pointer) {
            Writes.reset();
            break;
          }
          Writes->push_back(Each.Part);
        }
      }
    }
  }
  Cache[Key] = Writes;
  return Writes;
}

CWrittenParts writtenPartsOf(const Expr *Given, const CSlot &Slot,
                             ASTContext &Context) {
  CWrittenParts Out;
  // The call it is given to, and the parameter it is given.
  const Expr *Argument = Given ? Given->IgnoreParenImpCasts() : nullptr;
  const CEffectIndex::CUse *Into = nullptr;
  llvm::SmallVector<CEffectIndex::CUse, 2> Uses;
  if (const FunctionDecl *Function =
          Argument ? slotFunction(Slot.Variable, Given) : nullptr;
      Function && Function->getBody()) {
    Uses = sharedFlowCache().indexOf(Function->getBody(), Context)
               .consumersOf(Given);
    for (const CEffectIndex::CUse &Use : Uses)
      if (Use.Effect && isa_and_nonnull<CallExpr>(Use.Parent) &&
          (Use.Effect->Use == EUse::Copy || Use.Effect->Use == EUse::Bind) &&
          Use.Effect->Into.Kind == EDestination::Parameter &&
          Use.Effect->Into.Callee)
        Into = &Use;
  }
  if (!Into)
    return Out;
  const auto *Call = cast<CallExpr>(Into->Parent);
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Into->Effect->Into.Callee);
      Method && mayRunOverride(Call, Method))
    return Out;
  const std::optional<llvm::SmallVector<CParameterWrite, 2>> Writes =
      parameterWrites(Into->Effect->Into.Callee, Into->Effect->Into.Index,
                      Context);
  if (!Writes)
    return Out;
  // What the parameter refers to or points to, of the slot.
  bool Told = true;
  const llvm::SmallVector<CSlot, 2> Of =
      Given->getType()->isPointerType()
          ? pointedSlots(Given, Context, &Told)
          : designatedSlots(Given, Context, &Told);
  if (!Told || Of.size() != 1 || Of.front().Variable != Slot.Variable ||
      Of.front().Element || !startsWith(Slot.Path, Of.front().Path))
    return Out;
  const llvm::ArrayRef<const FieldDecl *> Rest =
      llvm::ArrayRef(Slot.Path).drop_front(Of.front().Path.size());
  Out.Told = true;
  Out.Writes = false;
  bool Indexed = Slot.Element;
  for (const CParameterWrite &Each : *Writes) {
    const size_t Common = std::min(Each.Path.size(), Rest.size());
    if (!std::equal(Each.Path.begin(), Each.Path.begin() + Common,
                    Rest.begin()))
      continue;
    Out.Writes = true;
    if (Each.Path.size() == Rest.size() && Each.Element && Each.Index)
      Out.Indices.push_back(*Each.Index);
    else
      Indexed = false;
  }
  if (!Indexed)
    Out.Indices.clear();
  return Out;
}

namespace {
/// Why a fact of a function's summary can't be told (CFlowFact::Reason).
enum class EUntold : unsigned char {
  None,
  /// A change whose value isn't told reaches the exit: a call of what can't
  /// be seen or changes it in a way that isn't told, a suspension, a write
  /// through what can't be told.
  Change,
  /// A value that isn't what the caller gives, or static storage, or
  /// nothing: a member of the object, what a way of provenance can't follow.
  Expression,
  /// The function reaches itself and its summary didn't settle.
  Recursion,
  /// The function reaches itself and its summary took as many passes as the
  /// engine takes at most (CSummaryEngine::MaxTotalPasses).
  Budget,
  /// The place's address can be used where the flow can't follow it, what a
  /// reference parameter refers to that escapes (CFacts::Told).
  Escaped,
};

/// A value a function leaves in a place, in the terms of what its caller
/// gives it (CFlowFact::Sources).
struct CFlowSource {
  enum class EKind : unsigned char {
    /// What the caller gives the parameter Index, as it is.
    Argument,
    /// The address of what the caller gives the reference parameter Index,
    /// of a part of it with Part.
    ArgumentAddress,
    /// The address of the static storage Named, of a part of it with Part.
    StaticAddress,
    /// Nothing, the null pointer Named.
    Nothing,
  };
  EKind Kind = EKind::Argument;
  unsigned Index = 0;
  bool Part = false;
  const Expr *Named = nullptr;
  bool operator==(const CFlowSource &Other) const {
    return Kind == Other.Kind && Index == Other.Index && Part == Other.Part &&
           Named == Other.Named;
  }
};

/// What a function leaves in a member of its object, Path, where it returns
/// (CFunctionSummary): each value that can be what it holds there (Sources),
/// whether those are all and the value it held where the function was
/// entered is none of them (EveryPath), and why that can't be told (Reason).
struct CFlowFact {
  llvm::SmallVector<const FieldDecl *, 2> Path;
  /// Of an element of the member Path, of an array or a container it holds
  /// (ownedElement), its constant index.
  bool Element = false;
  std::optional<int64_t> Index = std::nullopt;
  llvm::SmallVector<CFlowSource, 1> Sources;
  bool EveryPath = false;
  EUntold Reason = EUntold::None;
  /// Of what a parameter refers to or points to (CFunctionSummary::
  /// Parameters), the parameter's index.
  unsigned Target = 0;
  /// What writes whose place can't be told, of storage only code that can't
  /// be seen gives (CDefinition::Unseen), can leave there too, and why that
  /// can't be told: what it can hold where what the caller gives is reached
  /// by such code, not where it is a local of the caller whose address
  /// doesn't escape (CLocalFlow::reachedByUnseen).
  llvm::SmallVector<CFlowSource, 1> Reached;
  EUntold ReachedReason = EUntold::None;
  bool operator==(const CFlowFact &Other) const {
    return Path == Other.Path && Element == Other.Element &&
           Index == Other.Index && Sources == Other.Sources &&
           EveryPath == Other.EveryPath && Reason == Other.Reason &&
           Target == Other.Target && Reached == Other.Reached &&
           ReachedReason == Other.ReachedReason;
  }
};

/// The write half of what a function does, worked out by its flow
/// (CLocalFlow::summarize), what a call of it leaves in the members of the
/// object it is called on (Leaves), of a member function, and in the pointer
/// a parameter refers to or points to, of one given by reference or by its
/// address, `int *&o_pValue` or `int **o_ppValue` (Parameters, setsParameter). While it is worked out in a recursion
/// first, also while its flow is (CLocalFlow::Building), Top: a way through
/// a call of it is one the function doesn't take, so what holds on every
/// other way holds. Where the recursion doesn't settle, what it leaves in
/// each member can't be told (Reason, EUntold::Recursion): no fact holds.
struct CFunctionSummary {
  llvm::SmallVector<CFlowFact, 2> Leaves;
  llvm::SmallVector<CFlowFact, 1> Parameters;
  bool Top = false;
  EUntold Reason = EUntold::None;
  bool operator==(const CFunctionSummary &Other) const {
    return Leaves == Other.Leaves && Parameters == Other.Parameters &&
           Top == Other.Top && Reason == Other.Reason;
  }
};

/// Whether \p Parameter is a pointer a function can set for its caller: one
/// given by a reference that isn't to const, `int *&o_pValue`, or by its
/// address, `int **o_ppValue`, of a pointer that isn't const.
bool setsParameter(const ParmVarDecl *Parameter) {
  const QualType Type = Parameter->getType();
  const QualType Pointer = Type->isLValueReferenceType()
                               ? Type.getNonReferenceType()
                           : Type->isPointerType() ? Type->getPointeeType()
                                                   : QualType();
  return !Pointer.isNull() && Pointer->isPointerType() &&
         !Pointer.isConstQualified();
}

/// Whether what \p Parameter refers to or points to can be written through
/// it, of what the caller gives it as \p Operand by \p Use: a reference to
/// what isn't const, bound to a glvalue, or a pointer to what isn't const,
/// copied; not a pointer it can set (setsParameter), which is a summary's
/// fact as a whole.
static bool writesReferent(const ParmVarDecl *Parameter, EUse Use,
                           const Expr *Operand) {
  const QualType Type = Parameter->getType();
  if (setsParameter(Parameter))
    return false;
  if (Type->isReferenceType())
    return Use == EUse::Bind && Operand->isGLValue() &&
           !Type.getNonReferenceType().isConstQualified();
  return Type->isPointerType() && Use == EUse::Copy &&
         Operand->getType()->isPointerType() &&
         !pointsToConst(Type);
}
} // namespace

class CLocalFlow {
public:
  CLocalFlow(const FunctionDecl *Function, ASTContext &Context);
  /// Finds what gives the slots values, and the locals that escape.
  void initialize();

  // With \p Final, as localSlotValues tells it.
  CSlotValues valuesAt(const CSlot &Slot, const Stmt *At,
                       bool *Final = nullptr) const;
  // What \p Slot can hold where the scope of its variable ends: the values
  // that reach each automatic destructor of the variable in the flow.
  CSlotValues valuesAtEnd(const CSlot &Slot, bool *Final = nullptr) const;
  CSlotReaching definitionsAt(const CSlot &Slot, const Stmt *At) const;
  bool unchangedBetween(const CSlot &Slot, const Stmt *From,
                        const Stmt *To) const;
  std::optional<llvm::SmallVector<CSlotDefinitionAt, 2>> definitionsReaching(
      const CSlot &Slot, llvm::function_ref<bool(const CSlotDefinition &)> Ends,
      llvm::function_ref<bool(const CFlowEvent &)> Stops,
      llvm::function_ref<bool(const CFlowEvent &)> Reached) const;
  std::optional<llvm::SmallVector<CThisDefinition, 2>>
  objectDefinitions(const CSlot &Slot, const Stmt *At) const;
  bool objectUnchangedBetween(const CSlot &Slot, const Stmt *From,
                              const Stmt *To) const;
  bool placeUnchangedBetween(const CSlot &Slot, const Stmt *From,
                             const Stmt *To) const;
  /// The summary of the function (CFunctionSummary), or, of \p Referent,
  /// what it leaves in what its parameter of that index refers to
  /// (CFlowSummaries::ParameterSummaries).
  CFunctionSummary
  summarize(std::optional<unsigned> Referent = std::nullopt) const;
  /// What reaches the exit of \p Place, a place of the flow that isn't a
  /// local slot, in the terms of the caller (summarize).
  CFlowFact factOf(const CSlot &Place) const;
  /// How many writes the flow has, of which the targets are summaries.
  unsigned writeCount() const { return Writes.size(); }
  CSlotValues valuesFrom(const CSlot &Slot, const Stmt *At) const;
  CSlotValues assignments(const CSlot &Slot) const;
  bool changedIn(const CSlot &Slot) const;
  llvm::SmallVector<CSlot, 2> writtenParts(const CSlot &Slot) const;
  bool wholeReaches(const CSlot &Part, const Stmt *At) const;
  llvm::SmallVector<CClosureWrite, 2> closureWritesAt(const Stmt *Call) const;
  llvm::SmallVector<CClosureWrite, 2>
  closureWritesAtEnd(const VarDecl *Guard) const;
  /// The facts of \p Slot as worked out of this flow (observeFacts).
  CFacts makeFacts(const CSlot &Slot) const;
  /// What the write \p iWrite designates as worked out (targetsOn).
  CTargets makeTargets(unsigned iWrite) const;
  /// The parts of \p Slot as worked out of this flow (writtenParts).
  llvm::SmallVector<CSlot, 2> makeParts(const CSlot &Slot) const;
  /// Whether \p Other was made the same: what gives the slots values, the
  /// closures that run, and the locals that escape.
  bool operator==(const CLocalFlow &Other) const;

private:
  /// What gives slots a value, where in the flow.
  struct CWrite {
    enum class EKind {
      /// What declares Variable.
      Declare,
      /// What is assigned to what Target designates, Value.
      Assign,
      /// What can change what Target designates, of what can't be told.
      Opaque,
      /// What can change what the pointer Target points to.
      Through,
      /// What a closure assigns to what Target, in its body, designates of
      /// the slots of the function (closureTargets), Value, where it can be
      /// called, only maybe.
      Closure,
    };
    EKind Kind = EKind::Opaque;
    unsigned Block = 0;
    unsigned Index = 0;
    const VarDecl *Variable = nullptr;
    const Expr *Target = nullptr;
    CSlotValue Value = {};
    bool Known = true;
    // Of a closure, whether it assigns it each time it runs, called there.
    bool Definite = false;
    // Of an assignment, whether it is only maybe made, by a setter that
    // doesn't always assign it.
    bool Maybe = false;
    // Of a member function called on what Target designates or points to,
    // whether the members of it that it can change are told (changedMembers),
    // and those members; it changes no other.
    bool MembersTold = false;
    llvm::SmallVector<const FieldDecl *, 2> Members = {};
    // Of an assignment, the member of what Target designates that it
    // assigns, by members from it, as a setter called on it does
    // (CFunctionSummary); empty where it assigns what Target designates.
    llvm::SmallVector<const FieldDecl *, 1> Path = {};
    // Of an assignment of Path, of an element of it, its constant index
    // (CFlowFact::Element).
    bool PathElement = false;
    std::optional<int64_t> PathIndex = std::nullopt;
    // Of an assignment, whether Target is a pointer and what it points to is
    // assigned, `*o_ppJob = _pJob;` of `fg_Set(&pJob, ...)`.
    bool ByPointer = false;
    // Whether it can change the object of the function as a whole, where
    // other code can run on it: a suspension, which has no Target.
    bool Object = false;
    // Of a call of a member function whose body is seen and that no
    // override can replace, on what Target designates or points to, the
    // function and the call: what it leaves in the members of what it is
    // called on (summaryWrites) is told where a slot it can write is asked of.
    const FunctionDecl *Summarized = nullptr;
    const Stmt *Call = nullptr;
    // Of a call of a function given what Target designates, or points to, as
    // a pointer it can set (setsParameter), the parameter's index and one.
    unsigned Sets = 0;
    // Whether what the call leaves (summaryWrites) can be a part of what
    // Target designates, which a member function leaves in the members of its
    // object; a pointer it sets it sets as a whole.
    // Of a call of a function given what Target designates, or points to, by
    // a reference or a pointer to what isn't const, other than a pointer it
    // can set, the parameter's index and one: what the call leaves in the
    // members of what it refers to is told by the summary of that parameter
    // (CFlowSummaries::ParameterSummaries).
    unsigned Referent = 0;
    bool leavesParts() const { return Summarized && !Sets; }
    // Of what a call leaves (summaryWrites), whether it is a write of storage
    // only code that can't be seen gives in the callee (CDefinition::Unseen).
    bool Unseen = false;
    bool operator==(const CWrite &Other) const {
      return Kind == Other.Kind && Block == Other.Block &&
             Index == Other.Index && Variable == Other.Variable &&
             Target == Other.Target && Value == Other.Value &&
             Known == Other.Known && Definite == Other.Definite &&
             Maybe == Other.Maybe && MembersTold == Other.MembersTold &&
             Members == Other.Members && Path == Other.Path &&
             PathElement == Other.PathElement &&
             PathIndex == Other.PathIndex &&
             ByPointer == Other.ByPointer && Object == Other.Object &&
             Summarized == Other.Summarized && Call == Other.Call &&
             Sets == Other.Sets && Unseen == Other.Unseen &&
             Referent == Other.Referent;
    }
  };

  using CKey = std::pair<const VarDecl *, CPath>;
  // What \p fObserve tells of the facts of \p Slot, which is all of them
  // that is used (TCSummaries::observe).
  template <typename tf_FObserve>
  auto observeFacts(const CSlot &Slot, tf_FObserve fObserve) const;
  // What \p fRead tells of the facts of \p Slot, all of them used: fRead
  // isn't kept, so it can refer to what lives only in the call.
  template <typename tf_FRead>
  auto readFacts(const CSlot &Slot, tf_FRead fRead) const;
  // Whether the facts of \p Slot are final (TCSummaries::finished).
  bool factsFinal(const CSlot &Slot) const;
  CSlotValues valuesIn(const CFacts &Facts, const Stmt *At) const;
  std::optional<llvm::BitVector> reachingRead(const CFacts &Facts,
                                              const Stmt *At) const;
  void primeTargets() const;
  CTargetsOn targetsOn(unsigned iWrite, const VarDecl *Variable,
                       EPlace Place) const;
  llvm::SmallVector<CWrite, 2> summaryWrites(const CWrite &Write,
                                             bool *Top = nullptr,
                                             bool *Replaces = nullptr,
                                             bool Reached = true) const;
  CTargetsOn composedOn(unsigned iWrite, const CWrite &Composed,
                        const VarDecl *Variable, EPlace Place) const;
  CTargets composedTargets(unsigned iWrite, const CWrite &Composed) const;
  bool writesSlot(unsigned iWrite, const CSlot &Slot) const;
  // Whether code that can't be seen can reach \p Slot: what isn't a local
  // slot, the object of the function or what a parameter refers to, whose
  // origin isn't known here, or a local whose address escapes.
  bool reachedByUnseen(const CSlot &Slot) const {
    return Slot.Place != EPlace::Variable || !Ready ||
           Escaped.contains(Slot.Variable);
  }
  void flow(const CFacts &Facts) const;
  void indexFacts(const CFacts &Facts) const;
  llvm::SmallVector<CDefinition, 8> objectChanges(const CSlot &Slot) const;
  std::optional<llvm::SmallVector<CDefinition, 2>>
  objectReaching(const CSlot &Slot, const Stmt *At) const;
  bool soleReaches(const CFacts &Facts,
                   std::pair<unsigned, unsigned> Position) const;
  std::optional<std::pair<unsigned, unsigned>> positionOf(const Stmt *At) const;
  bool unordered(const Stmt *Left, const Stmt *Right) const;
  const Stmt *expressionOf(const Stmt *S) const;
  llvm::BitVector reachingAt(const CFacts &Facts, unsigned Block,
                             unsigned Index) const;
  bool inClosureBody(const Stmt *At) const;
  void findEscapes();
  llvm::SmallVector<CSlot, 2> closureTargets(const Expr *Target,
                                             bool &Told) const;
  llvm::SmallVector<const Stmt *, 4>
  callsOf(const LambdaExpr *Lambda, llvm::ArrayRef<const Stmt *> Calls,
          const VarDecl *&Guard, bool *Escapes = nullptr,
          llvm::SmallVectorImpl<std::pair<unsigned, unsigned>> *Ends = nullptr,
          llvm::DenseSet<const Stmt *> *Calling = nullptr) const;

public:
  // Whether the closure \p Lambda, made in the function, can run where it
  // is made: called where a call can run, given to a guard, or given where
  // the flow can't follow it (callsOf).
  bool runs(const LambdaExpr *Lambda) const {
    return RunClosures.contains(Lambda);
  }
  // Whether the closure \p Lambda, made in the function, runs only while the
  // function runs: it isn't given where it can be run later (callsOf).
  bool runsInCall(const LambdaExpr *Lambda) const {
    // Not one made where the body doesn't make it, in a default member
    // initializer the constructor uses.
    if (!Graph || !Ready || Index->usesOf(Lambda).empty())
      return false;
    const VarDecl *Guard = nullptr;
    bool Escapes = false;
    (void)callsOf(Lambda, {}, Guard, &Escapes);
    return !Escapes;
  }

  // Whether the address of the local \p Variable can be used where the flow
  // can't follow it (findEscapes).
  bool escapes(const VarDecl *Variable) const {
    return !Ready || Escaped.contains(Variable);
  }



private:
  llvm::DenseSet<const LambdaExpr *> RunClosures;
  llvm::SmallVector<std::pair<unsigned, unsigned>, 2>
  endsOf(const VarDecl *Variable) const;

  const FunctionDecl *Function;
  ASTContext &Context;
  const CFG *Graph = nullptr;
  llvm::BitVector Reachable;
  const CFlowOrder *FlowOrder = nullptr;
  const CEffectIndex *Index = nullptr;
  /// The closure whose call operator it is, of a lambda.
  const LambdaExpr *Closure = nullptr;
  // The dominators of Graph and its blocks by ID, made when first asked
  // (soleReaches).
  struct CDominators {
    CFGDomTree Tree;
    std::vector<const CFGBlock *> Blocks;
  };
  mutable std::shared_ptr<CDominators> Dominators;
  // The elements past which a way leaves the function other than by a
  // return, by `throw` or a call that doesn't return, as block and index,
  // once asked (objectReaching).
  mutable std::optional<std::vector<std::pair<unsigned, unsigned>>> Leaving;
  llvm::DenseMap<const Stmt *, std::pair<unsigned, unsigned>> Positions;
  // The outermost expression each statement asked of is in (expressionOf).
  mutable llvm::DenseMap<const Stmt *, const Stmt *> Expressions;
  llvm::DenseMap<const VarDecl *, std::pair<unsigned, unsigned>> Declarations;
  std::vector<CWrite> Writes;
  // What a closure assigns: the target in its body, the value, and whether
  // it replaces what the slot held each time it runs (assignedOnEachWay), of
  // one called there.
  struct CAssign {
    const Expr *Target;
    CSlotValue Value;
    bool Definite = false;
    // Whether each way to the exit of the closure assigns the slot again,
    // so that what it assigns isn't what the slot holds after it runs.
    bool Overwritten = false;
    // Whether it is a change whose value can't be told, by a member function
    // that isn't const called on what Target designates, or Target given
    // where it can be changed, not an assignment.
    bool Changed = false;
    bool operator==(const CAssign &Other) const {
      return Target == Other.Target && Value == Other.Value &&
             Definite == Other.Definite && Overwritten == Other.Overwritten &&
             Changed == Other.Changed;
    }
  };
  /// What the closures each call can run assign.
  llvm::DenseMap<const Stmt *, llvm::SmallVector<CAssign, 2>> ClosureCalls;
  /// What the closures the local objects that run them where they end, the
  /// guards of a scope exit, assign there.
  llvm::DenseMap<const VarDecl *, llvm::SmallVector<CAssign, 2>> ClosureEnds;
  // Where those guards end and run them, uncancelled, by block and element.
  llvm::DenseSet<std::pair<unsigned, unsigned>> EndsRunning;

public:
  // Whether the end of a guard at element \p Index of block \p Block runs
  // what it is given (closureWritesAtEnd), not after it is cancelled.
  bool runsAtEnd(unsigned Block, unsigned Index) const {
    return EndsRunning.contains({Block, Index});
  }

private:
  // Of what a closure's operator assigns, by index: what replaces what a
  // slot held, the first that reaches the exit of each slot assigned on
  // each way there, and what is overwritten on each way there.
  struct CExitWrites {
    llvm::SmallVector<size_t, 2> Definite;
    llvm::SmallVector<size_t, 2> Overwritten;
  };
  CExitWrites
  assignedOnEachWay(const FunctionDecl *Operator,
                    llvm::ArrayRef<CAssign> Assigned,
                    llvm::ArrayRef<std::pair<const Stmt *, size_t>> Candidates);
  /// The locals whose address can be used where the flow can't follow it.
  llvm::DenseSet<const VarDecl *> Escaped;
  /// The reference parameters what they refer to of which can be used where
  /// the flow can't follow it.
  llvm::DenseSet<const VarDecl *> EscapedReferents;
  /// Whether what escapes is told, after which what is told of a slot is
  /// kept.
  bool Ready = false;

public:
  /// Of the one a recursion takes while the flow is made (LocalFlows), which
  /// tells nothing, and of which a function summary is Top.
  bool Building = false;
};

/// The summaries of the flows of a translation unit (TCSummaries), each a
/// function of what it is of: whether a function only calls what a
/// parameter is given, what the uses of a closure are, the local flow of a
/// function, the facts of its slots and what its writes designate, what a
/// function gives back, and what an expression designates.
struct CFlowSummaries {
  explicit CFlowSummaries(ASTContext &Context);
  // A closure value or a declaration whose names hold one, \p Root, of \p
  // Body of \p Owner, and whether it is a declaration (closureUsesOf).
  using CClosureKey =
      std::tuple<const Decl *, const Stmt *, const void *, unsigned>;
  TCSummaries<std::pair<const FunctionDecl *, unsigned>, bool> OnlyCalls;
  TCSummaries<CClosureKey, CClosureUses> ClosureUses;
  TCSummaries<const FunctionDecl *, CLocalFlow> LocalFlows;
  TCSummaries<CSlotKey, CFacts> Facts;
  TCSummaries<std::pair<const FunctionDecl *, unsigned>, CTargets> Targets;
  TCSummaries<CSlotKey, llvm::SmallVector<CSlot, 2>> Parts;
  // Of each function, the first write whose targets aren't asked by a
  // priming yet (CLocalFlow::primeTargets), which only orders what is worked
  // out.
  llvm::DenseMap<const FunctionDecl *, unsigned> Primed;
  TCSummaries<const FunctionDecl *, std::optional<CReturnedRoots>>
      ReturnedRoots;
  TCSummaries<std::pair<const Expr *, unsigned>, CTargets> Designations;
  TCSummaries<const FunctionDecl *, CFunctionSummary> FunctionSummaries;
  // Of each function and parameter given by a reference or a pointer to what
  // isn't const, what it leaves in the members of what that refers to, as
  // CFunctionSummary::Parameters: worked out only where a call of it is
  // asked of a slot what is given there holds (CLocalFlow::summarize).
  TCSummaries<std::pair<const FunctionDecl *, unsigned>, CFunctionSummary>
      ParameterSummaries;
  // Of each function whose body is seen, the indices of the reference and
  // pointer parameters what it is given of which it keeps beyond the call,
  // other than only given back (EEscapes::Parameters), and of a member
  // function whether it keeps its object or a part of it so
  // (EEscapes::Object): worked out of the effects of its body and the
  // summaries of what it calls, no flow, where a call of it is asked of
  // (keptBeyondCall, objectKeptBeyondCall). A recursion grows from none;
  // where it doesn't settle, each is kept.
  TCSummaries<const FunctionDecl *, llvm::SmallVector<unsigned, 2>> KeptBeyond;
  TCSummaries<const FunctionDecl *, bool> ObjectKeptBeyond;
  // Of each function, the keys of the facts and parts of its flow, so that
  // they go with it (release).
  llvm::DenseMap<const FunctionDecl *, llvm::SmallVector<CSlotKey, 4>> FactKeys;
  llvm::DenseMap<const FunctionDecl *, llvm::SmallVector<CSlotKey, 2>> PartKeys;
  /// Drops the flow of \p Function and the facts, parts and targets of its
  /// writes made of it: once the summary of a function whose flow was made
  /// for it is made, the flow isn't kept, and is made again where it is asked
  /// again. Where one of them can't be dropped yet (TCSummaries::droppable),
  /// a recursion still being worked out refers to it, it is dropped once that
  /// is done (CSummaryEngine::whenIdle), unless the flow is asked for before.
  void release(const FunctionDecl *Function);
  // Of each function whose flow is made, when it was last made, by the
  // count of flows made (Generation), and the most writes it was made with,
  // the keys of the targets of its writes.
  struct CMade {
    unsigned At = 0;
    unsigned nWrites = 0;
  };
  llvm::DenseMap<const FunctionDecl *, CMade> Made;
  unsigned Generation = 0;
  // Those to release once the recursion that refers to them is done, unless
  // asked for before (localFlowOf).
  llvm::DenseSet<const FunctionDecl *> Pending;
};

// Whether \p Callee, whose body can be seen and isn't a coroutine's nor a
// constructor's, only calls what its parameter \p iParameter is given,
// `fg_ForEach(Items, [&] { ... })`: no use of the parameter in its body that
// can run escapes (closureUses). A fixed point, of a call of itself too,
// `fp_QuickSort`, which it is taken to only call while it is worked out
// (CFlowSummaries::OnlyCalls).
bool CFlowCache::onlyCallsParameter(const FunctionDecl *Callee,
                                    unsigned iParameter, ASTContext &Context) {
  // Not a constructor, which keeps what it is given in what it makes by its
  // initializers, apart from its body.
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody() ||
      isa<CoroutineBodyStmt>(Definition->getBody()) ||
      isa<CXXConstructorDecl>(Definition) ||
      iParameter >= Definition->getNumParams())
    return false;
  return summaries(Context).OnlyCalls.get({Definition, iParameter});
}

const CClosureUses &CFlowCache::closureUses(const Decl *Owner, const Stmt *Body,
                                            const Stmt *Value,
                                            ASTContext &Context) {
  return summaries(Context).ClosureUses.get({Owner, Body, Value, 0u});
}

const CClosureUses &CFlowCache::closureUses(const Decl *Owner, const Stmt *Body,
                                            const ValueDecl *Named,
                                            ASTContext &Context) {
  return summaries(Context).ClosureUses.get({Owner, Body, Named, 1u});
}

CLocalFlow::CLocalFlow(const FunctionDecl *Function, ASTContext &Context)
    : Function(Function), Context(Context) {}

void CLocalFlow::initialize() {
  CFlowCache &Cache = sharedFlowCache();
  const Stmt *Body = Function->getBody();
  if (!Body)
    return;
  Graph = Cache.graphOf(Function, Body, Context);
  if (!Graph)
    return;
  Reachable = Cache.reachableOf(Function, Body, Context);
  FlowOrder = Cache.orderOf(Function, Body, Context);
  Index = &Cache.indexOf(Body, Context);
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      Method && Method->getParent()->isLambda()) {
    for (const DynTypedNode &Parent : Context.getParents(*Body))
      if (const auto *Lambda = Parent.get<LambdaExpr>())
        Closure = Lambda;
    // Of a specialization of a generic lambda's call operator, the lambda
    // whose class it is a member of; of a coroutine lambda, whose body is its
    // call operator's, not under the lambda expression, too.
    if (!Closure && (Method->getTemplateInstantiationPattern() ||
                     isa<CoroutineBodyStmt>(Body)))
      Closure = lambdaOfCallOperator(Method, Context);
  }
  // Where each statement is, and what each that can run gives slots
  // (forEachRunning).
  const llvm::DenseSet<const Stmt *> *Running =
      Cache.running(Function, Body, Context);
  llvm::SmallVector<const Stmt *, 16> Calls;
  for (const CFGBlock *Block : *Graph) {
    if (!Block || !Reachable.test(Block->getBlockID()))
      continue;
    unsigned Here = 0;
    for (const CFGElement &Element : *Block) {
      const unsigned At = Here++;
      const CFlowEvent Event = eventOf(Element);
      const Stmt *S = Event.Kind == EEvent::Statement ? Event.Statement
                      : Event.Kind == EEvent::Initializer && Event.Initializer
                          ? Event.Initializer->getInit()
                          : nullptr;
      if (!S)
        continue;
      Positions.try_emplace(S, Block->getBlockID(), At);
      if (const Stmt *Original =
              Cache.originalOf(Function, Body, S, Context))
        Positions.try_emplace(Original, Block->getBlockID(), At);
      if (Running && !Running->contains(S))
        continue;
      const CEffects &Effects = effectsOf(S, Context);
      if (isa<CallExpr, CXXConstructExpr, CoroutineSuspendExpr>(S))
        Calls.push_back(S);
      auto Add = [&](CWrite Write) {
        Write.Block = Block->getBlockID();
        Write.Index = At;
        Writes.push_back(Write);
      };
      for (const Decl *Declared : Effects.Declares)
        if (const auto *Variable = dyn_cast<VarDecl>(Declared);
            isSlotVariable(Variable)) {
          Declarations.try_emplace(Variable, Block->getBlockID(), At);
          const MaterializeTemporaryExpr *Extended = extendedSlot(Variable);
          const Expr *Init =
              Extended ? Extended->getSubExpr() : Variable->getInit();
          CWrite Write{CWrite::EKind::Declare, 0, 0, Variable};
          Write.Known = Init != nullptr;
          Write.Value = {Variable->getType().getNonReferenceType()->isArrayType()
                             ? CSlotValue::EKind::Stored
                             : CSlotValue::EKind::Value,
                         Init};
          Add(Write);
        }
      // A suspension, after which other code can have run on the object of
      // the function.
      if (Effects.bSuspends && isSuspension(S)) {
        CWrite Write{CWrite::EKind::Opaque, 0, 0, nullptr, nullptr, {}, false};
        Write.Object = true;
        Add(Write);
      }
      for (const COperandEffect &Effect : Effects.Operands) {
        const auto *Operand = dyn_cast<Expr>(Effect.Operand);
        if (!Operand)
          continue;
        switch (Effect.Use) {
        case EUse::Copy:
        case EUse::Bind:
          if (Effect.Into.Kind == EDestination::Designated && Effect.Into.Object)
            Add({CWrite::EKind::Assign, 0, 0, nullptr, Effect.Into.Object,
                 {CSlotValue::EKind::Value, Operand}});
          // What is given by reference, or by a pointer, to what can
          // change it, also what can't be told, a call through a pointer;
          // not of what only gives it back (givesItsArgument).
          else if (Effect.Into.Kind == EDestination::Parameter) {
            const auto *Call = dyn_cast<CallExpr>(S);
            if (Call && givesItsArgument(Call))
              break;
            // What an exchange gives the first of what it is given, its
            // second (ExchangeFunctions), `fg_Exchange(pValue, nullptr)`.
            if (const Expr *Given = argumentFor(S, 1, Context);
                Given && Effect.Into.Index == 0 && Effect.Into.Callee &&
                Effect.Into.Callee->getIdentifier() &&
                isExchangeFunction(identifierTextOf(Effect.Into.Callee)) &&
                Effect.Use == EUse::Bind && Operand->isGLValue()) {
              Add({CWrite::EKind::Assign, 0, 0, nullptr, Operand,
                   {Given->isGLValue() ? CSlotValue::EKind::Stored
                                       : CSlotValue::EKind::Value,
                    Given}});
              break;
            }
            // What a swap gives each of the two it is given, what the other
            // held (SwapFunctions), `fg_Swap(pKeep, pJob)`.
            if (const Expr *Other = Effect.Into.Index < 2
                                        ? argumentFor(S, 1 - Effect.Into.Index,
                                                      Context)
                                        : nullptr;
                Other && Other->isGLValue() && Effect.Into.Callee &&
                Effect.Into.Callee->getIdentifier() &&
                isSwapFunction(identifierTextOf(Effect.Into.Callee)) &&
                Effect.Use == EUse::Bind && Operand->isGLValue()) {
              Add({CWrite::EKind::Assign, 0, 0, nullptr, Operand,
                   {CSlotValue::EKind::Stored, Other}});
              break;
            }
            // What a function whose body is seen sets a pointer it is given
            // by reference to (setsParameter), `fg_Set(pValue, &Value)` of
            // `o_pValue = _pValue;`, also one given by its address,
            // `fg_Set(&pValue, &Value)` of `*o_ppValue = _pValue;`: what its
            // summary tells, where a slot the call can write is asked of
            // (summaryWrites).
            const auto *Given =
                dyn_cast_or_null<ParmVarDecl>(Effect.Into.Declaration);
            unsigned Offset = 0;
            const FunctionDecl *Sets =
                Call && Given && setsParameter(Given) &&
                        ((Effect.Use == EUse::Bind && Operand->isGLValue() &&
                          Given->getType()->isLValueReferenceType()) ||
                         (Effect.Use == EUse::Copy &&
                          Operand->getType()->isPointerType() &&
                          Given->getType()->isPointerType()))
                    ? setterCalled(Call, Offset)
                    : nullptr;
            // Of any other parameter given by a reference or a pointer to
            // what isn't const, what the body that runs, seen, leaves in
            // the members of what it refers to (summaryWrites), not of a
            // coroutine, which runs on past the call.
            const FunctionDecl *Leaves =
                !Sets && Call && Given &&
                        writesReferent(Given, Effect.Use, Operand)
                    ? calledDefinition(Call)
                    : nullptr;
            if (Leaves && isa<CoroutineBodyStmt>(Leaves->getBody()))
              Leaves = nullptr;
            auto AddCall = [&](CWrite Write) {
              if (Sets) {
                Write.Summarized = Sets;
                Write.Call = S;
                Write.Sets = Effect.Into.Index + 1;
              } else if (Leaves) {
                Write.Summarized = Leaves;
                Write.Call = S;
                Write.Referent = Effect.Into.Index + 1;
              }
              Add(Write);
            };
            const auto *Parameter =
                dyn_cast_or_null<ParmVarDecl>(Effect.Into.Declaration);
            const QualType Type = Parameter ? Parameter->getType() : QualType();
            // What a shared or unique owner points to, `*pJob`, is its own
            // allocation, no slot's.
            const auto *Pointee =
                dyn_cast<CXXOperatorCallExpr>(sameValue(Operand, Context));
            if (Pointee && Pointee->getOperator() == OO_Star &&
                Pointee->getNumArgs() == 1) {
              const EOwnerKind Kind = ownerKindOf(
                  Pointee->getArg(0)->getType().getNonReferenceType(), Context);
              if (Kind == EOwnerKind::Shared || Kind == EOwnerKind::Unique)
                break;
            }
            // Not given to a body that can be seen to only read through it.
            if (Effect.Use == EUse::Bind && Operand->isGLValue() &&
                (Type.isNull() ||
                 (changesThrough(Type) &&
                  changedThroughParameter(S, Parameter, Context)) ||
                 changedThroughConst(S, Parameter, Context)))
              AddCall({CWrite::EKind::Opaque, 0, 0, nullptr, Operand, {}, false});
            else if (Effect.Use == EUse::Copy &&
                     Operand->getType()->isPointerType() &&
                     (Type.isNull() ? !Operand->getType()
                                           ->getPointeeType()
                                           .isConstQualified()
                                    : changesThrough(Type) &&
                                          changedThroughParameter(
                                              S, Parameter, Context)))
              AddCall({CWrite::EKind::Through, 0, 0, nullptr, Operand, {}, false});
          }
          break;
        // Stepped: by what it held, `++pValue`.
        case EUse::Update:
          Add({CWrite::EKind::Assign, 0, 0, nullptr, Operand,
               {CSlotValue::EKind::Stored, Operand}});
          break;
        // What a member function that can change it is called on, not one
        // whose body can be seen only to give access to it
        // (onlyGivesAccess), `Values.f_GetArray()`; by a copy or a move
        // assignment, given what is copied, `Copy = View;`: of one the
        // compiler defines, through the call, which copies as a copy
        // constructor the compiler defines does (copiedByDefinedAssignment).
        case EUse::CallObject:
          if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Effects.Callee);
              Method && !hasConstObject(Method) && !Method->isStatic() &&
              !onlyGivesAccess(Method)) {
            if (const Expr *Copied = copiedFrom(S, Context);
                Copied && Operand->isGLValue()) {
              const auto *Call = dyn_cast<Expr>(S);
              const Expr *Value =
                  Call && copiedByDefinedAssignment(Call, Context) ? Call
                                                                   : Copied;
              Add({CWrite::EKind::Assign, 0, 0, nullptr, Operand,
                   {CSlotValue::EKind::Value, Value}});
            } else if (Operand->isGLValue() ||
                       Operand->getType()->isPointerType()) {
              CWrite Write{Operand->isGLValue() ? CWrite::EKind::Opaque
                                                : CWrite::EKind::Through,
                           0, 0, nullptr, Operand, {}, false};
              const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(S);
              // The member function a call can only run (calledDefinition),
              // whose summary tells what it leaves in the members of its
              // object (summaryWrites), `View.f_Set(&Local)` of `m_pJob =
              // _pJob;`, also through a pointer to it, `pView->f_Set(&Local)`.
              Write.Summarized =
                  MemberCall ? calledDefinition(MemberCall) : nullptr;
              Write.Call = S;
              if (const auto Changed = changedMembers(
                      Method, Context,
                      MemberCall && !callsOverride(MemberCall))) {
                Write.MembersTold = true;
                Write.Members.assign(Changed->begin(), Changed->end());
              }
              Add(Write);
            }
          }
          break;
        case EUse::Forward:
        case EUse::Access:
        case EUse::Write:
        case EUse::Dereference:
        case EUse::AddressOf:
        case EUse::Callee:
        case EUse::Condition:
        case EUse::Value:
        case EUse::Discarded:
        case EUse::Statement:
        case EUse::Unevaluated:
        case EUse::Opaque:
          break;
        }
      }
    }
  }
  // What the closures made here assign that can run, where each call that
  // can run one is (callsOf), only maybe; what each write designates is
  // worked out where it is asked (closureTargets).
  llvm::SmallVector<const LambdaExpr *, 8> Lambdas;
  forEachRunning(Function, Body, Body, Context, [&](const Stmt *S) {
    if (Index->effectsOf(S)->Statement == EStatement::Lambda)
      if (const auto *Lambda = cast<LambdaExpr>(S);
          Lambda->getBody() && Lambda->capture_size() != 0)
        Lambdas.push_back(Lambda);
  });
  // The calls of the closures made here in the bodies of the closures that
  // capture them, of each that calls it itself (closureUses), of one that
  // stays local, nor is replaced in a function object.
  llvm::DenseMap<const Stmt *, llvm::SmallVector<const LambdaExpr *, 1>>
      CapturedCalls;
  for (const LambdaExpr *Lambda : Lambdas) {
    const CClosureUses &Uses = Cache.closureUses(Function, Body, Lambda, Context);
    if (!Uses.bLocal || Uses.bReplaced)
      continue;
    for (const CClosureSite &Site : Uses.Calls)
      if (Site.Kind == EClosureUse::Call && !Site.Through && Site.Body != Body)
        CapturedCalls[Site.Use.Parent].push_back(Lambda);
  }
  // Of each call operator a closure has (callOperatorsOf), what it assigns,
  // also what the closures made in it that it runs (runs) assign, each as
  // maybe, to the closures in them; by the operator of it that runs them, a
  // specialization of a generic lambda. Each once, made before it is added;
  // asked again while it is made, through captures, nothing.
  using CAssigned = llvm::SmallVector<CAssign, 2>;
  using CByOperator =
      llvm::SmallVector<std::pair<const FunctionDecl *, CAssigned>, 1>;
  llvm::DenseMap<const LambdaExpr *, std::unique_ptr<CByOperator>> Summaries;
  llvm::DenseSet<const LambdaExpr *> Summarizing;
  TCRecursive<const CByOperator &(const LambdaExpr *)> SummaryOf =
      [&](const LambdaExpr *Lambda) -> const CByOperator & {
    static const CByOperator None;
    if (const auto It = Summaries.find(Lambda); It != Summaries.end())
      return *It->second;
    if (!Summarizing.insert(Lambda).second)
      return None;
    auto Made = std::make_unique<CByOperator>();
    CByOperator &ByOperator = *Made;
    CAssigned *Collecting = nullptr;
    TCRecursive<void(const LambdaExpr *, int)> Collect =
        [&](const LambdaExpr *Of, int Depth) {
          for (const FunctionDecl *Operator : callOperatorsOf(Of)) {
          if (Depth == 0) {
            ByOperator.push_back({Operator, {}});
            Collecting = &ByOperator.back().second;
          }
          CAssigned &Assigned = *Collecting;
          // The values the closure itself assigns, not one in it, which it
          // may not run, where each is, which assignedOnEachWay tells of.
          llvm::SmallVector<std::pair<const Stmt *, size_t>, 4> Candidates;
          forEachRunning(
              Operator, Operator->getBody(), Operator->getBody(), Context,
              [&](const Stmt *Each) {
                const CEffects &Effects = effectsOf(Each, Context);
                // What a closure made in it, called there, assigns each time
                // it runs (closureWritesAt), of the operator itself, as an
                // assignment of its own there; and so what a closure made
                // here that it captures and calls there does, of one
                // operator.
                if (Depth == 0) {
                  const CLocalFlow &Inner =
                      sharedFlowCache().localFlowOf(Operator, Context);
                  if (const auto It = Inner.ClosureCalls.find(Each);
                      It != Inner.ClosureCalls.end())
                    for (const CAssign &Called : It->second)
                      if (Called.Definite) {
                        Candidates.push_back({Each, Assigned.size()});
                        Assigned.push_back({Called.Target, Called.Value});
                      }
                  if (const auto It = CapturedCalls.find(Each);
                      It != CapturedCalls.end())
                    for (const LambdaExpr *Captured : It->second)
                      if (const CByOperator &Of = SummaryOf(Captured);
                          Of.size() == 1)
                        for (const CAssign &Called : Of.front().second)
                          if (Called.Definite) {
                            Candidates.push_back({Each, Assigned.size()});
                            Assigned.push_back({Called.Target, Called.Value});
                          }
                }
                if (Effects.Statement == EStatement::Lambda && Depth < 8)
                  if (const auto *Inner = cast<LambdaExpr>(Each);
                      Inner->getBody() &&
                      sharedFlowCache()
                          .localFlowOf(Operator, Context)
                          .runs(Inner))
                    Collect(Inner, Depth + 1);
                for (const COperandEffect &Effect : Effects.Operands)
                  if ((Effect.Use == EUse::Copy || Effect.Use == EUse::Bind) &&
                      Effect.Into.Kind == EDestination::Designated &&
                      Effect.Into.Object)
                  {
                    if (Depth == 0)
                      Candidates.push_back({Each, Assigned.size()});
                    Assigned.push_back({Effect.Into.Object,
                                        {CSlotValue::EKind::Value,
                                         dyn_cast<Expr>(Effect.Operand)}});
                  } else if (const auto *Target =
                                 dyn_cast<Expr>(Effect.Operand);
                             Target && Effect.Use == EUse::Update)
                    Assigned.push_back(
                        {Target, {CSlotValue::EKind::Stored, Target}});
                  else if (const auto *Method =
                               dyn_cast_or_null<CXXMethodDecl>(Effects.Callee);
                           Target && Target->isGLValue() &&
                           namesEnclosingOwner(Target, Context) &&
                           Effect.Use == EUse::CallObject && Method &&
                           !hasConstObject(Method) && !Method->isStatic() &&
                           !onlyGivesAccess(Method)) {
                    // By a copy or a move assignment, what is copied, as
                    // the flow of a function has it; else a change.
                    if (const Expr *Copied = copiedFrom(Each, Context)) {
                      const auto *Call = dyn_cast<Expr>(Each);
                      if (Depth == 0)
                        Candidates.push_back({Each, Assigned.size()});
                      Assigned.push_back(
                          {Target,
                           {CSlotValue::EKind::Value,
                            Call && copiedByDefinedAssignment(Call, Context)
                                ? Call
                                : Copied}});
                    } else
                      Assigned.push_back({Target, {}, false, false, true});
                  } else if (const auto *Parameter =
                                 dyn_cast_or_null<ParmVarDecl>(
                                     Effect.Into.Declaration);
                             Target && Target->isGLValue() &&
                             namesEnclosingOwner(Target, Context) &&
                             Effect.Use == EUse::Bind &&
                             Effect.Into.Kind == EDestination::Parameter &&
                             (!Parameter ||
                              changesThrough(Parameter->getType()) ||
                              changedThroughConst(Each, Parameter, Context)))
                    Assigned.push_back({Target, {}, false, false, true});
              });
          const CExitWrites Exit =
              assignedOnEachWay(Operator, Assigned, Candidates);
          for (const size_t iAssigned : Exit.Definite)
            Assigned[iAssigned].Definite = true;
          for (const size_t iAssigned : Exit.Overwritten)
            Assigned[iAssigned].Overwritten = true;
          }
        };
    Collect(Lambda, 0);
    for (auto &[Operator, Each] : ByOperator)
      llvm::erase_if(Each, [](const CAssign &Assign) { return Assign.Overwritten; });
    Summarizing.erase(Lambda);
    return *Summaries.try_emplace(Lambda, std::move(Made)).first->second;
  };
  for (const LambdaExpr *Lambda : Lambdas) {
    const CByOperator &ByOperator = SummaryOf(Lambda);
    const VarDecl *Guard = nullptr;
    bool Escapes = false;
    llvm::SmallVector<std::pair<unsigned, unsigned>, 2> GuardEnds;
    llvm::DenseSet<const Stmt *> Calling;
    const llvm::SmallVector<const Stmt *, 4> CallsOfIt =
        callsOf(Lambda, Calls, Guard, &Escapes, &GuardEnds, &Calling);
    if (Guard || Escapes ||
        llvm::any_of(CallsOfIt, [&](const Stmt *Call) {
          return Positions.count(Call) && Reachable.test(Positions.find(Call)->second.first);
        }))
      RunClosures.insert(Lambda);
    CAssigned Assigned;
    for (const auto &[Operator, Each] : ByOperator)
      llvm::append_range(Assigned, Each);
    if (Assigned.empty())
      continue;
    for (const Stmt *Call : CallsOfIt) {
      const auto It = Positions.find(Call);
      if (It == Positions.end())
        continue;
      // Of a call of it itself (closureUses), also through a function object
      // of SynchronousFunctionTypes or `std::invoke`, what the operator it
      // runs assigns, the one it has or the one called; else, of what it is
      // given to, what any of them does.
      const FunctionDecl *Called = Index->effectsOf(Call)
                                       ? Index->effectsOf(Call)->Callee
                                       : nullptr;
      // What it assigns each time it runs is assigned there, of a closure
      // only called.
      const CAssigned *Runs = &Assigned;
      bool Calls = false;
      for (const auto &[Operator, Each] : ByOperator)
        if (Calling.contains(Call) &&
            (ByOperator.size() == 1 ||
             (Called &&
              Called->getCanonicalDecl() == Operator->getCanonicalDecl()))) {
          Runs = &Each;
          Calls = !Guard && !Escapes;
        }
      for (const CAssign &Each : *Runs) {
        CWrite Write{CWrite::EKind::Closure, It->second.first,
                     It->second.second, nullptr, Each.Target, Each.Value};
        Write.Definite = Calls && Each.Definite;
        Write.Known = !Each.Changed;
        Writes.push_back(Write);
        if (!Each.Changed)
          ClosureCalls[Call].push_back(
              {Each.Target, Each.Value, Write.Definite});
      }
    }
    // Where the object it is given to ends, which runs it, a scope exit; of
    // a guard nothing names, which can't be told not to run or run early,
    // and a closure that doesn't escape too, what it assigns on each way
    // through it there too (assignedOnEachWay).
    if (Guard) {
      const bool Runs =
          !Escapes && ByOperator.size() == 1 &&
          llvm::none_of(Index->statements(), [&](const Stmt *S) {
            return Index->effectsOf(S)->Statement == EStatement::Name &&
                   cast<DeclRefExpr>(S)->getDecl() == Guard;
          });
      // Where it ends uncancelled (callsOf), or, of a closure that escapes,
      // wherever it ends.
      if (Escapes)
        GuardEnds = endsOf(Guard);
      for (const auto &[Block, Index] : GuardEnds) {
        EndsRunning.insert({Block, Index});
        for (const CAssign &Each : Assigned) {
          CWrite Write{CWrite::EKind::Closure, Block, Index, nullptr,
                       Each.Target, Each.Value};
          Write.Definite = Runs && Each.Definite;
          Write.Known = !Each.Changed;
          Writes.push_back(Write);
          if (!Each.Changed)
            ClosureEnds[Guard].push_back(
                {Each.Target, Each.Value, Write.Definite});
        }
      }
    }
  }
  llvm::stable_sort(Writes, [](const CWrite &Left, const CWrite &Right) {
    return std::make_pair(Left.Block, Left.Index) <
           std::make_pair(Right.Block, Right.Index);
  });
  findEscapes();
  Ready = true;
}

// The calls of \p Calls that can run the closure \p Lambda: of one only
// called, also through the local it is copied or bound to, those calls; of
// one given to what makes a local object, \p Guard, `auto Cleanup =
// g_OnScopeExit / [&] { ... };`, each that can run after it is made while
// that is alive, and where it ends (endsOf); else each that can run after it
// is made, which the closure can be given to. In \p Calling, those that
// call it itself (closureUses).
llvm::SmallVector<const Stmt *, 4>
CLocalFlow::callsOf(
    const LambdaExpr *Lambda, llvm::ArrayRef<const Stmt *> Calls,
    const VarDecl *&Guard, bool *Escapes,
    llvm::SmallVectorImpl<std::pair<unsigned, unsigned>> *Ends,
    llvm::DenseSet<const Stmt *> *Calling) const {
  llvm::SmallVector<const Stmt *, 4> Direct;
  bool Anywhere = false;
  Guard = nullptr;
  // What the closure is given to to make the guard, which keeps it, not runs
  // it.
  const Stmt *Making = nullptr;
  // A local object what is given \p Value as an argument makes.
  auto GuardOf = [&](const CEffectIndex::CUse &Use) -> const VarDecl * {
    if (!Use.Effect || Use.Effect->Into.Kind != EDestination::Parameter ||
        !Use.Parent)
      return nullptr;
    for (const CEffectIndex::CUse &Made : Index->consumersOf(Use.Parent))
      if ((Made.Effect->Use == EUse::Copy || Made.Effect->Use == EUse::Bind) &&
          Made.Effect->Into.Kind == EDestination::Variable)
        if (const auto *Local =
                dyn_cast_or_null<VarDecl>(Made.Effect->Into.Declaration);
            Local && Local->hasLocalStorage() &&
            Local->getType().getNonReferenceType()->isRecordType() &&
            runsWhereItEnds(
                Local->getType().getNonReferenceType()->getAsCXXRecordDecl(),
                Context))
          return Local;
    return nullptr;
  };
  // By what each use of it is (closureUses): its calls and what it is given
  // to that only calls it, through its aliases and the closures that
  // capture it, here; a guard made of it, before what only calls it, which
  // a guard's constructor can be taken as; a copy or a move of it, by where
  // that goes, a callable alias of it; any other, anywhere. Each value once.
  llvm::DenseSet<const Stmt *> Followed;
  TCRecursive<void(const Stmt *, int)> Follow = [&](const Stmt *Value,
                                                      int Depth) {
    if (!Followed.insert(Value).second)
      return;
    const Stmt *Body = Function->getBody();
    const CClosureUses &Uses =
        sharedFlowCache().closureUses(Function, Body, Value, Context);
    auto Guarded = [&](const CClosureSite &Site) {
      const VarDecl *Held = Site.Body == Body ? GuardOf(Site.Use) : nullptr;
      if (!Held || Guard)
        return false;
      Guard = Held;
      Making = Site.Use.Parent;
      return true;
    };
    for (const CClosureSite &Site : Uses.Calls)
      if (Site.Body == Body &&
          !(Site.Kind == EClosureUse::Pass && Guarded(Site))) {
        Direct.push_back(Site.Use.Parent);
        // What calls it itself, also through an alias of it, not a closure
        // that captures it nor a function object given another callable.
        if (Calling && Site.Kind == EClosureUse::Call && !Site.Through &&
            !Uses.bReplaced)
          Calling->insert(Site.Use.Parent);
      }
    for (const CClosureSite &Site : Uses.Escapes) {
      // An escape without a use (the root outside a body) is given to no
      // copy.
      const CDestination *Into =
          Site.Use.Effect ? &Site.Use.Effect->Into : nullptr;
      const auto *Copier =
          Into && Into->Kind == EDestination::Parameter
              ? dyn_cast_or_null<CXXConstructorDecl>(Into->Callee)
              : nullptr;
      if (Site.Body == Body && Copier && Copier->isCopyOrMoveConstructor() &&
          Site.Use.Parent && Depth < 8)
        Follow(Site.Use.Parent, Depth + 1);
      else if (!Guarded(Site))
        Anywhere = true;
    }
  };
  Follow(Lambda, 0);
  // A guard that is shared (OwnerTypes), or whose own value can leave it,
  // `m_Cleanup = fg_Move(Cleanup)`, can end anywhere: a name of it copied
  // from, its address taken, or bound to what can change it, a reference that
  // isn't to const.
  auto ValueLeaves = [&](const VarDecl *Local) {
    for (const DeclRefExpr *Name : sharedFlowCache().namesIn(
             Function, Function->getBody(), Local, Context))
      for (const CEffectIndex::CUse &Use : Index->consumersOf(Name))
        switch (Use.Effect->Use) {
        case EUse::Copy:
        case EUse::AddressOf:
        case EUse::Opaque:
          return true;
        case EUse::Bind: {
          const CDestination &Into = Use.Effect->Into;
          QualType Bound;
          if (Into.Kind == EDestination::Parameter && Into.Callee &&
              Into.Index < Into.Callee->getNumParams())
            Bound = Into.Callee->getParamDecl(Into.Index)->getType();
          else if (const auto *Variable =
                       dyn_cast_or_null<VarDecl>(Into.Declaration))
            Bound = Variable->getType();
          if (Bound.isNull() || !Bound->isReferenceType() ||
              !Bound.getNonReferenceType().isConstQualified())
            return true;
          break;
        }
        case EUse::Forward:
        case EUse::Access:
        case EUse::Update:
        case EUse::Write:
        case EUse::Dereference:
        case EUse::CallObject:
        case EUse::Callee:
        case EUse::Condition:
        case EUse::Value:
        case EUse::Discarded:
        case EUse::Statement:
        case EUse::Unevaluated:
          break;
        }
    return false;
  };
  if (Guard && (ownerKindOf(Guard->getType(), Context) == EOwnerKind::Shared ||
                ValueLeaves(Guard)))
    Anywhere = true;
  if (Escapes)
    *Escapes = Anywhere;
  if (!Anywhere && !Guard)
    return Direct;
  // Each call after it is made: later in its block, and in each block that
  // can be reached from it, its own again in a loop.
  const auto Made = Positions.find(Lambda);
  if (Made == Positions.end())
    return {};
  // Not past where the guard ends.
  llvm::DenseMap<std::pair<unsigned, unsigned>, const Stmt *> CallAt;
  for (const Stmt *Call : Calls)
    if (const auto It = Positions.find(Call); It != Positions.end())
      CallAt[It->second] = Call;
  llvm::SmallVector<const Stmt *, 4> Later = Direct;
  llvm::BitVector Seen(Graph->getNumBlockIDs());
  llvm::SmallVector<std::pair<const CFGBlock *, unsigned>, 8> Work;
  for (const CFGBlock *Block : *Graph)
    if (Block && Block->getBlockID() == Made->second.first)
      Work.push_back({Block, Made->second.second + 1});
  while (!Work.empty()) {
    auto [Block, From] = Work.pop_back_val();
    bool Ended = false;
    for (unsigned At = From; At < Block->size() && !Ended; ++At) {
      const CFlowEvent Event = eventOf((*Block)[At]);
      Ended = Guard && !Anywhere && Event.Variable == Guard &&
              (Event.Kind == EEvent::LifetimeEnds ||
               Event.Kind == EEvent::AutomaticDestructor);
      if (Ended && Ends)
        Ends->push_back({Block->getBlockID(), At});
      // Not after the guard is cancelled (cancelsGuard), which then doesn't
      // run it, nor where it ends.
      if (Guard && !Anywhere && Event.Kind == EEvent::Statement &&
          cancelsGuard(Event.Statement, Guard)) {
        Ended = true;
        break;
      }
      if (const auto It = CallAt.find({Block->getBlockID(), At});
          !Ended && It != CallAt.end() && It->second != Making &&
          !llvm::is_contained(Later, It->second))
        Later.push_back(It->second);
    }
    if (Ended)
      continue;
    for (const CFGBlock::AdjacentBlock &Successor : Block->succs())
      if (const CFGBlock *Next = Successor.getReachableBlock();
          Next && !Seen.test(Next->getBlockID())) {
        Seen.set(Next->getBlockID());
        Work.push_back({Next, 0});
      }
  }
  return Later;
}

// The slots of the function \p Target, an assigned location in the body of
// a closure made in it, designates (designatedSlots), also through the
// references and the pointers the closure refers to, not the copies it keeps
// itself (slotFunction), nor its own locals; \p Told is cleared when it can
// be what isn't one of them.
llvm::SmallVector<CSlot, 2> CLocalFlow::closureTargets(const Expr *Target,
                                                       bool &Told) const {
  // Also what a parameter of the function refers to or points to, and of a
  // function that isn't a closure's, its object.
  llvm::SmallVector<CSlot, 2> Slots = designatedPlaces(Target, Context, &Told);
  llvm::erase_if(Slots, [&](const CSlot &Slot) {
    switch (Slot.Place) {
    case EPlace::Variable:
      return slotFunction(Slot.Variable, Target) != Function;
    case EPlace::Referent:
      return Slot.Variable->getDeclContext() != Function;
    case EPlace::This:
      return Closure != nullptr;
    }
    return true;
  });
  return Slots;
}

// Where the local \p Variable ends, its destructor and the end of its life,
// in the blocks that can be reached.
llvm::SmallVector<std::pair<unsigned, unsigned>, 2>
CLocalFlow::endsOf(const VarDecl *Variable) const {
  llvm::SmallVector<std::pair<unsigned, unsigned>, 2> Ends;
  for (const CFGBlock *Block : *Graph) {
    if (!Block || !Reachable.test(Block->getBlockID()))
      continue;
    for (unsigned At = 0; At < Block->size(); ++At) {
      const CFlowEvent Event = eventOf((*Block)[At]);
      if (Event.Variable == Variable &&
          (Event.Kind == EEvent::LifetimeEnds ||
           Event.Kind == EEvent::AutomaticDestructor))
        Ends.push_back({Block->getBlockID(), At});
    }
  }
  return Ends;
}

llvm::SmallVector<CClosureWrite, 2>
CLocalFlow::closureWritesAtEnd(const VarDecl *Guard) const {
  llvm::SmallVector<CClosureWrite, 2> Written;
  const auto It = ClosureEnds.find(Guard);
  if (It == ClosureEnds.end())
    return Written;
  for (const CAssign &Each : It->second) {
    bool Told = true;
    const llvm::SmallVector<CSlot, 2> Slots = closureTargets(Each.Target, Told);
    for (const CSlot &Slot : Slots)
      Written.push_back({Slot, Each.Value,
                         Each.Definite && Told && Slots.size() == 1 &&
                             !Slot.Element});
  }
  return Written;
}

llvm::SmallVector<CClosureWrite, 2>
CLocalFlow::closureWritesAt(const Stmt *Call) const {
  llvm::SmallVector<CClosureWrite, 2> Written;
  const auto It = ClosureCalls.find(Call);
  if (It == ClosureCalls.end())
    return Written;
  for (const CAssign &Each : It->second) {
    bool Told = true;
    const llvm::SmallVector<CSlot, 2> Slots = closureTargets(Each.Target, Told);
    for (const CSlot &Slot : Slots)
      Written.push_back({Slot, Each.Value,
                         Each.Definite && Told && Slots.size() == 1 &&
                             !Slot.Element});
  }
  return Written;
}

// Of \p Candidates, the assignments of \p Assigned by the statements of the
// body of the closure's \p Operator to whole slots (namedSlots), what the
// slots hold where it returns (CExitWrites), also by the edges to handlers:
// a must-assigned analysis over its CFG, the slots assigned on every way
// into a block, then by its elements, of each of which the first assignment
// that reaches the exit replaces what the slot held and the others add to
// it, what is assigned on each branch of an `if` too; and a may analysis of
// the assignments that reach the exit, without what each way assigns again.
CLocalFlow::CExitWrites CLocalFlow::assignedOnEachWay(
    const FunctionDecl *Operator, llvm::ArrayRef<CAssign> Assigned,
    llvm::ArrayRef<std::pair<const Stmt *, size_t>> Candidates) {
  CExitWrites Found;
  CFlowCache &Cache = sharedFlowCache();
  const CFG *Of = Operator->getBody() && !Candidates.empty()
                      ? Cache.graphOf(Operator, Operator->getBody(), Context)
                      : nullptr;
  if (!Of)
    return Found;
  // The whole slot each candidate assigns, by index.
  llvm::SmallVector<CKey, 2> Keys;
  llvm::DenseMap<const Stmt *, llvm::SmallVector<unsigned, 1>> AssignsAt;
  llvm::SmallVector<std::optional<unsigned>, 4> KeyOf;
  // Also through what a pointer holds, one told local address, `*ppData`
  // of `[ppData = &pData]` (designatedSlots).
  for (const auto &[At, iAssigned] : Candidates) {
    bool Told = true;
    const llvm::SmallVector<CSlot, 2> Slots =
        designatedSlots(Assigned[iAssigned].Target, Context, &Told);
    KeyOf.push_back(std::nullopt);
    if (!Told || Slots.size() != 1 || Slots.front().Element)
      continue;
    const CKey Key(Slots.front().Variable,
                   CPath(Slots.front().Path.begin(), Slots.front().Path.end()));
    auto It = llvm::find(Keys, Key);
    const unsigned iKey = It - Keys.begin();
    if (It == Keys.end())
      Keys.push_back(Key);
    KeyOf.back() = iKey;
    AssignsAt[At].push_back(iKey);
  }
  if (Keys.empty())
    return Found;
  const unsigned nKeys = Keys.size();
  const unsigned nBlocks = Of->getNumBlockIDs();
  // What each block is entered with: each slot, until a way says otherwise.
  std::vector<llvm::BitVector> In(nBlocks, llvm::BitVector(nKeys, true));
  In[Of->getEntry().getBlockID()].reset();
  llvm::BitVector Reached(nBlocks);
  Reached.set(Of->getEntry().getBlockID());
  auto Through = [&](const CFGBlock *Block) {
    llvm::BitVector Out = In[Block->getBlockID()];
    for (const CFGElement &Element : *Block)
      if (const CFlowEvent Event = eventOf(Element);
          Event.Kind == EEvent::Statement && Event.Statement)
        if (const auto It = AssignsAt.find(Event.Statement);
            It != AssignsAt.end())
          for (const unsigned iKey : It->second)
            Out.set(iKey);
    return Out;
  };
  for (bool Changed = true; Changed;) {
    Changed = false;
    for (const CFGBlock *Block : *Of) {
      if (!Block || !Reached.test(Block->getBlockID()))
        continue;
      const llvm::BitVector Out = Through(Block);
      for (const CFGBlock::AdjacentBlock &Successor : Block->succs())
        if (const CFGBlock *To = Successor.getReachableBlock()) {
          // A block reached the first time is a change, whatever it is
          // entered with, so that what is after it is worked out too.
          llvm::BitVector &Into = In[To->getBlockID()];
          if (!Reached.test(To->getBlockID())) {
            Reached.set(To->getBlockID());
            Into = Out;
            Changed = true;
            continue;
          }
          const llvm::BitVector Before = Into;
          Into &= Out;
          Changed |= Into != Before;
        }
    }
  }
  if (!Reached.test(Of->getExit().getBlockID()))
    return Found;
  const llvm::BitVector &AtExit = In[Of->getExit().getBlockID()];
  // The assignments that reach the exit, a may analysis: one kills the
  // others of its slot.
  const unsigned nCandidates = Candidates.size();
  llvm::DenseMap<const Stmt *, llvm::SmallVector<unsigned, 1>> CandidatesAt;
  for (unsigned iCandidate = 0; iCandidate < nCandidates; ++iCandidate)
    if (KeyOf[iCandidate])
      CandidatesAt[Candidates[iCandidate].first].push_back(iCandidate);
  std::vector<llvm::BitVector> Reaching(nBlocks, llvm::BitVector(nCandidates));
  for (bool Changed = true; Changed;) {
    Changed = false;
    for (const CFGBlock *Block : *Of) {
      if (!Block || !Reached.test(Block->getBlockID()))
        continue;
      llvm::BitVector Out = Reaching[Block->getBlockID()];
      for (const CFGElement &Element : *Block)
        if (const CFlowEvent Event = eventOf(Element);
            Event.Kind == EEvent::Statement && Event.Statement)
          if (const auto It = CandidatesAt.find(Event.Statement);
              It != CandidatesAt.end())
            for (const unsigned iCandidate : It->second) {
              for (unsigned iOther = 0; iOther < nCandidates; ++iOther)
                if (KeyOf[iOther] == KeyOf[iCandidate])
                  Out.reset(iOther);
              Out.set(iCandidate);
            }
      for (const CFGBlock::AdjacentBlock &Successor : Block->succs())
        if (const CFGBlock *To = Successor.getReachableBlock()) {
          llvm::BitVector &Into = Reaching[To->getBlockID()];
          const llvm::BitVector Before = Into;
          Into |= Out;
          Changed |= Into != Before;
        }
    }
  }
  const llvm::BitVector &ReachExit = Reaching[Of->getExit().getBlockID()];
  llvm::BitVector First(nKeys);
  for (unsigned iCandidate = 0; iCandidate < nCandidates; ++iCandidate) {
    const std::optional<unsigned> iKey = KeyOf[iCandidate];
    if (!iKey)
      continue;
    if (!ReachExit.test(iCandidate))
      Found.Overwritten.push_back(Candidates[iCandidate].second);
    else if (AtExit.test(*iKey) && !First.test(*iKey)) {
      First.set(*iKey);
      Found.Definite.push_back(Candidates[iCandidate].second);
    }
  }
  return Found;
}

/// Whether \p Operand is storage of only values (a number, an enumerator, a
/// record or an array of them) in what a shared or unique owner (OwnerTypes) owns,
/// `pJob->m_Result` or `(*pJob).m_Result`: what is done with its address or a
/// reference to it changes no owner and no address, so the owner's slot
/// doesn't escape by it.
static bool ownedValuesOf(const Expr *Operand, ASTContext &Context) {
  auto OnlyValues = [&] {
    const QualType Type = Operand->getType().getNonReferenceType();
    return !Type.isNull() &&
           (Type->isArithmeticType() || Type->isEnumeralType() ||
            ((Type->isRecordType() || Type->isArrayType()) &&
             holdsOnlyValues(Type, Context)));
  };
  auto IsOwnerAccess = [&](const Expr *E) {
    const auto *Call = dyn_cast<CXXOperatorCallExpr>(E->IgnoreParenImpCasts());
    if (!Call || Call->getNumArgs() != 1 ||
        (Call->getOperator() != OO_Arrow && Call->getOperator() != OO_Star))
      return false;
    const EOwnerKind Kind = ownerKindOf(
        Call->getArg(0)->getType().getNonReferenceType(), Context);
    return Kind == EOwnerKind::Shared || Kind == EOwnerKind::Unique;
  };
  const Expr *E = Operand;
  for (int Step = 0; E && Step < 16; ++Step) {
    E = E->IgnoreParenImpCasts();
    if (IsOwnerAccess(E))
      return OnlyValues();
    const auto *Member = dyn_cast<MemberExpr>(E);
    if (!Member)
      return false;
    if (Member->isArrow())
      return IsOwnerAccess(Member->getBase()) && OnlyValues();
    E = Member->getBase();
  }
  return false;
}

/// Whether each body \p Call can run is seen and gives back only a member,
/// or an element of a member, of what it is given (returnedRoots): of its
/// object where \p Parameter is none, `return m_Elements[_Index];`, else of
/// what that parameter refers to, `return _Tuple.m_First;`. What it gives is
/// that part of what it is given, which the way through its bodies
/// designates.
static bool givesPartOf(const CallExpr *Call, std::optional<unsigned> Parameter,
                        ASTContext &Context) {
  const CCalledRoots Called =
      CProvenance(Context).calledRoots(Call, Call->getDirectCallee());
  if (Called.bUnseen || Called.Bodies.empty())
    return false;
  for (const auto &Body : Called.Bodies) {
    if (Body.second->Other || Body.second->Roots.empty())
      return false;
    for (const CReturnedRoot &Root : Body.second->Roots)
      if (Root.Kind != (Parameter ? CReturnedRoot::EKind::Parameter
                                  : CReturnedRoot::EKind::Object) ||
          (Parameter && Root.Index != *Parameter) || !Root.Bound.empty() ||
          Root.Projections.empty() || Root.Projections.front().Path.empty())
        return false;
  }
  return true;
}

// Whether the call \p Parent keeps where code that can't be seen can reach
// it what it gives its parameter \p Into, the location \p Operand designates
// or points to: of a body that can be seen, where what the parameter refers
// to or points to is kept beyond it, not only given back, by the summary of
// what it keeps (CFlowSummaries::KeptBeyond), which builds no flow, nor of
// what only gives back what it is given (givesItsArgument); of one that
// can't be seen, or of an override that can run instead, only where it
// states that it keeps it beyond the call (DMibKeepsArgument), as it keeps
// nothing elsewhere (Rule R). Not of a constructor, whose object, of the
// frame of the caller or not, is what keeps it.
static bool keptBeyondCall(const Stmt *Parent, const CDestination &Into,
                           ASTContext &Context) {
  const auto *Call = dyn_cast_or_null<CallExpr>(Parent);
  if (Into.Kind != EDestination::Parameter || !Into.Callee || !Call ||
      givesItsArgument(Call))
    return false;
  const FunctionDecl *Definition = Into.Callee->getDefinition();
  const auto *Method = dyn_cast<CXXMethodDecl>(Into.Callee);
  if (!Definition || !Definition->getBody() ||
      (Method && mayRunOverride(Call, Method)))
    return keepsArgument(Into.Callee, Into.Index);
  if (Into.Index >= Definition->getNumParams() ||
      isa<CoroutineBodyStmt>(Definition->getBody()))
    return false;
  return llvm::is_contained(
      sharedFlowCache().summaries(Context).KeptBeyond.get(Definition),
      Into.Index);
}

// Whether the call \p Parent keeps its object, what a member function is
// called on, where code that can't be seen can reach it: of a body that can
// be seen, where it keeps its object beyond it, not only given back, by the
// summary of what it keeps (CFlowSummaries::KeptBeyond), as keptBeyondCall
// tells of a parameter; not of one that can't be seen, nor of an override
// that can run instead, which keep nothing elsewhere (Rule R), nor of a
// coroutine; nor of \p From itself called on its own object in its body,
// which keeps it only where the rest of the body does.
static bool objectKeptBeyondCall(const Stmt *Parent, const FunctionDecl *From,
                                 ASTContext &Context) {
  const auto *Call = dyn_cast_or_null<CallExpr>(Parent);
  const CMemberCall Member =
      Call ? memberCallOf(Call, /*Operators=*/true) : CMemberCall();
  const CXXMethodDecl *Method = Member ? Member->getMethodDecl() : nullptr;
  if (!Method || !Method->isImplicitObjectMemberFunction() ||
      mayRunOverride(Call, Method))
    return false;
  const FunctionDecl *Definition = Method->getDefinition();
  if (!Definition || !Definition->getBody() ||
      isa<CoroutineBodyStmt>(Definition->getBody()) ||
      (From && Definition == From->getDefinition()))
    return false;
  return sharedFlowCache().summaries(Context).ObjectKeptBeyond.get(Definition);
}

namespace {
/// What a function does with the addresses of its locals and what its
/// reference and pointer parameters refer to or point to (escapesOf): those
/// used where its flow can't follow them, and of those, those kept beyond
/// it, other than only given back, which its caller follows.
struct CEscapes {
  llvm::DenseSet<const VarDecl *> Escaped;
  llvm::DenseSet<const VarDecl *> EscapedReferents;
  llvm::DenseSet<const VarDecl *> Kept;
  llvm::DenseSet<const VarDecl *> KeptReferents;
  /// Of a member function, whether its object, or a part of it, is kept
  /// beyond it, as KeptReferents tells of a parameter.
  bool KeptObject = false;
};

/// What escapesOf works out.
enum class EEscapes {
  /// Of the locals and what the parameters refer to or point to, with what
  /// designates told through what pointers hold: a flow's own (findEscapes).
  Flow,
  /// Of the locals and what the parameters refer to or point to, without:
  /// a summary's (CFlowSummaries::KeptBeyond).
  Parameters,
  /// Of the object, without (CFlowSummaries::ObjectKeptBeyond).
  Object,
};
} // namespace

// What \p Function, whose effects \p BodyIndex has, does with the addresses
// of its locals, of what its reference and pointer parameters refer to or
// point to, and of its object (CEscapes), by the effects of its body and of
// the closures it makes, and what it calls keeps (keptBeyondCall,
// objectKeptBeyondCall), as far as \p Asked asks (EEscapes).
static CEscapes escapesOf(const FunctionDecl *Function,
                          const CEffectIndex &BodyIndex, EEscapes Asked,
                          ASTContext &Context) {
  // What a location designates is told through what pointers hold, which
  // its own flow tells (designatedPlaces), only of the flow's own; else
  // through no flow (EDesignation::NamedPlaces).
  const bool ThroughValues = Asked == EEscapes::Flow;
  const bool AskLocals = Asked != EEscapes::Object;
  const bool AskObject = Asked == EEscapes::Object;
  CEscapes Out;
  const CEffectIndex *Index = &BodyIndex;
  llvm::DenseSet<const VarDecl *> &Escaped = Out.Escaped;
  llvm::DenseSet<const VarDecl *> &EscapedReferents = Out.EscapedReferents;
  llvm::DenseSet<const VarDecl *> &Kept = Out.Kept;
  llvm::DenseSet<const VarDecl *> &KeptReferents = Out.KeptReferents;
  bool &KeptObject = Out.KeptObject;
  // Of each local, where its location is named, also through a local
  // reference bound to it, and where its address is: what is done with
  // each, not followed where it is kept in a local pointer, a reference, or
  // given to what only changes it (CWrite::EKind::Through), else it is used
  // where the flow can't follow it. In the function, or in the body of a
  // closure made in it, which follows what is done there the same way.
  struct CScope {
    const Decl *Owner;
    const Stmt *Body;
    const CEffectIndex *Index;
  };
  llvm::SmallVector<CScope, 2> Scopes{{Function, Function->getBody(), Index}};
  llvm::DenseMap<const VarDecl *, llvm::SmallVector<const VarDecl *, 1>>
      Holders;
  // The scope each holder is a local of, where it is read.
  llvm::DenseMap<const VarDecl *, unsigned> HolderScopes;
  struct CWork {
    const Expr *Address;
    const VarDecl *Local;
    unsigned Scope;
  };
  llvm::SmallVector<CWork, 16> Work;
  // That \p Local is used where the flow can't follow it, and, unless only
  // given back, \p Returned, kept there beyond the function (Kept).
  auto Escape = [&](const VarDecl *Local, bool Returned) {
    Escaped.insert(Local);
    if (!Returned)
      Kept.insert(Local);
  };
  // Also a reference parameter of the function, by what it refers to,
  // which is a place of the flow (EPlace::Referent), its own storage, not
  // what it points to or owns: of a function that has one.
  const bool HasReferences =
      llvm::any_of(Function->parameters(), [](const ParmVarDecl *Parameter) {
        return Parameter->getType()->isReferenceType();
      });
  // What \p E designates of the places of the flow, with \p Values also
  // through what its pointers hold (designatedPlaces), else not
  // (EDesignation::NamedPlaces).
  auto PlacesThrough = [&](const Expr *E, bool *Told, bool Values) {
    bool Each = true;
    llvm::SmallVector<CSlot, 2> Found =
        Values ? designatedPlaces(E, Context, &Each)
               : designation(E, EDesignation::NamedPlaces, Context, Each);
    if (Told)
      *Told = Each;
    return Found;
  };
  auto PlacesOf = [&](const Expr *E, bool *Told) {
    return PlacesThrough(E, Told, ThroughValues);
  };
  // The locals \p E designates, and what the reference parameters it
  // designates refer to, with \p Values also through what pointers hold.
  auto LocalsThrough = [&](const Expr *E, bool Values) {
    llvm::SmallVector<const VarDecl *, 2> Found;
    for (const CSlot &Slot : namedSlots(E, Context, nullptr))
      if (Slot.Variable && !llvm::is_contained(Found, Slot.Variable))
        Found.push_back(Slot.Variable);
    if (!HasReferences)
      return Found;
    for (const CSlot &Slot : PlacesThrough(E, nullptr, Values))
      if (Slot.Place == EPlace::Referent && !Slot.Element &&
          !crossesPointer(Slot.Path, 0) &&
          Slot.Variable->getType()->isReferenceType() &&
          Slot.Variable->getDeclContext() == Function &&
          !llvm::is_contained(Found, Slot.Variable))
        Found.push_back(Slot.Variable);
    return Found;
  };
  auto Locals = [&](const Expr *E) { return LocalsThrough(E, ThroughValues); };
  // The operand of \p Effect, of \p S, when it binds a reference to it where
  // the flow can't follow it, else null: not a local reference, an
  // assignment, a parameter of what gives back no reference or pointer, what
  // `co_return` gives, nor, \p Captures, a capture.
  // Whether what \p S gives back is dropped where \p In has it: each use of
  // it a statement that runs or a discarded value.
  auto DroppedResult = [](const Stmt *S, const CEffectIndex &In) {
    const llvm::SmallVector<CEffectIndex::CUse, 2> Uses = In.consumersOf(S);
    return !Uses.empty() &&
           llvm::all_of(Uses, [](const CEffectIndex::CUse &Use) {
             return Use.Effect && (Use.Effect->Use == EUse::Discarded ||
                                   Use.Effect->Use == EUse::Statement);
           });
  };
  // Whether what \p S gives back is only copied where \p In has it, each
  // use of it a copy, given to a copy or move constructor, or a discarded
  // value.
  auto CopiedOnly = [](const Stmt *S, const CEffectIndex &In) {
    const llvm::SmallVector<CEffectIndex::CUse, 2> Uses = In.consumersOf(S);
    return !Uses.empty() &&
           llvm::all_of(Uses, [](const CEffectIndex::CUse &Use) {
             if (!Use.Effect)
               return false;
             if (const auto *Construct =
                     dyn_cast_or_null<CXXConstructExpr>(Use.Parent);
                 Construct && Use.Effect->Use == EUse::Bind &&
                 Construct->getConstructor()->isCopyOrMoveConstructor())
               return true;
             return Use.Effect->Use == EUse::Copy ||
                    Use.Effect->Use == EUse::Discarded ||
                    Use.Effect->Use == EUse::Statement;
           });
  };
  auto BindsAway = [&](const Stmt *S, const COperandEffect &Effect,
                       bool Captures, const CEffectIndex &In) -> const Expr * {
    const auto *Operand = dyn_cast<Expr>(Effect.Operand);
    if (!Operand || Effect.Use != EUse::Bind || !Operand->isGLValue())
      return nullptr;
    const auto *Bound =
        Effect.Into.Kind == EDestination::Variable
            ? dyn_cast_or_null<VarDecl>(Effect.Into.Declaration)
            : nullptr;
    const bool Local = Bound && Bound->hasLocalStorage();
    // Not what an exchange gives back, what the slot held, not a reference
    // to it (ExchangeFunctions), nor a copy assignment the compiler defines,
    // which gives back its object (copiedFrom), nor of a call whose result is
    // dropped.
    const auto *Call = dyn_cast<CallExpr>(S);
    const bool Returned =
        Effect.Into.Kind == EDestination::Parameter && Effect.Into.Callee &&
        (Effect.Into.Callee->getReturnType()->isReferenceType() ||
         Effect.Into.Callee->getReturnType()->isPointerType()) &&
        !(Call && givesItsArgument(Call)) && !copiedFrom(S, Context) &&
        !(Effect.Into.Callee->getIdentifier() &&
          isExchangeFunction(identifierTextOf(Effect.Into.Callee))) &&
        !DroppedResult(S, In);
    // Nor what `co_return` gives, past which the function doesn't run.
    const bool Result = Effect.Into.Kind == EDestination::Return &&
                        effectsOf(S, Context).Statement == EStatement::Coreturn;
    return !Local && !Result &&
                   !(Effect.Into.Kind == EDestination::Parameter &&
                     !Returned) &&
                   Effect.Into.Kind != EDestination::Designated &&
                   !(Captures && Effect.Into.Kind == EDestination::Capture) &&
                   !ownedValuesOf(Operand, Context)
               ? Operand
               : nullptr;
  };
  // What \p Effect, of \p S, binds a reference parameter to, where the call
  // keeps it beyond the call (keptBeyondCall): each local it designates is
  // used where the flow can't follow it.
  auto KeptAway = [&](const Stmt *S, const COperandEffect &Effect) {
    const auto *Operand = dyn_cast<Expr>(Effect.Operand);
    if (!Operand || Effect.Use != EUse::Bind || !Operand->isGLValue() ||
        Effect.Into.Kind != EDestination::Parameter)
      return;
    const llvm::SmallVector<const VarDecl *, 2> Bound = Locals(Operand);
    if (Bound.empty() || ownedValuesOf(Operand, Context) ||
        !keptBeyondCall(S, Effect.Into, Context))
      return;
    for (const VarDecl *Each : Bound)
      Escape(Each, false);
  };
  // The addresses \p S, in the scope \p Scope, takes, `&Value`, also by a
  // call, `std::addressof(Value)`.
  auto Addresses = [&](const Stmt *S, const CEffects &Effects, unsigned Scope) {
    if (!isa<Expr>(S))
      return;
    for (const COperandEffect &Effect : Effects.Operands)
      if (const auto *Operand = dyn_cast<Expr>(Effect.Operand);
          Operand && Effect.Use == EUse::AddressOf &&
          !ownedValuesOf(Operand, Context))
        for (const VarDecl *Local : Locals(Operand))
          Work.push_back({cast<Expr>(S), Local, Scope});
    if (const auto *Call = dyn_cast<CallExpr>(S);
        Call && givesItsArgument(Call) && Call->getNumArgs() == 1 &&
        Call->getType()->isPointerType())
      for (const VarDecl *Local : Locals(Call->getArg(0)))
        Work.push_back({Call, Local, Scope});
  };
  // Of a member function, or a closure made in one, whose `this` is the
  // object: where the object, or a part of it, is (designatedPlaces), and
  // the values of its address to follow (ObjectValues), by scope.
  const auto *Method = dyn_cast<CXXMethodDecl>(Function);
  const bool HasObject = Method && Method->isInstance();
  auto OfObject = [&](const Expr *E) {
    return HasObject &&
           llvm::any_of(PlacesOf(E, nullptr),
                        [](const CSlot &Place) {
                          return Place.Place == EPlace::This &&
                                 !throughMember(Place);
                        });
  };
  llvm::SmallVector<std::pair<const Stmt *, unsigned>, 8> ObjectValues;
  // What \p S, of the scope \p Scope, does with the object, where it is
  // asked: its address taken, `this` or `&m_Value`, is followed
  // (ObjectValues); a reference to it or a part of it bound where the flow
  // can't follow it, also by a call that keeps it (keptBeyondCall), and a
  // member function that keeps its object called on it
  // (objectKeptBeyondCall), keep it beyond the function (KeptObject). Where
  // the locals are asked, of any function, a local, or what a reference
  // parameter refers to, that a member function that keeps its object is
  // called on escapes. What a location designates is asked before what a
  // callee keeps, whose summary walks its body.
  auto Objects = [&](const Stmt *S, const CEffects &Effects, unsigned Scope,
                     bool Captures, const CEffectIndex &In) {
    const bool Object = AskObject && HasObject;
    if (Object && Effects.Statement == EStatement::This)
      ObjectValues.push_back({S, Scope});
    for (const COperandEffect &Effect : Effects.Operands) {
      const auto *Operand = dyn_cast_or_null<Expr>(Effect.Operand);
      if (!Operand)
        continue;
      switch (Effect.Use) {
      case EUse::AddressOf:
        if (Object && OfObject(Operand))
          ObjectValues.push_back({S, Scope});
        break;
      // Not given back, which the caller follows.
      case EUse::Bind:
        if (Object && Operand->isGLValue() &&
            Effect.Into.Kind != EDestination::Return &&
            !ownedValuesOf(Operand, Context) && OfObject(Operand) &&
            (BindsAway(S, Effect, Captures, In) ||
             (Effect.Into.Kind == EDestination::Parameter &&
              keptBeyondCall(S, Effect.Into, Context))))
          KeptObject = true;
        break;
      case EUse::CallObject:
        if (!Operand->isGLValue())
          break;
        if (Object && OfObject(Operand) &&
            objectKeptBeyondCall(S, Function, Context))
          KeptObject = true;
        // Not through what pointers hold, which the walk of an address
        // follows (Work), and which would ask the flow while it is made.
        if (AskLocals)
          if (const llvm::SmallVector<const VarDecl *, 2> Of =
                  LocalsThrough(Operand, /*Values=*/false);
              !Of.empty() && objectKeptBeyondCall(S, nullptr, Context))
            for (const VarDecl *Local : Of)
              Escape(Local, false);
        break;
      case EUse::Value:
      case EUse::Access:
      case EUse::Update:
      case EUse::Write:
      case EUse::Copy:
      case EUse::Forward:
      case EUse::Dereference:
      case EUse::Callee:
      case EUse::Condition:
      case EUse::Opaque:
      case EUse::Discarded:
      case EUse::Statement:
      case EUse::Unevaluated:
        break;
      }
    }
  };
  // Only what can run (forEachRunning).
  const Stmt *Body = Function->getBody();
  forEachRunning(Function, Body, Body, Context, [&](const Stmt *S) {
    const CEffects &Effects = *Index->effectsOf(S);
    if (AskLocals)
      Addresses(S, Effects, 0);
    Objects(S, Effects, 0, true, *Index);
    // A reference bound to it that isn't a local one; not a capture of a
    // closure made here, whose writes are where it can be called (callsOf)
    // and whose body is told below.
    // Of a call that gives back only a part of what the parameter refers to
    // (givesPartOf), what is done with what it gives, as with an address.
    for (const COperandEffect &Effect : Effects.Operands)
      if (!AskLocals)
        break;
      else if (const Expr *Bound = BindsAway(S, Effect, true, *Index))
        for (const VarDecl *Each : Locals(Bound))
          if (const auto *Call = dyn_cast<CallExpr>(S);
              Call && Effect.Into.Kind == EDestination::Parameter &&
              givesPartOf(Call, Effect.Into.Index, Context))
            Work.push_back({Call, Each, 0});
          else
            Escape(Each, Effect.Into.Kind == EDestination::Return);
      else
        KeptAway(S, Effect);
    // What a closure made here does with what it refers to: its address,
    // followed in its body, a reference bound to it that isn't local, a
    // closure in it that refers to it, each where the flow can't follow it.
    if (Index->effectsOf(S)->Statement == EStatement::Lambda)
      if (const auto *Lambda = cast<LambdaExpr>(S); Lambda->getBody()) {
        const unsigned Scope = Scopes.size();
        Scopes.push_back({Lambda->getCallOperator(), Lambda->getBody(),
                          &sharedFlowCache().indexOf(Lambda->getBody(),
                                                     Context)});
        forEachRunning(
            Lambda->getCallOperator(), Lambda->getBody(), Lambda->getBody(),
            Context, [&](const Stmt *Each) {
              const CEffects &Of = effectsOf(Each, Context);
              if (AskLocals)
                Addresses(Each, Of, Scope);
              Objects(Each, Of, Scope, false, *Scopes[Scope].Index);
              for (const COperandEffect &Effect : Of.Operands)
                if (!AskLocals)
                  break;
                else if (const Expr *Bound = BindsAway(Each, Effect, false,
                                                       *Scopes[Scope].Index))
                  for (const VarDecl *Local : Locals(Bound))
                    Escape(Local, Effect.Into.Kind == EDestination::Return);
                else
                  KeptAway(Each, Effect);
            });
      }
  });
  llvm::DenseSet<std::pair<const Expr *, const VarDecl *>> Seen;
  // The holders whose reads were followed for a local.
  llvm::DenseSet<std::pair<const VarDecl *, const VarDecl *>> Followed;
  while (!Work.empty()) {
    const CWork Each = Work.pop_back_val();
    const Expr *Address = Each.Address;
    const VarDecl *Local = Each.Local;
    if (!Seen.insert({Address, Local}).second || Kept.contains(Local))
      continue;
    const CScope &In = Scopes[Each.Scope];
    // Of the scope it is a local of: a local of a closure there, else of the
    // function, also one a closure refers to. Where a local pointer that
    // holds it is read, its value goes on, in that scope, each holder once;
    // a pointer to it whose own address is used elsewhere gives it there too
    // (the escapes of holders, below).
    auto Hold = [&](const VarDecl *Holder) {
      Holders[Holder].push_back(Local);
      unsigned Scope = 0;
      const auto *Parent =
          dyn_cast_or_null<FunctionDecl>(Holder->getParentFunctionOrMethod());
      for (unsigned iScope = 1; iScope < Scopes.size(); ++iScope)
        if (Parent && Scopes[iScope].Owner == Parent)
          Scope = iScope;
      HolderScopes.try_emplace(Holder, Scope);
      if (!Followed.insert({Holder, Local}).second)
        return;
      const CScope &Of = Scopes[HolderScopes.lookup(Holder)];
      for (const DeclRefExpr *Named :
           sharedFlowCache().namesIn(Of.Owner, Of.Body, Holder, Context))
        if (Named->getDecl() == Holder)
          for (const CEffectIndex::CUse &Use : Of.Index->consumersOf(Named))
            if (Use.Effect->Use == EUse::Access && isa<Expr>(Use.Parent))
              Work.push_back(
                  {cast<Expr>(Use.Parent), Local, HolderScopes.lookup(Holder)});
    };
    for (const CEffectIndex::CUse &Use : In.Index->consumersOf(Address)) {
      const CDestination &Into = Use.Effect->Into;
      switch (Use.Effect->Use) {
      case EUse::Copy:
      case EUse::Bind:
        switch (Into.Kind) {
        case EDestination::Variable:
          if (const auto *Holder = dyn_cast_or_null<VarDecl>(Into.Declaration);
              Holder && Holder->hasLocalStorage())
            Hold(Holder);
          else
            Escape(Local, false);
          break;
        case EDestination::Designated: {
          bool Told = true;
          const auto Targets = namedSlots(Into.Object, Context, &Told);
          if (!Told || Targets.empty())
            Escape(Local, false);
          for (const CSlot &Target : Targets)
            Hold(Target.Variable);
          break;
        }
        case EDestination::Parameter: {
          const auto *Call = dyn_cast_or_null<CallExpr>(Use.Parent);
          if (Call && givesItsArgument(Call)) {
            Work.push_back({Call, Local, Each.Scope});
            break;
          }
          // What can keep it, what gives back a pointer or a reference; not
          // a reference that is only copied, `auto pKeep =
          // fg_GetJob(pContext)`, whose copies hold the value it refers to,
          // not where it is (CopiedOnly); and what keeps it beyond the call
          // (keptBeyondCall).
          if (!Into.Callee || Into.Callee->getReturnType()->isPointerType() ||
              (Into.Callee->getReturnType()->isReferenceType() &&
               !(Call && CopiedOnly(Call, *In.Index))) ||
              keptBeyondCall(Use.Parent, Into, Context))
            Escape(Local, false);
          break;
        }
        case EDestination::None:
        case EDestination::Field:
        case EDestination::Capture:
        case EDestination::Element:
        case EDestination::Base:
        case EDestination::Return:
        case EDestination::Heap:
        case EDestination::Temporary:
        case EDestination::Thrown:
          Escape(Local, Into.Kind == EDestination::Return);
          break;
        }
        break;
      case EUse::Opaque:
        Escape(Local, false);
        break;
      // The object of a member function that keeps it beyond the call.
      case EUse::CallObject:
        if (objectKeptBeyondCall(Use.Parent, nullptr, Context))
          Escape(Local, false);
        break;
      // What it points to, as what such a member function is called on,
      // `(*pValue).f_Register()`, past what gives it on.
      case EUse::Dereference: {
        llvm::SmallVector<const Stmt *, 2> Pointee{Use.Parent};
        for (size_t iStep = 0; iStep < Pointee.size() && iStep < 16; ++iStep)
          for (const CEffectIndex::CUse &Of :
               In.Index->consumersOf(Pointee[iStep]))
            if (Of.Effect->Use == EUse::Forward)
              Pointee.push_back(Of.Parent);
            else if (Of.Effect->Use == EUse::CallObject &&
                     objectKeptBeyondCall(Of.Parent, nullptr, Context))
              Escape(Local, false);
        break;
      }
      case EUse::Forward:
      case EUse::Access:
      case EUse::Update:
      case EUse::Write:
      case EUse::AddressOf:
      case EUse::Callee:
      case EUse::Condition:
      case EUse::Value:
      case EUse::Discarded:
      case EUse::Statement:
      case EUse::Unevaluated:
        break;
      }
    }
  }
  // What a pointer that escapes holds escapes with it.
  for (bool Changed = true; Changed;) {
    Changed = false;
    for (const auto &[Holder, Held] : Holders)
      for (const VarDecl *Local : Held) {
        if (Escaped.contains(Holder))
          Changed |= Escaped.insert(Local).second;
        if (Kept.contains(Holder))
          Changed |= Kept.insert(Local).second;
      }
  }
  // Of a reference parameter, what it refers to, whose place isn't a slot.
  for (const ParmVarDecl *Parameter : Function->parameters())
    if (Parameter->getType()->isReferenceType() && Escaped.erase(Parameter)) {
      EscapedReferents.insert(Parameter);
      if (Kept.erase(Parameter))
        KeptReferents.insert(Parameter);
    }
  // Of a pointer parameter, what it points to, where its value goes where
  // the flow can't follow it: from each read of it, and of each local
  // pointer it is given to, through what gives it on, as an address is
  // followed above; also where such a local's own address escapes.
  for (const ParmVarDecl *Parameter : Function->parameters()) {
    if (!AskLocals || !Parameter->getType()->isPointerType() ||
        !isSlotVariable(Parameter))
      continue;
    // One whose own address escapes can be given another value there.
    if (Escaped.contains(Parameter)) {
      EscapedReferents.insert(Parameter);
      if (Kept.contains(Parameter))
        KeptReferents.insert(Parameter);
      continue;
    }
    llvm::SmallVector<const VarDecl *, 4> Pointers{Parameter};
    llvm::SmallPtrSet<const VarDecl *, 4> Told{Parameter};
    // Each value, with the index of the scope it is in: the function's, or
    // that of a closure made in it whose body names it (Scopes).
    llvm::SmallVector<std::pair<const Stmt *, const CEffectIndex *>, 8> Values;
    // Kept beyond the function, or only given back (Returns).
    bool Escapes = false;
    bool Returns = false;
    for (size_t iPointer = 0; iPointer < Pointers.size() && !Escapes &&
                              iPointer < 64;
         ++iPointer) {
      const VarDecl *Pointer = Pointers[iPointer];
      if (Pointer != Parameter && Escaped.contains(Pointer)) {
        Escapes = Kept.contains(Pointer);
        Returns = true;
        if (Escapes)
          break;
      }
      for (const CScope &In : Scopes)
        for (const DeclRefExpr *Named :
             sharedFlowCache().namesIn(In.Owner, In.Body, Pointer, Context))
          if (Named->getDecl() == Pointer)
            for (const CEffectIndex::CUse &Use : In.Index->consumersOf(Named))
              if (Use.Effect->Use == EUse::Access)
                Values.push_back({Use.Parent, In.Index});
      // Each value of it, up through what gives it on.
      for (size_t nStep = 0; !Values.empty() && !Escapes && nStep < 256;
           ++nStep) {
        const auto [Value, ValueIndex] = Values.pop_back_val();
        for (const CEffectIndex::CUse &Use : ValueIndex->consumersOf(Value)) {
          const CDestination &Into = Use.Effect->Into;
          switch (Use.Effect->Use) {
          case EUse::Forward:
            Values.push_back({Use.Parent, ValueIndex});
            break;
          case EUse::Copy:
          case EUse::Bind:
            switch (Into.Kind) {
            case EDestination::Variable:
              if (const auto *Holder =
                      dyn_cast_or_null<VarDecl>(Into.Declaration);
                  Holder && Holder->hasLocalStorage() &&
                  isSlotVariable(Holder)) {
                if (Told.insert(Holder).second)
                  Pointers.push_back(Holder);
              } else
                Escapes = true;
              break;
            case EDestination::Designated: {
              bool Named = true;
              const auto Targets = namedSlots(Into.Object, Context, &Named);
              if (!Named || Targets.empty())
                Escapes = true;
              for (const CSlot &Target : Targets) {
                const auto *Holder = dyn_cast_or_null<VarDecl>(Target.Variable);
                if (!Holder || !Target.Path.empty() || !isSlotVariable(Holder))
                  Escapes = true;
                else if (Told.insert(Holder).second)
                  Pointers.push_back(Holder);
              }
              break;
            }
            case EDestination::Parameter: {
              const auto *Call = dyn_cast_or_null<CallExpr>(Use.Parent);
              if (Call && givesItsArgument(Call))
                Values.push_back({Call, ValueIndex});
              else if (!Into.Callee ||
                       Into.Callee->getReturnType()->isPointerType() ||
                       Into.Callee->getReturnType()->isReferenceType() ||
                       keptBeyondCall(Use.Parent, Into, Context))
                Escapes = true;
              break;
            }
            // A closure made here that holds it: what is done with the
            // closure; its body's names of it are followed in its scope.
            case EDestination::Capture:
              if (const auto *Holding =
                      dyn_cast_or_null<LambdaExpr>(Into.Object);
                  Holding && llvm::any_of(Scopes, [&](const CScope &Of) {
                    return Of.Body == Holding->getBody();
                  }))
                Values.push_back({Holding, ValueIndex});
              else
                Escapes = true;
              break;
            case EDestination::None:
            case EDestination::Field:
            case EDestination::Element:
            case EDestination::Base:
            case EDestination::Heap:
            case EDestination::Temporary:
            case EDestination::Thrown:
              Escapes = true;
              break;
            case EDestination::Return:
              Returns = true;
              break;
            }
            break;
          case EUse::Opaque:
            Escapes = true;
            break;
          // The object of a member function that keeps it beyond the call.
          case EUse::CallObject:
            Escapes =
                Escapes || objectKeptBeyondCall(Use.Parent, nullptr, Context);
            break;
          case EUse::Access:
          case EUse::Update:
          case EUse::Write:
          case EUse::Dereference:
          case EUse::AddressOf:
          case EUse::Callee:
          case EUse::Condition:
          case EUse::Value:
          case EUse::Discarded:
          case EUse::Statement:
          case EUse::Unevaluated:
            break;
          }
        }
      }
    }
    if (Escapes || Returns)
      EscapedReferents.insert(Parameter);
    if (Escapes)
      KeptReferents.insert(Parameter);
  }
  // Of the object, each value of its address, `this` or the address of a
  // part, up through what gives it on, in the scope it is in, as a pointer
  // parameter's value above: kept in a local pointer, followed by its
  // names; given back, not kept; anywhere the flow can't follow it, also a
  // call that keeps it or a member function that keeps its object called
  // through it, kept beyond the function (KeptObject).
  llvm::DenseSet<std::pair<const Stmt *, unsigned>> ObjectSeen;
  llvm::SmallPtrSet<const VarDecl *, 4> ObjectHolders;
  // The members of the object that hold its address, and how many of them
  // have their reads followed.
  llvm::SmallVector<CSlot, 2> ObjectMembers;
  size_t nObjectMembersRead = 0;
  for (size_t nStep = 0; !KeptObject && nStep < 1024; ++nStep) {
    // Each read of a member that holds it, in each scope, once.
    if (ObjectValues.empty() && nObjectMembersRead < ObjectMembers.size()) {
      const CSlot Member = ObjectMembers[nObjectMembersRead++];
      for (unsigned iScope = 0; iScope < Scopes.size(); ++iScope)
        for (const Stmt *Read : Scopes[iScope].Index->statements())
          if (const auto *Named = dyn_cast<Expr>(Read);
              Named &&
              effectsOf(Named, Context).Statement == EStatement::Member)
            for (const CEffectIndex::CUse &Use :
                 Scopes[iScope].Index->consumersOf(Named))
              if (Use.Effect->Use == EUse::Access) {
                bool ReadTold = true;
                const llvm::SmallVector<CSlot, 2> Of =
                    PlacesOf(Named, &ReadTold);
                if (!ReadTold || llvm::is_contained(Of, Member))
                  ObjectValues.push_back({Use.Parent, iScope});
              }
      continue;
    }
    if (ObjectValues.empty())
      break;
    const auto [Value, Scope] = ObjectValues.pop_back_val();
    if (!ObjectSeen.insert({Value, Scope}).second)
      continue;
    const CScope &In = Scopes[Scope];
    for (const CEffectIndex::CUse &Use : In.Index->consumersOf(Value)) {
      const CDestination &Into = Use.Effect->Into;
      switch (Use.Effect->Use) {
      case EUse::Forward:
        ObjectValues.push_back({Use.Parent, Scope});
        break;
      case EUse::Copy:
      case EUse::Bind:
        switch (Into.Kind) {
        case EDestination::Variable:
          if (const auto *Holder = dyn_cast_or_null<VarDecl>(Into.Declaration);
              Holder && Holder->hasLocalStorage() && isSlotVariable(Holder)) {
            if (ObjectHolders.insert(Holder).second)
              for (const DeclRefExpr *Named :
                   sharedFlowCache().namesIn(In.Owner, In.Body, Holder,
                                             Context))
                if (Named->getDecl() == Holder)
                  for (const CEffectIndex::CUse &Read :
                       In.Index->consumersOf(Named))
                    if (Read.Effect->Use == EUse::Access)
                      ObjectValues.push_back({Read.Parent, Scope});
          } else
            KeptObject = true;
          break;
        case EDestination::Designated: {
          // In a member of the object itself, `m_ppJob = &m_pSaved;`: held
          // there, each read of that member followed.
          bool PlacesTold = true;
          const llvm::SmallVector<CSlot, 2> Places =
              PlacesOf(Into.Object, &PlacesTold);
          if (PlacesTold && !Places.empty() &&
              llvm::all_of(Places, [](const CSlot &Place) {
                return Place.Place == EPlace::This && !Place.Path.empty() &&
                       !throughMember(Place);
              })) {
            for (const CSlot &Place : Places)
              if (llvm::find(ObjectMembers, Place) == ObjectMembers.end())
                ObjectMembers.push_back(Place);
            break;
          }
          bool Named = true;
          const auto Targets = namedSlots(Into.Object, Context, &Named);
          if (!Named || Targets.empty())
            KeptObject = true;
          for (const CSlot &Target : Targets) {
            const auto *Holder = dyn_cast_or_null<VarDecl>(Target.Variable);
            if (!Holder || !Target.Path.empty() || !isSlotVariable(Holder))
              KeptObject = true;
            else if (ObjectHolders.insert(Holder).second)
              for (const DeclRefExpr *Read :
                   sharedFlowCache().namesIn(In.Owner, In.Body, Holder,
                                             Context))
                if (Read->getDecl() == Holder)
                  for (const CEffectIndex::CUse &Each :
                       In.Index->consumersOf(Read))
                    if (Each.Effect->Use == EUse::Access)
                      ObjectValues.push_back({Each.Parent, Scope});
          }
          break;
        }
        case EDestination::Parameter: {
          const auto *Call = dyn_cast_or_null<CallExpr>(Use.Parent);
          if (Call && givesItsArgument(Call))
            ObjectValues.push_back({Call, Scope});
          else if (!Into.Callee ||
                   Into.Callee->getReturnType()->isPointerType() ||
                   Into.Callee->getReturnType()->isReferenceType() ||
                   keptBeyondCall(Use.Parent, Into, Context))
            KeptObject = true;
          break;
        }
        // A closure that holds it: what is done with the closure.
        case EDestination::Capture:
          if (Into.Object)
            ObjectValues.push_back({Into.Object, Scope});
          else
            KeptObject = true;
          break;
        case EDestination::None:
        case EDestination::Field:
        case EDestination::Element:
        case EDestination::Base:
        case EDestination::Heap:
        case EDestination::Temporary:
        case EDestination::Thrown:
          KeptObject = true;
          break;
        case EDestination::Return:
          break;
        }
        break;
      case EUse::Opaque:
        KeptObject = true;
        break;
      case EUse::CallObject:
        KeptObject =
            KeptObject || objectKeptBeyondCall(Use.Parent, Function, Context);
        break;
      case EUse::Access:
      case EUse::Update:
      case EUse::Write:
      case EUse::Dereference:
      case EUse::AddressOf:
      case EUse::Callee:
      case EUse::Condition:
      case EUse::Value:
      case EUse::Discarded:
      case EUse::Statement:
      case EUse::Unevaluated:
        break;
      }
    }
  }
  // Past as many steps as it follows, it can't be told: kept.
  if (!ObjectValues.empty() || nObjectMembersRead < ObjectMembers.size())
    KeptObject = true;
  return Out;
}

void CLocalFlow::findEscapes() {
  CEscapes Of = escapesOf(Function, *Index, EEscapes::Flow, Context);
  Escaped = std::move(Of.Escaped);
  EscapedReferents = std::move(Of.EscapedReferents);
}

// Whether \p At is in the body of a closure the function makes, not in what
// it is made with: what positionOf finds first is the closure.
// Whether \p Left and \p Right are in operands of an expression whose
// order the effect table leaves open (COperandEffect::Sequence), the
// arguments of a call, `fg_Both((g_pValue = pValue), (pValue = &t_Value))`,
// so either can run first; not past the expression they are in.
bool CLocalFlow::unordered(const Stmt *Left, const Stmt *Right) const {
  if (!Left || !Right || !Index)
    return false;
  // The expressions Left is in, with the operand of each it is in.
  llvm::SmallDenseMap<const Stmt *, std::pair<const Stmt *, unsigned>, 8>
      Around;
  for (const Stmt *S = Left; isa<Expr>(S);) {
    const llvm::ArrayRef<CEffectIndex::CUse> Uses = Index->usesOf(S);
    if (Uses.empty())
      break;
    Around.try_emplace(Uses.front().Parent, S, Uses.front().Effect->Sequence);
    S = Uses.front().Parent;
  }
  for (const Stmt *S = Right; isa<Expr>(S);) {
    const llvm::ArrayRef<CEffectIndex::CUse> Uses = Index->usesOf(S);
    if (Uses.empty())
      return false;
    // Not the operands of what a condition decides, each on a path of its
    // own, the values of a conditional.
    if (const auto It = Around.find(Uses.front().Parent); It != Around.end()) {
      const CEffects *Of = Index->effectsOf(It->first);
      return It->second.first != S &&
             It->second.second == Uses.front().Effect->Sequence && Of &&
             llvm::none_of(Of->Operands, [](const COperandEffect &Effect) {
               return Effect.Use == EUse::Condition;
             });
    }
    S = Uses.front().Parent;
  }
  return false;
}

// The outermost expression \p S is in, by the effect index, \p S itself
// when it is in none; null of what isn't in the index.
const Stmt *CLocalFlow::expressionOf(const Stmt *S) const {
  if (!S || !Index)
    return nullptr;
  if (const auto It = Expressions.find(S); It != Expressions.end())
    return It->second;
  const Stmt *Outer = S;
  for (int Step = 0; Step < 1024; ++Step) {
    const llvm::ArrayRef<CEffectIndex::CUse> Uses = Index->usesOf(Outer);
    if (Uses.empty() || !isa<Expr>(Uses.front().Parent))
      break;
    Outer = Uses.front().Parent;
  }
  return Expressions[S] = Outer;
}

llvm::BitVector CLocalFlow::reachingAt(const CFacts &Facts, unsigned Block,
                                       unsigned Index) const {
  // What reaches the start of the block, then the definitions in it before
  // the element, in order, as reachingBefore applies them: the last that
  // replaces what reaches and those after it, or what reaches the start and
  // each of them when none replaces it.
  // A definition that ends a way the function doesn't take ends what
  // reaches there and gives nothing (CDefinition::Infeasible).
  if (Block >= Facts.In.size())
    return llvm::BitVector(Facts.Definitions.size());
  indexFacts(Facts);
  // Those of the block before the element.
  const auto First = llvm::partition_point(
      Facts.InBlock, [&](unsigned iDefinition) {
        return Facts.Definitions[iDefinition].Block < Block;
      });
  const auto Last = std::partition_point(
      First, Facts.InBlock.end(),
      [&](unsigned iDefinition) {
        return Facts.Definitions[iDefinition].Block == Block &&
               Facts.Definitions[iDefinition].Index < Index;
      });
  const llvm::ArrayRef<unsigned> Before(
      Facts.InBlock.data() + (First - Facts.InBlock.begin()), Last - First);
  // Past the last that replaces what reaches, if any.
  size_t Replacing = Before.size();
  while (Replacing > 0 && Facts.Definitions[Before[Replacing - 1]].Weak)
    --Replacing;
  llvm::BitVector Reaching = Replacing > 0
                                 ? llvm::BitVector(Facts.Definitions.size())
                                 : Facts.In[Block];
  for (unsigned iDefinition :
       Before.drop_front(Replacing > 0 ? Replacing - 1 : 0))
    Reaching.set(iDefinition);
  for (unsigned iDefinition : Reaching.set_bits())
    if (Facts.Definitions[iDefinition].Infeasible)
      Reaching.reset(iDefinition);
  return Reaching;
}

bool CLocalFlow::inClosureBody(const Stmt *At) const {
  if (!At)
    return false;
  DynTypedNode Node = DynTypedNode::create(*At);
  for (int Step = 0; Step < 256; ++Step) {
    if (const Stmt *Each = Node.get<Stmt>()) {
      if (Each == Function->getBody())
        return false;
      if (Positions.count(Each))
        return Each != At && isa<LambdaExpr>(Each);
    }
    const auto Parents = Context.getParents(Node);
    if (Parents.empty())
      return false;
    Node = Parents[0];
  }
  return false;
}

std::optional<std::pair<unsigned, unsigned>>
CLocalFlow::positionOf(const Stmt *At) const {
  if (!At)
    return std::nullopt;
  DynTypedNode Node = DynTypedNode::create(*At);
  for (int Step = 0; Step < 256; ++Step) {
    if (const Stmt *Each = Node.get<Stmt>()) {
      if (const auto It = Positions.find(Each); It != Positions.end())
        return It->second;
      if (Each == Function->getBody())
        return std::nullopt;
    }
    if (const auto *Variable = Node.get<VarDecl>())
      if (const auto It = Declarations.find(Variable); It != Declarations.end())
        return It->second;
    const auto Parents = Context.getParents(Node);
    if (Parents.empty())
      return std::nullopt;
    Node = Parents[0];
  }
  return std::nullopt;
}

// What \p fObserve tells of the facts of \p Slot (CFlowSummaries::Facts), as
// the slot analysis of the function has them, worked out once.
// The targets of the writes are worked out before the facts of slots that
// aren't yet: each designation of a write that reads the values
// of a slot then works out those facts inside it, which observe only what
// that write designates of their variable, rather than the write inside the
// facts, which would read their values before they are worked out.
// Each write once (CFlowSummaries::Primed), from where a priming it is in
// is at.
void CLocalFlow::primeTargets() const {
  if (!Ready)
    return;
  CFlowSummaries &Summaries = sharedFlowCache().summaries(Context);
  for (;;) {
    const unsigned iWrite = Summaries.Primed[Function]++;
    if (iWrite >= Writes.size())
      return;
    if (Writes[iWrite].Kind != CWrite::EKind::Declare)
      Summaries.Targets.observe({Function, iWrite},
                                [](const CTargets &) { return true; });
  }
}

template <typename tf_FObserve>
auto CLocalFlow::observeFacts(const CSlot &Slot, tf_FObserve fObserve) const {
  CFlowSummaries &Summaries = sharedFlowCache().summaries(Context);
  const CSlotKey Key{Function, Slot.Variable, Slot.Path, Slot.Place,
                     Slot.Element ? Slot.Index : std::nullopt};
  if (!Summaries.Facts.finished(Key)) {
    if (!Summaries.Facts.contains(Key))
      Summaries.FactKeys[Function].push_back(Key);
    primeTargets();
  }
  return Summaries.Facts.observe(Key, std::move(fObserve));
}

template <typename tf_FRead>
auto CLocalFlow::readFacts(const CSlot &Slot, tf_FRead fRead) const {
  CFlowSummaries &Summaries = sharedFlowCache().summaries(Context);
  const CSlotKey Key{Function, Slot.Variable, Slot.Path, Slot.Place,
                     Slot.Element ? Slot.Index : std::nullopt};
  if (!Summaries.Facts.finished(Key)) {
    if (!Summaries.Facts.contains(Key))
      Summaries.FactKeys[Function].push_back(Key);
    primeTargets();
  }
  return fRead(Summaries.Facts.get(Key));
}

bool CLocalFlow::factsFinal(const CSlot &Slot) const {
  const CSlotKey Key{Function, Slot.Variable, Slot.Path, Slot.Place,
                     Slot.Element ? Slot.Index : std::nullopt};
  return sharedFlowCache().summaries(Context).Facts.finished(Key) != nullptr;
}

// What \p Slot is given where in the flow: what it starts with, each value
// it is given by what designates it (targetsOn), and of a member, what the
// whole it is in is given.
// The type of what \p Place designates, a place that isn't a local slot,
// null of an element: of its last member, else of what the parameter refers
// to or points to, or of the object of the function.
static QualType placeType(const CSlot &Place, const FunctionDecl *Function) {
  if (Place.Element)
    return QualType();
  QualType Type;
  if (!Place.Path.empty())
    Type = Place.Path.back()->getType();
  else if (Place.Place == EPlace::Referent)
    Type = Place.Variable->getType()->isPointerType()
               ? Place.Variable->getType()->getPointeeType()
               : Place.Variable->getType().getNonReferenceType();
  else if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Function))
    Type = Method->getFunctionObjectParameterType();
  return Type.isNull() ? Type
                       : Type.getNonReferenceType()
                             .getCanonicalType()
                             .getUnqualifiedType();
}

// \p Type without a reference or qualifiers, canonical.
static QualType plainType(QualType Type) {
  return Type.isNull() ? Type
                       : Type.getNonReferenceType()
                             .getCanonicalType()
                             .getUnqualifiedType();
}

// What an element of what has \p Type is: of an array its element, of a
// pointer what it points to, else what has \p Type itself.
static QualType elementTypeOf(QualType Type) {
  // What a reference refers to, of an array its element.
  if (!Type.isNull() && Type->isReferenceType()) {
    Type = plainType(Type);
    if (const ArrayType *Array = Type->getAsArrayTypeUnsafe())
      return plainType(Array->getElementType());
    return Type;
  }
  Type = plainType(Type);
  if (Type.isNull())
    return Type;
  if (const ArrayType *Array = Type->getAsArrayTypeUnsafe())
    return plainType(Array->getElementType());
  if (Type->isPointerType())
    return plainType(Type->getPointeeType());
  return Type;
}

namespace {
/// Of each two record types, whether an object of the first holds one of
/// the second by value (holdsObjectOf), once told.
struct CHeldObjects {
  llvm::DenseMap<std::pair<const Type *, const Type *>, bool> Of;
};
} // namespace

// Whether an object of \p Outer holds one of \p Inner by value, other than
// as itself: a member, an element of an array member, or a base, at any
// depth, of the type or of a class derived from it or that it derives from.
// A work stack over the records, each once.
static bool holdsObjectOf(QualType Outer, QualType Inner, ASTContext &Context) {
  const auto *Of = plainType(Outer).isNull()
                       ? nullptr
                       : plainType(Outer)->getAsCXXRecordDecl();
  const auto *Held = plainType(Inner).isNull()
                         ? nullptr
                         : plainType(Inner)->getAsCXXRecordDecl();
  if (!Of || !Held || !Of->hasDefinition())
    return false;
  auto &Cache = astCache<CHeldObjects>(Context).Of;
  const std::pair<const Type *, const Type *> Key{
      plainType(Outer).getTypePtr(), plainType(Inner).getTypePtr()};
  if (const auto It = Cache.find(Key); It != Cache.end())
    return It->second;
  auto Related = [&](const CXXRecordDecl *Record) {
    return Record->getCanonicalDecl() == Held->getCanonicalDecl() ||
           (Record->hasDefinition() && Record->isDerivedFrom(Held)) ||
           (Held->hasDefinition() && Held->isDerivedFrom(Record));
  };
  bool Holds = false;
  llvm::SmallVector<const CXXRecordDecl *, 8> Work{Of};
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen{Of};
  while (!Work.empty() && !Holds) {
    const CXXRecordDecl *Record = Work.pop_back_val();
    if (!Record->hasDefinition())
      continue;
    llvm::SmallVector<const CXXRecordDecl *, 8> Parts;
    for (const CXXBaseSpecifier &Base : Record->bases())
      if (const auto *Part = Base.getType()->getAsCXXRecordDecl())
        Parts.push_back(Part);
    for (const FieldDecl *Field : Record->fields())
      if (!Field->getType()->isReferenceType())
        if (const auto *Part = Context.getBaseElementType(Field->getType())
                                   ->getAsCXXRecordDecl())
          Parts.push_back(Part);
    for (const CXXRecordDecl *Part : Parts) {
      if (Related(Part)) {
        Holds = true;
        break;
      }
      if (Seen.insert(Part).second)
        Work.push_back(Part);
    }
  }
  Cache[Key] = Holds;
  return Holds;
}

// Whether what has \p Type can be any object's bytes: `void`, a character
// type, `std::byte`.
static bool bytesType(QualType Type) {
  Type = plainType(Type);
  return !Type.isNull() &&
         (Type->isVoidType() || Type->isCharType() ||
          Type->isSpecificBuiltinType(BuiltinType::UChar) ||
          Type->isSpecificBuiltinType(BuiltinType::SChar) ||
          Type->isStdByteType());
}

bool throughMember(const CSlot &Place) {
  if (crossesPointer(Place.Path, 0))
    return true;
  const QualType Last =
      Place.Path.empty() ? QualType() : Place.Path.back()->getType();
  return Place.Element && !Last.isNull() &&
         (Last->isPointerType() || Last->isReferenceType());
}

bool ownedElement(const CSlot &Place, const FunctionDecl *Function) {
  if (!Place.Element || crossesPointer(Place.Path, 0))
    return false;
  QualType Holder;
  if (!Place.Path.empty())
    Holder = Place.Path.back()->getType();
  else if (Place.Place == EPlace::Variable && Place.Variable)
    Holder = Place.Variable->getType();
  else
    Holder = placeType({Place.Variable, {}, false, Place.Place}, Function);
  return !Holder.isNull() && !Holder->isPointerType() &&
         !Holder->isReferenceType();
}

bool canBeInObject(QualType Written, QualType Holder, ASTContext &Context) {
  Written = plainType(Written);
  Holder = plainType(Holder);
  if (Written.isNull() || Holder.isNull() || bytesType(Written))
    return true;
  if (Written->getAsCXXRecordDecl()) {
    const auto *Of = Holder->getAsCXXRecordDecl();
    const auto *Held = Written->getAsCXXRecordDecl();
    return Of && (Of->getCanonicalDecl() == Held->getCanonicalDecl() ||
                  (Of->hasDefinition() && Of->isDerivedFrom(Held)) ||
                  (Held->hasDefinition() && Held->isDerivedFrom(Of)) ||
                  holdsObjectOf(Holder, Written, Context));
  }
  // A member of the type, at any depth by value: a work stack over the
  // records, each once.
  llvm::SmallVector<const CXXRecordDecl *, 8> Work;
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
  if (const auto *Record = Holder->getAsCXXRecordDecl())
    Work.push_back(Record);
  while (!Work.empty()) {
    const CXXRecordDecl *Record = Work.pop_back_val();
    if (!Record->hasDefinition() || !Seen.insert(Record).second)
      continue;
    for (const CXXBaseSpecifier &Base : Record->bases())
      if (const auto *Part = Base.getType()->getAsCXXRecordDecl())
        Work.push_back(Part);
    for (const FieldDecl *Field : Record->fields()) {
      if (Field->getType()->isReferenceType())
        continue;
      const QualType Each =
          plainType(Context.getBaseElementType(Field->getType()));
      if (Each == Written)
        return true;
      if (const auto *Part = Each->getAsCXXRecordDecl())
        Work.push_back(Part);
    }
  }
  return false;
}

namespace {
/// An object that holds a place that isn't a local slot, or the place itself
/// (objectsAround): its type, and the member of what holds it that it is;
/// null of the root, of what a member points to, and of an element, which
/// can be any object's.
struct CAround {
  QualType Type;
  const FieldDecl *Member = nullptr;
};

/// Where a write can be one of a place that isn't a local slot
/// (writesAround): how many members of the place's path lead to what it
/// writes, whether it writes the place as a whole, and whether the rest of
/// the path is a part of what it writes, of an object of the same type or
/// one derived from it or that it derives from, that projected tells.
struct CAliasAt {
  size_t Depth = 0;
  bool Whole = false;
  bool Projected = false;
  /// Whether what it writes is the object that holds the rest of the path,
  /// of its type or one derived from it or that it derives from: not one
  /// that holds it, nor bytes.
  bool Exact = false;
};
} // namespace

// The objects that hold \p Slot, a place that isn't a local slot, from its
// root, the object of the function or what its parameter refers to or
// points to, through each member of its path, of a member that points to
// what holds the next what it points to, to the slot itself last, of an
// element the element.
static llvm::SmallVector<CAround, 4> objectsAround(const CSlot &Slot,
                                                   const FunctionDecl *Function) {
  llvm::SmallVector<CAround, 4> Around;
  Around.push_back(
      {placeType({Slot.Variable, {}, false, Slot.Place}, Function), nullptr});
  const llvm::ArrayRef<const FieldDecl *> Path = Slot.Path;
  for (size_t iField = 0; iField < Path.size(); ++iField) {
    if (iField + 1 < Path.size() && crossesPointer(Path.slice(iField, 2), 1))
      Around.push_back({elementTypeOf(Path[iField]->getType()), nullptr});
    else
      Around.push_back({plainType(Path[iField]->getType()), Path[iField]});
  }
  if (Slot.Element)
    Around.push_back({elementTypeOf(Around.back().Type), nullptr});
  return Around;
}

// Whether what the members \p Members lead to, the last the object written,
// can be the object \p Around[iAround] is, by the members to both where
// both are told: from the outermost of them in, the first two that differ
// are members of a union, which are the same storage, or none differ.
static bool sameMembers(llvm::ArrayRef<const FieldDecl *> Members,
                        llvm::ArrayRef<CAround> Around, size_t iAround) {
  size_t nTold = 0;
  while (nTold < Members.size() && nTold <= iAround &&
         Around[iAround - nTold].Member)
    ++nTold;
  for (size_t iMember = nTold; iMember-- > 0;) {
    const FieldDecl *Mine = Around[iAround - iMember].Member;
    const FieldDecl *Theirs = Members[Members.size() - 1 - iMember];
    if (Mine != Theirs)
      return Mine->getParent()->isUnion() || Theirs->getParent()->isUnion();
  }
  return true;
}

// Whether a write of an object of the type \p Written, what the members \p
// Members of what holds it lead to where those are told, the last the object
// written, of its members \p Then, can be one of \p Slot, a place that isn't
// a local slot (CAliasAt): an object that holds the slot, or the slot itself
// (objectsAround), can be what it writes, of the same type, a class derived
// from it or that it derives from, the members to it the same where both are
// told, or members of one union (sameMembers); or, of what isn't a member of
// an object told, one that holds it by value (holdsObjectOf); what can be any
// object's bytes (bytesType) can be any of them.
static std::optional<CAliasAt>
writesAround(const CSlot &Slot, QualType Written,
             llvm::ArrayRef<const FieldDecl *> Members,
             llvm::ArrayRef<const FieldDecl *> Then,
             const FunctionDecl *Function) {
  Written = plainType(Written);
  if (Written.isNull())
    return std::nullopt;
  ASTContext &Context = Function->getASTContext();
  const llvm::SmallVector<CAround, 4> Around = objectsAround(Slot, Function);
  const size_t nPath = Slot.Path.size();
  if (bytesType(Written))
    return CAliasAt{0, false, false};
  const auto *WrittenRecord = Written->getAsCXXRecordDecl();
  for (size_t iAround = 0; iAround < Around.size(); ++iAround) {
    const CAround &At = Around[iAround];
    if (At.Type.isNull())
      continue;
    const auto *Record = At.Type->getAsCXXRecordDecl();
    const bool Same = At.Type == Written;
    // Of a class that derives from the one written, the base written holds
    // the member next on the path, or is the slot.
    const bool Base =
        !Same && Record && WrittenRecord && WrittenRecord->hasDefinition() &&
        Record->hasDefinition() && Record->isDerivedFrom(WrittenRecord) &&
        (iAround >= nPath ||
         [&] {
           const auto *Of = dyn_cast<CXXRecordDecl>(Slot.Path[iAround]->getParent());
           return Of && (Of->getCanonicalDecl() ==
                             WrittenRecord->getCanonicalDecl() ||
                         WrittenRecord->isDerivedFrom(Of));
         }());
    const bool Derived = !Same && Record && WrittenRecord &&
                         WrittenRecord->hasDefinition() &&
                         WrittenRecord->isDerivedFrom(Record);
    // Of what isn't a member of an object told, an object that holds it:
    // a member is in the object it is a member of, of its type; also of
    // what isn't a record, a record with a member of its type by value,
    // `*_pHolder = _Old;` of what `CJob const *&` refers to (canBeInObject),
    // not an element of an array member, which is in that member's object.
    const bool InArrayMember = iAround > 0 && Around[iAround - 1].Member &&
                               !Around[iAround - 1].Type.isNull() &&
                               Around[iAround - 1].Type->isArrayType();
    const bool Holding =
        !Same && !Base && !Derived && !At.Member &&
        (holdsObjectOf(Written, At.Type, Context) ||
         (!Record && !InArrayMember && WrittenRecord &&
          canBeInObject(At.Type, Written, Context)));
    if (!Same && !Base && !Derived && !Holding)
      continue;
    // The members to it, of both where both are told (sameMembers).
    if (!Holding && !sameMembers(Members, Around, iAround))
      continue;
    if (Holding)
      return CAliasAt{iAround, false, false};
    // The element itself.
    if (iAround > nPath)
      return CAliasAt{nPath, Then.empty() && !Base, false, true};
    // Of the members it writes, those of the path from there on.
    const size_t Common = std::min(Then.size(), nPath - std::min(nPath, iAround));
    if (!llvm::equal(Then.take_front(Common),
                     llvm::ArrayRef(Slot.Path).slice(std::min(nPath, iAround),
                                                     Common)))
      continue;
    const size_t Depth = iAround + Then.size();
    if (Then.size() > Common || Depth > nPath)
      return CAliasAt{std::min(Depth, nPath), false, false};
    return CAliasAt{Depth, Depth + 1 == Around.size() && !Base,
                    !Slot.Element && Depth < nPath, true};
  }
  return std::nullopt;
}

// Whether a write of \p Other, a place that isn't a local slot, can be one of
// \p Slot, another such place (CAliasAt): of the object of the function and
// what a parameter refers to or points to, or of two parameters, unless one
// of them is stated to be distinct (DMibDistinct), or of the same one through
// what a member points to, `m_pSelf->m_pJob`, the same member of objects
// that can be one, or an object that holds the slot or is it
// (writesAround); of the same parameter or object without a member that
// points elsewhere, the place itself. A place reached through what a member
// points to or refers to, also what the last member of it does, `*m_ppJob`
// or the referent of a reference member, can be any object of its type,
// one of the same root too, of either place.
static std::optional<CAliasAt> mayAlias(const CSlot &Slot, const CSlot &Other,
                                        const FunctionDecl *Function) {
  if (Other.Place == EPlace::Variable || Slot.Place == EPlace::Variable)
    return std::nullopt;
  const llvm::ArrayRef<const FieldDecl *> Path = Other.Path;
  // The members of the object written past the last one it is reached
  // through, of what holds it to it.
  size_t Written = 0;
  for (size_t iField = 0; iField + 1 < Path.size(); ++iField)
    if (crossesPointer(Path.slice(iField, 2), 1))
      Written = iField + 1;
  auto Crosses = [](const CSlot &Place) {
    for (size_t iField = 0; iField + 1 < Place.Path.size(); ++iField)
      if (crossesPointer(llvm::ArrayRef(Place.Path).slice(iField, 2), 1))
        return true;
    const QualType Last =
        Place.Path.empty() ? QualType() : Place.Path.back()->getType();
    return Place.Element && !Last.isNull() &&
           (Last->isPointerType() || Last->isReferenceType());
  };
  const bool Through = Crosses(Slot) || Crosses(Other);
  if (!Through) {
    if (Other.Place == Slot.Place && Other.Variable == Slot.Variable)
      return std::nullopt;
    for (const CSlot *Each : {&Slot, &Other})
      if (const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Each->Variable);
          Parameter && distinctParameter(Parameter))
        return std::nullopt;
  }
  if (!Slot.Path.empty() && !Other.Path.empty() &&
      Slot.Path.back() == Other.Path.back() && Slot.Element == Other.Element) {
    // Elements of another constant index of the member, of storage of its
    // own (ownedElement): of what a pointer member points to, the member can
    // point elsewhere between the two.
    if (Slot.Element && Slot.Index && Other.Index &&
        *Slot.Index != *Other.Index && ownedElement(Slot, Function) &&
        ownedElement(Other, Function))
      return std::nullopt;
    return CAliasAt{Slot.Path.size(), true, false};
  }
  // What an element written is: what its array or pointer holds.
  if (Other.Element)
    return writesAround(
        Slot,
        elementTypeOf(Path.empty()
                          ? placeType({Other.Variable, {}, false, Other.Place},
                                      Function)
                          : Path.back()->getType()),
        {}, {}, Function);
  return writesAround(Slot, placeType(Other, Function), Path.drop_front(Written),
                      {}, Function);
}

// Whether each way of provenance from what \p Target designates, or points
// to, ends at an object that neither the object of the function nor what a
// parameter refers to can be: one made there, by `new`, a construction, a
// temporary or a library function that gives back storage of its own
// (CLibraryFunction::New), or a local variable of the function; not where a
// way can't be told.
static bool freshStorage(const Expr *Target, ASTContext &Context) {
  CProvenance Walk(Context);
  bool Ended = false;
  bool Fresh = true;
  const bool Complete = Walk.reach(
      Target, 0,
      [&](const Expr *At, unsigned Level, bool) {
        if (Level > 0)
          return EReach::Continue;
        const CEffects &Of = effectsOf(At, Context);
        switch (Of.Statement) {
        // Not of what a placement gives, which is where it is placed.
        case EStatement::New: {
          const auto *Made = cast<CXXNewExpr>(At);
          const FunctionDecl *Allocator = Made->getOperatorNew();
          if (Made->getNumPlacementArgs() != 0 &&
              !(Allocator && Allocator->isReplaceableGlobalAllocationFunction())) {
            Fresh = false;
            return EReach::Root;
          }
          return EReach::Root;
        }
        case EStatement::Temporary:
        case EStatement::Construction:
        case EStatement::InheritedConstruction:
        case EStatement::InitializerList:
        case EStatement::Lambda:
          return EReach::Root;
        case EStatement::Call:
          if (const CLibraryFunction *Library = libraryFunction(Of.Callee);
              Library && Library->New)
            return EReach::Root;
          return EReach::Continue;
        case EStatement::Name: {
          // A local of the function, not a parameter, nor one of the
          // function a closure is made in, which a parameter of the closure
          // can refer to; a local reference by what it is bound to.
          const auto *Named = cast<DeclRefExpr>(At);
          const auto *Local = dyn_cast<VarDecl>(Named->getDecl());
          if (Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
              !Named->refersToEnclosingVariableOrCapture())
            return Local->getType()->isReferenceType() ? EReach::Continue
                                                        : EReach::Root;
          Fresh = false;
          return EReach::Root;
        }
        case EStatement::This:
          Fresh = false;
          return EReach::Root;
        case EStatement::Other:
        case EStatement::Member:
        case EStatement::Declaration:
        case EStatement::RangeLoop:
        case EStatement::CoroutineBody:
        case EStatement::Return:
        case EStatement::Coreturn:
        case EStatement::Default:
          return EReach::Continue;
        }
        return EReach::Continue;
      },
      &Ended);
  return Complete && !Ended && Fresh;
}

// Whether each way of provenance from what \p Target designates, or points
// to, ends at storage that only code that can't be seen gives: what a
// function whose body can't be seen gives back where it is given nothing
// that can hold an address, `fg_OtherView()->m_pJob`, or static storage; not
// what can come from what the function is given, its object, a parameter, a
// local, nor where a way can't be told. Such a write can be one of what
// unseen code can reach only: what a caller gives where that is reached
// (CFlowFact::Reached), never a local of the caller whose address doesn't
// escape.
static bool unseenStorage(const Expr *Target, ASTContext &Context) {
  // An argument that can't carry an address: a number that is a constant,
  // a floating point number, an enumerator, a null pointer constant.
  auto Carries = [&](const Expr *Argument) {
    const QualType Type = Argument->getType();
    if (Type->isNullPtrType() || Type->isRealFloatingType())
      return false;
    if (Type->isIntegralOrEnumerationType() && !Type->isReferenceType() &&
        !Argument->isValueDependent()) {
      return !constantIntegerOf(Argument, Context);
    }
    return true;
  };
  CProvenance Walk(Context);
  bool Ended = false;
  bool Unseen = true;
  const bool Complete = Walk.reach(
      Target, 0,
      [&](const Expr *At, unsigned Level, bool) {
        if (Level > 0)
          return EReach::Continue;
        const CEffects &Of = effectsOf(At, Context);
        switch (Of.Statement) {
        // A call of a body that can't be seen, also one an override can
        // replace, given nothing that can carry an address; one whose body is
        // seen by what it gives back.
        case EStatement::Call: {
          const auto *Call = dyn_cast<CallExpr>(At);
          const FunctionDecl *Definition =
              Of.Callee ? Of.Callee->getDefinition() : nullptr;
          const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Of.Callee);
          if (Call && Definition && Definition->getBody() &&
              !(Method && mayRunOverride(Call, Method)))
            return EReach::Continue;
          if (!Call || callObject(Call, Context) ||
              llvm::any_of(Call->arguments(), Carries))
            Unseen = false;
          return EReach::Root;
        }
        case EStatement::Name: {
          const auto *Named = dyn_cast<VarDecl>(cast<DeclRefExpr>(At)->getDecl());
          if (!Named || Named->hasLocalStorage() ||
              Named->getType()->isReferenceType())
            Unseen = false;
          return EReach::Root;
        }
        case EStatement::New:
        case EStatement::Temporary:
        case EStatement::Construction:
        case EStatement::InheritedConstruction:
        case EStatement::InitializerList:
        case EStatement::Lambda:
        case EStatement::This:
          Unseen = false;
          return EReach::Root;
        case EStatement::Other:
        case EStatement::Member:
        case EStatement::Declaration:
        case EStatement::RangeLoop:
        case EStatement::CoroutineBody:
        case EStatement::Return:
        case EStatement::Coreturn:
        case EStatement::Default:
          return EReach::Continue;
        }
        return EReach::Continue;
      },
      &Ended);
  return Complete && !Ended && Unseen;
}

// Whether \p Write, whose places can't be told (CTargets::Told), can write
// \p Slot, a place that isn't a local slot (CAliasAt): what it writes is
// reached through a pointer or a reference, or can't be followed
// (designatedRoot), and an object of its type can hold the slot or be it
// (writesAround); of what a call leaves in the members of what it is called
// on (summaryWrites), the members it writes are of the slot's path.
// Whether a place of the members \p Written of what holds \p Path is
// another member of a union that holds a member of \p Path, which is the
// same storage: neither is a part of the other, and where they part the
// members are of one union.
static bool sharesUnionStorage(llvm::ArrayRef<const FieldDecl *> Path,
                               llvm::ArrayRef<const FieldDecl *> Written) {
  for (size_t iField = 0; iField < Path.size() && iField < Written.size();
       ++iField)
    if (Path[iField] != Written[iField])
      return Path[iField]->getParent() == Written[iField]->getParent() &&
             Path[iField]->getParent()->isUnion();
  return false;
}

static std::optional<CAliasAt>
untoldWrites(const CSlot &Slot, const Expr *Target, bool Pointer,
             llvm::ArrayRef<const FieldDecl *> Then,
             const FunctionDecl *Function) {
  if (!Target)
    return std::nullopt;
  const CDesignatedRoot Root = designatedRoot(Target, Function);
  if ((Root.Root != ERoot::None && !Root.bThrough) ||
      freshStorage(Target, Function->getASTContext()))
    return std::nullopt;
  const QualType Type = Target->getType().getNonReferenceType();
  return writesAround(Slot,
                      Pointer || !Target->isGLValue()
                          ? (Type->isPointerType() ? Type->getPointeeType()
                                                   : QualType())
                          : Type,
                      {}, Then, Function);
}

CFacts CLocalFlow::makeFacts(const CSlot &Slot) const {
  CFacts Result;
  CFacts *Facts = &Result;
  const unsigned Entry = Graph ? Graph->getEntry().getBlockID() : 0;
  // What escapes of a variable, or of what a reference parameter refers
  // to, makes it untold; the object of the function is told by what the
  // function does to it.
  if (Slot.Place == EPlace::Variable)
    Facts->Told = !Escaped.contains(Slot.Variable);
  else if (Slot.Place == EPlace::Referent)
    Facts->Told = !EscapedReferents.contains(Slot.Variable);
  // What it starts with: what a parameter is given, or what the object of
  // the function or what a parameter refers to holds where the function is
  // entered; of a copy a closure keeps, what it copies where the closure is
  // made.
  if (Slot.Place != EPlace::Variable ||
      (isa<ParmVarDecl>(Slot.Variable) &&
       Slot.Variable->getDeclContext() == Function))
    Facts->Definitions.push_back({Entry,
                                  ~0u,
                                  {CSlotValue::EKind::Given, nullptr},
                                  true,
                                  false,
                                  nullptr,
                                  !Slot.Path.empty()});
  else if (Closure && (Slot.Variable->getDeclContext() != Function ||
                       Slot.Variable->isInitCapture()))
    for (const LambdaCapture &Capture : Closure->captures())
      if (Capture.capturesVariable() &&
          Capture.getCapturedVar() == Slot.Variable) {
        const Expr *Init = captureInitOf(Closure, Capture);
        // Of an init capture of this closure its initializer; one of a
        // closure around it is copied like any variable.
        const Expr *Made = Slot.Variable->isInitCapture() &&
                                   Slot.Variable->getDeclContext() == Function
                               ? Slot.Variable->getInit()
                               : Init;
        if (!Slot.Path.empty()) {
          const Expr *Copied = Made;
          Made = projected(Made, Slot.Path);
          // Of a member of a copy of a local record, what that member of
          // the record holds where the closure is made, in the flow of the
          // function it is a slot of there (localSlotValues).
          const Expr *Same =
              Copied && !Made
                  ? sameValue(ownerValueSource(Copied, Context), Context)
                  : nullptr;
          if (const Expr *From = Same ? copiedFrom(Same, Context) : nullptr)
            Same = From;
          bool Told = true;
          const llvm::SmallVector<CSlot, 2> Sources =
              Same && Same->isGLValue() && Same->getType()->isRecordType()
                  ? designatedSlots(Same, Context, &Told)
                  : llvm::SmallVector<CSlot, 2>();
          if (Told && Sources.size() == 1 && !Sources.front().Element) {
            CSlot Member = Sources.front();
            llvm::append_range(Member.Path, Slot.Path);
            const CSlotValues Values = localSlotValues(Member, Same, Context);
            if (Values.Told && !Values.Values.empty()) {
              bool Weak = false;
              for (const CSlotValue &Each : Values.Values) {
                Facts->Definitions.push_back(
                    {Entry, ~0u, Each, true, Weak, nullptr, true});
                Weak = true;
              }
              continue;
            }
          }
        }
        Facts->Definitions.push_back(
            {Entry, ~0u,
             {Slot.Variable->getType().getNonReferenceType()->isArrayType() &&
                      Slot.Path.empty()
                  ? CSlotValue::EKind::Stored
                  : CSlotValue::EKind::Value,
              Made},
             Made != nullptr, false, nullptr, !Slot.Path.empty()});
      }
  // Of a member, \p Path, of a copy of a local record, `auto Copy = View;`
  // or `Copy = View;`, what that member of the record copied holds there, a
  // definition each, the first a strong one unless \p Weak; false when it
  // can't be told.
  const CWrite *At = nullptr;
  auto CopiedMembers = [&](const Expr *Made, llvm::ArrayRef<const FieldDecl *> Path,
                           bool Weak) {
    // A copy made, or the record itself, a glvalue, also past what only
    // gives it on (sameValue).
    const Expr *Same = Made ? sameValue(Made, Context) : nullptr;
    const Expr *Copied = Made ? copiedFrom(Made, Context) : nullptr;
    if (!Copied && Same && Same->isGLValue() &&
        Same->getType()->isRecordType())
      Copied = Same;
    bool Told = true;
    const llvm::SmallVector<CSlot, 2> Sources =
        Copied ? designatedSlots(Copied, Context, &Told)
               : llvm::SmallVector<CSlot, 2>();
    if (!Told || Sources.size() != 1 || Sources.front().Element)
      return false;
    CSlot Member = Sources.front();
    llvm::append_range(Member.Path, Path);
    const CSlotValues Values = valuesAt(Member, Copied);
    if (!Values.Told || Values.Values.empty())
      return false;
    for (const CSlotValue &Each : Values.Values) {
      Facts->Definitions.push_back(
          {At->Block, At->Index, Each, true, Weak, At->Target, true});
      Weak = true;
    }
    return true;
  };
  // What \p Write, the write \p iWrite or, \p Composed, what its call
  // leaves in the members of what it is called on (summaryWrites), gives
  // the slot; \p Infeasible of a call that ends a way the function doesn't
  // take.
  auto Apply = [&](const CWrite &Write, unsigned iWrite, bool Composed,
                   bool Infeasible) {
    At = &Write;
    // Of the object of the function or what a parameter refers to, a
    // write of another such place that can be the same object (mayAlias),
    // or of a place that can't be told that can be it (untoldWrites), gives
    // it what it writes, only maybe: of what holds it, that part of what it
    // writes (projected).
    if (Slot.Place != EPlace::Variable && Write.Kind != CWrite::EKind::Closure &&
        !Write.Object && Write.Target) {
      const CTargets All =
          Composed ? composedTargets(iWrite, Write)
                   : sharedFlowCache().summaries(Context).Targets.get(
                         {Function, iWrite});
      // Of a member function called on what it designates, only a member it
      // can change (CWrite::Members) of the object that holds the slot.
      std::optional<CAliasAt> Alias;
      for (const CSlot &Other : All.Slots) {
        if (Alias)
          break;
        Alias = mayAlias(Slot, Other, Function);
        if (Alias && Alias->Exact && Write.MembersTold && Write.Path.empty() &&
            Alias->Depth < Slot.Path.size() &&
            !llvm::is_contained(Write.Members, Slot.Path[Alias->Depth]))
          Alias.reset();
      }
      // Of storage only code that can't be seen gives, a write of what a
      // caller gives where such code reaches it (unseenStorage).
      bool Unseen = Write.Unseen;
      if (!Alias && !All.Told) {
        Alias = untoldWrites(
            Slot, Write.Target,
            Write.ByPointer || Write.Kind == CWrite::EKind::Through, Write.Path,
            Function);
        Unseen = Unseen ||
                 (Alias && unseenStorage(Write.Target, Function->getASTContext()));
      }
      if (Alias) {
        CSlotValue Value =
            Write.Kind == CWrite::EKind::Assign ? Write.Value : CSlotValue{};
        if (!Alias->Whole)
          Value = {CSlotValue::EKind::Value,
                   Alias->Projected && Value.Kind == CSlotValue::EKind::Value &&
                           Value.Value
                       ? projected(Value.Value, llvm::ArrayRef(Slot.Path)
                                                    .drop_front(Alias->Depth))
                       : nullptr};
        Facts->Definitions.push_back(
            {Write.Block, Write.Index, Value, Value.Value != nullptr, true,
             Write.Target, false, Write.Kind != CWrite::EKind::Assign, false,
             false, Unseen});
      }
    }
    // Of a pointer, what it points to, as `*pValue` designates.
    const CTargetsOn Found =
        Composed ? composedOn(iWrite, Write, Slot.Variable, Slot.Place)
                 : targetsOn(iWrite, Slot.Variable, Slot.Place);
    for (const CSlot &Target : Found.Slots) {
      // Of another member of a union it is in, what it holds after can't be
      // told, only maybe.
      if (sharesUnionStorage(Slot.Path, Target.Path)) {
        Facts->Definitions.push_back({Write.Block, Write.Index, {}, false, true,
                                      Write.Target, false, true, false,
                                      false});
        continue;
      }
      if (!startsWith(Slot.Path, Target.Path))
        continue;
      // An element of another constant index.
      if (Slot.Element && Target.Element && Slot.Index && Target.Index &&
          Slot.Path == Target.Path && *Slot.Index != *Target.Index)
        continue;
      // A member the member function called on it can't change.
      if (Write.MembersTold && !Target.Element &&
          Slot.Path.size() > Target.Path.size() &&
          !llvm::is_contained(Write.Members, Slot.Path[Target.Path.size()]))
        continue;
      // Of the object of the function, a change whose value isn't told
      // replaces what it held too: what it holds after it is told by what
      // changes it, a member function called on it or what it is given
      // to, or other code that runs on it (CThisDefinition).
      const bool Strong =
          ((Write.Kind == CWrite::EKind::Assign && !Write.Maybe) ||
           (Write.Kind == CWrite::EKind::Closure && Write.Definite) ||
           (Slot.Place == EPlace::This &&
            (Write.Kind == CWrite::EKind::Opaque ||
             Write.Kind == CWrite::EKind::Through))) &&
          Found.Only && Found.Told &&
          (!Target.Element ||
           (Slot.Element && Slot.Path == Target.Path &&
            ownedElement(Target, Function) && Target.Index && Slot.Index &&
            *Target.Index == *Slot.Index));
      CSlotValue Value = Write.Value;
      bool Known = (Write.Kind == CWrite::EKind::Assign ||
                    Write.Kind == CWrite::EKind::Closure) &&
                   Value.Value;
      // Of a member of what a member it assigns points to, what that
      // points to after it can't be told.
      if (crossesPointer(Slot.Path, Target.Path.size())) {
        Value = {};
        Known = false;
      } else if (Slot.Path.size() > Target.Path.size()) {
        const llvm::ArrayRef<const FieldDecl *> Rest =
            llvm::ArrayRef(Slot.Path).drop_front(Target.Path.size());
        const Expr *Made = Known && Value.Kind == CSlotValue::EKind::Value
                               ? Value.Value
                               : nullptr;
        // Of a record stored there as it is where it is read, a swap's
        // other, that member of it.
        const Expr *Copied =
            Made ? Made
                 : Known && Value.Kind == CSlotValue::EKind::Stored
                     ? Value.Value
                     : nullptr;
        Value.Value = Made ? projected(Made, Rest) : nullptr;
        Value.Kind = CSlotValue::EKind::Value;
        Known = Value.Value != nullptr;
        if (!Known && CopiedMembers(Copied, Rest, !Strong))
          continue;
      }
      // Of a call worked out first in a recursion, of what its summary
      // tells, the object of the function or a pointer it sets: a way through
      // it isn't one the function takes, which ends what reaches there.
      const bool Unreached = Infeasible && (Slot.Place == EPlace::This ||
                                            Write.Sets || Write.Referent);
      Facts->Definitions.push_back(
          {Write.Block, Write.Index, Value, Known, !Strong && !Unreached,
           Write.Target, Slot.Path.size() > Target.Path.size(),
           Write.Kind == CWrite::EKind::Opaque ||
               Write.Kind == CWrite::EKind::Through ||
               (Write.Kind == CWrite::EKind::Closure && !Write.Known),
           Unreached,
           Target.Element && !Slot.Element &&
               Target.Path.size() == Slot.Path.size(),
           Write.Unseen});
    }
  };
  // What the call of the element being gone through leaves (summaryWrites),
  // given after all of the call's own changes, of each of what it is given
  // and of what it is called on, and before what a closure it runs assigns
  // there: a change of an argument can't come after what the summary of
  // another tells, which tells it too.
  llvm::SmallVector<std::pair<unsigned, CWrite>, 4> Leaving;
  auto Leave = [&] {
    for (const auto &[iLeft, Each] : Leaving)
      Apply(Each, iLeft, true, false);
    Leaving.clear();
  };
  for (unsigned iWrite = 0; iWrite < Writes.size(); ++iWrite) {
    const CWrite &Write = Writes[iWrite];
    if (!Leaving.empty() &&
        (Write.Kind == CWrite::EKind::Closure ||
         Writes[Leaving.front().first].Block != Write.Block ||
         Writes[Leaving.front().first].Index != Write.Index))
      Leave();
    At = &Write;
    switch (Write.Kind) {
    case CWrite::EKind::Declare: {
      if (Write.Variable != Slot.Variable)
        break;
      CSlotValue Value = Write.Value;
      bool Known = Write.Known;
      if (!Slot.Path.empty()) {
        const Expr *Made = Value.Value;
        Value.Value = Made ? projected(Made, Slot.Path) : nullptr;
        Value.Kind = CSlotValue::EKind::Value;
        Known = Value.Value != nullptr;
        if (!Known && CopiedMembers(Made, Slot.Path, false))
          break;
      }
      Facts->Definitions.push_back({Write.Block, Write.Index, Value, Known,
                                    false, nullptr, !Slot.Path.empty()});
      break;
    }
    case CWrite::EKind::Assign:
    case CWrite::EKind::Opaque:
    case CWrite::EKind::Through:
    case CWrite::EKind::Closure: {
      // Of a call whose summary is asked, what it leaves in the members of
      // what it is called on after its own change, where it can write the
      // slot (summaryWrites); of one worked out first in a recursion, the
      // way through it isn't taken.
      bool Top = false;
      bool Replaces = false;
      const llvm::SmallVector<CWrite, 2> Composed =
          writesSlot(iWrite, Slot)
              ? summaryWrites(Write, &Top, &Replaces, reachedByUnseen(Slot))
              : llvm::SmallVector<CWrite, 2>();
      if (!Replaces)
        Apply(Write, iWrite, false, Top);
      for (const CWrite &Each : Composed)
        Leaving.push_back({iWrite, Each});
      break;
    }
    }
  }
  Leave();
  return Result;
}

// What the write \p iWrite designates of \p Variable (makeTargets), which is
// all the facts of its slots use of it.
CTargetsOn CLocalFlow::targetsOn(unsigned iWrite, const VarDecl *Variable,
                                 EPlace Place) const {
  return sharedFlowCache().summaries(Context).Targets.observe(
      {Function, iWrite}, [Variable, Place](const CTargets &Of) {
        CTargetsOn On;
        for (const CSlot &Each : Of.Slots)
          if (Each.Variable == Variable && Each.Place == Place)
            On.Slots.push_back(Each);
        if (!On.Slots.empty()) {
          On.Only = Of.Slots.size() == 1;
          On.Told = Of.Told;
        }
        return On;
      });
}

// What the call of \p Write, of a member function whose summary tells it
// (CWrite::Summarized), leaves in the members of what it is called on, as
// assignments after the call's own change, one of each value of each member
// it sets on every way through it, the first replacing what it held, of each
// value in the terms of the call; none of a member a value of which can't be
// told there. \p Top tells whether the summary is worked out first in a
// recursion, so that the way through the call isn't taken. Of a slot that
// code that can't be seen can't reach, \p Reached false, not what the
// callee's writes of storage only such code gives leave (CFlowFact::Reached).
llvm::SmallVector<CLocalFlow::CWrite, 2>
CLocalFlow::summaryWrites(const CWrite &Write, bool *Top, bool *Replaces,
                          bool Reached) const {
  llvm::SmallVector<CWrite, 2> Composed;
  if (Replaces)
    *Replaces = false;
  if (!Write.Summarized || !Write.Call || !Write.Target)
    return Composed;
  // Of what a parameter refers to, its summary (ParameterSummaries), whose
  // facts are of the members of what it refers to as of the object's.
  CFlowSummaries &Summaries = sharedFlowCache().summaries(Context);
  const CFunctionSummary &Summary =
      Write.Referent
          ? Summaries.ParameterSummaries.get(
                {Write.Summarized, Write.Referent - 1})
          : Summaries.FunctionSummaries.get(Write.Summarized);
  if (Top)
    *Top = Summary.Top;
  // Of a pointer it is given to set (CWrite::Sets), each value it sets it
  // to, only maybe of one it doesn't set on every way, which replaces the
  // call's own change where each value is told.
  // Each value of \p Sources in the terms of the call that can be told, of
  // \p Unseen ones what the callee's writes of storage only unseen code
  // gives leave; false where one can't be told.
  auto ValuesOf = [&](llvm::ArrayRef<CFlowSource> Sources, bool Unseen,
                      llvm::SmallVectorImpl<std::pair<CSlotValue, bool>> &Values) {
    bool Told = true;
    for (const CFlowSource &Source : Sources) {
      const Expr *Given =
          Source.Kind == CFlowSource::EKind::Argument ||
                  Source.Kind == CFlowSource::EKind::ArgumentAddress
              ? argumentFor(Write.Call, Source.Index, Context)
              : Source.Named;
      if (!Given) {
        Told = false;
        continue;
      }
      const bool Address =
          Source.Kind == CFlowSource::EKind::ArgumentAddress ||
          Source.Kind == CFlowSource::EKind::StaticAddress;
      Values.push_back({{Address ? CSlotValue::EKind::Address
                                 : CSlotValue::EKind::Value,
                         Given, Address && Source.Part},
                        Unseen});
    }
    return Told;
  };
  // Of a pointer it is given to set (CWrite::Sets), each value it sets it
  // to, only maybe of one it doesn't set on every way, which replaces the
  // call's own change where each value is told.
  if (Write.Sets) {
    for (const CFlowFact &Fact : Summary.Parameters) {
      if (Fact.Target + 1 != Write.Sets)
        continue;
      llvm::SmallVector<std::pair<CSlotValue, bool>, 1> Values;
      bool Told = ValuesOf(Fact.Sources, false, Values);
      if (Reached)
        Told = ValuesOf(Fact.Reached, true, Values) && Told;
      for (const auto &[Value, Unseen] : Values) {
        CWrite Assign{CWrite::EKind::Assign, Write.Block, Write.Index, nullptr,
                      Write.Target, Value};
        Assign.ByPointer = Write.Kind == CWrite::EKind::Through;
        Assign.Maybe = !Fact.EveryPath || &Value != &Values.front().first;
        Assign.Unseen = Unseen;
        Composed.push_back(std::move(Assign));
      }
      if (Replaces)
        *Replaces = !Values.empty() && Told && Fact.Reason == EUntold::None &&
                    (!Reached || Fact.ReachedReason == EUntold::None);
    }
    return Composed;
  }
  for (const CFlowFact &Fact :
       Write.Referent ? llvm::ArrayRef<CFlowFact>(Summary.Parameters)
                      : llvm::ArrayRef<CFlowFact>(Summary.Leaves)) {
    if (!Fact.EveryPath || (Reached && Fact.ReachedReason != EUntold::None))
      continue;
    llvm::SmallVector<std::pair<CSlotValue, bool>, 1> Values;
    if (!ValuesOf(Fact.Sources, false, Values) ||
        (Reached && !ValuesOf(Fact.Reached, true, Values)))
      continue;
    for (const auto &[Value, Unseen] : Values) {
      CWrite Assign{CWrite::EKind::Assign, Write.Block, Write.Index, nullptr,
                    Write.Target, Value};
      Assign.ByPointer = !Write.Target->isGLValue();
      Assign.MembersTold = true;
      Assign.Members.push_back(Fact.Path.front());
      Assign.Path.assign(Fact.Path.begin(), Fact.Path.end());
      Assign.PathElement = Fact.Element;
      Assign.PathIndex = Fact.Index;
      Assign.Maybe = &Value != &Values.front().first;
      Assign.Unseen = Unseen;
      Composed.push_back(std::move(Assign));
    }
  }
  return Composed;
}

// What \p Composed, one of what the call of the write \p iWrite leaves
// (summaryWrites), designates: the member it assigns of each place the call
// is on; of an element, which no member place is, none it can tell.
CTargets CLocalFlow::composedTargets(unsigned iWrite,
                                     const CWrite &Composed) const {
  CTargets Found =
      sharedFlowCache().summaries(Context).Targets.get({Function, iWrite});
  for (CSlot &Slot : Found.Slots) {
    Found.Told = Found.Told && !Slot.Element;
    llvm::append_range(Slot.Path, Composed.Path);
    Slot.Element = Slot.Element || Composed.PathElement;
    Slot.Index = Composed.PathElement ? Composed.PathIndex : Slot.Index;
  }
  return Found;
}

// What \p Composed, one of what the call of the write \p iWrite leaves
// (summaryWrites), designates of \p Variable, as targetsOn tells of a write.
CTargetsOn CLocalFlow::composedOn(unsigned iWrite, const CWrite &Composed,
                                  const VarDecl *Variable, EPlace Place) const {
  return sharedFlowCache().summaries(Context).Targets.observe(
      {Function, iWrite},
      [Variable, Place, Path = Composed.Path, Element = Composed.PathElement,
       ElementIndex = Composed.PathIndex](const CTargets &Of) {
        CTargetsOn On;
        bool Told = Of.Told;
        for (CSlot Each : Of.Slots) {
          Told = Told && !Each.Element;
          if (Each.Variable != Variable || Each.Place != Place)
            continue;
          llvm::append_range(Each.Path, Path);
          Each.Element = Each.Element || Element;
          Each.Index = Element ? ElementIndex : Each.Index;
          On.Slots.push_back(std::move(Each));
        }
        if (!On.Slots.empty()) {
          On.Only = Of.Slots.size() == 1;
          On.Told = Told;
        }
        return On;
      });
}

// Whether the call of the write \p iWrite, of a member function whose
// summary tells what it leaves (CWrite::Summarized), can write \p Slot or a
// part of it: it is called on what holds it, or, of a place that isn't a
// local slot, on another such place, which can be the same object
// (mayAlias).
bool CLocalFlow::writesSlot(unsigned iWrite, const CSlot &Slot) const {
  if (!Writes[iWrite].Summarized)
    return false;
  const CTargetsOn Found = targetsOn(iWrite, Slot.Variable, Slot.Place);
  if (llvm::any_of(Found.Slots, [&](const CSlot &Target) {
        return startsWith(Slot.Path, Target.Path);
      }))
    return true;
  return Slot.Place != EPlace::Variable &&
         llvm::any_of(
             sharedFlowCache().summaries(Context).Targets.get({Function, iWrite})
                 .Slots,
             [](const CSlot &Other) { return Other.Place != EPlace::Variable; });
}

// What the write \p iWrite designates (designatedSlots), of a write through
// a pointer what it points to, of a closure's what it designates in the
// closure (closureTargets); not told when it can be what isn't a slot as
// well.
CTargets CLocalFlow::makeTargets(unsigned iWrite) const {
  CTargets Found;
  if (iWrite >= Writes.size())
    return Found;
  const CWrite &Write = Writes[iWrite];
  // What can change the object of the function as a whole, a suspension.
  if (Write.Object) {
    Found.Slots.push_back({nullptr, {}, false, EPlace::This});
    return Found;
  }
  // Each place, also the object of the function and what a parameter refers
  // to (designatedPlaces), of a write through a pointer what it points to.
  switch (Write.Kind) {
  case CWrite::EKind::Closure:
    Found.Slots = closureTargets(Write.Target, Found.Told);
    break;
  case CWrite::EKind::Through:
  case CWrite::EKind::Assign:
  case CWrite::EKind::Opaque:
  case CWrite::EKind::Declare:
    Found.Slots = designatedPlaces(Write.Target, Context, &Found.Told);
    break;
  }
  // Of a member it assigns, that member of each slot it designates; of an
  // element, which no member slot is, none it can tell.
  if (!Write.Path.empty())
    for (CSlot &Slot : Found.Slots) {
      Found.Told = Found.Told && !Slot.Element;
      llvm::append_range(Slot.Path, Write.Path);
    }
  return Found;
}

// The parts of \p Slot the writes that can run assign on their own
// (localSlotParts), worked out once (makeParts).
llvm::SmallVector<CSlot, 2> CLocalFlow::writtenParts(const CSlot &Slot) const {
  if (!Graph || !Ready || Slot.Element)
    return {};
  CFlowSummaries &Summaries = sharedFlowCache().summaries(Context);
  const CSlotKey Key{Function, Slot.Variable, Slot.Path, Slot.Place,
                     Slot.Element ? Slot.Index : std::nullopt};
  if (!Summaries.Parts.contains(Key))
    Summaries.PartKeys[Function].push_back(Key);
  return Summaries.Parts.get(Key);
}

// The parts of \p Slot the writes that can run assign on their own, each
// once, by the members to them.
llvm::SmallVector<CSlot, 2> CLocalFlow::makeParts(const CSlot &Slot) const {
  llvm::SmallVector<CSlot, 2> Parts;
  if (!Graph || !Ready || Slot.Element)
    return Parts;
  for (unsigned iWrite = 0; iWrite < Writes.size(); ++iWrite) {
    if (Writes[iWrite].Kind == CWrite::EKind::Declare)
      continue;
    // Also of what a call leaves in the members of what it is called on
    // (summaryWrites).
    auto Add = [&](const CTargetsOn &Found) {
      for (const CSlot &Target : Found.Slots)
        if (Target.Path.size() > Slot.Path.size() &&
            startsWith(Target.Path, Slot.Path) &&
            !crossesPointer(Target.Path, Slot.Path.size()) &&
            !(Target.Element &&
              Target.Path.back()->getType()->isPointerType()) &&
            llvm::none_of(Parts, [&](const CSlot &Part) {
              return Part.Path == Target.Path;
            }))
          Parts.push_back({Target.Variable, Target.Path, false, Target.Place});
    };
    const CTargetsOn Found = targetsOn(iWrite, Slot.Variable, Slot.Place);
    Add(Found);
    if (Found.Slots.empty() || !Writes[iWrite].leavesParts())
      continue;
    for (const CWrite &Composed : summaryWrites(Writes[iWrite]))
      Add(composedOn(iWrite, Composed, Slot.Variable, Slot.Place));
  }
  return Parts;
}

// Whether what the whole of what \p Part is in is given can be what the part
// holds where \p At is, not only what the part is assigned on its own, as its
// definitions that reach there tell (CDefinition::Whole); of one that can't
// be told, it can.
bool CLocalFlow::wholeReaches(const CSlot &Part, const Stmt *At) const {
  if (!Graph || !Ready)
    return true;
  return observeFacts(Part, [this, At](const CFacts &Facts) {
    const auto Position = positionOf(At);
    if (!Facts.Told || !Position || inClosureBody(At))
      return true;
    flow(Facts);
    const llvm::BitVector Reaching =
        reachingAt(Facts, Position->first, Position->second);
    return llvm::any_of(Reaching.set_bits(), [&](unsigned iDefinition) {
      return Facts.Definitions[iDefinition].Whole ||
             !Facts.Definitions[iDefinition].Known;
    });
  });
}

const CallExpr *runningCallOf(const CClosureUses &Uses, unsigned &iGiven,
                              bool &bRunning, ASTContext &Context) {
  bRunning = false;
  const CClosureSite *Given =
      Uses.Calls.size() + Uses.Escapes.size() == 1 && Uses.Locals.empty()
          ? (Uses.Calls.empty() ? &Uses.Escapes.front() : &Uses.Calls.front())
          : nullptr;
  const auto *Call =
      Given ? dyn_cast_or_null<CallExpr>(Given->Use.Parent) : nullptr;
  const FunctionDecl *Callee = Call ? Call->getDirectCallee() : nullptr;
  if (!Callee || !runsGivenFunction(Callee) || !Given->Use.Effect ||
      Given->Use.Effect->Into.Kind != EDestination::Parameter)
    return nullptr;
  bRunning = true;
  iGiven = Given->Use.Effect->Into.Index + argumentShift(Call, Callee);
  // Its first callable argument: none before it is a closure, another
  // function object or a function.
  for (unsigned iBefore = 0; iBefore < iGiven && iBefore < Call->getNumArgs();
       ++iBefore) {
    const QualType Type = Call->getArg(iBefore)->getType();
    const auto *Record = Type->getAsCXXRecordDecl();
    if (Type->isFunctionPointerType() || Type->isFunctionType() ||
        (Record && Record->hasDefinition() &&
         !Record->lookup(Context.DeclarationNames.getCXXOperatorName(OO_Call))
              .empty()))
      return nullptr;
  }
  return Call;
}

// Whether the call operator of \p Closure can run again after it ran: not
// of one only given to a function that runs it once (runningCallOf).
static bool runsAgain(const LambdaExpr *Closure, ASTContext &Context) {
  const auto *Function = dyn_cast_or_null<FunctionDecl>(
      Closure->getLambdaClass()->getDeclContext());
  if (!Function || !Function->getBody())
    return true;
  bool bRunning = false;
  unsigned iGiven = 0;
  return !runningCallOf(sharedFlowCache().closureUses(
                            Function, Function->getBody(), Closure, Context),
                        iGiven, bRunning, Context);
}

void CLocalFlow::flow(const CFacts &Facts) const {
  if (Facts.Flowed || !Graph)
    return;
  Facts.Flowed = true;
  // Of a closure that can be called again, what it assigns, from a call
  // before; one a function that runs it once is given runs once.
  Facts.In = reachingIn(*Graph, Reachable, *FlowOrder, Facts.Definitions,
                        Closure && Closure->isMutable() &&
                            runsAgain(Closure, Context));
}

// Indexes the definitions of \p Facts once (CFacts::InBlock, InExpression), so
// that what reaches an element costs the definitions of its block and its
// expression, not all of them.
void CLocalFlow::indexFacts(const CFacts &Facts) const {
  if (Facts.Indexed)
    return;
  Facts.Indexed = true;
  for (unsigned iDefinition = 0; iDefinition < Facts.Definitions.size();
       ++iDefinition) {
    const CDefinition &Each = Facts.Definitions[iDefinition];
    Facts.InBlock.push_back(iDefinition);
    if (Each.Target && !Each.Infeasible)
      Facts.InExpression.push_back({expressionOf(Each.Target), iDefinition});
  }
  llvm::stable_sort(Facts.InBlock, [&](unsigned Left, unsigned Right) {
    return std::make_pair(Facts.Definitions[Left].Block,
                          Facts.Definitions[Left].Index) <
           std::make_pair(Facts.Definitions[Right].Block,
                          Facts.Definitions[Right].Index);
  });
  llvm::stable_sort(Facts.InExpression, [](const auto &Left, const auto &Right) {
    return std::less<const Stmt *>()(Left.first, Right.first);
  });
}

// Whether the one definition of \p Facts, which replaces what the slot holds,
// is all that reaches \p Position, a reachable element: before it in its
// block, or in a block that dominates it, so each way there passes through
// it and no other definition can follow. The flow isn't needed then, which
// would cost the size of the function for each slot asked.
bool CLocalFlow::soleReaches(const CFacts &Facts,
                             std::pair<unsigned, unsigned> Position) const {
  if (!Graph || Facts.Definitions.size() != 1 ||
      Facts.Definitions.front().Weak || Facts.Definitions.front().Infeasible ||
      Position.first >= Reachable.size() || !Reachable.test(Position.first))
    return false;
  const CDefinition &Only = Facts.Definitions.front();
  if (Only.Block == Position.first)
    return Only.Index != ~0u && Only.Index < Position.second;
  if (!Dominators) {
    // The tree reads the graph only.
    Dominators = std::make_shared<CDominators>();
    Dominators->Tree.buildDominatorTree(const_cast<CFG *>(Graph));
    Dominators->Blocks.resize(Graph->getNumBlockIDs());
    for (const CFGBlock *Block : *Graph)
      if (Block)
        Dominators->Blocks[Block->getBlockID()] = Block;
  }
  const auto &Blocks = Dominators->Blocks;
  return Only.Block < Blocks.size() && Blocks[Only.Block] &&
         Blocks[Position.first] &&
         Dominators->Tree.dominates(Blocks[Only.Block], Blocks[Position.first]);
}

// Each value \p Facts give their slot that is known.
static CSlotValues assignmentsIn(const CFacts &Facts) {
  CSlotValues Values;
  for (const CDefinition &Each : Facts.Definitions)
    if (Each.Known && !llvm::is_contained(Values.Values, Each.Value))
      Values.Values.push_back(Each.Value);
  return Values;
}

CSlotValues CLocalFlow::assignments(const CSlot &Slot) const {
  return observeFacts(Slot, &assignmentsIn);
}

// Whether a definition of \p Slot isn't at the entry of the flow, or its
// address escapes; also of a flow that isn't made.
bool CLocalFlow::changedIn(const CSlot &Slot) const {
  if (!Graph || !Ready)
    return true;
  return observeFacts(Slot, [](const CFacts &Facts) {
    return !Facts.Told ||
           llvm::any_of(Facts.Definitions, [](const CDefinition &Each) {
             return Each.Index != ~0u;
           });
  });
}

CSlotValues CLocalFlow::valuesAt(const CSlot &Slot, const Stmt *At,
                                 bool *Final) const {
  if (Final)
    *Final = false;
  // Not told of a flow that isn't made, its approximation while it is
  // (CFlowSummaries::LocalFlows).
  if (!Graph || !Ready) {
    CSlotValues Values;
    Values.Told = false;
    return Values;
  }
  CSlotValues Values = observeFacts(
      Slot, [this, At](const CFacts &Facts) { return valuesIn(Facts, At); });
  // Final after the call: observed final, or worked out to the end in it.
  if (Final)
    *Final = factsFinal(Slot);
  return Values;
}

// What the slot of \p Facts holds where \p At is, by the definitions that
// reach there (reachingRead); read in the body of a closure made here, which
// runs where it is called, each value it is given.
CSlotValues CLocalFlow::valuesIn(const CFacts &Facts, const Stmt *At) const {
  CSlotValues Values;
  const std::optional<llvm::BitVector> Reaching = reachingRead(Facts, At);
  if (!Reaching) {
    Values = assignmentsIn(Facts);
    Values.Told = false;
    return Values;
  }
  // Each value once, in order; past a few, by a set of those seen.
  llvm::DenseSet<std::pair<const Expr *, unsigned>> Seen;
  for (unsigned iDefinition : Reaching->set_bits()) {
    const CDefinition &Each = Facts.Definitions[iDefinition];
    if (!Each.Known) {
      Values.Told = false;
      continue;
    }
    const std::pair<const Expr *, unsigned> Key{
        Each.Value.Value,
        unsigned(Each.Value.Kind) * 2 + (Each.Value.Part ? 1 : 0)};
    if (Values.Values.size() < 16) {
      if (llvm::is_contained(Values.Values, Each.Value))
        continue;
    } else {
      if (Seen.empty())
        for (const CSlotValue &Kept : Values.Values)
          Seen.insert({Kept.Value,
                       unsigned(Kept.Kind) * 2 + (Kept.Part ? 1 : 0)});
      if (!Seen.insert(Key).second)
        continue;
    }
    Values.Values.push_back(Each.Value);
  }
  return Values;
}

// The definitions of the slot of \p Facts that reach where \p At is; none
// when what reaches can't be told: of a slot whose address escapes, read
// where the flow has no position, or in the body of a closure made here.
std::optional<llvm::BitVector>
CLocalFlow::reachingRead(const CFacts &Facts, const Stmt *At) const {
  const auto Position = positionOf(At);
  if (!Facts.Told || !Position || inClosureBody(At))
    return std::nullopt;
  llvm::BitVector Reaching;
  if (soleReaches(Facts, *Position))
    Reaching = llvm::BitVector(1, true);
  else {
    flow(Facts);
    Reaching = reachingAt(Facts, Position->first, Position->second);
  }
  // Of a definition in an operand whose order to the read's is open
  // (unordered), in any block of the expression, `fg_Both((g_pValue =
  // pValue), bChoose ? (pValue = &t_Value) : nullptr)`: it, which can run
  // before the read, and what reaches it, before which the read can run.
  indexFacts(Facts);
  const Stmt *Root = expressionOf(At);
  const auto [First, Last] = std::equal_range(
      Facts.InExpression.begin(), Facts.InExpression.end(),
      std::make_pair(Root, 0u), [](const auto &Left, const auto &Right) {
        return std::less<const Stmt *>()(Left.first, Right.first);
      });
  for (const auto &[Expression, iDefinition] : llvm::make_range(First, Last)) {
    const CDefinition &Each = Facts.Definitions[iDefinition];
    if (!unordered(Each.Target, At))
      continue;
    flow(Facts);
    Reaching.set(iDefinition);
    Reaching |= reachingAt(Facts, Each.Block, Each.Index);
  }
  return Reaching;
}

CSlotReaching CLocalFlow::definitionsAt(const CSlot &Slot,
                                        const Stmt *At) const {
  if (!Graph || !Ready)
    return {{}, false};
  return observeFacts(Slot, [this, At](const CFacts &Facts) {
    CSlotReaching Result;
    const std::optional<llvm::BitVector> Reaching = reachingRead(Facts, At);
    if (!Reaching)
      return CSlotReaching{{}, false};
    for (unsigned iDefinition : Reaching->set_bits()) {
      const CDefinition &Each = Facts.Definitions[iDefinition];
      const CSlotDefinition Definition{Each.Value, Each.Known, Each.Changed,
                                       Each.Changed ? Each.Target : nullptr};
      if (!llvm::is_contained(Result.Definitions, Definition))
        Result.Definitions.push_back(Definition);
    }
    return Result;
  });
}

// Whether each way to \p To passes \p From after the last definition of
// \p Slot before it: the definitions, each replacing what reaches, and an
// event at \p From, which replaces them too, reach the elements by the one
// may analysis (reachingIn) from an event at the entry; only the one at \p
// From reaches \p To.
bool CLocalFlow::unchangedBetween(const CSlot &Slot, const Stmt *From,
                                  const Stmt *To) const {
  if (!Graph || !Ready)
    return false;
  const auto Start = positionOf(From);
  const auto End = positionOf(To);
  if (!Start || !End || inClosureBody(From) || inClosureBody(To))
    return false;
  if (*Start == *End)
    return true;
  // By value: what is observed of a slot being worked out is observed
  // again when it is (TCSummaries::observe), after this returns.
  return observeFacts(Slot, [this, StartAt = *Start,
                             EndAt = *End](const CFacts &Facts) {
    if (!Facts.Told)
      return false;
    struct CEvent {
      unsigned Block;
      unsigned Index;
      bool Weak = false;
    };
    llvm::SmallVector<CEvent, 8> Events{
        {Graph->getEntry().getBlockID(), ~0u}};
    for (const CDefinition &Each : Facts.Definitions)
      Events.push_back({Each.Block, Each.Index});
    Events.push_back({StartAt.first, StartAt.second});
    const std::vector<llvm::BitVector> In =
        reachingIn(*Graph, Reachable, *FlowOrder, Events, false);
    const llvm::BitVector Reaching =
        reachingBefore(In, Events, EndAt.first, EndAt.second);
    return Reaching.count() == 1 && Reaching.test(Events.size() - 1);
  });
}

namespace {
// An event of a flow over a CFG (reachingIn): at the element Index of the
// block Block, Weak when it only adds to what reaches.
struct CFlowMark {
  unsigned Block = 0;
  unsigned Index = 0;
  bool Weak = false;
};
} // namespace

// The elements of the blocks of \p Graph that \p Reachable has that \p Picks
// picks (eventOf), each a mark that replaces what reaches.
static llvm::SmallVector<CFlowMark, 8>
pickedElements(const CFG &Graph, const llvm::BitVector &Reachable,
               llvm::function_ref<bool(const CFlowEvent &)> Picks) {
  llvm::SmallVector<CFlowMark, 8> Marks;
  for (const CFGBlock *Block : Graph) {
    if (!Block || !Reachable.test(Block->getBlockID()))
      continue;
    unsigned Here = 0;
    for (const CFGElement &Element : *Block) {
      const unsigned At = Here++;
      if (Picks(eventOf(Element)))
        Marks.push_back({Block->getBlockID(), At});
    }
  }
  return Marks;
}

// The values of the definitions of \p Slot that reach each element that
// destroys its variable where its scope ends; not told without such an
// element or where what reaches can't be told.
CSlotValues CLocalFlow::valuesAtEnd(const CSlot &Slot, bool *Final) const {
  if (Final)
    *Final = false;
  CSlotValues Values;
  Values.Told = false;
  if (!Graph || !Ready || !Slot.Variable)
    return Values;
  const llvm::SmallVector<CFlowMark, 8> Ends =
      pickedElements(*Graph, Reachable, [&](const CFlowEvent &Event) {
        return Event.Kind == EEvent::AutomaticDestructor &&
               Event.Variable == Slot.Variable;
      });
  if (Ends.empty())
    return Values;
  // Final after the call, as valuesAt.
  const llvm::scope_exit Finished([&] {
    if (Final)
      *Final = factsFinal(Slot);
  });
  return observeFacts(Slot, [this, Ends](const CFacts &Facts) {
    CSlotValues Result;
    Result.Told = Facts.Told;
    if (!Facts.Told)
      return Result;
    flow(Facts);
    for (const CFlowMark &End : Ends) {
      // Named, as the loop goes through its bits after the full expression.
      const llvm::BitVector Reaching = reachingAt(Facts, End.Block, End.Index);
      for (unsigned iDefinition : Reaching.set_bits()) {
        const CDefinition &Each = Facts.Definitions[iDefinition];
        if (!Each.Known)
          Result.Told = false;
        else if (!llvm::is_contained(Result.Values, Each.Value))
          Result.Values.push_back(Each.Value);
      }
    }
    return Result;
  });
}

// What a definition made at \p Index of \p Block is made by, of the events
// of \p Graph: the statement of that element; null of what the slot starts
// with or an element of another kind.
static const Stmt *elementStatement(const CFG &Graph, unsigned Block,
                                    unsigned Index) {
  if (Index == ~0u)
    return nullptr;
  for (const CFGBlock *Each : Graph)
    if (Each && Each->getBlockID() == Block && Index < Each->size())
      return eventOf((*Each)[Index]).Statement;
  return nullptr;
}

// The definitions of \p Slot made where the entry reaches past no stop, an
// event at the entry that each stop replaces, and that reach an element of
// \p Reached past no stop and no definition \p Ends picks, each a mark that
// replaces what reaches, the other definitions marks that add to it, by the
// one may analysis (reachingIn).
std::optional<llvm::SmallVector<CSlotDefinitionAt, 2>>
CLocalFlow::definitionsReaching(
    const CSlot &Slot, llvm::function_ref<bool(const CSlotDefinition &)> Ends,
    llvm::function_ref<bool(const CFlowEvent &)> Stops,
    llvm::function_ref<bool(const CFlowEvent &)> Reached) const {
  if (!Graph || !Ready)
    return std::nullopt;
  const llvm::SmallVector<CFlowMark, 8> StopMarks =
      pickedElements(*Graph, Reachable, Stops);
  const llvm::SmallVector<CFlowMark, 8> ReachedMarks =
      pickedElements(*Graph, Reachable, Reached);
  return readFacts(Slot, [&](const CFacts &Facts) {
    llvm::SmallVector<CSlotDefinitionAt, 2> Found;
    if (ReachedMarks.empty() || Facts.Definitions.empty())
      return Found;
    llvm::SmallVector<CFlowMark, 8> FromEntry{
        {Graph->getEntry().getBlockID(), ~0u}};
    FromEntry.append(StopMarks.begin(), StopMarks.end());
    const std::vector<llvm::BitVector> EntryIn =
        reachingIn(*Graph, Reachable, *FlowOrder, FromEntry, false);
    llvm::SmallVector<CFlowMark, 8> Marks;
    for (const CDefinition &Each : Facts.Definitions)
      Marks.push_back(
          {Each.Block, Each.Index,
           Each.Weak || !Ends({Each.Value, Each.Known, Each.Changed})});
    const unsigned nDefinitions = Marks.size();
    Marks.append(StopMarks.begin(), StopMarks.end());
    const std::vector<llvm::BitVector> In =
        reachingIn(*Graph, Reachable, *FlowOrder, Marks, false);
    llvm::BitVector Reaching(Marks.size());
    for (const CFlowMark &Each : ReachedMarks)
      Reaching |= reachingBefore(In, Marks, Each.Block, Each.Index);
    for (unsigned iDefinition : Reaching.set_bits()) {
      if (iDefinition >= nDefinitions)
        break;
      const CDefinition &Each = Facts.Definitions[iDefinition];
      if (Each.Infeasible ||
          (Each.Index != ~0u &&
           !reachingBefore(EntryIn, FromEntry, Each.Block, Each.Index)
                .test(0)))
        continue;
      Found.push_back({{Each.Value, Each.Known, Each.Changed},
                       elementStatement(*Graph, Each.Block, Each.Index),
                       Each.Target});
    }
    return Found;
  });
}

// What \p Slot holds where \p At is (valuesAt) and what it is given anywhere
// that can run after it, a block it reaches, later in its own, also its own
// again in a loop: what it can hold from there on.
CSlotValues CLocalFlow::valuesFrom(const CSlot &Slot, const Stmt *At) const {
  if (!Graph || !Ready) {
    CSlotValues Values;
    Values.Told = false;
    return Values;
  }
  return observeFacts(Slot, [this, At](const CFacts &Facts) {
    CSlotValues Values = valuesIn(Facts, At);
    const auto Position = positionOf(At);
    if (!Values.Told || !Position)
      return Values;
    llvm::BitVector Reaches(Graph->getNumBlockIDs());
    llvm::SmallVector<const CFGBlock *, 8> Work;
    for (const CFGBlock *Block : *Graph)
      if (Block && Block->getBlockID() == Position->first)
        Work.push_back(Block);
    while (!Work.empty())
      for (const CFGBlock::AdjacentBlock &Successor :
           Work.pop_back_val()->succs())
        if (const CFGBlock *To = Successor.getReachableBlock();
            To && !Reaches.test(To->getBlockID())) {
          Reaches.set(To->getBlockID());
          Work.push_back(To);
        }
    for (const CDefinition &Each : Facts.Definitions) {
      if (!Reaches.test(Each.Block) &&
          !(Each.Block == Position->first && Each.Index != ~0u &&
            Each.Index > Position->second))
        continue;
      if (!Each.Known)
        Values.Told = false;
      else if (!llvm::is_contained(Values.Values, Each.Value))
        Values.Values.push_back(Each.Value);
    }
    return Values;
  });
}

// The definitions of \p Slot, a member of the object of the function
// (EPlace::This), and the changes of each part of it written on its own
// (writtenParts), whose values aren't told: what changes what it holds.
llvm::SmallVector<CDefinition, 8>
CLocalFlow::objectChanges(const CSlot &Slot) const {
  llvm::SmallVector<CDefinition, 8> Changes = readFacts(
      Slot, [](const CFacts &Facts) {
        return llvm::SmallVector<CDefinition, 8>(Facts.Definitions.begin(),
                                                 Facts.Definitions.end());
      });
  for (const CSlot &Part : writtenParts(Slot))
    readFacts(Part, [&](const CFacts &Facts) {
      for (CDefinition Each : Facts.Definitions)
        if (Each.Index != ~0u && !Each.Whole) {
          Each.Value = {};
          Each.Known = false;
          Each.Changed = false;
          Each.Whole = true;
          Changes.push_back(Each);
        }
      return true;
    });
  return Changes;
}

// The definitions of \p Slot, a member of the object of the function, and
// the changes of its parts (objectChanges), that reach the element of \p
// At, or the exit where \p At is null, on the ways that don't end in what
// leaves by throwing or a call of a function that doesn't return, by the one
// may analysis (reachingIn): what it holds where the function is entered
// (Entry), a value assigned to it as a whole, or a change whose value isn't
// told, Called when it is a call that is given the object or is called on
// it. None where the flow isn't made or \p At isn't an element of it.
std::optional<llvm::SmallVector<CDefinition, 2>>
CLocalFlow::objectReaching(const CSlot &Slot, const Stmt *At) const {
  if (!Graph || !Ready || !FlowOrder)
    return std::nullopt;
  const llvm::SmallVector<CDefinition, 8> Changes = objectChanges(Slot);
  llvm::SmallVector<CFlowMark, 8> Marks;
  for (const CDefinition &Each : Changes)
    Marks.push_back({Each.Block, Each.Index, Each.Weak});
  // Where a way leaves other than by a return, the same for each slot.
  if (!Leaving) {
    Leaving.emplace();
    for (const CFlowMark &End :
         pickedElements(*Graph, Reachable, [&](const CFlowEvent &Event) {
           if (Event.Kind != EEvent::Statement || !Event.Statement)
             return false;
           const CEffects &Of = effectsOf(Event.Statement, Context);
           return (Of.bLeaves && Of.Statement != EStatement::Return &&
                   Of.Statement != EStatement::Coreturn) ||
                  (Of.Callee && Of.Callee->isNoReturn());
         }))
      Leaving->push_back({End.Block, End.Index});
  }
  for (const auto &[Block, Element] : *Leaving)
    Marks.push_back({Block, Element});
  llvm::SmallVector<CFlowMark, 8> Places;
  if (At) {
    Places = pickedElements(*Graph, Reachable, [&](const CFlowEvent &Event) {
      return Event.Kind == EEvent::Statement && Event.Statement == At;
    });
    if (Places.empty())
      return std::nullopt;
  } else
    Places.push_back({Graph->getExit().getBlockID(), 0});
  const std::vector<llvm::BitVector> In =
      reachingIn(*Graph, Reachable, *FlowOrder, Marks, false);
  llvm::BitVector Reaching(Marks.size());
  for (const CFlowMark &Place : Places)
    Reaching |= reachingBefore(In, Marks, Place.Block, Place.Index);
  llvm::SmallVector<CDefinition, 2> Reached;
  for (unsigned Bit : Reaching.set_bits())
    if (Bit < Changes.size() && !Changes[Bit].Infeasible)
      Reached.push_back(Changes[Bit]);
  return Reached;
}

// The definitions of \p Slot, a member of the object of the function, and
// the changes of its parts (objectChanges), that reach the element of \p
// At, or the exit where \p At is null (objectReaching): what it holds where
// the function is entered (Entry), a value assigned to it as a whole, or a
// change whose value isn't told, Called when it is a call that is given the
// object or is called on it. None where the flow isn't made or \p At isn't
// an element of it.
std::optional<llvm::SmallVector<CThisDefinition, 2>>
CLocalFlow::objectDefinitions(const CSlot &Slot, const Stmt *At) const {
  const std::optional<llvm::SmallVector<CDefinition, 2>> Reaching =
      objectReaching(Slot, At);
  if (!Reaching)
    return std::nullopt;
  llvm::SmallVector<CThisDefinition, 2> Reached;
  for (const CDefinition &Each : *Reaching) {
    CThisDefinition Definition;
    if (Each.Index == ~0u)
      Definition.Entry = true;
    else if (Each.Changed) {
      // A call given the object as a whole, or called on it.
      bool Told = true;
      if (Each.Target)
        for (const CSlot &Place :
             designatedPlaces(Each.Target, Context, &Told))
          if (Place.Place == EPlace::This && Place.Path.empty()) {
            Definition.Called = true;
            Definition.Call = elementStatement(*Graph, Each.Block, Each.Index);
          }
    } else if (Each.Known && !Each.Whole &&
               Each.Value.Kind == CSlotValue::EKind::Value)
      Definition.Value = Each.Value.Value;
    Reached.push_back(Definition);
  }
  return Reached;
}

// The level at which a location of \p Type holds what it refers to: of a
// pointer, a record or an array what it holds, else itself.
static unsigned heldLevelOfType(QualType Type) {
  Type = Type.getNonReferenceType();
  return Type->isPointerType() || Type->isRecordType() || Type->isArrayType()
             ? 1
             : 0;
}

static CReturnedRoots valueRoots(
    const FunctionDecl *Definition,
    llvm::ArrayRef<std::pair<const Expr *, unsigned>> Values,
    ASTContext &Context, bool UntilOther = false);

/// How many values a fact of a function summary keeps (CFlowFact::Sources)
/// before what it holds can't be told, reason Budget.
static constexpr size_t MaxFactSources = 32;

// What reaches the exit of \p Place, a place of the flow that isn't a local
// slot, in the terms of the caller (summarize): of each definition that
// reaches the exit (objectReaching), its value by where its way of
// provenance ends (valueRoots).
CFlowFact CLocalFlow::factOf(const CSlot &Place) const {
  CFlowFact Fact;
  Fact.Path = Place.Path;
  Fact.Element = Place.Element;
  Fact.Index = Place.Element ? Place.Index : std::nullopt;
  const std::optional<llvm::SmallVector<CDefinition, 2>> Reaching =
      objectReaching(Place, nullptr);
  bool Entry = !Reaching;
  auto Untold = [&](EUntold Reason) {
    if (Fact.Reason == EUntold::None)
      Fact.Reason = Reason;
  };
  if (!Reaching)
    Untold(EUntold::Change);
  // Of a place whose address escapes, what code that can't be followed
  // writes there can be what it holds too.
  if (!readFacts(Place, [](const CFacts &Facts) { return Facts.Told; }))
    Untold(EUntold::Escaped);
  // Where the ways of each value end, once for each value and level: many
  // definitions that reach give the same value (valueRoots).
  llvm::SmallDenseMap<std::pair<const Expr *, unsigned>, CReturnedRoots, 4>
      RootsOf;
  for (const CDefinition &Each : Reaching ? *Reaching
                                          : llvm::SmallVector<CDefinition, 2>()) {
    if (Each.Index == ~0u) {
      Entry = true;
      continue;
    }
    // Of a write of storage only code that can't be seen gives, apart
    // (CFlowFact::Reached).
    auto &Sources = Each.Unseen ? Fact.Reached : Fact.Sources;
    auto UntoldOf = [&](EUntold Reason) {
      EUntold &Of = Each.Unseen ? Fact.ReachedReason : Fact.Reason;
      if (Of == EUntold::None)
        Of = Reason;
    };
    if (!Each.Known || Each.Changed || !Each.Value.Value) {
      UntoldOf(EUntold::Change);
      continue;
    }
    const bool Address = Each.Value.Kind == CSlotValue::EKind::Address;
    if (Each.Value.Kind != CSlotValue::EKind::Value && !Address) {
      UntoldOf(EUntold::Expression);
      continue;
    }
    // Where the value's ways end, from the level at which it holds what it
    // refers to; of an address, the location whose address it is.
    const Expr *Value = Each.Value.Value;
    const unsigned Level =
        Address ? 0
        : Value->isGLValue()
            ? heldLevelOfType(Value->getType())
            : (Value->getType()->isPointerType() ? 0 : 1);
    // A value one of whose ways ends elsewhere can't be told, whatever
    // its other ways end at: its walk stops there.
    auto Known = RootsOf.find({Value, Level});
    if (Known == RootsOf.end())
      Known = RootsOf
                  .try_emplace({Value, Level},
                               valueRoots(Function, {{Value, Level}}, Context,
                                          true))
                  .first;
    const CReturnedRoots &Roots = Known->second;
    if (Roots.Other) {
      UntoldOf(EUntold::Expression);
      continue;
    }
    if (Roots.Roots.empty() && !Address) {
      CFlowSource Source;
      Source.Kind = CFlowSource::EKind::Nothing;
      Source.Named = Value;
      if (!llvm::is_contained(Sources, Source))
        Sources.push_back(Source);
      continue;
    }
    for (const CReturnedRoot &Root : Roots.Roots) {
      const bool Taken = llvm::any_of(
          Root.Projections, [](const CProjection &Projection) {
            return !Projection.Path.empty() || Projection.Element;
          });
      CFlowSource Source;
      Source.Part = Taken || Root.Part || (Address && Each.Value.Part);
      const ParmVarDecl *Parameter =
          Root.Kind == CReturnedRoot::EKind::Parameter &&
                  Root.Index < Function->getNumParams()
              ? Function->getParamDecl(Root.Index)
              : nullptr;
      const QualType Type = Parameter ? Parameter->getType() : QualType();
      if (!Root.Bound.empty()) {
        UntoldOf(EUntold::Expression);
        continue;
      }
      // What the parameter is given as it is, a copy: of a parameter by
      // value or a reference to a record, at the level its name holds
      // what it refers to, of a pointer also into what it points to,
      // `_pBase + 1`; of a reference to a pointer, what that held where the
      // function was entered, `o_pValue = o_pValue + 0`.
      if (Parameter && !Address &&
          (Type->isReferenceType()
               ? (Type.getNonReferenceType()->isPointerType()
                      ? Root.Level == 1
                      : Type.getNonReferenceType()->isRecordType() &&
                            !Source.Part && Root.Level == 1)
               : Root.Level == heldLevelOfType(Type) &&
                     (!Source.Part || Type->isPointerType()))) {
        Source.Kind = CFlowSource::EKind::Argument;
        Source.Index = Root.Index;
      }
      // The address of what a reference parameter refers to.
      else if (Parameter && Type->isReferenceType() && Root.Level == 0 &&
               (Address || !Taken || Source.Part)) {
        Source.Kind = CFlowSource::EKind::ArgumentAddress;
        Source.Index = Root.Index;
      } else if (Root.Kind == CReturnedRoot::EKind::Static &&
                 Root.Level == 0 && Root.Name) {
        Source.Kind = CFlowSource::EKind::StaticAddress;
        Source.Named = Root.Name;
      } else {
        UntoldOf(EUntold::Expression);
        continue;
      }
      if (!llvm::is_contained(Sources, Source))
        Sources.push_back(Source);
    }
  }
  // Past as many values as a fact keeps, what it holds can't be told: a
  // summary costs at most so much in each pass of a recursion.
  if (Fact.Sources.size() > MaxFactSources) {
    Untold(EUntold::Budget);
    Fact.Sources.clear();
  }
  if (Fact.Reached.size() > MaxFactSources) {
    if (Fact.ReachedReason == EUntold::None)
      Fact.ReachedReason = EUntold::Budget;
    Fact.Reached.clear();
  }
  // In one order, so that a recursion settles where the values do.
  auto Order = [](const CFlowSource &Left, const CFlowSource &Right) {
    auto Key = [](const CFlowSource &Of) {
      return std::make_tuple(
          static_cast<unsigned>(Of.Kind), Of.Index, Of.Part,
          Of.Named ? Of.Named->getBeginLoc().getRawEncoding() : 0u);
    };
    return Key(Left) < Key(Right);
  };
  llvm::sort(Fact.Sources, Order);
  llvm::sort(Fact.Reached, Order);
  Fact.EveryPath =
      !Entry && Fact.Reason == EUntold::None && !Fact.Sources.empty();
  return Fact;
}

// What the function, a member function, leaves in each member of its object
// that it writes, where it returns (CFunctionSummary): of each definition
// that reaches the exit (objectReaching), its value in the terms of the
// caller, by where its way of provenance ends (valueRoots): what a parameter
// is given as it is, the address of what a reference parameter refers to or
// a part of it, the address of static storage, nothing; else untold.
CFunctionSummary CLocalFlow::summarize(std::optional<unsigned> Referent) const {
  CFunctionSummary Summary;
  // Of the members of its object, a member function's, not a closure's; of
  // the pointers a parameter is given to set (setsParameter), any function's;
  // of \p Referent, of what that parameter refers to only.
  const auto *Method = dyn_cast<CXXMethodDecl>(Function);
  const ParmVarDecl *Of = Referent && *Referent < Function->getNumParams()
                              ? Function->getParamDecl(*Referent)
                              : nullptr;
  const bool Members = !Referent && Method &&
                       Method->isImplicitObjectMemberFunction() &&
                       !Method->getParent()->isLambda();
  if ((Referent ? !Of
                : !Members &&
                      llvm::none_of(Function->parameters(), setsParameter)) ||
      !Function->getBody() || isa<CoroutineBodyStmt>(Function->getBody()))
    return Summary;
  // Of one whose flow is being made, in a recursion, what it summarizes
  // once made, from the top.
  if (Building) {
    Summary.Top = true;
    return Summary;
  }
  if (!Graph || !Ready)
    return Summary;
  CFlowSummaries &Summaries = sharedFlowCache().summaries(Context);
  // The members of the object the writes designate, and the parameters given
  // to set whose pointer they designate.
  // Each a member path, and of an element of an array or a container it
  // holds at a constant index (ownedElement), that index.
  using CSink = std::pair<llvm::SmallVector<const FieldDecl *, 2>,
                          std::optional<int64_t>>;
  llvm::SmallVector<CSink, 4> Paths;
  llvm::SmallVector<const ParmVarDecl *, 1> Set;
  for (unsigned iWrite = 0; iWrite < Writes.size(); ++iWrite) {
    if (Writes[iWrite].Kind == CWrite::EKind::Declare || Writes[iWrite].Object)
      continue;
    for (const CSlot &Target : Summaries.Targets.get({Function, iWrite}).Slots)
      if (const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Target.Variable);
          !Referent && Parameter && Target.Place == EPlace::Referent &&
          Target.Path.empty() && !Target.Element &&
          Parameter->getDeclContext() == Function && setsParameter(Parameter) &&
          !llvm::is_contained(Set, Parameter))
        Set.push_back(Parameter);
    if (!Members && !Of)
      continue;
    // Of the object, or of what the parameter refers to.
    auto Holds = [&](const CSlot &Target) {
      return Of ? Target.Place == EPlace::Referent && Target.Variable == Of
                : Target.Place == EPlace::This;
    };
    // Also of what a call on the object, or given what the parameter refers
    // to, leaves in its members (summaryWrites).
    auto Add = [&](const CTargets &Found) {
      for (const CSlot &Target : Found.Slots) {
        if (!Holds(Target) || Target.Path.empty() ||
            (Target.Element &&
             (!Target.Index || !ownedElement(Target, Function))))
          continue;
        CSink Sink{Target.Path,
                   Target.Element ? Target.Index : std::nullopt};
        if (!llvm::is_contained(Paths, Sink))
          Paths.push_back(std::move(Sink));
      }
    };
    const CTargets Found = Summaries.Targets.get({Function, iWrite});
    Add(Found);
    if (!Writes[iWrite].leavesParts() || llvm::none_of(Found.Slots, Holds))
      continue;
    for (const CWrite &Composed : summaryWrites(Writes[iWrite]))
      Add(composedTargets(iWrite, Composed));
  }
  for (const auto &[Path, ElementIndex] : Paths) {
    if (!Of) {
      Summary.Leaves.push_back(factOf({nullptr, Path, ElementIndex.has_value(),
                                       EPlace::This, ElementIndex}));
      continue;
    }
    CFlowFact Fact = factOf({Of, Path, ElementIndex.has_value(),
                             EPlace::Referent, ElementIndex});
    Fact.Target = *Referent;
    Summary.Parameters.push_back(std::move(Fact));
  }
  for (const ParmVarDecl *Parameter : Set) {
    CFlowFact Fact = factOf({Parameter, {}, false, EPlace::Referent});
    Fact.Target = Parameter->getFunctionScopeIndex();
    Summary.Parameters.push_back(std::move(Fact));
  }
  return Summary;
}

// Whether each way to \p To, or the exit where it is null, passes \p From
// after the last change of \p Slot, a member of the object of the function,
// or of a part of it (objectChanges), by the one may analysis (reachingIn).
bool CLocalFlow::objectUnchangedBetween(const CSlot &Slot, const Stmt *From,
                                        const Stmt *To) const {
  if (!Graph || !Ready || !FlowOrder || !From)
    return false;
  auto At = [](const Stmt *S) {
    return [S](const CFlowEvent &Event) {
      return Event.Kind == EEvent::Statement && Event.Statement == S;
    };
  };
  llvm::SmallVector<CFlowMark, 8> Marks{{Graph->getEntry().getBlockID(), ~0u}};
  const llvm::SmallVector<CFlowMark, 8> Starts =
      pickedElements(*Graph, Reachable, At(From));
  const llvm::SmallVector<CFlowMark, 8> Ends =
      To ? pickedElements(*Graph, Reachable, At(To))
         : llvm::SmallVector<CFlowMark, 8>{{Graph->getExit().getBlockID(), 0}};
  if (Starts.empty() || Ends.empty())
    return false;
  Marks.append(Starts.begin(), Starts.end());
  for (const CDefinition &Each : objectChanges(Slot))
    if (Each.Index != ~0u)
      Marks.push_back({Each.Block, Each.Index});
  const std::vector<llvm::BitVector> In =
      reachingIn(*Graph, Reachable, *FlowOrder, Marks, false);
  return llvm::all_of(Ends, [&](const CFlowMark &End) {
    const llvm::BitVector Reaching =
        reachingBefore(In, Marks, End.Block, End.Index);
    return Reaching.any() &&
           llvm::all_of(Reaching.set_bits(), [&](unsigned Bit) {
             return Bit >= 1 && Bit <= Starts.size();
           });
  });
}

// Whether each way to \p To passes \p From, or leads from the entry where it
// is null, after the last change of \p Slot, a place of the flow that isn't
// a local slot, what a parameter refers to or points to or the object of the
// function, or of a part of it (objectChanges), not of an element of it only;
// not of one whose facts can't be told, whose address escapes, nor read in
// the body of a closure made here.
bool CLocalFlow::placeUnchangedBetween(const CSlot &Slot, const Stmt *From,
                                       const Stmt *To) const {
  if (!Graph || !Ready)
    return false;
  const auto Start =
      From ? positionOf(From)
           : std::optional<std::pair<unsigned, unsigned>>(
                 {Graph->getEntry().getBlockID(), ~0u});
  const auto End = positionOf(To);
  if (!Start || !End || (From && inClosureBody(From)) || inClosureBody(To))
    return false;
  if (*Start == *End)
    return true;
  if (!readFacts(Slot, [](const CFacts &Facts) { return Facts.Told; }))
    return false;
  llvm::SmallVector<CFlowMark, 8> Marks{{Graph->getEntry().getBlockID(), ~0u}};
  for (const CDefinition &Each : objectChanges(Slot))
    if (Each.Index != ~0u && !(Each.Element && !Slot.Element))
      Marks.push_back({Each.Block, Each.Index});
  // From the entry, the entry's own mark.
  if (From)
    Marks.push_back({Start->first, Start->second});
  const std::vector<llvm::BitVector> In =
      reachingIn(*Graph, Reachable, *FlowOrder, Marks, false);
  const llvm::BitVector Reaching =
      reachingBefore(In, Marks, End->first, End->second);
  return Reaching.count() == 1 && Reaching.test(From ? Marks.size() - 1 : 0);
}

const CLocalFlow &CFlowCache::localFlowOf(const FunctionDecl *Function,
                                          ASTContext &Context) {
  CFlowSummaries &Of = summaries(Context);
  // What asks for it refers to it: it isn't released.
  if (!Of.Pending.empty())
    Of.Pending.erase(Function);
  return Of.LocalFlows.get(Function);
}

bool CLocalFlow::operator==(const CLocalFlow &Other) const {
  return Function == Other.Function && Ready == Other.Ready &&
         Building == Other.Building &&
         Writes == Other.Writes && RunClosures == Other.RunClosures &&
         ClosureCalls == Other.ClosureCalls &&
         ClosureEnds == Other.ClosureEnds && EndsRunning == Other.EndsRunning &&
         Escaped == Other.Escaped && EscapedReferents == Other.EscapedReferents;
}

namespace {
using CReads = llvm::SmallVector<const Stmt *, 4>;
using CProjections = llvm::SmallVector<CProjection, 2>;

// The level what an object of \p Type holds is at, the object at \p Level: of
// a record or an array, what is stored in it, its own; of a value stored, a
// pointer, a level down.
unsigned heldLevel(QualType Type, unsigned Level) {
  if (!Type.isNull() && (Type->isRecordType() || Type->isArrayType()))
    return Level;
  return Level > 0 ? Level - 1 : 0;
}

// \p Inner, what a function gives back is made of, as what the way of \p Outer
// at the call reaches: the projections of the levels above the call's, that
// of the call's level after the value's, then the value's levels below.
CProjections composed(llvm::ArrayRef<CProjection> Inner,
                      llvm::ArrayRef<CProjection> Outer) {
  CProjections Out(Outer.begin(), Outer.end() - 1);
  CProjection Merged = Inner.front();
  if (!Merged.Element) {
    llvm::append_range(Merged.Path, Outer.back().Path);
    Merged.Element = Outer.back().Element;
  }
  Out.push_back(std::move(Merged));
  Out.append(Inner.begin() + 1, Inner.end());
  return Out;
}
} // namespace

// The provenance of \p E, the one model of storage and what is stored in it,
// a pointer or a reference, which the checks, their exemptions and their
// summaries all ask: \p Visit is told each expression it is made of and at
// what level, and decides what it is (EReach). At level 0, a glvalue is the
// The members of an object of \p Record, also those of its bases, which a
// slot's path names as its own (CSlot::Path), each told to \p Each; false of
// one with a virtual base, whose members can't be told so.
static bool forEachMember(const RecordDecl *Record,
                          llvm::function_ref<void(const FieldDecl *)> Each) {
  if (stackNearlyExhausted())
    return withStack([&] { return forEachMember(Record, Each); });
  if (const auto *Class = dyn_cast<CXXRecordDecl>(Record)) {
    if (Class->getNumVBases() > 0)
      return false;
    for (const CXXBaseSpecifier &Base : Class->bases()) {
      const RecordDecl *Of = Base.getType()->getAsRecordDecl();
      if (!Of || !forEachMember(Of, Each))
        return false;
    }
  }
  for (const FieldDecl *Field : Record->fields())
    Each(Field);
  return true;
}

// Whether \p Slot, of a parameter held by value, can hold what the parameter
// was given where \p At is, at a part that can hold a reference: by its values
// there (localSlotValues, CSlotValue::EKind::Given), and of an object some
// parts of which are assigned on their own (localSlotParts), by each member
// that can hold a reference, recursively, so that one whose such members are
// each replaced on every path, `_Span.m_pData = nullptr;`, or `_Result.m_Data
// .m_pData = nullptr;` of a nested view, can't. Of a slot that can't be told,
// or past as many members as code nests, it can.
static bool holdsGiven(const CSlot &Slot, const Stmt *At, ASTContext &Context) {
  if (stackNearlyExhausted())
    return withStack([&] { return holdsGiven(Slot, At, Context); });
  const QualType Held =
      (Slot.Path.empty() ? Slot.Variable->getType()
                         : Slot.Path.back()->getType())
          .getNonReferenceType();
  if (!holdsReference(Held))
    return false;
  const CSlotValues Values = localSlotValues(Slot, At, Context);
  if (!Values.Told)
    return true;
  if (llvm::none_of(Values.Values, [](const CSlotValue &Value) {
        return Value.Kind == CSlotValue::EKind::Given;
      }))
    return false;
  const auto *Record = Held->getAsRecordDecl();
  if (!Record || Slot.Path.size() > 16 ||
      (!Slot.Path.empty() && Slot.Path.back()->getType()->isReferenceType()) ||
      localSlotParts(Slot, At, Context).empty())
    return true;
  bool Holds = false;
  return !forEachMember(Record,
                        [&](const FieldDecl *Field) {
                          CSlot Member = Slot;
                          Member.Path.push_back(Field);
                          Holds = Holds || holdsGiven(Member, At, Context);
                        }) ||
         Holds;
}

/// Whether \p Cast converts a null pointer constant to a null pointer.
static bool isNullCast(const CastExpr *Cast) {
  return Cast->getCastKind() == CK_NullToPointer ||
         Cast->getCastKind() == CK_NullToMemberPointer;
}

ELeaf leafOf(const Expr *E) {
  switch (E->getStmtClass()) {
  // A null pointer constant, a literal that isn't a pointer, a value made
  // of a type alone or a value-initialized scalar: nothing.
  case Stmt::CXXNullPtrLiteralExprClass:
  case Stmt::GNUNullExprClass:
  case Stmt::IntegerLiteralClass:
  case Stmt::FloatingLiteralClass:
  case Stmt::CharacterLiteralClass:
  case Stmt::CXXBoolLiteralExprClass:
  case Stmt::FixedPointLiteralClass:
  case Stmt::ImaginaryLiteralClass:
  case Stmt::ObjCBoolLiteralExprClass:
  case Stmt::ImplicitValueInitExprClass:
  case Stmt::CXXScalarValueInitExprClass:
  case Stmt::UnaryExprOrTypeTraitExprClass:
  case Stmt::TypeTraitExprClass:
  case Stmt::ArrayTypeTraitExprClass:
  case Stmt::ExpressionTraitExprClass:
  case Stmt::CXXNoexceptExprClass:
  case Stmt::SizeOfPackExprClass:
  case Stmt::OffsetOfExprClass:
  case Stmt::ConceptSpecializationExprClass:
  case Stmt::RequiresExprClass:
  case Stmt::OpenACCAsteriskSizeExprClass:
    return ELeaf::Nothing;
  // What has static storage: a string literal and what names one, a label.
  case Stmt::StringLiteralClass:
  case Stmt::PredefinedExprClass:
  case Stmt::ObjCStringLiteralClass:
  case Stmt::ObjCEncodeExprClass:
  case Stmt::ObjCSelectorExprClass:
  case Stmt::ObjCProtocolExprClass:
  case Stmt::AddrLabelExprClass:
  case Stmt::CXXUuidofExprClass:
  case Stmt::SYCLUniqueStableNameExprClass:
    return ELeaf::Static;
  // A cast of a null pointer constant refers to nothing; another cast is
  // followed to what it casts.
  case Stmt::ImplicitCastExprClass:
  case Stmt::CStyleCastExprClass:
  case Stmt::CXXStaticCastExprClass:
  case Stmt::CXXFunctionalCastExprClass:
  case Stmt::CXXReinterpretCastExprClass:
  case Stmt::CXXConstCastExprClass:
  case Stmt::CXXDynamicCastExprClass:
  case Stmt::CXXAddrspaceCastExprClass:
  case Stmt::BuiltinBitCastExprClass:
  case Stmt::ObjCBridgedCastExprClass:
    return isNullCast(cast<CastExpr>(E)) ? ELeaf::Nothing : ELeaf::Followed;
  // `__builtin_FILE()` is a string literal, `__builtin_LINE()` a number.
  case Stmt::SourceLocExprClass:
    return E->getType()->isPointerType() ? ELeaf::Static : ELeaf::Nothing;
  // `{}` of a scalar is value-initialization; else its elements are followed.
  case Stmt::InitListExprClass:
    return cast<InitListExpr>(E)->getNumInits() == 0 &&
                   E->getType()->isScalarType()
               ? ELeaf::Nothing
               : ELeaf::Followed;
  // What provenance follows, by the effect table of each kind (step), and
  // what it can't: nothing it follows to is told of these.
  case Stmt::NoStmtClass:
  case Stmt::WhileStmtClass:
  case Stmt::LabelStmtClass:
  case Stmt::VAArgExprClass:
  case Stmt::UnaryOperatorClass:
  case Stmt::SubstNonTypeTemplateParmPackExprClass:
  case Stmt::SubstNonTypeTemplateParmExprClass:
  case Stmt::StmtExprClass:
  case Stmt::ShuffleVectorExprClass:
  case Stmt::RecoveryExprClass:
  case Stmt::PseudoObjectExprClass:
  case Stmt::ParenListExprClass:
  case Stmt::ParenExprClass:
  case Stmt::PackIndexingExprClass:
  case Stmt::PackExpansionExprClass:
  case Stmt::UnresolvedMemberExprClass:
  case Stmt::UnresolvedLookupExprClass:
  case Stmt::OpaqueValueExprClass:
  case Stmt::ObjCSubscriptRefExprClass:
  case Stmt::ObjCPropertyRefExprClass:
  case Stmt::ObjCDictionaryLiteralClass:
  case Stmt::ObjCBoxedExprClass:
  case Stmt::ObjCArrayLiteralClass:
  case Stmt::ObjCMessageExprClass:
  case Stmt::ObjCIvarRefExprClass:
  case Stmt::ObjCIsaExprClass:
  case Stmt::ObjCIndirectCopyRestoreExprClass:
  case Stmt::ObjCAvailabilityCheckExprClass:
  case Stmt::OMPIteratorExprClass:
  case Stmt::OMPArrayShapingExprClass:
  case Stmt::NoInitExprClass:
  case Stmt::MemberExprClass:
  case Stmt::MatrixSubscriptExprClass:
  case Stmt::MatrixSingleSubscriptExprClass:
  case Stmt::MatrixElementExprClass:
  case Stmt::MaterializeTemporaryExprClass:
  case Stmt::MSPropertySubscriptExprClass:
  case Stmt::MSPropertyRefExprClass:
  case Stmt::LambdaExprClass:
  case Stmt::HLSLOutArgExprClass:
  case Stmt::GenericSelectionExprClass:
  case Stmt::FunctionParmPackExprClass:
  case Stmt::ExprWithCleanupsClass:
  case Stmt::ConstantExprClass:
  case Stmt::ExtVectorElementExprClass:
  case Stmt::EmbedExprClass:
  case Stmt::DesignatedInitUpdateExprClass:
  case Stmt::DesignatedInitExprClass:
  case Stmt::DependentScopeDeclRefExprClass:
  case Stmt::DependentCoawaitExprClass:
  case Stmt::DeclRefExprClass:
  case Stmt::CoyieldExprClass:
  case Stmt::CoawaitExprClass:
  case Stmt::ConvertVectorExprClass:
  case Stmt::CompoundLiteralExprClass:
  case Stmt::ChooseExprClass:
  case Stmt::CallExprClass:
  case Stmt::UserDefinedLiteralClass:
  case Stmt::CXXOperatorCallExprClass:
  case Stmt::CXXMemberCallExprClass:
  case Stmt::CUDAKernelCallExprClass:
  case Stmt::CXXUnresolvedConstructExprClass:
  case Stmt::CXXTypeidExprClass:
  case Stmt::CXXThrowExprClass:
  case Stmt::CXXThisExprClass:
  case Stmt::CXXStdInitializerListExprClass:
  case Stmt::CXXRewrittenBinaryOperatorClass:
  case Stmt::CXXReflectExprClass:
  case Stmt::CXXPseudoDestructorExprClass:
  case Stmt::CXXParenListInitExprClass:
  case Stmt::CXXNewExprClass:
  case Stmt::CXXInheritedCtorInitExprClass:
  case Stmt::CXXFoldExprClass:
  case Stmt::CXXExpansionSelectExprClass:
  case Stmt::CXXDependentScopeMemberExprClass:
  case Stmt::CXXDeleteExprClass:
  case Stmt::CXXDefaultInitExprClass:
  case Stmt::CXXDefaultArgExprClass:
  case Stmt::CXXConstructExprClass:
  case Stmt::CXXTemporaryObjectExprClass:
  case Stmt::CXXBindTemporaryExprClass:
  case Stmt::BlockExprClass:
  case Stmt::BinaryOperatorClass:
  case Stmt::CompoundAssignOperatorClass:
  case Stmt::AtomicExprClass:
  case Stmt::AsTypeExprClass:
  case Stmt::ArraySubscriptExprClass:
  case Stmt::ArraySectionExprClass:
  case Stmt::ArrayInitLoopExprClass:
  case Stmt::ArrayInitIndexExprClass:
  case Stmt::ConditionalOperatorClass:
  case Stmt::BinaryConditionalOperatorClass:
  case Stmt::AttributedStmtClass:
  case Stmt::UnresolvedSYCLKernelCallStmtClass:
  case Stmt::SwitchStmtClass:
  case Stmt::DefaultStmtClass:
  case Stmt::CaseStmtClass:
  case Stmt::SYCLKernelCallStmtClass:
  case Stmt::SEHTryStmtClass:
  case Stmt::SEHLeaveStmtClass:
  case Stmt::SEHFinallyStmtClass:
  case Stmt::SEHExceptStmtClass:
  case Stmt::ReturnStmtClass:
  case Stmt::OpenACCWaitConstructClass:
  case Stmt::OpenACCUpdateConstructClass:
  case Stmt::OpenACCShutdownConstructClass:
  case Stmt::OpenACCSetConstructClass:
  case Stmt::OpenACCInitConstructClass:
  case Stmt::OpenACCExitDataConstructClass:
  case Stmt::OpenACCEnterDataConstructClass:
  case Stmt::OpenACCCacheConstructClass:
  case Stmt::OpenACCLoopConstructClass:
  case Stmt::OpenACCHostDataConstructClass:
  case Stmt::OpenACCDataConstructClass:
  case Stmt::OpenACCComputeConstructClass:
  case Stmt::OpenACCCombinedConstructClass:
  case Stmt::OpenACCAtomicConstructClass:
  case Stmt::ObjCForCollectionStmtClass:
  case Stmt::ObjCAutoreleasePoolStmtClass:
  case Stmt::ObjCAtTryStmtClass:
  case Stmt::ObjCAtThrowStmtClass:
  case Stmt::ObjCAtSynchronizedStmtClass:
  case Stmt::ObjCAtFinallyStmtClass:
  case Stmt::ObjCAtCatchStmtClass:
  case Stmt::OMPTeamsDirectiveClass:
  case Stmt::OMPTaskyieldDirectiveClass:
  case Stmt::OMPTaskwaitDirectiveClass:
  case Stmt::OMPTaskgroupDirectiveClass:
  case Stmt::OMPTaskDirectiveClass:
  case Stmt::OMPTargetUpdateDirectiveClass:
  case Stmt::OMPTargetTeamsDirectiveClass:
  case Stmt::OMPTargetParallelForDirectiveClass:
  case Stmt::OMPTargetParallelDirectiveClass:
  case Stmt::OMPTargetExitDataDirectiveClass:
  case Stmt::OMPTargetEnterDataDirectiveClass:
  case Stmt::OMPTargetDirectiveClass:
  case Stmt::OMPTargetDataDirectiveClass:
  case Stmt::OMPSingleDirectiveClass:
  case Stmt::OMPSectionsDirectiveClass:
  case Stmt::OMPSectionDirectiveClass:
  case Stmt::OMPScopeDirectiveClass:
  case Stmt::OMPScanDirectiveClass:
  case Stmt::OMPParallelSectionsDirectiveClass:
  case Stmt::OMPParallelMasterDirectiveClass:
  case Stmt::OMPParallelMaskedDirectiveClass:
  case Stmt::OMPParallelDirectiveClass:
  case Stmt::OMPOrderedDirectiveClass:
  case Stmt::OMPMetaDirectiveClass:
  case Stmt::OMPMasterDirectiveClass:
  case Stmt::OMPMaskedDirectiveClass:
  case Stmt::OMPTeamsGenericLoopDirectiveClass:
  case Stmt::OMPTeamsDistributeSimdDirectiveClass:
  case Stmt::OMPTeamsDistributeParallelForSimdDirectiveClass:
  case Stmt::OMPTeamsDistributeParallelForDirectiveClass:
  case Stmt::OMPTeamsDistributeDirectiveClass:
  case Stmt::OMPTaskLoopSimdDirectiveClass:
  case Stmt::OMPTaskLoopDirectiveClass:
  case Stmt::OMPTargetTeamsGenericLoopDirectiveClass:
  case Stmt::OMPTargetTeamsDistributeSimdDirectiveClass:
  case Stmt::OMPTargetTeamsDistributeParallelForSimdDirectiveClass:
  case Stmt::OMPTargetTeamsDistributeParallelForDirectiveClass:
  case Stmt::OMPTargetTeamsDistributeDirectiveClass:
  case Stmt::OMPTargetSimdDirectiveClass:
  case Stmt::OMPTargetParallelGenericLoopDirectiveClass:
  case Stmt::OMPTargetParallelForSimdDirectiveClass:
  case Stmt::OMPSimdDirectiveClass:
  case Stmt::OMPParallelMasterTaskLoopSimdDirectiveClass:
  case Stmt::OMPParallelMasterTaskLoopDirectiveClass:
  case Stmt::OMPParallelMaskedTaskLoopSimdDirectiveClass:
  case Stmt::OMPParallelMaskedTaskLoopDirectiveClass:
  case Stmt::OMPParallelGenericLoopDirectiveClass:
  case Stmt::OMPParallelForSimdDirectiveClass:
  case Stmt::OMPParallelForDirectiveClass:
  case Stmt::OMPMasterTaskLoopSimdDirectiveClass:
  case Stmt::OMPMasterTaskLoopDirectiveClass:
  case Stmt::OMPMaskedTaskLoopSimdDirectiveClass:
  case Stmt::OMPMaskedTaskLoopDirectiveClass:
  case Stmt::OMPGenericLoopDirectiveClass:
  case Stmt::OMPForSimdDirectiveClass:
  case Stmt::OMPForDirectiveClass:
  case Stmt::OMPDistributeSimdDirectiveClass:
  case Stmt::OMPDistributeParallelForSimdDirectiveClass:
  case Stmt::OMPDistributeParallelForDirectiveClass:
  case Stmt::OMPDistributeDirectiveClass:
  case Stmt::OMPUnrollDirectiveClass:
  case Stmt::OMPTileDirectiveClass:
  case Stmt::OMPStripeDirectiveClass:
  case Stmt::OMPSplitDirectiveClass:
  case Stmt::OMPReverseDirectiveClass:
  case Stmt::OMPInterchangeDirectiveClass:
  case Stmt::OMPInteropDirectiveClass:
  case Stmt::OMPFlushDirectiveClass:
  case Stmt::OMPErrorDirectiveClass:
  case Stmt::OMPDispatchDirectiveClass:
  case Stmt::OMPDepobjDirectiveClass:
  case Stmt::OMPCriticalDirectiveClass:
  case Stmt::OMPFuseDirectiveClass:
  case Stmt::OMPCancellationPointDirectiveClass:
  case Stmt::OMPCancelDirectiveClass:
  case Stmt::OMPBarrierDirectiveClass:
  case Stmt::OMPAtomicDirectiveClass:
  case Stmt::OMPAssumeDirectiveClass:
  case Stmt::OMPCanonicalLoopClass:
  case Stmt::NullStmtClass:
  case Stmt::MSDependentExistsStmtClass:
  case Stmt::ContinueStmtClass:
  case Stmt::BreakStmtClass:
  case Stmt::IndirectGotoStmtClass:
  case Stmt::IfStmtClass:
  case Stmt::GotoStmtClass:
  case Stmt::ForStmtClass:
  case Stmt::DoStmtClass:
  case Stmt::DeferStmtClass:
  case Stmt::DeclStmtClass:
  case Stmt::CoroutineBodyStmtClass:
  case Stmt::CoreturnStmtClass:
  case Stmt::CompoundStmtClass:
  case Stmt::CapturedStmtClass:
  case Stmt::CXXTryStmtClass:
  case Stmt::CXXForRangeStmtClass:
  case Stmt::CXXExpansionStmtPatternClass:
  case Stmt::CXXExpansionStmtInstantiationClass:
  case Stmt::CXXCatchStmtClass:
  case Stmt::MSAsmStmtClass:
  case Stmt::GCCAsmStmtClass:
    return ELeaf::Followed;
  }
  return ELeaf::Followed;
}

bool refersToNothing(const Expr *E, unsigned Level) {
  switch (leafOf(E)) {
  case ELeaf::Nothing:
    return true;
  case ELeaf::Static:
    return Level > 0;
  case ELeaf::Followed:
    return false;
  }
  return false;
}

// storage it designates, a pointer what it points to; at each level above,
// what the value stored there, loaded, points to. By the transition of each
// effect (step): what gives on the same location or value, also a part of it,
// a member or an element, a load, one level up, what a pointer is followed to
// and its address at the same level, a store a level down, a temporary made of
// it, what a function gives back of what it is called with (returnedRoots). A
// local slot above level 0 by what it holds where it is read
// (localSlotValues). A name of a parameter held by value above level 0 only
// where the part of it the way takes can hold what it was given (holdsGiven),
// else by what it holds there. What each way takes of each level, the members
// and the elements (CProjection), Visit is told by projections. Whether each way
// ended where the table tells or \p Visit stopped it, not where it can't be
// told. \p Ended, when given, is set when a way ends at what isn't a root
// (EReach::Stop, or nothing past it), of what must hold of every alternative,
// not only of one.
// A way of reach: the expression with the level, the part, the reads and
// the projections it is reached with.
// How many ways reach tells at most (its Seen); past that, what they reach
// can't be told.
static constexpr unsigned MaxWaysTold = 200000;

struct CProvenance::CWay {
  const Expr *At;
  unsigned Level;
  bool Part;
  CReads Reads;
  CProjections Taken;
};

// What tells the ways of reach apart besides where they are and their level
// (its Seen): where the slots they read are read and what they take of each
// level, but the index and the stride of an element.
static size_t wayHash(const CReads &Reads, const CProjections &Taken) {
  size_t Hash = llvm::hash_combine_range(Reads.begin(), Reads.end());
  for (const CProjection &Each : Taken)
    Hash = llvm::hash_combine(
        Hash, llvm::hash_combine_range(Each.Path.begin(), Each.Path.end()),
        Each.Element);
  return Hash;
}

// Whether \p Way is at a name of a parameter held by value whose part the
// way takes holds only what replaced what it was given there, which reach
// doesn't tell (Visit) but follows (step).
bool CProvenance::holdsOnlyReplaced(const CWay &Way) const {
  if (Way.Level == 0 || Way.Taken.back().Element ||
      effectsOf(Way.At, Context).Statement != EStatement::Name ||
      holderKind(cast<DeclRefExpr>(Way.At)->getDecl()) != EHolder::Parameter)
    return false;
  const auto *Parameter =
      cast<ParmVarDecl>(cast<DeclRefExpr>(Way.At)->getDecl());
  return !Parameter->getType()->isReferenceType() &&
         slotFunction(Parameter, Way.At) == Parameter->getDeclContext() &&
         !holdsGiven({Parameter, Way.Taken.back().Path, false},
                     Way.Reads.empty() ? Way.At : Way.Reads.back(), Context);
}

// What a way of reach at \p Way goes on to (step), each added to \p Into in
// the order step tells them; \p Complete is cleared where that can't be told.
void CProvenance::expandWay(const CWay &Way, llvm::SmallVectorImpl<CWay> &Into,
                            bool &Complete) const {
  // Also inside a step that reaches again, as the coroutine check's.
  const Stmt *WasReadAt = ReadAt;
  const EHop WasHop = Hop;
  const EProject WasProject = Project;
  const FieldDecl *WasField = ProjectField;
  llvm::SmallVector<const FieldDecl *, 2> WasPath = ProjectPath;
  const CReturnedRoot *WasRoot = ProjectRoot;
  const llvm::SmallVectorImpl<CProjection> *WasTaken = Projections;
  ReadAt = Way.Reads.empty() ? nullptr : Way.Reads.back();
  Hop = EHop::None;
  Project = EProject::None;
  ProjectField = nullptr;
  ProjectPath.clear();
  ProjectRoot = nullptr;
  Projections = &Way.Taken;
  // The reads still to be made, the next last: a load reads its operand's
  // slot where it is; what a slot holds is read by the read it was reached
  // for, and what it held before an update where the update reads it; what
  // its value is made of past that is read by the reads before.
  // What each level is taken of (CProjection), one for each level, the
  // level it is at last: a load adds one, what is held a level down drops
  // one.
  step(
      Way.At, Way.Level, Way.Part,
      [&](const Expr *To, unsigned NextLevel, bool NextPart) {
        const EHop Was = Hop;
        Hop = EHop::None;
        CReads Then = Way.Reads;
        switch (Was) {
        case EHop::Load:
          Then.push_back(Way.At);
          break;
        case EHop::Held:
          if (!Then.empty())
            Then.pop_back();
          break;
        case EHop::HeldBefore:
          if (!Then.empty())
            Then.pop_back();
          Then.push_back(To);
          break;
        // What a binding binds is an expression of its declaration, not
        // of the flow: what it reads is read where the name is.
        case EHop::Binding:
          if (Then.empty())
            Then.push_back(Way.At);
          break;
        // A step a level down reads what that level was loaded for: the
        // read of the level it leaves is made.
        case EHop::None:
          for (unsigned Down = NextLevel; Down < Way.Level && !Then.empty();
               ++Down)
            Then.pop_back();
          break;
        }
        CProjections Taking = Way.Taken;
        switch (Project) {
        case EProject::Field:
          if (NextLevel == Way.Level && ProjectField)
            Taking.back().Path.insert(Taking.back().Path.begin(),
                                      ProjectField);
          break;
        case EProject::Element:
          if (NextLevel == Way.Level)
            Taking.back() = {{}, true, ProjectIndex, ProjectStride};
          break;
        case EProject::Consume:
          if (!Taking.back().Path.empty())
            Taking.back().Path.erase(Taking.back().Path.begin());
          break;
        case EProject::Clear:
          Taking.back() = {};
          break;
        // What the slot held before is the slot itself, as it is named;
        // what it pointed to, an element of what it points to now.
        case EProject::Stepped:
          Taking.back() = {};
          if (Taking.size() >= 2)
            Taking[Taking.size() - 2] = {{}, true};
          break;
        case EProject::Returned:
          if (ProjectRoot && !ProjectRoot->Projections.empty())
            Taking = composed(ProjectRoot->Projections, Taking);
          break;
        case EProject::Path:
          if (NextLevel == Way.Level)
            Taking.back() = {{ProjectPath.begin(), ProjectPath.end()}, false};
          break;
        case EProject::None:
          break;
        }
        Taking.resize(NextLevel + 1);
        Into.push_back(
            {To, NextLevel, NextPart, std::move(Then), std::move(Taking)});
      },
      Complete);
  ReadAt = WasReadAt;
  Hop = WasHop;
  Project = WasProject;
  ProjectField = WasField;
  ProjectPath = std::move(WasPath);
  ProjectRoot = WasRoot;
  Projections = WasTaken;
}

bool CProvenance::reach(
    const Expr *E, unsigned Level,
    llvm::function_ref<EReach(const Expr *At, unsigned Level, bool Part)> Visit,
    bool *Ended) const {
  // A walk a step of another starts, which can't be told past a depth of
  // them in the summary it is in (s_Nested), as a way that goes on too far
  // can't, `p = p->m_pNext` in a loop.
  if (*s_Nested >= 16)
    return false;
  ++*s_Nested;
  const llvm::scope_exit Unnest([] { --*s_Nested; });
  // Each way with where the slots it reads are read (CReads) and what it takes
  // of each level (CProjection), once for each.
  llvm::DenseSet<std::pair<std::pair<const Expr *, unsigned>, size_t>> Seen;
  bool Complete = true;
  // What a way goes on to, by a work stack: each expression with the level,
  // the part, the reads and the projections it is reached with, told once
  // (Seen), so a chain or a diamond of copies is told in time linear in its
  // length; past a budget of what is told, what it reaches can't be told.
  // The stack holds indices of Pool, which has each way on it once however
  // often it is pushed, a read that each of N definitions reaches pushed by
  // each, with its hash (wayHash) and the places of Work that hold it.
  struct CPooled {
    CWay Way;
    size_t Hash = 0;
    unsigned Uses = 0;
  };
  std::deque<CPooled> Pool;
  llvm::SmallVector<unsigned, 8> FreePooled;
  llvm::DenseMap<std::tuple<const Expr *, unsigned, unsigned, size_t>, unsigned>
      PoolIndex;
  llvm::SmallVector<unsigned, 16> Work;
  auto Push = [&](CWay &&Way) {
    const size_t Hash = wayHash(Way.Reads, Way.Taken);
    const auto [It, New] = PoolIndex.try_emplace(
        {Way.At, Way.Level, unsigned(Way.Part), Hash},
        unsigned(Pool.size()));
    if (!New) {
      CPooled &Known = Pool[It->second];
      if (Known.Way.Reads == Way.Reads && Known.Way.Taken == Way.Taken) {
        ++Known.Uses;
        Work.push_back(It->second);
        return;
      }
    }
    unsigned Made;
    if (!FreePooled.empty()) {
      Made = FreePooled.pop_back_val();
      Pool[Made] = {std::move(Way), Hash, 1};
    } else {
      Made = Pool.size();
      Pool.push_back({std::move(Way), Hash, 1});
    }
    It->second = Made;
    Work.push_back(Made);
  };
  // What a way at \p At goes on to (step), each pushed so that the first is
  // told first; whether there is any.
  auto Expand = [&](const CWay &Way) {
    llvm::SmallVector<CWay, 4> Next;
    expandWay(Way, Next, Complete);
    for (CWay &Each : llvm::reverse(Next))
      Push(std::move(Each));
    return !Next.empty();
  };
  Push({E, Level, false, CReads(), CProjections(Level + 1)});
  while (!Work.empty() && !(bStopAtEnd && Ended && *Ended)) {
    const unsigned Popped = Work.pop_back_val();
    // Released once no place of Work holds it, after this way is done with.
    const llvm::scope_exit Release([&] {
      CPooled &Done = Pool[Popped];
      if (--Done.Uses != 0)
        return;
      const auto It = PoolIndex.find(
          {Done.Way.At, Done.Way.Level, unsigned(Done.Way.Part), Done.Hash});
      if (It != PoolIndex.end() && It->second == Popped)
        PoolIndex.erase(It);
      Done.Way = CWay();
      FreePooled.push_back(Popped);
    });
    const CWay &Way = Pool[Popped].Way;
    const size_t WayHash = Pool[Popped].Hash;
    if (!Way.At) {
      if (Ended)
        *Ended = true;
      continue;
    }
    if (Way.Level > 4 || Seen.size() >= MaxWaysTold) {
      Complete = false;
      continue;
    }
    if (!Seen.insert({{Way.At, Way.Level}, WayHash}).second)
      continue;
    if (holdsOnlyReplaced(Way)) {
      Expand(Way);
      continue;
    }
    // Also inside a Visit that reaches again with this.
    const llvm::SmallVectorImpl<CProjection> *WasTaken = Projections;
    Projections = &Way.Taken;
    const EReach Reach = Visit(Way.At, Way.Level, Way.Part);
    Projections = WasTaken;
    switch (Reach) {
    case EReach::Root:
      continue;
    case EReach::Stop:
      if (Ended)
        *Ended = true;
      continue;
    case EReach::Continue:
      break;
    }
    // A way that ends where nothing is past it ends, unless what it ends at
    // refers to nothing (leafOf), which is no alternative.
    if (!Expand(Way) && Ended && !refersToNothing(Way.At, Way.Level))
      *Ended = true;
  }
  return Complete;
}

// Of each way of reach the closures are asked about: what tells it apart
// besides its key (wayHash), its records, what it goes on to, and of the
// strongly connected ways it is one of, by Tarjan's algorithm, the records of
// all they reach.
struct CReachClosures::CNode {
  const Expr *At = nullptr;
  unsigned Level = 0;
  bool Part = false;
  CReads Reads;
  CProjections Taken;
  llvm::SmallVector<unsigned, 2> Records;
  // What it goes on to, once the walk gets to it.
  llvm::SmallVector<unsigned, 2> Next;
  bool Expanded = false;
  // Whether Visit was told it, and whether reach can't tell what it goes on
  // to (its Complete cleared, or a way past level 4).
  bool Told = false;
  bool Incomplete = false;
  unsigned Order = 0;
  unsigned Low = 0;
  bool OnStack = false;
  // The component it is in once it is finished, an index of Closures.
  unsigned Component = ~0u;
};

struct CReachClosures::CData {
  std::vector<CNode> Nodes;
  llvm::DenseMap<std::pair<std::pair<const Expr *, unsigned>, size_t>, unsigned>
      Index;
  // Of each start whose component is finished, its node: what NodeOf gives
  // it again, without hashing the way.
  llvm::DenseMap<std::pair<const Expr *, unsigned>, unsigned> Starts;
  std::vector<llvm::SmallVector<unsigned, 4>> Closures;
  // Whether no way reach would tell apart from another it can't be had (its
  // Seen keeps the first), and the ways are within its budget: else no
  // closure is told.
  bool Shared = true;
  unsigned NextOrder = 0;
  // Of each component, whether a way it reaches is incomplete.
  llvm::BitVector Incomplete;
  // Of visits: of each expression Visit is told a way at, its bit; of each
  // component, the bits of the ways it reaches; the bits all components take,
  // bounded (MaxVisitedBits).
  bool bVisits = false;
  llvm::DenseMap<const Expr *, unsigned> Bits;
  std::vector<llvm::BitVector> Visited;
  size_t VisitedBits = 0;
};

// The bits the components of visits take at most, 16 MB, of a body whose
// copies chain thousands of ways; past that reach is asked instead.
static constexpr size_t MaxVisitedBits = size_t(1) << 27;

CReachClosures::CReachClosures(const CProvenance &Walk, bool bVisits)
    : Walk(Walk), Data(std::make_unique<CData>()) {
  Data->bVisits = bVisits;
}

CReachClosures::~CReachClosures() = default;

const llvm::SmallVectorImpl<unsigned> *
CReachClosures::closure(const Expr *E, unsigned Level, FVisit Visit,
                        FReached Reached, bool *Incomplete) {
  assert(!Data->bVisits);
  const std::optional<unsigned> Component = component(E, Level, Visit, Reached);
  if (!Component)
    return nullptr;
  if (Incomplete)
    *Incomplete = Data->Incomplete[*Component];
  return &Data->Closures[*Component];
}

std::optional<bool> CReachClosures::visits(const Expr *E, unsigned Level,
                                           const Expr *Target) {
  assert(Data->bVisits);
  const std::optional<unsigned> Component = component(
      E, Level,
      [](const Expr *, unsigned, llvm::SmallVectorImpl<unsigned> &) {
        return EReach::Continue;
      },
      [](const Expr *) {});
  if (!Component)
    return std::nullopt;
  if (Data->Incomplete[*Component])
    return true;
  const llvm::BitVector &Visits = Data->Visited[*Component];
  const auto It = Data->Bits.find(Target);
  return It != Data->Bits.end() && It->second < Visits.size() &&
         Visits.test(It->second);
}

std::optional<unsigned> CReachClosures::component(const Expr *E,
                                                  unsigned Level, FVisit Visit,
                                                  FReached Reached) {
  using CWay = CProvenance::CWay;
  CData &D = *Data;
  // Asked where reach would be asked outside any walk, at the depth of walks
  // reach is at then (s_Nested).
  if (!D.Shared || *s_Nested != 0)
    return std::nullopt;
  if (const auto It = D.Starts.find({E, Level}); It != D.Starts.end())
    return D.Nodes[It->second].Component;
  ++*s_Nested;
  const llvm::scope_exit Unnest([] { --*s_Nested; });
  // The node of \p Way, as reach tells a way it hasn't seen; none of a way
  // reach doesn't tell.
  auto NodeOf = [&](const CWay &Way) -> std::optional<unsigned> {
    if (!Way.At || Way.Level > 4)
      return std::nullopt;
    const auto Key = std::make_pair(std::make_pair(Way.At, Way.Level),
                                    wayHash(Way.Reads, Way.Taken));
    if (const auto It = D.Index.find(Key); It != D.Index.end()) {
      const CNode &Known = D.Nodes[It->second];
      if (Known.Reads != Way.Reads || !(Known.Taken == Way.Taken))
        D.Shared = false;
      return It->second;
    }
    if (D.Nodes.size() >= MaxWaysTold) {
      D.Shared = false;
      return std::nullopt;
    }
    const unsigned Made = D.Nodes.size();
    D.Index.try_emplace(Key, Made);
    CNode Node;
    Node.At = Way.At;
    Node.Level = Way.Level;
    Node.Part = Way.Part;
    Node.Reads = Way.Reads;
    Node.Taken = Way.Taken;
    D.Nodes.push_back(std::move(Node));
    return Made;
  };
  // Its records and what it goes on to, as reach tells it.
  auto Expand = [&](unsigned Node) {
    D.Nodes[Node].Expanded = true;
    const CWay Way{D.Nodes[Node].At, D.Nodes[Node].Level, D.Nodes[Node].Part,
                   D.Nodes[Node].Reads, D.Nodes[Node].Taken};
    Reached(Way.At);
    llvm::SmallVector<unsigned, 2> Records;
    bool Expands = true;
    if (!Walk.holdsOnlyReplaced(Way)) {
      const llvm::SmallVectorImpl<CProjection> *WasTaken = Walk.Projections;
      Walk.Projections = &Way.Taken;
      Expands = Visit(Way.At, Way.Level, Records) == EReach::Continue;
      Walk.Projections = WasTaken;
      D.Nodes[Node].Told = true;
      if (D.bVisits)
        D.Bits.try_emplace(Way.At, D.Bits.size());
    }
    llvm::SmallVector<CWay, 4> Ways;
    bool Complete = true;
    if (Expands)
      Walk.expandWay(Way, Ways, Complete);
    llvm::SmallVector<unsigned, 2> Next;
    for (const CWay &Each : Ways) {
      if (Each.At && Each.Level > 4)
        Complete = false;
      if (const std::optional<unsigned> To = NodeOf(Each))
        Next.push_back(*To);
    }
    D.Nodes[Node].Incomplete = !Complete;
    llvm::sort(Records);
    Records.erase(llvm::unique(Records), Records.end());
    D.Nodes[Node].Records = std::move(Records);
    D.Nodes[Node].Next = std::move(Next);
  };
  const std::optional<unsigned> Start =
      NodeOf({E, Level, false, CReads(), CProjections(Level + 1)});
  if (!D.Shared)
    return std::nullopt;
  // Of a start reach doesn't tell, nothing is visited; past level 4, what it
  // reaches can't be told.
  if (!Start) {
    D.Closures.emplace_back();
    D.Incomplete.push_back(E && Level > 4);
    if (D.bVisits)
      D.Visited.emplace_back();
    return D.Closures.size() - 1;
  }
  // Each node not finished, depth first, without recursion: the node and the
  // next of what it goes on to.
  llvm::SmallVector<std::pair<unsigned, unsigned>, 16> Frames;
  llvm::SmallVector<unsigned, 16> Stack;
  auto Enter = [&](unsigned Node) {
    Expand(Node);
    D.Nodes[Node].Order = D.Nodes[Node].Low = ++D.NextOrder;
    D.Nodes[Node].OnStack = true;
    Stack.push_back(Node);
    Frames.push_back({Node, 0});
  };
  if (D.Nodes[*Start].Component == ~0u)
    Enter(*Start);
  while (!Frames.empty() && D.Shared) {
    const unsigned At = Frames.back().first;
    if (Frames.back().second < D.Nodes[At].Next.size()) {
      const unsigned Next = D.Nodes[At].Next[Frames.back().second++];
      CNode &To = D.Nodes[Next];
      if (To.Component != ~0u)
        continue;
      if (To.Order == 0)
        Enter(Next);
      else if (To.OnStack)
        D.Nodes[At].Low = std::min(D.Nodes[At].Low, To.Order);
      continue;
    }
    Frames.pop_back();
    if (!Frames.empty())
      D.Nodes[Frames.back().first].Low =
          std::min(D.Nodes[Frames.back().first].Low, D.Nodes[At].Low);
    if (D.Nodes[At].Low != D.Nodes[At].Order)
      continue;
    // A component: its records and those of the components it reaches.
    const unsigned Component = D.Closures.size();
    llvm::SmallVector<unsigned, 4> Members;
    unsigned Member;
    do {
      Member = Stack.pop_back_val();
      D.Nodes[Member].OnStack = false;
      D.Nodes[Member].Component = Component;
      Members.push_back(Member);
    } while (Member != At);
    llvm::SmallVector<unsigned, 4> Records;
    auto Add = [&](llvm::ArrayRef<unsigned> More) {
      if (More.empty())
        return;
      llvm::SmallVector<unsigned, 4> Union;
      std::set_union(Records.begin(), Records.end(), More.begin(), More.end(),
                     std::back_inserter(Union));
      Records = std::move(Union);
    };
    llvm::BitVector Visits;
    bool bIncomplete = false;
    for (unsigned Each : Members) {
      Add(D.Nodes[Each].Records);
      bIncomplete |= D.Nodes[Each].Incomplete;
      if (D.bVisits && D.Nodes[Each].Told) {
        const unsigned Bit = D.Bits.find(D.Nodes[Each].At)->second;
        if (Visits.size() <= Bit)
          Visits.resize(Bit + 1);
        Visits.set(Bit);
      }
      for (unsigned Next : D.Nodes[Each].Next)
        if (D.Nodes[Next].Component != Component) {
          Add(D.Closures[D.Nodes[Next].Component]);
          bIncomplete |= D.Incomplete[D.Nodes[Next].Component];
          if (D.bVisits)
            Visits |= D.Visited[D.Nodes[Next].Component];
        }
      // What only the walk needs.
      D.Nodes[Each].Next = {};
      D.Nodes[Each].Records = {};
    }
    D.Closures.push_back(std::move(Records));
    D.Incomplete.push_back(bIncomplete);
    if (D.bVisits) {
      D.VisitedBits += Visits.size();
      if (D.VisitedBits > MaxVisitedBits)
        D.Shared = false;
      D.Visited.push_back(std::move(Visits));
    }
  }
  if (!D.Shared)
    return std::nullopt;
  D.Starts.try_emplace({E, Level}, *Start);
  return D.Nodes[*Start].Component;
}

// One transition of reach from \p At at \p AtLevel, of \p Part: \p Next is
// told each expression it is made of at the level it is reached at, by the
// effects of \p At; \p Complete is cleared where that can't be told. What it
// takes of what it is at is told to reach (EProject).
void CProvenance::step(
    const Expr *At, unsigned AtLevel, bool Part,
    llvm::function_ref<void(const Expr *Next, unsigned Level, bool Part)> Next,
    bool &Complete) const {
  const CEffects &Of = effectsOf(At, Context);
  // What the way takes of the level it is at, which a slot it reads is read
  // by, `Handler.m_pValue` of what `Handler.f_Get()` gives.
  const CProjection *Taken =
      Projections && !Projections->empty() ? &Projections->back() : nullptr;
  auto Taking = [&](EProject Kind, const FieldDecl *Field = nullptr,
                    const CReturnedRoot *Root = nullptr,
                    llvm::ArrayRef<const FieldDecl *> Path = {}) {
    Project = Kind;
    ProjectField = Field;
    ProjectRoot = Root;
    ProjectPath.assign(Path.begin(), Path.end());
    ProjectIndex.reset();
    ProjectStride = 0;
  };
  // What a local slot holds where it is read (localSlotValues), at \p
  // Level: a value given it, a level down of a value stored, a pointer, at
  // its level of a record or an array; what is stored where a location is
  // at the same level, also what an array is made with; of a reference
  // member what it is bound to, at the same level. Of what the way takes of
  // it, that part (CProjection), not of a reference, whose referent it is.
  // Read where the read it is reached for is (reach). What a parameter is
  // given, read through a member of \p Entry, a name of it, `_Span.m_pData`,
  // is that name, of the part read, where reach tells what it holds
  // (holdsGiven); else what it is given, and what can't be told of it
  // (CSlotValues::Told), is an alternative that ends there, not a value
  // followed.
  TCRecursive<void(CSlot, bool, unsigned, bool, const Expr *)> StoredIn;
  auto Stored = [&](CSlot Slot, bool Bound, unsigned Level,
                    const Expr *Entry = nullptr) {
    StoredIn(std::move(Slot), Bound, Level, true, Entry);
  };
  StoredIn = [&](CSlot Slot, bool Bound, unsigned Level, bool Parts,
                 const Expr *Entry) {
    const bool Projected = !Bound && Taken && !Slot.Element;
    const bool Whole = !Bound && !Slot.Element && (!Taken || Taken->Path.empty());
    if (Projected) {
      llvm::append_range(Slot.Path, Taken->Path);
      // An element of a constant index of an array or a container the
      // slot holds, its own slot (ownedElement), `View.m_pJobs[0]`.
      CSlot Element = Slot;
      Element.Element = true;
      Element.Index = Taken->Index;
      if (Taken->Element && Taken->Index && ownedElement(Element, nullptr))
        Slot = std::move(Element);
    }
    const Stmt *Read = ReadAt ? ReadAt : At;
    // Of the whole of it, each part assigned on its own,
    // `Result.m_pValue = &Value;`, by what that part holds there; a member,
    // also of a member, that holds only what it is assigned on its own there,
    // not what the whole was given, `Result.m_pData = g_Output;` after
    // `CIoSpan Result = _Span;`, or `Result.m_Data.m_pData = nullptr;` after
    // `CReadResult Result{_Span};`, isn't what the whole holds.
    const llvm::SmallVector<CSlot, 2> Written =
        Parts && Whole ? localSlotParts(Slot, Read, Context)
                       : llvm::SmallVector<CSlot, 2>();
    const QualType Held = Slot.Path.empty()
                              ? Slot.Variable->getType().getNonReferenceType()
                              : Slot.Path.back()->getType().getNonReferenceType();
    // Of an object, the members of it the whole holds what it is given in:
    // each that can hold a reference and isn't replaced, of one some parts
    // of which are assigned on their own each of its members, recursively;
    // relative to it (forEachMember). Of one whose members can't be told,
    // the whole.
    const RecordDecl *Record =
        llvm::any_of(Written,
                     [&](const CSlot &Each) {
                       return !localSlotWholeReaches(Each, Read, Context);
                     })
            ? Held->getAsRecordDecl()
            : nullptr;
    llvm::SmallVector<llvm::SmallVector<const FieldDecl *, 2>, 4> Holding;
    if (Record) {
      CSlot Part = Slot;
      TCRecursive<bool(const RecordDecl *)> Members =
          [&](const RecordDecl *Of) {
            return forEachMember(Of, [&](const FieldDecl *Field) {
              if (!holdsReference(Field->getType()))
                return;
              Part.Path.push_back(Field);
              const bool Replaced =
                  llvm::any_of(Written,
                               [&](const CSlot &Each) {
                                 return Each.Path == Part.Path;
                               }) &&
                  !localSlotWholeReaches(Part, Read, Context);
              const bool Inside = llvm::any_of(Written, [&](const CSlot &Each) {
                return Each.Path.size() > Part.Path.size() &&
                       startsWith(Each.Path, Part.Path);
              });
              const RecordDecl *Inner = Field->getType()->isReferenceType()
                                            ? nullptr
                                            : Field->getType()->getAsRecordDecl();
              if (!Replaced) {
                const size_t Before = Holding.size();
                if (!Inside || !Inner || Part.Path.size() >= 16 ||
                    !Members(Inner)) {
                  Holding.resize(Before);
                  Holding.emplace_back(Part.Path.begin() + Slot.Path.size(),
                                       Part.Path.end());
                }
              }
              Part.Path.pop_back();
            });
          };
      if (!Members(Record))
        Record = nullptr;
    }
    const CSlotValues Values = localSlotValues(Slot, Read, Context);
    bool Unknown = !Values.Told;
    for (const CSlotValue &Value : Values.Values) {
      switch (Value.Kind) {
      case CSlotValue::EKind::Value:
        Hop = Bound ? EHop::None : EHop::Held;
        if (Record && Value.Value->getType()->getAsRecordDecl() == Record) {
          // Each member of what the whole is given that it holds.
          for (const llvm::SmallVector<const FieldDecl *, 2> &Member : Holding) {
            Taking(EProject::Path, nullptr, nullptr, Member);
            Next(Value.Value, heldLevel(Value.Value->getType(), Level), Part);
          }
          break;
        }
        if (Projected)
          Taking(EProject::Clear);
        Next(Value.Value,
             Bound ? Level : heldLevel(Value.Value->getType(), Level), Part);
        break;
      // What it held before it is stepped, the slot itself, or what an
      // array is made with.
      case CSlotValue::EKind::Stored:
        Hop = EHop::HeldBefore;
        if (!Bound)
          Taking(Value.Value->isGLValue() ? EProject::Stepped
                                          : EProject::Clear);
        Next(Value.Value, Level, Part);
        break;
      // The address of what a setter's source designates, a level down; of
      // a part of it, an element of what it designates there.
      case CSlotValue::EKind::Address:
        Hop = Bound ? EHop::None : EHop::Held;
        if (Value.Part)
          Taking(EProject::Stepped);
        else if (Projected)
          Taking(EProject::Clear);
        if (Level > 0)
          Next(Value.Value, Level - 1, Part);
        break;
      case CSlotValue::EKind::Given:
        if (!Entry || Bound) {
          Unknown = true;
          break;
        }
        if (holdsReference(Held)) {
          Taking(EProject::Path, nullptr, nullptr, Slot.Path);
          Next(Entry, Level, Part);
        }
        break;
      }
      Hop = EHop::None;
      if (!Bound)
        Taking(EProject::None);
    }
    if (Unknown)
      Next(nullptr, Level, Part);
    for (const CSlot &Each : Written)
      StoredIn(Each, false, Level, false, Entry);
  };
  switch (Of.Statement) {
  // A name, by what it names (holderKind): a structured binding by what
  // it binds; a local slot above level 0, also through a local
  // reference, by what it holds where it is read (designatedSlots); a
  // reference to what isn't one by what it is bound to; the rest end.
  case EStatement::Name: {
    const ValueDecl *Named = cast<DeclRefExpr>(At)->getDecl();
    switch (holderKind(Named)) {
    case EHolder::Binding:
      Hop = EHop::Binding;
      Next(cast<BindingDecl>(Named)->getBinding(), AtLevel, Part);
      Hop = EHop::None;
      return;
    case EHolder::Variable:
    case EHolder::Decomposition:
    case EHolder::Parameter: {
      const auto *Variable = cast<VarDecl>(Named);
      const bool Reference = Variable->getType()->isReferenceType();
      if (Reference && isa<ParmVarDecl>(Variable))
        return;
      // A copy a closure keeps of a reference, where it is named in
      // the closure, is its own slot (closureCopying), at level 0 the
      // closure's storage.
      if (Reference && closureCopying(Variable, At)) {
        if (AtLevel > 0)
          Stored({Variable, {}, false}, false, AtLevel);
        return;
      }
      if (Reference && (AtLevel == 0 || !Variable->hasLocalStorage())) {
        Next(Variable->getInit(), AtLevel, Part);
        return;
      }
      if (!Variable->hasLocalStorage() && !Variable->isInitCapture())
        return;
      if (AtLevel == 0)
        return;
      bool Told = true;
      const llvm::SmallVector<CSlot, 2> Slots =
          designatedSlots(At, Context, &Told);
      for (const CSlot &Slot : Slots)
        Stored(Slot, false, AtLevel);
      if (Reference && (!Told || Slots.empty()))
        Next(Variable->getInit(), AtLevel, Part);
      return;
    }
    case EHolder::None:
    case EHolder::ImplicitParameter:
    case EHolder::Field:
    case EHolder::IndirectField:
    case EHolder::Temporary:
    case EHolder::Constant:
    case EHolder::Function:
    case EHolder::Statement:
    case EHolder::Opaque:
      return;
    }
    return;
  }
  // A member: a part of its object at level 0; above, of a slot, of a
  // local or what a local pointer points to, what it holds where it is
  // read (localSlotValues), of a temporary aggregate what it is made
  // with (localMemberInit), else what its object holds, of which it is a
  // part. A reference member is what it is bound to, else can't be told.
  case EStatement::Member: {
    const ValueDecl *Member = memberDecl(At, Context);
    switch (holderKind(Member)) {
    case EHolder::Field:
    case EHolder::IndirectField:
      break;
    case EHolder::Variable:
    case EHolder::None:
    case EHolder::Parameter:
    case EHolder::ImplicitParameter:
    case EHolder::Decomposition:
    case EHolder::Binding:
    case EHolder::Temporary:
    case EHolder::Constant:
    case EHolder::Function:
    case EHolder::Statement:
    case EHolder::Opaque:
      return;
    }
    const bool Reference = Member->getType()->isReferenceType();
    if (!Reference && AtLevel == 0)
      break;
    const auto *Selected = cast<MemberExpr>(At);
    const auto *Field = dyn_cast<FieldDecl>(Member);
    llvm::SmallVector<CSlot, 2> Slots =
        !Field ? llvm::SmallVector<CSlot, 2>()
        : Selected->isArrow()
            ? pointedSlots(Selected->getBase(), Context)
            : designatedSlots(Selected->getBase(), Context);
    llvm::erase_if(Slots, [](const CSlot &Slot) { return Slot.Element; });
    // Of a parameter, what it was given is told by its name (StoredIn).
    const Expr *Base = Selected->getBase()->IgnoreParens();
    const ValueDecl *Named =
        !Selected->isArrow() &&
                effectsOf(Base, Context).Statement == EStatement::Name
            ? cast<DeclRefExpr>(Base)->getDecl()
            : nullptr;
    for (CSlot &Slot : Slots) {
      Slot.Path.push_back(Field);
      Stored(Slot, Reference, AtLevel, Named == Slot.Variable ? Base : nullptr);
    }
    if (!Slots.empty())
      return;
    if (const Expr *Made = Selected->isArrow() ? nullptr : localMemberInit(At))
      Next(Made, Reference ? AtLevel : heldLevel(Made->getType(), AtLevel),
           false);
    else if (!Reference) {
      Taking(Field ? EProject::Field : EProject::Element, Field);
      Next(Selected->getBase(), AtLevel, true);
      Taking(EProject::None);
    } else
      Next(nullptr, AtLevel, Part);
    return;
  }
  case EStatement::Other:
  case EStatement::This:
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
    break;
  }
  // An object a constructor makes, at a level above 0: of the member the way
  // takes, else of each, what the constructor gives it of its arguments
  // (projected), a reference as a pointer.
  if (const auto *Construct = Of.Statement == EStatement::Construction
                                  ? dyn_cast<CXXConstructExpr>(At)
                                  : nullptr;
      Construct && AtLevel > 0 && Construct->getConstructor()) {
    auto Member = [&](const FieldDecl *Field, bool Taken) {
      const Expr *Given = projected(Construct, {Field});
      if (!Given)
        return;
      if (Taken)
        Taking(EProject::Consume);
      Next(Given,
           Field->getType()->isReferenceType()
               ? (Taken ? AtLevel : AtLevel - 1)
               : heldLevel(Given->getType(), AtLevel),
           Part);
      Taking(EProject::None);
    };
    if (Taken && !Taken->Path.empty())
      Member(Taken->Path.front(), true);
    else
      for (const FieldDecl *Field :
           Construct->getConstructor()->getParent()->fields())
        Member(Field, false);
  }
  const auto *Call =
      Of.Statement == EStatement::Call ? cast<CallExpr>(At) : nullptr;
  // What a copy or a move assignment the compiler defines gives its object,
  // at a level above 0, what it copies, as a copy constructor the compiler
  // defines gives a new one (copiedByDefinedAssignment).
  if (Call && AtLevel > 0)
    if (const Expr *Copied = copiedByDefinedAssignment(Call, Context)) {
      Next(Copied, AtLevel, Part);
      return;
    }
  // What a function gives back of what it is called with, as each way of
  // each of its returns ends (returnedRoots): its object, what a parameter
  // is given, a reference by what it is bound to, a value by what it is
  // given, a level down of a pointer; what isn't of it, a capture by
  // reference, as named there; of a reference member of one of those, what
  // it is bound to; a way that ends elsewhere, of a walk that tells every
  // alternative (setOtherEnds), an alternative that ends.
  // Of each body the call can run, each override where one can run instead
  // (calledRoots). Not what only gives back what it is given
  // (givesItsArgument, keptPointerArgument), nor a body that can't be seen,
  // below.
  CCalledRoots Called;
  if (Call && Of.Callee && !givesItsArgument(Call) &&
      !keptPointerArgument(Call))
    Called = calledRoots(Call, Of.Callee);
  if (!Called.Bodies.empty()) {
    const COperandEffect *ObjectOf = nullptr;
    for (const COperandEffect &Effect : Of.Operands)
      if (Effect.Use == EUse::CallObject)
        ObjectOf = &Effect;
    // What \p Root is of \p Object, at \p Level.
    auto Each = [&](const Expr *Object, const CReturnedRoot &Root,
                    unsigned Level) {
      if (!Object) {
        Next(nullptr, Level, Part);
        return;
      }
      if (Root.Bound.empty()) {
        Next(Object, Level, Part || Root.Part);
        return;
      }
      bool Told = true;
      llvm::SmallVector<CSlot, 2> Slots =
          Object->isGLValue() ? designatedSlots(Object, Context, &Told)
                              : pointedSlots(Object, Context, &Told);
      llvm::erase_if(Slots, [](const CSlot &Slot) { return Slot.Element; });
      for (CSlot &Slot : Slots) {
        llvm::append_range(Slot.Path, Root.Bound);
        Stored(Slot, true, Level);
      }
      if (!Told || Slots.empty())
        Next(nullptr, Level, Part);
    };
    // Each way of each body.
    llvm::SmallVector<const CReturnedRoot *, 4> Ways;
    for (const auto &Body : Called.Bodies)
      for (const CReturnedRoot &Root : Body.second->Roots)
        Ways.push_back(&Root);
    for (const CReturnedRoot *Way : Ways) {
      const CReturnedRoot &Root = *Way;
      Taking(EProject::Returned, nullptr, &Root);
      switch (Root.Kind) {
      case CReturnedRoot::EKind::Object:
        Each(ObjectOf ? dyn_cast<Expr>(ObjectOf->Operand) : nullptr, Root,
             AtLevel + Root.Level);
        break;
      case CReturnedRoot::EKind::Parameter: {
        const COperandEffect *Given = nullptr;
        for (const COperandEffect &Effect : Of.Operands)
          // Also the explicit object, which the call is on (CallObject).
          if ((Effect.Use == EUse::Bind || Effect.Use == EUse::Copy ||
               Effect.Use == EUse::CallObject) &&
              Effect.Into.Kind == EDestination::Parameter &&
              Effect.Into.Index == Root.Index)
            Given = &Effect;
        const auto *Parameter =
            Given ? dyn_cast_or_null<ParmVarDecl>(Given->Into.Declaration)
                  : nullptr;
        if (!Parameter) {
          Next(nullptr, AtLevel, Part);
          break;
        }
        const QualType Type = Parameter->getType();
        const unsigned Level = AtLevel + Root.Level;
        if (Type->isReferenceType())
          Each(dyn_cast<Expr>(Given->Operand), Root, Level);
        // Of one held by value, what it is given, of a pointer a level down;
        // its own storage, past its function, isn't.
        else if (Level > 0 || Type->isRecordType() || Type->isArrayType())
          Each(dyn_cast<Expr>(Given->Operand), Root, heldLevel(Type, Level));
        break;
      }
      case CReturnedRoot::EKind::Outer:
      case CReturnedRoot::EKind::Static:
        Each(Root.Name, Root, AtLevel + Root.Level);
        break;
      }
      Taking(EProject::None);
    }
    if (bOtherEnds && llvm::any_of(Called.Bodies, [](const auto &Body) {
          return Body.second->Other;
        }))
      Next(nullptr, AtLevel, Part);
  }
  // An element of what a pointer points to, `pValues[1]`, is a part.
  // Not the operand it gives on itself, the object a dynamic cast checks.
  // The operands it gives on, and the objects of the locations its operands
  // are given (EDestination::Designated), each found once for all of its
  // operands.
  llvm::SmallPtrSet<const Stmt *, 4> Forwarded;
  llvm::SmallPtrSet<const Expr *, 4> DesignatedObjects;
  for (const COperandEffect &Effect : Of.Operands) {
    if (Effect.Use == EUse::Forward)
      Forwarded.insert(Effect.Operand);
    if (Effect.Into.Kind == EDestination::Designated)
      DesignatedObjects.insert(Effect.Into.Object);
  }
  const COperandEffect *Index = nullptr;
  for (const COperandEffect &Effect : Of.Operands)
    if (Effect.Use == EUse::Value && !Forwarded.contains(Effect.Operand))
      Index = &Effect;
  const bool Indexed = Index != nullptr;
  // The element it takes of what \p Stepped, a pointer or an array, points
  // to, of a constant index, that index, and the size of what it steps over.
  auto TakingIndexed = [&](const Expr *Stepped) {
    Taking(EProject::Element);
    if (const std::optional<llvm::APSInt> Constant = constantIntegerOf(
            dyn_cast_or_null<Expr>(Index->Operand), Context))
      ProjectIndex = signedValueOf(*Constant);
    const QualType Type = Stepped->IgnoreParenImpCasts()->getType();
    QualType Over;
    if (Type->isPointerType())
      Over = Type->getPointeeType();
    else if (const ArrayType *Array = Type->getAsArrayTypeUnsafe())
      Over = Array->getElementType();
    if (!Over.isNull() && !Over->isDependentType() &&
        !Over->isIncompleteType() && !Over->isSizelessType() &&
        !Over->isFunctionType())
      ProjectStride = Context.getTypeSizeInChars(Over).getQuantity();
  };
  // Whether it steps \p Operand, a pointer, by the constant 0, `p[0]` or `p +
  // 0`, which is what the pointer is, `*p` of `p[0]`: no element of what it
  // points to, of an object that isn't an array the object itself. Not of an
  // array, whose element 0 it takes.
  auto ZeroStep = [&](const Expr *Operand) {
    if (!Indexed || Operand->IgnoreParenImpCasts()->getType()->isArrayType())
      return false;
    const auto *Value = dyn_cast_or_null<Expr>(Index->Operand);
    const std::optional<llvm::APSInt> Constant =
        Value ? constantIntegerOf(Value, Context) : std::nullopt;
    return Constant && Constant->isZero();
  };
  for (const COperandEffect &Effect : Of.Operands) {
    const Expr *Operand = dyn_cast_or_null<Expr>(Effect.Operand);
    // What a block gives on, the last expression of a statement expression,
    // `({ &Value; })`.
    for (const Stmt *Given = Effect.Operand;
         !Operand && Given && Effect.Use == EUse::Forward;) {
      const Stmt *Last = nullptr;
      for (const COperandEffect &Inner : effectsOf(Given, Context).Operands)
        if (Inner.Use == EUse::Forward)
          Last = Inner.Operand;
      Given = Last;
      Operand = dyn_cast_or_null<Expr>(Last);
    }
    if (!Operand)
      continue;
    switch (Effect.Use) {
    // The same location or value, also a part of it: a member, a base,
    // or a step of a pointer. Not the location an assignment gives
    // back, loaded, which is what it is given (EDestination::
    // Designated), not what was stored there before.
    case EUse::Forward:
      if (AtLevel > 0 && DesignatedObjects.contains(Operand))
        break;
      if (Of.Statement == EStatement::Member) {
        const auto *Field = dyn_cast_or_null<FieldDecl>(memberDecl(At, Context));
        Taking(Field ? EProject::Field : EProject::Element, Field);
      } else if (Indexed && !ZeroStep(Operand))
        TakingIndexed(Operand);
      Next(Operand, AtLevel,
           Part || Of.Statement == EStatement::Member ||
               (At->isGLValue() &&
                At->getType().getCanonicalType().getUnqualifiedType() !=
                    Operand->getType()
                        .getCanonicalType()
                        .getUnqualifiedType()) ||
               (!At->isGLValue() && Indexed && !ZeroStep(Operand)));
      Taking(EProject::None);
      break;
    // A load: what the value stored there points to, of a part of
    // that, the same part, `(*pArray)[0]`.
    case EUse::Access:
      Hop = EHop::Load;
      Next(Operand, AtLevel + 1, Part);
      Hop = EHop::None;
      break;
    // The value a postfix step gives is the one loaded; a prefix one
    // gives the location (EUse::Forward).
    case EUse::Update:
      if (At->isPRValue()) {
        Hop = EHop::Load;
        Next(Operand, AtLevel + 1, false);
        Hop = EHop::None;
      }
      break;
    // What a pointer points to, as a glvalue, the whole of it, an element
    // of it, or a member of it, `pContext->m_pJob`, as of `.`.
    case EUse::Dereference:
      if (At->isGLValue()) {
        const bool Member = Of.Statement == EStatement::Member;
        if (Member) {
          const auto *Field =
              dyn_cast_or_null<FieldDecl>(memberDecl(At, Context));
          Taking(Field ? EProject::Field : EProject::Element, Field);
        } else if (Indexed && !ZeroStep(Operand))
          TakingIndexed(Operand);
        Next(Operand, AtLevel,
             Part || (Indexed && !ZeroStep(Operand)) || Member);
        Taking(EProject::None);
      }
      break;
    // Its address, of the whole of it, `&Values`, or of its first
    // element, of an array that decays, `Values`, a part; of an element the
    // way takes already of what the array decays to, `Values[1]` or
    // `*(Values + 1)`, that element of the array, with its index.
    case EUse::AddressOf: {
      const bool Decays =
          !At->getType()->getPointeeType().isNull() &&
          At->getType()->getPointeeType().getCanonicalType().getUnqualifiedType() !=
              Operand->getType().getCanonicalType().getUnqualifiedType();
      if (Decays && !(Taken && Taken->Element && Taken->Path.empty()))
        Taking(EProject::Element);
      Next(Operand, AtLevel, Part || Decays);
      Taking(EProject::None);
      break;
    }
    case EUse::Bind:
    case EUse::Copy:
      switch (Effect.Into.Kind) {
      // A temporary: what a glvalue, an object or an array given it is;
      // of a value, a pointer, the temporary is its storage, which stores
      // it, a level down. A `std::initializer_list` refers to the array it
      // is made of, a level down, as a pointer does.
      case EDestination::Temporary:
        if (Effect.Into.Object != At)
          break;
        if (Of.Statement == EStatement::InitializerList && AtLevel > 0)
          Next(Operand, AtLevel - 1, false);
        else if (Operand->isGLValue() || Operand->getType()->isRecordType() ||
                 Operand->getType()->isArrayType())
          Next(Operand, AtLevel, Part);
        else if (AtLevel > 0)
          Next(Operand, AtLevel - 1, false);
        break;
      // The elements of an array, at a level above 0: what each
      // stores, of an array or a record copied what it holds.
      case EDestination::Element:
        if (Effect.Into.Object == At && AtLevel > 0 &&
            At->getType()->isArrayType())
          Next(Operand, heldLevel(Operand->getType(), AtLevel), false);
        break;
      // The members of an aggregate made, at a level above 0: of the
      // member the way takes, what it is made with, else what each holds,
      // a reference as a pointer.
      case EDestination::Field: {
        if (Effect.Into.Object != At || AtLevel == 0)
          break;
        const auto *Field =
            dyn_cast_or_null<FieldDecl>(Effect.Into.Declaration);
        const bool Reference = Field && Field->getType()->isReferenceType();
        if (Taken && !Taken->Path.empty()) {
          if (Taken->Path.front() != Field)
            break;
          Taking(EProject::Consume);
          Next(Operand,
               Reference ? AtLevel : heldLevel(Operand->getType(), AtLevel),
               Part);
          Taking(EProject::None);
          break;
        }
        Next(Operand,
             Reference ? AtLevel - 1 : heldLevel(Operand->getType(), AtLevel),
             false);
        break;
      }
      // What only gives back what it is given: of `std::addressof`, of
      // MoveFunctions and ForwardFunctions, also without a body, and the
      // builtins like `std::move`, the first (givesItsArgument); what only
      // the compiler looks at, the pointer it is given,
      // `__builtin_assume_aligned(pValue, 16)` (keptPointerArgument).
      case EDestination::Parameter:
        if (!Call || !Of.Callee)
          break;
        if (givesItsArgument(Call)) {
          if (Effect.Into.Index == 0)
            Next(Operand, AtLevel, Part);
          break;
        }
        if (const Expr *Kept = keptPointerArgument(Call))
          if (Kept == Operand)
            Next(Operand, AtLevel, Part);
        break;
      // What an assignment gives back, loaded, is what it is given.
      case EDestination::Designated:
        if (AtLevel > 0 &&
            llvm::any_of(Of.Operands, [&](const COperandEffect &Own) {
              return Own.Use == EUse::Forward &&
                     Own.Operand == Effect.Into.Object;
            }))
          Next(Operand, AtLevel - 1, false);
        break;
      // A base of an aggregate made, at a level above 0: what it is made
      // with holds what its members do.
      case EDestination::Base:
        if (Effect.Into.Object == At && AtLevel > 0)
          Next(Operand, AtLevel, Part);
        break;
      case EDestination::None:
      case EDestination::Variable:
      case EDestination::Capture:
      case EDestination::Return:
      case EDestination::Heap:
      case EDestination::Thrown:
        break;
      }
      break;
    // What a body that can't be seen gives (calledRoots), of a reference or
    // a pointer, is taken as a part of the object, which it can't be told
    // what of (bUnseenObjectPart).
    case EUse::CallObject:
      if (!Called.open() || !bUnseenObjectPart || !Call ||
          !dyn_cast_or_null<CXXMethodDecl>(Of.Callee) ||
          givesItsArgument(Call) || keptPointerArgument(Call))
        break;
      if (Of.Callee->getReturnType()->isReferenceType() ||
          Of.Callee->getReturnType()->isPointerType()) {
        Taking(EProject::Element);
        Next(Operand, AtLevel, true);
        Taking(EProject::None);
      }
      break;
    case EUse::Opaque:
      Complete = false;
      break;
    case EUse::Write:
    case EUse::Callee:
    case EUse::Condition:
    case EUse::Value:
    case EUse::Discarded:
    case EUse::Statement:
    case EUse::Unevaluated:
      break;
    }
  }
}

// What each way of each return of \p Function ends at (CReturnedRoots), found
// once for it; null of a body that can't be seen, a coroutine, or one being
// found.
const CReturnedRoots *
CProvenance::returnedRoots(const FunctionDecl *Function) const {
  const FunctionDecl *Definition = nullptr;
  if (!Function || !Function->hasBody(Definition) || !Definition ||
      !Definition->getBody() || isa<CoroutineBodyStmt>(Definition->getBody()))
    return nullptr;
  const std::optional<CReturnedRoots> &Roots =
      sharedFlowCache().summaries(Context).ReturnedRoots.get(Definition);
  return Roots ? &*Roots : nullptr;
}

CCalledRoots CProvenance::calledRoots(const CallExpr *Call,
                                      const FunctionDecl *Callee) const {
  CCalledRoots Called;
  llvm::SmallVector<const FunctionDecl *, 2> Bodies{Callee};
  if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
      Method && mayRunOverride(Call, Method)) {
    Called.bOverridable = true;
    Bodies.clear();
    for (const CXXMethodDecl *Override :
         dispatchedMethods(CMemberCall::of(Call, /*Operators=*/true)))
      if (!Override->isPureVirtual())
        Bodies.push_back(Override);
  }
  for (const FunctionDecl *Body : Bodies) {
    if (const CReturnedRoots *Of = returnedRoots(Body))
      Called.Bodies.push_back({Body, Of});
    else
      Called.bUnseen = true;
  }
  return Called;
}

// What each way of each return of \p Definition ends at (returnedRoots),
// worked out once (CFlowSummaries::ReturnedRoots); none, which can't be told,
// while it is.
static std::optional<CReturnedRoots>
returnedRootsUncached(const FunctionDecl *Definition, ASTContext &Context) {
  llvm::SmallVector<std::pair<const Expr *, unsigned>, 4> Values;
  for (const Expr *Value : writtenReturns(Definition, Context))
    Values.push_back({Value, 0});
  return valueRoots(Definition, Values, Context);
}

// What each way of each of \p Values, an expression of \p Definition at the
// level paired with it, ends at (CReturnedRoots): its object, a parameter, a
// local of what isn't the function, static storage.
static CReturnedRoots valueRoots(
    const FunctionDecl *Definition,
    llvm::ArrayRef<std::pair<const Expr *, unsigned>> Values,
    ASTContext &Context, bool UntilOther) {
  auto Found = std::make_unique<CReturnedRoots>();
  // What \p At, reached by \p Walk at \p Level, of \p Part, is: its object,
  // a parameter, also by what a parameter held by value is assigned where it
  // is read, and by what it is given when that can reach there too
  // (assignedAt); a local slot of what isn't the function, which a closure
  // refers to by reference; what a reference member of one of those is bound
  // to, of each way to it. Added to \p Out; none of what isn't one of them.
  TCRecursive<std::optional<EReach>(const Expr *, unsigned, bool,
                                      const CProvenance &,
                                      llvm::SmallVectorImpl<CReturnedRoot> &)> RootOf = [&](const Expr *At, unsigned Level, bool Part,
                   const CProvenance &Walk,
                   llvm::SmallVectorImpl<CReturnedRoot> &Out)
      -> std::optional<EReach> {
    auto Add = [&](CReturnedRoot Root) {
      Root.Level = Level;
      Root.Part = Part;
      Root.Projections.assign(Walk.projections().begin(),
                              Walk.projections().end());
      Out.push_back(std::move(Root));
    };
    // Of a lambda, `this` is what it captures, the object of the function
    // it is made in, as named there, not the closure.
    const auto *Method = dyn_cast<CXXMethodDecl>(Definition);
    const bool Lambda = Method && Method->getParent()->isLambda();
    // What refers to nothing adds no way, the identity of the merge; what
    // has static storage is a static root (leafOf).
    if (refersToNothing(At, Level))
      return EReach::Root;
    switch (leafOf(At)) {
    case ELeaf::Static: {
      CReturnedRoot Root;
      Root.Kind = CReturnedRoot::EKind::Static;
      Root.Name = At;
      Add(std::move(Root));
      return EReach::Root;
    }
    case ELeaf::Nothing:
    case ELeaf::Followed:
      break;
    }
    switch (effectsOf(At, Context).Statement) {
    case EStatement::This:
      if (Lambda) {
        CReturnedRoot Root;
        Root.Kind = CReturnedRoot::EKind::Outer;
        Root.Name = At;
        Add(std::move(Root));
        return EReach::Root;
      }
      Add({});
      return EReach::Root;
    case EStatement::Name: {
      const auto *Variable = dyn_cast<VarDecl>(cast<DeclRefExpr>(At)->getDecl());
      if (!Variable)
        return std::nullopt;
      // A reference of the function a lambda is made in, which it refers to
      // as it is, `[&_Value] { return &_Value; }`, as named there.
      if (Lambda && Variable->getType()->isReferenceType() &&
          Variable->hasLocalStorage() &&
          Variable->getDeclContext() != Definition &&
          !closureCopying(Variable, At)) {
        CReturnedRoot Root;
        Root.Kind = CReturnedRoot::EKind::Outer;
        Root.Name = At;
        Add(std::move(Root));
        return EReach::Root;
      }
      if (const auto *Parameter = dyn_cast<ParmVarDecl>(Variable);
          Parameter && Parameter->getDeclContext() == Definition) {
        bool Given = true;
        bool Assigned = false;
        if (!Parameter->getType()->isReferenceType() && Level > 0)
          Assigned = !assignedAt(Parameter, At, Context, &Given).empty();
        if (Given) {
          CReturnedRoot Root;
          Root.Kind = CReturnedRoot::EKind::Parameter;
          Root.Index = Parameter->getFunctionScopeIndex();
          Add(std::move(Root));
        }
        return Assigned ? EReach::Continue : EReach::Root;
      }
      if (isSlotVariable(Variable) && !closureCopying(Variable, At) &&
          slotFunction(Variable, At) != Definition) {
        CReturnedRoot Root;
        Root.Kind = CReturnedRoot::EKind::Outer;
        Root.Name = At;
        Add(std::move(Root));
        return EReach::Root;
      }
      // What has static storage, not of a thread, `static CConfig const
      // s_Config{5}; return s_Config;`.
      // Not a part that can't be told, what a member function whose body
      // can't be seen gives of it, only an element of an array.
      const llvm::ArrayRef<CProjection> Taken = Walk.projections();
      if (Variable->hasGlobalStorage() &&
          Variable->getTLSKind() == VarDecl::TLS_None && Level == 0 &&
          !Variable->getType()->isReferenceType() &&
          (Taken.empty() || !Taken.front().Element ||
           Variable->getType()->isArrayType())) {
        CReturnedRoot Root;
        Root.Kind = CReturnedRoot::EKind::Static;
        Root.Name = At;
        Add(std::move(Root));
        return EReach::Root;
      }
      return std::nullopt;
    }
    case EStatement::Member: {
      const auto *Field = dyn_cast_or_null<FieldDecl>(memberDecl(At, Context));
      if (!Field || !Field->getType()->isReferenceType())
        return std::nullopt;
      // Of its object at level 0, each way, the whole of a root that isn't
      // itself of a reference member.
      CProvenance Sub(Context);
      Sub.setOtherEnds(true);
      llvm::SmallVector<CReturnedRoot, 2> Bases;
      bool Ended = false;
      const bool Complete = Sub.reach(
          cast<MemberExpr>(At)->getBase(), 0,
          [&](const Expr *Base, unsigned BaseLevel, bool BasePart) {
            if (BaseLevel > 0)
              return EReach::Stop;
            const std::optional<EReach> Reach =
                RootOf(Base, 0, BasePart, Sub, Bases);
            return Reach ? *Reach : EReach::Continue;
          },
          &Ended);
      if (Ended || !Complete || Bases.empty() ||
          llvm::any_of(Bases, [](const CReturnedRoot &Base) {
            return !Base.Bound.empty() || Base.Projections.size() != 1 ||
                   Base.Projections.back().Element;
          }))
        return std::nullopt;
      for (CReturnedRoot &Base : Bases) {
        CReturnedRoot Root;
        Root.Kind = Base.Kind;
        Root.Index = Base.Index;
        Root.Name = Base.Name;
        Root.Bound.assign(Base.Projections.back().Path.begin(),
                          Base.Projections.back().Path.end());
        Root.Bound.push_back(Field);
        Add(std::move(Root));
      }
      return EReach::Root;
    }
    case EStatement::Other:
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
    return std::nullopt;
  };
  for (const auto &[Value, Level] : Values) {
    CProvenance Walk(Context);
    Walk.setOtherEnds(true);
    Walk.setStopAtEnd(UntilOther);
    bool Ended = false;
    const bool Complete = Walk.reach(
        Value, Level,
        [&](const Expr *At, unsigned Level, bool Part) {
          const std::optional<EReach> Reach =
              RootOf(At, Level, Part, Walk, Found->Roots);
          return Reach ? *Reach : EReach::Continue;
        },
        &Ended);
    Found->Other |= Ended || !Complete;
  }
  return std::move(*Found);
}

// The slots what \p E designates at level 0 is (designatedSlots), by
// provenance (CProvenance::reach): where a way ends at a local slot at level
// 0, the slot, by the members and the element the way takes of it
// (CProjection); also a copy a closure keeps of a reference, and a temporary
// a local reference extends, as that reference, its own storage. A null
// pointer and a temporary that isn't extended designate none; a way that ends
// elsewhere, or, without \p Values, past a load, which what pointers hold
// isn't asked of, clears \p Told.
namespace {
/// Of each pointer parameter, whether it holds what it is given wherever it is
/// read (holdsGiven), once told.
struct CGivenParameters {
  llvm::DenseMap<const ParmVarDecl *, bool> Of;
};
} // namespace

// Whether the pointer parameter \p Parameter holds what it is given wherever
// it is read: no name of it in the body of its function, nor in the body of a
// closure made there, is given a value, stepped, given its address or bound
// to a reference that can change it, by what each use of a name does with it
// (effectsOf). Told without the flow of the function, which asks what the
// parameter points to (designationOf).
static bool holdsGiven(const ParmVarDecl *Parameter, ASTContext &Context) {
  auto &Cache = astCache<CGivenParameters>(Context).Of;
  if (const auto It = Cache.find(Parameter); It != Cache.end())
    return It->second;
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Parameter->getDeclContext());
  bool Given = Function && Function->getBody();
  CFlowCache &Flows = sharedFlowCache();
  // The bodies the names are in: the function's, and those of the closures
  // made in each, each once.
  llvm::SmallVector<std::pair<const Decl *, const Stmt *>, 4> Bodies;
  if (Given)
    Bodies.push_back({Function, Function->getBody()});
  llvm::SmallPtrSet<const Stmt *, 4> Seen;
  while (Given && !Bodies.empty()) {
    const auto [Owner, Body] = Bodies.pop_back_val();
    if (!Seen.insert(Body).second)
      continue;
    const CEffectIndex &Index = Flows.indexOf(Body, Context);
    for (const Stmt *S : Index.statements())
      if (Index.effectsOf(S)->Statement == EStatement::Lambda)
        for (const FunctionDecl *Operator :
             callOperatorsOf(cast<LambdaExpr>(S)))
          Bodies.push_back({Operator, Operator->getBody()});
    for (const DeclRefExpr *Name :
         Flows.namesIn(Owner, Body, Parameter, Context)) {
      for (const CEffectIndex::CUse &Use : Index.consumersOf(Name)) {
        if (!Use.Effect) {
          Given = false;
          break;
        }
        switch (Use.Effect->Use) {
        case EUse::Write:
        case EUse::Update:
        case EUse::AddressOf:
        case EUse::Opaque:
          Given = false;
          break;
        case EUse::Bind: {
          // Of a reference that can change it; not a capture by reference
          // of a closure made here, whose body's names of it are told as
          // the function's are (Bodies).
          if (Use.Effect->Into.Kind == EDestination::Capture &&
              isa_and_nonnull<LambdaExpr>(Use.Effect->Into.Object))
            break;
          const auto *Into =
              dyn_cast_or_null<ValueDecl>(Use.Effect->Into.Declaration);
          const QualType Type = Into ? Into->getType() : QualType();
          Given = !Type.isNull() && Type->isReferenceType() &&
                  Type.getNonReferenceType().isConstQualified();
          break;
        }
        case EUse::Forward:
        case EUse::Access:
        case EUse::Dereference:
        case EUse::Copy:
        case EUse::CallObject:
        case EUse::Callee:
        case EUse::Condition:
        case EUse::Value:
        case EUse::Discarded:
        case EUse::Statement:
        case EUse::Unevaluated:
          break;
        }
        if (!Given)
          break;
      }
      if (!Given)
        break;
    }
  }
  Cache[Parameter] = Given;
  return Given;
}

static llvm::SmallVector<CSlot, 2> designationOf(const Expr *E,
                                                 EDesignation Mode,
                                                 ASTContext &Context,
                                                 bool &Told);
static const Expr *elementIndexOfCall(const Expr *Call);
static llvm::SmallVector<CSlot, 2> designation(const Expr *E,
                                               EDesignation Mode,
                                               ASTContext &Context,
                                               bool &Told) {
  // Worked out once for each (CFlowSummaries::Designations); asked inside
  // itself, a cycle through what slots hold, `p = p->m_pNext`, none, untold.
  if (!E) {
    Told = false;
    return {};
  }
  // Of the slots through what pointers hold, the places, each of which isn't
  // a slot clearing Told, as a way that ends there does.
  const CTargets &Found = sharedFlowCache().summaries(Context).Designations.get(
      {E, static_cast<unsigned>(Mode == EDesignation::Values
                                    ? EDesignation::Places
                                    : Mode)});
  Told = Found.Told;
  if (Mode != EDesignation::Values)
    return Found.Slots;
  llvm::SmallVector<CSlot, 2> Slots;
  for (const CSlot &Slot : Found.Slots) {
    if (Slot.Place == EPlace::Variable)
      Slots.push_back(Slot);
    else
      Told = false;
  }
  return Slots;
}

// What designation finds, worked out.
/// The object of \p At, a call of a member function seen only to give
/// access to it (onlyGivesAccess) that gives a reference, or of \p Pointer a
/// pointer, also one whose body can't be seen or does more that gives a
/// pointer to an owner it is given as a template argument, when that object
/// owns its elements: a buffer owner (OwnerTypes), of a reference, or a class
/// template given an owner, a container of owners; null otherwise.
static const Expr *elementObject(const Expr *At, bool Pointer,
                                 ASTContext &Context) {
  const auto *Member = dyn_cast<CXXMemberCallExpr>(At);
  const auto *Operator = dyn_cast<CXXOperatorCallExpr>(At);
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(
      Member || Operator ? cast<CallExpr>(At)->getDirectCallee() : nullptr);
  const Expr *Object = Member                            ? Member->getImplicitObjectArgument()
                       : Operator && Operator->getNumArgs() ? Operator->getArg(0)
                                                            : nullptr;
  if (!Method || !Object ||
      (Pointer ? !At->isPRValue() || !At->getType()->isPointerType()
               : !At->isLValue()) ||
      Object->getType()->isPointerType())
    return nullptr;
  const bool Access = onlyGivesAccess(Method);
  if (!Access && !Pointer)
    return nullptr;
  const QualType Type = Object->getType().getNonReferenceType();
  const EOwnerKind Kind = ownerKindOf(Type, Context);
  if (Access && !Pointer &&
      (Kind == EOwnerKind::Buffer || Kind == EOwnerKind::SharedBuffer))
    return Object;
  const auto *Specialization = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Type->getAsCXXRecordDecl());
  if (!Specialization)
    return nullptr;
  const QualType Pointee = Pointer ? At->getType()
                                         ->getPointeeType()
                                         .getCanonicalType()
                                         .getUnqualifiedType()
                                   : QualType();
  for (const TemplateArgument &Argument :
       Specialization->getTemplateArgs().asArray())
    if (Argument.getKind() == TemplateArgument::Type &&
        ownerKindOf(Argument.getAsType(), Context) != EOwnerKind::None &&
        (Access ||
         Argument.getAsType().getCanonicalType().getUnqualifiedType() ==
             Pointee))
      return Object;
  return nullptr;
}

/// The object of \p At, an element access that gives a reference
/// (elementObject), `Jobs[0]`.
static const Expr *elementAccessObject(const Expr *At, ASTContext &Context) {
  return elementObject(At, false, Context);
}

/// The object of \p At, an element access that gives a pointer
/// (elementObject), `Jobs.f_FindEqual(_Key)`.
static const Expr *elementPointerObject(const Expr *At, ASTContext &Context) {
  return elementObject(At, true, Context);
}

namespace {
/// What an element taken of an object is (elementOfObject).
enum class EElementOf : unsigned char {
  /// An element of an array, or of what can't be told to be the object.
  Element,
  /// What isn't in the object, which can't be told.
  Outside,
};
} // namespace

/// What the element \p Taken takes of an object of the type \p Of, in the
/// same projection as the members to it: of an array, an element; of an
/// object that isn't one, by its origin: a step of a pointer to it by the
/// constant 0 is the object itself and takes no element (step), at another
/// constant index it is what is outside it, which can't be told, unless the
/// object is a number and the bytes stepped to are inside it, a part of it,
/// `reinterpret_cast<uint8 *>(&m_Value)[4]` of a `uint64`; else an element,
/// also of what an element access of a container gives (elementObject) and
/// of an index that isn't a constant.
static EElementOf elementOfObject(QualType Of, const CProjection &Taken,
                                  const ASTContext &Context) {
  if (Of.isNull() || Of.getNonReferenceType()->isArrayType())
    return EElementOf::Element;
  if (!Taken.Index || *Taken.Index == 0)
    return EElementOf::Element;
  // Only of a number: an element of a pointer is what it points to
  // (designationOf), one of a record is of a container.
  const QualType Object = Of.getNonReferenceType();
  // An offset past 64 bits is past any object.
  bool Overflowed = false;
  if (Taken.Stride && *Taken.Index > 0 && !Object->isDependentType() &&
      (Object->isArithmeticType() || Object->isEnumeralType()) &&
      llvm::SaturatingMultiply(uint64_t(*Taken.Index), Taken.Stride,
                               &Overflowed) <
          uint64_t(Context.getTypeSizeInChars(Object).getQuantity()) &&
      !Overflowed)
    return EElementOf::Element;
  return EElementOf::Outside;
}

static llvm::SmallVector<CSlot, 2> designationOf(const Expr *E,
                                                 EDesignation Mode,
                                                 ASTContext &Context,
                                                 bool &Told) {
  const bool Values =
      Mode == EDesignation::Values || Mode == EDesignation::Places;
  const bool Places =
      Mode == EDesignation::Places || Mode == EDesignation::NamedPlaces;
  llvm::SmallVector<CSlot, 2> Out;
  CProvenance Walk(Context);
  Walk.setOtherEnds(true);
  bool Ended = false;
  const bool Complete = Walk.reach(
      E, 0,
      [&](const Expr *At, unsigned Level, bool) {
        const llvm::ArrayRef<CProjection> Projections = Walk.projections();
        // Of places, the object of the function or what a parameter refers
        // to or, of \p Pointer, points to, also through what its members
        // point to: the members the way takes of each level, the outermost
        // first; of \p Pointer not of the pointer itself. An element of an
        // element, or a member of an element, can't be told.
        auto Place = [&](const VarDecl *Variable, EPlace Of, bool Pointer) {
          CSlot Found{Variable, {}, false, Of};
          for (size_t iLevel = Projections.size(); iLevel-- > 0;) {
            const CProjection &Each = Projections[iLevel];
            if (Pointer && iLevel + 1 == Projections.size()) {
              if (!Each.Path.empty() || Each.Element)
                return EReach::Stop;
              continue;
            }
            if (Found.Element && (!Each.Path.empty() || Each.Element))
              return EReach::Stop;
            // What a member reached before points to, as a whole, `*m_pJob`,
            // is an element of what it points to, as `m_pJob[0]` is.
            // What an element points to, `*m_pJobs[1]`, isn't the element,
            // and no place says it is past it: it can't be told.
            if (iLevel + 1 < Projections.size() && !Found.Path.empty() &&
                Each.Path.empty() && !Each.Element) {
              if (Found.Element)
                return EReach::Stop;
              Found.Element = true;
              continue;
            }
            llvm::append_range(Found.Path, Each.Path);
            if (Each.Element) {
              // Of the object a member of this level, or the root, is, not
              // of what one reached before points to: of one that isn't an
              // array, at another constant index than 0, outside it
              // (elementOfObject).
              const bool OfObject =
                  !Each.Path.empty() || (Found.Path.empty() && !Pointer);
              const QualType Holder =
                  !Each.Path.empty() ? Each.Path.back()->getType()
                  : isa<CXXThisExpr>(At)
                      ? At->getType()->getPointeeType()
                      : At->getType();
              switch (OfObject ? elementOfObject(Holder, Each, Context)
                               : EElementOf::Element) {
              case EElementOf::Element:
                Found.Element = true;
                Found.Index = Each.Index;
                break;
              case EElementOf::Outside:
                return EReach::Stop;
              }
            }
          }
          Out.push_back(std::move(Found));
          return EReach::Root;
        };
        if (Level > 0) {
          if (Places)
            switch (effectsOf(At, Context).Statement) {
            case EStatement::This:
              return Place(nullptr, EPlace::This, false);
            case EStatement::Name:
              if (const auto *Parameter = dyn_cast<ParmVarDecl>(
                      cast<DeclRefExpr>(At)->getDecl())) {
                if (Parameter->getType()->isReferenceType())
                  return Parameter->isExplicitObjectParameter()
                             ? Place(nullptr, EPlace::This, false)
                             : Place(Parameter, EPlace::Referent, false);
                // What a pointer parameter points to, of one that holds
                // what it is given (holdsGiven), which its flow tells: only
                // through what pointers hold.
                if (Level == 1 && Values &&
                    Parameter->getType()->isPointerType() &&
                    isSlotVariable(Parameter) && holdsGiven(Parameter, Context))
                  return Place(Parameter, EPlace::Referent, true);
              }
              break;
            case EStatement::Other:
            case EStatement::Member:
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
            case EStatement::InitializerList:
            case EStatement::Temporary:
            case EStatement::Call:
              break;
            }
          return Values ? EReach::Continue : EReach::Stop;
        }
        const CProjection &Taken = Projections.back();
        const VarDecl *Slot = nullptr;
        switch (effectsOf(At, Context).Statement) {
        case EStatement::Name: {
          const ValueDecl *Named = cast<DeclRefExpr>(At)->getDecl();
          switch (holderKind(Named)) {
          case EHolder::Variable:
          case EHolder::Parameter:
          case EHolder::Decomposition: {
            const auto *Variable = cast<VarDecl>(Named);
            if (isSlotVariable(Variable) ||
                (Variable->getType()->isReferenceType() &&
                 closureCopying(Variable, At)))
              Slot = Variable;
            // Of places, a reference parameter is what it refers to: the
            // object of a function with an explicit object, else a referent.
            else if (const auto *Parameter =
                         holderKind(Named) == EHolder::Parameter
                             ? cast<ParmVarDecl>(Variable)
                             : nullptr;
                     Places && Parameter &&
                     Parameter->getType()->isReferenceType())
              return Parameter->isExplicitObjectParameter()
                         ? Place(nullptr, EPlace::This, false)
                         : Place(Parameter, EPlace::Referent, false);
            break;
          }
          // A binding a closure captures, named in the closure: the member
          // of the decomposed variable it names (bindingSlotOf).
          case EHolder::Binding:
            if (cast<DeclRefExpr>(At)->refersToEnclosingVariableOrCapture())
              if (const std::optional<CSlot> Bound =
                      bindingSlotOf(cast<BindingDecl>(Named))) {
                CSlot Part = *Bound;
                llvm::append_range(Part.Path, Taken.Path);
                Part.Element = Taken.Element;
                Part.Index = Taken.Index;
                Out.push_back(std::move(Part));
                return EReach::Root;
              }
            break;
          case EHolder::None:
          case EHolder::ImplicitParameter:
          case EHolder::Field:
          case EHolder::IndirectField:
          case EHolder::Temporary:
          case EHolder::Constant:
          case EHolder::Function:
          case EHolder::Statement:
          case EHolder::Opaque:
            break;
          }
          break;
        }
        case EStatement::Temporary: {
          const auto *Extending = dyn_cast_or_null<VarDecl>(
              cast<MaterializeTemporaryExpr>(At)->getExtendingDecl());
          if (!Extending || !isSlotVariable(Extending))
            return EReach::Root;
          Slot = Extending;
          break;
        }
        // An element of a local container of owners, or of a buffer owner,
        // that a member function seen only to give access gives a reference
        // to, `Jobs[0]`, or that one gives a pointer to,
        // `Jobs.f_FindEqual(_Key)` (elementObject). Where each body it can
        // run gives a part of its object, `return m_Elements[_Index];`, the
        // way goes on through them to that part (givesPartOf).
        case EStatement::Call:
          if (const Expr *Object =
                  Values ? elementObject(At, At->isPRValue(), Context)
                         : elementAccessObject(At, Context);
              Object &&
              !givesPartOf(cast<CallExpr>(At), std::nullopt, Context)) {
            bool ObjectTold = true;
            const llvm::SmallVector<CSlot, 2> Of =
                designation(Object, Mode, Context, ObjectTold);
            if (ObjectTold && Of.size() == 1 && !Of.front().Element) {
              CSlot Element = Of.front();
              Element.Element = true;
              if (const std::optional<llvm::APSInt> Index =
                      constantIntegerOf(elementIndexOfCall(At), Context))
                Element.Index = signedValueOf(*Index);
              Out.push_back(std::move(Element));
              return EReach::Root;
            }
          }
          if (refersToNothing(At, Level))
            return EReach::Root;
          break;
        // Of places, the object of the function.
        case EStatement::This:
          if (Places)
            return Place(nullptr, EPlace::This, false);
          break;
        case EStatement::Other:
        case EStatement::Member:
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
        case EStatement::InitializerList:
          if (refersToNothing(At, Level))
            return EReach::Root;
          break;
        }
        if (!Slot)
          return EReach::Continue;
        // Of what isn't an array, an element at another constant index than
        // 0 is outside it (elementOfObject).
        bool Element = Taken.Element;
        if (Element)
          switch (elementOfObject(Taken.Path.empty()
                                      ? Slot->getType()
                                      : Taken.Path.back()->getType(),
                                  Taken, Context)) {
          case EElementOf::Element:
            break;
          case EElementOf::Outside:
            return EReach::Stop;
          }
        Out.push_back({Slot, Taken.Path, Element, EPlace::Variable,
                       Element ? Taken.Index : std::nullopt});
        return EReach::Root;
      },
      &Ended);
  Told = Complete && !Ended;
  // Each once.
  llvm::SmallVector<CSlot, 2> Unique;
  for (CSlot &Slot : Out)
    if (!llvm::is_contained(Unique, Slot))
      Unique.push_back(std::move(Slot));
  return Unique;
}

/// \p Slots, each once, without the constant index of an element, which
/// only the flow tells apart (designatedPlaces); those who ask for slots
/// tell elements apart by their index expressions (elementIndexOf).
static llvm::SmallVector<CSlot, 2>
withoutIndices(llvm::SmallVector<CSlot, 2> Slots) {
  llvm::SmallVector<CSlot, 2> Unique;
  for (CSlot &Slot : Slots) {
    Slot.Index.reset();
    if (!llvm::is_contained(Unique, Slot))
      Unique.push_back(std::move(Slot));
  }
  return Unique;
}

/// The slots \p Target designates without what pointers hold (designatedSlots).
static llvm::SmallVector<CSlot, 2>
namedSlots(const Expr *Target, ASTContext &Context, bool *Told) {
  bool Each = true;
  llvm::SmallVector<CSlot, 2> Out =
      designation(Target, EDesignation::Named, Context, Each);
  if (Told)
    *Told = Each;
  return Out;
}

llvm::SmallVector<CSlot, 2> designatedSlots(const Expr *Target,
                                            ASTContext &Context, bool *Told) {
  // A name of a slot, as it is.
  if (Target && effectsOf(Target, Context).Statement == EStatement::Name)
    if (const auto *Variable = dyn_cast<VarDecl>(
            cast<DeclRefExpr>(Target)->getDecl());
        Variable && !Variable->getType()->isReferenceType() &&
        isSlotVariable(Variable)) {
      if (Told)
        *Told = true;
      return {{Variable, {}, false}};
    }
  bool Each = true;
  llvm::SmallVector<CSlot, 2> Out = withoutIndices(
      designation(Target, EDesignation::Values, Context, Each));
  if (Told)
    *Told = Each;
  return Out;
}

llvm::SmallVector<CSlot, 2> pointedSlots(const Expr *Pointer,
                                         ASTContext &Context, bool *Told) {
  bool Each = true;
  llvm::SmallVector<CSlot, 2> Out = withoutIndices(
      designation(Pointer, EDesignation::Values, Context, Each));
  if (Told)
    *Told = Each;
  return Out;
}

llvm::SmallVector<CSlot, 2> designatedPlaces(const Expr *Target,
                                             ASTContext &Context, bool *Told) {
  bool Each = true;
  llvm::SmallVector<CSlot, 2> Out =
      designation(Target, EDesignation::Places, Context, Each);
  if (Told)
    *Told = Each;
  return Out;
}

const Expr *ownerValueSource(const Expr *E, ASTContext &Context) {
  for (int Step = 0; E && Step < 32; ++Step) {
    const Expr *Inner = E->IgnoreImplicit()->IgnoreParens();
    if (Inner != E) {
      E = Inner;
      continue;
    }
    auto IsOwner = [&](QualType Type) {
      const EOwnerKind Kind =
          ownerKindOf(Type.getNonReferenceType(), Context);
      return Kind != EOwnerKind::None && Kind != EOwnerKind::Weak;
    };
    // What a copy or a move assignment the compiler defines gives, what it
    // copies (copiedByDefinedAssignment).
    if (const Expr *Copied = copiedByDefinedAssignment(E, Context)) {
      E = Copied;
      continue;
    }
    // A copy or a move of a record, or a conversion of an owner to an owner
    // of the same kind, `TCSharedPointer<CBase>(pDerived)`, by its first
    // argument, the others defaulted.
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(E);
        Construct && !Construct->isListInitialization() &&
        Construct->getNumArgs() >= 1 &&
        ((Construct->getConstructor() &&
          Construct->getConstructor()->isCopyOrMoveConstructor()) ||
         (IsOwner(Construct->getType()) &&
          IsOwner(Construct->getArg(0)->getType()) &&
          ownerKindOf(Construct->getType(), Context) ==
              ownerKindOf(
                  Construct->getArg(0)->getType().getNonReferenceType(),
                  Context))) &&
        llvm::all_of(llvm::drop_begin(Construct->arguments()),
                     [](const Expr *Argument) {
                       return isa<CXXDefaultArgExpr>(Argument);
                     })) {
      E = Construct->getArg(0);
      continue;
    }
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
        Cast && IsOwner(Cast->getType()) &&
        IsOwner(Cast->getSubExpr()->getType())) {
      E = Cast->getSubExpr();
      continue;
    }
    if (const auto *Call = dyn_cast<CallExpr>(E)) {
      // What a move, a forward or a copy function gives back (CopyFunctions),
      // and what an exchange gives back
      // of the first it is given, as it held it before (ExchangeFunctions).
      if (Call->getNumArgs() == 1 && !isMemberOrOperatorCall(Call) &&
          (vocabulary().MoveOrForwardFunctions.contains(calleeName(Call)) ||
           isCopyFunction(calleeName(Call)))) {
        E = Call->getArg(0);
        continue;
      }
      if (const FunctionDecl *Callee = Call->getDirectCallee();
          Callee && Callee->getIdentifier() && Call->getNumArgs() >= 1 &&
          !isMemberOrOperatorCall(Call) &&
          isExchangeFunction(identifierTextOf(Callee)) &&
          Call->getArg(0)->isGLValue()) {
        E = Call->getArg(0);
        continue;
      }
      // A member function of a shared owner that gives by value an owner of
      // the same class template shares what its object owns,
      // `pJob.f_ShareAsConst()`.
      if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call)) {
        const Expr *Object = Member->getImplicitObjectArgument();
        const auto *Given = Call->getType()->getAsCXXRecordDecl();
        const QualType ObjectType =
            Object ? (Object->getType()->isPointerType()
                          ? Object->getType()->getPointeeType()
                          : Object->getType())
                   : QualType();
        const auto *Of =
            Object ? ObjectType->getAsCXXRecordDecl() : nullptr;
        const auto *GivenSpecialization =
            dyn_cast_or_null<ClassTemplateSpecializationDecl>(Given);
        const auto *OfSpecialization =
            dyn_cast_or_null<ClassTemplateSpecializationDecl>(Of);
        if (Object && GivenSpecialization && OfSpecialization &&
            GivenSpecialization->getSpecializedTemplate() ==
                OfSpecialization->getSpecializedTemplate() &&
            ownerKindOf(ObjectType, Context) == EOwnerKind::Shared) {
          E = Object;
          continue;
        }
      }
    }
    break;
  }
  return E;
}

// The index or key the element access \p Call is given, its first argument
// past the object, past parentheses and implicit steps; null where it has
// none.
static const Expr *elementIndexOfCall(const Expr *Call) {
  const Expr *Index = nullptr;
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call))
    Index = Operator->getNumArgs() >= 2 ? Operator->getArg(1) : nullptr;
  else if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call))
    Index = Member->getNumArgs() >= 1 ? Member->getArg(0) : nullptr;
  return Index ? Index->IgnoreParenImpCasts() : nullptr;
}

CReferentPart referentPartOf(const Expr *E, ASTContext &Context,
                             bool Pointer) {
  llvm::SmallVector<const FieldDecl *, 4> Reversed;
  bool Element = false;
  const Expr *Index = nullptr;
  const Expr *At = E;
  for (int Step = 0; At && Step < 16; ++Step) {
    At = sameValue(At, Context)->IgnoreParenImpCasts();
    if (const auto *Named = dyn_cast<DeclRefExpr>(At)) {
      if (const auto *Parameter = dyn_cast<ParmVarDecl>(Named->getDecl());
          Parameter && Parameter->getType()->isReferenceType() &&
          !Parameter->getType().getNonReferenceType()->isPointerType()) {
        CReferentPart Part;
        Part.Parameter = Parameter;
        Part.Path.assign(Reversed.rbegin(), Reversed.rend());
        Part.Element = Element;
        Part.Index = Index;
        return Part;
      }
      // A local reference refers to what it is bound to where it is made.
      const auto *Local = dyn_cast<VarDecl>(Named->getDecl());
      if (!Local || isa<ParmVarDecl>(Local) || !Local->hasLocalStorage() ||
          !Local->getType()->isReferenceType() || !Local->getInit())
        return {};
      At = Local->getInit();
      continue;
    }
    if (const auto *Member = dyn_cast<MemberExpr>(At)) {
      const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
      if (!Field || Element)
        return {};
      Reversed.push_back(Field);
      At = Member->getBase()->IgnoreParenImpCasts();
      // Through a pointer member only, `m_pContext->m_pJob`, also a shared
      // or unique owner member (OwnerTypes) whose access gives what it
      // owns; of \p Pointer also a pointer parameter, `_pContext->m_pJob`.
      if (const auto *Access = dyn_cast<CXXOperatorCallExpr>(At);
          Access && Access->getOperator() == OO_Arrow &&
          Access->getNumArgs() == 1)
        if (const auto *Holder =
                dyn_cast<MemberExpr>(Access->getArg(0)->IgnoreParenImpCasts());
            Holder && isa<FieldDecl>(Holder->getMemberDecl())) {
          const EOwnerKind Kind = ownerKindOf(
              Holder->getType().getNonReferenceType(), Context);
          if (Kind == EOwnerKind::Shared || Kind == EOwnerKind::Unique) {
            At = Holder;
            continue;
          }
        }
      if (Member->isArrow()) {
        if (const auto *Named = dyn_cast<DeclRefExpr>(At); Pointer && Named)
          if (const auto *Parameter = dyn_cast<ParmVarDecl>(Named->getDecl());
              Parameter && Parameter->getType()->isPointerType()) {
            CReferentPart Part;
            Part.Parameter = Parameter;
            Part.Path.assign(Reversed.rbegin(), Reversed.rend());
            return Part;
          }
        const auto *Holder = dyn_cast<MemberExpr>(At);
        const auto *PointerMember =
            Holder ? dyn_cast<FieldDecl>(Holder->getMemberDecl()) : nullptr;
        if (!PointerMember || !PointerMember->getType()->isPointerType())
          return {};
      }
      continue;
    }
    const Expr *Object = nullptr;
    const Expr *Call = nullptr;
    if (const Expr *Of = elementAccessObject(At, Context)) {
      Object = Of;
      Call = At;
    } else if (const auto *Dereference = dyn_cast<UnaryOperator>(At);
               Dereference && Dereference->getOpcode() == UO_Deref) {
      Call = Dereference->getSubExpr()->IgnoreParenImpCasts();
      Object = elementPointerObject(Call, Context);
    }
    if (!Object || Element || !Reversed.empty())
      return {};
    Element = true;
    Index = elementIndexOfCall(Call);
    At = Object;
  }
  return {};
}

const Expr *elementIndexOf(const Expr *E, ASTContext &Context) {
  const Expr *At = E;
  for (int Step = 0; At && Step < 16; ++Step) {
    At = sameValue(At, Context)->IgnoreParenImpCasts();
    if (const auto *Named = dyn_cast<DeclRefExpr>(At)) {
      const auto *Local = dyn_cast<VarDecl>(Named->getDecl());
      if (!Local || isa<ParmVarDecl>(Local) || !Local->hasLocalStorage() ||
          !Local->getType()->isReferenceType() || !Local->getInit())
        return nullptr;
      At = Local->getInit();
      continue;
    }
    if (elementAccessObject(At, Context))
      return elementIndexOfCall(At);
    // An element of an array, not of what a pointer points to,
    // `Batch.m_pJobs[0]`.
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(At))
      return Subscript->getBase()->IgnoreParenImpCasts()->getType()
                     ->isArrayType()
                 ? Subscript->getIdx()->IgnoreParenImpCasts()
                 : nullptr;
    // Of a pointer to an element, the element it points to,
    // `Jobs.f_FindEqual(_Key)`.
    if (elementPointerObject(At, Context))
      return elementIndexOfCall(At);
    const auto *Dereference = dyn_cast<UnaryOperator>(At);
    if (!Dereference || Dereference->getOpcode() != UO_Deref)
      return nullptr;
    const Expr *Pointer = Dereference->getSubExpr()->IgnoreParenImpCasts();
    if (elementPointerObject(Pointer, Context))
      return elementIndexOfCall(Pointer);
    // What a local pointer holds there, by its one definition that reaches.
    const auto *Named = dyn_cast<DeclRefExpr>(Pointer);
    const auto *Local = Named ? dyn_cast<VarDecl>(Named->getDecl()) : nullptr;
    if (!Local || !Local->hasLocalStorage() ||
        Local->getType()->isReferenceType())
      return nullptr;
    const CSlotReaching Reaching =
        localSlotReaching({Local, {}, false}, Named, Context);
    if (!Reaching.Told || Reaching.Definitions.size() != 1 ||
        !Reaching.Definitions.front().Known ||
        Reaching.Definitions.front().Value.Kind != CSlotValue::EKind::Value ||
        !Reaching.Definitions.front().Value.Value)
      return nullptr;
    const Expr *Value =
        Reaching.Definitions.front().Value.Value->IgnoreParenImpCasts();
    if (!elementPointerObject(Value, Context))
      return nullptr;
    return elementIndexOfCall(Value);
  }
  return nullptr;
}

bool localReferentUnchangedBetween(const CReferentPart &Part,
                                   const Stmt *From, const Stmt *To,
                                   ASTContext &Context) {
  const auto *Function =
      Part.Parameter
          ? dyn_cast_or_null<FunctionDecl>(Part.Parameter->getDeclContext())
          : nullptr;
  if (!Function || !Function->getBody())
    return false;
  const CLocalFlow &Flow = sharedFlowCache().localFlowOf(Function, Context);
  CSlot Slot{Part.Parameter, Part.Path, Part.Element, EPlace::Referent};
  if (Part.Element)
    if (const std::optional<llvm::APSInt> Index =
            constantIntegerOf(Part.Index, Context))
      Slot.Index = signedValueOf(*Index);
  return Flow.placeUnchangedBetween(Slot, From, To);
}

CSlotReaching localSlotReaching(const CSlot &Slot, const Stmt *At,
                                ASTContext &Context) {
  const FunctionDecl *Function =
      Slot.Variable ? slotFunction(Slot.Variable, At) : nullptr;
  if (!Function || !Function->getBody())
    return {{}, false};
  return sharedFlowCache().localFlowOf(Function, Context).definitionsAt(Slot,
                                                                        At);
}

bool localSlotUnchangedBetween(const CSlot &Slot, const Stmt *From,
                               const Stmt *To, ASTContext &Context) {
  const FunctionDecl *Function =
      Slot.Variable ? slotFunction(Slot.Variable, To) : nullptr;
  if (!Function || !Function->getBody() ||
      slotFunction(Slot.Variable, From) != Function)
    return false;
  return sharedFlowCache().localFlowOf(Function, Context).unchangedBetween(
      Slot, From, To);
}

std::optional<llvm::SmallVector<CSlotDefinitionAt, 2>>
localSlotDefinitionsReaching(
    const FunctionDecl *Function, const CSlot &Slot,
    llvm::function_ref<bool(const CSlotDefinition &)> Ends,
    llvm::function_ref<bool(const CFlowEvent &)> Stops,
    llvm::function_ref<bool(const CFlowEvent &)> Reached, ASTContext &Context) {
  if (!Function || !Function->getBody() || !Slot.Variable)
    return std::nullopt;
  return sharedFlowCache().localFlowOf(Function, Context).definitionsReaching(
      Slot, Ends, Stops, Reached);
}

// An event at the entry, which each element \p Passes picks replaces, by the
// one may analysis (reachingIn): each way passes one where it doesn't reach
// the element of \p To, or the exit.
bool passesOnEachWay(const Decl *Owner, const Stmt *Body,
                     llvm::function_ref<bool(const CFlowEvent &)> Passes,
                     ASTContext &Context, const Stmt *To) {
  CFlowCache &Cache = sharedFlowCache();
  const CFG *Graph = Body ? Cache.graphOf(Owner, Body, Context) : nullptr;
  const CFlowOrder *Order = Graph ? Cache.orderOf(Owner, Body, Context) : nullptr;
  if (!Graph || !Order)
    return false;
  const llvm::BitVector &Reachable = Cache.reachableOf(Owner, Body, Context);
  CFlowMark End{Graph->getExit().getBlockID(), 0};
  if (To) {
    const llvm::SmallVector<CFlowMark, 8> Ends =
        pickedElements(*Graph, Reachable, [&](const CFlowEvent &Event) {
          return Event.Kind == EEvent::Statement && Event.Statement == To;
        });
    if (Ends.empty())
      return false;
    End = Ends.front();
  }
  llvm::SmallVector<CFlowMark, 8> Marks{{Graph->getEntry().getBlockID(), ~0u}};
  const llvm::SmallVector<CFlowMark, 8> Passed =
      pickedElements(*Graph, Reachable, Passes);
  Marks.append(Passed.begin(), Passed.end());
  const std::vector<llvm::BitVector> In =
      reachingIn(*Graph, Reachable, *Order, Marks, false);
  return !reachingBefore(In, Marks, End.Block, End.Index).test(0);
}

// An element of \p From before one of \p To in its block, or a way from the
// block of one to the block of the other (reachedFrom): what reaches by the one
// may analysis (reachingIn) from a mark at each element of \p From, each of
// which replaces only the others, is never empty past one.
bool canRunAfter(const Decl *Owner, const Stmt *Body, const Stmt *From,
                 const Stmt *To, ASTContext &Context) {
  CFlowCache &Cache = sharedFlowCache();
  CFlowPositions *Positions =
      Body && Cache.orderOf(Owner, Body, Context)
          ? Cache.positionsOf(Owner, Body, Context)
          : nullptr;
  if (!Positions)
    return true;
  const auto FromAt = Positions->Of.find(From);
  const auto ToAt = Positions->Of.find(To);
  if (FromAt == Positions->Of.end() || ToAt == Positions->Of.end())
    return true;
  for (const auto &[FromBlock, FromIndex] : FromAt->second)
    for (const auto &[ToBlock, ToIndex] : ToAt->second)
      if ((FromBlock == ToBlock && FromIndex < ToIndex) ||
          Cache.reachedFrom(Owner, Body, FromBlock, Context).test(ToBlock))
        return true;
  return false;
}


// The flow of the function of \p Member, a member of its object; null where
// it has no body or \p Member names no member.
static const CLocalFlow *objectFlowOf(const CThisMember &Member,
                                      ASTContext &Context) {
  const FunctionDecl *Function = Member.Function;
  if (!Function || !Function->getBody() || Member.Path.empty())
    return nullptr;
  return &sharedFlowCache().localFlowOf(Function, Context);
}

std::optional<llvm::SmallVector<CThisDefinition, 2>>
thisMemberDefinitions(const CThisMember &Member, const Stmt *At,
                      ASTContext &Context) {
  const CLocalFlow *Flow = objectFlowOf(Member, Context);
  if (!Flow)
    return std::nullopt;
  return Flow->objectDefinitions({nullptr, Member.Path, false, EPlace::This},
                                 At);
}

bool thisMemberUnchangedBetween(const CThisMember &Member, const Stmt *From,
                                const Stmt *To, ASTContext &Context) {
  const CLocalFlow *Flow = objectFlowOf(Member, Context);
  return Flow && Flow->objectUnchangedBetween(
                     {nullptr, Member.Path, false, EPlace::This}, From, To);
}

CSlotValues localSlotValues(const CSlot &Slot, const Stmt *At,
                            ASTContext &Context, bool *Final) {
  if (Final)
    *Final = false;
  const FunctionDecl *Function =
      Slot.Variable ? slotFunction(Slot.Variable, At) : nullptr;
  if (!Function || !Function->getBody()) {
    CSlotValues None;
    None.Told = false;
    return None;
  }
  return sharedFlowCache().localFlowOf(Function, Context).valuesAt(Slot, At,
                                                                    Final);
}

CSlotValues localSlotValuesAtEnd(const CSlot &Slot, ASTContext &Context,
                                 bool *Final) {
  if (Final)
    *Final = false;
  const FunctionDecl *Function =
      Slot.Variable ? slotFunction(Slot.Variable, nullptr) : nullptr;
  if (!Function || !Function->getBody()) {
    CSlotValues None;
    None.Told = false;
    return None;
  }
  return sharedFlowCache().localFlowOf(Function, Context).valuesAtEnd(Slot,
                                                                      Final);
}

bool localSlotEscapes(const VarDecl *Variable, ASTContext &Context) {
  const FunctionDecl *Function =
      Variable ? slotFunction(Variable, nullptr) : nullptr;
  return !Function || !Function->getBody() ||
         sharedFlowCache().localFlowOf(Function, Context).escapes(Variable);
}

llvm::SmallVector<CSlotValue, 2> assignedAt(const ParmVarDecl *Parameter,
                                            const Stmt *At,
                                            ASTContext &Context, bool *Given) {
  llvm::SmallVector<CSlotValue, 2> Assigned;
  bool Starts = false;
  for (const CSlotValue &Value :
       localSlotValues({Parameter, {}, false}, At, Context).Values)
    if (Value.Kind == CSlotValue::EKind::Given)
      Starts = true;
    else
      Assigned.push_back(Value);
  if (Given)
    *Given = Starts || Assigned.empty();
  return Assigned;
}

CSlotValues localSlotValuesFrom(const CSlot &Slot, const Stmt *At,
                                ASTContext &Context) {
  const FunctionDecl *Function =
      Slot.Variable ? slotFunction(Slot.Variable, At) : nullptr;
  if (!Function || !Function->getBody()) {
    CSlotValues None;
    None.Told = false;
    return None;
  }
  return sharedFlowCache().localFlowOf(Function, Context).valuesFrom(Slot, At);
}

llvm::SmallVector<CSlot, 2> localSlotParts(const CSlot &Slot, const Stmt *At,
                                           ASTContext &Context) {
  const FunctionDecl *Function =
      Slot.Variable ? slotFunction(Slot.Variable, At) : nullptr;
  if (!Function || !Function->getBody())
    return {};
  return sharedFlowCache().localFlowOf(Function, Context).writtenParts(Slot);
}

bool localSlotWholeReaches(const CSlot &Part, const Stmt *At,
                           ASTContext &Context) {
  const FunctionDecl *Function =
      Part.Variable ? slotFunction(Part.Variable, At) : nullptr;
  if (!Function || !Function->getBody())
    return true;
  return sharedFlowCache().localFlowOf(Function, Context).wholeReaches(Part,
                                                                       At);
}

bool localSlotChangedIn(const FunctionDecl *Function, const CSlot &Slot,
                        ASTContext &Context) {
  if (!Function || !Function->getBody() || !Slot.Variable)
    return true;
  return sharedFlowCache().localFlowOf(Function, Context).changedIn(Slot);
}

CSlotValues localSlotAssignments(const CSlot &Slot, ASTContext &Context) {
  const FunctionDecl *Function =
      Slot.Variable ? slotFunction(Slot.Variable, nullptr) : nullptr;
  if (!Function || !Function->getBody())
    return {};
  return sharedFlowCache().localFlowOf(Function, Context).assignments(Slot);
}

llvm::SmallVector<CClosureWrite, 2>
closureWritesAt(const FunctionDecl *Function, const Stmt *Call,
                ASTContext &Context) {
  if (!Function || !Function->getBody())
    return {};
  return sharedFlowCache().localFlowOf(Function, Context).closureWritesAt(Call);
}

llvm::SmallVector<CClosureWrite, 2>
closureWritesAtEnd(const FunctionDecl *Function, const VarDecl *Guard,
                   ASTContext &Context) {
  if (!Function || !Function->getBody())
    return {};
  return sharedFlowCache().localFlowOf(Function, Context).closureWritesAtEnd(
      Guard);
}

// Whether the closure of the class \p Closure in \p Initializer, of \p
// Owner, is only called there.
static bool closureOnlyCalledIn(const Decl *Owner, const Expr *Initializer,
                                const CXXRecordDecl *Closure,
                                ASTContext &Context) {
  if (const auto *Default = dyn_cast_or_null<CXXDefaultInitExpr>(Initializer))
    Initializer = Default->getExpr();
  if (!Owner || !Initializer || !Closure)
    return false;
  for (const Stmt *Each :
       sharedFlowCache().indexOf(Initializer, Context).statements())
    if (const auto *Lambda = dyn_cast<LambdaExpr>(Each);
        Lambda && Lambda->getLambdaClass() == Closure) {
      const CClosureUses &Uses =
          closureUses(Owner, Initializer, Lambda, Context);
      return Uses.bLocal && !Uses.Calls.empty();
    }
  return false;
}

bool closureRunsInInitializer(const Decl *Holder, const CXXRecordDecl *Closure,
                              ASTContext &Context) {
  const Expr *Initializer = nullptr;
  if (const auto *Field = dyn_cast_or_null<FieldDecl>(Holder))
    Initializer = Field->getInClassInitializer();
  else if (const auto *Variable = dyn_cast_or_null<VarDecl>(Holder))
    Initializer = Variable->getInit();
  return closureOnlyCalledIn(Holder, Initializer, Closure, Context);
}

bool closureRunsInInitializer(const CXXCtorInitializer *Initializer,
                              const CXXRecordDecl *Closure,
                              ASTContext &Context) {
  return Initializer &&
         closureOnlyCalledIn(Initializer->getAnyMember(),
                             Initializer->getInit(), Closure, Context);
}

bool closureRunsInCall(const FunctionDecl *Function, const LambdaExpr *Lambda,
                       ASTContext &Context) {
  if (!Function || !Function->getBody() || !Lambda)
    return false;
  return sharedFlowCache().localFlowOf(Function, Context).runsInCall(Lambda);
}

// Whether the end of a guard of a scope exit at element \p Index of block \p
// Block of the CFG of \p Function runs the closure it is given there
// (closureWritesAtEnd): not where it is cancelled before (cancelsGuard).
bool closureRunsAtEnd(const FunctionDecl *Function, unsigned Block,
                      unsigned Index, ASTContext &Context) {
  if (!Function || !Function->getBody())
    return false;
  return sharedFlowCache().localFlowOf(Function, Context).runsAtEnd(Block,
                                                                    Index);
}

// The indices of the reference and pointer parameters of \p Function, each
// of which a summary that can't be told keeps (CFlowSummaries::KeptBeyond).
static llvm::SmallVector<unsigned, 2>
referenceParametersOf(const FunctionDecl *Function) {
  llvm::SmallVector<unsigned, 2> Each;
  for (const ParmVarDecl *Parameter : Function->parameters())
    if (Parameter->getType()->isReferenceType() ||
        Parameter->getType()->isPointerType())
      Each.push_back(Parameter->getFunctionScopeIndex());
  return Each;
}

// Whether \p After only grew from \p Before, the summary of the same function
// in the pass of a recursion before: from the top, any; else each fact of
// Before is one of After, of the same member path or parameter, with each of
// its values and a reason where it had one, and on every way only where it
// was.
static bool summaryGrew(const CFunctionSummary &Before,
                        const CFunctionSummary &After) {
  if (Before.Top)
    return true;
  if (After.Top || (Before.Reason != EUntold::None &&
                    After.Reason == EUntold::None))
    return false;
  auto Grew = [](llvm::ArrayRef<CFlowFact> From, llvm::ArrayRef<CFlowFact> To) {
    return llvm::all_of(From, [&](const CFlowFact &Was) {
      return llvm::any_of(To, [&](const CFlowFact &Is) {
        return Is.Path == Was.Path && Is.Element == Was.Element &&
               Is.Index == Was.Index && Is.Target == Was.Target &&
               (Was.Reason == EUntold::None || Is.Reason != EUntold::None) &&
               (Was.ReachedReason == EUntold::None ||
                Is.ReachedReason != EUntold::None) &&
               (!Is.EveryPath || Was.EveryPath) &&
               llvm::all_of(Was.Sources, [&](const CFlowSource &Source) {
                 return llvm::is_contained(Is.Sources, Source);
               }) &&
               llvm::all_of(Was.Reached, [&](const CFlowSource &Source) {
                 return llvm::is_contained(Is.Reached, Source);
               });
      });
    });
  };
  return Grew(Before.Leaves, After.Leaves) &&
         Grew(Before.Parameters, After.Parameters);
}

CFlowSummaries::CFlowSummaries(ASTContext &Context)
    : OnlyCalls(
          [&Context](const std::pair<const FunctionDecl *, unsigned> &Key) {
            return sharedFlowCache()
                .closureUses(Key.first, Key.first->getBody(),
                             Key.first->getParamDecl(Key.second), Context)
                .bLocal;
          },
          true, false),
      ClosureUses(
          [&Context](const CClosureKey &Key) {
            return closureUsesUncached(std::get<0>(Key), std::get<1>(Key),
                                       std::get<2>(Key), std::get<3>(Key) != 0,
                                       Context);
          },
          CClosureUses(),
          [] {
            CClosureUses Escaping;
            Escaping.bLocal = false;
            return Escaping;
          }()),
      // While it is made, one that tells nothing.
      LocalFlows(
          [&Context](const FunctionDecl *const &Function) {
            CLocalFlow Flow(Function, Context);
            Flow.initialize();
            CFlowSummaries &Summaries = sharedFlowCache().summaries(Context);
            CFlowSummaries::CMade &Made = Summaries.Made[Function];
            Made.At = ++Summaries.Generation;
            Made.nWrites = std::max(Made.nWrites, Flow.writeCount());
            return Flow;
          },
          [&Context](const FunctionDecl *const &Function) {
            CLocalFlow Building(Function, Context);
            Building.Building = true;
            return Building;
          },
          [&Context](const FunctionDecl *const &Function) {
            return CLocalFlow(Function, Context);
          }),
      Facts(
          [&Context](const CSlotKey &Key) {
            return sharedFlowCache()
                .localFlowOf(Key.Function, Context)
                .makeFacts({Key.Variable, Key.Path, Key.Index.has_value(),
                            Key.Place, Key.Index});
          },
          CFacts(),
          [] {
            CFacts Untold;
            Untold.Told = false;
            return Untold;
          }()),
      // While it is worked out, none, which another write of what designates
      // it asks.
      Targets(
          [&Context](const std::pair<const FunctionDecl *, unsigned> &Key) {
            return sharedFlowCache()
                .localFlowOf(Key.first, Context)
                .makeTargets(Key.second);
          },
          CTargets(), CTargets{{}, false}),
      Parts(
          [&Context](const CSlotKey &Key) {
            return sharedFlowCache()
                .localFlowOf(Key.Function, Context)
                .makeParts({Key.Variable, Key.Path, false, Key.Place});
          },
          [](const CSlotKey &) { return llvm::SmallVector<CSlot, 2>(); },
          // Where a recursion doesn't settle, the slot itself, so that what
          // any write gives it is a change of a part that can't be told
          // (objectChanges): not none, which would tell that nothing is
          // written of it on its own.
          [](const CSlotKey &Key) {
            return llvm::SmallVector<CSlot, 2>{
                CSlot{Key.Variable, Key.Path, false, Key.Place}};
          }),
      ReturnedRoots(
          [&Context](const FunctionDecl *const &Definition) {
            return returnedRootsUncached(Definition, Context);
          },
          std::optional<CReturnedRoots>(), std::optional<CReturnedRoots>()),
      Designations(
          [&Context](const std::pair<const Expr *, unsigned> &Key) {
            CTargets Found;
            Found.Slots =
                designationOf(Key.first, static_cast<EDesignation>(Key.second),
                              Context, Found.Told);
            return Found;
          },
          CTargets{{}, false}, CTargets{{}, false}),
      // A recursion starts from the top, the ways through it not taken, and
      // widens to what can't be told.
      FunctionSummaries(
          [&Context](const FunctionDecl *const &Function) {
            // A flow made only for the summary isn't kept (release).
            // Its flow, made for it, isn't kept (release).
            CFlowCache &Cache = sharedFlowCache();
            CFlowSummaries &Summaries = Cache.summaries(Context);
            const unsigned Start = Summaries.Generation;
            CFunctionSummary Summary =
                Cache.localFlowOf(Function, Context).summarize();
            if (const auto Made = Summaries.Made.find(Function);
                Made != Summaries.Made.end() && Made->second.At > Start)
              Summaries.release(Function);
            return Summary;
          },
          [] {
            CFunctionSummary Top;
            Top.Top = true;
            return Top;
          }(),
          [] {
            CFunctionSummary Widened;
            Widened.Reason = EUntold::Recursion;
            return Widened;
          }()),
      ParameterSummaries(
          [&Context](const std::pair<const FunctionDecl *, unsigned> &Key) {
            // Its flow, made for it, isn't kept (release).
            CFlowCache &Cache = sharedFlowCache();
            CFlowSummaries &Summaries = Cache.summaries(Context);
            const unsigned Start = Summaries.Generation;
            CFunctionSummary Summary =
                Cache.localFlowOf(Key.first, Context).summarize(Key.second);
            if (const auto Making = Summaries.Made.find(Key.first);
                Making != Summaries.Made.end() && Making->second.At > Start)
              Summaries.release(Key.first);
            return Summary;
          },
          [] {
            CFunctionSummary Top;
            Top.Top = true;
            return Top;
          }(),
          [] {
            CFunctionSummary Widened;
            Widened.Reason = EUntold::Recursion;
            return Widened;
          }()),
      KeptBeyond(
          [&Context](const FunctionDecl *const &Function) {
            const CEscapes Escapes = escapesOf(
                Function,
                sharedFlowCache().indexOf(Function->getBody(), Context),
                EEscapes::Parameters, Context);
            llvm::SmallVector<unsigned, 2> Kept;
            for (const ParmVarDecl *Parameter : Function->parameters())
              if (Escapes.KeptReferents.contains(Parameter))
                Kept.push_back(Parameter->getFunctionScopeIndex());
            return Kept;
          },
          [](const FunctionDecl *const &) {
            return llvm::SmallVector<unsigned, 2>();
          },
          [](const FunctionDecl *const &Function) {
            return referenceParametersOf(Function);
          }),
      ObjectKeptBeyond(
          [&Context](const FunctionDecl *const &Function) {
            return escapesOf(
                       Function,
                       sharedFlowCache().indexOf(Function->getBody(), Context),
                       EEscapes::Object, Context)
                .KeptObject;
          },
          false, true) {
  // A pass of a recursion whose summary only grew, each fact's values more
  // and none on every way that wasn't, isn't counted: the values a fact can
  // have are finite, so it settles (a rotation of ten parameters,
  // f_Rotate10). One that takes the engine's most passes can't be told,
  // reason Budget.
  FunctionSummaries.setProgress(
      [](const CFunctionSummary &Before, const CFunctionSummary &After) {
        return summaryGrew(Before, After);
      },
      [](const FunctionDecl *const &) {
        CFunctionSummary Capped;
        Capped.Reason = EUntold::Budget;
        return Capped;
      });
  ParameterSummaries.setProgress(
      [](const CFunctionSummary &Before, const CFunctionSummary &After) {
        return summaryGrew(Before, After);
      },
      [](const std::pair<const FunctionDecl *, unsigned> &) {
        CFunctionSummary Capped;
        Capped.Reason = EUntold::Budget;
        return Capped;
      });
  // What a recursion keeps only grows, each parameter and the object at
  // most once: a pass that keeps more isn't counted. One that takes the
  // engine's most passes keeps each.
  KeptBeyond.setProgress(
      [](const llvm::SmallVector<unsigned, 2> &Before,
         const llvm::SmallVector<unsigned, 2> &After) {
        return llvm::all_of(Before, [&](unsigned Index) {
          return llvm::is_contained(After, Index);
        });
      },
      [](const FunctionDecl *const &Function) {
        return referenceParametersOf(Function);
      });
  ObjectKeptBeyond.setProgress(
      [](bool Before, bool After) { return !Before || After; },
      [](const FunctionDecl *const &) { return true; });
}


void CFlowSummaries::release(const FunctionDecl *Function) {
  if (!LocalFlows.contains(Function))
    return;
  const unsigned nWrites = Made.lookup(Function).nWrites;
  const auto FactsOf = FactKeys.find(Function);
  const auto PartsOf = PartKeys.find(Function);
  const llvm::ArrayRef<CSlotKey> Facted =
      FactsOf != FactKeys.end() ? llvm::ArrayRef<CSlotKey>(FactsOf->second)
                                : llvm::ArrayRef<CSlotKey>();
  const llvm::ArrayRef<CSlotKey> Parted =
      PartsOf != PartKeys.end() ? llvm::ArrayRef<CSlotKey>(PartsOf->second)
                                : llvm::ArrayRef<CSlotKey>();
  bool Droppable =
      LocalFlows.droppable(Function) &&
      llvm::all_of(Facted,
                   [&](const CSlotKey &Key) { return Facts.droppable(Key); }) &&
      llvm::all_of(Parted,
                   [&](const CSlotKey &Key) { return Parts.droppable(Key); });
  for (unsigned iWrite = 0; Droppable && iWrite < nWrites; ++iWrite)
    Droppable = Targets.droppable({Function, iWrite});
  // Of one a recursion still being worked out refers to, once it is done,
  // unless it is asked for before (localFlowOf).
  if (!Droppable) {
    if (Pending.insert(Function).second && Pending.size() == 1)
      CSummaryEngine::engine().whenIdle([this] {
        const llvm::SmallVector<const FunctionDecl *, 8> Each(Pending.begin(),
                                                              Pending.end());
        Pending.clear();
        for (const FunctionDecl *Function : Each)
          if (!Pending.contains(Function))
            release(Function);
      });
    return;
  }
  for (const CSlotKey &Key : Facted)
    Facts.drop(Key);
  for (const CSlotKey &Key : Parted)
    Parts.drop(Key);
  for (unsigned iWrite = 0; iWrite < nWrites; ++iWrite)
    Targets.drop({Function, iWrite});
  FactKeys.erase(Function);
  PartKeys.erase(Function);
  Primed.erase(Function);
  Made.erase(Function);
  LocalFlows.drop(Function);
}

llvm::SmallVector<CSetParameter, 1>
summarySetParameters(const FunctionDecl *Function) {
  llvm::SmallVector<CSetParameter, 1> Sets;
  if (!Function || !Function->getBody() ||
      llvm::none_of(Function->parameters(), setsParameter))
    return Sets;
  const CFunctionSummary &Summary =
      sharedFlowCache()
          .summaries(Function->getASTContext())
          .FunctionSummaries.get(Function);
  for (const CFlowFact &Fact : Summary.Parameters) {
    // On every way only where each value it can be set to is what a
    // parameter is given or the address of what one refers to.
    bool First = llvm::all_of(Fact.Sources, [](const CFlowSource &Source) {
      return Source.Kind == CFlowSource::EKind::Argument ||
             Source.Kind == CFlowSource::EKind::ArgumentAddress;
    });
    const bool ByPointer =
        Function->getParamDecl(Fact.Target)->getType()->isPointerType();
    for (const CFlowSource &Source : Fact.Sources) {
      if (Source.Kind != CFlowSource::EKind::Argument &&
          Source.Kind != CFlowSource::EKind::ArgumentAddress)
        continue;
      const bool Addressed =
          Source.Kind == CFlowSource::EKind::ArgumentAddress;
      const CSetParameter Set{Fact.Target, Source.Index,
                              First && Fact.EveryPath, Addressed, ByPointer,
                              Addressed && Source.Part};
      // Each once, of the same argument as it is or into what it points to.
      if (llvm::none_of(Sets, [&](const CSetParameter &Each) {
            return Each.Target == Set.Target && Each.Source == Set.Source &&
                   Each.Addressed == Set.Addressed && Each.Part == Set.Part;
          }))
        Sets.push_back(Set);
      First = false;
    }
  }
  return Sets;
}

CFlowCache::~CFlowCache() = default;

CFlowCache &sharedFlowCache() {
  static thread_local CFlowCache s_Cache;
  return s_Cache;
}

} // namespace clang::tidy::malterlib
