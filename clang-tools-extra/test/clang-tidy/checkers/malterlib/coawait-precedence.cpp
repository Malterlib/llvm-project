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

// Reported whatever `operator%` the awaitable and the message have: the check
// offers no fix-it.
namespace NLandingFixes {
struct CRead {
  bool await_ready() const noexcept;
  void await_suspend(std::coroutine_handle<>) const noexcept;
  CResult await_resume() const;
};
CRead fg_Read();

struct CReadWithMessage {
  bool await_ready() const noexcept;
  void await_suspend(std::coroutine_handle<>) const noexcept;
  CResult await_resume() const;
};
struct CMessageRead {
  bool await_ready() const noexcept;
  void await_suspend(std::coroutine_handle<>) const noexcept;
  CResult await_resume() const;
  CReadWithMessage operator%(char const *) &&;
};
CMessageRead fg_MessageRead();

struct CTwoWays {
  bool await_ready() const noexcept;
  void await_suspend(std::coroutine_handle<>) const noexcept;
  CResult await_resume() const;
  CTwoWays operator%(char const *) &&;
};
CTwoWays operator%(CTwoWays const &, char const *);
CTwoWays fg_TwoWays();

struct CConstrained {
  bool await_ready() const noexcept;
  void await_suspend(std::coroutine_handle<>) const noexcept;
  CResult await_resume() const;
  template <typename t_CMessage>
    requires(sizeof(t_CMessage) > 64)
  CConstrained operator%(t_CMessage const &) &&;
};
CConstrained fg_Constrained();

struct CDeleted {
  bool await_ready() const noexcept;
  void await_suspend(std::coroutine_handle<>) const noexcept;
  CResult await_resume() const;
  CDeleted operator%(char const *) && = delete;
};
CDeleted fg_Deleted();

struct CLValueOnly {
  bool await_ready() const noexcept;
  void await_suspend(std::coroutine_handle<>) const noexcept;
  CResult await_resume() const;
  CLValueOnly operator%(char const *) &;
};
CLValueOnly fg_LValueOnly();

TCFuture<int> fg_Run() {
  CResult NoOperator = co_await fg_Read() % "Message";
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: '%' applies to the result of 'co_await', {{.*}}
  CResult Other = co_await fg_MessageRead() % "Message";
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: '%' applies to the result of 'co_await', {{.*}}
  CResult TwoWays = co_await fg_TwoWays() % "Message";
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: '%' applies to the result of 'co_await', {{.*}}
  CResult Constrained = co_await fg_Constrained() % "Message";
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: '%' applies to the result of 'co_await', {{.*}}
  CResult Deleted = co_await fg_Deleted() % "Message";
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: '%' applies to the result of 'co_await', {{.*}}
  CResult LValueOnly = co_await fg_LValueOnly() % "Message";
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: '%' applies to the result of 'co_await', {{.*}}
  co_return 0;
}
} // namespace NLandingFixes
