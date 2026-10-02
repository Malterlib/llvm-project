// REQUIRES: malterlib-plugin
// UNSUPPORTED: system-windows

// A `.clang-tidy` that exists and can't be read as a configuration file fails
// the compilation: also one whose status can't be read for another reason than
// that it doesn't exist, a symbolic link to itself, and one that isn't a
// regular file, a directory or a pipe. One that doesn't exist, also a dangling
// symbolic link, which can't be told from a file that doesn't exist, continues
// the search in the parent directory, as does an empty one.
// RUN: rm -rf %t && mkdir -p %t/parent/loop %t/parent/directory/.clang-tidy %t/parent/pipe %t/parent/dangling %t/parent/empty
// RUN: printf "Checks: '-*,malterlib-function-naming'\nCheckOptions:\n  malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'\n" > %t/parent/.clang-tidy
// RUN: ln -s .clang-tidy %t/parent/loop/.clang-tidy
// RUN: mkfifo %t/parent/pipe/.clang-tidy
// RUN: ln -s missing.yaml %t/parent/dangling/.clang-tidy
// RUN: printf '' > %t/parent/empty/.clang-tidy
// RUN: printf 'void Bad();\n' > %t/parent/loop/main.cpp
// RUN: printf 'void Bad();\n' > %t/parent/directory/main.cpp
// RUN: printf 'void Bad();\n' > %t/parent/pipe/main.cpp
// RUN: printf 'void Bad();\n' > %t/parent/dangling/main.cpp
// RUN: printf 'void Bad();\n' > %t/parent/empty/main.cpp
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-enable %t/parent/loop/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-LOOP %s
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-enable %t/parent/directory/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-DIRECTORY %s
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-enable %t/parent/pipe/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-PIPE %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-enable %t/parent/dangling/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-PARENT --implicit-check-not=error: %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-enable %t/parent/empty/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-PARENT --implicit-check-not=error: %s
// Plain clang-tidy prints the error and goes on, as it does for one it can't
// read.
// RUN: clang-tidy %t/parent/loop/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-TIDY %s

// CHECK-LOOP: error: cannot read the malterlib plugin configuration '{{.*}}loop{{/|\\}}.clang-tidy': {{[Tt]}}oo many levels of symbolic links
// CHECK-DIRECTORY: error: cannot read the malterlib plugin configuration '{{.*}}directory{{/|\\}}.clang-tidy': {{[Ii]}}s a directory
// CHECK-PIPE: error: cannot read the malterlib plugin configuration '{{.*}}pipe{{/|\\}}.clang-tidy': {{.+}}
// CHECK-PARENT: warning: global function 'Bad' should have the 'fg_' prefix [malterlib-function-naming]
// CHECK-TIDY: Can't read {{.*}}loop{{/|\\}}.clang-tidy: {{[Tt]}}oo many levels of symbolic links
// CHECK-TIDY: warning: global function 'Bad' should have the 'fg_' prefix [malterlib-function-naming]
