// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-conceptual-prefix %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

// An element of a range whose `begin` is a function, like Malterlib's adapters
// of its containers, and a value moved from one, are named for the generic
// type they have in the template, not for what an instantiation makes it.

namespace NContainer {
template <typename t_CData>
struct TCVector { t_CData *f_GetArray(); };
template <typename tf_CData>
tf_CData *begin(TCVector<tf_CData> &_Vector) {
  return _Vector.f_GetArray();
}
template <typename tf_CData>
tf_CData *end(TCVector<tf_CData> &_Vector) {
  return _Vector.f_GetArray() + 1;
}
template <typename tf_CData>
void fg_Visit(TCVector<tf_CData> &_Vector) {
  for (auto &Value : _Vector) {}
}
void fg_Run(TCVector<bool> &_Vector) { fg_Visit(_Vector); }
void fg_RunConcrete(TCVector<bool> &_Vector) {
  for (auto &Value : _Vector) {}
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'Value' is a boolean and should have the 'b' concept prefix [malterlib-conceptual-prefix]
}
}

namespace NMove {
template <typename tf_CType>
tf_CType &&fg_Move(tf_CType &_Value) {
  return static_cast<tf_CType &&>(_Value);
}
template <typename tf_CType>
void fg_Run(tf_CType &_Value) {
  auto Temp = fg_Move(_Value);
}
void fg_Test(bool &_bFlag) { fg_Run(_bFlag); }
void fg_Concrete(bool &_bFlag) {
  auto Temp = fg_Move(_bFlag);
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'Temp' is a boolean and should have the 'b' concept prefix [malterlib-conceptual-prefix]
}
}

// Also through a `begin` function that gives an iterator, and a member of a
// base, which each instantiation makes of the parameter the container is given.
namespace NIterator {
template<typename t_CData> struct TCLinkedList {
  struct CIterator {
    t_CData &operator*() const;
    CIterator &operator++();
    bool operator!=(CIterator const &_iOther) const;
  };
  CIterator f_GetIterator();
};
template<typename tf_CContainer>
auto begin(tf_CContainer &&_Container)
    -> decltype(_Container.f_GetIterator());
template<typename tf_CContainer>
auto end(tf_CContainer &&_Container)
    -> decltype(_Container.f_GetIterator());

template<typename tf_CData>
void fg_Visit(TCLinkedList<tf_CData> &_List) {
  for (auto &Value : _List) { (void)Value; }
}
void fg_Use(TCLinkedList<bool> &_List) { fg_Visit(_List); }
void fg_Concrete(TCLinkedList<bool> &_List) { for (auto &Value : _List) { (void)Value; } }
// CHECK-MESSAGES: :[[@LINE-1]]:58: warning: 'Value' is a boolean and should have the 'b' concept prefix [malterlib-conceptual-prefix]
}

namespace NBase {
template<typename t_CKey, typename t_CValue>
struct TCMap {
  t_CValue &operator[](t_CKey const &_Key);
};
template<typename t_CKey, typename t_CValue>
struct TCMapWithPool : TCMap<t_CKey, t_CValue> {};

template<typename tf_CValue>
void fg_Update(TCMapWithPool<int, tf_CValue> &_Map) {
  auto &Value = _Map[0];
  (void)Value;
}
void fg_Use(TCMapWithPool<int, bool> &_Map) {
  fg_Update(_Map);
}
void fg_Concrete(TCMapWithPool<int, bool> &_Map) { auto &Value = _Map[0]; (void)Value; }
// CHECK-MESSAGES: :[[@LINE-1]]:58: warning: 'Value' is a boolean and should have the 'b' concept prefix [malterlib-conceptual-prefix]
}

// Also in a member function of a class template, of the class's parameter.
namespace NClassTemplate {
template<typename t_CValue> struct TCMap {
  t_CValue &operator[](int _Key);
};
template<typename t_CValue>
struct TCMapWithPool : TCMap<t_CValue> {};

template<typename t_CValue> struct TCProcessor {
  void f_Process(TCMapWithPool<t_CValue> &_Map) {
    auto &Value = _Map[0];
    (void)Value;
  }
};
void fg_Run(TCMapWithPool<bool> &_Map) {
  TCProcessor<bool> Processor;
  Processor.f_Process(_Map);
}
struct CConcrete {
  void f_Process(TCMapWithPool<bool> &_Map) {
    auto &Value = _Map[0];
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'Value' is a boolean and should have the 'b' concept prefix [malterlib-conceptual-prefix]
    (void)Value;
  }
};
}

