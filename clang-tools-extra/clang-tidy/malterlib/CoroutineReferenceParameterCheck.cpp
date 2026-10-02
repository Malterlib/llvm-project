//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CoroutineReferenceParameterCheck.h"
#include "RunsToCompletion.h"
#include "../utils/OptionsUtils.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

CoroutineReferenceParameterCheck::CoroutineReferenceParameterCheck(
    StringRef Name, ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      ViewTypes(utils::options::parseStringList(
          listOption("ViewTypes", CVocabulary::builtIn("ViewTypes")))),
      CheckPointers(boolOption("CheckPointers", true)) {}

void CoroutineReferenceParameterCheck::storeCheckOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "ViewTypes",
                utils::options::serializeStringList(ViewTypes));
  Options.store(Opts, "CheckPointers", CheckPointers);
}

void CoroutineReferenceParameterCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(functionDecl(isDefinition()).bind("function"), this);
  // Lambda call operators are reached through their expressions.
  Finder->addMatcher(lambdaExpr().bind("lambda"), this);
}

bool CoroutineReferenceParameterCheck::isViewType(
    QualType Type, const ASTContext &Context) const {
  return matchesTypeName(Type, ViewTypes, Context);
}

/// Whether \p Type points or refers to an object that coroutines may use like
/// `this`: of an actor, its internals, or a class that allows it.
static bool refersToSafeThisClass(QualType Type) {
  const QualType Pointee = Type->isReferenceType() || Type->isPointerType()
                               ? Type->getPointeeType()
                               : QualType();
  return !Pointee.isNull() &&
         isKnownSafeThisClass(Pointee->getAsCXXRecordDecl());
}

/// Whether \p Type points or refers to an object that coroutines may use like
/// `this`: of an actor, its internals, or a class that allows it.
static bool refersToSafeThisClass(QualType Type);

const Stmt *CoroutineReferenceParameterCheck::borrowedUse(
    const FunctionDecl *Function, const ParmVarDecl *Parameter,
    ASTContext &Context) const {
  const QualType Type = Parameter->getType();
  const bool Reference = Type->isReferenceType();
  const bool Pointer = Type->isPointerType() && !Type->isFunctionPointerType();
  if (!Reference && !(CheckPointers && Pointer) && !isViewType(Type, Context))
    return nullptr;
  // A pointer or reference to an actor, its internals or a class that allows
  // it is used like `this`, and a function doesn't go away.
  if (refersToSafeThisClass(Type) ||
      (Reference && Type->getPointeeType()->isFunctionType()))
    return nullptr;
  // A coroutine runs in its call until it first suspends, while the caller
  // still has the data.
  // Not to read only what is its own, of a view taken by value,
  // `Text.f_GetLen()`, which is in the frame.
  auto IsParameter = [&](const Stmt *S) {
    return isReferenceTo(S, Parameter) && !readsOnlyOwnPart(S, Context);
  };
  // What a pointer or view parameter points to is the borrowed data.
  auto IsView = [&](QualType Type) { return isViewType(Type, Context); };
  BorrowRoots Roots{IsParameter, /*PointerValues=*/true, IsView};
  // A function that runs to completion is joined by its callers, which keep
  // what its parameters refer to until then: only its result can keep it
  // longer.
  Roots.Joined = isJoinedByCallers(Function);
  Roots.Decl = Parameter;
  return firstUseAfterSuspension(Function, IsParameter, Context, &Roots);
}

