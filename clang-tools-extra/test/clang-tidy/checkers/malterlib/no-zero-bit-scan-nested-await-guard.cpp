// RUN: clang-tidy --checks='-*,malterlib-no-zero-bit-scan' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --allow-empty %s

// The uses of a parameter in a constructor that keeps it are looked at once,
// also in nested awaits in its body or in the initializer of another member,
// whose operands the expressions that make and call their awaiters have too.

#include "malterlib-concurrency.h"

CAwaiter fg_Wait(int _Value);
unsigned fg_GetHighestBitSetNoZero(unsigned _Value);

#define DAwait(X) fg_Wait(co_await (X))
#define DAwait2(X) DAwait(DAwait(X))
#define DAwait4(X) DAwait2(DAwait2(X))
#define DAwait40(X) DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(X))))))))))

struct CGuard
{
	unsigned &m_Bits;
	CGuard(unsigned &_Bits)
		: m_Bits(_Bits)
	{
		auto fWork = []() -> TCFuture<int>
			{
				co_return co_await DAwait40(CAwaiter{});
			}
		;
		(void)fWork;
	}
};

unsigned fg_Test(unsigned _Bits)
{
	if (_Bits)
	{
		CGuard Guard(_Bits);
		return fg_GetHighestBitSetNoZero(_Bits);
	}
	return 0;
}

struct CInitializerGuard
{
	unsigned &m_Bits;
	TCFuture<int> m_Future;
	CInitializerGuard(unsigned &_Bits)
		: m_Bits(_Bits)
		, m_Future
		(
			[]() -> TCFuture<int>
			{
				co_return co_await DAwait40(CAwaiter{});
			}()
		)
	{
	}
};

unsigned fg_TestInitializer(unsigned _Bits)
{
	if (_Bits)
	{
		CInitializerGuard Guard(_Bits);
		return fg_GetHighestBitSetNoZero(_Bits);
	}
	return 0;
}

// CHECK-NOT: {{warning|error}}:
