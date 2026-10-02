// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/conceptual-prefix-matrix.py code > %t.cpp
// RUN: %python %S/Inputs/conceptual-prefix-matrix.py expect > %t-expect.txt
// RUN: clang-tidy %t.cpp '-checks=-*,malterlib-conceptual-prefix' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: (warning|error|note): (.*)$/\1 \2: \3/p' > %t-actual.txt
// RUN: diff %t-expect.txt %t-actual.txt

// What malterlib-conceptual-prefix concludes of the evidence a class gives a
// concept by, in each state the evidence can be in (Inputs/conceptual-prefix-
// matrix.py): an iterator's operators, also free ones, a pointer's operators
// and conversion to bool, a name SmartPointerTypes lists exactly, a function
// object's call operator and conversion to a function pointer; plain, deleted,
// constrained true, false, by a parameter of the class nothing uses or
// something uses, a member template, constrained by its own parameter,
// inherited, brought in with `using`, hiding a base's, private, missing, not
// defined; in a variable of a specialization, a parameter of a function
// template given a specialization of the parameters, and an instance of a
// function template given it. Expected by the rule the generator states, not by
// what the check does; a row it doesn't report yet is RESTORE-MESSAGES, one it
// reports wrongly KNOWN-FALSE-POSITIVE, one waiting for the user QUESTION. The
notes are expected too, and an error fails it.
