// RUN: clang-tidy --checks='-*,malterlib-parameter-ownership' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --allow-empty %s

// What a branch assigns to a local is looked at once, also through nested
// awaits in a coroutine lambda after it, whose operands the expressions that
// make and call their awaiters have too.

#include "malterlib-concurrency.h"

CAwaiter fg_Wait(int _Value);

#define DAwait(X) fg_Wait(co_await (X))
#define DAwait2(X) DAwait(DAwait(X))
#define DAwait4(X) DAwait2(DAwait2(X))
#define DAwait40(X) DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(X))))))))))

extern bool g_bChoose;

struct CValue
{
	CValue(CValue const &_Other);
	~CValue()
	{
		int *pScratch;
		if (g_bChoose)
			pScratch = new int(1);
		else
			pScratch = new int(2);

		auto fWork = []() -> TCFuture<int>
			{
				co_return co_await DAwait40(CAwaiter{});
			}
		;
		(void)fWork;
		delete pScratch;
	}
};

void fg_Use(CValue _Value)
{
}

// CHECK-NOT: {{warning|error}}:
