// RUN: %python %S/Inputs/diamond-bases.py 8 16 parameter > %t.cpp
// RUN: clang-tidy --checks='-*,malterlib-parameter-naming' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t.cpp -- -std=c++20 2>&1 | FileCheck --allow-empty %s

// Whether an object taken by value can be written through goes through each
// base once, also one that every path of a diamond leads to: shared bases of
// layers of classes are gone through in time linear in their number, not
// exponential.

// CHECK-NOT: .tmp.cpp:{{[0-9]+}}:{{[0-9]+}}: warning:
