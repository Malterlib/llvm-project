// REQUIRES: malterlib-plugin
// RUN: clang-tidy --checks='-*,malterlib-coroutine-reference-parameter' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 2>&1 | FileCheck --implicit-check-not='warning:' %s
// RUN: clang-tidy --checks='-*,malterlib-coroutine-reference-parameter' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -fconstexpr-steps=0 2>&1 | FileCheck --implicit-check-not='warning:' %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -fconstexpr-steps=0 -fplugin-arg-malterlib-checks='-*,malterlib-coroutine-reference-parameter' %s 2>&1 | FileCheck --implicit-check-not='warning:' %s

// The value an awaiter's await_ready returns is read only when clang stored it
// or it runs nothing (round 237): a constexpr call, which clang never
// evaluated there and which with `-fconstexpr-steps=0` never ends, like
// fg_Ready, can't be told, so the awaiter is taken to be able to suspend.

namespace std
{
	template <class T = void>
	struct coroutine_handle
	{
		static coroutine_handle from_address(void *) noexcept;
		template <class U>
		operator coroutine_handle<U>() const noexcept;
	};
	template <class R, class... A>
	struct coroutine_traits
	{
		using promise_type = typename R::promise_type;
	};
	struct suspend_never
	{
		bool await_ready() noexcept { return true; }
		void await_suspend(coroutine_handle<>) noexcept;
		void await_resume() noexcept;
	};
}

template <class T>
struct TCFuture
{
	struct promise_type
	{
		TCFuture get_return_object();
		std::suspend_never initial_suspend();
		std::suspend_never final_suspend() noexcept;
		void return_value(T);
		void unhandled_exception();
	};
};

constexpr bool fg_Ready(bool _bLoop)
{
	while (_bLoop)
	{
	}
	return true;
}

struct CCallReady
{
	bool await_ready() { return fg_Ready(true); }
	void await_suspend(std::coroutine_handle<>) {}
	void await_resume() {}
};

TCFuture<int> fg_CallReady(int &_Value)
// CHECK: :[[@LINE-1]]:33: warning: TCFuture coroutine 'fg_CallReady' uses '_Value' after a suspension, but it takes it as 'int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	co_await CCallReady{};
	co_return _Value;
}

// A literal is folded: this awaiter never suspends.
struct CLiteralReady
{
	bool await_ready() { return true; }
	void await_suspend(std::coroutine_handle<>) {}
	void await_resume() {}
};

TCFuture<int> fg_LiteralReady(int &_Value)
{
	co_await CLiteralReady{};
	co_return _Value;
}
