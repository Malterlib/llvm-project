//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_CONCEPTUALPREFIXCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_CONCEPTUALPREFIXCHECK_H

#include "MalterlibCheck.h"
#include "NamingUtils.h"
#include <optional>

namespace clang::tidy::malterlib {

/// Checks the conceptual prefixes that follow the storage prefix of variables
/// and parameters, where the declared type tells the concept: `b` for
/// booleans, `p` for pointers and smart pointers, `f` for function pointers
/// and function objects, and `i` for iterators.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/conceptual-prefix.html
class ConceptualPrefixCheck : public MalterlibCheck {
public:
  ConceptualPrefixCheck(StringRef Name, ClangTidyContext *Context);
  void storeCheckOptions(ClangTidyOptions::OptionMap &Opts) override;
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override {
    Repeated.invalidate();
    HandleAliases.clear();
  }
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_IgnoreUnlessSpelledInSource;
  }

private:
  std::optional<char> expectedConcept(QualType Type,
                                      const ASTContext &Context) const;
  QualType handleAliasOf(QualType Type, const ASTContext &Context);
  /// Whether \p Template, a class template or a specialization of one, is
  /// one of ValueWrapperTypes.
  bool isValueWrapper(const NamedDecl *Template) const;
  bool isRecordInList(
      const CXXRecordDecl *Record, llvm::ArrayRef<StringRef> List,
      llvm::ArrayRef<OverloadedOperatorKind> Operators = {},
      bool OrConvertsToBool = false) const;

  const std::vector<StringRef> SmartPointerTypes;
  /// The aliases of smart pointers that are handles, named for what they
  /// give rather than as pointers, like `CActorSubscription`.
  const std::vector<StringRef> SmartPointerHandleTypes;
  const std::vector<StringRef> FunctionObjectTypes;
  const std::vector<StringRef> IteratorTypes;
  /// The templates that wrap a value whose concept is the wrapper's, like
  /// `TCAtomic<bool>`, a boolean.
  const std::vector<StringRef> ValueWrapperTypes;
  /// The classes that are a boolean or a set of flags, which the `b` prefix
  /// can name without requiring it, like `CAtomicFlag`.
  const std::vector<StringRef> FlagTypes;
  const std::vector<StringRef> IgnoredNames;
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
  /// The handle aliases found of the types asked about in the translation
  /// unit, or none.
  llvm::DenseMap<const clang::Type *, QualType> HandleAliases;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_CONCEPTUALPREFIXCHECK_H
