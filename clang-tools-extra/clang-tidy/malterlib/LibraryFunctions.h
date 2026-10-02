//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_LIBRARYFUNCTIONS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_LIBRARYFUNCTIONS_H

#include <cstdint>
#include <optional>

namespace clang {
class AsmStmt;
class FunctionDecl;
} // namespace clang

namespace clang::tidy::malterlib {

/// What a library function does with what an argument points to.
enum class EArgumentUse : uint8_t {
  /// Nothing, `__builtin_expect(_Value, 1)`, or the argument isn't a pointer.
  None,
  /// Reads it, `memcmp(_pLeft, _pRight, _Size)`.
  Reads,
  /// Writes it, and nothing beyond it, `memset(_pData, 0, _Size)`.
  Writes,
  /// Releases it, `free(_pData)`.
  Releases,
};

/// What a function the compiler knows as a builtin does, which has no body
/// to look into: with each argument, and what it gives back. It keeps
/// nothing it is given.
struct CLibraryFunction {
  /// Of the first arguments, by position.
  EArgumentUse Arguments[3];
  /// Of the arguments after them.
  EArgumentUse Rest;
  /// The argument it gives back as it is given, `memcpy` its destination,
  /// or -1.
  int Returns;
  /// Whether it gives back storage of its own that nothing it is given is:
  /// memory it allocates, `malloc`, or the frame of the coroutine it is in.
  bool New;
};

/// What \p Function does, one table for every check that asks what a
/// function without a body does: by its builtin ID, `memset`, also
/// `std::memset` and `__builtin_memset`, the memory, string, allocation and
/// fence functions, and of any other builtin clang knows to have no side
/// effects, const or pure, reading what it is given. Null for other
/// functions, whose bodies tell, or which can do anything when unseen.
const CLibraryFunction *libraryFunction(const FunctionDecl *Function);

/// What \p Function does with the argument at \p Index (libraryFunction),
/// or none when the table doesn't know it.
std::optional<EArgumentUse> libraryArgumentUse(const FunctionDecl *Function,
                                               unsigned Index);

/// Whether \p Asm does nothing to what a program keeps when it runs: its
/// template is empty, `__asm__ __volatile__("" ::: "memory")`, which only
/// keeps the compiler from moving memory accesses across it, or it has no
/// operands nor a `memory` clobber, `__asm__("int $3")`, a breakpoint, which
/// reaches nothing of it. Any other can change what its operands give it,
/// and with a `memory` clobber anything.
bool asmDoesNothing(const AsmStmt *Asm);

} // namespace clang::tidy::malterlib

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MALTERLIB_LIBRARYFUNCTIONS_H
