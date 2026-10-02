// The documentation's examples are run with this after malterlib-concurrency.h
// (lbt/doc-examples.py): as the library's, a TCFuture<void> is awaited as a
// CVoidTag, and a coroutine that gives one co_returns `{}` or what awaiting
// another one gives, through a promise of its own. The lit tests don't include
// it, so the mock's own promise and awaiter stay as they are for them.

#pragma once

namespace NMib::NConcurrency
{
	struct CVoidTag
	{
	};

	template <>
	struct TCAwaiter<void>
	{
		bool await_ready() const noexcept;
		void await_suspend(std::coroutine_handle<>) const noexcept {}
		CVoidTag await_resume() const noexcept { return {}; }
	};

	struct CVoidPromise
	{
		TCFuture<void> get_return_object();
		CInitialSuspend initial_suspend();
		std::suspend_never final_suspend() noexcept;
		void return_value(CVoidTag);
		void unhandled_exception();
		CAwaiter await_transform(ECoroutineFlag);
		CCaptureExceptionScopeAwaiter await_transform(CCaptureExceptionsHelper) noexcept;
		template <typename t_CAwaitable>
		t_CAwaitable &&await_transform(t_CAwaitable &&_Awaitable)
		{
			return static_cast<t_CAwaitable &&>(_Awaitable);
		}
	};
}

namespace std
{
	template <typename ...tp_CArgs>
	struct coroutine_traits<NMib::NConcurrency::TCFuture<void>, tp_CArgs...>
	{
		using promise_type = NMib::NConcurrency::CVoidPromise;
	};
}
