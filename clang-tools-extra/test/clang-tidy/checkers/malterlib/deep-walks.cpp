// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/deep-walks.py ptr constructor %if asan %{1000%} %else %{3000%} > %t-ptr-constructor.cpp
// RUN: sh -c "ulimit -s 2048 && clang-tidy %t-ptr-constructor.cpp '-checks=-*,malterlib-coroutine-reference-parameter' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs"
// RUN: %python %S/Inputs/deep-walks.py ref constructor %if asan %{1000%} %else %{3000%} > %t-ref-constructor.cpp
// RUN: sh -c "ulimit -s 2048 && clang-tidy %t-ref-constructor.cpp '-checks=-*,malterlib-coroutine-reference-parameter' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs"
// RUN: %python %S/Inputs/deep-walks.py ptrref constructor %if asan %{1000%} %else %{3000%} > %t-ptrref-constructor.cpp
// RUN: sh -c "ulimit -s 2048 && clang-tidy %t-ptrref-constructor.cpp '-checks=-*,malterlib-coroutine-reference-parameter' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs"
// RUN: %python %S/Inputs/deep-walks.py assign constructor %if asan %{1000%} %else %{3000%} > %t-assign-constructor.cpp
// RUN: sh -c "ulimit -s 2048 && clang-tidy %t-assign-constructor.cpp '-checks=-*,malterlib-coroutine-reference-parameter' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs"
// RUN: %python %S/Inputs/deep-walks.py cond constructor %if asan %{1000%} %else %{3000%} > %t-cond-constructor.cpp
// RUN: sh -c "ulimit -s 2048 && clang-tidy %t-cond-constructor.cpp '-checks=-*,malterlib-coroutine-reference-parameter' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs"
// RUN: %python %S/Inputs/deep-walks.py ptr lambda %if asan %{1000%} %else %{3000%} > %t-ptr-lambda.cpp
// RUN: sh -c "ulimit -s 2048 && clang-tidy %t-ptr-lambda.cpp '-checks=-*,malterlib-coroutine-unsafe-this' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs"
// RUN: %python %S/Inputs/deep-walks.py ref lambda %if asan %{1000%} %else %{3000%} > %t-ref-lambda.cpp
// RUN: sh -c "ulimit -s 2048 && clang-tidy %t-ref-lambda.cpp '-checks=-*,malterlib-coroutine-unsafe-this' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs"
// RUN: %python %S/Inputs/deep-walks.py ptrref lambda %if asan %{1000%} %else %{3000%} > %t-ptrref-lambda.cpp
// RUN: sh -c "ulimit -s 2048 && clang-tidy %t-ptrref-lambda.cpp '-checks=-*,malterlib-coroutine-unsafe-this' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs"
// RUN: %python %S/Inputs/deep-walks.py record lambda %if asan %{1000%} %else %{3000%} > %t-record-lambda.cpp
// RUN: sh -c "ulimit -s 2048 && clang-tidy %t-record-lambda.cpp '-checks=-*,malterlib-coroutine-unsafe-this' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs"
// RUN: %python %S/Inputs/deep-walks.py call actor %if asan %{1333%} %else %{4000%} > %t-call-actor.cpp
// RUN: sh -c "ulimit -s %if asan %{16384%} %else %{4096%} && clang-tidy %t-call-actor.cpp '-checks=-*,malterlib-coroutine-actor-reference' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs"

// Code as deep as a few thousand frames of a walk of the checks, which each
// check follows without running out of stack: a chain of local aliases,
// each made of the one before, that what a constructor keeps and what a
// coroutine lambda captures are followed through, and an expression nested
// as deep, which what suspends in it is found in. The walks recurse through
// withStack (StackSegments.h), which goes on on a new stack segment where
// the one it is on is nearly exhausted. The stack of the process is limited
// here so that a few thousand levels need one, where clang's own parsing of
// them still fits: the walk of 4000 nested calls needs about twice what the
// parsing does, 5 to 6 MiB, which AddressSanitizer makes more of both, and
// slower: under it each is a third as deep.
