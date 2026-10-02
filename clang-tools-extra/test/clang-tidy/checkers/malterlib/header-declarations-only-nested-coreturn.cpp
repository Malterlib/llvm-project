// RUN: %python %S/Inputs/nested-braced-coreturn.py 40 > %t.cpp
// RUN: clang-tidy --checks='-*,malterlib-header-declarations-only' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t.cpp -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --allow-empty %s

// The braced list a co_return gives to the constructor of what it returns,
// `co_return {Value};`, is looked at once, in it, not also as the operand of
// the co_return: nested coroutine lambdas that return so are gone through in
// time linear in their depth, not exponential.

// CHECK-NOT: .tmp.cpp:{{[0-9]+}}:{{[0-9]+}}: warning:
