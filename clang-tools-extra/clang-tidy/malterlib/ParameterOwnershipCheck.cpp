//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ParameterOwnershipCheck.h"
#include "../utils/OptionsUtils.h"
#include "NamingUtils.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/SaveAndRestore.h"
#include <limits>

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

ParameterOwnershipCheck::ParameterOwnershipCheck(StringRef Name,
                                                 ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      AllowedTypes(
          utils::options::parseStringList(listOption("AllowedTypes", ""))),
      // The functions that move, and those that forward, of the vocabulary.
      MoveFunctions(utils::options::parseStringList(keepList(editList(
          listOption("MoveFunctions", CVocabulary::builtIn("MoveFunctions")),
          vocabulary().ForwardFunctions.joined())))) {}

void ParameterOwnershipCheck::storeCheckOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "AllowedTypes",
                utils::options::serializeStringList(AllowedTypes));
  Options.store(Opts, "MoveFunctions",
                utils::options::serializeStringList(MoveFunctions));
}

void ParameterOwnershipCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(functionDecl(isDefinition()).bind("function"), this);
  // An interface can't be a coroutine, but its overrides take its signature.
  Finder->addMatcher(cxxMethodDecl(isPure(), unless(isDefinition()))
                         .bind("function"),
                     this);
  Finder->addMatcher(lambdaExpr().bind("lambda"), this);
}

void ParameterOwnershipCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  // Lambdas are checked through their expressions, which are visited when
  // their call operators are not.
  if (const auto *Function = Result.Nodes.getNodeAs<FunctionDecl>("function");
      Function && !isLambdaCallOperator(Function))
    checkFunction(Function, *Result.Context);
  if (const auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda")) {
    // A generic lambda, through what its call operator is instantiated as,
    // `[](auto _Value) {}` called with an owner.
    if (const FunctionTemplateDecl *Template =
            Lambda->getDependentCallOperator()) {
      for (const FunctionDecl *Instance : Template->specializations()) {
        const InstantiationScope Scope(*this, Instance);
        checkFunction(Instance, *Result.Context);
      }
    } else
      checkFunction(Lambda->getCallOperator(), *Result.Context);
  }
}

/// What \p Name names in \p Record, also in the bases it inherits it from,
/// like an option constant of `struct COptions : CDefaults {}`.
static llvm::SmallVector<const NamedDecl *, 2>
lookupInherited(const CXXRecordDecl *Record, DeclarationName Name) {
  llvm::SmallVector<const NamedDecl *, 2> Found;
  // In the order the bases are written, each looked into once, also one
  // that several bases share.
  llvm::SmallVector<const CXXRecordDecl *, 8> Worklist{Record};
  llvm::SmallPtrSet<const CXXRecordDecl *, 16> Visited;
  while (!Worklist.empty()) {
    const CXXRecordDecl *Current = Worklist.pop_back_val();
    if (!Current || !Current->hasDefinition())
      continue;
    Current = Current->getDefinition();
    if (!Visited.insert(Current).second)
      continue;
    for (const NamedDecl *Member : Current->lookup(Name))
      Found.push_back(Member);
    if (!Found.empty())
      return Found;
    for (const CXXBaseSpecifier &Base : llvm::reverse(Current->bases()))
      Worklist.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  return Found;
}


static bool releasesOnDestruction(const CXXRecordDecl *Record);

static bool ofObject(const Expr *E);

/// Whether \p E is `this`, also with its const cast away, `const_cast<CView
/// *>(this)`, like a const member function that keeps a cache does.
static bool isThis(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const auto *Cast = dyn_cast<CXXConstCastExpr>(E))
    E = Cast->getSubExpr()->IgnoreParenImpCasts();
  return isa<CXXThisExpr>(E);
}

/// Whether \p Value is \p Parameter, also moved along it, `_pData + 1`, or its
/// address, of a reference, `&_pData`, which refers to it.
static bool isParameterAlong(const Expr *Value, const ParmVarDecl *Parameter) {
  for (int Level = 0; Value && Level < 8; ++Level) {
    Value = Value->IgnoreParenImpCasts();
    if (const auto *Address = dyn_cast<UnaryOperator>(Value);
        Address && Address->getOpcode() == UO_AddrOf &&
        Parameter->getType()->isReferenceType()) {
      Value = Address->getSubExpr();
      continue;
    }
    const auto *Offset = dyn_cast<BinaryOperator>(Value);
    if (!Offset || !Offset->isAdditiveOp() ||
        !Offset->getType()->isPointerType())
      break;
    Value = Offset->getLHS()->getType()->isPointerType() ? Offset->getLHS()
                                                         : Offset->getRHS();
  }
  const auto *Ref =
      Value ? dyn_cast<DeclRefExpr>(Value->IgnoreParenImpCasts()) : nullptr;
  return Ref && Ref->getDecl() == Parameter;
}

/// Whether the body of \p Definition can keep \p Parameter: assigns it, or
/// gives it on, unless to a function whose body can be seen not to keep it,
/// like a helper of the constructor that only reads it to make its own,
/// `f_SetStr(_pText)` of `m_pData = new char[...]{*_pText}`.
/// Each parameter once at each depth: helpers that each give it to the one
/// before several times would be looked at again through each path to them.
using CKeptMemo = llvm::DenseMap<std::pair<const ParmVarDecl *, int>, bool>;
static bool keepsParameterInBody(const FunctionDecl *Definition,
                                 const ParmVarDecl *Parameter, int Depth,
                                 CKeptMemo &Memo);
static bool keepsParameterInBody(const FunctionDecl *Definition,
                                 const ParmVarDecl *Parameter, int Depth) {
  CKeptMemo Memo;
  return keepsParameterInBody(Definition, Parameter, Depth, Memo);
}
static bool keepsParameterInBody(const FunctionDecl *Definition,
                                 const ParmVarDecl *Parameter, int Depth,
                                 CKeptMemo &Memo) {
  if (!Definition->getBody())
    return true;
  if (auto It = Memo.find({Parameter, Depth}); It != Memo.end())
    return It->second;
  bool Kept = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *Inner) {
    if (!Inner || Kept)
      return;
    if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
        Assign && Assign->getOpcode() == BO_Assign &&
        isParameterAlong(Assign->getRHS(), Parameter))
      Kept = true;
    if (const auto *Call = dyn_cast<CallExpr>(Inner)) {
      const FunctionDecl *Callee = Call->getDirectCallee();
      const FunctionDecl *Seen = Callee ? Callee->getDefinition() : nullptr;
      const bool Follows = Seen && Seen->getBody() && Depth < 8 &&
                           !isa<CXXOperatorCallExpr>(Call) &&
                           !callsOverride(Call);
      for (unsigned iArgument = 0; iArgument < Call->getNumArgs(); ++iArgument)
        if (isParameterAlong(Call->getArg(iArgument), Parameter) &&
            (!Follows || iArgument >= Seen->getNumParams() ||
             keepsParameterInBody(Seen, Seen->getParamDecl(iArgument),
                                  Depth + 1, Memo)))
          Kept = true;
    }
    for (const Stmt *Child : childrenOnce(Inner))
      Visit(Child);
  };
  Visit(Definition->getBody());
  Memo[{Parameter, Depth}] = Kept;
  return Kept;
}

/// Whether what \p Init makes an object of is given what \p Of tells is of
/// something, like `m_pData` of `CGuard{m_pData}`, not what is allocated anew
/// from it, `CGuard{new int(*m_pData)}`.
static bool gives(const Expr *Init, llvm::function_ref<bool(const Expr *)> Of,
                  int Depth = 0) {
  if (!Init || Depth > 8)
    return false;
  const Expr *E = Init->IgnoreParenImpCasts();
  if (const auto *Full = dyn_cast<FullExpr>(E))
    E = Full->getSubExpr()->IgnoreParenImpCasts();
  if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(E))
    E = Materialize->getSubExpr()->IgnoreParenImpCasts();
  if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(E))
    E = Bind->getSubExpr()->IgnoreParenImpCasts();
  if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(E))
    E = Cast->getSubExpr()->IgnoreParenImpCasts();
  auto AnyGives = [&](llvm::ArrayRef<const Expr *> Parts) {
    return llvm::any_of(Parts, [&](const Expr *Part) {
      return gives(Part, Of, Depth + 1);
    });
  };
  if (const auto *List = dyn_cast<InitListExpr>(E))
    return AnyGives(llvm::ArrayRef(List->getInits(), List->getNumInits()));
  if (const auto *List = dyn_cast<CXXParenListInitExpr>(E))
    return AnyGives(List->getInitExprs());
  // A lambda that captures the object, `[this] { delete m_pData; }`, or what
  // is of it.
  if (const auto *Lambda = dyn_cast<LambdaExpr>(E)) {
    if (llvm::any_of(Lambda->captures(), [](const LambdaCapture &Capture) {
          return Capture.capturesThis();
        }))
      return true;
    return llvm::any_of(Lambda->capture_inits(), [&](const Expr *Capture) {
      return gives(Capture, Of, Depth + 1);
    });
  }
  // Also a local that holds such a lambda, `TCGuard Guard{fCleanup}` with
  // `auto fCleanup = [this] { delete m_pData; };`, which can't be assigned
  // another one with captures.
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E))
    if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
        Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
        Local->getInit())
      if (const CXXRecordDecl *Closure =
              Local->getType().getNonReferenceType()->getAsCXXRecordDecl();
          Closure && Closure->isLambda() &&
          gives(Local->getInit(), Of, Depth + 1))
        return true;
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(E)) {
    // A copy that the class makes itself, like a deep copy, `COwner
    // Scratch(*m_pOwner)`, is its own, unlike one copied as it is or moved.
    const CXXConstructorDecl *Constructor = Construct->getConstructor();
    if (Constructor->isCopyConstructor() && Constructor->isUserProvided())
      return false;
    // One whose body can be seen gives it only what it keeps as it is given,
    // `m_pData(_pData)`, not what it only reads to make its own, like a deep
    // copy, `m_pData(new int(*_pData))`.
    if (const auto *Definition = dyn_cast_or_null<CXXConstructorDecl>(
            Constructor->getDefinition());
        Definition && Definition->hasBody() && !Constructor->isCopyOrMoveConstructor()) {
      auto Keeps = [&](const ParmVarDecl *Parameter) {
        // Also in what a member is made of, `m_Release{_pData}`.
        auto IsParameter = [&](const Expr *Value) {
          return isParameterAlong(Value, Parameter);
        };
        for (const CXXCtorInitializer *Initializer : Definition->inits())
          if (Initializer->isWritten() &&
              (IsParameter(Initializer->getInit()) ||
               gives(Initializer->getInit(), IsParameter, Depth + 1)))
            return true;
        return keepsParameterInBody(Definition, Parameter, Depth);
      };
      for (unsigned iArgument = 0; iArgument < Construct->getNumArgs() &&
                                   iArgument < Definition->getNumParams();
           ++iArgument)
        if (Keeps(Definition->getParamDecl(iArgument)) &&
            gives(Construct->getArg(iArgument), Of, Depth + 1))
          return true;
      return false;
    }
    return AnyGives(
        llvm::ArrayRef(Construct->getArgs(), Construct->getNumArgs()));
  }
  return Of(E);
}

/// Whether what \p Init makes an object of is given what is of the object.
static bool givesObject(const Expr *Init) { return gives(Init, ofObject); }

/// Whether \p Call can give the local \p Local what is of the object: swaps
/// them, `fg_Swap(pData, m_pData)`, or takes the local by a reference that
/// isn't const with what is of the object, also by a reference, and assigns
/// the one from the other, `void fg_Take(int *&o_pData, int *&_pData) {
/// o_pData = _pData; }`, or can, when its body can't be seen.
static bool givenFromObject(const CallExpr *Call, const VarDecl *Local) {
  auto IsLocal = [&](const Expr *E) {
    const auto *Ref =
        E ? dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts()) : nullptr;
    return Ref && Ref->getDecl() == Local;
  };
  if (const auto [Left, Right] = swappedArguments(Call); Left)
    return (IsLocal(Left) && ofObject(Right)) ||
           (IsLocal(Right) && ofObject(Left));
  const FunctionDecl *Callee = Call->getDirectCallee();
  if (!Callee || isa<CXXOperatorCallExpr>(Call))
    return false;
  auto ByReference = [&](unsigned iArgument) {
    if (iArgument >= Callee->getNumParams())
      return false;
    const QualType Type = Callee->getParamDecl(iArgument)->getType();
    return Type->isReferenceType() &&
           !Type.getNonReferenceType().isConstQualified();
  };
  const FunctionDecl *Definition = Callee->getDefinition();
  for (unsigned iLocal = 0; iLocal < Call->getNumArgs(); ++iLocal) {
    if (!IsLocal(Call->getArg(iLocal)) || !ByReference(iLocal))
      continue;
    for (unsigned iObject = 0; iObject < Call->getNumArgs(); ++iObject) {
      if (iObject == iLocal || iObject >= Callee->getNumParams() ||
          !ofObject(Call->getArg(iObject)))
        continue;
      if (!Definition || !Definition->getBody())
        return true;
      const ParmVarDecl *Target = Definition->getParamDecl(iLocal);
      const ParmVarDecl *Source = Definition->getParamDecl(iObject);
      // Assigned it, also moved, `o_pData = fg_Move(_pData)`, or swapped
      // with it.
      auto Names = [](const Expr *E, const ParmVarDecl *Parameter) {
        E = E->IgnoreParenImpCasts();
        if (const auto *Move = dyn_cast<CallExpr>(E);
            Move && Move->getNumArgs() == 1 && Move->getDirectCallee() &&
            Move->getDirectCallee()->getIdentifier() &&
            (isMoveFunction(Move->getDirectCallee()->getName())))
          E = Move->getArg(0)->IgnoreParenImpCasts();
        const auto *Ref = dyn_cast<DeclRefExpr>(E);
        return Ref && Ref->getDecl() == Parameter;
      };
      bool Given = false;
      std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
        if (!S || Given)
          return;
        if (const auto *Assign = dyn_cast<BinaryOperator>(S);
            Assign && Assign->getOpcode() == BO_Assign &&
            Names(Assign->getLHS(), Target) && Names(Assign->getRHS(), Source))
          Given = true;
        if (const auto *Inner = dyn_cast<CallExpr>(S))
          if (const auto [Left, Right] = swappedArguments(Inner); Left)
            Given = Given || (Names(Left, Target) && Names(Right, Source)) ||
                    (Names(Left, Source) && Names(Right, Target));
        for (const Stmt *Child : childrenOnce(S))
          Visit(Child);
      };
      Visit(Definition->getBody());
      if (Given)
        return true;
    }
  }
  return false;
}

/// Whether a call before \p Ref, a use of the local \p Local, can give it
/// what is of the object, like `fg_Take(pData, m_pData)`.
static bool givenFromObjectBefore(const VarDecl *Local,
                                  const DeclRefExpr *Ref) {
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return false;
  const ASTContext &Context = Function->getASTContext();
  // Where the first such call ends, found once for each local, also one used
  // many times; invalid when there is none.
  struct CGiven {
    llvm::DenseMap<const VarDecl *, SourceLocation> Ends;
  };
  llvm::DenseMap<const VarDecl *, SourceLocation> &Ends =
      astCache<CGiven>(Context).Ends;
  const SourceManager &SM = Context.getSourceManager();
  auto It = Ends.find(Local);
  if (It == Ends.end()) {
    SourceLocation First;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || isa<LambdaExpr>(S))
        return;
      if (const auto *Call = dyn_cast<CallExpr>(S);
          Call &&
          (First.isInvalid() ||
           SM.isBeforeInTranslationUnit(Call->getEndLoc(), First)) &&
          givenFromObject(Call, Local))
        First = Call->getEndLoc();
      // Not in a branch of an `if constexpr` that is discarded.
      for (const Stmt *Child : keptChildren(S, Context))
        Visit(Child);
    };
    Visit(Function->getBody());
    It = Ends.try_emplace(Local, First).first;
  }
  return It->second.isValid() &&
         SM.isBeforeInTranslationUnit(It->second, Ref->getBeginLoc());
}

/// Whether the local \p Local is assigned what is of the object in its
/// function, like `pData = m_pData;`.
static bool assignedFromObject(const VarDecl *Local) {
  static thread_local int s_Depth = 0;
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
  if (!Function || !Function->getBody() || s_Depth > 4)
    return false;
  ++s_Depth;
  const llvm::scope_exit Done([] { --s_Depth; });
  bool Assigned = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Assigned)
      return;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->getOpcode() == BO_Assign)
      if (const auto *Target =
              dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts());
          Target && Target->getDecl() == Local &&
          ofObject(Assign->getRHS()))
        Assigned = true;
    // Or given it by a function that it is given to with what is of the
    // object, `fg_Swap(pData, m_pData)` or `fg_Take(pData, m_pData)`.
    if (const auto *Call = dyn_cast<CallExpr>(S))
      Assigned = Assigned || givenFromObject(Call, Local);
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Function->getBody());
  return Assigned;
}

/// Whether \p Body reads the variadic arguments of its function, with
/// `va_start` or `__builtin_va_start`.
static bool readsVariadicArguments(const Stmt *Body) {
  if (!Body)
    return false;
  if (isa<VAArgExpr>(Body))
    return true;
  if (const auto *Call = dyn_cast<CallExpr>(Body);
      Call && Call->getDirectCallee() &&
      Call->getDirectCallee()->getIdentifier() &&
      llvm::is_contained({StringRef("__builtin_va_start"),
                          StringRef("__builtin_c23_va_start"),
                          StringRef("__va_start")},
                         Call->getDirectCallee()->getName()))
    return true;
  return llvm::any_of(childrenOnce(Body), readsVariadicArguments);
}

/// Whether \p Local is assigned, stepped or has its address taken in \p Body,
/// so it can hold another value than it was made with.
static bool isReassigned(const VarDecl *Local, const Stmt *Body) {
  bool Changed = false;
  auto IsLocal = [&](const Expr *E) {
    const auto *Ref = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
    return Ref && Ref->getDecl() == Local;
  };
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Changed)
      return;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->isAssignmentOp() && IsLocal(Assign->getLHS()))
      Changed = true;
    if (const auto *Unary = dyn_cast<UnaryOperator>(S);
        Unary &&
        (Unary->isIncrementDecrementOp() || Unary->getOpcode() == UO_AddrOf) &&
        IsLocal(Unary->getSubExpr()))
      Changed = true;
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Body);
  return Changed;
}

/// Whether \p Call, of a function of one integer whose body can be seen, gives
/// back the pointer that integer keeps, on its one return: the integer itself,
/// shifted or masked, made a pointer, `return (t_CType *)(_Pointer <<
/// t_nBits);`.
static bool decodesPointer(const CallExpr *Call) {
  const FunctionDecl *Callee = Call->getDirectCallee();
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  if (!Definition || Definition->getNumParams() != 1 ||
      Call->getNumArgs() != 1 ||
      !Definition->getParamDecl(0)->getType()->isIntegerType())
    return false;
  const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
  const auto *Return = Body && Body->size() == 1
                           ? dyn_cast<ReturnStmt>(Body->body_front())
                           : nullptr;
  const Expr *Value = Return ? Return->getRetValue() : nullptr;
  for (int Depth = 0; Value && Depth < 8; ++Depth) {
    Value = Value->IgnoreParenCasts();
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Value))
      return Ref->getDecl() == Definition->getParamDecl(0);
    const auto *Binary = dyn_cast<BinaryOperator>(Value);
    if (!Binary || !(Binary->isShiftOp() || Binary->getOpcode() == BO_And ||
                     Binary->getOpcode() == BO_Or))
      return false;
    Value = Binary->getLHS();
  }
  return false;
}

/// Whether \p From moves an owner it has out, `return fg_Move(m_pValue);` of a
/// mutable member, also of what a member points to, `return
/// fg_Move(m_pSlot->m_pValue);`, which gives what the object held, also one
/// made of what it exchanges out of a member, `return TCUniquePointer<int>(
/// fg_Exchange(m_pValue, nullptr));`.
static bool movesOwnMember(const CXXMethodDecl *From) {
  const FunctionDecl *Definition = From->getDefinition();
  if (!Definition || !Definition->getBody())
    return false;
  llvm::SmallVector<const Expr *, 2> Returned;
  std::function<void(const Stmt *)> Collect = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return;
    if (const auto *Return = dyn_cast<ReturnStmt>(S);
        Return && Return->getRetValue())
      Returned.push_back(Return->getRetValue());
    for (const Stmt *Child : childrenOnce(S))
      Collect(Child);
  };
  Collect(Definition->getBody());
  return llvm::any_of(Returned, [](const Expr *Value) {
    const Expr *Moved = Value->IgnoreImplicit()->IgnoreParens();
    const Expr *Made = Moved;
    if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Made))
      Made = Cast->getSubExpr()->IgnoreImplicit();
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(Made);
        Construct && Construct->getNumArgs() >= 1 &&
        !Construct->getConstructor()->isCopyOrMoveConstructor())
      Made = Construct->getArg(0);
    const auto *Exchange = dyn_cast<CallExpr>(Made->IgnoreParenImpCasts());
    if (Exchange && Exchange->getNumArgs() == 2 &&
        Exchange->getDirectCallee() &&
        Exchange->getDirectCallee()->getIdentifier() &&
        isExchangeFunction(Exchange->getDirectCallee()->getName())) {
      Moved = Exchange->getArg(0);
    } else {
      if (const auto *Construct = dyn_cast<CXXConstructExpr>(Moved);
          Construct && Construct->getNumArgs() == 1 &&
          Construct->getConstructor()->isMoveConstructor())
        Moved = Construct->getArg(0)->IgnoreImplicit()->IgnoreParens();
      if (const auto *Move = dyn_cast<CallExpr>(Moved);
          Move && Move->getNumArgs() == 1 && Move->getDirectCallee() &&
          Move->getDirectCallee()->getIdentifier() &&
          (isMoveFunction(Move->getDirectCallee()->getName())))
        Moved = Move->getArg(0);
      else if (const auto *Cast = dyn_cast<CXXStaticCastExpr>(Moved);
               Cast && Cast->getTypeAsWritten()->isRValueReferenceType())
        Moved = Cast->getSubExpr();
      else if (!Moved->isXValue())
        return false;
    }
    for (int Level = 0; Level < 8; ++Level) {
      const auto *Member =
          dyn_cast<MemberExpr>(Moved->IgnoreParenImpCasts());
      if (!Member || !isa<FieldDecl>(Member->getMemberDecl()))
        return false;
      if (isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()))
        return true;
      Moved = Member->getBase();
    }
    return false;
  });
}

/// Whether what \p Getter, a call of a member function that can be looked
/// into, returns is of its object, on any of its returns; none when it can't
/// be looked into.
static std::optional<bool> returnsOfObject(const CallExpr *Getter) {
  static thread_local int s_Depth = 0;
  const FunctionDecl *Method = Getter->getDirectCallee();
  // A const member function can't give away what its object owns, so an owner
  // it gives back by value owns something of its own, like a shared pointer
  // it takes another reference for, `m_pWeak->f_Lock()`, unless it moves out
  // what a mutable member owns.
  if (const auto *Member = dyn_cast_or_null<CXXMethodDecl>(Method);
      Member && Member->isConst() && !Member->isStatic())
    if (const CXXRecordDecl *Returned =
            Member->getReturnType()->getAsCXXRecordDecl();
        Returned && Returned->hasDefinition() &&
        !Returned->hasTrivialDestructor() && !movesOwnMember(Member))
      return false;
  const FunctionDecl *Definition = Method ? Method->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody() || s_Depth > 4)
    return std::nullopt;
  ++s_Depth;
  const llvm::scope_exit Restore([] { --s_Depth; });
  bool Of = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Of || isa<LambdaExpr>(S))
      return;
    // Also what it makes of it, `return {m_pData};` or `return
    // CGuard(m_pData);`.
    auto MadeOfObject = [](const Expr *Value) {
      Value = Value->IgnoreImplicit();
      if (const auto *List = dyn_cast<InitListExpr>(Value))
        return llvm::any_of(List->inits(), ofObject);
      if (const auto *Construct = dyn_cast<CXXConstructExpr>(Value);
          Construct && !Construct->getConstructor()->isCopyOrMoveConstructor())
        return llvm::any_of(Construct->arguments(), ofObject);
      return false;
    };
    if (const auto *Return = dyn_cast<ReturnStmt>(S);
        Return && Return->getRetValue() &&
        (ofObject(Return->getRetValue()) ||
         MadeOfObject(Return->getRetValue())))
      Of = true;
    // Not in a branch that an `if constexpr` discards.
    if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConstexpr())
      if (const std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Definition->getASTContext())) {
        Visit(If->getInit());
        Visit(*Case);
        return;
      }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Definition->getBody());
  return Of;
}

/// Whether the lambda whose body \p Ref is in is called where it is made,
/// `[pScratch] { delete pScratch; }()`.
/// Whether the local \p Holder of a lambda is only called, `fCleanup()`, in
/// its function, also through local copies of it that are, `auto fCopy =
/// fCleanup; fCopy();`, not given or kept anywhere else, so the lambda runs
/// there.
static bool onlyCalled(const VarDecl *Holder, int Depth = 0) {
  if (Depth > 16 || !Holder->hasLocalStorage() || isa<ParmVarDecl>(Holder) ||
      Holder->getType()->isReferenceType())
    return false;
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Holder->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return false;
  ASTContext &Context = Holder->getASTContext();
  bool Called = true;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || !Called)
      return;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Ref->getDecl() == Holder) {
      DynTypedNode Up = DynTypedNode::create(*Ref);
      for (;;) {
        const auto Parents = instantiatedParents(Up, Context);
        const auto *Parent = Parents.empty() ? nullptr : Parents[0].get<Expr>();
        if (Parent && isa<ImplicitCastExpr, ParenExpr>(Parent)) {
          Up = Parents[0];
          continue;
        }
        if (const auto *Copy = dyn_cast_or_null<CXXConstructExpr>(Parent);
            Copy && Copy->getNumArgs() == 1 &&
            Copy->getConstructor()->isCopyOrMoveConstructor()) {
          const auto Above = instantiatedParents(*Copy, Context);
          const auto *Into = Above.empty() ? nullptr : Above[0].get<VarDecl>();
          Called =
              Into && Into != Holder && Into->getInit() &&
              Into->getInit()->IgnoreImplicit() == Copy->IgnoreImplicit() &&
              onlyCalled(Into, Depth + 1);
          break;
        }
        const auto *Invoke = dyn_cast_or_null<CXXOperatorCallExpr>(Parent);
        Called = Invoke && Invoke->getOperator() == OO_Call &&
                 Invoke->getNumArgs() >= 1 &&
                 Invoke->getArg(0)->IgnoreParenImpCasts() == Ref;
        break;
      }
    }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Function->getBody());
  return Called;
}

static bool calledWhereMade(const DeclRefExpr *Ref) {
  const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
  if (!Variable)
    return false;
  ASTContext &Context = Variable->getASTContext();
  DynTypedNode Node = DynTypedNode::create(*Ref);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    Node = Parents[0];
    const auto *Lambda = Node.get<LambdaExpr>();
    if (!Lambda)
      continue;
    const auto Above = instantiatedParents(*Lambda, Context);
    for (DynTypedNode Up = Above.empty() ? DynTypedNode() : Above[0];;) {
      // Also kept in a local that is only called, `auto fCleanup = [pScratch]
      // { delete pScratch; }; fCleanup();`.
      if (const auto *Holder = Up.get<VarDecl>())
        return Holder->getInit() &&
               Holder->getInit()->IgnoreUnlessSpelledInSource() == Lambda &&
               onlyCalled(Holder);
      const auto *Parent = Up.get<Expr>();
      if (!Parent)
        return false;
      if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(Parent))
        return Invoke->getOperator() == OO_Call && Invoke->getNumArgs() >= 1 &&
               Invoke->getArg(0)->IgnoreUnlessSpelledInSource() == Lambda;
      if (!isa<ImplicitCastExpr, MaterializeTemporaryExpr, CXXBindTemporaryExpr,
               ParenExpr, ExprWithCleanups>(Parent))
        return false;
      const auto Next = instantiatedParents(Up, Context);
      if (Next.empty())
        return false;
      Up = Next[0];
    }
  }
  return false;
}

/// What the local \p Local may have where \p Ref reads it when an `if` and
/// its `else` before it in a block around it each assign it last, also
/// through nested ones, `if (bChoose) pScratch = new int(1); else pScratch =
/// new int(2);`: each value assigned. None when that can't be told.
static llvm::SmallVector<const Expr *, 2>
branchAssignedValues(const VarDecl *Local, const DeclRefExpr *Ref) {
  llvm::SmallVector<const Expr *, 2> Values;
  ASTContext &Context = Local->getASTContext();
  // Whether \p S names the local other than to read it.
  std::function<bool(const Stmt *)> Changes = [&](const Stmt *S) {
    if (!S)
      return false;
    if (const auto *Cast = dyn_cast<ImplicitCastExpr>(S);
        Cast && Cast->getCastKind() == CK_LValueToRValue &&
        isa<DeclRefExpr>(Cast->getSubExpr()->IgnoreParens()))
      return false;
    if (const auto *Named = dyn_cast<DeclRefExpr>(S))
      return Named->getDecl() == Local;
    return llvm::any_of(childrenOnce(S), Changes);
  };
  auto Labeled = [](const Stmt *S) {
    std::function<bool(const Stmt *)> Has = [&](const Stmt *Child) {
      return Child && (isa<LabelStmt, SwitchCase>(Child) ||
                       llvm::any_of(childrenOnce(Child), Has));
    };
    return Has(S);
  };
  // What each path of \p S assigns last, when each does.
  std::function<bool(const Stmt *, int)> Collect = [&](const Stmt *S,
                                                       int Depth) {
    if (!S || Depth > 16 || Labeled(S))
      return false;
    if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(S))
      S = Cleanups->getSubExpr();
    if (const auto *Assign = dyn_cast<BinaryOperator>(S->IgnoreContainers());
        Assign && Assign->getOpcode() == BO_Assign) {
      const auto *Target =
          dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts());
      if (!Target || Target->getDecl() != Local || Changes(Assign->getRHS()))
        return false;
      Values.push_back(Assign->getRHS());
      return true;
    }
    if (const auto *If = dyn_cast<IfStmt>(S))
      return If->getElse() && !If->getInit() && !If->getConditionVariable() &&
             !Changes(If->getCond()) && Collect(If->getThen(), Depth + 1) &&
             Collect(If->getElse(), Depth + 1);
    if (const auto *Block = dyn_cast<CompoundStmt>(S)) {
      for (const Stmt *Statement : llvm::reverse(Block->body()))
        if (Changes(Statement))
          return Collect(Statement, Depth + 1);
    }
    return false;
  };
  DynTypedNode Node = DynTypedNode::create(*Ref);
  for (int Depth = 0; Depth < 64; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return {};
    const DynTypedNode &Parent = Parents[0];
    // Not past a loop, which can assign it after the use the next time
    // around, nor out of the function or a lambda.
    if (Parent.get<FunctionDecl>() || Parent.get<LambdaExpr>() ||
        Parent.get<ForStmt>() || Parent.get<WhileStmt>() ||
        Parent.get<DoStmt>() || Parent.get<CXXForRangeStmt>())
      return {};
    if (const auto *Block = Parent.get<CompoundStmt>()) {
      const Stmt *Child = Node.get<Stmt>();
      auto Use = llvm::find(Block->body(), Child);
      if (Use == Block->body_end())
        return {};
      for (auto Statement = std::make_reverse_iterator(Use);
           Statement != Block->body_rend(); ++Statement) {
        if (Labeled(*Statement))
          return {};
        if (Changes(*Statement)) {
          if (!isa<IfStmt>(*Statement) || !Collect(*Statement, 0))
            return {};
          return Values;
        }
      }
    }
    Node = Parent;
  }
  return {};
}

/// The arguments of \p Call whose members the function it calls, whose body
/// can be seen, returns, `_Owner` of `return _Owner.m_pData;`, also through
/// a pointer, `_pOwner->m_pData`.
static llvm::SmallVector<const Expr *, 2>
returnedMemberArguments(const CallExpr *Call) {
  llvm::SmallVector<const Expr *, 2> Arguments;
  const FunctionDecl *Callee = Call->getDirectCallee();
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody())
    return Arguments;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return;
    if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
      const Expr *Value = Return->getRetValue()
                              ? Return->getRetValue()->IgnoreParenImpCasts()
                              : nullptr;
      if (!isa_and_nonnull<MemberExpr>(Value))
        return;
      while (const auto *Member = dyn_cast_or_null<MemberExpr>(Value))
        Value = Member->getBase()->IgnoreParenImpCasts();
      const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Value);
      const auto *Parameter =
          Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl()) : nullptr;
      if (Parameter &&
          Parameter->getFunctionScopeIndex() < Call->getNumArgs() &&
          !llvm::is_contained(Arguments,
                              Call->getArg(Parameter->getFunctionScopeIndex())))
        Arguments.push_back(Call->getArg(Parameter->getFunctionScopeIndex()));
      return;
    }
    // Not in a branch of an `if constexpr` that is discarded.
    for (const Stmt *Child : keptChildren(S, Definition->getASTContext()))
      Visit(Child);
  };
  Visit(Definition->getBody());
  return Arguments;
}

/// The lambda that the scope exit \p Local holds runs where it is destroyed,
/// `auto Cleanup = g_OnScopeExit / [] { ... };` or `fg_OnScopeExit([] { ...
/// })`, or null. Not one that dispatches it to an actor, which runs it later.
static const LambdaExpr *scopeExitCallback(const VarDecl *Local) {
  if (!Local->hasLocalStorage() || Local->getType()->isReferenceType() ||
      !Local->getInit())
    return nullptr;
  const Expr *Init = Local->getInit()->IgnoreImplicit();
  if (const auto *Copy = dyn_cast<CXXConstructExpr>(Init);
      Copy && Copy->getNumArgs() == 1 &&
      Copy->getConstructor()->isCopyOrMoveConstructor())
    Init = Copy->getArg(0)->IgnoreImplicit();
  Init = Init->IgnoreParenImpCasts();
  auto IsScopeExit = [](StringRef Name) {
    return Name.contains("OnScopeExit") && !Name.contains("Actor");
  };
  if (const auto *Divide = dyn_cast<CXXOperatorCallExpr>(Init);
      Divide && Divide->getOperator() == OO_Slash &&
      Divide->getNumArgs() == 2) {
    const CXXRecordDecl *Helper = Divide->getArg(0)
                                      ->getType()
                                      .getNonReferenceType()
                                      ->getAsCXXRecordDecl();
    if (Helper && Helper->getIdentifier() && IsScopeExit(Helper->getName()))
      return calledLambda(Divide->getArg(1));
    return nullptr;
  }
  if (const auto *Call = dyn_cast<CallExpr>(Init);
      Call && Call->getNumArgs() >= 1 && Call->getDirectCallee() &&
      Call->getDirectCallee()->getIdentifier() &&
      IsScopeExit(Call->getDirectCallee()->getName()))
    return calledLambda(Call->getArg(0));
  return nullptr;
}

static bool ofObjectUncached(const Expr *E);

/// Whether \p E is the object or of it: `this`, a member of it, what one
/// points to, or a local made from one, `auto pData = m_pData;`. One that is
/// reached again while it is looked at, like a local made from itself, `int
/// *pData = false ? pData : m_pData`, is not, through that path.
static bool ofObject(const Expr *E) {
  static thread_local llvm::SmallPtrSet<const Expr *, 16> *s_pVisiting =
      nullptr;
  std::optional<llvm::SmallPtrSet<const Expr *, 16>> Owned;
  if (!s_pVisiting)
    s_pVisiting = &Owned.emplace();
  const llvm::scope_exit Release([&] {
    if (Owned)
      s_pVisiting = nullptr;
  });
  if (!E || s_pVisiting->size() > 1024 || !s_pVisiting->insert(E).second)
    return false;
  const bool Of = ofObjectUncached(E);
  s_pVisiting->erase(E);
  return Of;
}

/// The range loop whose variable \p Local is, `for (int *pData : Pointers)`,
/// or null.
static const CXXForRangeStmt *rangeLoopOf(const VarDecl *Local) {
  ASTContext &Context = const_cast<ASTContext &>(Local->getASTContext());
  const auto Parents = instantiatedParents(*Local, Context);
  const auto *Declaration =
      Parents.empty() ? nullptr : Parents[0].get<DeclStmt>();
  if (!Declaration)
    return nullptr;
  const auto Above = instantiatedParents(*Declaration, Context);
  const auto *Loop = Above.empty() ? nullptr : Above[0].get<CXXForRangeStmt>();
  return Loop && Loop->getLoopVariable() == Local ? Loop : nullptr;
}

/// What the member \p Member of a local aggregate made with a list can hold
/// where it is used, `m_pData` of `Args.m_pData` with `CReleaseArgs
/// Args{m_pData}`, also through an unchanged local pointer to it,
/// `pArgs->m_pData` with `auto *pArgs = &Args`: what the last assignment to it
/// in a statement of the body before gives, `Args.m_pData = new int(...)`, or
/// what it is made with, and what assignments in branches before give. Empty
/// when that can't be told.
static llvm::SmallVector<const Expr *, 2>
memberValuesOfLocal(const MemberExpr *Member) {
  llvm::SmallVector<const Expr *, 2> Values;
  const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
  const Expr *Base = Member->getBase()->IgnoreParenImpCasts();
  // The pointer to it the member is reached through, whose making takes its
  // address.
  const VarDecl *Through = nullptr;
  const Expr *TakenAddress = nullptr;
  if (Member->isArrow()) {
    const auto *PointerRef = dyn_cast<DeclRefExpr>(Base);
    const auto *Pointer =
        PointerRef ? dyn_cast<VarDecl>(PointerRef->getDecl()) : nullptr;
    const auto *PointerFunction =
        Pointer ? dyn_cast_or_null<FunctionDecl>(
                      Pointer->getParentFunctionOrMethod())
                : nullptr;
    const auto *Address =
        Pointer && Pointer->hasLocalStorage() && !isa<ParmVarDecl>(Pointer) &&
                Pointer->getInit() && PointerFunction &&
                PointerFunction->getBody() &&
                !isReassigned(Pointer, PointerFunction->getBody())
            ? dyn_cast<UnaryOperator>(Pointer->getInit()->IgnoreParenImpCasts())
            : nullptr;
    if (!Address || Address->getOpcode() != UO_AddrOf)
      return Values;
    Through = Pointer;
    TakenAddress = Address;
    Base = Address->getSubExpr()->IgnoreParenImpCasts();
  }
  const auto *Ref = dyn_cast<DeclRefExpr>(Base);
  const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  if (!Field || !Local || !Local->hasLocalStorage() ||
      isa<ParmVarDecl>(Local) || Local->getType()->isReferenceType() ||
      !Local->getInit())
    return Values;
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
  const auto *List = dyn_cast<InitListExpr>(Local->getInit()->IgnoreImplicit());
  const auto *Record = Local->getType()->getAsCXXRecordDecl();
  // The unchanged local pointers to it, `auto *pArgs = &Args`, through which
  // its member is assigned too, `pArgs->m_pData = ...`, and the addresses
  // they are made with.
  llvm::SmallPtrSet<const VarDecl *, 2> Aliases;
  llvm::SmallPtrSet<const Expr *, 2> AliasAddresses;
  if (Through) {
    Aliases.insert(Through);
    AliasAddresses.insert(TakenAddress);
  }
  if (Function && Function->getBody()) {
    std::function<void(const Stmt *)> FindAliases = [&](const Stmt *S) {
      if (!S || isa<LambdaExpr>(S))
        return;
      if (const auto *Declaration = dyn_cast<DeclStmt>(S))
        for (const Decl *D : Declaration->decls())
          if (const auto *Alias = dyn_cast<VarDecl>(D);
              Alias && Alias->getType()->isPointerType() && Alias->getInit())
            if (const auto *Address = dyn_cast<UnaryOperator>(
                    Alias->getInit()->IgnoreParenImpCasts());
                Address && Address->getOpcode() == UO_AddrOf)
              if (const auto *Target = dyn_cast<DeclRefExpr>(
                      Address->getSubExpr()->IgnoreParenImpCasts());
                  Target && Target->getDecl() == Local &&
                  !isReassigned(Alias, Function->getBody())) {
                Aliases.insert(Alias);
                AliasAddresses.insert(Address);
              }
      for (const Stmt *Child : childrenOnce(S))
        FindAliases(Child);
    };
    if (Local)
      FindAliases(Function->getBody());
  }
  // Not changed as a whole, nor its address taken other than by those.
  auto ChangedOtherwise = [&] {
    bool Changed = false;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || Changed)
        return;
      if (const auto *Assign = dyn_cast<BinaryOperator>(S);
          Assign && Assign->isAssignmentOp())
        if (const auto *Target =
                dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts());
            Target && Target->getDecl() == Local)
          Changed = true;
      if (const auto *Address = dyn_cast<UnaryOperator>(S);
          Address && !AliasAddresses.contains(Address) &&
          Address->getOpcode() == UO_AddrOf)
        if (const auto *Target = dyn_cast<DeclRefExpr>(
                Address->getSubExpr()->IgnoreParenImpCasts());
            Target && Target->getDecl() == Local)
          Changed = true;
      for (const Stmt *Child : childrenOnce(S))
        Visit(Child);
    };
    Visit(Function->getBody());
    return Changed;
  };
  if (!Function || !Function->getBody() || !List || !Record ||
      Field->getParent()->getCanonicalDecl() != Record->getCanonicalDecl() ||
      ChangedOtherwise())
    return Values;
  const Expr *Init = nullptr;
  if (List->getInitializedFieldInUnion()) {
    if (List->getInitializedFieldInUnion() != Field || !List->getNumInits())
      return Values;
    Init = List->getInit(0);
  } else {
    // After the bases, in the order of the fields.
    unsigned iInit = Record->getNumBases();
    for (const FieldDecl *Other : Record->fields()) {
      if (Other->isUnnamedBitField())
        continue;
      if (Other == Field) {
        Init = iInit < List->getNumInits() ? List->getInit(iInit) : nullptr;
        break;
      }
      ++iInit;
    }
  }
  if (!Init)
    return Values;
  // The assignments to the member, `Args.m_pData = ...`, before the use, and
  // whether each is a statement of the body, which runs on every path.
  const SourceManager &SM = Function->getASTContext().getSourceManager();
  const auto *Body = dyn_cast<CompoundStmt>(Function->getBody());
  const Expr *Last = Init;
  bool Unknown = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Unknown)
      return;
    if (isa<LabelStmt, ForStmt, WhileStmt, DoStmt, CXXForRangeStmt>(S) &&
        !SM.isBeforeInTranslationUnit(S->getEndLoc(), Member->getBeginLoc()))
      Unknown = true;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->isAssignmentOp())
      if (const auto *Target =
              dyn_cast<MemberExpr>(Assign->getLHS()->IgnoreParenImpCasts());
          Target && Target->getMemberDecl() == Field)
        if (const auto *Of =
                dyn_cast<DeclRefExpr>(Target->getBase()->IgnoreParenImpCasts());
            Of &&
            (Target->isArrow()
                 ? Aliases.contains(dyn_cast<VarDecl>(Of->getDecl()))
                 : Of->getDecl() == Local) &&
            SM.isBeforeInTranslationUnit(Assign->getEndLoc(),
                                         Member->getBeginLoc())) {
          if (Assign->getOpcode() != BO_Assign) {
            Unknown = true;
            return;
          }
          const bool Statement =
              Body && llvm::any_of(Body->body(), [&](const Stmt *Child) {
                const auto *E = dyn_cast<Expr>(Child);
                return E && E->IgnoreImplicit() == Assign;
              });
          if (Statement) {
            Values.clear();
            Last = Assign->getRHS();
          } else
            Values.push_back(Assign->getRHS());
        }
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Function->getBody());
  if (Unknown)
    return {};
  Values.push_back(Last);
  return Values;
}

static bool ofObjectUncached(const Expr *E) {
  int nLocals = 0;
  // Whether what is looked at is read as a value, `fg_Close(Descriptor)`,
  // like the initializer of a local copied from it, not referred to, like
  // `&Value`.
  bool AsValue = false;
  if (const auto *Read = dyn_cast<ImplicitCastExpr>(E->IgnoreParens()))
    AsValue = Read->getCastKind() == CK_LValueToRValue;
  for (E = E->IgnoreParenImpCasts();;) {
    if (isa<CXXThisExpr>(E))
      return true;
    // A local made from it, `auto pData = m_pData;`.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
      // A name of a structured binding, `auto &[pData] = *this`, is what it
      // binds.
      if (const auto *Binding = dyn_cast<BindingDecl>(Ref->getDecl());
          Binding && Binding->getBinding() && ++nLocals <= 4) {
        E = Binding->getBinding()->IgnoreParenImpCasts();
        continue;
      }
      const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
      // An init capture is what it is made from, like a scratch copy,
      // `[pScratch = new int(*m_pData)]`.
      // What it was assigned last in the body, like `pData = new
      // int(*pData)`, is what it has there.
      // But in the body of a lambda that isn't called where it is made, one
      // is a member of the lambda, like a capture, `[pData = _pData] {
      // delete pData; }` that a guard calls.
      if (Local && Local->isInitCapture() && Local->getInit() &&
          !calledWhereMade(Ref))
        return true;
      if (Local && Local->isInitCapture() && Local->getInit() &&
          ++nLocals <= 4) {
        const Expr *Assigned = assignedValueAt(Local, Ref);
        E = (Assigned ? Assigned : Local->getInit())->IgnoreParenImpCasts();
        continue;
      }
      // A capture, in the body of a lambda, is a member of the lambda, like
      // the `_pData` of `[_pData] { delete _pData; }` that a guard calls,
      // but one of a local by a lambda called where it is made is what the
      // local is made from, like a scratch copy, `int *pScratch = new
      // int(*m_pData); [pScratch] { delete pScratch; }()`, and one of a
      // parameter is the parameter, like a visitor given to a member function,
      // `[&] { fs_Call(this, _Visitor); }()`.
      if (Ref->refersToEnclosingVariableOrCapture() &&
          !(Local && Local->hasLocalStorage() && calledWhereMade(Ref)))
        return true;
      // A local scalar copied from what a pointer points to, `int Value =
      // *m_pData`, is a copy of the data, not of what the object owns, where
      // it is referred to, `fg_Reset(&Value)`, which changes only the copy,
      // but its value is the data's, like a handle, `close(Descriptor)`.
      if (!AsValue && Local && Local->hasLocalStorage() &&
          !isa<ParmVarDecl>(Local) && Local->getType()->isScalarType() &&
          !Local->getType()->isPointerType() && Local->getInit())
        if (const Expr *Copied = Local->getInit()->IgnoreParenImpCasts();
            (isa<UnaryOperator>(Copied) &&
             cast<UnaryOperator>(Copied)->getOpcode() == UO_Deref) ||
            isa<ArraySubscriptExpr>(Copied) ||
            (isa<MemberExpr>(Copied) && cast<MemberExpr>(Copied)->isArrow() &&
             !isa<CXXThisExpr>(
                 cast<MemberExpr>(Copied)->getBase()->IgnoreParenImpCasts())))
          if (!assignedFromObject(Local))
            return false;
      if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
          ++nLocals > 4)
        return false;
      // What a local that isn't a reference is made from is read as a value.
      if (!Local->getType()->isReferenceType())
        AsValue = true;
      // The variable of a range loop, `for (int *pData : Pointers)`, is any
      // element of the range: of a local array made with a list, `int
      // *Pointers[] = {m_pData}`, any it is made with, or of what is of the
      // object, `m_Pointers`.
      if (const CXXForRangeStmt *Loop = rangeLoopOf(Local)) {
        const Expr *Range = Loop->getRangeInit()->IgnoreParenImpCasts();
        const auto *RangeRef = dyn_cast<DeclRefExpr>(Range);
        const auto *Array =
            RangeRef ? dyn_cast<VarDecl>(RangeRef->getDecl()) : nullptr;
        if (Array && Array->hasLocalStorage() && !isa<ParmVarDecl>(Array) &&
            Array->getType()->isArrayType() && Array->getInit())
          if (const auto *List =
                  dyn_cast<InitListExpr>(Array->getInit()->IgnoreImplicit()))
            return llvm::any_of(List->inits(), [](const Expr *Element) {
              return ofObject(Element);
            });
        E = Range;
        continue;
      }
      // Given what is of the object by a call before, `fg_Swap(pData,
      // m_pData)` or `fg_Take(pData, m_pData)`, which an assignment after
      // can't be told to replace.
      if (givenFromObjectBefore(Local, Ref))
        return true;
      // What it was last assigned on every path, like `pCopy = new int`, or
      // its initializer when nothing assigns it before, not what is assigned
      // later.
      if (const Expr *Assigned = assignedValueAt(Local, Ref)) {
        E = Assigned->IgnoreParenImpCasts();
        continue;
      }
      // Also one assigned it later, `pData = m_pData;`.
      if (assignedFromObject(Local))
        return true;
      // Or what each branch before assigns it last, `if (bChoose) pScratch =
      // new int(1); else pScratch = new int(2);`.
      if (const llvm::SmallVector<const Expr *, 2> Values =
              branchAssignedValues(Local, Ref);
          !Values.empty())
        return llvm::any_of(Values,
                            [](const Expr *Value) { return ofObject(Value); });
      if (!Local->getInit())
        return false;
      E = Local->getInit()->IgnoreParenImpCasts();
      continue;
    }
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E)) {
      E = Cast->getSubExpr()->IgnoreParenImpCasts();
      continue;
    }
    // An aggregate made with it, `fg_Free({m_pData, 16})`, which holds it.
    if (const auto *List = dyn_cast<InitListExpr>(E);
        List && List->getType()->isRecordType())
      return llvm::any_of(
          List->inits(), [](const Expr *Element) { return ofObject(Element); });
    // A copy of it, like a handle given by value, `fg_Close(m_Handle)`, but
    // not one a copy constructor makes by doing something of its own, like
    // taking another reference to what it shares, `++m_pPointTo->m_nUsers`,
    // which owns that and releases only that.
    if (const auto *Copy = dyn_cast<CXXConstructExpr>(E);
        Copy && Copy->getNumArgs() == 1 &&
        Copy->getConstructor()->isCopyOrMoveConstructor()) {
      const CXXConstructorDecl *Constructor = Copy->getConstructor();
      // Also in its initializers, through a constructor of a base or member
      // that does, `CStr(CStr const &_Str) : CImp(_Str) {}`, or what isn't a
      // member copied as it is, `m_pData(_Other.m_pData)`.
      std::function<bool(const CXXConstructorDecl *, int)> DoesOwn =
          [&](const CXXConstructorDecl *Made, int Depth) -> bool {
        const auto *Defined =
            dyn_cast_or_null<CXXConstructorDecl>(Made->getDefinition());
        if (!Defined || !Made->isUserProvided() || Depth > 8)
          return false;
        if (const auto *Body = dyn_cast_or_null<CompoundStmt>(Defined->getBody());
            Body && !Body->body_empty())
          return true;
        return llvm::any_of(
            Defined->inits(), [&](const CXXCtorInitializer *Initializer) {
              if (!Initializer->isWritten() || !Initializer->getInit())
                return false;
              const Expr *Init = Initializer->getInit()->IgnoreImplicit();
              if (const auto *Inner = dyn_cast<CXXConstructExpr>(Init))
                return DoesOwn(Inner->getConstructor(), Depth + 1);
              return !isa<MemberExpr, DeclRefExpr>(Init->IgnoreParenImpCasts());
            });
      };
      if (Constructor->isCopyConstructor() && DoesOwn(Constructor, 0))
        return false;
      E = Copy->getArg(0)->IgnoreParenImpCasts();
      continue;
    }
    if (const auto *Member = dyn_cast<MemberExpr>(E)) {
      // A member of an unchanged local aggregate made with a list, `Args
      // .m_pData` of `CReleaseArgs Args{m_pData}`, is what that member is
      // made with.
      if (const llvm::SmallVector<const Expr *, 2> Values =
              memberValuesOfLocal(Member);
          !Values.empty()) {
        if (Values.size() == 1) {
          E = Values.front()->IgnoreParenImpCasts();
          continue;
        }
        return llvm::any_of(Values,
                            [](const Expr *Value) { return ofObject(Value); });
      }
      E = Member->getBase()->IgnoreParenImpCasts();
    }
    // An element of an array member, `m_pData[0]`, or of a local array made
    // with a list, `int *Pointers[] = {m_pData}`, the one chosen, or any.
    else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E)) {
      const auto *ArrayRef =
          dyn_cast<DeclRefExpr>(Subscript->getBase()->IgnoreParenImpCasts());
      const auto *Array =
          ArrayRef ? dyn_cast<VarDecl>(ArrayRef->getDecl()) : nullptr;
      const auto *List =
          Array && Array->hasLocalStorage() && !isa<ParmVarDecl>(Array) &&
                  Array->getType()->isArrayType() && Array->getInit()
              ? dyn_cast<InitListExpr>(Array->getInit()->IgnoreImplicit())
              : nullptr;
      // The element chosen, when the index is a constant.
      std::optional<llvm::APSInt> Chosen;
      if (Array && !Subscript->getIdx()->isValueDependent())
        Chosen =
            Subscript->getIdx()->getIntegerConstantExpr(Array->getASTContext());
      // Also what is assigned to its elements, `Pointers[0] = m_pData`: to
      // the one chosen, or to one that can't be told.
      // Each array once while what an element of it is is told, which an
      // element assigned itself, `Pointers[0] = Pointers[0]`, asks again.
      static thread_local llvm::SmallPtrSet<const VarDecl *, 4> s_Arrays;
      if (Array && Array->hasLocalStorage() && !isa<ParmVarDecl>(Array) &&
          Array->getType()->isArrayType() && nLocals < 4 &&
          !s_Arrays.contains(Array)) {
        s_Arrays.insert(Array);
        const llvm::scope_exit Release([&] { s_Arrays.erase(Array); });
        auto MayBeChosen = [&](const Expr *Index) {
          if (!Chosen || Index->isValueDependent())
            return true;
          const std::optional<llvm::APSInt> Assigned =
              Index->getIntegerConstantExpr(Array->getASTContext());
          return !Assigned || llvm::APSInt::isSameValue(*Assigned, *Chosen);
        };
        const auto *Function =
            dyn_cast_or_null<FunctionDecl>(Array->getParentFunctionOrMethod());
        // What the chosen element was last assigned by a statement of the
        // body before it is read, `Pointers[0] = new int(*Pointers[0]);`,
        // which replaces what it was made with.
        const auto *Body =
            Function ? dyn_cast_or_null<CompoundStmt>(Function->getBody())
                     : nullptr;
        const Expr *Replaced = nullptr;
        const Stmt *ReplacedBy = nullptr;
        if (Chosen && Body)
          for (const Stmt *Statement : Body->body()) {
            if (!Array->getASTContext()
                     .getSourceManager()
                     .isBeforeInTranslationUnit(Statement->getEndLoc(),
                                                Subscript->getBeginLoc()))
              break;
            if (isa<LabelStmt, SwitchCase>(Statement))
              Replaced = nullptr;
            const auto *Expression = dyn_cast<Expr>(Statement);
            const auto *Assign =
                Expression
                    ? dyn_cast<BinaryOperator>(Expression->IgnoreImplicit())
                    : nullptr;
            const auto *Element =
                Assign && Assign->getOpcode() == BO_Assign
                    ? dyn_cast<ArraySubscriptExpr>(
                          Assign->getLHS()->IgnoreParenImpCasts())
                    : nullptr;
            const auto *Of =
                Element ? dyn_cast<DeclRefExpr>(
                              Element->getBase()->IgnoreParenImpCasts())
                        : nullptr;
            const std::optional<llvm::APSInt> Index =
                Of && Of->getDecl() == Array &&
                        !Element->getIdx()->isValueDependent()
                    ? Element->getIdx()->getIntegerConstantExpr(
                          Array->getASTContext())
                    : std::nullopt;
            if (Index && llvm::APSInt::isSameValue(*Index, *Chosen)) {
              Replaced = Assign->getRHS();
              ReplacedBy = Statement;
            }
          }
        // Not what it is assigned after it is read, `delete Pointers[0];
        // Pointers[0] = m_pData;`, unless a loop around the read, or a jump,
        // can run it again after.
        const SourceManager &SM = Array->getASTContext().getSourceManager();
        const Stmt *Loop = nullptr;
        bool Jumps = false;
        std::function<void(const Stmt *)> FindLoop = [&](const Stmt *S) {
          if (!S || isa<LambdaExpr>(S))
            return;
          if (isa<LabelStmt, IndirectGotoStmt>(S))
            Jumps = true;
          if (!Loop && isa<ForStmt, WhileStmt, DoStmt, CXXForRangeStmt>(S) &&
              !SM.isBeforeInTranslationUnit(Subscript->getBeginLoc(),
                                            S->getBeginLoc()) &&
              !SM.isBeforeInTranslationUnit(S->getEndLoc(),
                                            Subscript->getEndLoc()))
            Loop = S;
          for (const Stmt *Child : childrenOnce(S))
            FindLoop(Child);
        };
        FindLoop(Function ? Function->getBody() : nullptr);
        auto MayRunBefore = [&](const Stmt *Assign) {
          return Jumps ||
                 SM.isBeforeInTranslationUnit(Assign->getBeginLoc(),
                                              Subscript->getBeginLoc()) ||
                 (Loop &&
                  !SM.isBeforeInTranslationUnit(Assign->getBeginLoc(),
                                                Loop->getBeginLoc()) &&
                  !SM.isBeforeInTranslationUnit(Loop->getEndLoc(),
                                                Assign->getEndLoc()));
        };
        bool Assigned = false;
        std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
          if (!S || Assigned || isa<LambdaExpr>(S))
            return;
          if (const auto *Assign = dyn_cast<BinaryOperator>(S);
              Assign && Assign->getOpcode() == BO_Assign &&
              MayRunBefore(Assign))
            if (const auto *Element = dyn_cast<ArraySubscriptExpr>(
                    Assign->getLHS()->IgnoreParenImpCasts()))
              if (const auto *Of = dyn_cast<DeclRefExpr>(
                      Element->getBase()->IgnoreParenImpCasts());
                  Of && Of->getDecl() == Array &&
                  MayBeChosen(Element->getIdx()) && ofObject(Assign->getRHS()))
                Assigned = true;
          for (const Stmt *Child : childrenOnce(S))
            Find(Child);
        };
        ++nLocals;
        // What it was assigned before it was replaced, `Pointers[0] =
        // m_pData;`, is no longer in it.
        if (ReplacedBy) {
          bool After = false;
          for (const Stmt *Statement : Body->body()) {
            if (After)
              Find(Statement);
            After = After || Statement == ReplacedBy;
          }
        } else
          Find(Function ? Function->getBody() : nullptr);
        if (Assigned)
          return true;
        if (Replaced) {
          E = Replaced->IgnoreParenImpCasts();
          continue;
        }
      }
      if (List && ++nLocals <= 4) {
        if (!Chosen)
          return llvm::any_of(List->inits(), ofObject);
        if (Chosen->isNegative() || Chosen->getZExtValue() >= List->getNumInits())
          return false;
        E = List->getInit(Chosen->getZExtValue())->IgnoreParenImpCasts();
        continue;
      }
      E = Subscript->getBase()->IgnoreParenImpCasts();
    } else if (const auto *Unary = dyn_cast<UnaryOperator>(E);
               Unary && (Unary->getOpcode() == UO_AddrOf ||
                         Unary->getOpcode() == UO_Deref)) {
      // The address of a local copy of a value, `&Value` of `int Value =
      // *m_pData`, is of its own storage, which writing doesn't change the
      // object.
      if (const auto *Ref = dyn_cast<DeclRefExpr>(
              Unary->getSubExpr()->IgnoreParenImpCasts());
          Ref && Unary->getOpcode() == UO_AddrOf)
        if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
            Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
            !Ref->refersToEnclosingVariableOrCapture() &&
            (Local->getType()->isArithmeticType() ||
             Local->getType()->isEnumeralType()))
          return false;
      // What a local array made with a list points to, `*Pointers` of `int
      // *Pointers[]{m_pData}`, is its first element, also one made empty,
      // `int *Pointers[1] = {}`, whose elements are null until assigned.
      if (const auto *Ref =
              dyn_cast<DeclRefExpr>(Unary->getSubExpr()->IgnoreParenImpCasts());
          Ref && Unary->getOpcode() == UO_Deref)
        if (const auto *Array = dyn_cast<VarDecl>(Ref->getDecl());
            Array && Array->hasLocalStorage() && !isa<ParmVarDecl>(Array) &&
            Array->getType()->isArrayType() && Array->getInit())
          if (const auto *List =
                  dyn_cast<InitListExpr>(Array->getInit()->IgnoreImplicit());
              List && ++nLocals <= 4) {
            const Expr *Made =
                List->getNumInits() > 0 ? List->getInit(0) : nullptr;
            // Or what the first element is assigned before it is read,
            // `Pointers[0] = m_pData`: the last statement of the body that
            // does, or any assignment before it, also in a branch, and the
            // element it was made with when one is in a branch.
            const auto *Function = dyn_cast_or_null<FunctionDecl>(
                Array->getParentFunctionOrMethod());
            const auto *Body =
                Function ? dyn_cast_or_null<CompoundStmt>(Function->getBody())
                         : nullptr;
            const SourceManager &SM = Array->getASTContext().getSourceManager();
            auto AssignedFirst = [&](const Stmt *S) -> const Expr * {
              const auto *Assign = dyn_cast_or_null<BinaryOperator>(
                  isa_and_nonnull<Expr>(S) ? cast<Expr>(S)->IgnoreImplicit()
                                           : S);
              if (!Assign || Assign->getOpcode() != BO_Assign)
                return nullptr;
              // Also through what it decays to, `*Pointers = ...`, which is
              // its first.
              if (const auto *Deref = dyn_cast<UnaryOperator>(
                      Assign->getLHS()->IgnoreParenImpCasts());
                  Deref && Deref->getOpcode() == UO_Deref) {
                const auto *First = dyn_cast<DeclRefExpr>(
                    Deref->getSubExpr()->IgnoreParenImpCasts());
                return First && First->getDecl() == Array ? Assign->getRHS()
                                                          : nullptr;
              }
              const auto *Element = dyn_cast<ArraySubscriptExpr>(
                  Assign->getLHS()->IgnoreParenImpCasts());
              const auto *Of =
                  Element ? dyn_cast<DeclRefExpr>(
                                Element->getBase()->IgnoreParenImpCasts())
                          : nullptr;
              if (!Of || Of->getDecl() != Array)
                return nullptr;
              const std::optional<llvm::APSInt> Index =
                  Element->getIdx()->isValueDependent()
                      ? std::nullopt
                      : Element->getIdx()->getIntegerConstantExpr(
                            Array->getASTContext());
              return !Index || Index->isZero() ? Assign->getRHS() : nullptr;
            };
            const Expr *Last = nullptr;
            bool Nested = false;
            llvm::SmallVector<const Expr *, 4> Values;
            std::function<void(const Stmt *, bool)> Find = [&](const Stmt *S,
                                                               bool TopLevel) {
              if (!S || isa<LambdaExpr>(S) ||
                  !SM.isBeforeInTranslationUnit(S->getBeginLoc(),
                                                Unary->getBeginLoc()))
                return;
              if (const Expr *Value = AssignedFirst(S)) {
                Values.push_back(Value);
                if (TopLevel)
                  Last = Value;
                else
                  Nested = true;
              }
              for (const Stmt *Child : childrenOnce(S))
                Find(Child, false);
            };
            if (Body)
              for (const Stmt *Statement : Body->body())
                Find(Statement, true);
            if (Values.empty()) {
              if (!Made)
                return false;
              E = Made->IgnoreParenImpCasts();
              continue;
            }
            if (Last && !Nested && Values.back() == Last) {
              E = Last->IgnoreParenImpCasts();
              continue;
            }
            if (Made)
              Values.push_back(Made);
            return llvm::any_of(Values, ofObject);
          }
      E = Unary->getSubExpr()->IgnoreParenImpCasts();
    }
    // What a function that can be looked into gives back of what it is
    // given, `fg_Get(m_pData)` of `return _pData;`.
    else if (const auto *Pass = dyn_cast<CallExpr>(E);
             Pass && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Pass) &&
             !returnedArguments(Pass).empty()) {
      const llvm::SmallVector<const Expr *, 2> Passed = returnedArguments(Pass);
      if (Passed.size() > 1)
        return llvm::any_of(Passed, ofObject);
      E = Passed.front()->IgnoreParenImpCasts();
    }
    // Also the address of what it is given by reference, `fg_Pointer(
    // m_pData)` of `int **fg_Pointer(int *&_pData) { return &_pData; }`.
    else if (const auto *Address = dyn_cast<CallExpr>(E);
             Address && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Address) &&
             !returnedArguments(Address, /*Addressed=*/true).empty()) {
      const llvm::SmallVector<const Expr *, 2> Addressed =
          returnedArguments(Address, /*Addressed=*/true);
      if (Addressed.size() > 1)
        return llvm::any_of(Addressed, ofObject);
      E = Addressed.front()->IgnoreParenImpCasts();
    }
    // Also a member of what it is given, `fs_Data(*this)` of `return
    // _Owner.m_pData;`, which is of what that is.
    else if (const auto *Pass = dyn_cast<CallExpr>(E);
             Pass && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Pass) &&
             !returnedMemberArguments(Pass).empty())
      return llvm::any_of(returnedMemberArguments(Pass), ofObject);
    // Also a pointer a function decodes from the integer it is given,
    // `fsp_FromInt(mp_PointTo)` of `return (t_CType *)(_Pointer << t_nBits);`
    // of a tagged pointer, which is what that integer keeps.
    else if (const auto *Decode = dyn_cast<CallExpr>(E);
             Decode && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Decode) &&
             Decode->getType()->isPointerType() && decodesPointer(Decode))
      E = Decode->getArg(0)->IgnoreParenImpCasts();
    // What a member function of it gives, `f_Get()`, also a copy of its
    // state, `f_GetState().m_pData`, unless it can be looked into and returns
    // only what isn't, like a scratch copy, `return new int(*m_pValue);`.
    else if (const auto *Getter = dyn_cast<CXXMemberCallExpr>(E);
             Getter && Getter->getImplicitObjectArgument() &&
             (Getter->getType()->isPointerType() || Getter->isGLValue() ||
              Getter->getType()->isIntegralOrEnumerationType() ||
              Getter->getType()->isRecordType())) {
      if (const std::optional<bool> Returns = returnsOfObject(Getter);
          Returns && !*Returns)
        return false;
      E = Getter->getImplicitObjectArgument()->IgnoreParenImpCasts();
    }
    // What a lambda called gives back, `[this] { return m_pData; }()`, of any
    // of its returns.
    else if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(E);
             Invoke && Invoke->getOperator() == OO_Call &&
             Invoke->getNumArgs() >= 1 && calledLambda(Invoke->getArg(0))) {
      llvm::SmallVector<const Expr *, 4> Values;
      std::function<void(const Stmt *)> Returns = [&](const Stmt *S) {
        if (!S || isa<LambdaExpr>(S))
          return;
        if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
          if (Return->getRetValue())
            Values.push_back(Return->getRetValue());
          return;
        }
        for (const Stmt *Child : childrenOnce(S))
          Returns(Child);
      };
      Returns(calledLambda(Invoke->getArg(0))->getBody());
      return llvm::any_of(Values, ofObject);
    }
    // Also an overloaded operator of it, `(*this)[0]`, `*Iterator` or
    // `Holder->m_pData`.
    else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
             Operator && Operator->getNumArgs() >= 1 &&
             hasImplicitObject(Operator->getDirectCallee()) &&
             !Operator->isAssignmentOp() &&
             (Operator->getType()->isPointerType() || Operator->isGLValue() ||
              Operator->getType()->isIntegralOrEnumerationType() ||
              Operator->getType()->isRecordType())) {
      if (const std::optional<bool> Returns = returnsOfObject(Operator);
          Returns && !*Returns)
        return false;
      E = Operator->getArg(0)->IgnoreParenImpCasts();
    }
    // Moved where it is, `--m_pData` or `m_pData -= 1`, which gives it.
    else if (const auto *Step = dyn_cast<UnaryOperator>(E);
             Step && Step->isIncrementDecrementOp())
      E = Step->getSubExpr()->IgnoreParenImpCasts();
    else if (const auto *Moved = dyn_cast<CompoundAssignOperator>(E);
             Moved && (Moved->getOpcode() == BO_AddAssign ||
                       Moved->getOpcode() == BO_SubAssign))
      E = Moved->getLHS()->IgnoreParenImpCasts();
    // A pointer of it moved, `mp_pData - 1`.
    else if (const auto *Binary = dyn_cast<BinaryOperator>(E);
             Binary && Binary->isAdditiveOp() &&
             (Binary->getLHS()->getType()->isPointerType() ||
              Binary->getRHS()->getType()->isPointerType()))
      E = (Binary->getLHS()->getType()->isPointerType() ? Binary->getLHS()
                                                         : Binary->getRHS())
              ->IgnoreParenImpCasts();
    // The last of a comma, `(m_bActive = false, m_pData)`.
    else if (const auto *Comma = dyn_cast<BinaryOperator>(E);
             Comma && Comma->getOpcode() == BO_Comma)
      E = Comma->getRHS()->IgnoreParenImpCasts();
    // A pointer kept in an integer with tag bits, `m_Pointer & ~umint(1)`,
    // or a handle kept encoded, `m_FilePlusOne - 1`, `m_Shifted >> 1` or
    // `m_PointerDivAlign * alignof(int)`, of either side.
    else if (const auto *Bits = dyn_cast<BinaryOperator>(E);
             Bits &&
             (Bits->isBitwiseOp() || Bits->isAdditiveOp() ||
              Bits->isShiftOp() || Bits->isMultiplicativeOp()) &&
             Bits->getType()->isIntegerType())
      return ofObject(Bits->getLHS()) || ofObject(Bits->getRHS());
    // A handle kept complemented or negated, `~mp_EncodedDescriptor`.
    else if (const auto *Unary = dyn_cast<UnaryOperator>(E);
             Unary &&
             (Unary->getOpcode() == UO_Not || Unary->getOpcode() == UO_Minus) &&
             Unary->getType()->isIntegerType())
      E = Unary->getSubExpr()->IgnoreParenImpCasts();
    // Either value of a conditional, `bArray ? mp_pData : nullptr`.
    else if (const auto *Conditional =
                 dyn_cast<AbstractConditionalOperator>(E))
      return ofObject(Conditional->getTrueExpr()) ||
             ofObject(Conditional->getFalseExpr());
    // A member through a pointer to it, `this->*pmData`.
    else if (const auto *Member = dyn_cast<BinaryOperator>(E);
             Member && Member->isPtrMemOp())
      E = Member->getLHS()->IgnoreParenImpCasts();
    else
      return false;
  }
}

/// What is worked out about classes and functions while the outermost
/// question is answered, so that ones reached on many paths, like helpers
/// that call each other many times or classes that share bases or members,
/// are each looked into once, not once per path to them.
struct CReleaseCache {
  using CFunctionKey = std::pair<const FunctionDecl *, unsigned>;
  using CBodyKey = const FunctionDecl *;
  llvm::DenseMap<const void *, bool> Records;
  llvm::DenseMap<CFunctionKey, bool> Releases;
  llvm::DenseMap<CFunctionKey, bool> ReleasesOfOwn;
  llvm::DenseMap<CFunctionKey, bool> Writes;
  llvm::DenseMap<CBodyKey, bool> Does;
  /// What is being looked into, by how many are being looked into below
  /// it.
  llvm::DenseMap<const void *, unsigned> RecordsInProgress;
  llvm::DenseMap<CFunctionKey, unsigned> ReleasesInProgress;
  llvm::DenseMap<CFunctionKey, unsigned> ReleasesOfOwnInProgress;
  llvm::DenseMap<CFunctionKey, unsigned> WritesInProgress;
  llvm::DenseMap<CBodyKey, unsigned> DoesInProgress;
  /// How many are being looked into.
  unsigned Depth = 0;
  /// The least depth of one being looked into that what is worked out now
  /// reached again, whose result then depends on it: kept only by the one
  /// at that depth, once it is worked out, not by those above it.
  unsigned Reached = std::numeric_limits<unsigned>::max();
  /// And the greatest, until which a result that depends on them is the same
  /// for each that asks again, like the helpers of a cycle that each call the
  /// one before twice: kept until that one is worked out, then forgotten.
  unsigned ReachedDeepest = 0;
  bool AnyReached = false;
  /// Such results, by what they are of and the depths they depend on, and
  /// what to forget once the one at a depth is worked out.
  struct CProvisional {
    bool Result;
    unsigned Reached;
    unsigned ReachedDeepest;
  };
  using CProvisionalKey = std::pair<const void *, std::pair<const void *, unsigned>>;
  llvm::DenseMap<CProvisionalKey, CProvisional> Provisional;
  llvm::DenseMap<unsigned, llvm::SmallVector<CProvisionalKey, 4>> ProvisionalOf;
};

static std::pair<const void *, unsigned> provisionalKey(const void *Key) {
  return {Key, 0};
}
static std::pair<const void *, unsigned>
provisionalKey(const std::pair<const FunctionDecl *, unsigned> &Key) {
  return {Key.first, Key.second + 1};
}

static thread_local CReleaseCache *s_pReleaseCache = nullptr;

/// The value \p fCompute works out for \p Key, kept in \p Results while the
/// outermost question is answered. One reached again while it is worked out,
/// like a function that calls itself, adds nothing there; what reached one
/// being worked out above it isn't kept, as it depends on that one, but what
/// only reached itself or what is below it is, like a recursive helper,
/// which then is looked into once, however many call it.
template <typename t_CKey, typename t_FCompute>
static bool
cachedResult(llvm::DenseMap<t_CKey, bool> CReleaseCache::*pResults,
             llvm::DenseMap<t_CKey, unsigned> CReleaseCache::*pInProgress,
             const t_CKey &Key, t_FCompute &&fCompute) {
  std::optional<CReleaseCache> Owned;
  if (!s_pReleaseCache)
    s_pReleaseCache = &Owned.emplace();
  const llvm::scope_exit Release([&] {
    if (Owned)
      s_pReleaseCache = nullptr;
  });
  CReleaseCache &Cache = *s_pReleaseCache;
  if (auto It = (Cache.*pResults).find(Key); It != (Cache.*pResults).end())
    return It->second;
  auto Reach = [&](unsigned Least, unsigned Deepest) {
    Cache.Reached = std::min(Cache.Reached, Least);
    Cache.ReachedDeepest =
        Cache.AnyReached ? std::max(Cache.ReachedDeepest, Deepest) : Deepest;
    Cache.AnyReached = true;
  };
  const CReleaseCache::CProvisionalKey Provisional{&(Cache.*pResults),
                                                   provisionalKey(Key)};
  if (auto It = Cache.Provisional.find(Provisional);
      It != Cache.Provisional.end()) {
    Reach(It->second.Reached, It->second.ReachedDeepest);
    return It->second.Result;
  }
  constexpr unsigned None = std::numeric_limits<unsigned>::max();
  if (auto It = (Cache.*pInProgress).find(Key);
      It != (Cache.*pInProgress).end()) {
    Reach(It->second, It->second);
    return false;
  }
  const unsigned Depth = Cache.Depth++;
  (Cache.*pInProgress).try_emplace(Key, Depth);
  const unsigned ReachedBefore = Cache.Reached;
  const unsigned DeepestBefore = Cache.ReachedDeepest;
  const bool AnyBefore = Cache.AnyReached;
  Cache.Reached = None;
  Cache.AnyReached = false;
  const bool Result = fCompute();
  --Cache.Depth;
  (Cache.*pInProgress).erase(Key);
  // What depended on this one, and was kept while it was worked out, isn't
  // the same any more.
  if (auto It = Cache.ProvisionalOf.find(Depth);
      It != Cache.ProvisionalOf.end()) {
    for (const CReleaseCache::CProvisionalKey &Forgotten : It->second)
      Cache.Provisional.erase(Forgotten);
    Cache.ProvisionalOf.erase(It);
  }
  const bool Depends = Cache.AnyReached && Cache.Reached < Depth;
  if (!Depends)
    (Cache.*pResults).try_emplace(Key, Result);
  else {
    // The deepest one above that it depends on, which is still being looked
    // into: until that is worked out, asking again gives the same.
    const unsigned Deepest = std::min(Cache.ReachedDeepest, Depth - 1);
    Cache.Provisional.try_emplace(
        Provisional, CReleaseCache::CProvisional{Result, Cache.Reached, Deepest});
    Cache.ProvisionalOf[Deepest].push_back(Provisional);
  }
  const unsigned Reached = Depends ? Cache.Reached : None;
  const unsigned Deepest =
      Depends ? std::min(Cache.ReachedDeepest, Depth - 1) : 0;
  Cache.Reached = ReachedBefore;
  Cache.ReachedDeepest = DeepestBefore;
  Cache.AnyReached = AnyBefore;
  if (Depends)
    Reach(Reached, Deepest);
  return Result;
}

