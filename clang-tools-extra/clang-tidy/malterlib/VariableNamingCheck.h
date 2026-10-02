//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_VARIABLENAMINGCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_VARIABLENAMINGCHECK_H

#include "MalterlibCheck.h"
#include "NamingUtils.h"

namespace clang::tidy::malterlib {

/// Checks the storage prefixes of variable names: none for local variables,
/// `c_` and `s_` for constant and static locals, `g_`, `gc_` and `gs_` for
/// globals, and `m_`, `mc_`, `ms_`, `mp_`, `mcp_` and `msp_` for members.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/variable-naming.html
class VariableNamingCheck : public MalterlibCheck {
public:
  VariableNamingCheck(StringRef Name, ClangTidyContext *Context)
      : MalterlibCheck(Name, Context),
        MinimumNameLength(
            unsignedOption("MinimumNameLength", DefaultMinimumNameLength)) {}
  void storeCheckOptions(ClangTidyOptions::OptionMap &Opts) override {
    Options.store(Opts, "MinimumNameLength", MinimumNameLength);
  }
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override { Repeated.invalidate(); }
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_IgnoreUnlessSpelledInSource;
  }

private:
  const unsigned MinimumNameLength;
  void checkName(const NamedDecl *Variable, SourceLocation Loc, StringRef Kind,
                 llvm::ArrayRef<StringRef> Expected);
  /// Checks that \p Variable has the prefix \p Expected, and that one that
  /// isn't constexpr isn't named as a constant, with \p ConstantPrefix.
  void checkName(const NamedDecl *Variable, SourceLocation Loc, StringRef Kind,
                 StringRef Expected, StringRef ConstantPrefix);
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_VARIABLENAMINGCHECK_H
