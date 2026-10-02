//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ContinueRunningOnActorCheck.h"
#include "../utils/OptionsUtils.h"
#include "ActorAnnotations.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"
#include "OncePerTypeVisitor.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <deque>

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

// None: once the coroutine runs on another actor, nothing keeps its own
// actor alive, so even `self` and the handles it holds can be gone, and the
// handles can be changed by its actor meanwhile.
ContinueRunningOnActorCheck::ContinueRunningOnActorCheck(
    StringRef Name, ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      AllowedMemberTypes(utils::options::parseStringList(
          listOption("AllowedMemberTypes", ""))) {}

void ContinueRunningOnActorCheck::storeCheckOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "AllowedMemberTypes",
                utils::options::serializeStringList(AllowedMemberTypes));
}

void ContinueRunningOnActorCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      callExpr(callee(functionDecl(hasName("fg_ContinueRunningOnActor"))))
          .bind("switch"),
      this);
  // Awaiting a transfer made elsewhere, like one given as a parameter.
  Finder->addMatcher(coawaitExpr().bind("await"), this);
  // A pointer to it, which can switch where it is called.
  Finder->addMatcher(
      declRefExpr(to(functionDecl(hasName("fg_ContinueRunningOnActor"))),
                  unless(hasParent(implicitCastExpr(hasParent(callExpr())))))
          .bind("switch"),
      this);
}

/// Whether a value of \p Type points or refers to the object of an actor,
/// also through a reference to a pointer to it, `CApp *const &_pActor`.
static bool refersToActorObject(QualType Type) {
  const bool Reference = Type->isReferenceType();
  Type = Type.getNonReferenceType();
  if (Type->isPointerType())
    Type = Type->getPointeeType();
  else if (!Reference)
    return false;
  return isActorClass(Type->getAsCXXRecordDecl());
}

/// Whether \p Parameter points or refers to the object of an actor, which a
/// function uses like `this`.
static bool isActorParameter(const ParmVarDecl *Parameter) {
  return refersToActorObject(Parameter->getType());
}

void ContinueRunningOnActorCheck::check(
    const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  for (const auto &[Id, Node] : Result.Nodes.getMap())
    if (isInTemplatePattern(Node, *Result.Context))
      return;
  // Check the function or lambda that switches, once.
  const auto *Switch = Result.Nodes.getNodeAs<Expr>("switch");
  if (const auto *Await = Result.Nodes.getNodeAs<CoawaitExpr>("await")) {
    if (!awaitsTransferOwnership(Await))
      return;
    Switch = Await;
  }
  ASTContext &Context = *Result.Context;
  DynTypedNode Node = DynTypedNode::create(*Switch);
  for (;;) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return;
    Node = Parents[0];
    const FunctionDecl *Function = nullptr;
    const auto *Lambda = Node.get<LambdaExpr>();
    if (Lambda)
      Function = Lambda->getCallOperator();
    else
      Function = Node.get<FunctionDecl>();
    if (!Function)
      continue;
    // The call operator of a lambda, which can be reached by itself.
    if (!Lambda && isLambdaCallOperator(Function))
      Lambda = Instantiations.lambdaOf(Function, Context);
    if (qualifies(Function, Lambda) && Checked.insert(Function).second) {
      const InstantiationScope Scope(*this, Function);
      checkFunction(Function, Lambda, Context);
    }
    return;
  }
}

bool ContinueRunningOnActorCheck::qualifies(const FunctionDecl *Function,
                                            const LambdaExpr *Lambda) {
  // Only the object of an actor has an actor to race with, which the function
  // reaches through `this` or a parameter that points or refers to it, like
  // `CInternal *_pThis` of a static function.
  const CXXRecordDecl *Record = nullptr;
  if (Lambda)
    Record = enclosingThisClass(Lambda);
  else if (const auto *Method = dyn_cast<CXXMethodDecl>(Function))
    Record = Method->getParent();
  // A lambda can capture such a pointer or reference, or one into the object,
  // like `&_pActor->m_Value`, which the check follows to where the lambda is
  // made.
  const bool CapturesReference =
      Lambda &&
      llvm::any_of(Lambda->captures(), [](const LambdaCapture &Capture) {
        if (!Capture.capturesVariable())
          return false;
        // A name of a structured binding captured by reference refers into
        // what its decomposition refers to.
        if (isa<BindingDecl>(Capture.getCapturedVar()))
          return Capture.getCaptureKind() == LCK_ByRef;
        const QualType Type = Capture.getCapturedVar()->getType();
        return Type->isPointerType() || Type->isReferenceType();
      });
  return (Record && isActorClass(Record)) ||
         llvm::any_of(Function->parameters(), isActorParameter) ||
         CapturesReference;
}

/// The callbacks of the registration \p Holder that `co_await
/// fg_OnResume(Lambda)` gives, if it is one, which run when the coroutine
/// resumes: one, or either of those of a conditional, `bChoose ? co_await
/// fg_OnResume(A) : co_await fg_OnResume(B)`.
static llvm::SmallVector<const LambdaExpr *, 2>
onResumeLambdas(const VarDecl *Holder) {
  llvm::SmallVector<const LambdaExpr *, 2> Lambdas;
  if (!Holder || !Holder->getInit())
    return Lambdas;
  for (const CoroutineSuspendExpr *Await :
       holderAwaits(Holder->getInit(), &Holder->getASTContext())) {
    // Also an awaiter made before, `co_await fg_Move(Awaiter)`, and either
    // value awaited, `co_await (bChoose ? fg_OnResume(A) : fg_OnResume(B))`.
    if (!Await)
      continue;
    for (const CallExpr *Call : onResumeCalls(Await->getOperand()))
      if (const LambdaExpr *Lambda = Call && Call->getNumArgs() >= 1
                                         ? asLambda(Call->getArg(0))
                                         : nullptr)
        Lambdas.push_back(Lambda);
  }
  return Lambdas;
}

/// The registrations of `fg_OnResume` that are alive at \p S: declared before
/// it in the blocks it is in, or in the init statements and conditions of the
/// statements around it, like `if (auto OnResume = co_await fg_OnResume(...);
/// true)`.
static llvm::SmallVector<const VarDecl *, 2>
liveOnResumeHolders(const Stmt *S, ASTContext &Context) {
  llvm::SmallVector<const VarDecl *, 2> Holders;
  const SourceManager &SM = Context.getSourceManager();
  // Declared before it, which also tells the statement it is in apart from
  // those after it, whatever implicit nodes are around it.
  auto Add = [&](const Stmt *Statement) {
    if (!Statement ||
        !SM.isBeforeInTranslationUnit(Statement->getEndLoc(), S->getBeginLoc()))
      return false;
    if (const auto *Declaration = dyn_cast<DeclStmt>(Statement))
      for (const Decl *D : Declaration->decls())
        if (const auto *Holder = dyn_cast<VarDecl>(D);
            Holder &&
            (!onResumeLambdas(Holder).empty() ||
             ownPhaseGuardCall(Holder->getInit(), &Holder->getASTContext())))
          Holders.push_back(Holder);
    return true;
  };
  DynTypedNode Node = DynTypedNode::create(*S);
  for (int Depth = 0; Depth < 64; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty() || Parents[0].get<LambdaExpr>() ||
        Parents[0].get<FunctionDecl>())
      break;
    if (const auto *Block = Parents[0].get<CompoundStmt>()) {
      for (const Stmt *Statement : Block->body())
        if (!Add(Statement))
          break;
    } else if (const auto *If = Parents[0].get<IfStmt>()) {
      Add(If->getInit());
      Add(If->getConditionVariableDeclStmt());
    } else if (const auto *Switch = Parents[0].get<SwitchStmt>()) {
      Add(Switch->getInit());
      Add(Switch->getConditionVariableDeclStmt());
    } else if (const auto *For = Parents[0].get<ForStmt>()) {
      Add(For->getInit());
      Add(For->getConditionVariableDeclStmt());
    } else if (const auto *While = Parents[0].get<WhileStmt>()) {
      Add(While->getConditionVariableDeclStmt());
    } else if (const auto *Range = Parents[0].get<CXXForRangeStmt>()) {
      Add(Range->getInit());
    }
    Node = Parents[0];
  }
  return Holders;
}

namespace {

/// Collects the variables a statement declares and the declarations it refers
/// to, including in implicit code, which matchers that ignore it skip.
class DeclCollector : public OncePerTypeVisitor<DeclCollector> {
public:
  llvm::SmallVector<const VarDecl *, 8> Variables;
  llvm::SmallVector<const DeclRefExpr *, 8> References;

  bool VisitVarDecl(VarDecl *Variable) {
    Variables.push_back(Variable);
    return true;
  }
  bool VisitDeclRefExpr(DeclRefExpr *Ref) {
    References.push_back(Ref);
    return true;
  }
  bool VisitBinaryOperator(BinaryOperator *Binary) {
    if (Binary->getOpcode() == BO_Assign)
      Assignments.push_back(Binary);
    return true;
  }
  llvm::SmallVector<const BinaryOperator *, 8> Assignments;
};

/// Calls \p Visit with each reference to a declaration in \p S.
template <typename tf_FVisit>
void forEachDeclRef(const Stmt *S, tf_FVisit &&Visit) {
  class CVisitor : public OncePerTypeVisitor<CVisitor> {
  public:
    explicit CVisitor(tf_FVisit &Visit) : Visit(Visit) {}
    bool VisitDeclRefExpr(DeclRefExpr *Ref) {
      Visit(Ref);
      return true;
    }

  private:
    tf_FVisit &Visit;
  };
  CVisitor(Visit).TraverseStmt(const_cast<Stmt *>(S));
}

/// Calls \p Visit with each statement in \p S that runs with it: not those of
/// the bodies of the lambdas it makes, only what they capture, nor, with
/// \p Context, of the branches of an `if constexpr` that are discarded.
template <typename tf_FVisit>
void forEachStmt(const Stmt *S, tf_FVisit &&Visit,
                 const ASTContext *Context = nullptr) {
  class CVisitor : public OncePerTypeVisitor<CVisitor> {
  public:
    CVisitor(tf_FVisit &Visit, const ASTContext *Context)
        : Visit(Visit), Context(Context) {}
    bool VisitStmt(Stmt *Inner) {
      Visit(Inner);
      return true;
    }
    bool TraverseIfStmt(IfStmt *If) {
      // Of an `if consteval`, what runs when not constant evaluated.
      if (If->isConsteval()) {
        Visit(If);
        return this->TraverseStmt(If->isNegatedConsteval() ? If->getThen()
                                                           : If->getElse());
      }
      // Of a condition that is constant, `if (false)`, the branch it takes,
      // unless a jump to a label in the other can reach it.
      bool Taken = false;
      if (Context && !If->isConstexpr() && If->getCond() &&
          !If->getConditionVariable() && !If->getCond()->isValueDependent() &&
          If->getCond()->EvaluateAsBooleanCondition(Taken, *Context) &&
          !hasJumpTarget(Taken ? If->getElse() : If->getThen())) {
        Visit(If);
        Stmt *Case = Taken ? If->getThen() : If->getElse();
        return this->TraverseStmt(If->getInit()) &&
               this->TraverseStmt(If->getCond()) &&
               (!Case || this->TraverseStmt(Case));
      }
      if (Context && If->isConstexpr() && !If->getCond()->isValueDependent())
        if (const std::optional<Stmt *> Case =
                If->getNondiscardedCase(*Context)) {
          Visit(If);
          return this->TraverseStmt(If->getInit()) &&
                 this->TraverseStmt(If->getConditionVariableDeclStmt()) &&
                 this->TraverseStmt(If->getCond()) &&
                 this->TraverseStmt(*Case);
        }
      return RecursiveASTVisitor<CVisitor>::TraverseIfStmt(If);
    }
    bool TraverseLambdaExpr(LambdaExpr *Lambda) {
      Visit(Lambda);
      for (Expr *Init : Lambda->capture_inits()) {
        if (Init && !this->TraverseStmt(Init))
          return false;
      }
      return true;
    }

  private:
    tf_FVisit &Visit;
    const ASTContext *Context;
  };
  CVisitor(Visit, Context).TraverseStmt(const_cast<Stmt *>(S));
}

/// The local pointers \p Binary can assign, if it is an assignment of them:
/// one, also through a cast to a reference, `static_cast<int *&>(pValue) =`,
/// or a local reference to it, or either of a conditional, `(bFirst ? pFirst
/// : pSecond) =`, which assigns only one of them.
llvm::SmallVector<const VarDecl *, 2>
assignedPointers(const BinaryOperator *Binary) {
  llvm::SmallVector<const VarDecl *, 2> Pointers;
  if (Binary->getOpcode() != BO_Assign)
    return Pointers;
  // Each reference is bound to one declared before it, so a chain of them
  // ends; one bound to either of a conditional, `int *&pAlias = bChoose ?
  // pFirst : pSecond`, assigns either.
  // Each once, also one that many are bound to.
  llvm::SmallVector<const DeclRefExpr *, 4> Work(
      assignedVariables(Binary->getLHS()));
  llvm::SmallPtrSet<const VarDecl *, 8> Followed;
  while (!Work.empty()) {
    const DeclRefExpr *Ref = Work.pop_back_val();
    const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
    if (!Variable ||
        !(Variable->hasLocalStorage() || Variable->isStaticLocal()) ||
        !Followed.insert(Variable).second)
      continue;
    if (Variable->getType()->isPointerType()) {
      if (!llvm::is_contained(Pointers, Variable))
        Pointers.push_back(Variable);
      continue;
    }
    // Through a reference to it, `int *&pAlias = pValue; pAlias = ...`.
    if (!Variable->getType()->isReferenceType() ||
        !Variable->getType().getNonReferenceType()->isPointerType() ||
        !Variable->getInit())
      continue;
    for (const DeclRefExpr *Bound : assignedVariables(Variable->getInit()))
      Work.push_back(Bound);
  }
  return Pointers;
}

/// The switches of a coroutine body between actors, and the uses of the object
/// of its own actor.
class ActorFlow {
public:
  ActorFlow(ASTContext &Context, llvm::ArrayRef<StringRef> AllowedMemberTypes)
      : Context(Context), AllowedMemberTypes(AllowedMemberTypes) {}

  /// The actor \p S switches to: true for another actor, false for the own
  /// actor, and std::nullopt when it doesn't switch.
  std::optional<bool> switchesTo(const Stmt *S) const {
    const Expr *Operand = nullptr;
    if (const auto *Await = dyn_cast<CoroutineSuspendExpr>(S))
      Operand = Await->getOperand();
    if (!Operand)
      return std::nullopt;
    // Also a wrapper whose `operator co_await` gives the awaiter of a switch,
    // which may be to another actor.
    if (const std::optional<bool> Switch = switchOf(Operand, 0))
      return Switch;
    if (const auto *Await = dyn_cast<CoroutineSuspendExpr>(S);
        Await && !isTransferOwnership(Operand->getType()) &&
        awaitsTransferOwnership(Await))
      return true;
    return std::nullopt;
  }

  /// The actor that awaiting \p Operand switches to, like switchesTo, each
  /// expression told once, however many conditionals and references reach
  /// it, `auto &&Second = bChoose ? First : First`.
  std::optional<bool> switchOf(const Expr *Operand, int Depth) const {
    if (auto It = Switches.find(Operand); It != Switches.end())
      return It->second;
    // One that reaches itself, through a reference bound to itself, may
    // switch anywhere.
    Switches[Operand] = true;
    const std::optional<bool> Result = switchOfUncached(Operand, Depth);
    Switches[Operand] = Result;
    return Result;
  }

  /// Whether \p Local is assigned, like isAssigned, each told once.
  bool isAssignedCached(const VarDecl *Local) const {
    auto [It, Inserted] = AssignedLocals.try_emplace(Local, false);
    if (Inserted)
      It->second = isAssigned(Local);
    return It->second;
  }

