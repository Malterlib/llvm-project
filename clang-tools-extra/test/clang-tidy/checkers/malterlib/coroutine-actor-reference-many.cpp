// RUN: %python %S/Inputs/many-reference-captures.py 16384 > %t.cpp
// RUN: clang-tidy --checks='-*,malterlib-coroutine-actor-reference' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t.cpp -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --allow-empty %s

// A lambda that captures many locals has what each is made from, its
// initializer and what its function assigns to it, found from the
// assignments of the function, which are found once, in time linear in the
// number of locals, not by going through the function for each.

// CHECK-NOT: .tmp.cpp:{{[0-9]+}}:{{[0-9]+}}: warning:
