//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "StructNotClassCheck.h"
#include "CheckedAccess.h"
#include "FixIts.h"
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
/// Whether \p Name is a keyword of a block that is there only when what it
/// names is, `__if_exists` or `__if_not_exists`.
static bool isExistsKeyword(StringRef Name) {
  return Name == "__if_exists" || Name == "__if_not_exists";
}

/// Records the preprocessing the fixes are told by (CPreprocessingRecorder),
/// and where macros are used in files that give a block of `__if_exists` or
/// `__if_not_exists`.
class PreprocessingCollector : public CPreprocessingRecorder {
public:
  PreprocessingCollector(const Preprocessor &ThePreprocessor,
                         CPreprocessing &Record,
                         llvm::SmallVectorImpl<SourceLocation> &ExistsExpansionList)
      : CPreprocessingRecorder(Record), PP(ThePreprocessor),
        ExistsExpansions(ExistsExpansionList) {}
  void MacroExpands(const Token &Name, const MacroDefinition &Definition,
                    SourceRange Range, const MacroArgs *Arguments) override {
    CPreprocessingRecorder::MacroExpands(Name, Definition, Range, Arguments);
    if (Name.getLocation().isFileID() && PP.getLangOpts().MicrosoftExt &&
        givesExists(Definition.getMacroInfo()))
      ExistsExpansions.push_back(Name.getLocation());
  }

private:
  /// Whether the definition \p Info, or that of a macro it uses as it is now
  /// defined, has a block of `__if_exists` or `__if_not_exists`: by one pass
  /// of Tarjan's algorithm over the definitions it reaches, on a stack of its
  /// own, not of calls, which as many macros as code can define would take
  /// past the stack. Each strongly connected component, definitions that
  /// lead to each other, gives one when one of them has one or leads to a
  /// component that gives one, told when it completes and kept for each of
  /// its definitions (Gives), so each definition and use is gone through once.
  bool givesExists(const MacroInfo *Info) {
    if (!Info)
      return false;
    if (auto It = Gives.find(Info); It != Gives.end())
      return It->second;
    struct CNode {
      unsigned Index;
      unsigned Low;
      bool OnStack;
    };
    struct CCall {
      const MacroInfo *Info;
      unsigned iToken;
    };
    llvm::DenseMap<const MacroInfo *, CNode> Nodes;
    llvm::SmallVector<const MacroInfo *, 16> Component;
    llvm::SmallVector<CCall, 16> Calls;
    unsigned NextIndex = 0;
    auto Enter = [&](const MacroInfo *Next) {
      Nodes[Next] = {NextIndex, NextIndex, true};
      ++NextIndex;
      Component.push_back(Next);
      Calls.push_back({Next, 0});
    };
    // What a definition names that is a macro, as it is now defined.
    auto Named = [&](const MacroInfo *Of, unsigned iToken) -> const MacroInfo * {
      const IdentifierInfo *Identifier =
          Of->getReplacementToken(iToken).getIdentifierInfo();
      return Identifier ? PP.getMacroInfo(Identifier) : nullptr;
    };
    auto HasKeyword = [](const MacroInfo *Of) {
      return llvm::any_of(Of->tokens(), [](const Token &Each) {
        const IdentifierInfo *Identifier = Each.getIdentifierInfo();
        return Identifier && isExistsKeyword(Identifier->getName());
      });
    };
    Enter(Info);
    while (!Calls.empty()) {
      const MacroInfo *Of = Calls.back().Info;
      if (Calls.back().iToken < Of->getNumTokens()) {
        const MacroInfo *Next = Named(Of, Calls.back().iToken++);
        if (!Next || Gives.contains(Next))
          continue;
        if (auto It = Nodes.find(Next); It == Nodes.end())
          Enter(Next);
        else if (It->second.OnStack)
          Nodes[Of].Low = std::min(Nodes[Of].Low, It->second.Index);
        continue;
      }
      Calls.pop_back();
      const CNode Done = Nodes[Of];
      if (!Calls.empty()) {
        CNode &Caller = Nodes[Calls.back().Info];
        Caller.Low = std::min(Caller.Low, Done.Low);
      }
      if (Done.Low != Done.Index)
        continue;
      // A component completes: it gives one when a definition of it has one,
      // or names one of a component told before that gives one.
      llvm::SmallVector<const MacroInfo *, 4> Members;
      const MacroInfo *Member = nullptr;
      do {
        Member = Component.pop_back_val();
        Nodes[Member].OnStack = false;
        Members.push_back(Member);
      } while (Member != Of);
      bool Has = false;
      for (const MacroInfo *Each : Members) {
        Has = Has || HasKeyword(Each);
        for (unsigned iToken = 0; !Has && iToken < Each->getNumTokens();
             ++iToken)
          if (const MacroInfo *Next = Named(Each, iToken))
            if (auto It = Gives.find(Next); It != Gives.end() && It->second)
              Has = true;
      }
      for (const MacroInfo *Each : Members)
        Gives[Each] = Has;
    }
    return Gives.lookup(Info);
  }

  /// Of each definition told, whether it gives a block (givesExists).
  llvm::DenseMap<const MacroInfo *, bool> Gives;

  const Preprocessor &PP;
  llvm::SmallVectorImpl<SourceLocation> &ExistsExpansions;
};
} // namespace

void StructNotClassCheck::registerPPCallbacks(const SourceManager &,
                                              Preprocessor *PP,
                                              Preprocessor *) {
  PP->addPPCallbacks(std::make_unique<PreprocessingCollector>(
      *PP, Preprocessing, ExistsExpansions));
}

namespace {
/// Whether the declaration is an explicit instantiation of a class, of a
/// class template, `extern template class TCThing<int>;`, or of a class of
/// one, `template class TCOuter<int>::CInner;`.
AST_MATCHER(Decl, isClassExplicitInstantiation) {
  const auto *Instantiation = dyn_cast<ExplicitInstantiationDecl>(&Node);
  return Instantiation &&
         isa_and_nonnull<CXXRecordDecl>(Instantiation->getSpecialization());
}
} // namespace

void StructNotClassCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(cxxRecordDecl().bind("record"), this);
  Finder->addMatcher(decl(isClassExplicitInstantiation()).bind("instantiation"),
                     this);
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

/// The definition of the primary template whose tag clang compares the tag
/// written for \p Record with, a specialization of a class template (an
/// instance, an explicit or a partial specialization); null for another
/// class.
static const CXXRecordDecl *comparedPrimary(const CXXRecordDecl *Record) {
  const auto *Specialization =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(Record);
  const ClassTemplateDecl *Template =
      Specialization ? Specialization->getSpecializedTemplate() : nullptr;
  return Template && Template->getTemplatedDecl()
             ? Template->getTemplatedDecl()->getDefinition()
             : nullptr;
}

/// Whether \p Record is a type with an external name (isExternalDeclaration):
/// it is a library's, or specializes a template of one, also one this code
/// redeclares, which the specialization can name, `template <> class
/// NLibrary::TCSerializer<CMessage>` after `namespace NLibrary { template
/// <typename t_CType> class TCSerializer; }`.
static bool isExternalType(const CXXRecordDecl *Record,
                           const SourceManager &SM) {
  if (isExternalDeclaration(Record, SM))
    return true;
  if (const auto *Specialization =
          dyn_cast<ClassTemplateSpecializationDecl>(Record))
    return isExternalDeclaration(Specialization->getSpecializedTemplate(), SM);
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
  if (stackNearlyExhausted())
    return withStack([&] { return writtenBefore(Record, Loc, SM); });
  const SourceLocation Brace = Record->getBraceRange().getBegin();
  // A brace from a macro, `struct CObject DOpen`, can come with members in
  // another configuration, `{ private: int mp_Secret;`.
  if (!Brace.isFileID())
    return true;
  if (!Loc.isFileID())
    return false;
  // In an included fragment: unless the label is the first token of the
  // fragment, past its preprocessing directives, and the include the first
  // thing of the body, what is before it can declare members in another
  // configuration, `DPrivateMembers` before `public:`.
  if (SM.getFileID(Brace) != SM.getFileID(Loc)) {
    const FileID Fragment = SM.getFileID(Loc);
    if (SM.getFileID(SM.getIncludeLoc(Fragment)) != SM.getFileID(Brace))
      return true;
    const LangOptions &Options = Record->getASTContext().getLangOpts();
    auto FirstToken = [&](FileID File, unsigned Offset, Token &First) {
      const StringRef Text = SM.getBufferData(File);
      Lexer Raw(SM.getLocForStartOfFile(File), Options, Text.begin(),
                Text.begin() + Offset, Text.end());
      // A directive is the tokens of its line, up to the next one that
      // starts a line.
      bool Directive = false;
      for (int Steps = 0; Steps < 65536; ++Steps) {
        if (Raw.LexFromRawLexer(First) && First.is(tok::eof))
          return false;
        if (First.isAtStartOfLine())
          Directive = First.is(tok::hash);
        if (!Directive)
          return true;
      }
      return false;
    };
    Token InBody;
    Token InFragment;
    const StringRef Body = SM.getBufferData(SM.getFileID(Brace));
    Lexer Raw(SM.getLocForStartOfFile(SM.getFileID(Brace)), Options,
              Body.begin(), Body.begin() + SM.getFileOffset(Brace) + 1,
              Body.end());
    return Raw.LexFromRawLexer(InBody) || !InBody.is(tok::hash) ||
           !InBody.isAtStartOfLine() ||
           !FirstToken(Fragment, 0, InFragment) ||
           InFragment.getLocation() != Loc;
  }
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
  if (llvm::any_of(basesOf(Definition), [](const CXXBaseSpecifier &Base) {
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
                             const CPreprocessing &Preprocessing,
                             const CFixIts &FixIts,
                             const LangOptions &LangOpts) {
  if (!canBeStruct(Definition) || !Definition->getInnerLocStart().isFileID())
    return false;
  // Nor one whose head, from its keyword through its opening brace, isn't
  // all written in one file (CFixIts): a macro used there can expand to a
  // name, bases or access that the fix changes the default of in another
  // configuration, the name, `class DClassName` with `DClassName` expanding to
  // `CThing : CBase`, the access of a base, `virtual DPublicBase CBase` with
  // `DPublicBase` expanding to nothing, a base, `public DBases` expanding to
  // `CBase, CHidden`, the base clause, `CObject DFinal` expanding to `:
  // CBase`, or what ends the class and starts another, `class DExport
  // CThing` with `DExport` expanding to `CHidden { int m_Value; }; class`;
  // and so can conditional preprocessing, `class CObject : #ifdef DPrivate
  // CBase #else public CBase #endif`.
  const SourceRange Braces = Definition->getBraceRange();
  if (Braces.isInvalid() || !Braces.getBegin().isFileID() ||
      !FixIts.writtenBetween(
          CharSourceRange::getCharRange(Definition->getInnerLocStart(),
                                        Braces.getBegin()),
          SM.getFileID(Braces.getBegin()), SM, LangOpts))
    return false;
  // Nor one with conditional preprocessing in its body, `#ifdef DLayout`,
  // whose skipped branches can have members before the first access
  // specifier that the fix would make public.
  if (hasLocationBetween(Preprocessing.Directives, Braces.getBegin(),
                         Braces.getEnd(), SM))
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

/// The range that removing the access label \p Access removes, its tokens
/// through its colon as the lexer reads them (CFixIts::tokens), when that
/// removes nothing but the label: it is written, also its colon, in \p
/// ClassFile, the file of the braces of its class, without attributes,
/// `private __attribute__((annotate("..."))):`, with no use of a macro,
/// include nor preprocessing directive in it, `public #define ... :`
/// (CFixIts), nor a comment in it or after it on its line, which tells of
/// its section: by the tokens with comments, also one whose opening a line
/// splice divides.
static std::optional<CharSourceRange>
labelRemoval(const AccessSpecDecl *Access, FileID ClassFile,
             const CFixIts &FixIts, const SourceManager &SM,
             const LangOptions &LangOpts) {
  if (Access->hasAttrs())
    return std::nullopt;
  const SourceLocation Label = Access->getAccessSpecifierLoc();
  const std::optional<CharSourceRange> Range = FixIts.tokens(
      Label, Access->getColonLoc(), ClassFile, SM, LangOpts);
  if (!Range)
    return std::nullopt;
  const StringRef Buffer = SM.getBufferData(ClassFile);
  Lexer Raw(SM.getLocForStartOfFile(ClassFile), LangOpts, Buffer.begin(),
            Buffer.begin() + SM.getFileOffset(Label), Buffer.end());
  Raw.SetCommentRetentionState(true);
  const unsigned ThroughOffset = SM.getFileOffset(Range->getEnd());
  Token Next;
  for (;;) {
    const bool Last = Raw.LexFromRawLexer(Next);
    const unsigned At = SM.getFileOffset(Next.getLocation());
    if (At >= ThroughOffset) {
      if (Next.is(tok::comment) && !Next.isAtStartOfLine())
        return std::nullopt;
      return Range;
    }
    if (Next.is(tok::comment))
      return std::nullopt;
    if (Last)
      return Range;
  }
}

static bool namedThroughSpecializedTemplate(QualType Type);
static const CXXRecordDecl *
namedRecord(QualType Type, llvm::SmallPtrSetImpl<const CXXRecordDecl *> &Seen);

/// The class or class template named \p Name in the class \p Qualifier
/// names, `TCOuter<t_CType>::CInner` of the template of that one, also of a
/// base it inherits, `struct TCOuter : CBase`. \p Seen holds the classes
/// already looked in, which aren't again: a base named through itself,
/// `struct TCA : TCB<t_CType>::CZ` and `struct TCB : TCA<t_CType>::CZ`,
/// names none.
static const NamedDecl *
nestedIn(NestedNameSpecifier Qualifier, const IdentifierInfo *Name,
         llvm::SmallPtrSetImpl<const CXXRecordDecl *> &Seen) {
  if (stackNearlyExhausted())
    return withStack([&] { return nestedIn(Qualifier, Name, Seen); });
  const clang::Type *Outer = qualifierTypeOf(Qualifier);
  const CXXRecordDecl *Scope =
      Outer && Name ? namedRecord(QualType(Outer, 0), Seen) : nullptr;
  llvm::SmallVector<const CXXRecordDecl *, 4> Scopes;
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
    for (const CXXBaseSpecifier &Base : basesOf(Current))
      if (!namedThroughSpecializedTemplate(Base.getType()))
        Scopes.push_back(namedRecord(Base.getType(), Seen));
  }
  return nullptr;
}

/// The record \p Type names, also the primary template of a dependent
/// specialization, like `TCThing<t_CType>`, not through the classes in
/// \p Seen (nestedIn).
static const CXXRecordDecl *
namedRecord(QualType Type, llvm::SmallPtrSetImpl<const CXXRecordDecl *> &Seen) {
  if (stackNearlyExhausted())
    return withStack([&] { return namedRecord(Type, Seen); });
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
      if (const auto *Template = dyn_cast_or_null<ClassTemplateDecl>(
              nestedIn(Dependent->getQualifier(),
                       Dependent->getName().getIdentifier(), Seen)))
        return Template->getTemplatedDecl();
    return nullptr;
  }
  // A class nested in a dependent one, `TCOuter<t_CType>::CInner`.
  if (const auto *Dependent = Type->getAs<DependentNameType>())
    return dyn_cast_or_null<CXXRecordDecl>(
        nestedIn(Dependent->getQualifier(), Dependent->getIdentifier(), Seen));
  return nullptr;
}

/// The record \p Type names, also the primary template of a dependent
/// specialization, like `TCThing<t_CType>`.
static const CXXRecordDecl *namedRecord(QualType Type) {
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
  return namedRecord(Type, Seen);
}

/// Whether dependent \p Type, like `TCOuter<t_CType>::CInner` or
/// `TCThing<t_CType>`, is named through a template that is specialized,
/// explicitly or partially, whose specializations can declare it with
/// another tag than the template does.
static bool namedThroughSpecializedTemplate(QualType Type) {
  if (stackNearlyExhausted())
    return withStack([&] { return namedThroughSpecializedTemplate(Type); });
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
    Named = qualifierTypeOf(Qualifier);
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
                                           SourceLocation Keyword,
                                           const CXXRecordDecl *Primary) {
  const SourceLocation Loc = reportLocation(Keyword, *SM);
  if (Loc.isInvalid())
    return;
  auto Diag = diag(Loc, "%0 is a struct, but is named with 'class' here; "
                        "name it with 'struct'")
              << diagnosticName(Record);
  // Compared with a primary template declared with `class`, the keyword
  // stays, and keeps the primary template from being fixed alone.
  if (Primary && Primary->isClass()) {
    KeptClassKeywords.insert(Primary);
    return;
  }
  if (!Keyword.isFileID())
    return;
  const FileID File = SM->getFileID(Keyword);
  if (const std::optional<CharSourceRange> Range =
          FixIts.tokens(Keyword, Keyword, File, *SM, getLangOpts()))
    if (const std::optional<FixItHint> Fix =
            FixIts.replacement(*Range, "struct", File, *SM, getLangOpts()))
      Diag << *Fix;
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

  // An explicit instantiation names what it is instantiated from
  // (taggedDefinition) with its own tag, the keyword that follows `template`,
  // before any attribute or macro, `extern template class DExport
  // TCThing<int>;`, also of a class of a template, `template class
  // TCOuter<int>::CInner;`, or of a partial specialization, `template class
  // TCBox<int *>;` of `template <typename T> class TCBox<T *>`.
  if (const auto *Instantiation =
          Result.Nodes.getNodeAs<ExplicitInstantiationDecl>("instantiation")) {
    const CXXRecordDecl *Definition = taggedDefinition(
        cast<CXXRecordDecl>(Instantiation->getSpecialization()));
    if (!Definition || isExternalType(Definition, SM))
      return;
    // As written in a file, not given by a macro, `template DClass
    // TCThing<int>;`, which other declarations can share.
    // Read as the compiler reads it, also with a line splice,
    // `cla\<newline>ss` (CFixIts::spelling).
    const SourceLocation Keyword = Instantiation->getTagKWLoc();
    const std::string Written =
        CFixIts::spelling(Keyword, SM, LangOpts).value_or(std::string());
    // Of a struct, named with `class`, it is reported there.
    const CXXRecordDecl *Primary =
        comparedPrimary(cast<CXXRecordDecl>(Instantiation->getSpecialization()));
    if (Definition->isStruct()) {
      if (Written == "class")
        reportElaborated(Definition, Keyword, Primary);
      return;
    }
    // Of a class, fixed with its definition, unless the keyword can't be,
    // which keeps the definition from being fixed. One of a partial
    // specialization is told with the definitions, which it keeps from being
    // fixed (onEndOfTranslationUnit).
    const auto *Specialization = dyn_cast<ClassTemplateSpecializationDecl>(
        Instantiation->getSpecialization());
    if (!Definition->isClass() || Written == "struct" ||
        (Specialization &&
         isa<ClassTemplatePartialSpecializationDecl *>(
             Specialization->getSpecializedTemplateOrPartial())))
      return;
    ElaboratedKeywords[Definition].push_back(Written == "class"
                                                 ? Keyword
                                                 : SourceLocation());
    if (Primary && Primary != Definition)
      ComparedWith[Definition].insert(Primary);
    return;
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
    // Clang compares the tag of a template-id with the primary template's.
    const CXXRecordDecl *Primary = comparedPrimary(Record);
    if (!Definition->isClass()) {
      reportElaborated(Record, Keyword, Primary);
      return;
    }
    ElaboratedKeywords[Definition].push_back(Keyword.isFileID() ? Keyword
                                                               : SourceLocation());
    if (Primary && Primary != Definition)
      ComparedWith[Definition].insert(Primary);
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
      if (CFixIts::spelling(Loc, SM, LangOpts) != "class")
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
    if (CFixIts::spelling(Loc, SM, LangOpts) != "class")
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
      if (const CXXRecordDecl *Primary = comparedPrimary(Record);
          Primary && Primary != Definition)
        ComparedWith[Definition].insert(Primary);
      return;
    }
    // A struct named with `class` through a specialized template can be a
    // class of a specialization, which needs `class`.
    if (FriendType->getType()->isDependentType() &&
        namedThroughSpecializedTemplate(FriendType->getType()))
      return;
    reportElaborated(Record, FriendType->getTypeLoc().getBeginLoc(),
                     comparedPrimary(Record));
    return;
  }

  // Also an unnamed type, like `typedef class { ... } CType;`.
  const auto *Record = Result.Nodes.getNodeAs<CXXRecordDecl>("record");
  if (Record->isImplicit() || Record->isLambda())
    return;
  if (Record->getFriendObjectKind() != Decl::FOK_None)
    return;
  // An instantiation, explicit by its own declaration, which is told above.
  if (const auto *Specialization =
          dyn_cast<ClassTemplateSpecializationDecl>(Record);
      Specialization && !Specialization->isExplicitSpecialization() &&
      !isa<ClassTemplatePartialSpecializationDecl>(Record))
    return;
  const bool Definition = Record->isThisDeclarationADefinition();
  if (Record->isClass()) {
    // The tag of an external type, also of a specialization of an external
    // template, `template <> class std::hash<CValue>`, matches its other
    // declarations. Its labels are checked as those of a class.
    if (isExternalType(Record, SM)) {
      if (Definition)
        reportRepeatedAccess(Record, true, SM);
      return;
    }
    // A declaration of a type that is not defined here may be of an
    // external type, whose tag has to match its definition.
    const CXXRecordDecl *TypeDefinition = Record->getDefinition();
    if (!TypeDefinition)
      return;
    const SourceLocation KeywordLoc = Record->getInnerLocStart();
    const SourceLocation Loc = reportLocation(KeywordLoc, SM);
    // Its labels that repeat the label before them, whatever its keyword,
    // also one a macro gives, not the default of a class, which a struct, as
    // it is reported to be or a macro can make it elsewhere, needs.
    if (Definition)
      reportRepeatedAccess(Record, false, SM);
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
      reportElaborated(TypeDefinition, KeywordLoc, comparedPrimary(Record));
    return;
  }

  if (Definition)
    reportRepeatedAccess(Record, true, SM);
}

void StructNotClassCheck::reportRepeatedAccess(const CXXRecordDecl *Definition,
                                               bool WithDefault,
                                               const SourceManager &SM) {
  // An access label that repeats the access in effect is redundant: the
  // default of a struct or union, public, or of a class, private, also after
  // members, `struct CActor { using CType = int; public: ...`, or that of the
  // label before it, `private: ... private:`. Of a type written here, also
  // one that specializes an external template, but not of a system header,
  // nor what a template is instantiated as, which is checked as written.
  // Nor of one that comes from a precompiled header or a module, whose
  // preprocessing this translation unit didn't see.
  if (SM.isInSystemHeader(Definition->getLocation()) ||
      Definition->getTemplateInstantiationPattern() ||
      Definition->isFromASTFile())
    return;
  // Only of one whose braces are written in one file, not from a macro,
  // whose body can be gone through.
  const SourceLocation Brace = Definition->getBraceRange().getBegin();
  const SourceLocation End = Definition->getBraceRange().getEnd();
  if (Brace.isInvalid() || End.isInvalid() || !Brace.isFileID() ||
      !End.isFileID() || SM.getFileID(Brace) != SM.getFileID(End))
    return;
  const FileID ClassFile = SM.getFileID(Brace);
  const LangOptions &LangOpts = Definition->getASTContext().getLangOpts();
  // Nor of one whose body includes a file. Nor, in Microsoft mode, of one
  // whose body has a block of `__if_exists` or `__if_not_exists`, whose
  // access labels are only there when what it names is, written, also with
  // a line splice, `__if_exi\` and `sts`, or from a macro, also through the
  // macros it uses (ExistsExpansions).
  if (hasLocationBetween(Preprocessing.Includes, Brace, End, SM))
    return;
  if (LangOpts.MicrosoftExt) {
    if (hasLocationBetween(ExistsExpansions, Brace, End, SM))
      return;
    const StringRef Buffer = SM.getBufferData(ClassFile);
    Lexer Raw(SM.getLocForStartOfFile(ClassFile), LangOpts, Buffer.begin(),
              Buffer.begin() + SM.getFileOffset(Brace), Buffer.end());
    const unsigned EndOffset = SM.getFileOffset(End);
    Token Next;
    for (;;) {
      const bool Last = Raw.LexFromRawLexer(Next);
      if (SM.getFileOffset(Next.getLocation()) >= EndOffset)
        break;
      if (Next.is(tok::raw_identifier) &&
          isExistsKeyword(Lexer::getSpelling(Next, SM, LangOpts)))
        return;
      if (Last)
        break;
    }
  }
  // Not the default of one whose tag a macro gives, which can be `struct` or
  // `class` in another configuration: the default is known only of one whose
  // keyword is written in the file of its opening brace, not by a macro nor
  // an include, with no preprocessing between them, which can choose it, `#if
  // DPublic struct #else class #endif CExample {`, `#include DTagFile`, nor a
  // macro, which can end the class and start another, `struct DTypeName {` of
  // `#define DTypeName CForward; class CSelected`.
  if (const SourceLocation Keyword = Definition->getInnerLocStart();
      !Keyword.isFileID() || SM.getFileID(Keyword) != ClassFile ||
      hasLocationBetween(Preprocessing.Directives, Keyword, Brace, SM) ||
      hasLocationBetween(Preprocessing.Expansions, Keyword, Brace, SM))
    WithDefault = false;
  // Since the brace or the label before it.
  SourceLocation From = Brace;
  const AccessSpecDecl *Previous = nullptr;
  bool FromLabel = false;
  AccessSpecifier InEffect = Definition->isClass() ? AS_private : AS_public;
  for (const Decl *Member : Definition->decls()) {
    const auto *Access = dyn_cast<AccessSpecDecl>(Member);
    if (!Access)
      continue;
    const SourceLocation Label = Access->getAccessSpecifierLoc();
    // Of a label written in the file of the braces, also its colon, and after
    // the brace or a label written there: one of an included fragment of the
    // body isn't compared, nor the label after it with it.
    // Not one with attributes, like an annotation of its section, `private
    // __attribute__((annotate("..."))):`, nor the one after it, which ends
    // that section: an annotated label is a barrier.
    const bool Repeats = Access->getAccess() == InEffect &&
                         (FromLabel || WithDefault) && !Access->hasAttrs() &&
                         !(Previous && Previous->hasAttrs()) &&
                         Label.isFileID() &&
                         Access->getColonLoc().isFileID() && From.isFileID() &&
                         SM.getFileID(Label) == ClassFile &&
                         SM.getFileID(From) == ClassFile;
    // Not after conditional preprocessing, `#if DPrivateLayout private:
    // #endif`, which another configuration needs, nor after any use of a
    // macro, also one that expands to nothing here, `DPrivateMembers`, or
    // gives only part of a member, `DMemberType mp_Value;`, or an include,
    // which can give another access in another configuration; up to the end
    // of its colon, which the fix removes too, `private DLayout:`, as the
    // lexer reads it, also after a line splice, `private\<newline>:`.
    const SourceLocation Through =
        Access->getColonLoc().isFileID()
            ? Lexer::getLocForEndOfToken(Access->getColonLoc(), 0, SM, LangOpts)
            : SourceLocation();
    // The text of the label, through its colon, in its file.
    StringRef Written;
    if (Repeats) {
      bool Invalid = false;
      const StringRef Buffer = SM.getBufferData(SM.getFileID(Label), &Invalid);
      const unsigned At = SM.getFileOffset(Label);
      const unsigned End = SM.getFileOffset(Through);
      if (!Invalid && Through.isValid() && At <= End && End <= Buffer.size())
        Written = Buffer.slice(At, End);
    }
    if (Repeats && !Written.empty() &&
        !hasLocationBetween(Preprocessing.Directives, From, Through, SM) &&
        !hasLocationBetween(Preprocessing.Expansions, From, Through, SM)) {
      static constexpr const char *Defaults[] = {"struct", "class", "union"};
      auto Diag =
          diag(Label, "'%0:' repeats the access in effect in %1, %select{the "
                      "default of a %3|that of the label before it}2")
          << (InEffect == AS_public
                  ? "public"
                  : (InEffect == AS_protected ? "protected" : "private"))
          << diagnosticName(Definition) << (FromLabel ? 1u : 0u)
          << Defaults[Definition->isClass() ? 1 : (Definition->isUnion() ? 2
                                                                         : 0)];
      // Not removed when anything else is in it or follows it on its line
      // (labelRemoval).
      if (const std::optional<CharSourceRange> Range =
              labelRemoval(Access, ClassFile, FixIts, SM, LangOpts))
        if (const std::optional<FixItHint> Fix =
                FixIts.tokenRemoval(*Range, ClassFile, SM, LangOpts))
          Diag << *Fix;
    }
    InEffect = Access->getAccess();
    From = Label;
    Previous = Access;
    FromLabel = true;
  }
}

void StructNotClassCheck::onEndOfTranslationUnit() {
  // The fixes of each definition, chosen first; a definition whose fix would
  // leave a tag clang compares with another different where the two were
  // equal is then not fixed (unfixMismatches), and the warning stays.
  struct CFixedDefinition {
    const CXXRecordDecl *Definition;
    SourceLocation Loc;
    llvm::SmallVector<FixItHint, 4> Fixes;
    bool Fixable = false;
  };
  llvm::SmallVector<CFixedDefinition, 8> Fixed;
  for (const auto &[Definition, Loc] : Definitions) {
    CFixedDefinition &Each = Fixed.emplace_back();
    Each.Definition = Definition;
    Each.Loc = Loc;
    llvm::SmallVector<FixItHint, 4> &Diag = Each.Fixes;
    // The tags of a class are fixed together, by the diagnostic of the
    // definition, so that suppressing the diagnostic of one declaration with
    // NOLINT can't leave the tags mismatched. A definition in another file
    // than one of its declarations or friends can't be fixed, as that file
    // may be a header whose fixes are not applied. The other elaborated type
    // specifiers of the class, like `class CType *pValue`, are fixed with it,
    // or the tags would not match.
    if (!canFixDefinition(Definition, *SM, Preprocessing, FixIts,
                          getLangOpts()))
      continue;
    const FileID File = SM->getFileID(Definition->getInnerLocStart());
    const auto Friends = FriendKeywords.lookup(Definition);
    auto OtherFile = [&](SourceLocation Keyword) {
      return Keyword.isInvalid() || SM->getFileID(Keyword) != File;
    };
    if (llvm::any_of(Friends, OtherFile))
      continue;
    // The keywords of the declarations and friends are fixed by themselves.
    llvm::SmallPtrSet<const void *, 8> Own;
    for (const TagDecl *Redecl : Definition->redecls())
      Own.insert(Redecl->getInnerLocStart().getPtrEncoding());
    for (const SourceLocation Keyword : Friends)
      Own.insert(Keyword.getPtrEncoding());
    llvm::SmallVector<SourceLocation, 4> Elaborated;
    // A partial specialization explicitly instantiated, `template class
    // TCBox<int *>;` of `TCBox<T *>`, isn't fixed: the compiler checks the
    // tag of the instantiation against the primary template's.
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
    // Nor is the primary template when an explicit instantiation is of a
    // partial specialization, `template class TCBox<int *>;`, whose tag the
    // instantiation keeps. The explicit instantiations of its own are fixed
    // with it, as elaborated type specifiers (ElaboratedKeywords).
    if (const ClassTemplateDecl *Template =
            Definition->getDescribedClassTemplate())
      for (const ClassTemplateSpecializationDecl *Instance :
           Template->specializations())
        if ((Instance->getSpecializationKind() ==
                 TSK_ExplicitInstantiationDeclaration ||
             Instance->getSpecializationKind() ==
                 TSK_ExplicitInstantiationDefinition) &&
            isa<ClassTemplatePartialSpecializationDecl *>(
                Instance->getSpecializedTemplateOrPartial()))
          Elaborated.push_back(SourceLocation());
    for (const SourceLocation Keyword : ElaboratedKeywords.lookup(Definition)) {
      if (Keyword.isValid() && Own.contains(Keyword.getPtrEncoding()))
        continue;
      if (llvm::is_contained(Elaborated, Keyword))
        continue;
      Elaborated.push_back(Keyword);
    }
    if (llvm::any_of(Elaborated, OtherFile))
      continue;
    // Each keyword once, also one that is both a declaration and a friend,
    // `friend class CTarget;` that introduces it, as its token is read
    // (CFixIts), every one or none: one left would mismatch the others.
    llvm::SmallPtrSet<const void *, 8> Replaced;
    llvm::SmallVector<FixItHint, 4> Fixes;
    bool Fixable = true;
    auto Replace = [&](SourceLocation Keyword) {
      if (!Fixable || !Replaced.insert(Keyword.getPtrEncoding()).second)
        return;
      const std::optional<CharSourceRange> Range =
          FixIts.tokens(Keyword, Keyword, File, *SM, getLangOpts());
      const std::optional<FixItHint> Fix =
          Range ? FixIts.replacement(*Range, "struct", File, *SM, getLangOpts())
                : std::nullopt;
      if (Fix)
        Fixes.push_back(*Fix);
      else
        Fixable = false;
    };
    for (const TagDecl *Redecl : Definition->redecls())
      if (Redecl->isClass())
        Replace(Redecl->getInnerLocStart());
    for (const SourceLocation Keyword : Friends)
      Replace(Keyword);
    for (const SourceLocation Keyword : Elaborated)
      Replace(Keyword);
    if (!Fixable)
      continue;
    // The leading `public:`, which a struct has by default, is removed as a
    // repeated one is, only where removing it removes nothing but the label
    // in the file of the braces (labelRemoval), not one an included fragment
    // that other classes share gives; else the class is fixed without
    // removing it.
    const SourceLocation Brace = Definition->getBraceRange().getBegin();
    if (const auto *Access =
            dyn_cast_or_null<AccessSpecDecl>(firstWrittenMember(Definition));
        Access && Access->getAccess() == AS_public && Brace.isFileID())
      if (const std::optional<CharSourceRange> Range = labelRemoval(
              Access, SM->getFileID(Brace), FixIts, *SM, getLangOpts()))
        if (const std::optional<FixItHint> Fix =
                FixIts.tokenRemoval(*Range, SM->getFileID(Brace), *SM,
                                    getLangOpts()))
          Diag.push_back(*Fix);
    llvm::append_range(Diag, Fixes);
    Each.Fixable = true;
  }
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Fixing;
  for (const CFixedDefinition &Each : Fixed)
    if (Each.Fixable)
      Fixing.insert(Each.Definition);
  unfixMismatches(Fixing);
  for (const CFixedDefinition &Each : Fixed) {
    auto Diag = diag(Each.Loc, "%0 should be declared with 'struct', like "
                               "Malterlib types are")
                << diagnosticName(Each.Definition);
    if (Fixing.contains(Each.Definition))
      for (const FixItHint &Fix : Each.Fixes)
        Diag << Fix;
  }
  Definitions.clear();
  FriendKeywords.clear();
  ElaboratedKeywords.clear();
  ComparedWith.clear();
  KeptClassKeywords.clear();
}

/// Whether a declaration of \p Record, a specialization of \p Template or
/// its primary template, is written with `class` and isn't one of the
/// definitions in \p Fixing, which keeps its tag.
static bool keepsClass(const CXXRecordDecl *Record,
                       const llvm::SmallPtrSetImpl<const CXXRecordDecl *> &Fixing) {
  const CXXRecordDecl *Definition = Record->getDefinition();
  if (Definition && Fixing.contains(Definition))
    return false;
  return llvm::any_of(Record->redecls(), [](const TagDecl *Redecl) {
    return Redecl->isClass();
  });
}

void StructNotClassCheck::unfixMismatches(
    llvm::SmallPtrSetImpl<const CXXRecordDecl *> &Fixing) {
  // Clang compares the tag of a primary template with the tags of its
  // explicit and partial specializations, and with those of the template-ids
  // and explicit instantiations of its instances (comparedPrimary). Those
  // equal before the fixes stay equal: to a fixpoint, a definition is left
  // unfixed when a tag it is compared with, `class`, stays.
  for (bool Changed = true; Changed;) {
    Changed = false;
    for (const CXXRecordDecl *Definition :
         llvm::SmallVector<const CXXRecordDecl *, 8>(Fixing.begin(),
                                                       Fixing.end())) {
      bool Keeps = false;
      // A specialization fixed, compared with its primary template.
      if (isa<ClassTemplateSpecializationDecl>(Definition))
        if (const CXXRecordDecl *Primary = comparedPrimary(Definition))
          Keeps = keepsClass(Primary, Fixing);
      // Keywords fixed with it that are compared with a primary template.
      for (const CXXRecordDecl *Primary : ComparedWith.lookup(Definition))
        Keeps = Keeps || keepsClass(Primary, Fixing);
      // A primary template fixed, compared with each of its specializations,
      // the keywords of other definitions compared with it, and the
      // keywords that stay `class`.
      if (const ClassTemplateDecl *Template =
              Definition->getDescribedClassTemplate()) {
        Keeps = Keeps || KeptClassKeywords.contains(Definition);
        for (const ClassTemplateSpecializationDecl *Specialization :
             Template->specializations())
          if (Specialization->isExplicitSpecialization())
            Keeps = Keeps || keepsClass(Specialization, Fixing);
        llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 4> Partials;
        const_cast<ClassTemplateDecl *>(Template)->getPartialSpecializations(
            Partials);
        for (const ClassTemplatePartialSpecializationDecl *Partial : Partials)
          Keeps = Keeps || keepsClass(Partial, Fixing);
        for (const auto &[Other, Primaries] : ComparedWith)
          if (Primaries.contains(Definition) && !Fixing.contains(Other) &&
              Other->isClass())
            Keeps = true;
      }
      if (Keeps) {
        Fixing.erase(Definition);
        Changed = true;
      }
    }
  }
}

} // namespace clang::tidy::malterlib
