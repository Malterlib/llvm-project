//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ActorAnnotations.h"
#include "CoroutineUtils.h"
#include "OncePerTypeVisitor.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/SaveAndRestore.h"
#include <functional>
#include <optional>

namespace clang::tidy::malterlib {

/// The phases that the arguments of \p Attribute name, like "Destroy,
/// StopApp".
static unsigned parsePhases(const AnnotateAttr *Attribute) {
  unsigned Phases = 0;
  for (const Expr *Argument : Attribute->args()) {
    const auto *Literal =
        dyn_cast<StringLiteral>(Argument->IgnoreParenImpCasts());
    if (!Literal)
      continue;
    llvm::SmallVector<StringRef, 4> Names;
    Literal->getString().split(Names, ',', -1, false);
    for (StringRef Name : Names) {
      Name = Name.trim();
      if (Name.equals_insensitive("Destroy"))
        Phases |= ActorPhase_Destroy;
      else if (Name.equals_insensitive("StopApp"))
        Phases |= ActorPhase_StopApp;
      else if (Name.equals_insensitive("Owner"))
        Phases |= ActorPhase_Owner;
    }
  }
  return Phases;
}

/// The annotation of \p Kind on \p D itself, on any of its declarations.
static const AnnotateAttr *ownAnnotation(const Decl *D, StringRef Kind) {
  for (const Decl *Redeclaration : D->redecls()) {
    for (const auto *Attribute : Redeclaration->specific_attrs<AnnotateAttr>()) {
      if (Attribute->getAnnotation() == Kind)
        return Attribute;
    }
  }
  return nullptr;
}

/// The annotation of \p Kind on \p D, or on what it was instantiated from.
static const AnnotateAttr *ownOrPatternAnnotation(const Decl *D,
                                                  StringRef Kind) {
  for (int Depth = 0; D && Depth < 8; ++Depth) {
    if (const AnnotateAttr *Attribute = ownAnnotation(D, Kind))
      return Attribute;
    const Decl *Pattern = nullptr;
    if (const auto *Function = dyn_cast<FunctionDecl>(D))
      Pattern = Function->getTemplateInstantiationPattern();
    else if (const auto *Field = dyn_cast<FieldDecl>(D)) {
      // The field at the same place of the pattern, which has its fields in
      // the same order, each pattern's fields listed once.
      if (const auto *Record = dyn_cast<CXXRecordDecl>(Field->getParent());
          Record && Record->getTemplateInstantiationPattern()) {
        struct CPatternFields {
          llvm::DenseMap<const RecordDecl *,
                         llvm::SmallVector<const FieldDecl *, 0>>
              Fields;
        };
        const CXXRecordDecl *PatternRecord =
            Record->getTemplateInstantiationPattern();
        auto [It, Inserted] = astCache<CPatternFields>(Field->getASTContext())
                                  .Fields.try_emplace(PatternRecord);
        if (Inserted)
          llvm::append_range(It->second, PatternRecord->fields());
        const unsigned Index = Field->getFieldIndex();
        if (Index < It->second.size() &&
            It->second[Index]->getName() == Field->getName())
          Pattern = It->second[Index];
      }
    }
    if (Pattern == D)
      return nullptr;
    D = Pattern;
  }
  return nullptr;
}

/// \p Method and the member functions it overrides, through each of them,
/// each once, which a hierarchy of shared bases reaches along many paths.
static llvm::SmallVector<const CXXMethodDecl *, 8>
overrideClosure(const CXXMethodDecl *Method) {
  llvm::SmallVector<const CXXMethodDecl *, 8> Closure{Method};
  llvm::SmallPtrSet<const CXXMethodDecl *, 8> Visited{Method};
  for (size_t iMethod = 0; iMethod < Closure.size(); ++iMethod)
    for (const CXXMethodDecl *Overridden :
         Closure[iMethod]->overridden_methods())
      if (Visited.insert(Overridden).second)
        Closure.push_back(Overridden);
  return Closure;
}

/// The annotation of \p Kind on \p D, on what it was instantiated from, or for
/// a member function on a function it overrides.
static const AnnotateAttr *findAnnotation(const Decl *D, StringRef Kind) {
  if (!D)
    return nullptr;
  if (const auto *Method = dyn_cast<CXXMethodDecl>(D)) {
    for (const CXXMethodDecl *Overridden : overrideClosure(Method))
      if (const AnnotateAttr *Attribute =
              ownOrPatternAnnotation(Overridden, Kind))
        return Attribute;
    return nullptr;
  }
  return ownOrPatternAnnotation(D, Kind);
}

std::optional<unsigned> annotatedPhases(const Decl *D, StringRef Kind) {
  // An override runs in the phases of each of the functions it overrides,
  // which virtual calls can reach it through, and in those of its own.
  if (const auto *Method = dyn_cast<CXXMethodDecl>(D);
      Method && Method->size_overridden_methods() > 0) {
    std::optional<unsigned> Phases;
    for (const CXXMethodDecl *Overridden : overrideClosure(Method))
      if (const AnnotateAttr *Attribute =
              Overridden == Method ? ownAnnotation(Method, Kind)
                                   : ownOrPatternAnnotation(Overridden, Kind))
        Phases = Phases.value_or(0) | parsePhases(Attribute);
    if (Phases)
      return Phases;
  }
  if (const AnnotateAttr *Attribute = findAnnotation(D, Kind))
    return parsePhases(Attribute);
  return std::nullopt;
}

bool hasAnnotation(const Decl *D, StringRef Kind) {
  if (!D)
    return false;
  if (findAnnotation(D, Kind))
    return true;
  const auto *Record = dyn_cast<CXXRecordDecl>(D);
  if (const auto *Method = dyn_cast<CXXMethodDecl>(D))
    Record = Method->getParent();
  // A class derived from an annotated class is annotated too, each base
  // looked at once.
  llvm::SmallVector<const CXXRecordDecl *, 8> Work{Record};
  llvm::SmallPtrSet<const CXXRecordDecl *, 16> Visited;
  while (!Work.empty()) {
    const CXXRecordDecl *Current = Work.pop_back_val();
    if (!Current || !Visited.insert(Current).second)
      continue;
    if (ownAnnotation(Current, Kind))
      return true;
    if (const CXXRecordDecl *Pattern =
            Current->getTemplateInstantiationPattern();
        Pattern && Pattern != Current && ownAnnotation(Pattern, Kind))
      return true;
    if (Current->hasDefinition())
      for (const CXXBaseSpecifier &Base : Current->getDefinition()->bases())
        Work.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  return false;
}

std::string phaseNames(unsigned Phases) {
  llvm::SmallVector<StringRef, 3> Names;
  if (Phases & ActorPhase_Destroy)
    Names.push_back("Destroy");
  if (Phases & ActorPhase_StopApp)
    Names.push_back("StopApp");
  if (Phases & ActorPhase_Owner)
    Names.push_back("Owner");
  if (Names.empty())
    return "no phase";
  std::string Result;
  for (size_t iName = 0; iName < Names.size(); ++iName) {
    if (iName > 0)
      Result += iName + 1 == Names.size() ? " and " : ", ";
    Result += Names[iName];
  }
  return Result;
}

const FunctionDecl *enclosingPhaseFunction(const Stmt *S, ASTContext &Context) {
  DynTypedNode Node = DynTypedNode::create(*S);
  for (int Depth = 0; Depth < 256; ++Depth) {
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return nullptr;
    Node = Parents[0];
    if (const auto *Function = Node.get<FunctionDecl>()) {
      const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      if (!Method || !Method->getParent()->isLambda())
        return Function;
    }
  }
  return nullptr;
}

unsigned contextPhases(const FunctionDecl *Function) {
  if (!Function)
    return 0;
  unsigned Phases = 0;
  if (std::optional<unsigned> Phase =
          annotatedPhases(Function, AnnotationActorPhase))
    Phases |= *Phase;
  if (std::optional<unsigned> Entry =
          annotatedPhases(Function, AnnotationActorPhaseEntry))
    Phases |= *Entry;
  return Phases & ~ActorPhase_Owner;
}

/// Whether \p Record is an actor, derived from `CActor`, not the internals
/// of one, `CActorInternal`.
static bool derivesFromCActor(const CXXRecordDecl *Record) {
  // Each class once, at any depth, which a hierarchy of shared bases reaches
  // along many paths.
  llvm::SmallVector<const CXXRecordDecl *, 8> Work{Record};
  llvm::SmallPtrSet<const CXXRecordDecl *, 16> Visited;
  while (!Work.empty()) {
    const CXXRecordDecl *Current = Work.pop_back_val();
    if (!Current || !Current->hasDefinition() ||
        !Visited.insert(Current->getDefinition()).second)
      continue;
    if (Current->getIdentifier() && Current->getName() == "CActor")
      return true;
    for (const CXXBaseSpecifier &Base : Current->getDefinition()->bases())
      Work.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  return false;
}

/// Whether \p Object is the object of the actor whose phase the context is:
/// `this`, or what is named for it, like the `pThis` of a static function or
/// the `m_pThis` of a protocol, also through unchanged local copies, and what
/// it holds by value or owns. A pointer to another actor, like a member
/// `m_pApp`, isn't.
static bool isOwnActorObjectUncached(const Expr *Object, int Depth);

bool isOwnActorObject(const Expr *Object, int Depth) {
  if (!Object)
    return true;
  // Each once in a question, which a chain of conditionals of the same
  // aliases, `auto *pNext = bChoose ? pPrevious : pPrevious`, meets a number
  // of times that doubles with each, within a budget of the steps of it.
  static thread_local int s_StepsLeft = 0;
  static thread_local bool s_Asking = false;
  static thread_local llvm::DenseMap<const Expr *, bool> s_Results;
  const bool First = !s_Asking;
  if (First) {
    s_Asking = true;
    s_StepsLeft = 4096;
    s_Results.clear();
  }
  const llvm::scope_exit Done([&] {
    if (First)
      s_Asking = false;
  });
  if (auto It = s_Results.find(Object); It != s_Results.end())
    return It->second;
  if (Depth > 1024 || --s_StepsLeft < 0)
    return false;
  // Taken not to be while it is told, which an alias of itself can't be.
  s_Results[Object] = false;
  const bool Own = isOwnActorObjectUncached(Object, Depth);
  s_Results[Object] = Own;
  return Own;
}

/// The value of \p Condition when it is written as a constant, `true`, also
/// as the template argument it is instantiated with, `t_bUseOwn` of
/// `f_Clear<true>()`, or a constant expression the compiler evaluated.
static std::optional<bool> literalCondition(const Expr *Condition) {
  for (int Depth = 0; Condition && Depth < 8; ++Depth) {
    Condition = Condition->IgnoreParenImpCasts();
    if (const auto *Bool = dyn_cast<CXXBoolLiteralExpr>(Condition))
      return Bool->getValue();
    if (const auto *Integer = dyn_cast<IntegerLiteral>(Condition))
      return !Integer->getValue().isZero();
    if (const auto *Constant = dyn_cast<ConstantExpr>(Condition);
        Constant && Constant->hasAPValueResult() &&
        Constant->getAPValueResult().isInt())
      return !Constant->getAPValueResult().getInt().isZero();
    if (const auto *Substituted =
            dyn_cast<SubstNonTypeTemplateParmExpr>(Condition)) {
      Condition = Substituted->getReplacement();
      continue;
    }
    if (const auto *Not = dyn_cast<UnaryOperator>(Condition);
        Not && Not->getOpcode() == UO_LNot) {
      const std::optional<bool> Value = literalCondition(Not->getSubExpr());
      return Value ? std::optional<bool>(!*Value) : std::nullopt;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

static bool isOwnActorObjectUncached(const Expr *Object, int Depth) {
  const Expr *E = Object->IgnoreParenImpCasts();
  if (isa<CXXThisExpr>(E))
    return true;
  // Either value of a conditional, `(bChoose ? this : pOwn)`, when both are,
  // or the one a constant condition chooses, `(t_bUseOwn ? this :
  // m_pOther)` of `f_Clear<true>()`.
  if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E)) {
    if (const std::optional<bool> Chosen =
            isa<ConditionalOperator>(Conditional)
                ? literalCondition(Conditional->getCond())
                : std::nullopt)
      return isOwnActorObject(*Chosen ? Conditional->getTrueExpr()
                                      : Conditional->getFalseExpr(),
                              Depth + 1);
    return isOwnActorObject(Conditional->getTrueExpr(), Depth + 1) &&
           isOwnActorObject(Conditional->getFalseExpr(), Depth + 1);
  }
  // A cast between the classes of the object, like
  // `static_cast<CActor *>(this)`, is of the same object.
  if (const auto *Cast = dyn_cast<ExplicitCastExpr>(E);
      Cast && (Cast->getCastKind() == CK_DerivedToBase ||
               Cast->getCastKind() == CK_UncheckedDerivedToBase ||
               Cast->getCastKind() == CK_BaseToDerived ||
               Cast->getCastKind() == CK_NoOp))
    return isOwnActorObject(Cast->getSubExpr(), Depth + 1);
  // Also through a pointer to it, `auto *pState = &mp_State`.
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary && (Unary->getOpcode() == UO_Deref ||
                Unary->getOpcode() == UO_AddrOf))
    return isOwnActorObject(Unary->getSubExpr(), Depth + 1);
  // What an owning pointer member points to, unless that is an actor, like
  // `m_pOther->f_Stop()` of a `TCSharedPointer<CApp>`, which is another one.
  if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(E);
      Call && (Call->getOperator() == OO_Star ||
               Call->getOperator() == OO_Arrow) &&
      Call->getNumArgs() == 1) {
    QualType Pointee = Call->getType().getNonReferenceType();
    if (Pointee->isPointerType())
      Pointee = Pointee->getPointeeType();
    if (derivesFromCActor(Pointee->getAsCXXRecordDecl()))
      return false;
    return isOwnActorObject(Call->getArg(0), Depth + 1);
  }
  if (const auto *Call = dyn_cast<CXXMemberCallExpr>(E);
      Call && Call->getMethodDecl() &&
      Call->getMethodDecl()->getIdentifier() &&
      Call->getMethodDecl()->getName() == "f_Get") {
    // Also unless what it gives is another actor, like `mp_pOther.f_Get()`.
    QualType Pointee = Call->getType().getNonReferenceType();
    if (Pointee->isPointerType())
      Pointee = Pointee->getPointeeType();
    if (derivesFromCActor(Pointee->getAsCXXRecordDecl()))
      return false;
    return isOwnActorObject(Call->getImplicitObjectArgument(), Depth + 1);
  }
  // An element of an array it holds, `mp_States[0]`.
  if (const auto *Subscript = dyn_cast<ArraySubscriptExpr>(E);
      Subscript &&
      Subscript->getBase()->IgnoreParenImpCasts()->getType()->isArrayType())
    return isOwnActorObject(Subscript->getBase()->IgnoreParenImpCasts(),
                            Depth + 1);
  const ValueDecl *Decl = nullptr;
  if (const auto *Member = dyn_cast<MemberExpr>(E))
    Decl = Member->getMemberDecl();
  else if (const auto *Ref = dyn_cast<DeclRefExpr>(E))
    Decl = Ref->getDecl();
  if (!Decl || !Decl->getIdentifier())
    return false;
  // Named for the own object, unless a variable that may have been changed,
  // also the copy a mutable lambda captured.
  if (Decl->getName().ends_with("This"))
    return !isa<DeclRefExpr>(E) || !isa<VarDecl>(Decl) ||
           isUnchangedAt(cast<DeclRefExpr>(E));
  // A parameter given the object of the current actor, `_pActor` of a static
  // or free function, which its callers are checked to give it.
  if (isa<DeclRefExpr>(E) && isActorObjectParameter(Decl))
    return isUnchangedAt(cast<DeclRefExpr>(E));
  // What the object holds by value, or owns, like `*mp_pInternal`.
  if (const auto *Member = dyn_cast<MemberExpr>(E);
      Member && isa<FieldDecl>(Decl) && !Decl->getType()->isPointerType() &&
      !Decl->getType()->isReferenceType())
    return isOwnActorObject(Member->getBase(), Depth + 1);
  // A copy that a lambda captured of what a reference refers to, like
  // `[State]` of `auto &State = mp_State`, is an object of its own.
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E);
      Ref && Decl->getType()->isReferenceType() && isCapturedByCopyAt(Ref))
    return false;
  if (const auto *Variable = dyn_cast<VarDecl>(Decl);
      Variable && Variable->hasLocalStorage() && !isa<ParmVarDecl>(Variable) &&
      Variable->getInit() &&
      (Variable->getType()->isReferenceType() ||
       (!isChangedVariable(Variable) &&
        // Also the copy a lambda captured, which a mutable lambda can change.
        (!isa<DeclRefExpr>(E) || isUnchangedAt(cast<DeclRefExpr>(E))))))
    return isOwnActorObject(Variable->getInit(), Depth + 1);
  return false;
}

unsigned testedPhaseFlags(const Expr *E) {
  if (!E)
    return 0;
  E = E->IgnoreParenImpCasts();
  if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(E))
    return testedPhaseFlags(Cleanups->getSubExpr());
  if (const auto *Binary = dyn_cast<BinaryOperator>(E)) {
    if (Binary->getOpcode() == BO_LOr)
      return testedPhaseFlags(Binary->getLHS()) |
             testedPhaseFlags(Binary->getRHS());
    // Compared to be set, like `bFlag == true` or `bFlag != false`.
    if (Binary->getOpcode() == BO_EQ || Binary->getOpcode() == BO_NE) {
      const bool Set = Binary->getOpcode() == BO_EQ;
      for (const auto &[Flag, Literal] :
           {std::pair{Binary->getLHS(), Binary->getRHS()},
            std::pair{Binary->getRHS(), Binary->getLHS()}}) {
        if (const auto *Bool =
                dyn_cast<CXXBoolLiteralExpr>(Literal->IgnoreParenImpCasts());
            Bool && Bool->getValue() == Set)
          return testedPhaseFlags(Flag);
      }
    }
    return 0;
  }
  // The flag of the own actor: a flag of another actor, like
  // `pOther->f_IsDestroyed()`, doesn't tell about this one.
  const Decl *Flag = nullptr;
  if (const auto *Member = dyn_cast<MemberExpr>(E)) {
    if (!isOwnActorObject(Member->getBase()))
      return 0;
    Flag = Member->getMemberDecl();
  } else if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    // A local reference to a flag, `bool const &bDestroyed = mp_bDestroyed`,
    // reads it where it is used, unless a lambda captured a copy of it.
    // One bound to itself, `bool const &bFlag = bFlag`, tells nothing, and
    // one bound to a temporary, `auto const &bDestroyed = f_IsDestroyed()`,
    // holds the value it had. Each is told once while the outermost test is,
    // also one that others are made from many times, `bool const &b1 = b0 ||
    // b0;`.
    static thread_local int s_Aliases = 0;
    static thread_local llvm::DenseMap<const VarDecl *, unsigned>
        *s_pAliasPhases = nullptr;
    if (const auto *Alias = dyn_cast<VarDecl>(Ref->getDecl());
        Alias && Alias->getType()->isReferenceType() &&
        Alias->hasLocalStorage() && !isa<ParmVarDecl>(Alias) &&
        !Alias->isInitCapture() && Alias->getInit() &&
        !isCapturedByCopyAt(Ref)) {
      const Expr *Init = Alias->getInit();
      if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(Init))
        Init = Cleanups->getSubExpr();
      if (isa<MaterializeTemporaryExpr>(Init->IgnoreParens()))
        return 0;
      std::optional<llvm::DenseMap<const VarDecl *, unsigned>> Owned;
      if (!s_pAliasPhases)
        s_pAliasPhases = &Owned.emplace();
      const llvm::scope_exit Done([&] {
        if (Owned)
          s_pAliasPhases = nullptr;
      });
      if (auto It = s_pAliasPhases->find(Alias); It != s_pAliasPhases->end())
        return It->second;
      if (s_Aliases > 32)
        return 0;
      (*s_pAliasPhases)[Alias] = 0;
      ++s_Aliases;
      const unsigned Phases = testedPhaseFlags(Alias->getInit());
      --s_Aliases;
      (*s_pAliasPhases)[Alias] = Phases;
      return Phases;
    }
    Flag = Ref->getDecl();
  } else if (const auto *Call = dyn_cast<CallExpr>(E)) {
    // Read through a member function of the flag, like `m_bStopping.load()`
    // of an atomic flag, or `m_bStopping.f_Load()` of a Malterlib one.
    if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
        Member && Member->getMethodDecl() &&
        Member->getMethodDecl()->getIdentifier() &&
        (Member->getMethodDecl()->getName() == "load" ||
         Member->getMethodDecl()->getName() == "f_Load") &&
        Member->getNumArgs() <= 1)
      if (const unsigned Phases =
              testedPhaseFlags(Member->getImplicitObjectArgument()))
        return Phases;
    if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
        Member && !isOwnActorObject(Member->getImplicitObjectArgument()))
      return 0;
    // A static or free getter tells about the actor it is given, like
    // `fs_IsStopping(pOther)` or `fs_IsStopping(Other)` of a handle, so that
    // has to be the own one.
    if (!isa<CXXMemberCallExpr>(Call) && Call->getDirectCallee() &&
        givenAnotherActor(Call, Call->getDirectCallee()->getASTContext()))
      return 0;
    Flag = Call->getDirectCallee();
  }
  if (!Flag)
    return 0;
  return annotatedPhases(Flag, AnnotationActorPhaseFlag).value_or(0) &
         ~ActorPhase_Owner;
}

/// The context of the locals followed while the outermost result is told,
/// which tells the value of a constant condition, `false ? Error : ...`.
static thread_local const ASTContext *s_pErrorResultContext = nullptr;

/// Whether what the local \p Local is made with is an error, each local once
/// while the outermost result is told, also one that others are made from
/// many times, `auto e1 = b ? e0 : e0;`, and one made from itself tells
/// nothing more.
static bool localIsErrorResult(const VarDecl *Local) {
  static thread_local llvm::DenseMap<const VarDecl *, std::optional<bool>>
      *s_pLocals = nullptr;
  std::optional<llvm::DenseMap<const VarDecl *, std::optional<bool>>> Owned;
  if (!s_pLocals)
    s_pLocals = &Owned.emplace();
  const ASTContext *SavedContext = s_pErrorResultContext;
  s_pErrorResultContext = &Local->getASTContext();
  const llvm::scope_exit Release([&] {
    s_pErrorResultContext = SavedContext;
    if (Owned)
      s_pLocals = nullptr;
  });
  if (auto [It, Inserted] = s_pLocals->try_emplace(Local, std::nullopt);
      !Inserted)
    return It->second.value_or(false);
  const bool Result = isErrorResult(Local->getInit());
  (*s_pLocals)[Local] = Result;
  return Result;
}

/// Whether \p E is an empty result, like `{}`, `nullptr` or a default
/// constructed exception pointer, which isn't an error.
bool isErrorResult(const Expr *E) {
  for (int Depth = 0; E && Depth < 16; ++Depth) {
    E = E->IgnoreParens();
    if (const auto *Cleanups = dyn_cast<ExprWithCleanups>(E)) {
      E = Cleanups->getSubExpr();
      continue;
    }
    if (const auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(E)) {
      E = Materialize->getSubExpr();
      continue;
    }
    if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(E)) {
      E = Bind->getSubExpr();
      continue;
    }
    if (const auto *Cast = dyn_cast<CastExpr>(E)) {
      E = Cast->getSubExpr();
      continue;
    }
    if (const auto *List = dyn_cast<InitListExpr>(E)) {
      if (List->getNumInits() != 1)
        return false;
      E = List->getInit(0);
      continue;
    }
    // Either value of a conditional may be the result, or the one a constant
    // condition selects.
    if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E)) {
      bool Value = false;
      if (s_pErrorResultContext &&
          !Conditional->getCond()->isValueDependent() &&
          Conditional->getCond()->EvaluateAsBooleanCondition(
              Value, *s_pErrorResultContext)) {
        E = Value ? Conditional->getTrueExpr() : Conditional->getFalseExpr();
        continue;
      }
      return isErrorResult(Conditional->getTrueExpr()) &&
             isErrorResult(Conditional->getFalseExpr());
    }
    // A local, what it was made with when it doesn't change, `auto Error =
    // fg_MakeError(...); return Error;`, and a result that is set, which
    // ends the coroutine too, `TCAsyncResult<int> Result;
    // Result.f_SetResult(0); return Result;`.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
      const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
      if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local))
        return false;
      if (recordName(Local->getType()) == "TCAsyncResult") {
        const auto *Function = dyn_cast_or_null<FunctionDecl>(
            Local->getParentFunctionOrMethod());
        // A call that sets it, not one that copies another result,
        // `f_SetResult(Other)`, which can be unset too.
        auto SetsIt = [&](const Stmt *S) {
          const auto *Call = dyn_cast_or_null<CXXMemberCallExpr>(S);
          if (!Call || !Call->getMethodDecl() ||
              !Call->getMethodDecl()->getIdentifier() ||
              !Call->getMethodDecl()->getName().starts_with("f_Set") ||
              llvm::any_of(Call->arguments(), [](const Expr *Argument) {
                return recordName(Argument->getType()) == "TCAsyncResult";
              }))
            return false;
          const auto *Object = dyn_cast<DeclRefExpr>(
              Call->getImplicitObjectArgument()->IgnoreParenImpCasts());
          return Object && Object->getDecl() == Local;
        };
        // The calls that set it, and the uses that change it otherwise, like
        // `Result.f_Clear()`, which makes it continue the coroutine: not
        // setting it, what is const and returning it, nor in a branch of an
        // `if constexpr` that is discarded. Found once for each result,
        // however many times it is returned.
        struct CResultUses {
          llvm::SmallVector<const CallExpr *, 4> Sets;
          llvm::SmallVector<const DeclRefExpr *, 4> Changes;
        };
        struct CLocals {
          llvm::DenseMap<const VarDecl *, CResultUses> Uses;
        };
        auto [UsesIt, Inserted] =
            astCache<CLocals>(Local->getASTContext()).Uses.try_emplace(Local);
        CResultUses &Uses = UsesIt->second;
        if (Inserted && Function) {
          std::function<void(const Stmt *)> Find = [&](const Stmt *S) {
            if (!S)
              return;
            if (const auto *If = dyn_cast<IfStmt>(S);
                If && If->isConstexpr() && !If->getCond()->isValueDependent())
              if (const std::optional<const Stmt *> Case =
                      If->getNondiscardedCase(Local->getASTContext())) {
                Find(If->getInit());
                Find(If->getCond());
                Find(*Case);
                return;
              }
            if (SetsIt(S))
              Uses.Sets.push_back(cast<CallExpr>(S));
            if (const auto *Use = dyn_cast<DeclRefExpr>(S);
                Use && Use->getDecl() == Local) {
              const auto Parents =
                  instantiatedParents(*Use, Local->getASTContext());
              const auto *Member =
                  Parents.empty() ? nullptr : Parents[0].get<MemberExpr>();
              const auto *Method =
                  Member ? dyn_cast<CXXMethodDecl>(Member->getMemberDecl())
                         : nullptr;
              const bool Returned =
                  !Parents.empty() &&
                  (Parents[0].get<ReturnStmt>() ||
                   (Parents[0].get<ImplicitCastExpr>() &&
                    !instantiatedParents(Parents[0], Local->getASTContext())
                         .empty() &&
                    instantiatedParents(Parents[0], Local->getASTContext())[0]
                        .get<ReturnStmt>()));
              // Not setting it from another result, which can be unset,
              // `Result.f_SetResult(Empty)`.
              const auto Above =
                  Member ? instantiatedParents(*Member, Local->getASTContext())
                         : Parents;
              const bool SetsFromResult =
                  Member && !Above.empty() &&
                  Above[0].get<CXXMemberCallExpr>() &&
                  !SetsIt(Above[0].get<CXXMemberCallExpr>());
              if (!Returned &&
                  !(Method && (Method->isConst() ||
                               (Method->getIdentifier() &&
                                Method->getName().starts_with("f_Set") &&
                                !SetsFromResult))))
                Uses.Changes.push_back(Use);
            }
            for (const Stmt *Child : childrenOnce(S))
              Find(Child);
          };
          Find(Function->getBody());
        }
        // Where the last set that runs on every path before it ends: what
        // changes it before doesn't matter.
        const SourceManager &SM = Local->getASTContext().getSourceManager();
        bool Set = false;
        SourceLocation SetEnd;
        auto SetAt = [&](SourceLocation End) {
          Set = true;
          if (SetEnd.isInvalid() || SM.isBeforeInTranslationUnit(SetEnd, End))
            SetEnd = End;
        };
        for (const CallExpr *Setter : Uses.Sets)
          if (runsOnEveryPathBefore(Setter, Ref, Local->getASTContext()))
            SetAt(Setter->getEndLoc());
        // Also set in each branch of an `if` before, `if (bOther)
        // Result.f_SetResult(1); else Result.f_SetResult(2);`.
        // Not one that a jump to a label after it can skip, `if (bSkip) goto
        // Done; Result.f_SetException(...); Done:`.
        std::function<bool(const Stmt *)> SetsOnEveryPath =
            [&](const Stmt *S) -> bool {
          if (!S)
            return false;
          if (const auto *E = dyn_cast<Expr>(S); E && SetsIt(E->IgnoreImplicit()))
            return true;
          if (const auto *Block = dyn_cast<CompoundStmt>(S)) {
            bool Sets = false;
            for (const Stmt *Statement : Block->body()) {
              if (hasJumpTarget(Statement))
                Sets = false;
              Sets = Sets || SetsOnEveryPath(Statement);
            }
            return Sets;
          }
          if (const auto *If = dyn_cast<IfStmt>(S))
            return If->getElse() && SetsOnEveryPath(If->getThen()) &&
                   SetsOnEveryPath(If->getElse());
          return false;
        };
        // Whether a loop around the return can run what follows it before
        // it runs again.
        bool InLoop = false;
        for (DynTypedNode Node = DynTypedNode::create(*Ref);;) {
          const auto Parents =
              instantiatedParents(Node, Local->getASTContext());
          if (Parents.empty() || Parents[0].get<LambdaExpr>() ||
              Parents[0].get<FunctionDecl>())
            break;
          InLoop |= Parents[0].get<ForStmt>() || Parents[0].get<WhileStmt>() ||
                    Parents[0].get<DoStmt>() ||
                    Parents[0].get<CXXForRangeStmt>();
          if (const auto *Block = Parents[0].get<CompoundStmt>()) {
            std::optional<SourceLocation> BlockSet;
            for (const Stmt *Statement : Block->body()) {
              if (hasJumpTarget(Statement))
                BlockSet.reset();
              if (Statement == Node.get<Stmt>())
                break;
              if (SetsOnEveryPath(Statement))
                BlockSet = Statement->getEndLoc();
            }
            if (BlockSet)
              SetAt(*BlockSet);
          }
          Node = Parents[0];
        }
        // And nothing else changes it after, before the return, `if (...)
        // return Result; Result.f_Clear();` changes only what a later return
        // gives, unless a loop runs it again before this one.
        const bool Changed =
            Set && llvm::any_of(Uses.Changes, [&](const DeclRefExpr *Use) {
              return Use != Ref &&
                     SM.isBeforeInTranslationUnit(SetEnd, Use->getBeginLoc()) &&
                     (InLoop || SM.isBeforeInTranslationUnit(
                                    Use->getBeginLoc(), Ref->getBeginLoc()));
            });
        return Set && !Changed;
      }
      if (!Local->getInit() || isChangedVariable(Local))
        return false;
      return localIsErrorResult(Local);
    }
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(E)) {
      // An error is made from what describes it, like `CException(Type,
      // File, Line, ...)`; one argument is a conversion of what it is made of.
      if (Construct->getNumArgs() >= 2)
        return true;
      if (Construct->getNumArgs() == 0)
        return false;
      E = Construct->getArg(0);
      continue;
    }
    // A conversion of what is returned, like of a `CException` to a
    // `CExceptionPointer`, is of what it converts.
    if (const auto *Conversion = dyn_cast<CXXMemberCallExpr>(E);
        Conversion && isa_and_nonnull<CXXConversionDecl>(
                          Conversion->getMethodDecl())) {
      E = Conversion->getImplicitObjectArgument();
      continue;
    }
    // A function that makes an error, like `fg_MakeError`, a message, or a
    // throw. Another function, a variable, a null pointer or an empty result
    // may be success.
    if (const auto *Call = dyn_cast<CallExpr>(E)) {
      const FunctionDecl *Callee = Call->getDirectCallee();
      // `std::make_exception_ptr(...)` always makes one, which the library
      // falls back to.
      if (Callee && Callee->getIdentifier() &&
          Callee->getName() == "make_exception_ptr" &&
          Callee->isInStdNamespace())
        return true;
      // A virtual one can run an override that gives success, unless it
      // can't be overridden, or the object is of the type it is made as, a
      // temporary, `DMibErrorInstance(...).f_ExceptionPointer()`, or a local
      // that isn't a reference.
      if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call))
        if (const CXXMethodDecl *Method = Member->getMethodDecl();
            Method && Method->isVirtual() &&
            !Method->hasAttr<FinalAttr>() &&
            !Method->getParent()->hasAttr<FinalAttr>() &&
            !(isa<MemberExpr>(Member->getCallee()->IgnoreParens()) &&
              cast<MemberExpr>(Member->getCallee()->IgnoreParens())
                  ->hasQualifier())) {
          const Expr *Object = Member->getImplicitObjectArgument();
          const Expr *Bare = Object ? Object->IgnoreParenImpCasts() : nullptr;
          const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Bare);
          const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          const bool Known =
              Bare && !Bare->getType()->isPointerType() &&
              (isa<MaterializeTemporaryExpr, CXXTemporaryObjectExpr,
                   CXXConstructExpr, CXXBindTemporaryExpr>(
                   Object->IgnoreParens()) ||
               !Bare->isGLValue() ||
               (Local && !Local->getType()->isReferenceType()));
          if (!Known)
            return false;
        }
      // One that can be looked into makes an error when each of its returns
      // does, not `fg_GetError() { return {}; }`, also through helpers
      // however many, but one that calls itself in turn can't be told to.
      // Each once while the outermost is told, also one that several call.
      static thread_local llvm::SmallPtrSet<const FunctionDecl *, 8>
          s_InProgress;
      static thread_local llvm::DenseMap<const FunctionDecl *, bool>
          *s_pResults = nullptr;
      if (const FunctionDecl *Definition =
              Callee ? Callee->getDefinition() : nullptr;
          Definition && Definition->getBody()) {
        if (s_pResults)
          if (const auto Found = s_pResults->find(Definition);
              Found != s_pResults->end())
            return Found->second;
        if (!s_InProgress.insert(Definition).second)
          return false;
        std::optional<llvm::DenseMap<const FunctionDecl *, bool>> Owned;
        if (!s_pResults)
          s_pResults = &Owned.emplace();
        const llvm::scope_exit Release([&] {
          s_InProgress.erase(Definition);
          if (Owned)
            s_pResults = nullptr;
        });
        bool Returns = false;
        bool AllErrors = true;
        std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
          if (!S || !AllErrors || isa<LambdaExpr>(S))
            return;
          if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
            Returns = true;
            AllErrors = Return->getRetValue() &&
                        isErrorResult(Return->getRetValue());
          }
          // Not a return in a branch an `if constexpr` discards, or that a
          // constant condition never takes, `if (t_bAllowSuccess)`.
          for (const Stmt *Child :
               evaluatedChildren(S, Definition->getASTContext()))
            Visit(Child);
        };
        Visit(Definition->getBody());
        (*s_pResults)[Definition] = Returns && AllErrors;
        return Returns && AllErrors;
      }
      // Not one that gets the current one, like `fg_CurrentException()`,
      // which is empty outside of a handler, nor one that gets what may be
      // empty, like `f_GetException()` of a result, nor one that checks for
      // one, like `fg_CheckError()`, which gives none when it finds none.
      return Callee && Callee->getIdentifier() &&
             (Callee->getName().contains("Error") ||
              Callee->getName().contains("Exception")) &&
             llvm::none_of(
                 std::initializer_list<StringRef>{"Current", "Get", "Check",
                                                  "Verify", "Validate", "Test",
                                                  "Try", "Find", "If"},
                 [&](StringRef Word) {
                   return Callee->getName().contains(Word);
                 });
    }
    // A status that isn't zero, like `return 1;`.
    if (const auto *Integer = dyn_cast<IntegerLiteral>(E))
      return !Integer->getValue().isZero();
    return isa<StringLiteral, CXXThrowExpr>(E);
  }
  return false;
}

static bool isEmptyResult(const Expr *E) { return !isErrorResult(E); }

/// Whether \p S leaves the callback with an error: a `return` of something,
/// or a `throw`.
static bool mayReturnSuccess(const Stmt *S, const ASTContext &Context);

static bool leavesWithError(const Stmt *S, const ASTContext &Context) {
  if (!S || mayReturnSuccess(S, Context))
    return false;
  if (const auto *Compound = dyn_cast<CompoundStmt>(S))
    return !Compound->body_empty() &&
           leavesWithError(Compound->body_back(), Context);
  if (const auto *Return = dyn_cast<ReturnStmt>(S))
    return Return->getRetValue() && !isEmptyResult(Return->getRetValue());
  if (const auto *E = dyn_cast<Expr>(S))
    return isa<CXXThrowExpr>(E->IgnoreParenImpCasts());
  // The branch of an `if constexpr` that is kept, `if constexpr
  // (t_bCheckDestroy) return fg_MakeError(...)`, and else both branches.
  if (const auto *If = dyn_cast<IfStmt>(S)) {
    if (If->isConstexpr() && !If->getCond()->isValueDependent())
      if (const std::optional<const Stmt *> Case =
              If->getNondiscardedCase(Context))
        return *Case && leavesWithError(*Case, Context);
    return If->getElse() && leavesWithError(If->getThen(), Context) &&
           leavesWithError(If->getElse(), Context);
  }
  return false;
}

/// Whether \p S can return from the callback without an error, not in a
/// branch an `if constexpr` discards, or a constant condition never takes.
static bool mayReturnSuccess(const Stmt *S, const ASTContext &Context) {
  if (!S)
    return false;
  if (isa<LambdaExpr>(S))
    return false;
  if (const auto *Return = dyn_cast<ReturnStmt>(S))
    return !Return->getRetValue() || isEmptyResult(Return->getRetValue());
  for (const Stmt *Child : evaluatedChildren(S, Context))
    if (mayReturnSuccess(Child, Context))
      return true;
  return false;
}

unsigned guardedPhases(const LambdaExpr *Lambda) {
  const CompoundStmt *Body = Lambda->getCompoundStmtBody();
  if (!Body || Lambda->getCallOperator()->getReturnType()->isVoidType() ||
      hasJumps(Body))
    return 0;
  unsigned Phases = 0;
  const ASTContext &Context = Lambda->getCallOperator()->getASTContext();
  for (const Stmt *S : Body->body()) {
    // Also with an init statement, `if (int Unused = 0; f_IsDestroyed())`,
    // which runs before the test, and what phase it starts is not guarded,
    // below.
    if (const auto *If = dyn_cast<IfStmt>(S);
        If && !If->getConditionVariable() &&
        leavesWithError(If->getThen(), Context)) {
      Phases |= testedPhaseFlags(If->getCond());
      if (mayReturnSuccess(If->getElse(), Context))
        break;
      continue;
    }
    if (mayReturnSuccess(S, Context))
      break;
  }
  // Not a phase that the callback starts or runs itself, like `f_StopNow()`
  // after the test, which then returns success with the entries gone.
  return Phases ? Phases & ~startedPhases(Body, Context) : 0;
}

const FunctionDecl *calleeThroughPointer(const CallExpr *Call) {
  const auto *Ref =
      dyn_cast<DeclRefExpr>(Call->getCallee()->IgnoreParenImpCasts());
  const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  const Expr *Init = Local && Local->hasLocalStorage() &&
                             !isa<ParmVarDecl>(Local) && Local->getInit() &&
                             !isChangedVariable(Local)
                         ? Local->getInit()->IgnoreParenImpCasts()
                         : nullptr;
  if (const auto *Address = dyn_cast_or_null<UnaryOperator>(Init);
      Address && Address->getOpcode() == UO_AddrOf)
    Init = Address->getSubExpr()->IgnoreParenImpCasts();
  const auto *Named = dyn_cast_or_null<DeclRefExpr>(Init);
  return Named ? dyn_cast<FunctionDecl>(Named->getDecl()) : nullptr;
}

unsigned startedPhases(const Stmt *S, const ASTContext &Context) {
  unsigned Phases = 0;
  // The helpers followed, each with which of its parameters are given
  // another object, since a helper given another object first, `fs_Stop(
  // _pOther); fs_Stop(this);`, starts the phases of the own one when it is
  // given that.
  llvm::DenseSet<std::pair<const FunctionDecl *, uint64_t>> Helpers;
  llvm::DenseMap<const FunctionDecl *, unsigned> MaskCounts;
  // The parameters of the helpers followed that are given another object
  // than the own, `fs_Stop(_pOther)` of `fs_Stop(CApp *_pThis)`.
  llvm::SmallPtrSet<const ParmVarDecl *, 4> Foreign;
  // The expressions of the default arguments and default member initializers
  // looked at.
  llvm::SmallPtrSet<const Expr *, 8> VisitedDefaults;
  // Whether what is looked into is of an object made or destroyed where it
  // is, `COther Other;`, whose `this` is that object, not the own, until a
  // member function of the own one is called in it.
  bool ThisForeign = false;
  auto Own = [&](const Expr *E) {
    const Expr *Bare = E ? E->IgnoreParenImpCasts() : nullptr;
    if (ThisForeign && isa_and_nonnull<CXXThisExpr>(
                           Bare ? Bare->IgnoreParenImpCasts() : nullptr))
      return false;
    // Also through unchanged local aliases of it, `auto *pAlias = _pThis`.
    for (int Depth = 0; Bare && Depth < 8; ++Depth) {
      if (const auto *Deref = dyn_cast<UnaryOperator>(Bare);
          Deref && (Deref->getOpcode() == UO_Deref ||
                    Deref->getOpcode() == UO_AddrOf)) {
        Bare = Deref->getSubExpr()->IgnoreParenImpCasts();
        continue;
      }
      const auto *Ref = dyn_cast<DeclRefExpr>(Bare);
      const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      if (!Local || isa<ParmVarDecl>(Local) || !Local->hasLocalStorage() ||
          !Local->getInit() || isChangedVariable(Local) ||
          !(Local->getType()->isPointerType() ||
            Local->getType()->isReferenceType()))
        break;
      Bare = Local->getInit()->IgnoreParenImpCasts();
    }
    if (const auto *Ref = dyn_cast_or_null<DeclRefExpr>(Bare);
        Ref && isa<ParmVarDecl>(Ref->getDecl()) &&
        Foreign.contains(cast<ParmVarDecl>(Ref->getDecl())))
      return false;
    return isOwnActorObject(E);
  };
  std::function<void(const Stmt *)> Started;
  // What destroying an object of \p Record runs: its destructor's body, and
  // the destructors of its members and bases, each class once.
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> DestroyedRecords;
  std::function<void(const CXXRecordDecl *)> Destroyed = [&](const CXXRecordDecl
                                                                 *Record) {
    if (!Record || !Record->hasDefinition())
      return;
    Record = Record->getDefinition();
    if (!DestroyedRecords.insert(Record).second)
      return;
    if (const CXXDestructorDecl *Destructor = Record->getDestructor())
      if (const FunctionDecl *Definition = Destructor->getDefinition();
          Definition && Definition->getBody() &&
          Helpers.insert({Definition, 0}).second)
        Started(Definition->getBody());
    for (const FieldDecl *Field : Record->fields())
      if (!Field->getType()->isReferenceType())
        Destroyed(
            Field->getType()->getBaseElementTypeUnsafe()->getAsCXXRecordDecl());
    for (const CXXBaseSpecifier &Base : Record->bases())
      Destroyed(Base.getType()->getAsCXXRecordDecl());
    for (const CXXBaseSpecifier &Base : Record->vbases())
      Destroyed(Base.getType()->getAsCXXRecordDecl());
  };
  // The helpers to follow, each with what is of another object in it: one is
  // followed after what calls it rather than within it, so that a chain of
  // helpers that each call the next, as long as it is, doesn't take a stack
  // as deep.
  struct CPending {
    const FunctionDecl *Definition;
    llvm::SmallPtrSet<const ParmVarDecl *, 4> Foreign;
    bool ThisForeign;
  };
  llvm::SmallVector<CPending, 0> Pending;
  // Follows the helper \p Definition given \p Arguments, from \p Offset on
  // for its parameters: what it is given of another object is that object in
  // it. A constructor also runs its member initializers.
  // With \p ForeignThis, of another object than the own, `this` in it.
  auto Follow = [&](const FunctionDecl *Definition,
                    llvm::ArrayRef<const Expr *> Arguments, unsigned Offset,
                    bool ForeignThis) {
    llvm::SmallVector<const ParmVarDecl *, 4> Given;
    uint64_t Mask = 0;
    for (unsigned iArgument = Offset;
         iArgument < Arguments.size() &&
         iArgument - Offset < Definition->getNumParams();
         ++iArgument) {
      const ParmVarDecl *Parameter =
          Definition->getParamDecl(iArgument - Offset);
      const QualType Type = Parameter->getType();
      if ((Type->isPointerType() || Type->isReferenceType()) &&
          Type->getPointeeType()->getAsCXXRecordDecl() &&
          !Own(Arguments[iArgument])) {
        Given.push_back(Parameter);
        Mask |= uint64_t(1) << std::min(iArgument - Offset, 63u);
      }
    }
    // A helper is followed with at most a few sets of the parameters given
    // another object, and then with none, which can only start more, so that
    // helpers given others in many ways, like templates that call each other
    // with each, aren't followed once per way.
    if (!Helpers.contains({Definition, Mask}) &&
        ++MaskCounts[Definition] > 4) {
      Mask = 0;
      Given.clear();
    }
    if (!Helpers.insert({Definition, Mask}).second)
      return;
    // In it, the parameters given the own object are the own object, and
    // what is of another object where it is called stays so, like the
    // parameters of the function a lambda it is the call operator of is in.
    CPending Item{Definition, Foreign, ForeignThis};
    for (const ParmVarDecl *Parameter : Definition->parameters())
      Item.Foreign.erase(Parameter);
    Item.Foreign.insert_range(Given);
    Pending.push_back(std::move(Item));
  };
  Started = [&](const Stmt *S) {
    // Not what isn't evaluated, `noexcept(f_StopApp())`.
    if (!S || isa<UnaryExprOrTypeTraitExpr, CXXNoexceptExpr>(S))
      return;
    if (const auto *Typeid = dyn_cast<CXXTypeidExpr>(S);
        Typeid && !Typeid->isPotentiallyEvaluated())
      return;
    // Setting the flag of a phase of the own object starts the phase,
    // `m_bStopping = true`, also of a member of it, `mp_State.m_bStopping`.
    const Expr *Assigned = nullptr;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->isAssignmentOp())
      Assigned = Assign->getLHS();
    else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(S);
             Operator && Operator->isAssignmentOp() &&
             Operator->getNumArgs() >= 1)
      Assigned = Operator->getArg(0);
    if (const auto *Member =
            Assigned ? dyn_cast<MemberExpr>(Assigned->IgnoreParenImpCasts())
                     : nullptr)
      if (const std::optional<unsigned> Flagged = annotatedPhases(
              Member->getMemberDecl(), AnnotationActorPhaseFlag)) {
        const Expr *Object = Member->getBase()->IgnoreParenImpCasts();
        while (const auto *Outer = dyn_cast<MemberExpr>(Object)) {
          if (Outer->isArrow() &&
              !isa<CXXThisExpr>(Outer->getBase()->IgnoreParenImpCasts()))
            break;
          Object = Outer->getBase()->IgnoreParenImpCasts();
        }
        if (Own(Object))
          Phases |= *Flagged;
      }
    // Also a static flag named by itself, `ms_bStopping = true`.
    if (const auto *Ref =
            Assigned ? dyn_cast<DeclRefExpr>(Assigned->IgnoreParenImpCasts())
                     : nullptr;
        Ref && isa<VarDecl>(Ref->getDecl()) &&
        cast<VarDecl>(Ref->getDecl())->isStaticDataMember())
      if (const std::optional<unsigned> Flagged =
              annotatedPhases(Ref->getDecl(), AnnotationActorPhaseFlag))
        Phases |= *Flagged;
    // Making a lambda runs what initializes its captures, and its body can
    // run where it goes, unless it goes into a local that isn't used, `auto
    // fUnused = [this] { f_StopApp(); };`.
    if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
      for (const Expr *Init : Lambda->capture_inits())
        Started(Init);
      DynTypedNode Node = DynTypedNode::create(*Lambda);
      const VarDecl *Into = nullptr;
      for (int Depth = 0; Depth < 8 && !Into; ++Depth) {
        const auto Parents =
            instantiatedParents(Node, const_cast<ASTContext &>(Context));
        if (Parents.empty())
          break;
        Into = Parents[0].get<VarDecl>();
        if (!Into && !(Parents[0].get<ImplicitCastExpr>() ||
                       Parents[0].get<MaterializeTemporaryExpr>() ||
                       Parents[0].get<CXXBindTemporaryExpr>() ||
                       Parents[0].get<ExprWithCleanups>() ||
                       Parents[0].get<ParenExpr>() ||
                       Parents[0].get<CXXConstructExpr>()))
          break;
        Node = Parents[0];
      }
      // Nor one of a scope exit whose guard is cleared before it can run,
      // `auto Rollback = g_OnScopeExit / [this] { ... }; Rollback.f_Clear();`.
      auto Cancelled = [&] {
        DynTypedNode Up = DynTypedNode::create(*Lambda);
        const VarDecl *Guard = nullptr;
        for (int Depth = 0; Depth < 16 && !Guard; ++Depth) {
          const auto Parents =
              instantiatedParents(Up, const_cast<ASTContext &>(Context));
          if (Parents.empty() ||
              !(Parents[0].get<Expr>() || Parents[0].get<VarDecl>()))
            return false;
          Guard = Parents[0].get<VarDecl>();
          Up = Parents[0];
        }
        if (!Guard || !Guard->hasLocalStorage() ||
            scopeExitLambda(Guard) != Lambda)
          return false;
        const auto Declarations =
            instantiatedParents(*Guard, const_cast<ASTContext &>(Context));
        const auto *Declaration =
            Declarations.empty() ? nullptr : Declarations[0].get<DeclStmt>();
        const auto Blocks =
            Declaration ? instantiatedParents(*Declaration,
                                              const_cast<ASTContext &>(Context))
                        : decltype(Declarations){};
        const auto *Block =
            Blocks.empty() ? nullptr : Blocks[0].get<CompoundStmt>();
        std::function<bool(const Stmt *)> Leaves = [&](const Stmt *Inner) {
          if (!Inner || isa<LambdaExpr>(Inner))
            return false;
          if (isa<ReturnStmt, CoreturnStmt, BreakStmt, ContinueStmt, GotoStmt,
                  IndirectGotoStmt, CoroutineSuspendExpr>(Inner))
            return true;
          return llvm::any_of(childrenOnce(Inner), Leaves);
        };
        return isClearedBeforeLeaving(Guard, Block, Leaves);
      };
      if ((!Into || !Into->hasLocalStorage() || Into->isReferenced()) &&
          !Cancelled())
        Started(Lambda->getBody());
      return;
    }
    if (const auto *Call = dyn_cast<CallExpr>(S)) {
      const FunctionDecl *Callee = Call->getDirectCallee();
      const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
      // Through a pointer to a function, `fStop(this)`: the one an unchanged
      // local or capture it is read from was made from, `auto fStop =
      // &fg_Stop`, or any, when it is given the object.
      if (!Callee && !Member && !isa<CXXOperatorCallExpr>(Call)) {
        Callee = calleeThroughPointer(Call);
        if (!Callee &&
            llvm::any_of(Call->arguments(),
                         [&](const Expr *Argument) { return Own(Argument); }))
          Phases = ~0u;
      }
      // Through a pointer to a member function of the object,
      // `(this->*fStop)()`: the one an unchanged local or capture it is read
      // from was made from, `auto fStop = &CApp::f_StopNow`, or any.
      if (const auto *Pointer =
              Member && !Callee
                  ? dyn_cast<BinaryOperator>(Member->getCallee()->IgnoreParens())
                  : nullptr;
          Pointer && Pointer->isPtrMemOp()) {
        const Expr *Object = Pointer->getLHS()->IgnoreParenImpCasts();
        if (const auto *Deref = dyn_cast<UnaryOperator>(Object);
            Deref && Deref->getOpcode() == UO_Deref)
          Object = Deref->getSubExpr();
        if (Own(Object)) {
          const auto *Ref =
              dyn_cast<DeclRefExpr>(Pointer->getRHS()->IgnoreParenImpCasts());
          const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
          const auto *Address =
              Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
                      Local->getInit() && !isChangedVariable(Local)
                  ? dyn_cast<UnaryOperator>(
                        Local->getInit()->IgnoreParenImpCasts())
                  : nullptr;
          const auto *Named =
              Address && Address->getOpcode() == UO_AddrOf
                  ? dyn_cast<DeclRefExpr>(Address->getSubExpr())
                  : nullptr;
          Callee = Named ? dyn_cast<FunctionDecl>(Named->getDecl()) : nullptr;
          if (!Callee)
            Phases = ~0u;
        }
      }
      // Not a phase of another actor, `_pOther->f_StopApp()`, also of a
      // static or free one given it, `fs_StopOther(Other)` with a
      // `TCActor<CApp> Other`, unless it is given the own object too,
      // `fs_Stop(this, pOther)`, which it is followed with.
      if (Callee && Member && Member->getImplicitObjectArgument() &&
          !Own(Member->getImplicitObjectArgument()))
        Callee = nullptr;
      if (Callee && !Member &&
          (!isa<CXXMethodDecl>(Callee) ||
           cast<CXXMethodDecl>(Callee)->isStatic()) &&
          givenAnotherActor(Call, const_cast<ASTContext &>(Context)) &&
          llvm::none_of(Call->arguments(),
                        [&](const Expr *Argument) { return Own(Argument); }))
        Callee = nullptr;
      // A virtual one can run any of its overrides.
      llvm::SmallVector<const FunctionDecl *, 4> Callees;
      if (Callee && Member && Member->getMethodDecl())
        for (const CXXMethodDecl *Method : dispatchedMethods(Member))
          Callees.push_back(Method);
      else if (Callee)
        Callees.push_back(Callee);
      for (const FunctionDecl *Candidate : Callees) {
        std::optional<unsigned> Runs =
            annotatedPhases(Candidate, AnnotationActorPhaseEntry);
        if (!Runs)
          Runs = annotatedPhases(Candidate, AnnotationActorPhase);
        if (Runs)
          Phases |= *Runs;
        else if (const FunctionDecl *Definition = Candidate->getDefinition();
                 Definition && Definition->getBody()) {
          // Of a member function of the own object, `this` is it.
          Follow(
              Definition,
              llvm::ArrayRef<const Expr *>(Call->getArgs(), Call->getNumArgs()),
              isa<CXXOperatorCallExpr>(Call) && hasImplicitObject(Definition)
                  ? 1
                  : 0,
              ThisForeign && !Member);
        }
      }
    }
    // What making an object runs, its constructor, `CStop Stop(this)` with
    // `CStop(CApp *_pThis) { _pThis->f_StopNow(); }`, also its member
    // initializers, and its destructor, which runs when it goes, also of an
    // aggregate, `CStop Stop{this}`, and the destructors of its members and
    // bases, which the destructor runs after its body.
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(S)) {
      const CXXConstructorDecl *Constructor = Construct->getConstructor();
      const FunctionDecl *Definition =
          Constructor ? Constructor->getDefinition() : nullptr;
      // Of a whole object, not a base of the own, `CApp() : CBase()`.
      const bool Complete =
          Construct->getConstructionKind() == CXXConstructionKind::Complete;
      if (Definition)
        Follow(Definition,
               llvm::ArrayRef<const Expr *>(Construct->getArgs(),
                                            Construct->getNumArgs()),
               0, ThisForeign || Complete);
      const llvm::SaveAndRestore Restore(ThisForeign, ThisForeign || Complete);
      Destroyed(Construct->getType()->getAsCXXRecordDecl());
    } else if (const auto *List = dyn_cast<InitListExpr>(S);
               List && List->isSemanticForm()) {
      const llvm::SaveAndRestore Restore(ThisForeign, true);
      Destroyed(List->getType()->getAsCXXRecordDecl());
    }
    // A default initializer of a member, `int m_Unused = (f_StopNow(), 0)`,
    // which a constructor runs, and a default argument, each once: the uses
    // of one share it, also those in other default arguments, `int f1(int =
    // f0() + f0());`, and it can't name what differs between them, like the
    // object or the parameters of a helper.
    if (const auto *Default = dyn_cast<CXXDefaultInitExpr>(S);
        Default && VisitedDefaults.insert(Default->getExpr()).second)
      Started(Default->getExpr());
    if (const auto *Default = dyn_cast<CXXDefaultArgExpr>(S);
        Default && VisitedDefaults.insert(Default->getExpr()).second)
      Started(Default->getExpr());
    for (const Stmt *Child : evaluatedChildren(S, Context))
      Started(Child);
  };
  Started(S);
  while (!Pending.empty()) {
    CPending Item = Pending.pop_back_val();
    Foreign = std::move(Item.Foreign);
    ThisForeign = Item.ThisForeign;
    if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Item.Definition))
      for (const CXXCtorInitializer *Initializer : Constructor->inits())
        Started(Initializer->getInit());
    Started(Item.Definition->getBody());
  }
  return Phases;
}

unsigned destructionStartedPhases(const CXXRecordDecl *Record,
                                  const ASTContext &Context) {
  unsigned Phases = 0;
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
  std::function<void(const CXXRecordDecl *)> Visit = [&](const CXXRecordDecl
                                                             *Class) {
    if (!Class || !Class->hasDefinition())
      return;
    Class = Class->getDefinition();
    if (!Seen.insert(Class).second)
      return;
    if (const CXXDestructorDecl *Destructor = Class->getDestructor())
      if (const FunctionDecl *Definition = Destructor->getDefinition();
          Definition && Definition->getBody())
        Phases |= startedPhases(Definition->getBody(), Context);
    for (const FieldDecl *Field : Class->fields())
      if (!Field->getType()->isReferenceType())
        Visit(
            Field->getType()->getBaseElementTypeUnsafe()->getAsCXXRecordDecl());
    for (const CXXBaseSpecifier &Base : Class->bases())
      Visit(Base.getType()->getAsCXXRecordDecl());
    for (const CXXBaseSpecifier &Base : Class->vbases())
      Visit(Base.getType()->getAsCXXRecordDecl());
  };
  Visit(Record);
  return Phases;
}

/// The phases the guard of the call \p Call guards, an annotated guard of the
/// own actor or `fg_OnResume` with a callback that tests the flags.
bool givenAnotherActor(const CallExpr *Call, ASTContext &Context) {
  // An object it is given by pointer or reference, which it can read.
  auto Designated = [](const Expr *Argument) -> const CXXRecordDecl * {
    QualType Type = Argument->getType();
    if (Type->isPointerType())
      Type = Type->getPointeeType();
    else if (!Argument->isGLValue())
      return nullptr;
    return Type->getAsCXXRecordDecl();
  };
  if (llvm::any_of(Call->arguments(), [&](const Expr *Argument) {
        QualType Type = Argument->getType().getNonReferenceType();
        if (Type->isPointerType())
          Type = Type->getPointeeType();
        const StringRef Name = recordName(Type);
        if (Name == "TCActor" || Name == "TCWeakActor")
          return !isOwnActor(Argument, Context);
        const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
        return Record && isActorClass(Record) &&
               !isOwnActorObject(Argument);
      }))
    return true;
  // Also only objects that aren't of the own actor, like a copy of its
  // state, `fg_IsStopping(&State)` of `auto State = mp_State`: only the live
  // object of the own actor tells its phases.
  bool Own = false;
  bool Other = false;
  for (const Expr *Argument : Call->arguments()) {
    if (!Designated(Argument))
      continue;
    if (isOwnActorObject(Argument))
      Own = true;
    else
      Other = true;
  }
  return Other && !Own;
}

static unsigned guardedPhasesOfCall(const Expr *Operand) {
  if (const auto *Call = dyn_cast<CallExpr>(Operand)) {
    if (const FunctionDecl *Callee = Call->getDirectCallee()) {
      if (std::optional<unsigned> Phases =
              annotatedPhases(Callee, AnnotationActorPhaseGuard)) {
        // A guard of another actor doesn't guard this one, also one given
        // the actor, like a static `fs_Guard(pOther)` or `fs_Guard(Other)`
        // of a handle.
        if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
            Member && !isOwnActorObject(Member->getImplicitObjectArgument()))
          return 0;
        if (givenAnotherActor(Call, Callee->getASTContext()))
          return 0;
        return *Phases & ~ActorPhase_Owner;
      }
    }
  }
  if (const CallExpr *Call = asCallTo(Operand, {"fg_OnResume"})) {
    unsigned Phases = 0;
    for (const Expr *Argument : Call->arguments()) {
      if (const LambdaExpr *Lambda = asLambda(Argument))
        Phases |= guardedPhases(Lambda);
    }
    return Phases;
  }
  return 0;
}

unsigned guardedPhasesOfHolder(const Expr *Init, const ASTContext *Context) {
  if (!Init)
    return 0;
  // What every value guards, of a conditional, `bChoose ? co_await A :
  // co_await B`.
  std::optional<unsigned> Phases;
  // Also through an awaiter made before, `co_await fg_Move(Awaiter)`, and
  // either value awaited, `co_await (bChoose ? A : B)`.
  for (const CoroutineSuspendExpr *Await : holderAwaits(Init, Context)) {
    if (!Await) {
      Phases = 0;
      continue;
    }
    for (const Expr *Value : awaitedValues(Await->getOperand())) {
      const unsigned Guarded = Value ? guardedPhasesOfCall(Value) : 0;
      Phases = Phases ? *Phases & Guarded : Guarded;
    }
  }
  return Phases.value_or(0);
}

const CallExpr *ownPhaseGuardCall(const Expr *Init, const ASTContext *Context) {
  if (!Init)
    return nullptr;
  for (const CoroutineSuspendExpr *Await : holderAwaits(Init, Context))
    for (const Expr *Value : Await ? awaitedValues(Await->getOperand())
                                   : llvm::SmallVector<const Expr *, 2>()) {
      const auto *Call = dyn_cast_or_null<CallExpr>(Value);
      const FunctionDecl *Callee = Call ? Call->getDirectCallee() : nullptr;
      if (!Callee || !annotatedPhases(Callee, AnnotationActorPhaseGuard))
        continue;
      // A member function of the own actor, or one given it, reads it.
      if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call)) {
        if (isOwnActorObject(Member->getImplicitObjectArgument()))
          return Call;
        continue;
      }
      if (llvm::any_of(Call->arguments(), [](const Expr *Argument) {
            return isOwnActorObject(Argument);
          }))
        return Call;
    }
  return nullptr;
}

void splitOptionList(StringRef List, llvm::SmallVectorImpl<StringRef> &Out) {
  llvm::SmallVector<StringRef, 16> Parts;
  List.split(Parts, ';', -1, false);
  for (StringRef Part : Parts) {
    if (!Part.trim().empty())
      Out.push_back(Part.trim());
  }
}

bool matchesNamePattern(StringRef Name, llvm::ArrayRef<StringRef> Patterns) {
  for (StringRef Pattern : Patterns) {
    if (Pattern.consume_back("*") ? Name.starts_with(Pattern) : Name == Pattern)
      return true;
  }
  return false;
}

/// The local variables that \p Key designates, through moves, copies like
/// `auto Copy = Key`, and the captures of lambdas that copy it: the last is
/// the one it was made as.
static llvm::SmallVector<const VarDecl *, 4> keyChain(const Expr *Key) {
  llvm::SmallVector<const VarDecl *, 4> Chain;
  if (!Key)
    return Chain;
  const Expr *Inner = skipWrappers(Key)->IgnoreParenImpCasts();
  if (const auto *Call = dyn_cast<CallExpr>(Inner);
      Call && Call->getNumArgs() == 1 && isMoveFunction(calleeName(Call)))
    Inner = skipWrappers(Call->getArg(0))->IgnoreParenImpCasts();
  const auto *Ref = dyn_cast<DeclRefExpr>(Inner);
  const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  for (int Depth = 0; Variable && Depth < 8; ++Depth) {
    Chain.push_back(Variable);
    if (!Variable->getInit() || isa<ParmVarDecl>(Variable) ||
        Variable->getType()->isReferenceType())
      break;
    const auto *Source = dyn_cast<DeclRefExpr>(
        skipWrappers(Variable->getInit())->IgnoreParenImpCasts());
    if (!Source)
      break;
    Variable = dyn_cast<VarDecl>(Source->getDecl());
  }
  return Chain;
}

const VarDecl *keyVariable(const Expr *Key) {
  const auto Chain = keyChain(Key);
  return Chain.empty() ? nullptr : Chain.back();
}

/// The object whose collection \p E designates: the base of the member, also
/// through local references and pointers to it.
static const Expr *collectionObject(const Expr *E, int Depth = 0) {
  if (!E || Depth > 4)
    return nullptr;
  E = E->IgnoreParenImpCasts();
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary && (Unary->getOpcode() == UO_Deref ||
                Unary->getOpcode() == UO_AddrOf))
    return collectionObject(Unary->getSubExpr(), Depth + 1);
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    const auto *Alias = dyn_cast<VarDecl>(Ref->getDecl());
    return Alias && Alias->getInit() && !isa<ParmVarDecl>(Alias)
               ? collectionObject(skipWrappers(Alias->getInit()), Depth + 1)
               : nullptr;
  }
  if (const auto *Member = dyn_cast<MemberExpr>(E))
    return Member->getBase()->IgnoreParenImpCasts();
  return nullptr;
}

/// Whether \p E is the root of the own object: `this`, what is named for it,
/// like `pThis`, or an unchanged local copy of one, like `auto *pSelf = this`.
static bool isOwnRoot(const Expr *E, int Depth = 0) {
  if (!E || Depth > 4)
    return false;
  E = E->IgnoreParenImpCasts();
  // Also cast to a base, `static_cast<CBase *>(this)`, the same object.
  while (const auto *Cast = dyn_cast<ExplicitCastExpr>(E)) {
    if (Cast->getCastKind() != CK_DerivedToBase &&
        Cast->getCastKind() != CK_UncheckedDerivedToBase &&
        Cast->getCastKind() != CK_NoOp)
      break;
    E = Cast->getSubExpr()->IgnoreParenImpCasts();
  }
  if (isa<CXXThisExpr>(E))
    return true;
  const auto *Ref = dyn_cast<DeclRefExpr>(E);
  if (!Ref)
    return false;
  // Named for it, and not changed, also a parameter given it, `_pActor` of a
  // static or free function.
  if ((Ref->getDecl()->getIdentifier() &&
       Ref->getDecl()->getName().ends_with("This")) ||
      isActorObjectParameter(Ref->getDecl()))
    return !isa<VarDecl>(Ref->getDecl()) || isUnchangedAt(Ref);
  const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
  return Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
         Local->getInit() && Local->getType()->isPointerType() &&
         !isChangedVariable(Local) && isUnchangedAt(Ref) &&
         isOwnRoot(Local->getInit(), Depth + 1);
}

/// Like isSameCollectionObject, \p Depth steps into \p Left and \p Right,
/// which a reference bound to what is reached through itself, `CApp &App =
/// App.m_Self`, would make endless.
static bool isSameCollectionObjectImpl(const Expr *Left, const Expr *Right,
                                       int Depth) {
  if (!Left || !Right || Depth > 64)
    return false;
  // What a local reference is bound to, `auto &State = mp_State`.
  auto Resolve = [](const Expr *E) {
    E = E->IgnoreParenImpCasts();
    for (int Depth = 0; Depth < 4; ++Depth) {
      const auto *Ref = dyn_cast<DeclRefExpr>(E);
      const auto *Alias = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      if (!Alias || !Alias->hasLocalStorage() || isa<ParmVarDecl>(Alias) ||
          !Alias->getType()->isReferenceType() || !Alias->getInit())
        break;
      E = Alias->getInit()->IgnoreParenImpCasts();
    }
    // The object itself, `*this`, also through a reference to it, `auto &App
    // = *this`, is `this`.
    if (const auto *Deref = dyn_cast<UnaryOperator>(E);
        Deref && Deref->getOpcode() == UO_Deref &&
        isa<CXXThisExpr>(Deref->getSubExpr()->IgnoreParenImpCasts()))
      E = Deref->getSubExpr()->IgnoreParenImpCasts();
    return E;
  };
  Left = Resolve(Left);
  Right = Resolve(Right);
  if (isOwnRoot(Left) && isOwnRoot(Right))
    return true;
  // The same member of the same object, like `Left.mp_Items`.
  if (const auto *LeftMember = dyn_cast<MemberExpr>(Left)) {
    const auto *RightMember = dyn_cast<MemberExpr>(Right);
    return RightMember &&
           LeftMember->getMemberDecl()->getCanonicalDecl() ==
               RightMember->getMemberDecl()->getCanonicalDecl() &&
           isSameCollectionObjectImpl(LeftMember->getBase(),
                                      RightMember->getBase(), Depth + 1);
  }
  // The same element of the same array, `mp_States[0]`, at a constant index.
  if (const auto *LeftElement = dyn_cast<ArraySubscriptExpr>(Left)) {
    const auto *RightElement = dyn_cast<ArraySubscriptExpr>(Right);
    if (!RightElement)
      return false;
    const auto *LeftIndex = dyn_cast<IntegerLiteral>(
        LeftElement->getIdx()->IgnoreParenImpCasts());
    const auto *RightIndex = dyn_cast<IntegerLiteral>(
        RightElement->getIdx()->IgnoreParenImpCasts());
    return LeftIndex && RightIndex &&
           LeftIndex->getValue() == RightIndex->getValue() &&
           isSameCollectionObjectImpl(LeftElement->getBase(),
                                      RightElement->getBase(), Depth + 1);
  }
  if (const auto *LeftUnary = dyn_cast<UnaryOperator>(Left)) {
    const auto *RightUnary = dyn_cast<UnaryOperator>(Right);
    return RightUnary && LeftUnary->getOpcode() == RightUnary->getOpcode() &&
           isSameCollectionObjectImpl(LeftUnary->getSubExpr(),
                                      RightUnary->getSubExpr(), Depth + 1);
  }
  // What the same owning pointer points to, `mp_pState->mp_Items`.
  if (const auto *LeftCall = dyn_cast<CXXOperatorCallExpr>(Left);
      LeftCall && (LeftCall->getOperator() == OO_Arrow ||
                   LeftCall->getOperator() == OO_Star) &&
      LeftCall->getNumArgs() == 1) {
    const auto *RightCall = dyn_cast<CXXOperatorCallExpr>(Right);
    return RightCall && RightCall->getOperator() == LeftCall->getOperator() &&
           RightCall->getNumArgs() == 1 &&
           isSameCollectionObjectImpl(LeftCall->getArg(0), RightCall->getArg(0),
                                      Depth + 1);
  }
  // The same variable, which keeps pointing to the same object.
  const auto *LeftRef = dyn_cast<DeclRefExpr>(Left);
  const auto *RightRef = dyn_cast<DeclRefExpr>(Right);
  if (!LeftRef || !RightRef || LeftRef->getDecl() != RightRef->getDecl())
    return false;
  const auto *Variable = dyn_cast<VarDecl>(LeftRef->getDecl());
  return !Variable || Variable->getType()->isReferenceType() ||
         !Variable->getType()->isPointerType() || !isChangedVariable(Variable);
}

bool isSameCollectionObject(const Expr *Left, const Expr *Right) {
  return isSameCollectionObjectImpl(Left, Right, 0);
}

/// Whether \p E designates \p Field, the collection itself.
static bool isFieldMember(const Expr *E, const FieldDecl *Field,
                          int Depth = 0) {
  if (!E || Depth > 4)
    return false;
  E = E->IgnoreParenImpCasts();
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary && Unary->getOpcode() == UO_Deref)
    return isFieldMember(Unary->getSubExpr(), Field, Depth + 1);
  // A local reference or pointer to the collection, like `auto &Items =
  // mp_Items`.
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    const auto *Alias = dyn_cast<VarDecl>(Ref->getDecl());
    if (!Alias || !Alias->hasLocalStorage() || !Alias->getInit() ||
        isa<ParmVarDecl>(Alias))
      return false;
    if (Alias->getType()->isReferenceType())
      return isFieldMember(Alias->getInit(), Field, Depth + 1);
    if (Alias->getType()->isPointerType() && !isChangedVariable(Alias)) {
      const auto *Address = dyn_cast<UnaryOperator>(
          skipWrappers(Alias->getInit())->IgnoreParenImpCasts());
      return Address && Address->getOpcode() == UO_AddrOf &&
             isFieldMember(Address->getSubExpr(), Field, Depth + 1);
    }
    return false;
  }
  const auto *Member = dyn_cast<MemberExpr>(E);
  return Member && Member->getMemberDecl()->getCanonicalDecl() ==
                       Field->getCanonicalDecl();
}

namespace {
/// Finds where a function adds an entry to a field by a key variable.
class InsertionFinder : public OncePerTypeVisitor<InsertionFinder> {
public:
  InsertionFinder(const VarDecl *Key, const FieldDecl *Field,
                  const Expr *Object)
      : Key(Key), Field(Field), Object(Object) {}

  bool shouldVisitLambdaBody() const { return true; }

  bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *Call) {
    if ((Call->getOperator() == OO_Subscript ||
         Call->getOperator() == OO_Call) &&
        Call->getNumArgs() >= 2 && isFieldMember(Call->getArg(0), Field) &&
        isSameCollectionObject(collectionObject(Call->getArg(0)), Object) &&
        keyVariable(Call->getArg(1)) == Key)
      Found = true;
    return !Found;
  }

  bool VisitCXXMemberCallExpr(CXXMemberCallExpr *Call) {
    const CXXMethodDecl *Method = Call->getMethodDecl();
    if (Method && Method->getIdentifier() && Call->getNumArgs() >= 1 &&
        matchesNamePattern(Method->getName(), {"f_Insert*", "f_Emplace*"}) &&
        isFieldMember(Call->getImplicitObjectArgument(), Field) &&
        isSameCollectionObject(
            collectionObject(Call->getImplicitObjectArgument()), Object) &&
        keyVariable(Call->getArg(0)) == Key)
      Found = true;
    return !Found;
  }

  bool Found = false;

private:
  const VarDecl *Key;
  const FieldDecl *Field;
  const Expr *Object;
};
} // namespace

/// Whether \p Key may be changed after it is initialized in \p Body: assigned,
/// incremented, or passed on by a non-const reference, other than to a lookup
/// or removal of \p Field, like `mp_Items.f_Remove(Key)` with a forwarding
/// reference, also through `fg_Move`.
static bool isKeyChanged(const VarDecl *Key, const FieldDecl *Field,
                         const Stmt *Body, ASTContext &Context) {
  bool Changed = false;
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    // Not what isn't evaluated, `sizeof(mp_Items[fg_Move(Key)])`.
    if (!S || Changed || isa<UnaryExprOrTypeTraitExpr, CXXNoexceptExpr>(S))
      return;
    if (const auto *Typeid = dyn_cast<CXXTypeidExpr>(S);
        Typeid && !Typeid->isPotentiallyEvaluated())
      return;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S); Ref && Ref->getDecl() == Key) {
      DynTypedNode Node = DynTypedNode::create(*Ref);
      // Moved, what it is given to can consume it, like `mp_Items[fg_Move(Key)]`.
      bool Moved = false;
      for (int Depth = 0; Depth < 8; ++Depth) {
        const auto Parents = instantiatedParents(Node, Context);
        if (Parents.empty())
          return;
        const DynTypedNode &Parent = Parents[0];
        const auto *Child = Node.get<Expr>();
        if (Parent.get<ImplicitCastExpr>() || Parent.get<ParenExpr>() ||
            Parent.get<MaterializeTemporaryExpr>()) {
          if (const auto *Cast = Parent.get<ImplicitCastExpr>();
              Cast && Cast->getCastKind() == CK_LValueToRValue)
            return;
          Node = Parent;
          continue;
        }
        // A cast to a reference designates it, `static_cast<CStr &>(Key) =
        // Other`; one to a value copies it.
        if (const auto *Cast = Parent.get<ExplicitCastExpr>()) {
          if (!Cast->isGLValue())
            return;
          Node = Parent;
          continue;
        }
        // Either value of a conditional that is assigned, `(bChoose ? Key :
        // Spare) = Other`, or the last of a comma.
        if (const auto *Conditional =
                Parent.get<AbstractConditionalOperator>();
            Conditional && Child != Conditional->getCond() &&
            Conditional->isGLValue()) {
          Node = Parent;
          continue;
        }
        if (const auto *Comma = Parent.get<BinaryOperator>();
            Comma && Comma->getOpcode() == BO_Comma &&
            Child == Comma->getRHS() && Comma->isGLValue()) {
          Node = Parent;
          continue;
        }
        if (const auto *Binary = Parent.get<BinaryOperator>()) {
          Changed = Binary->isAssignmentOp() && Binary->getLHS() == Child;
          return;
        }
        if (const auto *Unary = Parent.get<UnaryOperator>()) {
          Changed = Unary->isIncrementDecrementOp() ||
                    (Unary->getOpcode() == UO_AddrOf &&
                     !Unary->getType()->getPointeeType().isConstQualified());
          return;
        }
        // Assigning it, like `Key = Other`, or calling what changes it.
        if (const auto *Operator = Parent.get<CXXOperatorCallExpr>();
            Operator && Operator->isAssignmentOp() &&
            Operator->getNumArgs() >= 1 && Operator->getArg(0) == Child) {
          Changed = true;
          return;
        }
        if (const auto *Member = Parent.get<MemberExpr>()) {
          const auto *Method = dyn_cast<CXXMethodDecl>(Member->getMemberDecl());
          Changed = Method && !Method->isConst();
          return;
        }
        if (const auto *Call = Parent.get<CallExpr>()) {
          // `fg_Move(Key)` passes it on to what the move is given to.
          if (Call->getNumArgs() == 1 && !isa<CXXOperatorCallExpr>(Call) &&
              (isMoveFunction(calleeName(Call)))) {
            Moved = true;
            Node = Parent;
            continue;
          }
          // A lookup or removal of the collection by the key.
          if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call);
              Member && isFieldMember(Member->getImplicitObjectArgument(), Field)) {
            Changed = Moved;
            return;
          }
          if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
              Operator && Operator->getNumArgs() >= 1 &&
              isFieldMember(Operator->getArg(0), Field)) {
            Changed = Moved;
            return;
          }
          const FunctionDecl *Callee = Call->getDirectCallee();
          for (unsigned iArgument = 0; iArgument < Call->getNumArgs(); ++iArgument) {
            if (Call->getArg(iArgument) != Child)
              continue;
            const unsigned Offset =
                isa<CXXOperatorCallExpr>(Call) && isa_and_nonnull<CXXMethodDecl>(Callee)
                    ? 1
                    : 0;
            if (iArgument < Offset)
              continue;
            if (!Callee || iArgument - Offset >= Callee->getNumParams()) {
              Changed = true;
              return;
            }
            const QualType Type = Callee->getParamDecl(iArgument - Offset)->getType();
            Changed = (Type->isReferenceType() &&
                       !Type.getNonReferenceType().isConstQualified()) ||
                      (Moved && !Type->isReferenceType());
          }
          return;
        }
        // Given to a constructor by a non-const reference, which can change
        // it, like `CReplaceKey Replace(Key, Existing)`.
        if (const auto *Construct = Parent.get<CXXConstructExpr>()) {
          const CXXConstructorDecl *Constructor = Construct->getConstructor();
          for (unsigned iArgument = 0; iArgument < Construct->getNumArgs();
               ++iArgument) {
            if (Construct->getArg(iArgument) != Child)
              continue;
            const QualType Type =
                iArgument < Constructor->getNumParams()
                    ? Constructor->getParamDecl(iArgument)->getType()
                    : QualType();
            Changed = Type.isNull() ||
                      (Type->isReferenceType() &&
                       !Type.getNonReferenceType().isConstQualified());
          }
          return;
        }
        if (const auto *Variable = Parent.get<VarDecl>()) {
          Changed = Variable->getType()->isReferenceType() &&
                    !Variable->getType().getNonReferenceType().isConstQualified();
          return;
        }
        return;
      }
      return;
    }
    // Nor a branch of an `if constexpr` that is discarded.
    for (const Stmt *Child : evaluatedChildren(S, Context))
      Visit(Child);
  };
  Visit(Body);
  return Changed;
}

bool isOwnEntryKey(const Expr *KeyExpr, const FieldDecl *Field,
                   const FunctionDecl *Function,
                   llvm::ArrayRef<StringRef> FreshKeyFunctions,
                   ASTContext &Context, const Expr *Object) {
  const auto Chain = keyChain(KeyExpr);
  if (Chain.empty() || !Field || !Function || !Function->getBody())
    return false;
  // Neither the key nor the copies it goes through change.
  for (const VarDecl *Key : Chain) {
    if (!Key->hasLocalStorage() || isa<ParmVarDecl>(Key) || !Key->getInit())
      return false;
    const auto *Home =
        dyn_cast_or_null<FunctionDecl>(Key->getParentFunctionOrMethod());
    if (!Home || !Home->getBody() ||
        isKeyChanged(Key, Field, Home->getBody(), Context))
      return false;
  }
  const VarDecl *Key = Chain.back();
  const auto *Init = dyn_cast<CallExpr>(
      skipWrappers(Key->getInit())->IgnoreParenImpCasts());
  if (!Init || !matchesNamePattern(calleeName(Init), FreshKeyFunctions))
    return false;
  // Fresh in the same collection, like `fg_RandomID(mp_Items)`: a key fresh
  // in another may be in this one.
  if (Init->getNumArgs() < 1 || !isFieldMember(Init->getArg(0), Field) ||
      !isSameCollectionObject(collectionObject(Init->getArg(0)), Object))
    return false;
  // Added to the same collection, of the same object.
  InsertionFinder Finder(Key, Field, Object);
  Finder.TraverseStmt(Function->getBody());
  return Finder.Found;
}

} // namespace clang::tidy::malterlib
