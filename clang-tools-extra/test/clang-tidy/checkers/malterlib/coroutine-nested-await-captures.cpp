// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/nested-await-captures.py 28 > %t.cpp
// RUN: sh -c "ulimit -t 20 && clang-tidy %t.cpp '-checks=-*,malterlib-coroutine-reference-capture' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs; echo status \$?" 2>&1 | FileCheck %s --implicit-check-not="warning:"

// Awaits nested 28 deep in each other's arguments, whose nodes a coroutine
// shares, with a captured local pointer and a captured reference: what is
// assigned to a local and the anchors of a borrow are collected by the walk
// that visits each statement once (CStmtWalk), not again for each sharing.

// CHECK: :8:22: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Value', after a suspension
// CHECK: :14:23: warning: a TCFuture coroutine lambda uses 'Alias', which it captures by reference, after a suspension
// CHECK: status 0
