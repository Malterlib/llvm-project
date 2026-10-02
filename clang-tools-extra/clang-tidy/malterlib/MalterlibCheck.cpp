//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MalterlibCheck.h"
#include "NamingUtils.h"
#include "OncePerTypeVisitor.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/YAMLParser.h"
#include <mutex>

namespace clang::tidy::malterlib {
namespace {

/// Whether \p D is made by instantiating a template: a specialization of a
/// function, class or variable template, or a member of one, that isn't
/// written out.
bool isInstantiation(const Decl *D) {
  if (const auto *Function = dyn_cast<FunctionDecl>(D))
    return isTemplateInstantiation(Function->getTemplateSpecializationKind());
  if (const auto *Record = dyn_cast<CXXRecordDecl>(D))
    return isTemplateInstantiation(Record->getTemplateSpecializationKind());
  if (const auto *Variable = dyn_cast<VarDecl>(D))
    return isTemplateInstantiation(Variable->getTemplateSpecializationKind());
  return false;
}

/// The innermost instantiation that \p D is, or is in.
const Decl *enclosingInstantiation(const Decl *D) {
  for (; D; D = isa<TranslationUnitDecl>(D->getDeclContext())
                    ? nullptr
                    : Decl::castFromDeclContext(D->getDeclContext()))
    if (isInstantiation(D))
      return D;
  return nullptr;
}

/// Where \p Instantiation was requested, or an invalid location.
SourceLocation pointOfInstantiation(const Decl *Instantiation) {
  if (const auto *Function = dyn_cast<FunctionDecl>(Instantiation))
    return Function->getPointOfInstantiation();
  if (const auto *Specialization =
          dyn_cast<ClassTemplateSpecializationDecl>(Instantiation))
    return Specialization->getPointOfInstantiation();
  if (const auto *Record = dyn_cast<CXXRecordDecl>(Instantiation))
    if (const MemberSpecializationInfo *Info =
            Record->getMemberSpecializationInfo())
      return Info->getPointOfInstantiation();
  if (const auto *Variable = dyn_cast<VarDecl>(Instantiation))
    return Variable->getPointOfInstantiation();
  return {};
}

/// The declarations whose code refers to each instantiation, by the
/// locations of the references, which tell what requested an instantiation
/// from its point of instantiation. Made once for a translation unit, when
/// a warning is first reported in an instantiation.
class ReferenceIndex : public OncePerTypeVisitor<ReferenceIndex> {
public:
  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const { return true; }

  /// The declaration with the reference to \p Instantiation at \p Loc.
  const Decl *referrer(const Decl *Instantiation, SourceLocation Loc,
                       ASTContext &Context) {
    index(Context);
    return References.lookup({Instantiation, Loc.getRawEncoding()});
  }

  /// The first reference to \p Instantiation, and the declaration it is in,
  /// for one without a point of instantiation, like the call operator of a
  /// generic lambda.
  std::pair<SourceLocation, const Decl *>
  firstReference(const Decl *Instantiation, ASTContext &Context) {
    index(Context);
    return First.lookup(Instantiation);
  }

private:
  void index(ASTContext &Context) {
    if (const uint64_t AST = astIdentity(Context); IndexedAST != AST) {
      References.clear();
      First.clear();
      Within.clear();
      Traversed.clear();
      clearTraversedTypes();
      IndexedAST = AST;
      TraverseAST(Context);
      Traversed.clear();
      clearTraversedTypes();
    }
  }

public:

  // Each statement once: the operand of a co_await is met again in the
  // expressions that make and call its awaiter, and an initializer list in
  // its syntactic and semantic forms, so traversing them again would take a
  // time that grows exponentially with how deep they nest. Like the parent
  // map, through the hook that every statement is traversed through.
  bool dataTraverseStmtPre(Stmt *S) { return Traversed.insert(S).second; }

  bool TraverseDecl(Decl *D) {
    if (!D)
      return true;
    const bool Holds = isa<FunctionDecl, VarDecl, FieldDecl, CXXRecordDecl>(D);
    if (Holds)
      Within.push_back(D);
    const bool Result = RecursiveASTVisitor::TraverseDecl(D);
    if (Holds)
      Within.pop_back();
    return Result;
  }

  bool VisitDeclRefExpr(DeclRefExpr *Ref) {
    add(Ref->getDecl(), {Ref->getLocation(), Ref->getBeginLoc()});
    return true;
  }
  bool VisitMemberExpr(MemberExpr *Member) {
    add(Member->getMemberDecl(),
        {Member->getMemberLoc(), Member->getExprLoc(), Member->getBeginLoc()});
    return true;
  }
  bool VisitCXXConstructExpr(CXXConstructExpr *Construct) {
    const CXXConstructorDecl *Constructor = Construct->getConstructor();
    const SourceLocation Locs[] = {Construct->getLocation(),
                                   Construct->getBeginLoc()};
    add(Constructor, Locs);
    if (Constructor)
      add(Constructor->getParent(), Locs);
    return true;
  }
  // A destructor is used without a reference: by a variable, a temporary
  // and a delete. Only what instantiated and written code uses is looked at:
  // a template's own code has dependent types without a class.
  bool VisitVarDecl(VarDecl *Variable) {
    if ((!Variable->hasLocalStorage() && !Variable->isStaticLocal()) ||
        Variable->getType()->isDependentType())
      return true;
    if (const CXXRecordDecl *Record = completeRecord(Variable->getType())) {
      const Expr *Init = Variable->getInit();
      add(Record->getDestructor(),
          {Variable->getLocation(), Variable->getBeginLoc(),
           Init ? Init->getBeginLoc() : SourceLocation(),
           Init ? Init->getEndLoc() : SourceLocation()});
    }
    return true;
  }
  bool VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *Bind) {
    if (!Bind->getTemporary())
      return true;
    add(Bind->getTemporary()->getDestructor(),
        {Bind->getBeginLoc(), Bind->getEndLoc(), Bind->getExprLoc()});
    return true;
  }
  bool VisitCXXDeleteExpr(CXXDeleteExpr *Delete) {
    if (Delete->isTypeDependent() || Delete->isValueDependent() ||
        Delete->getArgument()->isTypeDependent())
      return true;
    const QualType Destroyed = Delete->getDestroyedType();
    if (const CXXRecordDecl *Record =
            Destroyed.isNull() ? nullptr : completeRecord(Destroyed))
      add(Record->getDestructor(), {Delete->getBeginLoc(), Delete->getEndLoc()});
    return true;
  }
  bool VisitTypeLoc(TypeLoc Type) {
    if (Type.getType().isNull())
      return true;
    if (const auto *Record = Type.getType()->getAsCXXRecordDecl())
      add(Record, {Type.getBeginLoc(), Type.getEndLoc()});
    return true;
  }

private:
  /// The class of \p Type with a definition, whose destructor can be looked
  /// up.
  static const CXXRecordDecl *completeRecord(QualType Type) {
    const CXXRecordDecl *Record = Type->getAsCXXRecordDecl();
    return Record && Record->hasDefinition() ? Record->getDefinition()
                                             : nullptr;
  }

  void add(const Decl *Referenced, llvm::ArrayRef<SourceLocation> Locs) {
    if (!Referenced || Within.empty() || !isInstantiation(Referenced))
      return;
    for (const SourceLocation Loc : Locs)
      if (Loc.isValid()) {
        References.try_emplace({Referenced, Loc.getRawEncoding()},
                               Within.back());
        First.try_emplace(Referenced, Loc, Within.back());
      }
  }

  uint64_t IndexedAST = 0;
  llvm::DenseMap<std::pair<const Decl *, SourceLocation::UIntTy>,
                 const Decl *>
      References;
  llvm::DenseMap<const Decl *, std::pair<SourceLocation, const Decl *>> First;
  std::vector<const Decl *> Within;
  llvm::DenseSet<const Stmt *> Traversed;
};

ReferenceIndex &sharedReferenceIndex() {
  static thread_local ReferenceIndex Index;
  return Index;
}

/// How clang names what an instantiation is of.
StringRef instantiationKind(const Decl *Instantiation) {
  if (const auto *Function = dyn_cast<FunctionDecl>(Instantiation))
    return Function->getPrimaryTemplate() ? "function template specialization"
                                          : "member function";
  if (isa<CXXRecordDecl>(Instantiation))
    return "template class";
  if (const auto *Variable = dyn_cast<VarDecl>(Instantiation))
    return Variable->isStaticDataMember() ? "static data member"
                                          : "variable template specialization";
  return "template";
}

} // namespace

MalterlibDiagnostic::~MalterlibDiagnostic() {
  // The warning first, then where it was instantiated.
  Builder.reset();
  if (!Instantiation)
    return;

  ASTContext &Context = Instantiation->getASTContext();
  struct Frame {
    SourceLocation Loc;
    const Decl *Instantiation;
  };
  llvm::SmallVector<Frame, 8> Frames;
  llvm::SmallPtrSet<const Decl *, 8> Seen;
  for (const Decl *Current = Instantiation;
       Current && Seen.insert(Current).second;) {
    const SourceLocation Loc = pointOfInstantiation(Current);
    // The call operator of a generic lambda is instantiated where it is
    // first called.
    if (Loc.isInvalid() && isa<FunctionDecl>(Current)) {
      const auto [Reference, Referrer] =
          sharedReferenceIndex().firstReference(Current, Context);
      if (Reference.isValid()) {
        Frames.push_back({Reference, Current});
        Current = Referrer ? enclosingInstantiation(Referrer) : nullptr;
        continue;
      }
    }
    // A lambda in an instantiation, or another member without a point of
    // instantiation of its own, is instantiated with what it is in.
    if (Loc.isInvalid()) {
      const auto *Parent =
          isa<TranslationUnitDecl>(Current->getDeclContext())
              ? nullptr
              : Decl::castFromDeclContext(Current->getDeclContext());
      Current = Parent ? enclosingInstantiation(Parent) : nullptr;
      continue;
    }
    Frames.push_back({Loc, Current});
    // What requested it, which is itself an instantiation or written code,
    // where the backtrace ends.
    const Decl *Referrer =
        sharedReferenceIndex().referrer(Current, Loc, Context);
    Current = Referrer ? enclosingInstantiation(Referrer) : nullptr;
  }

  // As many as clang shows of its own backtraces, the outermost and the
  // innermost.
  const unsigned Limit = Context.getDiagnostics().getTemplateBacktraceLimit();
  const size_t SkipStart = Limit && Frames.size() > Limit
                               ? Limit / 2 + Limit % 2
                               : Frames.size();
  const size_t SkipEnd = Limit && Frames.size() > Limit
                             ? Frames.size() - Limit / 2
                             : Frames.size();
  for (size_t iFrame = 0; iFrame < Frames.size(); ++iFrame) {
    const Frame &Current = Frames[iFrame];
    if (iFrame == SkipStart && SkipStart < SkipEnd) {
      Check.diag(Current.Loc,
                 "(skipping %0 contexts in backtrace; use "
                 "-ftemplate-backtrace-limit=0 to see all)",
                 DiagnosticIDs::Note)
          << static_cast<unsigned>(SkipEnd - SkipStart);
      iFrame = SkipEnd - 1;
      continue;
    }
    // Not with template arguments too large to print, of it or of the
    // classes it is in, `TCReader<...>::f_Read<...>`.
    const auto *Instantiation = cast<NamedDecl>(Current.Instantiation);
    if (hasTooLargeTemplateArguments(Instantiation)) {
      Check.diag(Current.Loc, "in instantiation of %0 '%1' requested here",
                 DiagnosticIDs::Note)
          << instantiationKind(Current.Instantiation)
          << abbreviatedName(Instantiation);
      continue;
    }
    Check.diag(Current.Loc, "in instantiation of %0 %q1 requested here",
               DiagnosticIDs::Note)
        << instantiationKind(Current.Instantiation)
        << cast<NamedDecl>(Current.Instantiation);
  }
}

namespace {

/// The errors of the configuration reported to each context while checks made
/// for it are alive, which report each once rather than once per check, and
/// again for the checks of the next file or of the next parse of the file,
/// which clangd makes another context for, also at the same address.
struct CReported {
  unsigned Checks = 0;
  llvm::StringSet<> Errors;
};
std::mutex ReportedMutex;
llvm::DenseMap<const ClangTidyContext *, CReported> Reported;

/// A list, `;`-separated, as an edit that replaces a list with it.
std::string replacingList(StringRef List) {
  return List.empty() ? "-*" : ("-*;" + List).str();
}

/// The values of an option of each configuration, \p Value and those it
/// replaced when configurations were merged, by priority.
SmallVector<std::pair<unsigned, StringRef>, 2>
valuesInOrder(const ClangTidyOptions::ClangTidyValue &Value) {
  SmallVector<std::pair<unsigned, StringRef>, 2> Values;
  for (const auto &[Replaced, Priority] : Value.Replaced)
    Values.push_back({Priority, Replaced});
  Values.push_back({Value.Priority, Value.Value});
  llvm::stable_sort(Values, llvm::less_first());
  return Values;
}

} // namespace

MalterlibCheck::MalterlibCheck(StringRef Name, ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context), CheckName(Name.str()), Context(Context) {
  {
    std::lock_guard<std::mutex> Lock(ReportedMutex);
    ++Reported[Context].Checks;
  }
  auto ReportOnce = [&](const std::string &Key, const std::string &Message) {
    // Not when the checks are only listed, `--list-checks` or
    // `--verify-config`, which report nothing.
    if (!Context->hasDiagnosticsEngine())
      return;
    {
      std::lock_guard<std::mutex> Lock(ReportedMutex);
      if (!Reported[Context].Errors.insert(Key).second)
        return;
    }
    // The message as it is, also a `%` of a path, not as a format.
    configurationDiag("%0", DiagnosticIDs::Error) << Message;
  };
  if (llvm::StringSet<> *Collector = Context->getOptionsCollector())
    Collector->insert(OptionsFileOption);
  const ClangTidyOptions::OptionMap &CheckOptions =
      Context->getOptions().CheckOptions;
  if (auto It = CheckOptions.find(OptionsFileOption);
      It == CheckOptions.end() || It->second.Value.empty())
    ReportOnce(
        "", (llvm::Twine("the Malterlib checks need the option '") +
             OptionsFileOption + "', the Malterlib options file, like '" +
             OptionsFileOption +
             ": Malterlib/Core/ClangTidyOptions.yaml' in the CheckOptions of "
             "the .clang-tidy file at the root, relative to the directory of "
             "that file")
                .str());
  else {
    std::string Error;
    File = loadOptionsFile(It->second.Value, Error, Context->getFileSystem());
    if (!File)
      ReportOnce(It->second.Value, Error);
  }
  Vocabulary = std::make_shared<const CVocabulary>(
      [&](StringRef Option, StringRef BuiltIn) {
        std::string List = BuiltIn.str();
        if (std::optional<StringRef> Edit = ofFile(Option))
          List = editList(List, *Edit);
        // Of each configuration, also those a configuration that inherits it
        // replaced.
        if (const ClangTidyOptions::ClangTidyValue *Value =
                configuredValue(Option))
          for (const auto &[Priority, Edit] : valuesInOrder(*Value))
            List = editList(List, Edit);
        return List;
      });
  setVocabulary(Vocabulary);
}

MalterlibCheck::~MalterlibCheck() {
  std::lock_guard<std::mutex> Lock(ReportedMutex);
  if (auto It = Reported.find(Context);
      It != Reported.end() && --It->second.Checks == 0)
    Reported.erase(It);
}

void MalterlibCheck::storeOptions(ClangTidyOptions::OptionMap &Opts) {
  storeCheckOptions(Opts);
  for (const auto &Name : ListNames)
    if (auto It = Opts.find((CheckName + "." + Name.getKey()).str());
        It != Opts.end())
      It->second.Value = replacingList(It->second.Value);
  const ClangTidyOptions::OptionMap &CheckOptions =
      Context->getOptions().CheckOptions;
  if (auto It = CheckOptions.find(OptionsFileOption); It != CheckOptions.end())
    Opts[OptionsFileOption] = It->second;
  Vocabulary->forEachList([&](StringRef Name, const CNameList &List) {
    Opts[Name] = ClangTidyOptions::ClangTidyValue(replacingList(List.joined()));
  });
}

const ClangTidyOptions::ClangTidyValue *
MalterlibCheck::configuredValue(StringRef Key) const {
  // Each option the check reads is one --verify-config knows.
  if (llvm::StringSet<> *Collector = Context->getOptionsCollector())
    Collector->insert(Key);
  const ClangTidyOptions::OptionMap &CheckOptions =
      Context->getOptions().CheckOptions;
  if (auto It = CheckOptions.find(Key); It != CheckOptions.end())
    return &It->second;
  return nullptr;
}

SmallVector<std::pair<std::string, std::string>, 2>
MalterlibCheck::configuredInOrder(StringRef Name) const {
  const std::string Local = CheckName + "." + Name.str();
  // Each value of each configuration, also those a configuration that
  // inherits it replaced, by priority, and of one configuration the plain one
  // first.
  struct CValue {
    unsigned Priority;
    bool Local;
    StringRef Key;
    StringRef Value;
  };
  SmallVector<CValue, 4> All;
  for (bool IsLocal : {false, true}) {
    StringRef Key = IsLocal ? StringRef(Local) : Name;
    if (const ClangTidyOptions::ClangTidyValue *Value = configuredValue(Key))
      for (const auto &[Priority, Edit] : valuesInOrder(*Value))
        All.push_back({Priority, IsLocal, Key, Edit});
  }
  llvm::stable_sort(All, [](const CValue &Left, const CValue &Right) {
    return std::tie(Left.Priority, Left.Local) <
           std::tie(Right.Priority, Right.Local);
  });
  SmallVector<std::pair<std::string, std::string>, 2> Values;
  for (const CValue &Value : All)
    Values.push_back({Value.Key.str(), Value.Value.str()});
  return Values;
}

std::optional<StringRef> MalterlibCheck::ofFile(StringRef Key) const {
  if (!File)
    return std::nullopt;
  if (auto It = File->find(Key); It != File->end())
    return StringRef(It->second);
  return std::nullopt;
}

StringRef MalterlibCheck::listOption(StringRef Name, StringRef BuiltIn) {
  ListNames.insert(Name);
  const std::string Local = CheckName + "." + Name.str();
  std::string List = BuiltIn.str();
  for (StringRef Key : {Name, StringRef(Local)})
    if (std::optional<StringRef> Edit = ofFile(Key))
      List = editList(List, *Edit);
  for (const auto &[Key, Edit] : configuredInOrder(Name))
    List = editList(List, Edit);
  return Lists.emplace_back(std::move(List));
}

std::optional<std::pair<std::string, std::string>>
MalterlibCheck::findValueOption(StringRef Name) const {
  // The configured value of the highest priority, or else that of the
  // options file.
  if (auto Values = configuredInOrder(Name); !Values.empty())
    return Values.back();
  const std::string Local = CheckName + "." + Name.str();
  for (StringRef Key : {StringRef(Local), Name})
    if (std::optional<StringRef> Value = ofFile(Key))
      return std::make_pair(Key.str(), Value->str());
  return std::nullopt;
}

std::string MalterlibCheck::valueOption(StringRef Name,
                                        StringRef BuiltIn) const {
  if (auto Found = findValueOption(Name))
    return Found->second;
  return BuiltIn.str();
}

// What clang-tidy reports of a value of an option it can't read.
static constexpr StringRef BadValue =
    "invalid configuration value '%0' for option '%1'; expected %select{a "
    "bool|an integer}2";

unsigned MalterlibCheck::unsignedOption(StringRef Name,
                                        unsigned BuiltIn) const {
  auto Found = findValueOption(Name);
  if (!Found)
    return BuiltIn;
  unsigned Value = BuiltIn;
  if (StringRef(Found->second).trim().getAsInteger(10, Value)) {
    configurationDiag(BadValue) << Found->second << Found->first << 1;
    return BuiltIn;
  }
  return Value;
}

bool MalterlibCheck::boolOption(StringRef Name, bool BuiltIn) const {
  auto Found = findValueOption(Name);
  if (!Found)
    return BuiltIn;
  const StringRef Value = StringRef(Found->second).trim();
  if (std::optional<bool> Parsed = llvm::yaml::parseBool(Value))
    return *Parsed;
  // Like clang-tidy, a number too.
  long long Number = 0;
  if (!Value.getAsInteger(10, Number))
    return Number != 0;
  configurationDiag(BadValue) << Found->second << Found->first << 0;
  return BuiltIn;
}

MalterlibDiagnostic MalterlibCheck::diag(SourceLocation Loc,
                                         StringRef Description,
                                         DiagnosticIDs::Level Level) {
  const Decl *Instantiation =
      Level == DiagnosticIDs::Note ? nullptr : instantiationAt(Loc);
  return MalterlibDiagnostic(ClangTidyCheck::diag(Loc, Description, Level),
                             *this, Instantiation);
}

void MalterlibCheck::run(
    const ast_matchers::MatchFinder::MatchResult &Result) {
  // Not in an instantiation another file makes, which checks it there, like
  // the inline members of `extern template struct TCBox<int>;` this file
  // instantiates.
  // But the explicit instantiation declaration itself, as it is written, is.
  for (const auto &[Id, Node] : Result.Nodes.getMap())
    if (const auto *D = Node.get<Decl>();
        D && isExternInstantiation(D) &&
        !(isa<ClassTemplateSpecializationDecl>(D) &&
          cast<ClassTemplateSpecializationDecl>(D)->getSpecializationKind() ==
              TSK_ExplicitInstantiationDeclaration))
      return;
  // The vocabulary of this check's configuration, also when the thread
  // checked another translation unit since it was made.
  if (&vocabulary() != Vocabulary.get())
    setVocabulary(Vocabulary);
  CurrentResult = &Result;
  check(Result);
  CurrentResult = nullptr;
}

const Decl *MalterlibCheck::instantiationAt(SourceLocation Loc) const {
  if (Loc.isInvalid())
    return nullptr;
  // Only an instantiation of a template that the warning is in tells where
  // it comes from: one the check found it through can be elsewhere.
  auto Contains = [&](const Decl *Instantiation) {
    const SourceManager &SM = Instantiation->getASTContext().getSourceManager();
    SourceRange Range = Instantiation->getSourceRange();
    // A function also from its type and parameters, which the call operator
    // of a lambda, whose range starts at its body, has before it.
    if (const auto *Function = dyn_cast<FunctionDecl>(Instantiation);
        Function && Range.isValid()) {
      auto Extend = [&](SourceLocation Begin) {
        if (Begin.isValid() &&
            SM.isBeforeInTranslationUnit(SM.getExpansionLoc(Begin),
                                         SM.getExpansionLoc(Range.getBegin())))
          Range.setBegin(Begin);
      };
      if (const TypeSourceInfo *Type = Function->getTypeSourceInfo())
        Extend(Type->getTypeLoc().getBeginLoc());
      for (const ParmVarDecl *Parameter : Function->parameters())
        Extend(Parameter->getBeginLoc());
    }
    const SourceLocation At = SM.getExpansionLoc(Loc);
    return Range.isValid() &&
           !SM.isBeforeInTranslationUnit(At,
                                         SM.getExpansionLoc(Range.getBegin())) &&
           !SM.isBeforeInTranslationUnit(SM.getExpansionLoc(Range.getEnd()),
                                         At);
  };
  // The innermost one that holds the warning.
  auto Holding = [&](const Decl *Instantiation) -> const Decl * {
    for (; Instantiation;
         Instantiation =
             isa<TranslationUnitDecl>(Instantiation->getDeclContext())
                 ? nullptr
                 : enclosingInstantiation(Decl::castFromDeclContext(
                       Instantiation->getDeclContext())))
      if (Contains(Instantiation))
        return Instantiation;
    return nullptr;
  };
  if (ScopeInstantiation)
    return Holding(enclosingInstantiation(ScopeInstantiation));
  if (!CurrentResult)
    return nullptr;
  ASTContext &Context = *CurrentResult->Context;
  for (const auto &[Id, Node] : CurrentResult->Nodes.getMap()) {
    DynTypedNode Current = Node;
    const Decl *D = Current.get<Decl>();
    for (unsigned Depth = 0; !D && Depth < 256; ++Depth) {
      const auto Parents = instantiatedParents(Current, Context);
      if (Parents.empty())
        break;
      Current = Parents[0];
      D = Current.get<Decl>();
    }
    if (const Decl *Instantiation = D ? Holding(enclosingInstantiation(D))
                                      : nullptr)
      return Instantiation;
  }
  return nullptr;
}

} // namespace clang::tidy::malterlib
