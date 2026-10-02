// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/keeper-matrix.py this-frame > %t-frame.cpp
// RUN: %python %S/Inputs/keeper-matrix.py this-frame-expect > %t-frame-expect.txt
// RUN: clang-tidy %t-frame.cpp '-checks=-*,malterlib-coroutine-unsafe-this' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-frame-actual.txt
// RUN: diff %t-frame-expect.txt %t-frame-actual.txt
// RUN: %python %S/Inputs/keeper-matrix.py this-closure > %t-closure.cpp
// RUN: %python %S/Inputs/keeper-matrix.py this-closure-expect > %t-closure-expect.txt
// RUN: clang-tidy %t-closure.cpp '-checks=-*,malterlib-coroutine-unsafe-this' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-closure-actual.txt
// RUN: diff %t-closure-expect.txt %t-closure-actual.txt

// Rule K for the object of a class that isn't an actor, by type
// (Inputs/keeper-matrix.py): a member coroutine that uses its object after a
// suspension, with a keeper in a parameter held by value or a local before
// the suspension, or whose result, without a suspension, holds a pointer to
// a member and a keeper; and a closure `[this]` given to fg_CallSafe in a
// member function, with a keeper captured by copy, moved or made, a
// parameter of the closure held by value, or a local of its body. Keepers of
// the class, a base or a derived class, a weak one, a `buffer`, a
// `sharedbuffer` and an owner of an unrelated class, held itself or in a
// field, a base, an array, a container or a nested record. Reported exactly
// where no fitting keeper keeps: a weak keeper and one that doesn't fit; and
// the controls: a keeper by
// reference or through a pointer, a local after the use or in a sibling
// scope, and a result that holds only a pointer to a keeper.
