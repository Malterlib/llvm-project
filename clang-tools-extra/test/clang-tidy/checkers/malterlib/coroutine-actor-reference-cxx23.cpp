// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-coroutine-actor-reference %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

TCFuture<int> fg_Pause();

// An assignment operator with an explicit object, whose value is its second
// parameter, `operator=(this CValue _Self, int const &_Value)`, binds a
// reference to what it is given before the left side suspends.
struct CValue {
  int m_Value;
  int operator=(this CValue _Self, int const &_Value) { return _Value; }
};

// But not one that takes a copy of it.
struct CCopied {
  int m_Value;
  void operator=(this CCopied &_Self, int _Value) { _Self.m_Value = _Value; }
};

struct CItems {
  CValue &operator[](int _Key);
  void f_Clear();
};

struct CCopiedItems {
  CCopied &operator[](int _Key);
  void f_Clear();
};

struct CApp : CActor {
  CItems mp_Items;
  CCopiedItems mp_Copied;

  void f_Clear() { mp_Items.f_Clear(); }

  TCFuture<int> f_Reference() {
    mp_Items[(co_await fg_Pause(), 0)] = mp_Items[1].m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: the data of the actor that this call uses is looked up before its arguments suspend, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
    co_return 0;
  }

  TCFuture<int> f_Copy() {
    mp_Copied[(co_await fg_Pause(), 0)] = mp_Copied[1].m_Value;
    co_return 0;
  }
};

namespace NRound160 {
TCFuture<int> fg_Suspend();
// A getter with an explicit object that gives a member of it.
struct CApp : CActor {
  int m_Value;
  int &f_Value(this CApp &_Self) { return _Self.m_Value; }
  TCFuture<int> f_Run() {
    int &Value = f_Value();
    co_await fg_Suspend();
    co_return Value;
  }
};
}

namespace NRound195 {
TCFuture<int> fg_Suspend();
// A member function with an explicit object that gives a member of it refers
// into an entry it is called on.
struct CValue {
  int m_Value;
  int &f_Value(this CValue &_Self) { return _Self.m_Value; }
};
template <typename t_CKey, typename t_CValue>
struct TCMap {
  t_CValue &operator[](t_CKey _Key);
};
struct CApp : CActor {
  TCMap<int, CValue> mp_Items;
  CValue m_Fixed;
  TCFuture<int> f_Entry() {
    int &Value = mp_Items[1].f_Value();
    co_await fg_Suspend();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Fixed() {
    int &Value = m_Fixed.f_Value();
    co_await fg_Suspend();
    co_return Value;
  }
};
}
