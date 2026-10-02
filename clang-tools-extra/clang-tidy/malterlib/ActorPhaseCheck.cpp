//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ActorPhaseCheck.h"
#include "../utils/ASTUtils.h"
#include "ActorAnnotations.h"
#include "CoroutineUtils.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/ScopeExit.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

ActorPhaseCheck::ActorPhaseCheck(StringRef Name, ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      RemovingMethods(listOption("RemovingMethods",
                                 CVocabulary::builtIn("RemovingMethods"))),
      IteratorMethods(listOption("IteratorMethods", BuiltInIteratorMethods)),
      FreshKeyFunctions(listOption("FreshKeyFunctions", "")) {
  splitOptionList(RemovingMethods, RemovingMethodList);
  splitOptionList(IteratorMethods, IteratorMethodList);
  splitOptionList(FreshKeyFunctions, FreshKeyFunctionList);
}

/// Whether the address \p Address, through parentheses and casts, is cast
/// to a pointer that can change what it points to, also after a cast that
/// Whether \p Operator is one that Malterlib's collections use to add
/// entries, `+=` and `|=`, like `TCMap::operator+=`, which keeps those it has.
static bool isAddingOperator(OverloadedOperatorKind Operator) {
  return Operator == OO_PlusEqual || Operator == OO_PipeEqual;
}

/// Whether \p Operator is a binary one that makes a new value of its
/// operands, `mp_Items - _Other` or `mp_Items == _Other`, rather than changing
/// the left one, like `-=` does, also when its object isn't const, `operator-
/// (this Self &&_Self, ...)` of a `TCSet`.
static bool isValueOperator(OverloadedOperatorKind Operator) {
  switch (Operator) {
  case OO_Plus:
  case OO_Minus:
  case OO_Star:
  case OO_Slash:
  case OO_Percent:
  case OO_Caret:
  case OO_Amp:
  case OO_Pipe:
  case OO_EqualEqual:
  case OO_ExclaimEqual:
  case OO_Less:
  case OO_Greater:
  case OO_LessEqual:
  case OO_GreaterEqual:
  case OO_Spaceship:
  case OO_AmpAmp:
  case OO_PipePipe:
    return true;
  default:
    return false;
  }
}

/// adds const, `const_cast<bool *>(static_cast<bool const *>(&bFlag))`.
static bool castToChangeable(const Expr *Address, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Address);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    const auto *Cast = Parents[0].get<ExplicitCastExpr>();
    if (!Cast && !Parents[0].get<ParenExpr>() &&
        !Parents[0].get<ImplicitCastExpr>())
      return false;
    if (Cast) {
      if (!Cast->getType()->isPointerType())
        return false;
      if (!Cast->getType()->getPointeeType().isConstQualified())
        return true;
    }
    Node = Parents[0];
  }
  return false;
}

void ActorPhaseCheck::storeCheckOptions(ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "RemovingMethods", RemovingMethods);
  Options.store(Opts, "IteratorMethods", IteratorMethods);
  Options.store(Opts, "FreshKeyFunctions", FreshKeyFunctions);
}

void ActorPhaseCheck::registerMatchers(MatchFinder *Finder) {
  // What refers to an annotated function, and to an annotated field. Whether
  // it is evaluated is told in `check`, after what it refers to is known to be
  // annotated, since most references aren't, and telling it walks the parents.
  Finder->addMatcher(declRefExpr(to(functionDecl())).bind("functionRef"), this);
  Finder->addMatcher(memberExpr().bind("member"), this);
  // A structured binding of an annotated member, like `Items` of `auto
  // &[Items, bFlag] = m_State`.
  Finder->addMatcher(declRefExpr(to(bindingDecl())).bind("bindingRef"), this);
  // A pointer to an annotated data member, like `&CApp::m_bDestroyed`.
  Finder->addMatcher(declRefExpr(to(fieldDecl())).bind("fieldRef"), this);
  // The const cast away from what a getter gives back of an annotated
  // member, `const_cast<bool &>(f_Status())`, also by a C-style cast,
  // `(bool &)f_Status()`.
  Finder->addMatcher(
      explicitCastExpr(anyOf(cxxConstCastExpr(), cStyleCastExpr()),
                       unless(isInUnevaluatedContext()))
          .bind("constCast"),
      this);
  // A static flag, named without an object, also through its definition
  // outside of its class, which isn't declared `static`.
  Finder->addMatcher(
      declRefExpr(to(varDecl(hasStaticStorageDuration()))).bind("variableRef"),
      this);
  // Assigning a whole object that holds a phase flag, like `mp_State = {}`.
  Finder->addMatcher(
      cxxOperatorCallExpr(isAssignmentOperator(),
                          unless(isInUnevaluatedContext()))
          .bind("assignment"),
      this);
  Finder->addMatcher(binaryOperator(isAssignmentOperator(),
                                    unless(isInUnevaluatedContext()))
                         .bind("assignment"),
                     this);
  // Called by name, like `mp_State.operator=({})`.
  Finder->addMatcher(cxxMemberCallExpr(callee(cxxMethodDecl(
                                           hasOverloadedOperatorName("="))),
                                       unless(isInUnevaluatedContext()))
                         .bind("assignment"),
                     this);
  // Moving a whole object out, like `CState Old = fg_Move(mp_State)`, which
  // takes the entries of the collections it holds.
  Finder->addMatcher(cxxConstructExpr(hasDeclaration(cxxConstructorDecl(
                                          isMoveConstructor())),
                                      unless(isInUnevaluatedContext()))
                         .bind("moveFrom"),
                     this);
  // Swapping objects, `std::swap(mp_State, Other)`, assigns each of them, and
  // exchanging one, `std::exchange(mp_State, CState{})`, assigns the first.
  Finder->addMatcher(
      callExpr(
          callee(functionDecl(hasAnyName(vocabulary().SwapFunctions.names()))),
          argumentCountIs(2), unless(isInUnevaluatedContext()))
          .bind("swap"),
      this);
  // Also `std::ranges::swap(mp_State, Other)`, a call of the object that
  // customizes it.
  Finder->addMatcher(
      cxxOperatorCallExpr(hasOverloadedOperatorName("()"), argumentCountIs(3),
                          hasArgument(0, ignoringParenImpCasts(declRefExpr(
                                             to(varDecl(hasName("swap")))))),
                          unless(isInUnevaluatedContext()))
          .bind("swap"),
      this);
  Finder->addMatcher(
      callExpr(callee(functionDecl(
                   hasAnyName(vocabulary().ExchangeFunctions.names()))),
               argumentCountIs(2), unless(isInUnevaluatedContext()))
          .bind("exchange"),
      this);
  // Clearing a pointer that owns an object holding annotated collections,
  // like `mp_pState.f_Clear()` of a `TCUniquePointer<CState>`, destroys them,
  // and detaching it, `mp_pState.f_Detach()`, gives them to what can. So does
  // making a new one in its place, `mp_State.f_CreateNew()` of a
  // `TCOptional<CState>`, which destroys the one it holds first.
  Finder->addMatcher(
      cxxMemberCallExpr(callee(cxxMethodDecl(hasAnyName(
                            "f_Clear", "clear", "reset", "f_Reset", "f_Detach",
                            "release", "f_CreateNew", "emplace"))),
                        unless(isInUnevaluatedContext()))
          .bind("clearOwner"),
      this);
  // Destroying an object that holds them, `mp_State.~CState()`.
  Finder->addMatcher(cxxMemberCallExpr(callee(cxxDestructorDecl()),
                                       unless(isInUnevaluatedContext()))
                         .bind("destroy"),
                     this);
  // Making one again where it is, `new (&mp_State) CState{}` or
  // `std::construct_at(&mp_State)`, or destroying it, `std::destroy_at(
  // &mp_State)`.
  Finder->addMatcher(cxxNewExpr(unless(isInUnevaluatedContext()))
                         .bind("placement"),
                     this);
  // Also writing its bytes, `memset(&mp_State, 0, sizeof(mp_State))` or
  // `fg_MemClear(mp_State)`.
  Finder->addMatcher(callExpr(callee(functionDecl(hasAnyName(
                                  "::std::construct_at", "::std::destroy_at",
                                  "memset", "memcpy", "memmove", "bzero",
                                  "__builtin_memset", "__builtin_memcpy",
                                  "__builtin_memmove", "fg_MemClear",
                                  "fg_MemCopy", "fg_MemMove"))),
                              unless(isInUnevaluatedContext()))
                         .bind("placement"),
                     this);
  // An override restricted to phases, which callers of what it overrides
  // don't know about.
  Finder->addMatcher(cxxMethodDecl(isOverride()).bind("override"), this);
}

/// Where a pointer that the call \p Call gives back, the address of what it
/// is given as \p Child, `T const *fg_Pointer(T const &_Value) { return
/// &_Value; }`, can change it: the call, when it points to what isn't const,
/// or where the const is cast away, `const_cast<bool *>(fg_Pointer(Flag))`.
/// Null when it can't.
static const Expr *mutableAddressOf(const CallExpr *Call, const Expr *Child,
                                    ASTContext &Context) {
  if (!Call->getType()->isPointerType() ||
      !llvm::is_contained(returnedArguments(Call, /*Addressed=*/true), Child))
    return nullptr;
  if (!Call->getType()->getPointeeType().isConstQualified())
    return Call;
  DynTypedNode Node = DynTypedNode::create(*Call);
  for (int Depth = 0; Depth < 8; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    const auto *Cast = Parents[0].get<CastExpr>();
    if (!Cast && !Parents[0].get<ParenExpr>())
      return nullptr;
    if (Cast && Cast->getType()->isPointerType() &&
        !Cast->getType()->getPointeeType().isConstQualified())
      return Cast;
    if (Cast && !Cast->getType()->isPointerType())
      return nullptr;
    Node = Parents[0];
  }
  return nullptr;
}

/// The parent of \p E, through the implicit nodes and parentheses that don't
/// change what it designates.
static DynTypedNode parentOf(const Expr *E, ASTContext &Context,
                             const Expr *&Child) {
  DynTypedNode Node = DynTypedNode::create(*E);
  Child = E;
  // As many as the code has: each step goes up, and parentheses can nest
  // deeply.
  for (int Depth = 0; Depth < 4096; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return {};
    const DynTypedNode &Parent = Parents[0];
    if (const auto *Cast = Parent.get<ImplicitCastExpr>();
        Cast && Cast->getCastKind() != CK_LValueToRValue) {
      Child = Cast;
      Node = Parent;
      continue;
    }
    if (const auto *Paren = Parent.get<ParenExpr>()) {
      Child = Paren;
      Node = Parent;
      continue;
    }
    // A designated initializer, `{.m_Items = Items}`, initializes in the
    // list it is in what it designates, which the list's semantic form has
    // as it is.
    if (Parent.get<DesignatedInitExpr>()) {
      Node = Parent;
      continue;
    }
    // The right side of a comma designates the same object.
    if (const auto *Comma = Parent.get<BinaryOperator>();
        Comma && Comma->getOpcode() == BO_Comma &&
        Comma->getRHS()->IgnoreParenImpCasts() == Child->IgnoreParenImpCasts()) {
      Child = Comma;
      Node = Parent;
      continue;
    }
    // A cast to a reference designates the same object, like
    // `static_cast<TCMap<...> &>(mp_Items)`.
    if (const auto *Cast = Parent.get<ExplicitCastExpr>();
        Cast && Cast->isGLValue()) {
      Child = Cast;
      Node = Parent;
      continue;
    }
    if (const auto *Materialize = Parent.get<MaterializeTemporaryExpr>()) {
      Child = Materialize;
      Node = Parent;
      continue;
    }
    // A temporary with a destructor, like an iterator, is the same object.
    if (const auto *Bind = Parent.get<CXXBindTemporaryExpr>()) {
      Child = Bind;
      Node = Parent;
      continue;
    }
    if (const auto *Cleanups = Parent.get<ExprWithCleanups>()) {
      Child = Cleanups;
      Node = Parent;
      continue;
    }
    // Either value of a conditional that designates it, like `(b ? x : y)`.
    if (const auto *Conditional = Parent.get<AbstractConditionalOperator>();
        Conditional && Conditional->isGLValue() &&
        (Conditional->getTrueExpr()->IgnoreParenImpCasts() ==
             Child->IgnoreParenImpCasts() ||
         Conditional->getFalseExpr()->IgnoreParenImpCasts() ==
             Child->IgnoreParenImpCasts())) {
      // Not the one a constant condition doesn't select, `(false ? x : y)`,
      // which is never used.
      if (bool Selected = false;
          Child != Conditional->getCond() &&
          !Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(Selected,
                                                             Context) &&
          (Selected ? Conditional->getTrueExpr() : Conditional->getFalseExpr())
                  ->IgnoreParenImpCasts() != Child->IgnoreParenImpCasts())
        return {};
      Child = Conditional;
      Node = Parent;
      continue;
    }
    return Parent;
  }
  return {};
}

/// The type that a `return` or `co_return` in \p Parent gives the caller of
/// \p Use's function: its return type, or the value of its future. Null when
/// \p Parent is neither.
static QualType returnedType(const DynTypedNode &Parent, const Expr *Use,
                             ASTContext &Context) {
  const bool Return = Parent.get<ReturnStmt>() != nullptr;
  const bool Coreturn = Parent.get<CoreturnStmt>() != nullptr;
  if (!Return && !Coreturn)
    return QualType();
  // The function it returns from, also a lambda.
  const FunctionDecl *Enclosing = nullptr;
  DynTypedNode Node = DynTypedNode::create(*Use);
  for (int Depth = 0; Depth < 256 && !Enclosing; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return QualType();
    Node = Parents[0];
    if (const auto *Lambda = Node.get<LambdaExpr>())
      Enclosing = Lambda->getCallOperator();
    else
      Enclosing = Node.get<FunctionDecl>();
  }
  if (!Enclosing)
    return QualType();
  return Coreturn ? futureValueType(Enclosing->getReturnType())
                  : Enclosing->getReturnType();
}

/// The collections declared with DMibActorEntries that an object of \p Type
/// holds, also in its members and bases.
static void heldCollections(QualType Type,
                            llvm::SmallVectorImpl<const FieldDecl *> &Held);

/// The phase flag that an object of \p Type holds, also in the objects it
/// holds by value or owns through an owning pointer.
static const FieldDecl *heldPhaseFlag(QualType Type);

static QualType ownedType(const CXXRecordDecl *Record);

void ActorPhaseCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  ASTContext &Context = *Result.Context;
  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  for (const auto &[Id, Node] : Result.Nodes.getMap())
    if (isInTemplatePattern(Node, Context))
      return;
  auto Unevaluated = [&](const Stmt *S) { return isNeverRun(S, Context); };
  if (const auto *Ref = Result.Nodes.getNodeAs<DeclRefExpr>("functionRef")) {
    const auto *Function = cast<FunctionDecl>(Ref->getDecl());
    if (std::optional<unsigned> Phases =
            annotatedPhases(Function, AnnotationActorPhase);
        Phases && !Unevaluated(Ref))
      checkPhaseUse(Ref, Function, *Phases & ~ActorPhase_Owner, Context);
    return;
  }
  if (const auto *Cast = Result.Nodes.getNodeAs<ExplicitCastExpr>("constCast")) {
    // A C-style cast that casts const away, to a reference or pointer.
    if (isa<CStyleCastExpr>(Cast)) {
      const QualType To = Cast->getTypeAsWritten();
      const QualType From = Cast->getSubExpr()->IgnoreParenImpCasts()->getType();
      auto Pointee = [](QualType Type) {
        return Type->isPointerType() ? Type->getPointeeType()
                                     : Type.getNonReferenceType();
      };
      if ((!To->isPointerType() && !To->isReferenceType()) ||
          !Pointee(From).isConstQualified() || Pointee(To).isConstQualified())
        return;
    }
    // A getter whose body can be seen, which returns only a member of its
    // object, `bool const &f_Status() const { return m_bStopping; }`, also
    // through a local reference to what it gives, `auto const &Alias =
    // f_Items();`.
    // Also what a pointer it gives points to, `*f_Items()`, and a local
    // pointer to it, `auto pItems = f_Items();`, what it holds there.
    const Expr *Source = Cast->getSubExpr()->IgnoreParenImpCasts();
    for (int Depth = 0; Depth < 8; ++Depth) {
      if (const auto *Deref = dyn_cast<UnaryOperator>(Source);
          Deref && Deref->getOpcode() == UO_Deref) {
        Source = Deref->getSubExpr()->IgnoreParenImpCasts();
        continue;
      }
      const auto *Ref = dyn_cast<DeclRefExpr>(Source);
      const auto *Alias = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      if (!Alias || !Alias->hasLocalStorage() || isa<ParmVarDecl>(Alias) ||
          !Alias->getInit())
        break;
      const Expr *Value = Alias->getType()->isReferenceType() ? Alias->getInit()
                          : Alias->getType()->isPointerType()
                              ? assignedValueAt(Alias, Ref)
                              : nullptr;
      if (!Value)
        break;
      Source = Value->IgnoreParenImpCasts();
    }
    const auto *Getter = dyn_cast<CXXMemberCallExpr>(Source);
    const CXXMethodDecl *Method = Getter ? Getter->getMethodDecl() : nullptr;
    const FunctionDecl *Definition = Method ? Method->getDefinition() : nullptr;
    const Stmt *Body = Definition ? Definition->getBody() : nullptr;
    // Each of its returns, also through a local reference to the member,
    // `bool const &Alias = m_bStopping; return Alias;`, gives the same one.
    const FieldDecl *Field = nullptr;
    bool Other = false;
    std::function<void(const Stmt *)> Returns = [&](const Stmt *S) {
      if (!S || Other || isa<LambdaExpr>(S))
        return;
      if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
        const Expr *Returned =
            Return->getRetValue()
                ? Return->getRetValue()->IgnoreParenImpCasts()
                : nullptr;
        for (int Depth = 0; Returned && Depth < 8; ++Depth) {
          const auto *Ref = dyn_cast<DeclRefExpr>(Returned);
          const auto *Alias = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          if (!Alias || !Alias->hasLocalStorage() ||
              !Alias->getType()->isReferenceType() || !Alias->getInit())
            break;
          Returned = Alias->getInit()->IgnoreParenImpCasts();
        }
        if (const auto *Address = dyn_cast_or_null<UnaryOperator>(Returned);
            Address && Address->getOpcode() == UO_AddrOf)
          Returned = Address->getSubExpr()->IgnoreParenImpCasts();
        const auto *Member = dyn_cast_or_null<MemberExpr>(Returned);
        const auto *Returning =
            Member &&
                    isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts())
                ? dyn_cast<FieldDecl>(Member->getMemberDecl())
                : nullptr;
        if (!Returning || (Field && Field != Returning))
          Other = true;
        else
          Field = Returning;
        return;
      }
      for (const Stmt *Child : childrenOnce(S))
        Returns(Child);
    };
    Returns(Body);
    if (!Field || Other)
      return;
    // What is done through the cast is done with the member of the object
    // the getter is called on.
    const Expr *Through = Cast;
    if (Cast->getType()->isPointerType())
      if (const auto *Deref = parentOf(Cast, Context, Through)
                                  .get<UnaryOperator>();
          Deref && Deref->getOpcode() == UO_Deref)
        Through = Deref;
    if (annotatedPhases(Field, AnnotationActorPhaseFlag))
      checkFlagUse(Through, Field, Context);
    else if (std::optional<unsigned> Phases =
                 annotatedPhases(Field, AnnotationActorEntries))
      checkEntriesUse(Through, Field, *Phases, Context, 0,
                      Getter->getImplicitObjectArgument());
    return;
  }
  if (const auto *Ref = Result.Nodes.getNodeAs<DeclRefExpr>("fieldRef")) {
    // Through a pointer to a non-const member, the flag can be cleared and the
    // collection changed anywhere, also on another object.
    const auto *Field = cast<FieldDecl>(Ref->getDecl());
    const bool Flag = annotatedPhases(Field, AnnotationActorPhaseFlag).has_value();
    if ((!Flag && !annotatedPhases(Field, AnnotationActorEntries)) ||
        Unevaluated(Ref))
      return;
    const Expr *Child = nullptr;
    const auto *Address = parentOf(Ref, Context, Child).get<UnaryOperator>();
    if (!Address || Address->getOpcode() != UO_AddrOf)
      return;
    // As what it is converted to, like `bool const CApp::*`.
    const Expr *Converted = nullptr;
    const DynTypedNode Held = parentOf(Address, Context, Converted);
    const auto *Pointer = Converted->getType()->getAs<MemberPointerType>();
    if (!Pointer)
      return;
    // To const, kept in a local whose const is cast away, `this->*
    // const_cast<bool CApp::*>(pFlag) = false`, which changes it too.
    if (Pointer->getPointeeType().isConstQualified()) {
      const auto *Local = Held.get<VarDecl>();
      const auto *Function =
          Local && Local->hasLocalStorage()
              ? dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod())
              : nullptr;
      if (!Function || !Function->getBody())
        return;
      // Also of a copy of it, `auto pCopy = pFlag`, also one assigned it.
      llvm::SmallPtrSet<const ValueDecl *, 4> Copies{Local};
      for (bool Added = true; Added && Copies.size() < 64;) {
        Added = false;
        std::function<void(const Stmt *)> Collect = [&](const Stmt *S) {
          if (!S)
            return;
          auto IsCopy = [&](const Expr *E) {
            const auto *From =
                E ? dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts()) : nullptr;
            return From && Copies.contains(From->getDecl());
          };
          if (const auto *Declaration = dyn_cast<DeclStmt>(S))
            for (const Decl *Declared : Declaration->decls())
              if (const auto *Copy = dyn_cast<VarDecl>(Declared);
                  Copy && IsCopy(Copy->getInit()))
                Added |= Copies.insert(Copy).second;
          if (const auto *Assign = dyn_cast<BinaryOperator>(S);
              Assign && Assign->getOpcode() == BO_Assign &&
              IsCopy(Assign->getRHS()))
            if (const auto *Target = dyn_cast<DeclRefExpr>(
                    Assign->getLHS()->IgnoreParenImpCasts()))
              Added |= Copies.insert(Target->getDecl()).second;
          for (const Stmt *Child : childrenOnce(S))
            Collect(Child);
        };
        Collect(Function->getBody());
      }
      bool CastAway = false;
      std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
        if (!S || CastAway)
          return;
        if (const auto *Cast = dyn_cast<ExplicitCastExpr>(S))
          if (const auto *To = Cast->getType()->getAs<MemberPointerType>();
              To && !To->getPointeeType().isConstQualified())
            if (const auto *Ref = dyn_cast<DeclRefExpr>(
                    Cast->getSubExpr()->IgnoreParenImpCasts());
                Ref && Copies.contains(Ref->getDecl()))
              CastAway = true;
        for (const Stmt *Child : childrenOnce(S))
          Visit(Child);
      };
      Visit(Function->getBody());
      if (!CastAway)
        return;
    }
    diag(Ref->getBeginLoc(),
         "a pointer to %0, %select{the flag of a phase|a collection declared "
         "with DMibActorEntries}1, is taken here, through which it can be "
         "changed anywhere; use the member directly, or take a pointer to "
         "const")
        << Field << (Flag ? 0 : 1);
    return;
  }
  if (const auto *Ref = Result.Nodes.getNodeAs<DeclRefExpr>("bindingRef")) {
    const auto *Binding = cast<BindingDecl>(Ref->getDecl());
    // A binding of a copy, `auto [Items] = mp_State`, is its own.
    if (const auto *Decomposition =
            dyn_cast_or_null<VarDecl>(Binding->getDecomposedDecl());
        Decomposition && !Decomposition->getType()->isReferenceType())
      return;
    const auto *Member =
        Binding->getBinding()
            ? dyn_cast<MemberExpr>(Binding->getBinding()->IgnoreParenImpCasts())
            : nullptr;
    const auto *Field =
        Member ? dyn_cast<FieldDecl>(Member->getMemberDecl()) : nullptr;
    if (!Field ||
        (!annotatedPhases(Field, AnnotationActorPhaseFlag) &&
         !annotatedPhases(Field, AnnotationActorEntries)) ||
        Unevaluated(Ref))
      return;
    if (annotatedPhases(Field, AnnotationActorPhaseFlag))
      checkFlagUse(Ref, Field, Context);
    else if (std::optional<unsigned> Phases =
                 annotatedPhases(Field, AnnotationActorEntries))
      checkEntriesUse(Ref, Field, *Phases, Context, 0, Member->getBase());
    return;
  }
  if (const auto *Ref = Result.Nodes.getNodeAs<DeclRefExpr>("variableRef")) {
    if (annotatedPhases(Ref->getDecl(), AnnotationActorPhaseFlag) &&
        !Unevaluated(Ref))
      checkFlagUse(Ref, Ref->getDecl(), Context);
    return;
  }
  if (const auto *Move = Result.Nodes.getNodeAs<CXXConstructExpr>("moveFrom")) {
    if (Move->getNumArgs() < 1)
      return;
    // What is moved: given to `fg_Move`, `std::move` or cast to an rvalue.
    const Expr *Source = Move->getArg(0)->IgnoreParenImpCasts();
    if (const auto *Call = dyn_cast<CallExpr>(Source);
        Call && Call->getNumArgs() == 1 && (isMoveFunction(calleeName(Call))))
      Source = Call->getArg(0)->IgnoreParenImpCasts();
    else if (const auto *Cast = dyn_cast<CXXStaticCastExpr>(Source))
      Source = Cast->getSubExpr()->IgnoreParenImpCasts();
    // Each once, which a chain of conditionals of the same aliases, `auto
    // &rNext = bChoose ? rPrevious : rPrevious`, meets a number of times that
    // doubles with each.
    llvm::SmallPtrSet<const Expr *, 16> Checked;
    std::function<void(const Expr *, int)> CheckSource = [&](const Expr *Source,
                                                             int Depth) {
      Source = Source->IgnoreParenImpCasts();
      if (!Checked.insert(Source).second)
        return;
      // Also through local references to it, `auto &State = mp_State`, each
      // bound to one declared before it, and either of a conditional,
      // `fg_Move(bChoose ? mp_First : mp_Second)`.
      for (int iAlias = 0; iAlias < 64; ++iAlias) {
        const auto *Ref = dyn_cast<DeclRefExpr>(Source);
        const auto *Alias = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        if (!Alias || !Alias->hasLocalStorage() ||
            !Alias->getType()->isReferenceType() || !Alias->getInit())
          break;
        Source = Alias->getInit()->IgnoreParenImpCasts();
      }
      if (const auto *Conditional = dyn_cast<ConditionalOperator>(Source);
          Conditional && Conditional->isGLValue() && Depth < 256) {
        CheckSource(Conditional->getTrueExpr(), Depth + 1);
        CheckSource(Conditional->getFalseExpr(), Depth + 1);
        return;
      }
      // A whole object, not a collection itself, whose moves are its uses,
      // also through an owning pointer, `fg_Move(*mp_pState)`.
      const Expr *Moved = Source;
      if (const auto *Deref = dyn_cast<UnaryOperator>(Moved);
          Deref && Deref->getOpcode() == UO_Deref)
        Moved = Deref->getSubExpr()->IgnoreParenImpCasts();
      else if (const auto *Star = dyn_cast<CXXOperatorCallExpr>(Moved);
               Star && Star->getOperator() == OO_Star &&
               Star->getNumArgs() == 1)
        Moved = Star->getArg(0)->IgnoreParenImpCasts();
      // Also through a local pointer to the member, `auto *pState = &mp_State;
      // fg_Move(*pState)`, what it points to where it is used.
      for (int iPointer = 0; iPointer < 4; ++iPointer) {
        const auto *Ref = dyn_cast<DeclRefExpr>(Moved);
        const auto *Pointer = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        if (!Pointer || !Pointer->hasLocalStorage() ||
            !Pointer->getType()->isPointerType() || isa<ParmVarDecl>(Pointer))
          break;
        const Expr *Value = assignedValueAt(Pointer, Ref);
        if (!Value)
          break;
        Value = Value->IgnoreParenImpCasts();
        if (const auto *Address = dyn_cast<UnaryOperator>(Value);
            Address && Address->getOpcode() == UO_AddrOf)
          Moved = Address->getSubExpr()->IgnoreParenImpCasts();
        else
          Moved = Value;
      }
      // A member of the object, or what a helper is given by reference that
      // can be it, `State stolen(fg_Move(_State))` of `void fg_Steal(State
      // &_State)`.
      const auto *Member = dyn_cast<MemberExpr>(Moved);
      const auto *Given = dyn_cast<DeclRefExpr>(Moved);
      const auto *Parameter =
          Given ? dyn_cast<ParmVarDecl>(Given->getDecl()) : nullptr;
      if (!(Member && !annotatedPhases(Member->getMemberDecl(),
                                       AnnotationActorEntries)) &&
          !(Parameter && Parameter->getType()->isLValueReferenceType() &&
            !Parameter->getType().getNonReferenceType().isConstQualified()))
        return;
      llvm::SmallVector<const FieldDecl *, 4> Collections;
      heldCollections(Source->getType().getNonReferenceType(), Collections);
      // Also the flag of a phase it holds, also through an owning pointer.
      const FieldDecl *Flag =
          heldPhaseFlag(Source->getType().getNonReferenceType());
      if (Collections.empty() && !Flag)
        return;
      if (const FunctionDecl *Enclosing = enclosingPhaseFunction(Move, Context);
          Enclosing && Enclosing->isDefaulted())
        return;
      for (const FieldDecl *Collection : Collections)
        checkChange(Move, Source, nullptr, Collection,
                    *annotatedPhases(Collection, AnnotationActorEntries),
                    Context);
      if (Flag)
        diag(
            Move->getBeginLoc(),
            "moving the object out takes %0, the flag of a phase, which is "
            "only ever set to true, so that what runs in the phase can rely on "
            "it staying set")
            << Flag;
    };
    CheckSource(Source, 0);
    return;
  }
  if (const auto *Clear =
          Result.Nodes.getNodeAs<CXXMemberCallExpr>("clearOwner")) {
    const Expr *Owner = Clear->getImplicitObjectArgument();
    const CXXRecordDecl *Record =
        Owner ? Owner->getType().getNonReferenceType()->getAsCXXRecordDecl()
              : nullptr;
    // Only of a pointer that owns the object, which `operator->` reaches.
    if (Record && Record->hasDefinition() &&
        !ownedType(Record->getDefinition()).isNull())
      checkFlagHolderAssignment(Clear, Owner, Context);
    return;
  }
  if (const auto *Destroy =
          Result.Nodes.getNodeAs<CXXMemberCallExpr>("destroy")) {
    if (const Expr *Object = Destroy->getImplicitObjectArgument())
      checkFlagHolderAssignment(Destroy, Object, Context);
    return;
  }
  if (const auto *Placement = Result.Nodes.getNodeAs<Expr>("placement")) {
    const Expr *Address = nullptr;
    if (const auto *New = dyn_cast<CXXNewExpr>(Placement))
      Address = New->getNumPlacementArgs() >= 1 ? New->getPlacementArg(0)
                                                : nullptr;
    else if (const auto *Call = dyn_cast<CallExpr>(Placement))
      Address = Call->getNumArgs() >= 1 ? Call->getArg(0) : nullptr;
    // Also through a cast or a local pointer to it, `auto *pState = &m_State;
    // memset(pState, ...)`, where it is given.
    for (int Depth = 0; Address && Depth < 16; ++Depth) {
      const Expr *Inner = Address->IgnoreParenImpCasts();
      if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Inner)) {
        Address = Cast->getSubExpr();
        continue;
      }
      const auto *Ref = dyn_cast<DeclRefExpr>(Inner);
      const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
          !Local->getType()->isPointerType())
        break;
      const Expr *Value = assignedValueAt(Local, Ref);
      if (!Value)
        break;
      Address = Value;
    }
    const auto *Unary =
        Address ? dyn_cast<UnaryOperator>(Address->IgnoreParenImpCasts())
                : nullptr;
    if (Unary && Unary->getOpcode() == UO_AddrOf)
      checkFlagHolderAssignment(Placement, Unary->getSubExpr(), Context);
    // Given by reference, `fg_MemClear(mp_State)`.
    else if (Address && Address->IgnoreParenImpCasts()->isGLValue() &&
             Address->IgnoreParenImpCasts()->getType()->isRecordType())
      checkFlagHolderAssignment(Placement, Address->IgnoreParenImpCasts(),
                                Context);
    return;
  }
  if (const auto *Swap = Result.Nodes.getNodeAs<CallExpr>("swap")) {
    // Not the swap of a class, a member function of it, which is checked
    // where it assigns its members.
    if (const auto [Left, Right] = swappedArguments(Swap); Left) {
      checkFlagHolderAssignment(Swap, Left, Context);
      checkFlagHolderAssignment(Swap, Right, Context);
    }
    return;
  }
  if (const auto *Exchange = Result.Nodes.getNodeAs<CallExpr>("exchange")) {
    checkFlagHolderAssignment(Exchange, Exchange->getArg(0), Context);
    return;
  }
  if (const auto *Assignment = Result.Nodes.getNodeAs<Expr>("assignment")) {
    const Expr *Target = nullptr;
    if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(Assignment))
      Target = Call->getNumArgs() >= 1 ? Call->getArg(0) : nullptr;
    else if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Assignment))
      Target = Call->getImplicitObjectArgument();
    else if (const auto *Binary = dyn_cast<BinaryOperator>(Assignment))
      Target = Binary->getLHS();
    if (Target)
      checkFlagHolderAssignment(Assignment, Target, Context);
    return;
  }
  if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("override")) {
    // Also of an instantiation, `CApp<CBase>::f_Clear`, whose template can't
    // tell what it overrides, which is reported where it is written.
    if (!Method->isFirstDecl())
      return;
    // Its phases, also annotated on a later declaration, like its definition,
    // or inherited from one of the functions it overrides: its body runs in
    // them, but callers of one that isn't annotated may call it in any phase.
    std::optional<unsigned> Phases;
    for (const FunctionDecl *Redeclaration : Method->redecls()) {
      if (std::optional<unsigned> Own =
              annotatedPhases(Redeclaration, AnnotationActorPhase))
        Phases = Phases.value_or(0) | *Own;
    }
    if (!Phases)
      return;
    for (const CXXMethodDecl *Overridden : Method->overridden_methods()) {
      if (annotatedPhases(Overridden, AnnotationActorPhase))
        continue;
      diag(Method->getLocation(),
           "%0 runs only in %1, but overrides %q2, which callers may call "
           "in any phase; declare %q2 with DMibActorPhase of the same phases")
          << diagnosticDecl(Method) << phaseNames(*Phases)
          << diagnosticDecl(Overridden);
      return;
    }
    return;
  }
  const auto *Member = Result.Nodes.getNodeAs<MemberExpr>("member");
  if (!Member)
    return;
  const ValueDecl *Decl = Member->getMemberDecl();
  // Only a member that is annotated, which most aren't.
  if (!(isa<CXXMethodDecl>(Decl) &&
        annotatedPhases(Decl, AnnotationActorPhase)) &&
      !annotatedPhases(Decl, AnnotationActorPhaseFlag) &&
      !(isa<FieldDecl>(Decl) && annotatedPhases(Decl, AnnotationActorEntries)))
    return;
  if (Unevaluated(Member))
    return;
  // What a defaulted copy or move does to the members is the copy or move of
  // the whole object, which isn't a use of a member.
  if (const FunctionDecl *Enclosing = enclosingPhaseFunction(Member, Context);
      Enclosing && Enclosing->isDefaulted())
    return;
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Decl)) {
    if (std::optional<unsigned> Phases =
            annotatedPhases(Method, AnnotationActorPhase)) {
      if (!isOwnActorObject(Member->isImplicitAccess() ? nullptr
                                                  : Member->getBase())) {
        diag(Member->getMemberLoc(),
             "%0 runs only in the %1 phase%s2 of its actor, but is used here "
             "on another object, which may not be in %select{that phase|those "
             "phases}3")
            << diagnosticDecl(Method) << phaseNames(*Phases & ~ActorPhase_Owner)
            << llvm::popcount(*Phases & ~ActorPhase_Owner)
            << (llvm::popcount(*Phases & ~ActorPhase_Owner) > 1);
        return;
      }
      checkPhaseUse(Member, Method, *Phases & ~ActorPhase_Owner, Context);
    }
    return;
  }
  if (annotatedPhases(Decl, AnnotationActorPhaseFlag)) {
    checkFlagUse(Member, Decl, Context);
    return;
  }
  if (const auto *Field = dyn_cast<FieldDecl>(Decl)) {
    if (std::optional<unsigned> Phases =
            annotatedPhases(Field, AnnotationActorEntries))
      checkEntriesUse(Member, Field, *Phases, Context, 0, nullptr);
  }
}

void ActorPhaseCheck::checkPhaseUse(const Expr *Use,
                                    const FunctionDecl *Function,
                                    unsigned Phases, ASTContext &Context) {
  // A pointer to a member function is only called right away on the own
  // object, like `(this->*&CApp::fp_DestroyAll)()`: one that escapes can be
  // called later, outside of the phase, or on another object.
  // One with an explicit object, `void f_Clear(this CApp &_This)`, is called
  // directly by its name, `f_Clear()`, on the object it is given first.
  bool CalledDirectly = false;
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      Method && Method->isExplicitObjectMemberFunction() &&
      isa<DeclRefExpr>(Use)) {
    const Expr *Child = nullptr;
    const auto *Call = parentOf(Use, Context, Child).get<CallExpr>();
    if (Call && Call->getCallee()->IgnoreParenImpCasts() ==
                    Child->IgnoreParenImpCasts() &&
        Call->getNumArgs() >= 1) {
      if (!isOwnActorObject(Call->getArg(0))) {
        diag(Use->getBeginLoc(),
             "%0 runs only in the %1 phase%s2 of its actor, but is used here "
             "on another object, which may not be in %select{that phase|those "
             "phases}3")
            << diagnosticDecl(Function) << phaseNames(Phases) << llvm::popcount(Phases)
            << (llvm::popcount(Phases) > 1);
        return;
      }
      CalledDirectly = true;
    }
  }
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      Method && !Method->isStatic() && isa<DeclRefExpr>(Use) &&
      !CalledDirectly) {
    const Expr *Child = nullptr;
    DynTypedNode Parent = parentOf(Use, Context, Child);
    if (const auto *Address = Parent.get<UnaryOperator>();
        Address && Address->getOpcode() == UO_AddrOf)
      Parent = parentOf(Address, Context, Child);
    const auto *Binary = Parent.get<BinaryOperator>();
    if (!Binary || !Binary->isPtrMemOp() ||
        Binary->getRHS()->IgnoreParenImpCasts() != Child->IgnoreParenImpCasts()) {
      diag(Use->getBeginLoc(),
           "%0 runs only in the %1 phase%s2 of its actor, but a pointer to it "
           "is taken here, which can be called outside of %select{it|them}3 "
           "or on another object; call it directly")
          << diagnosticDecl(Function) << phaseNames(Phases) << llvm::popcount(Phases)
          << (llvm::popcount(Phases) > 1);
      return;
    }
    if (!isOwnActorObject(Binary->getLHS())) {
      diag(Use->getBeginLoc(),
           "%0 runs only in the %1 phase%s2 of its actor, but is used here on "
           "another object, which may not be in %select{that phase|those "
           "phases}3")
          << diagnosticDecl(Function) << phaseNames(Phases) << llvm::popcount(Phases)
          << (llvm::popcount(Phases) > 1);
      return;
    }
  }
  // A static or free function of the phase gets the actor as an argument,
  // like `fs_DestroyEntries(pThis)`: it must be the own object, which is in
  // the phase.
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      !Method || Method->isStatic()) {
    const Expr *Child = nullptr;
    DynTypedNode Parent = parentOf(Use, Context, Child);
    const auto *Call = Parent.get<CallExpr>();
    // A pointer to it can be called by another actor, outside of the phase.
    if (!Call || Call->getCallee()->IgnoreParenImpCasts() !=
                     Child->IgnoreParenImpCasts()) {
      diag(Use->getBeginLoc(),
           "%0 runs only in the %1 phase%s2 of its actor, but a pointer to it "
           "is taken here, which can be called outside of %select{it|them}3 "
           "or on another object; call it directly")
          << diagnosticDecl(Function) << phaseNames(Phases) << llvm::popcount(Phases)
          << (llvm::popcount(Phases) > 1);
      return;
    }
    {
      // The actor object that \p Type, a pointer or an object, is of.
      auto ActorOf = [](QualType Type) -> const CXXRecordDecl * {
        const CXXRecordDecl *Record = Type->isPointerType()
                                          ? Type->getPointeeCXXRecordDecl()
                                          : Type->getAsCXXRecordDecl();
        return Record && isActorClass(Record) ? Record : nullptr;
      };
      // Also what is part of another actor, `&pOther->m_State`, which isn't
      // in the phase either.
      // Also through a local alias of it, `auto *pState = &pOther->m_State`,
      // what it holds where it is given.
      std::function<bool(const Expr *, int)> OfAnotherActorAt =
          [&](const Expr *Argument, int Depth) {
            if (Depth > 16)
              return false;
            const Expr *Inner = Argument->IgnoreParenImpCasts();
            // Also cast, `static_cast<CState *>(&pOther->m_State)`, or either
            // value of a conditional, `bOwn ? &m_State : &pOther->m_State`.
            while (const auto *Cast = dyn_cast<ExplicitCastExpr>(Inner))
              Inner = Cast->getSubExpr()->IgnoreParenImpCasts();
            // Only the one a constant condition chooses, `true ? &m_State :
            // &pOther->m_State`.
            if (const auto *Conditional =
                    dyn_cast<AbstractConditionalOperator>(Inner)) {
              if (const Expr *Condition = Conditional->getCond();
                  !Condition->isValueDependent())
                if (std::optional<llvm::APSInt> Value =
                        Condition->getIntegerConstantExpr(Context))
                  return OfAnotherActorAt(Value->isZero()
                                              ? Conditional->getFalseExpr()
                                              : Conditional->getTrueExpr(),
                                          Depth + 1);
              return OfAnotherActorAt(Conditional->getTrueExpr(), Depth + 1) ||
                     OfAnotherActorAt(Conditional->getFalseExpr(), Depth + 1);
            }
            if (const auto *Ref = dyn_cast<DeclRefExpr>(Inner)) {
              const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
              if (!Local || !Local->hasLocalStorage() ||
                  isa<ParmVarDecl>(Local))
                return false;
              if (Local->getType()->isReferenceType())
                return Local->getInit() &&
                       OfAnotherActorAt(Local->getInit(), Depth + 1);
              if (const Expr *Value = assignedValueAt(Local, Ref))
                return OfAnotherActorAt(Value, Depth + 1);
              // Where what it holds there can't be told, `if (bOther) pState
              // = &pOther->m_State;`, any of what it is made from and
              // assigned.
              if (!Local->getType()->isPointerType())
                return false;
              if (Local->getInit() &&
                  OfAnotherActorAt(Local->getInit(), Depth + 1))
                return true;
              const auto *Function = dyn_cast_or_null<FunctionDecl>(
                  Local->getParentFunctionOrMethod());
              if (!Function || !Function->getBody())
                return false;
              for (const auto &Nodes : match(
                       findAll(binaryOperator(
                                   isAssignmentOperator(),
                                   hasLHS(ignoringParenImpCasts(declRefExpr(
                                       to(varDecl(equalsNode(Local)))))))
                                   .bind("assign")),
                       *Function->getBody(), Context))
                if (OfAnotherActorAt(
                        Nodes.getNodeAs<BinaryOperator>("assign")->getRHS(),
                        Depth + 1))
                  return true;
              return false;
            }
            // Moved along, `&_pOther->m_State + 0`, it points into the same.
            if (const auto *Offset = dyn_cast<BinaryOperator>(Inner);
                Offset && Offset->isAdditiveOp() &&
                Offset->getType()->isPointerType())
              return OfAnotherActorAt(
                  Offset->getLHS()->getType()->isPointerType()
                      ? Offset->getLHS()
                      : Offset->getRHS(),
                  Depth + 1);
            if (const auto *Address = dyn_cast<UnaryOperator>(Inner);
                Address && Address->getOpcode() == UO_AddrOf) {
              Inner = Address->getSubExpr()->IgnoreParenImpCasts();
              // The address of what a pointer points to, `&*pState`, is the
              // pointer, and of what a local reference refers to, `&State`,
              // the address of that.
              if (const auto *Deref = dyn_cast<UnaryOperator>(Inner);
                  Deref && Deref->getOpcode() == UO_Deref)
                return OfAnotherActorAt(Deref->getSubExpr(), Depth + 1);
              if (const auto *Ref = dyn_cast<DeclRefExpr>(Inner))
                if (const auto *Alias = dyn_cast<VarDecl>(Ref->getDecl());
                    Alias && Alias->hasLocalStorage() &&
                    Alias->getType()->isReferenceType() && Alias->getInit())
                  Inner = Alias->getInit()->IgnoreParenImpCasts();
            }
            // What a member function of it gives back, a pointer or a
            // reference into it, `_pOther->f_GetData()`.
            if (const auto *Getter = dyn_cast<CXXMemberCallExpr>(Inner);
                Getter && Getter->getImplicitObjectArgument() &&
                (Getter->getType()->isPointerType() || Getter->isGLValue())) {
              const Expr *Object = Getter->getImplicitObjectArgument();
              if (ActorOf(Object->getType()))
                return !isOwnActorObject(Object);
              Inner = Object->IgnoreParenImpCasts();
            }
            // Also an element of an array member, `&_pOther->m_States[0]`.
            for (int Level = 0; Level < 64; ++Level) {
              if (const auto *Member = dyn_cast<MemberExpr>(Inner)) {
                if (ActorOf(Member->getBase()->getType()))
                  return !isOwnActorObject(Member->getBase());
                Inner = Member->getBase()->IgnoreParenImpCasts();
              } else if (const auto *Element =
                             dyn_cast<ArraySubscriptExpr>(Inner);
                         Element && Element->getBase()
                                        ->IgnoreParenImpCasts()
                                        ->getType()
                                        ->isConstantArrayType())
                Inner = Element->getBase()->IgnoreParenImpCasts();
              else
                break;
            }
            return false;
          };
      auto OfAnotherActor = [&](const Expr *Argument) {
        return OfAnotherActorAt(Argument, 0);
      };
      for (const Expr *Argument : Call->arguments()) {
        if (ActorOf(Argument->getType()) ? isOwnActorObject(Argument)
                                         : !OfAnotherActor(Argument))
          continue;
        diag(Argument->getBeginLoc(),
             "%0 runs only in the %1 phase%s2 of its actor, but is given "
             "another object here, which may not be in %select{that phase|"
             "those phases}3")
            << diagnosticDecl(Function) << phaseNames(Phases) << llvm::popcount(Phases)
            << (llvm::popcount(Phases) > 1);
        return;
      }
    }
  }
  const FunctionDecl *Enclosing = enclosingPhaseFunction(Use, Context);
  // A default member initializer runs in the constructor, in no phase.
  if (!Enclosing) {
    diag(Use->getBeginLoc(),
         "%0 runs only in the %1 phase%s2 of the actor, but is used here "
         "outside of a function, like in a default member initializer, which "
         "runs outside of %select{it|them}3")
        << diagnosticDecl(Function) << phaseNames(Phases) << llvm::popcount(Phases)
        << (llvm::popcount(Phases) > 1);
    return;
  }
  const unsigned Current = contextPhases(Enclosing);
  if (Current && !(Current & ~Phases))
    return;
  diag(Use->getBeginLoc(),
       "%0 runs only in the %1 phase%s3 of the actor, but is used here in "
       "%2, which can run outside of %select{it|them}4; use it only from a "
       "function of %select{that phase|those phases}4 (DMibActorPhase), or "
       "from the entry of %select{the phase|a phase}4")
      << diagnosticDecl(Function) << phaseNames(Phases) << diagnosticDecl(Enclosing) << llvm::popcount(Phases)
      << (llvm::popcount(Phases) > 1);
}

/// Whether \p Child, an initializer of the aggregate \p List, `CAlias{...}`
/// or `CAlias(...)`, is bound to a non-const reference member.
static bool bindsMutableReferenceMember(const Expr *List, const Expr *Child) {
  llvm::ArrayRef<Expr *> Inits;
  // The initializers in the order of what they initialize, also designated
  // ones, `{.m_Items = Items}`.
  if (const auto *Written = dyn_cast_or_null<InitListExpr>(List);
      Written && Written->isSyntacticForm() && Written->getSemanticForm())
    List = Written->getSemanticForm();
  if (const auto *Braces = dyn_cast_or_null<InitListExpr>(List))
    Inits = llvm::ArrayRef<Expr *>(Braces->getInits(), Braces->getNumInits());
  else if (const auto *Parens = dyn_cast_or_null<CXXParenListInitExpr>(List))
    Inits = Parens->getInitExprs();
  else
    return false;
  const auto *Record = List->getType()->getAsCXXRecordDecl();
  if (!Record)
    return false;
  // The initializers of the bases come first.
  unsigned iInit = Record->getNumBases();
  for (const FieldDecl *Member : Record->fields()) {
    // An unnamed bit-field isn't initialized, `int : 0`.
    if (Member->isUnnamedBitField())
      continue;
    if (iInit < Inits.size() && Inits[iInit] == Child &&
        Member->getType()->isReferenceType() &&
        !Member->getType().getNonReferenceType().isConstQualified())
      return true;
    ++iInit;
  }
  return false;
}

/// The type of what a member of type \p Type holds by value: itself, or the
/// elements of an array.
static QualType heldType(QualType Type) {
  return QualType(Type->getBaseElementTypeUnsafe(), 0);
}

/// The flags of phases an object of \p Type holds, as heldPhaseFlag tells,
/// each of them, or with \p First only the first.
static void heldPhaseFlags(QualType Type,
                           llvm::SmallVectorImpl<const FieldDecl *> &Flags,
                           bool First = false) {
  // Each class once, also of a deep hierarchy, or one that holds itself
  // through an owning pointer, also the elements of an array that is
  // replaced as a whole, `fg_Swap(mp_States, Empty)`.
  llvm::SmallVector<QualType, 8> Work{heldType(Type)};
  llvm::SmallPtrSet<const CXXRecordDecl *, 16> Visited;
  while (!Work.empty()) {
    const auto *Record = Work.pop_back_val()->getAsCXXRecordDecl();
    if (!Record || !Record->hasDefinition() ||
        !Visited.insert(Record->getDefinition()).second)
      continue;
    Record = Record->getDefinition();
    // Also through an owning pointer, `TCSharedPointer<CState>`, whose
    // replacement replaces the flag, but not a handle to an actor.
    if (const QualType Owned = ownedType(Record); !Owned.isNull()) {
      const CXXRecordDecl *OwnedRecord = Owned->getAsCXXRecordDecl();
      if (OwnedRecord && !isActorClass(OwnedRecord))
        Work.push_back(Owned);
      continue;
    }
    for (const FieldDecl *Field : Record->fields()) {
      if (annotatedPhases(Field, AnnotationActorPhaseFlag)) {
        Flags.push_back(Field);
        if (First)
          return;
        continue;
      }
      // Also in the elements of an array.
      const QualType Held = heldType(Field->getType());
      if (!Held->isReferenceType() && !Held->isPointerType())
        Work.push_back(Held);
    }
    // Also one it inherits.
    for (const CXXBaseSpecifier &Base : Record->bases())
      Work.push_back(Base.getType());
  }
}

static const FieldDecl *heldPhaseFlag(QualType Type) {
  llvm::SmallVector<const FieldDecl *, 1> Flags;
  heldPhaseFlags(Type, Flags, /*First=*/true);
  return Flags.empty() ? nullptr : Flags.front();
}

/// Whether destroying an object of \p Record can release what it points to:
/// not when its destructor is trivial, or one whose body can be seen only
/// assigns, `~CObserver() { m_pState = nullptr; }`, or does nothing, and so do
/// those of its members and bases.
static bool mayReleaseOnDestructionUncached(const CXXRecordDecl *Record,
                                            int Depth);

/// Like mayReleaseOnDestructionUncached, each class told once, which classes
/// that hold several members of the same class, `struct C2 { C1 a, b; };`,
/// would otherwise make exponential.
static bool mayReleaseOnDestruction(const CXXRecordDecl *Record, int Depth) {
  if (!Record || !Record->hasDefinition())
    return true;
  Record = Record->getDefinition();
  if (Record->hasTrivialDestructor())
    return false;
  struct CReleases {
    llvm::DenseMap<const CXXRecordDecl *, bool> Records;
  };
  auto &Records = astCache<CReleases>(Record->getASTContext()).Records;
  if (auto It = Records.find(Record); It != Records.end())
    return It->second;
  // One that holds itself, through a base, may.
  Records[Record] = true;
  const bool Releases = mayReleaseOnDestructionUncached(Record, Depth);
  Records[Record] = Releases;
  return Releases;
}

static bool mayReleaseOnDestructionUncached(const CXXRecordDecl *Record,
                                            int Depth) {
  if (Depth > 256)
    return true;
  const CXXDestructorDecl *Destructor = Record->getDestructor();
  if (!Destructor)
    return true;
  if (Destructor->isUserProvided()) {
    const FunctionDecl *Definition = Destructor->getDefinition();
    if (!Definition || !Definition->getBody())
      return true;
    bool Releases = false;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || Releases)
        return;
      if (isa<CallExpr, CXXConstructExpr, CXXDeleteExpr, AtomicExpr,
              CXXThrowExpr, AsmStmt>(S)) {
        Releases = true;
        return;
      }
      for (const Stmt *Child : childrenOnce(S))
        Visit(Child);
    };
    Visit(Definition->getBody());
    if (Releases)
      return true;
  }
  // Not a member that is a reference, a pointer or a value, which destroying
  // releases nothing.
  for (const FieldDecl *Field : Record->fields())
    if (const CXXRecordDecl *Member = Field->getType()->isReferenceType()
                                          ? nullptr
                                          : Field->getType()
                                                ->getBaseElementTypeUnsafe()
                                                ->getAsCXXRecordDecl();
        Member && mayReleaseOnDestruction(Member, Depth + 1))
      return true;
  for (const CXXBaseSpecifier &Base : Record->bases())
    if (mayReleaseOnDestruction(Base.getType()->getAsCXXRecordDecl(),
                                Depth + 1))
      return true;
  return false;
}

