//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LibraryFunctions.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/Builtins.h"
#include "llvm/ADT/StringExtras.h"

namespace clang::tidy::malterlib {

namespace {
using EUse = EArgumentUse;
// Copies the bytes of the second to the first, giving back the first:
// `memcpy`, `memmove`, `strcpy`, `strcat`.
constexpr CLibraryFunction gc_Copy{
    {EUse::Writes, EUse::Reads, EUse::None}, EUse::None, 0, false};
// The same, giving back nothing it is given, `mempcpy`, which gives back
// where it ends, or `__builtin_memcpy_inline`.
constexpr CLibraryFunction gc_CopyGivingOther{
    {EUse::Writes, EUse::Reads, EUse::None}, EUse::None, -1, false};
// Copies the bytes of the first to the second, `bcopy`.
constexpr CLibraryFunction gc_CopyBackwards{
    {EUse::Reads, EUse::Writes, EUse::None}, EUse::None, -1, false};
// Sets the bytes of the first, giving it back, `memset`.
constexpr CLibraryFunction gc_Set{
    {EUse::Writes, EUse::None, EUse::None}, EUse::None, 0, false};
// Sets the bytes of the first, `bzero`.
constexpr CLibraryFunction gc_Clear{
    {EUse::Writes, EUse::None, EUse::None}, EUse::None, -1, false};
// Reads what it is given, `memcmp`, `strlen`.
constexpr CLibraryFunction gc_Read{
    {EUse::Reads, EUse::Reads, EUse::Reads}, EUse::Reads, -1, false};
// Reads the first, giving back a place in it, `memchr`, `strchr`.
constexpr CLibraryFunction gc_Find{
    {EUse::Reads, EUse::None, EUse::None}, EUse::None, -1, false};
// Gives back what it is given, which it only looks at,
// `__builtin_launder(pValue)`, `__builtin_assume_aligned(pValue, 16)`.
constexpr CLibraryFunction gc_Given{
    {EUse::None, EUse::None, EUse::None}, EUse::None, 0, false};
// Does nothing with what it is given, a fence or a hint,
// `__atomic_signal_fence(__ATOMIC_SEQ_CST)`, `__builtin_expect(_Value, 1)`.
constexpr CLibraryFunction gc_Nothing{
    {EUse::None, EUse::None, EUse::None}, EUse::None, -1, false};
// Releases what it is given, `free`.
constexpr CLibraryFunction gc_Release{
    {EUse::Releases, EUse::None, EUse::None}, EUse::None, -1, false};
// Releases what it is given and gives back new memory, `realloc`.
constexpr CLibraryFunction gc_Reallocate{
    {EUse::Releases, EUse::None, EUse::None}, EUse::None, -1, true};
// Gives back new memory, `malloc`.
constexpr CLibraryFunction gc_Allocate{
    {EUse::None, EUse::None, EUse::None}, EUse::None, -1, true};
// Gives back the frame of the coroutine it is in, storage of its own that
// nothing it is given is, `__builtin_coro_frame()`.
constexpr CLibraryFunction gc_Frame{
    {EUse::None, EUse::None, EUse::None}, EUse::None, -1, true};
// Gives back new memory with a copy of what it reads, `strdup`.
constexpr CLibraryFunction gc_Duplicate{
    {EUse::Reads, EUse::None, EUse::None}, EUse::None, -1, true};
} // namespace

const CLibraryFunction *libraryFunction(const FunctionDecl *Function) {
  if (!Function)
    return nullptr;
  const unsigned ID = Function->getBuiltinID();
  if (!ID)
    return nullptr;
  switch (ID) {
  case Builtin::BImemcpy:
  case Builtin::BI__builtin_memcpy:
  case Builtin::BI__builtin___memcpy_chk:
  case Builtin::BImemmove:
  case Builtin::BI__builtin_memmove:
  case Builtin::BI__builtin___memmove_chk:
  case Builtin::BIwmemcpy:
  case Builtin::BI__builtin_wmemcpy:
  case Builtin::BIwmemmove:
  case Builtin::BI__builtin_wmemmove:
  case Builtin::BIstrcpy:
  case Builtin::BI__builtin_strcpy:
  case Builtin::BIstrncpy:
  case Builtin::BI__builtin_strncpy:
  case Builtin::BIstrcat:
  case Builtin::BI__builtin_strcat:
  case Builtin::BIstrncat:
  case Builtin::BI__builtin_strncat:
    return &gc_Copy;
  case Builtin::BI__builtin_memcpy_inline:
  case Builtin::BImempcpy:
  case Builtin::BI__builtin_mempcpy:
    return &gc_CopyGivingOther;
  case Builtin::BIbcopy:
  case Builtin::BI__builtin_bcopy:
    return &gc_CopyBackwards;
  case Builtin::BImemset:
  case Builtin::BI__builtin_memset:
  case Builtin::BI__builtin___memset_chk:
    return &gc_Set;
  case Builtin::BI__builtin_memset_inline:
  case Builtin::BIbzero:
  case Builtin::BI__builtin_bzero:
    return &gc_Clear;
  case Builtin::BImemcmp:
  case Builtin::BI__builtin_memcmp:
  case Builtin::BIbcmp:
  case Builtin::BI__builtin_bcmp:
  case Builtin::BIwmemcmp:
  case Builtin::BI__builtin_wmemcmp:
  case Builtin::BIstrlen:
  case Builtin::BI__builtin_strlen:
  case Builtin::BIwcslen:
  case Builtin::BI__builtin_wcslen:
  case Builtin::BIstrcmp:
  case Builtin::BI__builtin_strcmp:
  case Builtin::BIstrncmp:
  case Builtin::BI__builtin_strncmp:
  case Builtin::BIwcscmp:
  case Builtin::BI__builtin_wcscmp:
  case Builtin::BIwcsncmp:
  case Builtin::BI__builtin_wcsncmp:
    return &gc_Read;
  case Builtin::BImemchr:
  case Builtin::BI__builtin_memchr:
  case Builtin::BIwmemchr:
  case Builtin::BI__builtin_wmemchr:
  case Builtin::BIstrchr:
  case Builtin::BI__builtin_strchr:
  case Builtin::BIstrrchr:
  case Builtin::BI__builtin_strrchr:
    return &gc_Find;
  case Builtin::BI__builtin_launder:
  case Builtin::BI__builtin_assume_aligned:
  case Builtin::BI__builtin_align_up:
  case Builtin::BI__builtin_align_down:
    return &gc_Given;
  case Builtin::BI__atomic_signal_fence:
  case Builtin::BI__atomic_thread_fence:
  case Builtin::BI__c11_atomic_signal_fence:
  case Builtin::BI__c11_atomic_thread_fence:
  case Builtin::BI__builtin_assume:
  case Builtin::BI__assume:
  case Builtin::BI__builtin_expect:
  case Builtin::BI__builtin_expect_with_probability:
  case Builtin::BI__builtin_unpredictable:
  case Builtin::BI__builtin_prefetch:
  case Builtin::BI__builtin_trap:
  case Builtin::BI__builtin_debugtrap:
  case Builtin::BI__builtin_unreachable:
    return &gc_Nothing;
  case Builtin::BIfree:
  case Builtin::BI__builtin_operator_delete:
    return &gc_Release;
  case Builtin::BIrealloc:
    return &gc_Reallocate;
  case Builtin::BImalloc:
  case Builtin::BIcalloc:
  case Builtin::BI__builtin_operator_new:
    return &gc_Allocate;
  case Builtin::BIstrdup:
  case Builtin::BIstrndup:
    return &gc_Duplicate;
  case Builtin::BI__builtin_coro_frame:
    return &gc_Frame;
  default:
    break;
  }
  const Builtin::Context &Builtins = Function->getASTContext().BuiltinInfo;
  // The compiler barriers of Microsoft's targets, builtins of each target,
  // which only keep the compiler from moving memory accesses across them.
  if (Builtins.isTSBuiltin(ID)) {
    const std::string Name = Builtins.getName(ID);
    if (Name == "_ReadWriteBarrier" || Name == "_ReadBarrier" ||
        Name == "_WriteBarrier")
      return &gc_Nothing;
  }
  // Any other that has no side effects reads at most what it is given.
  if (Builtins.isConst(ID) || Builtins.isPure(ID))
    return &gc_Read;
  return nullptr;
}

std::optional<EArgumentUse> libraryArgumentUse(const FunctionDecl *Function,
                                               unsigned Index) {
  const CLibraryFunction *Library = libraryFunction(Function);
  if (!Library)
    return std::nullopt;
  return Index < std::size(Library->Arguments) ? Library->Arguments[Index]
                                               : Library->Rest;
}

bool asmDoesNothing(const AsmStmt *Asm) {
  // What it can change it reaches through its operands or, of a GNU one, a
  // `memory` clobber, which says it reads and writes any memory.
  const bool Operands = Asm->getNumOutputs() != 0 || Asm->getNumInputs() != 0;
  if (const auto *Gcc = dyn_cast<GCCAsmStmt>(Asm)) {
    bool Memory = false;
    for (unsigned iClobber = 0; iClobber < Gcc->getNumClobbers(); ++iClobber)
      Memory = Memory || Gcc->getClobber(iClobber) == "memory";
    return llvm::StringRef(Gcc->getAsmString()).trim().empty() ||
           (!Operands && !Memory);
  }
  if (const auto *Microsoft = dyn_cast<MSAsmStmt>(Asm))
    return Microsoft->getAsmString().trim().empty() || !Operands;
  return false;
}

} // namespace clang::tidy::malterlib