// Also what an exchange gives back of what it is given, named as that is.
namespace NExchange {
template<typename t_CType> struct TCUniquePointer {
  TCUniquePointer &operator=(decltype(nullptr) _pValue);
};
struct CActorSubscriptionReference {};
using CActorSubscription = TCUniquePointer<CActorSubscriptionReference>;

template<typename tf_CValue, typename tf_CSetTo>
auto fg_Exchange(tf_CValue &_Value, tf_CSetTo &&_New) {
  auto Temp = static_cast<tf_CValue &&>(_Value);
  _Value = static_cast<tf_CSetTo &&>(_New);
  return Temp;
}

void fg_Remove(CActorSubscription &_Subscription) {
  auto Subscription = fg_Exchange(_Subscription, nullptr);
  (void)Subscription;
}
}

// Also an element of the class itself, `*this`, which is given its parameters.
namespace NThis {
template<typename t_CValue> struct TCVector {
  t_CValue m_Value{};
  t_CValue *begin() { return &m_Value; }
  t_CValue *end() { return &m_Value + 1; }
  void f_Visit() {
    for (auto &Value : *this) (void)Value;
  }
};
void fg_Run() {
  TCVector<bool> Vector;
  Vector.f_Visit();
}
}

// Also what a function returns that its template declares a type for, which
// takes no concept prefix that the instantiations would require, since a
// specialization for an argument can change it.
namespace NFixed {
template<typename t_CValue> struct TCVector {
  bool f_IsEmpty() const { return false; }
};
template<typename tf_CValue>
void fg_Check(TCVector<tf_CValue> const &_Vector) {
  auto Empty = _Vector.f_IsEmpty();
  (void)Empty;
}
void fg_Run() { fg_Check(TCVector<bool>{}); }
}

// Also an element an iterator gives, through its `operator*` or a conversion
// to a pointer, and one indexed through a local deduced from a pointer to it.
namespace NIterator {
template <typename t_CData>
struct TCVectorIterator {
  operator t_CData *() const;
  TCVectorIterator &operator++();
};
template <typename t_CValue>
struct TCMapIterator {
  t_CValue &operator*() const;
};
template <typename t_CData>
struct TCVector { t_CData *f_GetArray(); };
template <typename tf_CData>
void fg_Visit(TCVectorIterator<tf_CData> _iValue, TCMapIterator<tf_CData> _iEntry,
              TCVector<tf_CData> &_Vector, tf_CData *_pArray) {
  auto &Value = *_iValue;
  auto &Entry = *_iEntry;
  auto pArray = _Vector.f_GetArray();
  auto &First = pArray[0];
  auto pCopied = _pArray;
  auto &Second = pCopied[0];
  auto &Third = *pArray;
  (void)Value;
  (void)Entry;
  (void)First;
  (void)Second;
  (void)Third;
}
void fg_Use(TCVectorIterator<bool> _iValue, TCMapIterator<bool> _iEntry,
            TCVector<bool> &_Vector, bool *_pArray) {
  fg_Visit(_iValue, _iEntry, _Vector, _pArray);
}
}

// Also what an iterator gives that its template declares a type for, which a
// specialization for an argument can change.
namespace NFixedIterator {
template <typename t_CValue>
struct TCFlagIterator {
  bool operator*() const;
};
template <typename tf_CValue>
void fg_Visit(TCFlagIterator<tf_CValue> _iFlag) {
  auto Flag = *_iFlag;
  (void)Flag;
}
void fg_Use(TCFlagIterator<bool> _iFlag) { fg_Visit(_iFlag); }
}

// Also a copy of a generic local, also in a lambda, either of a conditional of
// two, what a function template gives back of what it is given, what a
// function object given the parameter as its result gives, and an element of
// what a member function gives.
namespace NRound332 {
template <typename tf_CType>
tf_CType const &fg_Const(tf_CType const &_Value) { return _Value; }
template <typename t_FSignature> struct TCFunction;
template <typename t_CReturn> struct TCFunction<t_CReturn()> {
  t_CReturn operator()();
};
template <typename t_CData> struct TCVector {
  t_CData *f_GetArray();
  t_CData &operator[](int _Index);
  bool m_bReady;
  auto f_IsEmpty() const { return false; }
  auto &f_GetFirst() { return f_GetArray()[0]; }
};
template <typename t_CKey, typename t_CValue> struct TCMap {
  t_CValue &operator[](t_CKey const &_Key);
};
// Instantiated with a boolean only, which these are generic of.
template <typename tf_CType>
void fg_Visit(tf_CType const &_Value, tf_CType &_First, tf_CType &_Second,
              bool _bFirst, TCVector<tf_CType> &_Vector,
              TCFunction<tf_CType()> &_fCallback) {
  auto Copy = _Value;
  auto fRun = [Copy] {
    auto Inner = Copy;
    (void)Inner;
  };
  fRun();
  auto &Selected = _bFirst ? _First : _Second;
  auto &Const = fg_Const(_Value);
  auto Result = _fCallback();
  auto &Element = _Vector.f_GetArray()[0];
  auto &First = _Vector.f_GetFirst();
  (void)Selected;
  (void)Const;
  (void)Result;
  (void)Element;
  (void)First;
}
// Also what the template declares a type of its own for takes no concept
// prefix that the instantiations would require, which an argument can change,
// only none that each of them excludes.
template <typename tf_CType>
void fg_Fixed(TCVector<tf_CType> &_Vector, TCMap<tf_CType, void *> &_Map,
              tf_CType const &_Key) {
  auto Ready = _Vector.m_bReady;
  auto Empty = _Vector.f_IsEmpty();
  (void)Ready;
  (void)Empty;
}
template <typename tf_CKey>
void fg_Mapped(TCMap<tf_CKey, void *> &_Map, tf_CKey const &_Key) {
  auto &Mapped = _Map[_Key];
  (void)Mapped;
}
void fg_Use(TCVector<bool> &_Vector, TCFunction<bool()> &_fCallback,
            TCMap<bool, void *> &_Map, TCMap<void *, void *> &_PointerMap) {
  bool bValue = false;
  fg_Visit(true, bValue, bValue, true, _Vector, _fCallback);
  fg_Fixed(_Vector, _Map, bValue);
  void *pKey = nullptr;
  fg_Mapped(_PointerMap, pKey);
}
}

// Also what a function object gives whose signature has a pack, or whose call
// operator is of a base, and an element of an array of the parameter, and
// what a call operator gives that declares its own type takes no concept
// prefix that the instantiations would require either.
namespace NRound333 {
template <typename t_FSignature> struct TCFunction;
template <typename t_CReturn, typename... tp_CParams>
struct TCFunction<t_CReturn(tp_CParams...)> {
  t_CReturn operator()(tp_CParams... p_Params);
};
template <typename t_CReturn> struct TCCallBase {
  t_CReturn operator()();
};
template <typename t_FSignature> struct TCInherited;
template <typename t_CReturn>
struct TCInherited<t_CReturn()> : TCCallBase<t_CReturn> {};
struct CFixedBase {
  bool operator()();
};
template <typename t_FSignature> struct TCFixed;
template <typename t_CReturn> struct TCFixed<t_CReturn()> : CFixedBase {};
template <typename tf_CResult, typename tf_CType, unsigned tf_nValues>
void fg_Run(TCFunction<tf_CResult()> &_fCallback,
            TCInherited<tf_CResult()> &_fInherited,
            TCFixed<tf_CResult()> &_fFixed, tf_CType (&_Values)[tf_nValues]) {
  auto Result = _fCallback();
  auto Inherited = _fInherited();
  auto Done = _fFixed();
  for (auto &Value : _Values)
    (void)Value;
  (void)Result;
  (void)Inherited;
  (void)Done;
}
void fg_Test(TCFunction<bool()> &_fCallback, TCInherited<bool()> &_fInherited,
             TCFixed<bool()> &_fFixed) {
  bool Values[2]{};
  fg_Run(_fCallback, _fInherited, _fFixed, Values);
}
}
