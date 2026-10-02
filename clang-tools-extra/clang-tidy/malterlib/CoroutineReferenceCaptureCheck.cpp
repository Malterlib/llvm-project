//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CoroutineReferenceCaptureCheck.h"
#include "RunsToCompletion.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/ScopeExit.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

void CoroutineReferenceCaptureCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(lambdaExpr().bind("lambda"), this);
}

static const llvm::ArrayRef<StringRef> AsyncDestroyNames(
    std::begin(AsyncDestroyFunctions), std::end(AsyncDestroyFunctions));

/// Whether \p E, through parentheses, implicit nodes and `fg_Move`, is the
/// cleanup argument of a call of `fg_AsyncDestroy`.
static bool isAsyncDestroyArgument(const Expr *E, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*E);
  for (int Depth = 0; Depth < 8; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    if (const auto *Call = Parents[0].get<CallExpr>()) {
      if (Call->getNumArgs() == 1 &&
          asCallTo(Call, vocabulary().MoveFunctions.names())) {
        Node = Parents[0];
        continue;
      }
      // A guard that is returned, or moved elsewhere, runs the cleanup after
      // the variables are gone.
      return asCallTo(Call, AsyncDestroyNames) && Call->getNumArgs() >= 1 &&
             Call->getArg(0)->IgnoreUnlessSpelledInSource() ==
                 Node.get<Expr>()->IgnoreUnlessSpelledInSource() &&
             isCleanupJoined(Call, Context);
    }
    if (!Parents[0].get<Expr>())
      return false;
    Node = Parents[0];
  }
  return false;
}

/// Whether every reference to the local \p Variable is the cleanup argument
/// of `fg_AsyncDestroy`, and there is one.
static bool onlyAsyncDestroyed(const VarDecl *Variable, ASTContext &Context) {
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return false;
  const auto Refs =
      match(findAll(declRefExpr(to(varDecl(equalsNode(Variable))),
                                unless(isInUnevaluatedContext()))
                        .bind("ref")),
            *Function->getBody(), Context);
  return !Refs.empty() && llvm::all_of(Refs, [&](const BoundNodes &Nodes) {
    const auto *Ref = Nodes.getNodeAs<DeclRefExpr>("ref");
    // What a lambda captures it with isn't a use; what its body does with it
    // is.
    const auto Parents = instantiatedParents(*Ref, Context);
    if (!Parents.empty() && Parents[0].get<LambdaExpr>())
      return true;
    return isAsyncDestroyArgument(Ref, Context);
  });
}

/// Whether \p Lambda is the cleanup function of `fg_AsyncDestroy`, which starts
/// it when its guard is destroyed, while the variables declared before the
/// guard are there. It is passed directly, through a local, like
/// `fg_AsyncDestroy(fg_Move(fCleanup))`, or made by a factory lambda that every
/// call of passes to it, like `fg_AsyncDestroy(fMakeCleanup(Location))`. The
/// arguments the factory's reference parameters, which the cleanup captures,
/// are bound to must be variables, not temporaries that are gone by then.
static bool isAsyncDestroyCleanup(const LambdaExpr *Lambda,
                                  ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Lambda);
  for (int Depth = 0; Depth < 8; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    if (Parents[0].get<CallExpr>())
      return isAsyncDestroyArgument(Node.get<Expr>(), Context);
    if (const auto *Variable = Parents[0].get<VarDecl>())
      return Variable->hasLocalStorage() && onlyAsyncDestroyed(Variable, Context);
    if (Parents[0].get<ReturnStmt>())
      break;
    if (!Parents[0].get<Expr>())
      return false;
    Node = Parents[0];
  }
  // Returned by a factory lambda held in a local, which isn't a coroutine.
  const auto *FactoryOperator =
      dyn_cast<CXXMethodDecl>(Lambda->getLambdaClass()->getDeclContext());
  if (!FactoryOperator || !FactoryOperator->getParent()->isLambda() ||
      isCoroutine(FactoryOperator))
    return false;
  const LambdaExpr *Factory = nullptr;
  for (DynTypedNode Up = DynTypedNode::create(*Lambda);;) {
    const auto Parents = instantiatedParents(Up, Context);
    if (Parents.empty())
      return false;
    Up = Parents[0];
    // Also of a generic one, whose call operator is a template that the
    // factory's is an instantiation of.
    if (const auto *Outer = Up.get<LambdaExpr>();
        Outer && Outer->getLambdaClass() == FactoryOperator->getParent()) {
      Factory = Outer;
      break;
    }
  }
  const auto FactoryParents = instantiatedParents(*Factory, Context);
  const auto *Holder =
      FactoryParents.empty() ? nullptr : FactoryParents[0].get<VarDecl>();
  const auto *Function =
      Holder ? dyn_cast_or_null<FunctionDecl>(Holder->getParentFunctionOrMethod())
             : nullptr;
  if (!Holder || !Holder->hasLocalStorage() || !Function || !Function->getBody())
    return false;
  // The factory's reference parameters that the cleanup captures.
  llvm::SmallVector<unsigned, 4> CapturedParameters;
  for (const LambdaCapture &Capture : Lambda->captures()) {
    if (const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(
            Capture.capturesVariable() ? Capture.getCapturedVar() : nullptr);
        Parameter && Parameter->getDeclContext() == FactoryOperator)
      CapturedParameters.push_back(Parameter->getFunctionScopeIndex());
  }
  const auto Refs =
      match(findAll(declRefExpr(to(varDecl(equalsNode(Holder)))).bind("ref")),
            *Function->getBody(), Context);
  return !Refs.empty() && llvm::all_of(Refs, [&](const BoundNodes &Nodes) {
    const auto *Ref = Nodes.getNodeAs<DeclRefExpr>("ref");
    const auto Parents = instantiatedParents(*Ref, Context);
    // What a lambda captures the factory with isn't a call of it; what its
    // body does with it is.
    if (!Parents.empty() && Parents[0].get<LambdaExpr>())
      return true;
    const auto *Call =
        Parents.empty() ? nullptr : Parents[0].get<CXXOperatorCallExpr>();
    if (!Call || Call->getOperator() != OO_Call ||
        Call->getArg(0)->IgnoreParenImpCasts() != Ref ||
        !isAsyncDestroyArgument(Call, Context))
      return false;
    return llvm::all_of(CapturedParameters, [&](unsigned iParameter) {
      if (iParameter + 1 >= Call->getNumArgs())
        return false;
      const auto *Argument = dyn_cast<DeclRefExpr>(
          Call->getArg(iParameter + 1)->IgnoreParenImpCasts());
      const auto *Variable =
          Argument ? dyn_cast<VarDecl>(Argument->getDecl()) : nullptr;
      return Variable && (Variable->hasLocalStorage() ||
                          Variable->hasGlobalStorage());
    });
  });
}

/// Whether what the pointer \p Pointer that \p Lambda captures points to
/// moves with \p Owner into the closure: the pointer is unchanged and what a
/// member function of it gives, `Owner.f_Get()` of `return m_pPointTo;`, and
/// the lambda has it moved into an init capture, `[pValue, Owner =
/// fg_Move(Owner)]`, whose move constructor takes that member of the other,
/// `m_pPointTo(_Other.m_pPointTo)`, so the closure owns what it points to.
static bool keptByMove(const LambdaExpr *Lambda, const ValueDecl *Pointer,
                       const VarDecl *Owner) {
  const auto *Copied = dyn_cast<VarDecl>(Pointer);
  if (!Copied || !Copied->getInit() || isChangedVariable(Copied) ||
      !Copied->getType()->isPointerType())
    return false;
  const auto *Get =
      dyn_cast<CXXMemberCallExpr>(Copied->getInit()->IgnoreParenImpCasts());
  const auto *Object =
      Get ? dyn_cast<DeclRefExpr>(
                Get->getImplicitObjectArgument()->IgnoreParenImpCasts())
          : nullptr;
  const CXXMethodDecl *Method = Get ? Get->getMethodDecl() : nullptr;
  const FunctionDecl *Definition = Method ? Method->getDefinition() : nullptr;
  if (!Object || Object->getDecl() != Owner || !Definition ||
      !Definition->getBody() || Method->isVirtual())
    return false;
  // The members from an object to what \p E names, outermost first, and the
  // object, `m_Data.m_pPointTo` of `this` or `_Other.m_Data.m_pPointTo` of
  // `_Other`.
  auto PathOf = [](const Expr *E, const Expr *&Base) {
    llvm::SmallVector<const FieldDecl *, 2> Path;
    E = E->IgnoreImplicit()->IgnoreParenImpCasts();
    // A copy of a member, `m_Data(_Other.m_Data)`.
    if (const auto *Copy = dyn_cast<CXXConstructExpr>(E);
        Copy && Copy->getNumArgs() == 1 &&
        Copy->getConstructor()->isCopyOrMoveConstructor())
      E = Copy->getArg(0)->IgnoreImplicit()->IgnoreParenImpCasts();
    while (const auto *Member = dyn_cast<MemberExpr>(E)) {
      const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
      if (!Field)
        return llvm::SmallVector<const FieldDecl *, 2>();
      Path.insert(Path.begin(), Field);
      E = Member->getBase()->IgnoreParenImpCasts();
      if (Member->isArrow())
        break;
    }
    Base = E;
    return Path;
  };
  // The pointer member it gives, also of a member, `m_Data.m_pPointTo`.
  llvm::SmallVector<const FieldDecl *, 2> Given;
  for (const Expr *Returned : returnedValues(Definition)) {
    const Expr *Base = nullptr;
    const llvm::SmallVector<const FieldDecl *, 2> Path = PathOf(Returned, Base);
    if (Path.empty() || !Path.back()->getType()->isPointerType() ||
        !isa_and_nonnull<CXXThisExpr>(Base) ||
        (!Given.empty() && Given != Path))
      return false;
    Given = Path;
  }
  if (Given.empty())
    return false;
  // Moved into the closure.
  bool Moved = false;
  for (const LambdaCapture &Capture : Lambda->captures()) {
    const auto *Init = Capture.capturesVariable()
                           ? dyn_cast<VarDecl>(Capture.getCapturedVar())
                           : nullptr;
    if (!Init || !Init->isInitCapture() || !Init->getInit())
      continue;
    const auto *Construct = dyn_cast<CXXConstructExpr>(
        Init->getInit()->IgnoreImplicit()->IgnoreParens());
    if (!Construct || Construct->getNumArgs() != 1 ||
        !Construct->getConstructor()->isMoveConstructor())
      continue;
    const Expr *From = Construct->getArg(0)->IgnoreParenImpCasts();
    if (const CallExpr *Move =
            asCallTo(From, vocabulary().MoveFunctions.names());
        Move && Move->getNumArgs() == 1)
      From = Move->getArg(0)->IgnoreParenImpCasts();
    else if (const auto *Cast = dyn_cast<CXXStaticCastExpr>(From);
             Cast && Cast->getTypeAsWritten()->isRValueReferenceType())
      From = Cast->getSubExpr()->IgnoreParenImpCasts();
    else
      continue;
    const auto *Ref = dyn_cast<DeclRefExpr>(From);
    if (!Ref || Ref->getDecl() != Owner)
      continue;
    // That takes the member of the other, where it initializes it,
    // `m_pPointTo(_Other.m_pPointTo)`, also the member it is in,
    // `m_Data(_Other.m_Data)`, or in its body, `m_Data.m_pPointTo =
    // _Other.m_Data.m_pPointTo;`, like `TCUniquePointer`.
    const CXXConstructorDecl *Constructor = Construct->getConstructor();
    const auto *MoveDefinition =
        dyn_cast_or_null<CXXConstructorDecl>(Constructor->getDefinition());
    if (!MoveDefinition)
      continue;
    if (MoveDefinition->isDefaulted()) {
      Moved = true;
      break;
    }
    const ParmVarDecl *Other = MoveDefinition->getParamDecl(0);
    auto IsOther = [&](const Expr *Base) {
      const auto *Named = dyn_cast_or_null<DeclRefExpr>(Base);
      return Named && Named->getDecl() == Other;
    };
    for (const CXXCtorInitializer *Initializer : MoveDefinition->inits()) {
      if (Initializer->getMember() != Given.front())
        continue;
      const Expr *Base = nullptr;
      const llvm::SmallVector<const FieldDecl *, 2> Taken =
          PathOf(Initializer->getInit(), Base);
      if (IsOther(Base) && !Taken.empty() &&
          (Taken == Given ||
           (Taken.size() == 1 && Taken.front() == Given.front())))
        Moved = true;
    }
    if (const auto *Body =
            dyn_cast_or_null<CompoundStmt>(MoveDefinition->getBody()))
      for (const Stmt *Statement : Body->body())
        if (const auto *Assign = dyn_cast<BinaryOperator>(Statement);
            Assign && Assign->getOpcode() == BO_Assign) {
          const Expr *To = nullptr, *Source = nullptr;
          if (PathOf(Assign->getLHS(), To) == Given &&
              isa_and_nonnull<CXXThisExpr>(To) &&
              PathOf(Assign->getRHS(), Source) == Given && IsOther(Source))
            Moved = true;
        }
    if (Moved)
      break;
  }
  return Moved;
}

/// Whether the destructor of \p Owner can release what its pointer member
/// \p Field points to: one of its own whose body uses it, also through the
/// member functions of the object it calls, `fp_Destroy()`, or one that can't
/// be seen, but not one the compiler makes, which only destroys the members,
/// none of which is a pointer, `TCSharedPointer<int> m_pOwned` next to `int
/// *m_pExternal`.
static bool mayReleaseMember(const CXXRecordDecl *Owner,
                             const FieldDecl *Field) {
  const CXXDestructorDecl *Destructor = Owner->getDestructor();
  if (!Destructor)
    return false;
  if (!Destructor->isUserProvided())
    return false;
  const FunctionDecl *Definition = Destructor->getDefinition();
  if (!Definition || !Definition->getBody())
    return true;
  llvm::SmallPtrSet<const FunctionDecl *, 8> Followed{Definition};
  std::function<bool(const Stmt *)> Uses = [&](const Stmt *S) {
    if (!S)
      return false;
    if (const auto *Member = dyn_cast<MemberExpr>(S);
        Member && Member->getMemberDecl() == Field)
      return true;
    // A member function of it, `fp_Destroy()`, or of a member of it,
    // `m_Data.f_Free()`, uses it where it does; one that can't be seen can.
    auto OfObject = [](const Expr *Object) {
      Object = Object->IgnoreParenImpCasts();
      while (const auto *Member = dyn_cast<MemberExpr>(Object)) {
        if (Member->isArrow() &&
            !isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()))
          return false;
        Object = Member->getBase()->IgnoreParenImpCasts();
      }
      return isa<CXXThisExpr>(Object);
    };
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(S);
        Call && Call->getImplicitObjectArgument() &&
        OfObject(Call->getImplicitObjectArgument())) {
      const CXXMethodDecl *Method = Call->getMethodDecl();
      const FunctionDecl *Called = Method ? Method->getDefinition() : nullptr;
      if (!Called || !Called->getBody() || Method->isVirtual())
        return true;
      if (Followed.size() < 64 && Followed.insert(Called).second &&
          Uses(Called->getBody()))
        return true;
    }
    return llvm::any_of(childrenOnce(S), Uses);
  };
  return Uses(Definition->getBody());
}

/// The local that a pointer \p Capture copies points into, like `Value` of
/// `[pValue = &Value]`, or of `[pValue]` with an unchanged `int *pValue =
/// &Value`: the lambda borrows it like `[&Value]`. Also a parameter by value,
/// which lives in the frame of a coroutine, but not what a reference
/// parameter refers to.
static const VarDecl *localPointedBy(const Expr *Value, bool Addressed = false);
static bool decidePointsToLocal(const VarDecl *Variable, int Depth);

static const VarDecl *pointedLocal(const LambdaExpr *Lambda,
                                   const LambdaCapture &Capture,
                                   const ASTContext &Context) {
  if (!Capture.capturesVariable() || Capture.getCaptureKind() != LCK_ByCopy)
    return nullptr;
  const auto *Captured = dyn_cast<VarDecl>(Capture.getCapturedVar());
  // Where the lambda copies it, which what a pointer has there is of.
  const auto CaptureIndex = &Capture - Lambda->capture_begin();
  const Expr *CaptureInit =
      CaptureIndex >= 0 &&
              static_cast<unsigned>(CaptureIndex) < Lambda->capture_size()
          ? Lambda->capture_init_begin()[CaptureIndex]
          : nullptr;
  const auto *CaptureRef =
      CaptureInit ? dyn_cast<DeclRefExpr>(
                        skipWrappers(CaptureInit)->IgnoreParenImpCasts())
                  : nullptr;
  // A local reference to a pointer, `int *&pAlias = pValue`, captured by copy
  // copies the pointer it refers to, what it has where the lambda copies it.
  const DeclRefExpr *Bound = nullptr;
  for (int Depth = 0;
       Depth < 1024 && Captured && Captured->getType()->isReferenceType() &&
       Captured->hasLocalStorage() && !isa<ParmVarDecl>(Captured) &&
       Captured->getInit();
       ++Depth) {
    const Expr *Init = Captured->getInit()->IgnoreParenImpCasts();
    // Also through a cast to a reference, `static_cast<int *&>(pValue)`, the
    // last of a comma, an assignment, `(pOther = pValue)`, which refers to
    // what it assigns, or a helper that gives back what it is given,
    // `fg_Identity(pValue)`.
    for (int Step = 0; Step < 16; ++Step) {
      const auto *Binary = dyn_cast<BinaryOperator>(Init);
      const auto *Call = dyn_cast<CallExpr>(Init);
      if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Init);
          Cast && Cast->isGLValue())
        Init = Cast->getSubExpr();
      else if (Binary && Binary->getOpcode() == BO_Comma)
        Init = Binary->getRHS();
      else if (Binary && Binary->isAssignmentOp())
        Init = Binary->getLHS();
      else if (Call && Call->isGLValue() && returnedArguments(Call).size() == 1)
        Init = returnedArguments(Call).front();
      else
        break;
      Init = Init->IgnoreParenImpCasts();
    }
    // Bound to either of a conditional, `int *&pAlias = bChoose ? pFirst :
    // pSecond`: a local either points to where the lambda copies it, the
    // one declared last, which goes first, of both.
    // Only the one a constant condition chooses, `true ? pSafe : pLocal`,
    // which is bound as it is.
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Init)) {
      bool Chosen = false;
      if (!Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(Chosen, Context)) {
        Init =
            (Chosen ? Conditional->getTrueExpr() : Conditional->getFalseExpr())
                ->IgnoreParenImpCasts();
        Bound = dyn_cast<DeclRefExpr>(Init);
        Captured = Bound ? dyn_cast<VarDecl>(Bound->getDecl()) : nullptr;
        continue;
      }
    }
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Init)) {
      const VarDecl *Pointed = nullptr;
      for (const Expr *Arm :
           {Conditional->getTrueExpr(), Conditional->getFalseExpr()}) {
        const auto *ArmRef = dyn_cast<DeclRefExpr>(Arm->IgnoreParenImpCasts());
        // Also through local references to it, `int *&pFirst = pValue`.
        for (int Level = 0; ArmRef && Level < 64; ++Level) {
          const auto *Alias = dyn_cast<VarDecl>(ArmRef->getDecl());
          if (!Alias || !Alias->getType()->isReferenceType() ||
              !Alias->hasLocalStorage() || isa<ParmVarDecl>(Alias) ||
              !Alias->getInit())
            break;
          ArmRef =
              dyn_cast<DeclRefExpr>(Alias->getInit()->IgnoreParenImpCasts());
        }
        const auto *Pointer =
            ArmRef ? dyn_cast<VarDecl>(ArmRef->getDecl()) : nullptr;
        if (!Pointer || !Pointer->getType()->isPointerType() ||
            !Pointer->hasLocalStorage() || isa<ParmVarDecl>(Pointer))
          continue;
        const VarDecl *Local = localPointedBy(
            assignedValueAt(Pointer, CaptureRef ? CaptureRef : ArmRef));
        if (Local &&
            (!Pointed || Context.getSourceManager().isBeforeInTranslationUnit(
                             Pointed->getLocation(), Local->getLocation())))
          Pointed = Local;
      }
      return Pointed;
    }
    Bound = dyn_cast<DeclRefExpr>(Init);
    Captured = Bound ? dyn_cast<VarDecl>(Bound->getDecl()) : nullptr;
  }
  const bool View =
      Captured && isDefaultViewType(Captured->getType(), Context);
  if (!Captured || (!Captured->getType()->isPointerType() && !View))
    return nullptr;
  const Expr *Value = nullptr;
  // What the pointer has where the lambda copies it through the reference,
  // not where the reference is bound to it.
  if (Bound && Captured->hasLocalStorage() && !isa<ParmVarDecl>(Captured))
    Value = assignedValueAt(Captured, CaptureRef ? CaptureRef : Bound);
  else if (Captured->isInitCapture() ||
           (Captured->hasLocalStorage() && !isa<ParmVarDecl>(Captured) &&
            !isChangedVariable(Captured)))
    Value = Captured->getInit();
  // One that is changed has what it was last assigned where the lambda
  // captures it, `p = &Value; [p] { ... }`.
  else if (Captured->hasLocalStorage() && !isa<ParmVarDecl>(Captured)) {
    const auto Index = &Capture - Lambda->capture_begin();
    const Expr *Init =
        Index >= 0 && static_cast<unsigned>(Index) < Lambda->capture_size()
            ? Lambda->capture_init_begin()[Index]
            : nullptr;
    if (const auto *Ref = Init ? dyn_cast<DeclRefExpr>(
                                     skipWrappers(Init)->IgnoreParenImpCasts())
                               : nullptr;
        Ref && Ref->getDecl() == Captured)
      Value = assignedValueAt(Captured, Ref);
  }
  // The calls given it by a reference that isn't const, which can point it
  // elsewhere, `fg_Set(pValue, Value)`, what it holds can't be told by its
  // assignments only.
  llvm::SmallVector<std::pair<const CallExpr *, unsigned>, 2> GivenTo;
  if (Captured->hasLocalStorage() && !isa<ParmVarDecl>(Captured) &&
      !Captured->isInitCapture())
    if (const auto *Function = dyn_cast_or_null<FunctionDecl>(
            Captured->getParentFunctionOrMethod());
        Function && Function->getBody()) {
      // Also through a local reference to it, `int *&pAlias = pValue`.
      auto NamesCaptured = [&](const Expr *Argument) {
        const auto *Ref =
            dyn_cast<DeclRefExpr>(Argument->IgnoreParenImpCasts());
        for (int Level = 0; Ref && Level < 64; ++Level) {
          if (Ref->getDecl() == Captured)
            return true;
          const auto *Alias = dyn_cast<VarDecl>(Ref->getDecl());
          if (!Alias || !Alias->getType()->isReferenceType() ||
              !Alias->hasLocalStorage() || isa<ParmVarDecl>(Alias) ||
              !Alias->getInit())
            return false;
          Ref = dyn_cast<DeclRefExpr>(Alias->getInit()->IgnoreParenImpCasts());
        }
        return false;
      };
      // Also of a local lambda, `fSet(pValue, Value)`, whose call operator
      // gets the lambda first.
      std::function<void(const Stmt *)> FindCalls = [&](const Stmt *S) {
        if (!S || isa<LambdaExpr>(S))
          return;
        if (const auto *Call = dyn_cast<CallExpr>(S);
            Call && (!isa<CXXOperatorCallExpr>(Call) ||
                     cast<CXXOperatorCallExpr>(Call)->getOperator() == OO_Call))
          for (unsigned iArgument = 0; iArgument < Call->getNumArgs();
               ++iArgument) {
            // Also its address, `fg_Set(&pValue, Value)` of `int **`.
            const auto *Address = dyn_cast<UnaryOperator>(
                Call->getArg(iArgument)->IgnoreParenImpCasts());
            if ((NamesCaptured(Call->getArg(iArgument)) &&
                 mayAssignArgument(Call, iArgument)) ||
                (Address && Address->getOpcode() == UO_AddrOf &&
                 NamesCaptured(Address->getSubExpr()) &&
                 !Address->getType()->getPointeeType().isConstQualified()))
              GivenTo.push_back({Call, iArgument});
          }
        for (const Stmt *Child : childrenOnce(S))
          FindCalls(Child);
      };
      FindCalls(Function->getBody());
      // What it is assigned after them, `fg_Set(pValue, Value); pValue =
      // &g_Value;`, it holds, without jumps that can go back before it.
      const SourceManager &SM = Context.getSourceManager();
      if (!GivenTo.empty() &&
          (!Value || hasJumps(Function->getBody()) ||
           llvm::any_of(
               GivenTo,
               [&](const std::pair<const CallExpr *, unsigned> &Entry) {
                 return !SM.isBeforeInTranslationUnit(Entry.first->getEndLoc(),
                                                      Value->getBeginLoc());
               })))
        Value = nullptr;
    }
  // A view, like `CStrSpan{Buffer, 8}`, refers to what the pointers it is
  // made of point to.
  auto ViewLocal = [&](const Expr *Made) -> const VarDecl * {
    // The local that what it is made of is the initializer of, whose
    // temporaries it extends, like the array of a `std::initializer_list`,
    // also an init capture, `[Values = {1, 2}]`, whose array the closure it
    // is made in extends, not a copy of it, which what it is given to keeps.
    const VarDecl *Owner = Captured->hasLocalStorage() &&
                                   !isa<ParmVarDecl>(Captured) &&
                                   Made == Captured->getInit()
                               ? Captured
                               : nullptr;
    // Also a copy of another, `CStrSpan Copy = Span`, which refers where it
    // does, through a chain of them, what each has where it is copied.
    Made = skipWrappers(Made)->IgnoreParenImpCasts();
    for (int Depth = 0; Depth < 32; ++Depth) {
      if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(Made))
        Made = Materialize->getSubExpr()->IgnoreParenImpCasts();
      else if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(Made))
        Made = Bind->getSubExpr()->IgnoreParenImpCasts();
      else if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Made))
        Made = Cast->getSubExpr()->IgnoreParenImpCasts();
      else if (const auto *Copy = dyn_cast<CXXConstructExpr>(Made);
               isa<DeclRefExpr>(Made) ||
               (Copy && Copy->getNumArgs() == 1 &&
                Copy->getConstructor()->isCopyOrMoveConstructor())) {
        const auto *Ref = dyn_cast<DeclRefExpr>(
            Copy ? Copy->getArg(0)->IgnoreParenImpCasts() : Made);
        const auto *Source = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        if (!Source || !Source->hasLocalStorage() || isa<ParmVarDecl>(Source))
          break;
        const Expr *Assigned = isChangedVariable(Source)
                                   ? assignedValueAt(Source, Ref)
                                   : Source->getInit();
        if (!Assigned)
          break;
        Owner = Assigned == Source->getInit() ? Source : nullptr;
        Made = skipWrappers(Assigned)->IgnoreParenImpCasts();
      } else
        break;
    }
    // The array of a `std::initializer_list`, `std::initializer_list<int>
    // Values{1, 2}`, which lives as long as the local it initializes.
    if (isa<CXXStdInitializerListExpr>(Made))
      return Owner;
    llvm::SmallVector<const Expr *, 4> Parts;
    if (const auto *List = dyn_cast<InitListExpr>(Made))
      Parts.append(List->inits().begin(), List->inits().end());
    else if (const auto *Construct = dyn_cast<CXXConstructExpr>(Made))
      Parts.append(Construct->arg_begin(), Construct->arg_end());
    for (const Expr *Part : Parts)
      if (!Part->getType()->isIntegralOrEnumerationType())
        if (const VarDecl *Local = localPointedBy(Part))
          return Local;
    return nullptr;
  };
  // A pointer or view whose value where it is captured can't be told, like
  // one assigned in a branch, `if (bChoose) pValue = &Local;`, can have any
  // of the values it is made from or assigned.
  if (!Value && Captured->hasLocalStorage() && !isa<ParmVarDecl>(Captured) &&
      !Captured->isInitCapture()) {
    llvm::SmallVector<const Expr *, 4> Values;
    if (Captured->getInit())
      Values.push_back(Captured->getInit());
    const auto *Function = dyn_cast_or_null<FunctionDecl>(
        Captured->getParentFunctionOrMethod());
    // Not what is assigned after the lambda copies it, which changes the
    // variable, not the copy, unless a loop around the lambda runs the
    // assignment before it again, or a jump can.
    const SourceManager &SM = Context.getSourceManager();
    llvm::SmallVector<SourceRange, 2> Loops;
    for (DynTypedNode Node = DynTypedNode::create(*Lambda);;) {
      const auto Parents =
          instantiatedParents(Node, const_cast<ASTContext &>(Context));
      if (Parents.empty() || Parents[0].get<FunctionDecl>() ||
          Parents[0].get<LambdaExpr>())
        break;
      Node = Parents[0];
      if (Node.get<ForStmt>() || Node.get<WhileStmt>() || Node.get<DoStmt>() ||
          Node.get<CXXForRangeStmt>())
        Loops.push_back(Node.getSourceRange());
    }
    const bool Jumps = Function && Function->getBody() &&
                       hasJumps(Function->getBody());
    auto BeforeCopy = [&](const Stmt *Assign) {
      if (Jumps ||
          !SM.isBeforeInTranslationUnit(Lambda->getEndLoc(),
                                        Assign->getBeginLoc()))
        return true;
      return llvm::any_of(Loops, [&](SourceRange Loop) {
        return SM.isBeforeInTranslationUnit(Assign->getBeginLoc(),
                                            Loop.getEnd());
      });
    };
    std::function<void(const Stmt *)> Assignments = [&](const Stmt *S) {
      if (!S)
        return;
      if (const auto *Assign = dyn_cast<BinaryOperator>(S);
          Assign && Assign->getOpcode() == BO_Assign && BeforeCopy(Assign))
        if (const auto *Target = dyn_cast<DeclRefExpr>(
                Assign->getLHS()->IgnoreParenImpCasts());
            Target && Target->getDecl() == Captured)
          Values.push_back(Assign->getRHS());
      // Also of a view, `View = CStrSpan(Local)`.
      if (const auto *Assign = dyn_cast<CXXOperatorCallExpr>(S);
          Assign && Assign->getOperator() == OO_Equal &&
          Assign->getNumArgs() == 2 && BeforeCopy(Assign))
        if (const auto *Target = dyn_cast<DeclRefExpr>(
                Assign->getArg(0)->IgnoreParenImpCasts());
            Target && Target->getDecl() == Captured)
          Values.push_back(Assign->getArg(1));
      for (const Stmt *Child : childrenOnce(S))
        Assignments(Child);
    };
    if (Function)
      Assignments(Function->getBody());
    // What a function it is given to whose body can be seen assigns it of
    // what it is given, `void fg_Set(int *&o_pValue, int &_Value) { o_pValue
    // = &_Value; }`: the argument, or its address.
    llvm::SmallVector<const Expr *, 2> AddressedValues;
    for (const std::pair<const CallExpr *, unsigned> &Entry : GivenTo) {
      const CallExpr *Call = Entry.first;
      const unsigned iArgument = Entry.second;
      const FunctionDecl *Callee =
          Call->getDirectCallee() ? Call->getDirectCallee()->getDefinition()
                                  : nullptr;
      // The call operator of a lambda gets the lambda as its first argument.
      const unsigned Offset =
          isa<CXXOperatorCallExpr>(Call) && Callee && hasImplicitObject(Callee)
              ? 1
              : 0;
      if (!BeforeCopy(Call) || !Callee || !Callee->getBody() ||
          iArgument < Offset || iArgument - Offset >= Callee->getNumParams())
        continue;
      // Also through another it gives it to, `fg_Set(o_pValue, _Value)`.
      for (const CSetParameter &Set : setParameters(Callee))
        if (Set.Target == iArgument - Offset &&
            Set.Source + Offset < Call->getNumArgs()) {
          if (Set.Addressed)
            AddressedValues.push_back(Call->getArg(Set.Source + Offset));
          else
            Values.push_back(Call->getArg(Set.Source + Offset));
        }
    }
    // Of all, the one declared last, which goes first: each must outlive
    // what the lambda needs it for.
    const VarDecl *Latest = nullptr;
    auto Consider = [&](const VarDecl *Local) {
      if (Local &&
          (!Latest || SM.isBeforeInTranslationUnit(Latest->getLocation(),
                                                   Local->getLocation())))
        Latest = Local;
    };
    for (const Expr *Assigned : Values)
      Consider(View ? ViewLocal(Assigned) : localPointedBy(Assigned));
    if (!View)
      for (const Expr *Argument : AddressedValues)
        Consider(localPointedBy(Argument, /*Addressed=*/true));
    return Latest;
  }
  if (!Value)
    return nullptr;
  if (View)
    return ViewLocal(Value);
  return localPointedBy(Value);
}

/// What \p Local, a local pointer or reference that is followed, points to
/// or is bound to, like `localPointedBy` of its initializer. Each local is
/// followed once while the outermost question is answered, so that locals
/// made from others many times, `pB = bChoose ? pA : pA`, don't make it
/// exponential.
static const VarDecl *pointedThroughLocal(const VarDecl *Local,
                                          bool Addressed) {
  using CKey = std::pair<const VarDecl *, unsigned>;
  struct CCache {
    llvm::DenseMap<CKey, const VarDecl *> Results;
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
  const CKey Key{Local, Addressed};
  if (auto It = s_pCache->Results.find(Key); It != s_pCache->Results.end())
    return It->second;
  // One that is made from itself, through others, points nowhere more.
  if (!s_pCache->InProgress.insert(Key).second)
    return nullptr;
  const VarDecl *Result = localPointedBy(Local->getInit(), Addressed);
  s_pCache->InProgress.erase(Key);
  s_pCache->Results.try_emplace(Key, Result);
  return Result;
}

/// The local that the pointer \p Value points into: `&Value`, a local array
/// it decays from, or what an unchanged local pointer or reference it is
/// made from points to or is bound to, also either value of a conditional.
/// \p Addressed tells whether the value is the address of what it is made of.
/// Whether \p Method, whose body can be seen, gives back only the address of
/// what is in its own object, `return &m_Value;`.
static bool returnsOwnAddress(const CXXMethodDecl *Method) {
  const FunctionDecl *Definition = Method ? Method->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody())
    return false;
  bool Returns = false;
  bool Other = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Other || isa<LambdaExpr>(S))
      return;
    if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
      const auto *Address =
          Return->getRetValue()
              ? dyn_cast<UnaryOperator>(
                    Return->getRetValue()->IgnoreParenImpCasts())
              : nullptr;
      const Expr *Object = Address && Address->getOpcode() == UO_AddrOf
                               ? Address->getSubExpr()->IgnoreParenImpCasts()
                               : nullptr;
      while (const auto *Member = dyn_cast_or_null<MemberExpr>(Object)) {
        const Expr *Base = Member->getBase()->IgnoreParenImpCasts();
        if (Member->isArrow() && !isa<CXXThisExpr>(Base)) {
          Object = nullptr;
          break;
        }
        Object = Base;
      }
      if (isa_and_nonnull<CXXThisExpr>(Object))
        Returns = true;
      else
        Other = true;
      return;
    }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Definition->getBody());
  return Returns && !Other;
}

/// What `pointsToLocal` decided in the translation unit, each variable once,
/// which the check clears when the next one starts: references bound to
/// conditionals of others, `int *&pB = bChoose ? pA : pA`, would otherwise be
/// followed a number of times that doubles with each.
static thread_local llvm::DenseMap<const ValueDecl *, bool> s_PointsToLocal;

/// Whether the local pointer \p Variable can point to a local, which is gone
/// with it: one that it is made from, or any when it is changed. Through
/// more references than can be followed, it can.
static bool pointsToLocal(const ValueDecl *Captured, int Depth = 0) {
  const auto *Variable = dyn_cast<VarDecl>(Captured);
  if (!Variable)
    return false;
  if (Depth > 1024)
    return true;
  if (auto It = s_PointsToLocal.find(Variable); It != s_PointsToLocal.end())
    return It->second;
  // One that reaches itself, `int *&pValue = pValue`, can.
  s_PointsToLocal[Variable] = true;
  const bool Points = decidePointsToLocal(Variable, Depth);
  s_PointsToLocal[Variable] = Points;
  return Points;
}

