//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ThreadLocalStorageCheck.h"
#include "ActorAnnotations.h"
#include "ThreadLocalUtils.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <array>
#include <functional>

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

namespace {

/// Whether \p Parameter, of an instantiation of a template, is of a type
/// that depends on the template's arguments, `t_CType *_pPtr`, so that what
/// it is given is thread-local data only as the instantiation is; also one of
/// a pack.
bool dependentInPattern(const ParmVarDecl *Parameter) {
  const auto *Function = dyn_cast<FunctionDecl>(Parameter->getDeclContext());
  const FunctionDecl *Pattern =
      Function ? Function->getTemplateInstantiationPattern() : nullptr;
  if (!Pattern)
    return false;
  const unsigned Index = Parameter->getFunctionScopeIndex();
  for (unsigned iParameter = 0; iParameter < Pattern->getNumParams();
       ++iParameter) {
    const ParmVarDecl *Patterned = Pattern->getParamDecl(iParameter);
    // A pack is the parameters from it on.
    if (Patterned->isParameterPack())
      return iParameter <= Index;
    if (iParameter == Index)
      return Patterned->getType()->isDependentType();
  }
  return false;
}

/// Whether \p Field, of an instantiation of a class template, is of a type
/// that depends on the template's arguments, `t_CType *mp_pPointTo`.
bool dependentInPattern(const FieldDecl *Field) {
  const auto *Record = dyn_cast<CXXRecordDecl>(Field->getParent());
  const CXXRecordDecl *Pattern =
      Record ? Record->getTemplateInstantiationPattern() : nullptr;
  if (!Pattern || !Field->getIdentifier())
    return false;
  for (const FieldDecl *Patterned : Pattern->fields())
    if (Patterned->getIdentifier() == Field->getIdentifier())
      return Patterned->getType()->isDependentType();
  return false;
}

/// Whether one of what \p E is made of, as it is or what it refers or
/// points into, through parts, casts, moves, address-of helpers, the
/// branches of conditionals and the locals of pointers and references
/// initialized with it, is \p Is. Not past a pointer read from a part,
/// `_Other.m_pValue`, which points to what the part does, not into it.
bool anyRoot(const Expr *E, llvm::function_ref<bool(const Expr *)> Is,
             int Depth = 0) {
  if (!E || Depth > 16)
    return false;
  for (int Step = 0; Step < 32; ++Step) {
    E = E->IgnoreParens();
    const auto *Cast = dyn_cast<CastExpr>(E);
    if (const auto *Full = dyn_cast<FullExpr>(E))
      E = Full->getSubExpr();
    else if (const auto *Temporary = dyn_cast<MaterializeTemporaryExpr>(E))
      E = Temporary->getSubExpr();
    else if (Cast && Cast->getCastKind() == CK_LValueToRValue &&
             !isa<DeclRefExpr>(Cast->getSubExpr()->IgnoreParens()))
      return Is(Cast->getSubExpr()->IgnoreParens());
    else if (Cast)
      E = Cast->getSubExpr();
    else
      break;
  }
  if (const auto *List = dyn_cast<InitListExpr>(E);
      List && List->getNumInits() == 1 && !List->getType()->isRecordType() &&
      !List->getType()->isArrayType())
    return anyRoot(List->getInit(0), Is, Depth + 1);
  if (Is(E))
    return true;
  if (const auto *Unary = dyn_cast<UnaryOperator>(E);
      Unary &&
      (Unary->getOpcode() == UO_AddrOf || Unary->getOpcode() == UO_Deref))
    return anyRoot(Unary->getSubExpr(), Is, Depth + 1);
  if (const auto *Member = dyn_cast<MemberExpr>(E))
    return isa<FieldDecl>(Member->getMemberDecl()) &&
           anyRoot(Member->getBase(), Is, Depth + 1);
  if (const auto *Element = dyn_cast<ArraySubscriptExpr>(E))
    return anyRoot(Element->getBase(), Is, Depth + 1);
  if (const auto *Binary = dyn_cast<BinaryOperator>(E)) {
    if (Binary->getOpcode() == BO_Comma)
      return anyRoot(Binary->getRHS(), Is, Depth + 1);
    if (Binary->isAdditiveOp() && E->getType()->isPointerType())
      return anyRoot(Binary->getLHS()->getType()->isPointerType()
                         ? Binary->getLHS()
                         : Binary->getRHS(),
                     Is, Depth + 1);
    return false;
  }
  if (const auto *Conditional = dyn_cast<AbstractConditionalOperator>(E))
    return anyRoot(Conditional->getTrueExpr(), Is, Depth + 1) ||
           anyRoot(Conditional->getFalseExpr(), Is, Depth + 1);
  if (const auto *Call = dyn_cast<CallExpr>(E);
      Call && Call->getNumArgs() == 1 &&
      (isMoveOrForwardFunction(calleeName(Call)) || isAddressOfCall(Call)))
    return anyRoot(Call->getArg(0), Is, Depth + 1);
  // A local pointer or reference, what it can hold there (localValuesAt),
  // else what it is made with.
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E))
    if (const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
        Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
        (Local->getType()->isPointerType() ||
         Local->getType()->isReferenceType())) {
      if (const auto Values = localValuesAt(Local, Ref);
          Values && !Values->empty())
        return llvm::any_of(*Values, [&](const Expr *Value) {
          return anyRoot(Value, Is, Depth + 1);
        });
      return Local->getInit() && anyRoot(Local->getInit(), Is, Depth + 1);
    }
  return false;
}

/// Whether \p E, in an instantiation of a template, is thread-local data
/// only as the instantiation is: made of a parameter, or a member of the
/// object, of a type that depends on the template's arguments, `mp_pPointTo =
/// _pPtr` of `t_CType *_pPtr`. It is reported where the data is given.
bool fromPattern(const Expr *E, const CThreadLocalData &Data) {
  // Of a conditional, each value that is the data by itself, `_bChoose ?
  // _pValue : &t_Value`.
  if (const auto *Conditional =
          dyn_cast<AbstractConditionalOperator>(E->IgnoreParenImpCasts());
      Conditional && !chosenValue(Conditional, &Data.context()))
    return llvm::all_of(
        std::array<const Expr *, 2>{Conditional->getTrueExpr(),
                                    Conditional->getFalseExpr()},
        [&](const Expr *Value) {
          return fromPattern(Value, Data) ||
                 !(Data.pointsTo(Value) || Data.isData(Value));
        });
  return anyRoot(E, [](const Expr *Root) {
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Root))
      if (const auto *Parameter = dyn_cast<ParmVarDecl>(Ref->getDecl()))
        return dependentInPattern(Parameter);
    if (const auto *Member = dyn_cast<MemberExpr>(Root))
      if (const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl()))
        return isa<CXXThisExpr>(Member->getBase()->IgnoreParenImpCasts()) &&
               dependentInPattern(Field);
    return false;
  });
}

/// Where what is stored in a target stays after the function that stores it
/// ends, by what it is given (keptBy).
struct CKept {
  /// Whatever it is given: a global or a static, an object on the heap, or
  /// what can't be told.
  bool Always = false;
  /// A member of its object, also of a part of it.
  bool Object = false;
  /// What these parameters of it refer or point to, or a member of that.
  llvm::SmallVector<unsigned, 2> Parameters;
  bool any() const { return Always || Object || !Parameters.empty(); }
  void merge(const CKept &Other) {
    Always |= Other.Always;
    Object |= Other.Object;
    for (unsigned Parameter : Other.Parameters)
      if (!llvm::is_contained(Parameters, Parameter))
        Parameters.push_back(Parameter);
  }
};

/// Of the parameter \p Parameter as what it refers or points to: its
/// object, of an explicit object parameter, else that parameter.
CKept parameterKept(const ParmVarDecl *Parameter) {
  CKept Kept;
  const auto *Method = dyn_cast<CXXMethodDecl>(Parameter->getDeclContext());
  if (Method && Method->isExplicitObjectMemberFunction() &&
      Parameter->getFunctionScopeIndex() == 0)
    Kept.Object = true;
  else
    Kept.Parameters.push_back(Parameter->getFunctionScopeIndex());
  return Kept;
}

CKept keptBy(const Expr *Target, const CThreadLocalData &Data, int Depth = 0);

/// Where what \p Pointer points to stays (keptBy): its object, what a
/// parameter points to, what holds a pointer member, like the storage of a
/// collection, `m_pData[iEntry]`, what a local pointer is made with or
/// assigned, of what a member function of an object gives, that object, `auto
/// pSpace = f_AllocSpace(...)`; else what can't be told, also the heap.
CKept keptByPointee(const Expr *Pointer, const CThreadLocalData &Data,
                    int Depth) {
  CKept Of;
  if (!Pointer || Depth > 16) {
    Of.Always = true;
    return Of;
  }
  Pointer = Pointer->IgnoreParenImpCasts();
  if (const Expr *Addressed = addressedExpr(Pointer))
    return keptBy(Addressed, Data, Depth + 1);
  if (const auto *This = dyn_cast<CXXThisExpr>(Pointer)) {
    Of.Object = !Data.isExempt(This->getType()->getPointeeCXXRecordDecl());
    return Of;
  }
  if (const auto *Member = dyn_cast<MemberExpr>(Pointer);
      Member && isa<FieldDecl>(Member->getMemberDecl()))
    return keptBy(Member, Data, Depth + 1);
  if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Pointer)) {
    const Expr *Object = Call->getImplicitObjectArgument();
    return Object->getType()->isPointerType()
               ? keptByPointee(Object, Data, Depth + 1)
               : keptBy(Object, Data, Depth + 1);
  }
  if (const auto *Ref = dyn_cast<DeclRefExpr>(Pointer))
    if (const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl())) {
      if (const auto *Parameter = dyn_cast<ParmVarDecl>(Variable))
        return parameterKept(Parameter);
      if (Variable->hasLocalStorage() && Variable->getType()->isPointerType()) {
        llvm::SmallVector<const Expr *, 4> Values;
        if (const auto Reaching = localValuesAt(Variable, Ref);
            Reaching && !Reaching->empty())
          Values = *Reaching;
        else if (Variable->getInit())
          Values.push_back(Variable->getInit());
        if (Values.empty())
          Of.Always = true;
        for (const Expr *Value : Values)
          Of.merge(keptByPointee(Value, Data, Depth + 1));
        return Of;
      }
    }
  Of.Always = true;
  return Of;
}

/// Where what is stored in \p Target, of the function it is in, stays after
/// that ends: a member of its object, what a parameter refers or points to
/// and a member of that, a global or a static, also through a local
/// reference or a local pointer to one, `auto &Slot = m_pValue`, and what a
/// pointer that isn't a local points to; nothing of a local, nor of a member
/// of a class whose members can refer to the data (isExempt).
CKept keptBy(const Expr *Target, const CThreadLocalData &Data, int Depth) {
  CKept Kept;
  if (!Target || Depth > 16)
    return Kept;
  Target = Target->IgnoreParenImpCasts();
  auto Pointee = [&](const Expr *Pointer) {
    return keptByPointee(Pointer, Data, Depth + 1);
  };
  if (const auto *Member = dyn_cast<MemberExpr>(Target)) {
    if (const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
        Field && Data.isExempt(Field->getParent()))
      return Kept;
    return Member->isArrow() ? Pointee(Member->getBase())
                             : keptBy(Member->getBase(), Data, Depth + 1);
  }
  if (const auto *This = dyn_cast<CXXThisExpr>(Target)) {
    Kept.Object = !Data.isExempt(This->getType()->getPointeeCXXRecordDecl());
    return Kept;
  }
  if (const auto *Element = dyn_cast<ArraySubscriptExpr>(Target))
    return Element->getBase()->IgnoreParenImpCasts()->getType()->isArrayType()
               ? keptBy(Element->getBase(), Data, Depth + 1)
               : Pointee(Element->getBase());
  if (const auto *Unary = dyn_cast<UnaryOperator>(Target);
      Unary && Unary->getOpcode() == UO_Deref) {
    const Expr *Pointer = Unary->getSubExpr()->IgnoreParenImpCasts();
    // A local pointer to a local, `*ppValue` of `int **ppValue = &pValue`.
    if (addressedExpr(Pointer) || isa<CXXThisExpr>(Pointer))
      return Pointee(Pointer);
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Pointer))
      if (const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
          Variable && Variable->hasLocalStorage() &&
          !isa<ParmVarDecl>(Variable))
        return Kept;
    return Pointee(Pointer);
  }
  if (const auto *Ref = dyn_cast<DeclRefExpr>(Target))
    if (const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl())) {
      if (!Variable->hasLocalStorage()) {
        Kept.Always = true;
        return Kept;
      }
      if (const auto *Parameter = dyn_cast<ParmVarDecl>(Variable))
        return Parameter->getType()->isReferenceType()
                   ? parameterKept(Parameter)
                   : Kept;
      if (Variable->getType()->isReferenceType() && Variable->getInit())
        return keptBy(Variable->getInit(), Data, Depth + 1);
      return Kept;
    }
  // Not a temporary, which ends with the expression, `CFormat() << Value`;
  // what else gives it, like a call by reference, can't be told.
  if (!isa<DeclRefExpr>(Target) && !Target->isPRValue())
    Kept.Always = true;
  return Kept;
}

/// The object of \p Call to \p Callee: of a member operator, its first
/// argument, of a function with an explicit object parameter, what that is
/// given, of another member function its object; null for one that has
/// none.
const Expr *objectOf(const CallExpr *Call, const FunctionDecl *Callee) {
  const auto *Method = dyn_cast<CXXMethodDecl>(Callee);
  if (Method && Method->isExplicitObjectMemberFunction())
    return argumentOf(Call, Callee, 0);
  if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
      Operator && Method && Method->isInstance() && Operator->getNumArgs() > 0)
    return Operator->getArg(0);
  if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call))
    return Member->getImplicitObjectArgument();
  return nullptr;
}

/// The first parameter of \p Callee that isn't its object.
unsigned firstGiven(const FunctionDecl *Callee) {
  const auto *Method = dyn_cast<CXXMethodDecl>(Callee);
  return Method && Method->isExplicitObjectMemberFunction() ? 1 : 0;
}

/// Where \p Function, as defined, stores what its parameter \p iParameter
/// is, or what it refers or points into, so that it stays after the function
/// ends (keptBy): assigned, given to a member, a static, an object on the
/// heap or what a parameter refers to, or given to a function that does so,
/// as what that is given, `m_Pointer.f_Set(_pValue)`; only what can run.
CKept storesParameter(const FunctionDecl *Function, unsigned iParameter,
                      const CThreadLocalData &Data, int Depth = 0);

/// Where what \p Callee stores of what \p Call gives its parameter \p
/// iParameter stays, as the function \p Call is in tells it (keptBy).
CKept keptByCall(const CallExpr *Call, const FunctionDecl *Callee,
                 unsigned iParameter, const CThreadLocalData &Data, int Depth) {
  const CKept Stored = storesParameter(Callee, iParameter, Data, Depth);
  CKept Kept;
  Kept.Always = Stored.Always;
  if (Stored.Object)
    if (const Expr *Object = objectOf(Call, Callee))
      Kept.merge(keptBy(Object, Data));
  for (unsigned Parameter : Stored.Parameters)
    Kept.merge(keptBy(argumentOf(Call, Callee, Parameter), Data));
  return Kept;
}

CKept storesParameterUncached(const FunctionDecl *Definition,
                              unsigned iParameter, const CThreadLocalData &Data,
                              int Depth) {
  ASTContext &Context = Definition->getASTContext();
  const ParmVarDecl *Parameter = Definition->getParamDecl(iParameter);
  auto From = [&](const Expr *E) {
    return anyRoot(E, [&](const Expr *Root) {
      const auto *Ref = dyn_cast<DeclRefExpr>(Root);
      return Ref && Ref->getDecl() == Parameter;
    });
  };
  CKept Kept;
  // What a constructor made in \p Made is given that it keeps, `m_Pointer(
  // _pValue)`, as \p Made tells.
  std::function<void(const Expr *, const CKept &)> Constructs =
      [&](const Expr *Init, const CKept &Made) {
        if (!Init || !Made.any())
          return;
        Init = Init->IgnoreImplicit();
        if (const auto *Construct = dyn_cast<CXXConstructExpr>(Init)) {
          const CXXConstructorDecl *Constructor = Construct->getConstructor();
          for (unsigned iArgument = 0;
               Constructor && iArgument < Construct->getNumArgs() &&
               iArgument < Constructor->getNumParams();
               ++iArgument) {
            if (!From(Construct->getArg(iArgument)))
              continue;
            const CKept Stored =
                storesParameter(Constructor, iArgument, Data, Depth + 1);
            if (Stored.Always || Stored.Object)
              Kept.merge(Made);
            for (unsigned Given : Stored.Parameters)
              if (Given < Construct->getNumArgs())
                Kept.merge(keptBy(Construct->getArg(Given), Data));
          }
          return;
        }
        // Of an aggregate, a pointer or a reference in it, also deeper.
        if (isa<InitListExpr, CXXParenListInitExpr>(Init)) {
          const Expr *List = Init;
          if (const auto *Braces = dyn_cast<InitListExpr>(Init);
              Braces && !Braces->isSemanticForm() && Braces->getSemanticForm())
            List = Braces->getSemanticForm();
          for (const CAggregatePart &Part : aggregateParts(List)) {
            if (Part.Field && Data.isExempt(Part.Field->getParent()))
              continue;
            if (Part.Type->isPointerType() || Part.Type->isReferenceType()) {
              if (From(Part.Init))
                Kept.merge(Made);
            } else
              Constructs(Part.Init, Made);
          }
        }
      };
  CKept Object;
  Object.Object = true;
  CKept Always;
  Always.Always = true;
  // Of a pointer, \p Init as it is, else what makes it.
  auto Keeps = [&](QualType Type, const Expr *Init, const CKept &Made) {
    if (Type->isPointerType() || Type->isReferenceType()) {
      if (From(Init))
        Kept.merge(Made);
    } else
      Constructs(Init, Made);
  };
  const Stmt *Body = Definition->getBody();
  // Only what can run (canRunIn), not after a branch that returns, `if
  // constexpr (!t_bKeep) return;`.
  auto CanRun = [&](const Stmt *S) {
    return canRunIn(S, Body, Definition, Context);
  };
  if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Definition))
    for (const CXXCtorInitializer *Initializer : Constructor->inits()) {
      const Expr *Init = Initializer->getInit();
      const FieldDecl *Field = Initializer->getMember();
      // Not a member of a class whose members can refer to it (isExempt).
      if (!Init || (Field && Data.isExempt(Field->getParent())))
        continue;
      Keeps(Field ? Field->getType() : Init->getType(), Init, Object);
    }
  std::function<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Kept.Always || isa<LambdaExpr>(S) || hasUnevaluatedOperands(S))
      return;
    if (const auto *Assign = dyn_cast<BinaryOperator>(S);
        Assign && Assign->getOpcode() == BO_Assign &&
        Assign->getLHS()->getType()->isPointerType() &&
        From(Assign->getRHS()) && CanRun(Assign))
      Kept.merge(keptBy(Assign->getLHS(), Data));
    if (const auto *Call = dyn_cast<CallExpr>(S))
      if (const FunctionDecl *Callee = Call->getDirectCallee())
        for (unsigned iGiven = firstGiven(Callee);
             iGiven < Callee->getNumParams(); ++iGiven)
          if (const Expr *Argument = argumentOf(Call, Callee, iGiven);
              Argument && From(Argument) && CanRun(Call))
            Kept.merge(keptByCall(Call, Callee, iGiven, Data, Depth + 1));
    // On the heap, or where it is placed, `new (pSpace) CType(...)`.
    if (const auto *New = dyn_cast<CXXNewExpr>(S);
        New && New->getInitializer() && CanRun(New))
      Keeps(New->getAllocatedType(), New->getInitializer(),
            New->getNumPlacementArgs() > 0
                ? keptByPointee(New->getPlacementArg(0), Data, 0)
                : Always);
    if (const auto *Declaration = dyn_cast<DeclStmt>(S);
        Declaration && CanRun(Declaration))
      for (const Decl *Declared : Declaration->decls())
        if (const auto *Variable = dyn_cast<VarDecl>(Declared);
            Variable && !Variable->hasLocalStorage() && Variable->getInit())
          Keeps(Variable->getType(), Variable->getInit(), Always);
    for (const Stmt *Child : evaluatedChildren(S, Context))
      Visit(Child);
  };
  Visit(Body);
  return Kept;
}

CKept storesParameter(const FunctionDecl *Function, unsigned iParameter,
                      const CThreadLocalData &Data, int Depth) {
  // Not a copy or a move, which copies what its source holds.
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      Method && (Method->isCopyAssignmentOperator() ||
                 Method->isMoveAssignmentOperator()))
    return {};
  if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Function);
      Constructor && Constructor->isCopyOrMoveConstructor())
    return {};
  const FunctionDecl *Definition = nullptr;
  if (Depth > 8 || !Function->hasBody(Definition) || !Definition ||
      iParameter >= Definition->getNumParams())
    return {};
  if (const QualType Type = Definition->getParamDecl(iParameter)->getType();
      !Type->isPointerType() && !Type->isReferenceType())
    return {};
  struct CStores {
    llvm::DenseMap<std::pair<const FunctionDecl *, unsigned>, CKept> Results;
  };
  auto &Results = astCache<CStores>(Definition->getASTContext()).Results;
  const std::pair<const FunctionDecl *, unsigned> Key(Definition, iParameter);
  if (auto It = Results.find(Key); It != Results.end())
    return It->second;
  // One that reaches itself stores it only as the rest of it does.
  Results[Key] = {};
  const CKept Kept =
      storesParameterUncached(Definition, iParameter, Data, Depth);
  astCache<CStores>(Definition->getASTContext()).Results[Key] = Kept;
  return Kept;
}

/// Whether what \p Parameter is given is reported where its function stores
/// it, as it is of a reference or a pointer to a thread-local type, not one
/// that depends on the arguments of the template it is an instantiation of.
bool reportedInside(const ParmVarDecl *Parameter,
                    const CThreadLocalData &Data) {
  return Data.isType(Parameter->getType(), true) &&
         !dependentInPattern(Parameter);
}

} // namespace

ThreadLocalStorageCheck::ThreadLocalStorageCheck(StringRef Name,
                                                 ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      ThreadLocalFunctions(listOption("ThreadLocalFunctions", "")),
      ThreadLocalTypes(listOption("ThreadLocalTypes", "")),
      ThreadLocalScopeTypes(listOption("ThreadLocalScopeTypes", "")),
      ThreadLocalTemplates(listOption("ThreadLocalTemplates", "")),
      ThreadLocalHandlerTypes(listOption("ThreadLocalHandlerTypes", "")),
      ScopeExitHelpers(listOption("ScopeExitHelpers", "")) {
  splitOptionList(ThreadLocalFunctions, Lists.Functions);
  splitOptionList(ThreadLocalTypes, Lists.Types);
  splitOptionList(ThreadLocalScopeTypes, Lists.ScopeTypes);
  splitOptionList(ThreadLocalTemplates, Lists.Templates);
  splitOptionList(ThreadLocalHandlerTypes, Lists.HandlerTypes);
  splitOptionList(ScopeExitHelpers, Lists.ScopeExitHelpers);
}

void ThreadLocalStorageCheck::storeCheckOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "ThreadLocalFunctions", ThreadLocalFunctions);
  Options.store(Opts, "ThreadLocalTypes", ThreadLocalTypes);
  Options.store(Opts, "ThreadLocalScopeTypes", ThreadLocalScopeTypes);
  Options.store(Opts, "ThreadLocalTemplates", ThreadLocalTemplates);
  Options.store(Opts, "ThreadLocalHandlerTypes", ThreadLocalHandlerTypes);
  Options.store(Opts, "ScopeExitHelpers", ScopeExitHelpers);
}

void ThreadLocalStorageCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(functionDecl(isDefinition()).bind("function"), this);
  // Also of the instantiations of templates, as instantiated.
  Finder->addMatcher(
      traverse(TK_AsIs,
               fieldDecl(hasInClassInitializer(anything())).bind("field")),
      this);
  Finder->addMatcher(
      traverse(TK_AsIs,
               declaratorDecl(anyOf(varDecl(), fieldDecl())).bind("holder")),
      this);
  Finder->addMatcher(
      traverse(TK_AsIs, varDecl(hasGlobalStorage(), unless(isStaticLocal()),
                                hasInitializer(anything()))
                            .bind("global")),
      this);
  Finder->addMatcher(traverse(TK_AsIs, cxxNewExpr().bind("new")), this);
  // A generic lambda, by its instantiations.
  Finder->addMatcher(lambdaExpr().bind("lambda"), this);
}

void ThreadLocalStorageCheck::reportStore(const Expr *Value, bool Reference,
                                          const Twine &Where,
                                          const SourceManager &SM) {
  // Not in a template that is given it, where it is given (fromPattern).
  if (Classifier && fromPattern(Value, *Classifier))
    return;
  const SourceLocation Loc = reportLocation(Value->getBeginLoc(), SM);
  if (Loc.isInvalid())
    return;
  diag(Loc, "%select{a pointer|a reference}0 to thread-local data is stored "
            "in %1, which can outlive the expression and be used on another "
            "thread; refer to thread-local data only from locals and "
            "parameters, and get it again where it is used")
      << Reference << Where.str();
}

void ThreadLocalStorageCheck::reportGiven(const Expr *Value, bool Reference,
                                          const FunctionDecl *Callee,
                                          const SourceManager &SM) {
  // Not in a template that is given it, where it is given (fromPattern).
  if (Classifier && fromPattern(Value, *Classifier))
    return;
  const SourceLocation Loc = reportLocation(Value->getBeginLoc(), SM);
  if (Loc.isInvalid())
    return;
  diag(Loc, "%select{a pointer|a reference}0 to thread-local data is given to "
            "%1, which stores it where it can outlive the expression and be "
            "used on another thread; refer to thread-local data only from "
            "locals and parameters, and get it again where it is used")
      << Reference << Callee;
}

void ThreadLocalStorageCheck::checkRoot(const Stmt *Root,
                                        const FunctionDecl *Function,
                                        const FunctionDecl *Current,
                                        const CThreadLocalData &Data,
                                        const CheckedInit *Initializer) {
  ASTContext &Context = Data.context();
  const SourceManager &SM = Context.getSourceManager();
  // The body of the function, whose statements that can't run store nothing
  // (canRunIn); none of an initializer outside a function.
  const Stmt *Body = Function ? Function->getBody() : nullptr;
  // The function the statements visited are in, also a lambda, which can
  // hold the data in a local object, a holder, unless it is a coroutine,
  // which can resume on another thread with it.
  const FunctionDecl *VisitedIn = Function;
  // Also in the body of the lambda it is in, by its own control flow.
  auto CanRun = [&](const Stmt *S) {
    if (!Body)
      return true;
    if (!canRunIn(S, Body, Function, Context))
      return false;
    return !VisitedIn || VisitedIn == Function || !VisitedIn->getBody() ||
           canRunIn(S, VisitedIn->getBody(), VisitedIn, Context);
  };
  auto Name = [](const NamedDecl *Decl) {
    return "'" + Decl->getNameAsString() + "'";
  };
  // Whether \p Target, assigned, is a local that ends with its function: a
  // local or a parameter held by value, an element of a local array of them,
  // or what a local reference is bound to that is, or thread-local data
  // itself, of the same thread; else what is stored there, \p Where.
  std::function<bool(const Expr *, std::string &, int)> Local =
      [&](const Expr *Target, std::string &Where, int Depth) {
        Target = Target->IgnoreParenImpCasts();
        if (Depth == 0 && Data.isStorage(Target))
          return true;
        if (Depth > 8)
          return false;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(Target)) {
          const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
          if (!Variable)
            return false;
          if (!Variable->hasLocalStorage() && !Variable->isInitCapture()) {
            Where = "the variable " + Name(Variable);
            return false;
          }
          if (!Variable->getType()->isReferenceType())
            return true;
          if (isa<ParmVarDecl>(Variable) || !Variable->getInit()) {
            Where = "what the parameter " + Name(Variable) + " refers to";
            return false;
          }
          return Local(Variable->getInit(), Where, Depth + 1);
        }
        if (const auto *Element = dyn_cast<ArraySubscriptExpr>(Target);
            Element &&
            Element->getBase()->IgnoreParenImpCasts()->getType()->isArrayType())
          return Local(Element->getBase(), Where, Depth + 1);
        if (const auto *Member = dyn_cast<MemberExpr>(Target)) {
          const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
          if (Field && Data.isExempt(Field->getParent()))
            return true;
          Where = "the member " + Name(Member->getMemberDecl());
          return false;
        }
        // What a local pointer to a local points to, `*ppValue` of `int
        // **ppValue = &pValue` (addressedExpr).
        if (const auto *Unary = dyn_cast<UnaryOperator>(Target);
            Unary && Unary->getOpcode() == UO_Deref)
          if (const Expr *Addressed = addressedExpr(Unary->getSubExpr()))
            return Local(Addressed, Where, Depth + 1);
        Where = "what the expression refers to";
        return false;
      };
  // Of \p Init, given to what is of \p Type, stored in \p Where: a pointer or
  // a reference to the data, unless \p InLocal, a local or an element of a
  // local array, or \p Exempt; also each part of an aggregate, also of a
  // base or an array, by its member, and what a constructor that keeps it
  // in what it makes is given.
  auto HoldsLocally = [&] { return !VisitedIn || !isCoroutine(VisitedIn); };
  // Whether what is stored in \p Target stays after the function visited
  // ends (keptBy), or in a coroutine is a local, which can resume on another
  // thread with it.
  auto KeptHere = [&](const Expr *Target) {
    if (!Target)
      return false;
    if (keptBy(Target, Data).any())
      return true;
    const auto *Ref = dyn_cast<DeclRefExpr>(Target->IgnoreParenImpCasts());
    const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
    return Local && Local->hasLocalStorage() && !HoldsLocally();
  };
  std::function<void(const Expr *, QualType, const std::string &, bool, bool,
                     int)>
      CheckInit = [&](const Expr *Init, QualType Type, const std::string &Where,
                      bool InLocal, bool Exempt, int Depth) {
        if (!Init || Type.isNull() || Depth > 16 || Init->isValueDependent())
          return;
        Init = Data.normalized(Init);
        // As it is for a pointer or a reference, also an array decayed, `int
        // *g_pValue = t_Array`, or a pointer read.
        const Expr *Given = Init;
        for (int Step = 0; Step < 8; ++Step) {
          const Expr *Inner = Init->IgnoreImplicit()->IgnoreParens();
          if (const auto *Cast = dyn_cast<CXXFunctionalCastExpr>(Inner))
            Inner = Cast->getSubExpr();
          Inner = Data.normalized(Inner);
          if (Inner == Init)
            break;
          Init = Inner;
        }
        if (Type->isPointerType() || Type->isReferenceType()) {
          if (!InLocal && !Exempt && Data.refersTo(Given, Type))
            reportStore(Given,
                        Type->isReferenceType() &&
                            !Type.getNonReferenceType()->isPointerType(),
                        Where, SM);
          return;
        }
        if (isa<InitListExpr, CXXParenListInitExpr>(Init)) {
          const Expr *Made = Init;
          if (const auto *List = dyn_cast<InitListExpr>(Init);
              List && !List->isSemanticForm() && List->getSemanticForm())
            Made = List->getSemanticForm();
          for (const CAggregatePart &Part : aggregateParts(Made))
            if (Part.Field)
              CheckInit(Part.Init, Part.Type, "the member " + Name(Part.Field),
                        false, Data.isExempt(Part.Field->getParent()),
                        Depth + 1);
            else if (Part.Base)
              CheckInit(Part.Init, Part.Type, Where, false, Exempt, Depth + 1);
            else
              CheckInit(Part.Init, Part.Type, Where, InLocal, Exempt,
                        Depth + 1);
          return;
        }
        // A constructor that keeps what it is given for a pointer or a
        // reference in what it makes: one whose body can be seen that stores
        // it (storesParameter), else one of a pointer or a reference to what
        // isn't const; not what it reports itself (reportedInside). Not a
        // copy or a move, which copies what its source holds, nor a local
        // outside a coroutine, which ends with the function on its thread.
        if (const auto *Construct = dyn_cast<CXXConstructExpr>(Init)) {
          const CXXConstructorDecl *Constructor = Construct->getConstructor();
          if (!Constructor || Constructor->isCopyOrMoveConstructor() ||
              Exempt || Data.isExempt(Constructor->getParent()) ||
              (InLocal && HoldsLocally()))
            return;
          for (unsigned iArgument = 0; iArgument < Construct->getNumArgs() &&
                                       iArgument < Constructor->getNumParams();
               ++iArgument) {
            const QualType Parameter =
                Constructor->getParamDecl(iArgument)->getType();
            if (!(Parameter->isPointerType() || Parameter->isReferenceType()))
              continue;
            const Expr *Argument = Construct->getArg(iArgument);
            if (!Data.refersTo(Argument, Parameter) ||
                reportedInside(Constructor->getParamDecl(iArgument), Data))
              continue;
            const CKept Stored = storesParameter(Constructor, iArgument, Data);
            const bool Keeps =
                Constructor->hasBody()
                    ? Stored.Always || Stored.Object ||
                          llvm::any_of(
                              Stored.Parameters,
                              [&](unsigned Given) {
                                return Given < Construct->getNumArgs() &&
                                       KeptHere(Construct->getArg(Given));
                              })
                    : !Parameter->getPointeeType().isNull() &&
                          !Parameter->getPointeeType().isConstQualified();
            if (Keeps)
              reportStore(
                  Argument,
                  Parameter->isReferenceType() &&
                      !Parameter.getNonReferenceType()->isPointerType(),
                  "the " + Name(Constructor->getParent()) + " made of it", SM);
          }
        }
      };
  // Whether the closure \p Lambda, made in \p Current, ends with the
  // function that makes it, before a coroutine suspends, after which it
  // can resume on another thread: called where it is made, `[&] { ... }()`,
  // a local only called, `auto fApply = [&] { ... }; fApply();`, or the
  // cleanup of a local scope exit, `auto Cleanup = g_OnScopeExit / [&] {
  // ... };` of one of ScopeExitHelpers, whose guard is only told to run or
  // not.
  auto StaysLocal = [&](const LambdaExpr *Lambda, const FunctionDecl *Current) {
    if (!Current)
      return false;
    const bool Coroutine = isCoroutine(Current);
    // Whether \p S suspends where that can run (canRunIn), not in a
    // lambda.
    std::function<bool(const Stmt *)> Suspends = [&](const Stmt *S) {
      if (!S || isa<LambdaExpr>(S))
        return false;
      if (isSuspension(S) && (!Body || canRunIn(S, Body, Current, Context)))
        return true;
      return llvm::any_of(S->children(), Suspends);
    };
    // Not one whose captures are made across a suspension, `[pValue =
    // &t_Value, Value = co_await f_Other()]`.
    if (Coroutine &&
        llvm::any_of(Lambda->capture_inits(), [&](const Expr *CaptureInit) {
          return CaptureInit && Suspends(CaptureInit);
        }))
      return false;
    // The parent of \p Node past what only wraps it.
    auto ParentOf = [&](DynTypedNode Node) {
      for (int Step = 0; Step < 8; ++Step) {
        const auto Parents = Context.getParents(Node);
        if (Parents.empty())
          return DynTypedNode();
        if (Parents[0].get<ImplicitCastExpr>() ||
            Parents[0].get<MaterializeTemporaryExpr>() ||
            Parents[0].get<CXXBindTemporaryExpr>() ||
            Parents[0].get<ExprWithCleanups>() || Parents[0].get<ParenExpr>() ||
            Parents[0].get<CXXConstructExpr>()) {
          Node = Parents[0];
          continue;
        }
        return Parents[0];
      }
      return DynTypedNode();
    };
    // Whether each use of the local \p Variable is \p Allowed, and, in a
    // coroutine, nothing after it in its block suspends.
    auto OnlyUsed = [&](const VarDecl *Variable, auto &&Allowed) {
      if (!Body || !Variable || !Variable->hasLocalStorage() ||
          Variable->getType()->isReferenceType())
        return false;
      if (Coroutine) {
        const DeclStmt *Declaration = nullptr;
        const CompoundStmt *Block = nullptr;
        if (const auto Parents = Context.getParents(*Variable);
            !Parents.empty())
          Declaration = Parents[0].get<DeclStmt>();
        if (Declaration)
          if (const auto Parents = Context.getParents(*Declaration);
              !Parents.empty())
            Block = Parents[0].get<CompoundStmt>();
        if (!Block)
          return false;
        bool After = false;
        for (const Stmt *Statement : Block->body())
          if (Statement == Declaration)
            After = true;
          else if (After && Suspends(Statement))
            return false;
      }
      llvm::SmallVector<const Stmt *, 32> Work{Body};
      while (!Work.empty()) {
        const Stmt *S = Work.pop_back_val();
        if (!S)
          continue;
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
            Ref && Ref->getDecl() == Variable && !Allowed(Ref))
          return false;
        for (const Stmt *Child : S->children())
          Work.push_back(Child);
      }
      return true;
    };
    const DynTypedNode Parent = ParentOf(DynTypedNode::create(*Lambda));
    // Called where it is made.
    if (const auto *Call = Parent.get<CXXOperatorCallExpr>();
        Call && Call->getOperator() == OO_Call && Call->getNumArgs() >= 1 &&
        Call->getArg(0)->IgnoreImplicit() == Lambda)
      return !Coroutine || !Suspends(Call);
    // A local only called.
    if (const auto *Variable = Parent.get<VarDecl>())
      return OnlyUsed(Variable, [&](const DeclRefExpr *Ref) {
        const auto *Call =
            ParentOf(DynTypedNode::create(*Ref)).get<CXXOperatorCallExpr>();
        return Call && Call->getOperator() == OO_Call &&
               Call->getArg(0)->IgnoreImplicit() == Ref;
      });
    // The cleanup of a local scope exit, whose guard is only told.
    if (const auto *Exit = Parent.get<CXXOperatorCallExpr>();
        Exit && Exit->getOperator() == OO_Slash && Exit->getNumArgs() == 2)
      if (const auto *Helper =
              dyn_cast<DeclRefExpr>(Exit->getArg(0)->IgnoreImplicit());
          Helper && Helper->getDecl()->getIdentifier() &&
          Data.isScopeExitHelper(Helper->getDecl()->getName()))
        return OnlyUsed(
            ParentOf(DynTypedNode::create(*Exit)).get<VarDecl>(),
            [&](const DeclRefExpr *Ref) {
              const auto *Member =
                  ParentOf(DynTypedNode::create(*Ref)).get<MemberExpr>();
              return Member && isa<CXXMethodDecl>(Member->getMemberDecl());
            });
    return false;
  };
  // Whether the local object \p Variable, a holder, leaves its function:
  // returned, also moved, `return View;`, assigned or given to what stays
  // (keptBy), or made on the heap from.
  auto Escapes = [&](const VarDecl *Variable) {
    if (!Body)
      return false;
    auto Names = [&](const Expr *E) {
      for (int Step = 0; E && Step < 16; ++Step) {
        E = E->IgnoreImplicit()->IgnoreParens();
        if (const auto *Construct = dyn_cast<CXXConstructExpr>(E);
            Construct && Construct->getNumArgs() == 1) {
          E = Construct->getArg(0);
          continue;
        }
        if (const auto *Call = dyn_cast<CallExpr>(E);
            Call && Call->getNumArgs() == 1 &&
            isMoveOrForwardFunction(calleeName(Call))) {
          E = Call->getArg(0);
          continue;
        }
        const auto *Ref = dyn_cast<DeclRefExpr>(E);
        return Ref && Ref->getDecl() == Variable;
      }
      return false;
    };
    llvm::SmallVector<const Stmt *, 32> Work{Body};
    while (!Work.empty()) {
      const Stmt *S = Work.pop_back_val();
      if (!S)
        continue;
      if (const auto *Return = dyn_cast<ReturnStmt>(S);
          Return && Names(Return->getRetValue()))
        return true;
      if (const auto *Return = dyn_cast<CoreturnStmt>(S);
          Return && Names(Return->getOperand()))
        return true;
      if (const auto *Assign = dyn_cast<CXXOperatorCallExpr>(S);
          Assign && Assign->isAssignmentOp() && Assign->getNumArgs() == 2 &&
          Names(Assign->getArg(1)) && keptBy(Assign->getArg(0), Data).any())
        return true;
      if (const auto *New = dyn_cast<CXXNewExpr>(S);
          New && New->getInitializer() && Names(New->getInitializer()))
        return true;
      for (const Stmt *Child : S->children())
        Work.push_back(Child);
    }
    return false;
  };
  std::function<void(const Stmt *, const FunctionDecl *)> Visit =
      [&](const Stmt *S, const FunctionDecl *Current) {
        if (!S)
          return;
        VisitedIn = Current;
        if (const auto *E = dyn_cast<Expr>(S);
            E && (E->isTypeDependent() || E->isValueDependent()))
          return;
        if (hasUnevaluatedOperands(S))
          return;
        if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
          const bool Local = StaysLocal(Lambda, Current) || !CanRun(Lambda);
          auto Init = Lambda->capture_init_begin();
          for (const LambdaCapture &Capture : Lambda->captures()) {
            const Expr *CaptureInit = *Init++;
            // `this`, a pointer to the object, not a copy of it, `[*this]`.
            if (Capture.capturesThis()) {
              const CXXRecordDecl *Class = enclosingThisClass(Lambda);
              if (Capture.getCaptureKind() != LCK_StarThis && !Local && Class &&
                  Data.isType(Context.getCanonicalTagType(Class))) {
                const SourceLocation Loc =
                    reportLocation(Capture.getLocation(), SM);
                if (Loc.isValid())
                  diag(Loc, "a pointer to thread-local data is stored in the "
                            "capture of 'this', which can outlive the "
                            "expression and be used on another thread; refer "
                            "to thread-local data only from locals and "
                            "parameters, and get it again where it is used");
              }
              continue;
            }
            if (!Capture.capturesVariable())
              continue;
            // A structured binding by what it binds, `[&Value]` of `auto
            // &[Value] = t_Data`.
            const ValueDecl *Captured = Capture.getCapturedVar();
            if (const auto *Binding = dyn_cast<BindingDecl>(Captured)) {
              if (!Local && Capture.getCaptureKind() == LCK_ByRef &&
                  Binding->getBinding() &&
                  (Data.isData(Binding->getBinding()) ||
                   Data.pointsTo(Binding->getBinding()))) {
                const SourceLocation Loc =
                    reportLocation(Capture.isImplicit() && CaptureInit
                                       ? CaptureInit->getBeginLoc()
                                       : Capture.getLocation(),
                                   SM);
                if (Loc.isValid())
                  diag(Loc, "a reference to thread-local data is stored in "
                            "the capture of %0, which can outlive the "
                            "expression and be used on another thread; refer "
                            "to thread-local data only from locals and "
                            "parameters, and get it again where it is used")
                      << Name(Binding);
              }
              continue;
            }
            const auto *Variable = dyn_cast<VarDecl>(Captured);
            if (!Variable)
              continue;
            if (Variable->isInitCapture()) {
              if (!Local)
                CheckInit(Variable->getInit(), Variable->getType(),
                          "the capture of " + Name(Variable), false, false, 0);
              continue;
            }
            // A pointer or an array of them, by reference or copied, also
            // a copy of a reference to one, `[rpValue]`, which copies the
            // pointer, or a reference, by reference, also one to a local
            // pointer, by what that holds: not a copy of what a reference
            // refers to otherwise.
            const QualType Type = Variable->getType();
            QualType Copied = Type.getNonReferenceType();
            while (const ArrayType *Array = Copied->getAsArrayTypeUnsafe())
              Copied = Array->getElementType();
            const VarDecl *Bound =
                Type->isReferenceType() ? Data.boundLocal(Variable) : nullptr;
            const bool Refers =
                Data.isVariable(Variable) ||
                (Bound && Bound != Variable && Data.isVariable(Bound));
            if (Local || !Refers ||
                !(Copied->isPointerType() ||
                  Capture.getCaptureKind() == LCK_ByRef))
              continue;
            const SourceLocation Loc = reportLocation(
                Capture.isImplicit() && CaptureInit ? CaptureInit->getBeginLoc()
                                                    : Capture.getLocation(),
                SM);
            if (Loc.isValid())
              diag(Loc, "%select{a pointer|a reference}0 to thread-local data "
                        "is stored in the capture of %1, which can outlive "
                        "the expression and be used on another thread; refer "
                        "to thread-local data only from locals and "
                        "parameters, and get it again where it is used")
                  << (Capture.getCaptureKind() == LCK_ByRef &&
                      !Copied->isPointerType())
                  << Name(Variable);
          }
          for (const Expr *CaptureInit : Lambda->capture_inits())
            Visit(CaptureInit, Current);
          // Not the body of a generic lambda, whose instantiations are
          // checked themselves.
          if (!Lambda->isGenericLambda())
            Visit(Lambda->getBody(), Lambda->getCallOperator());
          VisitedIn = Current;
          return;
        }
        if (const auto *Assign = dyn_cast<BinaryOperator>(S);
            Assign && Assign->getOpcode() == BO_Assign &&
            Assign->getLHS()->getType()->isPointerType() &&
            Data.pointsTo(Assign->getRHS()) && CanRun(Assign)) {
          std::string Where;
          if (!Local(Assign->getLHS(), Where, 0))
            reportStore(Assign->getRHS(), false, Where, SM);
        }
        // What a function stores of a parameter that it can't tell is
        // thread-local data, of another type or of a template's argument,
        // `m_pInherit = &_Inherit` of a generic pointer's `operator =`, where
        // it is given (storesParameter).
        // Where it stays (KeptHere): of an object, or what a parameter is
        // given, that stays, or a local one in a coroutine; not a temporary,
        // `"{}"_f << Value`, nor a local formatter of the function called.
        if (const auto *Call = dyn_cast<CallExpr>(S); Call && CanRun(Call))
          if (const FunctionDecl *Callee = Call->getDirectCallee();
              Callee && !namedFunction(Callee, Lists.Functions)) {
            const Expr *Object = objectOf(Call, Callee);
            for (unsigned iParameter = firstGiven(Callee);
                 iParameter < Callee->getNumParams(); ++iParameter) {
              const ParmVarDecl *Parameter = Callee->getParamDecl(iParameter);
              const QualType Type = Parameter->getType();
              const Expr *Argument = argumentOf(Call, Callee, iParameter);
              if (Argument &&
                  (Type->isPointerType() || Type->isReferenceType()) &&
                  !reportedInside(Parameter, Data) &&
                  Data.refersTo(Argument, Type) && [&] {
                    const CKept Stored =
                        storesParameter(Callee, iParameter, Data);
                    return Stored.Always ||
                           (Stored.Object && KeptHere(Object)) ||
                           llvm::any_of(Stored.Parameters, [&](unsigned Given) {
                             return KeptHere(argumentOf(Call, Callee, Given));
                           });
                  }())
                reportGiven(Argument,
                            Type->isReferenceType() &&
                                !Type.getNonReferenceType()->isPointerType(),
                            Callee, SM);
            }
          }
        // An object given an aggregate or what a constructor makes,
        // `View = CView{&t_Value}`.
        // Not a pointer, which what the operator stores of it tells.
        if (const auto *Assign = dyn_cast<CXXOperatorCallExpr>(S);
            Assign && Assign->getOperator() == OO_Equal &&
            Assign->getNumArgs() == 2 &&
            !Assign->getArg(1)->getType()->isPointerType() && CanRun(Assign))
          CheckInit(Assign->getArg(1), Assign->getArg(1)->getType(),
                    "what it is assigned to", false, false, 0);
        if (const auto *Declaration = dyn_cast<DeclStmt>(S);
            Declaration && CanRun(Declaration))
          for (const Decl *Declared : Declaration->decls())
            if (const auto *Variable = dyn_cast<VarDecl>(Declared);
                Variable && Variable->getInit())
              CheckInit(
                  Variable->getInit(), Variable->getType(),
                  (Variable->hasLocalStorage() ? "the local " : "the static ") +
                      Name(Variable),
                  Variable->hasLocalStorage() &&
                      !(Variable->getType()->isRecordType() &&
                        Escapes(Variable)),
                  false, 0);
        if (const auto *New = dyn_cast<CXXNewExpr>(S);
            New && New->getInitializer() && CanRun(New))
          CheckInit(New->getInitializer(), New->getInitializer()->getType(),
                    "an object on the heap", false, false, 0);
        // What is returned, unless its type tells it is the data.
        const Expr *Returned = nullptr;
        QualType ReturnType;
        if (const auto *Return = dyn_cast<ReturnStmt>(S)) {
          Returned = Return->getRetValue();
          if (Current)
            ReturnType = Current->getReturnType();
        } else if (const auto *Return = dyn_cast<CoreturnStmt>(S)) {
          Returned = Return->getOperand();
          if (Returned)
            ReturnType = Returned->getType();
        }
        // Not of a member function of a thread-local type, what its callers
        // tell is in it (isData).
        const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Current);
        if (Returned && !ReturnType.isNull() && CanRun(S) &&
            !namedFunction(Current, Lists.Functions) &&
            !(Method && Method->isInstance() &&
              Data.isType(Context.getCanonicalTagType(Method->getParent())))) {
          if ((ReturnType->isPointerType() || ReturnType->isReferenceType()) &&
              !Data.isType(ReturnType, true) &&
              Data.refersTo(Returned, ReturnType)) {
            const SourceLocation Loc =
                reportLocation(Returned->getBeginLoc(), SM);
            if (Loc.isValid())
              diag(Loc,
                   "%select{a pointer|a reference}0 to thread-local data is "
                   "returned as a type that isn't %select{a pointer|a "
                   "reference}0 to a thread-local type, so what it is given "
                   "to can't tell it is thread-local data; return the "
                   "thread-local type, or a copy")
                  << ReturnType->isReferenceType();
          } else if (!ReturnType->isPointerType() &&
                     !ReturnType->isReferenceType())
            // An aggregate or what a constructor makes, `return
            // CView{&t_Value};`.
            CheckInit(Returned, ReturnType, "what is returned", false, false,
                      0);
        }
        // Only what runs, not a branch a constant condition leaves out.
        for (const Stmt *Child : evaluatedChildren(S, Context))
          Visit(Child, Current);
      };
  // What a constructor gives the members of what it makes, not by default,
  // which the member's initializer is checked for.
  if (const auto *Constructor = dyn_cast_or_null<CXXConstructorDecl>(Function);
      Constructor && Root == Function->getBody())
    for (const CXXCtorInitializer *Initializer : Constructor->inits()) {
      const FieldDecl *Field = Initializer->getMember();
      const Expr *Init = Initializer->getInit();
      if (!Field || !Init || !Initializer->isWritten())
        continue;
      CheckInit(Init, Field->getType(), "the member " + Name(Field), false,
                Data.isExempt(Field->getParent()), 0);
      Visit(Init, Function);
    }
  // An initializer outside a function, by what it initializes.
  if (Initializer)
    CheckInit(Initializer->Init, Initializer->Type, Initializer->Where, false,
              Initializer->Exempt, 0);
  Visit(Root, Current);
}

void ThreadLocalStorageCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  ASTContext &Context = *Result.Context;
  const CThreadLocalData Data(Context, Lists);
  Classifier = &Data;
  const SourceManager &SM = Context.getSourceManager();
  // What is held for each thread, a class that isn't one of ThreadLocalTypes,
  // which isn't taken as thread-local data elsewhere: each of them.
  if (const auto *Holder = Result.Nodes.getNodeAs<DeclaratorDecl>("holder")) {
    // Also a member of a template as instantiated, of a type that doesn't
    // depend on it.
    if (Holder->getType().isNull() || Holder->getType()->isDependentType())
      return;
    // Not a member of a template whose type is an argument of it, which is
    // reported where the argument is given.
    if (isa<FieldDecl>(Holder) &&
        Holder->getType()->getAs<SubstTemplateTypeParmType>())
      return;
    for (const CXXRecordDecl *Held : Data.heldPerThread(Holder->getType())) {
      if (const CXXRecordDecl *Definition = Held->getDefinition())
        Held = Definition;
      if (Data.isListedType(Held))
        continue;
      const SourceLocation Loc = reportLocation(Holder->getLocation(), SM);
      if (Loc.isValid())
        diag(Loc, "%0 holds '%1' for each thread, which isn't one of "
                  "ThreadLocalTypes, so it isn't taken as thread-local data; "
                  "add it to ThreadLocalTypes in the options file, or hold a "
                  "struct of its own for the thread instead")
            << Holder << threadLocalTypeName(Held);
    }
    return;
  }
  // Also what is made on the heap.
  if (const auto *New = Result.Nodes.getNodeAs<CXXNewExpr>("new")) {
    const QualType Type = New->getAllocatedType();
    if (Type.isNull() || Type->isDependentType())
      return;
    for (const CXXRecordDecl *Held : Data.heldPerThread(Type)) {
      if (const CXXRecordDecl *Definition = Held->getDefinition())
        Held = Definition;
      if (Data.isListedType(Held))
        continue;
      const SourceLocation Loc = reportLocation(New->getBeginLoc(), SM);
      if (Loc.isValid())
        diag(Loc, "the object made on the heap holds '%0' for each thread, "
                  "which isn't one of ThreadLocalTypes, so it isn't taken as "
                  "thread-local data; add it to ThreadLocalTypes in the "
                  "options file, or hold a struct of its own for the thread "
                  "instead")
            << threadLocalTypeName(Held);
    }
    return;
  }
  // A default member initializer, also of a template as instantiated, and
  // the initializer of a global or a static member, also what lambdas in
  // them capture.
  if (const auto *Field = Result.Nodes.getNodeAs<FieldDecl>("field")) {
    const Expr *Init = Field->getInClassInitializer();
    if (!Init || Init->isValueDependent() ||
        Field->getParent()->isDependentContext())
      return;
    checkInitializer(Init, Field->getType(),
                     "the member '" + Field->getNameAsString() + "'",
                     Data.isExempt(Field->getParent()), Data);
    return;
  }
  if (const auto *Variable = Result.Nodes.getNodeAs<VarDecl>("global")) {
    if (!Variable->getInit() ||
        Variable->getDeclContext()->isDependentContext() ||
        Variable->getInit()->isValueDependent() ||
        Variable->getParentFunctionOrMethod())
      return;
    checkInitializer(Variable->getInit(), Variable->getType(),
                     "the variable '" + Variable->getNameAsString() + "'",
                     false, Data);
    return;
  }
  // Of a lambda, as part of the function it is in; of a generic one, each of
  // its instantiations, which that doesn't visit.
  if (const auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda")) {
    const FunctionDecl *Operator = Lambda->getCallOperator();
    if (!Lambda->isGenericLambda() || !Operator)
      return;
    for (const FunctionDecl *Instance :
         Instantiations.instantiationsOf(Operator, Context)) {
      if (Instance == Operator || Instance->isDependentContext() ||
          !Instance->getBody())
        continue;
      const InstantiationScope Scope(*this, Instance);
      checkRoot(Instance->getBody(), Instance, Instance, Data);
    }
    return;
  }
  const auto *Function = Result.Nodes.getNodeAs<FunctionDecl>("function");
  if (!Function || isLambdaCallOperator(Function))
    return;
  // Not of the implementation of one of ThreadLocalTemplates itself.
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      Method && Data.isTemplate(Method->getParent()))
    return;
  if (!Function->isTemplated()) {
    checkRoot(Function->getBody(), Function, Function, Data);
    return;
  }
  for (const FunctionDecl *Instance :
       Instantiations.instantiationsOf(Function, Context)) {
    const InstantiationScope Scope(*this, Instance);
    checkRoot(Instance->getBody(), Instance, Instance, Data);
  }
}

void ThreadLocalStorageCheck::checkInitializer(const Expr *Init, QualType Type,
                                               const std::string &Where,
                                               bool Exempt,
                                               const CThreadLocalData &Data) {
  CheckedInit Initializer{Init, Type, Where, Exempt};
  checkRoot(Init, nullptr, nullptr, Data, &Initializer);
}

} // namespace clang::tidy::malterlib
