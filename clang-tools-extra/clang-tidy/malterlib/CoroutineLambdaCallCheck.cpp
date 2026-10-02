//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CoroutineLambdaCallCheck.h"
#include "../utils/OptionsUtils.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ExprCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopeExit.h"
#include <optional>

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

CoroutineLambdaCallCheck::CoroutineLambdaCallCheck(StringRef Name,
                                                   ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      SafeCallees(utils::options::parseStringList(
          listOption("SafeCallees", CVocabulary::builtIn("SafeCallees")))) {}

void CoroutineLambdaCallCheck::storeCheckOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "SafeCallees",
                utils::options::serializeStringList(SafeCallees));
}

void CoroutineLambdaCallCheck::registerMatchers(MatchFinder *Finder) {
  // Whether it is evaluated is told in `check`, for a lambda coroutine,
  // since telling it walks the parents.
  Finder->addMatcher(
      cxxOperatorCallExpr(hasOverloadedOperatorName("()")).bind("call"), this);
  Finder->addMatcher(lambdaExpr().bind("lambda"),
                     this);
}

/// The first use after a suspension of the captures of the lambda whose call
/// operator is \p Operator, when it is a protected coroutine whose closure
/// they live in or refer through. A lambda without captures has no state to
/// dangle, and neither has one with an explicit object parameter taken by
/// value, `this auto Self`, whose copy of the closure lives in the coroutine
/// frame.
static const Stmt *danglingCaptureUse(const CXXMethodDecl *Operator,
                                      ASTContext &Context) {
  if (Operator->getParent()->captures().empty() ||
      !isProtectedCoroutine(Operator) ||
      (Operator->isExplicitObjectMemberFunction() &&
       !Operator->getParamDecl(0)->getType()->isReferenceType()))
    return nullptr;
  return firstCaptureUseAfterSuspension(Operator, Context);
}

static bool keepsParameterPointer(const FunctionDecl *Function,
                                  unsigned iParameter, int Depth = 0);

/// Whether the constructor \p Constructor keeps the address of its reference
/// parameter \p iParameter in the object, like `CResult(int const &_Value) :
/// m_pValue(&_Value)` or a reference member bound to it, also through the
/// constructor it delegates to, `CResult(int const &_Value) :
/// CResult(&_Value)`, or of a base, `CResult(int const &_Value) :
/// CBase(_Value)`.
static bool keepsParameterAddressUncached(const CXXConstructorDecl *Constructor,
                                          unsigned iParameter, int Depth);

/// Like keepsParameterAddressUncached, each constructor and parameter told
/// once, which the constructors of bases that many classes share, `struct C1
/// : virtual B0, virtual C0`, are met through a number of paths that grows
/// exponentially with the depth.
static bool keepsParameterAddress(const CXXConstructorDecl *Constructor,
                                  unsigned iParameter, int Depth = 0) {
  if (!Constructor)
    return false;
  struct CKeeps {
    llvm::DenseMap<std::pair<const CXXConstructorDecl *, unsigned>, bool>
        Results;
  };
  auto &Results = astCache<CKeeps>(Constructor->getASTContext()).Results;
  const std::pair<const CXXConstructorDecl *, unsigned> Key(
      Constructor->getCanonicalDecl(), iParameter);
  if (auto It = Results.find(Key); It != Results.end())
    return It->second;
  Results[Key] = false;
  const bool Keeps =
      keepsParameterAddressUncached(Constructor, iParameter, Depth);
  Results[Key] = Keeps;
  return Keeps;
}

/// Whether \p Constructor, whose body can't be seen, defined in another file,
/// can keep the address of its reference parameter \p iParameter: unless it
/// is a copy or a move, which keeps what the other has, not it.
static bool mayKeepUnseen(const CXXConstructorDecl *Constructor,
                          unsigned iParameter) {
  return Constructor && !Constructor->getDefinition() &&
         !Constructor->isCopyOrMoveConstructor() &&
         iParameter < Constructor->getNumParams() &&
         Constructor->getParamDecl(iParameter)->getType()->isReferenceType();
}

static bool keepsParameterAddressUncached(const CXXConstructorDecl *Constructor,
                                          unsigned iParameter, int Depth) {
  if (!Constructor || iParameter >= Constructor->getNumParams() ||
      !Constructor->getParamDecl(iParameter)->getType()->isReferenceType())
    return false;
  const ParmVarDecl *Parameter = Constructor->getParamDecl(iParameter);
  const CXXConstructorDecl *Definition = Constructor;
  if (const FunctionDecl *Defined = Constructor->getDefinition())
    Definition = cast<CXXConstructorDecl>(Defined);
  Parameter = Definition->getParamDecl(iParameter);
  // Also a part of it, a member, `_Data.m_Value`, or an element of an array
  // member, which is in what it refers to.
  // Each expression once, which a chain of conditionals of the same
  // references, `int const &rNext = b ? rPrevious : rPrevious`, meets a number
  // of times that doubles with each, and one that refers to itself, `int const
  // &r = b ? r : g_Value`, again while it is told.
  std::function<bool(const Expr *)> IsParameterOf;
  llvm::DenseMap<const Expr *, bool> IsParameterResults;
  std::function<bool(const Expr *)> IsParameterUncached;
  IsParameterOf = [&](const Expr *E) -> bool {
    if (!E)
      return false;
    if (auto It = IsParameterResults.find(E); It != IsParameterResults.end())
      return It->second;
    IsParameterResults[E] = false;
    const bool Result = IsParameterUncached(E);
    IsParameterResults[E] = Result;
    return Result;
  };
  auto IsParameter = [&](const Expr *E) { return IsParameterOf(E); };
  IsParameterUncached = [&](const Expr *E) -> bool {
    for (int Level = 0; E; ++Level) {
      // Deeper than can be looked into, it can be.
      if (Level >= 256)
        return true;
      E = E->IgnoreParenImpCasts();
      // What designates what it changes, `++_Value` or `(_Value = 1)`, and
      // the last of a comma.
      if (const auto *Step = dyn_cast<UnaryOperator>(E);
          Step && Step->isPrefix() && Step->isIncrementDecrementOp()) {
        E = Step->getSubExpr();
        continue;
      }
      if (const auto *Binary = dyn_cast<BinaryOperator>(E);
          Binary && Binary->isGLValue() &&
          (Binary->isAssignmentOp() || Binary->getOpcode() == BO_Comma)) {
        E = Binary->getOpcode() == BO_Comma ? Binary->getRHS()
                                            : Binary->getLHS();
        continue;
      }
      if (const auto *Member = dyn_cast<MemberExpr>(E);
          Member && !Member->isArrow() &&
          isa<FieldDecl>(Member->getMemberDecl()))
        E = Member->getBase();
      else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E);
               Subscript && Subscript->getBase()
                                ->IgnoreParenImpCasts()
                                ->getType()
                                ->isConstantArrayType())
        E = Subscript->getBase();
      // Also through a local reference of the constructor bound to it,
      // `int const &Alias = _Value`.
      else if (const auto *Named = dyn_cast<DeclRefExpr>(E);
               Named && isa<VarDecl>(Named->getDecl()) &&
               cast<VarDecl>(Named->getDecl())->hasLocalStorage() &&
               !isa<ParmVarDecl>(Named->getDecl()) &&
               cast<VarDecl>(Named->getDecl())->getType()->isReferenceType() &&
               cast<VarDecl>(Named->getDecl())->getInit())
        E = cast<VarDecl>(Named->getDecl())->getInit();
      // Also through a cast to a reference, `static_cast<int const &>(_Value)`.
      else if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
               Cast && Cast->isGLValue())
        E = Cast->getSubExpr();
      // Also either value of a conditional, `bChoose ? _Value : g_Value`.
      else if (const auto *Conditional = dyn_cast<ConditionalOperator>(E);
               Conditional && Conditional->isGLValue()) {
        bool Chosen = false;
        if (!Conditional->getCond()->isValueDependent() &&
            Conditional->getCond()->EvaluateAsBooleanCondition(
                Chosen, Definition->getASTContext()))
          E = Chosen ? Conditional->getTrueExpr() : Conditional->getFalseExpr();
        else
          return IsParameterOf(Conditional->getTrueExpr()) ||
                 IsParameterOf(Conditional->getFalseExpr());
      }
      // Also what a helper gives back of what it is given by reference,
      // `fg_Const(_Value)` of `return _Value;`.
      else if (const auto *Call = dyn_cast<CallExpr>(E);
               Call && Call->isGLValue() && returnedArguments(Call).size() == 1)
        E = returnedArguments(Call).front();
      else
        break;
    }
    const auto *Ref = dyn_cast_or_null<DeclRefExpr>(E);
    return Ref && Ref->getDecl() == Parameter;
  };
  // Its address, `&_Value` or `std::addressof(_Value)`, also cast to another
  // pointer, `static_cast<void const *>(&_Value)`, as either of a
  // conditional, `bKeep ? &_Value : nullptr`, the last of a comma, or through
  // a local pointer of the constructor, `int const *pSaved = &_Value;`, what
  // it holds there.
  // Each expression once, which a chain of conditionals of the same locals,
  // `int const *pNext = b ? pPrevious : pPrevious`, meets a number of times
  // that doubles with each; one met again while it is told tells nothing.
  llvm::DenseMap<const Expr *, bool> AddressResults;
  std::function<bool(const Expr *, int)> IsAddressAtUncached;
  std::function<bool(const Expr *, int)> IsAddressAt = [&](const Expr *E,
                                                           int Level) {
    if (!E)
      return false;
    if (auto It = AddressResults.find(E); It != AddressResults.end())
      return It->second;
    AddressResults[E] = false;
    const bool Result = IsAddressAtUncached(E, Level);
    AddressResults[E] = Result;
    return Result;
  };
  IsAddressAtUncached = [&](const Expr *E, int Level) {
    // Deeper than can be looked into, it can be.
    if (Level > 256)
      return true;
    E = E->IgnoreParenImpCasts();
    for (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
         Cast && Cast->getType()->isPointerType();
         Cast = dyn_cast<ExplicitCastExpr>(E))
      E = Cast->getSubExpr()->IgnoreParenImpCasts();
    // Only the one a constant condition chooses, `false ? &_Value : nullptr`.
    if (const auto *Conditional = dyn_cast<ConditionalOperator>(E)) {
      bool Chosen = false;
      if (!Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(
              Chosen, Definition->getASTContext()))
        return IsAddressAt(Chosen ? Conditional->getTrueExpr()
                                  : Conditional->getFalseExpr(),
                           Level + 1);
      return IsAddressAt(Conditional->getTrueExpr(), Level + 1) ||
             IsAddressAt(Conditional->getFalseExpr(), Level + 1);
    }
    if (const auto *Comma = dyn_cast<BinaryOperator>(E);
        Comma && Comma->getOpcode() == BO_Comma)
      return IsAddressAt(Comma->getRHS(), Level + 1);
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E))
      if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
          Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
          Local->getType()->isPointerType())
        return IsAddressAt(assignedValueAt(Local, Ref), Level + 1);
    if (const auto *Address = dyn_cast<UnaryOperator>(E);
        Address && Address->getOpcode() == UO_AddrOf)
      return IsParameter(Address->getSubExpr());
    const auto *AddressOf = dyn_cast<CallExpr>(E);
    if (isAddressOfCall(AddressOf))
      return IsParameter(AddressOf->getArg(0));
    // What a helper whose body can be seen gives back: the address of what
    // it is given by reference, `fg_Address(_Value)` of `return &_Value;`, or
    // a pointer as it is.
    if (const auto *Helper = dyn_cast<CallExpr>(E))
      return llvm::any_of(returnedArguments(Helper, /*Addressed=*/true),
                          IsParameter) ||
             llvm::any_of(returnedArguments(Helper), [&](const Expr *Argument) {
               return IsAddressAt(Argument, Level + 1);
             });
    return false;
  };
  auto IsAddress = [&](const Expr *E) { return IsAddressAt(E, 0); };
  for (const CXXCtorInitializer *Initializer : Definition->inits()) {
    const Expr *Init = Initializer->getInit();
    if (!Init || !Initializer->isWritten())
      continue;
    Init = Init->IgnoreImplicit();
    // Also through the constructor of a base, `CResult(int const &_Value) :
    // CBase(_Value)`, whose members are in the object.
    if (const auto *Delegated = dyn_cast<CXXConstructExpr>(Init);
        Delegated && (Initializer->isDelegatingInitializer() ||
                      Initializer->isBaseInitializer())) {
      // Deeper than can be looked into, it can be kept.
      if (Depth >= 256)
        return true;
      for (unsigned iArgument = 0; iArgument < Delegated->getNumArgs();
           ++iArgument) {
        const Expr *Argument = Delegated->getArg(iArgument);
        if ((IsAddress(Argument) &&
             keepsParameterPointer(Delegated->getConstructor(), iArgument)) ||
            (IsParameter(Argument) &&
             keepsParameterAddress(Delegated->getConstructor(), iArgument,
                                   Depth + 1)))
          return true;
      }
      continue;
    }
    // Also braced, `m_pValue{&_Value}`.
    if (const auto *List = dyn_cast<ParenListExpr>(Init);
        List && List->getNumExprs() == 1)
      Init = List->getExpr(0)->IgnoreImplicit();
    else if (const auto *Braced = dyn_cast<InitListExpr>(Init);
             Braced && Braced->getNumInits() == 1 &&
             !Init->getType()->isRecordType())
      Init = Braced->getInit(0)->IgnoreImplicit();
    if (IsAddress(Init) &&
        !resetsFirst(Definition, Initializer->getMember(), IsAddress))
      return true;
    if (const FieldDecl *Field = Initializer->getMember();
        Field && Field->getType()->isReferenceType() && IsParameter(Init))
      return true;
  }
  // Or in its body, `m_pValue = &_Value;`, but not in a local of it, `int
  // const *pScratch; pScratch = &_Value;`, which is gone after it.
  bool Kept = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Kept || isa<LambdaExpr>(S))
      return;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->getOpcode() == BO_Assign && IsAddress(Assign->getRHS())) {
      const auto *Target =
          dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts());
      const auto *Local = Target ? dyn_cast<VarDecl>(Target->getDecl()) : nullptr;
      // Also through a local reference to a local, `int const *&pAlias =
      // pScratch`, which is gone after it too.
      for (int Level = 0;
           Local && Local->getType()->isReferenceType() &&
           Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
           Local->getInit() && Level < 256;
           ++Level) {
        const auto *Bound =
            dyn_cast<DeclRefExpr>(Local->getInit()->IgnoreParenImpCasts());
        Local = Bound ? dyn_cast<VarDecl>(Bound->getDecl()) : nullptr;
      }
      if (!Local || !Local->hasLocalStorage() ||
          Local->getType()->isReferenceType())
        Kept = true;
    }
    // Not in a branch of an `if constexpr` that is discarded, nor one that a
    // constant condition never takes.
    for (const Stmt *Child : evaluatedChildren(S, Definition->getASTContext()))
      Visit(Child);
  };
  Visit(Definition->getBody());
  return Kept;
}

/// Whether the constructor \p Constructor can keep the pointer its parameter
/// at \p iParameter is given, in a member, `m_pValue(_pValue)`, or what can,
/// also a reference member bound to what it points to, `m_Value(*_pValue)` of
/// `int const &m_Value`, rather than only reading through it, `m_Value(
/// *_pValue)` of `int m_Value`; one whose body can't be seen can. Also of
/// another function it gives it to, like a helper that only copies what it
/// points to, `f_SetStr(_pStr)`, or a constructor of a base.
static bool keepsParameterPointerImpl(const FunctionDecl *Definition,
                                      unsigned iParameter, int Depth);

static bool keepsParameterPointer(const FunctionDecl *Function,
                                  unsigned iParameter, int Depth) {
  const FunctionDecl *Definition =
      Function ? Function->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody() ||
      iParameter >= Definition->getNumParams() || Depth > 8)
    return true;
  // Each once, also of helpers that call each other many times, and one that
  // calls itself, `f_SetStr(_pStr + 1)`, keeps it only by what else it does
  // with it.
  using CKey = std::pair<const FunctionDecl *, unsigned>;
  static thread_local llvm::DenseMap<CKey, bool> *s_pKept = nullptr;
  std::optional<llvm::DenseMap<CKey, bool>> Owned;
  if (!s_pKept)
    s_pKept = &Owned.emplace();
  const llvm::scope_exit Release([&] {
    if (Owned)
      s_pKept = nullptr;
  });
  const CKey Key{Definition->getCanonicalDecl(), iParameter};
  if (auto It = s_pKept->find(Key); It != s_pKept->end())
    return It->second;
  s_pKept->try_emplace(Key, false);
  const bool Kept = keepsParameterPointerImpl(Definition, iParameter, Depth);
  (*s_pKept)[Key] = Kept;
  return Kept;
}

static bool keepsParameterPointerImpl(const FunctionDecl *Definition,
                                      unsigned iParameter, int Depth) {
  const ParmVarDecl *Parameter = Definition->getParamDecl(iParameter);
  if (!Parameter->getType()->isPointerType())
    return true;
  ASTContext &Context = Definition->getASTContext();
  bool Kept = false;
  // What the initializer of a reference member being looked at binds it to.
  const Expr *Bound = nullptr;
  // Not what it points to bound to a reference member, `m_Value(*_pValue)` of
  // `int const &m_Value`, or its address taken, `m_pValue(&_pValue->m_Value)`,
  // also of a member of it, which keep it, from \p Reached, what designates
  // it; bound to a local reference, that is followed too.
  llvm::SmallPtrSet<const ValueDecl *, 4> Aliases;
  auto FollowPointee = [&](const Expr *Reached) {
    for (int Level = 0; Reached && !Kept; ++Level) {
      if (Level >= 256) {
        Kept = true;
        break;
      }
      if (Bound && Reached->IgnoreParenImpCasts() == Bound) {
        Kept = true;
        break;
      }
      const auto Above = instantiatedParents(*Reached, Context);
      if (const auto *Alias = Above.empty() ? nullptr : Above[0].get<VarDecl>();
          Alias && Alias->hasLocalStorage() &&
          Alias->getType()->isReferenceType() && Alias->getInit() &&
          Alias->getInit()->IgnoreParenImpCasts() ==
              Reached->IgnoreParenImpCasts()) {
        Aliases.insert(Alias);
        break;
      }
      const auto *Next = Above.empty() ? nullptr : Above[0].get<Expr>();
      const auto *Field = dyn_cast_or_null<MemberExpr>(Next);
      if (const auto *Address = dyn_cast_or_null<UnaryOperator>(Next);
          Address && Address->getOpcode() == UO_AddrOf)
        Kept = true;
      else if (isa_and_nonnull<ParenExpr>(Next) ||
               (Field && !Field->isArrow()) ||
               (isa_and_nonnull<ImplicitCastExpr>(Next) &&
                cast<ImplicitCastExpr>(Next)->getCastKind() == CK_NoOp))
        Reached = Next;
      else
        break;
    }
  };
  // A use of it that isn't a read through it, `*_pValue`, `_pValue->m_X` or
  // `_pValue[0]`, also moved along it first, `*(_pValue + 1)`.
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Kept || isa<LambdaExpr>(S))
      return;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Ref->getDecl() == Parameter) {
      const Expr *Read = Ref;
      for (int Level = 0; Level < 8; ++Level) {
        const auto Parents = instantiatedParents(*Read, Context);
        const auto *Parent =
            Parents.empty() ? nullptr : Parents[0].get<Expr>();
        if (Parent && isa<ImplicitCastExpr, ParenExpr>(Parent)) {
          Read = Parent;
          continue;
        }
        if (const auto *Offset = dyn_cast_or_null<BinaryOperator>(Parent);
            Offset && Offset->isAdditiveOp() &&
            Offset->getType()->isPointerType()) {
          Read = Parent;
          continue;
        }
        // Also stepped, `*_pFrom++`, which gives where it pointed.
        if (const auto *Step = dyn_cast_or_null<UnaryOperator>(Parent);
            Step && Step->isIncrementDecrementOp() &&
            Step->getType()->isPointerType()) {
          Read = Parent;
          continue;
        }
        // Given to a function whose body can be seen, what that does with
        // it, not to a virtual one an override can run instead.
        const auto *Call = dyn_cast_or_null<CallExpr>(Parent);
        const auto *Construct = dyn_cast_or_null<CXXConstructExpr>(Parent);
        if ((Call && !isa<CXXOperatorCallExpr>(Call) &&
             Call->getDirectCallee() && !callsOverride(Call)) ||
            Construct) {
          const FunctionDecl *Callee =
              Call ? Call->getDirectCallee() : Construct->getConstructor();
          const auto Arguments =
              Call ? llvm::ArrayRef(Call->getArgs(), Call->getNumArgs())
                   : llvm::ArrayRef(Construct->getArgs(),
                                    Construct->getNumArgs());
          const auto *Position = llvm::find(Arguments, Read);
          if (Position == Arguments.end() ||
              keepsParameterPointer(Callee, Position - Arguments.begin(),
                                    Depth + 1))
            Kept = true;
          break;
        }
        const auto *Deref = dyn_cast_or_null<UnaryOperator>(Parent);
        const auto *Member = dyn_cast_or_null<MemberExpr>(Parent);
        const auto *Subscript = dyn_cast_or_null<ArraySubscriptExpr>(Parent);
        if (!((Deref && Deref->getOpcode() == UO_Deref) ||
              (Member && Member->isArrow()) ||
              (Subscript && Subscript->getBase() == Read)))
          Kept = true;
        if (!Kept)
          FollowPointee(Parent);
        break;
      }
    }
    // A local reference to what it points to, `int const &Alias = *_pValue`,
    // is that.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Aliases.contains(Ref->getDecl()))
      FollowPointee(Ref);
    for (const Stmt *Child : evaluatedChildren(S, Context))
      Visit(Child);
  };
  if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Definition))
    for (const CXXCtorInitializer *Initializer : Constructor->inits())
      if (Initializer->isWritten()) {
        const FieldDecl *Field = Initializer->getMember();
        Bound = Field && Field->getType()->isReferenceType()
                    ? Initializer->getInit()->IgnoreParenImpCasts()
                    : nullptr;
        Visit(Initializer->getInit());
      }
  Bound = nullptr;
  Visit(Definition->getBody());
  return Kept;
}

/// Whether what the member function \p Method returns can be in its object,
/// unless its body can be seen to return only what isn't, like a string
/// literal, `return "Name";`, or a global: what mentions neither the object
/// nor a local, other than one that keeps what it is made from, `int const
/// *pValue = &g_Value; return pValue;`.
/// Called on \p Object, a virtual one that isn't final runs the override of
/// its dynamic type, which is its type only when it is an object itself, a
/// variable or a member of one, not a reference or cast, `static_cast<CBase
/// const &>(Value).f_Get()`; another can be overridden to.
static bool mayReturnIntoObject(const CXXMethodDecl *Method,
                                const Expr *Object = nullptr) {
  if (Method && Method->isVirtual() && !Method->hasAttr<FinalAttr>() &&
      !Method->getParent()->hasAttr<FinalAttr>()) {
    const Expr *Named = Object ? Object->IgnoreParenImpCasts() : nullptr;
    const ValueDecl *Declared = nullptr;
    if (const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Named))
      Declared = Ref->getDecl();
    else if (const auto *Member = dyn_cast_or_null<MemberExpr>(Named);
             Member && !Member->isArrow())
      Declared = Member->getMemberDecl();
    // Also one that refers to a final class, which is what it refers to.
    const CXXRecordDecl *Declaring =
        Declared
            ? Declared->getType().getNonReferenceType()->getAsCXXRecordDecl()
            : nullptr;
    const CXXRecordDecl *Dynamic =
        Declaring && (!Declared->getType()->isReferenceType() ||
                      (Declaring->hasDefinition() &&
                       Declaring->getDefinition()->hasAttr<FinalAttr>()))
            ? Declaring
            : nullptr;
    Method = Dynamic && Dynamic->hasDefinition()
                 ? Method->getCorrespondingMethodInClass(
                       Dynamic->getDefinition(), /*MayBeBase=*/true)
                 : nullptr;
    if (!Method)
      return true;
  }
  const FunctionDecl *Definition = Method ? Method->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody())
    return true;
  // What the initializer of each local followed mentions, also when it is
  // returned again; one being followed mentions it, as far as it can tell.
  llvm::DenseMap<const VarDecl *, bool> Followed;
  std::function<bool(const Stmt *)> MentionsObject = [&](const Stmt *S) {
    if (!S)
      return false;
    if (isa<CXXThisExpr>(S))
      return true;
    // Of a conditional, what it gives, not what chooses it, `m_bFirst ?
    // &g_First : &g_Second`.
    if (const auto *Conditional = dyn_cast<ConditionalOperator>(S))
      return MentionsObject(Conditional->getTrueExpr()) ||
             MentionsObject(Conditional->getFalseExpr());
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
      if (const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
          Variable && Variable->hasLocalStorage()) {
        if (isa<ParmVarDecl>(Variable) || !Variable->getInit() ||
            !isUnchangedAt(Ref))
          return true;
        auto [It, New] = Followed.try_emplace(Variable, true);
        if (!New)
          return It->second;
        const bool Mentions = MentionsObject(Variable->getInit());
        Followed[Variable] = Mentions;
        return Mentions;
      }
    // A static local, `static int const *s_pValue`, has what it was made
    // with or what the function assigns it, `s_pValue = &m_Value`.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
      if (const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
          Variable && Variable->isStaticLocal() &&
          !Variable->getType().isConstQualified()) {
        auto [It, New] = Followed.try_emplace(Variable, true);
        if (!New)
          return It->second;
        bool Mentions =
            Variable->getInit() && MentionsObject(Variable->getInit());
        std::function<void(const Stmt *)> Assigned = [&](const Stmt *Inner) {
          if (!Inner || Mentions)
            return;
          if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
              Assign && Assign->isAssignmentOp())
            if (const auto *Target = dyn_cast<DeclRefExpr>(
                    Assign->getLHS()->IgnoreParenImpCasts());
                Target && Target->getDecl() == Variable)
              Mentions = MentionsObject(Assign->getRHS());
          for (const Stmt *Child : childrenOnce(Inner))
            Assigned(Child);
        };
        Assigned(Definition->getBody());
        Followed[Variable] = Mentions;
        return Mentions;
      }
    return llvm::any_of(childrenOnce(S), MentionsObject);
  };
  bool Into = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Into || isa<LambdaExpr>(S))
      return;
    if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
      Into = MentionsObject(Return->getRetValue());
      return;
    }
    // Not in a branch of an `if constexpr` that is discarded.
    for (const Stmt *Child : evaluatedChildren(S, Definition->getASTContext()))
      Visit(Child);
  };
  Visit(Definition->getBody());
  return Into;
}

/// Where the result of the lambda of the call operator \p Operator is made
/// from the address of what it captured by copy, which lives in its closure:
/// `co_return &Value`, also through a local, `auto *pValue = &Value;
/// co_return pValue;`, or a view made of it.
static const Stmt *capturedAddressInResult(const CXXMethodDecl *Operator) {
  ASTContext &Context = Operator->getASTContext();
  const CXXRecordDecl *Closure = Operator->getParent();
  const Stmt *LambdaBodyStmt = Operator->getBody();
  if (!LambdaBodyStmt)
    return nullptr;
  llvm::SmallPtrSet<const ValueDecl *, 4> Copies;
  for (const LambdaCapture &Capture : Closure->captures())
    if (Capture.capturesVariable() && Capture.getCaptureKind() == LCK_ByCopy)
      Copies.insert(Capture.getCapturedVar());
  // And the copy of the closure that an explicit object parameter taken by
  // value is, `[Value = 1](this auto Self)`, which the frame holds.
  if (Operator->isExplicitObjectMemberFunction() &&
      !Operator->getParamDecl(0)->getType()->isReferenceType())
    Copies.insert(Operator->getParamDecl(0));
  // A copy of the object, `[*this]`, is in the closure too.
  const bool CopiesThis =
      llvm::any_of(Closure->captures(), [](const LambdaCapture &Capture) {
        return Capture.capturesThis() &&
               Capture.getCaptureKind() == LCK_StarThis;
      });
  if (Copies.empty() && !CopiesThis)
    return nullptr;
  const Stmt *Found = nullptr;
  // Whether \p Object, through its members and elements, is a copy, also
  // either of a conditional, `&(bChoose ? Value : g_Value)`, each conditional
  // once, also in a chain of references bound to them, `int const &r1 =
  // bChoose ? r0 : r0`. Past as many as the code can have, it can be.
  llvm::DenseMap<const Expr *, bool> Conditionals;
  std::function<bool(const Expr *, int)> IsInCopy;
  auto ConditionalInCopy = [&](const ConditionalOperator *Conditional,
                               int Depth) {
    if (auto It = Conditionals.find(Conditional); It != Conditionals.end())
      return It->second;
    bool In = true;
    if (Depth < 256) {
      bool Chosen = false;
      if (!Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(Chosen, Context))
        In = IsInCopy(Chosen ? Conditional->getTrueExpr()
                             : Conditional->getFalseExpr(),
                      Depth + 1);
      else
        In = IsInCopy(Conditional->getTrueExpr(), Depth + 1) ||
             IsInCopy(Conditional->getFalseExpr(), Depth + 1);
    }
    Conditionals[Conditional] = In;
    return In;
  };
  IsInCopy = [&](const Expr *Object, int Depth) {
    int Followed = 0;
    int Aliases = 0;
    Object = Object->IgnoreParenImpCasts();
    while (true) {
      if (const auto *Conditional = dyn_cast<ConditionalOperator>(Object);
          Conditional && Conditional->isGLValue())
        return ConditionalInCopy(Conditional, Depth);
      // A static member, `Data.ms_Value`, isn't in it.
      if (const auto *Member = dyn_cast<MemberExpr>(Object);
          Member && !isa<FieldDecl>(Member->getMemberDecl()))
        return false;
      if (const auto *Member = dyn_cast<MemberExpr>(Object);
          Member &&
          (!Member->isArrow() ||
           (CopiesThis &&
            isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()))))
        Object = Member->getBase()->IgnoreParenImpCasts();
      else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Object);
               Subscript && Subscript->getBase()->IgnoreParenImpCasts()
                                ->getType()
                                ->isArrayType())
        Object = Subscript->getBase()->IgnoreParenImpCasts();
      // A cast of it, `static_cast<int const &>(Value)`.
      else if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Object);
               Cast && Cast->isGLValue())
        Object = Cast->getSubExpr()->IgnoreParenImpCasts();
      // What a prefix increment gives, `++Value`, an assignment, `(Value =
      // 1)`, or a comma, `(fg_Log(), Value)`, which is it.
      else if (const auto *Unary = dyn_cast<UnaryOperator>(Object);
               Unary && (Unary->getOpcode() == UO_PreInc ||
                         Unary->getOpcode() == UO_PreDec))
        Object = Unary->getSubExpr()->IgnoreParenImpCasts();
      else if (const auto *Binary = dyn_cast<BinaryOperator>(Object);
               Binary && Binary->isGLValue() &&
               (Binary->isAssignmentOp() || Binary->getOpcode() == BO_Comma))
        Object = (Binary->getOpcode() == BO_Comma ? Binary->getRHS()
                                                  : Binary->getLHS())
                     ->IgnoreParenImpCasts();
      // A local reference to it, `auto const &Alias = Value`.
      else if (const auto *Ref = dyn_cast<DeclRefExpr>(Object);
               Ref && !Copies.contains(Ref->getDecl()) &&
               isa<VarDecl>(Ref->getDecl()) &&
               cast<VarDecl>(Ref->getDecl())->getType()->isReferenceType() &&
               cast<VarDecl>(Ref->getDecl())->hasLocalStorage() &&
               cast<VarDecl>(Ref->getDecl())->getInit() &&
               // Each is bound to one declared before it, so a chain of them
               // ends.
               ++Aliases < 256)
        Object = cast<VarDecl>(Ref->getDecl())->getInit()->IgnoreParenImpCasts();
      // What a function that can be looked into returns of what it is given
      // by reference, `fg_Const(Value)`; past as many as code can have, it
      // may be.
      else if (const auto *Call = dyn_cast<CallExpr>(Object);
               Call && Call->isGLValue() && Call->getDirectCallee()) {
        if (++Followed > 1024)
          return true;
        const FunctionDecl *Definition = Call->getDirectCallee()->getDefinition();
        const auto *Body = Definition ? dyn_cast_or_null<CompoundStmt>(
                                            Definition->getBody())
                                      : nullptr;
        const auto *Return =
            Body && Body->size() == 1 ? dyn_cast<ReturnStmt>(Body->body_front())
                                      : nullptr;
        const auto *Returned =
            Return && Return->getRetValue()
                ? dyn_cast<DeclRefExpr>(
                      Return->getRetValue()->IgnoreParenImpCasts())
                : nullptr;
        const auto *Parameter =
            Returned ? dyn_cast<ParmVarDecl>(Returned->getDecl()) : nullptr;
        if (!Parameter ||
            Parameter->getFunctionScopeIndex() >= Call->getNumArgs())
          break;
        Object = Call->getArg(Parameter->getFunctionScopeIndex())
                     ->IgnoreParenImpCasts();
      } else
        break;
    }
    if (CopiesThis && isa<CXXThisExpr>(Object))
      return true;
    const auto *Ref = dyn_cast<DeclRefExpr>(Object);
    return Ref && Copies.contains(Ref->getDecl());
  };
  // Each use of a local is followed to its values once, which a chain of
  // copies, `auto p1 = p0`, goes through.
  llvm::SmallPtrSet<const DeclRefExpr *, 8> Followed;
  // Each expression is searched once, also one that both a helper's returned
  // argument and the helper's children reach, `fg_Same(fg_Same(...))`.
  llvm::SmallPtrSet<const Stmt *, 32> Searched;
  std::function<void(const Stmt *, int)> Search = [&](const Stmt *S,
                                                      int Depth) {
    if (!S || Found || isa<LambdaExpr>(S) || !Searched.insert(S).second)
      return;
    // Past as many steps as code can have, it may be.
    if (Depth > 1024) {
      Found = S;
      return;
    }
    // Of a comma only what it gives, not what it discards, `(&Value,
    // &g_Value)`.
    if (const auto *Comma = dyn_cast<BinaryOperator>(S);
        Comma && Comma->getOpcode() == BO_Comma) {
      Search(Comma->getRHS(), Depth);
      return;
    }
    // Only the value a conditional with a constant condition gives, `true ?
    // nullptr : &Value`.
    if (const auto *Conditional = dyn_cast<ConditionalOperator>(S)) {
      bool Condition = false;
      if (!Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(Condition,
                                                             Context)) {
        Search(Condition ? Conditional->getTrueExpr()
                         : Conditional->getFalseExpr(),
               Depth);
        return;
      }
    }
    // A value that can't refer to anything, like the `int` that
    // `fg_Read(&Value)` returns.
    if (const auto *Value = dyn_cast<Expr>(S);
        Value && !Value->isGLValue() &&
        (Value->getType()->isArithmeticType() ||
         Value->getType()->isEnumeralType() || Value->getType()->isVoidType()))
      return;
    // A value read or copied from what an expression designates, like the
    // string `*pContents` of a map, keeps no address of it; a local is
    // followed to what it is made from.
    const auto IsLocalRef = [](const Expr *E) {
      return isa<DeclRefExpr>(E->IgnoreParenImpCasts());
    };
    if (const auto *Read = dyn_cast<ImplicitCastExpr>(S);
        Read && Read->getCastKind() == CK_LValueToRValue &&
        !IsLocalRef(Read->getSubExpr()))
      return;
    if (const auto *Copy = dyn_cast<CXXConstructExpr>(S);
        Copy && Copy->getConstructor()->isCopyOrMoveConstructor() &&
        Copy->getNumArgs() >= 1 && !IsLocalRef(Copy->getArg(0)))
      return;
    // Made by a constructor that keeps the address of what it is given,
    // `CResult(Value)` with `CResult(int const &_Value) : m_pValue(&_Value)`,
    // also, for the result itself, one whose body can't be seen, which can.
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(S))
      for (unsigned iArgument = 0; iArgument < Construct->getNumArgs();
           ++iArgument)
        if ((keepsParameterAddress(Construct->getConstructor(), iArgument) ||
             (Depth == 0 &&
              mayKeepUnseen(Construct->getConstructor(), iArgument))) &&
            IsInCopy(Construct->getArg(iArgument), 0)) {
          Found = Construct;
          return;
        }
    // What one whose body can be seen only reads through, `CResult(&Value)`
    // with `CResult(int const *_pValue) : m_Value(*_pValue)`, isn't kept.
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(S);
        Construct && !Construct->getConstructor()->isCopyOrMoveConstructor() &&
        Construct->getConstructor()->getDefinition()) {
      for (unsigned iArgument = 0; iArgument < Construct->getNumArgs();
           ++iArgument)
        if (keepsParameterPointer(Construct->getConstructor(), iArgument))
          Search(Construct->getArg(iArgument), Depth);
      return;
    }
    // What a helper whose body can be seen gives back of what it is given,
    // the address of a capture, `fg_Address(Value)` of `int const
    // *fg_Address(int const &_Value) { return &_Value; }`, or a pointer as
    // it is.
    // Also a lambda called, `[](int const &_Value) { return &_Value;
    // }(Value)`.
    // Also called by the name of its operator, `[&] { ... }.operator()()`.
    const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S);
    const auto *ByName = dyn_cast<CXXMemberCallExpr>(S);
    const bool NamedCall =
        ByName && ByName->getMethodDecl() &&
        ByName->getMethodDecl()->getOverloadedOperator() == OO_Call;
    if (const auto *Helper = dyn_cast<CallExpr>(S);
        Helper && (!ByName || NamedCall) &&
        (!Operator || Operator->getOperator() == OO_Call)) {
      for (const Expr *Argument : returnedArguments(Helper, /*Addressed=*/true))
        if (IsInCopy(Argument, 0)) {
          Found = Helper;
          return;
        }
      for (const Expr *Argument : returnedArguments(Helper))
        Search(Argument, Depth);
      // And what a lambda called where it is made returns of what it
      // captures by reference, `[&] { return &Value; }()`, which is of this
      // closure.
      const Expr *Object = Operator    ? Operator->getArg(0)
                           : NamedCall ? ByName->getImplicitObjectArgument()
                                       : nullptr;
      if (const auto *Lambda =
              Object ? dyn_cast<LambdaExpr>(
                           Object->IgnoreImplicit()->IgnoreParens())
                     : nullptr;
          Lambda && Depth < 16 &&
          llvm::all_of(
              Lambda->captures(),
              [](const LambdaCapture &Capture) {
                return Capture.getCaptureKind() == LCK_ByRef ||
                       Capture.getCaptureKind() == LCK_This;
              }))
        for (const Expr *Returned : returnedValues(Lambda->getCallOperator()))
          Search(Returned, Depth + 1);
    }
    // `&Value`, also `std::addressof(Value)`, `Text.m_Text` decayed to a
    // pointer, or a pointer that a member function of it gives,
    // `Text.f_GetStr()`.
    // Also through `__builtin_addressof(Value)`.
    const Expr *Addressed = nullptr;
    if (const auto *Address = dyn_cast<UnaryOperator>(S);
        Address && Address->getOpcode() == UO_AddrOf)
      Addressed = Address->getSubExpr();
    else if (const auto *AddressOf = dyn_cast<CallExpr>(S);
             isAddressOfCall(AddressOf))
      Addressed = AddressOf->getArg(0);
    // The address of a static member, `&Data.ms_Value`, is of no capture.
    if (Addressed)
      if (const auto *Member =
              dyn_cast<MemberExpr>(Addressed->IgnoreParenImpCasts());
          Member && isa<VarDecl>(Member->getMemberDecl()))
        return;
    if (const auto *Address = cast_or_null<Expr>(Addressed ? S : nullptr)) {
      if (IsInCopy(Addressed, 0)) {
        Found = Address;
        return;
      }
      // An address through a pointer, `&pValue->m_X`, is into what the
      // pointer points to. What a member function of an object returns a
      // reference to, `&Value.f_Get()`, is into the object, like a pointer
      // it returns.
      const Expr *Object = Addressed->IgnoreParenImpCasts();
      bool ThroughGetter = false;
      while (true) {
        if (const auto *Member = dyn_cast<MemberExpr>(Object);
            Member && !Member->isArrow())
          Object = Member->getBase()->IgnoreParenImpCasts();
        else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Object))
          Object = Subscript->getBase()->IgnoreParenImpCasts();
        else if (const auto *Getter = dyn_cast<CXXMemberCallExpr>(Object);
                 Getter && Getter->isGLValue() &&
                 Getter->getImplicitObjectArgument() &&
                 !Getter->getImplicitObjectArgument()
                      ->getType()
                      ->isPointerType() &&
                 mayReturnIntoObject(Getter->getMethodDecl(),
                                     Getter->getImplicitObjectArgument())) {
          Object = Getter->getImplicitObjectArgument()->IgnoreParenImpCasts();
          ThroughGetter = true;
        }
        // Also an overloaded operator, like the subscript of a container,
        // `&Values[0]`.
        else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Object);
                 Operator && Operator->isGLValue() &&
                 Operator->getNumArgs() >= 1 &&
                 isa_and_nonnull<CXXMethodDecl>(Operator->getDirectCallee()) &&
                 !Operator->getArg(0)->getType()->isPointerType() &&
                 mayReturnIntoObject(
                     cast<CXXMethodDecl>(Operator->getDirectCallee()),
                     Operator->getArg(0))) {
          Object = Operator->getArg(0)->IgnoreParenImpCasts();
          ThroughGetter = true;
        } else
          break;
      }
      if (ThroughGetter && IsInCopy(Object, 0)) {
        Found = Address;
        return;
      }
      if (const auto *Member = dyn_cast<MemberExpr>(Object);
          Member && Member->isArrow()) {
        Search(Member->getBase(), Depth);
        return;
      }
      if (const auto *Deref = dyn_cast<UnaryOperator>(Object);
          Deref && Deref->getOpcode() == UO_Deref) {
        Search(Deref->getSubExpr(), Depth);
        return;
      }
    }
    // What a pointer points to, `*pContents` or `pEntry->m_Value`, is copied
    // into the result, unless it is a pointer itself.
    if (const auto *Deref = dyn_cast<UnaryOperator>(S);
        Deref && Deref->getOpcode() == UO_Deref &&
        !Deref->getType()->isPointerType())
      return;
    if (const auto *Member = dyn_cast<MemberExpr>(S);
        Member && Member->isArrow() && !Member->getType()->isPointerType() &&
        !Member->getType()->isArrayType())
      return;
    // So is an element, `Values[0]`.
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(S);
        Subscript && !Subscript->getType()->isPointerType() &&
        !Subscript->getType()->isArrayType())
      return;
    if (const auto *Decay = dyn_cast<ImplicitCastExpr>(S);
        Decay && Decay->getCastKind() == CK_ArrayToPointerDecay &&
        IsInCopy(Decay->getSubExpr(), 0)) {
      Found = Decay;
      return;
    }
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(S);
        Call && Call->getType()->isPointerType() &&
        Call->getImplicitObjectArgument() &&
        !Call->getImplicitObjectArgument()->getType()->isPointerType() &&
        IsInCopy(Call->getImplicitObjectArgument(), 0) &&
        mayReturnIntoObject(Call->getMethodDecl(),
                            Call->getImplicitObjectArgument())) {
      Found = Call;
      return;
    }
    // Bound to a reference member of an aggregate, `CRef{Value}`, also made
    // with parentheses, `CRef(Value)`.
    llvm::ArrayRef<Expr *> Inits;
    const RecordDecl *Aggregate = nullptr;
    if (const auto *List = dyn_cast<InitListExpr>(S)) {
      Inits = llvm::ArrayRef(List->getInits(), List->getNumInits());
      Aggregate = List->getType()->getAsRecordDecl();
    } else if (const auto *Parens = dyn_cast<CXXParenListInitExpr>(S)) {
      Inits = Parens->getInitExprs();
      Aggregate = Parens->getType()->getAsRecordDecl();
    }
    if (Aggregate) {
      unsigned iInit = 0;
      if (const auto *Class = dyn_cast<CXXRecordDecl>(Aggregate))
        iInit = Class->getNumBases();
      for (const FieldDecl *Field : Aggregate->fields()) {
        // An unnamed bit-field isn't initialized, `int : 0`.
        if (Field->isUnnamedBitField())
          continue;
        if (iInit >= Inits.size())
          break;
        // Also to a part of one, `CRef{Value.m_Value}`.
        if (Field->getType()->isReferenceType() && IsInCopy(Inits[iInit], 0)) {
          Found = Inits[iInit];
          return;
        }
        // Also to what a pointer points to, `CRef{*pValue}`, where it points.
        if (Field->getType()->isReferenceType()) {
          const Expr *Bound = Inits[iInit]->IgnoreParenImpCasts();
          while (const auto *Part = dyn_cast<MemberExpr>(Bound)) {
            if (Part->isArrow()) {
              Search(Part->getBase(), Depth + 1);
              break;
            }
            Bound = Part->getBase()->IgnoreParenImpCasts();
          }
          if (const auto *Deref = dyn_cast<UnaryOperator>(Bound);
              Deref && Deref->getOpcode() == UO_Deref)
            Search(Deref->getSubExpr(), Depth + 1);
          if (Found)
            return;
        }
        ++iInit;
      }
    }
    // What a local of the lambda is made from, and assigned: the last
    // assignment of a statement of the body before the result, or else any.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Followed.insert(Ref).second)
      if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
          Local && Local->hasLocalStorage() && !Local->isInitCapture() &&
          LambdaBodyStmt->getSourceRange().fullyContains(
              Local->getSourceRange())) {
        const SourceManager &SM = Local->getASTContext().getSourceManager();
        // Also one without an initializer, `int const *p; p = &Value;`.
        llvm::SmallVector<const Expr *, 4> Values;
        if (Local->getInit())
          Values.push_back(Local->getInit());
        const Stmt *LambdaBody = LambdaBodyStmt;
        if (const auto *Coroutine = dyn_cast<CoroutineBodyStmt>(LambdaBody))
          LambdaBody = Coroutine->getBody();
        const auto *Body = dyn_cast<CompoundStmt>(LambdaBody);
        std::function<bool(const Stmt *)> HasJumps = [&](const Stmt *Inner) {
          if (!Inner || isa<LambdaExpr>(Inner))
            return false;
          if (isa<GotoStmt, IndirectGotoStmt, LabelStmt>(Inner))
            return true;
          return llvm::any_of(childrenOnce(Inner), HasJumps);
        };
        const bool BodyJumps = HasJumps(LambdaBody);
        // What the parts of it are assigned, each with the part.
        llvm::SmallVector<std::pair<const Expr *, const Expr *>, 2> PartValues;
        // An assignment before the use, not the one the use is in, whose
        // value it is, `pValue = bKeep ? pValue : nullptr`, which is of what
        // it held before.
        auto Before = [&](const Stmt *Assignment) {
          return SM.isBeforeInTranslationUnit(Assignment->getBeginLoc(),
                                              Ref->getBeginLoc()) &&
                 !Assignment->getSourceRange().fullyContains(
                     Ref->getSourceRange());
        };
        // Not in what never runs, a branch an `if constexpr` discards, or a
        // constant condition never takes.
        std::function<void(const Stmt *)> Assignments = [&](const Stmt *Inner) {
          if (!Inner || isa<LambdaExpr>(Inner))
            return;
          if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
              Assign && Assign->getOpcode() == BO_Assign && Before(Assign))
            if (const auto *Target = dyn_cast<DeclRefExpr>(
                    Assign->getLHS()->IgnoreParenImpCasts());
                Target && Target->getDecl() == Local) {
              // Of a statement of the body, it replaces what came before,
              // unless a jump can pass it.
              if (Body && !BodyJumps && llvm::is_contained(Body->body(), Assign))
                Values.clear();
              Values.push_back(Assign->getRHS());
            }
          // Or all of it, `Result = CResult{&Value}`.
          if (const auto *Whole = dyn_cast<CXXOperatorCallExpr>(Inner);
              Whole && Whole->getOperator() == OO_Equal &&
              Whole->getNumArgs() == 2 && Before(Whole))
            if (const auto *Target = dyn_cast<DeclRefExpr>(
                    Whole->getArg(0)->IgnoreParenImpCasts());
                Target && Target->getDecl() == Local) {
              // Of a statement of the body, it replaces what came before,
              // unless a jump can pass it.
              if (Body && !BodyJumps &&
                  llvm::any_of(Body->body(), [&](const Stmt *Statement) {
                    const auto *Expression = dyn_cast<Expr>(Statement);
                    return Expression && Expression->IgnoreImplicit() ==
                                             static_cast<const Expr *>(Whole);
                  })) {
                Values.clear();
                PartValues.clear();
              }
              Values.push_back(Whole->getArg(1));
            }
          // Or a part of it, `Result.m_pValue = &Value`.
          if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
              Assign && Assign->getOpcode() == BO_Assign && Before(Assign)) {
            const Expr *Part = Assign->getLHS()->IgnoreParenImpCasts();
            bool IsPart = false;
            // Also through a reference to a part, `auto &pValue =
            // Result.m_pValue; pValue = &Value`.
            for (int Depth = 0; Depth < 4; ++Depth) {
              const auto *Alias = dyn_cast<DeclRefExpr>(Part);
              const auto *Reference =
                  Alias ? dyn_cast<VarDecl>(Alias->getDecl()) : nullptr;
              if (!Reference || !Reference->getType()->isReferenceType() ||
                  !Reference->getInit() || Reference == Local)
                break;
              Part = Reference->getInit()->IgnoreParenImpCasts();
            }
            while (true) {
              if (const auto *Member = dyn_cast<MemberExpr>(Part);
                  Member && !Member->isArrow())
                Part = Member->getBase()->IgnoreParenImpCasts();
              else if (const auto *Subscript =
                           dyn_cast<ArraySubscriptExpr>(Part);
                       Subscript && Subscript->getBase()
                                        ->IgnoreParenImpCasts()
                                        ->getType()
                                        ->isArrayType())
                Part = Subscript->getBase()->IgnoreParenImpCasts();
              else
                break;
              IsPart = true;
            }
            if (const auto *Target = dyn_cast<DeclRefExpr>(Part);
                IsPart && Target && Target->getDecl() == Local) {
              // One of a statement of the body replaces what was assigned
              // to the same part before, `Result.m_pValue = nullptr;`,
              // unless a jump can pass it.
              if (Body && !BodyJumps &&
                  llvm::any_of(Body->body(), [&](const Stmt *Statement) {
                    const auto *Expression = dyn_cast<Expr>(Statement);
                    return Expression && Expression->IgnoreImplicit() ==
                                             static_cast<const Expr *>(Assign);
                  }))
                llvm::erase_if(PartValues, [&](const auto &Earlier) {
                  return sameLocalPart(Earlier.first, Assign->getLHS());
                });
              PartValues.push_back({Assign->getLHS(), Assign->getRHS()});
            }
          }
          for (const Stmt *Child : evaluatedChildren(Inner, Context))
            Assignments(Child);
        };
        Assignments(LambdaBodyStmt);
        for (const auto &[Part, Value] : PartValues)
          Values.push_back(Value);
        for (const Expr *Value : Values)
          Search(Value, Depth + 1);
      }
    for (const Stmt *Child : childrenOnce(S))
      Search(Child, Depth);
  };
  std::function<void(const Stmt *)> FindReturns = [&](const Stmt *S) {
    if (!S || Found || isa<LambdaExpr>(S))
      return;
    // Not in a branch of an `if constexpr` that is discarded.
    if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConstexpr())
      if (const std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Context)) {
        FindReturns(If->getInit());
        FindReturns(*Case);
        return;
      }
    if (const auto *Return = dyn_cast<CoreturnStmt>(S)) {
      // An array, which the result is a pointer to, `co_return Text.m_Text`.
      if (const Expr *Operand = Return->getOperand();
          Operand && Operand->getType()->isArrayType() &&
          IsInCopy(Operand, 0)) {
        Found = Operand;
        return;
      }
      // Converted to the result by a constructor that keeps its address,
      // `co_return Value;` into a `TCFuture<CResult>`.
      if (const Expr *Operand = Return->getOperand();
          Operand && IsInCopy(Operand, 0))
        if (const QualType Value =
                futureValueType(Operator->getReturnType());
            !Value.isNull())
        if (const CXXRecordDecl *Result = Value->getAsCXXRecordDecl();
            Result && Result->hasDefinition() &&
            !Context.hasSameUnqualifiedType(
                Value,
                Operand->getType().getNonReferenceType())) {
          // The constructor that the conversion calls, given to the promise,
          // when it is told, not another one, `CResult(int &)` over
          // `CResult(int const &)`.
          const CXXConstructExpr *Converting = nullptr;
          if (const auto *Promise =
                  dyn_cast_or_null<CallExpr>(Return->getPromiseCall());
              Promise && Promise->getNumArgs() == 1) {
            const Expr *Argument = Promise->getArg(0);
            for (int Depth = 0; Argument && Depth < 8 && !Converting; ++Depth) {
              Argument = Argument->IgnoreParenImpCasts();
              if (const auto *Construct = dyn_cast<CXXConstructExpr>(Argument);
                  Construct &&
                  !Construct->getConstructor()->isCopyOrMoveConstructor())
                Converting = Construct;
              else if (const auto *Materialize =
                           dyn_cast<MaterializeTemporaryExpr>(Argument))
                Argument = Materialize->getSubExpr();
              else if (const auto *Bind =
                           dyn_cast<CXXBindTemporaryExpr>(Argument))
                Argument = Bind->getSubExpr();
              else if (const auto *Copy = dyn_cast<CXXConstructExpr>(Argument);
                       Copy && Copy->getNumArgs() == 1)
                Argument = Copy->getArg(0);
              else
                break;
            }
          }
          if (Converting) {
            if (Converting->getNumArgs() >= 1 &&
                (keepsParameterAddress(Converting->getConstructor(), 0) ||
                 mayKeepUnseen(Converting->getConstructor(), 0))) {
              Found = Operand;
              return;
            }
          } else
          for (const CXXConstructorDecl *Constructor : Result->ctors())
            if (!Constructor->isCopyOrMoveConstructor() &&
                Constructor->getNumParams() >= 1 &&
                Context.hasSameUnqualifiedType(
                    Constructor->getParamDecl(0)
                        ->getType()
                        .getNonReferenceType(),
                    Operand->getType().getNonReferenceType()) &&
                (keepsParameterAddress(Constructor, 0) ||
                 mayKeepUnseen(Constructor, 0))) {
              Found = Operand;
              return;
            }
        }
      Search(Return->getOperand(), 0);
      // Also what the promise is given, which converts it, like through a
      // conversion of it to a pointer into it, `operator int const *()`.
      if (const auto *Promise =
              dyn_cast_or_null<CallExpr>(Return->getPromiseCall());
          Promise && Promise->getNumArgs() == 1)
        Search(Promise->getArg(0), 0);
      return;
    }
    // Not what comes after a return in a block, which no label in it lets a
    // jump reach.
    if (const auto *Block = dyn_cast<CompoundStmt>(S)) {
      bool Left = false;
      for (const Stmt *Statement : Block->body()) {
        if (Left && !hasJumpTarget(Statement))
          continue;
        Left = false;
        FindReturns(Statement);
        if (isa<CoreturnStmt, ReturnStmt>(Statement))
          Left = true;
      }
      return;
    }
    // Nor in a branch that a constant condition never takes.
    for (const Stmt *Child : evaluatedChildren(S, Context))
      FindReturns(Child);
  };
  FindReturns(LambdaBodyStmt);
  return Found;
}

void CoroutineLambdaCallCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  const SourceManager &SM = *Result.SourceManager;
  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  for (const auto &[Id, Node] : Result.Nodes.getMap())
    if (isInTemplatePattern(Node, *Result.Context))
      return;
  if (const auto *Call = Result.Nodes.getNodeAs<CXXOperatorCallExpr>("call")) {
    const auto *Operator =
        dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
    // Not one that never runs, in a branch an `if constexpr` discards or a
    // constant condition never takes.
    if (!Operator || !isLambdaCallOperator(Operator) ||
        !isCoroutine(Operator) ||
        isInUnevaluatedOperand(DynTypedNode::create(*Call), *Result.Context) ||
        isInDiscardedBranch(DynTypedNode::create(*Call), *Result.Context) ||
        isInNeverTakenBranch(DynTypedNode::create(*Call), *Result.Context))
      return;
    // Only a temporary closure, or one in a local variable, is the call's
    // own. A call of a copy that a lambda around captured is reported where
    // that lambda captures it. So is one given as a parameter,
    // `_fFunction(1)`, or one held elsewhere, in a member,
    // `State.m_Function()`, or through a pointer, `(*pFunction)()`, like a
    // function wrapper holds it, where it is given.
    const Expr *Closure = Call->getArg(0)->IgnoreParenImpCasts();
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Closure)) {
      const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
      if (!Variable || Ref->refersToEnclosingVariableOrCapture() ||
          isa<ParmVarDecl>(Variable) || !Variable->hasLocalStorage())
        return;
    } else if (!Closure->isPRValue() &&
               !isa<MaterializeTemporaryExpr>(Closure))
      return;
    const Stmt *Use = danglingCaptureUse(Operator, *Result.Context);
    // One that takes its closure by value, `this auto Self`, keeps the
    // captures in its frame, but not for what its result refers to.
    if (!Use && Operator->isExplicitObjectMemberFunction() &&
        !Operator->getParamDecl(0)->getType()->isReferenceType() &&
        isProtectedCoroutine(Operator))
      // The call operator called, of a generic one the specialization, which
      // tells what its helpers return.
      if (capturedAddressInResult(Operator)) {
        const SourceLocation Loc =
            reportLocationIn(Call->getBeginLoc(), Call->getEndLoc(), SM);
        if (Loc.isValid())
          diag(Loc, "a lambda coroutine returns what refers to its captures, "
                    "which are gone once its frame is; return a copy");
        return;
      }
    if (!Use)
      return;
    const SourceLocation Loc = reportLocationIn(Call->getBeginLoc(), Call->getEndLoc(), SM);
    if (Loc.isInvalid())
      return;
    diag(Loc, "a lambda coroutine that uses its captures after a suspension "
              "is called directly; the captures dangle when the closure is a "
              "temporary, so run it through fg_CallSafe, g_Dispatch, 'self /' "
              "or an actor functor");
    diag(Use->getBeginLoc(), "a capture is used here after a suspension, or in a way that keeps a reference to it",
         DiagnosticIDs::Note);
    return;
  }

  // Other uses of the closure, which can call it directly or keep it too
  // short.
  const auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda");
  // Its call operator, or of a generic lambda, each specialization of it.
  llvm::SmallVector<const CXXMethodDecl *, 2> Operators;
  if (const FunctionTemplateDecl *Template =
          Lambda->getCallOperator()->getDescribedFunctionTemplate()) {
    for (const FunctionDecl *Specialization : Template->specializations())
      Operators.push_back(cast<CXXMethodDecl>(Specialization));
  } else
    Operators.push_back(Lambda->getCallOperator());
  const Stmt *Use = nullptr;
  for (const CXXMethodDecl *Operator : Operators)
    if ((Use = danglingCaptureUse(Operator, *Result.Context)))
      break;
  // What the result keeps of the captures, like `co_return &Value`, also
  // through a local, `auto *pValue = &Value; co_return pValue;`, or a view
  // made of it, outlives the closure also when what runs it keeps it alive
  // while it runs.
  const Stmt *ResultUse = nullptr;
  for (const CXXMethodDecl *Operator : Operators)
    if (isProtectedCoroutine(Operator) &&
        (ResultUse = capturedAddressInResult(Operator)))
      break;
  const bool InResult = ResultUse;
  if ((!Use && !ResultUse) ||
      isInUnevaluatedOperand(DynTypedNode::create(*Lambda), *Result.Context) ||
      isInDiscardedBranch(DynTypedNode::create(*Lambda), *Result.Context) ||
      isInNeverTakenBranch(DynTypedNode::create(*Lambda), *Result.Context))
    return;
  llvm::SmallVector<ClosureConsumer, 2> Consumers =
      closureConsumers(Lambda, *Result.Context, SafeCallees);
  giveUnlistedCallSafe(Consumers, SafeCallees);
  for (const ClosureConsumer &Consumer : Consumers) {
    // Not where it doesn't run, `if constexpr (false) fFunction();`.
    if (const DynTypedNode Site = DynTypedNode::create(*Consumer.Site);
        isInUnevaluatedOperand(Site, *Result.Context) ||
        isInDiscardedBranch(Site, *Result.Context) ||
        isInNeverTakenBranch(Site, *Result.Context))
      continue;
    // A direct call through a dependent type, in a template, an explicit
    // call of the call operator, or a call through a function wrapper, like
    // `fFunction()` of a `TCFunction` made from it, which the matcher of calls
    // of the lambda's call operator doesn't see.
    const auto *OperatorCall = dyn_cast<CXXOperatorCallExpr>(Consumer.Site);
    const auto *CallOperator =
        OperatorCall
            ? dyn_cast_or_null<CXXMethodDecl>(OperatorCall->getDirectCallee())
            : nullptr;
    if (Consumer.Use == ClosureUse::DirectCall && Use &&
        !(CallOperator && isLambdaCallOperator(CallOperator))) {
      const SourceLocation Loc =
          reportLocationIn(Consumer.Site->getBeginLoc(),
                         Consumer.Site->getEndLoc(), SM);
      if (Loc.isInvalid())
        continue;
      diag(Loc, "a lambda coroutine that uses its captures after a suspension "
                "is called directly; the captures dangle when the closure is a "
                "temporary, so run it through fg_CallSafe, g_Dispatch, 'self /' "
                "or an actor functor");
      diag(Use->getBeginLoc(), "a capture is used here after a suspension, or in a way that keeps a reference to it",
           DiagnosticIDs::Note);
      continue;
    }
    // One that takes its closure by value, called directly other than by the
    // call operator matcher, like `Task.operator()()`, keeps the captures in
    // its frame, but not for what its result refers to.
    const CXXMethodDecl *LambdaOperator = Lambda->getCallOperator();
    if (Consumer.Use == ClosureUse::DirectCall && !Use && InResult &&
        !(CallOperator && isLambdaCallOperator(CallOperator)) &&
        LambdaOperator->isExplicitObjectMemberFunction() &&
        !LambdaOperator->getParamDecl(0)->getType()->isReferenceType()) {
      const SourceLocation Loc = reportLocationIn(
          Consumer.Site->getBeginLoc(), Consumer.Site->getEndLoc(), SM);
      if (Loc.isValid())
        diag(Loc, "a lambda coroutine returns what refers to its captures, "
                  "which are gone once its frame is; return a copy");
      continue;
    }
    if (InResult && (Consumer.Use == ClosureUse::SafeCallee ||
                     Consumer.Use == ClosureUse::CallSafe)) {
      Use = ResultUse;
      const SourceLocation Loc = reportLocationIn(
          Consumer.Site->getBeginLoc(), Consumer.Site->getEndLoc(), SM);
      if (Loc.isInvalid())
        continue;
      diag(Loc, "a lambda coroutine returns what refers to its captures, "
                "which are gone once what it is given to has run it; return "
                "a copy");
      diag(Use->getBeginLoc(), "a capture is used here after a suspension, or in a way that keeps a reference to it",
           DiagnosticIDs::Note);
      continue;
    }
    if (Consumer.Use != ClosureUse::Escape || !Use)
      continue;
    const SourceLocation Loc =
        reportLocationIn(Consumer.Site->getBeginLoc(),
                         Consumer.Site->getEndLoc(), SM);
    if (Loc.isInvalid())
      continue;
    diag(Loc, "the closure of a lambda coroutine that uses its captures after "
              "a suspension is given to %0, which can call it directly; give "
              "it only to fg_CallSafe, g_Dispatch, 'self /', an actor functor "
              "or another function that keeps it alive while it runs")
        << Consumer.Target;
    diag(Use->getBeginLoc(), "a capture is used here after a suspension, or in a way that keeps a reference to it",
         DiagnosticIDs::Note);
  }
}

} // namespace clang::tidy::malterlib
