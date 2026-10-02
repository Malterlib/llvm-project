// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-unused-parameter-cast %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

// What a `co_return` of a coroutine returns, like a `return` of a function.
struct CTask
{
	struct promise_type
	{
		CTask get_return_object();
		std::suspend_never initial_suspend();
		std::suspend_never final_suspend() noexcept;
		void return_void();
		void unhandled_exception();
	};
};

CTask fg_Unused(int _Value)
{
	co_return (void)_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: parameter '_Value' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
}

namespace NRound138 {
// In the initializer of a `switch`.
void fg_Switch(int _Unused, unsigned _Bits) {
  switch ((void)_Unused; _Bits) {
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: parameter '_Unused' is silenced with a cast to void; leave an unused parameter named, without the cast [malterlib-unused-parameter-cast]
  default:
    break;
  }
}
}
