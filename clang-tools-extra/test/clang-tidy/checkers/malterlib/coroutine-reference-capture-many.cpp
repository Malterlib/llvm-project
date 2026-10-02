// RUN: %python %S/Inputs/many-reference-captures.py 16384 > %t.cpp
// RUN: clang-tidy --checks='-*,malterlib-coroutine-reference-capture' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t.cpp -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck %s

// A lambda that captures many locals by reference has the uses of each found
// by the index of its body made once, in time linear in their number, not by
// going through the body for each.

// CHECK: warning: a TCFuture coroutine lambda uses 'Value7', which it captures by reference, after a suspension
// CHECK-NOT: warning:
