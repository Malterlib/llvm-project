//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeNamingCheck.h"
#include "../utils/OptionsUtils.h"
#include "NamingUtils.h"
#include "OncePerTypeVisitor.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/DenseMap.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

TypeNamingCheck::TypeNamingCheck(StringRef Name, ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      LowerCaseTypes(utils::options::parseStringList(listOption(
          "LowerCaseTypes", editList(DefaultLowerCaseTypes,
                                     vocabulary().BuiltinTypes.joined())))),
      MinimumNameLength(
          unsignedOption("MinimumNameLength", DefaultMinimumNameLength)) {}

void TypeNamingCheck::storeCheckOptions(ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "LowerCaseTypes",
                utils::options::serializeStringList(LowerCaseTypes));
  Options.store(Opts, "MinimumNameLength", MinimumNameLength);
}

void TypeNamingCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      decl(anyOf(recordDecl(), enumDecl(), enumConstantDecl(), namespaceDecl(),
                 namespaceAliasDecl(), typedefNameDecl(), conceptDecl()))
          .bind("type"),
      this);
  // The uses of alias templates, which tell what they are, also in the
  // instantiations of templates, `FValue<t_CType>` of `TCBox<int>`.
  Finder->addMatcher(
      traverse(TK_AsIs,
               typeLoc(loc(templateSpecializationType())).bind("aliasUse")),
      this);
  // Also of those a call of a function template makes of its defaults,
  // `fg_Run<CTraits>()`, also of a member function template,
  // `Runner.f_Run<CTraits>()`.
  Finder->addMatcher(
      traverse(
          TK_AsIs,
          declRefExpr(to(functionDecl(ast_matchers::isTemplateInstantiation())))
              .bind("functionUse")),
      this);
  Finder->addMatcher(
      traverse(TK_AsIs, memberExpr(member(functionDecl(
                                       ast_matchers::isTemplateInstantiation())))
                            .bind("memberUse")),
      this);
}

/// Whether \p Record is meant to be a function object: it declares a call
/// operator or a conversion to a function, whatever they are. None when that
/// can't be told, like of a specialization that isn't instantiated in the
/// translation unit.
static std::optional<bool> hasCallOperator(const CXXRecordDecl *Record) {
  return declaresCallable(Record);
}

/// Whether \p Canonical, a type that doesn't depend on template parameters,
/// is known not to be a function type, a pointer to one or a function object.
static bool knownNotFunction(QualType Canonical) {
  const QualType Referred = Canonical.getNonReferenceType();
  const std::optional<bool> CallOperator =
      hasCallOperator(Referred->getAsCXXRecordDecl());
  return CallOperator && !*CallOperator && !Referred->isFunctionType() &&
         !(Referred->isPointerType() &&
           Referred->getPointeeType()->isFunctionType()) &&
         !Referred->isMemberFunctionPointerType() &&
         !Referred->isBlockPointerType();
}

void TypeNamingCheck::check(const MatchFinder::MatchResult &Result) {
  const SourceManager &SM = *Result.SourceManager;
  // An alias template named as a function type or an enum, `template
  // <typename t_CType> using FValue = t_CType;`, by what a use makes of it,
  // like `int` of `FValue<int>`.
  const auto *CalledRef = Result.Nodes.getNodeAs<DeclRefExpr>("functionUse");
  const auto *CalledMember = Result.Nodes.getNodeAs<MemberExpr>("memberUse");
  const ValueDecl *Called = CalledRef      ? CalledRef->getDecl()
                            : CalledMember ? CalledMember->getMemberDecl()
                                           : nullptr;
  if (const auto *Use = Result.Nodes.getNodeAs<TypeLoc>("aliasUse");
      Use || Called) {
    // Also of the alias it is an alias of in turn, `FCallback<CTraits>` of
    // `TCWrapped<CTraits>` with `using TCWrapped = FCallback<t_CTraits>;`,
    // which the substitution makes.
    // Of \p Alias, which a use makes \p Aliased of.
    auto CheckAlias = [&](const TypeAliasTemplateDecl *Alias,
                          QualType Aliased) {
      // Of a member of a class template, `TCWrap<CTraits>::TCCallback<int>`,
      // the one written, whose pattern is what the class is instantiated from.
      for (int Depth = 0; Alias && Depth < 16; ++Depth) {
        const auto *From = dyn_cast_or_null<TypeAliasTemplateDecl>(
            Alias->getInstantiatedFromMemberTemplate());
        if (!From)
          break;
        Alias = From;
      }
      // Not one of a specialization of a template of another library, whose
      // names are its.
      if (!Alias || !Alias->getIdentifier() || Aliased->isDependentType() ||
          ReportedAliases.contains(Alias) ||
          isInExternalSpecialization(Alias->getTemplatedDecl(), SM))
        return;
      const QualType Canonical = Aliased.getCanonicalType();
      // One of a member of what depends on its parameters, `using TCCallback =
      // typename t_CTraits::FSignature;`, also of what an expression that
      // depends on them is, `decltype(t_CTraits::fs_Run)`, is a function type
      // when each use makes one, which is told at the end of the translation
      // unit.
      // Also through another alias template, `TCIdentity<typename
      // t_CTraits::FSignature>`, what it is an alias of, and through another
      // alias that depends on them, `using TCCallback = FSignature;`.
      const clang::Type *Pattern =
          Alias->getTemplatedDecl()->getUnderlyingType().getTypePtr();
      for (int Depth = 0; Depth < 16; ++Depth)
        if (const auto *Attributed = dyn_cast<AttributedType>(Pattern))
          Pattern = Attributed->getModifiedType().getTypePtr();
        else if (const auto *Other = dyn_cast<TypedefType>(Pattern);
                 Other && Other->isDependentType())
          Pattern = Other->desugar().getTypePtr();
        else if (const auto *Wrapper =
                     dyn_cast<TemplateSpecializationType>(Pattern);
                 Wrapper && Wrapper->isTypeAlias())
          Pattern = Wrapper->getAliasedType().getTypePtr();
        else if (const auto *Substituted =
                     dyn_cast<SubstTemplateTypeParmType>(Pattern))
          Pattern = Substituted->getReplacementType().getTypePtr();
        else
          break;
      const auto *MemberTemplate =
          dyn_cast<TemplateSpecializationType>(Pattern);
      // Also a transformation of what depends on them,
      // `std::remove_reference_t< typename t_CTraits::FSignature>`, but not of
      // a parameter, also not the `type` of a template given one, `typename
      // std::enable_if<t_bEnable, t_CType>::type`, which is generic, whatever
      // it is given.
      const auto *Transform = dyn_cast<UnaryTransformType>(Pattern);
      if (!isTypeOfGivenParameter(Pattern, /*Nested=*/true) &&
          (isa<DependentNameType, UnresolvedUsingType>(Pattern) ||
           (isa<DecltypeType, TypeOfExprType, TypeOfType>(Pattern) &&
            Pattern->isDependentType()) ||
           (Transform && Transform->isDependentType() &&
            !givesParameter(Transform->getBaseType(), /*Nested=*/true)) ||
           (MemberTemplate &&
            MemberTemplate->getTemplateName().getAsDependentTemplateName()))) {
        // A use where a member of what a parameter is given gives what that
        // is given in turn, `TCMapKey<TCMap<EMode, int>>` of `typename
        // t_CMap::CKey`, is generic, like that of a member alias, which keeps
        // it from being named as what the others are.
        CAliasUses &Uses = MemberAliasUses[Alias];
        (isMemberOfGivenParameter(Pattern) && forwardsArgument(Aliased)
             ? Uses.Others
         : Canonical->isFunctionType() ? Uses.Functions
         : Canonical->isEnumeralType() ? Uses.Enums
                                       : Uses.Others) = true;
        Uses.SM = &SM;
      }
      // Of `TF`, which an alias template of a function type has: one with
      // the `F` of an alias is reported where it is declared.
      const bool WrongFunction = hasTypePrefix(Alias->getName(), {"TF"}) &&
                                 knownNotFunction(Canonical);
      const bool WrongEnum = hasTypePrefix(Alias->getName(), {"E"}) &&
                             !Canonical->isEnumeralType();
      if (!WrongFunction && !WrongEnum)
        return;
      ReportedAliases.insert(Alias);
      const SourceLocation Loc = nameLocation(Alias->getTemplatedDecl(), SM);
      if (Loc.isValid())
        diag(Loc,
             WrongFunction
                 ? "type alias %0 has the 'F' prefix of function types, but "
                   "is not a function type or a function object"
                 : "type alias %0 has the 'E' prefix of enums, but is not "
                   "an enum")
            << Alias;
    };
    auto AliasOf = [](const TemplateSpecializationType *Specialization) {
      return Specialization && Specialization->isTypeAlias()
                 ? dyn_cast_or_null<TypeAliasTemplateDecl>(
                       Specialization->getTemplateName().getAsTemplateDecl())
                 : nullptr;
    };
    // Each alias in \p Written, as what is at its place in \p Given, also
    // nested in what it is given, `TCFunction<TCSignature<tf_CTraits>>` of a
    // default given `TCFunction<void ()>`, and in what an alias stands for,
    // `TCCallback<CTraits>` of `using TCCallback =
    // TCFunction<TCSignature<t_CTraits>>`.
    // Each pair once, as deep as it is met, which one given the same
    // arguments, `TCPart<TCOne<T>, TCOne<T>>`, would otherwise meet once for
    // each path to it.
    llvm::DenseMap<std::pair<void *, void *>, int> Checked;
    std::function<void(QualType, QualType, int)> CheckNested =
        [&](QualType Written, QualType Given, int Depth) {
          if (Written.isNull() || Given.isNull() || Depth > 16 ||
              Given->isDependentType())
            return;
          const auto [Entry, Inserted] = Checked.try_emplace(
              {Written.getAsOpaquePtr(), Given.getAsOpaquePtr()}, Depth);
          if (!Inserted) {
            if (Entry->second <= Depth)
              return;
            Entry->second = Depth;
          }
          const auto *Specialization =
              Written->getAs<TemplateSpecializationType>();
          if (!Specialization) {
            if (Written->isReferenceType() && Given->isReferenceType())
              CheckNested(Written->getPointeeType(), Given->getPointeeType(),
                          Depth + 1);
            else if (Written->isPointerType() && Given->isPointerType())
              CheckNested(Written->getPointeeType(), Given->getPointeeType(),
                          Depth + 1);
            else if (const auto *Function =
                         Written->getAs<FunctionProtoType>()) {
              const auto *Made = Given->getAs<FunctionProtoType>();
              if (!Made || Made->getNumParams() != Function->getNumParams())
                return;
              CheckNested(Function->getReturnType(), Made->getReturnType(),
                          Depth + 1);
              for (unsigned iParam = 0; iParam < Function->getNumParams();
                   ++iParam)
                CheckNested(Function->getParamType(iParam),
                            Made->getParamType(iParam), Depth + 1);
            }
            return;
          }
          if (Specialization->isTypeAlias()) {
            CheckAlias(AliasOf(Specialization), Given);
            CheckNested(Specialization->getAliasedType(), Given, Depth + 1);
            return;
          }
          const auto *Template =
              Specialization->getTemplateName().getAsTemplateDecl();
          const auto *Made = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
              Given->getAsCXXRecordDecl());
          if (!Template || !Made ||
              Made->getSpecializedTemplate()->getCanonicalDecl() !=
                  Template->getCanonicalDecl())
            return;
          const TemplateArgumentList &Arguments = Made->getTemplateArgs();
          const auto WrittenArguments = Specialization->template_arguments();
          for (unsigned iArgument = 0; iArgument < WrittenArguments.size() &&
                                       iArgument < Arguments.size();
               ++iArgument) {
            if (WrittenArguments[iArgument].isPackExpansion())
              break;
            if (WrittenArguments[iArgument].getKind() ==
                    TemplateArgument::Type &&
                Arguments[iArgument].getKind() == TemplateArgument::Type)
              CheckNested(WrittenArguments[iArgument].getAsType(),
                          Arguments[iArgument].getAsType(), Depth + 1);
          }
        };
    // Of a function template, the defaults of what the call leaves out and
    // its parameters don't deduce, `typename tf_CSignature =
    // TCSignature<tf_CTraits>`, as the specialization is given them.
    if (Called) {
      const auto *Function = dyn_cast<FunctionDecl>(Called);
      const FunctionTemplateDecl *Template =
          Function ? Function->getPrimaryTemplate() : nullptr;
      const TemplateArgumentList *Arguments =
          Function ? Function->getTemplateSpecializationArgs() : nullptr;
      if (!Template || !Arguments)
        return;
      const TemplateParameterList *Parameters =
          Template->getTemplateParameters();
      // The call, which deduces from the arguments it gives, not from those
      // it leaves to their defaults, `fg_Run<CTraits>()` of `fg_Run(
      // tf_FSignature *_fCallback = nullptr)`; none where it isn't called,
      // like where its address is taken.
      const CallExpr *Call = nullptr;
      {
        const Expr *Callee = CalledRef ? static_cast<const Expr *>(CalledRef)
                                       : CalledMember;
        DynTypedNode Node = DynTypedNode::create(*Callee);
        for (int Level = 0; Level < 4 && !Call; ++Level) {
          const auto Parents = Result.Context->getParents(Node);
          if (Parents.empty())
            break;
          Node = Parents[0];
          if (const auto *Parent = Node.get<CallExpr>())
            Call = Parent->getCallee()->IgnoreParenImpCasts() == Callee
                       ? Parent
                       : nullptr;
          else if (!Node.get<ImplicitCastExpr>() && !Node.get<ParenExpr>())
            break;
        }
      }
      auto Passed = [&](unsigned iParameter) {
        return !Call || (iParameter < Call->getNumArgs() &&
                         !isa<CXXDefaultArgExpr>(Call->getArg(iParameter)));
      };
      // Whether a parameter of the function the call gives an argument for
      // names it in its type where the call can deduce it from, not `tf_CData
      // &` of another one, nor in a qualifier, `typename TCIdentity<
      // tf_FCallback>::CType *`, or `decltype`, which aren't deduced. Each type
      // once, which one that names another twice, `F16 (*)(F16, F16)`, would
      // otherwise meet once for each path to it.
      auto Deducible = [&](const TemplateTypeParmDecl *Parameter) {
        struct CMentions : OncePerTypeVisitor<CMentions> {
          const clang::Type *Type = nullptr;
          bool Found = false;
          bool TraverseNestedNameSpecifier(NestedNameSpecifier) { return true; }
          bool TraverseNestedNameSpecifierLoc(NestedNameSpecifierLoc) {
            return true;
          }
          bool TraverseDecltypeType(DecltypeType *, bool = true) {
            return true;
          }
          bool TraverseDecltypeTypeLoc(DecltypeTypeLoc, bool = true) {
            return true;
          }
          // Through what an alias stands for, `TCIdentity<tf_CValue>` of
          // `typename TCIdentityHelper<t_CType>::CType`, whose qualifier
          // isn't deduced.
          bool TraverseTemplateSpecializationType(
              TemplateSpecializationType *Specialization,
              bool TraverseQualifier = true) {
            if (Specialization->isTypeAlias())
              return TraverseType(Specialization->getAliasedType());
            return OncePerTypeVisitor<CMentions>::
                TraverseTemplateSpecializationType(Specialization,
                                                   TraverseQualifier);
          }
          bool TraverseTemplateSpecializationTypeLoc(
              TemplateSpecializationTypeLoc Specialization,
              bool TraverseQualifier = true) {
            if (Specialization.getTypePtr()->isTypeAlias())
              return TraverseType(
                  Specialization.getTypePtr()->getAliasedType());
            return OncePerTypeVisitor<CMentions>::
                TraverseTemplateSpecializationTypeLoc(Specialization,
                                                      TraverseQualifier);
          }
          bool VisitTemplateTypeParmType(TemplateTypeParmType *Each) {
            Found = Each->getCanonicalTypeInternal().getTypePtr() == Type;
            return !Found;
          }
        };
        CMentions Mentions;
        Mentions.Type =
            Parameter->getTypeForDecl()->getCanonicalTypeInternal().getTypePtr();
        for (const ParmVarDecl *Given :
             Template->getTemplatedDecl()->parameters())
          if (!Passed(Given->getFunctionScopeIndex()))
            continue;
          else if (const TypeSourceInfo *Written = Given->getTypeSourceInfo();
                   Written && !Mentions.TraverseTypeLoc(Written->getTypeLoc()))
            return true;
          else if (!Written && !Mentions.TraverseType(Given->getType()))
            return true;
        return false;
      };
      for (unsigned iParameter = CalledRef ? CalledRef->getNumTemplateArgs()
                                           : CalledMember->getNumTemplateArgs();
           iParameter < Parameters->size() && iParameter < Arguments->size();
           ++iParameter) {
        const auto *Parameter =
            dyn_cast<TemplateTypeParmDecl>(Parameters->getParam(iParameter));
        if (!Parameter || Parameter->isParameterPack() ||
            !Parameter->hasDefaultArgument() ||
            Arguments->get(iParameter).getKind() != TemplateArgument::Type ||
            Deducible(Parameter))
          continue;
        const QualType Default =
            Parameter->getDefaultArgument().getArgument().getAsType();
        CheckNested(Default, Arguments->get(iParameter).getAsType(), 0);
      }
      return;
    }
    const auto *Specialization =
        Use->getType()->getAs<TemplateSpecializationType>();
    // Also an alias that is the default argument of a class template,
    // `typename t_FCallback = FCallback<t_CTraits>`, which a use that leaves
    // the argument out, `TCHandler<CTraits>`, makes what the specialization
    // is given.
    if (const auto *Template =
            Specialization && !Specialization->isTypeAlias()
                ? dyn_cast_or_null<ClassTemplateDecl>(
                      Specialization->getTemplateName().getAsTemplateDecl())
                : nullptr)
      if (const auto *Made = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
              Use->getType()->getAsCXXRecordDecl());
          Made && !Made->isDependentContext() &&
          llvm::none_of(Specialization->template_arguments(),
                        [](const TemplateArgument &Written) {
                          return Written.isPackExpansion();
                        })) {
        const TemplateParameterList *Parameters =
            Template->getTemplateParameters();
        const TemplateArgumentList &Arguments = Made->getTemplateArgs();
        for (unsigned iParameter = Specialization->template_arguments().size();
             iParameter < Parameters->size() && iParameter < Arguments.size();
             ++iParameter) {
          const auto *Parameter =
              dyn_cast<TemplateTypeParmDecl>(Parameters->getParam(iParameter));
          if (!Parameter || Parameter->isParameterPack() ||
              !Parameter->hasDefaultArgument() ||
              Arguments[iParameter].getKind() != TemplateArgument::Type)
            continue;
          const QualType Default =
              Parameter->getDefaultArgument().getArgument().getAsType();
          CheckNested(Default, Arguments[iParameter].getAsType(), 0);
        }
      }
    if (Specialization && Specialization->isTypeAlias())
      CheckNested(QualType(Specialization, 0), Use->getType(), 0);
    return;
  }
  if (Repeated.repeated(Result))
    return;
  const auto *Type = Result.Nodes.getNodeAs<NamedDecl>("type");
  if (!Type->getIdentifier() || Type->isImplicit() ||
      isInExternalSpecialization(Type, SM))
    return;
  // The names of the standard library, also declared again, like a
  // specialization of `std::hash`.
  if (Type->isInStdNamespace() ||
      (isa<NamespaceDecl>(Type) && cast<NamespaceDecl>(Type)->isStdNamespace()))
    return;

  // A type of a C library that this code only declares, in `extern "C"`,
  // `extern "C" { struct x509_store_st; }`, which the library defines
  // elsewhere, has the library's name, whichever header declares it first.
  auto OfCLibrary = [](const TagDecl *Tag) {
    return Tag && !Tag->getDefinition() &&
           llvm::any_of(Tag->redecls(), [](const TagDecl *Redecl) {
             return Redecl->getLexicalDeclContext()->isExternCContext();
           });
  };

  if (const auto *Record = dyn_cast<RecordDecl>(Type)) {
    if (isa<ClassTemplateSpecializationDecl>(Record) ||
        Record->isAnonymousStructOrUnion() || OfCLibrary(Record))
      return;
    const auto *CXXRecord = dyn_cast<CXXRecordDecl>(Record);
    if (CXXRecord &&
        (CXXRecord->isLambda() || CXXRecord->isInjectedClassName() ||
         CXXRecord->getTemplateSpecializationKind() ==
             TSK_ImplicitInstantiation ||
         CXXRecord->getInstantiatedFromMemberClass()))
      return;
    // Check a type once: at its definition, or at its first declaration when
    // it is not defined here. A type defined in a system header has an
    // external name, even when this code declares it first.
    const RecordDecl *Definition = Record->getDefinition();
    if (Definition ? Definition != Record : !Record->isFirstDecl())
      return;
    if (Definition && SM.isInSystemHeader(Definition->getLocation()))
      return;
    // Only a template is named as one, also nested in another, not a class
    // that a template only has, `struct CNode` in `TCMap`.
    if (CXXRecord && CXXRecord->getDescribedClassTemplate())
      checkName(Record, SM, "class template", {"TC", "TIC"});
    else
      checkName(Record, SM, Record->isUnion() ? "union" : "class",
                {"C", "IC"});
    return;
  }

  if (const auto *Enum = dyn_cast<EnumDecl>(Type)) {
    // Like a class: at its definition, or at its first declaration when it
    // isn't defined here, and not when it is defined in a system header.
    const EnumDecl *Definition = Enum->getDefinition();
    if ((Definition ? Definition != Enum : !Enum->isFirstDecl()) ||
        Enum->getInstantiatedFromMemberEnum())
      return;
    if (Definition && SM.isInSystemHeader(Definition->getLocation()))
      return;
    // Without the underscore that separates its enumerators' names from its
    // own, `EMode_Read` of `EMode`.
    if (Enum->getIdentifier() && hasTypePrefix(Enum->getName(), {"E"}) &&
        Enum->getName().contains('_')) {
      const SourceLocation Loc = nameLocation(Enum, SM);
      if (Loc.isValid())
        diag(Loc, "enum %0 should have the 'E' prefix followed by upper camel "
                  "case without underscores, which separate the names of its "
                  "enumerators from its own")
            << Enum;
      return;
    }
    checkName(Enum, SM, "enum", {"E"});
    return;
  }

  if (isa<EnumConstantDecl>(Type)) {
    const auto *Enum = cast<EnumDecl>(Type->getDeclContext());
    // The enumerators of a scoped enum are named as the member constants they
    // are used as, like `EFlag::mc_None`.
    if (Enum->isScoped()) {
      StringRef Prefix, Rest;
      if (!splitStoragePrefix(Type->getName(), Prefix, Rest) ||
          Prefix != "mc_" || !isUpperCamelWithConcepts(Rest)) {
        const SourceLocation Loc = nameLocation(Type, SM);
        if (Loc.isValid())
          diag(Loc, "enumerator %0 of a scoped enum should have the 'mc_' "
                    "prefix followed by upper camel case")
              << Type;
      }
      return;
    }
    // An unnamed enum is what constants were before `constexpr`, `enum {
    // mc_Value = 1 };`, which are constexpr variables now, and enumerators
    // without an enum to name them, `enum { EMode_Read };`, which belong in
    // a named one.
    const TypedefNameDecl *Typedef = Enum->getTypedefNameForAnonDecl();
    if (!Enum->getIdentifier() && !Typedef) {
      StringRef Prefix, Rest;
      const SourceLocation Loc = nameLocation(Type, SM);
      if (Loc.isInvalid())
        return;
      if (splitStoragePrefix(Type->getName(), Prefix, Rest))
        diag(Loc, "enumerator %0 of an unnamed enum is a constant; declare it "
                  "as a 'constexpr' variable instead")
            << Type;
      else
        diag(Loc, "enumerator %0 of an unnamed enum should be in a named enum, "
                  "or be a 'constexpr' variable")
            << Type;
      return;
    }
    // The name of an enumerator is the name of its enum, an underscore and
    // upper camel case, `EMode_Read` of `EMode`, also of one named by a
    // typedef, `typedef enum { ... } EMode;`.
    const StringRef EnumName =
        Enum->getIdentifier() ? Enum->getName() : Typedef->getName();
    const StringRef Name = Type->getName();
    if (Name.size() > EnumName.size() + 1 && Name.starts_with(EnumName) &&
        Name[EnumName.size()] == '_' &&
        (llvm::isUpper(Name[EnumName.size() + 1]) ||
         llvm::isDigit(Name[EnumName.size() + 1])) &&
        hasUpperGroups(Name.drop_front(EnumName.size() + 1)))
      return;
    const SourceLocation Loc = nameLocation(Type, SM);
    if (Loc.isValid())
      diag(Loc, "enumerator %0 should have the name of its enum followed by "
                "an underscore, '%1_', and upper camel case")
          << Type << EnumName;
    return;
  }

  if (const auto *Namespace = dyn_cast<NamespaceDecl>(Type)) {
    if (!Namespace->isFirstDecl())
      return;
    // One a system header declares too is named by the library, also where
    // it is declared first, `namespace tinyxml2 { class XMLNode; }`.
    if (llvm::any_of(Namespace->redecls(), [&](const NamespaceDecl *Redecl) {
          return SM.isInSystemHeader(Redecl->getLocation());
        }))
      return;
    checkName(Namespace, SM, "namespace", {"N"});
    return;
  }

  if (isa<NamespaceAliasDecl>(Type)) {
    checkName(Type, SM, "namespace alias", {"N"});
    return;
  }

  if (const auto *Typedef = dyn_cast<TypedefNameDecl>(Type)) {
    if (!Typedef->isFirstDecl())
      return;
    // Also a name it gives such a type in `extern "C"`, `typedef struct
    // x509_store_st X509_STORE;`.
    if (Typedef->getLexicalDeclContext()->isExternCContext() &&
        OfCLibrary(Typedef->getUnderlyingType()->getAsTagDecl()))
      return;
    const auto *Alias = dyn_cast<TypeAliasDecl>(Typedef);
    const bool Template = Alias && Alias->getDescribedAliasTemplate();
    // `F` is for function types, like `using FTest = void ();`, which take
    // it, pointers to them and function objects, like `TCFunction<void ()>`,
    // and `E` for enums. A dependent type can be any of them.
    const QualType Underlying = Typedef->getUnderlyingType();
    // A function type is one also when what it returns or takes depends on a
    // template parameter, like `t_CResult ()`.
    if (Underlying->isDependentType() && Underlying->isFunctionType()) {
      checkName(Typedef, SM, "function type alias",
                Template ? llvm::ArrayRef<StringRef>{"TF"}
                         : llvm::ArrayRef<StringRef>{"F"});
      return;
    }
    // An array is never a function or a function object, whatever it is made
    // of, `t_CType[2]`, nor a pointer to one, `t_CType (*)[2]`, or to a
    // pointer, `t_CType **`, which can't be called.
    const QualType Aliased = Underlying.getNonReferenceType();
    const bool NeverCallable =
        Aliased->isArrayType() ||
        (Aliased->isPointerType() &&
         (Aliased->getPointeeType()->isArrayType() ||
          Aliased->getPointeeType()->isPointerType()));
    if (Underlying->isDependentType() && NeverCallable &&
        hasTypePrefix(Typedef->getName(), {"F", "TF"})) {
      const SourceLocation Loc = nameLocation(Typedef, SM);
      if (Loc.isValid())
        diag(Loc, "type alias %0 has the 'F' prefix of function types, but is "
                  "not a function type or a function object")
            << Typedef;
      return;
    }
    // Whether \p Canonical, a type that doesn't depend on template
    // parameters, is known not to be a function type, a pointer to one or a
    // function object.
    auto KnownNotFunction = [](QualType Canonical) {
      const QualType Referred = Canonical.getNonReferenceType();
      const std::optional<bool> CallOperator =
          hasCallOperator(Referred->getAsCXXRecordDecl());
      return CallOperator && !*CallOperator && !Referred->isFunctionType() &&
             !(Referred->isPointerType() &&
               Referred->getPointeeType()->isFunctionType()) &&
             !Referred->isMemberFunctionPointerType() &&
             !Referred->isBlockPointerType();
    };
    // The aliases the instantiations make of a dependent one: of a member
    // of a class template, and of a local of a function template.
    llvm::SmallVector<const TypedefNameDecl *, 4> InstantiatedAliases;
    if (Underlying->isDependentType()) {
      if (const auto *Class =
              dyn_cast<CXXRecordDecl>(Typedef->getDeclContext())) {
        for (const CXXRecordDecl *Instance : instancesOf(Class))
          if (Instance->hasDefinition())
            for (const NamedDecl *Found :
                 Instance->lookup(Typedef->getDeclName()))
              if (const auto *Alias = dyn_cast<TypedefNameDecl>(Found))
                InstantiatedAliases.push_back(Alias);
      } else if (Typedef->getDeclContext()->isFunctionOrMethod())
        for (const Decl *Instance : localInstances(Typedef))
          InstantiatedAliases.push_back(cast<TypedefNameDecl>(Instance));
    }
    // A member alias of a class template, `using FValue = t_CType;`, is
    // what its instantiations make of it, like `int` of `TCContainer<int>`,
    // and so is a local alias of a function template.
    if (Underlying->isDependentType() &&
        hasTypePrefix(Typedef->getName(), {"F", "TF", "E"}))
      for (const TypedefNameDecl *Instantiated : InstantiatedAliases) {
        if (Instantiated->getUnderlyingType()->isDependentType())
          continue;
        const QualType Canonical =
            Instantiated->getUnderlyingType().getCanonicalType();
        const SourceLocation Loc = nameLocation(Typedef, SM);
        if (hasTypePrefix(Typedef->getName(), {"F", "TF"}) &&
            KnownNotFunction(Canonical)) {
          if (Loc.isValid())
            diag(Loc, "type alias %0 has the 'F' prefix of function types, "
                      "but is not a function type or a function object")
                << Typedef;
          return;
        }
        if (hasTypePrefix(Typedef->getName(), {"E"}) &&
            !Canonical->isEnumeralType()) {
          if (Loc.isValid())
            diag(Loc, "type alias %0 has the 'E' prefix of enums, but is not "
                      "an enum")
                << Typedef;
          return;
        }
      }
    // And one of a member of what depends on template parameters that each
    // instantiation makes a function type, `using CCallback = typename
    // t_CTraits::FSignature;`, which the traits it is given decide, also
    // what is computed from them, `decltype(t_CTraits::fs_Run)`, is named as
    // one, also a local one, but not one of a parameter, `using CValue =
    // t_CType;`, which is generic, whatever it is instantiated with.
    // Also one a using declaration brings in from a base that depends on
    // them, `using typename TCParent<t_CType>::FSignature`, and one annotated,
    // `[[clang::annotate_type("...")]]`.
    // Also through an alias template, `TCIdentity<typename
    // t_CTraits::FSignature>`, what it is an alias of, and through another
    // alias that depends on them, `using CCallback = FSignature;`, and in
    // parentheses, `typedef typename t_CTraits::FSignature (CCallback);`.
    const clang::Type *Written = Underlying.getTypePtr();
    for (int Depth = 0; Depth < 16; ++Depth)
      if (const auto *Attributed = dyn_cast<AttributedType>(Written))
        Written = Attributed->getModifiedType().getTypePtr();
      else if (const auto *Parens = dyn_cast<ParenType>(Written))
        Written = Parens->getInnerType().getTypePtr();
      else if (const auto *Other = dyn_cast<TypedefType>(Written);
               Other && Other->isDependentType())
        Written = Other->desugar().getTypePtr();
      else if (const auto *Alias =
                   dyn_cast<TemplateSpecializationType>(Written);
               Alias && Alias->isTypeAlias())
        Written = Alias->getAliasedType().getTypePtr();
      else if (const auto *Substituted =
                   dyn_cast<SubstTemplateTypeParmType>(Written))
        Written = Substituted->getReplacementType().getTypePtr();
      else
        break;
    // The declared type of what it names, `decltype(_Value)` of `tf_CType
    // _Value`, which is what that is, also generic, not of an expression in
    // parentheses, `decltype((_Value))`, which is computed.
    if (const auto *Decltype = dyn_cast<DecltypeType>(Written))
      if (const auto *Named =
              dyn_cast<DeclRefExpr>(Decltype->getUnderlyingExpr());
          Named && isa<VarDecl>(Named->getDecl()))
        Written = Named->getDecl()->getType().getTypePtr();
    const auto *MemberTemplate = dyn_cast<TemplateSpecializationType>(Written);
    // Also a transformation of what depends on them, `std::remove_reference_t<
    // typename t_CTraits::FSignature>`, but not of a parameter, which is
    // generic.
    const auto *Transform = dyn_cast<UnaryTransformType>(Written);
    if (!isTypeOfGivenParameter(Written, /*Nested=*/true) &&
        (isa<DependentNameType, UnresolvedUsingType>(Written) ||
         (isa<DecltypeType, TypeOfExprType, TypeOfType>(Written) &&
          Written->isDependentType()) ||
         (Transform && Transform->isDependentType() &&
          !givesParameter(Transform->getBaseType(), /*Nested=*/true)) ||
         (MemberTemplate &&
          MemberTemplate->getTemplateName().getAsDependentTemplateName())) &&
        !hasTypePrefix(Typedef->getName(), {"F", "TF"})) {
      bool Functions = false;
      bool Enums = false;
      bool Others = false;
      // A member of what a parameter is given, `typename t_CMap::CKey`, is
      // what that defines it as, `typename t_CTraits::EState`, unless it
      // gives what it is given in turn, `TCMap<EMode, int>::CKey`.
      const bool OfParameter = isMemberOfGivenParameter(Written);
      // One that gives what it is given in turn is generic, which keeps it
      // from being named as what the others are.
      for (const TypedefNameDecl *Instantiated : InstantiatedAliases)
        if (const QualType Made = Instantiated->getUnderlyingType();
            !Made->isDependentType())
          (OfParameter && forwardsArgument(Made) ? Others
           : Made->isFunctionType()              ? Functions
           : Made->isEnumeralType()              ? Enums
                                                 : Others) = true;
      if (Functions && !Enums && !Others) {
        checkName(Typedef, SM, "function type alias",
                  Template ? llvm::ArrayRef<StringRef>{"TF"}
                           : llvm::ArrayRef<StringRef>{"F"});
        return;
      }
      // And an enum, `using CState = typename t_CTraits::EState;`.
      if (Enums && !Functions && !Others) {
        checkName(Typedef, SM, "enum type alias", {"E"});
        return;
      }
    }
    if (!Underlying->isDependentType()) {
      const QualType Canonical = Underlying.getCanonicalType();
      const bool Function = Canonical->isFunctionType();
      // A reference is what it refers to, `FCallback *&` or `CFunctor &`.
      const QualType Referred = Canonical.getNonReferenceType();
      const std::optional<bool> CallOperator =
          hasCallOperator(Referred->getAsCXXRecordDecl());
      const bool FunctionLike =
          Referred->isFunctionType() ||
          (Aliased->isPointerType() &&
           Referred->getPointeeType()->isFunctionType()) ||
          Referred->isMemberFunctionPointerType() ||
          Referred->isBlockPointerType() || CallOperator.value_or(false);
      if (Function) {
        checkName(Typedef, SM, "function type alias",
                  Template ? llvm::ArrayRef<StringRef>{"TF"}
                           : llvm::ArrayRef<StringRef>{"F"});
        return;
      }
      auto Report = [&](StringRef Message) {
        const SourceLocation Loc = nameLocation(Typedef, SM);
        if (Loc.isValid())
          diag(Loc, Message) << Typedef;
      };
      // A class that isn't defined in the translation unit, like a forward
      // declared `TCFunctionMovable<void ()>` or a specialization that isn't
      // instantiated, or one whose call operator can't be told, can be
      // either: it is checked where it is.
      const bool Unknown = !CallOperator;
      if (!FunctionLike && !Unknown &&
          hasTypePrefix(Typedef->getName(), {"F", "TF"})) {
        Report("type alias %0 has the 'F' prefix of function types, but is "
               "not a function type or a function object");
        return;
      }
      if (!Canonical->isEnumeralType() &&
          hasTypePrefix(Typedef->getName(), {"E"})) {
        Report("type alias %0 has the 'E' prefix of enums, but is not an "
               "enum");
        return;
      }
      // One of an enum is named like one, `using EStateAlias = EState;`,
      // also a template of one known to be, `template <typename t_CType>
      // using EStatus = EState;`.
      if (Canonical->isEnumeralType()) {
        checkName(Typedef, SM, "enum type alias", {"E"});
        return;
      }
    }
    // One of an enum, `template <typename t_CType> using EResult = ...`,
    // takes `E`, which its uses check is of an enum.
    // Of a function type, `TF`, not `F`, and, like a class, only a template
    // is named as one, not an alias that one only has.
    if (Template)
      checkName(Typedef, SM, "alias template", {"TC", "TIC", "TF", "E"});
    else
      checkName(Typedef, SM, "type alias", {"C", "IC", "F", "E"});
    return;
  }

  if (isa<ConceptDecl>(Type))
    checkName(Type, SM, "concept", {"c"});
}

