//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ActorDispatchThisCheck.h"
#include "../utils/OptionsUtils.h"
#include "ActorAnnotations.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ExprCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <functional>

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

// The types that `X / Lambda` runs the lambda on another actor than the
// current one for: `g_Dispatch(Actor)`, `g_ConcurrentDispatch`, and actors
// whose results run lambdas.
// Also the subscriptions whose cleanup runs on an actor, `g_ActorSubscription(
// Actor) / Lambda`, or on a blocking actor, `g_BlockingActorSubscription /
// Lambda`, and the functors whose calls run on an actor, `g_ActorFunctor(
// Actor) / Lambda`, also weak ones, `g_ActorFunctorWeak(Actor) / Lambda`.

ActorDispatchThisCheck::ActorDispatchThisCheck(StringRef Name,
                                               ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      OtherActorDispatchers(utils::options::parseStringList(
          listOption("OtherActorDispatchers", ""))) {}

void ActorDispatchThisCheck::storeCheckOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "OtherActorDispatchers",
                utils::options::serializeStringList(OtherActorDispatchers));
}

void ActorDispatchThisCheck::registerMatchers(MatchFinder *Finder) {
  // Each kind of node by itself, which the finder tries only on nodes of that
  // kind, not on every expression.
  Finder->addMatcher(cxxOperatorCallExpr(hasOverloadedOperatorName("/"),
                                         unless(isInUnevaluatedContext()))
                         .bind("divide"),
                     this);
  // Also called by name, `Dispatcher.operator/(Lambda)`.
  Finder->addMatcher(
      cxxMemberCallExpr(callee(cxxMethodDecl(hasOverloadedOperatorName("/"))),
                        unless(isInUnevaluatedContext()))
          .bind("divide"),
      this);
  Finder->addMatcher(
      binaryOperator(hasOperatorName("/"), unless(isInUnevaluatedContext()))
          .bind("divide"),
      this);
  Finder->addMatcher(
      callExpr(callee(functionDecl(hasAnyName(
                   "fg_RegisterTimer", "fg_RegisterExactTimer",
                   "fg_OneshotTimer", "fg_OneshotTimerAbortable"))),
               unless(isInUnevaluatedContext()))
          .bind("timer"),
      this);
  // `fg_Dispatch(Actor, Lambda)` and `fg_CallSafeDispatchedOn(Actor, Lambda)`
  // run it on the actor, and `fg_ConcurrentDispatch(Lambda)` on a concurrent
  // one, and so does a functor `fg_ActorFunctor(Actor, Lambda)` makes, also
  // a weak one, `fg_ActorFunctorWeak`, when it is called.
  Finder->addMatcher(callExpr(callee(functionDecl(hasAnyName(
                                  "fg_Dispatch", "fg_CallSafeDispatchedOn",
                                  "fg_ConcurrentDispatch", "fg_ActorFunctor",
                                  "fg_ActorFunctorWeak"))),
                              unless(isInUnevaluatedContext()))
                         .bind("dispatch"),
                     this);
  // Also a functor made directly, `TCActorFunctor<...>(Actor, Lambda)`, like
  // one `fg_ActorFunctor` makes, also a weak one.
  Finder->addMatcher(
      cxxConstructExpr(
          hasDeclaration(cxxConstructorDecl(ofClass(cxxRecordDecl(
              hasAnyName("TCActorFunctor", "TCActorFunctorWeak"))))),
          argumentCountAtLeast(2), unless(isInUnevaluatedContext()))
          .bind("functor"),
      this);
  // Also through an unchanged local pointer to one of them, `auto fDispatch =
  // &fg_ConcurrentDispatch<...>; fDispatch(fCallback)`, also dereferenced,
  // `(*fDispatch)(fCallback)`.
  const auto LocalPointer = declRefExpr(to(varDecl(hasLocalStorage())));
  Finder->addMatcher(
      callExpr(
          callee(expr(ignoringParenImpCasts(anyOf(
              LocalPointer, unaryOperator(hasOperatorName("*"),
                                          hasUnaryOperand(ignoringParenImpCasts(
                                              LocalPointer))))))),
          unless(isInUnevaluatedContext()))
          .bind("dispatch"),
      this);
  // `Actor.f_Bind<&CActor::f_DispatchWithReturn<...>>(Lambda)`, also of
  // `f_Dispatch`, runs it on the actor too.
  Finder->addMatcher(
      cxxMemberCallExpr(callee(cxxMethodDecl(hasAnyName("f_Bind",
                                                        "f_BindByValue"))),
                        unless(isInUnevaluatedContext()))
          .bind("bind"),
      this);
  // `Actor(&CActor::f_DispatchWithReturn<...>, Lambda)`, also of
  // `f_Dispatch`, an actor call of a function that runs what it is given,
  // runs it on the actor too.
  Finder->addMatcher(cxxOperatorCallExpr(hasOverloadedOperatorName("()"),
                                         unless(isInUnevaluatedContext()))
                         .bind("actorCall"),
                     this);
  // Also called by its name, `Actor.operator()(...)`, or through
  // `std::invoke(Actor, ...)`.
  Finder->addMatcher(
      cxxMemberCallExpr(callee(cxxMethodDecl(hasOverloadedOperatorName("()"))),
                        unless(isInUnevaluatedContext()))
          .bind("actorCall"),
      this);
  Finder->addMatcher(callExpr(callee(functionDecl(hasAnyName(
                                  "::std::invoke", "::std::invoke_r"))),
                              unless(isInUnevaluatedContext()))
                         .bind("actorCall"),
                     this);
  // A free or static member function given the object of an actor,
  // `fs_Stop(this)`, which runs on that actor.
  Finder->addMatcher(
      callExpr(callee(functionDecl(hasAnyParameter(parmVarDecl(hasType(
                   qualType(anyOf(pointerType(), referenceType()))))))),
               unless(isInUnevaluatedContext()))
          .bind("given"),
      this);
  // Also through an unchanged local pointer to one, `auto fStop = &fs_Stop;
  // fStop(this)`.
  const auto LocalFunction = declRefExpr(to(varDecl(hasLocalStorage())));
  Finder->addMatcher(
      callExpr(callee(expr(ignoringParenImpCasts(
                   anyOf(LocalFunction,
                         unaryOperator(hasOperatorName("*"),
                                       hasUnaryOperand(ignoringParenImpCasts(
                                           LocalFunction))))))),
               unless(isInUnevaluatedContext()))
          .bind("given"),
      this);
  // A helper of an actor constructed with the object of an actor,
  // `CInternal(this)`, or through `fg_Construct(this)`, which constructs it
  // as what it converts to, `mp_pInternal(fg_Construct(this))`.
  Finder->addMatcher(
      cxxConstructExpr(hasDeclaration(cxxConstructorDecl(hasAnyParameter(
                           parmVarDecl(hasType(qualType(
                               anyOf(pointerType(), referenceType()))))))),
                       unless(isInUnevaluatedContext()))
          .bind("helperConstruct"),
      this);
  Finder->addMatcher(
      callExpr(callee(functionDecl(hasName("::NMib::fg_Construct"))),
               unless(isInUnevaluatedContext()))
          .bind("construct"),
      this);
}

/// Whether \p Record is a helper of an actor, which derives from
/// `CActorInternal` or `CAllowUnsafeThis`, but isn't an actor itself.
static bool isActorHelper(const CXXRecordDecl *Record) {
  return Record && Record->hasDefinition() && isKnownSafeThisClass(Record) &&
         !isActorCallClass(Record);
}

/// Whether \p Type points or refers to the object of an actor, or of a
/// helper of one.
static bool pointsToActorObject(QualType Type) {
  if (!Type->isPointerType() && !Type->isReferenceType())
    return false;
  return isActorClass(Type->getPointeeType()->getAsCXXRecordDecl());
}

/// Whether \p Argument is what the function it is in was given, a parameter,
/// also forwarded, cast or a member of it, like the
/// `fg_Forward<tfp_CParams>(p_Params)...` of `fg_ConstructObject`: its
/// callers give it, where it is checked.
static bool isGivenOn(const Expr *Argument) {
  const Expr *E = Argument;
  for (int Depth = 0; E && Depth < 16; ++Depth) {
    E = E->IgnoreParenImpCasts();
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E)) {
      E = Cast->getSubExpr();
      continue;
    }
    if (const auto *Call = dyn_cast<CallExpr>(E);
        Call && Call->getNumArgs() >= 1 &&
        returnsItsParameter(Call->getDirectCallee())) {
      E = Call->getArg(0);
      continue;
    }
    if (const auto *Member = dyn_cast<MemberExpr>(E)) {
      E = Member->getBase();
      continue;
    }
    if (const auto *Unary = dyn_cast<UnaryOperator>(E);
        Unary && (Unary->getOpcode() == UO_Deref ||
                  Unary->getOpcode() == UO_AddrOf)) {
      E = Unary->getSubExpr();
      continue;
    }
    const auto *Ref = dyn_cast<DeclRefExpr>(E);
    return Ref && isa<ParmVarDecl>(Ref->getDecl());
  }
  return false;
}

/// The class that what \p Call to `fg_Construct` returns constructs: the
/// object of the holder it converts to, like the `CInternal` of
/// `TCUniquePointer<CInternal>`.
static const CXXRecordDecl *constructedBy(const CallExpr *Call,
                                          ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Call);
  for (int Depth = 0; Depth < 16; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    Node = Parents[0];
    if (Node.get<ImplicitCastExpr>() || Node.get<MaterializeTemporaryExpr>() ||
        Node.get<CXXBindTemporaryExpr>() || Node.get<ExprWithCleanups>() ||
        Node.get<ParenExpr>())
      continue;
    const CXXRecordDecl *Holder = nullptr;
    if (const auto *Construct = Node.get<CXXConstructExpr>())
      Holder = Construct->getConstructor()->getParent();
    else if (const auto *Member = Node.get<CXXMemberCallExpr>())
      Holder = Member->getMethodDecl() ? Member->getMethodDecl()->getParent()
                                       : nullptr;
    else if (const auto *Operator = Node.get<CXXOperatorCallExpr>())
      if (const auto *Method =
              dyn_cast_or_null<CXXMethodDecl>(Operator->getDirectCallee()))
        Holder = Method->getParent();
    const auto *Specialization =
        dyn_cast_or_null<ClassTemplateSpecializationDecl>(Holder);
    if (!Specialization || Specialization->getTemplateArgs().size() == 0 ||
        Specialization->getTemplateArgs()[0].getKind() !=
            TemplateArgument::Type)
      return nullptr;
    return Specialization->getTemplateArgs()[0]
        .getAsType()
        ->getAsCXXRecordDecl();
  }
  return nullptr;
}

void ActorDispatchThisCheck::reportHelperConstruction(
    const CXXRecordDecl *Helper, llvm::ArrayRef<const Expr *> Arguments,
    const SourceManager &SM, ASTContext &Context) {
  for (const Expr *Given : Arguments) {
    // A pointer to the object, or the object itself, also what a default
    // argument is, `CHelper(CApp *_pApp = g_pOther)`, reported where it is
    // used.
    const auto *Default = dyn_cast_or_null<CXXDefaultArgExpr>(Given);
    const Expr *Argument = Default ? Default->getExpr() : Given;
    const QualType Type = Argument ? Argument->getType() : QualType();
    if (!Argument ||
        !(Type->isPointerType()
              ? isActorClass(Type->getPointeeType()->getAsCXXRecordDecl())
              : Argument->isGLValue() &&
                    isActorClass(Type->getAsCXXRecordDecl())) ||
        Argument->isNullPointerConstant(Context,
                                        Expr::NPC_ValueDependentIsNotNull) ||
        refersToCurrentActorObject(Argument, &Context) ||
        isOwnActorObject(Argument))
      continue;
    const SourceLocation Loc =
        Default ? reportLocationIn(Default->getUsedLocation(),
                                   Default->getUsedLocation(), SM)
                : reportLocationIn(Argument->getBeginLoc(),
                                   Argument->getEndLoc(), SM);
    if (Loc.isValid())
      diag(Loc, "%0 is constructed with the object of an actor, which it "
                "uses like 'this' on the actor it runs on, but this may be of "
                "another actor; give it the object of the current actor, like "
                "'this'")
          << Helper;
  }
}

/// The parameter or capture of an actor object that \p Decl points or
/// refers to, like a `CActor *_pThis` parameter, or null.
static const ValueDecl *
actorObjectRootOf(const ValueDecl *Decl, int Depth,
                  llvm::SmallVectorImpl<const ValueDecl *> *Others = nullptr);

/// The actor class that \p Type points or refers to.
static const CXXRecordDecl *pointedActorClass(QualType Type);

static bool isActorOf(const Expr *Actor, const ValueDecl *Variable,
                      bool SameCall = false);

bool ActorDispatchThisCheck::isDispatcherType(QualType Type) const {
  if (llvm::is_contained(OtherActorDispatchers, recordName(Type)))
    return true;
  // Also a class derived from one, like `TCActorInterface` of `TCActor`.
  const CXXRecordDecl *Record = Type.getNonReferenceType()->getAsCXXRecordDecl();
  if (!Record || !Record->hasDefinition())
    return false;
  // Each base looked into once, also one that several bases share, which
  // `forallBases` looks into once per path to it.
  llvm::SmallVector<const CXXRecordDecl *, 8> Worklist{Record};
  llvm::SmallPtrSet<const CXXRecordDecl *, 16> Visited{Record};
  while (!Worklist.empty()) {
    const CXXRecordDecl *Current = Worklist.pop_back_val();
    if (!Current->hasDefinition())
      continue;
    for (const CXXBaseSpecifier &Base : Current->getDefinition()->bases()) {
      const CXXRecordDecl *BaseRecord = Base.getType()->getAsCXXRecordDecl();
      if (!BaseRecord || !Visited.insert(BaseRecord).second)
        continue;
      if (BaseRecord->getIdentifier() &&
          llvm::is_contained(OtherActorDispatchers, BaseRecord->getName()))
        return true;
      Worklist.push_back(BaseRecord);
    }
  }
  return false;
}

/// Whether \p Function is one of the functions of `CActor` that run the
/// function they are given, `f_DispatchWithReturn` and `f_Dispatch`.
static bool isDispatchFunction(const ValueDecl *Function) {
  return Function && Function->getIdentifier() &&
         (Function->getName() == "f_DispatchWithReturn" ||
          Function->getName() == "f_Dispatch");
}

/// Whether \p Bind, a call of `f_Bind` or `f_BindByValue`, binds
/// `CActor::f_DispatchWithReturn` or `CActor::f_Dispatch`, which run the
/// function they are given.
static bool bindsDispatch(const CXXMemberCallExpr *Bind) {
  const CXXMethodDecl *Method = Bind->getMethodDecl();
  const TemplateArgumentList *Arguments =
      Method ? Method->getTemplateSpecializationArgs() : nullptr;
  if (!Arguments || Arguments->size() < 1)
    return false;
  const TemplateArgument &Bound = Arguments->get(0);
  const ValueDecl *Function = Bound.getKind() == TemplateArgument::Declaration
                                  ? Bound.getAsDecl()
                                  : nullptr;
  return isDispatchFunction(Function);
}

/// The context of the translation unit the roots are told in, which a
/// constant condition is evaluated in.
static thread_local const ASTContext *s_RootContext = nullptr;

/// The lambdas that \p E can be, also made into a function object, like
/// `TCFunction<int ()>{[this] { ... }}`, which runs it where it is run. Each
/// local and value in \p Followed has what it can be added once, also one
/// assigned itself, `fCallback = fCallback`, or a local made from another on
/// either side of a conditional, `bChoose ? fOther : fOther`.
static void dispatchedLambdas(const Expr *E,
                              llvm::SmallVectorImpl<const LambdaExpr *> &Lambdas,
                              llvm::SmallPtrSetImpl<const void *> &Followed,
                              int Depth = 0) {
  for (; E && Depth < 64; ++Depth) {
    if (const LambdaExpr *Lambda = asLambda(E)) {
      if (!llvm::is_contained(Lambdas, Lambda))
        Lambdas.push_back(Lambda);
      return;
    }
    E = skipWrappers(E)->IgnoreParenImpCasts();
    // Either value of a conditional, `bChoose ? TCFunction<int ()>{[this]
    // {...}} : ...`, also of closures, `bChoose ? fFirst : fSecond`, or the one
    // a constant condition chooses.
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E)) {
      bool Chosen = false;
      if (isa<ConditionalOperator>(Conditional) && s_RootContext &&
          !Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(Chosen,
                                                             *s_RootContext)) {
        E = Chosen ? Conditional->getTrueExpr() : Conditional->getFalseExpr();
        continue;
      }
      dispatchedLambdas(Conditional->getTrueExpr(), Lambdas, Followed,
                        Depth + 1);
      E = Conditional->getFalseExpr();
      continue;
    }
    const StringRef Name = recordName(E->getType());
    if (!Name.starts_with("TCFunction") && Name != "function")
      return;
    // A wrapper moved, `fg_Move(fCallback)`, or held in a local,
    // `TCFunction<int ()> fCallback{[this] { ... }}`.
    if (const auto *Move = dyn_cast<CallExpr>(E);
        Move && Move->getNumArgs() == 1 && (isMoveFunction(calleeName(Move)))) {
      E = Move->getArg(0);
      continue;
    }
    // What it was last assigned, `fCallback = [this] { ... }`, or else its
    // initializer, or, when an assignment on some paths only can be the last,
    // each of what it is initialized and assigned with.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
      const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
      if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local))
        return;
      if (const Expr *Value = assignedValueAt(Local, Ref)) {
        if (!Followed.insert(Value).second)
          return;
        E = Value;
        continue;
      }
      const auto *Function =
          dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
      if (!Function || !Function->getBody() || !Followed.insert(Local).second)
        return;
      if (Local->getInit())
        dispatchedLambdas(Local->getInit(), Lambdas, Followed, Depth + 1);
      // Only an assignment that can run before it is given, before it or in a
      // loop around both, not after it, which can't change the copy given.
      const SourceManager &SM = Local->getASTContext().getSourceManager();
      const SourceRange At = Ref->getSourceRange();
      std::function<void(const Stmt *, bool)> Visit = [&](const Stmt *S,
                                                          bool InLoop) {
        if (!S)
          return;
        if (isa<ForStmt, WhileStmt, DoStmt, CXXForRangeStmt>(S) &&
            S->getSourceRange().fullyContains(At))
          InLoop = true;
        const Expr *Target = nullptr;
        const Expr *Value = nullptr;
        if (const auto *Assign = dyn_cast<BinaryOperator>(S);
            Assign && Assign->getOpcode() == BO_Assign) {
          Target = Assign->getLHS();
          Value = Assign->getRHS();
        } else if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(S);
                   Call && Call->getOperator() == OO_Equal &&
                   Call->getNumArgs() == 2) {
          Target = Call->getArg(0);
          Value = Call->getArg(1);
        }
        if (const auto *Assigned =
                Target ? dyn_cast<DeclRefExpr>(Target->IgnoreParenImpCasts())
                       : nullptr;
            Assigned && Assigned->getDecl() == Local &&
            (InLoop ||
             SM.isBeforeInTranslationUnit(S->getBeginLoc(), At.getBegin())))
          dispatchedLambdas(Value, Lambdas, Followed, Depth + 1);
        for (const Stmt *Child : childrenOnce(S))
          Visit(Child, InLoop);
      };
      Visit(Function->getBody(), false);
      return;
    }
    if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(E))
      E = Cast->getSubExpr();
    else if (const auto *List = dyn_cast<InitListExpr>(E);
             List && List->getNumInits() == 1)
      E = List->getInit(0);
    else if (const auto *Construct = dyn_cast<CXXConstructExpr>(E);
             Construct && Construct->getNumArgs() == 1)
      E = Construct->getArg(0);
    else
      return;
  }
}

