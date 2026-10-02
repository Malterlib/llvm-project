// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/keeper-matrix.py landed > %t.cpp
// RUN: %python %S/Inputs/keeper-matrix.py landed-expect > %t-expect.txt
// RUN: clang-tidy %t.cpp '-checks=-*,malterlib-coroutine-actor-reference' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-actual.txt
// RUN: diff %t-expect.txt %t-actual.txt

// Where a borrow from an entry of actor state lands, the "landed" rule of
// actor-reference's stale data (Inputs/keeper-matrix.py, section landed): a
// borrow of what the owner in an entry owns, used after a suspension, is alive
// where the frame keeps what that owner owns, by each derivation shape: the
// local initialized directly, a reference local, a pointer local by `&*`, also
// `auto *`, a chain of two references, a member of the borrow, an identity
// cast and a downcast, a getter by reference and by pointer, and a conditional
// of two borrows. Reported in each shape with no keeper and with a keeper of
// the actor only, and a borrow of the entry itself, by reference, pointer,
// chain, cast and conditional, with a keeper.
