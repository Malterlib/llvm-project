// UNSUPPORTED: system-windows
// RUN: sh -c "ulimit -t 20 && clang-tidy %s '-checks=-*,malterlib-no-zero-bit-scan' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs" 2>&1 | FileCheck %s

// Whether a scanned value suspends is told of all its syntax, also of an arm
// a constant condition leaves out, before its profile goes through it: nested
// awaits in that arm aren't profiled, which would go through the operand of
// each co_await again in the expressions that make and call its awaiter, a
// number of times exponential in how deep they nest.

#include "malterlib-concurrency.h"

TCFuture<unsigned> fg_Work(unsigned _Value);
unsigned fg_GetHighestBitSetNoZero(unsigned _Value);

#define DAwait(X) co_await fg_Work(X)
#define DAwait2(X) DAwait(DAwait(X))
#define DAwait4(X) DAwait2(DAwait2(X))
#define DAwait28(X) DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(DAwait4(X)))))))

TCFuture<int> fg_Scan(unsigned _Bits)
{
	if (_Bits)
		co_return fg_GetHighestBitSetNoZero(_Bits + (false ? DAwait28(_Bits) : 0));
	co_return 0;
}

// The sum isn't checked for zero, which a value that suspends, its own, isn't
// either; told in time.
// CHECK: :23:13: warning: the argument of 'fg_GetHighestBitSetNoZero' is not checked for zero first
// CHECK-NOT: {{warning|error}}:
