// A header that is included in the arguments of an attribute that leads a
// declaration, whose range leaves them out, is checked.
// REQUIRES: malterlib-plugin
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -Werror -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=plugin-attribute-argument[.]hpp$' -I %S/Inputs %s 2>&1 | FileCheck --implicit-check-not=warning: --implicit-check-not=error: %s

// CHECK: plugin-attribute-argument.hpp:1:8: error: parameter 'Wrong' should have the '_' prefix [malterlib-parameter-naming]

#include "plugin-attribute.hpp"
void fg_Run();
