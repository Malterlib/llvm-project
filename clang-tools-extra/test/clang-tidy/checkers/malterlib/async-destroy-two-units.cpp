// UNSUPPORTED: system-windows
// RUN: rm -rf %t && mkdir -p %t
// RUN: cp %S/actor-dispatch-this.cpp %t/first.cpp
// RUN: cp %S/async-destroy.cpp %t/second.cpp
// RUN: cp %S/actor-dispatch-this.cpp %t/third.cpp
// RUN: clang-tidy %t/first.cpp '-checks=-*,malterlib-async-destroy,malterlib-actor-dispatch-this' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs 2>&1 | grep 'warning:' | sed 's|.*/||' > %t/alone.txt
// RUN: clang-tidy %t/second.cpp '-checks=-*,malterlib-async-destroy,malterlib-actor-dispatch-this' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs 2>&1 | grep 'warning:' | sed 's|.*/||' >> %t/alone.txt
// RUN: clang-tidy %t/third.cpp '-checks=-*,malterlib-async-destroy,malterlib-actor-dispatch-this' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs 2>&1 | grep 'warning:' | sed 's|.*/||' >> %t/alone.txt
// RUN: clang-tidy %t/first.cpp %t/second.cpp %t/third.cpp '-checks=-*,malterlib-async-destroy,malterlib-actor-dispatch-this' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 -I %S/Inputs 2>&1 | grep 'warning:' | sed 's|.*/||' > %t/together.txt
// RUN: diff %t/alone.txt %t/together.txt
// RUN: grep -q 'first.cpp:.*malterlib-actor-dispatch-this' %t/together.txt
// RUN: grep -q 'third.cpp:.*malterlib-actor-dispatch-this' %t/together.txt

// What the checks keep of a translation unit is of that one only: a check
// that runs walks before its first match of the next one, like the matcher
// of async-destroy, has no context of the one before (setWalkContext), nor
// actor-dispatch-this the root context of a match of the unit before
// (s_RootContext), which the first and the third unit set, each with the
// unit of async-destroy between; units checked in one run give what each
// gives alone.
