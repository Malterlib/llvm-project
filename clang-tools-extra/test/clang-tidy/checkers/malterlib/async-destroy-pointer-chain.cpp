// RUN: %python %S/Inputs/reverse-pointer-chain.py 20000 > %t.cpp
// RUN: clang-tidy --checks='-*,malterlib-async-destroy' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t.cpp -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck %s

// The pointers to the guard are found each once, also when they are declared
// in the opposite order of how each is given the one before, which a search
// that goes over all of them until none is added finds one at a time, in a
// time quadratic in their number.

// CHECK: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
// CHECK-NOT: {{warning|error}}:
