// RUN: %python %S/Inputs/many-overrides.py 380 > %t-380.cpp
// RUN: clang-tidy --checks='-*,malterlib-coroutine-actor-reference' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t-380.cpp -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --allow-empty %s
// RUN: %python %S/Inputs/many-overrides.py 400 > %t-400.cpp
// RUN: clang-tidy --checks='-*,malterlib-coroutine-actor-reference,malterlib-coroutine-reference-parameter,malterlib-coroutine-unsafe-this' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %t-400.cpp -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --allow-empty %s

// Many overrides of one virtual method (Inputs/many-overrides.py): the
// overrides a call through a pointer can run are ordered by their places in
// the source, each place told once and kept by value while they are
// compared, as telling one adds to where the others are kept: with 380 and
// malterlib-coroutine-actor-reference alone, the table of places grows while
// two of them are compared.

// CHECK-NOT: .cpp:{{[0-9]+}}:{{[0-9]+}}: warning:
// CHECK-NOT: error:
