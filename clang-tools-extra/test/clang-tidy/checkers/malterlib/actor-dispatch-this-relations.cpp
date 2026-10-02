// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-actor-dispatch-this %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

// Whether a pointer a lambda that runs on another actor captures points into
// the object of the actor is told by what it is relative to the object: what
// the object holds by value, also a member of a member, an element of an array
// member, through a local alias, a cast, a conditional or a comma, is; what a
// pointer member points to, which can be another actor, isn't, also through a
// local alias of it, nor a value a member function makes.
// This file is generated: each case captures a form in a lambda dispatched to
// another actor.

namespace std {
class type_info;
}

namespace NDispatchRelations {
struct CState {
  int m_Value = 0;
  CState f_Copy() const;
};
struct COuter {
  CState m_Inner;
};
struct CHolder {
  int m_Value = 0;
};
inline int *&fg_Identity(int *&_pValue) { return _pValue; }
inline int &fg_Alias(int &_Value) {
  int &Alias = _Value;
  return Alias;
}
struct CParts : CActor {
  int m_Value = 0;
  int *m_pValue = nullptr;
  int m_Values[2] = {};
  CState m_State;
  COuter m_Outer;
  CHolder *m_pOther = nullptr;
  bool m_bChoose = false;
  TCActor<> mp_Other;
  // A class whose getter gives what a pointer it holds points to, which,
  // as that pointer read directly, isn't the object.
  struct CStorage {
    int *m_pStorage = nullptr;
    int *data() { return m_pStorage; }
  };
  CStorage mp_Storage;
  // An accessor that gives a pointer to the object itself.
  CParts *f_SelfPointer() { return this; }
  CParts *f_SelfPointerAlias() {
    auto *pSelf = this;
    return pSelf;
  }
  CParts *f_SelfPointerAddress() { return &*this; }

