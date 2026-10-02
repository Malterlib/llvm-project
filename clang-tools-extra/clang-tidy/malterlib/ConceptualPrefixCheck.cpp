//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ConceptualPrefixCheck.h"
#include "CheckedAccess.h"
#include "../utils/OptionsUtils.h"
#include "NamingUtils.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

/// The concept prefix of a range, which an array is.
static constexpr char RangePrefix = 'r';

/// The pointer that pointer arithmetic in a template steps, whose type is
/// dependent there, `_pDest + (_Len - 1)` of `tf_CData *_pDest`, and is that
/// pointer's type whatever the arguments are: of a pointer and an integer.
static QualType steppedPointer(const Expr *E, int Depth = 0) {
  if (stackNearlyExhausted())
    return withStack([&] { return steppedPointer(E, Depth); });
  const auto *Binary = dyn_cast<BinaryOperator>(E->IgnoreParenImpCasts());
  if (!Binary || !Binary->isAdditiveOp() || Depth > 16)
    return QualType();
  auto Integer = [](const Expr *Operand) {
    const QualType Of = Operand->getType();
    return !Of->isDependentType() && Of->isIntegralOrUnscopedEnumerationType();
  };
  auto Pointer = [&](const Expr *Operand) {
    const QualType Of = Operand->IgnoreParenImpCasts()->getType();
    return Of->isPointerType() ? Of : steppedPointer(Operand, Depth + 1);
  };
  if (const QualType Of = Pointer(Binary->getLHS());
      !Of.isNull() && Integer(Binary->getRHS()))
    return Of;
  if (Binary->getOpcode() == BO_Add)
    if (const QualType Of = Pointer(Binary->getRHS());
        !Of.isNull() && Integer(Binary->getLHS()))
      return Of;
  return QualType();
}

// The types of the standard library; the Malterlib options file adds
// Malterlib's.
static constexpr llvm::StringLiteral BuiltInSmartPointerTypes =
    "std::unique_ptr;std::shared_ptr;std::weak_ptr";
static constexpr llvm::StringLiteral BuiltInFunctionObjectTypes =
    "std::function;std::move_only_function;std::copyable_function";
static constexpr llvm::StringLiteral BuiltInIteratorTypes = "*iterator";
static constexpr llvm::StringLiteral BuiltInValueWrapperTypes = "std::atomic";

ConceptualPrefixCheck::ConceptualPrefixCheck(StringRef Name,
                                             ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      SmartPointerTypes(utils::options::parseStringList(
          listOption("SmartPointerTypes", BuiltInSmartPointerTypes))),
      SmartPointerHandleTypes(utils::options::parseStringList(
          listOption("SmartPointerHandleTypes", ""))),
      FunctionObjectTypes(utils::options::parseStringList(
          listOption("FunctionObjectTypes", BuiltInFunctionObjectTypes))),
      IteratorTypes(utils::options::parseStringList(
          listOption("IteratorTypes", BuiltInIteratorTypes))),
      ValueWrapperTypes(utils::options::parseStringList(
          listOption("ValueWrapperTypes", BuiltInValueWrapperTypes))),
      FlagTypes(utils::options::parseStringList(listOption("FlagTypes", ""))),
      IgnoredNames(
          utils::options::parseStringList(listOption("IgnoredNames", ""))),
      NotFunctionTypes(utils::options::parseStringList(
          listOption("NotFunctionTypes", ""))),
      NotFunctionObjectTypes(utils::options::parseStringList(
          listOption("NotFunctionObjectTypes", ""))),
      OpaqueHandleTypes(utils::options::parseStringList(
          listOption("OpaqueHandleTypes", ""))) {}

void ConceptualPrefixCheck::storeCheckOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "SmartPointerTypes",
                utils::options::serializeStringList(SmartPointerTypes));
  Options.store(Opts, "SmartPointerHandleTypes",
                utils::options::serializeStringList(SmartPointerHandleTypes));
  Options.store(Opts, "FunctionObjectTypes",
                utils::options::serializeStringList(FunctionObjectTypes));
  Options.store(Opts, "IteratorTypes",
                utils::options::serializeStringList(IteratorTypes));
  Options.store(Opts, "ValueWrapperTypes",
                utils::options::serializeStringList(ValueWrapperTypes));
  Options.store(Opts, "FlagTypes",
                utils::options::serializeStringList(FlagTypes));
  Options.store(Opts, "IgnoredNames",
                utils::options::serializeStringList(IgnoredNames));
  Options.store(Opts, "NotFunctionTypes",
                utils::options::serializeStringList(NotFunctionTypes));
  Options.store(Opts, "NotFunctionObjectTypes",
                utils::options::serializeStringList(NotFunctionObjectTypes));
  Options.store(Opts, "OpaqueHandleTypes",
                utils::options::serializeStringList(OpaqueHandleTypes));
}

void ConceptualPrefixCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      decl(anyOf(varDecl(), fieldDecl(), nonTypeTemplateParmDecl(),
                 bindingDecl()))
          .bind("declaration"),
      this);
}

/// Whether \p Name matches \p Pattern: a qualified name like `std::function`
/// matches the qualified name, an unqualified one the name alone, a leading
/// `*` matches names ending with the rest of the pattern, and one with a
/// trailing `*` too names containing it, like `*Iterator*`.
static bool matchesName(const NamedDecl *Decl, StringRef Pattern) {
  if (!Decl->getIdentifier())
    return false;
  if (Pattern.consume_front("*"))
    return Pattern.consume_back("*")
               ? identifierTextOf(Decl).contains(Pattern)
               : identifierTextOf(Decl).ends_with(Pattern);
  if (Pattern.contains("::"))
    return hasQualifiedName(Decl, Pattern);
  return identifierTextOf(Decl) == Pattern;
}

/// The operators an iterator has, one of which a class that a pattern only
/// matches the name of needs.
static constexpr OverloadedOperatorKind IteratorOperators[] = {
    OO_Star, OO_Arrow, OO_PlusPlus};

/// Whether \p Aliased, what an alias that \p Pattern matches the name of
/// stands for, is an iterator: anything for an exact name, like `iterator`,
/// but for a pattern that the name only contains or ends with, like
/// `*Iterator*`, a pointer or a class that has the operators of an iterator,
/// not a sentinel like `CIteratorEndSentinel`. None for what depends on
/// template parameters, which can be either, and where whether it has them
/// can't be told (hasOverloadedOperator).
static std::optional<bool> modelsIterator(StringRef Pattern, QualType Aliased) {
  if (!Pattern.starts_with("*") || Aliased->isPointerType())
    return true;
  if (Aliased->isDependentType())
    return std::nullopt;
  return hasUsableOperator(Aliased->getAsCXXRecordDecl(),
                               IteratorOperators);
}

static constexpr OverloadedOperatorKind PointerOperators[] = {OO_Arrow,
                                                              OO_Star};

/// Whether \p Aliased, what an alias that \p Pattern matches the name of
/// stands for, is a smart pointer: anything for an exact name, but for a
/// pattern that the name ends with or contains, like `*Pointer`, a pointer
/// or a class that has the operators of a pointer, or that tells whether it
/// points to anything, `using CExceptionPointer = std::exception_ptr`. None
/// for what depends on template parameters, and where whether it has them
/// can't be told (hasOverloadedOperator).
static std::optional<bool> modelsPointer(StringRef Pattern, QualType Aliased) {
  if (!Pattern.starts_with("*") || Aliased->isPointerType())
    return true;
  if (Aliased->isDependentType())
    return std::nullopt;
  const CXXRecordDecl *Record = Aliased->getAsCXXRecordDecl();
  return eitherHolds(hasUsableOperator(Record, PointerOperators),
                     convertsToBool(Record));
}

/// Whether \p Written is a member type that depends on template parameters,
/// `typename t_CTraits::CPointer`, or what is computed from them,
/// `decltype(_pValue)`, `__typeof__`, `tfp_CTypes...[0]` or
/// `__remove_reference_t(...)`, also one a using declaration brings in from a
/// base that depends on them, `using typename TCParent<t_CType>::CPointer`:
/// what its instantiations make it.
static bool dependsAsMember(const clang::Type *Written) {
  const auto *MemberTemplate = dyn_cast<TemplateSpecializationType>(Written);
  return isa<DependentNameType, UnresolvedUsingType>(Written) ||
         (MemberTemplate &&
          MemberTemplate->getTemplateName().getAsDependentTemplateName()) ||
         (isa<DecltypeType, TypeOfExprType, TypeOfType, PackIndexingType,
              UnaryTransformType>(Written) &&
          Written->isDependentType());
}

/// Whether the declared type \p Declaration, an alias or a class, is named as
/// a function type by its `F` or `TF` type prefix: an external one
/// (isExternalDeclaration) has a name of its own library, like Windows'
/// `FILETIME` or macOS' `FSRef`, nor is one NotFunctionTypes lists.
bool ConceptualPrefixCheck::declaredAsFunctionType(
    const NamedDecl *Declaration) const {
  if (!Declaration || !Declaration->getIdentifier() ||
      !hasTypePrefix(identifierTextOf(Declaration), {"F", "TF"}))
    return false;
  const SourceManager &SM = Declaration->getASTContext().getSourceManager();
  if (isExternalDeclaration(Declaration, SM))
    return false;
  return llvm::none_of(NotFunctionTypes, [&](StringRef Pattern) {
    return matchesName(Declaration, Pattern);
  });
}

/// Whether \p Type, as it is written, is named as a function type, with the
/// `F` or `TF` type prefix after its storage prefix: a template parameter,
/// `tf_FFunction`, also as instantiated, a dependent member type or member
/// alias template,
/// `typename t_CTraits::FCallback`, or an alias, `FCallback`, also through
/// what only forwards one, `TCRemoveReference<tf_FAction>` or `typename
/// TCIdentity<tf_FAction>::CType`.
bool ConceptualPrefixCheck::namedAsFunctionType(QualType Type,
                                                int Depth) const {
  if (Depth > 8)
    return false;
  // Each type once at each depth: an alias given the same argument many
  // times, `TCFirst<TCLevel<tf_F>, TCLevel<tf_F>, ...>`, would be asked again
  // through each path to it, a number of times exponential in the depth.
  const auto Key = std::make_pair(Type.getAsOpaquePtr(), Depth);
  if (const auto Found = NamedAsFunction.find(Key);
      Found != NamedAsFunction.end())
    return Found->second;
  const bool Named = namedAsFunctionTypeOnce(Type, Depth);
  NamedAsFunction[Key] = Named;
  return Named;
}

bool ConceptualPrefixCheck::namedAsFunctionTypeOnce(QualType Type,
                                                    int Depth) const {
  if (stackNearlyExhausted())
    return withStack([&] { return namedAsFunctionTypeOnce(Type, Depth); });
  // Of the type arguments of what forwards one, each of them: of several,
  // which it gives can depend on the others, `TCConditional<true, bool,
  // tf_FCallback>`, unless each is, also a reference to one,
  // `TCConditional<t_bReference, t_FCallback const &, t_FCallback>`.
  auto OnlyArgumentNamed = [&](llvm::ArrayRef<TemplateArgument> Arguments) {
    bool Any = false;
    for (const TemplateArgument &Argument : Arguments) {
      if (Argument.getKind() != TemplateArgument::Type)
        continue;
      if (!namedAsFunctionType(Argument.getAsType().getNonReferenceType(),
                               Depth + 1))
        return false;
      Any = true;
    }
    return Any;
  };
  auto Named = [](StringRef Name) {
    StringRef Prefix, Rest;
    if (!splitStoragePrefix(Name, Prefix, Rest))
      Rest = Name;
    return hasTypePrefix(Rest, {"F", "TF"});
  };
  const clang::Type *Written = Type.getTypePtr();
  // Also brought in with `using NCallbacks::FCallback`, or deduced from what
  // has it, `auto Copy = fCallback`.
  for (int Depth = 0; Depth < 8; ++Depth) {
    // Also in parentheses, `FAction (_Action)`.
    if (const auto *Paren = dyn_cast<ParenType>(Written))
      Written = Paren->getInnerType().getTypePtr();
    else if (const auto *Using = dyn_cast<UsingType>(Written))
      Written = Using->desugar().getTypePtr();
    else if (const auto *Deduced = dyn_cast<DeducedType>(Written);
             Deduced && !Deduced->getDeducedType().isNull())
      Written = Deduced->getDeducedType().getTypePtr();
    // Also what `decltype(fAction)` names, and `__typeof__` of a type,
    // `__typeof__(FAction)`, or of an expression.
    else if (const auto *Decltype = dyn_cast<DecltypeType>(Written);
             Decltype && !Decltype->isDependentType())
      Written = Decltype->getUnderlyingType().getTypePtr();
    // Also the type a variable is declared with, `decltype(_fAction)` of
    // `tf_FAction _fAction`.
    else if (const auto *Declared = dyn_cast<DecltypeType>(Written);
             Declared && isa<DeclRefExpr>(Declared->getUnderlyingExpr()) &&
             isa<VarDecl>(
                 cast<DeclRefExpr>(Declared->getUnderlyingExpr())->getDecl()))
      Written = cast<DeclRefExpr>(Declared->getUnderlyingExpr())
                    ->getDecl()
                    ->getType()
                    .getTypePtr();
    else if (const auto *TypeOf = dyn_cast<TypeOfType>(Written))
      Written = TypeOf->getUnmodifiedType().getTypePtr();
    else if (const auto *TypeOfExpr = dyn_cast<TypeOfExprType>(Written);
             TypeOfExpr && !TypeOfExpr->isDependentType())
      Written = TypeOfExpr->desugar().getTypePtr();
    // Also one of a pack, `tfp_FCallbacks...[0]`, named by the pack.
    else if (const auto *Indexed = dyn_cast<PackIndexingType>(Written))
      Written = Indexed->getPattern().getTypePtr();
    // Also annotated, `FCallback [[clang::annotate_type("...")]]`.
    else if (const auto *Attributed = dyn_cast<AttributedType>(Written))
      Written = Attributed->getModifiedType().getTypePtr();
    else
      break;
  }
  if (const auto *Parameter = dyn_cast<TemplateTypeParmType>(Written))
    return Parameter->getIdentifier() &&
           Named(Parameter->getIdentifier()->getName());
  // Also as instantiated, `CAction` given for `tf_FAction`, which a copy in
  // the instantiation, `auto Copy = _fAction`, has.
  // Also what an alias is given for it, `t_FCallback` for `t_CFirst` of
  // `TCConditional<true, t_FCallback, int>`.
  if (const auto *Substituted = dyn_cast<SubstTemplateTypeParmType>(Written))
    return (Substituted->getReplacementType()->isDependentType() &&
            Substituted->getReplacedParameter()->getIdentifier() &&
            Named(identifierTextOf(Substituted->getReplacedParameter()))) ||
           namedAsFunctionType(Substituted->getReplacementType(), Depth + 1);
  if (const auto *Member = dyn_cast<DependentNameType>(Written)) {
    if (Member->getIdentifier() && Named(Member->getIdentifier()->getName()))
      return true;
    const clang::Type *Of = qualifierTypeOf(Member->getQualifier());
    const auto *Forwarder =
        Of ? Of->getAs<TemplateSpecializationType>() : nullptr;
    // The argument the template's member is, `tf_FCallback` of `typename
    // TCMap<int, tf_FCallback>::CValue`, also referred to, `tf_FCallback
    // const &`.
    if (const TemplateArgument *Forwarded = forwardedArgument(Member))
      return Forwarded->getKind() == TemplateArgument::Type &&
             namedAsFunctionType(Forwarded->getAsType()
                                     .getNonReferenceType()
                                     .getUnqualifiedType(),
                                 Depth + 1);
    // Not of one a partial specialization defines, which can give a part of
    // what it is given, `CReturn` of `TCSignature<t_CReturn (tp_CParams...)>`.
    const auto *Template =
        Forwarder ? dyn_cast_or_null<ClassTemplateDecl>(
                        Forwarder->getTemplateName().getAsTemplateDecl())
                  : nullptr;
    llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 2> Partials;
    if (Template)
      const_cast<ClassTemplateDecl *>(Template)->getPartialSpecializations(
          Partials);
    return Forwarder && Partials.empty() &&
           isTypeOfGivenParameter(Member, /*Nested=*/false) &&
           OnlyArgumentNamed(Forwarder->template_arguments());
  }
  if (const auto *Typedef = dyn_cast<TypedefType>(Written))
    return declaredAsFunctionType(Typedef->getDecl());
  // Also brought in from what depends on the parameters, `using typename
  // TCParent<t_CType>::FCallback;`.
  if (const auto *Unresolved = dyn_cast<UnresolvedUsingType>(Written))
    return Unresolved->getDecl()->getIdentifier() &&
           Named(identifierTextOf(Unresolved->getDecl()));
  if (const auto *Specialization = dyn_cast<TemplateSpecializationType>(Written)) {
    const TemplateName Template = Specialization->getTemplateName();
    if (const DependentTemplateName *Dependent =
            Template.getAsDependentTemplateName())
      return Dependent->getName().getIdentifier() &&
             Named(Dependent->getName().getIdentifier()->getName());
    // Also what it is an alias of, `typename TCConditionalHelper<true,
    // t_FCallback, int>::CType` of `TCConditional<true, t_FCallback, int>`,
    // which can give one of its arguments only.
    if (Specialization->isTypeAlias())
      if (const auto *Alias = dyn_cast_or_null<TypeAliasTemplateDecl>(
              Template.getAsTemplateDecl());
          Alias && Alias->getIdentifier())
        return declaredAsFunctionType(Alias) ||
               namedAsFunctionType(Specialization->getAliasedType(),
                                   Depth + 1) ||
               (givesParameter(Alias->getTemplatedDecl()->getUnderlyingType(),
                               /*Nested=*/false) &&
                OnlyArgumentNamed(Specialization->template_arguments()));
    // Also a template template parameter, `tf_TFCallback<int>`.
    if (const auto *Parameter = dyn_cast_or_null<TemplateTemplateParmDecl>(
            Template.getAsTemplateDecl());
        Parameter && Parameter->getIdentifier())
      return Named(identifierTextOf(Parameter));
  }
  return false;
}

/// Whether the types \p Written and \p Other, one of them or both with
/// template parameters in them, can be the same: not a pointer and something
/// else, `tf_CType *` and `int`, nor two types without parameters that
/// differ. One that can't be told can, like a member of what depends on
/// them, `typename tf_CTraits::CValue`.
static bool mayBeSameType(QualType Written, QualType Other) {
  for (int Depth = 0; Depth < 16; ++Depth) {
    Written = Written.getCanonicalType();
    Other = Other.getCanonicalType();
    // Also what depends on them that can be any type, `typename
    // tf_CTraits::CValue` or `decltype(...)`.
    auto AnyType = [](QualType Type) {
      if (const auto *Specialization =
              dyn_cast<TemplateSpecializationType>(Type.getTypePtr());
          Specialization &&
          Specialization->getTemplateName().getAsDependentTemplateName())
        return true;
      return isa<TemplateTypeParmType, DependentNameType, DecltypeType,
                 TypeOfExprType, PackIndexingType, UnaryTransformType>(
          Type.getTypePtr());
    };
    if (AnyType(Written) || AnyType(Other))
      return true;
    if (!Written->isDependentType() && !Other->isDependentType())
      return Written.getUnqualifiedType() == Other.getUnqualifiedType();
    if (Written->isPointerType() != Other->isPointerType() ||
        Written->isLValueReferenceType() != Other->isLValueReferenceType() ||
        Written->isRValueReferenceType() != Other->isRValueReferenceType())
      return false;
    if (Written->isPointerType()) {
      Written = Written->getPointeeType();
      Other = Other->getPointeeType();
      continue;
    }
    if (Written->isReferenceType()) {
      Written = Written.getNonReferenceType();
      Other = Other.getNonReferenceType();
      continue;
    }
    return true;
  }
  return true;
}

/// The template type parameter \p Type is written as, through references,
/// `const` and a pack, like `tf_CType` of `tf_CType const &`, or null.
static const TemplateTypeParmType *writtenTemplateParameter(QualType Type) {
  Type = Type.getNonReferenceType();
  if (const auto *Pack = Type->getAs<PackExpansionType>())
    Type = Pack->getPattern().getNonReferenceType();
  // Also in parentheses, `tf_FAction (_fAction)`, and annotated,
  // `tf_FAction [[clang::annotate_type("...")]] &&_fAction`.
  for (int Depth = 0; Depth < 16; ++Depth)
    if (const auto *Paren = dyn_cast<ParenType>(Type.getTypePtr()))
      Type = Paren->getInnerType().getNonReferenceType();
    else if (const auto *Attributed =
                 dyn_cast<AttributedType>(Type.getTypePtr()))
      Type = Attributed->getModifiedType().getNonReferenceType();
    else
      break;
  return dyn_cast<TemplateTypeParmType>(Type.getTypePtr());
}

/// Whether what \p Record is made of can't be told from what clang
/// instantiated: it, or a class it derives from, isn't defined in the
/// translation unit, also a specialization nothing completes or a template
/// only declared, or has a base that depends on the parameters of its
/// template.
static bool hasUnknownBases(const CXXRecordDecl *Record) {
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Visited;
  llvm::SmallVector<const CXXRecordDecl *, 8> Worklist{Record};
  while (!Worklist.empty()) {
    const CXXRecordDecl *Current = Worklist.pop_back_val();
    if (!Current)
      return true;
    const CXXRecordDecl *Definition = Current->getDefinition();
    // Nor of a template's own declaration that isn't defined, a forward
    // declared one, `template <typename t_CType> struct TCActorFunctor;`,
    // unless it has partial specializations, which are what anything made
    // of it is made of, and are looked at themselves.
    if (!Definition) {
      llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 4> Partials;
      if (ClassTemplateDecl *Template = Current->getDescribedClassTemplate())
        Template->getPartialSpecializations(Partials);
      if (!Partials.empty())
        continue;
      return true;
    }
    if (!Visited.insert(Definition).second)
      continue;
    for (const CXXBaseSpecifier &Base : basesOf(Definition)) {
      if (Base.getType()->isDependentType())
        return true;
      Worklist.push_back(Base.getType()->getAsCXXRecordDecl());
    }
  }
  return false;
}

std::optional<bool> ConceptualPrefixCheck::isFunctionObject(
    const CXXRecordDecl *Record) const {
  // Not one NotFunctionObjectTypes lists, also through a base, whose call
  // operator only binds and gives a builder, like `g_ActorFunctor`.
  if (!NotFunctionObjectTypes.empty() &&
      knownTrue(isRecordInList(Record, NotFunctionObjectTypes)))
    return false;
  // One named by a pattern whose call operator can't be told isn't known.
  const std::optional<bool> Listed =
      isRecordInList(Record, FunctionObjectTypes, {OO_Call});
  if (!possiblyTrue(Listed))
    return false;
  if (!Listed)
    return std::nullopt;
  // One whose bases can't be told, with one that depends on the parameters,
  // `struct TCAdapterFunctor : t_CBase`, which an instantiation can make one
  // of those, or not defined in the translation unit, a specialization
  // nothing completes, `TCAdapterFunctor<CBuilder>`, isn't known.
  if (!NotFunctionObjectTypes.empty() && hasUnknownBases(Record))
    return std::nullopt;
  return true;
}

bool ConceptualPrefixCheck::isOpaqueHandle(QualType Type,
                                           const ASTContext &Context) const {
  if (OpaqueHandleTypes.empty() || Type.isNull())
    return false;
  // What each level of sugar is is kept (OpaqueHandles), so the types along
  // one chain of aliases go through each alias once.
  llvm::SmallVector<void *, 4> Levels;
  bool Opaque = false;
  for (QualType Level = Type;;) {
    if (const auto Found = OpaqueHandles.find(Level.getAsOpaquePtr());
        Found != OpaqueHandles.end()) {
      Opaque = Found->second;
      break;
    }
    Levels.push_back(Level.getAsOpaquePtr());
    if (const auto *Typedef = dyn_cast<TypedefType>(Level.getTypePtr());
        Typedef &&
        !matchingPatterns(Typedef->getDecl(), OpaqueHandleTypes).empty()) {
      Opaque = true;
      break;
    }
    const QualType Next = Level.getSingleStepDesugaredType(Context);
    if (Next == Level)
      break;
    Level = Next;
  }
  for (void *Level : Levels)
    OpaqueHandles[Level] = Opaque;
  return Opaque;
}

bool ConceptualPrefixCheck::isValueWrapper(const NamedDecl *Template) const {
  return Template && llvm::any_of(ValueWrapperTypes, [&](StringRef Pattern) {
           return matchesName(Template, Pattern);
         });
}

std::optional<bool> ConceptualPrefixCheck::isRecordInList(
    const CXXRecordDecl *Record, llvm::ArrayRef<StringRef> List,
    llvm::ArrayRef<OverloadedOperatorKind> Operators,
    bool OrConvertsToBool) const {
  // A function object is what its name says, or for a name that only
  // contains what is listed, like `*Functor*`, what declares a call
  // operator or a conversion to a function, whatever they are.
  const bool Callable =
      Operators.size() == 1 && Operators.front() == OO_Call;
  // The name a class is named by in a list: of a specialization, its
  // template's.
  auto NameOf = [](const CXXRecordDecl *Class) -> const NamedDecl * {
    if (const ClassTemplateDecl *Template = Class->getDescribedClassTemplate())
      return Template;
    if (const auto *Specialization =
            dyn_cast<ClassTemplateSpecializationDecl>(Class))
      return Specialization->getSpecializedTemplate();
    return Class;
  };
  // The class of a name a list names exactly, `TCPointer`, is what the name
  // says, whatever it declares.
  if (Record->getIdentifier() && llvm::any_of(List, [&](StringRef Pattern) {
        return !Pattern.starts_with("*") && matchesName(NameOf(Record), Pattern);
      }))
    return true;
  // What the concept needs, \p Operators and, with \p OrConvertsToBool, a
  // conversion to `bool`, as code can use it (convertsToBool).
  auto Does = [&] {
    return eitherHolds(hasUsableOperator(Record, Operators),
                       OrConvertsToBool ? convertsToBool(Record)
                                        : std::optional<bool>(false));
  };
  // One that declares what the concept does, an operator or the conversion,
  // decides by what it declares, which hides that of a listed base: not one
  // where that can't be used, `operator++() = delete;` or `requires false`,
  // and unknown where that can't be told, `requires t_bEnabled` that nothing
  // uses, which clang hasn't checked.
  if (const CXXRecordDecl *Definition = Record->getDefinition();
      Definition && !Operators.empty() && !Callable &&
      llvm::any_of(Definition->decls(), [&](const Decl *Member) {
        const auto *Named = dyn_cast<NamedDecl>(Member);
        if (!Named)
          return false;
        const DeclarationName Name = Named->getDeclName();
        return (Name.getNameKind() == DeclarationName::CXXOperatorName &&
                llvm::is_contained(Operators,
                                   Name.getCXXOverloadedOperator())) ||
               (OrConvertsToBool &&
                Name.getNameKind() ==
                    DeclarationName::CXXConversionFunctionName &&
                Name.getCXXNameType()->isBooleanType());
      })) {
    const std::optional<bool> Usable = Does();
    if (!Usable)
      return std::nullopt;
    if (!*Usable)
      return false;
  }
  // A name that only contains what is listed, like `CIteratorEndSentinel` for
  // `*Iterator*`, also needs what the concept does: one of \p Operators,
  // which can be unknown.
  std::optional<bool> Models = true;
  bool ModelsTold = Operators.empty();
  auto Matches = [&](const NamedDecl *Named,
                     StringRef Pattern) -> std::optional<bool> {
    if (!matchesName(Named, Pattern))
      return false;
    if (!Pattern.starts_with("*"))
      return true;
    if (!ModelsTold) {
      Models = Callable ? declaresCallable(Record) : Does();
      ModelsTold = true;
    }
    return Models;
  };
  // The class and its bases, each once, as they are defined in the
  // translation unit: a specialization that isn't instantiated only by the
  // name of its template, and a base that depends on parameters of a
  // template, which can be anything, not at all.
  // One that a name matches where what it does can't be told isn't known,
  // unless another name tells.
  bool Unknown = false;
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Visited;
  llvm::SmallVector<const CXXRecordDecl *, 8> Worklist{Record};
  while (!Worklist.empty()) {
    const CXXRecordDecl *Current = Worklist.pop_back_val();
    if (!Current || !Visited.insert(Current->getCanonicalDecl()).second)
      continue;
    const NamedDecl *Named = NameOf(Current);
    // A class without a name, `struct : TCFunction<void()> {} g_fCallback;`,
    // is only one of its bases.
    if (Current->getIdentifier())
      for (const StringRef Pattern : List) {
        const std::optional<bool> Match = Matches(Named, Pattern);
        if (knownTrue(Match))
          return true;
        Unknown |= !Match;
      }
    const CXXRecordDecl *Definition = Current->getDefinition();
    if (!Definition)
      continue;
    for (const CXXBaseSpecifier &Base : basesOf(Definition))
      if (!Base.getType()->isDependentType())
        Worklist.push_back(Base.getType()->getAsCXXRecordDecl());
  }
  if (Unknown)
    return std::nullopt;
  return false;
}

ConceptualPrefixCheck::CConcepts
ConceptualPrefixCheck::expectedConcept(QualType Type,
                                       const ASTContext &Context) const {
  // Each type once: the declarations of one type, also through a long chain
  // of aliases, would each go through the chain again.
  if (const auto Found = ExpectedConcepts.find(Type.getAsOpaquePtr());
      Found != ExpectedConcepts.end())
    return Found->second;
  CConcepts Concepts = expectedConceptOnce(Type, Context);
  ExpectedConcepts[Type.getAsOpaquePtr()] = Concepts;
  return Concepts;
}

const TemplateSpecializationType *
ConceptualPrefixCheck::writtenSpecialization(const clang::Type *Type) const {
  // As getAs: the first level of sugar that is one. What each level leads to
  // is kept, so the types along one chain of aliases go through each alias
  // once.
  llvm::SmallVector<const clang::Type *, 4> Levels;
  const TemplateSpecializationType *Specialization = nullptr;
  for (const clang::Type *Level = Type;;) {
    if (const auto Found = WrittenSpecializations.find(Level);
        Found != WrittenSpecializations.end()) {
      Specialization = Found->second;
      break;
    }
    Levels.push_back(Level);
    if ((Specialization = dyn_cast<TemplateSpecializationType>(Level)))
      break;
    const clang::Type *Next =
        Level->getLocallyUnqualifiedSingleStepDesugaredType().getTypePtr();
    if (Next == Level)
      break;
    Level = Next;
  }
  for (const clang::Type *Level : Levels)
    WrittenSpecializations[Level] = Specialization;
  return Specialization;
}

llvm::SmallVector<unsigned, 1>
ConceptualPrefixCheck::matchingPatterns(const NamedDecl *Named,
                                        llvm::ArrayRef<StringRef> List) const {
  const auto Key =
      std::make_pair(Named, static_cast<const void *>(List.data()));
  if (const auto Found = MatchingPatterns.find(Key);
      Found != MatchingPatterns.end())
    return Found->second;
  llvm::SmallVector<unsigned, 1> Matching;
  for (unsigned Index = 0; Index < List.size(); ++Index)
    if (matchesName(Named, List[Index]))
      Matching.push_back(Index);
  MatchingPatterns[Key] = Matching;
  return Matching;
}

ConceptualPrefixCheck::CConcepts
ConceptualPrefixCheck::expectedConceptOnce(QualType Type,
                                           const ASTContext &Context) const {
  CConcepts Concepts;
  // A classification that ranks before the others is unknown, an alias named
  // as an iterator or a pointer whose operators can't be told: no concept is
  // required, whatever the rest tells.
  bool Undecided = false;
  const std::optional<char> Required =
      conceptOf(Type, Context, Concepts, Undecided);
  if (!Undecided)
    Concepts.Required = Required;
  return Concepts;
}

std::optional<char>
ConceptualPrefixCheck::conceptOf(QualType Type, const ASTContext &Context,
                                 CConcepts &Concepts, bool &Undecided) const {
  // A concept it can be where that can't be told (CConcepts::MayBe).
  auto Allow = [&](char Concept) {
    if (!Concepts.mayBe(Concept))
      Concepts.MayBe.push_back(Concept);
  };
  auto Opaque = [&] {
    Concepts.Opaque = true;
    return std::nullopt;
  };
  // A reference to an alias of a reference, `CSubscription &` of `using
  // CSubscription = TCSharedPointer<int> &`, collapses to the alias.
  for (int Depth = 0; !Type.isNull() && Depth < 8; ++Depth) {
    const auto *Reference = dyn_cast<ReferenceType>(Type.getTypePtr());
    if (!Reference || !Reference->getPointeeTypeAsWritten()->isReferenceType())
      break;
    Type = Reference->getPointeeTypeAsWritten();
  }
  // An alias of a reference named as a function type, `using
  // FCallbackReference = CAction &`, is one too.
  if (!Type.isNull() && Type->isReferenceType() && namedAsFunctionType(Type))
    return 'f';
  // A smart pointer is a pointer, also through an alias, `using CNodePtr =
  // TCSharedPointer<CNode>`, but not through one listed as a handle, named for
  // what it gives, like `CActorSubscription`, of SmartPointerHandleTypes.
  auto IsHandle = [&](const clang::Type *Alias) {
    const NamedDecl *Named = nullptr;
    if (const auto *Typedef = dyn_cast<TypedefType>(Alias))
      Named = Typedef->getDecl();
    else if (const auto *Specialization =
                 dyn_cast<TemplateSpecializationType>(Alias);
             Specialization && Specialization->isTypeAlias())
      Named = Specialization->getTemplateName().getAsTemplateDecl();
    return Named && !matchingPatterns(Named, SmartPointerHandleTypes).empty();
  };
  // A class derived from a handle is a handle too, like
  // `TCActorSubscriptionWithID`, which only adds an ID to the
  // `CActorSubscription` it derives from. Each base once, at the least depth
  // it is reached at, as one that several paths lead to, of a diamond, would
  // otherwise be gone through for each path: what it is then is kept, for
  // those paths and for the other records that are asked about, which can
  // share it, the primary template and a partial specialization.
  // None when it can be one, which isn't known: a class with a base that
  // depends on the parameters, `struct TCAdapter : t_CBase`, which its
  // instantiations can make a handle, and one whose bases go deeper than they
  // are followed. So the generic form can be named either way, and the
  // instantiations only exclude what is wrong in them. Without
  // SmartPointerHandleTypes nothing is one, and the bases aren't gone through.
  llvm::DenseMap<const CXXRecordDecl *, std::pair<int, std::optional<bool>>>
      ExploredAt;
  TCRecursive<std::optional<bool>(const CXXRecordDecl *, int)> DerivesFromHandle;
  TCRecursive<std::optional<bool>(const CXXRecordDecl *, int)> Explore =
      [&](const CXXRecordDecl *Derived, int Depth) -> std::optional<bool> {
    bool Unknown = false;
    // Whether \p Base of an instantiation is one its pattern has of the
    // parameters, `t_CBase`.
    const CXXRecordDecl *Pattern = Derived->getTemplateInstantiationPattern();
    // Written as what a parameter is: the parameter, `t_CBase`, or a member of
    // one, `typename t_CTraits::CBase`, not a template given them,
    // `TCUniquePointer<t_CType>`, which is what it names.
    auto GivenWhole = [](QualType Type) {
      Type = Type.getCanonicalType();
      if (const auto *Expansion = Type->getAs<PackExpansionType>())
        Type = Expansion->getPattern().getCanonicalType();
      return isa<TemplateTypeParmType, DependentNameType>(Type.getTypePtr());
    };
    // The base of the pattern \p Base is of: of one with a pack of them,
    // `tp_CBases...`, those before the pack and after it one to one, and
    // those between of the pack, `struct TCAdapter : TCPointer<int>,
    // tp_CBases...`. Of one with several packs, which aren't told apart, any
    // can be when any is written so.
    auto FromParameter = [&](const CXXBaseSpecifier &Base) {
      if (!Pattern)
        return false;
      const size_t Index = &Base - basesOf(Derived).begin();
      const size_t Count = basesOf(Derived).size();
      const size_t PatternCount = basesOf(Pattern).size();
      const CXXBaseSpecifier *Pack = nullptr;
      for (const CXXBaseSpecifier &Other : basesOf(Pattern)) {
        if (!Other.isPackExpansion())
          continue;
        if (Pack)
          return llvm::any_of(basesOf(Pattern),
                              [&](const CXXBaseSpecifier &Any) {
                                return GivenWhole(Any.getType());
                              });
        Pack = &Other;
      }
      if (!Pack)
        return Index < PatternCount &&
               GivenWhole(basesOf(Pattern).begin()[Index].getType());
      const size_t Before = Pack - basesOf(Pattern).begin();
      const size_t After = PatternCount - Before - 1;
      const CXXBaseSpecifier *Of =
          Index < Before ? basesOf(Pattern).begin() + Index
          : Index + After >= Count
              ? basesOf(Pattern).begin() + (PatternCount - (Count - Index))
              : Pack;
      return GivenWhole(Of->getType());
    };
    for (const CXXBaseSpecifier &Base : basesOf(Derived)) {
      for (QualType Level = Base.getType();;) {
        if (IsHandle(Level.getTypePtr()))
          return true;
        const QualType Next = Level.getSingleStepDesugaredType(Context);
        if (Next == Level)
          break;
        Level = Next;
      }
      // A base an instantiation is given, `TCAdapter<CActorSubscription>` of
      // `struct TCAdapter : t_CBase`, which clang keeps without the alias, and
      // that the same specialization has whether the alias or what it is an
      // alias of was given, `TCAdapter<TCUniquePointer<...>>`: when it is
      // what a handle is an alias of, which it is isn't known.
      if (FromParameter(Base) &&
          !handleAliasOf(Base.getType(), Context).isNull()) {
        Unknown = true;
        continue;
      }
      const CXXRecordDecl *BaseRecord = Base.getType()->getAsCXXRecordDecl();
      if (Base.getType()->isDependentType() &&
          (!BaseRecord || !BaseRecord->hasDefinition())) {
        Unknown = true;
        continue;
      }
      const std::optional<bool> Inherited =
          DerivesFromHandle(BaseRecord, Depth + 1);
      if (knownTrue(Inherited))
        return true;
      Unknown |= !Inherited;
    }
    if (Unknown)
      return std::nullopt;
    return false;
  };
  DerivesFromHandle = [&](const CXXRecordDecl *Derived,
                          int Depth) -> std::optional<bool> {
    if (!Derived || SmartPointerHandleTypes.empty())
      return false;
    // One not defined in the translation unit, also a specialization nothing
    // completes, whose bases can't be told, isn't known.
    Derived = Derived->getDefinition();
    if (!Derived)
      return std::nullopt;
    if (Depth > 8)
      return std::nullopt;
    // Reached deeper than before, which follows no more of its bases.
    if (const auto It = ExploredAt.find(Derived);
        It != ExploredAt.end() && It->second.first <= Depth)
      return It->second.second;
    const std::optional<bool> Handle = Explore(Derived, Depth);
    ExploredAt[Derived] = {Depth, Handle};
    return Handle;
  };
  // A smart pointer that isn't known to be a handle or not is named either
  // way: no concept, and `p` allowed.
  bool HandleUnknown = false;
  auto PointerUnlessHandle =
      [&](bool ThroughHandle,
          llvm::ArrayRef<const CXXRecordDecl *> Records) -> std::optional<char> {
    if (ThroughHandle)
      return std::nullopt;
    // A handle when what it can be made of all are, a pointer when none
    // are; when one isn't known, or they disagree, unknown.
    bool Handles = false;
    bool NotHandles = false;
    bool Unknown = false;
    for (const CXXRecordDecl *Record : Records) {
      const std::optional<bool> Handle = DerivesFromHandle(Record, 0);
      Unknown |= !Handle;
      Handles |= knownTrue(Handle);
      NotHandles |= !possiblyTrue(Handle);
    }
    if (!Unknown && !NotHandles)
      return std::nullopt;
    if (!Unknown && !Handles)
      return 'p';
    HandleUnknown = true;
    Allow('p');
    return std::nullopt;
  };
  // An alias of a reference, `using CSubscription = TCSharedPointer<int> &`,
  // names what it refers to like an alias of it.
  bool ReferenceThroughHandle = false;
  if (!Type.isNull() && Type->isReferenceType())
    for (QualType Level = Type; !isa<ReferenceType>(Level.getTypePtr());) {
      if (IsHandle(Level.getTypePtr())) {
        ReferenceThroughHandle = true;
        break;
      }
      const QualType Next = Level.getSingleStepDesugaredType(Context);
      if (Next == Level)
        break;
      Level = Next;
    }
  Type = Type.getNonReferenceType();
  // A pack is named for what each of its elements is, like `p_pValues` of
  // `tfp_CTypes *...`.
  if (!Type.isNull())
    if (const auto *Pack = Type->getAs<PackExpansionType>())
      Type = Pack->getPattern().getNonReferenceType();
  if (Type.isNull() || Type->isUndeducedAutoType())
    return std::nullopt;
  // An opaque handle of a system, which is a pointer on some targets and an
  // integer, an array or a structure on others, like `pthread_t` and
  // `va_list`, takes no concept prefix, as none would fit every target: one
  // OpaqueHandleTypes lists, also through an alias of it, `CMibArgList`.
  if (isOpaqueHandle(Type, Context))
    return Opaque();
  // A type named as a function type, a template parameter, `tf_FFunction`, a
  // dependent member type or alias template, `typename t_CTraits::FCallback`,
  // or an alias, `FCallback`, is a function object, as its name says.
  if (namedAsFunctionType(Type))
    return 'f';
  // Also through an alias of one, `using CCallback = tf_FCallback`, also an
  // alias template, `TCIdentity<tf_FCallback>`.
  {
    QualType Aliased = Type;
    for (int Depth = 0; Depth < 16; ++Depth) {
      const QualType Next = Aliased.getSingleStepDesugaredType(Context);
      if (Next == Aliased)
        break;
      Aliased = Next;
      if (namedAsFunctionType(Aliased))
        return 'f';
    }
  }
  // A pointer to what depends on a template parameter, like `t_CType *`, is
  // a pointer whatever it points to, unless it points to a function, like
  // `void (*)(tf_CType)`. A parameter named as a function type, like
  // `tf_FFunction *`, can be a function, which makes it a function object, or
  // a function object, which makes it a pointer, so either is fine.
  if (Type->isDependentType()) {
    // A lambda declared in a template, whatever its arguments.
    if (const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
        Record && Record->isLambda())
      return 'f';
    // A function, also referred to, or a pointer to a member function, like
    // `void (&_fCallback)(tf_CValue)`, and a pointer to a data member, like
    // `int t_COwner::*m_pMember`.
    // Also a block, `void (^_Callback)(tf_CValue)`.
    if (Type->isFunctionType() || Type->isMemberFunctionPointerType() ||
        Type->isBlockPointerType())
      return 'f';
    if (Type->isMemberDataPointerType())
      return 'p';
    // A specialization of a listed template written as it is, whatever its
    // arguments, like `TCUniquePointer<tf_CType>` or
    // `TCFunction<tf_FSignature>`, also through an alias template of one,
    // `TCFunctionSmallMovable< tf_FSignature>`, but not of a smart pointer
    // through an alias listed as a handle, like for a type that doesn't
    // depend on parameters. Also through an alias that depends on them,
    // `using CCallback = TCFunction<void (t_CType _Value)>`.
    const clang::Type *Written = Type.getTypePtr();
    bool ThroughHandle = ReferenceThroughHandle;
    for (int Depth = 0; Depth < 16; ++Depth) {
      if (const auto *Typedef = dyn_cast<TypedefType>(Written)) {
        ThroughHandle |= IsHandle(Typedef);
        Written = Typedef->desugar().getTypePtr();
        continue;
      }
      // Also in parentheses, `TCSharedPointer<tf_CType> (_pValue)`, or
      // annotated, `[[clang::annotate_type("...")]]`.
      if (const auto *Paren = dyn_cast<ParenType>(Written)) {
        Written = Paren->getInnerType().getTypePtr();
        continue;
      }
      if (const auto *Attributed = dyn_cast<AttributedType>(Written)) {
        Written = Attributed->getModifiedType().getTypePtr();
        continue;
      }
      const auto *Alias = dyn_cast<TemplateSpecializationType>(Written);
      if (!Alias || !Alias->isTypeAlias())
        break;
      ThroughHandle |= IsHandle(Alias);
      Written = Alias->getAliasedType().getTypePtr();
    }
    // Also the name of a class template in itself, `TCVectorIterator` in its
    // body, which is its specialization of its own parameters.
    if (const auto *Injected = dyn_cast<InjectedClassNameType>(Written))
      if (const QualType Specialized =
              Injected->getDecl()->getCanonicalTemplateSpecializationType(
                  Context);
          !Specialized.isNull())
        Written = Specialized.getTypePtr();
    if (const auto *Specialization =
            dyn_cast<TemplateSpecializationType>(Written);
        Specialization && !Specialization->isTypeAlias())
      if (const auto *Template = dyn_cast_or_null<ClassTemplateDecl>(
              Specialization->getTemplateName().getAsTemplateDecl())) {
        // As every specialization it can be is, the partial ones and those
        // declared explicitly too, like `TCDispatch<t_CType *>`, which can be
        // of another kind than the template.
        // Not one whose arguments can't be what is written, `TCWrapper<int>`
        // of `TCWrapper<tf_CType *>`.
        llvm::SmallVector<const CXXRecordDecl *, 4> Records{
            Template->getTemplatedDecl()};
        auto MayBe = [&](llvm::ArrayRef<TemplateArgument> Arguments) {
          const llvm::ArrayRef<TemplateArgument> Written =
              Specialization->template_arguments();
          if (Written.size() != Arguments.size())
            return true;
          // Also a value, `TCIterator<tf_CType, false>`, which a
          // specialization for another value, `<t_CType, true>`, can't be.
          auto ValueOf = [&](const TemplateArgument &Argument)
              -> std::optional<llvm::APSInt> {
            return templateArgumentValueOf(Argument, Context);
          };
          for (size_t iArgument = 0; iArgument < Written.size(); ++iArgument) {
            if (Written[iArgument].getKind() == TemplateArgument::Type &&
                Arguments[iArgument].getKind() == TemplateArgument::Type &&
                !mayBeSameType(Written[iArgument].getAsType(),
                               Arguments[iArgument].getAsType()))
              return false;
            const std::optional<llvm::APSInt> WrittenValue =
                ValueOf(Written[iArgument]);
            const std::optional<llvm::APSInt> Value =
                ValueOf(Arguments[iArgument]);
            if (WrittenValue && Value &&
                !llvm::APSInt::isSameValue(*WrittenValue, *Value))
              return false;
          }
          return true;
        };
        llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 4> Partials;
        const_cast<ClassTemplateDecl *>(Template)->getPartialSpecializations(
            Partials);
        for (const ClassTemplatePartialSpecializationDecl *Partial : Partials)
          if (MayBe(Partial->getTemplateArgs().asArray()))
            Records.push_back(Partial);
        for (const ClassTemplateSpecializationDecl *Declared :
             Template->specializations())
          if (Declared->isExplicitSpecialization() &&
              MayBe(Declared->getTemplateArgs().asArray()))
            Records.push_back(Declared);
        auto All = [&](auto &&Is) { return llvm::all_of(Records, Is); };
        // Whether each of them is in \p List, and whether each can be.
        auto AllListed = [&](llvm::ArrayRef<StringRef> List,
                             llvm::ArrayRef<OverloadedOperatorKind> Operators,
                             bool OrConvertsToBool, bool Known) {
          return All([&](const CXXRecordDecl *Record) {
            const std::optional<bool> Listed =
                isRecordInList(Record, List, Operators, OrConvertsToBool);
            return Known ? knownTrue(Listed) : possiblyTrue(Listed);
          });
        };
        if (All([&](const CXXRecordDecl *Record) {
              return knownTrue(isFunctionObject(Record));
            }))
          return 'f';
        // One whose being a function object isn't known can be named as one,
        // or as anything else it can be, or neither: also when what it can be
        // made of disagree, a primary template that is one and a partial
        // specialization that a base of NotFunctionObjectTypes excludes.
        if (llvm::any_of(Records,
                         [&](const CXXRecordDecl *Record) {
                           return !isFunctionObject(Record);
                         }) ||
            llvm::any_of(Records, [&](const CXXRecordDecl *Record) {
              return knownTrue(isFunctionObject(Record));
            })) {
          Allow('f');
          for (const CXXRecordDecl *Record : Records) {
            if (possiblyTrue(
                    isRecordInList(Record, IteratorTypes, IteratorOperators)))
              Allow('i');
            if (possiblyTrue(isRecordInList(Record, SmartPointerTypes,
                                            PointerOperators,
                                            /*OrConvertsToBool=*/true)))
              Allow('p');
          }
          return std::nullopt;
        }
        // A smart pointer, unless a handle, before an iterator, as of what
        // doesn't depend on parameters: one that isn't known to be a handle,
        // which would be named as the iterator it is, or not, a pointer, can
        // be named either, or neither; so can one whose being a smart pointer
        // or an iterator can't be told.
        if (AllListed(SmartPointerTypes, PointerOperators,
                      /*OrConvertsToBool=*/true, /*Known=*/true)) {
          if (const std::optional<char> Pointer =
                  PointerUnlessHandle(ThroughHandle, Records))
            return Pointer;
          if (HandleUnknown) {
            if (AllListed(IteratorTypes, IteratorOperators,
                          /*OrConvertsToBool=*/false, /*Known=*/false))
              Allow('i');
            return std::nullopt;
          }
        } else if (!ThroughHandle &&
                   AllListed(SmartPointerTypes, PointerOperators,
                             /*OrConvertsToBool=*/true, /*Known=*/false)) {
          Allow('p');
          if (AllListed(IteratorTypes, IteratorOperators,
                        /*OrConvertsToBool=*/false, /*Known=*/false))
            Allow('i');
          return std::nullopt;
        }
        if (AllListed(IteratorTypes, IteratorOperators,
                      /*OrConvertsToBool=*/false, /*Known=*/true))
          return 'i';
        if (AllListed(IteratorTypes, IteratorOperators,
                      /*OrConvertsToBool=*/false, /*Known=*/false)) {
          Allow('i');
          return std::nullopt;
        }
        // An atomic pointer or function, `TCAtomic<t_CType *>`, also one
        // that clears itself.
        if (isValueWrapper(Template) &&
            !Specialization->template_arguments().empty() &&
            Specialization->template_arguments()[0].getKind() ==
                TemplateArgument::Type) {
          const QualType Held =
              Specialization->template_arguments()[0].getAsType();
          // Of an opaque handle, `TCAtomic<pthread_t>`, which is one too, also
          // one that is a boolean on this target.
          if (isOpaqueHandle(Held, Context))
            return Opaque();
          // A boolean that clears itself, also given what depends on them,
          // `TCAutoClearInt<bool, t_bInitial>`.
          if (Held->isBooleanType())
            return 'b';
          if (Held->isFunctionPointerType() ||
              Held->isMemberFunctionPointerType())
            return 'f';
          // Of one named as a function type, `TCAtomic<t_FCallback *>`, which
          // can be a pointer to a function or to a function object: either
          // concept, like the pointer itself.
          if (Held->isPointerType() &&
              namedAsFunctionType(Held->getPointeeType()))
            return 'x';
          // A written pointer is a pointer whatever an argument makes what it
          // points to, also to a member of what depends on the parameters,
          // `TCAtomic<typename t_CTraits::CType *>`.
          if (Held->isPointerType() || Held->isMemberDataPointerType())
            return 'p';
        }
        if (Records.size() > 1)
          return std::nullopt;
      }
    if (!Type->isPointerType())
      return std::nullopt;
    const QualType Pointee = Type->getPointeeType();
    if (Pointee->isFunctionType())
      return 'f';
    // A pointer to a pointer, also one an alias names whatever it is named,
    // `FCallback<tf_CType> *` of `using FCallback = void (*)(t_CType)`.
    if (Pointee.getCanonicalType()->isAnyPointerType())
      return 'p';
    // Also one of a pack, `tfp_FCallbacks...[0]`, named by the pack.
    QualType Named = Pointee;
    if (const auto *Indexed = Pointee->getAs<PackIndexingType>())
      Named = Indexed->getPattern();
    if (const auto *Parameter = Named->getAs<TemplateTypeParmType>();
        Parameter && Parameter->getIdentifier()) {
      StringRef Prefix, Rest;
      if (splitStoragePrefix(Parameter->getIdentifier()->getName(), Prefix,
                             Rest) &&
          hasTypePrefix(Rest, {"F", "TF"}))
        return 'x';
    }
    // Also a dependent member type named as a function type, like
    // `typename tf_CCallbacks::FCallback *`.
    if (const auto *Member = Pointee->getAs<DependentNameType>();
        Member && Member->getIdentifier() &&
        hasTypePrefix(Member->getIdentifier()->getName(), {"F", "TF"}))
      return 'x';
    // Also one brought in by a using declaration, `using typename
    // TCParent<t_CType>::FCallback;`.
    if (const auto *Unresolved = Pointee->getAs<UnresolvedUsingType>();
        Unresolved && Unresolved->getDecl()->getIdentifier() &&
        hasTypePrefix(identifierTextOf(Unresolved->getDecl()), {"F", "TF"}))
      return 'x';
    // Also a member alias template, `typename TCSignatures<tf_CType>::template
    // FCallback<int> *`.
    if (const auto *Specialization =
            Pointee->getAs<TemplateSpecializationType>()) {
      if (const DependentTemplateName *Dependent =
              Specialization->getTemplateName().getAsDependentTemplateName())
        if (const IdentifierInfo *Name = Dependent->getName().getIdentifier();
            Name && hasTypePrefix(Name->getName(), {"F", "TF"}))
          return 'x';
      // And an alias template named as one, `FCallback<tf_CType> *`.
      if (const TemplateDecl *Alias =
              Specialization->getTemplateName().getAsTemplateDecl();
          Specialization->isTypeAlias() && declaredAsFunctionType(Alias))
        return 'x';
      // And a template template parameter named as one, `tf_TFCallback<int>
      // *`.
      if (const auto *Parameter = dyn_cast_or_null<TemplateTemplateParmDecl>(
              Specialization->getTemplateName().getAsTemplateDecl());
          Parameter && Parameter->getIdentifier()) {
        StringRef Prefix, Rest;
        if (splitStoragePrefix(identifierTextOf(Parameter), Prefix, Rest) &&
            hasTypePrefix(Rest, {"TF"}))
          return 'x';
      }
    }
    // And an alias named as one, `using FCallback = tf_CType;`.
    if (const auto *Typedef = Pointee->getAs<TypedefType>();
        Typedef && declaredAsFunctionType(Typedef->getDecl()))
      return 'x';
    // A written pointer is a pointer whatever an argument makes what it points
    // to, also to what depends on them and isn't named as a function type,
    // `typename TCIdentity<tf_FSignature>::CType *` or
    // `decltype(tf_CTraits::fs_Run) *`, which `p` fits in every instantiation.
    return 'p';
  }

  // An iterator type is often an alias, like `iterator`, so look at every
  // level of sugar, before looking at the pointer an alias can name. A smart
  // pointer is a pointer also through an alias, but not through one listed as
  // a handle, which names a concept of its own, like `CActorSubscription`.
  bool ThroughHandle = ReferenceThroughHandle;
  // Not a raw pointer or a function, which is named as one whatever its
  // alias is named, like `CIntIterator` for `int *`.
  const bool Raw = Type->isAnyPointerType() || Type->isFunctionType() ||
                   Type->isMemberPointerType() || Type->isBlockPointerType();
  // Whether an alias \p Named that a pattern of \p List matches stands for
  // what \p Models tells of \p Aliased: known, or where any of them can't be
  // told and none tells, unknown, which allows the concept and requires none
  // (Undecided).
  auto NamedAs = [&](const NamedDecl *Named, llvm::ArrayRef<StringRef> List,
                     std::optional<bool> (*Models)(StringRef, QualType),
                     QualType Aliased) {
    std::optional<bool> Is = false;
    for (const unsigned Index : matchingPatterns(Named, List))
      Is = eitherHolds(Is, Models(List[Index], Aliased));
    return Is;
  };
  // What the walk of the sugar from a level finds, with whether it starts
  // through a handle, is kept for that level (AliasWalks): the types along one
  // chain of aliases, which each walk to its end, go through each alias once.
  // A level's own part, with what it allows (Allow) and whether it makes a
  // smart pointer unknown to be a handle (HandleUnknown) kept apart.
  auto WalkLevel = [&](QualType Level, bool InHandle) {
    CAliasWalk Walk;
    Walk.ThroughHandle = InHandle;
    llvm::SmallString<4> MayBeBefore;
    std::swap(MayBeBefore, Concepts.MayBe);
    const bool HandleUnknownBefore = std::exchange(HandleUnknown, false);
    auto Returned = [&](std::optional<char> Concept) {
      Walk.Returns = true;
      Walk.Return = Concept;
      return true;
    };
    const auto Part = [&] {
      if (const auto *Typedef = dyn_cast<TypedefType>(Level.getTypePtr())) {
        const std::optional<bool> Iterator =
            NamedAs(Typedef->getDecl(), IteratorTypes, modelsIterator,
                    Typedef->desugar());
        if (knownTrue(Iterator))
          return Returned('i');
        if (!Iterator) {
          Allow('i');
          Walk.Undecided = true;
        }
        Walk.ThroughHandle |= IsHandle(Typedef);
        // Also a smart pointer named as one, `using CExceptionPointer =
        // std::exception_ptr` of `*Pointer`, not a handle, also not a class
        // derived from one, `using CSubscriptionPointer = CSubscriptionWithID`,
        // nor one that isn't known not to be one.
        const std::optional<bool> Pointer =
            Walk.ThroughHandle
                ? std::optional<bool>(false)
                : NamedAs(Typedef->getDecl(), SmartPointerTypes, modelsPointer,
                          Typedef->desugar());
        if (!Pointer) {
          Allow('p');
          Walk.Undecided = true;
        }
        if (knownTrue(Pointer)) {
          // Of a handle, what else it is, like an iterator, as of the class
          // spelled itself, after; of what isn't known to be one, that too.
          const CXXRecordDecl *Named =
              Typedef->desugar()->getAsCXXRecordDecl();
          if (!Named)
            return Returned('p');
          if (const std::optional<char> Pointer =
                  PointerUnlessHandle(false, Named))
            return Returned(Pointer);
          Walk.ThroughHandle = true;
        }
      }
      if (const auto *Specialization =
              dyn_cast<TemplateSpecializationType>(Level.getTypePtr());
          Specialization && Specialization->isTypeAlias()) {
        // An alias template, like `template <...> using TCIterator = ...`.
        if (const TemplateDecl *Template =
                Specialization->getTemplateName().getAsTemplateDecl()) {
          const std::optional<bool> Iterator =
              NamedAs(Template, IteratorTypes, modelsIterator,
                      Specialization->desugar());
          if (knownTrue(Iterator))
            return Returned('i');
          if (!Iterator) {
            Allow('i');
            Walk.Undecided = true;
          }
        }
        Walk.ThroughHandle |= IsHandle(Specialization);
      }
      return false;
    };
    Part();
    Walk.Allowed = Concepts.MayBe;
    Walk.HandleUnknown = HandleUnknown;
    Concepts.MayBe = MayBeBefore;
    HandleUnknown = HandleUnknownBefore;
    return Walk;
  };
  if (!Raw) {
    struct CStep {
      void *Level;
      bool InHandle;
      CAliasWalk Walk;
    };
    llvm::SmallVector<CStep, 4> Steps;
    std::optional<CAliasWalk> Rest;
    QualType Level = Type;
    bool InHandle = ThroughHandle;
    for (;;) {
      if (const auto Found = AliasWalks.find(
              std::make_pair(Level.getAsOpaquePtr(), int(InHandle)));
          Found != AliasWalks.end()) {
        Rest = Found->second;
        break;
      }
      Steps.push_back(
          {Level.getAsOpaquePtr(), InHandle, WalkLevel(Level, InHandle)});
      if (Steps.back().Walk.Returns)
        break;
      const QualType Next = Level.getSingleStepDesugaredType(Context);
      if (Next == Level)
        break;
      Level = Next;
      InHandle = Steps.back().Walk.ThroughHandle;
    }
    // From the last level back, each level's walk is its own part followed by
    // the walk from the next level.
    for (CStep &Step : llvm::reverse(Steps)) {
      if (!Step.Walk.Returns && Rest) {
        for (const char Concept : Rest->Allowed)
          if (!llvm::is_contained(Step.Walk.Allowed, Concept))
            Step.Walk.Allowed.push_back(Concept);
        Step.Walk.Undecided |= Rest->Undecided;
        Step.Walk.HandleUnknown |= Rest->HandleUnknown;
        Step.Walk.ThroughHandle = Rest->ThroughHandle;
        Step.Walk.Returns = Rest->Returns;
        Step.Walk.Return = Rest->Return;
      }
      AliasWalks[std::make_pair(Step.Level, int(Step.InHandle))] = Step.Walk;
      Rest = Step.Walk;
    }
    for (const char Concept : Rest->Allowed)
      Allow(Concept);
    Undecided |= Rest->Undecided;
    HandleUnknown |= Rest->HandleUnknown;
    ThroughHandle = Rest->ThroughHandle;
    if (Rest->Returns)
      return Rest->Return;
  }

  // The function object concept covers function pointers too.
  if (Type->isFunctionPointerType() || Type->isMemberFunctionPointerType() ||
      Type->isFunctionType() || Type->isBlockPointerType())
    return 'f';
  if (Type->isAnyPointerType() || Type->isNullPtrType() ||
      Type->isMemberDataPointerType())
    return 'p';
  if (Type->isBooleanType())
    return 'b';
  // An atomic pointer or function, `TCAtomic<int *>`, also one that clears
  // itself.
  if (const auto *Holder = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
          Type->getAsCXXRecordDecl());
      Holder && isValueWrapper(Holder) &&
      Holder->getTemplateArgs().size() >= 1 &&
      Holder->getTemplateArgs()[0].getKind() == TemplateArgument::Type) {
    QualType Held = Holder->getTemplateArgs()[0].getAsType();
    // What it holds as written, which keeps the alias of an opaque handle,
    // `TCAtomic<pthread_t>`, that the specialization's arguments drop, also
    // through an alias template, `TCWrapped<pthread_t>` of `using TCWrapped =
    // TCAtomic<t_CType>`, whose aliased type keeps what it is given.
    for (QualType Level = Type; !Level.isNull();) {
      // Of the template however it was declared, also forward, `template
      // <typename t_CType> struct TCAtomic;` before its definition.
      const auto *Written = Level->getAs<TemplateSpecializationType>();
      const TemplateDecl *WrittenTemplate =
          Written ? Written->getTemplateName().getAsTemplateDecl() : nullptr;
      if (WrittenTemplate &&
          WrittenTemplate->getCanonicalDecl() ==
              Holder->getSpecializedTemplate()->getCanonicalDecl()) {
        if (!Written->template_arguments().empty() &&
            Written->template_arguments()[0].getKind() ==
                TemplateArgument::Type)
          Held = Written->template_arguments()[0].getAsType();
        // Also a default argument of the wrapper, `TCAtomic<>` of `template
        // <typename t_CType = pthread_t>`, as it is written there: of the
        // declaration the use names, which has the defaults of those before
        // it, not the first one, which a forward declaration can be.
        else if (const auto *Parameter = dyn_cast<TemplateTypeParmDecl>(
                     WrittenTemplate->getTemplateParameters()->getParam(0));
                 Written->template_arguments().empty() && Parameter &&
                 Parameter->hasDefaultArgument() &&
                 !Parameter->getDefaultArgument()
                      .getArgument()
                      .getAsType()
                      ->isDependentType())
          Held = Parameter->getDefaultArgument().getArgument().getAsType();
        break;
      }
      const QualType Next = Level.getSingleStepDesugaredType(Context);
      if (Next == Level)
        break;
      Level = Next;
    }
    if (isOpaqueHandle(Held, Context))
      return Opaque();
    // A boolean that clears itself, Malterlib's `zbool`, a
    // `TCAutoClear<bool>`, also `TCAutoClearInt<bool>`, or an atomic one,
    // `TCAtomic<bool>`, unless an opaque handle, as written, is one here.
    if (Held->isBooleanType())
      return 'b';
    if (Held->isFunctionPointerType() || Held->isMemberFunctionPointerType())
      return 'f';
    if (Held->isPointerType() || Held->isMemberDataPointerType())
      return 'p';
  }

  const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
  if (!Record)
    return std::nullopt;
  if (Record->isLambda())
    return 'f';
  const std::optional<bool> Pointer = isRecordInList(
      Record, SmartPointerTypes, PointerOperators, /*OrConvertsToBool=*/true);
  const std::optional<bool> Iterator =
      isRecordInList(Record, IteratorTypes, IteratorOperators);
  // One whose being a function object isn't known can be named as one, or as
  // anything else it can be, or neither.
  if (const std::optional<bool> Function = isFunctionObject(Record)) {
    if (*Function)
      return 'f';
  } else {
    Allow('f');
    if (possiblyTrue(Iterator))
      Allow('i');
    if (possiblyTrue(Pointer))
      Allow('p');
    return std::nullopt;
  }
  if (knownTrue(Pointer)) {
    // A handle, or what isn't known to be one, can still be an iterator.
    if (const std::optional<char> Known =
            PointerUnlessHandle(ThroughHandle, Record))
      return Known;
  } else if (!Pointer && !ThroughHandle) {
    // One whose being a smart pointer can't be told, which ranks before an
    // iterator, can be named either, or neither.
    Allow('p');
    if (possiblyTrue(Iterator))
      Allow('i');
    return std::nullopt;
  }
  if (knownTrue(Iterator)) {
    // Of what isn't known to be a handle, which is an iterator, or not, a
    // pointer: either, also `i`.
    if (HandleUnknown) {
      Allow('i');
      return std::nullopt;
    }
    return 'i';
  }
  if (!Iterator)
    Allow('i');
  return std::nullopt;
}

static StringRef conceptName(char Concept) {
  switch (Concept) {
  case 'b':
    return "a boolean";
  case 'f':
    return "a function object";
  case 'i':
    return "an iterator";
  case 'p':
    return "a pointer";
  case 'x':
    return "a function or a pointer to a function object";
  default:
    return "";
  }
}

// What the parameters of a partial specialization \p Pattern are bound
// to by matching its arguments \p Taken to those \p Written, `t_CReturn`
// of `TCFunction<t_CReturn ()>` to `tf_CReturn` of `TCFunction<tf_CReturn
// ()>`, or none when they don't match as written. A value is matched by a
// parameter of it, `t_iCall`, or by an equal constant, `true` of
// `TCResult<tf_CValue, true>`, which \p Context evaluates. A parameter bound
// to arguments written differently, a handle and the smart pointer it is an
// alias of, `TCPair<CActorSubscription, TCUniquePointer<...>>` of
// `TCPair<t_CType, t_CType>`, is in \p Ambiguous: what it is written as can't
// be told.
/// The aliases \p Type is named through at its top, outermost first, as
/// clang's single-step desugaring passes them, at most 16.
static llvm::SmallVector<const Decl *, 4> topAliases(QualType Type) {
  llvm::SmallVector<const Decl *, 4> Aliases;
  for (int Step = 0; Step < 16; ++Step) {
    const clang::Type *Node = Type.getTypePtr();
    if (const auto *Typedef = dyn_cast<TypedefType>(Node))
      Aliases.push_back(Typedef->getDecl()->getCanonicalDecl());
    else if (const auto *Specialization =
                 dyn_cast<TemplateSpecializationType>(Node);
             Specialization && Specialization->isTypeAlias())
      Aliases.push_back(Specialization->getTemplateName().getAsTemplateDecl());
    const QualType Next = Node->getLocallyUnqualifiedSingleStepDesugaredType();
    if (Next.getTypePtr() == Node)
      break;
    Type = Next;
  }
  return Aliases;
}

static std::optional<llvm::DenseMap<unsigned, QualType>>
bindPartial(llvm::ArrayRef<TemplateArgument> Taken,
            llvm::ArrayRef<TemplateArgument> Written, unsigned Depth,
            llvm::SmallDenseSet<unsigned, 2> &Ambiguous,
            const ASTContext *Context = nullptr) {
  llvm::DenseMap<unsigned, QualType> Bound;
  TCRecursive<bool(QualType, QualType, int)> Match =
      [&](QualType In, QualType Of, int Level) {
        if (Level > 8 || In.isNull() || Of.isNull())
          return false;
        In = In.getCanonicalType();
        if (const auto *Parameter = dyn_cast<TemplateTypeParmType>(In);
            Parameter && Parameter->getDepth() == Depth) {
          auto [It, New] = Bound.try_emplace(Parameter->getIndex(), Of);
          if (New)
            return true;
          if (It->second.getCanonicalType() != Of.getCanonicalType())
            return false;
          // Written through other aliases at the top, where a handle or a
          // listed alias is told apart from what it stands for; sugar isn't
          // unique, `TCUniquePointer<int>` written twice is two types.
          if (topAliases(It->second) != topAliases(Of))
            Ambiguous.insert(Parameter->getIndex());
          return true;
        }
        const QualType Canonical = Of.getCanonicalType();
        if (In.getLocalQualifiers() != Canonical.getLocalQualifiers())
          return false;
        if (const auto *Function = dyn_cast<FunctionProtoType>(In)) {
          const auto *Other = Canonical->getAs<FunctionProtoType>();
          const auto *Spelled =
              Of.getNonReferenceType()->getAs<FunctionProtoType>();
          // Also of a pack last, `t_CReturn (tp_CParams...)`, which takes
          // the rest, whatever they are.
          const unsigned Fixed =
              Function->getNumParams() &&
                      isa<PackExpansionType>(Function->getParamType(
                          Function->getNumParams() - 1))
                  ? Function->getNumParams() - 1
                  : Function->getNumParams();
          const bool Pack = Fixed != Function->getNumParams();
          if (!Other || !Spelled ||
              (Pack ? Other->getNumParams() < Fixed
                    : Other->getNumParams() != Fixed) ||
              Other->isVariadic() != Function->isVariadic())
            return false;
          for (unsigned iParam = 0; iParam < Fixed; ++iParam)
            if (!Match(Function->getParamType(iParam),
                       Spelled->getParamType(iParam), Level + 1))
              return false;
          return Match(Function->getReturnType(), Spelled->getReturnType(),
                       Level + 1);
        }
        if (const auto *Pointer = dyn_cast<PointerType>(In))
          return Canonical->isPointerType() &&
                 Match(Pointer->getPointeeType(), Of->getPointeeType(),
                       Level + 1);
        if (const auto *Reference = dyn_cast<ReferenceType>(In))
          return Canonical->isReferenceType() &&
                 isa<LValueReferenceType>(Reference) ==
                     Canonical->isLValueReferenceType() &&
                 Match(Reference->getPointeeType(),
                       Of.getNonReferenceType(), Level + 1);
        return !In->isDependentType() && !Canonical->isDependentType() &&
               In == Canonical;
      };
  auto Value =
      [&](const TemplateArgument &Argument) -> std::optional<llvm::APSInt> {
    if (Argument.getKind() == TemplateArgument::Integral)
      return Argument.getAsIntegral();
    if (!Context)
      return std::nullopt;
    return templateArgumentValueOf(Argument, *Context);
  };
  auto MatchValue = [&](const TemplateArgument &In,
                        const TemplateArgument &Of) {
    if (In.getKind() == TemplateArgument::Expression)
      if (const auto *Ref =
              dyn_cast<DeclRefExpr>(In.getAsExpr()->IgnoreParenImpCasts()))
        if (const auto *Parameter =
                dyn_cast<NonTypeTemplateParmDecl>(Ref->getDecl()))
          return Parameter->getDepth() == Depth;
    const std::optional<llvm::APSInt> Left = Value(In);
    const std::optional<llvm::APSInt> Right = Value(Of);
    return Left && Right && llvm::APSInt::compareValues(*Left, *Right) == 0;
  };
  if (Taken.size() != Written.size())
    return std::nullopt;
  for (size_t iArgument = 0; iArgument < Taken.size(); ++iArgument) {
    if (Taken[iArgument].getKind() == TemplateArgument::Type &&
        Written[iArgument].getKind() == TemplateArgument::Type) {
      if (!Match(Taken[iArgument].getAsType(), Written[iArgument].getAsType(),
                 0))
        return std::nullopt;
      continue;
    }
    if (Taken[iArgument].getKind() == TemplateArgument::Type ||
        Written[iArgument].getKind() == TemplateArgument::Type ||
        !MatchValue(Taken[iArgument], Written[iArgument]))
      return std::nullopt;
  }
  return Bound;
}