  std::optional<bool> switchOfUncached(const Expr *Operand, int Depth) const {
    // An ordinary future, `TCFuture<int>`, doesn't switch, however it is
    // reached, `co_await fg_Move(rFuture)`.
    if (const StringRef Awaited = recordName(
            skipWrappers(Operand)->IgnoreParenImpCasts()->getType());
        Awaited == "TCFuture" || Awaited == "TCUnsafeFuture" ||
        Awaited == "TCFutureWithFlags" || Awaited == "TCFuturePack")
      return std::nullopt;
    // Past as many steps as the code can have, it may switch.
    if (Depth > 256)
      return true;
    const auto *Call = asCallTo(Operand, {"fg_ContinueRunningOnActor"});
    const Expr *Inner = skipWrappers(Operand)->IgnoreParenImpCasts();
    // The last of a comma, `(fg_Log(), fg_ContinueRunningOnActor(Own))`.
    if (const auto *Comma = dyn_cast<BinaryOperator>(Inner);
        Comma && Comma->getOpcode() == BO_Comma)
      return switchOf(Comma->getRHS(), Depth + 1);
    // Either of two switches, `Choose ? fg_ContinueRunningOnActor(A) :
    // fg_ContinueRunningOnActor(B)`, switches to another actor if either
    // does.
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Inner)) {
      // Of a constant condition, `true ? ... : ...`, the one it chooses.
      bool Condition = false;
      if (isa<ConditionalOperator>(Conditional) &&
          !Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(Condition,
                                                             Context))
        return switchOf(Condition ? Conditional->getTrueExpr()
                                  : Conditional->getFalseExpr(),
                        Depth + 1);
      const std::optional<bool> True =
          switchOf(Conditional->getTrueExpr(), Depth + 1);
      const std::optional<bool> False =
          switchOf(Conditional->getFalseExpr(), Depth + 1);
      // Back to the own actor only when both do.
      if (True.value_or(false) || False.value_or(false))
        return true;
      if (True && False)
        return false;
      return std::nullopt;
    }
    // Also the switch made before and kept in a local, like `auto Switch =
    // fg_ContinueRunningOnActor(Other); co_await fg_Move(Switch);`, also
    // through a reference to it.
    if (!Call) {
      // Moved or forwarded, `fg_Move(Switch)`, or cast to a reference to it,
      // `static_cast<decltype(Switch) &&>(Switch)`.
      for (int Level = 0; Level < 8; ++Level)
        if (const auto *Move = dyn_cast<CallExpr>(Inner);
            Move && Move->getNumArgs() == 1 &&
            isMoveOrForwardFunction(calleeName(Move)))
          Inner = skipWrappers(Move->getArg(0))->IgnoreParenImpCasts();
        else if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Inner);
                 Cast && Cast->isGLValue() &&
                 recordName(Cast->getType()) ==
                     recordName(Cast->getSubExpr()->getType()))
          Inner = skipWrappers(Cast->getSubExpr())->IgnoreParenImpCasts();
        else
          break;
      const auto *Ref = dyn_cast<DeclRefExpr>(Inner);
      const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      if (Local && Local->hasLocalStorage() && Local->getInit()) {
        // A switch assigned another one may switch anywhere, but not an
        // ordinary future, which resumes on the actor it is awaited on.
        if (isAssignedCached(Local))
          return isTransferOwnership(Local->getType().getNonReferenceType())
                     ? std::optional<bool>(true)
                     : std::nullopt;
        if (Local->getType()->isReferenceType())
          return switchOf(Local->getInit(), Depth + 1);
        // Either of a conditional it is made from, `bChoose ?
        // fg_ContinueRunningOnActor(Back) : ...`.
        if (isa<AbstractConditionalOperator>(
                skipWrappers(Local->getInit())->IgnoreParenImpCasts()))
          return switchOf(Local->getInit(), Depth + 1);
        // Also another one moved or forwarded into it, `auto Moved =
        // fg_Move(Switch)`, or copied, `auto Copy = Switch`, which keeps where
        // it switches to.
        if (const CallExpr *Move = asCallTo(
                Local->getInit(), vocabulary().MoveOrForwardFunctions.names());
            Move && Move->getNumArgs() == 1 &&
            isTransferOwnership(
                Move->getArg(0)->getType().getNonReferenceType()))
          return switchOf(Move->getArg(0), Depth + 1);
        if (const auto *Copied = dyn_cast<DeclRefExpr>(
                skipWrappers(Local->getInit())->IgnoreParenImpCasts());
            Copied && Copied->getDecl() != Local &&
            isTransferOwnership(Copied->getType().getNonReferenceType()))
          return switchOf(Copied, Depth + 1);
        Call = asCallTo(Local->getInit(), {"fg_ContinueRunningOnActor"});
      }
    }
    // A transfer that can't be told, like one through a pointer to
    // `fg_ContinueRunningOnActor`, may switch to another actor.
    if (!Call)
      return isTransferOwnership(Operand->getType())
                 ? std::optional<bool>(true)
                 : std::nullopt;
    if (Call->getNumArgs() != 1)
      return std::nullopt;
    return !(OwnActorIsObject && isOwnActor(Call->getArg(0), Context)) &&
           !(ParameterIsOwnActor && isParameterActor(Call->getArg(0)));
  }

  /// Whether \p Local is assigned in its function, like `Switch =
  /// fg_ContinueRunningOnActor(Other)`, or swapped.
  static bool isAssigned(const VarDecl *Local) {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
    if (!Function || !Function->getBody())
      return true;
    // Also through a local reference to it, `auto &Alias = Back`, or
    // either branch of a conditional, `(bChoose ? Back : Spare) = ...`.
    // Each expression once, which a chain of conditionals of the same
    // references, `int &r2 = b ? r1 : r1`, meets a number of times that
    // doubles with each; one met again while it is told tells nothing.
    llvm::DenseMap<const Expr *, bool> Referring;
    std::function<bool(const Expr *, int)> RefersToUncached;
    std::function<bool(const Expr *, int)> RefersTo = [&](const Expr *E,
                                                          int Depth) {
      if (!E)
        return false;
      if (auto It = Referring.find(E); It != Referring.end())
        return It->second;
      Referring[E] = false;
      const bool Result = RefersToUncached(E, Depth);
      Referring[E] = Result;
      return Result;
    };
    RefersToUncached = [&](const Expr *E, int Depth) {
      // Deeper than can be looked into, it can.
      if (Depth > 64)
        return true;
      E = skipWrappers(E)->IgnoreParenImpCasts();
      // Also cast to a reference, `static_cast<CTransfer &>(Back) = ...`.
      while (const auto *Cast = dyn_cast<ExplicitCastExpr>(E)) {
        if (!Cast->getTypeAsWritten()->isReferenceType())
          break;
        E = Cast->getSubExpr()->IgnoreParenImpCasts();
      }
      if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E))
        return RefersTo(Conditional->getTrueExpr(), Depth + 1) ||
               RefersTo(Conditional->getFalseExpr(), Depth + 1);
      if (const auto *Comma = dyn_cast<BinaryOperator>(E);
          Comma && Comma->getOpcode() == BO_Comma)
        return RefersTo(Comma->getRHS(), Depth + 1);
      const auto *Ref = dyn_cast<DeclRefExpr>(E);
      const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      if (Variable && Variable != Local &&
          Variable->getType()->isReferenceType() && Variable->getInit())
        return RefersTo(Variable->getInit(), Depth + 1);
      return Variable == Local;
    };
    bool Assigned = false;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || Assigned)
        return;
      // Not what doesn't run, in `sizeof` or a discarded branch.
      if (isa<UnaryExprOrTypeTraitExpr, CXXNoexceptExpr>(S))
        return;
      if (const auto *Typeid = dyn_cast<CXXTypeidExpr>(S);
          Typeid && !Typeid->isPotentiallyEvaluated())
        return;
      if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConstexpr()) {
        for (const Stmt *Child : evaluatedChildren(If))
          Visit(Child);
        return;
      }
      // Nor the branch a constant condition doesn't take, `if (false)`, nor
      // what follows a `return` or `co_return` in a block, unless a jump can
      // reach it.
      if (const auto *If = dyn_cast<IfStmt>(S);
          If && !If->isConsteval() && !If->getConditionVariable() &&
          If->getCond() && !If->getCond()->isValueDependent()) {
        bool Taken = false;
        if (If->getCond()->EvaluateAsBooleanCondition(Taken,
                                                      Local->getASTContext())) {
          const Stmt *Skipped = Taken ? If->getElse() : If->getThen();
          if (!Skipped || !hasJumpTarget(Skipped)) {
            Visit(If->getInit());
            Visit(If->getCond());
            Visit(Taken ? If->getThen() : If->getElse());
            return;
          }
        }
      }
      if (const auto *Block = dyn_cast<CompoundStmt>(S)) {
        bool Left = false;
        for (const Stmt *Statement : Block->body()) {
          if (Left && !hasJumpTarget(Statement))
            continue;
          Left = false;
          Visit(Statement);
          if (isa<ReturnStmt, CoreturnStmt>(Statement))
            Left = true;
        }
        return;
      }
      // Awaiting it, `co_await Back`, doesn't change where it goes: only what
      // is awaited is looked at, not the `await_transform` that gets it.
      if (const auto *Await = dyn_cast<CoroutineSuspendExpr>(S)) {
        Visit(Await->getOperand());
        return;
      }
      if (const auto *Binary = dyn_cast<BinaryOperator>(S);
          Binary && Binary->isAssignmentOp() && RefersTo(Binary->getLHS(), 0))
        Assigned = true;
      if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(S);
          Call && Call->isAssignmentOp() && Call->getNumArgs() >= 1 &&
          RefersTo(Call->getArg(0), 0))
        Assigned = true;
      // Casting const away from what refers or points to it, `*const_cast<T
      // *>(pAlias) = ...` of `auto const *pAlias = &Back`, can assign it.
      if (const auto *Cast = dyn_cast<CXXConstCastExpr>(S)) {
        std::function<bool(const Expr *, int)> LeadsTo = [&](const Expr *E,
                                                             int Depth) {
          if (!E || Depth > 8)
            return false;
          E = E->IgnoreParenImpCasts();
          if (const auto *Unary = dyn_cast<UnaryOperator>(E);
              Unary && (Unary->getOpcode() == UO_AddrOf ||
                        Unary->getOpcode() == UO_Deref))
            return LeadsTo(Unary->getSubExpr(), Depth + 1);
          if (RefersTo(E, 0))
            return true;
          const auto *Ref = dyn_cast<DeclRefExpr>(E);
          const auto *Alias = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          return Alias && Alias->hasLocalStorage() && Alias->getInit() &&
                 (Alias->getType()->isReferenceType() ||
                  Alias->getType()->isPointerType()) &&
                 LeadsTo(Alias->getInit(), Depth + 1);
        };
        if (LeadsTo(Cast->getSubExpr(), 0))
          Assigned = true;
      }
      // A pointer to it that isn't to const can assign it, `*pBack = ...`.
      if (const auto *Address = dyn_cast<UnaryOperator>(S);
          Address && Address->getOpcode() == UO_AddrOf &&
          !Address->getType()->getPointeeType().isConstQualified() &&
          RefersTo(Address->getSubExpr(), 0))
        Assigned = true;
      // Swapped or exchanged, or given to what can change it, like
      // `fg_Exchange(Switch, Other)`.
      if (const auto *Call = dyn_cast<CallExpr>(S);
          Call && !isa<CXXOperatorCallExpr>(Call)) {
        const StringRef Name = calleeName(Call);
        const bool Replaces = isSwapFunction(Name) || isExchangeFunction(Name);
        // Moving it, like into the await, `co_await fg_Move(Back)`, gives
        // where it goes away.
        const bool Moves = isMoveFunction(Name);
        for (unsigned iArgument = 0; iArgument < Call->getNumArgs();
             ++iArgument)
          if (RefersTo(Call->getArg(iArgument), 0) && !Moves &&
              (Replaces || mayAssignArgument(Call, iArgument)))
            Assigned = true;
      }
      for (const Stmt *Child : childrenOnce(S))
        Visit(Child);
    };
    Visit(Function->getBody());
    return Assigned;
  }

  /// Whether the function has no actor of its own, so it uses the objects of
  /// actors its parameters and captures point or refer to like `this`.
  bool TrackActorObjects = false;

  /// Whether the object the function uses is that of its only parameter that
  /// points or refers to an actor, since it has no `this`, so switching to
  /// the actor of that parameter switches back.
  bool ParameterIsOwnActor = false;

  /// The only parameter or capture that points or refers to the object of an
  /// actor, when ParameterIsOwnActor.
  const ValueDecl *OwnActorObject = nullptr;

  /// Whether \p Parameter can be given another value in its function, after
  /// which it may point to another actor. A reference can't, but a pointer it
  /// refers to can, `CApp *&_pThis`, unless it is const.
  static bool isReassigned(const ParmVarDecl *Parameter) {
    return !rebindsNothing(Parameter->getType()) &&
           isChangedVariable(Parameter);
  }

  /// Whether what a variable of \p Type refers to can't be changed to refer
  /// to another object: a reference, not one to a pointer that isn't const.
  static bool rebindsNothing(QualType Type) {
    return Type->isReferenceType() &&
           !(Type.getNonReferenceType()->isPointerType() &&
             !Type.getNonReferenceType().isConstQualified());
  }

  /// Whether \p E is the actor of the parameter or capture that points or
  /// refers to the object: `fg_ThisActor(_pThis)` or `_pThis->self`.
  bool isParameterActor(const Expr *E) const {
    // Also through an unchanged local handle, like `auto Own =
    // fg_ThisActor(_pThis)`.
    E = unchangedLocalValue(E);
    auto IsActorParameter = [&](const Expr *Object) {
      // Also through an unchanged local alias, like `auto *pActor = _pActor`.
      for (int Depth = 0; Depth < 4; ++Depth) {
        const Expr *Inner = Object->IgnoreParenImpCasts();
        // A cast to a base, `static_cast<CActor *>(_pActor)`.
        while (const auto *Cast = dyn_cast<ExplicitCastExpr>(Inner)) {
          if (!Cast->getType()->isPointerType() && !Cast->isGLValue())
            break;
          Inner = Cast->getSubExpr()->IgnoreParenImpCasts();
        }
        if (const auto *Unary = dyn_cast<UnaryOperator>(Inner);
            Unary && (Unary->getOpcode() == UO_AddrOf ||
                      Unary->getOpcode() == UO_Deref))
          Inner = Unary->getSubExpr()->IgnoreParenImpCasts();
        if (const auto *Ref = dyn_cast<DeclRefExpr>(Inner);
            Ref && Ref->getDecl() == OwnActorObject)
          return true;
        const Expr *Next = unchangedLocalValue(Inner);
        if (Next == Inner)
          return false;
        Object = Next;
      }
      return false;
    };
    E = skipWrappers(E);
    if (const auto *Call = asCallTo(E, {"fg_ThisActor"}))
      return Call->getNumArgs() == 1 && IsActorParameter(Call->getArg(0));
    // The conversion of `self`, a `CThisActor`, to a handle, also of a local
    // alias of it, like `auto Self = _pThis->self`.
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E);
        Call && isa_and_nonnull<CXXConversionDecl>(Call->getMethodDecl()))
      E = skipWrappers(
          unchangedLocalValue(skipWrappers(Call->getImplicitObjectArgument())));
    if (const auto *Member = dyn_cast<MemberExpr>(E))
      return Member->getMemberDecl()->getIdentifier() &&
             Member->getMemberDecl()->getName() == "self" &&
             IsActorParameter(Member->getBase());
    return false;
  }

  /// Whether \p E, with the local pointers and references in \p Bits, points
  /// or refers into the object: `this`, `&m_Value`, `pThis` or `&pThis->m_X`.
  bool pointsIntoObject(const Expr *E, const llvm::BitVector &Bits) const {
    // Each expression told once for the state of the pointers, however many
    // ways reach it, like the returns of lambdas that call each other.
    const bool Outermost = PointsDepth++ == 0;
    const llvm::scope_exit Leave([&] { --PointsDepth; });
    if (Outermost)
      PointsDecided.clear();
    if (auto It = PointsDecided.find(E); It != PointsDecided.end())
      return It->second;
    // While it is told, a way back to it tells nothing.
    PointsDecided[E] = false;
    const bool Into = decidePointsIntoObject(E, Bits);
    PointsDecided[E] = Into;
    return Into;
  }

private:
  mutable llvm::DenseMap<const Expr *, bool> PointsDecided;
  mutable llvm::DenseMap<const LambdaExpr *, llvm::BitVector> RelevantPointers;
  mutable int PointsDepth = 0;

  /// What a name of a structured binding of a reference, `auto &[Value] =
  /// m_Data`, is of: the reference its decomposition is, which refers where
  /// it does; else \p Decl.
  static const ValueDecl *decomposedReference(const ValueDecl *Decl) {
    if (const auto *Binding = dyn_cast_or_null<BindingDecl>(Decl))
      if (const auto *Decomposed =
              dyn_cast_or_null<VarDecl>(Binding->getDecomposedDecl());
          Decomposed && Decomposed->getType()->isReferenceType())
        return Decomposed;
    return Decl;
  }

  bool decidePointsIntoObject(const Expr *E,
                              const llvm::BitVector &Bits) const {
    // Also past the cleanups of an iterator with a destructor, `auto iEntry =
    // mp_Items.f_GetIterator()`.
    for (int Depth = 0; Depth < 8; ++Depth) {
      const Expr *Inner = E->IgnoreImplicit()->IgnoreParenImpCasts();
      if (Inner == E)
        break;
      E = Inner;
    }
    // The condition that is the first value of `&m_Value ?: &g_Value`.
    if (const auto *Opaque = dyn_cast<OpaqueValueExpr>(E);
        Opaque && Opaque->getSourceExpr())
      return pointsIntoObject(Opaque->getSourceExpr(), Bits);
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
      if (ObjectReferences.contains(Ref->getDecl()))
        return true;
      const ValueDecl *Decl = decomposedReference(Ref->getDecl());
      if (ObjectReferences.contains(Decl))
        return true;
      if (auto It = Pointers.find(Decl); It != Pointers.end())
        return Bits.test(It->second);
      // The iterator a range-based `for` makes, `__begin` of `for (int
      // &Value : mp_Values)`, is into what its range is.
      if (const auto *Implicit = dyn_cast<VarDecl>(Ref->getDecl());
          Implicit && Implicit->isImplicit() && Implicit->hasLocalStorage() &&
          Implicit->getInit())
        return pointsIntoObject(Implicit->getInit(), Bits);
      return false;
    }
    if (const auto *Unary = dyn_cast<UnaryOperator>(E)) {
      const auto *Of =
          dyn_cast<DeclRefExpr>(Unary->getSubExpr()->IgnoreParenImpCasts());
      // What a pointer to a local pointer points to is that pointer as it is
      // now, `*ppValue` of `int **ppValue = &pValue`.
      if (const auto *Local = Of ? dyn_cast<VarDecl>(Of->getDecl()) : nullptr;
          Unary->getOpcode() == UO_Deref && Local && Local->hasLocalStorage() &&
          !isa<ParmVarDecl>(Local) && Local->getInit() &&
          !isChangedVariable(Local))
        if (const auto *Address = dyn_cast<UnaryOperator>(
                Local->getInit()->IgnoreParenImpCasts());
            Address && Address->getOpcode() == UO_AddrOf)
          if (const auto *Target = dyn_cast<DeclRefExpr>(
                  Address->getSubExpr()->IgnoreParenImpCasts()))
            if (auto It = Pointers.find(Target->getDecl());
                It != Pointers.end())
              return Bits.test(It->second);
      // Also a pointer given a unary plus, `+&m_Value`, which is the same.
      if (Unary->getOpcode() == UO_AddrOf || Unary->getOpcode() == UO_Deref ||
          Unary->getOpcode() == UO_Plus)
        return pointsIntoObject(Unary->getSubExpr(), Bits);
    }
    // Not a static member, `&this->ms_Value`, which isn't in the object.
    if (const auto *Member = dyn_cast<MemberExpr>(E))
      return !isa<VarDecl>(Member->getMemberDecl()) &&
             pointsIntoObject(Member->getBase(), Bits);
    // A pointer or reference cast to another type, like to a base,
    // `static_cast<CData *>(_pActor)`.
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
        Cast && (Cast->getType()->isPointerType() || Cast->isGLValue()))
      return pointsIntoObject(Cast->getSubExpr(), Bits);
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E))
      return pointsIntoObject(Subscript->getBase(), Bits);
    if (const auto *Binary = dyn_cast<BinaryOperator>(E);
        Binary && Binary->isAdditiveOp())
      return pointsIntoObject(Binary->getLHS()->getType()->isPointerType()
                                  ? Binary->getLHS()
                                  : Binary->getRHS(),
                              Bits);
    // A member through a pointer to it, `this->*pMember`.
    if (const auto *Binary = dyn_cast<BinaryOperator>(E);
        Binary && Binary->isPtrMemOp())
      return pointsIntoObject(Binary->getLHS(), Bits);
    // The last of a comma expression, `(void(0), &m_Value)`, and what an
    // assignment of a pointer assigns, `q = (p = &m_Value)`; of anything else,
    // an assignment, also a compound one, or a prefix increment refers to what
    // it changes, `int &Value = (m_Value = 1)` or `++m_Value`.
    if (const auto *Binary = dyn_cast<BinaryOperator>(E);
        Binary && (Binary->getOpcode() == BO_Comma ||
                   (Binary->getOpcode() == BO_Assign &&
                    Binary->getType()->isPointerType())))
      return pointsIntoObject(Binary->getRHS(), Bits);
    if (const auto *Binary = dyn_cast<BinaryOperator>(E);
        Binary && Binary->isAssignmentOp())
      return pointsIntoObject(Binary->getLHS(), Bits);
    if (const auto *Step = dyn_cast<UnaryOperator>(E);
        Step && Step->isPrefix() && Step->isIncrementDecrementOp() &&
        !Step->getType()->isPointerType())
      return pointsIntoObject(Step->getSubExpr(), Bits);
    // A pointer stepped, `p++` or `++p`, gives one into the same data.
    if (const auto *Step = dyn_cast<UnaryOperator>(E);
        Step && Step->isIncrementDecrementOp() &&
        Step->getType()->isPointerType())
      return pointsIntoObject(Step->getSubExpr(), Bits);
    // Only the one a constant condition chooses, `false ? &m_Value : &g_Value`.
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E)) {
      bool Chosen = false;
      if (!Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(Chosen, Context))
        return pointsIntoObject(Chosen ? Conditional->getTrueExpr()
                                       : Conditional->getFalseExpr(),
                                Bits);
      return pointsIntoObject(Conditional->getTrueExpr(), Bits) ||
             pointsIntoObject(Conditional->getFalseExpr(), Bits);
    }
    // What a free function whose body can be seen gives back as it is given
    // it, `fg_Identity(&m_Value)`, or the address of what it is given by
    // reference, `fg_Address(m_Value)` of `return &_Value;`.
    if (const auto *Free = dyn_cast<CallExpr>(E);
        Free && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Free)) {
      for (const Expr *Argument : returnedArguments(Free))
        if (pointsIntoObject(Argument, Bits))
          return true;
      for (const Expr *Argument : returnedArguments(Free, /*Addressed=*/true))
        if (pointsIntoObject(Argument, Bits))
          return true;
      // Also a lookup into what it is given by reference, `fg_Find(mp_Items)`
      // of `return _Items.f_FindEqual(1);`.
      if (const FunctionDecl *Callee = Free->getDirectCallee())
        if (const FunctionDecl *Definition = Callee->getDefinition();
            Definition && Definition->getBody() &&
            !isa<CoroutineBodyStmt>(Definition->getBody()))
          for (const Expr *Returned : returnedValues(Definition)) {
            const auto *Lookup =
                dyn_cast<CXXMemberCallExpr>(Returned->IgnoreParenImpCasts());
            const Expr *Object =
                Lookup ? Lookup->getImplicitObjectArgument() : nullptr;
            const auto *Given =
                Object ? dyn_cast<DeclRefExpr>(Object->IgnoreParenImpCasts())
                       : nullptr;
            const auto *Parameter =
                Given ? dyn_cast<ParmVarDecl>(Given->getDecl()) : nullptr;
            if (Parameter && Parameter->getType()->isReferenceType() &&
                (Lookup->getType()->isPointerType() || Lookup->isGLValue()) &&
                Parameter->getFunctionScopeIndex() < Free->getNumArgs() &&
                pointsIntoObject(
                    Free->getArg(Parameter->getFunctionScopeIndex()), Bits))
              return true;
          }
    }
    // What an exchange returns, the value the first had before,
    // `fg_Exchange(pValue, nullptr)`.
    if (const auto *Call = dyn_cast<CallExpr>(E);
        Call && Call->getNumArgs() == 2 &&
        (isExchangeFunction(calleeName(Call)))) {
      if (auto It = ExchangedPointers.find(Call);
          It != ExchangedPointers.end())
        return It->second;
      return pointsIntoObject(Call->getArg(0), Bits);
    }
    // The address of it, `std::addressof(m_Value)`.
    if (const auto *Call = dyn_cast<CallExpr>(E); isAddressOfCall(Call))
      return pointsIntoObject(Call->getArg(0), Bits);
    // What a member function returns a reference or pointer to, like
    // `_pActor->f_Get()`, is in the object, unless it can be seen to give
    // back only what has static storage, `return "Downloads";`, and no
    // override can give something else.
    // Also an iterator into it, like `begin()` gives.
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E);
        Call && (Call->isGLValue() || Call->getType()->isPointerType() ||
                 isIteratorType(Call->getType()))) {
      if (returnsOnlyStatic(Call->getMethodDecl()) && !callsOverride(Call))
        return false;
      return pointsIntoObject(Call->getImplicitObjectArgument(), Bits);
    }
    if (const auto *Free = dyn_cast<CallExpr>(E);
        Free && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Free) &&
        Free->getNumArgs() == 1 && isIteratorType(Free->getType()) &&
        (calleeName(Free) == "begin" || calleeName(Free) == "end"))
      return pointsIntoObject(Free->getArg(0), Bits);
    // What a lambda called in place returns, `[this] { return &m_Value; }()`,
    // of any of its returns, also of what it is given, `[](int &_Value) {
    // return &_Value; }(m_Value)`.
    if (const CXXMethodDecl *Operator = calledInPlaceLambda(E)) {
      const auto *Call = cast<CallExpr>(E);
      for (const bool Addressed : {false, true})
        for (const Expr *Argument : returnedArguments(Call, Addressed))
          if (pointsIntoObject(Argument, Bits))
            return true;
      return llvm::any_of(returnedValues(Operator), [&](const Expr *Value) {
        return pointsIntoObject(Value, Bits);
      });
    }
    if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
        Call && Call->getNumArgs() >= 1 &&
        (Call->isGLValue() || Call->getType()->isPointerType()) &&
        (Call->getOperator() == OO_Arrow || Call->getOperator() == OO_Star ||
         Call->getOperator() == OO_Subscript ||
         Call->getOperator() == OO_Call))
      return pointsIntoObject(Call->getArg(0), Bits);
    return ThisIsObject && isRootedInThis(E);
  }

public:
  /// Whether \p Invoked, a lambda that runs where it is made, on the actor
  /// the coroutine is on, uses the object; only before \p Before when it is
  /// valid, where the lambda switches to another actor by itself.
  bool invokedUses(const LambdaExpr *Invoked, const llvm::BitVector &Bits,
                   SourceLocation Before = {}) const {
    // Each lambda is looked into once while the outermost question is
    // answered, also through lambdas that call it many times, and one that
    // calls itself adds nothing there.
    // Also by what the pointers point into, which the lambdas that call it
    // can change first.
    using CKey =
        std::pair<const LambdaExpr *, std::pair<unsigned, unsigned>>;
    struct CCache {
      llvm::DenseMap<CKey, bool> Results;
      llvm::DenseSet<CKey> InProgress;
    };
    static thread_local CCache *s_pCache = nullptr;
    std::optional<CCache> Owned;
    if (!s_pCache)
      s_pCache = &Owned.emplace();
    const llvm::scope_exit Release([&] {
      if (Owned)
        s_pCache = nullptr;
    });
    // Only of the pointers it can read, so that calls that differ in others,
    // `f1(); p2 = nullptr; f1();`, are the same, which would otherwise be
    // looked into for each combination of them.
    llvm::BitVector Relevant = Bits;
    Relevant &= relevantPointers(Invoked);
    const unsigned BitsHash = unsigned(llvm::hash_combine_range(
        Relevant.set_bits_begin(), Relevant.set_bits_end()));
    const CKey Key{Invoked, {Before.getRawEncoding(), BitsHash}};
    if (auto It = s_pCache->Results.find(Key); It != s_pCache->Results.end())
      return It->second;
    if (!s_pCache->InProgress.insert(Key).second)
      return false;
    const bool Uses = invokedUsesUncached(Invoked, Bits, Before);
    s_pCache->InProgress.erase(Key);
    s_pCache->Results.try_emplace(Key, Uses);
    return Uses;
  }

  /// The pointers that \p Invoked can read: those named in it, and in what
  /// the locals it names are made of, like the lambdas it calls, `fRead()`
  /// of `auto fRead = [&] { ... }`, however deep.
  const llvm::BitVector &relevantPointers(const LambdaExpr *Invoked) const {
    auto [It, Inserted] = RelevantPointers.try_emplace(Invoked);
    if (!Inserted)
      return It->second;
    llvm::BitVector Relevant(bitCount());
    // And what the copies that lambdas captured point into, all of them.
    Relevant.set(nPointerBits, bitCount());
    llvm::SmallPtrSet<const VarDecl *, 16> Followed;
    // Each statement once, also the operand of an await, which the
    // expressions that make its awaiter have again.
    llvm::SmallPtrSet<const Stmt *, 32> Visited;
    llvm::SmallVector<const Stmt *, 16> Work{Invoked};
    while (!Work.empty()) {
      const Stmt *S = Work.pop_back_val();
      if (!S || !Visited.insert(S).second)
        continue;
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S)) {
        if (auto Pointer = Pointers.find(Ref->getDecl());
            Pointer != Pointers.end())
          Relevant.set(Pointer->second);
        if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
            Local && Local->getInit() && Followed.insert(Local).second)
          Work.push_back(Local->getInit());
      }
      for (const Stmt *Child : childrenOnce(S))
        Work.push_back(Child);
    }
    return RelevantPointers[Invoked] = std::move(Relevant);
  }

  bool invokedUsesUncached(const LambdaExpr *Invoked,
                           const llvm::BitVector &Bits,
                           SourceLocation Before) const {
    const SourceManager &SM = Context.getSourceManager();
    auto IsBefore = [&](const Stmt *Inner) {
      return Before.isInvalid() ||
             SM.isBeforeInTranslationUnit(Inner->getBeginLoc(), Before);
    };
    // The lambdas it calls in turn before it switches, like `fRead()` in
    // `[&fRead] { return fRead(); }`, run here too, with what it assigns to
    // the pointers it captures by reference before, `p = &Local; return
    // fRead();`: for sure in a statement of its body before anything that
    // can leave, or else maybe.
    bool NestedUses = false;
    llvm::BitVector Current = Bits;
    const llvm::SmallPtrSet<const Stmt *, 8> Always =
        statementsBeforeLeaving(Invoked->getBody());
    forEachStmt(Invoked->getBody(), [&](const Stmt *Inner) {
        if (NestedUses || !IsBefore(Inner))
          return;
        if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
            Assign && Assign->getOpcode() == BO_Assign) {
          const llvm::SmallVector<const DeclRefExpr *, 2> Targets =
              assignedVariables(Assign->getLHS());
          for (const DeclRefExpr *Ref : Targets) {
            const bool ByReference = llvm::any_of(
                Invoked->captures(), [&](const LambdaCapture &Capture) {
                  return Capture.capturesVariable() &&
                         Capture.getCapturedVar() == Ref->getDecl() &&
                         Capture.getCaptureKind() == LCK_ByRef;
                });
            auto It = Pointers.find(Ref->getDecl());
            if (!ByReference || It == Pointers.end())
              continue;
            const bool Into = pointsIntoObject(Assign->getRHS(), Current);
            Current[It->second] =
                Into || ((Targets.size() != 1 || !Always.contains(Assign)) &&
                         Current[It->second]);
          }
          return;
        }
        const LambdaExpr *Nested = nullptr;
        if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(Inner);
            Call && Call->getOperator() == OO_Call && Call->getNumArgs() >= 1)
          Nested = asLambda(Call->getArg(0));
        else if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Inner);
                 Member && Member->getMethodDecl() &&
                 Member->getMethodDecl()->getOverloadedOperator() == OO_Call)
          Nested = asLambda(Member->getImplicitObjectArgument());
        else if (const auto *Call = dyn_cast<CallExpr>(Inner);
                 Call && Call->getNumArgs() >= 1 &&
                 (asCallTo(Call, {"fg_CallSafe"}) || isStdInvoke(Call)))
          Nested = asLambda(Call->getArg(0));
        if (Nested && Nested != Invoked)
          NestedUses = invokedUses(Nested, Current, firstSwitch(Nested));
      }, &Context);
    if (NestedUses)
      return true;
    // Its body is checked like the code around it, so static members stay
    // allowed, also through the locals it points into the
    // object, like `auto *pThis = this`.
    if (ThisIsObject &&
        llvm::any_of(Invoked->captures(),
                     [](const LambdaCapture &Capture) {
                       return Capture.capturesThis() &&
                              Capture.getCaptureKind() != LCK_StarThis;
                     })) {
      llvm::SmallPtrSet<const ValueDecl *, 4> LocalAliases;
      bool Uses = false;
      forEachStmt(Invoked->getBody(), [&](const Stmt *Inner) {
        if (!IsBefore(Inner))
          return;
        if (const auto *Declaration = dyn_cast<DeclStmt>(Inner)) {
          for (const Decl *D : Declaration->decls()) {
            const auto *Variable = dyn_cast<VarDecl>(D);
            if (Variable && Variable->getInit() &&
                (Variable->getType()->isPointerType() ||
                 Variable->getType()->isReferenceType()) &&
                pointsIntoObject(Variable->getInit(), Bits))
              LocalAliases.insert(Variable);
          }
        }
        if (const auto *Member = dyn_cast<MemberExpr>(Inner)) {
          if (const auto *Ref = dyn_cast<DeclRefExpr>(
                  Member->getBase()->IgnoreParenImpCasts());
              Ref && LocalAliases.contains(Ref->getDecl()) &&
              !isAllowedMember(Member))
            Uses = true;
        }
        Uses = Uses || usesObject(Inner, Bits);
      }, &Context);
      if (Uses)
        return true;
    }
    // A capture that points into the object, like `[pThis = this]` or a
    // local pointer, used in the body.
    llvm::SmallPtrSet<const ValueDecl *, 4> IntoObject;
    auto Init = Invoked->capture_init_begin();
    for (const LambdaCapture &Capture : Invoked->captures()) {
      const Expr *CaptureInit = *Init++;
      if (!Capture.capturesVariable())
        continue;
      const ValueDecl *Captured = Capture.getCapturedVar();
      const auto *Variable = dyn_cast<VarDecl>(Captured);
      // A copy of what a reference refers to, `[Value]` of `int &Value`, or
      // `[Value = Value]`, is a value of its own.
      if (Capture.getCaptureKind() == LCK_ByCopy &&
          !(Variable && Variable->isInitCapture()
                ? Variable->getType()->isPointerType() ||
                      Variable->getType()->isReferenceType()
                : Captured->getType().getNonReferenceType()->isPointerType()))
        continue;
      const std::optional<unsigned> Copy = lambdaCopy(Invoked, Captured);
      // Its copy, or, for a lambda that runs where it is made, before its
      // copy is made, what it is made from.
      const bool Into =
          (Copy && Bits.test(*Copy)) ||
          (Variable && Variable->isInitCapture()
               ? Variable->getInit() &&
                     pointsIntoObject(Variable->getInit(), Bits)
               : CaptureInit && pointsIntoObject(CaptureInit, Bits));
      if (Into)
        IntoObject.insert(Captured);
    }
    if (IntoObject.empty())
      return false;
    // Its uses, but not accesses of members of the allowed types, nor
    // captures by other lambdas.
    auto UsesCapture = [&](const DeclRefExpr *Ref) {
      const Stmt *User = userOf(Ref);
      if (const auto *Member = dyn_cast_or_null<MemberExpr>(User);
          Member && Member->getBase()->IgnoreParenImpCasts() == Ref)
        return !isAllowedMember(Member);
      // Assigning it, `pValue = nullptr`, changes the variable, not the
      // object, but not a reference, `Value = 1` of `int &Value`, which
      // writes what it refers to.
      if (const auto *Assign = dyn_cast_or_null<BinaryOperator>(User);
          Assign && Assign->getOpcode() == BO_Assign &&
          Assign->getLHS()->IgnoreParens() == Ref &&
          !Ref->getDecl()->getType()->isReferenceType())
        return false;
      // Nor comparing or testing a pointer, `pValue == nullptr`, which reads
      // only its value.
      if (Ref->getDecl()->getType()->isPointerType()) {
        DynTypedNode Node = DynTypedNode::create(*Ref);
        for (int Depth = 0; Depth < 8; ++Depth) {
          const auto Parents = instantiatedParents(Node, Context);
          if (Parents.empty())
            break;
          const DynTypedNode &Parent = Parents[0];
          if (const auto *Cast = Parent.get<ImplicitCastExpr>()) {
            if (Cast->getCastKind() == CK_PointerToBoolean)
              return false;
            Node = Parent;
            continue;
          }
          if (Parent.get<ParenExpr>()) {
            Node = Parent;
            continue;
          }
          if (const auto *Binary = Parent.get<BinaryOperator>();
              Binary && (Binary->isComparisonOp() || Binary->isLogicalOp()))
            return false;
          if (const auto *Unary = Parent.get<UnaryOperator>();
              Unary && Unary->getOpcode() == UO_LNot)
            return false;
          break;
        }
      }
      return !isa_and_nonnull<LambdaExpr>(User);
    };
    // Not one the lambda first points elsewhere, `[pValue] { pValue =
    // &Local; return *pValue; }`.
    auto PointedElsewhere = [&](const DeclRefExpr *Ref) {
      return !Ref->getDecl()->getType()->isReferenceType() &&
             reassignedBefore(Invoked->getBody(), Ref->getDecl(), Ref,
                              Context.getSourceManager(),
                              [&](const Expr *Value) {
                                return !pointsIntoObject(Value, Bits);
                              });
    };
    // Not one that never runs, in a branch that is discarded or never taken,
    // or in a lambda in it that isn't called.
    auto Runs = [&](const DeclRefExpr *Ref) {
      if (isNeverRun(Ref, Context))
        return false;
      DynTypedNode Node = DynTypedNode::create(*Ref);
      for (int Depth = 0; Depth < 256; ++Depth) {
        const auto Parents = instantiatedParents(Node, Context);
        if (Parents.empty())
          return true;
        if (const auto *Nested = Parents[0].get<LambdaExpr>()) {
          if (Nested == Invoked)
            return true;
          if (Node.get<Stmt>() == Nested->getBody()) {
            const auto Consumers = closureConsumers(Nested, Context, {});
            if (Consumers.empty() || !isCalledInScope(Consumers))
              return false;
          }
        }
        if (const auto *Operator = Parents[0].get<FunctionDecl>()) {
          if (Operator == Invoked->getCallOperator())
            return true;
          if (const LambdaExpr *Nested =
                  isLambdaCallOperator(Operator)
                      ? lambdaOfCallOperator(cast<CXXMethodDecl>(Operator),
                                             Context)
                      : nullptr;
              Nested && Nested != Invoked) {
            const auto Consumers = closureConsumers(Nested, Context, {});
            if (Consumers.empty() || !isCalledInScope(Consumers))
              return false;
            Node = DynTypedNode::create(*Nested);
            continue;
          }
          return true;
        }
        Node = Parents[0];
      }
      return true;
    };
    bool Uses = false;
    forEachDeclRef(Invoked->getBody(), [&](const DeclRefExpr *Ref) {
      Uses = Uses || (IsBefore(Ref) && IntoObject.contains(Ref->getDecl()) &&
                      UsesCapture(Ref) && !PointedElsewhere(Ref) && Runs(Ref));
    });
    return Uses;
  }

  /// Where \p Lambda first switches to another actor by itself on every
  /// path, or an invalid location. A switch in a branch or a loop, or in a
  /// lambda with a jump, may not run.
  SourceLocation firstSwitch(const LambdaExpr *Lambda) const {
    SourceLocation First;
    const SourceManager &SM = Context.getSourceManager();
    // A jump can pass any switch.
    bool Jumps = false;
    forEachStmt(Lambda->getBody(), [&](const Stmt *Inner) {
      Jumps = Jumps || isa<GotoStmt, IndirectGotoStmt, LabelStmt>(Inner);
    });
    if (Jumps)
      return First;
    auto Unconditional = [&](const Expr *Switch) {
      DynTypedNode Node = DynTypedNode::create(*Switch);
      for (int Depth = 0; Depth < 256; ++Depth) {
        const auto Parents = instantiatedParents(Node, Context);
        if (Parents.empty())
          return false;
        Node = Parents[0];
        if (Node.get<Stmt>() == Lambda->getBody())
          return true;
        if (Node.get<LambdaExpr>() || Node.get<IfStmt>() ||
            Node.get<ForStmt>() || Node.get<WhileStmt>() ||
            Node.get<DoStmt>() || Node.get<CXXForRangeStmt>() ||
            Node.get<SwitchStmt>() || Node.get<AbstractConditionalOperator>() ||
            Node.get<CXXTryStmt>() || Node.get<CXXCatchStmt>() ||
            Node.get<LabelStmt>())
          return false;
        if (const auto *Binary = Node.get<BinaryOperator>();
            Binary && Binary->isLogicalOp())
          return false;
      }
      return false;
    };
    // Only awaiting it switches: making the awaitable doesn't.
    auto Awaited = [&](const Expr *Switch) {
      DynTypedNode Node = DynTypedNode::create(*Switch);
      for (int Depth = 0; Depth < 8; ++Depth) {
        const auto Parents = instantiatedParents(Node, Context);
        if (Parents.empty())
          return false;
        if (const auto *Await = Parents[0].get<CoawaitExpr>())
          return Await->getOperand()->IgnoreImplicit() ==
                 Node.get<Expr>()->IgnoreImplicit();
        if (!Parents[0].get<ImplicitCastExpr>() &&
            !Parents[0].get<ParenExpr>() &&
            !Parents[0].get<MaterializeTemporaryExpr>() &&
            !Parents[0].get<CXXBindTemporaryExpr>())
          return false;
        Node = Parents[0];
      }
      return false;
    };
    // Also one kept in an unchanged local and awaited later, `auto Switch =
    // fg_ContinueRunningOnActor(...); co_await fg_Move(Switch);`, which
    // switches where it is awaited.
    auto StoredSwitch = [&](const CoawaitExpr *Await) {
      const Expr *Operand = Await->getOperand()->IgnoreImplicit();
      if (const auto *Move = dyn_cast<CallExpr>(Operand);
          Move && Move->getNumArgs() == 1 && (isMoveFunction(calleeName(Move))))
        Operand = Move->getArg(0)->IgnoreImplicit();
      const auto *Ref = dyn_cast<DeclRefExpr>(Operand->IgnoreParenImpCasts());
      const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      // Also through a local reference to it, `auto &Alias = Switch;`.
      llvm::SmallVector<const VarDecl *, 2> Locals;
      for (int Depth = 0; Local && Depth < 4; ++Depth) {
        if (!Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
            !Local->getInit())
          return false;
        Locals.push_back(Local);
        if (!Local->getType()->isLValueReferenceType())
          break;
        const auto *Bound =
            dyn_cast<DeclRefExpr>(Local->getInit()->IgnoreParenImpCasts());
        Local = Bound ? dyn_cast<VarDecl>(Bound->getDecl()) : nullptr;
      }
      if (!Local || Locals.empty() || Locals.back() != Local ||
          Local->getType()->isReferenceType() ||
          !asCallTo(Local->getInit()->IgnoreImplicit(),
                    {"fg_ContinueRunningOnActor"}))
        return false;
      // Each used only there.
      llvm::DenseMap<const VarDecl *, unsigned> nUses;
      forEachStmt(Lambda->getBody(), [&](const Stmt *Inner) {
        if (const auto *Use = dyn_cast<DeclRefExpr>(Inner))
          if (const auto *Used = dyn_cast<VarDecl>(Use->getDecl());
              Used && llvm::is_contained(Locals, Used))
            ++nUses[Used];
      });
      return llvm::all_of(
          Locals, [&](const VarDecl *Each) { return nUses.lookup(Each) == 1; });
    };
    // Its arguments run before it switches, so it switches at its end.
    SourceLocation FirstEnd;
    forEachStmt(Lambda->getBody(), [&](const Stmt *Inner) {
      const auto *Expression = dyn_cast<Expr>(Inner);
      if (!Expression ||
          !(First.isInvalid() ||
            SM.isBeforeInTranslationUnit(Expression->getBeginLoc(), First)) ||
          !Unconditional(Expression))
        return;
      const auto *Await = dyn_cast<CoawaitExpr>(Expression);
      if ((asCallTo(Expression, {"fg_ContinueRunningOnActor"}) &&
           Awaited(Expression)) ||
          (Await && StoredSwitch(Await))) {
        First = Expression->getBeginLoc();
        FirstEnd = Expression->getEndLoc();
      }
    });
    return FirstEnd;
  }

  ASTContext &context() const { return Context; }

  /// Whether \p Lambda is the callback given to `fg_OnResume`.
  bool isOnResumeCallback(const LambdaExpr *Lambda) const {
    DynTypedNode Node = DynTypedNode::create(*Lambda);
    for (int Depth = 0; Depth < 8; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return false;
      if (const auto *Call = Parents[0].get<CallExpr>())
        return asCallTo(Call, {"fg_OnResume"}) && Call->getNumArgs() >= 1 &&
               asLambda(Call->getArg(0)) == Lambda;
      if (!Parents[0].get<ImplicitCastExpr>() &&
          !Parents[0].get<MaterializeTemporaryExpr>() &&
          !Parents[0].get<CXXBindTemporaryExpr>() &&
          !Parents[0].get<CXXConstructExpr>() &&
          !Parents[0].get<CXXFunctionalCastExpr>() &&
          !Parents[0].get<ParenExpr>())
        return false;
      Node = Parents[0];
    }
    return false;
  }

  /// Whether the scope exit that destroying \p Local runs uses the object,
  /// or the callback \p S of the registration \p Local, or that of a phase
  /// guard of the actor, `co_await f_CheckDestroyedOnResume()`, which reads
  /// the object.
  bool scopeExitUses(const VarDecl *Local, const Stmt *S,
                     const llvm::BitVector &Bits) const {
    // The lambda given, of a scope exit, one of the cleanups of a guard, or a
    // callback of `fg_OnResume`.
    const LambdaExpr *Cleanup = scopeExitLambda(Local);
    if (!Cleanup)
      Cleanup = dyn_cast<LambdaExpr>(S);
    if (Cleanup)
      return invokedUses(Cleanup, Bits, firstSwitch(Cleanup));
    return Local && ThisIsObject &&
           ownPhaseGuardCall(Local->getInit(), &Local->getASTContext());
  }

  /// Whether \p Lambda is dispatched to the current actor: `g_Dispatch /
  /// Lambda`, `fg_Dispatch(Lambda)`, or a timer callback without an actor,
  /// which runs on the current one.
  bool isDispatchedToCurrentActor(const LambdaExpr *Lambda) const {
    const Expr *Actor = nullptr;
    if (closureDispatch(Lambda, Lambda->getLambdaClass(), Context, &Actor))
      return !Actor;
    DynTypedNode Node = DynTypedNode::create(*Lambda);
    for (int Depth = 0; Depth < 8; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return false;
      if (const auto *Timer = Parents[0].get<CallExpr>()) {
        const StringRef Name = calleeName(Timer);
        // Also given an empty actor, `TCActor<>{}`, also in an unchanged
        // local, which the timer replaces with the current one too.
        return (Name == "fg_RegisterTimer" || Name == "fg_RegisterExactTimer" ||
                Name == "fg_OneshotTimer" ||
                Name == "fg_OneshotTimerAbortable") &&
               Timer->getNumArgs() >= 2 &&
               asLambda(Timer->getArg(1)) == Lambda &&
               (Timer->getNumArgs() < 3 || isEmptyActor(Timer->getArg(2)));
      }
      const auto *Parent = Parents[0].get<Expr>();
      if (!Parent ||
          !isa<ParenExpr, ImplicitCastExpr, MaterializeTemporaryExpr,
               CXXBindTemporaryExpr, CXXConstructExpr, CXXFunctionalCastExpr>(
              Parent))
        return false;
      Node = Parents[0];
    }
    return false;
  }

  /// Whether \p Lambda initializes a local, like `auto fRun = [...]`.
  bool isHeldInLocal(const LambdaExpr *Lambda) const {
    DynTypedNode Node = DynTypedNode::create(*Lambda);
    for (int Depth = 0; Depth < 8; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return false;
      if (const auto *Variable = Parents[0].get<VarDecl>())
        return Variable->hasLocalStorage();
      const auto *Parent = Parents[0].get<Expr>();
      if (!Parent ||
          !isa<ParenExpr, ImplicitCastExpr, MaterializeTemporaryExpr,
               CXXBindTemporaryExpr, ExprWithCleanups, CXXConstructExpr>(Parent))
        return false;
      Node = Parents[0];
    }
    return false;
  }

  /// Whether \p Ref is the initializer of a capture by reference.
  bool isCapturedByReference(const DeclRefExpr *Ref) const {
    const auto Parents = instantiatedParents(*Ref, Context);
    return !Parents.empty() && Parents[0].get<LambdaExpr>();
  }

  /// Whether the pointer \p Ref is only read as a value: compared, tested,
  /// or copied into a local pointer.
  bool isPointerValueRead(const DeclRefExpr *Ref) const {
    DynTypedNode Node = DynTypedNode::create(*Ref);
    for (int Depth = 0; Depth < 8; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return false;
      const DynTypedNode &Parent = Parents[0];
      if (const auto *Cast = Parent.get<ImplicitCastExpr>()) {
        if (Cast->getCastKind() == CK_PointerToBoolean)
          return true;
        Node = Parent;
        continue;
      }
      if (Parent.get<ParenExpr>()) {
        Node = Parent;
        continue;
      }
      if (const auto *Binary = Parent.get<BinaryOperator>()) {
        // Assigned to a pointer, also of which the value is then used, like
        // `*(pValue = pSaved)`, which is read where that is.
        if (!assignedPointers(Binary).empty() &&
            Binary->getRHS()->IgnoreParenImpCasts() == Ref) {
          const auto Above = instantiatedParents(Parent, Context);
          const auto *User = Above.empty() ? nullptr : Above[0].get<Expr>();
          if (User && !isa<ExprWithCleanups>(User)) {
            Node = Parent;
            continue;
          }
          return true;
        }
        return Binary->isComparisonOp() || Binary->isLogicalOp();
      }
      if (const auto *Unary = Parent.get<UnaryOperator>())
        return Unary->getOpcode() == UO_LNot;
      if (const auto *Variable = Parent.get<VarDecl>())
        return (Variable->hasLocalStorage() || Variable->isStaticLocal()) &&
               Variable->getType()->isPointerType();
      return false;
    }
    return false;
  }

  /// Whether \p S uses the object of the own actor: accesses a member of
  /// `this`, dereferences it, or uses a local that refers into the object.
  bool usesObject(const Stmt *S, const llvm::BitVector &Bits) const {
    auto RefersToObject = [&](const ValueDecl *Decl) {
      // A captured name of a binding is recorded itself.
      if (ObjectReferences.contains(Decl))
        return true;
      Decl = decomposedReference(Decl);
      if (ObjectReferences.contains(Decl))
        return true;
      auto It = Pointers.find(Decl);
      return It != Pointers.end() && Bits.test(It->second);
    };
    // Past pointer and reference casts, like to a base,
    // `static_cast<CBase *>(this)->ms_Value`.
    auto StripCasts = [](const Expr *E) {
      E = E->IgnoreParenImpCasts();
      while (const auto *Cast = dyn_cast<ExplicitCastExpr>(E)) {
        if (!Cast->getType()->isPointerType() && !Cast->isGLValue())
          break;
        E = Cast->getSubExpr()->IgnoreParenImpCasts();
      }
      return E;
    };
    // A member function called after an await in its arguments, like
    // `f_Update((co_await fg_ContinueRunningOnActor(...), 1))`, runs where
    // the await resumed.
    // Also of a member of the object, `m_Value.f_Read(...)`, which is used
    // through the member of the object it is in.
    // Also through a lookup, `mp_Items[0].f_Read(...)` or
    // `mp_Items.f_FindEqual(0)->f_Read(...)`, and what other arguments that
    // are references or pointers give it, `fg_Read(Value, (co_await ...))`.
    // The member of the object that the object of a call is reached from.
    auto RootMember = [&](const Expr *E) -> const MemberExpr * {
      for (int Depth = 0; E && Depth < 16; ++Depth) {
        E = StripCasts(E);
        if (const auto *Member = dyn_cast<MemberExpr>(E)) {
          const Expr *Base = StripCasts(Member->getBase());
          // Also of what a pointer to it points to, `(*this).f_Read(...)`.
          const Expr *Object = Base;
          if (const auto *Deref = dyn_cast<UnaryOperator>(Object);
              Deref && Deref->getOpcode() == UO_Deref)
            Object = StripCasts(Deref->getSubExpr());
          if (isa<CXXThisExpr, DeclRefExpr>(Object))
            return Member;
          E = Base;
        } else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
                   Operator && Operator->getNumArgs() >= 1)
          E = Operator->getArg(0);
        else if (const auto *Inner = dyn_cast<CXXMemberCallExpr>(E))
          E = Inner->getImplicitObjectArgument();
        // Also through its address, `&m_Value`, which is given as a pointer,
        // or what it points to, `*m_pValue`.
        else if (const auto *Unary = dyn_cast<UnaryOperator>(E);
                 Unary && (Unary->getOpcode() == UO_AddrOf ||
                           Unary->getOpcode() == UO_Deref))
          E = Unary->getSubExpr();
        else
          return nullptr;
      }
      return nullptr;
    };
    // Whether the member a call's object is reached from uses the object,
    // also through what a pointer to it points to, `(*this).f_Read(...)`.
    auto RootUses = [&](const MemberExpr *Root) {
      if (usesObject(Root, Bits))
        return true;
      const auto *Deref = dyn_cast<UnaryOperator>(StripCasts(Root->getBase()));
      return Deref && Deref->getOpcode() == UO_Deref && usesObject(Deref, Bits);
    };
    // What a constructor whose arguments switch is given by reference or
    // pointer, `CCopy{m_Value, (co_await ...)}`, it uses after the switch.
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(S);
        Construct &&
        llvm::any_of(Construct->arguments(), [&](const Expr *Argument) {
          return suspendsIn(Argument, Context);
        }))
      for (unsigned iArgument = 0; iArgument < Construct->getNumArgs();
           ++iArgument) {
        const QualType Type = argumentParameterType(Construct, iArgument);
        if (Type.isNull() ||
            (!Type->isReferenceType() && !Type->isPointerType()))
          continue;
        // What it gives, also the last of a comma, `(fg_Log(), m_Value)`, and
        // either of a conditional, `bChoose ? m_Value : m_Other`.
        llvm::SmallVector<const Expr *, 2> Values{Construct->getArg(iArgument)};
        while (!Values.empty()) {
          const Expr *Argument = StripCasts(Values.pop_back_val());
          if (const auto *Comma = dyn_cast<BinaryOperator>(Argument);
              Comma && Comma->getOpcode() == BO_Comma) {
            Values.push_back(Comma->getRHS());
            continue;
          }
          if (const auto *Conditional =
                  dyn_cast<AbstractConditionalOperator>(Argument)) {
            Values.push_back(Conditional->getTrueExpr());
            Values.push_back(Conditional->getFalseExpr());
            continue;
          }
          if (ThisIsObject && isa<CXXThisExpr>(Argument))
            return true;
          if (const auto *Ref = dyn_cast<DeclRefExpr>(Argument);
              Ref && RefersToObject(Ref->getDecl()))
            return true;
          if (const MemberExpr *Root = RootMember(Argument);
              Root && RootUses(Root))
            return true;
        }
      }
    // What a pointer points to accessed after a switch in the same
    // expression, `pValue[(co_await fg_ContinueRunningOnActor(...), 0)]`.
    if (const Expr *Pointer = accessedAfterSuspension(S, Context))
      for (const Expr *Root : pointerRoots(Pointer)) {
        if (ThisIsObject && isa<CXXThisExpr>(Root))
          return true;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(Root);
            Ref && RefersToObject(Ref->getDecl()))
          return true;
        // One pointed elsewhere after it is read, `pValue[(co_await ...,
        // pValue = &Local, 0)]`, is read as it was before, which can't be
        // told here: a pointer that can point into the object is taken to.
        if (const auto *Ref = dyn_cast<DeclRefExpr>(Root);
            Ref && Pointers.contains(Ref->getDecl())) {
          bool AssignedAfter = false;
          forEachStmt(S, [&](const Stmt *Inner) {
            if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
                Assign && Assign->isAssignmentOp())
              for (const DeclRefExpr *Target :
                   assignedVariables(Assign->getLHS()))
                AssignedAfter |= Target->getDecl() == Ref->getDecl();
          });
          if (AssignedAfter)
            return true;
        }
        if (const MemberExpr *Member = RootMember(Root);
            Member && usesObject(Member, Bits))
          return true;
      }
    if (const auto *Call = dyn_cast<CallExpr>(S);
        Call && llvm::any_of(Call->arguments(), [&](const Expr *Argument) {
          return suspendsIn(Argument, Context);
        })) {
      if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call)) {
        if (const auto *Callee =
                dyn_cast<MemberExpr>(Member->getCallee()->IgnoreParens()))
          if (const MemberExpr *Root = RootMember(Callee))
            if (RootUses(Root))
              return true;
        // Through a pointer to a member function, `(this->*fRead)(...)`.
        if (const auto *Pointer =
                dyn_cast<BinaryOperator>(Member->getCallee()->IgnoreParens());
            Pointer && Pointer->isPtrMemOp()) {
          const Expr *Object = StripCasts(Pointer->getLHS());
          if (const auto *Deref = dyn_cast<UnaryOperator>(Object);
              Deref && Deref->getOpcode() == UO_Deref)
            Object = StripCasts(Deref->getSubExpr());
          if (ThisIsObject && isa<CXXThisExpr>(Object))
            return true;
          if (const auto *Ref = dyn_cast<DeclRefExpr>(Object);
              Ref && RefersToObject(Ref->getDecl()))
            return true;
          if (const MemberExpr *Root = RootMember(Object);
              Root && RootUses(Root))
            return true;
        }
      }
      const FunctionDecl *Callee = Call->getDirectCallee();
      const unsigned Offset =
          isa<CXXOperatorCallExpr>(Call) && isa_and_nonnull<CXXMethodDecl>(Callee)
              ? 1
              : 0;
      if (Offset == 1)
        if (const MemberExpr *Root = RootMember(Call->getArg(0)))
          if (RootUses(Root))
            return true;
      // Also either value of a conditional, `bChoose ? &m_Value : &g_Value`,
      // or the one a constant condition chooses, and the last of a comma.
      std::function<bool(const Expr *, int)> Gives = [&](const Expr *Argument,
                                                         int Depth) {
        Argument = StripCasts(Argument);
        if (Depth < 16) {
          if (const auto *Conditional =
                  dyn_cast<AbstractConditionalOperator>(Argument)) {
            bool Chosen = false;
            if (isa<ConditionalOperator>(Conditional) &&
                !Conditional->getCond()->isValueDependent() &&
                Conditional->getCond()->EvaluateAsBooleanCondition(Chosen,
                                                                   Context))
              return Gives(Chosen ? Conditional->getTrueExpr()
                                  : Conditional->getFalseExpr(),
                           Depth + 1);
            return Gives(Conditional->getTrueExpr(), Depth + 1) ||
                   Gives(Conditional->getFalseExpr(), Depth + 1);
          }
          if (const auto *Comma = dyn_cast<BinaryOperator>(Argument);
              Comma && Comma->getOpcode() == BO_Comma)
            return Gives(Comma->getRHS(), Depth + 1);
        }
        if (ThisIsObject && isa<CXXThisExpr>(Argument))
          return true;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(Argument);
            Ref && RefersToObject(Ref->getDecl()))
          return true;
        const MemberExpr *Root = RootMember(Argument);
        return Root && RootUses(Root);
      };
      // Also through a pointer to a function, `fUse(m_Value, ...)`.
      for (unsigned iArgument = Offset; iArgument < Call->getNumArgs();
           ++iArgument) {
        const QualType Type = argumentParameterType(Call, iArgument);
        if (Type.isNull() ||
            (!Type->isReferenceType() && !Type->isPointerType()))
          continue;
        if (Gives(Call->getArg(iArgument), 0))
          return true;
      }
    }
    if (const auto *Member = dyn_cast<MemberExpr>(S)) {
      const Expr *Base = StripCasts(Member->getBase());
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Base)) {
        if (!RefersToObject(Ref->getDecl()))
          return false;
      } else if (!ThisIsObject || !isa<CXXThisExpr>(Base)) {
        return false;
      }
      return !isAllowedMember(Member);
    }
    // A lambda that captures a copy of what refers into the object, like
    // `[Value]` of `int &Value = m_Value`, reads it when it is made.
    if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
      bool Uses = false;
      auto Init = Lambda->capture_init_begin();
      for (const LambdaCapture &Capture : Lambda->captures()) {
        const Expr *CaptureInit = *Init++;
        if (!CaptureInit || Capture.getCaptureKind() != LCK_ByCopy ||
            !Capture.capturesVariable())
          continue;
        const auto *Ref = dyn_cast<DeclRefExpr>(CaptureInit->IgnoreParenImpCasts());
        if (Ref && !Ref->getType()->isPointerType() &&
            RefersToObject(Ref->getDecl()))
          Uses = true;
      }
      if (Uses)
        return true;
      // The callback of `fg_OnResume` runs right away when it is registered.
      if (isOnResumeCallback(Lambda))
        return invokedUses(Lambda, Bits, firstSwitch(Lambda));
      // One dispatched to the current actor, like `g_Dispatch / [this] {
      // ... }`, `fg_Dispatch(Lambda)` or a timer without an actor, runs on
      // the actor the coroutine is on here.
      if (isDispatchedToCurrentActor(Lambda))
        return invokedUses(Lambda, Bits, firstSwitch(Lambda));
      // One that runs where it is made, through fg_CallSafe or an explicit
      // call of its call operator, until it switches by itself, after which
      // it is checked by itself; a call of the call operator is checked
      // where it is.
      // One held in a local is checked where it is called, which may be
      // after a switch, and so is one that `fg_CallSafe` runs, after its
      // other arguments, which can switch, `fg_CallSafe([this](int) {...},
      // (co_await fg_ContinueRunningOnActor(_Other), 0))`.
      if (isHeldInLocal(Lambda))
        return false;
      {
        DynTypedNode Node = DynTypedNode::create(*Lambda);
        for (int Depth = 0; Depth < 8; ++Depth) {
          const auto Parents = instantiatedParents(Node, Context);
          if (Parents.empty())
            break;
          if (const auto *Call = Parents[0].get<CallExpr>()) {
            if (asCallTo(Call, {"fg_CallSafe"}) && Call->getNumArgs() >= 1 &&
                asLambda(Call->getArg(0)) == Lambda)
              return false;
            // Also a function that can be seen to call it before it returns,
            // `fg_Read([this] { ... })` with `int fg_Read(auto &&_fRead) {
            // return _fRead(); }`, after its other arguments.
            for (unsigned iArgument = 0; iArgument < Call->getNumArgs();
                 ++iArgument)
              if (asLambda(Call->getArg(iArgument)) == Lambda &&
                  callsArgument(Call, iArgument))
                return false;
            break;
          }
          if (!Parents[0].get<ImplicitCastExpr>() &&
              !Parents[0].get<MaterializeTemporaryExpr>() &&
              !Parents[0].get<CXXBindTemporaryExpr>() &&
              !Parents[0].get<CXXConstructExpr>() &&
              !Parents[0].get<ParenExpr>())
            break;
          Node = Parents[0];
        }
      }
      // Until it switches, it runs where it is made: what it does before its
      // first switch is checked here.
      const SourceLocation FirstSwitch = firstSwitch(Lambda);
      const auto Consumers = closureConsumers(Lambda, Context, {});
      if (Consumers.empty() || !isCalledInScope(Consumers) ||
          llvm::any_of(Consumers, [](const ClosureConsumer &Consumer) {
            return isa<CXXOperatorCallExpr>(Consumer.Site);
          }))
        return false;
      return invokedUses(Lambda, Bits, FirstSwitch);
    }
    // One given to a function that can be seen to call it before it returns,
    // `fg_Read([this] { ... })`, also one held in a local, runs on the actor
    // the coroutine is on, also given to a lambda called right away.
    if (const auto *Call = dyn_cast<CallExpr>(S);
        Call && !asCallTo(Call, {"fg_CallSafe"}) && !isStdInvoke(Call))
      for (unsigned iArgument = 0; iArgument < Call->getNumArgs(); ++iArgument)
        if (const LambdaExpr *Invoked = asLambda(Call->getArg(iArgument));
            Invoked &&
            (isa<LambdaExpr>(skipWrappers(Call->getArg(iArgument))) ||
             isHeldInLocal(Invoked)) &&
            callsArgument(Call, iArgument) &&
            invokedUses(Invoked, Bits, firstSwitch(Invoked)))
          return true;
    // A lambda called right away, like `[this] { return m_Value; }()`, runs on
    // the actor the coroutine is on.
    if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(S);
        Call && Call->getOperator() == OO_Call && Call->getNumArgs() >= 1) {
      if (const LambdaExpr *Invoked = asLambda(Call->getArg(0)))
        return invokedUses(Invoked, Bits, firstSwitch(Invoked));
    }
    // Also through its call operator by name, `fRead.operator()()`.
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(S);
        Call && Call->getMethodDecl() &&
        Call->getMethodDecl()->getOverloadedOperator() == OO_Call) {
      if (const LambdaExpr *Invoked = asLambda(Call->getImplicitObjectArgument()))
        return invokedUses(Invoked, Bits, firstSwitch(Invoked));
    }
    // A lambda called right away by `std::invoke(fRead)` runs where it is
    // called.
    if (const auto *Call = dyn_cast<CallExpr>(S); isStdInvoke(Call))
      if (const LambdaExpr *Invoked = asLambda(Call->getArg(0)))
        return invokedUses(Invoked, Bits, firstSwitch(Invoked));
    // A callback held in a local that `fg_OnResume` registers, `co_await
    // fg_OnResume(fOnResume)`, runs where it is registered, which can be after
    // a switch the lambda is made before.
    if (const auto *Call = dyn_cast<CallExpr>(S);
        Call && Call->getNumArgs() >= 1 && asCallTo(Call, {"fg_OnResume"}))
      if (const LambdaExpr *Held = asLambda(Call->getArg(0));
          Held && isHeldInLocal(Held) &&
          invokedUses(Held, Bits, firstSwitch(Held)))
        return true;
    // A lambda run in place by `fg_CallSafe`, also one held in a local,
    // `fg_CallSafe(fRun)`, runs where it is called, after its arguments.
    if (const auto *Call = dyn_cast<CallExpr>(S);
        Call && Call->getNumArgs() >= 1 && asCallTo(Call, {"fg_CallSafe"})) {
      if (const LambdaExpr *Invoked = asLambda(Call->getArg(0));
          Invoked && (isa<LambdaExpr>(skipWrappers(Call->getArg(0))) ||
                      isHeldInLocal(Invoked)))
        return invokedUses(Invoked, Bits, firstSwitch(Invoked));
    }
    // A lambda held in a local and dispatched to the current actor, `auto
    // fRead = [this] {...}; ... g_Dispatch / fRead`, runs on the actor the
    // coroutine is on where it is dispatched.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S)) {
      const Expr *Actor = nullptr;
      if (const LambdaExpr *Stored = asLambda(Ref);
          Stored && isHeldInLocal(Stored) &&
          closureDispatch(Ref, Stored->getLambdaClass(), Context, &Actor) &&
          !Actor)
        return invokedUses(Stored, Bits, firstSwitch(Stored));
    }
    // Passing `this` on, like `fg_Mutate(this)`, lets the callee use the
    // object. Member accesses through it are checked by themselves, and so
    // is `*this`; capturing it or making an alias of it doesn't use it yet, but
    // making a handle to its actor, `fg_ThisActor(this)`, reads the object,
    // which can be gone.
    if (const auto *This = dyn_cast<CXXThisExpr>(S)) {
      if (This->isImplicit() || !ThisIsObject)
        return false;
      // A cast is used where the cast is, and so is a conditional or comma it
      // is chosen by, `(bChoose ? this : pOther)->m_Value`, whose member
      // access is checked here, a static one allowed.
      const Stmt *Through = This;
      const Stmt *User = userOf(This);
      bool Chosen = false;
      for (int Step = 0; User && Step < 16; ++Step) {
        auto Is = [&](const Expr *E) {
          return E && E->IgnoreParenImpCasts() == Through;
        };
        const auto *Comma = dyn_cast<BinaryOperator>(User);
        const auto *Conditional = dyn_cast<AbstractConditionalOperator>(User);
        if (const auto *Cast = dyn_cast<ExplicitCastExpr>(User)) {
          if (!Cast->getType()->isPointerType() && !Cast->isGLValue())
            break;
        } else if (Conditional && (Is(Conditional->getTrueExpr()) ||
                                   Is(Conditional->getFalseExpr())))
          Chosen = true;
        else if (Comma && Comma->getOpcode() == BO_Comma && Is(Comma->getRHS()))
          Chosen = true;
        else
          break;
        Through = User;
        User = userOf(cast<Expr>(User));
      }
      if (const auto *Member = dyn_cast_or_null<MemberExpr>(User))
        return Chosen && !isAllowedMember(Member);
      if (!User || isa<LambdaExpr>(User))
        return false;
      if (const auto *Unary = dyn_cast<UnaryOperator>(User);
          Unary && Unary->getOpcode() == UO_Deref)
        return false;
      if (const auto *Binary = dyn_cast<BinaryOperator>(User);
          Binary && Binary->getOpcode() == BO_Assign)
        return false;
      return true;
    }
    if (const auto *Unary = dyn_cast<UnaryOperator>(S)) {
      if (!ThisIsObject || Unary->getOpcode() != UO_Deref ||
          !isa<CXXThisExpr>(StripCasts(Unary->getSubExpr())))
        return false;
      // Of a member that may be used anywhere, `(*this).self`, like
      // `this->self`.
      const auto *Member = dyn_cast_or_null<MemberExpr>(userOf(Unary));
      return !(Member && isAllowedMember(Member));
    }
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S)) {
      if (!RefersToObject(Ref->getDecl()))
        return false;
      const Stmt *User = userOf(Ref);
      // Through a cast to a base, `static_cast<CActor *>(_pActor)->self`, a
      // member access is checked like one through it.
      if (const auto *Cast = dyn_cast_or_null<ExplicitCastExpr>(User);
          Cast && (Cast->getType()->isPointerType() || Cast->isGLValue())) {
        const Stmt *CastUser = userOf(Cast);
        if (const auto *Member = dyn_cast_or_null<MemberExpr>(CastUser);
            Member && Member->getBase()->IgnoreParenImpCasts() == Cast)
          return !isAllowedMember(Member);
      }
      // Assigning a pointer doesn't use what it points to.
      if (const auto *Binary = dyn_cast_or_null<BinaryOperator>(User);
          Binary && !assignedPointers(Binary).empty() &&
          Binary->getLHS()->IgnoreParenImpCasts() == Ref)
        return false;
      // A member access through it is checked by itself, so that static
      // members stay allowed, like `pThis->fs_Get()`.
      if (const auto *Member = dyn_cast_or_null<MemberExpr>(User);
          Member && Member->getBase()->IgnoreParenImpCasts() == Ref)
        return false;
      // Capturing a pointer, or a reference by reference, doesn't use the
      // object yet, like capturing `this`; what the lambda does with it is
      // checked where it runs. Capturing a copy of what a reference refers
      // to reads it, which the lambda is reported for.
      if (isa_and_nonnull<LambdaExpr>(User))
        return false;
      // Reading the value of a pointer, to compare it, test it or copy it into
      // a local pointer, which is followed by itself, doesn't use what it
      // points to.
      // Not through a reference to a pointer in the object, `int *const
      // &pValue = mp_pValue`, whose value is read from the object.
      if (Ref->getType()->isPointerType() && isPointerValueRead(Ref) &&
          !(Ref->getDecl()->getType()->isReferenceType() &&
            !referencesLocalPointer(Ref->getDecl())))
        return false;
      return true;
    }
    return false;
  }

  /// Whether \p Decl is a reference bound to a local pointer, `int *&pAlias =
  /// pValue`, also through others.
  static bool referencesLocalPointer(const ValueDecl *Decl) {
    for (int Depth = 0; Depth < 64; ++Depth) {
      const auto *Alias = dyn_cast<VarDecl>(Decl);
      if (!Alias || !Alias->getType()->isReferenceType() || !Alias->getInit())
        return false;
      const auto *Bound =
          dyn_cast<DeclRefExpr>(Alias->getInit()->IgnoreParenImpCasts());
      const auto *Local = Bound ? dyn_cast<VarDecl>(Bound->getDecl()) : nullptr;
      if (!Local)
        return false;
      if (!Local->getType()->isReferenceType())
        return Local->hasLocalStorage() || Local->isStaticLocal();
      Decl = Local;
    }
    return false;
  }

  /// The expression that uses \p E, past parentheses and implicit casts.
  const Stmt *userOf(const Expr *E) const {
    DynTypedNode Node = DynTypedNode::create(*E);
    for (;;) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return nullptr;
      if (!Parents[0].get<ParenExpr>() && !Parents[0].get<ImplicitCastExpr>())
        return Parents[0].get<Stmt>();
      Node = Parents[0];
    }
  }

  /// Collects the locals of \p Body that point or refer into the object:
  /// pointers and references initialized from `this` or its members, like
  /// `auto &Internal = *mp_pInternal` or `[pThis = this]`.
  void collectObjectReferences(const Stmt *Body,
                               const FunctionDecl *Function) {
    if (const auto *Lambda =
            dyn_cast<CXXRecordDecl>(Function->getDeclContext());
        Lambda && Lambda->isLambda()) {
      // A captured pointer can be assigned in a mutable lambda, so it is
      // followed along the control flow too, from where it pointed when the
      // lambda was made.
      for (const LambdaCapture &Capture : Lambda->captures()) {
        if (!Capture.capturesVariable())
          continue;
        const auto *Variable = dyn_cast<VarDecl>(Capture.getCapturedVar());
        if (!Variable)
          continue;
        if (Variable->getType()->isPointerType() &&
            Capture.getCaptureKind() == LCK_ByCopy) {
          if (Pointers.try_emplace(Variable, nPointerBits).second)
            ++nPointerBits;
          CapturedPointers[Variable] = refersToObject(Variable, 0);
        } else {
          addIfObjectReference(Variable);
        }
      }
    }
    // Without an actor of its own, a function uses the object of an actor
    // that a parameter points or refers to like `this`, also when it is given
    // another actor's object, whose state can't be used from another actor
    // either; so does a lambda that captures one. With an actor of its own,
    // the object of another actor isn't the one it switches back to, which
    // this doesn't track.
    // With an actor of its own, these are the objects of other actors, which
    // are used only on their own actors.
    auto &ActorObjects = TrackActorObjects ? ObjectReferences : ForeignObjects;
    for (const ParmVarDecl *Parameter : Function->parameters()) {
      if (isActorParameter(Parameter))
        ActorObjects.insert(Parameter);
    }
    if (const auto *Closure =
            dyn_cast<CXXRecordDecl>(Function->getDeclContext());
        Closure && Closure->isLambda()) {
      for (const LambdaCapture &Capture : Closure->captures()) {
        const auto *Variable = Capture.capturesVariable()
                                   ? dyn_cast<VarDecl>(Capture.getCapturedVar())
                                   : nullptr;
        // An alias of `this`, like `[pThis = this]`, is the own object.
        if (Variable && refersToActorObject(Variable->getType()) &&
            !isCapturedAliasOfThis(Variable, /*OwnObject=*/true))
          ActorObjects.insert(Variable);
      }
    }
    // What a local pointer, reference or iterator refers to is followed along
    // the control flow, in a bit per local, so that copies of a pointer that
    // is assigned into the object later are followed too.
    DeclCollector Collector;
    Collector.TraverseStmt(const_cast<Stmt *>(Body));
    // Also a static local, `static int *s_pValue`, and a pointer parameter
    // that is assigned, `_pValue = &m_Value`, which points where the caller
    // gave it until then; not one that is the object of an actor.
    llvm::SmallVector<const VarDecl *, 16> Locals(Collector.Variables.begin(),
                                                  Collector.Variables.end());
    for (const ParmVarDecl *Parameter : Function->parameters())
      if (Parameter->getType()->isPointerType() &&
          !isActorParameter(Parameter) && isChangedVariable(Parameter))
        Locals.push_back(Parameter);
    for (const VarDecl *Variable : Locals) {
      if (!(Variable->hasLocalStorage() || Variable->isStaticLocal()) ||
          (isa<ParmVarDecl>(Variable) && !isChangedVariable(Variable)) ||
          !(Variable->getType()->isPointerType() ||
            Variable->getType()->isReferenceType() ||
            isIteratorType(Variable->getType())))
        continue;
      // A local reference to a local pointer, `int *&pAlias = pValue`, is
      // that pointer: they share a bit, so that assigning, exchanging or
      // swapping either changes both.
      if (Variable->getType()->isReferenceType() &&
          Variable->getType().getNonReferenceType()->isPointerType() &&
          Variable->getInit())
        if (const auto *Bound = dyn_cast<DeclRefExpr>(
                Variable->getInit()->IgnoreParenImpCasts()))
          if (const auto It = Pointers.find(Bound->getDecl());
              It != Pointers.end()) {
            Pointers.try_emplace(Variable, It->second);
            continue;
          }
      if (Pointers.try_emplace(Variable, nPointerBits).second)
        ++nPointerBits;
    }
    // The pointers to local pointers that aren't changed, `int **ppValue =
    // &pValue`, which share what those point to; whether one is changed,
    // which looks through the function, only of those made so.
    for (const auto &[Decl, Bit] : Pointers)
      if (const auto *Alias = dyn_cast<VarDecl>(Decl);
          Alias && Alias->getInit())
        if (const auto *Address = dyn_cast<UnaryOperator>(
                Alias->getInit()->IgnoreParenImpCasts());
            Address && Address->getOpcode() == UO_AddrOf)
          if (const auto *Target = dyn_cast<DeclRefExpr>(
                  Address->getSubExpr()->IgnoreParenImpCasts()))
            if (auto It = Pointers.find(Target->getDecl());
                It != Pointers.end() && It->second != Bit &&
                !isChangedVariable(Alias))
              AddressAliases.emplace_back(Bit, It->second);
    // A lambda copies the pointers it captures when it is made, so each copy
    // has a bit of its own, which a call of the lambda uses.
    for (const auto &Nodes : ast_matchers::match(
             ast_matchers::findAll(ast_matchers::lambdaExpr().bind("lambda")),
             *Body, Context)) {
      const auto *Inner = Nodes.getNodeAs<LambdaExpr>("lambda");
      for (const LambdaCapture &Capture : Inner->captures()) {
        if (Capture.capturesVariable() &&
            Capture.getCaptureKind() == LCK_ByCopy &&
            Pointers.count(Capture.getCapturedVar()))
          LambdaCopies.try_emplace({Inner, Capture.getCapturedVar()},
                                   nPointerBits + LambdaCopies.size());
      }
    }
  }

  /// Whether \p Decl always refers to the object.
  bool isObjectReference(const ValueDecl *Decl) const {
    return ObjectReferences.contains(Decl);
  }

  /// The objects of other actors a function with an actor of its own gets in
  /// parameters and captures. This doesn't track which actor the function is
  /// on for each of them, so after any switch their use is reported.
  llvm::SmallPtrSet<const ValueDecl *, 8> ForeignObjects;

  /// Whether `this` is the object, as opposed to only the parameters and
  /// captures that point or refer to actors.
  bool ThisIsObject = true;

  /// Whether switching to the own actor, like `self`, switches back to the
  /// actor of the object: not when the object is that of the parameters and
  /// captures of a function without an actor of its own.
  bool OwnActorIsObject = true;



  /// The number of bits of the pointer state.
  unsigned bitCount() const { return nPointerBits + LambdaCopies.size(); }

  /// The bits a lambda's own body starts with, for the pointers it captures.
  llvm::BitVector entryBits() const {
    llvm::BitVector Bits(bitCount());
    for (const auto &[Variable, RefersToObject] : CapturedPointers) {
      if (RefersToObject)
        Bits.set(Pointers.lookup(Variable));
    }
    return Bits;
  }

  /// The bit of the copy of \p Variable that \p Lambda captured, if it did.
  std::optional<unsigned> lambdaCopy(const LambdaExpr *Lambda,
                                     const ValueDecl *Variable) const {
    if (auto It = LambdaCopies.find({Lambda, Variable});
        It != LambdaCopies.end())
      return It->second;
    return std::nullopt;
  }

  /// The class whose object the function uses.
  const CXXRecordDecl *ThisClass = nullptr;

  /// The local pointers and references that may point or refer into the
  /// object, by their bits.
  llvm::DenseMap<const ValueDecl *, unsigned> Pointers;
  /// The bits of pointers to local pointers, and of the pointers they point
  /// to.
  llvm::SmallVector<std::pair<unsigned, unsigned>, 2> AddressAliases;
  /// The bits given to pointers, of which local references to them share
  /// theirs.
  unsigned nPointerBits = 0;
  /// The bits of the copies of the pointers that lambdas capture.
  llvm::DenseMap<std::pair<const LambdaExpr *, const ValueDecl *>, unsigned>
      LambdaCopies;
  /// The pointers the lambda being checked captures, and whether they point
  /// into the object where it captures them.
  llvm::DenseMap<const VarDecl *, bool> CapturedPointers;
  /// Whether what an exchange of a local pointer returns, the value it had
  /// before, `fg_Exchange(pValue, nullptr)`, points into the object.
  mutable llvm::DenseMap<const CallExpr *, bool> ExchangedPointers;
  mutable llvm::DenseMap<const Expr *, std::optional<bool>> Switches;
  mutable llvm::DenseMap<const VarDecl *, bool> AssignedLocals;

  /// Records that \p Binding, a name of a structured binding a lambda
  /// captures by reference, refers into the object.
  void setBindingCapture(const BindingDecl *Binding) {
    ObjectReferences.insert(Binding);
  }

  /// Sets whether a capture of \p Variable refers into the object, from what
  /// it points to where the lambda captures it.
  void setCapture(const VarDecl *Variable, bool RefersToObject) {
    if (auto It = CapturedPointers.find(Variable); It != CapturedPointers.end()) {
      It->second = RefersToObject;
      return;
    }
    if (RefersToObject)
      ObjectReferences.insert(Variable);
    else
      ObjectReferences.erase(Variable);
  }

  /// Updates \p Bits for what \p S makes the local pointers point to, and
  /// what the references it declares refer to.
  void updatePointers(const Stmt *S, llvm::BitVector &Bits) const {
    updatePointersOf(S, Bits);
    // A pointer to a local pointer, `int **ppValue = &pValue`, points where
    // that one does now.
    for (const auto &[Alias, Pointer] : AddressAliases)
      Bits[Alias] = Bits.test(Pointer);
  }

  void updatePointersOf(const Stmt *S, llvm::BitVector &Bits) const {
    // A lambda that sets them through its parameters, and captures nothing it
    // could assign too, is like a function.
    unsigned SetterOffset = 0;
    auto SetterOf = [&](const CallExpr *Call) -> const FunctionDecl * {
      const FunctionDecl *Setter = setterCalled(Call, SetterOffset);
      if (const LambdaExpr *Lambda =
              SetterOffset ? calledLambda(Call->getArg(0)) : nullptr;
          Lambda && Lambda->capture_size() != 0)
        return nullptr;
      return Setter && !setParameters(Setter).empty() ? Setter : nullptr;
    };
    if (const auto *Declaration = dyn_cast<DeclStmt>(S)) {
      for (const Decl *D : Declaration->decls()) {
        const auto *Variable = dyn_cast<VarDecl>(D);
        if (auto It = Pointers.find(Variable); It != Pointers.end())
          Bits[It->second] = Variable->getInit() &&
                             !isHandleReference(Variable) &&
                             pointsIntoObject(Variable->getInit(), Bits);
      }
    } else if (const auto *Binary = dyn_cast<BinaryOperator>(S)) {
      // Either of a conditional may be assigned, and keep what it held.
      if (const llvm::SmallVector<const VarDecl *, 2> Assigned =
              assignedPointers(Binary);
          !Assigned.empty()) {
        const bool Into = pointsIntoObject(Binary->getRHS(), Bits);
        const bool Definite = Assigned.size() == 1;
        for (const VarDecl *Pointer : Assigned)
          if (auto It = Pointers.find(Pointer); It != Pointers.end())
            Bits[It->second] = Into || (!Definite && Bits[It->second]);
        // And the reference it is assigned through, which refers to it.
        if (Definite)
          if (const auto *Ref = dyn_cast<DeclRefExpr>(
                  Binary->getLHS()->IgnoreParenImpCasts()))
            if (auto It = Pointers.find(Ref->getDecl()); It != Pointers.end())
              Bits[It->second] = Into;
      }
    } else if (const auto *Call = dyn_cast<CallExpr>(S);
               Call && Call->getNumArgs() == 2 &&
               (isExchangeFunction(calleeName(Call)))) {
      // Assigning the second to the first, `fg_Exchange(pValue, &m_Value)`,
      // and returning what the first was.
      ExchangedPointers[Call] = pointsIntoObject(Call->getArg(0), Bits);
      if (const llvm::SmallVector<const DeclRefExpr *, 2> Targets =
              assignedVariables(Call->getArg(0));
          Targets.size() == 1)
        if (auto It = Pointers.find(Targets[0]->getDecl()); It != Pointers.end())
          Bits[It->second] = pointsIntoObject(Call->getArg(1), Bits);
    } else if (const auto *Call = dyn_cast<CallExpr>(S);
               Call && Call->getNumArgs() == 2 &&
               (isSwapFunction(calleeName(Call)))) {
      // Swapping two pointers swaps where they point, `fg_Swap(pA, pB)`, also
      // through a cast to a reference, `static_cast<int *&>(pA)`.
      auto Swapped = [](const Expr *Argument) -> const DeclRefExpr * {
        const llvm::SmallVector<const DeclRefExpr *, 2> Targets =
            assignedVariables(Argument);
        return Targets.size() == 1 ? Targets[0] : nullptr;
      };
      const DeclRefExpr *Left = Swapped(Call->getArg(0));
      const DeclRefExpr *Right = Swapped(Call->getArg(1));
      const auto LeftIt =
          Left ? Pointers.find(Left->getDecl()) : Pointers.end();
      const auto RightIt =
          Right ? Pointers.find(Right->getDecl()) : Pointers.end();
      const bool LeftInto =
          LeftIt != Pointers.end() ? Bits.test(LeftIt->second)
                                   : Left && pointsIntoObject(Left, Bits);
      const bool RightInto =
          RightIt != Pointers.end() ? Bits.test(RightIt->second)
                                    : Right && pointsIntoObject(Right, Bits);
      if (LeftIt != Pointers.end())
        Bits[LeftIt->second] = RightInto;
      if (RightIt != Pointers.end())
        Bits[RightIt->second] = LeftInto;
    } else if (const auto *Call = dyn_cast<CallExpr>(S);
               Call && SetterOf(Call)) {
      // A function whose body can be seen that assigns a pointer it is given
      // by reference what it is given, `fg_Set(pValue, &m_Value)`, for sure
      // or maybe, also a lambda, `fSet(pValue, &m_Value)`.
      // Not for sure when it can throw before it assigns, to a `try` around
      // the call that catches it.
      const FunctionDecl *Setter = SetterOf(Call);
      const auto *Prototype = Setter->getType()->getAs<FunctionProtoType>();
      const bool MayLeave = !(Prototype && Prototype->isNothrow()) &&
                            insideTryBlock(Call, Context);
      for (const CSetParameter &Set : setParameters(Setter)) {
        const unsigned iSource = Set.Source;
        const bool Always = Set.Always;
        // Also one given by its address, `fg_Set(&pValue, &m_Value)`.
        const Expr *Named = setTargetArgument(Call, Set, SetterOffset);
        if (!Named || iSource + SetterOffset >= Call->getNumArgs())
          continue;
        const llvm::SmallVector<const DeclRefExpr *, 2> Targets =
            assignedVariables(Named);
        if (Targets.size() != 1)
          continue;
        if (auto It = Pointers.find(Targets[0]->getDecl());
            It != Pointers.end())
          Bits[It->second] =
              pointsIntoObject(Call->getArg(iSource + SetterOffset), Bits) ||
              ((!Always || MayLeave) && Bits[It->second]);
      }
    } else if (const LambdaExpr *Called = calledHere(S)) {
      // A lambda called, `fInit()`, assigns what it captures by reference,
      // `[&pValue] { pValue = &m_Value; }`: for sure in a statement of its
      // body before anything that can leave, or else maybe, also in the
      // lambdas it calls in turn, `[&] { fInit(); }()`.
      // Each time it is called, `fCopy(); q = &m_Value; fCopy();`, with what
      // the pointers hold then, but not one in itself, nor more times than
      // code can have.
      // Past as many as code can have, each that is left once, as one that
      // may run, so what it assigns is still applied.
      llvm::SmallPtrSet<const LambdaExpr *, 4> InProgress;
      llvm::SmallPtrSet<const LambdaExpr *, 4> Overflowed;
      unsigned nApplied = 0;
      std::function<void(const LambdaExpr *, bool)> Apply =
          [&](const LambdaExpr *Called, bool CalledSure) {
      const bool Over = ++nApplied > 1024;
      if (Over ? !Overflowed.insert(Called).second
               : !InProgress.insert(Called).second)
        return;
      if (Over)
        CalledSure = false;
      const llvm::scope_exit Done([&] {
        if (!Over)
          InProgress.erase(Called);
      });
      const llvm::SmallPtrSet<const Stmt *, 8> Always =
          statementsBeforeLeaving(Called->getBody(),
                                  insideTryBlock(S, Context));
      forEachStmt(Called->getBody(), [&](const Stmt *Inner) {
        if (const LambdaExpr *Nested = calledHere(Inner)) {
          Apply(Nested, CalledSure && Always.contains(Inner));
          return;
        }
        const auto *Assign = dyn_cast<BinaryOperator>(Inner);
        if (!Assign || Assign->getOpcode() != BO_Assign)
          return;
        const llvm::SmallVector<const DeclRefExpr *, 2> Targets =
            assignedVariables(Assign->getLHS());
        for (const DeclRefExpr *Ref : Targets) {
          // Not a copy that a lambda around it captures, `[p] { [&p] { p =
          // nullptr; }(); }`.
          const bool ByReference =
              capturedByReferenceThroughout(Called, Ref->getDecl(), Context);
          auto It = Pointers.find(Ref->getDecl());
          if (!ByReference || It == Pointers.end())
            continue;
          const bool Into = pointsIntoObject(Assign->getRHS(), Bits);
          Bits[It->second] =
              Into || ((Targets.size() != 1 || !CalledSure ||
                        !Always.contains(Assign)) &&
                       Bits[It->second]);
        }
      });
          };
      Apply(Called, true);
    } else if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
      for (const LambdaCapture &Capture : Lambda->captures()) {
        if (!Capture.capturesVariable())
          continue;
        std::optional<unsigned> Copy =
            lambdaCopy(Lambda, Capture.getCapturedVar());
        if (!Copy)
          continue;
        // An init capture, like `[pThis = this]`, is initialized here.
        const auto *Variable = dyn_cast<VarDecl>(Capture.getCapturedVar());
        Bits[*Copy] = Variable && Variable->isInitCapture()
                          ? Variable->getInit() &&
                                pointsIntoObject(Variable->getInit(), Bits)
                          : Bits.test(Pointers.lookup(Variable));
      }
    }
  }

private:
  /// The lambda \p S calls: called directly, `fInit()`, or run right away
  /// by `fg_CallSafe(Lambda)`, whose future is awaited where it is called.
  static const LambdaExpr *calledHere(const Stmt *S) {
    if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(S);
        Invoke && Invoke->getOperator() == OO_Call && Invoke->getNumArgs() >= 1)
      return calledLambda(Invoke->getArg(0));
    // Also through its call operator by name, `fInit.operator()()`.
    if (const auto *Named = dyn_cast<CXXMemberCallExpr>(S);
        Named && Named->getMethodDecl() &&
        Named->getMethodDecl()->getOverloadedOperator() == OO_Call)
      return calledLambda(Named->getImplicitObjectArgument());
    // Also by `std::invoke(Lambda)`, which calls it right away.
    if (const auto *Call = dyn_cast<CallExpr>(S);
        Call && !isa<CXXOperatorCallExpr>(Call) && Call->getNumArgs() >= 1 &&
        (calleeName(Call) == "fg_CallSafe" ||
         (calleeName(Call) == "invoke" && Call->getDirectCallee() &&
          Call->getDirectCallee()->isInStdNamespace())))
      return calledLambda(Call->getArg(0));
    return nullptr;
  }

  bool isAllowedMember(const MemberExpr *Member) const {
    const ValueDecl *Decl = Member->getMemberDecl();
    // Calls of member functions use the object, but static ones don't, and
    // neither do static data members, like `this->mc_Value`.
    if (const auto *Method = dyn_cast<CXXMethodDecl>(Decl))
      return Method->isStatic();
    if (const auto *Variable = dyn_cast<VarDecl>(Decl);
        Variable && Variable->isStaticDataMember())
      return true;
    // A member of one of the allowed types, when only read.
    return isHandleMember(Member) && !isChanged(Member);
  }

  /// Whether \p Member is of one of the allowed types, which can be read
  /// from any actor.
  bool isHandleMember(const MemberExpr *Member) const {
    return llvm::is_contained(AllowedMemberTypes,
                              recordName(Member->getType()));
  }

  bool isChanged(const MemberExpr *Member) const {
    // Look through parentheses and implicit conversions, like the move of
    // `fg_Move`'s argument, and casts to a reference to it, like
    // `static_cast<TCActor<> &>(m_Actor)`.
    DynTypedNode Node = DynTypedNode::create(*Member);
    for (;;) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return false;
      const auto *Cast = Parents[0].get<ExplicitCastExpr>();
      // Also either value of a conditional, `(bFirst ? m_A : m_B)`, and the
      // right side of a comma, which give it.
      const auto *Conditional = Parents[0].get<AbstractConditionalOperator>();
      const auto *Comma = Parents[0].get<BinaryOperator>();
      const bool GivesIt =
          (Conditional && Conditional->isGLValue() &&
           Conditional->getCond() != Node.get<Expr>()) ||
          (Comma && Comma->getOpcode() == BO_Comma &&
           Comma->getRHS() == Node.get<Expr>());
      if (!Parents[0].get<ParenExpr>() && !Parents[0].get<ImplicitCastExpr>() &&
          !(Cast && Cast->isGLValue()) && !GivesIt)
        return isChangedBy(Parents[0], Node.get<Expr>());
      Node = Parents[0];
    }
  }

  /// Whether \p Parent, the use of \p Member, changes it.
  bool isChangedBy(const DynTypedNode &Parent, const Expr *Member) const {
    Member = Member->IgnoreParenImpCasts();
    // A call of a non-const member function, like `f_Clear()`.
    if (const auto *Outer = Parent.get<MemberExpr>()) {
      const auto *Method = dyn_cast<CXXMethodDecl>(Outer->getMemberDecl());
      return Method && !Method->isStatic() && !Method->isConst();
    }
    if (const auto *Binary = Parent.get<BinaryOperator>())
      return Binary->isAssignmentOp() &&
             Binary->getLHS()->IgnoreParenImpCasts() == Member;
    if (const auto *Call = Parent.get<CXXOperatorCallExpr>();
        Call && Call->isAssignmentOp())
      return Call->getNumArgs() >= 1 &&
             Call->getArg(0)->IgnoreParenImpCasts() == Member;
    // Its address, unless to const, lets what it is passed to change it, like
    // `fg_Replace(&m_OtherActor)`.
    if (const auto *Unary = Parent.get<UnaryOperator>())
      return Unary->getOpcode() == UO_AddrOf &&
             !Unary->getType()->getPointeeType().isConstQualified();
    if (const auto *Call = Parent.get<CallExpr>()) {
      const StringRef Name = calleeName(Call);
      if (isMoveFunction(Name) || isSwapFunction(Name) ||
          isExchangeFunction(Name))
        return true;
      // Passing the handle by non-const reference lets the callee change it.
      for (unsigned iArgument = 0; iArgument < Call->getNumArgs(); ++iArgument) {
        if (Call->getArg(iArgument)->IgnoreParenImpCasts() == Member &&
            mayAssignArgument(Call, iArgument))
          return true;
      }
      return false;
    }
    return false;
  }

  void addIfObjectReference(const VarDecl *Variable) {
    if (refersToObject(Variable, 0))
      ObjectReferences.insert(Variable);
  }

public:
  /// Whether \p Variable is a const reference to a handle, like
  /// `auto const &ThisActor = self`, which can't change it, so it is used like
  /// the handle.
  bool isHandleReference(const VarDecl *Variable) const {
    const auto *Member =
        Variable->getInit()
            ? dyn_cast<MemberExpr>(Variable->getInit()->IgnoreParenImpCasts())
            : nullptr;
    if (!Member || !Variable->getType()->isReferenceType() ||
        !Variable->getType().getNonReferenceType().isConstQualified() ||
        !isHandleMember(Member))
      return false;
    // Of `this`, or of the object of an actor a pointer or reference points
    // or refers to, like `_pActor->self`.
    const Expr *Base = Member->getBase()->IgnoreParenImpCasts();
    if (isa<CXXThisExpr>(Base))
      return true;
    const auto *Ref = dyn_cast<DeclRefExpr>(Base);
    return Ref && refersToActorObject(Ref->getDecl()->getType());
  }

  /// Whether \p Lambda captures a pointer or reference that may point or
  /// refer into the object where it is made, other than OwnActorObject.
  bool capturesObject(const LambdaExpr *Lambda) const {
    return llvm::any_of(Lambda->captures(), [&](const LambdaCapture &Capture) {
      if (!Capture.capturesVariable())
        return false;
      const ValueDecl *Variable = Capture.getCapturedVar();
      // The object of the only actor it captures is the own one.
      if (Variable == OwnActorObject)
        return false;
      if (ObjectReferences.contains(Variable))
        return true;
      const auto *Var = dyn_cast<VarDecl>(Variable);
      auto It = Var ? CapturedPointers.find(Var) : CapturedPointers.end();
      return It != CapturedPointers.end() && It->second;
    });
  }