  void f_PointerMemberReference() {
    (void)(g_Dispatch(mp_Other) / [&pValue = m_pValue] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ReassignedAlias() {
    auto *pOther = m_pOther;
    (void)(g_Dispatch(mp_Other) / [pCopy = pOther] { return pCopy != nullptr; });
    pOther = nullptr;
    (void)pOther;
  }

  void f_Own() {
    (void)(g_Dispatch(mp_Other) / [pValue = this] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ByValue() {
    (void)(g_Dispatch(mp_Other) / [pValue = &m_Value] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_MemberOfMember() {
    (void)(g_Dispatch(mp_Other) / [pValue = &m_Outer.m_Inner.m_Value] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ArrayElement() {
    (void)(g_Dispatch(mp_Other) / [pValue = &m_Values[1]] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ByValueAlias() {
    auto *pState = &m_State;
    (void)(g_Dispatch(mp_Other) / [pValue = &pState->m_Value] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ByValueThisAlias() {
    auto *pThis = this;
    (void)(g_Dispatch(mp_Other) / [pValue = &pThis->m_Value] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ByValueCast() {
    (void)(g_Dispatch(mp_Other) / [pValue = static_cast<int *>(&m_Value)] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ByValueConditional() {
    (void)(g_Dispatch(mp_Other) / [pValue = m_bChoose ? &m_Value : &m_State.m_Value] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_MixedConditional() {
    (void)(g_Dispatch(mp_Other) / [pValue = m_bChoose ? &m_Value : &m_pOther->m_Value] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ByValueComma() {
    (void)(g_Dispatch(mp_Other) / [pValue = ((void)0, &m_Value)] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_PointerMemberAddress() {
    (void)(g_Dispatch(mp_Other) / [pValue = &m_pValue] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_AddressOf() {
    (void)(g_Dispatch(mp_Other) / [pValue = __builtin_addressof(m_Value)] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ThroughInteger() {
    (void)(g_Dispatch(mp_Other) / [pValue = reinterpret_cast<int *>(reinterpret_cast<__UINTPTR_TYPE__>(&m_Value))] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ThroughIntegerBitCast() {
    (void)(g_Dispatch(mp_Other) / [pValue = __builtin_bit_cast(int *, __builtin_bit_cast(__UINTPTR_TYPE__, &m_Value))] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ThroughIntegerComma() {
    (void)(g_Dispatch(mp_Other) / [pValue = reinterpret_cast<int *>(((void)0, reinterpret_cast<__UINTPTR_TYPE__>(&m_Value)))] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ThroughIntegerConditional() {
    (void)(g_Dispatch(mp_Other) / [pValue = reinterpret_cast<int *>(m_bChoose ? reinterpret_cast<__UINTPTR_TYPE__>(&m_Value) : reinterpret_cast<__UINTPTR_TYPE__>(&m_Values[1]))] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_StorageGetter() {
    (void)(g_Dispatch(mp_Other) / [pValue = mp_Storage.data()] { return pValue != nullptr; });
  }

  void f_ThroughIntegerOffset() {
    (void)(g_Dispatch(mp_Other) / [pValue = reinterpret_cast<int *>(sizeof(int) + reinterpret_cast<__UINTPTR_TYPE__>(m_Values))] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_TypeInformation() {
    (void)(g_Dispatch(mp_Other) / [pValue = &typeid(*this)] { return pValue != nullptr; });
  }

  void f_ReferenceAliasOfPointerMember() {
    auto &pAlias = m_pOther;
    (void)(g_Dispatch(mp_Other) / [pValue = &pAlias->m_Value] { return pValue != nullptr; });
  }

  void f_AddressOfLocalPointer() {
    int *pLocal = &m_Value;
    (void)(g_Dispatch(mp_Other) / [pValue = &pLocal] { return pValue != nullptr; });
  }

  void f_IdentityOfLocalPointer() {
    int *pLocal = &m_Value;
    (void)(g_Dispatch(mp_Other) / [pValue = fg_Identity(pLocal)] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_AliasHelperOfOther() {
    (void)(g_Dispatch(mp_Other) / [pValue = &fg_Alias(m_pOther->m_Value)] { return pValue != nullptr; });
  }

  void f_AliasHelperOfOwn() {
    (void)(g_Dispatch(mp_Other) / [pValue = &fg_Alias(m_Value)] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_AssignedWithoutInitializer() {
    int *pLocal;
    pLocal = &m_Value;
    (void)(g_Dispatch(mp_Other) / [pValue = pLocal] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_AssignedInBranches() {
    int *pLocal;
    if (m_bChoose) pLocal = &m_pOther->m_Value;
    else pLocal = &m_Value;
    (void)(g_Dispatch(mp_Other) / [pValue = pLocal] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ConditionalOfLocalAndMember() {
    int *pLocal = &m_Value;
    (void)(g_Dispatch(mp_Other) / [pValue = m_bChoose ? pLocal : m_pValue] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ConditionalOfMembers() {
    (void)(g_Dispatch(mp_Other) / [pValue = m_bChoose ? m_pValue : m_pValue] { return pValue != nullptr; });
  }

  void f_SelfReferencingAlias() {
    int *&pAlias = false ? pAlias : m_pValue;
    (void)(g_Dispatch(mp_Other) / [pValue = pAlias] { return pValue != nullptr; });
  }

  void f_AddressOfLocalPointerByBuiltin() {
    int *pLocal = &m_Value;
    (void)(g_Dispatch(mp_Other) / [pValue = __builtin_addressof(pLocal)] { return pValue != nullptr; });
  }

  void f_AddressOfLocalPointerReference() {
    int *pLocal = &m_Value;
    int *&rpLocal = pLocal;
    (void)(g_Dispatch(mp_Other) / [pValue = &rpLocal] { return pValue != nullptr; });
  }

  void f_AddressOfLocalPointerCast() {
    int *pLocal = &m_Value;
    (void)(g_Dispatch(mp_Other) / [pValue = &static_cast<int *&>(pLocal)] { return pValue != nullptr; });
  }

  void f_SelfPointerAccessor() {
    (void)(g_Dispatch(mp_Other) / [pValue = f_SelfPointer()] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_SelfPointerAccessorMember() {
    (void)(g_Dispatch(mp_Other) / [pValue = &f_SelfPointer()->m_Value] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_SelfPointerAccessorAlias() {
    (void)(g_Dispatch(mp_Other) / [pValue = f_SelfPointerAlias()] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_SelfPointerAccessorAddress() {
    (void)(g_Dispatch(mp_Other) / [pValue = f_SelfPointerAddress()] { return pValue != nullptr; });
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: a lambda that captures {{.*}} runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }

  void f_ThroughPointer() {
    (void)(g_Dispatch(mp_Other) / [pValue = &m_pOther->m_Value] { return pValue != nullptr; });
  }

  void f_ThroughPointerValue() {
    (void)(g_Dispatch(mp_Other) / [pValue = m_pOther] { return pValue != nullptr; });
  }

  void f_ThroughPointerAlias() {
    auto *pOther = m_pOther;
    (void)(g_Dispatch(mp_Other) / [pValue = &pOther->m_Value] { return pValue != nullptr; });
  }

  void f_ThroughPointerCast() {
    (void)(g_Dispatch(mp_Other) / [pValue = static_cast<int *>(&m_pOther->m_Value)] { return pValue != nullptr; });
  }

  void f_ThroughPointerConditional() {
    (void)(g_Dispatch(mp_Other) / [pValue = m_bChoose ? &m_pOther->m_Value : &m_pOther[1].m_Value] { return pValue != nullptr; });
  }

  void f_ThroughPointerComma() {
    (void)(g_Dispatch(mp_Other) / [pValue = ((void)0, &m_pOther->m_Value)] { return pValue != nullptr; });
  }
};
}
