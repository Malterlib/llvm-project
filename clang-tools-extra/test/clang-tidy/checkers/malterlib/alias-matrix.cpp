// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/alias-matrix.py alias > %t.cpp
// RUN: %python %S/Inputs/alias-matrix.py alias-expect > %t-expect.txt
// RUN: clang-tidy %t.cpp '-checks=-*,malterlib-coroutine-reference-parameter' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-actual.txt
// RUN: diff %t-expect.txt %t-actual.txt

// Where a write through another way can land on a place a function sets
// (Inputs/alias-matrix.py): each way of writing (a member of another object
// through a pointer or a reference, the whole object, a base or derived class,
// an object that holds it, bytes, placement new, an element, another member
// of a union at any level, a pointer to the member, a reference member, a
// closure's capture and parameter, the object through its own pointer member,
// what an unseen function gives, a member function of another object, a local
// alias of it), crossed with each place (a member, a nested member and an
// element of an array member of the object of a member function, of what a
// reference or a pointer parameter refers to, the pointer a setter sets, a
// member of a local record) and each fact that separates them (none,
// DMibDistinct, new storage of the function, another member, another type).
// Reported where the write can land on the place, by the rule the generator
// states, not by what the checks do; a row they don't report yet is pending,
// one they report on safe code is marked KNOWN-FALSE-POSITIVE with the reason.
