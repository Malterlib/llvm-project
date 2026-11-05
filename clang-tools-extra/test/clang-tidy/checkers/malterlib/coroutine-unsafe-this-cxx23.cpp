// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-coroutine-unsafe-this %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

// The result of a member function with an explicit object parameter that must
// not outlive its object is checked like that of any other member function.
#define DMibResultEndsBeforeObject [[clang::annotate("malterlib-result-ends-before-object")]]
using namespace NMib::NConcurrency;

struct CLaunches
{
	DMibResultEndsBeforeObject TCUnsafeFuture<CAsyncDestroyAwaiter> f_Init(this CLaunches &_Self);
};

TCUnsafeFuture<CAsyncDestroyAwaiter> fg_ReturnsInit()
{
	CLaunches Launches;
	return Launches.f_Init();
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
}

TCUnsafeFuture<int> fg_KeepsInit()
{
	CLaunches Launches;
	auto DestroyLaunches = co_await co_await Launches.f_Init();
	co_return 0;
}

// A call of a member function of the holder with an explicit object parameter
// that refers to it is one of its member functions too.
struct CGuard
{
	void f_Clear(this CGuard &_Self);
};
struct CGuardLaunches
{
	DMibResultEndsBeforeObject CGuard f_Init();
};
void fg_ClearsGuard()
{
	CGuardLaunches Launches;
	auto Guard = Launches.f_Init();
	Guard.f_Clear();
}

// Also through a function pointer to one with an explicit object parameter.
TCUnsafeFuture<CAsyncDestroyAwaiter> fg_ReturnsThroughPointer()
{
	CLaunches Launches;
	auto const fInit = &CLaunches::f_Init;
	return fInit(Launches);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
}

// Also through the function a pointer to it gives.
TCUnsafeFuture<CAsyncDestroyAwaiter> fg_ReturnsThroughDereferencedPointer()
{
	CLaunches Launches;
	auto const fInit = &CLaunches::f_Init;
	return (*fInit)(Launches);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: the result of 'f_Init' must not outlive its object; keep it in a local declared after the object, a local, in the same block, and don't return, move or capture it [malterlib-coroutine-unsafe-this]
}

// The lambda of a guarded result given to the promise's `return_value` with
// an explicit object parameter, `return_value(this CPromise &, CGuard)`, the
// promise first, is part of it like any other.
TCFuture<int> fg_Wait();
struct CExplicitGuard
{
	template <typename t_FFunction>
	CExplicitGuard(t_FFunction &&_fFunction)
	{
	}
};
struct CExplicitPromise
{
	TCFuture<CExplicitGuard> get_return_object();
	std::suspend_never initial_suspend();
	std::suspend_never final_suspend() noexcept;
	void return_value(this CExplicitPromise &_Self, CExplicitGuard _Value);
	void unhandled_exception();
	template <typename t_CAwaitable>
	t_CAwaitable &&await_transform(t_CAwaitable &&_Awaitable)
	{
		return static_cast<t_CAwaitable &&>(_Awaitable);
	}
};
struct CGuardedLaunches;
template <>
struct std::coroutine_traits<TCFuture<CExplicitGuard>, CGuardedLaunches &>
{
	using promise_type = CExplicitPromise;
};
struct CGuardedLaunches
{
	int m_Value = 0;
	DMibResultEndsBeforeObject TCFuture<CExplicitGuard> f_Init();
};
TCFuture<CExplicitGuard> CGuardedLaunches::f_Init()
{
	co_return [this]() -> TCFuture<int>
		{
			auto Value = m_Value;
			co_await fg_Wait();
			(void)Value;
			co_return 0;
		}
	;
}

// Of a promise whose `return_value` takes a forwarding reference, as
// Malterlib's, what is returned is compared with the value type of the
// future, which it converts to: not what a factory makes that a conversion
// operator converts, but what makes the value type itself.
struct CProxyGuard
{
	template <typename t_FFunction>
	CProxyGuard(t_FFunction &&_fFunction)
	{
	}
};
struct CProxy
{
	operator CProxyGuard() const;
};
template <typename t_FFunction>
CProxy fg_MakeProxy(t_FFunction &&_fFunction)
{
	return {};
}
template <typename t_FFunction>
CProxyGuard fg_MakeProxyGuard(t_FFunction &&_fFunction)
{
	return {static_cast<t_FFunction &&>(_fFunction)};
}
struct CForwardingPromise
{
	TCFuture<CProxyGuard> get_return_object();
	std::suspend_never initial_suspend();
	std::suspend_never final_suspend() noexcept;
	template <typename tf_CValue>
	void return_value(tf_CValue &&_Value);
	void unhandled_exception();
	template <typename t_CAwaitable>
	t_CAwaitable &&await_transform(t_CAwaitable &&_Awaitable)
	{
		return static_cast<t_CAwaitable &&>(_Awaitable);
	}
};
struct CProxyLaunches;
template <>
struct std::coroutine_traits<TCFuture<CProxyGuard>, CProxyLaunches &>
{
	using promise_type = CForwardingPromise;
};
struct CProxyLaunches
{
	int m_Value = 0;
	DMibResultEndsBeforeObject TCFuture<CProxyGuard> f_InitProxy();
	DMibResultEndsBeforeObject TCFuture<CProxyGuard> f_InitGuard();
};
TCFuture<CProxyGuard> CProxyLaunches::f_InitProxy()
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: TCFuture coroutine 'f_InitProxy' is a member of 'CProxyLaunches', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CProxyLaunches' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
{
	co_return fg_MakeProxy
		(
			[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda captures 'this' of 'CProxyLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CProxyLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
			{
				auto Value = m_Value;
				co_await fg_Wait();
				(void)Value;
				co_return 0;
			}
		)
	;
}
TCFuture<CProxyGuard> CProxyLaunches::f_InitGuard()
{
	co_return fg_MakeProxyGuard
		(
			[this]() -> TCFuture<int>
			{
				auto Value = m_Value;
				co_await fg_Wait();
				(void)Value;
				co_return 0;
			}
		)
	;
}

// Also one whose parameter is deduced by value or by a reference to const.
struct CDeducedPromise
{
	TCFuture<CProxyGuard> get_return_object();
	std::suspend_never initial_suspend();
	std::suspend_never final_suspend() noexcept;
	template <typename tf_CValue>
	void return_value(tf_CValue const &_Value);
	void unhandled_exception();
	template <typename t_CAwaitable>
	t_CAwaitable &&await_transform(t_CAwaitable &&_Awaitable)
	{
		return static_cast<t_CAwaitable &&>(_Awaitable);
	}
};
struct CDeducedLaunches;
template <>
struct std::coroutine_traits<TCFuture<CProxyGuard>, CDeducedLaunches &>
{
	using promise_type = CDeducedPromise;
};
struct CDeducedLaunches
{
	int m_Value = 0;
	DMibResultEndsBeforeObject TCFuture<CProxyGuard> f_InitProxy();
};
TCFuture<CProxyGuard> CDeducedLaunches::f_InitProxy()
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: TCFuture coroutine 'f_InitProxy' is a member of 'CDeducedLaunches', which is not an actor, and uses its object after a suspension, when it may be gone; derive 'CDeducedLaunches' from CActor or CAllowUnsafeThis, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
{
	co_return fg_MakeProxy
		(
			[this]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda captures 'this' of 'CDeducedLaunches', which is not an actor, so the object may be gone when it runs or resumes; derive 'CDeducedLaunches' from CActor or CAllowUnsafeThis, keep it alive with a shared pointer in the capture, or return a TCUnsafeFuture [malterlib-coroutine-unsafe-this]
			{
				auto Value = m_Value;
				co_await fg_Wait();
				(void)Value;
				co_return 0;
			}
		)
	;
}
