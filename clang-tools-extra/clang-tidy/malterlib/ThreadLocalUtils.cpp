//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ThreadLocalUtils.h"
#include "CheckedAccess.h"
#include "HeldData.h"
#include "ValueFlow.h"
#include "CoroutineUtils.h"
#include "MalterlibOptions.h"
#include "NamingUtils.h"
#include "clang/AST/Attr.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/ScopeExit.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
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
      // Not past a function, a local class's, whose name can be an operator's
      // or a constructor's, which isn't an identifier.
      const auto *Named = dyn_cast_or_null<NamedDecl>(Context);
      Matches = Named && !isa<FunctionDecl>(Named) && Named->getIdentifier() &&
                identifierTextOf(Named) == Parts[iPart].trim();
      if (Context)
        Context = Context->getParent();
    }
    if (Matches)
      return true;
  }
  return false;
}

/// The function \p E, a call (EStatement::Call), calls: directly, or through
/// an unchanged local pointer to it, `auto fGet = &fg_ConcurrencyThreadLocal;
/// fGet()` (functionCalledBy); null of what isn't a call.
static const FunctionDecl *calleeOf(const Expr *E, const ASTContext &Context) {
  return callTarget(E, Context);
}

/// The variable \p E names, by what the declaration it names is
/// (holderKind), a name or a member: a variable, also a static member, or
/// what a structured binding binds, `thread_local auto [Value, Count] =
/// ...`; null otherwise.
static const VarDecl *namedVariable(const Expr *E, const ASTContext &Context) {
  const CEffects &Of = effectsOf(E, Context);
  const ValueDecl *Named = nullptr;
  switch (Of.Statement) {
  case EStatement::Name:
    Named = cast<DeclRefExpr>(E)->getDecl();
    break;
  case EStatement::Member:
    Named = memberDecl(E, Context);
    break;
  case EStatement::Other:
  case EStatement::This:
  case EStatement::Call:
  case EStatement::Construction:
  case EStatement::Lambda:
  case EStatement::Declaration:
  case EStatement::RangeLoop:
  case EStatement::CoroutineBody:
  case EStatement::Temporary:
  case EStatement::New:
  case EStatement::Return:
  case EStatement::Coreturn:
  case EStatement::Default:
  case EStatement::InheritedConstruction:
  case EStatement::InitializerList:
    return nullptr;
  }
  if (!Named)
    return nullptr;
  switch (holderKind(Named)) {
  case EHolder::Variable:
  case EHolder::Decomposition:
    return cast<VarDecl>(Named);
  // What a structured binding binds, when that is a part of what it
  // decomposes held in it, a member or an element, not what a reference
  // member refers to, nor what a tuple-like class gives.
  case EHolder::Binding: {
    const auto *Binding = cast<BindingDecl>(Named);
    const auto *Decomposed =
        dyn_cast_or_null<VarDecl>(Binding->getDecomposedDecl());
    for (const Expr *Part = Binding->getBinding(); Part && Decomposed;) {
      Part = Part->IgnoreImpCasts();
      if (const auto *Ref = dyn_cast<DeclRefExpr>(Part))
        return Ref->getDecl() == Decomposed ? Decomposed : nullptr;
      if (const auto *Member = dyn_cast<MemberExpr>(Part);
          Member && !Member->isArrow() &&
          !Member->getMemberDecl()->getType()->isReferenceType())
        Part = Member->getBase();
      else if (const auto *Element = dyn_cast<ArraySubscriptExpr>(Part))
        Part = Element->getBase();
      else
        return nullptr;
    }
    return nullptr;
  }
  case EHolder::None:
  case EHolder::Parameter:
  case EHolder::ImplicitParameter:
  case EHolder::Field:
  case EHolder::IndirectField:
  case EHolder::Temporary:
  case EHolder::Constant:
  case EHolder::Function:
  case EHolder::Statement:
  case EHolder::Opaque:
    return nullptr;
  }
  return nullptr;
}

bool objectCopiedAround(const LambdaExpr *Lambda) {
  // Outward through the call operators of the closures it is in.
  const DeclContext *Around =
      Lambda ? Lambda->getLambdaClass()->getDeclContext() : nullptr;
  for (int Depth = 0; Around && Depth < 16; ++Depth) {
    const auto *Operator = dyn_cast<CXXMethodDecl>(Around);
    const CXXRecordDecl *Closure = Operator ? Operator->getParent() : nullptr;
    if (!Closure || !Closure->isLambda())
      return false;
    bool CapturesThis = false;
    for (const LambdaCapture &Capture : Closure->captures())
      switch (Capture.getCaptureKind()) {
      case LCK_StarThis:
        return true;
      case LCK_This:
        CapturesThis = true;
        break;
      case LCK_ByCopy:
      case LCK_ByRef:
      case LCK_VLAType:
        break;
      }
    if (!CapturesThis)
      return false;
    Around = Closure->getDeclContext();
  }
  return false;
}

bool isThreadLocalData(const ASTContext &Context, const Expr *E,
                       llvm::ArrayRef<StringRef> Functions) {
  // Not a reference, which is what it refers to, `thread_local int *&t_rpSlot
  // = g_pValue` (CThreadLocalData::reach).
  if (const VarDecl *Variable = namedVariable(E, Context))
    return Variable->getTLSKind() != VarDecl::TLS_None &&
           !Variable->getType()->isReferenceType();
  const FunctionDecl *Callee = calleeOf(E, Context);
  if (!Callee)
    return false;
  // Given nothing but its object (EUse::CallObject), also an explicit object
  // parameter: no argument given to another parameter.
  const CEffects &Of = effectsOf(E, Context);
  llvm::SmallPtrSet<const Stmt *, 2> Objects;
  for (const COperandEffect &Effect : Of.Operands)
    if (Effect.Use == EUse::CallObject)
      Objects.insert(Effect.Operand);
  const bool OnlyObject =
      llvm::none_of(Of.Operands, [&](const COperandEffect &Effect) {
        return Effect.Into.Kind == EDestination::Parameter &&
               !Objects.contains(Effect.Operand);
      });
  return OnlyObject && namedFunction(Callee, Functions);
}

namespace {
/// A variable as it is asked of where the roots of a template's pattern
/// aren't, or `this` is a copy, and where (CThreadLocalData::isVariable).
using CVariableKey = std::tuple<const VarDecl *, unsigned, const DeclContext *>;

/// Of the AST a thread analyzes, what it knows of thread-local data.
struct CThreadLocalFacts {
  /// The lists what is told is of (CThreadLocalData::listsKey), and the AST.
  std::string Lists;
  const CThreadLocalLists *Of = nullptr;
  ASTContext *Context = nullptr;
  /// Of each canonical class, whether it is a thread-local type.
  llvm::DenseMap<const CXXRecordDecl *, bool> Types;
  /// Of each function, whether what it gives back is thread-local data of its
  /// own (givesData); while it is decided, it doesn't.
  TCSummaries<const FunctionDecl *, bool> Gives{
      [this](const FunctionDecl *const &Definition) {
        return CThreadLocalData(*Context, *Of).givesDataUncached(Definition);
      },
      false, false};
  /// Of each variable, as it is asked of, whether it refers to the data
  /// (isVariable); while it is decided, it doesn't.
  TCSummaries<CVariableKey, bool> Variables{
      [this](const CVariableKey &Key) {
        const CThreadLocalData Data(*Context, *Of);
        Data.setPatternExcluded((std::get<1>(Key) & 1) != 0);
        Data.setObjectCopied((std::get<1>(Key) & 2) != 0, std::get<2>(Key));
        return Data.isVariableUncached(std::get<0>(Key));
      },
      false, false};
};

/// What is kept of the AST of \p Context for the lists \p Of, whose key is
/// \p Lists.
CThreadLocalFacts &threadLocalFacts(ASTContext &Context,
                                    const CThreadLocalLists &Of,
                                    const std::string &Lists) {
  CThreadLocalFacts &Facts = astCache<CThreadLocalFacts>(Context);
  if (Facts.Lists != Lists) {
    Facts.Lists = Lists;
    Facts.Types.clear();
    Facts.Gives.clear();
    Facts.Variables.clear();
  }
  Facts.Of = &Of;
  Facts.Context = &Context;
  return Facts;
}
} // namespace

const std::string &CThreadLocalData::listsKey() const {
  if (ListsKey.empty())
    for (llvm::ArrayRef<StringRef> List :
         {llvm::ArrayRef<StringRef>(Lists.Functions),
          llvm::ArrayRef<StringRef>(Lists.Types),
          llvm::ArrayRef<StringRef>(Lists.ScopeTypes),
          llvm::ArrayRef<StringRef>(Lists.Templates),
          llvm::ArrayRef<StringRef>(Lists.HandlerTypes),
          llvm::ArrayRef<StringRef>(Lists.ScopeExitHelpers)})
      ListsKey += llvm::join(List, ";") + "|";
  return ListsKey;
}

std::string threadLocalTypeName(const CXXRecordDecl *Record) {
  llvm::SmallVector<StringRef, 4> Parts;
  for (const DeclContext *Context = Record; Context;
       Context = Context->getParent()) {
    if (const auto *Namespace = dyn_cast<NamespaceDecl>(Context)) {
      if (Namespace->isAnonymousNamespace() || Namespace->isInlineNamespace())
        continue;
      Parts.push_back(identifierTextOf(Namespace));
      continue;
    }
    if (const auto *Class = dyn_cast<RecordDecl>(Context)) {
      if (!Class->getIdentifier())
        break;
      Parts.push_back(identifierTextOf(Class));
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

bool dependentInPattern(const ParmVarDecl *Parameter) {
  const auto *Function = dyn_cast<FunctionDecl>(Parameter->getDeclContext());
  const FunctionDecl *Pattern =
      Function ? Function->getTemplateInstantiationPattern() : nullptr;
  if (!Pattern)
    return false;
  const unsigned Index = Parameter->getFunctionScopeIndex();
  // A pack is as many as it expands to, those after it the rest, by their
  // place from the end.
  const unsigned nPacks = llvm::count_if(
      Pattern->parameters(),
      [](const ParmVarDecl *Each) { return Each->isParameterPack(); });
  if (nPacks > 1 || Function->getNumParams() + nPacks < Pattern->getNumParams())
    return true;
  const unsigned nExpanded =
      Function->getNumParams() + nPacks - Pattern->getNumParams();
  unsigned iInstantiated = 0;
  for (const ParmVarDecl *Patterned : Pattern->parameters()) {
    const unsigned nMade = Patterned->isParameterPack() ? nExpanded : 1;
    if (Index < iInstantiated + nMade)
      return Patterned->isParameterPack() ||
             Patterned->getType()->isDependentType();
    iInstantiated += nMade;
  }
  return false;
}

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

void CThreadLocalData::setPatternExcluded(bool Excluded) const {
  bPatternExcluded = Excluded;
}

bool CThreadLocalData::isListedType(const CXXRecordDecl *Record) const {
  if (!Record || !Record->getIdentifier())
    return false;
  return llvm::any_of(Lists.Types, [&](StringRef Pattern) {
    Pattern = Pattern.trim().ltrim(':');
    return Pattern.contains("::") ? threadLocalTypeName(Record) == Pattern
                                  : identifierTextOf(Record) == Pattern;
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
  CThreadLocalFacts &Facts = threadLocalFacts(Context, Lists, listsKey());
  if (auto It = Facts.Types.find(Record); It != Facts.Types.end())
    return It->second;
  // Added once told: telling it can add others.
  const bool Listed = isListedType(Record);
  Facts.Types[Record] = Listed;
  return Listed;
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
  // Each type expanded once at the smallest depth it is reached at: a type
  // shared by many arguments, `TCList<L4, L4, ...>`, would be expanded once
  // per way to it.
  llvm::DenseMap<const clang::Type *, int> Expanded;
  while (!Work.empty()) {
    auto [Current, Depth] = Work.pop_back_val();
    if (Current.isNull() || Depth > 4 || Current->isDependentType())
      continue;
    if (const auto [It, Inserted] = Expanded.try_emplace(
            Current.getCanonicalType().getTypePtr(), Depth);
        !Inserted) {
      if (It->second <= Depth)
        continue;
      It->second = Depth;
    }
    while (const ArrayType *Array = Current->getAsArrayTypeUnsafe())
      Current = Array->getElementType();
    // Also through the bases of a class, `struct CMine :
    // TCThreadLocal<CData> {}`.
    if (const CXXRecordDecl *Record = Current->getAsCXXRecordDecl();
        Record && Record->hasDefinition())
      for (const CXXBaseSpecifier &Base : Record->getDefinition()->bases())
        Work.push_back({Base.getType(), Depth + 1});
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
  // By its name, or by its qualified name when the entry has `::`, like
  // ThreadLocalTypes (isListedType).
  return Record->getIdentifier() &&
         llvm::any_of(Lists.Templates, [&](StringRef Pattern) {
           Pattern = Pattern.trim().ltrim(':');
           return Pattern.contains("::")
                      ? threadLocalTypeName(Record) == Pattern
                      : identifierTextOf(Record) == Pattern;
         });
}

bool CThreadLocalData::isExplicitThread(const Expr *Call) const {
  if (!Call)
    return false;
  const FunctionDecl *Callee = calleeOf(Call, Context);
  return Callee && namedFunction(Callee, Functions) &&
         !isThreadLocalData(Context, Call, Functions);
}

bool CThreadLocalData::reach(
    const Expr *E, unsigned Level,
    llvm::function_ref<EReach(const Expr *At, unsigned Level, bool Part)> Visit,
    bool *Ended) const {
  return Provenance.reach(E, Level, Visit, Ended);
}

void CThreadLocalData::step(
    const Expr *At, unsigned AtLevel, bool Part,
    llvm::function_ref<void(const Expr *Next, unsigned Level, bool Part)> Next,
    bool &Complete) const {
  Provenance.step(At, AtLevel, Part, Next, Complete);
}

// Whether the member \p Member is of local slots that aren't only elements
// and holds there what its function tells (localSlotValues): each value it
// can hold where it is known, none given to the function; what a member of a
// local aggregate is made with, `CLink Link{&Copy}`, not by its type.
static bool toldLocalMember(const MemberExpr *Member, ASTContext &Context) {
  const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
  if (!Field)
    return false;
  bool Told = true;
  llvm::SmallVector<CSlot, 2> Slots =
      Member->isArrow() ? pointedSlots(Member->getBase(), Context, &Told)
                        : designatedSlots(Member->getBase(), Context, &Told);
  if (!Told || Slots.empty())
    return false;
  for (CSlot &Slot : Slots) {
    if (Slot.Element)
      return false;
    Slot.Path.push_back(Field);
    const CSlotValues Values = localSlotValues(Slot, Member, Context);
    if (!Values.Told || Values.Values.empty() ||
        llvm::any_of(Values.Values, [](const CSlotValue &Value) {
          return Value.Kind == CSlotValue::EKind::Given;
        }))
      return false;
  }
  return true;
}

EReach CThreadLocalData::rootAt(const Expr *E, unsigned Level) const {
  // Of a reference or a pointer to a thread-local type.
  auto Typed = [&](QualType Type) {
    return !Type.isNull() &&
           (Type->isReferenceType() || Type->isPointerType()) &&
           isType(Type, true);
  };
  // Whether \p Object is made of what a call that names the thread gives,
  // `fg_SystemThreadLocal(_ThreadID)`.
  // Of each alternative, not one of them alone.
  auto OfExplicitThread = [&](const Expr *Object) {
    bool Found = false;
    bool Ended = false;
    reach(
        Object, 0,
        [&](const Expr *At, unsigned, bool) {
          if (!isExplicitThread(At))
            return EReach::Continue;
          Found = true;
          return EReach::Root;
        },
        &Ended);
    return Found && !Ended;
  };
  const CEffects &Of = effectsOf(E, Context);
  // The declared type of what it names, and whether that holds it apart from
  // its function: a parameter or a variable that isn't local, a member.
  QualType Declared;
  bool Kept = false;
  switch (Of.Statement) {
  case EStatement::Name: {
    const ValueDecl *Named = cast<DeclRefExpr>(E)->getDecl();
    switch (holderKind(Named)) {
    case EHolder::Parameter: {
      const auto *Parameter = cast<ParmVarDecl>(Named);
      // Not a parameter of a type of the pattern, while those aren't, nor
      // the explicit object of a copy of the object.
      const auto *Method =
          dyn_cast<CXXMethodDecl>(Parameter->getDeclContext());
      if ((bPatternExcluded && dependentInPattern(Parameter)) ||
          (Method && Method->isExplicitObjectMemberFunction() &&
           Parameter->getFunctionScopeIndex() == 0 && copiedIn(Method)))
        return EReach::Stop;
      Declared = Parameter->getType();
      Kept = true;
      break;
    }
    case EHolder::Variable:
    case EHolder::Decomposition: {
      const auto *Variable = cast<VarDecl>(Named);
      Declared = Variable->getType();
      Kept = !(Variable->hasLocalStorage() || Variable->isInitCapture());
      break;
    }
    case EHolder::None:
    case EHolder::ImplicitParameter:
    case EHolder::Binding:
    case EHolder::Field:
    case EHolder::IndirectField:
    case EHolder::Temporary:
    case EHolder::Constant:
    case EHolder::Function:
    case EHolder::Statement:
    case EHolder::Opaque:
      break;
    }
    break;
  }
  // Not one of a type of the pattern of the object, while those aren't, nor
  // of what a call that names the thread gives.
  // A static data member, like a variable that isn't local, whatever names
  // it, `Provider.ms_pData`.
  case EStatement::Member: {
    const ValueDecl *Member = memberDecl(E, Context);
    if (holderKind(Member) == EHolder::Variable) {
      Declared = Member->getType();
      Kept = true;
      break;
    }
    if (holderKind(Member) != EHolder::Field)
      break;
    const Expr *Base = cast<MemberExpr>(E)->getBase();
    const bool OfThis = effectsOf(sameValue(Base, Context), Context).Statement ==
                        EStatement::This;
    // Also an array of them, `CData *m_pData[2]`, whose elements are stored
    // in it, and a pointer to one, at any depth, `CData **m_ppData`, which
    // the levels below tell.
    QualType Elements = Member->getType();
    while (const ArrayType *Array = Elements->getAsArrayTypeUnsafe())
      Elements = Array->getElementType();
    bool TypedAtDepth = Typed(Member->getType());
    for (QualType Pointer = Elements; !TypedAtDepth && !Pointer.isNull();
         Pointer = Pointer->isPointerType() ? Pointer->getPointeeType()
                                             : QualType())
      TypedAtDepth = Typed(Pointer);
    if (!TypedAtDepth)
      break;
    if ((bPatternExcluded && OfThis &&
         dependentInPattern(cast<FieldDecl>(Member))) ||
        OfExplicitThread(Base))
      return EReach::Stop;
    Declared = Member->getType();
    // Of a local by what it holds (toldLocalMember), which reach follows,
    // also a reference member by what it is bound to.
    Kept = (Level == 0 && !Member->getType()->isReferenceType()) ||
           !toldLocalMember(cast<MemberExpr>(E), Context);
    break;
  }
  case EStatement::Other:
  case EStatement::This:
  case EStatement::Call:
  case EStatement::Construction:
  case EStatement::Lambda:
  case EStatement::Declaration:
  case EStatement::RangeLoop:
  case EStatement::CoroutineBody:
  case EStatement::Temporary:
  case EStatement::New:
  case EStatement::Return:
  case EStatement::Coreturn:
  case EStatement::Default:
  case EStatement::InheritedConstruction:
  case EStatement::InitializerList:
    break;
  }
  // A member a pointer to a data member selects, like the member it names,
  // `Link.*&CDataLink::m_pData`.
  if (const auto *Select = dyn_cast<BinaryOperator>(E);
      Select && Select->isPtrMemOp() &&
      !E->getType()->isSpecificPlaceholderType(BuiltinType::BoundMember))
    if (const auto *Pointer =
            Select->getRHS()->getType()->getAs<MemberPointerType>()) {
      QualType Elements = Pointer->getPointeeType();
      while (const ArrayType *Array = Elements->getAsArrayTypeUnsafe())
        Elements = Array->getElementType();
      if (Typed(Pointer->getPointeeType()) || Typed(Elements)) {
        if (OfExplicitThread(Select->getLHS()))
          return EReach::Stop;
        Declared = Pointer->getPointeeType();
        Kept = true;
      }
    }
  // What it declares is stored in it, or, of a reference, in what it refers
  // to, of an array in each element.
  auto Element = [](QualType Type) {
    if (Type.isNull())
      return Type;
    Type = Type.getNonReferenceType();
    while (const ArrayType *Array = Type->getAsArrayTypeUnsafe())
      Type = Array->getElementType();
    return Type;
  };
  const QualType Stored = Element(Declared);
  // Of a call that gives a reference to a pointer, the pointer slot it gives.
  const QualType Returned =
      Of.Statement == EStatement::Call
          ? cast<CallExpr>(E)->getCallReturnType(Context)
          : QualType();
  const bool SlotReturned =
      !Returned.isNull() && Returned->isReferenceType() &&
      Returned.getNonReferenceType()->isPointerType();
  if (Level > 0) {
    // The value of a pointer of a pointer to a thread-local type stored where
    // its function doesn't tell what it holds, also an element of an array
    // of them, or in a slot a call gives; not the value stored in
    // thread-local storage, which points elsewhere.
    // Also deeper, what a pointer to such a pointer points to, `*ppData`;
    // of a parameter assigned where it is read, what it is assigned, unless
    // what it is given can reach there too.
    QualType Pointer = Stored;
    for (unsigned Step = 1; Step < Level && !Pointer.isNull(); ++Step)
      Pointer = Pointer->isPointerType() ? Pointer->getPointeeType()
                                         : QualType();
    if (Kept && !Pointer.isNull() && Pointer->isPointerType() &&
        Typed(Pointer)) {
      const auto *Parameter =
          Of.Statement == EStatement::Name
              ? dyn_cast<ParmVarDecl>(cast<DeclRefExpr>(E)->getDecl())
              : nullptr;
      bool Given = true;
      if (Parameter && !Parameter->getType()->isReferenceType())
        assignedAt(Parameter, E, Context, &Given);
      return Given ? EReach::Root : EReach::Continue;
    }
    if (Level == 1 && SlotReturned && Typed(Returned.getNonReferenceType()) &&
        !isExplicitThread(E))
      return EReach::Root;
    // What is stored in thread-local storage, by the type stored there:
    // of a glvalue its own, of a pointer an accessor gives what it points
    // to, `CData *` of `TCThreadLocal<CData *>::f_Get()`, not `CData` of
    // `TCThreadLocal<CData>::f_Get()`.
    if (Level == 1 && isThreadLocalData(Context, E, Functions)) {
      const QualType Here = E->isGLValue() ? E->getType()
                            : E->getType()->isPointerType()
                                ? E->getType()->getPointeeType()
                                : QualType();
      return Typed(Element(Here)) ? EReach::Root : EReach::Stop;
    }
    return EReach::Continue;
  }
  if (isThreadLocalData(Context, E, Functions))
    return EReach::Root;
  switch (Of.Statement) {
  // `this` of a member function of a thread-local type, also captured, not
  // of a copy of the object.
  case EStatement::This:
    return !thisCopied(E) && isType(E->getType(), true) ? EReach::Root
                                                         : EReach::Stop;
  // What a call gives by its type, not of a thread named, also through a
  // member function of what it gives, `fg_SystemThreadLocal(_ThreadID).
  // f_Self()`; else what its body gives back of thread-local data, whatever
  // it is called on.
  case EStatement::Call: {
    if (isExplicitThread(E))
      return EReach::Stop;
    // What gives back what it is given, by that (givesItsArgument), and
    // what gives its object or a part of it as its body tells, by what it is
    // called on (relatedToObject), also what each way of each of its returns
    // ends at, of what it is called with (returnedRoots), `fg_Const(Copy)`,
    // before its type.
    if (givesItsArgument(cast<CallExpr>(E)) || relatedToObject(E))
      return EReach::Continue;
    if (const FunctionDecl *Callee = callTarget(E, Context)) {
      const auto *Method = dyn_cast<CXXMethodDecl>(Callee);
      if (const CReturnedRoots *Roots =
              Method && mayRunOverride(cast<CallExpr>(E), Method)
                  ? nullptr
                  : Provenance.returnedRoots(Callee);
          Roots && !Roots->Other && !Roots->Roots.empty())
        return EReach::Continue;
    }
    // A slot of a pointer is what its body gives (givesData), not by the
    // type of the pointer, which is what is stored in it.
    if (SlotReturned)
      return givesData(callTarget(E, Context)) ? EReach::Root
                                               : EReach::Continue;
    if (Typed(cast<CallExpr>(E)->getCallReturnType(Context))) {
      const Expr *Object = callObject(E, Context);
      return (!Object || !OfExplicitThread(Object)) &&
                     !givesExplicitThread(E, OfExplicitThread)
                 ? EReach::Root
                 : EReach::Stop;
    }
    return givesData(callTarget(E, Context)) ? EReach::Root
                                             : EReach::Continue;
  }
  // A reference to a thread-local type held apart, not to a pointer.
  case EStatement::Name:
  case EStatement::Member:
    return Kept && Declared->isReferenceType() && !Stored->isPointerType() &&
                   isType(Stored)
               ? EReach::Root
               : EReach::Continue;
  case EStatement::Other:
  case EStatement::Construction:
  case EStatement::Lambda:
  case EStatement::Declaration:
  case EStatement::RangeLoop:
  case EStatement::CoroutineBody:
  case EStatement::Temporary:
  case EStatement::New:
  case EStatement::Return:
  case EStatement::Coreturn:
  case EStatement::Default:
  case EStatement::InheritedConstruction:
  case EStatement::InitializerList:
    return EReach::Continue;
  }
  return EReach::Continue;
}

bool CThreadLocalData::givesExplicitThread(
    const Expr *Call,
    llvm::function_ref<bool(const Expr *)> OfExplicitThread) const {
  if (stackNearlyExhausted())
    return withStack([&] { return givesExplicitThread(Call, OfExplicitThread); });
  const FunctionDecl *Callee = callTarget(Call, Context);
  const FunctionDecl *Definition = Callee ? Callee->getDefinition() : nullptr;
  if (!Definition || !Definition->getBody() ||
      isa<CoroutineBodyStmt>(Definition->getBody()))
    return false;
  // One asked again while it is told doesn't, which is assumed of it.
  if (!DecidingHelpers.insert(Definition).second)
    return false;
  const llvm::scope_exit Decided([&] { DecidingHelpers.erase(Definition); });
  // Each alternative of each return (reach) ends at a parameter whose
  // argument is of a thread named, or at what a thread named gives itself,
  // `return fg_SystemThreadLocal(_ThreadID);`, also through another such
  // helper, and one of them does; none at another root, or where it can't be
  // told. What refers to nothing, `return nullptr;`, adds no way, the
  // identity of the merge, as of what the returns give (refersToNothing).
  bool Any = false;
  const bool All = llvm::all_of(
      writtenReturns(Definition, Context), [&](const Expr *Value) {
    bool Each = true;
    bool Ended = false;
    const bool Complete = reach(
        Value, 0,
        [&](const Expr *At, unsigned Level, bool) {
          if (refersToNothing(At, Level))
            return EReach::Root;
          if (effectsOf(At, Context).Statement == EStatement::Call &&
              (isExplicitThread(At) ||
               givesExplicitThread(At, OfExplicitThread))) {
            Any = true;
            return EReach::Root;
          }
          if (const auto *Named = effectsOf(At, Context).Statement ==
                                          EStatement::Name
                                      ? dyn_cast<ParmVarDecl>(
                                            cast<DeclRefExpr>(At)->getDecl())
                                      : nullptr;
              Named && Named->getDeclContext() == Definition) {
            const Expr *Argument =
                argumentFor(Call, Named->getFunctionScopeIndex(), Context);
            Each &= Argument && OfExplicitThread(Argument);
            Any = true;
            return EReach::Root;
          }
          if (rootAt(At, Level) == EReach::Root) {
            Each = false;
            return EReach::Root;
          }
          return EReach::Continue;
        },
        &Ended);
    return Each && !Ended && Complete;
  });
  return All && Any;
}

bool CThreadLocalData::isRoot(const Expr *E) const {
  if (isThreadLocalData(Context, E, Functions))
    return true;
  // Of a reference or a pointer to a thread-local type.
  auto Typed = [&](QualType Type) {
    return !Type.isNull() &&
           (Type->isReferenceType() || Type->isPointerType()) &&
           isType(Type, true);
  };
  // Whether \p Object is made of what a call that names the thread gives,
  // `fg_SystemThreadLocal(_ThreadID)`.
  auto OfExplicitThread = [&](const Expr *Object) {
    return objectRelation(
               Object,
               [&](const Expr *Base) { return isExplicitThread(Base); },
               &Context)
               .Kind != EObjectRelation::Unrelated;
  };
  // By what it is (effectsOf).
  switch (effectsOf(E, Context).Statement) {
  // `this` of a member function of a thread-local type, also captured, not of
  // a copy of the object.
  case EStatement::This:
    return !thisCopied(E) && isType(E->getType(), true);
  // What a call gives by its type, not of a thread named, also through a
  // member function of what it gives, `fg_SystemThreadLocal(_ThreadID).
  // f_Self()`; else what its body gives back of thread-local data, whatever
  // it is called on: a getter of a `thread_local` variable or of what an
  // accessor gives, of any type.
  case EStatement::Call: {
    if (isExplicitThread(E) || givesItsArgument(cast<CallExpr>(E)) ||
        relatedToObject(E))
      return false;
    if (!Typed(cast<CallExpr>(E)->getCallReturnType(Context)))
      return givesData(callTarget(E, Context));
    const Expr *Object = callObject(E, Context);
    return (!Object || !OfExplicitThread(Object)) &&
           !givesExplicitThread(E, OfExplicitThread);
  }
  // A parameter or a variable that isn't local by its type, also a pointer
  // parameter its function points into the data; not a local, whose values
  // objectRelation follows (localValuesAt); not a parameter of a type of the
  // pattern, while those aren't.
  case EStatement::Name: {
    const ValueDecl *Named = cast<DeclRefExpr>(E)->getDecl();
    switch (holderKind(Named)) {
    case EHolder::Parameter: {
      const auto *Parameter = cast<ParmVarDecl>(Named);
      if (bPatternExcluded && dependentInPattern(Parameter))
        return false;
      if (Typed(Parameter->getType()))
        return true;
      // A pointer parameter by what its function assigns it where it is
      // (assignedAt), not what it is given.
      // Not while it is told, which is assumed of it.
      if (!Parameter->getType()->isPointerType() ||
          !DecidingParameters.insert(E).second)
        return false;
      const bool Points = llvm::any_of(
          assignedAt(Parameter, E, Context), [&](const CSlotValue &Value) {
            return Value.Kind == CSlotValue::EKind::Value
                       ? pointsTo(Value.Value)
                       : isDataAt(Value.Value,
                                  Value.Kind == CSlotValue::EKind::Address ? 0
                                                                           : 1);
          });
      DecidingParameters.erase(E);
      return Points;
    }
    case EHolder::Variable:
    case EHolder::Decomposition: {
      const auto *Variable = cast<VarDecl>(Named);
      return !(Variable->hasLocalStorage() || Variable->isInitCapture()) &&
             Typed(Variable->getType());
    }
    case EHolder::None:
    case EHolder::ImplicitParameter:
    case EHolder::Binding:
    case EHolder::Field:
    case EHolder::IndirectField:
    case EHolder::Temporary:
    case EHolder::Constant:
    case EHolder::Function:
    case EHolder::Statement:
    case EHolder::Opaque:
      return false;
    }
    return false;
  }
  // A member of a reference or a pointer to a thread-local type, not of what
  // a call that names the thread gives, `fg_SystemThreadLocal(_ThreadID)`;
  // not one of a type of the pattern of the object, while those aren't; not
  // a member of a local that holds what its function tells
  // (toldLocalMember).
  case EStatement::Member: {
    const ValueDecl *Member = memberDecl(E, Context);
    // A static data member, like a variable that isn't local.
    if (holderKind(Member) == EHolder::Variable)
      return Typed(Member->getType());
    if (holderKind(Member) != EHolder::Field || !Typed(Member->getType()))
      return false;
    const Expr *Base = cast<MemberExpr>(E)->getBase();
    const bool OfThis = effectsOf(sameValue(Base, Context), Context).Statement ==
                        EStatement::This;
    return !(bPatternExcluded && OfThis &&
             dependentInPattern(cast<FieldDecl>(Member))) &&
           !OfExplicitThread(Base) &&
           !toldLocalMember(cast<MemberExpr>(E), Context);
  }
  case EStatement::Other:
  case EStatement::Construction:
  case EStatement::Lambda:
  case EStatement::Declaration:
  case EStatement::RangeLoop:
  case EStatement::CoroutineBody:
  case EStatement::Temporary:
  case EStatement::New:
  case EStatement::Return:
  case EStatement::Coreturn:
  case EStatement::Default:
  case EStatement::InheritedConstruction:
  case EStatement::InitializerList:
    return false;
  }
  return false;
}

/// The local pointer, or array of them, the location \p Target designates
/// as a whole or an element (designatedSlots), of only one, or null.
static const VarDecl *boundSlot(const Expr *Target, ASTContext &Context) {
  bool Told = true;
  const llvm::SmallVector<CSlot, 2> Slots =
      Target ? designatedSlots(Target, Context, &Told)
             : llvm::SmallVector<CSlot, 2>();
  return Told && Slots.size() == 1 && Slots.front().Path.empty()
             ? Slots.front().Variable
             : nullptr;
}

CThreadLocalData::CCaptured
CThreadLocalData::capturedBy(const LambdaCapture &Capture,
                             const Expr *Init) const {
  if (stackNearlyExhausted())
    return withStack([&] { return capturedBy(Capture, Init); });
  CCaptured Of;
  if (!Capture.capturesVariable())
    return Of;
  const ValueDecl *Captured = Capture.getCapturedVar();
  Of.Binding = dyn_cast<BindingDecl>(Captured);
  Of.Variable = dyn_cast_or_null<VarDecl>(
      Of.Binding ? Of.Binding->getDecomposedDecl() : Captured);
  if (!Of.Variable)
    return Of;
  Of.Type = Of.Binding ? Of.Binding->getType() : Of.Variable->getType();
  Of.Copied = Capture.getCaptureKind() == LCK_ByCopy;
  const bool InitCapture = !Of.Binding && Of.Variable->isInitCapture();
  // A copy of a reference is a copy of what it refers to, of its type.
  if (Of.Copied && !InitCapture)
    Of.Type = Of.Type.getNonReferenceType();
  QualType Element = Of.Type.getNonReferenceType();
  while (const ArrayType *Array = Element->getAsArrayTypeUnsafe())
    Element = Array->getElementType();
  Of.Refers = InitCapture ? Of.Type->isReferenceType() || Element->isPointerType()
                          : !Of.Copied || Element->isPointerType();
  // A reference by what it is bound to, a local's storage, also of a copy,
  // which copies what that holds.
  const VarDecl *Bound =
      !Of.Binding && Of.Variable->getType()->isReferenceType()
          ? boundLocal(Of.Variable)
          : nullptr;
  if (Bound == Of.Variable)
    Bound = nullptr;
  Of.Slot = Bound && (!Of.Copied || InitCapture) ? Bound : Of.Variable;
  if (!Of.Refers)
    return Of;
  // By reference of what the lambda it is made in copies, that copy, which
  // that closure keeps, `[Stats] { [&Stats] { ... }; }`: as that capture
  // holds (capturedBy), a copied pointer what it points to.
  if (!Of.Copied && Init) {
    DynTypedNode Node = DynTypedNode::create(*Init);
    const Stmt *From = Init;
    for (int Step = 0; Step < 1024; ++Step) {
      const auto Parents = Context.getParents(Node);
      if (Parents.empty() || Parents[0].get<FunctionDecl>())
        break;
      const LambdaExpr *Around = nullptr;
      for (const DynTypedNode &Parent : Parents)
        if (const auto *Lambda = Parent.get<LambdaExpr>();
            Lambda && From && From == Lambda->getBody())
          Around = Lambda;
      if (Around) {
        for (auto [Outer, OuterInit] :
             llvm::zip(Around->captures(), captureInitsOf(Around)))
          if (Outer.capturesVariable() &&
              Outer.getCapturedVar() == Captured &&
              Outer.getCaptureKind() == LCK_ByCopy) {
            const CCaptured Copy = capturedBy(Outer, OuterInit);
            Of.Holds = Copy.Refers && Copy.Holds;
            return Of;
          }
        break;
      }
      From = Parents[0].get<Stmt>();
      Node = Parents[0];
    }
  }
  // What a local slot it is captured from, by reference or an array copied,
  // holds where the closure is made and, by reference, what it is given
  // after (localSlotValuesFrom), an array copied by what it holds there
  // (localSlotValues); else by what it holds anywhere (isVariable).
  auto Holds = [&](const VarDecl *Variable) {
    if (!Variable || !Init || isa<ParmVarDecl>(Variable) ||
        Variable->getType()->isReferenceType() ||
        !Variable->hasLocalStorage())
      return isVariable(Variable);
    const CSlotValues Values =
        Of.Copied ? localSlotValues({Variable, {}, false}, Init, Context)
                  : localSlotValuesFrom({Variable, {}, false}, Init, Context);
    if (!Values.Told)
      return isVariable(Variable);
    return llvm::any_of(Values.Values, [&](const CSlotValue &Value) {
      switch (Value.Kind) {
      case CSlotValue::EKind::Value:
        return pointsTo(Value.Value);
      case CSlotValue::EKind::Stored:
        return isDataAt(Value.Value, 1);
      case CSlotValue::EKind::Address:
        return isDataAt(Value.Value, 0);
      case CSlotValue::EKind::Given:
        return isVariable(Variable);
      }
      return true;
    });
  };
  if (Of.Binding) {
    // A structured binding, by what it binds, read where the closure is
    // made, by what it is made with (reach), else where it is declared.
    const Expr *Bound = Init ? Init : Of.Binding->getBinding();
    Of.Holds = Bound && (Of.Copied ? Of.Type->isPointerType() &&
                                         (pointsTo(Bound) ||
                                          isVariable(boundSlot(Bound, Context)))
                                   : isData(Bound) || pointsTo(Bound));
  } else if (InitCapture)
    // Of a reference to a local, as a capture of that local by reference
    // (Holds), `[&pCurrent = pText]` as `[&pText]`.
    Of.Holds = Bound && !Of.Copied ? Holds(Bound)
                                   : Init && refersTo(Init, Of.Type);
  // By reference by what it holds where the closure is made and what it is
  // given after (Holds), an array copied by what it holds there. A pointer
  // copied by what it is given.
  else if (!Of.Copied || Of.Type.getNonReferenceType()->isArrayType())
    Of.Holds = Holds(Of.Variable) || (Bound && Holds(Bound));
  else
    Of.Holds = Init && refersTo(Init, Element);
  return Of;
}

bool CThreadLocalData::copiedIn(const DeclContext *Function) const {
  if (!bObjectCopied || !ObjectCopiedIn)
    return bObjectCopied;
  for (int Depth = 0; Function && Depth < 16; ++Depth) {
    if (Function == ObjectCopiedIn)
      return true;
    const auto *Operator = dyn_cast<CXXMethodDecl>(Function);
    const CXXRecordDecl *Closure = Operator ? Operator->getParent() : nullptr;
    if (!Closure || !Closure->isLambda())
      return false;
    Function = Closure->getDeclContext();
  }
  return false;
}

bool CThreadLocalData::thisCopied(const Expr *This) const {
  if (!bObjectCopied || !ObjectCopiedIn)
    return bObjectCopied;
  DynTypedNode Node = DynTypedNode::create(*This);
  const Stmt *From = This;
  for (int Step = 0; Step < 1024; ++Step) {
    const auto Parents = Context.getParents(Node);
    if (Parents.empty())
      return false;
    for (const DynTypedNode &Parent : Parents)
      if (const auto *Lambda = Parent.get<LambdaExpr>();
          Lambda && From && From == Lambda->getBody())
        return copiedIn(Lambda->getCallOperator());
    if (const auto *Function = Parents[0].get<FunctionDecl>())
      return copiedIn(Function);
    From = Parents[0].get<Stmt>();
    Node = Parents[0];
  }
  return false;
}

bool CThreadLocalData::relatedToObject(const Expr *E) const {
  const auto *Call = dyn_cast<CallExpr>(E);
  const auto *Method =
      Call ? dyn_cast_or_null<CXXMethodDecl>(callTarget(E, Context)) : nullptr;
  if (!Method || Method->isStatic() || !callObject(E, Context) ||
      mayRunOverride(Call, Method))
    return false;
  const std::optional<CObjectRelation> Returned =
      returnedRelation(Method, &Context);
  if (!Returned || Returned->bThroughPointer || Returned->EntryDepth > 0)
    return false;
  switch (Returned->Kind) {
  // Not a value made of it, a copy, which is its own.
  case EObjectRelation::SameObject:
  case EObjectRelation::Subobject:
  case EObjectRelation::Entry:
    return !Returned->bCopy || E->isGLValue() ||
           E->getType()->isAnyPointerType();
  case EObjectRelation::Unrelated:
  case EObjectRelation::Unknown:
    return false;
  }
  return false;
}

bool CThreadLocalData::givesData(const FunctionDecl *Function) const {
  const FunctionDecl *Definition = nullptr;
  if (!Function || !Function->hasBody(Definition) || !Definition)
    return false;
  const QualType Returned = Definition->getReturnType();
  if (!Returned->isReferenceType() && !Returned->isPointerType())
    return false;
  return threadLocalFacts(Context, Lists, listsKey()).Gives.get(Definition);
}

bool CThreadLocalData::givesDataUncached(const FunctionDecl *Definition) const {
  // Of its own, whatever it is called on: not what is of its object, which
  // what it is called on tells (returnedRelation), `Copy.f_Member()`. Of its
  // body as it is, not of the pattern of what calls it, nor of what is told
  // of the variables where it is asked, which is of that.
  const QualType Returned = Definition->getReturnType();
  setObjectCopied(true, Definition);
  return llvm::any_of(
      writtenReturns(Definition, Context), [&](const Expr *Value) {
        return Value &&
               (Returned->isReferenceType() ? isData(Value) : pointsTo(Value));
      });
}

bool CThreadLocalData::isStorage(const Expr *Target) const {
  // The storage itself is thread-local data, a part or an element of it, of
  // the same thread, of each alternative; not what a pointer it holds points
  // to (reach).
  if (!Target)
    return false;
  bool Found = false;
  bool Ended = false;
  reach(
      Target, 0,
      [&](const Expr *At, unsigned Level, bool) {
        const EReach Reach = rootAt(At, Level);
        Found |= Reach == EReach::Root;
        return Reach;
      },
      &Ended);
  return Found && !Ended;
}

bool CThreadLocalData::isData(const Expr *E) const { return isDataAt(E, 0); }

bool CThreadLocalData::isDataAt(const Expr *E, unsigned Level) const {
  if (!E)
    return false;
  // Whether a way reach visits is a root, by the records of the shared walks:
  // what each way's Visit tells depends only on where it is and its level.
  if (!bDataWalking && DecidingHelpers.empty() && DecidingParameters.empty()) {
    const std::tuple<bool, bool, const DeclContext *> Settings{
        bPatternExcluded, bObjectCopied, ObjectCopiedIn};
    if (!DataWalks || DataWalksSettings != Settings) {
      DataWalks = std::make_unique<CReachClosures>(Provenance);
      DataWalksSettings = Settings;
    }
    bDataWalking = true;
    const llvm::SmallVectorImpl<unsigned> *Roots = DataWalks->closure(
        E, Level,
        [&](const Expr *At, unsigned AtLevel,
            llvm::SmallVectorImpl<unsigned> &Records) {
          const EReach Reach = rootAt(At, AtLevel);
          if (Reach == EReach::Root)
            Records.push_back(0);
          return Reach;
        },
        [](const Expr *) {});
    bDataWalking = false;
    if (Roots)
      return !Roots->empty();
  }
  bool Found = false;
  reach(E, Level, [&](const Expr *At, unsigned AtLevel, bool) {
    const EReach Reach = rootAt(At, AtLevel);
    Found |= Reach == EReach::Root;
    return Found ? EReach::Root : Reach;
  });
  return Found;
}

bool CThreadLocalData::pointsTo(const Expr *E) const {
  return isData(E);
}

const VarDecl *CThreadLocalData::boundLocal(const VarDecl *Reference) const {
  if (!Reference)
    return nullptr;
  // A local, also a parameter held by value.
  if (!Reference->getType()->isReferenceType())
    return Reference->hasLocalStorage() ? Reference : nullptr;
  if (!Reference->hasLocalStorage() || isa<ParmVarDecl>(Reference) ||
      !Reference->getInit())
    return nullptr;
  // What it is bound to (reach): the storage of a local that isn't a
  // reference, the whole of it, also through other local references.
  // Of each alternative, `bLocal ? Local : t_Value` isn't.
  const VarDecl *Found = nullptr;
  bool Other = false;
  reach(
      Reference->getInit(), 0,
      [&](const Expr *At, unsigned Level, bool Part) {
        const auto *Local =
            effectsOf(At, Context).Statement == EStatement::Name
                ? dyn_cast<VarDecl>(cast<DeclRefExpr>(At)->getDecl())
                : nullptr;
        if (!Local || !Local->hasLocalStorage() ||
            Local->getType()->isReferenceType())
          return EReach::Continue;
        if (Level == 0 && !Part && (!Found || Found == Local))
          Found = Local;
        else
          Other = true;
        return EReach::Root;
      },
      &Other);
  return Other ? nullptr : Found;
}

bool CThreadLocalData::refersTo(const Expr *E, QualType Bound) const {
  if (!E || Bound.isNull())
    return false;
  // A reference to a pointer, by the pointer slot itself when it is the data,
  // `int *&rpValue = t_Data.m_pValue` (isStorage), or by the pointer it
  // gives, `int *const &_pValue`.
  // A reference to a pointer, by the pointer slot it refers to when that is
  // the data, `int *&rpValue = t_Data.m_pValue`, or, of a temporary it
  // extends, by the pointer it is given, `int *const &_pValue = &t_Value`.
  // Also through a local reference to one, `auto &rpAlias = pValue;` of
  // `auto *const &pValue = &t_Value`.
  auto Extended = [&](const Expr *Of) {
    for (int Depth = 0; Of && Depth < 8; ++Depth) {
      Of = sameValue(Of, Context);
      if (effectsOf(Of, Context).Statement == EStatement::Temporary)
        return true;
      const auto *Ref = dyn_cast<DeclRefExpr>(Of);
      const auto *Local = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
      if (!Local || !Local->getType()->isReferenceType() ||
          !Local->hasLocalStorage() || isa<ParmVarDecl>(Local))
        return false;
      Of = Local->getInit();
    }
    return false;
  };
  if (Bound->isReferenceType() && Bound.getNonReferenceType()->isPointerType())
    return E->isGLValue() ? isData(E) || (Extended(E) && isDataAt(E, 1))
                          : pointsTo(E);
  if (Bound->isReferenceType())
    return isData(E);
  if (Bound->isPointerType())
    return pointsTo(E);
  // Of an array of pointers, what any of its elements, also of an array of
  // them, stores (reach).
  if (Bound->isArrayType())
    return isDataAt(E, 1);
  return false;
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
  // A parameter of a reference or a pointer to a thread-local type, also a
  // reference to an array of it, `CData (&_Data)[2]`, or a variable that
  // isn't local.
  QualType Referred = Type.getNonReferenceType();
  while (const ArrayType *Array = Referred->getAsArrayTypeUnsafe())
    Referred = Array->getElementType();
  if (!Local && (isType(Type, true) ||
                 (Type->isReferenceType() &&
                  Type.getNonReferenceType()->isArrayType() &&
                  isType(Referred))))
    return true;
  if (!Local && !(Parameter && Type->isPointerType()))
    return false;
  return threadLocalFacts(Context, Lists, listsKey())
      .Variables.get({Variable,
                      (bPatternExcluded ? 1u : 0u) | (bObjectCopied ? 2u : 0u),
                      bObjectCopied ? ObjectCopiedIn : nullptr});
}

bool CThreadLocalData::isVariableUncached(const VarDecl *Variable) const {
  const QualType Type = Variable->getType();
  const bool Local =
      (Variable->hasLocalStorage() || Variable->isInitCapture()) &&
      !isa<ParmVarDecl>(Variable);
  bool Refers = false;
  if (const Expr *Init = Local ? Variable->getInit() : nullptr) {
    Refers = refersTo(Init, Type);
  }
  // A local reference to a pointer, also an element, by the pointer it
  // refers to, `int *&rpValue = pValues[0]`.
  if (Local && !Refers && Type->isReferenceType() &&
      Type.getNonReferenceType()->isPointerType())
    if (const Expr *Init = Variable->getInit())
      Refers = isDataAt(Init, 1);
  // Of a pointer or an array of them, also what its function assigns it,
  // also of a parameter held by value.
  if (!Refers && (!Type->isReferenceType() || extendedSlot(Variable)))
    Refers = llvm::any_of(
        localSlotAssignments({Variable, {}, false}, Context).Values,
        [&](const CSlotValue &Value) {
          return Value.Kind == CSlotValue::EKind::Value
                     ? pointsTo(Value.Value)
                     : isDataAt(Value.Value,
                                Value.Kind == CSlotValue::EKind::Address ? 0
                                                                         : 1);
        });
  return Refers;
}

} // namespace clang::tidy::malterlib
