// RUN: %python %S/Inputs/deep-phase-helpers.py 8192 > %t.cpp
// RUN: clang-tidy --checks='-*,malterlib-actor-phase' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t.cpp -- -std=c++20 2>&1 | FileCheck %s

// The phases a chain of helpers starts, each calling the next, are found
// through each helper, as long as the chain is, without a stack as deep: the
// last one starts the phase, so the flag isn't kept set by the assignment.

// CHECK: warning: 'm_bDestroyed' is the flag of a phase, which is only ever set to true
// CHECK-NOT: warning:
