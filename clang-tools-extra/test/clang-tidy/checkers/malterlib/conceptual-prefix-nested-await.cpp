// RUN: clang-tidy --checks='-*,malterlib-conceptual-prefix' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --allow-empty %s

// The callback type written in an instantiated function is found by going
// into each statement once, also the operand of each of nested awaits, which
// the expressions that make and call its awaiter have too, and would be gone
// into again through each of them, a number of times exponential in the
// depth of the nesting.

#include "malterlib-concurrency.h"

CAwaiter fg_Wait(int _Value);
struct CTraits
{
	using CType = bool;
};

#define DAwait(X) fg_Wait(co_await (X))
#define DAwait4(X) DAwait(DAwait(DAwait(DAwait(X))))
#define DAwait32(X) DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(X))))))))

template <class t_CTraits>
TCFuture<int> fg_Test()
{
	co_await DAwait32(CAwaiter{});
	(void)sizeof(TCFunction<void (typename t_CTraits::CType _bValue)>);
	co_return 0;
}

void fg_Start()
{
	fg_Test<CTraits>();
}

// CHECK-NOT: {{warning|error}}:
