//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_MALTERLIBCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_MALTERLIBCHECK_H

#include "../ClangTidyCheck.h"
#include "MalterlibOptions.h"
#include <deque>
#include <optional>

namespace clang::tidy::malterlib {

/// A report of a Malterlib check. Once it is emitted, a warning in a template
/// instantiation is followed by notes of where it was instantiated, like
/// clang's own diagnostics: the checks decide from what templates are
/// instantiated with, so a warning in a template only tells where it is with
/// the instantiation that has it.
class MalterlibDiagnostic {
public:
  MalterlibDiagnostic(DiagnosticBuilder &&Builder, ClangTidyCheck &Check,
                      const Decl *Instantiation)
      : Builder(std::move(Builder)), Check(Check),
        Instantiation(Instantiation) {}
  /// Takes over the report of \p Other, which then reports nothing, like
  /// `DiagnosticBuilder` does, so `auto Diag = diag(...) << Name;` reports
  /// once.
  MalterlibDiagnostic(const MalterlibDiagnostic &Other)
      : Builder(std::move(Other.Builder)), Check(Other.Check),
        Instantiation(Other.Instantiation) {
    Other.Builder.reset();
    Other.Instantiation = nullptr;
  }
  MalterlibDiagnostic &operator=(const MalterlibDiagnostic &) = delete;
  ~MalterlibDiagnostic();

  template <typename T>
  const MalterlibDiagnostic &operator<<(const T &Value) const {
    *Builder << Value;
    return *this;
  }

private:
  mutable std::optional<DiagnosticBuilder> Builder;
  ClangTidyCheck &Check;
  /// The template instantiation the warning is in, or null.
  mutable const Decl *Instantiation;
};

/// The base of the Malterlib checks, whose warnings tell where the template
/// instantiation they are in was instantiated.
class MalterlibCheck : public ClangTidyCheck {
public:
  /// Reads the Malterlib options file the configuration names in
  /// `malterlib.OptionsFile`, which it must, and makes the vocabulary of the
  /// translation unit of it.
  MalterlibCheck(StringRef Name, ClangTidyContext *Context);
  ~MalterlibCheck() override;

  /// The options of the check, storeCheckOptions, with each list as it is
  /// (`-*;Name...`), which takes the place of the one built in and of the
  /// options file, and the options of all checks: the options file and the
  /// lists of the vocabulary. So a configuration `--dump-config` gives checks
  /// the same.
  void storeOptions(ClangTidyOptions::OptionMap &Opts) final;

  /// Like `ClangTidyCheck::diag`. A warning in the instantiation the check
  /// analyzes, the one of an `InstantiationScope` or else the one a node of
  /// the match is in, is followed by notes of where it was instantiated.
  MalterlibDiagnostic diag(SourceLocation Loc, StringRef Description,
                           DiagnosticIDs::Level Level = DiagnosticIDs::Warning);
  using ClangTidyCheck::diag;

protected:
  /// The options of the check, for storeOptions.
  virtual void storeCheckOptions(ClangTidyOptions::OptionMap &Opts) {}

  /// The list option \p Name: \p BuiltIn, the names of the language and the
  /// standard library, edited (editList) by the options file and then by the
  /// configuration, each by the option of all checks, `Name`, and then by the
  /// one of this check, `malterlib-<check>.Name`. The option of a check of the
  /// name of a list of the vocabulary is that list with the edits of the
  /// check, in that order, from the built-in names of the list,
  /// CVocabulary::builtIn. The check keeps the list,
  /// which the views of `parseStringList` refer to.
  StringRef listOption(StringRef Name, StringRef BuiltIn);
  /// \p List, which the check keeps, like listOption.
  StringRef keepList(std::string List) {
    return Lists.emplace_back(std::move(List));
  }
  /// The value option \p Name: of the configuration, of this check or of all
  /// checks, or else of the options file, or else \p BuiltIn.
  std::string valueOption(StringRef Name, StringRef BuiltIn) const;
  /// A value option that is a number, \p BuiltIn when it isn't, which is
  /// reported.
  unsigned unsignedOption(StringRef Name, unsigned BuiltIn) const;
  /// A value option that is a boolean, `true`, `False`, `1`..., \p BuiltIn
  /// when it isn't, which is reported.
  bool boolOption(StringRef Name, bool BuiltIn) const;

  /// Tells the reports that the check analyzes \p Instantiation while the
  /// scope is alive, for checks that find a template and analyze each of its
  /// instantiations.
  class InstantiationScope {
  public:
    InstantiationScope(MalterlibCheck &Check, const Decl *Instantiation)
        : Check(Check), Previous(Check.ScopeInstantiation) {
      Check.ScopeInstantiation = Instantiation;
    }
    InstantiationScope(const InstantiationScope &) = delete;
    InstantiationScope &operator=(const InstantiationScope &) = delete;
    ~InstantiationScope() { Check.ScopeInstantiation = Previous; }

  private:
    MalterlibCheck &Check;
    const Decl *Previous;
  };

private:
  void run(const ast_matchers::MatchFinder::MatchResult &Result) override;

  /// The instantiation a warning at \p Loc is in, if the check analyzes one.
  const Decl *instantiationAt(SourceLocation Loc) const;

  /// The value of the option \p Key of the configuration, or null.
  const ClangTidyOptions::ClangTidyValue *configuredValue(StringRef Key) const;
  /// The keys and values of the configuration of the option \p Name of all
  /// checks and of this check, also those that a configuration that inherits
  /// another's replaced, lowest priority first: that of the
  /// configuration that gives them, like that of a .clang-tidy that inherits
  /// another's over the other's, and that of this check over that of all
  /// checks where it is the same.
  SmallVector<std::pair<std::string, std::string>, 2>
  configuredInOrder(StringRef Name) const;
  /// The value of the option \p Key of the options file, or none.
  std::optional<StringRef> ofFile(StringRef Key) const;
  /// The key and the value of the value option \p Name, as valueOption finds
  /// it, or none.
  std::optional<std::pair<std::string, std::string>>
  findValueOption(StringRef Name) const;

  const ast_matchers::MatchFinder::MatchResult *CurrentResult = nullptr;
  const Decl *ScopeInstantiation = nullptr;
  std::string CheckName;
  ClangTidyContext *Context;
  std::shared_ptr<const COptionsFile> File;
  /// The lists listOption gave, which don't move.
  std::deque<std::string> Lists;
  /// The names of the list options listOption read.
  llvm::StringSet<> ListNames;
  /// The vocabulary of the configuration, which this thread uses.
  std::shared_ptr<const CVocabulary> Vocabulary;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_MALTERLIBCHECK_H
