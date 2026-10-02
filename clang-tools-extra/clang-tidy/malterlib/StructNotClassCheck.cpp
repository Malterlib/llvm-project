//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "StructNotClassCheck.h"
#include "NamingUtils.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"

using namespace clang::ast_matchers;

namespace clang::tidy::malterlib {

namespace {
/// Records the text the preprocessor skips, and where the conditional
/// directives are.
class SkippedRangeCollector : public PPCallbacks {
public:
  SkippedRangeCollector(llvm::SmallVectorImpl<SourceRange> &Ranges,
                        llvm::SmallVectorImpl<SourceLocation> &Directives)
      : Ranges(Ranges), Directives(Directives) {}
  void SourceRangeSkipped(SourceRange Range, SourceLocation) override {
    Ranges.push_back(Range);
  }
  void If(SourceLocation Loc, SourceRange, ConditionValueKind) override {
    Directives.push_back(Loc);
  }
  void Elif(SourceLocation Loc, SourceRange, ConditionValueKind,
            SourceLocation) override {
    Directives.push_back(Loc);
  }
  void Ifdef(SourceLocation Loc, const Token &,
             const MacroDefinition &) override {
    Directives.push_back(Loc);
  }
  void Ifndef(SourceLocation Loc, const Token &,
              const MacroDefinition &) override {
    Directives.push_back(Loc);
  }
  void Elifdef(SourceLocation Loc, const Token &,
               const MacroDefinition &) override {
    Directives.push_back(Loc);
  }
  void Elifdef(SourceLocation Loc, SourceRange, SourceLocation) override {
    Directives.push_back(Loc);
  }
  void Elifndef(SourceLocation Loc, const Token &,
                const MacroDefinition &) override {
    Directives.push_back(Loc);
  }
  void Elifndef(SourceLocation Loc, SourceRange, SourceLocation) override {
    Directives.push_back(Loc);
  }
  void Else(SourceLocation Loc, SourceLocation) override {
    Directives.push_back(Loc);
  }
  void Endif(SourceLocation Loc, SourceLocation) override {
    Directives.push_back(Loc);
  }

private:
  llvm::SmallVectorImpl<SourceRange> &Ranges;
  llvm::SmallVectorImpl<SourceLocation> &Directives;
};
} // namespace

void StructNotClassCheck::registerPPCallbacks(const SourceManager &,
                                              Preprocessor *PP,
                                              Preprocessor *) {
  PP->addPPCallbacks(std::make_unique<SkippedRangeCollector>(
      SkippedRanges, ConditionalDirectives));
}

/// Whether a conditional directive of \p Directives is between \p Begin and
/// \p End. The preprocessor reports them in the order of the translation
/// unit, which a binary search finds the first after \p Begin in.
static bool hasConditionalDirective(llvm::ArrayRef<SourceLocation> Directives,
                                    SourceLocation Begin, SourceLocation End,
                                    const SourceManager &SM) {
  const SourceLocation *First =
      llvm::partition_point(Directives, [&](SourceLocation Directive) {
        return !SM.isBeforeInTranslationUnit(Begin, Directive);
      });
  return First != Directives.end() && SM.isBeforeInTranslationUnit(*First, End);
}

/// Whether \p Loc is in a range of \p Ranges, which the preprocessor skipped,
/// and reports in the order of the translation unit, without overlaps.
static bool isSkipped(llvm::ArrayRef<SourceRange> Ranges, SourceLocation Loc,
                      const SourceManager &SM) {
  const SourceRange *After =
      llvm::partition_point(Ranges, [&](SourceRange Range) {
        return !SM.isBeforeInTranslationUnit(Loc, Range.getBegin());
      });
  return After != Ranges.begin() &&
         SM.isBeforeInTranslationUnit(Loc, std::prev(After)->getEnd());
}

void StructNotClassCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(cxxRecordDecl().bind("record"), this);
  Finder->addMatcher(friendDecl().bind("friend"), this);
  Finder->addMatcher(typeLoc(loc(tagType())).bind("elaborated"), this);
  Finder->addMatcher(
      typeLoc(loc(dependentNameType())).bind("dependentElaborated"), this);
  Finder->addMatcher(typeLoc(loc(templateSpecializationType()))
                         .bind("elaboratedTemplate"),
                     this);
}

/// The definition whose tag the tag of \p Tag has to match: of the class, or
/// of the template or partial specialization an implicit specialization like
/// `TCThing<int>` is made from, or
/// of the class of the template a class of an instantiation like
/// `TCOuter<int>::CInner` is made from, which a fix of the template changes
/// too.
static const CXXRecordDecl *taggedDefinition(const TagDecl *Tag) {
  const auto *Record = dyn_cast_or_null<CXXRecordDecl>(Tag);
  if (!Record)
    return nullptr;
  if (const auto *Specialization =
          dyn_cast<ClassTemplateSpecializationDecl>(Record);
      Specialization && !Specialization->isExplicitSpecialization() &&
      !isa<ClassTemplatePartialSpecializationDecl>(Record)) {
    // Of the partial specialization it is made from, `TCBox<int *>` of
    // `TCBox<t_CType *>`, which can be declared with another tag.
    const auto From = Specialization->getSpecializedTemplateOrPartial();
    if (const auto *Partial =
            From.dyn_cast<ClassTemplatePartialSpecializationDecl *>())
      Record = Partial;
    else
      Record = Specialization->getSpecializedTemplate()->getTemplatedDecl();
  }
  for (int Depth = 0; Depth < 64; ++Depth) {
    const CXXRecordDecl *Pattern = Record->getInstantiatedFromMemberClass();
    if (!Pattern || Record->getTemplateSpecializationKind() ==
                        TSK_ExplicitSpecialization)
      break;
    Record = Pattern;
  }
  return Record->getDefinition();
}

/// Whether \p Record is a type with an external name: it is defined in a
/// system header or specializes a template from one.
static bool isExternalType(const CXXRecordDecl *Record,
                           const SourceManager &SM) {
  if (const CXXRecordDecl *Definition = Record->getDefinition();
      Definition && SM.isInSystemHeader(Definition->getLocation()))
    return true;
  if (const auto *Specialization =
          dyn_cast<ClassTemplateSpecializationDecl>(Record))
    return SM.isInSystemHeader(
        Specialization->getSpecializedTemplate()->getLocation());
  return false;
}

/// The first declaration written in the body of \p Record.
static const Decl *firstWrittenMember(const CXXRecordDecl *Record) {
  for (const Decl *Member : Record->decls()) {
    if (!Member->isImplicit())
      return Member;
  }
  return nullptr;
}

/// Whether the body of \p Record has something written before \p Loc, in its
/// file, which the AST doesn't have: a macro that expands to nothing here,
/// `DPrivateMembers`, but can declare members in another configuration, also
/// one that gives its opening brace.
static bool writtenBefore(const CXXRecordDecl *Record, SourceLocation Loc,
                          const SourceManager &SM) {
  const SourceLocation Brace = Record->getBraceRange().getBegin();
  // A brace from a macro, `struct CObject DOpen`, can come with members in
  // another configuration, `{ private: int mp_Secret;`.
  if (!Brace.isFileID())
    return true;
  if (!Loc.isFileID() || SM.getFileID(Brace) != SM.getFileID(Loc))
    return false;
  const StringRef Buffer = SM.getBufferData(SM.getFileID(Brace));
  Lexer Raw(SM.getLocForStartOfFile(SM.getFileID(Brace)),
            Record->getASTContext().getLangOpts(), Buffer.begin(),
            Buffer.begin() + SM.getFileOffset(Brace) + 1, Buffer.end());
  Token Next;
  return !Raw.LexFromRawLexer(Next) && Next.getLocation() != Loc &&
         SM.isBeforeInTranslationUnit(Next.getLocation(), Loc);
}

/// Whether \p Definition, a class, keeps its meaning as a struct, which makes
/// the members and bases public by default: every base names its access and
/// no member comes before the first access specifier.
static bool canBeStruct(const CXXRecordDecl *Definition) {
  if (llvm::any_of(Definition->bases(), [](const CXXBaseSpecifier &Base) {
        return Base.getAccessSpecifierAsWritten() == AS_none;
      }))
    return false;
  const Decl *First = firstWrittenMember(Definition);
  return !First || isa<AccessSpecDecl>(First);
}

/// Whether the fix can turn \p Definition into a struct: it keeps its meaning
/// as one, and its keyword is not from a macro, which other types can share.
/// Every other declaration of the type with `class` is fixed with it, so they
/// have to be in the file of the definition, whose fixes are applied
/// together, and not have their keywords from macros either. A declaration
/// left with `class`, like a forward declaration in a header that is not
/// fixed, would mismatch the tag of the definition.
static bool canFixDefinition(const CXXRecordDecl *Definition,
                             const SourceManager &SM,
                             llvm::ArrayRef<SourceLocation> Directives) {
  if (!canBeStruct(Definition) || !Definition->getInnerLocStart().isFileID())
    return false;
  // Nor one with conditional preprocessing in its body, `#ifdef DLayout`,
  // whose skipped branches can have members before the first access
  // specifier that the fix would make public.
  // Also in its bases, `class CObject : #ifdef DPrivate CBase #else public
  // CBase #endif`, whose default access the fix would change, and a base from
  // a macro, which can leave the access out.
  const SourceRange Braces = Definition->getBraceRange();
  if (Braces.isInvalid() ||
      hasConditionalDirective(Directives, Definition->getInnerLocStart(),
                              Braces.getEnd(), SM) ||
      llvm::any_of(Definition->bases(), [](const CXXBaseSpecifier &Base) {
        return Base.getBeginLoc().isMacroID();
      }))
    return false;
  // Nor one with what expands to nothing before its first access specifier,
  // `DPrivateMembers`, which can declare members another configuration has.
  // Nor one whose first access specifier comes from a macro, `DInterface`
  // expanding to `public:`, which another configuration can define to
  // nothing.
  const auto *First =
      dyn_cast_or_null<AccessSpecDecl>(firstWrittenMember(Definition));
  if (First && First->getAccessSpecifierLoc().isMacroID())
    return false;
  if (writtenBefore(Definition,
                    First ? First->getAccessSpecifierLoc() : Braces.getEnd(),
                    SM))
    return false;
  const FileID File = SM.getFileID(Definition->getInnerLocStart());
  return llvm::all_of(Definition->redecls(), [&](const TagDecl *Redecl) {
    const SourceLocation Keyword = Redecl->getInnerLocStart();
    return !Redecl->isClass() ||
           (Keyword.isFileID() && SM.getFileID(Keyword) == File);
  });
}

/// The range of the `public:` of \p Access, with the whitespace that follows
/// it up to the next line.
static CharSourceRange accessRange(const AccessSpecDecl *Access) {
  return CharSourceRange::getCharRange(
      Access->getAccessSpecifierLoc(),
      Access->getColonLoc().getLocWithOffset(1));
}

static bool namedThroughSpecializedTemplate(QualType Type);
static const CXXRecordDecl *namedRecord(QualType Type);

/// The class or class template named \p Name in the class \p Qualifier
/// names, `TCOuter<t_CType>::CInner` of the template of that one, also of a
/// base it inherits, `struct TCOuter : CBase`.
static const NamedDecl *nestedIn(NestedNameSpecifier Qualifier,
                                 const IdentifierInfo *Name) {
  const clang::Type *Outer =
      Qualifier.getKind() == NestedNameSpecifier::Kind::Type
          ? Qualifier.getAsType()
          : nullptr;
  const CXXRecordDecl *Scope =
      Outer && Name ? namedRecord(QualType(Outer, 0)) : nullptr;
  llvm::SmallVector<const CXXRecordDecl *, 4> Scopes;
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
  if (Scope)
    Scopes.push_back(Scope);
  while (!Scopes.empty()) {
    const CXXRecordDecl *Current = Scopes.pop_back_val();
    if (!Current || !Current->hasDefinition() ||
        !Seen.insert(Current->getDefinition()).second)
      continue;
    Current = Current->getDefinition();
    for (const NamedDecl *Found : Current->lookup(Name))
      if (isa<CXXRecordDecl, ClassTemplateDecl>(Found))
        return Found;
    // Also a base that depends on parameters of the template, of its
    // template, `struct TCOuter : TCBase<t_CType>`, unless that is
    // specialized, which its instantiations can declare it in otherwise.
    for (const CXXBaseSpecifier &Base : Current->bases())
      if (!namedThroughSpecializedTemplate(Base.getType()))
        Scopes.push_back(namedRecord(Base.getType()));
  }
  return nullptr;
}

/// The record \p Type names, also the primary template of a dependent
/// specialization, like `TCThing<t_CType>`.
static const CXXRecordDecl *namedRecord(QualType Type) {
  if (const CXXRecordDecl *Record = Type->getAsCXXRecordDecl())
    return Record;
  if (const auto *Specialization = Type->getAs<TemplateSpecializationType>()) {
    const TemplateName Name = Specialization->getTemplateName();
    if (const auto *Template =
            dyn_cast_or_null<ClassTemplateDecl>(Name.getAsTemplateDecl()))
      return Template->getTemplatedDecl();
    // A class template nested in a dependent class, `TCOuter<t_CType>::template
    // TCInner<int>`, of the template of that one.
    if (const DependentTemplateName *Dependent =
            Name.getAsDependentTemplateName())
      if (const auto *Template = dyn_cast_or_null<ClassTemplateDecl>(nestedIn(
              Dependent->getQualifier(), Dependent->getName().getIdentifier())))
        return Template->getTemplatedDecl();
    return nullptr;
  }
  // A class nested in a dependent one, `TCOuter<t_CType>::CInner`.
  if (const auto *Dependent = Type->getAs<DependentNameType>())
    return dyn_cast_or_null<CXXRecordDecl>(
        nestedIn(Dependent->getQualifier(), Dependent->getIdentifier()));
  return nullptr;
}

/// Whether dependent \p Type, like `TCOuter<t_CType>::CInner` or
/// `TCThing<t_CType>`, is named through a template that is specialized,
/// explicitly or partially, whose specializations can declare it with
/// another tag than the template does.
static bool namedThroughSpecializedTemplate(QualType Type) {
  const clang::Type *Named = Type.getTypePtr();
  // Of a class template nested in it, `TCOuter<t_CType>::template
  // TCInner<int>`, through the class it is nested in.
  const DependentTemplateName *DependentTemplate = nullptr;
  if (const auto *Specialization = Type->getAs<TemplateSpecializationType>())
    DependentTemplate =
        Specialization->getTemplateName().getAsDependentTemplateName();
  if (const auto *Dependent = Type->getAs<DependentNameType>();
      Dependent || DependentTemplate) {
    const NestedNameSpecifier Qualifier =
        Dependent ? Dependent->getQualifier()
                  : DependentTemplate->getQualifier();
    Named = Qualifier.getKind() == NestedNameSpecifier::Kind::Type
                ? Qualifier.getAsType()
                : nullptr;
    // Also through a class nested deeper, `TCOuter<t_CType>::CNested::CInner`,
    // whose qualifier is the class of the template, or a name in it.
    if (Named && Named->getAs<DependentNameType>())
      return namedThroughSpecializedTemplate(QualType(Named, 0));
  }
  const auto *Specialization =
      Named ? Named->getAs<TemplateSpecializationType>() : nullptr;
  const auto *Template =
      Specialization ? dyn_cast_or_null<ClassTemplateDecl>(
                           Specialization->getTemplateName().getAsTemplateDecl())
                     : nullptr;
  if (!Template && Named &&
      (Type->getAs<DependentNameType>() || DependentTemplate))
    for (const auto *Class = Named->getAsCXXRecordDecl(); Class && !Template;
         Class = dyn_cast<CXXRecordDecl>(Class->getDeclContext()))
      Template = Class->getDescribedClassTemplate();
  if (!Template)
    return false;
  llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 2> Partials;
  const_cast<ClassTemplateDecl *>(Template)->getPartialSpecializations(Partials);
  return !Partials.empty() ||
         llvm::any_of(Template->specializations(),
                      [](const ClassTemplateSpecializationDecl *Explicit) {
                        return Explicit->isExplicitSpecialization();
                      });
}

void StructNotClassCheck::reportElaborated(const CXXRecordDecl *Record,
                                           SourceLocation Keyword) {
  const SourceLocation Loc = reportLocation(Keyword, *SM);
  if (Loc.isInvalid())
    return;
  auto Diag = diag(Loc, "%0 is a struct, but is named with 'class' here; "
                        "name it with 'struct'")
              << Record;
  if (Keyword.isFileID())
    Diag << FixItHint::CreateReplacement(
        CharSourceRange::getTokenRange(Keyword, Keyword), "struct");
}

void StructNotClassCheck::check(const MatchFinder::MatchResult &Result) {
  if (Repeated.repeated(Result))
    return;
  const SourceManager &SM = *Result.SourceManager;
  this->SM = &SM;
  const LangOptions &LangOpts = Result.Context->getLangOpts();

  // The type of a friend declaration, like `friend class CThing;`, is
  // reported with the friend.
  for (const char *Name :
       {"elaboratedTemplate", "elaborated", "dependentElaborated"}) {
    if (const auto *Loc = Result.Nodes.getNodeAs<TypeLoc>(Name)) {
      const auto Parents = instantiatedParents(*Loc, *Result.Context);
      if (!Parents.empty() && Parents[0].get<FriendDecl>())
        return;
    }
  }

  // A specialization of a class template, like `class TCThing<int> *`.
  if (const auto *Loc = Result.Nodes.getNodeAs<TypeLoc>("elaboratedTemplate")) {
    // Also qualified, `class TCThing<int> const &`.
    const auto SpecializationLoc =
        Loc->getUnqualifiedLoc().getAs<TemplateSpecializationTypeLoc>();
    if (!SpecializationLoc ||
        SpecializationLoc.getTypePtr()->getKeyword() !=
            ElaboratedTypeKeyword::Class)
      return;
    // One nested in a dependent class, `class TCOuter<t_CType>::template
    // TCInner<int>`, of the template of that one, unless that is
    // specialized, which can declare it with another tag.
    if (SpecializationLoc.getTypePtr()
            ->getTemplateName()
            .getAsDependentTemplateName() &&
        namedThroughSpecializedTemplate(Loc->getType()))
      return;
    const CXXRecordDecl *Record =
        namedRecord(QualType(SpecializationLoc.getTypePtr(), 0));
    const CXXRecordDecl *Definition = taggedDefinition(Record);
    if (!Definition || isExternalType(Record, SM))
      return;
    const SourceLocation Keyword = SpecializationLoc.getElaboratedKeywordLoc();
    if (!Definition->isClass()) {
      reportElaborated(Record, Keyword);
      return;
    }
    ElaboratedKeywords[Definition].push_back(Keyword.isFileID() ? Keyword
                                                               : SourceLocation());
    return;
  }

  if (const auto *Loc = Result.Nodes.getNodeAs<TypeLoc>("elaborated")) {
    const auto TagLoc = Loc->getUnqualifiedLoc().getAs<TagTypeLoc>();
    if (!TagLoc ||
        TagLoc.getTypePtr()->getKeyword() != ElaboratedTypeKeyword::Class)
      return;
    const CXXRecordDecl *Definition = taggedDefinition(TagLoc.getDecl());
    const auto *Record = dyn_cast<CXXRecordDecl>(TagLoc.getDecl());
    if (!Definition || isExternalType(Record, SM))
      return;
    const SourceLocation Keyword = TagLoc.getElaboratedKeywordLoc();
    // `class CThing *` of a struct only has its own keyword to fix.
    if (!Definition->isClass()) {
      reportElaborated(Record, Keyword);
      return;
    }
    ElaboratedKeywords[Definition].push_back(Keyword.isFileID() ? Keyword
                                                               : SourceLocation());
    return;
  }

  // A class nested in a dependent one, `class TCOuter<t_CType>::CInner *`, of
  // the template of that one, unless that is specialized, which can declare
  // it with another tag.
  if (const auto *Loc =
          Result.Nodes.getNodeAs<TypeLoc>("dependentElaborated")) {
    const auto NameLoc = Loc->getUnqualifiedLoc().getAs<DependentNameTypeLoc>();
    if (!NameLoc ||
        NameLoc.getTypePtr()->getKeyword() != ElaboratedTypeKeyword::Class ||
        namedThroughSpecializedTemplate(Loc->getType()))
      return;
    const CXXRecordDecl *Record = namedRecord(Loc->getType());
    const CXXRecordDecl *Definition =
        Record ? taggedDefinition(Record) : nullptr;
    if (!Definition || isExternalType(Record, SM))
      return;
    const SourceLocation Keyword = NameLoc.getElaboratedKeywordLoc();
    if (!Definition->isClass()) {
      reportElaborated(Record, Keyword);
      return;
    }
    ElaboratedKeywords[Definition].push_back(
        Keyword.isFileID() ? Keyword : SourceLocation());
    return;
  }

  if (const auto *Friend = Result.Nodes.getNodeAs<FriendDecl>("friend")) {
    const TypeSourceInfo *FriendType = Friend->getFriendType();
    // A friend class template, `template <...> friend class TCList;`, which
    // names a template rather than a type, is told by its tag keyword like
    // another friend, and fixed with the definition.
    if (const auto *FriendTemplate =
            dyn_cast_or_null<ClassTemplateDecl>(Friend->getFriendDecl());
        !FriendType && FriendTemplate) {
      const CXXRecordDecl *Templated = FriendTemplate->getTemplatedDecl();
      const CXXRecordDecl *Definition =
          Templated ? taggedDefinition(Templated) : nullptr;
      // One in a class template isn't a declaration of the template it names
      // until it is instantiated: of the one its name finds where it is.
      if (Templated && !Definition)
        for (const NamedDecl *Found :
             Templated->getDeclContext()->getRedeclContext()->lookup(
                 Templated->getDeclName()))
          if (const auto *Named = dyn_cast<ClassTemplateDecl>(Found))
            if ((Definition = taggedDefinition(Named->getTemplatedDecl())))
              break;
      if (!Definition || isExternalType(Templated, SM))
        return;
      const SourceLocation Loc =
          reportLocation(Templated->getInnerLocStart(), SM);
      if (Loc.isInvalid() || !Loc.isFileID()) {
        if (Definition->isClass())
          FriendKeywords[Definition].push_back(SourceLocation());
        return;
      }
      Token Keyword;
      if (Lexer::getRawToken(Loc, Keyword, SM, LangOpts) ||
          !Keyword.is(tok::raw_identifier) ||
          Keyword.getRawIdentifier() != "class")
        return;
      if (Definition->isClass()) {
        FriendKeywords[Definition].push_back(
            Templated->getInnerLocStart().isFileID() ? Loc : SourceLocation());
        return;
      }
      reportElaborated(Templated, Templated->getInnerLocStart());
      return;
    }
    if (!FriendType)
      return;
    // A dependent specialization, like `friend class TCThing<t_CType>`, is of
    // its template.
    const CXXRecordDecl *Record = namedRecord(FriendType->getType());
    if (!Record || !taggedDefinition(Record) || isExternalType(Record, SM))
      return;
    const SourceLocation Loc =
        reportLocation(FriendType->getTypeLoc().getBeginLoc(), SM);
    // A friend that is not reported, like one a system macro declares, keeps
    // its tag, so the definition keeps its tag too.
    if (Loc.isInvalid() || !Loc.isFileID()) {
      if (taggedDefinition(Record)->isClass())
        FriendKeywords[taggedDefinition(Record)].push_back(SourceLocation());
      return;
    }
    Token Keyword;
    if (Lexer::getRawToken(Loc, Keyword, SM, LangOpts) ||
        !Keyword.is(tok::raw_identifier) ||
        Keyword.getRawIdentifier() != "class")
      return;
    // The tag of a friend of a class is reported and fixed with the
    // definition, like its declarations; only a friend that names a struct
    // with `class` is reported here.
    const CXXRecordDecl *Definition = taggedDefinition(Record);
    // Through a specialized template, it can name a class of a
    // specialization that the fix leaves, `friend class
    // TCOuter<t_CType>::CInner` with a `TCOuter<int>`.
    const bool FileKeyword =
        FriendType->getTypeLoc().getBeginLoc().isFileID() &&
        !(FriendType->getType()->isDependentType() &&
          namedThroughSpecializedTemplate(FriendType->getType()));
    if (Definition->isClass()) {
      FriendKeywords[Definition].push_back(FileKeyword ? Loc
                                                       : SourceLocation());
      return;
    }
    // A struct named with `class` through a specialized template can be a
    // class of a specialization, which needs `class`.
    if (FriendType->getType()->isDependentType() &&
        namedThroughSpecializedTemplate(FriendType->getType()))
      return;
    reportElaborated(Record, FriendType->getTypeLoc().getBeginLoc());
    return;
  }

  // Also an unnamed type, like `typedef class { ... } CType;`.
  const auto *Record = Result.Nodes.getNodeAs<CXXRecordDecl>("record");
  if (Record->isImplicit() || Record->isLambda())
    return;
  if (Record->getFriendObjectKind() != Decl::FOK_None)
    return;
  if (const auto *Specialization =
          dyn_cast<ClassTemplateSpecializationDecl>(Record);
      Specialization && !Specialization->isExplicitSpecialization() &&
      !isa<ClassTemplatePartialSpecializationDecl>(Record)) {
    // An explicit instantiation of a struct template written with `class`,
    // `template class TCBox<int>;`; those of a class template, or of a partial
    // specialization that is one, are fixed with its definition.
    const TemplateSpecializationKind Kind =
        Specialization->getSpecializationKind();
    // Of what it is instantiated from, also a partial specialization,
    // `template <typename T> class TCBox<T *>`, not the primary template.
    const auto From = Specialization->getSpecializedTemplateOrPartial();
    const CXXRecordDecl *Template =
        isa<ClassTemplatePartialSpecializationDecl *>(From)
            ? static_cast<const CXXRecordDecl *>(
                  cast<ClassTemplatePartialSpecializationDecl *>(From))
            : cast<ClassTemplateDecl *>(From)->getTemplatedDecl();
    if ((Kind == TSK_ExplicitInstantiationDeclaration ||
         Kind == TSK_ExplicitInstantiationDefinition) &&
        Template->getDefinition() && Template->getDefinition()->isStruct() &&
        !isExternalType(Template->getDefinition(), SM)) {
      const SourceLocation TemplateLoc = Specialization->getTemplateKeywordLoc();
      if (TemplateLoc.isValid())
        if (const std::optional<Token> Tag =
                Lexer::findNextToken(TemplateLoc, SM, LangOpts);
            Tag && Tag->is(tok::raw_identifier) &&
            Tag->getRawIdentifier() == "class")
          reportElaborated(Template->getDefinition(), Tag->getLocation());
    }
    return;
  }
  const bool Definition = Record->isThisDeclarationADefinition();
  if (Record->isClass()) {
    // The tag of an external type, also of a specialization of an external
    // template, `template <> class std::hash<CValue>`, matches its other
    // declarations.
    if (isExternalType(Record, SM))
      return;
    // A declaration of a type that is not defined here may be of an
    // external type, whose tag has to match its definition.
    const CXXRecordDecl *TypeDefinition = Record->getDefinition();
    if (!TypeDefinition)
      return;
    const SourceLocation KeywordLoc = Record->getInnerLocStart();
    const SourceLocation Loc = reportLocation(KeywordLoc, SM);
    if (Loc.isInvalid())
      return;
    // A class definition is reported once its friends are known, and its
    // other declarations, like forward declarations, are reported and fixed
    // with it.
    if (Definition)
      Definitions.emplace_back(Record, Loc);
    else if (!TypeDefinition->isClass())
      // A declaration of a struct only has its own tag to fix. A keyword from
      // a macro can be shared with other types, so it isn't fixed.
      reportElaborated(TypeDefinition, KeywordLoc);
    return;
  }

  // A leading `public:` is redundant in a struct written here, also one that
  // specializes an external template, but not in one of a system header.
  if (!Record->isStruct() || !Definition ||
      SM.isInSystemHeader(Record->getLocation()))
    return;
  const auto *Access =
      dyn_cast_or_null<AccessSpecDecl>(firstWrittenMember(Record));
  if (!Access || Access->getAccess() != AS_public)
    return;
  // Not one after conditional preprocessing, `#if DPrivateLayout private:
  // #endif`, which another configuration needs.
  if (hasConditionalDirective(ConditionalDirectives,
                              Record->getBraceRange().getBegin(),
                              Access->getAccessSpecifierLoc(), SM))
    return;
  // Nor one after what expands to nothing here but can declare members of
  // another access elsewhere, a macro like `DPrivateMembers`.
  if (writtenBefore(Record, Access->getAccessSpecifierLoc(), SM))
    return;
  const SourceLocation Loc = reportLocation(Access->getAccessSpecifierLoc(), SM);
  if (Loc.isInvalid())
    return;
  auto Diag = diag(Loc, "%0 starts with a 'public:' section, which is the "
                        "default of a struct")
              << Record;
  if (Loc.isFileID())
    Diag << FixItHint::CreateRemoval(accessRange(Access));
}

void StructNotClassCheck::onEndOfTranslationUnit() {
  for (const auto &[Definition, Loc] : Definitions) {
    auto Diag = diag(Loc, "%0 should be declared with 'struct', like "
                          "Malterlib types are")
                << Definition;
    // The tags of a class are fixed together, by the diagnostic of the
    // definition, so that suppressing the diagnostic of one declaration with
    // NOLINT can't leave the tags mismatched. A definition in another file
    // than one of its declarations or friends can't be fixed, as that file
    // may be a header whose fixes are not applied. The other elaborated type
    // specifiers of the class, like `class CType *pValue`, are fixed with it,
    // or the tags would not match.
    if (!canFixDefinition(Definition, *SM, ConditionalDirectives))
      continue;
    const FileID File = SM->getFileID(Definition->getInnerLocStart());
    const auto Friends = FriendKeywords.lookup(Definition);
    auto OtherFile = [&](SourceLocation Keyword) {
      return Keyword.isInvalid() || SM->getFileID(Keyword) != File;
    };
    if (llvm::any_of(Friends, OtherFile))
      continue;
    // The keywords of the declarations and friends are fixed by themselves.
    llvm::SmallPtrSet<const void *, 8> Fixed;
    for (const TagDecl *Redecl : Definition->redecls())
      Fixed.insert(Redecl->getInnerLocStart().getPtrEncoding());
    for (const SourceLocation Keyword : Friends)
      Fixed.insert(Keyword.getPtrEncoding());
    llvm::SmallVector<SourceLocation, 4> Elaborated;
    // A partial specialization explicitly instantiated, `template class
    // TCBox<int *>;` of `TCBox<T *>`, whose tag can't be told apart from
    // those of the primary template in the text, isn't fixed.
    if (const auto *Partial =
            dyn_cast<ClassTemplatePartialSpecializationDecl>(Definition))
      for (const ClassTemplateSpecializationDecl *Instance :
           Partial->getSpecializedTemplate()->specializations())
        if ((Instance->getSpecializationKind() ==
                 TSK_ExplicitInstantiationDeclaration ||
             Instance->getSpecializationKind() ==
                 TSK_ExplicitInstantiationDefinition) &&
            Instance->getSpecializedTemplateOrPartial()
                    .dyn_cast<ClassTemplatePartialSpecializationDecl *>() ==
                Partial)
          Elaborated.push_back(SourceLocation());
    // The explicit instantiations of a class template repeat its tag, like
    // `template class TCThing<int>;`.
    if (const ClassTemplateDecl *Template =
            Definition->getDescribedClassTemplate()) {
      for (const ClassTemplateSpecializationDecl *Instance :
           Template->specializations()) {
        if (Instance->getSpecializationKind() !=
                TSK_ExplicitInstantiationDeclaration &&
            Instance->getSpecializationKind() !=
                TSK_ExplicitInstantiationDefinition)
          continue;
        // One of a partial specialization, `template class TCBox<int *>;` of
        // `TCBox<T *>`, has its tag, not this one's, which the text of the
        // instantiations can't tell apart: this one isn't fixed then.
        if (isa<ClassTemplatePartialSpecializationDecl *>(
                Instance->getSpecializedTemplateOrPartial())) {
          Elaborated.push_back(SourceLocation());
          continue;
        }
        const SourceLocation TemplateLoc = Instance->getTemplateKeywordLoc();
        if (TemplateLoc.isInvalid() || !TemplateLoc.isFileID()) {
          Elaborated.push_back(SourceLocation());
          continue;
        }
        const std::optional<Token> Tag =
            Lexer::findNextToken(TemplateLoc, *SM, getLangOpts());
        if (Tag && Tag->is(tok::raw_identifier) &&
            Tag->getRawIdentifier() == "class")
          Elaborated.push_back(Tag->getLocation());
        // A tag from a macro, `template DClass TCThing<int>;`, can't be
        // fixed with it.
        else if (!Tag || !Tag->is(tok::raw_identifier) ||
                 Tag->getRawIdentifier() != "struct")
          Elaborated.push_back(SourceLocation());
      }
      // A specialization knows only its last explicit instantiation, so an
      // earlier one of it, `extern template class TCThing<int>;` before
      // `template class TCThing<int>;`, is found in the text of the file.
      if (llvm::any_of(Template->specializations(),
                       [](const ClassTemplateSpecializationDecl *Instance) {
                         return Instance->getSpecializationKind() ==
                                    TSK_ExplicitInstantiationDeclaration ||
                                Instance->getSpecializationKind() ==
                                    TSK_ExplicitInstantiationDefinition;
                       })) {
        const StringRef Buffer = SM->getBufferData(File);
        Lexer Raw(SM->getLocForStartOfFile(File), getLangOpts(),
                  Buffer.begin(), Buffer.begin(), Buffer.end());
        // The text can't tell it from another template of the same name,
        // `NB::TCThing`, whose instantiations aren't fixed with it.
        bool Ambiguous = false;
        std::function<void(const DeclContext *, int)> FindOthers =
            [&](const DeclContext *Context, int Depth) {
              if (Ambiguous || Depth > 32)
                return;
              for (const Decl *Member : Context->decls()) {
                if (const auto *Other = dyn_cast<ClassTemplateDecl>(Member);
                    Other && Other->getCanonicalDecl() !=
                                 Template->getCanonicalDecl() &&
                    Other->getDeclName() == Template->getDeclName())
                  Ambiguous = true;
                if (const auto *Inner = dyn_cast<DeclContext>(Member);
                    Inner && isa<NamespaceDecl, CXXRecordDecl, LinkageSpecDecl>(
                                 Member))
                  FindOthers(Inner, Depth + 1);
              }
            };
        FindOthers(Template->getASTContext().getTranslationUnitDecl(), 0);
        // `template class TCThing<`, also qualified, `template class
        // N::TCThing<`.
        llvm::SmallVector<Token, 16> Previous;
        // Not what the preprocessor skipped, like an instantiation of
        // another template of the same name in an inactive `#ifdef`.
        auto Skipped = [&](SourceLocation Loc) {
          return isSkipped(SkippedRanges, Loc, *SM);
        };
        for (Token Tok; !Raw.LexFromRawLexer(Tok);) {
          if (Tok.is(tok::less) && !Previous.empty() &&
              !Skipped(Tok.getLocation()) &&
              Previous.back().is(tok::raw_identifier) &&
              Previous.back().getRawIdentifier() == Template->getName()) {
            size_t iToken = Previous.size() - 1;
            while (iToken >= 2 && Previous[iToken - 1].is(tok::coloncolon) &&
                   Previous[iToken - 2].is(tok::raw_identifier))
              iToken -= 2;
            if (iToken >= 1 && Previous[iToken - 1].is(tok::coloncolon))
              --iToken;
            if (iToken >= 2 && Previous[iToken - 1].is(tok::raw_identifier) &&
                Previous[iToken - 1].getRawIdentifier() == "class" &&
                Previous[iToken - 2].is(tok::raw_identifier) &&
                Previous[iToken - 2].getRawIdentifier() == "template" &&
                !llvm::is_contained(Elaborated,
                                    Previous[iToken - 1].getLocation()))
              Elaborated.push_back(Ambiguous
                                       ? SourceLocation()
                                       : Previous[iToken - 1].getLocation());
          }
          Previous.push_back(Tok);
          if (Previous.size() > 16)
            Previous.erase(Previous.begin());
        }
      }
    }
    for (const SourceLocation Keyword : ElaboratedKeywords.lookup(Definition)) {
      if (Keyword.isValid() && Fixed.contains(Keyword.getPtrEncoding()))
        continue;
      if (llvm::is_contained(Elaborated, Keyword))
        continue;
      Elaborated.push_back(Keyword);
    }
    if (llvm::any_of(Elaborated, OtherFile))
      continue;
    if (const auto *Access =
            dyn_cast_or_null<AccessSpecDecl>(firstWrittenMember(Definition));
        Access && Access->getAccess() == AS_public &&
        Access->getAccessSpecifierLoc().isFileID())
      Diag << FixItHint::CreateRemoval(accessRange(Access));
    // Each keyword once, also one that is both a declaration and a friend,
    // `friend class CTarget;` that introduces it.
    llvm::SmallPtrSet<const void *, 8> Replaced;
    auto Replace = [&](SourceLocation Keyword) {
      if (Replaced.insert(Keyword.getPtrEncoding()).second)
        Diag << FixItHint::CreateReplacement(
            CharSourceRange::getTokenRange(Keyword, Keyword), "struct");
    };
    for (const TagDecl *Redecl : Definition->redecls())
      if (Redecl->isClass())
        Replace(Redecl->getInnerLocStart());
    for (const SourceLocation Keyword : Friends)
      Replace(Keyword);
    for (const SourceLocation Keyword : Elaborated)
      Replace(Keyword);
  }
  Definitions.clear();
  FriendKeywords.clear();
  ElaboratedKeywords.clear();
}

} // namespace clang::tidy::malterlib
