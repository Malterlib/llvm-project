//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VariableNamingCheck.h"
#include "NamingUtils.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

void VariableNamingCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(varDecl(unless(parmVarDecl())).bind("variable"), this);
  Finder->addMatcher(fieldDecl().bind("field"), this);
  Finder->addMatcher(bindingDecl().bind("binding"), this);
}

/// Whether \p Variable is named as a constant: it is constexpr, also when
/// only another declaration is, like the definition of `extern const int
/// gc_Value;` or of `static const int mc_Value = 42;`. That it is const and
/// made by a constant expression doesn't make it one: it is declared
/// constexpr then, like what was written before constexpr was.
static bool isConstexpr(const VarDecl *Variable) {
  return llvm::any_of(Variable->redecls(), [](const VarDecl *Declaration) {
    return Declaration->isConstexpr();
  });
}

/// Whether \p Variable is a global declared const that is defined by another
/// declaration, `extern const int gc_Value;`, which is constexpr only if that
/// is, so that its name is checked where the definition is.
static bool isDefinedElsewhere(const VarDecl *Variable) {
  // Not constexpr itself: also when the definition is, `constexpr int
  // gc_Value = 42;`. A static member is named in its class, also when only
  // its definition is constexpr.
  // Also a reference, which is never const itself, `extern int
  // &gc_Reference;`.
  return (Variable->isFileVarDecl() || Variable->isLocalExternDecl()) &&
         (Variable->getType().isConstQualified() ||
          Variable->getType()->isReferenceType()) &&
         !(Variable->isStaticDataMember() ? isConstexpr(Variable)
                                          : Variable->isConstexpr()) &&
         !Variable->isThisDeclarationADefinition();
}

static bool isPrivate(AccessSpecifier Access) {
  return Access == AS_private || Access == AS_protected;
}

/// The access of \p Field, taking members of anonymous structs and unions
/// from the access of the anonymous member.
static AccessSpecifier fieldAccess(const FieldDecl *Field) {
  const RecordDecl *Parent = Field->getParent();
  while (Parent->isAnonymousStructOrUnion()) {
    if (isPrivate(Parent->getAccess()))
      return Parent->getAccess();
    const auto *Outer = dyn_cast<RecordDecl>(Parent->getDeclContext());
    if (!Outer)
      break;
    Parent = Outer;
  }
  return Field->getAccess();
}

void VariableNamingCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  const SourceManager &SM = *Result.SourceManager;
  const bool CPlusPlus = Result.Context->getLangOpts().CPlusPlus;

  if (const auto *Field = Result.Nodes.getNodeAs<FieldDecl>("field")) {
    if (!Field->getIdentifier() || Field->isImplicit() ||
        isa<ObjCIvarDecl>(Field) || isInExternalSpecialization(Field, SM))
      return;
    const SourceLocation Loc = nameLocation(Field, SM);
    if (Loc.isInvalid())
      return;
    // A member of an anonymous union outside of a class, like `union { int
    // Value; }` in a function, is a variable of the scope it is in: a local,
    // or a static global at namespace scope.
    const RecordDecl *Outermost = Field->getParent();
    while (Outermost->isAnonymousStructOrUnion())
      if (const auto *Outer = dyn_cast<RecordDecl>(Outermost->getDeclContext()))
        Outermost = Outer;
      else
        break;
    if (Outermost->isAnonymousStructOrUnion() &&
        !isa<RecordDecl>(Outermost->getDeclContext())) {
      if (Outermost->getDeclContext()->isFunctionOrMethod()) {
        // With the storage of the unnamed variable of the union, `static
        // union { int s_Value; };`.
        const auto IsObject = [&](const Decl *Member) {
          const auto *Object = dyn_cast<VarDecl>(Member);
          return Object && Object->isStaticLocal() &&
                 Object->getType()->getAsRecordDecl() == Outermost;
        };
        if (llvm::any_of(Outermost->getDeclContext()->decls(), IsObject))
          checkName(Field, Loc, "static local variable", {"s_"});
        else
          checkName(Field, Loc, "local variable", {""});
      }
      else {
        // At namespace scope, with the storage of the unnamed variable of the
        // union: `static union { int gs_Value; };`, or in an anonymous
        // namespace, `namespace { union { int g_Value; }; }`, which isn't
        // declared static.
        const auto IsStatic = [&](const Decl *Member) {
          const auto *Object = dyn_cast<VarDecl>(Member);
          return Object && Object->getStorageClass() == SC_Static &&
                 Object->getType()->getAsRecordDecl() == Outermost;
        };
        if (llvm::any_of(Outermost->getDeclContext()->decls(), IsStatic))
          checkName(Field, Loc, "static global variable", {"gs_"});
        else
          checkName(Field, Loc, "global variable", {"g_"});
      }
      return;
    }
    if (isPrivate(fieldAccess(Field)))
      checkName(Field, Loc, "private or protected member variable", {"mp_"});
    else
      checkName(Field, Loc, "member variable", {"m_"});
    return;
  }

  if (const auto *Binding = Result.Nodes.getNodeAs<BindingDecl>("binding")) {
    if (!Binding->getIdentifier())
      return;
    const auto *Decomposition =
        dyn_cast_or_null<VarDecl>(Binding->getDecomposedDecl());
    const SourceLocation Loc = nameLocation(Binding, SM);
    if (Loc.isInvalid())
      return;
    // Named for the storage of what it decomposes, like `g_First` of a global
    // or `s_First` of a static local, and as a constant when it is
    // constexpr.
    const bool Constant = Decomposition && isConstexpr(Decomposition);
    if (Decomposition && Decomposition->isFileVarDecl()) {
      const bool Static = Decomposition->getStorageClass() == SC_Static;
      checkName(Binding, Loc, "global structured binding",
                Constant ? "gc_" : (Static ? "gs_" : "g_"), "gc_");
      return;
    }
    if (Decomposition && Decomposition->isStaticLocal()) {
      checkName(Binding, Loc, "static structured binding",
                Constant ? "c_" : "s_", "c_");
      return;
    }
    checkName(Binding, Loc, "structured binding", Constant ? "c_" : "", "c_");
    return;
  }

  const auto *Variable = Result.Nodes.getNodeAs<VarDecl>("variable");
  if (!Variable || !Variable->getIdentifier() || Variable->isImplicit() ||
      isa<DecompositionDecl>(Variable))
    return;
  // Each is named where it is first declared, but one declared const before
  // it is defined, `extern const int gc_Value;`, also where it is defined.
  // Also an explicit specialization of a static member that is constexpr
  // where the member isn't, or the other way, `template <> constexpr int
  // TCBox<int>::ms_Value = 2;`, which the constant prefixes tell.
  const VarDecl *Specialized =
      Variable->getTemplateSpecializationKind() == TSK_ExplicitSpecialization &&
              Variable->isStaticDataMember()
          ? Variable->getInstantiatedFromStaticDataMember()
          : nullptr;
  const bool SpecializedConstness =
      Specialized && Variable->isConstexpr() != isConstexpr(Specialized);
  if (!Variable->isFirstDecl() && !SpecializedConstness &&
      (!Variable->isThisDeclarationADefinition() ||
       !isDefinedElsewhere(Variable->getFirstDecl())))
    return;
  // Not an instantiation of a variable template, which is named by it, but
  // an explicit specialization, which is written here.
  if (isTemplateInstantiation(Variable->getTemplateSpecializationKind()))
    return;
  if (CPlusPlus && Variable->isExternC())
    return;
  if (isInExternalSpecialization(Variable, SM))
    return;
  const SourceLocation Loc = nameLocation(Variable, SM);
  if (Loc.isInvalid())
    return;

  // Of a specialization that changes it, what it is itself.
  const bool Constant =
      SpecializedConstness ? Variable->isConstexpr() : isConstexpr(Variable);

  if (Variable->isStaticDataMember()) {
    // The identifier of a locale facet, `static std::locale::id id`, which
    // `std::use_facet` looks up by that name.
    if (const CXXRecordDecl *Type = Variable->getType()->getAsCXXRecordDecl();
        Type && Variable->getName() == "id" && Type->getIdentifier() &&
        Type->getName() == "id")
      if (const auto *Locale = dyn_cast<CXXRecordDecl>(Type->getDeclContext());
          Locale && Locale->getIdentifier() && Locale->getName() == "locale" &&
          Locale->isInStdNamespace())
        return;
    const bool Private = isPrivate(Variable->getAccess());
    checkName(Variable, Loc,
              Private ? "private or protected static member variable"
                      : "static member variable",
              Constant ? (Private ? "mcp_" : "mc_") : (Private ? "msp_" : "ms_"),
              Private ? "mcp_" : "mc_");
    return;
  }

  // A block scope `extern` declares a global.
  if (Variable->isFileVarDecl() || Variable->isLocalExternDecl()) {
    // An explicit specialization of a variable template, `template <> int
    // gs_Value<int>`, is named by its template and can't repeat `static`:
    // it has the storage of the template.
    const auto *Specialization =
        dyn_cast<VarTemplateSpecializationDecl>(Variable);
    const VarDecl *Storage =
        Specialization ? Specialization->getSpecializedTemplate()
                             ->getTemplatedDecl()
                       : Variable;
    const bool Static = Storage->getStorageClass() == SC_Static;
    // A const one defined by another declaration, `extern const int
    // gc_Value;`, is constexpr if its definition is, which is checked where
    // that is.
    if (isDefinedElsewhere(Variable)) {
      checkName(Variable, Loc, "global variable",
                llvm::ArrayRef<StringRef>({"g_", "gc_"}));
      return;
    }
    checkName(Variable, Loc,
              Constant ? "global constant"
                       : (Static ? "static global variable" : "global variable"),
              Constant ? "gc_" : (Static ? "gs_" : "g_"), "gc_");
    return;
  }

  if (Variable->isStaticLocal()) {
    checkName(Variable, Loc, "static local variable", Constant ? "c_" : "s_",
              "c_");
    return;
  }

  // Catch parameters are named like parameters.
  if (Variable->isExceptionVariable()) {
    checkName(Variable, Loc, "exception variable", "_", "");
    return;
  }

  // Local variables, including init captures, also of a pack, which are
  // locals rather than parameters, and the variables of range-based for loops
  // and conditions.
  checkName(Variable, Loc, "local variable", Constant ? "c_" : "", "c_");
}

void VariableNamingCheck::checkName(const NamedDecl *Variable,
                                    SourceLocation Loc, StringRef Kind,
                                    StringRef Expected,
                                    StringRef ConstantPrefix) {
  // Named as a constant, but not constexpr, like `const int gc_Value = 42`
  // written before constexpr was.
  StringRef Prefix, Rest;
  if (Expected != ConstantPrefix &&
      splitStoragePrefix(Variable->getName(), Prefix, Rest) &&
      Prefix == ConstantPrefix) {
    if (Expected.empty())
      diag(Loc, "%0 %1 has the constant prefix '%2' but isn't constexpr; "
                "declare it constexpr, or name it without a storage prefix")
          << Kind << Variable << ConstantPrefix;
    else
      diag(Loc, "%0 %1 has the constant prefix '%2' but isn't constexpr; "
                "declare it constexpr, or use the '%3' prefix")
          << Kind << Variable << ConstantPrefix << Expected;
    return;
  }
  checkName(Variable, Loc, Kind, llvm::ArrayRef<StringRef>(Expected));
}

void VariableNamingCheck::checkName(const NamedDecl *Variable,
                                    SourceLocation Loc, StringRef Kind,
                                    llvm::ArrayRef<StringRef> Expected) {
  const StringRef Name = Variable->getName();
  StringRef Prefix, Rest = Name;
  const bool HasPrefix = splitStoragePrefix(Name, Prefix, Rest);
  if (!llvm::is_contained(Expected, HasPrefix ? Prefix : StringRef())) {
    if (Expected.size() == 1 && Expected[0].empty())
      diag(Loc, "%0 %1 should not have a storage prefix") << Kind << Variable;
    else if (llvm::is_contained(Expected, StringRef())) {
      llvm::SmallVector<StringRef, 2> Prefixes;
      llvm::copy_if(Expected, std::back_inserter(Prefixes),
                    [](StringRef Prefix) { return !Prefix.empty(); });
      diag(Loc, "%0 %1 should have no storage prefix or the %2 prefix")
          << Kind << Variable << quotedList(Prefixes);
    } else
      diag(Loc, "%0 %1 should have the %2 prefix")
          << Kind << Variable << quotedList(Expected);
    return;
  }
  if (const StringRef Wrong = wrongConceptPrefix(Rest); !Wrong.empty())
    diag(Loc, "%0 %1 has %select{the prefix '%2', which isn't a concept prefix; remove "
              "it or use %3|the concept prefixes '%2', of which only the "
              "outermost, '%3', is written}4")
        << Kind << Variable << Wrong
        << (Wrong.size() == 1 ? quotedConceptPrefixes() : Wrong.take_front(1).str())
        << (Wrong.size() == 2);
  else if (!isUpperCamelWithConcepts(Rest))
    diag(Loc, "%0 %1 should be upper camel case%select{| after its prefix}2")
        << Kind << Variable << HasPrefix;
  else if (withoutConcept(Rest).size() < MinimumNameLength)
    diag(Loc, "%0 %1 should have at least %2 characters after its prefixes")
        << Kind << Variable << MinimumNameLength;
}

} // namespace clang::tidy::malterlib
