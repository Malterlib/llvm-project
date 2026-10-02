// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/parameter-ownership-place-matrix.py code > %t.cpp
// RUN: %python %S/Inputs/parameter-ownership-place-matrix.py expect > %t-expect.txt
// RUN: clang-tidy %t.cpp '-checks=-*,malterlib-parameter-ownership' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-actual.txt
// RUN: diff %t-expect.txt %t-actual.txt

// Where a write a destructor makes lands (Inputs/parameter-ownership-place-matrix.py):
// each place (a member, an element of an array member, a member of such an
// element, a base's member, the object through a cast, bytes inside and
// outside a member, storage the destructor makes, what a pointer member
// points to, also of an element of an array of pointers, a member of that,
// what a reference member refers to, static storage), crossed with each way
// of writing it (assignment, compound assignment, increment, a member function
// that writes its object, a helper given its address, a helper given the
// object, `memset`). Reported where copies of the object share the place, by
// the rule the generator states, not by what the check does; a row it doesn't
// report yet is pending, a row it reports on the object's own storage is a
// known false positive, each counted by the gate (lbt/po-matrix-check.sh).
