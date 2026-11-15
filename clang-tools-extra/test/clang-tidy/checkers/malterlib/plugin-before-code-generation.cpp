// The checks of the plugin run before the compile generates code: what they
// report comes before what generating the code reports, and an error of a
// check stops the compile before it generates code.
// REQUIRES: malterlib-plugin
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -c -Wframe-larger-than=1 -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' %s -o %t.o 2>&1 | FileCheck %s
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -c -Wframe-larger-than=1 -fplugin-arg-malterlib-checks='-*,malterlib-function-naming' -fplugin-arg-malterlib-warnings-as-errors='malterlib-*' %s -o %t.error.o 2>&1 | FileCheck --check-prefix=CHECK-ERROR --implicit-check-not='stack frame size' %s

void fg_Use(char *_pBuffer);

// CHECK: :[[@LINE+3]]:6: warning: global function 'Bad' should have the 'fg_' prefix [malterlib-function-naming]
// CHECK: warning: stack frame size
// CHECK-ERROR: :[[@LINE+1]]:6: error: global function 'Bad' should have the 'fg_' prefix [malterlib-function-naming]
void Bad() {
  char Buffer[256];
  fg_Use(Buffer);
}