static bool decidePointsToLocal(const VarDecl *Variable, int Depth) {
  // A local reference to a pointer, `int *&pAlias = pValue`, also to either
  // of a conditional, refers to the pointer it is bound to, which can point
  // to a local.
  if (Variable->getType()->isReferenceType() &&
      Variable->getType().getNonReferenceType()->isPointerType() &&
      Variable->hasLocalStorage() && !isa<ParmVarDecl>(Variable) &&
      Variable->getInit()) {
    std::function<bool(const Expr *)> Bound = [&](const Expr *Init) {
      Init = Init->IgnoreParenImpCasts();
      if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Init))
        return Bound(Conditional->getTrueExpr()) ||
               Bound(Conditional->getFalseExpr());
      const auto *Ref = dyn_cast<DeclRefExpr>(Init);
      return Ref && pointsToLocal(Ref->getDecl(), Depth + 1);
    };
    return Bound(Variable->getInit());
  }
  if (!Variable->getType()->isPointerType())
    return false;
  if (isChangedVariable(Variable) || !Variable->getInit())
    return true;
  return localPointedBy(Variable->getInit()) != nullptr;
}

static const VarDecl *decideLocalPointedBy(const Expr *Value, bool Addressed);

/// What `localPointedBy` decided in the translation unit, which the check
/// clears when the next one starts.
static thread_local llvm::DenseMap<std::pair<const Expr *, unsigned>,
                                   const VarDecl *>
    s_LocalPointedBy;

/// Like `decideLocalPointedBy`, each expression once in a translation unit:
/// copies of pointers that are assigned conditionals of others, `p2 = bChoose
/// ? p1 : p1`, would otherwise be followed a number of times that doubles
/// with each.
static const VarDecl *localPointedBy(const Expr *Value, bool Addressed) {
  if (!Value)
    return nullptr;
  if (const auto It = s_LocalPointedBy.find({Value, unsigned(Addressed)});
      It != s_LocalPointedBy.end())
    return It->second;
  const VarDecl *Pointed = decideLocalPointedBy(Value, Addressed);
  s_LocalPointedBy[{Value, unsigned(Addressed)}] = Pointed;
  return Pointed;
}

static const VarDecl *decideLocalPointedBy(const Expr *Value, bool Addressed) {
  // Of two, the one declared last, which goes first: both must outlive what
  // the lambda needs them for.
  auto Later = [](const VarDecl *First, const VarDecl *Second) {
    if (!First || !Second)
      return First ? First : Second;
    return First->getASTContext().getSourceManager().isBeforeInTranslationUnit(
               First->getLocation(), Second->getLocation())
               ? Second
               : First;
  };
  // Through as many steps as code has, each of which goes into what it is
  // made of, like a member of a member, `&Local.m_Child.m_Child.m_Value`.
  for (int Depth = 0; Value && Depth < 1024; ++Depth) {
    Value = skipWrappers(Value)->IgnoreParenImpCasts();
    // The condition that is the first value of `pValue ?: &g_Value`.
    if (const auto *Opaque = dyn_cast<OpaqueValueExpr>(Value);
        Opaque && Opaque->getSourceExpr()) {
      Value = Opaque->getSourceExpr();
      continue;
    }
    // Of either of a conditional, or the one a constant condition selects,
    // `false ? &Local : &g_Value`, told with the context of a declaration it
    // names.
    if (const auto *Conditional =
            dyn_cast<AbstractConditionalOperator>(Value)) {
      const Decl *Named = nullptr;
      std::function<void(const Stmt *)> FindNamed = [&](const Stmt *S) {
        if (!S || Named)
          return;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
          Named = Ref->getDecl();
        for (const Stmt *Child : childrenOnce(S))
          FindNamed(Child);
      };
      FindNamed(Conditional);
      bool Selected = false;
      if (Named && !Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(
              Selected, Named->getASTContext())) {
        Value =
            Selected ? Conditional->getTrueExpr() : Conditional->getFalseExpr();
        continue;
      }
      return Later(localPointedBy(Conditional->getTrueExpr(), Addressed),
                   localPointedBy(Conditional->getFalseExpr(), Addressed));
    }
    if (const auto *Unary = dyn_cast<UnaryOperator>(Value);
        Unary && Unary->getOpcode() == UO_AddrOf) {
      Value = Unary->getSubExpr();
      Addressed = true;
      continue;
    }
    // Also `std::addressof(Value)`, `fg_AddressOf(Value)` or
    // `__builtin_addressof(Value)`.
    if (const auto *Call = dyn_cast<CallExpr>(Value); isAddressOfCall(Call)) {
      Value = Call->getArg(0);
      Addressed = true;
      continue;
    }
    // A pointer moved or forwarded, `fg_Move(pValue)`, or given a unary plus,
    // `+pValue`, which points where it did.
    if (const auto *Call = dyn_cast<CallExpr>(Value);
        Call && Call->getNumArgs() == 1 && Call->getType()->isPointerType() &&
        isMoveOrForwardFunction(calleeName(Call))) {
      // What a local pointer holds where it is moved, which the move, by
      // reference, doesn't change.
      const auto *Ref =
          dyn_cast<DeclRefExpr>(Call->getArg(0)->IgnoreParenImpCasts());
      const auto *Moved = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      const Expr *Held = Moved && Moved->hasLocalStorage() &&
                                 !isa<ParmVarDecl>(Moved) &&
                                 Moved->getType()->isPointerType()
                             ? assignedValueAt(Moved, Ref)
                             : nullptr;
      Value = Held ? Held : Call->getArg(0);
      continue;
    }
    // The last of a comma, `(fg_Log(), &Value)`, and the value of a statement
    // expression, `({ &Value; })`.
    if (const auto *Comma = dyn_cast<BinaryOperator>(Value);
        Comma && Comma->getOpcode() == BO_Comma) {
      Value = Comma->getRHS();
      continue;
    }
    if (const auto *Statement = dyn_cast<StmtExpr>(Value)) {
      Value = Statement->getSubStmt()->body_empty()
                  ? nullptr
                  : dyn_cast<Expr>(Statement->getSubStmt()->body_back());
      continue;
    }
    // What a lambda called in place returns, `[&] { return &Value; }()`, of
    // any of its returns.
    // Of a parameter it is given by reference, `[](int &_Value) { return
    // &_Value; }(Value)`, what the argument refers to.
    if (const CXXMethodDecl *Operator = calledInPlaceLambda(Value)) {
      const auto *InPlace = dyn_cast<CallExpr>(Value);
      const unsigned Offset =
          InPlace && (isa<CXXOperatorCallExpr>(InPlace) || isStdInvoke(InPlace))
              ? 1
              : 0;
      const VarDecl *Pointed = nullptr;
      for (const Expr *Returned : returnedValues(Operator)) {
        const VarDecl *Of = localPointedBy(Returned, Addressed);
        if (const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Of);
            Parameter && InPlace && Parameter->getDeclContext() == Operator &&
            Parameter->getType()->isReferenceType()) {
          const unsigned iArgument =
              Parameter->getFunctionScopeIndex() + Offset;
          Of = iArgument < InPlace->getNumArgs()
                   ? localPointedBy(InPlace->getArg(iArgument),
                                    /*Addressed=*/true)
                   : nullptr;
        }
        Pointed = Later(Pointed, Of);
      }
      return Pointed;
    }
    // What a member function of a local returns of the object, `Local.f_Get()`
    // of `int *f_Get() { return &m_Value; }`, which points into the local,
    // also the address of what it returns a reference to, `&Local.f_Get()` of
    // `int &f_Get() { return m_Value; }`. Also a member operator,
    // `&Values[0]` of `t_CValue &operator[](int _Index) { return
    // m_Values[_Index]; }`.
    const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(Value);
    const auto *OperatorCall = dyn_cast<CXXOperatorCallExpr>(Value);
    const auto *Operator =
        OperatorCall && OperatorCall->getNumArgs() >= 1
            ? dyn_cast_or_null<CXXMethodDecl>(OperatorCall->getDirectCallee())
            : nullptr;
    if (Operator && !Operator->isImplicitObjectMemberFunction())
      Operator = nullptr;
    const auto *Call = MemberCall ? cast<CallExpr>(MemberCall)
                       : Operator ? cast<CallExpr>(OperatorCall)
                                  : nullptr;
    const Expr *Object = MemberCall ? MemberCall->getImplicitObjectArgument()
                         : Operator ? OperatorCall->getArg(0)
                                    : nullptr;
    if (Call &&
        (Addressed ? Call->isLValue() : Call->getType()->isPointerType()) &&
        Object && (Operator || MemberCall->getMethodDecl())) {
      const bool ByReference = Addressed;
      const auto *Callee =
          MemberCall
              ? dyn_cast<MemberExpr>(Call->getCallee()->IgnoreParenImpCasts())
              : nullptr;
      const bool Arrow = Callee && Callee->isArrow();
      bool OfObject = false;
      // Of a virtual one, each override that the call can run, `Base.f_Get()`
      // of a `CDerived` whose `f_Get()` returns its member.
      llvm::SmallVector<const CXXMethodDecl *, 4> Methods;
      if (MemberCall)
        Methods = dispatchedMethods(MemberCall);
      else
        Methods.push_back(Operator);
      for (const CXXMethodDecl *Method : Methods)
        if (const FunctionDecl *Definition = Method->getDefinition();
            Definition && Definition->getBody())
          for (const Expr *Given : returnedValues(Definition)) {
            // Also either value of a conditional, `bFirst ? &m_A : &m_B`,
            // moved along, `m_Values + 1`, or cast, which stay in what they
            // point into.
            // Each with the member function whose body has it.
            llvm::SmallVector<std::pair<const Expr *, const CXXMethodDecl *>, 2>
                Pending{{Given, Method}};
            llvm::SmallPtrSet<const Expr *, 4> Seen;
            while (!Pending.empty()) {
              const std::pair<const Expr *, const CXXMethodDecl *> Entry =
                  Pending.pop_back_val();
              const CXXMethodDecl *In = Entry.second;
              const Expr *Returned = Entry.first->IgnoreParenImpCasts();
              if (!Seen.insert(Returned).second)
                continue;
              // Also what another getter of the object gives back, `return
              // fp_Get();`, of each override it can run, also of a member
              // that is an object, `return m_Values.f_GetArray();`, which is
              // of the object too.
              const Expr *Receiver = isa<CXXMemberCallExpr>(Returned)
                                         ? cast<CXXMemberCallExpr>(Returned)
                                               ->getImplicitObjectArgument()
                                         : nullptr;
              for (int Level = 0; Receiver && Level < 16; ++Level) {
                Receiver = Receiver->IgnoreParenImpCasts();
                const auto *Field = dyn_cast<MemberExpr>(Receiver);
                if (!Field ||
                    (Field->isArrow() &&
                     !isa<CXXThisExpr>(
                         Field->getBase()->IgnoreParenImpCasts())) ||
                    !isa<FieldDecl>(Field->getMemberDecl()) ||
                    Field->getMemberDecl()->getType()->isReferenceType())
                  break;
                Receiver = Field->getBase();
              }
              if (const auto *Inner = dyn_cast<CXXMemberCallExpr>(Returned);
                  Inner && Receiver && isa<CXXThisExpr>(Receiver) &&
                  Seen.size() < 32) {
                for (const CXXMethodDecl *Called : dispatchedMethods(Inner))
                  if (const FunctionDecl *CalledDefinition =
                          Called->getDefinition();
                      CalledDefinition && CalledDefinition->getBody())
                    for (const Expr *Value : returnedValues(CalledDefinition))
                      Pending.push_back({Value, Called});
                continue;
              }
              if (const auto *Conditional =
                      dyn_cast<AbstractConditionalOperator>(Returned)) {
                Pending.push_back({Conditional->getTrueExpr(), In});
                Pending.push_back({Conditional->getFalseExpr(), In});
                continue;
              }
              if (const auto *Offset = dyn_cast<BinaryOperator>(Returned);
                  Offset && Offset->isAdditiveOp() &&
                  Offset->getType()->isPointerType()) {
                Pending.push_back({Offset->getLHS()->getType()->isPointerType()
                                       ? Offset->getLHS()
                                       : Offset->getRHS(),
                                   In});
                continue;
              }
              if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Returned);
                  Cast && Cast->getSubExpr()->getType()->isPointerType()) {
                Pending.push_back({Cast->getSubExpr(), In});
                continue;
              }
              // What a pointer member of an object that releases what it
              // holds when it is destroyed points to, `return m_pValue;` of a
              // unique pointer, or into, `return &m_pData[_Index];` or a
              // reference to it, `return m_pData[_Index];`, of a vector, is
              // of storage that the object owns.
              // Also of a member of it that is a struct, `return
              // m_Data.m_pPointTo;` of `TCUniquePointer`.
              auto Owned = [&](const Expr *Pointer) {
                const auto *Member =
                    dyn_cast<MemberExpr>(Pointer->IgnoreParenImpCasts());
                const CXXRecordDecl *Owner = In->getParent();
                if (!Member || !isa<FieldDecl>(Member->getMemberDecl()) ||
                    !Member->getType()->isPointerType() || !Owner ||
                    !Owner->hasDefinition() ||
                    Owner->getDefinition()->hasTrivialDestructor() ||
                    !mayReleaseMember(Owner->getDefinition(),
                                      cast<FieldDecl>(Member->getMemberDecl())))
                  return false;
                const Expr *Base = Member->getBase()->IgnoreParenImpCasts();
                for (int Level = 0; Level < 8; ++Level) {
                  const auto *Inner = dyn_cast<MemberExpr>(Base);
                  if (!Inner ||
                      (Inner->isArrow() &&
                       !isa<CXXThisExpr>(
                           Inner->getBase()->IgnoreParenImpCasts())) ||
                      !isa<FieldDecl>(Inner->getMemberDecl()) ||
                      Inner->getMemberDecl()->getType()->isReferenceType())
                    break;
                  Base = Inner->getBase()->IgnoreParenImpCasts();
                }
                return isa<CXXThisExpr>(Base);
              };
              {
                const Expr *Pointee = Returned;
                if (const auto *AddressOf = dyn_cast<UnaryOperator>(Pointee);
                    AddressOf && AddressOf->getOpcode() == UO_AddrOf)
                  Pointee = AddressOf->getSubExpr()->IgnoreParenImpCasts();
                if (const auto *Element = dyn_cast<ArraySubscriptExpr>(Pointee);
                    Element && (Pointee != Returned || ByReference)) {
                  if (Owned(Element->getBase()))
                    OfObject = true;
                } else if (const auto *Deref = dyn_cast<UnaryOperator>(Pointee);
                           Deref && Deref->getOpcode() == UO_Deref &&
                           ByReference && Pointee == Returned) {
                  if (Owned(Deref->getSubExpr()))
                    OfObject = true;
                } else if (Pointee == Returned && !ByReference &&
                           Owned(Returned)) {
                  OfObject = true;
                }
              }
              // `return &m_Value;`, `return m_Values;` of an array, or `return
              // this;`, also of a member of a member.
              const Expr *Part = Returned->IgnoreParenImpCasts();
              bool Address = false;
              if (const auto *AddressOf = dyn_cast<UnaryOperator>(Part);
                  AddressOf && AddressOf->getOpcode() == UO_AddrOf) {
                Part = AddressOf->getSubExpr()->IgnoreParenImpCasts();
                Address = true;
              }
              // Also an element of an array member, `return &m_Values[0];`,
              // or a reference to one, `return m_Values[_Index];`.
              if (const auto *Element = dyn_cast<ArraySubscriptExpr>(Part);
                  Element && (Address || ByReference) &&
                  Element->getBase()
                      ->IgnoreParenImpCasts()
                      ->getType()
                      ->isArrayType())
                Part = Element->getBase()->IgnoreParenImpCasts();
              // Also through a local reference to it, `auto &Value =
              // m_Value; return &Value;`, which can't be bound elsewhere.
              for (int Level = 0; Level < 8; ++Level) {
                const auto *Alias = dyn_cast<DeclRefExpr>(Part);
                const auto *Local =
                    Alias ? dyn_cast<VarDecl>(Alias->getDecl()) : nullptr;
                if (!Local || !Local->hasLocalStorage() ||
                    isa<ParmVarDecl>(Local) ||
                    !Local->getType()->isReferenceType() || !Local->getInit())
                  break;
                Part = Local->getInit()->IgnoreParenImpCasts();
              }
              const bool Array = !Address && Part->getType()->isArrayType();
              for (int Level = 0; Level < 16; ++Level) {
                const auto *Member = dyn_cast<MemberExpr>(Part);
                if (!Member || !isa<FieldDecl>(Member->getMemberDecl()))
                  break;
                if (Member->isArrow()) {
                  Part = Member->getBase()->IgnoreParenImpCasts();
                  break;
                }
                Part = Member->getBase()->IgnoreParenImpCasts();
              }
              if (isa<CXXThisExpr>(Part) &&
                  (Address || Array ||
                   (ByReference && !Address &&
                    Returned->IgnoreParenImpCasts() != Part) ||
                   Returned->IgnoreParenImpCasts() == Part))
                OfObject = true;
            }
          }
      if (OfObject) {
        Value = Object;
        Addressed = !Arrow;
        continue;
      }
    }
    // What a function returns of what it is given, `fg_Data(Values)` of
    // `int *fg_Data(int (&_Values)[2]) { return _Values; }`, or the address
    // of, `return &_Value;`.
    if (const auto *Call = dyn_cast<CallExpr>(Value);
        Call && Call->getType()->isPointerType() && !Addressed) {
      const VarDecl *Pointed = nullptr;
      for (const Expr *Argument : returnedArguments(Call))
        Pointed = Later(Pointed, localPointedBy(Argument, false));
      for (const Expr *Argument : returnedArguments(Call, /*Addressed=*/true))
        Pointed = Later(Pointed, localPointedBy(Argument, true));
      // Also what it returns that points into what a reference parameter
      // refers to, `begin(Values)` of `return _Values.f_GetArray();`, into
      // what it is given there, also of a static member function,
      // `CHelpers::fs_Data(Values)`, whose arguments are its parameters.
      static thread_local int s_Depth = 0;
      const FunctionDecl *Callee = Call->getDirectCallee();
      const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
      const FunctionDecl *Definition =
          Callee && (!Method ||
                     (Method->isStatic() && !isa<CXXOperatorCallExpr>(Call)))
              ? Callee->getDefinition()
              : nullptr;
      if (!Pointed && Definition && Definition->getBody() && s_Depth < 4) {
        ++s_Depth;
        const llvm::scope_exit Done([] { --s_Depth; });
        for (const Expr *Returned : returnedValues(Definition))
          if (const auto *Given = dyn_cast_or_null<ParmVarDecl>(
                  localPointedBy(Returned, false));
              Given && Given->getType()->isReferenceType() &&
              Given->getDeclContext() == Definition &&
              Given->getFunctionScopeIndex() < Call->getNumArgs())
            Pointed =
                Later(Pointed,
                      localPointedBy(
                          Call->getArg(Given->getFunctionScopeIndex()), true));
      }
      return Pointed;
    }
    // And the address of what it returns a reference to of what it is given,
    // `&fg_Const(Value)`.
    if (const auto *Call = dyn_cast<CallExpr>(Value);
        Call && Call->isGLValue() && Addressed &&
        !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Call)) {
      const VarDecl *Pointed = nullptr;
      for (const Expr *Argument : returnedArguments(Call))
        Pointed = Later(Pointed, localPointedBy(Argument, true));
      return Pointed;
    }
    if (const auto *Plus = dyn_cast<UnaryOperator>(Value);
        Plus && Plus->getOpcode() == UO_Plus) {
      Value = Plus->getSubExpr();
      continue;
    }
    // A pointer stepped, `p++` or `++p`, points into the same data.
    if (const auto *Step = dyn_cast<UnaryOperator>(Value);
        Step && Step->isIncrementDecrementOp() &&
        Step->getType()->isPointerType() && !Addressed) {
      Value = Step->getSubExpr();
      continue;
    }
    // Not a static member named through it, `&Digest.mc_Size`, which has
    // static storage.
    if (const auto *Member = dyn_cast<MemberExpr>(Value);
        Member && isa<VarDecl>(Member->getMemberDecl()))
      return nullptr;
    // Into a member or an element of it, `&Value.m_Member` or `&Values[0]`.
    if (const auto *Member = dyn_cast<MemberExpr>(Value);
        Member && !Member->isArrow() && Addressed) {
      Value = Member->getBase();
      continue;
    }
    // Into what a pointer points to, `&pValue->m_Member` or `&*pValue`,
    // where the pointer points.
    if (const auto *Member = dyn_cast<MemberExpr>(Value);
        Member && Member->isArrow() && Addressed) {
      Value = Member->getBase();
      Addressed = false;
      continue;
    }
    // Also a member through a pointer to it, `&(Value.*pMember)` or
    // `&(pValue->*pMember)`.
    if (const auto *Member = dyn_cast<BinaryOperator>(Value);
        Member && Member->isPtrMemOp() && Addressed) {
      Value = Member->getLHS();
      Addressed = Member->getOpcode() == BO_PtrMemD;
      continue;
    }
    if (const auto *Deref = dyn_cast<UnaryOperator>(Value);
        Deref && Deref->getOpcode() == UO_Deref && Addressed) {
      Value = Deref->getSubExpr();
      Addressed = false;
      continue;
    }
    // An element of an array, `&Values[1]`, or of what a pointer points to,
    // `&pValues[1]`, where the pointer points.
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Value);
        Subscript && Addressed) {
      Value = Subscript->getBase();
      if (!Subscript->getBase()->IgnoreParenImpCasts()->getType()->isArrayType())
        Addressed = false;
      continue;
    }
    // A cast to another pointer, `static_cast<int const *>(&Value)`.
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Value);
        Cast && Cast->getType()->isPointerType() &&
        Cast->getSubExpr()->getType()->isPointerType()) {
      Value = Cast->getSubExpr();
      continue;
    }
    // Moved along it, `Values + 1`.
    if (const auto *Binary = dyn_cast<BinaryOperator>(Value);
        Binary && Binary->isAdditiveOp()) {
      Value = Binary->getLHS()->getType()->isPointerType() ? Binary->getLHS()
                                                            : Binary->getRHS();
      continue;
    }
    // The last of a comma, `(fg_Prepare(), &Value)`.
    if (const auto *Comma = dyn_cast<BinaryOperator>(Value);
        Comma && Comma->getOpcode() == BO_Comma) {
      Value = Comma->getRHS();
      continue;
    }
    // Also of a thread, `thread_local`, which is gone when the thread that
    // made the lambda ends, and a structured binding, `auto [Value] = ...`,
    // whose object is what it decomposes.
    const auto *Ref = dyn_cast<DeclRefExpr>(Value);
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (const auto *Binding =
            Ref ? dyn_cast<BindingDecl>(Ref->getDecl()) : nullptr)
      Local = dyn_cast_or_null<VarDecl>(Binding->getDecomposedDecl());
    if (!Local || (!Local->hasLocalStorage() &&
                   Local->getTLSKind() == VarDecl::TLS_None))
      return nullptr;
    // A local reference is what it is bound to, `&Alias` of `int &Alias =
    // Value`. A reference parameter refers to what its caller keeps, which
    // its address points to like a capture of it by reference refers to,
    // `[pValue = &_Value]` like `[&_Value]`.
    if (Local->getType()->isReferenceType()) {
      if (isa<ParmVarDecl>(Local))
        return Addressed ? Local : nullptr;
      if (!Local->getInit())
        return nullptr;
      // Bound to a temporary that it extends, `int const &Value = 42`, which
      // lives as long as it does.
      const Expr *Init = Local->getInit();
      while (true) {
        if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(Init))
          Init = Cleanups->getSubExpr();
        else if (isa<ImplicitCastExpr, ParenExpr>(Init))
          Init = cast<Expr>(*Init->child_begin());
        else
          break;
      }
      if (const auto *Temporary = dyn_cast<MaterializeTemporaryExpr>(Init);
          Temporary && Temporary->getExtendingDecl() == Local)
        return Addressed ? Local
                         : localPointedBy(Temporary->getSubExpr(), false);
      return pointedThroughLocal(Local, Addressed);
    }
    // A copy of another local pointer, `int *pValue = pOther;`, points where
    // it does; the address of one, `&pOther`, points to it.
    if (Local->getType()->isPointerType() && !Addressed) {
      if (isa<ParmVarDecl>(Local))
        return nullptr;
      // One that is changed, by what it was last assigned where it is
      // copied, `p = &Local; int *q = p;`.
      if (isChangedVariable(Local)) {
        static thread_local llvm::SmallPtrSet<const VarDecl *, 8> s_InProgress;
        const Expr *Assigned = assignedValueAt(Local, Ref);
        if (!Assigned || !s_InProgress.insert(Local).second)
          return nullptr;
        const VarDecl *Pointed = localPointedBy(Assigned, false);
        s_InProgress.erase(Local);
        return Pointed;
      }
      if (!Local->getInit())
        return nullptr;
      return pointedThroughLocal(Local, false);
    }
    // The elements of an array that decays to a pointer to them.
    if (!Addressed && !Local->getType()->isArrayType())
      return nullptr;
    return Local;
  }
  return nullptr;
}

void CoroutineReferenceCaptureCheck::onStartOfTranslationUnit() {
  Instantiations.invalidate();
  Repeated.invalidate();
  s_LocalPointedBy.clear();
  s_PointsToLocal.clear();
}

void CoroutineReferenceCaptureCheck::check(
    const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  const auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda");
  const CXXMethodDecl *Operator = Lambda->getCallOperator();
  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  if (!Operator->isTemplated()) {
    checkLambda(Lambda, Operator, Result);
    return;
  }
  for (const FunctionDecl *Instance :
       Instantiations.instantiationsOf(Operator, *Result.Context))
    if (const auto *InstanceOperator = dyn_cast<CXXMethodDecl>(Instance))
      if (const LambdaExpr *InstanceLambda =
              Instantiations.lambdaOf(Instance, *Result.Context)) {
        const InstantiationScope Scope(*this, Instance);
        checkLambda(InstanceLambda, InstanceOperator, Result);
      }
}

void CoroutineReferenceCaptureCheck::checkLambda(
    const LambdaExpr *Lambda, const CXXMethodDecl *Operator,
    const MatchFinder::MatchResult &Result) {
  auto IsByReference = [](const LambdaCapture &Capture) {
    return Capture.capturesVariable() && Capture.getCaptureKind() == LCK_ByRef;
  };
  // A copy of a pointer into a local, `[pValue = &Value]`, borrows the local
  // like `[&Value]`.
  auto IsPointerIntoLocal = [&](const LambdaCapture &Capture) {
    return pointedLocal(Lambda, Capture, *Result.Context) != nullptr;
  };
  if (!isProtectedCoroutine(Operator) ||
      (llvm::none_of(Lambda->captures(), IsByReference) &&
       llvm::none_of(Lambda->captures(), IsPointerIntoLocal)))
    return;
  // Not one that is never made, in what isn't evaluated or never runs, `if
  // constexpr (false)`.
  if (isNeverRun(Lambda, *Result.Context))
    return;

  // The cleanup of `fg_AsyncDestroy` starts when its guard is destroyed, while
  // the variables declared before it are there, and runs until it first
  // suspends.
  const bool AsyncDestroyCleanup = isAsyncDestroyCleanup(Lambda, *Result.Context);
  // The factory that returns the cleanup, which isn't a coroutine.
  const DeclContext *FactoryOperator = nullptr;
  if (AsyncDestroyCleanup) {
    for (DynTypedNode Node = DynTypedNode::create(*Lambda);;) {
      const auto Parents = instantiatedParents(Node, *Result.Context);
      if (Parents.empty() || Parents[0].get<CallExpr>() || Parents[0].get<VarDecl>())
        break;
      if (Parents[0].get<ReturnStmt>()) {
        FactoryOperator = Lambda->getLambdaClass()->getDeclContext();
        break;
      }
      if (!Parents[0].get<Expr>())
        break;
      Node = Parents[0];
    }
  }

  // A lambda dispatched and joined right away, by a function that is not a
  // coroutine, runs while the locals of that function are there.
  if (isJoinedBlocking(Lambda, *Result.Context) &&
      llvm::all_of(Lambda->captures(), [&](const LambdaCapture &Capture) {
        if (const VarDecl *Pointee = pointedLocal(Lambda, Capture, *Result.Context))
          return isLocalWhileRunning(Pointee, Lambda, *Result.Context);
        return !IsByReference(Capture) ||
               isLocalWhileRunning(Capture.getCapturedVar(), Lambda,
                                   *Result.Context);
      }))
    return;

  // A lambda that code that runs to completion starts and joins runs while
  // what it captures lives until the join, which is never aborted.
  std::optional<RunsToCompletionJoin> Join;
  // In an entry, only once it has switched to a concurrent actor itself: it
  // resumes after awaiting another entry on the actor it was called on.
  const bool RunsToCompletion =
      inRunsToCompletionContext(Lambda, *Result.Context, /*AfterSwitch=*/true);
  if (RunsToCompletion)
    Join = lambdaJoin(Lambda, *Result.Context, nullptr);

  // A lambda called directly runs right away, where the variables it captures
  // are if the lambdas around it keep them, until it first suspends, also one
  // given to `fg_CallSafe` while the options keep it among the SafeCallees.
  llvm::SmallVector<ClosureConsumer, 2> Consumers =
      closureConsumers(Lambda, *Result.Context, {});
  giveUnlistedCallSafe(Consumers, vocabulary().SafeCallees.names());
  const bool CalledInScope = isCalledInScope(Consumers);

  const SourceManager &SM = *Result.SourceManager;
  bool ReportedDefault = false;
  for (const LambdaCapture &Capture : Lambda->captures()) {
    if (!IsByReference(Capture))
      continue;
    const ValueDecl *Variable = Capture.getCapturedVar();
    // A reference init capture of what has static storage, like `[&Value =
    // s_Value]`, refers to what lives as long as the program, but not one
    // of a thread, `thread_local`, which is gone when the thread that made
    // the lambda ends.
    {
      const ValueDecl *Target = Variable;
      // Also through references, which live only as long as what they
      // refer to, `static int &Saved = _Value`, and of a member of it or an
      // element of an array of it, `[&Value = g_Data.m_Value]`, which is in
      // it.
      for (int Depth = 0; Depth < 32; ++Depth) {
        const auto *Alias = dyn_cast<VarDecl>(Target);
        const Expr *Bound =
            Alias &&
                    (Alias->isInitCapture() ||
                     Alias->getType()->isReferenceType()) &&
                    Alias->getInit()
                ? skipWrappers(Alias->getInit())->IgnoreParenImpCasts()
                : nullptr;
        for (int Level = 0; Bound && Level < 16; ++Level)
          if (const auto *Member = dyn_cast<MemberExpr>(Bound);
              Member && !Member->isArrow() &&
              isa<FieldDecl>(Member->getMemberDecl()))
            Bound = Member->getBase()->IgnoreParenImpCasts();
          else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Bound);
                   Subscript && Subscript->getBase()
                                    ->IgnoreParenImpCasts()
                                    ->getType()
                                    ->isConstantArrayType())
            Bound = Subscript->getBase()->IgnoreParenImpCasts();
          // Also through a cast to a reference, `static_cast<int
          // &>(g_Value)`, and a function that gives back what it is given,
          // `fg_Const(g_Value)`.
          else if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Bound);
                   Cast && Cast->isGLValue())
            Bound = Cast->getSubExpr()->IgnoreParenImpCasts();
          else if (const auto *Call = dyn_cast<CallExpr>(Bound);
                   Call && Call->isGLValue() && returnedArgument(Call))
            Bound = skipWrappers(returnedArgument(Call))->IgnoreParenImpCasts();
          else
            break;
        // Also a static member named through an object, `Digest.mc_Size`.
        if (const auto *Static = dyn_cast_or_null<MemberExpr>(Bound);
            Static && isa<VarDecl>(Static->getMemberDecl())) {
          Target = Static->getMemberDecl();
          break;
        }
        const auto *Init = dyn_cast_or_null<DeclRefExpr>(Bound);
        if (!Init)
          break;
        Target = Init->getDecl();
      }
      if (const auto *Static = dyn_cast<VarDecl>(Target);
          Static && Static != Variable && Static->hasGlobalStorage() &&
          Static->getTLSKind() == VarDecl::TLS_None &&
          !Static->getType()->isReferenceType())
        continue;
    }
    const Stmt *Use = nullptr;
    // A pointer that lives until the join, `[&pValue]`, is only as good as
    // what it points to: each local it is made from or pointed at, like
    // `pValue = &Inner`, must live until the join too.
    auto PointeesOutliveJoin = [&]() {
      // The values a local pointer is made from and assigned, also through
      // a local reference to it, `auto &Alias = pValue; Alias = &Inner`. A
      // pointer that is changed where that can't be seen, through a pointer
      // to it or a reference parameter, `fg_Set(pValue)`, has a null value,
      // which nothing outlives.
      // Whether \p Assignment runs only after the lambda is joined, `co_await
      // fg_Move(Guard); ... pValue = &Late;`, which can't change what it
      // points to while it runs, unless a loop around both runs the lambda
      // again after it.
      auto AfterJoin = [&](const Stmt *Assignment) {
        const SourceManager &SM = Result.Context->getSourceManager();
        if (!Join || Join->EarlyAwait.isInvalid() ||
            !SM.isBeforeInTranslationUnit(Join->EarlyAwait,
                                          Assignment->getBeginLoc()))
          return false;
        DynTypedNode Up = DynTypedNode::create(*Assignment);
        for (int Depth = 0; Depth < 256; ++Depth) {
          const auto Above = instantiatedParents(Up, *Result.Context);
          if (Above.empty() || Above[0].get<LambdaExpr>() ||
              Above[0].get<FunctionDecl>())
            return true;
          Up = Above[0];
          if (Up.get<ForStmt>() || Up.get<WhileStmt>() || Up.get<DoStmt>() ||
              Up.get<CXXForRangeStmt>())
            if (const Stmt *Loop = Up.get<Stmt>();
                !SM.isBeforeInTranslationUnit(Lambda->getBeginLoc(),
                                              Loop->getBeginLoc()))
              return false;
        }
        return false;
      };
      auto ValuesOf = [&](const VarDecl *Pointer) {
        llvm::SmallVector<const Expr *, 4> Values;
        if (Pointer->getInit())
          Values.push_back(Pointer->getInit());
        const auto *Function = dyn_cast_or_null<FunctionDecl>(
            Pointer->getParentFunctionOrMethod());
        if (!Function || !Function->getBody())
          return Values;
        const auto Refs =
            match(findAll(declRefExpr(unless(isInUnevaluatedContext()))
                              .bind("ref")),
                  *Function->getBody(), *Result.Context);
        llvm::SmallPtrSet<const ValueDecl *, 4> Names{Pointer};
        for (bool Added = true; Added;) {
          Added = false;
          for (const auto &Nodes : Refs) {
            const auto *Ref = Nodes.getNodeAs<DeclRefExpr>("ref");
            if (!Names.contains(Ref->getDecl()))
              continue;
            const auto Parents = instantiatedParents(*Ref, *Result.Context);
            const auto *Alias =
                Parents.empty() ? nullptr : Parents[0].get<VarDecl>();
            if (Alias && Alias->getType()->isReferenceType() &&
                Names.insert(Alias).second)
              Added = true;
          }
        }
        for (const auto &Nodes : Refs) {
          const auto *Ref = Nodes.getNodeAs<DeclRefExpr>("ref");
          if (!Names.contains(Ref->getDecl()))
            continue;
          const auto Parents = instantiatedParents(*Ref, *Result.Context);
          if (Parents.empty())
            continue;
          // Assigned, also through parentheses, a cast to a reference,
          // `static_cast<int *&>(pValue) = &Local`, or as either of a
          // conditional, `(bChoose ? pFirst : pSecond) = &Local`.
          {
            DynTypedNode Up = DynTypedNode::create(*Ref);
            const BinaryOperator *Target = nullptr;
            for (int Depth = 0; Depth < 8 && !Target; ++Depth) {
              const auto Above = instantiatedParents(Up, *Result.Context);
              if (Above.empty())
                break;
              const Expr *Inner = Up.get<Expr>();
              if (const auto *Assign = Above[0].get<BinaryOperator>();
                  Assign && Assign->getOpcode() == BO_Assign &&
                  Assign->getLHS() == Inner)
                Target = Assign;
              else if (Above[0].get<ParenExpr>() ||
                       (Above[0].get<ExplicitCastExpr>() &&
                        Above[0].get<ExplicitCastExpr>()->isGLValue()) ||
                       (Above[0].get<ImplicitCastExpr>() &&
                        Above[0].get<ImplicitCastExpr>()->getCastKind() !=
                            CK_LValueToRValue) ||
                       (Above[0].get<ConditionalOperator>() &&
                        Above[0].get<ConditionalOperator>()->getCond() != Inner &&
                        Above[0].get<ConditionalOperator>()->isGLValue()))
                Up = Above[0];
              else
                break;
            }
            if (Target) {
              if (!AfterJoin(Target))
                Values.push_back(Target->getRHS());
              continue;
            }
          }
          // Also what `fg_Exchange(pValue, &Inner)` assigns.
          if (const auto *Exchange = Parents[0].get<CallExpr>();
              Exchange && Exchange->getNumArgs() == 2 &&
              Exchange->getArg(0) == Ref &&
              (isExchangeFunction(calleeName(Exchange)))) {
            if (!AfterJoin(Exchange))
              Values.push_back(Exchange->getArg(1));
            continue;
          }
          if (const auto *Address = Parents[0].get<UnaryOperator>();
              Address && Address->getOpcode() == UO_AddrOf) {
            Values.push_back(nullptr);
            continue;
          }
          if (const auto *Call = Parents[0].get<CallExpr>())
            if (const FunctionDecl *Callee = Call->getDirectCallee())
              for (unsigned iArgument = 0; iArgument < Call->getNumArgs() &&
                                           iArgument < Callee->getNumParams();
                   ++iArgument)
                if (Call->getArg(iArgument) == Ref) {
                  QualType Type = Callee->getParamDecl(iArgument)->getType();
                  if (Type->isReferenceType() &&
                      !Type.getNonReferenceType().isConstQualified())
                    Values.push_back(nullptr);
                }
        }
        return Values;
      };
      // Each pointer copied from is looked at once: what it is made from and
      // assigned outlives the join, or the answer is no where it was first
      // looked at.
      llvm::SmallPtrSet<const VarDecl *, 8> Followed;
      // Each local that a value points into lives until the join, also
      // through copies of pointers, `p = q`, and either side of `?:`.
      std::function<bool(const Expr *, int)> Outlives = [&](const Expr *Value,
                                                            int Depth) {
        // Past as many steps as the code can have, what it points to isn't
        // told, which doesn't prove that it lives.
        if (!Value || Depth > 256)
          return false;
        Value = Value->IgnoreParenImpCasts();
        if (const auto *Conditional =
                dyn_cast<AbstractConditionalOperator>(Value))
          return Outlives(Conditional->getTrueExpr(), Depth + 1) &&
                 Outlives(Conditional->getFalseExpr(), Depth + 1);
        // A cast to another pointer, `static_cast<int *>(&Inner)`.
        if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Value);
            Cast && Cast->getType()->isPointerType() &&
            Cast->getSubExpr()->getType()->isPointerType())
          return Outlives(Cast->getSubExpr(), Depth + 1);
        // Further into what a pointer points to, `Values + 1`.
        if (const auto *Binary = dyn_cast<BinaryOperator>(Value);
            Binary && Binary->isAdditiveOp())
          return Outlives(Binary->getLHS()->getType()->isPointerType()
                              ? Binary->getLHS()
                              : Binary->getRHS(),
                          Depth + 1);
        if (const auto *Ref = dyn_cast<DeclRefExpr>(Value)) {
          const auto *Copy = dyn_cast<VarDecl>(Ref->getDecl());
          // A local array that decays to a pointer to it, `p = Values`.
          if (Copy && Copy->hasLocalStorage() && !isa<ParmVarDecl>(Copy) &&
              Copy->getType()->isArrayType())
            return outlivesJoin(Copy, *Join, Lambda, Lambda, *Result.Context);
          // A local reference to a pointer, `int *&pAlias = pSource`, is the
          // pointer it is bound to, which, followed before, like by the other
          // arm of `b ? pAlias : pAlias`, is decided with the rest.
          if (Copy && Copy->hasLocalStorage() && !isa<ParmVarDecl>(Copy) &&
              Copy->getType()->isReferenceType() &&
              Copy->getType().getNonReferenceType()->isPointerType() &&
              Copy->getInit())
            return !Followed.insert(Copy).second ||
                   Outlives(Copy->getInit(), Depth + 1);
          if (!Copy || !Copy->hasLocalStorage() || isa<ParmVarDecl>(Copy) ||
              !Copy->getType()->isPointerType() || !Followed.insert(Copy).second)
            return true;
          return llvm::all_of(ValuesOf(Copy), [&](const Expr *Inner) {
            return Outlives(Inner, Depth + 1);
          });
        }
        const Expr *Object = nullptr;
        // What a getter gives back of the object it is called on, `Local.
        // f_Get()` of `int *f_Get() { return &m_Value; }`, points into it,
        // and so can one of an object with a destructor of its own, which
        // can own what it points to, `Values.f_GetArray()` of a vector.
        auto OwnsWhatItGives = [](const CXXMethodDecl *Method) {
          const CXXDestructorDecl *Destructor =
              Method ? Method->getParent()->getDestructor() : nullptr;
          return Destructor && Destructor->isUserProvided();
        };
        if (const auto *Getter = dyn_cast<CXXMemberCallExpr>(Value);
            Getter && Getter->getType()->isPointerType() &&
            Getter->getImplicitObjectArgument() &&
            (returnsOwnAddress(Getter->getMethodDecl()) ||
             OwnsWhatItGives(Getter->getMethodDecl()))) {
          Object = Getter->getImplicitObjectArgument()->IgnoreParenImpCasts();
          if (Object->getType()->isPointerType())
            return Outlives(Object, Depth + 1);
        } else {
          // The address of it, also `std::addressof(Local)`.
          const auto *Address = dyn_cast<UnaryOperator>(Value);
          const auto *AddressOf = dyn_cast<CallExpr>(Value);
          if (!isAddressOfCall(AddressOf))
            AddressOf = nullptr;
          if ((!Address || Address->getOpcode() != UO_AddrOf) && !AddressOf)
            return true;
          Object = (AddressOf ? AddressOf->getArg(0) : Address->getSubExpr())
                       ->IgnoreParenImpCasts();
        }
        while (true) {
          const auto *Member = dyn_cast<MemberExpr>(Object);
          // What a pointer points to, `&pLocal->m_Value`, is where the
          // pointer points.
          if (Member && Member->isArrow())
            return Outlives(Member->getBase(), Depth + 1);
          if (Member)
            Object = Member->getBase()->IgnoreParenImpCasts();
          else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Object))
            Object = Subscript->getBase()->IgnoreParenImpCasts();
          // What an overloaded operator of an object gives, `&Values[0]` of a
          // vector or `&*pValue` of an owning pointer, is taken to be what it
          // owns, which goes with it.
          else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Object);
                   Operator && Operator->getNumArgs() >= 1 &&
                   isa_and_nonnull<CXXMethodDecl>(
                       Operator->getDirectCallee()) &&
                   (Operator->getOperator() == OO_Subscript ||
                    Operator->getOperator() == OO_Star ||
                    Operator->getOperator() == OO_Arrow))
            Object = Operator->getArg(0)->IgnoreParenImpCasts();
          // What a function whose body can be seen gives back of what it is
          // given by reference, `&fg_Const(Local)`, is that.
          else if (const auto *Call = dyn_cast<CallExpr>(Object);
                   Call && Call->isGLValue() &&
                   !isa<CXXOperatorCallExpr, CXXMemberCallExpr>(Call)) {
            const auto Given = returnedArguments(Call);
            if (Given.size() != 1)
              break;
            Object = Given.front()->IgnoreParenImpCasts();
          } else
            break;
        }
        // What a pointer points to, `&*pInner`, is where the pointer points.
        if (const auto *Deref = dyn_cast<UnaryOperator>(Object);
            Deref && Deref->getOpcode() == UO_Deref)
          return Outlives(Deref->getSubExpr(), Depth + 1);
        const auto *Ref = dyn_cast<DeclRefExpr>(Object);
        const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        return !Local || !Local->hasLocalStorage() ||
               outlivesJoin(Local, *Join, Lambda, Lambda, *Result.Context);
      };
      const auto *Pointer = dyn_cast<VarDecl>(Variable);
      if (!Pointer)
        return true;
      // A view, like `CStrSpan`, refers to what the pointers it is made of
      // and assigned point to, `Span = {Local, 5}`.
      if (!Pointer->getType()->isPointerType() &&
          isDefaultViewType(Pointer->getType(), *Result.Context)) {
        // What a local view is made from and assigned, also through local
        // references to it, `auto &Alias = View; Alias = ...`.
        auto ViewValues = [&](const VarDecl *View) {
          llvm::SmallVector<const Expr *, 4> Values;
          if (View->getInit())
            Values.push_back(View->getInit());
          const auto *Function = dyn_cast_or_null<FunctionDecl>(
              View->getParentFunctionOrMethod());
          if (!Function || !Function->getBody())
            return Values;
          llvm::SmallPtrSet<const VarDecl *, 4> Names{View};
          llvm::SmallVector<const VarDecl *, 4> References;
          std::function<void(const Stmt *)> CollectReferences =
              [&](const Stmt *S) {
                if (!S)
                  return;
                if (const auto *Declaration = dyn_cast<DeclStmt>(S))
                  for (const Decl *D : Declaration->decls())
                    if (const auto *Reference = dyn_cast<VarDecl>(D);
                        Reference && Reference->getType()->isReferenceType() &&
                        Reference->getInit())
                      References.push_back(Reference);
                for (const Stmt *Child : childrenOnce(S))
                  CollectReferences(Child);
              };
          CollectReferences(Function->getBody());
          for (bool Added = true; Added;) {
            Added = false;
            for (const VarDecl *Reference : References)
              if (const auto *Of = dyn_cast<DeclRefExpr>(
                      Reference->getInit()->IgnoreParenImpCasts());
                  Of && Names.contains(dyn_cast<VarDecl>(Of->getDecl())) &&
                  Names.insert(Reference).second)
                Added = true;
          }
          for (const auto &Nodes : match(
                   findAll(cxxOperatorCallExpr(
                               hasOverloadedOperatorName("="),
                               hasArgument(0, ignoringParenImpCasts(
                                                  declRefExpr().bind("to"))))
                               .bind("assign")),
                   *Function->getBody(), *Result.Context))
            if (Names.contains(dyn_cast<VarDecl>(
                    Nodes.getNodeAs<DeclRefExpr>("to")->getDecl())))
              Values.push_back(
                  Nodes.getNodeAs<CXXOperatorCallExpr>("assign")->getArg(1));
          return Values;
        };
        const llvm::SmallVector<const Expr *, 4> Values = ViewValues(Pointer);
        llvm::SmallPtrSet<const VarDecl *, 4> FollowedViews{Pointer};
        std::function<bool(const Expr *, int)> PartsOutlive =
            [&](const Expr *Value, int Depth) {
              if (!Value)
                return true;
              // Past as many steps as the code can have, what it refers to
              // isn't told, which doesn't prove that it lives.
              if (Depth > 256)
                return false;
              Value = skipWrappers(Value)->IgnoreParenImpCasts();
              if (const auto *Materialize =
                      dyn_cast<MaterializeTemporaryExpr>(Value))
                return PartsOutlive(Materialize->getSubExpr(), Depth + 1);
              if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(Value))
                return PartsOutlive(Bind->getSubExpr(), Depth + 1);
              // A copy of another local view, `Outer = Span`, refers where
              // that does.
              if (const auto *Ref = dyn_cast<DeclRefExpr>(Value))
                if (const auto *Other = dyn_cast<VarDecl>(Ref->getDecl());
                    Other && Other->hasLocalStorage() &&
                    !isa<ParmVarDecl>(Other) &&
                    isDefaultViewType(Other->getType(), *Result.Context)) {
                  if (!FollowedViews.insert(Other).second)
                    return true;
                  return llvm::all_of(ViewValues(Other), [&](const Expr *Made) {
                    return PartsOutlive(Made, Depth + 1);
                  });
                }
              llvm::SmallVector<const Expr *, 4> Parts;
              if (const auto *List = dyn_cast<InitListExpr>(Value))
                Parts.append(List->inits().begin(), List->inits().end());
              else if (const auto *Construct = dyn_cast<CXXConstructExpr>(Value);
                       Construct && Construct->getNumArgs() == 1 &&
                       Construct->getConstructor()->isCopyOrMoveConstructor())
                return PartsOutlive(Construct->getArg(0), Depth + 1);
              else if (const auto *Construct = dyn_cast<CXXConstructExpr>(Value))
                Parts.append(Construct->arg_begin(), Construct->arg_end());
              else
                return Outlives(Value, 0);
              return llvm::all_of(Parts, [&](const Expr *Part) {
                return Part->getType()->isIntegralOrEnumerationType() ||
                       PartsOutlive(Part, Depth + 1);
              });
            };
        return llvm::all_of(Values, [&](const Expr *Value) {
          return PartsOutlive(Value, 0);
        });
      }
      if (!Pointer->getType()->isPointerType())
        return true;
      return llvm::all_of(ValuesOf(Pointer), [&](const Expr *Value) {
        return Outlives(Value, 0);
      });
    };
    // Joined, the variable lives while the lambda runs: only what its result
    // keeps of it, like `co_return &Value`, outlives the join.
    if (Join && outlivesJoin(Variable, *Join, Lambda, Lambda, *Result.Context) &&
        PointeesOutliveJoin()) {
      auto IsVariable = [&](const Stmt *S) { return isReferenceTo(S, Variable); };
      auto IsView = [&](QualType Type) {
        return isDefaultViewType(Type, *Result.Context);
      };
      BorrowRoots Roots{IsVariable, /*PointerValues=*/false, IsView};
      Roots.Joined = true;
      // Its references are found by the index of the body made once, not by
      // going through the body for each capture.
      Roots.Decl = Variable;
      Use = firstUseAfterSuspension(Operator, IsVariable, *Result.Context,
                                    &Roots);
      if (!Use)
        continue;
    }
    // The cleanup of an `fg_AsyncDestroy` in code that runs to completion runs
    // before the functions around it end.
    // A local, not a parameter, is there only until the cleanup first
    // suspends: the coroutine joins the cleanup after its locals are gone.
    if (!Use && AsyncDestroyCleanup && RunsToCompletion &&
        outlivesEnclosing(Variable, Lambda)) {
      if (isa<ParmVarDecl>(Variable))
        continue;
      auto IsVariable = [&](const Stmt *S) { return isReferenceTo(S, Variable); };
      auto IsView = [&](QualType Type) {
        return isDefaultViewType(Type, *Result.Context);
      };
      // Also what a pointer it captures points to, through a copy made
      // before, `int *pSaved = pValue`, when that can be a local too.
      BorrowRoots Roots{IsVariable, pointsToLocal(Variable), IsView};
      Roots.Decl = Variable;
      Use = firstUseAfterSuspension(Operator, IsVariable, *Result.Context,
                                    &Roots);
      if (!Use)
        continue;
    }
    // A cleanup returned by a factory runs after the factory returned, so it
    // can't refer to the factory's locals; its reference parameters refer to
    // the variables of the call.
    // Of a reference, what it refers to, `value` of `[&Alias = value]` or of
    // `int &Alias = _Input`, where that is told by what it is bound to.
    const ValueDecl *Referent = Variable;
    for (int Depth = 0; Depth < 64; ++Depth) {
      const auto *Reference = dyn_cast<VarDecl>(Referent);
      if (!Reference || !Reference->getType()->isReferenceType() ||
          isa<ParmVarDecl>(Reference) || !Reference->getInit())
        break;
      const auto *Bound =
          dyn_cast<DeclRefExpr>(Reference->getInit()->IgnoreParenImpCasts());
      if (!Bound || !isa<VarDecl>(Bound->getDecl()))
        break;
      Referent = Bound->getDecl();
    }
    const bool FactoryLocal =
        FactoryOperator &&
        Referent->getParentFunctionOrMethod() == FactoryOperator &&
        !(isa<ParmVarDecl>(Referent) && Referent->getType()->isReferenceType());
    // A reference to a member the object of an actor holds by value, `[&Value
    // = m_Value]` or a local reference to one, `auto &Value = m_Value`, lives
    // with the actor, like `[this]`, whose uses the checks of `this` tell.
    if (!Use) {
      const ValueDecl *Of = Variable;
      for (int Depth = 0; Depth < 4; ++Depth) {
        const auto *Reference = dyn_cast<VarDecl>(Of);
        if (!Reference || !Reference->getType()->isReferenceType() ||
            isa<ParmVarDecl>(Reference) || !Reference->getInit())
          break;
        const Expr *Init = Reference->getInit()->IgnoreParenImpCasts();
        bool Member = false;
        while (const auto *Access = dyn_cast<MemberExpr>(Init)) {
          const auto *Field = dyn_cast<FieldDecl>(Access->getMemberDecl());
          if (!Field || Field->getType()->isReferenceType() ||
              Field->getType()->isPointerType())
            break;
          Member = true;
          Init = Access->getBase()->IgnoreParenImpCasts();
          if (Access->isArrow())
            break;
        }
        const auto *Method = dyn_cast_or_null<CXXMethodDecl>(
            Reference->getParentFunctionOrMethod());
        // Also through the lambdas around it, of the member function they are
        // in.
        for (int Level = 0;
             Method && Method->getParent()->isLambda() && Level < 8; ++Level)
          Method = dyn_cast_or_null<CXXMethodDecl>(
              Method->getParent()->getParentFunctionOrMethod());
        if (Member && isa<CXXThisExpr>(Init) && Method &&
            isActorClass(Method->getParent())) {
          Of = nullptr;
          break;
        }
        const auto *Bound = dyn_cast<DeclRefExpr>(Init);
        if (!Bound)
          break;
        Of = Bound->getDecl();
      }
      if (!Of)
        continue;
    }
    // The lambdas around it must keep the variable too.
    if (!Use && (AsyncDestroyCleanup || CalledInScope) && !FactoryLocal &&
        isLocalWhileRunning(Referent, Lambda, *Result.Context)) {
      auto IsVariable = [&](const Stmt *S) { return isReferenceTo(S, Variable); };
      auto IsView = [&](QualType Type) {
        return isDefaultViewType(Type, *Result.Context);
      };
      // Also what a pointer it captures points to, through a copy made
      // before, `int *pSaved = pValue`, when that can be a local too.
      BorrowRoots Roots{IsVariable, pointsToLocal(Variable), IsView};
      Roots.Decl = Variable;
      Use = firstUseAfterSuspension(Operator, IsVariable, *Result.Context,
                                    &Roots);
      if (!Use)
        continue;
    }
    // Captures made by `[&]` are reported once, at the default.
    if (Capture.isImplicit() && ReportedDefault)
      continue;
    ReportedDefault = ReportedDefault || Capture.isImplicit();
    const SourceLocation Loc = reportLocationIn(
        Capture.isImplicit() ? Lambda->getCaptureDefaultLoc()
                             : Capture.getLocation(),
        Lambda->getEndLoc(), SM);
    if (Loc.isInvalid())
      continue;
    if (!Use) {
      diag(Loc, "a TCFuture coroutine lambda captures %0 by reference but is "
                "not called directly where %0 is, so it can run after %0 is "
                "gone; capture by value, or return a TCUnsafeFuture")
          << Variable;
      continue;
    }
    diag(Loc, "a TCFuture coroutine lambda uses %0, which it captures by "
              "reference, after a suspension, when %0 may be gone; capture by "
              "value, or return a TCUnsafeFuture")
        << Variable;
    diag(Use->getBeginLoc(), "%0 is used here after a suspension, or in a way that keeps a reference to it",
         DiagnosticIDs::Note)
        << Variable;
  }

  // A copy of a pointer into a local borrows the local: it is checked like a
  // capture of the local by reference, with the uses of the copy.
  for (const LambdaCapture &Capture : Lambda->captures()) {
    const VarDecl *Pointee = pointedLocal(Lambda, Capture, *Result.Context);
    if (!Pointee)
      continue;
    const ValueDecl *Pointer = Capture.getCapturedVar();
    // What moves with its owner into the closure lives while it runs, like
    // what is joined: only what its result keeps of it outlives it.
    const bool KeptByClosure = keptByMove(Lambda, Pointer, Pointee);
    const bool FactoryLocal =
        FactoryOperator && Pointee->getParentFunctionOrMethod() == FactoryOperator;
    const Stmt *Use = nullptr;
    const bool Joined =
        Join && outlivesJoin(Pointee, *Join, Lambda, Lambda, *Result.Context);
    // Also what an init capture of this lambda holds, the array of
    // `[Values = {1, 2}]`, which lives with its closure, while it runs, but
    // not of a cleanup, whose closure is moved into its guard: a copy of the
    // list refers to the array of the closure it was made in, which is gone.
    const auto *CaptureOperator =
        dyn_cast<CXXMethodDecl>(Pointee->getDeclContext());
    const bool OwnInitCapture =
        Pointee->isInitCapture() && CaptureOperator &&
        CaptureOperator->getParent() == Lambda->getLambdaClass() &&
        !AsyncDestroyCleanup;
    if (KeptByClosure ||
        ((Joined ||
          ((AsyncDestroyCleanup || CalledInScope) && !FactoryLocal)) &&
         (OwnInitCapture ||
          isLocalWhileRunning(Pointee, Lambda, *Result.Context)))) {
      // Not where it is only compared or tested, `pValue != nullptr`, which
      // reads the closure's copy of the pointer, not what it points to.
      auto IsPointer = [&](const Stmt *S) {
        return isReferenceTo(S, Pointer) &&
               !(isa<Expr>(S) && Pointer->getType()->isPointerType() &&
                 isOnlyTestedPointer(cast<Expr>(S), *Result.Context));
      };
      auto IsView = [&](QualType Type) {
        return isDefaultViewType(Type, *Result.Context);
      };
      // The value of the pointer is what is borrowed, also in a copy of it,
      // `int *pSaved = p`; joined, only what the result keeps of it outlives
      // the join.
      BorrowRoots Roots{IsPointer, /*PointerValues=*/true, IsView};
      Roots.Joined = Joined || KeptByClosure;
      Use = firstUseAfterSuspension(Operator, IsPointer, *Result.Context, &Roots);
      if (!Use)
        continue;
    }
    const SourceLocation Loc =
        reportLocationIn(Capture.isImplicit() ? Lambda->getCaptureDefaultLoc()
                                              : Capture.getLocation(),
                         Lambda->getEndLoc(), SM);
    if (Loc.isInvalid())
      continue;
    // The array of a list it captures, `[Values = {1, 2}]`, which stays with
    // the closure the list is made in, not a copy of the closure.
    if (!Use && Pointer == Pointee) {
      diag(Loc, "a TCFuture coroutine lambda captures the list %0, whose "
                "array stays with the closure it is made in, which can be "
                "gone when a copy of the closure runs; capture an array or a "
                "container by value")
          << Pointer;
      continue;
    }
    if (!Use) {
      diag(Loc, "a TCFuture coroutine lambda captures %0, which points to %1, but "
                "is not called directly where %1 is, so it can run after %1 "
                "is gone; capture a value or a shared pointer, or return a "
                "TCUnsafeFuture")
          << Pointer << Pointee;
      continue;
    }
    diag(Loc, "a TCFuture coroutine lambda uses %0, which points to %1, after a "
              "suspension, when %1 may be gone; capture a value or a shared "
              "pointer, or return a TCUnsafeFuture")
        << Pointer << Pointee;
    diag(Use->getBeginLoc(), "%0 is used here after a suspension",
         DiagnosticIDs::Note)
        << Pointer;
  }
}

} // namespace clang::tidy::malterlib
