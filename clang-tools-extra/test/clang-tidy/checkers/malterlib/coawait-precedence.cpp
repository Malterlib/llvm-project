// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coawait-precedence %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

struct CResult
{
	CResult operator % (char const *) const;
};

TCFuture<CResult> fg_Function();
TCFuture<int> fg_Count();

TCFuture<int> fg_Precedence()
{
	CResult Wrong = co_await fg_Function() % "Message";
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: '%' applies to the result of 'co_await', which binds tighter; parenthesize the operand of 'co_await' to apply it to what is awaited [malterlib-coawait-precedence]
// CHECK-FIXES: CResult Wrong = co_await (fg_Function() % "Message");
	CResult Right = co_await (fg_Function() % "Message");
	CResult Explicit = (co_await fg_Function()) % "Message";
	int Remainder = co_await fg_Count() % 2;
	co_return 0;
}

// Coroutine lambdas that return each other nest without being traversed once
// per level, which would never end.
#define DRound95Return(d_Value) ([]() -> TCFuture<int> { co_return d_Value; })().f_CallSync()
#define DRound95Return4(d_Value) DRound95Return(DRound95Return(DRound95Return(DRound95Return(d_Value))))

TCFuture<int> fg_Round95Nested()
{
	co_return DRound95Return4(DRound95Return4(DRound95Return4(DRound95Return4(DRound95Return4(DRound95Return4(0))))));
}

// Round 120: the remainder of a number that is a class, by one of its type.
namespace NRound120ClassRemainder
{
struct CBigInt {
  CBigInt operator%(CBigInt const &_Right) const;
};
TCFuture<CBigInt> fg_Read();

TCFuture<int> fg_Run(CBigInt _Divisor) {
  CBigInt Remainder = co_await fg_Read() % _Divisor;
  co_return 0;
}
}
