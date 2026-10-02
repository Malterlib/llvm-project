//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../ClangTidy.h"
#include "../ClangTidyModule.h"
#include "ConceptualPrefixCheck.h"
#include "FunctionNamingCheck.h"
#include "HeaderDeclarationsOnlyCheck.h"
#include "MacroNamingCheck.h"
#include "NoZeroBitScanCheck.h"
#include "ParameterNamingCheck.h"
#include "ParameterOwnershipCheck.h"
#include "StructNotClassCheck.h"
#include "TypeNamingCheck.h"
#include "UnusedParameterCastCheck.h"
#include "VariableNamingCheck.h"

namespace clang::tidy {
namespace malterlib {
namespace {

/// This module is for checks of the Malterlib coding standard.
class MalterlibModule : public ClangTidyModule {
public:
  void addCheckFactories(ClangTidyCheckFactories &CheckFactories) override {
    CheckFactories.registerCheck<ConceptualPrefixCheck>(
        "malterlib-conceptual-prefix");
    CheckFactories.registerCheck<FunctionNamingCheck>(
        "malterlib-function-naming");
    CheckFactories.registerCheck<HeaderDeclarationsOnlyCheck>(
        "malterlib-header-declarations-only");
    CheckFactories.registerCheck<MacroNamingCheck>("malterlib-macro-naming");
    CheckFactories.registerCheck<NoZeroBitScanCheck>(
        "malterlib-no-zero-bit-scan");
    CheckFactories.registerCheck<ParameterNamingCheck>(
        "malterlib-parameter-naming");
    CheckFactories.registerCheck<ParameterOwnershipCheck>(
        "malterlib-parameter-ownership");
    CheckFactories.registerCheck<StructNotClassCheck>(
        "malterlib-struct-not-class");
    CheckFactories.registerCheck<TypeNamingCheck>("malterlib-type-naming");
    CheckFactories.registerCheck<UnusedParameterCastCheck>(
        "malterlib-unused-parameter-cast");
    CheckFactories.registerCheck<VariableNamingCheck>(
        "malterlib-variable-naming");
  }
};

} // namespace

// Register the MalterlibTidyModule using this statically initialized variable.
static ClangTidyModuleRegistry::Add<MalterlibModule>
    X("malterlib-module", "Adds checks of the Malterlib coding standard.");
} // namespace malterlib

// This anchor is used to force the linker to link in the generated object file
// and thus register the MalterlibModule.
volatile int MalterlibModuleAnchorSource = 0; // NOLINT(misc-use-internal-linkage)

} // namespace clang::tidy
