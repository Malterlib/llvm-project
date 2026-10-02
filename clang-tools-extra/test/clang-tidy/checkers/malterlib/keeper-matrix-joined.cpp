// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/keeper-matrix.py joined > %t.cpp
// RUN: %python %S/Inputs/keeper-matrix.py joined-expect > %t-expect.txt
// RUN: clang-tidy %t.cpp '-checks=-*,malterlib-coroutine-reference-capture' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-actual.txt
// RUN: diff %t-expect.txt %t-actual.txt

// Alive until a join (Inputs/keeper-matrix.py, section joined): what a lambda
// that code that runs to completion starts and joins in the full expression,
// `co_await fg_CallSafe([...]() ...)`, borrows lives until the join by the
// kind of its storage, a local or a parameter declared before the full
// expression, a temporary of it, static storage and what an owner owns with a
// keeper there, in each derivation shape: captured directly, through a
// reference local, a pointer by `&*`, a chain of two references, a pointer to
// a member, an identity cast and a downcast, a getter by reference and by
// pointer, and a conditional of two borrows. Reported for a local declared
// after it, a temporary of an earlier statement, what an owner owns with no
// keeper, thread-local storage, and where the code doesn't run to
// completion.
