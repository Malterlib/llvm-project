// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/keeper-matrix.py capture-owned > %t-owned.cpp
// RUN: %python %S/Inputs/keeper-matrix.py capture-owned-expect > %t-owned-expect.txt
// RUN: clang-tidy %t-owned.cpp '-checks=-*,malterlib-coroutine-reference-capture' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-owned-actual.txt
// RUN: diff %t-owned-expect.txt %t-owned-actual.txt
// RUN: %python %S/Inputs/keeper-matrix.py capture-parameter > %t-parameter.cpp
// RUN: %python %S/Inputs/keeper-matrix.py capture-parameter-expect > %t-parameter-expect.txt
// RUN: clang-tidy %t-parameter.cpp '-checks=-*,malterlib-coroutine-reference-capture' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-parameter-actual.txt
// RUN: diff %t-parameter-expect.txt %t-parameter-actual.txt
// RUN: %python %S/Inputs/keeper-matrix.py capture-object > %t-object.cpp
// RUN: %python %S/Inputs/keeper-matrix.py capture-object-expect > %t-object-expect.txt
// RUN: clang-tidy %t-object.cpp '-checks=-*,malterlib-coroutine-reference-capture' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-object-actual.txt
// RUN: diff %t-object-expect.txt %t-object-actual.txt

// Rule K in closures by type (Inputs/keeper-matrix.py): a closure given to
// fg_CallSafe that captures by reference what a local owner owns, what a
// reference or pointer parameter refers to, or a member of its object, and
// uses it after a suspension; with a keeper given to the function, of the
// owned class, a base or a derived class, a weak one, a `buffer`, a
// `sharedbuffer` and an owner of an unrelated class, held itself or in a
// field, a base, an array, a container or a nested record; captured by copy,
// moved or made, a parameter of the closure held by value, and a local of
// its body, held whatever initializes it and wherever it is declared; and
// of what an owner owns, records copied into the closure, also by an init
// capture, which are values (Rule V), and the value of a pointer member
// read through a pointer to a local record and of a reference member, which
// aren't followed, a view captured by copy of which only its own storage is
// read, getters of an object that holds the owner, and owners
// registered by their own names spelled through an alias in a class
// template and an alias template. Reported exactly where no fitting keeper
// keeps: a weak keeper, one that doesn't fit, a capture by copy of a
// `buffer` owner; and the controls: a keeper captured by reference or
// through a pointer, a parameter by reference, a pointer into a local
// captured itself, a captured view's pointer used, a dispatched closure that
// holds a view of a local whatever it uses, each row with no keeper, and a
// local of the frame, which no keeper keeps.