/// Whether \p E mentions \p Parameter, also through locals made from it.
static bool mentionsParameter(const Stmt *E, const ParmVarDecl *Parameter,
                              int Depth = 0) {
  // Not what is allocated anew, like a scratch copy, `new int(*_pData)`, also
  // by a function that can be looked into and only returns that, `fg_Copy(
  // _pData)` of `return new int(*_pData);`.
  if (!E || Depth > 4 || isa<LambdaExpr, CXXNewExpr>(E))
    return false;
  if (const auto *Call = dyn_cast<CallExpr>(E);
      Call && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Call))
    if (const FunctionDecl *Callee = Call->getDirectCallee()) {
      const FunctionDecl *Definition = Callee->getDefinition();
      bool Fresh = Definition && Definition->getBody() &&
                   Callee->getReturnType()->isPointerType();
      bool Any = false;
      std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
        if (!S || !Fresh || isa<LambdaExpr>(S))
          return;
        if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
          const Expr *Value = Return->getRetValue()
                                  ? Return->getRetValue()->IgnoreParenImpCasts()
                                  : nullptr;
          Any = true;
          Fresh =
              Value &&
              (isa<CXXNewExpr>(Value) ||
               Value->isNullPointerConstant(Definition->getASTContext(),
                                            Expr::NPC_ValueDependentIsNotNull));
          return;
        }
        for (const Stmt *Child : childrenOnce(S))
          Visit(Child);
      };
      if (Fresh)
        Visit(Definition->getBody());
      if (Fresh && Any)
        return false;
    }
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    // Taken by value and assigned on every path before, `_pData = new
    // int(*_pData)`, it is what it was assigned, not what it was given.
    if (Ref->getDecl() == Parameter) {
      if (Parameter->getType()->isReferenceType())
        return true;
      const Expr *Value = assignedValueAt(Parameter, Ref);
      if (!Value || Value == Parameter->getInit())
        return true;
      return mentionsParameter(Value, Parameter, Depth + 1);
    }
    const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
    if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local))
      return false;
    // What it was last assigned on every path, or its initializer, or else
    // any value it is assigned, like `pData = _pData`.
    if (const Expr *Value = assignedValueAt(Local, Ref))
      return mentionsParameter(Value, Parameter, Depth + 1);
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
    bool Mentions = Local->getInit() &&
                    mentionsParameter(Local->getInit(), Parameter, Depth + 1);
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || Mentions)
        return;
      if (const auto *Assign = dyn_cast<BinaryOperator>(S);
          Assign && Assign->getOpcode() == BO_Assign)
        if (const auto *Target = dyn_cast<DeclRefExpr>(
                Assign->getLHS()->IgnoreParenImpCasts());
            Target && Target->getDecl() == Local)
          Mentions = mentionsParameter(Assign->getRHS(), Parameter, Depth + 1);
      for (const Stmt *Child : childrenOnce(S))
        Visit(Child);
    };
    if (Function)
      Visit(Function->getBody());
    return Mentions;
  }
  return llvm::any_of(childrenOnce(E), [&](const Stmt *Child) {
    return mentionsParameter(Child, Parameter, Depth);
  });
}

/// Whether the function \p Callee releases what is given to its parameter at
/// \p Index, like `fg_Close(int _Handle) { close(_Handle); }`: deletes it,
/// or gives it on to what can release it, a function that can't be looked
/// into that takes it as a number, or by a pointer or reference that isn't to
/// const, or one that can be looked into that releases it.
static bool doesSomething(const Stmt *S, const ASTContext &Context);

/// Whether the body of \p Definition does something, like \c doesSomething.
static bool bodyDoesSomething(const FunctionDecl *Definition,
                              const ASTContext &Context) {
  return cachedResult(&CReleaseCache::Does, &CReleaseCache::DoesInProgress,
                      Definition, [&] {
                        return doesSomething(Definition->getBody(), Context);
                      });
}

/// Whether \p Argument designates storage of the object itself: the address of
/// a member, `&m_pData`, also of one of a member that is an object, or an
/// array member that decays to a pointer to its first element, `m_Cache`,
/// or, given to a reference, \p ByReference, the member, `m_pData`.
static bool isOwnStorage(const Expr *Argument, bool ByReference = false) {
  const Expr *E = Argument->IgnoreParens();
  if (ByReference)
    ;
  else if (const auto *Decay = dyn_cast<ImplicitCastExpr>(E);
           Decay && Decay->getCastKind() == CK_ArrayToPointerDecay)
    E = Decay->getSubExpr();
  else if (const auto *Address = dyn_cast<UnaryOperator>(E->IgnoreImpCasts());
           Address && Address->getOpcode() == UO_AddrOf)
    E = Address->getSubExpr();
  else
    return false;
  // Not through a reference member, `&m_nUsers` of `unsigned &m_nUsers`,
  // which refers to what isn't in it.
  const auto *Member = dyn_cast<MemberExpr>(E->IgnoreParenImpCasts());
  if (!Member || !isa<FieldDecl>(Member->getMemberDecl()) ||
      Member->getMemberDecl()->getType()->isReferenceType())
    return false;
  for (const Expr *Base = Member->getBase()->IgnoreParenImpCasts();;) {
    if (isa<CXXThisExpr>(Base))
      return true;
    // Also of a member of it, `m_State.m_Value`, which is `this->m_State`.
    const auto *Outer = dyn_cast<MemberExpr>(Base);
    if (!Outer ||
        (Outer->isArrow() &&
         !isa<CXXThisExpr>(Outer->getBase()->IgnoreParenImpCasts())) ||
        Outer->getMemberDecl()->getType()->isReferenceType())
      return false;
    Base = Outer->getBase()->IgnoreParenImpCasts();
  }
}

/// Whether the function \p Callee, which can be looked into, writes past what
/// its pointer parameter at \p Index points to, `**_ppData = 0` or
/// `_ppData[0][0] = 0`, or gives it on, which can. A reference parameter,
/// `int *&_pData`, is like a pointer to what it refers to: `*_pData = 0` is
/// past it.
static bool writesBeyondParameter(const FunctionDecl *Callee, unsigned Index) {
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody() ||
      Index >= Definition->getNumParams())
    return true;
  const ParmVarDecl *Parameter = Definition->getParamDecl(Index);
  const int Referred = Parameter->getType()->isReferenceType() ? 1 : 0;
  // How many times \p Target is dereferenced on the way to the parameter,
  // or -1 when it isn't of it.
  auto Levels = [&](const Expr *Target) {
    int Count = 0;
    for (int Step = 0; Target && Step < 16; ++Step) {
      Target = Target->IgnoreParenImpCasts();
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Target))
        return Ref->getDecl() == Parameter ? Count + Referred : -1;
      if (const auto *Deref = dyn_cast<UnaryOperator>(Target);
          Deref && Deref->getOpcode() == UO_Deref) {
        ++Count;
        Target = Deref->getSubExpr();
      } else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Target)) {
        ++Count;
        Target = Subscript->getBase();
      } else if (const auto *Member = dyn_cast<MemberExpr>(Target)) {
        Count += Member->isArrow() ? 1 : 0;
        Target = Member->getBase();
      } else
        return -1;
    }
    return -1;
  };
  bool Beyond = false;
  // Each lambda called once, `[&] { ... }()` or `fReset()`.
  llvm::SmallPtrSet<const LambdaExpr *, 4> Called;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    // Not what isn't evaluated, `sizeof(**_ppData = 0)`, nor a lambda that is
    // only made, but one that is called, where it is.
    if (!S || Beyond || isUnevaluatedNode(S))
      return;
    if (isa<LambdaExpr>(S))
      return;
    if (const auto *Call = dyn_cast<CallExpr>(S)) {
      const Expr *Callee = Call->getCallee();
      if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
          Operator && Operator->getOperator() == OO_Call &&
          Operator->getNumArgs() >= 1)
        Callee = Operator->getArg(0);
      if (const LambdaExpr *Lambda = calledLambda(Callee);
          Lambda && Called.insert(Lambda).second)
        Visit(Lambda->getBody());
      // And one given to what can call it, `fg_Invoke([&] { ... })`.
      for (const Expr *Argument : Call->arguments())
        if (const LambdaExpr *Lambda = calledLambda(Argument);
            Lambda && Called.insert(Lambda).second)
          Visit(Lambda->getBody());
    }
    // Not in a branch of an `if constexpr` that is discarded.
    if (const auto *If = dyn_cast<IfStmt>(S);
        If && If->isConstexpr() && !If->getCond()->isValueDependent())
      if (const std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Definition->getASTContext())) {
        Visit(If->getInit());
        Visit(If->getCond());
        Visit(*Case);
        return;
      }
    const Expr *Target = nullptr;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->isAssignmentOp())
      Target = Assign->getLHS();
    else if (const auto *Step = dyn_cast<UnaryOperator>(S);
             Step && Step->isIncrementDecrementOp())
      Target = Step->getSubExpr();
    if (Target && Levels(Target) >= 2)
      Beyond = true;
    // Given on, `fg_Other(_ppData)`, unless to a function that can be looked
    // into and doesn't write past it either, or deleted, `delete *_ppData`.
    if (const auto *Call = dyn_cast<CallExpr>(S))
      for (unsigned iArgument = 0; iArgument < Call->getNumArgs();
           ++iArgument) {
        const Expr *Argument = Call->getArg(iArgument);
        if (Levels(Argument) < 0 || Levels(Argument) >= 2 ||
            !Argument->getType()->isPointerType())
          continue;
        static thread_local int s_Depth = 0;
        const FunctionDecl *Given = Call->getDirectCallee();
        if (Levels(Argument) == Referred && Given &&
            !isa<CXXOperatorCallExpr, CXXMemberCallExpr>(Call) && s_Depth < 8) {
          ++s_Depth;
          const bool GivenBeyond = writesBeyondParameter(Given, iArgument);
          --s_Depth;
          if (!GivenBeyond)
            continue;
        }
        Beyond = true;
      }
    if (const auto *Delete = dyn_cast<CXXDeleteExpr>(S);
        Delete && Levels(Delete->getArgument()) >= 0)
      Beyond = true;
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Definition->getBody());
  return Beyond;
}

/// Whether the function \p Callee, which can be looked into, changes what its
/// parameter at \p Index points or refers to, `*_pValue = 0`, or gives it on,
/// unlike one that only checks it, `if (!_pValue) throw ...`.
static bool writesThroughParameterUncached(const FunctionDecl *Definition,
                                           unsigned Index, bool Object);

/// \p Object tells that the parameter is given the object itself, `this`,
/// whose own members changed through it, `_pView->m_pData = nullptr`, aren't
/// what it points to.
static bool writesThroughParameter(const FunctionDecl *Callee, unsigned Index,
                                   bool Object = false) {
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody() ||
      Index >= Definition->getNumParams())
    return true;
  return cachedResult(
      &CReleaseCache::Writes, &CReleaseCache::WritesInProgress,
      CReleaseCache::CFunctionKey{Definition, Index | (Object ? 1u << 31 : 0)},
      [&] { return writesThroughParameterUncached(Definition, Index, Object); });
}

static bool writesThroughParameterUncached(const FunctionDecl *Definition,
                                           unsigned Index, bool Object) {
  const ParmVarDecl *Parameter = Definition->getParamDecl(Index);
  // The object given, `_pView` or `*_pView`, also through an unchanged local
  // made from it, `CView *pView = _pView` or `CView &View = *_pView`.
  auto IsObject = [&](const Expr *E) {
    for (int Step = 0; E && Step < 8; ++Step) {
      E = E->IgnoreParenImpCasts();
      if (const auto *Deref = dyn_cast<UnaryOperator>(E);
          Deref && Deref->getOpcode() == UO_Deref) {
        E = Deref->getSubExpr();
        continue;
      }
      const auto *Ref = dyn_cast<DeclRefExpr>(E);
      if (!Ref)
        return false;
      if (Ref->getDecl() == Parameter)
        return true;
      const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
      if (!Local || !Local->hasLocalStorage() || !Local->getInit() ||
          !(Local->getType()->isPointerType() ||
            Local->getType()->isReferenceType()) ||
          (Local->getType()->isPointerType() &&
           isReassigned(Local, Definition->getBody())))
        return false;
      E = Local->getInit();
    }
    return false;
  };
  // Through it, not the parameter itself, `_pValue = nullptr`, nor a local
  // pointer made from it, `pLocal = nullptr` or `++pLocal`.
  auto Through = [&](const Expr *Target) {
    Target = Target->IgnoreParenImpCasts();
    // A member of the object given, `_pView->m_pData` or `(*_pView).m_pData`,
    // not what it points to.
    if (const auto *Member = dyn_cast<MemberExpr>(Target);
        Object && Member && isa<FieldDecl>(Member->getMemberDecl()) &&
        !Member->getMemberDecl()->getType()->isReferenceType() &&
        IsObject(Member->getBase()))
      return false;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Target)) {
      const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
      if (Ref->getDecl() == Parameter ||
          (Variable && Variable->hasLocalStorage()))
        return Variable && Variable->getType()->isReferenceType() &&
               mentionsParameter(Target, Parameter);
    }
    return mentionsParameter(Target, Parameter);
  };
  // Whether what is given as the argument at \p iArgument of \p Called, the
  // first \p Offset of which are the object, can be written through: unless it
  // takes it as const, `fg_Check(int const *_pValue)`, or only checks it in
  // turn.
  auto GivesOnWrite = [](const FunctionDecl *Called, unsigned iArgument,
                         unsigned Offset) {
    const QualType Type =
        Called && iArgument >= Offset &&
                iArgument - Offset < Called->getNumParams()
            ? Called->getParamDecl(iArgument - Offset)->getType()
            : QualType();
    if (!Type.isNull() && (Type->isPointerType() || Type->isReferenceType()) &&
        Type->getPointeeType().isConstQualified())
      return false;
    // A copy of a value read through it, `fg_IsPositive(*_pValue)` with
    // `bool fg_IsPositive(int _Value)`, can't write it.
    if (!Type.isNull() && Type->isScalarType() && !Type->isPointerType() &&
        !Type->isMemberPointerType())
      return false;
    if (!Type.isNull() && (Type->isPointerType() || Type->isReferenceType()) &&
        !writesThroughParameter(Called, iArgument - Offset))
      return false;
    return true;
  };
  bool Writes = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Writes)
      return;
    // Not what doesn't run: what isn't evaluated, like `noexcept(...)`, a
    // branch of an `if constexpr` that is discarded, or the body of a lambda
    // it only makes.
    if (isUnevaluatedNode(S))
      return;
    if (const auto *If = dyn_cast<IfStmt>(S);
        If && If->isConstexpr() && !If->getCond()->isValueDependent())
      if (const std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Definition->getASTContext())) {
        Visit(If->getInit());
        Visit(If->getCond());
        Visit(*Case);
        return;
      }
    if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
      for (const Expr *Init : Lambda->capture_inits())
        Visit(Init);
      return;
    }
    if (const auto *Binary = dyn_cast<BinaryOperator>(S);
        Binary && Binary->isAssignmentOp() && Through(Binary->getLHS()))
      Writes = true;
    else if (const auto *Unary = dyn_cast<UnaryOperator>(S);
             Unary && Unary->isIncrementDecrementOp() &&
             Through(Unary->getSubExpr()))
      Writes = true;
    else if (const auto *Delete = dyn_cast<CXXDeleteExpr>(S);
             Delete && mentionsParameter(Delete->getArgument(), Parameter))
      Writes = true;
    // An atomic operation other than a load through it, `__atomic_fetch_sub(
    // _pCount, 1u, __ATOMIC_RELEASE)`.
    else if (const auto *Atomic = dyn_cast<AtomicExpr>(S);
             Atomic && !isAtomicLoad(Atomic) &&
             mentionsParameter(Atomic->getPtr(), Parameter))
      Writes = true;
    // Given on, to what can do anything with it.
    else if (const auto *Call = dyn_cast<CallExpr>(S);
             Call && !isa<CXXMemberCallExpr>(Call)) {
      const FunctionDecl *Called = Call->getDirectCallee();
      const unsigned Offset =
          isa<CXXOperatorCallExpr>(Call) && hasImplicitObject(Called)
              ? 1
              : 0;
      for (unsigned iArgument = 0; iArgument < Call->getNumArgs() && !Writes;
           ++iArgument) {
        if (!mentionsParameter(Call->getArg(iArgument), Parameter))
          continue;
        if (GivesOnWrite(Called, iArgument, Offset))
          Writes = true;
      }
    }
    // Given on to a constructor the same way, of which a copy only reads it,
    // `CValue Copy(*_pValue)`.
    else if (const auto *Construct = dyn_cast<CXXConstructExpr>(S)) {
      for (unsigned iArgument = 0;
           iArgument < Construct->getNumArgs() && !Writes; ++iArgument)
        if (mentionsParameter(Construct->getArg(iArgument), Parameter) &&
            GivesOnWrite(Construct->getConstructor(), iArgument, 0))
          Writes = true;
    }
    // Also a member function that isn't const, unless it only gives access
    // to the object, `_pThis->fp_GetAsStorage()`, whose use tells, or is the
    // destructor of a class whose destruction releases nothing, `_Value.~T()`
    // of a trivially destroyed one, which no override takes the place of.
    else if (const auto *Member = dyn_cast<CXXMemberCallExpr>(S);
             Member && Member->getImplicitObjectArgument() &&
             mentionsParameter(Member->getImplicitObjectArgument(),
                               Parameter) &&
             (!Member->getMethodDecl() ||
              (!Member->getMethodDecl()->isConst() &&
               !onlyGivesAccess(Member->getMethodDecl()) &&
               !(isa<CXXDestructorDecl>(Member->getMethodDecl()) &&
                 !callsOverride(Member) &&
                 !releasesOnDestruction(
                     Member->getMethodDecl()->getParent())))))
      Writes = true;
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Definition->getBody());
  return Writes;
}

/// Whether \p Init makes an object with a constructor and a destructor that
/// can be seen, that keeps what \p Gives accepts of its arguments only in
/// members its destructor doesn't use, `CGuard Guard(m_pData, new int(0))`
/// with `: m_pObserved(_pObserved), m_pScratch(_pScratch)` and `~CGuard() {
/// delete m_pScratch; }`: it borrows them. Also an aggregate made with a
/// list, `CGuard Guard{m_pData, new int(0)}`, which keeps each in the member
/// it initializes.
static bool keptOnlyUnused(const Expr *Init,
                           llvm::function_ref<bool(const Expr *)> Gives) {
  // Also a temporary made by naming its type, `CGuard{...}`.
  Init = Init ? Init->IgnoreImplicit() : nullptr;
  if (const auto *Cast = dyn_cast_or_null<CXXFunctionalCastExpr>(Init))
    Init = Cast->getSubExpr()->IgnoreImplicit();
  const auto *Construct = Init ? dyn_cast<CXXConstructExpr>(Init) : nullptr;
  const auto *List =
      Init && !Construct ? dyn_cast<InitListExpr>(Init) : nullptr;
  const auto *Constructor =
      Construct ? dyn_cast_or_null<CXXConstructorDecl>(
                      Construct->getConstructor()->getDefinition())
                : nullptr;
  const CXXRecordDecl *Record = Constructor ? Constructor->getParent()
                                : List ? List->getType()->getAsCXXRecordDecl()
                                       : nullptr;
  const CXXDestructorDecl *Destructor =
      Record && Record->hasDefinition() ? Record->getDestructor() : nullptr;
  const FunctionDecl *DestructorDefinition =
      Destructor ? Destructor->getDefinition() : nullptr;
  if ((!List && (!Constructor || !Constructor->getBody())) ||
      !DestructorDefinition || !DestructorDefinition->getBody())
    return false;
  // Whether \p S refers to \p Decl.
  std::function<bool(const Stmt *, const ValueDecl *)> Mentions =
      [&](const Stmt *S, const ValueDecl *Decl) {
        if (!S)
          return false;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
            Ref && Ref->getDecl() == Decl)
          return true;
        if (const auto *Member = dyn_cast<MemberExpr>(S);
            Member && Member->getMemberDecl() == Decl)
          return true;
        return llvm::any_of(childrenOnce(S), [&](const Stmt *Child) {
          return Mentions(Child, Decl);
        });
      };
  // Whether the destructor uses \p Field, also in a member function of it
  // that it calls, `~CGuard() { f_Clear(); }`, whose body can be seen, each
  // once; one that can't be seen can.
  auto DestroyUses = [&](const FieldDecl *Field) {
    llvm::SmallPtrSet<const FunctionDecl *, 4> Helpers;
    std::function<bool(const Stmt *)> Uses = [&](const Stmt *S) {
      if (!S)
        return false;
      if (Mentions(S, Field))
        return true;
      bool Called = false;
      std::function<void(const Stmt *)> Find = [&](const Stmt *Inner) {
        if (!Inner || Called)
          return;
        if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Inner);
            Call && Call->getImplicitObjectArgument() &&
            isa<CXXThisExpr>(
                Call->getImplicitObjectArgument()->IgnoreParenImpCasts()))
          if (const CXXMethodDecl *Method = Call->getMethodDecl()) {
            const FunctionDecl *Helper = Method->getDefinition();
            if (!Helper || !Helper->getBody())
              Called = true;
            else if (Helpers.size() < 16 && Helpers.insert(Helper).second &&
                     Uses(Helper->getBody()))
              Called = true;
          }
        for (const Stmt *Child : childrenOnce(Inner))
          Find(Child);
      };
      Find(S);
      return Called;
    };
    return Uses(DestructorDefinition->getBody());
  };
  // Of a member that releases by itself when it is destroyed, `CRelease
  // m_Release`, after the destructor.
  auto ReleasesWith = [&](const FieldDecl *Field) {
    return !Field || DestroyUses(Field) ||
           releasesOnDestruction(Field->getType()
                                     ->getBaseElementTypeUnsafe()
                                     ->getAsCXXRecordDecl());
  };
  bool Any = false;
  if (List) {
    // The members in order, of an aggregate without bases.
    if (Record->getNumBases())
      return false;
    unsigned iInit = 0;
    for (const FieldDecl *Field : Record->fields()) {
      if (Field->isUnnamedBitField())
        continue;
      if (iInit >= List->getNumInits())
        break;
      if (Gives(List->getInit(iInit))) {
        Any = true;
        if (ReleasesWith(Field))
          return false;
      }
      ++iInit;
    }
    return Any;
  }
  for (unsigned iArgument = 0; iArgument < Construct->getNumArgs() &&
                               iArgument < Constructor->getNumParams();
       ++iArgument) {
    if (!Gives(Construct->getArg(iArgument)))
      continue;
    Any = true;
    const ParmVarDecl *Parameter = Constructor->getParamDecl(iArgument);
    if (Mentions(Constructor->getBody(), Parameter))
      return false;
    for (const CXXCtorInitializer *Initializer : Constructor->inits()) {
      if (!Mentions(Initializer->getInit(), Parameter))
        continue;
      if (ReleasesWith(Initializer->getMember()))
        return false;
    }
  }
  return Any;
}

static bool writesOwnObject(const CXXMethodDecl *Method, int Depth = 0);

static bool releasesParameterUncached(const FunctionDecl *Definition,
                                      unsigned Index, bool OwnState);

/// Whether \p Callee releases what it is given at \p Index. With
/// \p OwnState, of what is the object's own by value, `m_Text`, not what
/// is shared, where a const member function called on it that writes what
/// is mutable of it, `f_GetLen()` that caches the length, releases nothing.
static bool releasesParameter(const FunctionDecl *Callee, unsigned Index,
                              bool OwnState = false) {
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody() ||
      Index >= Definition->getNumParams())
    return false;
  return cachedResult(
      OwnState ? &CReleaseCache::ReleasesOfOwn : &CReleaseCache::Releases,
      OwnState ? &CReleaseCache::ReleasesOfOwnInProgress
               : &CReleaseCache::ReleasesInProgress,
      CReleaseCache::CFunctionKey{Definition, Index},
      [&] { return releasesParameterUncached(Definition, Index, OwnState); });
}