/// The alias of SmartPointerHandleTypes that \p Type is, like
/// `CActorSubscription` of `TCUniquePointer<CActorSubscriptionReference>`,
/// looked up in the namespaces and classes of the smart pointer and of its
/// arguments, or none. A handle names a type of its own, which nothing names
/// otherwise.
QualType ConceptualPrefixCheck::handleAliasOf(QualType Type,
                                              const ASTContext &Context) const {
  const QualType Canonical =
      Type.getNonReferenceType().getCanonicalType().getUnqualifiedType();
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(
          Canonical->getAsCXXRecordDecl());
  if (!Specialization || SmartPointerHandleTypes.empty())
    return QualType();
  if (auto Found = HandleAliases.find(Canonical.getTypePtr());
      Found != HandleAliases.end())
    return Found->second;
  QualType &Alias = HandleAliases[Canonical.getTypePtr()];
  llvm::SmallVector<const DeclContext *, 8> Contexts;
  auto AddContexts = [&](const DeclContext *Enclosing) {
    for (; Enclosing; Enclosing = Enclosing->getParent())
      if (Enclosing->isFileContext() || isa<CXXRecordDecl>(Enclosing))
        if (!llvm::is_contained(Contexts, Enclosing->getPrimaryContext()))
          Contexts.push_back(Enclosing->getPrimaryContext());
  };
  AddContexts(Specialization->getDeclContext());
  for (const TemplateArgument &Argument :
       Specialization->getTemplateArgs().asArray())
    if (Argument.getKind() == TemplateArgument::Type)
      if (const TagDecl *Tag = Argument.getAsType()->getAsTagDecl())
        AddContexts(Tag->getDeclContext());
  for (StringRef Pattern : SmartPointerHandleTypes) {
    // A pattern, `*Subscription`, names none to look up.
    if (Pattern.starts_with("*"))
      continue;
    StringRef Name = Pattern;
    if (const size_t Colons = Pattern.rfind("::"); Colons != StringRef::npos)
      Name = Pattern.drop_front(Colons + 2);
    const DeclarationName Looked = &Context.Idents.get(Name);
    for (const DeclContext *Within : Contexts)
      for (const NamedDecl *Found : Within->lookup(Looked))
        if (const auto *Typedef = dyn_cast<TypedefNameDecl>(Found);
            Typedef && matchesName(Typedef, Pattern) &&
            Context.hasSameType(Typedef->getUnderlyingType(), Canonical)) {
          Alias = Context.getTypedefType(ElaboratedTypeKeyword::None,
                                         std::nullopt, Typedef);
          return Alias;
        }
  }
  return Alias;
}

void ConceptualPrefixCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  const SourceManager &SM = *Result.SourceManager;
  const auto *Declaration = Result.Nodes.getNodeAs<ValueDecl>("declaration");
  // Nor the placeholder of what is ignored, C++26 `bool _ = ...` or `auto
  // [Ticket, _] = ...`, which is no name.
  if (!Declaration->getIdentifier() || Declaration->isImplicit() ||
      Declaration->isInvalidDecl() ||
      Declaration->isPlaceholderVar(Result.Context->getLangOpts()) ||
      isInExternalSpecialization(Declaration, SM))
    return;
  // Not what an instantiation of a variable template makes, in a lambda of
  // its initializer, `t_CType g_Value = [] { t_CType Value{}; ... }()`, or an
  // instantiation of a function template in a lambda of a default argument,
  // which are checked in the template.
  {
    DynTypedNode Node = DynTypedNode::create(*Declaration);
    for (int Depth = 0; Depth < 256; ++Depth) {
      const auto Parents =
          Result.Context->getParentMapContext().getParents(Node);
      if (Parents.empty())
        break;
      Node = Parents[0];
      if (const auto *Function = Node.get<FunctionDecl>()) {
        if (Function->isTemplateInstantiation())
          return;
        if (!isLambdaCallOperator(Function))
          break;
      }
      if (const auto *Variable = Node.get<VarDecl>();
          Variable && !isa<ParmVarDecl>(Variable) &&
          isTemplateInstantiation(Variable->getTemplateSpecializationKind()))
        return;
    }
  }
  // Nor a member of an instantiation of a class template, also an explicit
  // one, `template struct TCStorage<bool>;`, which is checked in the template.
  for (const DeclContext *Context = Declaration->getDeclContext(); Context;
       Context = Context->getParent())
    if (const auto *Record = dyn_cast<CXXRecordDecl>(Context);
        Record &&
        isTemplateInstantiation(Record->getTemplateSpecializationKind()))
      return;
  if (const auto *Variable = dyn_cast<VarDecl>(Declaration)) {
    if (!isa<ParmVarDecl>(Variable) && !Variable->isFirstDecl())
      return;
    // Not an instantiation, but an explicit or partial specialization of a
    // variable template, which is written like any other.
    if (isTemplateInstantiation(Variable->getTemplateSpecializationKind()))
      return;
    if (Result.Context->getLangOpts().CPlusPlus && Variable->isExternC())
      return;
    // The parameters of a function with C linkage are checked like any
    // other: their names are neither its symbol nor its ABI, and only the
    // sources of checked repositories are, so a system's declarations aren't.
    if (const auto *Function =
            dyn_cast_or_null<FunctionDecl>(Variable->getDeclContext());
        Function && isa<ParmVarDecl>(Variable) &&
        Function->isTemplateInstantiation())
      return;
  }

  if (llvm::is_contained(IgnoredNames, identifierTextOf(Declaration)))
    return;
  // An array parameter is adjusted to a pointer, but is written as an array.
  const QualType Type = isa<ParmVarDecl>(Declaration)
                            ? cast<ParmVarDecl>(Declaration)->getOriginalType()
                            : Declaration->getType();
  // The type of a structured binding is not known until its decomposition is
  // complete, which it may not be after errors.
  if (Type.isNull())
    return;
  // An array has no concept prefix, whatever its elements are, `g_WeekDays`
  // of `CStr const *g_WeekDays[7]`, also one it is referred to as. It is a
  // range, which `r` names, `int (&o_rValues)[4]`.
  QualType Arrayed = Type.getNonReferenceType();
  if (const auto *Pack = Arrayed->getAs<PackExpansionType>())
    Arrayed = Pack->getPattern().getNonReferenceType();
  if (Arrayed->isArrayType()) {
    StringRef Storage, Named = identifierTextOf(Declaration);
    splitStoragePrefix(identifierTextOf(Declaration), Storage, Named);
    if (Named.size() >= 2 && ConceptPrefixes.contains(Named[0]) &&
        Named[0] != RangePrefix &&
        (llvm::isUpper(Named[1]) || llvm::isDigit(Named[1])))
      if (const SourceLocation Loc = nameLocation(Declaration, SM);
          Loc.isValid())
        diag(Loc, "%0 is an array, which has no concept prefix")
            << diagnosticName(Declaration);
    return;
  }
  std::optional<char> Concept = expectedConcept(Type, *Result.Context).Required;
  // A type that is a member of what depends on template parameters,
  // `typename t_CTraits::CPointer`, which the traits it is given decide. Not
  // one that is a parameter, `t_CType m_Value`, which is generic, whatever it
  // is instantiated with.
  const clang::Type *Written = Type.getNonReferenceType().getTypePtr();
  // Of a pack, `typename tfp_CTraits::CPointer ...p_Values`, what it expands,
  // also in parentheses, `typename tf_CTraits::CPointer (_Value)`, or through
  // a local alias of a dependent type, `using CPointer = typename
  // tf_CTraits::CPointer`.
  for (int Depth = 0; Depth < 16; ++Depth) {
    if (const auto *Expansion = dyn_cast<PackExpansionType>(Written))
      Written = Expansion->getPattern().getNonReferenceType().getTypePtr();
    else if (const auto *Paren = dyn_cast<ParenType>(Written))
      Written = Paren->getInnerType().getNonReferenceType().getTypePtr();
    // Also annotated, `typename tf_CTraits::CValue
    // [[clang::annotate_type("...")]]`.
    else if (const auto *Attributed = dyn_cast<AttributedType>(Written))
      Written =
          Attributed->getModifiedType().getNonReferenceType().getTypePtr();
    else if (const auto *Typedef = dyn_cast<TypedefType>(Written);
             Typedef && Typedef->isDependentType())
      Written = Typedef->desugar().getNonReferenceType().getTypePtr();
    // Also through an alias template, `TCIdentity<typename
    // t_CTraits::CValue>`, what it is an alias of.
    else if (const auto *Alias = dyn_cast<TemplateSpecializationType>(Written);
             Alias && Alias->isTypeAlias() && Alias->isDependentType())
      Written = Alias->getAliasedType().getNonReferenceType().getTypePtr();
    else if (const auto *Substituted =
                 dyn_cast<SubstTemplateTypeParmType>(Written);
             Substituted && Substituted->isDependentType())
      Written =
          Substituted->getReplacementType().getNonReferenceType().getTypePtr();
    else
      break;
  }
  // A member type that depends on the parameters, or what is computed from
  // them, like `__remove_reference_t(typename tf_CTraits::CValue)`, but not
  // what only forwards a parameter, which is generic like it,
  // `__remove_reference_t(tf_CType)` or `typename
  // TCIdentity<tf_CType>::CType`. Not a pointer to one, which is a pointer
  // whatever it points to.
  const bool ForwardsParameter =
      givesParameter(QualType(Written, 0), /*Nested=*/false);
  const bool DependentMember =
      !ForwardsParameter && dependsAsMember(Written);
  // A specialization of a class template given what depends on the
  // parameters, `TCDeferred<tf_CResult>`, whose instantiations tell what it is
  // not, like a function object through a base that depends on them, also one
  // whose concept is that of what it holds, `TCAtomic<t_CType>`, which is
  // generic, but not of a concept its instantiations exclude.
  // Also through an alias template, `TCWrapped<typename t_CTraits::CBool>`
  // of `using TCWrapped = TCAtomic<t_CType>`, of what it names.
  const TemplateSpecializationType *Specialization =
      writtenSpecialization(Type.getNonReferenceType().getTypePtr());
  for (int Depth = 0; Specialization && Specialization->isTypeAlias() &&
                      Depth < 8;
       ++Depth)
    Specialization = Specialization->getAliasedType()
                         ->getAs<TemplateSpecializationType>();
  const bool DependentSpecialization = Specialization &&
                                       !Specialization->isTypeAlias() &&
                                       Type->isDependentType();
  StringRef Prefix, Rest = identifierTextOf(Declaration);
  splitStoragePrefix(identifierTextOf(Declaration), Prefix, Rest);
  // A pointer to what is named as a function type, named as a function,
  // `tf_FFunction *_fObject`, whose instantiations tell whether it points to
  // one, also a reference to one, `tf_FFunction *&_fObject`.
  const bool FunctionPointerNamed =
      Concept == std::optional<char>('x') && Written->isPointerType() &&
      Rest.size() >= 2 && Rest[0] == 'f' &&
      (llvm::isUpper(Rest[1]) || llvm::isDigit(Rest[1]));
  // What the instantiations below make it, also where they don't tell a
  // concept, `int` of `typename t_CTraits::CType`, whose name then can't have
  // one it can't be, unless one is of an argument it is given as it is, which
  // is generic.
  // Each with the instantiation it is of, which a report decided by it names
  // (InstantiationScope), `fg_Read<int>` of a local `tf_CType pValue`.
  // Of a use of an alias template, which makes no instantiation, the use and
  // the alias template (noteAliasUse).
  struct CObserved {
    QualType Type;
    const Decl *Instance;
    SourceLocation Use = SourceLocation();
    const NamedDecl *Alias = nullptr;
  };
  llvm::SmallVector<CObserved, 4> Observed;
  // A variable template whose type is deduced from what depends on template
  // parameters, `auto g_pValue = static_cast<t_CType *>(nullptr)`, or is a
  // member of it, `typename t_CTraits::CPointer g_pValue`, or holds one,
  // `TCAtomic<typename t_CTraits::CBool> g_Ready`, is what its
  // instantiations make it, when they all agree, like for the members below,
  // also a partial specialization, of the instantiations of it.
  const auto *TemplateVariable = dyn_cast<VarDecl>(Declaration);
  const auto *Partial =
      dyn_cast_or_null<VarTemplatePartialSpecializationDecl>(TemplateVariable);
  const VarTemplateDecl *VariableTemplate =
      Partial            ? Partial->getSpecializedTemplate()
      : TemplateVariable ? TemplateVariable->getDescribedVarTemplate()
                         : nullptr;
  // Also one of a parameter as it is, `t_CType gc_Max`, also through what
  // forwards one, `TCRemoveReference<tf_CType> &_Value`.
  const bool OfParameter =
      Type.getNonReferenceType()->getAs<TemplateTypeParmType>() ||
      ForwardsParameter;
  if ((!Concept || FunctionPointerNamed) && VariableTemplate &&
      (Type.getNonReferenceType()->isUndeducedAutoType() || DependentMember ||
       DependentSpecialization || OfParameter || FunctionPointerNamed)) {
    // Also of a member template of a class template, `template <typename
    // t_CUnused> static inline typename t_CTraits::CPointer ms_Value`, whose
    // specializations are of the member template of each instantiation of
    // the class.
    // Only what clang instantiated from it (isInstanceOf), of the primary
    // template or of the partial specialization it is, also through the
    // partial specializations each instantiation of the class makes of it.
    llvm::SmallVector<const VarTemplateDecl *, 2> Templates{VariableTemplate};
    if (const auto *Class =
            dyn_cast<CXXRecordDecl>(VariableTemplate->getDeclContext());
        Class && Class->isDependentContext())
      for (const CXXRecordDecl *Instance : instancesOf(Class))
        for (const Decl *Member : Instance->decls())
          if (const auto *Instantiated = dyn_cast<VarTemplateDecl>(Member);
              Instantiated &&
              Instantiated->getInstantiatedFromMemberTemplate() &&
              Instantiated->getInstantiatedFromMemberTemplate()
                      ->getCanonicalDecl() ==
                  VariableTemplate->getCanonicalDecl())
            Templates.push_back(Instantiated);
    for (const VarTemplateDecl *Of : Templates)
      for (const VarTemplateSpecializationDecl *Instance :
           Of->specializations()) {
        if (!isInstanceOf(Instance, TemplateVariable) ||
            Instance->getType()->isUndeducedAutoType() ||
            Instance->getType()->isDependentType())
          continue;
        Observed.push_back({Instance->getType(), Instance});
      }
  }
  // A copy of what is named as a function type in a template, `auto Copy =
  // _fAction` of `tf_FAction &&_fAction`, is a function object, as written,
  // also where an instantiation deduces a reference, which loses the name,
  // and one moved, forwarded or copied, `[Callback = fg_Move(_fCallback)]`
  // or `fg_TempCopy(_fCallback)`.
  const Expr *CopiedFrom = nullptr;
  // Whether through a call that gives its type without the name it has,
  // `fg_TempCopy(_Scope)`.
  bool CopiedThroughCall = false;
  if (const auto *Variable = dyn_cast<VarDecl>(Declaration);
      Variable && Variable->getInit()) {
    CopiedFrom = Variable->getInit()->IgnoreParenImpCasts();
    // Also initialized in parentheses in a template, `auto Copy(_fAction)`,
    // whose initializer is the list of them.
    if (const auto *List = dyn_cast<ParenListExpr>(CopiedFrom);
        List && List->getNumExprs() == 1)
      CopiedFrom = List->getExpr(0)->IgnoreParenImpCasts();
    // Also one not resolved yet in the template, `fg_Move(_fCallback)`, and
    // one constructed from it, by the copy or the move of the class.
    for (int Depth = 0; Depth < 4; ++Depth) {
      if (const auto *Construct = dyn_cast<CXXConstructExpr>(CopiedFrom);
          Construct && Construct->getNumArgs() == 1 &&
          Construct->getConstructor()->isCopyOrMoveConstructor())
        CopiedFrom =
            Construct->getArg(0)->IgnoreImplicit()->IgnoreParenImpCasts();
      const auto *Call = dyn_cast<CallExpr>(CopiedFrom);
      if (!Call || Call->getNumArgs() < 1 || Call->getNumArgs() > 2)
        break;
      DeclarationName Name;
      if (const FunctionDecl *Callee = Call->getDirectCallee())
        Name = Callee->getDeclName();
      else if (const auto *Unresolved = dyn_cast<UnresolvedLookupExpr>(
                   Call->getCallee()->IgnoreParenImpCasts()))
        Name = Unresolved->getName();
      // Not one called through what depends on the parameters, `Power.
      // f_ToInt(bOverflow)`, which has no name yet.
      // Also what an exchange gives back of what it is given first, `auto
      // Subscription = fg_Exchange(_Subscription, nullptr)`.
      const StringRef Called = Name.getAsIdentifierInfo()
                                   ? Name.getAsIdentifierInfo()->getName()
                                   : StringRef();
      if (Called.empty() ||
          !(Call->getNumArgs() == 1
                ? isMoveOrForwardFunction(Called) || isCopyFunction(Called)
                : isExchangeFunction(Called)))
        break;
      CopiedFrom = Call->getArg(0)->IgnoreParenImpCasts();
      CopiedThroughCall = true;
    }
  }
  // Also where it is deduced, outside of a template, from a move that loses
  // the name, `auto Copy = fg_Move(_fAction)` of `FAction _fAction`, whose
  // deduced type is the class, which takes no prefix.
  // Also each of a pack of them, `[...p_Actions = fg_TempCopy(p_fActions)]`.
  // And through such a call, what the copy is named as that it copies, `auto
  // Scope = fg_TempCopy(_Scope)` of an alias of a smart pointer named for
  // what it is, `COnScopeExitShared`, like a copy written with its type.
  auto PastPack = [](QualType Of) {
    Of = Of.getNonReferenceType();
    if (const auto *Pack = Of->getAs<PackExpansionType>())
      Of = Pack->getPattern().getNonReferenceType();
    return Of;
  };
  // Whether what it copies tells what it is named as, also that it takes no
  // prefix, which the instantiations of a template don't tell.
  bool NamedAsSource = false;
  if (const auto *Variable = dyn_cast<VarDecl>(Declaration);
      (!Concept || *Concept == 'x' || CopiedThroughCall) && Variable &&
      (Variable->isLocalVarDecl() || Variable->isInitCapture() ||
       Variable->isFileVarDecl() || Variable->isStaticDataMember()) &&
      PastPack(Type)->getContainedAutoType() && CopiedFrom) {
    // Of a variable, or of a member, `auto Copy = m_fAction`, also one
    // instantiated as a reference, `t_FAction` of `CAction &`.
    // Also what a getter gives, as its function declares it, `fg_Move(
    // f_GetSubscription())` of `CActorSubscription &f_GetSubscription()`.
    // Also either of a conditional whose results are written as the same,
    // `fg_Move(_bFirst ? _fFirst : _fSecond)`.
    TCRecursive<QualType(const Expr *, int)> WrittenOf =
        [&](const Expr *From, int Depth) -> QualType {
      From = From->IgnoreParenImpCasts();
      if (const auto *Copied = dyn_cast<DeclRefExpr>(From))
        if (const auto *Source = dyn_cast<VarDecl>(Copied->getDecl());
            Source && Source != Variable)
          return Source->getType();
      if (const auto *Member = dyn_cast<MemberExpr>(From))
        if (const auto *Source = dyn_cast<FieldDecl>(Member->getMemberDecl()))
          return Source->getType();
      if (const auto *Getter = dyn_cast<CallExpr>(From);
          Getter && CopiedThroughCall && Getter->getDirectCallee())
        return Getter->getDirectCallee()->getReturnType();
      // Also what a pointer it is written as points to, `*_pAction` of
      // `tf_FAction *_pAction`.
      if (const auto *Deref = dyn_cast<UnaryOperator>(From);
          Deref && Deref->getOpcode() == UO_Deref && Depth < 8)
        if (const QualType Pointer = WrittenOf(Deref->getSubExpr(), Depth + 1);
            !Pointer.isNull() && Pointer->isPointerType())
          return Pointer->getPointeeType();
      if (const auto *Conditional = dyn_cast<ConditionalOperator>(From);
          Conditional && Depth < 8) {
        const QualType True = WrittenOf(Conditional->getTrueExpr(), Depth + 1);
        const QualType False =
            WrittenOf(Conditional->getFalseExpr(), Depth + 1);
        if (!True.isNull() && !False.isNull() &&
            True.getNonReferenceType().getUnqualifiedType() ==
                False.getNonReferenceType().getUnqualifiedType())
          return True;
      }
      return QualType();
    };
    if (const QualType Written = WrittenOf(CopiedFrom, 0); !Written.isNull()) {
      const QualType SourceType = PastPack(Written);
      if (namedAsFunctionType(Written) || namedAsFunctionType(SourceType))
        Concept = 'f';
      else if (CopiedThroughCall && !SourceType->isDependentType() &&
               !Type->isDependentType() &&
               Result.Context->hasSameUnqualifiedType(
                   SourceType, PastPack(Type).getUnqualifiedType()))
        Concept = expectedConcept(SourceType, *Result.Context).Required;
      // Also in a template, where it isn't deduced yet: what the vocabulary
      // gives back is what it is given, whatever the arguments are (decided
      // Q5 and Q6), instantiated or not.
      else if (CopiedThroughCall && !SourceType->isDependentType() &&
               Variable->getDeclContext()->isDependentContext()) {
        Concept = expectedConcept(SourceType, *Result.Context).Required;
        NamedAsSource = true;
      }
    }
  }
  // Also what a member function or operator of a class template gives of a
  // parameter of it, `_Result.f_Get()` of `t_CValue f_Get()` of `TCFuture<
  // CActorSubscription>`, or a pointer to one, `*begin()` of a range loop,
  // named as the argument is written, also a handle, which the type it is
  // deduced as doesn't keep; also a copy or a move of it, either of a
  // conditional written as the same, and what `co_await` gives.
  TCRecursive<QualType(const Expr *, int)> WrittenType;
  // Whether WrittenType met a parameter of a partial specialization bound to
  // arguments written differently (bindPartial): what it gives is then
  // unknown, not the first of them.
  bool WrittenAmbiguously = false;
  // What is written at the place of each parameter of the template \p Class
  // is instantiated from, given as \p Written, also of a partial
  // specialization, `CActorSubscription` of `t_CReturn` of `TCFunction<
  // t_CReturn ()>` written `TCFunction<CActorSubscription ()>`, and their
  // depth, and of those bound ambiguously, \p Ambiguous.
  auto Bindings = [&](const ClassTemplateSpecializationDecl *Class,
                      const TemplateSpecializationType *Written,
                      unsigned &ParameterDepth,
                      llvm::SmallDenseSet<unsigned, 2> &Ambiguous)
      -> std::optional<llvm::DenseMap<unsigned, QualType>> {
    const auto From = Class->getSpecializedTemplateOrPartial();
    if (const auto *Partial =
            From.dyn_cast<ClassTemplatePartialSpecializationDecl *>()) {
      ParameterDepth = Partial->getTemplateParameters()->getDepth();
      return bindPartial(Partial->getTemplateArgs().asArray(),
                         Written->template_arguments(), ParameterDepth,
                         Ambiguous);
    }
    ParameterDepth =
        Class->getSpecializedTemplate()->getTemplateParameters()->getDepth();
    llvm::DenseMap<unsigned, QualType> Bound;
    for (const auto [iArgument, Argument] :
         llvm::enumerate(Written->template_arguments()))
      if (Argument.getKind() == TemplateArgument::Type)
        Bound[iArgument] = Argument.getAsType();
    return Bound;
  };
  // What \p InPattern, a type as the pattern of a member of \p Class writes
  // it, of what is written \p Object, is as written, \p Actual what it is
  // instantiated as: a parameter, a pointer to one, or a specialization of a
  // template given them, `TCAwaiter<t_CType>`, which is then given what is
  // written at their places.
  TCRecursive<QualType(const Expr *, bool, const CXXRecordDecl *, QualType,
                         QualType, int)> OfMember = [&](const Expr *ObjectOf, bool Arrow,
                     const CXXRecordDecl *Parent, QualType InPattern,
                     QualType Actual, int Depth) -> QualType {
    const auto *Class =
        dyn_cast_or_null<ClassTemplateSpecializationDecl>(Parent);
    if (!Class)
      return QualType();
    QualType Object = WrittenType(ObjectOf, Depth + 1);
    if (Object.isNull())
      Object = ObjectOf->getType();
    Object = Object.getNonReferenceType();
    if (Arrow && Object->isPointerType())
      Object = Object->getPointeeType();
    const auto *Written = Object->getAs<TemplateSpecializationType>();
    // Of what an alias template stands for, as it is written with what the
    // alias is given.
    for (int Level = 0; Written && Written->isTypeAlias() && Level < 16;
         ++Level)
      Written = Written->getAliasedType()->getAs<TemplateSpecializationType>();
    // Only of a use that names the class the member is of, with its
    // arguments: not of a class derived from it, `TCContextBox<CContext *,
    // bool>` of `TCBox<t_CValue>::f_Get()`, whose arguments are of another
    // template; there what clang instantiated, \p Actual, is what it is.
    if (Written &&
        !Result.Context->hasSameType(
            QualType(Written, 0), Result.Context->getCanonicalTagType(Class)))
      Written = nullptr;
    unsigned ParameterDepth = 0;
    llvm::SmallDenseSet<unsigned, 2> Ambiguous;
    const std::optional<llvm::DenseMap<unsigned, QualType>> Bound =
        Written ? Bindings(Class, Written, ParameterDepth, Ambiguous)
                : std::nullopt;
    if (!Bound)
      return QualType();
    auto Of = [&](QualType Type) -> QualType {
      const auto *Parameter = dyn_cast<TemplateTypeParmType>(
          Type.getCanonicalType().getUnqualifiedType());
      if (!Parameter || Parameter->getDepth() != ParameterDepth)
        return QualType();
      if (Ambiguous.contains(Parameter->getIndex())) {
        WrittenAmbiguously = true;
        return QualType();
      }
      const auto Found = Bound->find(Parameter->getIndex());
      return Found == Bound->end() ? QualType() : Found->second;
    };
    InPattern = InPattern.getNonReferenceType();
    const bool Pointer = InPattern->isPointerType();
    if (Pointer)
      InPattern = InPattern->getPointeeType();
    QualType Made = Of(InPattern);
    // A specialization given the parameters, `TCAwaiter<t_CType>`.
    if (const auto *Specialization =
            InPattern->getAs<TemplateSpecializationType>();
        Made.isNull() && Specialization && !Specialization->isTypeAlias()) {
      llvm::SmallVector<TemplateArgument, 2> Arguments;
      llvm::SmallVector<TemplateArgument, 2> Canonical;
      for (const TemplateArgument &Argument :
           Specialization->template_arguments()) {
        TemplateArgument Given = Argument;
        if (Argument.getKind() == TemplateArgument::Type) {
          if (const QualType Substituted = Of(Argument.getAsType());
              !Substituted.isNull())
            Given = TemplateArgument(Substituted);
          else if (Argument.getAsType()->isDependentType())
            return QualType();
        } else if (Argument.isDependent())
          return QualType();
        Arguments.push_back(Given);
        Canonical.push_back(
            Result.Context->getCanonicalTemplateArgument(Given));
      }
      QualType Instantiated = Actual.getNonReferenceType();
      if (Pointer && Instantiated->isPointerType())
        Instantiated = Instantiated->getPointeeType();
      // Only what names what clang instantiated: each argument, as canonical,
      // the one of the specialization it is, which then is its canonical type.
      const auto *Record = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
          Instantiated->getAsCXXRecordDecl());
      const TemplateDecl *Named =
          Specialization->getTemplateName().getAsTemplateDecl();
      if (!Record || !Named ||
          Record->getSpecializedTemplate()->getCanonicalDecl() !=
              Named->getCanonicalDecl() ||
          Record->getTemplateArgs().size() < Canonical.size() ||
          llvm::any_of(llvm::enumerate(Canonical), [&](const auto &Each) {
            return !Each.value().structurallyEquals(
                Record->getTemplateArgs()[Each.index()]);
          }))
        return QualType();
      Made = Result.Context->getTemplateSpecializationType(
          ElaboratedTypeKeyword::None, Specialization->getTemplateName(),
          Arguments, Canonical, Instantiated.getCanonicalType());
    }
    if (Made.isNull())
      return QualType();
    return Pointer ? Result.Context->getPointerType(Made) : Made;
  };
  WrittenType = [&](const Expr *E, int Depth) -> QualType {
    if (!E || Depth > 16)
      return QualType();
    E = E->IgnoreImplicit()->IgnoreParenImpCasts();
    if (const auto *Copy = dyn_cast<CXXConstructExpr>(E);
        Copy && Copy->getNumArgs() == 1 &&
        Copy->getConstructor()->isCopyOrMoveConstructor())
      return WrittenType(Copy->getArg(0), Depth + 1);
    // Of a variable as it is written, also of what a hidden one of a range
    // loop, `__begin1`, or a deduced one is made of.
    if (const auto *Ref = dyn_cast<DeclRefExpr>(E)) {
      const auto *Source = dyn_cast<VarDecl>(Ref->getDecl());
      if (!Source || Source == Declaration)
        return QualType();
      if (Source->hasLocalStorage() && Source->getInit() &&
          (Source->isImplicit() || Source->getType()->getContainedAutoType()))
        return WrittenType(Source->getInit(), Depth + 1);
      return Source->getType();
    }
    if (const auto *Await = dyn_cast<CoawaitExpr>(E))
      return WrittenType(Await->getResumeExpr(), Depth + 1);
    // A member of what a class template is given, `_Entry.m_Value` of
    // `t_CValue &m_Value`, as its pattern declares it.
    if (const auto *Member = dyn_cast<MemberExpr>(E))
      if (const auto *Field = dyn_cast<FieldDecl>(Member->getMemberDecl()))
        if (const auto *Class =
                dyn_cast<ClassTemplateSpecializationDecl>(Field->getParent()))
          if (const CXXRecordDecl *Pattern =
                  Class->getTemplateInstantiationPattern())
            for (const NamedDecl *Found : Pattern->lookup(Field->getDeclName()))
              if (const auto *Declared = dyn_cast<FieldDecl>(Found))
                return OfMember(Member->getBase(), Member->isArrow(), Class,
                                Declared->getType(), Member->getType(),
                                Depth);
    // The awaiter that `co_await` holds, as what it is made of is written,
    // `TCAwaiter<CActorSubscription>{}`.
    if (const auto *Opaque = dyn_cast<OpaqueValueExpr>(E);
        Opaque && Opaque->getSourceExpr()) {
      const QualType Source = WrittenType(Opaque->getSourceExpr(), Depth + 1);
      return Source.isNull() ? Opaque->getSourceExpr()->getType() : Source;
    }
    if (const auto *Conditional = dyn_cast<ConditionalOperator>(E)) {
      const QualType True = WrittenType(Conditional->getTrueExpr(), Depth + 1);
      const QualType False = WrittenType(Conditional->getFalseExpr(), Depth + 1);
      return !True.isNull() && !False.isNull() &&
                     True.getNonReferenceType().getUnqualifiedType() ==
                         False.getNonReferenceType().getUnqualifiedType()
                 ? True
                 : QualType();
    }
    if (const auto *Deref = dyn_cast<UnaryOperator>(E);
        Deref && Deref->getOpcode() == UO_Deref) {
      const QualType Pointer = WrittenType(Deref->getSubExpr(), Depth + 1);
      return !Pointer.isNull() && Pointer->isPointerType()
                 ? Pointer->getPointeeType()
                 : QualType();
    }
    const auto *Call = dyn_cast<CallExpr>(E);
    if (!Call)
      return QualType();
    // Moved, forwarded or copied, `fg_Move(_First)`.
    if (const FunctionDecl *Callee = Call->getDirectCallee();
        Callee && Callee->getIdentifier() && Call->getNumArgs() == 1 &&
        !isa<CXXMethodDecl>(Callee) &&
        (isMoveOrForwardFunction(identifierTextOf(Callee)) ||
         isCopyFunction(identifierTextOf(Callee))))
      return WrittenType(Call->getArg(0), Depth + 1);
    // What gives back what it is given as it is, `await_transform(
    // _Awaitable)` of `tf_CAwaitable &&await_transform(tf_CAwaitable &&)`.
    if (const FunctionDecl *Callee = Call->getDirectCallee();
        Callee && Callee->getPrimaryTemplate() &&
        Callee->getTemplateInstantiationPattern()) {
      const FunctionDecl *Generic = Callee->getTemplateInstantiationPattern();
      const auto *Returned = dyn_cast<TemplateTypeParmType>(
          Generic->getReturnType()
              .getNonReferenceType()
              .getCanonicalType()
              .getUnqualifiedType());
      const unsigned Offset = argumentShift(Call, Callee);
      const FunctionTemplateDecl *Described =
          Generic->getDescribedFunctionTemplate();
      if (Returned && Described &&
          Returned->getDepth() ==
              Described->getTemplateParameters()->getDepth())
        for (unsigned iParam = 0; iParam < Generic->getNumParams() &&
                                  iParam + Offset < Call->getNumArgs();
             ++iParam)
          if (Generic->getParamDecl(iParam)
                  ->getType()
                  .getNonReferenceType()
                  .getCanonicalType()
                  .getUnqualifiedType()
                  .getTypePtr() == Returned) {
            const Expr *Given = Call->getArg(iParam + Offset);
            const QualType Of = WrittenType(Given, Depth + 1);
            return Of.isNull() ? Given->IgnoreImplicit()->getType() : Of;
          }
    }
    const auto *Method = dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee());
    const Expr *ObjectOf = nullptr;
    if (const CMemberCall Member = memberCallOf(Call))
      ObjectOf = Member->getImplicitObjectArgument();
    else if (const auto *Operator = dyn_cast<CXXOperatorCallExpr>(Call);
             Operator && Method && Method->isImplicitObjectMemberFunction() &&
             Operator->getNumArgs() >= 1)
      ObjectOf = Operator->getArg(0);
    const FunctionDecl *Pattern =
        Method ? Method->getTemplateInstantiationPattern() : nullptr;
    if (!ObjectOf || !Pattern)
      return QualType();
    const bool Arrow = isa<MemberExpr>(Call->getCallee()) &&
                       cast<MemberExpr>(Call->getCallee())->isArrow();
    return OfMember(ObjectOf, Arrow, Method->getParent(),
                    Pattern->getReturnType(), Call->getType(), Depth);
  };
  // Also a structured binding of a member of one, `auto &[Key, Subscription]
  // = _Pair` of `TCPair<CActorSubscription>`.
  const Expr *Deduced = nullptr;
  if (const auto *Variable = dyn_cast<VarDecl>(Declaration);
      Variable && Variable->getInit() &&
      PastPack(Type)->getContainedAutoType())
    Deduced = Variable->getInit();
  else if (const auto *Binding = dyn_cast<BindingDecl>(Declaration))
    Deduced = Binding->getBinding();
  // What a deduced one is in an instantiation, also as the handle alias its
  // type is, `auto Subscription = fg_Make<tf_CType>()`.
  auto InstanceAs = [&](QualType Instance) {
    if (Deduced && !Instance->isDependentType())
      if (const QualType Handle = handleAliasOf(Instance, *Result.Context);
          !Handle.isNull())
        return Handle;
    return Instance;
  };
  // What it is written as, where that is known, which a contradicting concept
  // is told by, `pSubscription` of a handle.
  QualType WrittenAs;
  if (!NamedAsSource && Deduced && !Type->isDependentType()) {
    WrittenAmbiguously = false;
    const QualType Spelled = WrittenType(Deduced, 0);
    // A class, which an alias can make a handle or a smart pointer, written
    // as an argument bound ambiguously: every prefix of what it can be is
    // allowed and none is required (decided Q9).
    if (WrittenAmbiguously && Type.getNonReferenceType()->isRecordType())
      return;
    if (!Spelled.isNull() && !isa<TemplateTypeParmType>(Spelled.getTypePtr()) &&
        Result.Context->hasSameUnqualifiedType(
            Spelled.getNonReferenceType(),
            PastPack(Type).getNonReferenceType().getUnqualifiedType())) {
      Concept = expectedConcept(Spelled, *Result.Context).Required;
      WrittenAs = Spelled;
      NamedAsSource = true;
    }
  }
  // Or the handle alias it is, when what it is deduced from can't tell,
  // `_fMake()` of `TCFunction<CActorSubscription ()>` through a base, also
  // where that spells the smart pointer, `auto &Subscription = _pOriginal`.
  if (Deduced && !Type->isDependentType())
    if (const QualType Handle = handleAliasOf(Type, *Result.Context);
        !Handle.isNull()) {
      Concept = expectedConcept(Handle, *Result.Context).Required;
      WrittenAs = Handle;
      NamedAsSource = true;
    }
  // A deduced one in a template, of the type of what it is made of there,
  // where no argument can change what that is, `auto pValue =
  // static_cast<tf_CType *>(nullptr)`; not of one an argument can change,
  // `typename tf_CTraits::CPointer` or what a call that depends on them gives,
  // which takes no concept prefix that its instantiations would require.
  // Also of pointer arithmetic, whose type is dependent there, `auto pEnd =
  // _pDest - 1` of `tf_CData *_pDest`, but which steps the pointer it is
  // given, of a type no argument can change.
  if (!Concept && !NamedAsSource && Deduced &&
      PastPack(Type).getNonReferenceType()->isUndeducedAutoType())
    if (const QualType Made = Deduced->getType(); !Made.isNull()) {
      if (!Made->isSpecificBuiltinType(BuiltinType::Dependent))
        Concept = expectedConcept(Made, *Result.Context).Required;
      else if (const QualType Stepped = steppedPointer(Deduced);
               !Stepped.isNull())
        Concept = expectedConcept(Stepped, *Result.Context).Required;
    }
  // What the instantiations below make it is only told of a name with a
  // concept prefix, which it can contradict (Observed).
  const bool ConceptNamed = Rest.size() >= 2 &&
                            ConceptPrefixes.contains(Rest[0]) &&
                            (llvm::isUpper(Rest[1]) || llvm::isDigit(Rest[1]));
  // So is a local or a parameter of a type that depends on the parameters of
  // a function template, `typename tf_CTraits::CPointer _pValue`, also of a
  // class template given them, `TCDeferred<tf_CResult> _fCallback`, and one
  // of a parameter as it is, `tf_CMemberFunction _pFunction`, whose
  // instantiations tell what it is, like a function object through a base
  // that depends on them. One deduced from what depends on them, `auto Value
  // = static_cast<tf_CType *>(nullptr)`, takes no concept prefix that they
  // would require, which can be mutually exclusive, only none that each of
  // them excludes.
  // Also each of a pack of init captures, `[...p_Values = p_pValues]`, whose
  // type is what each element is deduced as.
  const QualType Unexpanded =
      isa<PackExpansionType>(Type.getNonReferenceType())
          ? cast<PackExpansionType>(Type.getNonReferenceType())->getPattern()
          : Type;
  if (const auto *Variable = dyn_cast<VarDecl>(Declaration);
      ConceptNamed && (!Concept || FunctionPointerNamed) && !NamedAsSource &&
      Variable &&
      ((Variable->isLocalVarDecl() &&
        Unexpanded.getNonReferenceType()->isUndeducedAutoType()) ||
       ((Variable->isLocalVarDecl() || isa<ParmVarDecl>(Variable)) &&
        (DependentMember || DependentSpecialization ||
         Unexpanded.getNonReferenceType()->getAs<TemplateTypeParmType>() ||
         ForwardsParameter || FunctionPointerNamed)))) {
    llvm::SmallVector<CObserved, 4> Instances;
    for (const Decl *Instance : localInstances(Variable))
      Instances.push_back({isa<ParmVarDecl>(Instance)
                               ? cast<ParmVarDecl>(Instance)->getOriginalType()
                               : cast<VarDecl>(Instance)->getType(),
                           Instance});
    // Also one of a function type written in the type of a declaration,
    // which has no function to be instantiated with, also of an alias
    // template.
    if (const auto *Parameter = dyn_cast<ParmVarDecl>(Variable);
        Parameter && Instances.empty())
      for (const CMadeParameterType &Made :
           functionTypeParameterTypes(Parameter, *Result.Context))
        Instances.push_back({Made.Type, Made.Instance, Made.Use, Made.Alias});
    for (const CObserved &Instance : Instances) {
      if (Instance.Type->isUndeducedAutoType() ||
          Instance.Type->isDependentType())
        continue;
      Observed.push_back(Instance);
    }
  }
  // So is a structured binding of a decomposition in a function template,
  // `auto [First, Second] = TCPair<tf_CType *>{...}`, which has the types of
  // the bindings at its place in the instantiations.
  // Not one of a member that is a parameter of a class template given a
  // parameter as, `auto const &[Key, Value] = _Entry` of `TCEntry<tf_CValue>
  // const &_Entry` with `t_CValue m_Value;`, which is generic like it.
  if (const auto *Binding = dyn_cast<BindingDecl>(Declaration);
      ConceptNamed && !Concept && Binding && Type->isDependentType())
    if (const auto *Decomposition =
            dyn_cast_or_null<DecompositionDecl>(Binding->getDecomposedDecl());
        Decomposition) {
      const auto Bindings = Decomposition->bindings();
      const auto Position = llvm::find(Bindings, Binding);
      const size_t iBinding = Position - Bindings.begin();
      for (const Decl *Instance : localInstances(Decomposition)) {
        const auto *InstanceDecomposition = dyn_cast<DecompositionDecl>(Instance);
        if (!InstanceDecomposition || Position == Bindings.end() ||
            iBinding >= InstanceDecomposition->bindings().size())
          continue;
        const QualType InstanceType =
            InstanceDecomposition->bindings()[iBinding]->getType();
        if (InstanceType.isNull() || InstanceType->isDependentType())
          continue;
        // Which also tells a concept it can't be, `fValue` of an `int`.
        Observed.push_back({InstanceType, Instance});
      }
    }
  // So is a member of a class template of such a type, `typename
  // t_CTraits::CPointer m_pValue`, also of a class template given them,
  // `TCStorage<t_FSignature> m_fCallback`, of a parameter as it is,
  // `t_CFunctionMemberPointer mp_pFunction`, or deduced from what depends on
  // them, `static inline auto ms_Value = static_cast<t_CType *>(nullptr)`.
  if (ConceptNamed && (!Concept || FunctionPointerNamed) &&
      (DependentMember || DependentSpecialization || OfParameter ||
       Type->isUndeducedAutoType() || FunctionPointerNamed) &&
      (isa<FieldDecl>(Declaration) ||
       (isa<VarDecl>(Declaration) &&
        cast<VarDecl>(Declaration)->isStaticDataMember())))
    if (const auto *Class =
            dyn_cast<CXXRecordDecl>(Declaration->getDeclContext())) {
      for (const CXXRecordDecl *Instance : instancesOf(Class)) {
        if (!Instance->hasDefinition())
          continue;
        for (const NamedDecl *Found : Instance->lookup(Declaration->getDeclName())) {
          const auto *Member = dyn_cast<ValueDecl>(Found);
          if (!Member || !isa<FieldDecl, VarDecl>(Member) ||
              Member->getType()->isDependentType() ||
              Member->getType()->isUndeducedAutoType())
            continue;
          Observed.push_back({Member->getType(), Member});
        }
      }
    }
  // So is a non-type template parameter of such a type, `typename
  // t_CTraits::CBool t_bFlag`, which has the type of its argument in each
  // instantiation of its template, also of a partial specialization, and of
  // each argument of a pack, `typename t_CTraits::CBool ...tp_bFlags`, also
  // one of a parameter as it is, `t_CType t_pValue`, and one whose type is
  // deduced from it, `auto t_fDeleter`. An alias template or a concept keeps
  // none of its uses to tell it by.
  if (const auto *Parameter = dyn_cast<NonTypeTemplateParmDecl>(Declaration);
      ConceptNamed && (!Concept || FunctionPointerNamed) && Parameter &&
      (DependentMember || OfParameter ||
       Parameter->getType()->getContainedAutoType() || FunctionPointerNamed)) {
    TCRecursive<void(const TemplateArgument &, const Decl *)> AddArgument =
        [&](const TemplateArgument &Argument, const Decl *Instance) {
          QualType ArgumentType;
          switch (Argument.getKind()) {
          case TemplateArgument::Pack:
            for (const TemplateArgument &Element : Argument.pack_elements())
              AddArgument(Element, Instance);
            return;
          case TemplateArgument::Integral:
            ArgumentType = Argument.getIntegralType();
            break;
          case TemplateArgument::Declaration:
            ArgumentType = Argument.getParamTypeForDecl();
            break;
          case TemplateArgument::NullPtr:
            ArgumentType = Argument.getNullPtrType();
            break;
          case TemplateArgument::StructuralValue:
            ArgumentType = Argument.getStructuralValueType();
            break;
          default:
            return;
          }
          if (ArgumentType.isNull() || ArgumentType->isDependentType())
            return;
          Observed.push_back({ArgumentType, Instance});
        };
    auto Add = [&](llvm::ArrayRef<TemplateArgument> Arguments,
                   const Decl *Instance) {
      if (Parameter->getIndex() < Arguments.size())
        AddArgument(Arguments[Parameter->getIndex()], Instance);
    };
    const DeclContext *Owner = Parameter->getDeclContext();
    // A parameter of the class, not of a member template of it, `template
    // <typename t_CTraits::CPointer t_pAddress> static constexpr int
    // mc_Value`, whose context is the class too.
    auto OfClass = [&](const CXXRecordDecl *Class) {
      const TemplateParameterList *List = nullptr;
      if (const ClassTemplateDecl *Template =
              Class->getDescribedClassTemplate())
        List = Template->getTemplateParameters();
      else if (const auto *Partial =
                   dyn_cast<ClassTemplatePartialSpecializationDecl>(Class))
        List = Partial->getTemplateParameters();
      return List && llvm::is_contained(*List, Parameter);
    };
    // Of a variable template, which isn't a context of its own: the one in
    // \p In whose parameters it is, also a partial specialization of one, of
    // its instantiations, with what they give its parameters, also those of
    // the member templates it is instantiated as in the instantiations of a
    // class template, \p Instances.
    auto AddVariableTemplates = [&](const DeclContext *In,
                                    llvm::ArrayRef<const CXXRecordDecl *>
                                        Instances) {
      // Of those clang instantiated from \p Pattern (isInstanceOf), not an
      // explicit specialization, nor an instance of a partial
      // specialization, which has parameters of its own.
      auto AddOf = [&](const VarTemplateDecl *Variable,
                       const VarTemplateDecl *Pattern) {
        for (const VarTemplateSpecializationDecl *Instance :
             Variable->specializations())
          if (isInstanceOf(Instance, Pattern->getTemplatedDecl()))
            Add(Instance->getTemplateArgs().asArray(), Instance);
      };
      for (const Decl *Member : In->decls()) {
        if (const auto *Variable = dyn_cast<VarTemplateDecl>(Member);
            Variable &&
            llvm::is_contained(Variable->getTemplateParameters()->asArray(),
                               Parameter)) {
          AddOf(Variable, Variable);
          for (const CXXRecordDecl *Instance : Instances)
            for (const Decl *InstanceMember : Instance->decls())
              if (const auto *Instantiated =
                      dyn_cast<VarTemplateDecl>(InstanceMember);
                  Instantiated &&
                  Instantiated->getInstantiatedFromMemberTemplate() &&
                  Instantiated->getInstantiatedFromMemberTemplate()
                          ->getCanonicalDecl() == Variable->getCanonicalDecl())
                AddOf(Instantiated, Variable);
          return;
        }
        if (const auto *Partial =
                dyn_cast<VarTemplatePartialSpecializationDecl>(Member);
            Partial &&
            llvm::is_contained(Partial->getTemplateParameters()->asArray(),
                               Parameter)) {
          for (const VarTemplateSpecializationDecl *Instance :
               Partial->getSpecializedTemplate()->specializations())
            if (isInstanceOf(Instance, Partial))
              Add(Instance->getTemplateInstantiationArgs().asArray(), Instance);
          return;
        }
      }
    };
    // A parameter of a class template repeated for a member defined out of
    // its class, `template <auto t_fDelete> void TCBox<t_fDelete>::f_Run()`,
    // is of the instantiations of that class.
    // The member whose declaration has it.
    const DeclaratorDecl *Member = nullptr;
    auto HasOuter = [&](const Decl *Candidate) {
      if (const auto *Template = dyn_cast_or_null<TemplateDecl>(Candidate))
        Candidate = Template->getTemplatedDecl();
      const auto *Declarator = dyn_cast_or_null<DeclaratorDecl>(Candidate);
      if (Declarator &&
          llvm::any_of(Declarator->getTemplateParameterLists(),
                       [&](const TemplateParameterList *List) {
                         return llvm::is_contained(List->asArray(), Parameter);
                       }))
        Member = Declarator;
      return Member != nullptr;
    };
    if (!HasOuter(dyn_cast<Decl>(Owner)))
      for (const DynTypedNode &Parent :
           Result.Context->getParentMapContext().getParents(*Parameter))
        if (HasOuter(Parent.get<Decl>()))
          break;
    if (Member) {
      for (const DeclContext *Context = Member->getDeclContext(); Context;
           Context = Context->getParent())
        if (const auto *Class = dyn_cast<CXXRecordDecl>(Context))
          if (const ClassTemplateDecl *Template =
                  Class->getDescribedClassTemplate();
              Template && Template->getTemplateParameters()->getDepth() ==
                              Parameter->getDepth()) {
            for (const CXXRecordDecl *Instance : instancesOf(Class))
              if (const auto *Specialization =
                      dyn_cast<ClassTemplateSpecializationDecl>(Instance))
                Add(Specialization->getTemplateInstantiationArgs().asArray(), Instance);
            break;
          }
    }
    if (const auto *Class = dyn_cast<CXXRecordDecl>(Owner)) {
      if (OfClass(Class)) {
        for (const CXXRecordDecl *Instance : instancesOf(Class))
          if (const auto *Specialization =
                  dyn_cast<ClassTemplateSpecializationDecl>(Instance))
            Add(Specialization->getTemplateInstantiationArgs().asArray(), Instance);
      } else {
        // Of a member variable template of it, `template <typename
        // t_CTraits::CBool t_Flag> static constexpr int mc_Value`.
        llvm::SmallVector<const CXXRecordDecl *, 4> Instances;
        if (Class->isDependentContext())
          llvm::append_range(Instances, instancesOf(Class));
        AddVariableTemplates(Class, Instances);
      }
    } else if (const auto *Function = dyn_cast<FunctionDecl>(Owner)) {
      if (const FunctionTemplateDecl *Template =
              Function->getDescribedFunctionTemplate()) {
        // Of those clang instantiated from it (isInstanceOf), not an
        // explicit specialization, which is given its arguments as written.
        auto AddOf = [&](const FunctionTemplateDecl *Of) {
          for (const FunctionDecl *Instance : Of->specializations())
            if (const TemplateArgumentList *Arguments =
                    Instance->getTemplateSpecializationArgs();
                Arguments && isInstanceOf(Instance, Function))
              Add(Arguments->asArray(), Instance);
        };
        AddOf(Template);
        // A generic lambda in a template, `[]<typename tf_CTraits::CBool
        // tf_Flag>() {}`, of the specializations of the call operator of each
        // lambda the instantiations of the template make, also for the static
        // function that its conversion to a pointer to a function gives,
        // whose parameters are those of the call operator.
        const auto *Method = dyn_cast<CXXMethodDecl>(Function);
        if (const CXXMethodDecl *Operator =
                Method && Method->getParent()->isLambda()
                    ? Method->getParent()->getLambdaCallOperator()
                    : nullptr)
          for (const FunctionDecl *Instance : functionInstances(Operator))
            if (const TemplateArgumentList *Arguments =
                    Instance->getTemplateSpecializationArgs())
              Add(Arguments->asArray(), Instance);
        // A member template of a class template, `template <typename
        // t_CTraits::CBool tf_Flag> static void fs_Run()`, is instantiated
        // with each instantiation of the class, whose member template has
        // the specializations.
        // Also a friend defined in it, `template <...> friend void
        // fg_Run(TCBox _Box) {}`, which its instantiations declare as friends.
        const auto *Class = dyn_cast<CXXRecordDecl>(Function->getParent());
        if (!Class)
          Class = dyn_cast<CXXRecordDecl>(Function->getLexicalParent());
        if (Class && Class->isDependentContext())
          for (const CXXRecordDecl *Instance : instancesOf(Class))
            for (const Decl *Member : Instance->decls())
              if (const auto *Instantiated =
                      dyn_cast_or_null<FunctionTemplateDecl>(
                          isa<FriendDecl>(Member)
                              ? cast<FriendDecl>(Member)->getFriendDecl()
                              : Member);
                  Instantiated &&
                  Instantiated->getInstantiatedFromMemberTemplate() &&
                  Instantiated->getInstantiatedFromMemberTemplate()
                          ->getCanonicalDecl() == Template->getCanonicalDecl())
                AddOf(Instantiated);
      }
    } else
      AddVariableTemplates(Owner, {});
  }
  // Named as a function object, `_fValue`, with a type that is a template
  // parameter, `tf_CType &&`, which then is one too: it is named as a
  // function type, `tf_FValue`.
  if (!Concept && Rest.size() >= 2 && Rest[0] == 'f' &&
      (llvm::isUpper(Rest[1]) || llvm::isDigit(Rest[1])))
    // Not one invented for `auto`, which has no name to give the prefix.
    if (const TemplateTypeParmType *Parameter = writtenTemplateParameter(Type);
        Parameter && Parameter->getIdentifier() &&
        !(Parameter->getDecl() && Parameter->getDecl()->isImplicit())) {
      const SourceLocation Loc = nameLocation(Declaration, SM);
      if (Loc.isValid())
        diag(Loc, "%0 is named as a function object, so its type %1 should "
                  "be named as a function type, with the 'F' type prefix")
            << diagnosticName(Declaration) << Parameter->getIdentifier();
      return;
    }
  // Nor a concept that the type, which it doesn't depend on, can't be, like
  // a function object of a number, `double g_fRatio`, or a pointer of one,
  // `int g_pCount`, also a boolean of a floating point number.
  // Also deduced, `auto fRatio = 0.5`, and in a template, of what it is in
  // any instantiation where the template doesn't tell, `auto pCount =
  // fg_Count<tf_CType>()`: a prefix is of what it is in each, which an
  // instantiation that it doesn't fit shows it isn't, while no prefix fits
  // any.
  // Nor any concept of what an instantiation makes an array, `typename
  // t_CTraits::CStorage m_pBuffer` of `using CStorage = uint8[8]`, other than
  // the range it is, `tf_CRange &_rValues` given an `int[4]`.
  if (!Concept && !Observed.empty() && Rest.size() >= 2 &&
      ConceptPrefixes.contains(Rest[0]) && Rest[0] != RangePrefix &&
      (llvm::isUpper(Rest[1]) || llvm::isDigit(Rest[1])) &&
      llvm::any_of(Observed, [](const CObserved &Of) {
        return Of.Type.getNonReferenceType()->isArrayType();
      })) {
    // Named by the instantiation that makes it one.
    const auto Array = llvm::find_if(Observed, [](const CObserved &Of) {
      return Of.Type.getNonReferenceType()->isArrayType();
    });
    if (const SourceLocation Loc = nameLocation(Declaration, SM);
        Loc.isValid()) {
      {
        const InstantiationScope Scope(*this, Array->Instance);
        diag(Loc, "%0 is an array, which has no concept prefix")
            << diagnosticName(Declaration);
      }
      noteAliasUse(Array->Use, Array->Alias);
    }
    return;
  }
  // Of what an instantiation makes it, \p Generic: a pointer is also an
  // iterator there, `tf_CIterator _iBegin` given an `int *`, which generic code
  // walks like any other.
  // A member that takes no space of its own where it is empty,
  // `[[no_unique_address]]`, which in generic code is what fits its concept
  // or an empty stand-in, `TCConditional<t_bStore, CStore *, CStoreEmpty>
  // m_pStore`.
  const auto *Field = dyn_cast<FieldDecl>(Declaration);
  const bool EmptyAllowed = Field && Field->hasAttr<NoUniqueAddressAttr>();
  bool OpaqueHandle = false;
  auto Contradicts = [&](QualType Of, bool Generic = false) {
    Of = InstanceAs(Of);
    // A null pointer an instantiation makes of it, `nullptr` given to `auto
    // t_fCallback` or a dependent type made `std::nullptr_t`, is the empty
    // value of a pointer and of a function pointer, which both prefixes fit.
    if (Generic && (Rest[0] == 'p' || Rest[0] == 'f') &&
        Of.getNonReferenceType()->isNullPtrType())
      return false;
    const CConcepts Concepts = expectedConcept(Of, *Result.Context);
    const std::optional<char> Expected = Concepts.Required;
    // An opaque handle, `pthread_t`, also one a value wrapper holds,
    // `TCAtomic<pthread_t>`, takes no concept prefix, on any target, whatever
    // it is on this one.
    if (llvm::is_contained({'b', 'f', 'i', 'p'}, Rest[0]) && Concepts.Opaque) {
      OpaqueHandle = true;
      return true;
    }
    if (const QualType Pointer = Of.getNonReferenceType().getCanonicalType();
        Generic && Rest[0] == 'i' && Pointer->isPointerType() &&
        !Pointer->isFunctionPointerType())
      return false;
    if (const CXXRecordDecl *Stand = Of->getAsCXXRecordDecl();
        Generic && EmptyAllowed && Stand && Stand->hasDefinition() &&
        Stand->isEmpty())
      return false;
    // Not of what is the concept, like a listed function object template,
    // `TCFunction<void (bool)>`, whatever it declares, nor of what it can be
    // where that can't be told: only what is known not to apply is reported.
    if (Expected == std::optional<char>(Rest[0]) || Concepts.mayBe(Rest[0]))
      return false;
    // But of another concept, like a pointer, `int const *`, named as an
    // iterator or a function object, as where it doesn't depend on
    // parameters: an iterator that is a range is named for the range, and a
    // pointer to what is named as a function type can be either.
    if (Expected && !(*Expected == 'i' && Rest[0] == 'r') &&
        !(*Expected == 'x' && (Rest[0] == 'f' || Rest[0] == 'p')))
      return true;
    const QualType Canonical =
        Of.getNonReferenceType().getCanonicalType().getUnqualifiedType();
    // The name of one of Malterlib's built-in types that \p Type is named
    // as, BuiltinTypes, some of which are classes, like `fp64` of
    // `TCFloat<...>`.
    // What each level of sugar is named as is kept (BuiltinNames), so the
    // types along one chain of aliases go through each alias once.
    auto BuiltinNameOf = [&](QualType Type) {
      llvm::SmallVector<void *, 4> Levels;
      StringRef Name;
      for (QualType Named = Type.getNonReferenceType(); !Named.isNull();) {
        if (const auto Found = BuiltinNames.find(Named.getAsOpaquePtr());
            Found != BuiltinNames.end()) {
          Name = Found->second;
          break;
        }
        Levels.push_back(Named.getAsOpaquePtr());
        if (const auto *Typedef = dyn_cast<TypedefType>(Named.getTypePtr());
            Typedef && Typedef->getDecl()->getIdentifier() &&
            isBuiltinTypeName(identifierTextOf(Typedef->getDecl()))) {
          Name = identifierTextOf(Typedef->getDecl());
          break;
        }
        const QualType Next =
            Named.getSingleStepDesugaredType(*Result.Context);
        if (Next == Named)
          break;
        Named = Next;
      }
      for (void *Level : Levels)
        BuiltinNames[Level] = Name;
      return Name;
    };
    // Named as a floating point number, `fp64`, also unsigned or cleared,
    // `ufp64`, `zfp64` or `zufp64`.
    auto Floating = [](StringRef Builtin) {
      const size_t At = Builtin.find("fp");
      return At != StringRef::npos && Builtin.size() > At + 2 &&
             llvm::all_of(Builtin.take_front(At), llvm::isLower) &&
             llvm::all_of(Builtin.drop_front(At + 2), llvm::isDigit);
    };
    const StringRef BuiltinName = BuiltinNameOf(Of);
    const bool Number = Canonical->isArithmeticType() ||
                        Canonical->isEnumeralType() ||
                        (!BuiltinName.empty() && Canonical->isRecordType());
    // An integer, a boolean or an enumeration, also one of the built-in types
    // that is a class, like `int128`, not `fp64`, and one of ValueWrapperTypes
    // that wraps one, `TCAtomic<umint>`: a set of flags or an index.
    TCRecursive<bool(QualType, int)> IntegerLike = [&](QualType Type,
                                                          int Depth) {
      if (Type.isNull() || Depth > 8)
        return false;
      const QualType Value =
          Type.getNonReferenceType().getCanonicalType().getUnqualifiedType();
      if (Value->isIntegralOrEnumerationType())
        return true;
      if (const StringRef Builtin = BuiltinNameOf(Type);
          !Builtin.empty() && Value->isRecordType())
        return !Floating(Builtin);
      const auto *Wrapper = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
          Value->getAsCXXRecordDecl());
      return Wrapper && isValueWrapper(Wrapper) &&
             Wrapper->getTemplateArgs().size() >= 1 &&
             Wrapper->getTemplateArgs()[0].getKind() ==
                 TemplateArgument::Type &&
             IntegerLike(Wrapper->getTemplateArgs()[0].getAsType(), Depth + 1);
    };
    const CXXRecordDecl *Record = Canonical->getAsCXXRecordDecl();
    const bool NotCallable = Record && Record->hasDefinition() &&
                             !possiblyTrue(declaresCallable(Record));
    // A boolean is an integer, a boolean, or a wrapper of one, a set of
    // flags, and an iterator one of IteratorTypes, a pointer, also only in
    // generic code above, or an index, an integer; a pointer is a pointer or
    // one of SmartPointerTypes, of which that it is the concept is told above,
    // not a handle, a smart pointer through an alias of
    // SmartPointerHandleTypes, `COnScopeExitShared`, which takes none.
    // Nor of a class whose bases can't be told, not defined in the
    // translation unit or with one that depends on parameters, which can
    // make it what the prefix is for: unknown, so allowed.
    if (Record && llvm::is_contained({'b', 'i', 'p'}, Rest[0]) &&
        hasUnknownBases(Record))
      return false;
    switch (Rest[0]) {
    case 'b':
      // Also a class that is a flag, FlagTypes, like `CAtomicFlag`.
      return !IntegerLike(Of, 0) &&
             !(Record && possiblyTrue(isRecordInList(Record, FlagTypes)));
    case 'i':
      // An index; one that can be an iterator is allowed above (MayBe).
      return !IntegerLike(Of, 0);
    case 'f':
      return Number || NotCallable;
    case 'p':
      // One that can be a smart pointer or a handle, which can be named
      // either way, is allowed above (MayBe).
      return true;
    default:
      return false;
    }
  };
  if (!Concept && Rest.size() >= 2 &&
      (llvm::isUpper(Rest[1]) || llvm::isDigit(Rest[1])) &&
      llvm::is_contained({'b', 'f', 'i', 'p'}, Rest[0])) {
    bool Known = !Type->isDependentType() && !Type->isUndeducedAutoType();
    bool Any = Known && Contradicts(WrittenAs.isNull() ? Type : WrittenAs);
    // The instantiation that contradicts it, which the report names.
    const Decl *Contradicting = nullptr;
    const CObserved *ContradictingUse = nullptr;
    if (!Known && !Observed.empty()) {
      const auto Found = llvm::find_if(Observed, [&](const CObserved &Of) {
        return !Of.Type->isDependentType() && !Of.Type->isUndeducedAutoType() &&
               Contradicts(Of.Type, /*Generic=*/true);
      });
      Known = Found != Observed.end();
      if (Known) {
        Contradicting = Found->Instance;
        ContradictingUse = &*Found;
      }
      Any = Known;
    }
    // Also a deduced local, of what an instantiation makes it, also one made
    // of a value of a parameter, which is wrong where it is in one.
    const auto *Local = dyn_cast<VarDecl>(Declaration);
    if (!Known && Local && Local->isLocalVarDecl() &&
        Type.getNonReferenceType()->isUndeducedAutoType())
      for (const Decl *Instance : localInstances(Declaration)) {
        const QualType Made = cast<VarDecl>(Instance)->getType();
        if (!Made->isDependentType() && !Made->isUndeducedAutoType() &&
            Contradicts(Made, /*Generic=*/true)) {
          Known = Any = true;
          Contradicting = Instance;
          break;
        }
      }
    if (Known && Any) {
      const SourceLocation Loc = nameLocation(Declaration, SM);
      {
        const InstantiationScope Scope(*this, Contradicting);
        if (Loc.isValid() && OpaqueHandle)
          diag(Loc, "%0 has the '%1' concept prefix of %2, but is an opaque "
                    "handle, which takes none, as it isn't one on every target")
              << diagnosticName(Declaration) << std::string(1, Rest[0])
              << conceptName(Rest[0]);
        else if (Loc.isValid())
          diag(Loc, "%0 has the '%1' concept prefix of %2, but is not one")
              << diagnosticName(Declaration) << std::string(1, Rest[0])
              << conceptName(Rest[0]);
      }
      if (Loc.isValid() && ContradictingUse)
        noteAliasUse(ContradictingUse->Use, ContradictingUse->Alias);
      return;
    }
  }
  if (!Concept)
    return;
  // Named as a function, a pointer to what is named as a function type is of
  // each instantiation, where it points to a function, or to what the 'f'
  // prefix fits, a function object, `tf_FFunction *_fObject` given a
  // `void (*)()` or a `CAction *`, but not to a value of another kind, an
  // `int *` or a `void *`; `p` fits every instantiation. What it points to is
  // the type itself, not a name it has there, the parameter it is given for
  // or a member alias, `typename tf_CTraits::FCallback` of `using FCallback =
  // int`, which says it is a function type.
  if (FunctionPointerNamed)
    for (const CObserved &Of : Observed) {
      const QualType Pointer = Of.Type.getNonReferenceType();
      if (!Pointer->isPointerType())
        continue;
      const QualType Pointee = Pointer->getPointeeType().getCanonicalType();
      if (Pointee->getAsCXXRecordDecl()
              ? !Contradicts(Pointee)
              : expectedConcept(Pointee, *Result.Context).Required ==
                    std::optional<char>('f'))
        continue;
      if (const SourceLocation Loc = nameLocation(Declaration, SM);
          Loc.isValid()) {
        {
          const InstantiationScope Scope(*this, Of.Instance);
          // What it points to, through the bounded formatter, which prints
          // it without what has too many parts to print.
          diag(Loc, "%0 has the 'f' concept prefix of %1, but this "
                    "instantiation makes it a pointer to %2, which is neither "
                    "a function nor a function object; the 'p' concept prefix "
                    "fits every instantiation")
              << diagnosticName(Declaration) << conceptName('f')
              << diagnosticType(Pointee, *Result.Context);
        }
        noteAliasUse(Of.Use, Of.Alias);
      }
      return;
    }
  // A pointer is always named as one, also one that walks over elements,
  // like `for (int *pValue = pBegin; ...)`: that it is a pointer matters
  // more than that it is an iterator. An iterator that is a range, which
  // knows its end, like `for (auto rByte = _rBytes; rByte; ++rByte)`, is
  // named for the range.
  // A pointer to what is named as a function type can be either, `x`.
  if (Rest.size() >= 2 &&
      (Rest[0] == *Concept || (*Concept == 'i' && Rest[0] == 'r') ||
       (*Concept == 'x' && (Rest[0] == 'f' || Rest[0] == 'p'))) &&
      (llvm::isUpper(Rest[1]) || llvm::isDigit(Rest[1])))
    return;

  const SourceLocation Loc = nameLocation(Declaration, SM);
  if (Loc.isInvalid())
    return;
  diag(Loc, "%0 is %1 and should have the '%2' concept prefix%select{| after "
            "its storage prefix}3")
      << diagnosticName(Declaration) << conceptName(*Concept)
      << (*Concept == 'x' ? std::string("f' or 'p")
                          : std::string(1, *Concept))
      << !Prefix.empty();
}

} // namespace clang::tidy::malterlib