void TypeNamingCheck::checkName(const NamedDecl *Type, const SourceManager &SM,
                                StringRef Kind,
                                llvm::ArrayRef<StringRef> Expected) {
  const StringRef Name = Type->getName();
  // The built-in types (`uint32`) and protocols (`promise_type`) are lower
  // case: classes and aliases, not namespaces, concepts or enums.
  const bool LowerCaseAllowed = isa<RecordDecl, TypedefNameDecl>(Type);
  if (LowerCaseAllowed && llvm::is_contained(LowerCaseTypes, Name))
    return;
  if (hasTypePrefix(Name, Expected)) {
    // At least as long as the minimum after the longest prefix it has, not
    // `CX`.
    size_t Longest = 0;
    for (const StringRef Prefix : Expected)
      if (hasTypePrefix(Name, {Prefix}))
        Longest = std::max(Longest, Prefix.size());
    if (Name.size() - Longest >= MinimumNameLength)
      return;
    const SourceLocation Loc = nameLocation(Type, SM);
    if (Loc.isValid())
      diag(Loc, "%0 %1 should have at least %2 characters after its prefix")
          << Kind << Type << MinimumNameLength;
    return;
  }
  const SourceLocation Loc = nameLocation(Type, SM);
  if (Loc.isInvalid())
    return;
  diag(Loc, "%0 %1 should have the %2 prefix followed by upper camel case")
      << Kind << Type << quotedList(Expected);
}

void TypeNamingCheck::onEndOfTranslationUnit() {
  // Also one that each use makes an enum, `template <typename t_CTraits> using
  // EState = typename t_CTraits::EState;`.
  for (const auto &[Alias, Uses] : MemberAliasUses) {
    if (!Uses.SM || Uses.Others)
      continue;
    if (Uses.Functions && !Uses.Enums)
      checkName(Alias->getTemplatedDecl(), *Uses.SM, "function type alias",
                {"TF"});
    else if (Uses.Enums && !Uses.Functions)
      checkName(Alias->getTemplatedDecl(), *Uses.SM, "enum type alias", {"E"});
  }
  MemberAliasUses.clear();
}

} // namespace clang::tidy::malterlib