/// What an owning pointer of type \p Type, like `TCSharedPointer<CState>`,
/// points to, through its `operator->`, also one instantiated from a
/// template: replacing or releasing it destroys what it owns. One whose
/// destructor does nothing, like an observer or an iterator, doesn't own what
/// it points to.
static QualType ownedType(const CXXRecordDecl *Record) {
  if (!mayReleaseOnDestruction(Record, 0))
    return {};
  // Also one it inherits, each base once.
  llvm::SmallVector<const CXXRecordDecl *, 4> Work{Record};
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Visited;
  while (!Work.empty()) {
    const CXXRecordDecl *Current = Work.pop_back_val();
    if (!Current || !Current->hasDefinition() ||
        !Visited.insert(Current->getDefinition()).second)
      continue;
    Current = Current->getDefinition();
    for (const NamedDecl *Found : Current->lookup(
             Current->getASTContext().DeclarationNames.getCXXOperatorName(
                 OO_Arrow))) {
      if (const auto *Method =
              dyn_cast<CXXMethodDecl>(Found->getUnderlyingDecl());
          Method && Method->getReturnType()->isPointerType())
        return Method->getReturnType()->getPointeeType();
      if (const auto *Template =
              dyn_cast<FunctionTemplateDecl>(Found->getUnderlyingDecl()))
        for (const FunctionDecl *Specialization : Template->specializations())
          if (Specialization->getReturnType()->isPointerType())
            return Specialization->getReturnType()->getPointeeType();
    }
    for (const CXXBaseSpecifier &Base : Current->bases())
      Work.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  return {};
}

/// The annotated collections that an object of \p Type holds, also in the
/// objects it holds by value or owns through an owning pointer, and its
/// bases.
static void heldCollections(QualType Type,
                            llvm::SmallVectorImpl<const FieldDecl *> &Held) {
  // Each class once, also of a deep hierarchy, or one that holds itself
  // through an owning pointer, also the elements of an array that is
  // replaced as a whole, `fg_Swap(mp_States, Empty)`.
  llvm::SmallVector<QualType, 8> Work{heldType(Type)};
  llvm::SmallPtrSet<const CXXRecordDecl *, 16> Visited;
  while (!Work.empty()) {
    const auto *Record = Work.pop_back_val()->getAsCXXRecordDecl();
    if (!Record || !Record->hasDefinition() ||
        !Visited.insert(Record->getDefinition()).second)
      continue;
    Record = Record->getDefinition();
    // But not through a handle to an actor, which is of another object.
    if (const QualType Owned = ownedType(Record); !Owned.isNull()) {
      if (const CXXRecordDecl *OwnedRecord = Owned->getAsCXXRecordDecl();
          OwnedRecord && !isActorClass(OwnedRecord))
        Work.push_back(Owned);
      continue;
    }
    for (const FieldDecl *Field : Record->fields()) {
      if (annotatedPhases(Field, AnnotationActorEntries))
        Held.push_back(Field);
      else if (const QualType Element = heldType(Field->getType());
               !Element->isReferenceType() && !Element->isPointerType())
        Work.push_back(Element);
    }
    for (const CXXBaseSpecifier &Base : Record->bases())
      Work.push_back(Base.getType());
  }
}

static bool isConstantTrue(const Expr *Value, const ASTContext &Context);
static bool setsFlag(const Expr *Made, const FieldDecl *Flag,
                     const ASTContext &Context, int Depth = 0);

void ActorPhaseCheck::checkFlagHolderAssignment(const Expr *Assignment,
                                                const Expr *Target,
                                                ASTContext &Context) {
  // Not what never runs, like in a branch of an `if constexpr` that is
  // discarded.
  if (isNeverRun(Assignment, Context))
    return;
  // What a defaulted assignment does is the assignment that calls it.
  if (const FunctionDecl *Enclosing = enclosingPhaseFunction(Assignment, Context);
      Enclosing && Enclosing->isDefaulted())
    return;
  // Assigning the flag itself is checked as a use of the flag.
  if (const auto *Member = dyn_cast<MemberExpr>(Target->IgnoreParenImpCasts());
      Member && annotatedPhases(Member->getMemberDecl(), AnnotationActorPhaseFlag))
    return;
  const QualType Type = Target->getType().getNonReferenceType();
  // Assigning an object that holds an annotated collection replaces it, and
  // with it its entries.
  if (const auto *Member = dyn_cast<MemberExpr>(Target->IgnoreParenImpCasts());
      !Member || !annotatedPhases(Member->getMemberDecl(), AnnotationActorEntries))
  {
    llvm::SmallVector<const FieldDecl *, 4> Collections;
    heldCollections(Type, Collections);
    for (const FieldDecl *Collection : Collections)
      checkChange(Assignment, Target, nullptr, Collection,
                  *annotatedPhases(Collection, AnnotationActorEntries), Context);
  }
  llvm::SmallVector<const FieldDecl *, 2> Flags;
  heldPhaseFlags(Type, Flags);
  if (Flags.empty())
    return;
  // Not an object made with each flag set, `mp_State = CState{true, 3}`,
  // also in what it holds, `mp_State = CState{{true}}`, which sets it whatever
  // it was: the first that isn't is overwritten.
  const Expr *Assigned = nullptr;
  if (const auto *Binary = dyn_cast<BinaryOperator>(Assignment);
      Binary && Binary->getOpcode() == BO_Assign)
    Assigned = Binary->getRHS();
  else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Assignment);
           Operator && Operator->getOperator() == OO_Equal &&
           Operator->getNumArgs() == 2)
    Assigned = Operator->getArg(1);
  const auto *Overwritten =
      llvm::find_if_not(Flags, [&](const FieldDecl *Flag) {
        return Assigned && setsFlag(Assigned, Flag, Context);
      });
  if (Overwritten == Flags.end())
    return;
  const FieldDecl *Flag = *Overwritten;
  diag(Assignment->getBeginLoc(),
       "assigning the object overwrites %0, the flag of a phase, which is "
       "only ever set to true, so that what runs in the phase can rely on it "
       "staying set")
      << Flag;
}

/// Whether \p Value is a constant that is true, `true`, `!false` or a
/// constant that is, `c_bYes`.
static bool isConstantTrue(const Expr *Value, const ASTContext &Context) {
  Value = skipWrappers(Value)->IgnoreParenImpCasts();
  Expr::EvalResult Result;
  return !Value->isValueDependent() &&
         Value->EvaluateAsRValue(Result, Context) && !Result.HasSideEffects &&
         Result.Val.isInt() && !Result.Val.getInt().isZero();
}

/// Whether what \p Made makes sets each copy of \p Flag it holds to true, as
/// what a list makes, `CState{true, 3}`, also in what it holds,
/// `CState{{true}}`, its bases and the elements of its arrays.
static bool setsFlag(const Expr *Made, const FieldDecl *Flag,
                     const ASTContext &Context, int Depth) {
  if (Depth > 8)
    return false;
  for (int Skipped = 0; Skipped < 8; ++Skipped) {
    Made = Made->IgnoreImplicit()->IgnoreParens();
    if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Made))
      Made = Cast->getSubExpr();
    else if (const auto *Construct = dyn_cast<CXXConstructExpr>(Made);
             Construct && Construct->getNumArgs() == 1 &&
             Construct->getConstructor()->isCopyOrMoveConstructor())
      Made = Construct->getArg(0);
    else
      break;
  }
  // What doesn't hold it can't clear it.
  llvm::SmallVector<const FieldDecl *, 2> Held;
  heldPhaseFlags(Made->getType(), Held);
  if (!llvm::is_contained(Held, Flag))
    return true;
  llvm::ArrayRef<Expr *> Inits;
  if (const auto *List = dyn_cast<InitListExpr>(Made)) {
    if (List->hasArrayFiller() || List->getInitializedFieldInUnion())
      return false;
    Inits = List->inits();
  } else if (const auto *Parens = dyn_cast<CXXParenListInitExpr>(Made)) {
    if (Parens->getArrayFiller())
      return false;
    Inits = Parens->getInitExprs();
  } else
    return false;
  if (const auto *Array = Context.getAsConstantArrayType(Made->getType())) {
    if (Inits.size() != Array->getZExtSize())
      return false;
    return llvm::all_of(Inits, [&](const Expr *Element) {
      return setsFlag(Element, Flag, Context, Depth + 1);
    });
  }
  const auto *Record = Made->getType()->getAsCXXRecordDecl();
  if (!Record || !Record->hasDefinition())
    return false;
  Record = Record->getDefinition();
  unsigned iInit = 0;
  for (; iInit < Record->getNumBases(); ++iInit)
    if (iInit >= Inits.size() ||
        !setsFlag(Inits[iInit], Flag, Context, Depth + 1))
      return false;
  // The list has no element for an unnamed bit-field.
  for (const FieldDecl *Field : Record->fields()) {
    if (Field->isUnnamedBitField())
      continue;
    if (iInit >= Inits.size())
      return false;
    const Expr *Init = Inits[iInit++];
    if (Field == Flag ? !isConstantTrue(Init, Context)
                      : !setsFlag(Init, Flag, Context, Depth + 1))
      return false;
  }
  return true;
}

void ActorPhaseCheck::checkFlagUse(const Expr *Use, const ValueDecl *Flag,
                                   ASTContext &Context) {
  const Expr *Child = nullptr;
  const DynTypedNode Parent = parentOf(Use, Context, Child);
  auto Report = [&](SourceLocation Loc) {
    diag(Loc, "%0 is the flag of a phase, which is only ever set to true, so "
              "that what runs in the phase can rely on it staying set")
        << Flag;
  };
  // An output of assembly, `asm("..." : "=m"(bFlag))`, can write anything.
  if (const auto *Assembly = Parent.get<AsmStmt>()) {
    for (unsigned iOutput = 0; iOutput < Assembly->getNumOutputs(); ++iOutput)
      if (Assembly->getOutputExpr(iOutput) == Child) {
        Report(Use->getBeginLoc());
        return;
      }
    return;
  }
  if (const auto *Binary = Parent.get<BinaryOperator>()) {
    if (Binary->getLHS() != Child)
      return;
    // A member function called through a pointer to it, `(bFlag.*pStore)(
    // false)` or `(bFlag.*&CFlag::store)(false)`, which can't be told, can
    // clear it unless it is const; a member through one is part of it.
    if (Binary->isPtrMemOp()) {
      const auto *Pointer =
          Binary->getRHS()->getType()->getAs<MemberPointerType>();
      if (Pointer && Pointer->isMemberFunctionPointer()) {
        const auto *Function =
            Pointer->getPointeeType()->getAs<FunctionProtoType>();
        if (!Function || !Function->isConst())
          Report(Binary->getOperatorLoc());
      } else
        checkFlagUse(Binary, Flag, Context);
      return;
    }
    // Setting it to what is true, `bFlag = true` or a constant that is,
    // `bFlag = c_bYes`, or or-ing it, `bFlag |= bOther`, also in the value,
    // `bFlag = bFlag || bOther`, which can't clear it, but not when what is
    // or-ed suspends, `bFlag = bFlag || co_await fg_Work()`, where the flag
    // read before can be set while it is suspended, or starts a phase,
    // `bFlag = bFlag || f_Begin()`, which can set it before it is written
    // back.
    bool SetsTrue = false;
    if (Binary->getOpcode() == BO_OrAssign &&
        Binary->getLHS()->getType()->isBooleanType())
      SetsTrue = true;
    // Whether what \p S calls can set the flag before it is written back: a
    // function whose body can't be seen, or one whose body names the flag
    // other than to read its value, `return mp_bDestroyed || ...`, also
    // through what it calls in turn, each once.
    // Also what constructs, `Set{this}`, and destroys temporaries, and the
    // default arguments it evaluates. A virtual function that isn't final can
    // be overridden by one that does.
    llvm::SmallPtrSet<const FunctionDecl *, 8> Followed;
    llvm::DenseMap<const Expr *, bool> Defaults;
    std::function<bool(const Stmt *)> MayWriteFlag;
    auto Follow = [&](const FunctionDecl *Callee) {
      if (!Callee)
        return true;
      if (Callee->getBuiltinID() || Callee->isTrivial())
        return false;
      if (const auto *Method = dyn_cast<CXXMethodDecl>(Callee);
          Method && Method->isVirtual() && !Method->hasAttr<FinalAttr>() &&
          !Method->getParent()->hasAttr<FinalAttr>())
        return true;
      const FunctionDecl *Definition = Callee->getDefinition();
      if (!Definition || !Definition->getBody())
        return true;
      if (Followed.size() >= 64)
        return true;
      if (!Followed.insert(Definition).second)
        return false;
      if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Definition))
        for (const CXXCtorInitializer *Initializer : Constructor->inits())
          if (MayWriteFlag(Initializer->getInit()))
            return true;
      return MayWriteFlag(Definition->getBody());
    };
    auto IsFlag = [&](const Expr *E) {
      E = E->IgnoreParens();
      if (const auto *Ref = dyn_cast<DeclRefExpr>(E))
        return Ref->getDecl() == Flag;
      const auto *Member = dyn_cast<MemberExpr>(E);
      return Member && Member->getMemberDecl() == Flag &&
             !MayWriteFlag(Member->getBase());
    };
    MayWriteFlag = [&](const Stmt *S) {
      if (!S || isa<LambdaExpr>(S))
        return false;
      // Its value read, `mp_bDestroyed || ...`, or discarded, `(void)
      // mp_bDestroyed`, which can't set it.
      if (const auto *Read = dyn_cast<ImplicitCastExpr>(S);
          Read && Read->getCastKind() == CK_LValueToRValue &&
          IsFlag(Read->getSubExpr()))
        return false;
      if (const auto *Discard = dyn_cast<ExplicitCastExpr>(S);
          Discard && Discard->getType()->isVoidType() &&
          IsFlag(Discard->getSubExpr()))
        return false;
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
          Ref && Ref->getDecl() == Flag)
        return true;
      if (const auto *Member = dyn_cast<MemberExpr>(S);
          Member && Member->getMemberDecl() == Flag)
        return true;
      // A member function of a local object of its own, `Other.f_Stop()` of
      // `CState Other = mp_State;`, sets the flag of that object through
      // `this`, which isn't the one assigned.
      if (const auto *Member = dyn_cast<CXXMemberCallExpr>(S)) {
        const auto *Object = dyn_cast_or_null<DeclRefExpr>(
            Member->getImplicitObjectArgument()
                ? Member->getImplicitObjectArgument()->IgnoreParenImpCasts()
                : nullptr);
        const auto *Local =
            Object ? dyn_cast<VarDecl>(Object->getDecl()) : nullptr;
        const FunctionDecl *Definition =
            Member->getMethodDecl() ? Member->getMethodDecl()->getDefinition()
                                    : nullptr;
        if (Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
            !Local->getType()->isReferenceType() &&
            !Local->getType()->isPointerType() && Definition &&
            Definition->getBody() && !callsOverride(Member)) {
          std::function<bool(const Stmt *)> OtherThanOwn =
              [&](const Stmt *Inner) {
                if (!Inner || isa<LambdaExpr>(Inner))
                  return false;
                if (const auto *Access = dyn_cast<MemberExpr>(Inner);
                    Access && Access->getMemberDecl() == Flag &&
                    isa<CXXThisExpr>(Access->getBase()->IgnoreParenImpCasts()))
                  return false;
                if (isa<CallExpr, CXXConstructExpr, CXXBindTemporaryExpr,
                        DeclRefExpr, MemberExpr, CXXDefaultArgExpr,
                        CXXDefaultInitExpr>(Inner))
                  return MayWriteFlag(Inner);
                return llvm::any_of(childrenOnce(Inner), OtherThanOwn);
              };
          return OtherThanOwn(Definition->getBody()) ||
                 llvm::any_of(Member->arguments(), MayWriteFlag);
        }
      }
      if (const auto *Call = dyn_cast<CallExpr>(S);
          Call && Follow(Call->getDirectCallee()))
        return true;
      if (const auto *Construct = dyn_cast<CXXConstructExpr>(S);
          Construct && Follow(Construct->getConstructor()))
        return true;
      if (const auto *Temporary = dyn_cast<CXXBindTemporaryExpr>(S);
          Temporary && Follow(Temporary->getTemporary()->getDestructor()))
        return true;
      // Each default once, which the calls of a function share.
      const Expr *Default = nullptr;
      if (const auto *Argument = dyn_cast<CXXDefaultArgExpr>(S))
        Default = Argument->getExpr();
      else if (const auto *Init = dyn_cast<CXXDefaultInitExpr>(S))
        Default = Init->getExpr();
      if (Default) {
        auto [It, Inserted] = Defaults.try_emplace(Default, false);
        if (!Inserted)
          return It->second;
        const bool Writes = MayWriteFlag(Default);
        Defaults[Default] = Writes;
        return Writes;
      }
      return llvm::any_of(childrenOnce(S), MayWriteFlag);
    };
    // Whether the target of the assignment is the object it is when the flag
    // is read: it is reached again after the right side, which runs first,
    // `States[i].m_bStopped = States[i].m_bStopped || (++i, false)`, so only
    // through `this`, members, constant indices and locals that don't change.
    std::function<bool(const Expr *, bool)> Fixed = [&](const Expr *E,
                                                        bool Value) {
      E = E->IgnoreParenImpCasts();
      if (isa<CXXThisExpr>(E))
        return true;
      if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
        const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
        if (!Value)
          return Ref->getDecl() == Flag ||
                 (Variable && (!Variable->getType()->isPointerType() ||
                               (Variable->hasLocalStorage() &&
                                !isChangedVariable(Variable))));
        return Variable && Variable->hasLocalStorage() &&
               !isChangedVariable(Variable);
      }
      if (const auto *Member = dyn_cast<MemberExpr>(E))
        return Fixed(Member->getBase(), Member->isArrow());
      if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E)) {
        const Expr *Index = Subscript->getIdx();
        return (Index->isValueDependent() ||
                        !Index->isIntegerConstantExpr(Context)
                    ? Fixed(Index, true)
                    : true) &&
               Fixed(Subscript->getBase(), Subscript->getBase()
                                               ->IgnoreParenImpCasts()
                                               ->getType()
                                               ->isPointerType());
      }
      if (const auto *Deref = dyn_cast<UnaryOperator>(E);
          Deref && Deref->getOpcode() == UO_Deref)
        return Fixed(Deref->getSubExpr(), true);
      return false;
    };
    std::function<bool(const Expr *)> KeepsSet = [&](const Expr *Value) {
      Value = Value->IgnoreParenImpCasts();
      // The flag itself, which an expression that changes something, like
      // `States[i++].m_bDestroyed`, can't tell is the same.
      if (!Binary->getLHS()->HasSideEffects(Context) &&
          Fixed(Binary->getLHS(), false) &&
          utils::areStatementsIdentical(
              Value, Binary->getLHS()->IgnoreParenImpCasts(), Context))
        return true;
      if (Expr::EvalResult Result;
          !Value->isValueDependent() &&
          Value->EvaluateAsRValue(Result, Context) &&
          !Result.HasSideEffects && Result.Val.isInt())
        return !Result.Val.getInt().isZero();
      const auto *Or = dyn_cast<BinaryOperator>(Value);
      return Or && Or->getOpcode() == BO_LOr &&
             ((KeepsSet(Or->getLHS()) && !suspendsIn(Or->getRHS(), Context) &&
               !startedPhases(Or->getRHS(), Context) &&
               !MayWriteFlag(Or->getRHS())) ||
              KeepsSet(Or->getRHS()));
    };
    if (Binary->getOpcode() == BO_Assign && KeepsSet(Binary->getRHS()))
      SetsTrue = true;
    if (SetsTrue) {
      checkFlagUse(Binary, Flag, Context);
      return;
    }
    if (Binary->getOpcode() == BO_Assign) {
      const auto *Literal = dyn_cast<CXXBoolLiteralExpr>(
          Binary->getRHS()->IgnoreParenImpCasts());
      if (!Literal || !Literal->getValue())
        Report(Binary->getOperatorLoc());
      // What the assignment gives is the flag, like in `(bFlag = true) =
      // false`.
      else
        checkFlagUse(Binary, Flag, Context);
      return;
    }
    if (Binary->isAssignmentOp() || Binary->isCompoundAssignmentOp())
      Report(Binary->getOperatorLoc());
    return;
  }
  // The address of the flag, `&bFlag` or `std::addressof(bFlag)`, at
  // \p Loc.
  auto CheckAddress = [&](const Expr *Address, SourceLocation Loc) {
    // Cast to a pointer that can change it, also after a cast that adds
    // const, `const_cast<bool *>(static_cast<bool const *>(&bFlag))`.
    if (castToChangeable(Address, Context)) {
      Report(Loc);
      return;
    }
    // As what it is converted to, like `bool const *`.
    // Only tested, like `bool(&bFlag)`, it changes nothing.
    const Expr *Converted = nullptr;
    const DynTypedNode Held = parentOf(Address, Context, Converted);
    // What an atomic operation does to it: loads it, `__atomic_load_n(
    // &bFlag, ...)`, or stores true, `__atomic_store_n(&bFlag, true, ...)`,
    // which can't clear it, as what it operates on; anything else, also as
    // where it puts a result, `__atomic_load(&bOther, &bFlag, ...)`, can.
    if (const auto *Atomic = Held.get<AtomicExpr>()) {
      if (Atomic->getPtr()->IgnoreParenImpCasts() !=
          Address->IgnoreParenImpCasts()) {
        Report(Loc);
        return;
      }
      if (isAtomicLoad(Atomic))
        return;
      switch (Atomic->getOp()) {
      case AtomicExpr::AO__atomic_store_n:
      case AtomicExpr::AO__c11_atomic_store:
      case AtomicExpr::AO__atomic_exchange_n:
      case AtomicExpr::AO__c11_atomic_exchange:
        if (isConstantTrue(Atomic->getVal1(), Context))
          return;
        break;
      default:
        break;
      }
      Report(Loc);
      return;
    }
    const QualType Pointee = Converted && Converted->getType()->isPointerType()
                                 ? Converted->getType()->getPointeeType()
                                 : QualType();
    if (!Pointee.isNull() && !Pointee.isConstQualified())
      Report(Loc);
    // Kept in a local pointer to const, which changes it where the const
    // is cast away, `*const_cast<bool *>(pFlag) = false`, also in copies of
    // it, `bool const *pCopy = pFlag`, also assigned to one, `pFlag =
    // &bFlag`.
    else if (const VarDecl *Local = [&]() -> const VarDecl * {
               if (const auto *Variable = Held.get<VarDecl>())
                 return Variable;
               const auto *Assign = Held.get<BinaryOperator>();
               if (!Assign || Assign->getOpcode() != BO_Assign ||
                   Assign->getRHS() != Converted)
                 return nullptr;
               const auto *Target = dyn_cast<DeclRefExpr>(
                   Assign->getLHS()->IgnoreParenImpCasts());
               return Target ? dyn_cast<VarDecl>(Target->getDecl()) : nullptr;
             }();
             Local && Local->hasLocalStorage() &&
             Local->getType()->isPointerType())
      if (const auto *Function = dyn_cast_or_null<FunctionDecl>(
              Local->getParentFunctionOrMethod());
          Function && Function->getBody()) {
        llvm::SmallVector<const VarDecl *, 4> Aliases{Local};
        llvm::SmallPtrSet<const VarDecl *, 4> Seen{Local};
        // What a pointer has where it is used: the address of the flag or
        // a copy of a pointer that has it, or what can't be told. Pointed
        // elsewhere before, `pFlag = &bLocal`, it doesn't point at it.
        // Also either value of a conditional, `bChoose ? pFlag : &bLocal`,
        // the last of a comma and a cast.
        std::function<bool(const Expr *, int)> PointsAtFlagIn =
            [&](const Expr *Value, int Depth) {
              if (!Value || Depth > 8)
                return true;
              Value = Value->IgnoreParenImpCasts();
              if (const auto *Conditional =
                      dyn_cast<ConditionalOperator>(Value))
                return PointsAtFlagIn(Conditional->getTrueExpr(), Depth + 1) ||
                       PointsAtFlagIn(Conditional->getFalseExpr(), Depth + 1);
              if (const auto *Comma = dyn_cast<BinaryOperator>(Value);
                  Comma && Comma->getOpcode() == BO_Comma)
                return PointsAtFlagIn(Comma->getRHS(), Depth + 1);
              if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Value))
                return PointsAtFlagIn(Cast->getSubExpr(), Depth + 1);
              const auto *Ref = dyn_cast<DeclRefExpr>(Value);
              return Value == Address ||
                     (Ref && Seen.contains(dyn_cast<VarDecl>(Ref->getDecl())));
            };
        auto PointsAtFlag = [&](const Expr *Value) {
          return PointsAtFlagIn(Value, 0);
        };
        // The uses of the locals of the function, found once however many
        // aliases there are.
        llvm::DenseMap<const VarDecl *,
                       llvm::SmallVector<const DeclRefExpr *, 4>>
            Uses;
        for (const auto &Nodes :
             match(findAll(
                       declRefExpr(to(varDecl(hasLocalStorage()))).bind("ref")),
                   *Function->getBody(), Context)) {
          const auto *Use = Nodes.getNodeAs<DeclRefExpr>("ref");
          Uses[cast<VarDecl>(Use->getDecl())].push_back(Use);
        }
        for (size_t iAlias = 0; iAlias < Aliases.size(); ++iAlias)
          for (const DeclRefExpr *Ref : Uses.lookup(Aliases[iAlias])) {
            if (isNeverRun(Ref, Context) ||
                !PointsAtFlag(assignedValueAt(Aliases[iAlias], Ref)))
              continue;
            // Past the read of the pointer, which `parentOf` stops at.
            DynTypedNode Up = DynTypedNode::create(*Ref);
            const ExplicitCastExpr *Cast = nullptr;
            for (int Depth = 0; Depth < 256 && !Cast; ++Depth) {
              const auto Parents = instantiatedParents(Up, Context);
              if (Parents.empty())
                break;
              // Also as a value of a conditional, `bChoose ? pFlag :
              // pFlag`, the last of a comma, `(0, pFlag)`, or moved along,
              // `pFlag + 0`, which point there too.
              const Expr *Child = Up.get<Expr>();
              const auto *Conditional =
                  Parents[0].get<AbstractConditionalOperator>();
              const auto *Binary = Parents[0].get<BinaryOperator>();
              const bool Value =
                  (Conditional && Conditional->getCond() != Child) ||
                  (Binary && Binary->getOpcode() == BO_Comma &&
                   Binary->getRHS() == Child) ||
                  (Binary && Binary->isAdditiveOp() &&
                   Binary->getType()->isPointerType());
              if (!(Value || Parents[0].get<ImplicitCastExpr>() ||
                    Parents[0].get<ParenExpr>() ||
                    Parents[0].get<ExplicitCastExpr>()))
                break;
              Cast = Parents[0].get<ExplicitCastExpr>();
              // One that keeps it a pointer to const, `static_cast<bool
              // const *>(pFlag)`, is the same pointer.
              if (Cast && Cast->getType()->isPointerType() &&
                  Cast->getType()->getPointeeType().isConstQualified())
                Cast = nullptr;
              Up = Parents[0];
            }
            // Dereferenced, it is the flag, also bound to a reference, `bool
            // const &bAlias = *pFlag`, whose const can be cast away, and so
            // is an element of it, `pFlag[0]`.
            const auto UpParents = instantiatedParents(Up, Context);
            if (const auto *Deref = Cast || UpParents.empty()
                                        ? nullptr
                                        : UpParents[0].get<UnaryOperator>();
                Deref && Deref->getOpcode() == UO_Deref) {
              checkFlagUse(Deref, Flag, Context);
              continue;
            }
            if (const auto *Subscript =
                    Cast || UpParents.empty()
                        ? nullptr
                        : UpParents[0].get<ArraySubscriptExpr>();
                Subscript && Up.get<Expr>() &&
                Subscript->getBase()->IgnoreParenImpCasts() ==
                    Up.get<Expr>()->IgnoreParenImpCasts()) {
              checkFlagUse(Subscript, Flag, Context);
              continue;
            }
            if (!Cast) {
              // Copied to another local pointer, `bool const *pCopy =
              // pFlag`, or assigned to one, `pCopy = pFlag`, also as a
              // value of a conditional or the last of a comma, `pCopy =
              // bChoose ? pFlag : &bLocal`.
              for (int Depth = 0; Depth < 256; ++Depth) {
                const auto Parents = instantiatedParents(Up, Context);
                const Expr *Child = Up.get<Expr>();
                if (Parents.empty())
                  break;
                if (const auto *Conditional =
                        Parents[0].get<ConditionalOperator>();
                    Conditional && Conditional->getCond() != Child) {
                  Up = Parents[0];
                  continue;
                }
                if (const auto *Comma = Parents[0].get<BinaryOperator>();
                    Comma && Comma->getOpcode() == BO_Comma &&
                    Comma->getRHS() == Child) {
                  Up = Parents[0];
                  continue;
                }
                // Also a cast that keeps it a pointer, `static_cast<bool
                // const *>(pFlag)`.
                if (Parents[0].get<ParenExpr>() ||
                    Parents[0].get<ImplicitCastExpr>() ||
                    (Parents[0].get<ExplicitCastExpr>() &&
                     Parents[0]
                         .get<ExplicitCastExpr>()
                         ->getType()
                         ->isPointerType())) {
                  Up = Parents[0];
                  continue;
                }
                break;
              }
              const auto Parents = instantiatedParents(Up, Context);
              const VarDecl *Copy =
                  Parents.empty() ? nullptr : Parents[0].get<VarDecl>();
              if (const auto *Assign = Parents.empty()
                                           ? nullptr
                                           : Parents[0].get<BinaryOperator>();
                  Assign && Assign->getOpcode() == BO_Assign &&
                  Assign->getRHS() == Up.get<Expr>())
                if (const auto *Target = dyn_cast<DeclRefExpr>(
                        Assign->getLHS()->IgnoreParenImpCasts()))
                  Copy = dyn_cast<VarDecl>(Target->getDecl());
              // Also a reference to it, `bool const *&pAlias = pFlag`.
              if (Copy && Copy->hasLocalStorage() &&
                  Copy->getType().getNonReferenceType()->isPointerType() &&
                  Seen.insert(Copy).second)
                Aliases.push_back(Copy);
              continue;
            }
            if (!Cast->getType()->isPointerType() ||
                Cast->getType()->getPointeeType().isConstQualified())
              continue;
            // Dereferenced, it is used like the flag; kept, it can change
            // it anywhere, like taking its address.
            const Expr *DerefChild = nullptr;
            if (const auto *Deref =
                    parentOf(Cast, Context, DerefChild).get<UnaryOperator>();
                Deref && Deref->getOpcode() == UO_Deref)
              checkFlagUse(Deref, Flag, Context);
            else
              Report(Cast->getBeginLoc());
          }
      }
  };
  if (const auto *Unary = Parent.get<UnaryOperator>()) {
    if (Unary->isIncrementDecrementOp())
      Report(Unary->getOperatorLoc());
    else if (Unary->getOpcode() == UO_AddrOf)
      CheckAddress(Unary, Unary->getOperatorLoc());
    return;
  }
  if (const auto *Call = Parent.get<CXXOperatorCallExpr>();
      Call && Call->getNumArgs() >= 1 && Call->getArg(0) == Child &&
      Call->isAssignmentOp()) {
    // Setting it, like `bFlag = true` of an atomic.
    if (Call->getOperator() != OO_Equal || Call->getNumArgs() != 2 ||
        !isConstantTrue(Call->getArg(1), Context))
      Report(Call->getOperatorLoc());
    else if (Call->getType()->isReferenceType() || Call->isGLValue())
      checkFlagUse(Call, Flag, Context);
    return;
  }
  // Another non-const member operator of it, like `++` of an atomic.
  if (const auto *Call = Parent.get<CXXOperatorCallExpr>();
      Call && Call->getNumArgs() >= 1 && Call->getArg(0) == Child) {
    if (const auto *Method =
            dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
        !Method || (!hasConstObject(Method) && !Method->isStatic()))
      Report(Call->getOperatorLoc());
    return;
  }
  // A non-const member function of it, which can clear it, like
  // `bFlag.store(false)` of an atomic. Only storing `true`, like
  // `bFlag.store(true)` or `bFlag.exchange(true)`, sets it, also with
  // Malterlib's atomics, `bFlag.f_Store(true)` or `bFlag.f_Exchange(true)`.
  if (const auto *Member = Parent.get<MemberExpr>()) {
    const auto *Method = dyn_cast<CXXMethodDecl>(Member->getMemberDecl());
    if (!Method || hasConstObject(Method) || Method->isStatic())
      return;
    const auto CallParents = instantiatedParents(*Member, Context);
    const auto *Call =
        CallParents.empty() ? nullptr : CallParents[0].get<CXXMemberCallExpr>();
    if (Call && Method->getIdentifier() &&
        llvm::is_contained({StringRef("store"), StringRef("exchange"),
                            StringRef("f_Store"), StringRef("f_Exchange")},
                           Method->getName()) &&
        Call->getNumArgs() >= 1 && isConstantTrue(Call->getArg(0), Context))
      return;
    // `compare_exchange_strong(Expected, true)` sets it or leaves it, and so
    // does `f_CompareExchangeStrong(Expected, true)`.
    if (Call && Method->getIdentifier() &&
        (Method->getName().starts_with("compare_exchange") ||
         Method->getName().starts_with("f_CompareExchange")) &&
        Call->getNumArgs() >= 2 && isConstantTrue(Call->getArg(1), Context))
      return;
    Report(Member->getMemberLoc());
    return;
  }
  // Bound to a non-const reference member, like `CAlias{Flag}` or
  // `CAlias(Flag)`.
  if (Parent.get<InitListExpr>() || Parent.get<CXXParenListInitExpr>()) {
    if (bindsMutableReferenceMember(Parent.get<Expr>(), Child))
      Report(Use->getBeginLoc());
    return;
  }
  if (const auto *Call = Parent.get<CallExpr>()) {
    const FunctionDecl *Callee = Call->getDirectCallee();
    if (isAddressOfCall(Call)) {
      CheckAddress(Call, Call->getBeginLoc());
      return;
    }
    // The object of a member operator, like a lambda's call operator, is its
    // first argument.
    const unsigned Offset =
        isa<CXXOperatorCallExpr>(Call) && isa_and_nonnull<CXXMethodDecl>(Callee) &&
                !cast<CXXMethodDecl>(Callee)->isExplicitObjectMemberFunction()
            ? 1
            : 0;
    for (unsigned iArgument = Offset; iArgument < Call->getNumArgs();
         ++iArgument) {
      if (Call->getArg(iArgument) != Child)
        continue;
      if (!Callee || iArgument - Offset >= Callee->getNumParams()) {
        Report(Use->getBeginLoc());
        return;
      }
      const QualType Type = Callee->getParamDecl(iArgument - Offset)->getType();
      if (Type->isReferenceType() &&
          !Type.getNonReferenceType().isConstQualified())
        Report(Use->getBeginLoc());
      // What gives it back is the flag too, `fg_Const(m_bDestroyed)`, also
      // as a const reference that a cast can change.
      else if (Call->isGLValue() &&
               llvm::is_contained(returnedArguments(Call), Child))
        checkFlagUse(Call, Flag, Context);
      // Also a pointer to it that it gives back, through which it can be
      // changed, `*const_cast<bool *>(fg_Pointer(m_bStopping)) = false`.
      else if (const Expr *Mutable = mutableAddressOf(Call, Child, Context))
        Report(Mutable->getBeginLoc());
    }
    return;
  }
  if (const auto *Construct = Parent.get<CXXConstructExpr>()) {
    const CXXConstructorDecl *Constructor = Construct->getConstructor();
    for (unsigned iArgument = 0; iArgument < Construct->getNumArgs();
         ++iArgument) {
      if (Construct->getArg(iArgument) != Child)
        continue;
      const QualType Type = iArgument < Constructor->getNumParams()
                                ? Constructor->getParamDecl(iArgument)->getType()
                                : QualType();
      if (Type.isNull() || (Type->isReferenceType() &&
                            !Type.getNonReferenceType().isConstQualified()))
        Report(Use->getBeginLoc());
    }
    return;
  }
  // Through a cast to a non-const reference, `const_cast<bool &>(bAlias) =
  // false`.
  if (const auto *Cast = Parent.get<ExplicitCastExpr>()) {
    if (Cast->isGLValue() && !Cast->getType().isConstQualified())
      checkFlagUse(Cast, Flag, Context);
    return;
  }
  if (const auto *Variable = Parent.get<VarDecl>()) {
    if (!Variable->getType()->isReferenceType())
      return;
    if (!Variable->getType().getNonReferenceType().isConstQualified()) {
      Report(Use->getBeginLoc());
      return;
    }
    // A local const reference to it is the flag too where the const is cast
    // away. Each is followed once while the outermost use is, also one made
    // of itself, `bool const &bAlias = bFirst ? bFlag : bAlias;`, and ones
    // made of others many times.
    static thread_local llvm::SmallPtrSet<const VarDecl *, 8> *s_pFollowed =
        nullptr;
    std::optional<llvm::SmallPtrSet<const VarDecl *, 8>> Owned;
    if (!s_pFollowed)
      s_pFollowed = &Owned.emplace();
    const llvm::scope_exit Release([&] {
      if (Owned)
        s_pFollowed = nullptr;
    });
    if (!s_pFollowed->insert(Variable).second)
      return;
    if (const auto *Function =
            Variable->hasLocalStorage()
                ? dyn_cast_or_null<FunctionDecl>(
                      Variable->getParentFunctionOrMethod())
                : nullptr;
        Function && Function->getBody())
      for (const auto &Nodes : match(
               findAll(declRefExpr(to(varDecl(equalsNode(Variable))))
                           .bind("ref")),
               *Function->getBody(), Context))
        // Not where it doesn't run, `sizeof(...)`.
        if (const auto *Ref = Nodes.getNodeAs<DeclRefExpr>("ref");
            !isNeverRun(Ref, Context))
          checkFlagUse(Ref, Flag, Context);
    return;
  }
  // Returned by a non-const reference, which the caller can change it
  // through, also as the value of a future.
  if (const QualType Type = returnedType(Parent, Use, Context);
      !Type.isNull() && (Type->isReferenceType() || Type->isPointerType()) &&
      !Type->getPointeeType().isConstQualified())
    Report(Use->getBeginLoc());
}

/// Whether \p E, a value a pointer \p Alias to the collection \p Field is
/// pointed at, may still point to it: it refers to the pointer itself or to
/// the collection, like `bChoose ? pItems : &Local`.
static bool mayStillPointTo(const Stmt *E, const VarDecl *Alias,
                            const FieldDecl *Field, int Depth = 0) {
  if (!E || isa<LambdaExpr>(E) || Depth > 8)
    return false;
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    if (Ref->getDecl() == Alias)
      return true;
    // Also a copy of it kept before, `auto *pSaved = pItems`.
    if (const auto *Saved = dyn_cast<VarDecl>(Ref->getDecl());
        Saved && Saved->hasLocalStorage() && Saved->getInit() &&
        (Saved->getType()->isPointerType() ||
         Saved->getType()->isReferenceType()))
      return mayStillPointTo(Saved->getInit(), Alias, Field, Depth + 1);
    return false;
  }
  if (const auto *Member = dyn_cast<MemberExpr>(E);
      Member && Member->getMemberDecl() == Field)
    return true;
  return llvm::any_of(childrenOnce(E), [&](const Stmt *Child) {
    return mayStillPointTo(Child, Alias, Field, Depth + 1);
  });
}

/// Whether \p Ref, of a pointer, is what an assignment points elsewhere,
/// `pItems = &Local`.
static bool isRepointing(const DeclRefExpr *Ref, ASTContext &Context) {
  // Also through a reference to the pointer, `pAlias = nullptr` with `auto
  // *&pAlias = pItems`, or through a pointer to it, `*ppItems = nullptr`: what
  // is written is a pointer, not the collection.
  const Expr *Target = Ref;
  for (int Depth = 0; Depth < 8; ++Depth) {
    const Expr *Child = nullptr;
    const DynTypedNode Parent = parentOf(Target, Context, Child);
    if (const auto *Assignment = Parent.get<BinaryOperator>())
      return Assignment->isAssignmentOp() && Assignment->getLHS() == Child &&
             Target->getType()->isPointerType();
    const auto *Deref = Parent.get<UnaryOperator>();
    if (!Deref || Deref->getOpcode() != UO_Deref)
      return false;
    Target = Deref;
  }
  return false;
}

void ActorPhaseCheck::checkEntriesUse(const Expr *Use, const FieldDecl *Field,
                                      unsigned Phases, ASTContext &Context,
                                      int Depth, const Expr *Object) {
  // Also an alias of it that isn't run, `if constexpr (false)
  // Items.f_Clear()`.
  if (isNeverRun(Use, Context))
    return;
  // Past a chain of aliases longer than is followed, what is done with it
  // can't be told, so it may change it.
  if (Depth > 64) {
    checkChange(Use, Object, nullptr, Field, Phases, Context);
    return;
  }
  const Expr *Child = nullptr;
  DynTypedNode Parent = parentOf(Use, Context, Child);
  // The object whose collection it is; an alias of the collection keeps the
  // object of what it was made from.
  if (const auto *Member = dyn_cast<MemberExpr>(Use);
      Member && Member->getMemberDecl() == Field)
    Object = Member->getBase();
  // Reading a pointer to the collection, like `p` in `p->f_Clear()`, isn't
  // a use of it; what is done with what it points to is.
  if (const auto *Cast = Parent.get<ImplicitCastExpr>();
      Cast && Cast->getCastKind() == CK_LValueToRValue &&
      Cast->getType()->isPointerType())
    Parent = parentOf(Cast, Context, Child);
  // A pointer to it is still one through a cast, `static_cast<decltype(p)>(p)`,
  // or as either value of a conditional, `bMember ? pItems : &Local`.
  for (int Level = 0; Level < 8; ++Level) {
    if (const auto *Conditional = Parent.get<ConditionalOperator>();
        Conditional && Conditional->getType()->isPointerType() &&
        Child != Conditional->getCond()) {
      Parent = parentOf(Conditional, Context, Child);
      continue;
    }
    if (const auto *Cast = Parent.get<ExplicitCastExpr>();
        Cast && Cast->getType()->isPointerType() &&
        Child->getType()->isPointerType()) {
      Parent = parentOf(Cast, Context, Child);
      continue;
    }
    // Also moved by an offset, `p + 0`, which can only stay at it.
    if (const auto *Offset = Parent.get<BinaryOperator>();
        Offset && Offset->isAdditiveOp() &&
        Offset->getType()->isPointerType() && Child->getType()->isPointerType()) {
      Parent = parentOf(Offset, Context, Child);
      continue;
    }
    // Also the collection itself through a cast to a reference to it,
    // `const_cast<TCMap<int, int> &>(fg_Const(mp_Items))`.
    if (const auto *Cast = Parent.get<ExplicitCastExpr>();
        Cast && Cast->isGLValue() && Child->isGLValue() &&
        Context.hasSameUnqualifiedType(Cast->getType(), Child->getType())) {
      Parent = parentOf(Cast, Context, Child);
      continue;
    }
    break;
  }
  if (const auto *Unary = Parent.get<UnaryOperator>();
      Unary && Unary->getOpcode() == UO_Deref) {
    checkEntriesUse(Unary, Field, Phases, Context, Depth + 1, Object);
    return;
  }
  // So is indexing the pointer, `p[0]`.
  if (const auto *Subscript = Parent.get<ArraySubscriptExpr>();
      Subscript && Child->getType()->isPointerType()) {
    checkEntriesUse(Subscript, Field, Phases, Context, Depth + 1, Object);
    return;
  }

  // A member function: one that removes entries changes the collection, and
  // an iterator can remove the entry it is at. \p Call is null where it is
  // only named, and its arguments other than the object begin at
  // \p FirstArgument.
  auto CheckMethod = [&](const CXXMethodDecl *Method, const CallExpr *Call,
                         const Expr *Named, unsigned FirstArgument) {
    if (hasConstObject(Method))
      return;
    const Expr *Change = Call ? static_cast<const Expr *>(Call) : Named;
    // Assigning by name, like `mp_Items.operator=({})`, replaces its entries,
    // and so can another operator that isn't const, `operator-=`, like when
    // it is used as an operator, but `operator[]` and `operator()` are
    // lookups.
    if (const OverloadedOperatorKind Operator = Method->getOverloadedOperator();
        Operator == OO_Equal ||
        (Operator != OO_None && Operator != OO_Subscript &&
         Operator != OO_Call && !isAddingOperator(Operator) &&
         !(isValueOperator(Operator) &&
           Method->getNumParams() ==
               (Method->isExplicitObjectMemberFunction() ? 2u : 1u)))) {
      checkChange(Change, Object, nullptr, Field, Phases, Context);
      return;
    }
    // Destroying it, `mp_Items.~TCMap()`, removes its entries.
    if (isa<CXXDestructorDecl>(Method)) {
      checkChange(Change, Object, nullptr, Field, Phases, Context);
      return;
    }
    if (!Method->getIdentifier())
      return;
    // Also one that calls one of its object that does, `f_GetArray(_Len)` of
    // `f_SetLen(_Len); return f_GetArray();`, also deeper.
    if (callsOwnMethod(Method, [&](const CXXMethodDecl *Each) {
          return Each->getIdentifier() &&
                 matchesNamePattern(Each->getName(), RemovingMethodList);
        })) {
      checkChange(Change, Object,
                  Call && Call->getNumArgs() > FirstArgument
                      ? Call->getArg(FirstArgument)
                      : nullptr,
                  Field, Phases, Context);
      return;
    }
    if (Call && matchesNamePattern(Method->getName(), IteratorMethodList))
      checkIterator(Call, Field, Phases, Context, Object, 0);
  };
  if (const auto *Member = Parent.get<MemberExpr>()) {
    const auto *Method = dyn_cast<CXXMethodDecl>(Member->getMemberDecl());
    if (!Method)
      return;
    const auto CallParents = instantiatedParents(*Member, Context);
    const CallExpr *Call =
        CallParents.empty() ? nullptr : CallParents[0].get<CXXMemberCallExpr>();
    CheckMethod(Method, Call, Member, 0);
    return;
  }
  // Returning a non-const reference or pointer to it lets the caller change
  // it anywhere.
  if (Parent.get<ReturnStmt>() || Parent.get<CoreturnStmt>()) {
    const QualType Type = returnedType(Parent, Use, Context);
    if (!Type.isNull() && (Type->isReferenceType() || Type->isPointerType()) &&
        !Type->getPointeeType().isConstQualified())
      checkChange(Use, Object, nullptr, Field, Phases, Context);
    return;
  }
  if (const auto *Call = Parent.get<CXXOperatorCallExpr>();
      Call && Call->getNumArgs() >= 1 && Call->getArg(0) == Child &&
      isa_and_nonnull<CXXMethodDecl>(Call->getDirectCallee())) {
    // A member operator of the collection: assigning replaces its entries,
    // lookups like `mp_Items[Key]`, adding, `mp_Items += Other`, making a new
    // value, `mp_Items - Other`, and const operators don't remove any, and
    // other ones can.
    // Not one that is given it to consume, `fg_Move(mp_Items) & _Other` of
    // `operator&(this Self &&_Self, ...)`, which can take its entries.
    const auto *Method = cast<CXXMethodDecl>(Call->getDirectCallee());
    const bool Consumes =
        Call->getArg(0)->isXValue() &&
        (Method->isExplicitObjectMemberFunction()
             ? !Method->getParamDecl(0)->getType()->isLValueReferenceType()
             : Method->getRefQualifier() == RQ_RValue);
    if (Call->getOperator() == OO_Equal ||
        (Call->getOperator() != OO_Subscript &&
         Call->getOperator() != OO_Call &&
         !isAddingOperator(Call->getOperator()) &&
         !(isValueOperator(Call->getOperator()) && Call->getNumArgs() == 2 &&
           !Consumes) &&
         !hasConstObject(Method)))
      checkChange(Call, Object, nullptr, Field, Phases, Context);
    return;
  }
  // The address of it, `std::addressof(mp_Items)`, is like `&mp_Items`.
  // Passed on to be changed: by a non-const reference, like to fg_Move or
  // fg_Swap, but not to what only makes a fresh key of it, like fg_RandomID.
  if (const auto *Call = Parent.get<CallExpr>(); Call && !isAddressOfCall(Call)) {
    const FunctionDecl *Callee = Call->getDirectCallee();
    if (Callee && Callee->getIdentifier() &&
        matchesNamePattern(Callee->getName(), FreshKeyFunctionList))
      return;
    // A free `begin(Container)`, like a loop over the collection uses, gives an
    // iterator.
    if (Callee && Callee->getIdentifier() &&
        matchesNamePattern(Callee->getName(), IteratorMethodList)) {
      checkIterator(Call, Field, Phases, Context, Object, 0);
      return;
    }
    // The object of a call to a member function with an explicit object,
    // `mp_Items.f_Difference(_Other)`, is like the object of any member
    // function.
    if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
        Method && Method->isExplicitObjectMemberFunction() &&
        !isa<CXXOperatorCallExpr>(Call) && Call->getNumArgs() >= 1 &&
        Call->getArg(0) == Child) {
      CheckMethod(Method, Call, Call, 1);
      return;
    }
    // The object of a member operator, like a lambda's call operator, is its
    // first argument.
    const unsigned Offset =
        isa<CXXOperatorCallExpr>(Call) && isa_and_nonnull<CXXMethodDecl>(Callee) &&
                !cast<CXXMethodDecl>(Callee)->isExplicitObjectMemberFunction()
            ? 1
            : 0;
    for (unsigned iArgument = Offset; iArgument < Call->getNumArgs();
         ++iArgument) {
      if (Call->getArg(iArgument) != Child)
        continue;
      const QualType Type =
          Callee && iArgument - Offset < Callee->getNumParams()
              ? Callee->getParamDecl(iArgument - Offset)->getType()
              : QualType();
      // By a non-const reference, or a pointer to it, like an alias
      // `auto *pItems = &mp_Items` given on.
      if (Type.isNull() ||
          (Type->isReferenceType() &&
           !Type.getNonReferenceType().isConstQualified()) ||
          (Type->isPointerType() && !Type->getPointeeType().isConstQualified() &&
           Child->getType()->isPointerType())) {
        checkChange(Call, Object, nullptr, Field, Phases, Context);
        continue;
      }
      // By a const reference to what the call gives back, `fg_Const(mp_Items)`
      // of `t_CType const &fg_Const(t_CType const &_Value)`, which can be
      // it: what is done with that is done with it, also where the const is
      // cast away, `const_cast<TCMap<int, int> &>(fg_Const(mp_Items))`.
      if (Type->isReferenceType() && Call->isGLValue() &&
          Context.hasSameUnqualifiedType(Call->getType(),
                                         Type.getNonReferenceType()))
        checkEntriesUse(Call, Field, Phases, Context, Depth + 1, Object);
      // Also a pointer to it that it gives back, which can change it,
      // `const_cast<TCMap<int, int> *>(fg_Pointer(mp_Items))->f_Clear()`.
      else if (const Expr *Mutable = mutableAddressOf(Call, Child, Context))
        checkChange(Mutable, Object, nullptr, Field, Phases, Context);
    }
    return;
  }
  // Bound to a non-const reference member, like `CAlias{mp_Items}` or
  // `CAlias(mp_Items)`.
  if (Parent.get<InitListExpr>() || Parent.get<CXXParenListInitExpr>()) {
    if (bindsMutableReferenceMember(Parent.get<Expr>(), Child))
      checkChange(Parent.get<Expr>(), Object, nullptr, Field, Phases, Context);
    return;
  }
  if (const auto *Construct = Parent.get<CXXConstructExpr>()) {
    const CXXConstructorDecl *Constructor = Construct->getConstructor();
    for (unsigned iArgument = 0; iArgument < Construct->getNumArgs();
         ++iArgument) {
      if (Construct->getArg(iArgument) != Child ||
          iArgument >= Constructor->getNumParams())
        continue;
      const QualType Type = Constructor->getParamDecl(iArgument)->getType();
      // By a non-const reference, or a pointer to it, like an alias `auto
      // *pItems = &mp_Items` given on, like to a function.
      if ((Type->isReferenceType() &&
           !Type.getNonReferenceType().isConstQualified()) ||
          (Type->isPointerType() && !Type->getPointeeType().isConstQualified() &&
           Child->getType()->isPointerType()))
        checkChange(Construct, Object, nullptr, Field, Phases, Context);
    }
    return;
  }
  // A non-const pointer or reference to the collection is an alias of it,
  // like the range of a loop over it, whose uses are checked the same way,
  // also a pointer it is assigned to, `pItems = pAlias`.
  const VarDecl *Alias = Parent.get<VarDecl>();
  // The assignment that made the alias, which isn't pointing it elsewhere.
  const Expr *Origin = nullptr;
  if (const auto *Binary = Parent.get<BinaryOperator>()) {
    // Also a member function called through a pointer to it, `(mp_Items.*
    // fClear)()`, which can be one that removes.
    // Not a pointer to it pointed elsewhere, `*ppItems = nullptr`, which
    // doesn't change the collection.
    const auto *Named = dyn_cast<MemberExpr>(Use);
    if (Binary->getLHS() == Child && Binary->isAssignmentOp() &&
        Child->getType()->isPointerType() &&
        !(Named && Named->getMemberDecl() == Field))
      return;
    if (Binary->getLHS() == Child &&
        (Binary->isAssignmentOp() || Binary->isPtrMemOp())) {
      checkChange(Binary, Object, nullptr, Field, Phases, Context);
      return;
    }
    if (Binary->getOpcode() != BO_Assign || Binary->getRHS() != Child ||
        !Child->getType()->isPointerType())
      return;
    const auto *Target =
        dyn_cast<DeclRefExpr>(Binary->getLHS()->IgnoreParenImpCasts());
    Alias = Target ? dyn_cast<VarDecl>(Target->getDecl()) : nullptr;
    Origin = Binary->getRHS();
    // A pointer to const kept in a local is followed, where its const can be
    // cast away, `q = p; const_cast<TCMap<int, int> *>(q)->f_Clear()`, but
    // not one kept elsewhere.
    if (Child->getType()->getPointeeType().isConstQualified() &&
        (!Alias || !Alias->hasLocalStorage()))
      return;
    // Kept where it isn't followed, like in a member, it can be used
    // anywhere.
    if (!Alias || !Alias->hasLocalStorage()) {
      checkChange(Binary, Object, nullptr, Field, Phases, Context);
      return;
    }
  }
  const Expr *Unary = nullptr;
  if (const auto *AddressOf = Parent.get<UnaryOperator>();
      AddressOf && AddressOf->getOpcode() == UO_AddrOf)
    Unary = AddressOf;
  else if (const auto *Call = Parent.get<CallExpr>(); isAddressOfCall(Call))
    Unary = Call;
  if (Unary) {
    const Expr *AddressChild = nullptr;
    const DynTypedNode AddressParent = parentOf(Unary, Context, AddressChild);
    Alias = AddressParent.get<VarDecl>();
    // Also assigned to a local pointer, `pItems = &mp_Items`.
    if (const auto *Assign = AddressParent.get<BinaryOperator>();
        !Alias && Assign && Assign->getOpcode() == BO_Assign &&
        Assign->getRHS()->IgnoreParenImpCasts() == Unary)
      if (const auto *Target =
              dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts());
          Target && isa<VarDecl>(Target->getDecl()) &&
          cast<VarDecl>(Target->getDecl())->hasLocalStorage()) {
        Alias = cast<VarDecl>(Target->getDecl());
        Origin = Unary;
      }
    // Converted to a pointer to const, like given to `fg_Read(TCMap<int, int>
    // const *_pItems)`, it can't change it; kept in a local, it can where the
    // const is cast away, `const_cast<TCMap<int, int> *>(pItems)->f_Clear()`.
    if (!Alias && castToChangeable(Unary, Context)) {
      checkChange(Unary, Object, nullptr, Field, Phases, Context);
      return;
    }
    if (!Alias &&
        (Unary->getType()->getPointeeType().isConstQualified() ||
         (AddressChild->getType()->isPointerType() &&
          AddressChild->getType()->getPointeeType().isConstQualified())))
      return;
    if (!Alias) {
      checkChange(Unary, Object, nullptr, Field, Phases, Context);
      return;
    }
  }
  if (!Alias)
    return;
  const QualType Type = Alias->getType();
  if (!Type->isReferenceType() && !Type->isPointerType())
    return;
  // A const one changes it only where the const is cast away,
  // `const_cast<TCMap<int, int> &>(Alias).f_Clear()`.
  const bool Const = Type->getPointeeType().isConstQualified();
  auto CastAwayConst = [&](const Expr *Ref) -> const Expr * {
    DynTypedNode Node = DynTypedNode::create(*Ref);
    // Of a pointer, also through what it points to, once,
    // `const_cast<TCMap<int, int> &>(*pAlias)` or `pAlias[0]`.
    bool Dereferenced = !Type->isPointerType();
    for (int Depth = 0; Depth < 256; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return nullptr;
      if (const auto *Cast = Parents[0].get<ExplicitCastExpr>();
          Cast && (isa<CXXConstCastExpr>(Cast) || isa<CStyleCastExpr>(Cast)))
        return (Cast->isGLValue() && !Cast->getType().isConstQualified()) ||
                       (Cast->getType()->isPointerType() &&
                        !Cast->getType()->getPointeeType().isConstQualified())
                   ? Cast
                   : nullptr;
      // Also as either value of a conditional that designates it, `bChoose
      // ? Alias : Alias`, or the last of a comma.
      const Expr *Child = Node.get<Expr>();
      if (const auto *Deref = Parents[0].get<UnaryOperator>();
          !Dereferenced && Deref && Deref->getOpcode() == UO_Deref) {
        Dereferenced = true;
        Node = Parents[0];
        continue;
      }
      if (const auto *Subscript = Parents[0].get<ArraySubscriptExpr>();
          !Dereferenced && Subscript && Child &&
          Subscript->getBase()->IgnoreParenImpCasts() ==
              Child->IgnoreParenImpCasts()) {
        Dereferenced = true;
        Node = Parents[0];
        continue;
      }
      const auto *Conditional =
          Parents[0].get<AbstractConditionalOperator>();
      const auto *Comma = Parents[0].get<BinaryOperator>();
      if (!Parents[0].get<ParenExpr>() && !Parents[0].get<ImplicitCastExpr>() &&
          !(Conditional && Conditional->isGLValue() &&
            Conditional->getCond() != Child) &&
          !(Comma && Comma->getOpcode() == BO_Comma &&
            Comma->getRHS() == Child))
        return nullptr;
      Node = Parents[0];
    }
    return nullptr;
  };
  // Whether \p Ref, of a const alias, makes another alias of it: a local
  // reference or pointer, its address, or, of a pointer, an assignment.
  auto MakesAlias = [&](const DeclRefExpr *Ref) {
    const Expr *Child = nullptr;
    DynTypedNode Parent = parentOf(Ref, Context, Child);
    // A pointer is copied by its value, `auto const *p1 = p0`, also through
    // a cast that keeps it a pointer, `static_cast<bool const *>(p0)`.
    for (int Level = 0; Type->isPointerType() && Level < 8; ++Level) {
      const auto *Cast = Parent.get<CastExpr>();
      if (!Cast || !Cast->getType()->isPointerType())
        break;
      Parent = parentOf(Cast, Context, Child);
    }
    const auto *Local = Parent.get<VarDecl>();
    const auto *Address = Parent.get<UnaryOperator>();
    return (Local && Local->hasLocalStorage() &&
            (Local->getType()->isReferenceType() ||
             Local->getType()->isPointerType())) ||
           (Address && Address->getOpcode() == UO_AddrOf) ||
           (Type->isPointerType() && Parent.get<BinaryOperator>());
  };
  const FunctionDecl *Enclosing = enclosingPhaseFunction(Use, Context);
  if (!Enclosing || !Enclosing->getBody())
    return;
  // The uses of each alias are looked at once while a use of the collection
  // is, also of aliases assigned each other, `p0 = p1; p1 = p0;`, which would
  // otherwise be looked at once per path through them.
  using CExpanded = llvm::DenseSet<std::pair<const VarDecl *, const Expr *>>;
  static thread_local CExpanded *s_pExpanded = nullptr;
  std::optional<CExpanded> Owned;
  if (!s_pExpanded)
    s_pExpanded = &Owned.emplace();
  const llvm::scope_exit Release([&] {
    if (Owned)
      s_pExpanded = nullptr;
  });
  if (!s_pExpanded->insert({Alias, Origin}).second)
    return;
  for (const DeclRefExpr *Ref : referencesTo(Enclosing, Alias)) {
    // A copy of the collection that a lambda captured is its own, but a copy
    // of a pointer to it points to it too.
    if (!Type->isPointerType() && isCapturedByCopyAt(Ref))
      continue;
    // Pointing the pointer elsewhere doesn't change the collection, and its
    // uses after that, on every path, are of something else, also of a
    // pointer to const.
    if (isRepointing(Ref, Context))
      continue;
    if (Type->isPointerType()) {
      // Unless what it is pointed at may still be the collection, like
      // `p = bChoose ? p : &Local`.
      if (const Expr *Assigned = assignedOnEveryPathBefore(Ref, Alias, Context);
          Assigned && Assigned != Origin && !mayStillPointTo(Assigned, Alias, Field))
        continue;
    }
    if (Const) {
      if (const Expr *Cast = CastAwayConst(Ref))
        checkEntriesUse(Cast, Field, Phases, Context, Depth + 1, Object);
      // Also another const alias made of it, `auto const &Second = First`
      // or `auto const *pItems = &First`, where the const can be cast away.
      else if (MakesAlias(Ref))
        checkEntriesUse(Ref, Field, Phases, Context, Depth + 1, Object);
      continue;
    }
    checkEntriesUse(Ref, Field, Phases, Context, Depth + 1, Object);
  }
}

/// Whether \p Child is an argument of the call operator \p Call of a
/// function object, like a lambda, not its object.
static bool isGivenToFunctionObject(const CXXOperatorCallExpr *Call,
                                    const Expr *Child) {
  return Call->getOperator() == OO_Call && Call->getNumArgs() >= 1 &&
         Call->getArg(0)->IgnoreParenImpCasts() !=
             Child->IgnoreParenImpCasts();
}

/// Whether \p Call calls a function of the standard library, like
/// `std::distance`, which can't remove from a collection through an iterator.
/// Whether \p Call moves or forwards its argument, like `std::move(iEntry)`,
/// and gives it.
static bool isMove(const CallExpr *Call) {
  const StringRef Name = calleeName(Call);
  return Call->getNumArgs() == 1 && isMoveOrForwardFunction(Name);
}

/// Whether \p Call is of a function of the standard library that doesn't
/// call what it is given, like `std::distance`, unlike `std::invoke`.
static bool isStandardFunction(const CallExpr *Call) {
  const FunctionDecl *Callee = Call->getDirectCallee();
  if (!Callee || !Callee->isInStdNamespace())
    return false;
  // Not one that calls what it is given, like `std::invoke` or `std::async`,
  // also an algorithm given a function object.
  if (Callee->getIdentifier() &&
      llvm::is_contained({StringRef("invoke"), StringRef("invoke_r"),
                          StringRef("apply"), StringRef("bind"),
                          StringRef("bind_front"), StringRef("bind_back"),
                          StringRef("mem_fn"), StringRef("visit"),
                          StringRef("async"), StringRef("thread"),
                          StringRef("jthread"), StringRef("function")},
                         Callee->getName()))
    return false;
  return llvm::none_of(Call->arguments(), [](const Expr *Argument) {
    const QualType Type = Argument->getType().getNonReferenceType();
    if (Type->isFunctionPointerType() || Type->isMemberFunctionPointerType())
      return true;
    const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
    if (!Record || !Record->hasDefinition())
      return false;
    if (Record->isLambda())
      return true;
    return llvm::any_of(Record->getDefinition()->methods(),
                        [](const CXXMethodDecl *Method) {
                          return Method->getOverloadedOperator() == OO_Call;
                        });
  });
}

/// Whether \p E, past implicit nodes and copies, is returned, to a caller
/// that can use it anywhere.
static bool isReturned(const Expr *E, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*E);
  for (int Depth = 0; Depth < 16; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    const DynTypedNode &Parent = Parents[0];
    if (Parent.get<ReturnStmt>() || Parent.get<CoreturnStmt>())
      return true;
    const auto *Construct = Parent.get<CXXConstructExpr>();
    if (!(Construct && Construct->getNumArgs() == 1 &&
          Construct->getConstructor()->isCopyOrMoveConstructor()) &&
        !Parent.get<ImplicitCastExpr>() && !Parent.get<ParenExpr>() &&
        !Parent.get<CXXBindTemporaryExpr>() &&
        !Parent.get<MaterializeTemporaryExpr>() &&
        !Parent.get<ExprWithCleanups>())
      return false;
    Node = Parent;
  }
  return false;
}

void ActorPhaseCheck::checkIterator(const Expr *Iterator,
                                    const FieldDecl *Field, unsigned Phases,
                                    ASTContext &Context, const Expr *Object,
                                    int Depth) {
  // Past as many copies as the code can have, what is done with it can't be
  // told, so it may remove.
  if (Depth > 256) {
    checkChange(Iterator, Object, nullptr, Field, Phases, Context);
    return;
  }
  // Returned, the caller can remove the entry it is at anywhere.
  if (isReturned(Iterator, Context)) {
    checkChange(Iterator, Object, nullptr, Field, Phases, Context);
    return;
  }
  const Expr *Child = nullptr;
  const DynTypedNode Parent = parentOf(Iterator, Context, Child);
  // A removal through a temporary iterator, like
  // `mp_Items.f_GetIterator().f_Remove()`.
  if (const auto *Member = Parent.get<MemberExpr>()) {
    const auto *Method = dyn_cast<CXXMethodDecl>(Member->getMemberDecl());
    if (Method && Method->getIdentifier() &&
        matchesNamePattern(Method->getName(), RemovingMethodList)) {
      checkChange(Member, Object, nullptr, Field, Phases, Context);
      return;
    }
    // Another iterator of the same collection that it makes, like
    // `iEntry.f_GetIterator()`, which can remove entries too.
    const CXXRecordDecl *IteratorClass =
        Iterator->getType().getNonReferenceType()->getAsCXXRecordDecl();
    const auto CallParents = instantiatedParents(*Member, Context);
    const auto *Call =
        CallParents.empty() ? nullptr : CallParents[0].get<CXXMemberCallExpr>();
    if (Method && Call && IteratorClass &&
        !Method->getReturnType()->isReferenceType()) {
      const CXXRecordDecl *Result =
          Method->getReturnType()->getAsCXXRecordDecl();
      if (Result &&
          Result->getCanonicalDecl() == IteratorClass->getCanonicalDecl())
        checkIterator(Call, Field, Phases, Context, Object, Depth + 1);
    }
    return;
  }
  if (const auto *Variable = Parent.get<VarDecl>()) {
    checkIteratorUses(Variable, Field, Phases, Context, Object, Depth);
    return;
  }
  // Either value of a conditional, `bA ? mp_Items.f_GetIterator(A) : ...`.
  if (const auto *Conditional = Parent.get<AbstractConditionalOperator>();
      Conditional && Conditional->getCond()->IgnoreParenImpCasts() !=
                         Child->IgnoreParenImpCasts()) {
    checkIterator(Conditional, Field, Phases, Context, Object, Depth + 1);
    return;
  }
  // A member function called through a pointer to it, `(iEntry.*fRemove)()`,
  // can be one that removes.
  if (const auto *Binary = Parent.get<BinaryOperator>();
      Binary && Binary->isPtrMemOp()) {
    checkChange(Binary, Object, nullptr, Field, Phases, Context);
    return;
  }
  // Assigned to a local, `iEntry = mp_Items.f_GetIterator()`.
  if (const auto *Assign = Parent.get<CXXOperatorCallExpr>();
      Assign && Assign->getOperator() == OO_Equal && Assign->getNumArgs() == 2 &&
      Assign->getArg(1)->IgnoreParenImpCasts() == Child->IgnoreParenImpCasts()) {
    if (const auto *Ref =
            dyn_cast<DeclRefExpr>(Assign->getArg(0)->IgnoreParenImpCasts()))
      if (const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl()))
        checkIteratorUses(Variable, Field, Phases, Context, Object, Depth + 1,
                          Assign->getArg(1));
    return;
  }
  // Given to a function, which can remove the entry it is at; the operators
  // of the iterator only move it, and the standard algorithms, like
  // `std::distance`, have no collection to remove from.
  // Also given to a function object, `fRemove(mp_Items.f_GetIterator())`.
  if (const auto *Call = Parent.get<CallExpr>();
      Call && (!isa<CXXOperatorCallExpr>(Call) ||
               isGivenToFunctionObject(cast<CXXOperatorCallExpr>(Call),
                                       Child))) {
    // Moving it gives the iterator, `std::move(iEntry)`.
    if (isMove(Call))
      checkIterator(Call, Field, Phases, Context, Object, Depth + 1);
    else if (!isStandardFunction(Call))
      checkChange(Call, Object, nullptr, Field, Phases, Context);
  }
  // A copy of it, which can remove too.
  else if (const auto *Construct = Parent.get<CXXConstructExpr>();
           Construct && Construct->getNumArgs() == 1 &&
           Construct->getConstructor()->isCopyOrMoveConstructor())
    checkIterator(Construct, Field, Phases, Context, Object, Depth + 1);
  // Kept in an object, like `CHolder{mp_Items.f_GetIterator()}`, which can
  // remove the entry it is at through it.
  else if (Parent.get<InitListExpr>() || Parent.get<CXXParenListInitExpr>() ||
           Parent.get<CXXConstructExpr>())
    checkChange(Iterator, Object, nullptr, Field, Phases, Context);
}

llvm::ArrayRef<const DeclRefExpr *>
ActorPhaseCheck::referencesTo(const FunctionDecl *Function,
                              const VarDecl *Variable) {
  auto Emplaced = References.try_emplace(Function);
  auto It = Emplaced.first;
  if (Emplaced.second && Function->getBody()) {
    // Also in the lambdas it makes, which reach its variables. Each once,
    // also the initializer of an init capture, which is reached both as
    // what initializes the capture and as the initializer of its variable.
    llvm::SmallPtrSet<const Stmt *, 32> Seen;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || !Seen.insert(S).second)
        return;
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
        if (const auto *Referred = dyn_cast<VarDecl>(Ref->getDecl()))
          It->second[Referred].push_back(Ref);
      if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
        for (const Expr *Init : Lambda->capture_inits())
          Visit(Init);
        for (const LambdaCapture &Capture : Lambda->captures())
          if (Capture.capturesVariable())
            if (const auto *Initialized =
                    dyn_cast<VarDecl>(Capture.getCapturedVar());
                Initialized && Initialized->isInitCapture())
              Visit(Initialized->getInit());
        Visit(Lambda->getBody());
        return;
      }
      for (const Stmt *Child : childrenOnce(S))
        Visit(Child);
    };
    Visit(Function->getBody());
  }
  auto Found = It->second.find(Variable);
  return Found == It->second.end() ? llvm::ArrayRef<const DeclRefExpr *>()
                                   : llvm::ArrayRef<const DeclRefExpr *>(
                                         Found->second);
}

void ActorPhaseCheck::checkIteratorUses(const VarDecl *Iterator,
                                        const FieldDecl *Field,
                                        unsigned Phases, ASTContext &Context,
                                        const Expr *Object, int Depth,
                                        const Expr *Origin) {
  // Copies assigned to each other, like `iA = iB; iB = iA;`, have their uses
  // looked at once while a use of the collection is, however many there are.
  // Past as many copies as the code can have, what is done with it can't be
  // told, so it may remove.
  if (Depth > 256) {
    if (Origin)
      checkChange(Origin, Object, nullptr, Field, Phases, Context);
    return;
  }
  using CExpanded = llvm::DenseSet<std::pair<const VarDecl *, const Expr *>>;
  static thread_local CExpanded *s_pExpanded = nullptr;
  std::optional<CExpanded> Owned;
  if (!s_pExpanded)
    s_pExpanded = &Owned.emplace();
  const llvm::scope_exit Release([&] {
    if (Owned)
      s_pExpanded = nullptr;
  });
  if (!s_pExpanded->insert({Iterator, Origin}).second)
    return;
  // Found from its declaration, which a copy assigned to later, like
  // `decltype(iEntry) iCopy;`, has no initializer to find it from.
  const auto Declarations = instantiatedParents(*Iterator, Context);
  const auto *Declaration =
      Declarations.empty() ? nullptr : Declarations[0].get<DeclStmt>();
  const Stmt *Start = Declaration ? static_cast<const Stmt *>(Declaration)
                                  : Iterator->getInit();
  const FunctionDecl *Enclosing =
      Start ? enclosingPhaseFunction(Start, Context) : nullptr;
  if (!Enclosing || !Enclosing->getBody())
    return;
  // Not after it is made another iterator on every path, `iEntry =
  // m_Other.f_GetIterator()`, unless that is what made this one, or it is
  // given itself or a copy of itself, `iEntry = iEntry` or `iEntry = iCopy`
  // with `auto iCopy = iEntry`, which keep its entry, also as either value of
  // a conditional, `iEntry = bKeep ? iEntry : iOther`.
  std::function<bool(const Expr *, int)> IsItself = [&](const Expr *Assigned,
                                                        int Level) {
    if (!Assigned || Level > 4)
      return false;
    const Expr *E = skipWrappers(Assigned)->IgnoreParenImpCasts();
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E))
      return IsItself(Conditional->getTrueExpr(), Level + 1) ||
             IsItself(Conditional->getFalseExpr(), Level + 1);
    const auto *Ref = dyn_cast<DeclRefExpr>(E);
    const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (!Variable)
      return false;
    if (Variable == Iterator)
      return true;
    if (!Variable->hasLocalStorage() || isa<ParmVarDecl>(Variable) ||
        isChangedVariable(Variable))
      return false;
    return IsItself(Variable->getInit(), Level + 1);
  };
  auto IsElsewhere = [&](const Stmt *Use) {
    const Expr *Assigned = assignedOnEveryPathBefore(Use, Iterator, Context);
    return Assigned && Assigned != Origin && !IsItself(Assigned, 0);
  };
  // Also through a pointer to it, like `(*piEntry).f_Remove()`, and as
  // either value of a conditional, `(bChoose ? iEntry : iOther).f_Remove()`,
  // or the last of a comma.
  const auto IteratorRef = declRefExpr(to(varDecl(equalsNode(Iterator))));
  const auto IteratorObject = anyOf(
      IteratorRef,
      unaryOperator(hasOperatorName("*"),
                    hasUnaryOperand(ignoringParenImpCasts(IteratorRef))));
  for (const auto &Nodes : match(
           findAll(cxxMemberCallExpr(
                       on(ignoringParenImpCasts(anyOf(
                           IteratorObject,
                           conditionalOperator(anyOf(
                               hasTrueExpression(
                                   ignoringParenImpCasts(IteratorObject)),
                               hasFalseExpression(
                                   ignoringParenImpCasts(IteratorObject)))),
                           binaryOperator(hasOperatorName(","),
                                          hasRHS(ignoringParenImpCasts(
                                              IteratorObject)))))))
                       .bind("call")),
           *Enclosing->getBody(), Context)) {
    const auto *Call = Nodes.getNodeAs<CXXMemberCallExpr>("call");
    const CXXMethodDecl *Method = Call->getMethodDecl();
    if (isNeverRun(Call, Context) || IsElsewhere(Call))
      continue;
    if (Method && Method->getIdentifier() &&
        matchesNamePattern(Method->getName(), RemovingMethodList))
      checkChange(Call, Object, nullptr, Field, Phases, Context);
    // Another iterator made from it, `iEntry.f_GetIterator()`, is of the
    // same collection.
    else if (const CXXRecordDecl *Result =
                 Method && !Method->getReturnType()->isReferenceType()
                     ? Method->getReturnType()->getAsCXXRecordDecl()
                     : nullptr;
             Result && Iterator->getType()
                               .getNonReferenceType()
                               ->getAsCXXRecordDecl() &&
             Result->getCanonicalDecl() ==
                 Iterator->getType()
                     .getNonReferenceType()
                     ->getAsCXXRecordDecl()
                     ->getCanonicalDecl())
      checkIterator(Call, Field, Phases, Context, Object, Depth + 1);
  }
  // A copy of the iterator, like `auto iCopy = iEntry`, can remove too.
  for (const DeclRefExpr *Ref : referencesTo(Enclosing, Iterator)) {
    if (isNeverRun(Ref, Context) || IsElsewhere(Ref))
      continue;
    const Expr *Child = nullptr;
    DynTypedNode Parent = parentOf(Ref, Context, Child);
    if (const auto *Cast = Parent.get<ImplicitCastExpr>())
      Parent = parentOf(Cast, Context, Child);
    if (isReturned(Ref, Context))
      checkChange(Ref, Object, nullptr, Field, Phases, Context);
    // A member function called through a pointer to it, `(iEntry.*fRemove)()`,
    // can be one that removes.
    else if (const auto *Binary = Parent.get<BinaryOperator>();
             Binary && Binary->isPtrMemOp())
      checkChange(Binary, Object, nullptr, Field, Phases, Context);
    else if (const auto *Construct = Parent.get<CXXConstructExpr>();
             Construct && Construct->getNumArgs() == 1 &&
             Construct->getConstructor()->isCopyOrMoveConstructor())
      checkIterator(Construct, Field, Phases, Context, Object, Depth + 1);
    // Kept in another object, `CHolder Holder(iEntry)`, which can remove the
    // entry it is at through it.
    else if (Parent.get<CXXConstructExpr>() || Parent.get<InitListExpr>() ||
             Parent.get<CXXParenListInitExpr>())
      checkChange(Ref, Object, nullptr, Field, Phases, Context);
    else if (const auto *Copy = Parent.get<VarDecl>(); Copy && Copy != Iterator)
      checkIteratorUses(Copy, Field, Phases, Context, Object, Depth + 1);
    // Assigned to another one, like `iCopy = iEntry`.
    else if (const auto *Assign = Parent.get<CXXOperatorCallExpr>();
             Assign && Assign->getOperator() == OO_Equal &&
             Assign->getNumArgs() == 2 && Assign->getArg(1) == Child) {
      const auto *Target =
          dyn_cast<DeclRefExpr>(Assign->getArg(0)->IgnoreParenImpCasts());
      const auto *Copy =
          Target ? dyn_cast<VarDecl>(Target->getDecl()) : nullptr;
      if (Copy && Copy != Iterator)
        checkIteratorUses(Copy, Field, Phases, Context, Object, Depth + 1,
                          Assign->getArg(1));
      else if (!Copy)
        checkChange(Assign, Object, nullptr, Field, Phases, Context);
    }
    // A pointer to it, `auto *piEntry = &iEntry`, removes through it.
    else if (const auto *Address = Parent.get<UnaryOperator>();
             Address && Address->getOpcode() == UO_AddrOf) {
      const Expr *AddressChild = nullptr;
      if (const auto *Alias =
              parentOf(Address, Context, AddressChild).get<VarDecl>())
        checkIteratorUses(Alias, Field, Phases, Context, Object, Depth + 1);
      else
        checkChange(Address, Object, nullptr, Field, Phases, Context);
    }
    else if (const auto *Call = Parent.get<CallExpr>();
             Call && (!isa<CXXOperatorCallExpr>(Call) ||
                      isGivenToFunctionObject(cast<CXXOperatorCallExpr>(Call),
                                              Child))) {
      // `std::addressof(iEntry)` gives a pointer to it, like `&iEntry`.
      const StringRef Name = calleeName(Call);
      if (isAddressOfCall(Call)) {
        const Expr *AddressChild = nullptr;
        if (const auto *Alias =
                parentOf(Call, Context, AddressChild).get<VarDecl>())
          checkIteratorUses(Alias, Field, Phases, Context, Object, Depth + 1);
        else
          checkChange(Call, Object, nullptr, Field, Phases, Context);
      } else if (isMove(Call))
        checkIterator(Call, Field, Phases, Context, Object, Depth + 1);
      // Exchanged, `std::exchange(iEntry, iOther)`, which returns it, or
      // gives it to what it replaces.
      else if (Call->getNumArgs() == 2 && (isExchangeFunction(Name))) {
        if (Call->getArg(0)->IgnoreParenImpCasts() ==
            Child->IgnoreParenImpCasts())
          checkIterator(Call, Field, Phases, Context, Object, Depth + 1);
        else if (const auto *TargetRef = dyn_cast<DeclRefExpr>(
                     Call->getArg(0)->IgnoreParenImpCasts());
                 TargetRef && isa<VarDecl>(TargetRef->getDecl()) &&
                 TargetRef->getDecl() != Iterator &&
                 cast<VarDecl>(TargetRef->getDecl())->hasLocalStorage())
          checkIteratorUses(cast<VarDecl>(TargetRef->getDecl()), Field,
                            Phases, Context, Object, Depth + 1);
        else
          checkChange(Call, Object, nullptr, Field, Phases, Context);
      }
      // Swapped with another, `std::swap(iEntry, iOther)`, which then is at
      // the entry.
      else if (const auto [Left, Right] = swappedArguments(Call); Left) {
        const Expr *Other =
            Left->IgnoreParenImpCasts() == Child->IgnoreParenImpCasts()
                ? Right
                : Left;
        const auto *OtherRef =
            dyn_cast<DeclRefExpr>(Other->IgnoreParenImpCasts());
        if (const auto *Swapped =
                OtherRef ? dyn_cast<VarDecl>(OtherRef->getDecl()) : nullptr;
            Swapped && Swapped != Iterator)
          checkIteratorUses(Swapped, Field, Phases, Context, Object,
                            Depth + 1);
        else
          checkChange(Call, Object, nullptr, Field, Phases, Context);
      } else if (!isStandardFunction(Call))
        checkChange(Call, Object, nullptr, Field, Phases, Context);
    }
    // Kept in an object, like `CHolder{iEntry}` or `CHolder(iEntry)`.
    else if (Parent.get<InitListExpr>() || Parent.get<CXXParenListInitExpr>())
      checkChange(Ref, Object, nullptr, Field, Phases, Context);
  }
}

/// Whether \p Site is in a lambda that isn't called where it is made, like a
/// callback kept in a member, which can run later, after construction.
static bool inDeferredLambda(const Stmt *Site, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Site);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    const DynTypedNode Parent = Parents[0];
    // From its body, also through its call operator, not what initializes
    // its captures.
    if (const auto *Lambda = Parent.get<LambdaExpr>();
        Lambda && !llvm::is_contained(Lambda->capture_inits(),
                                      Node.get<Expr>())) {
      // A coroutine, which goes on after it suspends, also after
      // construction.
      if (const CXXMethodDecl *Operator = Lambda->getCallOperator();
          Operator && isa_and_nonnull<CoroutineBodyStmt>(Operator->getBody()))
        return true;
      // Called right away, `[this] { ... }()`.
      DynTypedNode Up = Parent;
      const Expr *Child = Lambda;
      for (int Level = 0; Level < 8; ++Level) {
        const auto Outer = instantiatedParents(Up, Context);
        if (Outer.empty())
          return true;
        if (Outer[0].get<ImplicitCastExpr>() || Outer[0].get<ParenExpr>() ||
            Outer[0].get<MaterializeTemporaryExpr>()) {
          Up = Outer[0];
          Child = Up.get<Expr>();
          continue;
        }
        // Held in a local that is only called, `auto fInit = [this] { ...
        // }; fInit();`.
        if (const auto *Local = Outer[0].get<VarDecl>();
            Local && Local->hasLocalStorage() &&
            !Local->getType()->isReferenceType()) {
          const auto *Function =
              dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
          if (!Function || !Function->getBody())
            return true;
          // Each local told once, however many uses in it are asked about.
          struct COnlyCalled {
            llvm::DenseMap<const VarDecl *, bool> Locals;
          };
          auto &Locals = astCache<COnlyCalled>(Context).Locals;
          if (auto It = Locals.find(Local); It != Locals.end()) {
            if (!It->second)
              return true;
            break;
          }
          bool OnlyCalled = true;
          std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
            if (!S || !OnlyCalled)
              return;
            if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(S);
                Call && Call->getOperator() == OO_Call &&
                Call->getNumArgs() >= 1)
              if (const auto *Called = dyn_cast<DeclRefExpr>(
                      Call->getArg(0)->IgnoreParenImpCasts());
                  Called && Called->getDecl() == Local) {
                for (const Expr *Argument : llvm::drop_begin(Call->arguments()))
                  Visit(Argument);
                return;
              }
            if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
                Ref && Ref->getDecl() == Local)
              OnlyCalled = false;
            for (const Stmt *Child : childrenOnce(S))
              Visit(Child);
          };
          Visit(Function->getBody());
          Locals[Local] = OnlyCalled;
          if (!OnlyCalled)
            return true;
          break;
        }
        // The cleanup of a local scope guard, `auto Cleanup = g_OnScopeExit
        // / [this] { ... };`, which runs before the function returns, not of
        // a shared one, which what it is given can keep.
        if (const auto *Guard = Outer[0].get<CXXOperatorCallExpr>();
            Guard && Guard->getOperator() == OO_Slash &&
            Guard->getNumArgs() == 2 && Guard->getArg(1) == Child) {
          const Expr *Helper = skipWrappers(Guard->getArg(0));
          const CXXRecordDecl *Type =
              Helper->getType().getNonReferenceType()->getAsCXXRecordDecl();
          if (!isScopeExitHelper(Helper, /*AlsoActor=*/false) || !Type ||
              Type->getName().contains("Shared"))
            return true;
          DynTypedNode Made = Outer[0];
          for (int Wrapped = 0; Wrapped < 8; ++Wrapped) {
            const auto Above = instantiatedParents(Made, Context);
            if (Above.empty())
              return true;
            if (Above[0].get<ImplicitCastExpr>() ||
                Above[0].get<MaterializeTemporaryExpr>() ||
                Above[0].get<CXXBindTemporaryExpr>() ||
                Above[0].get<ExprWithCleanups>() ||
                Above[0].get<CXXConstructExpr>()) {
              Made = Above[0];
              continue;
            }
            const auto *Local = Above[0].get<VarDecl>();
            if (!Local || !Local->hasLocalStorage() ||
                Local->getType()->isReferenceType())
              return true;
            break;
          }
          break;
        }
        const auto *Call = Outer[0].get<CXXOperatorCallExpr>();
        if (!Call || Call->getOperator() != OO_Call || Call->getNumArgs() < 1 ||
            Call->getArg(0) != Child)
          return true;
        break;
      }
    }
    if (const auto *Function = Parent.get<FunctionDecl>()) {
      const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      if (!Method || !Method->getParent()->isLambda())
        return false;
    }
    if (Parent.get<FieldDecl>() || Parent.get<VarDecl>())
      return false;
    Node = Parent;
  }
  return false;
}

/// Whether \p Class derives from \p Base, looking at each class once,
/// unlike `isDerivedFrom`, which goes through each path of bases repeated in
/// the hierarchy.
static bool derivesFrom(const CXXRecordDecl *Class, const CXXRecordDecl *Base) {
  if (!Class || !Class->hasDefinition())
    return false;
  const CXXRecordDecl *Target = Base->getCanonicalDecl();
  llvm::SmallPtrSet<const CXXRecordDecl *, 16> Visited;
  llvm::SmallVector<const CXXRecordDecl *, 16> Worklist{Class};
  while (!Worklist.empty()) {
    const CXXRecordDecl *Current = Worklist.pop_back_val();
    if (!Current || !Current->hasDefinition())
      continue;
    for (const CXXBaseSpecifier &Specifier : Current->bases()) {
      const CXXRecordDecl *Next = Specifier.getType()->getAsCXXRecordDecl();
      if (!Next)
        continue;
      if (Next->getCanonicalDecl() == Target)
        return true;
      if (Visited.insert(Next->getCanonicalDecl()).second)
        Worklist.push_back(Next);
    }
  }
  return false;
}

/// Whether \p Object is a part of `this` held by value, a member of it, of
/// such a member, or an element of an array of them, `m_State` or
/// `m_States[0].m_Inner`, which is made and destroyed with it.
static bool isHeldByThis(const Expr *Object) {
  bool Member = false;
  for (int Depth = 0; Object && Depth < 64; ++Depth) {
    const Expr *E = Object->IgnoreParenImpCasts();
    if (isa<CXXThisExpr>(E))
      return Member;
    if (const auto *Access = dyn_cast<MemberExpr>(E)) {
      const auto *Field = dyn_cast<FieldDecl>(Access->getMemberDecl());
      if (!Field || Field->getType()->isReferenceType())
        return false;
      if (Access->isArrow() &&
          !isa<CXXThisExpr>(Access->getBase()->IgnoreParenImpCasts()))
        return false;
      Member = true;
      Object = Access->getBase();
      continue;
    }
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E);
        Subscript &&
        Subscript->getBase()->IgnoreParenImpCasts()->getType()->isArrayType()) {
      Object = Subscript->getBase()->IgnoreParenImpCasts();
      continue;
    }
    return false;
  }
  return false;
}

void ActorPhaseCheck::checkChange(const Expr *Site, const Expr *Object,
                                  const Expr *Key, const FieldDecl *Field,
                                  unsigned Phases, ASTContext &Context) {
  // Of a local object of its own, a copy like `auto Copy = mp_State`, whose
  // collections are its own, also captured by a lambda, `[&Copy] {
  // Copy.m_Items.f_Clear(); }`, or copied into one, `[Copy]`.
  for (const Expr *Root = Object; Root;) {
    Root = Root->IgnoreParenImpCasts();
    if (const auto *Member = dyn_cast<MemberExpr>(Root);
        Member && !Member->isArrow()) {
      Root = Member->getBase();
      continue;
    }
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Root))
      if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
          Local && Local->hasLocalStorage() &&
          !Local->getType()->isReferenceType() &&
          !Local->getType()->isPointerType())
        return;
    break;
  }
  const FunctionDecl *Enclosing = enclosingPhaseFunction(Site, Context);
  // What a callback that can run later does, `TCFunction<void ()> fClear =
  // [this] { ... }`, isn't done while the object is made or destroyed.
  const bool Deferred = inDeferredLambda(Site, Context);
  // Outside of a function, in the initializer of a member or a variable, which
  // runs where the object or variable is made, outside of any phase, unless
  // it is a member of the actor's own class, made by its constructor.
  const NamedDecl *Where = diagnosticDecl(Enclosing);
  if (!Enclosing) {
    DynTypedNode Node = DynTypedNode::create(*Site);
    for (int Depth = 0; Depth < 256 && !Where; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return;
      Node = Parents[0];
      if (const auto *Member = Node.get<FieldDecl>()) {
        const auto *FieldClass = cast<CXXRecordDecl>(Field->getParent());
        const auto *Class = dyn_cast<CXXRecordDecl>(Member->getParent());
        if (Class && isOwnActorObject(Object) && !Deferred &&
            (Class->getCanonicalDecl() == FieldClass->getCanonicalDecl() ||
             derivesFrom(Class, FieldClass) || isHeldByThis(Object)))
          return;
        Where = Member;
      } else if (const auto *Variable = Node.get<VarDecl>()) {
        Where = Variable;
      } else if (const auto *D = Node.get<Decl>()) {
        // Past a lambda, `m_fClear = [this] { ... }`, to what it is in.
        const auto *Record = dyn_cast<CXXRecordDecl>(D);
        if (const auto *Method = dyn_cast<CXXMethodDecl>(D))
          Record = Method->getParent();
        if (!Record || !Record->isLambda())
          return;
      }
    }
    if (!Where)
      return;
  }
  // Before any call of the actor can look the entries up, and after none can,
  // on the object being constructed or destroyed, also of a base of its
  // class, or of what it holds by value, `m_State.m_Items`, which are parts of
  // it.
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Enclosing);
      Method &&
      (isa<CXXConstructorDecl>(Method) || isa<CXXDestructorDecl>(Method)) &&
      isOwnActorObject(Object) && !Deferred) {
    const CXXRecordDecl *Class = Method->getParent();
    const auto *FieldClass = cast<CXXRecordDecl>(Field->getParent());
    if (Class->getCanonicalDecl() == FieldClass->getCanonicalDecl() ||
        derivesFrom(Class, FieldClass) || isHeldByThis(Object))
      return;
  }
  const unsigned EntryPhases = Phases & ~ActorPhase_Owner;
  const unsigned Current = contextPhases(Enclosing);
  if (Current && !(Current & ~EntryPhases) && isOwnActorObject(Object))
    return;
  // The call that added the entry with a fresh key removes it by that key.
  if ((Phases & ActorPhase_Owner) &&
      isOwnEntryKey(Key, Field, Enclosing, FreshKeyFunctionList, Context,
                    Object))
    return;
  if (!Phases) {
    diag(Site->getBeginLoc(),
         "%0 is declared to never lose entries, but is changed here in %1; "
         "remove its entries only in its constructor or destructor")
        << Field << Where;
    return;
  }
  if (!EntryPhases) {
    diag(Site->getBeginLoc(),
         "%0 is declared to lose entries only by the calls that added them, "
         "but is changed here in %1; remove only the entry this call added, "
         "by its fresh key")
        << Field << Where;
    return;
  }
  diag(Site->getBeginLoc(),
       "%0 is declared to lose entries only in %1, but is changed here in %2; "
       "change it in a function of %select{that phase|those phases}3 "
       "(DMibActorPhase)%select{|, or remove only the entry this call added, "
       "by its fresh key}4")
      << Field << phaseNames(Phases) << Where
      << (llvm::popcount(EntryPhases) > 1)
      << ((Phases & ActorPhase_Owner) != 0);
}

} // namespace clang::tidy::malterlib
