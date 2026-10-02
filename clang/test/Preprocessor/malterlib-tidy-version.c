// The version of the Malterlib clang-tidy checks, which code can test for.
// RUN: %clang_cc1 -E -dM %s | FileCheck %s
// RUN: %clang_cc1 -x c++ -E -dM %s | FileCheck %s
// CHECK: #define __malterlib_tidy_version__ 1
