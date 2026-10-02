// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-lambda-call %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

TCFuture<int> fg_Suspend();

// A warning in a template instantiation is followed by where it was
// instantiated, like clang's own diagnostics, up to the code that isn't
// instantiated: the checks decide from what templates are instantiated with,
// so without it, a warning in a template doesn't tell where it comes from.
template <typename t_CType>
TCFuture<int> fg_Inner(t_CType _Value)
{
	co_return co_await [_Value]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return _Value;
		}
		();
}

template <typename t_CType>
struct TCOuter
{
	TCFuture<int> f_Run(t_CType _Value)
	{
		return fg_Inner(_Value);
	}
};

TCFuture<int> fg_Use()
{
	TCOuter<int> Outer;
	return Outer.f_Run(1);
}

// CHECK-NOTES: :14:21: warning: a lambda coroutine that uses its captures after a suspension is called directly; {{.*}}
// CHECK-NOTES: :27:10: note: in instantiation of function template specialization 'fg_Inner<int>' requested here
// CHECK-NOTES: :34:15: note: in instantiation of member function 'TCOuter<int>::f_Run' requested here
// CHECK-NOTES: :17:14: note: a capture is used here after a suspension, or in a way that keeps a reference to it
