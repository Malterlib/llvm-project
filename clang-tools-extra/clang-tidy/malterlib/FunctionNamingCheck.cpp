//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FunctionNamingCheck.h"
#include "../utils/OptionsUtils.h"
#include "CheckedAccess.h"
#include "NamingUtils.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/StringExtras.h"
#include <functional>
#include <optional>
#include <tuple>

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

FunctionNamingCheck::FunctionNamingCheck(StringRef Name,
                                         ClangTidyContext *Context)
    : MalterlibCheck(Name, Context),
      LowerCaseFunctions(utils::options::parseStringList(
          listOption("LowerCaseFunctions", DefaultLowerCaseFunctions))),
      LowerCaseFreeFunctions(utils::options::parseStringList(
          listOption("LowerCaseFreeFunctions", DefaultLowerCaseFreeFunctions))),
      MinimumNameLength(
          unsignedOption("MinimumNameLength", DefaultMinimumNameLength)) {}

void FunctionNamingCheck::storeCheckOptions(ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "LowerCaseFunctions",
                utils::options::serializeStringList(LowerCaseFunctions));
  Options.store(Opts, "LowerCaseFreeFunctions",
                utils::options::serializeStringList(LowerCaseFreeFunctions));
  Options.store(Opts, "MinimumNameLength", MinimumNameLength);
}

/// Whether \p Method, of a class template with a dependent base, overrides a
/// function of that base in an instantiation, like `f_Run` of
/// `TCDerived<CBase>` with `template <typename t_CBase> struct TCDerived :
/// t_CBase`, which the template itself can't tell, or may: nothing
/// instantiates it.
static bool overridesInInstantiation(const CXXMethodDecl *Method) {
  const CXXRecordDecl *Record = Method->getParent();
  if (Method->isStatic() ||
      llvm::none_of(basesOf(Record), [](const CXXBaseSpecifier &Base) {
        return Base.getType()->isDependentType();
      }))
    return false;
  // The template whose instantiations instantiate it: of its class, also of a
  // partial specialization, `template <typename t_CBase> struct
  // TCDerived<t_CBase *> : t_CBase`, whose instantiations are of its
  // template, or of a class it is nested in, `TCOuter<t_CBase>::CInner`.
  // Each template it is in, also around a template, `TCOuter<t_CBase>::
  // TCInner<t_CValue>`, whose instantiations instantiate the ones in it.
  llvm::SmallVector<const ClassTemplateDecl *, 2> Templates;
  for (const DeclContext *Context = Record; Context;
       Context = Context->getParent()) {
    const auto *Class = dyn_cast<CXXRecordDecl>(Context);
    if (!Class)
      continue;
    const auto *Partial = dyn_cast<ClassTemplatePartialSpecializationDecl>(Class);
    if (const ClassTemplateDecl *Template =
            Partial ? Partial->getSpecializedTemplate()
                    : Class->getDescribedClassTemplate())
      Templates.push_back(Template);
  }
  // Its instantiations in the classes of each specialization, also nested,
  // also in the specializations of the templates nested in them.
  bool Instantiated = false;
  TCRecursive<bool(const CXXRecordDecl *, int)> Overrides =
      [&](const CXXRecordDecl *Class, int Depth) {
        if (!Class || Depth > 16)
          return false;
        // Of those clang instantiated from it (isInstanceOf), not an
        // explicit specialization of the member, which is written out.
        for (const CXXMethodDecl *Instance : Class->methods())
          if (isInstanceOf(Instance, Method)) {
            Instantiated = true;
            if (Instance->size_overridden_methods() > 0)
              return true;
          }
        for (const Decl *Member : Class->decls()) {
          if (const auto *Nested = dyn_cast<CXXRecordDecl>(Member);
              Nested && !Nested->isImplicit() &&
              Overrides(Nested, Depth + 1))
            return true;
          if (const auto *NestedTemplate = dyn_cast<ClassTemplateDecl>(Member))
            for (const ClassTemplateSpecializationDecl *Specialization :
                 NestedTemplate->specializations())
              if (Overrides(Specialization, Depth + 1))
                return true;
        }
        return false;
      };
  for (const ClassTemplateDecl *Template : Templates)
    for (const ClassTemplateSpecializationDecl *Specialization :
         Template->specializations())
      if (Overrides(Specialization, 0))
        return true;
  // And of a local class of a function template, `struct CLocal :
  // tf_CBase`, in each instantiation of the function.
  if (Record->isLocalClass())
    for (const CXXRecordDecl *Instance : instancesOf(Record))
      if (Overrides(Instance, 0))
        return true;
  return !Instantiated;
}

void FunctionNamingCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(functionDecl().bind("function"), this);
}

/// Whether \p Method, or a function of the same name in a base, has the
/// annotation that lets a static member function be named as if it weren't.
/// With \p DecidedBy, the instantiation of a class whose dependent base
/// decided it, which a report decided by it names: the first that tells it.
static std::optional<bool>
namedAsMemberFunction(const CXXMethodDecl *Method,
                      const CXXRecordDecl *&DecidedBy) {
  auto Annotated = [](const Decl *D) {
    for (const Decl *Redeclaration : D->redecls())
      for (const auto *Attribute :
           Redeclaration->specific_attrs<AnnotateAttr>())
        if (Attribute->getAnnotation() == "malterlib-named-as-member-function")
          return true;
    return false;
  };
  if (Annotated(Method))
    return true;
  if (const FunctionDecl *Pattern = Method->getTemplateInstantiationPattern())
    if (Annotated(Pattern))
      return true;
  if (!Method->getDeclName().isIdentifier())
    return false;
  // Of each record what its bases tell, once at the least depth it is reached
  // at, as a deeper one can stop before an annotated base. An incomplete
  // one, a specialization nothing instantiates, can't tell.
  llvm::DenseMap<const CXXRecordDecl *, std::pair<std::optional<bool>, int>>
      Decided;
  TCRecursive<std::optional<bool>(const CXXRecordDecl *, int)> InBases =
      [&](const CXXRecordDecl *Record, int Depth) -> std::optional<bool> {
    if (!Record)
      return false;
    Record = Record->getDefinition();
    if (!Record || Depth > 16)
      return std::nullopt;
    if (auto [It, Inserted] =
            Decided.try_emplace(Record, std::optional<bool>(false), Depth);
        !Inserted) {
      if (It->second.second <= Depth)
        return It->second.first;
      It->second = {std::optional<bool>(false), Depth};
    }
    auto Decide = [&](std::optional<bool> Told) {
      Decided[Record] = {Told, Depth};
      return Told;
    };
    bool Unknown = false;
    bool Dependent = false;
    for (const CXXBaseSpecifier &Base : basesOf(Record)) {
      if (Base.getType()->isDependentType()) {
        Dependent = true;
        continue;
      }
      const CXXRecordDecl *BaseRecord = Base.getType()->getAsCXXRecordDecl();
      if (!BaseRecord || !BaseRecord->getDefinition())
        continue;
      // Also a member template, whose function has the annotation.
      for (const NamedDecl *Found :
           BaseRecord->getDefinition()->lookup(Method->getDeclName())) {
        // Of a member template instantiated with the base, also the one it
        // is instantiated from, whose definition can add it later.
        if (const auto *Template = dyn_cast<FunctionTemplateDecl>(Found)) {
          for (const FunctionTemplateDecl *From = Template; From;
               From = From->getInstantiatedFromMemberTemplate())
            if (Annotated(From->getTemplatedDecl()))
              return Decide(true);
          Found = Template->getTemplatedDecl();
        }
        if (Annotated(Found))
          return Decide(true);
        // Also of the template a base's member is instantiated from, whose
        // definition can add the annotation after the instantiation was
        // made, `template <typename t_CType> DMibNamedAsMemberFunction void
        // *TCAllocator<t_CType>::f_Alloc(...)`.
        if (const auto *Function = dyn_cast<FunctionDecl>(Found))
          if (const FunctionDecl *Pattern =
                  Function->getTemplateInstantiationPattern();
              Pattern && Annotated(Pattern))
            return Decide(true);
      }
      const std::optional<bool> Inherited = InBases(BaseRecord, Depth + 1);
      if (knownTrue(Inherited))
        return Decide(true);
      Unknown |= !Inherited;
    }
    // A base that depends on the parameters of the class template, `template
    // <typename t_CBase> struct TCDerived : t_CBase`, also of a partial
    // specialization or a member class of one, is what each instantiation
    // is given: what they tell, which only excludes what is wrong in them,
    // and nothing when none is instantiated, which can be named either way.
    if (Dependent) {
      const llvm::SmallVector<const CXXRecordDecl *, 4> Instances =
          instancesOf(Record);
      std::optional<bool> Combined;
      bool Mixed = false;
      // The first instantiation that tells each answer.
      const CXXRecordDecl *FirstNamed = nullptr;
      const CXXRecordDecl *FirstNot = nullptr;
      // Of the complete ones: one that can't be told doesn't cancel what
      // another tells.
      for (const CXXRecordDecl *Instance : Instances) {
        const std::optional<bool> Told = InBases(Instance, Depth + 1);
        if (!Told)
          continue;
        const CXXRecordDecl *&First = *Told ? FirstNamed : FirstNot;
        if (!First)
          First = Instance;
        if (Combined && *Combined != *Told)
          Mixed = true;
        Combined = Told;
      }
      if (Mixed) {
        if (!DecidedBy)
          DecidedBy = FirstNot;
        return Decide(false);
      }
      if (knownTrue(Combined)) {
        if (!DecidedBy)
          DecidedBy = FirstNamed;
        return Decide(true);
      }
      if (!Combined)
        Unknown = true;
    }
    return Decide(Unknown ? std::nullopt : std::optional(false));
  };
  return InBases(Method->getParent(), 0);
}

void FunctionNamingCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  const auto *Function = Result.Nodes.getNodeAs<FunctionDecl>("function");
  // Constructors, destructors, operators and conversions have no identifier.
  if (!Function->getDeclName().isIdentifier() || Function->isImplicit() ||
      !Function->isFirstDecl())
    return;
  if (Function->isTemplateInstantiation() ||
      Function->getTemplateSpecializationKind() == TSK_ExplicitSpecialization)
    return;
  // The names of these are given by the language or, for functions with C
  // linkage in C++, by the C code they are shared with.
  if (Function->isMain() || Function->isMSVCRTEntryPoint())
    return;
  if (Result.Context->getLangOpts().CPlusPlus &&
      (Function->isExternC() ||
         (Function->isInExternCContext() && !isa<CXXMethodDecl>(Function))))
    return;

  // The names of the standard library, like an overload of `std::swap`, and
  // of the members of specializations of external templates, like `parse` of
  // a `fmt::formatter<CValue>`, which the template gives.
  if (Function->isInStdNamespace() ||
      isInExternalSpecialization(Function, *Result.SourceManager))
    return;
  const StringRef Name = identifierTextOf(Function);
  // Only the protocols of the language and the standard library are lower
  // case, like `begin` and `await_ready`: public member functions, which what
  // uses the protocol calls, and the few that are free functions it finds by
  // argument dependent lookup, like `swap`, not a private member function,
  // `count`, nor any other free function, `clear`.
  {
    const auto *Member = dyn_cast<CXXMethodDecl>(Function);
    if (Member && Function->getFriendObjectKind() == Decl::FOK_None
            ? (Member->getAccess() == AS_public ||
               Member->getAccess() == AS_none) &&
                  llvm::is_contained(LowerCaseFunctions, Name)
            : llvm::is_contained(LowerCaseFreeFunctions, Name))
      return;
  }
  // A friend named with a qualifier, `friend void
  // TCProvider<t_CType>::f_Run();`, is a function declared elsewhere, whose
  // name is checked there.
  if (Function->getFriendObjectKind() != Decl::FOK_None &&
      Function->getQualifier())
    return;
  const SourceLocation Loc = nameLocation(Function, *Result.SourceManager);
  if (Loc.isInvalid())
    return;
  // Whether it is a member function that overrides one of a dependent base
  // in an instantiation, whose name isn't reported; told once, before the
  // first report, as it loads each member of each instantiation.
  std::optional<bool> OverridesInInstantiation;
  auto Overrides = [&] {
    if (!OverridesInInstantiation) {
      const auto *Member = dyn_cast<CXXMethodDecl>(Function);
      OverridesInInstantiation =
          Member && Function->getFriendObjectKind() == Decl::FOK_None &&
          overridesInInstantiation(Member);
    }
    return *OverridesInInstantiation;
  };

  // The prefixes it can have, the first the one the diagnostic suggests.
  llvm::SmallVector<std::string, 2> Prefixes;
  StringRef Kind;
  // Where the name has the prefix of another declaration, like `fp_Run` of a
  // public member function, the declaration can be what is wrong, and the
  // diagnostic tells either fix: what makes the declaration that one.
  std::string Otherwise;
  // Of a static member function named as if it weren't, whose staticness
  // isn't what decides its prefix, none to tell of that.
  bool StaticDecides = true;
  auto Declared = [&](bool Static, bool WasStatic, bool Private,
                      bool WasPrivate) {
    llvm::SmallVector<std::string, 2> Changes;
    // Of a name with the prefix of a static one, which a static member
    // function named as if it weren't can't have in any section, none.
    if (!StaticDecides && Static)
      return;
    if (Static != WasStatic && StaticDecides)
      Changes.push_back(WasStatic ? "not be declared static"
                                  : "be declared static");
    if (Private != WasPrivate)
      Changes.push_back(WasPrivate ? "be moved into the public section"
                                   : "be moved into the private or protected "
                                     "section");
    if (!Changes.empty())
      Otherwise = ", or " + llvm::join(Changes, " and ");
  };
  // The instantiation whose dependent base decided the prefixes it can have,
  // which the report of them names (namedAsMemberFunction).
  const CXXRecordDecl *DecidedBy = nullptr;
  const auto *Method = dyn_cast<CXXMethodDecl>(Function);
  if (Method && Function->getFriendObjectKind() == Decl::FOK_None) {
    // An override takes the name of the function it overrides, which is
    // checked where it is declared, or is the name of an external API, also
    // one of a dependent base that an instantiation overrides, which is told
    // only for a name that is reported (Overrides).
    if (Method->size_overridden_methods() > 0 ||
        Method->hasAttr<OverrideAttr>())
      return;
    const bool Private = Method->getAccess() == AS_private ||
                         Method->getAccess() == AS_protected;
    // A static one named as if it weren't, which shares a name with the
    // non-static functions of the same interface, like the `f_Alloc` of the
    // allocators: `DMibNamedAsMemberFunction`, `[[clang::annotate(
    // "malterlib-named-as-member-function")]]`, on it or on a function of
    // the same name in a base.
    // When a base it depends on can't be told, none instantiated, either.
    const std::optional<bool> NamedAsMember =
        Method->isStatic() ? namedAsMemberFunction(Method, DecidedBy)
                           : std::optional<bool>(false);
    StaticDecides = !possiblyTrue(NamedAsMember);
    std::string Base = "f";
    if (Method->isStatic() && !knownTrue(NamedAsMember))
      Base += "s";
    if (Private)
      Base += "p";
    Prefixes.push_back(Base + "_");
    if (!NamedAsMember)
      Prefixes.push_back(Private ? "fp_" : "f_");
    Kind = Method->isStatic() ? (Private ? "private or protected static member "
                                           "function"
                                         : "static member function")
                              : (Private ? "private or protected member "
                                           "function"
                                         : "member function");
    for (const auto &[Other, Static, OtherPrivate] :
         {std::tuple{"f_", false, false}, {"fp_", false, true},
          {"fs_", true, false}, {"fsp_", true, true}})
      if (Name.starts_with(Other))
        Declared(Static, Method->isStatic() && !possiblyTrue(NamedAsMember),
                 OtherPrivate, Private);
  } else {
    // One declared `static` can also have the prefix of any other, as in an
    // anonymous namespace: unlike a static member function, it is no other
    // kind of function, only one of the translation unit.
    const bool Static = Function->getStorageClass() == SC_Static;
    Prefixes.push_back("fg_");
    if (Static)
      Prefixes.push_back("fsg_");
    Kind = Static ? "static global function" : "global function";
    if (!Static && Name.starts_with("fsg_"))
      Declared(/*Static=*/true, /*WasStatic=*/false, false, false);
  }

  // Not a recursive one, `fgr_Name` or `fg_rName`, which a variable of a
  // function object can't be named as.
  StringRef Rest;
  if (const auto *Prefix = llvm::find_if(
          Prefixes, [&](StringRef Prefix) { return Name.starts_with(Prefix); });
      Prefix != Prefixes.end())
    Rest = Name.drop_front(Prefix->size());
  else {
    if (Overrides())
      return;
    llvm::SmallVector<StringRef, 2> Quoted(Prefixes.begin(), Prefixes.end());
    const InstantiationScope Scope(*this, DecidedBy);
    diag(Loc, "%0 %1 should have the %2 prefix%3")
        << Kind << diagnosticName(Function) << quotedList(Quoted) << Otherwise;
    // The rest of the name is checked too, so a name with several problems is
    // reported for all of them at once: after the function prefix it has, of
    // another kind of function, `fp_fopen_func`, or all of it, `cmake_main`.
    Rest = Name;
    for (StringRef Other : {"fspr_", "fsgr_", "fsp_", "fsg_", "fgr_", "fsr_",
                            "fpr_", "fp_", "fs_", "fg_", "fr_", "f_"})
      if (Name.starts_with(Other)) {
        Rest = Name.drop_front(Other.size());
        break;
      }
  }

  if (Rest.empty() || !(llvm::isUpper(Rest[0]) || llvm::isDigit(Rest[0])) ||
      !hasUpperGroups(Rest)) {
    if (!Overrides())
      diag(Loc, "%0 %1 should be upper camel case after its prefix")
          << Kind << diagnosticName(Function);
  } else if (Rest.size() < MinimumNameLength && !Overrides())
    diag(Loc, "%0 %1 should have at least %2 characters after its prefix")
        << Kind << diagnosticName(Function) << MinimumNameLength;
}

} // namespace clang::tidy::malterlib
