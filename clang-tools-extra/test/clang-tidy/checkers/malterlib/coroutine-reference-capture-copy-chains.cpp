// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/copy-chains.py 200 > %t.cpp
// RUN: sh -c "ulimit -t 20 && clang-tidy %t.cpp '-checks=-*,malterlib-coroutine-reference-capture' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs; echo status \$?" 2>&1 | FileCheck %s --implicit-check-not="warning:"

// A pointer captured by a dispatched coroutine lambda, pointed at a local
// through 200 copied conditionals, 200 diamonds of copies, or a conditional of
// 200 copies, is reported: provenance tells each expression once, whatever
// the length of the chain. The same chain from a global isn't.
// The 200 pointers assigned to each other in chained assignments round a loop
// are told in the time limit, and the capture is reported: provenance follows
// every assignment that can reach it.

// CHECK: warning: a TCFuture coroutine lambda captures 'pValue', which points to 'LateChain', but is not called directly where 'LateChain' is
// CHECK: warning: a TCFuture coroutine lambda captures 'pValue', which points to 'LateDiamond', but is not called directly where 'LateDiamond' is
// CHECK: warning: a TCFuture coroutine lambda captures 'pValue', which points to 'LateWide', but is not called directly where 'LateWide' is
// CHECK: warning: a TCFuture coroutine lambda captures 'pValue', which points to 'LateLoop', but is not called directly where 'LateLoop' is
// CHECK: status 0
