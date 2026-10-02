//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_NOZEROBITSCANCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_NOZEROBITSCANCHECK_H

#include "MalterlibCheck.h"
#include "NamingUtils.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"

namespace clang::tidy::malterlib {

/// Finds calls of bit scans that require a value other than zero, like
/// `fg_GetHighestBitSetNoZero`, whose argument is not checked for zero first.
/// This is a best effort check.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/no-zero-bit-scan.html
class NoZeroBitScanCheck : public MalterlibCheck {
public:
  NoZeroBitScanCheck(StringRef Name, ClangTidyContext *Context);
  void storeCheckOptions(ClangTidyOptions::OptionMap &Opts) override;
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override { Repeated.invalidate(); }
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_AsIs;
  }

private:
  bool isChecked(const CallExpr *Call, ASTContext &Context,
                 bool UnknownChecks = true, const Expr *Chosen = nullptr) const;
  /// isChecked, with \p ThroughCopies, through a const local copy of what it
  /// tests to what it is a copy of, or else not; \p Copied tells whether it
  /// went through one.
  bool isCheckedThrough(const CallExpr *Call, ASTContext &Context,
                        bool UnknownChecks, const Expr *Chosen,
                        bool ThroughCopies, bool &Copied) const;
  void report(SourceLocation Loc, const NamedDecl *Callee);

  const std::vector<StringRef> Functions;
  const std::vector<StringRef> CheckMacros;
  const std::vector<StringRef> AssumeMacros;
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_NOZEROBITSCANCHECK_H
