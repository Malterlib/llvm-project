//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AsyncDestroyCheck.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"
#include "OncePerTypeVisitor.h"
#include "RunsToCompletion.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/Builtins.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

/// What \p E takes the address of: `Value` of `&Value`, also of
/// `std::addressof(Value)` or `__builtin_addressof(Value)`. Null otherwise.
static const Expr *addressedBy(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary && Unary->getOpcode() == UO_AddrOf)
    return Unary->getSubExpr();
  if (const auto *Call = dyn_cast<CallExpr>(E);
      Call && Call->getNumArgs() == 1 &&
      (Call->getBuiltinCallee() == Builtin::BIaddressof ||
       Call->getBuiltinCallee() == Builtin::BI__addressof ||
       Call->getBuiltinCallee() == Builtin::BI__builtin_addressof))
    return Call->getArg(0);
  return nullptr;
}

namespace {
/// Whether \p Node calls one of the functions that register a cleanup through
/// an unchanged local pointer to it, `auto fDestroy = &fg_AsyncDestroy<
/// CResource &>; fDestroy(Resource)`, also through copies of that.
AST_MATCHER(CallExpr, callsAsyncDestroyIndirectly) {
  if (Node.getDirectCallee())
    return false;
  const FunctionDecl *Called = functionCalledBy(&Node);
  return Called && Called->getIdentifier() &&
         llvm::is_contained(
             llvm::ArrayRef<StringRef>(std::begin(AsyncDestroyFunctions),
                                       std::end(AsyncDestroyFunctions)),
             Called->getName());
}
} // namespace

void AsyncDestroyCheck::registerMatchers(MatchFinder *Finder) {
  const auto Names = hasAnyName(llvm::ArrayRef<StringRef>(
      std::begin(AsyncDestroyFunctions), std::end(AsyncDestroyFunctions)));
  Finder->addMatcher(
      callExpr(callee(functionDecl(Names)), unless(isInUnevaluatedContext()))
          .bind("destroy"),
      this);
  // Also through a local pointer to one, `auto fDestroy =
  // &fg_AsyncDestroy<CResource &>; fDestroy(Resource)`, also dereferenced,
  // `(*fDestroy)(Resource)`, and through copies of it.
  Finder->addMatcher(
      callExpr(callsAsyncDestroyIndirectly(), unless(isInUnevaluatedContext()))
          .bind("destroy"),
      this);
  // Each kind of node by itself, which the finder tries only on nodes of that
  // kind, not on every expression.
  Finder->addMatcher(cxxOperatorCallExpr(hasOverloadedOperatorName("/"),
                                         unless(isInUnevaluatedContext()))
                         .bind("divide"),
                     this);
  Finder->addMatcher(
      binaryOperator(hasOperatorName("/"), unless(isInUnevaluatedContext()))
          .bind("divide"),
      this);
  // Also called by name, `g_OnScopeExit.operator/(...)`.
  Finder->addMatcher(
      cxxMemberCallExpr(callee(cxxMethodDecl(hasOverloadedOperatorName("/"))),
                        unless(isInUnevaluatedContext()))
          .bind("divide"),
      this);
  // And the functions that make the same guards, `fg_OnScopeExit([&] { ...
  // })`, also the shared one, `fg_OnScopeExitShared(...)`.
  Finder->addMatcher(
      callExpr(callee(functionDecl(
                   hasAnyName("fg_OnScopeExit", "fg_OnScopeExitShared"))),
               argumentCountIs(1), unless(isInUnevaluatedContext()))
          .bind("scopeExit"),
      this);
}

namespace {
using VariableSet = llvm::SmallPtrSet<const VarDecl *, 4>;
AST_MATCHER_P(VarDecl, isOneOf, VariableSet, Variables) {
  return Variables.contains(&Node);
}
AST_MATCHER_P(Expr, satisfies, std::function<bool(const Expr *)>, Predicate) {
  return Predicate(&Node);
}

/// The expressions that \p E can be, also either of a conditional,
/// `bChoose ? Guard : Other`, the last of a comma, and what a cast to a
/// reference refers to, `static_cast<CAsyncDestroy &>(Guard)`. Each once,
/// which keeps it linear in the size of \p E.
static llvm::SmallVector<const Expr *, 2> alternativesOf(const Expr *E) {
  llvm::SmallVector<const Expr *, 2> Alternatives;
  llvm::SmallVector<const Expr *, 4> Pending{E};
  llvm::SmallPtrSet<const Expr *, 4> Seen;
  while (!Pending.empty()) {
    const Expr *Current = Pending.pop_back_val()->IgnoreParenImpCasts();
    if (!Seen.insert(Current).second)
      continue;
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Current);
        Cast && Cast->isGLValue()) {
      Pending.push_back(Cast->getSubExpr());
    } else if (const auto *Conditional =
                   dyn_cast<AbstractConditionalOperator>(Current)) {
      Pending.push_back(Conditional->getTrueExpr());
      Pending.push_back(Conditional->getFalseExpr());
    } else if (const auto *Comma = dyn_cast<BinaryOperator>(Current);
               Comma && Comma->getOpcode() == BO_Comma) {
      Pending.push_back(Comma->getRHS());
    } else {
      Alternatives.push_back(Current);
    }
  }
  return Alternatives;
}
} // namespace

void AsyncDestroyCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  for (const auto &[Id, Node] : Result.Nodes.getMap())
    if (isInTemplatePattern(Node, *Result.Context))
      return;
  // Not in a branch of an `if constexpr` that is discarded, which doesn't run,
  // nor in one that a constant condition never takes, `if (false)`.
  auto NeverRuns = [&](const Expr *E) {
    return isInDiscardedBranch(DynTypedNode::create(*E), *Result.Context) ||
           isInNeverTakenBranch(DynTypedNode::create(*E), *Result.Context);
  };
  if (const auto *Call = Result.Nodes.getNodeAs<CallExpr>("destroy")) {
    if (!NeverRuns(Call))
      checkAsyncDestroy(Call, *Result.Context);
    return;
  }
  if (const auto *Made = Result.Nodes.getNodeAs<CallExpr>("scopeExit")) {
    if (!NeverRuns(Made))
      checkScopeExit(Made, nullptr, Made->getArg(0), *Result.Context);
    return;
  }
  const auto *Divide = Result.Nodes.getNodeAs<Expr>("divide");
  if (NeverRuns(Divide))
    return;
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Divide)) {
    if (Operator->getNumArgs() == 2)
      checkScopeExit(Divide, Operator->getArg(0), Operator->getArg(1),
                     *Result.Context);
  } else if (const auto *Binary = dyn_cast<BinaryOperator>(Divide)) {
    checkScopeExit(Divide, Binary->getLHS(), Binary->getRHS(),
                   *Result.Context);
  } else if (const auto *Named = dyn_cast<CXXMemberCallExpr>(Divide);
             Named && Named->getNumArgs() == 1) {
    checkScopeExit(Divide, Named->getImplicitObjectArgument(),
                   Named->getArg(0), *Result.Context);
  }
}

/// The first parent of \p Node that is not an implicit or wrapping
/// expression around it.
static DynTypedNode outerParent(DynTypedNode Node, ASTContext &Context) {
  for (;;) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return {};
    const DynTypedNode &Parent = Parents[0];
    const auto *E = Parent.get<Expr>();
    if (E && (isa<ExprWithCleanups, MaterializeTemporaryExpr,
                  CXXBindTemporaryExpr, ImplicitCastExpr, ParenExpr,
                  OpaqueValueExpr>(E) ||
              (isa<CXXConstructExpr>(E) &&
               !isa<CXXTemporaryObjectExpr>(E) &&
               cast<CXXConstructExpr>(E)->getNumArgs() == 1))) {
      Node = Parent;
      continue;
    }
    // Also the last of a comma, `(fg_Prepare(), fg_AsyncDestroy(...))`, and
    // either value of a conditional, `bChoose ? fg_AsyncDestroy(A) :
    // fg_AsyncDestroy(B)`, which is where the value goes.
    const auto *Child = Node.get<Expr>();
    if (const auto *Comma = dyn_cast_or_null<BinaryOperator>(E);
        Comma && Comma->getOpcode() == BO_Comma && Child &&
        Comma->getRHS()->IgnoreParens() == Child->IgnoreParens()) {
      Node = Parent;
      continue;
    }
    if (const auto *Conditional = dyn_cast_or_null<ConditionalOperator>(E);
        Conditional && Child && Conditional->getCond() != Child) {
      Node = Parent;
      continue;
    }
    return Parent;
  }
}

/// The function or lambda \p Node is in.
static const FunctionDecl *enclosingFunction(DynTypedNode Node,
                                             ASTContext &Context) {
  for (;;) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    Node = Parents[0];
    if (const auto *Lambda = Node.get<LambdaExpr>())
      return Lambda->getCallOperator();
    if (const auto *Function = Node.get<FunctionDecl>())
      return Function;
  }
}

/// Whether \p Method, whose body can be seen, gives a pointer to the data of
/// its object, `return &m_Value;`, or to what the object owns, `return
/// m_pData;`, which the object's destructor frees, on each of its returns.
/// Whether each path through \p Graph to \p Later passes one of \p Earlier
/// first, also when no one of them is on every path, like the branches of an
/// `if` that each have one. None when \p Later isn't in it.
static std::optional<bool>
passesOnEveryPath(const CFG &Graph, llvm::ArrayRef<const DeclRefExpr *> Earlier,
                  const Stmt *Later) {
  const llvm::SmallPtrSet<const Stmt *, 4> Passed(Earlier.begin(),
                                                  Earlier.end());
  const CFGBlock *LaterBlock = nullptr;
  unsigned LaterIndex = 0;
  for (const CFGBlock *Block : Graph) {
    for (unsigned iElement = 0; iElement < Block->size(); ++iElement)
      if (const std::optional<CFGStmt> Element =
              (*Block)[iElement].getAs<CFGStmt>();
          Element && Element->getStmt() == Later) {
        LaterBlock = Block;
        LaterIndex = iElement;
      }
  }
  if (!LaterBlock)
    return std::nullopt;
  // Whether the elements of \p Block before \p End pass one of them.
  auto PassesIn = [&](const CFGBlock *Block, unsigned End) {
    for (unsigned iElement = 0; iElement < End; ++iElement)
      if (const std::optional<CFGStmt> Element =
              (*Block)[iElement].getAs<CFGStmt>();
          Element && Passed.contains(Element->getStmt()))
        return true;
    return false;
  };
  if (PassesIn(LaterBlock, LaterIndex))
    return true;
  // Back from the block of the later one, to the entry, through the blocks
  // that don't pass one of them.
  llvm::SmallVector<const CFGBlock *, 16> Work{LaterBlock};
  llvm::SmallPtrSet<const CFGBlock *, 16> Seen;
  while (!Work.empty()) {
    const CFGBlock *Block = Work.pop_back_val();
    if (Block == &Graph.getEntry())
      return false;
    for (const CFGBlock::AdjacentBlock &Predecessor : Block->preds()) {
      const CFGBlock *Before = Predecessor.getReachableBlock();
      if (!Before || !Seen.insert(Before).second ||
          PassesIn(Before, Before->size()))
        continue;
      Work.push_back(Before);
    }
  }
  return true;
}

static bool givesOwnData(const CXXMethodDecl *Method) {
  if (!Method || Method->isStatic())
    return false;
  const FunctionDecl *Definition = nullptr;
  if (!Method->hasBody(Definition) || !Definition->getBody())
    return false;
  // A member of the object, `m_Value` or `this->m_Value`.
  auto OfObject = [](const Expr *E) {
    for (int Depth = 0; Depth < 16; ++Depth) {
      E = E->IgnoreParenImpCasts();
      const auto *Member = dyn_cast<MemberExpr>(E);
      if (!Member || !isa<FieldDecl>(Member->getMemberDecl()))
        return false;
      const Expr *Base = Member->getBase()->IgnoreParenImpCasts();
      if (isa<CXXThisExpr>(Base))
        return true;
      if (Member->isArrow())
        return false;
      E = Base;
    }
    return false;
  };
  bool Any = false;
  bool All = true;
  std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
    if (!S || !All || isa<LambdaExpr>(S))
      return;
    if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
      const Expr *Value =
          Return->getRetValue() ? Return->getRetValue()->IgnoreParenImpCasts()
                                : nullptr;
      const auto *Address = dyn_cast_or_null<UnaryOperator>(Value);
      Any = true;
      All = Value && ((Address && Address->getOpcode() == UO_AddrOf &&
                       OfObject(Address->getSubExpr())) ||
                      isa<CXXThisExpr>(Value) ||
                      (Value->getType()->isPointerType() && OfObject(Value)));
      return;
    }
    for (const Stmt *Child : childrenOnce(S))
      Find(Child);
  };
  Find(Definition->getBody());
  return Any && All;
}

/// Whether \p Move, a move of the result of `fg_AsyncDestroy`, is what a
/// `co_await` awaits, directly or through `f_Wrap()`.
static bool isAwaitedCleanup(const Expr *Move, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Move);
  for (;;) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    const DynTypedNode &Parent = Parents[0];
    // Also parenthesized, `co_await (fg_Move(Cleanup))`.
    auto Same = [&](const Expr *Operand) {
      const auto *Child = Node.get<Expr>();
      return Operand && Child &&
             Child->IgnoreParens() == Operand->IgnoreParens();
    };
    if (const auto *Await = Parent.get<CoroutineSuspendExpr>())
      return Same(Await->getOperand()) || Same(Await->getCommonExpr());
    const auto *E = Parent.get<Expr>();
    if (!E)
      return false;
    // Only wrappers of the moved result, and calls of `f_Wrap()` on it.
    if (const auto *Member = dyn_cast<MemberExpr>(E)) {
      if (!Member->getMemberDecl()->getIdentifier() ||
          Member->getMemberDecl()->getName() != "f_Wrap")
        return false;
    } else if (!isa<ImplicitCastExpr, ParenExpr, MaterializeTemporaryExpr,
                    CXXBindTemporaryExpr, ExprWithCleanups,
                    CXXMemberCallExpr, OpaqueValueExpr>(E) &&
               !(isa<CXXConstructExpr>(E) &&
                 cast<CXXConstructExpr>(E)->getNumArgs() == 1)) {
      return false;
    }
    Node = Parent;
  }
}

const AsyncDestroyCheck::CFunctionIndex &
AsyncDestroyCheck::indexOf(const FunctionDecl *Function) {
  auto [It, Inserted] = Indices.try_emplace(Function);
  if (!Inserted)
    return It->second;
  CFunctionIndex &Index = It->second;
  std::function<void(const Stmt *)> Collect = [&](const Stmt *S) {
    if (!S)
      return;
    if (const auto *Declaration = dyn_cast<DeclStmt>(S))
      for (const Decl *D : Declaration->decls())
        if (const auto *Local = dyn_cast<VarDecl>(D)) {
          if (Local->getType()->isReferenceType() && Local->getInit())
            Index.References.push_back(Local);
          // Also one pointed later, `CCleanup *pCleanup; pCleanup = ...`.
          else if (Local->getType()->isPointerType())
            Index.Pointers.push_back(Local);
        }
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
      Index.Named.insert(Ref->getDecl());
    for (const Stmt *Child : childrenOnce(S))
      Collect(Child);
  };
  Collect(Function->getBody());
  return Index;
}

void AsyncDestroyCheck::checkAsyncDestroy(const CallExpr *Call,
                                          ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  const DynTypedNode Parent =
      outerParent(DynTypedNode::create(*Call), Context);
  const auto *Await = Parent.get<CoroutineSuspendExpr>();
  const FunctionDecl *Callee = functionCalledBy(Call);
  if (!Callee)
    return;
  const StringRef Name =
      Callee->getIdentifier() ? Callee->getName() : StringRef();

  // A function that returns the awaiter leaves the awaiting to its caller,
  // also through a local it is kept in that is only returned, `auto Awaiter
  // = fg_AsyncDestroyByValue(...); return Awaiter;`.
  auto OnlyReturned = [&](const VarDecl *Local) {
    const FunctionDecl *Function =
        enclosingFunction(DynTypedNode::create(*Call), Context);
    if (!Local || !Local->hasLocalStorage() ||
        Local->getType()->isReferenceType() || !Function ||
        !Function->getBody())
      return false;
    bool Returns = false;
    for (const auto &Nodes :
         match(findAll(declRefExpr(to(varDecl(equalsNode(Local)))).bind("use")),
               *Function->getBody(), Context)) {
      DynTypedNode Node =
          DynTypedNode::create(*Nodes.getNodeAs<DeclRefExpr>("use"));
      for (int Depth = 0;; ++Depth) {
        const auto Parents = instantiatedParents(Node, Context);
        if (Parents.empty() || Depth > 8)
          return false;
        if (Parents[0].get<ReturnStmt>()) {
          Returns = true;
          break;
        }
        const auto *Move = Parents[0].get<CallExpr>();
        if (!Parents[0].get<ImplicitCastExpr>() &&
            !Parents[0].get<ParenExpr>() &&
            !Parents[0].get<CXXConstructExpr>() &&
            !Parents[0].get<MaterializeTemporaryExpr>() &&
            !(Move && Move->getNumArgs() == 1 &&
              (isMoveFunction(calleeName(Move)))))
          return false;
        Node = Parents[0];
      }
    }
    return Returns;
  };
  const bool Returned = Parent.get<ReturnStmt>() || Parent.get<CoreturnStmt>() ||
                        OnlyReturned(Parent.get<VarDecl>());
  // An awaiter that refers to the function it is given until it is awaited,
  // like the `TCAsyncDestroyAwaiter<FDestroy &, FDestroy>` of
  // `fg_AsyncDestroy`, unlike `fg_AsyncDestroyByValue`, which keeps a copy, is
  // returned as it is, not converted to one that keeps a copy: a temporary or
  // a local of the function that returns it is gone by then.
  const Expr *Callable = Call->getNumArgs() >= 1 ? Call->getArg(0) : nullptr;
  const auto *Awaiter = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Call->getType()->getAsCXXRecordDecl());
  const FunctionDecl *Returning =
      Returned ? enclosingFunction(DynTypedNode::create(*Call), Context)
               : nullptr;
  bool Gone = false;
  if (Returning && Callable && Awaiter &&
      Awaiter->getTemplateArgs().size() >= 1 &&
      Awaiter->getTemplateArgs()[0].getKind() == TemplateArgument::Type &&
      Awaiter->getTemplateArgs()[0].getAsType()->isReferenceType() &&
      (Context.hasSameUnqualifiedType(Returning->getReturnType(),
                                      Call->getType()) ||
       // Or the value of the future a coroutine returns, `co_return
       // fg_AsyncDestroy(Cleanup)` of a `TCFuture<CAwaiter>`.
       (!futureValueType(Returning->getReturnType()).isNull() &&
        Context.hasSameUnqualifiedType(
            futureValueType(Returning->getReturnType()), Call->getType())))) {
    const Expr *Value = Callable->IgnoreParenImpCasts();
    // Moved, `fg_Move(Cleanup)`, it is still the local.
    for (int Depth = 0; Depth < 4; ++Depth) {
      const auto *Move = dyn_cast<CallExpr>(Value);
      if (!Move || Move->getNumArgs() != 1 ||
          (!isMoveFunction(calleeName(Move)) &&
           !isForwardFunction(calleeName(Move))))
        break;
      Value = Move->getArg(0)->IgnoreParenImpCasts();
    }
    if (isa<MaterializeTemporaryExpr, LambdaExpr, CXXBindTemporaryExpr>(
            Value) ||
        isa<MaterializeTemporaryExpr>(Callable->IgnoreParens()))
      Gone = true;
    else {
      // Also through a local reference to it, `auto &Alias = Cleanup`, or
      // an unchanged local pointer to it, `*pCleanup` with `auto *pCleanup =
      // &Cleanup`, and either value of a conditional, `bChoose ? Cleanup :
      // Other`.
      // Each value once, also one that a chain of conditionals reaches in
      // many ways, `auto &r1 = bChoose ? r0 : r0`, which would take
      // exponential time.
      llvm::SmallPtrSet<const Expr *, 16> Followed;
      std::function<void(const Expr *, int)> Follow = [&](const Expr *Value,
                                                          int Level) {
        const VarDecl *Local = nullptr;
        for (int Depth = 0; Depth < 64 && Value; ++Depth) {
          Value = Value->IgnoreParenImpCasts();
          if (!Followed.insert(Value).second)
            return;
          if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(Value))
            Value = Cleanups->getSubExpr()->IgnoreParenImpCasts();
          // The last of a comma, `(fg_Prepare(), Cleanup)`, and what the
          // address of a dereference is, `*&Cleanup`.
          if (const auto *Comma = dyn_cast<BinaryOperator>(Value);
              Comma && Comma->getOpcode() == BO_Comma) {
            Value = Comma->getRHS();
            continue;
          }
          if (const auto *Deref = dyn_cast<UnaryOperator>(Value);
              Deref && Deref->getOpcode() == UO_Deref)
            if (const auto *Address = dyn_cast<UnaryOperator>(
                    Deref->getSubExpr()->IgnoreParenImpCasts());
                Address && Address->getOpcode() == UO_AddrOf) {
              Value = Address->getSubExpr();
              continue;
            }
          // Also cast to a reference to it, `static_cast<decltype(Cleanup)
          // &&>(Cleanup)`, which is it.
          while (const auto *Cast = dyn_cast<ExplicitCastExpr>(Value)) {
            if (!Cast->isGLValue())
              break;
            Value = Cast->getSubExpr()->IgnoreParenImpCasts();
          }
          // A temporary that a local reference extends, `auto &&fCleanup =
          // []() -> TCFuture<void> { ... }`, goes with the local.
          if (Depth > 0 &&
              isa<MaterializeTemporaryExpr, LambdaExpr, CXXBindTemporaryExpr>(
                  Value)) {
            Gone = true;
            break;
          }
          if (const auto *Conditional = dyn_cast<ConditionalOperator>(Value)) {
            // Only the one a constant condition chooses, `true ? g_fCleanup :
            // fCleanup`.
            bool Chosen = false;
            if (!Conditional->getCond()->isValueDependent() &&
                Conditional->getCond()->EvaluateAsBooleanCondition(Chosen,
                                                                   Context)) {
              Value = Chosen ? Conditional->getTrueExpr()
                             : Conditional->getFalseExpr();
              continue;
            }
            // Deeper than is looked into, it can be gone.
            if (Level >= 256) {
              Gone = true;
              return;
            }
            Follow(Conditional->getTrueExpr(), Level + 1);
            Follow(Conditional->getFalseExpr(), Level + 1);
            return;
          }
          if (const auto *Deref = dyn_cast<UnaryOperator>(Value);
              Deref && Deref->getOpcode() == UO_Deref) {
            const auto *Pointer = dyn_cast<DeclRefExpr>(
                Deref->getSubExpr()->IgnoreParenImpCasts());
            const auto *PointerLocal =
                Pointer ? dyn_cast<VarDecl>(Pointer->getDecl()) : nullptr;
            const auto *Address =
                PointerLocal && PointerLocal->hasLocalStorage() &&
                        !isa<ParmVarDecl>(PointerLocal) &&
                        PointerLocal->getInit() &&
                        !isChangedVariable(PointerLocal)
                    ? dyn_cast<UnaryOperator>(
                          PointerLocal->getInit()->IgnoreParenImpCasts())
                    : nullptr;
            Local = nullptr;
            Value = Address && Address->getOpcode() == UO_AddrOf
                        ? Address->getSubExpr()
                        : nullptr;
            continue;
          }
          const auto *Ref = dyn_cast<DeclRefExpr>(Value);
          Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
              !Local->getType()->isReferenceType() || !Local->getInit())
            break;
          Value = Local->getInit();
          Local = nullptr;
        }
        if (Local && Local->hasLocalStorage() &&
            !Local->getType()->isReferenceType())
          Gone = true;
      };
      Follow(Value, 0);
    }
  }
  if (Gone) {
    const SourceLocation Loc = reportLocation(Call->getBeginLoc(), SM);
    if (Loc.isValid())
      diag(Loc, "'%0' refers to its cleanup function until it is co_awaited, "
                "but the function it is returned from destroys that first; "
                "use fg_AsyncDestroyByValue")
          << Name;
  } else if (Returned) {
    // The cleanup function is checked below.
  } else if (!Await) {
    const SourceLocation Loc = reportLocation(Call->getBeginLoc(), SM);
    if (Loc.isValid())
      diag(Loc, "'%0' schedules the cleanup when it is co_awaited; co_await "
                "it and keep the result in a named variable")
          << Name;
  } else {
    // The cleanup runs when the result goes out of scope, so a temporary
    // runs it at the end of the statement.
    const DynTypedNode AwaitNode = DynTypedNode::create(*Await);
    DynTypedNode Holder = outerParent(AwaitNode, Context);
    // Either branch of a conditional that initializes it, like `auto
    // Cleanup = bLeft ? co_await fg_AsyncDestroy(Left) : co_await
    // fg_AsyncDestroy(Right)`.
    // Also the last of a comma, `auto Cleanup = (fg_Prepare(), co_await
    // fg_AsyncDestroy(...))`.
    for (DynTypedNode Branch = AwaitNode;;) {
      const auto *Conditional = Holder.get<AbstractConditionalOperator>();
      const auto *Comma = Holder.get<BinaryOperator>();
      const Expr *Inner = Branch.get<Expr>();
      if (!(Conditional && Conditional->getCond() != Inner) &&
          !(Comma && Comma->getOpcode() == BO_Comma && Inner &&
            Comma->getRHS()->IgnoreParens() == Inner->IgnoreParens()))
        break;
      Branch = Holder;
      Holder = outerParent(Holder, Context);
    }
    // An element of a local array or aggregate it initializes, like
    // `CAsyncDestroy Guards[] = {co_await fg_AsyncDestroy(...)}`, also with
    // parentheses, `CHolder Holder(co_await fg_AsyncDestroy(...))`, which
    // lives as long as it.
    for (int Depth = 0; Depth < 8 && (Holder.get<InitListExpr>() ||
                                      Holder.get<CXXParenListInitExpr>());
         ++Depth)
      Holder = outerParent(Holder, Context);
    const auto *Variable = Holder.get<VarDecl>();
    if (!Variable || !Variable->hasLocalStorage()) {
      const SourceLocation Loc = reportLocation(Await->getBeginLoc(), SM);
      if (Loc.isValid())
        diag(Loc, "the result of co_awaiting '%0' is not kept in a named "
                  "variable, so the cleanup runs at the end of the statement "
                  "instead of at the end of the scope")
            << Name;
    } else if (const FunctionDecl *Function =
                   enclosingFunction(DynTypedNode::create(*Call), Context);
               Function && Function->getBody()) {
      // Moving the result elsewhere runs the cleanup after the locals it
      // refers to are gone.
      // Also forwarded, swapped or exchanged, or cast to an rvalue, like
      // `static_cast<CCleanup &&>(Cleanup)`.
      const auto MovingFunction = hasAnyName([] {
        const CVocabulary &Names = vocabulary();
        std::vector<StringRef> Moving;
        for (const CNameList *List :
             {&Names.MoveOrForwardFunctions, &Names.SwapFunctions,
              &Names.ExchangeFunctions})
          llvm::append_range(Moving, List->names());
        return Moving;
      }());
      // Also through local references to it, `auto &Alias = Cleanup`, also
      // of references to those, and either of a conditional, `auto &Alias =
      // bChoose ? Cleanup : Other`.
      const CFunctionIndex &Index = indexOf(Function);
      const llvm::SmallVector<const VarDecl *, 8> &References = Index.References;
      VariableSet Aliases{Variable};
      // From each variable to those that are made of it, each found once,
      // also in an order of declarations that is the opposite of how they
      // are made of each other.
      llvm::DenseMap<const VarDecl *, llvm::SmallVector<const VarDecl *, 2>>
          MadeOf;
      auto Spread = [&](const VariableSet &Seeds, VariableSet &Found) {
        llvm::SmallVector<const VarDecl *, 8> Worklist(Seeds.begin(),
                                                       Seeds.end());
        while (!Worklist.empty())
          for (const VarDecl *Made : MadeOf.lookup(Worklist.pop_back_val()))
            if (Found.insert(Made).second)
              Worklist.push_back(Made);
      };
      for (const VarDecl *Reference : References)
        for (const Expr *Init : alternativesOf(Reference->getInit()))
          if (const auto *Ref = dyn_cast<DeclRefExpr>(Init))
            if (const auto *Initial = dyn_cast<VarDecl>(Ref->getDecl()))
              MadeOf[Initial].push_back(Reference);
      Spread(VariableSet{Variable}, Aliases);
      // And through a pointer to it, `fg_Move(*pCleanup)` with `auto
      // *pCleanup = &Cleanup`, also a copy of one, `auto *pCopy = pCleanup`.
      // Also one pointed at it after, `pCleanup = &Cleanup`.
      const llvm::SmallVector<const VarDecl *, 8> &PointerLocals =
          Index.Pointers;
      llvm::DenseMap<const VarDecl *, llvm::SmallVector<const Expr *, 2>>
          PointerValues;
      for (const VarDecl *Pointer : PointerLocals)
        if (Pointer->getInit())
          PointerValues[Pointer].push_back(Pointer->getInit());
      if (!PointerLocals.empty()) {
        const VariableSet IsPointerLocal(PointerLocals.begin(),
                                         PointerLocals.end());
        std::function<void(const Stmt *)> Assigned = [&](const Stmt *S) {
          if (!S)
            return;
          if (const auto *Assign = dyn_cast<BinaryOperator>(S);
              Assign && Assign->getOpcode() == BO_Assign)
            if (const auto *Target = dyn_cast<DeclRefExpr>(
                    Assign->getLHS()->IgnoreParenImpCasts()))
              if (const auto *Pointer = dyn_cast<VarDecl>(Target->getDecl());
                  Pointer && IsPointerLocal.contains(Pointer))
                PointerValues[Pointer].push_back(Assign->getRHS());
          for (const Stmt *Child : childrenOnce(S))
            Assigned(Child);
        };
        Assigned(Function->getBody());
      }
      // A pointer to it, or one given another such pointer.
      VariableSet PointerAliases;
      MadeOf.clear();
      for (const VarDecl *Pointer : PointerLocals)
        for (const Expr *Value : PointerValues.lookup(Pointer)) {
          const Expr *Init = Value->IgnoreParenImpCasts();
          bool Addressed = false;
          if (const auto *Address = dyn_cast<UnaryOperator>(Init);
              Address && Address->getOpcode() == UO_AddrOf) {
            Init = Address->getSubExpr()->IgnoreParenImpCasts();
            Addressed = true;
          }
          const auto *Target = dyn_cast<DeclRefExpr>(Init);
          const auto *Of =
              Target ? dyn_cast<VarDecl>(Target->getDecl()) : nullptr;
          if (!Of)
            continue;
          if (!Addressed)
            MadeOf[Of].push_back(Pointer);
          else if (Aliases.contains(Of))
            PointerAliases.insert(Pointer);
        }
      Spread(VariableSet(PointerAliases), PointerAliases);
      // Whether what has type \p Type is the guard or holds it, by value, as
      // a member, also of a member, or a base, or an element of an array.
      const QualType GuardType = Await->getType()
                                     .getNonReferenceType()
                                     .getCanonicalType()
                                     .getUnqualifiedType();
      // Each type once, as many members can have the same type, which would
      // take a time that grows exponentially with how deep they nest. One that
      // is being decided, which a type can't hold by value, holds none.
      llvm::DenseMap<const Type *, bool> Holds;
      std::function<bool(QualType)> HoldsGuard = [&](QualType Type) {
        Type = QualType(Type->getBaseElementTypeUnsafe(), 0)
                   .getCanonicalType()
                   .getUnqualifiedType();
        if (Type == GuardType)
          return true;
        const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
        if (!Record || !Record->hasDefinition())
          return false;
        if (auto [It, Inserted] = Holds.try_emplace(Type.getTypePtr(), false);
            !Inserted)
          return It->second;
        Record = Record->getDefinition();
        const bool Held =
            llvm::any_of(Record->fields(),
                         [&](const FieldDecl *Field) {
                           return !Field->getType()->isReferenceType() &&
                                  HoldsGuard(Field->getType());
                         }) ||
            llvm::any_of(Record->bases(), [&](const CXXBaseSpecifier &Base) {
              return HoldsGuard(Base.getType());
            });
        Holds[Type.getTypePtr()] = Held;
        return Held;
      };
      // Also an element of an array of guards, `fg_Move(Guards[0])`.
      const auto IsGuard = ignoringParenImpCasts(
          anyOf(declRefExpr(to(varDecl(isOneOf(Aliases)))),
                arraySubscriptExpr(hasBase(ignoringParenImpCasts(
                    declRefExpr(to(varDecl(isOneOf(Aliases))))))),
                // Or a member of an aggregate that holds it,
                // `fg_Move(Holder.m_Guard)`, not another one beside it,
                // `fg_Move(Holder.m_Result)`.
                memberExpr(hasObjectExpression(ignoringParenImpCasts(
                               declRefExpr(to(varDecl(isOneOf(Aliases)))))),
                           satisfies([&](const Expr *Member) {
                             return HoldsGuard(Member->getType());
                           })),
                unaryOperator(hasOperatorName("*"),
                              hasUnaryOperand(ignoringParenImpCasts(declRefExpr(
                                  to(varDecl(isOneOf(PointerAliases)))))))));
      // Also either of a conditional, `fg_Move(bChoose ? Cleanup : Other)`.
      const StatementMatcher Guard = expr(IsGuard);
      const auto IsVariable = expr(satisfies([&](const Expr *E) {
        return llvm::any_of(alternativesOf(E), [&](const Expr *Alternative) {
          return !match(Guard, *Alternative, Context).empty();
        });
      }));
      // Only when it is named after it is made, which most results aren't.
      const bool Named =
          llvm::any_of(Aliases,
                       [&](const VarDecl *Alias) {
                         return Index.Named.contains(Alias);
                       }) ||
          llvm::any_of(PointerAliases, [&](const VarDecl *Alias) {
            return Index.Named.contains(Alias);
          });
      const auto Moves = !Named ? llvm::SmallVector<BoundNodes, 1>() : match(
          findAll(expr(anyOf(callExpr(anyOf(callee(functionDecl(MovingFunction)),
                                             callee(unresolvedLookupExpr(
                                                 hasAnyDeclaration(namedDecl(
                                                     MovingFunction))))),
                                      hasAnyArgument(IsVariable)),
                             explicitCastExpr(hasSourceExpression(IsVariable))),
                       unless(isInUnevaluatedContext()))
                      .bind("move")),
          *Function->getBody(), Context);
      for (const auto &Move : Moves) {
        // `co_await fg_Move(Cleanup)` runs the cleanup early, on purpose, and
        // so does `co_await fg_Move(Cleanup).f_Wrap()`. Awaiting something
        // the cleanup is passed to does not.
        const auto *MoveExpr = Move.getNodeAs<Expr>("move");
        // Also a cast to an rvalue reference awaited, `co_await
        // static_cast<CGuard &&>(Cleanup)`.
        if (const auto *Cast = dyn_cast<ExplicitCastExpr>(MoveExpr);
            Cast && (!Cast->getTypeAsWritten()->isRValueReferenceType() ||
                     isAwaitedCleanup(Cast, Context)))
          continue;
        // Bound to a local reference, `auto &&Alias = fg_Move(Cleanup)`,
        // which moves nothing.
        {
          DynTypedNode Node = DynTypedNode::create(*MoveExpr);
          bool Bound = false;
          for (int Depth = 0; Depth < 8; ++Depth) {
            const auto Parents = instantiatedParents(Node, Context);
            if (Parents.empty())
              break;
            if (const auto *Alias = Parents[0].get<VarDecl>()) {
              Bound = Alias->getType()->isReferenceType();
              break;
            }
            if (!Parents[0].get<ImplicitCastExpr>() &&
                !Parents[0].get<ParenExpr>() &&
                !Parents[0].get<ExprWithCleanups>() &&
                !Parents[0].get<MaterializeTemporaryExpr>())
              break;
            Node = Parents[0];
          }
          if (Bound)
            continue;
        }
        // Forwarded as an lvalue, `fg_Forward<CGuard &>(Cleanup)`, moves
        // nothing.
        if (const auto *Forward = dyn_cast<CallExpr>(MoveExpr);
            Forward && (isForwardFunction(calleeName(Forward))) &&
            Forward->isLValue())
          continue;
        // Also forwarded as an rvalue, `co_await fg_Forward<CGuard>(Cleanup)`,
        // which is the same.
        if (const auto *MoveCall = dyn_cast<CallExpr>(MoveExpr);
            MoveCall &&
            (isMoveFunction(calleeName(MoveCall)) ||
             ((isForwardFunction(calleeName(MoveCall))) &&
              MoveCall->isXValue())) &&
            isAwaitedCleanup(MoveCall, Context))
          continue;
        const SourceLocation Loc = reportLocation(MoveExpr->getBeginLoc(), SM);
        if (Loc.isValid())
          diag(Loc, "the result of '%0' is moved, so the cleanup can run "
                    "after the locals it refers to are gone; keep it in "
                    "its variable")
              << Name;
      }
      // Returning the result moves it implicitly.
      const auto Returns =
          !Named ? llvm::SmallVector<BoundNodes, 1>()
                 : match(findAll(stmt(anyOf(returnStmt(), coreturnStmt()))
                                     .bind("return")),
                         *Function->getBody(), Context);
      for (const auto &Return : Returns) {
        const Stmt *S = Return.getNodeAs<Stmt>("return");
        const Expr *Value = nullptr;
        if (const auto *Plain = dyn_cast<ReturnStmt>(S))
          Value = Plain->getRetValue();
        else
          Value = cast<CoreturnStmt>(S)->getOperand();
        const auto *Ref =
            Value ? dyn_cast<DeclRefExpr>(skipWrappers(Value)) : nullptr;
        if (!Ref || Ref->getDecl() != Variable)
          continue;
        const SourceLocation Loc = reportLocation(Ref->getBeginLoc(), SM);
        if (Loc.isValid())
          diag(Loc, "the result of '%0' is returned, so the cleanup can run "
                    "after the locals it refers to are gone; keep it in "
                    "its variable")
              << Name;
      }
      // A pointer that the cleanup captures by reference and reads when it
      // runs, pointed at a local declared after the result, which is
      // destroyed before it: `pValue = &Local`.
      // Also one a factory makes, `fg_AsyncDestroy(fMakeCleanup())`.
      const LambdaExpr *Cleanup =
          Call->getNumArgs() >= 1 ? cleanupLambdaOf(Call->getArg(0)) : nullptr;
      llvm::SmallPtrSet<const VarDecl *, 4> Pointers;
      // Only one it reads, not one it only assigns, `pValue = nullptr`, nor
      // one it reads only after it assigns it, `pValue = &g_Value; co_return
      // *pValue;`, in a statement of its body, nor what isn't evaluated,
      // `sizeof(*pValue)`, or what an `if constexpr` discards.
      auto ReadIn = [&](const VarDecl *Pointer) {
        bool Read = false;
        auto Assigns = [&](const Stmt *S) -> const BinaryOperator * {
          const auto *E = dyn_cast_or_null<Expr>(S);
          const auto *Assign =
              E ? dyn_cast<BinaryOperator>(E->IgnoreImplicit()->IgnoreParens())
                : nullptr;
          const auto *Target =
              Assign && Assign->getOpcode() == BO_Assign
                  ? dyn_cast<DeclRefExpr>(
                        Assign->getLHS()->IgnoreParenImpCasts())
                  : nullptr;
          return Target && Target->getDecl() == Pointer ? Assign : nullptr;
        };
        std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
          if (!S || Read || isUnevaluatedNode(S))
            return;
          if (const BinaryOperator *Assign = Assigns(S)) {
            Visit(Assign->getRHS());
            return;
          }
          if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
              Ref && Ref->getDecl() == Pointer)
            Read = true;
          for (const Stmt *Child : keptChildren(S, Context))
            Visit(Child);
        };
        // The body as written, of a coroutine without the code it adds.
        const Stmt *Written = Cleanup->getBody();
        if (const auto *Coroutine =
                dyn_cast_or_null<CoroutineBodyStmt>(Written))
          Written = Coroutine->getBody();
        const auto *Body = dyn_cast_or_null<CompoundStmt>(Written);
        if (!Body) {
          Visit(Written);
          return Read;
        }
        for (const Stmt *Statement : Body->body()) {
          Visit(Statement);
          if (Read || Assigns(Statement))
            break;
        }
        return Read;
      };
      if (Cleanup)
        for (const LambdaCapture &Capture : Cleanup->captures()) {
          const auto *Captured = dyn_cast_or_null<VarDecl>(
              Capture.capturesVariable() ? Capture.getCapturedVar() : nullptr);
          if (!Captured || Capture.getCaptureKind() != LCK_ByRef)
            continue;
          // Also through a local reference to the pointer, `int *&pAlias =
          // pValue`, which sees what it is assigned later, also to either of
          // a conditional, `int *&pAlias = bChoose ? pFirst : pSecond`.
          if (!ReadIn(Captured))
            continue;
          llvm::SmallVector<const VarDecl *, 4> Work{Captured};
          llvm::SmallPtrSet<const VarDecl *, 8> Seen;
          while (!Work.empty() && Seen.size() < 64) {
            const VarDecl *Pointer = Work.pop_back_val();
            if (!Seen.insert(Pointer).second)
              continue;
            if (Pointer->getType()->isPointerType()) {
              Pointers.insert(Pointer);
              continue;
            }
            if (!Pointer->getType()->isReferenceType() ||
                !Pointer->hasLocalStorage() || isa<ParmVarDecl>(Pointer) ||
                !Pointer->getInit())
              continue;
            llvm::SmallVector<const Expr *, 2> Bound{
                Pointer->getInit()->IgnoreParenImpCasts()};
            while (!Bound.empty()) {
              const Expr *Value = Bound.pop_back_val()->IgnoreParenImpCasts();
              if (const auto *Conditional =
                      dyn_cast<AbstractConditionalOperator>(Value)) {
                Bound.push_back(Conditional->getTrueExpr());
                Bound.push_back(Conditional->getFalseExpr());
              } else if (const auto *Ref = dyn_cast<DeclRefExpr>(Value))
                if (const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl()))
                  Work.push_back(Variable);
            }
          }
        }
      // What an address is of, through members and elements.
      // What each pointer can be given anywhere, worked out once: one being
      // worked out, through copies that go around, `p = q; q = p;`, adds
      // nothing more.
      llvm::DenseMap<const VarDecl *, const VarDecl *> AnyGiven;
      llvm::DenseMap<const DeclRefExpr *, const VarDecl *> AtRead;
      // What each local reference is bound to, worked out once, also when a
      // chain of conditionals of the ones before, `int &r2 = b ? r1 : r1;`,
      // reaches it on many paths.
      llvm::DenseMap<const VarDecl *, const VarDecl *> ReferredBy;
      llvm::SmallPtrSet<const VarDecl *, 8> AnyGivenInProgress;
      std::function<const VarDecl *(const Expr *, int)> LocalOf =
          [&](const Expr *E, int Depth) -> const VarDecl * {
        E = E->IgnoreParenImpCasts();
        // A copy of a local pointer, `pValue = pLate`, points where it was
        // made to point: what it was last assigned before, or, when that
        // can't be told, any of what it was given, the local declared last,
        // through as many copies as the code has.
        if (const auto *Copy = dyn_cast<DeclRefExpr>(E);
            Copy && Depth < 256 && Copy->getType()->isPointerType())
          if (const auto *Pointer = dyn_cast<VarDecl>(Copy->getDecl());
              Pointer && Pointer->hasLocalStorage() &&
              !isa<ParmVarDecl>(Pointer)) {
            // Each read once, also when copies made of others many times,
            // `pB = bChoose ? pA : pA`, reach it on many paths.
            if (const Expr *Value = assignedValueAt(Pointer, Copy)) {
              if (auto It = AtRead.find(Copy); It != AtRead.end())
                return It->second;
              const VarDecl *Local = LocalOf(Value, Depth + 1);
              AtRead[Copy] = Local;
              return Local;
            }
            const auto *Function = dyn_cast_or_null<FunctionDecl>(
                Pointer->getParentFunctionOrMethod());
            if (!Function || !Function->getBody())
              return nullptr;
            if (auto It = AnyGiven.find(Pointer); It != AnyGiven.end())
              return It->second;
            if (!AnyGivenInProgress.insert(Pointer).second)
              return nullptr;
            const llvm::scope_exit Done([&] {
              AnyGivenInProgress.erase(Pointer);
            });
            const VarDecl *Latest =
                Pointer->getInit() ? LocalOf(Pointer->getInit(), Depth + 1)
                                   : nullptr;
            for (const auto &Nodes : match(
                     findAll(binaryOperator(
                                 isAssignmentOperator(),
                                 hasLHS(ignoringParenImpCasts(declRefExpr(
                                     to(varDecl(equalsNode(Pointer)))))))
                                 .bind("assign")),
                     *Function->getBody(), Context)) {
              const VarDecl *Given = LocalOf(
                  Nodes.getNodeAs<BinaryOperator>("assign")->getRHS(),
                  Depth + 1);
              if (Given && (!Latest || SM.isBeforeInTranslationUnit(
                                           Latest->getLocation(),
                                           Given->getLocation())))
                Latest = Given;
            }
            AnyGiven[Pointer] = Latest;
            return Latest;
          }
        // Moved within an array, `Late + 1`.
        if (const auto *Offset = dyn_cast<BinaryOperator>(E);
            Offset && Offset->isAdditiveOp() &&
            Offset->getType()->isPointerType())
          return LocalOf(Offset->getLHS()->getType()->isPointerType()
                             ? Offset->getLHS()
                             : Offset->getRHS(),
                         Depth + 1);
        // Either value of a conditional, `bChoose ? &Late : nullptr`: the
        // local declared last, which is destroyed first, or the one a
        // constant condition chooses.
        if (const auto *Conditional = dyn_cast<ConditionalOperator>(E)) {
          bool Chosen = false;
          if (!Conditional->getCond()->isValueDependent() &&
              Conditional->getCond()->EvaluateAsBooleanCondition(Chosen,
                                                                 Context))
            return LocalOf(Chosen ? Conditional->getTrueExpr()
                                  : Conditional->getFalseExpr(),
                           Depth + 1);
          const VarDecl *True = LocalOf(Conditional->getTrueExpr(), Depth + 1);
          const VarDecl *False =
              LocalOf(Conditional->getFalseExpr(), Depth + 1);
          if (!True || !False)
            return True ? True : False;
          return SM.isBeforeInTranslationUnit(True->getLocation(),
                                              False->getLocation())
                     ? False
                     : True;
        }
        // A cast to another pointer, `static_cast<CBase *>(&Late)`.
        if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
            Cast && Cast->getType()->isPointerType() &&
            Cast->getSubExpr()->getType()->isPointerType())
          return LocalOf(Cast->getSubExpr(), Depth + 1);
        const auto *Address = dyn_cast<UnaryOperator>(E);
        const auto *AddressOf = dyn_cast<CallExpr>(E);
        const auto *Getter = dyn_cast<CXXMemberCallExpr>(E);
        if (Address && Address->getOpcode() == UO_AddrOf)
          E = Address->getSubExpr()->IgnoreParenImpCasts();
        // What a getter of an object gives of it, `Late.f_Get()` of `int
        // *f_Get() { return &m_Value; }`, also what it owns, `return
        // m_pData;`, which the object's destructor frees.
        else if (Getter && Getter->getType()->isPointerType() &&
                 Getter->getImplicitObjectArgument() &&
                 !Getter->getImplicitObjectArgument()
                      ->IgnoreParenImpCasts()
                      ->getType()
                      ->isPointerType() &&
                 givesOwnData(Getter->getMethodDecl()))
          E = Getter->getImplicitObjectArgument()->IgnoreParenImpCasts();
        // Also `std::addressof(Late)`.
        else if (isAddressOfCall(AddressOf))
          E = AddressOf->getArg(0)->IgnoreParenImpCasts();
        else if (!isa<DeclRefExpr, MemberExpr, ArraySubscriptExpr>(E) ||
                 !E->getType()->isArrayType())
          return nullptr;
        // A local reference is what it is bound to, `int &Alias = Outer`, also
        // through a chain of them, not a temporary it extends, which lives as
        // long as it.
        auto ExtendsTemporary = [](const VarDecl *Reference) {
          const Expr *Init = Reference->getInit();
          if (const auto *Cleanups = dyn_cast_or_null<ExprWithCleanups>(Init))
            Init = Cleanups->getSubExpr();
          const auto *Temporary =
              Init ? dyn_cast<MaterializeTemporaryExpr>(Init->IgnoreParens())
                   : nullptr;
          return Temporary && Temporary->getExtendingDecl() == Reference;
        };
        std::function<const VarDecl *(const Expr *, int)> Referred =
            [&](const Expr *Bound, int Hop) -> const VarDecl * {
          if (Hop > 64)
            return nullptr;
          // As the address of what it is bound to.
          for (int Level = 0; Level < 16; ++Level) {
            Bound = Bound->IgnoreParenImpCasts();
            if (const auto *Member = dyn_cast<MemberExpr>(Bound);
                Member && !Member->isArrow())
              Bound = Member->getBase();
            else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Bound);
                     Subscript && Subscript->getBase()
                                      ->IgnoreParenImpCasts()
                                      ->getType()
                                      ->isArrayType())
              Bound = Subscript->getBase();
            else
              break;
          }
          // Either value of a conditional, `bChoose ? First : Second`, also
          // through references, `bChoose ? Alias : Second`: the local declared
          // last, which is destroyed first, as the address of each.
          if (const auto *Conditional = dyn_cast<ConditionalOperator>(Bound)) {
            bool Constant = false;
            if (!Conditional->getCond()->isValueDependent() &&
                Conditional->getCond()->EvaluateAsBooleanCondition(Constant,
                                                                   Context))
              return Referred(Constant ? Conditional->getTrueExpr()
                                       : Conditional->getFalseExpr(),
                              Hop + 1);
            const VarDecl *Last = nullptr;
            for (const Expr *Chosen :
                 {Conditional->getTrueExpr(), Conditional->getFalseExpr()})
              if (const VarDecl *ChosenLocal = Referred(Chosen, Hop + 1);
                  ChosenLocal && (!Last || SM.isBeforeInTranslationUnit(
                                               Last->getLocation(),
                                               ChosenLocal->getLocation())))
                Last = ChosenLocal;
            return Last;
          }
          // What a function whose body can be seen gives back of what it is
          // given by reference, `fg_Const(Local)`: the local declared last of
          // those.
          if (const auto *Call = dyn_cast<CallExpr>(Bound);
              Call && Call->isGLValue()) {
            const VarDecl *Last = nullptr;
            for (const Expr *Given : returnedArguments(Call))
              if (const VarDecl *GivenLocal = Referred(Given, Hop + 1);
                  GivenLocal && (!Last || SM.isBeforeInTranslationUnit(
                                              Last->getLocation(),
                                              GivenLocal->getLocation())))
                Last = GivenLocal;
            return Last;
          }
          const auto *Ref = dyn_cast<DeclRefExpr>(Bound);
          const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          if (!Local || !Local->hasLocalStorage())
            return nullptr;
          if (!Local->getType()->isReferenceType())
            return Local;
          if (!Local->getInit() || isa<ParmVarDecl>(Local))
            return nullptr;
          if (ExtendsTemporary(Local))
            return Local;
          const Expr *Init = Local->getInit()->IgnoreParenImpCasts();
          if (Init->getType()->isPointerType() || Depth > 16)
            return nullptr;
          if (auto It = ReferredBy.find(Local); It != ReferredBy.end())
            return It->second;
          const VarDecl *Found = Referred(Init, Hop + 1);
          ReferredBy[Local] = Found;
          return Found;
        };
        return Referred(E, 0);
      };
      // Not after the result has run or cancelled the cleanup, like `co_await
      // fg_Move(Cleanup)` or `Cleanup.f_Clear()`: where it is first moved, or
      // a member function of it is called.
      // On every path to the assignment, also through a reference to it,
      // `fg_Move(Alias)`.
      llvm::SmallVector<const DeclRefExpr *, 4> Finishing;
      if (!Pointers.empty())
        for (const auto &Nodes :
             match(findAll(declRefExpr(to(varDecl(isOneOf(Aliases))))
                               .bind("guard")),
                   *Function->getBody(), Context)) {
          const auto *Guard = Nodes.getNodeAs<DeclRefExpr>("guard");
          const auto Parents = instantiatedParents(*Guard, Context);
          DynTypedNode Parent = Parents.empty() ? DynTypedNode() : Parents[0];
          while (const auto *Cast = Parent.get<ImplicitCastExpr>()) {
            const auto Outer = instantiatedParents(*Cast, Context);
            Parent = Outer.empty() ? DynTypedNode() : Outer[0];
          }
          const auto *Move = Parent.get<CallExpr>();
          const auto *Cast = Parent.get<ExplicitCastExpr>();
          // Also by a cast to an rvalue reference, `static_cast<decltype(
          // Cleanup) &&>(Cleanup)`.
          const bool Moved =
              (Move && Move->getNumArgs() == 1 &&
               (isMoveFunction(calleeName(Move)))) ||
              (Cast && Cast->getTypeAsWritten()->isRValueReferenceType());
          const auto *Member = Parent.get<MemberExpr>();
          // Not one that isn't evaluated, `noexcept(Cleanup.f_Clear())`.
          if ((Moved || Member) &&
              !isInUnevaluatedOperand(DynTypedNode::create(*Guard), Context))
            Finishing.push_back(Guard);
        }
      // Each assignment of a pointer, `pValue = &Local`, also by
      // `fg_Exchange(pValue, &Local)`.
      // Or by a setter whose body can be seen, `fg_Set(pValue, Late)` of
      // `void fg_Set(int *&_pValue, int &_Value) { _pValue = &_Value; }`,
      // with the local it points it at.
      llvm::SmallVector<std::tuple<const Expr *, const DeclRefExpr *,
                                   const Expr *, const VarDecl *>,
                        4>
          Assignments;
      if (!Pointers.empty()) {
        // Also through a cast to a reference, `static_cast<int *&>(pValue) =
        // &Local`.
        for (const auto &Nodes : match(
                 findAll(binaryOperator(hasOperatorName("=")).bind("assign")),
                 *Function->getBody(), Context)) {
          const auto *Assign = Nodes.getNodeAs<BinaryOperator>("assign");
          // Also either of a conditional, `(bFirst ? pFirst : pSecond) =
          // &Local`.
          for (const DeclRefExpr *Pointer : assignedVariables(Assign->getLHS()))
            Assignments.emplace_back(Assign, Pointer, Assign->getRHS(),
                                     nullptr);
        }
        for (const auto &Nodes :
             match(findAll(callExpr().bind("setter")), *Function->getBody(),
                   Context)) {
          const auto *Setter = Nodes.getNodeAs<CallExpr>("setter");
          // Also a lambda, `fSet(pValue, &Value)`, whose arguments start
          // after it.
          unsigned Offset = 0;
          const FunctionDecl *Definition = setterCalled(Setter, Offset);
          if (!Definition)
            continue;
          auto ArgumentOf = [&](const ParmVarDecl *Parameter) -> const Expr * {
            const unsigned iArgument =
                Parameter->getFunctionScopeIndex() + Offset;
            return iArgument < Setter->getNumArgs() ? Setter->getArg(iArgument)
                                                    : nullptr;
          };
          for (const auto &Assigns :
               match(findAll(binaryOperator(hasOperatorName("=")).bind("in")),
                     *Definition->getBody(), Context)) {
            const auto *In = Assigns.getNodeAs<BinaryOperator>("in");
            const auto *Target =
                dyn_cast<DeclRefExpr>(In->getLHS()->IgnoreParenImpCasts());
            const auto *TargetParameter =
                Target ? dyn_cast<ParmVarDecl>(Target->getDecl()) : nullptr;
            if (!TargetParameter ||
                !TargetParameter->getType()->isLValueReferenceType() ||
                !TargetParameter->getType()
                     .getNonReferenceType()
                     ->isPointerType())
              continue;
            const Expr *PointerArgument = ArgumentOf(TargetParameter);
            const auto *PointerRef =
                PointerArgument ? dyn_cast<DeclRefExpr>(
                                      PointerArgument->IgnoreParenImpCasts())
                                : nullptr;
            if (!PointerRef)
              continue;
            // What it is pointed at: the address of what a reference
            // parameter is given, `&_Value`, also of a member of it,
            // `&_Value.m_Value`, which lives with it, or where a pointer
            // parameter points, `_pValue`, also one given by reference, `int
            // *const &_pValue`.
            const Expr *Value = In->getRHS()->IgnoreParenImpCasts();
            // Also through an unchanged local pointer made of it, `CValue
            // *pNew = &_Value; o_pValue = pNew;`.
            for (int Depth = 0; Depth < 8; ++Depth) {
              const auto *Ref = dyn_cast<DeclRefExpr>(Value);
              const auto *Local =
                  Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
              if (!Local || isa<ParmVarDecl>(Local) ||
                  !Local->hasLocalStorage() || !Local->getInit() ||
                  !Local->getType()->isPointerType() ||
                  isChangedVariable(Local))
                break;
              Value = Local->getInit()->IgnoreParenImpCasts();
            }
            const VarDecl *Known = nullptr;
            const Expr *Given = nullptr;
            const Expr *Addressed = nullptr;
            if (const auto *Address = dyn_cast<UnaryOperator>(Value);
                Address && Address->getOpcode() == UO_AddrOf) {
              Addressed = Address->getSubExpr()->IgnoreParenImpCasts();
              for (int Depth = 0; Depth < 16; ++Depth)
                if (const auto *Member = dyn_cast<MemberExpr>(Addressed);
                    Member && !Member->isArrow() &&
                    isa<FieldDecl>(Member->getMemberDecl()))
                  Addressed = Member->getBase()->IgnoreParenImpCasts();
                else if (const auto *Element =
                             dyn_cast<ArraySubscriptExpr>(Addressed);
                         Element && Element->getBase()
                                        ->IgnoreParenImpCasts()
                                        ->getType()
                                        ->isArrayType())
                  Addressed = Element->getBase()->IgnoreParenImpCasts();
                else
                  break;
            }
            if (Addressed)
              if (const auto *Of = dyn_cast<DeclRefExpr>(Addressed))
                if (const auto *Parameter = dyn_cast<ParmVarDecl>(Of->getDecl());
                    Parameter && Parameter->getType()->isReferenceType())
                  if (const Expr *Argument = ArgumentOf(Parameter))
                    if (const auto *Local = dyn_cast<DeclRefExpr>(
                            Argument->IgnoreParenImpCasts())) {
                      Known = dyn_cast<VarDecl>(Local->getDecl());
                      // Of a local reference, what it refers to, `CValue
                      // &Alias = Early`.
                      for (int Depth = 0;
                           Known && Known->getType()->isReferenceType() &&
                           Known->hasLocalStorage() &&
                           !isa<ParmVarDecl>(Known) && Known->getInit() &&
                           Depth < 8;
                           ++Depth) {
                        const auto *Bound = dyn_cast<DeclRefExpr>(
                            Known->getInit()->IgnoreParenImpCasts());
                        Known = Bound ? dyn_cast<VarDecl>(Bound->getDecl())
                                      : nullptr;
                      }
                    }
            if (const auto *Of = dyn_cast<DeclRefExpr>(Value))
              if (const auto *Parameter = dyn_cast<ParmVarDecl>(Of->getDecl());
                  Parameter &&
                  Parameter->getType().getNonReferenceType()->isPointerType())
                Given = ArgumentOf(Parameter);
            if (Known && Known->hasLocalStorage())
              Assignments.emplace_back(Setter, PointerRef, nullptr, Known);
            else if (Given)
              Assignments.emplace_back(Setter, PointerRef, Given, nullptr);
          }
        }
        for (const auto &Nodes : match(
                 findAll(callExpr(
                             callee(functionDecl(hasAnyName(
                                 vocabulary().ExchangeFunctions.names()))),
                             argumentCountIs(2),
                             hasArgument(0, ignoringParenImpCasts(
                                                declRefExpr().bind("pointer"))))
                             .bind("exchange")),
                 *Function->getBody(), Context)) {
          const auto *Exchange = Nodes.getNodeAs<CallExpr>("exchange");
          Assignments.emplace_back(Exchange,
                                   Nodes.getNodeAs<DeclRefExpr>("pointer"),
                                   Exchange->getArg(1), nullptr);
        }
        // A swap, `fg_Swap(pValue, pLate)`, assigns each the other.
        for (const auto &Nodes : match(findAll(callExpr().bind("swap")),
                                       *Function->getBody(), Context)) {
          const auto *Swap = Nodes.getNodeAs<CallExpr>("swap");
          const auto [Left, Right] = swappedArguments(Swap);
          if (!Left)
            continue;
          const Expr *Sides[2] = {Left, Right};
          for (int iSide = 0; iSide < 2; ++iSide)
            if (const auto *Pointer =
                    dyn_cast<DeclRefExpr>(Sides[iSide]->IgnoreParenImpCasts());
                Pointer && Pointer->getType()->isPointerType())
              Assignments.emplace_back(Swap, Pointer, Sides[1 - iSide],
                                       nullptr);
        }
      }
      // The lambdas called in the function, whose assignments run.
      llvm::SmallPtrSet<const FunctionDecl *, 8> Called;
      // Also one `fg_CallSafe` runs right away, `fg_CallSafe([&] { ... })`.
      // Not a call that never runs, in a branch that is discarded or never
      // taken, or in a lambda that isn't called itself.
      if (!Assignments.empty()) {
        llvm::SmallVector<std::pair<const LambdaExpr *, const FunctionDecl *>,
                          8>
            Calls;
        for (const auto &Nodes : match(findAll(callExpr().bind("call")),
                                       *Function->getBody(), Context)) {
          const auto *Call = Nodes.getNodeAs<CallExpr>("call");
          const LambdaExpr *Lambda = calledLambda(Call);
          if (!Lambda && calleeName(Call) == "fg_CallSafe" &&
              Call->getNumArgs() >= 1)
            Lambda = asLambda(Call->getArg(0));
          if (!Lambda || isNeverRun(Call, Context))
            continue;
          // The lambda it is in, of those in the function.
          const FunctionDecl *In = nullptr;
          DynTypedNode Node = DynTypedNode::create(*Call);
          for (int Depth = 0; Depth < 256 && !In; ++Depth) {
            const auto Parents = instantiatedParents(Node, Context);
            if (Parents.empty())
              break;
            // The body of a lambda is in its call operator.
            if (const auto *Operator = Parents[0].get<FunctionDecl>()) {
              if (Operator != Function && isLambdaCallOperator(Operator))
                In = Operator;
              break;
            }
            if (const auto *Around = Parents[0].get<LambdaExpr>();
                Around && Node.get<Stmt>() == Around->getBody())
              In = Around->getCallOperator();
            Node = Parents[0];
          }
          Calls.push_back({Lambda, In});
        }
        for (bool Added = true; Added;) {
          Added = false;
          for (const auto &[Lambda, In] : Calls)
            if ((!In || Called.contains(In)) &&
                Called.insert(Lambda->getCallOperator()).second)
              Added = true;
        }
      }
      // Not an assignment that doesn't run: in a branch of an `if constexpr`
      // that is discarded, or that a constant condition never takes, `if
      // (false)`, in a lambda that isn't called, or in the cleanup, which
      // runs after.
      auto Runs = [&](const Expr *Assign) {
        if (isInDiscardedBranch(DynTypedNode::create(*Assign), Context) ||
            isInNeverTakenBranch(DynTypedNode::create(*Assign), Context))
          return false;
        for (DynTypedNode Node = DynTypedNode::create(*Assign);;) {
          const auto Parents = instantiatedParents(Node, Context);
          if (Parents.empty())
            return true;
          Node = Parents[0];
          // The body of a lambda is in its call operator.
          if (const auto *Operator = Node.get<FunctionDecl>())
            return Operator == Function || !isLambdaCallOperator(Operator) ||
                   ((!Cleanup || Operator != Cleanup->getCallOperator()) &&
                    Called.contains(Operator));
        }
      };
      std::unique_ptr<CFG> FinishGraph;
      for (const auto &Assignment : Assignments) {
        const Expr *Assign = std::get<0>(Assignment);
        const DeclRefExpr *Ref = std::get<1>(Assignment);
        const Expr *Value = std::get<2>(Assignment);
        const VarDecl *KnownLocal = std::get<3>(Assignment);
        if (!Runs(Assign))
          continue;
        const auto *Pointer = dyn_cast<VarDecl>(Ref->getDecl());
        // Also through a reference to it, `int *&pAlias = pValue;`, also a
        // chain of them.
        for (int Depth = 0;
             Pointer && Depth < 64 && Pointer->getType()->isReferenceType() &&
             Pointer->getInit();
             ++Depth) {
          const auto *Init =
              dyn_cast<DeclRefExpr>(Pointer->getInit()->IgnoreParenImpCasts());
          Pointer = Init ? dyn_cast<VarDecl>(Init->getDecl()) : nullptr;
        }
        const VarDecl *Local = KnownLocal ? KnownLocal : LocalOf(Value, 0);
        if (!Pointer || !Pointers.contains(Pointer) || !Local ||
            !SM.isBeforeInTranslationUnit(Variable->getLocation(),
                                          Local->getLocation()) ||
            llvm::any_of(Finishing, [&](const DeclRefExpr *Guard) {
              return runsOnEveryPathBefore(Guard, Assign, Context);
            }))
          continue;
        // Or one of them on each path, like in either branch of an `if`.
        if (!Finishing.empty()) {
          if (!FinishGraph) {
            CFG::BuildOptions Options;
            Options.setAllAlwaysAdd();
            FinishGraph =
                CFG::buildCFG(Function, Function->getBody(), &Context, Options);
          }
          if (FinishGraph && passesOnEveryPath(*FinishGraph, Finishing, Assign)
                                 .value_or(false))
            continue;
        }
        // Nor where the block it is in runs the cleanup, `co_await
        // fg_Move(Cleanup)`, or points it elsewhere, `pValue = nullptr`, as a
        // later statement, before anything can leave it, while the local is
        // still there.
        {
          DynTypedNode Node = DynTypedNode::create(*Assign);
          const CompoundStmt *Block = nullptr;
          const Stmt *Statement = nullptr;
          for (int Depth = 0; Depth < 64 && !Block; ++Depth) {
            const auto Parents = instantiatedParents(Node, Context);
            if (Parents.empty() || Parents[0].get<LambdaExpr>() ||
                Parents[0].get<FunctionDecl>())
              break;
            Block = Parents[0].get<CompoundStmt>();
            Statement = Node.get<Stmt>();
            Node = Parents[0];
          }
          std::function<bool(const Stmt *)> Leaves = [&](const Stmt *Inner) {
            if (!Inner || isa<LambdaExpr>(Inner))
              return false;
            if (isa<ReturnStmt, CoreturnStmt, CXXThrowExpr, BreakStmt,
                    ContinueStmt, GotoStmt, IndirectGotoStmt,
                    CoroutineSuspendExpr>(Inner))
              return true;
            return llvm::any_of(childrenOnce(Inner), Leaves);
          };
          std::function<bool(const Stmt *, const Stmt *)> Contains =
              [&](const Stmt *Outer, const Stmt *Inner) {
                return Outer == Inner ||
                       (Outer && llvm::any_of(childrenOnce(Outer),
                                              [&](const Stmt *Child) {
                                                return Contains(Child, Inner);
                                              }));
              };
          // The local is there while the block is, when it is declared in
          // it or a block around it.
          const bool LocalThere =
              Block && Local->getLexicalDeclContext() == Function &&
              SM.isBeforeInTranslationUnit(Local->getLocation(),
                                           Assign->getBeginLoc());
          bool Done = false;
          bool After = false;
          if (Block && LocalThere)
            for (const Stmt *Each : Block->body()) {
              if (!After) {
                After = Each == Statement;
                continue;
              }
              if (llvm::any_of(Finishing, [&](const DeclRefExpr *Guard) {
                    return Contains(Each, Guard);
                  })) {
                Done = true;
                break;
              }
              if (const auto *Again = dyn_cast<BinaryOperator>(
                      isa<Expr>(Each) ? cast<Expr>(Each)->IgnoreImplicit()
                                      : nullptr);
                  Again && Again->getOpcode() == BO_Assign &&
                  llvm::any_of(assignedVariables(Again->getLHS()),
                               [&](const DeclRefExpr *Target) {
                                 return Target->getDecl() == Pointer;
                               }) &&
                  !LocalOf(Again->getRHS(), 0)) {
                Done = true;
                break;
              }
              if (Leaves(Each))
                break;
            }
          if (Done)
            continue;
        }
        const SourceLocation Loc = reportLocation(Assign->getBeginLoc(), SM);
        if (Loc.isValid())
          diag(Loc, "%0, which the cleanup of '%1' uses, is pointed at %2, "
                    "which is destroyed before the cleanup runs; point it at "
                    "what is declared before the result of '%1'")
              << Pointer << Name << Local;
      }
    }
  }

  // Also one a factory makes, `fg_AsyncDestroy(fMakeCleanup())`.
  if (Call->getNumArgs() >= 1) {
    if (const LambdaExpr *Cleanup = cleanupLambdaOf(Call->getArg(0)))
      checkCleanup(Cleanup, Context);
  }
}

namespace {

/// What refers to the storage of the variables a cleanup captures by
/// reference: the captured variables, references bound to them or to their
/// members and elements, like `auto &Sub = Captured.m_Value`, and pointers to
/// them, like `auto *pCaptured = &Captured` and copies of such pointers. What
/// a captured pointer points to is not in the captured storage, so neither
/// `auto *pCopy = pCaptured` nor `auto &Pointee = *pCaptured` refers to it.
///
/// References can't be rebound, so they are found once. Pointers can be
/// assigned, so whether one points into the storage is followed along the
/// control flow, in a bit per pointer that may.
class CleanupStorage : public OncePerTypeVisitor<CleanupStorage> {
public:
  explicit CleanupStorage(const LambdaExpr *Cleanup) {
    for (const LambdaCapture &Capture : Cleanup->captures()) {
      if (Capture.capturesVariable() && Capture.getCaptureKind() == LCK_ByRef &&
          !refersToStaticStorage(Capture.getCapturedVar()))
        Storage.insert(Capture.getCapturedVar());
    }
  }

  /// Whether \p Captured, a reference init capture, refers to what has static
  /// storage, which lives as long as the program, `[&Value = g_Value]`, also
  /// to a member or an element of it, or through a cast, also a local
  /// reference captured by reference, `[&Value]` of `int &Value = g_Value`,
  /// which captures what it refers to.
  static bool refersToStaticStorage(const ValueDecl *Captured) {
    const auto *Variable = dyn_cast<VarDecl>(Captured);
    if (!Variable || !Variable->getInit() ||
        !(Variable->isInitCapture() ||
          (Variable->getType()->isReferenceType() &&
           Variable->hasLocalStorage() && !isa<ParmVarDecl>(Variable))))
      return false;
    const Expr *Init = Variable->getInit();
    for (int Depth = 0; Depth < 32; ++Depth) {
      Init = Init->IgnoreParenImpCasts();
      if (const auto *Member = dyn_cast<MemberExpr>(Init);
          Member && !Member->isArrow())
        Init = Member->getBase();
      else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Init);
               Subscript && Subscript->getBase()
                                ->IgnoreParenImpCasts()
                                ->getType()
                                ->isArrayType())
        Init = Subscript->getBase();
      else if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Init);
               Cast && Cast->isGLValue())
        Init = Cast->getSubExpr();
      // Also through a local reference, `int &Alias = g_Value`, what it is
      // bound to.
      else if (const auto *Alias = dyn_cast<DeclRefExpr>(Init);
               Alias && isa<VarDecl>(Alias->getDecl()) &&
               cast<VarDecl>(Alias->getDecl())->getType()->isReferenceType() &&
               cast<VarDecl>(Alias->getDecl())->hasLocalStorage() &&
               !isa<ParmVarDecl>(Alias->getDecl()) &&
               cast<VarDecl>(Alias->getDecl())->getInit())
        Init = cast<VarDecl>(Alias->getDecl())->getInit();
      // Also what a function it is given to gives back by reference,
      // `fg_Const(g_Value)`.
      else if (const auto *Call = dyn_cast<CallExpr>(Init);
               Call && Call->isGLValue() && returnedArgument(Call))
        Init = returnedArgument(Call);
      else
        break;
    }
    const auto *Ref = dyn_cast<DeclRefExpr>(Init);
    const auto *Static = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    return Static && Static->hasGlobalStorage() &&
           Static->getTLSKind() == VarDecl::TLS_None &&
           !Static->getType()->isReferenceType();
  }

  bool empty() const { return Storage.empty(); }

  /// The pointers that may point into the storage, by their bits.
  llvm::DenseMap<const ValueDecl *, unsigned> Pointers;
  /// The captured variables and the references bound into them. A reference
  /// bound through a pointer, like `auto &Value = *pValue`, is added when the
  /// control flow reaches it with the pointer pointing into the storage.
  llvm::SmallPtrSet<const ValueDecl *, 8> Storage;
  /// The references that may be bound into the storage.
  llvm::SmallPtrSet<const ValueDecl *, 8> MaybeStorage;

  // Pointers that are initialized or assigned to point into the storage at
  // some point, and references that may be bound into it, in source order.
  bool VisitVarDecl(VarDecl *Variable) {
    if (!Variable->getInit())
      return true;
    const Expr *Init = withoutCleanups(Variable->getInit());
    if (Variable->getType()->isReferenceType() && isInStorage(Init, nullptr))
      MaybeStorage.insert(Variable);
    else if (Variable->getType()->isPointerType() &&
             pointsIntoStorage(Init, nullptr))
      Pointers.try_emplace(Variable, Pointers.size());
    return true;
  }

  /// Adds the references \p Declaration binds into the storage, with the
  /// pointers that point into it in \p Bits.
  void bindReferences(const DeclStmt *Declaration,
                      const llvm::BitVector &Bits) {
    for (const Decl *D : Declaration->decls()) {
      const auto *Variable = dyn_cast<VarDecl>(D);
      if (Variable && MaybeStorage.contains(Variable) &&
          isInStorage(withoutCleanups(Variable->getInit()), &Bits))
        Storage.insert(Variable);
    }
  }
  bool VisitBinaryOperator(BinaryOperator *Binary) {
    if (pointsIntoStorage(Binary->getRHS(), nullptr))
      for (const DeclRefExpr *Ref : assignedPointers(Binary))
        Pointers.try_emplace(aliasedPointer(Ref->getDecl()), Pointers.size());
    return true;
  }

  /// The local pointers \p Binary can assign, if it is an assignment of
  /// them: one, or either of a conditional, `(bFirst ? pFirst : pSecond) =
  /// ...`, which assigns only one of them.
  static llvm::SmallVector<const DeclRefExpr *, 2>
  assignedPointers(const BinaryOperator *Binary) {
    llvm::SmallVector<const DeclRefExpr *, 2> Pointers;
    if (Binary->getOpcode() != BO_Assign)
      return Pointers;
    for (const DeclRefExpr *Ref : assignedVariables(Binary->getLHS()))
      if (const auto *Variable =
              dyn_cast<VarDecl>(aliasedPointer(Ref->getDecl()));
          Variable && Variable->getType()->isPointerType())
        Pointers.push_back(Ref);
    return Pointers;
  }

  /// The local pointer that \p Declaration is, also through references to
  /// it, `int *&pAlias = pValue`, which can't be rebound.
  static const ValueDecl *aliasedPointer(const ValueDecl *Declaration) {
    for (int Depth = 0; Depth < 8; ++Depth) {
      const auto *Variable = dyn_cast_or_null<VarDecl>(Declaration);
      if (!Variable || !Variable->getType()->isReferenceType() ||
          !Variable->getType()->getPointeeType()->isPointerType() ||
          !Variable->getInit())
        break;
      const auto *Bound =
          dyn_cast<DeclRefExpr>(Variable->getInit()->IgnoreParenImpCasts());
      if (!Bound)
        break;
      Declaration = Bound->getDecl();
    }
    return Declaration;
  }

  static const Expr *withoutCleanups(const Expr *E) {
    if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(E))
      return Cleanups->getSubExpr();
    return E;
  }

  /// Whether the object \p E designates is in the storage. \p Bits says
  /// which pointers point into the storage, or null for any that may.
  bool isInStorage(const Expr *E, const llvm::BitVector *Bits,
                   int Depth = 0) const {
    if (Depth > 8)
      return false;
    E = E->IgnoreParens();
    if (const auto *Cast = dyn_cast<CastExpr>(E);
        Cast && (Cast->getCastKind() == CK_NoOp ||
                 Cast->getCastKind() == CK_DerivedToBase ||
                 Cast->getCastKind() == CK_UncheckedDerivedToBase ||
                 Cast->getCastKind() == CK_LValueBitCast))
      return isInStorage(Cast->getSubExpr(), Bits, Depth + 1);
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E))
      return Storage.contains(Ref->getDecl()) ||
             (!Bits && MaybeStorage.contains(Ref->getDecl()));
    if (const auto *Member = dyn_cast<MemberExpr>(E))
      return Member->isArrow()
                 ? pointsIntoStorage(Member->getBase(), Bits, Depth + 1)
                 : isInStorage(Member->getBase(), Bits, Depth + 1);
    // Binding a reference to `fg_Move(Captured)` doesn't move the object.
    if (const auto *Call = dyn_cast<CallExpr>(E);
        Call && Call->getNumArgs() == 1 &&
        asCallTo(Call, vocabulary().MoveOrForwardFunctions.names()))
      return isInStorage(Call->getArg(0), Bits, Depth + 1);
    if (const auto *Unary = dyn_cast<UnaryOperator>(E);
        Unary && Unary->getOpcode() == UO_Deref)
      return pointsIntoStorage(Unary->getSubExpr(), Bits, Depth + 1);
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E))
      return pointsIntoStorage(Subscript->getBase(), Bits, Depth + 1);
    // Either arm of `bChoose ? Value : Local`.
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E))
      return isInStorage(Conditional->getTrueExpr(), Bits, Depth + 1) ||
             isInStorage(Conditional->getFalseExpr(), Bits, Depth + 1);
    // What overloaded operators give, like `Captured[0]` or `*Captured` of a
    // container or an optional, is taken to be in the object.
    if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
        Call && Call->getNumArgs() >= 1 &&
        (Call->getOperator() == OO_Subscript ||
         Call->getOperator() == OO_Star || Call->getOperator() == OO_Arrow))
      return isInStorage(Call->getArg(0), Bits, Depth + 1);
    // Also what a getter gives by reference, `Name.f_Get()`.
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E))
      return getsFromStorage(Call, Bits, Depth);
    return false;
  }

  /// Whether the visible member function \p Call calls can give back what is
  /// in its object, `char const *f_GetStr() const { return m_Data; }` or
  /// `CValue const &f_Get() const { return m_Value; }`, on any of its
  /// returns, when \p Call is of what is in the storage.
  bool getsFromStorage(const CXXMemberCallExpr *Call,
                       const llvm::BitVector *Bits, int Depth) const {
    const CXXMethodDecl *Method = Call->getMethodDecl();
    const auto *Callee =
        dyn_cast<MemberExpr>(Call->getCallee()->IgnoreParens());
    if (!Method || !Callee || Method->isExplicitObjectMemberFunction())
      return false;
    // Of its object, `this`, also through members, `&m_Value`, and the arrays
    // in it, `m_Data`. Each once for each way and level it is met at: getters
    // that each return the one before more than once would be looked at again
    // through each path to them.
    llvm::DenseMap<std::tuple<const Expr *, unsigned, int>, bool> OfThisMemo;
    std::function<bool(const Expr *, bool, int)> OfThisUncached;
    auto OfThis = [&](const Expr *Value, bool Pointer, int Level) {
      if (!Value || Level > 16)
        return false;
      const std::tuple<const Expr *, unsigned, int> Key{Value, Pointer, Level};
      if (auto It = OfThisMemo.find(Key); It != OfThisMemo.end())
        return It->second;
      const bool Result = OfThisUncached(Value, Pointer, Level);
      OfThisMemo[Key] = Result;
      return Result;
    };
    OfThisUncached = [&](const Expr *Value, bool Pointer, int Level) {
      Value = Value->IgnoreParens();
      // Not a pointer it holds, `return m_pValue;`, which points
      // elsewhere.
      if (const auto *Cast = dyn_cast<CastExpr>(Value)) {
        switch (Cast->getCastKind()) {
        case CK_ArrayToPointerDecay:
          return Pointer && OfThis(Cast->getSubExpr(), false, Level + 1);
        case CK_NoOp:
        case CK_DerivedToBase:
        case CK_UncheckedDerivedToBase:
          return OfThis(Cast->getSubExpr(), Pointer, Level + 1);
        default:
          return false;
        }
      }
      if (isa<CXXThisExpr>(Value))
        return Pointer;
      // Also what another getter of the object gives, `return fp_GetStr();`,
      // also of an object in it, `return m_Imp.f_GetStr();`, but not a
      // virtual one, which an override can replace.
      if (const auto *Forwarded = dyn_cast<CXXMemberCallExpr>(Value);
          Forwarded && Forwarded->getMethodDecl() &&
          Forwarded->getImplicitObjectArgument() &&
          dyn_cast<MemberExpr>(Forwarded->getCallee()->IgnoreParens()) &&
          OfThis(Forwarded->getImplicitObjectArgument(),
                 cast<MemberExpr>(Forwarded->getCallee()->IgnoreParens())
                     ->isArrow(),
                 Level + 1) &&
          !Forwarded->getMethodDecl()->isVirtual() &&
          (Pointer ? Forwarded->getType()->isPointerType()
                   : Forwarded->isGLValue()))
        return llvm::any_of(returnedValues(Forwarded->getMethodDecl()),
                            [&](const Expr *Inner) {
                              return OfThis(Inner, Pointer, Level + 1);
                            });
      if (const auto *Address = dyn_cast<UnaryOperator>(Value);
          Address && Pointer && Address->getOpcode() == UO_AddrOf)
        return OfThis(Address->getSubExpr(), false, Level + 1);
      if (const auto *Deref = dyn_cast<UnaryOperator>(Value);
          Deref && !Pointer && Deref->getOpcode() == UO_Deref)
        return OfThis(Deref->getSubExpr(), true, Level + 1);
      if (const auto *Member = dyn_cast<MemberExpr>(Value); Member && !Pointer)
        return OfThis(Member->getBase(), Member->isArrow(), Level + 1);
      if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Value);
          Subscript && !Pointer)
        return OfThis(Subscript->getBase(), true, Level + 1);
      if (const auto *Conditional =
              dyn_cast<AbstractConditionalOperator>(Value))
        return OfThis(Conditional->getTrueExpr(), Pointer, Level + 1) ||
               OfThis(Conditional->getFalseExpr(), Pointer, Level + 1);
      return false;
    };
    const QualType Returned = Method->getReturnType();
    const bool Pointer = Returned->isPointerType();
    if (!Pointer && !Returned->isReferenceType())
      return false;
    for (const Expr *Value : returnedValues(Method))
      if (OfThis(Value, Pointer, 0))
        return Callee->isArrow()
                   ? pointsIntoStorage(Call->getImplicitObjectArgument(), Bits,
                                       Depth + 1)
                   : isInStorage(Call->getImplicitObjectArgument(), Bits,
                                 Depth + 1);
    return false;
  }

  /// Whether the pointer value \p E points into the storage.
  bool pointsIntoStorage(const Expr *E, const llvm::BitVector *Bits,
                         int Depth = 0) const {
    if (Depth > 8)
      return false;
    E = withoutCleanups(E)->IgnoreParens();
    if (const auto *Cast = dyn_cast<CastExpr>(E)) {
      switch (Cast->getCastKind()) {
      case CK_ArrayToPointerDecay:
        return isInStorage(Cast->getSubExpr(), Bits, Depth + 1);
      case CK_LValueToRValue: {
        const auto *Ref =
            dyn_cast<DeclRefExpr>(Cast->getSubExpr()->IgnoreParens());
        if (!Ref)
          return false;
        // Also the value of a pointer it captures by reference, `[&pValue]`,
        // that points to a local of the function it is in, unchanged, `int
        // *pValue = &Value`, which is gone like what it captures.
        if (const auto *Captured = dyn_cast<VarDecl>(Ref->getDecl());
            Captured && Storage.contains(Captured) &&
            Captured->getType()->isPointerType() && Captured->getInit() &&
            !isChangedVariable(Captured))
          if (const Expr *Addressed = addressedBy(
                  withoutCleanups(Captured->getInit())->IgnoreParenImpCasts()))
            if (const auto *Local =
                    dyn_cast<DeclRefExpr>(Addressed->IgnoreParenImpCasts());
                Local && isa<VarDecl>(Local->getDecl()) &&
                cast<VarDecl>(Local->getDecl())->hasLocalStorage() &&
                !cast<VarDecl>(Local->getDecl())->getType()->isReferenceType())
              return true;
        auto It = Pointers.find(aliasedPointer(Ref->getDecl()));
        return It != Pointers.end() && (!Bits || Bits->test(It->second));
      }
      case CK_NoOp:
      case CK_BitCast:
      case CK_DerivedToBase:
      case CK_UncheckedDerivedToBase:
        return pointsIntoStorage(Cast->getSubExpr(), Bits, Depth + 1);
      default:
        return false;
      }
    }
    if (const Expr *Addressed = addressedBy(E))
      return isInStorage(Addressed, Bits, Depth + 1);
    if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
        Call && Call->getNumArgs() >= 1 && Call->getOperator() == OO_Arrow)
      return isInStorage(Call->getArg(0), Bits, Depth + 1);
    // Pointer arithmetic stays in the same object, like `Values + 1`.
    if (const auto *Binary = dyn_cast<BinaryOperator>(E);
        Binary && Binary->isAdditiveOp()) {
      if (Binary->getLHS()->getType()->isPointerType())
        return pointsIntoStorage(Binary->getLHS(), Bits, Depth + 1);
      if (Binary->getRHS()->getType()->isPointerType())
        return pointsIntoStorage(Binary->getRHS(), Bits, Depth + 1);
    }
    // Either arm of `bFlag ? &Value : &Local`.
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E))
      return pointsIntoStorage(Conditional->getTrueExpr(), Bits, Depth + 1) ||
             pointsIntoStorage(Conditional->getFalseExpr(), Bits, Depth + 1);
    // What a getter gives, `Name.f_GetStr()`, and an argument a function
    // gives back, `fg_Identity(pValue)`.
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E))
      return getsFromStorage(Call, Bits, Depth);
    if (const auto *Call = dyn_cast<CallExpr>(E))
      if (const Expr *Given = returnedArgument(Call))
        return pointsIntoStorage(Given, Bits, Depth + 1);
    return false;
  }
};

/// Finds the references to local pointers that assign them, which don't
/// use what they point to.
class AssignedPointerFinder : public OncePerTypeVisitor<AssignedPointerFinder> {
public:
  llvm::SmallPtrSet<const Expr *, 8> Assigned;

  bool VisitBinaryOperator(BinaryOperator *Binary) {
    for (const DeclRefExpr *Ref : CleanupStorage::assignedPointers(Binary))
      Assigned.insert(Ref);
    return true;
  }
};

} // namespace

void AsyncDestroyCheck::checkCleanup(const LambdaExpr *Cleanup,
                                     ASTContext &Context) {
  CleanupStorage Storage(Cleanup);
  if (Storage.empty())
    return;
  // In code that runs to completion, the cleanup ends before the coroutine
  // does, while its parameters are there, and before an early await of its
  // guard, while the locals declared before the guard are: those outlive the
  // cleanup, like the reference capture check tells.
  if (inRunsToCompletionContext(Cleanup, Context, /*AfterSwitch=*/true)) {
    const std::optional<RunsToCompletionJoin> Join =
        lambdaJoin(Cleanup, Context, nullptr);
    llvm::SmallVector<const ValueDecl *, 8> Outliving;
    for (const ValueDecl *Variable : Storage.Storage)
      if ((isa<ParmVarDecl>(Variable) &&
           outlivesEnclosing(Variable, Cleanup)) ||
          (Join && outlivesJoin(Variable, *Join, Cleanup, Cleanup, Context)))
        Outliving.push_back(Variable);
    for (const ValueDecl *Variable : Outliving)
      Storage.Storage.erase(Variable);
    if (Storage.empty())
      return;
  }
  // A pointer copied from one that is only found later in the source, like in
  // a loop, is found by going over the body until nothing new is.
  for (size_t nFound = SIZE_MAX;
       nFound != Storage.Pointers.size() + Storage.MaybeStorage.size();) {
    nFound = Storage.Pointers.size() + Storage.MaybeStorage.size();
    Storage.TraverseStmt(Cleanup->getBody());
  }
  AssignedPointerFinder Assignments;
  Assignments.TraverseStmt(Cleanup->getBody());

  const FunctionDecl *Function = Cleanup->getCallOperator();
  Stmt *Body = Function->getBody();
  if (!Body)
    return;
  if (auto *CoroutineBody = dyn_cast<CoroutineBodyStmt>(Body))
    Body = CoroutineBody->getBody();
  CFG::BuildOptions Options;
  Options.setAllAlwaysAdd();
  // Exceptions can reach handlers after a suspension.
  Options.AddEHEdges = true;
  const std::unique_ptr<CFG> Graph =
      CFG::buildCFG(Function, Body, &Context, Options);
  // Not accepted when its control flow can't be built, like through a long
  // chain of conditionals in default arguments that destroy temporaries.
  if (!Graph) {
    const SourceLocation Loc =
        reportLocation(Cleanup->getBeginLoc(), Context.getSourceManager());
    if (Loc.isValid())
      diag(Loc, "the control flow of the cleanup is too complex to analyze, "
                "so whether it uses what is gone when it runs can't be told; "
                "split up what makes it complex");
    return;
  }
  // Blocks that can't run, like `if (false)` branches, don't change the state.
  const llvm::BitVector Reachable = reachableBlocks(*Graph);
  const auto SuspendPoints = suspendPoints(Body);

  // The references are valid until the first suspension of the cleanup, so
  // whatever it needs after it is copied or moved into locals first. Follow
  // the control flow, so a use in a later iteration of a loop that suspends
  // counts too, and so does a pointer assigned to point into the storage
  // after it was declared.
  // Of each pointer, whether it can point into the storage, and whether it
  // can on a path that has suspended, which is what is gone: not one that
  // points elsewhere where it suspends, `if (_bAsync) { pText = &Copy;
  // co_await fg_Work(); }`, merged with one that points into it where it
  // doesn't.
  struct CState {
    bool Suspended = false;
    llvm::BitVector Pointers;
    llvm::BitVector Stale;

    bool operator!=(const CState &Other) const {
      return Suspended != Other.Suspended || Pointers != Other.Pointers ||
             Stale != Other.Stale;
    }
    void merge(const CState &Other) {
      Suspended |= Other.Suspended;
      Pointers |= Other.Pointers;
      Stale |= Other.Stale;
    }
    void set(unsigned Index, bool Into) {
      Pointers[Index] = Into;
      Stale[Index] = Into && Suspended;
    }
  };
  // A lambda copies the pointers it captures when it is made, so each copy
  // has a bit of its own, which a call of the lambda uses.
  llvm::DenseMap<std::pair<const LambdaExpr *, const ValueDecl *>, unsigned>
      LambdaCopies;
  for (const auto &Nodes : match(findAll(lambdaExpr().bind("lambda")),
                                 *Cleanup->getBody(), Context)) {
    const auto *Inner = Nodes.getNodeAs<LambdaExpr>("lambda");
    for (const LambdaCapture &Capture : Inner->captures()) {
      if (!Capture.capturesVariable())
        continue;
      // A copy of a pointer, or an init capture, like `[pValue = &Value]` or
      // `[&Sub = Value.m_Sub]`, which is initialized where the lambda is made.
      const auto *Variable = dyn_cast<VarDecl>(Capture.getCapturedVar());
      const bool InitCapture = Variable && Variable->isInitCapture() &&
                               Variable->getInit() &&
                               (Variable->getType()->isPointerType() ||
                                Variable->getType()->isReferenceType());
      if (InitCapture ||
          (Capture.getCaptureKind() == LCK_ByCopy &&
           Storage.Pointers.count(
               CleanupStorage::aliasedPointer(Capture.getCapturedVar()))))
        LambdaCopies.try_emplace({Inner, Capture.getCapturedVar()},
                                 Storage.Pointers.size() + LambdaCopies.size());
    }
  }
  const unsigned nPointers = Storage.Pointers.size() + LambdaCopies.size();
  auto Transfer = [&](const CFGBlock *Block, CState State, auto &&OnUse) {
    for (const CFGElement &Element : *Block) {
      const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>();
      if (!Statement)
        continue;
      const Stmt *S = Statement->getStmt();
      // Not one that only names a static member, `Digest.mc_Size`, which
      // doesn't need the object.
      auto NamesStatic = [&](const DeclRefExpr *Ref) {
        DynTypedNode Node = DynTypedNode::create(*Ref);
        for (int Depth = 0; Depth < 8; ++Depth) {
          const auto Parents = instantiatedParents(Node, Context);
          if (Parents.empty())
            return false;
          if (Parents[0].get<ImplicitCastExpr>() ||
              Parents[0].get<ParenExpr>()) {
            Node = Parents[0];
            continue;
          }
          const auto *Member = Parents[0].get<MemberExpr>();
          if (!Member)
            return false;
          const ValueDecl *Named = Member->getMemberDecl();
          const auto *Method = dyn_cast<CXXMethodDecl>(Named);
          return (isa<VarDecl>(Named) &&
                  cast<VarDecl>(Named)->isStaticDataMember()) ||
                 (Method && Method->isStatic()) || isa<EnumConstantDecl>(Named);
        }
        return false;
      };
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
          Ref && State.Suspended && !NamesStatic(Ref)) {
        auto It = Storage.Pointers.find(
            CleanupStorage::aliasedPointer(Ref->getDecl()));
        if (Storage.Storage.contains(Ref->getDecl()) ||
            (It != Storage.Pointers.end() && State.Stale.test(It->second) &&
             !Assignments.Assigned.contains(Ref)))
          OnUse(Ref->getBeginLoc(), Ref->getDecl());
      }
      // A lambda that captures the storage by reference and is called after
      // the suspension, like `fRead()` or `fRead.operator()()` with `auto
      // fRead = [&Value] { ... }`, uses it then, also through the local
      // lambdas it captures, which it can call, `[&fRead] { return fRead();
      // }`.
      llvm::SmallPtrSet<const LambdaExpr *, 4> Checked;
      std::function<void(const LambdaExpr *)> CheckInvoked =
          [&](const LambdaExpr *Invoked) {
            if (!Invoked || !Checked.insert(Invoked).second)
              return;
            auto Init = Invoked->capture_init_begin();
            for (const LambdaCapture &Capture : Invoked->captures()) {
              const Expr *CaptureInit = *Init++;
              if (!Capture.capturesVariable())
                continue;
              auto Copy =
                  LambdaCopies.find({Invoked, Capture.getCapturedVar()});
              const auto *Variable =
                  dyn_cast<VarDecl>(Capture.getCapturedVar());
              if (Variable && Variable->isInitCapture()) {
                if (Copy != LambdaCopies.end() &&
                    State.Stale.test(Copy->second))
                  OnUse(Capture.getLocation(), Variable);
                continue;
              }
              if (Variable && Variable->hasLocalStorage() &&
                  Variable->getInit())
                CheckInvoked(dyn_cast<LambdaExpr>(
                    CleanupStorage::withoutCleanups(Variable->getInit())
                        ->IgnoreParenImpCasts()));
              const auto *Captured = dyn_cast_or_null<DeclRefExpr>(
                  CaptureInit ? CaptureInit->IgnoreParenImpCasts() : nullptr);
              if (!Captured)
                continue;
              // A reference to the storage, a copy of a pointer into it, or a
              // reference to such a pointer, which points where it does now.
              auto Pointer = Storage.Pointers.find(
                  CleanupStorage::aliasedPointer(Captured->getDecl()));
              if ((Capture.getCaptureKind() == LCK_ByRef &&
                   (Storage.Storage.contains(Captured->getDecl()) ||
                    (Pointer != Storage.Pointers.end() &&
                     State.Stale.test(Pointer->second)))) ||
                  (Copy != LambdaCopies.end() &&
                   State.Stale.test(Copy->second)))
                OnUse(Captured->getBeginLoc(), Captured->getDecl());
            }
          };
      if (const auto *Call = dyn_cast<CallExpr>(S); Call && State.Suspended)
        CheckInvoked(calledLambda(Call));
      // What a call or subscript after the suspension uses that is evaluated
      // before it, the object of a member function, `Value.f_Read(co_await
      // fg_Work())`, what is given by reference or pointed to by a pointer
      // given, `fg_Read(&Value, co_await fg_Work())`, and the array or
      // pointer indexed, `Values[(co_await fg_Work(), 0)]`.
      if (State.Suspended) {
        llvm::SmallVector<const Expr *, 4> Early;
        llvm::SmallVector<const DeclRefExpr *, 2> EarlyPointers;
        if (const auto *Member = dyn_cast<CXXMemberCallExpr>(S))
          Early.push_back(Member->getImplicitObjectArgument());
        else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(S))
          Early.push_back(Subscript->getBase());
        // Also of a constructor, `CRead{Value, co_await fg_Work()}`, and
        // through a pointer to a function.
        const auto *Call = dyn_cast<CallExpr>(S);
        const auto *Construct = dyn_cast<CXXConstructExpr>(S);
        if (Call || Construct) {
          const auto *Called = cast<Expr>(S);
          const unsigned nArguments =
              Call ? Call->getNumArgs() : Construct->getNumArgs();
          auto ArgumentAt = [&](unsigned iArgument) {
            return Call ? Call->getArg(iArgument)
                        : Construct->getArg(iArgument);
          };
          const auto *Method =
              Call ? dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee())
                   : nullptr;
          if (isa_and_nonnull<CXXOperatorCallExpr>(Call) && Method &&
              !Method->isExplicitObjectMemberFunction() && nArguments >= 1)
            Early.push_back(Call->getArg(0));
          for (unsigned iArgument = 0; iArgument < nArguments; ++iArgument) {
            const QualType Type = argumentParameterType(Called, iArgument);
            if (Type.isNull())
              continue;
            const Expr *Argument = ArgumentAt(iArgument);
            if (Type->isReferenceType()) {
              Early.push_back(Argument);
              continue;
            }
            if (!Type->isPointerType())
              continue;
            // Also cast, `static_cast<int const *>(&Value)`.
            Argument = Argument->IgnoreParenCasts();
            if (const Expr *Addressed = addressedBy(Argument))
              Early.push_back(Addressed);
            else if (const auto *Ref = dyn_cast<DeclRefExpr>(Argument))
              EarlyPointers.push_back(Ref);
          }
        }
        // A pointer is read before, and what it points to used after.
        for (const DeclRefExpr *Ref : EarlyPointers) {
          auto It = Storage.Pointers.find(
              CleanupStorage::aliasedPointer(Ref->getDecl()));
          if (It != Storage.Pointers.end() && State.Stale.test(It->second) &&
              !Assignments.Assigned.contains(Ref))
            OnUse(Ref->getBeginLoc(), Ref->getDecl());
        }
        // Also each that a conditional can choose, `_bFirst ? First :
        // Second`, or the one a constant condition chooses.
        llvm::SmallVector<const Expr *, 8> Used(Early.begin(), Early.end());
        for (size_t Index = 0; Index < Used.size() && Index < 256; ++Index) {
          const Expr *E = Used[Index];
          if (!E)
            continue;
          // Also through what a pointer to it points to, `(&Value)[0]` or
          // `(*pValue)`.
          E = E->IgnoreParenImpCasts();
          for (int Depth = 0; Depth < 16; ++Depth)
            if (const auto *Member = dyn_cast<MemberExpr>(E);
                Member && !Member->isArrow())
              E = Member->getBase()->IgnoreParenImpCasts();
            else if (const auto *Unary = dyn_cast<UnaryOperator>(E);
                     Unary && (Unary->getOpcode() == UO_Deref ||
                               Unary->getOpcode() == UO_AddrOf))
              E = Unary->getSubExpr()->IgnoreParenImpCasts();
            else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E))
              E = Subscript->getBase()->IgnoreParenImpCasts();
            else
              break;
          if (const auto *Member = dyn_cast<MemberExpr>(E))
            E = Member->getBase()->IgnoreParenImpCasts();
          if (const auto *Conditional =
                  dyn_cast<AbstractConditionalOperator>(E)) {
            const auto *Binary = dyn_cast<BinaryConditionalOperator>(E);
            const Expr *True =
                Binary ? Binary->getCommon() : Conditional->getTrueExpr();
            bool Chosen = false;
            if (!Conditional->getCond()->isValueDependent() &&
                Conditional->getCond()->EvaluateAsBooleanCondition(Chosen,
                                                                   Context))
              Used.push_back(Chosen ? True : Conditional->getFalseExpr());
            else
              Used.append({True, Conditional->getFalseExpr()});
            continue;
          }
          const auto *Ref = dyn_cast<DeclRefExpr>(E);
          if (!Ref)
            continue;
          auto It = Storage.Pointers.find(
              CleanupStorage::aliasedPointer(Ref->getDecl()));
          if (Storage.Storage.contains(Ref->getDecl()) ||
              (It != Storage.Pointers.end() && State.Stale.test(It->second) &&
               !Assignments.Assigned.contains(Ref)))
            OnUse(Ref->getBeginLoc(), Ref->getDecl());
        }
      }
      if (const auto *Made = dyn_cast<LambdaExpr>(S)) {
        for (const auto &[Key, Index] : LambdaCopies) {
          if (Key.first != Made)
            continue;
          const auto *Variable = dyn_cast<VarDecl>(Key.second);
          if (Variable && Variable->isInitCapture()) {
            State.set(Index,
                      Variable->getType()->isReferenceType()
                          ? Storage.isInStorage(CleanupStorage::withoutCleanups(
                                                    Variable->getInit()),
                                                &State.Pointers)
                          : Storage.pointsIntoStorage(Variable->getInit(),
                                                      &State.Pointers));
          } else {
            // A copy is gone where what it copies is.
            const unsigned Copied = Storage.Pointers.lookup(
                CleanupStorage::aliasedPointer(Key.second));
            State.Pointers[Index] = State.Pointers.test(Copied);
            State.Stale[Index] = State.Stale.test(Copied);
          }
        }
      }
      if (const auto *Declaration = dyn_cast<DeclStmt>(S)) {
        // A reference bound points where it is bound, gone if that is.
        const llvm::BitVector Before = State.Pointers;
        Storage.bindReferences(Declaration, State.Pointers);
        for (unsigned Index = 0; Index < nPointers; ++Index)
          if (State.Pointers[Index] != Before[Index])
            State.Stale[Index] = State.Pointers[Index] && State.Suspended;
        for (const Decl *D : Declaration->decls()) {
          const auto *Variable = dyn_cast<VarDecl>(D);
          auto It = Variable ? Storage.Pointers.find(Variable)
                             : Storage.Pointers.end();
          if (It != Storage.Pointers.end())
            State.set(It->second,
                      Variable->getInit() &&
                          Storage.pointsIntoStorage(Variable->getInit(),
                                                    &State.Pointers));
        }
      } else if (const auto *Binary = dyn_cast<BinaryOperator>(S)) {
        // Either of a conditional may be assigned, and keep what it held.
        const llvm::SmallVector<const DeclRefExpr *, 2> Assigned =
            CleanupStorage::assignedPointers(Binary);
        const bool Into =
            !Assigned.empty() &&
            Storage.pointsIntoStorage(Binary->getRHS(), &State.Pointers);
        for (const DeclRefExpr *Ref : Assigned)
          if (auto It = Storage.Pointers.find(
                  CleanupStorage::aliasedPointer(Ref->getDecl()));
              It != Storage.Pointers.end()) {
            // Either of a conditional keeps what it was, also gone.
            const bool Kept = Assigned.size() > 1;
            const bool Stale = Kept && State.Stale[It->second];
            State.set(It->second, Into || (Kept && State.Pointers[It->second]));
            State.Stale[It->second] = State.Stale[It->second] || Stale;
          }
      }
      if (isSuspension(S) || SuspendPoints.contains(S)) {
        State.Suspended = true;
        State.Stale |= State.Pointers;
      }
    }
    return State;
  };
  llvm::DenseMap<const CFGBlock *, CState> StateAtEnd;
  auto Entry = [&](const CFGBlock *Block) {
    CState State{false, llvm::BitVector(nPointers), llvm::BitVector(nPointers)};
    for (const CFGBlock::AdjacentBlock &Predecessor : Block->preds()) {
      if (const CFGBlock *Pred = Predecessor.getReachableBlock()) {
        if (auto It = StateAtEnd.find(Pred); It != StateAtEnd.end())
          State.merge(It->second);
      }
    }
    return State;
  };
  // Bits and references bound into the storage are only ever added, so this
  // ends.
  const llvm::SmallVector<const CFGBlock *, 16> Order = forwardOrder(*Graph);
  for (bool Changed = true; Changed;) {
    const size_t nBound = Storage.Storage.size();
    Changed = false;
    for (const CFGBlock *Block : Order) {
      if (!Reachable.test(Block->getBlockID()))
        continue;
      const CState End = Transfer(Block, Entry(Block),
                                  [](SourceLocation, const ValueDecl *) {});
      auto [It, Inserted] = StateAtEnd.try_emplace(
          Block, CState{false, llvm::BitVector(nPointers),
                        llvm::BitVector(nPointers)});
      CState Merged = It->second;
      Merged.merge(End);
      if (Inserted || Merged != It->second) {
        It->second = Merged;
        Changed = true;
      }
    }
    if (Storage.Storage.size() != nBound)
      Changed = true;
  }

  const SourceManager &SM = Context.getSourceManager();
  llvm::SmallPtrSet<const ValueDecl *, 4> Reported;
  for (const CFGBlock *Block : *Graph) {
    if (!Reachable.test(Block->getBlockID()))
      continue;
    Transfer(Block, Entry(Block),
             [&](SourceLocation UseLoc, const ValueDecl *Used) {
               if (!Reported.insert(Used).second)
                 return;
               const SourceLocation Loc = reportLocation(UseLoc, SM);
               if (Loc.isValid())
                 diag(Loc, "the cleanup function of 'fg_AsyncDestroy' uses "
                           "%0, which it captures by reference, after its "
                           "first suspension, where the reference is gone; "
                           "copy or move it into a local before the "
                           "suspension")
                     << Used;
             });
  }
}

