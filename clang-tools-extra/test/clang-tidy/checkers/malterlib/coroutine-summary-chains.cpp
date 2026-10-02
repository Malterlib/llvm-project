// RUN: %python %S/Inputs/summary-chain.py kept 10000 > %t-kept.cpp
// RUN: clang-tidy %t-kept.cpp -checks='-*,malterlib-coroutine-reference-parameter' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs
// RUN: %python %S/Inputs/summary-chain.py set 12000 > %t-set.cpp
// RUN: clang-tidy %t-set.cpp -checks='-*,malterlib-coroutine-reference-parameter' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs
// RUN: clang-tidy %t-set.cpp -checks='-*,malterlib-thread-local-storage' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs
// RUN: %python %S/Inputs/summary-chain.py returned 10000 > %t-returned.cpp
// RUN: clang-tidy %t-returned.cpp -checks='-*,malterlib-coroutine-reference-parameter' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs
// RUN: %python %S/Inputs/summary-chain.py callback 10000 > %t-callback.cpp
// RUN: clang-tidy %t-callback.cpp -checks='-*,malterlib-thread-local-storage' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs
// RUN: %python %S/Inputs/summary-chain.py reads 10000 > %t-reads.cpp
// RUN: clang-tidy %t-reads.cpp -checks='-*,malterlib-coroutine-reference-parameter' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs
// RUN: %python %S/Inputs/summary-chain.py relation 10000 > %t-relation.cpp
// RUN: clang-tidy %t-relation.cpp -checks='-*,malterlib-coroutine-*,malterlib-actor-*' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs
// RUN: %python %S/Inputs/summary-chain.py cycle 1000 > %t-cycle.cpp
// RUN: clang-tidy %t-cycle.cpp -checks='-*,malterlib-no-zero-bit-scan' -warnings-as-errors='*' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs
// RUN: %python %S/Inputs/summary-chain.py alias 30000 > %t-alias.cpp
// RUN: clang-tidy %t-alias.cpp -checks='-*,malterlib-*' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck %s -check-prefix=ALIAS

// A chain of 10000 or more distinct functions of each kind of
// interprocedural summary, each giving what it is given to the next: what a
// constructor keeps of what it is given, what a setter sets, what a function
// gives back, whether a function only calls what it is given, whether a
// member function reads only its own storage, what a member function gives
// back of its object; and a chain of 30000 local pointers in a coroutine,
// each a copy of the one before, the last of which a coroutine lambda
// captures. Each check ends, and doesn't run out of stack: the summaries and
// the walks of local aliases go on on a new stack segment where the one they
// are on is nearly exhausted (withStack). The chain of awaited coroutines
// is coroutine-reference-parameter-await-chain.cpp, and other walks as deep
// are deep-walks.cpp. And 1000 helpers in one recursion, each calling the
// one before or the next, worked out in time quadratic in their number, not
// exponential, which the guarded scan of what the getter gives isn't
// reported of.

// ALIAS: warning: a TCFuture coroutine lambda uses 'p30000', which points to 'Value', after a suspension, when 'Value' may be gone
