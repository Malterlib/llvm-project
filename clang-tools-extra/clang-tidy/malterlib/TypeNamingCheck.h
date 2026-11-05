//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_TYPENAMINGCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_TYPENAMINGCHECK_H

#include "MalterlibCheck.h"
#include "llvm/ADT/MapVector.h"
#include "NamingUtils.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace clang::tidy::malterlib {

/// Checks the prefixes of type names: `C`, `IC`, `TC` and `TIC` for classes,
/// structs and type aliases, `F` for function types, `E` for enums and
/// enumerators, `N` for namespaces and `c` for concepts.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/type-naming.html
class TypeNamingCheck : public MalterlibCheck {
public:
  TypeNamingCheck(StringRef Name, ClangTidyContext *Context);
  void storeCheckOptions(ClangTidyOptions::OptionMap &Opts) override;
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override {
    Repeated.invalidate();
    ReportedAliases.clear();
    CheckedAliasUses.clear();
  }
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_IgnoreUnlessSpelledInSource;
  }

private:
  void checkName(const NamedDecl *Type, const SourceManager &SM,
                 StringRef Kind, llvm::ArrayRef<StringRef> Expected);

  const std::vector<StringRef> LowerCaseTypes;
  /// The shortest a name can be after its prefix.
  const unsigned MinimumNameLength;
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
  /// The alias templates reported by what their uses make of them.
  llvm::SmallPtrSet<const Decl *, 4> ReportedAliases;
  /// The pairs of a written type and what it is given that the uses of alias
  /// templates have checked, with the least depth they were met at.
  llvm::DenseMap<std::pair<void *, void *>, int> CheckedAliasUses;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_TYPENAMINGCHECK_H
