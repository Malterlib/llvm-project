# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Structure

This is the LLVM Project monorepo containing multiple subprojects:

- **llvm/**: Core LLVM libraries, tools, and infrastructure
- **clang/**: C/C++/Objective-C compiler frontend
- **clang-tools-extra/**: Additional Clang tools (clangd, clang-tidy, etc.)
- **lld/**: LLVM linker
- **lldb/**: LLVM debugger
- **compiler-rt/**: Runtime libraries (sanitizers, profiling, etc.)
- **libcxx/**: C++ standard library implementation
- **libcxxabi/**: C++ ABI library
- **libunwind/**: Stack unwinding library
- **libc/**: LLVM's C library implementation
- **flang/**: Fortran compiler frontend
- **mlir/**: Multi-Level Intermediate Representation infrastructure
- **openmp/**: OpenMP runtime library
- **polly/**: Loop optimization framework
- **bolt/**: Binary optimization and layout tool
- **cross-project-tests/**: Integration tests across subprojects

## Build System

LLVM uses CMake as its primary build system. The main configuration happens at the top level.

### Basic Build Commands

```bash
# Configure build (from repo root)
cmake -S llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Build specific target
cmake --build build --target clang

# Install
cmake --build build --target install
```

### Common CMake Options

- `-DLLVM_ENABLE_PROJECTS="clang;lld;clang-tools-extra"` - Enable additional subprojects
- `-DCMAKE_BUILD_TYPE=Debug|Release|RelWithDebInfo|MinSizeRel` - Build type
- `-DLLVM_ENABLE_ASSERTIONS=ON` - Enable assertions (default ON for Debug)
- `-DLLVM_USE_LINKER=lld` - Use LLD linker for faster linking
- `-DLLVM_PARALLEL_COMPILE_JOBS=N` - Limit parallel compilation jobs
- `-DLLVM_TARGETS_TO_BUILD="X86;AArch64"` - Build only specific targets

### Testing

```bash
# Run all LLVM tests
cmake --build build --target check-llvm

# Run Clang tests
cmake --build build --target check-clang

# Run specific test suite
llvm-lit build/test/Analysis

# Run single test file
llvm-lit path/to/test.ll
```

## Development Workflow

### Code Organization

- **include/**: Public headers for each subproject
- **lib/**: Implementation files
- **tools/**: Standalone tools and drivers
- **test/**: Test files (uses lit testing framework)
- **unittests/**: Unit tests (uses Google Test)
- **docs/**: Documentation (Sphinx/RST format)

### Key Architecture Concepts

- **LLVM IR**: Intermediate representation - the core data structure
- **Pass System**: Modular optimization and analysis framework
- **TableGen**: Domain-specific language for code generation
- **Target Description**: Architecture-specific code generation rules
- **Analysis/Transform Passes**: Optimization and analysis components

### Important File Patterns

- `*.td` - TableGen files (target descriptions, intrinsics, etc.)
- `*.ll` - LLVM IR files (often test cases)
- `*.inc` - Generated include files (usually from TableGen)
- `lit.cfg.py` - Lit test configuration files
- `CMakeLists.txt` - Build configuration files

### Testing Framework

LLVM uses the `lit` (LLVM Integrated Tester) framework:
- Test files typically have `.ll`, `.c`, `.cpp` extensions
- Tests use `RUN:` lines with FileCheck for verification
- `FileCheck` pattern matching for output validation

### Common Development Tasks

When working on LLVM code:

1. **Format code**: Use clang-format (usually configured in .clang-format)
2. **Run tests**: Always run relevant test suites after changes
3. **Update tests**: Add or modify tests when changing functionality
4. **Check build**: Ensure clean builds across different configurations
5. **Documentation**: Update relevant .rst files for API changes

### Debugging and Analysis

- Use `llvm-dis` to convert bitcode to readable IR
- Use `opt` to run optimization passes on IR
- Use `llc` to compile IR to assembly
- Enable debug builds with assertions for development
- Use sanitizers (AddressSanitizer, etc.) built into compiler-rt

This is a complex, mature codebase with intricate interdependencies. When making changes, consider the impact across multiple subprojects and target architectures.