/// The lambdas that \p E can be.
static llvm::SmallVector<const LambdaExpr *, 2> dispatchedLambdas(const Expr *E) {
  llvm::SmallVector<const LambdaExpr *, 2> Lambdas;
  llvm::SmallPtrSet<const void *, 4> Followed;
  dispatchedLambdas(E, Lambdas, Followed);
  return Lambdas;
}

void ActorDispatchThisCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  s_RootContext = Result.Context;
  // Not a call given what can't be the object of an actor, like those an
  // await makes, `await_transform(...)`, which most calls are: told before
  // the parents, which deeply nested awaits have many of.
  if (const auto *Call = Result.Nodes.getNodeAs<CallExpr>("given")) {
    const FunctionDecl *Callee = functionCalledBy(Call);
    if (!Callee ||
        llvm::none_of(Callee->parameters(), [&](const ParmVarDecl *Parameter) {
          return takesActorObject(Parameter);
        }))
      return;
  }
  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell, and not what never runs, in a branch of an
  // `if constexpr` that is discarded.
  for (const auto &[Id, Node] : Result.Nodes.getMap())
    if (Node.get<Stmt>() && (isInTemplatePattern(Node, *Result.Context) ||
                             isInDiscardedBranch(Node, *Result.Context)))
      return;
  if (const auto *Call = Result.Nodes.getNodeAs<CallExpr>("given")) {
    reportGivenActorObjects(Call, *Result.SourceManager, *Result.Context);
    return;
  }
  if (const auto *Construct =
          Result.Nodes.getNodeAs<CXXConstructExpr>("helperConstruct")) {
    const CXXConstructorDecl *Constructor = Construct->getConstructor();
    if (!isActorHelper(Constructor->getParent()) ||
        Constructor->isCopyOrMoveConstructor() ||
        Result.SourceManager->isInSystemHeader(Constructor->getLocation()))
      return;
    llvm::SmallVector<const Expr *, 4> Arguments;
    for (unsigned iArgument = 0; iArgument < Construct->getNumArgs() &&
                                 iArgument < Constructor->getNumParams();
         ++iArgument)
      if (pointsToActorObject(
              Constructor->getParamDecl(iArgument)->getType()) &&
          !isGivenOn(Construct->getArg(iArgument)))
        Arguments.push_back(Construct->getArg(iArgument));
    reportHelperConstruction(Constructor->getParent(), Arguments,
                             *Result.SourceManager, *Result.Context);
    return;
  }
  if (const auto *Call = Result.Nodes.getNodeAs<CallExpr>("construct")) {
    const CXXRecordDecl *Helper = constructedBy(Call, *Result.Context);
    if (!isActorHelper(Helper))
      return;
    llvm::SmallVector<const Expr *, 4> Arguments(Call->arguments());
    reportHelperConstruction(Helper, Arguments, *Result.SourceManager,
                             *Result.Context);
    return;
  }
  if (const auto *Call = Result.Nodes.getNodeAs<CallExpr>("actorCall")) {
    // The actor and the arguments after it: the object of a call of the
    // operator by its name, `Actor.operator()(...)`, else the first.
    const auto *ByName = dyn_cast<CXXMemberCallExpr>(Call);
    const Expr *Actor =
        ByName ? ByName->getImplicitObjectArgument()
               : (Call->getNumArgs() >= 1 ? Call->getArg(0) : nullptr);
    const unsigned First = ByName ? 0 : 1;
    if (!Actor || Call->getNumArgs() < First + 2 ||
        !isDispatcherType(Actor->getType()))
      return;
    // Also kept in an unchanged local, `auto fDispatch =
    // &CActor::f_DispatchWithReturn<int>; Actor(fDispatch, ...)`.
    const auto *Address = dyn_cast<UnaryOperator>(
        skipWrappers(
            unchangedLocalValue(Call->getArg(First)->IgnoreParenImpCasts()))
            ->IgnoreParenImpCasts());
    const auto *Named =
        Address && Address->getOpcode() == UO_AddrOf
            ? dyn_cast<DeclRefExpr>(Address->getSubExpr()->IgnoreParens())
            : nullptr;
    if (!Named || !isDispatchFunction(Named->getDecl()))
      return;
    const bool Own = isOwnActor(Actor, *Result.Context);
    if (!Own)
      for (unsigned iArgument = First + 2; iArgument < Call->getNumArgs();
           ++iArgument)
        reportGivenObject(Call->getArg(iArgument), *Result.SourceManager,
                          *Result.Context);
    for (unsigned iArgument = First + 1; iArgument < Call->getNumArgs();
         ++iArgument)
      for (const LambdaExpr *Lambda : dispatchedLambdas(Call->getArg(iArgument))) {
        if (Own)
          reportOnOwnActor(Lambda, Actor, *Result.SourceManager);
        else
          reportCapturedThis(Lambda, Actor, *Result.SourceManager,
                             *Result.Context);
      }
    return;
  }
  if (const auto *Bind = Result.Nodes.getNodeAs<CXXMemberCallExpr>("bind")) {
    if (!bindsDispatch(Bind) || Bind->getNumArgs() < 1)
      return;
    for (const LambdaExpr *Lambda : dispatchedLambdas(Bind->getArg(0))) {
      if (isOwnActor(Bind->getImplicitObjectArgument(), *Result.Context))
        reportOnOwnActor(Lambda, Bind->getImplicitObjectArgument(),
                         *Result.SourceManager);
      else
        reportCapturedThis(Lambda, Bind->getImplicitObjectArgument(),
                           *Result.SourceManager, *Result.Context);
    }
    return;
  }
  if (const auto *Functor =
          Result.Nodes.getNodeAs<CXXConstructExpr>("functor")) {
    const Expr *Actor = Functor->getArg(0);
    if (isOwnActor(Actor, *Result.Context)) {
      for (const LambdaExpr *Own : dispatchedLambdas(Functor->getArg(1)))
        reportOnOwnActor(Own, Actor, *Result.SourceManager);
      return;
    }
    for (const LambdaExpr *Lambda : dispatchedLambdas(Functor->getArg(1)))
      reportCapturedThis(Lambda, Actor, *Result.SourceManager, *Result.Context);
    return;
  }
  if (const auto *Dispatch = Result.Nodes.getNodeAs<CallExpr>("dispatch")) {
    // The function called, also through an unchanged local pointer to it,
    // and copies of that, `auto fCopy = fDispatch;`.
    StringRef Name = calleeName(Dispatch);
    if (!Dispatch->getDirectCallee()) {
      const FunctionDecl *Function = functionCalledBy(Dispatch);
      Name = Function && Function->getIdentifier() ? Function->getName() : "";
      if (Name != "fg_Dispatch" && Name != "fg_CallSafeDispatchedOn" &&
          Name != "fg_ConcurrentDispatch")
        return;
    }
    llvm::SmallVector<const LambdaExpr *, 2> Lambdas;
    const Expr *Actor = nullptr;
    // What is given to the function after it, which it is called with on
    // the other actor.
    unsigned iGiven = 0;
    if (Name == "fg_ConcurrentDispatch") {
      if (Dispatch->getNumArgs() >= 1)
        Lambdas = dispatchedLambdas(Dispatch->getArg(0));
      iGiven = 1;
    } else if (Dispatch->getNumArgs() >= 2) {
      // Of a functor, what follows is the subscription it keeps, not what
      // the lambda is given.
      const bool Functor =
          Name == "fg_ActorFunctor" || Name == "fg_ActorFunctorWeak";
      if (isOwnActor(Dispatch->getArg(0), *Result.Context)) {
        for (const LambdaExpr *Own : dispatchedLambdas(Dispatch->getArg(1)))
          reportOnOwnActor(Own, Dispatch->getArg(0), *Result.SourceManager);
        if (Functor)
          return;
        // What is given to it, which can be the object of another actor,
        // `fg_Dispatch(fg_ThisActor(this), fRead, pOther)`, also as either of
        // a conditional, `bChoose ? pOther : this`, but not the object of the
        // actor it runs on, `fg_Dispatch(fg_ThisActor(pOwn), fRead, pOwn)`.
        for (unsigned iArgument = 2; iArgument < Dispatch->getNumArgs();
             ++iArgument) {
          llvm::SmallVector<const ValueDecl *, 2> Roots;
          // What it names itself, read in the same call as the actor.
          llvm::SmallPtrSet<const ValueDecl *, 2> Named;
          std::function<void(const Expr *, int)> Collect = [&](const Expr *E,
                                                               int Depth) {
            if (!E || Depth > 16)
              return;
            E = E->IgnoreParenImpCasts();
            if (const auto *Conditional =
                    dyn_cast<AbstractConditionalOperator>(E)) {
              Collect(Conditional->getTrueExpr(), Depth + 1);
              Collect(Conditional->getFalseExpr(), Depth + 1);
              return;
            }
            if (const auto *Comma = dyn_cast<BinaryOperator>(E);
                Comma && Comma->getOpcode() == BO_Comma)
              return Collect(Comma->getRHS(), Depth + 1);
            if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E))
              return Collect(Cast->getSubExpr(), Depth + 1);
            if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
              Named.insert(Ref->getDecl());
              if (const ValueDecl *First =
                      actorObjectRootOf(Ref->getDecl(), 0, &Roots))
                Roots.push_back(First);
            }
          };
          Collect(Dispatch->getArg(iArgument), 0);
          const auto *Other = llvm::find_if(Roots, [&](const ValueDecl *Root) {
            return !isActorOf(Dispatch->getArg(0), Root, Named.contains(Root));
          });
          if (Other == Roots.end())
            continue;
          const ValueDecl *Root = *Other;
          const SourceLocation Loc = reportLocationIn(
              Dispatch->getArg(iArgument)->getBeginLoc(),
              Dispatch->getArg(iArgument)->getEndLoc(), *Result.SourceManager);
          if (Loc.isValid())
            diag(Loc, "the object of actor %0 is given to a function that runs "
                      "on another actor, where using it races with its own "
                      "actor; give copies of what it needs instead")
                << pointedActorClass(Root->getType());
        }
        return;
      }
      Lambdas = dispatchedLambdas(Dispatch->getArg(1));
      Actor = Dispatch->getArg(0);
      iGiven = Functor ? Dispatch->getNumArgs() : 2;
    }
    for (unsigned iArgument = iGiven;
         iGiven && iArgument < Dispatch->getNumArgs(); ++iArgument)
      reportGivenObject(Dispatch->getArg(iArgument), *Result.SourceManager,
                        *Result.Context, Actor);
    for (const LambdaExpr *Lambda : Lambdas)
      reportCapturedThis(Lambda, Actor, *Result.SourceManager,
                         *Result.Context);
    return;
  }
  // A timer callback, `fg_RegisterTimer(Period, Lambda, Actor)`, runs on the
  // actor it is given, or the current one.
  if (const auto *Timer = Result.Nodes.getNodeAs<CallExpr>("timer")) {
    if (Timer->getNumArgs() < 2)
      return;
    for (const LambdaExpr *Lambda : dispatchedLambdas(Timer->getArg(1))) {
      // On the own actor, given or the current one when it is empty, it can't
      // use the object of another actor it captures.
      if (Timer->getNumArgs() < 3 || isEmptyActor(Timer->getArg(2)) ||
          isOwnActor(Timer->getArg(2), *Result.Context))
        reportOnOwnActor(Lambda,
                         Timer->getNumArgs() >= 3 ? Timer->getArg(2) : nullptr,
                         *Result.SourceManager);
      else
        reportCapturedThis(Lambda, Timer->getArg(2), *Result.SourceManager,
                           *Result.Context);
    }
    return;
  }
  const auto *Divide = Result.Nodes.getNodeAs<Expr>("divide");
  const Expr *Left = nullptr;
  const Expr *Right = nullptr;
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Divide)) {
    if (Operator->getNumArgs() != 2)
      return;
    Left = Operator->getArg(0);
    Right = Operator->getArg(1);
  } else if (const auto *Named = dyn_cast<CXXMemberCallExpr>(Divide)) {
    if (Named->getNumArgs() != 1)
      return;
    Left = Named->getImplicitObjectArgument();
    Right = Named->getArg(0);
  } else {
    Left = cast<BinaryOperator>(Divide)->getLHS();
    Right = cast<BinaryOperator>(Divide)->getRHS();
  }
  const llvm::SmallVector<const LambdaExpr *, 2> Lambdas =
      dispatchedLambdas(Right);
  if (Lambdas.empty())
    return;

  // `g_Dispatch(Actor) / Lambda` runs on `Actor`, and `Actor / Lambda` too,
  // also through a local that keeps the dispatcher.
  Left = skipWrappers(unchangedLocalValue(Left));
  // `self / Lambda` and `g_Dispatch / Lambda` run on the current actor, where
  // the object of another actor it captures can't be used, also `self` of
  // another object, `pOther->self / Lambda`, which dispatches to the current
  // actor too, and `_pThis->self` in a static function, which runs on the
  // actor of `_pThis`.
  if (const StringRef Current = recordName(Left->getType());
      Current == "CThisActor" || Current == "CDispatchHelper") {
    for (const LambdaExpr *Lambda : Lambdas)
      reportOnOwnActor(Lambda, nullptr, *Result.SourceManager);
    return;
  }
  if (!isDispatcherType(Left->getType()))
    return;
  const Expr *Actor = Left;
  // Past the subscription given to a functor helper, `g_ActorFunctor(Actor)(
  // fg_Move(Subscription))`, to the actor it was given, or else to the helper
  // given none, `g_ActorFunctor(fg_Move(Subscription))`, which makes it run on
  // the current actor.
  const Expr *Given = Left;
  for (int Depth = 0; Depth < 4; ++Depth) {
    const auto *Call = dyn_cast<CXXOperatorCallExpr>(Given);
    if (!Call || Call->getOperator() != OO_Call || Call->getNumArgs() != 2 ||
        isDispatcherType(Call->getArg(1)->getType()))
      break;
    Given = skipWrappers(Call->getArg(0));
  }
  if (recordName(Given->getType()) == "TCActorFunctorHelper") {
    for (const LambdaExpr *Lambda : Lambdas)
      reportOnOwnActor(Lambda, nullptr, *Result.SourceManager);
    return;
  }
  if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(Given);
      Call && Call->getOperator() == OO_Call && Call->getNumArgs() == 2)
    Actor = Call->getArg(1);
  else if (const auto *Call = dyn_cast<CallExpr>(Left);
           Call && !isa<CXXOperatorCallExpr>(Call) && Call->getNumArgs() == 1 &&
           recordName(Call->getType()).starts_with("CDispatchHelper"))
    Actor = Call->getArg(0);
  // `Actor.f_Dispatch() / Lambda` dispatches to the actor it is called on.
  else if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Left);
           Call && Call->getNumArgs() == 0 &&
           recordName(Call->getType()).starts_with("CDispatchHelper"))
    Actor = Call->getImplicitObjectArgument();
  const bool Own = isOwnActor(Actor, *Result.Context);
  for (const LambdaExpr *Lambda : Lambdas)
    if (Own)
      reportOnOwnActor(Lambda, Actor, *Result.SourceManager);
    else
      reportCapturedThis(Lambda, Actor, *Result.SourceManager, *Result.Context);
}

/// Whether \p E designates, or points to, the storage of the object itself,
/// like `m_Value`, `&m_Values[0]` or an entry of a collection member,
/// `m_Map[Key]`, but not what a pointer member points to, like the value of
/// `mp_pInternal`, which can be of another actor.
static bool isIntoObject(const Expr *E, const ASTContext *Context = nullptr,
                         int Depth = 0) {
  // Past as many steps as the code can have, it may be.
  if (!E)
    return false;
  if (Depth > 1024)
    return true;
  // Each read of a local, and each local reference, is followed to its value
  // once while the outermost question is answered, so that values chosen by
  // conditionals of copies, `pB = bChoose ? pA : pA`, or of references, `int
  // &B = bChoose ? A : A`, don't make it exponential.
  static thread_local llvm::SmallPtrSet<const Expr *, 16> *s_pFollowed =
      nullptr;
  std::optional<llvm::SmallPtrSet<const Expr *, 16>> Owned;
  if (!s_pFollowed)
    s_pFollowed = &Owned.emplace();
  const llvm::scope_exit Release([&] {
    if (Owned)
      s_pFollowed = nullptr;
  });
  E = E->IgnoreParens();
  if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(E))
    return isIntoObject(Cleanups->getSubExpr(), Context, Depth + 1);
  // Either value of a conditional, `bChoose ? &m_Value : &s_Other`, or the
  // one a constant condition chooses.
  if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E)) {
    if (bool Chosen = false;
        Context && !Conditional->getCond()->isValueDependent() &&
        Conditional->getCond()->EvaluateAsBooleanCondition(Chosen, *Context))
      return isIntoObject(Chosen ? Conditional->getTrueExpr()
                                 : Conditional->getFalseExpr(),
                          Context, Depth + 1);
    return isIntoObject(Conditional->getTrueExpr(), Context, Depth + 1) ||
           isIntoObject(Conditional->getFalseExpr(), Context, Depth + 1);
  }
  // The condition of `&m_Value ?: &s_Other`, which is its first value.
  if (const auto *Opaque = dyn_cast<OpaqueValueExpr>(E))
    return isIntoObject(Opaque->getSourceExpr(), Context, Depth + 1);
  // Given by reference, like the value of `fg_Exchange(pValue, &m_Value)`.
  if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(E))
    return isIntoObject(Materialize->getSubExpr(), Context, Depth + 1);
  if (const auto *Cast = dyn_cast<CastExpr>(E)) {
    // The value of a pointer, unless it is a local pointer that is still what
    // it was made from, `int *pAlias = pValue;`.
    if (Cast->getCastKind() == CK_LValueToRValue) {
      // Also what an assignment or a comma gives, `(pValue = &m_Value)`.
      if (const auto *Binary =
              dyn_cast<BinaryOperator>(Cast->getSubExpr()->IgnoreParens());
          Binary &&
          (Binary->isAssignmentOp() || Binary->getOpcode() == BO_Comma))
        return isIntoObject(Binary, Context, Depth + 1);
      const auto *Ref =
          dyn_cast<DeclRefExpr>(Cast->getSubExpr()->IgnoreParenImpCasts());
      const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      // What it was last assigned where it is read, also when it is changed
      // after, `int *pCopy = pValue; pValue = nullptr;`.
      if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
          !Local->getType()->isPointerType())
        return false;
      if (!s_pFollowed->insert(Ref).second)
        return false;
      const Expr *Value = assignedValueAt(Local, Ref);
      return Value && isIntoObject(Value, Context, Depth + 1);
    }
    return isIntoObject(Cast->getSubExpr(), Context, Depth + 1);
  }
  // A local reference to it, `auto &Value = m_Value;`.
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
    return Local && Local->hasLocalStorage() &&
           Local->getType()->isReferenceType() && Local->getInit() &&
           s_pFollowed->insert(Local->getInit()).second &&
           isIntoObject(Local->getInit(), Context, Depth + 1);
  }
  // Its address, `std::addressof(m_Value)`.
  if (const auto *Call = dyn_cast<CallExpr>(E); isAddressOfCall(Call))
    return isIntoObject(Call->getArg(0), Context, Depth + 1);
  // What a free function gives back as it is given it, `fg_Identity(&m_Value)`:
  // when its body can be seen, what that returns, and else a reference to an
  // argument of the type it returns, `fg_Const(m_Value)` of `t_CType const
  // &fg_Const(t_CType const &_Value)`. Each argument is looked at once, which
  // nested helpers, `fg_Id(fg_Id(...))`, would otherwise make exponential.
  if (const auto *Free = dyn_cast<CallExpr>(E);
      Free && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Free) &&
      Free->getDirectCallee()) {
    const FunctionDecl *Definition = Free->getDirectCallee()->getDefinition();
    if (Definition && Definition->getBody()) {
      for (const Expr *Argument : returnedArguments(Free))
        if (isIntoObject(Argument, Context, Depth + 1))
          return true;
      // Also the address of what it is given by reference, `fg_Address(
      // m_Value)` of `return &_Value;`.
      for (const Expr *Argument : returnedArguments(Free, /*Addressed=*/true))
        if (isIntoObject(Argument, Context, Depth + 1))
          return true;
      return false;
    }
    if (Free->isGLValue())
      for (unsigned iArgument = 0;
           iArgument < Free->getNumArgs() &&
           iArgument < Free->getDirectCallee()->getNumParams();
           ++iArgument)
        if (Free->getDirectCallee()
                ->getParamDecl(iArgument)
                ->getType()
                ->isReferenceType() &&
            Free->getArg(iArgument)->getType().getUnqualifiedType() ==
                Free->getType().getUnqualifiedType() &&
            isIntoObject(Free->getArg(iArgument), Context, Depth + 1))
          return true;
  }
  if (isa<CXXThisExpr>(E))
    return true;
  // What a member function of it gives a pointer or reference to, like
  // `mp_Items.f_FindEqual(Key)`.
  if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E);
      Call && (Call->getType()->isPointerType() || Call->isGLValue()) &&
      Call->getImplicitObjectArgument())
    return isIntoObject(Call->getImplicitObjectArgument(), Context, Depth + 1);
  // A member of it, also through an unchanged local pointer to it, `auto
  // *pThis = this; &pThis->m_Value`.
  // Not a static member, `&this->ms_Value`, which isn't in the object.
  if (const auto *Member = dyn_cast<MemberExpr>(E))
    return !isa<VarDecl>(Member->getMemberDecl()) &&
           isIntoObject(Member->getBase(), Context, Depth + 1);
  // The last of a comma, `(fg_Log(), &m_Value)`, and what an assignment
  // gives: of a pointer, the value it assigns, `(pValue = &m_Value)`, or
  // the one it steps, `(pValue += 1)`, and else what it assigns to, `(m_Value
  // = 1)`.
  if (const auto *Binary = dyn_cast<BinaryOperator>(E);
      Binary && Binary->getOpcode() == BO_Comma)
    return isIntoObject(Binary->getRHS(), Context, Depth + 1);
  if (const auto *Binary = dyn_cast<BinaryOperator>(E);
      Binary && Binary->isAssignmentOp())
    return isIntoObject(Binary->getOpcode() == BO_Assign &&
                                Binary->getType()->isPointerType()
                            ? Binary->getRHS()
                            : Binary->getLHS(),
                        Context, Depth + 1);
  // A member through a pointer to it, `this->*pMember`.
  if (const auto *Binary = dyn_cast<BinaryOperator>(E);
      Binary && Binary->isPtrMemOp())
    return isIntoObject(Binary->getLHS(), Context, Depth + 1);
  // Further into an array of it, `m_Values + 1`.
  if (const auto *Binary = dyn_cast<BinaryOperator>(E);
      Binary && Binary->isAdditiveOp())
    return isIntoObject(Binary->getLHS()->getType()->isPointerType()
                            ? Binary->getLHS()
                            : Binary->getRHS(),
                        Context, Depth + 1);
  // Also what a pointer into it points to, `*mp_Items.f_FindEqual(1)`, and
  // the pointer as it is, `+pValue`.
  // Also what a prefix increment changes, which it refers to, `++m_Value`.
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary &&
      (Unary->getOpcode() == UO_AddrOf || Unary->getOpcode() == UO_Deref ||
       Unary->getOpcode() == UO_Plus || Unary->getOpcode() == UO_PreInc ||
       Unary->getOpcode() == UO_PreDec))
    return isIntoObject(Unary->getSubExpr(), Context, Depth + 1);
  // And what a postfix one of a pointer gives, where it pointed before,
  // `pValue++`, like a read of it, but not a copy of a number, `m_Value++`.
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary && Unary->isPostfix() && Unary->getType()->isPointerType()) {
    const Expr *Operand = Unary->getSubExpr()->IgnoreParens();
    const auto *Ref = dyn_cast<DeclRefExpr>(Operand);
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
        !Local->getType()->isPointerType())
      return isIntoObject(Operand, Context, Depth + 1);
    if (!s_pFollowed->insert(Ref).second)
      return false;
    const Expr *Value = assignedValueAt(Local, Ref);
    return Value && isIntoObject(Value, Context, Depth + 1);
  }
  // An element of an array of it, `m_Values[1]`, or through a pointer into
  // it, `mp_Items.f_FindEqual(1)[0]`.
  if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E))
    return isIntoObject(Subscript->getBase()->IgnoreParenImpCasts(), Context,
                        Depth + 1);
  // What a lambda called gives back, also of what it is given, `[](int
  // &_Value) { return &_Value; }(m_Value)`, not the closure.
  if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
      Call && Call->getOperator() == OO_Call && Call->getNumArgs() >= 1)
    if (const LambdaExpr *Lambda = calledLambda(Call)) {
      for (const bool Addressed : {false, true})
        for (const Expr *Argument : returnedArguments(Call, Addressed))
          if (isIntoObject(Argument, Context, Depth + 1))
            return true;
      // Each value it returns once too, also of a lambda called from two
      // branches, `[&] { return b ? f0() : f0(); }`, which nested ones would
      // otherwise follow once for each path.
      for (const Expr *Value : returnedValues(Lambda->getCallOperator()))
        if (s_pFollowed->insert(Value).second &&
            isIntoObject(Value, Context, Depth + 1))
          return true;
      return false;
    }
  // Also what a member that owns or points to it gives, `mp_pInternal->m_Value`
  // and `*mp_pInternal`, like a pointer member.
  if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
      Call && Call->getNumArgs() >= 1 &&
      (Call->getOperator() == OO_Subscript || Call->getOperator() == OO_Call ||
       Call->getOperator() == OO_Arrow ||
       (Call->getOperator() == OO_Star && Call->getNumArgs() == 1)))
    return isIntoObject(Call->getArg(0), Context, Depth + 1);
  return false;
}

/// What the pointer \p Variable that \p Lambda copies can be when it is
/// captured: what it is pointed at on every path before the lambda, or else
/// what it is made from, and what it is pointed at after that on some paths,
/// like `if (bUse) pValue = &m_Value;`.
static llvm::SmallVector<const Expr *, 4>
capturedValues(const LambdaCapture &Capture, const VarDecl *Variable,
               const LambdaExpr *Lambda, ASTContext &Context) {
  llvm::SmallVector<const Expr *, 4> Values;
  // A copy of a pointer through a reference to it, `[pAlias]` with `int
  // *&pAlias = pValue`, is a copy of the pointer.
  if (Capture.getCaptureKind() == LCK_ByCopy)
    for (int Depth = 0; Depth < 4 && Variable->getType()->isReferenceType() &&
                        Variable->getInit();
         ++Depth) {
      const auto *Init =
          dyn_cast<DeclRefExpr>(Variable->getInit()->IgnoreParenImpCasts());
      const auto *Aliased = Init ? dyn_cast<VarDecl>(Init->getDecl()) : nullptr;
      if (!Aliased || !Aliased->hasLocalStorage())
        break;
      Variable = Aliased;
    }
  if (Variable->getType()->isReferenceType()) {
    Values.push_back(Variable->getInit());
    return Values;
  }
  // A pointer captured by reference is what it is pointed at when the lambda
  // runs: what it is made from, or any assignment.
  const bool ByReference = Capture.getCaptureKind() != LCK_ByCopy;
  const Expr *Assigned =
      ByReference ? nullptr
                  : assignedOnEveryPathBefore(Lambda, Variable, Context);
  Values.push_back(Assigned ? Assigned : Variable->getInit());
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return Values;
  const SourceManager &SM = Context.getSourceManager();
  const SourceLocation From =
      Assigned ? Assigned->getEndLoc() : Variable->getLocation();
  // Also through local references to it, `int *&pAlias = pValue`, also when
  // bound to either of a conditional, `int *&pAlias = bChoose ? pValue :
  // pOther`, and to those, `int *&pOther = pAlias`, each declared after what
  // it refers to.
  llvm::SmallPtrSet<const VarDecl *, 4> Aliases{Variable};
  std::function<void(const Stmt *)> CollectAliases = [&](const Stmt *S) {
    if (!S)
      return;
    if (const auto *Declaration = dyn_cast<DeclStmt>(S))
      for (const Decl *D : Declaration->decls())
        if (const auto *Alias = dyn_cast<VarDecl>(D);
            Alias && Alias->getType()->isReferenceType() && Alias->getInit())
          for (const DeclRefExpr *Init : assignedVariables(Alias->getInit()))
            if (isa<VarDecl>(Init->getDecl()) &&
                Aliases.contains(cast<VarDecl>(Init->getDecl())))
              Aliases.insert(Alias);
    for (const Stmt *Child : childrenOnce(S))
      CollectAliases(Child);
  };
  CollectAliases(Function->getBody());
  auto IsVariable = [&](const Expr *E) {
    const auto *Ref = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
    const auto *Target = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    return Target && Aliases.contains(Target);
  };
  auto Between = [&](const Expr *Change) {
    return ByReference ||
           (SM.isBeforeInTranslationUnit(From, Change->getBeginLoc()) &&
            SM.isBeforeInTranslationUnit(Change->getEndLoc(),
                                         Lambda->getBeginLoc()));
  };
  // What a local pointer given by reference holds where it is given,
  // `fg_Swap(pValue, pOther)`, which is what the other gets.
  auto HeldValue = [](const Expr *Argument) {
    const auto *Ref = dyn_cast<DeclRefExpr>(Argument->IgnoreParenImpCasts());
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
        !Local->getType()->isPointerType() || !Argument->isGLValue())
      return Argument;
    const Expr *Value = assignedValueAt(Local, Ref);
    return Value ? Value : Argument;
  };
  std::function<void(const Stmt *)> Collect = [&](const Stmt *S) {
    if (!S)
      return;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->getOpcode() == BO_Assign &&
        IsVariable(Assign->getLHS()) && Between(Assign))
      Values.push_back(Assign->getRHS());
    // Exchanged, `fg_Exchange(pValue, &m_Value)`, or swapped, `fg_Swap(pValue,
    // pOther)`, which gives it what the other has.
    if (const auto *Call = dyn_cast<CallExpr>(S);
        Call && Call->getNumArgs() == 2 &&
        (isExchangeFunction(calleeName(Call))) && IsVariable(Call->getArg(0)) &&
        Between(Call))
      Values.push_back(HeldValue(Call->getArg(1)));
    // Pointed by a function whose body can be seen at what it is given,
    // `fg_Set(pValue, m_Value)` of `o_pValue = &_Value;`, also a local
    // lambda, whose call operator gets the lambda first.
    if (const auto *Call = dyn_cast<CallExpr>(S); Call && Between(Call)) {
      const FunctionDecl *Callee =
          Call->getDirectCallee() ? Call->getDirectCallee()->getDefinition()
                                  : nullptr;
      const unsigned Offset =
          isa<CXXOperatorCallExpr>(Call) && Callee && hasImplicitObject(Callee)
              ? 1
              : 0;
      if (Callee && Callee->getBody())
        for (const CSetParameter &Set : setParameters(Callee))
          if (Set.Target + Offset < Call->getNumArgs() &&
              Set.Source + Offset < Call->getNumArgs() &&
              setTargetArgument(Call, Set, Offset) &&
              IsVariable(setTargetArgument(Call, Set, Offset))) {
            const Expr *Given = Call->getArg(Set.Source + Offset);
            Values.push_back(Set.Addressed ? Given : HeldValue(Given));
          }
    }
    if (const auto *Call = dyn_cast<CallExpr>(S); Call && Between(Call))
      if (const auto [Left, Right] = swappedArguments(Call); Left) {
        if (IsVariable(Left))
          Values.push_back(HeldValue(Right));
        if (IsVariable(Right))
          Values.push_back(HeldValue(Left));
      }
    for (const Stmt *Child : childrenOnce(S))
      Collect(Child);
  };
  Collect(Function->getBody());
  return Values;
}

/// The class of the actor whose object a value of \p Type points or refers
/// to, if it does, also through a reference to a pointer, `CApp *const &`.
static const CXXRecordDecl *pointedActorClass(QualType Type) {
  if (Type->isReferenceType() && Type.getNonReferenceType()->isPointerType())
    Type = Type.getNonReferenceType();
  if (!Type->isPointerType() && !Type->isReferenceType())
    return nullptr;
  const CXXRecordDecl *Record = Type->getPointeeType()->getAsCXXRecordDecl();
  return Record && isActorClass(Record) ? Record : nullptr;
}

/// Whether \p Actor is the actor of the object that \p Variable points or
/// refers to: `fg_ThisActor(_pActor)` or `_pActor->self`, also through an
/// unchanged local. With \p SameCall, \p Variable is read where \p Actor is
/// made, in the same call, `fg_Dispatch(fg_ThisActor(_pActor), fRead,
/// _pActor)`, which has the same object, whatever changes it elsewhere.
static bool isActorOf(const Expr *Actor, const ValueDecl *Variable,
                      bool SameCall) {
  // One that is changed, `pActor = pOther`, may be of another actor than the
  // handle made from it.
  if (const auto *Changed = dyn_cast<VarDecl>(Variable);
      Changed && !SameCall && isChangedVariable(Changed))
    return false;
  for (int Depth = 0; Actor && Depth < 4; ++Depth) {
    Actor = skipWrappers(Actor)->IgnoreParenImpCasts();
    if (const auto *Conversion = dyn_cast<CXXMemberCallExpr>(Actor);
        Conversion &&
        isa_and_nonnull<CXXConversionDecl>(Conversion->getMethodDecl()))
      Actor = skipWrappers(Conversion->getImplicitObjectArgument())
                  ->IgnoreParenImpCasts();
    // Also converted to a handle of a base, `TCActor<>` made from a
    // `TCActor<CApp>`, which is of the same actor.
    if (const auto *Converted = dyn_cast<CXXConstructExpr>(Actor);
        Converted && Converted->getNumArgs() == 1 &&
        recordName(Converted->getType()) == "TCActor" &&
        recordName(Converted->getArg(0)->getType().getNonReferenceType()) ==
            "TCActor")
      Actor = skipWrappers(Converted->getArg(0))->IgnoreParenImpCasts();
    auto IsVariable = [&](const Expr *E) {
      E = skipWrappers(E)->IgnoreParenImpCasts();
      if (const auto *Unary = dyn_cast<UnaryOperator>(E);
          Unary && (Unary->getOpcode() == UO_AddrOf ||
                    Unary->getOpcode() == UO_Deref))
        E = Unary->getSubExpr()->IgnoreParenImpCasts();
      const auto *Ref = dyn_cast<DeclRefExpr>(E);
      // Also through an unchanged local alias, `auto *pAlias = _pActor`.
      return Ref && (Ref->getDecl() == Variable ||
                     actorObjectRootOf(Ref->getDecl(), 0) == Variable);
    };
    if (const CallExpr *Call = asCallTo(Actor, {"fg_ThisActor"}))
      return Call->getNumArgs() == 1 && IsVariable(Call->getArg(0));
    if (const auto *Member = dyn_cast<MemberExpr>(Actor))
      return Member->getMemberDecl()->getIdentifier() &&
             Member->getMemberDecl()->getName() == "self" &&
             IsVariable(Member->getBase());
    const Expr *Next = unchangedLocalValue(Actor);
    if (Next == Actor)
      return false;
    Actor = Next;
  }
  return false;
}

bool ActorDispatchThisCheck::reportCapturedThisOfActor(
    const LambdaExpr *Lambda, const SourceManager &SM, ASTContext &Context) {
  const CXXRecordDecl *Record = enclosingThisClass(Lambda);
  if (!Record || !isActorClass(Record))
    return false;

  for (const LambdaCapture &Capture : Lambda->captures()) {
    bool CapturesThis =
        Capture.capturesThis() && Capture.getCaptureKind() != LCK_StarThis;
    // `[pThis = this]`, or `[pThis]` with `auto *pThis = this`.
    if (!CapturesThis && Capture.capturesVariable()) {
      if (const auto *Variable =
              dyn_cast<VarDecl>(Capture.getCapturedVar());
          Variable)
        CapturesThis = isCapturedAliasOfThis(Variable, /*OwnObject=*/true);
    }
    // A pointer or reference into the object, `[pValue = &m_Value]` or
    // `[&Value = m_Value]`, also through a local, `auto &Value = m_Value`.
    // Also a binding of what refers into it, `auto &[Value] = m_State;`.
    if (!CapturesThis && Capture.capturesVariable() &&
        Capture.getCaptureKind() == LCK_ByRef)
      if (const auto *Binding = dyn_cast<BindingDecl>(Capture.getCapturedVar()))
        if (const auto *Decomposition =
                dyn_cast_or_null<VarDecl>(Binding->getDecomposedDecl());
            Decomposition && Decomposition->getType()->isReferenceType() &&
            isIntoObject(Decomposition->getInit(), &Context))
          CapturesThis = true;
    if (!CapturesThis && Capture.capturesVariable())
      if (const auto *Variable = dyn_cast<VarDecl>(Capture.getCapturedVar());
          Variable && Variable->hasLocalStorage() &&
          (Variable->getType()->isPointerType() ||
           Variable->getType()->isReferenceType() ||
           Capture.getCaptureKind() == LCK_ByRef) &&
          // What it can be where the lambda uses it: a pointer copied into
          // the lambda is what it is when it is captured, and one captured by
          // reference what it is pointed at anywhere.
          llvm::any_of(capturedValues(Capture, Variable, Lambda, Context),
                       [&](const Expr *Value) {
                         return isIntoObject(Value, &Context);
                       }) &&
          !(Capture.getCaptureKind() == LCK_ByCopy &&
            !Variable->isInitCapture() &&
            !Variable->getType().getNonReferenceType()->isPointerType()))
        CapturesThis = true;
    if (!CapturesThis)
      continue;
    const SourceLocation Loc = reportLocationIn(
        Capture.isImplicit() ? Lambda->getCaptureDefaultLoc()
                             : Capture.getLocation(),
        Lambda->getEndLoc(), SM);
    if (Loc.isValid())
      diag(Loc, "a lambda that captures 'this' of actor %0 runs on another "
                "actor, where using the object races with its own actor; "
                "capture copies of what it needs instead")
          << Record;
    return true;
  }
  return false;
}

/// The variable that points or refers to the object of an actor that \p
/// Value is of: itself, like `_pThis`, or what it is made from, like
/// `&_pThis->m_Value`, also through unchanged local pointers and references,
/// like `auto *pAlias = _pActor`, which is `_pActor`. Not a static member,
/// which isn't of the object, nor `this`.
/// The variable that points or refers to the object of an actor that
/// \p Value is in, or null. With \p Others, the roots of the other values it
/// can be, like of the other value of a conditional, are added there.
static const ValueDecl *
actorObjectRoot(const Expr *Value, bool Addressed, int Depth = 0,
                llvm::SmallVectorImpl<const ValueDecl *> *Others = nullptr);

/// The roots told of each declaration in a question, with the other roots it
/// added: one met again through another value of it, `b ? p0 : p0`, is told
/// once.
struct CRootOf {
  const ValueDecl *Root = nullptr;
  llvm::SmallVector<const ValueDecl *, 2> Others;
};
static thread_local llvm::DenseMap<const ValueDecl *, CRootOf> *s_pRootsOf =
    nullptr;

static const ValueDecl *
actorObjectRootOfUncached(const ValueDecl *Decl, int Depth,
                          llvm::SmallVectorImpl<const ValueDecl *> *Others);

/// Adds to \p To each of \p From it doesn't have, so that the roots of a
/// chain of conditionals, `p1 = b ? p0 : p0`, are each there once, rather
/// than a number that doubles with each link.
static void addRoots(llvm::SmallVectorImpl<const ValueDecl *> &To,
                     llvm::ArrayRef<const ValueDecl *> From) {
  for (const ValueDecl *Root : From)
    if (!llvm::is_contained(To, Root))
      To.push_back(Root);
}

static const ValueDecl *
actorObjectRootOf(const ValueDecl *Decl, int Depth,
                  llvm::SmallVectorImpl<const ValueDecl *> *Others) {
  if (!s_pRootsOf)
    return actorObjectRootOfUncached(Decl, Depth, Others);
  if (auto It = s_pRootsOf->find(Decl); It != s_pRootsOf->end()) {
    if (Others)
      addRoots(*Others, It->second.Others);
    return It->second.Root;
  }
  // Taken to have none while it is told, which one made of itself can't.
  s_pRootsOf->try_emplace(Decl);
  llvm::SmallVector<const ValueDecl *, 2> Added;
  const ValueDecl *Root = actorObjectRootOfUncached(Decl, Depth, &Added);
  llvm::SmallVector<const ValueDecl *, 2> Unique;
  addRoots(Unique, Added);
  if (Others)
    addRoots(*Others, Unique);
  CRootOf &Told = (*s_pRootsOf)[Decl];
  Told.Root = Root;
  Told.Others = std::move(Unique);
  return Root;
}

static const ValueDecl *
actorObjectRootOfUncached(const ValueDecl *Decl, int Depth,
                          llvm::SmallVectorImpl<const ValueDecl *> *Others) {
  const auto *Local = dyn_cast<VarDecl>(Decl);
  // An unchanged local made from another one, which the actor is told by.
  const bool Followed =
      Local && Local->getInit() &&
      (Local->isInitCapture() ||
       (Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
        (Local->getType()->isPointerType() ||
         Local->getType()->isReferenceType()) &&
        (Local->getType()->isReferenceType() || !isChangedVariable(Local))));
  // A copy that isn't a pointer or a reference, like `[Name = m_Name]`, is
  // a value of its own.
  if (Local && Local->isInitCapture() && !Local->getType()->isPointerType() &&
      !Local->getType()->isReferenceType())
    return nullptr;
  // What a reference is bound to is what it refers to; a pointer is what its
  // value is the address of.
  if (Followed)
    if (const ValueDecl *Root =
            actorObjectRoot(Local->getInit(),
                            Local->getType()->isReferenceType(), Depth + 1,
                            Others))
      return Root;
  // An init capture is what it is made from, not the value of a pointer
  // member, like `[pInternal = mp_pInternal]`, which can be of another actor,
  // and otherwise, like a local, what its type points to, `[pActor =
  // fg_GetActor()]`.
  if (Local && Local->isInitCapture())
    return !isa<MemberExpr>(
               skipWrappers(Local->getInit())->IgnoreParenImpCasts()) &&
                   pointedActorClass(Decl->getType())
               ? Decl
               : nullptr;
  // A local pointer assigned in its function, `pValue = &_pActor->m_Value`,
  // can be what any assignment makes it.
  if (Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
      Local->getType()->isPointerType() && Depth < 4)
    if (const auto *Function =
            dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
        Function && Function->getBody()) {
      // Of each assignment, not only the first: what is assigned last in the
      // code is taken to be what it is, and the others are \p Others, also
      // what it is made with, which it has until it is assigned, also when a
      // copy is made of it before, `[pValue]` before `pValue = nullptr`.
      llvm::SmallVector<const ValueDecl *, 2> Roots;
      if (const ValueDecl *Root =
              Local->getInit()
                  ? actorObjectRoot(Local->getInit(), false, Depth + 1)
                  : nullptr)
        Roots.push_back(Root);
      std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
        if (!S)
          return;
        if (const auto *Assign = dyn_cast<BinaryOperator>(S);
            Assign && Assign->getOpcode() == BO_Assign)
          if (const auto *Target = dyn_cast<DeclRefExpr>(
                  Assign->getLHS()->IgnoreParenImpCasts());
              Target && Target->getDecl() == Local)
            if (const ValueDecl *Root =
                    actorObjectRoot(Assign->getRHS(), false, Depth + 1);
                Root && !llvm::is_contained(Roots, Root))
              Roots.push_back(Root);
        for (const Stmt *Child : childrenOnce(S))
          Visit(Child);
      };
      Visit(Function->getBody());
      if (!Roots.empty()) {
        if (Others)
          addRoots(*Others, llvm::ArrayRef(Roots).drop_back());
        return Roots.back();
      }
    }
  return pointedActorClass(Decl->getType()) ? Decl : nullptr;
}

static const ValueDecl *
actorObjectRoot(const Expr *Value, bool Addressed, int Depth,
                llvm::SmallVectorImpl<const ValueDecl *> *Others) {
  // Through as many copies as the code can have, `int *p1 = p0;`, within a
  // budget for the whole question, which conditionals that each follow both
  // ways, `b ? p0 : p0`, would otherwise make exponential.
  static thread_local int s_Budget = -1;
  const bool Outermost = s_Budget < 0;
  std::optional<llvm::DenseMap<const ValueDecl *, CRootOf>> RootsOf;
  if (Outermost) {
    s_Budget = 4096;
    s_pRootsOf = &RootsOf.emplace();
  }
  const llvm::scope_exit Release([&] {
    if (Outermost) {
      s_Budget = -1;
      s_pRootsOf = nullptr;
    }
  });
  for (; Value && Depth < 64 && --s_Budget > 0; ++Depth) {
    Value = skipWrappers(Value)->IgnoreParenImpCasts();
    if (const auto *Unary = dyn_cast<UnaryOperator>(Value);
        Unary && (Unary->getOpcode() == UO_AddrOf ||
                  Unary->getOpcode() == UO_Deref)) {
      // The address of what it is applied to; what a pointer points to is
      // an object it is the address of.
      Addressed = Unary->getOpcode() == UO_AddrOf;
      Value = Unary->getSubExpr();
      continue;
    }
    // A member, of which only the address or what a reference binds to is in
    // the object: the value of a member, like `[pStore = Internal.m_pStore]`,
    // is a copy, but the elements of a member array are in it.
    if (const auto *Member = dyn_cast<MemberExpr>(Value)) {
      if (!isa<FieldDecl>(Member->getMemberDecl()) ||
          (!Addressed && !Member->getType()->isArrayType()))
        return nullptr;
      Value = Member->getBase();
      // What `->` is applied to is a pointer, whose value is the address.
      Addressed = !Member->isArrow();
      continue;
    }
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Value)) {
      if (!Addressed)
        return nullptr;
      Value = Subscript->getBase();
      continue;
    }
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Value)) {
      Value = Cast->getSubExpr();
      continue;
    }
    // Either value of a conditional, `bChoose ? _pActor : nullptr`, or the
    // last of a comma; of a constant condition, the one it chooses.
    if (const auto *Conditional = dyn_cast<ConditionalOperator>(Value);
        Conditional && s_RootContext &&
        !Conditional->getCond()->isValueDependent()) {
      bool Chosen = false;
      if (Conditional->getCond()->EvaluateAsBooleanCondition(Chosen,
                                                             *s_RootContext)) {
        Value =
            Chosen ? Conditional->getTrueExpr() : Conditional->getFalseExpr();
        continue;
      }
    }
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(Value)) {
      if (const ValueDecl *Root = actorObjectRoot(
              Conditional->getTrueExpr(), Addressed, Depth + 1, Others)) {
        if (!Others)
          return Root;
        addRoots(*Others, Root);
      }
      Value = Conditional->getFalseExpr();
      continue;
    }
    // Moved within an array, like `_pActor->m_Values + 1`.
    if (const auto *Offset = dyn_cast<BinaryOperator>(Value);
        Offset && Offset->isAdditiveOp() && Offset->getType()->isPointerType()) {
      Value = Offset->getLHS()->getType()->isPointerType() ? Offset->getLHS()
                                                           : Offset->getRHS();
      continue;
    }
    if (const auto *Comma = dyn_cast<BinaryOperator>(Value);
        Comma && Comma->getOpcode() == BO_Comma) {
      Value = Comma->getRHS();
      continue;
    }
    // A variable that points or refers to the object, which is what is
    // pointed to, or a local made from one.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Value))
      return actorObjectRootOf(Ref->getDecl(), Depth, Others);
    return nullptr;
  }
  return nullptr;
}

void ActorDispatchThisCheck::reportCapturedThis(const LambdaExpr *Lambda,
                                                const Expr *Actor,
                                                const SourceManager &SM,
                                                ASTContext &Context) {
  if (reportCapturedThisOfActor(Lambda, SM, Context))
    return;
  reportCapturedActorObjects(Lambda, Actor, SM, /*OnOwnActor=*/false);
}

void ActorDispatchThisCheck::reportGivenObject(const Expr *Argument,
                                               const SourceManager &SM,
                                               ASTContext &Context,
                                               const Expr *Actor) {
  // A pointer to the object of the own actor, `this`, or into it,
  // `&m_Value`, which the function uses on the other actor; a value is
  // copied.
  if (!Argument->IgnoreParenImpCasts()->getType()->isPointerType())
    return;
  // Also a local pointer, by what it was last assigned, `auto *pThis =
  // this`, which giving it on by reference doesn't change.
  const Expr *Given = Argument;
  if (const auto *Ref = dyn_cast<DeclRefExpr>(Argument->IgnoreParenImpCasts()))
    if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
        Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
        Local->getType()->isPointerType())
      if (const Expr *Value = assignedValueAt(Local, Ref))
        Given = Value;
  auto Report = [&](const CXXRecordDecl *Record) {
    const SourceLocation Loc =
        reportLocationIn(Argument->getBeginLoc(), Argument->getEndLoc(), SM);
    if (Loc.isValid())
      diag(Loc, "the object of actor %0 is given to a function that runs on "
                "another actor, where using it races with its own actor; give "
                "copies of what it needs instead")
          << Record;
  };
  // Also a parameter given the object of the current actor, `_pThis` of a
  // static function, also one given on, which can change it, of the class it
  // points to.
  if (!(refersToOwnObject(Given, &Context) || isIntoObject(Given, &Context))) {
    const auto *Ref = dyn_cast<DeclRefExpr>(Given->IgnoreParenImpCasts());
    const auto *Parameter =
        Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl()) : nullptr;
    // Not the object of the actor it runs on, `fg_Dispatch(fg_ThisActor(
    // _pActor), fRead, _pActor)`, whichever the current one is.
    if (Parameter && Actor &&
        isActorOf(Actor, Parameter,
                  /*SameCall=*/Given->IgnoreParenImpCasts() == Ref))
      return;
    if (const CXXRecordDecl *Pointed = pointedActorClass(Given->getType());
        Pointed && ((Parameter && takesActorObject(Parameter)) ||
                    refersToCurrentActorObject(Given, &Context)))
      Report(Pointed);
    return;
  }
  // The class of the member function it is in, also through a lambda in it,
  // whose object it is, also through an alias, `auto *pThis = this`.
  const CXXRecordDecl *Record = nullptr;
  DynTypedNode Node = DynTypedNode::create(*Argument);
  for (int Depth = 0; Depth < 256 && !Record; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      break;
    if (const auto *Method = Parents[0].get<CXXMethodDecl>();
        Method && !Method->isStatic() && !isLambdaCallOperator(Method))
      Record = Method->getParent();
    Node = Parents[0];
  }
  if (Record && isActorClass(Record))
    Report(Record);
}

void ActorDispatchThisCheck::reportGivenActorObjects(const CallExpr *Call,
                                                     const SourceManager &SM,
                                                     ASTContext &Context) {
  // Not one of a system header, like `std::addressof(*this)`, which isn't
  // given the object to use it.
  const FunctionDecl *Callee = functionCalledBy(Call);
  if (!Callee ||
      Context.getSourceManager().isInSystemHeader(Callee->getLocation()))
    return;
  // Nor one that only gives back what it is given, like `fg_Forward` and
  // `fg_Move`: what is done with the result matters, which is checked where
  // it is used.
  if (returnsItsParameter(Callee))
    return;
  // The object of a call operator, `fCallback(this)`, is the first argument,
  // which an explicit object parameter, `this auto &Self`, is given.
  const auto *Method = dyn_cast<CXXMethodDecl>(Callee);
  const unsigned Offset = isa<CXXOperatorCallExpr>(Call) && Method &&
                                  Method->isImplicitObjectMemberFunction()
                              ? 1
                              : 0;
  for (unsigned iParameter = 0; iParameter < Callee->getNumParams() &&
                                iParameter + Offset < Call->getNumArgs();
       ++iParameter) {
    if (!takesActorObject(Callee->getParamDecl(iParameter)))
      continue;
    // Nor one that only constructs or destroys with the object, like
    // `fg_DeleteObject` and `fg_ConstructObject`, which manage it, not when
    // the call runs an override, whose body isn't that one, `_Interface.
    // f_Use(_pOther)` of a virtual one.
    if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
        !(Member && callsOverride(Member)) &&
        onlyConstructsOrDestroys(Callee, iParameter))
      continue;
    // The object of the actor the call runs on, `this`, or a parameter given
    // it in turn, `_pThis`, also through unchanged local aliases, or what the
    // object owns, its internals, `&*mp_pInternal`, or none.
    // Also a default argument, `fg_Read()` of `fg_Read(CApp *_pApp =
    // g_pOther)`, which is what it is made of, reported at the call.
    const Expr *Argument = Call->getArg(iParameter + Offset);
    const auto *Default = dyn_cast<CXXDefaultArgExpr>(Argument);
    if (Default)
      Argument = Default->getExpr();
    if (Argument->isNullPointerConstant(Context,
                                        Expr::NPC_ValueDependentIsNotNull) ||
        refersToCurrentActorObject(Argument, &Context) ||
        isOwnActorObject(Argument))
      continue;
    const SourceLocation Loc =
        Default ? reportLocationIn(Call->getBeginLoc(), Call->getEndLoc(), SM)
                : reportLocationIn(Argument->getBeginLoc(),
                                   Argument->getEndLoc(), SM);
    if (Loc.isValid())
      diag(Loc, "%0 is given the object of an actor, which it uses like "
                "'this' on the actor the call runs on, but this may be of "
                "another actor; give it the object of the current actor, like "
                "'this', or call it on the actor of the object")
          << diagnosticDecl(Callee);
  }
}

void ActorDispatchThisCheck::reportOnOwnActor(const LambdaExpr *Lambda,
                                              const Expr *Actor,
                                              const SourceManager &SM) {
  // In a member function of an actor, the own actor is the one of `this`,
  // which isn't the actor of the object of another one it captures, and so
  // is it in a free or static function given the object of an actor, the
  // one of that object.
  const CXXRecordDecl *Record = enclosingThisClass(Lambda);
  const FunctionDecl *Function = enclosingFunction(Lambda);
  while (Function && isLambdaCallOperator(Function))
    Function = dyn_cast_or_null<FunctionDecl>(
        cast<CXXMethodDecl>(Function)->getParent()->getDeclContext());
  if ((Record && isActorClass(Record)) ||
      (Function && llvm::any_of(Function->parameters(), takesActorObject)))
    reportCapturedActorObjects(Lambda, Actor, SM, /*OnOwnActor=*/true);
}

void ActorDispatchThisCheck::reportCapturedActorObjects(const LambdaExpr *Lambda,
                                                        const Expr *Actor,
                                                        const SourceManager &SM,
                                                        bool OnOwnActor) {
  // A pointer or reference to the object of an actor, like the parameter
  // `CActor *_pThis` of a static function, or into it, like `[pValue =
  // &_pThis->m_Value]`, used from another actor than its own.
  for (const LambdaCapture &Capture : Lambda->captures()) {
    if (!Capture.capturesVariable())
      continue;
    const ValueDecl *Captured = Capture.getCapturedVar();
    // A copy that isn't a pointer, like `[Value]` of `int &Value =
    // _pActor->m_Value`, is a value of its own, but one of a reference to a
    // pointer, `CApp *const &_pActor`, is the pointer.
    if (const auto *Variable = dyn_cast<VarDecl>(Captured);
        Capture.getCaptureKind() == LCK_ByCopy && Variable &&
        !Variable->isInitCapture() &&
        !Variable->getType().getNonReferenceType()->isPointerType())
      continue;
    // What refers to `this`, which the check of `this` tells.
    if (const auto *Variable = dyn_cast<VarDecl>(Captured);
        Variable && isCapturedAliasOfThis(Variable, /*OwnObject=*/true))
      continue;
    // Of each value it can be, like either of `bChoose ? _pA : _pB`.
    llvm::SmallVector<const ValueDecl *, 2> Roots;
    if (const ValueDecl *First = actorObjectRootOf(Captured, 0, &Roots))
      Roots.push_back(First);
    // A parameter given the object of the current actor, `_pThis` of a
    // static function, is of the actor it runs on.
    const auto *Other = llvm::find_if(Roots, [&](const ValueDecl *Root) {
      return !(Actor && isActorOf(Actor, Root)) &&
             !(OnOwnActor && isActorObjectParameter(Root));
    });
    if (Other == Roots.end())
      continue;
    const ValueDecl *Root = *Other;
    const SourceLocation Loc = reportLocationIn(
        Capture.isImplicit() ? Lambda->getCaptureDefaultLoc()
                             : Capture.getLocation(),
        Lambda->getEndLoc(), SM);
    if (Loc.isValid())
      diag(Loc, "a lambda that captures %0, which points or refers to the "
                "object of actor %1, runs on another actor, where using the "
                "object races with its own actor; capture copies of what it "
                "needs instead")
          << Captured << pointedActorClass(Root->getType());
    return;
  }
}

} // namespace clang::tidy::malterlib
