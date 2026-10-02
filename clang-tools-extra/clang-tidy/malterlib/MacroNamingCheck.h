//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_MACRONAMINGCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_MACRONAMINGCHECK_H

#include "MalterlibCheck.h"
#include "NamingUtils.h"

namespace clang::tidy::malterlib {

/// Checks that macro names have the `D` prefix and macro parameters the `d_`
/// prefix.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/malterlib/macro-naming.html
class MacroNamingCheck : public MalterlibCheck {
public:
  MacroNamingCheck(StringRef Name, ClangTidyContext *Context)
      : MalterlibCheck(Name, Context),
        MinimumNameLength(
            unsignedOption("MinimumNameLength", DefaultMinimumNameLength)) {}
  void storeCheckOptions(ClangTidyOptions::OptionMap &Opts) override {
    Options.store(Opts, "MinimumNameLength", MinimumNameLength);
  }
  void registerPPCallbacks(const SourceManager &SM, Preprocessor *PP,
                           Preprocessor *ModuleExpanderPP) override;

  /// The shortest a name can be after its prefix.
  const unsigned MinimumNameLength;
};

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_MACRONAMINGCHECK_H
