// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/keeper-matrix.py cpp > %t.cpp
// RUN: %python %S/Inputs/keeper-matrix.py expect > %t-expect.txt
// RUN: clang-tidy %t.cpp '-checks=-*,malterlib-coroutine-reference-capture' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-actual.txt
// RUN: diff %t-expect.txt %t-actual.txt
// RUN: %python %S/Inputs/keeper-matrix.py result > %t-result.cpp
// RUN: %python %S/Inputs/keeper-matrix.py result-expect > %t-result-expect.txt
// RUN: clang-tidy %t-result.cpp '-checks=-*,malterlib-coroutine-lambda-call' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-result-actual.txt
// RUN: diff %t-result-expect.txt %t-result-actual.txt

// Rule K, what keeps storage alive, by type (Inputs/keeper-matrix.py): of
// each owner kind, `shared`, `unique`, `buffer`, `sharedbuffer`, and a weak
// keeper; each holder of the keeper, the owner itself, a field, a base, an
// array member, a container and a nested record; each take, a copy of an
// lvalue, a move and a new value, in a capture; a parameter of the closure
// held by value, which keeps through any kind; and a lambda coroutine's
// result made as a whole, a new value whatever its keeper member is
// initialized by. Reported exactly where no keeper keeps: a capture by copy
// of a `buffer` owner, a weak keeper, and a result whose keeper owns an
// unrelated class or that holds only a pointer to the captured owner.
