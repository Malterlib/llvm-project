// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/cyclic-macros.py 40 > %t.cpp
// RUN: sh -c "ulimit -t 20 && clang-tidy %t.cpp '-checks=-*,malterlib-struct-not-class' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -fms-extensions" 2>&1 | FileCheck %s --implicit-check-not=warning:

// In Microsoft mode, the macro definitions a use reaches are gone through
// once each, also when each names the one before twice and the first names
// itself, a cycle that the definitions above it lead to (givesExists): what
// each strongly connected component of them gives is told once.

// CHECK: :56:1: warning: 'private:' repeats the access in effect in 'CPlain'
