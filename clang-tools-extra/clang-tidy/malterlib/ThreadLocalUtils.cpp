//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ThreadLocalUtils.h"
#include "CoroutineUtils.h"
#include "MalterlibOptions.h"
#include "NamingUtils.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include <string>

namespace clang::tidy::malterlib {

bool namedFunction(const FunctionDecl *Function,
                   llvm::ArrayRef<StringRef> Patterns) {
  if (!Function)
    return false;
  // A member of a template as its class declares it.
  const FunctionDecl *Declared = Function;
  if (const FunctionDecl *Pattern = Function->getTemplateInstantiationPattern())
    Declared = Pattern;
  auto WithoutSpaces = [](StringRef Name) {
    std::string Result;
    for (const char Character : Name)
      if (Character != ' ')
        Result.push_back(Character);
    return Result;
  };
  // Not the canonical type of a conversion, which names a template's
  // parameter by its position.
  const auto *Conversion = dyn_cast<CXXConversionDecl>(Declared);
  const std::string Name = WithoutSpaces(
      Conversion ? "operator " + Conversion->getConversionType().getAsString()
                 : Declared->getNameAsString());
  for (StringRef Pattern : Patterns) {
    // The scope, split at its `::`, and the name, also of an operator whose
    // type is qualified, `CThreadData::operator NData::CData *`.
    llvm::SmallVector<StringRef, 4> Parts;
    StringRef Scope = Pattern;
    StringRef Last;
    if (const size_t Operator = Pattern.find("operator");
        Operator != StringRef::npos &&
        (Operator == 0 || Pattern.substr(0, Operator).ends_with("::"))) {
      Last = Pattern.substr(Operator);
      Scope = Operator == 0 ? StringRef() : Pattern.substr(0, Operator - 2);
    } else if (const size_t Colons = Pattern.rfind("::");
               Colons != StringRef::npos) {
      Last = Pattern.substr(Colons + 2);
      Scope = Pattern.substr(0, Colons);
    } else {
      Last = Pattern;
      Scope = StringRef();
    }
    if (!Scope.empty())
      Scope.split(Parts, "::");
    Parts.push_back(Last);
    if (WithoutSpaces(Parts.back()) != Name)
      continue;
    bool Matches = true;
    const DeclContext *Context = Function->getDeclContext();
    for (size_t iPart = Parts.size() - 1; Matches && iPart-- > 0;) {
      while (Context && !isa<NamedDecl>(Context))
        Context = Context->getParent();
      const auto *Named = dyn_cast_or_null<NamedDecl>(Context);
      Matches = Named && Named->getName() == Parts[iPart].trim();
      if (Context)
        Context = Context->getParent();
    }
    if (Matches)
      return true;
  }
  return false;
}

bool isThreadLocalData(const Expr *E, llvm::ArrayRef<StringRef> Functions) {
  const VarDecl *Variable = nullptr;
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    Variable = dyn_cast<VarDecl>(Ref->getDecl());
    // Also a name of a structured binding of one, `thread_local auto
    // [Value, Count] = ...`.
    if (const auto *Binding = dyn_cast<BindingDecl>(Ref->getDecl()))
      Variable = dyn_cast_or_null<VarDecl>(Binding->getDecomposedDecl());
  } else if (const auto *Member = dyn_cast<MemberExpr>(E))
    Variable = dyn_cast<VarDecl>(Member->getMemberDecl());
  if (Variable)
    return Variable->getTLSKind() != VarDecl::TLS_None;
  const auto *Call = dyn_cast<CallExpr>(E);
  if (!Call)
    return false;
  // Also through an unchanged local pointer to it, `auto fGet =
  // &fg_ConcurrencyThreadLocal; fGet()` (functionCalledBy).
  const FunctionDecl *Callee = Call->getDirectCallee();
  if (!Callee)
    Callee = functionCalledBy(Call);
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Callee);
  const bool ExplicitObject =
      Method && Method->isExplicitObjectMemberFunction();
  const unsigned nObjects =
      (isa<CXXOperatorCallExpr>(Call) && Method) || ExplicitObject ? 1 : 0;
  return Call->getNumArgs() == nObjects && namedFunction(Callee, Functions);
}

namespace {
/// Of the AST a thread analyzes, what it knows of thread-local data.
struct CThreadLocalFacts {
  /// The ThreadLocalTypes the types are of.
  std::string Lists;
  /// Of each canonical class, whether it is a thread-local type.
  llvm::DenseMap<const CXXRecordDecl *, bool> Types;
  /// Of each function body, what it assigns to each of its locals.
  llvm::DenseMap<
      const Stmt *,
      llvm::DenseMap<const VarDecl *, llvm::SmallVector<const Expr *, 2>>>
      Assigned;
};
} // namespace

std::string threadLocalTypeName(const CXXRecordDecl *Record) {
  llvm::SmallVector<StringRef, 4> Parts;
  for (const DeclContext *Context = Record; Context;
       Context = Context->getParent()) {
    if (const auto *Namespace = dyn_cast<NamespaceDecl>(Context)) {
      if (Namespace->isAnonymousNamespace() || Namespace->isInlineNamespace())
        continue;
      Parts.push_back(Namespace->getName());
      continue;
    }
    if (const auto *Class = dyn_cast<RecordDecl>(Context)) {
      if (!Class->getIdentifier())
        break;
      Parts.push_back(Class->getName());
      continue;
    }
    if (!Context->isTranslationUnit() && !Context->isTransparentContext())
      break;
  }
  std::string Name;
  for (StringRef Part : llvm::reverse(Parts)) {
    if (!Name.empty())
      Name += "::";
    Name += Part;
  }
  return Name;
}

bool CThreadLocalData::isListedType(const CXXRecordDecl *Record) const {
  if (!Record || !Record->getIdentifier())
    return false;
  return llvm::any_of(Lists.Types, [&](StringRef Pattern) {
    Pattern = Pattern.trim().ltrim(':');
    return Pattern.contains("::") ? threadLocalTypeName(Record) == Pattern
                                  : Record->getName() == Pattern;
  });
}

bool CThreadLocalData::isType(QualType Type, bool Through) const {
  if (Type.isNull())
    return false;
  if (Through) {
    Type = Type.getNonReferenceType();
    if (Type->isPointerType())
      Type = Type->getPointeeType();
  }
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record)
    return false;
  if (const CXXRecordDecl *Definition = Record->getDefinition())
    Record = Definition;
  Record = Record->getCanonicalDecl();
  CThreadLocalFacts &Facts = astCache<CThreadLocalFacts>(Context);
  if (const std::string Key = llvm::join(Lists.Types, ";");
      Facts.Lists != Key) {
    Facts.Lists = Key;
    Facts.Types.clear();
  }
  auto [It, Inserted] = Facts.Types.try_emplace(Record, false);
  if (Inserted)
    It->second = isListedType(Record);
  return It->second;
}

bool CThreadLocalData::isExempt(const RecordDecl *Record) const {
  const auto *Class = dyn_cast_or_null<CXXRecordDecl>(Record);
  if (!Class)
    return false;
  const QualType Type = Context.getCanonicalTagType(Class);
  return isType(Type) || isHandler(Class) ||
         matchesTypeName(Type, Lists.ScopeTypes, Context);
}

bool CThreadLocalData::isScopeType(QualType Type) const {
  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  return Record && !isHandler(Record) &&
         matchesTypeName(Type, Lists.ScopeTypes, Context);
}

bool CThreadLocalData::isHandler(const CXXRecordDecl *Record) const {
  llvm::SmallVector<const CXXRecordDecl *, 8> Work{Record};
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
  while (!Work.empty()) {
    const CXXRecordDecl *Current = Work.pop_back_val();
    if (!Current || !Current->hasDefinition() ||
        !Seen.insert(Current->getCanonicalDecl()).second)
      continue;
    Current = Current->getDefinition();
    if (matchesTypeName(Context.getCanonicalTagType(Current),
                        Lists.HandlerTypes, Context))
      return true;
    for (const CXXBaseSpecifier &Base : Current->bases())
      Work.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  return false;
}

llvm::SmallVector<const CXXRecordDecl *, 2>
CThreadLocalData::heldPerThread(QualType Type) const {
  llvm::SmallVector<const CXXRecordDecl *, 2> Held;
  llvm::SmallVector<std::pair<QualType, int>, 4> Work{{Type, 0}};
  // Each type argument, also of a pack.
  auto AddArguments = [&](llvm::ArrayRef<TemplateArgument> Arguments, int Depth,
                          auto &&AddArguments) -> void {
    for (const TemplateArgument &Argument : Arguments)
      if (Argument.getKind() == TemplateArgument::Type)
        Work.push_back({Argument.getAsType(), Depth});
      else if (Argument.getKind() == TemplateArgument::Pack)
        AddArguments(Argument.pack_elements(), Depth, AddArguments);
  };
  while (!Work.empty()) {
    auto [Current, Depth] = Work.pop_back_val();
    if (Current.isNull() || Depth > 4 || Current->isDependentType())
      continue;
    while (const ArrayType *Array = Current->getAsArrayTypeUnsafe())
      Current = Array->getElementType();
    const auto *Specialization =
        dyn_cast_or_null<ClassTemplateSpecializationDecl>(
            Current->getAsCXXRecordDecl());
    if (!Specialization)
      continue;
    const TemplateArgumentList &Arguments = Specialization->getTemplateArgs();
    if (isTemplate(Specialization)) {
      if (Arguments.size() < 1 ||
          Arguments[0].getKind() != TemplateArgument::Type)
        continue;
      const CXXRecordDecl *Class =
          Arguments[0].getAsType()->getAsCXXRecordDecl();
      if (Class && !isa<ClassTemplateSpecializationDecl>(Class) &&
          !llvm::is_contained(Held, Class))
        Held.push_back(Class);
      continue;
    }
    AddArguments(Arguments.asArray(), Depth + 1, AddArguments);
  }
  return Held;
}

bool CThreadLocalData::isTemplate(const CXXRecordDecl *Record) const {
  if (!Record)
    return false;
  if (const auto *Specialization =
          dyn_cast<ClassTemplateSpecializationDecl>(Record))
    Record = Specialization->getSpecializedTemplate()->getTemplatedDecl();
  return Record->getIdentifier() &&
         llvm::any_of(Lists.Templates, [&](StringRef Pattern) {
           const size_t Colons = Pattern.rfind("::");
           return Pattern.substr(Colons == StringRef::npos ? 0 : Colons + 2)
                      .trim() == Record->getName();
         });
}

bool CThreadLocalData::isExplicitThread(const CallExpr *Call) const {
  if (!Call)
    return false;
  const FunctionDecl *Callee = Call->getDirectCallee();
  if (!Callee)
    Callee = functionCalledBy(Call);
  return Callee && namedFunction(Callee, Functions) &&
         !isThreadLocalData(Call, Functions);
}

const Expr *CThreadLocalData::normalized(const Expr *E) const {
  for (int Depth = 0; E && Depth < 32; ++Depth) {
    const Expr *Inner = E->IgnoreParens();
    if (const auto *Full = dyn_cast<FullExpr>(Inner))
      Inner = Full->getSubExpr();
    else if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(Inner))
      Inner = Bind->getSubExpr();
    // A scalar given in braces, `int *p{&t_Value}`.
    else if (const auto *List = dyn_cast<InitListExpr>(Inner);
             List && List->getNumInits() == 1 &&
             !List->getType()->isRecordType() &&
             !List->getType()->isArrayType())
      Inner = List->getInit(0);
    if (Inner == E)
      return E;
    E = Inner;
  }
  return E;
}

/// Whether \p Relation is of the data: the object, a member of it or an
/// entry of it, not reached through a pointer it holds, which points
/// elsewhere, nor a copy, which is the copy's own. Unknown when what is made
/// of it can't be told (madeOfData).
static std::optional<bool> isOfData(const CObjectRelation &Relation) {
  if (Relation.Kind == EObjectRelation::Unknown)
    return std::nullopt;
  return Relation.Kind != EObjectRelation::Unrelated &&
         !Relation.bThroughPointer && !Relation.bCopy;
}

bool CThreadLocalData::madeOfData(
    const Expr *E, int Depth,
    llvm::SmallPtrSetImpl<const VarDecl *> &Seen) const {
  for (; E && Depth < 32; ++Depth) {
    E = normalized(E)->IgnoreParens();
    if (isRoot(E))
      return true;
    if (const auto *Cast = dyn_cast<CastExpr>(E)) {
      // A pointer read from a part points where the part does, not into it.
      if (Cast->getCastKind() == CK_LValueToRValue &&
          !isa<DeclRefExpr>(Cast->getSubExpr()->IgnoreParens()))
        return isRoot(Cast->getSubExpr()->IgnoreParens());
      E = Cast->getSubExpr();
      continue;
    }
    if (const auto *Temporary = dyn_cast<MaterializeTemporaryExpr>(E)) {
      E = Temporary->getSubExpr();
      continue;
    }
    if (const auto *Unary = dyn_cast<UnaryOperator>(E)) {
      if (Unary->getOpcode() != UO_AddrOf && Unary->getOpcode() != UO_Deref &&
          !Unary->isIncrementDecrementOp())
        return false;
      E = Unary->getSubExpr();
      continue;
    }
    if (const auto *Member = dyn_cast<MemberExpr>(E)) {
      if (!isa<FieldDecl>(Member->getMemberDecl()) ||
          Member->getMemberDecl()->getType()->isReferenceType())
        return false;
      E = Member->getBase();
      continue;
    }
    if (const auto *Element = dyn_cast<ArraySubscriptExpr>(E)) {
      E = Element->getBase();
      continue;
    }
    if (const auto *Binary = dyn_cast<BinaryOperator>(E)) {
      if (Binary->isCommaOp() || Binary->getOpcode() == BO_Assign)
        E = Binary->getRHS();
      else if (Binary->isCompoundAssignmentOp())
        E = Binary->getLHS();
      else if (Binary->isAdditiveOp() && E->getType()->isPointerType())
        E = Binary->getLHS()->getType()->isPointerType() ? Binary->getLHS()
                                                         : Binary->getRHS();
      else
        return false;
      continue;
    }
    if (const auto *Choice = dyn_cast<AbstractConditionalOperator>(E)) {
      if (const Expr *Chosen = chosenValue(Choice, &Context)) {
        E = Chosen;
        continue;
      }
      return madeOfData(Choice->getTrueExpr(), Depth + 1, Seen) ||
             madeOfData(Choice->getFalseExpr(), Depth + 1, Seen);
    }
    if (const auto *Call = dyn_cast<CallExpr>(E)) {
      if (Call->getNumArgs() != 1 ||
          !(isMoveOrForwardFunction(calleeName(Call)) || isAddressOfCall(Call)))
        return false;
      E = Call->getArg(0);
      continue;
    }
    const auto *Ref = dyn_cast<DeclRefExpr>(E);
    if (!Ref)
      return false;
    if (const auto *Binding = dyn_cast<BindingDecl>(Ref->getDecl())) {
      E = Binding->getBinding();
      continue;
    }
    const auto *Local = dyn_cast<VarDecl>(Ref->getDecl());
    if (!Local || !Local->hasLocalStorage() || isa<ParmVarDecl>(Local) ||
        !Seen.insert(Local).second)
      return false;
    llvm::SmallVector<const Expr *, 4> Values;
    if (const auto Reaching = localValuesAt(Local, Ref);
        Reaching && !Reaching->empty())
      Values = *Reaching;
    else {
      if (Local->getInit())
        Values.push_back(Local->getInit());
      llvm::append_range(Values, assignedTo(Local));
    }
    return llvm::any_of(Values, [&](const Expr *Value) {
      return madeOfData(Value, Depth + 1, Seen);
    });
  }
  return false;
}

bool CThreadLocalData::isRoot(const Expr *E) const {
  if (isThreadLocalData(E, Functions))
    return true;
  // Of a reference or a pointer to a thread-local type.
  auto Typed = [&](QualType Type) {
    return !Type.isNull() &&
           (Type->isReferenceType() || Type->isPointerType()) &&
           isType(Type, true);
  };
  // `this` of a member function of a thread-local type, also captured.
  if (isa<CXXThisExpr>(E))
    return isType(E->getType(), true);
  // What a call gives by its type, not of a thread named.
  if (const auto *Call = dyn_cast<CallExpr>(E))
    return !isExplicitThread(Call) && Typed(Call->getCallReturnType(Context));
  // A parameter or a variable that isn't local by its type, also a pointer
  // parameter its function points into the data; not a local, whose values
  // objectRelation follows (localValuesAt).
  if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
    const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
    if (!Variable ||
        ((Variable->hasLocalStorage() || Variable->isInitCapture()) &&
         !isa<ParmVarDecl>(Variable)))
      return false;
    return Typed(Variable->getType()) ||
           (isa<ParmVarDecl>(Variable) && isVariable(Variable));
  }
  // A member of a reference or a pointer to a thread-local type, not of what
  // a call that names the thread gives, `fg_SystemThreadLocal(_ThreadID)`.
  if (const auto *Member = dyn_cast<MemberExpr>(E))
    if (const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
        Field && Typed(Field->getType()))
      return objectRelation(
                 Member->getBase(),
                 [&](const Expr *Base) {
                   const auto *Call = dyn_cast<CallExpr>(Base);
                   return Call && isExplicitThread(Call);
                 },
                 &Context)
                 .Kind == EObjectRelation::Unrelated;
  return false;
}

bool CThreadLocalData::givesOtherData(const Expr *E) const {
  E = normalized(E)->IgnoreParenCasts();
  const auto *Call = dyn_cast<CallExpr>(E);
  const auto *Method =
      Call ? dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee()) : nullptr;
  if (!Method || !Method->isInstance() || isRoot(Call))
    return false;
  // Of an object reached through a pointer the data holds, `m_pArena->...`.
  const Expr *Object = nullptr;
  if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call))
    Object = Member->getImplicitObjectArgument();
  else if (isa<CXXOperatorCallExpr>(Call) && Call->getNumArgs() > 0)
    Object = Call->getArg(0);
  if (Object) {
    // What a pointer member points to, `m_pArena`, which is apart from what
    // holds it, also when it can be storage the holder owns.
    if (const auto *Member =
            dyn_cast<MemberExpr>(Object->IgnoreParenImpCasts());
        Member && Object->getType()->isPointerType() &&
        isa<FieldDecl>(Member->getMemberDecl()) && !isRoot(Member))
      return true;
    auto IsRoot = [&](const Expr *Root) { return isRoot(Root); };
    const CObjectRelation Of = Object->getType()->isPointerType()
                                   ? pointeeRelation(Object, IsRoot, &Context)
                                   : objectRelation(Object, IsRoot, &Context);
    if (Of.Kind != EObjectRelation::Unrelated && Of.bThroughPointer)
      return true;
  }
  // Of one whose body gives what a pointer it holds points to, or a copy,
  // `return pRet;` of `auto pRet = m_pAllocation;`.
  if (const std::optional<CObjectRelation> Returned =
          returnedRelation(Method, &Context))
    return Returned->Kind == EObjectRelation::Unrelated ||
           Returned->bThroughPointer || Returned->bCopy;
  return false;
}

