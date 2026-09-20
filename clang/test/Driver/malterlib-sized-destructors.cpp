// RUN: %clang -### --target=x86_64-linux-gnu -fmalterlib-sized-destructors -c %s 2>&1 | FileCheck %s
// RUN: %clang -### --target=i386-linux-gnu -fmalterlib-sized-destructors -c %s 2>&1 | FileCheck %s
// RUN: %clang -### --target=aarch64-apple-macosx -fmalterlib-sized-destructors -c %s 2>&1 | FileCheck %s
// RUN: %clang -### --target=arm-linux-gnueabihf -fmalterlib-sized-destructors -c %s 2>&1 | FileCheck %s
// RUN: %clang -### --target=x86_64-windows-msvc -fmalterlib-sized-destructors -c %s 2>&1 | FileCheck %s
// CHECK: "-fmalterlib-sized-destructors"

// A sized deleting destructor returns a pointer and a size in two registers.
// A target whose ABI returns such a struct otherwise cannot stay compatible
// with code compiled without the flag.
// RUN: not %clang -### --target=wasm32-unknown-unknown -fmalterlib-sized-destructors -c %s 2>&1 | FileCheck %s --check-prefix=WASM
// WASM: error: unsupported option '-fmalterlib-sized-destructors' for target 'wasm32-unknown-unknown'
// RUN: not %clang -### --target=arm64ec-windows-msvc -fmalterlib-sized-destructors -c %s 2>&1 | FileCheck %s --check-prefix=ARM64EC
// ARM64EC: error: unsupported option '-fmalterlib-sized-destructors' for target 'arm64ec-{{.*}}windows-msvc{{.*}}'
// RUN: %clang -### --target=wasm32-unknown-unknown -fmalterlib-sized-destructors -fno-malterlib-sized-destructors -c %s 2>&1 | FileCheck %s --check-prefix=WASM-OFF
// WASM-OFF-NOT: error:
// WASM-OFF: "-fno-malterlib-sized-destructors"