namespace {

/// Finds calls of member functions named `f_Destroy`.
class DestroyFinder : public OncePerTypeVisitor<DestroyFinder> {
public:
  explicit DestroyFinder(const ASTContext &Context) : Context(Context) {}

  const Expr *Found = nullptr;
  const ASTContext &Context;

  // Not a branch of an `if constexpr` that is discarded, nor one that a
  // constant condition never takes, `if (false)`, unless a jump can reach it.
  bool TraverseIfStmt(IfStmt *If) {
    if (If->isConstexpr() && !If->getCond()->isValueDependent())
      if (const std::optional<Stmt *> Case = If->getNondiscardedCase(Context)) {
        return TraverseStmt(If->getInit()) &&
               TraverseStmt(If->getConditionVariableDeclStmt()) &&
               TraverseStmt(If->getCond()) && TraverseStmt(*Case);
      }
    bool Taken = false;
    if (!If->isConstexpr() && !If->isConsteval() &&
        !If->getConditionVariable() && !If->getCond()->isValueDependent() &&
        If->getCond()->EvaluateAsBooleanCondition(Taken, Context) &&
        !hasJumpTarget(Taken ? If->getElse() : If->getThen()))
      return TraverseStmt(If->getInit()) && TraverseStmt(If->getCond()) &&
             TraverseStmt(Taken ? If->getThen() : If->getElse());
    return RecursiveASTVisitor::TraverseIfStmt(If);
  }

  // Nor what comes after what always leaves, `if constexpr (!c_bNeedsDestroy)
  // return;`, unless a jump can reach it.
  bool TraverseCompoundStmt(CompoundStmt *Compound) {
    bool Left = false;
    for (Stmt *Statement : Compound->body()) {
      if (Left && !hasJumpTarget(Statement))
        continue;
      Left = false;
      if (!TraverseStmt(Statement))
        return false;
      Left = alwaysLeaves(Statement, Context);
    }
    return true;
  }

  // Not what isn't evaluated, `sizeof(Resource.f_Destroy())`.
  bool TraverseUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *) {
    return true;
  }
  bool TraverseCXXNoexceptExpr(CXXNoexceptExpr *) { return true; }
  bool TraverseCXXTypeidExpr(CXXTypeidExpr *Typeid) {
    return !Typeid->isPotentiallyEvaluated() ||
           RecursiveASTVisitor::TraverseCXXTypeidExpr(Typeid);
  }
  // Nor a requirement, `requires { Resource.f_Destroy(); }`, which only
  // tells whether it is well-formed.
  bool TraverseRequiresExpr(RequiresExpr *) { return true; }
  // Nor what is in a type, `decltype(Resource.f_Destroy())`.
  bool TraverseDecltypeTypeLoc(DecltypeTypeLoc, bool = true) { return true; }
  bool TraverseTypeOfExprTypeLoc(TypeOfExprTypeLoc, bool = true) {
    return true;
  }
  // Nor a lambda that is made but not called where it is, whose body runs
  // only when it is called.
  bool TraverseLambdaExpr(LambdaExpr *Lambda) {
    for (Expr *Init : Lambda->capture_inits())
      if (!TraverseStmt(Init))
        return false;
    return true;
  }
  // One that is called runs its body, also held in a local, `fDestroy()`,
  // or called through its call operator by name, `[&] { ... }.operator()()`,
  // each once.
  // Also one a scope exit made in it runs, `auto Inner = g_OnScopeExit / [&]
  // { ... };`, where the scope ends.
  bool TraverseCXXOperatorCallExpr(CXXOperatorCallExpr *Call) {
    if (Call->getOperator() == OO_Call && Call->getNumArgs() >= 1)
      if (!traverseCalled(calledLambda(Call->getArg(0))))
        return false;
    if (Call->getOperator() == OO_Slash && Call->getNumArgs() == 2 &&
        isScopeExitHelper(skipWrappers(Call->getArg(0)), /*AlsoActor=*/false))
      if (!traverseCalled(asLambda(Call->getArg(1))))
        return false;
    return RecursiveASTVisitor::TraverseCXXOperatorCallExpr(Call);
  }
  // Nor the value of a conditional that a constant condition doesn't choose,
  // `t_bOwnCallback ? fg_Move(_Callback).f_Destroy() : TCFuture<int>{}`.
  bool TraverseConditionalOperator(ConditionalOperator *Conditional) {
    bool Taken = false;
    if (!Conditional->getCond()->isValueDependent() &&
        Conditional->getCond()->EvaluateAsBooleanCondition(Taken, Context))
      return TraverseStmt(Conditional->getCond()) &&
             TraverseStmt(Taken ? Conditional->getTrueExpr()
                                : Conditional->getFalseExpr());
    return RecursiveASTVisitor::TraverseConditionalOperator(Conditional);
  }
  // Also a member function whose body can be seen, `Subscription.
  // f_TryDestroy()` of `fg_Move(m_Callback).f_Destroy();`, but not a virtual
  // one, which an override can replace, nor an `f_Destroy` that returns
  // nothing, which is synchronous whatever it starts, each once.
  bool TraverseCXXMemberCallExpr(CXXMemberCallExpr *Call) {
    if (const CXXMethodDecl *Method = Call->getMethodDecl();
        Method && Method->getOverloadedOperator() == OO_Call &&
        Call->getImplicitObjectArgument())
      if (!traverseCalled(calledLambda(Call->getImplicitObjectArgument())))
        return false;
    if (const CXXMethodDecl *Method = Call->getMethodDecl();
        Method && Method->getOverloadedOperator() == OO_None &&
        !(Method->getIdentifier() && Method->getName() == "f_Destroy") &&
        !(Method->isVirtual() && !Method->hasAttr<FinalAttr>() &&
          !Method->getParent()->hasAttr<FinalAttr>()))
      if (!traverseFunction(Method))
        return false;
    return RecursiveASTVisitor::TraverseCXXMemberCallExpr(Call);
  }
  // Also one `std::invoke` or `fg_CallSafe` calls, `std::invoke([&] { ... })`,
  // and a function
  // whose body can be seen, which tears down what it is given,
  // `fg_DestroySubscription(Resource)` of `return _Resource.f_Destroy();`,
  // also a static member function, `CSubscription::fs_Destroy(Callback)`,
  // each once.
  bool TraverseCallExpr(CallExpr *Call) {
    if (isStdInvoke(Call)) {
      if (!traverseCalled(calledLambda(Call->getArg(0))))
        return false;
    } else if (calleeName(Call) == "fg_OnScopeExit" &&
               Call->getNumArgs() >= 1) {
      if (!traverseCalled(asLambda(Call->getArg(0))))
        return false;
    } else if (calleeName(Call) == "fg_CallSafe" && Call->getNumArgs() >= 1) {
      // Also what `fg_CallSafe` calls right away, `fg_CallSafe([...]() ->
      // TCFuture<int> { co_await fg_Move(Actor).f_Destroy(); ... })`.
      if (!traverseCalled(asLambda(Call->getArg(0))))
        return false;
    } else if (const FunctionDecl *Callee = Call->getDirectCallee();
               Callee && (!isa<CXXMethodDecl>(Callee) ||
                          cast<CXXMethodDecl>(Callee)->isStatic()))
      if (!traverseFunction(Callee))
        return false;
    return RecursiveASTVisitor::TraverseCallExpr(Call);
  }
  bool traverseCalled(const LambdaExpr *Lambda) {
    if (!Lambda || !Called.insert(Lambda).second)
      return true;
    return TraverseStmt(Lambda->getBody());
  }
  bool traverseFunction(const FunctionDecl *Function) {
    const FunctionDecl *Definition = Function->getDefinition();
    if (!Definition || !Definition->getBody() ||
        !CalledFunctions.insert(Definition).second ||
        CalledFunctions.size() > 64)
      return true;
    return TraverseStmt(Definition->getBody());
  }
  llvm::SmallPtrSet<const LambdaExpr *, 4> Called;
  llvm::SmallPtrSet<const FunctionDecl *, 4> CalledFunctions;

  // A synchronous `f_Destroy` that returns nothing is not asynchronous
  // teardown.
  bool VisitCXXMemberCallExpr(CXXMemberCallExpr *Call) {
    const CXXMethodDecl *Method = Call->getMethodDecl();
    // Also through a pointer to it, `(Resource.*fDestroy)()` with an
    // unchanged local `auto fDestroy = &CResource::f_Destroy`.
    if (!Method)
      if (const auto *Through =
              dyn_cast<BinaryOperator>(Call->getCallee()->IgnoreParens());
          Through && Through->isPtrMemOp()) {
        const Expr *Pointer = Through->getRHS()->IgnoreParenImpCasts();
        for (int Depth = 0; Depth < 8; ++Depth) {
          const auto *Ref = dyn_cast<DeclRefExpr>(Pointer);
          const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
              !Local->getInit() || isChangedVariable(Local))
            break;
          Pointer = Local->getInit()->IgnoreParenImpCasts();
        }
        if (const auto *Address = dyn_cast<UnaryOperator>(Pointer);
            Address && Address->getOpcode() == UO_AddrOf)
          if (const auto *Named = dyn_cast<DeclRefExpr>(
                  Address->getSubExpr()->IgnoreParenImpCasts()))
            Method = dyn_cast<CXXMethodDecl>(Named->getDecl());
      }
    if (Method && Method->getIdentifier() && Method->getName() == "f_Destroy" &&
        !Call->getType()->isVoidType())
      Found = Call;
    return !Found;
  }
  // Also one with an explicit object parameter, `f_Destroy(this CResource
  // &&_Self)`, called as `fg_Move(Resource).f_Destroy()`.
  bool VisitCallExpr(CallExpr *Call) {
    if (const auto *Method =
            dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
        Method && Method->isExplicitObjectMemberFunction() &&
        Method->getIdentifier() && Method->getName() == "f_Destroy" &&
        !Call->getType()->isVoidType())
      Found = Call;
    return !Found;
  }
};

} // namespace

