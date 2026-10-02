//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CoroutineUnsafeThisCheck.h"
#include "ActorAnnotations.h"
#include "CheckedAccess.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"
#include "OncePerTypeVisitor.h"
#include "RunsToCompletion.h"
#include "StorageModel.h"
#include "ValueFlow.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/IgnoreExpr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/CFG.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

/// The annotation of a member function whose result must not outlive its
/// object, `DMibResultEndsBeforeObject`, like the guard `CGitLaunches::f_Init`
/// gives, which runs a lambda using the object when it is destroyed.
static constexpr llvm::StringLiteral ResultEndsBeforeObject =
    "malterlib-result-ends-before-object";

static bool endsBeforeObject(const FunctionDecl *Function) {
  if (!Function)
    return false;
  if (const FunctionDecl *Pattern = Function->getTemplateInstantiationPattern())
    Function = Pattern;
  for (const Decl *Redeclaration : Function->redecls())
    for (const auto *Attribute : Redeclaration->specific_attrs<AnnotateAttr>())
      if (Attribute->getAnnotation() == ResultEndsBeforeObject)
        return true;
  return false;
}

/// The member function \p Lambda is made in, through the lambdas around it.
static const CXXMethodDecl *enclosingMember(const LambdaExpr *Lambda) {
  for (const DeclContext *Context = Lambda->getLambdaClass()->getDeclContext();
       Context; Context = Context->getParent()) {
    const auto *Method = dyn_cast<CXXMethodDecl>(Context);
    if (Method && !isLambdaCallOperator(Method))
      return Method;
    if (isa<RecordDecl>(Context) && !cast<RecordDecl>(Context)->isLambda())
      return nullptr;
  }
  return nullptr;
}

/// Whether \p Lambda is part of what the member function \p Member returns,
/// told from what is returned: past parentheses, cleanups, temporaries and
/// implicit conversions that aren't user-defined, what one of its `return`
/// statements returns, or what a `co_return` initializes the `T` of its
/// future with, by type: of another type, a conversion that takes \p Lambda
/// given as a prvalue or an xvalue; else what is made by a constructor, also
/// in braces, or by a call of a function that isn't a member, of the type
/// the member returns, or of the `T`, and \p Lambda is one of its direct
/// arguments, also through an implicit conversion to the parameter, also by
/// a converting constructor, `co_return fg_AsyncDestroyGeneric([this]() {
/// ... });` of an `fg_AsyncDestroyGeneric(FUnitVoidFutureFunction &&)`.
static bool partOfResult(const LambdaExpr *Lambda, const CXXMethodDecl *Member,
                         ASTContext &Context) {
  // The statement that returns, of the member, not of a lambda in it.
  const Stmt *Return = nullptr;
  DynTypedNode Node = DynTypedNode::create(*Lambda);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    Node = Parents[0];
    if (!Return) {
      if (Node.get<ReturnStmt>() || Node.get<CoreturnStmt>()) {
        Return = Node.get<Stmt>();
        continue;
      }
      if (!Node.get<Stmt>())
        return false;
    }
    if (Node.get<LambdaExpr>() || Node.get<BlockExpr>())
      return false;
    if (const auto *Function = Node.get<FunctionDecl>()) {
      if (!Return ||
          !(Function == Member ||
            Function->getTemplateInstantiationPattern() == Member ||
            Member->getTemplateInstantiationPattern() == Function))
        return false;
      break;
    }
  }
  if (!Return)
    return false;
  // What is returned, and the type it is returned as.
  const Expr *Value = nullptr;
  QualType Result;
  if (const auto *Plain = dyn_cast<ReturnStmt>(Return)) {
    Value = Plain->getRetValue();
    Result = Member->getReturnType();
  } else {
    // What `co_return` initializes the result with, the `T` of its future,
    // by type: the promise's call is plumbing, never looked into.
    Value = cast<CoreturnStmt>(Return)->getOperand();
    Result = futureValueType(Member->getReturnType());
    if (Result.isNull())
      return false;
  }
  if (!Value)
    return false;
  auto Same = [&](QualType Type) {
    return Context.hasSameUnqualifiedType(Type.getNonReferenceType(),
                                          Result.getNonReferenceType());
  };
  // Past what only passes it on, also an implicit conversion by a
  // constructor, which makes what is converted to, not a user-defined
  // conversion operator.
  auto Strip = [](const Expr *E) {
    for (;;) {
      const Expr *Next =
          IgnoreExprNodes(E, IgnoreParensSingleStep, [](Expr *Inner) {
            if (auto *Full = dyn_cast<FullExpr>(Inner))
              return Full->getSubExpr();
            if (auto *Temporary = dyn_cast<MaterializeTemporaryExpr>(Inner))
              return Temporary->getSubExpr();
            if (auto *Bind = dyn_cast<CXXBindTemporaryExpr>(Inner))
              return Bind->getSubExpr();
            if (auto *Cast = dyn_cast<ImplicitCastExpr>(Inner);
                Cast && Cast->getCastKind() != CK_UserDefinedConversion)
              return Cast->getSubExpr();
            // The written `CGuard(...)` of one argument, or `CGuard{...}` of
            // an aggregate.
            if (auto *Functional = dyn_cast<CXXFunctionalCastExpr>(Inner);
                Functional &&
                (Functional->getCastKind() == CK_ConstructorConversion ||
                 Functional->getCastKind() == CK_NoOp))
              return Functional->getSubExpr();
            return Inner;
          });
      if (Next == E)
        return E;
      E = Next;
    }
  };
  // Whether \p Argument is \p Lambda, past what passes it on, also its copy or
  // move, and its conversion to the parameter by a constructor that isn't
  // written, `FUnitVoidFutureFunction`.
  auto IsLambda = [&](const Expr *Argument) {
    for (int Depth = 0; Argument && Depth < 8; ++Depth) {
      Argument = Strip(Argument);
      if (const auto *Found = dyn_cast<LambdaExpr>(Argument))
        return Found == Lambda ||
               Found->getLambdaClass() == Lambda->getLambdaClass();
      const auto *Converted = dyn_cast<CXXConstructExpr>(Argument);
      if (!Converted || isa<CXXTemporaryObjectExpr>(Converted) ||
          Converted->getParenOrBraceRange().isValid() ||
          Converted->getNumArgs() < 1)
        return false;
      Argument = Converted->getArg(0);
    }
    return false;
  };
  const Expr *Made = Strip(Value);
  // Only an object of a class, a guard whose destruction can end what the
  // lambda is given to, not a number, like a token of a registration.
  if (!Result.getNonReferenceType()->isRecordType())
    return false;
  // Of another type, what `co_return` gives is converted to the result by a
  // constructor that can't be seen, which takes a closure given as a prvalue
  // (Rule R): the lambda is part of it.
  // Of braces, `co_return {...};`, the list initializes it.
  if (isa<CoreturnStmt>(Return) && !Same(Made->getType())) {
    if (const auto *List = dyn_cast<InitListExpr>(Made))
      return llvm::any_of(List->inits(), IsLambda);
    return !Value->isLValue() && IsLambda(Value);
  }
  if (!Same(Made->getType()))
    return false;
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(Made))
    return llvm::any_of(Construct->arguments(), IsLambda);
  if (const auto *List = dyn_cast<InitListExpr>(Made))
    return llvm::any_of(List->inits(), IsLambda);
  if (const auto *List = dyn_cast<CXXParenListInitExpr>(Made))
    return llvm::any_of(List->getInitExprs(), IsLambda);
  // Also through a pointer to a function, `factory(...)` of `auto factory =
  // &make_guard`, not a member function.
  if (const auto *Call = dyn_cast<CallExpr>(Made)) {
    const FunctionDecl *Callee = Call->getDirectCallee();
    const bool NonMember =
        Callee ? !isa<CXXMethodDecl>(Callee)
               : !isMemberOrOperatorCall(Call) &&
                     Call->getCallee()->getType()->isFunctionPointerType();
    return NonMember && llvm::any_of(Call->arguments(), IsLambda);
  }
  return false;
}

/// Whether \p Lambda is a guard's of the member function \p Member whose
/// result its callers end before its object: \p Member is annotated so, and
/// so is each function it overrides, through whose callers the result can
/// otherwise come, and \p Lambda is part of what it returns (partOfResult).
static bool guardedResultLambda(const LambdaExpr *Lambda,
                                const CXXMethodDecl *Member,
                                ASTContext &Context) {
  if (!Member || !endsBeforeObject(Member))
    return false;
  // Each overridden function once; one that isn't gone through because of
  // the limit can't be known to be annotated, so none is exempt then.
  llvm::SmallVector<const CXXMethodDecl *, 4> Overridden(
      Member->overridden_methods());
  llvm::SmallPtrSet<const CXXMethodDecl *, 16> Visited;
  while (!Overridden.empty()) {
    const CXXMethodDecl *Base = Overridden.pop_back_val();
    if (!Visited.insert(Base->getCanonicalDecl()).second)
      continue;
    if (Visited.size() > 256 || !endsBeforeObject(Base))
      return false;
    Overridden.append(Base->overridden_methods().begin(),
                      Base->overridden_methods().end());
  }
  return partOfResult(Lambda, Member, Context);
}

static const CXXMethodDecl *pointerTarget(const Expr *PointerExpr,
                                          int Depth = 0);

/// The member function a member pointer call calls, `(Object.*fInit)()`,
/// when the pointer is a variable made of one, `auto fInit = &CObject::f_Init;`,
/// that isn't changed: const, or only read in the function it is declared in.
static const CXXMethodDecl *memberPointerTarget(CMemberCall Call) {
  const auto *Binary =
      dyn_cast<BinaryOperator>(Call->getCallee()->IgnoreParens());
  if (!Binary || !Binary->isPtrMemOp())
    return nullptr;
  return pointerTarget(Binary->getRHS());
}

/// The member function \p PointerExpr names, a variable made of its address,
/// `auto fInit = &CObject::f_Init;`, that isn't changed: also a function
/// pointer to one with an explicit object parameter, `fInit(Object)`.
static const CXXMethodDecl *pointerTarget(const Expr *PointerExpr,
                                          int Depth) {
  if (stackNearlyExhausted())
    return withStack([&] { return pointerTarget(PointerExpr, Depth); });
  if (!PointerExpr || Depth > 8)
    return nullptr;
  // Also through the function a pointer to it gives, `(*fInit)(Object)`.
  if (const auto *Deref =
          dyn_cast<UnaryOperator>(PointerExpr->IgnoreParenImpCasts());
      Deref && Deref->getOpcode() == UO_Deref)
    return pointerTarget(Deref->getSubExpr(), Depth + 1);
  // The address itself, also through explicit casts, `static_cast<CGuard
  // (CObject::*)()>(&CObject::f_Init)` of an overloaded one.
  if (const auto *Address =
          dyn_cast<UnaryOperator>(PointerExpr->IgnoreParenCasts());
      Address && Address->getOpcode() == UO_AddrOf)
    if (const auto *Named =
            dyn_cast<DeclRefExpr>(Address->getSubExpr()->IgnoreParens()))
      return dyn_cast<CXXMethodDecl>(Named->getDecl());
  // Not a parameter, whose initializer is its default argument, not what it
  // is given, `CGuard (CObject::*const fInit)() = &CObject::f_Init`. Only a
  // local variable, not a static one, of the function the use is in, not in
  // a lambda: one that is global, static or a member, or used in the body
  // of a lambda, isn't known.
  const auto *Ref = dyn_cast<DeclRefExpr>(PointerExpr->IgnoreParenCasts());
  const auto *Pointer = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  if (!Pointer || isa<ParmVarDecl>(Pointer) || !Pointer->getInit() ||
      !Pointer->hasLocalStorage())
    return nullptr;
  {
    const FunctionDecl *UsedIn = nullptr;
    DynTypedNode Up = DynTypedNode::create(*Ref);
    for (int Steps = 0; Steps < 1024 && !UsedIn; ++Steps) {
      const auto Above = instantiatedParents(Up, Pointer->getASTContext());
      if (Above.empty() || Above[0].get<LambdaExpr>() ||
          Above[0].get<BlockExpr>())
        return nullptr;
      UsedIn = Above[0].get<FunctionDecl>();
      Up = Above[0];
    }
    if (!UsedIn || isLambdaCallOperator(UsedIn) ||
        UsedIn != dyn_cast_or_null<FunctionDecl>(
                      Pointer->getParentFunctionOrMethod()))
      return nullptr;
  }
  // Of a variable made of one, also a copy of another such variable,
  // `constexpr auto fCopy = fInit;`.
  // Also braced, `auto const fInit{&CObject::f_Init};`.
  const Expr *Init = Pointer->getInit();
  if (const auto *List = dyn_cast<InitListExpr>(Init->IgnoreImplicit());
      List && List->getNumInits() == 1)
    Init = List->getInit(0);
  const CXXMethodDecl *Target = pointerTarget(Init, Depth + 1);
  if (!Target)
    return nullptr;
  // Not one any lambda in the function names, also one in what isn't
  // evaluated, `decltype([&] { fInit = ...; })`, captured or used there,
  // whatever it does with it, also a const or constexpr one: its target isn't
  // known.
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Pointer->getParentFunctionOrMethod());
  ASTContext &Context = Pointer->getASTContext();
  // Anywhere where a lambda is written, from its `[` to its closing `}`: its
  // captures, its template parameters, `[]<auto t_fTarget = fInit>()`, its
  // signature, `[]() -> decltype(fInit)`, and its body, by where each
  // reference to it in the function is spelled, also in a macro, or a use
  // anywhere in a lambda, also one a macro writes. Not by where a macro is
  // expanded, which is the same for a lambda a macro writes and a use beside
  // it, `#define CALL(p, o) ([] {}, (o.*p)())`.
  if (Function && Function->getBody()) {
    // Each type once, which a nest of function pointer types can name
    // exponentially often (OncePerTypeVisitor).
    struct CUses : OncePerTypeVisitor<CUses> {
      const VarDecl *Variable = nullptr;
      llvm::SmallVector<const DeclRefExpr *, 8> Refs;
      llvm::SmallVector<const LambdaExpr *, 8> Lambdas;
      int LambdaDepth = 0;
      bool bInLambda = false;
      bool VisitDeclRefExpr(DeclRefExpr *Use) {
        if (Use->getDecl() != Variable)
          return true;
        if (LambdaDepth > 0)
          bInLambda = true;
        Refs.push_back(Use);
        return true;
      }
      bool TraverseLambdaExpr(LambdaExpr *Lambda) {
        Lambdas.push_back(Lambda);
        ++LambdaDepth;
        const bool bResult =
            OncePerTypeVisitor<CUses>::TraverseLambdaExpr(Lambda);
        --LambdaDepth;
        return bResult;
      }
    } Uses;
    Uses.Variable = Pointer;
    Uses.TraverseStmt(const_cast<Stmt *>(Function->getBody()));
    if (Uses.bInLambda)
      return nullptr;
    const SourceManager &SM = Context.getSourceManager();
    for (const DeclRefExpr *Use : Uses.Refs) {
      const SourceLocation At = SM.getSpellingLoc(Use->getBeginLoc());
      for (const LambdaExpr *Lambda : Uses.Lambdas)
        if (SM.isPointWithin(At, SM.getSpellingLoc(Lambda->getBeginLoc()),
                             SM.getSpellingLoc(Lambda->getEndLoc())))
          return nullptr;
    }
  }
  if (Pointer->getType().isConstQualified())
    return Target;
  if (!Function || !Function->getBody())
    return nullptr;
  // Each use of it, past parentheses, in what reads it, other than what
  // doesn't evaluate it. Each statement once, also an operand that an await
  // shares with its common expression.
  llvm::SmallVector<const Stmt *, 32> Pending{Function->getBody()};
  llvm::SmallPtrSet<const Stmt *, 32> Visited;
  while (!Pending.empty()) {
    const Stmt *Parent = Pending.pop_back_val();
    if (!Parent || !Visited.insert(Parent).second)
      continue;
    // Not what isn't evaluated, `sizeof(fInit)`, `noexcept(fInit)` or a
    // `requires` expression (hasUnevaluatedOperands).
    if (hasUnevaluatedOperands(Parent))
      continue;
    // Not what never runs, in a discarded `if constexpr` branch.
    for (const Stmt *Child : evaluatedChildren(Parent, Context)) {
      if (!Child)
        continue;
      const auto *Use = dyn_cast<Expr>(Child);
      const auto *Named =
          Use ? dyn_cast<DeclRefExpr>(Use->IgnoreParens()) : nullptr;
      const auto *Read = dyn_cast<ImplicitCastExpr>(Parent);
      if (Named && Named->getDecl() == Pointer && !isa<ParenExpr>(Parent) &&
          !(Read && Read->getCastKind() == CK_LValueToRValue))
        return nullptr;
      Pending.push_back(Child);
    }
  }
  return Target;
}

/// Whether \p Construct moves its first argument into what it makes with a
/// move constructor, also one with trailing default arguments, `CGuard(CGuard
/// &&, int = 0)`.
static bool isMoveConstruction(const CXXConstructExpr *Construct) {
  const CXXConstructorDecl *Constructor = Construct->getConstructor();
  return Constructor && Constructor->isMoveConstructor() &&
         Construct->getNumArgs() >= 1 &&
         std::all_of(std::next(Construct->arg_begin()), Construct->arg_end(),
                     [](const Expr *Default) {
                       return isa<CXXDefaultArgExpr>(Default);
                     });
}

/// The await \p E is the operand of, directly, but for parentheses and the
/// temporaries and conversions around it, `co_await Launches.f_Init()`, or
/// null.
static const CoroutineSuspendExpr *directAwaitOf(const Expr *E,
                                                 ASTContext &Context) {
  auto Strip = [](const Expr *Of) {
    return IgnoreExprNodes(Of, IgnoreImplicitSingleStep,
                           IgnoreParensSingleStep);
  };
  DynTypedNode Node = DynTypedNode::create(*E);
  for (int Depth = 0; Depth < 16; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty() || !Parents[0].get<Expr>())
      return nullptr;
    Node = Parents[0];
    if (const auto *Await = Node.get<CoroutineSuspendExpr>())
      return Await->getOperand() && Strip(Await->getOperand()) == Strip(E)
                 ? Await
                 : nullptr;
  }
  return nullptr;
}

/// The local that keeps what \p From, the result of a member function that
/// must not outlive its object or an await of it, gives, through what awaits
/// it and the temporaries around it, `auto DestroyLaunches = co_await co_await
/// Launches.f_Init();`, and owns it, or null.
/// Whether \p Node isn't evaluated where it is, up to the body of the
/// innermost lambda it is in, which is evaluated when the lambda is called,
/// wherever the lambda is written, or else as isInUnevaluatedOperand tells.
/// Also the argument of `__builtin_assume(...)` or `__assume(...)`, which
/// is never evaluated.
static bool unevaluatedInFunction(const DynTypedNode &Node,
                                  ASTContext &Context) {
  DynTypedNode Current = Node;
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Current, Context);
    if (Parents.empty())
      break;
    const DynTypedNode &Parent = Parents[0];
    if (const auto *Assume = Parent.get<CallExpr>();
        Assume &&
        (Assume->getBuiltinCallee() == Builtin::BI__builtin_assume ||
         Assume->getBuiltinCallee() == Builtin::BI__assume) &&
        Current.get<Expr>() != Assume->getCallee())
      return true;
    if (const auto *Lambda = Parent.get<LambdaExpr>();
        Lambda && Current.get<Stmt>() == Lambda->getBody()) {
      // Of what is between it and the body, only the unevaluated operands of
      // the body: the same as asking about it with the lambda's body as the
      // function it is in, which isInUnevaluatedOperand ends at.
      DynTypedNode Up = Node;
      for (int Steps = 0; Steps < 256; ++Steps) {
        const auto Above = instantiatedParents(Up, Context);
        if (Above.empty() || Above[0].get<Stmt>() == Lambda->getBody() ||
            Above[0].get<LambdaExpr>() == Lambda)
          return false;
        // Each step as isInUnevaluatedOperand tells it, also
        // `__builtin_constant_p(...)`.
        if (isUnevaluatedOperandOf(Up, Above[0]))
          return true;
        Up = Above[0];
      }
      return false;
    }
    if (Parent.get<FunctionDecl>() || Parent.get<TranslationUnitDecl>())
      break;
    Current = Parent;
  }
  return isInUnevaluatedOperand(Node, Context);
}

/// What the direct awaits of \p From give, `co_await co_await
/// Launches.f_Init()`, the last of them, or \p From when none awaits it, and
/// in \p Unawaited whether a future, of a type \p Futures lists, is left on
/// the way that isn't the direct operand of the next await, also one left when
/// the awaits are deeper than followed, 16.
static const Expr *followAwaits(const Expr *From, ASTContext &Context,
                                llvm::ArrayRef<StringRef> Futures,
                                bool &Unawaited) {
  const Expr *Given = From;
  Unawaited = false;
  for (int Awaits = 0;; ++Awaits) {
    const bool Future = matchesTypeName(Given->getType(), Futures, Context);
    const CoroutineSuspendExpr *Await =
        Awaits < 16 ? directAwaitOf(Given, Context) : nullptr;
    if (!Await) {
      Unawaited = Future;
      return Given;
    }
    Given = Await;
  }
}

/// A future, of a type \p Futures lists, isn't owned by a local: it isn't
/// awaited directly.
static const VarDecl *findHolder(const Expr *From, ASTContext &Context,
                                 llvm::ArrayRef<StringRef> Futures) {
  const VarDecl *Holder = nullptr;
  DynTypedNode Node = DynTypedNode::create(*From);
  // What gives the value the holder is made of: the call, or the last await
  // of it, and whether it was moved after.
  const Expr *Produced = From;
  bool Moved = false;
  // A future on the way, also of what an await gives or a move constructor
  // makes, is only the direct operand of an await, not moved or made into
  // another, `co_await fg_Move(co_await fg_Move(Launches.f_Init()))`.
  auto UnawaitedFuture = [&](const Expr *Made) {
    return matchesTypeName(Made->getType(), Futures, Context) &&
           !directAwaitOf(Made, Context);
  };
  if (UnawaitedFuture(Produced))
    return nullptr;
  for (int Depth = 0; Depth < 32 && !Holder; ++Depth) {
    const auto Parents = Context.getParents(Node);
    if (Parents.empty())
      break;
    Node = Parents[0];
    // It owns the result: it is of the type of what gives it, which gives it
    // by value, not a pointer, nor what another constructor makes of it,
    // which the parents can skip, `CGuardHolder Holder = co_await ...;`, and
    // a reference only to the temporary it extends the life of, not moved.
    // Not a structured binding, whose bindings aren't followed.
    if (const auto *Variable = Node.get<VarDecl>()) {
      const QualType Type = Variable->getType();
      // Also a reference to a pointer, `auto &&Guard = Object.f_Init()`, which
      // extends the life of the pointer, not of what it points to.
      if (!isa<DecompositionDecl>(Variable) &&
          !Type.getNonReferenceType()->isPointerType() &&
          Produced->isPRValue() && !(Type->isReferenceType() && Moved) &&
          Context.hasSameUnqualifiedType(Type.getNonReferenceType(),
                                         Produced->getType()) &&
          !matchesTypeName(Produced->getType(), Futures, Context))
        Holder = Variable;
      break;
    }
    if (const auto *Suspend = Node.get<CoroutineSuspendExpr>()) {
      Produced = Suspend;
      Moved = false;
      if (UnawaitedFuture(Produced))
        return nullptr;
    }
    // Only through what keeps what the result owns: an await of it, the
    // temporaries around it, a move of it, `fg_Move(...)`, or a move
    // constructor of its type, not another constructor, which can keep it
    // elsewhere, nor a member function of it.
    const Expr *Parent = Node.get<Expr>();
    if (!Parent)
      break;
    // A move constructor makes a new object of the result, which is what
    // gives it then, `CGuard(fg_Move(Launches.f_Init()))`.
    // Of the type of what gives it, not a base, which slices it, `CBase(
    // Launches.f_Init())`.
    auto SameType = [&](const Expr *Made) {
      return Context.hasSameUnqualifiedType(Made->getType(),
                                            Produced->getType());
    };
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(Parent)) {
      if (!isMoveConstruction(Construct) || !SameType(Construct))
        break;
      Produced = Construct;
      Moved = false;
      if (UnawaitedFuture(Produced))
        return nullptr;
      continue;
    }
    // Also of its type, `CGuard(Launches.f_Init())` or `CGuard{...}`, which
    // makes nothing, the result initializing what it does.
    if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Parent)) {
      if (!SameType(Cast))
        break;
      const auto *Construct = dyn_cast_or_null<CXXConstructExpr>(
          Cast->getSubExpr()->IgnoreImplicit());
      if (Construct && !isMoveConstruction(Construct))
        break;
      if (Construct || !Moved) {
        Produced = Cast;
        Moved = false;
        if (UnawaitedFuture(Produced))
          return nullptr;
      }
      continue;
    }
    // Initialized in braces by it, `CGuard Guard{Launches.f_Init()}`, of its
    // type.
    if (const auto *List = dyn_cast<InitListExpr>(Parent)) {
      if (List->getNumInits() != 1 || !SameType(List))
        break;
      continue;
    }
    if (const auto *Move = dyn_cast<CallExpr>(Parent)) {
      const FunctionDecl *Callee = Move->getDirectCallee();
      if (!Callee || !Callee->getIdentifier() || Move->getNumArgs() != 1 ||
          !isMoveFunction(identifierTextOf(Callee)))
        break;
      Moved = true;
      continue;
    }
    if (!isa<CoroutineSuspendExpr, ExprWithCleanups, MaterializeTemporaryExpr,
             CXXBindTemporaryExpr, ImplicitCastExpr, ParenExpr,
             OpaqueValueExpr>(Parent))
      break;
  }
  return Holder;
}

/// The block \p Variable, a local, is declared in, or null.
static const CompoundStmt *declaredIn(const VarDecl *Variable,
                                      ASTContext &Context) {
  const auto Parents = Context.getParents(*Variable);
  if (Parents.empty())
    return nullptr;
  const auto *Declaration = Parents[0].get<DeclStmt>();
  if (!Declaration)
    return nullptr;
  const auto Blocks = Context.getParents(*Declaration);
  return Blocks.empty() ? nullptr : Blocks[0].get<CompoundStmt>();
}

