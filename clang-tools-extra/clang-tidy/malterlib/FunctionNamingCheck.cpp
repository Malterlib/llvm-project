//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FunctionNamingCheck.h"
#include <functional>
#include "../utils/OptionsUtils.h"
#include "NamingUtils.h"
#include "clang/AST/DeclCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/StringExtras.h"
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
/// t_CBase`, which the template itself can't tell.
static bool overridesInInstantiation(const CXXMethodDecl *Method) {
  const CXXRecordDecl *Record = Method->getParent();
  if (Method->isStatic() ||
      llvm::none_of(Record->bases(), [](const CXXBaseSpecifier &Base) {
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
  std::function<bool(const CXXRecordDecl *, int)> Overrides =
      [&](const CXXRecordDecl *Class, int Depth) {
        if (!Class || Depth > 16)
          return false;
        for (const CXXMethodDecl *Instance : Class->methods())
          if (const FunctionDecl *From =
                  Instance->getInstantiatedFromMemberFunction();
              From && From->getCanonicalDecl() == Method->getCanonicalDecl() &&
              Instance->size_overridden_methods() > 0)
            return true;
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
  return false;
}

void FunctionNamingCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(functionDecl().bind("function"), this);
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
  const StringRef Name = Function->getName();
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

  // The prefixes it can have, the first the one the diagnostic suggests.
  llvm::SmallVector<std::string, 2> Prefixes;
  StringRef Kind;
  // Where the name has the prefix of another declaration, like `fp_Run` of a
  // public member function, the declaration can be what is wrong, and the
  // diagnostic tells either fix: what makes the declaration that one.
  std::string Otherwise;
  auto Declared = [&](bool Static, bool WasStatic, bool Private,
                      bool WasPrivate) {
    llvm::SmallVector<std::string, 2> Changes;
    if (Static != WasStatic)
      Changes.push_back(WasStatic ? "not be declared static"
                                  : "be declared static");
    if (Private != WasPrivate)
      Changes.push_back(WasPrivate ? "be moved into the public section"
                                   : "be moved into the private or protected "
                                     "section");
    if (!Changes.empty())
      Otherwise = ", or " + llvm::join(Changes, " and ");
  };
  const auto *Method = dyn_cast<CXXMethodDecl>(Function);
  if (Method && Function->getFriendObjectKind() == Decl::FOK_None) {
    // An override takes the name of the function it overrides, which is
    // checked where it is declared, or is the name of an external API, also
    // one of a dependent base that an instantiation overrides.
    if (Method->size_overridden_methods() > 0 ||
        Method->hasAttr<OverrideAttr>() || overridesInInstantiation(Method))
      return;
    const bool Private = Method->getAccess() == AS_private ||
                         Method->getAccess() == AS_protected;
    std::string Base = "f";
    if (Method->isStatic())
      Base += "s";
    if (Private)
      Base += "p";
    Prefixes.push_back(Base + "_");
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
        Declared(Static, Method->isStatic(), OtherPrivate, Private);
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
    llvm::SmallVector<StringRef, 2> Quoted(Prefixes.begin(), Prefixes.end());
    diag(Loc, "%0 %1 should have the %2 prefix%3")
        << Kind << Function << quotedList(Quoted) << Otherwise;
    return;
  }

  if (Rest.empty() || !(llvm::isUpper(Rest[0]) || llvm::isDigit(Rest[0])) ||
      !hasUpperGroups(Rest))
    diag(Loc, "%0 %1 should be upper camel case after its prefix")
        << Kind << Function;
  else if (Rest.size() < MinimumNameLength)
    diag(Loc, "%0 %1 should have at least %2 characters after its prefix")
        << Kind << Function << MinimumNameLength;
}

} // namespace clang::tidy::malterlib
