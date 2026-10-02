// RUN: clang-tidy --checks='-*,malterlib-no-zero-bit-scan' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck %s

// The test of an expression that suspends, here nested awaits, is its own:
// the key of what is tested doesn't go through the operand of each co_await
// again in the expressions that make and call its awaiter, which would take a
// number of steps exponential in the depth of the nesting.

#include "malterlib-concurrency.h"

using namespace NMib::NConcurrency;

unsigned fg_GetHighestBitSetNoZero(unsigned _Value);
TCFuture<unsigned> fg_Read(unsigned _Value);

#define DAwait(x) co_await fg_Read(x)
#define DAwait2(x) DAwait(DAwait(x))
#define DAwait4(x) DAwait2(DAwait2(x))
#define DAwait8(x) DAwait4(DAwait4(x))
#define DAwait16(x) DAwait8(DAwait8(x))
#define DAwait32(x) DAwait16(DAwait16(x))

TCFuture<unsigned> fg_Test(unsigned _Bits)
{
	if (DAwait32(DAwait4(DAwait2(1))))
		co_return fg_GetHighestBitSetNoZero(_Bits);
	co_return 0;
}

// CHECK: :[[@LINE-4]]:13: warning: {{.*}} [malterlib-no-zero-bit-scan]
// CHECK-NOT: warning:
