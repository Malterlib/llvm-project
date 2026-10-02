// RUN: clang-tidy --checks='-*,malterlib-coroutine-unsafe-this' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --allow-empty %s

// Each getter is looked at once for whether what it gives points into the
// object, also getters that each return the one before twice, which would be
// looked at again through each path to them, a number of times exponential in
// the length of the chain.

#include "malterlib-concurrency.h"

int g_Value;
TCFuture<int> fg_Suspend();

struct CState
{
	template <int t_nDepth>
	int *f_Get(bool _bChoose)
	{
		if constexpr (t_nDepth == 0)
			return &g_Value;
		else
		{
			if (_bChoose)
				return f_Get<t_nDepth - 1>(_bChoose);
			return f_Get<t_nDepth - 1>(_bChoose);
		}
	}

	TCFuture<int> f_Start(bool _bChoose)
	{
		return fg_CallSafe
			(
				[pValue = f_Get<30>(_bChoose)]() -> TCFuture<int>
				{
					co_await fg_Suspend();
					co_return *pValue;
				}
			)
		;
	}
};

// CHECK-NOT: warning:

// Round 173: a helper that gives a shared owner made of its object, `return
// TCSharedPointer<CState>{fg_Explicit(this)};`, captured keeps the object, as
// that owner made in the capture does. Reported: the helper called on another
// object, whose owner keeps that one.
namespace NReview2Round173Getters {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CState {
  int m_Value = 0;
  TCSharedPointer<CState> f_Keep() { return TCSharedPointer<CState>{fg_Explicit(this)}; }
  TCFuture<int> f_Run() {
    return fg_CallSafe([this, pThis = f_Keep()]() -> TCFuture<int> { co_await fg_Work(); co_return m_Value; });
  }
  TCFuture<int> f_RunDirect() {
    return fg_CallSafe([this, pThis = TCSharedPointer<CState>{fg_Explicit(this)}]() -> TCFuture<int> { co_await fg_Work(); co_return m_Value; });
  }
  TCFuture<int> f_RunOther(CState *_pOther) {
    return fg_CallSafe([this, pThis = _pOther->f_Keep()]() -> TCFuture<int> { co_await fg_Work(); co_return m_Value; });
// CHECK: :[[@LINE-1]]:{{[0-9]+}}: warning: a TCFuture coroutine lambda captures 'this' of 'CState'
  }
};
} // namespace NReview2Round173Getters

// CHECK-NOT: warning:
