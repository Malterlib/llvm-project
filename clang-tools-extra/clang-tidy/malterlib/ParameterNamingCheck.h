//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_PARAMETERNAMINGCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_PARAMETERNAMINGCHECK_H

#include "MalterlibCheck.h"
#include "NamingUtils.h"

namespace clang {
class TemplateParameterList;
} // namespace clang

namespace clang::tidy::malterlib {

/// Checks the prefixes of parameter names: `_`, `p_` (pack), `o_` (output)
/// and `po_` (output pack) for function parameters, and `t_`, `tp_` (pack),
/// `tf_` and `tfp_` (of function templates) for template parameters.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/parameter-naming.html
class ParameterNamingCheck : public MalterlibCheck {
public:
  ParameterNamingCheck(StringRef Name, ClangTidyContext *Context)
      : MalterlibCheck(Name, Context),
        MinimumNameLength(
            unsignedOption("MinimumNameLength", DefaultMinimumNameLength)) {}
  void storeCheckOptions(ClangTidyOptions::OptionMap &Opts) override {
    Options.store(Opts, "MinimumNameLength", MinimumNameLength);
  }
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onStartOfTranslationUnit() override {
    Repeated.invalidate();
    Instantiated.clear();
    LambdaKeys.clear();
    Deferred.clear();
  }
  void onEndOfTranslationUnit() override;
  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return TK_IgnoreUnlessSpelledInSource;
  }

private:
  const unsigned MinimumNameLength;
  void checkDeclaration(const NamedDecl *Declaration, const SourceManager &SM);
  void checkFunctionParameter(const ParmVarDecl *Parameter,
                              const SourceManager &SM);
  void checkTemplateParameters(const TemplateParameterList *Parameters,
                               bool OfFunction, const SourceManager &SM);
  /// The nodes checked before in the translation unit.
  CRepeatedMatches Repeated;
  /// A parameter of a function that is a template or of one, by the
  /// canonical declaration of the function and its index.
  using CParameterKey = std::pair<const FunctionDecl *, unsigned>;
  /// Whether some instantiation of each parameter can be written through.
  llvm::DenseMap<CParameterKey, bool> Instantiated;
  /// The function a parameter is keyed by: its canonical declaration, or of
  /// the call operator of a lambda, the first of those written at its place,
  /// which a lambda in a variable template has in each of its
  /// specializations, not instantiated from the written one.
  const FunctionDecl *keyOf(const FunctionDecl *Function);
  /// The call operators of lambdas by where they are written.
  llvm::DenseMap<unsigned, const FunctionDecl *> LambdaKeys;
  /// A parameter named as an output whose type depends on the parameters of
  /// a template, which its instantiations tell whether it can be.
  struct CDeferred {
    const ParmVarDecl *Parameter;
    SourceLocation Loc;
  };
  llvm::SmallVector<CDeferred, 4> Deferred;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_PARAMETERNAMINGCHECK_H
