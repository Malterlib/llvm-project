// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/actor-matrix.py code > %t.cpp
// RUN: %python %S/Inputs/actor-matrix.py expect > %t-expect.txt
// RUN: clang-tidy %t.cpp '-checks=-*,malterlib-actor-dispatch-this,malterlib-actor-phase,malterlib-continue-running-on-actor' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' | sort -u -n > %t-actual.txt
// RUN: diff %t-expect.txt %t-actual.txt

// The rules of the three actor checks over the forms they meet
// (Inputs/actor-matrix.py): actor-dispatch-this, each carrier of the object
// into a closure (`this`, an init capture of it, the address of a member, a
// reference to one, a local pointer replaced before, after, in one branch, in
// both, by a visible setter, a record that holds the address, one whose member
// is replaced), crossed with each use in the body (read, write, given to a
// helper that reads, compared, turned into a number) and each actor it runs on
// (another, the own one, an own handle changed before or after the dispatch);
// whether the dispatch can run; the forms that dispatch and the function
// objects on the way; the handle's actor and the object of a parameter where it
// is read. actor-phase, each way to an annotated collection (directly, a
// reference or a pointer to the state, a local copy, a reference or a pointer
// to the copy, a reference member of a local) crossed with each change and
// each context (another function, the phase, the constructor), and the
// callbacks a constructor makes. continue-running-on-actor, each carrier of the
// object crossed with each use and each place of the use against the switches.
