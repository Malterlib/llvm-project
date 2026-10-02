// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/nested-conversions.py 30 > %t.cpp
// RUN: sh -c "ulimit -t 20 && clang-tidy %t.cpp '-checks=-*,malterlib-no-zero-bit-scan' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20" 2>&1 | FileCheck %s --implicit-check-not=warning:

// Integer wrappers nested thirty deep, each converting the one it holds at two
// returns: the members a conversion gives are told once for each conversion
// (givenMembers), not again at each return through each level below, which
// doubles with each level. The scan after a test of the innermost member is
// clean, the one without it is reported.

// CHECK: :40:10: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first
