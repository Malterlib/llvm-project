// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-coroutine-reference-parameter %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

TCFuture<int> fg_Suspend();

// What only runs when constant evaluated doesn't run in a coroutine.
TCFuture<int> fg_Consteval(int &_Value)
{
	co_await fg_Suspend();
	if consteval
	{
		co_return _Value;
	}
	else
	{
		co_return 0;
	}
}

// What runs when not constant evaluated does.
TCFuture<int> fg_NotConsteval(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: TCFuture coroutine 'fg_NotConsteval' uses '_Value' after a suspension, {{.*}}
{
	co_await fg_Suspend();
	if !consteval
	{
		co_return _Value;
	}
	else
	{
		co_return 0;
	}
}

namespace NRound144 {
TCFuture<int> fg_Work();

// The explicit object of a member function of an actor, which is its object.
struct CApp : CActor {
  int m_Value;
  TCFuture<int> f_Explicit(this CApp &_Self) {
    co_await fg_Work();
    co_return _Self.m_Value;
  }
};
}

namespace NRound152OwnStorage {
// A getter with an explicit object that reads only what a view holds itself,
// also through a view it holds by value, as one with an implicit object.
TCFuture<int> fg_Work();
using umint = unsigned long;
struct CReadStrSpan {
  char const *m_pStr;
  umint m_Length;
  umint f_GetLen(this CReadStrSpan const &_This) { return _This.m_Length; }
  char const *f_GetStr(this CReadStrSpan const &_This) { return _This.m_pStr; }
};
struct CSourceStrSpan {
  CReadStrSpan m_Text;
  umint f_GetLen(this CSourceStrSpan const &_This) { return _This.m_Text.f_GetLen(); }
};
TCFuture<umint> fg_Length(CReadStrSpan _Text) { co_await fg_Work(); co_return _Text.f_GetLen(); }
TCFuture<umint> fg_SourceLength(CSourceStrSpan _Text) { co_await fg_Work(); co_return _Text.f_GetLen(); }
TCFuture<char> fg_First(CReadStrSpan _Text) { co_await fg_Work(); co_return *_Text.f_GetStr(); }
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: TCFuture coroutine 'fg_First' uses '_Text' after a suspension, but it takes it as 'CReadStrSpan', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
}

// A three-way comparison of two pointer parameters after a suspension reads
// only their values, like `<` and `==` (pointerOperationOf).
namespace std {
struct strong_ordering {
  signed char m_Value;
  static const strong_ordering less, equal, equivalent, greater;
  friend constexpr bool operator<(strong_ordering _Ordering, int) { return _Ordering.m_Value < 0; }
};
inline constexpr strong_ordering strong_ordering::less{-1}, strong_ordering::equal{0}, strong_ordering::equivalent{0}, strong_ordering::greater{1};
} // namespace std
namespace NReview2Round194ThreeWay {
struct CJob {
  int m_Result = 0;
};
TCFuture<int> fg_Work();
TCFuture<int> fg_Order(CJob const *_pLeft, CJob const *_pRight) { co_await fg_Work(); co_return (_pLeft <=> _pRight) < 0; }
TCFuture<int> fg_Less(CJob const *_pLeft, CJob const *_pRight) { co_await fg_Work(); co_return _pLeft < _pRight; }
TCFuture<int> fg_Read(CJob const *_pLeft) { co_await fg_Work(); co_return _pLeft->m_Result; }
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: TCFuture coroutine 'fg_Read' uses '_pLeft' after a suspension, but it takes it as 'const CJob *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
} // namespace NReview2Round194ThreeWay
