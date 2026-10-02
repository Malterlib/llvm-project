//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HeldData.h"
#include "CoroutineUtils.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/ADT/SmallPtrSet.h"

// Every switch over a kind of clang's lists all of them, no default, so that
// one added there is an error here until it is classified.
#pragma clang diagnostic error "-Wswitch"

namespace clang::tidy::malterlib {

EHolder holderKind(const Decl *D) {
  switch (D->getKind()) {
  case Decl::Var:
  case Decl::VarTemplateSpecialization:
  case Decl::VarTemplatePartialSpecialization:
    return EHolder::Variable;
  case Decl::ParmVar:
    return EHolder::Parameter;
  case Decl::ImplicitParam:
    return EHolder::ImplicitParameter;
  case Decl::Decomposition:
    return EHolder::Decomposition;
  case Decl::Binding:
    return EHolder::Binding;
  case Decl::Field:
    return EHolder::Field;
  case Decl::IndirectField:
    return EHolder::IndirectField;
  case Decl::LifetimeExtendedTemporary:
    return EHolder::Temporary;
  case Decl::TemplateParamObject:
  case Decl::UnnamedGlobalConstant:
  case Decl::MSGuid:
    return EHolder::Constant;
  case Decl::Function:
  case Decl::CXXMethod:
  case Decl::CXXDestructor:
  case Decl::CXXConversion:
  case Decl::CXXConstructor:
  case Decl::CXXDeductionGuide:
  case Decl::Block:
  case Decl::Captured:
  case Decl::OutlinedFunction:
    return EHolder::Function;
  case Decl::CXXExpansionStmt:
    return EHolder::Statement;
  case Decl::OMPCapturedExpr:
  case Decl::ObjCIvar:
  case Decl::ObjCAtDefsField:
  case Decl::ObjCMethod:
  case Decl::ObjCProperty:
  case Decl::ObjCPropertyImpl:
  case Decl::MSProperty:
  case Decl::HLSLBuffer:
  case Decl::OMPDeclareReduction:
  case Decl::OMPDeclareMapper:
    return EHolder::Opaque;
  case Decl::TranslationUnit:
  case Decl::TopLevelStmt:
  case Decl::RequiresExprBody:
  case Decl::LinkageSpec:
  case Decl::ExternCContext:
  case Decl::Export:
  case Decl::StaticAssert:
  case Decl::PragmaDetectMismatch:
  case Decl::PragmaComment:
  case Decl::OpenACCRoutine:
  case Decl::OpenACCDeclare:
  case Decl::OMPThreadPrivate:
  case Decl::OMPRequires:
  case Decl::OMPGroupPrivate:
  case Decl::OMPAllocate:
  case Decl::ObjCProtocol:
  case Decl::ObjCInterface:
  case Decl::ObjCImplementation:
  case Decl::ObjCCategoryImpl:
  case Decl::ObjCCategory:
  case Decl::UnresolvedUsingValue:
  case Decl::EnumConstant:
  case Decl::NonTypeTemplateParm:
  case Decl::UsingShadow:
  case Decl::ConstructorUsingShadow:
  case Decl::UsingPack:
  case Decl::UsingDirective:
  case Decl::UnresolvedUsingIfExists:
  case Decl::Record:
  case Decl::CXXRecord:
  case Decl::ClassTemplateSpecialization:
  case Decl::ClassTemplatePartialSpecialization:
  case Decl::Enum:
  case Decl::UnresolvedUsingTypename:
  case Decl::Typedef:
  case Decl::TypeAlias:
  case Decl::ObjCTypeParam:
  case Decl::TemplateTypeParm:
  case Decl::TemplateTemplateParm:
  case Decl::VarTemplate:
  case Decl::TypeAliasTemplate:
  case Decl::FunctionTemplate:
  case Decl::ClassTemplate:
  case Decl::Concept:
  case Decl::BuiltinTemplate:
  case Decl::ObjCCompatibleAlias:
  case Decl::Namespace:
  case Decl::NamespaceAlias:
  case Decl::Label:
  case Decl::HLSLRootSignature:
  case Decl::UsingEnum:
  case Decl::Using:
  case Decl::Import:
  case Decl::ImplicitConceptSpecialization:
  case Decl::FriendTemplate:
  case Decl::Friend:
  case Decl::FileScopeAsm:
  case Decl::ExplicitInstantiation:
  case Decl::Empty:
  case Decl::AccessSpec:
    return EHolder::None;
  }
  return EHolder::Opaque;
}

/// Of what is automatic in \p Function: the frame of a coroutine, which
/// lives across its suspensions, or the scope.
static CLifetime automaticIn(const FunctionDecl *Function) {
  return {Function && isCoroutine(Function) ? ELifetime::Frame
                                            : ELifetime::Automatic,
          Function};
}

/// Of what has \p Duration, automatic in \p Function.
static CLifetime durationLifetime(StorageDuration Duration,
                                  const FunctionDecl *Function) {
  switch (Duration) {
  case SD_FullExpression:
    return {ELifetime::FullExpression, Function};
  case SD_Automatic:
    return automaticIn(Function);
  case SD_Thread:
    return {ELifetime::Thread};
  case SD_Static:
    return {ELifetime::Static};
  case SD_Dynamic:
    return {ELifetime::Dynamic};
  }
  return {};
}

CLifetime variableLifetime(const VarDecl *Variable) {
  if (!Variable)
    return {};
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  // An init capture is a member of the closure.
  if (Variable->isInitCapture())
    return {ELifetime::Closure, Function};
  return durationLifetime(Variable->getStorageDuration(), Function);
}

CLifetime lifetimeOf(const CDestination &Into) {
  switch (Into.Kind) {
  case EDestination::None:
    return {};
  case EDestination::Variable:
    return variableLifetime(dyn_cast_or_null<VarDecl>(Into.Declaration));
  case EDestination::Parameter:
    return {ELifetime::Parameter, Into.Callee};
  case EDestination::Field:
  case EDestination::Designated:
  case EDestination::Capture:
  case EDestination::Element:
  case EDestination::Base:
    return {ELifetime::ObjectOf, nullptr, Into.Object};
  case EDestination::Return:
    return {ELifetime::Return};
  case EDestination::Heap:
  case EDestination::Thrown:
    return {ELifetime::Dynamic};
  case EDestination::Temporary:
    // A temporary a reference can extend; the array of a
    // `std::initializer_list` lives as long as it; an awaiter, a compound
    // literal and the rest until the end of the full expression.
    if (const auto *Temporary =
            dyn_cast_or_null<MaterializeTemporaryExpr>(Into.Object)) {
      const auto *Extending =
          dyn_cast_or_null<VarDecl>(Temporary->getExtendingDecl());
      return durationLifetime(
          Temporary->getStorageDuration(),
          Extending ? dyn_cast_or_null<FunctionDecl>(
                          Extending->getParentFunctionOrMethod())
                    : nullptr);
    }
    if (isa_and_nonnull<CXXStdInitializerListExpr>(Into.Object))
      return {ELifetime::ObjectOf, nullptr, Into.Object};
    return {ELifetime::FullExpression};
  }
  return {};
}

llvm::StringRef lifetimeName(ELifetime Kind) {
  switch (Kind) {
  case ELifetime::FullExpression:
    return "FullExpression";
  case ELifetime::Automatic:
    return "Automatic";
  case ELifetime::Frame:
    return "Frame";
  case ELifetime::Thread:
    return "Thread";
  case ELifetime::Static:
    return "Static";
  case ELifetime::Dynamic:
    return "Dynamic";
  case ELifetime::ObjectOf:
    return "ObjectOf";
  case ELifetime::Closure:
    return "Closure";
  case ELifetime::Parameter:
    return "Parameter";
  case ELifetime::Return:
    return "Return";
  case ELifetime::Unknown:
    return "Unknown";
  }
  return "";
}

llvm::StringRef holderName(EHolder Kind) {
  switch (Kind) {
  case EHolder::None:
    return "None";
  case EHolder::Variable:
    return "Variable";
  case EHolder::Parameter:
    return "Parameter";
  case EHolder::ImplicitParameter:
    return "ImplicitParameter";
  case EHolder::Decomposition:
    return "Decomposition";
  case EHolder::Binding:
    return "Binding";
  case EHolder::Field:
    return "Field";
  case EHolder::IndirectField:
    return "IndirectField";
  case EHolder::Temporary:
    return "Temporary";
  case EHolder::Constant:
    return "Constant";
  case EHolder::Function:
    return "Function";
  case EHolder::Statement:
    return "Statement";
  case EHolder::Opaque:
    return "Opaque";
  }
  return "";
}

llvm::StringRef heldName(EHeld Kind) {
  switch (Kind) {
  case EHeld::Pointer:
    return "Pointer";
  case EHeld::Reference:
    return "Reference";
  case EHeld::MemberPointer:
    return "MemberPointer";
  case EHeld::BlockPointer:
    return "BlockPointer";
  case EHeld::Record:
    return "Record";
  case EHeld::Dependent:
    return "Dependent";
  case EHeld::Opaque:
    return "Opaque";
  }
  return "";
}

namespace {

/// The walk of forEachHeld: an overload for each concrete class of type,
/// which the dispatch generated from clang's list of them calls; the
/// template is deleted, so that a class without its own overload doesn't
/// compile.
class CHeldWalk {
public:
  CHeldWalk(llvm::function_ref<EVisit(const CHeld &)> Visit) : Visit(Visit) {}

  bool walk(QualType Of) {
    if (stackNearlyExhausted())
      return withStack([&] { return walk(Of); });
    if (Of.isNull() || bFound)
      return bFound;
    const clang::Type *Bare = Of.getTypePtr();
    Current = Of;
    switch (Bare->getTypeClass()) {
#define ABSTRACT_TYPE(Class, Base)
#define TYPE(Class, Base)                                                      \
  case Type::Class:                                                            \
    of(cast<Class##Type>(Bare));                                               \
    break;
#include "clang/AST/TypeNodes.inc"
    }
    return bFound;
  }

  template <typename t_CType> void of(const t_CType *) = delete;

  // Sugar, by what it stands for; what is dependent, as that.
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base)                                        \
  void of(const Class##Type *T) { into(T->desugar()); }
#define DEPENDENT_TYPE(Class, Base)                                            \
  void of(const Class##Type *) { visit({EHeld::Dependent, Current}); }
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base)                       \
  void of(const Class##Type *T) {                                              \
    if (T->isSugared())                                                        \
      into(T->desugar());                                                      \
    else                                                                       \
      visit({EHeld::Dependent, Current});                                      \
  }
#include "clang/AST/TypeNodes.inc"

  // What holds nothing that leads elsewhere.
  void of(const BuiltinType *) {}
  void of(const BitIntType *) {}
  void of(const EnumType *) {}
  void of(const FunctionNoProtoType *) {}
  void of(const FunctionProtoType *) {}

  // What holds its elements in it.
  void of(const ConstantArrayType *T) { into(T->getElementType()); }
  void of(const ArrayParameterType *T) { into(T->getElementType()); }
  void of(const IncompleteArrayType *T) { into(T->getElementType()); }
  void of(const VariableArrayType *T) { into(T->getElementType()); }
  void of(const AtomicType *T) { into(T->getValueType()); }
  void of(const ComplexType *T) { into(T->getElementType()); }
  void of(const VectorType *T) { into(T->getElementType()); }
  void of(const ExtVectorType *T) { into(T->getElementType()); }
  void of(const ConstantMatrixType *T) { into(T->getElementType()); }
  void of(const OverflowBehaviorType *T) { into(T->getUnderlyingType()); }

  // A placeholder for a type, by what it is deduced to.
  void of(const AutoType *T) { deduced(T); }
  void of(const DeducedTemplateSpecializationType *T) { deduced(T); }

  void of(const PointerType *T) {
    leads(EHeld::Pointer, T->getPointeeType());
  }
  void of(const LValueReferenceType *T) {
    leads(EHeld::Reference, T->getPointeeType());
  }
  void of(const RValueReferenceType *T) {
    leads(EHeld::Reference, T->getPointeeType());
  }
  void of(const MemberPointerType *T) {
    leads(EHeld::MemberPointer, T->getPointeeType());
  }
  void of(const BlockPointerType *T) {
    leads(EHeld::BlockPointer, T->getPointeeType());
  }

  void of(const RecordType *T);

  // What isn't C++.
  void of(const ObjCObjectPointerType *) { visit({EHeld::Opaque, Current}); }
  void of(const ObjCObjectType *) { visit({EHeld::Opaque, Current}); }
  void of(const ObjCInterfaceType *) { visit({EHeld::Opaque, Current}); }
  void of(const PipeType *) { visit({EHeld::Opaque, Current}); }
  void of(const HLSLAttributedResourceType *) {
    visit({EHeld::Opaque, Current});
  }
  void of(const HLSLInlineSpirvType *) { visit({EHeld::Opaque, Current}); }

private:
  void into(QualType Type) { walk(Type); }
  EVisit visit(const CHeld &Held) {
    const EVisit Result = Visit(Held);
    if (Result == EVisit::Found)
      bFound = true;
    return Result;
  }
  void leads(EHeld Kind, QualType Pointee) {
    if (visit({Kind, Current, Pointee, nullptr, heldElement(Pointee)}) ==
        EVisit::Descend)
      into(Pointee);
  }
  template <typename t_CDeduced> void deduced(const t_CDeduced *T) {
    if (T->isDeduced() && !T->getDeducedType().isNull())
      into(T->getDeducedType());
    else
      visit({EHeld::Dependent, Current});
  }

  llvm::function_ref<EVisit(const CHeld &)> Visit;
  llvm::SmallPtrSet<const Decl *, 8> Seen;
  QualType Current;
  bool bFound = false;
};

void CHeldWalk::of(const RecordType *T) {
  const RecordDecl *Record = T->getDecl();
  const QualType Type = Current;
  if (visit({EHeld::Record, Type, QualType(), Record}) != EVisit::Descend ||
      !Seen.insert(Record->getCanonicalDecl()).second)
    return;
  // The type arguments of the template \p Of is a specialization of, also
  // of a pack.
  auto Arguments = [&](const RecordDecl *Of) {
    const auto *Specialization = dyn_cast<ClassTemplateSpecializationDecl>(Of);
    if (!Specialization)
      return;
    TCRecursive<void(const TemplateArgument &)> Argument =
        [&](const TemplateArgument &Each) {
          switch (Each.getKind()) {
          case TemplateArgument::Type:
            into(Each.getAsType());
            break;
          case TemplateArgument::Pack:
            for (const TemplateArgument &Element : Each.pack_elements())
              Argument(Element);
            break;
          case TemplateArgument::Null:
          case TemplateArgument::Declaration:
          case TemplateArgument::NullPtr:
          case TemplateArgument::Integral:
          case TemplateArgument::StructuralValue:
          case TemplateArgument::Template:
          case TemplateArgument::TemplateExpansion:
          case TemplateArgument::Expression:
            break;
          }
        };
    for (const TemplateArgument &Each :
         Specialization->getTemplateArgs().asArray())
      Argument(Each);
  };
  const RecordDecl *Definition = Record->getDefinition();
  // What isn't defined, by the arguments of its template.
  if (!Definition) {
    Arguments(Record);
    return;
  }
  if (const auto *Class = dyn_cast<CXXRecordDecl>(Definition))
    for (const CXXBaseSpecifier &Base : Class->bases())
      into(Base.getType());
  bool Bytes = false;
  for (const FieldDecl *Field : Definition->fields()) {
    into(Field->getType());
    // Storage of bytes, an array of them, which can hold what isn't told by
    // its type, `alignas(t_CValue) unsigned char m_Data[sizeof(t_CValue)]`.
    QualType Element = Field->getType();
    while (const ArrayType *Array = Element->getAsArrayTypeUnsafe())
      Element = Array->getElementType();
    Bytes |= Field->getType()->isArrayType() &&
             (Element->isCharType() || Element->isStdByteType());
  }
  // What bytes of it can hold: the arguments of its template and of the
  // templates it is in, `TCVariantCommon<t_CValue>::CStorageType`.
  if (Bytes)
    for (const DeclContext *Around = Definition; Around;
         Around = Around->getParent())
      if (const auto *Enclosing = dyn_cast<RecordDecl>(Around))
        Arguments(Enclosing);
}

} // namespace

QualType heldElement(QualType Type) {
  if (Type.isNull())
    return Type;
  Type = Type.getCanonicalType().getUnqualifiedType();
  while (const ArrayType *Array = Type->getAsArrayTypeUnsafe())
    Type = Array->getElementType().getCanonicalType().getUnqualifiedType();
  return Type;
}

bool forEachHeld(QualType Type,
                 llvm::function_ref<EVisit(const CHeld &)> Visit) {
  CHeldWalk Walk(Visit);
  return Walk.walk(Type);
}

bool holdsReference(QualType Type) {
  return forEachHeld(Type, [](const CHeld &Part) {
    switch (Part.Kind) {
    case EHeld::Pointer:
    case EHeld::Reference:
    case EHeld::BlockPointer:
    case EHeld::Opaque:
      return EVisit::Found;
    case EHeld::Record:
      return EVisit::Descend;
    case EHeld::MemberPointer:
    case EHeld::Dependent:
      return EVisit::Skip;
    }
    return EVisit::Skip;
  });
}

} // namespace clang::tidy::malterlib