void CoroutineReferenceParameterCheck::check(
    const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  const FunctionDecl *Function =
      Result.Nodes.getNodeAs<FunctionDecl>("function");
  if (Function && isLambdaCallOperator(Function))
    return;
  if (const auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda"))
    Function = Lambda->getCallOperator();
  if (!Function)
    return;
  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  if (!Function->isTemplated()) {
    checkFunction(Function, *Result.Context);
    return;
  }
  for (const FunctionDecl *Instance :
       Instantiations.instantiationsOf(Function, *Result.Context)) {
    const InstantiationScope Scope(*this, Instance);
    checkFunction(Instance, *Result.Context);
  }
}

void CoroutineReferenceParameterCheck::checkFunction(
    const FunctionDecl *Function, ASTContext &Context) {
  if (!isCoroutine(Function) || !isProtectedCoroutine(Function))
    return;
  // Actor calls move the arguments of a member function of an actor into its
  // coroutine once, so it can't take references.
  const auto *Method = dyn_cast<CXXMethodDecl>(Function);
  const CXXRecordDecl *ActorClass =
      Method && !Method->isStatic() && !isLambdaCallOperator(Method) &&
              isActorCallClass(Method->getParent())
          ? Method->getParent()
          : nullptr;

  // Whether the body uses \p Parameter, other than in what isn't evaluated,
  // in the copies of the parameters into the frame, which every coroutine
  // makes, or to read only what is its own, `Text.f_GetLen()`. Assigning a
  // pointer, `_pValue = &m_Value`, doesn't use what it was given, and neither
  // does what reads it after it is assigned on every path.
  auto UsedIn = [&](const ParmVarDecl *Parameter) {
    const Stmt *Body = Function->getBody();
    if (const auto *Coroutine = dyn_cast_or_null<CoroutineBodyStmt>(Body))
      Body = Coroutine->getBody();
    const bool Value = !Parameter->getType()->isReferenceType();
    // Also through a cast to a reference to it, `static_cast<int
    // *&>(_pValue)`, and a local reference bound to it, `auto &pCursor =
    // _pValue`, which binding doesn't read.
    llvm::SmallPtrSet<const VarDecl *, 2> Aliases;
    auto Names = [&](const Expr *E) {
      E = E->IgnoreParenImpCasts();
      while (const auto *Cast = dyn_cast<ExplicitCastExpr>(E)) {
        if (!Cast->isGLValue())
          break;
        E = Cast->getSubExpr()->IgnoreParenImpCasts();
      }
      return dyn_cast<DeclRefExpr>(E);
    };
    auto IsParameter = [&](const Expr *E) {
      const DeclRefExpr *Ref = Names(E);
      return Ref && (Ref->getDecl() == Parameter ||
                     Aliases.contains(dyn_cast<VarDecl>(Ref->getDecl())));
    };
    {
      std::function<void(const Stmt *)> FindAliases = [&](const Stmt *S) {
        if (!S)
          return;
        if (const auto *Declaration = dyn_cast<DeclStmt>(S))
          for (const Decl *D : Declaration->decls())
            if (const auto *Alias = dyn_cast<VarDecl>(D);
                Alias && Alias->hasLocalStorage() && Alias->getInit() &&
                Alias->getType()->isLValueReferenceType() &&
                !Alias->getType().getNonReferenceType().isConstQualified() &&
                IsParameter(Alias->getInit()))
              Aliases.insert(Alias);
        for (const Stmt *Child : childrenOnce(S))
          FindAliases(Child);
      };
      FindAliases(Body);
    }
    // After a statement of the body that assigns it, `_pValue = &m_Value;`,
    // also as what an `if`, a `switch` or a `for` starts with, `if (_pValue =
    // mp_Items.f_FindEqual(1); _pValue)`, which every use after it is past,
    // unless a label lets a jump go back before it, it has that value: found
    // once, not for each use.
    const SourceManager &SM = Context.getSourceManager();
    SourceLocation AssignedBy;
    if (const auto *Block = dyn_cast_or_null<CompoundStmt>(Body);
        Block && Value) {
      std::function<bool(const Stmt *)> HasLabel = [&](const Stmt *S) {
        return S &&
               (isa<LabelStmt>(S) || llvm::any_of(childrenOnce(S), HasLabel));
      };
      if (!HasLabel(Block))
        for (const Stmt *Statement : Block->body()) {
          if (const auto *If = dyn_cast<IfStmt>(Statement); If && If->getInit())
            Statement = If->getInit();
          else if (const auto *Switch = dyn_cast<SwitchStmt>(Statement);
                   Switch && Switch->getInit())
            Statement = Switch->getInit();
          else if (const auto *For = dyn_cast<ForStmt>(Statement);
                   For && For->getInit())
            Statement = For->getInit();
          const auto *Assign = dyn_cast<BinaryOperator>(Statement);
          if (Assign && Assign->getOpcode() == BO_Assign &&
              IsParameter(Assign->getLHS())) {
            AssignedBy = Assign->getEndLoc();
            break;
          }
        }
    }
    // A lambda that captures it by reference, `[&_pValue] { return *_pValue;
    // }`, reads it where it is called: not before the statement that assigns
    // it when each call is after it.
    auto RunsOnlyAfterAssigned = [&](const LambdaExpr *Lambda) {
      if (AssignedBy.isInvalid() ||
          llvm::none_of(Lambda->captures(), [&](const LambdaCapture &Capture) {
            return Capture.capturesVariable() &&
                   Capture.getCapturedVar() == Parameter &&
                   Capture.getCaptureKind() == LCK_ByRef;
          }))
        return false;
      const auto Consumers = closureConsumers(Lambda, Context, {});
      return !Consumers.empty() && isCalledInScope(Consumers) &&
             llvm::all_of(Consumers, [&](const ClosureConsumer &Consumer) {
               return SM.isBeforeInTranslationUnit(
                   AssignedBy, Consumer.Site->getBeginLoc());
             });
    };
    bool Used = false;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || Used || isa<UnaryExprOrTypeTraitExpr, CXXNoexceptExpr>(S))
        return;
      if (AssignedBy.isValid() &&
          SM.isBeforeInTranslationUnit(AssignedBy, S->getBeginLoc()))
        return;
      if (const auto *Lambda = dyn_cast<LambdaExpr>(S);
          Lambda && RunsOnlyAfterAssigned(Lambda))
        return;
      if (const auto *Assign = dyn_cast<BinaryOperator>(S);
          Value && Assign && Assign->getOpcode() == BO_Assign &&
          IsParameter(Assign->getLHS()))
        return Visit(Assign->getRHS());
      // Binding a reference to it reads nothing.
      if (const auto *Declaration = dyn_cast<DeclStmt>(S);
          Declaration && llvm::all_of(Declaration->decls(), [&](const Decl *D) {
            return Aliases.contains(dyn_cast<VarDecl>(D));
          }))
        return;
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
          Ref &&
          (Ref->getDecl() == Parameter ||
           Aliases.contains(dyn_cast<VarDecl>(Ref->getDecl()))) &&
          !readsOnlyOwnPart(Ref, Context)) {
        const Expr *Assigned =
            Value ? assignedValueAt(Parameter, Ref) : nullptr;
        if (!Assigned || Assigned == Parameter->getInit())
          Used = true;
      }
      for (const Stmt *Child : evaluatedChildren(S, Context))
        Visit(Child);
    };
    Visit(Body);
    return Used;
  };

  for (const ParmVarDecl *Parameter : Function->parameters()) {
    // The explicit object of a member function of an actor, `this CApp
    // &_Self`, is its object, like `this`, which the actor keeps.
    if (Parameter->isExplicitObjectParameter() && ActorClass)
      continue;
    const QualType Type = Parameter->getType();
    const bool Reference = Type->isReferenceType();
    const bool Pointer = Type->isPointerType() && !Type->isFunctionPointerType();
    if (!Reference && !(CheckPointers && Pointer) && !isViewType(Type, Context))
      continue;
    const SourceLocation Loc = reportLocation(
        Parameter->getIdentifier() ? Parameter->getLocation()
                                   : Parameter->getBeginLoc(),
        Context.getSourceManager());
    if (Loc.isInvalid())
      continue;
    if (Reference && ActorClass) {
      diag(Loc, "TCFuture coroutine %0 is a member function of actor %1 and "
                "takes %2; actor calls move the arguments into the coroutine "
                "once, so take it by value")
          << diagnosticDecl(Function) << diagnosticDecl(ActorClass)
          << diagnosticType(Type, Context);
      continue;
    }
    // A pointer or view of one, which an actor call copies into the coroutine
    // that it starts later, after what it points to can be gone, unless it is
    // to an actor or what allows it, which is used like `this`.
    if (ActorClass && !refersToSafeThisClass(Type)) {
      if (UsedIn(Parameter))
        diag(Loc, "TCFuture coroutine %0 is a member function of actor %1 and "
                  "takes %2, whose data can be gone when an actor call starts "
                  "the coroutine, after the call returns; take the data by "
                  "value")
            << diagnosticDecl(Function) << diagnosticDecl(ActorClass)
            << diagnosticType(Type, Context);
      continue;
    }
    const Stmt *Use = borrowedUse(Function, Parameter, Context);
    if (!Use)
      continue;
    diag(Loc, "TCFuture coroutine %0 uses %1 after a suspension, but it takes "
              "it as %2, which refers to data the caller owns, which can be "
              "gone by then; use it only before the first suspension, take the "
              "data by value, or return a TCUnsafeFuture")
        << diagnosticDecl(Function) << Parameter
        << diagnosticType(Type, Context);
    diag(Use->getBeginLoc(), "%0 is used here after a suspension, or in a way that keeps a reference to it",
         DiagnosticIDs::Note)
        << Parameter;
  }
}

} // namespace clang::tidy::malterlib
