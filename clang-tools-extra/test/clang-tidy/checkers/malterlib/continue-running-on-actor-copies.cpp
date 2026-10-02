// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-continue-running-on-actor %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

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

  TCFuture<int> f_PlainOwnSourceDirect() {
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *(&mp_Plain.m_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_PlainOwnSourcePointer() {
    int *pUse = &mp_Plain.m_Value;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *pUse;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_PlainOwnCopyDirect() {
    auto Copy = mp_Plain;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *(&Copy.m_Value);
  }
  TCFuture<int> f_PlainOwnCopyPointer() {
    auto Copy = mp_Plain;
    int *pUse = &Copy.m_Value;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *pUse;
  }
  TCFuture<int> f_CollectionOwnSourceDirect() {
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *(&mp_Values[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_CollectionOwnSourcePointer() {
    int *pUse = &mp_Values[0];
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *pUse;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_CollectionOwnCopyDirect() {
    auto Copy = mp_Values;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *(&Copy[0]);
  }
  TCFuture<int> f_CollectionOwnCopyPointer() {
    auto Copy = mp_Values;
    int *pUse = &Copy[0];
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *pUse;
  }
  TCFuture<int> f_CollectionOwnByAccessorSourceDirect() {
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *(&mp_Values.operator[](0));
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_CollectionOwnByAccessorSourcePointer() {
    int *pUse = &mp_Values.operator[](0);
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *pUse;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_CollectionOwnByAccessorCopyDirect() {
    auto Copy = mp_Values;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *(&Copy.operator[](0));
  }
  TCFuture<int> f_CollectionOwnByAccessorCopyPointer() {
    auto Copy = mp_Values;
    int *pUse = &Copy.operator[](0);
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *pUse;
  }
  TCFuture<int> f_PointersOwnSourceDirect() {
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return int(*(&mp_Pointers[0]) != nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_PointersOwnSourcePointer() {
    CEntry * *pUse = &mp_Pointers[0];
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return int(*pUse != nullptr);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_PointersOwnCopyDirect() {
    auto Copy = mp_Pointers;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return int(*(&Copy[0]) != nullptr);
  }
  TCFuture<int> f_PointersOwnCopyPointer() {
    auto Copy = mp_Pointers;
    CEntry * *pUse = &Copy[0];
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return int(*pUse != nullptr);
  }
  TCFuture<int> f_MixedOwnSourceDirect() {
    CMixedView Source{0, {}, &mp_Items};
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *(&Source.m_Value);
  }
  TCFuture<int> f_MixedOwnSourcePointer() {
    CMixedView Source{0, {}, &mp_Items};
    int *pUse = &Source.m_Value;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *pUse;
  }
  TCFuture<int> f_MixedOwnCopyDirect() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *(&Copy.m_Value);
  }
  TCFuture<int> f_MixedOwnCopyPointer() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    int *pUse = &Copy.m_Value;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return *pUse;
  }
  TCFuture<int> f_MixedOwnStorageSourceDirect() {
    CMixedView Source{0, {}, &mp_Items};
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (*(&Source.m_Scratch))[0];
  }
  TCFuture<int> f_MixedOwnStorageSourcePointer() {
    CMixedView Source{0, {}, &mp_Items};
    TCValues<int> *pUse = &Source.m_Scratch;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (*pUse)[0];
  }
  TCFuture<int> f_MixedOwnStorageCopyDirect() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (*(&Copy.m_Scratch))[0];
  }
  TCFuture<int> f_MixedOwnStorageCopyPointer() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    TCValues<int> *pUse = &Copy.m_Scratch;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (*pUse)[0];
  }
  TCFuture<int> f_MixedOwnByAccessorSourceDirect() {
    CMixedView Source{0, {}, &mp_Items};
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (*(&Source.f_Scratch()))[0];
  }
  TCFuture<int> f_MixedOwnByAccessorSourcePointer() {
    CMixedView Source{0, {}, &mp_Items};
    TCValues<int> *pUse = &Source.f_Scratch();
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (*pUse)[0];
  }
  TCFuture<int> f_MixedOwnByAccessorCopyDirect() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (*(&Copy.f_Scratch()))[0];
  }
  TCFuture<int> f_MixedOwnByAccessorCopyPointer() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    TCValues<int> *pUse = &Copy.f_Scratch();
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (*pUse)[0];
  }
  TCFuture<int> f_MixedBorrowedSourceDirect() {
    CMixedView Source{0, {}, &mp_Items};
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (Source.m_pItems)->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_MixedBorrowedSourcePointer() {
    CMixedView Source{0, {}, &mp_Items};
    CItems *pUse = Source.m_pItems;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_MixedBorrowedCopyDirect() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (Copy.m_pItems)->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_MixedBorrowedCopyPointer() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    CItems *pUse = Copy.m_pItems;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_MixedBorrowedByAccessorSourceDirect() {
    CMixedView Source{0, {}, &mp_Items};
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (&Source.f_Items())->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_MixedBorrowedByAccessorSourcePointer() {
    CMixedView Source{0, {}, &mp_Items};
    CItems *pUse = &Source.f_Items();
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_MixedBorrowedByAccessorCopyDirect() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (&Copy.f_Items())->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_MixedBorrowedByAccessorCopyPointer() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    CItems *pUse = &Copy.f_Items();
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_SharedBorrowedSourceDirect() {
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (mp_pShared.operator->())->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_SharedBorrowedSourcePointer() {
    CItems *pUse = mp_pShared.operator->();
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_SharedBorrowedCopyDirect() {
    auto Copy = mp_pShared;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (Copy.operator->())->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_SharedBorrowedCopyPointer() {
    auto Copy = mp_pShared;
    CItems *pUse = Copy.operator->();
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_SharedBorrowedByAccessorSourceDirect() {
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (&*mp_pShared)->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_SharedBorrowedByAccessorSourcePointer() {
    CItems *pUse = &*mp_pShared;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_SharedBorrowedByAccessorCopyDirect() {
    auto Copy = mp_pShared;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (&*Copy)->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_SharedBorrowedByAccessorCopyPointer() {
    auto Copy = mp_pShared;
    CItems *pUse = &*Copy;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_OptionalOwnSourceDirect() {
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (mp_OptionalItems.operator->())->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_OptionalOwnSourcePointer() {
    CItems *pUse = mp_OptionalItems.operator->();
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_OptionalOwnCopyDirect() {
    auto Copy = mp_OptionalItems;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (Copy.operator->())->m_Count;
  }
  TCFuture<int> f_OptionalOwnCopyPointer() {
    auto Copy = mp_OptionalItems;
    CItems *pUse = Copy.operator->();
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
  }
  TCFuture<int> f_OptionalOwnByAccessorSourceDirect() {
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (&*mp_OptionalItems)->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_OptionalOwnByAccessorSourcePointer() {
    CItems *pUse = &*mp_OptionalItems;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_OptionalOwnByAccessorCopyDirect() {
    auto Copy = mp_OptionalItems;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (&*Copy)->m_Count;
  }
  TCFuture<int> f_OptionalOwnByAccessorCopyPointer() {
    auto Copy = mp_OptionalItems;
    CItems *pUse = &*Copy;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
  }
  TCFuture<int> f_ViewBorrowedSourceDirect() {
    CItemsView Source{&mp_Items};
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (Source.m_pItems)->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_ViewBorrowedSourcePointer() {
    CItemsView Source{&mp_Items};
    CItems *pUse = Source.m_pItems;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_ViewBorrowedCopyDirect() {
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (Copy.m_pItems)->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_ViewBorrowedCopyPointer() {
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    CItems *pUse = Copy.m_pItems;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_ViewBorrowedByAccessorSourceDirect() {
    CItemsView Source{&mp_Items};
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (&Source.f_Get())->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_ViewBorrowedByAccessorSourcePointer() {
    CItemsView Source{&mp_Items};
    CItems *pUse = &Source.f_Get();
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_ViewBorrowedByAccessorCopyDirect() {
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return (&Copy.f_Get())->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:{{[0-9]+}}: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCFuture<int> f_ViewBorrowedByAccessorCopyPointer() {
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    CItems *pUse = &Copy.f_Get();
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    co_return pUse->m_Count;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the object of the actor is used after 'fg_ContinueRunningOnActor' {{.*}}
  }
  TCUniquePointer<TCValues<int>> mp_pUniqueValues;
  TCFuture<int> f_UniqueMoved() {
    auto pValues = fg_Move(mp_pUniqueValues);
    auto BlockingActorCheckout = fg_BlockingActor();
    co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
    pValues->f_Clear();
    co_return 0;
  }
};
} // namespace NCopies
