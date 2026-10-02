// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-coroutine-actor-reference %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-actor-phase.h"

// A copy is new at its own level: its storage and what it holds by value are
// its own, which nothing of the actor changes. What its borrowed parts lead
// to, its references and pointers, and what a shared owner points to, is
// what the source's lead to, which the actor can change. A mixed object is
// both, by its members.

namespace NCopies {
TCFuture<int> fg_Work();

struct CEntry {
  int m_Value;
};

template <typename t_CValue> struct TCValues {
  t_CValue *m_pData;
  TCValues();
  TCValues(TCValues const &_Other);
  t_CValue &operator[](int _Index);
  t_CValue *begin();
  t_CValue *end();
  void f_Clear();
  ~TCValues();
};

struct CItems {
  int m_Count;
  CEntry *f_FindEqual(int _Key);
  CEntry &operator[](int _Key);
  void f_Clear();
};

template <typename t_CType> struct TCSharedPointer {
  t_CType *m_pValue;
  int *m_pReferences;
  t_CType *operator->() const;
  t_CType &operator*() const;
  ~TCSharedPointer() {
    if (m_pReferences && --*m_pReferences == 0)
      delete m_pValue;
  }
};

template <typename t_CType> struct TCUniquePointer {
  t_CType *m_pValue;
  TCUniquePointer();
  TCUniquePointer(TCUniquePointer const &) = delete;
  TCUniquePointer(TCUniquePointer &&_Other);
  t_CType *operator->() const;
  t_CType &operator*() const;
  ~TCUniquePointer();
};

// A collection that holds its storage in a class in it, like TCVector.
template <typename t_CData> struct TCVector {
  struct CVectorData {
    int m_Length;
    t_CData *f_GetData() { return (t_CData *)(this + 1); }
  };
  struct CStaticData {
    CVectorData *m_pData = nullptr;
  };
  TCVector();
  TCVector(TCVector const &_Other);
  ~TCVector();
  t_CData &operator[](int _Index) {
    return (mp_StaticData.m_pData->f_GetData())[_Index];
  }
  void f_Clear();
  CStaticData mp_StaticData;
};

// A collection whose getter gives the storage it owns.
template <typename t_CValue> struct TCArray {
  t_CValue *m_pData;
  t_CValue *f_GetArray() { return m_pData; }
  void f_Clear();
};

struct CArrayEntry {
  TCArray<int> m_Values;
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
  void f_Tidy(this CMixedView &_Self) { _Self.m_pItems->f_Clear(); }
  void f_TidyOwn(this CMixedView &_Self) { _Self.m_Scratch.f_Clear(); }
};

// What only refers.
struct CItemsView {
  CItems *m_pItems;
  CItems &f_Get() const { return *m_pItems; }
};

struct CSharedEntry {
  int m_Value;
  TCSharedPointer<CItems> m_pChildren;
};

template <typename t_CEntry> struct TCEntries {
  t_CEntry *f_FindEqual(int _Key);
  t_CEntry &operator[](int _Key);
};

// A guard that keeps a copy and changes it, or what it borrows, when it goes.
struct CMixedClearGuard {
  CMixedView m_View;
  ~CMixedClearGuard() { m_View.m_pItems->f_Clear(); }
};
struct CMixedOwnGuard {
  CMixedView m_View;
  ~CMixedOwnGuard() { m_View.m_Scratch.f_Clear(); }
};
inline void fg_GuardBorrowed(CItems &_Items) {
  CMixedView Source{0, {}, &_Items};
  CMixedClearGuard Guard{Source};
  (void)Guard;
}
inline void fg_GuardOwn(CItems &_Items) {
  CMixedView Source{0, {}, &_Items};
  CMixedOwnGuard Guard{Source};
  (void)Guard;
}

// A guard that keeps a copy of a shared owner, of an optional or of a view,
// and changes what it points to, or its own, when it goes.
struct CSharedClearGuard {
  TCSharedPointer<CItems> m_pItems;
  ~CSharedClearGuard() { m_pItems->f_Clear(); }
};
struct CSharedResetGuard {
  TCSharedPointer<CItems> m_pItems;
  ~CSharedResetGuard() { m_pItems = TCSharedPointer<CItems>{}; }
};
struct COptionalClearGuard {
  TCOptional<CItems> m_Items;
  ~COptionalClearGuard() { m_Items->f_Clear(); }
};
struct CViewClearGuard {
  CItemsView m_View;
  ~CViewClearGuard() { m_View.m_pItems->f_Clear(); }
};
inline void fg_GuardSharedBorrowed(TCSharedPointer<CItems> &_pItems) {
  CSharedClearGuard Guard{_pItems};
  (void)Guard;
}
inline void fg_GuardSharedOwn(TCSharedPointer<CItems> &_pItems) {
  CSharedResetGuard Guard{_pItems};
  (void)Guard;
}
inline void fg_GuardOptionalOwn(TCOptional<CItems> &_Items) {
  COptionalClearGuard Guard{_Items};
  (void)Guard;
}
inline void fg_GuardViewBorrowed(CItems &_Items) {
  CItemsView Source{&_Items};
  CViewClearGuard Guard{Source};
  (void)Guard;
}

// A view of values, to loop over.
struct CValuesView {
  TCValues<int> *m_pValues;
  TCValues<int> &f_Get() const { return *m_pValues; }
};

// An entry with a value of its own and a pointer it borrows, and accessors
// of each.
struct CPeerEntry {
  int m_Value;
  int *m_pPeer;
  int *f_Peer() const { return m_pPeer; }
  int f_Read() const { return *m_pPeer; }
  int f_Own() const { return m_Value; }
  int operator()(this CPeerEntry const &_Self) { return *_Self.m_pPeer; }
  int f_Size() const { return int(sizeof(m_pPeer)) + m_Value; }
};

// What holds a collection of its own and a pointer it borrows, which it
// clears only of its own.
struct CWrappedItems {
  TCValues<int> m_Values;
  CItems *m_pOther;
  int &operator[](int _Index) { return m_Values[_Index]; }
  void f_Clear() { m_Values.f_Clear(); }
};

// What holds a collection and a pointer, owned alone.
struct CState {
  CItems m_Items;
  CItems *m_pOther;
};

// A collection of pointers that copies them into its own storage, and frees
// only that.
template <typename t_CValue> struct TCDeepValues {
  t_CValue *m_pData;
  int m_nValues;
  TCDeepValues();
  TCDeepValues(TCDeepValues const &_Other)
      : m_pData(new t_CValue[_Other.m_nValues]), m_nValues(_Other.m_nValues) {}
  t_CValue &operator[](int _Index) { return m_pData[_Index]; }
  void f_Clear() {
    delete[] m_pData;
    m_pData = nullptr;
  }
};

// Helpers given a view by value and to const, which clear what it refers to,
// and one given a mixed object by value, which changes only its copy.
inline void fg_ClearView(CItemsView _View) { _View.m_pItems->f_Clear(); }
inline void fg_ClearViewConst(CItemsView const &_View) {
  _View.m_pItems->f_Clear();
}
inline void fg_ResetCopy(CMixedView _View) {
  _View.m_Scratch.f_Clear();
  _View.m_Value = 0;
}

// A range a coroutine gives by value, which refers to what it is made from.
template <typename t_CValue> struct TCRange {
  t_CValue *m_pBegin;
  t_CValue *m_pEnd;
  t_CValue *begin() const;
  t_CValue *end() const;
};

// A helper that gives an entry to a function through a pointer.
extern void (*g_fEntryHook)(CEntry &_Entry);
inline void fg_ApplyEntry(CEntry &_Entry) { g_fEntryHook(_Entry); }

// A structured binding of what refers to a collection.
struct CWrapper {
  CItems &m_Items;
};
inline void fg_ClearBound(CWrapper &_Wrapper) {
  auto &[Items] = _Wrapper;
  Items.f_Clear();
}
inline void fg_ForwardBound(CWrapper &_Wrapper) { fg_ClearBound(_Wrapper); }

struct CApp : CActor {
  CItems mp_Items;
  CPlainState mp_Plain;
  TCValues<int> mp_Values;
  TCValues<CEntry *> mp_Pointers;
  TCEntries<CSharedEntry> mp_SharedEntries;
  TCOptional<CItems> mp_OptionalItems;
  TCSharedPointer<TCValues<int>> mp_pSharedValues;
  TCOptional<TCValues<int>> mp_OptionalValues;
  TCEntries<CPeerEntry> mp_Peers;
  CWrappedItems mp_Wrapped;
  TCUniquePointer<CState> mp_pState;
  TCVector<int> mp_Vector;
  TCVector<CItems> mp_NestedVector;
  TCEntries<CArrayEntry> mp_ArrayEntries;
  TCEntries<TCOptional<CItemsView>> mp_OptionalViews;
  TCDeepValues<CEntry *> mp_DeepPointers;
  TCValues<CEntry> mp_Entries;
  TCFuture<TCRange<CEntry>> f_RangeAsync();


