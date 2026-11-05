//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MemberOrderCheck.h"
#include "../GlobList.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/CharInfo.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

void MemberOrderCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      cxxRecordDecl(isDefinition(), unless(isLambda()), unless(isImplicit()))
          .bind("record"),
      this);
}

namespace {
/// The kinds of members, in the order a section has them.
enum class EMemberKind {
  Type,
  Constructor,
  Function,
  Constant,
  Variable,
};
} // namespace

/// The kind of \p Member, or none for what isn't ordered: friends, static
/// assertions, and using declarations that bring in members of several kinds.
static std::optional<EMemberKind> kindOf(const Decl *Member) {
  // Also a type brought in from what depends on the parameters, `using
  // typename t_CBase::CType;`.
  if (isa<TagDecl, TypedefNameDecl, TypeAliasTemplateDecl, ClassTemplateDecl,
          ClassTemplatePartialSpecializationDecl, UnresolvedUsingTypenameDecl>(
          Member))
    return EMemberKind::Type;
  const Decl *Templated = Member;
  if (const auto *Template = dyn_cast<FunctionTemplateDecl>(Member))
    Templated = Template->getTemplatedDecl();
  if (isa<CXXConstructorDecl, CXXDestructorDecl>(Templated))
    return EMemberKind::Constructor;
  if (isa<FunctionDecl>(Templated))
    return EMemberKind::Function;
  // Of a member variable template of an instantiation of a class template,
  // `using TCBase<int>::ms_Value;`, the one of the class template it is made
  // from, which is what is written: the instantiation's declaration has no
  // value.
  if (const auto *Template = dyn_cast<VarTemplateDecl>(Member)) {
    for (int Depth = 0; Depth < 16; ++Depth) {
      const VarTemplateDecl *From = Template->getInstantiatedFromMemberTemplate();
      if (!From)
        break;
      Template = From;
    }
    Templated = Template->getTemplatedDecl();
  }
  // The declaration of a variable in its class, not its definition out of
  // it, `template <typename t_CType> constexpr int CClass::ms_Value<t_CType> =
  // 1;`, which a using declaration or an instantiation can lead to.
  auto InClass = [](const VarDecl *Variable) {
    for (const VarDecl *Declaration : Variable->redecls())
      if (Declaration->getLexicalDeclContext()->isRecord())
        return Declaration;
    return Variable;
  };
  // A static member that is constexpr, also one declared const without its
  // value whose definition is constexpr, `static CStr const mc_Prefix;`, is a
  // constant, also when the translation unit doesn't have the definition. One
  // whose definition isn't constexpr, `int const CClass::ms_Value =
  // fg_Read();`, is a variable.
  // One whose type depends on the parameters of a template, `static T
  // ms_Value;`, can be either as it is instantiated, and isn't ordered.
  // Of a static member of a class template, or a member variable template,
  // also one a using declaration brings in, whose instantiations and
  // specializations can each be defined their own way, only what the
  // declaration in the class says: a constant when it is declared constexpr,
  // a variable when it is given a value that isn't, and not ordered when it
  // is declared without its value, `static int const ms_Value;`, or its type
  // depends on the parameters; not what an out-of-class definition says.
  // Also of a specialization that doesn't depend on them a using declaration
  // brings it in from, `using TCBase<int>::ms_Value;`, which is what its
  // declaration in the class template says, and a partial specialization of a
  // member variable template, `template <typename t_CType> static int const
  // ms_Value<t_CType *>;`.
  if (const auto *Variable = dyn_cast<VarDecl>(Templated);
      Variable && Variable->isStaticDataMember()) {
    if (const VarDecl *Pattern =
            Variable->getInstantiatedFromStaticDataMember())
      Variable = Pattern;
    else if (!isa<VarTemplateDecl, VarTemplatePartialSpecializationDecl>(
                 Member) &&
             !Variable->getDeclContext()->isDependentContext())
      Variable = nullptr;
    if (Variable)
      Variable = InClass(Variable);
    if (Variable) {
      if (Variable->isConstexpr())
        return EMemberKind::Constant;
      if (!Variable->hasInit() || Variable->getType()->isDependentType())
        return std::nullopt;
      return EMemberKind::Variable;
    }
  }
  if (const auto *Variable = dyn_cast<VarDecl>(Templated)) {
    if (llvm::any_of(Variable->redecls(), [](const VarDecl *Declaration) {
          return Declaration->isConstexpr();
        }))
      return EMemberKind::Constant;
    if (Variable->getType()->isDependentType())
      return std::nullopt;
    return Variable->getType().isConstQualified() && !Variable->hasInit() &&
                   !Variable->getDefinition()
               ? EMemberKind::Constant
               : EMemberKind::Variable;
  }
  // Also the storage of an anonymous struct or union, `union { int m_Value;
  // };`.
  // Also one of an anonymous struct or union a using declaration brings in,
  // `using CBase::m_Value;`.
  if (isa<FieldDecl, IndirectFieldDecl>(Member))
    return EMemberKind::Variable;
  // Also of the constructors of a base a class template is given, `using
  // t_CBase::t_CBase;`, which its name tells.
  if (const auto *Unresolved = dyn_cast<UnresolvedUsingValueDecl>(Member);
      Unresolved && Unresolved->getDeclName().getNameKind() ==
                        DeclarationName::CXXConstructorName)
    return EMemberKind::Constructor;
  // A using declaration is of what it brings in, `using CBase::CBase;` a
  // constructor.
  if (const auto *Using = dyn_cast<UsingDecl>(Member)) {
    std::optional<EMemberKind> Kind;
    for (const UsingShadowDecl *Shadow : Using->shadows()) {
      const std::optional<EMemberKind> Shadowed =
          isa<ConstructorUsingShadowDecl>(Shadow)
              ? std::optional<EMemberKind>(EMemberKind::Constructor)
              : kindOf(Shadow->getTargetDecl());
      if (!Shadowed || (Kind && *Kind != *Shadowed))
        return std::nullopt;
      Kind = Shadowed;
    }
    return Kind;
  }
  // An enumerator a using declaration brings in, `using EKind::EKind_First;`,
  // or a using enum declaration, `using enum EKind;`, are constants.
  if (isa<EnumConstantDecl, UsingEnumDecl>(Member))
    return EMemberKind::Constant;
  return std::nullopt;
}

namespace {
/// A NOLINT comment, as clang-tidy reads it: its kind, where it is, and the
/// checks it lists, none when it lists none, which is all of them.
struct CNoLint {
  StringRef Kind;
  size_t Pos;
  std::optional<std::string> Checks;
  /// Whether it covers \p Check, by clang-tidy's globs, without the negative
  /// ones, as clang-tidy reads them.
  bool covers(StringRef Check) const {
    return GlobList(Checks ? StringRef(*Checks) : StringRef("*"),
                    /*KeepNegativeGlobs=*/false)
        .contains(Check);
  }
};
} // namespace

/// The NOLINT comments of \p Buffer, read as clang-tidy reads them: `NOLINT`,
/// `NOLINTNEXTLINE`, `NOLINTBEGIN` or `NOLINTEND`, with the list of checks in
/// parentheses on the same line, its spaces around each check ignored.
static llvm::SmallVector<CNoLint, 4> readNoLints(StringRef Buffer) {
  llvm::SmallVector<CNoLint, 4> NoLints;
  size_t Pos = 0;
  while (Pos < Buffer.size()) {
    const size_t Found = Buffer.find("NOLINT", Pos);
    if (Found == StringRef::npos)
      break;
    Pos = Found + 6;
    while (Pos < Buffer.size() && isLetter(Buffer[Pos]))
      ++Pos;
    const StringRef Kind = Buffer.slice(Found, Pos);
    if (Kind != "NOLINT" && Kind != "NOLINTNEXTLINE" &&
        Kind != "NOLINTBEGIN" && Kind != "NOLINTEND")
      continue;
    std::optional<std::string> Checks;
    if (Pos < Buffer.size() && Buffer[Pos] == '(') {
      const size_t Close = Buffer.find_first_of("\n)", ++Pos);
      if (Close != StringRef::npos && Buffer[Close] == ')') {
        llvm::SmallVector<StringRef, 4> Split;
        Buffer.slice(Pos, Close).split(Split, ',');
        for (StringRef &Item : Split)
          Item = Item.trim();
        Checks = llvm::join(Split, ",");
        Pos = Close + 1;
      }
    }
    NoLints.push_back({Kind, Found, std::move(Checks)});
  }
  return NoLints;
}

bool MemberOrderCheck::coveredByNoLint(SourceLocation Loc,
                                       const SourceManager &SM) {
  const StringRef Check = getID();
  Loc = SM.getExpansionLoc(Loc);
  if (Loc.isInvalid())
    return false;
  const auto [File, Offset] = SM.getDecomposedLoc(Loc);
  bool Invalid = false;
  const StringRef Buffer = SM.getBufferData(File, &Invalid);
  if (Invalid || Offset > Buffer.size())
    return false;
  // In a `NOLINTBEGIN` and `NOLINTEND` block of the file that covers it, each
  // found once: as clang-tidy pairs them, an end with the last begin open if
  // their lists are the same; one that isn't paired covers nothing.
  auto [Blocks, Inserted] = NoLintBlocks.try_emplace(File);
  if (Inserted) {
    llvm::SmallVector<CNoLint, 4> Open;
    for (CNoLint &NoLint : readNoLints(Buffer)) {
      if (NoLint.Kind == "NOLINTBEGIN")
        Open.push_back(std::move(NoLint));
      else if (NoLint.Kind == "NOLINTEND" && !Open.empty() &&
               Open.back().Checks == NoLint.Checks) {
        if (Open.back().covers(Check))
          Blocks->second.push_back({Open.back().Pos, NoLint.Pos});
        Open.pop_back();
      }
    }
  }
  if (llvm::any_of(Blocks->second, [&](const std::pair<size_t, size_t> &Block) {
        return Block.first <= Offset && Offset < Block.second;
      }))
    return true;
  // On its line, `NOLINT`, or on the line before, `NOLINTNEXTLINE`.
  auto LineHas = [&](size_t Begin, size_t End, StringRef Kind) {
    return llvm::any_of(readNoLints(Buffer.slice(Begin, End)),
                        [&](const CNoLint &NoLint) {
                          return NoLint.Kind == Kind && NoLint.covers(Check);
                        });
  };
  // rfind looks before the offset it is given, which it leaves out.
  const size_t LineStart = Buffer.rfind('\n', Offset);
  const size_t Begin = LineStart == StringRef::npos ? 0 : LineStart + 1;
  const size_t End = std::min(Buffer.find('\n', Offset), Buffer.size());
  if (LineHas(Begin, End, "NOLINT"))
    return true;
  if (Begin == 0)
    return false;
  const size_t PreviousEnd = Begin - 1;
  const size_t PreviousNewline = Buffer.rfind('\n', PreviousEnd);
  const size_t PreviousBegin =
      PreviousNewline == StringRef::npos ? 0 : PreviousNewline + 1;
  return LineHas(PreviousBegin, PreviousEnd, "NOLINTNEXTLINE");
}

/// Whether \p Member is a friend that only declares, `friend struct CX;`,
/// `friend void fg_X();`, also a template, not one that defines a function in
/// the class, a hidden friend, nor one a macro writes.
static bool declaringFriend(const Decl *Member) {
  const auto *Friend = dyn_cast<FriendDecl>(Member);
  if (!Friend || Member->getBeginLoc().isMacroID() ||
      Member->getLocation().isMacroID())
    return false;
  if (const NamedDecl *Named = Friend->getFriendDecl()) {
    const Decl *Templated = Named;
    if (const auto *Template = dyn_cast<FunctionTemplateDecl>(Named))
      Templated = Template->getTemplatedDecl();
    if (const auto *Function = dyn_cast<FunctionDecl>(Templated))
      return !Function->isThisDeclarationADefinition();
  }
  return true;
}

static unsigned rankOf(AccessSpecifier Access) {
  switch (Access) {
  case AS_public:
    return 0;
  case AS_protected:
    return 1;
  default:
    return 2;
  }
}

static StringRef nameOf(AccessSpecifier Access) {
  switch (Access) {
  case AS_public:
    return "public";
  case AS_protected:
    return "protected";
  default:
    return "private";
  }
}

void MemberOrderCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  const auto *Record = Result.Nodes.getNodeAs<CXXRecordDecl>("record");
  const SourceManager &SM = *Result.SourceManager;
  // Not what a template is instantiated as, which is checked as written, also
  // a member class of an instantiation of its class, explicit too, `template
  // struct TCOuter<int>;`, nor a class a macro writes.
  if (isa<ClassTemplateSpecializationDecl>(Record) &&
      !isa<ClassTemplatePartialSpecializationDecl>(Record) &&
      !cast<ClassTemplateSpecializationDecl>(Record)
           ->isExplicitSpecialization())
    return;
  if (Record->getInstantiatedFromMemberClass() &&
      Record->getTemplateSpecializationKind() != TSK_ExplicitSpecialization)
    return;
  // Nor the copy an instantiation of its class makes of an explicit
  // specialization written in a class template, `template <> struct
  // TCInner<int> : t_CType`, which is in the instantiation; not one written
  // for an instantiated class, `template <> struct TCOuter<int>::CInner`,
  // which isn't in it.
  // Also in one written in another, `TCOuter<T>::TCMiddle<int>::TCInner<int>`,
  // whose copy is in a copy: by any class it is written in.
  for (const DeclContext *Lexical = Record->getLexicalDeclContext(); Lexical;
       Lexical = Lexical->getLexicalParent())
    if (const auto *Enclosing = dyn_cast<CXXRecordDecl>(Lexical);
        Enclosing &&
        isTemplateInstantiation(Enclosing->getTemplateSpecializationKind()))
      return;
  if (Record->getBeginLoc().isMacroID() || Record->getLocation().isMacroID() ||
      SM.isInSystemHeader(Record->getLocation()))
    return;

  // The section a struct or union starts with is public, and a class's private.
  AccessSpecifier Section = Record->isClass() ? AS_private : AS_public;
  // The latest section in order that has members or a label, none while the
  // one the class starts with has neither.
  std::optional<AccessSpecifier> Latest;
  llvm::SmallVector<std::pair<const Decl *, EMemberKind>, 32> Members;
  // Reports the fewest members of a section that have to move: those outside
  // its longest run in order. So a member that is kept early for what needs it
  // first is the one reported, and one NOLINT on it covers it.
  // A member a NOLINT covers, kept early for what needs it, is left out of
  // the run, so it doesn't make another the one reported.
  auto ReportSection = [&] {
    const size_t NumMembers = Members.size();
    llvm::SmallVector<bool, 32> Suppressed(NumMembers, false);
    for (size_t I = 0; I < NumMembers; ++I)
      Suppressed[I] = coveredByNoLint(Members[I].first->getLocation(), SM);
    llvm::SmallVector<unsigned, 32> Length(NumMembers, 1);
    llvm::SmallVector<int, 32> Previous(NumMembers, -1);
    std::optional<size_t> Longest;
    for (size_t I = 0; I < NumMembers; ++I) {
      if (Suppressed[I])
        continue;
      for (size_t J = 0; J < I; ++J)
        if (!Suppressed[J] && Members[J].second <= Members[I].second &&
            Length[J] + 1 > Length[I]) {
          Length[I] = Length[J] + 1;
          Previous[I] = static_cast<int>(J);
        }
      if (!Longest || Length[I] > Length[*Longest])
        Longest = I;
    }
    llvm::SmallVector<bool, 32> InOrder(NumMembers, false);
    for (int I = Longest ? static_cast<int>(*Longest) : -1; I >= 0;
         I = Previous[I])
      InOrder[I] = true;
    for (size_t I = 0; I < NumMembers; ++I) {
      if (InOrder[I] || Suppressed[I])
        continue;
      const EMemberKind Kind = Members[I].second;
      // The nearest kind in order before it that it should come before, or
      // else the nearest after it that it should come after.
      std::optional<EMemberKind> Before;
      for (size_t J = 0; J < I; ++J)
        if (InOrder[J] && Members[J].second > Kind &&
            (!Before || Members[J].second < *Before))
          Before = Members[J].second;
      std::optional<EMemberKind> After;
      if (!Before)
        for (size_t J = I + 1; J < NumMembers; ++J)
          if (InOrder[J] && Members[J].second < Kind &&
              (!After || Members[J].second > *After))
            After = Members[J].second;
      if (!Before && !After)
        continue;
      // Not named by the constructors of a base a class template is given,
      // whose name is of a parameter's type.
      const auto *Named = dyn_cast<NamedDecl>(Members[I].first);
      if (Named && Named->getDeclName().getNameKind() ==
                       DeclarationName::CXXConstructorName &&
          Named->getDeclName().getCXXNameType()->isDependentType())
        Named = nullptr;
      diag(Members[I].first->getLocation(),
           "%select{type|constructor or destructor|function|constant|variable}0"
           "%select{| %2}1 should come %select{before|after}3 the "
           "%select{types|constructors and destructor|functions|constants|"
           "variables}4 of its section; a section has types, constructors and "
           "destructor, functions, constants and variables, in that order")
          << static_cast<unsigned>(Kind)
          << (Named && Named->getDeclName() ? 1u : 0u) << Named
          << (Before ? 0u : 1u)
          << static_cast<unsigned>(Before ? *Before : *After);
    }
    Members.clear();
  };
  // Of a using declaration of what depends on the parameters, `using
  // T::f_Run;`, what the instantiations of the class make of it, when they
  // agree; not ordered when there is none, or they don't.
  // The instantiations a using declaration of what depends on the parameters
  // is told by: of a class template's own pattern, its specializations, also
  // of a partial specialization written outside a class template, those made
  // of it. In any other dependent context, a member class of a class
  // template, an explicit or partial specialization written in one, or a
  // class in those, there are none, and it isn't ordered.
  auto Instances = [&](const CXXRecordDecl *Of) {
    llvm::SmallVector<const ClassTemplateSpecializationDecl *, 4> Found;
    const ClassTemplateDecl *Template = Of->getDescribedClassTemplate();
    const auto *Partial = dyn_cast<ClassTemplatePartialSpecializationDecl>(Of);
    if (Partial)
      Template = Partial->getSpecializedTemplate();
    if (!Template || Of->getDeclContext()->isDependentContext())
      return Found;
    for (const ClassTemplateSpecializationDecl *Instance :
         Template->specializations()) {
      if (Instance->isExplicitSpecialization())
        continue;
      const auto From = Instance->getSpecializedTemplateOrPartial();
      if (Partial ? From.dyn_cast<ClassTemplatePartialSpecializationDecl *>() ==
                        Partial
                  : isa<ClassTemplateDecl *>(From))
        Found.push_back(Instance);
    }
    return Found;
  };
  auto KindOfMember = [&](const Decl *Member) -> std::optional<EMemberKind> {
    const auto *Unresolved = dyn_cast<UnresolvedUsingValueDecl>(Member);
    if (!Unresolved || Unresolved->getDeclName().getNameKind() ==
                           DeclarationName::CXXConstructorName)
      return kindOf(Member);
    std::optional<EMemberKind> Agreed;
    for (const CXXRecordDecl *Instance : Instances(Record)) {
      if (!Instance->hasDefinition())
        continue;
      for (const Decl *Instantiated : Instance->getDefinition()->decls()) {
        const auto *Using = dyn_cast<UsingDecl>(Instantiated);
        if (!Using || Result.Context->getInstantiatedFromUsingDecl(
                          const_cast<UsingDecl *>(Using)) != Unresolved)
          continue;
        const std::optional<EMemberKind> Kind = kindOf(Using);
        if (!Kind || (Agreed && *Agreed != *Kind))
          return std::nullopt;
        Agreed = Kind;
      }
    }
    return Agreed;
  };
  // A friend that only declares goes first in the private section, before its
  // types, or the protected one when the class has no private members; with
  // neither, of its own, it grants nothing. Reported elsewhere.
  // Its own members are what declares a member, not an unnamed bit-field, nor
  // what has no access of its own, like a partial or explicit specialization
  // of a member template, which has its template's, also one of a member
  // function template, `template <> void fp_Run<int>();`, which clang gives
  // the access of where it is written.
  // Also a partial or explicit specialization of a member variable template,
  // `template <> static constexpr int ms_Value<int> = 2;`.
  auto OwnMember = [&](const Decl *Member) {
    const auto *Field = dyn_cast<FieldDecl>(Member);
    // Nor what is written in it but declared in the namespace around it, a
    // tag an elaborated type specifier introduces, `struct COther *m_pOther;`.
    if (Member->getDeclContext() != Record && !isa<FriendDecl>(Member))
      return false;
    const auto *Function = dyn_cast<FunctionDecl>(Member);
    const auto *Variable = dyn_cast<VarDecl>(Member);
    return !Member->isImplicit() && !(Field && Field->isUnnamedBitField()) &&
           Member->getAccess() != AS_none &&
           !(Function && Function->getTemplateSpecializationKind() ==
                             TSK_ExplicitSpecialization) &&
           !isa<VarTemplateSpecializationDecl>(Member) &&
           !(Variable && Variable->getTemplateSpecializationKind() ==
                             TSK_ExplicitSpecialization) &&
           !isa<FriendDecl, AccessSpecDecl, StaticAssertDecl, EmptyDecl>(
               Member);
  };
  bool HasOwn[3] = {false, false, false};
  for (const Decl *Member : Record->decls())
    if (OwnMember(Member))
      HasOwn[rankOf(Member->getAccess())] = true;
  const std::optional<AccessSpecifier> FriendSection =
      HasOwn[rankOf(AS_private)]
          ? std::optional<AccessSpecifier>(AS_private)
          : (HasOwn[rankOf(AS_protected)]
                 ? std::optional<AccessSpecifier>(AS_protected)
                 : std::nullopt);
  bool SeenOwn[3] = {false, false, false};
  for (const Decl *Member : Record->decls()) {
    if (declaringFriend(Member)) {
      // At its `friend`.
      const SourceLocation FriendLoc = cast<FriendDecl>(Member)->getFriendLoc();
      // Only a friend class is unneeded there: a friend function can be what
      // makes the function found, by argument-dependent lookup, where no
      // declaration of it in its namespace is, and is left unordered.
      if (!FriendSection) {
        const NamedDecl *Friended = cast<FriendDecl>(Member)->getFriendDecl();
        // Of a class with no bases at all, whose friends a base can't give
        // access to more.
        if ((!Friended || isa<ClassTemplateDecl>(Friended)) &&
            Record->getNumBases() == 0)
          diag(FriendLoc,
               "friend declaration grants nothing; %0 has no private or "
               "protected members")
              << Record;
      } else if (Section != *FriendSection ||
                 SeenOwn[rankOf(*FriendSection)])
        diag(FriendLoc,
             "friend declaration should come first in the %0 section, before "
             "its types; a friend that only declares goes first in the "
             "private section, or the protected one when there are no "
             "private members")
            << nameOf(*FriendSection);
    } else if (OwnMember(Member))
      SeenOwn[rankOf(Section)] = true;
    // Not what is declared in the namespace around it, though written in it,
    // a tag an elaborated type specifier introduces, `struct COther
    // *m_pOther;`.
    if (Member->getDeclContext() != Record && !isa<FriendDecl>(Member))
      continue;
    // An anonymous struct or union is ordered by its storage, which is
    // implicit.
    const auto *Field = dyn_cast<FieldDecl>(Member);
    if (Member->isImplicit() && !(Field && Field->isAnonymousStructOrUnion()))
      continue;
    // Nor what a use of a member template instantiates, `mc_Value<int>`,
    // which isn't implicit but is ordered as the template.
    if (const auto *Variable = dyn_cast<VarTemplateSpecializationDecl>(Member);
        Variable && !Variable->isExplicitSpecialization())
      continue;
    if (const auto *Class = dyn_cast<ClassTemplateSpecializationDecl>(Member);
        Class && !Class->isExplicitSpecialization() &&
        !isa<ClassTemplatePartialSpecializationDecl>(Class))
      continue;
    if (const auto *Access = dyn_cast<AccessSpecDecl>(Member)) {
      // A label that repeats the access of the section it is in continues it.
      if (Access->getAccess() == Section) {
        if (!Latest)
          Latest = Section;
        continue;
      }
      ReportSection();
      Section = Access->getAccess();
      if (Latest && rankOf(Section) < rankOf(*Latest)) {
        if (Access->getLocation().isFileID())
          diag(Access->getLocation(),
               "the %0 section should come before the %1 section; a class has "
               "its public, protected and private sections in that order")
              << nameOf(Section) << nameOf(*Latest);
      } else
        Latest = Section;
      continue;
    }
    // An unnamed bit-field, `int : 0;`, is no member: neither ordered nor
    // what makes a section.
    if (Field && Field->isUnnamedBitField())
      continue;
    // What isn't a member doesn't make the section the class starts with one.
    if (!isa<FriendDecl, StaticAssertDecl, EmptyDecl>(Member) && !Latest)
      Latest = Section;
    // What a macro writes, which is ordered where the macro is used, and what
    // is no member, a friend or a static assertion. A member that only starts
    // with a macro, like an attribute, `inline_small void f_Run();`, is
    // ordered like any other.
    if (Member->getLocation().isMacroID() ||
        isa<FriendDecl, StaticAssertDecl, EmptyDecl, IndirectFieldDecl>(Member))
      continue;
    // The members of an anonymous struct or union are its variable's, and an
    // unnamed one is the type of the variable or alias it is written in,
    // `struct { int m_Value; } m_Header;`, which is ordered instead.
    if (const auto *Nested = dyn_cast<CXXRecordDecl>(Member);
        Nested && (Nested->isAnonymousStructOrUnion() || !Nested->getIdentifier()))
      continue;
    if (const std::optional<EMemberKind> Kind = KindOfMember(Member))
      Members.emplace_back(Member, *Kind);
  }
  ReportSection();
}

} // namespace clang::tidy::malterlib
