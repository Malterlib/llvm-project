// REQUIRES: malterlib-plugin
// The plugin selects files by the wildcards of the repositories as mib tidy
// does, by Malterlib's fg_StrMatchWildcard, row by row of the table.
// RUN: rm -rf %t && mkdir -p %t
// RUN: %python %S/Inputs/plugin-wildcards.py %S/Inputs/plugin-wildcards.tsv %t
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -fsyntax-only -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-repositories=%t/rules.json %t/main.cpp > %t/output.txt 2>&1
// RUN: FileCheck %t/matched.txt < %t/output.txt
// RUN: FileCheck %t/unmatched.txt < %t/output.txt
