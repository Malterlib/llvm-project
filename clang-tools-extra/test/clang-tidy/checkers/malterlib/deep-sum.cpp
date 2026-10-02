// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/deep-sum.py 60000 > %t.cpp
// RUN: sh -c "ulimit -s 8192 && ulimit -t 120 && clang-tidy %t.cpp '-checks=-*,malterlib-coroutine-reference-parameter' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs; echo status \$?" 2>&1 | FileCheck %s --implicit-check-not="Stack dump" --implicit-check-not="warning:"

// An expression nested as deep as a sum of 60,000 terms is gone through on
// the stack helper where the parents of a coroutine's statements are told
// (CParents), not on the thread's own stack, which it would exhaust.

// CHECK: status 0