bool CThreadLocalData::isStorage(const Expr *Target) const {
  Target = Target->IgnoreParenImpCasts();
  if (isa<DeclRefExpr>(Target) ||
      (Target->getType()->isPointerType() && isRoot(Target)))
    return false;
  return isData(Target);
}

bool CThreadLocalData::isData(const Expr *E) const {
  if (!E)
    return false;
  E = normalized(E);
  if (givesOtherData(E))
    return false;
  if (const std::optional<bool> Of = isOfData(objectRelation(
          E, [&](const Expr *Root) { return isRoot(Root); }, &Context)))
    return *Of;
  llvm::SmallPtrSet<const VarDecl *, 8> Seen;
  return madeOfData(E, 0, Seen);
}

bool CThreadLocalData::pointsTo(const Expr *E) const {
  if (!E)
    return false;
  E = normalized(E);
  // Not an object made on the heap, which is new, whatever it holds.
  if (isa<CXXNewExpr>(E->IgnoreParenImpCasts()) || givesOtherData(E))
    return false;
  if (const std::optional<bool> Of = isOfData(pointeeRelation(
          E, [&](const Expr *Root) { return isRoot(Root); }, &Context)))
    return *Of;
  llvm::SmallPtrSet<const VarDecl *, 8> Seen;
  return madeOfData(E, 0, Seen);
}

const VarDecl *CThreadLocalData::boundLocal(const VarDecl *Reference) const {
  for (int Depth = 0; Reference && Depth < 16; ++Depth) {
    // A local, also a parameter held by value.
    if (!Reference->getType()->isReferenceType())
      return Reference->hasLocalStorage() ? Reference : nullptr;
    if (!Reference->hasLocalStorage() || isa<ParmVarDecl>(Reference) ||
        !Reference->getInit())
      return nullptr;
    const Expr *Init = normalized(Reference->getInit())->IgnoreParenCasts();
    const auto *Ref = dyn_cast<DeclRefExpr>(Init);
    Reference = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  }
  return nullptr;
}

bool CThreadLocalData::refersTo(const Expr *E, QualType Bound) const {
  if (!E || Bound.isNull())
    return false;
  // A reference to a pointer, by the pointer it gives, `int *const &_pValue`.
  if (Bound->isReferenceType() && Bound.getNonReferenceType()->isPointerType())
    return pointsTo(E);
  if (Bound->isReferenceType())
    return isData(E);
  if (Bound->isPointerType())
    return pointsTo(E);
  // Of an array of pointers, any of its elements.
  if (Bound->isArrayType()) {
    if (const auto *List = dyn_cast<InitListExpr>(normalized(E)))
      return llvm::any_of(List->inits(), [&](const Expr *Each) {
        return Each && pointsTo(Each);
      });
    return pointsTo(E);
  }
  return false;
}

const llvm::SmallVector<const Expr *, 2> &
CThreadLocalData::assignedTo(const VarDecl *Variable) const {
  static const llvm::SmallVector<const Expr *, 2> None;
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  if (!Function || !Function->getBody())
    return None;
  CThreadLocalFacts &Facts = astCache<CThreadLocalFacts>(Context);
  auto [Found, New] = Facts.Assigned.try_emplace(Function->getBody());
  if (New) {
    // What each assignment that can run (canRunIn) gives a local pointer,
    // also a parameter held by value, through a local reference to it,
    // `int *&rpValue = pValue; rpValue = ...`, or an element of a local
    // array of them, also through a reference to it, `pValues[0] = ...`; not
    // a copy a lambda captures, `[pValue]() mutable { pValue = ...; }`.
    llvm::DenseMap<const VarDecl *, llvm::SmallVector<const Expr *, 2>>
        Assigned;
    std::function<void(const Stmt *, const llvm::DenseSet<const VarDecl *> &)>
        Visit =
            [&](const Stmt *S, const llvm::DenseSet<const VarDecl *> &Copies) {
              if (!S)
                return;
              if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
                llvm::DenseSet<const VarDecl *> Inner = Copies;
                for (const LambdaCapture &Capture : Lambda->captures())
                  if (Capture.capturesVariable() &&
                      Capture.getCaptureKind() == LCK_ByCopy)
                    if (const auto *Copied =
                            dyn_cast<VarDecl>(Capture.getCapturedVar()))
                      Inner.insert(Copied);
                for (const Expr *Init : Lambda->capture_inits())
                  Visit(Init, Copies);
                Visit(Lambda->getBody(), Inner);
                return;
              }
              if (const auto *Assign = dyn_cast<BinaryOperator>(S);
                  Assign && Assign->getOpcode() == BO_Assign &&
                  canRunIn(Assign, Function->getBody(), Function, Context))
                if (const VarDecl *Target = assignedLocal(Assign->getLHS());
                    Target && !Copies.contains(Target))
                  Assigned[Target].push_back(Assign->getRHS());
              for (const Stmt *Child : evaluatedChildren(S, Context))
                Visit(Child, Copies);
            };
    Visit(Function->getBody(), {});
    astCache<CThreadLocalFacts>(Context).Assigned[Function->getBody()] =
        std::move(Assigned);
  }
  const auto &Assigned =
      astCache<CThreadLocalFacts>(Context).Assigned[Function->getBody()];
  if (auto Values = Assigned.find(Variable); Values != Assigned.end())
    return Values->second;
  return None;
}

const VarDecl *CThreadLocalData::assignedLocal(const Expr *Target) const {
  Target = Target->IgnoreParenImpCasts();
  if (const auto *Element = dyn_cast<ArraySubscriptExpr>(Target)) {
    const auto *Base =
        dyn_cast<DeclRefExpr>(Element->getBase()->IgnoreParenImpCasts());
    const auto *Named = Base ? dyn_cast<VarDecl>(Base->getDecl()) : nullptr;
    // Also through a local reference to the array, `auto &Values =
    // pValues`.
    const VarDecl *Array = Named ? boundLocal(Named) : nullptr;
    return Array && Array->getType()->isArrayType() ? Array : nullptr;
  }
  const auto *Ref = dyn_cast<DeclRefExpr>(Target);
  const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  return Variable ? boundLocal(Variable) : nullptr;
}

bool CThreadLocalData::isVariable(const VarDecl *Variable) const {
  if (!Variable)
    return false;
  QualType Type = Variable->getType();
  QualType Element = Type;
  while (const ArrayType *Array = Element->getAsArrayTypeUnsafe())
    Element = Array->getElementType();
  if (!Type->isReferenceType() && !Type->isPointerType() &&
      !(Type->isArrayType() && Element->isPointerType()))
    return false;
  const bool Parameter = isa<ParmVarDecl>(Variable);
  const bool Local =
      (Variable->hasLocalStorage() || Variable->isInitCapture()) && !Parameter;
  // A parameter of a reference or a pointer to a thread-local type, or a
  // variable that isn't local.
  if (!Local && isType(Type, true))
    return true;
  if (!Local && !(Parameter && Type->isPointerType()))
    return false;
  if (auto It = Variables.find(Variable); It != Variables.end())
    return It->second;
  // While it is decided, it doesn't.
  Variables[Variable] = false;
  bool Refers = false;
  if (const Expr *Init = Local ? Variable->getInit() : nullptr) {
    if (Type->isArrayType()) {
      if (const auto *List = dyn_cast<InitListExpr>(normalized(Init)))
        Refers = llvm::any_of(List->inits(), [&](const Expr *Each) {
          return Each && pointsTo(Each);
        });
    } else
      Refers = refersTo(Init, Type);
  }
  // Of a pointer or an array of them, also what its function assigns it,
  // also of a parameter held by value.
  if (!Refers && !Type->isReferenceType())
    Refers = llvm::any_of(assignedTo(Variable),
                          [&](const Expr *Value) { return pointsTo(Value); });
  Variables[Variable] = Refers;
  return Refers;
}

} // namespace clang::tidy::malterlib
