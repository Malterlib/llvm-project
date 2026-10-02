// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-coroutine-unsafe-this %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

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

struct CState {
  CItems mp_Items;
  CPlainState mp_Plain;
  TCValues<int> mp_Values;
  TCValues<CEntry *> mp_Pointers;
  TCSharedPointer<CItems> mp_pShared;
  TCOptional<CItems> mp_OptionalItems;
  int m_Count;
  TCValues<int> m_Entries;

  TCFuture<int> f_PlainOwnSourceCaptured() {
    return fg_CallSafe([pUse = &mp_Plain.m_Value]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pUse', which points to or into the object of 'CState', {{.*}}
      co_await fg_Suspend();
      co_return int(*pUse);
    });
  }
  TCFuture<int> f_PlainOwnSourceSuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_PlainOwnSourceSuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    int *pUse = &mp_Plain.m_Value;
    co_await fg_Suspend();
    co_return int(*pUse);
  }
  TCFuture<int> f_PlainOwnCopyCaptured() {
    auto Copy = mp_Plain;
    return fg_CallSafe([pUse = &Copy.m_Value]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(*pUse);
    });
  }
  TCFuture<int> f_PlainOwnCopySuspended()
  {
    auto Copy = mp_Plain;
    int *pUse = &Copy.m_Value;
    co_await fg_Suspend();
    co_return int(*pUse);
  }
  TCFuture<int> f_CollectionOwnSourceCaptured() {
    return fg_CallSafe([pUse = &mp_Values[0]]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(*pUse);
    });
  }
  TCFuture<int> f_CollectionOwnSourceSuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_CollectionOwnSourceSuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    int *pUse = &mp_Values[0];
    co_await fg_Suspend();
    co_return int(*pUse);
  }
  TCFuture<int> f_CollectionOwnCopyCaptured() {
    auto Copy = mp_Values;
    return fg_CallSafe([pUse = &Copy[0]]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(*pUse);
    });
  }
  TCFuture<int> f_CollectionOwnCopySuspended()
  {
    auto Copy = mp_Values;
    int *pUse = &Copy[0];
    co_await fg_Suspend();
    co_return int(*pUse);
  }
  TCFuture<int> f_CollectionOwnByAccessorSourceCaptured() {
    return fg_CallSafe([pUse = &mp_Values.operator[](0)]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(*pUse);
    });
  }
  TCFuture<int> f_CollectionOwnByAccessorSourceSuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_CollectionOwnByAccessorSourceSuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    int *pUse = &mp_Values.operator[](0);
    co_await fg_Suspend();
    co_return int(*pUse);
  }
  TCFuture<int> f_CollectionOwnByAccessorCopyCaptured() {
    auto Copy = mp_Values;
    return fg_CallSafe([pUse = &Copy.operator[](0)]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(*pUse);
    });
  }
  TCFuture<int> f_CollectionOwnByAccessorCopySuspended()
  {
    auto Copy = mp_Values;
    int *pUse = &Copy.operator[](0);
    co_await fg_Suspend();
    co_return int(*pUse);
  }
  TCFuture<int> f_PointersOwnSourceCaptured() {
    return fg_CallSafe([pUse = &mp_Pointers[0]]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(int(*pUse != nullptr));
    });
  }
  TCFuture<int> f_PointersOwnSourceSuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_PointersOwnSourceSuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    CEntry * *pUse = &mp_Pointers[0];
    co_await fg_Suspend();
    co_return int(int(*pUse != nullptr));
  }
  TCFuture<int> f_PointersOwnCopyCaptured() {
    auto Copy = mp_Pointers;
    return fg_CallSafe([pUse = &Copy[0]]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(int(*pUse != nullptr));
    });
  }
  TCFuture<int> f_PointersOwnCopySuspended()
  {
    auto Copy = mp_Pointers;
    CEntry * *pUse = &Copy[0];
    co_await fg_Suspend();
    co_return int(int(*pUse != nullptr));
  }
  TCFuture<int> f_MixedOwnSourceCaptured() {
    CMixedView Source{0, {}, &mp_Items};
    return fg_CallSafe([pUse = &Source.m_Value]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(*pUse);
    });
  }
  TCFuture<int> f_MixedOwnSourceSuspended()
// Clean: the pointer is to a member of a record the frame holds, not into the object.
  {
    CMixedView Source{0, {}, &mp_Items};
    int *pUse = &Source.m_Value;
    co_await fg_Suspend();
    co_return int(*pUse);
  }
  TCFuture<int> f_MixedOwnCopyCaptured() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    return fg_CallSafe([pUse = &Copy.m_Value]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(*pUse);
    });
  }
  TCFuture<int> f_MixedOwnCopySuspended()
// Clean: the pointer is to a member of a record the frame holds, not into the object.
  {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    int *pUse = &Copy.m_Value;
    co_await fg_Suspend();
    co_return int(*pUse);
  }
  TCFuture<int> f_MixedOwnStorageSourceCaptured() {
    CMixedView Source{0, {}, &mp_Items};
    return fg_CallSafe([pUse = &Source.m_Scratch]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int((*pUse)[0]);
    });
  }
  TCFuture<int> f_MixedOwnStorageSourceSuspended()
// Clean: the pointer is to a member of a record the frame holds, not into the object.
  {
    CMixedView Source{0, {}, &mp_Items};
    TCValues<int> *pUse = &Source.m_Scratch;
    co_await fg_Suspend();
    co_return int((*pUse)[0]);
  }
  TCFuture<int> f_MixedOwnStorageCopyCaptured() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    return fg_CallSafe([pUse = &Copy.m_Scratch]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int((*pUse)[0]);
    });
  }
  TCFuture<int> f_MixedOwnStorageCopySuspended()
// Clean: the pointer is to a member of a record the frame holds, not into the object.
  {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    TCValues<int> *pUse = &Copy.m_Scratch;
    co_await fg_Suspend();
    co_return int((*pUse)[0]);
  }
  TCFuture<int> f_MixedOwnByAccessorSourceCaptured() {
    CMixedView Source{0, {}, &mp_Items};
    return fg_CallSafe([pUse = &Source.f_Scratch()]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int((*pUse)[0]);
    });
  }
  TCFuture<int> f_MixedOwnByAccessorSourceSuspended()
// Clean: the pointer is to a member of a record the frame holds, not into the object.
  {
    CMixedView Source{0, {}, &mp_Items};
    TCValues<int> *pUse = &Source.f_Scratch();
    co_await fg_Suspend();
    co_return int((*pUse)[0]);
  }
  TCFuture<int> f_MixedOwnByAccessorCopyCaptured() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    return fg_CallSafe([pUse = &Copy.f_Scratch()]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int((*pUse)[0]);
    });
  }
  TCFuture<int> f_MixedOwnByAccessorCopySuspended()
// Clean: the pointer is to a member of a record the frame holds, not into the object.
  {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    TCValues<int> *pUse = &Copy.f_Scratch();
    co_await fg_Suspend();
    co_return int((*pUse)[0]);
  }
  TCFuture<int> f_MixedBorrowedSourceCaptured() {
    CMixedView Source{0, {}, &mp_Items};
    return fg_CallSafe([pUse = Source.m_pItems]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pUse', which points to or into the object of 'CState', {{.*}}
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_MixedBorrowedSourceSuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_MixedBorrowedSourceSuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    CMixedView Source{0, {}, &mp_Items};
    CItems *pUse = Source.m_pItems;
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_MixedBorrowedCopyCaptured() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    return fg_CallSafe([pUse = Copy.m_pItems]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pUse', which points to or into the object of 'CState', {{.*}}
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_MixedBorrowedCopySuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_MixedBorrowedCopySuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    CItems *pUse = Copy.m_pItems;
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_MixedBorrowedByAccessorSourceCaptured() {
    CMixedView Source{0, {}, &mp_Items};
    return fg_CallSafe([pUse = &Source.f_Items()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pUse', which points to or into the object of 'CState', {{.*}}
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_MixedBorrowedByAccessorSourceSuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_MixedBorrowedByAccessorSourceSuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    CMixedView Source{0, {}, &mp_Items};
    CItems *pUse = &Source.f_Items();
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_MixedBorrowedByAccessorCopyCaptured() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    return fg_CallSafe([pUse = &Copy.f_Items()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pUse', which points to or into the object of 'CState', {{.*}}
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_MixedBorrowedByAccessorCopySuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_MixedBorrowedByAccessorCopySuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    CItems *pUse = &Copy.f_Items();
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_SharedBorrowedSourceCaptured() {
    return fg_CallSafe([pUse = mp_pShared.operator->()]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_SharedBorrowedSourceSuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_SharedBorrowedSourceSuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    CItems *pUse = mp_pShared.operator->();
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_SharedBorrowedCopyCaptured() {
    auto Copy = mp_pShared;
    return fg_CallSafe([pUse = Copy.operator->()]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_SharedBorrowedCopySuspended()
  {
    auto Copy = mp_pShared;
    CItems *pUse = Copy.operator->();
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_SharedBorrowedByAccessorSourceCaptured() {
    return fg_CallSafe([pUse = &*mp_pShared]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_SharedBorrowedByAccessorSourceSuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_SharedBorrowedByAccessorSourceSuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    CItems *pUse = &*mp_pShared;
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_SharedBorrowedByAccessorCopyCaptured() {
    auto Copy = mp_pShared;
    return fg_CallSafe([pUse = &*Copy]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_SharedBorrowedByAccessorCopySuspended()
  {
    auto Copy = mp_pShared;
    CItems *pUse = &*Copy;
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_OptionalOwnSourceCaptured() {
    return fg_CallSafe([pUse = mp_OptionalItems.operator->()]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_OptionalOwnSourceSuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_OptionalOwnSourceSuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    CItems *pUse = mp_OptionalItems.operator->();
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_OptionalOwnCopyCaptured() {
    auto Copy = mp_OptionalItems;
    return fg_CallSafe([pUse = Copy.operator->()]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_OptionalOwnCopySuspended()
  {
    auto Copy = mp_OptionalItems;
    CItems *pUse = Copy.operator->();
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_OptionalOwnByAccessorSourceCaptured() {
    return fg_CallSafe([pUse = &*mp_OptionalItems]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_OptionalOwnByAccessorSourceSuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_OptionalOwnByAccessorSourceSuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    CItems *pUse = &*mp_OptionalItems;
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_OptionalOwnByAccessorCopyCaptured() {
    auto Copy = mp_OptionalItems;
    return fg_CallSafe([pUse = &*Copy]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_OptionalOwnByAccessorCopySuspended()
  {
    auto Copy = mp_OptionalItems;
    CItems *pUse = &*Copy;
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_ViewBorrowedSourceCaptured() {
    CItemsView Source{&mp_Items};
    return fg_CallSafe([pUse = Source.m_pItems]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pUse', which points to or into the object of 'CState', {{.*}}
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_ViewBorrowedSourceSuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_ViewBorrowedSourceSuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    CItemsView Source{&mp_Items};
    CItems *pUse = Source.m_pItems;
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_ViewBorrowedCopyCaptured() {
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    return fg_CallSafe([pUse = Copy.m_pItems]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pUse', which points to or into the object of 'CState', {{.*}}
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_ViewBorrowedCopySuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_ViewBorrowedCopySuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    CItems *pUse = Copy.m_pItems;
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_ViewBorrowedByAccessorSourceCaptured() {
    CItemsView Source{&mp_Items};
    return fg_CallSafe([pUse = &Source.f_Get()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pUse', which points to or into the object of 'CState', {{.*}}
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_ViewBorrowedByAccessorSourceSuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_ViewBorrowedByAccessorSourceSuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    CItemsView Source{&mp_Items};
    CItems *pUse = &Source.f_Get();
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_ViewBorrowedByAccessorCopyCaptured() {
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    return fg_CallSafe([pUse = &Copy.f_Get()]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pUse', which points to or into the object of 'CState', {{.*}}
      co_await fg_Suspend();
      co_return int(pUse->m_Count);
    });
  }
  TCFuture<int> f_ViewBorrowedByAccessorCopySuspended()
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: TCFuture coroutine 'f_ViewBorrowedByAccessorCopySuspended' is a member of 'CState', which is not an actor, and uses its object after a suspension, {{.*}}
  {
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    CItems *pUse = &Copy.f_Get();
    co_await fg_Suspend();
    co_return int(pUse->m_Count);
  }
  TCFuture<int> f_CountedCopyCaptured() {
    TCCounted<CItems> Source{{}, &m_Count};
    auto Copy = Source;
    return fg_CallSafe([pUse = Copy.m_pCounter]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pUse', which points to or into the object of 'CState', {{.*}}
      co_await fg_Suspend();
      co_return *pUse;
    });
  }
  TCFuture<int> f_ViewCastCaptured() {
    CPointerView View{&m_Count};
    return fg_CallSafe([pUse = static_cast<int const *>(View.m_pValue)]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pUse', which points to or into the object of 'CState', {{.*}}
      co_await fg_Suspend();
      co_return *pUse;
    });
  }
  TCFuture<int> f_PairGlobalCaptured() {
    CPointerPair Pair{&m_Count, &g_Global};
    return fg_CallSafe([pUse = Pair.m_pGlobal]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return *pUse;
    });
  }
  TCFuture<int> f_PairOwnCaptured() {
    CPointerPair Pair{&m_Count, &g_Global};
    return fg_CallSafe([pUse = Pair.m_pValue]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda captures 'pUse', which points to or into the object of 'CState', {{.*}}
      co_await fg_Suspend();
      co_return *pUse;
    });
  }
  TCFuture<int> f_ViewEntryCaptured() {
    CPointerView View{&m_Entries[0]};
    return fg_CallSafe([pUse = View.m_pValue]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return *pUse;
    });
  }
  TCFuture<int> f_ViewRefreshedCaptured() {
    CPointerView View{&m_Count};
    View.m_pValue = &g_Global;
    return fg_CallSafe([pUse = View.m_pValue]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return *pUse;
    });
  }
};

struct CValueState {
  int m_Value = 0;
  TCFuture<int> f_Run() {
    auto const &Self = fg_Const(*this);
    return fg_CallSafe([Self]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return Self.m_Value;
    });
  }
};
} // namespace NCopies
