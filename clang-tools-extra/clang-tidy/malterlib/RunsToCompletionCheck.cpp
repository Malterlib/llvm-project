//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RunsToCompletionCheck.h"
#include "CoroutineUtils.h"
#include "RunsToCompletion.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/ScopeExit.h"
#include <functional>

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

void RunsToCompletionCheck::registerMatchers(MatchFinder *Finder) {
  // Whether a use is evaluated is told in `check`, for the functions that run
  // to completion, since telling it walks the parents.
  Finder->addMatcher(declRefExpr(to(functionDecl())).bind("ref"), this);
  Finder->addMatcher(memberExpr(member(cxxMethodDecl())).bind("member"),
                     this);
  Finder->addMatcher(
      functionDecl(isDefinition()).bind("function"),
      this);
  Finder->addMatcher(cxxMethodDecl(isOverride()).bind("override"), this);
  Finder->addMatcher(
      callExpr(callee(functionDecl(hasName("fg_ContinueRunningOnActor"))),
               unless(isInUnevaluatedContext()))
          .bind("switch"),
      this);
  // Awaiting a transfer made elsewhere, like one given as a parameter.
  Finder->addMatcher(coawaitExpr().bind("await"), this);
  // A call in an instantiation whose callee it shares with the template,
  // which isn't dependent, `fg_Work(Value)` in `fg_Start<int>`: the callee
  // has the template's call as its parent too, so it is told by the call.
  Finder->addMatcher(callExpr(callee(functionDecl())).bind("call"), this);
}

/// The call that \p Use is the callee of, past parentheses and implicit
/// nodes, any number of them, `(((fg_Read)))(Value)`.
static const CallExpr *callOf(const Expr *Use, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Use);
  for (int Depth = 0; Depth < 4096; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    if (Parents[0].get<ParenExpr>() || Parents[0].get<ImplicitCastExpr>()) {
      Node = Parents[0];
      continue;
    }
    const auto *Call = Parents[0].get<CallExpr>();
    if (Call && Call->getCallee()->IgnoreParenImpCasts() ==
                    Node.get<Expr>()->IgnoreParenImpCasts())
      return Call;
    return nullptr;
  }
  return nullptr;
}

static const ValueDecl *rootOf(const Expr *E, bool &This, bool &Temporary,
                               bool Pointee, ASTContext &Context);

/// What either of \p Values refers to, which is given: the one that goes
/// first, a local declared last, must outlive the join, as rootOf tells.
static const ValueDecl *rootOfEither(llvm::ArrayRef<const Expr *> Values,
                                     bool &This, bool &Temporary, bool Pointee,
                                     ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  This = false;
  Temporary = false;
  bool AllThis = true;
  const ValueDecl *First = nullptr;
  // A local goes before a static or global, whatever the order they are
  // declared in.
  auto IsLocal = [](const ValueDecl *Decl) {
    const auto *Variable = dyn_cast<VarDecl>(Decl);
    return Variable && Variable->hasLocalStorage();
  };
  for (const Expr *Value : Values) {
    bool ValueThis = false, ValueTemporary = false;
    const ValueDecl *Root =
        rootOf(Value, ValueThis, ValueTemporary, Pointee, Context);
    Temporary |= ValueTemporary;
    // The object of `this` outlives what its function joins.
    if (ValueThis && !ValueTemporary)
      continue;
    AllThis = false;
    if (!Root)
      return nullptr;
    if (!First || (IsLocal(Root) != IsLocal(First)
                       ? IsLocal(Root)
                       : SM.isBeforeInTranslationUnit(First->getLocation(),
                                                      Root->getLocation())))
      First = Root;
  }
  if (AllThis) {
    This = true;
    return nullptr;
  }
  return First;
}

/// What \p E refers to: the local or parameter it is rooted in, past members,
/// dereferences and addresses. Null for `this`, which \p This is set for, and
/// for what can't be told. \p Temporary is set for a temporary.
static const ValueDecl *rootOfImpl(const Expr *E, bool &This, bool &Temporary,
                                   bool Pointee, ASTContext &Context) {
  const SourceManager &SM = Context.getSourceManager();
  This = false;
  Temporary = false;
  // Either value of a conditional, like `b ? a : c`: the one that goes first,
  // a local declared last, must outlive the join.
  if (const auto *Conditional =
          E ? dyn_cast<ConditionalOperator>(E->IgnoreParenImpCasts()) : nullptr;
      Conditional && (Conditional->isGLValue() ||
                      Conditional->getType()->isPointerType())) {
    // Only the value a constant condition chooses, like `true ? Local :
    // Alias`, is ever given.
    bool Chosen = false;
    if (!Conditional->getCond()->isValueDependent() &&
        Conditional->getCond()->EvaluateAsBooleanCondition(Chosen, Context))
      return rootOf(Chosen ? Conditional->getTrueExpr()
                           : Conditional->getFalseExpr(),
                    This, Temporary, Pointee, Context);
    // A null pointer refers to nothing: the other value is what is given,
    // like `bChoose ? &Local : nullptr`.
    auto IsNull = [&](const Expr *Value) {
      return Value->isNullPointerConstant(Context,
                                          Expr::NPC_ValueDependentIsNotNull);
    };
    if (IsNull(Conditional->getFalseExpr()))
      return rootOf(Conditional->getTrueExpr(), This, Temporary, Pointee,
                    Context);
    if (IsNull(Conditional->getTrueExpr()))
      return rootOf(Conditional->getFalseExpr(), This, Temporary, Pointee,
                    Context);
    return rootOfEither(
        {Conditional->getTrueExpr(), Conditional->getFalseExpr()}, This,
        Temporary, Pointee, Context);
  }
  // For a pointer argument, what it points to must outlive the join. How many
  // pointers deep what is referred to is, like 2 for `**ppValue`.
  int Dereferenced = Pointee ? 1 : 0;
  for (int Depth = 0; E && Depth < 16; ++Depth) {
    if (isa<MaterializeTemporaryExpr>(E)) {
      Temporary = true;
      return nullptr;
    }
    E = E->IgnoreParenImpCasts();
    if (isa<CXXThisExpr>(E)) {
      This = true;
      return nullptr;
    }
    // A default argument, `fg_Read()` of `fg_Read(int const &_Value =
    // g_Value)`, is what it is made of where it is used.
    if (const auto *Default = dyn_cast<CXXDefaultArgExpr>(E)) {
      E = Default->getExpr();
      continue;
    }
    // Also a conditional reached on the way, like what a local pointer was
    // made from, `int *pValue = bChoose ? &A : &B`.
    if (isa<ConditionalOperator>(E) && Depth > 0 && Dereferenced <= 1)
      return rootOf(E, This, Temporary, Dereferenced == 1, Context);
    // The address of a variable designates the variable itself; what a
    // pointer points to is through a dereference.
    if (const auto *Unary = dyn_cast<UnaryOperator>(E);
        Unary && (Unary->getOpcode() == UO_AddrOf ||
                  Unary->getOpcode() == UO_Deref)) {
      if (Unary->getOpcode() == UO_Deref)
        ++Dereferenced;
      else if (Dereferenced)
        --Dereferenced;
      E = Unary->getSubExpr();
      continue;
    }
    // A cast of a pointer or reference to another, like
    // `static_cast<int const *>(&Value)`, is of what it casts.
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
        Cast && (Cast->getType()->isPointerType() || Cast->isGLValue()) &&
        (Cast->getSubExpr()->getType()->isPointerType() ||
         Cast->getSubExpr()->isGLValue())) {
      E = Cast->getSubExpr();
      continue;
    }
    // A pointer kept in a member, or read through another pointer, may point
    // anywhere: what it points to can't be told. So can what a view kept in
    // a member refers to, but a member array has its elements.
    if (Dereferenced && E->getType()->isPointerType() &&
        (isa<MemberExpr>(E) ||
         (isa<UnaryOperator>(E) &&
          cast<UnaryOperator>(E)->getOpcode() == UO_Deref)))
      return nullptr;
    if (Dereferenced && isa<MemberExpr>(E) && !E->getType()->isArrayType())
      return nullptr;
    if (const auto *Member = dyn_cast<MemberExpr>(E)) {
      Dereferenced += Member->isArrow() ? 1 : 0;
      E = Member->getBase();
      continue;
    }
    // A copy, like of a view given by value, is of what it copies.
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(E);
        Construct && Construct->getNumArgs() == 1 &&
        Construct->getConstructor()->isCopyOrMoveConstructor()) {
      E = Construct->getArg(0);
      continue;
    }
    // A call that gives back what it is given, `fg_Const(Value)`, refers to
    // that.
    if (const auto *Call = dyn_cast<CallExpr>(E);
        Call && (Call->isGLValue() || Call->getType()->isPointerType())) {
      const llvm::SmallVector<const Expr *, 2> Choices =
          returnedArgumentChoices(Call);
      if (Choices.size() == 1) {
        E = Choices[0];
        continue;
      }
      // Or one of them, `fg_Choose(bFirst, First, Second)` of `return
      // _bFirst ? _First : _Second;`, like a conditional.
      if (Choices.size() > 1 && Dereferenced <= 1)
        return rootOfEither(Choices, This, Temporary, Dereferenced == 1,
                            Context);
    }
    // A member that a visible getter gives a reference to, `Object.f_Get()`
    // of `return m_Value;`, lives as long as the object, also an operator of
    // it, `*State` of `TCOptional`.
    const CXXMethodDecl *Getter = nullptr;
    const Expr *GetterObject = nullptr;
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E)) {
      Getter = Call->getMethodDecl();
      GetterObject = Call->getImplicitObjectArgument();
    } else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
               Operator && Operator->getNumArgs() >= 1) {
      Getter = dyn_cast_or_null<CXXMethodDecl>(Operator->getDirectCallee());
      if (Getter && Getter->isImplicitObjectMemberFunction())
        GetterObject = Operator->getArg(0);
    }
    if (const auto *Call = dyn_cast<CallExpr>(E);
        Call && !Dereferenced && Call->isGLValue() && Getter && GetterObject &&
        (!Getter->isVirtual() || Getter->hasAttr<FinalAttr>() ||
         Getter->getParent()->hasAttr<FinalAttr>()))
      if (const FunctionDecl *Definition = Getter->getDefinition();
          Definition && Definition->getBody()) {
        auto IsOwnMember = [](const Expr *Value) {
          Value = Value->IgnoreParenImpCasts();
          while (const auto *Member = dyn_cast<MemberExpr>(Value)) {
            const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
            if (!Field || Field->getType()->isReferenceType())
              return false;
            Value = Member->getBase()->IgnoreParenImpCasts();
            if (Member->isArrow())
              return isa<CXXThisExpr>(Value);
          }
          return false;
        };
        const llvm::SmallVector<const Expr *, 4> Values =
            returnedValues(Definition);
        if (!Values.empty() && llvm::all_of(Values, IsOwnMember)) {
          const Expr *Object = GetterObject;
          if (Object->getType()->isPointerType())
            ++Dereferenced;
          E = Object;
          continue;
        }
      }
    // Pointer arithmetic, like `Values + 1`, stays in what the pointer points
    // into.
    if (const auto *Binary = dyn_cast<BinaryOperator>(E);
        Binary && Binary->isAdditiveOp() && Binary->getType()->isPointerType()) {
      E = Binary->getLHS()->getType()->isPointerType() ? Binary->getLHS()
                                                         : Binary->getRHS();
      continue;
    }
    // An element of a local array lives as long as the array.
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E);
        Subscript && Subscript->getBase()->IgnoreParenImpCasts()->getType()->isArrayType()) {
      E = Subscript->getBase();
      continue;
    }
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
      const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
      // A local reference refers to what it was bound to.
      if (Variable && Variable->getType()->isReferenceType() &&
          Variable->hasLocalStorage() && !isa<ParmVarDecl>(Variable) &&
          Variable->getInit()) {
        E = Variable->getInit();
        // A temporary it extends lives as long as it, `CValue const &Value =
        // CValue{1}`; what the temporary points to is followed.
        if (const auto *Temporary = extendedTemporary(Variable)) {
          if (!Dereferenced)
            return Variable;
          E = Temporary->getSubExpr();
        }
        continue;
      }
      // A local array has its elements.
      if (Variable && Variable->getType()->isArrayType())
        return Dereferenced > 1 ? nullptr : Ref->getDecl();
      // What a local view refers to: what it was made of, the addresses of
      // locals, unless it changes. The one declared last must outlive the
      // join.
      if (Dereferenced == 1 && Variable &&
          !Variable->getType()->isPointerType() &&
          !Variable->getType()->isReferenceType() &&
          Variable->hasLocalStorage() && !isa<ParmVarDecl>(Variable)) {
        if (!Variable->getInit() || isChangedVariable(Variable))
          return nullptr;
        const Expr *Init = skipWrappers(Variable->getInit())->IgnoreParenImpCasts();
        llvm::SmallVector<const Expr *, 2> Parts;
        if (const auto *List = dyn_cast<InitListExpr>(Init))
          Parts.append(List->inits().begin(), List->inits().end());
        else if (const auto *Construct = dyn_cast<CXXConstructExpr>(Init))
          Parts.append(Construct->arg_begin(), Construct->arg_end());
        else
          return nullptr;
        const ValueDecl *Latest = nullptr;
        for (const Expr *Part : Parts) {
          // A size, like the length of `std::span<char>(Text, 8)`, refers to
          // nothing.
          if (Part->getType()->isIntegralOrEnumerationType())
            continue;
          const Expr *Inner = Part->IgnoreParenImpCasts();
          const auto *Address = dyn_cast<UnaryOperator>(Inner);
          const auto *Referred =
              Address && Address->getOpcode() == UO_AddrOf
                  ? dyn_cast<DeclRefExpr>(
                        Address->getSubExpr()->IgnoreParenImpCasts())
                  : nullptr;
          // A local array, which has its elements.
          if (const auto *Array = dyn_cast<DeclRefExpr>(Inner);
              !Referred && Array && Array->getType()->isArrayType() &&
              isa<VarDecl>(Array->getDecl()) &&
              cast<VarDecl>(Array->getDecl())->hasLocalStorage())
            Referred = Array;
          if (!Referred)
            return nullptr;
          if (!Latest || SM.isBeforeInTranslationUnit(
                             Latest->getLocation(),
                             Referred->getDecl()->getLocation()))
            Latest = Referred->getDecl();
        }
        return Latest;
      }
      // What a local pointer points to must outlive the join, not the
      // pointer: follow it to what it was initialized with, unless it changes.
      if (Dereferenced && Variable && Variable->getType()->isPointerType() &&
          Variable->hasLocalStorage() && !isa<ParmVarDecl>(Variable)) {
        if (!Variable->getInit() || isChangedVariable(Variable))
          return nullptr;
        // Still what it points to: `&Local` makes it the local, and a copy of
        // another pointer, `pCopy = pOther`, follows that one.
        E = Variable->getInit();
        continue;
      }
      // What a pointer that another one points to points to can't be told,
      // nor what a pointer or view that isn't local refers to, like of a
      // global, which may be pointed at a local.
      if (Dereferenced > 1)
        return nullptr;
      if (Dereferenced && Variable && !Variable->hasLocalStorage() &&
          !Variable->getType()->isArrayType())
        return nullptr;
      return Ref->getDecl();
    }
    if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(E)) {
      E = Cleanups->getSubExpr();
      continue;
    }
    return nullptr;
  }
  return nullptr;
}

/// `rootOfImpl`, told once for each expression while the outermost is, also
/// one reached many times, like through a chain of references made of
/// conditionals of the one before, `int &Second = bChoose ? First : First`,
/// which would otherwise be followed once per path. One reached again while
/// it is told, through a reference made of itself, `int &Alias = bChoose ?
/// Local : Alias`, can't be told.
static const ValueDecl *rootOf(const Expr *E, bool &This, bool &Temporary,
                               bool Pointee, ASTContext &Context) {
  struct CRoot {
    const ValueDecl *Decl;
    bool This;
    bool Temporary;
  };
  using CCache = llvm::DenseMap<std::pair<const Expr *, unsigned>, CRoot>;
  static thread_local CCache *s_pCache = nullptr;
  std::optional<CCache> Owned;
  if (!s_pCache)
    s_pCache = &Owned.emplace();
  const llvm::scope_exit Release([&] {
    if (Owned)
      s_pCache = nullptr;
  });
  if (auto It = s_pCache->find({E, unsigned(Pointee)}); It != s_pCache->end()) {
    This = It->second.This;
    Temporary = It->second.Temporary;
    return It->second.Decl;
  }
  s_pCache->try_emplace({E, unsigned(Pointee)}, CRoot{nullptr, false, false});
  const ValueDecl *Decl = rootOfImpl(E, This, Temporary, Pointee, Context);
  (*s_pCache)[{E, unsigned(Pointee)}] = CRoot{Decl, This, Temporary};
  return Decl;
}

void RunsToCompletionCheck::checkUse(const Expr *Use,
                                     const FunctionDecl *Function,
                                     ASTContext &Context) {
  // An entry begins to run to completion where it is called, which may be
  // anywhere, also when it is a member of a class that runs to completion.
  if (!isRunsToCompletion(Function) || isRunsToCompletionEntry(Function) ||
      !returnsFuture(Function) ||
      isInUnevaluatedOperand(DynTypedNode::create(*Use), Context) ||
      isInDiscardedBranch(DynTypedNode::create(*Use), Context))
    return;
  // Nor in a branch that a constant condition never takes, `if (false)`,
  // unless a jump to a label or a case in it can reach it.
  for (DynTypedNode Node = DynTypedNode::create(*Use);;) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty() || Parents[0].get<LambdaExpr>() ||
        Parents[0].get<FunctionDecl>())
      break;
    if (const auto *If = Parents[0].get<IfStmt>();
        If && !If->isConsteval() && If->getCond() &&
        !If->getConditionVariable() && !If->getCond()->isValueDependent())
      if (const std::optional<llvm::APSInt> Value =
              If->getCond()->getIntegerConstantExpr(Context))
        if (const Stmt *Skipped =
                Value->isZero() ? If->getThen() : If->getElse();
            Skipped && Node.get<Stmt>() == Skipped && !hasJumpTarget(Skipped))
          return;
    Node = Parents[0];
  }
  const CallExpr *Call = callOf(Use, Context);
  if (!Call) {
    diag(Use->getBeginLoc(),
         "%0 runs to completion, but is used here without being called, so "
         "that whether its callers join it can't be told")
        << diagnosticDecl(Function);
    return;
  }
  checkCall(Call, Function, Context);
}

void RunsToCompletionCheck::checkCall(const CallExpr *Call,
                                      const FunctionDecl *Function,
                                      ASTContext &Context) {
  if (!inRunsToCompletionContext(Call, Context)) {
    diag(Call->getBeginLoc(),
         "%0 runs to completion, but is called here where the code doesn't, "
         "so it can be aborted while %0 refers to its data; call it from a "
         "function that runs to completion (DMibRunsToCompletion), or one "
         "that begins to (DMibRunsToCompletionEntry)")
        << diagnosticDecl(Function);
    return;
  }
  // Before an entry switches to a concurrent actor, what it calls runs on the
  // actor of its caller, which can be destroyed.
  if (!runsOnConcurrentActor(Call, Context)) {
    diag(Call->getBeginLoc(),
         "%0 runs to completion, but is called here before the entry "
         "switches to a concurrent actor, so it runs on an actor that can be "
         "destroyed while %0 refers to its data; call it after the switch")
        << diagnosticDecl(Function);
    return;
  }
  const std::optional<RunsToCompletionJoin> Join = joinOf(Call, Context);
  if (!Join) {
    diag(Call->getBeginLoc(),
         "%0 runs to completion, but isn't joined here, so it can run after "
         "what it refers to is gone; co_await it, or collect its future into "
         "a local that an fg_AsyncDestroy guard declared after the local "
         "awaits")
        << diagnosticDecl(Function);
    return;
  }
  if (!Join->Guard)
    return;
  // What the arguments refer to must outlive the guard that joins the call.
  auto CheckArgument = [&](const Expr *Argument, QualType Type,
                           bool Object = false) {
    // Also a view or pointer given by reference, like `CStrPtr const &`, whose
    // data must outlive the join as well as the variable.
    const QualType Value = Type.isNull() ? Type : Type.getNonReferenceType();
    const bool View = !Value.isNull() && isDefaultViewType(Value, Context);
    if (!Object && !Type.isNull() && !Type->isReferenceType() &&
        !Type->isPointerType() && !View)
      return;
    auto Outlives = [&](bool Pointee) {
      bool This = false;
      bool Temporary = false;
      // What a view refers to can't be told from its construction: it must
      // be a view of a variable that outlives the join.
      const ValueDecl *Root =
          rootOf(Argument, This, Temporary, Pointee, Context);
      return This ||
             (Root && outlivesJoin(Root, *Join, Call, nullptr, Context));
    };
    // A string literal lives as long as the program, and a null pointer
    // refers to nothing.
    const Expr *Given = Argument->IgnoreParenImpCasts();
    if (isa<StringLiteral>(Given) ||
        Given->isNullPointerConstant(Context,
                                     Expr::NPC_ValueDependentIsNotNull))
      return;
    const bool Indirect =
        !Value.isNull() && (Value->isPointerType() || View);
    if (Outlives(Indirect) &&
        (!Indirect || !Type->isReferenceType() || Outlives(false)))
      return;
    // A default argument has no location of its own; the call has.
    const SourceLocation Loc = Argument->getBeginLoc().isValid()
                                   ? Argument->getBeginLoc()
                                   : Call->getBeginLoc();
    diag(Loc,
         "%0 runs to completion and is joined by %1, but this argument may "
         "not live until then; pass a local declared before %1")
        << diagnosticDecl(Function) << Join->Guard;
  };
  // The object of a member operator, also of a static one, is the first
  // argument of the call.
  // An explicit object parameter, `this CFunctor Self`, is a parameter.
  const bool ExplicitObject =
      isa<CXXMethodDecl>(Function) &&
      cast<CXXMethodDecl>(Function)->isExplicitObjectMemberFunction();
  const unsigned Offset = isa<CXXOperatorCallExpr>(Call) &&
                                  isa<CXXMethodDecl>(Function) && !ExplicitObject
                              ? 1
                              : 0;
  for (unsigned iArgument = Offset; iArgument < Call->getNumArgs();
       ++iArgument) {
    const unsigned iParam = iArgument - Offset;
    CheckArgument(Call->getArg(iArgument),
                  iParam < Function->getNumParams()
                      ? Function->getParamDecl(iParam)->getType()
                      : QualType());
  }
  // The object, also through a pointer, like `pState->f_Run()`, whose
  // pointee must outlive the join.
  if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
      Member && !cast<CXXMethodDecl>(Function)->isStatic() && !ExplicitObject)
    CheckArgument(Member->getImplicitObjectArgument(),
                  Member->getImplicitObjectArgument()->getType(), true);
  // The object of an operator, like `Functor()`, is its first argument; a
  // static operator has none.
  if (Offset == 1 && Call->getNumArgs() >= 1 &&
      !cast<CXXMethodDecl>(Function)->isStatic() && !ExplicitObject)
    CheckArgument(Call->getArg(0), QualType());
}

void RunsToCompletionCheck::checkOverride(const FunctionDecl *Function) {
  // Calls through a function it overrides that doesn't run to completion
  // aren't checked, so their callers needn't join it.
  // Also of an instantiation, `CWorker<CBase>::f_Run`, whose template can't
  // tell what it overrides, which is reported where it is written.
  const auto *Method = dyn_cast<CXXMethodDecl>(Function);
  if (!Method || !isJoinedByCallers(Method) || !returnsFuture(Method))
    return;
  for (const CXXMethodDecl *Overridden : Method->overridden_methods()) {
    if (isRunsToCompletion(Overridden))
      continue;
    diag(Method->getLocation(),
         "%0 runs to completion, but overrides %q1, which doesn't, so the "
         "callers that call it through %q1 aren't checked to join it; declare "
         "%q1 with DMibRunsToCompletion too")
        << diagnosticDecl(Method) << diagnosticDecl(Overridden);
    return;
  }
}

