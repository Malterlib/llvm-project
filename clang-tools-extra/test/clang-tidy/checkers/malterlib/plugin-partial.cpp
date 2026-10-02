// The instantiations of a partial specialization defined in a source are
// checked there, also when the template it specializes is declared in a
// header that isn't reported, whose instantiations they are.
// REQUIRES: malterlib-plugin
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=nomatch' -I %S/Inputs %s 2>&1 | FileCheck --implicit-check-not=warning: --implicit-check-not=error: %s

#include "plugin-partial.h"

template <typename t_CType>
struct TCPartialBox<t_CType *>
{
	void f_Run(t_CType const &o_Value) {}
// CHECK: plugin-partial.cpp:[[@LINE-1]]:{{[0-9]+}}: warning: {{.*}}'o_Value'{{.*}}[malterlib-parameter-naming]
};

void fg_Use()
{
	TCPartialBox<int *>{}.f_Run(42);
}
