//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "StorageModel.h"
#include "CheckedAccess.h"
#include "ActorAnnotations.h"
#include "CoroutineUtils.h"
#include "ExprEffects.h"
#include "HeldData.h"
#include "MalterlibCheck.h"
#include "MalterlibOptions.h"
#include "NamingUtils.h"
#include "OncePerTypeVisitor.h"
#include "StackSegments.h"
#include "ValueFlow.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ParentMapContext.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopeExit.h"
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <tuple>

namespace clang::tidy::malterlib {

EOwnerKind ownerKindOf(QualType Type, const ASTContext &Context) {
  if (Type.isNull())
    return EOwnerKind::None;
  // By the name of the canonical type: an alias is never looked up.
  Type = Type.getNonReferenceType().getCanonicalType().getUnqualifiedType();
  if (!Type->getAsCXXRecordDecl())
    return EOwnerKind::None;
  const CVocabulary &Vocabulary = vocabulary();
  if (matchesTypeName(Type, Vocabulary.SharedOwnerTypes.names(), Context))
    return EOwnerKind::Shared;
  if (matchesTypeName(Type, Vocabulary.WeakOwnerTypes.names(), Context))
    return EOwnerKind::Weak;
  if (matchesTypeName(Type, Vocabulary.UniqueOwnerTypes.names(), Context))
    return EOwnerKind::Unique;
  if (isBufferOwningType(Type, Context))
    return matchesTypeName(Type, Vocabulary.SharedBufferOwnerTypes.names(),
                           Context)
               ? EOwnerKind::SharedBuffer
               : EOwnerKind::Buffer;
  return EOwnerKind::None;
}

bool copyKeepsOwned(EOwnerKind Kind) {
  switch (Kind) {
  case EOwnerKind::Shared:
  case EOwnerKind::SharedBuffer:
    return true;
  case EOwnerKind::None:
  case EOwnerKind::Weak:
  case EOwnerKind::Unique:
  case EOwnerKind::Buffer:
    return false;
  }
  return false;
}

bool ownsBuffer(EOwnerKind Kind) {
  switch (Kind) {
  case EOwnerKind::Buffer:
  case EOwnerKind::SharedBuffer:
    return true;
  case EOwnerKind::None:
  case EOwnerKind::Weak:
  case EOwnerKind::Shared:
  case EOwnerKind::Unique:
    return false;
  }
  return false;
}

static bool holdsOwnerElements(QualType Type, const ASTContext &Context);

namespace {
/// How a value holds owners that keep what they own, by value (holdingOf).
enum class EHolding {
  /// It holds none.
  None,
  /// It is one, of a shared, unique, buffer or sharedbuffer kind.
  Owner,
  /// A record that holds them in members or bases by value, `CContext`, a
  /// `std::tuple` in its leaf bases.
  Record,
  /// An array whose elements hold them.
  Array,
  /// A container whose elements, of an argument of its template, are owners
  /// or hold them, kept where its members don't hold them by value
  /// (holdsOwnerElements), `TCMap<int, TCSharedPointer<CJob>>`, also an owner
  /// of a buffer of them, `TCVector<TCSharedPointer<CJob>>`, and one that
  /// keeps them in storage of bytes, `TCOptional<TCSharedPointer<CJob>>`.
  Container,
};

/// Of each specialization of a class template, whether it holds owners as
/// elements (holdsOwnerElements), a summary of the translation unit: a
/// cycle of containers through their arguments is worked out to its least
/// fixed point, from false.
struct COwnerElementTypes {
  static bool holdsUncached(const Type *const &Of);
  TCSummaries<const Type *, bool> Holds{
      [](const Type *const &Of) { return holdsUncached(Of); }, false, true};
};

/// Of each type, how it holds owners (holdingUncached), a summary of the
/// translation unit, from none: each type is told once, and a cycle of
/// containers through their arguments is worked out with the one of
/// holdsOwnerElements.
struct CHoldings {
  static EHolding holdingUncached(const Type *const &Held);
  TCSummaries<const Type *, EHolding> Of{
      [](const Type *const &Held) { return holdingUncached(Held); },
      EHolding::None, EHolding::Record};
};
} // namespace

/// How a value of \p Type holds owners that keep what they own by value, the
/// one classification of what holds them (CHoldings), which every question
/// of which owners a value holds and at which paths goes through: a value
/// held by a reference holds none, nor does a weak owner.
static EHolding holdingOf(QualType Type, const ASTContext &Context) {
  if (Type.isNull() || Type->isReferenceType())
    return EHolding::None;
  if (stackNearlyExhausted())
    return withStack([&] { return holdingOf(Type, Context); });
  // By the canonical type (ownerKindOf).
  return astCache<CHoldings>(Context).Of.get(
      Type.getCanonicalType().getUnqualifiedType().getTypePtr());
}

/// Whether \p Holding is of what holds owners as its parts, a record, an
/// array or a container, whose copy copies them and whose move moves them.
static bool holdsParts(EHolding Holding) {
  switch (Holding) {
  case EHolding::Record:
  case EHolding::Array:
  case EHolding::Container:
    return true;
  case EHolding::None:
  case EHolding::Owner:
    return false;
  }
  return false;
}

bool holdsOwnerParts(QualType Type, const ASTContext &Context) {
  return holdsParts(holdingOf(Type.getNonReferenceType(), Context));
}

/// Whether what \p Type holds by value, past members, bases, arrays and the
/// elements of containers, not past pointers, references or what weak owners
/// refer to, has an owner that keeps what it owns (holdingOf).
static bool holdsOwners(QualType Type, const ASTContext &Context) {
  return holdingOf(Type, Context) != EHolding::None;
}

/// Of an array, its elements; of an owner, by its kind, also a buffer of
/// owners; of a record, by its bases, also virtual ones, and members held by
/// value, else as a container (holdsOwnerElements).
EHolding CHoldings::holdingUncached(const Type *const &Of) {
  const QualType Type(Of, 0);
  if (Type->isDependentType())
    return EHolding::None;
  const auto *Record = Type->getAsRecordDecl();
  const ASTContext *Context = Record ? &Record->getASTContext() : nullptr;
  if (const ArrayType *Array = Type->getAsArrayTypeUnsafe()) {
    const RecordDecl *Inner =
        Array->getElementType()->getBaseElementTypeUnsafe()->getAsRecordDecl();
    return Inner && holdingOf(Array->getElementType(),
                              Inner->getASTContext()) != EHolding::None
               ? EHolding::Array
               : EHolding::None;
  }
  if (!Record)
    return EHolding::None;
  switch (ownerKindOf(Type, *Context)) {
  case EOwnerKind::Weak:
    return EHolding::None;
  case EOwnerKind::Shared:
  case EOwnerKind::Unique:
    return EHolding::Owner;
  case EOwnerKind::Buffer:
  case EOwnerKind::SharedBuffer:
    return holdsOwnerElements(Type, *Context) ? EHolding::Container
                                              : EHolding::Owner;
  case EOwnerKind::None:
    break;
  }
  if (const RecordDecl *Definition = Record->getDefinition()) {
    if (const auto *Class = dyn_cast<CXXRecordDecl>(Definition))
      for (const CXXBaseSpecifier &Base : Class->bases())
        if (holdingOf(Base.getType(), *Context) != EHolding::None)
          return EHolding::Record;
    for (const FieldDecl *Field : Definition->fields())
      if (holdingOf(Field->getType(), *Context) != EHolding::None)
        return EHolding::Record;
  }
  return holdsOwnerElements(Type, *Context) ? EHolding::Container
                                            : EHolding::None;
}

/// Whether \p Type is a container whose elements are owners or hold them by
/// value, told by the arguments of its template, as forEachHeld classifies
/// them, not by its name: a specialization of a class template, not a view
/// (ViewTypes) nor an owner of what it points to (a shared, unique or weak
/// one), with a type argument that holds owners (holdsOwners),
/// `TCVector<TCSharedPointer<CJob>>`, `TCMap<int, TCSharedPointer<CJob>>`;
/// a copy of it copies them, a move moves them.
static bool holdsOwnerElements(QualType Type, const ASTContext &Context) {
  if (stackNearlyExhausted())
    return withStack([&] { return holdsOwnerElements(Type, Context); });
  Type = Type.getNonReferenceType();
  if (Type.isNull() ||
      !isa_and_nonnull<ClassTemplateSpecializationDecl>(
          Type->getAsCXXRecordDecl()))
    return false;
  return astCache<COwnerElementTypes>(Context).Holds.get(
      Type.getCanonicalType().getUnqualifiedType().getTypePtr());
}

bool COwnerElementTypes::holdsUncached(const Type *const &Of) {
  const QualType Type(Of, 0);
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(
          Type->getAsCXXRecordDecl());
  if (!Specialization)
    return false;
  const ASTContext &Context = Specialization->getASTContext();
  const EOwnerKind Kind = ownerKindOf(Type, Context);
  if ((Kind != EOwnerKind::None && !ownsBuffer(Kind)) ||
      isDefaultViewType(Type, Context))
    return false;
  llvm::SmallVector<TemplateArgument, 4> Arguments(
      Specialization->getTemplateArgs().asArray());
  for (size_t iArgument = 0; iArgument < Arguments.size(); ++iArgument) {
    const TemplateArgument &Argument = Arguments[iArgument];
    switch (Argument.getKind()) {
    case TemplateArgument::Type:
      if (holdsOwners(Argument.getAsType(), Context))
        return true;
      break;
    case TemplateArgument::Pack:
      llvm::append_range(Arguments, Argument.pack_elements());
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
  }
  return false;
}

bool CStorageRoots::all(
    llvm::function_ref<bool(const CStorageRoot &)> Is) const {
  return llvm::all_of(Roots, Is);
}

bool CStorageRoots::allStatic() const {
  return !Roots.empty() && all([](const CStorageRoot &Root) {
    return Root.Kind == ERootKind::Static;
  });
}

/// The root of the object of class \p Class: of an actor, of a class of
/// CAllowUnsafeThis, or of another, of \p Explicit, its explicit object
/// parameter, null of `this`.
static CStorageRoot objectRoot(const CXXRecordDecl *Class,
                               const ValueDecl *Explicit) {
  CStorageRoot Root;
  Root.Class = Class;
  Root.Variable = Explicit;
  if (Class && isActorClass(Class))
    Root.Kind = ERootKind::Actor;
  else if (Class && isKnownSafeThisClass(Class))
    Root.Kind = ERootKind::UnsafeThis;
  else
    Root.Kind = ERootKind::Object;
  return Root;
}

CStorageRoot objectRootOf(const CXXRecordDecl *Class) {
  CStorageRoot Root;
  Root.Kind = ERootKind::Object;
  Root.Class = Class;
  return Root;
}

static CStorageRoots memberValueRootsUncached(const FieldDecl *const &Field);

namespace {
/// Of each member, the roots of each value it is ever given
/// (memberValueRoots), a summary of the translation unit: a value made of
/// the member itself is worked out to its least fixed point, from none,
/// widened to what can't be told.
struct CFieldRoots {
  TCSummaries<const FieldDecl *, CStorageRoots> Of{
      &memberValueRootsUncached, CStorageRoots{},
      CStorageRoots{{CStorageRoot{}}}};
};
} // namespace

CStorageRoots memberValueRoots(const FieldDecl *Field, ASTContext &Context) {
  if (stackNearlyExhausted())
    return withStack([&] { return memberValueRoots(Field, Context); });
  return astCache<CFieldRoots>(Context).Of.get(Field);
}

static CStorageRoots memberValueRootsUncached(const FieldDecl *const &Field) {
  ASTContext &Context = Field->getASTContext();
  CStorageRoots Roots;
  // What the code of its class gives it is all it is ever given only when
  // that is stated (targetToldByOwnClass).
  if (!targetToldByOwnClass(Field)) {
    CStorageRoot Untold;
    Untold.Member = Field;
    Roots.Roots.push_back(Untold);
    return Roots;
  }
  bool Told = true;
  // What a view refers to is what its value points to, a level up.
  const QualType Type = Field->getType();
  const unsigned Level =
      !Type->isReferenceType() && isDefaultViewType(Type, Context) ? 1 : 0;
  auto Given = [&](const Expr *Value) {
    if (!Value || !Told)
      return;
    if (nullPointerConstantOf(Value->IgnoreParenImpCasts(), Context,
                              Expr::NPC_ValueDependentIsNotNull))
      return;
    const CStorageRoots Of = storageRootsOf(Value, Context, Level);
    if (Of.Roots.empty()) {
      Told = false;
      return;
    }
    for (const CStorageRoot &Root : Of.Roots)
      if (!llvm::is_contained(Roots.Roots, Root))
        Roots.Roots.push_back(Root);
  };
  const auto *Class = dyn_cast<CXXRecordDecl>(Field->getParent());
  const CXXRecordDecl *Definition = Class ? Class->getDefinition() : nullptr;
  if (!Definition || Definition->hasFriends() ||
      !llvm::all_of(Definition->methods(), [](const CXXMethodDecl *Method) {
        return Method->isImplicit() || Method->isDefaulted() ||
               isa<CXXDestructorDecl>(Method) || Method->getDefinition();
      }))
    Told = false;
  if (Told && Field->hasInClassInitializer())
    Given(Field->getInClassInitializer());
  // The member a target names, `m_pValue` or `this->m_View`.
  auto IsField = [&](const Expr *Target) {
    const auto *Member = dyn_cast<MemberExpr>(Target->IgnoreParenImpCasts());
    return Member && Member->getMemberDecl() == Field;
  };
  if (Told)
    for (const CXXMethodDecl *Method : Definition->methods()) {
      const FunctionDecl *Body = Method->getDefinition();
      if (!Body || !Body->getBody())
        continue;
      if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Body))
        for (const CXXCtorInitializer *Initializer : Constructor->inits())
          if (Initializer->getMember() == Field && Initializer->isWritten())
            Given(Initializer->getInit());
      TCRecursive<void(const Stmt *)> Visit = [&](const Stmt *S) {
        if (!S || !Told)
          return;
        if (const auto *Assign = dyn_cast<BinaryOperator>(S);
            Assign && Assign->isAssignmentOp() && IsField(Assign->getLHS())) {
          if (Assign->getOpcode() == BO_Assign)
            Given(Assign->getRHS());
          else
            Told = false;
        }
        // An assignment operator of the member's class, of a view.
        if (const auto *Assign = dyn_cast<CXXOperatorCallExpr>(S);
            Assign && Assign->isAssignmentOp() && Assign->getNumArgs() == 2 &&
            IsField(Assign->getArg(0))) {
          if (Assign->getOperator() == OO_Equal)
            Given(Assign->getArg(1));
          else
            Told = false;
        }
        for (const Stmt *Child : keptChildren(S, Context))
          Visit(Child);
      };
      Visit(Body->getBody());
    }
  if (!Told)
    Roots.Roots.assign(1, CStorageRoot{});
  return Roots;
}

CStorageRoots borrowedPartRoots(const Expr *Record,
                                const FunctionDecl *Function,
                                ASTContext &Context) {
  CStorageRoots Roots;
  auto Add = [&](const CStorageRoot &Root) {
    if (!llvm::is_contained(Roots.Roots, Root))
      Roots.Roots.push_back(Root);
  };
  CStorageRoot Unknown;
  // The member of the object the record is, whose initializers in its class
  // give its parts their values too, unless a member function of the class
  // assigns it or a part of it.
  const CThisMember Member =
      Function ? thisMemberOf(Record, Function) : CThisMember{};
  const FieldDecl *First =
      Member.Path.empty() || Member.Element ? nullptr : Member.Path.front();
  llvm::SmallVector<const Expr *, 4> Initializers;
  bool OuterTold = true;
  if (const auto *Outer =
          First ? dyn_cast<CXXRecordDecl>(First->getParent()) : nullptr) {
    if (First->hasInClassInitializer())
      Initializers.push_back(First->getInClassInitializer());
    for (const CXXMethodDecl *Method : Outer->methods()) {
      const FunctionDecl *Body = Method->getDefinition();
      if (!Body || !Body->getBody())
        continue;
      if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Body))
        for (const CXXCtorInitializer *Initializer : Constructor->inits())
          if (Initializer->getMember() == First && Initializer->isWritten())
            Initializers.push_back(Initializer->getInit());
      if (isa<CXXConstructorDecl, CXXDestructorDecl>(Body))
        continue;
      forEachStmtOnce(Body->getBody(), [&](const Stmt *S) {
        const Expr *Target = nullptr;
        if (const auto *Assign = dyn_cast<BinaryOperator>(S);
            Assign && Assign->isAssignmentOp())
          Target = Assign->getLHS();
        else if (const auto *Call = dyn_cast<CXXOperatorCallExpr>(S);
                 Call && Call->isAssignmentOp() && Call->getNumArgs() > 0)
          Target = Call->getArg(0);
        if (Target) {
          const CThisMember Assigned = thisMemberOf(Target, Body);
          if (!Assigned.Path.empty() && Assigned.Path.front() == First)
            OuterTold = false;
        }
        return OuterTold ? EWalk::Descend : EWalk::Stop;
      });
    }
  }
  // The members of the record and of the records it holds by value, each
  // with the members on the way from the record.
  QualType Type = Record->getType().getNonReferenceType();
  if (Type->isPointerType())
    Type = Type->getPointeeType();
  struct CStep {
    const RecordDecl *Of;
    llvm::SmallVector<const FieldDecl *, 4> Path;
  };
  llvm::SmallVector<CStep, 4> Work;
  if (const RecordDecl *Of = Context.getBaseElementType(Type)->getAsRecordDecl())
    Work.push_back({Of, {}});
  llvm::SmallPtrSet<const RecordDecl *, 8> Seen;
  while (!Work.empty()) {
    const CStep Step = Work.pop_back_val();
    const RecordDecl *Definition = Step.Of->getDefinition();
    if (!Definition || !Seen.insert(Definition).second)
      continue;
    for (const FieldDecl *Field : Definition->fields()) {
      llvm::SmallVector<const FieldDecl *, 4> Path = Step.Path;
      Path.push_back(Field);
      const QualType Of = Context.getBaseElementType(Field->getType());
      if (!isBorrowedField(Field)) {
        if (Of->isRecordType() && isValueRecordType(Of, Context))
          Work.push_back({Of->getAsRecordDecl(), std::move(Path)});
        continue;
      }
      for (const CStorageRoot &Root : memberValueRoots(Field, Context).Roots)
        Add(Root);
      if (!First || !targetToldByOwnClass(Field))
        continue;
      if (!OuterTold) {
        Add(Unknown);
        continue;
      }
      // What the initializers of the member of the object give it.
      llvm::SmallVector<const FieldDecl *, 4> Projection(
          Member.Path.begin() + 1, Member.Path.end());
      Projection.append(Path.begin(), Path.end());
      for (const Expr *Initializer : Initializers) {
        const Expr *Given = projected(Initializer, Projection);
        if (!Given) {
          Add(Unknown);
          continue;
        }
        if (nullPointerConstantOf(Given, Context,
                                  Expr::NPC_ValueDependentIsNotNull))
          continue;
        for (const CStorageRoot &Root :
             (Field->getType()->isReferenceType()
                  ? storageRootsOf(Given, Context)
                  : pointeeRootsOf(Given, Context))
                 .Roots)
          Add(Root);
      }
    }
  }
  return Roots;
}

/// Whether the local reference \p Variable is bound to what is made of
/// itself, `int &Chosen = _bChoose ? Local : Chosen`, which it isn't yet.
static bool boundToItself(const VarDecl *Variable, ASTContext &Context) {
  bool Found = false;
  TCRecursive<void(const Stmt *)> Visit = [&](const Stmt *S) {
    if (!S || Found)
      return;
    if (const auto *Ref = dyn_cast<DeclRefExpr>(S);
        Ref && Ref->getDecl() == Variable)
      Found = true;
    for (const Stmt *Child : keptChildren(S, Context))
      Visit(Child);
  };
  Visit(Variable->getInit());
  return Found;
}

/// Whether \p Construct makes a view (ViewTypes) of what it is given, not
/// a copy or a move of another.
static bool makesView(const CXXConstructExpr *Construct, ASTContext &Context) {
  return isDefaultViewType(Construct->getType(), Context) &&
         !Construct->getConstructor()->isCopyOrMoveConstructor();
}

static QualType ownerTypeOf(const Expr *Owner) {
  const QualType Type = Owner->getType();
  return Type->isPointerType() ? Type->getPointeeType() : Type;
}

static bool givenOwnActor(const ParmVarDecl *Parameter, ASTContext &Context);

/// A root of the kind \p Kind.
static CStorageRoot rootOf(ERootKind Kind) {
  CStorageRoot Root;
  Root.Kind = Kind;
  return Root;
}

/// The root of the storage of the variable \p Variable, held by value, by its
/// storage duration: of a thread, ThreadLocal; static, Static; automatic,
/// Frame, of a local or a parameter.
static CStorageRoot variableRoot(const VarDecl *Variable) {
  if (Variable->getTLSKind() != VarDecl::TLS_None) {
    CStorageRoot Root = rootOf(ERootKind::ThreadLocal);
    Root.Variable = Variable;
    return Root;
  }
  if (Variable->hasGlobalStorage())
    return rootOf(ERootKind::Static);
  CStorageRoot Root = rootOf(ERootKind::Frame);
  Root.Variable = Variable;
  return Root;
}

/// The root of what the reference or pointer parameter \p Parameter, named
/// at \p At, refers to, of the class \p Class: the object of the actor the
/// function runs on when each call gives it that (givenOwnActor) and, of a
/// pointer, it holds what it is given there (assignedAt), else what it refers
/// to.
static CStorageRoot parameterRoot(const ParmVarDecl *Parameter,
                                  const CXXRecordDecl *Class, const Expr *At,
                                  ASTContext &Context) {
  if (Class && isActorClass(Class) &&
      (Parameter->getType()->isReferenceType() ||
       assignedAt(Parameter, At, Context).empty()) &&
      givenOwnActor(Parameter, Context))
    return objectRoot(Class, Parameter);
  CStorageRoot Root;
  Root.Kind = ERootKind::Parameter;
  Root.Variable = Parameter;
  Root.Class = Class;
  return Root;
}

/// The anchor of what \p E refers to at \p Level of provenance, by its type:
/// at 0 what a glvalue designates or a pointer value points to, each level
/// above through a pointer; none past what isn't a pointer.
static std::optional<CAnchor> referentAnchorAt(const Expr *E, unsigned Level) {
  QualType Type = E->getType().getNonReferenceType();
  unsigned Steps = Level + (E->isPRValue() && Type->isPointerType() ? 1 : 0);
  for (; Steps > 0; --Steps) {
    if (Type.isNull() || !Type->isPointerType())
      return std::nullopt;
    Type = Type->getPointeeType();
  }
  if (Type.isNull() || Type->isVoidType() || Type->isFunctionType() ||
      Type->isDependentType())
    return std::nullopt;
  return objectAnchorOf(Type);
}

bool isValueRecordType(QualType Type, const ASTContext &Context) {
  if (Type.isNull() || Type->isDependentType())
    return false;
  const QualType Element =
      Context.getBaseElementType(Type.getNonReferenceType());
  return Element->isRecordType() && !isDefaultViewType(Element, Context) &&
         ownerKindOf(Element, Context) == EOwnerKind::None &&
         !isActorClass(Element->getAsCXXRecordDecl());
}

/// \p Variable as a local record held by value (isOpaqueRecord); null for
/// another declaration.
static const VarDecl *opaqueRecordOf(const ValueDecl *Variable,
                                     const ASTContext &Context) {
  const auto *Local = dyn_cast_or_null<VarDecl>(Variable);
  if (!Local || !Local->hasLocalStorage() ||
      Local->getType()->isReferenceType() ||
      !isValueRecordType(Local->getType(), Context))
    return nullptr;
  return Local;
}

bool isOpaqueRecord(const ValueDecl *Variable, const ASTContext &Context) {
  return opaqueRecordOf(Variable, Context);
}

/// Whether \p Type is of the `buffer` or `sharedbuffer` kind of OwnerTypes by
/// the name of its canonical type with a layout that has room for its data,
/// `TCVector<int, CAllocator_Inline>`, which isn't an owner (ownerKindOf) and
/// holds its data in itself.
static bool holdsDataInItself(QualType Type, const ASTContext &Context) {
  const CVocabulary &Vocabulary = vocabulary();
  Type = Type.getCanonicalType().getUnqualifiedType();
  return (matchesTypeName(Type, Vocabulary.BufferOwnerTypes.names(), Context) ||
          matchesTypeName(Type, Vocabulary.SharedBufferOwnerTypes.names(),
                          Context)) &&
         !isBufferOwningType(Type, Context);
}

namespace {
/// A member on the way from a record to what a walk of provenance reaches
/// (recordPartAt), and whether it is selected through a pointer, `->`.
struct CMemberStep {
  const FieldDecl *Field = nullptr;
  bool Arrow = false;
};
} // namespace

/// What the way reaches, at \p Level, of a record of the type \p Holder,
/// through the members \p Path of it, the first first, and an element or a
/// part that can't be told after them (\p Element) (recordPartAt). Of a
/// record that isn't a view nor an owner (isValueRecordType), a member that
/// is a reference, and one that is a pointer or a view, also an array of
/// them, whose value is loaded, at a level above 0 or selected through, is
/// a value, except of a local variable, whose members' values the
/// provenance follows to what each was given (its slots). \p Record is the
/// variable the record is, when the way starts at one, of which its own
/// storage and what an owner member owns are told apart, and with \p Tell
/// of any record; else those are followed as anything else.
static CRecordPart recordPartThrough(QualType Holder, const VarDecl *Record,
                                     llvm::ArrayRef<CMemberStep> Path,
                                     bool Element, unsigned Level,
                                     ASTContext &Context, bool Tell = false) {
  CRecordPart Part;
  Part.Record = Record;
  auto Is = [&](ERecordPart Kind) {
    Part.Kind = Kind;
    return Part;
  };
  // Of where the record is, told only of a variable, or when asked.
  auto Where = [&](ERecordPart Kind) {
    return Is(Record || Tell ? Kind : ERecordPart::Followed);
  };
  // A pointer, reference or view stored in it: of a local variable, what its
  // slot was given; of another record, a value, except of a pointer or a
  // reference member (isBorrowedField) at the level of what it refers to,
  // what its class gives it (Assigned), and past that what can't be told.
  auto Stored = [&](size_t iStep = 0, const FieldDecl *Field = nullptr) {
    if (Record && !isa<ParmVarDecl>(Record))
      return Is(ERecordPart::Followed);
    if (!Field || !isBorrowedField(Field))
      return Is(ERecordPart::Value);
    // What it refers to, or a member of that held by value, at the level of
    // its storage.
    const bool Reference = Field->getType()->isReferenceType();
    bool Exact = Level == (Reference || iStep + 1 < Path.size() ? 0u : 1u);
    for (size_t iRest = iStep + 1; Exact && iRest < Path.size(); ++iRest)
      Exact = !isBorrowedField(Path[iRest].Field) &&
              !Path[iRest].Field->getType()->isPointerType() &&
              !isDefaultViewType(Path[iRest].Field->getType(), Context);
    if (!Exact && targetToldByOwnClass(Field))
      return Is(ERecordPart::Value);
    Part.Field = Field;
    return Is(ERecordPart::Assigned);
  };
  Holder = Context.getBaseElementType(Holder.getNonReferenceType());
  for (size_t iStep = 0; iStep < Path.size(); ++iStep) {
    if (holdsDataInItself(Holder, Context))
      return Where(ERecordPart::Own);
    if (!isValueRecordType(Holder, Context))
      return Is(ERecordPart::Followed);
    const FieldDecl *Field = Path[iStep].Field;
    const QualType Type = Field->getType();
    if (Type->isReferenceType())
      return Stored(iStep, Field);
    const bool Last = iStep + 1 == Path.size();
    const bool Loaded = Level > 0 || (!Last && Path[iStep + 1].Arrow);
    const QualType Of = Context.getBaseElementType(Type);
    if (ownerKindOf(Of, Context) != EOwnerKind::None) {
      if (Level == 0)
        return Is(ERecordPart::Followed);
      Part.Owner = Field;
      return Where(ERecordPart::Owned);
    }
    if (Of->isPointerType() || isDefaultViewType(Of, Context) ||
        !Of->isRecordType())
      return Loaded ? Stored(iStep, Field) : Is(ERecordPart::Followed);
    Holder = Of;
  }
  if (Level == 0)
    return Is(ERecordPart::Followed);
  if (holdsDataInItself(Holder, Context))
    return Where(ERecordPart::Own);
  if (!isValueRecordType(Holder, Context))
    return Is(ERecordPart::Followed);
  // A part of it that can't be told is any of them, unless it holds no owner.
  return Element && holdsOwnerParts(Holder, Context) ? Is(ERecordPart::Followed)
                                                     : Stored();
}

CRecordPart recordPartAt(const Expr *At, unsigned Level,
                         const CProvenance &Walk, ASTContext &Context) {
  const llvm::ArrayRef<CProjection> Taken = Walk.projections();
  // What a call gives (callResultOf): a pointer, reference or view member
  // stored in its object is a value; what an owner member of a record held
  // by value owns is Owned where the record is. Else what the call gives is
  // followed, below by the type it gives.
  if (isa<CallExpr>(At->IgnoreParens())) {
    const CCallResult Result = callResultOf(At, Level, Context);
    CRecordPart Part;
    // The record, also as a pointer to it is given, `fg_Job(&Context)`.
    const Expr *Record = Result.Of ? Result.Of->IgnoreParenImpCasts() : nullptr;
    if (const auto *Address = dyn_cast_or_null<UnaryOperator>(Record);
        Address && Address->getOpcode() == UO_AddrOf)
      Record = Address->getSubExpr()->IgnoreParenImpCasts();
    const auto *Object = dyn_cast_or_null<DeclRefExpr>(Record);
    // Of a local variable, what the member's slot was given, followed
    // through the body (recordPartThrough).
    if (Result.Kind == ECallResult::StoredBorrow) {
      if (Object && !isa<ParmVarDecl>(Object->getDecl()) &&
          isOpaqueRecord(Object->getDecl(), Context))
        return Part;
      Part.Kind = Result.Field ? ERecordPart::Assigned : ERecordPart::Value;
      Part.Field = Result.Field;
      return Part;
    }
    if (const VarDecl *Record =
            Object ? opaqueRecordOf(Object->getDecl(), Context) : nullptr;
        Result.Kind == ECallResult::Owned && Result.Owner && Record) {
      Part.Kind = ERecordPart::Owned;
      Part.Record = Record;
      Part.Owner = Result.Owner;
      return Part;
    }
  }
  // The members on the way, the outermost first: of a member, `.` or `->`,
  // also of a member of it, and of a structured binding of a record, the
  // member it names of the variable it decomposes, `auto [pValue] =
  // Context`; then what the way takes past them.
  llvm::SmallVector<CMemberStep, 4> Path;
  const Expr *Of = At->IgnoreParens();
  for (unsigned Depth = 0; Depth < 64; ++Depth) {
    if (const auto *Member = dyn_cast<MemberExpr>(Of)) {
      const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl());
      if (!Field)
        return {};
      Path.insert(Path.begin(), {Field, Member->isArrow()});
      Of = Member->getBase()->IgnoreParenImpCasts();
      continue;
    }
    const auto *Name = dyn_cast<DeclRefExpr>(Of);
    const auto *Binding =
        Name ? dyn_cast<BindingDecl>(Name->getDecl()) : nullptr;
    if (!Binding)
      break;
    if (!Binding->getBinding() ||
        !isa<MemberExpr>(Binding->getBinding()->IgnoreParens()))
      return {};
    Of = Binding->getBinding()->IgnoreParens();
  }
  bool Element = false;
  if (Level < Taken.size()) {
    for (const FieldDecl *Field : Taken[Level].Path)
      Path.push_back({Field, false});
    Element = Taken[Level].Element;
  }
  // Through a pointer to the record, `pContext->m_pValue`, of its type.
  if (!Path.empty() && Path.front().Arrow) {
    const QualType Pointer = Of->getType();
    return Pointer->isPointerType()
               ? recordPartThrough(Pointer->getPointeeType(), nullptr, Path,
                                   Element, Level, Context)
               : CRecordPart{};
  }
  const auto *Ref = dyn_cast<DeclRefExpr>(Of);
  if (const VarDecl *Record =
          Ref ? opaqueRecordOf(Ref->getDecl(), Context) : nullptr)
    return recordPartThrough(Record->getType(), Record, Path, Element, Level,
                             Context);
  const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  // Of another record, a parameter by reference, what a call gives, `*this`,
  // by its type.
  if (!Variable || isa<ParmVarDecl>(Variable) ||
      !Variable->getType()->isReferenceType() || !Variable->hasLocalStorage())
    return Path.empty() ? CRecordPart{}
                        : recordPartThrough(Of->getType(), nullptr, Path,
                                            Element, Level, Context);
  // Through a local reference, of what it designates, each the same; of what
  // can't be told, by its type.
  bool Told = true;
  const llvm::SmallVector<CSlot, 2> Slots =
      designatedSlots(Ref, Context, &Told);
  if (!Told || Slots.empty() || llvm::any_of(Slots, [&](const CSlot &Slot) {
        return !isOpaqueRecord(Slot.Variable, Context);
      }))
    return Path.empty() ? CRecordPart{}
                        : recordPartThrough(Of->getType(), nullptr, Path,
                                            Element, Level, Context);
  std::optional<CRecordPart> Same;
  for (const CSlot &Slot : Slots) {
    llvm::SmallVector<CMemberStep, 4> Through;
    for (const FieldDecl *Field : Slot.Path)
      Through.push_back({Field, false});
    if (!Slot.Element)
      Through.append(Path.begin(), Path.end());
    const CRecordPart Part =
        recordPartThrough(Slot.Variable->getType(), Slot.Variable, Through,
                          Slot.Element || Element, Level, Context);
    if (Same && (Same->Kind != Part.Kind || Same->Record != Part.Record ||
                 Same->Owner != Part.Owner))
      return {};
    Same = Part;
  }
  return *Same;
}

CCallResult callResultOf(const Expr *E, unsigned Level, ASTContext &Context) {
  const CCallResult Untold;
  const auto *Call =
      dyn_cast_or_null<CallExpr>(E ? E->IgnoreParens() : nullptr);
  if (!Call)
    return Untold;
  // What gives back what it is given, `fg_Move(Value)`, is that.
  if (givesItsArgument(Call) && Call->getNumArgs() > 0)
    return {ECallResult::Given, nullptr, Call->getArg(0), false};
  const CEffects &Effects = effectsOf(Call, Context);
  if (!Effects.Callee || keptPointerArgument(Call))
    return Untold;
  // Of each body the call can run, each override of a virtual one in the
  // translation unit (calledRoots): one that can't be seen there can't be
  // told; one of another isn't taken to run.
  const CCalledRoots Called =
      CProvenance(Context).calledRoots(Call, Effects.Callee);
  if (Called.Bodies.empty() || Called.bUnseen)
    return Untold;
  llvm::SmallVector<std::pair<const CReturnedRoot *, const FunctionDecl *>, 4>
      Returned;
  for (const auto &[Callee, Roots] : Called.Bodies) {
    if (Roots->Other)
      return Untold;
    for (const CReturnedRoot &Root : Roots->Roots)
      Returned.push_back({&Root, Callee});
  }
  // Each way of each return: of what it ends at, the object or an argument
  // given by reference, by its type, through the members the way takes
  // there, at its level (recordPartThrough).
  auto RootResult = [&](const CReturnedRoot &Root,
                        const FunctionDecl *Callee) -> CCallResult {
    if (Root.Kind == CReturnedRoot::EKind::Static ||
        Root.Kind == CReturnedRoot::EKind::Outer)
      return {ECallResult::New};
    const COperandEffect *Given = nullptr;
    for (const COperandEffect &Effect : Effects.Operands)
      if (Root.Kind == CReturnedRoot::EKind::Object
              ? Effect.Use == EUse::CallObject
              : (Effect.Use == EUse::Bind || Effect.Use == EUse::Copy ||
                 Effect.Use == EUse::CallObject) &&
                    Effect.Into.Kind == EDestination::Parameter &&
                    Effect.Into.Index == Root.Index)
        Given = &Effect;
    const auto *Of = Given ? dyn_cast_or_null<Expr>(Given->Operand) : nullptr;
    if (!Of)
      return Untold;
    const bool Object = Given->Use == EUse::CallObject;
    // Of the object or the argument \p Of.
    auto Is = [&](ECallResult Kind, const FieldDecl *Owner = nullptr) {
      return CCallResult{Kind, Owner, Of, Object};
    };
    // The record the way ends in: the object, also what a pointer the call
    // is made through points to, `pBuffer->f_GetArray()`, what an argument
    // given by reference designates, and what a pointer parameter points to,
    // given by value or by reference, a level above the parameter's name,
    // where the way ends: `return _pContext->m_pJob;` of `CContext const
    // *_pContext` is a member stored in what the argument points to, as
    // `return _Context.m_pJob;` of `CContext const &_Context` is of what it
    // designates.
    const auto *Parameter =
        Root.Kind == CReturnedRoot::EKind::Parameter
            ? dyn_cast_or_null<ParmVarDecl>(Given->Into.Declaration)
            : nullptr;
    const bool ByValue =
        Parameter && !Parameter->getType()->isReferenceType();
    const bool Pointer = Parameter && Parameter->getType()
                                          .getNonReferenceType()
                                          ->isPointerType();
    const unsigned HolderLevel = Pointer ? 1 : 0;
    // Whether the argument as it is given is the record, not a reference to
    // the pointer to it.
    const bool OfIsHolder = !Pointer || ByValue;
    const unsigned Relative =
        Root.Level >= HolderLevel ? Root.Level - HolderLevel : 0;
    const CProjection *Taken = Root.Level >= HolderLevel &&
                                       Relative < Root.Projections.size()
                                   ? &Root.Projections[Relative]
                                   : nullptr;
    const bool Whole = Root.Bound.empty() && !Root.Part &&
                       (!Taken || (Taken->Path.empty() && !Taken->Element));
    // Of an argument held by value that isn't a pointer, only the value
    // itself, a number, is told; of a pointer, the pointer itself is the
    // parameter of the function called, or the one given by reference.
    if (ByValue && !Pointer)
      return Whole && !Parameter->getType()->isRecordType() &&
                     !Parameter->getType()->isArrayType()
                 ? Is(ECallResult::Given)
                 : Untold;
    if (Root.Level < HolderLevel)
      return !ByValue && Whole ? Is(ECallResult::Given) : Untold;
    // An argument itself, at each level, as the call is given it.
    if (Root.Kind == CReturnedRoot::EKind::Parameter && Whole && OfIsHolder)
      return Is(ECallResult::Given);
    // The members the way takes at the level it ends at, `return m_pJob;`
    // of `CJob *const &f_Job() const`, whose value is what is loaded above
    // level 0. Of the whole object there, what is taken of it is of the
    // result's type, which recordPartAt tells.
    if (Relative == 0 && Level > 0 && Whole &&
        Root.Kind == CReturnedRoot::EKind::Object)
      return Untold;
    llvm::SmallVector<CMemberStep, 4> Steps;
    bool Part = false;
    if (!Root.Bound.empty()) {
      for (const FieldDecl *Field : Root.Bound)
        Steps.push_back({Field, false});
    } else if (Taken) {
      for (const FieldDecl *Field : Taken->Path)
        Steps.push_back({Field, false});
      Part = Taken->Element;
    }
    if (Relative == 0)
      Part = Part || Root.Part;
    QualType Holder = Of->getType().getNonReferenceType();
    if (Holder->isPointerType()) {
      if (!Object && !Pointer)
        return Untold;
      Holder = Holder->getPointeeType();
    }
    const unsigned At = Level + Relative;
    const CRecordPart Reached = recordPartThrough(Holder, nullptr, Steps, Part,
                                                  At, Context, /*Tell=*/true);
    switch (Reached.Kind) {
    case ERecordPart::Value:
      return Is(ECallResult::StoredBorrow);
    case ERecordPart::Assigned: {
      CCallResult Stored = Is(ECallResult::StoredBorrow);
      Stored.Field = Reached.Field;
      return Stored;
    }
    case ERecordPart::Owned:
      return Is(ECallResult::Owned, Reached.Owner);
    case ERecordPart::Own:
      return Is(ECallResult::OwnStorage);
    case ERecordPart::Followed:
      break;
    }
    // The storage of the object at level 0, a member of it or itself, as
    // the way takes a part or, of the object of a member function, as what
    // its returns give of it (returnedRelation) tells; not what a reference
    // member of a view or an owner refers to.
    if (At == 0) {
      if (!Root.Bound.empty())
        return Untold;
      if (!Steps.empty() || Part)
        return Is(ECallResult::OwnStorage);
      // Of a reference to a pointer, what it points to.
      if (Root.Kind != CReturnedRoot::EKind::Object)
        return Is(OfIsHolder ? ECallResult::Given : ECallResult::OwnStorage);
      const auto *Getter = dyn_cast<CXXMethodDecl>(Callee);
      if (!Getter)
        return Is(ECallResult::Given);
      const std::optional<CObjectRelation> Gives =
          returnedRelation(Getter, &Context);
      if (!Gives || Gives->bThroughPointer || isCopyStorage(*Gives))
        return Untold;
      if (Gives->Kind == EObjectRelation::SameObject)
        return Is(ECallResult::Given);
      if (Gives->Kind == EObjectRelation::Subobject)
        return Is(ECallResult::OwnStorage);
      return Untold;
    }
    // What the object, an owner, owns.
    if (Root.Bound.empty() && ownerKindOf(Context.getBaseElementType(Holder),
                                          Context) != EOwnerKind::None)
      return Is(ECallResult::Owned);
    return Untold;
  };
  if (Returned.empty())
    return {ECallResult::New};
  // All ways the same; a way that gives nothing of the object or the
  // arguments, `return &g_Global;` of one override, doesn't change what the
  // others give.
  std::optional<CCallResult> Same;
  bool New = false;
  for (const auto &[Root, Callee] : Returned) {
    const CCallResult Result = RootResult(*Root, Callee);
    if (Result.Kind == ECallResult::New) {
      New = true;
      continue;
    }
    if (Result.Kind == ECallResult::Untold ||
        (Same && (Same->Kind != Result.Kind || Same->Owner != Result.Owner ||
                  Same->Of != Result.Of || Same->Field != Result.Field)))
      return Untold;
    Same = Result;
  }
  if (!Same && New)
    return {ECallResult::New};
  return Same.value_or(Untold);
}

static const FunctionDecl *executingFunction(const Stmt *Use,
                                             FParentOf ParentOf,
                                             ASTContext &Context,
                                             const LambdaExpr *&Closure);

/// The values a view member of the object of the member function whose code
/// \p Read is in holds where \p Read reads it, in that function's flow
/// (thisMemberDefinitions), each what an assignment gives it; empty where
/// \p Read reads no view member of the object as a whole, the flow isn't
/// made, or no definition that reaches it is an assignment. What it holds
/// where the function is entered, or after a change that can't be told,
/// isn't one of them.
static llvm::SmallVector<const Expr *, 2>
assignedViewValues(const Expr *Read, ASTContext &Context) {
  llvm::SmallVector<const Expr *, 2> Values;
  if (!isa<MemberExpr>(Read) || !isDefaultViewType(Read->getType(), Context))
    return Values;
  const LambdaExpr *Closure = nullptr;
  const FunctionDecl *Function =
      executingFunction(Read, nullptr, Context, Closure);
  const CThisMember Member =
      Function ? thisMemberOf(Read, Function) : CThisMember{};
  if (Member.Path.empty() || Member.Element)
    return Values;
  const std::optional<llvm::SmallVector<CThisDefinition, 2>> Reaching =
      thisMemberDefinitions(Member, Read, Context);
  if (!Reaching)
    return Values;
  for (const CThisDefinition &Each : *Reaching)
    if (Each.Value && !llvm::is_contained(Values, Each.Value))
      Values.push_back(Each.Value);
  return Values;
}

CStorageRoots pointeeRootsOf(const Expr *E, ASTContext &Context) {
  if (!E)
    return CStorageRoots{{CStorageRoot{}}};
  return storageRootsOf(
      E, Context, E->isGLValue() && !E->getType()->isArrayType() ? 1 : 0);
}

CStorageRoots pointedAtRootsOf(const Expr *E, ASTContext &Context) {
  if (E && E->isGLValue() && !E->getType()->isPointerType() &&
      !E->getType()->isArrayType())
    return storageRootsOf(E, Context, 0);
  return pointeeRootsOf(E, Context);
}

CStorageRoots storageRootsOf(const Expr *E, ASTContext &Context,
                             unsigned Level) {
  if (stackNearlyExhausted())
    return withStack([&] { return storageRootsOf(E, Context, Level); });
  CStorageRoots Roots;
  if (!E)
    return Roots;
  CProvenance Walk(Context);
  // What a body that can't be seen gives is told below.
  Walk.setUnseenObjectPart(false);
  // What can't be told is anchored by the type of what \p E refers to.
  const std::optional<CAnchor> StartAnchor = referentAnchorAt(E, Level);
  // Each walk is of what a way reaches that is followed by itself, not deeper
  // in the walk that reaches it, which a chain of them would take past its
  // bound: what a local reference is bound to, what a copy of a view is a
  // copy of, what a view is made of. Each once at each level, with whether
  // the way to it took a part of what it refers to. Also of the storage an
  // owner is in, with the anchors of the owners on the way from it to the
  // borrow, which make what is found there Owned (OwnedBy), and of the
  // object of a member function whose returns can't be seen, for the owners
  // on the way only (PastBorrowed).
  struct CFollow {
    const Expr *E;
    unsigned Level;
    bool Part;
    llvm::SmallVector<CAnchor, 2> Anchors;
    bool PastBorrowed = false;
    llvm::SmallVector<CHop, 2> Hops;
  };
  llvm::SmallVector<CFollow, 8> Work{{E, Level, false, {}, false, {}}};
  // The anchors of each owner followed, which are Owned also when nothing
  // is found where it is, whether the way passed what isn't owned, and the
  // hops of the way.
  struct COwnerFollowed {
    llvm::SmallVector<CAnchor, 2> Anchors;
    bool PastBorrowed = false;
    llvm::SmallVector<CHop, 2> Hops;
  };
  llvm::SmallVector<COwnerFollowed, 2> OwnersFollowed;
  llvm::ArrayRef<CAnchor> Anchors;
  bool PastBorrowed = false;
  // The hops of the way being walked, the one nearest \p E first.
  llvm::ArrayRef<CHop> Hops;
  llvm::DenseSet<std::pair<const Expr *, unsigned>> Followed;
  // Of the level a way is at, whether it takes a part of the root.
  unsigned WalkLevel = 0;
  bool WalkPart = false;
  bool FollowedPart = false;
  auto PartAt = [&] {
    const llvm::ArrayRef<CProjection> Taken = Walk.projections();
    return FollowedPart || WalkPart ||
           (WalkLevel < Taken.size() &&
            (!Taken[WalkLevel].Path.empty() || Taken[WalkLevel].Element));
  };
  // Of the storage an owner is in, what it owns, Owned with the anchors and
  // where the outermost owner is; past what isn't owned, what is found there
  // only gives the owners on the way, and without them can't be told. What
  // can't be told is anchored by the type of what \p E refers to.
  auto Add = [&](CStorageRoot Root) {
    if (Root.Kind != ERootKind::Unknown)
      Root.bPart = Root.bPart || PartAt();
    if (PastBorrowed && Anchors.empty())
      Root = CStorageRoot{};
    if (!Anchors.empty()) {
      CStorageRoot Owned;
      Owned.Kind = ERootKind::Owned;
      Owned.Anchors.assign(Anchors.begin(), Anchors.end());
      Owned.Outer = Root.Kind;
      Owned.Variable = Root.Variable;
      Owned.Class = Root.Class;
      Owned.bPart = true;
      Owned.bPastBorrowed = PastBorrowed;
      Owned.Hops.assign(Hops.begin(), Hops.end());
      Root = std::move(Owned);
    }
    if (Root.Kind == ERootKind::Unknown && Root.Anchors.empty() && StartAnchor)
      Root.Anchors.push_back(*StartAnchor);
    if (!llvm::is_contained(Roots.Roots, Root))
      Roots.Roots.push_back(Root);
  };
  auto Unknown = [&] { Add(CStorageRoot{}); };
  // Past what isn't owned, \p Hop anchors what is found there too: the type
  // of the storage that can't be told.
  auto Follow = [&](const Expr *Next, unsigned NextLevel,
                    bool NextPastBorrowed = false,
                    std::optional<CAnchor> Hop = std::nullopt) {
    llvm::SmallVector<CAnchor, 2> NextAnchors(Anchors.begin(), Anchors.end());
    if (Hop && !llvm::is_contained(NextAnchors, *Hop))
      NextAnchors.push_back(*Hop);
    llvm::SmallVector<CHop, 2> NextHops(Hops.begin(), Hops.end());
    if (NextPastBorrowed)
      NextHops.push_back(CHop{});
    Work.push_back({Next, NextLevel, PartAt(), std::move(NextAnchors),
                    PastBorrowed || NextPastBorrowed, std::move(NextHops)});
  };
  // The hop of a dereference of an owner of the type \p Owner, what it owns
  // of \p Anchor, \p Past when the way from it passes what it doesn't own,
  // read where \p Read is, when told.
  auto OwnerHop = [&](QualType Owner, std::optional<CAnchor> Anchor,
                      bool Past, const Expr *Read = nullptr) {
    CHop Hop;
    Hop.Kind = CHop::EKind::Owner;
    Hop.OwnerKind = ownerKindOf(Owner, Context);
    Hop.Anchor = Anchor;
    Hop.bPast = Past;
    Hop.Owner = Read;
    return Hop;
  };
  // What the owner \p Owner, an owner or a pointer to one, owns: Owned, of
  // its anchor and those of the way here, where the storage it is in is,
  // followed by itself; not what a weak one refers to. \p Past when the way
  // from it passes what isn't owned, where \p Hop anchors what it reaches.
  auto OwnedBy = [&](const Expr *Owner, bool Past = false,
                     std::optional<CAnchor> Hop = std::nullopt) {
    const std::optional<CAnchor> Anchor =
        ownerAnchorOf(ownerTypeOf(Owner), Context);
    if (!Anchor) {
      Unknown();
      return;
    }
    llvm::SmallVector<CAnchor, 2> Next(Anchors.begin(), Anchors.end());
    if (!llvm::is_contained(Next, *Anchor))
      Next.push_back(*Anchor);
    if (Hop && !llvm::is_contained(Next, *Hop))
      Next.push_back(*Hop);
    llvm::SmallVector<CHop, 2> NextHops(Hops.begin(), Hops.end());
    NextHops.push_back(OwnerHop(ownerTypeOf(Owner), Anchor, Past, Owner));
    OwnersFollowed.push_back({Next, PastBorrowed || Past, NextHops});
    Work.push_back(
        {Owner,
         Owner->isGLValue() && Owner->getType()->isPointerType() ? 1u : 0u,
         false, std::move(Next), PastBorrowed || Past, std::move(NextHops)});
  };
  while (!Work.empty()) {
    const CFollow Each = Work.pop_back_val();
    if (!Followed.insert({Each.E, Each.Level}).second)
      continue;
    Anchors = Each.Anchors;
    PastBorrowed = Each.PastBorrowed;
    Hops = Each.Hops;
    if (Followed.size() > 1024) {
      Unknown();
      break;
    }
    FollowedPart = Each.Part;
    bool Ended = false;
    const bool Complete = Walk.reach(
        Each.E, Each.Level,
        [&](const Expr *At, unsigned AtLevel, bool Part) {
          WalkLevel = AtLevel;
          WalkPart = Part;
          const Expr *Inner = At->IgnoreParens();
          // A cast that checks the class of what a pointer points to gives
          // null where it isn't of it.
          if (const auto *Checked = dyn_cast<CXXDynamicCastExpr>(Inner);
              Checked && AtLevel == 0 &&
              Checked->getCastKind() == CK_Dynamic &&
              Checked->getType()->isPointerType())
            Roots.Null = true;
          const llvm::ArrayRef<CProjection> Taken = Walk.projections();
          // What a copy or a move assignment the compiler defines gives its
          // object above its storage, what it copies, which the way goes on
          // to (copiedByDefinedAssignment).
          if (AtLevel > 0 && copiedByDefinedAssignment(Inner, Context))
            return EReach::Continue;
          // What a member function gives by reference or pointer when a body
          // the call can run can't be seen (calledRoots), which can be
          // anything of its object or elsewhere; but of an owner
          // (OwnerTypes), what it owns, by its declared kind,
          // `pJob->m_Result`: of each owner variable the object is. Past it,
          // and of another object, the owners the object is reached through
          // anchor it, `pContext->f_GetJob()->m_Value`. What each body that
          // can be seen gives is followed too (step), each override in the
          // translation unit.
          const CMemberCall Call = memberCallOf(Inner, /*Operators=*/true);
          const CCalledRoots Called =
              Call && Call->getMethodDecl() &&
                      (Call->getType()->isPointerType() || Call->isGLValue())
                  ? Walk.calledRoots(Call, Call->getMethodDecl())
                  : CCalledRoots{{}, false, false};
          if (Call && Call->getMethodDecl() &&
              (Call->getType()->isPointerType() || Call->isGLValue()) &&
              Called.open()) {
            const Expr *Object = Call->getImplicitObjectArgument();
            const EOwnerKind Kind =
                Object ? ownerKindOf(Call->isArrow() ? ownerTypeOf(Object)
                                                     : Object->getType(),
                                     Context)
                       : EOwnerKind::None;
            if (Kind != EOwnerKind::None && Kind != EOwnerKind::Weak) {
              OwnedBy(Object, /*Past=*/AtLevel > 0,
                      AtLevel > 0 ? referentAnchorAt(Inner, AtLevel)
                                  : std::nullopt);
              return EReach::Root;
            }
            if (Object)
              Follow(Object, 0, /*NextPastBorrowed=*/true,
                     referentAnchorAt(Inner, AtLevel));
            else
              Unknown();
            return Called.Bodies.empty() ? EReach::Root : EReach::Continue;
          }
          // A temporary, by the language's rule of extension: the storage of
          // the declaration that extends it, also the array of a
          // `std::initializer_list` of the local it initializes; else, also
          // of an init capture, which extends nothing past the closure that
          // is made in the full expression, it ends with its full expression.
          if (const auto *Temporary = dyn_cast<MaterializeTemporaryExpr>(Inner);
              Temporary && AtLevel == 0) {
            if (const auto *Extending =
                    dyn_cast_or_null<VarDecl>(Temporary->getExtendingDecl());
                Extending && !Extending->isInitCapture())
              Add(variableRoot(Extending));
            else {
              CStorageRoot Root = rootOf(ERootKind::Temporary);
              Root.Made = Temporary;
              Add(Root);
            }
            return EReach::Root;
          }
          // What refers to nothing adds no root, a null pointer in any
          // spelling; static storage is a static root (leafOf).
          if (refersToNothing(Inner, AtLevel)) {
            if (AtLevel == 0 && leafOf(Inner) == ELeaf::Nothing)
              Roots.Null = true;
            return EReach::Root;
          }
          if (leafOf(Inner) == ELeaf::Static) {
            Add(rootOf(ERootKind::Static));
            return EReach::Root;
          }
          // A copy or a move of what shares what it points to with its source,
          // a view or an aggregate of pointers the compiler copies member by
          // member (copySharesPointees), refers to what its source does.
          if (const auto *Copy = dyn_cast<CXXConstructExpr>(Inner);
              Copy && AtLevel > 0 && Copy->getNumArgs() >= 1 &&
              Copy->getConstructor()->isCopyOrMoveConstructor() &&
              copySharesPointees(Copy->getType(), Context)) {
            Follow(Copy->getArg(0), AtLevel);
            return EReach::Root;
          }
          // A view made of a pointer and a length, or of another view, by a
          // constructor, also one whose body can't be seen, refers to what
          // they do, `CStrSpan Name(pName, 8)`: each pointer a level down,
          // each view at the level; not a size.
          // Also an aggregate view, `CAggregateStrSpan{"download", 8}`.
          llvm::ArrayRef<const Expr *> MadeOf;
          if (const auto *Construct = dyn_cast<CXXConstructExpr>(Inner);
              Construct && makesView(Construct, Context))
            MadeOf =
                llvm::ArrayRef(Construct->getArgs(), Construct->getNumArgs());
          else if (const auto *List = dyn_cast<InitListExpr>(Inner);
                   List && isDefaultViewType(List->getType(), Context))
            MadeOf = llvm::ArrayRef(List->getInits(), List->getNumInits());
          if (AtLevel > 0 && !MadeOf.empty()) {
            for (const Expr *Argument : MadeOf) {
              // Not what can't hold a pointer, a reference or a view, a size
              // or a tag, `CInitByRange{}` (holdsReference).
              if (isa<CXXDefaultArgExpr>(Argument) ||
                  !holdsReference(Argument->getType()))
                continue;
              // Of an owner of a buffer, `CStrPtr(Text)` of a `CStr`, the
              // buffer, what it owns.
              const bool Pointer = Argument->getType()->isPointerType();
              if (!Pointer &&
                  !isDefaultViewType(Argument->getType(), Context) &&
                  !ownsBuffer(ownerKindOf(Argument->getType(), Context))) {
                Unknown();
                continue;
              }
              Follow(Argument,
                     Pointer && Argument->isPRValue() ? AtLevel - 1 : AtLevel);
            }
            return EReach::Root;
          }
          // What an owner owns, at the level of its value, past the members
          // of it at that level and the storage what it owns owns (OwnedBy):
          // of an owner value, `CArguments{pJob, ...}`, of a member,
          // `mp_pJob` or `Holder.m_pJob`, of what a call gives, and a level
          // down of a pointer to one, `ppJob` of `ppJob->f_Get()`; a local or
          // a parameter below. Past what that only refers to, the owner only
          // anchors it, `pContext->m_pRawJob->m_Value`.
          // The first member on the way from the borrow that is a pointer or
          // a reference that isn't owned, whose referent anchors it; none
          // when the owner owns the way.
          auto HopPast = [&](unsigned FirstLevel) -> std::optional<CAnchor> {
            for (unsigned iLevel = FirstLevel; iLevel < AtLevel; ++iLevel)
              if (iLevel < Taken.size() && !Taken[iLevel].Path.empty() &&
                  isBorrowedField(Taken[iLevel].Path.back())) {
                const QualType Field = Taken[iLevel].Path.back()->getType();
                return objectAnchorOf(Field->isPointerType()
                                          ? Field->getPointeeType()
                                          : Field.getNonReferenceType());
              }
            return std::nullopt;
          };
          if (AtLevel > 1 && Inner->getType()->isPointerType() &&
              ownerKindOf(ownerTypeOf(Inner), Context) != EOwnerKind::None) {
            const std::optional<CAnchor> Hop = HopPast(2);
            OwnedBy(Inner, /*Past=*/Hop.has_value(), Hop);
            return EReach::Root;
          }
          if (AtLevel > 0 && !isa<DeclRefExpr>(Inner) &&
              ownerKindOf(Inner->getType(), Context) != EOwnerKind::None) {
            const std::optional<CAnchor> Hop = HopPast(1);
            OwnedBy(Inner, /*Past=*/Hop.has_value(), Hop);
            return EReach::Root;
          }
          // A static member named through an object, `Digest.mc_Size`, is that
          // static.
          if (const auto *Member = dyn_cast<MemberExpr>(Inner)) {
            if (const auto *Static =
                    dyn_cast<VarDecl>(Member->getMemberDecl())) {
              if (AtLevel == 0)
                Add(variableRoot(Static));
              else
                Unknown();
              return EReach::Root;
            }
          }
          // The object, of the class \p Class, `this` or the explicit object
          // parameter \p Object that refers to it: at the level of its
          // storage, the object; what a pointer member of it points to,
          // `mp_pConfig`, where each value it is given does.
          auto ObjectReached = [&](const CXXRecordDecl *Class,
                                   const ParmVarDecl *Object) {
            if (AtLevel == 0) {
              Add(objectRoot(Class, Object));
              return EReach::Root;
            }
            if (AtLevel == 1 && Taken.size() > 1 && !Taken[1].Element &&
                Taken[1].Path.size() == 1 &&
                Taken[1].Path.back()->getType()->isPointerType()) {
              for (const CStorageRoot &Root :
                   memberValueRoots(Taken[1].Path.back(), Context).Roots)
                Add(Root);
              return EReach::Root;
            }
            Unknown();
            return EReach::Root;
          };
          // Of a record that isn't a view nor an owner (recordPartAt), at its
          // name, a member of it, also through a pointer to it, a structured
          // binding of a member of it, or a getter that returns a reference
          // member of it: what a pointer, reference or view member stored in
          // it refers to, and a copy of one held by value, nothing; of one
          // held by value its own storage, the frame's, and what an owner
          // member owns, Owned of its anchor where the record is. Else none.
          auto RecordReached = [&]() -> std::optional<EReach> {
            switch (const CRecordPart Reached =
                        recordPartAt(Inner, AtLevel, Walk, Context);
                    Reached.Kind) {
            case ERecordPart::Followed:
              return std::nullopt;
            case ERecordPart::Value:
              return EReach::Root;
            // Where each value its class gives it refers to, where that is
            // all it is given and the level is that of what it refers to;
            // else what can't be told.
            case ERecordPart::Assigned:
              for (const CStorageRoot &Root :
                   memberValueRoots(Reached.Field, Context).Roots)
                Add(Root);
              return EReach::Root;
            case ERecordPart::Own:
            case ERecordPart::Owned: {
              CStorageRoot Root;
              Root.Kind = ERootKind::Frame;
              Root.Variable = Reached.Record;
              if (Reached.Kind == ERecordPart::Own) {
                Add(Root);
                return EReach::Root;
              }
              const std::optional<CAnchor> Anchor = ownerAnchorOf(
                  Context.getBaseElementType(Reached.Owner->getType()),
                  Context);
              if (!Anchor) {
                Unknown();
                return EReach::Root;
              }
              const std::optional<CAnchor> Hop = HopPast(1);
              llvm::SmallVector<CAnchor, 2> Owning(Anchors.begin(),
                                                   Anchors.end());
              for (const std::optional<CAnchor> &Extra : {Anchor, Hop})
                if (Extra && !llvm::is_contained(Owning, *Extra))
                  Owning.push_back(*Extra);
              llvm::SmallVector<CHop, 2> OwningHops(Hops.begin(), Hops.end());
              OwningHops.push_back(
                  OwnerHop(Context.getBaseElementType(Reached.Owner->getType()),
                           Anchor, Hop.has_value()));
              const llvm::ArrayRef<CAnchor> Before = Anchors;
              const bool BeforePast = PastBorrowed;
              const llvm::ArrayRef<CHop> BeforeHops = Hops;
              Anchors = Owning;
              PastBorrowed = PastBorrowed || Hop.has_value();
              Hops = OwningHops;
              Add(Root);
              Anchors = Before;
              PastBorrowed = BeforePast;
              Hops = BeforeHops;
              return EReach::Root;
            }
            }
            return std::nullopt;
          };
          // A raw pointer member whose target its class doesn't state
          // (targetToldByOwnClass), loaded from a local record held by
          // value, of the members the way takes of it, whose type isn't an
          // owner and has a destructor of its own, made by a constructor of
          // its class or what else can't be told, `Values.mp_pFirst` or
          // `Values.f_GetFirst()` of a `TCList`: what it points to can be
          // storage the record manages, which ends with it
          // (CStorageRoot::Record).
          if (const auto *Name = dyn_cast<DeclRefExpr>(Inner);
              Name && AtLevel == 1 && Taken.size() > 1 &&
              !Taken[1].Element && !Taken[1].Path.empty()) {
            const auto *Record = dyn_cast<VarDecl>(Name->getDecl());
            const auto *Class =
                Record ? Record->getType()->getAsCXXRecordDecl() : nullptr;
            const CXXDestructorDecl *Destructor =
                Class && Class->hasDefinition() ? Class->getDestructor()
                                                : nullptr;
            const FieldDecl *Field = Taken[1].Path.back();
            const bool ByValue = llvm::all_of(
                llvm::ArrayRef(Taken[1].Path).drop_back(),
                [](const FieldDecl *Through) {
                  return !Through->getType()->isPointerType() &&
                         !Through->getType()->isReferenceType();
                });
            // Made by a constructor the compiler defines, other than a copy
            // or a move, or by a list of its members, its members are what
            // that gives them, which the provenance follows.
            const Expr *Init =
                Record ? sameValue(Record->getInit(), Context) : nullptr;
            const auto *Construct = dyn_cast_or_null<CXXConstructExpr>(Init);
            const CXXConstructorDecl *Constructor =
                Construct ? Construct->getConstructor() : nullptr;
            const bool Defined =
                isa_and_nonnull<InitListExpr>(Init) ||
                (Constructor && !Constructor->isUserProvided() &&
                 !Constructor->isCopyOrMoveConstructor());
            if (ByValue && !Defined && Field->getType()->isPointerType() &&
                !targetToldByOwnClass(Field) &&
                isOpaqueRecord(Record, Context) && !isa<ParmVarDecl>(Record) &&
                Destructor && Destructor->isUserProvided()) {
              CStorageRoot Managed;
              Managed.Member = Field;
              Managed.Record = Record;
              Add(Managed);
            }
          }
          switch (effectsOf(Inner, Context).Statement) {
          case EStatement::This: {
            const QualType Pointee = Inner->getType()->getPointeeType();
            return ObjectReached(
                Pointee.isNull() ? nullptr : Pointee->getAsCXXRecordDecl(),
                nullptr);
          }
          case EStatement::New:
            if (AtLevel == 0) {
              Unknown();
              return EReach::Root;
            }
            return EReach::Continue;
          case EStatement::Name:
            break;
          case EStatement::Member:
            // A view member of the object read as a whole, above its
            // storage: what each assignment of it that reaches the read in
            // its function's flow refers to (assignedViewValues), then what
            // else it can hold, as of another record.
            if (AtLevel > 0 && (AtLevel >= Taken.size() ||
                                (Taken[AtLevel].Path.empty() &&
                                 !Taken[AtLevel].Element)))
              for (const Expr *Value : assignedViewValues(Inner, Context))
                Follow(Value, AtLevel);
            return RecordReached().value_or(EReach::Continue);
          case EStatement::Call:
            return RecordReached().value_or(EReach::Continue);
          default:
            return EReach::Continue;
          }
          const ValueDecl *Named = cast<DeclRefExpr>(Inner)->getDecl();
          const auto *Variable = dyn_cast<VarDecl>(Named);
          if (!Variable && !isa<BindingDecl>(Named))
            return EReach::Continue;
          const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Variable);
          const QualType Type = Named->getType();
          // The explicit object parameter, by reference, is the object; by
          // value it is a copy in the frame, as another parameter.
          if (Parameter && Parameter->isExplicitObjectParameter() &&
              Type->isReferenceType())
            return ObjectReached(
                Type.getNonReferenceType()->getAsCXXRecordDecl(), Parameter);
          // What an owner owns, at the level of its value: past the members of
          // it at that level and the storage what it owns owns, not past what
          // that only refers to.
          if (Variable && AtLevel > 0 &&
              ownerKindOf(Type, Context) != EOwnerKind::None &&
              (Variable->hasLocalStorage() || Parameter)) {
            const std::optional<CAnchor> Hop = HopPast(1);
            OwnedBy(Inner, /*Past=*/Hop.has_value(), Hop);
            return EReach::Root;
          }
          if (const std::optional<EReach> Reached = RecordReached())
            return *Reached;
          // A structured binding is what it binds (reach).
          if (!Variable)
            return EReach::Continue;
          // A reference is what it is bound to, not what is made of itself; of
          // a parameter, what it refers to.
          if (Type->isReferenceType()) {
            if (!Parameter && Variable->getInit() &&
                boundToItself(Variable, Context)) {
              Unknown();
              return EReach::Root;
            }
            // Bound once, what it designates is followed by itself; what is
            // loaded through it is read where the way reads it.
            if (!Parameter) {
              if (AtLevel > 0 || !Variable->getInit() ||
                  Variable->isInitCapture() || !Variable->hasLocalStorage())
                return EReach::Continue;
              Follow(Variable->getInit(), AtLevel);
              return EReach::Root;
            }
            if (AtLevel == 0)
              Add(parameterRoot(
                  Parameter, Type.getNonReferenceType()->getAsCXXRecordDecl(),
                  Inner, Context));
            else
              Unknown();
            return EReach::Root;
          }
          // Its own storage, by its storage duration; what a variable of
          // static or thread storage holds can be anything.
          if (AtLevel == 0) {
            Add(variableRoot(Variable));
            return EReach::Root;
          }
          if (Variable->hasGlobalStorage()) {
            Unknown();
            return EReach::Root;
          }
          // What a pointer parameter held by value points to; what a local
          // holds is followed by what it is given.
          if (Parameter) {
            Add(parameterRoot(Parameter,
                              AtLevel == 1 && Type->isPointerType()
                                  ? Type->getPointeeType()->getAsCXXRecordDecl()
                                  : nullptr,
                              Inner, Context));
            return EReach::Root;
          }
          return EReach::Continue;
        },
        &Ended);
    if (!Complete || Ended)
      Unknown();
  }
  // What an owner owns is Owned also where nothing is found of the storage it
  // is in.
  Anchors = {};
  Hops = {};
  for (const COwnerFollowed &Owner : OwnersFollowed)
    if (llvm::none_of(Roots.Roots, [&](const CStorageRoot &Root) {
          return Root.Kind == ERootKind::Owned &&
                 llvm::all_of(Owner.Anchors, [&](const CAnchor &Anchor) {
                   return llvm::is_contained(Root.Anchors, Anchor);
                 });
        })) {
      CStorageRoot Owned;
      Owned.Kind = ERootKind::Owned;
      Owned.Anchors = Owner.Anchors;
      Owned.bPart = true;
      Owned.bPastBorrowed = Owner.PastBorrowed;
      Owned.Hops = Owner.Hops;
      Roots.Roots.push_back(std::move(Owned));
    }
  return Roots;
}

llvm::SmallVector<const CXXMethodDecl *, 2>
runningOperators(const LambdaExpr *Closure) {
  llvm::SmallVector<const CXXMethodDecl *, 2> Operators;
  const CXXMethodDecl *Operator = Closure->getCallOperator();
  if (const FunctionTemplateDecl *Template =
          Operator->getDescribedFunctionTemplate()) {
    for (const FunctionDecl *Specialization : Template->specializations())
      if (const auto *Method = dyn_cast<CXXMethodDecl>(Specialization))
        Operators.push_back(Method);
  } else
    Operators.push_back(Operator);
  return Operators;
}

/// The type of what an owner of \p Owner owns, its first type argument;
/// null when that can't be told, of an owner that isn't a template.
static QualType ownedTypeOf(QualType Owner) {
  if (Owner.isNull())
    return {};
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(
          Owner.getNonReferenceType()->getAsCXXRecordDecl());
  if (!Specialization || Specialization->getTemplateArgs().size() == 0 ||
      Specialization->getTemplateArgs()[0].getKind() != TemplateArgument::Type)
    return {};
  return Specialization->getTemplateArgs()[0].getAsType();
}

CAnchor objectAnchorOf(QualType Object) {
  return {Object.getNonReferenceType()
              .getCanonicalType()
              .getUnqualifiedType()
              .getTypePtr()};
}

std::optional<CAnchor> ownerAnchorOf(QualType Owner,
                                     const ASTContext &Context) {
  const EOwnerKind Kind = ownerKindOf(Owner, Context);
  if (Kind == EOwnerKind::None || Kind == EOwnerKind::Weak)
    return std::nullopt;
  if (Kind == EOwnerKind::Shared || Kind == EOwnerKind::Unique)
    if (const QualType Owned = ownedTypeOf(Owner); !Owned.isNull())
      return objectAnchorOf(Owned);
  return objectAnchorOf(Owner);
}

namespace {
/// Of each pair of classes, whether the second is a base of the first
/// (isBaseOf), once told.
struct CBaseRelations {
  llvm::DenseMap<std::pair<const CXXRecordDecl *, const CXXRecordDecl *>, bool>
      Of;
};
} // namespace

/// Whether \p Base is a base of \p Derived, direct or not, virtual or not:
/// a work stack over the classes of the bases, each once, so a hierarchy
/// that reaches a base through many paths is gone through once per class;
/// kept per pair.
static bool isBaseOf(const CXXRecordDecl *Base, const CXXRecordDecl *Derived,
                     const ASTContext &Context) {
  CBaseRelations &Cache = astCache<CBaseRelations>(Context);
  const auto Key = std::make_pair(Derived, Base);
  if (auto It = Cache.Of.find(Key); It != Cache.Of.end())
    return It->second;
  bool Found = false;
  llvm::SmallPtrSet<const CXXRecordDecl *, 16> Visited{Derived};
  llvm::SmallVector<const CXXRecordDecl *, 16> Work{Derived};
  while (!Work.empty() && !Found) {
    const CXXRecordDecl *Class = Work.pop_back_val();
    for (const CXXBaseSpecifier &Specifier : Class->bases()) {
      const CXXRecordDecl *Of = Specifier.getType()->getAsCXXRecordDecl();
      Of = Of ? Of->getDefinition() : nullptr;
      if (!Of)
        continue;
      if (Of == Base) {
        Found = true;
        break;
      }
      if (Visited.insert(Of).second)
        Work.push_back(Of);
    }
  }
  Cache.Of[Key] = Found;
  return Found;
}

/// Whether a keeper of the kind \p Kind whose anchor is \p Keeper
/// (ownerAnchorOf) fits \p Anchor, the one relation of it (holdsKeeper): the
/// same type; of a `shared` or `unique` one, also a base of it or a class
/// derived from it (isBaseOf).
static bool fits(EOwnerKind Kind, const CAnchor &Keeper, const CAnchor &Anchor,
                 const ASTContext &Context) {
  if (!Keeper.Of || !Anchor.Of)
    return false;
  if (Keeper.Of == Anchor.Of)
    return true;
  if (Kind != EOwnerKind::Shared && Kind != EOwnerKind::Unique)
    return false;
  const CXXRecordDecl *Kept = Keeper.Of->getAsCXXRecordDecl();
  const CXXRecordDecl *Of = Anchor.Of->getAsCXXRecordDecl();
  Kept = Kept ? Kept->getDefinition() : nullptr;
  Of = Of ? Of->getDefinition() : nullptr;
  return Kept && Of &&
         (isBaseOf(Of, Kept, Context) || isBaseOf(Kept, Of, Context));
}

namespace {
/// Of each type, the owners that are by-value parts of a value of it
/// (keepersOf), once told.
struct CKeepers {
  llvm::DenseMap<const Type *,
                 llvm::SmallVector<std::pair<EOwnerKind, CAnchor>, 2>>
      Of;
};
} // namespace

/// The owners, not weak, that a value of \p Type holds by value, each with
/// its kind and anchor: the value itself, members and bases of a record, the
/// elements of an array, and of a container (holdsOwnerElements) the
/// arguments of its template, past those, not past what an owner of the
/// `shared` or `unique` kind owns, which isn't held by value. Each by its
/// canonical type (ownerKindOf). A work stack over the types,
/// each once, at most 256.
static llvm::ArrayRef<std::pair<EOwnerKind, CAnchor>>
keepersOf(QualType Type, const ASTContext &Context) {
  Type = Type.getNonReferenceType().getCanonicalType().getUnqualifiedType();
  CKeepers &Cache = astCache<CKeepers>(Context);
  if (auto It = Cache.Of.find(Type.getTypePtr()); It != Cache.Of.end())
    return It->second;
  llvm::SmallVector<std::pair<EOwnerKind, CAnchor>, 2> Keepers;
  llvm::SmallVector<QualType, 8> Work{Type};
  llvm::SmallPtrSet<const clang::Type *, 16> Seen;
  while (!Work.empty() && Seen.size() < 256) {
    const QualType Each =
        Work.pop_back_val().getCanonicalType().getUnqualifiedType();
    if (Each.isNull() || Each->isReferenceType() || Each->isDependentType() ||
        !Seen.insert(Each.getTypePtr()).second)
      continue;
    if (const ArrayType *Array = Each->getAsArrayTypeUnsafe()) {
      Work.push_back(Array->getElementType());
      continue;
    }
    if (!holdsOwners(Each, Context))
      continue;
    const EOwnerKind Kind = ownerKindOf(Each, Context);
    if (const std::optional<CAnchor> Anchor = ownerAnchorOf(Each, Context)) {
      const std::pair<EOwnerKind, CAnchor> Keeper{Kind, *Anchor};
      if (!llvm::is_contained(Keepers, Keeper))
        Keepers.push_back(Keeper);
    }
    if (Kind == EOwnerKind::Shared || Kind == EOwnerKind::Unique)
      continue;
    const auto *Class = Each->getAsCXXRecordDecl();
    const CXXRecordDecl *Definition = Class ? Class->getDefinition() : nullptr;
    if (Kind == EOwnerKind::None && Definition) {
      for (const CXXBaseSpecifier &Base : Definition->bases())
        Work.push_back(Base.getType());
      for (const FieldDecl *Field : Definition->fields())
        Work.push_back(Field->getType());
    }
    if (const auto *Specialization =
            dyn_cast_or_null<ClassTemplateSpecializationDecl>(Class);
        Specialization && holdsOwnerElements(Each, Context)) {
      llvm::SmallVector<TemplateArgument, 4> Arguments(
          Specialization->getTemplateArgs().asArray());
      for (size_t iArgument = 0; iArgument < Arguments.size(); ++iArgument)
        if (Arguments[iArgument].getKind() == TemplateArgument::Type)
          Work.push_back(Arguments[iArgument].getAsType());
        else if (Arguments[iArgument].getKind() == TemplateArgument::Pack)
          llvm::append_range(Arguments, Arguments[iArgument].pack_elements());
    }
  }
  return Cache.Of[Type.getTypePtr()] = std::move(Keepers);
}

/// Whether an owner of the kind \p Kind keeps what it owns as it is taken
/// by \p Take.
static bool takeKeeps(EOwnerKind Kind, ETake Take) {
  switch (Take) {
  case ETake::Copy:
    return copyKeepsOwned(Kind);
  case ETake::Move:
  case ETake::Held:
    return Kind != EOwnerKind::None && Kind != EOwnerKind::Weak;
  }
  return false;
}

namespace {
/// Where the scopes of locals are (inScopeAt): of each local, the scope
/// statement it is declared in and the item of it that declares it; of each
/// place, the item of each statement around it that it is in.
struct CScopes {
  llvm::DenseMap<const VarDecl *, std::pair<const Stmt *, const Stmt *>>
      Declared;
  llvm::DenseMap<const Stmt *, llvm::DenseMap<const Stmt *, const Stmt *>>
      Around;
};
} // namespace

/// Whether the local \p Local is in scope at \p Where, lexically: the
/// statement that declares it, past the labels it is under, is an item of a
/// scope \p Where is in, and, with \p Ordered, begins before the item of
/// that scope \p Where is in, not in its own declaration; the variable of a
/// handler in the handler. By the instantiated parents, each local and each
/// place once.
static bool inScopeAt(const VarDecl *Local, const Stmt *Where, bool Ordered,
                      ASTContext &Context) {
  if (!Local || !Where)
    return false;
  CScopes &Scopes = astCache<CScopes>(Context);
  auto [Declared, DeclaredNew] = Scopes.Declared.try_emplace(Local);
  if (DeclaredNew) {
    const Stmt *Item = nullptr;
    const Stmt *Scope = nullptr;
    DynTypedNode Node = DynTypedNode::create(*Local);
    for (int Step = 0; Step < 64; ++Step) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty())
        break;
      const Stmt *Parent = Parents[0].get<Stmt>();
      if (!Parent) {
        if (!Parents[0].get<Decl>() || Parents[0].get<FunctionDecl>())
          break;
        Node = Parents[0];
        continue;
      }
      if (isa<CXXCatchStmt>(Parent) && !Item) {
        Scope = Parent;
        break;
      }
      if (!Item || isa<SwitchCase, LabelStmt, AttributedStmt>(Parent)) {
        Item = Parent;
        Node = Parents[0];
        continue;
      }
      Scope = Parent;
      break;
    }
    Declared->second = {Scope, Item};
  }
  const auto [Scope, Item] = Declared->second;
  if (!Scope)
    return false;
  auto [Around, AroundNew] = Scopes.Around.try_emplace(Where);
  if (AroundNew) {
    DynTypedNode Node = DynTypedNode::create(*Where);
    const Stmt *Below = Where;
    for (int Step = 0; Step < 4096; ++Step) {
      const auto Parents = instantiatedParents(Node, Context);
      if (Parents.empty() || Parents[0].get<FunctionDecl>())
        break;
      if (const Stmt *Parent = Parents[0].get<Stmt>()) {
        Around->second.try_emplace(Parent, Below);
        Below = Parent;
      }
      Node = Parents[0];
    }
  }
  const auto Below = Around->second.find(Scope);

  if (Below == Around->second.end())
    return false;
  if (!Item || !Ordered)
    return true;
  // In the initializer of a later declarator of the same declaration,
  // `TCSharedPointer<CJob> pKeep(this), pSelected = ...;`.
  if (Below->second == Item)
    if (const auto *Declaration = dyn_cast<DeclStmt>(Item)) {
      bool bAfter = false;
      for (const Decl *Each : Declaration->decls()) {
        const auto *Variable = dyn_cast<VarDecl>(Each);
        if (bAfter && Variable && Variable->getInit() &&
            Context.getSourceManager().isPointWithin(
                Where->getBeginLoc(), Variable->getInit()->getBeginLoc(),
                Variable->getInit()->getEndLoc()))
          return true;
        if (Each == Local)
          bAfter = true;
      }
      return false;
    }
  return Below->second != Item &&
         Context.getSourceManager().isBeforeInTranslationUnit(
             Item->getBeginLoc(), Below->second->getBeginLoc());
}

namespace {
/// Where in a value of a type an owner is held by value (keeperPathsOf): the
/// members on the way, the outermost first, and whether it is an element of
/// what they reach, of an array or a container, past which no member is
/// told (Deep).
struct CKeeperPath {
  llvm::SmallVector<const FieldDecl *, 2> Fields;
  bool Element = false;
  bool Deep = false;
};

/// What an owner value is in the flow (ownerValueOf): what the local slot
/// Slot holds where At is, or the member of the object Member, or else the
/// expression Leaf.
struct COwnerValue {
  CSlot Slot;
  const Stmt *At = nullptr;
  const Expr *Leaf = nullptr;
  CThisMember Member;
  /// Of a Leaf that makes a record, its member this is (madeMemberValues).
  const FieldDecl *Part = nullptr;
  /// The constructions whose constructors' parameters are what each gives
  /// them, of a value in the body of the last one's, the one it is made in
  /// before it.
  llvm::SmallVector<const CXXConstructExpr *, 1> Sites;
  /// Of an element of Slot, the index or key that designates it where At is
  /// (elementIndexOf); null where none is told.
  const Expr *Index = nullptr;
  /// Of Slot, whether it is what it holds after a call that can change it,
  /// whose value can't be told (calledValues).
  bool Changed = false;
  /// Of a Leaf, whether this is a part or an element of what it gives, not
  /// the whole.
  bool Inside = false;
};

/// The reference parameter when \p Slot is what it refers to, or a part of it
/// (ownerValueOf), which isn't a local slot: only its writes through its
/// names are told; null for another slot.
static const ParmVarDecl *referentOf(const CSlot &Slot) {
  const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Slot.Variable);
  return Parameter && Parameter->getType()->isReferenceType() ? Parameter
                                                              : nullptr;
}

/// Whether \p Slot is what a reference parameter refers to (referentOf).
static bool isReferent(const CSlot &Slot) { return referentOf(Slot); }

/// What a leaf of an owner value is (leafKindOf).




enum class EOwnerLeaf {
  /// Empty: made empty, by its default, of a null pointer, `{}`.
  Empty,
  /// A new object, made here: a construction or a `new`.
  Made,
  /// What a call gives back.
  Called,
  /// What a parameter held by value is given (Parameter).
  Given,
  /// What can't be told: a name of what isn't a local slot, a value whose
  /// definitions can't be told.
  Untold,
};

/// Whether a keeper owns what a borrow is derived through (keeperOwns).
enum class EOwns { Same, Other, Untold };

/// The ways from the owner a borrow is derived through to where it is made
/// (ownerLine): each slot the value is in, where, by the sole definition
/// that reaches each, and the leaf it ends at.
struct COwnerLine {
  llvm::SmallVector<std::pair<CSlot, const Stmt *>, 4> Slots;
  /// Of each of Slots, the index of the element it is (COwnerValue::Index).
  llvm::SmallVector<const Expr *, 4> Indices;
  /// The member of the object it ends at, and where it is read there.
  std::optional<std::pair<CThisMember, const Stmt *>> Member;
  /// Of a borrow into the object itself, the object root it is of
  /// (madeFromObject), instead of an owner.
  const CStorageRoot *Object = nullptr;
  /// Of a borrow into what a parameter refers to, of a function one of whose
  /// parameters is DMibDistinct, that parameter, instead of an owner.
  const ParmVarDecl *Referent = nullptr;
  EOwnerLeaf Leaf = EOwnerLeaf::Untold;
  const Expr *LeafExpr = nullptr;
  const VarDecl *Parameter = nullptr;
};
} // namespace

/// The places in a value of \p Type of each owner of the kind \p Kind and
/// the anchor \p Anchor it holds by value, as keepersOf finds them: itself,
/// the members and bases of a record, an element of an array or of a
/// container. A work stack, at most 256 steps.
static llvm::SmallVector<CKeeperPath, 1>
keeperPathsOf(QualType Type, EOwnerKind Kind, const CAnchor &Anchor,
              const ASTContext &Context) {
  llvm::SmallVector<CKeeperPath, 1> Paths;
  llvm::SmallVector<std::pair<QualType, CKeeperPath>, 8> Work{{Type, {}}};
  for (int Step = 0; !Work.empty() && Step < 256; ++Step) {
    auto [Each, Path] = Work.pop_back_val();
    Each = Each.getNonReferenceType().getCanonicalType().getUnqualifiedType();
    if (Each.isNull() || Each->isDependentType())
      continue;
    if (const ArrayType *Array = Each->getAsArrayTypeUnsafe()) {
      CKeeperPath Next = Path;
      Next.Deep = Next.Deep || Next.Element;
      Next.Element = true;
      Work.push_back({Array->getElementType(), Next});
      continue;
    }
    if (!holdsOwners(Each, Context))
      continue;
    const EOwnerKind EachKind = ownerKindOf(Each, Context);
    if (EachKind == Kind)
      if (const std::optional<CAnchor> Own = ownerAnchorOf(Each, Context);
          Own && *Own == Anchor) {
        Paths.push_back(Path);
        continue;
      }
    if (EachKind == EOwnerKind::Shared || EachKind == EOwnerKind::Unique)
      continue;
    const auto *Class = Each->getAsCXXRecordDecl();
    const CXXRecordDecl *Definition = Class ? Class->getDefinition() : nullptr;
    if (EachKind == EOwnerKind::None && Definition) {
      for (const CXXBaseSpecifier &Base : Definition->bases())
        Work.push_back({Base.getType(), Path});
      for (const FieldDecl *Field : Definition->fields()) {
        CKeeperPath Next = Path;
        Next.Deep = Next.Deep || Next.Element;
        Next.Fields.push_back(Field);
        Work.push_back({Field->getType(), Next});
      }
    }
    // An element by the arguments of the template, where no member of the
    // definition holds one by value, which its own path is
    // (`t_CType m_Elements[t_Size];`), also in a member, a base or an array
    // of one. A work stack, at most 64 steps.
    auto HeldByMember = [&](QualType Argument) {
      const QualType Element =
          Argument.getCanonicalType().getUnqualifiedType();
      llvm::SmallVector<const CXXRecordDecl *, 4> Records{Definition};
      for (int iRecord = 0; !Records.empty() && iRecord < 64; ++iRecord) {
        const CXXRecordDecl *Record = Records.pop_back_val();
        for (const CXXBaseSpecifier &Base : Record->bases())
          if (const CXXRecordDecl *Of = Base.getType()->getAsCXXRecordDecl();
              Of && Of->getDefinition())
            Records.push_back(Of->getDefinition());
        for (const FieldDecl *Field : Record->fields()) {
          QualType Held = Field->getType().getCanonicalType();
          while (const ArrayType *Array = Held->getAsArrayTypeUnsafe())
            Held = Array->getElementType().getCanonicalType();
          if (Held.getUnqualifiedType() == Element)
            return true;
          if (const CXXRecordDecl *Of = Held->getAsCXXRecordDecl();
              Of && Of->getDefinition() &&
              ownerKindOf(Held, Context) == EOwnerKind::None)
            Records.push_back(Of->getDefinition());
        }
      }
      return false;
    };
    if (const auto *Specialization =
            dyn_cast_or_null<ClassTemplateSpecializationDecl>(Class);
        Specialization && holdsOwnerElements(Each, Context))
      for (const TemplateArgument &Argument :
           Specialization->getTemplateArgs().asArray())
        if (Argument.getKind() == TemplateArgument::Type &&
            !(EachKind == EOwnerKind::None && Definition &&
              HeldByMember(Argument.getAsType()))) {
          CKeeperPath Next = Path;
          Next.Deep = Next.Deep || Next.Element;
          Next.Element = true;
          Work.push_back({Argument.getAsType(), Next});
        }
  }
  return Paths;
}

/// The constructor whose body runs with the arguments of \p Construct: of
/// an inheriting constructor, `using CBase::CBase;`, the base's constructor
/// it inherits, which is given them as they are.
static const CXXConstructorDecl *
constructorRun(const CXXConstructExpr *Construct) {
  const CXXConstructorDecl *Constructor =
      Construct ? Construct->getConstructor() : nullptr;
  if (Constructor && Constructor->isInheritingConstructor())
    if (const CXXConstructorDecl *Inherited =
            Constructor->getInheritedConstructor().getConstructor())
      return Inherited;
  return Constructor;
}

/// Whether \p Source is a call that gives back a reference to \p Slot, a slot
/// of what it is given by a pointer or a reference, `fg_ResetAndGet(&Context)`,
/// that a body it can run can change through that parameter
/// (parameterOnlyRead): what the slot holds where it is read is what it holds
/// after the call, which can't be told.
static bool changedByCall(const Expr *Source, const CSlot &Slot,
                          ASTContext &Context) {
  const auto *Call = dyn_cast<CallExpr>(sameValue(Source, Context));
  const FunctionDecl *Callee = Call ? Call->getDirectCallee() : nullptr;
  if (!Callee || isa<CXXMemberCallExpr, CXXOperatorCallExpr>(Call))
    return false;
  const unsigned Shift = argumentShift(Call, Callee);
  for (unsigned iParameter = 0; iParameter < Callee->getNumParams();
       ++iParameter) {
    const QualType Type = Callee->getParamDecl(iParameter)->getType();
    if (iParameter + Shift >= Call->getNumArgs() ||
        !(Type->isPointerType() || Type->isReferenceType()) ||
        pointsToConst(Type))
      continue;
    const Expr *Argument = Call->getArg(iParameter + Shift);
    bool Told = true;
    const llvm::SmallVector<CSlot, 2> Given =
        Type->isPointerType() ? pointedSlots(Argument, Context, &Told)
                              : designatedSlots(Argument, Context, &Told);
    if (llvm::any_of(Given,
                     [&](const CSlot &Each) {
                       return Each.Variable == Slot.Variable;
                     }) &&
        !parameterOnlyRead(Call, Callee, iParameter, Context))
      return true;
  }
  return false;
}

/// The owner value \p E gives, by the provenance (ownerValueSource): what
/// the one local slot it designates holds there, or the member of the object
/// it designates where it is (thisMemberOf), else \p E as a leaf.
static COwnerValue ownerValueOf(const Expr *E, ASTContext &Context) {
  if (!E)
    return {};
  const Expr *Source = ownerValueSource(E, Context);
  const QualType Type = Source->getType().getNonReferenceType();
  // Of a pointer to an owner, what it points to, `ppJob`.
  const bool Pointer =
      Type->isPointerType() &&
      ownerKindOf(Type->getPointeeType(), Context) != EOwnerKind::None;
  // The name of a reference parameter, what it refers to (referentOf).
  if (const auto *Named = dyn_cast<DeclRefExpr>(sameValue(Source, Context)))
    if (const auto *Parameter = dyn_cast<ParmVarDecl>(Named->getDecl());
        Parameter && Parameter->getType()->isReferenceType() &&
        !Parameter->getType().getNonReferenceType()->isPointerType())
      return {{Parameter, {}, false}, Source, nullptr, {}, nullptr, {}};
  // A member or an element of what a reference parameter refers to,
  // `_Jobs[0]` (referentPartOf).
  if (Source->isGLValue())
    if (CReferentPart Part = referentPartOf(Source, Context);
        Part.Parameter && (Part.Element || !Part.Path.empty())) {
      COwnerValue Value;
      Value.Slot = {Part.Parameter, Part.Path, Part.Element};
      Value.At = Source;
      Value.Index = Part.Index;
      return Value;
    }
  if (Source->isGLValue() || Pointer) {
    bool Told = true;
    const llvm::SmallVector<CSlot, 2> Slots =
        Pointer ? pointedSlots(Source, Context, &Told)
                : designatedSlots(Source, Context, &Told);
    if (Told && Slots.size() == 1) {
      COwnerValue Value{Slots.front(), Source, nullptr, {}, nullptr, {}};
      if (Value.Slot.Element)
        Value.Index = elementIndexOf(Source, Context);
      Value.Changed = changedByCall(Source, Value.Slot, Context);
      return Value;
    }
  }
  if (Source->isGLValue()) {
    const LambdaExpr *Closure = nullptr;
    if (CThisMember Member = thisMemberOf(
            Source, executingFunction(Source, nullptr, Context, Closure));
        !Member.Path.empty() && Member.Function)
      return {{}, Source, nullptr, std::move(Member), nullptr, {}};
  }
  return {{}, nullptr, Pointer ? nullptr : Source, {}, nullptr, {}};
}

/// Where the owner \p Owner a borrow is derived through is read: the call it
/// is the object or the first operand of, `Values.f_GetArray(8)` or
/// `pJob->`, past implicit steps and parentheses, after which what the call
/// changes of it is what it holds; else \p Owner itself.
static const Stmt *ownerReadAt(const Expr *Owner, ASTContext &Context) {
  const TraversalKindScope AsIs(Context, TK_AsIs);
  const Expr *Below = Owner;
  for (int Step = 0; Below && Step < 8; ++Step) {
    const auto Parents = instantiatedParents(*Below, Context);
    const Expr *Parent = Parents.empty() ? nullptr : Parents[0].get<Expr>();
    if (!Parent)
      break;
    if (isa<ImplicitCastExpr, ParenExpr, MaterializeTemporaryExpr,
            CXXBindTemporaryExpr>(Parent)) {
      Below = Parent;
      continue;
    }
    if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Parent);
        Member && Member->getImplicitObjectArgument() == Below)
      return Parent;
    if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Parent);
        Operator && Operator->getNumArgs() && Operator->getArg(0) == Below)
      return Parent;
    if (const auto *Member = dyn_cast<MemberExpr>(Parent);
        Member && Member->getBase() == Below)
      if (const auto Calls = instantiatedParents(*Member, Context);
          !Calls.empty())
        if (const auto *Call = Calls[0].get<CXXMemberCallExpr>())
          return Call;
    break;
  }
  return Owner;
}

/// What the leaf \p E of an owner value is (EOwnerLeaf).
static EOwnerLeaf leafKindOf(const Expr *E) {
  if (!E || E->isGLValue())
    return EOwnerLeaf::Untold;
  if (isa<CXXScalarValueInitExpr, ImplicitValueInitExpr>(E))
    return EOwnerLeaf::Empty;
  if (const auto *List = dyn_cast<InitListExpr>(E))
    return List->getNumInits() == 0 ? EOwnerLeaf::Empty : EOwnerLeaf::Made;
  if (const auto *Construct = dyn_cast<CXXConstructExpr>(E)) {
    const bool Defaulted = llvm::all_of(
        Construct->arguments(),
        [](const Expr *Argument) { return isa<CXXDefaultArgExpr>(Argument); });
    const Expr *First =
        Construct->getNumArgs() ? Construct->getArg(0) : nullptr;
    if (Defaulted ||
        (First && (First->getType()->isPointerType() ||
                   First->getType()->isNullPtrType()) &&
         leafOf(First) == ELeaf::Nothing))
      return EOwnerLeaf::Empty;
    return EOwnerLeaf::Made;
  }
  if (isa<CXXNewExpr>(E))
    return EOwnerLeaf::Made;
  if (isa<CallExpr>(E))
    return EOwnerLeaf::Called;
  return EOwnerLeaf::Untold;
}

/// The owner that \p Made, a construction of a `shared` or `unique` owner
/// (OwnerTypes) given only a raw pointer to its object, is made from where that pointer is what another owner
/// owns, `TCSharedPointer<CJob>(_pJob.f_Get())` or `(&*_pJob)`: that owner;
/// null otherwise.
static const Expr *rawPointerOwner(const Expr *Made, ASTContext &Context) {
  const auto *Construct =
      Made ? dyn_cast<CXXConstructExpr>(sameValue(Made, Context)) : nullptr;
  if (!Construct || Construct->getNumArgs() == 0)
    return nullptr;
  const EOwnerKind Kind = ownerKindOf(Construct->getType(), Context);
  if ((Kind != EOwnerKind::Shared && Kind != EOwnerKind::Unique) ||
      !Construct->getArg(0)->getType()->isPointerType() ||
      !llvm::all_of(llvm::drop_begin(Construct->arguments()),
                    [](const Expr *Argument) {
                      return isa<CXXDefaultArgExpr>(Argument);
                    }))
    return nullptr;
  const Expr *Pointer = sameValue(Construct->getArg(0), Context);
  auto OwnerOf = [&](const Expr *E) -> const Expr * {
    const EOwnerKind Of =
        E ? ownerKindOf(E->getType().getNonReferenceType(), Context)
          : EOwnerKind::None;
    return Of == EOwnerKind::Shared || Of == EOwnerKind::Unique ? E : nullptr;
  };
  // `&*_pJob`.
  if (const auto *Address = dyn_cast<UnaryOperator>(Pointer);
      Address && Address->getOpcode() == UO_AddrOf)
    if (const auto *Star = dyn_cast<CXXOperatorCallExpr>(
            sameValue(Address->getSubExpr(), Context));
        Star && Star->getOperator() == OO_Star && Star->getNumArgs() == 1)
      return OwnerOf(Star->getArg(0));
  // `_pJob.f_Get()` or `_pJob.operator->()`: a const member function of the
  // owner given nothing that gives a pointer.
  if (const auto *Call = dyn_cast<CXXMemberCallExpr>(Pointer);
      Call && Call->getNumArgs() == 0 && Call->getMethodDecl() &&
      Call->getMethodDecl()->isConst())
    return OwnerOf(Call->getImplicitObjectArgument());
  if (const auto *Arrow = dyn_cast<CXXOperatorCallExpr>(Pointer);
      Arrow && Arrow->getOperator() == OO_Arrow && Arrow->getNumArgs() == 1)
    return OwnerOf(Arrow->getArg(0));
  return nullptr;
}

/// The line of the owner value \p Owner, the owner a borrow is derived
/// through where it is read: each slot it is in by the one definition of
/// each that reaches there, a copy of another slot, until a leaf, or several
/// or unknown definitions, which can't be told. At most 32 slots.
static COwnerLine ownerLine(const Expr *Owner, ASTContext &Context) {
  COwnerLine Line;
  COwnerValue Value = ownerValueOf(Owner, Context);
  if (Value.Slot.Variable || !Value.Member.Path.empty())
    Value.At = ownerReadAt(Owner, Context);
  for (int Step = 0; Step < 32; ++Step) {
    if (!Value.Member.Path.empty()) {
      Line.Member.emplace(Value.Member, Value.At);
      return Line;
    }
    if (Value.Leaf) {
      Line.Leaf = leafKindOf(Value.Leaf);
      Line.LeafExpr = Value.Leaf;
      return Line;
    }
    if (!Value.Slot.Variable)
      return Line;
    Line.Slots.push_back({Value.Slot, Value.At});
    Line.Indices.push_back(Value.Index);
    if (isReferent(Value.Slot))
      return Line;
    const CSlotReaching Reaching =
        localSlotReaching(Value.Slot, Value.At, Context);
    if (!Reaching.Told || Reaching.Definitions.size() != 1 ||
        !Reaching.Definitions.front().Known)
      return Line;
    const CSlotValue &Only = Reaching.Definitions.front().Value;
    if (Only.Kind == CSlotValue::EKind::Given) {
      Line.Leaf = EOwnerLeaf::Given;
      Line.Parameter = Value.Slot.Path.empty() ? Value.Slot.Variable : nullptr;
      return Line;
    }
    if ((Only.Kind != CSlotValue::EKind::Value &&
         Only.Kind != CSlotValue::EKind::Stored) ||
        !Only.Value)
      return Line;
    Value = ownerValueOf(Only.Value, Context);
  }
  return Line;
}

/// Whether a leaf \p Leaf of a keeper's value, of the kind \p Kind (of a
/// parameter \p Parameter), owns what the line \p Line ends at: an empty
/// value owns nothing; the same leaf owns it; a new object made here is no
/// other value; what a call gives back isn't a new object made here, and of
/// another call, also one whose body is seen, it can't be told; a parameter
/// isn't a new object; what else can't be told.
static EOwns leafOwns(const Expr *Leaf, EOwnerLeaf Kind,
                      const VarDecl *Parameter, const COwnerLine &Line) {
  if (Kind == EOwnerLeaf::Empty)
    return EOwns::Other;
  if (Leaf && Leaf == Line.LeafExpr)
    return EOwns::Same;
  // A call of a function that states it gives back a new object each time
  // (DMibReturnsNew) is a new object made here.
  auto MakesNew = [](const Expr *E) {
    const auto *Call = dyn_cast_or_null<CallExpr>(E);
    return Call && returnsNew(Call->getDirectCallee());
  };
  if (Kind == EOwnerLeaf::Called && MakesNew(Leaf))
    Kind = EOwnerLeaf::Made;
  // A new object made here is no other value.
  if (Kind == EOwnerLeaf::Made)
    return EOwns::Other;
  if (Kind == EOwnerLeaf::Untold || Line.Leaf == EOwnerLeaf::Untold)
    return EOwns::Untold;
  // An empty owner a borrow is made through gets what it owns later, a new
  // object of its own.
  const bool LineMade =
      Line.Leaf == EOwnerLeaf::Made || Line.Leaf == EOwnerLeaf::Empty ||
      (Line.Leaf == EOwnerLeaf::Called && MakesNew(Line.LeafExpr));
  switch (Kind) {
  case EOwnerLeaf::Given:
    if (Line.Leaf == EOwnerLeaf::Given)
      return Parameter && Parameter == Line.Parameter ? EOwns::Same
                                                      : EOwns::Untold;
    return LineMade ? EOwns::Other : EOwns::Untold;
  case EOwnerLeaf::Made:
    return EOwns::Other;
  case EOwnerLeaf::Called:
    return LineMade ? EOwns::Other : EOwns::Untold;
  case EOwnerLeaf::Empty:
  case EOwnerLeaf::Untold:
    break;
  }
  return EOwns::Untold;
}

/// Whether the call \p Leaf is given what the line \p Line goes through, as
/// an argument or its object, a name or the address of a slot of a variable
/// of it, so that what it gives back can be what that owns.
static bool givenLine(const Expr *Leaf, const COwnerLine &Line,
                      ASTContext &Context) {
  const auto *Call = dyn_cast_or_null<CallExpr>(Leaf);
  if (!Call)
    return false;
  llvm::SmallVector<const Expr *, 4> Given(Call->arguments());
  if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call))
    Given.push_back(Member->getImplicitObjectArgument());
  for (const Expr *Each : Given) {
    if (!Each)
      continue;
    bool Told = true;
    const Expr *Source = ownerValueSource(Each, Context);
    const llvm::SmallVector<CSlot, 2> Slots =
        Source->isGLValue() ? designatedSlots(Source, Context, &Told)
        : Source->getType()->isPointerType()
            ? pointedSlots(Source, Context, &Told)
            : llvm::SmallVector<CSlot, 2>();
    if (!Told)
      return true;
    for (const CSlot &Slot : Slots)
      if (llvm::any_of(Line.Slots, [&](const auto &Of) {
            return Of.first.Variable == Slot.Variable;
          }))
        return true;
  }
  return false;
}

/// The owner values what the call \p Leaf gives back is made of, where each
/// body it can run can be seen (CProvenance::calledRoots): what each return
/// of each gives (writtenReturns) where it is, of a parameter held by value
/// that isn't assigned there the argument given, of a member of what a
/// parameter points to or refers to that member of the slot the argument
/// designates, where the call is. False when a body can't be seen or gives
/// nothing.
static bool calledValues(const Expr *Leaf,
                         llvm::SmallVectorImpl<COwnerValue> &Values,
                         ASTContext &Context) {
  const auto *Call = dyn_cast_or_null<CallExpr>(Leaf);
  const FunctionDecl *Callee = Call ? Call->getDirectCallee() : nullptr;
  if (!Callee)
    return false;
  const CProvenance Walk(Context);
  const CCalledRoots Called = Walk.calledRoots(Call, Callee);
  if (Called.open())
    return false;
  const unsigned Shift = argumentShift(Call, Callee);
  for (const auto &Each : Called.Bodies) {
    const FunctionDecl *Body = Each.first;
    const llvm::ArrayRef<const Expr *> Returns = writtenReturns(Body, Context);
    if (Returns.empty())
      return false;
    for (const Expr *Returned : Returns) {
      // A member of what a parameter points to or refers to: that member of
      // what the call gives it, where the call is, changed there where the
      // body can change it through the parameter (parameterOnlyRead).
      if (const CReferentPart Part = referentPartOf(
              ownerValueSource(Returned, Context), Context, true);
          Part.Parameter && Part.Parameter->getDeclContext() == Body &&
          !Part.Path.empty() && !Part.Element &&
          Part.Parameter->getFunctionScopeIndex() + Shift <
              Call->getNumArgs()) {
        const Expr *Argument =
            Call->getArg(Part.Parameter->getFunctionScopeIndex() + Shift);
        bool Told = true;
        const llvm::SmallVector<CSlot, 2> Slots =
            Part.Parameter->getType()->isPointerType()
                ? pointedSlots(Argument, Context, &Told)
                : designatedSlots(Argument, Context, &Told);
        if (Told && Slots.size() == 1 && Slots.front().Path.empty() &&
            !Slots.front().Element) {
          COwnerValue Given;
          Given.Slot = Slots.front();
          Given.Slot.Path.assign(Part.Path.begin(), Part.Path.end());
          Given.At = Call;
          Given.Changed = !parameterOnlyRead(
              Call, Body, Part.Parameter->getFunctionScopeIndex(), Context);
          Values.push_back(Given);
          continue;
        }
      }
      const COwnerValue Value = ownerValueOf(Returned, Context);
      const auto *Parameter =
          dyn_cast_or_null<ParmVarDecl>(Value.Slot.Variable);
      if (Parameter && Value.Slot.Path.empty() && !Value.Slot.Element &&
          Parameter->getDeclContext() == Body) {
        const CSlotReaching Reaching =
            localSlotReaching(Value.Slot, Value.At, Context);
        const unsigned iArgument =
            Parameter->getFunctionScopeIndex() + Shift;
        if (Reaching.Told && iArgument < Call->getNumArgs() &&
            llvm::all_of(Reaching.Definitions,
                         [](const CSlotDefinition &Definition) {
                           return Definition.Known &&
                                  Definition.Value.Kind ==
                                      CSlotValue::EKind::Given;
                         })) {
          Values.push_back(ownerValueOf(Call->getArg(iArgument), Context));
          continue;
        }
      }
      Values.push_back(Value);
    }
  }
  return !Values.empty();
}

/// The owner values the parameter \p Parameter of the call operator of a
/// lambda is given: the argument of each call of the closure, also through
/// the locals it is put in, where it stays local and is only called in the
/// function that makes it (closureUses), or, of a closure given as the first
/// callable argument to a function that runs it with its other arguments
/// (DMibRunsGivenFunction), the argument of that call after it. False
/// otherwise.
static bool closureArguments(const ParmVarDecl *Parameter,
                             llvm::SmallVectorImpl<COwnerValue> &Values,
                             ASTContext &Context) {
  const auto *Operator =
      dyn_cast_or_null<CXXMethodDecl>(Parameter->getDeclContext());
  const LambdaExpr *Closure =
      Operator && Operator->getParent()->isLambda()
          ? lambdaOfCallOperator(Operator, Context)
          : nullptr;
  const auto *Function =
      Closure ? dyn_cast_or_null<FunctionDecl>(
                    Closure->getLambdaClass()->getDeclContext())
              : nullptr;
  if (!Function || !Function->getBody())
    return false;
  const CClosureUses &Uses =
      closureUses(Function, Function->getBody(), Closure, Context);
  // Given, as its first callable argument, to a function that calls it with
  // its other arguments and keeps both until that call's result is done
  // (DMibRunsGivenFunction): what that call gives after it.
  {
    bool bRunning = false;
    unsigned iGiven = 0;
    if (const CallExpr *Call =
            runningCallOf(Uses, iGiven, bRunning, Context)) {
      const unsigned iArgument =
          iGiven + 1 + Parameter->getFunctionScopeIndex();
      if (iArgument >= Call->getNumArgs())
        return false;
      Values.push_back(ownerValueOf(Call->getArg(iArgument), Context));
      return true;
    }
    if (bRunning)
      return false;
  }
  if (!Uses.bLocal)
    return false;

  // Its calls, also through the locals it is put in.
  llvm::SmallVector<CClosureSite, 4> Calls(Uses.Calls.begin(),
                                           Uses.Calls.end());
  for (const CClosureLocal &Local : Uses.Locals) {
    const CClosureUses &Named = sharedFlowCache().closureUses(
        Function, Function->getBody(), Local.Variable, Context);
    if (!Named.bLocal || Local.Owner != Function)
      return false;
    llvm::append_range(Calls, Named.Calls);
  }
  if (Calls.empty())
    return false;
  for (const CClosureSite &Site : Calls) {
    const auto *Call = dyn_cast_or_null<CallExpr>(Site.Use.Parent);
    // Called by its function call operator, the object of the call.
    const auto *Called = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
    const bool Object = Called && Called->getOperator() == OO_Call &&
                        Site.Use.Effect &&
                        Site.Use.Effect->Use == EUse::CallObject;
    if (Site.Kind != EClosureUse::Call || Site.Through || !Call ||
        !Site.Use.Effect ||
        (Site.Use.Effect->Use != EUse::Callee && !Object))
      return false;
    const unsigned iArgument =
        Parameter->getFunctionScopeIndex() + argumentShift(Call, Operator);
    if (iArgument >= Call->getNumArgs())
      return false;
    Values.push_back(ownerValueOf(Call->getArg(iArgument), Context));
  }
  return true;
}

/// The values of the member Part of the record the construction Leaf of \p
/// Made makes, where it is made, its constructor's body seen: of each
/// definition of the member that reaches the end of the body on a way that
/// doesn't throw (thisMemberDefinitions), what is assigned, or what its
/// initializer gives it, or the construction of the constructor it delegates
/// to or of the base the member is of, each with the construction last of
/// its Sites, appended to \p Values. Other where one is a change that can't
/// be told; Untold where it can't be told, or one is a member function
/// called on the object or what it is given to.
static EOwns madeMemberValues(const COwnerValue &Made,
                              llvm::SmallVectorImpl<COwnerValue> &Values,
                              ASTContext &Context) {
  const auto *Construct =
      dyn_cast<CXXConstructExpr>(sameValue(Made.Leaf, Context));
  const CXXConstructorDecl *Constructor = constructorRun(Construct);
  const auto *Definition =
      Constructor ? dyn_cast_or_null<CXXConstructorDecl>(
                        Constructor->getDefinition())
                  : nullptr;
  const auto *Of = dyn_cast<CXXRecordDecl>(Made.Part->getParent());
  if (!Definition || !Definition->getBody() || !Of ||
      (Definition->getParent() != Of &&
       !Definition->getParent()->isDerivedFrom(Of)))
    return EOwns::Untold;
  CThisMember Member;
  Member.Function = Definition;
  Member.Path.push_back(Made.Part);
  const std::optional<llvm::SmallVector<CThisDefinition, 2>> Reaching =
      thisMemberDefinitions(Member, nullptr, Context);
  if (!Reaching || Reaching->empty())
    return EOwns::Untold;
  llvm::SmallVector<const CXXConstructExpr *, 1> Sites = Made.Sites;
  Sites.push_back(Construct);
  EOwns Owns = EOwns::Same;
  // Each definition, of the constructor's body, or of the body of a member
  // function it calls on the object, Setter by Call.
  struct CPending {
    CThisDefinition Definition;
    const FunctionDecl *Setter = nullptr;
    const CallExpr *Call = nullptr;
  };
  llvm::SmallVector<CPending, 4> Work;
  for (const CThisDefinition &Each : *Reaching)
    Work.push_back({Each});
  for (int Step = 0; !Work.empty(); ++Step) {
    if (Step >= 32)
      return EOwns::Untold;
    const CPending Pending = Work.pop_back_val();
    const CThisDefinition &Each = Pending.Definition;
    COwnerValue Next;
    // Of a setter, a parameter of it is what the call gives it; one that
    // doesn't assign it on each way, whose ways can depend on what it is
    // given, `if (_bKeep) m_pJob = _pJob;`, or that calls another on the
    // object, can't be told.
    if (Pending.Setter) {
      if (Each.Called || Each.Entry)
        return EOwns::Untold;
      if (!Each.Value)
        return EOwns::Other;
      const Expr *Value = Each.Value;
      if (const auto *Named =
              dyn_cast<DeclRefExpr>(sameValue(Each.Value, Context)))
        if (const auto *Parameter = dyn_cast<ParmVarDecl>(Named->getDecl());
            Parameter && Parameter->getDeclContext() == Pending.Setter) {
          const unsigned iArgument =
              Parameter->getFunctionScopeIndex() +
              argumentShift(Pending.Call, Pending.Setter);
          if (iArgument >= Pending.Call->getNumArgs())
            return EOwns::Untold;
          Value = Pending.Call->getArg(iArgument);
        }
      Next = ownerValueOf(Value, Context);
      Next.Sites = Sites;
      Values.push_back(Next);
      continue;
    }
    // A member function called on the object whose body can be seen and
    // that no override can replace, a setter, also through an explicit
    // object parameter: what reaches the end of its body.
    if (Each.Called) {
      const auto *Call = dyn_cast_or_null<CallExpr>(Each.Call);
      const auto *Called = Call ? dyn_cast_or_null<CXXMethodDecl>(
                                      Call->getDirectCallee())
                                : nullptr;
      const auto *Setter =
          Called ? dyn_cast_or_null<CXXMethodDecl>(Called->getDefinition())
                 : nullptr;
      CThisMember Set;
      Set.Function = Setter;
      Set.Path = Member.Path;
      const std::optional<llvm::SmallVector<CThisDefinition, 2>> InSetter =
          Setter && Setter->getBody() && !isa<CXXConstructorDecl>(Setter) &&
                  (!Setter->isVirtual() || Setter->hasAttr<FinalAttr>() ||
                   Setter->getParent()->isEffectivelyFinal())
              ? thisMemberDefinitions(Set, nullptr, Context)
              : std::nullopt;
      if (!InSetter || InSetter->empty()) {
        Owns = EOwns::Untold;
        continue;
      }
      for (const CThisDefinition &Inner : *InSetter)
        Work.push_back({Inner, Setter, Call});
      continue;
    }
    if (Each.Entry) {
      // What its initializer gives it, or else the constructor it delegates
      // to or that of the base it is of: what that construction makes it.
      for (const CXXCtorInitializer *Initializer : Definition->inits()) {
        const Expr *Init = Initializer->getInit();
        if (Initializer->getMember() == Made.Part)
          Next = ownerValueOf(Init, Context);
        else if (const Type *Base = Initializer->getBaseClass();
                 (Initializer->isDelegatingInitializer() ||
                  (Base && Base->getAsCXXRecordDecl() &&
                   (Base->getAsCXXRecordDecl() == Of ||
                    Base->getAsCXXRecordDecl()->isDerivedFrom(Of)))) &&
                 Init && isa<CXXConstructExpr>(sameValue(Init, Context))) {
          Next.Leaf = Init;
          Next.Part = Made.Part;
        } else
          continue;
        break;
      }
      if (!Next.Leaf && !Next.Slot.Variable && Next.Member.Path.empty())
        return EOwns::Untold;
    } else if (!Each.Value)
      return EOwns::Other;
    else
      Next = ownerValueOf(Each.Value, Context);
    Next.Sites = Sites;
    Values.push_back(Next);
  }
  return Owns;
}

/// Whether the leaf \p Leaf of a keeper's value, of the kind \p Kind, owns
/// the object of \p Object (an object root): an empty one doesn't; one made
/// of a pointer, `TCSharedPointer<CState>{fg_Explicit(this)}`, owns what
/// that points to (pointeeRootsOf), the object where each root is it, not
/// where none is or one is another object, a parameter or what isn't of
/// it, nor where it can be null on a way (CStorageRoots::Null); what a member function whose bodies can be seen gives back, made of
/// a pointer to its own object (calledValues), owns the object it is called
/// on, told the same; what else can't be told.
static EOwns madeFromObject(const Expr *Leaf, EOwnerLeaf Kind,
                            const CStorageRoot &Object, ASTContext &Context) {
  if (Kind == EOwnerLeaf::Empty)
    return EOwns::Other;
  // Of storage roots, whether each is the object, none is, or else what
  // can't be told.
  auto Of = [&](const CStorageRoots &Roots) {
    if (Roots.Roots.empty())
      return EOwns::Untold;
    bool Same = false;
    bool Other = false;
    for (const CStorageRoot &Root : Roots.Roots) {
      // Made of what a member whose target can't be told points to, it
      // doesn't own the object (targetToldByOwnClass).
      if (Root.Kind == ERootKind::Unknown && Root.Member)
        return EOwns::Other;
      if (Root.Kind == ERootKind::Unknown)
        return EOwns::Untold;
      const bool Is = Root.Kind == ERootKind::Object &&
                      Root.Variable == Object.Variable;
      (Is ? Same : Other) = true;
    }
    return Same && !Other ? EOwns::Same : Other && !Same ? EOwns::Other
                                                         : EOwns::Untold;
  };
  const Expr *Made = Leaf ? sameValue(Leaf, Context) : nullptr;
  if (const auto *Construct = dyn_cast_or_null<CXXConstructExpr>(Made);
      Construct && Construct->getNumArgs() >= 1 &&
      Construct->getArg(0)->getType()->isPointerType()) {
    // A null pointer on a way keeps nothing there.
    const CStorageRoots Roots = pointeeRootsOf(Construct->getArg(0), Context);
    return Roots.Null ? EOwns::Other : Of(Roots);
  }
  const auto *Call = dyn_cast_or_null<CXXMemberCallExpr>(Made);
  // The object it is called on, by its storage.
  auto OfCalled = [&](const CXXMemberCallExpr *On) {
    const Expr *Called = On->getImplicitObjectArgument();
    return Called ? Of(Called->getType()->isPointerType()
                           ? pointeeRootsOf(Called, Context)
                           : storageRootsOf(Called, Context))
                  : EOwns::Untold;
  };
  // A member function that states its result owns the object it runs on
  // (DMibKeepsThis): the object it is called on.
  if (Call && keepsThis(Call->getMethodDecl()))
    return OfCalled(Call);
  // A member of another object, `_pHolder->m_pState`, which can't state that
  // it owns this one; one of the object itself, or of what can't be told,
  // can't be told.
  if (const auto *Member = dyn_cast_or_null<MemberExpr>(Made))
    return Of(Member->isArrow() ? pointeeRootsOf(Member->getBase(), Context)
                                : storageRootsOf(Member->getBase(), Context)) ==
                   EOwns::Other
               ? EOwns::Other
               : EOwns::Untold;
  // A call that isn't on an object, `fg_Other()`, gives an owner of the
  // object only where it states that.
  if (!Call && Kind == EOwnerLeaf::Called && isa_and_nonnull<CallExpr>(Made) &&
      !isa<CXXOperatorCallExpr>(Made))
    return EOwns::Other;
  llvm::SmallVector<COwnerValue, 2> Returned;
  // A member function whose returns can't be seen, of the object or of
  // another, gives an owner of it only where it states that (DMibKeepsThis).
  if (Call && !calledValues(Call, Returned, Context))
    return OfCalled(Call) == EOwns::Untold ? EOwns::Untold : EOwns::Other;
  if (!Call || Returned.empty())
    return EOwns::Untold;
  // Each made of a pointer to the callee's own object.
  for (const COwnerValue &Each : Returned) {
    const auto *Construct =
        Each.Leaf ? dyn_cast<CXXConstructExpr>(sameValue(Each.Leaf, Context))
                  : nullptr;
    if (!Construct || Construct->getNumArgs() < 1 ||
        !Construct->getArg(0)->getType()->isPointerType() ||
        Of(pointeeRootsOf(Construct->getArg(0), Context)) != EOwns::Same)
      return EOwns::Untold;
  }
  return OfCalled(Call);
}

/// Whether the index \p Left of an element, read where \p LeftAt is, and the
/// index \p Right of an element of the same slot, read where \p RightAt is,
/// designate the same element: the same read, the same constant, or the
/// same variable unchanged between, either way, the same; other constants,
/// other variables, or a variable a definition the flow tells changes
/// between, another, another entry of the container; a constant and a
/// variable, or a variable whose definitions can't be told, can't be told.
/// Where either isn't told, a whole container or an element whose index
/// isn't, the same.
static EOwns sameElement(const Expr *Left, const Stmt *LeftAt,
                         const Expr *Right, const Stmt *RightAt,
                         ASTContext &Context) {
  if (!Left || !Right || Left == Right)
    return EOwns::Same;
  auto Constant = [&](const Expr *E) -> std::optional<llvm::APSInt> {
    if (E->isValueDependent() || !E->getType()->isIntegralOrEnumerationType())
      return std::nullopt;
    return integerConstantExprOf(E, Context);
  };
  const std::optional<llvm::APSInt> LeftValue = Constant(Left);
  const std::optional<llvm::APSInt> RightValue = Constant(Right);
  if (LeftValue && RightValue)
    return llvm::APSInt::isSameValue(*LeftValue, *RightValue) ? EOwns::Same
                                                              : EOwns::Other;
  if (LeftValue || RightValue)
    return EOwns::Untold;
  const auto *LeftName = dyn_cast<DeclRefExpr>(Left);
  const auto *RightName = dyn_cast<DeclRefExpr>(Right);
  const auto *Variable =
      LeftName ? dyn_cast<VarDecl>(LeftName->getDecl()) : nullptr;
  if (!Variable || !RightName)
    return EOwns::Untold;
  if (Variable != RightName->getDecl())
    return isa<VarDecl>(RightName->getDecl()) ? EOwns::Other : EOwns::Untold;
  if (const auto *Parameter = dyn_cast<ParmVarDecl>(Variable);
      Parameter && Parameter->getType()->isReferenceType()) {
    CReferentPart Whole;
    Whole.Parameter = Parameter;
    return localReferentUnchangedBetween(Whole, LeftAt, RightAt, Context) ||
                   localReferentUnchangedBetween(Whole, RightAt, LeftAt,
                                                 Context)
               ? EOwns::Same
               : EOwns::Other;
  }
  const CSlot Slot{Variable, {}, false};
  if (localSlotUnchangedBetween(Slot, LeftAt, RightAt, Context) ||
      localSlotUnchangedBetween(Slot, RightAt, LeftAt, Context))
    return EOwns::Same;
  return localSlotReaching(Slot, LeftAt, Context).Told &&
                 localSlotReaching(Slot, RightAt, Context).Told
             ? EOwns::Other
             : EOwns::Untold;
}

/// Makes \p Value, a whole value given to a holding, the part of it at \p
/// Path: of a slot that part of the slot, of a leaf that part of what it
/// gives; false where that can't be told.
static bool partOfValue(COwnerValue &Value, const CKeeperPath &Path) {
  if (Path.Fields.empty() && !Path.Element)
    return true;
  if (Value.Slot.Variable) {
    if (Value.Slot.Element)
      return false;
    llvm::append_range(Value.Slot.Path, Path.Fields);
    Value.Slot.Element = Path.Element;
    Value.Index = nullptr;
    return true;
  }
  if (Value.Leaf && !Value.Part && !Value.Inside) {
    if (Path.Fields.size() == 1 && !Path.Element)
      Value.Part = Path.Fields.front();
    else
      Value.Inside = true;
    return true;
  }
  return false;
}

static const Expr *captureInitOf(const LambdaExpr *Closure, size_t iCapture);

/// Whether the element \p Value designates, by the index the line \p Line
/// reads it by where it is the same slot (COwnerLine::Indices), else its own
/// (COwnerValue::Index), is a constant none of \p Indices is.
static bool otherElementThan(llvm::ArrayRef<int64_t> Indices,
                             const COwnerValue &Value, const COwnerLine &Line,
                             ASTContext &Context) {
  const Expr *Index = Value.Index;
  for (size_t iSlot = 0; iSlot < Line.Slots.size(); ++iSlot)
    if (Line.Slots[iSlot].first == Value.Slot && Line.Indices[iSlot])
      Index = Line.Indices[iSlot];
  const std::optional<llvm::APSInt> Constant =
      constantIntegerOf(Index, Context);
  const std::optional<int64_t> Constant64 =
      Constant ? signedValueOf(*Constant) : std::nullopt;
  return Constant64 && !llvm::is_contained(Indices, *Constant64);
}

/// Of \p Value, a member of a record a closure copies read in the closure's
/// body, that the code of the closure doesn't change: that member of what the
/// capture copies, where the closure is made (ownerValueOf), a slot or a part
/// of what a reference parameter refers to, of an element only where what
/// each definition that reaches there gives it is known; none otherwise.
static COwnerValue capturedRecordOf(const COwnerValue &Value,
                                    ASTContext &Context) {
  const auto *Variable = Value.Slot.Variable;
  const LambdaExpr *Closure = closureCopying(Variable, Value.At);
  if (!Closure || llvm::any_of(runningOperators(Closure),
                               [&](const CXXMethodDecl *Running) {
                                 return localSlotChangedIn(Running, Value.Slot,
                                                           Context);
                               }))
    return {};
  for (size_t iCapture = 0; iCapture < Closure->capture_size(); ++iCapture) {
    const LambdaCapture &Capture = Closure->capture_begin()[iCapture];
    if (!Capture.capturesVariable() || Capture.getCapturedVar() != Variable)
      continue;
    const Expr *Copied = captureInitOf(Closure, iCapture);
    COwnerValue Next = Copied ? ownerValueOf(Copied, Context) : COwnerValue{};
    if (!Next.Slot.Variable || !Next.Slot.Path.empty() || Next.Slot.Element ||
        !Context.hasSameUnqualifiedType(
            Copied->getType().getNonReferenceType(),
            Variable->getType().getNonReferenceType()))
      return {};
    Next.Slot.Path = Value.Slot.Path;
    Next.Slot.Element = Value.Slot.Element;
    // Of an element, only where what each definition of the record that
    // reaches there gives it can be told: a change of the whole record leaves
    // which element can't be told.
    // A change by a call whose bodies tell which elements it can change
    // (writtenPartsOf) leaves which can be told.
    if (Next.Slot.Element && !isReferent(Next.Slot))
      if (const CSlotReaching Reaching =
              localSlotReaching(Next.Slot, Next.At, Context);
          !Reaching.Told ||
          llvm::any_of(Reaching.Definitions, [&](const CSlotDefinition &Each) {
            if (Each.Known)
              return false;
            if (!Each.Changed || !Each.Target)
              return true;
            const CWrittenParts Written =
                writtenPartsOf(Each.Target, Next.Slot, Context);
            return !Written.Told ||
                   (Written.Writes && Written.Indices.empty());
          }))
        return {};
    Next.Index = Value.Index;
    Next.Sites = Value.Sites;
    return Next;
  }
  return {};
}

/// Of \p Value, a member of a local record, where the one definition of the
/// whole record that reaches there is a copy or a move of a whole record of
/// its type: that member of what is copied (ownerValueOf), of a slot or of
/// what a call gives; none otherwise.
static COwnerValue copiedRecordOf(const COwnerValue &Value,
                                  ASTContext &Context) {
  // A record a closure copies is capturedRecordOf's; an init capture's is
  // its initializer.
  if (!Value.Slot.Variable->isInitCapture() &&
      closureCopying(Value.Slot.Variable, Value.At))
    return {};
  const CSlot Whole{Value.Slot.Variable, {}, false};
  const CSlotReaching Reaching = localSlotReaching(Whole, Value.At, Context);
  if (!Reaching.Told || Reaching.Definitions.size() != 1)
    return {};
  const CSlotDefinition &Only = Reaching.Definitions.front();
  if (!Only.Known || Only.Changed ||
      (Only.Value.Kind != CSlotValue::EKind::Value &&
       Only.Value.Kind != CSlotValue::EKind::Stored) ||
      !Only.Value.Value ||
      !Context.hasSameUnqualifiedType(
          Only.Value.Value->getType().getNonReferenceType(),
          Value.Slot.Variable->getType().getNonReferenceType()))
    return {};
  COwnerValue Next = ownerValueOf(Only.Value.Value, Context);
  CKeeperPath Part;
  Part.Fields.assign(Value.Slot.Path.begin(), Value.Slot.Path.end());
  Part.Element = Value.Slot.Element;
  if ((!Next.Slot.Variable && !Next.Leaf) || !Next.Member.Path.empty() ||
      !partOfValue(Next, Part))
    return {};
  // Of an element, only where what each definition of what is copied that
  // reaches there gives it can be told, as of a capture (capturedRecordOf).
  if (Next.Slot.Element && !isReferent(Next.Slot))
    if (const CSlotReaching Copied =
            localSlotReaching(Next.Slot, Next.At, Context);
        !Copied.Told ||
        llvm::any_of(Copied.Definitions, [](const CSlotDefinition &Each) {
          return !Each.Known;
        }))
      return {};
  Next.Sites = Value.Sites;
  return Next;
}

/// Whether \p Target, what a change of a container designates, is the object
/// of a call of a member function of RemovingMethods given only the constant
/// 0, `Jobs.f_SetLen(0)`: a resize to nothing, which removes every element;
/// also of one that clears it, given nothing, `Jobs.f_Clear()` or
/// `Jobs.clear()`.
static bool emptiesContainer(const Expr *Target, ASTContext &Context) {
  if (!Target)
    return false;
  const auto Parents = instantiatedParents(*Target, Context);
  const CMemberCall Call =
      Parents.empty() ? CMemberCall() : receiverCallOf(Parents[0], Target, Context);
  const CXXMethodDecl *Method = Call ? Call->getMethodDecl() : nullptr;
  if (!Method || !Method->getIdentifier() ||
      !matchesNamePattern(identifierTextOf(Method),
                          vocabulary().RemovingMethods.names()))
    return false;
  const unsigned Shift = argumentShift(Call.call(), Method);
  if (Call->getNumArgs() == Shift)
    return identifierTextOf(Method) == "f_Clear" || identifierTextOf(Method) == "clear";
  if (Call->getNumArgs() != 1 + Shift)
    return false;
  const Expr *Length = Call->getArg(Call->getNumArgs() - 1);
  if (Length->isValueDependent() ||
      !Length->getType()->isIntegralOrEnumerationType())
    return false;
  const std::optional<llvm::APSInt> Value =
      integerConstantExprOf(Length, Context);
  return Value && Value->isZero();
}

/// Of a parameter \p Variable of a member function of the class of \p
/// Object, an object root, that runs on the object, not of a lambda, whether
/// it owns the object (DMibKeepsThis): Same where it states that, else
/// Other, as it may own another object; none of another variable.
static std::optional<EOwns> objectParameterOwns(const ValueDecl *Variable,
                                                const CStorageRoot &Object) {
  const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Variable);
  const auto *Method =
      Parameter ? dyn_cast<CXXMethodDecl>(Parameter->getDeclContext())
                : nullptr;
  if (!Method || !Method->isImplicitObjectMemberFunction() ||
      Method->getParent()->isLambda() || !Object.Class ||
      Method->getParent()->getCanonicalDecl() !=
          Object.Class->getCanonicalDecl())
    return std::nullopt;
  return keepsThis(Parameter) ? EOwns::Same : EOwns::Other;
}

/// The parameter the line \p Line ends at: what it is into
/// (COwnerLine::Referent), the parameter its leaf is given by, or the last
/// of its slots that is a parameter's; null of another line.
static const ParmVarDecl *lineParameterOf(const COwnerLine &Line) {
  if (Line.Referent)
    return Line.Referent;
  if (const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Line.Parameter))
    return Parameter;
  for (const auto &Slot : llvm::reverse(Line.Slots))
    if (const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Slot.first.Variable))
      return Parameter;
  return nullptr;
}

/// Of \p Variable, a parameter whose value or referent a keeper is, and \p
/// Through, a parameter of the same function the line ends at, whether the
/// keeper owns what the line goes through by what the parameters state
/// (DMibDistinct): Same of the same parameter, Other of another where either
/// states it is distinct from the others; none otherwise.
static std::optional<EOwns> distinctParameterOwns(const ValueDecl *Variable,
                                                  const ParmVarDecl *Through) {
  const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Variable);
  if (!Parameter || !Through ||
      Parameter->getDeclContext() != Through->getDeclContext() ||
      !(distinctParameter(Parameter) || distinctParameter(Through)))
    return std::nullopt;
  return Parameter == Through ? EOwns::Same : EOwns::Other;
}

/// Whether the leaf \p Leaf of a keeper's value, of the kind \p Kind, owns
/// what the parameter \p Referent refers to, of a function one of whose
/// parameters is DMibDistinct: an empty one doesn't; one made of a pointer,
/// `TCSharedPointer<CJob>{fg_Explicit(&_Other)}`, owns what that points to
/// (pointeeRootsOf), by what the parameters state of each root that is what
/// a parameter refers to (distinctParameterOwns), not where it can be null
/// on a way; what else can't be told.
static EOwns madeFromReferent(const Expr *Leaf, EOwnerLeaf Kind,
                              const ParmVarDecl *Referent,
                              ASTContext &Context) {
  if (Kind == EOwnerLeaf::Empty)
    return EOwns::Other;
  const auto *Construct = dyn_cast_or_null<CXXConstructExpr>(
      Leaf ? sameValue(Leaf, Context) : nullptr);
  if (!Construct || Construct->getNumArgs() < 1 ||
      !Construct->getArg(0)->getType()->isPointerType())
    return EOwns::Untold;
  const CStorageRoots Roots = pointeeRootsOf(Construct->getArg(0), Context);
  if (Roots.Null)
    return EOwns::Other;
  if (Roots.Roots.empty())
    return EOwns::Untold;
  bool Same = false;
  bool Other = false;
  for (const CStorageRoot &Root : Roots.Roots) {
    const std::optional<EOwns> Owns =
        Root.Kind == ERootKind::Parameter
            ? distinctParameterOwns(Root.Variable, Referent)
            : std::nullopt;
    if (!Owns)
      return EOwns::Untold;
    (*Owns == EOwns::Same ? Same : Other) = true;
  }
  return Same && !Other ? EOwns::Same : Other && !Same ? EOwns::Other
                                                       : EOwns::Untold;
}

/// Whether the owner value \p Start, a keeper's where it is taken or held,
/// owns what the owner whose line is \p Asked (ownerLine) owns where a borrow
/// is derived through it (Rule K's identity): each value of it that can reach
/// there, by the definitions of each slot it is in, a copy of another slot
/// followed to that slot, owns it where it and the line are in
/// the same slot that is unchanged between them (localSlotUnchangedBetween);
/// a value the slot is changed to that can't be told, or another definition
/// of a slot of the line, doesn't; a leaf by leafOwns, but what a call whose
/// bodies can be seen gives back is followed to what it is made of
/// (calledValues), and what another call given what the line goes through
/// gives back can't be told (givenLine). A member of the object (CThisMember)
/// owns it where the line ends at the same member and it is unchanged
/// between the two reads, either way (thisMemberUnchangedBetween); a member
/// of a record a construction makes is what reaches the end of the
/// constructor's body (madeMemberValues), a parameter of the constructor
/// what the construction gives it, and a line that ends at what such a
/// parameter refers to goes on with what the construction gives it. Other
/// when any value doesn't, else
/// Untold when any can't be told. A work stack, each slot and place once, at
/// most 64.
static EOwns keeperOwns(const COwnerValue &Start, const COwnerLine &Asked,
                        ASTContext &Context) {
  // A line that ends at what a reference parameter of the constructor of the
  // last of Start's Sites refers to, where the keeper is a member of the
  // object that construction makes: on with what the construction gives it.
  COwnerLine Line = Asked;
  if (const auto *Parameter =
          !Start.Sites.empty() && Line.Leaf == EOwnerLeaf::Untold &&
                  !Line.Member && !Line.Object && !Line.Slots.empty() &&
                  Line.Slots.back().first.Path.empty() &&
                  !Line.Slots.back().first.Element
              ? referentOf(Line.Slots.back().first)
              : nullptr;
      Parameter &&
      Parameter->getDeclContext() ==
          constructorRun(Start.Sites.back())->getDefinition() &&
      Parameter->getFunctionScopeIndex() < Start.Sites.back()->getNumArgs()) {
    const COwnerLine Through = ownerLine(
        Start.Sites.back()->getArg(Parameter->getFunctionScopeIndex()),
        Context);
    llvm::append_range(Line.Slots, Through.Slots);
    llvm::append_range(Line.Indices, Through.Indices);
    Line.Member = Through.Member;
    Line.Leaf = Through.Leaf;
    Line.LeafExpr = Through.LeafExpr;
    Line.Parameter = Through.Parameter;
  }
  bool Untold = false;
  llvm::SmallVector<COwnerValue, 4> Work{Start};
  llvm::SmallVector<std::pair<CSlot, const Stmt *>, 8> Seen;
  for (int Step = 0; !Work.empty(); ++Step) {
    if (Step >= 64)
      return EOwns::Untold;
    const COwnerValue Value = Work.pop_back_val();
    // A parameter of the constructor of the last of Sites: what the
    // construction gives it, in the one before it.
    if (const auto *Parameter =
            dyn_cast_or_null<ParmVarDecl>(Value.Slot.Variable);
        !Value.Sites.empty() && Parameter && Value.Slot.Path.empty() &&
        !Value.Slot.Element &&
        Parameter->getDeclContext() ==
            constructorRun(Value.Sites.back())->getDefinition()) {
      const CXXConstructExpr *Site = Value.Sites.back();
      if (Parameter->getFunctionScopeIndex() >= Site->getNumArgs()) {
        Untold = true;
        continue;
      }
      COwnerValue Given = ownerValueOf(
          Site->getArg(Parameter->getFunctionScopeIndex()), Context);
      Given.Sites.assign(Value.Sites.begin(), Value.Sites.end() - 1);
      Work.push_back(Given);
      continue;
    }
    // A part of what a call gives back whose bodies can't be seen, not given
    // what the line goes through: what the whole is (leafOwns); a part of
    // anything else can't be told.
    if (Value.Leaf &&
        (Value.Inside ||
         (Value.Part &&
          !isa<CXXConstructExpr>(sameValue(Value.Leaf, Context))))) {
      const EOwnerLeaf Kind = leafKindOf(Value.Leaf);
      // Of the object, a part of what a call that can't give an owner of it
      // gives doesn't own it either (madeFromObject).
      if (Line.Object && Kind == EOwnerLeaf::Called &&
          madeFromObject(Value.Leaf, Kind, *Line.Object, Context) ==
              EOwns::Other)
        return EOwns::Other;
      llvm::SmallVector<COwnerValue, 2> Returned;
      if (Kind != EOwnerLeaf::Called || Line.Object ||
          calledValues(Value.Leaf, Returned, Context) ||
          givenLine(Value.Leaf, Line, Context)) {
        Untold = true;
        continue;
      }
      switch (leafOwns(Value.Leaf, Kind, nullptr, Line)) {
      case EOwns::Other:
        return EOwns::Other;
      case EOwns::Untold:
        Untold = true;
        break;
      case EOwns::Same:
        break;
      }
      continue;
    }
    // A member of a record a constructor makes (madeMemberValues).
    if (Value.Leaf && Value.Part) {
      switch (madeMemberValues(Value, Work, Context)) {
      case EOwns::Other:
        return EOwns::Other;
      case EOwns::Untold:
        Untold = true;
        break;
      case EOwns::Same:
        break;
      }
      continue;
    }
    // A member of the object: where the line ends at it, the same where it
    // is unchanged between the two reads, either way, else another value;
    // else what it holds can't be told.
    if (!Value.Member.Path.empty()) {
      if (!Line.Member || !(Line.Member->first == Value.Member)) {
        Untold = true;
        continue;
      }
      if (!thisMemberUnchangedBetween(Value.Member, Line.Member->second,
                                      Value.At, Context) &&
          !thisMemberUnchangedBetween(Value.Member, Value.At,
                                      Line.Member->second, Context))
        return EOwns::Other;
      continue;
    }
    if (Value.Leaf || !Value.Slot.Variable) {
      const EOwnerLeaf Kind = leafKindOf(Value.Leaf);
      // Of the object, what a call gives back is told by the object it is
      // called on (madeFromObject).
      if (llvm::SmallVector<COwnerValue, 2> Returned;
          Kind == EOwnerLeaf::Called && !Line.Object &&
          calledValues(Value.Leaf, Returned, Context)) {
        llvm::append_range(Work, Returned);
        continue;
      }
      if (Kind == EOwnerLeaf::Called && !Line.Object &&
          givenLine(Value.Leaf, Line, Context)) {
        Untold = true;
        continue;
      }
      // An owner made from a raw pointer to what another owner owns owns
      // what that one does (rawPointerOwner).
      if (const Expr *Owner = Kind == EOwnerLeaf::Made && !Line.Object
                                  ? rawPointerOwner(Value.Leaf, Context)
                                  : nullptr) {
        COwnerValue Next = ownerValueOf(Owner, Context);
        if (Next.Slot.Variable && !Next.At)
          Next.At = Value.Leaf;
        Work.push_back(Next);
        continue;
      }
      switch (Line.Object     ? madeFromObject(Value.Leaf, Kind, *Line.Object,
                                               Context)
              : Line.Referent ? madeFromReferent(Value.Leaf, Kind,
                                                 Line.Referent, Context)
                              : leafOwns(Value.Leaf, Kind, nullptr, Line)) {
      case EOwns::Other:
        return EOwns::Other;
      case EOwns::Untold:
        Untold = true;
        break;
      case EOwns::Same:
        break;
      }
      continue;
    }
    // What a call that can change it leaves in a slot of the line isn't
    // what the line read.
    if (Value.Changed) {
      if (llvm::any_of(Line.Slots, [&](const auto &Of) {
            return Of.first.Variable == Value.Slot.Variable;
          }))
        return EOwns::Other;
      Untold = true;
      continue;
    }
    const std::pair<CSlot, const Stmt *> Key{Value.Slot, Value.At};
    if (llvm::is_contained(Seen, Key))
      continue;
    Seen.push_back(Key);
    // What a reference parameter refers to, or a part of it: the same as
    // where the line reads it, of the same element, while nothing writes it
    // through the parameter's names between, else another value.
    if (const ParmVarDecl *Referent = referentOf(Value.Slot)) {
      const auto Of = llvm::find_if(Line.Slots, [&](const auto &Each) {
        return Each.first == Value.Slot;
      });
      if (Of == Line.Slots.end()) {
        // Of a closure given to a function that runs it with its other
        // arguments (DMibRunsGivenFunction), what the argument it is bound
        // to is, the same part of it.
        if (llvm::SmallVector<COwnerValue, 2> Given;
            closureArguments(Referent, Given, Context)) {
          CKeeperPath Part;
          Part.Fields.assign(Value.Slot.Path.begin(), Value.Slot.Path.end());
          Part.Element = Value.Slot.Element;
          for (COwnerValue &Each : Given) {
            if (!partOfValue(Each, Part)) {
              Untold = true;
              continue;
            }
            Work.push_back(Each);
          }
          continue;
        }
        // Of the object, what an owner parameter of the member function
        // refers to owns it only where it states that (DMibKeepsThis); of
        // what another parameter refers to or owns, not where one of the two
        // states it is distinct (DMibDistinct).
        if (const std::optional<EOwns> Owns =
                Line.Object
                    ? objectParameterOwns(Value.Slot.Variable, *Line.Object)
                    : distinctParameterOwns(Value.Slot.Variable,
                                            lineParameterOf(Line))) {
          if (*Owns == EOwns::Other)
            return EOwns::Other;
          continue;
        }
        // What a reference parameter of the function itself refers to is
        // there before the function makes anything: not a new object the
        // line is made with, while nothing writes it through the parameter's
        // names between the line's read and this one.
        const ParmVarDecl *Parameter = Referent;
        if (!Line.Object && !Line.Slots.empty() &&
            (Line.Leaf == EOwnerLeaf::Made || Line.Leaf == EOwnerLeaf::Empty) &&
            !isLambdaCallOperator(Parameter->getDeclContext())) {
          CReferentPart Part;
          Part.Parameter = Parameter;
          Part.Path.assign(Value.Slot.Path.begin(), Value.Slot.Path.end());
          Part.Element = Value.Slot.Element;
          Part.Index = Value.Index;
          // Also what it held where the function was entered, unchanged
          // to this read, whatever is done to it after, `fg_Move(_Keep)`.
          const Stmt *LineAt = Line.Slots.front().second;
          if (localReferentUnchangedBetween(Part, nullptr, Value.At, Context) ||
              localReferentUnchangedBetween(Part, LineAt, Value.At, Context) ||
              localReferentUnchangedBetween(Part, Value.At, LineAt, Context))
            return EOwns::Other;
        }
        Untold = true;
        continue;
      }
      const Expr *LineIndex = Line.Indices[Of - Line.Slots.begin()];
      switch (sameElement(Value.Index, Value.At, LineIndex, Of->second,
                          Context)) {
      case EOwns::Other:
        return EOwns::Other;
      case EOwns::Untold:
        Untold = true;
        continue;
      case EOwns::Same:
        break;
      }
      CReferentPart Part;
      Part.Parameter = Referent;
      Part.Path.assign(Value.Slot.Path.begin(), Value.Slot.Path.end());
      Part.Element = Value.Slot.Element;
      Part.Index = LineIndex ? LineIndex : Value.Index;
      if (!localReferentUnchangedBetween(Part, Of->second, Value.At, Context) &&
          !localReferentUnchangedBetween(Part, Value.At, Of->second, Context))
        return EOwns::Other;
      continue;
    }
    // The same slot as the line's, unchanged between, of the same element.
    bool Matched = false;
    for (size_t iSlot = 0; iSlot < Line.Slots.size() && !Matched; ++iSlot) {
      const auto &[Slot, At] = Line.Slots[iSlot];
      if (!(Slot == Value.Slot) ||
          (!localSlotUnchangedBetween(Value.Slot, At, Value.At, Context) &&
           !localSlotUnchangedBetween(Value.Slot, Value.At, At, Context)))
        continue;
      Matched = true;
      switch (sameElement(Value.Index, Value.At, Line.Indices[iSlot], At,
                          Context)) {
      case EOwns::Other:
        return EOwns::Other;
      case EOwns::Untold:
        Untold = true;
        break;
      case EOwns::Same:
        break;
      }
    }
    // The same slot as the line's where the one same definition reaches
    // both, a read the other's ways don't all pass, `_bChoose ? *pJob :
    // *pJob`.
    for (size_t iSlot = 0; iSlot < Line.Slots.size() && !Matched; ++iSlot) {
      const auto &[Slot, At] = Line.Slots[iSlot];
      if (!(Slot == Value.Slot) || Value.Slot.Element)
        continue;
      const CSlotReaching There = localSlotReaching(Slot, At, Context);
      const CSlotReaching Here = localSlotReaching(Value.Slot, Value.At, Context);
      Matched = There.Told && Here.Told && There.Definitions.size() == 1 &&
                There.Definitions == Here.Definitions &&
                There.Definitions.front().Known &&
                !There.Definitions.front().Changed;
    }
    if (Matched)
      continue;
    // Another definition of a variable of the line than the one it read.
    const bool OfLine = llvm::any_of(Line.Slots, [&](const auto &Of) {
      return Of.first.Variable == Value.Slot.Variable;
    });
    const CSlotReaching Reaching =
        localSlotReaching(Value.Slot, Value.At, Context);
    if (!Reaching.Told || Reaching.Definitions.empty()) {
      Untold = true;
      continue;
    }
    // The one known definition the line read the same slot by, where it
    // reaches here too, is what the line read: of a change between that
    // writtenPartsOf tells leaves the slot as it was, the other definitions
    // decide.
    std::optional<CSlotDefinition> LineRead;
    for (const auto &[Slot, At] : Line.Slots)
      if (Slot == Value.Slot)
        if (const CSlotReaching There = localSlotReaching(Slot, At, Context);
            There.Told && There.Definitions.size() == 1 &&
            There.Definitions.front().Known &&
            !There.Definitions.front().Changed)
          LineRead = There.Definitions.front();
    for (const CSlotDefinition &Each : Reaching.Definitions) {
      if (LineRead && Each == *LineRead)
        continue;
      // A change by a call whose bodies tell what they can change through
      // what they are given (writtenPartsOf) leaves the slot as it was where
      // it can't change it, or, of an element, where the line reads another
      // by a constant index.
      if (!Each.Known && Each.Changed && Each.Target)
        if (const CWrittenParts Written =
                writtenPartsOf(Each.Target, Value.Slot, Context);
            Written.Told &&
            (!Written.Writes ||
             (Value.Slot.Element && !Written.Indices.empty() &&
              otherElementThan(Written.Indices, Value, Line, Context))))
          continue;
      // A change of a container the line doesn't go through leaves which
      // element is what can't be told; a resize to nothing leaves none.
      if (!Each.Known && Each.Changed &&
          (OfLine || !Value.Slot.Element ||
           emptiesContainer(Each.Target, Context)))
        return EOwns::Other;
      // A member of an init capture made by a construction, what it starts
      // with in the closure: that member of what the construction makes
      // (madeMemberValues).
      if (!Each.Known && !Each.Changed && Value.Slot.Path.size() == 1 &&
          !Value.Slot.Element && Value.Slot.Variable->isInitCapture() &&
          Value.Slot.Variable->getInit()) {
        COwnerValue Next = ownerValueOf(Value.Slot.Variable->getInit(), Context);
        if (Next.Leaf && isa<CXXConstructExpr>(sameValue(Next.Leaf, Context))) {
          Next.Part = Value.Slot.Path.front();
          Work.push_back(Next);
          continue;
        }
      }
      // A member of a record a closure copies, what it starts with in the
      // closure where its body doesn't change it: that member of what the
      // capture copies where the closure is made.
      if (!Each.Known && !Each.Changed && !Value.Slot.Path.empty() &&
          Value.At)
        if (COwnerValue Next = capturedRecordOf(Value, Context);
            Next.Slot.Variable) {
          Work.push_back(Next);
          continue;
        }
      // A member of a local record whose one definition that reaches there
      // is a copy of a whole record of its type: that member of what is
      // copied.
      if (!Each.Known && !Each.Changed && !Value.Slot.Path.empty() &&
          Value.At)
        if (COwnerValue Next = copiedRecordOf(Value, Context);
            Next.Slot.Variable || Next.Leaf) {
          Work.push_back(Next);
          continue;
        }
      if (!Each.Known) {
        Untold = true;
        continue;
      }
      switch (Each.Value.Kind) {
      // What it is given, or what is stored where a location is there,
      // `fg_Swap(pKeep, pJob)`: of an element, also what the whole is given,
      // whose elements can't be told when it is made there.
      case CSlotValue::EKind::Value:
      case CSlotValue::EKind::Stored: {
        COwnerValue Next = ownerValueOf(Each.Value.Value, Context);
        // A member given by a copy of the whole record it is in, a copy a
        // closure keeps: the same member of what is copied.
        if (!Value.Slot.Path.empty() && Each.Value.Value &&
            Context.hasSameUnqualifiedType(
                Each.Value.Value->getType().getNonReferenceType(),
                Value.Slot.Variable->getType().getNonReferenceType())) {
          if (Next.Slot.Variable && Next.Slot.Path.empty())
            Next.Slot.Path = Value.Slot.Path;
          else if (!Next.Member.Path.empty() && !Next.Member.Element)
            llvm::append_range(Next.Member.Path, Value.Slot.Path);
          else if (const Expr *Part =
                       Next.Leaf ? projected(Next.Leaf, Value.Slot.Path)
                                 : nullptr)
            Next = ownerValueOf(Part, Context);
          else if (Next.Leaf && Value.Slot.Path.size() == 1)
            Next.Part = Value.Slot.Path.front();
          else {
            Untold = true;
            break;
          }
        }
        QualType Slot = Value.Slot.Path.empty()
                            ? Value.Slot.Variable->getType()
                            : Value.Slot.Path.back()->getType();
        const bool Whole =
            Value.Slot.Element && Each.Value.Value &&
            Context.hasSameUnqualifiedType(
                Each.Value.Value->getType().getNonReferenceType(),
                Slot.getNonReferenceType());
        // Of a variable of the line, it isn't what the line reads.
        if (Whole && !Next.Slot.Variable) {
          if (OfLine)
            return EOwns::Other;
          // An element of what a call gives (keeperOwns).
          if (Next.Leaf && !Next.Part && !Next.Inside &&
              leafKindOf(Next.Leaf) == EOwnerLeaf::Called) {
            Next.Inside = true;
            Next.Sites = Value.Sites;
            Work.push_back(Next);
            break;
          }
          Untold = true;
          break;
        }
        if (Whole)
          Next.Slot.Element = true;
        if (OfLine && (Next.Leaf || !Next.Slot.Variable))
          return EOwns::Other;
        Next.Sites = Value.Sites;
        Work.push_back(Next);
        break;
      }
      case CSlotValue::EKind::Given:
        if (OfLine)
          return EOwns::Other;
        // Of a closure called where it is made, what each call gives it, the
        // same part of it.
        if (llvm::SmallVector<COwnerValue, 2> Given;
            isa<ParmVarDecl>(Value.Slot.Variable) &&
            closureArguments(cast<ParmVarDecl>(Value.Slot.Variable), Given,
                             Context)) {
          CKeeperPath Part;
          Part.Fields.assign(Value.Slot.Path.begin(), Value.Slot.Path.end());
          Part.Element = Value.Slot.Element;
          for (COwnerValue &One : Given) {
            if (!partOfValue(One, Part)) {
              Untold = true;
              continue;
            }
            Work.push_back(One);
          }
          break;
        }
        // Of the object, an owner parameter of the member function owns it
        // only where it states that (DMibKeepsThis); of what another
        // parameter refers to or owns, not where one of the two states it is
        // distinct (DMibDistinct).
        switch (Line.Object
                    ? objectParameterOwns(Value.Slot.Variable, *Line.Object)
                          .value_or(EOwns::Untold)
                : distinctParameterOwns(Value.Slot.Variable,
                                        lineParameterOf(Line))
                    ? *distinctParameterOwns(Value.Slot.Variable,
                                             lineParameterOf(Line))
                    : leafOwns(nullptr, EOwnerLeaf::Given,
                               Value.Slot.Path.empty() ? Value.Slot.Variable
                                                       : nullptr,
                               Line)) {
        case EOwns::Other:
          return EOwns::Other;
        case EOwns::Untold:
          Untold = true;
          break;
        case EOwns::Same:
          break;
        }
        break;
      case CSlotValue::EKind::Address:
        Untold = true;
        break;
      }
    }
  }
  return Untold ? EOwns::Untold : EOwns::Same;
}

/// Of \p Made, an array member (Part) of a record a construction makes,
/// whether an element of it owns what the line \p Line ends at: the keeper
/// holds each element, so of each value the member can be made with, an
/// array initialized element by element, it owns it where any element does
/// (keeperOwns); Other where on one value no element can, else Untold where
/// a value or an element can't be told.
static EOwns madeElementsOwn(const COwnerValue &Made, const COwnerLine &Line,
                             ASTContext &Context) {
  llvm::SmallVector<COwnerValue, 2> Values;
  if (madeMemberValues(Made, Values, Context) != EOwns::Same || Values.empty())
    return EOwns::Untold;
  bool Untold = false;
  for (const COwnerValue &Each : Values) {
    const auto *List = Each.Leaf ? dyn_cast<InitListExpr>(
                                       sameValue(Each.Leaf, Context))
                                 : nullptr;
    if (!List || !List->getType()->isArrayType() ||
        List->getNumInits() == 0 || List->hasArrayFiller())
      return EOwns::Untold;
    EOwns Any = EOwns::Other;
    for (const Expr *Init : List->inits()) {
      COwnerValue Element = ownerValueOf(Init, Context);
      Element.Sites = Each.Sites;
      const EOwns Owns = keeperOwns(Element, Line, Context);
      if (Owns == EOwns::Same) {
        Any = EOwns::Same;
        break;
      }
      if (Owns == EOwns::Untold)
        Any = EOwns::Untold;
    }
    if (Any == EOwns::Other)
      return EOwns::Other;
    Untold |= Any == EOwns::Untold;
  }
  return Untold ? EOwns::Untold : EOwns::Same;
}

/// Of the array \p Array, a local or a member of one, or a closure's copy of
/// one, where \p At is, made of a list of its elements by the one
/// definition that reaches there, `TCSharedPointer<CState> pStates[2]{_pPeer,
/// _pPeer}`, whether an element of it owns what the line \p Line ends at
/// (keeperOwns): Same where one does, Other where none can, else Untold.
/// Of a copy, where the closure is made, while its code doesn't change it.
/// None where it isn't made so, or where the line goes through it, which
/// the line's own slots tell.
static std::optional<EOwns> listedElementsOwn(const CSlot &Array,
                                              const Stmt *At,
                                              const COwnerLine &Line,
                                              ASTContext &Context) {
  if (!Array.Variable || Array.Element || !At ||
      llvm::any_of(Line.Slots, [&](const auto &Of) {
        return Of.first.Variable == Array.Variable;
      }))
    return std::nullopt;
  if (const LambdaExpr *Closure = closureCopying(Array.Variable, At)) {
    if (llvm::any_of(runningOperators(Closure),
                     [&](const CXXMethodDecl *Running) {
                       return localSlotChangedIn(Running, Array, Context);
                     }))
      return std::nullopt;
    At = Closure;
  }
  const CSlotReaching Reaching = localSlotReaching(Array, At, Context);
  if (!Reaching.Told || Reaching.Definitions.size() != 1 ||
      !Reaching.Definitions.front().Known ||
      (Reaching.Definitions.front().Value.Kind != CSlotValue::EKind::Value &&
       Reaching.Definitions.front().Value.Kind != CSlotValue::EKind::Stored) ||
      !Reaching.Definitions.front().Value.Value)
    return std::nullopt;
  const Expr *Made = sameValue(Reaching.Definitions.front().Value.Value, Context);
  // Of a container, a construction given only a list of its elements,
  // `TCList<TCSharedPointer<CJob>> Jobs{_pOther}`, which it holds.
  if (const InitListExpr *Given =
          elementListGivenTo(dyn_cast<CXXConstructExpr>(Made)))
    Made = Given;
  const auto *List = dyn_cast<InitListExpr>(Made);
  if (!List || !List->getType()->isArrayType() || List->getNumInits() == 0 ||
      List->hasArrayFiller())
    return std::nullopt;
  EOwns Any = EOwns::Other;
  for (const Expr *Init : List->inits()) {
    const EOwns Owns = keeperOwns(ownerValueOf(Init, Context), Line, Context);
    if (Owns == EOwns::Same)
      return EOwns::Same;
    if (Owns == EOwns::Untold)
      Any = EOwns::Untold;
  }
  return Any;
}

/// Whether the owner of the kind \p Kind and the anchor \p Anchor that \p
/// Held holds by value can own what the line \p Line ends at, the owner a
/// borrow is derived through or the object (keeperOwns): of each place it is
/// held in the value
/// (keeperPathsOf), its value in the slot of the holding where the context is
/// used, else in what the holding is made of; true unless each is another.
static bool heldOwns(const CHeldValue &Held, EOwnerKind Kind,
                     const CAnchor &Anchor, const COwnerLine &Line,
                     ASTContext &Context) {
  for (const CKeeperPath &Path :
       keeperPathsOf(Held.Type, Kind, Anchor, Context)) {
    if (Path.Deep)
      return true;
    // A capture of a closure as a whole, where no use is told, keeps what
    // it is made of only where the code of the closure doesn't change it;
    // where it does, each use is asked what reaches it there.
    if (Held.Closure && !Held.Where && Held.Holder &&
        llvm::any_of(
            runningOperators(Held.Closure), [&](const CXXMethodDecl *Running) {
              return localSlotChangedIn(
                  Running, {Held.Holder, Path.Fields, Path.Element}, Context);
            }))
      continue;
    // Of a capture of a structured binding, by the member of the decomposed
    // variable it names (bindingSlotOf), likewise, also where a use is told,
    // as the closure's flow has no slot of it to ask there.
    if (const BindingDecl *Binding = Held.Closure && !Held.Holder
                                         ? bindingCopiedBy(Held.Made)
                                         : nullptr)
      if (const std::optional<CSlot> Bound = bindingSlotOf(Binding)) {
        CSlot Part = *Bound;
        llvm::append_range(Part.Path, Path.Fields);
        Part.Element = Path.Element;
        if (llvm::any_of(runningOperators(Held.Closure),
                         [&](const CXXMethodDecl *Running) {
                           return localSlotChangedIn(Running, Part, Context);
                         }))
          continue;
      }
    COwnerValue Start;
    llvm::SmallVector<COwnerValue, 2> Given;
    // An element of an array member of the record an init capture is made
    // with by a construction, where no code of the closure changes it
    // before the use (madeElementsOwn).
    if (const VarDecl *Holder = Held.Holder;
        Holder && Held.Where && Path.Element && Path.Fields.size() == 1 &&
        Holder->isInitCapture() && Holder->getInit() &&
        isa<CXXConstructExpr>(sameValue(Holder->getInit(), Context))) {
      const CSlotReaching Reaching = localSlotReaching(
          {Holder, Path.Fields, Path.Element}, Held.Where, Context);
      if (Reaching.Told &&
          llvm::all_of(Reaching.Definitions, [](const CSlotDefinition &Each) {
            return !Each.Known && !Each.Changed;
          })) {
        COwnerValue Made;
        Made.Leaf = Holder->getInit();
        Made.Part = Path.Fields.front();
        if (madeElementsOwn(Made, Line, Context) != EOwns::Other)
          return true;
        continue;
      }
    }
    // An element of an array, or of a container, made of a list of its
    // elements (listedElementsOwn).
    if (Held.Holder && Path.Element)
      if (const std::optional<EOwns> Owns = listedElementsOwn(
              {Held.Holder, Path.Fields, false},
              Held.Where     ? Held.Where
              : Held.Closure ? static_cast<const Stmt *>(Held.Closure)
                             : Held.Made,
              Line, Context)) {
        if (*Owns != EOwns::Other)
          return true;
        continue;
      }
    if (Held.Holder && Held.Where) {
      Start.Slot = {Held.Holder, Path.Fields, Path.Element};
      Start.At = Held.Where;
    } else if (const auto *Parameter =
                   dyn_cast_or_null<ParmVarDecl>(Held.Holder);
               Parameter && closureArguments(Parameter, Given, Context)) {
      // A parameter of a closure called where it is made, wherever in its
      // body: what each call gives it, the same part of it.
      if (llvm::all_of(Given, [&](COwnerValue &Each) {
            return partOfValue(Each, Path) &&
                   keeperOwns(Each, Line, Context) == EOwns::Other;
          }))
        continue;
      return true;
    } else if (Held.ObjectOf && !Path.Fields.empty() && !Path.Element) {
      // A member of the object of the function that joins, at the join.
      Start.Member.Function = Held.ObjectOf;
      Start.Member.Path.assign(Path.Fields.begin(), Path.Fields.end());
      Start.At = Held.Where;
    } else if (Held.Made) {
      const Expr *Part = Path.Fields.empty()
                             ? Held.Made
                             : projected(Held.Made, Path.Fields);
      Start = ownerValueOf(Part ? Part : Held.Made, Context);
      // A part of what a call gives (keeperOwns).
      if ((!Part || Path.Fields.empty()) && Start.Leaf &&
          leafKindOf(Start.Leaf) == EOwnerLeaf::Called &&
          (!Path.Fields.empty() || Path.Element)) {
        partOfValue(Start, Path);
        if (keeperOwns(Start, Line, Context) != EOwns::Other)
          return true;
        continue;
      }
      // Of the object, a part of what a call made in it gives, by the
      // innermost member the way can project to, `CResult{fg_Make<CField>(),
      // ...}` of a keeper in a member of the CField: a call that can't give an
      // owner of it doesn't (madeFromObject).
      if (!Part && Line.Object && !Path.Fields.empty()) {
        bool Other = false;
        for (size_t nFields = Path.Fields.size() - 1; nFields > 0 && !Other;
             --nFields) {
          const Expr *Outer = projected(
              Held.Made, llvm::ArrayRef(Path.Fields).take_front(nFields));
          const COwnerValue Whole =
              Outer ? ownerValueOf(Outer, Context) : COwnerValue();
          Other = Whole.Leaf &&
                  leafKindOf(Whole.Leaf) == EOwnerLeaf::Called &&
                  madeFromObject(Whole.Leaf, EOwnerLeaf::Called, *Line.Object,
                                 Context) == EOwns::Other;
          if (Outer)
            break;
        }
        if (Other)
          continue;
      }
      if (!Part) {
        if (!Start.Member.Path.empty() && !Start.Member.Element)
          llvm::append_range(Start.Member.Path, Path.Fields);
        else if (Start.Leaf && Path.Fields.size() == 1)
          Start.Part = Path.Fields.front();
        else if (!Start.Slot.Variable || Start.Slot.Element)
          return true;
        else
          llvm::append_range(Start.Slot.Path, Path.Fields);
      }
      if (Path.Element && Start.Slot.Variable)
        Start.Slot.Element = true;
      else if (Path.Element && Start.Leaf && Start.Part) {
        if (madeElementsOwn(Start, Line, Context) != EOwns::Other)
          return true;
        continue;
      } else if (Path.Element) {
        // Of the object, an element of what a call that can't give an owner
        // of it gives (madeFromObject).
        if (Line.Object && Start.Leaf &&
            leafKindOf(Start.Leaf) == EOwnerLeaf::Called &&
            madeFromObject(Start.Leaf, EOwnerLeaf::Called, *Line.Object,
                           Context) == EOwns::Other)
          continue;
        return true;
      }
    } else if (const auto *Function =
                   Parameter ? dyn_cast<FunctionDecl>(Parameter->getDeclContext())
                             : nullptr;
               Function && !isLambdaCallOperator(Function)) {
      // A parameter of the function itself that its code doesn't change:
      // what it is given, which is there before anything the function makes.
      if (localSlotChangedIn(Function, {Parameter, Path.Fields, Path.Element},
                             Context) ||
          leafOwns(nullptr, EOwnerLeaf::Given,
                   Path.Fields.empty() && !Path.Element ? Parameter : nullptr,
                   Line) != EOwns::Other)
        return true;
      continue;
    } else
      return true;
    if (keeperOwns(Start, Line, Context) != EOwns::Other)
      return true;
  }
  return false;
}

/// Whether the owners of the kind \p Kind and the anchor \p Anchor that \p
/// Held holds are elements of a container that can be empty where the
/// holding is used, else where it is made (keeperPathsOf): of each place,
/// a definition of the container that reaches there empties it
/// (emptiesContainer), also of the container a copy or a move of it that
/// reaches there alone copies, `Jobs = fg_Move(_Jobs)` after
/// `_Jobs.f_Clear()`. A holding that can be empty keeps nothing on that way
/// (Rule K's flow), whatever object it would own.
static bool keeperEmptied(const CHeldValue &Held, EOwnerKind Kind,
                          const CAnchor &Anchor, ASTContext &Context) {
  const llvm::SmallVector<CKeeperPath, 1> Paths =
      keeperPathsOf(Held.Type, Kind, Anchor, Context);
  if (Paths.empty())
    return false;
  for (const CKeeperPath &Path : Paths) {
    if (!Path.Element || Path.Deep)
      return false;
    CSlot Slot;
    const Stmt *At = nullptr;
    if (Held.Holder && Held.Where) {
      Slot = {Held.Holder, Path.Fields, true};
      At = Held.Where;
    } else if (Held.Made) {
      bool Told = true;
      const llvm::SmallVector<CSlot, 2> Slots = designatedSlots(
          ownerValueSource(Held.Made, Context), Context, &Told);
      if (!Told || Slots.size() != 1 || Slots.front().Element)
        return false;
      Slot = Slots.front();
      llvm::append_range(Slot.Path, Path.Fields);
      Slot.Element = true;
      At = Held.Made;
    } else
      return false;
    bool Emptied = false;
    for (int Step = 0; Step < 8 && !Emptied; ++Step) {
      const CSlotReaching Reaching = localSlotReaching(Slot, At, Context);
      if (!Reaching.Told)
        break;
      Emptied = llvm::any_of(
          Reaching.Definitions, [&](const CSlotDefinition &Each) {
            return Each.Changed && emptiesContainer(Each.Target, Context);
          });
      // The one definition, a copy or a move of a whole container of its
      // type: on with that container where it is copied.
      if (Emptied || Reaching.Definitions.size() != 1 ||
          !Reaching.Definitions.front().Known ||
          Reaching.Definitions.front().Value.Kind !=
              CSlotValue::EKind::Value ||
          !Reaching.Definitions.front().Value.Value)
        break;
      const Expr *Made = Reaching.Definitions.front().Value.Value;
      const Expr *Source = ownerValueSource(Made, Context);
      const QualType Whole = Slot.Variable->getType().getNonReferenceType();
      bool Told = true;
      const llvm::SmallVector<CSlot, 2> Slots =
          Slot.Path.size() == Path.Fields.size() && Source != Made &&
                  Context.hasSameUnqualifiedType(
                      Source->getType().getNonReferenceType(), Whole)
              ? designatedSlots(Source, Context, &Told)
              : llvm::SmallVector<CSlot, 2>();
      if (!Told || Slots.size() != 1 || Slots.front().Element)
        break;
      Slot = Slots.front();
      llvm::append_range(Slot.Path, Path.Fields);
      Slot.Element = true;
      At = Made;
    }
    if (!Emptied)
      return false;
  }
  return true;
}

static bool ownedChangedIn(const VarDecl *Owner, const Stmt *Body,
                           const Expr *Name, const Expr *Made,
                           ASTContext &Context);

/// Whether the owner \p Hops[iInner] a borrow is derived through is in what
/// the owner \p Hops[iOuter], which the capture \p Held holds, owns alone, so
/// that only the code of the function that makes the closure and of the
/// closure can change it: each owner from the one after it out to that one
/// is a `unique` owner of what the one before is in, with no pointer or
/// lookup between (CHop); the outermost is a local of that function held by
/// value, or a parameter by value or by `&&`, named where the borrow reads
/// it; and neither the function, other than by that name and by what the
/// capture is made of, nor the closure changes what it owns
/// (ownedChangedIn).
static bool ownedAlone(llvm::ArrayRef<CHop> Hops, size_t iInner, size_t iOuter,
                       const CHeldValue &Held, ASTContext &Context) {
  for (size_t iHop = iInner + 1; iHop <= iOuter; ++iHop)
    if (Hops[iHop].Kind != CHop::EKind::Owner ||
        Hops[iHop].OwnerKind != EOwnerKind::Unique ||
        Hops[iHop].bPast != Hops[iInner].bPast)
      return false;
  if (!Held.Closure || !Held.Holder || !Held.Made || !Hops[iOuter].Owner)
    return false;
  const auto *Name =
      dyn_cast<DeclRefExpr>(ownerValueSource(Hops[iOuter].Owner, Context));
  const auto *Outer = Name ? dyn_cast<VarDecl>(Name->getDecl()) : nullptr;
  if (!Outer || !Outer->hasLocalStorage() ||
      Name->refersToEnclosingVariableOrCapture() ||
      (Outer->getType()->isReferenceType() &&
       !(isa<ParmVarDecl>(Outer) &&
         Outer->getType()->isRValueReferenceType())))
    return false;
  const auto *Function = dyn_cast<FunctionDecl>(Outer->getDeclContext());
  if (!Function || !Function->getBody() ||
      ownedChangedIn(Outer, Function->getBody(), Name, Held.Made, Context))
    return false;
  return llvm::none_of(runningOperators(Held.Closure),
                       [&](const CXXMethodDecl *Running) {
                         return ownedChangedIn(Held.Holder, Running->getBody(),
                                               nullptr, nullptr, Context);
                       });
}

/// Whether what \p Held holds by value has a keeper for one of \p Anchors,
/// the one question of what keeps storage alive (Rule K), asked only by
/// isAlive: an owner of OwnerTypes that is a by-value part of one of them by
/// its declared type (keepersOf), the value itself, a member or base of a
/// record, also a virtual one, an element of an array or, by the arguments
/// of its template, of a container, a capture of a closure, that keeps by
/// its kind as it is taken (ETake) and fits the anchor by type: of `shared`
/// and `unique`, the owned type is the anchor's, a base of it or derived
/// from it; of `buffer` and `sharedbuffer`, the owner is the anchor's; cv
/// ignored. A `weak` owner never keeps. A local keeps only where it is in
/// scope (inScopeAt). Where the borrow is derived through owners, \p Hops,
/// one that fits the keeper by type is asked whether the keeper owns what
/// it owns (heldOwns): not when its value is another object or none. Where
/// it is into the object \p Object itself, whether the keeper owns that
/// object (madeFromObject).
static bool holdsKeeper(llvm::ArrayRef<CHeldValue> Held,
                        llvm::ArrayRef<CAnchor> Anchors, ASTContext &Context,
                        llvm::ArrayRef<CHop> Hops = {},
                        const CStorageRoot *Object = nullptr,
                        const ParmVarDecl *Referent = nullptr) {
  for (const CHeldValue &Each : Held)
    if (!Each.Local ||
        inScopeAt(Each.Local, Each.Where, Each.bOrdered, Context))
      for (const std::pair<EOwnerKind, CAnchor> &Keeper :
           keepersOf(Each.Type, Context))
        if (takeKeeps(Keeper.first, Each.Take) &&
            llvm::any_of(Anchors, [&](const CAnchor &Anchor) {
              return fits(Keeper.first, Keeper.second, Anchor, Context);
            })) {
          bool Fitted = false;
          bool Owns = false;
          for (size_t iHop = 0; iHop < Hops.size(); ++iHop) {
            const CHop &Hop = Hops[iHop];
            if (Hop.Kind != CHop::EKind::Owner || !Hop.Owner || !Hop.Anchor ||
                !fits(Keeper.first, Keeper.second, *Hop.Anchor, Context))
              continue;
            Fitted = true;
            // Of an owner the borrow is derived through further out, what
            // it owns keeps the owners nearer the borrow only where none of
            // them can be changed while it is held: a const one, or one in
            // what it owns alone (ownedAlone); else whoever shares it can
            // replace them.
            bool Changeable = false;
            for (size_t iInner = 0; iInner < iHop && !Changeable; ++iInner) {
              const CHop &Inner = Hops[iInner];
              Changeable = Inner.Kind == CHop::EKind::Owner && Inner.Owner &&
                           !ownerValueSource(Inner.Owner, Context)
                                ->getType()
                                .isConstQualified() &&
                           !ownedAlone(Hops, iInner, iHop, Each, Context);
            }
            if (Changeable)
              continue;
            if (heldOwns(Each, Keeper.first, Keeper.second,
                         ownerLine(Hop.Owner, Context), Context)) {
              Owns = true;
              break;
            }
          }
          if (!Fitted && Object && Hops.empty()) {
            COwnerLine Line;
            Line.Object = Object;
            Fitted = true;
            Owns = heldOwns(Each, Keeper.first, Keeper.second, Line, Context);
          }
          // Of what a parameter refers to, where a parameter of its function
          // states it is distinct from the others (DMibDistinct), whether
          // the keeper owns it by what each parameter states.
          if (!Fitted && Referent && Hops.empty()) {
            COwnerLine Line;
            Line.Referent = Referent;
            Fitted = true;
            Owns = heldOwns(Each, Keeper.first, Keeper.second, Line, Context);
          }
          // A keeper that no owner the borrow is derived through fits keeps
          // by its type, unless it can be empty (keeperEmptied).
          if (Owns || (!Fitted && !keeperEmptied(Each, Keeper.first,
                                                 Keeper.second, Context)))
            return true;
        }
  return false;
}

bool namesImplicitlyMovable(const Expr *Named) {
  const auto *Ref = dyn_cast<DeclRefExpr>(Named->IgnoreParenImpCasts());
  const auto *Variable = Ref ? dyn_cast<VarDecl>(Ref->getDecl()) : nullptr;
  return Variable && !Ref->refersToEnclosingVariableOrCapture() &&
         Variable->hasLocalStorage() && !Variable->isInitCapture() &&
         !Variable->getType()->isReferenceType() &&
         !Variable->getType().isVolatileQualified();
}

/// How the value \p Made is taken where a holding is made of it, a capture,
/// an argument or, with \p Result, a result: by the category of the
/// expression as written, past the construction of the copy or the move
/// that takes it, its cleanups and parentheses: a copy of an lvalue, else a
/// move or a new value, also of a temporary bound and of the name of an
/// entity that the result moves implicitly.
static ETake takeOf(const Expr *Made, bool Result) {
  if (!Made)
    return ETake::Copy;
  if (Result && namesImplicitlyMovable(Made))
    return ETake::Move;
  const Expr *Taken = Made;
  for (int Step = 0; Step < 16; ++Step) {
    Taken = Taken->IgnoreParens();
    if (const auto *Full = dyn_cast<FullExpr>(Taken)) {
      Taken = Full->getSubExpr();
      continue;
    }
    if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(Taken)) {
      Taken = Bind->getSubExpr();
      continue;
    }
    // The construction a copy-initialization makes of its source, not one
    // written, `T(Value)` or `T{Value}`.
    if (const auto *Construct = dyn_cast<CXXConstructExpr>(Taken);
        Construct && !isa<CXXTemporaryObjectExpr>(Construct) &&
        !Construct->isListInitialization() && Construct->getNumArgs() >= 1 &&
        llvm::all_of(llvm::drop_begin(Construct->arguments()),
                     [](const Expr *Argument) {
                       return isa<CXXDefaultArgExpr>(Argument);
                     })) {
      Taken = Construct->getArg(0);
      continue;
    }
    break;
  }
  if (isa<MaterializeTemporaryExpr>(Taken))
    return ETake::Move;
  return Taken->isLValue() ? ETake::Copy : ETake::Move;
}

/// Appends to \p Held the parameters of \p Function held by value, the
/// explicit object parameter too.
static void parametersHeld(const FunctionDecl *Function,
                           llvm::SmallVectorImpl<CHeldValue> &Held) {
  for (const ParmVarDecl *Parameter : Function->parameters())
    if (!Parameter->getType()->isReferenceType())
      Held.push_back({Parameter->getType(), ETake::Held, nullptr, nullptr,
                      true, Parameter});
}

namespace {
/// Of each closure, what it holds by value (closureHeld), told once.
struct CClosureHeld {
  llvm::DenseMap<const LambdaExpr *, llvm::SmallVector<CHeldValue, 4>> Of;
};
} // namespace

/// The initializer of the capture \p iCapture of \p Closure: of an init
/// capture its variable's.
static const Expr *captureInitOf(const LambdaExpr *Closure, size_t iCapture) {
  const LambdaCapture &Capture = Closure->capture_begin()[iCapture];
  const Expr *Init = captureInitAt(Closure, iCapture);
  if (const auto *Variable = capturedVariableOf(Capture);
      Variable && Variable->isInitCapture() &&
      Variable->getDeclContext() == Closure->getCallOperator() &&
      Variable->getInit())
    Init = Variable->getInit();
  return Init;
}

/// Appends to \p Held what \p Closure holds by value: each member of its
/// class held by value, a capture by copy, an init capture or `*this`, by
/// its declared type, taken as its initializer is (takeOf); and the
/// parameters of its call operators held by value.
static void closureHeld(const LambdaExpr *Closure,
                        llvm::SmallVectorImpl<CHeldValue> &Held) {
  ASTContext &Context = Closure->getCallOperator()->getASTContext();
  auto [It, Inserted] = astCache<CClosureHeld>(Context).Of.try_emplace(Closure);
  if (Inserted) {
    size_t iCapture = 0;
    for (const FieldDecl *Field : Closure->getLambdaClass()->fields()) {
      if (iCapture >= Closure->capture_size())
        break;
      const size_t iThis = iCapture++;
      if (Field->getType()->isReferenceType() ||
          Field->getType()->isDependentType())
        continue;
      const LambdaCapture &Capture = Closure->capture_begin()[iThis];
      const Expr *Init = captureInitOf(Closure, iThis);
      It->second.push_back({Field->getType(), takeOf(Init, false), nullptr,
                            nullptr, true, capturedVariableOf(Capture), Init,
                            Closure});
    }
    for (const CXXMethodDecl *Running : runningOperators(Closure))
      parametersHeld(Running, It->second);
  }
  llvm::append_range(Held, It->second);
}

namespace {
/// Of each body, its locals held by value (localsOf), told once.
struct CLocalsOfBodies {
  llvm::DenseMap<const Stmt *, llvm::SmallVector<const VarDecl *, 8>> Of;
};
} // namespace

/// The type of what the local \p Variable holds by value: its own, or of a
/// reference the temporary it extends, `auto const &pKeep =
/// TCSharedPointer<CJob>{this}`, which lives as long as it (extendedSlot);
/// null of a reference to what lives elsewhere, which is that, not a holding
/// of its own.
static QualType heldTypeOf(const VarDecl *Variable) {
  if (!Variable->getType()->isReferenceType())
    return Variable->getType();
  if (const MaterializeTemporaryExpr *Extended = extendedSlot(Variable))
    return Extended->getType();
  return QualType();
}

/// The locals of \p Body held by value, wherever in it they are declared,
/// not those of the lambdas in it: a variable it declares, not one the
/// compiler declares, also a reference that extends a temporary
/// (heldTypeOf), a structured binding by the variable that holds it and the
/// variable of a handler. A work stack over what runs in it, once for each
/// body.
static llvm::ArrayRef<const VarDecl *> localsOf(const Stmt *Body,
                                                ASTContext &Context) {
  auto [It, Inserted] = astCache<CLocalsOfBodies>(Context).Of.try_emplace(Body);
  if (!Inserted || !Body)
    return It->second;
  llvm::SmallVector<const VarDecl *, 8> &Locals = It->second;
  auto Declare = [&](const VarDecl *Variable) {
    if (!Variable || Variable->isImplicit() || isa<ParmVarDecl>(Variable) ||
        Variable->isInitCapture() || heldTypeOf(Variable).isNull() ||
        Variable->getType()->isDependentType())
      return;
    Locals.push_back(Variable);
  };
  llvm::SmallVector<const Stmt *, 32> Work{Body};
  llvm::SmallPtrSet<const Stmt *, 32> Seen;
  while (!Work.empty()) {
    const Stmt *S = Work.pop_back_val();
    if (!Seen.insert(S).second)
      continue;
    if (const auto *Declaration = dyn_cast<DeclStmt>(S))
      for (const Decl *D : Declaration->decls())
        Declare(dyn_cast<VarDecl>(D));
    else if (const auto *Handler = dyn_cast<CXXCatchStmt>(S))
      Declare(Handler->getExceptionDecl());
    const auto *Lambda = dyn_cast<LambdaExpr>(S);
    for (const Stmt *Child : keptChildren(S, Context))
      if (Child && !(Lambda && Child == Lambda->getBody()))
        Work.push_back(Child);
  }
  return Locals;
}

/// Appends to \p Held the locals of \p Body held by value (localsOf), each
/// held where it is in scope at \p Where (inScopeAt).
static void localsHeld(const Stmt *Body,
                       llvm::SmallVectorImpl<CHeldValue> &Held,
                       ASTContext &Context, const Stmt *Where) {
  for (const VarDecl *Local : localsOf(Body, Context))
    Held.push_back({heldTypeOf(Local), ETake::Held, Local, Where, true, Local});
}

/// Whether the code of \p Closure is a coroutine, of any of its call
/// operators that can run.
static bool isCoroutineLambda(const LambdaExpr *Closure) {
  return llvm::any_of(runningOperators(Closure), [](const CXXMethodDecl *Each) {
    return isCoroutine(Each);
  });
}

/// Appends to \p Held what the code of \p Closure holds by value: with \p
/// Captures what the closure holds (closureHeld), else the parameters of its
/// call operators held by value; and the locals of the body of \p Operator,
/// the call operator that runs, or of each that can (runningOperators), in
/// scope at \p Where.
static void lambdaHeld(const LambdaExpr *Closure, const CXXMethodDecl *Operator,
                       bool Captures, llvm::SmallVectorImpl<CHeldValue> &Held,
                       ASTContext &Context, const Stmt *Where) {
  if (Captures)
    closureHeld(Closure, Held);
  else
    for (const CXXMethodDecl *Running : runningOperators(Closure))
      parametersHeld(Running, Held);
  if (Operator)
    localsHeld(Operator->getBody(), Held, Context, Where);
  else
    for (const CXXMethodDecl *Running : runningOperators(Closure))
      localsHeld(Running->getBody(), Held, Context, Where);
}

/// The function whose code \p Use is in, the innermost, by its parents
/// (\p ParentOf, then the instantiated parents of the translation unit):
/// the call operator of the lambda whose body it is in, with \p Closure
/// that lambda, or else the function whose body it is in. Null outside of a
/// function.
static const FunctionDecl *executingFunction(const Stmt *Use,
                                             FParentOf ParentOf,
                                             ASTContext &Context,
                                             const LambdaExpr *&Closure) {
  Closure = nullptr;
  DynTypedNode Node = DynTypedNode::create(*Use);
  for (int Step = 0; Step < 4096; ++Step) {
    const Stmt *Child = Node.get<Stmt>();
    DynTypedNode Parent;
    if (const Stmt *Above = Child && ParentOf ? ParentOf(Child) : nullptr)
      Parent = DynTypedNode::create(*Above);
    else if (const auto Parents = instantiatedParents(Node, Context);
             !Parents.empty())
      Parent = Parents[0];
    else
      return nullptr;
    if (const auto *Lambda = Parent.get<LambdaExpr>();
        Lambda && Child && Lambda->getBody() == Child) {
      Closure = Lambda;
      return Lambda->getCallOperator();
    }
    if (const auto *Function = Parent.get<FunctionDecl>())
      return Function;
    Node = Parent;
  }
  return nullptr;
}

/// Appends to \p Use what the code of \p Function holds by value, of the
/// lambda \p Closure, or else of the lambda whose call operator it is, what
/// its code holds (lambdaHeld: its captures unless its class is Use's NotKept, its
/// parameters and the locals of \p Function, or of each call operator that
/// can run when \p Function is the lambda's own); of a function its
/// parameters held by value and its locals. Through a lambda that isn't a
/// coroutine, also what holds the code where the lambda is, out to the
/// nearest coroutine or the outermost function. With \p Frames, each of
/// those functions is a frame of \p Use. The locals of \p Function are
/// held in scope at \p Where, those of each function around a lambda in
/// scope at the lambda.
static void nestedHeld(const FunctionDecl *Function, const LambdaExpr *Closure,
                       CStorageUse &Use, ASTContext &Context, bool Frames,
                       const Stmt *Where) {
  // What is appended from \p First on is held where \p Where is.
  auto HeldAt = [&](size_t First) {
    for (size_t iHeld = First; iHeld < Use.Held.size(); ++iHeld)
      if (!Use.Held[iHeld].Where)
        Use.Held[iHeld].Where = Where;
  };
  for (int Step = 0; Function && Step < 256; ++Step) {
    const auto *Method = dyn_cast<CXXMethodDecl>(Function);
    if (!Closure && Method && Method->getParent()->isLambda())
      Closure = lambdaOfCallOperator(Method, Context);
    if (Frames)
      Use.Frames.push_back(Function);
    const size_t First = Use.Held.size();
    if (!Closure) {
      parametersHeld(Function, Use.Held);
      localsHeld(Function->getBody(), Use.Held, Context, Where);
      HeldAt(First);
      return;
    }
    // Found by the lambda's body, each call operator of it that can run.
    const bool Each = Closure->getCallOperator() == Function;
    if (Frames && Each)
      llvm::append_range(Use.Frames, runningOperators(Closure));
    lambdaHeld(Closure, Each ? nullptr : Method,
               Closure->getLambdaClass() != Use.NotKept,
               Use.Held, Context, Where);
    HeldAt(First);
    if (Each ? isCoroutineLambda(Closure) : isCoroutine(Function))
      return;
    Function = dyn_cast_or_null<FunctionDecl>(
        Closure->getLambdaClass()->getDeclContext());
    Where = Closure;
    Closure = nullptr;
  }
}

/// The function the code of \p Closure runs in, past the lambdas around it.
static const FunctionDecl *runsInOf(const LambdaExpr *Closure) {
  const auto *Function = dyn_cast_or_null<FunctionDecl>(
      Closure->getLambdaClass()->getDeclContext());
  while (Function) {
    const auto *Method = dyn_cast<CXXMethodDecl>(Function);
    if (!Method || !Method->getParent()->isLambda())
      break;
    Function = dyn_cast_or_null<FunctionDecl>(
        Method->getParent()->getParentFunctionOrMethod());
  }
  return Function;
}

/// Whether \p Function is a member function of an actor, which runs as its
/// callback.
static bool runsOnOwnActor(const FunctionDecl *Function) {
  const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Function);
  return Method && isActorClass(Method->getParent());
}

CStorageUse closureUse(const LambdaExpr *Closure, ASTContext &Context) {
  CStorageUse Use;
  Use.Closure = Closure;
  Use.RunsIn = runsInOf(Closure);
  Use.bOnOwnActor = runsOnOwnActor(Use.RunsIn);
  nestedHeld(Closure->getCallOperator(), Closure, Use, Context,
             /*Frames=*/false, /*Where=*/nullptr);
  return Use;
}

CStorageUse useContext(const Stmt *Use, ASTContext &Context,
                       FParentOf ParentOf, const CXXRecordDecl *NotKept) {
  CStorageUse At;
  At.NotKept = NotKept;
  if (!Use)
    return At;
  const LambdaExpr *Closure = nullptr;
  const FunctionDecl *Function =
      executingFunction(Use, ParentOf, Context, Closure);
  if (!Function)
    return At;
  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function);
      !Closure && Method && Method->getParent()->isLambda())
    Closure = lambdaOfCallOperator(Method, Context);
  At.Closure = Closure;
  At.RunsIn = Closure ? runsInOf(Closure) : Function;
  At.bOnOwnActor = runsOnOwnActor(At.RunsIn);
  nestedHeld(Function, Closure, At, Context, /*Frames=*/true, Use);
  return At;
}

CStorageUse resultUse(const Expr *Made, ASTContext &Context, QualType Result) {
  CStorageUse Use;
  if (!Made)
    return Use;
  if (!Result.isNull() && !Result->isReferenceType() &&
      !Result->isDependentType()) {
    // Of another type, the result is made from it, a new value, whatever its
    // category; of its own type, a copy or a move by its category.
    const QualType Given = Made->getType();
    const bool Converted =
        !Given.isNull() && !Made->isTypeDependent() &&
        !Context.hasSameUnqualifiedType(Given.getNonReferenceType(), Result);
    Use.Held.push_back({Result, Converted ? ETake::Move : takeOf(Made, true),
                        nullptr, nullptr, true, nullptr,
                        Converted ? nullptr : Made});
  } else if (!Made->getType().isNull() && !Made->isTypeDependent())
    Use.Held.push_back({Made->getType().getNonReferenceType(),
                        takeOf(Made, true), nullptr, nullptr, true, nullptr,
                        Made});
  return Use;
}

CStorageUse argumentsUse(const CallExpr *Call, const FunctionDecl *Callee,
                         ASTContext &) {
  CStorageUse Use;
  const unsigned FirstArgument = argumentShift(Call, Callee);
  for (unsigned iArgument = FirstArgument; iArgument < Call->getNumArgs();
       ++iArgument)
    if (const unsigned iParameter = iArgument - FirstArgument;
        iParameter < Callee->getNumParams())
      if (const QualType Type = Callee->getParamDecl(iParameter)->getType();
          !Type->isReferenceType())
        Use.Held.push_back({Type, takeOf(Call->getArg(iArgument), false),
                            nullptr, nullptr, true, nullptr,
                            Call->getArg(iArgument)});
  return Use;
}

/// Where \p Function awaits the guard of \p Join early, the `co_await` that
/// RunsToCompletionJoin::EarlyAwait is in; null where it isn't found.
static const Stmt *earlyJoinOf(const RunsToCompletionJoin &Join,
                               const FunctionDecl *Function,
                               ASTContext &Context) {
  const Stmt *Found = nullptr;
  if (Join.EarlyAwait.isValid() && Function->getBody())
    forEachRunning(Function, Function->getBody(), Function->getBody(), Context,
                   [&](const Stmt *S) {
                     if (!Found && isa<CoawaitExpr>(S) &&
                         Context.getSourceManager().isPointWithin(
                             Join.EarlyAwait, S->getBeginLoc(),
                             S->getEndLoc()))
                       Found = S;
                   });
  return Found;
}

CStorageUse keptContext(const CKeptAt &Kept, ASTContext &Context) {
  switch (Kept.Kind) {
  // Where the use is, which aliveWhereUsed tells from the use.
  case CKeptAt::EKind::Local:
    return {};
  case CKeptAt::EKind::Capture:
    return Kept.Closure ? closureUse(Kept.Closure, Context) : CStorageUse{};
  case CKeptAt::EKind::Result:
    return resultUse(Kept.Made, Context, Kept.Result);
  case CKeptAt::EKind::Argument:
    return Kept.Call && Kept.Callee
               ? argumentsUse(Kept.Call, Kept.Callee, Context)
               : CStorageUse{};
  case CKeptAt::EKind::Joined: {
    if (!Kept.Join || !Kept.Site)
      return {};
    // Where it starts, what is held there that is there at the join: the
    // parameters and the locals held by value of the functions whose frames
    // are alive there that are (frameAliveAtJoin), what the closures of
    // those that are lambdas hold, and the object of a member function,
    // which the callers of the code keep until they join it.
    CStorageUse Use = useContext(Kept.Site, Context);
    Use.Held.clear();
    auto Hold = [&](const VarDecl *Variable) {
      if (const QualType Type = heldTypeOf(Variable);
          !Type.isNull() && !Variable->getType()->isDependentType() &&
          frameAliveAtJoin(Variable, *Kept.Join, Kept.Site, Kept.Closure,
                           Context))
        Use.Held.push_back(
            {Type, ETake::Held, nullptr, Kept.Site, true, Variable});
    };
    for (const FunctionDecl *Frame : Use.Frames) {
      for (const ParmVarDecl *Parameter : Frame->parameters())
        Hold(Parameter);
      for (const VarDecl *Local : localsOf(Frame->getBody(), Context))
        Hold(Local);
      const auto *Method = dyn_cast<CXXMethodDecl>(Frame);
      if (!Method || !Method->isInstance())
        continue;
      if (!Method->getParent()->isLambda()) {
        CHeldValue Object{Context.getCanonicalTagType(Method->getParent()),
                          ETake::Held};
        // Of the function that joins, its members at the join: where it
        // awaits the guard early, or its exit.
        if (Frame == Use.RunsIn) {
          Object.Where = earlyJoinOf(*Kept.Join, Frame, Context);
          if (Object.Where || Kept.Join->EarlyAwait.isInvalid())
            Object.ObjectOf = Frame;
        }
        Use.Held.push_back(Object);
      } else if (const LambdaExpr *Closure =
                   lambdaOfCallOperator(Method, Context);
               Closure && Closure->getLambdaClass() != Use.NotKept)
        closureHeld(Closure, Use.Held);
    }
    if (Kept.Closure)
      closureHeld(Kept.Closure, Use.Held);
    else if (Kept.Call && Kept.Callee)
      llvm::append_range(Use.Held,
                         argumentsUse(Kept.Call, Kept.Callee, Context).Held);
    Use.Joined = CJoined{*Kept.Join, Kept.Site, Kept.Closure};
    return Use;
  }
  }
  return {};
}

/// The anchor of what the reference or pointer parameter \p Variable refers
/// to, by its type.
static CAnchor referentAnchorOf(const ValueDecl *Variable) {
  QualType Type = Variable->getType();
  if (Type->isReferenceType())
    Type = Type.getNonReferenceType();
  else if (Type->isPointerType())
    Type = Type->getPointeeType();
  return objectAnchorOf(Type);
}

/// Whether \p Made, a temporary, is of the full expression where the callee
/// that \p Joined joins starts, which ends after the join: made in the code
/// that runs where the callee starts, also in an init capture of a closure
/// there, within that full expression (fullExpressionOf).
static bool ofJoinedFullExpression(const Expr *Made, const CJoined &Joined,
                                   ASTContext &Context) {
  const auto *Site = dyn_cast_or_null<Expr>(Joined.Site);
  if (!Site)
    return false;
  const LambdaExpr *MadeIn = nullptr;
  const LambdaExpr *SiteIn = nullptr;
  if (executingFunction(Made, {}, Context, MadeIn) !=
      executingFunction(Site, {}, Context, SiteIn))
    return false;
  const SourceManager &SM = Context.getSourceManager();
  const SourceRange Full = fullExpressionOf(Site, Context)->getSourceRange();
  return Full.isValid() &&
         SM.isPointWithin(Made->getBeginLoc(), Full.getBegin(), Full.getEnd());
}

/// isAlive of \p Root until the join of \p Use (CStorageUse::Joined), each
/// kind of storage in one place, \p Kept whether a keeper of anchors is held.
static bool
aliveAtJoin(const CStorageRoot &Root, const CStorageUse &Use,
            ASTContext &Context,
            llvm::function_ref<bool(llvm::ArrayRef<CAnchor>)> Kept) {
  const CJoined &Joined = *Use.Joined;
  switch (Root.Kind) {
  // The callers of code that runs to completion keep its object until they
  // join it.
  case ERootKind::Static:
  case ERootKind::Actor:
  case ERootKind::UnsafeThis:
  case ERootKind::Object:
    return true;
  // A local or a parameter while it is there at the join.
  case ERootKind::Frame: {
    const auto *Variable = dyn_cast_or_null<VarDecl>(Root.Variable);
    return Variable && frameAliveAtJoin(Variable, Joined.Join, Joined.Site,
                                        Joined.Started, Context);
  }
  // A temporary of the full expression where it starts, joined there.
  case ERootKind::Temporary:
    return !Joined.Join.Guard && Root.Made &&
           ofJoinedFullExpression(Root.Made, Joined, Context);
  // What a parameter refers to while the callers of its function join it,
  // or a class of CAllowUnsafeThis, or by a keeper.
  case ERootKind::Parameter: {
    const auto *Function = Root.Variable
                               ? dyn_cast_or_null<FunctionDecl>(
                                     Root.Variable->getParentFunctionOrMethod())
                               : nullptr;
    if (Function && isJoinedByCallers(Function))
      return true;
    if (Root.Class && isKnownSafeThisClass(Root.Class) &&
        !isActorClass(Root.Class))
      return true;
    return Root.Variable ? Kept(referentAnchorOf(Root.Variable)) : Kept({});
  }
  case ERootKind::Owned:
  case ERootKind::Unknown:
    return Kept(Root.Anchors);
  // What is of a thread isn't there on the thread the callee resumes on.
  case ERootKind::ThreadLocal:
    return false;
  }
  return false;
}

/// isAlive of \p Root, with \p Derivation the anchors of the derivation of
/// the use (derivationAnchors), which a keeper of fits too for each root a
/// keeper can keep.
static bool aliveBy(const CStorageRoot &Root, const CStorageUse &Use,
                    ASTContext &Context, llvm::ArrayRef<CAnchor> Derivation) {
  // A keeper of one of \p Own, or of one of the anchors of the derivation,
  // that owns what the owners the root is derived through own.
  // Of the object itself, `this` or an explicit object, which it owns.
  const CStorageRoot *Object =
      Root.Kind == ERootKind::Object ? &Root : nullptr;
  // Of what a parameter refers to, of a function one of whose parameters
  // states it is distinct from the others (DMibDistinct), that parameter.
  const auto *DistinctReferent =
      Root.Kind == ERootKind::Parameter
          ? dyn_cast_or_null<ParmVarDecl>(Root.Variable)
          : nullptr;
  if (DistinctReferent &&
      !hasDistinctParameter(
          dyn_cast<FunctionDecl>(DistinctReferent->getDeclContext())))
    DistinctReferent = nullptr;
  auto Kept = [&](llvm::ArrayRef<CAnchor> Own) {
    return holdsKeeper(Use.Held, Own, Context, Root.Hops, Object,
                       DistinctReferent) ||
           (!Derivation.empty() &&
            holdsKeeper(Use.Held, Derivation, Context, Root.Hops, Object,
                        DistinctReferent));
  };
  if (Use.Joined)
    return aliveAtJoin(Root, Use, Context, Kept);
  switch (Root.Kind) {
  case ERootKind::Static:
  case ERootKind::UnsafeThis:
    return true;
  // In its own callbacks, also in a function each call gives it to as the
  // object of the actor the caller runs on (givenOwnActor), which runs on it.
  case ERootKind::Actor: {
    if (Use.bOnOwnActor)
      return true;
    const auto *Parameter = dyn_cast_or_null<ParmVarDecl>(Root.Variable);
    const auto *Of = Parameter
                         ? dyn_cast<FunctionDecl>(Parameter->getDeclContext())
                         : nullptr;
    return Of && Use.RunsIn &&
           Of->getCanonicalDecl() == Use.RunsIn->getCanonicalDecl();
  }
  case ERootKind::Frame: {
    const auto *Variable = dyn_cast_or_null<VarDecl>(Root.Variable);
    if (!Variable)
      return false;
    // A copy the closure keeps, an init capture of it.
    if (Use.Closure && Variable->isInitCapture() &&
        Variable->getDeclContext() == Use.Closure->getCallOperator())
      return true;
    const auto *Of = dyn_cast<FunctionDecl>(Variable->getDeclContext());
    return Of && llvm::is_contained(Use.Frames, Of);
  }
  // Of what owners own, of another object, of what a parameter refers to,
  // and of what can't be told, by its anchors, a keeper by type in what the
  // context holds by value.
  case ERootKind::Owned:
  case ERootKind::Unknown:
    return Kept(Root.Anchors);
  case ERootKind::Object: {
    if (!Root.Class)
      return Kept({});
    const CAnchor Class =
        objectAnchorOf(Context.getCanonicalTagType(Root.Class));
    return Kept(Class);
  }
  // Also of a class whose objects may be referred to like `this`
  // (CAllowUnsafeThis), by its type.
  case ERootKind::Parameter: {
    if (Root.Class && isKnownSafeThisClass(Root.Class) &&
        !isActorClass(Root.Class))
      return true;
    if (!Root.Variable)
      return Kept({});
    const CAnchor Referent = referentAnchorOf(Root.Variable);
    return Kept(Referent);
  }
  // What is of a thread is alive on it only, which no context is known to
  // stay on, nor is it kept by a keeper.
  case ERootKind::ThreadLocal:
  case ERootKind::Temporary:
    return false;
  }
  return false;
}

bool isAlive(const CStorageRoot &Root, const CStorageUse &Use,
             ASTContext &Context) {
  return aliveBy(Root, Use, Context, {});
}

/// Whether there are roots and each is alive at \p Use (isAlive), or else
/// by \p OrElse.
static bool allAlive(const CStorageRoots &Roots, const CStorageUse &Use,
                     ASTContext &Context,
                     llvm::function_ref<bool(const CStorageRoot &)> OrElse,
                     llvm::ArrayRef<CAnchor> Derivation = {}) {
  return Roots.all([&](const CStorageRoot &Root) {
    return aliveBy(Root, Use, Context, Derivation) || (OrElse && OrElse(Root));
  });
}

/// The level at which the capture \p Capture of \p Closure, a copy, holds
/// what it refers to, relative to its initializer \p Init: of a pointer
/// value the level of what it points to, else above its own storage.
static unsigned copyLevelOf(const Expr *Init) {
  return Init->isGLValue() || !Init->getType()->isPointerType() ? 1 : 0;
}

CStorageRoots captureRoots(const LambdaExpr *Closure,
                           const LambdaCapture &Capture, ASTContext &Context) {
  const Expr *CaptureInit =
      Capture.capturesVariable() ? captureInitOf(Closure, Capture) : nullptr;
  if (!CaptureInit)
    return CStorageRoots{{CStorageRoot{}}};
  // What a reference refers to, of its value; what a copy refers to, a
  // pointer, a view or an object that holds them, `[Span, Data =
  // fg_Move(Data)]`, a level down, past the pointer of a pointer value. Of
  // any declaration captured, also a structured binding, by what its
  // initializer designates.
  const bool ByReference = Capture.getCaptureKind() == LCK_ByRef;
  return storageRootsOf(CaptureInit, Context,
                        ByReference ? 0 : copyLevelOf(CaptureInit));
}

namespace {
/// Of each body, the values assigned to each variable (assignedValues),
/// told once.
struct CAssignedValues {
  llvm::DenseMap<
      const Stmt *,
      llvm::DenseMap<const VarDecl *, llvm::SmallVector<const Expr *, 2>>>
      Of;
};

/// Of each expression, the anchors of its derivation (derivationAnchors),
/// told once.
struct CDerivationAnchors {
  llvm::DenseMap<const Expr *, llvm::SmallVector<CAnchor, 4>> Of;
};
} // namespace

/// The values assigned to \p Variable, a local, in the body of the function
/// it is of, also in the lambdas in it: by the walk that visits each
/// statement of the body once (forEachStmtOnce), once for each body.
static llvm::ArrayRef<const Expr *> assignedValues(const VarDecl *Variable,
                                                   ASTContext &Context) {
  const auto *Function =
      dyn_cast_or_null<FunctionDecl>(Variable->getParentFunctionOrMethod());
  const Stmt *Body = Function ? Function->getBody() : nullptr;
  if (!Body)
    return {};
  auto [It, Inserted] = astCache<CAssignedValues>(Context).Of.try_emplace(Body);
  if (Inserted) {
    auto &Assignments = It->second;
    forEachStmtOnce(Body, [&](const Stmt *S) {
      if (const auto *Assign = dyn_cast<BinaryOperator>(S);
          Assign && Assign->getOpcode() == BO_Assign)
        if (const auto *Target =
                dyn_cast<DeclRefExpr>(Assign->getLHS()->IgnoreParenImpCasts()))
          if (const auto *Assigned = dyn_cast<VarDecl>(Target->getDecl()))
            Assignments[Assigned].push_back(Assign->getRHS());
      return EWalk::Descend;
    });
  }
  const auto Found = It->second.find(Variable);
  if (Found == It->second.end())
    return {};
  return Found->second;
}

/// The value \p E makes in place, a record or an array by a constructor or an
/// initializer list, past what only wraps it: parentheses, a full expression,
/// a temporary materialized or bound, a cast that changes nothing or
/// constructs. With \p Made, each of them goes in it. Null of what makes
/// nothing.
static const Expr *
madeValueOf(const Expr *E,
            llvm::SmallPtrSetImpl<const Stmt *> *Made = nullptr) {
  llvm::SmallVector<const Expr *, 4> Chain;
  for (int Depth = 0; E && Depth < 64; ++Depth) {
    Chain.push_back(E);
    if (isa<CXXConstructExpr, InitListExpr, CXXParenListInitExpr>(E) &&
        !E->getType()->isDependentType()) {
      if (Made)
        Made->insert(Chain.begin(), Chain.end());
      return E;
    }
    if (const auto *Full = dyn_cast<FullExpr>(E))
      E = Full->getSubExpr();
    else if (const auto *Paren = dyn_cast<ParenExpr>(E))
      E = Paren->getSubExpr();
    else if (const auto *Temporary = dyn_cast<MaterializeTemporaryExpr>(E))
      E = Temporary->getSubExpr();
    else if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(E))
      E = Bind->getSubExpr();
    else if (const auto *Cast = dyn_cast<CastExpr>(E);
             Cast && (isa<CXXFunctionalCastExpr>(Cast) ||
                      Cast->getCastKind() == CK_NoOp ||
                      Cast->getCastKind() == CK_ConstructorConversion))
      E = Cast->getSubExpr();
    else
      return nullptr;
  }
  return nullptr;
}

/// The direct initializers of \p Made, a value made in place (madeValueOf):
/// the arguments of its constructor, the elements of its initializer list.
static llvm::ArrayRef<const Expr *> directInitializersOf(const Expr *Made) {
  if (const auto *Construct = dyn_cast_or_null<CXXConstructExpr>(Made))
    return llvm::ArrayRef(Construct->getArgs(), Construct->getNumArgs());
  if (const auto *List = dyn_cast_or_null<InitListExpr>(Made))
    return llvm::ArrayRef(List->getInits(), List->getNumInits());
  if (const auto *List = dyn_cast_or_null<CXXParenListInitExpr>(Made))
    return List->getInitExprs();
  return {};
}

llvm::ArrayRef<CAnchor> derivationAnchors(const Expr *Derivation,
                                          ASTContext &Context) {
  if (!Derivation)
    return {};
  auto &Cache = astCache<CDerivationAnchors>(Context).Of;
  if (auto It = Cache.find(Derivation); It != Cache.end())
    return It->second;
  llvm::SmallVector<CAnchor, 4> Anchors;
  auto Add = [&](const CAnchor &Anchor) {
    if (Anchor.Of && !llvm::is_contained(Anchors, Anchor))
      Anchors.push_back(Anchor);
  };
  // Of a value made in the expression that can hold owners, a result, an
  // argument or an init capture: a direct initializer of it, also of a record
  // made in place in it, whose type holds owners and can't hold a pointer, a
  // reference or a view is a keeper put in, which isn't on the derivation of
  // what it keeps; nor is the type of what is made in place (madeValueOf).
  // Another direct initializer is on it in full, the owners in it too.
  auto HoldsOwners = [&](const Expr *Of) {
    const QualType Type = Of->getType().getNonReferenceType();
    return !Type.isNull() && !Type->isDependentType() &&
           holdsOwners(Type, Context);
  };
  llvm::SmallPtrSet<const Stmt *, 8> Made;
  llvm::SmallPtrSet<const Stmt *, 4> Keepers;
  llvm::SmallVector<const Expr *, 4> MadeWork{Derivation};
  while (!MadeWork.empty()) {
    const Expr *Each = MadeWork.pop_back_val();
    const Expr *Value = Made.contains(Each) ? nullptr : madeValueOf(Each);
    if (!Value || !HoldsOwners(Value))
      continue;
    madeValueOf(Each, &Made);
    for (const Expr *Initializer : directInitializersOf(Value))
      if (HoldsOwners(Initializer) &&
          !holdsBorrowed(Initializer->getType().getNonReferenceType(), Context))
        Keepers.insert(Initializer);
      else if (madeValueOf(Initializer))
        MadeWork.push_back(Initializer);
  }
  // Each expression once (CStmtWalk), at most 4096.
  CStmtWalk Walk(Derivation);
  unsigned nVisited = 0;
  while (const Stmt *S = Walk.next()) {
    const auto *E = dyn_cast<Expr>(S);
    if (!E || Keepers.contains(E))
      continue;
    if (++nVisited > 4096)
      break;
    Walk.addChildren(E);
    if (const auto *Opaque = dyn_cast<OpaqueValueExpr>(E))
      Walk.add(Opaque->getSourceExpr());
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
      if (const auto *Binding = dyn_cast<BindingDecl>(Ref->getDecl()))
        Walk.add(Binding->getBinding());
      if (const auto *Variable = dyn_cast<VarDecl>(Ref->getDecl());
          Variable && !isa<ParmVarDecl>(Variable) &&
          (Variable->isInitCapture() ||
           (Variable->hasLocalStorage() &&
            (Variable->getType()->isReferenceType() ||
             Variable->getType()->isPointerType())))) {
        Walk.add(Variable->getInit());
        for (const Expr *Assigned : assignedValues(Variable, Context))
          Walk.add(Assigned);
      }
    }
    const QualType Type = E->getType();
    if (Type.isNull() || E->isTypeDependent() || Type->isDependentType() ||
        Type->isVoidType() || Type->isFunctionType() || Made.contains(E))
      continue;
    if (E->isGLValue()) {
      Add(objectAnchorOf(Type));
      continue;
    }
    if (Type->isPointerType()) {
      const QualType Pointee = Type->getPointeeType();
      if (!Pointee->isVoidType() && !Pointee->isFunctionType() &&
          !Pointee->isDependentType())
        Add(objectAnchorOf(Pointee));
    }
  }
  // Of each type on it, the anchors of the owners it holds by value, its
  // parts, the one holder classification (keepersOf), not of itself.
  for (size_t iAnchor = 0, nTypes = Anchors.size(); iAnchor < nTypes;
       ++iAnchor) {
    const QualType Of(Anchors[iAnchor].Of, 0);
    const std::optional<CAnchor> Own = ownerAnchorOf(Of, Context);
    const EOwnerKind OwnKind = ownerKindOf(Of, Context);
    for (const std::pair<EOwnerKind, CAnchor> &Keeper : keepersOf(Of, Context))
      if (!Own || !(Keeper.second == *Own) || Keeper.first != OwnKind)
        Add(Keeper.second);
  }
  return Cache[Derivation] = std::move(Anchors);
}

/// Where what \p Use reaches at \p Level lives (storageRootsOf); of the
/// name of a variable that a closure around the use, from \p Closure out,
/// captures by copy, or of a member of it, what the copy refers to, of the
/// capture's initializer, or with \p CopyIn set to that closure the copy
/// itself, which lives in it.
static CStorageRoots rootsAtUse(const Expr *Use, unsigned Level,
                                const LambdaExpr *Closure, ASTContext &Context,
                                const LambdaExpr *&CopyIn) {
  CopyIn = nullptr;
  const Expr *Named = Use->IgnoreParens();
  unsigned NamedLevel = Level;
  // Whether the way reads a reference member of what it names, which
  // designates what that refers to.
  bool ThroughReference = false;
  if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Named);
      Cast && Cast->getCastKind() == CK_LValueToRValue &&
      Cast->getType()->isPointerType()) {
    Named = Cast->getSubExpr()->IgnoreParens();
    ++NamedLevel;
  }
  for (int Step = 0; Step < 64; ++Step)
    if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Named);
        Cast && Cast->getCastKind() == CK_NoOp)
      Named = Cast->getSubExpr()->IgnoreParens();
    else if (const auto *Member = dyn_cast<MemberExpr>(Named);
             Member && !Member->isArrow() &&
             isa<FieldDecl>(Member->getMemberDecl())) {
      ThroughReference |=
          Member->getMemberDecl()->getType()->isReferenceType();
      Named = Member->getBase()->IgnoreParens();
    } else
      break;
  const auto *Ref = dyn_cast<DeclRefExpr>(Named);
  for (const LambdaExpr *Around = Closure;
       Ref && Ref->refersToEnclosingVariableOrCapture() && Around;) {
    for (const LambdaCapture &Capture : Around->captures()) {
      if (!Capture.capturesVariable() ||
          Capture.getCapturedVar() != Ref->getDecl())
        continue;
      if (Capture.getCaptureKind() != LCK_ByCopy)
        return storageRootsOf(Use, Context, Level);
      // Of a copy of the record a local reference refers to, through a
      // reference member, what its parts refer to, which the copy shares
      // (borrowedPartRoots), `Ref.m_Ref` of `[Ref]`.
      if (const auto *Copied = dyn_cast<VarDecl>(Ref->getDecl());
          ThroughReference && Copied &&
          Copied->getType()->isReferenceType() && Copied->getInit())
        return borrowedPartRoots(Copied->getInit(),
                                 dyn_cast_or_null<FunctionDecl>(
                                     Copied->getParentFunctionOrMethod()),
                                 Context);
      if (NamedLevel == 0) {
        CopyIn = Around;
        return {};
      }
      const Expr *Init = captureInitOf(Around, Capture);
      if (!Init)
        return CStorageRoots{{CStorageRoot{}}};
      const unsigned Offset = 1 - copyLevelOf(Init);
      return storageRootsOf(Init, Context,
                            NamedLevel >= Offset ? NamedLevel - Offset : 0);
    }
    const auto *Operator = dyn_cast_or_null<CXXMethodDecl>(
        Around->getLambdaClass()->getDeclContext());
    Around = Operator && Operator->getParent()->isLambda()
                 ? lambdaOfCallOperator(Operator, Context)
                 : nullptr;
  }
  return storageRootsOf(Use, Context, Level);
}

bool aliveAtUse(const Expr *Use, unsigned Level, const CStorageUse &At,
                ASTContext &Context, const Expr *Derivation,
                llvm::function_ref<bool(const CStorageRoot &)> OrElse) {
  if (!Use)
    return false;
  const LambdaExpr *CopyIn = nullptr;
  const CStorageRoots Roots =
      rootsAtUse(Use, Level, At.Closure, Context, CopyIn);
  return (CopyIn && CopyIn->getLambdaClass() != At.NotKept) ||
         allAlive(Roots, At, Context, OrElse,
                  derivationAnchors(Derivation ? Derivation : Use, Context));
}

/// The expression \p E is directly in, by \p ParentOf, or else by the
/// instantiated parents of the translation unit; null past a statement.
static const Expr *parentExprOf(const Expr *E, FParentOf ParentOf,
                                ASTContext &Context) {
  if (ParentOf)
    return dyn_cast_or_null<Expr>(ParentOf(E));
  const auto Parents = instantiatedParents(*E, Context);
  return Parents.empty() ? nullptr : Parents[0].get<Expr>();
}

/// The node \p E is directly in, an expression, a statement or a
/// declaration, by \p ParentOf, or else by the instantiated parents of the
/// translation unit with the implicit nodes (TK_AsIs), whatever the traversal
/// of the check that asks, so that each step of an access path is one node.
static DynTypedNode pathParentOf(const Expr *E, FParentOf ParentOf,
                                 ASTContext &Context) {
  if (ParentOf) {
    const Stmt *Parent = ParentOf(E);
    return Parent ? DynTypedNode::create(*Parent) : DynTypedNode();
  }
  const TraversalKindScope AsIs(Context, TK_AsIs);
  const auto Parents = instantiatedParents(*E, Context);
  return Parents.empty() ? DynTypedNode() : Parents[0];
}

/// The statement \p S is directly in, by \p ParentOf, or else by the
/// instantiated parents with the implicit nodes.
static const Stmt *parentStmtOf(const Stmt *S, FParentOf ParentOf,
                                ASTContext &Context) {
  if (ParentOf)
    return ParentOf(S);
  const TraversalKindScope AsIs(Context, TK_AsIs);
  const auto Parents = instantiatedParents(*S, Context);
  return Parents.empty() ? nullptr : Parents[0].get<Stmt>();
}

namespace {
/// What a step of an access path reads (accessPathOf): what \p Read
/// reaches at \p Level, where the use as written is reached at \p Relative
/// from it at level 0, unless the way to it goes through what a call whose
/// body can't be seen gives, which provenance tells.
struct CAccessRead {
  const Expr *Read = nullptr;
  unsigned Level = 0;
  unsigned Relative = 0;
  /// Whether a suspension reads it when it resumes, `co_await Awaiter`.
  bool bResumed = false;
};
/// The access path of a use (accessPathOf): its outermost expression, the
/// level at which the use as written is reached from it at level 0, and
/// what the steps on it read.
struct CAccessPath {
  const Expr *Top = nullptr;
  unsigned TopLevel = 0;
  llvm::SmallVector<CAccessRead, 2> Reads;
  /// The outermost expression the use's value is handed on to unchanged by
  /// the steps from it (valueEndOf), and what that is in.
  const Expr *ValueTop = nullptr;
  DynTypedNode ValueConsumer;
};
/// No level relative to the use as written (CAccessRead::Relative).
constexpr unsigned UntoldRelative = ~0u >> 1;
} // namespace

/// Whether \p Above is \p E or holds it through implicit nodes and
/// parentheses only, which the parents of a traversal of what is spelled
/// skip.
static bool implicitlyAbove(const Expr *Above, const Expr *E) {
  for (int Step = 0; Above && Step < 16; ++Step) {
    if (Above == E)
      return true;
    if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Above))
      Above = Cast->getSubExpr();
    else if (const auto *Paren = dyn_cast<ParenExpr>(Above))
      Above = Paren->getSubExpr();
    else if (const auto *Full = dyn_cast<FullExpr>(Above))
      Above = Full->getSubExpr();
    else if (const auto *Temporary = dyn_cast<MaterializeTemporaryExpr>(Above))
      Above = Temporary->getSubExpr();
    else if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(Above))
      Above = Bind->getSubExpr();
    else
      return false;
  }
  return false;
}

/// Whether the step of the provenance from \p Above at \p AboveLevel to \p
/// Below at \p Level, a part of it or not by \p Part, hands Below's value on
/// unchanged: not a part, at the same level and value category, or a load of
/// it; not what an operator or a cast does something else with
/// (pointerOperationOf), `*p`, nor a write or an update of it, `p = q`,
/// `++p`.
static bool handsOnUnchanged(const Expr *Above, unsigned AboveLevel,
                             const Expr *Below, unsigned Level, bool Part,
                             const ASTContext &Context) {
  if (Part)
    return false;
  const bool Same =
      Level == AboveLevel && Above->isGLValue() == Below->isGLValue();
  const bool Load =
      Level == AboveLevel + 1 && Below->isGLValue() && !Above->isGLValue();
  if (!Same && !Load)
    return false;
  std::optional<EPointerOperation> Operation;
  if (const auto *Cast = dyn_cast<CastExpr>(Above))
    Operation = pointerOperationOf(Cast->getCastKind());
  else if (const auto *Unary = dyn_cast<UnaryOperator>(Above))
    Operation = pointerOperationOf(Unary->getOpcode());
  else if (const auto *Binary = dyn_cast<BinaryOperator>(Above))
    Operation = pointerOperationOf(Binary->getOpcode());
  if (Operation && (*Operation == EPointerOperation::Compare ||
                    *Operation == EPointerOperation::Offset ||
                    *Operation == EPointerOperation::Dereference))
    return false;
  return llvm::none_of(effectsOf(Above, Context).Operands,
                       [&](const COperandEffect &Effect) {
                         return Effect.Operand == Below &&
                                (Effect.Use == EUse::Write ||
                                 Effect.Use == EUse::Update);
                       });
}

/// The access path of \p Use, the inverse of the provenance's step: from
/// the use, each parent whose provenance at level 0 derives from what is
/// below it by the step itself (CProvenance::step), also through the
/// implicit nodes between them; of the object of a member function the
/// call. The outermost. What a step reaches levels up is read at each level
/// below those, a load at the storage of what it loads, a call at what its
/// body reads on the way to what it gives. Where the steps from the use stop
/// handing its value on unchanged (handsOnUnchanged), the value's end; with
/// \p ValueOnly, the path up to there only, without what it reads.
static CAccessPath accessPathOf(const Expr *Use, FParentOf ParentOf,
                                ASTContext &Context, bool ValueOnly = false) {
  CAccessPath Path;
  Path.Top = Use;
  Path.ValueTop = Use;
  bool Value = true;
  // The value stops being handed on unchanged at the top, which \p Consumer
  // does something else with.
  auto EndValue = [&](const DynTypedNode &Consumer) {
    if (!Value)
      return;
    Path.ValueTop = Path.Top;
    Path.ValueConsumer = Consumer;
    Value = false;
  };
  const CProvenance Walk(Context);
  for (int Step = 0; Step < 64; ++Step) {
    DynTypedNode Node = pathParentOf(Path.Top, ParentOf, Context);
    const Expr *Parent = Node.get<Expr>();
    // A bound member function isn't a value: the object it is bound to is
    // an operand of the call it is the callee of.
    if (const Expr *Callee = Parent;
        Callee && Callee->hasPlaceholderType(BuiltinType::BoundMember)) {
      const Expr *Above = pathParentOf(Callee, ParentOf, Context).get<Expr>();
      for (int Paren = 0; isa_and_nonnull<ParenExpr>(Above) && Paren < 16;
           ++Paren)
        Above = pathParentOf(Above, ParentOf, Context).get<Expr>();
      if (const auto *Call = dyn_cast_or_null<CallExpr>(Above);
          Call && Call->getCallee()->IgnoreParens() == Callee) {
        Parent = Call;
        Node = DynTypedNode::create(*Call);
      }
    }
    if (!Parent) {
      EndValue(Node);
      break;
    }
    // The step from the parent at level 0 down to the top, each expression
    // on the way with its level; whether each hands on its value unchanged.
    llvm::SmallVector<std::pair<const Expr *, unsigned>, 4> Way;
    bool Unchanged = true;
    for (std::pair<const Expr *, unsigned> At{Parent, 0};
         At.first != Path.Top && Way.size() < 16;) {
      std::optional<std::pair<const Expr *, unsigned>> Below;
      bool BelowPart = false;
      bool Complete = true;
      Walk.step(
          At.first, At.second, false,
          [&](const Expr *Next, unsigned Level, bool Part) {
            if (!Below && Next && Next != At.first &&
                implicitlyAbove(Next, Path.Top)) {
              Below.emplace(Next, Level);
              BelowPart = Part;
            }
          },
          Complete);
      if (!Below)
        break;
      Unchanged = Unchanged &&
                  handsOnUnchanged(At.first, At.second, Below->first,
                                   Below->second, BelowPart, Context);
      Way.push_back(*Below);
      At = *Below;
    }
    if (Way.empty() || Way.back().first != Path.Top) {
      EndValue(Node);
      break;
    }
    if (!Unchanged)
      EndValue(Node);
    if (ValueOnly && !Value)
      break;
    // What is reached levels up is read at each level below them. Past a
    // call whose body can't be seen, which provenance tells, no level
    // relative to the use is told.
    const unsigned Reached = Way.back().second;
    auto Unseen = [&](const Expr *E) {
      const auto *Call = dyn_cast<CallExpr>(E);
      return Call && (!Call->getDirectCallee() ||
                      Walk.calledRoots(Call, Call->getDirectCallee()).open());
    };
    // The resume of a suspension, `await_resume()`, reads when it resumes.
    const auto *Suspend = dyn_cast_or_null<CoroutineSuspendExpr>(
        pathParentOf(Parent, ParentOf, Context).get<Expr>());
    const bool Resumed =
        Suspend && implicitlyAbove(Suspend->getResumeExpr(), Parent);
    llvm::SmallVector<bool, 4> Untold(Way.size());
    bool Below = Path.TopLevel == UntoldRelative;
    for (size_t iStep = Way.size(); iStep-- > 0;) {
      Below = Below || (iStep + 1 < Way.size() && Unseen(Way[iStep].first));
      Untold[iStep] = Below;
    }
    // A call whose body can't be seen uses what it is called on or given,
    // at its own storage.
    unsigned Above = 0;
    for (size_t iStep = 0; iStep < Way.size(); ++iStep) {
      const auto &[Each, Level] = Way[iStep];
      const unsigned Relative =
          Untold[iStep] ? UntoldRelative : Path.TopLevel + Reached - Level;
      const bool Called = Unseen(iStep ? Way[iStep - 1].first : Parent);
      for (unsigned Read = 0; Read + Above < Level || (Called && Read == 0);
           ++Read)
        Path.Reads.push_back({Each, Read, Relative, Resumed});
      Above = Level;
    }
    const bool TopUntold = (!Untold.empty() && Untold[0]) || Unseen(Parent);
    Path.Top = Parent;
    Path.TopLevel = TopUntold ? UntoldRelative : Path.TopLevel + Reached;
  }
  if (Value)
    Path.ValueTop = Path.Top;
  return Path;
}

/// What \p Consumer does with \p Top, where a value stops being handed on
/// unchanged (valueEndOf): an operator or a cast what it does with a pointer
/// (pointerOperationOf), a conversion that keeps the value \p Converts;
/// anything else what the effect table says it does with it.
static EValueEnd valueEndKindOf(const DynTypedNode &Consumer, const Expr *Top,
                                const ASTContext &Context, bool &Converts) {
  Converts = false;
  std::optional<EPointerOperation> Operation;
  if (const auto *Cast = Consumer.get<CastExpr>())
    Operation = pointerOperationOf(Cast->getCastKind());
  else if (const auto *Unary = Consumer.get<UnaryOperator>())
    Operation = pointerOperationOf(Unary->getOpcode());
  else if (const auto *Binary = Consumer.get<BinaryOperator>())
    Operation = pointerOperationOf(Binary->getOpcode());
  if (Operation) {
    switch (*Operation) {
    case EPointerOperation::Compare:
      return EValueEnd::Compared;
    case EPointerOperation::Offset:
      return EValueEnd::Offset;
    case EPointerOperation::Dereference:
      return EValueEnd::Dereferenced;
    case EPointerOperation::Convert:
      Converts = !Consumer.get<BinaryOperator>();
      return EValueEnd::Kept;
    case EPointerOperation::None:
      return EValueEnd::Kept;
    }
  }
  const Stmt *In = Consumer.get<Stmt>();
  if (!In || !Top)
    return EValueEnd::Kept;
  bool Dereferenced = false;
  bool Given = false;
  bool Tested = false;
  bool Other = false;
  for (const COperandEffect &Effect : effectsOf(In, Context).Operands) {
    if (Effect.Operand != Top)
      continue;
    switch (Effect.Use) {
    case EUse::Dereference:
      Dereferenced = true;
      break;
    case EUse::CallObject:
      Given = true;
      break;
    case EUse::Bind:
    case EUse::Copy:
      Given = Given || Effect.Into.Kind == EDestination::Parameter;
      Other = Other || Effect.Into.Kind != EDestination::Parameter;
      break;
    case EUse::Condition:
      Tested = true;
      break;
    case EUse::Forward:
    case EUse::Access:
    case EUse::Update:
    case EUse::Write:
    case EUse::AddressOf:
    case EUse::Callee:
    case EUse::Value:
    case EUse::Discarded:
    case EUse::Statement:
    case EUse::Unevaluated:
    case EUse::Opaque:
      Other = true;
      break;
    }
  }
  if (Dereferenced)
    return EValueEnd::Dereferenced;
  if (Given)
    return EValueEnd::Given;
  return Tested && !Other ? EValueEnd::Compared : EValueEnd::Kept;
}

CValueEnd valueEndOf(const Expr *Use, FParentOf ParentOf, ASTContext &Context) {
  CValueEnd End;
  const Expr *From = Use;
  for (int Conversion = 0; From && Conversion < 8; ++Conversion) {
    const CAccessPath Path =
        accessPathOf(From, ParentOf, Context, /*ValueOnly=*/true);
    End.Top = Path.ValueTop;
    End.Consumer = Path.ValueConsumer;
    bool Converts = false;
    End.Kind = valueEndKindOf(End.Consumer, End.Top, Context, Converts);
    if (!Converts)
      break;
    // What it is converted to is what is used on.
    End.bConverted = true;
    From = End.Consumer.get<Expr>();
  }
  const QualType Type = Use->getType();
  End.Uses = storageUsesOf(
      Use, Use->isGLValue() && Type->isPointerType() ? 1 : 0, Context,
      ParentOf);
  return End;
}

namespace {
/// A name of a declaration in a body (CNameIndex), with the innermost lambda
/// around it there, null outside of each.
struct CName {
  const Expr *Name = nullptr;
  const LambdaExpr *Closure = nullptr;
};

/// The names of the declarations in each body, and of `this` by a null
/// declaration, told once by the one walker
/// (forEachStmtOnce), not in what doesn't run, and whether each closure is
/// called in the scope that makes it (isCalledInScope).
struct CNameIndex {
  llvm::DenseMap<const Stmt *,
                 llvm::DenseMap<const Decl *, llvm::SmallVector<CName, 2>>>
      Of;
  llvm::DenseMap<const LambdaExpr *, bool> CalledInScope;
};
} // namespace

/// The names of \p Named in \p Body, of `this` where null (CNameIndex).
static llvm::SmallVector<CName, 4> namesIn(const ValueDecl *Named,
                                           const Stmt *Body,
                                           ASTContext &Context) {
  auto &Of = astCache<CNameIndex>(Context).Of;
  auto [It, bNew] = Of.try_emplace(Body);
  if (bNew) {
    auto &Names = It->second;
    // Each lambda's captures are named where it is made, its body in it.
    llvm::SmallVector<std::pair<const Stmt *, const LambdaExpr *>, 4> Roots{
        {Body, nullptr}};
    while (!Roots.empty()) {
      const auto Next = Roots.pop_back_val();
      const Stmt *Root = Next.first;
      const LambdaExpr *Closure = Next.second;
      forEachStmtOnce(Root, [&](const Stmt *S) {
        if (const auto *Lambda = dyn_cast<LambdaExpr>(S)) {
          for (const Expr *Init : captureInitsOf(Lambda))
            if (Init)
              Roots.push_back({Init, Closure});
          Roots.push_back({Lambda->getBody(), Lambda});
          return EWalk::Skip;
        }
        if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
          Names[Ref->getDecl()].push_back({Ref, Closure});
        else if (isa<CXXThisExpr>(S))
          Names[nullptr].push_back({cast<Expr>(S), Closure});
        return EWalk::Descend;
      });
    }
  }
  const auto Found = It->second.find(Named);
  if (Found == It->second.end())
    return {};
  return llvm::SmallVector<CName, 4>(Found->second.begin(),
                                     Found->second.end());
}

/// Whether \p Closure is called in the scope that makes it
/// (isCalledInScope), told once.
static bool calledInScope(const LambdaExpr *Closure, ASTContext &Context) {
  if (const auto Found =
          astCache<CNameIndex>(Context).CalledInScope.find(Closure);
      Found != astCache<CNameIndex>(Context).CalledInScope.end())
    return Found->second;
  const auto Consumers = closureConsumers(Closure, Context, {});
  const bool bCalled = !Consumers.empty() && isCalledInScope(Consumers);
  astCache<CNameIndex>(Context).CalledInScope[Closure] = bCalled;
  return bCalled;
}

/// Whether \p E is in the body of \p Closure, by the instantiated parents
/// with the implicit nodes.
static bool isInClosure(const Expr *E, const LambdaExpr *Closure,
                        ASTContext &Context) {
  const TraversalKindScope AsIs(Context, TK_AsIs);
  DynTypedNode Node = DynTypedNode::create(*E);
  for (int Steps = 0; Steps < 4096; ++Steps) {
    if (Node.get<Stmt>() == Closure->getBody())
      return true;
    if (Node.get<LambdaExpr>() == Closure)
      return false;
    const auto Parents = instantiatedParents(Node, Context);
    if (Parents.empty())
      return false;
    Node = Parents[0];
  }
  return false;
}

/// The range-based `for` whose range \p Variable is, `auto &&__range =
/// Range;`.
static const CXXForRangeStmt *rangeLoopOf(const VarDecl *Variable,
                                          ASTContext &Context) {
  if (!Variable->isImplicit())
    return nullptr;
  const TraversalKindScope AsIs(Context, TK_AsIs);
  const auto Above = instantiatedParents(*Variable, Context);
  const auto *Declaration = Above.empty() ? nullptr : Above[0].get<DeclStmt>();
  if (!Declaration)
    return nullptr;
  const auto Loops = instantiatedParents(*Declaration, Context);
  const auto *Loop =
      Loops.empty() ? nullptr : Loops[0].get<CXXForRangeStmt>();
  return Loop && Loop->getRangeStmt() == Declaration ? Loop : nullptr;
}

/// The function the names of \p Named are in: of a local, a parameter or
/// an init capture the function it is in, of a binding that of what it
/// decomposes.
static const FunctionDecl *functionNaming(const ValueDecl *Named) {
  if (const auto *Binding = dyn_cast<BindingDecl>(Named))
    Named = Binding->getDecomposedDecl();
  const auto *Variable = dyn_cast_or_null<VarDecl>(Named);
  return Variable ? dyn_cast_or_null<FunctionDecl>(
                        Variable->getParentFunctionOrMethod())
                  : nullptr;
}

/// The body the names of \p Named are in (functionNaming).
static const Stmt *bodyNaming(const ValueDecl *Named) {
  const FunctionDecl *Function = functionNaming(Named);
  return Function ? Function->getBody() : nullptr;
}

namespace {
/// The members of a record, the first first, through which a part of it
/// holds the storage a walk of storageUsesOf follows: the member Field, and
/// the part of it Next; a null Next of a member, and a null path of a
/// record, is any part of it that can hold a pointer, a reference or a view,
/// where what the part holds isn't told. One without a Field (wholePath) is
/// the record as a whole, which holds it by what made it (Rule R), any part
/// of it too. Elements of an array and bases add no step: an element is the
/// array's part, a base's members are named through the derived record.
struct CMemberPath {
  const FieldDecl *Field = nullptr;
  const CMemberPath *Next = nullptr;
};

/// The member paths of a translation unit, each made once, so a path is
/// told by its address (memberPath).
struct CMemberPaths {
  llvm::DenseMap<std::pair<const FieldDecl *, const CMemberPath *>,
                 const CMemberPath *>
      Of;
  std::deque<CMemberPath> Nodes;
};
} // namespace

/// The path of the member \p Field and then \p Next, made once per
/// translation unit; any part (null) past a depth of 8.
static const CMemberPath *memberPath(const FieldDecl *Field,
                                     const CMemberPath *Next,
                                     ASTContext &Context) {
  unsigned Depth = 0;
  for (const CMemberPath *Each = Next; Each; Each = Each->Next)
    if (++Depth >= 8)
      return nullptr;
  CMemberPaths &Paths = astCache<CMemberPaths>(Context);
  auto [It, bNew] = Paths.Of.try_emplace({Field, Next}, nullptr);
  if (bNew) {
    Paths.Nodes.push_back({Field, Next});
    It->second = &Paths.Nodes.back();
  }
  return It->second;
}

/// The object a copy or move assignment \p Call assigns, `Info` of `Info =
/// Other`; null of what isn't one.
static const Expr *assignedObject(const Stmt *Call) {
  if (const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call))
    return Operator->getNumArgs() > 0 ? Operator->getArg(0) : nullptr;
  if (const auto *Member = dyn_cast_or_null<CXXMemberCallExpr>(Call))
    return Member->getImplicitObjectArgument();
  return nullptr;
}

/// The record of the frame, a variable that isn't a reference, of a value of
/// records (isValueRecordType), that \p Target designates a member of, also
/// of an element of an array member of it (designatedSlots), with the path of
/// the members on the way in \p Held; null of anything else.
static const VarDecl *recordOfMember(const Expr *Target,
                                     const CMemberPath *&Held,
                                     ASTContext &Context) {
  // The one slot it designates (designatedSlots), also through a local
  // reference or pointer to the record, a member of the record by its path.
  bool bTold = true;
  const llvm::SmallVector<CSlot, 2> Slots =
      designatedSlots(Target, Context, &bTold);
  if (!bTold || Slots.size() != 1 || Slots.front().Path.empty())
    return nullptr;
  const CSlot &Slot = Slots.front();
  const VarDecl *Variable = Slot.Variable;
  if (!Variable || !Variable->hasLocalStorage() ||
      Variable->getType()->isReferenceType() ||
      !isValueRecordType(Variable->getType(), Context))
    return nullptr;
  Held = nullptr;
  for (const FieldDecl *Field : llvm::reverse(Slot.Path))
    Held = memberPath(Field, Held, Context);
  return Variable;
}

/// The path of a record that holds the storage as a whole (CMemberPath).
static const CMemberPath *wholePath(ASTContext &Context) {
  return memberPath(nullptr, nullptr, Context);
}

/// What \p Use does to the storage at \p Level of it (storageUsesOf), of a
/// name of a parameter or of `this` in its body when \p bBody: what a
/// `return` gives back is returned, not kept. Of a record that holds it,
/// through the part \p Path of it (CMemberPath). With \p bNumberUse, \p Use
/// is an integer made of the value of a pointer that points to it there.
static CStorageUses usesOf(const Expr *Use, unsigned Level,
                           ASTContext &Context, FParentOf ParentOf, bool bBody,
                           FKeptAt KeptAt, const CMemberPath *Path = nullptr,
                           bool bNumberUse = false);

/// The levels of pointer \p Type has past a reference, what a name of a
/// parameter of it can be asked at; with \p bPart, of what a part of a
/// record that isn't a view nor an owner (isValueRecordType) the walk knows
/// holds (CMemberPath), one more.
static unsigned levelsOf(QualType Type, const ASTContext &Context,
                         bool bPart) {
  Type = Type.getNonReferenceType();
  unsigned Levels = 0;
  while (const auto *Pointer = Type->getAs<PointerType>()) {
    ++Levels;
    Type = Pointer->getPointeeType();
  }
  return bPart && isValueRecordType(Type, Context) ? Levels + 1 : Levels;
}

/// Whether a value of \p Type can't refer to storage: it can't hold a
/// pointer, a reference or a view (holdsBorrowed), nor own one.
static bool refersNowhere(QualType Type, const ASTContext &Context) {
  return !Type->isDependentType() && !holdsBorrowed(Type, Context) &&
         ownerKindOf(Type, Context) == EOwnerKind::None;
}

/// The level at which what is made of or bound to \p E, which has the
/// storage at \p At, holds it, given by reference when \p Bound: a copy of a
/// record at its level; a pointer or a reference at the level of what it
/// refers to; a record, \p Record, a level above that.
static unsigned heldLevelOf(const Expr *E, bool Bound, unsigned At,
                            bool Record) {
  if (!Bound && E->getType()->isRecordType())
    return At;
  const unsigned Referred = Bound || E->isPRValue() || At == 0 ? At : At - 1;
  return Record ? Referred + 1 : Referred;
}

namespace {
/// What the names of each parameter, and of `this` of each member function,
/// do in its body (parameterUses), and those asked, of a budget for the
/// translation unit.
struct CParameterUses {
  using CKey = std::pair<std::pair<const Decl *, unsigned>,
                         std::pair<const CMemberPath *, ASTContext *>>;
  static constexpr unsigned Budget = 16384;
  llvm::DenseSet<CKey> Asked;
  TCSummaries<CKey, CStorageUses> Of{
      [](const CKey &Key) {
        CStorageUses Uses;
        ASTContext &Context = *Key.second.second;
        const auto *Parameter = dyn_cast<ParmVarDecl>(Key.first.first);
        const auto *Function =
            Parameter ? dyn_cast_or_null<FunctionDecl>(
                            Parameter->getParentFunctionOrMethod())
                      : dyn_cast<FunctionDecl>(Key.first.first);
        const Stmt *Body = Function ? Function->getBody() : nullptr;
        if (!Body) {
          Uses.bGiven = true;
          return Uses;
        }
        // The body, and of a constructor each initializer.
        llvm::SmallVector<const Stmt *, 4> Bodies{Body};
        if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Function))
          for (const CXXCtorInitializer *Initializer : Constructor->inits())
            if (Initializer->getInit())
              Bodies.push_back(Initializer->getInit());
        for (const Stmt *In : Bodies)
          for (const CName &Name : namesIn(Parameter, In, Context)) {
            // In a closure that isn't called in the scope that makes it,
            // what it is copied into is kept.
            if (Name.Closure && !calledInScope(Name.Closure, Context)) {
              Uses.bKept = true;
              continue;
            }
            // A `return` in a closure called in the scope gives back from
            // the closure, not from the body.
            Uses |= usesOf(Name.Name, Key.first.second, Context, {},
                           /*bBody=*/!Name.Closure, {}, Key.second.first);
          }
        // What names it in the body ends with the call.
        Uses.bHeld = false;
        return Uses;
      },
      CStorageUses{}, CStorageUses{true, true, true, true, 0}};
};
} // namespace

/// What the names of the parameter \p Parameter, or of `this` of the member
/// function \p Parameter, do in its body to the storage at \p Level of it
/// (usesOf), of a record through the part \p Path of it, worked out once,
/// recursion through the summary engine; none past the levels its type has
/// (of `this`, the object and what it holds) or past the budget of the
/// translation unit, where what a body that can't be seen does answers.
static std::optional<CStorageUses>
parameterUses(const Decl *Parameter, unsigned Level, ASTContext &Context,
              const CMemberPath *Path = nullptr) {
  const auto *Named = dyn_cast<ParmVarDecl>(Parameter);
  if (Level > (Named ? levelsOf(Named->getType(), Context, Path) : 1u))
    return std::nullopt;
  CParameterUses &Uses = astCache<CParameterUses>(Context);
  const CParameterUses::CKey Key{{Parameter, Level}, {Path, &Context}};
  if (!Uses.Asked.contains(Key)) {
    if (Uses.Asked.size() >= CParameterUses::Budget)
      return std::nullopt;
    Uses.Asked.insert(Key);
  }
  return Uses.Of.get(Key);
}

CStorageUses storageUsesOf(const Expr *Use, unsigned Level,
                           ASTContext &Context, FParentOf ParentOf,
                           FKeptAt KeptAt) {
  return usesOf(Use, Level, Context, ParentOf, /*bBody=*/false, KeptAt);
}

CStorageUses numberUsesOf(const Expr *Number, ASTContext &Context) {
  return usesOf(Number, 0, Context, {}, /*bBody=*/false, {}, nullptr,
                /*bNumber=*/true);
}

/// Whether the code of \p Body, also of the closures it makes, can change
/// what the owner variable \p Owner owns, other than by the name \p Name
/// and its names in \p Made: where a name of it is dereferenced,
/// `pContext->m_pJob = {}`, what the dereference designates is written or
/// kept beyond the frame (storageUsesOf), not where it is only read or
/// given by const reference; a const member function of it whose result
/// can't refer to anything, `if (pContext)`, changes nothing; a local
/// reference bound to it, `auto &&rContext = pContext`, is it, by its own
/// names; any other use of a name of it can change it. A work stack over the
/// references.
static bool ownedChangedIn(const VarDecl *Owner, const Stmt *Body,
                           const Expr *Name, const Expr *Made,
                           ASTContext &Context) {
  if (!Owner || !Body)
    return true;
  llvm::SmallVector<const VarDecl *, 2> Work{Owner};
  llvm::SmallPtrSet<const VarDecl *, 2> Seen{Owner};
  while (!Work.empty()) {
    const VarDecl *Named = Work.pop_back_val();
    llvm::SmallPtrSet<const Expr *, 4> Excepted;
    if (Name)
      Excepted.insert(Name);
    if (Made)
      for (const CName &Each : namesIn(Named, Made, Context))
        Excepted.insert(Each.Name);
    for (const CName &Each : namesIn(Named, Body, Context)) {
      if (Excepted.contains(Each.Name))
        continue;
      const Expr *Use = Each.Name;
      DynTypedNode Parent = pathParentOf(Use, {}, Context);
      while (const auto *Up = Parent.get<Expr>()) {
        if (!isa<ParenExpr>(Up) && !isa<ImplicitCastExpr>(Up))
          break;
        Use = Up;
        Parent = pathParentOf(Use, {}, Context);
      }
      if (const auto *Operator = Parent.get<CXXOperatorCallExpr>();
          Operator && Operator->getNumArgs() == 1 &&
          Operator->getArg(0) == Use &&
          (Operator->getOperator() == OO_Arrow ||
           Operator->getOperator() == OO_Star)) {
        const CStorageUses Uses = storageUsesOf(Operator, 0, Context);
        if (Uses.bWritten || Uses.bKept)
          return true;
        continue;
      }
      if (const auto *Member = Parent.get<MemberExpr>()) {
        const auto *Call = dyn_cast_or_null<CXXMemberCallExpr>(
            pathParentOf(Member, {}, Context).get<Expr>());
        if (Call && Call->getImplicitObjectArgument() == Use &&
            Call->getMethodDecl() && Call->getMethodDecl()->isConst() &&
            refersNowhere(Call->getType(), Context))
          continue;
        return true;
      }
      if (const auto *Reference = Parent.get<VarDecl>();
          Reference && Reference->getType()->isReferenceType() &&
          Reference->hasLocalStorage() && !isa<ParmVarDecl>(Reference) &&
          Reference->getInit() == Use &&
          Context.hasSameUnqualifiedType(
              Reference->getType().getNonReferenceType(),
              Owner->getType().getNonReferenceType())) {
        if (Seen.insert(Reference).second)
          Work.push_back(Reference);
        continue;
      }
      return true;
    }
  }
  return false;
}

/// Whether \p At is a call whose result can hold what it is given because its
/// body can't be seen (Rule R by type).
static bool unseenCallHolds(const Expr *At) {
  const FunctionDecl *Callee = nullptr;
  if (const auto *Call = dyn_cast<CallExpr>(At))
    Callee = Call->getDirectCallee();
  else if (const auto *Construct = dyn_cast<CXXConstructExpr>(At))
    Callee = Construct->getConstructor();
  return isa<CallExpr, CXXConstructExpr>(At) &&
         (!Callee || !Callee->hasBody());
}

/// What the calls of usesOf share while the scope is alive: what reach
/// visits from each name and value asked (CReachClosures), and where it ends
/// at a call that can hold it (unseenCallHolds), and what each place of a
/// frame holds where it is asked, of final answers only (localSlotValues).
struct CStorageUsesScope::CShared {
  explicit CShared(ASTContext &Context)
      : Context(Context), AST(astIdentity(Context)), Walk(Context),
        Reached(Walk, /*bVisits=*/true), Unseen(Walk) {}
  ASTContext &Context;
  uint64_t AST;
  const CProvenance Walk;
  CReachClosures Reached;
  // Asked only with a Visit that ends at a call unseenCallHolds tells.
  CReachClosures Unseen;
  // Of each place asked, each value it holds anywhere it is asked, once: a
  // place asked at many points holds a growing part of the same values.
  struct CPlace {
    CSlot Slot;
    std::vector<CSlotValue> Values;
    llvm::DenseMap<std::pair<const Expr *, unsigned>, unsigned> Index;
  };
  // Values of a place by their index in its Values, in the order
  // localSlotValues gives them, each list once: the points of a loop hold
  // the same.
  struct CList {
    unsigned Place = 0;
    std::vector<unsigned> Values;
  };
  // What a place holds at a point, of Lists; none where that can't be told.
  struct CHeld {
    unsigned Place = 0;
    bool Told = true;
    unsigned List = 0;
  };
  // By variable, the lists by hash, and the points by where and by variable.
  llvm::DenseMap<const VarDecl *, llvm::SmallVector<unsigned, 1>> PlaceIndex;
  std::vector<CPlace> Places;
  llvm::DenseMap<size_t, llvm::SmallVector<unsigned, 1>> ListIndex;
  std::vector<CList> Lists;
  llvm::DenseMap<std::pair<const Stmt *, const VarDecl *>,
                 llvm::SmallVector<unsigned, 1>>
      HeldIndex;
  std::vector<CHeld> Held;
};

static thread_local CStorageUsesScope::CShared *t_pStorageUsesShared = nullptr;

CStorageUsesScope::CStorageUsesScope(ASTContext &Context)
    : Shared(std::make_unique<CShared>(Context)),
      Previous(t_pStorageUsesShared) {
  t_pStorageUsesShared = Shared.get();
}

CStorageUsesScope::~CStorageUsesScope() { t_pStorageUsesShared = Previous; }

/// What the scope alive shares of \p Context (CStorageUsesScope); null where
/// none is, or it is of another AST.
static CStorageUsesScope::CShared *sharedUses(ASTContext &Context) {
  CStorageUsesScope::CShared *Shared = t_pStorageUsesShared;
  if (!Shared || &Shared->Context != &Context ||
      Shared->AST != astIdentity(Context))
    return nullptr;
  return Shared;
}

/// Whether a value \p Slot can hold where \p At is, or where the scope of its
/// variable ends where \p At is null (localSlotValues, localSlotValuesAtEnd),
/// is one \p Picks picks, or what it holds can't be told: of \p Shared, when
/// given and the answer is final. \p Picks can ask usesOf again, which adds
/// to \p Shared: what is shared is read by index. \p Picked, when given, keeps
/// the answer of each list of values (CShared::CList) by \p PickedKey for the
/// caller, whose Picks gives the same answer of a value for the same key.
static bool holdsPicked(CStorageUsesScope::CShared *Shared, const CSlot &Slot,
                        const Stmt *At, ASTContext &Context,
                        llvm::function_ref<bool(const CSlotValue &)> Picks,
                        llvm::DenseMap<std::pair<unsigned, unsigned>, bool>
                            *Picked = nullptr,
                        unsigned PickedKey = 0) {
  auto Own = [&](const CSlotValues &Values) {
    return !Values.Told || llvm::any_of(Values.Values, Picks);
  };
  if (!Shared)
    return Own(At ? localSlotValues(Slot, At, Context)
                  : localSlotValuesAtEnd(Slot, Context));
  using CShared = CStorageUsesScope::CShared;
  const std::pair<const Stmt *, const VarDecl *> Key{At, Slot.Variable};
  std::optional<unsigned> iHeld;
  if (const auto It = Shared->HeldIndex.find(Key);
      It != Shared->HeldIndex.end())
    for (unsigned Each : It->second)
      if (Shared->Places[Shared->Held[Each].Place].Slot == Slot) {
        iHeld = Each;
        break;
      }
  if (!iHeld) {
    bool bFinal = false;
    const CSlotValues Values =
        At ? localSlotValues(Slot, At, Context, &bFinal)
           : localSlotValuesAtEnd(Slot, Context, &bFinal);
    if (!bFinal)
      return Own(Values);
    std::optional<unsigned> iPlace;
    for (unsigned Each : Shared->PlaceIndex.lookup(Slot.Variable))
      if (Shared->Places[Each].Slot == Slot) {
        iPlace = Each;
        break;
      }
    if (!iPlace) {
      iPlace = Shared->Places.size();
      Shared->PlaceIndex[Slot.Variable].push_back(*iPlace);
      Shared->Places.emplace_back().Slot = Slot;
    }
    CShared::CHeld Held{*iPlace, Values.Told, 0};
    if (Values.Told) {
      CShared::CPlace &Place = Shared->Places[*iPlace];
      CShared::CList List{*iPlace, {}};
      List.Values.reserve(Values.Values.size());
      for (const CSlotValue &Value : Values.Values) {
        const auto [It, bNew] = Place.Index.try_emplace(
            {Value.Value, unsigned(Value.Kind) * 2 + (Value.Part ? 1 : 0)},
            Place.Values.size());
        if (bNew)
          Place.Values.push_back(Value);
        List.Values.push_back(It->second);
      }
      const size_t Hash = llvm::hash_combine(
          *iPlace,
          llvm::hash_combine_range(List.Values.begin(), List.Values.end()));
      llvm::SmallVector<unsigned, 1> &Same = Shared->ListIndex[Hash];
      const auto Found = llvm::find_if(Same, [&](unsigned Each) {
        return Shared->Lists[Each].Place == List.Place &&
               Shared->Lists[Each].Values == List.Values;
      });
      if (Found != Same.end())
        Held.List = *Found;
      else {
        Held.List = Shared->Lists.size();
        Same.push_back(Held.List);
        Shared->Lists.push_back(std::move(List));
      }
    }
    iHeld = Shared->Held.size();
    Shared->HeldIndex[Key].push_back(*iHeld);
    Shared->Held.push_back(Held);
  }
  const CShared::CHeld Held = Shared->Held[*iHeld];
  if (!Held.Told)
    return true;
  if (Picked)
    if (const auto It = Picked->find({Held.List, PickedKey});
        It != Picked->end())
      return It->second;
  bool bPicked = false;
  for (size_t iValue = 0; iValue < Shared->Lists[Held.List].Values.size();
       ++iValue) {
    const CShared::CList &List = Shared->Lists[Held.List];
    const CSlotValue Value =
        Shared->Places[List.Place].Values[List.Values[iValue]];
    if (Picks(Value)) {
      bPicked = true;
      break;
    }
  }
  if (Picked)
    Picked->try_emplace({Held.List, PickedKey}, bPicked);
  return bPicked;
}

static CStorageUses usesOf(const Expr *Use, unsigned Level,
                           ASTContext &Context, FParentOf ParentOf, bool bBody,
                           FKeptAt KeptAt, const CMemberPath *Path,
                           bool bNumberUse) {
  // What the step being told does, merged into Total after it; held where
  // the step was reached through a name of the frame the value was put in.
  CStorageUses Total;
  CStorageUses Uses;
  const CProvenance Walk(Context);
  // Each expression that has the storage at a level, from the use, each
  // once, whether a dereference gave the glvalue it is, and whether it was
  // reached through a name of the frame the value was put in.
  // Of a record that holds it, the part of it that does (CMemberPath).
  struct CStep {
    const Expr *E = nullptr;
    unsigned At = 0;
    bool bIndirected = false;
    bool bNamed = false;
    const CMemberPath *Path = nullptr;
    // Whether E is a number made of the pointer value (bNumber of Next).
    bool bNumber = false;
    // Whether E is an object a constructor makes that keeps it as a whole by
    // the stated fact (DMibKeepsArgument), whatever its members are.
    bool bStated = false;
    // The function whose local's names it was reached through, of bNamed.
    const FunctionDecl *Frame = nullptr;
  };
  llvm::SmallVector<CStep, 8> Work{
      {Use, Level, false, false, Path, bNumberUse}};
  llvm::DenseSet<std::pair<std::pair<const Expr *, unsigned>,
                           const CMemberPath *>>
      Seen;
  // The implicit nodes a parent map that skips them leaves out, `InitListExpr`
  // of its syntactic form: of each, the node it is in.
  llvm::DenseMap<const Stmt *, const Stmt *> ImplicitParent;
  bool bNamed = false;
  const FunctionDecl *StepFrame = nullptr;
  const CMemberPath *StepPath = nullptr;
  bool StepNumber = false;
  // Goes on with \p E at \p At, reached as the step being told is, or
  // through a name of the frame when \p bName, of a record through the part
  // \p NextPath of it; with \p bNumber, \p E is an integer made of the value
  // of a pointer that points to it there, `reinterpret_cast<umint>(&_Job)`.
  auto Next = [&](const Expr *E, unsigned At, bool bIndirected = false,
                  bool bName = false, const CMemberPath *NextPath = nullptr,
                  bool bNumber = false, bool bStated = false,
                  const FunctionDecl *Frame = nullptr) {
    Work.push_back({E, At, bIndirected, bNamed || bName, NextPath, bNumber,
                    bStated, bName && Frame ? Frame : StepFrame});
  };
  // Kept beyond the frame where \p E is given by \p Effect to \p Parent,
  // as KeptAt tells.
  auto KeptBy = [&](const Expr *E, unsigned At, const COperandEffect &Effect,
                    const Stmt *Parent) {
    if (!KeptAt || KeptAt(E, At, Effect, Parent))
      Uses.bKept = true;
  };
  // Whether the code of \p Closure runs in the frame of the use: it is
  // called in the scope that makes it, or the use, or \p From, is in it.
  auto RunsInFrame = [&](const LambdaExpr *Closure, const Expr *From) {
    return calledInScope(Closure, Context) ||
           isInClosure(Use, Closure, Context) ||
           (From != Use && isInClosure(From, Closure, Context));
  };
  // Whether \p At, where a way of a value a place holds is (PlaceHolds), is
  // the use, or a call whose result can hold what it is given because its
  // body can't be seen (Rule R by type).
  auto EndsHolding = [&](const Expr *At) {
    return At == Use || unseenCallHolds(At);
  };
  // The walks of the values places hold (PlaceHolds), shared by the places
  // and the points asked: each value's history is that of the values before.
  CReachClosures Holding(Walk);
  // What reach visits from each name Reaches asks, shared by the names and
  // the values asked of them: each walk would go through what the earlier
  // went through again, a pointer's history of copies. Of the scope alive
  // first (CStorageUsesScope), which the other calls share; this call's own
  // where that can't tell.
  CStorageUsesScope::CShared *const Shared = sharedUses(Context);
  CReachClosures Reached(Walk, /*bVisits=*/true);
  // Whether the provenance of \p Name at \p NameLevel reaches \p Value
  // (CProvenance::reach, by the values that reach each name); also where it
  // can't be told.
  auto Reaches = [&](const Expr *Name, unsigned NameLevel, const Expr *Value) {
    std::optional<bool> Visits;
    if (Shared)
      Visits = Shared->Reached.visits(Name, NameLevel, Value);
    if (!Visits)
      Visits = Reached.visits(Name, NameLevel, Value);
    if (Visits)
      return *Visits;
    bool bFound = false;
    const bool bComplete =
        Walk.reach(Name, NameLevel, [&](const Expr *At, unsigned, bool) {
          if (At != Value)
            return EReach::Continue;
          bFound = true;
          return EReach::Root;
        });
    return bFound || !bComplete;
  };
  // Whether \p E designates a part of a record of the frame by members
  // (designatedSlots): of a local or a parameter held by value, also
  // through a local reference to it, a value of records (isValueRecordType).
  auto OfFrameRecord = [&](const Expr *E) {
    bool bTold = true;
    const llvm::SmallVector<CSlot, 2> Slots =
        designatedSlots(E, Context, &bTold);
    return bTold && Slots.size() == 1 && !Slots.front().Element &&
           Slots.front().Variable &&
           isValueRecordType(Slots.front().Variable->getType(), Context);
  };
  // The one answer to what a place of the frame, a variable and the members
  // on its path, holds at a point, of the walk: whether a value of the place
  // that reaches the point, by its definitions there (localSlotValues, the
  // flow cache's reaching definitions per member path), can be what the walk
  // started from, the storage at \p PlaceLevel of it: a way of the
  // provenance of the value reaches the start, or a call whose body can't be
  // seen, whose result can hold what it is given (Rule R by type); also
  // where it can't be told. The place is what \p Place designates where it is
  // read (designatedSlots), or \p Slot at \p Point, or where the scope of its
  // variable ends where \p Point is null (localSlotValuesAtEnd). Of each list
  // of values the scope shares, the answer by the level, for the points that
  // hold the same: those of a loop.
  llvm::DenseMap<std::pair<unsigned, unsigned>, bool> PlacePicked;
  auto PlaceHolds = [&](const Expr *Place, unsigned PlaceLevel,
                        const CSlot *Slot = nullptr,
                        const Stmt *Point = nullptr) {
    auto CanBe = [&](const Expr *Value) {
      bool bIncomplete = false;
      // Of the scope's walks, which every call shares, as they don't depend
      // on the use: the walk that ends at the use or at such a call reaches
      // one, or can't tell, exactly where the walk that goes on everywhere
      // reaches the use or the walk that ends only at such calls reaches
      // one, or either can't tell; a way past where one ends was found.
      if (Shared) {
        const std::optional<bool> Visits =
            Shared->Reached.visits(Value, PlaceLevel, Use);
        const llvm::SmallVectorImpl<unsigned> *Ends =
            Visits ? Shared->Unseen.closure(
                         Value, PlaceLevel,
                         [](const Expr *At, unsigned,
                            llvm::SmallVectorImpl<unsigned> &Records) {
                           if (!unseenCallHolds(At))
                             return EReach::Continue;
                           Records.push_back(0);
                           return EReach::Root;
                         },
                         [](const Expr *) {}, &bIncomplete)
                   : nullptr;
        if (Ends)
          return *Visits || !Ends->empty() || bIncomplete;
        bIncomplete = false;
      }
      if (const llvm::SmallVectorImpl<unsigned> *Ends = Holding.closure(
              Value, PlaceLevel,
              [&](const Expr *At, unsigned,
                  llvm::SmallVectorImpl<unsigned> &Records) {
                if (!EndsHolding(At))
                  return EReach::Continue;
                Records.push_back(0);
                return EReach::Root;
              },
              [](const Expr *) {}, &bIncomplete))
        return !Ends->empty() || bIncomplete;
      bool bFound = false;
      const bool bComplete =
          Walk.reach(Value, PlaceLevel, [&](const Expr *At, unsigned, bool) {
            if (!EndsHolding(At))
              return EReach::Continue;
            bFound = true;
            return EReach::Root;
          });
      return bFound || !bComplete;
    };
    // Of an element, one of a constant index of storage of its own
    // (ownedElement), by its own definitions, `Values[0]`.
    auto OwnElement = [&](const CSlot &Of) {
      return Of.Element && Of.Index && ownedElement(Of, nullptr);
    };
    CSlot Designated;
    if (Place) {
      bool bTold = true;
      const llvm::SmallVector<CSlot, 2> Slots =
          designatedPlaces(Place, Context, &bTold);
      if (!bTold || Slots.size() != 1 ||
          Slots.front().Place != EPlace::Variable ||
          (Slots.front().Element && !OwnElement(Slots.front())) ||
          (Slots.front().Path.empty() && !Slots.front().Element))
        return true;
      Designated = Slots.front();
      Slot = &Designated;
      Point = Place;
    }
    if (!Slot || (Slot->Element && !OwnElement(*Slot)) ||
        (!Slot->Element && Slot->Variable->getType()->isArrayType()))
      return true;
    return holdsPicked(Shared, *Slot, Point, Context,
                       [&](const CSlotValue &Value) {
                         return Value.Kind != CSlotValue::EKind::Value ||
                                !Value.Value || CanBe(Value.Value);
                       },
                       &PlacePicked, PlaceLevel);
  };
  // Goes on with the names of \p Named, which name the storage at \p At,
  // of a record through the part \p OnPath of it, from \p From, held there
  // when \p bHolds: a name in a closure whose code doesn't run in the frame
  // is told where the closure captures it. Of a copy, \p bCopy, only the
  // names the provenance of which reaches \p From; with \p After, only the
  // names that can run after it (canRunAfter).
  auto GoOnWith = [&](const ValueDecl *Named, unsigned At, const Expr *From,
                      bool bCopy, bool bHolds,
                      const CMemberPath *OnPath = nullptr,
                      bool bNumber = false, const Stmt *After = nullptr) {
    const Stmt *Body = bodyNaming(Named);
    if (!Body) {
      Uses.bKept = true;
      return;
    }
    for (const CName &Name : namesIn(Named, Body, Context)) {
      if (bCopy && !Reaches(Name.Name, At, From))
        continue;
      // A number is followed by the definitions of the local that reach the
      // name, as the provenance follows a pointer: not after it is given
      // another value.
      if (bNumber && !Name.Closure) {
        const auto *Variable = dyn_cast<VarDecl>(Named);
        if (Variable &&
            !holdsPicked(Shared, {Variable, {}, false}, Name.Name, Context,
                         [&](const CSlotValue &Value) {
                           return Value.Kind != CSlotValue::EKind::Value ||
                                  (Value.Value &&
                                   sameValue(Value.Value, Context) ==
                                       sameValue(From, Context));
                         }))
          continue;
      }
      if (After && !Name.Closure &&
          !canRunAfter(functionNaming(Named), Body, After, Name.Name,
                       Context))
        continue;
      if (Name.Closure && !RunsInFrame(Name.Closure, From))
        continue;
      Next(Name.Name, At, false, bHolds, OnPath, bNumber, false,
           functionNaming(Named));
    }
  };
  // \p E copied or bound into the pointer \p Variable of the frame: goes on
  // with its names, a copy of what a glvalue at 0 designates reads it.
  auto IntoPointer = [&](const VarDecl *Variable, const Expr *E, unsigned At,
                         bool bHolds) {
    if (E->isGLValue() && At == 0)
      Uses.bRead = true;
    else
      GoOnWith(Variable, E->isGLValue() ? At : At + 1, E, /*bCopy=*/true,
               bHolds, StepPath);
  };
  // What the destructor of a record of \p Type that holds the storage at
  // \p At, through the part \p Held of it, does to it where the record
  // ends: a destructor whose body can be seen, what the names of `this` do
  // there; one that can't be seen, it is given; one the compiler defines,
  // the destructors of the members on the path; a trivial one nothing. By a
  // work stack over the members.
  auto DestroyedUses = [&](QualType Type, unsigned At,
                           const CMemberPath *Held) {
    CStorageUses Destroyed;
    struct CPart {
      QualType Type;
      const CMemberPath *Path;
    };
    llvm::SmallVector<CPart, 4> Parts{{Type, Held}};
    for (int Steps = 0; !Parts.empty() && Steps < 64; ++Steps) {
      const CPart Part = Parts.pop_back_val();
      const QualType Of = Context.getBaseElementType(Part.Type);
      const auto *Class = Of->getAsCXXRecordDecl();
      if (!Class || !Class->hasDefinition() ||
          Class->hasTrivialDestructor() || !isValueRecordType(Of, Context))
        continue;
      const CXXDestructorDecl *Destructor = Class->getDestructor();
      if (Destructor && Destructor->isUserProvided()) {
        const FunctionDecl *Definition = nullptr;
        std::optional<CStorageUses> InBody;
        if (Destructor->hasBody(Definition) && Definition)
          InBody = parameterUses(Definition, At, Context, Part.Path);
        if (!InBody) {
          Destroyed.bGiven = true;
          continue;
        }
        CStorageUses Body = *InBody;
        Body.ReturnedLevels = 0;
        Destroyed |= Body;
        continue;
      }
      for (const CXXBaseSpecifier &Base : Class->bases())
        Parts.push_back({Base.getType(), Part.Path});
      const bool bTold = Part.Path && Part.Path->Field;
      for (const FieldDecl *Field : Class->fields())
        if (!bTold || Part.Path->Field == Field)
          Parts.push_back(
              {Field->getType(), bTold ? Part.Path->Next : Part.Path});
    }
    return Destroyed;
  };
  // Whether the local \p Variable can be destroyed after a suspension of
  // its function, where its scope ends: its declaration can run after one,
  // or a suspension is in its scope after it (inScopeAt); also where that
  // can't be told.
  auto DestroyedAfterSuspension = [&](const VarDecl *Variable) {
    const FunctionDecl *Function = functionNaming(Variable);
    if (!Function || !Function->getBody())
      return true;
    const Stmt *Declaration = nullptr;
    for (const DynTypedNode &Parent : instantiatedParents(*Variable, Context))
      if ((Declaration = Parent.get<DeclStmt>()))
        break;
    if (!Declaration ||
        mayRunAfterSuspension(Declaration, Function, Context))
      return true;
    return llvm::any_of(suspendPoints(Function->getBody()),
                        [&](const Stmt *Point) {
                          return inScopeAt(Variable, Point, /*Ordered=*/true,
                                           Context);
                        });
  };
  // \p E, from \p From, held at \p At by the record \p Variable of the
  // frame, a value of records (isValueRecordType), through the part \p Held
  // of it: its names go on where the provenance of each reaches \p From,
  // where it is put there in place by the call \p InPlace those that can
  // run after it, and where it ends its destructor (DestroyedUses), through
  // it.
  auto HeldInRecord = [&](const VarDecl *Variable, const Expr *From,
                          unsigned At, const CMemberPath *Held,
                          const Stmt *InPlace = nullptr) {
    GoOnWith(Variable, At, From, /*bCopy=*/!InPlace, /*bHolds=*/true, Held,
             /*bNumber=*/false, InPlace);
    if (Variable->getType()->isReferenceType())
      return;
    CStorageUses Destroyed = DestroyedUses(Variable->getType(), At, Held);
    // Of a part told by members, what the destructor uses of it is what the
    // place holds where the scope ends (PlaceHolds): nothing where what was
    // put in no longer reaches there.
    if (!Destroyed.none() && Held && Held->Field &&
        !Variable->getType()->isArrayType()) {
      CSlot Place{Variable, {}, false};
      for (const CMemberPath *Each = Held; Each && Each->Field;
           Each = Each->Next)
        Place.Path.push_back(Each->Field);
      if (!PlaceHolds(nullptr, At, &Place))
        Destroyed = CStorageUses{};
    }
    if (!Destroyed.none() && DestroyedAfterSuspension(Variable))
      Total.bHeld = true;
    Uses |= Destroyed;
  };
  // A copy of \p E into \p Variable of \p Type, \p bLocal of the frame: of
  // what a glvalue at 0 designates it reads it; a value of records of the
  // frame holds it as \p E does (HeldInRecord); anything else keeps it.
  auto CopiedInto = [&](const VarDecl *Variable, QualType Type, bool bLocal,
                        const Expr *E, unsigned At) {
    if (E->isGLValue() && At == 0)
      Uses.bRead = true;
    else if (!bLocal || !isValueRecordType(Type, Context))
      Uses.bKept = true;
    else if (Variable && E->getType()->isRecordType())
      HeldInRecord(Variable, E, At, StepPath);
  };
  // \p Variable initialized with \p E: a reference of the frame, a
  // structured binding by reference and the variable of a range-based `for`
  // by reference go on with their names, a pointer of the frame goes on
  // with its names; else a copy (CopiedInto).
  auto Declared = [&](const VarDecl *Variable, const Expr *E, unsigned At) {
    const bool bLocal =
        Variable->hasLocalStorage() && !isa<ParmVarDecl>(Variable);
    if (const CXXForRangeStmt *Loop = rangeLoopOf(Variable, Context)) {
      const VarDecl *Element = Loop->getLoopVariable();
      if (Element && Element->getType()->isReferenceType())
        GoOnWith(Element, At, E, /*bCopy=*/false, /*bHolds=*/true);
      else
        CopiedInto(Element, Element ? Element->getType() : QualType(),
                   Element, E, At);
      return;
    }
    if (const auto *Decomposition = dyn_cast<DecompositionDecl>(Variable)) {
      const bool bReference = Variable->getType()->isReferenceType();
      // Of a record that holds it, each binding of a member on the path, a
      // copy of it or a reference to it, the referent of a reference member
      // a level down.
      if (At > 0 && E->getType()->isRecordType() &&
          (bReference || (bLocal && isValueRecordType(Variable->getType(),
                                                      Context)))) {
        for (const BindingDecl *Binding : Decomposition->bindings()) {
          const FieldDecl *Field = boundMember(Binding);
          if (!Field) {
            GoOnWith(Binding, At, E, !bReference, /*bHolds=*/true);
            continue;
          }
          const QualType Type = Field->getType();
          const bool bTold = StepPath && StepPath->Field;
          if (bTold ? StepPath->Field != Field
                    : !Type->isReferenceType() &&
                          refersNowhere(Type, Context))
            continue;
          const CMemberPath *Rest = bTold ? StepPath->Next : StepPath;
          if (Type->isReferenceType() && !(Rest && Rest->Field))
            GoOnWith(Binding, At - 1, E, !bReference, /*bHolds=*/true);
          else
            GoOnWith(Binding, At, E, !bReference, /*bHolds=*/true, Rest);
        }
        return;
      }
      if (bReference) {
        for (const BindingDecl *Binding : Decomposition->bindings())
          GoOnWith(Binding, At, E, /*bCopy=*/false, /*bHolds=*/true);
      } else
        CopiedInto(nullptr, Variable->getType(), bLocal, E, At);
      return;
    }
    // A reference to a record is the record, with the same parts.
    if (bLocal && Variable->getType()->isReferenceType()) {
      GoOnWith(Variable, At, E, /*bCopy=*/false, /*bHolds=*/true, StepPath);
      return;
    }
    if (bLocal && Variable->getType()->isPointerType()) {
      IntoPointer(Variable, E, At, /*bHolds=*/true);
      return;
    }
    CopiedInto(Variable, Variable->getType(), bLocal, E, At);
  };
  // What the call or construction \p Call keeps of \p E, given to its
  // parameter \p Index (callKept, Rule R): what it makes goes on with it.
  // Whether what a pointer to \p Pointee, or a reference to it, refers to
  // can be storage of \p Type: the same type, cv aside, a base or a class
  // derived from it, or bytes, either way: bytes can be storage of any
  // object, `alignas(t_CType) unsigned char m_Storage[...]`.
  auto Fits = [&](QualType Pointee, QualType Type) {
    Pointee = Pointee.getUnqualifiedType();
    Type = Type.getNonReferenceType().getUnqualifiedType();
    if (Pointee->isVoidType() || Pointee->isCharType() || Type->isCharType() ||
        Pointee->isDependentType() || Type->isDependentType() ||
        Context.hasSameUnqualifiedType(Pointee, Type))
      return true;
    const auto *Of = Pointee->getAsCXXRecordDecl();
    const auto *Storage = Type->getAsCXXRecordDecl();
    return Of && Storage && Of->hasDefinition() && Storage->hasDefinition() &&
           (Of->isDerivedFrom(Storage) || Storage->isDerivedFrom(Of));
  };
  // Whether storage of \p Referred can be a part of a \p Record, the
  // record itself, a base or a member by value, by a work stack over its
  // parts.
  auto IsPart = [&](QualType Referred, QualType Record) {
    llvm::SmallVector<QualType, 8> Parts{Record};
    for (int Steps = 0; !Parts.empty() && Steps < 64; ++Steps) {
      const QualType Type = Context.getBaseElementType(Parts.pop_back_val());
      if (Fits(Referred, Type))
        return true;
      const auto *Class = Type->getAsCXXRecordDecl();
      if (!Class || !Class->hasDefinition())
        continue;
      for (const CXXBaseSpecifier &Base : Class->bases())
        Parts.push_back(Base.getType());
      for (const FieldDecl *Field : Class->fields())
        if (!Field->getType()->isReferenceType())
          Parts.push_back(Field->getType());
    }
    return false;
  };
  // The result \p Call of a body that can't be seen, a pointer or a
  // reference, given \p E with the storage at \p At, bound to it when \p
  // Bound, a pointer to an object it is called on too (Rule R by type): what
  // it refers to can be a part of what \p E designates or points to, or
  // what that holds a level down, each where its type fits.
  auto ResultAt = [&](const Expr *Call, const Expr *E, bool Bound,
                      unsigned At) {
    const QualType Result = Call->getType();
    const QualType Referred =
        Call->isGLValue()
            ? Result.getNonReferenceType()
            : (Result->isPointerType() ? Result->getPointeeType() : QualType());
    if (Referred.isNull())
      return;
    QualType Type = E->getType().getNonReferenceType();
    // What a pointer value points to.
    if (!E->isGLValue()) {
      if (!Type->isPointerType())
        return;
      Type = Type->getPointeeType();
    }
    if (IsPart(Referred, Type))
      Next(Call, At);
    // What a pointer points to, or what a record holds, a level down.
    if (At > 0 && (Type->isRecordType() ||
                   (Type->isPointerType() &&
                    IsPart(Referred, Type->getPointeeType()))))
      Next(Call, At - 1);
  };
  // Kept in a body that can be seen, \p bKeptInBody, where that isn't, what
  // the call gives back at each level the provenance of its value reaches
  // \p E at (reach), `int *p; int **pp = &p; *pp = _pValue; return p;`.
  auto KeptByCall = [&](const Expr *Call, unsigned Index, const Expr *E,
                        bool Bound, unsigned At, bool bUnseen,
                        bool bKeptInBody) {
    const CCallKept Kept = callKept(Call, Index, E, bUnseen, Context);
    if (Kept.bKept)
      Uses.bKept = true;
    if (Kept.Made) {
      const bool Record = Kept.Made->isPRValue() &&
                          Kept.Made->getType()->isRecordType();
      // An object a constructor makes that keeps the argument by the stated
      // fact (DMibKeepsArgument) holds it as a whole, whatever its members.
      const auto *Made = dyn_cast<CXXConstructExpr>(Kept.Made);
      const bool bStated = Made && Made->getConstructor() &&
                           keepsArgument(Made->getConstructor(), Index);
      // Of a constructor whose body can be seen, the members it is the
      // value of (CCallKept::ObjectMembers), each with the part of it that
      // holds it.
      if (Made && !bStated && !Kept.ObjectMembers.empty())
        for (const FieldDecl *Member : Kept.ObjectMembers) {
          const CMemberPath *Inside =
              E->getType()->isRecordType() ? StepPath : nullptr;
          // A member made as an aggregate, by the members in it given it.
          bool bNested = false;
          for (const auto &Nested : Kept.ObjectPaths) {
            if (Nested.empty() || Nested.front() != Member)
              continue;
            bNested = true;
            const CMemberPath *Chain = Inside;
            for (const FieldDecl *Each : llvm::reverse(Nested))
              Chain = memberPath(Each, Chain, Context);
            Next(Kept.Made, heldLevelOf(E, Bound, At, /*Record=*/true), false,
                 false, Chain);
          }
          if (!bNested)
            Next(Kept.Made, heldLevelOf(E, Bound, At, /*Record=*/true), false,
                 false, memberPath(Member, Inside, Context));
        }
      else if (Record || Made)
        Next(Kept.Made, heldLevelOf(E, Bound, At, /*Record=*/true), false,
             false, wholePath(Context), false, bStated);
      else
        ResultAt(Kept.Made, E, Bound, At);
    }
    for (const Expr *Holder : Kept.Temporaries)
      Next(Holder, heldLevelOf(E, Bound, At, /*Record=*/true));
    // Kept in a value of records the frame holds, the object called on or
    // what another argument refers to, `Receiver.f_Take(_Value)`,
    // `fg_Store(_Value, &Sink)`: held in that record as a whole.
    for (const Expr *Holder : Kept.Values) {
      const Expr *Named = sameValue(Holder, Context);
      bool bAddress = false;
      if (const auto *Address = dyn_cast<UnaryOperator>(Named);
          Address && Address->getOpcode() == UO_AddrOf) {
        Named = Address->getSubExpr();
        bAddress = true;
      }
      const auto *Record = dyn_cast_or_null<VarDecl>(namedDecl(Named, Context));
      const CMemberCall Member = memberCallOf(Call, /*Operators=*/true);
      const bool bReceiver =
          Member && Member->getImplicitObjectArgument() == Holder;
      // A temporary record a member function is called on, `CFormat("{}")
      // << _Text`, whose result is that object itself, `return *this;`
      // (returnedRelation): the result holds it as a whole, and what is made
      // of it goes on with it.
      if (!Record && bReceiver) {
        const std::optional<CObjectRelation> Gives =
            Member->getMethodDecl()
                ? returnedRelation(Member->getMethodDecl(), &Context)
                : std::nullopt;
        const CStorageRoots Roots = Gives ? storageRootsOf(Holder, Context)
                                          : CStorageRoots{};
        if (Gives && Gives->Kind == EObjectRelation::SameObject &&
            !Gives->bThroughPointer && !isCopyStorage(*Gives) &&
            !Roots.Roots.empty() &&
            llvm::all_of(Roots.Roots, [](const CStorageRoot &Root) {
              return Root.Kind == ERootKind::Temporary;
            })) {
          Next(Call, heldLevelOf(E, Bound, At, /*Record=*/true), false, false,
               wholePath(Context));
          continue;
        }
      }
      // The object a member function whose body can be seen is called on
      // holds it in the members it is the value of, of a member that holds
      // it in a part of it, that part (CCallKept::ObjectPaths); else as a
      // whole.
      llvm::SmallVector<const CMemberPath *, 2> Paths;
      if (bReceiver)
        for (const FieldDecl *Field : Kept.ObjectMembers) {
          const CMemberPath *Inside =
              E->getType()->isRecordType() ? StepPath : nullptr;
          bool bNested = false;
          for (const auto &Nested : Kept.ObjectPaths) {
            if (Nested.empty() || Nested.front() != Field)
              continue;
            bNested = true;
            const CMemberPath *Chain = Inside;
            for (const FieldDecl *Each : llvm::reverse(Nested))
              Chain = memberPath(Each, Chain, Context);
            Paths.push_back(Chain);
          }
          if (!bNested)
            Paths.push_back(memberPath(Field, Inside, Context));
        }
      if (Paths.empty())
        Paths.push_back(wholePath(Context));
      const unsigned HeldLevel = heldLevelOf(E, Bound, At, /*Record=*/true);
      auto IsFrameRecord = [](const VarDecl *Local) {
        return Local && Local->hasLocalStorage() && !isa<ParmVarDecl>(Local) &&
               !Local->getType()->isReferenceType() &&
               !Local->getType()->isPointerType();
      };
      if (IsFrameRecord(Record)) {
        for (const CMemberPath *Part : Paths)
          HeldInRecord(Record, Holder, HeldLevel, Part, Call);
        continue;
      }
      // Of another local of the frame given by reference, or its address, a
      // pointer or a number, `pOther` or `&pOther`: held in it, its names
      // after the call go on.
      if ((bAddress || (Holder->isGLValue() && !bReceiver)) && Record &&
          Record->hasLocalStorage() &&
          !isa<ParmVarDecl>(Record) && !Record->getType()->isReferenceType()) {
        GoOnWith(Record, HeldLevel, Holder, /*bCopy=*/false, /*bHolds=*/true,
                 nullptr, /*bNumber=*/false, Call);
        continue;
      }
      // Held through a local pointer to records of the frame, `pView->f_Set(
      // &_Job)` or `(*pView).f_Set(&_Job)`: the names of the pointer that
      // can run after the call, a level up, and the records it points to.
      // The pointer is found past a load and a dereference, by what each
      // does to its operand.
      const Expr *Through = Named;
      for (int Steps = 0; Steps < 4 && Through && !namedDecl(Through, Context);
           ++Steps) {
        const CEffects &Of = effectsOf(Through, Context);
        Through = Of.Operands.size() == 1 &&
                          (Of.Operands.front().Use == EUse::Access ||
                           Of.Operands.front().Use == EUse::Dereference)
                      ? dyn_cast_or_null<Expr>(Of.Operands.front().Operand)
                      : nullptr;
      }
      const auto *Pointer =
          Through ? dyn_cast_or_null<VarDecl>(namedDecl(Through, Context))
                  : nullptr;
      const bool bPointer = Pointer && Pointer->hasLocalStorage() &&
                            !isa<ParmVarDecl>(Pointer) &&
                            Pointer->getType()->isPointerType();
      const CStorageRoots Roots = Holder->getType()->isPointerType()
                                      ? pointeeRootsOf(Holder, Context)
                                      : storageRootsOf(Holder, Context);
      for (const CMemberPath *Part : Paths) {
        if (bPointer)
          GoOnWith(Pointer, HeldLevel + 1, Holder, /*bCopy=*/false,
                   /*bHolds=*/true, Part, /*bNumber=*/false, Call);
        for (const CStorageRoot &Root : Roots.Roots)
          if (const auto *Pointed = dyn_cast_or_null<VarDecl>(Root.Variable);
              Root.Kind == ERootKind::Frame && IsFrameRecord(Pointed))
            HeldInRecord(Pointed, Holder, HeldLevel, Part, Call);
      }
    }
    if (bKeptInBody && !Kept.bKept)
      for (unsigned Back = 0; Back < 3; ++Back)
        if (Reaches(Call, Back, E))
          Next(Call, Back);
  };
  // Given to the parameter \p Index of \p Call, a call or a construction, a
  // reference bound to the storage at \p At of \p E (\p Bound), else a copy
  // of a pointer to it: in each body the call can run, what the parameter's
  // names do, and what it gives back goes on; where one can't be seen, what
  // such a body does. What it keeps, by Rule R.
  auto Given = [&](const Expr *Call, const FunctionDecl *Callee,
                   unsigned Index, bool Bound, const Expr *E, unsigned At) {
    const auto *AsCall = dyn_cast_or_null<CallExpr>(Call);
    const auto *Construct = dyn_cast_or_null<CXXConstructExpr>(Call);
    if (Construct)
      Callee = Construct->getConstructor();
    const QualType Parameter = Callee && Index < Callee->getNumParams()
                                   ? Callee->getParamDecl(Index)->getType()
                                   : QualType();
    CCalledRoots Called;
    if (AsCall && Callee)
      Called = Walk.calledRoots(AsCall, Callee);
    else if (Construct && Callee) {
      const FunctionDecl *Definition = Callee->getDefinition();
      if (Definition && Definition->getBody())
        Called.Bodies.push_back({Definition, nullptr});
    }
    bool bUnseen = Called.open();
    unsigned ReturnedLevels = 0;
    bool bKeptInBody = false;
    CStorageUses InBodies;
    // A record, bound or copied, holds it at its level, through the part
    // the walk is at.
    const bool Record = E->getType()->isRecordType();
    if (!bUnseen)
      for (const auto &[Body, Returned] : Called.Bodies) {
        const std::optional<CStorageUses> Of =
            Index < Body->getNumParams()
                ? parameterUses(Body->getParamDecl(Index),
                                Bound || Record ? At : At + 1, Context,
                                Record ? StepPath : nullptr)
                : std::nullopt;
        if (!Of) {
          bUnseen = true;
          break;
        }
        // Where it is kept is told by Rule R (callKept).
        CStorageUses InBody = *Of;
        ReturnedLevels |= InBody.ReturnedLevels;
        InBody.ReturnedLevels = 0;
        bKeptInBody = bKeptInBody || InBody.bKept;
        InBody.bKept = false;
        InBodies |= InBody;
      }
    if (bUnseen) {
      if (Bound && At == 0) {
        Uses.bRead = true;
        Uses.bWritten =
            Uses.bWritten || Parameter.isNull() ||
            !Parameter.getNonReferenceType().isConstQualified();
      } else
        Uses.bGiven = true;
    } else
      Uses |= InBodies;
    // Given back by a body, the walk goes on with the call's value at each
    // level the storage is given back at, as the access path does.
    if (!bUnseen)
      for (unsigned Back = 0; ReturnedLevels >> Back; ++Back)
        if (ReturnedLevels & (1u << Back))
          Next(Call, Back);
    if (Call)
      KeptByCall(Call, Index, E, Bound, At, bUnseen, bKeptInBody && !bUnseen);
  };
  // The object, at \p At, of \p Call of the member function \p Method,
  // const when \p bConst: in each body the call can run, what the names of
  // `this` do, and what it gives back goes on, `return *this;`; where one
  // can't be seen, what is there is read, and written by one that isn't
  // const, and what its result can hold by its type goes on with it (Rule
  // R), not of a handle called on nor of a function that returns a
  // `TCFuture`.
  auto CalledOn = [&](const CallExpr *Call, const CXXMethodDecl *Method,
                      const Expr *Object, unsigned At, bool bConst) {
    // Called on a part of a record of the frame told by members, what the
    // call reads of it is what that place holds there (PlaceHolds): nothing
    // where what was put in no longer reaches the call.
    if (Call && Object && StepPath && StepPath->Field && !StepNumber &&
        OfFrameRecord(Object)) {
      bool bTold = true;
      const llvm::SmallVector<CSlot, 2> Slots =
          designatedSlots(Object, Context, &bTold);
      if (bTold && Slots.size() == 1 && !Slots.front().Element &&
          !Slots.front().Variable->getType()->isArrayType()) {
        CSlot Place = Slots.front();
        for (const CMemberPath *Each = StepPath; Each && Each->Field;
             Each = Each->Next)
          Place.Path.push_back(Each->Field);
        if (!PlaceHolds(nullptr, At, &Place, Call))
          return;
      }
    }
    CCalledRoots Called;
    if (Call && Method)
      Called = Walk.calledRoots(Call, Method);
    bool bUnseen = Called.open();
    unsigned ReturnedLevels = 0;
    CStorageUses InBodies;
    if (!bUnseen)
      for (const auto &[Body, Returned] : Called.Bodies) {
        const std::optional<CStorageUses> Of =
            parameterUses(Body, At, Context, StepPath);
        if (!Of) {
          bUnseen = true;
          break;
        }
        CStorageUses InBody = *Of;
        ReturnedLevels |= InBody.ReturnedLevels;
        InBody.ReturnedLevels = 0;
        InBodies |= InBody;
      }
    if (!bUnseen) {
      Uses |= InBodies;
      for (unsigned Back = 0; ReturnedLevels >> Back; ++Back)
        if (ReturnedLevels & (1u << Back))
          Next(Call, Back);
      return;
    }
    if (At == 0) {
      Uses.bRead = true;
      Uses.bWritten = Uses.bWritten || !bConst;
    } else
      Uses.bGiven = true;
    if (!Call)
      return;
    const CCallKept Kept =
        callKept(Call, CallObjectIndex, nullptr, /*bUnseen=*/true, Context);
    if (Kept.bKept)
      Uses.bKept = true;
    if (Kept.Made && Object) {
      if (Kept.Made->isPRValue() && Kept.Made->getType()->isRecordType())
        Next(Kept.Made, At + 1, false, false, wholePath(Context));
      else
        ResultAt(Kept.Made, Object, /*Bound=*/true, At);
    }
  };
  int Steps = 0;
  for (; !Work.empty() && Steps < 256; ++Steps) {
    const CStep Step = Work.pop_back_val();
    const Expr *E = Step.E;
    const unsigned At = Step.At;
    const bool bIndirected = Step.bIndirected;
    bNamed = Step.bNamed;
    StepFrame = Step.Frame;
    StepPath = Step.Path;
    StepNumber = Step.bNumber;
    if (!Seen.insert({{E, At}, StepPath}).second)
      continue;
    // What it tells is merged after it, held where it was reached through a
    // name of the frame.
    // Held only where the name can run after a suspension of the function
    // it is a local of.
    llvm::scope_exit Merge([&] {
      if (bNamed && !Uses.none() &&
          (!StepFrame || mayRunAfterSuspension(E, StepFrame, Context)))
        Total.bHeld = true;
      Total |= Uses;
      Uses = CStorageUses();
    });
    // A value that can't refer to storage, an integer, is over (Rule R),
    // unless it is made of the value of a pointer (bNumber), or is an object
    // that keeps it by the stated fact (bStated).
    if (E != Use && E->isPRValue() && !Step.bNumber && !Step.bStated &&
        refersNowhere(E->getType(), Context))
      continue;
    // A member that isn't a record nor a reference, of a record of the frame
    // by members, also of a view in it, read through the record or a local
    // reference to it: only where the value that place holds there can be
    // what was put in (PlaceHolds).
    if (const auto *Member = dyn_cast<MemberExpr>(E);
        Member && E != Use && At > 0 && !Step.bNumber && E->isGLValue() &&
        !E->getType()->isRecordType() && isa<FieldDecl>(Member->getMemberDecl()) &&
        !Member->getMemberDecl()->getType()->isReferenceType() &&
        OfFrameRecord(Member) && !PlaceHolds(E, At))
      continue;
    // An element of a constant index of storage of its own of the frame,
    // an array or a container, read where the value that element holds
    // there can't be what was put in (PlaceHolds), `Values[0]`, by what it
    // designates (designatedPlaces).
    if (E != Use && At > 0 && !Step.bNumber && E->isGLValue() &&
        !E->getType()->isRecordType() && !E->getType()->isReferenceType()) {
      bool bTold = true;
      const llvm::SmallVector<CSlot, 2> Places =
          designatedPlaces(E, Context, &bTold);
      if (bTold && Places.size() == 1 &&
          Places.front().Place == EPlace::Variable && Places.front().Element &&
          Places.front().Index && ownedElement(Places.front(), nullptr) &&
          !PlaceHolds(E, At))
        continue;
    }
    const auto Implicit = ImplicitParent.find(E);
    DynTypedNode Node = Implicit != ImplicitParent.end()
                            ? DynTypedNode::create(*Implicit->second)
                            : pathParentOf(E, ParentOf, Context);
    // An element of the written form of a nested list that initializes a
    // record of the frame (isValueRecordType), which the parent map gives, is
    // an element of the list that is evaluated, `CNested Hold{{_pJob,
    // &Other}};`.
    if (const auto *List = Node.get<InitListExpr>();
        List && List->isSyntacticForm() && List->getSemanticForm()) {
      DynTypedNode Up = pathParentOf(List, ParentOf, Context);
      for (int Depth = 0; Depth < 8 && Up.get<InitListExpr>(); ++Depth)
        Up = pathParentOf(Up.get<InitListExpr>(), ParentOf, Context);
      if (const auto *Record = Up.get<VarDecl>();
          Record && Record->hasLocalStorage() &&
          !Record->getType()->isReferenceType() &&
          isValueRecordType(Record->getType(), Context))
        Node = DynTypedNode::create(*List->getSemanticForm());
    }
    // An integer made of the value of a pointer is that value: converted
    // back to a pointer it is the pointer again; copied into an integer of
    // the frame, its names go on, each (the provenance doesn't follow the
    // value of a number);
    // through parentheses, a conversion to another integer, an offset or
    // the arm of a conditional it goes on; compared, tested, hashed,
    // logged, given or returned as a number it touches nothing.
    if (Step.bNumber) {
      auto IsLocalNumber = [](const VarDecl *Variable) {
        return Variable && Variable->hasLocalStorage() &&
               !isa<ParmVarDecl>(Variable) &&
               !Variable->getType()->isReferenceType() &&
               (Variable->getType()->isIntegralOrEnumerationType());
      };
      // The variable it initializes, by the parent or by what the effect
      // table says the statement it is in does with it.
      const VarDecl *Into = Node.get<VarDecl>();
      if (const Stmt *In = Node.get<Stmt>(); !Into && In)
        for (const COperandEffect &Effect : effectsOf(In, Context).Operands)
          if (Effect.Operand == E &&
              Effect.Into.Kind == EDestination::Variable)
            Into = dyn_cast_or_null<VarDecl>(Effect.Into.Declaration);
      if (Into) {
        if (IsLocalNumber(Into))
          GoOnWith(Into, At, E, /*bCopy=*/false, /*bHolds=*/true, nullptr,
                   /*bNumber=*/true);
        continue;
      }
      const auto *Above = dyn_cast_or_null<Expr>(Node.get<Stmt>());
      if (const auto *Cast = dyn_cast_or_null<CastExpr>(Above)) {
        if (Cast->getType()->isPointerType())
          Next(Cast, At);
        else if (Cast->getType()->isIntegralOrEnumerationType())
          Next(Cast, At, false, false, nullptr, /*bNumber=*/true);
      } else if (isa_and_nonnull<ParenExpr>(Above))
        Next(Above, At, false, false, nullptr, /*bNumber=*/true);
      else if (const auto *Conditional =
                   dyn_cast_or_null<ConditionalOperator>(Above)) {
        if (Conditional->getCond() != E)
          Next(Above, At, false, false, nullptr, /*bNumber=*/true);
      } else if (const auto *Binary = dyn_cast_or_null<BinaryOperator>(Above)) {
        if (Binary->isAdditiveOp() &&
            Binary->getType()->isIntegralOrEnumerationType())
          Next(Above, At, false, false, nullptr, /*bNumber=*/true);
        else if (Binary->getOpcode() == BO_Assign && Binary->getRHS() == E)
          if (const auto *Variable = dyn_cast_or_null<VarDecl>(
                  namedDecl(Binary->getLHS(), Context));
              IsLocalNumber(Variable))
            GoOnWith(Variable, At, E, /*bCopy=*/false, /*bHolds=*/true,
                     nullptr, /*bNumber=*/true);
      }
      continue;
    }
    if (const auto *Variable = Node.get<VarDecl>()) {
      // A default argument is a value of its own.
      if (!isa<ParmVarDecl>(Variable))
        Declared(Variable, E, At);
      continue;
    }
    // A member a constructor initializes is of its object, a copy of what
    // a glvalue at 0 designates reads it (Rule R tells the caller).
    if (const auto *Initializer = Node.get<CXXCtorInitializer>()) {
      const FieldDecl *Field = Initializer->getMember();
      if (E->isGLValue() && At == 0 && Field &&
          !Field->getType()->isReferenceType())
        Uses.bRead = true;
      else
        Uses.bKept = true;
      continue;
    }
    const Stmt *Parent = Node.get<Stmt>();
    if (!Parent)
      continue;
    const auto *Above = dyn_cast<Expr>(Parent);
    // A step that can't be told because it is type-dependent reads, unless
    // it hands the value on whatever the types: parentheses, a conditional.
    auto Dependent = [&] {
      if (!Above || !Above->isTypeDependent())
        return false;
      if (isa<ParenExpr, ConditionalOperator, BinaryConditionalOperator>(
              Above))
        Next(Above, At, bIndirected, false, StepPath);
      else {
        Uses.bRead = true;
        Uses.bKept = true;
      }
      return true;
    };
    // Given to the promise, plumbing: by `co_return`, what the statement
    // gives the result (`return_value`); else what is yielded, kept.
    if (const auto *Call = dyn_cast<CallExpr>(Parent);
        Call && isPromiseCall(Call) &&
        llvm::is_contained(Call->arguments(), E)) {
      const FunctionDecl *Callee = Call->getDirectCallee();
      if (Callee && Callee->getDeclName().isIdentifier() &&
          identifierTextOf(Callee) == "return_value") {
        COperandEffect Returned;
        Returned.Operand = E;
        Returned.Use = EUse::Copy;
        Returned.Into.Kind = EDestination::Return;
        if (E->isGLValue() && At == 0)
          Uses.bRead = true;
        else
          KeptBy(E, At, Returned, Call);
      } else
        Uses.bKept = true;
      continue;
    }
    // Awaited, the awaiter holds it while the coroutine is suspended: the
    // operand, or what a suspension awaits, its common expression.
    if (isa<CoroutineSuspendExpr>(Parent)) {
      Uses.bKept = true;
      continue;
    }
    if (const auto *Awaiter = dyn_cast<OpaqueValueExpr>(Parent)) {
      const Stmt *Up = Awaiter;
      bool bAwaited = false;
      for (int Hops = 0; Hops < 8 && Up && !bAwaited; ++Hops) {
        Up = parentStmtOf(Up, ParentOf, Context);
        if (const auto *Suspend = dyn_cast_or_null<CoroutineSuspendExpr>(Up))
          bAwaited = Suspend->getCommonExpr() == Awaiter ||
                     Suspend->getCommonExpr() == Awaiter->getSourceExpr();
      }
      if (bAwaited) {
        Uses.bKept = true;
        continue;
      }
    }
    // The value of a statement expression, its last statement, `({
    // &_Value; })`.
    if (const auto *Block = dyn_cast<CompoundStmt>(Parent);
        Block && Block->body_back() == E) {
      if (const auto *Statement = dyn_cast_or_null<StmtExpr>(
              parentStmtOf(Block, ParentOf, Context))) {
        Next(Statement, At, bIndirected, false, StepPath);
        continue;
      }
    }
    // A member of a record that holds it (isValueRecordType), also through
    // a pointer to it: on the path the part goes on, a level down past a
    // reference member, which designates what it is bound to; off the path,
    // or of a type that can't hold it, nothing.
    if (const auto *Member = dyn_cast<MemberExpr>(Parent);
        Member && At > 0 && Member->getBase() == E &&
        isa<FieldDecl>(Member->getMemberDecl())) {
      const auto *Field = cast<FieldDecl>(Member->getMemberDecl());
      QualType Of = E->getType().getNonReferenceType();
      if (Member->isArrow())
        Of = Of->isPointerType() ? Of->getPointeeType() : QualType();
      if (!Of.isNull() && isValueRecordType(Of, Context)) {
        const QualType Type = Field->getType();
        const bool bTold = StepPath && StepPath->Field;
        // Another member of a union the one on the path is in is the same
        // storage: of the same type the same part of it, else any.
        const bool bShared =
            bTold && StepPath->Field != Field &&
            StepPath->Field->getParent() == Field->getParent() &&
            Field->getParent()->isUnion();
        if (bTold ? StepPath->Field != Field && !bShared
                  : !Type->isReferenceType() && refersNowhere(Type, Context))
          continue;
        const CMemberPath *Rest =
            !bTold ? StepPath
            : !bShared || Context.hasSameType(StepPath->Field->getType(), Type)
                ? StepPath->Next
                : nullptr;
        if (Type->isReferenceType() && !(Rest && Rest->Field))
          Next(Member, At - 1, bIndirected || Member->isArrow());
        else
          Next(Member, At, bIndirected || Member->isArrow(), false, Rest);
        continue;
      }
    }
    // A member function bound to it: the object of the call.
    if (const auto *Member = dyn_cast<MemberExpr>(Parent);
        Member && Member->hasPlaceholderType(BuiltinType::BoundMember)) {
      const auto *Method = dyn_cast<CXXMethodDecl>(Member->getMemberDecl());
      const Stmt *Called = parentStmtOf(Member, ParentOf, Context);
      while (isa_and_nonnull<ParenExpr>(Called))
        Called = parentStmtOf(Called, ParentOf, Context);
      // An explicit object is a parameter.
      if (Method && Method->isExplicitObjectMemberFunction())
        Given(dyn_cast_or_null<CallExpr>(Called), Method, 0, /*Bound=*/true,
              E, At);
      else
        CalledOn(dyn_cast_or_null<CallExpr>(Called), Method, E, At,
                 Method && Method->isConst());
      continue;
    }
    // Through a pointer to a member, `->*` and `.*`, a dereference
    // (pointerOperationOf): the part, of a member function the object of
    // the call.
    if (const auto *Binary = dyn_cast<BinaryOperator>(Parent);
        Binary && Binary->getLHS() == E &&
        pointerOperationOf(Binary->getOpcode()) ==
            EPointerOperation::Dereference) {
      if (Binary->hasPlaceholderType(BuiltinType::BoundMember)) {
        const auto *Pointer =
            Binary->getRHS()->getType()->getAs<MemberPointerType>();
        const auto *Prototype =
            Pointer ? Pointer->getPointeeType()->getAs<FunctionProtoType>()
                    : nullptr;
        CalledOn(nullptr, nullptr, E, At,
                 Prototype && Prototype->getMethodQuals().hasConst());
      } else
        Next(Binary, At, bIndirected || Binary->getOpcode() == BO_PtrMemI);
      continue;
    }
    // A conversion that keeps the value (pointerOperationOf), not a load; of
    // a pointer to an integer, the integer is the pointer's value (bNumber
    // of Next); to another type that can't hold a pointer, a reference or a
    // view the value is over (Rule R).
    if (const auto *Cast = dyn_cast<CastExpr>(Parent);
        Cast && Cast->getCastKind() != CK_LValueToRValue &&
        Cast->getCastKind() != CK_LValueToRValueBitCast &&
        pointerOperationOf(Cast->getCastKind()) ==
            EPointerOperation::Convert) {
      if (!Cast->isPRValue() || (!Cast->getType()->isArithmeticType() &&
                                 !Cast->getType()->isEnumeralType()))
        Next(Cast, At, bIndirected, false, StepPath);
      else if (E->isPRValue() && E->getType()->isPointerType() &&
               Cast->getType()->isIntegralOrEnumerationType() &&
               !Cast->getType()->isBooleanType())
        Next(Cast, At, false, false, nullptr, /*bNumber=*/true);
      continue;
    }
    bool bFound = false;
    for (const COperandEffect &Effect : effectsOf(Parent, Context).Operands) {
      if (Effect.Operand != E)
        continue;
      bFound = true;
      switch (Effect.Use) {
      case EUse::Forward:
        if (Above)
          Next(Above, At, bIndirected, false, StepPath);
        break;
      // Loaded: of a pointer, what it points to is the same record, with the
      // same parts.
      case EUse::Access:
        if (At == 0)
          Uses.bRead = true;
        else if (Above)
          Next(Above, At - 1, false, false,
               E->getType()->isPointerType() ? StepPath : nullptr);
        break;
      case EUse::Update:
        if (At == 0) {
          Uses.bRead = true;
          Uses.bWritten = true;
        } else if (Above && Above->isPRValue())
          Next(Above, At - 1);
        break;
      case EUse::Write:
        Uses.bWritten = Uses.bWritten || At == 0;
        break;
      case EUse::Dereference:
        if (isa<CXXDeleteExpr>(Parent))
          Uses.bWritten = Uses.bWritten || At == 0;
        else if (Above)
          Next(Above, At, /*bIndirected=*/true, false, StepPath);
        break;
      case EUse::AddressOf:
        if (Above)
          Next(Above, At, false, false, StepPath);
        break;
      case EUse::Bind:
      case EUse::Copy: {
        const bool Bound = Effect.Use == EUse::Bind;
        const CDestination &Into = Effect.Into;
        if (Into.Kind == EDestination::Parameter) {
          // What only gives back what it is given, `fg_Move`, `fg_Forward`,
          // `std::addressof` (givesItsArgument), or a pointer the compiler
          // only looks at (keptPointerArgument), as the provenance takes it:
          // the call's value at the same level.
          if (const auto *Call = dyn_cast<CallExpr>(Parent);
              Call && Into.Callee &&
              ((givesItsArgument(Call) && Into.Index == 0) ||
               keptPointerArgument(Call) == E)) {
            Next(Call, At, bIndirected);
            break;
          }
          // A copy or a move is a copy (Rule V): what a glvalue at 0
          // designates is read; a copy made of a view goes on with it as the
          // view, of anything else it is a value of its own; a copy or a move
          // assignment the compiler defines is over where it assigns a value
          // of records, and else keeps it.
          const auto *Construct = dyn_cast<CXXConstructExpr>(Parent);
          const auto *Constructor =
              Construct ? Construct->getConstructor() : nullptr;
          const auto *Assignment =
              Construct ? nullptr
                        : dyn_cast_or_null<CXXMethodDecl>(Into.Callee);
          if (Into.Index == 0 &&
              ((Constructor && Constructor->isCopyOrMoveConstructor()) ||
               (Assignment && !Assignment->isUserProvided() &&
                (Assignment->isCopyAssignmentOperator() ||
                 Assignment->isMoveAssignmentOperator())))) {
            if (E->isGLValue() && At == 0)
              Uses.bRead = true;
            else if (Construct) {
              if (isDefaultViewType(Construct->getType(), Context))
                Next(Construct, At);
              // What a copy or a move the compiler defines makes holds what
              // its source's part the walk knows holds (CMemberPath); of an
              // untold part, or one written, it is a value (Rule V).
              else if (StepPath && !Constructor->isUserProvided() &&
                       isValueRecordType(Construct->getType(), Context))
                Next(Construct, At, false, false, StepPath);
            } else if (!isValueRecordType(
                           Context.getCanonicalTagType(Assignment->getParent()),
                           Context))
              Uses.bKept = true;
            else if (const auto *Receiver = dyn_cast_or_null<VarDecl>(
                         namedDecl(assignedObject(Parent), Context));
                     Receiver && Receiver->hasLocalStorage() &&
                     !Receiver->getType()->isReferenceType()) {
              if (StepPath)
                HeldInRecord(Receiver, dyn_cast<Expr>(Parent), At, StepPath);
            } else
              Uses.bKept = true;
            break;
          }
          if (!Bound && E->isGLValue())
            At == 0 ? (void)(Uses.bRead = true) : (void)(Uses.bGiven = true);
          else
            Given(dyn_cast<Expr>(Parent), Into.Callee, Into.Index, Bound, E,
                  At);
          break;
        }
        if (Into.Kind == EDestination::Variable) {
          if (const auto *Variable =
                  dyn_cast_or_null<VarDecl>(Into.Declaration)) {
            Declared(Variable, E, At);
            break;
          }
        }
        // Assigned to a pointer of the frame, `pInitial = pValue;`.
        if (Into.Kind == EDestination::Designated && Into.Object) {
          const auto *Variable =
              dyn_cast_or_null<VarDecl>(namedDecl(Into.Object, Context));
          if (Variable && Variable->hasLocalStorage() &&
              Variable->getType()->isPointerType()) {
            IntoPointer(Variable, E, At, /*bHolds=*/true);
            // What the assignment gives back is the pointer given it,
            // `*(pValue = pSaved)`.
            if (Above && Above->isGLValue() && !(E->isGLValue() && At == 0))
              Next(Above, E->isGLValue() ? At : At + 1);
            break;
          }
        }
        // Assigned to a member of a record of the frame, `Result.m_pValue =
        // &Value;`, also an element of an array member of it: held there
        // (HeldInRecord).
        if (Into.Kind == EDestination::Designated && Into.Object) {
          const CMemberPath *Held = nullptr;
          if (const VarDecl *Record =
                  recordOfMember(Into.Object, Held, Context)) {
            HeldInRecord(Record, E,
                         heldLevelOf(E, Bound, At, /*Record=*/true), Held);
            break;
          }
        }
        // Captured by a closure whose code runs in the frame: by reference
        // its names, by copy a pointer's names, a copy of what is there
        // reads it.
        if (Into.Kind == EDestination::Capture) {
          const auto *Closure = dyn_cast_or_null<LambdaExpr>(Into.Object);
          const auto *Captured = dyn_cast_or_null<VarDecl>(Into.Declaration);
          // `this` captured by a closure that is called in the scope that
          // makes it: its body's names of `this` are the object's own names
          // (namesIn), followed where they are; the capture holds nothing
          // past them.
          if (Closure && !Captured &&
              effectsOf(E, Context).Statement == EStatement::This &&
              calledInScope(Closure, Context))
            break;
          if (Closure && Captured && RunsInFrame(Closure, E)) {
            // Held there unless the closure is called where it is made, or
            // what the closure holds keeps it alive (KeptAt, Rule K).
            const bool bHolds =
                !isCalledWhereMade(Closure,
                                   [&](const Stmt *S) {
                                     return parentStmtOf(S, ParentOf, Context);
                                   }) &&
                (!KeptAt || KeptAt(E, At, Effect, Parent));
            if (Bound)
              GoOnWith(Captured, At, E, /*bCopy=*/false, bHolds);
            else if (Captured->getType()->isPointerType())
              IntoPointer(Captured, E, At, bHolds);
            else
              CopiedInto(Captured, Captured->getType(), /*bLocal=*/true, E,
                         At);
            break;
          }
        }
        // Given back by a `return` of the body a parameter is of: the caller
        // goes on with the call's value (Given), unless a copy of what is
        // there, which reads it.
        if (Into.Kind == EDestination::Return && bBody &&
            isa<ReturnStmt>(Parent)) {
          // A copy of a pointer gives back what it points to a level down.
          const unsigned Returned =
              !Bound && E->isGLValue() && !E->getType()->isRecordType() ? At - 1
                                                                        : At;
          if (!Bound && E->isGLValue() && At == 0)
            Uses.bRead = true;
          else if (Returned < 16)
            Uses.ReturnedLevels |= 1u << Returned;
          else
            Uses.bKept = true;
          break;
        }
        // Made into a temporary, which goes on as the slot that holds it,
        // `fg_ByReference(&_Job)`; a copy of what is there reads it.
        if (Into.Kind == EDestination::Temporary && Into.Object) {
          if (E->isGLValue() && At == 0)
            Uses.bRead = true;
          else if (E->getType()->isRecordType())
            Next(Into.Object, At, false, false, StepPath);
          else
            Next(Into.Object, E->isGLValue() ? At : At + 1);
          break;
        }
        // Put in an aggregate, a member, a base or an element of what is
        // made, which goes on with it as a part; a copy of what is there
        // reads it.
        if ((Into.Kind == EDestination::Field ||
             Into.Kind == EDestination::Element ||
             Into.Kind == EDestination::Base) &&
            Into.Object) {
          if (!Bound && E->isGLValue() && At == 0)
            Uses.bRead = true;
          else {
            // Of a member, that member of it, then the part of what is put
            // in it that holds it; an element or a base adds no step.
            const auto *Member =
                dyn_cast_or_null<FieldDecl>(Into.Declaration);
            const CMemberPath *Held =
                Into.Kind != EDestination::Field ? StepPath
                : Member ? memberPath(Member, StepPath, Context)
                         : nullptr;
            Next(Into.Object, heldLevelOf(E, Bound, At, /*Record=*/true),
                 false, false, Held);
          }
          break;
        }
        // A copy of what is there reads it; anything else keeps it beyond
        // the frame, as KeptAt tells.
        if (!Bound && E->isGLValue() && At == 0)
          Uses.bRead = true;
        else
          KeptBy(E, At, Effect, Parent);
        break;
      }
      case EUse::CallObject: {
        const auto *Call = dyn_cast<CallExpr>(Parent);
        const auto *Method = Call ? dyn_cast_or_null<CXXMethodDecl>(
                                        Call->getCalleeDecl())
                                  : nullptr;
        // An explicit object is a parameter.
        if (Method && Method->isExplicitObjectMemberFunction())
          Given(Call, Method, 0, /*Bound=*/true, E, At);
        else
          CalledOn(Call, Method, E, At, Method && Method->isConst());
        break;
      }
      case EUse::Value:
        Uses.bRead = Uses.bRead || (E->isGLValue() && At == 0);
        break;
      // What the effect table can't tell is over, unless it is
      // type-dependent.
      case EUse::Opaque:
        (void)Dependent();
        break;
      // Discarded, what a dereference gave is still reached through the
      // pointer, `(void)*pEntry`.
      case EUse::Discarded:
        Uses.bRead = Uses.bRead || (bIndirected && At == 0 && E->isGLValue());
        break;
      case EUse::Callee:
      case EUse::Condition:
      case EUse::Statement:
      case EUse::Unevaluated:
        break;
      }
    }
    // An operand that is \p E past implicit nodes the parent map leaves
    // out: told again through them.
    if (!bFound && !ImplicitParent.count(E))
      for (const COperandEffect &Effect : effectsOf(Parent, Context).Operands) {
        const auto *Operand = dyn_cast_or_null<Expr>(Effect.Operand);
        if (!Operand || Operand == E)
          continue;
        llvm::SmallVector<std::pair<const Stmt *, const Stmt *>, 4> Links;
        const Stmt *Outer = Parent;
        const Expr *Inner = Operand;
        for (int Depth = 0; Inner && Inner != E && Depth < 8; ++Depth) {
          Links.push_back({Inner, Outer});
          Outer = Inner;
          if (const auto *Cast = dyn_cast<ImplicitCastExpr>(Inner))
            Inner = Cast->getSubExpr();
          else if (const auto *Full = dyn_cast<FullExpr>(Inner))
            Inner = Full->getSubExpr();
          else if (const auto *Temporary =
                       dyn_cast<MaterializeTemporaryExpr>(Inner))
            Inner = Temporary->getSubExpr();
          else if (const auto *Bind = dyn_cast<CXXBindTemporaryExpr>(Inner))
            Inner = Bind->getSubExpr();
          else
            Inner = nullptr;
        }
        if (Inner != E)
          continue;
        for (const auto &[Link, Into] : Links)
          ImplicitParent.try_emplace(Link, Into);
        ImplicitParent.try_emplace(E, Outer);
        Seen.erase({{E, At}, StepPath});
        Next(E, At, bIndirected, false, StepPath);
        bFound = true;
        break;
      }
    // What it is in that the effect table doesn't tell is over, unless it
    // is type-dependent.
    if (!bFound)
      (void)Dependent();
  }
  // A walk that runs out of its steps hasn't told where it goes: kept.
  if (!Work.empty())
    Total.bKept = true;
  return Total;
}

const Expr *keptDerivationOf(const Expr *Use, FParentOf ParentOf,
                             ASTContext &Context) {
  const Expr *Whole = Use;
  for (int Step = 0; Step < 1024; ++Step) {
    const Expr *Above = parentExprOf(Whole, ParentOf, Context);
    if (!Above || isa<LambdaExpr>(Above))
      break;
    Whole = Above;
  }
  return Whole;
}

// What an operator or a cast does with a pointer value: each enumerator
// classified, no default, so a new one is a warning until it is.
EPointerOperation pointerOperationOf(BinaryOperatorKind Kind) {
  switch (Kind) {
  case BO_Cmp:
  case BO_LT:
  case BO_GT:
  case BO_LE:
  case BO_GE:
  case BO_EQ:
  case BO_NE:
  case BO_LAnd:
  case BO_LOr:
    return EPointerOperation::Compare;
  case BO_Add:
  case BO_Sub:
  case BO_AddAssign:
  case BO_SubAssign:
    return EPointerOperation::Offset;
  case BO_PtrMemD:
  case BO_PtrMemI:
    return EPointerOperation::Dereference;
  case BO_Mul:
  case BO_Div:
  case BO_Rem:
  case BO_Shl:
  case BO_Shr:
  case BO_And:
  case BO_Xor:
  case BO_Or:
  case BO_Assign:
  case BO_MulAssign:
  case BO_DivAssign:
  case BO_RemAssign:
  case BO_ShlAssign:
  case BO_ShrAssign:
  case BO_AndAssign:
  case BO_XorAssign:
  case BO_OrAssign:
  case BO_Comma:
    return EPointerOperation::None;
  }
  return EPointerOperation::None;
}

EPointerOperation pointerOperationOf(UnaryOperatorKind Kind) {
  switch (Kind) {
  case UO_LNot:
    return EPointerOperation::Compare;
  case UO_PostInc:
  case UO_PostDec:
  case UO_PreInc:
  case UO_PreDec:
    return EPointerOperation::Offset;
  case UO_Deref:
    return EPointerOperation::Dereference;
  case UO_Plus:
  case UO_Extension:
    return EPointerOperation::Convert;
  case UO_AddrOf:
  case UO_Minus:
  case UO_Not:
  case UO_Real:
  case UO_Imag:
  case UO_Coawait:
    return EPointerOperation::None;
  }
  return EPointerOperation::None;
}

EPointerOperation pointerOperationOf(CastKind Kind) {
  switch (Kind) {
  case CK_PointerToBoolean:
  case CK_MemberPointerToBoolean:
    return EPointerOperation::Compare;
  case CK_BitCast:
  case CK_LValueBitCast:
  case CK_LValueToRValueBitCast:
  case CK_LValueToRValue:
  case CK_NoOp:
  case CK_BaseToDerived:
  case CK_DerivedToBase:
  case CK_UncheckedDerivedToBase:
  case CK_Dynamic:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
  case CK_BaseToDerivedMemberPointer:
  case CK_DerivedToBaseMemberPointer:
  case CK_ReinterpretMemberPointer:
  case CK_IntegralToPointer:
  case CK_PointerToIntegral:
  case CK_CPointerToObjCPointerCast:
  case CK_BlockPointerToObjCPointerCast:
  case CK_AnyPointerToBlockPointerCast:
  case CK_ObjCObjectLValueCast:
  case CK_ARCProduceObject:
  case CK_ARCConsumeObject:
  case CK_ARCReclaimReturnedObject:
  case CK_ARCExtendBlockObject:
  case CK_AtomicToNonAtomic:
  case CK_NonAtomicToAtomic:
  case CK_CopyAndAutoreleaseBlockObject:
  case CK_BuiltinFnToFnPtr:
  case CK_AddressSpaceConversion:
    return EPointerOperation::Convert;
  case CK_Dependent:
  case CK_ToUnion:
  case CK_NullToPointer:
  case CK_NullToMemberPointer:
  case CK_UserDefinedConversion:
  case CK_ConstructorConversion:
  case CK_ToVoid:
  case CK_MatrixCast:
  case CK_VectorSplat:
  case CK_IntegralCast:
  case CK_IntegralToBoolean:
  case CK_IntegralToFloating:
  case CK_FloatingToFixedPoint:
  case CK_FixedPointToFloating:
  case CK_FixedPointCast:
  case CK_FixedPointToIntegral:
  case CK_IntegralToFixedPoint:
  case CK_FixedPointToBoolean:
  case CK_FloatingToIntegral:
  case CK_FloatingToBoolean:
  case CK_BooleanToSignedIntegral:
  case CK_FloatingCast:
  case CK_FloatingRealToComplex:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexToBoolean:
  case CK_FloatingComplexCast:
  case CK_FloatingComplexToIntegralComplex:
  case CK_IntegralRealToComplex:
  case CK_IntegralComplexToReal:
  case CK_IntegralComplexToBoolean:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex:
  case CK_ZeroToOCLOpaqueType:
  case CK_IntToOCLSampler:
  case CK_HLSLVectorTruncation:
  case CK_HLSLMatrixTruncation:
  case CK_HLSLArrayRValue:
  case CK_HLSLElementwiseCast:
  case CK_HLSLAggregateSplatCast:
    return EPointerOperation::None;
  }
  return EPointerOperation::None;
}

bool aliveWhereUsed(const CLifetimeUse &Use, ASTContext &Context) {
  if (!Use.Use)
    return false;
  // The context where the use is, of the function executing it
  // (useContext); of what is kept elsewhere, where it is kept (keptContext).
  // Of a value kept in a local of the frame, which is read where that local
  // is, after the use, the locals of the function whose scope is open at
  // the use, wherever declared in it.
  CStorageUse At =
      (Use.Used != EUsed::Kept && Use.Used != EUsed::Pointee) ||
              Use.KeptIn.Kind == CKeptAt::EKind::Local
          ? useContext(Use.Use, Context, Use.ParentOf, Use.NotKept)
          : keptContext(Use.KeptIn, Context);
  if (Use.Used == EUsed::Kept && Use.KeptIn.Kind == CKeptAt::EKind::Local)
    for (CHeldValue &Each : At.Held)
      if (Each.Local && Each.Where == Use.Use)
        Each.bOrdered = false;
  // Whether what \p E reaches at \p Level is alive, derived by \p
  // Derivation: a copy a closure keeps where it is kept alive.
  auto AliveAt = [&](const Expr *E, unsigned Level, const Expr *Derivation) {
    const LambdaExpr *CopyIn = nullptr;
    const CStorageRoots Roots =
        rootsAtUse(E, Level, At.Closure, Context, CopyIn);
    if (CopyIn)
      return CopyIn->getLambdaClass() != At.NotKept;
    return allAlive(Roots, At, Context, {},
                    derivationAnchors(Derivation, Context));
  };
  // What the access path \p Path touches, derived by \p Derivation: its
  // outermost expression, a glvalue what it designates, a pointer value
  // what it points to, another value that can refer to storage what it
  // refers to; what its steps read, of what something keeps (\p Reads
  // false) only what a suspension reads when it resumes, and of that a
  // pointer or a view also what it refers to. What is the use's own
  // storage, by the use as written.
  auto AliveOnPath = [&](const CAccessPath &Path, const Expr *Derivation,
                         bool Reads) {
    auto Touched = [&](const Expr *E, unsigned Level, unsigned Relative) {
      return Relative + Level == 0 ? AliveAt(Use.Use, 0, Derivation)
                                   : AliveAt(E, Level, Derivation);
    };
    const Expr *Top = Path.Top;
    const QualType Type = Top->getType().getNonReferenceType();
    // Of a use where it is, what the top refers to, the use's storage at
    // the level the top reaches, where nothing touches it (storageUsesOf),
    // only compared or tested, also past its address, isn't touched; nor is
    // what is past it, which is reached only by reading it. What a local
    // keeps across a suspension is asked by itself.
    const bool Compared =
        Reads && Path.TopLevel != UntoldRelative &&
        storageUsesOf(Use.Use, Path.TopLevel, Context, Use.ParentOf).none();
    if (!Compared &&
        (Top->isGLValue() || Type->isPointerType()
             ? !Touched(Top, 0, Path.TopLevel)
             : holdsBorrowed(Type, Context) &&
                   !Touched(Top, 1, Path.TopLevel)))
      return false;
    // A pointer or a view the path ends at: what it refers to, of what
    // something keeps, and of the use's own storage where it reads what it
    // refers to by the one use classification (readsOnlyOwnPart), a getter
    // whose body reads it into a new value, `_Text.f_GetFirstChunk()`.
    const auto *Named = dyn_cast<DeclRefExpr>(Use.Use->IgnoreParenImpCasts());
    if (!Compared && Top->isGLValue() &&
        (Type->isPointerType() || isDefaultViewType(Type, Context)) &&
        (!Reads ||
         (Path.TopLevel == 0 && Named &&
          !readsOnlyOwnPart(Use.Use, Named->getDecl(),
                            Named->getDecl()->getType().getNonReferenceType(),
                            Context))) &&
        !Touched(Top, 1, Path.TopLevel))
      return false;
    // A copy a closure keeps of the record a local reference refers to,
    // given where more than its own part can be read (readsOnlyOwnPart),
    // `fg_Read(Request)` of `[Request]`: what its pointer, reference and
    // view parts refer to, which the copy shares (borrowedPartRoots).
    if (!Compared && Reads && Top->isGLValue() && Path.TopLevel == 0 &&
        Named && Type->isRecordType() && !isDefaultViewType(Type, Context))
      if (const auto *Copied = dyn_cast<VarDecl>(Named->getDecl());
          Copied && Copied->getType()->isReferenceType() &&
          Copied->getInit() && closureCopying(Copied, Use.Use) &&
          !readsOnlyOwnPart(Use.Use, Copied, Type, Context) &&
          !allAlive(borrowedPartRoots(
                        Copied->getInit(),
                        dyn_cast_or_null<FunctionDecl>(
                            Copied->getParentFunctionOrMethod()),
                        Context),
                    At, Context, {}, derivationAnchors(Derivation, Context)))
        return false;
    return llvm::all_of(Path.Reads, [&](const CAccessRead &Read) {
      return (!Reads && !Read.bResumed) ||
             Touched(Read.Read, Read.Level, Read.Relative);
    });
  };
  switch (Use.Used) {
  case EUsed::Designated: {
    const CAccessPath Path = accessPathOf(Use.Use, Use.ParentOf, Context);
    return AliveOnPath(Path, Path.Top, /*Reads=*/true);
  }
  case EUsed::Kept: {
    // Of an argument a call keeps, the argument: the call keeps it, not the
    // expression the call is in.
    const bool Argument =
        Use.KeptIn.Call &&
        llvm::is_contained(Use.KeptIn.Call->arguments(), Use.Use);
    return AliveOnPath(
        accessPathOf(Use.Use, Use.ParentOf, Context),
        Argument ? Use.Use : keptDerivationOf(Use.Use, Use.ParentOf, Context),
        /*Reads=*/false);
  }
  case EUsed::Landed: {
    // Where each way of the derivation lands (CStorageRoot::Hops): the hop
    // nearest the use dereferences an owner of the `shared` or `unique` kind,
    // not past what it only refers to, whose owned type the context keeps.
    // What a pointer names, what it points to.
    const QualType Type = Use.Use->getType().getNonReferenceType();
    const LambdaExpr *CopyIn = nullptr;
    const CStorageRoots Roots = rootsAtUse(
        Use.Use, Use.Use->isGLValue() && Type->isPointerType() ? 1 : 0,
        At.Closure, Context, CopyIn);
    return !CopyIn && !Roots.Roots.empty() &&
           llvm::all_of(Roots.Roots, [&](const CStorageRoot &Root) {
             if (Root.Kind != ERootKind::Owned || Root.Hops.empty())
               return false;
             const CHop &Landed = Root.Hops.front();
             return Landed.Kind == CHop::EKind::Owner && !Landed.bPast &&
                    (Landed.OwnerKind == EOwnerKind::Shared ||
                     Landed.OwnerKind == EOwnerKind::Unique) &&
                    Landed.Anchor &&
                    holdsKeeper(At.Held, *Landed.Anchor, Context);
           });
  }
  case EUsed::Pointee:
    return allAlive(pointedAtRootsOf(Use.Use, Context), At, Context, {},
                    derivationAnchors(Use.Use, Context));
  }
  return false;
}

bool reportLifetime(MalterlibCheck &Check, const CLifetimeUse &Use,
                    ASTContext &Context, const CLifetimeReport &Report) {
  if (Report.Location.isInvalid() || aliveWhereUsed(Use, Context))
    return false;
  {
    const MalterlibDiagnostic Warning =
        Check.diag(Report.Location, Report.Message);
    if (Report.Arguments)
      Report.Arguments(Warning);
  }
  if (!Report.Note.empty() && Use.Use) {
    const MalterlibDiagnostic Note = Check.diag(
        Use.Use->getBeginLoc(), Report.Note, DiagnosticIDs::Note);
    if (Report.NoteArguments)
      Report.NoteArguments(Note);
  }
  return true;
}

/// Whether a keeper in what a context holds can keep \p Root alive, so that
/// where it is used can tell: what owners own, another object, what a
/// parameter refers to, and what can't be told when it or the derivation of
/// the borrow has an anchor (\p Derivation).
static bool keptByKeeper(const CStorageRoot &Root,
                         llvm::ArrayRef<CAnchor> Derivation) {
  switch (Root.Kind) {
  case ERootKind::Owned:
  case ERootKind::Object:
  case ERootKind::Parameter:
    return true;
  case ERootKind::Unknown:
    return !Root.Anchors.empty() || !Derivation.empty();
  case ERootKind::Static:
  case ERootKind::ThreadLocal:
  case ERootKind::Actor:
  case ERootKind::UnsafeThis:
  case ERootKind::Frame:
  case ERootKind::Temporary:
    return false;
  }
  return false;
}

namespace {
/// Of each closure, the names in its body that can run of each declaration,
/// told once (captureAlive).
struct CClosureNames {
  llvm::DenseMap<const LambdaExpr *,
                 llvm::DenseMap<const ValueDecl *,
                                llvm::SmallVector<const DeclRefExpr *, 2>>>
      Of;
};
} // namespace

bool captureAlive(const LambdaExpr *Closure, const LambdaCapture &Capture,
                  ASTContext &Context, bool EachUse) {
  const ValueDecl *Captured =
      Capture.capturesVariable() ? Capture.getCapturedVar() : nullptr;
  if (!Captured || !Closure->getBody())
    return false;
  // What the closure holds, wherever in its body (closureUse without a
  // use), with the anchors of the capture's initializer, its derivation.
  const CStorageUse Anywhere = closureUse(Closure, Context);
  const CStorageRoots Roots = captureRoots(Closure, Capture, Context);
  const llvm::ArrayRef<CAnchor> Derivation = derivationAnchors(
      captureInitOf(Closure, &Capture - Closure->capture_begin()), Context);
  if (allAlive(Roots, Anywhere, Context, {}, Derivation))
    return true;
  // Not when what isn't alive anywhere in it can't be kept by a keeper.
  if (!EachUse || !Roots.all([&](const CStorageRoot &Root) {
        return keptByKeeper(Root, Derivation) ||
               aliveBy(Root, Anywhere, Context, Derivation);
      }))
    return false;
  // Else alive at each use of it in the body (aliveAtUse), by what is held
  // there, also in the lambdas in it: what it refers to, a level down of a
  // copy, derived by the use.
  // The names of each declaration in the body that can run, by a work stack
  // over what runs, once for each closure.
  auto [It, Inserted] =
      astCache<CClosureNames>(Context).Of.try_emplace(Closure);
  if (Inserted) {
    llvm::SmallVector<const Stmt *, 32> Work{Closure->getBody()};
    llvm::SmallPtrSet<const Stmt *, 32> Seen;
    while (!Work.empty()) {
      const Stmt *S = Work.pop_back_val();
      if (!Seen.insert(S).second)
        continue;
      if (const auto *Ref = dyn_cast<DeclRefExpr>(S))
        It->second[Ref->getDecl()].push_back(Ref);
      for (const Stmt *Child : keptChildren(S, Context))
        if (Child)
          Work.push_back(Child);
    }
  }
  const auto Names = It->second.find(Captured);
  if (Names == It->second.end())
    return false;
  const unsigned Level = Capture.getCaptureKind() == LCK_ByRef ? 0 : 1;
  for (const DeclRefExpr *Ref : Names->second)
    if (!aliveAtUse(Ref, Level, useContext(Ref, Context), Context))
      return false;
  return true;
}

bool holdsBorrowed(QualType Type, const ASTContext &Context) {
  return forEachHeld(Type, [&](const CHeld &Held) {
    switch (Held.Kind) {
    case EHeld::Record: {
      const EOwnerKind Kind = ownerKindOf(Held.Type, Context);
      return Kind != EOwnerKind::None && Kind != EOwnerKind::Weak
                 ? EVisit::Skip
                 : EVisit::Descend;
    }
    case EHeld::MemberPointer:
      return EVisit::Skip;
    case EHeld::Pointer:
    case EHeld::Reference:
    case EHeld::BlockPointer:
    case EHeld::Dependent:
    case EHeld::Opaque:
      return EVisit::Found;
    }
    return EVisit::Found;
  });
}

namespace {
/// The calls of each function of a translation unit, and the other names of
/// it, which take its address (givenOwnActor).
class CCallIndex : public OncePerTypeVisitor<CCallIndex> {
public:
  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const { return true; }
  // Each statement once: the operand of a co_await is met again in the
  // expressions that make and call its awaiter.
  bool dataTraverseStmtPre(Stmt *S) { return Traversed.insert(S).second; }

  bool VisitCallExpr(CallExpr *Call) {
    if (const FunctionDecl *Callee = Call->getDirectCallee()) {
      Calls[Callee->getCanonicalDecl()].push_back(Call);
      Callees.insert(Call->getCallee()->IgnoreParenImpCasts());
    }
    return true;
  }
  bool VisitDeclRefExpr(DeclRefExpr *Ref) {
    if (const auto *Function = dyn_cast<FunctionDecl>(Ref->getDecl()))
      Names[Function->getCanonicalDecl()].push_back(Ref);
    return true;
  }
  bool VisitMemberExpr(MemberExpr *Member) {
    if (const auto *Function = dyn_cast<FunctionDecl>(Member->getMemberDecl()))
      Names[Function->getCanonicalDecl()].push_back(Member);
    return true;
  }

  llvm::DenseMap<const FunctionDecl *, llvm::SmallVector<const CallExpr *, 2>>
      Calls;
  llvm::DenseMap<const FunctionDecl *, llvm::SmallVector<const Expr *, 2>>
      Names;
  llvm::DenseSet<const Expr *> Callees;
  llvm::DenseSet<const Stmt *> Traversed;
};

/// What a translation unit tells of the parameters that are given the
/// object of the actor the caller runs on (givenOwnActor).
struct COwnActorParameters {
  bool Indexed = false;
  CCallIndex Index;
  TCSummaries<const ParmVarDecl *, bool> Given{
      [](const ParmVarDecl *const &Parameter) {
        return givenOwnActorUncached(Parameter);
      },
      true, false};
  static bool givenOwnActorUncached(const ParmVarDecl *Parameter);
};
} // namespace

/// Whether each call of the function of \p Parameter, a pointer or a
/// reference to an actor (takesActorObject), gives it the object of the
/// actor the caller runs on, `fs_Read(this)`, also a parameter of the caller
/// that is (storageRootsOf): there are calls, and nothing else names the
/// function, which would take its address.
bool COwnActorParameters::givenOwnActorUncached(const ParmVarDecl *Parameter) {
  ASTContext &Context = Parameter->getASTContext();
  const auto *Function = dyn_cast<FunctionDecl>(Parameter->getDeclContext());
  if (!Function || !takesActorObject(Parameter))
    return false;
  COwnActorParameters &Facts = astCache<COwnActorParameters>(Context);
  if (!Facts.Indexed) {
    Facts.Indexed = true;
    Facts.Index.TraverseAST(Context);
    Facts.Index.Traversed.clear();
  }
  const FunctionDecl *Canonical = Function->getCanonicalDecl();
  const auto Calls = Facts.Index.Calls.find(Canonical);
  if (Calls == Facts.Index.Calls.end() || Calls->second.empty())
    return false;
  for (const Expr *Name : Facts.Index.Names.lookup(Canonical))
    if (!Facts.Index.Callees.contains(Name))
      return false;
  const QualType Type = Parameter->getType();
  // Of a reference to a pointer, what the pointer it refers to points to.
  const unsigned Level =
      Type->isReferenceType() && Type.getNonReferenceType()->isPointerType()
          ? 1
          : 0;
  for (const CallExpr *Call : Calls->second) {
    const unsigned iArgument =
        Parameter->getFunctionScopeIndex() + argumentShift(Call, Function);
    if (iArgument >= Call->getNumArgs())
      return false;
    const CStorageRoots Roots =
        storageRootsOf(Call->getArg(iArgument), Context, Level);
    if (Roots.Roots.empty() || !Roots.all([](const CStorageRoot &Root) {
          return Root.Kind == ERootKind::Actor && !Root.bPart;
        }))
      return false;
  }
  return true;
}

static bool givenOwnActor(const ParmVarDecl *Parameter, ASTContext &Context) {
  return astCache<COwnActorParameters>(Context).Given.get(Parameter);
}

} // namespace clang::tidy::malterlib
