// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/async-destroy-copy-chains.py 200 > %t.cpp
// RUN: sh -c "ulimit -t 20 && clang-tidy %t.cpp '-checks=-*,malterlib-async-destroy' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs; echo status \$?" 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+:[0-9]+): warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p; s/^(status [0-9]+)$/\1/p' > %t.txt
// RUN: FileCheck %s --input-file=%t.txt --implicit-check-not=malterlib-

// A pointer the cleanup reads, pointed at a local declared after the guard
// through 200 copied conditionals, 200 diamonds of copies and a conditional of
// 200 copies, is reported: provenance tells each expression once, whatever the
// length of the chain. The same chain from a local declared before the guard
// isn't.

// CHECK: 209:2 malterlib-async-destroy
// CHECK-NEXT: 819:2 malterlib-async-destroy
// CHECK-NEXT: 1028:2 malterlib-async-destroy
// CHECK-NEXT: status 0
