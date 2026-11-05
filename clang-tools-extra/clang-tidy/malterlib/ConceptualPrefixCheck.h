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
  /// The concept \p Type is, none when it has none or that can't be told.
  /// \p IteratorAllowed, when given, is set when it is also an iterator, which
  /// a handle is named as. \p PointerAllowed, when given, is set when it is a
  /// smart pointer that
  /// isn't known not to be a handle, which can be named either way.
  /// \p OpaqueHandle, when given, is set when it is an opaque handle, also
  /// one a value wrapper holds, `TCAtomic<pthread_t>`, which takes no concept
  /// prefix on any target.
  std::optional<char> expectedConcept(QualType Type, const ASTContext &Context,
                                      bool *PointerAllowed = nullptr,
                                      bool *IteratorAllowed = nullptr,
                                      bool *OpaqueHandle = nullptr) const;
  QualType handleAliasOf(QualType Type, const ASTContext &Context) const;
  /// Whether the declared type \p Declaration, an alias or a class, is named
  /// as a function type by its `F` or `TF` type prefix, and isn't one of
  /// NotFunctionTypes or of a system header.
  bool declaredAsFunctionType(const NamedDecl *Declaration) const;
  /// Whether \p Type, as it is written, is named as a function type.
  bool namedAsFunctionType(QualType Type, int Depth = 0) const;
  /// Whether \p Type is one of OpaqueHandleTypes, also through an alias of
  /// one, `CThreadID` of `pthread_t`.
  bool isOpaqueHandle(QualType Type, const ASTContext &Context) const;
  /// Whether \p Template, a class template or a specialization of one, is
  /// one of ValueWrapperTypes.
  bool isValueWrapper(const NamedDecl *Template) const;
  /// Whether \p Record is one of FunctionObjectTypes, and not of
  /// NotFunctionObjectTypes; none when that isn't known, of one with a base
  /// that depends on the parameters of its template, which an instantiation
  /// can make one of NotFunctionObjectTypes.
  std::optional<bool> isFunctionObject(const CXXRecordDecl *Record) const;
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
  /// The types whose names start with the `F` or `TF` type prefix that
  /// aren't function types, of a library whose headers aren't system headers.
  const std::vector<StringRef> NotFunctionTypes;
  /// The classes that FunctionObjectTypes would match that aren't function
  /// objects, as their call operator only binds and gives a builder, like the
  /// type of `g_ActorFunctor`.
  const std::vector<StringRef> NotFunctionObjectTypes;
  /// The opaque handles of systems that are pointers on some targets only,
  /// like `pthread_t`, which take no concept prefix.
  const std::vector<StringRef> OpaqueHandleTypes;
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
  /// The handle aliases found of the types asked about in the translation
  /// unit, or none.
  mutable llvm::DenseMap<const clang::Type *, QualType> HandleAliases;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_CONCEPTUALPREFIXCHECK_H
