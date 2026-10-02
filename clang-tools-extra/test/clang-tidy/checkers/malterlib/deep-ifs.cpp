// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/deep-ifs.py 1500 > %t.cpp
// RUN: sh -c "ulimit -t 120 && clang-tidy %t.cpp '-checks=-*,malterlib-coroutine-reference-parameter' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -fbracket-depth=4096 -I %S/Inputs" 2>&1 | FileCheck %s

// Blocks of a constant condition nested as deep as code can nest them are
// gone through once each: what always leaves and what has a target of a
// jump are each told once of a statement (alwaysLeaves, hasJumpTarget), not
// again at each level around it.

// CHECK: :4:28: warning: TCFuture coroutine 'fg_Deep' uses '_Value' after a suspension
