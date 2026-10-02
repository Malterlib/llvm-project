//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Runs the Malterlib clang-tidy checks inside a normal compile. The plugin is
// linked into clang and runs when it gets an argument:
//
//   -fplugin-arg-malterlib-checks=<glob>       Checks to run, added to the
//                                              ones the configuration enables.
//   -fplugin-arg-malterlib-config-file=<path>  Configuration file to use
//                                              instead of the .clang-tidy
//                                              files of the source directories.
//   -fplugin-arg-malterlib-options-file=<path> The Malterlib options file,
//                                              instead of the one the
//                                              configuration names in
//                                              malterlib.OptionsFile, which
//                                              the checks need.
//   -fplugin-arg-malterlib-header-filter=<regex>
//   -fplugin-arg-malterlib-exclude-header-filter=<regex>
//                                              Matched against the absolute
//                                              name of a header without `.`
//                                              and `..` components.
//   -fplugin-arg-malterlib-repositories=<path>
//                                              A JSON file of the rules of
//                                              the repositories, which decide
//                                              instead of the header filters
//                                              which files are checked, also
//                                              the source: an array of
//                                              `{"Location": <absolute path>,
//                                              "Tidy": {"Enabled": <bool>,
//                                              "IncludeFiles": [<wildcard>],
//                                              "ExcludeFiles": [<wildcard>]}}`.
//                                              A file is checked when the
//                                              deepest repository it is in is
//                                              enabled, and its path relative
//                                              to it matches an include, if
//                                              there are any, and no exclude.
//                                              In a wildcard, `*` matches any
//                                              text, also `/`, and `?` any
//                                              character.
//   -fplugin-arg-malterlib-warnings-as-errors=<glob>
//   -fplugin-arg-malterlib-enable              Run with the configuration only.
//   -fplugin-arg-malterlib-profile             Print the time each check takes.
//
// -Werror makes the warnings of the checks errors, unless warnings-as-errors
// is given, and -w disables the checks.
//
// What a precompiled header declares is checked when it is built; a compile
// that uses it checks the definitions it instantiates of its templates, but
// not what an explicit instantiation declaration, `extern template`, says
// another file instantiates, which is checked there.
//
//===----------------------------------------------------------------------===//

#include "../../ClangTidy.h"
#include "../../ClangTidyDiagnosticConsumer.h"
#include "../../ClangTidyModule.h"
#include "../NamingUtils.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"

namespace clang::tidy::malterlib {
namespace {

/// Whether \p String matches \p WrittenPattern like Malterlib's wildcards,
/// which mib tidy matches the same rules with: `*` matches any text, also
/// `/`, up to the first place the text after it matches, and `?` any
/// character. A run of them matches at least as many characters as it has
/// `?`, also when a `*` comes first, `*?.cpp`.
bool matchesWildcard(StringRef String, StringRef WrittenPattern) {
  std::string Pattern;
  for (size_t iWritten = 0; iWritten < WrittenPattern.size();) {
    const size_t End = WrittenPattern.find_first_not_of("*?", iWritten);
    const StringRef Run = WrittenPattern.slice(
        iWritten, End == StringRef::npos ? WrittenPattern.size() : End);
    if (Run.empty()) {
      Pattern += WrittenPattern[iWritten++];
      continue;
    }
    Pattern.append(Run.count('?'), '?');
    if (Run.contains('*'))
      Pattern += '*';
    iWritten += Run.size();
  }
  size_t iString = 0;
  size_t iPattern = 0;
  while (iString < String.size() && iPattern < Pattern.size()) {
    if (Pattern[iPattern] == '*') {
      while (iPattern < Pattern.size() && Pattern[iPattern] == '*')
        ++iPattern;
      size_t End = Pattern.find_first_of("*?", iPattern);
      if (End == std::string::npos)
        End = Pattern.size();
      const StringRef Literal = StringRef(Pattern).slice(iPattern, End);
      while (iString < String.size()) {
        if (!Literal.empty() && String.substr(iString).starts_with(Literal))
          break;
        ++iString;
      }
    } else if (Pattern[iPattern] == '?') {
      ++iPattern;
      ++iString;
    } else {
      if (Pattern[iPattern] != String[iString])
        break;
      ++iPattern;
      ++iString;
    }
  }
  while (iPattern < Pattern.size() && Pattern[iPattern] == '*')
    ++iPattern;
  return iString == String.size() && iPattern == Pattern.size();
}

/// The rules of the repositories, which decide which files are checked.
class RepositoryRules {
public:
  /// Reads the rules from the JSON \p Text, or tells what is wrong with it.
  static llvm::Expected<RepositoryRules> parse(StringRef Text) {
    llvm::Expected<llvm::json::Value> Json = llvm::json::parse(Text);
    if (!Json)
      return Json.takeError();
    RepositoryRules Rules;
    const llvm::json::Array *Entries = Json->getAsArray();
    if (!Entries)
      return llvm::createStringError("expected an array of repositories");
    for (const llvm::json::Value &Entry : *Entries) {
      const llvm::json::Object *Object = Entry.getAsObject();
      const std::optional<StringRef> Location =
          Object ? Object->getString("Location") : std::nullopt;
      if (!Location)
        return llvm::createStringError(
            "expected a repository with a Location");
      Repository Added;
      Added.Location = Location->rtrim('/').str();
      // A value of another type than its own is an error, not a setting that
      // turns checking off, `"Enabled": "true"`.
      if (const llvm::json::Object *Tidy = Object->getObject("Tidy")) {
        if (const llvm::json::Value *Enabled = Tidy->get("Enabled")) {
          if (!Enabled->getAsBoolean())
            return llvm::createStringError("expected Tidy.Enabled of " +
                                           Added.Location + " to be a boolean");
          Added.Enabled = *Enabled->getAsBoolean();
        }
        for (auto [Name, Files] :
             {std::pair{"IncludeFiles", &Added.IncludeFiles},
              std::pair{"ExcludeFiles", &Added.ExcludeFiles}}) {
          const llvm::json::Value *Value = Tidy->get(Name);
          if (!Value)
            continue;
          const llvm::json::Array *Wildcards = Value->getAsArray();
          if (!Wildcards)
            return llvm::createStringError(llvm::Twine("expected Tidy.") +
                                           Name + " of " + Added.Location +
                                           " to be an array of strings");
          for (const llvm::json::Value &Wildcard : *Wildcards) {
            const std::optional<StringRef> String = Wildcard.getAsString();
            if (!String)
              return llvm::createStringError(llvm::Twine("expected Tidy.") +
                                             Name + " of " + Added.Location +
                                             " to be an array of strings");
            Files->push_back(String->str());
          }
        }
      } else if (Object->get("Tidy"))
        return llvm::createStringError("expected Tidy of " + Added.Location +
                                       " to be an object");
      // Also where it is past symbolic links, `/private/tmp/Repository` of
      // `/tmp/Repository`, which the real names of files are compared with.
      llvm::SmallString<256> Real;
      if (!llvm::sys::fs::real_path(Added.Location, Real)) {
        const std::string RealLocation =
            StringRef(llvm::sys::path::convert_to_slash(Real)).rtrim('/').str();
        if (RealLocation != Added.Location) {
          Repository Resolved = Added;
          Resolved.Location = RealLocation;
          Rules.Repositories.push_back(std::move(Resolved));
        }
      }
      Rules.Repositories.push_back(std::move(Added));
    }
    // The deepest repository a file is in decides, so the longest location
    // is looked at first.
    llvm::stable_sort(Rules.Repositories,
                      [](const Repository &Left, const Repository &Right) {
                        return Left.Location.size() > Right.Location.size();
                      });
    return Rules;
  }

  /// Whether the file of the absolute name \p Name without `.` and `..`
  /// components is checked, by the rules of the repository it is in, or
  /// nothing when it is in none.
  std::optional<bool> rule(StringRef Name) const {
    for (const Repository &Repository : Repositories) {
      if (!Name.starts_with(Repository.Location) ||
          !Name.substr(Repository.Location.size()).starts_with("/"))
        continue;
      if (!Repository.Enabled)
        return false;
      const StringRef Relative = Name.substr(Repository.Location.size() + 1);
      auto Matches = [&](const std::string &Wildcard) {
        return matchesWildcard(Relative, Wildcard);
      };
      return (Repository.IncludeFiles.empty() ||
              llvm::any_of(Repository.IncludeFiles, Matches)) &&
             llvm::none_of(Repository.ExcludeFiles, Matches);
    }
    return std::nullopt;
  }

  /// Whether the file of \p Name, the absolute name without `.` and `..`
  /// components, is checked: by the repository it is in, \p Real past
  /// symbolic links, `Include/Header.h` linking to `Repository/Header.h`,
  /// or else by the one its name is in. Where it is is told from the name as
  /// written, since the `..` of `Link/../Source.cpp` is in where the link
  /// leads.
  bool isChecked(StringRef Name, StringRef Real) const {
    if (!Real.empty()) {
      const std::string RealName = llvm::sys::path::convert_to_slash(Real);
      if (RealName != Name)
        if (const std::optional<bool> Checked = rule(RealName))
          return *Checked;
    }
    return rule(Name).value_or(false);
  }

private:
  struct Repository {
    std::string Location;
    bool Enabled = false;
    std::vector<std::string> IncludeFiles;
    std::vector<std::string> ExcludeFiles;
  };
  std::vector<Repository> Repositories;
};

/// Reads the configuration files like `FileOptionsProvider`, through the
/// file system of the compilation, also what `-ivfsoverlay` gives, and
/// reports one that can't be parsed as an error of the compilation.
class ReportingFileOptionsProvider : public FileOptionsProvider {
public:
  ReportingFileOptionsProvider(
      ClangTidyGlobalOptions GlobalOptions, ClangTidyOptions DefaultOptions,
      ClangTidyOptions OverrideOptions, DiagnosticsEngine &Diags,
      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS)
      : FileOptionsProvider(std::move(GlobalOptions), std::move(DefaultOptions),
                            std::move(OverrideOptions), std::move(FS)) {
    DiagnosticsEngine *Engine = &Diags;
    ConfigHandlers.clear();
    ConfigHandlers.emplace_back(
        ".clang-tidy",
        [this, Engine](llvm::MemoryBufferRef Config)
            -> llvm::ErrorOr<ClangTidyOptions> {
          // Before reporting, one that can't be parsed is read as no options
          // without printing anything: it needn't be used, like the one of a
          // parent directory that the source file's doesn't inherit.
          if (!Reporting) {
            llvm::ErrorOr<ClangTidyOptions> Parsed = parseConfigurationWithDiags(
                Config, [](const llvm::SMDiagnostic &) {});
            return Parsed ? Parsed : llvm::ErrorOr<ClangTidyOptions>(
                                         ClangTidyOptions());
          }
          llvm::ErrorOr<ClangTidyOptions> Parsed = parseConfiguration(Config);
          if (!Parsed)
            Engine->Report(Engine->getCustomDiagID(
                DiagnosticsEngine::Error,
                "invalid malterlib plugin configuration '%0': %1"))
                << Config.getBufferIdentifier()
                << Parsed.getError().message();
          return Parsed;
        });
  }

  /// Reports the configurations that can't be parsed from now on: the
  /// context looks up the options of a placeholder file in the working
  /// directory first, whose configuration needn't be the one of the source
  /// file, like when the source file's doesn't inherit it. What that lookup
  /// read is read again, so that one that can't be parsed is reported when a
  /// source file uses it.
  void startReporting() {
    Reporting = true;
    CachedOptions.Memorized.clear();
    CachedOptions.Storage.clear();
  }

private:
  bool Reporting = false;
};

/// Ends the source file of the diagnostic consumer of the checks, which
/// reports the last diagnostic it holds back. Ends it once, both when the main
/// file ends and when the AST consumer is destroyed, which clang skips with
/// -disable-free.
class SourceFileEnd {
public:
  explicit SourceFileEnd(DiagnosticConsumer &Consumer) : Consumer(Consumer) {}

  void end() {
    if (Ended)
      return;
    Ended = true;
    Consumer.EndSourceFile();
  }

private:
  DiagnosticConsumer &Consumer;
  bool Ended = false;
};

/// Ends the source file of the checks when the main file ends. Registered
/// before the callbacks of the checks, it runs after them, so it also reports
/// what they find at the end of the main file, while the compile still
/// reports diagnostics.
class EndOfMainFileCallback : public PPCallbacks {
public:
  explicit EndOfMainFileCallback(std::shared_ptr<SourceFileEnd> End)
      : End(std::move(End)) {}

  void EndOfMainFile() override { End->end(); }

private:
  std::shared_ptr<SourceFileEnd> End;
};

/// Adds files that exist as dependencies of the compilation when the main
/// file ends, after the source and its headers, so that a build that tracks
/// them, `-MD`, compiles and checks again when one changes.
class DependenciesCallback : public PPCallbacks {
public:
  DependenciesCallback(CompilerInstance &Compiler,
                       std::vector<std::string> Files)
      : Compiler(Compiler), Files(std::move(Files)) {}

  void EndOfMainFile() override {
    llvm::vfs::FileSystem &FS = Compiler.getVirtualFileSystem();
    for (const std::string &Name : Files)
      if (!Name.empty() && FS.exists(Name))
        for (const std::shared_ptr<DependencyCollector> &Collector :
             Compiler.getDependencyCollectors())
          Collector->maybeAddDependency(Name, /*FromModule=*/false,
                                        /*IsSystem=*/false,
                                        /*IsModuleFile=*/false,
                                        /*IsDirectModuleImport=*/false,
                                        /*IsMissing=*/false);
  }

private:
  CompilerInstance &Compiler;
  std::vector<std::string> Files;
};

class MalterlibPluginAction : public PluginASTAction {
  /// Gives the context and the diagnostics engine the lifetime of the AST
  /// consumer.
  class WrapConsumer : public MultiplexConsumer {
    std::unique_ptr<ClangTidyContext> Context;
    std::unique_ptr<DiagnosticsEngine> DiagEngine;
    std::shared_ptr<SourceFileEnd> End;
    /// The checks that don't run, with -w, which the callbacks they gave the
    /// preprocessor, like those of macro naming, still refer to.
    std::unique_ptr<ASTConsumer> Idle;

    /// Records the definitions the compile instantiates, of functions and
    /// variables, also those whose templates are from a precompiled header,
    /// which the traversal doesn't reach through their templates.
    class CInstantiations : public ASTMutationListener {
    public:
      std::vector<const Decl *> Definitions;
      void FunctionDefinitionInstantiated(const FunctionDecl *D) override {
        Definitions.push_back(D);
      }
      void VariableDefinitionInstantiated(const VarDecl *D) override {
        Definitions.push_back(D);
      }
      // And the specializations of class templates it makes, whose members,
      // like `typename t_CTraits::CPointer m_Value`, their types tell.
      void AddedCXXTemplateSpecialization(
          const ClassTemplateDecl *,
          const ClassTemplateSpecializationDecl *D) override {
        Definitions.push_back(D);
      }
      // And of function templates, also only declared, `requires {
      // fg_Test<CTraits>(nullptr); }`, whose signatures their parameters'
      // types tell.
      void AddedCXXTemplateSpecialization(const FunctionTemplateDecl *,
                                          const FunctionDecl *D) override {
        Definitions.push_back(D);
      }
      // Also one a precompiled header declared, `TCBox<CTraits> *`, whose
      // definition this compile instantiates.
      void CompletedTagDefinition(const TagDecl *D) override {
        if (const auto *Specialization =
                dyn_cast<ClassTemplateSpecializationDecl>(D))
          Definitions.push_back(Specialization);
      }
    };
    CInstantiations Instantiations;

  public:
    WrapConsumer(std::unique_ptr<ClangTidyContext> Context,
                 std::unique_ptr<DiagnosticsEngine> DiagEngine,
                 std::shared_ptr<SourceFileEnd> End,
                 std::vector<std::unique_ptr<ASTConsumer>> Consumers,
                 std::unique_ptr<ASTConsumer> Idle)
        : MultiplexConsumer(std::move(Consumers)), Context(std::move(Context)),
          DiagEngine(std::move(DiagEngine)), End(std::move(End)),
          Idle(std::move(Idle)) {}

    void HandleTranslationUnit(ASTContext &Ctx) override {
      const std::vector<Decl *> PreviousScope = Ctx.getTraversalScope();
      Ctx.setTraversalScope(reportedDecls(Ctx));
      MultiplexConsumer::HandleTranslationUnit(Ctx);
      Ctx.setTraversalScope(PreviousScope);
    }

    ~WrapConsumer() override { End->end(); }

    /// The checks have no listener of their own, so the one of the
    /// multiplexed consumers is unused.
    ASTMutationListener *GetASTMutationListener() override {
      return &Instantiations;
    }

  private:
    /// The top level declarations whose diagnostics can be reported: those in
    /// the main file and in the headers the header filters let through. Every
    /// compile includes many headers, and checking only these keeps the
    /// checks cheap. Declarations from a precompiled header were checked when
    /// it was built, but not the definitions this compile instantiates of its
    /// templates, which are added.
    ///
    /// A declaration is selected by its expansion location, so one that a
    /// macro of the main file declares in a header that is not reported, which
    /// clang-tidy would report at the macro, is not checked. Defining a macro
    /// in a source and expanding it in a header is rare, and following
    /// spelling locations would mean traversing every header.
    std::vector<Decl *> reportedDecls(ASTContext &Ctx) {
      const ClangTidyOptions &Options = Context->getOptions();
      // An empty filter, like clang-tidy's default, checks no header.
      const std::optional<llvm::Regex> HeaderFilter =
          Options.HeaderFilterRegex.value_or("").empty()
              ? std::nullopt
              : std::optional<llvm::Regex>(*Options.HeaderFilterRegex);
      const std::optional<llvm::Regex> ExcludeHeaderFilter =
          Options.ExcludeHeaderFilterRegex.value_or("").empty()
              ? std::nullopt
              : std::optional<llvm::Regex>(*Options.ExcludeHeaderFilterRegex);
      // A filter of a configuration file that isn't a regular expression
      // would check no header, like one given as an argument.
      for (const auto &[Name, Filter] :
           {std::pair<StringRef, const llvm::Regex *>{
                "HeaderFilterRegex", HeaderFilter ? &*HeaderFilter : nullptr},
            {"ExcludeHeaderFilterRegex",
             ExcludeHeaderFilter ? &*ExcludeHeaderFilter : nullptr}}) {
        std::string Error;
        if (Filter && !Filter->isValid(Error)) {
          DiagnosticsEngine &Diags = Ctx.getDiagnostics();
          Diags.Report(Diags.getCustomDiagID(
              DiagnosticsEngine::Error,
              "invalid regular expression in the malterlib plugin's %0: %1"))
              << Name << Error;
        }
      }
      const bool SystemHeaders = Options.SystemHeaders.value_or(false);

      const SourceManager &SM = Ctx.getSourceManager();
      llvm::DenseMap<FileID, bool> Reported;
      auto IsReported = [&](SourceLocation Loc) {
        const FileID File = SM.getFileID(Loc);
        auto [It, Inserted] = Reported.try_emplace(File, false);
        if (!Inserted)
          return It->second;
        if (File == SM.getMainFileID())
          It->second = true;
        else if (!SystemHeaders && SM.isInSystemHeader(Loc))
          It->second = false;
        else if (OptionalFileEntryRef Entry = SM.getFileEntryRefForID(File)) {
          // A header included through a relative or `..` path is matched by
          // its absolute name, like the forwarded diagnostics are, and also
          // by where it is, past symbolic links, `Include/Header.h` linking
          // to `Repository/Header.h`.
          const std::string Name =
              normalizeHeaderName(Entry->getName(), SM.getFileManager());
          // With forward slashes like the name, also on Windows.
          const std::string Real = llvm::sys::path::convert_to_slash(
              SM.getFileManager().getCanonicalName(*Entry));
          const auto &IsHeaderReported =
              Context->getGlobalOptions().IsHeaderReported;
          // The rules of the repositories tell it by where it is themselves;
          // the header filters match either name, and either name that an
          // exclude matches leaves it out.
          const bool Linked = !Real.empty() && Real != Name;
          if (IsHeaderReported)
            It->second = IsHeaderReported(Name, Real);
          else
            It->second = HeaderFilter &&
                         (HeaderFilter->match(Name) ||
                          (Linked && HeaderFilter->match(Real))) &&
                         !(ExcludeHeaderFilter &&
                           (ExcludeHeaderFilter->match(Name) ||
                            (Linked && ExcludeHeaderFilter->match(Real))));
        }
        else
          // Virtual buffers, like the predefines.
          It->second = false;
        return It->second;
      };

      // Where the files that are reported are included, and the files that
      // include them: a function whose body includes one, like `#include
      // "Body.hpp"` anywhere in its body, is checked.
      llvm::DenseMap<FileID, llvm::SmallVector<unsigned, 4>> ReportedIncludes;
      for (unsigned iEntry = 0, nEntries = SM.local_sloc_entry_size();
           iEntry < nEntries; ++iEntry) {
        const SrcMgr::SLocEntry &Entry = SM.getLocalSLocEntry(iEntry);
        if (!Entry.isFile() || Entry.getFile().getIncludeLoc().isInvalid())
          continue;
        // A file location is encoded as its offset.
        const SourceLocation Start =
            SourceLocation::getFromRawEncoding(Entry.getOffset());
        if (SM.getFileID(Start) == SM.getMainFileID() || !IsReported(Start))
          continue;
        for (SourceLocation Include = Entry.getFile().getIncludeLoc();
             Include.isValid() && Include.isFileID();
             Include = SM.getIncludeLoc(SM.getFileID(Include))) {
          const auto [File, Offset] = SM.getDecomposedLoc(Include);
          ReportedIncludes[File].push_back(Offset);
        }
      }

      std::vector<Decl *> Scope;
      // A parameter of a template whose type, or whose default argument, of a
      // type parameter, depends on the others, `typename tf_CTraits::CBool
      // tf_Flag` or `typename t_FCallback = TCFunction<void (typename
      // t_CTraits::CBool _bReady)>`, which only the instantiations tell.
      auto DependsOnOthers = [](const NamedDecl *Parameter) {
        if (const auto *Value = dyn_cast<NonTypeTemplateParmDecl>(Parameter))
          return Value->getType()->isDependentType();
        if (const auto *Type = dyn_cast<TemplateTypeParmDecl>(Parameter))
          return Type->hasDefaultArgument() && Type->getDefaultArgument()
                                                   .getArgument()
                                                   .getAsType()
                                                   ->isDependentType();
        return false;
      };
      auto Collect = [&](auto &&Self, const DeclContext *Context) -> void {
        for (Decl *D : Context->noload_decls()) {
          if (D->isFromASTFile())
            continue;
          const SourceLocation Loc = SM.getExpansionLoc(D->getLocation());
          if (Loc.isInvalid())
            continue;
          // Also a template, or a function, whose parameters are in a header
          // that is reported, while what they declare is in one that isn't.
          auto ReportedParameter = [&](const NamedDecl *Parameter) {
            const SourceLocation ParameterLoc =
                SM.getExpansionLoc(Parameter->getLocation());
            return ParameterLoc.isValid() && IsReported(ParameterLoc);
          };
          const auto *Parameterized = dyn_cast<TemplateDecl>(D);
          // The parameters of a template, also of a partial specialization,
          // `template <...> struct TCWrapper<t_CType *>`, which is a class.
          const TemplateParameterList *Parameters =
              Parameterized ? Parameterized->getTemplateParameters() : nullptr;
          if (const auto *Partial =
                  dyn_cast<ClassTemplatePartialSpecializationDecl>(D))
            Parameters = Partial->getTemplateParameters();
          else if (const auto *Partial =
                       dyn_cast<VarTemplatePartialSpecializationDecl>(D))
            Parameters = Partial->getTemplateParameters();
          const FunctionDecl *Function = D->getAsFunction();
          // Or a function whose body includes another file, which can be
          // reported, like `#include "Body.hpp"` in its body, or another
          // declaration that isn't looked into, like a variable whose
          // initializer includes one.
          // Whether \p Range includes a file that is reported.
          auto RangeIncludes = [&](SourceRange Range) {
            if (Range.isInvalid())
              return false;
            const std::pair<FileID, unsigned> Begin =
                SM.getDecomposedLoc(SM.getExpansionLoc(Range.getBegin()));
            const std::pair<FileID, unsigned> End =
                SM.getDecomposedLoc(SM.getExpansionLoc(Range.getEnd()));
            if (End.first != Begin.first)
              return true;
            const auto Includes = ReportedIncludes.find(Begin.first);
            return Includes != ReportedIncludes.end() &&
                   llvm::any_of(Includes->second, [&](unsigned Offset) {
                     return Offset >= Begin.second && Offset <= End.second;
                   });
          };
          auto IncludesIn = [&] {
            if (ReportedIncludes.empty())
              return false;
            // The parameters of a template, also of a class, whose default
            // arguments can include one, `template <typename t_FCallback =
            // void (#include "Parameters.hpp")>`.
            if (Parameters && RangeIncludes(Parameters->getSourceRange()))
              return true;
            // The arguments of its attributes, which its range leaves out
            // when they lead it, `[[clang::annotate("...", #include
            // "Argument.hpp")]]`, also of what a template declares.
            const Decl *Templated =
                Parameterized ? Parameterized->getTemplatedDecl() : nullptr;
            for (const Decl *Attributed :
                 {static_cast<const Decl *>(D), Templated})
              if (Attributed && Attributed->hasAttrs())
                for (const Attr *Attribute : Attributed->attrs())
                  if (!Attribute->isImplicit() &&
                      RangeIncludes(Attribute->getRange()))
                    return true;
            SourceRange Range;
            // What is before the body of a class, its bases, attributes, like
            // `alignas(...)`, and the requires clause of its template, whose
            // members are looked into instead.
            const auto *Record = dyn_cast<TagDecl>(D);
            if (!Record && Parameterized)
              Record = dyn_cast_or_null<TagDecl>(
                  Parameterized->getTemplatedDecl());
            if (Record && Record->getBraceRange().isValid() &&
                RangeIncludes(SourceRange(
                    D->getBeginLoc(), Record->getBraceRange().getBegin())))
              return true;
            // All of a function, also the initializers of a constructor and
            // default arguments, also of a declaration without a body, and
            // the leading requires clause of its template, `template
            // <typename tf_CType> requires (...)`.
            if (Function)
              Range = SourceRange(D->getBeginLoc(),
                                  Function->getSourceRange().getEnd());
            else if (!isa<NamespaceDecl, LinkageSpecDecl, ExportDecl,
                            RecordDecl, EnumDecl>(D) &&
                       !(Parameterized &&
                         isa_and_nonnull<DeclContext>(
                             Parameterized->getTemplatedDecl())))
              Range = D->getSourceRange();
            return RangeIncludes(Range);
          };
          if (IsReported(Loc) ||
              (Parameters && llvm::any_of(*Parameters, ReportedParameter)) ||
              (Function &&
               llvm::any_of(Function->parameters(), ReportedParameter)) ||
              IncludesIn()) {
            Scope.push_back(D);
            // The instantiations of a template are traversed with its first
            // declaration, which can be in a header that isn't reported, like
            // a declaration ahead of a definition here: they are what tells
            // the checks what the definition is used with.
            if (const auto *Function = dyn_cast<FunctionTemplateDecl>(D);
                Function && !Function->isCanonicalDecl()) {
              for (const FunctionDecl *Instance : Function->specializations())
                Scope.push_back(const_cast<FunctionDecl *>(Instance));
              // Also of a member template of a class template defined outside
              // of it, `template <typename tf_CType> void
              // TCBox<t_CType>::f_Run(...)`, whose instantiations are of the
              // member templates of the instantiations of the class.
              if (const auto *Pattern = dyn_cast<CXXRecordDecl>(
                      Function->getTemplatedDecl()->getDeclContext());
                  Pattern && Pattern->isDependentContext())
                for (const CXXRecordDecl *Instance : instancesOf(Pattern))
                  for (const Decl *Member : Instance->decls())
                    if (const auto *Instantiated =
                            dyn_cast<FunctionTemplateDecl>(Member);
                        Instantiated &&
                        Instantiated->getInstantiatedFromMemberTemplate() &&
                        Instantiated->getInstantiatedFromMemberTemplate()
                                ->getCanonicalDecl() ==
                            Function->getCanonicalDecl())
                      for (const FunctionDecl *Specialization :
                           Instantiated->specializations())
                        Scope.push_back(
                            const_cast<FunctionDecl *>(Specialization));
            } else if (const auto *Class = dyn_cast<ClassTemplateDecl>(D);
                       Class && !Class->isCanonicalDecl()) {
              for (const ClassTemplateSpecializationDecl *Instance :
                   Class->specializations())
                if (!Instance->isExplicitSpecialization())
                  Scope.push_back(
                      const_cast<ClassTemplateSpecializationDecl *>(Instance));
            } else if (const auto *Partial =
                           dyn_cast<ClassTemplatePartialSpecializationDecl>(D);
                       Partial && !IsReported(SM.getExpansionLoc(
                                      Partial->getSpecializedTemplate()
                                          ->getCanonicalDecl()
                                          ->getLocation()))) {
              // And of a partial specialization, which are traversed with
              // the template it specializes, when that is declared in a
              // header that isn't reported.
              for (const ClassTemplateSpecializationDecl *Instance :
                   Partial->getSpecializedTemplate()->specializations())
                if (!Instance->isExplicitSpecialization() &&
                    Instance->getSpecializedTemplateOrPartial()
                            .dyn_cast<
                                ClassTemplatePartialSpecializationDecl *>() ==
                        Partial)
                  Scope.push_back(
                      const_cast<ClassTemplateSpecializationDecl *>(Instance));
            } else if (const auto *Method = dyn_cast<CXXMethodDecl>(D);
                       Method && Method->isOutOfLine()) {
              // And of a member of a class template defined outside of it,
              // `void TCBox<t_CType>::f_Run(...) {}`, which are in the
              // instantiations of the class.
              // Also of a partial specialization, whose instantiations are
              // of the template it specializes, and of a class in a class
              // template, `void TCOuter<t_CType>::CInner::f_Run(...) {}`,
              // whose instantiations are members of the outer ones.
              llvm::SmallVector<const CXXRecordDecl *, 4> Instances;
              if (const auto *Partial =
                      dyn_cast<ClassTemplatePartialSpecializationDecl>(
                          Method->getDeclContext())) {
                for (const ClassTemplateSpecializationDecl *Instance :
                     Partial->getSpecializedTemplate()->specializations())
                  Instances.push_back(Instance);
              } else if (const auto *Pattern =
                             dyn_cast<CXXRecordDecl>(Method->getDeclContext()))
                Instances = instancesOf(Pattern);
              for (const CXXRecordDecl *Instance : Instances)
                for (CXXMethodDecl *Member : Instance->methods())
                  if (const CXXMethodDecl *From =
                          dyn_cast_or_null<CXXMethodDecl>(
                              Member->getInstantiatedFromMemberFunction());
                      From &&
                      From->getCanonicalDecl() == Method->getCanonicalDecl())
                    Scope.push_back(Member);
            }
            continue;
          }
          // A namespace, linkage block, class, enum or function of a header
          // that is not reported can include headers that are.
          if (isa<NamespaceDecl, LinkageSpecDecl, ExportDecl, RecordDecl,
                  EnumDecl, FunctionDecl>(D))
            Self(Self, cast<DeclContext>(D));
          else if (const auto *Template = dyn_cast<TemplateDecl>(D);
                   Template && isa_and_nonnull<DeclContext>(
                                   Template->getTemplatedDecl()))
            Self(Self, cast<DeclContext>(Template->getTemplatedDecl()));
        }
      };
      Collect(Collect, Ctx.getTranslationUnitDecl());
      // The definitions instantiated here of the templates of a precompiled
      // header, whose declarations were checked when it was built, but not
      // with what this compile instantiates them, like `fg_Run<CMyType>()`,
      // also of a member of a class instantiated there whose definition is
      // instantiated here. The checks look at a template through its
      // instantiations in what they traverse, so its definition is traversed
      // with them, but not what the precompiled header instantiated, which
      // was checked with it.
      llvm::DenseSet<const Decl *> Added;
      for (const Decl *Definition : Instantiations.Definitions) {
        const Decl *Pattern = nullptr;
        // Its definition, which of a member defined outside of its class,
        // `void TCBox<t_CType>::f_Run() {...}`, isn't the declaration in it.
        if (const auto *Function = dyn_cast<FunctionDecl>(Definition)) {
          if (const FunctionDecl *From =
                  Function->getTemplateInstantiationPattern()) {
            const FunctionDecl *FromDefinition = From->getDefinition();
            Pattern = FromDefinition ? FromDefinition : From;
          }
        } else if (const auto *Variable = dyn_cast<VarDecl>(Definition)) {
          if (const VarDecl *From =
                  Variable->getTemplateInstantiationPattern()) {
            const VarDecl *FromDefinition = From->getDefinition();
            Pattern = FromDefinition ? FromDefinition : From;
          }
        } else if (const auto *Class =
                       dyn_cast<ClassTemplateSpecializationDecl>(Definition)) {
          // Not one only declared, like `TCBox<int> *`, which has no members.
          if (!Class->isCompleteDefinition() ||
              Class->getTemplateSpecializationKind() ==
                  TSK_ExplicitSpecialization)
            continue;
          Pattern = Class->getTemplateInstantiationPattern();
        }
        // Not what another file instantiates, `extern template`, which is
        // checked there, but the class an explicit instantiation declaration
        // still instantiates here, `extern template struct TCBox<CTraits>;`,
        // whose members tell what it is.
        if (!Pattern || !Pattern->isFromASTFile() ||
            (isExternInstantiation(Definition) &&
             !(isa<ClassTemplateSpecializationDecl>(Definition) &&
               cast<ClassTemplateSpecializationDecl>(Definition)
                       ->getSpecializationKind() ==
                   TSK_ExplicitInstantiationDeclaration)))
          continue;
        const SourceLocation Loc = SM.getExpansionLoc(Pattern->getLocation());
        if (Loc.isInvalid() || !IsReported(Loc))
          continue;
        // Of a class, only the members whose types depend on what it is
        // instantiated with, `typename t_CTraits::CPointer m_Value`, which
        // weren't told where the template was checked; the rest was.
        // Also those of an anonymous member, `union { typename
        // t_CTraits::CPointer m_Value; };`, and the non-type parameters of
        // the template of such a type, `typename t_CTraits::CBool t_Flag`.
        // As declared in the template, which the checks look at through
        // its instantiations, also this one, like they do without the
        // precompiled header, so that one of what it is given, `t_CType
        // m_Value`, stays generic.
        if (isa<ClassTemplateSpecializationDecl>(Definition)) {
          auto AddFields = [&](auto &Self, const RecordDecl *From) -> void {
            for (const FieldDecl *Field : From->fields()) {
              if (Field->isAnonymousStructOrUnion()) {
                if (const RecordDecl *Inner =
                        Field->getType()->getAsRecordDecl())
                  Self(Self, Inner);
                continue;
              }
              // Also one whose default initializer depends on them, `int
              // m_Value = [](typename t_CTraits::CPointer _pValue) { ... }(
              // nullptr)`, which this one instantiates.
              if ((Field->getType()->isDependentType() ||
                   (Field->getInClassInitializer() &&
                    Field->getInClassInitializer()
                        ->isInstantiationDependent())) &&
                  Added.insert(Field).second)
                Scope.push_back(const_cast<FieldDecl *>(Field));
            }
            // And its aliases and static members of such types, `using
            // FValue = typename t_CTraits::CValue;`, also deduced from what
            // depends on them, `static inline auto ms_Value = ...`, and its
            // member functions with parameters of such types, `void
            // f_Run(t_CType const &_Value)`, or a return type with them,
            // `TCFunction<void (typename t_CTraits::CBool _bReady)> f_Make()`,
            // whose instantiations this one declares.
            for (Decl *Member : From->decls()) {
              QualType Type;
              if (const auto *Alias = dyn_cast<TypedefNameDecl>(Member))
                Type = Alias->getUnderlyingType();
              else if (const auto *Static = dyn_cast<VarDecl>(Member);
                       Static && Static->isStaticDataMember())
                Type = Static->getType();
              else if (const auto *Method = dyn_cast<CXXMethodDecl>(Member);
                       Method && !Method->isImplicit() &&
                       (Method->getReturnType()->isDependentType() ||
                        llvm::any_of(
                            Method->parameters(),
                            [](const ParmVarDecl *Parameter) {
                              return Parameter->getType()->isDependentType();
                            })) &&
                       Added.insert(Member).second) {
                Scope.push_back(Member);
                continue;
              }
              if (!Type.isNull() &&
                  (Type->isDependentType() || Type->isUndeducedAutoType()) &&
                  Added.insert(Member).second)
                Scope.push_back(Member);
            }
            // And of a named class in it, `struct CInner`, whose
            // instantiation with this one is used, each once.
            for (const Decl *Member : From->decls())
              if (const auto *Nested = dyn_cast<CXXRecordDecl>(Member);
                  Nested && Nested->getIdentifier() && !Nested->isLambda() &&
                  Nested->isThisDeclarationADefinition() &&
                  !Nested->getDescribedClassTemplate() &&
                  Added.insert(Nested).second)
                Self(Self, Nested);
          };
          const auto *From = cast<CXXRecordDecl>(Pattern);
          AddFields(AddFields, From);
          // And the declarations of its member functions as this one
          // instantiates them, `void f_Run(int const &o_Value)`, of those
          // with parameters of what it is instantiated with.
          for (CXXMethodDecl *Method :
               cast<ClassTemplateSpecializationDecl>(Definition)->methods())
            if (const auto *From = dyn_cast_or_null<CXXMethodDecl>(
                    Method->getInstantiatedFromMemberFunction());
                From && !Method->isImplicit() &&
                llvm::any_of(From->parameters(),
                             [](const ParmVarDecl *Parameter) {
                               return Parameter->getType()->isDependentType();
                             }) &&
                Added.insert(Method).second)
              Scope.push_back(Method);
          const TemplateParameterList *Parameters =
              isa<ClassTemplatePartialSpecializationDecl>(From)
                  ? cast<ClassTemplatePartialSpecializationDecl>(From)
                        ->getTemplateParameters()
              : From->getDescribedClassTemplate()
                  ? From->getDescribedClassTemplate()->getTemplateParameters()
                  : nullptr;
          if (Parameters)
            for (NamedDecl *Parameter : *Parameters)
              if (DependsOnOthers(Parameter) && Added.insert(Parameter).second)
                Scope.push_back(Parameter);
          continue;
        }
        if (Added.insert(Pattern).second)
          Scope.push_back(const_cast<Decl *>(Pattern));
        if (Added.insert(Definition).second)
          Scope.push_back(const_cast<Decl *>(Definition));
        // And the non-type parameters of the template of a type that depends
        // on the others, `typename tf_CTraits::CBool tf_Flag`, of a function
        // or variable template, also of a partial specialization of one.
        const TemplateParameterList *Parameters = nullptr;
        if (const auto *Function = dyn_cast<FunctionDecl>(Pattern)) {
          if (const FunctionTemplateDecl *Template =
                  Function->getDescribedFunctionTemplate())
            Parameters = Template->getTemplateParameters();
        } else if (const auto *Partial =
                       dyn_cast<VarTemplatePartialSpecializationDecl>(Pattern))
          Parameters = Partial->getTemplateParameters();
        else if (const auto *Variable = dyn_cast<VarDecl>(Pattern)) {
          if (const VarTemplateDecl *Template =
                  Variable->getDescribedVarTemplate())
            Parameters = Template->getTemplateParameters();
        }
        if (Parameters)
          for (NamedDecl *Parameter : *Parameters)
            if (DependsOnOthers(Parameter) && Added.insert(Parameter).second)
              Scope.push_back(Parameter);
      }
      return Scope;
    }

  };

public:
  ActionType getActionType() override { return AddAfterMainAction; }

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &Compiler,
                                                 StringRef File) override {
    // The files the arguments name, and, of a source that is checked, the
    // configuration files its checks use, are dependencies.
    std::vector<std::string> Dependencies = ArgumentFiles;
    auto AddDependencies = [&] {
      Compiler.getPreprocessor().addPPCallbacks(
          std::make_unique<DependenciesCallback>(Compiler,
                                                 std::move(Dependencies)));
    };
    // A source the rules of its repository leave out is not checked, nor are
    // the headers through it.
    // Also by where it is, past symbolic links, `Include/Source.cpp` linking
    // to `Repository/Source.cpp`.
    if (Rules) {
      llvm::SmallString<256> Absolute(File);
      Compiler.getFileManager().makeAbsolutePath(Absolute);
      llvm::SmallString<256> Real;
      if (llvm::sys::fs::real_path(Absolute, Real))
        Real.clear();
      if (!Rules->isChecked(
              normalizeHeaderName(File, Compiler.getFileManager()), Real)) {
        AddDependencies();
        return std::make_unique<ASTConsumer>();
      }
    }

    {
      llvm::SmallString<256> Absolute(File);
      Compiler.getFileManager().makeAbsolutePath(Absolute);
      llvm::append_range(Dependencies, configurationFiles(Absolute));
      AddDependencies();
    }

    // The diagnostics of the checks are reported through the diagnostics of
    // the compile, so warning flags like -Werror apply to them.
    auto *DiagConsumer =
        new ClangTidyDiagnosticConsumer(*Context, &Compiler.getDiagnostics());
    auto DiagOpts = std::make_unique<DiagnosticOptions>();
    auto DiagEngine = std::make_unique<DiagnosticsEngine>(
        DiagnosticIDs::create(), *DiagOpts, DiagConsumer);
    Context->setDiagnosticsEngine(std::move(DiagOpts), DiagEngine.get());
    // Clang only tells its own diagnostic client about the source file.
    DiagConsumer->BeginSourceFile(Compiler.getLangOpts(),
                                  &Compiler.getPreprocessor());
    auto End = std::make_shared<SourceFileEnd>(*DiagConsumer);
    Compiler.getPreprocessor().addPPCallbacks(
        std::make_unique<EndOfMainFileCallback>(End));

    // The factory sets up the static analyzer for the clang-analyzer checks,
    // which the plugin never runs, so keep what the compile itself analyzes
    // with --analyze.
    AnalyzerOptions &Analyzer = Compiler.getAnalyzerOpts();
    const auto CheckersAndPackages = Analyzer.CheckersAndPackages;
    ClangTidyASTConsumerFactory Factory(*Context);
    std::vector<std::unique_ptr<ASTConsumer>> Consumers;
    // The checks are made, which reads their configuration, also with -w,
    // where they don't run but are kept for the callbacks they gave the
    // preprocessor, whose warnings -w drops.
    std::unique_ptr<ASTConsumer> Checks =
        Factory.createASTConsumer(Compiler, File);
    std::unique_ptr<ASTConsumer> Idle;
    if (IgnoreWarnings) {
      Idle = std::move(Checks);
      // What they report from those callbacks is dropped: their diagnostics,
      // which have levels of their own, aren't warnings that -w ignores. The
      // errors of their configuration are reported as they are made.
      DiagEngine->setSuppressAllDiagnostics(true);
    } else
      Consumers.push_back(std::move(Checks));
    Analyzer.CheckersAndPackages = CheckersAndPackages;
    return std::make_unique<WrapConsumer>(
        std::move(Context), std::move(DiagEngine), std::move(End),
        std::move(Consumers), std::move(Idle));
  }

  bool ParseArgs(const CompilerInstance &Compiler,
                 const std::vector<std::string> &Args) override {
    // Without arguments the plugin is not used.
    if (Args.empty())
      return false;

    DiagnosticsEngine &Diags = Compiler.getDiagnostics();
    ClangTidyOptions OverrideOptions;
    std::optional<std::string> ConfigFile;
    std::optional<std::string> RepositoriesFile;
    bool HeaderFilters = false;
    bool Profile = false;
    for (const StringRef Arg : Args) {
      if (Arg == "enable")
        continue;
      if (Arg == "profile") {
        Profile = true;
        continue;
      }
      auto [Name, Value] = Arg.split('=');
      if (Name == "checks")
        OverrideOptions.Checks = Value.str();
      else if (Name == "config-file")
        ConfigFile = Value.str();
      // The Malterlib options file, like `malterlib.OptionsFile` of a
      // configuration, relative to the current directory.
      else if (Name == "options-file") {
        SmallString<256> Path(Value);
        llvm::sys::fs::make_absolute(Path);
        // Not `..`, which after a symbolic link is in another directory.
        llvm::sys::path::remove_dots(Path);
        OverrideOptions.CheckOptions[OptionsFileOption] =
            ClangTidyOptions::ClangTidyValue(Path.str());
      } else if (Name == "repositories")
        RepositoriesFile = Value.str();
      else if (Name == "header-filter" || Name == "exclude-header-filter") {
        // A filter that isn't a regular expression would check no header. An
        // empty one checks no header, or excludes none.
        std::string Error;
        if (!Value.empty() && !llvm::Regex(Value).isValid(Error)) {
          Diags.Report(Diags.getCustomDiagID(
              DiagnosticsEngine::Error,
              "invalid regular expression '%0' for the malterlib plugin's "
              "%1: %2"))
              << Value << Name << Error;
          return false;
        }
        HeaderFilters = true;
        if (Name == "header-filter")
          OverrideOptions.HeaderFilterRegex = Value.str();
        else
          OverrideOptions.ExcludeHeaderFilterRegex = Value.str();
      }
      else if (Name == "warnings-as-errors")
        OverrideOptions.WarningsAsErrors = Value.str();
      else {
        Diags.Report(Diags.getCustomDiagID(
            DiagnosticsEngine::Error,
            "unknown argument '%0' for the malterlib plugin"))
            << Arg;
        return false;
      }
    }

    // The diagnostics of the checks have no warning group, so clang does not
    // apply -w and -Werror to them. Apply them here, so the checks fail a
    // build like other warnings: with -w the checks don't run, but their
    // configuration is still read, whose errors fail it.
    IgnoreWarnings = Diags.getIgnoreAllWarnings();
    if (Diags.getWarningsAsErrors() && !OverrideOptions.WarningsAsErrors)
      OverrideOptions.WarningsAsErrors = "*";

    // Only the checks of the modules linked into clang can run, but the
    // static analyzer checks are part of clang-tidy itself. A configuration
    // shared with clang-tidy can enable them, and they are too slow to run in
    // every compile.
    OverrideOptions.Checks =
        OverrideOptions.Checks.value_or("") + ",-clang-analyzer-*";

    // The configuration files are read like the sources, through the file
    // system of the compilation.
    const llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS =
        Compiler.hasVirtualFileSystem() ? Compiler.getVirtualFileSystemPtr()
                                        : llvm::vfs::getRealFileSystem();

    // The header filters name directories absolutely, and headers are often
    // included through relative and `..` paths.
    ClangTidyGlobalOptions GlobalOptions;
    GlobalOptions.NormalizeHeaderNames = true;
    // The checks see only part of the translation unit, so their fixes can be
    // incomplete; they are applied with clang-tidy, which sees all of it.
    GlobalOptions.ForwardFixes = false;
    if (RepositoriesFile) {
      if (HeaderFilters) {
        Diags.Report(Diags.getCustomDiagID(
            DiagnosticsEngine::Error,
            "the malterlib plugin's repositories decide which headers are "
            "checked, so they can't be given with header filters"));
        return false;
      }
      llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Text =
          FS->getBufferForFile(*RepositoriesFile);
      if (!Text) {
        Diags.Report(Diags.getCustomDiagID(
            DiagnosticsEngine::Error,
            "cannot read the malterlib plugin repositories '%0': %1"))
            << *RepositoriesFile << Text.getError().message();
        return false;
      }
      llvm::Expected<RepositoryRules> Parsed =
          RepositoryRules::parse((*Text)->getBuffer());
      if (!Parsed) {
        Diags.Report(Diags.getCustomDiagID(
            DiagnosticsEngine::Error,
            "invalid malterlib plugin repositories '%0': %1"))
            << *RepositoriesFile << llvm::toString(Parsed.takeError());
        return false;
      }
      Rules = std::make_shared<const RepositoryRules>(std::move(*Parsed));
      GlobalOptions.IsHeaderReported = [Rules = Rules](StringRef Name,
                                                       StringRef Real) {
        return Rules->isChecked(Name, Real);
      };
    }
    const ClangTidyOptions DefaultOptions = ClangTidyOptions::getDefaults();
    std::unique_ptr<ClangTidyOptionsProvider> OptionsProvider;
    ReportingFileOptionsProvider *ReportingProvider = nullptr;
    if (ConfigFile) {
      llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Text =
          FS->getBufferForFile(*ConfigFile);
      if (!Text) {
        Diags.Report(Diags.getCustomDiagID(
            DiagnosticsEngine::Error,
            "cannot read the malterlib plugin configuration '%0': %1"))
            << *ConfigFile << Text.getError().message();
        return false;
      }
      llvm::ErrorOr<ClangTidyOptions> Config =
          parseConfiguration((*Text)->getMemBufferRef());
      if (!Config) {
        Diags.Report(Diags.getCustomDiagID(
            DiagnosticsEngine::Error,
            "invalid malterlib plugin configuration '%0': %1"))
            << *ConfigFile << Config.getError().message();
        return false;
      }
      OptionsProvider = std::make_unique<ConfigOptionsProvider>(
          GlobalOptions, DefaultOptions, std::move(*Config),
          std::move(OverrideOptions), FS);
    } else {
      // A configuration file that can't be parsed is an error of the
      // compilation, not only a message, so that the checks it enables can't
      // silently be off.
      auto Reporting = std::make_unique<ReportingFileOptionsProvider>(
          GlobalOptions, DefaultOptions, std::move(OverrideOptions), Diags, FS);
      ReportingProvider = Reporting.get();
      OptionsProvider = std::move(Reporting);
    }
    Provider = OptionsProvider.get();
    ArgumentFiles.clear();
    if (ConfigFile)
      ArgumentFiles.push_back(*ConfigFile);
    if (RepositoriesFile)
      ArgumentFiles.push_back(*RepositoriesFile);
    Context = std::make_unique<ClangTidyContext>(std::move(OptionsProvider));
    if (ReportingProvider)
      ReportingProvider->startReporting();
    Context->setEnableProfiling(Profile);
    return true;
  }

private:
  std::unique_ptr<ClangTidyContext> Context;
  /// Whether warnings are off, -w, where the checks are made, which reads
  /// their configuration, but don't run.
  bool IgnoreWarnings = false;
  /// The rules of the repositories, when they decide which files are checked.
  std::shared_ptr<const RepositoryRules> Rules;
  /// What reads the configuration files, which the context owns.
  ClangTidyOptionsProvider *Provider = nullptr;
  /// The configuration and repositories files the arguments name.
  std::vector<std::string> ArgumentFiles;

  /// The configuration files the checks of \p File use: the `.clang-tidy`
  /// files, also those it inherits, and the Malterlib options file.
  std::vector<std::string> configurationFiles(StringRef File) const {
    std::vector<std::string> Files;
    for (const ClangTidyOptionsProvider::OptionsSource &Source :
         Provider->getRawOptions(File))
      Files.push_back(Source.second);
    const ClangTidyOptions Options = Context->getOptionsForFile(File);
    if (auto It = Options.CheckOptions.find(OptionsFileOption);
        It != Options.CheckOptions.end())
      Files.push_back(It->second.Value);
    return Files;
  }
};

} // namespace
} // namespace clang::tidy::malterlib

namespace clang::tidy {
// This anchor is used to force the linker to link the MalterlibModule, whose
// checks the plugin runs.
extern volatile int MalterlibModuleAnchorSource;
[[maybe_unused]] static int MalterlibModuleAnchorDestination =
    MalterlibModuleAnchorSource;
} // namespace clang::tidy

// This anchor is used to force the linker to link in the generated object file
// and thus register the plugin.
// NOLINTNEXTLINE(misc-use-internal-linkage)
volatile int MalterlibPluginAnchorSource = 0;

static clang::FrontendPluginRegistry::Add<
    clang::tidy::malterlib::MalterlibPluginAction>
    X("malterlib", "Runs the Malterlib clang-tidy checks");
