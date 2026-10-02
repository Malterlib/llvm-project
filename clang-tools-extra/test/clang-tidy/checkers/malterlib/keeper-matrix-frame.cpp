// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/keeper-matrix.py frame > %t.cpp
// RUN: %python %S/Inputs/keeper-matrix.py frame-expect > %t-expect.txt
// RUN: clang-tidy %t.cpp '-checks=-*,malterlib-coroutine-reference-parameter' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-actual.txt
// RUN: diff %t-expect.txt %t-actual.txt

// Rule K in the frame by type (Inputs/keeper-matrix.py): a coroutine that
// uses what a reference or pointer parameter refers to after a suspension,
// with a keeper held by value in a parameter or a local, whatever
// initializes it and wherever in the body it is declared; and one without a
// suspension whose result, made as a whole, holds a pointer to it and a
// keeper, its member copied, moved or made: a new value. Keepers of the
// class, a base or a derived class, a weak one, a `buffer`, a
// `sharedbuffer` and an owner of an unrelated class, held itself or in a
// field, a base, an array, a container or a nested record. One row each,
// with a control, of the other holders, a virtual base, an array
// initialized by `{}` or in part and a local closure that captures a keeper
// by copy; of the other declarations, a by-value explicit object parameter,
// a local under a `case` or `default` label, a local of an enclosing scope
// and a structured binding by value; of the takes at a result, the name of
// a local result, its `fg_Move` and a copy of a reference to it, records
// held by value, which are values (Rule V), as are the values of a pointer
// member read through a pointer to a local record and of a reference
// member, also as a getter returns it, and a `co_return` of another
// type, which makes a new value; and of lambdas nested in the coroutine and
// generic ones, owners registered by their own names spelled through
// aliases, buffers borrowed through a reference parameter, a value kept in
// a capture, the result type and the anchors on a derivation. Reported
// exactly where no fitting keeper keeps: a weak keeper, one that doesn't
// fit; and the controls: a keeper parameter by reference or through a
// pointer, a closure that captures the keeper by reference, an explicit
// object parameter and a structured binding by reference, a result that
// holds only a pointer to a keeper, an owner whose entry names an alias,
// which registers nothing, and each row with no keeper.
