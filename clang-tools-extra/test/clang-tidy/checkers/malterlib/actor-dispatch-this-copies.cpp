// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-actor-dispatch-this %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

// This file is generated.

#include "malterlib-concurrency.h"

// A copy is new at its own level: its storage and what it holds by value are
// its own, not the object's. What its borrowed parts lead to, its references
// and pointers, and what a shared owner points to, is what the source's lead
// to, also into the object. Each part of each kind of copy, and the same part
// of the source itself.
namespace NCopies {
TCFuture<int> fg_Suspend();

template <typename t_CValue> struct TCValues {
  t_CValue *m_pData;
  TCValues();
  TCValues(TCValues const &_Other);
  t_CValue &operator[](int _Index);
  void f_Clear();
  ~TCValues();
};

struct CEntry {
  int m_Value;
};

struct CItems {
  int m_Count;
};

template <typename t_CType> struct TCSharedPointer {
  t_CType *m_pValue;
  t_CType *operator->() const;
  t_CType &operator*() const;
};

template <typename t_CType> struct TCOptional {
  alignas(t_CType) unsigned char m_Storage[sizeof(t_CType)];
  bool m_bValid;
  t_CType *operator->();
  t_CType &operator*();
};

// Plain values.
struct CPlainState {
  int m_Value;
  int m_Other;
};

// What holds a collection of its own and a pointer it borrows.
struct CMixedView {
  int m_Value;
  TCValues<int> m_Scratch;
  CItems *m_pItems;
  CItems &f_Items() const { return *m_pItems; }
  TCValues<int> &f_Scratch() { return m_Scratch; }
};

// A wrapper of a collection, which is one too, and a pointer it borrows.
template <typename t_CValue> struct TCCounted {
  TCValues<t_CValue> m_Values;
  int *m_pCounter;
  void f_Clear();
};

// Views of pointers.
struct CPointerView {
  int *m_pValue;
};
struct CPointerPair {
  int *m_pValue;
  int *m_pGlobal;
};
inline int g_Global = 0;

template <typename t_CType> struct TCUniquePointer {
  TCUniquePointer();
  TCUniquePointer(TCUniquePointer const &) = delete;
  TCUniquePointer(TCUniquePointer &&_Other);
  ~TCUniquePointer();
  t_CType *operator->() const { return m_pValue; }
  void f_Clear();
  t_CType *m_pValue;
};

// A lookup that gives a handle that refers into the collection's storage.
template <typename t_CValue> struct TCResults {
  struct CResult {
    t_CValue &m_Value;
    bool m_bAdded;
    t_CValue &f_GetResult() const { return m_Value; }
  };
  t_CValue *m_pData;
  CResult operator()(int _Key) { return {m_pData[_Key], false}; }
  void f_Clear();
};

template <typename t_CType> t_CType const &fg_Const(t_CType &_Value) {
  return _Value;
}

// What only refers.
struct CItemsView {
  CItems *m_pItems;
  CItems &f_Get() const { return *m_pItems; }
};

struct CApp : CActor {
  CItems mp_Items;
  CPlainState mp_Plain;
  TCValues<int> mp_Values;
  TCValues<CEntry *> mp_Pointers;
  TCSharedPointer<CItems> mp_pShared;
  TCOptional<CItems> mp_OptionalItems;
  int m_Count;
  TCValues<int> m_Entries;
  TCActor<> mp_Other;

  TCFuture<int> f_PlainOwnSource() {
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &mp_Plain.m_Value] { return int(*pUse); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_PlainOwnCopy() {
    auto Copy = mp_Plain;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &Copy.m_Value] { return int(*pUse); });
  }
  TCFuture<int> f_CollectionOwnSource() {
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &mp_Values[0]] { return int(*pUse); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_CollectionOwnCopy() {
    auto Copy = mp_Values;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &Copy[0]] { return int(*pUse); });
  }
  TCFuture<int> f_CollectionOwnByAccessorSource() {
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &mp_Values.operator[](0)] { return int(*pUse); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_CollectionOwnByAccessorCopy() {
    auto Copy = mp_Values;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &Copy.operator[](0)] { return int(*pUse); });
  }
  TCFuture<int> f_PointersOwnSource() {
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &mp_Pointers[0]] { return int(int(*pUse != nullptr)); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_PointersOwnCopy() {
    auto Copy = mp_Pointers;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &Copy[0]] { return int(int(*pUse != nullptr)); });
  }
  TCFuture<int> f_MixedOwnSource() {
    CMixedView Source{0, {}, &mp_Items};
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &Source.m_Value] { return int(*pUse); });
  }
  TCFuture<int> f_MixedOwnCopy() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &Copy.m_Value] { return int(*pUse); });
  }
  TCFuture<int> f_MixedOwnStorageSource() {
    CMixedView Source{0, {}, &mp_Items};
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &Source.m_Scratch] { return int((*pUse)[0]); });
  }
  TCFuture<int> f_MixedOwnStorageCopy() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &Copy.m_Scratch] { return int((*pUse)[0]); });
  }
  TCFuture<int> f_MixedOwnByAccessorSource() {
    CMixedView Source{0, {}, &mp_Items};
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &Source.f_Scratch()] { return int((*pUse)[0]); });
  }
  TCFuture<int> f_MixedOwnByAccessorCopy() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &Copy.f_Scratch()] { return int((*pUse)[0]); });
  }
  TCFuture<int> f_MixedBorrowedSource() {
    CMixedView Source{0, {}, &mp_Items};
    co_return co_await (g_Dispatch(mp_Other) / [pUse = Source.m_pItems] { return int(pUse->m_Count); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_MixedBorrowedCopy() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = Copy.m_pItems] { return int(pUse->m_Count); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_MixedBorrowedByAccessorSource() {
    CMixedView Source{0, {}, &mp_Items};
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &Source.f_Items()] { return int(pUse->m_Count); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_MixedBorrowedByAccessorCopy() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &Copy.f_Items()] { return int(pUse->m_Count); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_SharedBorrowedSource() {
    co_return co_await (g_Dispatch(mp_Other) / [pUse = mp_pShared.operator->()] { return int(pUse->m_Count); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_SharedBorrowedCopy() {
    auto Copy = mp_pShared;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = Copy.operator->()] { return int(pUse->m_Count); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_SharedBorrowedByAccessorSource() {
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &*mp_pShared] { return int(pUse->m_Count); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_SharedBorrowedByAccessorCopy() {
    auto Copy = mp_pShared;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &*Copy] { return int(pUse->m_Count); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_OptionalOwnSource() {
    co_return co_await (g_Dispatch(mp_Other) / [pUse = mp_OptionalItems.operator->()] { return int(pUse->m_Count); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_OptionalOwnCopy() {
    auto Copy = mp_OptionalItems;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = Copy.operator->()] { return int(pUse->m_Count); });
  }
  TCFuture<int> f_OptionalOwnByAccessorSource() {
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &*mp_OptionalItems] { return int(pUse->m_Count); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_OptionalOwnByAccessorCopy() {
    auto Copy = mp_OptionalItems;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &*Copy] { return int(pUse->m_Count); });
  }
  TCFuture<int> f_ViewBorrowedSource() {
    CItemsView Source{&mp_Items};
    co_return co_await (g_Dispatch(mp_Other) / [pUse = Source.m_pItems] { return int(pUse->m_Count); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_ViewBorrowedCopy() {
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = Copy.m_pItems] { return int(pUse->m_Count); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_ViewBorrowedByAccessorSource() {
    CItemsView Source{&mp_Items};
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &Source.f_Get()] { return int(pUse->m_Count); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCFuture<int> f_ViewBorrowedByAccessorCopy() {
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    co_return co_await (g_Dispatch(mp_Other) / [pUse = &Copy.f_Get()] { return int(pUse->m_Count); });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
  TCResults<int> mp_Results;
  TCFuture<int> f_LookupResult() {
    co_return co_await (g_Dispatch(mp_Other) / [pValue = &mp_Results(0).f_GetResult()] { return *pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
  }
};

struct CInternalCopy : CActorInternal {
  int m_Value = 0;
  int *m_pPointer = nullptr;
  TCFuture<int> f_Run() {
    auto Copy = *this;
    co_return co_await (g_ConcurrentDispatch / [pCopy = &Copy] { return pCopy->m_Value; });
  }
};
} // namespace NCopies
