//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeNamingCheck.h"
#include "../utils/OptionsUtils.h"
#include "CheckedAccess.h"
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
  return !possiblyTrue(CallOperator) && !Referred->isFunctionType() &&
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
    // Where it is used, which a report it decides names.
    const SourceLocation UsedAt = Use        ? Use->getBeginLoc()
                                  : CalledRef ? CalledRef->getExprLoc()
                                              : CalledMember->getExprLoc();
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
      // One of what depends on its parameters, `using TCCallback = typename
      // t_CTraits::FSignature;`, is generic: its generic name is always
      // allowed, and its uses only exclude what is wrong in them.
      // Of `TF`, which an alias template of a function type has: one with
      // the `F` of an alias is reported where it is declared.
      const bool WrongFunction =
          hasTypePrefix(identifierTextOf(Alias), {"TF"}) &&
          knownNotFunction(Canonical);
      const bool WrongEnum = hasTypePrefix(identifierTextOf(Alias), {"E"}) &&
                             !Canonical->isEnumeralType();
      if (!WrongFunction && !WrongEnum)
        return;
      ReportedAliases.insert(Alias);
      const SourceLocation Loc = nameLocation(Alias->getTemplatedDecl(), SM);
      if (Loc.isInvalid())
        return;
      diag(Loc,
           WrongFunction
               ? "type alias %0 has the 'F' prefix of function types, but "
                 "is not a function type or a function object"
               : "type alias %0 has the 'E' prefix of enums, but is not "
                 "an enum")
          << diagnosticName(Alias);
      noteAliasUse(UsedAt, Alias);
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
    // Each pair once in the translation unit, as deep as it is met, which one
    // given the same arguments, `TCPart<TCOne<T>, TCOne<T>>`, would otherwise
    // meet once for each path to it, and each use in each instantiation again.
    auto &Checked = CheckedAliasUses;
    TCRecursive<void(QualType, QualType, int)> CheckNested =
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
    // Of a function template, the defaults clang used where it is named,
    // `typename tf_CSignature = TCSignature<tf_CTraits>` of `fg_Run<CTraits>()`
    // (defaultedTemplateParameters), as the specialization is given them.
    if (Called) {
      const auto *Function = dyn_cast<FunctionDecl>(Called);
      const FunctionTemplateDecl *Template =
          Function ? Function->getPrimaryTemplate() : nullptr;
      const TemplateArgumentList *Arguments =
          Function ? Function->getTemplateSpecializationArgs() : nullptr;
      if (!Template || !Arguments)
        return;
      const Expr *Reference = CalledRef ? static_cast<const Expr *>(CalledRef)
                                        : CalledMember;
      for (const CDefaultedParameter &Defaulted : defaultedTemplateParameters(
               Reference, callOfCallee(Reference, *Result.Context)))
        CheckNested(Defaulted.Parameter->getDefaultArgument()
                        .getArgument()
                        .getAsType(),
                    Arguments->get(Defaulted.Index).getAsType(), 0);
      return;
    }
    const auto *Specialization =
        Use->getType()->getAs<TemplateSpecializationType>();
    // Also an alias that is the default argument of a class template,
    // `typename t_FCallback = FCallback<t_CTraits>`, which a use that leaves
    // the argument out, `TCHandler<CTraits>`, makes what the specialization
    // is given (forEachDefaultedTemplateParameter).
    if (Specialization)
      forEachDefaultedTemplateParameter(
          Specialization,
          [&](const ClassTemplateSpecializationDecl *Made,
              const CDefaultedParameter &Defaulted) {
            CheckNested(Defaulted.Parameter->getDefaultArgument()
                            .getArgument()
                            .getAsType(),
                        Made->getTemplateArgs()[Defaulted.Index].getAsType(), 0);
          });
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
  // Also when the library's header, a system header, defines it in the same
  // translation unit, as libdbus defines `DBusMessageIter`, which callers
  // allocate.
  auto OfCLibrary = [&SM](const TagDecl *Tag) {
    if (!Tag)
      return false;
    const TagDecl *Definition = Tag->getDefinition();
    return (!Definition || SM.isInSystemHeader(Definition->getLocation())) &&
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
    // it is not defined here. An external type (isExternalDeclaration), one
    // a system header defines, or declares when none defines it, has the
    // library's name, also when this code declares it first.
    const RecordDecl *Definition = Record->getDefinition();
    if (Definition ? Definition != Record : !Record->isFirstDecl())
      return;
    if (isExternalDeclaration(Record, SM))
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
    // isn't defined here, and not when it is external.
    const EnumDecl *Definition = Enum->getDefinition();
    if ((Definition ? Definition != Enum : !Enum->isFirstDecl()) ||
        Enum->getInstantiatedFromMemberEnum())
      return;
    if (isExternalDeclaration(Enum, SM))
      return;
    // Without the underscore that separates its enumerators' names from its
    // own, `EMode_Read` of `EMode`.
    if (Enum->getIdentifier() && hasTypePrefix(identifierTextOf(Enum), {"E"}) &&
        identifierTextOf(Enum).contains('_')) {
      const SourceLocation Loc = nameLocation(Enum, SM);
      if (Loc.isValid())
        diag(Loc, "enum %0 should have the 'E' prefix followed by upper camel "
                  "case without underscores, which separate the names of its "
                  "enumerators from its own")
            << diagnosticName(Enum);
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
      if (!splitStoragePrefix(identifierTextOf(Type), Prefix, Rest) ||
          Prefix != "mc_" || !isUpperCamelWithConcepts(Rest)) {
        const SourceLocation Loc = nameLocation(Type, SM);
        if (Loc.isValid())
          diag(Loc, "enumerator %0 of a scoped enum should have the 'mc_' "
                    "prefix followed by upper camel case")
              << diagnosticName(Type);
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
      if (splitStoragePrefix(identifierTextOf(Type), Prefix, Rest))
        diag(Loc, "enumerator %0 of an unnamed enum is a constant; declare it "
                  "as a 'constexpr' variable instead")
            << diagnosticName(Type);
      else
        diag(Loc, "enumerator %0 of an unnamed enum should be in a named enum, "
                  "or be a 'constexpr' variable")
            << diagnosticName(Type);
      return;
    }
    // The name of an enumerator is the name of its enum, an underscore and
    // upper camel case, `EMode_Read` of `EMode`, also of one named by a
    // typedef, `typedef enum { ... } EMode;`. Like any group, the one after the
    // enum's name can be a built-in type of Malterlib, `EResultDataType_fp64`.
    const StringRef EnumName = Enum->getIdentifier()
                                   ? identifierTextOf(Enum)
                                   : identifierTextOf(Typedef);
    const StringRef Name = identifierTextOf(Type);
    if (Name.size() > EnumName.size() + 1 && Name.starts_with(EnumName) &&
        Name[EnumName.size()] == '_' &&
        hasUpperGroups(Name.drop_front(EnumName.size())))
      return;
    const SourceLocation Loc = nameLocation(Type, SM);
    if (Loc.isValid())
      diag(Loc, "enumerator %0 should have the name of its enum followed by "
                "an underscore, '%1_', and upper camel case")
          << diagnosticName(Type) << EnumName;
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
    // x509_store_st X509_STORE;`, and one that gives a type of `extern "C"`
    // its own name, `typedef struct DBusError DBusError;`, which is named by
    // the type, checked where it is declared: also when a header that isn't a
    // system header defines it, like libdbus's through a search path.
    if (Typedef->getLexicalDeclContext()->isExternCContext()) {
      const TagDecl *Tag = Typedef->getUnderlyingType()->getAsTagDecl();
      if (OfCLibrary(Tag))
        return;
      if (Tag && Tag->getIdentifier() &&
          identifierTextOf(Tag) == identifierTextOf(Typedef) &&
          llvm::any_of(Tag->redecls(), [](const TagDecl *Redecl) {
            return Redecl->getLexicalDeclContext()->isExternCContext();
          }))
        return;
    }
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
        hasTypePrefix(identifierTextOf(Typedef), {"F", "TF"})) {
      const SourceLocation Loc = nameLocation(Typedef, SM);
      if (Loc.isValid())
        diag(Loc, "type alias %0 has the 'F' prefix of function types, but is "
                  "not a function type or a function object")
            << diagnosticName(Typedef);
      return;
    }
    // Whether \p Canonical, a type that doesn't depend on template
    // parameters, is known not to be a function type, a pointer to one or a
    // function object.
    auto KnownNotFunction = [](QualType Canonical) {
      const QualType Referred = Canonical.getNonReferenceType();
      const std::optional<bool> CallOperator =
          hasCallOperator(Referred->getAsCXXRecordDecl());
      return !possiblyTrue(CallOperator) && !Referred->isFunctionType() &&
             !(Referred->isPointerType() &&
               Referred->getPointeeType()->isFunctionType()) &&
             !Referred->isMemberFunctionPointerType() &&
             !Referred->isBlockPointerType();
    };
    // A member alias of a class template, `using FValue = t_CType;`, is
    // what its instantiations make of it, like `int` of `TCContainer<int>`,
    // and so is a local alias of a function template.
    if (Underlying->isDependentType() &&
        hasTypePrefix(identifierTextOf(Typedef), {"F", "TF", "E"})) {
      // The aliases the instantiations make of it: of a member of a class
      // template, and of a local of a function template.
      llvm::SmallVector<const TypedefNameDecl *, 4> InstantiatedAliases;
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
      for (const TypedefNameDecl *Instantiated : InstantiatedAliases) {
        if (Instantiated->getUnderlyingType()->isDependentType())
          continue;
        const QualType Canonical =
            Instantiated->getUnderlyingType().getCanonicalType();
        // A report decided by this instantiation names it.
        const InstantiationScope Scope(*this, Instantiated);
        const SourceLocation Loc = nameLocation(Typedef, SM);
        if (hasTypePrefix(identifierTextOf(Typedef), {"F", "TF"}) &&
            KnownNotFunction(Canonical)) {
          if (Loc.isValid())
            diag(Loc, "type alias %0 has the 'F' prefix of function types, "
                      "but is not a function type or a function object")
                << diagnosticName(Typedef);
          return;
        }
        if (hasTypePrefix(identifierTextOf(Typedef), {"E"}) &&
            !Canonical->isEnumeralType()) {
          if (Loc.isValid())
            diag(Loc, "type alias %0 has the 'E' prefix of enums, but is not "
                      "an enum")
                << diagnosticName(Typedef);
          return;
        }
      }
    }
    // One of what depends on template parameters, also a member of what a
    // parameter is given, `using CCallback = typename t_CTraits::FSignature;`,
    // or what is computed from them, `TCConditional<..., typename
    // t_CMap::CUserData, CKey const>`, is generic: its generic name, `C`, is
    // always allowed, also when every instantiation of the translation unit
    // makes it a function type or an enum, and the instantiations only
    // exclude what is wrong in them, `F` or `E`, above.
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
          Referred->isBlockPointerType() || knownTrue(CallOperator);
      if (Function) {
        checkName(Typedef, SM, "function type alias",
                  Template ? llvm::ArrayRef<StringRef>{"TF"}
                           : llvm::ArrayRef<StringRef>{"F"});
        return;
      }
      auto Report = [&](StringRef Message) {
        const SourceLocation Loc = nameLocation(Typedef, SM);
        if (Loc.isValid())
          diag(Loc, Message) << diagnosticName(Typedef);
      };
      // A class that isn't defined in the translation unit, like a forward
      // declared `TCFunctionMovable<void ()>` or a specialization that isn't
      // instantiated, or one whose call operator can't be told, can be
      // either: it is checked where it is.
      const bool Unknown = !CallOperator;
      if (!FunctionLike && !Unknown &&
          hasTypePrefix(identifierTextOf(Typedef), {"F", "TF"})) {
        Report("type alias %0 has the 'F' prefix of function types, but is "
               "not a function type or a function object");
        return;
      }
      if (!Canonical->isEnumeralType() &&
          hasTypePrefix(identifierTextOf(Typedef), {"E"})) {
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
  const StringRef Name = identifierTextOf(Type);
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
          << Kind << diagnosticName(Type) << MinimumNameLength;
    return;
  }
  const SourceLocation Loc = nameLocation(Type, SM);
  if (Loc.isInvalid())
    return;
  diag(Loc, "%0 %1 should have the %2 prefix followed by upper camel case")
      << Kind << diagnosticName(Type) << quotedList(Expected);
}

} // namespace clang::tidy::malterlib
