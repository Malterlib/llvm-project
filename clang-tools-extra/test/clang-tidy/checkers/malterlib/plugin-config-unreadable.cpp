// REQUIRES: malterlib-plugin, non-root-user
// UNSUPPORTED: system-windows

// A configuration file that can't be read is an error of the compilation, also
// one a configuration given as an argument inherits.
// RUN: rm -rf %t && mkdir -p %t/project && printf "Checks: '-*,malterlib-function-naming'\n" > %t/project/.clang-tidy && chmod 000 %t/project/.clang-tidy && printf 'void fg_Ok();\n' > %t/project/main.cpp
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-enable %t/project/main.cpp 2>&1 | FileCheck %s
// RUN: printf 'InheritParentConfig: true\n' > %t/config.yaml
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-config-file=%t/config.yaml %t/project/main.cpp 2>&1 | FileCheck %s
// RUN: chmod 644 %t/project/.clang-tidy
// CHECK: error: cannot read the malterlib plugin configuration '{{.*}}project{{/|\\}}.clang-tidy':

// Also one whose status can't be read: a symbolic link to a file in a directory
// that can't be searched.
// RUN: chmod -R u+rwx %t.locked || true
// RUN: rm -rf %t.locked && mkdir -p %t.locked/project %t.locked/locked && printf "Checks: '-*,malterlib-function-naming'\n" > %t.locked/locked/rules.yaml && ln -s ../locked/rules.yaml %t.locked/project/.clang-tidy && printf 'void fg_Ok();\n' > %t.locked/project/main.cpp && chmod 000 %t.locked/locked
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-enable %t.locked/project/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-LOCKED %s
// RUN: chmod 755 %t.locked/locked
// CHECK-LOCKED: error: cannot read the malterlib plugin configuration '{{.*}}project{{/|\\}}.clang-tidy': {{[Pp]}}ermission denied
