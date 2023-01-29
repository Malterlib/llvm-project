// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP__NEW_NEW
#define _LIBCPP__NEW_NEW

#include <__availability>
#include <__config>

namespace std  // purposefully not using versioning namespace
{

#if !defined(_LIBCPP_ABI_VCRUNTIME)
struct _LIBCPP_EXPORTED_FROM_ABI nothrow_t {
  explicit nothrow_t() = default;
};
extern _LIBCPP_EXPORTED_FROM_ABI const nothrow_t nothrow;
#endif

#if !defined(_LIBCPP_HAS_NO_LIBRARY_ALIGNED_ALLOCATION) && !defined(_LIBCPP_ABI_VCRUNTIME)
#  ifndef _LIBCPP_CXX03_LANG
enum class align_val_t : size_t {};
#  else
enum align_val_t { __zero = 0, __max = (size_t)-1 };
#  endif
#endif

#if _LIBCPP_STD_VER >= 20
// Enable the declaration even if the compiler doesn't support the language
// feature.
struct destroying_delete_t {
  explicit destroying_delete_t() = default;
};
inline constexpr destroying_delete_t destroying_delete{};
#endif // _LIBCPP_STD_VER >= 20

}

#endif // _LIBCPP__NEW_NEW