private:
  /// Whether \p Variable, which a lambda captures, is a pointer or a reference
  /// into the object, like `[pThis = this]`.
  bool refersToObject(const VarDecl *Variable, int) const {
    if (!Variable || !Variable->getInit() ||
        !(Variable->getType()->isPointerType() ||
          Variable->getType()->isReferenceType()) ||
        isHandleReference(Variable))
      return false;
    return ThisIsObject && isRootedInThis(Variable->getInit());
  }

  ASTContext &Context;
  llvm::ArrayRef<StringRef> AllowedMemberTypes;
  llvm::SmallPtrSet<const ValueDecl *, 8> ObjectReferences;
};

/// Propagates which local pointers of \p Flow may point into the object over
/// \p Graph, and returns the bits at the start of each block.
llvm::DenseMap<const CFGBlock *, llvm::BitVector>
pointerStates(const ActorFlow &Flow, const CFG &Graph,
              const llvm::BitVector &Reachable) {
  const unsigned nPointers = Flow.bitCount();
  llvm::DenseMap<const CFGBlock *, llvm::BitVector> AtEnd;
  auto Entry = [&](const CFGBlock *Block) {
    // What the parameters and captures point to when the function starts.
    llvm::BitVector Bits =
        Block == &Graph.getEntry() ? Flow.entryBits() : llvm::BitVector(nPointers);
    for (const CFGBlock::AdjacentBlock &Predecessor : Block->preds()) {
      if (const CFGBlock *Pred = Predecessor.getReachableBlock()) {
        if (auto It = AtEnd.find(Pred); It != AtEnd.end())
          Bits |= It->second;
      }
    }
    return Bits;
  };
  const llvm::SmallVector<const CFGBlock *, 16> Order = forwardOrder(Graph);
  for (bool Changed = true; Changed;) {
    Changed = false;
    for (const CFGBlock *Block : Order) {
      if (!Reachable.test(Block->getBlockID()))
        continue;
      llvm::BitVector Bits = Entry(Block);
      for (const CFGElement &Element : *Block) {
        if (const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>())
          Flow.updatePointers(Statement->getStmt(), Bits);
      }
      auto [It, Inserted] = AtEnd.try_emplace(Block, llvm::BitVector(nPointers));
      llvm::BitVector Merged = It->second;
      Merged |= Bits;
      if (Inserted || Merged != It->second) {
        It->second = Merged;
        Changed = true;
      }
    }
  }
  llvm::DenseMap<const CFGBlock *, llvm::BitVector> AtStart;
  for (const CFGBlock *Block : Graph)
    AtStart[Block] = Entry(Block);
  return AtStart;
}

void setCaptures(ActorFlow &Flow, const LambdaExpr *Lambda,
                 ASTContext &Context,
                 llvm::ArrayRef<StringRef> AllowedMemberTypes, int Depth);

std::optional<bool> pointsIntoObjectAtCaptureUncached(
    const ActorFlow &Flow, const VarDecl *Variable, const LambdaExpr *Lambda,
    bool InitCapture, bool ByReference, ASTContext &Context,
    llvm::ArrayRef<StringRef> AllowedMemberTypes, int Depth);

/// Whether the pointer or reference \p Variable may point or refer into the
/// object of \p Flow where \p Lambda captures it, from the control flow of
/// the function or lambda that makes \p Lambda, or null when that can't be
/// told. An init capture, like `[pValue = &_pActor->m_Value]`, is initialized
/// there. Each once while a capture is looked at, since the captures of
/// lambdas nested in each other are each looked at for each capture of the
/// ones inside.
std::optional<bool> pointsIntoObjectAtCapture(
    const ActorFlow &Flow, const VarDecl *Variable, const LambdaExpr *Lambda,
    bool InitCapture, bool ByReference, ASTContext &Context,
    llvm::ArrayRef<StringRef> AllowedMemberTypes, int Depth) {
  using CKey = std::tuple<const void *, const void *, const void *, unsigned>;
  using CResults = llvm::DenseMap<CKey, std::optional<bool>>;
  static thread_local CResults *s_pResults = nullptr;
  std::optional<CResults> Owned;
  if (!s_pResults)
    s_pResults = &Owned.emplace();
  const llvm::scope_exit Release([&] {
    if (Owned)
      s_pResults = nullptr;
  });
  const CKey Key{Variable, Lambda, Flow.ThisClass,
                 unsigned(InitCapture) | unsigned(ByReference) << 1 |
                     unsigned(Flow.ThisIsObject) << 2 |
                     unsigned(Flow.TrackActorObjects) << 3};
  if (const auto Found = s_pResults->find(Key); Found != s_pResults->end())
    return Found->second;
  const std::optional<bool> Result = pointsIntoObjectAtCaptureUncached(
      Flow, Variable, Lambda, InitCapture, ByReference, Context,
      AllowedMemberTypes, Depth);
  (*s_pResults)[Key] = Result;
  return Result;
}

std::optional<bool> pointsIntoObjectAtCaptureUncached(
    const ActorFlow &Flow, const VarDecl *Variable, const LambdaExpr *Lambda,
    bool InitCapture, bool ByReference, ASTContext &Context,
    llvm::ArrayRef<StringRef> AllowedMemberTypes, int Depth) {
  const FunctionDecl *Outer = enclosingFunction(Lambda);
  Stmt *Body = Outer ? Outer->getBody() : nullptr;
  if (!Body)
    return std::nullopt;
  if (auto *CoroutineBody = dyn_cast<CoroutineBodyStmt>(Body))
    Body = CoroutineBody->getBody();
  CFG::BuildOptions Options;
  Options.setAllAlwaysAdd();
  Options.AddEHEdges = true;
  const std::unique_ptr<CFG> Graph =
      CFG::buildCFG(Outer, Body, &Context, Options);
  if (!Graph)
    return std::nullopt;
  // The object is the same where the lambda is made: what `this` is, or what
  // the actors of parameters and captures are.
  ActorFlow OuterFlow(Context, AllowedMemberTypes);
  OuterFlow.ThisClass = Flow.ThisClass;
  OuterFlow.ThisIsObject = Flow.ThisIsObject;
  OuterFlow.TrackActorObjects = Flow.TrackActorObjects;
  OuterFlow.collectObjectReferences(Body, Outer);
  // A lambda that makes the lambda gets what it captures from further out.
  if (const auto *OuterMethod = dyn_cast<CXXMethodDecl>(Outer);
      OuterMethod && OuterMethod->getParent()->isLambda()) {
    DynTypedNode Node = DynTypedNode::create(*Lambda);
    for (int Up = 0; Up < 256; ++Up) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        break;
      Node = Parents[0];
      if (const auto *Around = Node.get<LambdaExpr>();
          Around && Around->getCallOperator() == OuterMethod) {
        setCaptures(OuterFlow, Around, Context, AllowedMemberTypes, Depth + 1);
        break;
      }
    }
  }
  if (InitCapture && (!Variable->getInit() ||
                      OuterFlow.isHandleReference(Variable)))
    return false;
  unsigned Bit = 0;
  if (!InitCapture) {
    if (OuterFlow.isObjectReference(Variable))
      return true;
    const auto It = OuterFlow.Pointers.find(Variable);
    if (It == OuterFlow.Pointers.end())
      return std::nullopt;
    Bit = It->second;
  }
  const llvm::BitVector Reachable = reachableBlocks(*Graph);
  const auto AtStart = pointerStates(OuterFlow, *Graph, Reachable);
  // A variable captured by reference can be given another value after the
  // lambda is made, while the lambda uses it, so where it points anywhere
  // counts.
  bool Anywhere = false;
  for (const CFGBlock *Block : *Graph) {
    if (!Reachable.test(Block->getBlockID()))
      continue;
    llvm::BitVector Bits = AtStart.lookup(Block);
    for (const CFGElement &Element : *Block) {
      const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>();
      if (!Statement)
        continue;
      if (ByReference && !InitCapture) {
        Anywhere |= Bits.test(Bit);
      } else if (Statement->getStmt() == Lambda) {
        return InitCapture
                   ? OuterFlow.pointsIntoObject(Variable->getInit(), Bits)
                   : Bits.test(Bit);
      }
      OuterFlow.updatePointers(Statement->getStmt(), Bits);
      if (ByReference && !InitCapture)
        Anywhere |= Bits.test(Bit);
    }
  }
  if (ByReference && !InitCapture)
    return Anywhere;
  return std::nullopt;
}

/// Sets where the pointers and references \p Lambda captures point or refer
/// for \p Flow, the flow of its body: a local pointer the lambda copies points
/// where it did when the lambda was made, which can differ from what it was
/// initialized with.
void setCaptures(ActorFlow &Flow, const LambdaExpr *Lambda,
                 ASTContext &Context,
                 llvm::ArrayRef<StringRef> AllowedMemberTypes, int Depth) {
  // Past as many lambdas around it as code has, what its pointers point to
  // can't be told, and may be the object.
  if (Depth > 64) {
    for (const LambdaCapture &Capture : Lambda->captures())
      if (const auto *Variable = Capture.capturesVariable()
                                     ? dyn_cast<VarDecl>(Capture.getCapturedVar())
                                     : nullptr;
          Variable && (Variable->getType()->isPointerType() ||
                       Variable->getType()->isReferenceType()))
        Flow.setCapture(Variable, true);
    return;
  }
  for (const LambdaCapture &Capture : Lambda->captures()) {
    // A name of a structured binding of an object, like `auto &[Value,
    // Other] = _pActor->m_Data`, captured by reference refers to a part of
    // what the decomposition refers to.
    if (const auto *Binding = dyn_cast_or_null<BindingDecl>(
            Capture.capturesVariable() ? Capture.getCapturedVar() : nullptr);
        Binding && Capture.getCaptureKind() == LCK_ByRef) {
      const auto *Decomposed =
          dyn_cast_or_null<VarDecl>(Binding->getDecomposedDecl());
      if (Decomposed && Decomposed->getType()->isReferenceType())
        if (const std::optional<bool> RefersToObject = pointsIntoObjectAtCapture(
                Flow, Decomposed, Lambda, false, true, Context,
                AllowedMemberTypes, Depth);
            RefersToObject && *RefersToObject)
          Flow.setBindingCapture(Binding);
      continue;
    }
    const auto *Variable = Capture.capturesVariable()
                               ? dyn_cast<VarDecl>(Capture.getCapturedVar())
                               : nullptr;
    if (!Variable || !(Variable->getType()->isPointerType() ||
                       Variable->getType()->isReferenceType()))
      continue;
    // A reference captured by copy, `[Value]` of `int &Value`, copies what
    // it refers to, which doesn't refer anywhere unless it is a pointer.
    const bool InitCapture = Lambda->isInitCapture(&Capture);
    if (!InitCapture && Capture.getCaptureKind() == LCK_ByCopy &&
        Variable->getType()->isReferenceType() &&
        !Variable->getType().getNonReferenceType()->isPointerType()) {
      Flow.setCapture(Variable, false);
      continue;
    }
    // An init capture of an outer lambda that this one captures again points
    // where the copy of the outer lambda does.
    if (const std::optional<bool> RefersToObject = pointsIntoObjectAtCapture(
            Flow, Variable, Lambda, InitCapture,
            Capture.getCaptureKind() == LCK_ByRef, Context,
            AllowedMemberTypes, Depth))
      Flow.setCapture(Variable, *RefersToObject);
  }
}

} // namespace

/// Reports the uses of the object of \p Flow on another actor: after a
/// switch to another actor, or with \p AfterAnySwitch, after any switch.
static void analyzeFlow(const ActorFlow &Flow, const CFG &Graph,
                        const llvm::BitVector &Reachable, bool AfterAnySwitch,
                        llvm::DenseSet<SourceLocation> &Reported,
                        const SourceManager &SM,
                        llvm::function_ref<void(SourceLocation)> Report) {
  // Whether each block may end on another actor, or after any switch, which
  // local pointers may point into the object there, and which scope exits
  // may still run, not cancelled by `f_Clear()` on every path.
  struct CState {
    bool OnOtherActor = false;
    llvm::BitVector Pointers;
    llvm::SmallPtrSet<const VarDecl *, 2> ArmedScopeExits;
    /// The callbacks of `fg_OnResume` registrations that are temporaries of
    /// the full expression being evaluated, registered on the way here,
    /// `(co_await fg_OnResume(...), co_await fg_ContinueRunningOnActor(
    /// Other))`, alive until its end.
    llvm::SmallPtrSet<const LambdaExpr *, 2> TemporaryResumes;

    bool operator!=(const CState &Other) const {
      return OnOtherActor != Other.OnOtherActor || Pointers != Other.Pointers ||
             ArmedScopeExits != Other.ArmedScopeExits ||
             TemporaryResumes != Other.TemporaryResumes;
    }
    void merge(const CState &Other) {
      OnOtherActor |= Other.OnOtherActor;
      Pointers |= Other.Pointers;
      ArmedScopeExits.insert(Other.ArmedScopeExits.begin(),
                             Other.ArmedScopeExits.end());
      TemporaryResumes.insert(Other.TemporaryResumes.begin(),
                              Other.TemporaryResumes.end());
    }
  };
  const unsigned nPointers = Flow.bitCount();
  // A scope exit that is a discarded temporary, `(void)(g_OnScopeExit /
  // [this] { ... })`, runs its lambda at the end of the full expression it is
  // made in, also after what the expression awaits after it, unless it
  // dispatches it to the actor it was made on.
  auto TemporaryExit = [&](const LambdaExpr *Lambda) {
    return Lambda && isTemporaryScopeExitLambda(Lambda, Flow.context()) &&
           !closureDispatch(Lambda, Lambda->getLambdaClass(), Flow.context(),
                            nullptr);
  };
  llvm::DenseMap<const Stmt *, llvm::SmallVector<const LambdaExpr *, 1>>
      ExitsAtEnd;
  // The full expression that each temporary `fg_OnResume` registration
  // ends with.
  llvm::DenseMap<const LambdaExpr *, const Expr *> RegistrationEnds;
  for (const CFGBlock *Block : Graph)
    if (Reachable.test(Block->getBlockID()))
      for (const CFGElement &Element : *Block)
        if (const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>())
          if (const auto *Lambda = dyn_cast<LambdaExpr>(Statement->getStmt());
              TemporaryExit(Lambda))
            ExitsAtEnd[fullExpressionOf(Lambda, Flow.context())].push_back(
                Lambda);
  auto Transfer = [&](const CFGBlock *Block, CState State, auto &&OnElement) {
    for (const CFGElement &Element : *Block) {
      // A scope exit, like `g_OnScopeExit / [this] { ... }`, runs its lambda
      // where its holder is destroyed, on the actor the coroutine is on then.
      // So does the cleanup of `fg_AsyncDestroy`, which it starts there,
      // unless the guard was moved elsewhere, like awaited early.
      if (const std::optional<CFGAutomaticObjDtor> Destructor =
              Element.getAs<CFGAutomaticObjDtor>()) {
        const VarDecl *Holder = Destructor->getVarDecl();
        // One that dispatches its lambda to the actor it was registered on,
        // `g_OnScopeExitActor / [this] { ... }`, is checked where it is made.
        if (const LambdaExpr *Cleanup = scopeExitLambda(Holder)) {
          if (State.ArmedScopeExits.contains(Holder) &&
              !closureDispatch(Cleanup, Cleanup->getLambdaClass(),
                               Flow.context(), nullptr))
            OnElement(Cleanup, State, Holder);
        }
        else if (State.ArmedScopeExits.contains(Holder))
          for (const LambdaExpr *Destroy : asyncDestroyLambdas(Holder))
            OnElement(Destroy, State, Holder);
        continue;
      }
      const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>();
      if (!Statement)
        continue;
      const Stmt *S = Statement->getStmt();
      // What runs a cleanup early runs it here: a scope exit called,
      // `Cleanup()`, unless it dispatches its lambda, or the cleanup of
      // `fg_AsyncDestroy` awaited early, `co_await fg_Move(Cleanup)`.
      updateArmedCleanups(
          S, State.ArmedScopeExits,
          [&](const LambdaExpr *Lambda, const VarDecl *Holder) {
            if (!scopeExitLambda(Holder) ||
                !closureDispatch(Lambda, Lambda->getLambdaClass(),
                                 Flow.context(), nullptr))
              OnElement(Lambda, State, Holder);
          });
      OnElement(S, State, nullptr);
      Flow.updatePointers(S, State.Pointers);
      if (const std::optional<bool> Other = Flow.switchesTo(S))
        State.OnOtherActor = AfterAnySwitch || *Other;
      // The temporary scope exits made in it run at its end, after it.
      if (auto It = ExitsAtEnd.find(S); It != ExitsAtEnd.end())
        for (const LambdaExpr *Lambda : It->second)
          OnElement(Lambda, State, nullptr);
      // The callbacks of `fg_OnResume` run where the coroutine resumes, also
      // on the actor it switched to, and when they are registered.
      // Registering runs the callback right away, which is checked where the
      // lambda is made.
      if (State.OnOtherActor && isSuspension(S))
        for (const VarDecl *Holder : liveOnResumeHolders(S, Flow.context())) {
          const llvm::SmallVector<const LambdaExpr *, 2> Lambdas =
              onResumeLambdas(Holder);
          for (const LambdaExpr *Lambda : Lambdas)
            OnElement(Lambda, State, Holder);
          if (Lambdas.empty())
            OnElement(Holder->getInit(), State, Holder);
        }
      // Also a registration that is a temporary of the full expression, alive
      // until its end, `(co_await fg_OnResume(...), co_await
      // fg_ContinueRunningOnActor(Other))`, along the paths that register
      // it, not one in another arm of a conditional, which runs instead.
      if (State.OnOtherActor && isSuspension(S))
        for (const LambdaExpr *Lambda : State.TemporaryResumes)
          OnElement(Lambda, State, nullptr);
      // Not of one held in a variable, which is checked by its holder.
      if (const auto *Await = dyn_cast<CoawaitExpr>(S)) {
        const llvm::SmallVector<const CallExpr *, 2> Registrations =
            onResumeCalls(Await->getOperand());
        if (!Registrations.empty())
          if (const Expr *Full = fullExpressionOf(Await, Flow.context())) {
            const auto Above = instantiatedParents(*Full, Flow.context());
            if (Above.empty() || !Above[0].get<VarDecl>())
              // Also either of a conditional, `co_await (bChoose ?
              // fg_OnResume(A) : fg_OnResume(B))`.
              for (const CallExpr *Registration : Registrations)
                if (const LambdaExpr *Lambda =
                        Registration && Registration->getNumArgs() >= 1
                            ? asLambda(Registration->getArg(0))
                            : nullptr) {
                  State.TemporaryResumes.insert(Lambda);
                  RegistrationEnds[Lambda] = Full;
                }
          }
      }
      // They end with the full expression they are made in.
      if (!State.TemporaryResumes.empty())
        for (const LambdaExpr *Lambda :
             llvm::SmallVector<const LambdaExpr *, 2>(
                 State.TemporaryResumes.begin(), State.TemporaryResumes.end()))
          if (RegistrationEnds.lookup(Lambda) == S)
            State.TemporaryResumes.erase(Lambda);
    }
    return State;
  };
  llvm::DenseMap<const CFGBlock *, CState> StateAtEnd;
  auto Entry = [&](const CFGBlock *Block) {
    CState State{false,
                 Block == &Graph.getEntry() ? Flow.entryBits()
                                            : llvm::BitVector(nPointers),
                 {},
                 {}};
    for (const CFGBlock::AdjacentBlock &Predecessor : Block->preds()) {
      if (const CFGBlock *Pred = Predecessor.getReachableBlock()) {
        if (auto It = StateAtEnd.find(Pred); It != StateAtEnd.end())
          State.merge(It->second);
      }
    }
    return State;
  };

  // Propagate until nothing changes, looking again only at the successors of
  // a block whose end changed. Bits are only ever added at the ends of
  // blocks, so this ends.
  std::deque<const CFGBlock *> Work;
  llvm::BitVector Queued(Graph.getNumBlockIDs());
  for (const CFGBlock *Block : llvm::reverse(Graph))
    if (Reachable.test(Block->getBlockID())) {
      Work.push_back(Block);
      Queued.set(Block->getBlockID());
    }
  while (!Work.empty()) {
    const CFGBlock *Block = Work.front();
    Work.pop_front();
    Queued.reset(Block->getBlockID());
    const CState End =
        Transfer(Block, Entry(Block),
                 [](const Stmt *, const CState &, const VarDecl *) {});
    auto [It, Inserted] = StateAtEnd.try_emplace(
        Block, CState{false, llvm::BitVector(nPointers), {}, {}});
    CState Merged = It->second;
    Merged.merge(End);
    if (!Inserted && !(Merged != It->second))
      continue;
    It->second = Merged;
    for (const CFGBlock *Successor : Block->succs())
      if (Successor && Reachable.test(Successor->getBlockID()) &&
          !Queued.test(Successor->getBlockID())) {
        Work.push_back(Successor);
        Queued.set(Successor->getBlockID());
      }
  }

  for (const CFGBlock *Block : Graph) {
    if (!Reachable.test(Block->getBlockID()))
      continue;
    Transfer(Block, Entry(Block), [&](const Stmt *S, const CState &State,
                                      const VarDecl *ScopeExit) {
      if (!State.OnOtherActor)
        return;
      auto ReportAt = [&](const Stmt *At) {
        if (!Reported.insert(At->getBeginLoc()).second)
          return;
        const SourceLocation Loc = reportLocation(At->getBeginLoc(), SM);
        if (Loc.isValid())
          Report(Loc);
      };
      const bool Runs =
          ScopeExit || TemporaryExit(dyn_cast<LambdaExpr>(S));
      // What a lambda that `fg_CallSafe` runs uses is reported at the lambda.
      const Stmt *At = S;
      if (const auto *Call = dyn_cast<CallExpr>(S);
          Call && Call->getNumArgs() >= 1 && asCallTo(Call, {"fg_CallSafe"}))
        if (const auto *Inline =
                dyn_cast<LambdaExpr>(skipWrappers(Call->getArg(0))))
          At = Inline;
      if (Runs ? Flow.scopeExitUses(ScopeExit, S, State.Pointers)
               : Flow.usesObject(S, State.Pointers))
        ReportAt(At);
    });
  }
}


void ContinueRunningOnActorCheck::checkFunction(const FunctionDecl *Function,
                                                const LambdaExpr *Lambda,
                                                ASTContext &Context) {
  if (!Function)
    return;
  Stmt *Body = Function->getBody();
  if (!Body)
    return;
  if (auto *CoroutineBody = dyn_cast<CoroutineBodyStmt>(Body))
    Body = CoroutineBody->getBody();

  CFG::BuildOptions Options;
  Options.setAllAlwaysAdd();
  // Exceptions can reach handlers after a suspension.
  Options.AddEHEdges = true;
  // Where scope exits run.
  Options.AddImplicitDtors = true;
  const std::unique_ptr<CFG> Graph =
      CFG::buildCFG(Function, Body, &Context, Options);
  // Not accepted when its control flow can't be built, like through a long
  // chain of conditionals in default arguments that destroy temporaries.
  if (!Graph) {
    const SourceLocation Loc =
        reportLocation(Lambda ? Lambda->getBeginLoc() : Function->getLocation(),
                       Context.getSourceManager());
    if (Loc.isValid())
      diag(Loc, "the control flow of %select{%1|the lambda}0 is too complex to "
                "analyze, so whether it uses the object of the actor after it "
                "switches away can't be told; split up what makes it complex")
          << isLambdaCallOperator(Function) << Function;
    return;
  }
  // Blocks that can't run, like `if (false)` branches, don't change the state.
  const llvm::BitVector Reachable = reachableBlocks(*Graph);

  ActorFlow Flow(Context, AllowedMemberTypes);
  if (Lambda)
    Flow.ThisClass = enclosingThisClass(Lambda);
  else if (const auto *Method = dyn_cast<CXXMethodDecl>(Function))
    Flow.ThisClass = Method->getParent();
  // An actor of its own: `this` of an actor, also for a lambda made in a
  // member function of one, whatever it captures.
  // However many lambdas are around it: each is a level of the code, and so
  // as many as the code has.
  const FunctionDecl *Owner = Function;
  while (true) {
    const auto *OwnerMethod = dyn_cast<CXXMethodDecl>(Owner);
    if (!OwnerMethod || !OwnerMethod->getParent()->isLambda())
      break;
    const auto *Around = dyn_cast_or_null<FunctionDecl>(
        OwnerMethod->getParent()->getDeclContext());
    if (!Around)
      break;
    Owner = Around;
  }
  const auto *OwnerMethod = dyn_cast<CXXMethodDecl>(Owner);
  // A lambda in a default member initializer is made by a constructor.
  const CXXRecordDecl *OwnerClass =
      OwnerMethod && OwnerMethod->getParent()->isLambda()
          ? OwnerMethod->getParent()
          : nullptr;
  const bool InMemberInitializer =
      OwnerClass && isa_and_nonnull<FieldDecl>(OwnerClass->getLambdaContextDecl());
  const bool HasOwnActor = Flow.ThisClass && isActorClass(Flow.ThisClass) &&
                           OwnerMethod &&
                           ((OwnerMethod->isInstance() &&
                             !OwnerMethod->getParent()->isLambda()) ||
                            InMemberInitializer);
  Flow.TrackActorObjects = !HasOwnActor;
  Flow.ThisIsObject = HasOwnActor;
  Flow.OwnActorIsObject = HasOwnActor;
  // Without an actor of its own, a function with only one parameter or
  // capture that points or refers to the object of an actor, which isn't
  // changed, switches back to the actor of that object.
  if (!HasOwnActor) {
    unsigned nObjects = 0;
    bool Unchanged = true;
    for (const ParmVarDecl *Parameter : Function->parameters()) {
      if (!isActorParameter(Parameter))
        continue;
      ++nObjects;
      Flow.OwnActorObject = Parameter;
      Unchanged = !ActorFlow::isReassigned(Parameter);
    }
    if (Lambda) {
      for (const LambdaCapture &Capture : Lambda->captures()) {
        if (!Capture.capturesVariable() ||
            !refersToActorObject(Capture.getCapturedVar()->getType()))
          continue;
        ++nObjects;
        Flow.OwnActorObject = Capture.getCapturedVar();
        // A reference can't be changed, and neither can a copy in a closure
        // that isn't mutable, or one its body doesn't change; a variable
        // captured by reference must not be changed anywhere.
        const auto *Variable = dyn_cast<VarDecl>(Capture.getCapturedVar());
        Unchanged =
            ActorFlow::rebindsNothing(Capture.getCapturedVar()->getType()) ||
            (Capture.getCaptureKind() == LCK_ByCopy &&
             (Lambda->getCallOperator()->isConst() ||
              (Variable && !isChangedIn(Variable, Lambda->getBody())))) ||
            (Capture.getCaptureKind() == LCK_ByRef && Variable &&
             !isChangedVariable(Variable));
      }
    }
    Flow.ParameterIsOwnActor = nObjects == 1 && Unchanged;
  }
  Flow.collectObjectReferences(Body, Function);
  if (Lambda)
    setCaptures(Flow, Lambda, Context, AllowedMemberTypes, 0);
  // Nor with a capture into an object, which may be another actor's.
  if (Lambda && Flow.capturesObject(Lambda))
    Flow.ParameterIsOwnActor = false;

  const SourceManager &SM = Context.getSourceManager();
  // A member access and the reference it accesses through start at the same
  // place.
  llvm::DenseSet<SourceLocation> Reported;
  auto Report = [&](SourceLocation Loc) {
    diag(Loc, "the object of the actor is used after "
              "'fg_ContinueRunningOnActor' moved the coroutine to another "
              "actor, where using it races with its own actor");
  };
  analyzeFlow(Flow, *Graph, Reachable, /*AfterAnySwitch=*/false, Reported, SM,
              Report);

  // The objects of other actors that a function with an actor of its own
  // gets, which aren't the actor it switches back to, are used safely only
  // before any switch.
  if (HasOwnActor && (!Flow.ForeignObjects.empty() || Lambda)) {
    ActorFlow Foreign(Context, AllowedMemberTypes);
    Foreign.ThisClass = Flow.ThisClass;
    Foreign.ThisIsObject = false;
    Foreign.TrackActorObjects = true;
    Foreign.collectObjectReferences(Body, Function);
    if (Lambda)
      setCaptures(Foreign, Lambda, Context, AllowedMemberTypes, 0);
    analyzeFlow(Foreign, *Graph, Reachable, /*AfterAnySwitch=*/true, Reported,
                SM, Report);
  }
}

} // namespace clang::tidy::malterlib
