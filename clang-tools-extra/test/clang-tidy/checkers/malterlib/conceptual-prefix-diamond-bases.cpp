// RUN: %python %S/Inputs/diamond-bases.py 8 16 pointer > %t.cpp
// RUN: clang-tidy --checks='-*,malterlib-conceptual-prefix' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t.cpp -- -std=c++20 2>&1 | FileCheck --allow-empty %s

// Whether a class derives from a handle goes through each base once, also one
// that every path of a diamond leads to: shared bases of layers of classes are
// gone through in time linear in their number, not exponential. The fixture's
// options file has SmartPointerHandleTypes, and the class is a smart pointer,
// so they are gone through.

// CHECK-NOT: .tmp.cpp:{{[0-9]+}}:{{[0-9]+}}: warning:
