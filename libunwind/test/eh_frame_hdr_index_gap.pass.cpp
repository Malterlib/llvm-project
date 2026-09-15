//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// REQUIRES: target={{x86_64-.+-linux-gnu}}

// An indexed PC gap must not trigger a linear scan of an ELF .eh_frame section,
// which may have no zero terminator (including recent glibc loaders).

#include <assert.h>
#include <link.h>
#include <stdint.h>
#include <unwind.h>

static int checkSegmentEdges(dl_phdr_info *info, size_t, void *) {
  for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
    const ElfW(Phdr) &header = info->dlpi_phdr[i];
    if (header.p_type != PT_LOAD || !(header.p_flags & PF_X) || !header.p_memsz)
      continue;

    uintptr_t begin = info->dlpi_addr + header.p_vaddr;
    dwarf_eh_bases bases;
    _Unwind_Find_FDE(reinterpret_cast<void *>(begin), &bases);
    _Unwind_Find_FDE(reinterpret_cast<void *>(begin + header.p_memsz - 1), &bases);
  }
  return 0;
}

int main(int, char **) {
  dwarf_eh_bases bases;
  assert(_Unwind_Find_FDE(reinterpret_cast<void *>(&main), &bases) != nullptr);
  return dl_iterate_phdr(checkSegmentEdges, nullptr);
}
