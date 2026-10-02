// A parameter pack of a function type in a class template is checked where
// the template declares it, not as the parameters an instantiation expands it
// to, which have the pack's name: TCCallSafeIsValidFunction of fg_CallSafe,
// instantiated by the requires clause of a function template in a precompiled
// header, whose instantiations a compile using it goes over.
// REQUIRES: malterlib-plugin
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++2c -x c++-header -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=.*' -I %S/Inputs %S/Inputs/plugin-expanded-pack.h -o %t.pch 2>&1 | count 0
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++2c -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=.*' -I %S/Inputs -include-pch %t.pch %s 2>&1 | count 0
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++2c -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=.*' -I %S/Inputs %s 2>&1 | count 0

#include "plugin-expanded-pack.h"

void fg_UseChecked()
{
	int Value = 0;
	fg_CallChecked([](int _Value, long _Other) {}, Value, 3L);
}
