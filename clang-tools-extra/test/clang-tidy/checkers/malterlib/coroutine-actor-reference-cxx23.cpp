// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-coroutine-actor-reference %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-actor-phase.h"

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

// A local of an fg_OnResume callback made by a lookup with an explicit object
// parameter, `f_FindEqual(this CApplications &_Self, ...)`, a plain call, is
// looked up each time the callback runs, like one with an implicit object.
namespace NExplicitObjectLookup {
TCFuture<int> fg_Work();
struct CVersion {
  int m_Value;
};
struct CApplication {
  TCMap<int, CVersion> m_Versions;
};
struct CApplications {
  CApplication *f_FindEqual(this CApplications &_Self, int _Key);
  void f_Remove(this CApplications &_Self, int _Key);
  void f_Tidy(this CApplications &_Self) { _Self.f_Remove(0); }
  void f_Reset(this CApplications &_Self) { _Self = CApplications{}; }
  void f_RemoveNothing(this CApplications &_Self) {}
  void operator()(this CApplications &_Self) { _Self.f_Remove(0); }
  void f_TidyAlias(this CApplications &_Self) {
    auto &Applications = _Self;
    Applications.f_Remove(0);
  }
};
struct CAssignedApplications {
  CApplication *f_FindEqual(this CAssignedApplications &_Self, int _Key);
  CAssignedApplications &operator=(this CAssignedApplications &_Self,
                                   CAssignedApplications const &_Other);
  CAssignedApplications &operator+=(this CAssignedApplications &_Self,
                                    int _Count);
  void operator()(this CAssignedApplications &_Self, int _Count);
  void f_ResetNamed(this CAssignedApplications &_Self) {
    _Self.operator=(CAssignedApplications{});
  }
  void f_Grow(this CAssignedApplications &_Self) { _Self += 1; }
  void f_Invoke(this CAssignedApplications &_Self) { _Self(1); }
};
// A member function with an explicit object called on the collection is told
// like one with an implicit object, its explicit object parameter the same as
// `this`: by its name, and by what its body does to its object.
struct CDelegatingApplications;
struct CReset {
  CReset(CDelegatingApplications &_Applications);
};
struct CDelegatingApplications {
  CApplication *m_pEntry;
  CApplication *f_FindEqual(this CDelegatingApplications &_Self, int _Key);
  void f_Remove(this CDelegatingApplications &_Self, int _Key);
  void f_NoOp(this CDelegatingApplications &_Self) {}
  void operator()(this CDelegatingApplications &_Self) { _Self.f_NoOp(); }
  void f_Delegates(this CDelegatingApplications &_Self) { _Self(); }
  void f_Recurses(this CDelegatingApplications &_Self, bool _bAgain) {
    if (_bAgain)
      _Self.f_Recurses(false);
  }
  void f_ByValue(this CDelegatingApplications _Self) { _Self.f_Remove(0); }
  void f_Copies(this CDelegatingApplications &_Self) { _Self.f_ByValue(); }
  void f_Tidy(this CDelegatingApplications &_Self) { _Self.f_Remove(0); }
  void f_TidiesThrough(this CDelegatingApplications &_Self) { _Self.f_Tidy(); }
  void f_Uncalled(this CDelegatingApplications &_Self) {
    auto fRemove = [&] { _Self.f_Remove(0); };
    (void)fRemove;
  }
  void f_RemovesThroughPointer(this CDelegatingApplications &_Self) {
    auto *pSelf = &_Self;
    pSelf->f_Remove(0);
  }
  void f_Deletes(this CDelegatingApplications &_Self) {
    delete _Self.m_pEntry;
  }
  void f_Constructs(this CDelegatingApplications &_Self) {
    CReset Reset(_Self);
  }
  void f_CallsLambda(this CDelegatingApplications &_Self) {
    auto fRemove = [&] { _Self.f_Remove(0); };
    fRemove();
  }
  void f_CallsLambdaDirectly(this CDelegatingApplications &_Self) {
    [&] { _Self.f_Remove(0); }();
  }
};
// The body of a free helper given the collection is looked into, also the
// body of each lambda it makes, called or not.
void fg_MakesRemover(CApplications &_Applications) {
  auto fRemove = [&] { _Applications.f_Remove(0); };
  (void)fRemove;
}
void fg_Looks(CApplications &_Applications) {
  (void)_Applications.f_FindEqual(0);
}
// A static `operator[]`, which has no object, isn't a lookup of what it is
// given, `m_Factory[mp_Applications]`; what it is given it looks up in, which
// doesn't change that.
struct CFactory {
  static CApplication *operator[](CApplications &_Applications) {
    return _Applications.f_FindEqual(0);
  }
};
// In it a member function called on the collection, also an operator, is
// told as if it were called in the callback: not a call operator whose body
// changes nothing, but one whose body removes, also with an explicit object
// parameter, a member operator whose body removes, an assignment, also by a
// const assignment operator, and deleting what an operator of it gives.
void fg_CallsOperator(CApplications &_Applications) { _Applications(); }
struct CImplicitApplications {
  CApplication *m_pApplication;
  CApplication *f_FindEqual(int _Key);
  void f_Clear();
  void operator()() {}
  CImplicitApplications &operator<<(int _Value) {
    f_Clear();
    return *this;
  }
  void operator=(CImplicitApplications const &_Other) const {
    delete m_pApplication;
  }
  CApplication *operator[](int _Key);
};
void fg_CallsImplicitOperator(CImplicitApplications &_Applications) {
  _Applications.operator()();
  _Applications();
}
void fg_Streams(CImplicitApplications &_Applications) { _Applications << 1; }
void fg_AssignsConst(CImplicitApplications &_Applications) {
  _Applications = CImplicitApplications{};
}
void fg_DeletesElement(CImplicitApplications &_Applications) {
  delete _Applications[0];
}
// Also cast, or as either value of a conditional.
struct CDerivedApplication : CApplication {};
void fg_DeletesCastElement(CImplicitApplications &_Applications) {
  delete static_cast<CDerivedApplication *>(_Applications[0]);
}
void fg_DeletesChosenElement(CImplicitApplications &_Applications,
                             bool _bFirst, CApplication *_pOther) {
  delete (_bFirst ? _pOther : _Applications[0]);
}
// Also through pointer arithmetic, or as the right of a comma.
void fg_DeletesOffsetElement(CImplicitApplications &_Applications) {
  delete (_Applications[0] + 0);
}
void fg_DeletesCommaElement(CImplicitApplications &_Applications) {
  delete ((void)0, _Applications[0]);
}
// A call operator with an implicit object, called as one or by name, is told
// like any other member function, by what it calls of its own; an assignment
// of an entry, or of an iterator made from the collection, doesn't change it.
struct CStepIterator {
  CStepIterator &operator+=(int _Steps);
};
struct CRemovingIterator {
  void f_Remove();
};
struct CClearingApplications {
  CRemovingIterator f_GetRemovingIterator(this CClearingApplications &_Self);
  CApplication *f_FindEqual(int _Key);
  int *f_FindValue(int _Key);
  CStepIterator f_GetIterator();
  void f_Clear();
  void operator()() { f_Clear(); }
};
void fg_CallsNamedOperator(CClearingApplications &_Applications) {
  _Applications.operator()();
}
void fg_CallsOperatorSyntax(CClearingApplications &_Applications) {
  _Applications();
}
void fg_WritesEntry(CClearingApplications &_Applications) {
  *_Applications.f_FindValue(1) = 42;
}
// Also through an iterator made by a member function with an explicit object
// parameter.
void fg_RemovesThroughIterator(CClearingApplications &_Applications) {
  auto iEntry = _Applications.f_GetRemovingIterator();
  iEntry.f_Remove();
}
void fg_StepsIterator(CClearingApplications &_Applications) {
  auto iEntry = _Applications.f_GetIterator();
  iEntry += 1;
}
// A member function with an implicit object is told the same way, by what its
// body does to `this`.
struct CImplicitShared;
void fg_UseShared(CImplicitShared &_Shared);
struct CImplicitShared {
  CApplication *f_FindEqual(int _Key);
  void f_Remove(int _Key);
  void operator()(int _Count);
  void f_Reset() { *this = CImplicitShared{}; }
  void f_Pass() { fg_UseShared(*this); }
  void f_Lambda() { [&] { f_Remove(0); }(); }
  void f_Uncalled() {
    auto fRemove = [&] { f_Remove(0); };
    (void)fRemove;
  }
  void f_Invoke() { (*this)(1); }
  CImplicitShared &operator<<(int _Value);
  void f_MovePointer() {
    auto *pSelf = this;
    auto *pCopy = fg_Move(pSelf);
    (void)pCopy;
  }
};
// In a helper's body, an operator without a body to look into can change it.
void fg_StreamsShared(CImplicitShared &_Shared) { _Shared << 42; }
// A pointer to it given by a reference to what isn't one of MoveFunctions or
// ForwardFunctions, which can change what it points to.
void fg_ResetPointer(CImplicitShared *&_pShared);
void fg_ReadPointer(CImplicitShared *const &_pShared);
void fg_PassesPointer(CImplicitShared &_Shared) {
  auto *pShared = &_Shared;
  fg_ResetPointer(pShared);
}
void fg_PassesConstPointer(CImplicitShared &_Shared) {
  auto *pShared = &_Shared;
  fg_ReadPointer(pShared);
}
// A member function of an entry changes the entry, not the collection.
struct CUpdatable {
  TCMap<int, CVersion> m_Versions;
  int m_Updates = 0;
  void f_Update() { m_Updates = 1; }
  void f_ResetVersions() { m_Versions = TCMap<int, CVersion>{}; }
};
struct CWrapper {
  CApplications m_Items;
};
// Moving the collection removes all its entries: given to fg_Move, cast to an
// rvalue reference, or the object of a member function qualified `&&`, like a
// destructive iteration, `fg_Move(mp_Items).f_Entries().
// f_GetIteratorDestructive()`, also in a helper, or a member function with an
// implicit or an explicit object.
template <typename t_CKey, typename t_CValue>
struct TCDestructiveMap {
  struct CIterator {
    explicit operator bool() const;
    void f_Next();
  };
  struct CEntries {
    CIterator f_GetIteratorDestructive() &&;
  };
  t_CValue *f_FindEqual(t_CKey const &_Key);
  CEntries f_Entries() &&;
  CIterator f_GetIteratorDestructive() &&;
  void f_DrainImplicit() {
    for (auto iEntry = fg_Move(*this).f_Entries().f_GetIteratorDestructive();
         iEntry; iEntry.f_Next())
      ;
  }
  void f_DrainExplicit(this TCDestructiveMap &_Self) {
    for (auto iEntry = fg_Move(_Self).f_Entries().f_GetIteratorDestructive();
         iEntry; iEntry.f_Next())
      ;
  }
};
template <typename t_CKey, typename t_CValue>
void fg_Drain(TCDestructiveMap<t_CKey, t_CValue> &_Items) {
  for (auto iEntry = fg_Move(_Items).f_Entries().f_GetIteratorDestructive();
       iEntry; iEntry.f_Next())
    ;
}
// The body of a member function, with an implicit or an explicit object, is
// looked into like a helper's, with its object as the collection: also
// through a member of it, an iterator made from it, a relocating call, an
// assignment by a const operator, a lambda run elsewhere, and the arguments
// of one with an explicit object other than its object, like a helper's.
template <typename t_FFunction>
void fg_Invoke(t_FFunction &&_fFunction);
struct CStorageItems {
  TCMap<int, CApplication> m_Storage;
  CApplication *f_FindEqual(int _Key);
  void f_ClearStorage() { m_Storage.f_Clear(); }
  void f_ClearStorageExplicit(this CStorageItems &_Self) {
    _Self.m_Storage.f_Clear();
  }
  void f_RemoveFirst(this CStorageItems &_Self) {
    auto iEntry = _Self.m_Storage.f_GetIterator();
    iEntry.f_Remove();
  }
  void f_InvokeClear() {
    fg_Invoke([&] { m_Storage.f_Clear(); });
  }
};
struct CGrowingItems {
  CApplication *f_FindEqual(int _Key);
  void f_Insert(this CGrowingItems &_Self, int _Value);
  void f_Grow(this CGrowingItems &_Self) { _Self.f_Insert(1); }
};
struct CConstAssignedItems {
  CApplication *f_FindEqual(int _Key);
  void operator=(CConstAssignedItems const &_Other) const;
  void f_Reset(this CConstAssignedItems &_Self) {
    _Self = CConstAssignedItems{};
  }
};
struct CProcessor {
  void f_Process(this CProcessor &_Self, CApplications &_Items) {
    _Items.f_Remove(0);
  }
};
// But a lookup, one of CallbackLookupFunctions, is told by its name only,
// whatever its body does.
struct CVisibleLookupItems;
void fg_Locate(CVisibleLookupItems &_Items);
struct CVisibleLookupItems {
  CApplication *m_pApplication;
  CApplication *f_FindEqual(int _Key) {
    fg_Locate(*this);
    return m_pApplication;
  }
};
struct CApp : CActor {
  CWrapper mp_Wrapper;
  TCMap<int, CVersion> mp_Other;
  TCMap<int, CApplication> mp_ImplicitItems;
  TCMap<int, CUpdatable> mp_Updatables;
  // A member function of the actor itself is gone through for the
  // collections it changes, not as a collection.
  void f_TidyOther() { mp_Other.f_Clear(); }
  void f_TidyImplicitItems() { mp_ImplicitItems.f_Clear(); }
  CVisibleLookupItems mp_VisibleLookup;
  CStorageItems mp_Storage;
  CGrowingItems mp_Growing;
  CConstAssignedItems mp_ConstAssigned;
  CProcessor m_Processor;
  CImplicitShared mp_Shared;
  TCDestructiveMap<int, CApplication> mp_Destructive;
  CApplications mp_Applications;
  CAssignedApplications mp_Assigned;
  CDelegatingApplications mp_Delegating;
  CFactory m_Factory;
  CImplicitApplications mp_Implicit;
  CClearingApplications mp_Clearing;
  TCFuture<int> f_Named() {
    CVersion *pVersion = nullptr;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto *pApplication = mp_Applications.f_FindEqual(0);
      pVersion = pApplication->m_Versions.f_FindEqual(1);
      if (!pVersion)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pVersion->m_Value;
  }
  TCFuture<int> f_StaticSubscript() {
    CVersion *pVersion = nullptr;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto *pApplication = m_Factory[mp_Applications];
      pVersion = pApplication->m_Versions.f_FindEqual(1);
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      if (!pVersion)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pVersion->m_Value;
  }
  // Also assigned to a pointer from before the suspension, which an
  // explicit-object lookup looks up again like any other, and doesn't remove
  // anything, unlike one named as removing, `f_Remove`.
  TCFuture<int> f_Assigned() {
    CApplication *pApplication = mp_Applications.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Applications.f_FindEqual(0);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
  }
  TCFuture<int> f_Removes() {
    CApplication *pApplication = mp_Applications.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Applications.f_FindEqual(0);
      mp_Applications.f_Remove(1);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A member function with an explicit object whose body calls one that
  // removes on its object removes.
  TCFuture<int> f_Tidies() {
    CApplication *pApplication = mp_Applications.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Applications.f_FindEqual(0);
      mp_Applications.f_Tidy();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Also one whose body assigns its object, or removes through an alias of
  // it.
  TCFuture<int> f_Resets() {
    CApplication *pApplication = mp_Applications.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Applications.f_FindEqual(0);
      mp_Applications.f_Reset();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_TidiesAlias() {
    CApplication *pApplication = mp_Applications.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Applications.f_FindEqual(0);
      mp_Applications.f_TidyAlias();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // But one named as removing does, whatever its body does.
  TCFuture<int> f_RemovesNothing() {
    CApplication *pApplication = mp_Applications.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Applications.f_FindEqual(0);
      mp_Applications.f_RemoveNothing();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Also an operator with an explicit object, `mp_Applications()`, whose
  // body removes.
  TCFuture<int> f_CallsOperator() {
    CApplication *pApplication = mp_Applications.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Applications.f_FindEqual(0);
      mp_Applications();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A free helper that makes a lambda that removes, though it doesn't call it.
  TCFuture<int> f_HelperMakesRemover() {
    CApplication *pApplication = mp_Applications.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Applications.f_FindEqual(0);
      fg_MakesRemover(mp_Applications);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // But not one that only looks up.
  TCFuture<int> f_HelperLooks() {
    CApplication *pApplication = mp_Applications.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Applications.f_FindEqual(0);
      fg_Looks(mp_Applications);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
  }
  // A helper's call of an operator, the same as one in the callback, here
  // one whose body removes.
  TCFuture<int> f_HelperCallsOperators() {
    CApplication *pApplication = mp_Applications.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Applications.f_FindEqual(0);
      fg_CallsOperator(mp_Applications);
      mp_Applications();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_HelperCallsImplicitOperators() {
    CApplication *pApplication = mp_Implicit.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Implicit.f_FindEqual(0);
      fg_CallsImplicitOperator(mp_Implicit);
      mp_Implicit();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
  }
  // But a member operator whose body removes does, also an assignment, also by a const assignment operator, and deleting what an operator gives.
  TCFuture<int> f_HelperStreams() {
    CApplication *pApplication = mp_Implicit.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Implicit.f_FindEqual(0);
      fg_Streams(mp_Implicit);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Streams() {
    CApplication *pApplication = mp_Implicit.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Implicit.f_FindEqual(0);
      mp_Implicit << 1;
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_HelperAssignsConst() {
    CApplication *pApplication = mp_Implicit.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Implicit.f_FindEqual(0);
      fg_AssignsConst(mp_Implicit);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_AssignsConst() {
    CApplication *pApplication = mp_Implicit.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Implicit.f_FindEqual(0);
      mp_Implicit = CImplicitApplications{};
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_HelperDeletesElement() {
    CApplication *pApplication = mp_Implicit.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Implicit.f_FindEqual(0);
      fg_DeletesElement(mp_Implicit);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_HelperDeletesCastElement() {
    CApplication *pApplication = mp_Implicit.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Implicit.f_FindEqual(0);
      fg_DeletesCastElement(mp_Implicit);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_HelperDeletesChosenElement() {
    CApplication *pApplication = mp_Implicit.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Implicit.f_FindEqual(0);
      fg_DeletesChosenElement(mp_Implicit, true, nullptr);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_HelperDeletesOffsetElement() {
    CApplication *pApplication = mp_Implicit.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Implicit.f_FindEqual(0);
      fg_DeletesOffsetElement(mp_Implicit);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_HelperDeletesCommaElement() {
    CApplication *pApplication = mp_Implicit.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Implicit.f_FindEqual(0);
      fg_DeletesCommaElement(mp_Implicit);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_CallsNamedOperator() {
    CApplication *pApplication = mp_Clearing.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Clearing.f_FindEqual(0);
      mp_Clearing.operator()();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_HelperCallsNamedOperator() {
    CApplication *pApplication = mp_Clearing.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Clearing.f_FindEqual(0);
      fg_CallsNamedOperator(mp_Clearing);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_CallsOperatorSyntax() {
    CApplication *pApplication = mp_Clearing.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Clearing.f_FindEqual(0);
      mp_Clearing();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_HelperCallsOperatorSyntax() {
    CApplication *pApplication = mp_Clearing.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Clearing.f_FindEqual(0);
      fg_CallsOperatorSyntax(mp_Clearing);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_HelperRemovesThroughIterator() {
    CApplication *pApplication = mp_Clearing.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Clearing.f_FindEqual(0);
      fg_RemovesThroughIterator(mp_Clearing);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_HelperWritesEntry() {
    CApplication *pApplication = mp_Clearing.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Clearing.f_FindEqual(0);
      fg_WritesEntry(mp_Clearing);
      fg_StepsIterator(mp_Clearing);
      *mp_Clearing.f_FindValue(1) = 42;
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
  }
  // Not a member function of the actor that changes another collection, but one that changes this one.
  TCFuture<int> f_TidiesOther() {
    CApplication *pApplication = mp_ImplicitItems.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_ImplicitItems.f_FindEqual(0);
      f_TidyOther();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
  }
  TCFuture<int> f_TidiesImplicitItems() {
    CApplication *pApplication = mp_ImplicitItems.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_ImplicitItems.f_FindEqual(0);
      f_TidyImplicitItems();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Moving a collection that is a member of one of the actor, counted against that one.
  TCFuture<int> f_MovesWrapped() {
    CApplication *pApplication = mp_Wrapper.m_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Wrapper.m_Items.f_FindEqual(0);
      (void)static_cast<CApplications &&>(mp_Wrapper.m_Items);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Not moving a pointer to it.
  TCFuture<int> f_SharedMovePointer() {
    CApplication *pApplication = mp_Shared.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Shared.f_FindEqual(0);
      mp_Shared.f_MovePointer();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
  }
  // An operator without a body in a helper changes it, unlike one in the callback.
  TCFuture<int> f_StreamsShared() {
    CApplication *pApplication = mp_Shared.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Shared.f_FindEqual(0);
      fg_StreamsShared(mp_Shared);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_StreamsSharedDirectly() {
    CApplication *pApplication = mp_Shared.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Shared.f_FindEqual(0);
      mp_Shared << 42;
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
  }
  // A pointer to it given by a reference to a function that can change what it points to, also to const.
  TCFuture<int> f_PassesPointer() {
    CApplication *pApplication = mp_Shared.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Shared.f_FindEqual(0);
      fg_PassesPointer(mp_Shared);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_PassesConstPointer() {
    CApplication *pApplication = mp_Shared.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Shared.f_FindEqual(0);
      fg_PassesConstPointer(mp_Shared);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Moving a collection through an alias of the object.
  TCFuture<int> f_MovesThroughAlias() {
    CApplication *pApplication = mp_ImplicitItems.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_ImplicitItems.f_FindEqual(0);
      auto *pThis = this; (void)fg_Move(pThis->mp_ImplicitItems);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Not a member function of an entry that changes the entry.
  TCFuture<int> f_UpdatesEntry() {
    CUpdatable *pUpdatable = mp_Updatables.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pUpdatable = mp_Updatables.f_FindEqual(0);
      mp_Updatables[1].f_Update();
      if (!pUpdatable)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pUpdatable->m_Versions.f_FindEqual(1)->m_Value;
  }
  // But one that assigns a collection in the entry changes what is looked up
  // in that collection, though not the entry itself.
  TCFuture<int> f_ResetsEntryCollection() {
    CVersion *pVersion = mp_Updatables.f_FindEqual(0)->m_Versions.f_FindEqual(1);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pVersion = mp_Updatables.f_FindEqual(0)->m_Versions.f_FindEqual(1);
      mp_Updatables[1].f_ResetVersions();
      if (!pVersion)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pVersion->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pVersion' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Not a lookup whose body gives its object on.
  TCFuture<int> f_VisibleLookup() {
    CApplication *pApplication = mp_VisibleLookup.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_VisibleLookup.f_FindEqual(0);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
  }
  // Through a member of its object, with an implicit or an explicit object.
  TCFuture<int> f_ClearsStorage() {
    CApplication *pApplication = mp_Storage.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Storage.f_FindEqual(0);
      mp_Storage.f_ClearStorage();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_ClearsStorageExplicit() {
    CApplication *pApplication = mp_Storage.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Storage.f_FindEqual(0);
      mp_Storage.f_ClearStorageExplicit();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Through an iterator made from it.
  TCFuture<int> f_RemovesFirst() {
    CApplication *pApplication = mp_Storage.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Storage.f_FindEqual(0);
      mp_Storage.f_RemoveFirst();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // In a lambda that is run elsewhere.
  TCFuture<int> f_InvokesClear() {
    CApplication *pApplication = mp_Storage.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Storage.f_FindEqual(0);
      mp_Storage.f_InvokeClear();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // By a relocating call on its explicit object.
  TCFuture<int> f_GrowsExplicit() {
    CApplication *pApplication = mp_Growing.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Growing.f_FindEqual(0);
      mp_Growing.f_Grow();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // By an assignment operator that is const.
  TCFuture<int> f_ResetsConst() {
    CApplication *pApplication = mp_ConstAssigned.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_ConstAssigned.f_FindEqual(0);
      mp_ConstAssigned.f_Reset();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // An argument of one with an explicit object other than its object.
  TCFuture<int> f_Processes() {
    CApplication *pApplication = mp_Applications.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Applications.f_FindEqual(0);
      m_Processor.f_Process(mp_Applications);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // One with an implicit object whose body assigns its object, gives it on, calls a lambda that removes, or calls its call operator without a body.
  TCFuture<int> f_SharedReset() {
    CApplication *pApplication = mp_Shared.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Shared.f_FindEqual(0);
      mp_Shared.f_Reset();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_SharedPass() {
    CApplication *pApplication = mp_Shared.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Shared.f_FindEqual(0);
      mp_Shared.f_Pass();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_SharedLambda() {
    CApplication *pApplication = mp_Shared.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Shared.f_FindEqual(0);
      mp_Shared.f_Lambda();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_SharedInvoke() {
    CApplication *pApplication = mp_Shared.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Shared.f_FindEqual(0);
      mp_Shared.f_Invoke();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Also one that only makes a lambda that would, which the analysis of a
  // body counts, called or not.
  TCFuture<int> f_SharedUncalled() {
    CApplication *pApplication = mp_Shared.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Shared.f_FindEqual(0);
      mp_Shared.f_Uncalled();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Moving the collection, iterating it destructively.
  TCFuture<int> f_DrainDirect() {
    CApplication *pApplication = mp_Destructive.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Destructive.f_FindEqual(0);
      for (auto iEntry = fg_Move(mp_Destructive).f_Entries().f_GetIteratorDestructive(); iEntry; iEntry.f_Next());
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DrainSet() {
    CApplication *pApplication = mp_Destructive.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Destructive.f_FindEqual(0);
      for (auto iEntry = fg_Move(mp_Destructive).f_GetIteratorDestructive(); iEntry; iEntry.f_Next());
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DrainCast() {
    CApplication *pApplication = mp_Destructive.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Destructive.f_FindEqual(0);
      for (auto iEntry = static_cast<TCDestructiveMap<int, CApplication> &&>(mp_Destructive).f_Entries().f_GetIteratorDestructive(); iEntry; iEntry.f_Next());
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DrainHelper() {
    CApplication *pApplication = mp_Destructive.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Destructive.f_FindEqual(0);
      fg_Drain(mp_Destructive);
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DrainImplicit() {
    CApplication *pApplication = mp_Destructive.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Destructive.f_FindEqual(0);
      mp_Destructive.f_DrainImplicit();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DrainExplicit() {
    CApplication *pApplication = mp_Destructive.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Destructive.f_FindEqual(0);
      mp_Destructive.f_DrainExplicit();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Also one whose body calls a member function on its object that does,
  // `f_TidiesThrough` of `f_Tidy`, though not one that calls itself, takes a
  // copy, calls an operator that doesn't, or only makes a lambda that would.
  TCFuture<int> f_Delegates() {
    CApplication *pApplication = mp_Delegating.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Delegating.f_FindEqual(0);
      mp_Delegating();
      mp_Delegating.f_Recurses(true);
      mp_Delegating.f_Copies();
      mp_Delegating.f_TidiesThrough();
      mp_Delegating.f_Uncalled();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // But not one whose body calls its call operator whose body doesn't.
  TCFuture<int> f_CallsOwnOperator() {
    CApplication *pApplication = mp_Delegating.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Delegating.f_FindEqual(0);
      mp_Delegating.f_Delegates();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
  }
  // But one named as removing does, also as deleting, whatever its body does.
  TCFuture<int> f_RemovesThroughPointer() {
    CApplication *pApplication = mp_Delegating.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Delegating.f_FindEqual(0);
      mp_Delegating.f_RemovesThroughPointer();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Deletes() {
    CApplication *pApplication = mp_Delegating.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Delegating.f_FindEqual(0);
      mp_Delegating.f_Deletes();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Also one whose body gives its object to a constructor, or calls a lambda
  // that removes, also directly.
  TCFuture<int> f_Constructs() {
    CApplication *pApplication = mp_Delegating.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Delegating.f_FindEqual(0);
      mp_Delegating.f_Constructs();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_CallsLambda() {
    CApplication *pApplication = mp_Delegating.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Delegating.f_FindEqual(0);
      mp_Delegating.f_CallsLambda();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_CallsLambdaDirectly() {
    CApplication *pApplication = mp_Delegating.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Delegating.f_FindEqual(0);
      mp_Delegating.f_CallsLambdaDirectly();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Also an assignment by an explicit-object operator= that can't be looked
  // into, also called by name.
  TCFuture<int> f_AssignsByName() {
    CApplication *pApplication = mp_Assigned.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Assigned.f_FindEqual(0);
      mp_Assigned.operator=(CAssignedApplications{});
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Also one whose body assigns its object, also compound or by name, or calls
  // its call operator, which has no body to look into.
  TCFuture<int> f_ResetsByName() {
    CApplication *pApplication = mp_Assigned.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Assigned.f_FindEqual(0);
      mp_Assigned.f_ResetNamed();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Grows() {
    CApplication *pApplication = mp_Assigned.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Assigned.f_FindEqual(0);
      mp_Assigned.f_Grow();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Invokes() {
    CApplication *pApplication = mp_Assigned.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pApplication = mp_Assigned.f_FindEqual(0);
      mp_Assigned.f_Invoke();
      if (!pApplication)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pApplication->m_Versions.f_FindEqual(1)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pApplication' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}
