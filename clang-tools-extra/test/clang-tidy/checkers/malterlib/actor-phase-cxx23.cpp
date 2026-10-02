// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-actor-phase %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-actor-phase.h"

// A function of a phase with an explicit object, called directly on the own
// object, and on another.
struct CExplicitObject : CPhaseActor
{
	DMibActorPhase(Destroy) void f_Clear(this CExplicitObject &_This)
	{
	}

	DMibActorPhase(Destroy) void f_Stop()
	{
		f_Clear();
	}

	DMibActorPhase(Destroy) void f_StopOther(CExplicitObject &_Other)
	{
		_Other.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'f_Clear' runs only in the Destroy phase of its actor, but is used here on another object, which may not be in that phase [malterlib-actor-phase]
	}
};

namespace NRound175 {
// An operator with an explicit object that is a reference to const doesn't
// remove entries.
struct CSet {
  CSet operator&(this CSet const &_Self, CSet const &_Other) { return {}; }
  CSet &operator-=(this CSet &_Self, CSet const &_Other) { return _Self; }
};
struct CApp : CActor {
  DMibActorEntries(Destroy) CSet mp_Items;
  CSet f_Intersection(CSet const &_Other) { return mp_Items & _Other; }
  void f_Subtract(CSet const &_Other) { mp_Items -= _Other; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Subtract'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
};
}

namespace NRound200 {
// A member function with an explicit object is like any other: one that
// copies doesn't remove entries, one that removes does, and so does one
// named like it.
struct CSet {
  template <class t_CThis>
  CSet f_Difference(this t_CThis &&_This, CSet const &_Other) {
    return {};
  }
  template <class t_CThis> void f_Clear(this t_CThis &&_This) {}
  template <class t_CThis> int f_Remove(this t_CThis &&_This, int _Key) {
    return _Key;
  }
};
struct CApp : CActor {
  DMibActorEntries(Destroy) CSet mp_Items;
  CSet f_Copy(CSet const &_Other) { return mp_Items.f_Difference(_Other); }
  void f_Clear() { mp_Items.f_Clear(); }
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Clear'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
  int f_Remove() { return mp_Items.f_Remove(1); }
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Remove'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
};
}

namespace NRound213 {
// A binary operator that makes a new value, also with an object that isn't
// const, doesn't remove entries.
struct CSet {
  template <class t_CThis>
  CSet f_Difference(this t_CThis &&_This, CSet const &_Other) {
    return {};
  }
  template <class t_CThis>
  CSet operator-(this t_CThis &&_This, CSet const &_Other) {
    return _This.f_Difference(_Other);
  }
  template <class t_CThis>
  CSet &operator-=(this t_CThis &&_This, CSet const &_Other);
};
struct CApp : CActor {
  DMibActorEntries(Destroy) CSet mp_Items;
  CSet f_Remaining(CSet const &_Other) { return mp_Items - _Other; }
  void f_Subtract(CSet const &_Other) { mp_Items -= _Other; }
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Subtract'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
};
}

namespace NRound217 {
struct TCVector {
  void f_SetLen(int);
  int f_GetLen(this TCVector const &_Self);
  // Removes entries through its explicit object.
  int *f_GetArray(this TCVector &_Self, int _Len) {
    _Self.f_SetLen(_Len);
    return nullptr;
  }
  int *f_View(this TCVector &_Self) {
    (void)_Self.f_GetLen();
    return nullptr;
  }
};
struct CApp : CActor {
  DMibActorEntries(Destroy) TCVector mp_Values;
  TCFuture<int> f_Run() {
    mp_Values.f_GetArray(0);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'mp_Values' is declared to lose entries only in Destroy, but is changed here in 'f_Run'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
    mp_Values.f_View();
    co_return 0;
  }
};
}

namespace NRound227 {
// An operator that makes a new value of what it is given to consume, cast to
// an rvalue, can take its entries, like `TCSet::f_And` does, but not of what
// it copies.
template <class t_CKey>
struct TCSet {
  void f_Clear();
  template <class t_CSelf>
  TCSet operator&(this t_CSelf &&_Self, TCSet const &) {
    if constexpr (__is_rvalue_reference(t_CSelf &&))
      _Self.f_Clear();
    return {};
  }
  TCSet operator|(TCSet const &) &&;
  TCSet operator-(TCSet const &) const &;
};
struct CApp : CActor {
  DMibActorEntries(Destroy) TCSet<int> mp_Items;
  void f_Process(TCSet<int> const &_Other) {
    auto And = static_cast<TCSet<int> &&>(mp_Items) & _Other;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Process'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
    auto Or = static_cast<TCSet<int> &&>(mp_Items) | _Other;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Process'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
    auto Copied = mp_Items & _Other;
    auto Difference = static_cast<TCSet<int> &&>(mp_Items) - _Other;
  }
};
}
