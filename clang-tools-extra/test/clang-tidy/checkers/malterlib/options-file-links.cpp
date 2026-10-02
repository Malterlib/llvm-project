// Symbolic links, which Git's ln for Windows copies, and where Windows
// resolves `..` before following a link.
// UNSUPPORTED: system-windows
// The options file of a configuration (options-file.cpp) through a symbolic
// link.
// RUN: rm -rf %t && mkdir -p %t

// A `..` after a symbolic link is in the directory the link is to.
// RUN: mkdir -p %t/linked/target/child %t/linked/config
// RUN: ln -s ../target/child %t/linked/config/alias
// RUN: printf 'LowerCaseFreeFunctions: clear\n' > %t/linked/target/options.yaml
// RUN: printf "Checks: '-*,malterlib-function-naming'\nCheckOptions:\n  malterlib.OptionsFile: alias/../options.yaml\n" > %t/linked/config/.clang-tidy
// RUN: printf 'void clear();\n' > %t/linked/config/main.cpp
// RUN: clang-tidy %t/linked/config/main.cpp -- 2>&1 | FileCheck --check-prefix=CHECK-LINKED --allow-empty %s
// RUN: %clang -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-options-file=%t/linked/config/alias/../options.yaml %t/linked/config/main.cpp 2>&1 | FileCheck --check-prefix=CHECK-LINKED --allow-empty %s
// CHECK-LINKED-NOT: {{error|warning}}:
