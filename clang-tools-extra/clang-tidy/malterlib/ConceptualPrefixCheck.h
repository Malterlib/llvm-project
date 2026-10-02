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
#include "llvm/ADT/SmallString.h"
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
    NamedAsFunction.clear();
    ExpectedConcepts.clear();
    MatchingPatterns.clear();
    BuiltinNames.clear();
    AliasWalks.clear();
    OpaqueHandles.clear();
    WrittenSpecializations.clear();
  }
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_IgnoreUnlessSpelledInSource;
  }

private:
  /// What the concept of a type is (expectedConcept). Required: the concept
  /// its name needs, only where each classification it rests on is known,
  /// and none when it has none or that can't be told. MayBe: the concepts it
  /// can be where that can't be told, of a classification that is unknown,
  /// like an operator whose constraint clang hasn't checked, `operator++()
  /// requires t_bEnabled`, or a smart pointer that can be a handle, whose
  /// prefixes are allowed and not required. Opaque: an opaque handle, also one
  /// a value wrapper holds, `TCAtomic<pthread_t>`, which takes no concept
  /// prefix on any target.
  struct CConcepts {
    std::optional<char> Required;
    llvm::SmallString<4> MayBe;
    bool Opaque = false;
    /// Whether \p Concept is among MayBe.
    bool mayBe(char Concept) const {
      return llvm::is_contained(MayBe, Concept);
    }
  };
  /// The concepts \p Type is (CConcepts).
  CConcepts expectedConcept(QualType Type, const ASTContext &Context) const;
  /// expectedConcept, not memoized.
  CConcepts expectedConceptOnce(QualType Type, const ASTContext &Context) const;
  /// The specialization that \p Type is, as written: Type::getAs, the first
  /// level of its sugar that is one.
  const TemplateSpecializationType *
  writtenSpecialization(const clang::Type *Type) const;
  /// The indexes of the patterns of \p List that \p Named matches
  /// (matchesName).
  llvm::SmallVector<unsigned, 1>
  matchingPatterns(const NamedDecl *Named,
                   llvm::ArrayRef<StringRef> List) const;
  /// The concept \p Type is, of expectedConcept, adding to \p Concepts what it
  /// can be where that can't be told, and setting \p Undecided where that
  /// ranks before the concept given.
  std::optional<char> conceptOf(QualType Type, const ASTContext &Context,
                                CConcepts &Concepts, bool &Undecided) const;
  QualType handleAliasOf(QualType Type, const ASTContext &Context) const;
  /// Whether the declared type \p Declaration, an alias or a class, is named
  /// as a function type by its `F` or `TF` type prefix, and isn't one of
  /// NotFunctionTypes or of a system header.
  bool declaredAsFunctionType(const NamedDecl *Declaration) const;
  /// Whether \p Type, as it is written, is named as a function type.
  bool namedAsFunctionType(QualType Type, int Depth = 0) const;
  /// namedAsFunctionType, not memoized.
  bool namedAsFunctionTypeOnce(QualType Type, int Depth) const;
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
  /// Whether \p Record, or a base of it, is one \p List names, also by a
  /// pattern that only matches a part of the name, `*Iterator*`, when it has
  /// one of \p Operators, or, with \p OrConvertsToBool, converts to `bool`;
  /// not when it declares one of \p Operators that can't be used, which hides
  /// those of its bases. None when that can't be told, of an operator whose
  /// constraint clang hasn't checked.
  std::optional<bool> isRecordInList(
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
  /// What namedAsFunctionType answered of each type at each depth in the
  /// translation unit.
  mutable llvm::DenseMap<std::pair<void *, int>, bool> NamedAsFunction;
  /// What expectedConcept answered of each type in the translation unit.
  mutable llvm::DenseMap<void *, CConcepts> ExpectedConcepts;
  /// What matchingPatterns answered of each declaration and list in the
  /// translation unit, by the list's data.
  mutable llvm::DenseMap<std::pair<const NamedDecl *, const void *>,
                         llvm::SmallVector<unsigned, 1>>
      MatchingPatterns;
  /// The name of the built-in type that each type is named as in the
  /// translation unit, or an empty one.
  mutable llvm::DenseMap<void *, StringRef> BuiltinNames;
  /// What conceptOf's walk of the sugar finds from a level, to the end of
  /// its aliases or to the concept one of them tells.
  struct CAliasWalk {
    /// Whether an alias tells the concept, which is Return.
    bool Returns = false;
    std::optional<char> Return;
    /// The concepts allowed on the way, in order (CConcepts::MayBe).
    llvm::SmallString<4> Allowed;
    /// Whether an alias whose concept can't be told was met.
    bool Undecided = false;
    /// Whether a smart pointer not known to be a handle or not was met.
    bool HandleUnknown = false;
    /// Whether the walk went through a handle, at its end.
    bool ThroughHandle = false;
  };
  /// Of each level of sugar in the translation unit, and whether the walk
  /// reaches it through a handle, what the walk finds from it.
  mutable llvm::DenseMap<std::pair<void *, int>, CAliasWalk> AliasWalks;
  /// Of each level of sugar in the translation unit, whether it or what it
  /// is an alias of is one of OpaqueHandleTypes (isOpaqueHandle).
  mutable llvm::DenseMap<void *, bool> OpaqueHandles;
  /// Of each level of sugar in the translation unit, the specialization
  /// writtenSpecialization finds from it, or none.
  mutable llvm::DenseMap<const clang::Type *,
                         const TemplateSpecializationType *>
      WrittenSpecializations;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_CONCEPTUALPREFIXCHECK_H
