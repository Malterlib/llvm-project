//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CoroutineActorReferenceCheck.h"
#include "ActorAnnotations.h"
#include "CoroutineUtils.h"
#include "NamingUtils.h"
#include "OncePerTypeVisitor.h"
#include "RunsToCompletion.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <deque>
#include <functional>

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

/// The assignments that \p Statement, an `if` or `switch`, always runs: of
/// its initializer, `if (pValue = mp_Items.f_FindEqual(0); !pValue)`, not a
/// declaration, and of its condition, `if (!(pValue = mp_Items.f_FindEqual(
/// 0)))`, also tested or compared, which runs before either branch.
static llvm::SmallVector<const BinaryOperator *, 2>
alwaysRunAssignments(const Stmt *Statement) {
  llvm::SmallVector<const BinaryOperator *, 2> Assignments;
  const Stmt *Init = nullptr;
  const Expr *Condition = nullptr;
  if (const auto *If = dyn_cast<IfStmt>(Statement)) {
    Init = If->getInit();
    Condition = If->getConditionVariable() ? nullptr : If->getCond();
  } else if (const auto *Switch = dyn_cast<SwitchStmt>(Statement)) {
    Init = Switch->getInit();
    Condition = Switch->getConditionVariable() ? nullptr : Switch->getCond();
  }
  if (const auto *Assign = dyn_cast_or_null<BinaryOperator>(
          isa_and_nonnull<Expr>(Init) ? cast<Expr>(Init)->IgnoreImplicit()
                                      : nullptr);
      Assign && Assign->getOpcode() == BO_Assign)
    Assignments.push_back(Assign);
  for (int Depth = 0; Condition && Depth < 8; ++Depth) {
    Condition = Condition->IgnoreParenImpCasts();
    if (const auto *Assign = dyn_cast<BinaryOperator>(Condition);
        Assign && Assign->getOpcode() == BO_Assign) {
      Assignments.push_back(Assign);
      break;
    }
    if (const auto *Not = dyn_cast<UnaryOperator>(Condition);
        Not && Not->getOpcode() == UO_LNot)
      Condition = Not->getSubExpr();
    else if (const auto *Compare = dyn_cast<BinaryOperator>(Condition);
             Compare && Compare->isEqualityOp())
      Condition = Compare->getLHS();
    else
      break;
  }
  return Assignments;
}

/// Whether \p Member is named for telling that what holds it was removed,
/// like `m_pDestroyed` or `m_pDeleted`.
static bool isRemovalFlagName(const ValueDecl *Member) {
  if (!Member || !Member->getIdentifier())
    return false;
  const StringRef Name = Member->getName();
  return Name.contains("Destroyed") || Name.contains("Deleted") ||
         Name.contains("Removed");
}

/// Whether \p Type points or refers to the object of an actor, also a
/// reference to such a pointer, `CApp *const &`.
static bool refersToActorClass(QualType Type) {
  if (Type->isReferenceType() && Type.getNonReferenceType()->isPointerType())
    Type = Type.getNonReferenceType();
  return (Type->isPointerType() || Type->isReferenceType()) &&
         isActorClass(Type->getPointeeType()->getAsCXXRecordDecl());
}

/// Whether \p Type is a handle that refers to what it was made from, like
/// the `TCMapResult` that `m_Items(Key)` returns, which holds a reference to
/// the entry.
static bool isReferenceHandle(QualType Type) {
  if (Type->isPointerType() || Type->isReferenceType())
    return false;
  // Also one it inherits, each base once, which a hierarchy of virtual bases
  // shares.
  llvm::SmallVector<const CXXRecordDecl *, 8> Work{Type->getAsCXXRecordDecl()};
  llvm::SmallPtrSet<const CXXRecordDecl *, 16> Visited;
  while (!Work.empty()) {
    const CXXRecordDecl *Record = Work.pop_back_val();
    if (!Record || !Record->hasDefinition())
      continue;
    Record = Record->getDefinition();
    if (!Visited.insert(Record).second)
      continue;
    // Not one of the class it is in, `TCMap &mp_Map` of `TCMap::CKeys`, a
    // view of that, which `mp_Items.f_Keys()` gives of the collection itself,
    // not of an entry.
    auto OfEnclosing = [&](const FieldDecl *Field) {
      const CXXRecordDecl *Referred =
          Field->getType().getNonReferenceType()->getAsCXXRecordDecl();
      for (const DeclContext *Context = Record->getDeclContext();
           Referred && Context; Context = Context->getParent())
        if (const auto *Enclosing = dyn_cast<CXXRecordDecl>(Context);
            Enclosing &&
            Enclosing->getCanonicalDecl() == Referred->getCanonicalDecl())
          return true;
      return false;
    };
    if (llvm::any_of(Record->fields(), [&](const FieldDecl *Field) {
          return Field->getType()->isReferenceType() && !OfEnclosing(Field);
        }))
      return true;
    for (const CXXBaseSpecifier &Base : Record->bases())
      Work.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  return false;
}

/// The flag that \p Binary compares to be set, like `*pDeleted` of
/// `*pDeleted == true` or `*pDeleted != false`, also with the numbers a flag
/// can be, `*pDeleted != 0` or `*pDeleted == 1`.
static const Expr *comparedToBeSet(const BinaryOperator *Binary) {
  if (Binary->getOpcode() != BO_EQ && Binary->getOpcode() != BO_NE)
    return nullptr;
  const bool Set = Binary->getOpcode() == BO_EQ;
  // The value of a side that is `true`, `false`, `1` or `0`.
  auto Constant = [](const Expr *Side) -> std::optional<bool> {
    Side = Side->IgnoreParenImpCasts();
    if (const auto *Bool = dyn_cast<CXXBoolLiteralExpr>(Side))
      return Bool->getValue();
    if (const auto *Number = dyn_cast<IntegerLiteral>(Side);
        Number && Number->getValue().ule(1))
      return !Number->getValue().isZero();
    return std::nullopt;
  };
  if (const std::optional<bool> Value = Constant(Binary->getRHS());
      Value && *Value == Set)
    return Binary->getLHS();
  if (const std::optional<bool> Value = Constant(Binary->getLHS());
      Value && *Value == Set)
    return Binary->getRHS();
  return nullptr;
}

/// Whether \p Type is an optional, `TCOptional` or `std::optional`, whose
/// value can be cleared, `mp_Value.f_Clear()`, unlike what an owning pointer
/// points to.
static bool isOptionalHolder(QualType Type) {
  const StringRef Holder = recordName(Type.getNonReferenceType());
  return Holder == "TCOptional" || Holder == "optional";
}

/// Whether \p Values, what a function gives back, are only members of the
/// object \p Object points to, or of its own object when null, `m_Value`,
/// `&_pThis->m_Fixed` or `m_Inner.m_Value`, which live with it: not one a
/// pointer or reference member refers to.
static bool givesOnlyMembers(llvm::ArrayRef<const Expr *> Values,
                             const ValueDecl *Object) {
  return llvm::all_of(Values, [&](const Expr *Value) {
    const Expr *Part = Value->IgnoreParenImpCasts();
    if (const auto *Address = dyn_cast<UnaryOperator>(Part);
        Address && Address->getOpcode() == UO_AddrOf)
      Part = Address->getSubExpr()->IgnoreParenImpCasts();
    bool Member = false;
    // Also an element of an array member, `return m_Axis[_iAxis];`, which
    // lives with it.
    auto PastElements = [&] {
      while (const auto *Element = dyn_cast<ArraySubscriptExpr>(Part)) {
        const Expr *Array = Element->getBase()->IgnoreParenImpCasts();
        if (!Array->getType()->isConstantArrayType())
          break;
        Part = Array;
      }
    };
    PastElements();
    // Also what an owning pointer member points to, `*_pThis->mp_pInternal`,
    // which lives with the object, as its lookups tell.
    if (const auto *Owned = dyn_cast<CXXOperatorCallExpr>(Part);
        Owned && Owned->getNumArgs() == 1 &&
        (Owned->getOperator() == OO_Star || Owned->getOperator() == OO_Arrow) &&
        isa<MemberExpr>(Owned->getArg(0)->IgnoreParenImpCasts()) &&
        Owned->getArg(0)->getType()->isRecordType() &&
        !isOptionalHolder(Owned->getArg(0)->getType()))
      Part = Owned->getArg(0)->IgnoreParenImpCasts();
    while (const auto *Access = dyn_cast<MemberExpr>(Part)) {
      if (!isa<FieldDecl>(Access->getMemberDecl()) ||
          Access->getMemberDecl()->getType()->isReferenceType() ||
          Access->getMemberDecl()->getType()->isPointerType())
        return false;
      Member = true;
      Part = Access->getBase()->IgnoreParenImpCasts();
      if (Access->isArrow())
        break;
      PastElements();
    }
    if (!Object)
      return Member && isa<CXXThisExpr>(Part);
    const auto *Ref = dyn_cast<DeclRefExpr>(Part);
    return Member && Ref && Ref->getDecl() == Object;
  });
}

/// Whether \p Type can hold a lookup: a pointer, a reference, a handle or an
/// iterator.
static bool canHoldLookup(QualType Type) {
  return Type->isPointerType() || Type->isReferenceType() ||
         isReferenceHandle(Type) || isIteratorType(Type);
}

CoroutineActorReferenceCheck::CoroutineActorReferenceCheck(
    StringRef Name, ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      FreshKeyFunctions(listOption("FreshKeyFunctions", "")),
      NodeStableContainers(
          listOption("NodeStableContainers", BuiltInNodeStableContainers)) {
  splitOptionList(FreshKeyFunctions, FreshKeyFunctionList);
  splitOptionList(NodeStableContainers, NodeStableContainerList);
}

void CoroutineActorReferenceCheck::storeCheckOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "FreshKeyFunctions", FreshKeyFunctions);
  Options.store(Opts, "NodeStableContainers", NodeStableContainers);
}

llvm::SmallVector<const Expr *, 4>
CoroutineActorReferenceCheck::valuesOf(const VarDecl *Local) {
  llvm::SmallVector<const Expr *, 4> Values;
  if (Local->getInit())
    Values.push_back(Local->getInit());
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Local->getParentFunctionOrMethod());
  if (!Function)
    return Values;
  auto [It, Inserted] = Assignments.try_emplace(Function);
  if (Inserted) {
    auto &Assigned = It->second;
    std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
      if (!S)
        return;
      if (const auto *Assign = dyn_cast<BinaryOperator>(S);
          Assign && Assign->isAssignmentOp())
        if (const auto *Target =
                dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts()))
          if (const auto *Variable = dyn_cast<VarDecl>(Target->getDecl()))
            Assigned[Variable].push_back(Assign->getRHS());
      for (const Stmt *Child : childrenOnce(S))
        Find(Child);
    };
    Find(Function->getBody());
  }
  if (auto Found = It->second.find(Local); Found != It->second.end())
    llvm::append_range(Values, Found->second);
  return Values;
}

/// Whether \p Ref, of a local pointer, only names its storage, without
/// reading what it points to: its address, `&pEntry`, or a reference bound
/// to it, `int *&pAlias = pEntry`, whose uses are followed by themselves. Not
/// the address of what a reference refers to, `&Entry` of `CEntry &Entry`,
/// which is of the data.
static bool namesOnlyStorage(const DeclRefExpr *Ref, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*Ref);
  for (int Depth = 0; Depth < 8; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    if (Parents[0].get<ParenExpr>()) {
      Node = Parents[0];
      continue;
    }
    if (const auto *Address = Parents[0].get<UnaryOperator>())
      return Address->getOpcode() == UO_AddrOf &&
             !Ref->getDecl()->getType()->isReferenceType();
    if (const auto *Alias = Parents[0].get<VarDecl>())
      return Alias->getType()->isReferenceType();
    return false;
  }
  return false;
}

/// What \p S, a call whose arguments suspend, uses after they did, though it
/// is evaluated before: the object of a member function or an operator, and
/// the arguments given by reference or pointer, `fg_Read(Entry, co_await
/// fg_Suspend())`. The left side of an assignment, also an overloaded one, is
/// evaluated after its right side, `mp_Items[0] = co_await fg_Suspend()`.
/// The pointer a subscript indexes is evaluated before its index when written
/// first, `(&mp_Items[0])[(co_await fg_Suspend(), 0)]`, and after it when
/// written last, `(co_await fg_Suspend(), 0)[&mp_Items[0]]`.
static llvm::SmallVector<const Expr *, 2>
earlyCallOperands(const Stmt *S, const ASTContext &Context) {
  llvm::SmallVector<const Expr *, 2> Operands;
  if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(S)) {
    if (Subscript->getLHS() == Subscript->getBase() &&
        suspendsIn(Subscript->getRHS(), Context) &&
        !suspendsIn(Subscript->getLHS(), Context) &&
        Subscript->getBase()->getType()->isPointerType())
      Operands.push_back(Subscript->getBase());
    return Operands;
  }
  // A constructor uses what it is given by reference after the arguments
  // after it, `CCopy{Entry, co_await fg_Suspend()}`. Braced ones are
  // evaluated in order, so what follows the last that suspends is looked up
  // after, `CCopy{co_await fg_Suspend(), Entry}`.
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(S)) {
    unsigned nEarly = 0;
    for (unsigned iArgument = 0; iArgument < Construct->getNumArgs();
         ++iArgument)
      if (suspendsIn(Construct->getArg(iArgument), Context))
        nEarly = Construct->isListInitialization() ? iArgument
                                                   : Construct->getNumArgs();
    // Also one that suspends itself, `CCopy{mp_Items[co_await fg_Key()],
    // co_await fg_Suspend()}`, which looks up after its own suspension but
    // before those of the arguments after it, or, unordered, of the others.
    for (unsigned iArgument = 0; iArgument < nEarly; ++iArgument) {
      const QualType Type = argumentParameterType(Construct, iArgument);
      const bool OthersSuspend = llvm::any_of(
          llvm::seq<unsigned>(0, Construct->getNumArgs()),
          [&](unsigned iOther) {
            return iOther != iArgument &&
                   (Construct->isListInitialization() ? iOther > iArgument
                                                      : true) &&
                   suspendsIn(Construct->getArg(iOther), Context);
          });
      if (!Type.isNull() &&
          (Type->isReferenceType() || Type->isPointerType()) &&
          (!suspendsIn(Construct->getArg(iArgument), Context) || OthersSuspend))
        Operands.push_back(Construct->getArg(iArgument));
    }
    return Operands;
  }
  const auto *Call = dyn_cast<CallExpr>(S);
  if (!Call || llvm::none_of(Call->arguments(), [&](const Expr *Argument) {
        return suspendsIn(Argument, Context);
      }))
    return Operands;
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
      Operator && Operator->isAssignmentOp() && Operator->getNumArgs() == 2) {
    // The parameter of the value, also after an explicit object,
    // `operator=(this CValue &_Self, int const &_Value)`.
    const QualType Type = argumentParameterType(Operator, 1);
    if (!suspendsIn(Operator->getArg(1), Context) && !Type.isNull() &&
        (Type->isReferenceType() || Type->isPointerType()))
      Operands.push_back(Operator->getArg(1));
    return Operands;
  }
  if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
      Member && Member->getImplicitObjectArgument())
    Operands.push_back(Member->getImplicitObjectArgument());
  const FunctionDecl *Callee = Call->getDirectCallee();
  const unsigned Offset =
      isa<CXXOperatorCallExpr>(Call) && hasImplicitObject(Callee) ? 1 : 0;
  if (Offset == 1)
    Operands.push_back(Call->getArg(0));
  // Also through a pointer to a function, `fUse(Entry, co_await ...)`.
  for (unsigned iArgument = Offset; iArgument < Call->getNumArgs();
       ++iArgument) {
    const QualType Type = argumentParameterType(Call, iArgument);
    if (Type.isNull() || !(Type->isReferenceType() || Type->isPointerType()))
      continue;
    const Expr *Argument = Call->getArg(iArgument);
    // Also a pointer moved by an offset that suspends, `pEntry + (co_await
    // fg_Suspend(), 0)`, which is read before.
    for (int Depth = 0; Depth < 16 && suspendsIn(Argument, Context); ++Depth) {
      Argument = Argument->IgnoreParenImpCasts();
      if (const auto *Offset = dyn_cast<BinaryOperator>(Argument);
          Offset && Offset->isAdditiveOp() &&
          Offset->getType()->isPointerType())
        Argument = Offset->getLHS()->getType()->isPointerType()
                       ? Offset->getLHS()
                       : Offset->getRHS();
      else if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Argument))
        Argument = Cast->getSubExpr();
      else
        break;
    }
    // Also one that suspends itself, `fg_Use(mp_Items[co_await fg_Key()],
    // co_await fg_Suspend())`, when another suspends too, which can run after
    // it looks up.
    if (!suspendsIn(Argument, Context) ||
        llvm::any_of(llvm::seq<unsigned>(Offset, Call->getNumArgs()),
                     [&](unsigned iOther) {
                       return iOther != iArgument &&
                              suspendsIn(Call->getArg(iOther), Context);
                     }))
      Operands.push_back(Argument);
  }
  return Operands;
}

/// The member of the object of the actor that \p E, a lookup or what a
/// removal is called on, goes through first, like `mp_Items` of
/// `mp_Items.f_FindEqual(1)` or `&mp_Items[1].m_Value`, also through a local
/// reference to it, `auto &Items = mp_Items`, or an unchanged local pointer,
/// `auto *pItems = &mp_Items`. Null when that can't be told.
static const FieldDecl *outermostField(const Expr *E) {
  for (int Depth = 0; E && Depth < 16; ++Depth) {
    E = skipWrappers(E)->IgnoreParenImpCasts();
    if (const auto *Member = dyn_cast<MemberExpr>(E)) {
      const Expr *Base = Member->getBase()->IgnoreParenImpCasts();
      if (isa<CXXThisExpr>(Base))
        return dyn_cast<FieldDecl>(Member->getMemberDecl());
      E = Base;
    } else if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E))
      E = Call->getImplicitObjectArgument();
    else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
             Operator && Operator->getNumArgs() >= 1)
      E = Operator->getArg(0);
    else if (const auto *Unary = dyn_cast<UnaryOperator>(E);
             Unary && (Unary->getOpcode() == UO_AddrOf ||
                       Unary->getOpcode() == UO_Deref))
      E = Unary->getSubExpr();
    else if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E))
      E = Subscript->getBase();
    else if (const auto *Offset = dyn_cast<BinaryOperator>(E);
             Offset && Offset->isAdditiveOp() &&
             Offset->getType()->isPointerType())
      E = Offset->getLHS()->getType()->isPointerType() ? Offset->getLHS()
                                                      : Offset->getRHS();
    else if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E))
      E = Cast->getSubExpr();
    else if (const auto *Ref = dyn_cast<DeclRefExpr>(E);
             Ref && isa<VarDecl>(Ref->getDecl()) &&
             cast<VarDecl>(Ref->getDecl())->hasLocalStorage() &&
             !isa<ParmVarDecl>(Ref->getDecl()) &&
             cast<VarDecl>(Ref->getDecl())->getInit() &&
             (Ref->getDecl()->getType()->isReferenceType() ||
              (Ref->getDecl()->getType()->isPointerType() &&
               !isChangedVariable(cast<VarDecl>(Ref->getDecl())))))
      E = cast<VarDecl>(Ref->getDecl())->getInit();
    else
      return nullptr;
  }
  return nullptr;
}

/// The collection that \p Object designates, when it is a field declared
/// with `DMibActorEntries`, also through local references and pointers to it,
/// through up to 64 of them and addresses and dereferences, and both values of
/// up to 4 nested conditionals, \p Branches, which each look at two.
static const FieldDecl *annotatedCollection(const Expr *Object,
                                            const Expr *&Owner, int Depth = 0,
                                            int Branches = 0) {
  if (!Object || Depth > 64 || Branches > 4)
    return nullptr;
  const Expr *E = skipWrappers(Object)->IgnoreParenImpCasts();
  if (const auto *Member = dyn_cast<MemberExpr>(E)) {
    const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
    Owner = Member->getBase()->IgnoreParenImpCasts();
    return Field && annotatedPhases(Field, AnnotationActorEntries) ? Field
                                                                   : nullptr;
  }
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary && (Unary->getOpcode() == UO_Deref ||
                Unary->getOpcode() == UO_AddrOf))
    return annotatedCollection(Unary->getSubExpr(), Owner, Depth + 1, Branches);
  // A conditional, `(bChoose ? mp_Items : mp_Other)`, when both values are
  // collections that lose entries in the same phases of the same object, what
  // a comma gives, `(void(0), mp_Items)`, and a cast to a reference to it.
  if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E)) {
    const Expr *TrueOwner = nullptr;
    const Expr *FalseOwner = nullptr;
    const FieldDecl *TrueField = annotatedCollection(
        Conditional->getTrueExpr(), TrueOwner, Depth + 1, Branches + 1);
    const FieldDecl *FalseField = annotatedCollection(
        Conditional->getFalseExpr(), FalseOwner, Depth + 1, Branches + 1);
    if (!TrueField || !FalseField ||
        annotatedPhases(TrueField, AnnotationActorEntries) !=
            annotatedPhases(FalseField, AnnotationActorEntries))
      return nullptr;
    auto Designated = [](const Expr *Of) -> const void * {
      Of = Of ? Of->IgnoreParenImpCasts() : nullptr;
      if (isa_and_nonnull<CXXThisExpr>(Of))
        return Of->getType().getTypePtr();
      if (const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Of))
        return Ref->getDecl();
      return nullptr;
    };
    const void *Object = Designated(TrueOwner);
    if (!Object || Object != Designated(FalseOwner))
      return nullptr;
    Owner = TrueOwner;
    return TrueField;
  }
  if (const auto *Comma = dyn_cast<BinaryOperator>(E);
      Comma && Comma->getOpcode() == BO_Comma)
    return annotatedCollection(Comma->getRHS(), Owner, Depth + 1, Branches);
  if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
      Cast && Cast->isGLValue())
    return annotatedCollection(Cast->getSubExpr(), Owner, Depth + 1, Branches);
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
    if (!Variable || !Variable->hasLocalStorage() || isa<ParmVarDecl>(Variable))
      return nullptr;
    if (Variable->getInit() &&
        (Variable->getType()->isReferenceType() ||
         (Variable->getType()->isPointerType() &&
          !isChangedVariable(Variable))))
      return annotatedCollection(Variable->getInit(), Owner, Depth + 1,
                                 Branches);
    // A pointer changed, what it was last assigned on every path, like
    // `pItems = pSaved`.
    if (Variable->getType()->isPointerType())
      if (const Expr *Value = assignedValueAt(Variable, Ref))
        return annotatedCollection(Value, Owner, Depth + 1, Branches);
  }
  return nullptr;
}

/// The annotated collection that the lookup \p Init finds an entry of, and
/// the key it finds it by. A member of the entry, like `&pEntry->m_Value`,
/// lives as long as the entry.
static const FieldDecl *lookupCollection(const Expr *Init, const Expr *&Key,
                                         const Expr *&Owner, int Depth = 0) {
  if (!Init || Depth > 8)
    return nullptr;
  // The value of a pointer member of an entry, `int *pValue =
  // mp_Items[Key].m_pValue`, points to an object of its own, which can be
  // replaced while the entry stays.
  for (const Expr *Read = Init;;) {
    if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(Read))
      Read = Cleanups->getSubExpr();
    else if (const auto *Paren = dyn_cast<ParenExpr>(Read))
      Read = Paren->getSubExpr();
    else if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Read)) {
      if (Cast->getCastKind() == CK_LValueToRValue) {
        // Also through a local reference to the member, `auto &pMember =
        // mp_Items[Key].m_pValue; int *pValue = pMember`, a chain of them
        // each looked at once.
        const Expr *Read = Cast->getSubExpr()->IgnoreParens();
        for (int Level = 0; Level < 32; ++Level) {
          const auto *Ref = dyn_cast<DeclRefExpr>(Read);
          const auto *Alias = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          if (!Alias || !Alias->getType()->isReferenceType() ||
              !Alias->hasLocalStorage() || isa<ParmVarDecl>(Alias) ||
              !Alias->getInit())
            break;
          Read = Alias->getInit()->IgnoreParenImpCasts();
        }
        // Also through a cast to a reference to it, `static_cast<int *&>(
        // mp_Items[Key].m_pValue)`, and a pointer to the member, `mp_Items[
        // Key].*pmValue`.
        while (const auto *Cast = dyn_cast<ExplicitCastExpr>(Read)) {
          if (!Cast->isGLValue())
            break;
          Read = Cast->getSubExpr()->IgnoreParenImpCasts();
        }
        const auto *Member = dyn_cast<MemberExpr>(Read);
        if (Member && isa<FieldDecl>(Member->getMemberDecl()) &&
            Member->getType()->isPointerType())
          return nullptr;
        if (const auto *Through = dyn_cast<BinaryOperator>(Read);
            Through && Through->isPtrMemOp() &&
            Through->getType()->isPointerType())
          return nullptr;
        break;
      }
      Read = Cast->getSubExpr();
    } else
      break;
  }
  const Expr *E = skipWrappers(Init)->IgnoreParenImpCasts();
  // The last of a comma, `(void(0), mp_Items[1])`, and either value of a
  // conditional, of the same collection of the same object, `bFirst ?
  // mp_Items[1] : mp_Items[2]`, which have no one key.
  if (const auto *Comma = dyn_cast<BinaryOperator>(E);
      Comma && Comma->getOpcode() == BO_Comma)
    return lookupCollection(Comma->getRHS(), Key, Owner, Depth + 1);
  if (const auto *Conditional = dyn_cast<ConditionalOperator>(E)) {
    const Expr *TrueKey = nullptr;
    const Expr *TrueOwner = nullptr;
    const FieldDecl *True = lookupCollection(Conditional->getTrueExpr(),
                                             TrueKey, TrueOwner, Depth + 1);
    const FieldDecl *False = lookupCollection(Conditional->getFalseExpr(), Key,
                                              Owner, Depth + 1);
    // Also of two collections of the object of the same type that lose entries
    // in the same phases, `bFirst ? mp_First[1] : mp_Second[1]`, which the
    // same guards keep.
    auto Alike = [&] {
      if (True == False)
        return true;
      const std::optional<unsigned> TruePhases =
          annotatedPhases(True, AnnotationActorEntries);
      const std::optional<unsigned> FalsePhases =
          annotatedPhases(False, AnnotationActorEntries);
      return TruePhases && TruePhases == FalsePhases &&
             True->getASTContext().hasSameType(True->getType(),
                                               False->getType());
    };
    if (!True || !False || !Alike() ||
        isOwnActorObject(TrueOwner) != isOwnActorObject(Owner) ||
        !isOwnActorObject(Owner))
      return nullptr;
    Key = nullptr;
    return True;
  }
  // What a member of an entry points to, like `*Entry.m_pValue`, is an object
  // of its own, which the member can be pointed elsewhere from while the
  // entry stays, also through a local made from it, `auto &pValue =
  // Entry.m_pValue`.
  auto IsMemberOfEntry = [](const Expr *Pointer) {
    for (int Level = 0; Level < 4; ++Level) {
      const Expr *E = skipWrappers(Pointer)->IgnoreParenImpCasts();
      while (const auto *Cast = dyn_cast<ExplicitCastExpr>(E)) {
        if (!Cast->isGLValue())
          break;
        E = Cast->getSubExpr()->IgnoreParenImpCasts();
      }
      if (const auto *Member = dyn_cast<MemberExpr>(E))
        return isa<FieldDecl>(Member->getMemberDecl());
      if (const auto *Through = dyn_cast<BinaryOperator>(E);
          Through && Through->isPtrMemOp())
        return true;
      const auto *Ref = dyn_cast<DeclRefExpr>(E);
      const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      if (!Variable || !Variable->getInit() || isa<ParmVarDecl>(Variable) ||
          !(Variable->hasLocalStorage() || Variable->isInitCapture()) ||
          !(Variable->getType()->isReferenceType() ||
            !isChangedVariable(Variable)))
        return false;
      Pointer = Variable->getInit();
    }
    return false;
  };
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary && (Unary->getOpcode() == UO_AddrOf ||
                Unary->getOpcode() == UO_Deref)) {
    if (Unary->getOpcode() == UO_Deref && IsMemberOfEntry(Unary->getSubExpr()))
      return nullptr;
    return lookupCollection(Unary->getSubExpr(), Key, Owner, Depth + 1);
  }
  if (const auto *Member = dyn_cast<MemberExpr>(E);
      Member && isa<FieldDecl>(Member->getMemberDecl())) {
    if (Member->isArrow() && IsMemberOfEntry(Member->getBase()))
      return nullptr;
    return lookupCollection(Member->getBase(), Key, Owner, Depth + 1);
  }
  // Also through a cast to a reference, `static_cast<int &>(mp_Items[1].m_Value)`,
  // and a pointer to a member, `mp_Items[1].*pmValue`.
  if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E); Cast && Cast->isGLValue())
    return lookupCollection(Cast->getSubExpr(), Key, Owner, Depth + 1);
  if (const auto *Pointer = dyn_cast<BinaryOperator>(E);
      Pointer && Pointer->isPtrMemOp() && !Pointer->getType()->isFunctionType() &&
      !Pointer->getType()->isSpecificBuiltinType(BuiltinType::BoundMember))
    return Pointer->getOpcode() == BO_PtrMemI &&
                   IsMemberOfEntry(Pointer->getLHS())
               ? nullptr
               : lookupCollection(Pointer->getLHS(), Key, Owner, Depth + 1);
  // An element of an array in the entry, `mp_Items[1].m_Values[0]`, and the
  // entry offset, `&mp_Items[1] + 0`, are in the entry.
  if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E);
      Subscript &&
      Subscript->getBase()->IgnoreParenImpCasts()->getType()->isArrayType())
    return lookupCollection(Subscript->getBase(), Key, Owner, Depth + 1);
  if (const auto *Offset = dyn_cast<BinaryOperator>(E);
      Offset && Offset->isAdditiveOp() && Offset->getType()->isPointerType())
    return lookupCollection(Offset->getLHS()->getType()->isPointerType()
                                ? Offset->getLHS()
                                : Offset->getRHS(),
                            Key, Owner, Depth + 1);
  // A handle made from a single lookup, like `CHandle Handle{*pEntry}`, or a
  // copy of one.
  if (const auto *List = dyn_cast<InitListExpr>(E);
      List && isReferenceHandle(List->getType()))
    return List->getNumInits() == 1
               ? lookupCollection(List->getInit(0), Key, Owner, Depth + 1)
               : nullptr;
  // Also with parentheses, `CHandle Handle(*pEntry)` of an aggregate.
  if (const auto *List = dyn_cast<CXXParenListInitExpr>(E);
      List && isReferenceHandle(List->getType()))
    return List->getInitExprs().size() == 1
               ? lookupCollection(List->getInitExprs()[0], Key, Owner,
                                  Depth + 1)
               : nullptr;
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(E);
      Construct && Construct->getNumArgs() == 1 &&
      isReferenceHandle(Construct->getType()) &&
      Construct->getConstructor()->isCopyOrMoveConstructor())
    return lookupCollection(Construct->getArg(0), Key, Owner, Depth + 1);
  // A handle made with more arguments, of which one is bound to a reference
  // or pointer, like `CHandle Handle(Entry, Tag)`.
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(E);
      Construct && isReferenceHandle(Construct->getType())) {
    const CXXConstructorDecl *Constructor = Construct->getConstructor();
    const Expr *Borrowed = nullptr;
    for (unsigned iArgument = 0; iArgument < Construct->getNumArgs() &&
                                 iArgument < Constructor->getNumParams();
         ++iArgument) {
      const QualType Type = Constructor->getParamDecl(iArgument)->getType();
      if (!Type->isReferenceType() && !Type->isPointerType())
        continue;
      if (Borrowed)
        return nullptr;
      Borrowed = Construct->getArg(iArgument);
    }
    return Borrowed ? lookupCollection(Borrowed, Key, Owner, Depth + 1)
                    : nullptr;
  }
  if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E)) {
    if ((Call->getOperator() == OO_Subscript ||
         Call->getOperator() == OO_Call) &&
        Call->getNumArgs() >= 1) {
      // Not a value it gives, like `int operator[](int) const`.
      if (!Call->isGLValue() && !canHoldLookup(Call->getType()))
        return nullptr;
      Key = Call->getNumArgs() >= 2 ? Call->getArg(1) : nullptr;
      return annotatedCollection(Call->getArg(0), Owner);
    }
    if ((Call->getOperator() == OO_Star || Call->getOperator() == OO_Arrow) &&
        Call->getNumArgs() == 1)
      return IsMemberOfEntry(Call->getArg(0))
                 ? nullptr
                 : lookupCollection(Call->getArg(0), Key, Owner, Depth + 1);
    return nullptr;
  }
  // A free `begin(Container)`, like a loop over it uses.
  if (const auto *Call = dyn_cast<CallExpr>(E);
      Call && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Call) &&
      Call->getNumArgs() == 1 &&
      (calleeName(Call) == "begin" || calleeName(Call) == "end")) {
    Key = nullptr;
    return annotatedCollection(Call->getArg(0), Owner);
  }
  if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E)) {
    const Expr *Object = Call->getImplicitObjectArgument();
    if (isReferenceHandle(Object->IgnoreParenImpCasts()->getType()))
      return lookupCollection(Object, Key, Owner, Depth + 1);
    Key = Call->getNumArgs() >= 1 ? Call->getArg(0) : nullptr;
    if (const FieldDecl *Collection = annotatedCollection(Object, Owner))
      return Collection;
    // What a getter of an entry gives of it, `mp_Items[1].f_GetName()` of
    // `return m_Name;`, which is in the entry, like `mp_Items[1].m_Name`.
    const CXXMethodDecl *Method = Call->getMethodDecl();
    const FunctionDecl *Definition =
        Method && !callsOverride(Call) ? Method->getDefinition() : nullptr;
    if (Definition && Definition->getBody() && Object &&
        (Call->isGLValue() || Call->getType()->isPointerType())) {
      const llvm::SmallVector<const Expr *, 4> Values =
          returnedValues(Definition);
      if (!Values.empty() && givesOnlyMembers(Values, nullptr))
        return lookupCollection(Object, Key, Owner, Depth + 1);
    }
    return nullptr;
  }
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
    if (Variable && Variable->getInit() && !isa<ParmVarDecl>(Variable) &&
        (Variable->hasLocalStorage() || Variable->isInitCapture()) &&
        (Variable->getType()->isReferenceType() ||
         !isChangedVariable(Variable)))
      return lookupCollection(Variable->getInit(), Key, Owner, Depth + 1);
  }
  return nullptr;
}

/// Whether the cleanup \p Lambda runs after the uses of the entry that
/// \p Variable refers to: an `fg_AsyncDestroy` cleanup whose guard isn't
/// awaited early, or a `g_OnScopeExit`, held in a local of a scope around
/// \p Variable, which runs it when the scope ends.
static bool isDeferredCleanup(const LambdaExpr *Lambda, const VarDecl *Variable,
                              ASTContext &Context) {
  DynTypedNode Up = DynTypedNode::create(*Lambda);
  const Expr *Cleanup = nullptr;
  for (int Depth = 0; Depth < 8 && !Cleanup; ++Depth) {
    const auto Parents = instantiatedParents(Up, Context);
    if (Parents.empty())
      return false;
    // A scope exit, `g_OnScopeExit / [...]`, also called explicitly,
    // `g_OnScopeExit.operator/([...])`, or `fg_OnScopeExit([...])`.
    if (const auto *Made = Parents[0].get<Expr>();
        Made && scopeExitOperand(Made) == Lambda) {
      Cleanup = Made;
      break;
    }
    if (const auto *Divide = Parents[0].get<CXXOperatorCallExpr>();
        Divide && Divide->getOperator() == OO_Slash)
      return false;
    if (const auto *Call = Parents[0].get<CallExpr>()) {
      if (Call->getNumArgs() < 1 || asLambda(Call->getArg(0)) != Lambda)
        return false;
      const StringRef Name = calleeName(Call);
      // Run when the coroutine ends, unless awaited before.
      if (llvm::is_contained(
              llvm::ArrayRef<StringRef>(std::begin(AsyncDestroyFunctions),
                                        std::end(AsyncDestroyFunctions)),
              Name)) {
        bool AwaitedEarly = false;
        if (!isCleanupJoined(Call, Context, &AwaitedEarly) || AwaitedEarly)
          return false;
        Cleanup = Call;
        break;
      }
      if (!Name.starts_with("fg_OnScopeExit"))
        return false;
      Cleanup = Call;
      break;
    }
    if (!Parents[0].get<Expr>())
      return false;
    Up = Parents[0];
  }
  if (!Cleanup)
    return false;
  // Held in a local, which runs it when its scope ends; a temporary runs it
  // at the end of its full-expression.
  const VarDecl *Holder = nullptr;
  for (DynTypedNode Node = DynTypedNode::create(*Cleanup); !Holder;) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    Node = Parents[0];
    if (const auto *Local = Node.get<VarDecl>()) {
      Holder = Local;
      break;
    }
    if (!Node.get<ImplicitCastExpr>() && !Node.get<CXXBindTemporaryExpr>() &&
        !Node.get<MaterializeTemporaryExpr>() && !Node.get<ExprWithCleanups>() &&
        !Node.get<CXXConstructExpr>() && !Node.get<ParenExpr>() &&
        !Node.get<CoawaitExpr>() && !Node.get<OpaqueValueExpr>())
      return false;
  }
  // Also a reference that extends it, `auto const &Cleanup = co_await
  // fg_AsyncDestroy(...)`, which lives as long as it.
  if (!Holder->hasLocalStorage() ||
      (Holder->getType()->isReferenceType() && !extendedTemporary(Holder)))
    return false;
  // Used, like `Cleanup()`, which runs it right away, or moved elsewhere.
  // Clearing it, `Cleanup.f_Clear()`, drops the cleanup, which then doesn't
  // remove anything.
  if (const auto *Function =
          dyn_cast_or_null<FunctionDecl>(Holder->getParentFunctionOrMethod());
      Function && Function->getBody()) {
    for (const auto &Nodes :
         match(findAll(declRefExpr(to(varDecl(equalsNode(Holder)))).bind("use")),
               *Function->getBody(), Context)) {
      const auto *Use = Nodes.getNodeAs<DeclRefExpr>("use");
      const auto Parents = instantiatedParents(*Use, Context);
      const auto *Member =
          Parents.empty() ? nullptr : Parents[0].get<MemberExpr>();
      if (Member && Member->getMemberDecl()->getIdentifier() &&
          Member->getMemberDecl()->getName() == "f_Clear")
        continue;
      return false;
    }
  }
  // The holder's scope is around the entry's variable.
  for (DynTypedNode Node = DynTypedNode::create(*Holder);;) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    Node = Parents[0];
    if (const auto *Compound = Node.get<CompoundStmt>()) {
      const SourceManager &SM = Context.getSourceManager();
      return !SM.isBeforeInTranslationUnit(Variable->getLocation(),
                                           Compound->getBeginLoc()) &&
             !SM.isBeforeInTranslationUnit(Compound->getEndLoc(),
                                           Variable->getLocation());
    }
  }
}

/// The variable a lambda captures by \p Init, its capture initializer, also
/// through the copy of an iterator, `[iEntry]`, which its constructor makes,
/// and which refers where it does, unlike the copy of another class, like a
/// shared pointer, which owns what it points to.
static const DeclRefExpr *capturedVariableRef(const Expr *Init) {
  if (!Init)
    return nullptr;
  Init = Init->IgnoreImplicit()->IgnoreParenImpCasts();
  if (const auto *Copy = dyn_cast<CXXConstructExpr>(Init);
      Copy && Copy->getNumArgs() == 1 &&
      Copy->getConstructor()->isCopyOrMoveConstructor() &&
      isIteratorType(Copy->getType()))
    Init = Copy->getArg(0)->IgnoreParenImpCasts();
  return dyn_cast<DeclRefExpr>(Init);
}

/// Whether \p Cast converts a pointer to bool, `bool(pEntry)` or
/// `static_cast<bool>(pEntry)`, which tests whether it is null.
static bool isPointerToBool(const ExplicitCastExpr *Cast) {
  if (Cast->getCastKind() == CK_PointerToBoolean)
    return true;
  return Cast->getType()->isBooleanType() &&
         Cast->getSubExpr()->IgnoreParenImpCasts()->getType()->isPointerType();
}

/// Whether \p Function, which added the entry that \p Variable refers to,
/// removes it from \p Field itself before a later use, also through an alias
/// of the collection, like `auto &Items = mp_Items; Items.f_Remove(Key)`.
/// The names of the member functions that remove entries of a collection,
/// like `f_Remove` and `f_Clear`, of the vocabulary.
static llvm::ArrayRef<StringRef> removingMethods() {
  return vocabulary().RemovingMethods.names();
}

static bool removesOwnEntry(const VarDecl *Variable, const Expr *Key,
                            const FieldDecl *Field, const FunctionDecl *Function,
                            llvm::ArrayRef<StringRef> FreshKeyFunctions,
                            ASTContext &Context) {
  // A key made fresh by this call, like `auto ID = fg_RandomID(mp_Items)`,
  // which no other key is.
  // Also through an unchanged copy of it, `auto Copy = ID`.
  std::function<const VarDecl *(const Expr *, int)> FreshKey =
      [&](const Expr *E, int Depth) -> const VarDecl * {
    const auto *Ref =
        E ? dyn_cast<DeclRefExpr>(skipWrappers(E)->IgnoreParenImpCasts())
          : nullptr;
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    if (!Local || !Local->hasLocalStorage() || !Local->getInit() ||
        isChangedVariable(Local) || Depth > 4)
      return nullptr;
    const Expr *Init = skipWrappers(Local->getInit())->IgnoreParenImpCasts();
    if (isa<DeclRefExpr>(Init))
      return FreshKey(Init, Depth + 1);
    const auto *Call = dyn_cast<CallExpr>(Init);
    return Call && matchesNamePattern(calleeName(Call), FreshKeyFunctions)
               ? Local
               : nullptr;
  };
  const VarDecl *EntryKey = FreshKey(Key, 0);
  const llvm::ArrayRef<StringRef> RemovingMethodList = removingMethods();
  const SourceManager &SM = Context.getSourceManager();
  for (const auto &Nodes :
       match(findAll(cxxMemberCallExpr().bind("removal")), *Function->getBody(),
             Context)) {
    const auto *Removal = Nodes.getNodeAs<CXXMemberCallExpr>("removal");
    const CXXMethodDecl *Method = Removal->getMethodDecl();
    const Expr *Owner = nullptr;
    if (!Method || !Method->getIdentifier() ||
        !matchesNamePattern(Method->getName(), RemovingMethodList) ||
        annotatedCollection(Removal->getImplicitObjectArgument(), Owner) !=
            Field)
      continue;
    // Not of another object that has the collection, like a copy of the
    // state, `auto Copy = mp_State; Copy.m_Items.f_Remove(Key)`.
    if (!isOwnActorObject(Owner))
      continue;
    // Not one that doesn't run, `if constexpr (false) mp_Items.f_Remove(Key)`,
    // `if (false) mp_Items.f_Remove(Key)`, or in `sizeof`.
    if (isInUnevaluatedOperand(DynTypedNode::create(*Removal), Context) ||
        isInDiscardedBranch(DynTypedNode::create(*Removal), Context) ||
        isInNeverTakenBranch(DynTypedNode::create(*Removal), Context))
      continue;
    // Removing the entry of another fresh key leaves this one.
    if (const VarDecl *RemovedKey =
            Removal->getNumArgs() >= 1 ? FreshKey(Removal->getArg(0), 0)
                                       : nullptr;
        EntryKey && RemovedKey && RemovedKey != EntryKey)
      continue;
    // In a cleanup that runs after the uses.
    const LambdaExpr *Lambda = nullptr;
    for (DynTypedNode Node = DynTypedNode::create(*Removal); !Lambda;) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty() || Parents[0].get<FunctionDecl>())
        break;
      Node = Parents[0];
      Lambda = Node.get<LambdaExpr>();
    }
    if (Lambda && isDeferredCleanup(Lambda, Variable, Context))
      continue;
    // A removal after the last use doesn't matter, unless a loop, or a jump
    // back, runs the use again after it: not a loop the reference is declared
    // in, which binds it again each time, `for (...) { auto &Value =
    // mp_Items[Key]; ... mp_Items.f_Remove(Key); }`.
    bool UsedAfter = !match(findAll(stmt(anyOf(gotoStmt(), labelStmt()))),
                            *Function->getBody(), Context)
                          .empty();
    for (DynTypedNode Up = DynTypedNode::create(*Removal);;) {
      const auto UpParents = instantiatedParents(Up, Context);
      if (UpParents.empty() || UpParents[0].get<FunctionDecl>() ||
          UpParents[0].get<LambdaExpr>())
        break;
      Up = UpParents[0];
      const Stmt *Body = nullptr;
      if (const auto *For = Up.get<ForStmt>())
        Body = For->getBody();
      else if (const auto *While = Up.get<WhileStmt>())
        Body = While->getBody();
      else if (const auto *Do = Up.get<DoStmt>())
        Body = Do->getBody();
      else if (const auto *Range = Up.get<CXXForRangeStmt>())
        Body = Range->getBody();
      if (!Body)
        continue;
      if (Body->getSourceRange().fullyContains(Variable->getSourceRange()))
        continue;
      UsedAfter = true;
      break;
    }
    // A removal before the lookup, without a loop or a jump back, is of an
    // entry that the lookup makes again, unless a lambda made before runs it
    // later.
    if (!UsedAfter && !Lambda &&
        SM.isBeforeInTranslationUnit(Removal->getEndLoc(),
                                     Variable->getBeginLoc()))
      continue;
    // After it in the source, or in the same full expression, which can
    // evaluate it after, like the left side of an assignment, `Entry =
    // (mp_Items.f_Remove(Key), 0)`, after the right.
    const Expr *RemovalFull = fullExpressionOf(Removal, Context);
    for (const auto &RefNodes :
         match(findAll(declRefExpr(to(varDecl(equalsNode(Variable))))
                           .bind("use")),
               *Function->getBody(), Context)) {
      const auto *Use = RefNodes.getNodeAs<DeclRefExpr>("use");
      if (SM.isBeforeInTranslationUnit(Removal->getEndLoc(),
                                       Use->getBeginLoc()) ||
          fullExpressionOf(Use, Context) == RemovalFull)
        UsedAfter = true;
    }
    if (UsedAfter)
      return true;
  }
  return false;
}

/// Whether \p Variable, a local, is changed only by assignments of the
/// function it is in, `pEntry = nullptr`, which the flow of the function
/// follows, not through its address, a reference to it, a lambda or a step,
/// `++pEntry`: each use reads it or is assigned.
static bool changedOnlyByAssignments(const VarDecl *Variable) {
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  if (!Function || !Function->getBody() || !Variable->hasLocalStorage() ||
      Variable->getType()->isReferenceType())
    return false;
  auto RefersTo = [&](const Expr *E) {
    const auto *Ref = dyn_cast<DeclRefExpr>(E->IgnoreParens());
    return Ref && Ref->getDecl() == Variable ? Ref : nullptr;
  };
  unsigned nUses = 0;
  unsigned nReadOrAssigned = 0;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S)
      return;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Ref->getDecl() == Variable)
      ++nUses;
    if (const auto *Load = dyn_cast<ImplicitCastExpr>(S);
        Load && Load->getCastKind() == CK_LValueToRValue &&
        RefersTo(Load->getSubExpr()))
      ++nReadOrAssigned;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->getOpcode() == BO_Assign)
      if (const DeclRefExpr *Ref = RefersTo(Assign->getLHS());
          Ref && !Ref->refersToEnclosingVariableOrCapture())
        ++nReadOrAssigned;
    for (const Stmt *Child : childrenOnce(S))
      Visit(Child);
  };
  Visit(Function->getBody());
  return nUses == nReadOrAssigned;
}

std::optional<unsigned> CoroutineActorReferenceCheck::requiredPhases(
    const VarDecl *Variable, bool RangeIterator, const FunctionDecl *Coroutine, ASTContext &Context,
    const FieldDecl **FieldOut) const {
  // A reference can't be bound to something else, also when its address is
  // taken, but an iterator it refers to can be moved on, like `++iEntry`. A
  // pointer assigned in the function, `pEntry = nullptr`, needs the guards
  // while it holds what it was made from, which the flow tells.
  const CXXRecordDecl *Referred =
      Variable->getType().getNonReferenceType()->getAsCXXRecordDecl();
  const bool MovableIterator =
      Referred && Referred->hasDefinition() &&
      llvm::any_of(Referred->methods(), [](const CXXMethodDecl *Method) {
        return Method->getOverloadedOperator() == OO_PlusPlus ||
               Method->getOverloadedOperator() == OO_MinusMinus ||
               (Method->getIdentifier() && (Method->getName() == "f_Next" ||
                                            Method->getName() == "f_Prev"));
      });
  if (!Variable->getInit() ||
      (!RangeIterator &&
       (!Variable->getType()->isReferenceType() || MovableIterator) &&
       isChangedVariable(Variable) &&
       (MovableIterator || !changedOnlyByAssignments(Variable))))
    return std::nullopt;
  const Expr *Key = nullptr;
  const Expr *Owner = nullptr;
  const FieldDecl *Field = lookupCollection(Variable->getInit(), Key, Owner);
  if (!Field)
    return std::nullopt;
  // Only the own actor's phases are guarded, and only its destruction ends
  // the coroutine: another actor can be destroyed while it is suspended.
  if (!isOwnActorObject(Owner))
    return std::nullopt;
  if (FieldOut)
    *FieldOut = Field;
  const unsigned Phases =
      annotatedPhases(Field, AnnotationActorEntries).value_or(0);
  // Other calls can add entries, which must leave this one where it is.
  if (!matchesTypeName(Field->getType(), NodeStableContainerList, Context))
    return std::nullopt;
  if (Phases & ActorPhase_Owner) {
    // Other calls remove the entries they added, which a loop can be at.
    if (RangeIterator)
      return std::nullopt;
    // The coroutine that made the key: a lambda that got the key from the
    // call that added the entry doesn't own it, since that call can remove it
    // while the lambda is suspended.
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
    if (!Function || !Function->getBody() || Function != Coroutine ||
        !isOwnEntryKey(Key, Field, Function, FreshKeyFunctionList, Context,
                       Owner))
      return std::nullopt;
    if (removesOwnEntry(Variable, Key, Field, Function, FreshKeyFunctionList,
                        Context))
      return std::nullopt;
  }
  return Phases & ~ActorPhase_Owner;
}

void CoroutineActorReferenceCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(cxxMethodDecl(isDefinition()).bind("method"), this);
  Finder->addMatcher(
      functionDecl(isDefinition()).bind("function"),
      this);
  Finder->addMatcher(lambdaExpr().bind("lambda"), this);
}

void CoroutineActorReferenceCheck::check(
    const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  // What a template is instantiated as, not the template, whose types and
  // calls its instantiations tell.
  auto ForEachChecked = [&](const FunctionDecl *Function, auto &&Check) {
    if (!Function->isTemplated()) {
      Check(Function);
      return;
    }
    for (const FunctionDecl *Instance :
         Instantiations.instantiationsOf(Function, *Result.Context)) {
      const InstantiationScope Scope(*this, Instance);
      Check(Instance);
    }
  };
  auto GetsActor = [](const FunctionDecl *Candidate) {
    return llvm::any_of(Candidate->parameters(),
                        [](const ParmVarDecl *Parameter) {
                          return refersToActorClass(Parameter->getType());
                        });
  };
  if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("method")) {
    ForEachChecked(Method, [&](const FunctionDecl *Function) {
      const auto *Instance = cast<CXXMethodDecl>(Function);
      if (!Instance->isStatic() && !isLambdaCallOperator(Instance) &&
          isActorClass(Instance->getParent()) && isCoroutine(Instance))
        checkFunction(Instance, nullptr, *Result.Context);
    });
    return;
  }
  // A free or static function that gets the object of an actor through a
  // parameter, like `CInternal *_pThis`, looks up its data like `this`.
  if (const auto *Function = Result.Nodes.getNodeAs<FunctionDecl>("function")) {
    ForEachChecked(Function, [&](const FunctionDecl *Instance) {
      const auto *Method = dyn_cast<CXXMethodDecl>(Instance);
      if ((!Method || Method->isStatic() ||
           (!isLambdaCallOperator(Method) &&
            !isActorClass(Method->getParent()))) &&
          isCoroutine(Instance) && GetsActor(Instance))
        checkFunction(Instance, nullptr, *Result.Context);
    });
    return;
  }
  const auto *Matched = Result.Nodes.getNodeAs<LambdaExpr>("lambda");
  // In an actor, or getting the object of an actor through a parameter or a
  // capture.
  ForEachChecked(Matched->getCallOperator(), [&](const FunctionDecl *Operator) {
    const LambdaExpr *Lambda = Operator == Matched->getCallOperator()
                                   ? Matched
                                   : Instantiations.lambdaOf(Operator,
                                                             *Result.Context);
    if (!Lambda || !isCoroutine(Operator))
      return;
    // Through all nodes: a matcher of the parent map's traversal skips the
    // callee of a member call, like `_pActor` of
    // `_pActor->mp_Items.f_FindEqual(_Key)`.
    // Also through the locals it is made from, like `q` of `auto *q = p`.
    llvm::SmallPtrSet<const VarDecl *, 8> Followed;
    std::function<bool(const Stmt *)> RefersToActor;
    // What a local is made from: its initializer, and what is assigned to it,
    // `p = &pActor->mp_Items[1]` after `int *p = nullptr`.
    auto LocalRefersToActor = [&](const VarDecl *Local) {
      if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
          !Followed.insert(Local).second)
        return false;
      return llvm::any_of(valuesOf(Local), RefersToActor);
    };
    RefersToActor = [&](const Stmt *S) {
      if (!S)
        return false;
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S)) {
        if (refersToActorClass(Ref->getDecl()->getType()))
          return true;
        if (LocalRefersToActor(dyn_cast<VarDecl>(Ref->getDecl())))
          return true;
      }
      return llvm::any_of(childrenOnce(S), RefersToActor);
    };
    // Also a local made from an actor that it captures, like `[pEntry]` of
    // `auto *pEntry = pActor->mp_Items.f_FindEqual(Key)`.
    const bool CapturesActor =
        llvm::any_of(Lambda->captures(), [&](const LambdaCapture &Capture) {
          if (!Capture.capturesVariable())
            return false;
          return refersToActorClass(Capture.getCapturedVar()->getType()) ||
                 LocalRefersToActor(
                     dyn_cast<VarDecl>(Capture.getCapturedVar()));
        }) ||
        llvm::any_of(Lambda->capture_inits(), RefersToActor);
    if (isActorClass(enclosingThisClass(Lambda)) || GetsActor(Operator) ||
        CapturesActor)
      checkFunction(Operator, Lambda, *Result.Context);
  });
}

namespace {

/// The variable that \p Decl refers to: itself, or what a reference init
/// capture like `[&pCurrent = pValue]` is bound to.
const ValueDecl *resolveReferenceCapture(const ValueDecl *Decl) {
  // Each is bound to one declared before it, so a chain of them ends.
  for (int Depth = 0; Depth < 64; ++Depth) {
    const auto *Variable = dyn_cast<VarDecl>(Decl);
    // A reference init capture, or a local reference to a pointer, like
    // `CValue *&pAlias = pValue`, also to an iterator or a reference handle,
    // `auto &iAlias = iEntry`, whose state is that of what it refers to.
    const QualType Referred =
        Variable ? Variable->getType().getNonReferenceType() : QualType();
    if (!Variable || !Variable->getType()->isReferenceType() ||
        !Variable->getInit() ||
        !(Variable->isInitCapture() ||
          (Variable->hasLocalStorage() &&
           (Referred->isPointerType() || isIteratorType(Referred) ||
            isReferenceHandle(Referred)))))
      return Decl;
    // Also through a helper that gives back what it is given by reference,
    // `auto &pAlias = fg_Pointer(pValue)` of `CValue *&fg_Pointer(CValue
    // *&_pValue) { return _pValue; }`.
    const Expr *Init = Variable->getInit()->IgnoreParenImpCasts();
    if (const auto *Call = dyn_cast<CallExpr>(Init); Call && Call->isLValue())
      if (const llvm::SmallVector<const Expr *, 2> Returned =
              returnedArguments(Call);
          Returned.size() == 1)
        Init = Returned.front()->IgnoreParenImpCasts();
    const auto *Ref = dyn_cast<DeclRefExpr>(Init);
    if (!Ref)
      return Decl;
    Decl = Ref->getDecl();
  }
  return Decl;
}

/// Collects what the analysis needs from a coroutine body: the pointers and
/// references into the data of the actor that it looks up, the variables that
/// `fg_OnResume` re-validates, and the left sides of assignments.
class BodyCollector : public OncePerTypeVisitor<BodyCollector> {
public:
  /// The parameters that point or refer to the objects of actors, whose data
  /// is looked up like that of `this`.
  llvm::SmallPtrSet<const ValueDecl *, 4> ActorObjects;

  /// Whether `this` is the object of an actor, also with actor objects.
  bool ThisIsActor = false;

  /// The context of what is collected, which tells the value of constant
  /// conditions.
  const ASTContext *Context = nullptr;
  /// The containers whose entries stay where they are when others are added.
  llvm::ArrayRef<StringRef> NodeStableContainers;

  /// Whether \p E is rooted in `this` or an actor object parameter.
  bool isRooted(const Expr *E) const {
    if (ActorObjects.empty() || (ThisIsActor && isRootedInThis(E)))
      return isRootedInThis(E);
    // Not `this` of what isn't an actor, like a `CAllowUnsafeThis` given an
    // actor, whose own data the actor's calls don't change.
    return isRootedIn(
        E, [&](const ValueDecl *Decl) { return ActorObjects.contains(Decl); },
        ThisIsActor);
  }

  llvm::SmallVector<const VarDecl *, 8> Lookups;
  /// The variables of Lookups, to tell which are.
  llvm::DenseSet<const VarDecl *> LookupSet;

  /// Adds \p Variable to the lookups, when it isn't one yet.
  bool addLookup(const VarDecl *Variable) {
    if (!LookupSet.insert(Variable).second)
      return false;
    Lookups.push_back(Variable);
    return true;
  }

  /// Makes the variables that are made or assigned from lookups found since
  /// they were visited lookups too, like `pFirst = pSecond` before `pSecond =
  /// mp_Items.f_FindEqual(1)` in a loop: each value that names a lookup is
  /// looked at again, not the whole body.
  void propagateLookups() {
    llvm::DenseMap<const ValueDecl *, llvm::SmallVector<unsigned, 2>> ByVariable;
    for (unsigned iSite = 0; iSite < PendingSites.size(); ++iSite) {
      llvm::SmallVector<const Stmt *, 8> Work{PendingSites[iSite].second};
      while (!Work.empty()) {
        const Stmt *S = Work.pop_back_val();
        if (!S)
          continue;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
          ByVariable[Ref->getDecl()].push_back(iSite);
        for (const Stmt *Child : childrenOnce(S))
          Work.push_back(Child);
      }
    }
    llvm::SmallVector<const VarDecl *, 8> Work(Lookups.begin(), Lookups.end());
    while (!Work.empty()) {
      const VarDecl *Found = Work.pop_back_val();
      auto It = ByVariable.find(Found);
      if (It == ByVariable.end())
        continue;
      for (const unsigned iSite : It->second) {
        const auto &[Target, Value] = PendingSites[iSite];
        if (!LookupSet.contains(Target) && isLookupIntoActor(Value) &&
            addLookup(Target))
          Work.push_back(Target);
      }
    }
    PendingSites.clear();
  }

  /// The variables made or assigned from what isn't a lookup when they were
  /// visited, and what from.
  llvm::SmallVector<std::pair<const VarDecl *, const Expr *>, 8> PendingSites;

  /// The variables that hold `fg_OnResume` registrations, and the variables
  /// they re-validate while they live.
  llvm::DenseMap<const VarDecl *, llvm::SmallVector<const ValueDecl *, 4>>
      Revalidations;
  /// Of those, the ones each re-validates only by comparing them with a
  /// fresh lookup and leaving with an error when they differ, which leaves
  /// them as they are, so a copy read before the suspension is valid too.
  llvm::DenseMap<const VarDecl *, llvm::SmallVector<const ValueDecl *, 4>>
      ComparedRevalidations;
  /// The phases that a holder guards, like `f_CheckDestroyedOnResume()` or an
  /// `fg_OnResume` callback that tests the flags of the phases.
  llvm::DenseMap<const VarDecl *, unsigned> HolderPhases;
  /// The phases the callback of each registration starts, like `if
  /// (m_bStopNow) f_StopApp();`, which the guards registered before it don't
  /// see: the callbacks run in the order they were registered.
  llvm::DenseMap<const VarDecl *, unsigned> HolderStarted;

  /// The pointers assigned, with what they are assigned.
  llvm::DenseMap<const DeclRefExpr *, const Expr *> AssignedRefs;
  /// The pointers read to assign what they point to, `ppEntry` of `*ppEntry =
  /// ...`, and the references to pointers assigned, with the variables
  /// assigned, what they are assigned, and whether it may be another, like
  /// for a reference bound to either of a conditional, `int *&pAlias =
  /// bChoose ? pFirst : pSecond`.
  struct CAssignedThrough {
    const ValueDecl *Assigned;
    const Expr *Value;
    bool Maybe;
  };
  llvm::DenseMap<const DeclRefExpr *, llvm::SmallVector<CAssignedThrough, 1>>
      AssignedThrough;
  /// Those of them that may not be assigned, either of a conditional.
  llvm::DenseSet<const DeclRefExpr *> MaybeAssignedRefs;

  bool TraverseLambdaExpr(LambdaExpr *Lambda) {
    // Lambdas are separate functions, which check their own uses. Capturing
    // a pointer doesn't use what it points to, but copying a stale one into a
    // coroutine, which is checked as if it were fresh, does.
    const bool Coroutine = isCoroutine(Lambda->getCallOperator());
    auto Init = Lambda->capture_init_begin();
    for (const LambdaCapture &Capture : Lambda->captures()) {
      const Expr *CaptureInit = *Init++;
      if (const DeclRefExpr *Ref = capturedVariableRef(CaptureInit)) {
        CaptureRefs.insert(Ref);
        if (Coroutine && Capture.capturesVariable() &&
            Capture.getCaptureKind() == LCK_ByCopy)
          CoroutineCaptureRefs.insert(Ref);
      }
      // An init capture that looks the data up, like `[pValue =
      // mp_Items.f_FindEqual(_Key)]`, is tracked like a local.
      if (Capture.capturesVariable()) {
        if (auto *Variable = dyn_cast<VarDecl>(Capture.getCapturedVar());
            Variable && Variable->isInitCapture())
          VisitVarDecl(Variable);
      }
    }
    // A local it captures by reference and assigns a lookup, `[&pEntry] {
    // pEntry = mp_Items.f_FindEqual(0); }`, holds it after the lambda is
    // called.
    std::function<void(const Stmt *)> Assigns = [&](const Stmt *S) {
      if (!S || isa<LambdaExpr>(S))
        return;
      if (const auto *Assign = dyn_cast<BinaryOperator>(S);
          Assign && Assign->getOpcode() == BO_Assign &&
          isLookupIntoActor(Assign->getRHS()))
        for (const DeclRefExpr *Ref : assignedVariables(Assign->getLHS()))
          if (const auto *Variable =
                  dyn_cast<VarDecl>(resolveReferenceCapture(Ref->getDecl()));
              Variable && Variable->hasLocalStorage() &&
              !isa<ParmVarDecl>(Variable) &&
              canHoldLookup(Variable->getType()) &&
              llvm::any_of(Lambda->captures(), [&](const LambdaCapture &Capture) {
                return Capture.capturesVariable() &&
                       Capture.getCapturedVar() == Ref->getDecl() &&
                       Capture.getCaptureKind() == LCK_ByRef;
              }))
            addLookup(Variable);
      for (const Stmt *Child : childrenOnce(S))
        Assigns(Child);
    };
    // Not one of a coroutine that runs elsewhere, but one that runs here,
    // called directly or by `fg_CallSafe(Lambda)`.
    if (!Coroutine || runsHere(Lambda))
      Assigns(Lambda->getBody());
    return true;
  }

  /// Whether \p Lambda is called where it is made, `[&] { ... }()`, or run
  /// right away by `fg_CallSafe`, also held in a local first, `auto fLookup =
  /// [&] { ... };` and `fg_CallSafe(fLookup)`.
  static bool runsHere(const LambdaExpr *Lambda) {
    ASTContext &Context = Lambda->getCallOperator()->getASTContext();
    DynTypedNode Node = DynTypedNode::create(*Lambda);
    for (int Depth = 0; Depth < 8; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return false;
      if (const auto *Local = Parents[0].get<VarDecl>())
        return Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
               calledInScope(Local, Lambda, Context);
      const auto *Parent = Parents[0].get<Expr>();
      if (!Parent)
        return false;
      auto IsFirst = [&](const Expr *Argument) {
        return Argument->IgnoreUnlessSpelledInSource() ==
               Node.get<Expr>()->IgnoreUnlessSpelledInSource();
      };
      if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(Parent))
        return Invoke->getOperator() == OO_Call && Invoke->getNumArgs() >= 1 &&
               IsFirst(Invoke->getArg(0));
      if (const auto *Call = dyn_cast<CallExpr>(Parent))
        return Call->getNumArgs() >= 1 && IsFirst(Call->getArg(0)) &&
               calleeName(Call) == "fg_CallSafe";
      if (!isa<ImplicitCastExpr, MaterializeTemporaryExpr, CXXBindTemporaryExpr,
               ParenExpr, CXXConstructExpr, ExprWithCleanups>(Parent))
        return false;
      Node = Parents[0];
    }
    return false;
  }

  /// Whether \p Lambda, which \p Local holds, is called or run by
  /// `fg_CallSafe` in the scope of \p Local.
  static bool calledInScope(const VarDecl *Local, const LambdaExpr *Lambda,
                            ASTContext &Context) {
    const auto Declarations = instantiatedParents(*Local, Context);
    if (Declarations.empty() || !Declarations[0].get<DeclStmt>())
      return false;
    const auto Scopes = instantiatedParents(Declarations[0], Context);
    const Stmt *Scope = Scopes.empty() ? nullptr : Scopes[0].get<Stmt>();
    std::function<bool(const Stmt *)> Calls = [&](const Stmt *S) {
      if (!S)
        return false;
      if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(S);
          Invoke && Invoke->getOperator() == OO_Call &&
          Invoke->getNumArgs() >= 1 &&
          calledLambda(Invoke->getArg(0)) == Lambda)
        return true;
      if (const auto *Call = dyn_cast<CallExpr>(S);
          Call && Call->getNumArgs() >= 1 &&
          calleeName(Call) == "fg_CallSafe" &&
          calledLambda(Call->getArg(0)) == Lambda)
        return true;
      return llvm::any_of(childrenOnce(S), Calls);
    };
    return Calls(Scope);
  }

  llvm::SmallPtrSet<const DeclRefExpr *, 8> CoroutineCaptureRefs;

  llvm::SmallPtrSet<const DeclRefExpr *, 8> CaptureRefs;

  /// The ranges of loops over copies the loops own, and their iterators.
  llvm::SmallPtrSet<const VarDecl *, 4> OwnedRanges;

  /// The iterators of loops over the data of the actor, like
  /// `for (auto &Entry : mp_Items)`, with their loops. Going on to the next
  /// entry after a suspension uses the iterator.
  llvm::DenseMap<const VarDecl *, const CXXForRangeStmt *> RangeIterators;

  bool VisitCXXForRangeStmt(CXXForRangeStmt *For) {
    const auto *Begin =
        For->getBeginStmt()
            ? dyn_cast_or_null<VarDecl>(For->getBeginStmt()->getSingleDecl())
            : nullptr;
    if (!Begin || !For->getRangeInit() || RangeIterators.contains(Begin))
      return true;
    // A member array, `int m_Values[2]`, can't lose its entries, nor can a
    // `std::array`.
    if (For->getRangeInit()->getType()->isConstantArrayType())
      return true;
    if (const auto *Record = For->getRangeInit()
                                 ->getType()
                                 .getNonReferenceType()
                                 ->getAsCXXRecordDecl();
        Record && Record->getIdentifier() && Record->getName() == "array" &&
        Record->isInStdNamespace())
      return true;
    // A copy of the collection, like `mp_Values.f_Copy()`, which the loop
    // owns, isn't the data of the actor.
    const Expr *Range = skipWrappers(For->getRangeInit())->IgnoreParenImpCasts();
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Range);
        Call && Call->isPRValue() && Call->getType()->getAsCXXRecordDecl() &&
        Call->getImplicitObjectArgument()->getType()->getAsCXXRecordDecl() &&
        Call->getType()->getAsCXXRecordDecl()->getCanonicalDecl() ==
            Call->getImplicitObjectArgument()
                ->getType()
                ->getAsCXXRecordDecl()
                ->getCanonicalDecl()) {
      // So isn't what its iterator and its entries refer to.
      if (const auto *RangeVariable = For->getRangeStmt()
                                          ? dyn_cast_or_null<VarDecl>(
                                                For->getRangeStmt()->getSingleDecl())
                                          : nullptr)
        OwnedRanges.insert(RangeVariable);
      OwnedRanges.insert(Begin);
      return true;
    }
    if (isLookupIntoActor(For->getRangeInit()) ||
        isRooted(For->getRangeInit())) {
      RangeIterators[Begin] = For;
      addLookup(Begin);
    }
    return true;
  }

  bool VisitVarDecl(VarDecl *Variable) {
    // Also a static local, `static int *pValue = &mp_Items[0]`, whose storage
    // doesn't keep the entry.
    if (resolveReferenceCapture(Variable) == Variable &&
        (Variable->hasLocalStorage() || Variable->isStaticLocal() ||
         Variable->isInitCapture()) &&
        !isa<ParmVarDecl>(Variable) && canHoldLookup(Variable->getType()) &&
        Variable->getInit() && !LookupSet.contains(Variable)) {
      if (isLookupIntoActor(Variable->getInit()))
        addLookup(Variable);
      else
        PendingSites.push_back({Variable, Variable->getInit()});
    }
    addRevalidation(Variable);
    return true;
  }

  // Only assigning a new pointer looks the data up again. Assigning through a
  // reference, or adjusting a pointer, uses what it refers to. A pointer
  // that is assigned a lookup is tracked too.
  bool VisitBinaryOperator(BinaryOperator *Binary) {
    if (Binary->getOpcode() != BO_Assign)
      return true;
    addAssigned(Binary->getLHS(), Binary->getRHS());
    // Also either of a conditional, `(bFirst ? pFirst : pSecond) = ...`,
    // which isn't assigned unconditionally.
    const llvm::SmallVector<const DeclRefExpr *, 2> Targets =
        assignedVariables(Binary->getLHS());
    if (CurrentHolder)
      for (const DeclRefExpr *Ref : Targets)
        if (!CallbackCopies.contains(Ref->getDecl()) &&
            !(Ref->getDecl()->getType()->isReferenceType() &&
              resolveReferenceCapture(Ref->getDecl()) == Ref->getDecl()))
          CallbackAssignments[CurrentHolder].push_back(
              {resolveReferenceCapture(Ref->getDecl()),
               isLookupIntoActor(Binary->getRHS()),
               Targets.size() == 1 && isUnconditionalInCallback(Binary)});
    // Also what a reference bound to either of a conditional is bound to,
    // `int *&pAlias = bChoose ? pFirst : pSecond`.
    llvm::SmallVector<const DeclRefExpr *, 4> Expanded(Targets.begin(),
                                                       Targets.end());
    for (size_t iTarget = 0; iTarget < Expanded.size() && iTarget < 64;
         ++iTarget)
      if (const auto *Alias = dyn_cast<VarDecl>(Expanded[iTarget]->getDecl());
          Alias && Alias->getType()->isReferenceType() &&
          resolveReferenceCapture(Alias) == Alias && Alias->hasLocalStorage() &&
          Alias->getInit())
        for (const DeclRefExpr *Bound : assignedVariables(Alias->getInit()))
          Expanded.push_back(Bound);
    // Also of a static local, `static int *s_pEntry; s_pEntry = ...`, and a
    // parameter, which point where they were assigned to.
    for (const DeclRefExpr *Ref : Expanded)
      if (const auto *Variable =
              dyn_cast<VarDecl>(resolveReferenceCapture(Ref->getDecl()));
          Variable &&
          (Variable->hasLocalStorage() || Variable->isStaticLocal()) &&
          !CallbackCopies.contains(Variable) &&
          Variable->getType()->isPointerType() &&
          !LookupSet.contains(Variable)) {
        if (isLookupIntoActor(Binary->getRHS()))
          addLookup(Variable);
        else
          PendingSites.push_back({Variable, Binary->getRHS()});
      }
    return true;
  }

  // Swapping pointers, `fg_Swap(pEntry, pOther)`, gives each the other's
  // value, and exchanging one, `fg_Exchange(pOther, pEntry)`, the first the
  // second's.
  bool VisitCallExpr(CallExpr *Call) {
    // A function whose body can be seen that assigns a pointer it is given
    // by reference what it is given, `void fg_Set(int *&o_pValue, int
    // *_pValue) { o_pValue = _pValue; }`, gives it that, also when it may,
    // in a branch, also a lambda, `fSet(pValue, mp_Items.f_FindEqual(1))`.
    unsigned Offset = 0;
    if (const FunctionDecl *Callee = setterCalled(Call, Offset)) {
      // Of each pointer it is given, what it has after the call: what it is
      // assigned last on every path, `o_pValue = _pSecond`, and what it may
      // be assigned after that, in a branch.
      llvm::SmallDenseMap<unsigned, llvm::SmallVector<CSetParameter, 2>, 2>
          ByTarget;
      for (CSetParameter Set : setParameters(Callee))
        if (Set.Target + Offset < Call->getNumArgs() &&
            Set.Source + Offset < Call->getNumArgs()) {
          Set.Target += Offset;
          Set.Source += Offset;
          ByTarget[Set.Target].push_back(Set);
        }
      // One that can throw before it assigns it, to a `try` around the call
      // that catches it, `try { fg_Set(pValue, &Local); } catch (...) {}`,
      // may leave it what it was.
      const auto *Prototype = Callee->getType()->getAs<FunctionProtoType>();
      const bool MayLeave = !(Prototype && Prototype->isNothrow()) &&
                            insideTryBlock(Call, Callee->getASTContext());
      for (const auto &[iTarget, Sets] : ByTarget) {
        // Also through a cast to a reference to it, `static_cast<int *&>(
        // pValue)`.
        // Also one given by its address, `fg_Set(&pValue, ...)`.
        const Expr *Named = setTargetArgument(Call, Sets.front(), 0);
        if (!Named)
          continue;
        const llvm::SmallVector<const DeclRefExpr *, 2> Targets =
            assignedVariables(Named);
        const DeclRefExpr *Ref = Targets.size() == 1 ? Targets[0] : nullptr;
        const auto *Variable =
            Ref ? dyn_cast<VarDecl>(resolveReferenceCapture(Ref->getDecl()))
                : nullptr;
        if (!Variable || !Variable->hasLocalStorage() ||
            isa<ParmVarDecl>(Variable) || !Variable->getType()->isPointerType())
          continue;
        const CSetParameter *Last = nullptr;
        for (const CSetParameter &Set : Sets)
          if (Set.Always)
            Last = &Set;
        // A lookup it may be given after the last it surely is, which it
        // then may hold.
        const Expr *Maybe = nullptr;
        for (const CSetParameter &Set :
             llvm::make_range(Last ? Last + 1 : Sets.begin(), Sets.end()))
          if (isLookupIntoActor(Call->getArg(Set.Source)))
            Maybe = Call->getArg(Set.Source);
        if (Maybe) {
          AssignedRefs[Ref] = Maybe;
          MaybeAssignedRefs.insert(Ref);
          addLookup(Variable);
        } else if (Last) {
          if (!MayLeave)
            AssignedRefs[Ref] = Call->getArg(Last->Source);
          if (isLookupIntoActor(Call->getArg(Last->Source)))
            addLookup(Variable);
        }
      }
    }
    // A member function of the actor whose body can be seen that assigns a
    // pointer it is given by reference a lookup into the data, `bool
    // f_Find(int *&o_pValue) { o_pValue = mp_Items.f_FindEqual(0); ... }`,
    // gives it that, maybe.
    if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
        Member && Member->getImplicitObjectArgument() &&
        refersToOwnObject(Member->getImplicitObjectArgument()))
      if (const FunctionDecl *Callee =
              Member->getMethodDecl() ? Member->getMethodDecl()->getDefinition()
                                      : nullptr;
          Callee && Callee->getBody() && !callsOverride(Member))
        for (unsigned iArgument = 0; iArgument < Member->getNumArgs() &&
                                     iArgument < Callee->getNumParams();
             ++iArgument) {
          const ParmVarDecl *Parameter = Callee->getParamDecl(iArgument);
          const QualType Type = Parameter->getType();
          if (!Type->isReferenceType() ||
              !Type.getNonReferenceType()->isPointerType() ||
              Type.getNonReferenceType().isConstQualified())
            continue;
          const auto *Ref = dyn_cast<DeclRefExpr>(
              Member->getArg(iArgument)->IgnoreParenImpCasts());
          const auto *Variable =
              Ref ? dyn_cast<VarDecl>(resolveReferenceCapture(Ref->getDecl()))
                  : nullptr;
          if (!Variable || !Variable->hasLocalStorage() ||
              isa<ParmVarDecl>(Variable) ||
              !Variable->getType()->isPointerType())
            continue;
          const Expr *Lookup = nullptr;
          // The parameter, also through a local reference to it, `int *&pOutput
          // = o_pValue`.
          auto IsParameter = [&](const Expr *E) {
            for (int Depth = 0; E && Depth < 8; ++Depth) {
              const auto *Target =
                  dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
              if (!Target)
                return false;
              if (Target->getDecl() == Parameter)
                return true;
              const auto *Alias = dyn_cast<VarDecl>(Target->getDecl());
              if (!Alias || !Alias->getType()->isReferenceType() ||
                  isa<ParmVarDecl>(Alias) || !Alias->hasLocalStorage())
                return false;
              E = Alias->getInit();
            }
            return false;
          };
          const ASTContext &CalleeContext = Callee->getASTContext();
          std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
            if (!S || Lookup || isa<LambdaExpr>(S))
              return;
            if (const auto *Assign = dyn_cast<BinaryOperator>(S);
                Assign && Assign->getOpcode() == BO_Assign &&
                IsParameter(Assign->getLHS()) &&
                isLookupIntoActor(Assign->getRHS()))
              Lookup = Assign->getRHS();
            // Not in the branch a constant condition never takes, `if
            // (mc_bUseCache)` of `false`.
            if (const auto *If = dyn_cast<IfStmt>(S);
                If && !If->isConstexpr() && !If->isConsteval() &&
                !If->getConditionVariable() &&
                !If->getCond()->isValueDependent()) {
              bool Taken = false;
              if (If->getCond()->EvaluateAsBooleanCondition(Taken,
                                                            CalleeContext)) {
                Find(If->getInit());
                Find(If->getCond());
                Find(Taken ? If->getThen() : If->getElse());
                return;
              }
            }
            for (const Stmt *Child : keptChildren(S, CalleeContext))
              Find(Child);
          };
          Find(Callee->getBody());
          if (Lookup) {
            AssignedRefs[Ref] = Lookup;
            MaybeAssignedRefs.insert(Ref);
            addLookup(Variable);
          }
        }
    // Also of `std::ranges::swap`, a call of the object that customizes it.
    const auto [SwapLeft, SwapRight] = swappedArguments(Call);
    const bool Swaps = SwapLeft != nullptr;
    if (!Swaps &&
        (isa<CXXOperatorCallExpr, CXXMemberCallExpr>(Call) ||
         Call->getNumArgs() != 2 || (!isExchangeFunction(calleeName(Call)))))
      return true;
    auto Gives = [&](const Expr *To, const Expr *From) {
      // Also through a cast to a reference, `static_cast<int *&>(pEntry)`.
      const llvm::SmallVector<const DeclRefExpr *, 2> Targets =
          assignedVariables(To);
      const DeclRefExpr *Ref = Targets.size() == 1 ? Targets[0] : nullptr;
      const auto *Variable =
          Ref ? dyn_cast<VarDecl>(resolveReferenceCapture(Ref->getDecl()))
              : nullptr;
      if (!Variable || !Variable->hasLocalStorage() ||
          isa<ParmVarDecl>(Variable) || !canHoldLookup(Variable->getType()) ||
          Variable->getType()->isReferenceType())
        return;
      if (!isLookupIntoActor(From))
        return;
      // A swap exchanges what the two hold where it runs, which the flow
      // does with both: only an exchange is an assignment.
      if (!Swaps)
        AssignedRefs[Ref] = From;
      addLookup(Variable);
    };
    if (Swaps) {
      Gives(SwapLeft, SwapRight);
      Gives(SwapRight, SwapLeft);
    } else
      Gives(Call->getArg(0), Call->getArg(1));
    return true;
  }

  // Assigning an iterator, or a handle, with its `operator=`, like `iEntry =
  // mp_Items.f_GetIterator(Key)`, looks the data up again, as assigning a
  // pointer does.
  bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *Call) {
    if (Call->getOperator() != OO_Equal || Call->getNumArgs() != 2)
      return true;
    return assignHandle(Call->getArg(0), Call->getArg(1));
  }
  // Also called by name, `iEntry.operator=(mp_Items.f_GetIterator(Key))`.
  bool VisitCXXMemberCallExpr(CXXMemberCallExpr *Call) {
    if (!Call->getMethodDecl() ||
        Call->getMethodDecl()->getOverloadedOperator() != OO_Equal ||
        Call->getNumArgs() != 1 || !Call->getImplicitObjectArgument())
      return true;
    return assignHandle(Call->getImplicitObjectArgument(), Call->getArg(0));
  }
  bool assignHandle(const Expr *Left, const Expr *Right) {
    const auto *Ref = dyn_cast<DeclRefExpr>(Left->IgnoreParenImpCasts());
    if (!Ref || !(isIteratorType(Ref->getType()) ||
                  isReferenceHandle(Ref->getType())))
      return true;
    if (!Ref->getDecl()->getType()->isReferenceType() ||
        resolveReferenceCapture(Ref->getDecl()) != Ref->getDecl())
      AssignedRefs[Ref] = Right;
    if (const auto *Variable =
            dyn_cast<VarDecl>(resolveReferenceCapture(Ref->getDecl()));
        Variable && Variable->hasLocalStorage() &&
        !isa<ParmVarDecl>(Variable) && !CallbackCopies.contains(Variable) &&
        isLookupIntoActor(Right))
      addLookup(Variable);
    return true;
  }

private:
  /// Records \p Variable when it holds the result of
  /// `co_await fg_OnResume(...)`, which keeps the re-validation registered
  /// while it lives.
  void addRevalidation(const VarDecl *Variable) {
    // A static one, `static auto Guard = co_await ...`, registers only for the
    // call that first runs it.
    if (!Variable->getInit() || !Variable->hasLocalStorage())
      return;
    // A guard of phases, like `co_await f_CheckDestroyedOnResume()`, ends the
    // coroutine on resume once the actor is in one of them.
    if (const unsigned Guarded = guardedPhasesOfHolder(
            Variable->getInit(), &Variable->getASTContext());
        Guarded && !isReferencedHolder(Variable)) {
      HolderPhases[Variable] = Guarded;
      Revalidations[Variable];
    }
    // The registration, or either of a conditional, `bChoose ? co_await
    // fg_OnResume(A) : co_await fg_OnResume(B)`, which re-validates what both
    // do.
    llvm::SmallVector<const CallExpr *, 2> Calls;
    // An alternative that registers no re-validation, like a guard, `bChoose
    // ? co_await fg_OnResume(...) : co_await f_CheckDestroyedOnResume()`,
    // re-validates nothing.
    bool bEveryAlternative = true;
    // Also an awaiter made before, `co_await fg_Move(Awaiter)`, and either
    // value awaited, `co_await (bChoose ? fg_OnResume(A) : fg_OnResume(B))`.
    for (const CoroutineSuspendExpr *Await :
         holderAwaits(Variable->getInit(), &Variable->getASTContext())) {
      if (!Await) {
        bEveryAlternative = false;
        continue;
      }
      for (const CallExpr *Call : onResumeCalls(Await->getOperand()))
        if (Call)
          Calls.push_back(Call);
        else
          bEveryAlternative = false;
    }
    if (Calls.empty())
      return;
    std::optional<std::remove_reference_t<decltype(Revalidations[Variable])>>
        Common;
    std::optional<std::remove_reference_t<decltype(Revalidations[Variable])>>
        CommonCompared;
    for (const CallExpr *Call : Calls) {
      std::remove_reference_t<decltype(Revalidations[Variable])> Variables;
      std::remove_reference_t<decltype(Revalidations[Variable])> ComparedOnly;
      // What the re-validation assigns, or passes on to be assigned. A callback
      // that returns something, like the exception that ends the coroutine
      // when `pNamespace != &Namespace`, validates everything it reads; one
      // that returns nothing doesn't re-validate what it only reads. What it
      // assigns lookups to is tracked too, for when the registration ends
      // before a suspension.
      // A variable the callback captures by copy is a member of the closure,
      // so assigning it doesn't re-validate the variable of the coroutine.
      for (const Expr *Argument : Call->arguments()) {
        const LambdaExpr *Lambda = asLambda(Argument);
        if (!Lambda)
          continue;
        HolderStarted[Variable] |= startedPhases(
            Lambda->getBody(), Lambda->getCallOperator()->getASTContext());
        // One that removes entries of the data of the actor, `mp_Items.f_Clear()`,
        // can remove what it just looked up, and what the callbacks
        // registered before it looked up, in that collection.
        if (CRemovals Removed = removals(Lambda->getBody()); Removed.any()) {
          CRemovals &Holder = HolderRemovals[Variable];
          Holder.bUnknown |= Removed.bUnknown;
          Holder.Fields.insert(Removed.Fields.begin(), Removed.Fields.end());
        }
        llvm::SmallPtrSet<const ValueDecl *, 4> Copies;
        for (const LambdaCapture &Capture : Lambda->captures()) {
          if (Capture.capturesVariable() &&
              Capture.getCaptureKind() == LCK_ByCopy)
            Copies.insert(Capture.getCapturedVar());
        }
        RefCollector Refs;
        Refs.Owner = this;
        Refs.bValidating = !Lambda->getCallOperator()->getReturnType()->isVoidType();
        // Those after one that can return without an error, `if (_bSkip)
        // return nullptr;`, don't run on every path that succeeds.
        Refs.TopLevel = statementsBeforeSuccess(Lambda->getBody());
        // Also what an `if` among them assigns on every path, and in its
        // initializer, `if (pValue = ...; !pValue)`, which runs each time.
        for (const BinaryOperator *Assign :
             everyPathAssignments(Lambda->getBody()))
          Refs.TopLevel.insert(Assign);
        for (const Stmt *Statement : llvm::SmallVector<const Stmt *, 8>(
                 Refs.TopLevel.begin(), Refs.TopLevel.end())) {
          for (const BinaryOperator *Assign : alwaysRunAssignments(Statement))
            Refs.TopLevel.insert(Assign);
        }
        // What a branch that leaves with an error assigns, like `pEntry =
        // nullptr; return DMibErrorInstance(...)`, doesn't matter where the
        // coroutine goes on.
        {
          std::function<void(const Stmt *)> InError = [&](const Stmt *S) {
            if (!S || isa<LambdaExpr>(S))
              return;
            if (const auto *Assign = dyn_cast<BinaryOperator>(S);
                Assign && Assign->getOpcode() == BO_Assign)
              Refs.TopLevel.insert(Assign);
            for (const Stmt *Child : childrenOnce(S))
              InError(Child);
          };
          std::function<void(const Stmt *)> FindBranches = [&](const Stmt *S) {
            if (!S || isa<LambdaExpr>(S))
              return;
            if (const auto *If = dyn_cast<IfStmt>(S)) {
              if (leavesWithError(If->getThen()))
                InError(If->getThen());
              if (If->getElse() && leavesWithError(If->getElse()))
                InError(If->getElse());
            }
            for (const Stmt *Child : childrenOnce(S))
              FindBranches(Child);
          };
          FindBranches(Lambda->getBody());
        }
        Refs.TraverseStmt(Lambda->getBody());
        // Testing a pointer kept from before for null, like `if (!pEntry)
        // return ...`, doesn't tell its entry is still there.
        for (const ValueDecl *Ref : Refs.Decls) {
          if (!Copies.contains(Ref) && !Refs.onlyNullTested(Ref))
            Variables.push_back(resolveReferenceCapture(Ref));
        }
        // What a validating callback keeps pointing to the same data: what it
        // compares with another pointer, like `pDistribution !=
        // pInitialDistribution`. References made from those, like `auto
        // &Distribution = *pDistribution`, are re-validated with them. A
        // pointer it only tests for null, like `if (!pEntry)`, still points to
        // the entry it was looked up as, which can be gone.
        if (Refs.bValidating) {
          llvm::SmallPtrSet<const ValueDecl *, 4> Compared;
          llvm::SmallPtrSet<const ValueDecl *, 4> StaleCompared;
          llvm::SmallPtrSet<const ValueDecl *, 4> FreshCompared;
          // What the callback assigns a lookup to on every path, in a
          // statement of its body, and where it does: an assignment in a
          // branch, or of something else, may leave it pointing to what it did
          // before.
          llvm::DenseMap<const ValueDecl *, SourceLocation> FreshlyAssigned;
          // What it assigns something else than a fresh lookup, like a pointer
          // kept from before, `pEntry = pSaved`, or only in a branch.
          llvm::SmallPtrSet<const ValueDecl *, 4> Unfresh;
          {
            const llvm::SmallPtrSet<const Stmt *, 8> TopLevel =
                statementsBeforeSuccess(Lambda->getBody());
            const SourceManager &LambdaSM =
                Lambda->getCallOperator()->getASTContext().getSourceManager();
            // A lookup made here, not a pointer kept from before, like
            // `pEntry = pSaved` with `int *pSaved = pEntry`.
            // Also through a cast or arithmetic, `static_cast<int
            // *>(pSaved)` or `pSaved + 0`, as a value of a conditional, or to
            // a part of what it points to, `&pSaved->m_Value` or
            // `&pSaved[0]`.
            std::function<bool(const Expr *, int)> IsKept =
                [&](const Expr *Value, int Level) {
                  if (!Value || Level > 8)
                    return false;
                  Value = Value->IgnoreParenImpCasts();
                  if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Value))
                    return IsKept(Cast->getSubExpr(), Level + 1);
                  if (const auto *Unary = dyn_cast<UnaryOperator>(Value);
                      Unary && (Unary->getOpcode() == UO_AddrOf ||
                                Unary->getOpcode() == UO_Deref))
                    return IsKept(Unary->getSubExpr(), Level + 1);
                  if (const auto *Binary = dyn_cast<BinaryOperator>(Value)) {
                    if (Binary->getOpcode() == BO_Comma)
                      return IsKept(Binary->getRHS(), Level + 1);
                    if (Binary->isAdditiveOp())
                      return IsKept(Binary->getLHS()->getType()->isPointerType()
                                        ? Binary->getLHS()
                                        : Binary->getRHS(),
                                    Level + 1);
                  }
                  if (const auto *Conditional =
                          dyn_cast<AbstractConditionalOperator>(Value))
                    return IsKept(Conditional->getTrueExpr(), Level + 1) ||
                           IsKept(Conditional->getFalseExpr(), Level + 1);
                  if (const auto *Member = dyn_cast<MemberExpr>(Value))
                    return IsKept(Member->getBase(), Level + 1);
                  if (const auto *Subscript =
                          dyn_cast<ArraySubscriptExpr>(Value))
                    return IsKept(Subscript->getBase(), Level + 1);
                  // Also moved or forwarded, `fg_Move(pSaved)`, or given
                  // back by a helper, `fg_Identity(pSaved)`.
                  if (const auto *Call = dyn_cast<CallExpr>(Value)) {
                    const StringRef Name = calleeName(Call);
                    if (Call->getNumArgs() == 1 &&
                        (isMoveFunction(Name) || isForwardFunction(Name)))
                      return IsKept(Call->getArg(0), Level + 1);
                    // Also what a lambda called here gives back, `[&] {
                    // return bRefresh ? lookup : pValue; }()`.
                    if (const CXXMethodDecl *Called = calledInPlaceLambda(Call);
                        Called && llvm::any_of(returnedValues(Called),
                                               [&](const Expr *Returned) {
                                                 return IsKept(Returned,
                                                               Level + 1);
                                               }))
                      return true;
                    return llvm::any_of(returnedArguments(Call),
                                        [&](const Expr *Argument) {
                                          return IsKept(Argument, Level + 1);
                                        });
                  }
                  const auto *ValueRef = dyn_cast<DeclRefExpr>(Value);
                  return ValueRef && isa<VarDecl>(ValueRef->getDecl()) &&
                         LambdaSM.isBeforeInTranslationUnit(
                             ValueRef->getDecl()->getLocation(),
                             Lambda->getBody()->getBeginLoc());
                };
            // An `if` among the statements of the body that assigns it on every
            // path, `if (Mode) pEntry = mp_Items.f_FindEqual(1); else pEntry =
            // mp_Items.f_FindEqual(2);`, assigns it where it is, a fresh lookup
            // when each of them is one.
            llvm::DenseMap<const BinaryOperator *, SourceLocation> Covered;
            if (const auto *Body = dyn_cast<CompoundStmt>(Lambda->getBody())) {
              const llvm::SmallPtrSet<const BinaryOperator *, 4> EveryPath =
                  everyPathAssignments(Body);
              for (const Stmt *Statement : Body->body()) {
                if (const auto *If = dyn_cast<IfStmt>(Statement))
                  for (const BinaryOperator *Assign : EveryPath)
                    if (If->getSourceRange().fullyContains(
                            Assign->getSourceRange()))
                      Covered.try_emplace(Assign, If->getBeginLoc());
                // Also the initializer of one, `if (pValue = ...; !pValue)`,
                // which runs each time.
                for (const BinaryOperator *Assign :
                     alwaysRunAssignments(Statement))
                  Covered.try_emplace(Assign, Statement->getBeginLoc());
              }
            }
            // Where it is last assigned what isn't fresh, and a fresh lookup:
            // what isn't, before a fresh one, `pValue = nullptr; pValue =
            // mp_Items.f_FindEqual(_Key);`, doesn't matter.
            llvm::DenseMap<const ValueDecl *, SourceLocation> UnfreshAt;
            llvm::DenseMap<const ValueDecl *, SourceLocation> LastFresh;
            auto Later =
                [&](llvm::DenseMap<const ValueDecl *, SourceLocation> &Map,
                    const ValueDecl *Assigned, SourceLocation At) {
                  auto [It, Inserted] = Map.try_emplace(Assigned, At);
                  if (!Inserted &&
                      LambdaSM.isBeforeInTranslationUnit(It->second, At))
                    It->second = At;
                };
            for (const auto &Nodes : match(
                     findAll(binaryOperator(hasOperatorName("=")).bind("assign")),
                     *Lambda->getBody(),
                     Lambda->getCallOperator()->getASTContext())) {
              const auto *Assign = Nodes.getNodeAs<BinaryOperator>("assign");
              // Also through a cast to a reference, `static_cast<int
              // *&>(pEntry) = ...`; either of a conditional, `(bFirst ? pFirst
              // : pSecond) = ...`, isn't assigned on every resume.
              const llvm::SmallVector<const DeclRefExpr *, 2> Targets =
                  assignedVariables(Assign->getLHS());
              if (Targets.size() != 1) {
                for (const DeclRefExpr *Target : Targets) {
                  Unfresh.insert(resolveReferenceCapture(Target->getDecl()));
                  Later(UnfreshAt, resolveReferenceCapture(Target->getDecl()),
                        Assign->getBeginLoc());
                }
                continue;
              }
              const DeclRefExpr *Target = Targets[0];
              const ValueDecl *Assigned = resolveReferenceCapture(Target->getDecl());
              // What a branch that leaves with an error assigns doesn't
              // matter.
              if (!TopLevel.contains(Assign) && Refs.TopLevel.contains(Assign))
                continue;
              const bool Kept = IsKept(Assign->getRHS(), 0);
              const auto CoveredAt = Covered.find(Assign);
              if ((CoveredAt == Covered.end() && !TopLevel.contains(Assign)) ||
                  Kept || !isLookupIntoActor(Assign->getRHS())) {
                Unfresh.insert(Assigned);
                Later(UnfreshAt, Assigned, Assign->getBeginLoc());
                continue;
              }
              const SourceLocation At = CoveredAt != Covered.end()
                                            ? CoveredAt->second
                                            : Assign->getBeginLoc();
              Later(LastFresh, Assigned, Assign->getBeginLoc());
              auto [It, Inserted] = FreshlyAssigned.try_emplace(Assigned, At);
              if (!Inserted &&
                  LambdaSM.isBeforeInTranslationUnit(At, It->second))
                It->second = At;
            }
            for (const auto &[Assigned, At] : UnfreshAt)
              if (const auto Fresh = LastFresh.find(Assigned);
                  Fresh != LastFresh.end() &&
                  LambdaSM.isBeforeInTranslationUnit(At, Fresh->second))
                Unfresh.erase(Assigned);
            for (const ValueDecl *Assigned : Unfresh)
              FreshlyAssigned.erase(Assigned);
          }
          // Only comparisons that end the coroutine with an error when the
          // pointer doesn't match, on every resume that succeeds.
          const llvm::SmallPtrSet<const BinaryOperator *, 4> Gates =
              mismatchTests(Lambda->getBody());
          for (const auto &Nodes :
               match(findAll(binaryOperator(anyOf(hasOperatorName("=="),
                                                  hasOperatorName("!=")))
                                 .bind("compare")),
                     *Lambda->getBody(), Lambda->getCallOperator()->getASTContext())) {
            const auto *Compare = Nodes.getNodeAs<BinaryOperator>("compare");
            const bool Gate = Gates.contains(Compare);
            // Compared with a fresh lookup, like `pDistribution !=
            // mp_Distributions.f_FindEqual(Key)`, or a local of the callback
            // that holds one: a null test, or a comparison with another pointer
            // kept from before, doesn't tell the entry is still there.
            auto Fresh = [&](const Expr *E) {
              E = E->IgnoreParenImpCasts();
              // What a cast or an offset is of, `static_cast<CValue *>(pSaved)`
              // or `pSaved + 0`, which points where that does.
              for (int Depth = 0; Depth < 8; ++Depth)
                if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
                    Cast && Cast->getType()->isPointerType())
                  E = Cast->getSubExpr()->IgnoreParenImpCasts();
                else if (const auto *Offset = dyn_cast<BinaryOperator>(E);
                         Offset && Offset->isAdditiveOp() &&
                         Offset->getType()->isPointerType())
                  E = (Offset->getLHS()->getType()->isPointerType()
                           ? Offset->getLHS()
                           : Offset->getRHS())
                          ->IgnoreParenImpCasts();
                else
                  break;
              // The address of a variable, `&Value`, is fresh as it is.
              if (const auto *Address = dyn_cast<UnaryOperator>(E);
                  Address && Address->getOpcode() == UO_AddrOf &&
                  isa<DeclRefExpr>(Address->getSubExpr()->IgnoreParenImpCasts()))
                E = Address->getSubExpr()->IgnoreParenImpCasts();
              if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
                const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
                if (!Local)
                  return false;
                // Looked up again by the callback before, like `pValue =
                // mp_Items.f_FindEqual(Key)`.
                if (auto It = FreshlyAssigned.find(resolveReferenceCapture(Local));
                    It != FreshlyAssigned.end() &&
                    Lambda->getCallOperator()
                        ->getASTContext()
                        .getSourceManager()
                        .isBeforeInTranslationUnit(It->second,
                                                   Compare->getBeginLoc()))
                  return true;
                const auto *Body = Lambda->getBody();
                return Local->hasLocalStorage() && Local->getInit() &&
                       !isChangedVariable(Local) &&
                       !Lambda->getCallOperator()
                            ->getASTContext()
                            .getSourceManager()
                            .isBeforeInTranslationUnit(Local->getLocation(),
                                                       Body->getBeginLoc()) &&
                       isLookupIntoActor(Local->getInit());
              }
              return isLookupIntoActor(E);
            };
            // Also the address of a reference, `&Value`.
            auto ComparedRef = [](const Expr *Side) -> const DeclRefExpr * {
              Side = Side->IgnoreParenImpCasts();
              if (const auto *Address = dyn_cast<UnaryOperator>(Side);
                  Address && Address->getOpcode() == UO_AddrOf)
                Side = Address->getSubExpr()->IgnoreParenImpCasts();
              return dyn_cast<DeclRefExpr>(Side);
            };
            const auto *Left = ComparedRef(Compare->getLHS());
            const auto *Right = ComparedRef(Compare->getRHS());
            // Compared with a fresh lookup anywhere, which its result tells
            // about, like `return mp_Items.f_FindEqual(Key) == &Value;`: one
            // whose result is acted on, a condition or returned, also through a
            // local it is saved in or a call it is given to, not one that is
            // discarded, `(void)(pEntry != ...)`, `fg_Log(pEntry != ...)` or
            // `bool bSame = pEntry != ...;` that nothing reads.
            ASTContext &CallbackContext =
                Lambda->getCallOperator()->getASTContext();
            std::function<bool(const Expr *, int)> ActedOn =
                [&](const Expr *E, int Depth) {
                  if (Depth > 4)
                    return true;
                  // What reads the local \p Saved acts on it.
                  auto ReadsActOn = [&](const ValueDecl *Saved) {
                    bool Acted = false;
                    std::function<void(const Stmt *)> Visit =
                        [&](const Stmt *Inner) {
                          if (!Inner || Acted)
                            return;
                          if (const auto *Ref = dyn_cast<DeclRefExpr>(Inner);
                              Ref && Ref->getDecl() == Saved)
                            Acted = ActedOn(Ref, Depth + 1);
                          for (const Stmt *Child : childrenOnce(Inner))
                            Visit(Child);
                        };
                    Visit(Lambda->getBody());
                    return Acted;
                  };
                  DynTypedNode Node = DynTypedNode::create(*E);
                  for (int Level = 0; Level < 32; ++Level) {
                    const auto Parents =
                        instantiatedParents(Node, CallbackContext);
                    if (Parents.empty())
                      return false;
                    const DynTypedNode &Parent = Parents[0];
                    const Stmt *Child = Node.get<Stmt>();
                    // An `if` acts on it when a branch leaves with an
                    // error, not when it only logs, `if (!bSame)
                    // fg_Log(...)`.
                    if (const auto *If = Parent.get<IfStmt>())
                      return Child == If->getCond() &&
                             (leavesWithError(If->getThen()) ||
                              leavesWithError(If->getElse()));
                    if (const auto *While = Parent.get<WhileStmt>())
                      return Child == While->getCond();
                    if (const auto *Do = Parent.get<DoStmt>())
                      return Child == Do->getCond();
                    if (const auto *For = Parent.get<ForStmt>())
                      return Child == For->getCond();
                    // What a call is given, `fg_Log(pEntry != ...)`, is acted on
                    // only as the call's result is.
                    if (Parent.get<ReturnStmt>())
                      return true;
                    if (Parent.get<CompoundStmt>() || Parent.get<LambdaExpr>())
                      return false;
                    if (const auto *Cast = Parent.get<ExplicitCastExpr>();
                        Cast && Cast->getType()->isVoidType())
                      return false;
                    if (const auto *Saved = Parent.get<VarDecl>())
                      return ReadsActOn(Saved);
                    if (const auto *Assign = Parent.get<BinaryOperator>();
                        Assign && Assign->isAssignmentOp()) {
                      if (Child != Assign->getRHS())
                        return false;
                      if (const auto *Target = dyn_cast<DeclRefExpr>(
                              Assign->getLHS()->IgnoreParenImpCasts());
                          Target && isa<VarDecl>(Target->getDecl()) &&
                          cast<VarDecl>(Target->getDecl())->hasLocalStorage())
                        return ReadsActOn(Target->getDecl());
                      return true;
                    }
                    // The left side of a comma is discarded.
                    if (const auto *Comma = Parent.get<BinaryOperator>();
                        Comma && Comma->getOpcode() == BO_Comma &&
                        Child == Comma->getLHS())
                      return false;
                    // The condition of a conditional whose value is acted
                    // on, not `bool bIgnored = p != ... ? true : false`.
                    // Which part of a conditional it is, is told by where it
                    // is, since the parents skip parentheses around it.
                    if (const auto *Conditional =
                            Parent.get<ConditionalOperator>()) {
                      const SourceManager &CallbackSM =
                          CallbackContext.getSourceManager();
                      if (CallbackSM.isBeforeInTranslationUnit(
                              Child->getBeginLoc(),
                              Conditional->getQuestionLoc()))
                        return ActedOn(Conditional, Depth + 1);
                      // A value of a conditional is acted on only when it is
                      // chosen: the other one must be an error, `bCheck ? (p
                      // == ... ? ...) : fg_MakeError(...)`.
                      const Expr *Other =
                          CallbackSM.isBeforeInTranslationUnit(
                              Child->getBeginLoc(), Conditional->getColonLoc())
                              ? Conditional->getFalseExpr()
                              : Conditional->getTrueExpr();
                      if (!isErrorResult(
                              skipWrappers(Other)->IgnoreParenImpCasts()))
                        return false;
                    } else if (const auto *Conditional =
                                   Parent.get<AbstractConditionalOperator>();
                               Conditional && Child == Conditional->getCond())
                      return ActedOn(Conditional, Depth + 1);
                    Node = Parent;
                  }
                  return true;
                };
            // Of a callback that ends the coroutine with an error, the
            // result when the pointer doesn't match, `p == pFresh` false, must
            // be one: not `p == pFresh || !pFresh ? CExceptionPointer{} :
            // ...`, which goes on when the entry is gone.
            const QualType CallbackResult =
                Lambda->getCallOperator()->getReturnType();
            const bool ErrorResult = !CallbackResult->isBooleanType() &&
                                     !CallbackResult->isIntegerType();
            // Through any number of unchanged copies of what tells it,
            // `bool b1 = b0;`, each once.
            llvm::SmallPtrSet<const VarDecl *, 8> FollowedSaved;
            std::function<bool(const Expr *, bool, int)> MismatchFails =
                [&](const Expr *E, bool Value, int Depth) {
                  if (Depth > 1024)
                    return false;
                  DynTypedNode Node = DynTypedNode::create(*E);
                  for (int Level = 0; Level < 32; ++Level) {
                    const auto Parents =
                        instantiatedParents(Node, CallbackContext);
                    if (Parents.empty())
                      return false;
                    const DynTypedNode &Parent = Parents[0];
                    const Stmt *Child = Node.get<Stmt>();
                    if (Parent.get<ParenExpr>() || Parent.get<ImplicitCastExpr>() ||
                        Parent.get<ExprWithCleanups>()) {
                      Node = Parent;
                      continue;
                    }
                    if (const auto *Not = Parent.get<UnaryOperator>();
                        Not && Not->getOpcode() == UO_LNot) {
                      Value = !Value;
                      Node = Parent;
                      continue;
                    }
                    // What decides a `&&` when false, or a `||` when true.
                    if (const auto *Logical = Parent.get<BinaryOperator>();
                        Logical && Logical->isLogicalOp()) {
                      if ((Logical->getOpcode() == BO_LAnd) == Value)
                        return false;
                      Node = Parent;
                      continue;
                    }
                    // The condition, also in parentheses, which the parents
                    // skip.
                    auto IsCondition = [&](const Expr *Condition) {
                      return Condition && Child &&
                             !CallbackContext.getSourceManager()
                                  .isBeforeInTranslationUnit(
                                      Child->getBeginLoc(),
                                      Condition->getBeginLoc()) &&
                             !CallbackContext.getSourceManager()
                                  .isBeforeInTranslationUnit(
                                      Condition->getEndLoc(),
                                      Child->getEndLoc());
                    };
                    // A statement of the body before what can return success,
                    // which runs on every resume that does.
                    if (const auto *If = Parent.get<IfStmt>()) {
                      if (!IsCondition(If->getCond()) ||
                          !Refs.TopLevel.contains(If))
                        return false;
                      if (const Stmt *Mismatch =
                              Value ? If->getThen() : If->getElse())
                        return leavesWithError(Mismatch);
                      // Without a branch for it, `if (p == pFresh) return
                      // {};`, what follows, when the other always leaves.
                      const auto Around =
                          instantiatedParents(*If, CallbackContext);
                      const auto *Block = Around.empty()
                                              ? nullptr
                                              : Around[0].get<CompoundStmt>();
                      if (!Block || !alwaysLeaves(If->getThen()))
                        return false;
                      const auto *Next = llvm::find(Block->body(), If);
                      return Next != Block->body_end() &&
                             ++Next != Block->body_end() &&
                             leavesWithError(*Next);
                    }
                    // Chosen by what is acted on: not a value of another
                    // conditional whose other value is success.
                    if (const auto *Conditional =
                            Parent.get<ConditionalOperator>()) {
                      if (!IsCondition(Conditional->getCond()))
                        return false;
                      return isErrorResult(skipWrappers(
                                               Value ? Conditional->getTrueExpr()
                                                     : Conditional->getFalseExpr())
                                               ->IgnoreParenImpCasts()) &&
                             ActedOn(Conditional, Depth + 1);
                    }
                    // Saved, `bool bSame = p == ...`, and acted on where it is
                    // read, unless it is changed before, `bSame = true`.
                    if (const auto *Saved = Parent.get<VarDecl>()) {
                      if (isChangedVariable(Saved) ||
                          !FollowedSaved.insert(Saved).second)
                        return false;
                      bool Fails = false;
                      std::function<void(const Stmt *)> Visit =
                          [&](const Stmt *Inner) {
                            if (!Inner || Fails)
                              return;
                            if (const auto *Ref = dyn_cast<DeclRefExpr>(Inner);
                                Ref && Ref->getDecl() == Saved)
                              Fails = MismatchFails(Ref, Value, Depth + 1);
                            for (const Stmt *Inside : childrenOnce(Inner))
                              Visit(Inside);
                          };
                      Visit(Lambda->getBody());
                      return Fails;
                    }
                    return false;
                  }
                  return false;
                };
            if (Fresh(Compare->getLHS()) != Fresh(Compare->getRHS()) &&
                (ErrorResult
                     ? MismatchFails(Compare, Compare->getOpcode() == BO_NE, 0)
                     : ActedOn(Compare, 0))) {
              if (Left)
                FreshCompared.insert(resolveReferenceCapture(Left->getDecl()));
              if (Right)
                FreshCompared.insert(resolveReferenceCapture(Right->getDecl()));
            }
            if (!Gate)
              continue;
            // A fresh lookup compared with a pointer kept from before, which
            // then still points to the same data: not with null. Two pointers
            // kept from before, `pValue != pSaved`, tell nothing of either.
            if (Fresh(Compare->getLHS()) == Fresh(Compare->getRHS())) {
              if (!Fresh(Compare->getLHS())) {
                if (Left)
                  StaleCompared.insert(resolveReferenceCapture(Left->getDecl()));
                if (Right)
                  StaleCompared.insert(resolveReferenceCapture(Right->getDecl()));
              }
              continue;
            }
            ASTContext &LambdaContext = Lambda->getCallOperator()->getASTContext();
            if (Compare->getLHS()->isNullPointerConstant(
                    LambdaContext, Expr::NPC_ValueDependentIsNotNull) ||
                Compare->getRHS()->isNullPointerConstant(
                    LambdaContext, Expr::NPC_ValueDependentIsNotNull))
              continue;
            if (Left)
              Compared.insert(resolveReferenceCapture(Left->getDecl()));
            if (Right)
              Compared.insert(resolveReferenceCapture(Right->getDecl()));
          }
          for (const ValueDecl *Ref : Refs.Decls) {
            const ValueDecl *Resolved = resolveReferenceCapture(Ref);
            if (!Copies.contains(Ref) && Compared.contains(Resolved))
              SameData[Variable].insert(Resolved);
          }
          // What refers into the data of the actor is re-validated only by a
          // fresh lookup, compared with it or assigned, or given to what can
          // assign it: not by reading it, like `*pEntry == 0`, nor by comparing
          // it with other pointers kept from before.
          // Also an iterator or a reference handle, which only testing,
          // `if (!iEntry)`, doesn't re-validate either.
          llvm::erase_if(Variables, [&](const ValueDecl *Validated) {
            const QualType Type = Validated->getType();
            const bool Refers = Type->isPointerType() ||
                                Type->isReferenceType() ||
                                isIteratorType(Type) || isReferenceHandle(Type);
            return (StaleCompared.contains(Validated) || Refers) &&
                   !Compared.contains(Validated) &&
                   !FreshCompared.contains(Validated) &&
                   !FreshlyAssigned.count(Validated) &&
                   !(Refs.Assigned.contains(Validated) &&
                     !Unfresh.contains(Validated));
          });
          for (const ValueDecl *Validated : Variables)
            if (FreshCompared.contains(Validated) &&
                !Refs.Assigned.contains(Validated) &&
                !FreshlyAssigned.count(Validated) &&
                !llvm::is_contained(ComparedOnly, Validated))
              ComparedOnly.push_back(Validated);
        }
        // A flag copied from an entry before, like `pDestroyed =
        // pEntry->m_pDestroyed`, that the callback tests and leaves with an
        // error for, like `if (*pDestroyed) return DMibErrorInstance(...)`,
        // re-validates the entry and what is made from it.
        if (Refs.bValidating && !isReferencedHolder(Variable)) {
          for (const ValueDecl *Entry : flagTestedEntries(Lambda->getBody())) {
            Variables.push_back(Entry);
            SameData[Variable].insert(Entry);
          }
        }
        // What it reads through, also of what it doesn't re-validate, which
        // it reads on every resume, `fg_Read(*pEntry)`.
        {
          llvm::SmallVector<const ValueDecl *, 8> Read(Variables.begin(),
                                                       Variables.end());
          for (const ValueDecl *Ref : Refs.Decls)
            if (const ValueDecl *Resolved = resolveReferenceCapture(Ref);
                !llvm::is_contained(Read, Resolved))
              Read.push_back(Resolved);
          addCallbackEarlyUses(Variable, Lambda, Read, Variables);
        }
        auto SavedCopies = std::move(CallbackCopies);
        CallbackCopies = std::move(Copies);
        const VarDecl *SavedHolder = CurrentHolder;
        const CompoundStmt *SavedBody = CurrentCallbackBody;
        CurrentHolder = Variable;
        CurrentCallbackBody = Lambda->getCompoundStmtBody();
        TraverseStmt(Lambda->getBody());
        CurrentHolder = SavedHolder;
        CurrentCallbackBody = SavedBody;
        CallbackCopies = std::move(SavedCopies);
      }
      if (auto Removed = HolderRemovals.find(Variable);
          Removed != HolderRemovals.end()) {
        llvm::erase_if(Variables, [&](const ValueDecl *Validated) {
          return mayRemove(Removed->second, Validated);
        });
        llvm::erase_if(ComparedOnly, [&](const ValueDecl *Validated) {
          return mayRemove(Removed->second, Validated);
        });
      }
      if (!Common)
        Common = std::move(Variables);
      else
        llvm::erase_if(*Common, [&](const ValueDecl *Validated) {
          return !llvm::is_contained(Variables, Validated);
        });
      if (!CommonCompared)
        CommonCompared = std::move(ComparedOnly);
      else
        llvm::erase_if(*CommonCompared, [&](const ValueDecl *Validated) {
          return !llvm::is_contained(ComparedOnly, Validated);
        });
    }
    if (!bEveryAlternative) {
      Common->clear();
      CommonCompared->clear();
    }
    llvm::append_range(Revalidations[Variable], *Common);
    llvm::append_range(ComparedRevalidations[Variable], *CommonCompared);
  }

  /// The collections of the data of the actor a callback can remove entries
  /// of, or whether that can't be told.
  struct CRemovals {
    llvm::SmallPtrSet<const FieldDecl *, 2> Fields;
    bool bUnknown = false;
    bool any() const { return bUnknown || !Fields.empty(); }
  };

  /// What \p Body removes entries of, with a member function like `f_Remove`
  /// or `f_Clear` of a collection of the data of the actor, also through the
  /// member functions of the actor it calls, `f_Tidy()`, and the lambdas it
  /// calls, `[&] { mp_Items.f_Clear(); }()`, and those of a scope exit it
  /// makes, `g_OnScopeExit / [this] { mp_Items.f_Clear(); }`, which runs
  /// before it returns, but not in the other lambdas it only makes, nor in a
  /// branch a constant condition never takes, `if (false)`. A member
  /// function of the actor that isn't const and can't be looked into can
  /// remove anything.
  CRemovals removals(const Stmt *Body) const {
    CRemovals Removals;
    llvm::SmallPtrSet<const Stmt *, 8> Visited;
    // A scope exit whose guard is a local of the callback that it clears,
    // `Rollback.f_Clear();`, before it can return without an error, after
    // which the coroutine doesn't resume, never removes what the coroutine
    // reads after.
    if (const auto *Compound = dyn_cast_or_null<CompoundStmt>(Body)) {
      const auto Statements = Compound->body();
      for (auto It = Statements.begin(); It != Statements.end(); ++It) {
        const auto *Declaration = dyn_cast<DeclStmt>(*It);
        const auto *Guard =
            Declaration && Declaration->isSingleDecl()
                ? dyn_cast<VarDecl>(Declaration->getSingleDecl())
                : nullptr;
        const LambdaExpr *Exit = Guard && !Guard->getType()->isReferenceType()
                                     ? scopeExitOperand(Guard->getInit())
                                     : nullptr;
        if (!Exit)
          continue;
        if (isClearedBeforeLeaving(Guard, Compound, [](const Stmt *S) {
              return mayReturnSuccess(S);
            }))
          Visited.insert(Exit->getBody());
      }
    }
    auto Children = [&](const Stmt *S) {
      return Context ? evaluatedChildren(S, *Context) : evaluatedChildren(S);
    };
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      // Not what isn't evaluated, `noexcept(mp_Items.f_Clear())`.
      if (!S || Removals.bUnknown ||
          isa<LambdaExpr, UnaryExprOrTypeTraitExpr, CXXNoexceptExpr>(S))
        return;
      if (const auto *Typeid = dyn_cast<CXXTypeidExpr>(S);
          Typeid && !Typeid->isPotentiallyEvaluated())
        return;
      if (const auto *Made = dyn_cast<Expr>(S))
        if (const LambdaExpr *Exit = scopeExitOperand(Made);
            Exit && Visited.insert(Exit->getBody()).second)
          Visit(Exit->getBody());
      // Also what moves the entries of a collection whose entries don't
      // stay where they are, `m_Items.reserve(...)` of a vector, which leaves
      // what points to them dangling.
      static const llvm::SmallVector<StringRef, 16> RelocatingMethods = [] {
        llvm::SmallVector<StringRef, 16> List;
        splitOptionList("f_Reserve*;f_Insert*;f_Add*;f_Append*;f_Emplace*;"
                        "f_Push*;f_Shrink*;f_SetCapacity*;reserve;"
                        "shrink_to_fit;push_back;push_front;emplace*;insert*;"
                        "append",
                        List);
        return List;
      }();
      // A collection given to a free helper that can change it, `fg_Erase(
      // mp_Items)` with `TCMap<int, int> &_Items`, which removes entries
      // when it removes them or assigns it, or when it can't be looked into.
      if (const auto *Helper = dyn_cast<CallExpr>(S);
          Helper && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Helper))
        if (const FunctionDecl *Callee = Helper->getDirectCallee())
          for (unsigned iArgument = 0; iArgument < Helper->getNumArgs() &&
                                       iArgument < Callee->getNumParams();
               ++iArgument) {
            const QualType Type = Callee->getParamDecl(iArgument)->getType();
            if (!(Type->isReferenceType() || Type->isPointerType()) ||
                Type->getPointeeType().isConstQualified())
              continue;
            const Expr *Argument = Helper->getArg(iArgument);
            const FieldDecl *Field = outermostField(Argument);
            if (!Field || !isRooted(Argument) ||
                refersToOwnObject(Argument))
              continue;
            const FunctionDecl *Definition = Callee->getDefinition();
            if (!Definition || !Definition->getBody()) {
              Removals.Fields.insert(Field);
              continue;
            }
            const ParmVarDecl *Parameter =
                Definition->getParamDecl(iArgument);
            // The parameter, also through a local reference or pointer to
            // it, `auto &Items = _Items` or `auto *pItems = &_Items`.
            auto IsParameter = [&](const Expr *E) {
              for (int Depth = 0; E && Depth < 8; ++Depth) {
                E = E->IgnoreParenImpCasts();
                if (const auto *Address = dyn_cast<UnaryOperator>(E);
                    Address && Address->getOpcode() == UO_AddrOf) {
                  E = Address->getSubExpr();
                  continue;
                }
                const auto *Ref = dyn_cast<DeclRefExpr>(E);
                if (!Ref)
                  return false;
                if (Ref->getDecl() == Parameter)
                  return true;
                const auto *Alias = dyn_cast<VarDecl>(Ref->getDecl());
                if (!Alias || !Alias->hasLocalStorage() ||
                    isa<ParmVarDecl>(Alias) ||
                    !(Alias->getType()->isReferenceType() ||
                      Alias->getType()->isPointerType()))
                  return false;
                E = Alias->getInit();
              }
              return false;
            };
            bool Removes = false;
            std::function<void(const Stmt *)> Find = [&](const Stmt *Inner) {
              if (!Inner || Removes)
                return;
              const Expr *Object = nullptr;
              // Also one that moves the entries of what doesn't keep them
              // where they are, `_Items.f_Insert(42)` of a vector.
              if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Inner);
                  Call && Call->getMethodDecl() &&
                  Call->getMethodDecl()->getIdentifier() &&
                  (matchesNamePattern(Call->getMethodDecl()->getName(),
                                      removingMethods()) ||
                   (matchesNamePattern(Call->getMethodDecl()->getName(),
                                       RelocatingMethods) &&
                    Context &&
                    !matchesTypeName(
                        Call->getImplicitObjectArgument()->getType(),
                        NodeStableContainers, *Context))))
                Object = Call->getImplicitObjectArgument();
              else if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
                       Assign && Assign->getOpcode() == BO_Assign)
                Object = Assign->getLHS();
              else if (const auto *Operator =
                           dyn_cast<CXXOperatorCallExpr>(Inner);
                       Operator && Operator->getOperator() == OO_Equal &&
                       Operator->getNumArgs() == 2)
                Object = Operator->getArg(0);
              // Or given on to what can't be told, not by a pointer or a
              // reference to const, `fg_Count(&_Items)` of `TCMap const *`.
              else if (const auto *Call = dyn_cast<CallExpr>(Inner);
                       Call && !isa<CXXMemberCallExpr>(Call)) {
                const FunctionDecl *Callee = Call->getDirectCallee();
                const unsigned Offset =
                    isa<CXXOperatorCallExpr>(Call) &&
                            isa_and_nonnull<CXXMethodDecl>(Callee) &&
                            cast<CXXMethodDecl>(Callee)
                                ->isImplicitObjectMemberFunction()
                        ? 1
                        : 0;
                for (unsigned iArgument = 0; iArgument < Call->getNumArgs();
                     ++iArgument) {
                  const Expr *Given = Call->getArg(iArgument);
                  QualType Type = Given->getType();
                  if (Type->isPointerType())
                    Type = Type->getPointeeType();
                  if (Callee && iArgument >= Offset &&
                      iArgument - Offset < Callee->getNumParams())
                    if (const QualType Parameter =
                            Callee->getParamDecl(iArgument - Offset)->getType();
                        Parameter->isReferenceType() ||
                        Parameter->isPointerType())
                      Type = Parameter->getPointeeType();
                  if (IsParameter(Given) && !Type.isConstQualified())
                    Removes = true;
                }
              }
              if (Object) {
                const Expr *Base = Object->IgnoreParenImpCasts();
                // Also through a local iterator made from it, `auto iEntry =
                // _Items.f_GetIterator(); iEntry.f_Remove();`.
                for (int Depth = 0; Depth < 4; ++Depth) {
                  const auto *Ref = dyn_cast<DeclRefExpr>(Base);
                  const auto *Local =
                      Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
                  if (!Local || !Local->hasLocalStorage() ||
                      isa<ParmVarDecl>(Local) || !Local->getInit() ||
                      !isIteratorType(Local->getType().getNonReferenceType()))
                    break;
                  const Expr *Init = Local->getInit()->IgnoreImplicit();
                  if (const auto *Copy = dyn_cast<CXXConstructExpr>(Init);
                      Copy && Copy->getNumArgs() == 1)
                    Init = Copy->getArg(0)->IgnoreImplicit();
                  const auto *Made =
                      dyn_cast<CXXMemberCallExpr>(Init->IgnoreParenImpCasts());
                  if (!Made || !Made->getImplicitObjectArgument())
                    break;
                  Base =
                      Made->getImplicitObjectArgument()->IgnoreParenImpCasts();
                }
                while (const auto *Member = dyn_cast<MemberExpr>(Base))
                  Base = Member->getBase()->IgnoreParenImpCasts();
                if (const auto *Deref = dyn_cast<UnaryOperator>(Base);
                    Deref && Deref->getOpcode() == UO_Deref)
                  Base = Deref->getSubExpr()->IgnoreParenImpCasts();
                if (IsParameter(Base))
                  Removes = true;
              }
              for (const Stmt *Child : Children(Inner))
                Find(Child);
            };
            Find(Definition->getBody());
            if (Removes)
              Removals.Fields.insert(Field);
          }
      // Also through a local iterator into it, `auto iEntry =
      // mp_Items.f_GetIterator(1); iEntry.f_Remove();`, what it was made
      // from, or anything when it is changed.
      auto RemovedFrom = [&](const Expr *Object) -> const Expr * {
        const auto *Ref = dyn_cast<DeclRefExpr>(Object->IgnoreParenImpCasts());
        const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
            !isIteratorType(Local->getType().getNonReferenceType()) ||
            !Local->getInit())
          return Object;
        if (const Expr *Value = assignedValueAt(Local, Ref))
          return Value;
        Removals.bUnknown = true;
        return Object;
      };
      // Also through one of its own that does, `f_GetArray(_Len)` of
      // `f_SetLen(_Len); return f_GetArray();`.
      auto Named = [](llvm::ArrayRef<StringRef> Patterns) {
        return [Patterns](const CXXMethodDecl *Method) {
          return Method->getIdentifier() &&
                 matchesNamePattern(Method->getName(), Patterns);
        };
      };
      // Also a member operator of it that does, `mp_Values << 42` of an
      // `operator <<` that inserts with `f_Insert`.
      if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S);
          Operator && Operator->getNumArgs() >= 1 &&
          Operator->getOperator() != OO_Call && !Operator->isAssignmentOp())
        if (const auto *Method =
                dyn_cast_or_null<CXXMethodDecl>(Operator->getDirectCallee());
            Method && !hasConstObject(Method) && isRooted(Operator->getArg(0)))
          if (const FieldDecl *Field = outermostField(Operator->getArg(0))) {
            if (callsOwnMethod(Method, Named(removingMethods())))
              Removals.Fields.insert(Field);
            else if (callsOwnMethod(Method, Named(RelocatingMethods)) &&
                     Context &&
                     !matchesTypeName(Operator->getArg(0)->getType(),
                                      NodeStableContainers, *Context))
              Removals.Fields.insert(Field);
          }
      if (const auto *Call = dyn_cast<CXXMemberCallExpr>(S);
          Call && Call->getMethodDecl() &&
          callsOwnMethod(Call->getMethodDecl(), Named(RelocatingMethods)) &&
          isRooted(Call->getImplicitObjectArgument()))
        if (const FieldDecl *Field =
                outermostField(Call->getImplicitObjectArgument());
            Field && Context &&
            Call->getImplicitObjectArgument()
                ->IgnoreParenImpCasts()
                ->getType()
                ->getAsCXXRecordDecl() &&
            !matchesTypeName(Call->getImplicitObjectArgument()->getType(),
                             NodeStableContainers, *Context))
          Removals.Fields.insert(Field);
      if (const auto *Call = dyn_cast<CXXMemberCallExpr>(S);
          Call && Call->getMethodDecl() &&
          callsOwnMethod(Call->getMethodDecl(), Named(removingMethods())) &&
          isRooted(RemovedFrom(Call->getImplicitObjectArgument()))) {
        if (const FieldDecl *Field =
                outermostField(RemovedFrom(Call->getImplicitObjectArgument())))
          Removals.Fields.insert(Field);
        else
          Removals.bUnknown = true;
      } else if (const auto *Own = dyn_cast<CXXMemberCallExpr>(S);
                 Own && Own->getMethodDecl() &&
                 Own->getImplicitObjectArgument() &&
                 refersToOwnObject(Own->getImplicitObjectArgument())) {
        // On the object, also through an unchanged local pointer to it,
        // `pThis->f_Tidy()`, and any override of a virtual one.
        for (const CXXMethodDecl *Method : dispatchedMethods(Own)) {
          const FunctionDecl *Definition = nullptr;
          if (Method->hasBody(Definition) && Definition) {
            if (Visited.insert(Definition->getBody()).second)
              Visit(Definition->getBody());
          } else if (!Method->isConst() && !Method->isStatic())
            Removals.bUnknown = true;
        }
      } else if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(S);
                 Invoke && Invoke->getOperator() == OO_Call &&
                 Invoke->getNumArgs() >= 1) {
        if (const LambdaExpr *Lambda = calledLambda(Invoke->getArg(0));
            Lambda && Visited.insert(Lambda->getBody()).second)
          Visit(Lambda->getBody());
      } else if (const auto *Free = dyn_cast<CallExpr>(S);
                 Free && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Free) &&
                 llvm::any_of(Free->arguments(), [](const Expr *Argument) {
                   return refersToOwnObject(Argument);
                 })) {
        // A free helper given the object, `fg_Clear(this)`, removes what it
        // does, and one that can't be looked into can remove anything.
        const FunctionDecl *Callee = Free->getDirectCallee();
        const FunctionDecl *Definition = nullptr;
        if (Callee && Callee->hasBody(Definition) && Definition) {
          if (Visited.insert(Definition->getBody()).second)
            Visit(Definition->getBody());
        } else
          Removals.bUnknown = true;
      }
      // Assigning a collection of the data, `mp_Items = {}`, removes its
      // entries.
      const Expr *Assigned = nullptr;
      if (const auto *Assign = dyn_cast<BinaryOperator>(S);
          Assign && Assign->getOpcode() == BO_Assign)
        Assigned = Assign->getLHS();
      else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S);
               Operator && Operator->getOperator() == OO_Equal &&
               Operator->getNumArgs() == 2)
        Assigned = Operator->getArg(0);
      // Also called by name, `mp_Items.operator=(...)`.
      else if (const auto *Named = dyn_cast<CXXMemberCallExpr>(S);
               Named && Named->getMethodDecl() &&
               Named->getMethodDecl()->getOverloadedOperator() == OO_Equal)
        Assigned = Named->getImplicitObjectArgument();
      // Also through a local reference to it, `auto &Items = mp_Items; Items =
      // {};`.
      const auto *AssignedRef =
          Assigned ? dyn_cast<DeclRefExpr>(Assigned->IgnoreParenImpCasts())
                   : nullptr;
      if (Assigned && Assigned->getType()->isRecordType() &&
          (isa<MemberExpr>(Assigned->IgnoreParenImpCasts()) ||
           (AssignedRef &&
            AssignedRef->getDecl()->getType()->isReferenceType() &&
            !isa<ParmVarDecl>(AssignedRef->getDecl()))) &&
          isRooted(Assigned)) {
        if (const FieldDecl *Field = outermostField(Assigned))
          Removals.Fields.insert(Field);
        else
          Removals.bUnknown = true;
      }
      for (const Stmt *Child : Children(S))
        Visit(Child);
    };
    Visit(Body);
    return Removals;
  }

  /// The collections \p Variable can be a lookup into: of its initializer and
  /// what it is assigned, or whether that can't be told.
  CRemovals lookupFields(const ValueDecl *Variable) const {
    CRemovals Fields;
    bool bAny = false;
    auto Add = [&](const Expr *Value) {
      if (!Value || !isLookupIntoActor(Value))
        return;
      bAny = true;
      if (const FieldDecl *Field = outermostField(Value))
        Fields.Fields.insert(Field);
      else
        Fields.bUnknown = true;
    };
    if (const auto *Local = dyn_cast<VarDecl>(Variable))
      Add(Local->getInit());
    for (const auto &[Ref, Value] : AssignedRefs)
      if (resolveReferenceCapture(Ref->getDecl()) == Variable)
        Add(Value);
    if (!bAny)
      Fields.bUnknown = true;
    return Fields;
  }

public:
  /// Whether removing \p Removals can remove what \p Variable refers to.
  bool mayRemove(const CRemovals &Removals, const ValueDecl *Variable) const {
    if (!Removals.any())
      return false;
    if (Removals.bUnknown)
      return true;
    const CRemovals Fields = lookupFields(Variable);
    return Fields.bUnknown ||
           llvm::any_of(Fields.Fields, [&](const FieldDecl *Field) {
             return Removals.Fields.contains(Field);
           });
  }

  /// What the callback of each holder removes entries of.
  llvm::DenseMap<const VarDecl *, CRemovals> HolderRemovals;

private:

  /// The entries whose copied flags \p Body tests in an `if` that leaves with
  /// an error: what the flag variable was copied from a member of.
  static llvm::SmallVector<const ValueDecl *, 2>
  flagTestedEntries(const Stmt *Body) {
    llvm::SmallVector<const ValueDecl *, 2> Entries;
    // In order, until the callback can return success: a test after that,
    // or one a jump can pass, doesn't always run.
    const auto *Compound = dyn_cast_or_null<CompoundStmt>(Body);
    if (!Compound || hasJumps(Compound))
      return Entries;
    for (const Stmt *S : Compound->body()) {
      if (const auto *If = dyn_cast<IfStmt>(S);
          If && !If->getInit() && !If->getConditionVariable() &&
          leavesWithError(If->getThen())) {
        collectFlags(If->getCond(), Entries);
        if (mayReturnSuccess(If->getElse()))
          break;
        continue;
      }
      if (mayReturnSuccess(S))
        break;
    }
    return Entries;
  }

  /// The comparisons of pointers that \p Body, a callback, leaves with an
  /// error for when they don't match, like `if (pEntry !=
  /// mp_Items.f_FindEqual(Key)) return DMibErrorInstance(...)`, in order,
  /// until the callback can return success: a test after that, or one a jump
  /// can pass, doesn't always run, and one whose result isn't acted on
  /// doesn't keep the coroutine from going on.
  static llvm::SmallPtrSet<const BinaryOperator *, 4>
  mismatchTests(const Stmt *Body) {
    llvm::SmallPtrSet<const BinaryOperator *, 4> Tests;
    const auto *Compound = dyn_cast_or_null<CompoundStmt>(Body);
    if (!Compound || hasJumps(Compound))
      return Tests;
    for (const Stmt *S : Compound->body()) {
      const auto *If = dyn_cast<IfStmt>(S);
      if (If && !If->getInit() && !If->getConditionVariable()) {
        if (leavesWithError(If->getThen())) {
          collectMismatches(If->getCond(), /*Error=*/true, Tests, Body);
          if (mayReturnSuccess(If->getElse()))
            break;
          continue;
        }
        if (If->getElse() && leavesWithError(If->getElse())) {
          collectMismatches(If->getCond(), /*Error=*/false, Tests, Body);
          if (mayReturnSuccess(If->getThen()))
            break;
          continue;
        }
      }
      if (mayReturnSuccess(S))
        break;
    }
    return Tests;
  }

  /// Collects the comparisons in \p E, a condition that leads to an error
  /// when it is \p Error, that are true or false when the pointers don't
  /// match: `!=` for true and `==` for false, also combined with other
  /// conditions and through `!`.
  static void
  collectMismatches(const Expr *E, bool Error,
                    llvm::SmallPtrSetImpl<const BinaryOperator *> &Tests,
                    const Stmt *Callback) {
    if (!E)
      return;
    E = E->IgnoreParenImpCasts();
    if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(E))
      return collectMismatches(Cleanups->getSubExpr(), Error, Tests, Callback);
    if (const auto *Unary = dyn_cast<UnaryOperator>(E);
        Unary && Unary->getOpcode() == UO_LNot)
      return collectMismatches(Unary->getSubExpr(), !Error, Tests, Callback);
    const auto *Binary = dyn_cast<BinaryOperator>(E);
    if (!Binary)
      return;
    // Through the other conditions it is combined with: a mismatch makes
    // `A || Mismatch` lead to the error, but `A && Mismatch` only when `A` is
    // true, so only beside a test of whether a pointer it compares is set,
    // like `pInitial && pEntry != pInitial`. The same for `&&` and `||`
    // leading away from it.
    if (Binary->isLogicalOp()) {
      if ((Binary->getOpcode() == BO_LOr) == Error) {
        collectMismatches(Binary->getLHS(), Error, Tests, Callback);
        collectMismatches(Binary->getRHS(), Error, Tests, Callback);
        return;
      }
      if (testsPointerSet(Binary->getLHS(), Binary->getRHS(), Error, Callback))
        collectMismatches(Binary->getRHS(), Error, Tests, Callback);
      if (testsPointerSet(Binary->getRHS(), Binary->getLHS(), Error, Callback))
        collectMismatches(Binary->getLHS(), Error, Tests, Callback);
      return;
    }
    if ((Binary->getOpcode() == BO_NE && Error) ||
        (Binary->getOpcode() == BO_EQ && !Error))
      Tests.insert(Binary);
  }

  /// Whether \p Guard tests that a pointer that \p Other reads is set, like
  /// `pInitial` or `pInitial != nullptr`, or, unless \p Set, that it isn't,
  /// like `!pInitial`: a pointer kept from before, not a fresh lookup that
  /// \p Callback makes before the test, which is null when the entry is gone.
  static bool testsPointerSet(const Expr *Guard, const Expr *Other, bool Set,
                              const Stmt *Callback) {
    Guard = Guard->IgnoreParenImpCasts();
    if (const auto *Unary = dyn_cast<UnaryOperator>(Guard);
        Unary && Unary->getOpcode() == UO_LNot)
      return testsPointerSet(Unary->getSubExpr(), Other, !Set, Callback);
    const Expr *Pointer = Guard;
    if (const auto *Binary = dyn_cast<BinaryOperator>(Guard);
        Binary && Binary->isEqualityOp()) {
      auto IsNull = [](const Expr *E) {
        E = E->IgnoreParenImpCasts();
        return isa<CXXNullPtrLiteralExpr, GNUNullExpr>(E) ||
               (isa<IntegerLiteral>(E) &&
                cast<IntegerLiteral>(E)->getValue().isZero());
      };
      if (IsNull(Binary->getRHS()))
        Pointer = Binary->getLHS();
      else if (IsNull(Binary->getLHS()))
        Pointer = Binary->getRHS();
      else
        return false;
      if (Binary->getOpcode() == BO_EQ)
        Set = !Set;
    }
    if (!Set)
      return false;
    const auto *Ref = dyn_cast<DeclRefExpr>(Pointer->IgnoreParenImpCasts());
    if (!Ref || !Ref->getType()->isPointerType())
      return false;
    // Declared or assigned by the callback.
    if (Callback) {
      if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
          Local && Callback->getSourceRange().fullyContains(
                       SourceRange(Local->getLocation())))
        return false;
      // Assigned before the test, like a fresh lookup; the pointer found
      // first, `pInitial = pValue` after the test, is kept from before.
      const SourceManager &SM = Ref->getDecl()->getASTContext().getSourceManager();
      bool Assigned = false;
      std::function<void(const Stmt *)> FindAssignment = [&](const Stmt *S) {
        if (!S || Assigned)
          return;
        if (const auto *Assign = dyn_cast<BinaryOperator>(S);
            Assign && Assign->isAssignmentOp() &&
            SM.isBeforeInTranslationUnit(Assign->getBeginLoc(),
                                         Guard->getBeginLoc()))
          if (const auto *Target = dyn_cast<DeclRefExpr>(
                  Assign->getLHS()->IgnoreParenImpCasts());
              Target && Target->getDecl() == Ref->getDecl())
            Assigned = true;
        for (const Stmt *Child : childrenOnce(S))
          FindAssignment(Child);
      };
      FindAssignment(Callback);
      if (Assigned)
        return false;
    }
    bool Reads = false;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || Reads)
        return;
      if (const auto *Inner = dyn_cast<DeclRefExpr>(S);
          Inner && Inner->getDecl() == Ref->getDecl())
        Reads = true;
      for (const Stmt *Child : childrenOnce(S))
        Visit(Child);
    };
    Visit(Other);
    return Reads;
  }


  /// Whether \p Value can be an empty result, like `{}` or `nullptr`, also
  /// one of the values of a conditional.
  static bool isEmptyValue(const Expr *Value) { return !isErrorResult(Value); }

  /// The statements of \p Body, a callback, that run on every path that
  /// succeeds: those up to and with the first one that can return without an
  /// error, `if (_bSkip) return nullptr;`, also past what wraps them, like the
  /// cleanups of `pEntry = mp_Items.f_FindEqual(0)` with a key given by
  /// reference, and those of a block in it, `{ pEntry = ...; }`, which runs
  /// each time it does, and those before what can leave a lambda it calls
  /// there that captures nothing by copy, `[&] { pEntry = ...; }()`.
  static llvm::SmallPtrSet<const Stmt *, 8>
  statementsBeforeSuccess(const Stmt *Body) {
    llvm::SmallPtrSet<const Stmt *, 8> Statements;
    // The least depth each lambda has been added at, past which again adds
    // nothing more: a chain of helpers that each call the one before more
    // than once, `auto f1 = [&] { f0(); f0(); };`, would add the first a
    // number of times that grows exponentially with the chain.
    llvm::DenseMap<const LambdaExpr *, int> AddedAt;
    std::function<void(const Stmt *, int)> AddCalled =
        [&](const Stmt *Statement, int Depth) {
          const auto *Call = dyn_cast<CallExpr>(Statement);
          const LambdaExpr *Called = Call ? calledLambda(Call) : nullptr;
          if (!Called || Depth >= 16 ||
              llvm::any_of(Called->captures(),
                           [](const LambdaCapture &Capture) {
                             return Capture.getCaptureKind() == LCK_ByCopy;
                           }))
            return;
          if (auto [It, Inserted] = AddedAt.try_emplace(Called, Depth);
              !Inserted) {
            if (It->second <= Depth)
              return;
            It->second = Depth;
          }
          for (const Stmt *Inner : statementsBeforeLeaving(Called->getBody())) {
            Statements.insert(Inner);
            if (const auto *E = dyn_cast<Expr>(Inner))
              AddCalled(E->IgnoreImplicit()->IgnoreParens(), Depth + 1);
          }
        };
    std::function<bool(const CompoundStmt *, int)> Add =
        [&](const CompoundStmt *Block, int Depth) {
          for (const Stmt *Statement : Block->body()) {
            Statements.insert(Statement);
            if (const auto *E = dyn_cast<Expr>(Statement)) {
              Statements.insert(E->IgnoreImplicit()->IgnoreParens());
              AddCalled(E->IgnoreImplicit()->IgnoreParens(), Depth);
            }
            if (const auto *Inner = dyn_cast<CompoundStmt>(Statement);
                Inner && Depth < 16) {
              if (!Add(Inner, Depth + 1))
                return false;
              continue;
            }
            // Nor past a jump, `if (_bSkip) goto Done;`, which can skip
            // them, or a label, which one can jump to past them.
            if (mayReturnSuccess(Statement) || hasJumps(Statement))
              return false;
          }
          return true;
        };
    if (const auto *Compound = dyn_cast_or_null<CompoundStmt>(Body))
      Add(Compound, 0);
    return Statements;
  }

  /// Whether \p S can return without an error, like `return {};`.
  static bool mayReturnSuccess(const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return false;
    if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
      if (!Return->getRetValue())
        return true;
      const Expr *Value = skipWrappers(Return->getRetValue())->IgnoreParenImpCasts();
      return isEmptyValue(Value);
    }
    // Not in a branch an `if constexpr` discards.
    for (const Stmt *Child : evaluatedChildren(S)) {
      if (mayReturnSuccess(Child))
        return true;
    }
    return false;
  }

  /// Whether \p S always leaves the callback, with a return or a throw.
  static bool alwaysLeaves(const Stmt *S) {
    if (const auto *Compound = dyn_cast_or_null<CompoundStmt>(S))
      return !Compound->body_empty() && alwaysLeaves(Compound->body_back());
    if (const auto *If = dyn_cast_or_null<IfStmt>(S))
      return If->getElse() && alwaysLeaves(If->getThen()) &&
             alwaysLeaves(If->getElse());
    if (const auto *E = dyn_cast_or_null<Expr>(S))
      return isa<CXXThrowExpr>(E->IgnoreParenImpCasts());
    return isa_and_nonnull<ReturnStmt>(S);
  }

  static bool leavesWithError(const Stmt *S) {
    if (mayReturnSuccess(S))
      return false;
    if (const auto *Compound = dyn_cast_or_null<CompoundStmt>(S))
      return !Compound->body_empty() && leavesWithError(Compound->body_back());
    if (const auto *Return = dyn_cast_or_null<ReturnStmt>(S)) {
      const Expr *Value = Return->getRetValue();
      if (!Value)
        return false;
      return !isEmptyValue(Value);
    }
    // Both branches, like `if (bDetailed) return ...; else return ...;`.
    if (const auto *If = dyn_cast_or_null<IfStmt>(S))
      return If->getElse() && leavesWithError(If->getThen()) &&
             leavesWithError(If->getElse());
    if (const auto *E = dyn_cast_or_null<Expr>(S))
      return isa<CXXThrowExpr>(E->IgnoreParenImpCasts());
    return false;
  }

  static void collectFlags(const Expr *E,
                           llvm::SmallVectorImpl<const ValueDecl *> &Entries) {
    if (!E)
      return;
    E = E->IgnoreParenImpCasts();
    if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(E))
      return collectFlags(Cleanups->getSubExpr(), Entries);
    // Only a flag tested as such, alone or in a disjunction: a negated flag
    // leaves when it is not set.
    if (const auto *Binary = dyn_cast<BinaryOperator>(E)) {
      if (Binary->getOpcode() == BO_LOr) {
        collectFlags(Binary->getLHS(), Entries);
        collectFlags(Binary->getRHS(), Entries);
      }
      // Compared to be set, like `*pDeleted == true`.
      if (const Expr *Flag = comparedToBeSet(Binary))
        collectFlags(Flag, Entries);
      return;
    }
    const Expr *Operand = nullptr;
    if (const auto *Unary = dyn_cast<UnaryOperator>(E);
        Unary && Unary->getOpcode() == UO_Deref)
      Operand = Unary->getSubExpr();
    else if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
             Call && Call->getOperator() == OO_Star && Call->getNumArgs() == 1)
      Operand = Call->getArg(0);
    // Also an atomic one loaded, `pDeleted->f_Load()`.
    else if (const auto *Load = dyn_cast<CXXMemberCallExpr>(E);
             Load && Load->getMethodDecl() &&
             Load->getMethodDecl()->getIdentifier() &&
             (Load->getMethodDecl()->getName() == "f_Load" ||
              Load->getMethodDecl()->getName() == "load")) {
      const Expr *Object =
          Load->getImplicitObjectArgument()->IgnoreParenImpCasts();
      const auto *Named =
          dyn_cast<MemberExpr>(Load->getCallee()->IgnoreParens());
      if (const auto *Arrow = dyn_cast<CXXOperatorCallExpr>(Object);
          Arrow && Arrow->getOperator() == OO_Arrow && Arrow->getNumArgs() == 1)
        Operand = Arrow->getArg(0);
      else if (const auto *Deref = dyn_cast<UnaryOperator>(Object);
               Deref && Deref->getOpcode() == UO_Deref)
        Operand = Deref->getSubExpr();
      else if (const auto *Dereferenced = dyn_cast<CXXOperatorCallExpr>(Object);
               Dereferenced && Dereferenced->getOperator() == OO_Star &&
               Dereferenced->getNumArgs() == 1)
        Operand = Dereferenced->getArg(0);
      else if (Named && Named->isArrow())
        Operand = Object;
    }
    if (!Operand)
      return;
    const auto *Ref = dyn_cast<DeclRefExpr>(Operand->IgnoreParenImpCasts());
    if (!Ref)
      return;
    const auto *Flag = dyn_cast<VarDecl>(resolveReferenceCapture(Ref->getDecl()));
    const Expr *Init =
        Flag && Flag->getInit() ? skipWrappers(Flag->getInit()) : nullptr;
    // Through the members to the entry the flag was copied from, a member
    // that tells it was removed, like `m_pDestroyed`.
    const Expr *Base = Init ? Init->IgnoreParenImpCasts() : nullptr;
    if (const auto *Flag = dyn_cast_or_null<MemberExpr>(Base);
        !Flag || !isRemovalFlagName(Flag->getMemberDecl()))
      return;
    bool Member = false;
    while (const auto *Access = dyn_cast_or_null<MemberExpr>(Base)) {
      Member = true;
      Base = Access->getBase()->IgnoreParenImpCasts();
      if (const auto *Deref = dyn_cast<UnaryOperator>(Base);
          Deref && Deref->getOpcode() == UO_Deref)
        Base = Deref->getSubExpr()->IgnoreParenImpCasts();
      // What a handle refers to.
      else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Base);
               Operator && Operator->getNumArgs() == 1 &&
               (Operator->getOperator() == OO_Star ||
                Operator->getOperator() == OO_Arrow) &&
               isReferenceHandle(Operator->getArg(0)->getType()))
        Base = Operator->getArg(0)->IgnoreParenImpCasts();
      else if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Base);
               Call && isReferenceHandle(Call->getImplicitObjectArgument()
                                             ->IgnoreParenImpCasts()
                                             ->getType()))
        Base = Call->getImplicitObjectArgument()->IgnoreParenImpCasts();
    }
    // Also a copy that a mutable callback captured, which it can change.
    if (const auto *Entry = dyn_cast_or_null<DeclRefExpr>(Base);
        Entry && Member && !isChangedVariable(Flag) && isUnchangedAt(Ref)) {
      const ValueDecl *Resolved = resolveReferenceCapture(Entry->getDecl());
      // What the flag was copied from must still be that entry.
      if (const auto *Variable = dyn_cast<VarDecl>(Resolved);
          Variable && !Variable->getType()->isReferenceType() &&
          isChangedVariable(Variable))
        return;
      Entries.push_back(Resolved);
    }
  }

  /// Whether \p Holder is used other than by being destroyed, like moved
  /// away, which can end the registration before its scope does.
  static bool isReferencedHolder(const VarDecl *Holder) {
    const auto *Function =
        dyn_cast_or_null<FunctionDecl>(Holder->getParentFunctionOrMethod());
    if (!Function || !Function->getBody())
      return true;
    bool Referenced = false;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
      if (!S || Referenced)
        return;
      // Casting it to void, like `(void)Guard`, keeps it.
      if (const auto *Cast = dyn_cast<ExplicitCastExpr>(S);
          Cast && Cast->getType()->isVoidType())
        if (const auto *Ref =
                dyn_cast<DeclRefExpr>(Cast->getSubExpr()->IgnoreParenImpCasts());
            Ref && Ref->getDecl() == Holder)
          return;
      // Nor what isn't evaluated, `sizeof(Guard)`, or a branch of an `if
      // constexpr` that is discarded.
      if (isa<UnaryExprOrTypeTraitExpr, CXXNoexceptExpr>(S))
        return;
      if (const auto *Typeid = dyn_cast<CXXTypeidExpr>(S);
          Typeid && !Typeid->isPotentiallyEvaluated())
        return;
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
          Ref && Ref->getDecl() == Holder)
        Referenced = true;
      // Nor what only looks at it, given by a reference to const,
      // `fg_Inspect(Guard)` of `fg_Inspect(COnResumeScope const &_Guard)`.
      if (const auto *Call = dyn_cast<CallExpr>(S))
        if (const FunctionDecl *Callee = Call->getDirectCallee()) {
          const unsigned Offset =
              isa<CXXOperatorCallExpr>(Call) && hasImplicitObject(Callee) ? 1
                                                                          : 0;
          for (const Stmt *Child : evaluatedChildren(S)) {
            const auto *Argument = dyn_cast<Expr>(Child);
            const auto *Ref =
                Argument ? dyn_cast<DeclRefExpr>(Argument->IgnoreParenImpCasts())
                         : nullptr;
            bool Inspected = false;
            if (Ref && Ref->getDecl() == Holder)
              for (unsigned iArgument = Offset; iArgument < Call->getNumArgs() &&
                                                iArgument - Offset <
                                                    Callee->getNumParams();
                   ++iArgument)
                if (Call->getArg(iArgument) == Argument) {
                  const QualType Type =
                      Callee->getParamDecl(iArgument - Offset)->getType();
                  Inspected = Type->isLValueReferenceType() &&
                              Type.getNonReferenceType().isConstQualified();
                }
            if (!Inspected)
              Visit(Child);
          }
          return;
        }
      for (const Stmt *Child : evaluatedChildren(S))
        Visit(Child);
    };
    Visit(Function->getBody());
    return Referenced;
  }

  /// The holder of the `fg_OnResume` registration whose callback is being
  /// traversed.
  const VarDecl *CurrentHolder = nullptr;
  /// The body of the callback of CurrentHolder.
  const CompoundStmt *CurrentCallbackBody = nullptr;

  /// Whether \p Assignment is a statement of the body of the callback being
  /// traversed, before anything that can return, also in an `if` among them
  /// that assigns what it does on every path, `if (Mode) pEntry = ...; else
  /// pEntry = ...;`.
  bool isUnconditionalInCallback(const BinaryOperator *Assignment) const {
    if (!CurrentCallbackBody)
      return false;
    for (const Stmt *Statement : CurrentCallbackBody->body()) {
      const auto *E = dyn_cast<Expr>(Statement);
      if (E && E->IgnoreImplicit()->IgnoreParens() == Assignment)
        return true;
      if (canReturn(Statement))
        break;
    }
    return everyPathAssignments(CurrentCallbackBody).contains(Assignment);
  }

  /// Adds to CallbackEarlyUses of \p Holder what its callback \p Lambda uses
  /// of \p Validated, the pointers and references it re-validates, before
  /// it first assigns it, compares it, or gives it to what can assign it:
  /// what it reads through it, `pEntry->m_Value`, not what tests it for null,
  /// `if (!pEntry)`.
  /// Of what it doesn't re-validate, \p Revalidated, only what it reads
  /// through, `*pEntry`, not copies of the pointer, `pOther = pEntry`.
  template <typename t_CVariables, typename t_CRevalidated>
  void addCallbackEarlyUses(const VarDecl *Holder, const LambdaExpr *Lambda,
                            const t_CVariables &Validated,
                            const t_CRevalidated &Revalidated) {
    ASTContext &CallbackContext = Lambda->getCallOperator()->getASTContext();
    const SourceManager &SM = CallbackContext.getSourceManager();
    for (const ValueDecl *Variable : Validated) {
      const QualType Type = Variable->getType();
      if (!Type->isPointerType() && !Type->isReferenceType())
        continue;
      const bool OnlyThrough = Type->isPointerType() &&
                               !llvm::is_contained(Revalidated, Variable);
      // Where it is looked up again, and read.
      llvm::SmallVector<const DeclRefExpr *, 2> Revalidations;
      llvm::SmallVector<const DeclRefExpr *, 2> Uses;
      // A comparison that decides where the callback goes, `if (pEntry !=
      // mp_Items.f_FindEqual(1))` or `return pEntry == ...;`, not one only
      // logged, `fg_Log(pEntry == ...)`.
      auto Decides = [&](const BinaryOperator *Comparison) {
        DynTypedNode Node = DynTypedNode::create(*Comparison);
        for (int Depth = 0; Depth < 16; ++Depth) {
          const auto Parents = instantiatedParents(Node, CallbackContext);
          if (Parents.empty())
            return false;
          const Stmt *Child = Node.get<Stmt>();
          if (Parents[0].get<ReturnStmt>())
            return true;
          if (const auto *If = Parents[0].get<IfStmt>())
            return If->getCond() == Child;
          if (const auto *While = Parents[0].get<WhileStmt>())
            return While->getCond() == Child;
          if (const auto *Conditional = Parents[0].get<ConditionalOperator>())
            return Conditional->getCond() == Child;
          const auto *Logical = Parents[0].get<BinaryOperator>();
          const auto *Not = Parents[0].get<UnaryOperator>();
          if (!Parents[0].get<ImplicitCastExpr>() &&
              !Parents[0].get<ParenExpr>() &&
              !(Logical && Logical->isLogicalOp()) &&
              !(Not && Not->getOpcode() == UO_LNot))
            return false;
          Node = Parents[0];
        }
        return false;
      };
      std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
        if (!S)
          return;
        // Also in a lambda called where it is made, `[&] { fg_Log(*pEntry);
        // }()`, which runs then.
        if (const auto *Inner = dyn_cast<LambdaExpr>(S)) {
          if (runsHere(Inner))
            Visit(Inner->getBody());
          return;
        }
        // Not what never runs, in a branch of an `if constexpr` that is
        // discarded or that a constant condition never takes.
        for (const Stmt *Child : evaluatedChildren(S, CallbackContext))
          Visit(Child);
        const auto *Ref = dyn_cast<DeclRefExpr>(S);
        if (!Ref || resolveReferenceCapture(Ref->getDecl()) != Variable ||
            isInNeverTakenBranch(DynTypedNode::create(*Ref), CallbackContext))
          return;
        bool Revalidates = false;
        bool Reads = true;
        DynTypedNode Node = DynTypedNode::create(*Ref);
        for (int Depth = 0; Depth < 8; ++Depth) {
          const auto Parents = instantiatedParents(Node, CallbackContext);
          if (Parents.empty())
            break;
          const Stmt *Child = Node.get<Stmt>();
          // Also explicitly, `bool(pEntry)`, where the parent map skips the
          // conversion to bool.
          if (const auto *Test = Parents[0].get<ExplicitCastExpr>();
              Test && isPointerToBool(Test)) {
            Reads = false;
            break;
          }
          if (const auto *Cast = Parents[0].get<ImplicitCastExpr>()) {
            if (Cast->getCastKind() == CK_PointerToBoolean) {
              Reads = false;
              break;
            }
            Node = Parents[0];
            continue;
          }
          // Tested, `pEntry && ...` or `if (!pEntry)`, also where the parents
          // skip the conversion to bool.
          if (const auto *Logical = Parents[0].get<BinaryOperator>();
              Logical && Logical->isLogicalOp()) {
            Reads = false;
            break;
          }
          if (const auto *Not = Parents[0].get<UnaryOperator>();
              Not && Not->getOpcode() == UO_LNot) {
            Reads = false;
            break;
          }
          if ((Parents[0].get<IfStmt>() &&
               Parents[0].get<IfStmt>()->getCond() == Child) ||
              (Parents[0].get<WhileStmt>() &&
               Parents[0].get<WhileStmt>()->getCond() == Child) ||
              (Parents[0].get<ConditionalOperator>() &&
               Parents[0].get<ConditionalOperator>()->getCond() == Child)) {
            Reads = false;
            break;
          }
          if (Parents[0].get<ParenExpr>()) {
            Node = Parents[0];
            continue;
          }
          // Bound to a reference, `int *&pAlias = pEntry;`, which reads
          // nothing: what is done through it is.
          if (const auto *Alias = Parents[0].get<VarDecl>();
              Alias && Alias->getType()->isReferenceType()) {
            Reads = false;
            break;
          }
          // Its address, `&Entry`, compared.
          if (const auto *Address = Parents[0].get<UnaryOperator>();
              Address && Address->getOpcode() == UO_AddrOf) {
            Node = Parents[0];
            continue;
          }
          if (const auto *Binary = Parents[0].get<BinaryOperator>()) {
            if (Binary->getOpcode() == BO_Assign && Child == Binary->getLHS())
              Revalidates = true;
            else if (Binary->isEqualityOp()) {
              Reads = false;
              Revalidates =
                  !Binary->getLHS()->isNullPointerConstant(
                      CallbackContext, Expr::NPC_ValueDependentIsNotNull) &&
                  !Binary->getRHS()->isNullPointerConstant(
                      CallbackContext, Expr::NPC_ValueDependentIsNotNull) &&
                  Decides(Binary);
            }
            break;
          }
          if (const auto *Call = Parents[0].get<CallExpr>()) {
            const FunctionDecl *Callee = Call->getDirectCallee();
            for (unsigned iArgument = 0; iArgument < Call->getNumArgs();
                 ++iArgument)
              if (Call->getArg(iArgument) == Child &&
                  (!Callee || (iArgument < Callee->getNumParams() &&
                               Callee->getParamDecl(iArgument)
                                   ->getType()
                                   ->isReferenceType() &&
                               !Callee->getParamDecl(iArgument)
                                    ->getType()
                                    .getNonReferenceType()
                                    .isConstQualified() &&
                               (!Callee->getDefinition() ||
                                !Callee->getDefinition()->getBody() ||
                                callsOverride(Call) ||
                                RefCollector::helperLooksUpAgain(
                                    Callee->getDefinition(), iArgument)))))
                Revalidates = true;
            break;
          }
          break;
        }
        // Through it, `*pEntry`, `pEntry->m_Value` or `pEntry[0]`.
        auto Through = [&] {
          const Expr *Read = Ref;
          for (int Depth = 0; Depth < 8; ++Depth) {
            const auto Parents = instantiatedParents(*Read, CallbackContext);
            const auto *Parent = Parents.empty() ? nullptr : Parents[0].get<Expr>();
            if (!Parent)
              return false;
            if (isa<ImplicitCastExpr, ParenExpr>(Parent)) {
              Read = Parent;
              continue;
            }
            if (const auto *Deref = dyn_cast<UnaryOperator>(Parent))
              return Deref->getOpcode() == UO_Deref;
            // Its base as it is, which the parents can show past the
            // implicit conversion of the pointer.
            if (const auto *Member = dyn_cast<MemberExpr>(Parent))
              return Member->isArrow() &&
                     Member->getBase()->IgnoreParenImpCasts() ==
                         Read->IgnoreParenImpCasts();
            if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(Parent))
              return Subscript->getBase()->IgnoreParenImpCasts() ==
                     Read->IgnoreParenImpCasts();
            return false;
          }
          return false;
        };
        if (Revalidates)
          Revalidations.push_back(Ref);
        else if (Reads && (!OnlyThrough || Through()))
          Uses.push_back(Ref);
      };
      Visit(Lambda->getBody());
      // The place of \p Node in \p Block, also past what wraps it that the
      // parents skip, like the cleanups of `pEntry = mp_Items.f_FindEqual(1)`.
      auto PlaceIn = [](const CompoundStmt *Block,
                        const Stmt *Node) -> std::optional<size_t> {
        size_t iStatement = 0;
        for (const Stmt *Statement : Block->body()) {
          if (Statement == Node ||
              (isa<Expr>(Statement) &&
               cast<Expr>(Statement)->IgnoreImplicit() == Node))
            return iStatement;
          ++iStatement;
        }
        return std::nullopt;
      };
      // The statements of blocks that the look up again runs as a part of
      // wherever the block runs, not in a branch or a loop of it, by the
      // block, and their place in it.
      auto Unconditional = [&](const Stmt *At) {
        llvm::SmallVector<std::pair<const CompoundStmt *, size_t>, 4> Places;
        DynTypedNode Node = DynTypedNode::create(*At);
        for (int Depth = 0; Depth < 64; ++Depth) {
          const auto Parents = instantiatedParents(Node, CallbackContext);
          if (Parents.empty() || Parents[0].get<LambdaExpr>())
            break;
          if (const auto *Block = Parents[0].get<CompoundStmt>()) {
            const std::optional<size_t> Place =
                PlaceIn(Block, Node.get<Stmt>());
            if (!Place)
              break;
            Places.push_back({Block, *Place});
          } else if (const Stmt *Up = Parents[0].get<Stmt>();
                     !Up ||
                     isa<IfStmt, SwitchStmt, SwitchCase, WhileStmt, DoStmt,
                         ForStmt, CXXForRangeStmt, AbstractConditionalOperator,
                         CXXTryStmt, CXXCatchStmt>(Up) ||
                     (isa<BinaryOperator>(Up) &&
                      cast<BinaryOperator>(Up)->isLogicalOp()))
            break;
          Node = Parents[0];
        }
        return Places;
      };
      // Whether every path through \p S looks it up again: one that does
      // where it runs, not in a branch of it, a block with a statement that
      // does, and an `if` whose branches each do.
      std::function<bool(const Stmt *, int)> Always = [&](const Stmt *S,
                                                          int Depth) {
        if (!S || Depth > 16)
          return false;
        if (const auto *If = dyn_cast<IfStmt>(S))
          return If->getElse() && !If->isConsteval() &&
                 Always(If->getThen(), Depth + 1) &&
                 Always(If->getElse(), Depth + 1);
        if (const auto *Block = dyn_cast<CompoundStmt>(S))
          return llvm::any_of(Block->body(), [&](const Stmt *Statement) {
            return Always(Statement, Depth + 1);
          });
        if (isa<SwitchStmt, WhileStmt, DoStmt, ForStmt, CXXForRangeStmt,
                CXXTryStmt>(S))
          return false;
        return llvm::any_of(Revalidations, [&](const DeclRefExpr *Again) {
          DynTypedNode Node = DynTypedNode::create(*Again);
          for (int Level = 0; Level < 64; ++Level) {
            if (Node.get<Stmt>() == S ||
                (isa<Expr>(S) &&
                 cast<Expr>(S)->IgnoreImplicit() == Node.get<Stmt>()))
              return true;
            const auto Parents = instantiatedParents(Node, CallbackContext);
            if (Parents.empty() || Parents[0].get<LambdaExpr>())
              return false;
            const Stmt *Up = Parents[0].get<Stmt>();
            if (!Up ||
                isa<IfStmt, SwitchStmt, WhileStmt, DoStmt, ForStmt,
                    CXXForRangeStmt, AbstractConditionalOperator, CXXTryStmt>(
                    Up) ||
                (isa<BinaryOperator>(Up) &&
                 cast<BinaryOperator>(Up)->isLogicalOp()))
              return Up == S;
            Node = Parents[0];
          }
          return false;
        });
      };
      // Before the look up again on its path: in a statement of a block that
      // comes after one that looks it up on every path through it, or in the
      // same one, after it, not in another branch, `if (_bFirst) pEntry =
      // ...; else fg_Log(*pEntry);`.
      auto Covered = [&](const DeclRefExpr *Use) {
        {
          DynTypedNode Node = DynTypedNode::create(*Use);
          for (int Depth = 0; Depth < 64; ++Depth) {
            const auto Parents = instantiatedParents(Node, CallbackContext);
            if (Parents.empty() || Parents[0].get<LambdaExpr>())
              break;
            if (const auto *Block = Parents[0].get<CompoundStmt>())
              if (const std::optional<size_t> Place =
                      PlaceIn(Block, Node.get<Stmt>())) {
                size_t iStatement = 0;
                for (const Stmt *Statement : Block->body()) {
                  if (iStatement++ >= *Place)
                    break;
                  if (Always(Statement, 0))
                    return true;
                }
              }
            Node = Parents[0];
          }
        }
        return llvm::any_of(Revalidations, [&](const DeclRefExpr *Again) {
          // Not a use in what the assignment that looks it up again
          // evaluates first, `pEntry = mp_Items.f_FindEqual(pEntry->m_Key)`.
          {
            DynTypedNode Node = DynTypedNode::create(*Again);
            for (int Depth = 0; Depth < 8; ++Depth) {
              const auto Parents = instantiatedParents(Node, CallbackContext);
              if (Parents.empty())
                break;
              if (const auto *Assign = Parents[0].get<BinaryOperator>();
                  Assign && Assign->isAssignmentOp()) {
                const SourceRange Range = Assign->getRHS()->getSourceRange();
                if (!SM.isBeforeInTranslationUnit(Use->getBeginLoc(),
                                                  Range.getBegin()) &&
                    !SM.isBeforeInTranslationUnit(Range.getEnd(),
                                                  Use->getBeginLoc()))
                  return false;
                break;
              }
              if (!Parents[0].get<ParenExpr>() &&
                  !Parents[0].get<ImplicitCastExpr>())
                break;
              Node = Parents[0];
            }
          }
          const auto Before = Unconditional(Again);
          DynTypedNode Node = DynTypedNode::create(*Use);
          for (int Depth = 0; Depth < 64; ++Depth) {
            const auto Parents = instantiatedParents(Node, CallbackContext);
            if (Parents.empty() || Parents[0].get<LambdaExpr>())
              return false;
            if (const auto *Block = Parents[0].get<CompoundStmt>())
              if (const std::optional<size_t> Place =
                      PlaceIn(Block, Node.get<Stmt>()))
                for (const auto &[Of, At] : Before)
                  if (Of == Block &&
                      (At < *Place ||
                       (At == *Place &&
                        SM.isBeforeInTranslationUnit(Again->getBeginLoc(),
                                                     Use->getBeginLoc()))))
                    return true;
            Node = Parents[0];
          }
          return false;
        });
      };
      // All of them when it doesn't re-validate it.
      for (const DeclRefExpr *Use : Uses)
        if (!Covered(Use))
          CallbackEarlyUses[Holder].push_back({Variable, Use->getBeginLoc()});
    }
  }

  /// Whether \p S can return or jump, outside of the lambdas in it.
  /// The assignments of the `if` statements among those of \p Body, before
  /// anything that can return, that assign a variable a lookup made there on
  /// every path through them, `if (Mode) pEntry = mp_Items.f_FindEqual(1);
  /// else pEntry = mp_Items.f_FindEqual(2);`, as a statement of each branch,
  /// also of nested ones: not a pointer kept from before on one of them,
  /// `pEntry = pSaved`, which may be gone.
  llvm::SmallPtrSet<const BinaryOperator *, 4>
  everyPathAssignments(const Stmt *Body) const {
    llvm::SmallPtrSet<const BinaryOperator *, 4> Assignments;
    const auto *Block = dyn_cast_or_null<CompoundStmt>(Body);
    if (!Block)
      return Assignments;
    std::function<bool(const Stmt *, const ValueDecl *,
                       llvm::SmallVectorImpl<const BinaryOperator *> &, int)>
        EveryPath = [&](const Stmt *S, const ValueDecl *Variable,
                        llvm::SmallVectorImpl<const BinaryOperator *> &List,
                        int Depth) {
          if (!S || Depth > 16)
            return false;
          if (const auto *Inner = dyn_cast<CompoundStmt>(S)) {
            for (const Stmt *Statement : Inner->body()) {
              if (EveryPath(Statement, Variable, List, Depth + 1))
                return true;
              if (canReturn(Statement) || canBreak(Statement))
                return false;
            }
            return false;
          }
          // A branch that leaves with an error, `else return
          // fg_MakeError(...);`, doesn't go on to what follows, but the other
          // must assign it.
          // Also each case of a `switch` with a default, through what it
          // falls through to until a `break`, or one that leaves with an
          // error.
          if (const auto *Switch = dyn_cast<SwitchStmt>(S)) {
            const auto *Body =
                dyn_cast_or_null<CompoundStmt>(Switch->getBody());
            if (!Body)
              return false;
            auto Unlabeled = [](const Stmt *Statement, bool &Default) {
              while (true)
                if (const auto *Case = dyn_cast<CaseStmt>(Statement))
                  Statement = Case->getSubStmt();
                else if (const auto *Other = dyn_cast<DefaultStmt>(Statement)) {
                  Default = true;
                  Statement = Other->getSubStmt();
                } else
                  return Statement;
            };
            const auto Statements = Body->body();
            bool HasDefault = false;
            bool AnyCase = false;
            for (auto It = Statements.begin(); It != Statements.end(); ++It) {
              if (!isa<SwitchCase>(*It))
                continue;
              AnyCase = true;
              bool Assigns = false;
              bool Leaves = false;
              for (auto Next = It; Next != Statements.end(); ++Next) {
                bool Default = false;
                const Stmt *Statement = Unlabeled(*Next, Default);
                if (Next == It)
                  HasDefault |= Default;
                if (isa<BreakStmt>(Statement))
                  break;
                if (EveryPath(Statement, Variable, List, Depth + 1)) {
                  Assigns = true;
                  break;
                }
                if (leavesWithError(Statement)) {
                  Leaves = true;
                  break;
                }
                // Also one that can leave the case, `if (_bSkip) break;`.
                if (canReturn(Statement) || canBreak(Statement))
                  break;
              }
              if (!Assigns && !Leaves)
                return false;
            }
            return AnyCase && HasDefault;
          }
          if (const auto *If = dyn_cast<IfStmt>(S)) {
            if (!If->getElse() || If->isConsteval())
              return false;
            const bool Then =
                EveryPath(If->getThen(), Variable, List, Depth + 1);
            const bool Else =
                EveryPath(If->getElse(), Variable, List, Depth + 1);
            return (Then && (Else || leavesWithError(If->getElse()))) ||
                   (Else && leavesWithError(If->getThen()));
          }
          const auto *E = dyn_cast<Expr>(S);
          const auto *Assign =
              E ? dyn_cast<BinaryOperator>(E->IgnoreImplicit()->IgnoreParens())
                : nullptr;
          if (!Assign || Assign->getOpcode() != BO_Assign)
            return false;
          const llvm::SmallVector<const DeclRefExpr *, 2> Targets =
              assignedVariables(Assign->getLHS());
          // Not a pointer kept from before, but one the callback looks up
          // itself, `if (auto *pFound = mp_Items.f_FindEqual(_Key); !pFound)
          // ... else pEntry = pFound;`.
          auto FreshLocal = [&](const Expr *Value) {
            const auto *Ref =
                dyn_cast<DeclRefExpr>(Value->IgnoreParenImpCasts());
            const auto *Local =
                Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
            const auto *Owner =
                Local ? dyn_cast<FunctionDecl>(Local->getDeclContext())
                      : nullptr;
            return Owner && Owner->getBody() == Block &&
                   Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
                   !Local->getType()->isReferenceType() && Local->getInit() &&
                   !isChangedVariable(Local) &&
                   isLookupIntoActor(Local->getInit());
          };
          if (Targets.size() != 1 ||
              resolveReferenceCapture(Targets[0]->getDecl()) != Variable ||
              (isa<DeclRefExpr>(Assign->getRHS()->IgnoreParenImpCasts())
                   ? !FreshLocal(Assign->getRHS())
                   : !isLookupIntoActor(Assign->getRHS())))
            return false;
          List.push_back(Assign);
          return true;
        };
    for (const Stmt *Statement : Block->body()) {
      if (isa<IfStmt, SwitchStmt>(Statement)) {
        llvm::SmallVector<const ValueDecl *, 4> Candidates;
        std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
          if (!S || isa<LambdaExpr>(S))
            return;
          if (const auto *Assign = dyn_cast<BinaryOperator>(S);
              Assign && Assign->getOpcode() == BO_Assign)
            for (const DeclRefExpr *Target :
                 assignedVariables(Assign->getLHS()))
              if (const ValueDecl *Variable =
                      resolveReferenceCapture(Target->getDecl());
                  !llvm::is_contained(Candidates, Variable))
                Candidates.push_back(Variable);
          for (const Stmt *Child : childrenOnce(S))
            Find(Child);
        };
        Find(Statement);
        for (const ValueDecl *Candidate : Candidates) {
          llvm::SmallVector<const BinaryOperator *, 4> List;
          if (EveryPath(Statement, Candidate, List, 0))
            Assignments.insert(List.begin(), List.end());
        }
      }
      if (canReturn(Statement))
        break;
    }
    return Assignments;
  }

  /// Whether \p S can leave the `switch` or loop it is in, with a `break`
  /// that isn't in one of its own.
  static bool canBreak(const Stmt *S) {
    if (!S || isa<LambdaExpr, SwitchStmt, ForStmt, WhileStmt, DoStmt,
                  CXXForRangeStmt>(S))
      return false;
    if (isa<BreakStmt>(S))
      return true;
    return llvm::any_of(childrenOnce(S), canBreak);
  }

  static bool canReturn(const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return false;
    if (isa<ReturnStmt, CoreturnStmt, CXXThrowExpr, GotoStmt,
            IndirectGotoStmt>(S))
      return true;
    return llvm::any_of(childrenOnce(S), canReturn);
  }

public:
  /// The variables each registration's callback keeps pointing to the same
  /// data, whose derived references it re-validates too.
  llvm::DenseMap<const VarDecl *, llvm::SmallPtrSet<const ValueDecl *, 4>>
      SameData;

  /// What a callback assigns to a variable it re-validates.
  struct CCallbackAssignment {
    const ValueDecl *Assigned;
    /// Whether it assigns a lookup into the data of the actor.
    bool bLookup;
    /// Whether every run of the callback assigns it: a statement of its body
    /// before anything that can return.
    bool bUnconditional;
  };

  /// What each registration's callback assigns to the variables it
  /// re-validates, in the order of the callback.
  llvm::DenseMap<const VarDecl *, llvm::SmallVector<CCallbackAssignment, 4>>
      CallbackAssignments;

  /// What each registration's callback uses of what it re-validates before
  /// it does, like `pEntry->m_Value` before `pEntry =
  /// mp_Items.f_FindEqual(Key)`: on a later resume, it uses it stale.
  llvm::DenseMap<const VarDecl *,
                 llvm::SmallVector<std::pair<const ValueDecl *, SourceLocation>,
                                   2>>
      CallbackEarlyUses;

private:
  /// The variables the `fg_OnResume` callback being traversed captures by
  /// copy.
  llvm::SmallPtrSet<const ValueDecl *, 4> CallbackCopies;

private:
  /// Collects the variables a callback assigns: on the left of an
  /// assignment, or passed by non-const reference, also to a call that is not
  /// resolved yet, which may assign them; or with bValidating, every variable
  /// it refers to.
  class RefCollector : public OncePerTypeVisitor<RefCollector> {
  public:
    llvm::SmallVector<const ValueDecl *, 8> Decls;
    /// What it assigns, or gives to what can assign it.
    llvm::SmallPtrSet<const ValueDecl *, 8> Assigned;
    bool bValidating = false; // Everything referred to counts.
    /// The statements of the body of the callback, which run on every path.
    llvm::SmallPtrSet<const Stmt *, 8> TopLevel;
    /// What tells lookups into the actor, of a value assigned.
    const BodyCollector *Owner = nullptr;
    bool VisitDeclRefExpr(DeclRefExpr *Ref) {
      if (bValidating)
        Decls.push_back(Ref->getDecl());
      ++Uses[Ref->getDecl()];
      if (NullTests.contains(Ref) || ConditionalTargets.contains(Ref))
        ++TestUses[Ref->getDecl()];
      return true;
    }
    bool VisitBinaryOperator(BinaryOperator *Binary) {
      if (Binary->isAssignmentOp())
        add(Binary->getLHS());
      // Assigned only on some paths, `if (bRefresh) pEntry = ...`, it isn't
      // looked up again on the others.
      if (Binary->getOpcode() == BO_Assign && !TopLevel.contains(Binary))
        if (const auto *Target =
                dyn_cast<DeclRefExpr>(Binary->getLHS()->IgnoreParenImpCasts()))
          ConditionalTargets.insert(Target);
      // `pEntry == nullptr` tests it.
      if (Binary->isEqualityOp()) {
        auto IsNull = [](const Expr *E) {
          E = E->IgnoreParenImpCasts();
          return isa<CXXNullPtrLiteralExpr, GNUNullExpr>(E) ||
                 (isa<IntegerLiteral>(E) &&
                  cast<IntegerLiteral>(E)->getValue().isZero());
        };
        if (IsNull(Binary->getRHS()))
          addNullTest(Binary->getLHS());
        else if (IsNull(Binary->getLHS()))
          addNullTest(Binary->getRHS());
      }
      return true;
    }
    // `!pEntry` and `if (pEntry)` test it, and so does `bool(pEntry)`.
    bool VisitCastExpr(CastExpr *Cast) {
      if (Cast->getCastKind() == CK_PointerToBoolean)
        addNullTest(Cast->getSubExpr());
      return true;
    }
    /// Whether every use of \p Decl only tests whether it is null, which
    /// doesn't tell that what it points to is still there, or assigns it on
    /// some paths only.
    bool onlyNullTested(const ValueDecl *Decl) const {
      const auto It = Uses.find(Decl);
      return It != Uses.end() && It->second == TestUses.lookup(Decl);
    }
    bool VisitCallExpr(CallExpr *Call) {
      if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call)) {
        // Like a built-in one, `iEntry = mp_Items.f_GetIterator(1)`: not a
        // copy of one kept from before, `iEntry = iSaved`, which may be gone,
        // nor one assigned on some paths only, `if (bRefresh) iEntry = ...`.
        if (Operator->getOperator() == OO_Equal &&
            Operator->getNumArgs() == 2) {
          const Expr *Value = Operator->getArg(1);
          if (isa<DeclRefExpr>(skipWrappers(Value)->IgnoreParenImpCasts()) ||
              (Owner && !Owner->isLookupIntoActor(Value)))
            return true;
          add(Operator->getArg(0));
          if (!TopLevel.contains(Operator))
            if (const auto *Target = dyn_cast<DeclRefExpr>(
                    Operator->getArg(0)->IgnoreParenImpCasts()))
              ConditionalTargets.insert(Target);
          return true;
        }
        if (Operator->isAssignmentOp() && Operator->getNumArgs() >= 1)
          add(Operator->getArg(0));
        return true;
      }
      // Also one called through an unchanged local pointer to it, `fNoOp(p)`
      // of `auto fNoOp = &fg_NoOp`.
      const FunctionDecl *Callee = Call->getDirectCallee();
      if (!Callee && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Call))
        Callee = calleeThroughPointer(Call);
      for (unsigned iArgument = 0; iArgument < Call->getNumArgs(); ++iArgument) {
        if (!Callee) {
          add(Call->getArg(iArgument));
          continue;
        }
        if (iArgument >= Callee->getNumParams())
          break;
        const QualType Type = Callee->getParamDecl(iArgument)->getType();
        // Not given to one whose body can be seen and doesn't look it up
        // again, like `void fg_NoOp(int *&_pEntry) {}` or one that only puts
        // back what it is given, `_pEntry = _pSaved`, or does so on some
        // paths only. Not of a virtual one, which can run an override whose
        // body isn't the one seen.
        const FunctionDecl *Definition = Callee->getDefinition();
        if (Definition && Definition->getBody() &&
            iArgument < Definition->getNumParams() && !callsOverride(Call) &&
            !helperLooksUpAgain(Definition, iArgument))
          continue;
        if (Type->isReferenceType() &&
            !Type.getNonReferenceType().isConstQualified()) {
          add(Call->getArg(iArgument));
          // Given to what can look it up again on some paths only, `if
          // (bRefresh) fg_Refresh(pEntry)`.
          if (!isTopLevelCall(Call))
            if (const auto *Target = dyn_cast<DeclRefExpr>(
                    Call->getArg(iArgument)->IgnoreParenImpCasts()))
              ConditionalTargets.insert(Target);
        }
      }
      return true;
    }

  public:
    /// Whether \p Definition, a helper whose body can be seen, assigns its
    /// parameter at \p Index in a statement of its body, on every path, what
    /// isn't made from a pointer it is given, `_pSaved`, also cast,
    /// `static_cast<int *>(_pEntry)`, or offset, `_pEntry + 0`, which is what
    /// the caller had. Not after
    /// what can leave first, `if (_pEntry) return;`. Also by giving it to
    /// another that does, the same way, or to what can assign it and whose
    /// body can't be seen, and not to one that puts back what it is given,
    /// `fg_Restore(_pEntry, _pSaved)`, \p Depth deep.
    static bool helperLooksUpAgain(const FunctionDecl *Definition,
                                   unsigned Index, int Depth = 0) {
      const ParmVarDecl *Parameter = Definition->getParamDecl(Index);
      const auto *Body = dyn_cast<CompoundStmt>(Definition->getBody());
      if (!Body)
        return false;
      const llvm::SmallPtrSet<const Stmt *, 8> Always =
          statementsBeforeLeaving(Body);
      // The argument of \p Call that is the parameter given by a reference
      // that can change it, `fg_Refresh(_pEntry)`, if one is.
      auto GivenChangeable =
          [&](const CallExpr *Call) -> std::optional<unsigned> {
        const FunctionDecl *Callee = Call->getDirectCallee();
        for (unsigned iArgument = 0; iArgument < Call->getNumArgs();
             ++iArgument) {
          const auto *Ref = dyn_cast<DeclRefExpr>(
              Call->getArg(iArgument)->IgnoreParenImpCasts());
          if (!Ref || Ref->getDecl() != Parameter)
            continue;
          if (!Callee)
            return iArgument;
          if (iArgument >= Callee->getNumParams())
            continue;
          const QualType Type = Callee->getParamDecl(iArgument)->getType();
          if (Type->isReferenceType() &&
              !Type.getNonReferenceType().isConstQualified())
            return iArgument;
        }
        return std::nullopt;
      };
      // What can change the parameter in a statement, an assignment, `++`,
      // taking its address, which can be written through, or giving it to
      // what can change it.
      std::function<bool(const Stmt *)> Changes = [&](const Stmt *S) {
        if (!S || isa<LambdaExpr>(S))
          return false;
        if (const auto *Call = dyn_cast<CallExpr>(S);
            Call && GivenChangeable(Call))
          return true;
        const Expr *Changed = nullptr;
        if (const auto *Binary = dyn_cast<BinaryOperator>(S);
            Binary && Binary->isAssignmentOp())
          Changed = Binary->getLHS();
        else if (const auto *Unary = dyn_cast<UnaryOperator>(S);
                 Unary && (Unary->isIncrementDecrementOp() ||
                           Unary->getOpcode() == UO_AddrOf))
          Changed = Unary->getSubExpr();
        if (const auto *Ref = Changed ? dyn_cast<DeclRefExpr>(
                                            Changed->IgnoreParenImpCasts())
                                      : nullptr;
            Ref && Ref->getDecl() == Parameter)
          return true;
        return llvm::any_of(childrenOnce(S), Changes);
      };
      // Whether \p Assign gives it what isn't made from a pointer it is given.
      auto IsFresh = [&](const BinaryOperator *Assign) {
        // Not one made from a pointer it is given, `_pEntry + 0`, also the
        // one it assigns, which is what the caller had, but a lookup with
        // other parameters, `mp_Items.f_FindEqual(_Key)`, is.
        // Also through a local made from one, `int *pSaved = _pEntry;
        // _pEntry = pSaved;`, and one that is changed may be.
        bool FromPointer = false;
        llvm::SmallPtrSet<const VarDecl *, 4> FollowedLocals;
        llvm::SmallPtrSet<const FunctionDecl *, 4> FollowedCalls;
        std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
          if (!S || FromPointer || isa<LambdaExpr>(S))
            return;
          // Also the address of what a reference parameter refers to,
          // `&_Value`, which is what the caller gave, like `*pEntry`.
          if (const auto *Address = dyn_cast<UnaryOperator>(S);
              Address && Address->getOpcode() == UO_AddrOf)
            if (const auto *Ref = dyn_cast<DeclRefExpr>(
                    Address->getSubExpr()->IgnoreParenImpCasts()))
              if (const auto *Given = dyn_cast<ParmVarDecl>(Ref->getDecl());
                  Given && Given->getType()->isReferenceType()) {
                FromPointer = true;
                return;
              }
          if (const auto *Ref = dyn_cast<DeclRefExpr>(S)) {
            // Not another that a statement before looks up again, `o_pValue =
            // &mp_Items[_Key]; o_pMember = &o_pValue->m_Value;`.
            auto RefreshedBefore = [&](const ParmVarDecl *Other) {
              if (Other == Parameter || Depth >= 4 ||
                  !helperLooksUpAgain(
                      Definition, Other->getFunctionScopeIndex(), Depth + 1))
                return false;
              const SourceManager &SM =
                  Definition->getASTContext().getSourceManager();
              return llvm::any_of(Body->body(), [&](const Stmt *Statement) {
                const auto *Earlier = dyn_cast<BinaryOperator>(Statement);
                const auto *Target =
                    Earlier && Earlier->getOpcode() == BO_Assign
                        ? dyn_cast<DeclRefExpr>(
                              Earlier->getLHS()->IgnoreParenImpCasts())
                        : nullptr;
                return Target && Target->getDecl() == Other &&
                       Always.contains(Statement) &&
                       SM.isBeforeInTranslationUnit(Earlier->getEndLoc(),
                                                    Assign->getBeginLoc());
              });
            };
            if (const auto *Given = dyn_cast<ParmVarDecl>(Ref->getDecl());
                Given &&
                Given->getType().getNonReferenceType()->isPointerType()) {
              if (!RefreshedBefore(Given))
                FromPointer = true;
            }
            // Nor one saved in a global or a static, `_pEntry = g_pSaved`.
            else if (const auto *Saved = dyn_cast<VarDecl>(Ref->getDecl());
                     Saved && Saved->hasGlobalStorage() &&
                     Saved->getType().getNonReferenceType()->isPointerType())
              FromPointer = true;
            else if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
                     Local && Local->hasLocalStorage() &&
                     !isa<ParmVarDecl>(Local) &&
                     (Local->getType()->isPointerType() ||
                      Local->getType()->isReferenceType()) &&
                     FollowedLocals.insert(Local).second) {
              if (isChangedVariable(Local))
                FromPointer = true;
              else
                Visit(Local->getInit());
            }
          }
          // Nor what a free function gives back of those, `fg_LoadSaved()`
          // of `return g_pSaved;`, each once, or anything when its body
          // can't be seen.
          if (const auto *Call = dyn_cast<CallExpr>(S);
              Call && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Call) &&
              Call->getType()->isPointerType()) {
            const FunctionDecl *Called = Call->getDirectCallee();
            const FunctionDecl *CalledDefinition =
                Called ? Called->getDefinition() : nullptr;
            if (!Called || (!CalledDefinition && !Called->getBuiltinID()))
              FromPointer = true;
            else if (CalledDefinition && FollowedCalls.insert(Called).second)
              for (const Expr *Returned : returnedValues(CalledDefinition))
                Visit(Returned);
          }
          for (const Stmt *Child : childrenOnce(S))
            Visit(Child);
        };
        Visit(Assign->getRHS());
        return !FromPointer && !Changes(Assign->getRHS());
      };
      // Whether \p Assign assigns the parameter, also through a local
      // reference bound to it, `int *&pAlias = _pEntry; pAlias = ...`.
      auto AssignsParameter = [&](const BinaryOperator *Assign) {
        const auto *Target =
            Assign && Assign->getOpcode() == BO_Assign
                ? dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts())
                : nullptr;
        const ValueDecl *Named = Target ? Target->getDecl() : nullptr;
        for (int Level = 0; Level < 16 && Named && Named != Parameter;
             ++Level) {
          const auto *Alias = dyn_cast<VarDecl>(Named);
          const auto *Bound =
              Alias && Alias->hasLocalStorage() && !isa<ParmVarDecl>(Alias) &&
                      Alias->getType()->isReferenceType() && Alias->getInit()
                  ? dyn_cast<DeclRefExpr>(
                        Alias->getInit()->IgnoreParenImpCasts())
                  : nullptr;
          Named = Bound ? Bound->getDecl() : nullptr;
        }
        return Target && Named == Parameter;
      };
      // Not when a lambda in it can change it later, like a scope exit that
      // puts back what the caller had, `g_OnScopeExit / [&] { _pEntry =
      // pSaved; }`.
      {
        bool LambdaChanges = false;
        std::function<void(const Stmt *)> FindLambdas = [&](const Stmt *S) {
          if (!S || LambdaChanges)
            return;
          if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
            LambdaChanges = Changes(Lambda->getBody());
            return;
          }
          for (const Stmt *Child : childrenOnce(S))
            FindLambdas(Child);
        };
        FindLambdas(Body);
        if (LambdaChanges)
          return false;
      }
      // An `if` that assigns it a fresh one on every path through it, `if
      // (_bFirst) _pEntry = mp_Items.f_FindEqual(1); else _pEntry = ...;`.
      std::function<bool(const Stmt *, int)> EveryPathFresh = [&](const Stmt *S,
                                                                  int Level) {
        if (!S || Level > 16)
          return false;
        if (const auto *Block = dyn_cast<CompoundStmt>(S)) {
          for (const Stmt *Statement : Block->body()) {
            if (EveryPathFresh(Statement, Level + 1))
              return true;
            if (canReturn(Statement))
              return false;
          }
          return false;
        }
        if (const auto *If = dyn_cast<IfStmt>(S))
          return If->getElse() && !If->isConsteval() &&
                 EveryPathFresh(If->getThen(), Level + 1) &&
                 EveryPathFresh(If->getElse(), Level + 1);
        const auto *E = dyn_cast<Expr>(S);
        const auto *Assign =
            E ? dyn_cast<BinaryOperator>(E->IgnoreImplicit()->IgnoreParens())
              : nullptr;
        return Assign && AssignsParameter(Assign) && IsFresh(Assign);
      };
      // The last of what it is assigned in the statements of its body tells,
      // `_pEntry = mp_Items.f_FindEqual(1); _pEntry = _pSaved;` gives back
      // what the caller had.
      bool LooksUp = false;
      for (const Stmt *Statement : Body->body()) {
        // After what can leave, only if nothing changes it again.
        if (!Always.contains(Statement)) {
          if (Changes(Statement))
            return false;
          continue;
        }
        if (isa<IfStmt>(Statement) && EveryPathFresh(Statement, 0)) {
          LooksUp = true;
          continue;
        }
        // Given to another, `fg_Refresh(_pEntry)`, what it does with it.
        if (const auto *Call = dyn_cast<CallExpr>(
                isa<Expr>(Statement)
                    ? cast<Expr>(Statement)->IgnoreImplicit()->IgnoreParens()
                    : Statement)) {
          if (const std::optional<unsigned> iArgument = GivenChangeable(Call)) {
            const FunctionDecl *Callee = Call->getDirectCallee();
            const FunctionDecl *CalleeDefinition =
                Callee ? Callee->getDefinition() : nullptr;
            if (!CalleeDefinition || !CalleeDefinition->getBody() ||
                callsOverride(Call))
              LooksUp = true;
            else
              LooksUp =
                  Depth < 8 && *iArgument < CalleeDefinition->getNumParams() &&
                  helperLooksUpAgain(CalleeDefinition, *iArgument, Depth + 1);
            continue;
          }
        }
        const auto *Assign = dyn_cast<BinaryOperator>(
            isa<Expr>(Statement) ? cast<Expr>(Statement)->IgnoreImplicit()
                                 : Statement);
        const auto *Target =
            Assign && Assign->getOpcode() == BO_Assign
                ? dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts())
                : nullptr;
        // Also through a local reference bound to it, `int *&pAlias =
        // _pEntry; pAlias = ...`.
        const ValueDecl *Named = Target ? Target->getDecl() : nullptr;
        for (int Depth = 0; Depth < 16 && Named && Named != Parameter;
             ++Depth) {
          const auto *Alias = dyn_cast<VarDecl>(Named);
          const auto *Bound =
              Alias && Alias->hasLocalStorage() && !isa<ParmVarDecl>(Alias) &&
                      Alias->getType()->isReferenceType() && Alias->getInit()
                  ? dyn_cast<DeclRefExpr>(
                        Alias->getInit()->IgnoreParenImpCasts())
                  : nullptr;
          Named = Bound ? Bound->getDecl() : nullptr;
        }
        if (!Target || Named != Parameter) {
          if (Changes(Statement))
            LooksUp = false;
          continue;
        }
        LooksUp = IsFresh(Assign);
      }
      return LooksUp;
    }

  private:
    /// Whether \p Call is a statement of the body, or all of one.
    bool isTopLevelCall(const CallExpr *Call) const {
      return llvm::any_of(TopLevel, [&](const Stmt *Statement) {
        const auto *Value = dyn_cast<Expr>(Statement);
        return Value && Value->IgnoreImplicit()->IgnoreParens() == Call;
      });
    }
    llvm::SmallPtrSet<const DeclRefExpr *, 4> NullTests;
    llvm::SmallPtrSet<const DeclRefExpr *, 4> ConditionalTargets;
    llvm::DenseMap<const ValueDecl *, unsigned> Uses;
    llvm::DenseMap<const ValueDecl *, unsigned> TestUses;
    void addNullTest(const Expr *E) {
      if (const auto *Ref = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
          Ref && Ref->getType()->isPointerType())
        NullTests.insert(Ref);
    }
    // Assigning a reference writes through it, which doesn't look the data up
    // again; a reference to a pointer rebinds the pointer.
    void add(const Expr *E) {
      if (const auto *Ref = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
          Ref && !(Ref->getDecl()->getType()->isReferenceType() &&
                   resolveReferenceCapture(Ref->getDecl()) == Ref->getDecl())) {
        Decls.push_back(Ref->getDecl());
        Assigned.insert(Ref->getDecl());
      }
    }
  };

  /// Whether \p Init looks up data of the actor, like
  /// `mp_Items.f_FindEqual(_Key)` or `&mp_Items[_Key]`, also through a local
  /// alias of the container or of an earlier lookup. Dereferencing an owning
  /// pointer, like `*mp_pInternal`, gives data that lives as long as the
  /// actor.
public:
  /// \p Depth counts the parts of expressions it goes into, which are as
  /// many as the code has, and the variables it follows, which can go on.
  /// Each expression is decided once in a call, also one that several ways
  /// lead to, like the argument a helper returns as it is, `fg_Const(...)`,
  /// whose nesting would otherwise take exponential time.
  bool isLookupIntoActor(const Expr *Init, int Depth = 0) const {
    const bool Outermost = LookupCallDepth++ == 0;
    const llvm::scope_exit Leave([&] { --LookupCallDepth; });
    if (Outermost)
      LookupDecided.clear();
    if (const auto It = LookupDecided.find(Init); It != LookupDecided.end())
      return It->second;
    // While it is decided, a way back to it tells nothing.
    LookupDecided[Init] = false;
    const bool Lookup = decideLookupIntoActor(Init, Depth);
    LookupDecided[Init] = Lookup;
    return Lookup;
  }

private:
  mutable llvm::DenseMap<const Expr *, bool> LookupDecided;
  mutable int LookupCallDepth = 0;

  bool decideLookupIntoActor(const Expr *Init, int Depth) const {
    if (Depth > 128)
      return false;
    // A temporary that a reference is bound to, like the copy of a bit-field,
    // `unsigned const &Bits = mp_Items[1].m_Bits`, or a value a call gives,
    // is its own, unless it can hold a lookup itself, like a handle.
    for (const Expr *Bound = Init;;) {
      if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(Bound)) {
        if (!canHoldLookup(Materialize->getType()))
          return false;
        break;
      }
      if (const auto *Full = dyn_cast<FullExpr>(Bound))
        Bound = Full->getSubExpr();
      else if (const auto *Paren = dyn_cast<ParenExpr>(Bound))
        Bound = Paren->getSubExpr();
      else if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Bound))
        Bound = Cast->getSubExpr();
      else
        break;
    }
    const Expr *E = Init->IgnoreParenImpCasts();
    if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(E))
      return isLookupIntoActor(Cleanups->getSubExpr(), Depth);
    // The condition that a GNU `?:` gives, `pEntry ?: pOther`.
    if (const auto *Opaque = dyn_cast<OpaqueValueExpr>(E))
      return Opaque->getSourceExpr() &&
             isLookupIntoActor(Opaque->getSourceExpr(), Depth + 1);
    // What a coroutine of the actor's own object that can be seen gives back,
    // awaited, `co_await fp_Find(_Key)` of `co_return
    // mp_Items.f_FindEqual(_Key);`.
    // Also of a coroutine lambda, `co_await fFind()` of `[this]() ->
    // TCFuture<int *> { co_return mp_Items.f_FindEqual(1); }`. Not of a
    // branch that a constant condition leaves out, `if (t_bCached)`.
    if (const auto *Await = dyn_cast<CoawaitExpr>(E)) {
      const Expr *Operand =
          skipWrappers(Await->getOperand())->IgnoreParenImpCasts();
      // Also moved, `co_await fg_Move(Pending)`, and kept in a local before,
      // `auto Pending = fp_Find(_Key);`, which is the call it holds.
      const LambdaExpr *CalledSafe = nullptr;
      for (int Step = 0; Step < 8; ++Step) {
        // And what `fg_CallSafe` calls, `co_await fg_CallSafe([this]() ->
        // TCFuture<int *> { ... })`, which gives what that does, also a local
        // one, moved, `fg_CallSafe(fg_Move(fFind))`.
        if (const auto *Safe = dyn_cast<CallExpr>(Operand);
            Safe && calleeName(Safe) == "fg_CallSafe" &&
            Safe->getNumArgs() >= 1) {
          const Expr *Called =
              skipWrappers(Safe->getArg(0))->IgnoreParenImpCasts();
          if (const auto *Move = dyn_cast<CallExpr>(Called);
              Move && Move->getNumArgs() == 1 &&
              isMoveOrForwardFunction(calleeName(Move)))
            Called = Move->getArg(0);
          if (const LambdaExpr *Lambda = calledLambda(Called)) {
            CalledSafe = Lambda;
            break;
          }
        }
        if (const auto *Move = dyn_cast<CallExpr>(Operand);
            Move && Move->getNumArgs() == 1 &&
            isMoveOrForwardFunction(calleeName(Move))) {
          Operand = skipWrappers(Move->getArg(0))->IgnoreParenImpCasts();
          continue;
        }
        // Also moved by its member, `Find.f_Move()` of a future, which gives
        // it moved, like `fg_Move(Find)`.
        if (const auto *Move = dyn_cast<CXXMemberCallExpr>(Operand);
            Move && Move->getNumArgs() == 0 && Move->getMethodDecl() &&
            Move->getMethodDecl()->getIdentifier() &&
            Move->getMethodDecl()->getName() == "f_Move" &&
            Move->getImplicitObjectArgument() &&
            Move->getImplicitObjectArgument()
                ->getType()
                ->getAsCXXRecordDecl() &&
            Move->getImplicitObjectArgument()
                ->getType()
                ->getAsCXXRecordDecl()
                ->getIdentifier() &&
            Move->getImplicitObjectArgument()
                ->getType()
                ->getAsCXXRecordDecl()
                ->getName()
                .starts_with("TCFuture")) {
          Operand = skipWrappers(Move->getImplicitObjectArgument())
                        ->IgnoreParenImpCasts();
          continue;
        }
        // Also given a context for its error, `fp_Find(1) % "Finding"`,
        // which gives the same result.
        if (const auto *WithError = dyn_cast<CXXOperatorCallExpr>(Operand);
            WithError && WithError->getOperator() == OO_Percent &&
            WithError->getNumArgs() == 2 &&
            WithError->getType()->getAsCXXRecordDecl() &&
            WithError->getType()->getAsCXXRecordDecl()->getIdentifier() &&
            WithError->getType()->getAsCXXRecordDecl()->getName().starts_with(
                "TCFuture")) {
          Operand = skipWrappers(WithError->getArg(0))->IgnoreParenImpCasts();
          continue;
        }
        const auto *Ref = dyn_cast<DeclRefExpr>(Operand);
        const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
        if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
            Local->getType()->isReferenceType())
          break;
        const Expr *Value = assignedValueAt(Local, Ref);
        if (!Value)
          break;
        Operand = skipWrappers(Value)->IgnoreParenImpCasts();
      }
      const FunctionDecl *Definition =
          CalledSafe ? CalledSafe->getCallOperator() : nullptr;
      if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Operand);
          !Definition && Call && Call->getImplicitObjectArgument() &&
          refersToOwnObject(Call->getImplicitObjectArgument()) &&
          Call->getMethodDecl() && !callsOverride(Call))
        Definition = Call->getMethodDecl()->getDefinition();
      else if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(Operand);
               Invoke && Invoke->getOperator() == OO_Call &&
               Invoke->getNumArgs() >= 1)
        if (const LambdaExpr *Lambda = calledLambda(Invoke->getArg(0)))
          Definition = Lambda->getCallOperator();
      // Also a free or static one, `co_await fg_Find(this, 1)`, which looks
      // it up in what it is given, like a free function does.
      const CallExpr *Free = nullptr;
      if (const auto *Call = dyn_cast<CallExpr>(Operand);
          !Definition && Call &&
          !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Call) &&
          Call->getDirectCallee() && calleeName(Call) != "fg_CallSafe")
        if (const auto *Method =
                dyn_cast<CXXMethodDecl>(Call->getDirectCallee());
            !Method || Method->isStatic()) {
          Definition = Call->getDirectCallee()->getDefinition();
          Free = Call;
        }
      if (Definition &&
          isa_and_nonnull<CoroutineBodyStmt>(Definition->getBody())) {
        const ASTContext &DefinitionContext = Definition->getASTContext();
        bool Lookup = false;
        llvm::SmallVector<const Expr *, 4> Values;
        std::function<void(const Stmt *)> Returns = [&](const Stmt *S) {
          if (!S || Lookup || isa<LambdaExpr>(S))
            return;
          if (const auto *Return = dyn_cast<CoreturnStmt>(S)) {
            if (Return->getOperand())
              Values.push_back(Return->getOperand());
            Lookup = Return->getOperand() &&
                     isLookupIntoActor(Return->getOperand(), Depth + 1);
            return;
          }
          if (const auto *If = dyn_cast<IfStmt>(S);
              If && !If->isConstexpr() && !If->getConditionVariable() &&
              !If->getInit() && !If->getCond()->isValueDependent()) {
            bool Taken = false;
            if (If->getCond()->EvaluateAsBooleanCondition(Taken,
                                                          DefinitionContext)) {
              Returns(Taken ? If->getThen() : If->getElse());
              return;
            }
          }
          for (const Stmt *Child : keptChildren(S, DefinitionContext))
            Returns(Child);
        };
        Returns(cast<CoroutineBodyStmt>(Definition->getBody())->getBody());
        // What it is given by reference, a lookup or the actor's data, or by
        // a pointer to the actor, `this`, unless it gives back only members
        // of that, which live with it.
        // Not when it gives only what is static, `co_return "Ready";`.
        if (!Lookup && Free && !Values.empty() &&
            !onlyStatic(Values, E->isGLValue()) &&
            (E->getType()->isPointerType() || isIteratorType(E->getType()) ||
             E->isGLValue()))
          for (unsigned iArgument = 0; iArgument < Free->getNumArgs() &&
                                       iArgument < Definition->getNumParams();
               ++iArgument) {
            const ParmVarDecl *Parameter = Definition->getParamDecl(iArgument);
            const Expr *Argument = Free->getArg(iArgument);
            if (givesOnlyMembers(Values, Parameter) &&
                !isLookupIntoActor(Argument, Depth + 1))
              continue;
            if ((Parameter->getType()->isReferenceType() &&
                 !Argument->getType()->isPointerType() && isRooted(Argument)) ||
                (Parameter->getType()->isPointerType() &&
                 (isa<CXXThisExpr>(Argument->IgnoreParenImpCasts()) ||
                  isRooted(Argument))))
              return true;
          }
        return Lookup;
      }
    }
    // The value of a statement expression, `({ mp_Items.f_FindEqual(1); })`.
    if (const auto *Statement = dyn_cast<StmtExpr>(E)) {
      const auto *Value =
          Statement->getSubStmt()->body_empty()
              ? nullptr
              : dyn_cast<Expr>(Statement->getSubStmt()->body_back());
      return Value && isLookupIntoActor(Value, Depth + 1);
    }
    // What a lambda called in place gives back, `[this] { return
    // mp_Items.f_FindEqual(1); }()`, of any of its returns, also of what it
    // is given, `[](int &_Value) { return &_Value; }(mp_Items[0])`.
    // Also of one held in a local, `fFind(mp_Items)` of `auto fFind =
    // [](TCMap<int, CEntry> &_Items) { return _Items.f_FindEqual(1); }`, what
    // it looks up in what it is given by reference, like a free function.
    const CXXMethodDecl *Operator = calledInPlaceLambda(E);
    if (const auto *Invoke = dyn_cast<CXXOperatorCallExpr>(E);
        !Operator && Invoke && Invoke->getOperator() == OO_Call &&
        Invoke->getNumArgs() >= 1 && calledLambda(Invoke->getArg(0)))
      Operator = dyn_cast_or_null<CXXMethodDecl>(Invoke->getDirectCallee());
    if (Operator) {
      const auto *Call = cast<CallExpr>(E);
      for (const bool Addressed : {false, true})
        for (const Expr *Argument : returnedArguments(Call, Addressed))
          if (isLookupIntoActor(Argument, Depth + 1))
            return true;
      const llvm::SmallVector<const Expr *, 4> Values =
          returnedValues(Operator);
      if (llvm::any_of(Values, [&](const Expr *Value) {
            return isLookupIntoActor(Value, Depth + 1);
          }))
        return true;
      const FunctionDecl *Definition = Operator->getDefinition();
      const unsigned Offset = isa<CXXOperatorCallExpr>(Call) ? 1 : 0;
      if (Definition && !Values.empty() && !returnsOnlyStatic(Definition) &&
          (Call->getType()->isPointerType() ||
           isIteratorType(Call->getType()) || Call->isGLValue()))
        for (unsigned iArgument = Offset;
             iArgument < Call->getNumArgs() &&
             iArgument - Offset < Definition->getNumParams();
             ++iArgument) {
          const ParmVarDecl *Parameter =
              Definition->getParamDecl(iArgument - Offset);
          const Expr *Argument = Call->getArg(iArgument);
          if (givesOnlyMembers(Values, Parameter) &&
              !isLookupIntoActor(Argument, Depth + 1))
            continue;
          // Also by a pointer to the actor, `fFind(this)`, like a free
          // function.
          if ((Parameter->getType()->isReferenceType() &&
               !Argument->getType()->isPointerType() && isRooted(Argument)) ||
              (Parameter->getType()->isPointerType() &&
               (isa<CXXThisExpr>(Argument->IgnoreParenImpCasts()) ||
                isRooted(Argument))))
            return true;
        }
      return false;
    }
    // Moved, `fg_Move(mp_Items.f_FindEqual(1))`, it is what is moved.
    if (const auto *Move = dyn_cast<CallExpr>(E);
        Move && Move->getNumArgs() == 1 &&
        (isMoveFunction(calleeName(Move)) ||
         isForwardFunction(calleeName(Move))))
      return isLookupIntoActor(Move->getArg(0), Depth + 1);
    // The address of a local that isn't a reference, `&pEntry`, also taken
    // with `std::addressof(pEntry)`, is of the local itself, not of what it
    // points to, also through local references bound to it, `&pAlias` of
    // `auto *&pAlias = pEntry`.
    int Aliases = 0;
    const Expr *Addressed = nullptr;
    if (const auto *Unary = dyn_cast<UnaryOperator>(E);
        Unary && Unary->getOpcode() == UO_AddrOf)
      Addressed = Unary->getSubExpr();
    else if (const auto *Call = dyn_cast<CallExpr>(E); isAddressOfCall(Call))
      Addressed = Call->getArg(0);
    if (Addressed)
      if (const auto *Ref =
              dyn_cast<DeclRefExpr>(Addressed->IgnoreParenImpCasts()))
        for (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
             Local && Local->hasLocalStorage();) {
          static constexpr int MaxAliases = 1024;
          if (!Local->getType()->isReferenceType())
            return false;
          const auto *Bound = !isa<ParmVarDecl>(Local) && Local->getInit()
                                  ? dyn_cast<DeclRefExpr>(
                                        Local->getInit()->IgnoreParenImpCasts())
                                  : nullptr;
          if (!Bound || ++Aliases > MaxAliases)
            break;
          Local = dyn_cast<VarDecl>(Bound->getDecl());
        }
    // Also a pointer given a unary plus, `+mp_Items.f_FindEqual(1)`.
    if (const auto *Unary = dyn_cast<UnaryOperator>(E);
        Unary && (Unary->getOpcode() == UO_AddrOf ||
                  Unary->getOpcode() == UO_Deref ||
                  (Unary->getOpcode() == UO_Plus &&
                   Unary->getType()->isPointerType())))
      return isLookupIntoActor(Unary->getSubExpr(), Depth);
    // Not a static member, `m_Items[0].c_Value`, which isn't in the entry.
    if (const auto *Member = dyn_cast<MemberExpr>(E))
      return !isa<VarDecl, EnumConstantDecl>(Member->getMemberDecl()) &&
             isLookupIntoActor(Member->getBase(), Depth);
    // Also through a pointer to the member, `mp_Items[1].*pmValue`.
    if (const auto *Pointer = dyn_cast<BinaryOperator>(E);
        Pointer && Pointer->isPtrMemOp() &&
        !Pointer->getType()->isSpecificPlaceholderType(BuiltinType::BoundMember))
      return isLookupIntoActor(Pointer->getLHS(), Depth);
    // A handle made from a lookup, like `CHandle Handle{*pEntry}`.
    if (const auto *List = dyn_cast<InitListExpr>(E);
        List && isReferenceHandle(List->getType()))
      return llvm::any_of(List->inits(), [&](const Expr *Init) {
        return isLookupIntoActor(Init, Depth + 1);
      });
    // Also with parentheses, `CHandle Handle(*pEntry)` of an aggregate.
    if (const auto *List = dyn_cast<CXXParenListInitExpr>(E);
        List && isReferenceHandle(List->getType()))
      return llvm::any_of(List->getInitExprs(), [&](const Expr *Init) {
        return isLookupIntoActor(Init, Depth + 1);
      });
    // A copy or a move of an iterator refers to the same entry, `auto iCopy =
    // iEntry`, which holds pointers rather than references.
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(E);
        Construct && Construct->getNumArgs() == 1 &&
        Construct->getConstructor()->isCopyOrMoveConstructor() &&
        isIteratorType(Construct->getType()))
      return isLookupIntoActor(Construct->getArg(0), Depth);
    // A copy of a handle refers to the same entry, and a handle made from a
    // lookup, like `CHandle Handle(*pEntry, Tag)`, to the entry.
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(E);
        Construct && Construct->getNumArgs() >= 1 &&
        isReferenceHandle(Construct->getType())) {
      if (Construct->getNumArgs() == 1)
        return isLookupIntoActor(Construct->getArg(0), Depth);
      const CXXConstructorDecl *Constructor = Construct->getConstructor();
      for (unsigned iArgument = 0; iArgument < Construct->getNumArgs() &&
                                   iArgument < Constructor->getNumParams();
           ++iArgument) {
        const QualType Type = Constructor->getParamDecl(iArgument)->getType();
        if ((Type->isReferenceType() || Type->isPointerType()) &&
            isLookupIntoActor(Construct->getArg(iArgument), Depth + 1))
          return true;
      }
      return false;
    }
    if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(E))
      return isLookupIntoActor(Bind->getSubExpr(), Depth);
    // Pointer arithmetic, like `m_Buffer.f_GetArray() + Index`, and pointer
    // casts stay in the data.
    if (const auto *Binary = dyn_cast<BinaryOperator>(E);
        Binary && Binary->isAdditiveOp() && Binary->getType()->isPointerType())
      return isLookupIntoActor(Binary->getLHS()->getType()->isPointerType()
                                   ? Binary->getLHS()
                                   : Binary->getRHS(),
                               Depth + 1);
    // Also to a reference, `static_cast<int &>(mp_Items[1])`, whose type is
    // the type it refers to.
    if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
        Cast && (Cast->getType()->isPointerType() ||
                 Cast->getTypeAsWritten()->isReferenceType()))
      return isLookupIntoActor(Cast->getSubExpr(), Depth + 1);
    // What a comma gives, `(f_Prepare(), mp_Items.f_FindEqual(Key))`, and
    // what an assignment of a pointer assigns, `(pEntry =
    // mp_Items.f_FindEqual(Key))`; of anything else, an assignment, also a
    // compound one, or a prefix increment, refers to what it changes,
    // `(mp_Items[0] = 1)` or `++mp_Items[0]`, also an overloaded one.
    if (const auto *Binary = dyn_cast<BinaryOperator>(E);
        Binary && (Binary->getOpcode() == BO_Comma ||
                   (Binary->getOpcode() == BO_Assign &&
                    Binary->getType()->isPointerType())))
      return isLookupIntoActor(Binary->getRHS(), Depth + 1);
    if (const auto *Binary = dyn_cast<BinaryOperator>(E);
        Binary && Binary->isAssignmentOp())
      return isLookupIntoActor(Binary->getLHS(), Depth + 1);
    if (const auto *Step = dyn_cast<UnaryOperator>(E);
        Step && Step->isPrefix() && Step->isIncrementDecrementOp() &&
        !Step->getType()->isPointerType())
      return isLookupIntoActor(Step->getSubExpr(), Depth + 1);
    // A pointer stepped, `p++` or `++p`, gives one into the same data.
    if (const auto *Step = dyn_cast<UnaryOperator>(E);
        Step && Step->isIncrementDecrementOp() &&
        Step->getType()->isPointerType())
      return isLookupIntoActor(Step->getSubExpr(), Depth + 1);
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(E);
        Operator && Operator->isGLValue() && Operator->getNumArgs() >= 1 &&
        (Operator->isAssignmentOp() ||
         ((Operator->getOperator() == OO_PlusPlus ||
           Operator->getOperator() == OO_MinusMinus) &&
          Operator->getNumArgs() == 1)))
      return isLookupIntoActor(Operator->getArg(0), Depth + 1);
    // Either value of a conditional, like `bFirst ? mp_Items(1) : mp_Items(2)`,
    // or the one a constant condition chooses.
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E)) {
      if (bool Chosen = false;
          Context && !Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(Chosen, *Context))
        return isLookupIntoActor(Chosen ? Conditional->getTrueExpr()
                                        : Conditional->getFalseExpr(),
                                 Depth + 1);
      return isLookupIntoActor(Conditional->getTrueExpr(), Depth + 1) ||
             isLookupIntoActor(Conditional->getFalseExpr(), Depth + 1);
    }
    if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(E))
      return isLookupIntoActor(Materialize->getSubExpr(), Depth);
    if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E)) {
      // The storage of a `std::array` held in the object, `m_Values.data()`
      // or `m_Values.front()`, lives with it, like its elements by subscript.
      if (const auto *Record = Call->getImplicitObjectArgument()
                                   ->getType()
                                   .getNonReferenceType()
                                   ->getPointeeOrArrayElementType()
                                   ->getAsCXXRecordDecl();
          Record && Record->getIdentifier() && Record->getName() == "array" &&
          Record->isInStdNamespace())
        return isLookupIntoActor(Call->getImplicitObjectArgument(), Depth);
      // An operator called by its name, `mp_pOwner.operator*()` or
      // `.operator->()`, is like the operator, `*mp_pOwner`.
      if (const CXXMethodDecl *Method = Call->getMethodDecl();
          Method &&
          (Method->getOverloadedOperator() == OO_Star ||
           Method->getOverloadedOperator() == OO_Arrow) &&
          Method->getNumParams() == 0) {
        if (isOptionalHolder(Call->getImplicitObjectArgument()->getType()))
          return isRooted(Call->getImplicitObjectArgument());
        return isLookupIntoActor(Call->getImplicitObjectArgument(), Depth);
      }
      // What a handle or an iterator refers to, like `Mapping.f_GetResult()`,
      // `iEntry.f_GetCurrent()`, `iEntry.operator->()` or the conversion to
      // the reference or pointer.
      if (const CXXMethodDecl *Method = Call->getMethodDecl();
          Method && (Method->getReturnType()->isReferenceType() ||
                     Method->getReturnType()->isPointerType() ||
                     isa<CXXConversionDecl>(Method)) &&
          (isReferenceHandle(Call->getImplicitObjectArgument()
                                 ->IgnoreParenImpCasts()
                                 ->getType()) ||
           isIteratorType(Call->getImplicitObjectArgument()
                              ->IgnoreParenImpCasts()
                              ->getType())) &&
          isLookupIntoActor(Call->getImplicitObjectArgument(), Depth))
        return true;
      // The pointer of an owning pointer member, like `mp_pInternal.f_Get()`
      // or `mp_pValue.get()`, lives as long as the actor, unless the owning
      // pointer is in an entry, `mp_Items[Key].m_pValue.f_Get()`, which can
      // be removed. What an optional holds, which f_Get() returns a reference
      // to, can be cleared.
      // Only of a pointer, which has `operator->`, not of what holds its
      // data otherwise, like `int *f_Get()` of a vector it can clear.
      if (const CXXMethodDecl *Method = Call->getMethodDecl();
          Method && Method->getIdentifier() &&
          (Method->getName() == "f_Get" || Method->getName() == "get") &&
          Method->getReturnType()->isPointerType() &&
          hasOverloadedOperator(Method->getParent(), {OO_Arrow}) == true)
        return isLookupIntoActor(Call->getImplicitObjectArgument(), Depth);
      // A value it gives, like the count `f_GetLen()` returns, also bound to
      // a reference that extends it, is its own.
      if (!Call->isGLValue() && !canHoldLookup(Call->getType()))
        return false;
      // One of the actor itself whose body can be seen, and that isn't
      // overridden, gives what it returns: a lookup only when that is one,
      // not a member that lives with the object, `return m_Value;`, a global,
      // or what it is given, `return _pValue;`, unless that is one.
      // Also through an unchanged local pointer to it, `pThis->f_Get()` with
      // `auto *pThis = this`, or `(*this).f_Get()`, or a parameter that is
      // the object of the actor, `_pThis->f_Get()`.
      auto IsThis = [](const Expr *Object) {
        Object = Object->IgnoreParenImpCasts();
        if (const auto *Deref = dyn_cast<UnaryOperator>(Object);
            Deref && Deref->getOpcode() == UO_Deref)
          Object = Deref->getSubExpr()->IgnoreParenImpCasts();
        if (const auto *Ref = dyn_cast<DeclRefExpr>(Object))
          if (const auto *Alias = dyn_cast<VarDecl>(Ref->getDecl());
              Alias && Alias->hasLocalStorage() && !isa<ParmVarDecl>(Alias) &&
              Alias->getType()->isPointerType() && Alias->getInit() &&
              !isChangedVariable(Alias))
            Object = Alias->getInit()->IgnoreParenImpCasts();
        if (const auto *Ref = dyn_cast<DeclRefExpr>(Object))
          return isActorObjectParameter(Ref->getDecl());
        return isa<CXXThisExpr>(Object);
      };
      // Also a virtual one called by name, `CApp::f_Name()`, which runs the
      // body that is seen.
      // Also what is named for the object of the actor, the `m_pThis` of an
      // internal, or an unchanged local reference to it, `CApp &This =
      // *this`, of a member function of the actor.
      if (const CXXMethodDecl *Method = Call->getMethodDecl();
          Method && Depth < 8 &&
          (IsThis(Call->getImplicitObjectArgument()) ||
           (isActorClass(Method->getParent()) &&
            isOwnActorObject(Call->getImplicitObjectArgument()))) &&
          !callsOverride(Call))
        if (const FunctionDecl *Definition = Method->getDefinition();
            Definition && Definition->getBody() &&
            !isa<CoroutineBodyStmt>(Definition->getBody())) {
          llvm::SmallVector<const Expr *, 2> Values;
          std::function<void(const Stmt *)> Returns = [&](const Stmt *S) {
            if (!S || isa<LambdaExpr>(S))
              return;
            if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
              if (Return->getRetValue())
                Values.push_back(Return->getRetValue());
              return;
            }
            // Not what never runs, in a branch an `if constexpr` discards or
            // a constant condition never takes, or after what always leaves,
            // `if constexpr (t_bDefault) return &m_Default;`.
            if (const auto *Block = dyn_cast<CompoundStmt>(S);
                Block && !hasJumpTarget(Block)) {
              for (const Stmt *Statement : Block->body()) {
                Returns(Statement);
                if (malterlib::alwaysLeaves(Statement,
                                            Definition->getASTContext()))
                  break;
              }
              return;
            }
            for (const Stmt *Child :
                 evaluatedChildren(S, Definition->getASTContext()))
              Returns(Child);
          };
          Returns(Definition->getBody());
          // What it gives back of what it is given, also cast or through a
          // local, `return static_cast<int *>(_pValue);`.
          for (const Expr *Argument : returnedArguments(Call))
            if (isLookupIntoActor(Argument, Depth + 1))
              return true;
          // Also a member of what it is given by reference, `return
          // _Value.m_Name;` or `return &_Value.m_Name;`, which is in that.
          if (!Values.empty())
            return llvm::any_of(Values, [&](const Expr *Value) {
              const Expr *Part = skipWrappers(Value)->IgnoreParenImpCasts();
              if (const auto *Address = dyn_cast<UnaryOperator>(Part);
                  Address && Address->getOpcode() == UO_AddrOf)
                Part = Address->getSubExpr()->IgnoreParenImpCasts();
              bool Member = false;
              while (const auto *Access = dyn_cast<MemberExpr>(Part)) {
                if (Access->isArrow() ||
                    !isa<FieldDecl>(Access->getMemberDecl()))
                  break;
                Member = true;
                Part = Access->getBase()->IgnoreParenImpCasts();
              }
              if (const auto *Ref = dyn_cast<DeclRefExpr>(Part); Member && Ref)
                if (const auto *Parameter =
                        dyn_cast<ParmVarDecl>(Ref->getDecl());
                    Parameter && Parameter->getType()->isReferenceType() &&
                    Parameter->getFunctionScopeIndex() < Call->getNumArgs() &&
                    isLookupIntoActor(
                        Call->getArg(Parameter->getFunctionScopeIndex()),
                        Depth + 1))
                  return true;
              const auto *Given = dyn_cast<DeclRefExpr>(
                  skipWrappers(Value)->IgnoreParenImpCasts());
              if (const auto *Parameter =
                      Given ? dyn_cast<ParmVarDecl>(Given->getDecl()) : nullptr)
                return Parameter->getFunctionScopeIndex() <
                           Call->getNumArgs() &&
                       isLookupIntoActor(
                           Call->getArg(Parameter->getFunctionScopeIndex()),
                           Depth + 1);
              // A lookup in what it is given by reference, `return
              // _Items.f_FindEqual(1);` of `f_Find(mp_Items)`.
              if (const auto *Lookup = dyn_cast<CXXMemberCallExpr>(
                      skipWrappers(Value)->IgnoreParenImpCasts());
                  Lookup && Lookup->getImplicitObjectArgument() &&
                  Lookup->getMethodDecl() &&
                  (Lookup->getType()->isPointerType() ||
                   isIteratorType(Lookup->getType()) || Lookup->isGLValue()) &&
                  !returnsOnlyStatic(Lookup->getMethodDecl()))
                if (const auto *Object = dyn_cast<DeclRefExpr>(
                        Lookup->getImplicitObjectArgument()
                            ->IgnoreParenImpCasts()))
                  if (const auto *Parameter =
                          dyn_cast<ParmVarDecl>(Object->getDecl());
                      Parameter && Parameter->getType()->isReferenceType() &&
                      Parameter->getFunctionScopeIndex() < Call->getNumArgs() &&
                      isRooted(
                          Call->getArg(Parameter->getFunctionScopeIndex())))
                    return true;
              return isLookupIntoActor(Value, Depth + 1);
            });
        }
      // Not one that can be seen to give back only what has static storage,
      // `char const *f_Category() const { return "Downloads"; }`, unless an
      // override can give something else.
      if (const CXXMethodDecl *Method = Call->getMethodDecl();
          Method && returnsOnlyStatic(Method) && !callsOverride(Call))
        return false;
      // One of another object that can be seen to give back only members of
      // its own, `int &f_Value() { return m_Value; }`, which live with it,
      // gives a lookup when the object is one.
      if (const CXXMethodDecl *Method = Call->getMethodDecl();
          Method && Depth < 8 && !callsOverride(Call))
        if (const FunctionDecl *Definition = Method->getDefinition();
            Definition && Definition->getBody() &&
            !isa<CoroutineBodyStmt>(Definition->getBody())) {
          const llvm::SmallVector<const Expr *, 4> Values =
              returnedValues(Definition);
          if (!Values.empty() && givesOnlyMembers(Values, nullptr))
            return isLookupIntoActor(Call->getImplicitObjectArgument(),
                                     Depth + 1);
        }
      // Not into a member that is const itself, `TCMap<int, int> const
      // m_Configuration`, also in one, whose entries nothing can remove, unlike
      // one through a reference to const, when they hold only values: what
      // a pointer in one points to, `TCMap<int, CEntry> const mp_Index` of
      // `int *m_pValue`, can be removed from elsewhere.
      const QualType Entry =
          Call->getType()->isPointerType() || Call->getType()->isReferenceType()
              ? Call->getType()->getPointeeType()
              : Call->getType();
      for (const Expr *Object =
               !Entry.isNull() && !Entry->isDependentType() && Context &&
                       holdsOnlyValues(Entry, *Context) &&
                       Call->getImplicitObjectArgument()
                   ? Call->getImplicitObjectArgument()->IgnoreParenImpCasts()
                   : nullptr;
           const auto *Member = dyn_cast_or_null<MemberExpr>(Object);
           Object = Member->getBase()->IgnoreParenImpCasts()) {
        const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
        if (!Field || Field->getType()->isReferenceType() ||
            Field->getType()->isPointerType() || Field->isMutable())
          break;
        if (Field->getType().isConstQualified())
          return false;
        if (Member->isArrow())
          break;
      }
      return isRooted(Call->getImplicitObjectArgument());
    }
    // An iterator of the data from a free function, `begin(mp_Items)`, also
    // a pointer into it, like `begin` of a vector gives.
    if (const auto *Free = dyn_cast<CallExpr>(E);
        Free && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Free) &&
        Free->getNumArgs() == 1 &&
        (isIteratorType(Free->getType()) || Free->getType()->isPointerType()) &&
        llvm::is_contained({StringRef("begin"), StringRef("cbegin"),
                            StringRef("rbegin"), StringRef("crbegin"),
                            StringRef("end"), StringRef("cend"),
                            StringRef("rend"), StringRef("crend")},
                           calleeName(Free))) {
      // Of an array the object holds, `std::begin(m_Values)`, also a
      // `std::array`, which lives with it, like its elements by subscript.
      const QualType Range = Free->getArg(0)->getType().getNonReferenceType();
      const CXXRecordDecl *Array = Range->getAsCXXRecordDecl();
      if (Range->isConstantArrayType() ||
          (Array && Array->getIdentifier() && Array->getName() == "array" &&
           Array->isInStdNamespace()))
        return isLookupIntoActor(Free->getArg(0), Depth);
      return isRooted(Free->getArg(0));
    }
    // What a function called through a pointer to it gives, `fFind(mp_Items,
    // 1)` of `auto fFind = &fg_Find;`, which can find it in what it is given.
    if (const auto *Indirect = dyn_cast<CallExpr>(E);
        Indirect && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Indirect) &&
        !Indirect->getDirectCallee() &&
        (Indirect->isGLValue() || canHoldLookup(Indirect->getType())) &&
        llvm::any_of(Indirect->arguments(),
                     [&](const Expr *Given) { return isRooted(Given); }))
      return true;
    // What a free function whose body can be seen gives back as it is given
    // it, `fg_Identity(mp_Items.f_FindEqual(1))` of `int *fg_Identity(int
    // *_pValue) { return _pValue; }`.
    // Only that, when each return gives one of them that it doesn't change,
    // `return o_pTo;` of `fg_StrCopy(Buffer, m_Text)`, not what it reads of
    // the others.
    if (const auto *Free = dyn_cast<CallExpr>(E);
        Free && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Free)) {
      for (const Expr *Argument : returnedArguments(Free))
        if (isLookupIntoActor(Argument, Depth + 1))
          return true;
      if (const FunctionDecl *Callee = Free->getDirectCallee()) {
        const llvm::SmallVector<const Expr *, 4> Values =
            returnedValues(Callee);
        if (!Values.empty() && llvm::all_of(Values, [&](const Expr *Value) {
              const auto *Ref = dyn_cast<DeclRefExpr>(
                  skipWrappers(Value)->IgnoreParenCasts());
              const auto *Parameter =
                  Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl()) : nullptr;
              return Parameter && !isChangedVariable(Parameter) &&
                     (Parameter->getType()->isPointerType() ||
                      (Free->isGLValue() &&
                       Parameter->getType()->isReferenceType()));
            }))
          return false;
      }
    }
    // What a free function gives of what it is given by reference, a pointer
    // or iterator into it, like `fg_FindEqual(mp_Items, Key)`, which looks it
    // up like the member function does.
    if (const auto *Free = dyn_cast<CallExpr>(E);
        Free && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Free) &&
        Free->getDirectCallee() &&
        (Free->getType()->isPointerType() || isIteratorType(Free->getType()) ||
         Free->isGLValue()) &&
        !isMoveFunction(calleeName(Free)) &&
        !isForwardFunction(calleeName(Free)) && !isAddressOfCall(Free) &&
        // Not one that can be seen to give only what has static storage,
        // `return "Name";`.
        !returnsOnlyStatic(Free->getDirectCallee())) {
      // A member function with an explicit object, `int &f_Value(this CApp
      // &_Self) { return _Self.m_Value; }`, that gives only members of the
      // object, which live with it: a lookup when the object is one,
      // `mp_Items[1].f_Value()`.
      if (const auto *Method = dyn_cast<CXXMethodDecl>(Free->getDirectCallee());
          Method && Method->isExplicitObjectMemberFunction())
        if (const FunctionDecl *Definition = Method->getDefinition();
            Definition && Definition->getNumParams() >= 1) {
          const ParmVarDecl *Self = Definition->getParamDecl(0);
          const llvm::SmallVector<const Expr *, 4> Values =
              returnedValues(Definition);
          if (!Values.empty() && llvm::all_of(Values, [&](const Expr *Value) {
                const Expr *Part = Value->IgnoreParenImpCasts();
                if (const auto *Address = dyn_cast<UnaryOperator>(Part);
                    Address && Address->getOpcode() == UO_AddrOf)
                  Part = Address->getSubExpr()->IgnoreParenImpCasts();
                bool Member = false;
                while (const auto *Access = dyn_cast<MemberExpr>(Part)) {
                  if (!isa<FieldDecl>(Access->getMemberDecl()) ||
                      Access->isArrow())
                    return false;
                  Member = true;
                  Part = Access->getBase()->IgnoreParenImpCasts();
                }
                const auto *Ref = dyn_cast<DeclRefExpr>(Part);
                return Member && Ref && Ref->getDecl() == Self;
              }))
            return Free->getNumArgs() >= 1 &&
                   isLookupIntoActor(Free->getArg(0), Depth + 1);
        }
      // One whose body can be seen to only give the address of what it is
      // given by reference, `return &_Value;`, gives that: the address of a
      // lookup, `fg_Address(*mp_Items.f_FindEqual(1))`, is one, but not that
      // of what the object holds, `fg_Address(m_Value)`.
      const llvm::SmallVector<const Expr *, 4> Values =
          returnedValues(Free->getDirectCallee());
      if (!Values.empty() && llvm::all_of(Values, [](const Expr *Value) {
            const auto *Address =
                dyn_cast<UnaryOperator>(Value->IgnoreParenImpCasts());
            const auto *Ref =
                Address && Address->getOpcode() == UO_AddrOf
                    ? dyn_cast<DeclRefExpr>(
                          Address->getSubExpr()->IgnoreParenImpCasts())
                    : nullptr;
            const auto *Parameter =
                Ref ? dyn_cast<ParmVarDecl>(Ref->getDecl()) : nullptr;
            return Parameter && Parameter->getType()->isReferenceType();
          })) {
        for (const Expr *Argument : returnedArguments(Free, /*Addressed=*/true))
          if (isLookupIntoActor(Argument, Depth + 1))
            return true;
        return false;
      }
      // Nor one whose body can be seen to give only what it allocates, a copy
      // of what it is given, `fg_ConstructObject<int>(Allocator,
      // mp_Items[1])` of `return new t_CObject(...)`, which has its own
      // storage.
      if (!Values.empty() && llvm::all_of(Values, [](const Expr *Value) {
            return isa<CXXNewExpr>(Value->IgnoreParenImpCasts());
          }))
        return false;
      for (unsigned iArgument = 0;
           iArgument < Free->getNumArgs() &&
           iArgument < Free->getDirectCallee()->getNumParams();
           ++iArgument) {
        const QualType Parameter =
            Free->getDirectCallee()->getParamDecl(iArgument)->getType();
        const Expr *Argument = Free->getArg(iArgument);
        const FunctionDecl *Definition =
            Free->getDirectCallee()->getDefinition();
        // Unless its body can be seen to give back only members of what it is
        // given, which live with it, `return _App.m_Value;`: a lookup when
        // what it is given is one, `fg_Value(mp_Items[1])`.
        if (Parameter->isReferenceType() &&
            !Argument->getType()->isPointerType() && isRooted(Argument)) {
          if (!(Definition && !Values.empty() &&
                givesOnlyMembers(Values, Definition->getParamDecl(iArgument))))
            return true;
          if (isLookupIntoActor(Argument, Depth + 1))
            return true;
        }
        // Also the object given by a pointer, `fs_Find(this, 1)`, to one whose
        // body can be seen, like a member function of it, unless it gives back
        // only members of it, which live with it, `return
        // &_pThis->m_Fixed;`.
        if (Parameter->isPointerType() && Definition && !Values.empty() &&
            (isa<CXXThisExpr>(Argument->IgnoreParenImpCasts()) ||
             isRooted(Argument)) &&
            !givesOnlyMembers(Values, Definition->getParamDecl(iArgument)))
          return true;
      }
    }
    // A pointer that a free function gives back as it is, `fg_Const(pEntry)`
    // of `t_CType const &fg_Const(t_CType const &_Value)`: a reference to an
    // argument of the type it returns.
    if (const auto *Free = dyn_cast<CallExpr>(E);
        Free && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Free) &&
        Free->getDirectCallee() && Free->isGLValue() &&
        Free->getType()->isPointerType())
      for (unsigned iArgument = 0;
           iArgument < Free->getNumArgs() &&
           iArgument < Free->getDirectCallee()->getNumParams();
           ++iArgument)
        if (Free->getDirectCallee()
                ->getParamDecl(iArgument)
                ->getType()
                ->isReferenceType() &&
            Free->getArg(iArgument)->getType()->isPointerType() &&
            Free->getArg(iArgument)
                    ->getType()
                    ->getPointeeType()
                    .getUnqualifiedType() ==
                Free->getType()->getPointeeType().getUnqualifiedType() &&
            isLookupIntoActor(Free->getArg(iArgument), Depth + 1))
          return true;
    // The address of a lookup, `std::addressof(mp_Items[Key])`.
    if (const auto *Address = dyn_cast<CallExpr>(E); isAddressOfCall(Address))
      return isLookupIntoActor(Address->getArg(0), Depth + 1);
    // The old value of what `fg_Exchange` assigns, `fg_Exchange(pEntry,
    // nullptr)`, is what it pointed to.
    if (const auto *Exchange = dyn_cast<CallExpr>(E);
        Exchange && Exchange->getNumArgs() == 2 &&
        (isExchangeFunction(calleeName(Exchange))))
      return isLookupIntoActor(Exchange->getArg(0), Depth + 1);
    if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
        Call && Call->getNumArgs() >= 1) {
      // `m_Items[Key]`, and `m_Items(Key)`, which finds or adds an entry of a
      // `TCMap`, like a member call.
      // A value it gives, like `int operator[](int) const`, is its own, like
      // that of a member function.
      // An element of a `std::array` held in the object lives with it, like
      // one of a built-in array.
      if (Call->getOperator() == OO_Subscript) {
        const QualType Object = Call->getArg(0)->getType().getNonReferenceType();
        const auto *Record = Object->getAsCXXRecordDecl();
        if (Record && Record->getIdentifier() && Record->getName() == "array" &&
            Record->isInStdNamespace())
          return isLookupIntoActor(Call->getArg(0), Depth);
      }
      // Nor one whose body can be seen to give only what the object holds,
      // `return m_Axis[_iAxis];` of a `TCVec`, which lives with it.
      if (const auto *Method =
              dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
          Method && Depth < 8 &&
          (Call->getOperator() == OO_Subscript ||
           Call->getOperator() == OO_Call) &&
          !(Method->isVirtual() && !Method->hasAttr<FinalAttr>() &&
            !Method->getParent()->hasAttr<FinalAttr>()))
        if (const FunctionDecl *Definition = Method->getDefinition();
            Definition && Definition->getBody() &&
            !isa<CoroutineBodyStmt>(Definition->getBody())) {
          const llvm::SmallVector<const Expr *, 4> Values =
              returnedValues(Definition);
          if (!Values.empty() && givesOnlyMembers(Values, nullptr))
            return isLookupIntoActor(Call->getArg(0), Depth + 1);
        }
      // Also of what a callable is given, `fFind(mp_Items, 1)` of a
      // `TCFunction<CEntry *(TCMap<int, CEntry> &, int)>`, which it can find
      // what it gives in.
      if (Call->getOperator() == OO_Call)
        return (Call->isGLValue() || canHoldLookup(Call->getType())) &&
               llvm::any_of(Call->arguments(),
                            [&](const Expr *Given) { return isRooted(Given); });
      if (Call->getOperator() == OO_Subscript)
        return (Call->isGLValue() || canHoldLookup(Call->getType())) &&
               isRooted(Call->getArg(0));
      // What an optional holds can be cleared, `mp_Value.f_Clear()`, unlike
      // what an owning pointer member points to.
      if (Call->getOperator() == OO_Arrow || Call->getOperator() == OO_Star) {
        if (isOptionalHolder(Call->getArg(0)->getType()))
          return isRooted(Call->getArg(0));
        return isLookupIntoActor(Call->getArg(0), Depth);
      }
    }
    // An element of an array held in the object, `mp_Values[Index]`, lives
    // with what holds the array; one through a pointer is a lookup, also
    // through a lookup, `pEntry[0]` like `*pEntry`.
    if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E)) {
      const Expr *Base = Subscript->getBase()->IgnoreParenImpCasts();
      if (Base->getType()->isConstantArrayType())
        return isLookupIntoActor(Base, Depth);
      return isRooted(Subscript->getBase()) || isLookupIntoActor(Base, Depth);
    }
    // A copy of an earlier lookup, like `auto *pAlias = pValue`, also of one
    // assigned before.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
      // A name of a structured binding of a reference, `auto &[Value] =
      // mp_Items[0]`, is in what that refers to.
      if (const auto *Binding = dyn_cast<BindingDecl>(Ref->getDecl())) {
        const auto *Decomposed =
            dyn_cast_or_null<VarDecl>(Binding->getDecomposedDecl());
        return Decomposed && Decomposed->getType()->isReferenceType() &&
               Binding->getBinding() &&
               isLookupIntoActor(Binding->getBinding(), Depth + 1);
      }
      const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
      if (Variable && OwnedRanges.contains(Variable))
        return false;
      if (Variable && LookupSet.contains(Variable))
        return true;
      // What it holds where it is read, `p = mp_Items.f_FindEqual(0); return
      // p;` in a getter, when that can be told, and else what it is made with.
      if (!Variable || isa<ParmVarDecl>(Variable) ||
          !(Variable->hasLocalStorage() || Variable->isInitCapture()) ||
          !canHoldLookup(Variable->getType()))
        return false;
      const Expr *Value = Variable->isInitCapture()
                              ? Variable->getInit()
                              : assignedValueAt(Variable, Ref);
      // Also any of those that branches that each assign it leave, `if
      // (_bFirst) pValue = mp_Items.f_FindEqual(1); else pValue = ...`.
      if (!Value && !Variable->isInitCapture()) {
        const llvm::SmallVector<const Expr *, 2> Values =
            assignedValuesAt(Variable, Ref);
        if (!Values.empty())
          return llvm::any_of(Values, [&](const Expr *Held) {
            return isLookupIntoActor(Held, Depth + 16);
          });
      }
      // Else any of what it may hold: what any assignment gives it, `if
      // (_bFind) pValue = mp_Items.f_FindEqual(_Key);`, or what it is made
      // with.
      if (!Value && !Variable->isInitCapture())
        if (const auto *Function = dyn_cast_or_null<FunctionDecl>(
                Variable->getParentFunctionOrMethod());
            Function && Function->getBody()) {
          bool Any = false;
          std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
            if (!S || Any || isa<LambdaExpr>(S))
              return;
            if (const auto *Assign = dyn_cast<BinaryOperator>(S);
                Assign && Assign->getOpcode() == BO_Assign &&
                llvm::any_of(assignedVariables(Assign->getLHS()),
                             [&](const DeclRefExpr *Target) {
                               return Target->getDecl() == Variable;
                             }) &&
                isLookupIntoActor(Assign->getRHS(), Depth + 16)) {
              Any = true;
              return;
            }
            for (const Stmt *Child : childrenOnce(S))
              Find(Child);
          };
          Find(Function->getBody());
          if (Any)
            return true;
        }
      if (!Value)
        Value = Variable->getInit();
      return Value && isLookupIntoActor(Value, Depth + 16);
    }
    return false;
  }

  /// Whether \p Declaration is a local reference, not a capture, that is
  /// bound where it is declared.
  static bool isLocalReferenceAlias(const ValueDecl *Declaration) {
    const auto *Alias = dyn_cast<VarDecl>(Declaration);
    return Alias && Alias->getType()->isReferenceType() &&
           resolveReferenceCapture(Alias) == Alias &&
           Alias->hasLocalStorage() && Alias->getInit();
  }

public:
  void addAssigned(const Expr *Left, const Expr *Right) {
    // Either of a conditional may be assigned, and keep what it held.
    const llvm::SmallVector<const DeclRefExpr *, 2> Targets =
        assignedVariables(Left);
    // One assigned through a local pointer to it, `*ppEntry = ...`, is
    // assigned where that is written, which is where the pointer is read,
    // not where the name it points to is.
    const auto *Deref = dyn_cast<UnaryOperator>(Left->IgnoreParenImpCasts());
    const auto *Through =
        Deref && Deref->getOpcode() == UO_Deref
            ? dyn_cast<DeclRefExpr>(Deref->getSubExpr()->IgnoreParenImpCasts())
            : nullptr;
    for (const DeclRefExpr *Ref : Targets) {
      if (!Ref->getType()->isPointerType())
        continue;
      // A local reference bound to either of a conditional of pointers
      // assigns either, also through references bound to such references,
      // `int *&z = c ? a : q;` with `int *&a = b ? p : q;`.
      if (isLocalReferenceAlias(Ref->getDecl())) {
        llvm::SmallVector<const ValueDecl *, 4> Bound;
        llvm::SmallPtrSet<const ValueDecl *, 4> Expanded;
        bool Maybe = Targets.size() > 1;
        llvm::SmallVector<const VarDecl *, 4> Pending = {
            cast<VarDecl>(Ref->getDecl())};
        while (!Pending.empty()) {
          const VarDecl *Alias = Pending.pop_back_val();
          if (!Expanded.insert(Alias).second)
            continue;
          const llvm::SmallVector<const DeclRefExpr *, 2> Initial =
              assignedVariables(Alias->getInit());
          Maybe |= Initial.size() > 1;
          for (const DeclRefExpr *Target : Initial)
            if (isLocalReferenceAlias(Target->getDecl()))
              Pending.push_back(cast<VarDecl>(Target->getDecl()));
            else
              Bound.push_back(resolveReferenceCapture(Target->getDecl()));
        }
        for (const ValueDecl *Target : Bound)
          AssignedThrough[Ref].push_back({Target, Right, Maybe});
        continue;
      }
      if (Ref->getDecl()->getType()->isReferenceType() &&
          resolveReferenceCapture(Ref->getDecl()) == Ref->getDecl())
        continue;
      if (!Left->getSourceRange().fullyContains(Ref->getSourceRange())) {
        if (Through && Targets.size() == 1)
          AssignedThrough[Through].push_back({Ref->getDecl(), Right, false});
        continue;
      }
      AssignedRefs[Ref] = Right;
      if (Targets.size() > 1)
        MaybeAssignedRefs.insert(Ref);
    }
  }
};

/// Whether the pointer \p Variable of the function that declares it may point
/// into the data of the actor where \p Lambda captures it, following the
/// lookups and other values assigned to it along the control flow. True when
/// that can't be told.
bool intoActorAtCapture(const VarDecl *Variable, const LambdaExpr *Lambda,
                        ASTContext &Context) {
  const auto *Outer =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  Stmt *Body = Outer ? Outer->getBody() : nullptr;
  if (!Body)
    return true;
  if (auto *CoroutineBody = dyn_cast<CoroutineBodyStmt>(Body))
    Body = CoroutineBody->getBody();
  BodyCollector Collector;
  Collector.Context = &Context;
  // The objects of actors that its parameters point to, and `this` of a
  // member of an actor.
  for (const ParmVarDecl *Parameter : Outer->parameters()) {
    if (refersToActorClass(Parameter->getType()))
      Collector.ActorObjects.insert(Parameter);
  }
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Outer);
      Method && !Method->isStatic() && isActorClass(Method->getParent()))
    Collector.ThisIsActor = true;
  for (size_t nLookups = SIZE_MAX; nLookups != Collector.Lookups.size();) {
    nLookups = Collector.Lookups.size();
    Collector.TraverseStmt(Body);
    Collector.propagateLookups();
  }
  CFG::BuildOptions Options;
  Options.setAllAlwaysAdd();
  Options.AddEHEdges = true;
  const std::unique_ptr<CFG> Graph = CFG::buildCFG(Outer, Body, &Context, Options);
  if (!Graph)
    return true;
  const llvm::BitVector Reachable = reachableBlocks(*Graph);
  // Whether the pointer may point into the data at the end of each block.
  llvm::DenseMap<const CFGBlock *, bool> AtEnd;
  auto Transfer = [&](const Stmt *S, bool &Into) {
    if (const auto *Declaration = dyn_cast<DeclStmt>(S)) {
      for (const Decl *D : Declaration->decls()) {
        if (D == Variable)
          Into = Variable->getInit() &&
                 Collector.isLookupIntoActor(Variable->getInit());
      }
    } else if (const auto *Binary = dyn_cast<BinaryOperator>(S);
               Binary && Binary->getOpcode() == BO_Assign) {
      if (const auto *Ref =
              dyn_cast<DeclRefExpr>(Binary->getLHS()->IgnoreParenImpCasts());
          Ref && Ref->getDecl() == Variable)
        Into = Collector.isLookupIntoActor(Binary->getRHS());
    }
  };
  auto Entry = [&](const CFGBlock *Block) {
    bool Into = false;
    for (const CFGBlock::AdjacentBlock &Predecessor : Block->preds()) {
      if (const CFGBlock *Pred = Predecessor.getReachableBlock())
        Into = Into || AtEnd.lookup(Pred);
    }
    return Into;
  };
  // Looking again only at the successors of a block whose state changed.
  {
    std::deque<const CFGBlock *> Work;
    llvm::BitVector Queued(Graph->getNumBlockIDs());
    auto Enqueue = [&](const CFGBlock *Block) {
      if (Block && Reachable.test(Block->getBlockID()) &&
          !Queued.test(Block->getBlockID())) {
        Queued.set(Block->getBlockID());
        Work.push_back(Block);
      }
    };
    Enqueue(&Graph->getEntry());
    for (const CFGBlock *Block : llvm::reverse(*Graph))
      Enqueue(Block);
    while (!Work.empty()) {
      const CFGBlock *Block = Work.front();
      Work.pop_front();
      Queued.reset(Block->getBlockID());
      bool Into = Entry(Block);
      for (const CFGElement &Element : *Block) {
        if (const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>())
          Transfer(Statement->getStmt(), Into);
      }
      auto [It, Inserted] = AtEnd.try_emplace(Block, false);
      if (Into && !It->second) {
        It->second = true;
        for (const CFGBlock::AdjacentBlock &Successor : Block->succs())
          Enqueue(Successor.getReachableBlock());
      }
    }
  }
  for (const CFGBlock *Block : *Graph) {
    if (!Reachable.test(Block->getBlockID()))
      continue;
    bool Into = Entry(Block);
    for (const CFGElement &Element : *Block) {
      const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>();
      if (!Statement)
        continue;
      if (Statement->getStmt() == Lambda)
        return Into;
      Transfer(Statement->getStmt(), Into);
    }
  }
  return true;
}

/// What names \p Variable in a diagnostic: itself, or the first name of a
/// structured binding, which has no name of its own.
static const NamedDecl *nameOf(const VarDecl *Variable) {
  if (const auto *Decomposition = dyn_cast<DecompositionDecl>(Variable);
      Decomposition && !Decomposition->bindings().empty())
    return Decomposition->bindings()[0];
  return Variable;
}

} // namespace

/// The lambda the call \p Called runs, `fStop()`, `fStop.operator()()`,
/// `fg_CallSafe(fStop)` or `fg_OnResume(fStop)`, which runs it when it is
/// awaited, or null.
static const LambdaExpr *invokedLambda(const CallExpr *Called) {
  if (const LambdaExpr *Lambda = calledLambda(Called))
    return Lambda;
  // Also by `std::invoke(fStop)`, which calls it right away.
  if (Called->getNumArgs() >= 1 &&
      (calleeName(Called) == "fg_CallSafe" ||
       calleeName(Called) == "fg_OnResume" || isStdInvoke(Called)))
    return asLambda(Called->getArg(0));
  return nullptr;
}

/// Whether the condition \p Condition of a test of removal flags only reads
/// them: it doesn't suspend, which can come after a flag is read, like
/// `*pDeleted || (co_await ..., false)`, nor call what can remove the entry
/// after it is read, like `*pDeleted || (f_Stop(), false)`.
static bool onlyReadsFlags(const Expr *Condition) {
  std::function<bool(const Stmt *)> Suspends = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return false;
    if (isa<CoroutineSuspendExpr>(S))
      return true;
    return llvm::any_of(childrenOnce(S), Suspends);
  };
  // A call other than of what reads the flag.
  std::function<bool(const Stmt *)> Calls = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return false;
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S)) {
      switch (Operator->getOperator()) {
      case OO_Star:
      case OO_Arrow:
      case OO_Exclaim:
      case OO_EqualEqual:
      case OO_ExclaimEqual:
        break;
      default:
        return true;
      }
    } else if (const auto *Member = dyn_cast<CXXMemberCallExpr>(S)) {
      const CXXMethodDecl *Method = Member->getMethodDecl();
      if (!Method ||
          !(isa<CXXConversionDecl>(Method) ||
            (Method->getIdentifier() &&
             (Method->getName() == "f_Load" || Method->getName() == "load"))))
        return true;
    } else if (isa<CallExpr>(S)) {
      return true;
    }
    return llvm::any_of(childrenOnce(S), Calls);
  };
  return !Suspends(Condition) && !Calls(Condition);
}

/// Whether calling \p Method can free or end the life of what it doesn't
/// create, also through what it calls, as clang instantiated them: deletes,
/// ends lives in place, `pEntry->~CEntry()`, or calls what can't be followed,
/// one without a body, through a pointer, or a virtual function that isn't
/// final. An intrusive container that only links, `m_TimerQueue.f_Remove(
/// pTimer)`, doesn't, and an owning one, `mp_Items.f_Remove(Key)`, does.
static bool mayFree(const FunctionDecl *Method) {
  if (!Method)
    return true;
  struct CMemo {
    llvm::DenseMap<const FunctionDecl *, bool> Decided;
  };
  const ASTContext &Context = Method->getASTContext();
  auto &Decided = astCache<CMemo>(Context).Decided;
  if (auto It = Decided.find(Method); It != Decided.end())
    return It->second;
  // Each function it can reach once, of which only so many are followed.
  llvm::SmallPtrSet<const FunctionDecl *, 32> Reached;
  llvm::SmallPtrSet<const Expr *, 8> Defaults;
  llvm::SmallVector<const FunctionDecl *, 32> Work;
  bool Frees = false;
  auto Reach = [&](const FunctionDecl *Callee) {
    if (Frees)
      return;
    if (!Callee) {
      Frees = true;
      return;
    }
    // What doesn't return, and can't throw to a `catch` around it after which
    // the code goes on, like `abort()`, can't free what is used after the
    // call.
    if (const auto *Prototype = Callee->getType()->getAs<FunctionProtoType>();
        Callee->isNoReturn() && Prototype && Prototype->isNothrow())
      return;
    if (unsigned Builtin = Callee->getBuiltinID()) {
      const std::string Owned = Context.BuiltinInfo.getName(Builtin);
      const StringRef Name = Owned;
      Frees = Name.contains("free") || Name.contains("delete") ||
              Name.contains("realloc");
      return;
    }
    if (Callee->isTrivial())
      return;
    if (const auto *Member = dyn_cast<CXXMethodDecl>(Callee);
        Member && Member->isVirtual() && !Member->hasAttr<FinalAttr>() &&
        !Member->getParent()->hasAttr<FinalAttr>()) {
      Frees = true;
      return;
    }
    const FunctionDecl *Definition = nullptr;
    if (!Callee->hasBody(Definition) || !Definition->getBody() ||
        Definition->isDependentContext() || Reached.size() >= 512) {
      Frees = true;
      return;
    }
    if (Reached.insert(Definition).second)
      Work.push_back(Definition);
  };
  auto ReachDestructor = [&](QualType Type) {
    if (const CXXRecordDecl *Record =
            Context.getBaseElementType(Type)->getAsCXXRecordDecl())
      if (Record->hasDefinition() && !Record->hasTrivialDestructor())
        Reach(Record->getDestructor());
  };
  Reach(Method);
  while (!Work.empty() && !Frees) {
    const FunctionDecl *Function = Work.pop_back_val();
    llvm::SmallVector<const Stmt *, 32> Statements{Function->getBody()};
    // A constructor also runs its initializers, and a destructor those of
    // its members and bases.
    if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Function))
      for (const CXXCtorInitializer *Initializer : Constructor->inits())
        Statements.push_back(Initializer->getInit());
    if (const auto *Destructor = dyn_cast<CXXDestructorDecl>(Function)) {
      const CXXRecordDecl *Record = Destructor->getParent();
      for (const FieldDecl *Field : Record->fields())
        ReachDestructor(Field->getType());
      for (const CXXBaseSpecifier &Base : Record->bases())
        ReachDestructor(Base.getType());
    }
    while (!Statements.empty() && !Frees) {
      const Stmt *S = Statements.pop_back_val();
      if (!S)
        continue;
      if (isa<CXXDeleteExpr, CXXPseudoDestructorExpr>(S)) {
        Frees = true;
        break;
      }
      // A default argument or member initializer, each once, which the
      // calls of a function share.
      if (const auto *Default = dyn_cast<CXXDefaultArgExpr>(S)) {
        if (Defaults.insert(Default->getExpr()).second)
          Statements.push_back(Default->getExpr());
        continue;
      }
      if (const auto *Default = dyn_cast<CXXDefaultInitExpr>(S)) {
        if (Defaults.insert(Default->getExpr()).second)
          Statements.push_back(Default->getExpr());
        continue;
      }
      // What a lambda does runs when it is called, through its call operator.
      if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
        for (const Expr *Init : Lambda->capture_inits())
          Statements.push_back(Init);
        continue;
      }
      // A coroutine runs the body written, and what it awaits and returns:
      // what makes and ends its frame, and suspends and resumes it, doesn't
      // free what it doesn't create.
      if (const auto *Coroutine = dyn_cast<CoroutineBodyStmt>(S)) {
        Statements.push_back(Coroutine->getBody());
        continue;
      }
      if (const auto *Return = dyn_cast<CoreturnStmt>(S)) {
        Statements.push_back(Return->getOperand());
        continue;
      }
      if (const auto *Suspend = dyn_cast<CoroutineSuspendExpr>(S)) {
        Statements.push_back(Suspend->getOperand());
        continue;
      }
      if (const auto *Call = dyn_cast<CallExpr>(S)) {
        const FunctionDecl *Callee = Call->getDirectCallee();
        // Ending the life of one in place, `pEntry->~CEntry()`.
        if (isa_and_nonnull<CXXDestructorDecl>(Callee)) {
          Frees = true;
          break;
        }
        // Also a virtual one called by its qualified name, which is final
        // there.
        if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
            Member && Callee && cast<CXXMethodDecl>(Callee)->isVirtual())
          if (const auto *Named =
                  dyn_cast<MemberExpr>(Member->getCallee()->IgnoreParens());
              Named && Named->hasQualifier()) {
            const FunctionDecl *Definition = nullptr;
            if (!Callee->hasBody(Definition) || !Definition->getBody())
              Frees = true;
            else if (Reached.insert(Definition).second)
              Work.push_back(Definition);
            for (const Stmt *Child : evaluatedChildren(S, Context))
              Statements.push_back(Child);
            continue;
          }
        Reach(Callee);
      } else if (const auto *Construct = dyn_cast<CXXConstructExpr>(S)) {
        Reach(Construct->getConstructor());
        ReachDestructor(Construct->getType());
      } else if (const auto *New = dyn_cast<CXXNewExpr>(S)) {
        // Allocating doesn't free, unless constructing throws.
        if (const CXXConstructExpr *Construct = New->getConstructExpr())
          Reach(Construct->getConstructor());
      } else if (const auto *Temporary = dyn_cast<CXXBindTemporaryExpr>(S)) {
        Reach(Temporary->getTemporary()->getDestructor());
      } else if (const auto *Declaration = dyn_cast<DeclStmt>(S)) {
        for (const Decl *D : Declaration->decls())
          if (const auto *Variable = dyn_cast<VarDecl>(D)) {
            ReachDestructor(Variable->getType());
            Statements.push_back(Variable->getInit());
          }
        continue;
      }
      for (const Stmt *Child : evaluatedChildren(S, Context))
        Statements.push_back(Child);
    }
  }
  Decided[Method] = Frees;
  return Frees;
}

void CoroutineActorReferenceCheck::checkFunction(const FunctionDecl *Function,
                                                 const LambdaExpr *Lambda,
                                                 ASTContext &Context) {
  Stmt *Body = Function->getBody();
  if (!Body)
    return;
  if (auto *CoroutineBody = dyn_cast<CoroutineBodyStmt>(Body))
    Body = CoroutineBody->getBody();

  // A copy of a pointer that is only found to be a lookup later in the
  // source, like in a loop, is found by going over the body until nothing new
  // is.
  BodyCollector Collector;
  Collector.Context = &Context;
  Collector.NodeStableContainers = NodeStableContainerList;
  // The objects of other actors that its parameters point to, like `CApp
  // *_pOther` of a member of an actor too, which can remove their entries
  // while it is suspended.
  for (const ParmVarDecl *Parameter : Function->parameters()) {
    if (refersToActorClass(Parameter->getType()))
      Collector.ActorObjects.insert(Parameter);
  }
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      Method && !Method->isStatic() &&
      (isActorClass(Method->getParent()) ||
       (Lambda && isActorClass(enclosingThisClass(Lambda)))))
    Collector.ThisIsActor = true;
  // A function that isn't a member of the actor, also a lambda's call
  // operator or a member of another class, gets the object of an actor
  // through its parameters and captures.
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      !Method || Method->isStatic() || !isActorClass(Method->getParent())) {
    // What a lambda captures, like `[pActor]` of an actor's object, and the
    // objects its init captures look up data of, like `[pEntry =
    // _pActor->mp_Items.f_FindEqual(_Key)]`.
    if (Lambda) {
      for (const LambdaCapture &Capture : Lambda->captures()) {
        if (Capture.capturesVariable() &&
            refersToActorClass(Capture.getCapturedVar()->getType()))
          Collector.ActorObjects.insert(Capture.getCapturedVar());
      }
      // Also the objects that the locals it captures look up data of, like
      // `[pEntry]` of `auto *pEntry = _pActor->mp_Items.f_FindEqual(_Key)`.
      llvm::SmallVector<const Expr *, 8> Inits(Lambda->capture_inits());
      for (const LambdaCapture &Capture : Lambda->captures()) {
        const auto *Variable =
            Capture.capturesVariable()
                ? dyn_cast<VarDecl>(Capture.getCapturedVar())
                : nullptr;
        if (Variable && !Variable->isInitCapture() &&
            Variable->hasLocalStorage() && !isa<ParmVarDecl>(Variable))
          llvm::append_range(Inits, valuesOf(Variable));
      }
      // Through all nodes: a matcher of the parent map's traversal skips the
      // callee of a member call, like `_pActor` of
      // `_pActor->mp_Items.f_FindEqual(_Key)`.
      // Also through the locals they are made from, like `q` of `auto *q =
      // p`.
      llvm::SmallPtrSet<const VarDecl *, 8> Followed;
      std::function<void(const Stmt *)> AddActors = [&](const Stmt *S) {
        if (!S)
          return;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S)) {
          if (refersToActorClass(Ref->getDecl()->getType()))
            Collector.ActorObjects.insert(Ref->getDecl());
          else if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
                   Local && Local->hasLocalStorage() &&
                   !isa<ParmVarDecl>(Local) && Followed.insert(Local).second)
            for (const Expr *Value : valuesOf(Local))
              AddActors(Value);
        }
        for (const Stmt *Child : childrenOnce(S))
          AddActors(Child);
      };
      for (const Expr *Init : Inits)
        AddActors(Init);
    }
  }
  for (size_t nLookups = SIZE_MAX; nLookups != Collector.Lookups.size();) {
    nLookups = Collector.Lookups.size();
    Collector.TraverseStmt(Body);
    Collector.propagateLookups();
  }
  // What a lambda captures is looked up before it runs.
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      Method && Method->getParent()->isLambda()) {
    for (const LambdaCapture &Capture : Method->getParent()->captures()) {
      if (!Capture.capturesVariable())
        continue;
      auto *Variable = dyn_cast<VarDecl>(Capture.getCapturedVar());
      if (!Variable)
        continue;
      Collector.VisitVarDecl(Variable);
      // A pointer of the enclosing function that is assigned a lookup after
      // its declaration may hold it when it is captured.
      if (!Variable->isInitCapture() && Variable->getType()->isPointerType() &&
          !Collector.LookupSet.contains(Variable)) {
        const auto *Outer = dyn_cast_or_null<FunctionDecl>(
            Variable->getParentFunctionOrMethod());
        bool AssignedLookup = false;
        if (Outer && Outer->getBody()) {
          const auto Assignments = match(
              findAll(binaryOperator(
                          hasOperatorName("="),
                          hasLHS(ignoringParenImpCasts(
                              declRefExpr(to(equalsNode(Variable))))))
                          .bind("assignment")),
              *Outer->getBody(), Context);
          for (const auto &Nodes : Assignments) {
            const auto *Assignment =
                Nodes.getNodeAs<BinaryOperator>("assignment");
            AssignedLookup = AssignedLookup ||
                             Collector.isLookupIntoActor(Assignment->getRHS());
          }
        }
        if (AssignedLookup)
          Collector.addLookup(Variable);
      }
    }
  }
  // A lookup used as it is by a call whose arguments suspend,
  // `mp_Items[0].f_Read(co_await fg_Suspend())`, in code that can run: not
  // in a branch that can't be taken, `if (false)`, nor after a `co_return`.
  // What can run is told when there is one, which is rare.
  std::optional<llvm::DenseSet<const Stmt *>> ReachableStatements;
  bool UnknownReachable = false;
  auto CanRun = [&](const Stmt *S) {
    if (!ReachableStatements && !UnknownReachable) {
      CFG::BuildOptions Options;
      Options.setAllAlwaysAdd();
      const std::unique_ptr<CFG> Graph =
          CFG::buildCFG(Function, Body, &Context, Options);
      if (!Graph)
        UnknownReachable = true;
      else {
        ReachableStatements.emplace();
        const llvm::BitVector Reachable = reachableBlocks(*Graph);
        for (const CFGBlock *Block : *Graph)
          if (Block && Reachable.test(Block->getBlockID()))
            for (const CFGElement &Element : *Block)
              if (const std::optional<CFGStmt> Statement =
                      Element.getAs<CFGStmt>())
                ReachableStatements->insert(Statement->getStmt());
      }
    }
    return UnknownReachable || ReachableStatements->contains(S);
  };
  std::function<void(const Stmt *)> FindLateLookups = [&](const Stmt *S) {
    if (!S || isa<LambdaExpr>(S))
      return;
    // Not a variable, also through its members, or either of a conditional,
    // `(bChoose ? pFirst : pSecond)`, whose uses are followed by themselves,
    // also of an array a pointer is at that a subscript indexes,
    // `pEntry->m_Values[...]`.
    std::function<bool(const Expr *, bool, int)> OfVariable =
        [&](const Expr *E, bool ThroughArrow, int Depth) {
          for (E = E->IgnoreParenImpCasts(); const auto *Member =
                                                 dyn_cast<MemberExpr>(E);
               E = Member->getBase()->IgnoreParenImpCasts())
            if (Member->isArrow() && !ThroughArrow)
              break;
          if (const auto *Conditional = dyn_cast<ConditionalOperator>(E))
            return Depth < 8 &&
                   OfVariable(Conditional->getTrueExpr(), ThroughArrow,
                              Depth + 1) &&
                   OfVariable(Conditional->getFalseExpr(), ThroughArrow,
                              Depth + 1);
          return isa<DeclRefExpr>(E);
        };
    // The phases the guards around it guard, declared before it in a scope
    // it is in, `auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();`,
    // which end the coroutine when it resumes in them, for as long as they
    // are in scope, but not those that a callback registered after a guard,
    // which runs after it on resume, can start.
    auto GuardedAround = [&] {
      const SourceManager &SM = Context.getSourceManager();
      auto InScope = [&](const VarDecl *Holder) {
        const auto Parents = instantiatedParents(*Holder, Context);
        const auto *Declaration =
            Parents.empty() ? nullptr : Parents[0].get<DeclStmt>();
        if (!Declaration)
          return false;
        const auto ScopeParents = instantiatedParents(*Declaration, Context);
        const Stmt *Scope =
            ScopeParents.empty() ? nullptr : ScopeParents[0].get<Stmt>();
        return Scope &&
               SM.isBeforeInTranslationUnit(Declaration->getEndLoc(),
                                            S->getBeginLoc()) &&
               !SM.isBeforeInTranslationUnit(Scope->getEndLoc(),
                                             S->getEndLoc());
      };
      unsigned Phases = 0;
      for (const auto &[Holder, Guarded] : Collector.HolderPhases) {
        if (!InScope(Holder))
          continue;
        unsigned Kept = Guarded;
        for (const auto &[Later, Started] : Collector.HolderStarted)
          if (Started && Later != Holder &&
              SM.isBeforeInTranslationUnit(Holder->getLocation(),
                                           Later->getLocation()) &&
              InScope(Later))
            Kept &= ~Started;
        Phases |= Kept;
      }
      return Phases;
    };
    // Not into a collection of the own actor that is declared to lose no
    // entries, `DMibActorEntries() TCMap<...>`, or only in the phases the
    // guards around it guard, of a type whose entries stay where they are
    // when others are added, like for a local reference.
    // Not the phases the call starts in its arguments, after the lookup,
    // `mp_Items[1].f_Read(f_ApplyStopResult(co_await fg_Work()))`, which the
    // guards checked before.
    auto NeverLosesEntries = [&](const Expr *Early, unsigned StartedInCall) {
      const Expr *Key = nullptr;
      const Expr *Owner = nullptr;
      const FieldDecl *Field = lookupCollection(Early, Key, Owner);
      const std::optional<unsigned> Phases =
          Field ? annotatedPhases(Field, AnnotationActorEntries) : std::nullopt;
      return Phases && !(*Phases & ActorPhase_Owner) &&
             (*Phases == 0 ||
              (*Phases & ~(GuardedAround() & ~StartedInCall)) == 0) &&
             isOwnActorObject(Owner) &&
             matchesTypeName(Field->getType(), NodeStableContainerList,
                             Context);
    };
    bool Reported = false;
    for (const Expr *Early : earlyCallOperands(S, Context))
      if (!OfVariable(Early, isa<ArraySubscriptExpr>(S), 0) &&
          Collector.isLookupIntoActor(Early) &&
          !NeverLosesEntries(Early, startedPhases(S, Context)) && CanRun(S)) {
        const SourceLocation Loc =
            reportLocation(S->getBeginLoc(), Context.getSourceManager());
        if (Loc.isValid())
          diag(Loc, "the data of the actor that this call uses is looked up "
                    "before its arguments suspend, during which other calls "
                    "of the actor can change or erase the data; look it up "
                    "after the suspension");
        Reported = true;
        break;
      }
    // Also what a pointer it looks up points to, accessed after a suspension
    // in the same expression, `*(mp_Values.f_GetArray() + co_await
    // fg_Index())`, like through a local pointer: looked up before the
    // suspension, not after it, `(co_await fg_Suspend(), 0)[&mp_Items[0]]`.
    auto LookedUpBefore = [&](const Expr *Root) {
      bool Before = false;
      std::function<void(const Stmt *)> Visit = [&](const Stmt *Inner) {
        if (!Inner || Before || isa<LambdaExpr>(Inner))
          return;
        // An assignment evaluates what it assigns before what it assigns to,
        // `*mp_Items.f_FindEqual(1) = co_await fg_Work()`, also an overloaded
        // one: of one whose target has it, only what is in the target.
        const Expr *Target = nullptr;
        if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
            Assign && Assign->isAssignmentOp())
          Target = Assign->getLHS();
        else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Inner);
                 Operator && Operator->isAssignmentOp() &&
                 Operator->getNumArgs() == 2)
          Target = Operator->getArg(0);
        if (Target &&
            Target->getSourceRange().fullyContains(Root->getSourceRange())) {
          Visit(Target);
          return;
        }
        if (isa<CoroutineSuspendExpr>(Inner) &&
            Context.getSourceManager().isBeforeInTranslationUnit(
                Root->getEndLoc(), Inner->getBeginLoc()))
          Before = true;
        for (const Stmt *Child : childrenOnce(Inner))
          Visit(Child);
      };
      Visit(S);
      return Before;
    };
    if (const Expr *Pointer =
            Reported ? nullptr : accessedAfterSuspension(S, Context))
      for (const Expr *Root : pointerRoots(Pointer))
        if (!isa<DeclRefExpr>(Root) && Collector.isLookupIntoActor(Root) &&
            LookedUpBefore(Root) &&
            !NeverLosesEntries(Root, startedPhases(S, Context)) && CanRun(S)) {
          const SourceLocation Loc =
              reportLocation(S->getBeginLoc(), Context.getSourceManager());
          if (Loc.isValid())
            diag(Loc, "the data of the actor that this expression uses is "
                      "looked up before it suspends, during which other calls "
                      "of the actor can change or erase the data; look it up "
                      "after the suspension");
          break;
        }
    // Not in a branch an `if constexpr` discards.
    for (const Stmt *Child : evaluatedChildren(S))
      FindLateLookups(Child);
  };
  FindLateLookups(Body);
  llvm::DenseMap<const ValueDecl *, unsigned> Tracked;
  llvm::SmallVector<const VarDecl *, 8> Variables;
  for (const VarDecl *Variable : Collector.Lookups) {
    Tracked[Variable] = Variables.size();
    Variables.push_back(Variable);
  }
  if (Variables.empty())
    return;

  // A lambda that copies a tracked pointer has its own copy, which looking the
  // pointer up again afterwards doesn't refresh. Each copy is tracked as a
  // variable of its own, from where the lambda is made.
  llvm::DenseMap<std::pair<const LambdaExpr *, const ValueDecl *>, unsigned>
      LambdaCopies;
  for (const auto &Nodes :
       match(findAll(lambdaExpr().bind("lambda")), *Body, Context)) {
    const auto *Lambda = Nodes.getNodeAs<LambdaExpr>("lambda");
    auto Init = Lambda->capture_init_begin();
    for (const LambdaCapture &Capture : Lambda->captures()) {
      const Expr *CaptureInit = *Init++;
      if (!Capture.capturesVariable())
        continue;
      // An init capture is its own variable, initialized where the lambda is
      // made.
      if (const auto *Variable = dyn_cast<VarDecl>(Capture.getCapturedVar());
          Variable && Variable->isInitCapture()) {
        if (Tracked.count(Variable))
          LambdaCopies[{Lambda, Variable}] = Tracked.lookup(Variable);
        continue;
      }
      if (Capture.getCaptureKind() != LCK_ByCopy)
        continue;
      const DeclRefExpr *Captured = capturedVariableRef(CaptureInit);
      if (!Captured)
        continue;
      const ValueDecl *Source = resolveReferenceCapture(Captured->getDecl());
      if (auto It = Tracked.find(Source); It != Tracked.end()) {
        LambdaCopies[{Lambda, Source}] = Variables.size();
        Variables.push_back(Variables[It->second]);
      }
    }
  }

  CFG::BuildOptions Options;
  Options.setAllAlwaysAdd();
  // Exceptions can reach handlers after a suspension.
  Options.AddEHEdges = true;
  // The end of the scope of an `fg_OnResume` registration ends it. A holder
  // of a dependent type has no destructor in the CFG, but its lifetime ends.
  Options.AddImplicitDtors = true;
  // So do destructors of temporaries, `(void)CStopper{this}`.
  Options.AddTemporaryDtors = true;
  // Past the limit of the uses of a shared default argument, leave out the
  // destructors of the rest, which only end their temporaries.
  Options.OmitRepeatedSharedTemporaryDtors = true;
  Options.AddLifetime = true;
  const std::unique_ptr<CFG> Graph =
      CFG::buildCFG(Function, Body, &Context, Options);
  // Not accepted when its control flow can't be built, like through a long
  // chain of conditionals in default arguments that destroy temporaries.
  if (!Graph) {
    const SourceLocation Loc =
        reportLocation(Function->getLocation(), Context.getSourceManager());
    if (Loc.isValid())
      diag(Loc, "the control flow of %select{%1|the lambda}0 is too complex to "
                "analyze, so whether it uses what refers into the data of the "
                "actor after a suspension can't be told; split up what makes "
                "it complex")
          << isLambdaCallOperator(Function) << Function;
    return;
  }
  // Blocks that can't run, like `if (false)` branches, don't change the state.
  const llvm::BitVector Reachable = reachableBlocks(*Graph);
  const auto SuspendPoints = suspendPoints(Body);

  // What a lookup into a collection declared with `DMibActorEntries` needs to
  // stay valid across a suspension: guards of the phases its entries are
  // removed in. One that needs none, like an entry of an insert-only
  // collection, or the entry this call added by its fresh key, is protected.
  llvm::SmallVector<std::optional<unsigned>, 8> Required;
  llvm::SmallVector<const FieldDecl *, 8> RequiredFields;
  llvm::BitVector Protected(Variables.size());
  for (unsigned iVariable = 0; iVariable < Variables.size(); ++iVariable) {
    const FieldDecl *Field = nullptr;
    Required.push_back(requiredPhases(
        Variables[iVariable],
        Collector.RangeIterators.contains(Variables[iVariable]), Function,
        Context, &Field));
    RequiredFields.push_back(Field);
    if (Required.back() && *Required.back() == 0)
      Protected.set(iVariable);
  }

  // The `fg_OnResume` registrations, and the tracked variables each
  // re-validates.
  llvm::DenseMap<const VarDecl *, unsigned> HolderIndices;
  llvm::SmallVector<llvm::BitVector, 4> HolderVariables;
  /// Of those, what each re-validates only by a comparison.
  llvm::SmallVector<llvm::BitVector, 4> HolderComparedVariables;
  llvm::SmallVector<llvm::BitVector, 4> HolderRemovedVariables;
  llvm::SmallVector<const VarDecl *, 4> HolderDecls;
  llvm::SmallVector<unsigned, 4> HolderPhaseMasks;
  llvm::SmallVector<unsigned, 4> HolderStartedMasks;
  for (const auto &[Holder, Revalidated] : Collector.Revalidations) {
    llvm::BitVector Bits(Variables.size());
    for (const ValueDecl *Variable : Revalidated) {
      if (auto Index = Tracked.find(Variable); Index != Tracked.end())
        Bits.set(Index->second);
    }
    // References made from what the callback keeps pointing to the same data,
    // like `auto &Distribution = *pDistribution` and `auto &Settings =
    // Distribution.m_Settings`, through what it points to and its members.
    llvm::SmallPtrSet<const ValueDecl *, 8> Same;
    if (auto It = Collector.SameData.find(Holder); It != Collector.SameData.end())
      Same.insert(It->second.begin(), It->second.end());
    for (bool bChanged = true; bChanged;) {
      bChanged = false;
      for (const auto &[Decl, Index] : Tracked) {
        const auto *Derived = dyn_cast<VarDecl>(Decl);
        if (!Derived || Same.contains(Derived) || !Derived->getInit() ||
            !Derived->getType()->isReferenceType() || isChangedVariable(Derived))
          continue;
        const Expr *Inner = skipWrappers(Derived->getInit())->IgnoreParenImpCasts();
        for (int Depth = 0; Depth < 8; ++Depth) {
          if (const auto *Unary = dyn_cast<UnaryOperator>(Inner);
              Unary && Unary->getOpcode() == UO_Deref)
            Inner = Unary->getSubExpr()->IgnoreParenImpCasts();
          else if (const auto *Member = dyn_cast<MemberExpr>(Inner);
                   Member && !isa<CXXMethodDecl>(Member->getMemberDecl()))
            Inner = Member->getBase()->IgnoreParenImpCasts();
          else
            break;
        }
        // Of a variable that the function doesn't assign another, `int
        // &Value = *pEntry; pEntry = ...`, outside the callbacks that look it
        // up again, which re-validates only what it holds then.
        const auto *Base = dyn_cast<DeclRefExpr>(Inner);
        const auto *BaseVariable =
            Base ? dyn_cast<VarDecl>(resolveReferenceCapture(Base->getDecl()))
                 : nullptr;
        auto ChangedOutsideLambdas = [&](const VarDecl *Variable) {
          const auto *Function = dyn_cast_or_null<FunctionDecl>(
              Variable->getParentFunctionOrMethod());
          bool Changed = false;
          std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
            if (!S || Changed || isa<LambdaExpr>(S))
              return;
            const Expr *Target = nullptr;
            if (const auto *Assign = dyn_cast<BinaryOperator>(S);
                Assign && Assign->isAssignmentOp())
              Target = Assign->getLHS();
            else if (const auto *Unary = dyn_cast<UnaryOperator>(S);
                     Unary && (Unary->isIncrementDecrementOp() ||
                               Unary->getOpcode() == UO_AddrOf))
              Target = Unary->getSubExpr();
            if (Target)
              for (const DeclRefExpr *Ref : assignedVariables(Target))
                Changed |= Ref->getDecl() == Variable;
            for (const Stmt *Child : childrenOnce(S))
              Visit(Child);
          };
          Visit(Function ? Function->getBody() : nullptr);
          return Changed;
        };
        if (!Base || !Same.contains(resolveReferenceCapture(Base->getDecl())) ||
            (BaseVariable && !BaseVariable->getType()->isReferenceType() &&
             BaseVariable->hasLocalStorage() &&
             ChangedOutsideLambdas(BaseVariable)))
          continue;
        Same.insert(Derived);
        Bits.set(Index);
        bChanged = true;
      }
    }
    HolderIndices[Holder] = HolderVariables.size();
    HolderDecls.push_back(Holder);
    HolderVariables.push_back(std::move(Bits));
    llvm::BitVector ComparedBits(Variables.size());
    if (auto It = Collector.ComparedRevalidations.find(Holder);
        It != Collector.ComparedRevalidations.end())
      for (const ValueDecl *Variable : It->second)
        if (auto Index = Tracked.find(Variable); Index != Tracked.end())
          ComparedBits.set(Index->second);
    HolderComparedVariables.push_back(std::move(ComparedBits));
    // What its callback can remove entries of, which the callbacks
    // registered before it looked up again.
    llvm::BitVector Removed(Variables.size());
    if (auto It = Collector.HolderRemovals.find(Holder);
        It != Collector.HolderRemovals.end())
      for (const auto &[Variable, Index] : Tracked)
        if (Collector.mayRemove(It->second, Variable))
          Removed.set(Index);
    HolderRemovedVariables.push_back(std::move(Removed));
    HolderPhaseMasks.push_back(Collector.HolderPhases.lookup(Holder));
    HolderStartedMasks.push_back(Collector.HolderStarted.lookup(Holder));
  }

  // Which of the variables may have been suspended over since they were
  // looked up, and which registrations may not be alive. A variable is
  // re-validated on resume while any registration for it is alive on every
  // path.
  // Which variables may point into the data of the actor: a pointer
  // assigned other storage, like `pValue = new CValue`, doesn't go stale.
  struct CState {
    llvm::BitVector Stale;
    llvm::BitVector MaybeDead;
    llvm::BitVector Into;
    /// The variables that may have been assigned since they were made, and
    /// may not hold what the guards their lookup needs protect.
    llvm::BitVector Reassigned;
    /// The registrations whose phases may have started since their callback
    /// last ran, when they were registered or on the last resume.
    llvm::BitVector Unchecked;
    /// The variables a test of a removal flag of their entry found there,
    /// which a call that can change the data of the actor after it can
    /// remove again, `if (*pDeleted) co_return 0; mp_Items.f_Clear();`.
    llvm::BitVector FlagValidated;
    /// The scope exits and guards of `fg_AsyncDestroy` whose cleanup may
    /// still run, not cancelled or run early on every path.
    llvm::SmallPtrSet<const VarDecl *, 2> ArmedCleanups;

    bool operator!=(const CState &Other) const {
      return Stale != Other.Stale || MaybeDead != Other.MaybeDead ||
             Into != Other.Into || Reassigned != Other.Reassigned ||
             Unchecked != Other.Unchecked ||
             FlagValidated != Other.FlagValidated ||
             ArmedCleanups != Other.ArmedCleanups;
    }
    void merge(const CState &Other) {
      Stale |= Other.Stale;
      MaybeDead |= Other.MaybeDead;
      Into |= Other.Into;
      Reassigned |= Other.Reassigned;
      Unchecked |= Other.Unchecked;
      FlagValidated |= Other.FlagValidated;
      ArmedCleanups.insert(Other.ArmedCleanups.begin(),
                           Other.ArmedCleanups.end());
    }
  };
  const unsigned nVariables = Variables.size();
  const unsigned nHolders = HolderVariables.size();
  auto EmptyState = [&] {
    return CState{llvm::BitVector(nVariables),
                  llvm::BitVector(nHolders),
                  llvm::BitVector(nVariables),
                  llvm::BitVector(nVariables),
                  llvm::BitVector(nHolders),
                  llvm::BitVector(nVariables),
                  {}};
  };
  // The guards the lookup of a variable needs, while it holds what it was
  // made from.
  auto RequiredAt = [&](const CState &State,
                        unsigned iVariable) -> std::optional<unsigned> {
    if (State.Reassigned.test(iVariable))
      return std::nullopt;
    return Required[iVariable];
  };
  // The phases in \p Started may begin here: what loses entries in them is
  // stale, and the registrations that check them haven't since.
  auto StartPhases = [&](CState &State, unsigned Started) {
    for (unsigned iVariable = 0; iVariable < nVariables; ++iVariable)
      if (const std::optional<unsigned> Needed = RequiredAt(State, iVariable);
          Needed && (*Needed & Started) && State.Into.test(iVariable))
        State.Stale.set(iVariable);
    for (unsigned iHolder = 0; iHolder < nHolders; ++iHolder)
      if (HolderPhaseMasks[iHolder] & Started)
        State.Unchecked.set(iHolder);
  };
  // With \p Revalidations, also not what the callbacks alive re-validate,
  // which their own early uses aren't protected by.
  // With \p Before, only of the guards registered before it, which run
  // before its callback on resume.
  // With \p Compared instead, only what they re-validate by a comparison,
  // which protects early uses too.
  auto Unguarded = [&](const CState &State, bool Revalidations = true,
                       const VarDecl *Before = nullptr, bool Compared = false) {
    llvm::BitVector Bits(nVariables, true);
    // The guards alive together cover the phases of each of them, but not
    // those that a callback registered after a guard starts, after the guard
    // ran.
    const SourceManager &SM = Context.getSourceManager();
    unsigned LivePhases = 0;
    for (unsigned iHolder = 0; iHolder < nHolders; ++iHolder) {
      if (State.MaybeDead.test(iHolder))
        continue;
      if (Before && !SM.isBeforeInTranslationUnit(
                        HolderDecls[iHolder]->getLocation(),
                        Before->getLocation()))
        continue;
      // Not what a callback registered after it and alive, which runs after
      // it on resume, can remove again.
      if (Revalidations || Compared) {
        llvm::BitVector Revalidated = Revalidations
                                          ? HolderVariables[iHolder]
                                          : HolderComparedVariables[iHolder];
        for (unsigned iLater = 0; iLater < nHolders; ++iLater)
          if (iLater != iHolder && !State.MaybeDead.test(iLater) &&
              SM.isBeforeInTranslationUnit(
                  HolderDecls[iHolder]->getLocation(),
                  HolderDecls[iLater]->getLocation()))
            Revalidated.reset(HolderRemovedVariables[iLater]);
        Bits.reset(Revalidated);
      }
      unsigned Guarded = HolderPhaseMasks[iHolder];
      if (!Guarded)
        continue;
      for (unsigned iLater = 0; iLater < nHolders; ++iLater)
        if (!State.MaybeDead.test(iLater) &&
            SM.isBeforeInTranslationUnit(HolderDecls[iHolder]->getLocation(),
                                         HolderDecls[iLater]->getLocation()))
          Guarded &= ~HolderStartedMasks[iLater];
      LivePhases |= Guarded;
      // Each guard covers what loses entries only in its phases.
      for (unsigned iVariable = 0; iVariable < nVariables; ++iVariable)
        if (const std::optional<unsigned> Needed = RequiredAt(State, iVariable);
            Needed && !(*Needed & ~Guarded))
          Bits.reset(iVariable);
    }
    for (unsigned iVariable = 0; iVariable < nVariables; ++iVariable) {
      if (const std::optional<unsigned> Needed = RequiredAt(State, iVariable);
          Needed && *Needed && !(*Needed & ~LivePhases))
        Bits.reset(iVariable);
    }
    return Bits;
  };
  // The state a pointer copies from what it is initialized or assigned with:
  // a tracked pointer passes on whether it points into the data of the actor
  // and whether it is stale, and anything else is a lookup or not.
  auto isRangeIterator = [&](const Expr *E) {
    const auto *Ref = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
    const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    return Variable && Collector.RangeIterators.contains(Variable);
  };
  std::function<void(unsigned, const Expr *, CState &)> CopyFrom =
      [&](unsigned Index, const Expr *E, CState &State) {
    const Expr *Inner = E->IgnoreParenImpCasts();
    if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(Inner))
      Inner = Cleanups->getSubExpr()->IgnoreParenImpCasts();
    // Either value of a conditional, `bFirst ? pValue : pOther`, or the one a
    // constant condition chooses, `false ? pEntry : &g_Value`.
    if (const auto *Conditional = dyn_cast<ConditionalOperator>(Inner)) {
      if (bool Chosen = false;
          !Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(Chosen, Context))
        return CopyFrom(Index,
                        Chosen ? Conditional->getTrueExpr()
                               : Conditional->getFalseExpr(),
                        State);
      CopyFrom(Index, Conditional->getTrueExpr(), State);
      const bool Into = State.Into.test(Index);
      const bool Stale = State.Stale.test(Index);
      CopyFrom(Index, Conditional->getFalseExpr(), State);
      State.Into[Index] = State.Into.test(Index) || Into;
      State.Stale[Index] = State.Stale.test(Index) || Stale;
      return;
    }
    // Through what a tracked pointer points to, like `CValue &Alias =
    // *pValue` or `&pValue->m_Sub`.
    for (int Depth = 0; Depth < 8; ++Depth) {
      if (const auto *Unary = dyn_cast<UnaryOperator>(Inner);
          Unary && (Unary->getOpcode() == UO_Deref ||
                    Unary->getOpcode() == UO_AddrOf))
        Inner = Unary->getSubExpr()->IgnoreParenImpCasts();
      else if (const auto *Member = dyn_cast<MemberExpr>(Inner))
        Inner = Member->getBase()->IgnoreParenImpCasts();
      // What an assignment assigns, `q = (p = ...)`.
      else if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
               Assign && Assign->getOpcode() == BO_Assign)
        Inner = Assign->getRHS()->IgnoreParenImpCasts();
      // The last of a comma, `(fg_Log(), pValue)`, and the value of a
      // statement expression, `({ pValue; })`.
      else if (const auto *Comma = dyn_cast<BinaryOperator>(Inner);
               Comma && Comma->getOpcode() == BO_Comma)
        Inner = Comma->getRHS()->IgnoreParenImpCasts();
      else if (const auto *Statement = dyn_cast<StmtExpr>(Inner);
               Statement && !Statement->getSubStmt()->body_empty() &&
               isa<Expr>(Statement->getSubStmt()->body_back()))
        Inner = cast<Expr>(Statement->getSubStmt()->body_back())
                    ->IgnoreParenImpCasts();
      // Moved, `fg_Move(pValue)`, it is what is moved.
      else if (const auto *Move = dyn_cast<CallExpr>(Inner);
               Move && Move->getNumArgs() == 1 &&
               (isMoveFunction(calleeName(Move)) ||
                isForwardFunction(calleeName(Move))))
        Inner = Move->getArg(0)->IgnoreParenImpCasts();
      // Given back by a function that returns it, `fg_Identity(pValue)`.
      else if (const Expr *Returned =
                   returnedArgument(dyn_cast<CallExpr>(Inner)))
        Inner = Returned->IgnoreParenImpCasts();
      // A cast or an offset of a pointer, `static_cast<int *>(pValue)` or
      // `pValue + 1`, points where it does.
      else if (const auto *Cast = dyn_cast<ExplicitCastExpr>(Inner);
               Cast && (Cast->getType()->isPointerType() || Cast->isGLValue()))
        Inner = Cast->getSubExpr()->IgnoreParenImpCasts();
      else if (const auto *Binary = dyn_cast<BinaryOperator>(Inner);
               Binary && Binary->isAdditiveOp() &&
               Binary->getType()->isPointerType())
        Inner = (Binary->getLHS()->getType()->isPointerType()
                     ? Binary->getLHS()
                     : Binary->getRHS())
                    ->IgnoreParenImpCasts();
      // What a handle refers to, like `*Mapping`, or a copy of the handle.
      else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Inner);
               Operator && Operator->getNumArgs() == 1 &&
               (Operator->getOperator() == OO_Star ||
                Operator->getOperator() == OO_Arrow) &&
               (isReferenceHandle(Operator->getArg(0)->getType()) ||
                isRangeIterator(Operator->getArg(0))))
        Inner = Operator->getArg(0)->IgnoreParenImpCasts();
      else if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Inner);
               Call && isReferenceHandle(Call->getImplicitObjectArgument()
                                             ->IgnoreParenImpCasts()
                                             ->getType()))
        Inner = Call->getImplicitObjectArgument()->IgnoreParenImpCasts();
      else if (const auto *Construct = dyn_cast<CXXConstructExpr>(Inner);
               Construct && Construct->getNumArgs() == 1 &&
               isReferenceHandle(Construct->getType()))
        Inner = Construct->getArg(0)->IgnoreParenImpCasts();
      else
        break;
    }
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Inner)) {
      if (auto It = Tracked.find(resolveReferenceCapture(Ref->getDecl()));
          It != Tracked.end()) {
        State.Into[Index] = State.Into.test(It->second);
        // The entry of a loop is reported where the loop goes on with its
        // iterator.
        State.Stale[Index] =
            State.Stale.test(It->second) &&
            !Collector.RangeIterators.contains(Variables[It->second]);
        return;
      }
    }
    State.Into[Index] = Collector.isLookupIntoActor(E);
    State.Stale.reset(Index);
  };
  llvm::BitVector EntryInto(Variables.size(), true);
  // A parameter points where the caller gave it, not into the data, until it
  // is assigned a lookup.
  for (const auto &[Variable, Index] : Tracked)
    if (isa<ParmVarDecl>(Variable))
      EntryInto.reset(Index);
  if (Lambda) {
    for (const LambdaCapture &Capture : Lambda->captures()) {
      const auto *Variable = Capture.capturesVariable()
                                 ? dyn_cast<VarDecl>(Capture.getCapturedVar())
                                 : nullptr;
      if (!Variable || Variable->isInitCapture() ||
          Capture.getCaptureKind() != LCK_ByCopy ||
          !Variable->getType()->isPointerType())
        continue;
      if (auto It = Tracked.find(Variable); It != Tracked.end())
        EntryInto[It->second] = intoActorAtCapture(Variable, Lambda, Context);
        }
  }
  // A flag copied from the data before a suspension that its destructor sets,
  // like `auto pDeleted = Entry.m_pDeleted`, tested after it by an `if` that
  // leaves, like `if (*pDeleted) co_return …;`, re-validates the data, and
  // the pointers made from it, where the `if` doesn't leave.
  auto BaseOf = [&](const Expr *E) -> const ValueDecl * {
    const Expr *Inner = skipWrappers(E)->IgnoreParenImpCasts();
    for (int Depth = 0; Depth < 8; ++Depth) {
      if (const auto *Unary = dyn_cast<UnaryOperator>(Inner);
          Unary && (Unary->getOpcode() == UO_Deref ||
                    Unary->getOpcode() == UO_AddrOf))
        Inner = Unary->getSubExpr()->IgnoreParenImpCasts();
      else if (const auto *Member = dyn_cast<MemberExpr>(Inner);
               Member && !isa<CXXMethodDecl>(Member->getMemberDecl()))
        Inner = Member->getBase()->IgnoreParenImpCasts();
      // What a handle refers to, `*Mapping` or `Mapping.f_GetResult()`.
      else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Inner);
               Operator && Operator->getNumArgs() == 1 &&
               (Operator->getOperator() == OO_Star ||
                Operator->getOperator() == OO_Arrow) &&
               isReferenceHandle(Operator->getArg(0)->getType()))
        Inner = Operator->getArg(0)->IgnoreParenImpCasts();
      else if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Inner);
               Call && isReferenceHandle(Call->getImplicitObjectArgument()
                                             ->IgnoreParenImpCasts()
                                             ->getType()))
        Inner = Call->getImplicitObjectArgument()->IgnoreParenImpCasts();
      else
        break;
    }
    const auto *Ref = dyn_cast<DeclRefExpr>(Inner);
    return Ref ? resolveReferenceCapture(Ref->getDecl()) : nullptr;
  };
  // Whether \p E reads a pointer of a member of the data, `Entry.m_pValue`,
  // or goes through one, `*Entry.m_pValue` or `Entry.m_pValue->m_Value`: what
  // it points to is elsewhere, which the flag of the data doesn't tell about.
  auto ThroughMemberPointer = [](const Expr *E) {
    auto IsMemberPointer = [](const Expr *Inner) {
      const auto *Member = dyn_cast<MemberExpr>(Inner->IgnoreParenImpCasts());
      return Member && !isa<CXXMethodDecl>(Member->getMemberDecl()) &&
             Member->getType()->isPointerType();
    };
    const Expr *Inner = skipWrappers(E)->IgnoreParenImpCasts();
    if (IsMemberPointer(Inner))
      return true;
    for (int Depth = 0; Depth < 8; ++Depth) {
      if (const auto *Unary = dyn_cast<UnaryOperator>(Inner);
          Unary && (Unary->getOpcode() == UO_Deref ||
                    Unary->getOpcode() == UO_AddrOf)) {
        if (Unary->getOpcode() == UO_Deref &&
            IsMemberPointer(Unary->getSubExpr()))
          return true;
        Inner = Unary->getSubExpr()->IgnoreParenImpCasts();
      } else if (const auto *Member = dyn_cast<MemberExpr>(Inner);
                 Member && !isa<CXXMethodDecl>(Member->getMemberDecl())) {
        if (Member->isArrow() && IsMemberPointer(Member->getBase()))
          return true;
        Inner = Member->getBase()->IgnoreParenImpCasts();
      } else
        break;
    }
    return false;
  };
  // Also out of a loop or to its next round, `if (*pRemoved) break;`, which
  // the false edge still goes past.
  auto Leaves = [](const Stmt *S) {
    if (const auto *Compound = dyn_cast_or_null<CompoundStmt>(S))
      S = Compound->body_empty() ? nullptr : Compound->body_back();
    return isa_and_nonnull<ReturnStmt, CoreturnStmt, BreakStmt, ContinueStmt>(
               S) ||
           (isa_and_nonnull<Expr>(S) &&
            isa<CXXThrowExpr>(cast<Expr>(S)->IgnoreImplicit()));
  };
  // Each check of flags, what it covers, and whether it is negated, `if
  // (!*pDeleted)`, whose true edge is where they are not set.
  llvm::DenseMap<const Stmt *, std::pair<llvm::BitVector, bool>> FlagChecks;
  for (const auto &Nodes : match(findAll(ifStmt().bind("if")), *Body, Context)) {
    const auto *If = Nodes.getNodeAs<IfStmt>("if");
    if (!If->getCond())
      continue;
    const auto *Not =
        dyn_cast<UnaryOperator>(If->getCond()->IgnoreParenImpCasts());
    const bool Negated = Not && Not->getOpcode() == UO_LNot;
    const Expr *Tested = Negated ? Not->getSubExpr() : If->getCond();
    // Also flags read into a local that can't change just before, `bool const
    // bRemoved = *pRemoved; if (bRemoved)`, as read in the condition.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Tested->IgnoreParenImpCasts()))
      if (const auto *Saved = dyn_cast<VarDecl>(Ref->getDecl());
          Saved && Saved->hasLocalStorage() && !isa<ParmVarDecl>(Saved) &&
          Saved->getType()->isBooleanType() && Saved->getInit() &&
          !isChangedVariable(Saved) && onlyReadsFlags(Saved->getInit())) {
        const auto Parents = instantiatedParents(*If, Context);
        const auto *Block =
            Parents.empty() ? nullptr : Parents[0].get<CompoundStmt>();
        const Stmt *Before = nullptr;
        if (Block)
          for (const Stmt *Statement : Block->body()) {
            if (Statement == If)
              break;
            Before = Statement;
          }
        if (const auto *Declaration = dyn_cast_or_null<DeclStmt>(Before);
            Declaration && Declaration->isSingleDecl() &&
            Declaration->getSingleDecl() == Saved)
          Tested = Saved->getInit();
      }
    if ((!Negated && !Leaves(If->getThen())) || !onlyReadsFlags(Tested))
      continue;
    llvm::BitVector Covered(Variables.size());
    // The flags the condition dereferences, `*pDeleted`.
    // Only a flag the condition tests as such, alone or in a disjunction, like
    // `*pDeleted || bOther`: the branch is then taken when it is set, and the
    // false edge is where it is not. A negated flag is the other way around.
    struct CFlags {
      llvm::SmallVector<const VarDecl *, 2> Flags;
      void collect(const Expr *E) {
        E = E->IgnoreParenImpCasts();
        if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(E))
          return collect(Cleanups->getSubExpr());
        if (const auto *Binary = dyn_cast<BinaryOperator>(E)) {
          if (Binary->getOpcode() == BO_LOr) {
            collect(Binary->getLHS());
            collect(Binary->getRHS());
          }
          // Compared to be set, like `*pDeleted == true`.
          if (const Expr *Flag = comparedToBeSet(Binary))
            collect(Flag);
          return;
        }
        const Expr *Operand = nullptr;
        if (const auto *Unary = dyn_cast<UnaryOperator>(E);
            Unary && Unary->getOpcode() == UO_Deref)
          Operand = Unary->getSubExpr();
        else if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
                 Call && Call->getOperator() == OO_Star &&
                 Call->getNumArgs() == 1)
          Operand = Call->getArg(0);
        // Also an atomic flag loaded, `pDeleted->f_Load()`.
        else if (const auto *Load = dyn_cast<CXXMemberCallExpr>(E);
                 Load && Load->getMethodDecl() &&
                 Load->getMethodDecl()->getIdentifier() &&
                 (Load->getMethodDecl()->getName() == "f_Load" ||
                  Load->getMethodDecl()->getName() == "load")) {
          Operand = Load->getImplicitObjectArgument()->IgnoreParenImpCasts();
          if (const auto *Arrow = dyn_cast<CXXOperatorCallExpr>(Operand);
              Arrow && Arrow->getOperator() == OO_Arrow &&
              Arrow->getNumArgs() == 1)
            Operand = Arrow->getArg(0);
          else if (const auto *Deref = dyn_cast<UnaryOperator>(Operand);
                   Deref && Deref->getOpcode() == UO_Deref)
            Operand = Deref->getSubExpr();
        }
        if (!Operand)
          return;
        if (const auto *Ref =
                dyn_cast<DeclRefExpr>(Operand->IgnoreParenImpCasts()))
          if (const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl()))
            Flags.push_back(Variable);
      }
    };
    CFlags FlagFinder;
    FlagFinder.collect(Tested);
    for (const VarDecl *Variable : FlagFinder.Flags) {
      // A flag assigned something else may not be the entry's any more.
      if (!Variable->hasLocalStorage() || !Variable->getInit() ||
          isChangedVariable(Variable))
        continue;
      // A copy of a member of the data that tells it was removed, like
      // `m_pDestroyed`.
      const auto *Member = dyn_cast<MemberExpr>(
          skipWrappers(Variable->getInit())->IgnoreParenImpCasts());
      if (Member && !isRemovalFlagName(Member->getMemberDecl()))
        continue;
      const ValueDecl *Data = Member ? BaseOf(Member->getBase()) : nullptr;
      if (!Data)
        continue;
      // The data and what is made from it, or what it is made from.
      for (const auto &[Decl, Index] : Tracked) {
        const auto *Tracked = dyn_cast<VarDecl>(Decl);
        // What the flag was copied from must still be that entry.
        const auto *DataVariable = dyn_cast<VarDecl>(Data);
        if (DataVariable && isChangedVariable(DataVariable) &&
            !DataVariable->getType()->isReferenceType())
          continue;
        if (Tracked == Data ||
            (Tracked && Tracked->getInit() &&
             BaseOf(Tracked->getInit()) == Data &&
             !ThroughMemberPointer(Tracked->getInit()) &&
             !isChangedVariable(Tracked)))
          Covered.set(Index);
      }
    }
    if (Covered.any())
      FlagChecks[If] = {std::move(Covered), Negated};
  }

  // Testing whether a pointer is null, or comparing it, doesn't use the data.
  // Neither does asking a handle something else than what it refers to, like
  // `Mapping.f_WasCreated()`, nor naming a static member through it,
  // `Entry.ms_Count` or `pEntry->f_Count()` of a static function.
  auto isPointerTest = [&](const DeclRefExpr *Ref) {
    {
      auto Parents = instantiatedParents(*Ref, Context);
      while (!Parents.empty() && (Parents[0].get<ImplicitCastExpr>() ||
                                  Parents[0].get<ParenExpr>()))
        Parents = instantiatedParents(Parents[0], Context);
      if (const auto *Member =
              Parents.empty() ? nullptr : Parents[0].get<MemberExpr>()) {
        const ValueDecl *Named = Member->getMemberDecl();
        const auto *Method = dyn_cast<CXXMethodDecl>(Named);
        if (isa<VarDecl>(Named) || (Method && Method->isStatic()))
          return true;
      }
    }
    if (isReferenceHandle(Ref->getType())) {
      auto Parents = instantiatedParents(*Ref, Context);
      while (!Parents.empty() && (Parents[0].get<ImplicitCastExpr>() ||
                                  Parents[0].get<ParenExpr>()))
        Parents = instantiatedParents(Parents[0], Context);
      if (Parents.empty())
        return false;
      // What the handle knows itself, like whether the lookup added the
      // entry; another member function may read the entry.
      if (const auto *Member = Parents[0].get<MemberExpr>()) {
        const auto *Method = dyn_cast<CXXMethodDecl>(Member->getMemberDecl());
        return Method && Method->getIdentifier() &&
               Method->getName() == "f_WasCreated";
      }
      return false;
    }
    if (!Ref->getType()->isPointerType())
      return false;
    // Not through a reference to a pointer in the data, `auto const &pEntry =
    // mp_Items[0]`, whose value is read from the entry, unlike one bound to a
    // local pointer.
    if (Ref->getDecl()->getType()->isReferenceType() &&
        resolveReferenceCapture(Ref->getDecl()) == Ref->getDecl())
      return false;
    DynTypedNode Node = DynTypedNode::create(*Ref);
    for (int Depth = 0; Depth < 4; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return false;
      // Also explicitly, `bool(pEntry)`, or kept, `bool bFound = pEntry;`,
      // where the parent map skips the conversion to bool.
      if (const auto *Test = Parents[0].get<ExplicitCastExpr>();
          Test && isPointerToBool(Test))
        return true;
      if (const auto *Kept = Parents[0].get<VarDecl>();
          Kept && Kept->getType()->isBooleanType() && Kept->getInit() &&
          Kept->getInit()->IgnoreParenImpCasts() ==
              Node.get<Expr>()->IgnoreParenImpCasts())
        return true;
      if (const auto *Cast = Parents[0].get<ImplicitCastExpr>()) {
        if (Cast->getCastKind() == CK_PointerToBoolean)
          return true;
        Node = Parents[0];
        continue;
      }
      if (Parents[0].get<ParenExpr>()) {
        Node = Parents[0];
        continue;
      }
      if (const auto *Unary = Parents[0].get<UnaryOperator>())
        return Unary->getOpcode() == UO_LNot;
      if (const auto *Binary = Parents[0].get<BinaryOperator>())
        return Binary->isComparisonOp() || Binary->isLogicalOp();
      // The condition itself, where the parent map skips the conversion to
      // bool.
      const Expr *Tested = Node.get<Expr>();
      auto IsCondition = [&](const Expr *Condition) {
        return Condition && Tested &&
               Condition->IgnoreParenImpCasts() == Tested->IgnoreParenImpCasts();
      };
      if (const auto *If = Parents[0].get<IfStmt>())
        return IsCondition(If->getCond());
      if (const auto *While = Parents[0].get<WhileStmt>())
        return IsCondition(While->getCond());
      if (const auto *Do = Parents[0].get<DoStmt>())
        return IsCondition(Do->getCond());
      if (const auto *For = Parents[0].get<ForStmt>())
        return IsCondition(For->getCond());
      if (const auto *Conditional = Parents[0].get<AbstractConditionalOperator>())
        return IsCondition(Conditional->getCond());
      return false;
    }
    return false;
  };

  llvm::DenseMap<const CFGBlock *, CState> StateAtEnd;
  // Whether \p Flag is a flag of the entry \p Captured points to that tells
  // it was removed, copied before, like `pDeleted = pEntry->m_pDeleted`.
  auto IsRemovalFlagOf = [&](const ValueDecl *Flag, const ValueDecl *Captured) {
    const auto *Copy = dyn_cast_or_null<VarDecl>(Flag);
    // A copy of the flag that is replaced, `pDeleted = pOther`, is another
    // flag.
    if (!Copy || !Copy->getInit() || isChangedVariable(Copy))
      return false;
    const auto *Member =
        dyn_cast<MemberExpr>(skipWrappers(Copy->getInit())->IgnoreParenImpCasts());
    if (!Member || !Member->getMemberDecl()->getIdentifier())
      return false;
    const StringRef Name = Member->getMemberDecl()->getName();
    const auto *Base =
        dyn_cast<DeclRefExpr>(Member->getBase()->IgnoreParenImpCasts());
    // Of the entry it still points to: one pointed at another entry after
    // the flag was copied, `pEntry = mp_Items.f_FindEqual(2)`, isn't told by
    // the flag of the first, but one pointed before, `pEntry = &Entry;`
    // after `CEntry *pEntry;`, is.
    auto AssignedAfterCopy = [&](const VarDecl *Pointer) {
      if (!isChangedVariable(Pointer))
        return false;
      const auto *Function =
          dyn_cast_or_null<FunctionDecl>(Pointer->getParentFunctionOrMethod());
      if (!Function || !Function->getBody())
        return true;
      const SourceManager &SM = Context.getSourceManager();
      bool After = false;
      bool Other = false;
      for (const auto &Nodes : match(
               findAll(declRefExpr(to(varDecl(equalsNode(Pointer))))
                           .bind("ref")),
               *Function->getBody(), Context)) {
        const auto *Ref = Nodes.getNodeAs<DeclRefExpr>("ref");
        DynTypedNode Node = DynTypedNode::create(*Ref);
        auto Parents = instantiatedParents(Node, Context);
        while (!Parents.empty() && (Parents[0].get<ParenExpr>() ||
                                    (Parents[0].get<ImplicitCastExpr>() &&
                                     Parents[0].get<ImplicitCastExpr>()
                                             ->getCastKind() != CK_LValueToRValue))) {
          Node = Parents[0];
          Parents = instantiatedParents(Node, Context);
        }
        if (Parents.empty())
          continue;
        // Assigned, before or after the copy of the flag; read, which
        // changes nothing; or changed otherwise, by its address, a step or
        // what gets it by reference.
        if (const auto *Assign = Parents[0].get<BinaryOperator>();
            Assign && Assign->isAssignmentOp() &&
            Assign->getLHS()->IgnoreParenImpCasts() == Ref) {
          if (Assign->getOpcode() == BO_Assign)
            After |= !SM.isBeforeInTranslationUnit(Assign->getEndLoc(),
                                                   Base->getBeginLoc());
          else
            Other = true;
        } else if (const auto *Unary = Parents[0].get<UnaryOperator>();
                   Unary && (Unary->getOpcode() == UO_AddrOf ||
                             Unary->isIncrementDecrementOp()))
          Other = true;
        else if (Parents[0].get<CallExpr>() || Parents[0].get<CXXConstructExpr>())
          Other = true;
      }
      return After || Other;
    };
    const auto *Pointer = dyn_cast<VarDecl>(Captured);
    return Base && Base->getDecl() == Captured &&
           (!Pointer || Pointer->getType()->isReferenceType() ||
            !AssignedAfterCopy(Pointer)) &&
           (Name.contains("Destroyed") || Name.contains("Deleted") ||
            Name.contains("Removed"));
  };
  // Whether \p Condition, when \p Set, tells that a removal flag of
  // \p Captured is set: `*pDeleted`, `pDeleted->f_Load()` or `*pDeleted ==
  // true`, or, unless Set, `!*pDeleted`.
  std::function<bool(const Expr *, const ValueDecl *, bool)> TestsRemoval =
      [&](const Expr *Condition, const ValueDecl *Captured, bool Set) {
        Condition = skipWrappers(Condition)->IgnoreParenImpCasts();
        if (const auto *Not = dyn_cast<UnaryOperator>(Condition);
            Not && Not->getOpcode() == UO_LNot)
          return TestsRemoval(Not->getSubExpr(), Captured, !Set);
        // Either side of `*pDeleted || bSkip` is true when the flag is set,
        // and each side of `!*pDeleted && bGo` when it isn't; the other way
        // around, both sides must tell.
        if (const auto *Logical = dyn_cast<BinaryOperator>(Condition);
            Logical && Logical->isLogicalOp()) {
          const bool Left = TestsRemoval(Logical->getLHS(), Captured, Set);
          const bool Right = TestsRemoval(Logical->getRHS(), Captured, Set);
          return (Logical->getOpcode() == BO_LOr) == Set ? Left || Right
                                                         : Left && Right;
        }
        if (!Set)
          return false;
        if (const auto *Binary = dyn_cast<BinaryOperator>(Condition)) {
          if (const Expr *Flag = comparedToBeSet(Binary))
            return TestsRemoval(Flag, Captured, true);
          return false;
        }
        const Expr *Operand = nullptr;
        if (const auto *Deref = dyn_cast<UnaryOperator>(Condition);
            Deref && Deref->getOpcode() == UO_Deref)
          Operand = Deref->getSubExpr();
        else if (const auto *Star = dyn_cast<CXXOperatorCallExpr>(Condition);
                 Star && Star->getOperator() == OO_Star &&
                 Star->getNumArgs() == 1)
          Operand = Star->getArg(0);
        else if (const auto *Load = dyn_cast<CXXMemberCallExpr>(Condition);
                 Load && Load->getMethodDecl() &&
                 Load->getMethodDecl()->getIdentifier() &&
                 (Load->getMethodDecl()->getName() == "f_Load" ||
                  Load->getMethodDecl()->getName() == "load")) {
          Operand = Load->getImplicitObjectArgument()->IgnoreParenImpCasts();
          if (const auto *Arrow = dyn_cast<CXXOperatorCallExpr>(Operand);
              Arrow && Arrow->getOperator() == OO_Arrow &&
              Arrow->getNumArgs() == 1)
            Operand = Arrow->getArg(0);
          else if (const auto *Deref = dyn_cast<UnaryOperator>(Operand);
                   Deref && Deref->getOpcode() == UO_Deref)
            Operand = Deref->getSubExpr();
        }
        const auto *Ref =
            Operand ? dyn_cast<DeclRefExpr>(Operand->IgnoreParenImpCasts())
                    : nullptr;
        return Ref && IsRemovalFlagOf(Ref->getDecl(), Captured);
      };
  // Whether \p Ref, in the body of \p Invoked, only runs when the entry of
  // \p Captured is still there: in the branch of `if (!*pDeleted)`, or after
  // `if (*pDeleted) return;` in the body.
  auto IsGuardedByFlag = [&](const DeclRefExpr *Ref, const LambdaExpr *Invoked,
                             const ValueDecl *Captured) {
    // A jump can pass the test, `goto Use;`.
    if (hasJumps(Invoked->getBody()))
      return false;
    const SourceManager &SM = Context.getSourceManager();
    // Nor can what runs between the test and the use call what can remove
    // the entry, `if (*pDeleted) return 0; mp_Items.f_Clear();`: a call that
    // uses the actor, other than a const member function of it, or one the use
    // is in, `mp_Items.f_Remove(*pEntry)`, which runs after it.
    auto ChangesBetween = [&](SourceLocation Tested) {
      bool Changes = false;
      std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
        if (!S || Changes || isa<LambdaExpr>(S))
          return;
        if (const auto *Call = dyn_cast<CallExpr>(S);
            Call && SM.isBeforeInTranslationUnit(Tested, Call->getBeginLoc()) &&
            SM.isBeforeInTranslationUnit(Call->getBeginLoc(),
                                         Ref->getBeginLoc()) &&
            SM.isBeforeInTranslationUnit(Call->getEndLoc(),
                                         Ref->getBeginLoc())) {
          // A const one, unless its class has mutable members or its body
          // can be seen to free, like the calls of the body do.
          const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
          const CXXMethodDecl *Method =
              Member ? Member->getMethodDecl() : nullptr;
          const bool Const = Method && Method->isConst() &&
                             !Method->getParent()->hasMutableFields() &&
                             !(Method->getDefinition() && mayFree(Method));
          // Also through what refers into the data, `Items.f_Clear()` of a
          // captured `auto &Items = mp_Items`.
          auto Uses = [&](const Expr *E) {
            return usesThis(E) || Collector.isRooted(E);
          };
          if (Const ? llvm::any_of(Call->arguments(), Uses)
                    : Uses(Call) ||
                          (Member && Member->getImplicitObjectArgument() &&
                           Uses(Member->getImplicitObjectArgument())) ||
                          llvm::any_of(Call->arguments(), Uses)) {
            Changes = true;
            return;
          }
        }
        for (const Stmt *Child : evaluatedChildren(S, Context))
          Visit(Child);
      };
      Visit(Invoked->getBody());
      return Changes;
    };
    if (const auto *Body = dyn_cast<CompoundStmt>(Invoked->getBody()))
      for (const Stmt *Statement : Body->body()) {
        if (!SM.isBeforeInTranslationUnit(Statement->getEndLoc(),
                                          Ref->getBeginLoc()))
          break;
        const auto *If = dyn_cast<IfStmt>(Statement);
        const Stmt *Then = If ? If->getThen() : nullptr;
        if (const auto *Block = dyn_cast_or_null<CompoundStmt>(Then);
            Block && Block->size() == 1)
          Then = Block->body_front();
        if (If && !If->getInit() && !If->getElse() &&
            isa_and_nonnull<ReturnStmt>(Then) &&
            TestsRemoval(If->getCond(), Captured, true) &&
            onlyReadsFlags(If->getCond()) &&
            !ChangesBetween(If->getCond()->getBeginLoc()))
          return true;
      }
    DynTypedNode Node = DynTypedNode::create(*Ref);
    for (int Depth = 0; Depth < 64; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty() || Parents[0].get<LambdaExpr>())
        return false;
      // In the branch of `if (!*pDeleted)`, or the other branch of `if
      // (*pDeleted)`.
      if (const auto *If = Parents[0].get<IfStmt>();
          If && !If->getInit() && !If->getConditionVariable() &&
          ((Node.get<Stmt>() == If->getThen() &&
            TestsRemoval(If->getCond(), Captured, false)) ||
           (Node.get<Stmt>() == If->getElse() &&
            TestsRemoval(If->getCond(), Captured, true))) &&
          onlyReadsFlags(If->getCond()) &&
          !ChangesBetween(If->getCond()->getBeginLoc()))
        return true;
      Node = Parents[0];
    }
    return false;
  };
  // Whether \p Ref is what an assignment assigns, `pEntry = nullptr`.
  auto isAssignedAt = [&](const DeclRefExpr *Ref) {
    DynTypedNode Node = DynTypedNode::create(*Ref);
    for (int Depth = 0; Depth < 4; ++Depth) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        return false;
      if (const auto *Assign = Parents[0].get<BinaryOperator>())
        return Assign->getOpcode() == BO_Assign &&
               Assign->getLHS()->IgnoreParens() == Node.get<Expr>();
      if (!Parents[0].get<ParenExpr>())
        return false;
      Node = Parents[0];
    }
    return false;
  };
  // What a lambda that runs here uses of what it captures: a pointer it only
  // tests, like `[pEntry] { return pEntry != nullptr; }`, isn't a use, and
  // neither is one after it tested that the entry is still there.
  auto UsesCaptured = [&](const LambdaExpr *Invoked, const ValueDecl *Captured) {
    bool Uses = false;
    std::function<void(const Stmt *)> Visit = [&](const Stmt *Inner) {
      if (!Inner || Uses)
        return;
      // Not what isn't evaluated, `sizeof(*pEntry)`, nor a branch of an `if
      // constexpr` that is discarded.
      if (isa<UnaryExprOrTypeTraitExpr, CXXNoexceptExpr>(Inner))
        return;
      if (const auto *Typeid = dyn_cast<CXXTypeidExpr>(Inner);
          Typeid && !Typeid->isPotentiallyEvaluated())
        return;
      // Making a lambda inside runs only what initializes its copies; what
      // its body does is of the lambdas that are run, which are told by
      // themselves.
      if (const auto *Nested = dyn_cast<LambdaExpr>(Inner)) {
        auto Init = Nested->capture_init_begin();
        for (const LambdaCapture &Capture : Nested->captures()) {
          const Expr *CaptureInit = *Init++;
          if (Capture.getCaptureKind() == LCK_ByCopy)
            Visit(CaptureInit);
        }
        return;
      }
      // Nor assigning it, `pEntry = nullptr`, which changes the variable,
      // not the data, but a reference, `Entry = 1` of `int &Entry`, writes
      // what it refers to.
      // Nor one the lambda looks up again first, `[this, pEntry]() mutable {
      // pEntry = mp_Items.f_FindEqual(1); return pEntry->m_Value; }`, a
      // value of its own, not a copy of another variable.
      const bool Reference = Captured->getType()->isReferenceType();
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Inner);
          Ref && Ref->getDecl() == Captured && !isPointerTest(Ref) &&
          (Reference || !isAssignedAt(Ref)) &&
          !IsGuardedByFlag(Ref, Invoked, Captured) &&
          (Reference ||
           !reassignedBefore(Invoked->getBody(), Captured, Ref,
                             Context.getSourceManager(),
                             [](const Expr *Value) {
                               return !isa<DeclRefExpr>(
                                   Value->IgnoreParenImpCasts());
                             })))
        Uses = true;
      for (const Stmt *Child : evaluatedChildren(Inner, Context))
        Visit(Child);
    };
    Visit(Invoked->getBody());
    return Uses;
  };
  // The lambdas that running \p Invoked runs: itself and the ones it calls
  // in turn, like `fRead()` in `[&fRead] { return fRead(); }`, however many
  // there are, in the order they run: what each reads where it starts, the
  // ones it calls in the order it calls them, each with what it calls, and
  // what it assigns where it ends, so that what it assigns isn't taken to be
  // before what those it calls read.
  // Each with whether it runs for sure when \p Invoked does: called in a
  // statement of the body of the one that calls it that runs each time it
  // does, not in a branch, a loop or a condition, `if (bRefresh) fRefresh()`.
  // Each once, or twice when it is reached first where it may not run and
  // then where it runs for sure, `if (bRefresh) fRefresh(); fRefresh();`.
  struct CRun {
    const LambdaExpr *Lambda;
    bool Sure;
    bool Assigns;
    // Of what it assigns, those written from and before these, when valid,
    // which run between the lambdas it calls.
    SourceLocation From;
    SourceLocation To;
  };
  auto RunLambdas = [&](const LambdaExpr *Invoked, bool Sure) {
    const SourceManager &SM = Context.getSourceManager();
    llvm::SmallVector<CRun, 8> Run;
    // Each time it is called, `fRefresh(); p = &Local; fRefresh();`, but not
    // one in itself, nor more times than code can have.
    // Past as many as code can have, each that is left once, as one that
    // may run, so what it reads is still looked at.
    llvm::SmallPtrSet<const LambdaExpr *, 4> InProgress;
    llvm::SmallPtrSet<const LambdaExpr *, 4> Overflowed;
    unsigned nAdded = 0;
    std::function<void(const LambdaExpr *, bool, int)> Add =
        [&](const LambdaExpr *Runner, bool RunnerSure, int Depth) {
          const bool Over = Depth > 64 || ++nAdded > 1024;
          if (Over ? !Overflowed.insert(Runner).second
                   : !InProgress.insert(Runner).second)
            return;
          if (Over)
            RunnerSure = false;
          const llvm::scope_exit Done([&] {
            if (!Over)
              InProgress.erase(Runner);
          });
          Run.push_back({Runner, RunnerSure, false, {}, {}});
          const llvm::SmallPtrSet<const Stmt *, 8> Always =
              statementsBeforeLeaving(Runner->getBody());
          // What it assigns, when that is all in statements of its body that
          // run each time, before a lambda it calls, `p = mp_Items.f_FindEqual(
          // 1); return fRead();`, is assigned before that one runs, and what
          // it assigns after, `p = nullptr;`, after.
          llvm::SmallVector<SourceLocation, 4> Assignments;
          bool AssignsInBranch = false;
          // Of a coroutine, the statements of its body.
          const Stmt *RunnerBody = Runner->getBody();
          if (const auto *Coroutine =
                  dyn_cast_or_null<CoroutineBodyStmt>(RunnerBody))
            RunnerBody = Coroutine->getBody();
          if (const auto *Body = dyn_cast_or_null<CompoundStmt>(RunnerBody))
            for (const Stmt *Statement : Body->body()) {
              std::function<void(const Stmt *)> Find = [&](const Stmt *Inner) {
                if (!Inner || isa<LambdaExpr>(Inner))
                  return;
                if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
                    Assign && Assign->isAssignmentOp()) {
                  Assignments.push_back(Assign->getBeginLoc());
                  if (!Always.contains(Statement))
                    AssignsInBranch = true;
                }
                for (const Stmt *Child : childrenOnce(Inner))
                  Find(Child);
              };
              Find(Statement);
            }
          // Where the assignments not yet applied start.
          SourceLocation AssignedUntil;
          auto AssignsBefore = [&](SourceLocation To) {
            return llvm::any_of(Assignments, [&](SourceLocation Loc) {
              return (AssignedUntil.isInvalid() ||
                      !SM.isBeforeInTranslationUnit(Loc, AssignedUntil)) &&
                     (To.isInvalid() || SM.isBeforeInTranslationUnit(Loc, To));
            });
          };
          std::function<void(const Stmt *, bool)> Visit =
              [&](const Stmt *Inner, bool Sure) {
                if (!Inner || isa<LambdaExpr>(Inner))
                  return;
                if (isa<IfStmt, ForStmt, WhileStmt, DoStmt, CXXForRangeStmt,
                        SwitchStmt, AbstractConditionalOperator, CXXTryStmt>(
                        Inner))
                  Sure = false;
                if (const auto *Logical = dyn_cast<BinaryOperator>(Inner);
                    Logical && Logical->isLogicalOp())
                  Sure = false;
                // What it is given runs before it.
                for (const Stmt *Child : childrenOnce(Inner))
                  Visit(Child, Sure);
                const LambdaExpr *Nested = nullptr;
                if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Inner);
                    Operator && Operator->getOperator() == OO_Call &&
                    Operator->getNumArgs() >= 1)
                  Nested = asLambda(Operator->getArg(0));
                else if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Inner);
                         Member && Member->getMethodDecl() &&
                         Member->getMethodDecl()->getOverloadedOperator() ==
                             OO_Call)
                  Nested = asLambda(Member->getImplicitObjectArgument());
                else if (const auto *Call = dyn_cast<CallExpr>(Inner);
                         Call && Call->getNumArgs() >= 1 &&
                         (calleeName(Call) == "fg_CallSafe" ||
                          isStdInvoke(Call)))
                  Nested = asLambda(Call->getArg(0));
                if (Nested) {
                  if (!AssignsInBranch && AssignsBefore(Inner->getBeginLoc())) {
                    Run.push_back({Runner, RunnerSure, true, AssignedUntil,
                                   Inner->getBeginLoc()});
                    AssignedUntil = Inner->getBeginLoc();
                  }
                  Add(Nested, Sure, Depth + 1);
                }
              };
          if (const auto *Body = dyn_cast_or_null<CompoundStmt>(RunnerBody))
            for (const Stmt *Statement : Body->body())
              Visit(Statement, RunnerSure && Always.contains(Statement));
          else
            Visit(Runner->getBody(), false);
          if (AssignsInBranch)
            Run.push_back({Runner, RunnerSure, true, {}, {}});
          else if (AssignsBefore({}))
            Run.push_back({Runner, RunnerSure, true, AssignedUntil, {}});
        };
    Add(Invoked, Sure, 0);
    return Run;
  };
  // With \p Sure, when it runs for sure, and with \p CallsLeave, where what
  // its calls throw can be caught, `try { fRefresh(); } catch (...) {}`.
  // What \p Invoked reads, or with \p AssignsPart, what it assigns.
  auto InvokeOne = [&](const LambdaExpr *Invoked, bool Sure, bool CallsLeave,
                       bool AssignsPart, SourceLocation From,
                       SourceLocation To, auto &State, auto &&OnUse) {
    if (!AssignsPart)
    for (const LambdaCapture &Capture : Invoked->captures()) {
      const auto *Variable = Capture.capturesVariable()
                                 ? dyn_cast<VarDecl>(Capture.getCapturedVar())
                                 : nullptr;
      if (!Variable || !Variable->isInitCapture())
        continue;
      if (auto Copy = LambdaCopies.find({Invoked, Variable});
          Copy != LambdaCopies.end() && State.Stale.test(Copy->second) &&
          UsesCaptured(Invoked, Variable))
        OnUse(Capture.getLocation(), Variable);
    }
    if (!AssignsPart)
    for (const Expr *Init : Invoked->capture_inits()) {
      const DeclRefExpr *Captured = capturedVariableRef(Init);
      if (!Captured)
        continue;
      const ValueDecl *Source =
          resolveReferenceCapture(Captured->getDecl());
      auto It = Tracked.find(Source);
      if (It == Tracked.end())
        continue;
      unsigned Index = It->second;
      if (auto Copy = LambdaCopies.find({Invoked, Source});
          Copy != LambdaCopies.end())
        Index = Copy->second;
      if (State.Stale.test(Index) &&
          UsesCaptured(Invoked, Captured->getDecl()))
        OnUse(Captured->getBeginLoc(), Variables[Index]);
    }
    if (!AssignsPart)
      return;
    // What it assigns to what it captures by reference, `[&pEntry] { pEntry
    // = mp_Items.f_FindEqual(0); }`, is what the variable holds after: for
    // sure when a statement of its body before anything that can return
    // does, or else maybe.
    const llvm::SmallPtrSet<const Stmt *, 8> Always =
        Sure ? statementsBeforeLeaving(Invoked->getBody(), CallsLeave)
             : llvm::SmallPtrSet<const Stmt *, 8>();
    std::function<void(const Stmt *)> Assigns = [&](const Stmt *Inner) {
      if (!Inner || isa<LambdaExpr>(Inner))
        return;
      const SourceManager &SM = Context.getSourceManager();
      if (const auto *Assign = dyn_cast<BinaryOperator>(Inner);
          Assign && Assign->getOpcode() == BO_Assign &&
          (From.isInvalid() ||
           !SM.isBeforeInTranslationUnit(Assign->getBeginLoc(), From)) &&
          (To.isInvalid() ||
           SM.isBeforeInTranslationUnit(Assign->getBeginLoc(), To))) {
        const llvm::SmallVector<const DeclRefExpr *, 2> Targets =
            assignedVariables(Assign->getLHS());
        for (const DeclRefExpr *Ref : Targets) {
          // Not a copy that a lambda around it captures, `[p] { [&p] { p =
          // nullptr; }(); }`.
          const bool ByReference =
              capturedByReferenceThroughout(Invoked, Ref->getDecl(), Context);
          auto It = Tracked.find(resolveReferenceCapture(Ref->getDecl()));
          if (!ByReference || It == Tracked.end())
            continue;
          const bool Stale = State.Stale.test(It->second);
          const bool Into = State.Into.test(It->second);
          CopyFrom(It->second, Assign->getRHS(), State);
          State.Reassigned.set(It->second);
          if (Targets.size() != 1 || !Always.contains(Assign)) {
            if (Stale)
              State.Stale.set(It->second);
            if (Into)
              State.Into.set(It->second);
          }
        }
      }
      for (const Stmt *Child : evaluatedChildren(Inner, Context))
        Assigns(Child);
    };
    Assigns(Invoked->getBody());
  };
  // Where \p CallSite runs it, in a `try` that can catch what it throws.
  // With \p Later, it only runs later, `g_Dispatch / [&pEntry] { pEntry =
  // ...; }`, with what it captures now, so what it assigns isn't what the
  // variables hold after.
  // One that runs later, like a dispatched one, `g_Dispatch / [pValue] {
  // ... }`, runs after other calls of the actor, which can remove what it
  // reads, as after a suspension, and changes nothing here.
  // With \p MayNotRun, it may not run, like one given to a function that
  // calls it, `fg_Read([pEntry] { ... })`, so what it assigns may stay.
  auto Invoke = [&](const LambdaExpr *Invoked, auto &State, auto &&OnUse,
                    const Stmt *CallSite = nullptr, bool Later = false,
                    bool MayNotRun = false) {
    const bool CallsLeave = CallSite && insideTryBlock(CallSite, Context);
    std::optional<CState> LaterState;
    if (Later) {
      LaterState = State;
      llvm::BitVector Suspended = Unguarded(*LaterState);
      Suspended &= LaterState->Into;
      llvm::BitVector Kept = Protected;
      Kept.reset(LaterState->Reassigned);
      Suspended.reset(Kept);
      LaterState->Stale |= Suspended;
    }
    for (const CRun &Run : RunLambdas(Invoked, !MayNotRun))
      if (!Later || !Run.Assigns)
        InvokeOne(Run.Lambda, Run.Sure, CallsLeave, Run.Assigns, Run.From,
                  Run.To, LaterState ? *LaterState : State, OnUse);
  };

  // Whether the function each call calls calls an argument, by its index,
  // looked into once.
  llvm::DenseMap<std::pair<const CallExpr *, unsigned>, bool> CallsArgument;
  auto CachedCallsArgument = [&](const CallExpr *Call, unsigned Index) {
    auto [It, Inserted] = CallsArgument.try_emplace({Call, Index}, false);
    if (Inserted)
      It->second = callsArgument(Call, Index);
    return It->second;
  };

  // The phases that the cleanups of each variable, and its destructor,
  // start where it goes.
  llvm::DenseMap<const VarDecl *, std::pair<unsigned, unsigned>>
      DestroyedPhases;
  llvm::DenseMap<const CXXDestructorDecl *, unsigned> TemporaryPhases;
  // The scope exits that are discarded temporaries, `(void)(g_OnScopeExit /
  // [pEntry] { ... })`, by the end of the full expression they are made in,
  // where they run their lambdas, also after what it awaits after them.
  llvm::DenseMap<const Stmt *, llvm::SmallVector<const LambdaExpr *, 1>>
      ExitsAtEnd;
  for (const CFGBlock *Block : *Graph)
    if (Reachable.test(Block->getBlockID()))
      for (const CFGElement &Element : *Block)
        if (const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>())
          if (const auto *Cleanup =
                  dyn_cast<LambdaExpr>(Statement->getStmt());
              Cleanup && isTemporaryScopeExitLambda(Cleanup, Context))
            ExitsAtEnd[fullExpressionOf(Cleanup, Context)].push_back(Cleanup);
  llvm::DenseMap<const VarDecl *, bool> HolderDestroysData;
  auto Transfer = [&](const CFGBlock *Block, CState State, auto &&OnUse) {
    // Given by a reference that isn't to const, which it can change
    // through, like `void fg_Erase(CMap &_Items) { _Items.f_Remove(0); }`,
    // whatever it assigns.
    auto GivesToChange = [&](const Expr *Argument, QualType Given,
                             llvm::function_ref<bool()> MayAssign) {
      const QualType Parameter = Given.getNonReferenceType();
      const bool Changeable =
          (Given.isNull()
               ? MayAssign()
               : Given->isReferenceType() && !Parameter.isConstQualified()) ||
          (Argument->getType()->isPointerType() &&
           !Argument->getType()->getPointeeType().isConstQualified() &&
           (Parameter.isNull() || !Parameter->isPointerType() ||
            !Parameter->getPointeeType().isConstQualified()));
      return Changeable && (usesThis(Argument) || Collector.isRooted(Argument));
    };
    // A local made with the data to change, `CErase Erase{mp_Items}` or
    // `CReset Reset(mp_Items)`, whose destructor can free, `~CErase() {
    // m_Items.f_Clear(); }`, where it is destroyed. Not a copy or a move.
    auto DestroysData = [&](const VarDecl *Holder) {
      auto [It, Inserted] = HolderDestroysData.try_emplace(Holder, false);
      if (!Inserted)
        return It->second;
      const CXXRecordDecl *Record = Holder->getType()->getAsCXXRecordDecl();
      const CXXDestructorDecl *Destructor =
          Record && Record->hasDefinition() ? Record->getDestructor() : nullptr;
      if (!Destructor || Destructor->isTrivial() || !Holder->getInit() ||
          scopeExitLambda(Holder))
        return false;
      const Expr *Init = Holder->getInit()->IgnoreImplicit();
      bool Gives = false;
      if (const auto *Construct = dyn_cast<CXXConstructExpr>(Init);
          Construct &&
          !Construct->getConstructor()->isCopyOrMoveConstructor()) {
        const CXXConstructorDecl *Constructor = Construct->getConstructor();
        for (unsigned iArgument = 0;
             iArgument < Construct->getNumArgs() && !Gives; ++iArgument)
          Gives = GivesToChange(
              Construct->getArg(iArgument),
              iArgument < Constructor->getNumParams()
                  ? Constructor->getParamDecl(iArgument)->getType()
                  : QualType(),
              [] { return true; });
      } else {
        // Also an aggregate, of the members it initializes, after its bases.
        llvm::ArrayRef<Expr *> Inits;
        if (const auto *List = dyn_cast<InitListExpr>(Init))
          Inits = List->inits();
        else if (const auto *Parens = dyn_cast<CXXParenListInitExpr>(Init))
          Inits = Parens->getInitExprs();
        const unsigned nBases = Record->getNumBases();
        unsigned iInit = 0;
        for (const FieldDecl *Field : Record->fields()) {
          if (Gives || nBases + iInit >= Inits.size())
            break;
          Gives = GivesToChange(Inits[nBases + iInit++], Field->getType(),
                                [] { return true; });
        }
      }
      return HolderDestroysData[Holder] = Gives && mayFree(Destructor);
    };
    for (const CFGElement &Element : *Block) {
      // A call that can change the data of the actor, a member function
      // that isn't const of it, `mp_Items.f_Clear()`, can remove what a test
      // of a removal flag found there.
      if (const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>();
          Statement && State.FlagValidated.any()) {
        const CXXMethodDecl *Method = nullptr;
        const Expr *Object = nullptr;
        if (const auto *Member =
                dyn_cast<CXXMemberCallExpr>(Statement->getStmt())) {
          Method = Member->getMethodDecl();
          Object = Member->getImplicitObjectArgument();
        } else if (const auto *Operator =
                       dyn_cast<CXXOperatorCallExpr>(Statement->getStmt());
                   Operator && Operator->getNumArgs() >= 1) {
          Method = dyn_cast_or_null<CXXMethodDecl>(Operator->getDirectCallee());
          Object = Operator->getArg(0);
        }
        // Also through a reference to the data, `auto &Items = mp_Items;
        // Items.f_Clear()`, or an actor object parameter, also a const one of
        // a class with mutable members, which it can change, or whose body
        // can be seen to free, like through a pointer member,
        // `mp_pItems->f_Clear()`, or that an override can replace, which can
        // free through one too.
        bool Removes =
            Method &&
            (!Method->isConst() || Method->getParent()->hasMutableFields() ||
             Method->getDefinition() ||
             (Method->isVirtual() && !Method->hasAttr<FinalAttr>() &&
              !Method->getParent()->hasAttr<FinalAttr>())) &&
            !Method->isStatic() && Object &&
            (usesThis(Object) || Collector.isRooted(Object)) && mayFree(Method);
        // Also a lambda called, `fClear()` with `auto fClear = [this] {
        // mp_Items.f_Clear(); }`, which does what its body does.
        if (!Removes && Object && Method && Method->getParent()->isLambda())
          if (const LambdaExpr *Called = calledLambda(Object))
            Removes = mayFree(Called->getCallOperator());
        // Also one run here, `fg_CallSafe(fClear)` or `std::invoke(fClear)`,
        // also a coroutine, which runs until it first suspends.
        if (const auto *Call = dyn_cast<CallExpr>(Statement->getStmt());
            !Removes && Call && Call->getNumArgs() >= 1 &&
            (calleeName(Call) == "fg_CallSafe" || isStdInvoke(Call)))
          if (const LambdaExpr *Called = asLambda(Call->getArg(0)))
            Removes = mayFree(Called->getCallOperator());
        // Also a call given the data to change, `fg_Clear(mp_Items)` or
        // `fg_Stop(this)`, by a reference or pointer that isn't to const, and
        // a constructor, `CReset Reset(mp_Items)`.
        if (const auto *Call = dyn_cast<CallExpr>(Statement->getStmt());
            Call && !Removes) {
          const unsigned iFirst =
              isa<CXXOperatorCallExpr>(Call) && Method ? 1 : 0;
          for (unsigned iArgument = iFirst;
               iArgument < Call->getNumArgs() && !Removes; ++iArgument)
            Removes = GivesToChange(
                          Call->getArg(iArgument),
                          argumentParameterType(Call, iArgument),
                          [&] { return mayAssignArgument(Call, iArgument); }) &&
                      mayFree(Call->getDirectCallee());
        }
        // Not a copy or a move, `auto fDestroy = fg_Move(pEntry->m_fDestroy)`,
        // which takes what it is given but removes no entry.
        if (const auto *Construct =
                dyn_cast<CXXConstructExpr>(Statement->getStmt());
            Construct && !Removes &&
            !Construct->getConstructor()->isCopyOrMoveConstructor()) {
          const CXXConstructorDecl *Constructor = Construct->getConstructor();
          for (unsigned iArgument = 0;
               iArgument < Construct->getNumArgs() && !Removes; ++iArgument)
            Removes = GivesToChange(
                          Construct->getArg(iArgument),
                          iArgument < Constructor->getNumParams()
                              ? Constructor->getParamDecl(iArgument)->getType()
                              : QualType(),
                          [] { return true; }) &&
                      mayFree(Constructor);
        }
        if (Removes) {
          State.Stale |= State.FlagValidated;
          State.FlagValidated.reset();
        }
      }
      // The registration ends with the scope of its holder, and a scope exit,
      // like `g_OnScopeExit / [pEntry] { ... }`, runs its lambda there,
      // unless cancelled or run early, and so does the guard of
      // `fg_AsyncDestroy` start its cleanup, unless moved away.
      if (const std::optional<CFGAutomaticObjDtor> Destructor =
              Element.getAs<CFGAutomaticObjDtor>()) {
        const VarDecl *Holder = Destructor->getVarDecl();
        if (auto It = HolderIndices.find(Holder); It != HolderIndices.end())
          State.MaybeDead.set(It->second);
        // What runs here can start a phase, `g_OnScopeExit / [this] {
        // f_StopApp(); }` or a destructor that does, which no guard alive
        // keeps the entries of.
        // Each once, the flow is told again until it settles.
        auto [Phases, Inserted] =
            DestroyedPhases.try_emplace(Holder, std::pair<unsigned, unsigned>());
        if (Inserted) {
          if (const LambdaExpr *Cleanup = scopeExitLambda(Holder))
            Phases->second.first |= startedPhases(Cleanup->getBody(), Context);
          for (const LambdaExpr *Cleanup : asyncDestroyLambdas(Holder))
            Phases->second.first |= startedPhases(Cleanup->getBody(), Context);
          // Also of its members and bases, `struct CWrapper { CStop m_Stop;
          // };`.
          if (!scopeExitLambda(Holder))
            Phases->second.second = destructionStartedPhases(
                Holder->getType()->getAsCXXRecordDecl(), Context);
        }
        if (State.FlagValidated.any() && DestroysData(Holder)) {
          State.Stale |= State.FlagValidated;
          State.FlagValidated.reset();
        }
        unsigned Started = Phases->second.second;
        if (State.ArmedCleanups.contains(Holder)) {
          // A cleanup that can free, `g_OnScopeExit / [this] {
          // mp_Items.f_Clear(); }`, can remove what a test of a removal flag
          // found.
          if (const LambdaExpr *Cleanup = scopeExitLambda(Holder)) {
            Invoke(Cleanup, State, OnUse);
            if (State.FlagValidated.any() &&
                mayFree(Cleanup->getCallOperator())) {
              State.Stale |= State.FlagValidated;
              State.FlagValidated.reset();
            }
          }
          for (const LambdaExpr *Cleanup : asyncDestroyLambdas(Holder))
            Invoke(Cleanup, State, OnUse);
          Started |= Phases->second.first;
        }
        if (Started)
          StartPhases(State, Started);
        continue;
      }
      // The destructor of a temporary, `(void)CStopper{this}`, can start a
      // phase where the full expression ends.
      if (const std::optional<CFGTemporaryDtor> Destructor =
              Element.getAs<CFGTemporaryDtor>()) {
        const CXXDestructorDecl *Ending =
            Destructor->getBindTemporaryExpr()->getTemporary()->getDestructor();
        auto [Phases, Inserted] = TemporaryPhases.try_emplace(Ending, 0u);
        if (Inserted && Ending)
          Phases->second =
              destructionStartedPhases(Ending->getParent(), Context);
        if (const unsigned Started = Phases->second)
          StartPhases(State, Started);
        continue;
      }
      if (const std::optional<CFGLifetimeEnds> End =
              Element.getAs<CFGLifetimeEnds>()) {
        if (auto It = HolderIndices.find(End->getVarDecl());
            It != HolderIndices.end())
          State.MaybeDead.set(It->second);
        continue;
      }
      const std::optional<CFGStmt> Statement = Element.getAs<CFGStmt>();
      if (!Statement)
        continue;
      const Stmt *S = Statement->getStmt();
      // A scope exit called, `Cleanup()`, or the cleanup of
      // `fg_AsyncDestroy` awaited early, `co_await fg_Move(Cleanup)`, runs
      // here.
      // Also the phases it starts.
      // A cleanup that can free, `[this] { mp_Items.f_Clear(); }`, can
      // remove what a test of a removal flag found, like where its holder
      // runs it.
      auto Removed = [&](const LambdaExpr *Cleanup) {
        if (State.FlagValidated.any() && mayFree(Cleanup->getCallOperator())) {
          State.Stale |= State.FlagValidated;
          State.FlagValidated.reset();
        }
      };
      updateArmedCleanups(S, State.ArmedCleanups,
                          [&](const LambdaExpr *Cleanup, const VarDecl *) {
                            Invoke(Cleanup, State, OnUse);
                            Removed(Cleanup);
                            if (const unsigned Started =
                                    startedPhases(Cleanup->getBody(), Context))
                              StartPhases(State, Started);
                          });
      // A scope exit that is a discarded temporary, `(void)(g_OnScopeExit /
      // [pEntry] { ... })`, runs its lambda at the end of the full expression
      // it is made in, and the phases it starts, which is taken to be where
      // it is made too.
      auto RunExit = [&](const LambdaExpr *Cleanup) {
        Invoke(Cleanup, State, OnUse);
        Removed(Cleanup);
        if (const unsigned Started =
                startedPhases(Cleanup->getBody(), Context))
          StartPhases(State, Started);
      };
      if (const auto *Cleanup = dyn_cast<LambdaExpr>(S);
          Cleanup && isTemporaryScopeExitLambda(Cleanup, Context))
        RunExit(Cleanup);
      if (const auto *Declaration = dyn_cast<DeclStmt>(S)) {
        for (const Decl *D : Declaration->decls()) {
          if (auto It = Tracked.find(dyn_cast<ValueDecl>(D));
              It != Tracked.end()) {
            const auto *Variable = cast<VarDecl>(D);
            // The iterator of a loop over the data, also from a free
            // `begin(Container)`, is into the data.
            State.Reassigned.reset(It->second);
            if (Collector.RangeIterators.contains(Variable)) {
              State.Into.set(It->second);
              State.Stale.reset(It->second);
            } else if (Variable->getInit()) {
              CopyFrom(It->second, Variable->getInit(), State);
              // A static local is made once, by the first call, and later
              // calls find what that looked up, which other calls of the
              // actor, or another object, can have changed.
              if (Variable->isStaticLocal() && State.Into.test(It->second))
                State.Stale.set(It->second);
            } else {
              State.Stale.reset(It->second);
              State.Into.reset(It->second);
            }
          }
          // A registration runs its re-validation right away, and on every
          // resume while its holder lives.
          if (auto It = HolderIndices.find(dyn_cast<VarDecl>(D));
              It != HolderIndices.end()) {
            // Right away it uses what it uses before it re-validates it as it
            // is here, which can be stale already, after an earlier
            // suspension.
            for (const auto &[Validated, Use] :
                 Collector.CallbackEarlyUses.lookup(cast<VarDecl>(D)))
              if (auto Early = Tracked.find(Validated);
                  Early != Tracked.end() && State.Stale.test(Early->second))
                OnUse(Use, Variables[Early->second]);
            State.MaybeDead.reset(It->second);
            State.Unchecked.reset(It->second);
            State.Stale.reset(HolderVariables[It->second]);
            // A guard ends the coroutine right away when its phases began,
            // which then can't have removed what loses entries only in them,
            // together with the other guards alive, which checked theirs on
            // the last resume or when they were registered after it, unless
            // their phases may have started since, like by a call, which
            // they don't check until the next resume.
            unsigned Guarded = 0;
            for (unsigned iHolder = 0; iHolder < nHolders; ++iHolder)
              if (!State.MaybeDead.test(iHolder) &&
                  !State.Unchecked.test(iHolder))
                Guarded |= HolderPhaseMasks[iHolder];
            if (Guarded)
              for (unsigned iVariable = 0; iVariable < nVariables; ++iVariable)
                if (const std::optional<unsigned> Needed =
                        RequiredAt(State, iVariable);
                    Needed && !(*Needed & ~Guarded))
                  State.Stale.reset(iVariable);
            // What the callback assigns says where it may point: into the
            // data of the actor when any path assigns a lookup, or keeps
            // where it pointed when a path doesn't assign it.
            llvm::SmallDenseMap<const ValueDecl *, std::pair<bool, bool>, 4>
                MayLookUp;
            for (const auto &Assignment :
                 Collector.CallbackAssignments.lookup(cast<VarDecl>(D))) {
              auto &[bLookup, bAlways] = MayLookUp[Assignment.Assigned];
              bLookup |= Assignment.bLookup;
              bAlways |= Assignment.bUnconditional;
            }
            for (const auto &[Assigned, Result] : MayLookUp) {
              if (auto Index = Tracked.find(Assigned); Index != Tracked.end())
                State.Into[Index->second] =
                    Result.first ||
                    (!Result.second && State.Into.test(Index->second));
            }
          }
        }
      } else if (const Expr *Pointer = accessedAfterSuspension(S, Context)) {
        // What a pointer points to accessed after a suspension in the same
        // expression, `pEntry[(co_await fg_Suspend(), 0)]`, is used there.
        for (const Expr *Root : pointerRoots(Pointer))
          if (const auto *Ref = dyn_cast<DeclRefExpr>(Root))
            if (auto It = Tracked.find(resolveReferenceCapture(Ref->getDecl()));
                It != Tracked.end() && State.Stale.test(It->second))
              OnUse(Ref->getBeginLoc(), Variables[It->second]);
      } else if (const auto *Ref = dyn_cast<DeclRefExpr>(S)) {
        // A name of a structured binding that refers into what a lookup
        // found, `auto &[Value] = mp_Items[Key]`, is a use of the lookup.
        const ValueDecl *Used = Ref->getDecl();
        if (const auto *Binding = dyn_cast<BindingDecl>(Used))
          if (const auto *Decomposed =
                  dyn_cast_or_null<VarDecl>(Binding->getDecomposedDecl());
              Decomposed && Decomposed->getType()->isReferenceType())
            Used = Decomposed;
        if (auto Through = Collector.AssignedThrough.find(Ref);
            Through != Collector.AssignedThrough.end())
          for (const auto &Assignment : Through->second)
            if (auto Target =
                    Tracked.find(resolveReferenceCapture(Assignment.Assigned));
                Target != Tracked.end()) {
              const bool Into = State.Into.test(Target->second);
              const bool Stale = State.Stale.test(Target->second);
              CopyFrom(Target->second, Assignment.Value, State);
              State.Reassigned.set(Target->second);
              // One that may not be assigned keeps what it held too.
              if (Assignment.Maybe) {
                if (Into)
                  State.Into.set(Target->second);
                if (Stale)
                  State.Stale.set(Target->second);
              }
            }
        auto It = Tracked.find(resolveReferenceCapture(Used));
        // What `fg_Exchange` gives back of it, `*fg_Exchange(pEntry,
        // mp_Items.f_FindEqual(0))`, is what it held before, also where it is
        // assigned: a use of that, unless it is discarded.
        auto UsesExchanged = [&] {
          DynTypedNode Node = DynTypedNode::create(*Ref);
          for (int Depth = 0; Depth < 8; ++Depth) {
            const auto Parents = instantiatedParents(Node, Context);
            if (Parents.empty())
              return false;
            if (const auto *Exchange = Parents[0].get<CallExpr>()) {
              if (Exchange->getNumArgs() != 2 ||
                  Exchange->getArg(0)->IgnoreParenImpCasts() != Ref ||
                  (!isExchangeFunction(calleeName(Exchange))))
                return false;
              const auto Above = instantiatedParents(*Exchange, Context);
              if (Above.empty() || Above[0].get<CompoundStmt>())
                return false;
              if (const auto *Cast = Above[0].get<ExplicitCastExpr>();
                  Cast && Cast->getType()->isVoidType())
                return false;
              return true;
            }
            if (!Parents[0].get<ImplicitCastExpr>() &&
                !Parents[0].get<ParenExpr>())
              return false;
            Node = Parents[0];
          }
          return false;
        };
        if (It != Tracked.end()) {
          if (Collector.AssignedRefs.contains(Ref) &&
              State.Stale.test(It->second) && UsesExchanged())
            OnUse(Ref->getBeginLoc(), Variables[It->second]);
          // Assigning looks the data up again.
          if (auto Assigned = Collector.AssignedRefs.find(Ref);
              Assigned != Collector.AssignedRefs.end()) {
            // One that may not be assigned keeps what it held too.
            const bool Into = State.Into.test(It->second);
            const bool Stale = State.Stale.test(It->second);
            CopyFrom(It->second, Assigned->second, State);
            State.Reassigned.set(It->second);
            if (Collector.MaybeAssignedRefs.contains(Ref)) {
              if (Into)
                State.Into.set(It->second);
              if (Stale)
                State.Stale.set(It->second);
            }
          } else if (State.Stale.test(It->second) &&
                     (!Collector.CaptureRefs.contains(Ref) ||
                      Collector.CoroutineCaptureRefs.contains(Ref)) &&
                     !isPointerTest(Ref) && !namesOnlyStorage(Ref, Context))
            OnUse(Ref->getBeginLoc(), Variables[It->second]);
        } else if (const auto *Pointer = dyn_cast<VarDecl>(Used);
                   Pointer && Pointer->hasLocalStorage() &&
                   !isa<ParmVarDecl>(Pointer) && Pointer->getInit() &&
                   Pointer->getType()->isPointerType() &&
                   !isChangedVariable(Pointer)) {
          // Through an unchanged local pointer to a variable, `**ppEntry`
          // of `int **ppEntry = &pEntry`, what that variable holds is used
          // where it is read through both.
          // Also through more of them, `***pppEntry`, and through unchanged
          // copies of them, `int **ppCopy = ppEntry`, each once.
          const DeclRefExpr *Target = nullptr;
          int nLevels = 0;
          llvm::SmallPtrSet<const VarDecl *, 4> Copies;
          for (const VarDecl *Level = Pointer; Level && nLevels < 8;) {
            if (const auto *Copied = dyn_cast<DeclRefExpr>(
                    Level->getInit()->IgnoreParenImpCasts())) {
              const auto *Source = dyn_cast<VarDecl>(Copied->getDecl());
              if (!Source || !Source->hasLocalStorage() ||
                  isa<ParmVarDecl>(Source) || !Source->getInit() ||
                  !Source->getType()->isPointerType() ||
                  isChangedVariable(Source) || !Copies.insert(Source).second) {
                Target = nullptr;
                break;
              }
              Level = Source;
              continue;
            }
            const auto *Address = dyn_cast<UnaryOperator>(
                Level->getInit()->IgnoreParenImpCasts());
            Target = Address && Address->getOpcode() == UO_AddrOf
                         ? dyn_cast<DeclRefExpr>(
                               Address->getSubExpr()->IgnoreParenImpCasts())
                         : nullptr;
            ++nLevels;
            const auto *Next =
                Target ? dyn_cast<VarDecl>(Target->getDecl()) : nullptr;
            // Unchanged other than by its address being taken for the
            // level before.
            if (!Next ||
                Tracked.contains(resolveReferenceCapture(Target->getDecl())) ||
                !Next->hasLocalStorage() || isa<ParmVarDecl>(Next) ||
                !Next->getInit() || !Next->getType()->isPointerType() ||
                isChangedOtherThanBy(Target))
              break;
            Level = Next;
          }
          auto Through = [&] {
            const Expr *Read = Ref;
            int nDerefs = 0;
            for (int Depth = 0; Depth < 16 && nDerefs < nLevels + 1; ++Depth) {
              const auto Parents = instantiatedParents(*Read, Context);
              const auto *Parent =
                  Parents.empty() ? nullptr : Parents[0].get<Expr>();
              if (!Parent)
                return false;
              if (const auto *Deref = dyn_cast<UnaryOperator>(Parent);
                  Deref && Deref->getOpcode() == UO_Deref)
                ++nDerefs;
              else if (const auto *Member = dyn_cast<MemberExpr>(Parent);
                       Member && Member->isArrow() && nDerefs == nLevels)
                ++nDerefs;
              else if (const auto *Subscript =
                           dyn_cast<ArraySubscriptExpr>(Parent);
                       Subscript && Subscript->getBase() == Read)
                ++nDerefs;
              // Also what an iterator it points to gives, `(*piEntry)->
              // m_Value` or `**piEntry`.
              else if (const auto *Operator =
                           dyn_cast<CXXOperatorCallExpr>(Parent);
                       Operator && nDerefs == nLevels &&
                       Operator->getNumArgs() == 1 &&
                       (Operator->getOperator() == OO_Arrow ||
                        Operator->getOperator() == OO_Star) &&
                       Operator->getArg(0)->IgnoreParenImpCasts() ==
                           Read->IgnoreParenImpCasts())
                ++nDerefs;
              // What the pointer it points to points to, moved along,
              // `*(*ppEntry + 1)`.
              else if (const auto *Offset = dyn_cast<BinaryOperator>(Parent);
                       !(Offset && Offset->isAdditiveOp() &&
                         nDerefs == nLevels &&
                         Offset->getType()->isPointerType()) &&
                       !isa<ImplicitCastExpr, ParenExpr>(Parent))
                return false;
              Read = Parent;
            }
            return nDerefs == nLevels + 1;
          };
          if (Target)
            if (auto Aliased =
                    Tracked.find(resolveReferenceCapture(Target->getDecl()));
                Aliased != Tracked.end() &&
                State.Stale.test(Aliased->second) && Through())
              OnUse(Ref->getBeginLoc(), Variables[Aliased->second]);
        }
      }
      // A member function of what a variable refers to, called after an
      // await in its arguments, `Entry.f_Update(co_await fg_Suspend())`, uses
      // it where the await resumed.
      // Also of a member of it, `Entry.m_Value.f_Read(...)`, of an operator,
      // `Entry(co_await fg_Suspend())`, and what is given by reference or
      // pointer to a call whose other arguments suspend, `fg_Read(Entry,
      // co_await fg_Suspend())`.
      for (const Expr *Early : earlyCallOperands(S, Context)) {
        while (Early) {
          Early = Early->IgnoreParenImpCasts();
          const auto *Member = dyn_cast<MemberExpr>(Early);
          if (!Member || Member->isArrow())
            break;
          Early = Member->getBase();
        }
        // What a callback looks up again on resume, `pEntry =
        // mp_Items.f_FindEqual(Key)`, isn't what was read before, which only
        // a guard protects, or a callback that compares it with a fresh
        // lookup and leaves it as it is, `if (pEntry !=
        // mp_Items.f_FindEqual(Key)) return fg_MakeError(...)`.
        auto StaleBefore = [&](unsigned iVariable) {
          return State.Stale.test(iVariable) ||
                 (State.Into.test(iVariable) &&
                  !(Protected.test(iVariable) &&
                    !State.Reassigned.test(iVariable)) &&
                  Unguarded(State, /*Revalidations=*/false, nullptr,
                            /*Compared=*/true)
                      .test(iVariable));
        };
        if (const auto *Object = dyn_cast_or_null<DeclRefExpr>(Early))
          if (auto It =
                  Tracked.find(resolveReferenceCapture(Object->getDecl()));
              It != Tracked.end() && StaleBefore(It->second))
            OnUse(S->getBeginLoc(), Variables[It->second]);
      }
      // A swap of two tracked locals, `fg_Swap(pEntry, pOther)`, exchanges
      // what they hold.
      if (const auto *Swap = dyn_cast<CallExpr>(S);
          Swap && swappedArguments(Swap).first) {
        auto IndexOf = [&](const Expr *E) -> std::optional<unsigned> {
          const llvm::SmallVector<const DeclRefExpr *, 2> Targets =
              assignedVariables(E);
          const DeclRefExpr *Ref = Targets.size() == 1 ? Targets[0] : nullptr;
          auto It = Ref ? Tracked.find(resolveReferenceCapture(Ref->getDecl()))
                        : Tracked.end();
          if (It == Tracked.end())
            return std::nullopt;
          return It->second;
        };
        const std::optional<unsigned> First =
            IndexOf(swappedArguments(Swap).first);
        const std::optional<unsigned> Second =
            IndexOf(swappedArguments(Swap).second);
        if (First && Second) {
          const bool FirstStale = State.Stale.test(*First);
          const bool FirstInto = State.Into.test(*First);
          State.Stale[*First] = State.Stale.test(*Second);
          State.Into[*First] = State.Into.test(*Second);
          State.Stale[*Second] = FirstStale;
          State.Into[*Second] = FirstInto;
          State.Reassigned.set(*First);
          State.Reassigned.set(*Second);
        }
      }
      // A lambda copies the pointers it captures when it is made.
      if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
        for (const auto &[Key, Index] : LambdaCopies) {
          if (Key.first != Lambda)
            continue;
          if (const auto *Variable = dyn_cast<VarDecl>(Key.second);
              Variable && Variable->isInitCapture() && Variable->getInit()) {
            CopyFrom(Index, Variable->getInit(), State);
            continue;
          }
          const unsigned Source = Tracked.lookup(Key.second);
          State.Stale[Index] = State.Stale.test(Source);
          State.Reassigned[Index] = State.Reassigned.test(Source);
          State.Into[Index] = State.Into.test(Source);
        }
      }
      // A lambda called, like `[pValue] { ... }()` or `fRead()`, uses what it
      // captures: its copy, or what it refers to.
      if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(S);
          Call && Call->getOperator() == OO_Call && Call->getNumArgs() >= 1) {
        if (const LambdaExpr *Invoked = asLambda(Call->getArg(0)))
          Invoke(Invoked, State, OnUse, Call);
      }
      // Also through its call operator by name, `fRead.operator()()`.
      if (const auto *Call = dyn_cast<CXXMemberCallExpr>(S);
          Call && Call->getMethodDecl() &&
          Call->getMethodDecl()->getOverloadedOperator() == OO_Call) {
        if (const LambdaExpr *Invoked =
                asLambda(Call->getImplicitObjectArgument()))
          Invoke(Invoked, State, OnUse, Call);
      }
      // Also dispatched, `g_Dispatch / [pEntry] { ... }`, which runs it
      // later, with what it captured now. `fg_DirectDispatch` runs it here,
      // until it suspends.
      auto InvokeDispatched = [&](const LambdaExpr *Dispatched,
                                  const Expr *Dispatch) {
        const auto *DispatchCall = dyn_cast<CallExpr>(Dispatch);
        if (DispatchCall && calleeName(DispatchCall) == "fg_DirectDispatch")
          Invoke(Dispatched, State, OnUse, DispatchCall);
        else
          Invoke(Dispatched, State, OnUse, nullptr, true);
      };
      if (const auto *Lambda = dyn_cast<LambdaExpr>(S))
        if (const Expr *Dispatch = closureDispatch(
                Lambda, Lambda->getLambdaClass(), Context, nullptr))
          InvokeDispatched(Lambda, Dispatch);
      // Also one held in a local, dispatched where it is given, `g_Dispatch
      // / fRead`.
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
        if (const LambdaExpr *Stored = asLambda(Ref))
          if (const Expr *Dispatch = closureDispatch(
                  Ref, Stored->getLambdaClass(), Context, nullptr))
            InvokeDispatched(Stored, Dispatch);
      // Also run by `fg_CallSafe(fRead)` or `std::invoke(fRead)`, which call
      // it here.
      if (const auto *Call = dyn_cast<CallExpr>(S);
          Call && Call->getNumArgs() >= 1 &&
          (calleeName(Call) == "fg_CallSafe" || isStdInvoke(Call))) {
        if (const LambdaExpr *Invoked = asLambda(Call->getArg(0)))
          Invoke(Invoked, State, OnUse, Call);
      }
      // Also by a function that can be seen to call the callable it is given
      // before it returns, `fg_Read([pEntry] { ... })` with `int
      // fg_Read(auto &&_fRead) { return _fRead(); }`, which may not call it.
      if (const auto *Call = dyn_cast<CallExpr>(S);
          Call && calleeName(Call) != "fg_CallSafe" && !isStdInvoke(Call))
        for (unsigned iArgument = 0; iArgument < Call->getNumArgs();
             ++iArgument)
          if (const LambdaExpr *Invoked = asLambda(Call->getArg(iArgument));
              Invoked && CachedCallsArgument(Call, iArgument))
            Invoke(Invoked, State, OnUse, Call, false, true);
      // Calling a function of the own actor that starts or runs a phase, like
      // `co_await f_StopApp()`, runs it here, where a guard of the phase
      // can't keep the entries that it removes, also in a lambda called here,
      // `fStop()` with `auto fStop = [this] { return f_StopApp(); }`.
      llvm::SmallVector<const CallExpr *, 4> Calls;
      if (const auto *Call = dyn_cast<CallExpr>(S)) {
        Calls.push_back(Call);
        // Also of the lambda it runs, and the lambdas that one calls in turn.
        llvm::SmallPtrSet<const LambdaExpr *, 4> Followed;
        // Not what doesn't run: what isn't evaluated, `noexcept(f_StopApp())`,
        // or a branch of an `if constexpr` that is discarded.
        std::function<void(const Stmt *)> CollectCalls = [&](const Stmt *Inner) {
          if (!Inner || isa<LambdaExpr, UnaryExprOrTypeTraitExpr,
                            CXXNoexceptExpr>(Inner))
            return;
          if (const auto *Typeid = dyn_cast<CXXTypeidExpr>(Inner);
              Typeid && !Typeid->isPotentiallyEvaluated())
            return;
          if (const auto *InnerCall = dyn_cast<CallExpr>(Inner)) {
            Calls.push_back(InnerCall);
            if (const LambdaExpr *Nested = invokedLambda(InnerCall);
                Nested && Followed.insert(Nested).second)
              CollectCalls(Nested->getBody());
          }
          for (const Stmt *Child : evaluatedChildren(Inner, Context))
            CollectCalls(Child);
        };
        if (const LambdaExpr *Invoked = invokedLambda(Call);
            Invoked && Followed.insert(Invoked).second)
          CollectCalls(Invoked->getBody());
      }
      // Also what a constructor given the object calls, `CStop Stop(this)`
      // with `CStop(CApp *_pThis) { _pThis->f_StopApp(); }`, also in its
      // initializers and the default initializers of the members, and what
      // a helper calls, also a constructor it gives the object.
      llvm::SmallPtrSet<const FunctionDecl *, 8> Helpers;
      llvm::SmallPtrSet<const LambdaExpr *, 4> HelperLambdas;
      auto GivesObject = [](llvm::iterator_range<
                             CXXConstructExpr::const_arg_iterator> Arguments) {
        return llvm::any_of(Arguments, [](const Expr *Argument) {
          const QualType Type = Argument->getType();
          return (Type->isPointerType() || Argument->isGLValue()) &&
                 Type->getPointeeOrArrayElementType()->getAsCXXRecordDecl() &&
                 isOwnActorObject(Argument);
        });
      };
      std::function<void(const FunctionDecl *)> CollectHelper =
          [&](const FunctionDecl *Definition) {
            if (!Definition || !Definition->getBody() ||
                !Helpers.insert(Definition).second)
              return;
            // The expression of each default argument once, which its uses
            // share, also those in other default arguments, `int f1(int =
            // f0() + f0());`.
            llvm::SmallPtrSet<const Expr *, 8> Defaults;
            std::function<void(const Stmt *)> Collect =
                [&](const Stmt *Inner) {
                  if (!Inner || isa<LambdaExpr, UnaryExprOrTypeTraitExpr,
                                    CXXNoexceptExpr>(Inner))
                    return;
                  if (const auto *Default = dyn_cast<CXXDefaultInitExpr>(Inner))
                    return Defaults.insert(Default->getExpr()).second
                               ? Collect(Default->getExpr())
                               : void();
                  if (const auto *Default = dyn_cast<CXXDefaultArgExpr>(Inner))
                    return Defaults.insert(Default->getExpr()).second
                               ? Collect(Default->getExpr())
                               : void();
                  // Also what a lambda it calls calls, `[this] {
                  // f_StopApp(); }()`.
                  if (const auto *InnerCall = dyn_cast<CallExpr>(Inner)) {
                    Calls.push_back(InnerCall);
                    if (const LambdaExpr *Nested = invokedLambda(InnerCall);
                        Nested && HelperLambdas.insert(Nested).second)
                      Collect(Nested->getBody());
                  }
                  if (const auto *Construct = dyn_cast<CXXConstructExpr>(Inner);
                      Construct && GivesObject(Construct->arguments()))
                    CollectHelper(Construct->getConstructor()->getDefinition());
                  for (const Stmt *Child :
                       evaluatedChildren(Inner, Definition->getASTContext()))
                    Collect(Child);
                };
            Collect(Definition->getBody());
            if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Definition))
              for (const CXXCtorInitializer *Initializer : Constructor->inits())
                Collect(Initializer->getInit());
          };
      if (const auto *Construct = dyn_cast<CXXConstructExpr>(S);
          Construct && GivesObject(Construct->arguments()))
        CollectHelper(Construct->getConstructor()->getDefinition());
      // Also what a helper of the own actor that is called calls in turn,
      // like `f_StopWrapper()` that calls `f_StopApp()`, each followed once
      // however many there are.
      for (size_t iCall = 0; iCall < Calls.size(); ++iCall) {
        const CallExpr *Call = Calls[iCall];
        const FunctionDecl *Callee = Call->getDirectCallee();
        // Also through an unchanged local pointer to it, `(this->*fStop)()`
        // with `auto fStop = &CDistributedAppActor::f_StopApp`, also through
        // copies of it, `auto fCopy = fStop`.
        if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
            !Callee && Member)
          if (const auto *Pointer = dyn_cast<BinaryOperator>(
                  Member->getCallee()->IgnoreParens());
              Pointer && Pointer->isPtrMemOp()) {
            const Expr *Value = Pointer->getRHS()->IgnoreParenImpCasts();
            for (int Depth = 0; Depth < 4 && Value && !Callee; ++Depth) {
              const auto *Ref = dyn_cast<DeclRefExpr>(Value);
              const auto *Local =
                  Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
              if (!Local || !Local->hasLocalStorage() || !Local->getInit() ||
                  isChangedVariable(Local))
                break;
              Value = Local->getInit()->IgnoreParenImpCasts();
              if (const auto *Address = dyn_cast<UnaryOperator>(Value);
                  Address && Address->getOpcode() == UO_AddrOf)
                if (const auto *Function = dyn_cast<DeclRefExpr>(
                        Address->getSubExpr()->IgnoreParenImpCasts()))
                  Callee = dyn_cast<FunctionDecl>(Function->getDecl());
            }
          }
        // Also through a pointer to a function, `fStop(this)` with `auto
        // fStop = &fg_Stop`, or any, when it is given the object.
        const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
        bool bUnknownGivenObject = false;
        // A pointer to a member function of the object that can't be
        // resolved, `(this->*fChange)()`, can be any of them.
        if (!Callee && Member &&
            isOwnActorObject(Member->getImplicitObjectArgument()))
          bUnknownGivenObject = true;
        if (!Callee && !Member && !isa<CXXOperatorCallExpr>(Call)) {
          Callee = calleeThroughPointer(Call);
          bUnknownGivenObject =
              !Callee && llvm::any_of(Call->arguments(), [](const Expr *Argument) {
                const QualType Type = Argument->getType();
                return (Type->isPointerType() || Argument->isGLValue()) &&
                       Type->getPointeeOrArrayElementType()->getAsCXXRecordDecl() &&
                       isOwnActorObject(Argument);
              });
        }
        // A virtual one can run any of its overrides, `f_Tick()` of a
        // `CDerived` that stops the app.
        llvm::SmallVector<const FunctionDecl *, 4> Callees;
        if (Member && Callee && Member->getMethodDecl())
          for (const CXXMethodDecl *Method : dispatchedMethods(Member))
            Callees.push_back(Method);
        else if (Callee)
          Callees.push_back(Callee);
        std::optional<unsigned> Started;
        llvm::SmallVector<const FunctionDecl *, 4> Unannotated;
        for (const FunctionDecl *Candidate : Callees) {
          std::optional<unsigned> Phases =
              annotatedPhases(Candidate, AnnotationActorPhaseEntry);
          if (!Phases)
            Phases = annotatedPhases(Candidate, AnnotationActorPhase);
          if (Phases)
            Started = Started.value_or(0) | *Phases;
          else
            Unannotated.push_back(Candidate);
        }
        if (bUnknownGivenObject)
          Started = ~0u;
        // A helper of the object: a member function called on it, or another
        // function given it, `fg_Stop(this)`.
        auto GivenObject = [&] {
          return llvm::any_of(Call->arguments(), [](const Expr *Argument) {
            const QualType Type = Argument->getType();
            return (Type->isPointerType() || Argument->isGLValue()) &&
                   Type->getPointeeOrArrayElementType()->getAsCXXRecordDecl() &&
                   isOwnActorObject(Argument);
          });
        };
        for (const FunctionDecl *Candidate : Unannotated)
          if (const FunctionDecl *Definition = Candidate->getDefinition();
              Definition &&
              ((isa<CXXMethodDecl>(Definition) && Member &&
                isOwnActorObject(Member->getImplicitObjectArgument())) ||
               (!Member && GivenObject())))
            CollectHelper(Definition);
        // A free or static one given another actor, `fs_Stop(Other)` with a
        // `TCActor<CApp> Other`, starts the phase of that one.
        if (Started && *Started &&
            (Member ? isOwnActorObject(Member->getImplicitObjectArgument())
                    : !givenAnotherActor(Call, Context) || GivenObject()))
          StartPhases(State, *Started);
      }
      if (isSuspension(S) || SuspendPoints.contains(S)) {
        // The callbacks of the registrations alive run on resume, and use
        // stale what they use before they re-validate it, unless a guard of
        // a phase alive protects it, which runs before them.
        for (unsigned iHolder = 0; iHolder < nHolders; ++iHolder) {
          if (State.MaybeDead.test(iHolder))
            continue;
          // Only a guard registered before the callback runs before it.
          const llvm::BitVector Unphased =
              Unguarded(State, false, HolderDecls[iHolder]);
          for (const auto &[Validated, Use] :
               Collector.CallbackEarlyUses.lookup(HolderDecls[iHolder]))
            if (auto It = Tracked.find(Validated);
                It != Tracked.end() && State.Into.test(It->second) &&
                !(Protected.test(It->second) &&
                  !State.Reassigned.test(It->second)) &&
                Unphased.test(It->second))
              OnUse(Use, Variables[It->second]);
        }
        llvm::BitVector Suspended = Unguarded(State);
        Suspended &= State.Into;
        llvm::BitVector Kept = Protected;
        Kept.reset(State.Reassigned);
        Suspended.reset(Kept);
        State.Stale |= Suspended;
        // Each registration alive checks its phases on resume.
        State.Unchecked.reset();
      }
      // The temporary scope exits made in it run at its end, after what it
      // awaits.
      if (auto It = ExitsAtEnd.find(S); It != ExitsAtEnd.end())
        for (const LambdaExpr *Cleanup : It->second)
          RunExit(Cleanup);
    }
    return State;
  };
  auto Entry = [&](const CFGBlock *Block) {
    CState State = EmptyState();
    // What a lambda captures was looked up before it runs, unless it points
    // elsewhere where the lambda captures it.
    if (Block == &Graph->getEntry()) {
      State.MaybeDead.set();
      State.Into = EntryInto;
    }
    for (const CFGBlock::AdjacentBlock &Predecessor : Block->preds()) {
      if (const CFGBlock *Pred = Predecessor.getReachableBlock()) {
        if (auto It = StateAtEnd.find(Pred); It != StateAtEnd.end()) {
          CState From = It->second;
          // Past a flag check that leaves otherwise, its false edge, or of
          // a negated one, its true edge.
          if (auto Check = FlagChecks.find(Pred->getTerminatorStmt());
              Check != FlagChecks.end() && Pred->succ_size() == 2 &&
              *(Pred->succ_begin() + (Check->second.second ? 0 : 1)) == Block) {
            llvm::BitVector Keep = Check->second.first;
            Keep.flip();
            llvm::BitVector Validated = From.Stale;
            Validated &= Check->second.first;
            From.Stale &= Keep;
            From.FlagValidated |= Validated;
          }
          State.merge(From);
        }
      }
    }
    return State;
  };

  // Propagate until nothing changes, looking again only at the successors of
  // a block whose state changed, from the entry on, so that a long function
  // is gone through about once, not once per block. Bits are only ever added
  // at the ends of blocks, so this ends.
  {
    std::deque<const CFGBlock *> Work;
    llvm::BitVector Queued(Graph->getNumBlockIDs());
    auto Enqueue = [&](const CFGBlock *Block) {
      if (Block && Reachable.test(Block->getBlockID()) &&
          !Queued.test(Block->getBlockID())) {
        Queued.set(Block->getBlockID());
        Work.push_back(Block);
      }
    };
    Enqueue(&Graph->getEntry());
    for (const CFGBlock *Block : llvm::reverse(*Graph))
      Enqueue(Block);
    while (!Work.empty()) {
      const CFGBlock *Block = Work.front();
      Work.pop_front();
      Queued.reset(Block->getBlockID());
      const CState End = Transfer(Block, Entry(Block),
                                  [](SourceLocation, const VarDecl *) {});
      auto [It, Inserted] = StateAtEnd.try_emplace(Block, EmptyState());
      CState Merged = It->second;
      Merged.merge(End);
      if (Inserted || Merged != It->second) {
        It->second = std::move(Merged);
        for (const CFGBlock::AdjacentBlock &Successor : Block->succs())
          Enqueue(Successor.getReachableBlock());
      }
    }
  }

  const SourceManager &SM = Context.getSourceManager();
  // The first use of each variable after a suspension, in the order of the
  // source, which is where it is to be looked up again.
  llvm::MapVector<const VarDecl *, SourceLocation> FirstUses;
  for (const CFGBlock *Block : *Graph) {
    if (!Reachable.test(Block->getBlockID()))
      continue;
    Transfer(Block, Entry(Block),
             [&](SourceLocation UseLoc, const VarDecl *Variable) {
               const SourceLocation Loc = reportLocation(UseLoc, SM);
               if (Loc.isInvalid())
                 return;
               auto [It, Inserted] = FirstUses.try_emplace(Variable, Loc);
               if (!Inserted && SM.isBeforeInTranslationUnit(Loc, It->second))
                 It->second = Loc;
             });
  }
  for (const auto &[Variable, Loc] : FirstUses) {
    if (const CXXForRangeStmt *For = Collector.RangeIterators.lookup(Variable)) {
      const SourceLocation RangeLoc =
          reportLocation(For->getRangeInit()->getBeginLoc(), SM);
      diag(RangeLoc.isValid() ? RangeLoc : Loc,
           "the loop goes on over the data of the actor after a suspension, "
           "during which other calls of the actor can change or erase the "
           "data; loop over a copy, or look the entry up again after the "
           "suspension")
          << For->getRangeInit()->getSourceRange();
      continue;
    }
    const unsigned Index = Tracked.lookup(Variable);
    if (Required[Index]) {
      diag(Loc, "%0 refers into the data of the actor and is used after a "
                "suspension; %1 loses entries only in %2, so a guard of "
                "%select{that phase|those phases}3 that is alive at the use, "
                "like %select{f_CheckDestroyedOnResume|"
                "fp_CheckStoppedOrDestroyedOnResume}4, makes it safe")
          << nameOf(Variable) << RequiredFields[Index]
          << phaseNames(*Required[Index])
          << (llvm::popcount(*Required[Index]) > 1)
          << ((*Required[Index] & ActorPhase_StopApp) != 0);
      continue;
    }
    diag(Loc, "%0 refers into the data of the actor and is used after a "
              "suspension, during which other calls of the actor can change "
              "or erase the data; look it up again, or re-validate it with "
              "fg_OnResume")
        << nameOf(Variable);
  }
}

} // namespace clang::tidy::malterlib