void AsyncDestroyCheck::checkScopeExit(const Expr *Divide, const Expr *Left,
                                       const Expr *Right,
                                       ASTContext &Context) {
  // Without a helper, made by `fg_OnScopeExit`.
  if (Left && !isScopeExitHelper(skipWrappers(Left), /*AlsoActor=*/false))
    return;
  // Also kept in a local before, `g_OnScopeExit / fg_Move(fCleanup)`.
  const LambdaExpr *Lambda = asLambda(Right);
  if (!Lambda)
    return;
  DestroyFinder Finder(Context);
  Finder.TraverseStmt(Lambda->getBody());
  if (!Finder.Found)
    return;
  // Only a coroutine can await the asynchronous teardown.
  const FunctionDecl *Function =
      enclosingFunction(DynTypedNode::create(*Divide), Context);
  if (!Function || !isCoroutine(Function))
    return;
  const SourceLocation Loc =
      reportLocation(Divide->getBeginLoc(), Context.getSourceManager());
  if (Loc.isInvalid())
    return;
  // What makes the guard: the helper, also the shared one,
  // `g_OnScopeExitShared`, or the function.
  StringRef Used = "g_OnScopeExit";
  if (Left) {
    const CXXRecordDecl *Helper = skipWrappers(Left)
                                      ->getType()
                                      .getNonReferenceType()
                                      ->getAsCXXRecordDecl();
    if (Helper && Helper->getIdentifier() &&
        Helper->getName().contains("Shared"))
      Used = "g_OnScopeExitShared";
  } else if (const auto *Call = dyn_cast<CallExpr>(Divide);
             Call && Call->getDirectCallee() &&
             Call->getDirectCallee()->getIdentifier())
    Used = Call->getDirectCallee()->getName();
  else
    Used = "fg_OnScopeExit";
  diag(Loc, "the teardown of an actor or functor in a coroutine uses "
            "'fg_AsyncDestroy' instead of '%0'")
      << Used;
}

} // namespace clang::tidy::malterlib
