// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/keeper-matrix.py getters > %t-getters.cpp
// RUN: %python %S/Inputs/keeper-matrix.py getters-expect > %t-getters-expect.txt
// RUN: clang-tidy %t-getters.cpp '-checks=-*,malterlib-coroutine-reference-capture,malterlib-coroutine-reference-parameter,malterlib-coroutine-unsafe-this,malterlib-async-destroy' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-getters-actual.txt
// RUN: diff %t-getters-expect.txt %t-getters-actual.txt

// What a call gives relative to its object and its arguments (the call-result
// relation), under each check that asks: Inputs/keeper-matrix.py, section
// `getters`, says what is reported and what is clean.
