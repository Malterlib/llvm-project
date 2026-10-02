// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/deep-macros.py 100000 > %t.cpp
// RUN: sh -c "ulimit -s 1024 && clang-tidy %t.cpp '-checks=-*,malterlib-struct-not-class' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -fms-extensions" 2>&1 | FileCheck %s --implicit-check-not=warning:

// In Microsoft mode, a class whose body uses a macro whose definition gives
// a block of `__if_exists` through as many macros as code can define isn't
// checked for repeated labels, which the walk of the definitions tells
// without running out of stack (givesExists); a class that uses none is.

// CHECK: :100021:1: warning: 'private:' repeats the access in effect in 'CPlain'
