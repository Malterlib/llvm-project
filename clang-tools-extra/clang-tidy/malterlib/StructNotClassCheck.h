//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_STRUCTNOTCLASSCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_STRUCTNOTCLASSCHECK_H

#include "FixIts.h"
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
  /// Reports `class` naming \p Record, a struct, at \p Keyword, which clang
  /// compares with the tag of \p Primary, the primary template of a
  /// template-id or of an explicit instantiation, when it is one: fixed only
  /// where that leaves the two tags as equal as they were.
  void reportElaborated(const CXXRecordDecl *Record, SourceLocation Keyword,
                        const CXXRecordDecl *Primary = nullptr);
  /// Leaves out of \p Fixing each definition whose fix would make a tag
  /// clang compares with another differ from it where the two are equal.
  void unfixMismatches(llvm::SmallPtrSetImpl<const CXXRecordDecl *> &Fixing);
  /// Reports the access labels of \p Definition that repeat the access in
  /// effect, also its default unless \p WithDefault is false.
  void reportRepeatedAccess(const CXXRecordDecl *Definition, bool WithDefault,
                            const SourceManager &SM);
  /// The class definitions to report, which are reported at the end of the
  /// translation unit, with fixes of the tags of every declaration, friend
  /// declaration, elaborated type specifier and explicit instantiation of
  /// them.
  llvm::SmallVector<std::pair<const CXXRecordDecl *, SourceLocation>, 8>
      Definitions;
  /// The keywords of the friend declarations of each class definition. An
  /// invalid location stands for one that can't be fixed.
  llvm::DenseMap<const CXXRecordDecl *, llvm::SmallVector<SourceLocation, 2>>
      FriendKeywords;
  /// The keywords of the elaborated type specifiers of classes, like
  /// `class CType *pValue`, and of their explicit instantiations, `extern
  /// template class TCThing<int>;`, which are fixed with the definition. An
  /// invalid location stands for one that can't be fixed.
  llvm::DenseMap<const CXXRecordDecl *, llvm::SmallVector<SourceLocation, 2>>
      ElaboratedKeywords;
  /// Of each class definition, the definitions of the primary templates
  /// whose tag clang compares the keywords fixed with it with: a template-id
  /// or an explicit instantiation of an instance of a partial specialization
  /// is compared with the primary template, not with the partial
  /// specialization it is made from.
  llvm::DenseMap<const CXXRecordDecl *,
                 llvm::SmallPtrSet<const CXXRecordDecl *, 2>>
      ComparedWith;
  /// The definitions of the primary templates that a `class` keyword which
  /// stays is compared with, which keeps them from being fixed.
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> KeptClassKeywords;
  const SourceManager *SM = nullptr;
  /// Where the conditional directives, the uses of macros and the includes
  /// are, which the fixes and the default access are told by.
  CPreprocessing Preprocessing;
  /// The edits of the fixes, told by Preprocessing.
  CFixIts FixIts{Preprocessing};
  /// Where macros are used in files whose definitions, also through the
  /// macros they use, have a block of `__if_exists` or `__if_not_exists`, in
  /// the order of the translation unit.
  llvm::SmallVector<SourceLocation, 4> ExistsExpansions;
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_STRUCTNOTCLASSCHECK_H
