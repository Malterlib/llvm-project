//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_HEADERDECLARATIONSONLYCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_HEADERDECLARATIONSONLYCHECK_H

#include "MalterlibCheck.h"
#include "NamingUtils.h"
#include "llvm/ADT/DenseMap.h"

namespace clang::tidy::malterlib {

/// Finds function definitions in `.h` files, which hold declarations only.
/// Template and inline function definitions go in `.hpp` files and the rest
/// in `.cpp` files.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/header-declarations-only.html
class HeaderDeclarationsOnlyCheck : public MalterlibCheck {
public:
  HeaderDeclarationsOnlyCheck(StringRef Name, ClangTidyContext *Context);
  void storeCheckOptions(ClangTidyOptions::OptionMap &Opts) override;
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override { Repeated.invalidate(); }
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_IgnoreUnlessSpelledInSource;
  }

  void onEndOfTranslationUnit() override { DeclarationFiles.clear(); }

private:
  const std::vector<StringRef> DeclarationFileExtensions;
  /// Whether each file that a function body is in holds declarations only.
  llvm::DenseMap<FileID, bool> DeclarationFiles;
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_HEADERDECLARATIONSONLYCHECK_H