/// Whether \p E is what the object holds by value, `m_Text` or `m_Inner
/// .m_Text`, not what a pointer or reference member refers to.
static bool ownState(const Expr *E) {
  for (int Depth = 0; E && Depth < 16; ++Depth) {
    E = E->IgnoreParenImpCasts();
    const auto *Member = dyn_cast<MemberExpr>(E);
    if (!Member)
      return false;
    const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
    if (!Field || Field->getType()->isReferenceType() ||
        Field->getType()->isPointerType())
      return false;
    if (Member->isArrow())
      return isThis(Member->getBase());
    E = Member->getBase();
    if (isThis(E))
      return true;
  }
  return false;
}

/// Whether \p From releases what its object owns, `delete m_pData;`, also
/// given to a function that releases it, `fg_Free(m_pData)`.
static bool releasesOwn(const CXXMethodDecl *From) {
  const FunctionDecl *Definition = From->getDefinition();
  if (!Definition || !Definition->getBody())
    return false;
  bool Releases = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Releases || isa<LambdaExpr>(S) || isUnevaluatedNode(S))
      return;
    if (const auto *Delete = dyn_cast<CXXDeleteExpr>(S);
        Delete && ofObject(Delete->getArgument()))
      Releases = true;
    if (const auto *Released = dyn_cast<CallExpr>(S))
      if (const FunctionDecl *Callee = Released->getDirectCallee())
        for (unsigned iArgument = 0;
             !Releases && iArgument < Released->getNumArgs(); ++iArgument)
          Releases = ofObject(Released->getArg(iArgument)) &&
                     releasesParameter(Callee, iArgument);
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Definition->getBody());
  return Releases;
}

/// Whether \p Method is a const one that gives back an owner it makes, like
/// a weak pointer locked, `f_Lock()`, which takes what it gives, released by
/// the caller, not one that moves an owner out or releases what the object
/// owns anyway, `delete m_pData; return {};`.
static bool takesWhatItGives(const CXXMethodDecl *Method) {
  const CXXRecordDecl *Given = Method->getReturnType()->getAsCXXRecordDecl();
  return Method->isConst() && Given && Given->hasDefinition() &&
         !Given->hasTrivialDestructor() && !movesOwnMember(Method) &&
         !releasesOwn(Method);
}

static bool releasesParameterUncached(const FunctionDecl *Definition,
                                      unsigned Index, bool OwnState) {
  const ParmVarDecl *Parameter = Definition->getParamDecl(Index);
  std::function<bool(const Stmt *)> Releases = [&](const Stmt *S) {
    // Not what isn't evaluated, like `noexcept(delete _pData)`.
    if (!S || isa<LambdaExpr>(S) || isUnevaluatedNode(S))
      return false;
    if (const auto *Choose = dyn_cast<ChooseExpr>(S);
        Choose && !Choose->isConditionDependent())
      return Releases(Choose->getChosenSubExpr());
    // Only the branch of an `if constexpr` that isn't discarded runs.
    if (const auto *If = dyn_cast<IfStmt>(S);
        If && If->isConstexpr() && !If->getCond()->isValueDependent())
      if (const std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Definition->getASTContext()))
        return Releases(If->getInit()) || Releases(If->getCond()) ||
               Releases(*Case);
    // What a lambda it calls does, like `[_pData] { delete _pData; }()`, also
    // one held in a local, `fRelease()`.
    if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(S);
        Call && Call->getOperator() == OO_Call && Call->getNumArgs() >= 1) {
      // Also a copy of one, `auto fCopy = fRelease;`, or through a pointer
      // to one, `(*pRelease)()`.
      if (const LambdaExpr *Lambda = calledLambda(Call->getArg(0));
          Lambda && Releases(Lambda->getBody()))
        return true;
    }
    // Also called by name, `fRelease.operator()()`.
    if (const auto *Named = dyn_cast<CXXMemberCallExpr>(S);
        Named && Named->getMethodDecl() &&
        Named->getMethodDecl()->getOverloadedOperator() == OO_Call)
      if (const LambdaExpr *Lambda =
              calledLambda(Named->getImplicitObjectArgument());
          Lambda && Releases(Lambda->getBody()))
        return true;
    if (const auto *Delete = dyn_cast<CXXDeleteExpr>(S);
        Delete && mentionsParameter(Delete->getArgument(), Parameter))
      return true;
    // A local guard given it whose destructor releases, `CGuard
    // Guard{_pData}`, also a temporary one, but not a deep copy made of it,
    // `COwner Copy(*_pData)`, nor an owner a const member function of it
    // makes, like a weak pointer locked, `_pWeak.f_Lock()`, which holds a
    // reference of its own, as when it is called directly.
    auto GivesParameter = [&](const Expr *Init) {
      return gives(Init, [&](const Expr *Part) {
        if (const auto *Made =
                dyn_cast<CXXMemberCallExpr>(Part->IgnoreParenImpCasts());
            Made && Made->getMethodDecl() &&
            takesWhatItGives(Made->getMethodDecl()) &&
            llvm::none_of(Made->arguments(), [&](const Expr *Argument) {
              return mentionsParameter(Argument, Parameter);
            }))
          return false;
        return mentionsParameter(Part, Parameter);
      });
    };
    if (const auto *Declaration = dyn_cast<DeclStmt>(S))
      for (const Decl *D : Declaration->decls())
        if (const auto *Local = dyn_cast<VarDecl>(D);
            Local && Local->getInit() && !Local->getType()->isReferenceType() &&
            GivesParameter(Local->getInit()) &&
            releasesOnDestruction(Local->getType()
                                      ->getBaseElementTypeUnsafe()
                                      ->getAsCXXRecordDecl()) &&
            !keptOnlyUnused(Local->getInit(), GivesParameter))
          return true;
    // Also a temporary, unless it keeps it only where it doesn't release it.
    if (const auto *Temporary = dyn_cast<CXXBindTemporaryExpr>(S);
        Temporary && GivesParameter(Temporary->getSubExpr()) &&
        releasesOnDestruction(Temporary->getType()->getAsCXXRecordDecl()) &&
        !keptOnlyUnused(Temporary->getSubExpr(), GivesParameter))
      return true;
    // A member function called on it that releases what it owns, also a
    // const one, `_Resource.f_Release()`.
    if (const auto *Member = dyn_cast<CXXMemberCallExpr>(S);
        Member && Member->getImplicitObjectArgument() &&
        mentionsParameter(Member->getImplicitObjectArgument(), Parameter)) {
      const CXXMethodDecl *Method = Member->getMethodDecl();
      const FunctionDecl *MethodDefinition =
          Method ? Method->getDefinition() : nullptr;
      if (!Method)
        return true;
      // Destroying it, `_Value.~CValue()`, releases what its destruction
      // does, nothing of a class that is trivially destroyed, but a virtual
      // destructor can run that of what it is, which can release.
      if (isa<CXXDestructorDecl>(Method)) {
        if (callsOverride(Member) || releasesOnDestruction(Method->getParent()))
          return true;
      } else if (callsOverride(Member)) {
        // What an override of a virtual one does, `_pResource->f_Release()`,
        // can't be seen from the body of the one named, also of a const one,
        // which can release what is mutable, a count of users.
        return true;
      } else if (MethodDefinition && MethodDefinition->getBody()) {
        // Also a const one that writes what is mutable, a count of users,
        // `f_Decrease()` of a `TCIntrusiveRefCount`, but not one that gives
        // back an owner it makes, `_pWeak.f_Lock()`, which the caller
        // releases, like when called on what is owned directly.
        if (!takesWhatItGives(Method) &&
            (bodyDoesSomething(MethodDefinition, Definition->getASTContext()) ||
             (!OwnState && writesOwnObject(Method))))
          return true;
      } else if (!Method->isConst())
        return true;
    }
    if (const auto *Call = dyn_cast<CallExpr>(S)) {
      const FunctionDecl *Called = Call->getDirectCallee();
      const unsigned Offset =
          isa<CXXOperatorCallExpr>(Call) && hasImplicitObject(Called)
              ? 1
              : 0;
      for (unsigned iArgument = Offset; iArgument < Call->getNumArgs();
           ++iArgument) {
        if (!mentionsParameter(Call->getArg(iArgument), Parameter))
          continue;
        if (!Called || iArgument - Offset >= Called->getNumParams())
          return true;
        const FunctionDecl *CalledDefinition = Called->getDefinition();
        if (CalledDefinition && CalledDefinition->getBody()) {
          if (releasesParameter(Called, iArgument - Offset, OwnState))
            return true;
          continue;
        }
        const QualType Type = Called->getParamDecl(iArgument - Offset)->getType();
        if (Type->isBooleanType() || Type->isRealFloatingType())
          continue;
        // Not a number given to a member of an instantiation another file
        // makes, `f_SetStr(_From.f_GetStr(), _From.f_GetLen())` of `extern
        // template struct TCStr<...>`, whose instantiation there tells what
        // it does.
        if ((Type->isIntegralOrEnumerationType() &&
             !isExternInstantiation(Called)) ||
            ((Type->isPointerType() || Type->isReferenceType()) &&
             !Type->getPointeeType().isConstQualified()))
          return true;
      }
    }
    return llvm::any_of(childrenOnce(S), Releases);
  };
  return Releases(Definition->getBody());
}

/// What a call of a member function of an `atomic_ref` that changes what it
/// refers to, `std::atomic_ref<unsigned>(*m_pCount).fetch_sub(1)`, which are
/// const, all but the ones that read, is made of: `*m_pCount`.
static const Expr *changedThroughAtomicRef(const Stmt *S) {
  const auto *Change = dyn_cast<CXXMemberCallExpr>(S);
  if (!Change || !Change->getMethodDecl() ||
      isa<CXXConversionDecl>(Change->getMethodDecl()) ||
      (Change->getMethodDecl()->getIdentifier() &&
       llvm::is_contained(
           {StringRef("load"), StringRef("wait"), StringRef("is_lock_free")},
           Change->getMethodDecl()->getName())))
    return nullptr;
  const auto *Ref =
      dyn_cast<CXXConstructExpr>(Change->getImplicitObjectArgument()
                                     ->IgnoreUnlessSpelledInSource()
                                     ->IgnoreImplicit());
  if (!Ref || Ref->getNumArgs() < 1 || !Ref->getConstructor() ||
      Ref->getConstructor()->getParent()->getName() != "atomic_ref")
    return nullptr;
  return Ref->getArg(0);
}

/// Whether the body of \p Method writes its own object, `--m_nUsers`, also
/// through another member function of it that does, `f_Release()`, but not
/// in a branch of an `if constexpr` that is discarded.
static bool writesOwnObject(const CXXMethodDecl *Method, int Depth) {
  const FunctionDecl *Definition = Method ? Method->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody())
    return true;
  if (Depth > 8)
    return false;
  // Whether \p E is of the object, `this` or a member of it, also a member
  // of one, `m_Inner.m_nUsers`.
  auto OfOwn = [](const Expr *E) {
    for (int Step = 0; E && Step < 16; ++Step) {
      E = E->IgnoreParenImpCasts();
      if (isThis(E))
        return true;
      // Also through a local reference to it, `auto &nUsers = m_nUsers;`.
      if (const auto *Ref = dyn_cast<DeclRefExpr>(E))
        if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
            Local && Local->isLocalVarDecl() &&
            Local->getType()->isReferenceType() && Local->getInit()) {
          E = Local->getInit();
          continue;
        }
      // Also cast to what isn't const, `const_cast<t_CType &>(m_Value)`.
      if (const auto *Cast = dyn_cast<CXXConstCastExpr>(E)) {
        E = Cast->getSubExpr();
        continue;
      }
      if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E);
          Subscript &&
          Subscript->getBase()->IgnoreParenImpCasts()->getType()->isArrayType()) {
        E = Subscript->getBase();
        continue;
      }
      const auto *Member = dyn_cast<MemberExpr>(E);
      if (!Member || (Member->isArrow() && !isThis(Member->getBase())))
        return false;
      E = Member->getBase();
    }
    return false;
  };
  std::function<bool(const Stmt *)> Writes = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S) || isUnevaluatedNode(S))
      return false;
    if (const auto *Choose = dyn_cast<ChooseExpr>(S);
        Choose && !Choose->isConditionDependent())
      return Writes(Choose->getChosenSubExpr());
    if (const auto *If = dyn_cast<IfStmt>(S);
        If && If->isConstexpr() && !If->getCond()->isValueDependent())
      if (const std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Definition->getASTContext()))
        return Writes(If->getInit()) || Writes(If->getCond()) || Writes(*Case);
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->isAssignmentOp() && OfOwn(Assign->getLHS()))
      return true;
    if (const auto *Step = dyn_cast<UnaryOperator>(S);
        Step && Step->isIncrementDecrementOp() && OfOwn(Step->getSubExpr()))
      return true;
    // An atomic operation other than a load of it, `__atomic_fetch_sub(
    // &m_Value, 1, ...)`, also through an `atomic_ref` made of it, `std::
    // atomic_ref<t_CType>(m_Value).fetch_sub(_Value)` of `TCAtomic`.
    if (const auto *Atomic = dyn_cast<AtomicExpr>(S);
        Atomic && !isAtomicLoad(Atomic)) {
      const auto *Address =
          dyn_cast<UnaryOperator>(Atomic->getPtr()->IgnoreParenImpCasts());
      if (Address && Address->getOpcode() == UO_AddrOf &&
          OfOwn(Address->getSubExpr()))
        return true;
    }
    if (const Expr *Referred = changedThroughAtomicRef(S);
        Referred && OfOwn(Referred))
      return true;
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S);
        Operator && Operator->isAssignmentOp() && Operator->getNumArgs() >= 1 &&
        OfOwn(Operator->getArg(0)))
      return true;
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(S);
        Call && Call->getImplicitObjectArgument() &&
        OfOwn(Call->getImplicitObjectArgument())) {
      // A const one writes only what is mutable, which its body, when it can
      // be seen, tells, `fp_Decrease() const`.
      const CXXMethodDecl *Called = Call->getMethodDecl();
      if (!Called)
        return true;
      if (Called->isConst()) {
        const FunctionDecl *Seen = Called->getDefinition();
        if (Seen && Seen->getBody() && writesOwnObject(Called, Depth + 1))
          return true;
      } else if (Called->getParent() != Method->getParent() ||
                 writesOwnObject(Called, Depth + 1))
        return true;
    }
    return llvm::any_of(childrenOnce(S), Writes);
  };
  return Writes(Definition->getBody());
}

/// Whether \p S, of the body of a destructor, does something: not only empty
/// statements and the branches of an `if constexpr` that are discarded,
/// which an instantiation leaves empty.
/// Whether the destructor of \p Guard, when it can be seen, doesn't use its
/// member \p Assigned, `CGuard { int *m_pObserved; int *m_pScratch; ~CGuard()
/// { delete m_pScratch; } }`, also through the member functions of it that it
/// calls, `f_Clear()`, unless that member releases by itself.
static bool unusedByDestructor(const CXXRecordDecl *Guard,
                               const ValueDecl *Assigned) {
  const CXXDestructorDecl *Destructor =
      Guard ? Guard->getDestructor() : nullptr;
  const FunctionDecl *Definition =
      Destructor ? Destructor->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody() || !Assigned ||
      releasesOnDestruction(Assigned->getType()
                                ->getBaseElementTypeUnsafe()
                                ->getAsCXXRecordDecl()))
    return false;
  llvm::SmallPtrSet<const FunctionDecl *, 8> Followed;
  std::function<bool(const Stmt *)> Mentions = [&](const Stmt *Inner) {
    if (!Inner)
      return false;
    if (const auto *Member = dyn_cast<MemberExpr>(Inner);
        Member && Member->getMemberDecl() == Assigned)
      return true;
    // A member function of it, `f_Clear()`, uses it where it does; one that
    // can't be seen can.
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Inner);
        Call && Call->getImplicitObjectArgument() &&
        isa<CXXThisExpr>(
            Call->getImplicitObjectArgument()->IgnoreParenImpCasts())) {
      const CXXMethodDecl *Method = Call->getMethodDecl();
      const FunctionDecl *Called = Method ? Method->getDefinition() : nullptr;
      if (!Called || !Called->getBody() || Method->isVirtual())
        return true;
      if (Followed.size() < 64 && Followed.insert(Called).second &&
          Mentions(Called->getBody()))
        return true;
    }
    return llvm::any_of(childrenOnce(Inner), Mentions);
  };
  return !Mentions(Definition->getBody());
}

static bool doesSomething(const Stmt *S, const ASTContext &Context) {
  if (!S || isa<NullStmt>(S))
    return false;
  // What isn't evaluated, like `noexcept(fg_Delete(m_pData))`, `sizeof` or
  // `__builtin_constant_p(...)`, and of `__builtin_choose_expr`, the arm it
  // doesn't choose.
  if (isUnevaluatedNode(S))
    return false;
  if (const auto *Choose = dyn_cast<ChooseExpr>(S);
      Choose && !Choose->isConditionDependent())
    return doesSomething(Choose->getChosenSubExpr(), Context);
  if (const auto *Compound = dyn_cast<CompoundStmt>(S))
    return llvm::any_of(Compound->body(), [&](const Stmt *Child) {
      return doesSomething(Child, Context);
    });
  // Releases what it owns, `delete m_pData`, or uses the object, like
  // `m_Data.f_Clear()` or `fg_Release(m_pData)`; checking it, like `if
  // (!m_pData) fg_Fail()`, doesn't.
  if (const auto *Delete = dyn_cast<CXXDeleteExpr>(S))
    return ofObject(Delete->getArgument()) ||
           doesSomething(Delete->getArgument(), Context);
  // Changes what a pointer of it points to, like a count it shares,
  // `--*m_pCount` or `m_pState->m_nUsers = 0`, which a copy would change
  // again.
  // Not a pointer to a member of the object itself, `*ppData = nullptr` with
  // `auto ppData = &m_pData`, which changes only the object, like writing the
  // member.
  auto ToOwnMember = [](const Expr *Pointer) {
    Pointer = Pointer->IgnoreParenImpCasts();
    for (int Depth = 0; Depth < 8; ++Depth) {
      // Stepped, `--pEnd`, also by an amount, `pEnd -= 1`, which it still
      // points into.
      if (const auto *Step = dyn_cast<UnaryOperator>(Pointer);
          Step && Step->isIncrementDecrementOp()) {
        Pointer = Step->getSubExpr()->IgnoreParenImpCasts();
        continue;
      }
      if (const auto *Step = dyn_cast<CompoundAssignOperator>(Pointer);
          Step &&
          (Step->getOpcode() == BO_AddAssign ||
           Step->getOpcode() == BO_SubAssign) &&
          Step->getType()->isPointerType()) {
        Pointer = Step->getLHS()->IgnoreParenImpCasts();
        continue;
      }
      // Moved along it, `m_Data + 1`.
      if (const auto *Offset = dyn_cast<BinaryOperator>(Pointer);
          Offset && Offset->isAdditiveOp() &&
          Offset->getType()->isPointerType()) {
        Pointer = (Offset->getLHS()->getType()->isPointerType() ||
                           Offset->getLHS()->getType()->isArrayType()
                       ? Offset->getLHS()
                       : Offset->getRHS())
                      ->IgnoreParenImpCasts();
        continue;
      }
      // Cast to another pointer, `reinterpret_cast<uint8 *>(&m_Value)`.
      if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Pointer);
          Cast && Cast->getType()->isPointerType() &&
          Cast->getSubExpr()->getType()->isPointerType()) {
        Pointer = Cast->getSubExpr()->IgnoreParenImpCasts();
        continue;
      }
      // What a local holds where it is used, `pCount = m_pCount`, not only
      // what it is made from, but the iterator of a range loop, `for (int
      // &Value : m_Data)`, which it only steps along the range, is.
      const auto *Ref = dyn_cast<DeclRefExpr>(Pointer);
      const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
          !Local->getInit())
        break;
      const Expr *Value =
          Local->getType()->isReferenceType() || Local->isImplicit()
              ? Local->getInit()
              : assignedValueAt(Local, Ref);
      if (!Value)
        return false;
      Pointer = Value->IgnoreParenImpCasts();
    }
    // Its address, `&m_pData`, or an array member it decays from, `int
    // *pCache = m_Cache`.
    const Expr *Object = nullptr;
    if (const auto *Address = dyn_cast<UnaryOperator>(Pointer);
        Address && Address->getOpcode() == UO_AddrOf)
      Object = Address->getSubExpr()->IgnoreParenImpCasts();
    else if (Pointer->getType()->isArrayType())
      Object = Pointer;
    if (!Object)
      return false;
    // Also of an element of an array member, `&m_Cache[0]`.
    for (int Depth = 0; Depth < 64; ++Depth) {
      if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Object);
          Subscript &&
          Subscript->getBase()->IgnoreParenImpCasts()->getType()->isArrayType())
        Object = Subscript->getBase()->IgnoreParenImpCasts();
      else if (const auto *Member = dyn_cast<MemberExpr>(Object)) {
        // Not through a reference member, `&m_nUsers` of `unsigned
        // &m_nUsers`, which refers to what isn't in it.
        if (Member->getMemberDecl()->getType()->isReferenceType())
          return false;
        if (Member->isArrow())
          return isThis(Member->getBase());
        Object = Member->getBase()->IgnoreParenImpCasts();
      } else
        break;
    }
    return isThis(Object);
  };
  std::function<bool(const Expr *)> ThroughPointer = [&](const Expr *Target) {
    // An element of an array that is a member, `m_Cache[0] = 0`, is in the
    // object, unless the array is in what a member points to.
    if (const auto *Subscript =
            dyn_cast<ArraySubscriptExpr>(Target->IgnoreParenImpCasts()))
      if (const auto *Decay =
              dyn_cast<ImplicitCastExpr>(Subscript->getBase()->IgnoreParens());
          Decay && Decay->getCastKind() == CK_ArrayToPointerDecay)
        return ThroughPointer(Decay->getSubExpr());
    Target = Target->IgnoreParenImpCasts();
    // Also through a local reference to it, `auto &Count = *m_pCount;
    // --Count;`.
    for (int Depth = 0; Depth < 8; ++Depth) {
      const auto *Ref = dyn_cast<DeclRefExpr>(Target);
      const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
          !Local->getType()->isReferenceType() || !Local->getInit())
        break;
      Target = Local->getInit()->IgnoreParenImpCasts();
    }
    // Not through a pointer to a local, `*ppCopy = nullptr` with `int **ppCopy
    // = &pCopy`, which writes that local, also through copies of it.
    auto ToLocal = [](const Expr *Pointer) {
      for (int Depth = 0; Pointer && Depth < 8; ++Depth) {
        Pointer = Pointer->IgnoreParenImpCasts();
        if (const auto *Address = dyn_cast<UnaryOperator>(Pointer);
            Address && Address->getOpcode() == UO_AddrOf) {
          const auto *Ref = dyn_cast<DeclRefExpr>(
              Address->getSubExpr()->IgnoreParenImpCasts());
          const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          return Local && Local->hasLocalStorage() &&
                 !Local->getType()->isReferenceType();
        }
        const auto *Ref = dyn_cast<DeclRefExpr>(Pointer);
        const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
            !Local->getType()->isPointerType())
          return false;
        Pointer = assignedValueAt(Local, Ref);
      }
      return false;
    };
    if (const auto *Deref = dyn_cast<UnaryOperator>(Target);
        Deref && Deref->getOpcode() == UO_Deref)
      return !ToLocal(Deref->getSubExpr()) && ofObject(Deref->getSubExpr()) &&
             !ToOwnMember(Deref->getSubExpr());
    // Not through `this`, also a local pointer to it, `auto pSelf = this;
    // pSelf->m_pBorrowed = nullptr`, which writes a member of the object.
    auto IsThis = [](const Expr *Base) {
      for (int Depth = 0; Base && Depth < 8; ++Depth) {
        Base = Base->IgnoreParenImpCasts();
        if (isThis(Base))
          return true;
        const auto *Ref = dyn_cast<DeclRefExpr>(Base);
        const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
            !Local->getType()->isPointerType())
          return false;
        Base = assignedValueAt(Local, Ref);
      }
      return false;
    };
    // Whether what \p Method, a getter of the object, or of a member it
    // holds, \p Object, gives a reference to is through a pointer of that,
    // `--m_Count.f_Get()` of `TCReference<umint> m_Count` with `return
    // *m_pObject;`, which a copy shares.
    auto GivesThroughPointer = [&](const FunctionDecl *Method,
                                   const Expr *Object) {
      for (int Depth = 0; Object && Depth < 8; ++Depth) {
        Object = Object->IgnoreParenImpCasts();
        const auto *Member = dyn_cast<MemberExpr>(Object);
        if (!Member || !isa<FieldDecl>(Member->getMemberDecl()) ||
            Member->getMemberDecl()->getType()->isReferenceType())
          break;
        if (Member->isArrow()) {
          Object = IsThis(Member->getBase()) ? Member->getBase() : nullptr;
          break;
        }
        Object = Member->getBase();
      }
      if (!Object || !IsThis(Object))
        return false;
      const FunctionDecl *Definition =
          Method ? Method->getDefinition() : nullptr;
      static thread_local int s_Depth = 0;
      if (!Definition || !Definition->getBody() || s_Depth >= 4)
        return false;
      ++s_Depth;
      const llvm::scope_exit Restore([] { --s_Depth; });
      bool Through = false;
      std::function<void(const Stmt *)> Visit = [&](const Stmt *Inner) {
        if (!Inner || Through || isa<LambdaExpr>(Inner))
          return;
        if (const auto *Return = dyn_cast<ReturnStmt>(Inner);
            Return && Return->getRetValue() &&
            ThroughPointer(Return->getRetValue())) {
          Through = true;
          return;
        }
        for (const Stmt *Child : childrenOnce(Inner))
          Visit(Child);
      };
      Visit(Definition->getBody());
      return Through;
    };
    // Also an element that an overloaded `operator[]` gives a reference to,
    // `--(*m_pUsers)[m_Index]`, or what a member function does, of what it
    // is reached through, also what an overloaded `operator*` does, `--
    // *m_pCount` of a `TCPointer<umint>`.
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Target);
        Operator && Operator->getOperator() == OO_Subscript &&
        Operator->getNumArgs() >= 1 && Operator->isLValue())
      return ThroughPointer(Operator->getArg(0));
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Target);
        Operator && Operator->getOperator() == OO_Star &&
        Operator->getNumArgs() == 1 && Operator->isLValue())
      return ThroughPointer(Operator->getArg(0)) ||
             GivesThroughPointer(Operator->getDirectCallee(),
                                 Operator->getArg(0));
    if (const auto *Getter = dyn_cast<CXXMemberCallExpr>(Target);
        Getter && Getter->isLValue() && Getter->getImplicitObjectArgument()) {
      const auto *Callee =
          dyn_cast<MemberExpr>(Getter->getCallee()->IgnoreParens());
      if (Callee && Callee->isArrow() && !IsThis(Callee->getBase()))
        return ofObject(Callee->getBase()) && !ToOwnMember(Callee->getBase());
      if (ThroughPointer(Getter->getImplicitObjectArgument()))
        return true;
      return GivesThroughPointer(Getter->getDirectCallee(),
                                 Getter->getImplicitObjectArgument());
    }
    // Nor through a pointer to a member of its own, `auto pState = &m_State;
    // pState->m_Value = 0`, which writes the object.
    if (const auto *Member = dyn_cast<MemberExpr>(Target);
        Member && Member->isArrow() && !IsThis(Member->getBase()))
      return ofObject(Member->getBase()) && !ToOwnMember(Member->getBase());
    // Also an element, `--m_pCount[0]`, but not of a pointer to a member of
    // the object itself, `ppData[0] = nullptr`.
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Target);
        Subscript && Subscript->getBase()->getType()->isPointerType())
      return ofObject(Subscript->getBase()) &&
             !ToOwnMember(Subscript->getBase());
    // Or what a reference member refers to, `--m_nUsers` of `unsigned
    // &m_nUsers`, also a member of it, `--m_State.m_nUsers` of `CState
    // &m_State`.
    // Also a member of what a local reference is bound to, `Entry.m_RefCount`
    // of `auto &Entry = *mp_pEntry`, which is a member of that.
    for (const Expr *Part = Target; Part;) {
      const auto *Member = dyn_cast<MemberExpr>(Part->IgnoreParenImpCasts());
      if (!Member) {
        const auto *Ref = dyn_cast<DeclRefExpr>(Part->IgnoreParenImpCasts());
        const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        return Part != Target && Local && Local->hasLocalStorage() &&
               !isa<ParmVarDecl>(Local) &&
               Local->getType()->isReferenceType() && Local->getInit() &&
               ThroughPointer(Local->getInit());
      }
      if (!isa<FieldDecl>(Member->getMemberDecl()))
        break;
      if (Member->getMemberDecl()->getType()->isReferenceType())
        return ofObject(Member->getBase());
      // Also a member of a member of what a pointer of it points to,
      // `--m_pState->m_References.m_nUsers`.
      if (Member->isArrow())
        return Part != Target && !IsThis(Member->getBase()) &&
               ofObject(Member->getBase()) && !ToOwnMember(Member->getBase());
      Part = Member->getBase();
    }
    return false;
  };
  if (const auto *Unary = dyn_cast<UnaryOperator>(S);
      Unary && Unary->isIncrementDecrementOp() &&
      ThroughPointer(Unary->getSubExpr()))
    return true;
  if (const auto *Binary = dyn_cast<BinaryOperator>(S);
      Binary && Binary->isAssignmentOp() && ThroughPointer(Binary->getLHS()))
    return true;
  // An atomic operation other than a load, `__atomic_fetch_sub(m_pCount, 1,
  // ...)`, changes what it is given a pointer to.
  if (const auto *Atomic = dyn_cast<AtomicExpr>(S);
      Atomic && !isAtomicLoad(Atomic) && ofObject(Atomic->getPtr()) &&
      !ToOwnMember(Atomic->getPtr()))
    return true;
  // And a member function that changes an `atomic_ref` made of it,
  // `std::atomic_ref<unsigned>(*m_pCount).fetch_sub(1)`, which are const,
  // since they change what it refers to: all but the ones that read.
  if (const Expr *Referred = changedThroughAtomicRef(S);
      Referred && ThroughPointer(Referred))
    return true;
  // A local made from it whose destructor releases, `CReleaseGuard
  // Guard{m_pData}`, also through a local made from it, `CGuard
  // Guard{pData}` with `int *pData = m_pData`, but not from what is
  // allocated anew, `CGuard Scratch{new int(*m_pData)}`, nor one the function
  // gives back, which isn't destroyed there, like the shared pointer `f_Lock()`
  // makes.
  if (const auto *Declaration = dyn_cast<DeclStmt>(S))
    for (const Decl *D : Declaration->decls())
      if (const auto *Local = dyn_cast<VarDecl>(D);
          Local && Local->getInit() && !Local->getType()->isReferenceType() &&
          !Local->isNRVOVariable() && givesObject(Local->getInit()) &&
          releasesOnDestruction(Local->getType()
                                    ->getBaseElementTypeUnsafe()
                                    ->getAsCXXRecordDecl()) &&
          !keptOnlyUnused(Local->getInit(), givesObject))
        return true;
  // Or given it after it is made, `CGuard Guard; Guard.m_pData = m_pData;`.
  if (const auto *Assign = dyn_cast<BinaryOperator>(S);
      Assign && Assign->getOpcode() == BO_Assign &&
      givesObject(Assign->getRHS())) {
    const Expr *Base = Assign->getLHS()->IgnoreParenImpCasts();
    bool ThroughMember = false;
    // The member of the local assigned, `m_pObserved` of `Guard.m_pObserved`.
    const ValueDecl *Assigned = nullptr;
    while (const auto *Member = dyn_cast<MemberExpr>(Base)) {
      if (Member->isArrow())
        break;
      ThroughMember = true;
      Assigned = Member->getMemberDecl();
      Base = Member->getBase()->IgnoreParenImpCasts();
    }
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Base); ThroughMember && Ref)
      if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
          Local && Local->hasLocalStorage() &&
          !Local->getType()->isReferenceType()) {
        const CXXRecordDecl *Guard =
            Local->getType()->getBaseElementTypeUnsafe()->getAsCXXRecordDecl();
        if (releasesOnDestruction(Guard) &&
            !unusedByDestructor(Guard, Assigned))
          return true;
      }
  }
  // Also through a member function of the local that keeps it in a member,
  // `Guard.f_Set(m_pData)` with `m_pData = _pData;`.
  if (const auto *Setter = dyn_cast<CXXMemberCallExpr>(S)) {
    const auto *Ref = dyn_cast_or_null<DeclRefExpr>(
        Setter->getImplicitObjectArgument()
            ? Setter->getImplicitObjectArgument()->IgnoreParenImpCasts()
            : nullptr);
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    const CXXMethodDecl *Method = Setter->getMethodDecl();
    const FunctionDecl *Definition = Method ? Method->getDefinition() : nullptr;
    if (Local && Local->hasLocalStorage() &&
        !Local->getType()->isReferenceType() && Definition &&
        Definition->getBody()) {
      const CXXRecordDecl *Guard =
          Local->getType()->getBaseElementTypeUnsafe()->getAsCXXRecordDecl();
      for (unsigned iArgument = 0; iArgument < Setter->getNumArgs() &&
                                   iArgument < Definition->getNumParams();
           ++iArgument) {
        if (!givesObject(Setter->getArg(iArgument)))
          continue;
        const ParmVarDecl *Parameter = Definition->getParamDecl(iArgument);
        bool Kept = false;
        std::function<void(const Stmt *)> Visit = [&](const Stmt *Inner) {
          if (!Inner || Kept || isa<LambdaExpr>(Inner))
            return;
          if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
              Assign && Assign->getOpcode() == BO_Assign &&
              mentionsParameter(Assign->getRHS(), Parameter))
            if (const auto *Member = dyn_cast<MemberExpr>(
                    Assign->getLHS()->IgnoreParenImpCasts());
                Member &&
                isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()))
              Kept = releasesOnDestruction(Guard) &&
                     !unusedByDestructor(Guard, Member->getMemberDecl());
          for (const Stmt *Child : childrenOnce(Inner))
            Visit(Child);
        };
        Visit(Definition->getBody());
        if (Kept)
          return true;
      }
    }
  }
  // And a temporary, `CReleaseGuard{m_pData};`, destroyed at the end of its
  // full-expression.
  // Not one that keeps what it is given only in members its destructor
  // doesn't use, `(void)CGuard{m_pData, new int(1)}`, like a local.
  if (const auto *Temporary = dyn_cast<CXXBindTemporaryExpr>(S);
      Temporary && givesObject(Temporary->getSubExpr()) &&
      releasesOnDestruction(Temporary->getType()->getAsCXXRecordDecl()) &&
      !keptOnlyUnused(Temporary->getSubExpr(), givesObject))
    return true;
  // A constructor that releases what it is given, `CDelete Delete(m_pData)`
  // with `CDelete(int *_pData) { delete _pData; }`, not a copy of what is
  // const, `TCSharedPointer pSnapshot(*m_pPointer)`, which takes a reference
  // of its own, which it releases itself, like a temporary copy.
  auto CopiesConst = [](const CXXConstructorDecl *Constructor) {
    unsigned Written = 0;
    return Constructor->isCopyConstructor(Written) &&
           (Written & Qualifiers::Const);
  };
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(S);
      Construct && !CopiesConst(Construct->getConstructor()))
    for (unsigned iArgument = 0; iArgument < Construct->getNumArgs();
         ++iArgument)
      if (givesObject(Construct->getArg(iArgument)) &&
          releasesParameter(Construct->getConstructor(), iArgument,
                            ownState(Construct->getArg(iArgument))))
        return true;
  // Making a lambda doesn't run it, only its captures, `[pData =
  // fg_Take(m_pData)] {}`.
  if (const auto *Lambda = dyn_cast<LambdaExpr>(S))
    return llvm::any_of(Lambda->capture_inits(), [&](const Expr *Init) {
      return doesSomething(Init, Context);
    });
  if (const auto *Call = dyn_cast<CallExpr>(S)) {
    // What a function does that can't change it, a const member function
    // like `f_Check() const`, or one given it as const, like
    // `fg_IsValid(int const *_pData)`: only reading it, unless its body,
    // when it can be seen, releases it anyway, `delete m_pData`.
    auto ReleasesAnyway = [&](const FunctionDecl *Function) {
      const FunctionDecl *Definition =
          Function ? Function->getDefinition() : nullptr;
      return Definition && Definition->getBody() &&
             bodyDoesSomething(Definition, Context);
    };
    // Whether what a function does can be looked into.
    auto Visible = [&](const FunctionDecl *Function) {
      const FunctionDecl *Definition =
          Function ? Function->getDefinition() : nullptr;
      return Definition && Definition->getBody();
    };
    // A lambda called right away, `[this] { delete m_pData; }()`, does what
    // its body does.
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
        Operator && Operator->getOperator() == OO_Call &&
        Operator->getNumArgs() >= 1) {
      // Also one held in a local, `auto fRelease = [this] { ... };
      // fRelease();`, a copy of one, `auto fCopy = fRelease`, or through a
      // pointer to one, `(*pRelease)()`.
      // What it is given, `[](int *_pData) { delete _pData; }(m_pData)`, is
      // looked at like for any function that is called.
      if (const LambdaExpr *Lambda = calledLambda(Operator->getArg(0));
          Lambda && (doesSomething(Lambda, Context) ||
                     doesSomething(Lambda->getBody(), Context)))
        return true;
    }
    // Also its call operator called by name, `fRelease.operator()()`.
    if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
        Member && Member->getMethodDecl() &&
        Member->getMethodDecl()->getOverloadedOperator() == OO_Call)
      if (const LambdaExpr *Lambda =
              calledLambda(Member->getImplicitObjectArgument());
          Lambda && (doesSomething(Lambda, Context) ||
                     doesSomething(Lambda->getBody(), Context)))
        return true;
    // A function object made from it and called, `CDelete{m_pData}()`, also
    // through a local, does what its call operator does to its own object.
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
        Operator && Operator->getOperator() == OO_Call &&
        Operator->getNumArgs() >= 1 && !ofObject(Operator->getArg(0))) {
      const Expr *Made = Operator->getArg(0)->IgnoreImplicit();
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Made->IgnoreParenImpCasts()))
        if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
            Local && Local->hasLocalStorage() && Local->getInit())
          Made = Local->getInit();
      if (const auto *Method =
              dyn_cast_or_null<CXXMethodDecl>(Operator->getDirectCallee());
          Method && givesObject(Made) && ReleasesAnyway(Method))
        return true;
    }
    // Destroying what is in the object explicitly,
    // `reinterpret_cast<CType *>(m_Data)->~CType()`, releases what that owns,
    // and destroying a scalar, `m_Value.~CInt()`, nothing.
    if (const auto *Pseudo = dyn_cast<CXXPseudoDestructorExpr>(
            Call->getCallee()->IgnoreParenImpCasts());
        Pseudo && ofObject(Pseudo->getBase()))
      if (const CXXRecordDecl *Record =
              Pseudo->getDestroyedType().isNull()
                  ? nullptr
                  : Pseudo->getDestroyedType()->getAsCXXRecordDecl();
          Record && releasesOnDestruction(Record))
        return true;
    // Also a virtual one that isn't final, `m_pResource->~ICResource()`,
    // which runs the destructor of what it is, which can release.
    if (const auto *Destroy = dyn_cast<CXXMemberCallExpr>(Call);
        Destroy &&
        isa_and_nonnull<CXXDestructorDecl>(Destroy->getMethodDecl()) &&
        Destroy->getImplicitObjectArgument() &&
        ofObject(Destroy->getImplicitObjectArgument())) {
      const CXXMethodDecl *Destructor = Destroy->getMethodDecl();
      const auto *Named =
          dyn_cast<MemberExpr>(Destroy->getCallee()->IgnoreParens());
      const bool Dispatched = Destructor->isVirtual() &&
                              !Destructor->hasAttr<FinalAttr>() &&
                              !Destructor->getParent()->hasAttr<FinalAttr>() &&
                              !(Named && Named->hasQualifier());
      if (Dispatched || releasesOnDestruction(Destructor->getParent()))
        return true;
    }
    // A function pointer of it called, `mp_fOnExit()`, can release anything.
    if (!Call->getDirectCallee() && !isa<CXXMemberCallExpr>(Call) &&
        !isa<CXXOperatorCallExpr>(Call) && ofObject(Call->getCallee()))
      return true;
    // Not a destructor, which releases what destroying its class does, as
    // above.
    if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
        Member && ofObject(Member->getImplicitObjectArgument()) &&
        !isa_and_nonnull<CXXDestructorDecl>(Member->getMethodDecl())) {
      // One that isn't const releases unless what it does can be seen not
      // to, like a check, `f_Check()`, not what an override of a virtual one
      // does, `m_pResource->f_Release()`.
      // Also one named through a pointer to it, `(this->*&CView::f_Read)()`.
      const CXXMethodDecl *Method = Member->getMethodDecl();
      if (const auto *Pointer =
              dyn_cast<BinaryOperator>(Member->getCallee()->IgnoreParens());
          !Method && Pointer && Pointer->isPtrMemOp())
        if (const auto *Address = dyn_cast<UnaryOperator>(
                Pointer->getRHS()->IgnoreParenImpCasts());
            Address && Address->getOpcode() == UO_AddrOf)
          if (const auto *Named =
                  dyn_cast<DeclRefExpr>(Address->getSubExpr()->IgnoreParens()))
            Method = dyn_cast<CXXMethodDecl>(Named->getDecl());
      // A const one that gives back an owner it makes, like a weak pointer
      // locked, `m_pWeak->f_Lock()`, takes what it gives, which the caller
      // releases.
      const bool TakesWhatItGives = Method && takesWhatItGives(Method);
      // One that moves an owner out gives what the object held, which then
      // goes, also from what a member points to.
      // Also a const one an override of a virtual one can be, which can
      // release what is mutable, `f_Release() const` of an interface whose
      // implementation decreases a count, whatever the one called does.
      if (!Method || callsOverride(Member) ||
          (!Method->isConst() && !Visible(Method)) ||
          (!TakesWhatItGives &&
           (ReleasesAnyway(Method) || movesOwnMember(Method))))
        return true;
      // Of what a member points or refers to, `m_pCounter->f_Release()`,
      // which a copy shares, writing its object changes what is shared,
      // like `--m_pCounter->m_nUsers`, also a const one that writes what is
      // mutable, `m_pCount->f_Decrease()` of a `TCIntrusiveRefCount`, unless
      // it gives back an owner it makes, as above.
      // Also a member of what it points to, `m_pResource->m_RefCount
      // .f_Decrease()` of a count in the resource, and what it points to
      // dereferenced, `(*m_pCount).f_Decrease()`, also through a local
      // reference to it, `auto &Count = *m_pCount; Count.f_Decrease();`.
      const auto *Callee =
          dyn_cast<MemberExpr>(Member->getCallee()->IgnoreParens());
      bool Shared = false;
      if (Callee) {
        const Expr *Object = Callee->getBase()->IgnoreParenImpCasts();
        Shared = Callee->isArrow() && !isThis(Object);
        for (int Level = 0; !Shared && Level < 8; ++Level) {
          if (const auto *Ref = dyn_cast<DeclRefExpr>(Object))
            if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
                Local && Local->isLocalVarDecl() &&
                Local->getType()->isReferenceType() && Local->getInit()) {
              Object = Local->getInit()->IgnoreParenImpCasts();
              continue;
            }
          if (const auto *Deref = dyn_cast<UnaryOperator>(Object);
              Deref && Deref->getOpcode() == UO_Deref) {
            Shared =
                !isa<CXXThisExpr>(Deref->getSubExpr()->IgnoreParenImpCasts());
            break;
          }
          const auto *Field = dyn_cast<MemberExpr>(Object);
          if (!Field)
            break;
          const Expr *Base = Field->getBase()->IgnoreParenImpCasts();
          Shared = Field->getMemberDecl()->getType()->isReferenceType() ||
                   (Field->isArrow() && !isa<CXXThisExpr>(Base));
          Object = Base;
        }
      }
      if (Shared && (!Method->isConst() || !TakesWhatItGives) &&
          (writesOwnObject(Method) || movesOwnMember(Method)))
        return true;
    }
    // Given to what can change or release it, not to a read only parameter,
    // like `fg_IsValid(int const *_pData)`, also through an unchanged local
    // pointer to it, `auto fObserve = &fg_Observe; fObserve(m_pValue)`.
    const FunctionDecl *Callee = Call->getDirectCallee();
    if (!Callee)
      if (const auto *Ref =
              dyn_cast<DeclRefExpr>(Call->getCallee()->IgnoreParenImpCasts()))
        if (const auto *Pointer = dyn_cast<VarDecl>(Ref->getDecl());
            Pointer && Pointer->hasLocalStorage() &&
            !isa<ParmVarDecl>(Pointer) && Pointer->getInit() &&
            Pointer->getType()->isFunctionPointerType() &&
            isa_and_nonnull<FunctionDecl>(
                Pointer->getParentFunctionOrMethod()) &&
            !isReassigned(Pointer, cast<FunctionDecl>(
                                       Pointer->getParentFunctionOrMethod())
                                       ->getBody())) {
          const Expr *Init = Pointer->getInit()->IgnoreParenImpCasts();
          if (const auto *Address = dyn_cast<UnaryOperator>(Init);
              Address && Address->getOpcode() == UO_AddrOf)
            Init = Address->getSubExpr()->IgnoreParenImpCasts();
          if (const auto *Named = dyn_cast<DeclRefExpr>(Init))
            Callee = dyn_cast<FunctionDecl>(Named->getDecl());
        }
    for (unsigned iArgument = 0; iArgument < Call->getNumArgs(); ++iArgument) {
      // A lambda given to what can call it, `fg_Invoke([this] { delete
      // m_pData; })`, does what its body does: not to a function whose body
      // can be seen not to use it, `fg_Ignore(_fCallback)`.
      if (const LambdaExpr *Given = calledLambda(Call->getArg(iArgument));
          Given && (doesSomething(Given, Context) ||
                    doesSomething(Given->getBody(), Context))) {
        const FunctionDecl *Definition =
            Callee && !isa<CXXOperatorCallExpr>(Call) ? Callee->getDefinition()
                                                      : nullptr;
        const ParmVarDecl *Parameter =
            Definition && Definition->getBody() &&
                    iArgument < Definition->getNumParams()
                ? Definition->getParamDecl(iArgument)
                : nullptr;
        // Used where it is evaluated, not only in `noexcept(_fWork())`, nor
        // only copied into a lambda the helper makes and doesn't call, `return
        // [_fWork] { _fWork(); };`, unless what it returns is called here.
        bool Deferred = false;
        std::function<bool(const Stmt *)> Evaluated = [&](const Stmt *S) {
          if (!S || isUnevaluatedNode(S))
            return false;
          if (const auto *Choose = dyn_cast<ChooseExpr>(S);
              Choose && !Choose->isConditionDependent())
            return Evaluated(Choose->getChosenSubExpr());
          if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
              Ref && Ref->getDecl() == Parameter)
            return true;
          if (const auto *Made = dyn_cast<LambdaExpr>(S)) {
            if (!llvm::any_of(Made->capture_inits(), Evaluated))
              return false;
            const Stmt *Up = S;
            for (int Depth = 0; Depth < 8; ++Depth) {
              const auto Parents =
                  instantiatedParents(*Up, const_cast<ASTContext &>(Context));
              const Stmt *Parent =
                  Parents.empty() ? nullptr : Parents[0].get<Stmt>();
              if (!isa_and_nonnull<ImplicitCastExpr, MaterializeTemporaryExpr,
                                   CXXBindTemporaryExpr, ParenExpr>(Parent)) {
                const auto *Invoke =
                    dyn_cast_or_null<CXXOperatorCallExpr>(Parent);
                if (Invoke && Invoke->getOperator() == OO_Call &&
                    Invoke->getNumArgs() >= 1 &&
                    Invoke->getArg(0)->IgnoreImplicit()->IgnoreParens() == Made)
                  return true;
                break;
              }
              Up = Parent;
            }
            Deferred = true;
            return false;
          }
          return llvm::any_of(childrenOnce(S), Evaluated);
        };
        // What the helper gives back called here, `fg_Deferred(...)()`, or
        // through a local that holds it.
        auto ResultCalled = [&] {
          const Stmt *Up = Call;
          for (int Depth = 0; Depth < 8; ++Depth) {
            const auto Parents =
                instantiatedParents(*Up, const_cast<ASTContext &>(Context));
            if (Parents.empty())
              return false;
            // A local that holds it, called in its function, `fDeferred()`.
            if (const auto *Holder = Parents[0].get<VarDecl>()) {
              const auto *Function = dyn_cast_or_null<FunctionDecl>(
                  Holder->getParentFunctionOrMethod());
              if (!Function || !Function->getBody())
                return true;
              bool CalledHolder = false;
              std::function<void(const Stmt *)> Visit = [&](const Stmt *Inner) {
                if (!Inner || CalledHolder)
                  return;
                if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(Inner);
                    Invoke && Invoke->getOperator() == OO_Call &&
                    Invoke->getNumArgs() >= 1)
                  if (const auto *Named = dyn_cast<DeclRefExpr>(
                          Invoke->getArg(0)->IgnoreParenImpCasts());
                      Named && Named->getDecl() == Holder)
                    CalledHolder = true;
                for (const Stmt *Child : childrenOnce(Inner))
                  Visit(Child);
              };
              Visit(Function->getBody());
              return CalledHolder;
            }
            const Stmt *Parent = Parents[0].get<Stmt>();
            if (const auto *Invoke =
                    dyn_cast_or_null<CXXOperatorCallExpr>(Parent);
                Invoke && Invoke->getOperator() == OO_Call &&
                Invoke->getNumArgs() >= 1)
              return true;
            if (!isa_and_nonnull<ImplicitCastExpr, MaterializeTemporaryExpr,
                                 CXXBindTemporaryExpr, ParenExpr,
                                 ExprWithCleanups, CXXConstructExpr>(Parent))
              return !isa_and_nonnull<CompoundStmt>(Parent);
            Up = Parent;
          }
          return true;
        };
        // Also by the initializers of a constructor, `m_fCleanup(_fCleanup)`.
        const auto *Constructor =
            dyn_cast_or_null<CXXConstructorDecl>(Definition);
        if (!Parameter || Parameter->isParameterPack() ||
            (Parameter->isReferenced() &&
             (Evaluated(Definition->getBody()) ||
              (Constructor &&
               llvm::any_of(Constructor->inits(),
                            [&](const CXXCtorInitializer *Initializer) {
                              return Evaluated(Initializer->getInit());
                            })) ||
              (Deferred && ResultCalled()))))
          return true;
      }
      if (!ofObject(Call->getArg(iArgument)))
        continue;
      // A local copy of a value given by reference, `fg_Normalize(State)` of
      // `CState State = *m_pState`, is its own storage, which writing doesn't
      // change the object, also a member of it, `State.m_Value`.
      const bool ObjectOperand =
          isa<CXXOperatorCallExpr>(Call) && Callee && hasImplicitObject(Callee);
      const Expr *Given = Call->getArg(iArgument)->IgnoreParenImpCasts();
      const Expr *Copied = Given;
      while (const auto *Part = dyn_cast<MemberExpr>(Copied)) {
        if (Part->isArrow() || !isa<FieldDecl>(Part->getMemberDecl()) ||
            Part->getMemberDecl()->getType()->isReferenceType())
          break;
        Copied = Part->getBase()->IgnoreParenImpCasts();
      }
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Copied);
          Ref && Callee && !(ObjectOperand && iArgument == 0) &&
          iArgument - (ObjectOperand ? 1 : 0) < Callee->getNumParams() &&
          Callee->getParamDecl(iArgument - (ObjectOperand ? 1 : 0))
              ->getType()
              ->isReferenceType() &&
          !Callee->getParamDecl(iArgument - (ObjectOperand ? 1 : 0))
               ->getType()
               ->getPointeeType()
               .isConstQualified())
        if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
            Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
            !Ref->refersToEnclosingVariableOrCapture() &&
            !Local->getType()->isReferenceType() &&
            // Also a copy of a pointer, or of what holds them, that it only
            // assigns, `fg_Reset(pCopy)` of `_pData = nullptr`.
            (holdsOnlyValues(Given->getType(), Context) ||
             onlyAssignsThrough(Callee, iArgument - (ObjectOperand ? 1 : 0))))
          continue;
      // The object of a member operator, like `!*this` with a const
      // `operator!`, like a member function called on it.
      // One that isn't const releases unless what it does can be seen not
      // to, like a check, `bool operator!() { return !m_pData; }`.
      if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
          Method && hasImplicitObject(Method) && isa<CXXOperatorCallExpr>(Call) &&
          iArgument == 0) {
        // Also one that isn't const of what a pointer member points to,
        // `--*m_pCount` of an atomic count of users, which the copies of the
        // object share, like a member function of it,
        // `m_pCount->f_FetchSub(1)`.
        // Also of a pointer in a member, `--m_Data.m_pState->m_nUsers`, or
        // read into a local, but not one to the object or a member of its
        // own, `auto pCount = &m_Count;`.
        const Expr *Object = Call->getArg(0)->IgnoreParenImpCasts();
        const Expr *Pointer = nullptr;
        if (const auto *Deref = dyn_cast<UnaryOperator>(Object);
            Deref && Deref->getOpcode() == UO_Deref)
          Pointer = Deref->getSubExpr()->IgnoreParenImpCasts();
        else if (const auto *Through = dyn_cast<MemberExpr>(Object);
                 Through && Through->isArrow())
          Pointer = Through->getBase()->IgnoreParenImpCasts();
        auto OwnAddress = [](const Expr *Value) {
          for (int Depth = 0; Value && Depth < 8; ++Depth) {
            Value = Value->IgnoreParenImpCasts();
            if (isThis(Value))
              return true;
            if (const auto *Address = dyn_cast<UnaryOperator>(Value);
                Address && Address->getOpcode() == UO_AddrOf)
              return true;
            const auto *Ref = dyn_cast<DeclRefExpr>(Value);
            const auto *Local =
                Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
            if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local))
              return false;
            Value = Local->getInit();
          }
          return true;
        };
        const bool OfPointee = Pointer && Pointer->getType()->isPointerType() &&
                               ofObject(Pointer) && !OwnAddress(Pointer);
        if ((!Method->isConst() && (!Visible(Method) || OfPointee)) ||
            ReleasesAnyway(Method))
          return true;
        continue;
      }
      const unsigned iParameter =
          iArgument - (isa<CXXOperatorCallExpr>(Call) &&
                               hasImplicitObject(Callee)
                           ? 1
                           : 0);
      if (Callee && iParameter < Callee->getNumParams() &&
          !(isa<CXXOperatorCallExpr>(Call) && iArgument == 0 &&
            hasImplicitObject(Callee))) {
        const QualType Parameter = Callee->getParamDecl(iParameter)->getType();
        // Not by a pointer or reference to const, unless what it is given
        // is released or written through it, what is mutable of it, a count
        // of users, `--_pCount->m_nUsers`.
        if ((Parameter->isPointerType() || Parameter->isReferenceType()) &&
            Parameter->getPointeeType().isConstQualified() &&
            !releasesParameter(Callee, iParameter,
                               ownState(Call->getArg(iArgument))) &&
            !(Visible(Callee) &&
              writesThroughParameter(
                  Callee, iParameter,
                  isa<CXXThisExpr>(
                      Call->getArg(iArgument)->IgnoreParenImpCasts()))))
          continue;
        // A number or flag given by value, `fg_Check(m_bValid)`, unless the
        // function releases what it identifies, `fg_Close(m_Handle)`, which
        // one that can't be looked into, like `close(m_Descriptor)`, can.
        // A flag or a floating point number identifies nothing.
        if (Parameter->isBooleanType() || Parameter->isRealFloatingType())
          continue;
        // Nor one given to a member of an instantiation another file makes,
        // whose instantiation there tells what it does.
        if ((Parameter->isArithmeticType() || Parameter->isEnumeralType()) &&
            ((Visible(Callee) && !releasesParameter(Callee, iParameter)) ||
             (!Visible(Callee) && isExternInstantiation(Callee))))
          continue;
        // A pointer or reference given to what only checks it, like
        // `fg_CheckPointer(int *_pValue)`, also the object itself given to
        // what only changes its own members, `fg_Reset(this)` with
        // `_pView->m_pData = nullptr`.
        if ((Parameter->isPointerType() || Parameter->isReferenceType()) &&
            Visible(Callee) && !releasesParameter(Callee, iParameter) &&
            !writesThroughParameter(
                Callee, iParameter,
                isa<CXXThisExpr>(
                    Call->getArg(iArgument)->IgnoreParenImpCasts())))
          continue;
        // Also the address of a member of its own, `fg_Reset(&m_pData)` with
        // `*_ppData = nullptr`, or an array of its own, `fg_Clear(m_Cache)`,
        // which writing changes only the member, unless what the member
        // points to is written, `**_ppData = 0`, or it is given on.
        if (Parameter->isPointerType() && Visible(Callee) &&
            !releasesParameter(Callee, iParameter) &&
            isOwnStorage(Call->getArg(iArgument)) &&
            !writesBeyondParameter(Callee, iParameter))
          continue;
        // Also a member of its own given to a reference, `fg_Reset(m_pData)`
        // with `int *&_pData` and `_pData = nullptr`.
        if (Parameter->isReferenceType() && Visible(Callee) &&
            !releasesParameter(Callee, iParameter) &&
            isOwnStorage(Call->getArg(iArgument), /*ByReference=*/true) &&
            !writesBeyondParameter(Callee, iParameter))
          continue;
        // A copy given to what can be looked into and doesn't release it,
        // like `fg_Log(m_Name)`, unlike `fg_Close(m_Handle)` with `delete
        // _Handle.m_pData`.
        if (Parameter->isRecordType() && Visible(Callee) &&
            !releasesParameter(Callee, iParameter))
          continue;
      }
      // One of the variadic arguments, `fg_Log("View", m_pData)`, of a
      // function that can be looked into and doesn't read them.
      if (Callee && Callee->isVariadic() &&
          iParameter >= Callee->getNumParams() && Visible(Callee) &&
          !readsVariadicArguments(Callee->getDefinition()->getBody()))
        continue;
      return true;
    }
  }
  if (const auto *If = dyn_cast<IfStmt>(S); If && If->isConstexpr()) {
    // What its initializer does, not that it has one, like `int Value = 0`.
    if (doesSomething(If->getInit(), Context))
      return true;
    if (const std::optional<const Stmt *> Case =
            If->getNondiscardedCase(Context))
      return doesSomething(*Case, Context);
    return doesSomething(If->getThen(), Context) ||
           doesSomething(If->getElse(), Context);
  }
  return llvm::any_of(childrenOnce(S), [&](const Stmt *Child) {
    return doesSomething(Child, Context);
  });
}

/// Whether \p Record is an actor: derived from `CActor`.
static bool derivesFromActor(const CXXRecordDecl *Record) {
  // Each base looked into once, also one that several bases share.
  llvm::SmallVector<const CXXRecordDecl *, 8> Worklist{Record};
  llvm::SmallPtrSet<const CXXRecordDecl *, 16> Visited;
  while (!Worklist.empty()) {
    const CXXRecordDecl *Current = Worklist.pop_back_val();
    if (!Current || !Current->hasDefinition())
      continue;
    Current = Current->getDefinition();
    if (!Visited.insert(Current).second)
      continue;
    // The actor of Malterlib, not another class of that name.
    if (Current->getIdentifier() && Current->getName() == "CActor" &&
        hasQualifiedName(Current, "NMib::NConcurrency::CActor"))
      return true;
    for (const CXXBaseSpecifier &Base : Current->bases())
      Worklist.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  return false;
}

static bool releasesOnDestructionUncached(const CXXRecordDecl *Record);

/// Whether destroying an object of \p Record releases something: its
/// destructor does something, or a base or member releases something. An
/// empty destructor, like of a view that has one, doesn't.
static bool releasesOnDestruction(const CXXRecordDecl *Record) {
  if (!Record || !Record->hasDefinition())
    return false;
  Record = Record->getDefinition();
  // A destructor that makes a local of its own class, like a recursive one,
  // is looked into once.
  return cachedResult(&CReleaseCache::Records,
                      &CReleaseCache::RecordsInProgress,
                      static_cast<const void *>(Record),
                      [&] { return releasesOnDestructionUncached(Record); });
}

static bool releasesOnDestructionUncached(const CXXRecordDecl *Record) {
  if (!Record->hasNonTrivialDestructor())
    return false;
  // One of a template that isn't instantiated isn't looked into: its
  // instantiation tells what it does, but what its members and bases release
  // when it is destroyed is told by them.
  if (const CXXDestructorDecl *Destructor = Record->getDestructor();
      Destructor && Destructor->isUserProvided() &&
      !(!Destructor->getBody() &&
        Destructor->getTemplateInstantiationPattern())) {
    const Stmt *Body = Destructor->getBody();
    // Defined in another file, it can't be looked into. A class that releases
    // something defines or deletes its copy constructor too, since copying it
    // as is would release it twice, so one copied as is, like a view, doesn't.
    const bool CopiedAsIs =
        llvm::any_of(Record->ctors(),
                     [](const CXXConstructorDecl *Constructor) {
                       return Constructor->isCopyConstructor() &&
                              !Constructor->isUserProvided() &&
                              !Constructor->isDeleted();
                     }) ||
        (Record->needsImplicitCopyConstructor() &&
         !Record->defaultedCopyConstructorIsDeleted());
    if (!Body) {
      if (!CopiedAsIs)
        return true;
    } else {
      ASTContext &Context = Record->getASTContext();
      if (doesSomething(Body, Context))
        return true;
      // A call of a function given nothing releases what the thread or
      // program has, like a lock a scope guard took, `fg_Unlock()`, which a
      // copy would release again, unless it can be seen to do nothing: as a
      // statement of it, also in a branch or loop of a class that isn't
      // copied as is, `if (m_bActive) fg_Unlock();`, but not one that only
      // fails a check of a view, `if (!m_pData) fg_Fail();`, also through
      // what a member that is called does, `m_fCleanup()`.
      llvm::SmallPtrSet<const FunctionDecl *, 4> Followed;
      // Whether `this` is a global in what is followed: of a member function
      // of one, `g_Resource.f_Release()`, whose members are the global's.
      bool ThisIsGlobal = false;
      // Of a global, also through members of it and elements of an array of
      // it, `g_State.m_Resource`, or a pointer it holds, `g_pResource`.
      auto IsGlobal = [&](const Expr *E) {
        for (int Level = 0; E && Level < 16; ++Level) {
          E = E->IgnoreParenImpCasts();
          if (isa<CXXThisExpr>(E))
            return ThisIsGlobal;
          if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
            const auto *Global = dyn_cast<VarDecl>(Ref->getDecl());
            if (Global && Global->hasGlobalStorage())
              return true;
            // Also through a local reference bound to one, `auto &nUsers =
            // g_nUsers;`, or an unchanged local pointer to one, `auto
            // *pUsers = &g_nUsers;` read through.
            if (!Global || !Global->hasLocalStorage() ||
                isa<ParmVarDecl>(Global) || !Global->getInit())
              return false;
            if (Global->getType()->isReferenceType()) {
              E = Global->getInit();
              continue;
            }
            return false;
          }
          if (const auto *Deref = dyn_cast<UnaryOperator>(E);
              Deref && Deref->getOpcode() == UO_Deref)
            if (const auto *Ref = dyn_cast<DeclRefExpr>(
                    Deref->getSubExpr()->IgnoreParenImpCasts()))
              if (const auto *Pointer = dyn_cast<VarDecl>(Ref->getDecl());
                  Pointer && Pointer->hasLocalStorage() &&
                  !isa<ParmVarDecl>(Pointer) && Pointer->getInit() &&
                  Pointer->getType()->isPointerType() &&
                  isa_and_nonnull<FunctionDecl>(
                      Pointer->getParentFunctionOrMethod()) &&
                  !isReassigned(
                      Pointer,
                      cast<FunctionDecl>(Pointer->getParentFunctionOrMethod())
                          ->getBody()))
                if (const auto *Address = dyn_cast<UnaryOperator>(
                        Pointer->getInit()->IgnoreParenImpCasts());
                    Address && Address->getOpcode() == UO_AddrOf) {
                  E = Address->getSubExpr();
                  continue;
                }
          if (const auto *Member = dyn_cast<MemberExpr>(E);
              Member &&
              (!Member->isArrow() ||
               isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts())))
            E = Member->getBase();
          else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E);
                   Subscript && Subscript->getBase()
                                    ->IgnoreParenImpCasts()
                                    ->getType()
                                    ->isArrayType())
            E = Subscript->getBase();
          else
            return false;
        }
        return false;
      };
      // Each helper is followed once, however deep: a chain of them,
      // `fg_Cleanup()` calling `fg_Close()` calling `fg_Release()`, ends.
      // So is each default argument, which its uses share.
      llvm::SmallPtrSet<const Expr *, 8> FollowedDefaults;
      std::function<bool(const Stmt *, int)> ReleasesGlobal =
          [&](const Stmt *S, int Depth) {
            if (!S || Depth > 32 || isa<LambdaExpr>(S) || isUnevaluatedNode(S))
              return false;
            if (const auto *Choose = dyn_cast<ChooseExpr>(S);
                Choose && !Choose->isConditionDependent())
              return ReleasesGlobal(Choose->getChosenSubExpr(), Depth);
            // Also with an attribute, `[[likely]] fg_Unlock();`.
            if (const auto *Attributed = dyn_cast<AttributedStmt>(S))
              return ReleasesGlobal(Attributed->getSubStmt(), Depth);
            // Also a default argument a call uses, `fg_Work()` of `void
            // fg_Work(int _Unused = (fg_Release(), 0))`.
            if (const auto *Default = dyn_cast<CXXDefaultArgExpr>(S))
              return FollowedDefaults.insert(Default->getExpr()).second &&
                     ReleasesGlobal(Default->getExpr(), Depth);
            if (const auto *Statements = dyn_cast<CompoundStmt>(S))
              return llvm::any_of(Statements->body(), [&](const Stmt *Child) {
                return ReleasesGlobal(Child, Depth);
              });
            // Also in a `try`, `try { fg_Unlock(); } catch (...) {}`, also a
            // function try block of the destructor, or in its handlers.
            if (const auto *Try = dyn_cast<CXXTryStmt>(S)) {
              if (ReleasesGlobal(Try->getTryBlock(), Depth))
                return true;
              for (unsigned iHandler = 0; iHandler < Try->getNumHandlers();
                   ++iHandler)
                if (ReleasesGlobal(Try->getHandler(iHandler)->getHandlerBlock(),
                                   Depth))
                  return true;
              return false;
            }
            const Stmt *Bare = S;
            // What a helper returns, `return fg_Unlock();`, it runs.
            if (const auto *Return = dyn_cast<ReturnStmt>(Bare))
              return ReleasesGlobal(Return->getRetValue(), Depth);
            if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(Bare))
              Bare = Cleanups->getSubExpr();
            // Also in parentheses or cast to void, `(fg_Unlock())` or
            // `(void)fg_Unlock()`.
            for (int Level = 0; Level < 8; ++Level) {
              const auto *E = dyn_cast<Expr>(Bare);
              if (!E)
                break;
              const Expr *Inner = E->IgnoreParens();
              if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Inner);
                  Cast && Cast->getType()->isVoidType())
                Inner = Cast->getSubExpr();
              if (Inner == Bare)
                break;
              Bare = Inner;
            }
            // Each side of a comma, `(fg_Release(), void())`.
            if (const auto *Comma = dyn_cast<BinaryOperator>(Bare);
                Comma && Comma->getOpcode() == BO_Comma)
              return ReleasesGlobal(Comma->getLHS(), Depth) ||
                     ReleasesGlobal(Comma->getRHS(), Depth);
            // Changing what the program has, like a count of users,
            // `--g_nUsers`, which a copy changes again.
            const Expr *Changed = nullptr;
            if (const auto *Unary = dyn_cast<UnaryOperator>(Bare);
                Unary && Unary->isIncrementDecrementOp())
              Changed = Unary->getSubExpr();
            else if (const auto *Binary = dyn_cast<BinaryOperator>(Bare);
                     Binary && Binary->isAssignmentOp())
              Changed = Binary->getLHS();
            // Also deleting what a global points to, `delete g_pData`.
            if (const auto *Delete = dyn_cast<CXXDeleteExpr>(Bare))
              Changed = Delete->getArgument();
            // Also a member or an element of one, `delete g_State.m_pData`
            // or `--g_Counts[0]`.
            for (int Level = 0; Changed && Level < 16; ++Level) {
              Changed = Changed->IgnoreParenImpCasts();
              if (const auto *Member = dyn_cast<MemberExpr>(Changed);
                  Member && !Member->isArrow())
                Changed = Member->getBase();
              else if (const auto *Subscript =
                           dyn_cast<ArraySubscriptExpr>(Changed);
                       Subscript && Subscript->getBase()
                                        ->IgnoreParenImpCasts()
                                        ->getType()
                                        ->isArrayType())
                Changed = Subscript->getBase();
              else
                break;
            }
            if (const auto *Ref = Changed ? dyn_cast<DeclRefExpr>(
                                                Changed->IgnoreParenImpCasts())
                                          : nullptr)
              if (const auto *Global = dyn_cast<VarDecl>(Ref->getDecl());
                  Global && Global->hasGlobalStorage())
                return true;
            // Also through a local reference to one, `auto &nUsers =
            // g_nUsers; --nUsers;`, or a local pointer to one, `--*pUsers`.
            if (const Expr *Written =
                    Changed ? Changed->IgnoreParenImpCasts() : nullptr;
                Written &&
                (isa<DeclRefExpr>(Written) ||
                 (isa<UnaryOperator>(Written) &&
                  cast<UnaryOperator>(Written)->getOpcode() == UO_Deref)) &&
                IsGlobal(Written))
              return true;
            // A member of a global whose member function is followed, `delete
            // m_pData` of `g_Resource.f_Release()`.
            if (Changed && ThisIsGlobal && IsGlobal(Changed))
              return true;
            // An atomic operation that changes a global, like a count of users,
            // `__atomic_fetch_sub(&g_nUsers, 1, __ATOMIC_RELAXED)`, not one
            // that only loads it.
            if (const auto *Atomic = dyn_cast<AtomicExpr>(Bare);
                Atomic && !isAtomicLoad(Atomic)) {
              const Expr *Pointer = Atomic->getPtr()->IgnoreParenImpCasts();
              if (const auto *Address = dyn_cast<UnaryOperator>(Pointer);
                  Address && Address->getOpcode() == UO_AddrOf &&
                  IsGlobal(Address->getSubExpr()))
                return true;
            }
            if (const auto *Call = dyn_cast<CallExpr>(Bare)) {
              // What its arguments do, `fg_Work((fg_Unlock(), 0))`, runs
              // before it.
              for (const Expr *Argument : Call->arguments())
                if (ReleasesGlobal(Argument, Depth))
                  return true;
              const FunctionDecl *Released = Call->getDirectCallee();
              // Also through a local pointer to a function that isn't
              // changed, `auto fUnlock = &fg_Unlock; fUnlock();`.
              if (const auto *Pointer = dyn_cast<DeclRefExpr>(
                      Call->getCallee()->IgnoreParenImpCasts());
                  !Released && Pointer)
                if (const auto *Local = dyn_cast<VarDecl>(Pointer->getDecl());
                    Local && Local->hasLocalStorage() &&
                    !isa<ParmVarDecl>(Local) && Local->getInit() &&
                    Local->getType()->isFunctionPointerType())
                  if (const auto *Function = dyn_cast_or_null<FunctionDecl>(
                          Local->getParentFunctionOrMethod());
                      Function && Function->getBody() &&
                      !isReassigned(Local, Function->getBody())) {
                    const Expr *Init = Local->getInit()->IgnoreParenImpCasts();
                    if (const auto *Address = dyn_cast<UnaryOperator>(Init);
                        Address && Address->getOpcode() == UO_AddrOf)
                      Init = Address->getSubExpr()->IgnoreParenImpCasts();
                    if (const auto *Named = dyn_cast<DeclRefExpr>(Init))
                      Released = dyn_cast<FunctionDecl>(Named->getDecl());
                  }
              // A function through a global pointer to it, `g_fUnlock()`,
              // can release anything.
              if (const auto *Pointer = dyn_cast<DeclRefExpr>(
                      Call->getCallee()->IgnoreParenImpCasts());
                  !Released && Pointer)
                if (const auto *Global = dyn_cast<VarDecl>(Pointer->getDecl());
                    Global && Global->hasGlobalStorage() &&
                    Global->getType()->isFunctionPointerType())
                  return true;
              // Not one that fails, `[[noreturn]] void fg_Fail();`, which
              // ends the program instead of releasing.
              if (Released && Call->getNumArgs() == 0 &&
                  !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Call) &&
                  !Released->isConsteval() && !Released->getBuiltinID() &&
                  !Released->isNoReturn()) {
                // One whose body can be seen releases what it does in it,
                // and one that only returns a constant releases nothing.
                const FunctionDecl *Definition = Released->getDefinition();
                if (!Definition || !Definition->getBody())
                  return true;
                if (Followed.insert(Definition).second &&
                    ReleasesGlobal(Definition->getBody(), Depth + 1))
                  return true;
              }
              // And what a function given more does, like a helper that
              // releases it, `fg_Release(m_bActive)`, when it can be seen.
              if (Released && Call->getNumArgs() > 0 &&
                  !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Call))
                if (const FunctionDecl *Definition = Released->getDefinition();
                    Definition && Definition->getBody() &&
                    Followed.insert(Definition).second &&
                    ReleasesGlobal(Definition->getBody(), Depth + 1))
                  return true;
              // What a member of it that is called does, `m_fCleanup()`.
              const Expr *Object = nullptr;
              if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call))
                Object = Member->getImplicitObjectArgument();
              else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
                       Operator && Operator->getNumArgs() >= 1 &&
                       hasImplicitObject(Operator->getDirectCallee()))
                Object = Operator->getArg(0);
              if (Object && Released && ofObject(Object))
                if (const FunctionDecl *Definition = Released->getDefinition();
                    Definition && Definition->getBody() &&
                    Followed.insert(Definition).second &&
                    ReleasesGlobal(Definition->getBody(), Depth + 1))
                  return true;
              // And what one of a global does, `g_Resource.f_Release()`,
              // which releases what the global has, not one that is const.
              if (const auto *Method =
                      dyn_cast_or_null<CXXMethodDecl>(Released);
                  Object && Method && !Method->isConst() &&
                  !Method->isStatic() && IsGlobal(Object))
                if (const FunctionDecl *Definition = Released->getDefinition();
                    Definition && Definition->getBody()) {
                  llvm::SaveAndRestore<bool> Global(ThisIsGlobal, true);
                  if (Followed.insert(Definition).second &&
                      ReleasesGlobal(Definition->getBody(), Depth + 1))
                    return true;
                }
            }
            // What runs each time it does: the initializers of its locals,
            // `unsigned nLeft = --g_nLocks;`, the condition of a branch or a
            // loop, `if (--g_nLocks == 0)`, and the body of a lambda called
            // where it is made, `[] { --g_nLocks; }()`.
            // Also the lambda of a scope exit it holds, which runs where its
            // scope ends, `auto Cleanup = g_OnScopeExit / [] { fg_Unlock();
            // };`.
            if (const auto *Declaration = dyn_cast<DeclStmt>(Bare))
              return llvm::any_of(Declaration->decls(), [&](const Decl *D) {
                const auto *Local = dyn_cast<VarDecl>(D);
                if (!Local)
                  return false;
                if (ReleasesGlobal(Local->getInit(), Depth))
                  return true;
                const LambdaExpr *Cleanup = scopeExitCallback(Local);
                return Cleanup &&
                       Followed.insert(Cleanup->getCallOperator()).second &&
                       ReleasesGlobal(Cleanup->getBody(), Depth + 1);
              });
            if (const auto *If = dyn_cast<IfStmt>(Bare);
                If && !If->isConstexpr() && !If->isConsteval() &&
                (ReleasesGlobal(If->getInit(), Depth) ||
                 ReleasesGlobal(If->getConditionVariableDeclStmt(), Depth) ||
                 ReleasesGlobal(If->getCond(), Depth)))
              return true;
            if (const auto *While = dyn_cast<WhileStmt>(Bare);
                While && ReleasesGlobal(While->getCond(), Depth))
              return true;
            if (const auto *For = dyn_cast<ForStmt>(Bare);
                For && (ReleasesGlobal(For->getInit(), Depth) ||
                        ReleasesGlobal(For->getCond(), Depth)))
              return true;
            if (const auto *Switch = dyn_cast<SwitchStmt>(Bare);
                Switch && (ReleasesGlobal(Switch->getInit(), Depth) ||
                           ReleasesGlobal(Switch->getCond(), Depth)))
              return true;
            if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(Bare);
                Invoke && Invoke->getOperator() == OO_Call &&
                Invoke->getNumArgs() >= 1)
              if (const LambdaExpr *Called = calledLambda(Invoke->getArg(0));
                  Called && Followed.insert(Called->getCallOperator()).second &&
                  ReleasesGlobal(Called->getBody(), Depth + 1))
                return true;
            // And the parts of an expression, `--g_nLocks == 0`, but of `&&`
            // and `||` only the left side, which runs each time, and of a
            // conditional only the condition; the others are like a branch,
            // `m_bActive && (fg_Unlock(), true)`, in a class that isn't copied
            // as is.
            if (const auto *Logical = dyn_cast<BinaryOperator>(Bare);
                Logical && Logical->isLogicalOp()) {
              if (ReleasesGlobal(Logical->getLHS(), Depth) ||
                  (!CopiedAsIs && ReleasesGlobal(Logical->getRHS(), Depth)))
                return true;
            } else if (const auto *Conditional =
                           dyn_cast<ConditionalOperator>(Bare)) {
              if (ReleasesGlobal(Conditional->getCond(), Depth) ||
                  (!CopiedAsIs &&
                   (ReleasesGlobal(Conditional->getTrueExpr(), Depth) ||
                    ReleasesGlobal(Conditional->getFalseExpr(), Depth))))
                return true;
            } else if (isa<Expr>(Bare) &&
                       !isa<AbstractConditionalOperator, LambdaExpr>(Bare) &&
                       llvm::any_of(childrenOnce(Bare), [&](const Stmt *Part) {
                         return ReleasesGlobal(Part, Depth);
                       }))
              return true;
            // In the branch of an `if constexpr` that isn't discarded, like a
            // statement of it.
            if (const auto *If = dyn_cast<IfStmt>(Bare);
                If && If->isConstexpr()) {
              if (const std::optional<const Stmt *> Case =
                      If->getNondiscardedCase(Context))
                return ReleasesGlobal(*Case, Depth);
            }
            // The body of a `do` loop runs at least once, like a statement.
            if (const auto *Do = dyn_cast<DoStmt>(Bare))
              return ReleasesGlobal(Do->getBody(), Depth);
            // In a branch or the body of another loop, of a class that isn't
            // copied as is, `while (m_nLocks--) fg_Unlock();`.
            if (CopiedAsIs)
              return false;
            if (const auto *If = dyn_cast<IfStmt>(Bare))
              return ReleasesGlobal(If->getThen(), Depth) ||
                     ReleasesGlobal(If->getElse(), Depth);
            if (const auto *While = dyn_cast<WhileStmt>(Bare))
              return ReleasesGlobal(While->getBody(), Depth);
            if (const auto *For = dyn_cast<ForStmt>(Bare))
              return ReleasesGlobal(For->getBody(), Depth);
            if (const auto *Range = dyn_cast<CXXForRangeStmt>(Bare))
              return ReleasesGlobal(Range->getBody(), Depth);
            // Also in a case of a `switch`, `case 1: fg_Unlock(); break;`, or
            // either value of a conditional, `m_bLocked ? fg_Unlock() :
            // void()`.
            if (const auto *Switch = dyn_cast<SwitchStmt>(Bare))
              return ReleasesGlobal(Switch->getBody(), Depth);
            if (const auto *Case = dyn_cast<SwitchCase>(Bare))
              return ReleasesGlobal(Case->getSubStmt(), Depth);
            if (const auto *Conditional =
                    dyn_cast<AbstractConditionalOperator>(Bare))
              return ReleasesGlobal(Conditional->getTrueExpr(), Depth) ||
                     ReleasesGlobal(Conditional->getFalseExpr(), Depth);
            return false;
          };
      if (ReleasesGlobal(Body, 0))
        return true;
    }
  }
  for (const CXXBaseSpecifier &Base : Record->bases())
    if (releasesOnDestruction(Base.getType()->getAsCXXRecordDecl()))
      return true;
  // Not the members of a union, which its destructor destroys only
  // explicitly.
  if (!Record->isUnion())
    for (const FieldDecl *Field : Record->fields())
      if (releasesOnDestruction(Field->getType()
                                    ->getBaseElementTypeUnsafe()
                                    ->getAsCXXRecordDecl()))
        return true;
  return false;
}

/// Whether an object of \p Type owns something that copying it duplicates:
/// a class that releases something when it is destroyed, also a closure that
/// captures such an object. A view or an iterator, which is at what it
/// doesn't own, is not, even when copying it does more than copy its bytes or
/// it has an empty destructor; one that owns something, like a shared
/// pointer, is.
static bool isOwningType(QualType Type) {
  return releasesOnDestruction(Type->getAsCXXRecordDecl());
}

/// Whether \p Concrete is a type the pattern \p Pattern of a partial
/// specialization matches, like `int *` of `t_CType *` or `TCResult<int *>`
/// of `TCResult<t_CType *>`, with what its template parameters are deduced
/// as in \p Deduced, as far as that can be told without deducing like clang.
/// The pairs of a pattern and a type that matched, which match again: what
/// they deduced stays deduced. A type that names another several times would
/// otherwise be matched again through each path to it.
using CMatchedPairs = llvm::DenseSet<std::pair<const void *, const void *>>;
static bool matchesPatternOnce(
    QualType Pattern, QualType Concrete,
    llvm::DenseMap<std::pair<unsigned, unsigned>, QualType> &Deduced,
    llvm::DenseMap<std::pair<unsigned, unsigned>, llvm::APSInt> &Values,
    const ASTContext &Context, int Depth, CMatchedPairs &Matched);
static bool matchesPattern(
    QualType Pattern, QualType Concrete,
    llvm::DenseMap<std::pair<unsigned, unsigned>, QualType> &Deduced,
    llvm::DenseMap<std::pair<unsigned, unsigned>, llvm::APSInt> &Values,
    const ASTContext &Context, int Depth, CMatchedPairs *Matched = nullptr) {
  CMatchedPairs Own;
  if (!Matched)
    Matched = &Own;
  const auto Key =
      std::make_pair(Pattern.getAsOpaquePtr(),
                     Context.getCanonicalType(Concrete).getAsOpaquePtr());
  if (Matched->contains(Key))
    return true;
  if (!matchesPatternOnce(Pattern, Concrete, Deduced, Values, Context, Depth,
                          *Matched))
    return false;
  Matched->insert(Key);
  return true;
}
static bool matchesPatternOnce(
    QualType Pattern, QualType Concrete,
    llvm::DenseMap<std::pair<unsigned, unsigned>, QualType> &Deduced,
    llvm::DenseMap<std::pair<unsigned, unsigned>, llvm::APSInt> &Values,
    const ASTContext &Context, int Depth, CMatchedPairs &Matched) {
  if (Depth > 16)
    return false;
  Concrete = Context.getCanonicalType(Concrete);
  if (const auto *Parameter = Pattern->getAs<TemplateTypeParmType>()) {
    // Also with qualifiers it adds, `t_CType const`.
    const Qualifiers Added = Pattern.getLocalQualifiers();
    if (!Concrete.getQualifiers().compatiblyIncludes(Added, Context))
      return false;
    QualType Bound = Concrete;
    Bound.removeLocalFastQualifiers(Added.getFastQualifiers());
    auto [It, Inserted] =
        Deduced.try_emplace({Parameter->getDepth(), Parameter->getIndex()}, Bound);
    return Inserted || Context.hasSameType(It->second, Bound);
  }
  if (!Pattern->isDependentType())
    return Context.hasSameType(Pattern, Concrete);
  if (Pattern.getCanonicalType().getLocalFastQualifiers() !=
      Concrete.getLocalFastQualifiers())
    return false;
  if (const auto *Pointer = Pattern->getAs<PointerType>()) {
    const auto *ConcretePointer = Concrete->getAs<PointerType>();
    return ConcretePointer &&
           matchesPattern(Pointer->getPointeeType(),
                          ConcretePointer->getPointeeType(), Deduced, Values,
                          Context, Depth + 1, &Matched);
  }
  if (const auto *Reference = Pattern->getAs<ReferenceType>()) {
    const auto *ConcreteReference = Concrete->getAs<ReferenceType>();
    return ConcreteReference &&
           Reference->isLValueReferenceType() ==
               ConcreteReference->isLValueReferenceType() &&
           matchesPattern(Reference->getPointeeType(),
                          ConcreteReference->getPointeeType(), Deduced, Values,
                          Context, Depth + 1, &Matched);
  }
  const auto *Specialization = Pattern->getAs<TemplateSpecializationType>();
  const TemplateDecl *Template =
      Specialization ? Specialization->getTemplateName().getAsTemplateDecl()
                     : nullptr;
  const auto *ConcreteRecord = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      Concrete->getAsCXXRecordDecl());
  if (!Template || !ConcreteRecord ||
      Template->getCanonicalDecl() !=
          ConcreteRecord->getSpecializedTemplate()->getCanonicalDecl())
    return false;
  const llvm::ArrayRef<TemplateArgument> Patterns =
      Specialization->template_arguments();
  const llvm::ArrayRef<TemplateArgument> Concretes =
      ConcreteRecord->getTemplateArgs().asArray();
  if (Patterns.size() != Concretes.size())
    return false;
  for (const auto &[PatternArgument, ConcreteArgument] :
       llvm::zip(Patterns, Concretes)) {
    if (PatternArgument.getKind() == TemplateArgument::Type) {
      if (ConcreteArgument.getKind() != TemplateArgument::Type ||
          !matchesPattern(PatternArgument.getAsType(),
                          ConcreteArgument.getAsType(), Deduced, Values,
                          Context, Depth + 1, &Matched))
        return false;
      continue;
    }
    // A value: of a parameter, `t_bValue`, the same each time it is used,
    // `TCResult<T, t_bValue, t_bValue>`, or a constant, `true`, which the
    // argument is.
    if (ConcreteArgument.getKind() != TemplateArgument::Integral)
      return false;
    const llvm::APSInt Given = ConcreteArgument.getAsIntegral();
    auto Same = [&](const llvm::APSInt &Value) {
      return llvm::APSInt::isSameValue(Value, Given);
    };
    if (PatternArgument.getKind() == TemplateArgument::Integral) {
      if (!Same(PatternArgument.getAsIntegral()))
        return false;
      continue;
    }
    if (PatternArgument.getKind() != TemplateArgument::Expression)
      return false;
    const Expr *Value = PatternArgument.getAsExpr()->IgnoreParenImpCasts();
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Value)) {
      if (const auto *Parameter =
              dyn_cast<NonTypeTemplateParmDecl>(Ref->getDecl())) {
        auto [It, Inserted] = Values.try_emplace(
            {Parameter->getDepth(), Parameter->getIndex()}, Given);
        if (!Inserted && !Same(It->second))
          return false;
        continue;
      }
    }
    std::optional<llvm::APSInt> Constant =
        Value->isValueDependent() ? std::nullopt
                                  : Value->getIntegerConstantExpr(Context);
    if (!Constant || !Same(*Constant))
      return false;
  }
  return true;
}

/// Whether \p Record has a type named `promise_type`, also inherited, which
/// the coroutine traits take the promise of a coroutine from: not a member of
/// another kind, `static int promise_type`.
static bool hasPromiseType(const CXXRecordDecl *Record, ASTContext &Context) {
  return llvm::any_of(
      lookupInherited(Record, &Context.Idents.get("promise_type")),
      [](const NamedDecl *Found) {
        return isa<TypeDecl>(Found->getUnderlyingDecl());
      });
}

/// Whether \p Function can be a coroutine by what it returns: its type has a
/// `promise_type`, or the specialization of `std::coroutine_traits` for its
/// result and parameters that the translation unit has, has one, or a partial
/// one that matches them, like the one of `TCFuture` for any parameters.
/// Whether \p Record has a `promise_type` like `hasPromiseType`, which is
/// complete, as a coroutine needs it to be: a partial specialization of the
/// traits, which the translation unit may not instantiate for the function,
/// tells that it can be a coroutine only by one, unlike `struct
/// promise_type;`.
static bool hasCompletePromiseType(const CXXRecordDecl *Record,
                                   ASTContext &Context) {
  return llvm::any_of(
      lookupInherited(Record, &Context.Idents.get("promise_type")),
      [&](const NamedDecl *Found) {
        const auto *Type = dyn_cast<TypeDecl>(Found->getUnderlyingDecl());
        return Type && !Context.getTypeDeclType(Type)->isIncompleteType();
      });
}

static bool canBeCoroutineResult(const FunctionDecl *Function,
                                 ASTContext &Context) {
  const QualType Type = Function->getReturnType();
  // Also inherited.
  if (const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
      Record && hasPromiseType(Record, Context))
    return true;
  const DeclContext *Std = nullptr;
  for (const Decl *D :
       Context.getTranslationUnitDecl()->lookup(&Context.Idents.get("std")))
    if (const auto *Namespace = dyn_cast<NamespaceDecl>(D))
      Std = Namespace;
  if (!Std)
    return false;
  for (StringRef Name : {"coroutine_traits", "extended_coroutine_traits"}) {
    for (const Decl *D : Std->lookup(&Context.Idents.get(Name))) {
      const auto *Traits = dyn_cast<ClassTemplateDecl>(D);
      if (!Traits)
        continue;
      // The arguments of the traits for the function: the result, and the
      // parameters, after the object of a member function.
      // As canonical types, like those of a specialization.
      auto Argument = [&](QualType Of) {
        return TemplateArgument(Context.getCanonicalType(Of));
      };
      llvm::SmallVector<TemplateArgument, 4> Given;
      Given.push_back(Argument(Type));
      llvm::SmallVector<TemplateArgument, 4> Parameters;
      if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
          Method && Method->isImplicitObjectMemberFunction())
        Parameters.push_back(Argument(
            Method->getRefQualifier() == RQ_RValue
                ? Context.getRValueReferenceType(
                      Method->getFunctionObjectParameterType())
                : Context.getLValueReferenceType(
                      Method->getFunctionObjectParameterType())));
      for (const ParmVarDecl *Parameter : Function->parameters())
        Parameters.push_back(Argument(Parameter->getType()));
      const TemplateParameterList *TraitsParameters =
          Traits->getTemplateParameters();
      if (TraitsParameters->size() == 2 &&
          TraitsParameters->getParam(1)->isTemplateParameterPack())
        Given.push_back(TemplateArgument::CreatePackCopy(Context, Parameters));
      else
        llvm::append_range(Given, Parameters);
      // The specialization for exactly these, which is selected when it is
      // defined, or declared explicitly: without a promise type, the function
      // can't be a coroutine through these traits, whatever their partial
      // specializations are.
      bool Selected = false;
      for (const ClassTemplateSpecializationDecl *Specialization :
           Traits->specializations()) {
        const llvm::ArrayRef<TemplateArgument> Arguments =
            Specialization->getTemplateArgs().asArray();
        if (Arguments.size() != Given.size() ||
            !llvm::all_of(llvm::zip(Arguments, Given), [&](const auto &Pair) {
              return std::get<0>(Pair).structurallyEquals(std::get<1>(Pair));
            }))
          continue;
        if (Specialization->hasDefinition()) {
          if (hasPromiseType(Specialization->getDefinition(), Context))
            return true;
          Selected = true;
        } else if (Specialization->isExplicitSpecialization())
          Selected = true;
      }
      if (Selected)
        continue;
      // Or a partial specialization for any parameters of the result,
      // `coroutine_traits<CFuture, tp_CParams...>`, or of a result its
      // pattern matches, `coroutine_traits<TCFuture<t_CType>, tp_CParams...>`
      // or `coroutine_traits<TCTask<t_CType> *, tp_CParams...>`, which any
      // function that returns one can select, also one with constraints,
      // which aren't told.
      llvm::SmallVector<ClassTemplatePartialSpecializationDecl *> Partials;
      const_cast<ClassTemplateDecl *>(Traits)->getPartialSpecializations(
          Partials);
      for (const ClassTemplatePartialSpecializationDecl *Partial : Partials) {
        const llvm::ArrayRef<TemplateArgument> Arguments =
            Partial->getTemplateArgs().asArray();
        if (Arguments.size() != 2 ||
            Arguments[0].getKind() != TemplateArgument::Type ||
            Arguments[1].getKind() != TemplateArgument::Pack)
          continue;
        // The parameters it is for: some that are given, which the function's
        // first ones match, `coroutine_traits<CTask, COwner, tp_CArgs...>`,
        // and then any parameters, `tp_CArgs...`, not only some, `tp_CArgs
        // *...`, or none more.
        llvm::ArrayRef<TemplateArgument> Fixed = Arguments[1].getPackAsArray();
        bool AnyMore = false;
        if (!Fixed.empty() && Fixed.back().getKind() == TemplateArgument::Type)
          if (const auto *Expansion =
                  Fixed.back().getAsType()->getAs<PackExpansionType>()) {
            if (!Expansion->getPattern()->getAs<TemplateTypeParmType>())
              continue;
            AnyMore = true;
            Fixed = Fixed.drop_back();
          }
        if (Fixed.size() > Parameters.size() ||
            (!AnyMore && Fixed.size() != Parameters.size()))
          continue;
        // Constraints aren't told, which can reject the parameters given,
        // `requires (sizeof(t_CParam) > 64)`, and a promise type that isn't
        // complete can't make a coroutine. Those of one for any parameters,
        // `coroutine_traits<TCFuture<t_CType>, tp_CParams...>`, are taken to
        // accept them, like those of Malterlib's futures, which reject only
        // reference parameters of the member functions of actors.
        if (!Fixed.empty() && (Partial->hasAssociatedConstraints() ||
                               !hasCompletePromiseType(Partial, Context)))
          continue;
        llvm::DenseMap<std::pair<unsigned, unsigned>, QualType> Deduced;
        llvm::DenseMap<std::pair<unsigned, unsigned>, llvm::APSInt> Values;
        if (!llvm::all_of(llvm::seq<size_t>(0, Fixed.size()), [&](size_t i) {
              if (Fixed[i].getKind() != TemplateArgument::Type ||
                  Fixed[i].getAsType()->getAs<PackExpansionType>())
                return false;
              const QualType Given = Parameters[i].getAsType();
              return Fixed[i].getAsType()->isDependentType()
                         ? matchesPattern(Fixed[i].getAsType(), Given, Deduced,
                                          Values, Context, 0)
                         : Context.hasSameType(Fixed[i].getAsType(), Given);
            }))
          continue;
        const bool HasPromise = hasPromiseType(Partial, Context);
        if (const QualType For = Arguments[0].getAsType();
            !For->isDependentType() && Context.hasSameType(For, Type)) {
          if (HasPromise)
            return true;
          continue;
        }
        // Of a result its pattern matches, `TCFuture<t_CReturnType>` any
        // specialization of the template, and `TCResult<t_CType *>` one of a
        // pointer.
        if (HasPromise && matchesPattern(Arguments[0].getAsType(), Type,
                                         Deduced, Values, Context, 0))
          return true;
      }
    }
  }
  return false;
}

/// Whether \p Type is named one of \p Allowed, also through an alias of it,
/// like `CStrPtr`: a qualified name matches the qualified name.
static bool isAllowedType(QualType Type, llvm::ArrayRef<StringRef> Allowed) {
  auto Matches = [&](const NamedDecl *Decl) {
    return llvm::any_of(Allowed, [&](StringRef Name) {
      return Name.contains("::") ? hasQualifiedName(Decl, Name)
                                 : Decl->getName() == Name;
    });
  };
  for (int Depth = 0; Depth < 16; ++Depth) {
    if (const auto *Typedef = Type->getAs<TypedefType>()) {
      if (Matches(Typedef->getDecl()))
        return true;
      Type = Typedef->getDecl()->getUnderlyingType();
      continue;
    }
    break;
  }
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  return Record && Matches(Record);
}

void ParameterOwnershipCheck::checkFunction(const FunctionDecl *Function,
                                            ASTContext &Context) {
  // What a template is instantiated as, not the template, whose types its
  // instantiations tell.
  // Also a defaulted comparison, `bool operator==(COwner _First, COwner
  // _Second) = default;`, which can't be a coroutine, but not another
  // defaulted function, which is a special member.
  if (!Function || Function->isImplicit() || Function->isDeleted() ||
      Function->isTemplated())
    return;
  if (Function->isDefaulted()) {
    switch (Function->getOverloadedOperator()) {
    case OO_EqualEqual:
    case OO_ExclaimEqual:
    case OO_Less:
    case OO_LessEqual:
    case OO_Greater:
    case OO_GreaterEqual:
    case OO_Spaceship:
      break;
    default:
      return;
    }
  }
  // A function that can be a coroutine, by what it returns, like a
  // `TCFuture`, takes what it needs by value, so it survives suspension, also
  // before it is made one, and so do the overrides of an interface and the
  // member functions of an actor, whose calls move the arguments into them.
  if (canBeCoroutineResult(Function, Context))
    return;
  // Also a defaulted comparison that nothing uses, whose body isn't made.
  if (!Function->doesThisDeclarationHaveABody() && !Function->isPureVirtual() &&
      !Function->isExplicitlyDefaulted())
    return;
  // A coroutine takes what it needs by value so it survives suspension, also
  // one that returns something else, like a generator.
  if (Function->doesThisDeclarationHaveABody() &&
      isa_and_nonnull<CoroutineBodyStmt>(Function->getBody()))
    return;
  // The signatures of these are given by others.
  if (Function->isExternC() || Function->isMain())
    return;
  // The member functions of an actor take their parameters by value, since
  // the calls of the actor move them in.
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      Method && (Method->size_overridden_methods() > 0 ||
                 Method->hasAttr<OverrideAttr>() ||
                 (!Method->isStatic() && derivesFromActor(Method->getParent()))))
    return;

  const SourceManager &SM = Context.getSourceManager();
  for (const ParmVarDecl *Parameter : Function->parameters()) {
    const QualType Type = Parameter->getType();
    if (Type->isReferenceType() || !isOwningType(Type))
      continue;
    if (isAllowedType(Type, AllowedTypes))
      continue;

    // An unnamed parameter is reported at its type.
    const SourceLocation Loc = reportLocation(
        Parameter->getIdentifier() ? Parameter->getLocation()
                                   : Parameter->getBeginLoc(),
        SM);
    if (Loc.isInvalid())
      continue;

    // Moving from the parameter consumes it, where the move runs: not in
    // `noexcept(fg_Move(_Value))` or a discarded branch of an `if constexpr`,
    // nor in a lambda, whose copy it can be, `[_Value] { fg_Consume(
    // fg_Move(_Value)); }`, or which can run later or never.
    bool Consumed = false;
    if (Parameter->getIdentifier() && Function->doesThisDeclarationHaveABody()) {
      const auto Moves = match(
          findAll(
              callExpr(callee(functionDecl(hasAnyName(MoveFunctions))),
                       hasArgument(0, ignoringParenImpCasts(
                                          declRefExpr(to(equalsNode(Parameter)))
                                              .bind("moved"))))
                  .bind("move")),
          *Function->getBody(), Context);
      Consumed = llvm::any_of(Moves, [&](const BoundNodes &Move) {
        const DynTypedNode Node =
            DynTypedNode::create(*Move.getNodeAs<CallExpr>("move"));
        return !Move.getNodeAs<DeclRefExpr>("moved")
                    ->refersToEnclosingVariableOrCapture() &&
               !isInUnevaluatedOperand(Node, Context) &&
               !isInDiscardedBranch(Node, Context);
      });
    }
    diag(Loc, "%select{unnamed parameter|parameter %1}0 takes %2 by value; "
              "take it by %select{'const &' when the function borrows it or "
              "by '&&' when it consumes it|'&&', since the function consumes "
              "it}3")
        << (Parameter->getIdentifier() != nullptr) << Parameter << Type
        << Consumed;
  }
}

} // namespace clang::tidy::malterlib