void RunsToCompletionCheck::checkEntry(const FunctionDecl *Function,
                                       ASTContext &Context) {
  // A template is checked in its instantiations, where the calls are resolved.
  if (Function->isDependentContext() || !isRunsToCompletionEntry(Function) ||
      !Function->getBody())
    return;
  // An entry that isn't a coroutine doesn't switch itself: what it returns
  // the future of runs where it is called, like the functions given to it.
  if (!isa<CoroutineBodyStmt>(Function->getBody())) {
    diag(Function->getLocation(),
         "%0 begins to run to completion, but isn't a coroutine, so it doesn't "
         "switch to a concurrent actor before what it runs suspends; make it a "
         "coroutine that first co_awaits fg_ContinueRunningOnActor with a "
         "concurrent actor")
        << diagnosticDecl(Function);
    return;
  }
  struct {
    const Expr *First;
  } Finder{firstSuspension(Function)};
  if (!Finder.First)
    return;
  // A jump can pass the switch.
  bool Jumps = false;
  std::function<void(const Stmt *)> FindJumps = [&](const Stmt *S) {
    if (!S || Jumps || isa<LambdaExpr>(S))
      return;
    if (isa<GotoStmt, IndirectGotoStmt, LabelStmt>(S))
      Jumps = true;
    for (const Stmt *Child : childrenOnce(S))
      FindJumps(Child);
  };
  FindJumps(Function->getBody());
  auto Report = [&] {
    diag(Finder.First->getBeginLoc(),
         "%0 begins to run to completion, but suspends here before it "
         "switches to a concurrent actor on every path; co_await "
         "fg_ContinueRunningOnActor with a concurrent actor first, "
         "unconditionally")
        << diagnosticDecl(Function);
  };
  if (Jumps) {
    Report();
    return;
  }
  // The first suspension is a statement of its own, like `co_await
  // fg_ContinueRunningOnActor(...);`, or what a declaration or `co_return`
  // awaits: within an expression, other operands can be evaluated first.
  {
    DynTypedNode Node = DynTypedNode::create(*Finder.First);
    for (int Depth = 0; Depth < 16; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        break;
      const DynTypedNode &Parent = Parents[0];
      const auto *Construct = Parent.get<CXXConstructExpr>();
      if (Parent.get<ImplicitCastExpr>() || Parent.get<ParenExpr>() ||
          Parent.get<ExprWithCleanups>() || Parent.get<MaterializeTemporaryExpr>() ||
          Parent.get<CXXBindTemporaryExpr>() ||
          (Construct && Construct->getNumArgs() == 1)) {
        Node = Parent;
        continue;
      }
      if (Parent.get<VarDecl>() || Parent.get<CoreturnStmt>() ||
          Parent.get<ReturnStmt>() || Parent.get<CompoundStmt>())
        break;
      // A statement of its own as a branch of an `if`, which the check below
      // allows for `if constexpr`.
      if (const auto *If = Parent.get<IfStmt>();
          If && (If->getThen() == Node.get<Stmt>() ||
                 If->getElse() == Node.get<Stmt>()))
        break;
      // What an assignment assigns is evaluated before what it assigns to,
      // like `Result = co_await fg_Entry()`.
      const auto *Child = Node.get<Expr>();
      if (const auto *Assign = Parent.get<BinaryOperator>();
          Assign && Assign->getOpcode() == BO_Assign &&
          Assign->getRHS() == Child) {
        Node = Parent;
        continue;
      }
      if (const auto *Assign = Parent.get<CXXOperatorCallExpr>();
          Assign && Assign->getOperator() == OO_Equal &&
          Assign->getNumArgs() == 2 && Assign->getArg(1) == Child) {
        Node = Parent;
        continue;
      }
      // So is the left side of a comma, `(co_await
      // fg_ContinueRunningOnActor(...), 1)`.
      if (const auto *Comma = Parent.get<BinaryOperator>();
          Comma && Comma->getOpcode() == BO_Comma && Comma->getLHS() == Child) {
        Node = Parent;
        continue;
      }
      Report();
      return;
    }
  }
  // The switch runs on every path: it isn't in a branch or a loop.
  for (DynTypedNode Node = DynTypedNode::create(*Finder.First);;) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      break;
    const DynTypedNode Child = Node;
    Node = Parents[0];
    if (Node.get<Stmt>() == Function->getBody() || Node.get<FunctionDecl>())
      break;
    // The branch of `if constexpr` that is kept always runs, and so does the
    // one of a constant condition, `if (true)`, that it takes.
    if (const auto *If = Node.get<IfStmt>(); If && If->isConstexpr())
      continue;
    // A coroutine never runs in a constant evaluation, so the branch that runs
    // otherwise is always taken, `if !consteval { ... }` or the `else` of `if
    // consteval`.
    if (const auto *If = Node.get<IfStmt>();
        If && If->isConsteval() &&
        Child.get<Stmt>() ==
            (If->isNegatedConsteval() ? If->getThen() : If->getElse()))
      continue;
    bool Taken = false;
    if (const auto *If = Node.get<IfStmt>();
        If && !If->isConsteval() && !If->getConditionVariable() &&
        !If->getCond()->isValueDependent() &&
        If->getCond()->EvaluateAsBooleanCondition(Taken, Context) &&
        Child.get<Stmt>() == (Taken ? If->getThen() : If->getElse()))
      continue;
    if (Node.get<IfStmt>() || Node.get<ForStmt>() || Node.get<WhileStmt>() ||
        Node.get<DoStmt>() || Node.get<CXXForRangeStmt>() ||
        Node.get<SwitchStmt>() || Node.get<AbstractConditionalOperator>() ||
        Node.get<CXXTryStmt>() || Node.get<CXXCatchStmt>()) {
      Report();
      return;
    }
    if (const auto *Binary = Node.get<BinaryOperator>();
        Binary && Binary->isLogicalOp()) {
      Report();
      return;
    }
  }
  const Expr *Operand = awaitedOperand(Finder.First);
  Operand = Operand ? skipWrappers(Operand)->IgnoreParenImpCasts() : nullptr;
  // Switching to a concurrent actor, or awaiting another entry, which does.
  if (const CallExpr *Switch =
          Operand ? asCallTo(Operand, {"fg_ContinueRunningOnActor"}) : nullptr;
      Switch && Switch->getNumArgs() >= 1 &&
      isConcurrentActor(Switch->getArg(0), Context))
    return;
  for (int Depth = 0; Operand && Depth < 8; ++Depth) {
    Operand = skipWrappers(Operand)->IgnoreImplicit()->IgnoreParenImpCasts();
    const auto *Call = dyn_cast<CallExpr>(Operand);
    if (!Call)
      break;
    if (isRunsToCompletionEntry(Call->getDirectCallee()))
      return;
    // `.f_Wrap()` of the call, and `Call % "Context"`.
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
        Operator && Operator->getOperator() == OO_Percent &&
        Operator->getNumArgs() == 2) {
      Operand = Operator->getArg(0);
      continue;
    }
    const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
    if (!Member || !Member->getMethodDecl() ||
        !Member->getMethodDecl()->getIdentifier() ||
        Member->getMethodDecl()->getName() != "f_Wrap")
      break;
    Operand = Member->getImplicitObjectArgument();
  }
  Report();
}

void RunsToCompletionCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  ASTContext &Context = *Result.Context;
  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  for (const auto &[Id, Node] : Result.Nodes.getMap())
    if (isInTemplatePattern(Node, Context))
      return;
  if (const auto *Call = Result.Nodes.getNodeAs<CallExpr>("call")) {
    const auto *Callee =
        dyn_cast<DeclRefExpr>(Call->getCallee()->IgnoreParenImpCasts());
    const auto *Function =
        Callee ? dyn_cast<FunctionDecl>(Callee->getDecl()) : nullptr;
    if (!Function || instantiatedParents(*Callee, Context).size() < 2 ||
        !isRunsToCompletion(Function) || isRunsToCompletionEntry(Function) ||
        !returnsFuture(Function) ||
        isInUnevaluatedOperand(DynTypedNode::create(*Call), Context) ||
        isInDiscardedBranch(DynTypedNode::create(*Call), Context))
      return;
    checkCall(Call, Function, Context);
    return;
  }
  if (const auto *Ref = Result.Nodes.getNodeAs<DeclRefExpr>("ref")) {
    // The switch called through a pointer, which actor it switches to can't
    // be told.
    const auto *Function = cast<FunctionDecl>(Ref->getDecl());
    if (Function->getIdentifier() &&
        Function->getName() == "fg_ContinueRunningOnActor" &&
        !callOf(Ref, Context) && inRunsToCompletionContext(Ref, Context) &&
        !isInUnevaluatedOperand(DynTypedNode::create(*Ref), Context)) {
      diag(Ref->getBeginLoc(),
           "code that runs to completion uses fg_ContinueRunningOnActor here "
           "without calling it, so whether the actor it switches to may be "
           "destroyed while it runs can't be told; call it directly with a "
           "concurrent actor");
      return;
    }
    checkUse(Ref, Function, Context);
    return;
  }
  if (const auto *Member = Result.Nodes.getNodeAs<MemberExpr>("member")) {
    checkUse(Member, cast<FunctionDecl>(Member->getMemberDecl()), Context);
    return;
  }
  if (const auto *Function = Result.Nodes.getNodeAs<FunctionDecl>("function")) {
    checkEntry(Function, Context);
    return;
  }
  if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("override")) {
    if (Method->isFirstDecl())
      checkOverride(Method);
    return;
  }
  // A transfer that isn't a switch made here, whose actor can't be told, like
  // one given as a parameter, may be to an actor that can be destroyed.
  if (const auto *Await = Result.Nodes.getNodeAs<CoawaitExpr>("await")) {
    if (!awaitsTransferOwnership(Await) ||
        !inRunsToCompletionContext(Await, Context))
      return;
    const Expr *Operand =
        skipWrappers(Await->getOperand())->IgnoreParenImpCasts();
    if (const auto *Move = dyn_cast<CallExpr>(Operand);
        Move && Move->getNumArgs() == 1 && (isMoveFunction(calleeName(Move))))
      Operand = skipWrappers(Move->getArg(0))->IgnoreParenImpCasts();
    // Made here, by a switch that is checked by itself, also kept in an
    // unchanged local.
    if (asCallTo(Operand, {"fg_ContinueRunningOnActor"}))
      return;
    // A call through a local pointer to it, reported where it is taken.
    if (const auto *Indirect = dyn_cast<CallExpr>(Operand);
        Indirect && !Indirect->getDirectCallee())
      if (const auto *Pointer = dyn_cast<DeclRefExpr>(
              Indirect->getCallee()->IgnoreParenImpCasts()))
        if (const auto *Local = dyn_cast<VarDecl>(Pointer->getDecl());
            Local && Local->getInit()) {
          bool TakesSwitch = false;
          std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
            if (!S || TakesSwitch)
              return;
            if (const auto *Function = dyn_cast<DeclRefExpr>(S);
                Function && Function->getDecl()->getIdentifier() &&
                Function->getDecl()->getName() == "fg_ContinueRunningOnActor")
              TakesSwitch = true;
            for (const Stmt *Child : childrenOnce(S))
              Find(Child);
          };
          Find(Local->getInit());
          if (TakesSwitch)
            return;
        }
    // Also through local references to it, `auto &Alias = Transfer`.
    // Unchanged, also through the references, other than by binding the
    // last of them to it.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Operand)) {
      const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
      const DeclRefExpr *Bound = nullptr;
      bool AliasChanged = false;
      for (int Depth = 0;
           Local && Local->getType()->isReferenceType() &&
           Local->hasLocalStorage() && Local->getInit() && Depth < 64;
           ++Depth) {
        AliasChanged |= isChangedVariable(Local);
        Bound = dyn_cast<DeclRefExpr>(Local->getInit()->IgnoreParenImpCasts());
        Local = Bound ? dyn_cast<VarDecl>(Bound->getDecl()) : nullptr;
      }
      if (Local && !isa<ParmVarDecl>(Local) && Local->hasLocalStorage() &&
          Local->getInit() && !AliasChanged &&
          !(Bound ? isChangedOtherThanBy(Bound) : isChangedVariable(Local)) &&
          asCallTo(Local->getInit(), {"fg_ContinueRunningOnActor"}))
        return;
    }
    diag(Await->getBeginLoc(),
         "code that runs to completion switches here to an actor that can't "
         "be told, which may be destroyed while it runs and abort it; switch "
         "with fg_ContinueRunningOnActor to a concurrent actor");
    return;
  }
  if (const auto *Switch = Result.Nodes.getNodeAs<CallExpr>("switch")) {
    if (Switch->getNumArgs() < 1 ||
        isConcurrentActor(Switch->getArg(0), Context) ||
        !inRunsToCompletionContext(Switch, Context))
      return;
    // Not one kept in a local that nothing uses, `auto Switch =
    // fg_ContinueRunningOnActor(Other);`, which is never awaited: only its
    // await switches.
    DynTypedNode Node = DynTypedNode::create(*Switch);
    const VarDecl *Kept = nullptr;
    for (int Depth = 0; Depth < 8 && !Kept; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        break;
      Node = Parents[0];
      if (const auto *Variable = Node.get<VarDecl>())
        Kept = Variable;
      else if (!Node.get<ImplicitCastExpr>() && !Node.get<FullExpr>() &&
               !Node.get<MaterializeTemporaryExpr>() &&
               !Node.get<CXXBindTemporaryExpr>() &&
               !Node.get<CXXConstructExpr>() && !Node.get<ParenExpr>())
        break;
    }
    // Nor one used only where that can't run, in a branch that is discarded
    // or never taken, `if constexpr (false) co_await fg_Move(Switch);`, or
    // only discarded, `(void)Switch;`.
    if (Kept && Kept->hasLocalStorage() && !isa<ParmVarDecl>(Kept))
      if (const auto *Function =
              dyn_cast_or_null<FunctionDecl>(Kept->getParentFunctionOrMethod());
          Function && Function->getBody()) {
        bool Used = false;
        std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
          if (!S || Used)
            return;
          if (const auto *Discard = dyn_cast<ExplicitCastExpr>(S);
              Discard && Discard->getType()->isVoidType())
            if (const auto *Ref = dyn_cast<DeclRefExpr>(
                    Discard->getSubExpr()->IgnoreParenImpCasts());
                Ref && Ref->getDecl() == Kept)
              return;
          if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
              Ref && Ref->getDecl() == Kept) {
            Used = true;
            return;
          }
          for (const Stmt *Child : evaluatedChildren(S, Context))
            Visit(Child);
        };
        Visit(Function->getBody());
        if (!Used)
          return;
      }
    diag(Switch->getBeginLoc(),
         "code that runs to completion switches here to an actor that may be "
         "destroyed while it runs, which would abort it; stay on concurrent "
         "actors, and call other actors instead");
  }
}

} // namespace clang::tidy::malterlib
