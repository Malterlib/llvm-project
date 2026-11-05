//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_MEMBERORDERCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_MEMBERORDERCHECK_H

#include "MalterlibCheck.h"
#include "NamingUtils.h"

namespace clang::tidy::malterlib {

/// Checks the order of a class's sections, public, protected and private, and
/// of the members in each section: types, constructors and destructor, other
/// functions, constants and variables.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/member-order.html
class MemberOrderCheck : public MalterlibCheck {
public:
  MemberOrderCheck(StringRef Name, ClangTidyContext *Context)
      : MalterlibCheck(Name, Context) {}
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus;
  }
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override {
    Repeated.invalidate();
    NoLintBlocks.clear();
  }
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_IgnoreUnlessSpelledInSource;
  }

private:
  /// Whether a NOLINT comment covers what is reported at \p Loc: one on its
  /// line, `// NOLINT(malterlib-member-order)`, also a glob, a
  /// `NOLINTNEXTLINE` on the line before, or a `NOLINTBEGIN` and `NOLINTEND`
  /// block around it.
  bool coveredByNoLint(SourceLocation Loc, const SourceManager &SM);

  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
  /// The `NOLINTBEGIN` and `NOLINTEND` blocks of the check in each file of
  /// the translation unit, as offsets, found once.
  llvm::DenseMap<FileID, llvm::SmallVector<std::pair<size_t, size_t>, 2>>
      NoLintBlocks;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_MEMBERORDERCHECK_H
