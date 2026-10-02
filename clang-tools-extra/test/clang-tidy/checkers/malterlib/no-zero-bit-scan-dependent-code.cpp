// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-no-zero-bit-scan %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

// Initializers without a type in the dependent code a scan's function holds:
// a parenthesized initializer in a generic lambda that isn't called is a
// ParenListExpr, which the checks read as nothing to tell.

#include "malterlib-concurrency.h"

unsigned fg_GetHighestBitSetNoZero(unsigned _Value);

void fg_ParenInit(unsigned _Bits)
{
	auto fMake = [](auto _Value)
	{
		decltype(_Value) Made(1, 2);
		decltype(_Value) Braced{_Value, 1};
		(void)Made;
		(void)Braced;
	};
	(void)fMake;
	if (_Bits)
		(void)fg_GetHighestBitSetNoZero(_Bits);
	(void)fg_GetHighestBitSetNoZero(_Bits);
	// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetHighestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}

// The promise of a generic coroutine lambda with a parameter that doesn't
// depend on its template parameters is made with a ParenListExpr of them.
void fg_CoroutineLambda(unsigned _Bits)
{
	auto fWork = [](auto, int _Count) -> TCFuture<int>
	{
		co_return _Count;
	};
	(void)fWork;
	if (_Bits)
		(void)fg_GetHighestBitSetNoZero(_Bits);
}

// A member initialized with a parenthesized list in a constructor of a local
// class of a generic lambda.
void fg_LocalClass(unsigned _Bits)
{
	auto fMake = [](auto _Value)
	{
		struct CLocal
		{
			decltype(_Value) m_Value;
			CLocal(decltype(_Value) _Initial) : m_Value(_Initial, 1)
			{
			}
		};
		CLocal Local(_Value);
		return Local.m_Value;
	};
	(void)fMake;
	if (_Bits)
		(void)fg_GetHighestBitSetNoZero(_Bits);
}