/// Whether the local \p Holder, which keeps the result of a member function
/// that must not outlive its object, can outlive the block it is declared in
/// \p Block. Only two uses of it are known not to: a call of one of its member
/// functions, `DestroyLaunches.f_Clear()`, and an await of it moved,
/// `co_await fg_Move(DestroyLaunches);`, which ends it there. Any other, like
/// returning it, moving it elsewhere, capturing it, giving it to a function or
/// using a data member of it, `Guard.m_fCleanup`, can.
static bool holderEscapes(const VarDecl *Holder, const Stmt *Block,
                          ASTContext &Context,
                          llvm::ArrayRef<StringRef> Futures, int Depth = 0) {
  if (stackNearlyExhausted())
    return withStack([&] { return holderEscapes(Holder, Block, Context, Futures, Depth); });
  // One with a cleanup function, `__attribute__((cleanup(fg_Save)))`, which
  // is given its address where it goes and can keep it.
  if (Holder->hasAttr<CleanupAttr>())
    return true;
  llvm::SmallVector<const DeclRefExpr *, 4> Uses;
  llvm::SmallVector<const Stmt *, 32> Pending{Block};
  while (!Pending.empty()) {
    const Stmt *S = Pending.pop_back_val();
    if (!S)
      continue;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Ref->getDecl() == Holder)
      Uses.push_back(Ref);
    for (const Stmt *Child : keptChildren(S))
      Pending.push_back(Child);
  }
  auto IsMove = [](const Expr *E) {
    const auto *Call = dyn_cast<CallExpr>(E);
    const FunctionDecl *Callee = Call ? Call->getDirectCallee() : nullptr;
    return Callee && Callee->getIdentifier() && Call->getNumArgs() == 1 &&
           isMoveFunction(identifierTextOf(Callee));
  };
  for (const DeclRefExpr *Use : Uses) {
    // In a lambda, which can outlive the block.
    if (Use->refersToEnclosingVariableOrCapture())
      return true;
    DynTypedNode Node = DynTypedNode::create(*Use);
    bool Moved = false;
    bool Safe = false;
    // The steps up from the use, apart from Depth, the holders followed.
    for (int Step = 0; Step < 16; ++Step) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        break;
      const Expr *Child = Node.get<Expr>();
      Node = Parents[0];
      const Expr *Parent = Node.get<Expr>();
      if (!Parent)
        break;
      // After the move, only what keeps what it owns: temporaries, and a move
      // constructor of its type, not another constructor, which can keep it
      // elsewhere, `CEscapingAwaiter(fg_Move(Guard))`.
      if (isa<ImplicitCastExpr, ParenExpr>(Parent) ||
          (Moved && (isa<MaterializeTemporaryExpr, CXXBindTemporaryExpr,
                         ExprWithCleanups, OpaqueValueExpr>(Parent) ||
                     (isa<CXXConstructExpr>(Parent) &&
                      isMoveConstruction(cast<CXXConstructExpr>(Parent)) &&
                      Context.hasSameUnqualifiedType(
                          Parent->getType(),
                          Holder->getType().getNonReferenceType())))))
        continue;
      if (!Moved && IsMove(Parent)) {
        Moved = true;
        continue;
      }
      // Awaited moved, which ends it there, and what the await gives is
      // discarded, or kept by a local that owns it in the block, after the
      // object, and doesn't escape either: awaiting a future of a guard gives
      // the guard, `co_return co_await fg_Move(Pending);` escapes.
      // What the await gives is followed like the result of the call: a
      // future it gives, or the awaits of it give, is awaited directly, also
      // when it is discarded.
      if (Moved) {
        const auto *Awaited = dyn_cast<CoroutineSuspendExpr>(Parent);
        if (!Awaited)
          break;
        bool Unawaited = false;
        const Expr *Suspend =
            followAwaits(Awaited, Context, Futures, Unawaited);
        if (Unawaited)
          break;
        if (Suspend->getType()->isVoidType()) {
          Safe = true;
          break;
        }
        DynTypedNode Up = DynTypedNode::create(*Suspend);
        const Expr *Statement = Suspend;
        for (int Steps = 0; Steps < 8; ++Steps) {
          const auto Above = instantiatedParents(Up, Context);
          if (Above.empty())
            break;
          Up = Above[0];
          // Also discarded explicitly, `(void)co_await fg_Move(Guard)` or
          // `static_cast<void>(...)`.
          if (const Expr *Wrapper = Up.get<Expr>();
              Wrapper &&
              (isa<ExprWithCleanups, ParenExpr, ImplicitCastExpr,
                   MaterializeTemporaryExpr, CXXBindTemporaryExpr>(Wrapper) ||
               (isa<ExplicitCastExpr>(Wrapper) &&
                Wrapper->getType()->isVoidType()))) {
            Statement = Wrapper;
            continue;
          }
          // An expression statement, whose value is discarded: of a block,
          // a label, or the body of an `if`, `else` or loop without braces,
          // `if (bRun) co_await fg_Move(Pending);`, not their condition.
          // The parents can skip the implicit nodes around it.
          auto Is = [&](const Stmt *Body) {
            const auto *BodyExpr = dyn_cast_or_null<Expr>(Body);
            return Body == Statement ||
                   (BodyExpr &&
                    BodyExpr->IgnoreImplicit() == Statement->IgnoreImplicit());
          };
          if (Up.get<CompoundStmt>() || Up.get<LabelStmt>() ||
              Up.get<SwitchCase>() || Up.get<AttributedStmt>())
            Safe = true;
          // Also an init statement, `if (co_await fg_Move(Pending); bRun)`.
          else if (const auto *If = Up.get<IfStmt>())
            Safe = Is(If->getThen()) || Is(If->getElse()) ||
                   Is(If->getInit());
          else if (const auto *While = Up.get<WhileStmt>())
            Safe = Is(While->getBody());
          else if (const auto *Do = Up.get<DoStmt>())
            Safe = Is(Do->getBody());
          else if (const auto *For = Up.get<ForStmt>())
            Safe = Is(For->getBody()) || Is(For->getInc()) ||
                   Is(For->getInit());
          else if (const auto *Range = Up.get<CXXForRangeStmt>())
            Safe = Is(Range->getBody()) || Is(Range->getInit());
          else if (const auto *Switch = Up.get<SwitchStmt>())
            Safe = Is(Switch->getInit());
          break;
        }
        if (!Safe && Depth < 4)
          if (const VarDecl *Next = findHolder(Suspend, Context, Futures);
              Next && Next != Holder && Next->hasLocalStorage() &&
              declaredIn(Next, Context) == Block &&
              Context.getSourceManager().isBeforeInTranslationUnit(
                  Holder->getLocation(), Next->getLocation()))
            Safe = !holderEscapes(Next, Block, Context, Futures, Depth + 1);
        break;
      }
      // A call of one of its member functions, also one with an explicit
      // object parameter that refers to it, `void f_Clear(this CGuard
      // &_Self)`, whose object is its first argument.
      // Past parentheses too, `(Guard).f_Clear()`, which the parents can
      // skip.
      auto Strip = [](const Expr *E) {
        return IgnoreExprNodes(E, IgnoreImplicitSingleStep,
                               IgnoreParensSingleStep);
      };
      if (const auto *Call = dyn_cast<CallExpr>(Parent)) {
        const auto *Method =
            dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
        Safe = Method && Method->isExplicitObjectMemberFunction() &&
               Call->getNumArgs() > 0 &&
               Strip(Call->getArg(0)) == Strip(Child) &&
               Method->getParamDecl(0)->getType()->isReferenceType();
        break;
      }
      if (const auto *Member = dyn_cast<MemberExpr>(Parent);
          Member && isa<CXXMethodDecl>(Member->getMemberDecl()) &&
          Strip(Member->getBase()) == Strip(Child)) {
        const auto Calls = instantiatedParents(*Member, Context);
        const auto *Call = Calls.empty() ? nullptr : Calls[0].get<CallExpr>();
        Safe = Call && Call->getCallee()->IgnoreParenImpCasts() == Member;
      }
      break;
    }
    if (!Safe)
      return true;
  }
  return false;
}

CoroutineUnsafeThisCheck::CoroutineUnsafeThisCheck(StringRef Name,
                                                   ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      FutureTypes(listOption("FutureTypes", "")) {
  splitOptionList(FutureTypes, FutureTypeList);
}

void CoroutineUnsafeThisCheck::storeCheckOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "FutureTypes", FutureTypes);
}

void CoroutineUnsafeThisCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(cxxMethodDecl(isDefinition()).bind("method"), this);
  Finder->addMatcher(lambdaExpr().bind("lambda"), this);
  // Also the calls of template instantiations, which a dependent call,
  // `_Launches.f_Init()`, is a member call in only, and the calls of member
  // operators, `Launches()`, and of members with an explicit object
  // parameter, which aren't member calls.
  Finder->addMatcher(traverse(TK_AsIs, callExpr().bind("call")), this);
}

void CoroutineUnsafeThisCheck::checkResultEndsBeforeObject(
    const CallExpr *Call, ASTContext &Context) {
  // Also through a member pointer that isn't changed, `(Object.*fInit)()`.
  const auto *Method =
      dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
  if (const CMemberCall MemberPointerCall = memberCallOf(Call);
      !Method && MemberPointerCall)
    Method = memberPointerTarget(MemberPointerCall);
  else if (!Method && !isa<CXXOperatorCallExpr>(Call))
    if (const CXXMethodDecl *Target = pointerTarget(Call->getCallee());
        Target && Target->isExplicitObjectMemberFunction())
      Method = Target;
  if (!Method || Method->isStatic() || !endsBeforeObject(Method))
    return;
  // Not one in a template, which is checked as its instantiations make it,
  // unless they share it, nor one that isn't evaluated, `decltype(
  // Launches.f_Init())`, or never runs, in a discarded `if constexpr` branch,
  // or one a constant condition never takes, `if (false)`, which makes no
  // result.
  // In the body of a lambda, which runs when it is called, only what is
  // unevaluated in the body: the lambda can be in what isn't evaluated,
  // `decltype([] { CObject Object; return Object.f_Init(); })`, and still be
  // called, `TCLambda{}()`.
  const DynTypedNode CallNode = DynTypedNode::create(*Call);
  if (isInTemplatePattern(CallNode, Context) ||
      unevaluatedInFunction(CallNode, Context) ||
      isInDiscardedBranch(CallNode, Context) ||
      isInNeverTakenBranch(CallNode, Context))
    return;
  // The object, a local, `CGitLaunches Launches{...}`, not one a pointer
  // points to, `pLaunches->f_Init()`: of a member operator or a member with
  // an explicit object parameter, the first argument.
  const CMemberCall MemberCall = memberCallOf(Call);
  const Expr *Object = MemberCall ? MemberCall->getImplicitObjectArgument()
                       : Call->getNumArgs() ? Call->getArg(0)
                                            : nullptr;
  const auto *ObjectRef =
      Object ? dyn_cast<DeclRefExpr>(Object->IgnoreParenImpCasts()) : nullptr;
  const auto *ObjectVariable =
      ObjectRef ? dyn_cast<VarDecl>(ObjectRef->getDecl()) : nullptr;
  // A result that is a future, of a type FutureTypes lists,
  // `TCUnsafeFuture<CGuard>`, whose destruction doesn't end what makes its
  // result, which goes on, ends first only when the call is directly
  // awaited, `co_await Launches.f_Init()`, and so on while what an await
  // gives is a future too, `co_await co_await Launches.f_Init()`, also after
  // what isn't one, an awaiter that gives one: what the last direct await
  // gives is what the holder keeps, which isn't a future either. Any other
  // use of a future, kept or moved, is reported, with no following of where
  // it goes, also one left when the awaits are deeper than followed.
  bool UnawaitedFuture = false;
  const Expr *Given =
      followAwaits(Call, Context, FutureTypeList, UnawaitedFuture);
  // What keeps the result, through the temporaries around it, `auto
  // DestroyLaunches = co_await co_await Launches.f_Init();`.
  const VarDecl *Holder =
      UnawaitedFuture ? nullptr : findHolder(Given, Context, FutureTypeList);
  // It ends first when it is a local declared after the object in the same
  // block, which destroys it first.
  auto DeclaredIn = [&](const VarDecl *Variable) {
    return declaredIn(Variable, Context);
  };
  const SourceManager &SM = Context.getSourceManager();
  // Not through a pointer, `pLaunches->f_Init()` or `(pLaunches->*fInit)()`.
  const auto *MemberPointer =
      MemberCall ? dyn_cast<BinaryOperator>(Call->getCallee()->IgnoreParens())
                 : nullptr;
  const bool ThroughObject =
      !MemberCall || (MemberPointer && MemberPointer->isPtrMemOp()
                          ? MemberPointer->getOpcode() == BO_PtrMemD
                          : !MemberCall->isArrow());
  const CompoundStmt *Block =
      ObjectVariable ? DeclaredIn(ObjectVariable) : nullptr;
  // And it doesn't leave the block, returned or moved out.
  const bool EndsFirst =
      ObjectVariable && Holder && ThroughObject &&
      ObjectVariable->hasLocalStorage() && Holder->hasLocalStorage() &&
      !ObjectVariable->getType()->isReferenceType() &&
      !ObjectVariable->getType()->isPointerType() && Block &&
      Block == DeclaredIn(Holder) &&
      SM.isBeforeInTranslationUnit(ObjectVariable->getLocation(),
                                   Holder->getLocation()) &&
      !holderEscapes(Holder, Block, Context, FutureTypeList);
  if (EndsFirst)
    return;
  const SourceLocation Loc =
      reportLocationIn(Call->getExprLoc(), Call->getEndLoc(), SM);
  if (Loc.isInvalid())
    return;
  diag(Loc, "the result of %0 must not outlive its object; keep it in a local "
            "declared after the object, a local, in the same block, and don't "
            "return, move or capture it")
      << diagnosticDecl(Method);
}

/// The report of the check that says the object may be gone where it is
/// used (reportLifetime).
static constexpr llvm::StringLiteral ObjectUsedAfterSuspension =
    "TCFuture coroutine %0 is a member of %1, which is not an actor, and uses "
    "its object after a suspension, when it may be gone; derive %1 from "
    "CActor or CAllowUnsafeThis, or return a TCUnsafeFuture";
static constexpr llvm::StringLiteral ObjectUsedHere =
    "the object is used here after a suspension, or in a way that keeps a "
    "reference to it";

void CoroutineUnsafeThisCheck::checkMethod(const CXXMethodDecl *Method,
                                           ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  if (Method->isStatic() || isLambdaCallOperator(Method))
    return;
  if (!isProtectedCoroutine(Method))
    return;
  const CXXRecordDecl *Record = Method->getParent();
  if (isKnownSafeThisClass(Record))
    return;
  // A member function is called directly, so it runs while the caller has
  // the object until it first suspends. Moving the object into the
  // coroutine with `co_await fg_MoveThis(*this)` before that is safe too.
  llvm::SmallVector<const CallExpr *, 2> MovedThis;
  usesThis(Method->getBody(), &MovedThis);
  // Only one whose result a variable of the class is made of, `auto This =
  // co_await fg_MoveThis(*this)`, moves the object: the result is a reference
  // to it, which one bound to it, `auto &&This = ...`, still refers to, and so
  // does a view made of it, `std::span<int const> View = ...`.
  llvm::erase_if(MovedThis, [&](const CallExpr *Call) {
    DynTypedNode Node = DynTypedNode::create(*Call);
    bool Awaited = false;
    for (int Depth = 0; Depth < 16; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return true;
      const Expr *Child = Node.get<Expr>();
      Node = Parents[0];
      // Either value of a conditional, `bChoose ? co_await fg_MoveThis(*this)
      // : co_await fg_MoveThis(*this)`, not its condition.
      if (const auto *Conditional = Node.get<ConditionalOperator>();
          Conditional && Conditional->getCond() != Child)
        continue;
      if (const auto *Variable = Node.get<VarDecl>()) {
        const CXXRecordDecl *Made = Variable->getType()->getAsCXXRecordDecl();
        return !Awaited || Variable->getType()->isReferenceType() || !Made ||
               Made->getCanonicalDecl() != Record->getCanonicalDecl();
      }
      if (Node.get<CoawaitExpr>()) {
        Awaited = true;
        continue;
      }
      if (!Node.get<ImplicitCastExpr>() && !Node.get<ParenExpr>() &&
          !Node.get<MaterializeTemporaryExpr>() &&
          !Node.get<CXXBindTemporaryExpr>() && !Node.get<ExprWithCleanups>() &&
          !Node.get<CXXConstructExpr>() && !Node.get<OpaqueValueExpr>())
        return true;
    }
    return true;
  });
  // Only those that can run: not in a discarded branch of an `if constexpr`,
  // which isn't in the flow, nor one a constant condition skips, `if
  // (false)`.
  {
    CFG::BuildOptions Options;
    Options.PruneTriviallyFalseEdges = true;
    if (std::unique_ptr<CFG> Graph =
            buildGraph(Method, Method->getBody(), Context, Options)) {
      const llvm::BitVector Reachable = reachableBlocks(*Graph);
      llvm::DenseSet<const Stmt *> Runs;
      for (const CFGBlock *Block : *Graph)
        if (Block && Reachable.test(Block->getBlockID()))
          for (const CFGElement &Element : *Block)
            if (const std::optional<CFGStmt> Statement =
                    Element.getAs<CFGStmt>())
              Runs.insert(Statement->getStmt());
      llvm::erase_if(MovedThis, [&](const CallExpr *Call) {
        return !Runs.contains(Call);
      });
    }
  }
  // Not `this` in the captures of a lambda that keeps the object alive,
  // `[this, pThis = TCSharedPointer<CState>{fg_Explicit(this)}]`, whose own
  // check tells where it runs.
  auto CapturedKept = [&](const Stmt *S) {
    DynTypedNode Node = DynTypedNode::create(*S);
    for (int Depth = 0; Depth < 16; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return false;
      // The initializer of an init capture is the variable's and the
      // lambda's.
      for (const DynTypedNode &Parent : Parents)
        if (const auto *Lambda = Parent.get<LambdaExpr>())
          return Node.get<Stmt>() != Lambda->getBody() &&
                 keepsObjectAlive(Lambda);
      const auto *Variable = Parents[0].get<VarDecl>();
      if (!Parents[0].get<Expr>() && !(Variable && Variable->isInitCapture()))
        return false;
      Node = Parents[0];
    }
    return false;
  };
  // Not what the body of a guard's lambda that is part of the result of the
  // member does, which its own check tells where it runs
  // (guardedResultLambda).
  auto InGuardedLambda = [&](const Stmt *S) {
    if (!endsBeforeObject(Method))
      return false;
    DynTypedNode Node = DynTypedNode::create(*S);
    for (int Depth = 0; Depth < 256; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty() || Parents[0].get<FunctionDecl>() == Method)
        return false;
      const Stmt *Child = Node.get<Stmt>();
      const Decl *ChildDecl = Node.get<Decl>();
      Node = Parents[0];
      // Only its body, which runs when the guard calls it, also reached
      // through its call operator and its class, which the parents can give
      // for a lambda that is in more than one place, `co_return [this] { ...
      // };` converted for the promise, not what its captures are made of,
      // `[Value = m_Value]`, which runs where the lambda is made, in the
      // member.
      if (const auto *Lambda = Node.get<LambdaExpr>();
          Lambda &&
          (Child == Lambda->getBody() ||
           (ChildDecl && ChildDecl == Lambda->getLambdaClass())) &&
          guardedResultLambda(Lambda, Method, Context))
        return true;
    }
    return false;
  };
  auto IsThis = [&](const Stmt *S) {
    return isa<CXXThisExpr>(S) &&
           llvm::none_of(MovedThis,
                         [&](const CallExpr *Call) {
                           return Call->getSourceRange().fullyContains(
                               S->getSourceRange());
                         }) &&
           !CapturedKept(S) && !InGuardedLambda(S);
  };
  auto IsView = [&](QualType Type) {
    return isDefaultViewType(Type, Context);
  };
  BorrowRoots Roots{IsThis, /*PointerValues=*/true, IsView};
  // The object of a member function that runs to completion is kept by its
  // callers until they join it, like a reference parameter: only its result
  // can keep it longer.
  Roots.Joined = isJoinedByCallers(Method);
  // Alive where the frame holds a keeper of it by value, a parameter or a
  // local in scope at the use (Rule K).
  Roots.AskKeepers = true;
  CLifetimeUse Found;
  Roots.Found = &Found;
  const Stmt *Use = firstUseAfterSuspension(Method, IsThis, Context, &Roots);
  if (!Use && !MovedThis.empty() && !Roots.Joined) {
    const llvm::DenseSet<const Stmt *> After =
        statementsAfterSuspension(Method, Context);
    for (const CallExpr *Call : MovedThis) {
      if (!isAwaited(Call, Context) || After.contains(Call)) {
        Use = Call;
        break;
      }
    }
  }
  if (!Use)
    return;
  const SourceLocation Loc = reportLocation(Method->getLocation(), SM);
  if (Loc.isInvalid())
    return;
  auto Arguments = [&](const MalterlibDiagnostic &Diag) {
    Diag << diagnosticDecl(Method) << diagnosticDecl(Record);
  };
  if (Found.Use) {
    reportLifetime(*this, Found, Context,
                   {Loc, ObjectUsedAfterSuspension, Arguments, ObjectUsedHere});
    return;
  }
  // What the result keeps, told in the context of the result (resultUses),
  // or the object moved by fg_MoveThis, not one use in one context.
  Arguments(diag(Loc, ObjectUsedAfterSuspension));
  diag(Use->getBeginLoc(), ObjectUsedHere, DiagnosticIDs::Note);
}

void CoroutineUnsafeThisCheck::check(const MatchFinder::MatchResult &Result) {
  // Nothing in a translation unit with an error that stops it from compiling
  // (hasUncompilableErrors): no flow is built there, and what the errors leave
  // can't be relied on.
  if (hasUncompilableErrors(*Result.Context))
    return;
  if (Repeated.repeated(Result))
    return;
  ASTContext &Context = *Result.Context;

  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("method")) {
    for (const FunctionDecl *Body : Instantiations.bodiesOf(Method, Context))
      if (const auto *BodyMethod = dyn_cast<CXXMethodDecl>(Body)) {
        std::optional<InstantiationScope> Scope;
        if (Body != Method)
          Scope.emplace(*this, Body);
        checkMethod(BodyMethod, Context);
      }
    return;
  }

  if (const auto *Call = Result.Nodes.getNodeAs<CallExpr>("call")) {
    checkResultEndsBeforeObject(Call, Context);
    return;
  }

  const auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda");
  const CXXMethodDecl *Pattern = Lambda->getCallOperator();
  for (const FunctionDecl *Body : Instantiations.bodiesOf(Pattern, Context)) {
    if (Body == Pattern) {
      checkLambda(Lambda, Pattern, Context);
      continue;
    }
    if (const auto *Operator = dyn_cast<CXXMethodDecl>(Body))
      if (const LambdaExpr *InstanceLambda =
              Instantiations.lambdaOf(Body, Context)) {
        const InstantiationScope Scope(*this, Body);
        checkLambda(InstanceLambda, Operator, Context);
      }
  }
}

/// Whether one of \p Roots is the object, `this` of any class.
static bool refersToObject(const CStorageRoots &Roots) {
  return llvm::any_of(Roots.Roots, [](const CStorageRoot &Root) {
    return !Root.Variable &&
           (Root.Kind == ERootKind::Object || Root.Kind == ERootKind::Actor ||
            Root.Kind == ERootKind::UnsafeThis);
  });
}

void CoroutineUnsafeThisCheck::checkLambda(const LambdaExpr *Lambda,
                                           const CXXMethodDecl *Operator,
                                           ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  // Not one that is never made, in what isn't evaluated or never runs, `if
  // constexpr (false)`.
  if (!isProtectedCoroutine(Operator) || isNeverRun(Lambda, Context))
    return;
  // The captures that point to the object: `this`, `[pThis = this]` and
  // `[pThis]` with `auto *pThis = this`. A copy of the object, `[*this]`,
  // lives in the closure.
  llvm::SmallPtrSet<const ValueDecl *, 2> Aliases;
  // Of those, the copies of a record, whose own parts are their own.
  llvm::SmallPtrSet<const ValueDecl *, 2> Copies;
  bool CapturesThis = false;
  for (const LambdaCapture &Capture : Lambda->captures()) {
    if (Capture.capturesThis() && Capture.getCaptureKind() != LCK_StarThis) {
      CapturesThis = true;
      continue;
    }
    const auto *Variable = capturedVariableOf(Capture);
    if (!Variable || !isCapturedAliasOfThis(Variable, /*OwnObject=*/false))
      continue;
    // A reference captured by copy, `[State]` of `auto &State =
    // fg_Const(*this)`, copies what it refers to into the closure, a value of
    // its own (Rule V): a view of ViewTypes is followed to what it refers
    // to; of a record, a use that reads only its own part isn't one of the
    // object (readsOnlyOwnPart), and one that reads past it is where a
    // pointer or reference member of it can refer to the object, or what it
    // refers to can't be told (borrowedPartRoots, DMibSetByOwnClass).
    const QualType Referent = Variable->getType().getNonReferenceType();
    if (Capture.getCaptureKind() == LCK_ByCopy &&
        Variable->getType()->isReferenceType() && !Referent->isPointerType()) {
      if (isDefaultViewType(Referent, Context)) {
        if (!refersToObject(storageRootsOf(Variable->getInit(), Context, 1)))
          continue;
      } else {
        const CStorageRoots Parts = borrowedPartRoots(
            Variable->getInit(), enclosingMember(Lambda), Context);
        if (!refersToObject(Parts) &&
            llvm::none_of(Parts.Roots, [](const CStorageRoot &Root) {
              return Root.Kind == ERootKind::Unknown;
            }))
          continue;
        Copies.insert(Variable);
      }
    }
    Aliases.insert(Variable);
  }
  // Whether \p S designates the object through an alias of it.
  auto IsAliasUse = [&](const Stmt *S) {
    const auto *Ref = dyn_cast<DeclRefExpr>(S);
    return Ref && Aliases.contains(Ref->getDecl()) &&
           (!Copies.contains(Ref->getDecl()) ||
            !readsOnlyOwnPart(Ref, Ref->getDecl(),
                              Ref->getDecl()->getType().getNonReferenceType(),
                              Context));
  };
  // A copy of a record none of whose uses reads past its own part holds
  // only values of its own.
  if (!Copies.empty()) {
    llvm::SmallPtrSet<const ValueDecl *, 2> Used;
    if (const Stmt *Body = Operator->getBody())
      forEachStmtOnce(Body, [&](const Stmt *S) {
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
            Ref && Copies.contains(Ref->getDecl()) && IsAliasUse(Ref))
          Used.insert(Ref->getDecl());
        return EWalk::Descend;
      });
    for (const ValueDecl *Copy : Copies)
      if (!Used.contains(Copy))
        Aliases.erase(Copy);
  }
  const LambdaCapture *First = nullptr;
  for (const LambdaCapture &Capture : Lambda->captures())
    if ((Capture.capturesThis() && Capture.getCaptureKind() != LCK_StarThis) ||
        (Capture.capturesVariable() &&
         Aliases.contains(Capture.getCapturedVar()))) {
      First = &Capture;
      break;
    }
  // A lambda that a member function that runs to completion starts and joins
  // runs while the object is kept for that function.
  if (const auto *Method = dyn_cast_or_null<CXXMethodDecl>(
          enclosingPhaseFunction(Lambda, Context));
      Method && !Method->isStatic() && isJoinedByCallers(Method) &&
      !isKnownSafeThisClass(Method->getParent()) &&
      lambdaJoin(Lambda, Context, nullptr) &&
      inRunsToCompletionContext(Lambda, Context)) {
    // Except what its result keeps of the object, like `co_return &m_Value`,
    // which outlives the join.
    // Also through a capture of a pointer to it, `[pThis = this]`.
    auto IsThis = [&](const Stmt *S) {
      return isa<CXXThisExpr>(S) || IsAliasUse(S);
    };
    auto IsView = [&](QualType Type) { return isDefaultViewType(Type, Context); };
    BorrowRoots Roots{IsThis, /*PointerValues=*/true, IsView};
    Roots.Joined = true;
    const Stmt *Use = firstUseAfterSuspension(Operator, IsThis,
                                              Context, &Roots);
    if (!Use)
      return;
    const SourceLocation Loc = reportLocationIn(Lambda->getBeginLoc(), Lambda->getEndLoc(), SM);
    if (Loc.isInvalid())
      return;
    diag(Loc, "a TCFuture coroutine lambda that %0, which runs to "
              "completion, starts and joins returns a pointer or reference "
              "into the object of %1, which can outlive the join; return a "
              "value instead")
        << diagnosticDecl(Method) << diagnosticDecl(Method->getParent());
    diag(Use->getBeginLoc(), "the object is kept here by the result",
         DiagnosticIDs::Note);
    return;
  }
  if (!First)
    return;
  const CXXRecordDecl *Record = enclosingThisClass(Lambda);
  if (!Record)
    return;
  if (isKnownSafeThisClass(Record))
    return;
  // A shared pointer to the object in the closure keeps it alive while the
  // closure is, which a direct call of a temporary closure doesn't.
  llvm::SmallVector<ClosureConsumer, 2> Consumers =
      closureConsumers(Lambda, Context, vocabulary().SafeCallees.names());
  giveUnlistedCallSafe(Consumers, vocabulary().SafeCallees.names());
  // Nothing is said of one in a local function wrapper whose closure can't
  // be told (ClosureUse::Untold).
  if (llvm::any_of(Consumers, [](const ClosureConsumer &Consumer) {
        return Consumer.Use == ClosureUse::Untold;
      }))
    return;
  const bool ClosureKept =
      !Consumers.empty() &&
      llvm::all_of(Consumers, [](const ClosureConsumer &Consumer) {
        return Consumer.Use == ClosureUse::SafeCallee ||
               Consumer.Use == ClosureUse::CallSafe;
      });
  // Each use of the object is alive where the context there holds a keeper
  // of it by value (Rule K): the parameters and the locals in scope, and the
  // captures of a closure kept alive while it runs.
  auto IsObject = [&](const Stmt *S) {
    return (CapturesThis && isa<CXXThisExpr>(S)) || IsAliasUse(S);
  };
  auto IsView = [&](QualType Type) { return isDefaultViewType(Type, Context); };
  BorrowRoots Roots{IsObject, /*PointerValues=*/true, IsView};
  // What `this` and a local pointer to it point to, what a local reference
  // to it designates, and what a copied view refers to (aliveWhereUsed).
  Roots.AskKeepers = true;
  Roots.ClosureKept = ClosureKept;
  // But not what its result keeps of the object, `co_return &m_Value`, which
  // outlives the closure.
  const bool KeptAlive = (keepsObjectAlive(Lambda) && ClosureKept) ||
                         aliveAtEachUse(Operator, IsObject, Roots, Context);
  // A lambda of a member function whose result its callers end before the
  // object, DMibResultEndsBeforeObject, which the guard it gives starts when
  // it is destroyed, runs while the object is alive until it first suspends:
  // one that is part of what it returns, not one it gives elsewhere.
  if (const CXXMethodDecl *Member = enclosingMember(Lambda);
      !KeptAlive && guardedResultLambda(Lambda, Member, Context)) {
    if (!firstUseAfterSuspension(Operator, IsObject, Context, &Roots))
      return;
    reportLambda(Lambda, *First, Record, SM);
    return;
  }
  // Otherwise the object must live while the function that makes the lambda
  // runs, through the lambdas around it.
  if (!KeptAlive && !isObjectAliveWhileRunning(Lambda, Context)) {
    reportLambda(Lambda, *First, Record, SM);
    return;
  }
  // A dispatch joined right away runs while the caller has the object.
  if (!KeptAlive && isJoinedBlocking(Lambda, Context))
    return;
  // A lambda called directly runs while the caller has the object until it
  // first suspends.
  if (KeptAlive) {
    BorrowRoots Result = Roots;
    Result.Joined = true;
    Result.AskKeepers = false;
    if (firstUseAfterSuspension(Operator, IsObject, Context, &Result))
      reportLambda(Lambda, *First, Record, SM);
    return;
  }
  if (isCalledInScope(Consumers) &&
      !firstUseAfterSuspension(Operator, IsObject, Context,
                               &Roots))
    return;
  reportLambda(Lambda, *First, Record, SM);
}

void CoroutineUnsafeThisCheck::reportLambda(const LambdaExpr *Lambda,
                                            const LambdaCapture &Capture,
                                            const CXXRecordDecl *Record,
                                            const SourceManager &SM) {
  const SourceLocation Loc = reportLocationIn(
      Capture.isImplicit() ? Lambda->getCaptureDefaultLoc()
                           : Capture.getLocation(),
      Lambda->getEndLoc(), SM);
  if (Loc.isInvalid())
    return;
  // Of an alias, `[pThis = this]`, or a pointer into it, `[pValue =
  // &m_Value]`, the capture.
  if (!Capture.capturesThis() && Capture.capturesVariable()) {
    diag(Loc, "a TCFuture coroutine lambda captures %1, which points to or "
              "into the object of %0, which is not an actor, so the object "
              "may be gone when it runs or resumes; derive %0 from CActor or "
              "CAllowUnsafeThis, keep it alive with a shared pointer in the "
              "capture, or return a TCUnsafeFuture")
        << diagnosticDecl(Record) << Capture.getCapturedVar();
    return;
  }
  diag(Loc, "a TCFuture coroutine lambda captures 'this' of %0, which is "
            "not an actor, so the object may be gone when it runs or "
            "resumes; derive %0 from CActor or CAllowUnsafeThis, keep it "
            "alive with a shared pointer in the capture, or return a "
            "TCUnsafeFuture")
      << diagnosticDecl(Record);
}

} // namespace clang::tidy::malterlib