  // Plain.
  TCFuture<int> f_UsePlainOwn() {
    auto Copy = mp_Plain;
    int *pUse = &Copy.m_Value;
    co_await fg_Work();
    co_return *pUse;
  }
  TCFuture<int> f_ChangePlainOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = mp_Plain;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.m_Value = 1;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangePlainOwnCaptured() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = mp_Plain;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.m_Value = 1;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }

  // Collection.
  TCFuture<int> f_UseCollectionOwn() {
    auto Copy = mp_Values;
    int *pUse = &Copy[0];
    co_await fg_Work();
    co_return *pUse;
  }
  TCFuture<int> f_UseCollectionOwnByAccessor() {
    auto Copy = mp_Values;
    int *pUse = &Copy.operator[](0);
    co_await fg_Work();
    co_return *pUse;
  }
  TCFuture<int> f_ChangeCollectionOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = mp_Values;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeCollectionOwnCaptured() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = mp_Values;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }

  // Pointers.
  TCFuture<int> f_UsePointersOwn() {
    auto Copy = mp_Pointers;
    CEntry * *pUse = &Copy[0];
    co_await fg_Work();
    co_return *pUse != nullptr;
  }
  TCFuture<int> f_ChangePointersOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = mp_Pointers;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangePointersOwnCaptured() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = mp_Pointers;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }

  // Mixed.
  TCFuture<int> f_UseMixedOwn() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    int *pUse = &Copy.m_Value;
    co_await fg_Work();
    co_return *pUse;
  }
  TCFuture<int> f_UseMixedBorrowed() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    CEntry *pUse = Copy.m_pItems->f_FindEqual(0);
    co_await fg_Work();
    co_return pUse->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pUse' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_UseMixedBorrowedByAccessor() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    CEntry *pUse = Copy.f_Items().f_FindEqual(0);
    co_await fg_Work();
    co_return pUse->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pUse' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeMixedOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.m_Scratch.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeMixedBorrowed() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.m_pItems->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeMixedOwnByAccessor() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.f_Scratch().f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeMixedBorrowedByAccessor() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.f_Items().f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeMixedOwnCaptured() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.m_Scratch.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeMixedBorrowedCaptured() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.m_pItems->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeMixedOwnByAccessorCaptured() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.f_Scratch().f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeMixedBorrowedByAccessorCaptured() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.f_Items().f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }

  // Shared.
  TCFuture<int> f_UseSharedBorrowed() {
    auto Copy = mp_SharedEntries[0].m_pChildren;
    CEntry *pUse = Copy->f_FindEqual(0);
    co_await fg_Work();
    co_return pUse->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pUse' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_UseSharedBorrowedByAccessor() {
    auto Copy = mp_SharedEntries[0].m_pChildren;
    CEntry *pUse = (*Copy).f_FindEqual(0);
    co_await fg_Work();
    co_return pUse->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pUse' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeSharedOwn() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto Copy = mp_SharedEntries[0].m_pChildren;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      Copy = TCSharedPointer<CItems>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeSharedBorrowed() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto Copy = mp_SharedEntries[0].m_pChildren;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      Copy->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeSharedBorrowedByAccessor() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto Copy = mp_SharedEntries[0].m_pChildren;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      (*Copy).f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeSharedOwnCaptured() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto Copy = mp_SharedEntries[0].m_pChildren;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      Copy = TCSharedPointer<CItems>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeSharedBorrowedCaptured() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto Copy = mp_SharedEntries[0].m_pChildren;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      Copy->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeSharedBorrowedByAccessorCaptured() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto Copy = mp_SharedEntries[0].m_pChildren;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      (*Copy).f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }

  // Optional.
  TCFuture<int> f_UseOptionalOwn() {
    auto Copy = mp_OptionalItems;
    CEntry *pUse = Copy->f_FindEqual(0);
    co_await fg_Work();
    co_return pUse->m_Value;
  }
  TCFuture<int> f_ChangeOptionalOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = mp_OptionalItems;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeOptionalOwnByAccessor() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = mp_OptionalItems;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (*Copy).f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeOptionalOwnCaptured() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = mp_OptionalItems;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeOptionalOwnByAccessorCaptured() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = mp_OptionalItems;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (*Copy).f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }

  // Vector.
  TCFuture<int> f_UseVectorOwn() {
    auto Copy = mp_Vector;
    int *pUse = &Copy[0];
    co_await fg_Work();
    co_return *pUse;
  }
  TCFuture<int> f_ChangeVectorOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = mp_Vector;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeVectorOwnCaptured() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = mp_Vector;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }

  // View.
  TCFuture<int> f_UseViewBorrowed() {
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    CEntry *pUse = Copy.m_pItems->f_FindEqual(0);
    co_await fg_Work();
    co_return pUse->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pUse' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_UseViewBorrowedByAccessor() {
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    CEntry *pUse = Copy.f_Get().f_FindEqual(0);
    co_await fg_Work();
    co_return pUse->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pUse' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeViewBorrowed() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.m_pItems->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeViewBorrowedByAccessor() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.f_Get().f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeViewBorrowedCaptured() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.m_pItems->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeViewBorrowedByAccessorCaptured() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CItemsView Source{&mp_Items};
    auto Copy = Source;
    auto OnResume = co_await fg_OnResume([this, &pEntry, Copy]() mutable -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.f_Get().f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_LoopCollectionCopy() {
    auto Copy = mp_Values;
    int Total = 0;
    for (auto &Value : Copy) {
      co_await fg_Work();
      Total += sizeof(Value);
    }
    co_return Total;
  }
  TCFuture<int> f_LoopPointersCopy() {
    auto Copy = mp_Pointers;
    int Total = 0;
    for (auto &Value : Copy) {
      co_await fg_Work();
      Total += sizeof(Value);
    }
    co_return Total;
  }
  TCFuture<int> f_LoopMixedScratch() {
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    int Total = 0;
    for (auto &Value : Copy.m_Scratch) {
      co_await fg_Work();
      Total += Value;
    }
    co_return Total;
  }
  TCFuture<int> f_ChangeGuardBorrowed() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = 0;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardBorrowed(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeGuardOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = 0;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardOwn(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeGuardSharedBorrowed() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto Copy = 0;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      fg_GuardSharedBorrowed(mp_SharedEntries[0].m_pChildren);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeGuardSharedOwn() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto Copy = 0;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      fg_GuardSharedOwn(mp_SharedEntries[0].m_pChildren);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeGuardOptionalOwn() {
    CEntry *pEntry = mp_OptionalItems->f_FindEqual(0);
    auto Copy = 0;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_OptionalItems->f_FindEqual(0);
      fg_GuardOptionalOwn(mp_OptionalItems);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeGuardViewBorrowed() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = 0;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardViewBorrowed(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeSharedCopyDestroyed() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto Copy = 0;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      auto pCopy = mp_SharedEntries[0].m_pChildren;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeSharedMovedDestroyed() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto Copy = 0;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      auto pMoved = fg_Move(mp_SharedEntries[0].m_pChildren);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_LoopSharedBorrowed() {
    auto Copy = mp_pSharedValues;
    int Total = 0;
    for (int &Value : *Copy) {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: the loop goes on over the data of the actor after a suspension, {{.*}}
      co_await fg_Work();
      Total += Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'Value' refers into the data of the actor and is used after a suspension, {{.*}}
    }
    co_return Total;
  }
  TCFuture<int> f_LoopOptionalOwn() {
    auto Copy = mp_OptionalValues;
    int Total = 0;
    for (int &Value : *Copy) {
      co_await fg_Work();
      Total += Value;
    }
    co_return Total;
  }
  TCFuture<int> f_LoopViewBorrowed() {
    CValuesView Source{&mp_Values};
    auto Copy = Source;
    int Total = 0;
    for (int &Value : *Copy.m_pValues) {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: the loop goes on over the data of the actor after a suspension, {{.*}}
      co_await fg_Work();
      Total += Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'Value' refers into the data of the actor and is used after a suspension, {{.*}}
    }
    co_return Total;
  }
  TCFuture<int> f_LoopViewBorrowedByAccessor() {
    CValuesView Source{&mp_Values};
    auto Copy = Source;
    int Total = 0;
    for (int &Value : Copy.f_Get()) {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: the loop goes on over the data of the actor after a suspension, {{.*}}
      co_await fg_Work();
      Total += Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'Value' refers into the data of the actor and is used after a suspension, {{.*}}
    }
    co_return Total;
  }
  TCFuture<int> f_CaptureCopiedValue() {
    auto &Value = mp_SharedEntries[0].m_Value;
    auto fRead = [Value] { return Value; };
    co_await fg_Work();
    co_return fRead();
  }
  TCFuture<int> f_CallbackCopyBorrowedByAccessor() {
    auto &Entry = mp_Peers[0];
    auto OnResume0 = co_await fg_OnResume([Entry]() -> CExceptionPointer {
      if (*Entry.f_Peer() == 0)
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'Entry' refers into the data of the actor and is used after a suspension, {{.*}}
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return 0;
  }
  TCFuture<int> f_CallbackCopyBorrowedByMember() {
    auto &Entry = mp_Peers[0];
    auto OnResume0 = co_await fg_OnResume([Entry]() -> CExceptionPointer {
      if (Entry.f_Read() == 0)
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'Entry' refers into the data of the actor and is used after a suspension, {{.*}}
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return 0;
  }
  TCFuture<int> f_CallbackCopyOwnByMember() {
    auto &Entry = mp_Peers[0];
    auto OnResume0 = co_await fg_OnResume([Entry]() -> CExceptionPointer {
      if (Entry.f_Own() == 0)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return 0;
  }
  TCFuture<int> f_CallbackCopyThenReference() {
    auto &Entry = mp_Peers[0];
    auto OnResume0 = co_await fg_OnResume([Entry]() -> CExceptionPointer {
      if (Entry.m_Value == 0)
        return fg_MakeError("Gone");
      return nullptr;
    });
    auto OnResume1 = co_await fg_OnResume([&Entry]() -> CExceptionPointer {
      if (Entry.m_Value == 0)
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'Entry' refers into the data of the actor and is used after a suspension, {{.*}}
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return 0;
  }
  TCFuture<int> f_ChangeCopiedPointersCleared() {
    CEntry **pSlot = &mp_Pointers[0];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pSlot = &mp_Pointers[0];
      auto Copy = mp_Pointers;
      Copy.f_Clear();
      if (!pSlot)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pSlot != nullptr;
  }
  TCFuture<int> f_ChangeCopiedWrappedCleared() {
    int *pValue = &mp_Wrapped[0];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = &mp_Wrapped[0];
      auto Copy = mp_Wrapped;
      Copy.f_Clear();
      if (!pValue)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pValue;
  }
  TCFuture<int> f_ChangeSharedKeptFromEntry() {
    CSharedEntry *pShared = &mp_SharedEntries[0];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pShared = mp_SharedEntries.f_FindEqual(0);
      if (!pShared)
        return fg_MakeError("Gone");
      auto pKeep = pShared->m_pChildren;
      (void)pKeep;
      return nullptr;
    });
    co_await fg_Work();
    co_return pShared->m_Value;
  }
  TCFuture<int> f_UseNestedVectorCopy() {
    auto Copy = mp_NestedVector;
    CEntry *pUse = Copy[0].f_FindEqual(0);
    co_await fg_Work();
    co_return pUse->m_Value;
  }
  TCFuture<int> f_UseNestedVectorSource() {
    auto Copy = mp_NestedVector;
    CEntry *pUse = mp_NestedVector[0].f_FindEqual(0);
    co_await fg_Work();
    co_return pUse->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pUse' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeOwnedBuffer() {
    int *pValue = mp_ArrayEntries[0].m_Values.f_GetArray();
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = mp_ArrayEntries[0].m_Values.f_GetArray();
      mp_ArrayEntries[0].m_Values.f_Clear();
      if (!pValue)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_UseUniqueMovedOwn() {
    auto pState = fg_Move(mp_pState);
    CEntry *pUse = pState->m_Items.f_FindEqual(0);
    co_await fg_Work();
    co_return pUse->m_Value;
  }
  TCFuture<int> f_UseUniqueMovedBorrowed() {
    auto pState = fg_Move(mp_pState);
    CEntry *pUse = pState->m_pOther->f_FindEqual(0);
    co_await fg_Work();
    co_return pUse->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pUse' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_CallbackCopyBorrowedByExplicitOperator() {
    auto &Entry = mp_Peers[0];
    auto OnResume0 = co_await fg_OnResume([Entry]() -> CExceptionPointer {
      if (Entry() == 0)
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'Entry' refers into the data of the actor and is used after a suspension, {{.*}}
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return 0;
  }
  TCFuture<int> f_CallbackCopyOwnBySizeof() {
    auto &Entry = mp_Peers[0];
    auto OnResume0 = co_await fg_OnResume([Entry]() -> CExceptionPointer {
      if (Entry.f_Size() == 0)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return 0;
  }
  TCFuture<int> f_ChangeMixedBorrowedByExplicit() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeMixedOwnByExplicit() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = Source;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      Copy.f_TidyOwn();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeViewByValue() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CItemsView View{&mp_Items};
    auto Copy = 0;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearView(View);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeViewByConst() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CItemsView View{&mp_Items};
    auto Copy = 0;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearViewConst(View);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeMixedByValue() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CMixedView Source{0, {}, &mp_Items};
    auto Copy = 0;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ResetCopy(Source);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_ChangeDeepPointersCleared() {
    CEntry **pSlot = &mp_DeepPointers[0];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pSlot = &mp_DeepPointers[0];
      auto Copy = mp_DeepPointers;
      Copy.f_Clear();
      if (!pSlot)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pSlot != nullptr;
  }
  TCFuture<int> f_CallbackOptionalViewCopy() {
    auto &Entry = mp_OptionalViews[0];
    auto OnResume = co_await fg_OnResume([Entry]() mutable -> CExceptionPointer {
      if (Entry->m_pItems->m_Count == 0)
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'Entry' refers into the data of the actor and is used after a suspension, {{.*}}
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return 0;
  }
  TCFuture<int> f_LoopAwaitedRange() {
    int Total = 0;
    for (CEntry &Entry : co_await f_RangeAsync()) {
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: the loop goes on over the data of the actor after a suspension, {{.*}}
      co_await fg_Work();
      Total += Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'Entry' refers into the data of the actor and is used after a suspension, {{.*}}
    }
    co_return Total;
  }
  TCFuture<int> f_UseLocalGivenLater() {
    CMixedView Local{};
    Local.m_pItems = &mp_Items;
    int *pUse = &Local.m_Scratch[0];
    co_await fg_Work();
    co_return *pUse;
  }
  TCFuture<int> f_ChangeIndirectEntryHook() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto Copy = 0;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ApplyEntry(*pEntry);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
  TCFuture<int> f_ChangeStructuredBinding() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    CWrapper Wrapper{mp_Items};
    auto Copy = 0;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ForwardBound(Wrapper);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
  }
};

// A deep copy through a pointer into the data of the actor is its own: of a
// value whose strings share a buffer they copy themselves, by a type of their
// own, and whose objects are collections of nodes linked to each other, like
// a sorted JSON value of Malterlib, `auto Publish = *pPublish;`.
struct CDeepStrData {
  int m_RefCount;
  int m_Length;
};
struct CDeepStr {
  using CData = CDeepStrData;
  CDeepStr();
  CDeepStr(CDeepStr const &_Other);
  ~CDeepStr();
  bool operator==(CDeepStr const &_Other) const;
  CData *m_pData;
};
struct CTreeLink {
  CTreeLink *m_pChildren[2];
};
template <typename t_CKey, typename t_CValue> struct TCTreeMap {
  struct CNode {
    CTreeLink m_Link;
    t_CKey m_Key;
    t_CValue m_Value;
    t_CKey const &f_Name() const { return m_Key; }
    t_CValue &f_Value() { return m_Value; }
  };
  struct CTree {
    CTreeLink *m_pRoot;
  };
  TCTreeMap();
  TCTreeMap(TCTreeMap const &_Other);
  CNode *begin();
  CNode *end();
  void f_Clear();
  CTree mp_Tree;
};
struct CConfig {
  TCTreeMap<CDeepStr, CDeepStr> &f_Object();
  CConfig const *f_GetMember(char const *_pName) const;
  TCTreeMap<CDeepStr, CDeepStr> mp_Object;
};
struct CConfigApp : CActor {
  CConfig mp_Config;
  TCFuture<int> f_LoopOverCopy() {
    if (auto const *pPublish = mp_Config.f_GetMember("Publish")) {
      // Copy, the config can be reloaded while suspended.
      auto Publish = *pPublish;
      for (auto &Entry : Publish.f_Object()) {
        auto &Name = Entry.f_Name();
        auto &Host = Entry.f_Value();
        co_await fg_Work();
        if (Name == Host)
          co_return 1;
      }
    }
    co_return 0;
  }
  TCFuture<int> f_LoopOverCopyMember() {
    auto Copy = mp_Config;
    for (auto &Entry : Copy.f_Object()) {
      co_await fg_Work();
      if (Entry.f_Name() == Entry.f_Value())
        co_return 1;
    }
    co_return 0;
  }
};
} // namespace NCopies
