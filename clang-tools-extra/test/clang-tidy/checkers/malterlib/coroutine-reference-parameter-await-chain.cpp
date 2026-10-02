// RUN: %python %S/Inputs/coroutine-await-chain.py 8000 > %t.cpp
// RUN: clang-tidy %t.cpp -checks='-*,malterlib-coroutine-reference-parameter' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs

// A chain of 8000 coroutines, each awaiting the next, awaited by a coroutine
// with a reference parameter: the check ends, and doesn't run out of stack.
