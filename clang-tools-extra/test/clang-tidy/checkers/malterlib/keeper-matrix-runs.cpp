// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/keeper-matrix.py runs > %t.cpp
// RUN: %python %S/Inputs/keeper-matrix.py runs-expect > %t-expect.txt
// RUN: clang-tidy %t.cpp '-checks=-*,malterlib-runs-to-completion' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-actual.txt
// RUN: diff %t-expect.txt %t-actual.txt

// Rule K in the arguments of a call that runs to completion, by type
// (Inputs/keeper-matrix.py): a pointer argument into what a local owner
// owns, of each owner kind, given to a call joined where the coroutine
// finishes, after its locals are gone, with a keeper made of the owner as
// another argument held by value, itself or in a field, a base, an array, a
// container or a nested record, copied, moved or made; and both in one
// record; a local record given by value and the value of a pointer member
// read through a pointer to one, which are values (Rule V). Reported exactly
// where no keeper keeps: a copy of a `buffer` owner
// and a weak keeper; and the controls: a keeper of an unrelated class, one
// given by reference or through a pointer, and a record made in the
// argument of a local of a block the join is outside of.
