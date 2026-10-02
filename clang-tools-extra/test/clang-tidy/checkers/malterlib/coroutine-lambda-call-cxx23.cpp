// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-coroutine-lambda-call %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

TCFuture<int> fg_Suspend();

TCFuture<int> fg_ExplicitObject(int _Value)
{
	// A closure taken by value lives in the coroutine frame.
	co_await [_Value](this auto _Self) -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
		()
	;

	co_await [_Value](this auto const &_Self) -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a lambda coroutine that uses its captures after a suspension is called directly; the captures dangle when the closure is a temporary, so run it through fg_CallSafe, g_Dispatch, 'self /' or an actor functor [malterlib-coroutine-lambda-call]
		{
			co_await fg_Suspend();
			co_return _Value;
		}
		()
	;

	co_return 0;
}

// One that takes its closure by value keeps its captures while it runs, but
// not for what its result refers to.
int fg_ByValueSelfResult(int _Value)
{
	auto fTask = [_Value](this auto _Self) -> TCFuture<int const *>
		{
			co_return &_Value;
		}
	;
	int const *pValue = fTask().f_CallSync();
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: a lambda coroutine returns what refers to its captures, which are gone once its frame is; return a copy [malterlib-coroutine-lambda-call]
	return *pValue;
}

// An explicit call of the call operator of a lambda that takes its closure by
// value, like a call of it, returns what refers to its frame.
int fg_Round67Explicit(int _Value)
{
	auto Task = [_Value](this auto _Self) -> TCFuture<int const *>
		{
			co_return &_Value;
		}
	;
	auto *pValue = Task.operator()().f_CallSync();
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a lambda coroutine returns what refers to its captures, which are gone once its frame is; return a copy [malterlib-coroutine-lambda-call]
	return *pValue;
}

// The copy of the closure that an explicit object parameter taken by value
// is lives in the frame, which is gone when the result is used.
void *fg_Round83Self()
{
	auto fTask = [Value = 1](this auto _Self) -> TCFuture<void *>
		{
			co_return static_cast<void *>(&_Self);
		}
	;
	return fTask().f_CallSync();
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a lambda coroutine returns what refers to its captures, which are gone once its frame is; return a copy [malterlib-coroutine-lambda-call]
}

// Round 113: a direct call of an explicit object lambda is checked by its specialization
namespace NRound113ExplicitObject
{
template<class T>
void const *address(T const &v) { return &v; }
void const *f() {
  auto task = [value = 1](this auto self) -> TCFuture<void const *> {
    co_return address(self);
  };
  return task().f_CallSync();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: a lambda coroutine returns what refers to its captures, which are gone once its frame is; return a copy [malterlib-coroutine-lambda-call]
}
}
