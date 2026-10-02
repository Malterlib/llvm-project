// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/keeper-matrix.py values > %t.cpp
// RUN: %python %S/Inputs/keeper-matrix.py values-expect > %t-expect.txt
// RUN: clang-tidy %t.cpp '-checks=-*,malterlib-coroutine-reference-parameter' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-actual.txt
// RUN: diff %t-expect.txt %t-actual.txt
// RUN: %python %S/Inputs/keeper-matrix.py values-capture > %t-capture.cpp
// RUN: %python %S/Inputs/keeper-matrix.py values-capture-expect > %t-capture-expect.txt
// RUN: clang-tidy %t-capture.cpp '-checks=-*,malterlib-coroutine-reference-capture' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-capture-actual.txt
// RUN: diff %t-capture-expect.txt %t-capture-actual.txt

// What a use does to the storage it designates or points to
// (Inputs/keeper-matrix.py, sections values and values-capture), the same for a
// pointer and for the address of a reference, past parentheses, a cast to
// `void *`, the arms of a conditional, the right of a comma and `?:`, and the
// same where the consumer is built in or is the body of a function the value is
// given to that is seen. Compared or tested, `== nullptr`, `!`, `if`, `<=>`,
// nothing of what it points to is touched, clean after a suspension;
// dereferenced, `->`, `*`, `[0]`, given to a function that can't be seen, or
// returned, reported; given to a callee that isn't seen, whatever it does,
// reported. Also through a seen helper that gives it back by value, by
// reference, forwarded or through a recursion, and through a local copied on
// and then pointed elsewhere, through an integer and back, put in a member of
// a record held by value, by an aggregate, a seen constructor that stores it
// or a copy assignment, and loaded from it, each the same as the value
// itself; a seen constructor that only compares it keeps nothing of it;
// given to a constructor that can't be seen,
// reported; through a member function that gives back its object, `return
// *this;`, the same as the value itself; given to a recursive helper that
// passes its parameter's address on, a level `void *` doesn't have, reported. A
// parameter under reference-parameter, and a local captured by a closure
// called in the scope under reference-capture. What a member of a record
// holds after the suspension is what the definitions of that member that
// reach there give: each way the member gets and keeps or loses the value
// (initialized, assigned, set by a seen setter or one that forwards to it, in
// a nested record, by a seen constructor of a nested aggregate, overwritten
// through each of those, on one way only, cleared) crossed with each consumer
// (a read, a getter, a destructor at the end of the scope, a copy a closure
// captures).
