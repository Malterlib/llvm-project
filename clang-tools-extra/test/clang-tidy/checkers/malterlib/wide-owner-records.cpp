// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/wide-owner-records.py 12 > %t.cpp
// RUN: sh -c "ulimit -t 20 && clang-tidy %t.cpp '-checks=-*,malterlib-coroutine-unsafe-this,malterlib-coroutine-reference-capture' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs" 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+:[0-9]+): (warning|error): .*[[]([a-z-]+)[]]$/\1 \3/p' > %t.txt
// RUN: FileCheck %s --input-file=%t.txt --implicit-check-not=malterlib- --implicit-check-not=clang-

// Records each holding ten members of the one before, twelve deep, captured
// with `this`: the keepers a record holds are told by a walk over its types,
// each once (keepersOf), not once per path through the records, which grows
// tenfold with each level. The copy holds owners given by a parameter of the
// member function, which may own another object of the class: an owner
// parameter keeps the object only where it states that it owns it
// (DMibKeepsThis), so the capture of `this` is reported.

// CHECK: 22:25 malterlib-coroutine-unsafe-this
