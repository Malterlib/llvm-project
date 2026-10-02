// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-actor-dispatch-this %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

// An explicit object call operator is given the actor as its parameter after the object.
namespace NRound115ExplicitObject {
struct App;
struct Reader {
  int operator()(this Reader &, App *p);
};
struct App : CActor {
  int value;
  int f(App *other) {
    Reader r;
    return r(other);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'operator()' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
  }
};
int Reader::operator()(this Reader &, App *p) {
  return p->value;
}
}

// A static call operator is given the actor as its parameter after the
// object its call names, `CReadValue{}`, which isn't given to it; like a
// call operator of an object.
namespace NReview2Round145
{
	struct CApp;
	struct CReadValue
	{
		static int operator()(CApp *_pApp);
	};
	struct CReadValueMember
	{
		int operator()(CApp *_pApp) const;
	};
	struct CApp : CActor
	{
		int m_Value = 0;
		int f_Read()
		{
			return CReadValue{}(this);
		}
		int f_ReadMember()
		{
			return CReadValueMember{}(this);
		}
		int f_ReadOther(CApp *_pOther)
		{
			return CReadValue{}(_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'operator()' is given the object of an actor, which it uses like 'this' on the actor the call runs on, {{.*}}
		}
		int f_ReadOtherMember(CApp *_pOther)
		{
			return CReadValueMember{}(_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'operator()' is given the object of an actor, which it uses like 'this' on the actor the call runs on, {{.*}}
		}
	};
	int CReadValue::operator()(CApp *_pApp)
	{
		return _pApp->m_Value;
	}
	int CReadValueMember::operator()(CApp *_pApp) const
	{
		return _pApp->m_Value;
	}
}
