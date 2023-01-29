// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___EXCEPTION_EXCEPTION_PTR_H
#define _LIBCPP___EXCEPTION_EXCEPTION_PTR_H

#include <__availability>
#include <__config>
#include <stddef.h>

#ifdef _LIBCPP_NO_EXCEPTIONS
#include <cstdlib>
#endif

#if defined(_LIBCPP_ABI_VCRUNTIME)
#include <vcruntime_exception.h>
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

namespace std  // purposefully not using versioning namespace
{

_LIBCPP_FUNC_VIS bool uncaught_exception() _NOEXCEPT;
_LIBCPP_FUNC_VIS _LIBCPP_AVAILABILITY_UNCAUGHT_EXCEPTIONS int uncaught_exceptions() _NOEXCEPT;

class _LIBCPP_TYPE_VIS exception_ptr;

_LIBCPP_FUNC_VIS exception_ptr current_exception() _NOEXCEPT;
_LIBCPP_NORETURN _LIBCPP_FUNC_VIS void rethrow_exception(exception_ptr);

#ifndef _LIBCPP_ABI_MICROSOFT

class _LIBCPP_TYPE_VIS exception_ptr
{
    void* __ptr_;
public:
    _LIBCPP_INLINE_VISIBILITY exception_ptr() _NOEXCEPT : __ptr_() {}
    _LIBCPP_INLINE_VISIBILITY exception_ptr(nullptr_t) _NOEXCEPT : __ptr_() {}

    exception_ptr(const exception_ptr&) _NOEXCEPT;
    exception_ptr& operator=(const exception_ptr&) _NOEXCEPT;
    ~exception_ptr() _NOEXCEPT;

    _LIBCPP_INLINE_VISIBILITY explicit operator bool() const _NOEXCEPT
    {return __ptr_ != nullptr;}

    friend _LIBCPP_INLINE_VISIBILITY
    bool operator==(const exception_ptr& __x, const exception_ptr& __y) _NOEXCEPT
        {return __x.__ptr_ == __y.__ptr_;}

    friend _LIBCPP_INLINE_VISIBILITY
    bool operator!=(const exception_ptr& __x, const exception_ptr& __y) _NOEXCEPT
        {return !(__x == __y);}

    friend _LIBCPP_FUNC_VIS exception_ptr current_exception() _NOEXCEPT;
    friend _LIBCPP_FUNC_VIS void rethrow_exception(exception_ptr);
};

template<class _Ep>
_LIBCPP_INLINE_VISIBILITY exception_ptr
make_exception_ptr(_Ep __e) _NOEXCEPT
{
#ifndef _LIBCPP_NO_EXCEPTIONS
    try
    {
        throw __e;
    }
    catch (...)
    {
        return current_exception();
    }
#else
    ((void)__e);
    _VSTD::abort();
#endif
}

#else // _LIBCPP_ABI_MICROSOFT

class _LIBCPP_TYPE_VIS exception_ptr
{
_LIBCPP_DIAGNOSTIC_PUSH
_LIBCPP_CLANG_DIAGNOSTIC_IGNORED("-Wunused-private-field")
    void* __ptr1_;
    void* __ptr2_;
_LIBCPP_DIAGNOSTIC_POP
public:
    exception_ptr() _NOEXCEPT;
    exception_ptr(nullptr_t) _NOEXCEPT;
    exception_ptr(const exception_ptr& __other) _NOEXCEPT;
    exception_ptr& operator=(const exception_ptr& __other) _NOEXCEPT;
    exception_ptr& operator=(nullptr_t) _NOEXCEPT;
    ~exception_ptr() _NOEXCEPT;
    explicit operator bool() const _NOEXCEPT;
};

_LIBCPP_FUNC_VIS
bool operator==(const exception_ptr& __x, const exception_ptr& __y) _NOEXCEPT;

inline _LIBCPP_INLINE_VISIBILITY
bool operator!=(const exception_ptr& __x, const exception_ptr& __y) _NOEXCEPT
    {return !(__x == __y);}

_LIBCPP_FUNC_VIS void swap(exception_ptr&, exception_ptr&) _NOEXCEPT;

_LIBCPP_FUNC_VIS exception_ptr __copy_exception_ptr(void *__except, const void* __ptr);
_LIBCPP_FUNC_VIS exception_ptr current_exception() _NOEXCEPT;
_LIBCPP_NORETURN _LIBCPP_FUNC_VIS void rethrow_exception(exception_ptr);

// This is a built-in template function which automagically extracts the required
// information.
template <class _E> void *__GetExceptionInfo(_E);

template<class _Ep>
_LIBCPP_INLINE_VISIBILITY exception_ptr
make_exception_ptr(_Ep __e) _NOEXCEPT
{
  return __copy_exception_ptr(_VSTD::addressof(__e), __GetExceptionInfo(__e));
}

#endif // _LIBCPP_ABI_MICROSOFT
} // namespace std

#endif // _LIBCPP___EXCEPTION_EXCEPTION_PTR_H
