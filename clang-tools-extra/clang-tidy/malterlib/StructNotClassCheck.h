//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_STRUCTNOTCLASSCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_STRUCTNOTCLASSCHECK_H

#include "MalterlibCheck.h"
#include "NamingUtils.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace clang::tidy::malterlib {

/// Checks that Malterlib types are declared with `struct`, not `class`, and
/// that a struct does not start with a `public:` section.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/struct-not-class.html
class StructNotClassCheck : public MalterlibCheck {
public:
  StructNotClassCheck(StringRef Name, ClangTidyContext *Context)
      : MalterlibCheck(Name, Context) {}
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus;
  }
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void registerPPCallbacks(const SourceManager &SM, Preprocessor *PP,
                           Preprocessor *ModuleExpanderPP) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override { Repeated.invalidate(); }
  void onEndOfTranslationUnit() override;
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_IgnoreUnlessSpelledInSource;
  }

private:
  /// Reports `class` naming \p Record, a struct, at \p Keyword.
  void reportElaborated(const CXXRecordDecl *Record, SourceLocation Keyword);
  /// The class definitions to report, which are reported at the end of the
  /// translation unit, with fixes of the tags of every declaration and
  /// friend declaration of them.
  llvm::SmallVector<std::pair<const CXXRecordDecl *, SourceLocation>, 8>
      Definitions;
  /// The keywords of the friend declarations of each class definition. An
  /// invalid location stands for one that can't be fixed.
  llvm::DenseMap<const CXXRecordDecl *, llvm::SmallVector<SourceLocation, 2>>
      FriendKeywords;
  /// The keywords of the elaborated type specifiers of classes, like
  /// `class CType *pValue`, which are fixed with the definition.
  llvm::DenseMap<const CXXRecordDecl *, llvm::SmallVector<SourceLocation, 2>>
      ElaboratedKeywords;
  const SourceManager *SM = nullptr;
  /// The text that the preprocessor skipped, like an inactive `#ifdef`
  /// branch, whose instantiations can be of another template of the same
  /// name.
  llvm::SmallVector<SourceRange, 8> SkippedRanges;
  /// Where the conditional preprocessing directives are, like `#ifdef`, also
  /// after a comment, `/* Layout */ #ifdef`.
  llvm::SmallVector<SourceLocation, 8> ConditionalDirectives;
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_STRUCTNOTCLASSCHECK_H
